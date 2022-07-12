//===--- SILGenConvert.cpp - Type Conversion Routines ---------------------===//
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

#include "SILGen.h"
#include "ArgumentSource.h"
#include "Conversion.h"
#include "Initialization.h"
#include "LValue.h"
#include "RValue.h"
#include "Scope.h"
#include "SwitchCaseFullExpr.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Decl.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/SubstitutionMap.h"
#include "swift/AST/Types.h"
#include "swift/Basic/type_traits.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/TypeLowering.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"

using namespace swift;
using namespace Lowering;

// FIXME: With some changes to their callers, all of the below functions
// could be re-worked to use emitInjectEnum().
ManagedValue
SILGenFunction::emitInjectOptional(SILLocation loc,
                                   const TypeLowering &optTL,
                                   SGFContext ctxt,
                      llvm::function_ref<ManagedValue(SGFContext)> generator) {
  SILType optTy = optTL.getLoweredType();
  SILType objectTy = optTy.getAnyOptionalObjectType();
  assert(objectTy && "expected type was not optional");

  auto someDecl = getASTContext().getOptionalSomeDecl();

  // If the value is loadable, just emit and wrap.
  // TODO: honor +0 contexts?
  if (optTL.isLoadable() || !silConv.useLoweredAddresses()) {
    ManagedValue objectResult = generator(SGFContext());
    auto some = B.createEnum(loc, objectResult.forward(*this), someDecl, optTy);
    return emitManagedRValueWithCleanup(some, optTL);
  }

  // Otherwise it's address-only; try to avoid spurious copies by
  // evaluating into the context.

  // Prepare a buffer for the object value.
  return B.bufferForExpr(
      loc, optTy.getObjectType(), optTL, ctxt,
      [&](SILValue optBuf) {
        auto objectBuf = B.createInitEnumDataAddr(loc, optBuf, someDecl, objectTy);

        // Evaluate the value in-place into that buffer.
        TemporaryInitialization init(objectBuf, CleanupHandle::invalid());
        ManagedValue objectResult = generator(SGFContext(&init));
        if (!objectResult.isInContext()) {
          objectResult.forwardInto(*this, loc, objectBuf);
        }

        // Finalize the outer optional buffer.
        B.createInjectEnumAddr(loc, optBuf, someDecl);
      });
}

void SILGenFunction::emitInjectOptionalValueInto(SILLocation loc,
                                                 ArgumentSource &&value,
                                                 SILValue dest,
                                                 const TypeLowering &optTL) {
  SILType optType = optTL.getLoweredType();
  assert(dest->getType() == optType.getAddressType());
  auto loweredPayloadTy = optType.getAnyOptionalObjectType();
  assert(loweredPayloadTy);

  // Project out the payload area.
  auto someDecl = getASTContext().getOptionalSomeDecl();
  auto destPayload =
    B.createInitEnumDataAddr(loc, dest, someDecl,
                             loweredPayloadTy.getAddressType());
  
  // Emit the value into the payload area.
  TemporaryInitialization emitInto(destPayload, CleanupHandle::invalid());
  std::move(value).forwardInto(*this, &emitInto);
  
  // Inject the tag.
  B.createInjectEnumAddr(loc, dest, someDecl);
}

void SILGenFunction::emitInjectOptionalNothingInto(SILLocation loc, 
                                                   SILValue dest,
                                                   const TypeLowering &optTL) {
  assert(optTL.getLoweredType().getAnyOptionalObjectType());
  
  B.createInjectEnumAddr(loc, dest, getASTContext().getOptionalNoneDecl());
}
      
/// Return a value for an optional ".None" of the specified type. This only
/// works for loadable enum types.
SILValue SILGenFunction::getOptionalNoneValue(SILLocation loc,
                                              const TypeLowering &optTL) {
  assert((optTL.isLoadable() || !silConv.useLoweredAddresses()) &&
         "Address-only optionals cannot use this");
  assert(optTL.getLoweredType().getAnyOptionalObjectType());

  return B.createEnum(loc, SILValue(), getASTContext().getOptionalNoneDecl(),
                      optTL.getLoweredType());
}

/// Return a value for an optional ".Some(x)" of the specified type. This only
/// works for loadable enum types.
ManagedValue SILGenFunction::
getOptionalSomeValue(SILLocation loc, ManagedValue value,
                     const TypeLowering &optTL) {
  assert((optTL.isLoadable() || !silConv.useLoweredAddresses()) &&
         "Address-only optionals cannot use this");
  SILType optType = optTL.getLoweredType();
  CanType formalOptType = optType.getSwiftRValueType();
  (void)formalOptType;

  assert(formalOptType.getAnyOptionalObjectType());
  auto someDecl = getASTContext().getOptionalSomeDecl();
  
  SILValue result =
    B.createEnum(loc, value.forward(*this), someDecl, optTL.getLoweredType());
  return emitManagedRValueWithCleanup(result, optTL);
}

auto SILGenFunction::emitSourceLocationArgs(SourceLoc sourceLoc,
                                            SILLocation emitLoc)
-> SourceLocArgs {
  auto &ctx = getASTContext();
  
  StringRef filename = "";
  unsigned line = 0;
  unsigned column = 0;
  if (sourceLoc.isValid()) {
    unsigned bufferID = ctx.SourceMgr.findBufferContainingLoc(sourceLoc);
    filename = ctx.SourceMgr.getIdentifierForBuffer(bufferID);
    std::tie(line, column) = ctx.SourceMgr.getLineAndColumn(sourceLoc);
  }
  
  bool isASCII = true;
  for (unsigned char c : filename) {
    if (c > 127) {
      isASCII = false;
      break;
    }
  }
  
  auto wordTy = SILType::getBuiltinWordType(ctx);
  auto i1Ty = SILType::getBuiltinIntegerType(1, ctx);
  
  SourceLocArgs result;
  SILValue literal = B.createStringLiteral(emitLoc, filename,
                                           StringLiteralInst::Encoding::UTF8);
  result.filenameStartPointer = ManagedValue::forUnmanaged(literal);
  // File length
  literal = B.createIntegerLiteral(emitLoc, wordTy, filename.size());
  result.filenameLength = ManagedValue::forUnmanaged(literal);
  // File is ascii
  literal = B.createIntegerLiteral(emitLoc, i1Ty, isASCII);
  result.filenameIsAscii = ManagedValue::forUnmanaged(literal);
  // Line
  literal = B.createIntegerLiteral(emitLoc, wordTy, line);
  result.line = ManagedValue::forUnmanaged(literal);
  // Column
  literal = B.createIntegerLiteral(emitLoc, wordTy, column);
  result.column = ManagedValue::forUnmanaged(literal);
  
  return result;
}

