//===--- MandatoryInlining.cpp - Perform inlining of "transparent" sites --===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "mandatory-inlining"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/DiagnosticsSIL.h"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/SILOptimizer/Utils/CFG.h"
#include "swift/SILOptimizer/Utils/Devirtualize.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "swift/SILOptimizer/Utils/SILInliner.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/ImmutableSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"

using namespace swift;

using DenseFunctionSet = llvm::DenseSet<SILFunction *>;
using ImmutableFunctionSet = llvm::ImmutableSet<SILFunction *>;

STATISTIC(NumMandatoryInlines,
          "Number of function application sites inlined by the mandatory "
          "inlining pass");

template<typename...T, typename...U>
static void diagnose(ASTContext &Context, SourceLoc loc, Diag<T...> diag,
                     U &&...args) {
  Context.Diags.diagnose(loc, diag, std::forward<U>(args)...);
}

namespace {

/// A helper class to update an instruction iterator if
/// removal of instructions would invalidate it.
class DeleteInstructionsHandler : public DeleteNotificationHandler {
  SILBasicBlock::iterator &CurrentI;
  SILModule &Module;

public:
  DeleteInstructionsHandler(SILBasicBlock::iterator &I)
      : CurrentI(I), Module(I->getModule()) {
    Module.registerDeleteNotificationHandler(this);
  }

  ~DeleteInstructionsHandler() override {
     // Unregister the handler.
    Module.removeDeleteNotificationHandler(this);
  }

  // Handling of instruction removal notifications.
  bool needsNotifications() override { return true; }

  // Handle notifications about removals of instructions.
  void handleDeleteNotification(SILNode *node) override {
    if (auto DeletedI = dyn_cast<SILInstruction>(node)) {
      if (CurrentI == SILBasicBlock::iterator(DeletedI)) {
        if (CurrentI != CurrentI->getParent()->begin()) {
          --CurrentI;
        } else {
          ++CurrentI;
        }
      }
    }
  }
};

} // end of namespace

/// \brief Fixup reference counts after inlining a function call (which is a
/// no-op unless the function is a thick function). Note that this function
/// makes assumptions about the release/retain convention of thick function
/// applications: namely, that an apply of a thick function consumes the callee
/// and that the function implementing the closure consumes its capture
/// arguments.
static void fixupReferenceCounts(
    SILBasicBlock::iterator I, SILValue CalleeValue,
    SmallVectorImpl<std::pair<SILValue, ParameterConvention>> &CaptureArgs,
    bool isCalleeGuaranteed) {
  // Add a copy of each non-address type capture argument to lifetime extend the
  // captured argument over the inlined function. This deals with the
  // possibility of the closure being destroyed by an earlier application and
  // thus cause the captured argument to be destroyed.
  for (auto &CaptureArg : CaptureArgs) {
    if (!CaptureArg.first->getType().isAddress() &&
        CaptureArg.second != ParameterConvention::Direct_Guaranteed &&
        CaptureArg.second != ParameterConvention::Direct_Unowned) {
      createIncrementBefore(CaptureArg.first, &*I);
    } else {
      // FIXME: What about indirectly owned parameters? The invocation of the
      // closure would perform an indirect copy which we should mimick here.
      assert(CaptureArg.second != ParameterConvention::Indirect_In &&
             "Missing indirect copy");
    }
  }

  // Destroy the callee as the apply would have done.
  if (!isCalleeGuaranteed)
    createDecrementBefore(CalleeValue, &*I);
}

