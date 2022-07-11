//===--- ResultPlan.cpp ---------------------------------------------------===//
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

#include "ResultPlan.h"
#include "Callee.h"
#include "Conversion.h"
#include "Initialization.h"
#include "LValue.h"
#include "RValue.h"
#include "SILGenFunction.h"
#include "swift/AST/GenericEnvironment.h"

using namespace swift;
using namespace Lowering;

//===----------------------------------------------------------------------===//
//                                Result Plans
//===----------------------------------------------------------------------===//

namespace {

/// A result plan for evaluating an indirect result into the address
/// associated with an initialization.
class InPlaceInitializationResultPlan final : public ResultPlan {
  Initialization *init;

public:
  InPlaceInitializationResultPlan(Initialization *init) : init(init) {}

  RValue finish(SILGenFunction &SGF, SILLocation loc, CanType substType,
                ArrayRef<ManagedValue> &directResults,
                SILValue bridgedForeignError) override {
    init->finishInitialization(SGF);
    return RValue::forInContext();
  }
  void
  gatherIndirectResultAddrs(SILGenFunction &SGF, SILLocation loc,
                            SmallVectorImpl<SILValue> &outList) const override {
    outList.emplace_back(init->getAddressForInPlaceInitialization(SGF, loc));
  }
};

/// A cleanup that handles the delayed emission of an indirect buffer for opened
/// Self arguments.
class IndirectOpenedSelfCleanup final : public Cleanup {
  SILValue box;
public:
  IndirectOpenedSelfCleanup()
    : box()
  {}
  
  void setBox(SILValue b) {
    assert(!box && "buffer already set?!");
    box = b;
  }
  
  void emit(SILGenFunction &SGF, CleanupLocation loc, ForUnwind_t forUnwind)
  override {
    assert(box && "buffer never emitted before activating cleanup?!");
    SGF.B.createDeallocBox(loc, box);
  }
  
  void dump(SILGenFunction &SGF) const override {
    llvm::errs() << "IndirectOpenedSelfCleanup\n";
    if (box)
      box->print(llvm::errs());
  }
};

/// Map a type expressed in terms of opened archetypes into a context-free
/// dependent type, returning the type, a generic signature with parameters
/// corresponding to each opened type,
static std::tuple<CanType, CanGenericSignature, SubstitutionMap>
mapTypeOutOfOpenedExistentialContext(CanType t) {
  SmallVector<OpenedArchetypeType *, 4> openedTypes;
  t->getOpenedExistentials(openedTypes);

  ArrayRef<Type> openedTypesAsTypes(
    reinterpret_cast<const Type *>(openedTypes.data()),
    openedTypes.size());

  SmallVector<GenericTypeParamType *, 4> params;
  for (unsigned i : indices(openedTypes)) {
    params.push_back(GenericTypeParamType::get(/*type sequence*/ false,
                                               /*depth*/ 0, /*index*/ i,
                                               t->getASTContext()));
  }
  
  auto mappedSig = GenericSignature::get(params, {});
  auto mappedSubs = SubstitutionMap::get(mappedSig, openedTypesAsTypes, {});

  auto mappedTy = t.subst(
    [&](SubstitutableType *t) -> Type {
      auto index = std::find(openedTypes.begin(), openedTypes.end(), t)
        - openedTypes.begin();
      assert(index != openedTypes.end() - openedTypes.begin());
      return params[index];
    },
    MakeAbstractConformanceForGenericType());

  return std::make_tuple(mappedTy->getCanonicalType(mappedSig),
                         mappedSig.getCanonicalSignature(), mappedSubs);
}

/// A result plan for an indirectly-returned opened existential value.
///
/// This defers allocating the temporary for the result to a later point so that
/// it happens after the arguments are evaluated.
class IndirectOpenedSelfResultPlan final : public ResultPlan {
  AbstractionPattern origType;
  CanType substType;
  CleanupHandle handle = CleanupHandle::invalid();
  mutable SILValue resultBox, resultBuf;

public:
  IndirectOpenedSelfResultPlan(SILGenFunction &SGF,
                               AbstractionPattern origType,
                               CanType substType)
    : origType(origType), substType(substType)
  {
    // Create a cleanup to deallocate the stack buffer at the proper scope.
    // We won't emit the buffer till later, after arguments have been opened,
    // though.
    SGF.Cleanups.pushCleanupInState<IndirectOpenedSelfCleanup>(
                                                         CleanupState::Dormant);
    handle = SGF.Cleanups.getCleanupsDepth();
  }
  
