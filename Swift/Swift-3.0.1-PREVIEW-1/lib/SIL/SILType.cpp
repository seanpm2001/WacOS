//===--- SILType.cpp - Defines SILType ------------------------------------===//
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

#include "swift/SIL/SILType.h"
#include "swift/AST/Type.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/TypeLowering.h"
#include "swift/SIL/AbstractionPattern.h"

using namespace swift;
using namespace swift::Lowering;

SILType SILType::getExceptionType(const ASTContext &C) {
  return SILType::getPrimitiveObjectType(C.getExceptionType());
}

SILType SILType::getNativeObjectType(const ASTContext &C) {
  return SILType(C.TheNativeObjectType, SILValueCategory::Object);
}

SILType SILType::getBridgeObjectType(const ASTContext &C) {
  return SILType(C.TheBridgeObjectType, SILValueCategory::Object);
}

SILType SILType::getUnknownObjectType(const ASTContext &C) {
  return getPrimitiveObjectType(C.TheUnknownObjectType);
}

SILType SILType::getRawPointerType(const ASTContext &C) {
  return getPrimitiveObjectType(C.TheRawPointerType);
}

SILType SILType::getBuiltinIntegerType(unsigned bitWidth,
                                       const ASTContext &C) {
  return getPrimitiveObjectType(CanType(BuiltinIntegerType::get(bitWidth, C)));
}

SILType SILType::getBuiltinFloatType(BuiltinFloatType::FPKind Kind,
                                     const ASTContext &C) {
  CanType ty;
  switch (Kind) {
  case BuiltinFloatType::IEEE16:  ty = C.TheIEEE16Type; break;
  case BuiltinFloatType::IEEE32:  ty = C.TheIEEE32Type; break;
  case BuiltinFloatType::IEEE64:  ty = C.TheIEEE64Type; break;
  case BuiltinFloatType::IEEE80:  ty = C.TheIEEE80Type; break;
  case BuiltinFloatType::IEEE128: ty = C.TheIEEE128Type; break;
  case BuiltinFloatType::PPC128:  ty = C.ThePPC128Type; break;
  }
  return getPrimitiveObjectType(ty);
}

SILType SILType::getBuiltinWordType(const ASTContext &C) {
  return getPrimitiveObjectType(CanType(BuiltinIntegerType::getWordType(C)));
}

bool SILType::isTrivial(SILModule &M) const {
  return M.getTypeLowering(*this).isTrivial();
}

bool SILType::isReferenceCounted(SILModule &M) const {
  return M.getTypeLowering(*this).isReferenceCounted();
}

bool SILType::isNoReturnFunction() const {
  if (auto funcTy = dyn_cast<SILFunctionType>(getSwiftRValueType()))
    return funcTy->getSILResult().getSwiftRValueType()->isUninhabited();

  return false;
}

std::string SILType::getAsString() const {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  print(OS);
  return OS.str();
}

bool SILType::isPointerSizeAndAligned() {
  auto &C = getASTContext();
  if (isHeapObjectReferenceType()
      || getSwiftRValueType()->isEqual(C.TheRawPointerType)) {
    return true;
  }
  if (auto intTy = dyn_cast<BuiltinIntegerType>(getSwiftRValueType()))
    return intTy->getWidth().isPointerWidth();

  return false;
}

