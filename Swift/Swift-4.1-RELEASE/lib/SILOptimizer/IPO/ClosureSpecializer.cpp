//===--- ClosureSpecializer.cpp - Performs Closure Specialization ---------===//
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
///
/// \file
///
/// Closure Specialization
/// ----------------------
///
/// The purpose of the algorithm in this file is to perform the following
/// transformation: given a closure passed into a function which the closure is
/// then invoked in, clone the function and create a copy of the closure inside
/// the function. This closure will be able to be eliminated easily and the
/// overhead is gone. We then try to remove the original closure.
///
/// There are some complications. They are listed below and how we work around
/// them:
///
/// 1. If we support the specialization of closures with multiple user callsites
///    that can be specialized, we need to ensure that any captured values have
///    their reference counts adjusted properly. This implies for every
///    specialized call site, we insert an additional retain for each captured
///    argument with reference semantics. We will pass them in as extra @owned
///    to the specialized function. This @owned will be consumed by the "copy"
///    partial apply that is in the specialized function. Now the partial apply
///    will own those ref counts. This is unapplicable to thin_to_thick_function
///    since they do not have any captured args.
///
/// 2. If the closure was passed in @owned vs if the closure was passed in
///    @guaranteed. If the original closure was passed in @owned, then we know
///    that there is a balancing release for the new "copy" partial apply. But
///    since the original partial apply no longer will have that corresponding
///    -1, we need to insert a release for the old partial apply. We do this
///    right after the old call site where the original partial apply was
///    called. This ensures we do not shrink the lifetime of the old partial
///    apply. In the case where the old partial_apply was passed in at +0, we
///    know that the old partial_apply does not need to have any ref count
///    adjustments. On the other hand, the new "copy" partial apply in the
///    specialized function now needs to be balanced lest we leak. Thus we
///    insert a release right before any exit from the function. This ensures
///    that the release occurs in the epilog after any retains associated with
///    @owned return values.
///
/// 3. In !useLoweredAddresses mode, we do not support specialization of closures
///    with arguments passed using any indirect calling conventions besides
///    @inout and @inout_aliasable.  This is a temporary limitation that goes
///    away with sil-opaque-values.
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "closure-specialization"
#include "swift/Basic/Range.h"
#include "swift/SIL/SILCloner.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILModule.h"
#include "swift/SILOptimizer/Analysis/BasicCalleeAnalysis.h"
#include "swift/SILOptimizer/Analysis/CFG.h"
#include "swift/SILOptimizer/Analysis/FunctionOrder.h"
#include "swift/SILOptimizer/Analysis/ValueTracking.h"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/SILOptimizer/Utils/SILInliner.h"
#include "swift/SILOptimizer/Utils/SpecializationMangler.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

using namespace swift;

STATISTIC(NumClosureSpecialized,
          "Number of functions with closures specialized");
STATISTIC(NumPropagatedClosuresEliminated,
          "Number of closures propagated and then eliminated");
STATISTIC(NumPropagatedClosuresNotEliminated,
          "Number of closures propagated but not eliminated");

llvm::cl::opt<bool> EliminateDeadClosures(
    "closure-specialize-eliminate-dead-closures", llvm::cl::init(true),
    llvm::cl::desc("Do not eliminate dead closures after closure "
                   "specialization. This is meant ot be used when testing."));

//===----------------------------------------------------------------------===//
//                                  Utility
//===----------------------------------------------------------------------===//

static bool isSupportedClosureKind(const SILInstruction *I) {
  return isa<ThinToThickFunctionInst>(I) || isa<PartialApplyInst>(I);
}

//===----------------------------------------------------------------------===//
//                       Closure Spec Cloner Interface
//===----------------------------------------------------------------------===//

namespace {

class CallSiteDescriptor;

/// \brief A SILCloner subclass which clones a function that takes a closure
/// argument. We update the parameter list to remove the parameter for the
/// closure argument and to append the variables captured in the closure.
/// We also need to replace the closure parameter with the partial apply
/// on the closure. We need to update the callsite to pass in the correct
/// arguments.
class ClosureSpecCloner : public SILClonerWithScopes<ClosureSpecCloner> {
public:
  using SuperTy = SILClonerWithScopes<ClosureSpecCloner>;
  friend class SILInstructionVisitor<ClosureSpecCloner>;
  friend class SILCloner<ClosureSpecCloner>;

  ClosureSpecCloner(const CallSiteDescriptor &CallSiteDesc,
                    StringRef ClonedName)
      : SuperTy(*initCloned(CallSiteDesc, ClonedName)),
        CallSiteDesc(CallSiteDesc) {}

  void populateCloned();

  SILValue cloneCalleeConversion(SILValue calleeValue, SILValue NewClosure,
                                 SILBuilder &Builder);