static SILValue cleanupLoadedCalleeValue(SILValue CalleeValue, LoadInst *LI) {
  auto *PBI = cast<ProjectBoxInst>(LI->getOperand());
  auto *ABI = cast<AllocBoxInst>(PBI->getOperand());

  // The load instruction must have no more uses left to erase it.
  if (!LI->use_empty())
    return SILValue();
  LI->eraseFromParent();

  // Look through uses of the alloc box the load is loading from to find up to
  // one store and up to one strong release.
  StrongReleaseInst *SRI = nullptr;
  for (Operand *ABIUse : ABI->getUses()) {
    if (SRI == nullptr && isa<StrongReleaseInst>(ABIUse->getUser())) {
      SRI = cast<StrongReleaseInst>(ABIUse->getUser());
      continue;
    }

    if (ABIUse->getUser() == PBI)
      continue;

    return SILValue();
  }

  StoreInst *SI = nullptr;
  for (Operand *PBIUse : PBI->getUses()) {
    if (SI == nullptr && isa<StoreInst>(PBIUse->getUser())) {
      SI = cast<StoreInst>(PBIUse->getUser());
      continue;
    }

    return SILValue();
  }

  // If we found a store, record its source and erase it.
  if (SI) {
    CalleeValue = SI->getSrc();
    SI->eraseFromParent();
  } else {
    CalleeValue = SILValue();
  }

  // If we found a strong release, replace it with a strong release of the
  // source of the store and erase it.
  if (SRI) {
    if (CalleeValue)
      SILBuilderWithScope(SRI).emitStrongReleaseAndFold(SRI->getLoc(),
                                                        CalleeValue);
    SRI->eraseFromParent();
  }

  assert(PBI->use_empty());
  PBI->eraseFromParent();
  assert(ABI->use_empty());
  ABI->eraseFromParent();

  return CalleeValue;
}

/// \brief Removes instructions that create the callee value if they are no
/// longer necessary after inlining.
static void cleanupCalleeValue(
    SILValue CalleeValue,
    ArrayRef<SILValue> FullArgs) {
  SmallVector<SILInstruction*, 16> InstsToDelete;
  for (SILValue V : FullArgs) {
    if (V != CalleeValue)
      if (auto *I = V->getDefiningInstruction())
        if (isInstructionTriviallyDead(I))
          InstsToDelete.push_back(I);
  }
  recursivelyDeleteTriviallyDeadInstructions(InstsToDelete, true);

  // Handle the case where the callee of the apply is a load instruction. If we
  // fail to optimize, return. Otherwise, see if we can look through other
  // abstractions on our callee.
  if (auto *LI = dyn_cast<LoadInst>(CalleeValue)) {
    CalleeValue = cleanupLoadedCalleeValue(CalleeValue, LI);
    if (!CalleeValue) {
      return;
    }
  }

  SILValue CalleeSource = CalleeValue;
  // Handle partial_apply/thin_to_thick -> convert_function:
  // tryDeleteDeadClosure must run before deleting a ConvertFunction that
  // uses the PartialApplyInst or ThinToThickFunctionInst. tryDeleteDeadClosure
  // will delete any uses of the closure, including this ConvertFunction.
  if (auto *CFI = dyn_cast<ConvertFunctionInst>(CalleeValue))
    CalleeSource = CFI->getOperand();

  if (auto *PAI = dyn_cast<PartialApplyInst>(CalleeSource)) {
    SILValue Callee = PAI->getCallee();
    if (!tryDeleteDeadClosure(PAI))
      return;
    CalleeValue = Callee;

  } else if (auto *TTTFI = dyn_cast<ThinToThickFunctionInst>(CalleeSource)) {
    SILValue Callee = TTTFI->getCallee();
    if (!tryDeleteDeadClosure(TTTFI))
      return;
    CalleeValue = Callee;
  }

  // Handle function_ref -> convert_function -> partial_apply/thin_to_thick.
  if (auto *CFI = dyn_cast<ConvertFunctionInst>(CalleeValue)) {
    if (isInstructionTriviallyDead(CFI)) {
      recursivelyDeleteTriviallyDeadInstructions(CFI, true);
      return;
    }
  }

  if (auto *FRI = dyn_cast<FunctionRefInst>(CalleeValue)) {
    if (!FRI->use_empty())
      return;
    FRI->eraseFromParent();
  }
}

static void collectPartiallyAppliedArguments(
    PartialApplyInst *PAI,
    SmallVectorImpl<std::pair<SILValue, ParameterConvention>> &CapturedArgs,
    SmallVectorImpl<SILValue> &FullArgs) {
  ApplySite Site(PAI);
  SILFunctionConventions CalleeConv(Site.getSubstCalleeType(),
                                    PAI->getModule());
  for (auto &Arg : PAI->getArgumentOperands()) {
    unsigned CalleeArgumentIndex = Site.getCalleeArgIndex(Arg);
    assert(CalleeArgumentIndex >= CalleeConv.getSILArgIndexOfFirstParam());
    auto ParamInfo = CalleeConv.getParamInfoForSILArg(CalleeArgumentIndex);
    CapturedArgs.push_back(std::make_pair(Arg.get(), ParamInfo.getConvention()));
    FullArgs.push_back(Arg.get());
  }
}