// Allow casting a struct by value when all elements in toType correspond to
// an element of the same size or larger laid out in the same order in
// fromType. The assumption is that if fromType has larger elements, or
// additional elements, their presence cannot induce a more compact layout of
// the overlapping elements.
//
// struct {A, B} -> A is castable
// struct {A, B, C} -> struct {A, B} is castable
// struct { struct {A, B}, C} -> struct {A, B} is castable
// struct { A, B, C} -> struct { struct {A, B}, C} is NOT castable
static bool canUnsafeCastStruct(SILType fromType, StructDecl *fromStruct,
                                SILType toType, SILModule &M) {
  auto fromRange = fromStruct->getStoredProperties();
  if (fromRange.begin() == fromRange.end())
    return false;

  // Can the first element of fromStruct be cast by value into toType?
  SILType fromEltTy = fromType.getFieldType(*fromRange.begin(), M);
  if (SILType::canUnsafeCastValue(fromEltTy, toType, M))
    return true;
  
  // Otherwise, flatten one level of struct elements on each side.
  StructDecl *toStruct = toType.getStructOrBoundGenericStruct();
  if (!toStruct)
    return false;

  auto toRange = toStruct->getStoredProperties();
  for (auto toI = toRange.begin(), toE = toRange.end(),
         fromI = fromRange.begin(), fromE = fromRange.end();
       toI != toE; ++toI, ++fromI) {

    if (fromI == fromE)
      return false; // fromType is a struct with fewer elements.
      
    SILType fromEltTy = fromType.getFieldType(*fromI, M);
    SILType toEltTy = toType.getFieldType(*toI, M);
    if (!SILType::canUnsafeCastValue(fromEltTy, toEltTy, M))
      return false;
  }
  // fromType's overlapping elements are compatible.
  return true;
}

// Allow casting a tuple by value when all elements in toType correspond to an
// element of the same size or larger in fromType in the same order.
static bool canUnsafeCastTuple(SILType fromType, CanTupleType fromTupleTy,
                               SILType toType, SILModule &M) {
  unsigned numFromElts = fromTupleTy->getNumElements();
  // Can the first element of fromTupleTy be cast by value into toType?
  if (numFromElts != 0 && SILType::canUnsafeCastValue(
        fromType.getTupleElementType(0), toType, M)) {
    return true;
  }
  // Otherwise, flatten one level of tuple elements on each side.
  CanTupleType toTupleTy = dyn_cast<TupleType>(toType.getSwiftRValueType());
  if (!toTupleTy)
    return false;

  unsigned numToElts = toTupleTy->getNumElements();
  if (numFromElts < numToElts)
    return false;

  for (unsigned i = 0; i != numToElts; ++i) {
    if (!SILType::canUnsafeCastValue(fromType.getTupleElementType(i),
                                      toType.getTupleElementType(i), M)) {
      return false;
    }
  }
  return true;
}

// Allow casting an enum by value when toType is an enum and each elements is
// individually castable to toType. An enum cannot be smaller than its payload.
static bool canUnsafeCastEnum(SILType fromType, EnumDecl *fromEnum,
                              SILType toType, SILModule &M) {
  unsigned numToElements = 0;
  SILType toElementTy;
  if (EnumDecl *toEnum = toType.getEnumOrBoundGenericEnum()) {
    for (auto toElement : toEnum->getAllElements()) {
      ++numToElements;
      if (!toElement->hasArgumentType())
        continue;
      // Bail on multiple payloads.
      if (!toElementTy.isNull())
        return false;
      toElementTy = toType.getEnumElementType(toElement, M);
    }
  } else {
    // If toType is not an enum, handle it like a singleton
    numToElements = 1;
    toElementTy = toType;
  }
  // If toType has more elements, it may be larger.
  auto fromElements = fromEnum->getAllElements();
  if (numToElements > std::distance(fromElements.begin(), fromElements.end()))
    return false;

  if (toElementTy.isNull())
    return true;

  // If any of the fromElements can be cast by value to the singleton toElement,
  // then the overall enum can be cast by value.
  for (auto fromElement : fromElements) {
    if (!fromElement->hasArgumentType())
      continue;

    auto fromElementTy = fromType.getEnumElementType(fromElement, M);
    if (SILType::canUnsafeCastValue(fromElementTy, toElementTy, M))
      return true;
  }
  return false;
}

