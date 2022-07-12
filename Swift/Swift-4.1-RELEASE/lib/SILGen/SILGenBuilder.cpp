//===--- SILGenBuilder.cpp ------------------------------------------------===//
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

#include "SILGenBuilder.h"
#include "ArgumentSource.h"
#include "RValue.h"
#include "SILGenFunction.h"
#include "Scope.h"
#include "SwitchCaseFullExpr.h"
#include "swift/AST/GenericSignature.h"
#include "swift/AST/SubstitutionMap.h"

using namespace swift;
using namespace Lowering;

//===----------------------------------------------------------------------===//
//                              Utility Methods
//===----------------------------------------------------------------------===//

SILGenModule &SILGenBuilder::getSILGenModule() const { return SGF.SGM; }

//===----------------------------------------------------------------------===//
//                                Constructors
//===----------------------------------------------------------------------===//

SILGenBuilder::SILGenBuilder(SILGenFunction &SGF)
    : SILBuilder(SGF.F), SGF(SGF) {}

SILGenBuilder::SILGenBuilder(SILGenFunction &SGF, SILBasicBlock *insertBB)
    : SILBuilder(insertBB), SGF(SGF) {}

SILGenBuilder::SILGenBuilder(SILGenFunction &SGF, SILBasicBlock *insertBB,
                             SmallVectorImpl<SILInstruction *> *insertedInsts)
    : SILBuilder(insertBB, insertedInsts), SGF(SGF) {}

SILGenBuilder::SILGenBuilder(SILGenFunction &SGF, SILBasicBlock *insertBB,
                             SILBasicBlock::iterator insertInst)
    : SILBuilder(insertBB, insertInst), SGF(SGF) {}

//===----------------------------------------------------------------------===//
//                            Instruction Emission
//===----------------------------------------------------------------------===//

MetatypeInst *SILGenBuilder::createMetatype(SILLocation loc, SILType metatype) {
  auto theMetatype = metatype.castTo<MetatypeType>();
  // Getting a nontrivial metatype requires forcing any conformances necessary
  // to instantiate the type.
  switch (theMetatype->getRepresentation()) {
  case MetatypeRepresentation::Thin:
    break;
  case MetatypeRepresentation::Thick:
  case MetatypeRepresentation::ObjC: {
    // Walk the type recursively to look for substitutions we may need.
    theMetatype.getInstanceType().findIf([&](Type t) -> bool {
      auto *decl = t->getAnyNominal();
      if (!decl)
        return false;

      auto *genericSig = decl->getGenericSignature();
      if (!genericSig)
        return false;

      auto subMap = t->getContextSubstitutionMap(getSILGenModule().SwiftModule,
                                                 decl);
      SmallVector<Substitution, 4> subs;
      genericSig->getSubstitutions(subMap, subs);
      getSILGenModule().useConformancesFromSubstitutions(subs);
      return false;
    });

    break;
  }
  }

  return SILBuilder::createMetatype(loc, metatype);
}

ApplyInst *SILGenBuilder::createApply(SILLocation loc, SILValue fn,
                                      SILType substFnTy, SILType result,
                                      SubstitutionList subs,
                                      ArrayRef<SILValue> args) {
  getSILGenModule().useConformancesFromSubstitutions(subs);
  return SILBuilder::createApply(loc, fn, subs, args, false);
}

TryApplyInst *
SILGenBuilder::createTryApply(SILLocation loc, SILValue fn, SILType substFnTy,
                              SubstitutionList subs, ArrayRef<SILValue> args,
                              SILBasicBlock *normalBB, SILBasicBlock *errorBB) {
  getSILGenModule().useConformancesFromSubstitutions(subs);
  return SILBuilder::createTryApply(loc, fn, subs, args, normalBB, errorBB);
}

PartialApplyInst *
SILGenBuilder::createPartialApply(SILLocation loc, SILValue fn,
                                  SILType substFnTy, SubstitutionList subs,
                                  ArrayRef<SILValue> args, SILType closureTy) {
  getSILGenModule().useConformancesFromSubstitutions(subs);
  return SILBuilder::createPartialApply(
      loc, fn, subs, args,
      closureTy.getAs<SILFunctionType>()->getCalleeConvention());
}

BuiltinInst *SILGenBuilder::createBuiltin(SILLocation loc, Identifier name,
                                          SILType resultTy,
                                          SubstitutionList subs,
                                          ArrayRef<SILValue> args) {
  getSILGenModule().useConformancesFromSubstitutions(subs);
  return SILBuilder::createBuiltin(loc, name, resultTy, subs, args);
}