ManagedValue
SILGenFunction::emitPreconditionOptionalHasValue(SILLocation loc,
                                                 ManagedValue optional) {
  // Generate code to the optional is present, and if not, abort with a message
  // (provided by the stdlib).
  SILBasicBlock *contBB = createBasicBlock();
  SILBasicBlock *failBB = createBasicBlock();

  bool hadCleanup = optional.hasCleanup();
  bool hadLValue = optional.isLValue();

  auto noneDecl = getASTContext().getOptionalNoneDecl();
  auto someDecl = getASTContext().getOptionalSomeDecl();
  if (optional.getType().isAddress()) {
    // We forward in the creation routine for
    // unchecked_take_enum_data_addr. switch_enum_addr is a +0 operation.
    B.createSwitchEnumAddr(loc, optional.getValue(),
                           /*defaultDest*/ nullptr,
                           {{someDecl, contBB}, {noneDecl, failBB}});
  } else {
    B.createSwitchEnum(loc, optional.forward(*this),
                       /*defaultDest*/ nullptr,
                       {{someDecl, contBB}, {noneDecl, failBB}});
  }
  B.emitBlock(failBB);

  // Call the standard library implementation of _diagnoseUnexpectedNilOptional.
  if (auto diagnoseFailure =
        getASTContext().getDiagnoseUnexpectedNilOptional(nullptr)) {
    auto args = emitSourceLocationArgs(loc.getSourceLoc(), loc);
    
    emitApplyOfLibraryIntrinsic(loc, diagnoseFailure, SubstitutionMap(),
                                {
                                  args.filenameStartPointer,
                                  args.filenameLength,
                                  args.filenameIsAscii,
                                  args.line
                                },
                                SGFContext());
  }

  B.createUnreachable(ArtificialUnreachableLocation());
  B.clearInsertionPoint();
  B.emitBlock(contBB);

  ManagedValue result;
  SILType payloadType = optional.getType().getAnyOptionalObjectType();

  if (payloadType.isObject()) {
    result = B.createOwnedPHIArgument(payloadType);
  } else {
    result =
        B.createUncheckedTakeEnumDataAddr(loc, optional, someDecl, payloadType);
  }

  if (hadCleanup) {
    return result;
  }

  if (hadLValue) {
    return ManagedValue::forLValue(result.forward(*this));
  }

  return ManagedValue::forUnmanaged(result.forward(*this));
}

SILValue SILGenFunction::emitDoesOptionalHaveValue(SILLocation loc,
                                                   SILValue addrOrValue) {
  auto boolTy = SILType::getBuiltinIntegerType(1, getASTContext());
  SILValue yes = B.createIntegerLiteral(loc, boolTy, 1);
  SILValue no = B.createIntegerLiteral(loc, boolTy, 0);
  auto someDecl = getASTContext().getOptionalSomeDecl();
  
  if (addrOrValue->getType().isAddress())
    return B.createSelectEnumAddr(loc, addrOrValue, boolTy, no,
                                  std::make_pair(someDecl, yes));
  return B.createSelectEnum(loc, addrOrValue, boolTy, no,
                            std::make_pair(someDecl, yes));
}

ManagedValue SILGenFunction::emitCheckedGetOptionalValueFrom(SILLocation loc,
                                                      ManagedValue src,
                                                      const TypeLowering &optTL,
                                                      SGFContext C) {
  // TODO: Make this take optTL.
  return emitPreconditionOptionalHasValue(loc, src);
}

ManagedValue SILGenFunction::emitUncheckedGetOptionalValueFrom(
    SILLocation loc, ManagedValue addrOrValue, const TypeLowering &optTL,
    SGFContext C) {
  SILType origPayloadTy =
    addrOrValue.getType().getAnyOptionalObjectType();

  auto someDecl = getASTContext().getOptionalSomeDecl();

  // Take the payload from the optional.
  if (!addrOrValue.getType().isAddress()) {
    return B.createUncheckedEnumData(loc, addrOrValue, someDecl);
  }

  // Cheat a bit in the +0 case--UncheckedTakeEnumData will never actually
  // invalidate an Optional enum value. This is specific to optionals.
  ManagedValue payload = B.createUncheckedTakeEnumDataAddr(
      loc, addrOrValue, someDecl, origPayloadTy);
  if (!optTL.isLoadable())
    return payload;

  // If we do not have a cleanup on our address, use a load_borrow.
  if (!payload.hasCleanup()) {
    return B.createLoadBorrow(loc, payload);
  }

  // Otherwise, perform a load take.
  return B.createLoadTake(loc, payload);
}

ManagedValue
SILGenFunction::emitOptionalSome(SILLocation loc, SILType optTy,
                                 ValueProducerRef produceValue,
                                 SGFContext C) {
  // If the conversion is a bridging conversion from an optional type,
  // do a bridging conversion from the non-optional type instead.
  // TODO: should this be a general thing for all conversions?
  if (auto optInit = C.getAsConversion()) {
    const auto &optConversion = optInit->getConversion();
    if (optConversion.isBridging()) {
      auto sourceValueType =
        optConversion.getBridgingSourceType().getAnyOptionalObjectType();
      assert(sourceValueType);
      if (auto valueConversion =
            optConversion.adjustForInitialOptionalConversions(sourceValueType)){
        return optInit->emitWithAdjustedConversion(*this, loc, *valueConversion,
                                                   produceValue);
      }
    }
  }

  auto &optTL = getTypeLowering(optTy);

  // If the type is loadable or we're not lowering address-only types
  // in SILGen, use a simple scalar pattern.
  if (!silConv.useLoweredAddresses() || optTL.isLoadable()) {
    auto value = produceValue(*this, loc, SGFContext());
    return getOptionalSomeValue(loc, value, optTL);
  }

  // Otherwise, emit into memory, preferably into an address from
  // the context.

  // Get an address to emit into.
  SILValue optAddr = getBufferForExprResult(loc, optTy, C);

  auto someDecl = getASTContext().getOptionalSomeDecl();

  auto valueTy = optTy.getAnyOptionalObjectType();  
  auto &valueTL = getTypeLowering(valueTy);

  // Project the value buffer within the address.
  SILValue valueAddr =
    B.createInitEnumDataAddr(loc, optAddr, someDecl,
                             valueTy.getAddressType());

  // Emit into the value buffer.
  auto valueInit = useBufferAsTemporary(valueAddr, valueTL);
  ManagedValue value = produceValue(*this, loc, SGFContext(valueInit.get()));
  if (!value.isInContext()) {
    valueInit->copyOrInitValueInto(*this, loc, value, /*isInit*/ true);
    valueInit->finishInitialization(*this);
  }

  // Kill the cleanup on the value.
  valueInit->getManagedAddress().forward(*this);

  // Finish the optional.
  B.createInjectEnumAddr(loc, optAddr, someDecl);

  return manageBufferForExprResult(optAddr, optTL, C);
}