static bool canUnsafeCastScalars(SILType fromType, SILType toType,
                                 SILModule &M) {
  CanType fromCanTy = fromType.getSwiftRValueType();
  bool isToPointer = toType.isPointerSizeAndAligned();

  unsigned LeastFromWidth = 0;
  // Like UnsafeRefBitCast, allow class existentials to be truncated to
  // single-pointer references. Unlike UnsafeRefBitCast, this also supports raw
  // pointers and words.
  if (fromType.isPointerSizeAndAligned()
      || fromCanTy.isAnyClassReferenceType()) {

    // Allow casting from a value that contains an aligned pointer into another
    // pointer value regardless of the fixed width.
    if (isToPointer)
      return true;

    LeastFromWidth = BuiltinIntegerWidth::pointer().getLeastWidth();

  } else if (auto fromIntTy = dyn_cast<BuiltinIntegerType>(fromCanTy)) {
    if (fromIntTy->isFixedWidth())
      LeastFromWidth = fromIntTy->getFixedWidth();
  }

  unsigned GreatestToWidth = UINT_MAX;
  if (isToPointer) {
    GreatestToWidth = BuiltinIntegerWidth::pointer().getGreatestWidth();

  } else if (auto toIntTy = dyn_cast<BuiltinIntegerType>(
               toType.getSwiftRValueType())) {
    if (toIntTy->isFixedWidth())
      GreatestToWidth = toIntTy->getFixedWidth();
  }
  return LeastFromWidth >= GreatestToWidth;
}

bool SILType::canUnsafeCastValue(SILType fromType, SILType toType,
                                 SILModule &M) {
  if (fromType == toType)
    return true;

  // Unwrap single element structs.
  if (StructDecl *toStruct = toType.getStructOrBoundGenericStruct()) {
    auto toRange = toStruct->getStoredProperties();
    if (toRange.begin() != toRange.end()
        && std::next(toRange.begin()) == toRange.end()) {
      toType = toType.getFieldType(*toRange.begin(), M);
    }
  }
  if (canUnsafeCastScalars(fromType, toType, M))
    return true;

  if (StructDecl *fromStruct = fromType.getStructOrBoundGenericStruct())
    return canUnsafeCastStruct(fromType, fromStruct, toType, M);

  if (CanTupleType fromTupleTy =
      dyn_cast<TupleType>(fromType.getSwiftRValueType())) {
    return canUnsafeCastTuple(fromType, fromTupleTy, toType, M);
  }
  if (EnumDecl *fromEnum = fromType.getEnumOrBoundGenericEnum())
    return canUnsafeCastEnum(fromType, fromEnum, toType, M);
  
  return false;
}

// Reference cast from representations with single pointer low bits.
// Only reference cast to simple single pointer representations.
//
// TODO: handle casting to a loadable existential by generating
// init_existential_ref. Until then, only promote to a heap object dest.
bool SILType::canRefCast(SILType operTy, SILType resultTy, SILModule &M) {
  OptionalTypeKind otk;
  auto fromTy = unwrapAnyOptionalType(operTy, M, otk);
  auto toTy = unwrapAnyOptionalType(resultTy, M, otk);
  return (fromTy.isHeapObjectReferenceType() || fromTy.isClassExistentialType())
    && toTy.isHeapObjectReferenceType();
}

SILType SILType::getFieldType(VarDecl *field, SILModule &M) const {
  assert(field->getDeclContext() == getNominalOrBoundGenericNominal());
  AbstractionPattern origFieldTy = M.Types.getAbstractionPattern(field);
  CanType substFieldTy;
  if (field->hasClangNode()) {
    substFieldTy = origFieldTy.getType();
  } else {
    substFieldTy =
      getSwiftRValueType()->getTypeOfMember(M.getSwiftModule(),
                                            field, nullptr)->getCanonicalType();
  }
  auto loweredTy = M.Types.getLoweredType(origFieldTy, substFieldTy);
  if (isAddress() || getClassOrBoundGenericClass() != nullptr) {
    return loweredTy.getAddressType();
  } else {
    return loweredTy.getObjectType();
  }
}

SILType SILType::getEnumElementType(EnumElementDecl *elt, SILModule &M) const {
  assert(elt->getDeclContext() == getEnumOrBoundGenericEnum());
  assert(elt->hasArgumentType());
  auto substEltTy =
    getSwiftRValueType()->getTypeOfMember(M.getSwiftModule(),
                                          elt, nullptr,
                                          elt->getArgumentInterfaceType());
  auto loweredTy =
    M.Types.getLoweredType(M.Types.getAbstractionPattern(elt), substEltTy);

  // If the case is indirect, then the payload is boxed.
  if (elt->isIndirect() || elt->getParentEnum()->isIndirect())
    loweredTy = SILType::getPrimitiveObjectType(
      SILBoxType::get(loweredTy.getSwiftRValueType()));

  return SILType(loweredTy.getSwiftRValueType(), getCategory());
}