InitExistentialAddrInst *SILGenBuilder::createInitExistentialAddr(
    SILLocation loc, SILValue existential, CanType formalConcreteType,
    SILType loweredConcreteType,
    ArrayRef<ProtocolConformanceRef> conformances) {
  for (auto conformance : conformances)
    getSILGenModule().useConformance(conformance);

  return SILBuilder::createInitExistentialAddr(
      loc, existential, formalConcreteType, loweredConcreteType, conformances);
}

InitExistentialValueInst *SILGenBuilder::createInitExistentialValue(
    SILLocation Loc, SILType ExistentialType, CanType FormalConcreteType,
    SILValue Concrete, ArrayRef<ProtocolConformanceRef> Conformances) {
  for (auto conformance : Conformances)
    getSILGenModule().useConformance(conformance);

  return SILBuilder::createInitExistentialValue(
      Loc, ExistentialType, FormalConcreteType, Concrete, Conformances);
}

ManagedValue SILGenBuilder::createInitExistentialValue(
    SILLocation loc, SILType existentialType, CanType formalConcreteType,
    ManagedValue concrete, ArrayRef<ProtocolConformanceRef> conformances) {
  // *NOTE* we purposely do not use a cleanup cloner here. The reason why is no
  // matter whether we have a trivial or non-trivial input,
  // init_existential_value returns a +1 value (the COW box).
  SILValue v = createInitExistentialValue(
      loc, existentialType, formalConcreteType, concrete.forward(SGF), conformances);
  return SGF.emitManagedRValueWithCleanup(v);
}

InitExistentialMetatypeInst *SILGenBuilder::createInitExistentialMetatype(
    SILLocation loc, SILValue metatype, SILType existentialType,
    ArrayRef<ProtocolConformanceRef> conformances) {
  for (auto conformance : conformances)
    getSILGenModule().useConformance(conformance);

  return SILBuilder::createInitExistentialMetatype(
      loc, metatype, existentialType, conformances);
}

InitExistentialRefInst *SILGenBuilder::createInitExistentialRef(
    SILLocation loc, SILType existentialType, CanType formalConcreteType,
    SILValue concreteValue, ArrayRef<ProtocolConformanceRef> conformances) {
  for (auto conformance : conformances)
    getSILGenModule().useConformance(conformance);

  return SILBuilder::createInitExistentialRef(
      loc, existentialType, formalConcreteType, concreteValue, conformances);
}

ManagedValue SILGenBuilder::createInitExistentialRef(
    SILLocation Loc, SILType ExistentialType, CanType FormalConcreteType,
    ManagedValue Concrete, ArrayRef<ProtocolConformanceRef> Conformances) {
  CleanupCloner Cloner(*this, Concrete);
  InitExistentialRefInst *IERI =
      createInitExistentialRef(Loc, ExistentialType, FormalConcreteType,
                               Concrete.forward(SGF), Conformances);
  return Cloner.clone(IERI);
}

AllocExistentialBoxInst *SILGenBuilder::createAllocExistentialBox(
    SILLocation loc, SILType existentialType, CanType concreteType,
    ArrayRef<ProtocolConformanceRef> conformances) {
  for (auto conformance : conformances)
    getSILGenModule().useConformance(conformance);

  return SILBuilder::createAllocExistentialBox(loc, existentialType,
                                               concreteType, conformances);
}

ManagedValue SILGenBuilder::createStructExtract(SILLocation loc,
                                                ManagedValue base,
                                                VarDecl *decl) {
  ManagedValue borrowedBase = base.borrow(SGF, loc);
  SILValue extract =
      SILBuilder::createStructExtract(loc, borrowedBase.getValue(), decl);
  return ManagedValue::forUnmanaged(extract);
}

ManagedValue SILGenBuilder::createRefElementAddr(SILLocation loc,
                                                 ManagedValue operand,
                                                 VarDecl *field,
                                                 SILType resultTy) {
  operand = operand.borrow(SGF, loc);
  SILValue result = createRefElementAddr(loc, operand.getValue(), field);
  return ManagedValue::forUnmanaged(result);
}

ManagedValue SILGenBuilder::createCopyValue(SILLocation loc,
                                            ManagedValue originalValue) {
  auto &lowering = SGF.getTypeLowering(originalValue.getType());
  return createCopyValue(loc, originalValue, lowering);
}

ManagedValue SILGenBuilder::createCopyValue(SILLocation loc,
                                            ManagedValue originalValue,
                                            const TypeLowering &lowering) {
  if (lowering.isTrivial())
    return originalValue;

  SILType ty = originalValue.getType();
  assert(!ty.isAddress() && "Can not perform a copy value of an address typed "
         "value");

  if (ty.isObject() &&
      originalValue.getOwnershipKind() == ValueOwnershipKind::Trivial) {
    return originalValue;
  }

  SILValue result =
      lowering.emitCopyValue(*this, loc, originalValue.getValue());
  return SGF.emitManagedRValueWithCleanup(result, lowering);
}