/// Emit an optional-to-optional transformation.
ManagedValue
SILGenFunction::emitOptionalToOptional(SILLocation loc,
                                       ManagedValue input,
                                       SILType resultTy,
                                       ValueTransformRef transformValue,
                                       SGFContext C) {
  auto contBB = createBasicBlock();
  auto isNotPresentBB = createBasicBlock();
  auto isPresentBB = createBasicBlock();

  SwitchEnumBuilder SEBuilder(B, loc, input);
  SILType noOptResultTy = resultTy.getAnyOptionalObjectType();
  assert(noOptResultTy);

  // Create a temporary for the output optional.
  auto &resultTL = getTypeLowering(resultTy);

  // If the result is address-only, we need to return something in memory,
  // otherwise the result is the BBArgument in the merge point.
  // TODO: use the SGFContext passed in.
  ManagedValue finalResult;
  if (resultTL.isAddressOnly() && silConv.useLoweredAddresses()) {
    finalResult = emitManagedBufferWithCleanup(
        emitTemporaryAllocation(loc, resultTy), resultTL);
  } else {
    SILGenSavedInsertionPoint IP(*this, contBB);
    finalResult = B.createOwnedPHIArgument(resultTL.getLoweredType());
  }

  SEBuilder.addCase(
      getASTContext().getOptionalSomeDecl(), isPresentBB, contBB,
      [&](ManagedValue input, SwitchCaseFullExpr &scope) {
        // If we have an address only type, we want to match the old behavior of
        // transforming the underlying type instead of the optional type. This
        // ensures that we use the more efficient non-generic code paths when
        // possible.
        if (getTypeLowering(input.getType()).isAddressOnly() &&
            silConv.useLoweredAddresses()) {
          auto *someDecl = B.getASTContext().getOptionalSomeDecl();
          input = B.createUncheckedTakeEnumDataAddr(
              loc, input, someDecl, input.getType().getAnyOptionalObjectType());
        }

        ManagedValue result = transformValue(*this, loc, input, noOptResultTy,
                                             SGFContext());

        if (!(resultTL.isAddressOnly() && silConv.useLoweredAddresses())) {
          SILValue some = B.createOptionalSome(loc, result).forward(*this);
          return scope.exitAndBranch(loc, some);
        }

        RValue R(*this, loc, noOptResultTy.getSwiftRValueType(), result);
        ArgumentSource resultValueRV(loc, std::move(R));
        emitInjectOptionalValueInto(loc, std::move(resultValueRV),
                                    finalResult.getValue(), resultTL);
        return scope.exitAndBranch(loc);
      });

  SEBuilder.addCase(
      getASTContext().getOptionalNoneDecl(), isNotPresentBB, contBB,
      [&](ManagedValue input, SwitchCaseFullExpr &scope) {
        if (!(resultTL.isAddressOnly() && silConv.useLoweredAddresses())) {
          SILValue none =
              B.createManagedOptionalNone(loc, resultTy).forward(*this);
          return scope.exitAndBranch(loc, none);
        }

        emitInjectOptionalNothingInto(loc, finalResult.getValue(), resultTL);
        return scope.exitAndBranch(loc);
      });

  std::move(SEBuilder).emit();

  B.emitBlock(contBB);
  return finalResult;
}

SILGenFunction::OpaqueValueRAII::~OpaqueValueRAII() {
  auto entry = Self.OpaqueValues.find(OpaqueValue);
  assert(entry != Self.OpaqueValues.end());
  Self.OpaqueValues.erase(entry);
}

RValue
SILGenFunction::emitPointerToPointer(SILLocation loc,
                                     ManagedValue input,
                                     CanType inputType,
                                     CanType outputType,
                                     SGFContext C) {
  auto converter = getASTContext().getConvertPointerToPointerArgument(nullptr);

  auto origValue = input;
  if (silConv.useLoweredAddresses()) {
    // The generic function currently always requires indirection, but pointers
    // are always loadable.
    auto origBuf = emitTemporaryAllocation(loc, input.getType());
    B.emitStoreValueOperation(loc, input.forward(*this), origBuf,
                              StoreOwnershipQualifier::Init);
    origValue = emitManagedBufferWithCleanup(origBuf);
  }
  // Invoke the conversion intrinsic to convert to the destination type.
  auto *M = SGM.M.getSwiftModule();
  auto *proto = getPointerProtocol();
  auto firstSubMap = inputType->getContextSubstitutionMap(M, proto);
  auto secondSubMap = outputType->getContextSubstitutionMap(M, proto);

  auto *genericSig = converter->getGenericSignature();
  auto subMap =
    SubstitutionMap::combineSubstitutionMaps(firstSubMap,
                                             secondSubMap,
                                             CombineSubstitutionMaps::AtIndex,
                                             1, 0,
                                             genericSig);
  
  return emitApplyOfLibraryIntrinsic(loc, converter, subMap, origValue, C);
}


namespace {

/// This is an initialization for an address-only existential in memory.
class ExistentialInitialization : public KnownAddressInitialization {
  CleanupHandle Cleanup;
public:
  /// \param existential The existential container
  /// \param address Address of value in existential container
  /// \param concreteFormalType Unlowered AST type of value
  /// \param repr Representation of container
  ExistentialInitialization(SILValue existential, SILValue address,
                            CanType concreteFormalType,
                            ExistentialRepresentation repr,
                            SILGenFunction &SGF)
      : KnownAddressInitialization(address) {
    // Any early exit before we store a value into the existential must
    // clean up the existential container.
    Cleanup = SGF.enterDeinitExistentialCleanup(existential,
                                                concreteFormalType,
                                                repr);
  }

  void finishInitialization(SILGenFunction &SGF) override {
    SingleBufferInitialization::finishInitialization(SGF);
    SGF.Cleanups.setCleanupState(Cleanup, CleanupState::Dead);
  }
};

} // end anonymous namespace

