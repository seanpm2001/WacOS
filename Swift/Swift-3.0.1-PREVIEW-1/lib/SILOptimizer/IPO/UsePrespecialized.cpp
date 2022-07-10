//===--- UsePrespecialized.cpp - use pre-specialized functions ------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "use-prespecialized"
#include "swift/Basic/Demangle.h"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "swift/SIL/Mangle.h"
#include "swift/SIL/SILBuilder.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILModule.h"
#include "llvm/Support/Debug.h"
#include "swift/SILOptimizer/Utils/Generics.h"

using namespace swift;

namespace {



static void collectApplyInst(SILFunction &F,
                             llvm::SmallVectorImpl<ApplySite> &NewApplies) {
  // Scan all of the instructions in this function in search of ApplyInsts.
  for (auto &BB : F)
    for (auto &I : BB)
      if (ApplySite AI = ApplySite::isa(&I))
        NewApplies.push_back(AI);
}

/// A simple pass which replaces each apply of a generic function by an apply
/// of the corresponding pre-specialized function, if such a pre-specialization
/// exists.
class UsePrespecialized: public SILModuleTransform {
  virtual ~UsePrespecialized() { }

  void run() override {
    auto &M = *getModule();
    for (auto &F : M) {
      if (replaceByPrespecialized(F)) {
        invalidateAnalysis(&F, SILAnalysis::InvalidationKind::Everything);
      }
    }
  }

  StringRef getName() override {
    return "Use pre-specialized versions of functions";
  }

  bool replaceByPrespecialized(SILFunction &F);
};

// Analyze the function and replace each apply of
// a generic function by an apply of the corresponding
// pre-specialized function, if such a pre-specialization exists.
bool UsePrespecialized::replaceByPrespecialized(SILFunction &F) {
  bool Changed = false;
  auto &M = F.getModule();

  llvm::SmallVector<ApplySite, 16> NewApplies;
  collectApplyInst(F, NewApplies);

  for (auto &AI : NewApplies) {
    auto *ReferencedF = AI.getReferencedFunction();
    if (!ReferencedF)
      continue;

    DEBUG(llvm::dbgs() << "Trying to use specialized function for:\n";
          AI.getInstruction()->dumpInContext());

    // Check if it is a call of a generic function.
    // If this is the case, check if there is a specialization
    // available for it already and use this specialization
    // instead of the generic version.

    ArrayRef<Substitution> Subs = AI.getSubstitutions();
    if (Subs.empty())
      continue;

    ReabstractionInfo ReInfo(ReferencedF, Subs);

    auto SpecType = ReInfo.getSpecializedType();
    if (!SpecType)
      continue;

    // Bail if any generic types parameters of the concrete type
    // are unbound.
    if (SpecType->hasArchetype())
      continue;

    // Bail if any generic types parameters of the concrete type
    // are unbound.
    if (hasUnboundGenericTypes(Subs))
      continue;

    // Create a name of the specialization.
    std::string ClonedName;
    {
      Mangle::Mangler Mangler;
      GenericSpecializationMangler GenericMangler(Mangler, ReferencedF, Subs,
                                                  ReferencedF->isFragile());
      GenericMangler.mangle();
      ClonedName = Mangler.finalize();
    }

    SILFunction *NewF = nullptr;
    // If we already have this specialization, reuse it.
    auto PrevF = M.lookUpFunction(ClonedName);
    if (PrevF) {
      DEBUG(llvm::dbgs() << "Found a specialization: " << ClonedName << "\n");
      if (PrevF->getLinkage() != SILLinkage::SharedExternal)
        NewF = PrevF;
      else {
        DEBUG(llvm::dbgs() << "Wrong linkage: " << (int)PrevF->getLinkage()
                           << "\n");
      }
    }

    if (!PrevF || !NewF) {
      // Check for the existence of this function in another module without
      // loading the function body.
      PrevF = lookupPrespecializedSymbol(M, ClonedName);
      DEBUG(llvm::dbgs()
            << "Checked if there is a specialization in a different module: "
            << PrevF << "\n");
      if (!PrevF)
        continue;
      assert(PrevF->isExternalDeclaration() &&
             "Prespecialized function should be an external declaration");
      NewF = PrevF;
    }

    if (!NewF)
      continue;

    // An existing specialization was found.
    DEBUG(llvm::dbgs() << "Found a specialization of " << ReferencedF->getName()
                       << " : " << NewF->getName() << "\n");

    auto NewAI = replaceWithSpecializedFunction(AI, NewF, ReInfo);
    AI.getInstruction()->replaceAllUsesWith(NewAI.getInstruction());
    recursivelyDeleteTriviallyDeadInstructions(AI.getInstruction(), true);
    Changed = true;
  }

  return Changed;
}

} // end anonymous namespace


SILTransform *swift::createUsePrespecialized() {
  return new UsePrespecialized();
}