ManagedValue SILGenBuilder::createCopyUnownedValue(SILLocation loc,
                                                   ManagedValue originalValue) {
  auto unownedType = originalValue.getType().castTo<UnownedStorageType>();
  assert(unownedType->isLoadable(ResilienceExpansion::Maximal));
  (void)unownedType;

  SILValue result =
      SILBuilder::createCopyUnownedValue(loc, originalValue.getValue());
  return SGF.emitManagedRValueWithCleanup(result);
}

ManagedValue
SILGenBuilder::createUnsafeCopyUnownedValue(SILLocation loc,
                                            ManagedValue originalValue) {
  // *NOTE* The reason why this is unsafe is that we are converting and
  // unconditionally retaining, rather than before converting from
  // unmanaged->ref checking that our value is not yet uninitialized.
  auto unmanagedType = originalValue.getType().getAs<UnmanagedStorageType>();
  SILValue result = SILBuilder::createUnmanagedToRef(
      loc, originalValue.getValue(),
      SILType::getPrimitiveObjectType(unmanagedType.getReferentType()));
  result = SILBuilder::createCopyValue(loc, result);
  return SGF.emitManagedRValueWithCleanup(result);
}

ManagedValue SILGenBuilder::createOwnedPHIArgument(SILType type) {
  SILPHIArgument *arg =
      getInsertionBB()->createPHIArgument(type, ValueOwnershipKind::Owned);
  return SGF.emitManagedRValueWithCleanup(arg);
}

ManagedValue SILGenBuilder::createGuaranteedPHIArgument(SILType type) {
  SILPHIArgument *arg =
      getInsertionBB()->createPHIArgument(type, ValueOwnershipKind::Guaranteed);
  return SGF.emitManagedBorrowedArgumentWithCleanup(arg);
}

ManagedValue SILGenBuilder::createAllocRef(
    SILLocation loc, SILType refType, bool objc, bool canAllocOnStack,
    ArrayRef<SILType> inputElementTypes,
    ArrayRef<ManagedValue> inputElementCountOperands) {
  llvm::SmallVector<SILType, 8> elementTypes(inputElementTypes.begin(),
                                             inputElementTypes.end());
  llvm::SmallVector<SILValue, 8> elementCountOperands;
  std::transform(std::begin(inputElementCountOperands),
                 std::end(inputElementCountOperands),
                 std::back_inserter(elementCountOperands),
                 [](ManagedValue mv) -> SILValue { return mv.getValue(); });

  AllocRefInst *i = SILBuilder::createAllocRef(
      loc, refType, objc, canAllocOnStack, elementTypes, elementCountOperands);
  return SGF.emitManagedRValueWithCleanup(i);
}

ManagedValue SILGenBuilder::createAllocRefDynamic(
    SILLocation loc, ManagedValue operand, SILType refType, bool objc,
    ArrayRef<SILType> inputElementTypes,
    ArrayRef<ManagedValue> inputElementCountOperands) {
  llvm::SmallVector<SILType, 8> elementTypes(inputElementTypes.begin(),
                                             inputElementTypes.end());
  llvm::SmallVector<SILValue, 8> elementCountOperands;
  std::transform(std::begin(inputElementCountOperands),
                 std::end(inputElementCountOperands),
                 std::back_inserter(elementCountOperands),
                 [](ManagedValue mv) -> SILValue { return mv.getValue(); });

  AllocRefDynamicInst *i =
      SILBuilder::createAllocRefDynamic(loc, operand.getValue(), refType, objc,
                                        elementTypes, elementCountOperands);
  return SGF.emitManagedRValueWithCleanup(i);
}

ManagedValue SILGenBuilder::createTupleExtract(SILLocation loc,
                                               ManagedValue base,
                                               unsigned index, SILType type) {
  ManagedValue borrowedBase = SGF.emitManagedBeginBorrow(loc, base.getValue());
  SILValue extract =
      SILBuilder::createTupleExtract(loc, borrowedBase.getValue(), index, type);
  return ManagedValue::forUnmanaged(extract);
}

ManagedValue SILGenBuilder::createTupleExtract(SILLocation loc,
                                               ManagedValue value,
                                               unsigned index) {
  SILType type = value.getType().getTupleElementType(index);
  return createTupleExtract(loc, value, index, type);
}

ManagedValue SILGenBuilder::createLoadBorrow(SILLocation loc,
                                             ManagedValue base) {
  if (SGF.getTypeLowering(base.getType()).isTrivial()) {
    auto *i = SILBuilder::createLoad(loc, base.getValue(),
                                     LoadOwnershipQualifier::Trivial);
    return ManagedValue::forUnmanaged(i);
  }

  auto *i = SILBuilder::createLoadBorrow(loc, base.getValue());
  return SGF.emitManagedBorrowedRValueWithCleanup(base.getValue(), i);
}