ManagedValue SILGenFunction::emitExistentialErasure(
                            SILLocation loc,
                            CanType concreteFormalType,
                            const TypeLowering &concreteTL,
                            const TypeLowering &existentialTL,
                            ArrayRef<ProtocolConformanceRef> conformances,
                            SGFContext C,
                            llvm::function_ref<ManagedValue (SGFContext)> F,
                            bool allowEmbeddedNSError) {
  // Mark the needed conformances as used.
  for (auto conformance : conformances)
    SGM.useConformance(conformance);

  // If we're erasing to the 'Error' type, we might be able to get an NSError
  // representation more efficiently.
  auto &ctx = getASTContext();
  if (ctx.LangOpts.EnableObjCInterop && conformances.size() == 1 &&
      conformances[0].getRequirement() == ctx.getErrorDecl() &&
      ctx.getNSErrorDecl()) {
    auto nsErrorDecl = ctx.getNSErrorDecl();

    // If the concrete type is NSError or a subclass thereof, just erase it
    // directly.
    auto nsErrorType = nsErrorDecl->getDeclaredType()->getCanonicalType();
    if (nsErrorType->isExactSuperclassOf(concreteFormalType)) {
      ManagedValue nsError =  F(SGFContext());
      if (nsErrorType != concreteFormalType) {
        nsError = B.createUpcast(loc, nsError, getLoweredType(nsErrorType));
      }
      return emitBridgedToNativeError(loc, nsError);
    }

    // If the concrete type is known to conform to _BridgedStoredNSError,
    // call the _nsError witness getter to extract the NSError directly,
    // then just erase the NSError.
    if (auto storedNSErrorConformance =
          SGM.getConformanceToBridgedStoredNSError(loc, concreteFormalType)) {
      auto nsErrorVar = SGM.getNSErrorRequirement(loc);
      if (!nsErrorVar) return emitUndef(loc, existentialTL.getLoweredType());

      SubstitutionList nsErrorVarSubstitutions;

      // Devirtualize.  Maybe this should be done implicitly by
      // emitPropertyLValue?
      if (storedNSErrorConformance->isConcrete()) {
        if (auto normal = dyn_cast<NormalProtocolConformance>(
                                    storedNSErrorConformance->getConcrete())) {
          if (auto witnessVar = normal->getWitness(nsErrorVar, nullptr)) {
            nsErrorVar = cast<VarDecl>(witnessVar.getDecl());
            nsErrorVarSubstitutions = witnessVar.getSubstitutions();
          }
        }
      }

      ManagedValue nativeError = F(SGFContext());

      FormalEvaluationScope writebackScope(*this);
      ManagedValue nsError =
          emitRValueForStorageLoad(
              loc, nativeError, concreteFormalType,
              /*super*/ false, nsErrorVar, RValue(), nsErrorVarSubstitutions,
              AccessSemantics::Ordinary, nsErrorType, SGFContext())
              .getAsSingleValue(*this, loc);

      return emitBridgedToNativeError(loc, nsError);
    }

    // Otherwise, if it's an archetype, try calling the _getEmbeddedNSError()
    // witness to try to dig out the embedded NSError.  But don't do this
    // when we're being called recursively.
    if (isa<ArchetypeType>(concreteFormalType) && allowEmbeddedNSError) {
      auto contBB = createBasicBlock();
      auto isNotPresentBB = createBasicBlock();
      auto isPresentBB = createBasicBlock();

      // Call swift_stdlib_getErrorEmbeddedNSError to attempt to extract an
      // NSError from the value.
      auto getEmbeddedNSErrorFn = SGM.getGetErrorEmbeddedNSError(loc);
      if (!getEmbeddedNSErrorFn)
        return emitUndef(loc, existentialTL.getLoweredType());

      auto getEmbeddedNSErrorSubstitutions =
        SubstitutionMap::getProtocolSubstitutions(ctx.getErrorDecl(),
                                                  concreteFormalType,
                                                  conformances[0]);

      ManagedValue concreteValue = F(SGFContext());
      ManagedValue potentialNSError =
        emitApplyOfLibraryIntrinsic(loc,
                                    getEmbeddedNSErrorFn,
                                    getEmbeddedNSErrorSubstitutions,
                                    { concreteValue.copy(*this, loc) },
                                    SGFContext())
          .getAsSingleValue(*this, loc);

      // We're going to consume 'concreteValue' in exactly one branch,
      // so kill its cleanup now and recreate it on both branches.
      (void) concreteValue.forward(*this);

      // Check whether we got an NSError back.
      std::pair<EnumElementDecl*, SILBasicBlock*> cases[] = {
        { ctx.getOptionalSomeDecl(), isPresentBB },
        { ctx.getOptionalNoneDecl(), isNotPresentBB }
      };
      B.createSwitchEnum(loc, potentialNSError.forward(*this),
                         /*default*/ nullptr, cases);

      // If we did get an NSError, emit the existential erasure from that
      // NSError.
      B.emitBlock(isPresentBB);
      SILValue branchArg;
      {
        // Don't allow cleanups to escape the conditional block.
        FullExpr presentScope(Cleanups, CleanupLocation::get(loc));
        enterDestroyCleanup(concreteValue.getValue());

        // Receive the error value.  It's typed as an 'AnyObject' for
        // layering reasons, so perform an unchecked cast down to NSError.
        SILType anyObjectTy =
          potentialNSError.getType().getAnyOptionalObjectType();
        ManagedValue nsError = B.createOwnedPHIArgument(anyObjectTy);
        nsError = B.createUncheckedRefCast(loc, nsError, 
                                           getLoweredType(nsErrorType));

        branchArg = emitBridgedToNativeError(loc, nsError).forward(*this);
      }
      B.createBranch(loc, contBB, branchArg);

      // If we did not get an NSError, just directly emit the existential.
      // Since this is a recursive call, make sure we don't end up in this
      // path again.
      B.emitBlock(isNotPresentBB);
      {
        FullExpr presentScope(Cleanups, CleanupLocation::get(loc));
        concreteValue = emitManagedRValueWithCleanup(concreteValue.getValue());
        branchArg = emitExistentialErasure(loc, concreteFormalType, concreteTL,
                                           existentialTL, conformances,
                                           SGFContext(),
                                           [&](SGFContext C) {
                                             return concreteValue;
                                           },
                                           /*allowEmbeddedNSError=*/false)
                      .forward(*this);
      }
      B.createBranch(loc, contBB, branchArg);

      // Continue.
      B.emitBlock(contBB);

      SILValue existentialResult = contBB->createPHIArgument(
          existentialTL.getLoweredType(), ValueOwnershipKind::Owned);
      return emitManagedRValueWithCleanup(existentialResult, existentialTL);
    }
  }

  switch (existentialTL.getLoweredType().getObjectType()
            .getPreferredExistentialRepresentation(SGM.M, concreteFormalType)) {
  case ExistentialRepresentation::None:
    llvm_unreachable("not an existential type");
  case ExistentialRepresentation::Metatype: {
    assert(existentialTL.isLoadable());

    SILValue metatype = F(SGFContext()).getUnmanagedValue();
    assert(metatype->getType().castTo<AnyMetatypeType>()->getRepresentation()
             == MetatypeRepresentation::Thick);

    auto upcast =
      B.createInitExistentialMetatype(loc, metatype,
                                      existentialTL.getLoweredType(),
                                      conformances);
    return ManagedValue::forUnmanaged(upcast);
  }
  case ExistentialRepresentation::Class: {
    assert(existentialTL.isLoadable());

    ManagedValue sub = F(SGFContext());
    assert(concreteFormalType->isBridgeableObjectType());
    return B.createInitExistentialRef(loc, existentialTL.getLoweredType(),
                                      concreteFormalType, sub, conformances);
  }
  case ExistentialRepresentation::Boxed: {
    // Allocate the existential.
    auto *existential = B.createAllocExistentialBox(loc,
                                           existentialTL.getLoweredType(),
                                           concreteFormalType,
                                           conformances);
    auto *valueAddr = B.createProjectExistentialBox(loc,
                                           concreteTL.getLoweredType(),
                                           existential);
    // Initialize the concrete value in-place.
    ExistentialInitialization init(existential, valueAddr, concreteFormalType,
                                   ExistentialRepresentation::Boxed, *this);
    ManagedValue mv = F(SGFContext(&init));
    if (!mv.isInContext()) {
      mv.forwardInto(*this, loc, init.getAddress());
      init.finishInitialization(*this);
    }
    
    return emitManagedRValueWithCleanup(existential);
  }
  case ExistentialRepresentation::Opaque: {
  
    // If the concrete value is a pseudogeneric archetype, first erase it to
    // its upper bound.
    auto anyObjectTy = getASTContext().getAnyObjectType();
    auto eraseToAnyObject =
    [&, concreteFormalType, F](SGFContext C) -> ManagedValue {
      auto concreteValue = F(SGFContext());
      assert(concreteFormalType->isBridgeableObjectType());
      return B.createInitExistentialRef(
          loc, SILType::getPrimitiveObjectType(anyObjectTy), concreteFormalType,
          concreteValue, {});
    };
    
    auto concreteTLPtr = &concreteTL;
    if (this->F.getLoweredFunctionType()->isPseudogeneric()) {
      if (anyObjectTy && concreteFormalType->is<ArchetypeType>()) {
        concreteFormalType = anyObjectTy;
        concreteTLPtr = &getTypeLowering(anyObjectTy);
        F = eraseToAnyObject;
      }
    }

    if (!silConv.useLoweredAddresses()) {
      // We should never create new buffers just for init_existential under
      // opaque values mode: This is a case of an opaque value that we can
      // "treat" as a by-value one
      ManagedValue sub = F(SGFContext());
      return B.createInitExistentialValue(
          loc, existentialTL.getLoweredType(), concreteFormalType,
          sub, conformances);
    }

    // Allocate the existential.
    return B.bufferForExpr(
        loc, existentialTL.getLoweredType(), existentialTL, C,
        [&](SILValue existential) {
          // Allocate the concrete value inside the container.
          SILValue valueAddr = B.createInitExistentialAddr(
              loc, existential,
              concreteFormalType,
              concreteTLPtr->getLoweredType(),
              conformances);
          // Initialize the concrete value in-place.
          InitializationPtr init(
              new ExistentialInitialization(existential, valueAddr, concreteFormalType,
                                            ExistentialRepresentation::Opaque,
                                            *this));
          ManagedValue mv = F(SGFContext(init.get()));
          if (!mv.isInContext()) {
            init->copyOrInitValueInto(*this, loc, mv, /*init*/ true);
            init->finishInitialization(*this);
          }
        });
  }
  }

  llvm_unreachable("Unhandled ExistentialRepresentation in switch.");
}