/// \brief Returns the callee SILFunction called at a call site, in the case
/// that the call is transparent (as in, both that the call is marked
/// with the transparent flag and that callee function is actually transparently
/// determinable from the SIL) or nullptr otherwise. This assumes that the SIL
/// is already in SSA form.
///
/// In the case that a non-null value is returned, FullArgs contains effective
/// argument operands for the callee function.
static SILFunction *getCalleeFunction(
    SILFunction *F, FullApplySite AI, bool &IsThick,
    SmallVectorImpl<std::pair<SILValue, ParameterConvention>> &CaptureArgs,
    SmallVectorImpl<SILValue> &FullArgs, PartialApplyInst *&PartialApply,
    SILModule::LinkingMode Mode) {
  IsThick = false;
  PartialApply = nullptr;
  CaptureArgs.clear();
  FullArgs.clear();

  for (const auto &Arg : AI.getArguments())
    FullArgs.push_back(Arg);
  SILValue CalleeValue = AI.getCallee();

  if (auto *LI = dyn_cast<LoadInst>(CalleeValue)) {
    // Conservatively only see through alloc_box; we assume this pass is run
    // immediately after SILGen
    auto *PBI = dyn_cast<ProjectBoxInst>(LI->getOperand());
    if (!PBI)
      return nullptr;
    auto *ABI = dyn_cast<AllocBoxInst>(PBI->getOperand());
    if (!ABI)
      return nullptr;
    // Ensure there are no other uses of alloc_box than the project_box and
    // retains, releases.
    for (Operand *ABIUse : ABI->getUses())
      if (ABIUse->getUser() != PBI &&
          !isa<StrongRetainInst>(ABIUse->getUser()) &&
          !isa<StrongReleaseInst>(ABIUse->getUser()))
        return nullptr;

    // Scan forward from the alloc box to find the first store, which
    // (conservatively) must be in the same basic block as the alloc box
    StoreInst *SI = nullptr;
    for (auto I = SILBasicBlock::iterator(ABI), E = I->getParent()->end();
         I != E; ++I) {
      // If we find the load instruction first, then the load is loading from
      // a non-initialized alloc; this shouldn't really happen but I'm not
      // making any assumptions
      if (&*I == LI)
        return nullptr;
      if ((SI = dyn_cast<StoreInst>(I)) && SI->getDest() == PBI) {
        // We found a store that we know dominates the load; now ensure there
        // are no other uses of the project_box except loads.
        for (Operand *PBIUse : PBI->getUses())
          if (PBIUse->getUser() != SI && !isa<LoadInst>(PBIUse->getUser()))
            return nullptr;
        // We can conservatively see through the store
        break;
      }
    }
    if (!SI)
      return nullptr;
    CalleeValue = SI->getSrc();
  }

  // PartialApply/ThinToThick -> ConvertFunction patterns are generated
  // by @noescape closures.
  //
  // FIXME: We don't currently handle mismatched return types, however, this
  // would be a good optimization to handle and would be as simple as inserting
  // a cast.
  auto skipFuncConvert = [](SILValue CalleeValue) {
    auto *CFI = dyn_cast<ConvertFunctionInst>(CalleeValue);
    if (!CFI)
      return CalleeValue;

    // TODO: Handle argument conversion. All the code in this file needs to be
    // cleaned up and generalized. The argument conversion handling in
    // optimizeApplyOfConvertFunctionInst should apply to any combine
    // involving an apply, not just a specific pattern.
    //
    // For now, just handle conversion that doesn't affect argument types,
    // return types, or throws. We could trivially handle any other
    // representation change, but the only one that doesn't affect the ABI and
    // matters here is @noescape, so just check for that.
    auto FromCalleeTy = CFI->getOperand()->getType().castTo<SILFunctionType>();
    auto ToCalleeTy = CFI->getType().castTo<SILFunctionType>();
    auto EscapingCalleeTy =
      ToCalleeTy->getWithExtInfo(ToCalleeTy->getExtInfo().withNoEscape(false));
    if (FromCalleeTy != EscapingCalleeTy)
      return CalleeValue;

    return CFI->getOperand();
  };

  CalleeValue = skipFuncConvert(CalleeValue);

  // We are allowed to see through exactly one "partial apply" instruction or
  // one "thin to thick function" instructions, since those are the patterns
  // generated when using auto closures.
  if (auto *PAI = dyn_cast<PartialApplyInst>(CalleeValue)) {

    // Collect the applied arguments and their convention.
    collectPartiallyAppliedArguments(PAI, CaptureArgs, FullArgs);

    CalleeValue = PAI->getCallee();
    IsThick = true;
    PartialApply = PAI;
  } else if (auto *TTTFI = dyn_cast<ThinToThickFunctionInst>(CalleeValue)) {
    CalleeValue = TTTFI->getOperand();
    IsThick = true;
  }

  CalleeValue = skipFuncConvert(CalleeValue);

  auto *FRI = dyn_cast<FunctionRefInst>(CalleeValue);
  if (!FRI)
    return nullptr;

  SILFunction *CalleeFunction = FRI->getReferencedFunction();

  switch (CalleeFunction->getRepresentation()) {
  case SILFunctionTypeRepresentation::Thick:
  case SILFunctionTypeRepresentation::Thin:
  case SILFunctionTypeRepresentation::Method:
  case SILFunctionTypeRepresentation::Closure:
  case SILFunctionTypeRepresentation::WitnessMethod:
    break;
    
  case SILFunctionTypeRepresentation::CFunctionPointer:
  case SILFunctionTypeRepresentation::ObjCMethod:
  case SILFunctionTypeRepresentation::Block:
    return nullptr;
  }

  // If CalleeFunction is a declaration, see if we can load it. If we fail to
  // load it, bail.
  if (CalleeFunction->empty()
      && !AI.getModule().linkFunction(CalleeFunction, Mode))
    return nullptr;

  // If the CalleeFunction is a not-transparent definition, we can not process
  // it.
  if (CalleeFunction->isTransparent() == IsNotTransparent)
    return nullptr;

  if (F->isSerialized() && !CalleeFunction->hasValidLinkageForFragileRef()) {
    if (!CalleeFunction->hasValidLinkageForFragileInline()) {
      llvm::errs() << "caller: " << F->getName() << "\n";
      llvm::errs() << "callee: " << CalleeFunction->getName() << "\n";
      llvm_unreachable("Should never be inlining a resilient function into "
                       "a fragile function");
    }
    return nullptr;
  }

  return CalleeFunction;
}