ManagedValue SILGenBuilder::createFormalAccessLoadBorrow(SILLocation loc,
                                                         ManagedValue base) {
  if (SGF.getTypeLowering(base.getType()).isTrivial()) {
    auto *i = SILBuilder::createLoad(loc, base.getValue(),
                                     LoadOwnershipQualifier::Trivial);
    return ManagedValue::forUnmanaged(i);
  }

  SILValue baseValue = base.getValue();
  auto *i = SILBuilder::createLoadBorrow(loc, baseValue);
  return SGF.emitFormalEvaluationManagedBorrowedRValueWithCleanup(loc,
                                                                  baseValue, i);
}

ManagedValue
SILGenBuilder::createFormalAccessCopyValue(SILLocation loc,
                                           ManagedValue originalValue) {
  SILType ty = originalValue.getType();
  const auto &lowering = SGF.getTypeLowering(ty);
  if (lowering.isTrivial())
    return originalValue;

  assert(!lowering.isAddressOnly() && "cannot perform a copy value of an "
                                      "address only type");

  if (ty.isObject() &&
      originalValue.getOwnershipKind() == ValueOwnershipKind::Trivial) {
    return originalValue;
  }

  SILValue result =
      lowering.emitCopyValue(*this, loc, originalValue.getValue());
  return SGF.emitFormalAccessManagedRValueWithCleanup(loc, result);
}

ManagedValue SILGenBuilder::createFormalAccessCopyAddr(
    SILLocation loc, ManagedValue originalAddr, SILValue newAddr,
    IsTake_t isTake, IsInitialization_t isInit) {
  SILBuilder::createCopyAddr(loc, originalAddr.getValue(), newAddr, isTake,
                             isInit);
  return SGF.emitFormalAccessManagedBufferWithCleanup(loc, newAddr);
}

ManagedValue SILGenBuilder::bufferForExpr(
    SILLocation loc, SILType ty, const TypeLowering &lowering,
    SGFContext context, std::function<void (SILValue)> rvalueEmitter) {
  // If we have a single-buffer "emit into" initialization, use that for the
  // result.
  SILValue address = context.getAddressForInPlaceInitialization(SGF, loc);

  // If we couldn't emit into the Initialization, emit into a temporary
  // allocation.
  if (!address) {
    address = SGF.emitTemporaryAllocation(loc, ty.getObjectType());
  }

  rvalueEmitter(address);

  // If we have a single-buffer "emit into" initialization, use that for the
  // result.
  if (context.finishInPlaceInitialization(SGF)) {
    return ManagedValue::forInContext();
  }

  // Add a cleanup for the temporary we allocated.
  if (lowering.isTrivial())
    return ManagedValue::forUnmanaged(address);

  return SGF.emitManagedBufferWithCleanup(address);
}


ManagedValue SILGenBuilder::formalAccessBufferForExpr(
    SILLocation loc, SILType ty, const TypeLowering &lowering,
    SGFContext context, std::function<void(SILValue)> rvalueEmitter) {
  // If we have a single-buffer "emit into" initialization, use that for the
  // result.
  SILValue address = context.getAddressForInPlaceInitialization(SGF, loc);

  // If we couldn't emit into the Initialization, emit into a temporary
  // allocation.
  if (!address) {
    address = SGF.emitTemporaryAllocation(loc, ty.getObjectType());
  }

  rvalueEmitter(address);

  // If we have a single-buffer "emit into" initialization, use that for the
  // result.
  if (context.finishInPlaceInitialization(SGF)) {
    return ManagedValue::forInContext();
  }

  // Add a cleanup for the temporary we allocated.
  if (lowering.isTrivial())
    return ManagedValue::forUnmanaged(address);

  return SGF.emitFormalAccessManagedBufferWithCleanup(loc, address);
}

ManagedValue SILGenBuilder::createUncheckedEnumData(SILLocation loc,
                                                    ManagedValue operand,
                                                    EnumElementDecl *element) {
  CleanupCloner cloner(*this, operand);
  SILValue result = createUncheckedEnumData(loc, operand.forward(SGF), element);
  return cloner.clone(result);
}

ManagedValue SILGenBuilder::createUncheckedTakeEnumDataAddr(
    SILLocation loc, ManagedValue operand, EnumElementDecl *element,
    SILType ty) {
  CleanupCloner cloner(*this, operand);
  SILValue result =
      createUncheckedTakeEnumDataAddr(loc, operand.forward(SGF), element);
  return cloner.clone(result);
}

ManagedValue SILGenBuilder::createLoadTake(SILLocation loc, ManagedValue v) {
  auto &lowering = SGF.getTypeLowering(v.getType());
  return createLoadTake(loc, v, lowering);
}