ManagedValue SILGenFunction::emitClassMetatypeToObject(SILLocation loc,
                                                       ManagedValue v,
                                                       SILType resultTy) {
  SILValue value = v.getUnmanagedValue();

  // Convert the metatype to objc representation.
  auto metatypeTy = value->getType().castTo<MetatypeType>();
  auto objcMetatypeTy = CanMetatypeType::get(metatypeTy.getInstanceType(),
                                             MetatypeRepresentation::ObjC);
  value = B.createThickToObjCMetatype(loc, value,
                           SILType::getPrimitiveObjectType(objcMetatypeTy));
  
  // Convert to an object reference.
  value = B.createObjCMetatypeToObject(loc, value, resultTy);
  return emitManagedRValueWithCleanup(value);
}

ManagedValue SILGenFunction::emitExistentialMetatypeToObject(SILLocation loc,
                                                             ManagedValue v,
                                                             SILType resultTy) {
  SILValue value = v.getUnmanagedValue();
  
  // Convert the metatype to objc representation.
  auto metatypeTy = value->getType().castTo<ExistentialMetatypeType>();
  auto objcMetatypeTy = CanExistentialMetatypeType::get(
                                              metatypeTy.getInstanceType(),
                                              MetatypeRepresentation::ObjC);
  value = B.createThickToObjCMetatype(loc, value,
                               SILType::getPrimitiveObjectType(objcMetatypeTy));
  
  // Convert to an object reference.
  value = B.createObjCExistentialMetatypeToObject(loc, value, resultTy);
  
  return emitManagedRValueWithCleanup(value);
}

ManagedValue SILGenFunction::emitProtocolMetatypeToObject(SILLocation loc,
                                                          CanType inputTy,
                                                          SILType resultTy) {
  ProtocolDecl *protocol = inputTy->castTo<MetatypeType>()
    ->getInstanceType()->castTo<ProtocolType>()->getDecl();

  SILValue value = B.createObjCProtocol(loc, protocol, resultTy);
  
  // Protocol objects, despite being global objects, inherit default reference
  // counting semantics from NSObject, so we need to retain the protocol
  // reference when we use it to prevent it being released and attempting to
  // deallocate itself. It doesn't matter if we ever actually clean up that
  // retain though.
  value = B.createCopyValue(loc, value);
  return emitManagedRValueWithCleanup(value);
}

SILGenFunction::OpaqueValueState
SILGenFunction::emitOpenExistential(
       SILLocation loc,
       ManagedValue existentialValue,
       ArchetypeType *openedArchetype,
       SILType loweredOpenedType,
       AccessKind accessKind) {
  // Open the existential value into the opened archetype value.
  bool isUnique = true;
  bool canConsume;
  ManagedValue archetypeMV;
  
  SILType existentialType = existentialValue.getType();
  switch (existentialType.getPreferredExistentialRepresentation(SGM.M)) {
  case ExistentialRepresentation::Opaque: {
    // With CoW existentials we can't consume the boxed value inside of
    // the existential. (We could only do so after a uniqueness check on
    // the box holding the value).
    canConsume = false;
    if (existentialType.isAddress()) {
      OpenedExistentialAccess allowedAccess =
          getOpenedExistentialAccessFor(accessKind);
      if (!loweredOpenedType.isAddress()) {
        assert(!silConv.useLoweredAddresses() &&
               "Non-address loweredOpenedType is only allowed under opaque "
               "value mode");
        loweredOpenedType = loweredOpenedType.getAddressType();
      }
      SILValue archetypeValue =
        B.createOpenExistentialAddr(loc, existentialValue.getValue(),
                                    loweredOpenedType, allowedAccess);
      archetypeMV = ManagedValue::forUnmanaged(archetypeValue);
    } else {
      // borrow the existential and return an unmanaged opened value.
      archetypeMV = getBuilder().createOpenExistentialValue(
          loc, existentialValue, loweredOpenedType);
    }
    break;
  }
  case ExistentialRepresentation::Metatype:
    assert(existentialType.isObject());
    archetypeMV =
        ManagedValue::forUnmanaged(
            B.createOpenExistentialMetatype(
                       loc, existentialValue.forward(*this),
                       loweredOpenedType));
    // Metatypes are always trivial. Consuming would be a no-op.
    canConsume = false;
    break;
  case ExistentialRepresentation::Class: {
    assert(existentialType.isObject());
    SILValue archetypeValue = B.createOpenExistentialRef(
                       loc, existentialValue.forward(*this),
                       loweredOpenedType);
    canConsume = existentialValue.hasCleanup();
    archetypeMV = (canConsume ? emitManagedRValueWithCleanup(archetypeValue)
                              : ManagedValue::forUnmanaged(archetypeValue));
    break;
  }
  case ExistentialRepresentation::Boxed:
    if (existentialType.isAddress()) {
      existentialValue = emitLoad(loc, existentialValue.getValue(),
                                  getTypeLowering(existentialType),
                                  SGFContext::AllowGuaranteedPlusZero,
                                  IsNotTake);
    }

    existentialType = existentialValue.getType();
    assert(existentialType.isObject());
    if (loweredOpenedType.isAddress()) {
      archetypeMV = ManagedValue::forUnmanaged(
        B.createOpenExistentialBox(loc, existentialValue.getValue(),
                                   loweredOpenedType));
    } else {
      assert(!silConv.useLoweredAddresses());
      archetypeMV = getBuilder().createOpenExistentialBoxValue(
        loc, existentialValue, loweredOpenedType);
    }
    // NB: Don't forward the cleanup, because consuming a boxed value won't
    // consume the box reference.
    // The boxed value can't be assumed to be uniquely referenced.
    // We can never consume it.
    // TODO: We could use isUniquelyReferenced to shorten the duration of
    // the box to the point that the opaque value is copied out.
    isUnique = false;
    canConsume = false;
    break;
  case ExistentialRepresentation::None:
    llvm_unreachable("not existential");
  }

  assert(!canConsume || isUnique); (void) isUnique;

  return SILGenFunction::OpaqueValueState{
    archetypeMV,
    /*isConsumable*/ canConsume,
    /*hasBeenConsumed*/ false
  };
}