  void
  gatherIndirectResultAddrs(SILGenFunction &SGF, SILLocation loc,
                            SmallVectorImpl<SILValue> &outList) const override {
    assert(!resultBox && "already created temporary?!");
    
    // We allocate the buffer as a box because the scope nesting won't clean
    // this up with good stack discipline relative to any stack allocations that
    // occur during argument emission. Escape analysis during mandatory passes
    // ought to clean this up.

    auto resultTy = SGF.getLoweredType(origType, substType).getASTType();
    CanType layoutTy;
    CanGenericSignature layoutSig;
    SubstitutionMap layoutSubs;
    std::tie(layoutTy, layoutSig, layoutSubs)
      = mapTypeOutOfOpenedExistentialContext(resultTy);

    auto boxLayout =
        SILLayout::get(SGF.getASTContext(), layoutSig.getCanonicalSignature(),
                       SILField(layoutTy->getCanonicalType(layoutSig), true));

    resultBox = SGF.B.createAllocBox(loc,
      SILBoxType::get(SGF.getASTContext(),
                      boxLayout,
                      layoutSubs));
    
    // Complete the cleanup to deallocate this buffer later, after we're
    // finished with the argument.
    static_cast<IndirectOpenedSelfCleanup&>(SGF.Cleanups.getCleanup(handle))
      .setBox(resultBox);
    SGF.Cleanups.setCleanupState(handle, CleanupState::Active);

    resultBuf = SGF.B.createProjectBox(loc, resultBox, 0);
    outList.emplace_back(resultBuf);
  }

  RValue finish(SILGenFunction &SGF, SILLocation loc, CanType substType,
                ArrayRef<ManagedValue> &directResults,
                SILValue bridgedForeignError) override {
    assert(resultBox && "never emitted temporary?!");
    
    // Lower the unabstracted result type.
    auto &substTL = SGF.getTypeLowering(substType);

    ManagedValue value;
    // If the value isn't address-only, go ahead and load.
    if (!substTL.isAddressOnly()) {
      auto load = substTL.emitLoad(SGF.B, loc, resultBuf,
                                   LoadOwnershipQualifier::Take);
      value = SGF.emitManagedRValueWithCleanup(load);
    } else {
      value = SGF.emitManagedRValueWithCleanup(resultBuf);
    }

    // A Self return should never be further abstracted. It's also never emitted
    // into context; we disable that optimization because Self may not even
    // be available to pre-allocate a stack buffer before we prepare a call.
    return RValue(SGF, loc, substType, value);
  }
};

/// A result plan for working with a single value and potentially
/// reabstracting it.  The value can actually be a tuple if the
/// abstraction is opaque.
class ScalarResultPlan final : public ResultPlan {
  std::unique_ptr<TemporaryInitialization> temporary;
  AbstractionPattern origType;
  Initialization *init;
  SILFunctionTypeRepresentation rep;

public:
  ScalarResultPlan(std::unique_ptr<TemporaryInitialization> &&temporary,
                   AbstractionPattern origType, Initialization *init,
                   SILFunctionTypeRepresentation rep)
      : temporary(std::move(temporary)), origType(origType), init(init),
        rep(rep) {}

  RValue finish(SILGenFunction &SGF, SILLocation loc, CanType substType,
                ArrayRef<ManagedValue> &directResults,
                SILValue bridgedForeignError) override {
    // Lower the unabstracted result type.
    auto &substTL = SGF.getTypeLowering(substType);

    // Claim the value:
    ManagedValue value;

    // If we were created with a temporary, that address was passed as
    // an indirect result.
    if (temporary) {
      // Establish the cleanup.
      temporary->finishInitialization(SGF);
      value = temporary->getManagedAddress();

      // If the value isn't address-only, go ahead and load.
      if (!substTL.isAddressOnly()) {
        auto load = substTL.emitLoad(SGF.B, loc, value.forward(SGF),
                                     LoadOwnershipQualifier::Take);
        value = SGF.emitManagedRValueWithCleanup(load);
      }

      // Otherwise, it was returned as a direct result.
    } else {
      value = directResults.front();
      directResults = directResults.slice(1);
    }

    // Reabstract the value if the types don't match.  This can happen
    // due to either substitution reabstractions or bridging.
    SILType loweredResultTy = substTL.getLoweredType();
    if (value.getType().hasAbstractionDifference(rep, loweredResultTy)) {
      Conversion conversion = [&] {
        // Assume that a C-language API doesn't have substitution
        // reabstractions.  This shouldn't be necessary, but
        // emitOrigToSubstValue can get upset.
        if (getSILFunctionLanguage(rep) == SILFunctionLanguage::C) {
          return Conversion::getBridging(Conversion::BridgeResultFromObjC,
                                         origType.getType(), substType,
                                         loweredResultTy);
        } else {
          return Conversion::getOrigToSubst(origType, substType,
                                            loweredResultTy);
        }
      }();

      // Attempt to peephole this conversion into the context.
      if (init) {
        if (auto outerConversion = init->getAsConversion()) {
          if (outerConversion->tryPeephole(SGF, loc, value, conversion)) {
            outerConversion->finishInitialization(SGF);
            return RValue::forInContext();
          }
        }
      }

      // If that wasn't possible, just apply the conversion.
      value = conversion.emit(SGF, loc, value, SGFContext(init));

      // If that successfully emitted into the initialization, we're done.
      if (value.isInContext()) {
        return RValue::forInContext();
      }
    }

    // Otherwise, forcibly emit into the initialization if it exists.
    if (init) {
      init->copyOrInitValueInto(SGF, loc, value, /*init*/ true);
      init->finishInitialization(SGF);
      return RValue::forInContext();

      // Otherwise, we've got the r-value we want.
    } else {
      return RValue(SGF, loc, substType, value);
    }
  }