/// True if the type, or the referenced type of an address type, is
/// address-only. For example, it could be a resilient struct or something of
/// unknown size.
bool SILType::isAddressOnly(SILModule &M) const {
  return M.getTypeLowering(*this).isAddressOnly();
}

SILType SILType::substGenericArgs(SILModule &M,
                                  ArrayRef<Substitution> Subs) const {
  SILFunctionType *fnTy = getSwiftRValueType()->castTo<SILFunctionType>();
  if (Subs.empty()) {
    assert(!fnTy->isPolymorphic() && "function type without subs must not "
           "be polymorphic.");
    return *this;
  }
  assert(fnTy->isPolymorphic() && "Can only subst interface generic args on "
         "polymorphic function types.");
  CanSILFunctionType canFnTy =
    fnTy->substGenericArgs(M, M.getSwiftModule(), Subs);
  return SILType::getPrimitiveObjectType(canFnTy);
}

ArrayRef<Substitution> SILType::gatherAllSubstitutions(SILModule &M) {
  return getSwiftRValueType()->gatherAllSubstitutions(M.getSwiftModule(),
                                                      nullptr);
}

bool SILType::isHeapObjectReferenceType() const {
  auto &C = getASTContext();
  if (getSwiftRValueType()->isBridgeableObjectType())
    return true;
  if (getSwiftRValueType()->isEqual(C.TheNativeObjectType))
    return true;
  if (getSwiftRValueType()->isEqual(C.TheBridgeObjectType))
    return true;
  if (getSwiftRValueType()->isEqual(C.TheUnknownObjectType))
    return true;
  if (is<SILBoxType>())
    return true;
  return false;
}

SILType SILType::getMetatypeInstanceType(SILModule &M) const {
  CanType MetatypeType = getSwiftRValueType();
  assert(MetatypeType->is<AnyMetatypeType>() &&
         "This method should only be called on SILTypes with an underlying "
         "metatype type.");
  assert(isObject() && "Should only be called on object types.");
  Type instanceType =
    MetatypeType->castTo<AnyMetatypeType>()->getInstanceType();

  return M.Types.getLoweredType(instanceType->getCanonicalType());
}

bool SILType::aggregateContainsRecord(SILType Record, SILModule &Mod) const {
  assert(!hasArchetype() && "Agg should be proven to not be generic "
                             "before passed to this function.");
  assert(!Record.hasArchetype() && "Record should be proven to not be generic "
                                    "before passed to this function.");

  llvm::SmallVector<SILType, 8> Worklist;
  Worklist.push_back(*this);

  // For each "subrecord" of agg in the worklist...
  while (!Worklist.empty()) {
    SILType Ty = Worklist.pop_back_val();

    // If it is record, we succeeded. Return true.
    if (Ty == Record)
      return true;

    // Otherwise, we gather up sub-records that need to be checked for
    // checking... First handle the tuple case.
    if (CanTupleType TT = Ty.getAs<TupleType>()) {
      for (unsigned i = 0, e = TT->getNumElements(); i != e; ++i)
        Worklist.push_back(Ty.getTupleElementType(i));
      continue;
    }

    // Then if we have an enum...
    if (EnumDecl *E = Ty.getEnumOrBoundGenericEnum()) {
      for (auto Elt : E->getAllElements())
        if (Elt->hasArgumentType())
          Worklist.push_back(Ty.getEnumElementType(Elt, Mod));
      continue;
    }

    // Then if we have a struct address...
    if (StructDecl *S = Ty.getStructOrBoundGenericStruct())
      for (VarDecl *Var : S->getStoredProperties())
        Worklist.push_back(Ty.getFieldType(Var, Mod));

    // If we have a class address, it is a pointer so it cannot contain other
    // types.

    // If we reached this point, then this type has no subrecords. Since it does
    // not equal our record, we can skip it.
  }

  // Could not find the record in the aggregate.
  return false;
}

