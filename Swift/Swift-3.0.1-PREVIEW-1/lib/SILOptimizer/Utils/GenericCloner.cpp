//===--- GenericCloner.cpp - Specializes generic functions  ---------------===//
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

#include "swift/SILOptimizer/Utils/GenericCloner.h"

#include "swift/AST/Type.h"
#include "swift/SIL/SILBasicBlock.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILValue.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

using namespace swift;

/// Create a new empty function with the correct arguments and a unique name.
SILFunction *GenericCloner::initCloned(SILFunction *Orig,
                                       IsFragile_t Fragile,
                                       const ReabstractionInfo &ReInfo,
                                       StringRef NewName) {
  assert((!Fragile || Orig->isFragile())
         && "Specialization cannot make body more resilient");
  assert((Orig->isTransparent() || Orig->isBare() || Orig->getLocation())
         && "SILFunction missing location");
  assert((Orig->isTransparent() || Orig->isBare() || Orig->getDebugScope())
         && "SILFunction missing DebugScope");
  assert(!Orig->isGlobalInit() && "Global initializer cannot be cloned");

  // Create a new empty function.
  SILFunction *NewF = Orig->getModule().createFunction(
      getSpecializedLinkage(Orig, Orig->getLinkage()), NewName,
      ReInfo.getSpecializedType(), nullptr,
      Orig->getLocation(), Orig->isBare(), Orig->isTransparent(),
      Fragile, Orig->isThunk(), Orig->getClassVisibility(),
      Orig->getInlineStrategy(), Orig->getEffectsKind(), Orig,
      Orig->getDebugScope(), Orig->getDeclContext());
  NewF->setDeclCtx(Orig->getDeclContext());
  for (auto &Attr : Orig->getSemanticsAttrs()) {
    NewF->addSemanticsAttr(Attr);
  }
  return NewF;
}

void GenericCloner::populateCloned() {
  SILFunction *Cloned = getCloned();
  SILModule &M = Cloned->getModule();

  // Create arguments for the entry block.
  SILBasicBlock *OrigEntryBB = &*Original.begin();
  SILBasicBlock *ClonedEntryBB = new (M) SILBasicBlock(Cloned);
  getBuilder().setInsertionPoint(ClonedEntryBB);

  llvm::SmallVector<AllocStackInst *, 8> AllocStacks;
  AllocStackInst *ReturnValueAddr = nullptr;

  // Create the entry basic block with the function arguments.
  auto I = OrigEntryBB->bbarg_begin(), E = OrigEntryBB->bbarg_end();
  int ArgIdx = 0;
  while (I != E) {
    SILArgument *OrigArg = *I;
    RegularLocation Loc((Decl *)OrigArg->getDecl());
    AllocStackInst *ASI = nullptr;
    SILType mappedType = remapType(OrigArg->getType());
    if (ReInfo.isArgConverted(ArgIdx)) {
      // We need an alloc_stack as a replacement for the indirect parameter.
      assert(mappedType.isAddress());
      mappedType = mappedType.getObjectType();
      ASI = getBuilder().createAllocStack(Loc, mappedType);
      ValueMap[OrigArg] = ASI;
      AllocStacks.push_back(ASI);
      if (ReInfo.isResultIndex(ArgIdx)) {
        // This result is converted from indirect to direct. The return inst
        // needs to load the value from the alloc_stack. See below.
        assert(!ReturnValueAddr);
        ReturnValueAddr = ASI;
      } else {
        // Store the new direct parameter to the alloc_stack.
        auto *NewArg =
          new (M) SILArgument(ClonedEntryBB, mappedType, OrigArg->getDecl());
        getBuilder().createStore(Loc, NewArg, ASI);

        // Try to create a new debug_value from an existing debug_value_addr.
        for (Operand *ArgUse : OrigArg->getUses()) {
          if (auto *DVAI = dyn_cast<DebugValueAddrInst>(ArgUse->getUser())) {
            getBuilder().createDebugValue(DVAI->getLoc(), NewArg,
                                          DVAI->getVarInfo());
            break;
          }
        }
      }
    } else {
      auto *NewArg =
        new (M) SILArgument(ClonedEntryBB, mappedType, OrigArg->getDecl());
      ValueMap[OrigArg] = NewArg;
    }
    ++I;
    ++ArgIdx;
  }

  BBMap.insert(std::make_pair(OrigEntryBB, ClonedEntryBB));
  // Recursively visit original BBs in depth-first preorder, starting with the
  // entry block, cloning all instructions other than terminators.
  visitSILBasicBlock(OrigEntryBB);

  // Now iterate over the BBs and fix up the terminators.
  for (auto BI = BBMap.begin(), BE = BBMap.end(); BI != BE; ++BI) {
    getBuilder().setInsertionPoint(BI->second);
    TermInst *OrigTermInst = BI->first->getTerminator();
    if (auto *RI = dyn_cast<ReturnInst>(OrigTermInst)) {
      SILValue ReturnValue;
      if (ReturnValueAddr) {
        // The result is converted from indirect to direct. We have to load the
        // returned value from the alloc_stack.
        ReturnValue = getBuilder().createLoad(ReturnValueAddr->getLoc(),
                                              ReturnValueAddr);
      }
      for (AllocStackInst *ASI : reverse(AllocStacks)) {
        getBuilder().createDeallocStack(ASI->getLoc(), ASI);
      }
      if (ReturnValue) {
        getBuilder().createReturn(RI->getLoc(), ReturnValue);
        continue;
      }
    } else if (isa<ThrowInst>(OrigTermInst)) {
      for (AllocStackInst *ASI : reverse(AllocStacks)) {
        getBuilder().createDeallocStack(ASI->getLoc(), ASI);
      }
    }
    visit(BI->first->getTerminator());
  }
}