  void
  gatherIndirectResultAddrs(SILGenFunction &SGF, SILLocation loc,
                            SmallVectorImpl<SILValue> &outList) const override {
    if (!temporary)
      return;
    outList.emplace_back(temporary->getAddress());
  }
};

/// A result plan which calls copyOrInitValueInto on an Initialization
/// using a temporary buffer initialized by a sub-plan.
class InitValueFromTemporaryResultPlan final : public ResultPlan {
  Initialization *init;
  ResultPlanPtr subPlan;
  std::unique_ptr<TemporaryInitialization> temporary;

public:
  InitValueFromTemporaryResultPlan(
      Initialization *init, ResultPlanPtr &&subPlan,
      std::unique_ptr<TemporaryInitialization> &&temporary)
      : init(init), subPlan(std::move(subPlan)),
        temporary(std::move(temporary)) {}

  RValue finish(SILGenFunction &SGF, SILLocation loc, CanType substType,
                ArrayRef<ManagedValue> &directResults,
                SILValue bridgedForeignError) override {
    RValue subResult = subPlan->finish(SGF, loc, substType, directResults,
                                       bridgedForeignError);
    assert(subResult.isInContext() && "sub-plan didn't emit into context?");
    (void)subResult;

    ManagedValue value = temporary->getManagedAddress();
    init->copyOrInitValueInto(SGF, loc, value, /*init*/ true);
    init->finishInitialization(SGF);

    return RValue::forInContext();
  }

  void
  gatherIndirectResultAddrs(SILGenFunction &SGF, SILLocation loc,
                            SmallVectorImpl<SILValue> &outList) const override {
    subPlan->gatherIndirectResultAddrs(SGF, loc, outList);
  }
};

/// A result plan which calls copyOrInitValueInto using the result of
/// a sub-plan.
class InitValueFromRValueResultPlan final : public ResultPlan {
  Initialization *init;
  ResultPlanPtr subPlan;

public:
  InitValueFromRValueResultPlan(Initialization *init, ResultPlanPtr &&subPlan)
      : init(init), subPlan(std::move(subPlan)) {}

  RValue finish(SILGenFunction &SGF, SILLocation loc, CanType substType,
                ArrayRef<ManagedValue> &directResults,
                SILValue bridgedForeignError) override {
    RValue subResult = subPlan->finish(SGF, loc, substType, directResults,
                                       bridgedForeignError);
    ManagedValue value = std::move(subResult).getAsSingleValue(SGF, loc);

    init->copyOrInitValueInto(SGF, loc, value, /*init*/ true);
    init->finishInitialization(SGF);

    return RValue::forInContext();
  }

  void
  gatherIndirectResultAddrs(SILGenFunction &SGF, SILLocation loc,
                            SmallVectorImpl<SILValue> &outList) const override {
    subPlan->gatherIndirectResultAddrs(SGF, loc, outList);
  }
};

/// A result plan which produces a larger RValue from a bunch of
/// components.
class TupleRValueResultPlan final : public ResultPlan {
  SmallVector<ResultPlanPtr, 4> eltPlans;

public:
  TupleRValueResultPlan(ResultPlanBuilder &builder, AbstractionPattern origType,
                        CanTupleType substType) {
    // Create plans for all the elements.
    eltPlans.reserve(substType->getNumElements());
    for (auto i : indices(substType->getElementTypes())) {
      AbstractionPattern origEltType = origType.getTupleElementType(i);
      CanType substEltType = substType.getElementType(i);
      eltPlans.push_back(builder.build(nullptr, origEltType, substEltType));
    }
  }

  RValue finish(SILGenFunction &SGF, SILLocation loc, CanType substType,
                ArrayRef<ManagedValue> &directResults,
                SILValue bridgedForeignError) override {
    RValue tupleRV(substType);

    // Finish all the component tuples.
    auto substTupleType = cast<TupleType>(substType);
    assert(substTupleType.getElementTypes().size() == eltPlans.size());
    for (auto i : indices(substTupleType.getElementTypes())) {
      RValue eltRV =
          eltPlans[i]->finish(SGF, loc, substTupleType.getElementType(i),
                              directResults, bridgedForeignError);
      tupleRV.addElement(std::move(eltRV));
    }

    return tupleRV;
  }