bool SILType::aggregateHasUnreferenceableStorage() const {
  if (auto s = getStructOrBoundGenericStruct()) {
    return s->hasUnreferenceableStorage();
  }
  return false;
}

OptionalTypeKind SILType::getOptionalTypeKind() const {
  OptionalTypeKind result;
  getSwiftRValueType()->getAnyOptionalObjectType(result);
  return result;
}

SILType SILType::getAnyOptionalObjectType(SILModule &M,
                                          OptionalTypeKind &OTK) const {
  if (auto objectTy = getSwiftRValueType()->getAnyOptionalObjectType(OTK)) {
    auto loweredTy
      = M.Types.getLoweredType(AbstractionPattern::getOpaque(), objectTy);
    
    return SILType(loweredTy.getSwiftRValueType(), getCategory());
  }

  OTK = OTK_None;
  return SILType();
}

/// True if the given type value is nonnull, and the represented type is NSError
/// or CFError, the error classes for which we support "toll-free" bridging to
/// Error existentials.
static bool isBridgedErrorClass(SILModule &M,
                                Type t) {
  if (!t)
    return false;

  if (auto archetypeType = t->getAs<ArchetypeType>())
    t = archetypeType->getSuperclass();

  // NSError (TODO: and CFError) can be bridged.
  auto nsErrorType = M.Types.getNSErrorType();
  if (t && nsErrorType && nsErrorType->isExactSuperclassOf(t, nullptr)) {
    return true;
  }
  
  return false;
}

static bool isErrorExistential(ArrayRef<ProtocolDecl*> protocols) {
  return protocols.size() == 1
    && protocols[0]->isSpecificProtocol(KnownProtocolKind::Error);
}

ExistentialRepresentation
SILType::getPreferredExistentialRepresentation(SILModule &M,
                                               Type containedType) const {
  SmallVector<ProtocolDecl *, 4> protocols;
  
  // Existential metatypes always use metatype representation.
  if (is<ExistentialMetatypeType>())
    return ExistentialRepresentation::Metatype;
  
  // Get the list of existential constraints. If the type isn't existential,
  // then there is no representation.
  if (!getSwiftRValueType()->isAnyExistentialType(protocols))
    return ExistentialRepresentation::None;
  
  // The (uncomposed) Error existential uses a special boxed representation.
  if (isErrorExistential(protocols)) {
    // NSError or CFError references can be adopted directly as Error
    // existentials.
    if (isBridgedErrorClass(M, containedType)) {
      return ExistentialRepresentation::Class;
    } else {
      return ExistentialRepresentation::Boxed;
    }
  }
  
  // A class-constrained protocol composition can adopt the conforming
  // class reference directly.
  for (auto proto : protocols) {
    if (proto->requiresClass())
      return ExistentialRepresentation::Class;
  }
  
  // Otherwise, we need to use a fixed-sized buffer.
  return ExistentialRepresentation::Opaque;
}

bool
SILType::canUseExistentialRepresentation(SILModule &M,
                                         ExistentialRepresentation repr,
                                         Type containedType) const {
  switch (repr) {
  case ExistentialRepresentation::None:
    return !isAnyExistentialType();
  case ExistentialRepresentation::Opaque:
  case ExistentialRepresentation::Class:
  case ExistentialRepresentation::Boxed: {
    // Look at the protocols to see what representation is appropriate.
    SmallVector<ProtocolDecl *, 4> protocols;
    if (!getSwiftRValueType()->isAnyExistentialType(protocols))
      return false;
    // The (uncomposed) Error existential uses a special boxed
    // representation. It can also adopt class references of bridged error types
    // directly.
    if (isErrorExistential(protocols))
      return repr == ExistentialRepresentation::Boxed
        || (repr == ExistentialRepresentation::Class
            && isBridgedErrorClass(M, containedType));
    
    // A class-constrained composition uses ClassReference representation;
    // otherwise, we use a fixed-sized buffer
    for (auto *proto : protocols) {
      if (proto->requiresClass())
        return repr == ExistentialRepresentation::Class;
    }
    return repr == ExistentialRepresentation::Opaque;
  }
  case ExistentialRepresentation::Metatype:
    return is<ExistentialMetatypeType>();
  }
}