ManagedValue SILGenBuilder::createLoadTake(SILLocation loc, ManagedValue v,
                                           const TypeLowering &lowering) {
  assert(lowering.getLoweredType().getAddressType() == v.getType());
  SILValue result =
      lowering.emitLoadOfCopy(*this, loc, v.forward(SGF), IsTake);
  if (lowering.isTrivial())
    return ManagedValue::forUnmanaged(result);
  assert(!lowering.isAddressOnly() && "cannot retain an unloadable type");
  return SGF.emitManagedRValueWithCleanup(result, lowering);
}

ManagedValue SILGenBuilder::createLoadCopy(SILLocation loc, ManagedValue v) {
  auto &lowering = SGF.getTypeLowering(v.getType());
  return createLoadCopy(loc, v, lowering);
}

ManagedValue SILGenBuilder::createLoadCopy(SILLocation loc, ManagedValue v,
                                           const TypeLowering &lowering) {
  assert(lowering.getLoweredType().getAddressType() == v.getType());
  SILValue result =
      lowering.emitLoadOfCopy(*this, loc, v.getValue(), IsNotTake);
  if (lowering.isTrivial())
    return ManagedValue::forUnmanaged(result);
  assert((!lowering.isAddressOnly()
          || !SGF.silConv.useLoweredAddresses()) &&
         "cannot retain an unloadable type");
  return SGF.emitManagedRValueWithCleanup(result, lowering);
}

ManagedValue SILGenBuilder::createFunctionArgument(SILType type,
                                                   ValueDecl *decl) {
  SILFunction &F = getFunction();

  SILFunctionArgument *arg = F.begin()->createFunctionArgument(type, decl);
  if (arg->getType().isObject()) {
    if (arg->getOwnershipKind().isTrivialOr(ValueOwnershipKind::Owned))
      return SGF.emitManagedRValueWithCleanup(arg);
    return ManagedValue::forBorrowedRValue(arg);
  }

  return SGF.emitManagedBufferWithCleanup(arg);
}

ManagedValue
SILGenBuilder::createMarkUninitialized(ValueDecl *decl, ManagedValue operand,
                                       MarkUninitializedInst::Kind muKind) {
  // We either have an owned or trivial value.
  SILValue value =
      SILBuilder::createMarkUninitialized(decl, operand.forward(SGF), muKind);
  assert(value->getType().isObject() && "Expected only objects here");

  // If we have a trivial value, just return without a cleanup.
  if (operand.getOwnershipKind() != ValueOwnershipKind::Owned) {
    return ManagedValue::forUnmanaged(value);
  }

  // Otherwise, recreate the cleanup.
  return SGF.emitManagedRValueWithCleanup(value);
}

ManagedValue SILGenBuilder::createEnum(SILLocation loc, ManagedValue payload,
                                       EnumElementDecl *decl, SILType type) {
  SILValue result =
      SILBuilder::createEnum(loc, payload.forward(SGF), decl, type);
  if (result.getOwnershipKind() != ValueOwnershipKind::Owned)
    return ManagedValue::forUnmanaged(result);
  return SGF.emitManagedRValueWithCleanup(result);
}

ManagedValue SILGenBuilder::createUnconditionalCheckedCastValue(
    SILLocation loc, ManagedValue operand, SILType type) {
  SILValue result = SILBuilder::createUnconditionalCheckedCastValue(
      loc, operand.forward(SGF), type);
  return SGF.emitManagedRValueWithCleanup(result);
}

ManagedValue SILGenBuilder::createUnconditionalCheckedCast(SILLocation loc,
                                                           ManagedValue operand,
                                                           SILType type) {
  SILValue result = SILBuilder::createUnconditionalCheckedCast(
      loc, operand.forward(SGF), type);
  return SGF.emitManagedRValueWithCleanup(result);
}

void SILGenBuilder::createCheckedCastBranch(SILLocation loc, bool isExact,
                                            ManagedValue operand, SILType type,
                                            SILBasicBlock *trueBlock,
                                            SILBasicBlock *falseBlock,
                                            ProfileCounter Target1Count,
                                            ProfileCounter Target2Count) {
  SILBuilder::createCheckedCastBranch(loc, isExact, operand.forward(SGF), type,
                                      trueBlock, falseBlock, Target1Count,
                                      Target2Count);
}

void SILGenBuilder::createCheckedCastValueBranch(SILLocation loc,
                                                 ManagedValue operand,
                                                 SILType type,
                                                 SILBasicBlock *trueBlock,
                                                 SILBasicBlock *falseBlock) {
  SILBuilder::createCheckedCastValueBranch(loc, operand.forward(SGF), type,
                                           trueBlock, falseBlock);
}

ManagedValue SILGenBuilder::createUpcast(SILLocation loc, ManagedValue original,
                                         SILType type) {
  CleanupCloner cloner(*this, original);
  SILValue convertedValue =
      SILBuilder::createUpcast(loc, original.forward(SGF), type);
  return cloner.clone(convertedValue);
}