ManagedValue SILGenFunction::manageOpaqueValue(OpaqueValueState &entry,
                                               SILLocation loc,
                                               SGFContext C) {
  // If the opaque value is consumable, we can just return the
  // value with a cleanup. There is no need to retain it separately.
  if (entry.IsConsumable) {
    assert(!entry.HasBeenConsumed
           && "Uniquely-referenced opaque value already consumed");
    entry.HasBeenConsumed = true;
    return entry.Value;
  }

  assert(!entry.Value.hasCleanup());

  // If the context wants a +0 value, guaranteed or immediate, we can
  // give it to them, because OpenExistential emission guarantees the
  // value.
  if (C.isGuaranteedPlusZeroOk()) {
    return entry.Value;
  }

  // If the context has an initialization a buffer, copy there instead
  // of making a temporary allocation.
  if (auto I = C.getEmitInto()) {
    I->copyOrInitValueInto(*this, loc, entry.Value, /*init*/ false);
    I->finishInitialization(*this);
    return ManagedValue::forInContext();
  }

  // Otherwise, copy the value into a temporary.
  return entry.Value.copyUnmanaged(*this, loc);
}

ManagedValue SILGenFunction::emitConvertedRValue(Expr *E,
                                                 const Conversion &conversion,
                                                 SGFContext C) {
  return emitConvertedRValue(E, conversion, C,
      [&](SILGenFunction &SGF, SILLocation loc, SGFContext C) {
    return emitRValueAsSingleValue(E, C);
  });
}

ManagedValue SILGenFunction::emitConvertedRValue(SILLocation loc,
                                                 const Conversion &conversion,
                                                 SGFContext C,
                                                 ValueProducerRef produceValue){
  // If we're emitting into a converting context, check whether we can
  // peephole the conversions together.
  if (auto outerConversion = C.getAsConversion()) {
    if (outerConversion->tryPeephole(*this, loc, conversion, produceValue)) {
      outerConversion->finishInitialization(*this);
      return ManagedValue::forInContext();
    }
  }

  // Otherwise, set up a reabstracting context and try to emit into that.
  ConvertingInitialization init(conversion, C);
  auto result = produceValue(*this, loc, SGFContext(&init));
  auto finishedResult = init.finishEmission(*this, loc, result);
  return finishedResult;
}

ManagedValue
ConvertingInitialization::finishEmission(SILGenFunction &SGF,
                                         SILLocation loc,
                                         ManagedValue formalResult) {
  switch (getState()) {
  case Uninitialized:
    assert(!formalResult.isInContext());
    State = Extracted;
    return TheConversion.emit(SGF, loc, formalResult, FinalContext);

  case Initialized:
    llvm_unreachable("initialization never finished");

  case Finished:
    assert(formalResult.isInContext());
    assert(!Value.isInContext() || FinalContext.getEmitInto());
    State = Extracted;
    return Value;

  case Extracted:
    llvm_unreachable("value already extracted");
  }
  llvm_unreachable("bad state");
}

bool ConvertingInitialization::tryPeephole(SILGenFunction &SGF,
                                           SILLocation loc,
                                           ManagedValue origValue,
                                           Conversion innerConversion) {
  return tryPeephole(SGF, loc, innerConversion,
      [&](SILGenFunction &SGF, SILLocation loc, SGFContext C) {
    return origValue;
  });
}

bool ConvertingInitialization::tryPeephole(SILGenFunction &SGF,
                                           Expr *E,
                                           Conversion innerConversion) {
  return tryPeephole(SGF, E, innerConversion,
      [&](SILGenFunction &SGF, SILLocation loc, SGFContext C) {
    return SGF.emitRValueAsSingleValue(E, C);
  });
}

bool ConvertingInitialization::tryPeephole(SILGenFunction &SGF, SILLocation loc,
                                           Conversion innerConversion,
                                           ValueProducerRef produceValue) {
  const auto &outerConversion = getConversion();
  auto hint = canPeepholeConversions(SGF, outerConversion, innerConversion);
  if (!hint)
    return false;

  ManagedValue value = emitPeepholedConversions(SGF, loc, outerConversion,
                                                innerConversion, *hint,
                                                FinalContext, produceValue);
  setConvertedValue(value);
  return true;
}

void ConvertingInitialization::copyOrInitValueInto(SILGenFunction &SGF,
                                                   SILLocation loc,
                                                   ManagedValue formalValue,
                                                   bool isInit) {
  assert(getState() == Uninitialized && "already have saved value?");

  // TODO: take advantage of borrowed inputs?
  if (!isInit) formalValue = formalValue.copy(SGF, loc);
  State = Initialized;
  Value = TheConversion.emit(SGF, loc, formalValue, FinalContext);
}

ManagedValue
ConvertingInitialization::emitWithAdjustedConversion(SILGenFunction &SGF,
                                          SILLocation loc,
                                          Conversion adjustedConversion,
                                          ValueProducerRef produceValue) {
  ConvertingInitialization init(adjustedConversion, getFinalContext());
  auto result = produceValue(SGF, loc, SGFContext(&init));
  result = init.finishEmission(SGF, loc, result);
  setConvertedValue(result);
  finishInitialization(SGF);
  return ManagedValue::forInContext();
}