static std::tuple<FullApplySite, SILBasicBlock::iterator>
tryDevirtualizeApplyHelper(FullApplySite InnerAI, SILBasicBlock::iterator I,
                           ClassHierarchyAnalysis *CHA) {
  auto NewInstPair = tryDevirtualizeApply(InnerAI, CHA);
  auto *NewInst = NewInstPair.first;
  if (!NewInst)
    return std::make_tuple(InnerAI, I);

  replaceDeadApply(InnerAI, NewInst);

  auto newApplyAI = NewInstPair.second.getInstruction();
  assert(newApplyAI && "devirtualized but removed apply site?");
  I = newApplyAI->getIterator();
  auto NewAI = FullApplySite::isa(newApplyAI);
  // *NOTE*, it is important that we return I here since we may have
  // devirtualized but not have a full apply site anymore.
  if (!NewAI)
    return std::make_tuple(FullApplySite(), I);

  return std::make_tuple(NewAI, I);
}

/// \brief Inlines all mandatory inlined functions into the body of a function,
/// first recursively inlining all mandatory apply instructions in those
/// functions into their bodies if necessary.
///
/// \param F the function to be processed
/// \param AI nullptr if this is being called from the top level; the relevant
///   ApplyInst requiring the recursive call when non-null
/// \param FullyInlinedSet the set of all functions already known to be fully
///   processed, to avoid processing them over again
/// \param SetFactory an instance of ImmutableFunctionSet::Factory
/// \param CurrentInliningSet the set of functions currently being inlined in
///   the current call stack of recursive calls
///
/// \returns true if successful, false if failed due to circular inlining.
static bool
runOnFunctionRecursively(SILFunction *F, FullApplySite AI,
                         SILModule::LinkingMode Mode,
                         DenseFunctionSet &FullyInlinedSet,
                         ImmutableFunctionSet::Factory &SetFactory,
                         ImmutableFunctionSet CurrentInliningSet,
                         ClassHierarchyAnalysis *CHA) {
  // Avoid reprocessing functions needlessly.
  if (FullyInlinedSet.count(F))
    return true;

  // Prevent attempt to circularly inline.
  if (CurrentInliningSet.contains(F)) {
    // This cannot happen on a top-level call, so AI should be non-null.
    assert(AI && "Cannot have circular inline without apply");
    SILLocation L = AI.getLoc();
    assert(L && "Must have location for transparent inline apply");
    diagnose(F->getModule().getASTContext(), L.getStartSourceLoc(),
             diag::circular_transparent);
    return false;
  }

  // Add to the current inlining set (immutably, so we only affect the set
  // during this call and recursive subcalls).
  CurrentInliningSet = SetFactory.add(CurrentInliningSet, F);

  SmallVector<std::pair<SILValue, ParameterConvention>, 16> CaptureArgs;
  SmallVector<SILValue, 32> FullArgs;

  for (auto BI = F->begin(), BE = F->end(); BI != BE; ++BI) {
    for (auto II = BI->begin(), IE = BI->end(); II != IE; ++II) {
      FullApplySite InnerAI = FullApplySite::isa(&*II);

      if (!InnerAI)
        continue;

      auto *ApplyBlock = InnerAI.getParent();

      // *NOTE* If devirtualization succeeds, sometimes II will not be InnerAI,
      // but a casted result of InnerAI or even a block argument due to
      // abstraction changes when calling the witness or class method. We still
      // know that InnerAI dominates II though.
      std::tie(InnerAI, II) = tryDevirtualizeApplyHelper(InnerAI, II, CHA);
      if (!InnerAI)
        continue;

      SILValue CalleeValue = InnerAI.getCallee();
      bool IsThick;
      PartialApplyInst *PAI;
      SILFunction *CalleeFunction = getCalleeFunction(
          F, InnerAI, IsThick, CaptureArgs, FullArgs, PAI, Mode);

      if (!CalleeFunction)
        continue;

      // Then recursively process it first before trying to inline it.
      if (!runOnFunctionRecursively(CalleeFunction, InnerAI, Mode,
                                    FullyInlinedSet, SetFactory,
                                    CurrentInliningSet, CHA)) {
        // If we failed due to circular inlining, then emit some notes to
        // trace back the failure if we have more information.
        // FIXME: possibly it could be worth recovering and attempting other
        // inlines within this same recursive call rather than simply
        // propagating the failure.
        if (AI) {
          SILLocation L = AI.getLoc();
          assert(L && "Must have location for transparent inline apply");
          diagnose(F->getModule().getASTContext(), L.getStartSourceLoc(),
                   diag::note_while_inlining);
        }
        return false;
      }

      // Create our initial list of substitutions.
      llvm::SmallVector<Substitution, 16> ApplySubs(InnerAI.subs_begin(),
                                                    InnerAI.subs_end());

      // Then if we have a partial_apply, add any additional subsitutions that
      // we may require to the end of the list.
      if (PAI) {
        copy(PAI->getSubstitutions(), std::back_inserter(ApplySubs));
      }

      SILOpenedArchetypesTracker OpenedArchetypesTracker(F);
      F->getModule().registerDeleteNotificationHandler(
          &OpenedArchetypesTracker);
      // The callee only needs to know about opened archetypes used in
      // the substitution list.
      OpenedArchetypesTracker.registerUsedOpenedArchetypes(
          InnerAI.getInstruction());
      if (PAI) {
        OpenedArchetypesTracker.registerUsedOpenedArchetypes(PAI);
      }

      SILInliner Inliner(*F, *CalleeFunction,
                         SILInliner::InlineKind::MandatoryInline, ApplySubs,
                         OpenedArchetypesTracker);
      if (!Inliner.canInlineFunction(InnerAI)) {
        // See comment above about casting when devirtualizing and how this
        // sometimes causes II and InnerAI to be different and even in different
        // blocks.
        II = InnerAI.getInstruction()->getIterator();
        continue;
      }

      // Inline function at I, which also changes I to refer to the first
      // instruction inlined in the case that it succeeds. We purposely
      // process the inlined body after inlining, because the inlining may
      // have exposed new inlining opportunities beyond those present in
      // the inlined function when processed independently.
      DEBUG(llvm::errs() << "Inlining @" << CalleeFunction->getName()
                         << " into @" << InnerAI.getFunction()->getName()
                         << "\n");

      // If we intend to inline a thick function, then we need to balance the
      // reference counts for correctness.
      if (IsThick) {
        bool IsCalleeGuaranteed =
            PAI &&
            PAI->getType().castTo<SILFunctionType>()->isCalleeGuaranteed();
        fixupReferenceCounts(II, CalleeValue, CaptureArgs, IsCalleeGuaranteed);
      }

      // Decrement our iterator (carefully, to avoid going off the front) so it
      // is valid after inlining is done.  Inlining deletes the apply, and can
      // introduce multiple new basic blocks.
      II = prev_or_default(II, ApplyBlock->begin(), ApplyBlock->end());

      Inliner.inlineFunction(InnerAI, FullArgs);

      // We were able to inline successfully. Remove the apply.
      InnerAI.getInstruction()->eraseFromParent();

      // Reestablish our iterator if it wrapped.
      if (II == ApplyBlock->end())
        II = ApplyBlock->begin();

      // Update the iterator when instructions are removed.
      DeleteInstructionsHandler DeletionHandler(II);

      // Now that the IR is correct, see if we can remove dead callee
      // computations (e.g. dead partial_apply closures).
      cleanupCalleeValue(CalleeValue, FullArgs);

      // Reposition iterators possibly invalidated by mutation.
      BI = SILFunction::iterator(ApplyBlock);
      IE = ApplyBlock->end();
      assert(BI == SILFunction::iterator(II->getParent()) &&
             "Mismatch between the instruction and basic block");
      ++NumMandatoryInlines;
    }
  }

  // Keep track of full inlined functions so we don't waste time recursively
  // reprocessing them.
  FullyInlinedSet.insert(F);
  return true;
}