ManagedValue SILGenBuilder::createOptionalSome(SILLocation loc,
                                               ManagedValue arg) {
  CleanupCloner cloner(*this, arg);
  auto &argTL = SGF.getTypeLowering(arg.getType());
  SILType optionalType = arg.getType().wrapAnyOptionalType(getFunction());
  if (argTL.isLoadable() || !SGF.silConv.useLoweredAddresses()) {
    SILValue someValue =
        SILBuilder::createOptionalSome(loc, arg.forward(SGF), optionalType);
    return cloner.clone(someValue);
  }

  SILValue tempResult = SGF.emitTemporaryAllocation(loc, optionalType);
  RValue rvalue(SGF, loc, arg.getType().getSwiftRValueType(), arg);
  ArgumentSource argValue(loc, std::move(rvalue));
  SGF.emitInjectOptionalValueInto(
      loc, std::move(argValue), tempResult,
      SGF.getTypeLowering(tempResult->getType()));
  return ManagedValue::forUnmanaged(tempResult);
}

ManagedValue SILGenBuilder::createManagedOptionalNone(SILLocation loc,
                                                      SILType type) {
  if (!type.isAddressOnly(getModule()) || !SGF.silConv.useLoweredAddresses()) {
    SILValue noneValue = SILBuilder::createOptionalNone(loc, type);
    return ManagedValue::forUnmanaged(noneValue);
  }

  SILValue tempResult = SGF.emitTemporaryAllocation(loc, type);
  SGF.emitInjectOptionalNothingInto(loc, tempResult,
                                    SGF.getTypeLowering(type));
  return ManagedValue::forUnmanaged(tempResult);
}

ManagedValue SILGenBuilder::createTupleElementAddr(SILLocation Loc,
                                                   ManagedValue Base,
                                                   unsigned Index,
                                                   SILType Type) {
  SILValue TupleEltAddr =
      SILBuilder::createTupleElementAddr(Loc, Base.getValue(), Index, Type);
  return ManagedValue::forUnmanaged(TupleEltAddr);
}

ManagedValue SILGenBuilder::createTupleElementAddr(SILLocation Loc,
                                                   ManagedValue Value,
                                                   unsigned Index) {
  SILType Type = Value.getType().getTupleElementType(Index);
  return createTupleElementAddr(Loc, Value, Index, Type);
}

ManagedValue SILGenBuilder::createUncheckedRefCast(SILLocation loc,
                                                   ManagedValue value,
                                                   SILType type) {
  CleanupCloner cloner(*this, value);
  SILValue cast =
      SILBuilder::createUncheckedRefCast(loc, value.forward(SGF), type);
  return cloner.clone(cast);
}

ManagedValue SILGenBuilder::createUncheckedBitCast(SILLocation loc,
                                                   ManagedValue value,
                                                   SILType type) {
  CleanupCloner cloner(*this, value);
  SILValue cast =
      SILBuilder::createUncheckedBitCast(loc, value.forward(SGF), type);
  return cloner.clone(cast);
}

ManagedValue SILGenBuilder::createOpenExistentialRef(SILLocation loc,
                                                     ManagedValue original,
                                                     SILType type) {
  CleanupCloner cloner(*this, original);
  SILValue openedExistential =
      SILBuilder::createOpenExistentialRef(loc, original.forward(SGF), type);
  return cloner.clone(openedExistential);
}

ManagedValue SILGenBuilder::createOpenExistentialValue(SILLocation loc,
                                                       ManagedValue original,
                                                       SILType type) {
  ManagedValue borrowedExistential = original.borrow(SGF, loc);
  SILValue openedExistential = SILBuilder::createOpenExistentialValue(
      loc, borrowedExistential.getValue(), type);
  return ManagedValue::forUnmanaged(openedExistential);
}

ManagedValue SILGenBuilder::createOpenExistentialBoxValue(SILLocation loc,
                                                          ManagedValue original,
                                                          SILType type) {
  ManagedValue borrowedExistential = original.borrow(SGF, loc);
  SILValue openedExistential = SILBuilder::createOpenExistentialBoxValue(
      loc, borrowedExistential.getValue(), type);
  return ManagedValue::forUnmanaged(openedExistential);
}

ManagedValue SILGenBuilder::createStore(SILLocation loc, ManagedValue value,
                                        SILValue address,
                                        StoreOwnershipQualifier qualifier) {
  SILModule &M = SGF.F.getModule();
  CleanupCloner cloner(*this, value);
  if (value.getType().isTrivial(M) || value.getOwnershipKind() == ValueOwnershipKind::Trivial)
    qualifier = StoreOwnershipQualifier::Trivial;
  SILBuilder::createStore(loc, value.forward(SGF), address, qualifier);
  return cloner.clone(address);
}