  void
  gatherIndirectResultAddrs(SILGenFunction &SGF, SILLocation loc,
                            SmallVectorImpl<SILValue> &outList) const override {
    for (const auto &eltPlan : eltPlans) {
      eltPlan->gatherIndirectResultAddrs(SGF, loc, outList);
    }
  }
};

/// A result plan which evaluates into the sub-components
/// of a splittable tuple initialization.
class TupleInitializationResultPlan final : public ResultPlan {
  Initialization *tupleInit;
  SmallVector<InitializationPtr, 4> eltInitsBuffer;
  MutableArrayRef<InitializationPtr> eltInits;
  SmallVector<ResultPlanPtr, 4> eltPlans;

public:
  TupleInitializationResultPlan(ResultPlanBuilder &builder,
                                Initialization *tupleInit,
                                AbstractionPattern origType,
                                CanTupleType substType)
      : tupleInit(tupleInit) {

    // Get the sub-initializations.
    eltInits = tupleInit->splitIntoTupleElements(builder.SGF, builder.loc,
                                                 substType, eltInitsBuffer);

    // Create plans for all the sub-initializations.
    eltPlans.reserve(substType->getNumElements());
    for (auto i : indices(substType->getElementTypes())) {
      AbstractionPattern origEltType = origType.getTupleElementType(i);
      CanType substEltType = substType.getElementType(i);
      Initialization *eltInit = eltInits[i].get();
      eltPlans.push_back(builder.build(eltInit, origEltType, substEltType));
    }
  }

  RValue finish(SILGenFunction &SGF, SILLocation loc, CanType substType,
                ArrayRef<ManagedValue> &directResults,
                SILValue bridgedForeignError) override {
    auto substTupleType = cast<TupleType>(substType);
    assert(substTupleType.getElementTypes().size() == eltPlans.size());
    for (auto i : indices(substTupleType.getElementTypes())) {
      auto eltType = substTupleType.getElementType(i);
      RValue eltRV = eltPlans[i]->finish(SGF, loc, eltType, directResults,
                                         bridgedForeignError);
      assert(eltRV.isInContext());
      (void)eltRV;
    }
    tupleInit->finishInitialization(SGF);

    return RValue::forInContext();
  }

  void
  gatherIndirectResultAddrs(SILGenFunction &SGF, SILLocation loc,
                            SmallVectorImpl<SILValue> &outList) const override {
    for (const auto &eltPlan : eltPlans) {
      eltPlan->gatherIndirectResultAddrs(SGF, loc, outList);
    }
  }
};

class ForeignAsyncInitializationPlan final : public ResultPlan {
  SILLocation loc;
  CalleeTypeInfo calleeTypeInfo;
  SILType opaqueResumeType;
  SILValue resumeBuf;
  SILValue continuation;
  
public:
  ForeignAsyncInitializationPlan(SILGenFunction &SGF, SILLocation loc,
                                 const CalleeTypeInfo &calleeTypeInfo)
    : loc(loc), calleeTypeInfo(calleeTypeInfo)
  {
    // Allocate space to receive the resume value when the continuation is
    // resumed.
    opaqueResumeType =
        SGF.getLoweredType(AbstractionPattern(calleeTypeInfo.substResultType),
                           calleeTypeInfo.substResultType);
    resumeBuf = SGF.emitTemporaryAllocation(loc, opaqueResumeType);
  }
  
  void
  gatherIndirectResultAddrs(SILGenFunction &SGF, SILLocation loc,
                            SmallVectorImpl<SILValue> &outList) const override {
    // A foreign async function shouldn't have any indirect results.
  }