//===----------------------------------------------------------------------===//
//                          Top Level Driver
//===----------------------------------------------------------------------===//

namespace {
class MandatoryInlining : public SILModuleTransform {
  /// The entry point to the transformation.
  void run() override {
    ClassHierarchyAnalysis *CHA = getAnalysis<ClassHierarchyAnalysis>();
    SILModule *M = getModule();
    SILModule::LinkingMode Mode = getOptions().LinkMode;
    bool ShouldCleanup = !getOptions().DebugSerialization;
    DenseFunctionSet FullyInlinedSet;
    ImmutableFunctionSet::Factory SetFactory;

    for (auto &F : *M) {
      
      // Don't inline into thunks, even transparent callees.
      if (F.isThunk())
        continue;

      runOnFunctionRecursively(&F,
                               FullApplySite(static_cast<ApplyInst*>(nullptr)),
                               Mode, FullyInlinedSet,
                               SetFactory, SetFactory.getEmptySet(), CHA);
    }

    // Make sure that we de-serialize all transparent functions,
    // even if we didn't inline them for some reason.
    // Transparent functions are not available externally, so we
    // have to generate code for them.
    for (auto &F : *M) {
      if (F.isTransparent())
        M->linkFunction(&F, Mode);
    }
    
    if (!ShouldCleanup)
      return;

    // Now that we've inlined some functions, clean up.  If there are any
    // transparent functions that are deserialized from another module that are
    // now unused, just remove them from the module.
    //
    // We do this with a simple linear scan, because transparent functions that
    // reference each other have already been flattened.
    for (auto FI = M->begin(), E = M->end(); FI != E; ) {
      SILFunction &F = *FI++;

      invalidateAnalysis(&F, SILAnalysis::InvalidationKind::Everything);

      if (F.getRefCount() != 0) continue;

      // Leave non-transparent functions alone.
      if (!F.isTransparent())
        continue;

      // We discard functions that don't have external linkage,
      // e.g. deserialized functions, internal functions, and thunks.
      // Being marked transparent controls this.
      if (F.isPossiblyUsedExternally()) continue;

      // ObjC functions are called through the runtime and are therefore alive
      // even if not referenced inside SIL.
      if (F.getRepresentation() == SILFunctionTypeRepresentation::ObjCMethod)
        continue;

      notifyDeleteFunction(&F);

      // Okay, just erase the function from the module.
      M->eraseFunction(&F);
    }
  }

};
} // end anonymous namespace

SILTransform *swift::createMandatoryInlining() {
  return new MandatoryInlining();
}