ManagedValue SILGenBuilder::createSuperMethod(SILLocation loc,
                                              ManagedValue operand,
                                              SILDeclRef member,
                                              SILType methodTy) {
  SILValue v = SILBuilder::createSuperMethod(loc, operand.getValue(), member,
                                             methodTy);
  return ManagedValue::forUnmanaged(v);
}

ManagedValue SILGenBuilder::createObjCSuperMethod(SILLocation loc,
                                                  ManagedValue operand,
                                                  SILDeclRef member,
                                                  SILType methodTy) {
  SILValue v = SILBuilder::createObjCSuperMethod(loc, operand.getValue(), member,
                                                 methodTy);
  return ManagedValue::forUnmanaged(v);
}

ManagedValue SILGenBuilder::
createValueMetatype(SILLocation loc, SILType metatype,
                    ManagedValue base) {
  SILValue v = createValueMetatype(loc, metatype, base.getValue());
  return ManagedValue::forUnmanaged(v);
}

void SILGenBuilder::createStoreBorrow(SILLocation loc, ManagedValue value,
                                      SILValue address) {
  assert(value.getOwnershipKind() == ValueOwnershipKind::Guaranteed);
  createStoreBorrow(loc, value.getValue(), address);
}

void SILGenBuilder::createStoreBorrowOrTrivial(SILLocation loc,
                                               ManagedValue value,
                                               SILValue address) {
  if (value.getOwnershipKind() == ValueOwnershipKind::Trivial) {
    createStore(loc, value, address, StoreOwnershipQualifier::Trivial);
    return;
  }

  createStoreBorrow(loc, value, address);
}

ManagedValue SILGenBuilder::createBridgeObjectToRef(SILLocation loc,
                                                    ManagedValue mv,
                                                    SILType destType) {
  CleanupCloner cloner(*this, mv);
  SILValue result = createBridgeObjectToRef(loc, mv.forward(SGF), destType);
  return cloner.clone(result);
}

ManagedValue SILGenBuilder::createBlockToAnyObject(SILLocation loc,
                                                   ManagedValue v,
                                                   SILType destType) {
  assert(SGF.getASTContext().LangOpts.EnableObjCInterop);
  assert(destType.isAnyObject());
  assert(v.getType().is<SILFunctionType>());
  assert(v.getType().castTo<SILFunctionType>()->getRepresentation() ==
           SILFunctionTypeRepresentation::Block);

  // For now, we don't have a better instruction than this.
  return createUncheckedRefCast(loc, v, destType);
}

BranchInst *SILGenBuilder::createBranch(SILLocation loc,
                                        SILBasicBlock *targetBlock,
                                        ArrayRef<ManagedValue> args) {
  llvm::SmallVector<SILValue, 8> newArgs;
  transform(args, std::back_inserter(newArgs),
            [&](ManagedValue mv) -> SILValue { return mv.forward(SGF); });
  return createBranch(loc, targetBlock, newArgs);
}

ReturnInst *SILGenBuilder::createReturn(SILLocation loc,
                                        ManagedValue returnValue) {
  return createReturn(loc, returnValue.forward(SGF));
}

ManagedValue SILGenBuilder::createTuple(SILLocation loc, SILType type,
                                        ArrayRef<ManagedValue> elements) {
  // Handle the empty tuple case.
  if (elements.empty()) {
    SILValue result = createTuple(loc, type, ArrayRef<SILValue>());
    return ManagedValue::forUnmanaged(result);
  }

  // We need to look for the first non-trivial value and use that as our cleanup
  // cloner value.
  auto iter = find_if(elements, [&](ManagedValue mv) -> bool {
    return mv.getType().isTrivial(getModule());
  });

  llvm::SmallVector<SILValue, 8> forwardedValues;
  // If we have all trivial values, then just create the tuple and return. No
  // cleanups need to be cloned.
  if (iter == elements.end()) {
    transform(elements, std::back_inserter(forwardedValues),
              [&](ManagedValue mv) -> SILValue {
                return mv.forward(getSILGenFunction());
              });
    SILValue result = createTuple(loc, type, forwardedValues);
    return ManagedValue::forUnmanaged(result);
  }

  // Otherwise, we use that values cloner. This is taking advantage of
  // instructions that forward ownership requiring that all input values have
  // the same ownership if they are non-trivial.
  CleanupCloner cloner(*this, *iter);
  transform(elements, std::back_inserter(forwardedValues),
            [&](ManagedValue mv) -> SILValue {
              return mv.forward(getSILGenFunction());
            });
  return cloner.clone(createTuple(loc, type, forwardedValues));
}

//===----------------------------------------------------------------------===//
//                            Switch Enum Builder
//===----------------------------------------------------------------------===//