  ManagedValue
  emitForeignAsyncCompletionHandler(SILGenFunction &SGF,
                                    AbstractionPattern origFormalType,
                                    SILLocation loc) override {
    // Get the current continuation for the task.
    bool throws =
        calleeTypeInfo.foreign.async->completionHandlerErrorParamIndex()
            .hasValue() ||
        calleeTypeInfo.foreign.error.hasValue();

    continuation = SGF.B.createGetAsyncContinuationAddr(loc, resumeBuf,
                               calleeTypeInfo.substResultType, throws);

    // Wrap the Builtin.RawUnsafeContinuation in an
    // UnsafeContinuation<T, E>.
    auto continuationDecl = SGF.getASTContext().getUnsafeContinuationDecl();

    auto errorTy = throws
      ? SGF.getASTContext().getErrorExistentialType()
      : SGF.getASTContext().getNeverType();
    auto continuationTy = BoundGenericType::get(continuationDecl, Type(),
                                                { calleeTypeInfo.substResultType, errorTy })
      ->getCanonicalType();
    auto wrappedContinuation =
        SGF.B.createStruct(loc,
                           SILType::getPrimitiveObjectType(continuationTy),
                           {continuation});

    // Stash it in a buffer for a block object.
    auto blockStorageTy = SILType::getPrimitiveAddressType(
        SILBlockStorageType::get(continuationTy));
    auto blockStorage = SGF.emitTemporaryAllocation(loc, blockStorageTy);
    auto continuationAddr = SGF.B.createProjectBlockStorage(loc, blockStorage);
    SGF.B.createStore(loc, wrappedContinuation, continuationAddr,
                      StoreOwnershipQualifier::Trivial);

    // Get the block invocation function for the given completion block type.
    auto completionHandlerIndex = calleeTypeInfo.foreign.async
      ->completionHandlerParamIndex();
    auto impTy = SGF.getSILType(calleeTypeInfo.substFnType
                                      ->getParameters()[completionHandlerIndex],
                                calleeTypeInfo.substFnType);
    bool handlerIsOptional;
    CanSILFunctionType impFnTy;
    if (auto impObjTy = impTy.getOptionalObjectType()) {
      handlerIsOptional = true;
      impFnTy = cast<SILFunctionType>(impObjTy.getASTType());
    } else {
      handlerIsOptional = false;
      impFnTy = cast<SILFunctionType>(impTy.getASTType());
    }
    auto env = SGF.F.getGenericEnvironment();
    auto sig = env ? env->getGenericSignature().getCanonicalSignature()
                   : CanGenericSignature();
    SILFunction *impl =
        SGF.SGM.getOrCreateForeignAsyncCompletionHandlerImplFunction(
            cast<SILFunctionType>(
                impFnTy->mapTypeOutOfContext()->getCanonicalType(sig)),
            continuationTy->mapTypeOutOfContext()->getCanonicalType(sig),
            origFormalType, sig, *calleeTypeInfo.foreign.async,
            calleeTypeInfo.foreign.error);
    auto impRef = SGF.B.createFunctionRef(loc, impl);
    
    // Initialize the block object for the completion handler.
    SILValue block = SGF.B.createInitBlockStorageHeader(loc, blockStorage,
                          impRef, SILType::getPrimitiveObjectType(impFnTy),
                          SGF.getForwardingSubstitutionMap());
    
    // Wrap it in optional if the callee expects it.
    if (handlerIsOptional) {
      block = SGF.B.createOptionalSome(loc, block, impTy);
    }
    
    // We don't need to manage the block because it's still on the stack. We
    // know we won't escape it locally so the callee can be responsible for
    // _Block_copy-ing it.
    return ManagedValue::forUnmanaged(block);
  }