  SILFunction *getCloned() { return &getBuilder().getFunction(); }
  static SILFunction *cloneFunction(const CallSiteDescriptor &CallSiteDesc,
                                    StringRef NewName) {
    ClosureSpecCloner C(CallSiteDesc, NewName);
    C.populateCloned();
    ++NumClosureSpecialized;
    return C.getCloned();
  };

private:
  static SILFunction *initCloned(const CallSiteDescriptor &CallSiteDesc,
                                 StringRef ClonedName);
  const CallSiteDescriptor &CallSiteDesc;
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
//                            Call Site Descriptor
//===----------------------------------------------------------------------===//

namespace {
struct ClosureInfo;

class CallSiteDescriptor {
  ClosureInfo *CInfo;
  FullApplySite AI;
  unsigned ClosureIndex;
  SILParameterInfo ClosureParamInfo;

  // This is only needed if we have guaranteed parameters. In most cases it will
  // have only one element, a return inst.
  llvm::TinyPtrVector<SILBasicBlock *> NonFailureExitBBs;

public:
  CallSiteDescriptor(ClosureInfo *CInfo, FullApplySite AI,
                     unsigned ClosureIndex, SILParameterInfo ClosureParamInfo,
                     llvm::TinyPtrVector<SILBasicBlock *> &&NonFailureExitBBs)
    : CInfo(CInfo), AI(AI), ClosureIndex(ClosureIndex),
      ClosureParamInfo(ClosureParamInfo),
      NonFailureExitBBs(NonFailureExitBBs) {}

  CallSiteDescriptor(CallSiteDescriptor&&) =default;
  CallSiteDescriptor &operator=(CallSiteDescriptor &&) =default;

  SILFunction *getApplyCallee() const {
    return cast<FunctionRefInst>(AI.getCallee())->getReferencedFunction();
  }

  SILFunction *getClosureCallee() const {
    if (auto *PAI = dyn_cast<PartialApplyInst>(getClosure()))
      return cast<FunctionRefInst>(PAI->getCallee())->getReferencedFunction();

    auto *TTTFI = cast<ThinToThickFunctionInst>(getClosure());
    return cast<FunctionRefInst>(TTTFI->getCallee())->getReferencedFunction();
  }

  bool closureHasRefSemanticContext() const {
    return isa<PartialApplyInst>(getClosure());
  }

  unsigned getClosureIndex() const { return ClosureIndex; }

  // Get the closure value passed to the apply (on the caller side).
  SILValue getClosureCallerArg() const {
    return getApplyInst().getArgument(ClosureIndex);
  }

  SILParameterInfo getClosureParameterInfo() const { return ClosureParamInfo; }

  SingleValueInstruction *
  createNewClosure(SILBuilder &B, SILValue V,
                   llvm::SmallVectorImpl<SILValue> &Args) const {
    if (isa<PartialApplyInst>(getClosure()))
      return B.createPartialApply(getClosure()->getLoc(), V, {}, Args,
                                  getClosure()
                                      ->getType()
                                      .getAs<SILFunctionType>()
                                      ->getCalleeConvention());

    assert(isa<ThinToThickFunctionInst>(getClosure()) &&
           "We only support partial_apply and thin_to_thick_function");
    return B.createThinToThickFunction(getClosure()->getLoc(), V,
                                       getClosure()->getType());
  }

  FullApplySite getApplyInst() const { return AI; }

  IsSerialized_t isSerialized() const;

  std::string createName() const;

  OperandValueArrayRef getArguments() const {
    if (auto *PAI = dyn_cast<PartialApplyInst>(getClosure()))
      return PAI->getArguments();

    // Thin to thick function has no non-callee arguments.
    assert(isa<ThinToThickFunctionInst>(getClosure()) &&
           "We only support partial_apply and thin_to_thick_function");
    return OperandValueArrayRef(ArrayRef<Operand>());
  }

  inline SingleValueInstruction *getClosure() const;

  unsigned getNumArguments() const {
    if (auto *PAI = dyn_cast<PartialApplyInst>(getClosure()))
      return PAI->getNumArguments();

    // Thin to thick function has no non-callee arguments.
    assert(isa<ThinToThickFunctionInst>(getClosure()) &&
           "We only support partial_apply and thin_to_thick_function");
    return 0;
  }

  bool isClosureGuaranteed() const {
    return getClosureParameterInfo().isGuaranteed();
  }

  bool isClosureConsumed() const {
    return getClosureParameterInfo().isConsumed();
  }

  SILLocation getLoc() const { return getClosure()->getLoc(); }

  SILModule &getModule() const { return AI.getModule(); }

  ArrayRef<SILBasicBlock *> getNonFailureExitBBs() const {
    return NonFailureExitBBs;
  }

  /// Extend the lifetime of 'Arg' to the lifetime of the closure.
  void extendArgumentLifetime(SILValue Arg,
                              SILArgumentConvention ArgConvention) const;
};
} // end anonymous namespace

namespace {
struct ClosureInfo {
  SingleValueInstruction *Closure;
  ValueLifetimeAnalysis::Frontier LifetimeFrontier;
  llvm::SmallVector<CallSiteDescriptor, 8> CallSites;

  ClosureInfo(SingleValueInstruction *Closure): Closure(Closure) {}