ManagedValue Conversion::emit(SILGenFunction &SGF, SILLocation loc,
                              ManagedValue value, SGFContext C) const {
  switch (getKind()) {
  case AnyErasure:
    return SGF.emitTransformedValue(loc, value, getBridgingSourceType(),
                                    getBridgingResultType(), C);

  case BridgeToObjC:
    return SGF.emitNativeToBridgedValue(loc, value,
                                        getBridgingSourceType(),
                                        getBridgingResultType(),
                                        getBridgingLoweredResultType(), C);

  case ForceAndBridgeToObjC: {
    auto &tl = SGF.getTypeLowering(value.getType());
    auto sourceValueType = getBridgingSourceType().getAnyOptionalObjectType();
    value = SGF.emitCheckedGetOptionalValueFrom(loc, value, tl, SGFContext());
    return SGF.emitNativeToBridgedValue(loc, value, sourceValueType,
                                        getBridgingResultType(),
                                        getBridgingLoweredResultType(), C);
  }

  case BridgeFromObjC:
    return SGF.emitBridgedToNativeValue(loc, value,
                                        getBridgingSourceType(),
                                        getBridgingResultType(),
                                        getBridgingLoweredResultType(), C);

  case BridgeResultFromObjC:
    return SGF.emitBridgedToNativeValue(loc, value,
                                        getBridgingSourceType(),
                                        getBridgingResultType(),
                                        getBridgingLoweredResultType(), C,
                                        /*isResult*/ true);

  case SubstToOrig:
    return SGF.emitSubstToOrigValue(loc, value,
                                    getReabstractionOrigType(),
                                    getReabstractionSubstType(), C);

  case OrigToSubst:
    return SGF.emitOrigToSubstValue(loc, value,
                                    getReabstractionOrigType(),
                                    getReabstractionSubstType(), C);
  }
  llvm_unreachable("bad kind");
}

Optional<Conversion>
Conversion::adjustForInitialOptionalConversions(CanType newSourceType) const {
  switch (getKind()) {
  case SubstToOrig:
  case OrigToSubst:
    // TODO: handle reabstraction conversions here, too.
    return None;

  case ForceAndBridgeToObjC:
    return None;

  case AnyErasure:
  case BridgeToObjC:
  case BridgeFromObjC:
  case BridgeResultFromObjC:
    return Conversion::getBridging(getKind(), newSourceType,
                                   getBridgingResultType(),
                                   getBridgingLoweredResultType(),
                                   isBridgingExplicit());
  }
  llvm_unreachable("bad kind");
}

Optional<Conversion> Conversion::adjustForInitialForceValue() const {
  switch (getKind()) {
  case SubstToOrig:
  case OrigToSubst:
  case AnyErasure:
  case BridgeFromObjC:
  case BridgeResultFromObjC:
  case ForceAndBridgeToObjC:
    return None;

  case BridgeToObjC: {
    auto sourceOptType =
      ImplicitlyUnwrappedOptionalType::get(getBridgingSourceType())
        ->getCanonicalType();
    return Conversion::getBridging(ForceAndBridgeToObjC,
                                   sourceOptType,
                                   getBridgingResultType(),
                                   getBridgingLoweredResultType(),
                                   isBridgingExplicit());
  }
  }
  llvm_unreachable("bad kind");
}

void Conversion::dump() const {
  print(llvm::errs());
  llvm::errs() << '\n';
}

static void printReabstraction(const Conversion &conversion,
                               llvm::raw_ostream &out, StringRef name) {
  out << name << "(orig: ";
  conversion.getReabstractionOrigType().print(out);
  out << ", subst: ";
  conversion.getReabstractionSubstType().print(out);
  out << ')';
}

static void printBridging(const Conversion &conversion, llvm::raw_ostream &out,
                          StringRef name) {
  out << name << "(from: ";
  conversion.getBridgingSourceType().print(out);
  out << ", to: ";
  conversion.getBridgingResultType().print(out);
  out << ", explicit: " << conversion.isBridgingExplicit() << ')';
}

void Conversion::print(llvm::raw_ostream &out) const {
  switch (getKind()) {
  case SubstToOrig:
    return printReabstraction(*this, out, "SubstToOrig");
  case OrigToSubst:
    return printReabstraction(*this, out, "OrigToSubst");
  case AnyErasure:
    return printBridging(*this, out, "AnyErasure");
  case BridgeToObjC:
    return printBridging(*this, out, "BridgeToObjC");
  case ForceAndBridgeToObjC:
    return printBridging(*this, out, "ForceAndBridgeToObjC");
  case BridgeFromObjC:
    return printBridging(*this, out, "BridgeFromObjC");
  case BridgeResultFromObjC:
    return printBridging(*this, out, "BridgeResultFromObjC");
  }
  llvm_unreachable("bad kind");
}

static bool areRelatedTypesForBridgingPeephole(CanType sourceType,
                                               CanType resultType) {
  if (sourceType == resultType)
    return true;

  if (auto resultObjType = resultType.getAnyOptionalObjectType()) {
    // Optional-to-optional.
    if (auto sourceObjType = sourceType.getAnyOptionalObjectType()) {
      return areRelatedTypesForBridgingPeephole(sourceObjType, resultObjType);
    }

    // Optional injection.
    return areRelatedTypesForBridgingPeephole(sourceType, resultObjType);
  }

  // If the result type is AnyObject, then we can always apply the bridge
  // via Any.
  if (resultType->isAnyObject()) {
    // ... as long as the source type is not an Optional.
    if (sourceType->isBridgeableObjectType())
      return true;
  }

  // TODO: maybe other class existentials? Existential conversions?
  // They probably aren't important here.

  // All the other rules only apply to class types.
  if (!sourceType->mayHaveSuperclass() ||
      !resultType->mayHaveSuperclass())
    return false;

  // Walk up the class hierarchy looking for an exact match.
  while (auto superclass = sourceType->getSuperclass()) {
    sourceType = superclass->getCanonicalType();
    if (sourceType == resultType)
      return true;
  }

  // Otherwise, we don't know how to do this conversion.
  return false;
}

/// Does the given conversion turn a non-class type into Any, taking into
/// account optional-to-optional conversions?
static bool isValueToAnyConversion(CanType from, CanType to) {
  while (auto toObj = to.getAnyOptionalObjectType()) {
    to = toObj;
    if (auto fromObj = from.getAnyOptionalObjectType()) {
      from = fromObj;
    }
  }

  assert(to->isAny());

  // Types that we can easily transform into AnyObject:
  //   - classes and class-bounded archetypes
  //   - class existentials, even if not pure-@objc
  //   - @convention(objc) metatypes
  //   - @convention(block) functions
  return !from->isAnyClassReferenceType() &&
         !from->isBridgeableObjectType();
}

/// Check whether this conversion is Any??? to AnyObject???.  If the result
/// type is less optional, it doesn't count.
static bool isMatchedAnyToAnyObjectConversion(CanType from, CanType to) {
  while (auto fromObject = from.getAnyOptionalObjectType()) {
    auto toObject = to.getAnyOptionalObjectType();
    if (!toObject) return false;
    from = fromObject;
    to = toObject;
  }

  if (from->isAny()) {
    assert(to->lookThroughAllAnyOptionalTypes()->isAnyObject());
    return true;
  }
  return false;
}