  RValue finish(SILGenFunction &SGF, SILLocation loc, CanType substType,
                ArrayRef<ManagedValue> &directResults,
                SILValue bridgedForeignError) override {
    // There should be no direct results from the call.
    assert(directResults.empty());
    
    // Await the continuation we handed off to the completion handler.
    SILBasicBlock *resumeBlock = SGF.createBasicBlock();
    SILBasicBlock *errorBlock = nullptr;
    bool throws =
        calleeTypeInfo.foreign.async->completionHandlerErrorParamIndex()
            .hasValue() ||
        calleeTypeInfo.foreign.error.hasValue();
    if (throws) {
      errorBlock = SGF.createBasicBlock(FunctionSection::Postmatter);
    }

    auto *awaitBB = SGF.B.getInsertionBB();
    if (bridgedForeignError) {
      // Avoid a critical edge from the block which branches to the await and
      // foreign error blocks to the await block (to which the error block will
      // be made to branch in a moment) by introducing a trampoline which will
      // branch to the await block.
      awaitBB = SGF.createBasicBlock();
      SGF.B.createBranch(loc, awaitBB);

      // Finish emitting the foreign error block:
      // (1) fulfill the unsafe continuation with the foreign error
      // (2) branch to the await block
      {
        // First, fulfill the unsafe continuation with the foreign error.
        // Currently, that block's code looks something like
        //     %foreignError = ... : $*Optional<NSError>
        //     %converter = function_ref _convertNSErrorToError(_:)
        //     %error = apply %converter(%foreignError)
        //     [... insert here ...]
        //     destroy_value %error
        //     destroy_value %foreignError
        // Insert code to fulfill it after the native %error is defined.  That
        // code should structure the RawUnsafeContinuation (continuation) into
        // an appropriately typed UnsafeContinuation and then pass that together
        // with (a copy of) the error to
        // _resumeUnsafeThrowingContinuationWithError.
        // [foreign_error_block_with_foreign_async_convention]
        SGF.B.setInsertionPoint(
            ++bridgedForeignError->getDefiningInstruction()->getIterator());

        auto continuationDecl = SGF.getASTContext().getUnsafeContinuationDecl();

        auto errorTy = SGF.getASTContext().getErrorExistentialType();
        auto continuationBGT =
            BoundGenericType::get(continuationDecl, Type(),
                                  {calleeTypeInfo.substResultType, errorTy});
        auto env = SGF.F.getGenericEnvironment();
        auto sig = env ? env->getGenericSignature().getCanonicalSignature()
                       : CanGenericSignature();
        auto mappedContinuationTy =
            continuationBGT->mapTypeOutOfContext()->getCanonicalType(sig);
        auto resumeType =
            cast<BoundGenericType>(mappedContinuationTy).getGenericArgs()[0];
        auto continuationTy = continuationBGT->getCanonicalType();

        auto errorIntrinsic =
            SGF.SGM.getResumeUnsafeThrowingContinuationWithError();
        Type replacementTypes[] = {
            SGF.F.mapTypeIntoContext(resumeType)->getCanonicalType()};
        auto subs = SubstitutionMap::get(errorIntrinsic->getGenericSignature(),
                                         replacementTypes,
                                         ArrayRef<ProtocolConformanceRef>{});
        auto wrappedContinuation = SGF.B.createStruct(
            loc, SILType::getPrimitiveObjectType(continuationTy),
            {continuation});

        auto continuationMV =
            ManagedValue::forUnmanaged(SILValue(wrappedContinuation));
        SGF.emitApplyOfLibraryIntrinsic(
            loc, errorIntrinsic, subs,
            {continuationMV,
             ManagedValue::forUnmanaged(bridgedForeignError).copy(SGF, loc)},
            SGFContext());

        // Second, emit a branch from the end of the foreign error block to the
        // await block, to await the continuation which was just fulfilled.
        SGF.B.setInsertionPoint(
            bridgedForeignError->getDefiningInstruction()->getParent());
        SGF.B.createBranch(loc, awaitBB);
      }

      SGF.B.emitBlock(awaitBB);
    }
    SGF.B.createAwaitAsyncContinuation(loc, continuation, resumeBlock, errorBlock);
    
    // Propagate an error if we have one.
    if (errorBlock) {
      SGF.B.emitBlock(errorBlock);
      
      Scope errorScope(SGF, loc);

      auto errorTy = SGF.getASTContext().getErrorExistentialType();
      auto errorVal = SGF.B.createTermResult(
        SILType::getPrimitiveObjectType(errorTy), OwnershipKind::Owned);

      SGF.emitThrow(loc, errorVal, true);
    }
    
    SGF.B.emitBlock(resumeBlock);
    
    // The incoming value is the maximally-abstracted result type of the
    // continuation. Move it out of the resume buffer and reabstract it if
    // necessary.
    auto resumeResult = SGF.emitLoad(loc, resumeBuf,
      calleeTypeInfo.origResultType
         ? *calleeTypeInfo.origResultType
         : AbstractionPattern(calleeTypeInfo.substResultType),
                 calleeTypeInfo.substResultType,
                 SGF.getTypeLowering(calleeTypeInfo.substResultType),
                 SGFContext(), IsTake);
    
    return RValue(SGF, loc, calleeTypeInfo.substResultType, resumeResult);
  }
};

class ForeignErrorInitializationPlan final : public ResultPlan {
  SILLocation loc;
  LValue lvalue;
  ResultPlanPtr subPlan;
  ManagedValue managedErrorTemp;
  CanType unwrappedPtrType;
  PointerTypeKind ptrKind;
  bool isOptional;
  CanType errorPtrType;

public:
  ForeignErrorInitializationPlan(SILGenFunction &SGF, SILLocation loc,
                                 const CalleeTypeInfo &calleeTypeInfo,
                                 ResultPlanPtr &&subPlan)
      : loc(loc), subPlan(std::move(subPlan)) {
    unsigned errorParamIndex =
        calleeTypeInfo.foreign.error->getErrorParameterIndex();
    auto substFnType = calleeTypeInfo.substFnType;
    SILParameterInfo errorParameter =
        substFnType->getParameters()[errorParamIndex];
    // We assume that there's no interesting reabstraction here beyond a layer
    // of optional.
    errorPtrType = errorParameter.getArgumentType(
        SGF.SGM.M, substFnType, SGF.getTypeExpansionContext());
    unwrappedPtrType = errorPtrType;
    Type unwrapped = errorPtrType->getOptionalObjectType();
    isOptional = (bool) unwrapped;

    if (unwrapped)
      unwrappedPtrType = unwrapped->getCanonicalType();

    auto errorType =
        CanType(unwrappedPtrType->getAnyPointerElementType(ptrKind));
    auto &errorTL = SGF.getTypeLowering(errorType);

    // Allocate a temporary.
    // It's flagged with "hasDynamicLifetime" because it's not possible to
    // statically verify the lifetime of the value.
    SILValue errorTemp =
        SGF.emitTemporaryAllocation(loc, errorTL.getLoweredType(),
                                    /*hasDynamicLifetime*/ true);

    // Nil-initialize it.
    SGF.emitInjectOptionalNothingInto(loc, errorTemp, errorTL);

    // Enter a cleanup to destroy the value there.
    managedErrorTemp = SGF.emitManagedBufferWithCleanup(errorTemp, errorTL);

    // Create the appropriate pointer type.
    lvalue = LValue::forAddress(SGFAccessKind::ReadWrite,
                                ManagedValue::forLValue(errorTemp),
                                /*TODO: enforcement*/ None,
                                AbstractionPattern(errorType), errorType);
  }