void SwitchEnumBuilder::emit() && {
  bool isAddressOnly = optional.getType().isAddressOnly(builder.getModule()) &&
                       getSGF().silConv.useLoweredAddresses();
  using DeclBlockPair = std::pair<EnumElementDecl *, SILBasicBlock *>;
  {
    // TODO: We could store the data in CaseBB form and not have to do this.
    llvm::SmallVector<DeclBlockPair, 8> caseBlocks;
    llvm::SmallVector<ProfileCounter, 8> caseBlockCounts;
    std::transform(caseDataArray.begin(), caseDataArray.end(),
                   std::back_inserter(caseBlocks),
                   [](NormalCaseData &caseData) -> DeclBlockPair {
                     return {caseData.decl, caseData.block};
                   });
    std::transform(caseDataArray.begin(), caseDataArray.end(),
                   std::back_inserter(caseBlockCounts),
                   [](NormalCaseData &caseData) -> ProfileCounter {
                     return caseData.count;
                   });
    SILBasicBlock *defaultBlock =
        defaultBlockData ? defaultBlockData->block : nullptr;
    ProfileCounter defaultBlockCount =
        defaultBlockData ? defaultBlockData->count : ProfileCounter();
    ArrayRef<ProfileCounter> caseBlockCountsRef = caseBlockCounts;
    if (isAddressOnly) {
      builder.createSwitchEnumAddr(loc, optional.getValue(), defaultBlock,
                                   caseBlocks, caseBlockCountsRef,
                                   defaultBlockCount);
    } else {
      if (optional.getType().isAddress()) {
        // TODO: Refactor this into a maybe load.
        if (optional.hasCleanup()) {
          optional = builder.createLoadTake(loc, optional);
        } else {
          optional = builder.createLoadCopy(loc, optional);
        }
      }
      builder.createSwitchEnum(loc, optional.forward(getSGF()), defaultBlock,
                               caseBlocks, caseBlockCountsRef,
                               defaultBlockCount);
    }
  }

  // If we are asked to create a default block and it is specified that the
  // default block should be emitted before normal cases, emit it now.
  if (defaultBlockData &&
      defaultBlockData->dispatchTime ==
          DefaultDispatchTime::BeforeNormalCases) {
    SILBasicBlock *defaultBlock = defaultBlockData->block;
    NullablePtr<SILBasicBlock> contBB = defaultBlockData->contBlock;
    DefaultCaseHandler handler = defaultBlockData->handler;

    // Don't allow cleanups to escape the conditional block.
    SwitchCaseFullExpr presentScope(builder.getSILGenFunction(),
                                    CleanupLocation::get(loc),
                                    contBB.getPtrOrNull());
    builder.emitBlock(defaultBlock);
    ManagedValue input = optional;
    if (!isAddressOnly) {
      input = builder.createOwnedPHIArgument(optional.getType());
    }
    handler(input, presentScope);
    assert(!builder.hasValidInsertionPoint());
  }

  for (NormalCaseData &caseData : caseDataArray) {
    EnumElementDecl *decl = caseData.decl;
    SILBasicBlock *caseBlock = caseData.block;
    NullablePtr<SILBasicBlock> contBlock = caseData.contBlock;
    NormalCaseHandler handler = caseData.handler;

    // Don't allow cleanups to escape the conditional block.
    SwitchCaseFullExpr presentScope(builder.getSILGenFunction(),
                                    CleanupLocation::get(loc),
                                    contBlock.getPtrOrNull());

    builder.emitBlock(caseBlock);

    ManagedValue input;
    if (decl->hasAssociatedValues()) {
      // Pull the payload out if we have one.
      SILType inputType =
          optional.getType().getEnumElementType(decl, builder.getModule());
      input = optional;
      if (!isAddressOnly) {
        input = builder.createOwnedPHIArgument(inputType);
      }
    }
    handler(input, presentScope);
    assert(!builder.hasValidInsertionPoint());
  }

  // If we are asked to create a default block and it is specified that the
  // default block should be emitted after normal cases, emit it now.
  if (defaultBlockData &&
      defaultBlockData->dispatchTime == DefaultDispatchTime::AfterNormalCases) {
    SILBasicBlock *defaultBlock = defaultBlockData->block;
    NullablePtr<SILBasicBlock> contBB = defaultBlockData->contBlock;
    DefaultCaseHandler handler = defaultBlockData->handler;

    // Don't allow cleanups to escape the conditional block.
    SwitchCaseFullExpr presentScope(builder.getSILGenFunction(),
                                    CleanupLocation::get(loc),
                                    contBB.getPtrOrNull());
    builder.emitBlock(defaultBlock);
    ManagedValue input = optional;
    if (!isAddressOnly) {
      input = builder.createOwnedPHIArgument(optional.getType());
    }
    handler(input, presentScope);
    assert(!builder.hasValidInsertionPoint());
  }
}