  ClosureInfo(ClosureInfo &&) =default;
  ClosureInfo &operator=(ClosureInfo &&) =default;
};
} // end anonymous namespace

SingleValueInstruction *CallSiteDescriptor::getClosure() const {
  return CInfo->Closure;
}

static bool isNonInoutIndirectSILArgument(SILValue Arg,
                                          SILArgumentConvention ArgConvention) {
  return !Arg->getType().isObject() && ArgConvention.isIndirectConvention() &&
         ArgConvention != SILArgumentConvention::Indirect_Inout &&
         ArgConvention != SILArgumentConvention::Indirect_InoutAliasable;
}

/// Update the callsite to pass in the correct arguments.
static void rewriteApplyInst(const CallSiteDescriptor &CSDesc,
                             SILFunction *NewF) {
  FullApplySite AI = CSDesc.getApplyInst();
  SingleValueInstruction *Closure = CSDesc.getClosure();
  SILBuilderWithScope Builder(Closure);
  FunctionRefInst *FRI = Builder.createFunctionRef(AI.getLoc(), NewF);

  // Create the args for the new apply by removing the closure argument...
  llvm::SmallVector<SILValue, 8> NewArgs;
  unsigned Index = 0;
  for (auto Arg : AI.getArguments()) {
    if (Index != CSDesc.getClosureIndex())
      NewArgs.push_back(Arg);
    Index++;
  }

  // ... and appending the captured arguments. We also insert retains here at
  // the location of the original closure. This is needed to balance the
  // implicit release of all captured arguments that occurs when the partial
  // apply is destroyed.
  SILModule &M = NewF->getModule();
  auto ClosureCalleeConv = CSDesc.getClosureCallee()->getConventions();
  unsigned ClosureArgIdx =
      ClosureCalleeConv.getNumSILArguments() - CSDesc.getNumArguments();
  for (auto Arg : CSDesc.getArguments()) {
    SILType ArgTy = Arg->getType();

    // If our argument is of trivial type, continue...
    if (ArgTy.isTrivial(M)) {
      NewArgs.push_back(Arg);
      ++ClosureArgIdx;
      continue;
    }

    auto ArgConvention =
        ClosureCalleeConv.getSILArgumentConvention(ClosureArgIdx);

    // Non-inout indirect arguments are not supported yet.
    assert(ArgTy.isObject() ||
           !isNonInoutIndirectSILArgument(Arg, ArgConvention));

    // If argument is not an object and it is an inout parameter,
    // continue...
    if (!ArgTy.isObject() &&
        !isNonInoutIndirectSILArgument(Arg, ArgConvention)) {
      NewArgs.push_back(Arg);
      ++ClosureArgIdx;
      continue;
    }

    // TODO: When we support address types, this code path will need to be
    // updated.

    // We need to balance the consumed argument of the new partial_apply in the
    // specialized callee by a retain. If both the original partial_apply and
    // the apply of the callee are in the same basic block we can assume they
    // are executed the same number of times. Therefore it is sufficient to just
    // retain the argument at the site of the original partial_apply.
    //
    //    %closure = partial_apply (%arg)
    //             = apply %callee(%closure)
    //  =>
    //             retain %arg
    //    %closure = partial_apply (%arg)
    //               apply %specialized_callee(..., %arg)
    //
    // However, if they are not in the same basic block the callee might be
    // executed more frequently than the closure (for example, if the closure is
    // created in a loop preheader and the callee taking the closure is executed
    // in the loop). In such a case we must keep the argument live across the
    // call site of the callee and emit a matching retain for every invocation
    // of the callee.
    //
    //    %closure = partial_apply (%arg)
    //
    //    while () {
    //             = %callee(%closure)
    //    }
    // =>
    //               retain %arg
    //    %closure = partial_apply (%arg)
    //
    //    while () {
    //               retain %arg
    //               apply %specialized_callee(.., %arg)
    //    }
    //               release %arg
    //
    if (AI.getParent() != Closure->getParent()) {
      // Emit the retain and release that keeps the argument life across the
      // callee using the closure.
      CSDesc.extendArgumentLifetime(Arg, ArgConvention);

      // Emit the retain that matches the captured argument by the
      // partial_apply
      // in the callee that is consumed by the partial_apply.
      Builder.setInsertionPoint(AI.getInstruction());
      Builder.createRetainValue(Closure->getLoc(), Arg,
                                Builder.getDefaultAtomicity());
    } else {
      Builder.createRetainValue(Closure->getLoc(), Arg,
                                Builder.getDefaultAtomicity());
    }

    NewArgs.push_back(Arg);
    ++ClosureArgIdx;
  }

  Builder.setInsertionPoint(AI.getInstruction());
  FullApplySite NewAI;
  if (auto *TAI = dyn_cast<TryApplyInst>(AI)) {
    NewAI = Builder.createTryApply(AI.getLoc(), FRI,
                                   SubstitutionList(), NewArgs,
                                   TAI->getNormalBB(), TAI->getErrorBB());
    // If we passed in the original closure as @owned, then insert a release
    // right after NewAI. This is to balance the +1 from being an @owned
    // argument to AI.
    if (CSDesc.isClosureConsumed() && CSDesc.closureHasRefSemanticContext()) {
      Builder.setInsertionPoint(TAI->getNormalBB()->begin());
      Builder.createReleaseValue(Closure->getLoc(), Closure, Builder.getDefaultAtomicity());
      Builder.setInsertionPoint(TAI->getErrorBB()->begin());
      Builder.createReleaseValue(Closure->getLoc(), Closure, Builder.getDefaultAtomicity());
      Builder.setInsertionPoint(AI.getInstruction());
    }
  } else {
    auto oldApply = cast<ApplyInst>(AI);
    auto newApply = Builder.createApply(oldApply->getLoc(), FRI,
                                        SubstitutionList(),
                                        NewArgs, oldApply->isNonThrowing());
    // If we passed in the original closure as @owned, then insert a release
    // right after NewAI. This is to balance the +1 from being an @owned
    // argument to AI.
    if (CSDesc.isClosureConsumed() && CSDesc.closureHasRefSemanticContext())
      Builder.createReleaseValue(Closure->getLoc(), Closure,
                                 Builder.getDefaultAtomicity());

    // Replace all uses of the old apply with the new apply.
    oldApply->replaceAllUsesWith(newApply);
  }
    
  // Erase the old apply.
  AI.getInstruction()->eraseFromParent();

  // TODO: Maybe include invalidation code for CallSiteDescriptor after we erase
  // AI from parent?
}

IsSerialized_t CallSiteDescriptor::isSerialized() const {
  if (getClosure()->getFunction()->isSerialized() &&
      getApplyCallee()->isSerialized())
    return IsSerializable;
  return IsNotSerialized;
}

std::string CallSiteDescriptor::createName() const {
  auto P = Demangle::SpecializationPass::ClosureSpecializer;
  Mangle::FunctionSignatureSpecializationMangler Mangler(P, isSerialized(),
                                                              getApplyCallee());

  if (auto *PAI = dyn_cast<PartialApplyInst>(getClosure())) {
    Mangler.setArgumentClosureProp(getClosureIndex(), PAI);
  } else {
    auto *TTTFI = cast<ThinToThickFunctionInst>(getClosure());
    Mangler.setArgumentClosureProp(getClosureIndex(), TTTFI);
  }
  return Mangler.mangle();
}

void CallSiteDescriptor::extendArgumentLifetime(
    SILValue Arg, SILArgumentConvention ArgConvention) const {
  assert(!CInfo->LifetimeFrontier.empty() &&
         "Need a post-dominating release(s)");

  auto ArgTy = Arg->getType();

  // Extend the lifetime of a captured argument to cover the callee.
  SILBuilderWithScope Builder(getClosure());

  // Indirect non-inout arguments are not supported yet.
  assert(!isNonInoutIndirectSILArgument(Arg, ArgConvention));

  if (ArgTy.isObject()) {
    Builder.createRetainValue(getClosure()->getLoc(), Arg,
                              Builder.getDefaultAtomicity());
    for (auto *I : CInfo->LifetimeFrontier) {
      Builder.setInsertionPoint(I);
      Builder.createReleaseValue(getClosure()->getLoc(), Arg,
                                 Builder.getDefaultAtomicity());
    }
  }
}

static bool isSupportedClosure(const SILInstruction *Closure) {
  if (!isSupportedClosureKind(Closure))
    return false;

  // We only support simple closures where a partial_apply or
  // thin_to_thick_function is passed a function_ref. This will be stored here
  // so the checking of the Callee can use the same code in both cases.
  SILValue Callee;

  // If Closure is a partial apply...
  if (auto *PAI = dyn_cast<PartialApplyInst>(Closure)) {
    // And it has substitutions, return false.
    if (PAI->hasSubstitutions())
      return false;

    // Ok, it is a closure we support, set Callee.
    Callee = PAI->getCallee();

  } else {
    // Otherwise closure must be a thin_to_thick_function.
    Callee = cast<ThinToThickFunctionInst>(Closure)->getCallee();
  }

  // Make sure that it is a simple partial apply (i.e. its callee is a
  // function_ref).
  //
  // TODO: We can probably handle other partial applies here.
  auto *FRI = dyn_cast<FunctionRefInst>(Callee);
  if (!FRI)
    return false;

  if (auto *PAI = dyn_cast<PartialApplyInst>(Closure)) {
    // Bail if any of the arguments are passed by address and
    // are not @inout.
    // This is a temporary limitation.
    auto ClosureCallee = FRI->getReferencedFunction();
    assert(ClosureCallee);
    auto ClosureCalleeConv = ClosureCallee->getConventions();
    unsigned ClosureArgIdx =
        ClosureCalleeConv.getNumSILArguments() - PAI->getNumArguments();
    for (auto Arg : PAI->getArguments()) {
      SILType ArgTy = Arg->getType();
      // If our argument is an object, continue...
      if (ArgTy.isObject()) {
        ++ClosureArgIdx;
        continue;
      }
      auto ArgConvention =
          ClosureCalleeConv.getSILArgumentConvention(ClosureArgIdx);
      if (ArgConvention != SILArgumentConvention::Indirect_Inout &&
          ArgConvention != SILArgumentConvention::Indirect_InoutAliasable)
        return false;
      ++ClosureArgIdx;
    }
  }

  // Otherwise, we do support specializing this closure.
  return true;
}

//===----------------------------------------------------------------------===//
//                     Closure Spec Cloner Implementation
//===----------------------------------------------------------------------===//

/// In this function we create the actual cloned function and its proper cloned
/// type. But we do not create any body. This implies that the creation of the
/// actual arguments in the function is in populateCloned.
///
/// \arg PAUser The function that is being passed the partial apply.
/// \arg PAI The partial apply that is being passed to PAUser.
/// \arg ClosureIndex The index of the partial apply in PAUser's function
///                   signature.
/// \arg ClonedName The name of the cloned function that we will create.
SILFunction *
ClosureSpecCloner::initCloned(const CallSiteDescriptor &CallSiteDesc,
                              StringRef ClonedName) {
  SILFunction *ClosureUser = CallSiteDesc.getApplyCallee();

  // This is the list of new interface parameters of the cloned function.
  llvm::SmallVector<SILParameterInfo, 4> NewParameterInfoList;

  // First add to NewParameterInfoList all of the SILParameterInfo in the
  // original function except for the closure.
  CanSILFunctionType ClosureUserFunTy = ClosureUser->getLoweredFunctionType();
  auto ClosureUserConv = ClosureUser->getConventions();
  unsigned Index = ClosureUserConv.getSILArgIndexOfFirstParam();
  for (auto &param : ClosureUserConv.getParameters()) {
    if (Index != CallSiteDesc.getClosureIndex())
      NewParameterInfoList.push_back(param);
    ++Index;
  }

  // Then add any arguments that are captured in the closure to the function's
  // argument type. Since they are captured, we need to pass them directly into
  // the new specialized function.
  SILFunction *ClosedOverFun = CallSiteDesc.getClosureCallee();
  auto ClosedOverFunConv = ClosedOverFun->getConventions();
  SILModule &M = ClosureUser->getModule();

  // Captured parameters are always appended to the function signature. If the
  // type of the captured argument is:
  // - direct and trivial, pass the argument as Direct_Unowned.
  // - direct and non-trivial, pass the argument as Direct_Owned.
  // - indirect, pass the argument using the same parameter convention as in the
  // original closure.
  //
  // We use the type of the closure here since we allow for the closure to be an
  // external declaration.
  unsigned NumTotalParams = ClosedOverFunConv.getNumParameters();
  unsigned NumNotCaptured = NumTotalParams - CallSiteDesc.getNumArguments();
  for (auto &PInfo : ClosedOverFunConv.getParameters().slice(NumNotCaptured)) {
    ParameterConvention ParamConv;
    if (PInfo.isFormalIndirect()) {
      ParamConv = PInfo.getConvention();
      assert(!SILModuleConventions(M).useLoweredAddresses()
             || ParamConv == ParameterConvention::Indirect_Inout
             || ParamConv == ParameterConvention::Indirect_InoutAliasable);
    } else {
      ParamConv = ClosedOverFunConv.getSILType(PInfo).isTrivial(M)
                      ? ParameterConvention::Direct_Unowned
                      : ParameterConvention::Direct_Owned;
    }

    SILParameterInfo NewPInfo(PInfo.getType(), ParamConv);
    NewParameterInfoList.push_back(NewPInfo);
  }

  // The specialized function is always a thin function. This is important
  // because we may add additional parameters after the Self parameter of
  // witness methods. In this case the new function is not a method anymore.
  auto ExtInfo = ClosureUserFunTy->getExtInfo();
  ExtInfo = ExtInfo.withRepresentation(SILFunctionTypeRepresentation::Thin);

  auto ClonedTy = SILFunctionType::get(
      ClosureUserFunTy->getGenericSignature(), ExtInfo,
      ClosureUserFunTy->getCoroutineKind(),
      ClosureUserFunTy->getCalleeConvention(), NewParameterInfoList,
      ClosureUserFunTy->getYields(), ClosureUserFunTy->getResults(),
      ClosureUserFunTy->getOptionalErrorResult(), M.getASTContext());

  // We make this function bare so we don't have to worry about decls in the
  // SILArgument.
  auto *Fn = M.createFunction(
      // It's important to use a shared linkage for the specialized function
      // and not the original linkage.
      // Otherwise the new function could have an external linkage (in case the
      // original function was de-serialized) and would not be code-gen'd.
      getSpecializedLinkage(ClosureUser, ClosureUser->getLinkage()), ClonedName,
      ClonedTy, ClosureUser->getGenericEnvironment(),
      ClosureUser->getLocation(), IsBare, ClosureUser->isTransparent(),
      CallSiteDesc.isSerialized(), ClosureUser->getEntryCount(),
      ClosureUser->isThunk(), ClosureUser->getClassSubclassScope(),
      ClosureUser->getInlineStrategy(), ClosureUser->getEffectsKind(),
      ClosureUser, ClosureUser->getDebugScope());
  if (!ClosureUser->hasQualifiedOwnership()) {
    Fn->setUnqualifiedOwnership();
  }
  for (auto &Attr : ClosureUser->getSemanticsAttrs())
    Fn->addSemanticsAttr(Attr);
  return Fn;
}

// Clone a chain of ConvertFunctionInsts.
SILValue ClosureSpecCloner::cloneCalleeConversion(SILValue calleeValue,
                                                  SILValue NewClosure,
                                                  SILBuilder &Builder) {
  if (calleeValue == CallSiteDesc.getClosure())
    return NewClosure;

  auto *CFI = cast<ConvertFunctionInst>(calleeValue);

  calleeValue = cloneCalleeConversion(CFI->getOperand(), NewClosure, Builder);

  return Builder.createConvertFunction(CallSiteDesc.getLoc(), calleeValue,
                                       CFI->getType());
}

/// \brief Populate the body of the cloned closure, modifying instructions as
/// necessary. This is where we create the actual specialized BB Arguments.
void ClosureSpecCloner::populateCloned() {
  SILFunction *Cloned = getCloned();
  SILFunction *ClosureUser = CallSiteDesc.getApplyCallee();

  // Create arguments for the entry block.
  SILBasicBlock *ClosureUserEntryBB = &*ClosureUser->begin();
  SILBasicBlock *ClonedEntryBB = Cloned->createBasicBlock();

  // Remove the closure argument.
  SILArgument *ClosureArg = nullptr;
  for (size_t i = 0, e = ClosureUserEntryBB->args_size(); i != e; ++i) {
    SILArgument *Arg = ClosureUserEntryBB->getArgument(i);
    if (i == CallSiteDesc.getClosureIndex()) {
      ClosureArg = Arg;
      continue;
    }

    // Otherwise, create a new argument which copies the original argument
    SILValue MappedValue =
        ClonedEntryBB->createFunctionArgument(Arg->getType(), Arg->getDecl());
    ValueMap.insert(std::make_pair(Arg, MappedValue));
  }

  // Next we need to add in any arguments that are not captured as arguments to
  // the cloned function.
  //
  // We do not insert the new mapped arguments into the value map since there by
  // definition is nothing in the partial apply user function that references
  // such arguments. After this pass is done the only thing that will reference
  // the arguments is the partial apply that we will create.
  SILFunction *ClosedOverFun = CallSiteDesc.getClosureCallee();
  auto ClosedOverFunConv = ClosedOverFun->getConventions();
  unsigned NumTotalParams = ClosedOverFunConv.getNumParameters();
  unsigned NumNotCaptured = NumTotalParams - CallSiteDesc.getNumArguments();
  llvm::SmallVector<SILValue, 4> NewPAIArgs;
  for (auto &PInfo : ClosedOverFunConv.getParameters().slice(NumNotCaptured)) {
    auto paramTy = ClosedOverFunConv.getSILType(PInfo);
    SILValue MappedValue = ClonedEntryBB->createFunctionArgument(paramTy);
    NewPAIArgs.push_back(MappedValue);
  }

  SILBuilder &Builder = getBuilder();
  Builder.setInsertionPoint(ClonedEntryBB);

  // Clone FRI and PAI, and replace usage of the removed closure argument
  // with result of cloned PAI.
  SILValue FnVal =
      Builder.createFunctionRef(CallSiteDesc.getLoc(), ClosedOverFun);
  auto *NewClosure = CallSiteDesc.createNewClosure(Builder, FnVal, NewPAIArgs);

  // Clone a chain of ConvertFunctionInsts.
  SILValue ConvertedCallee = cloneCalleeConversion(
      CallSiteDesc.getClosureCallerArg(), NewClosure, Builder);
  ValueMap.insert(std::make_pair(ClosureArg, ConvertedCallee));

  BBMap.insert(std::make_pair(ClosureUserEntryBB, ClonedEntryBB));
  // Recursively visit original BBs in depth-first preorder, starting with the
  // entry block, cloning all instructions other than terminators.
  visitSILBasicBlock(ClosureUserEntryBB);

  // Now iterate over the BBs and fix up the terminators.
  for (auto BI = BBMap.begin(), BE = BBMap.end(); BI != BE; ++BI) {
    Builder.setInsertionPoint(BI->second);
    visit(BI->first->getTerminator());
  }

  // Then insert a release in all non failure exit BBs if our partial apply was
  // guaranteed. This is b/c it was passed at +0 originally and we need to
  // balance the initial increment of the newly created closure.
  if (CallSiteDesc.isClosureGuaranteed() &&
      CallSiteDesc.closureHasRefSemanticContext()) {
    for (SILBasicBlock *BB : CallSiteDesc.getNonFailureExitBBs()) {
      SILBasicBlock *OpBB = BBMap[BB];

      TermInst *TI = OpBB->getTerminator();
      auto Loc = CleanupLocation::get(NewClosure->getLoc());

      // If we have a return, we place the release right before it so we know
      // that it will be executed at the end of the epilogue.
      if (isa<ReturnInst>(TI)) {
        Builder.setInsertionPoint(TI);
        Builder.createReleaseValue(Loc, SILValue(NewClosure),
                                   Builder.getDefaultAtomicity());
        continue;
      }

      // We use casts where findAllNonFailureExitBBs should have made sure that
      // this is true. This will ensure that the code is updated when we hit the
      // cast failure in debug builds.
      auto *Unreachable = cast<UnreachableInst>(TI);
      auto PrevIter = std::prev(SILBasicBlock::iterator(Unreachable));
      auto NoReturnApply = FullApplySite::isa(&*PrevIter);

      // We insert the release value right before the no return apply so that if
      // the partial apply is passed into the no-return function as an @owned
      // value, we will retain the partial apply before we release it and
      // potentially eliminate it.
      Builder.setInsertionPoint(NoReturnApply.getInstruction());
      Builder.createReleaseValue(Loc, SILValue(NewClosure), Builder.getDefaultAtomicity());
    }
  }
}

//===----------------------------------------------------------------------===//
//                            Closure Specializer
//===----------------------------------------------------------------------===//

namespace {

class SILClosureSpecializerTransform : public SILFunctionTransform {
  void gatherCallSites(SILFunction *Caller,
                       llvm::SmallVectorImpl<ClosureInfo*> &ClosureCandidates,
                       llvm::DenseSet<FullApplySite> &MultipleClosureAI);
  bool specialize(SILFunction *Caller,
                  std::vector<SingleValueInstruction *> &PropagatedClosures);

public:
  SILClosureSpecializerTransform() {}