  RValue finish(SILGenFunction &SGF, SILLocation loc, CanType substType,
                ArrayRef<ManagedValue> &directResults,
                SILValue bridgedForeignError) override {
    return subPlan->finish(SGF, loc, substType, directResults,
                           bridgedForeignError);
  }

  void
  gatherIndirectResultAddrs(SILGenFunction &SGF, SILLocation loc,
                            SmallVectorImpl<SILValue> &outList) const override {
    subPlan->gatherIndirectResultAddrs(SGF, loc, outList);
  }

  ManagedValue
  emitForeignAsyncCompletionHandler(SILGenFunction &SGF,
                                    AbstractionPattern origFormalType,
                                    SILLocation loc) override {
    return subPlan->emitForeignAsyncCompletionHandler(SGF, origFormalType, loc);
  }

  Optional<std::pair<ManagedValue, ManagedValue>>
  emitForeignErrorArgument(SILGenFunction &SGF, SILLocation loc) override {
    SILGenFunction::PointerAccessInfo pointerInfo = {
      unwrappedPtrType, ptrKind, SGFAccessKind::ReadWrite
    };
    auto pointerValue =
        SGF.emitLValueToPointer(loc, std::move(lvalue), pointerInfo);

    // Wrap up in an Optional if called for.
    if (isOptional) {
      auto &optTL = SGF.getTypeLowering(errorPtrType);
      pointerValue = SGF.getOptionalSomeValue(loc, pointerValue, optTL);
    }

    return std::make_pair(managedErrorTemp, pointerValue);
  }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
//                            Result Plan Builder
//===----------------------------------------------------------------------===//

/// Build a result plan for the results of an apply.
///
/// If the initialization is non-null, the result plan will emit into it.
ResultPlanPtr ResultPlanBuilder::buildTopLevelResult(Initialization *init,
                                                     SILLocation loc) {
  // First check if we have a foreign error and/or async convention.
  if (auto foreignError = calleeTypeInfo.foreign.error) {
    // Handle the foreign error first.
    //
    // The plan needs to be built using the formal result type after foreign-error
    // adjustment.
    switch (foreignError->getKind()) {
    // These conventions make the formal result type ().
    case ForeignErrorConvention::ZeroResult:
    case ForeignErrorConvention::NonZeroResult:
      assert(calleeTypeInfo.substResultType->isVoid() ||
             calleeTypeInfo.foreign.async);
      allResults.clear();
      break;

    // These conventions leave the formal result alone.
    case ForeignErrorConvention::ZeroPreservedResult:
    case ForeignErrorConvention::NonNilError:
      break;

    // This convention changes the formal result to the optional object type; we
    // need to make our own make SILResultInfo array.
    case ForeignErrorConvention::NilResult: {
      assert(allResults.size() == 1);
      auto substFnTy = calleeTypeInfo.substFnType;
      CanType objectType = allResults[0]
                               .getReturnValueType(SGF.SGM.M, substFnTy,
                                                   SGF.getTypeExpansionContext())
                               .getOptionalObjectType();
      SILResultInfo optResult = allResults[0].getWithInterfaceType(objectType);
      allResults.clear();
      allResults.push_back(optResult);
      break;
    }
    }

    ResultPlanPtr subPlan;
    if (auto foreignAsync = calleeTypeInfo.foreign.async) {
      subPlan = ResultPlanPtr(
          new ForeignAsyncInitializationPlan(SGF, loc, calleeTypeInfo));
    } else {
      subPlan = build(init, calleeTypeInfo.origResultType.getValue(),
                      calleeTypeInfo.substResultType);
    }
    return ResultPlanPtr(new ForeignErrorInitializationPlan(
        SGF, loc, calleeTypeInfo, std::move(subPlan)));
  } else if (auto foreignAsync = calleeTypeInfo.foreign.async) {
    // Create a result plan that gets the result schema from the completion
    // handler callback's arguments.
    return ResultPlanPtr(
        new ForeignAsyncInitializationPlan(SGF, loc, calleeTypeInfo));
  } else {
    // Otherwise, we can just call build.
    return build(init, calleeTypeInfo.origResultType.getValue(),
                 calleeTypeInfo.substResultType);
  }
}

/// Build a result plan for the results of an apply.
///
/// If the initialization is non-null, the result plan will emit into it.
ResultPlanPtr ResultPlanBuilder::build(Initialization *init,
                                       AbstractionPattern origType,
                                       CanType substType) {
  // Destructure original tuples.
  if (origType.isTuple()) {
    return buildForTuple(init, origType, cast<TupleType>(substType));
  }

  // Otherwise, grab the next result.
  auto result = allResults.pop_back_val();

  auto calleeTy = calleeTypeInfo.substFnType;
  
  // If the result is indirect, and we have an address to emit into, and
  // there are no abstraction differences, then just do it.
  if (init && init->canPerformInPlaceInitialization() &&
      SGF.silConv.isSILIndirect(result) &&
      !SGF.getLoweredType(substType).getAddressType().hasAbstractionDifference(
          calleeTypeInfo.getOverrideRep(),
          result.getSILStorageType(SGF.SGM.M, calleeTy,
                                   SGF.getTypeExpansionContext()))) {
    return ResultPlanPtr(new InPlaceInitializationResultPlan(init));
  }

  // Otherwise, we need to:
  //   - get the value, either directly or indirectly
  //   - possibly reabstract it
  //   - store it to the destination
  // We could break this down into different ResultPlan implementations,
  // but it's easier not to.
  
  // If the result type involves an indirectly-returned opened existential,
  // then we need to evaluate the arguments first in order to have access to
  // the opened Self type. A special result plan defers allocating the stack
  // slot to the point the call is emitted.
  if (result
          .getReturnValueType(SGF.SGM.M, calleeTy,
                              SGF.getTypeExpansionContext())
          ->hasOpenedExistential() &&
      SGF.silConv.isSILIndirect(result)) {
    return ResultPlanPtr(
      new IndirectOpenedSelfResultPlan(SGF, origType, substType));
  }

  // Create a temporary if the result is indirect.
  std::unique_ptr<TemporaryInitialization> temporary;
  if (SGF.silConv.isSILIndirect(result)) {
    auto &resultTL = SGF.getTypeLowering(result.getReturnValueType(
        SGF.SGM.M, calleeTy, SGF.getTypeExpansionContext()));
    temporary = SGF.emitTemporary(loc, resultTL);
  }

  return ResultPlanPtr(new ScalarResultPlan(
      std::move(temporary), origType, init, calleeTypeInfo.getOverrideRep()));
}

ResultPlanPtr ResultPlanBuilder::buildForTuple(Initialization *init,
                                               AbstractionPattern origType,
                                               CanTupleType substType) {
  // If we don't have an initialization for the tuple, just build the
  // individual components.
  if (!init) {
    return ResultPlanPtr(new TupleRValueResultPlan(*this, origType, substType));
  }

  // Okay, we have an initialization for the tuple that we need to emit into.

  // If we can just split the initialization, do so.
  if (init->canSplitIntoTupleElements()) {
    return ResultPlanPtr(
        new TupleInitializationResultPlan(*this, init, origType, substType));
  }

  // Otherwise, we're going to have to call copyOrInitValueInto, which only
  // takes a single value.

  // If the tuple is address-only, we'll get much better code if we
  // emit into a single buffer.
  auto &substTL = SGF.getTypeLowering(substType);
  if (substTL.isAddressOnly()) {
    // Create a temporary.
    auto temporary = SGF.emitTemporary(loc, substTL);

    // Build a sub-plan to emit into the temporary.
    auto subplan = buildForTuple(temporary.get(), origType, substType);

    // Make a plan to initialize into that.
    return ResultPlanPtr(new InitValueFromTemporaryResultPlan(
        init, std::move(subplan), std::move(temporary)));
  }

  // Build a sub-plan that doesn't know about the initialization.
  auto subplan = buildForTuple(nullptr, origType, substType);

  // Make a plan that calls copyOrInitValueInto.
  return ResultPlanPtr(
      new InitValueFromRValueResultPlan(init, std::move(subplan)));
}

ResultPlanPtr
ResultPlanBuilder::computeResultPlan(SILGenFunction &SGF,
                                     const CalleeTypeInfo &calleeTypeInfo,
                                     SILLocation loc, SGFContext evalContext) {
  ResultPlanBuilder builder(SGF, loc, calleeTypeInfo);
  return builder.buildTopLevelResult(evalContext.getEmitInto(), loc);
}