Optional<ConversionPeepholeHint>
Lowering::canPeepholeConversions(SILGenFunction &SGF,
                                 const Conversion &outerConversion,
                                 const Conversion &innerConversion) {
  switch (outerConversion.getKind()) {
  case Conversion::OrigToSubst:
  case Conversion::SubstToOrig:
    // TODO: peephole these when the abstraction patterns are the same!
    return None;

  case Conversion::AnyErasure:
  case Conversion::BridgeFromObjC:
  case Conversion::BridgeResultFromObjC:
    // TODO: maybe peephole bridging through a Swift type?
    // This isn't actually something that happens in normal code generation.
    return None;

  case Conversion::ForceAndBridgeToObjC:
  case Conversion::BridgeToObjC:
    switch (innerConversion.getKind()) {
    case Conversion::AnyErasure:
    case Conversion::BridgeFromObjC:
    case Conversion::BridgeResultFromObjC: {
      bool outerExplicit = outerConversion.isBridgingExplicit();
      bool innerExplicit = innerConversion.isBridgingExplicit();

      // Never peephole if both conversions are explicit; there might be
      // something the user's trying to do which we don't understand.
      if (outerExplicit && innerExplicit)
        return None;

      // Otherwise, we can peephole if we understand the resulting conversion
      // and applying the peephole doesn't change semantics.

      CanType sourceType = innerConversion.getBridgingSourceType();
      CanType intermediateType = innerConversion.getBridgingResultType();
      assert(intermediateType == outerConversion.getBridgingSourceType());

      // If we're doing a peephole involving a force, we want to propagate
      // the force to the source value.  If it's not in fact optional, that
      // won't work.
      bool forced =
        outerConversion.getKind() == Conversion::ForceAndBridgeToObjC;
      if (forced) {
        sourceType = sourceType.getAnyOptionalObjectType();
        if (!sourceType) return None;
        intermediateType = intermediateType.getAnyOptionalObjectType();
        assert(intermediateType);
      }

      CanType resultType = outerConversion.getBridgingResultType();
      SILType loweredSourceTy = SGF.getLoweredType(sourceType);
      SILType loweredResultTy = outerConversion.getBridgingLoweredResultType();

      auto applyPeephole = [&](ConversionPeepholeHint::Kind kind) {
        return ConversionPeepholeHint(kind, forced);
      };

      // Converting to Any doesn't do anything semantically special, so we
      // can apply the peephole unconditionally.
      if (isMatchedAnyToAnyObjectConversion(intermediateType, resultType)) {
        if (loweredSourceTy == loweredResultTy) {
          return applyPeephole(ConversionPeepholeHint::Identity);
        } else if (isValueToAnyConversion(sourceType, intermediateType)) {
          return applyPeephole(ConversionPeepholeHint::BridgeToAnyObject);
        } else {
          return applyPeephole(ConversionPeepholeHint::Subtype);
        }
      }

      // Otherwise, undoing a bridging conversions can change semantics by
      // e.g. removing a copy, so we shouldn't do it unless the special
      // syntactic bridging peephole applies.  That requires one of the
      // conversions to be explicit.
      // TODO: use special SILGen to preserve semantics in this case,
      // e.g. by making a copy.
      if (!outerExplicit && !innerExplicit) {
        return None;
      }

      // Okay, now we're in the domain of the bridging peephole: an
      // explicit bridging conversion can cancel out an implicit bridge
      // between related types.

      // If the source and destination types have exactly the same
      // representation, then (1) they're related and (2) we can directly
      // emit into the context.
      if (loweredSourceTy.getObjectType() == loweredResultTy.getObjectType()) {
        return applyPeephole(ConversionPeepholeHint::Identity);
      }

      // Look for a subtype relationship between the source and destination.
      if (areRelatedTypesForBridgingPeephole(sourceType, resultType)) {
        return applyPeephole(ConversionPeepholeHint::Subtype);
      }

      // If the inner conversion is a result conversion that removes
      // optionality, and the non-optional source type is a subtype of the
      // value type, this is just an implicit force.
      if (!forced &&
          innerConversion.getKind() == Conversion::BridgeResultFromObjC) {
        if (auto sourceValueType = sourceType.getAnyOptionalObjectType()) {
          if (!intermediateType.getAnyOptionalObjectType() &&
              areRelatedTypesForBridgingPeephole(sourceValueType, resultType)) {
            forced = true;
            return applyPeephole(ConversionPeepholeHint::Subtype);
          }
        }
      }

      return None;
    }

    default:
      return None;
    }
  }
  llvm_unreachable("bad kind");
}

ManagedValue
Lowering::emitPeepholedConversions(SILGenFunction &SGF, SILLocation loc,
                                   const Conversion &outerConversion,
                                   const Conversion &innerConversion,
                                   ConversionPeepholeHint hint,
                                   SGFContext C,
                                   ValueProducerRef produceOrigValue) {
  auto produceValue = [&](SGFContext C) {
    if (!hint.isForced()) {
      return produceOrigValue(SGF, loc, C);
    }

    auto value = produceOrigValue(SGF, loc, SGFContext());
    auto &optTL = SGF.getTypeLowering(value.getType());
    return SGF.emitCheckedGetOptionalValueFrom(loc, value, optTL, C);
  };

  auto getBridgingSourceType = [&] {
    CanType sourceType = innerConversion.getBridgingSourceType();
    if (hint.isForced())
      sourceType = sourceType.getAnyOptionalObjectType();
    return sourceType;
  };
  auto getBridgingResultType = [&] {
    return outerConversion.getBridgingResultType();
  };
  auto getBridgingLoweredResultType = [&] {
    return outerConversion.getBridgingLoweredResultType();
  };

  switch (hint.getKind()) {
  case ConversionPeepholeHint::Identity:
    return produceValue(C);

  case ConversionPeepholeHint::BridgeToAnyObject: {
    auto value = produceValue(SGFContext());
    return SGF.emitNativeToBridgedValue(loc, value, getBridgingSourceType(),
                                        getBridgingResultType(),
                                        getBridgingLoweredResultType(), C);
  }

  case ConversionPeepholeHint::Subtype: {
    // Otherwise, emit and convert.
    // TODO: if the context allows +0, use it in more situations.
    auto value = produceValue(SGFContext());
    SILType loweredResultTy = getBridgingLoweredResultType();

    // Nothing to do if the value already has the right representation.
    if (value.getType().getObjectType() == loweredResultTy.getObjectType())
      return value;

    CanType sourceType = getBridgingSourceType();
    CanType resultType = getBridgingResultType();
    return SGF.emitTransformedValue(loc, value, sourceType, resultType, C);
  }
  }
  llvm_unreachable("bad kind");
}