  void run() override;

};

void SILClosureSpecializerTransform::run() {
  SILFunction *F = getFunction();

  // Don't optimize functions that are marked with the opt.never
  // attribute.
  if (!F->shouldOptimize())
    return;

  // If F is an external declaration, there is nothing to specialize.
  if (F->isExternalDeclaration())
    return;

  std::vector<SingleValueInstruction *> PropagatedClosures;

  if (!specialize(F, PropagatedClosures))
    return;

  // If for testing purposes we were asked to not eliminate dead closures,
  // return.
  if (EliminateDeadClosures) {
    // Otherwise, remove any local dead closures that are now dead since we
    // specialized all of their uses.
    DEBUG(llvm::dbgs() << "Trying to remove dead closures!\n");
    sortUnique(PropagatedClosures);
    for (auto *Closure : PropagatedClosures) {
      DEBUG(llvm::dbgs() << "    Visiting: " << *Closure);
      if (!tryDeleteDeadClosure(Closure)) {
        DEBUG(llvm::dbgs() << "        Failed to delete closure!\n");
        NumPropagatedClosuresNotEliminated++;
        continue;
      }

      DEBUG(llvm::dbgs() << "        Deleted closure!\n");
      ++NumPropagatedClosuresEliminated;
    }
  }

  // Invalidate everything since we delete calls as well as add new
  // calls and branches.
  invalidateAnalysis(SILAnalysis::InvalidationKind::Everything);
}

void SILClosureSpecializerTransform::gatherCallSites(
    SILFunction *Caller,
    llvm::SmallVectorImpl<ClosureInfo*> &ClosureCandidates,
    llvm::DenseSet<FullApplySite> &MultipleClosureAI) {

  // A set of apply inst that we have associated with a closure. We use this to
  // make sure that we do not handle call sites with multiple closure arguments.
  llvm::DenseSet<FullApplySite> VisitedAI;

  // For each basic block BB in Caller...
  for (auto &BB : *Caller) {

    // For each instruction II in BB...
    for (auto &II : BB) {
      // If II is not a closure that we support specializing, skip it...
      if (!isSupportedClosure(&II))
        continue;
      auto ClosureInst = cast<SingleValueInstruction>(&II);

      ClosureInfo *CInfo = nullptr;

      // Go through all uses of our closure.

      // Worklist of operands.
      SmallVector<Operand *, 8> Uses(ClosureInst->getUses());

      // Live range end points.
      SmallVector<SILInstruction *, 8> UsePoints;

      // Uses may grow in this loop.
      for (size_t UseIndex = 0; UseIndex < Uses.size(); ++UseIndex) {
        auto *Use = Uses[UseIndex];
        UsePoints.push_back(Use->getUser());

        // Recurse through conversions.
        if (auto *CFI = dyn_cast<ConvertFunctionInst>(Use->getUser())) {
          // Push Uses in reverse order so they are visited in forward order.
          Uses.append(CFI->getUses().begin(), CFI->getUses().end());
          continue;
        }
        // If this use is not an apply inst or an apply inst with
        // substitutions, there is nothing interesting for us to do, so
        // continue...
        auto AI = FullApplySite::isa(Use->getUser());
        if (!AI || AI.hasSubstitutions())
          continue;

        // Check if we have already associated this apply inst with a closure to
        // be specialized. We do not handle applies that take in multiple
        // closures at this time.
        if (!VisitedAI.insert(AI).second) {
          MultipleClosureAI.insert(AI);
          continue;
        }

        // If AI does not have a function_ref definition as its callee, we can
        // not do anything here... so continue...
        SILFunction *ApplyCallee = AI.getReferencedFunction();
        if (!ApplyCallee || ApplyCallee->isExternalDeclaration())
          continue;

        // Don't specialize non-fragile callees if the caller is fragile;
        // the specialized callee will have shared linkage, and thus cannot
        // be referenced from the fragile caller.
        if (Caller->isSerialized() &&
            !ApplyCallee->hasValidLinkageForFragileInline())
          continue;

        // If the callee uses a dynamic Self, we cannot specialize it,
        // since the resulting specialization might longer has 'self' as the
        // last parameter.
        //
        // We could fix this by inserting new arguments more carefully, or
        // changing how we model dynamic Self altogether.
        if (mayBindDynamicSelf(ApplyCallee))
          return;

        // Ok, we know that we can perform the optimization but not whether or
        // not the optimization is profitable. Find the index of the argument
        // corresponding to our partial apply.
        Optional<unsigned> ClosureIndex;
        for (unsigned i = 0, e = AI.getNumArguments(); i != e; ++i) {
          if (AI.getArgument(i) != Use->get())
            continue;
          ClosureIndex = i;
          DEBUG(llvm::dbgs() << "    Found callsite with closure argument at "
                << i << ": " << *AI.getInstruction());
          break;
        }

        // If we did not find an index, there is nothing further to do,
        // continue.
        if (!ClosureIndex.hasValue())
          continue;

        // Make sure that the Closure is invoked in the Apply's callee. We only
        // want to perform closure specialization if we know that we will be
        // able to change a partial_apply into an apply.
        //
        // TODO: Maybe just call the function directly instead of moving the
        // partial apply?
        SILValue Arg = ApplyCallee->getArgument(ClosureIndex.getValue());
        if (std::none_of(Arg->use_begin(), Arg->use_end(),
                         [&Arg](Operand *Op) -> bool {
                           auto UserAI = FullApplySite::isa(Op->getUser());
                           return UserAI && UserAI.getCallee() == Arg;
                         })) {
          continue;
        }

        unsigned firstParamArgIdx =
            AI.getSubstCalleeConv().getSILArgIndexOfFirstParam();
        assert(ClosureIndex.getValue() >= firstParamArgIdx);
        auto ClosureParamIndex = ClosureIndex.getValue() - firstParamArgIdx;

        auto ParamInfo = AI.getSubstCalleeType()->getParameters();
        SILParameterInfo ClosureParamInfo = ParamInfo[ClosureParamIndex];

        // Get all non-failure exit BBs in the Apply Callee if our partial apply
        // is guaranteed. If we do not understand one of the exit BBs, bail.
        //
        // We need this to make sure that we insert a release in the appropriate
        // locations to balance the +1 from the creation of the partial apply.
        //
        // However, thin_to_thick_function closures don't have a context and
        // don't need to be released.
        llvm::TinyPtrVector<SILBasicBlock *> NonFailureExitBBs;
        if (ClosureParamInfo.isGuaranteed() &&
            !isa<ThinToThickFunctionInst>(ClosureInst) &&
            !findAllNonFailureExitBBs(ApplyCallee, NonFailureExitBBs)) {
          continue;
        }

        // Compute the final release points of the closure. We will insert
        // release of the captured arguments here.
        if (!CInfo)
          CInfo = new ClosureInfo(ClosureInst);

        // Now we know that CSDesc is profitable to specialize. Add it to our
        // call site list.
        CInfo->CallSites.push_back(
          CallSiteDescriptor(CInfo, AI, ClosureIndex.getValue(),
                             ClosureParamInfo, std::move(NonFailureExitBBs)));
      }
      if (CInfo) {
        ValueLifetimeAnalysis VLA(CInfo->Closure, UsePoints);
        VLA.computeFrontier(CInfo->LifetimeFrontier,
                            ValueLifetimeAnalysis::AllowToModifyCFG);
        ClosureCandidates.push_back(CInfo);
      }
    }
  }
}

bool SILClosureSpecializerTransform::specialize(SILFunction *Caller,
                    std::vector<SingleValueInstruction *> &PropagatedClosures) {
  DEBUG(llvm::dbgs() << "Optimizing callsites that take closure argument in "
                     << Caller->getName() << '\n');

  // Collect all of the PartialApplyInsts that are used as arguments to
  // ApplyInsts. Check the profitability of specializing the closure argument.
  llvm::SmallVector<ClosureInfo*, 8> ClosureCandidates;
  llvm::DenseSet<FullApplySite> MultipleClosureAI;
  gatherCallSites(Caller, ClosureCandidates, MultipleClosureAI);

  bool Changed = false;
  for (auto *CInfo : ClosureCandidates) {
    for (auto &CSDesc : CInfo->CallSites) {
      // Do not specialize apply insts that take in multiple closures. This pass
      // does not know how to do this yet.
      if (MultipleClosureAI.count(CSDesc.getApplyInst()))
        continue;

      auto NewFName = CSDesc.createName();
      DEBUG(llvm::dbgs() << "    Perform optimizations with new name "
                         << NewFName << '\n');

      // Then see if we already have a specialized version of this function in
      // our module.
      SILFunction *NewF = CInfo->Closure->getModule().lookUpFunction(NewFName);

      // If not, create a specialized version of ApplyCallee calling the closure
      // directly.
      if (!NewF) {
        NewF = ClosureSpecCloner::cloneFunction(CSDesc, NewFName);
        notifyAddFunction(NewF, CSDesc.getApplyCallee());
      }

      // Rewrite the call
      rewriteApplyInst(CSDesc, NewF);

      PropagatedClosures.push_back(CSDesc.getClosure());
      Changed = true;
    }
    delete CInfo;
  }
  return Changed;
}

} // end anonymous namespace

SILTransform *swift::createClosureSpecializer() {
  return new SILClosureSpecializerTransform();
}
