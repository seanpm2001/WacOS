//===--- SILGenPoly.cpp - Function Type Thunks ----------------------------===//
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
//
// In Swift's AST-level type system, function types are allowed to be equivalent
// or have a subtyping relationship even if the SIL-level lowering of the
// calling convention is different. The routines in this file implement thunking
// between lowered function types.
//
//
// Re-abstraction thunks
// =====================
// After SIL type lowering, generic substitutions become explicit, for example
// the AST type Int -> Int passes the Ints directly, whereas T -> T with Int
// substituted for T will pass the Ints like a T, as an address-only value with
// opaque type metadata. Such a thunk is called a "re-abstraction thunk" -- the
// AST-level type of the function value does not change, only the manner in
// which parameters and results are passed.
//
// Function conversion thunks
// ==========================
// In Swift's AST-level type system, certain types have a subtype relation
// involving a representation change. For example, a concrete type is always
// a subtype of any protocol it conforms to. The upcast from the concrete
// type to an existential type for the protocol requires packaging the
// payload together with type metadata and witness tables.
//
// Between function types, the type A -> B is defined to be a subtype of
// A' -> B' iff A' is a subtype of A, and B is a subtype of B' -- parameters
// are contravariant, and results are covariant.
//
// A subtype conversion of a function value A -> B is performed by wrapping
// the function value in a thunk of type A' -> B'. The thunk takes an A' and
// converts it into an A, calls the inner function value, and converts the
// result from B to B'.
//
// VTable thunks
// =============
//
// If a base class is generic and a derived class substitutes some generic
// parameter of the base with a concrete type, the derived class can override
// methods in the base that involved generic types. In the derived class, a
// method override that involves substituted types will have a different
// SIL lowering than the base method. In this case, the overridden vtable entry
// will point to a thunk which transforms parameters and results and invokes
// the derived method.
//
// Some limited forms of subtyping are also supported for method overrides;
// namely, a derived method's parameter can be a superclass of, or more
// optional than, a parameter of the base, and result can be a subclass of,
// or less optional than, the result of the base.
//
// Witness thunks
// ==============
//
// Currently protocol witness methods are called with an additional generic
// parameter bound to the Self type, and thus always require a thunk.
//
// Thunks for class method witnesses dispatch through the vtable allowing
// inherited witnesses to be overridden in subclasses. Hence a witness thunk
// might require two levels of abstraction difference -- the method might
// override a base class method with more generic types, and the protocol
// requirement may involve associated types which are always concrete in the
// conforming class.
//
// Other thunks
// ============
//
// Foreign-to-native, native-to-foreign thunks for declarations and function
// values are implemented in SILGenBridging.cpp.
//
//===----------------------------------------------------------------------===//

#include "SILGen.h"
#include "Scope.h"
#include "swift/Basic/Fallthrough.h"
#include "swift/AST/AST.h"
#include "swift/AST/Decl.h"
#include "swift/AST/DiagnosticsCommon.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/Types.h"
#include "swift/SIL/PrettyStackTrace.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/TypeLowering.h"
#include "Initialization.h"
#include "LValue.h"
#include "RValue.h"

using namespace swift;
using namespace Lowering;

/// A helper function that pulls an element off the front of an array.
template <class T>
static const T &claimNext(ArrayRef<T> &array) {
  assert(!array.empty() && "claiming next from empty array!");
  const T &result = array.front();
  array = array.slice(1);
  return result;
}

namespace {
  /// An abstract class for transforming first-class SIL values.
  class Transform {
  private:
    SILGenFunction &SGF;
    SILLocation Loc;

  public:
    Transform(SILGenFunction &SGF, SILLocation loc) : SGF(SGF), Loc(loc) {}
    virtual ~Transform() = default;

    /// Transform an arbitrary value.
    RValue transform(RValue &&input,
                     AbstractionPattern inputOrigType,
                     CanType inputSubstType,
                     AbstractionPattern outputOrigType,
                     CanType outputSubstType,
                     SGFContext ctxt);

    /// Transform an arbitrary value.
    ManagedValue transform(ManagedValue input,
                           AbstractionPattern inputOrigType,
                           CanType inputSubstType,
                           AbstractionPattern outputOrigType,
                           CanType outputSubstType,
                           SGFContext ctxt);

    /// Transform a metatype value.
    ManagedValue transformMetatype(ManagedValue fn,
                                   AbstractionPattern inputOrigType,
                                   CanMetatypeType inputSubstType,
                                   AbstractionPattern outputOrigType,
                                   CanMetatypeType outputSubstType);

    /// Transform a tuple value.
    ManagedValue transformTuple(ManagedValue input,
                                AbstractionPattern inputOrigType,
                                CanTupleType inputSubstType,
                                AbstractionPattern outputOrigType,
                                CanTupleType outputSubstType,
                                SGFContext ctxt);

    /// Transform a function value.
    ManagedValue transformFunction(ManagedValue fn,
                                   AbstractionPattern inputOrigType,
                                   CanAnyFunctionType inputSubstType,
                                   AbstractionPattern outputOrigType,
                                   CanAnyFunctionType outputSubstType,
                                   const TypeLowering &expectedTL);
  };
};

static ArrayRef<ProtocolConformanceRef>
collectExistentialConformances(Module *M, Type fromType, Type toType) {
  assert(!fromType->isAnyExistentialType());
  
  SmallVector<ProtocolDecl *, 4> protocols;
  toType->getAnyExistentialTypeProtocols(protocols);
  
  SmallVector<ProtocolConformanceRef, 4> conformances;
  for (auto proto : protocols) {
    auto conformance =
      M->lookupConformance(fromType, proto, nullptr).getPointer();
    conformances.push_back(*conformance);
  }
  
  return M->getASTContext().AllocateCopy(conformances);
}

static CanArchetypeType getOpenedArchetype(Type openedType) {
  while (auto metatypeTy = openedType->getAs<MetatypeType>())
    openedType = metatypeTy->getInstanceType();
  return cast<ArchetypeType>(openedType->getCanonicalType());
}

static ManagedValue emitTransformExistential(SILGenFunction &SGF,
                                             SILLocation loc,
                                             ManagedValue input,
                                             CanType inputType,
                                             CanType outputType,
                                             SGFContext ctxt) {
  assert(inputType != outputType);

  SILGenFunction::OpaqueValueState state;
  CanArchetypeType openedArchetype;

  if (inputType->isAnyExistentialType()) {
    CanType openedType = ArchetypeType::getAnyOpened(inputType);
    SILType loweredOpenedType = SGF.getLoweredType(openedType);

    // Unwrap zero or more metatype levels
    openedArchetype = getOpenedArchetype(openedType);

    state = SGF.emitOpenExistential(loc, input,
                                    openedArchetype, loweredOpenedType);
    inputType = openedType;
  }

  // Build conformance table
  Type fromInstanceType = inputType;
  Type toInstanceType = outputType;
  
  // Look through metatypes
  while (fromInstanceType->is<AnyMetatypeType>() &&
         toInstanceType->is<ExistentialMetatypeType>()) {
    fromInstanceType = fromInstanceType->castTo<AnyMetatypeType>()
        ->getInstanceType();
    toInstanceType = toInstanceType->castTo<ExistentialMetatypeType>()
        ->getInstanceType();
  }

  ArrayRef<ProtocolConformanceRef> conformances =
      collectExistentialConformances(SGF.SGM.M.getSwiftModule(),
                                     fromInstanceType,
                                     toInstanceType);

  // Build result existential
  AbstractionPattern opaque = AbstractionPattern::getOpaque();
  const TypeLowering &concreteTL = SGF.getTypeLowering(opaque, inputType);
  const TypeLowering &expectedTL = SGF.getTypeLowering(outputType);
  input = SGF.emitExistentialErasure(
                   loc, inputType, concreteTL, expectedTL,
                   conformances, ctxt,
                   [&](SGFContext C) -> ManagedValue {
                     if (openedArchetype)
                       return SGF.manageOpaqueValue(state, loc, C);
                     return input;
                   });
  
  return input;
}

/// Apply this transformation to an arbitrary value.
RValue Transform::transform(RValue &&input,
                            AbstractionPattern inputOrigType,
                            CanType inputSubstType,
                            AbstractionPattern outputOrigType,
                            CanType outputSubstType,
                            SGFContext ctxt) {
  // Fast path: we don't have a tuple.
  auto inputTupleType = dyn_cast<TupleType>(inputSubstType);
  if (!inputTupleType) {
    assert(!isa<TupleType>(outputSubstType) &&
           "transformation introduced a tuple?");
    auto result = transform(std::move(input).getScalarValue(),
                            inputOrigType, inputSubstType,
                            outputOrigType, outputSubstType, ctxt);
    return RValue(result, outputSubstType);
  }

  // Okay, we have a tuple.  The output type will also be a tuple unless
  // there's a subtyping conversion that erases tuples, but that's currently
  // not allowed by the typechecker, which considers existential erasure to
  // be a conversion relation, not a subtyping one.  Anyway, it would be
  // possible to support that here, but since it's not currently required...
  assert(isa<TupleType>(outputSubstType) &&
         "subtype constraint erasing tuple is not currently implemented");
  auto outputTupleType = cast<TupleType>(outputSubstType);
  assert(inputTupleType->getNumElements() == outputTupleType->getNumElements());

  // Pull the r-value apart.
  SmallVector<RValue, 8> inputElts;
  std::move(input).extractElements(inputElts);

  // Emit into the context initialization if it's present and possible
  // to split.
  SmallVector<InitializationPtr, 4> eltInitsBuffer;
  MutableArrayRef<InitializationPtr> eltInits;
  auto tupleInit = ctxt.getEmitInto();
  if (!ctxt.getEmitInto()->canSplitIntoTupleElements()) {
    tupleInit = nullptr;
  } else {
    eltInits = tupleInit->splitIntoTupleElements(SGF, Loc, outputTupleType,
                                                 eltInitsBuffer);
  }

  // At this point, if tupleInit is non-null, we must emit all of the
  // elements into their corresponding contexts.
  assert(tupleInit == nullptr ||
         eltInits.size() == inputTupleType->getNumElements());

  SmallVector<ManagedValue, 8> outputExpansion;
  for (auto eltIndex : indices(inputTupleType->getElementTypes())) {
    // Determine the appropriate context for the element.
    SGFContext eltCtxt;
    if (tupleInit) eltCtxt = SGFContext(eltInits[eltIndex].get());

    // Recurse.
    RValue outputElt = transform(std::move(inputElts[eltIndex]),
                                 inputOrigType.getTupleElementType(eltIndex),
                                 inputTupleType.getElementType(eltIndex),
                                 outputOrigType.getTupleElementType(eltIndex),
                                 outputTupleType.getElementType(eltIndex),
                                 eltCtxt);

    // Force the r-value into its context if necessary.
    assert(!outputElt.isInContext() || tupleInit != nullptr);
    if (tupleInit && !outputElt.isInContext()) {
      std::move(outputElt).forwardInto(SGF, Loc, eltInits[eltIndex].get());
    } else {
      std::move(outputElt).getAll(outputExpansion);
    }
  }

  // If we emitted into context, be sure to finish the overall initialization.
  if (tupleInit) {
    tupleInit->finishInitialization(SGF);
    return RValue();
  }

  return RValue(outputExpansion, outputTupleType);
}

// Single @objc protocol value metatypes can be converted to the ObjC
// Protocol class type.
static bool isProtocolClass(Type t) {
  auto classDecl = t->getClassOrBoundGenericClass();
  if (!classDecl)
    return false;

  ASTContext &ctx = classDecl->getASTContext();
  return (classDecl->getName() == ctx.Id_Protocol &&
          classDecl->getModuleContext()->getName() == ctx.Id_ObjectiveC);
};

static ManagedValue emitManagedLoad(SILGenFunction &gen, SILLocation loc,
                                    ManagedValue addr,
                                    const TypeLowering &addrTL) {
  auto loadedValue = gen.B.createLoad(loc, addr.forward(gen));
  return gen.emitManagedRValueWithCleanup(loadedValue, addrTL);
}

/// Apply this transformation to an arbitrary value.
ManagedValue Transform::transform(ManagedValue v,
                                  AbstractionPattern inputOrigType,
                                  CanType inputSubstType,
                                  AbstractionPattern outputOrigType,
                                  CanType outputSubstType,
                                  SGFContext ctxt) {
  // Look through inout types.
  if (isa<InOutType>(inputSubstType))
    inputSubstType = CanType(inputSubstType->getInOutObjectType());
  
  // Load if the result isn't address-only.  All the translation routines
  // expect this.
  if (v.getType().isAddress()) {
    auto &inputTL = SGF.getTypeLowering(v.getType());
    if (!inputTL.isAddressOnly()) {
      v = emitManagedLoad(SGF, Loc, v, inputTL);
    }
  }

  const TypeLowering &expectedTL = SGF.getTypeLowering(outputOrigType,
                                                       outputSubstType);
  auto loweredResultTy = expectedTL.getLoweredType();

  // Nothing to convert
  if (v.getType() == loweredResultTy)
    return v;

  OptionalTypeKind outputOTK, inputOTK;
  CanType inputObjectType = inputSubstType.getAnyOptionalObjectType(inputOTK);
  CanType outputObjectType = outputSubstType.getAnyOptionalObjectType(outputOTK);

  // If the value is less optional than the desired formal type, wrap in
  // an optional.
  if (outputOTK != OTK_None && inputOTK == OTK_None) {
    return SGF.emitInjectOptional(Loc, v,
                                  inputSubstType, outputSubstType,
                                  expectedTL, ctxt);
  }

  // If the value is IUO, but the desired formal type isn't optional, force it.
  if (inputOTK == OTK_ImplicitlyUnwrappedOptional
      && outputOTK == OTK_None) {
    v = SGF.emitCheckedGetOptionalValueFrom(Loc, v,
                                            SGF.getTypeLowering(v.getType()),
                                            SGFContext());

    // Check if we have any more conversions remaining.
    if (v.getType() == loweredResultTy)
      return v;

    inputOTK = OTK_None;
  }

  // Optional-to-optional conversion.
  if (inputOTK != OTK_None && outputOTK != OTK_None &&
      (inputOTK != outputOTK ||
       inputObjectType != outputObjectType)) {
    // If the conversion is trivial, just cast.
    if (SGF.SGM.Types.checkForABIDifferences(v.getType().getSwiftRValueType(),
                                             loweredResultTy.getSwiftRValueType())
          == TypeConverter::ABIDifference::Trivial) {
      SILValue result = v.getValue();
      if (v.getType().isAddress())
        result = SGF.B.createUncheckedAddrCast(Loc, result, loweredResultTy);
      else
        result = SGF.B.createUncheckedBitCast(Loc, result, loweredResultTy);
      return ManagedValue(result, v.getCleanup());
    }

    auto transformOptionalPayload = [&](SILGenFunction &gen,
                                        SILLocation loc,
                                        ManagedValue input,
                                        SILType loweredResultTy) -> ManagedValue {
      return transform(input,
                       AbstractionPattern::getOpaque(), inputObjectType,
                       AbstractionPattern::getOpaque(), outputObjectType,
                       SGFContext());
    };

    return SGF.emitOptionalToOptional(Loc, v, loweredResultTy,
                                      transformOptionalPayload);
  }
  
  // Abstraction changes:

  //  - functions
  if (auto outputFnType = dyn_cast<AnyFunctionType>(outputSubstType)) {
    auto inputFnType = cast<AnyFunctionType>(inputSubstType);
    return transformFunction(v,
                             inputOrigType, inputFnType,
                             outputOrigType, outputFnType,
                             expectedTL);
  }

  //  - tuples of transformable values
  if (auto outputTupleType = dyn_cast<TupleType>(outputSubstType)) {
    auto inputTupleType = cast<TupleType>(inputSubstType);
    return transformTuple(v,
                          inputOrigType, inputTupleType,
                          outputOrigType, outputTupleType,
                          ctxt);
  }

  //  - metatypes
  if (auto outputMetaType = dyn_cast<MetatypeType>(outputSubstType)) {
    auto inputMetaType = cast<MetatypeType>(inputSubstType);
    return transformMetatype(v,
                             inputOrigType, inputMetaType,
                             outputOrigType, outputMetaType);
  }

  // Subtype conversions:

  //  - upcasts
  if (outputSubstType->getClassOrBoundGenericClass() &&
      inputSubstType->getClassOrBoundGenericClass()) {
    auto class1 = inputSubstType->getClassOrBoundGenericClass();
    auto class2 = outputSubstType->getClassOrBoundGenericClass();

    // CF <-> Objective-C via toll-free bridging.
    if ((class1->getForeignClassKind() == ClassDecl::ForeignKind::CFType) ^
        (class2->getForeignClassKind() == ClassDecl::ForeignKind::CFType)) {
       return ManagedValue(SGF.B.createUncheckedRefCast(Loc,
                                                        v.getValue(),
                                                        loweredResultTy),
                           v.getCleanup());
    }

    // Upcast to a superclass.
    return ManagedValue(SGF.B.createUpcast(Loc,
                                           v.getValue(),
                                           loweredResultTy),
                        v.getCleanup());
  }

  //  - upcasts from an archetype
  if (outputSubstType->getClassOrBoundGenericClass()) {
    if (auto archetypeType = dyn_cast<ArchetypeType>(inputSubstType)) {
      if (archetypeType->getSuperclass()) {
        // Replace the cleanup with a new one on the superclass value so we
        // always use concrete retain/release operations.
        return ManagedValue(SGF.B.createUpcast(Loc,
                                               v.getValue(),
                                               loweredResultTy),
                            v.getCleanup());
      }
    }
  }

  // - metatype to Protocol conversion
  if (isProtocolClass(outputSubstType)) {
    if (auto metatypeTy = dyn_cast<MetatypeType>(inputSubstType)) {
      return SGF.emitProtocolMetatypeToObject(Loc, metatypeTy,
                                   SGF.getLoweredLoadableType(outputSubstType));
    }
  }

  // - metatype to AnyObject conversion
  if (outputSubstType->isAnyObject() &&
      isa<MetatypeType>(inputSubstType)) {
    return SGF.emitClassMetatypeToObject(Loc, v,
                                   SGF.getLoweredLoadableType(outputSubstType));
  }
  
  // - existential metatype to AnyObject conversion
  if (outputSubstType->isAnyObject() &&
      isa<ExistentialMetatypeType>(inputSubstType)) {
    return SGF.emitExistentialMetatypeToObject(Loc, v,
                                   SGF.getLoweredLoadableType(outputSubstType));
  }

  //  - existentials
  if (outputSubstType->isAnyExistentialType()) {
    // We have to re-abstract payload if its a metatype or a function
    v = SGF.emitSubstToOrigValue(Loc, v, AbstractionPattern::getOpaque(),
                                 inputSubstType);
    return emitTransformExistential(SGF, Loc, v,
                                    inputSubstType, outputSubstType,
                                    ctxt);
  }

  // Should have handled the conversion in one of the cases above.
  llvm_unreachable("Unhandled transform?");
}

ManagedValue Transform::transformMetatype(ManagedValue meta,
                                          AbstractionPattern inputOrigType,
                                          CanMetatypeType inputSubstType,
                                          AbstractionPattern outputOrigType,
                                          CanMetatypeType outputSubstType) {
  assert(!meta.hasCleanup() && "metatype with cleanup?!");

  auto expectedType = SGF.getTypeLowering(outputOrigType,
                                          outputSubstType).getLoweredType();
  auto wasRepr = meta.getType().castTo<MetatypeType>()->getRepresentation();
  auto willBeRepr = expectedType.castTo<MetatypeType>()->getRepresentation();
  
  SILValue result;

  if ((wasRepr == MetatypeRepresentation::Thick &&
       willBeRepr == MetatypeRepresentation::Thin) ||
      (wasRepr == MetatypeRepresentation::Thin &&
       willBeRepr == MetatypeRepresentation::Thick)) {
    // If we have a thin-to-thick abstraction change, cook up new a metatype
    // value out of nothing -- thin metatypes carry no runtime state.
    result = SGF.B.createMetatype(Loc, expectedType);
  } else {
    // Otherwise, we have a metatype subtype conversion of thick metatypes.
    assert(wasRepr == willBeRepr && "Unhandled metatype conversion");
    result = SGF.B.createUpcast(Loc, meta.getUnmanagedValue(), expectedType);
  }

  return ManagedValue::forUnmanaged(result);
}

/// Explode a managed tuple into a bunch of managed elements.
///
/// If the tuple is in memory, the result elements will also be in
/// memory.
typedef std::pair<ManagedValue, const TypeLowering *> ManagedValueAndType;
static void explodeTuple(SILGenFunction &gen,
                         SILLocation loc,
                         ManagedValue managedTuple,
                         SmallVectorImpl<ManagedValueAndType> &out) {
  // None of the operations we do here can fail, so we can atomically
  // disable the tuple's cleanup and then create cleanups for all the
  // elements.
  SILValue tuple = managedTuple.forward(gen);

  auto tupleSILType = tuple->getType();
  auto tupleType = tupleSILType.castTo<TupleType>();

  out.reserve(tupleType->getNumElements());

  for (auto index : indices(tupleType.getElementTypes())) {
    // We're starting with a SIL-lowered tuple type, so the elements
    // must also all be SIL-lowered.
    SILType eltType = tupleSILType.getTupleElementType(index);

    auto &eltTL = gen.getTypeLowering(eltType);

    ManagedValue elt;
    if (tupleSILType.isAddress()) {
      auto addr = gen.B.createTupleElementAddr(loc, tuple, index, eltType);
      elt = gen.emitManagedBufferWithCleanup(addr, eltTL);
    } else {
      auto value = gen.B.createTupleExtract(loc, tuple, index, eltType);
      elt = gen.emitManagedRValueWithCleanup(value, eltTL);
    }

    out.push_back(ManagedValueAndType(elt, &eltTL));
  }
}

/// Apply this transformation to all the elements of a tuple value,
/// which just entails mapping over each of its component elements.
ManagedValue Transform::transformTuple(ManagedValue inputTuple,
                                       AbstractionPattern inputOrigType,
                                       CanTupleType inputSubstType,
                                       AbstractionPattern outputOrigType,
                                       CanTupleType outputSubstType,
                                       SGFContext ctxt) {
  const TypeLowering &outputTL =
    SGF.getTypeLowering(outputOrigType, outputSubstType);
  assert(outputTL.isAddressOnly() == inputTuple.getType().isAddress() &&
         "expected loadable inputs to have been loaded");

  // If there's no representation difference, we're done.
  if (outputTL.getLoweredType() == inputTuple.getType())
    return inputTuple;

  assert(inputOrigType.matchesTuple(outputSubstType));
  assert(outputOrigType.matchesTuple(outputSubstType));

  auto inputType = inputTuple.getType().castTo<TupleType>();
  assert(outputSubstType->getNumElements() == inputType->getNumElements());

  // If the tuple is address only, we need to do the operation in memory.
  SILValue outputAddr;
  if (outputTL.isAddressOnly())
    outputAddr = SGF.getBufferForExprResult(Loc, outputTL.getLoweredType(),
                                            ctxt);

  // Explode the tuple into individual managed values.
  SmallVector<ManagedValueAndType, 4> inputElts;
  explodeTuple(SGF, Loc, inputTuple, inputElts);

  // Track all the managed elements whether or not we're actually
  // emitting to an address, just so that we can disable them after.
  SmallVector<ManagedValue, 4> outputElts;

  for (auto index : indices(inputType->getElementTypes())) {
    auto &inputEltTL = *inputElts[index].second;
    ManagedValue inputElt = inputElts[index].first;
    if (inputElt.getType().isAddress() && !inputEltTL.isAddressOnly()) {
      inputElt = emitManagedLoad(SGF, Loc, inputElt, inputEltTL);
    }

    auto inputEltOrigType = inputOrigType.getTupleElementType(index);
    auto inputEltSubstType = inputSubstType.getElementType(index);
    auto outputEltOrigType = outputOrigType.getTupleElementType(index);
    auto outputEltSubstType = outputSubstType.getElementType(index);

    // If we're emitting to memory, project out this element in the
    // destination buffer, then wrap that in an Initialization to
    // track the cleanup.
    Optional<TemporaryInitialization> outputEltTemp;
    if (outputAddr) {
      SILValue outputEltAddr =
        SGF.B.createTupleElementAddr(Loc, outputAddr, index);
      auto &outputEltTL = SGF.getTypeLowering(outputEltAddr->getType());
      assert(outputEltTL.isAddressOnly() == inputEltTL.isAddressOnly());
      auto cleanup =
        SGF.enterDormantTemporaryCleanup(outputEltAddr, outputEltTL);
      outputEltTemp.emplace(outputEltAddr, cleanup);
    }

    SGFContext eltCtxt =
      (outputEltTemp ? SGFContext(&outputEltTemp.getValue()) : SGFContext());
    auto outputElt = transform(inputElt,
                               inputEltOrigType, inputEltSubstType,
                               outputEltOrigType, outputEltSubstType,
                               eltCtxt);

    // If we're not emitting to memory, remember this element for
    // later assembly into a tuple.
    if (!outputEltTemp) {
      assert(outputElt);
      assert(!inputEltTL.isAddressOnly());
      outputElts.push_back(outputElt);
      continue;
    }

    // Otherwise, make sure we emit into the slot.
    auto &temp = outputEltTemp.getValue();
    auto outputEltAddr = temp.getManagedAddress();

    // That might involve storing directly.
    if (outputElt) {
      outputElt.forwardInto(SGF, Loc, outputEltAddr.getValue());
      temp.finishInitialization(SGF);
    }

    outputElts.push_back(outputEltAddr);
  }

  // Okay, disable all the individual element cleanups and collect
  // the values for a potential tuple aggregate.
  SmallVector<SILValue, 4> outputEltValues;
  for (auto outputElt : outputElts) {
    SILValue value = outputElt.forward(SGF);
    if (!outputAddr) outputEltValues.push_back(value);
  }

  // If we're emitting to an address, just manage that.
  if (outputAddr)
    return SGF.manageBufferForExprResult(outputAddr, outputTL, ctxt);

  // Otherwise, assemble the tuple value and manage that.
  auto outputTuple =
    SGF.B.createTuple(Loc, outputTL.getLoweredType(), outputEltValues);
  return SGF.emitManagedRValueWithCleanup(outputTuple, outputTL);
}

static ManagedValue manageParam(SILGenFunction &gen,
                                SILLocation loc,
                                SILValue paramValue,
                                SILParameterInfo info,
                                bool allowPlusZero) {
  switch (info.getConvention()) {
  // A deallocating parameter can always be accessed directly.
  case ParameterConvention::Direct_Deallocating:
    return ManagedValue::forUnmanaged(paramValue);
  case ParameterConvention::Direct_Guaranteed:
    if (allowPlusZero)
      return ManagedValue::forUnmanaged(paramValue);
    SWIFT_FALLTHROUGH;
  // Unowned parameters are only guaranteed at the instant of the call, so we
  // must retain them even if we're in a context that can accept a +0 value.
  case ParameterConvention::Direct_Unowned:
    gen.getTypeLowering(paramValue->getType())
          .emitRetainValue(gen.B, loc, paramValue);
    SWIFT_FALLTHROUGH;
  case ParameterConvention::Direct_Owned:
    return gen.emitManagedRValueWithCleanup(paramValue);

  case ParameterConvention::Indirect_In_Guaranteed:
    // FIXME: Avoid a behavior change while guaranteed self is disabled by
    // default.
    if (allowPlusZero) {
      return ManagedValue::forUnmanaged(paramValue);
    } else {
      auto copy = gen.emitTemporaryAllocation(loc, paramValue->getType());
      gen.B.createCopyAddr(loc, paramValue, copy, IsNotTake, IsInitialization);
      return gen.emitManagedBufferWithCleanup(copy);
    }
  case ParameterConvention::Indirect_Inout:
  case ParameterConvention::Indirect_InoutAliasable:
    return ManagedValue::forLValue(paramValue);
  case ParameterConvention::Indirect_In:
    return gen.emitManagedBufferWithCleanup(paramValue);
  }
  llvm_unreachable("bad parameter convention");
}

void SILGenFunction::collectThunkParams(SILLocation loc,
                                        SmallVectorImpl<ManagedValue> &params,
                                        bool allowPlusZero) {
  // Add the indirect results.
  for (auto result : F.getLoweredFunctionType()->getIndirectResults()) {
    auto paramTy = F.mapTypeIntoContext(result.getSILType());
    (void) new (SGM.M) SILArgument(F.begin(), paramTy);
  }

  // Add the parameters.
  auto paramTypes = F.getLoweredFunctionType()->getParameters();
  for (auto param : paramTypes) {
    auto paramTy = F.mapTypeIntoContext(param.getSILType());
    auto paramValue = new (SGM.M) SILArgument(F.begin(), paramTy);
    auto paramMV = manageParam(*this, loc, paramValue, param, allowPlusZero);
    params.push_back(paramMV);
  }
}

/// Force a ManagedValue to be stored into a temporary initialization
/// if it wasn't emitted that way directly.
static void emitForceInto(SILGenFunction &SGF, SILLocation loc,
                          ManagedValue result, TemporaryInitialization &temp) {
  if (result.isInContext()) return;
  result.forwardInto(SGF, loc, temp.getAddress());
  temp.finishInitialization(SGF);
}

/// If the type is a single-element tuple, return the element type.
static CanType getSingleTupleElement(CanType type) {
  if (auto tupleType = dyn_cast<TupleType>(type)) {
    if (tupleType->getNumElements() == 1)
      return tupleType.getElementType(0);
  }

  return type;
}

namespace {
  class TranslateArguments {
    SILGenFunction &SGF;
    SILLocation Loc;
    ArrayRef<ManagedValue> Inputs;
    SmallVectorImpl<ManagedValue> &Outputs;
    ArrayRef<SILParameterInfo> OutputTypes;
  public:
    TranslateArguments(SILGenFunction &SGF, SILLocation loc,
                       ArrayRef<ManagedValue> inputs,
                       SmallVectorImpl<ManagedValue> &outputs,
                       ArrayRef<SILParameterInfo> outputTypes)
      : SGF(SGF), Loc(loc), Inputs(inputs), Outputs(outputs),
        OutputTypes(outputTypes) {}

    void translate(AbstractionPattern inputOrigType,
                   CanType inputSubstType,
                   AbstractionPattern outputOrigType,
                   CanType outputSubstType) {
      // Most of this function is about tuples: tuples can be represented
      // as one or many values, with varying levels of indirection.
      auto inputTupleType = dyn_cast<TupleType>(inputSubstType);
      auto outputTupleType = dyn_cast<TupleType>(outputSubstType);

      // Look inside one-element exploded tuples, but not if both input
      // and output types are *both* one-element tuples.
      if (!(inputTupleType && outputTupleType &&
            inputTupleType.getElementTypes().size() == 1 &&
            outputTupleType.getElementTypes().size() == 1)) {
        if (inputOrigType.isTuple() &&
            inputOrigType.getNumTupleElements() == 1) {
          inputOrigType = inputOrigType.getTupleElementType(0);
          inputSubstType = getSingleTupleElement(inputSubstType);
          return translate(inputOrigType, inputSubstType,
                           outputOrigType, outputSubstType);
        }

        if (outputOrigType.isTuple() &&
            outputOrigType.getNumTupleElements() == 1) {
          outputOrigType = outputOrigType.getTupleElementType(0);
          outputSubstType = getSingleTupleElement(outputSubstType);
          return translate(inputOrigType, inputSubstType,
                           outputOrigType, outputSubstType);
        }
      }

      // Special-case: tuples containing inouts.
      if (inputTupleType && inputTupleType->hasInOut()) {
        // Non-materializable tuple types cannot be bound as generic
        // arguments, so none of the remaining transformations apply.
        // Instead, the outermost tuple layer is exploded, even when
        // they are being passed opaquely. See the comment in
        // AbstractionPattern.h for a discussion.
        return translateParallelExploded(inputOrigType,
                                         inputTupleType,
                                         outputOrigType,
                                         outputTupleType);
      }

      // Case where the input type is an exploded tuple.
      if (inputOrigType.isTuple()) {
        if (outputOrigType.isTuple()) {
          // Both input and output are exploded tuples, easy case.
          return translateParallelExploded(inputOrigType,
                                           inputTupleType,
                                           outputOrigType,
                                           outputTupleType);
        }

        // Tuple types are subtypes of their optionals
        OptionalTypeKind outputOTK;
        if (auto outputObjectType = outputSubstType.getAnyOptionalObjectType(outputOTK)) {
          // The input is exploded and the output is an optional tuple.
          // Translate values and collect them into a single optional
          // payload.
          auto outputTupleType = cast<TupleType>(outputObjectType);

          return translateAndImplodeIntoOptional(inputOrigType,
                                                 inputTupleType,
                                                 outputTupleType,
                                                 outputOTK);

          // FIXME: optional of Any (ugh...)
        }

        if (outputTupleType) {
          // The input is exploded and the output is not. Translate values
          // and store them to a result tuple in memory.
          assert(outputOrigType.isTypeParameter() &&
                 "Output is not a tuple and is not opaque?");

          auto output = claimNextOutputType();
          auto &outputTL = SGF.getTypeLowering(output.getSILType());
          auto temp = SGF.emitTemporary(Loc, outputTL);
          translateAndImplodeInto(inputOrigType,
                                  inputTupleType,
                                  outputOrigType,
                                  outputTupleType,
                                  *temp.get());

          Outputs.push_back(temp->getManagedAddress());
          return;
        }

        // FIXME: Tuple-to-Any conversions
        llvm_unreachable("Unhandled conversion from exploded tuple");
      }

      // Handle output being an exploded tuple when the input is opaque.
      if (outputOrigType.isTuple()) {
        if (inputTupleType) {
          // The input is exploded and the output is not. Translate values
          // and store them to a result tuple in memory.
          assert(inputOrigType.isTypeParameter() &&
                 "Input is not a tuple and is not opaque?");

          return translateAndExplodeOutOf(inputOrigType,
                                          inputTupleType,
                                          outputOrigType,
                                          outputTupleType,
                                          claimNextInput());
        }

        // FIXME: IUO<Tuple> to Tuple
        llvm_unreachable("Unhandled conversion to exploded tuple");
      }

      // Okay, we are now working with a single value turning into a
      // single value.
      auto inputElt = claimNextInput();
      auto outputEltType = claimNextOutputType();
      translateSingle(inputOrigType, inputSubstType,
                      outputOrigType, outputSubstType,
                      inputElt, outputEltType);
    }

  private:
    /// Handle a tuple that has been exploded in the input but wrapped in
    /// an optional in the output.
    void translateAndImplodeIntoOptional(AbstractionPattern inputOrigType,
                                         CanTupleType inputTupleType,
                                         CanTupleType outputTupleType,
                                         OptionalTypeKind OTK) {
      assert(!inputTupleType->hasInOut() &&
             !outputTupleType->hasInOut());
      assert(inputTupleType->getNumElements() ==
             outputTupleType->getNumElements());

      // Collect the tuple elements, which should all be maximally abstracted
      // to go in the optional payload.
      auto opaque = AbstractionPattern::getOpaque();
      auto &loweredTL = SGF.getTypeLowering(opaque, outputTupleType);
      auto loweredTy = loweredTL.getLoweredType();
      auto optionalTy = claimNextOutputType().getSILType();
      auto someDecl = SGF.getASTContext().getOptionalSomeDecl(OTK);
      if (loweredTL.isLoadable()) {
        // Implode into a maximally-abstracted value.
        std::function<ManagedValue (CanTupleType, CanTupleType, CanTupleType)>
        translateAndImplodeIntoValue
          = [&](CanTupleType lowered, CanTupleType input, CanTupleType output) -> ManagedValue {
            SmallVector<ManagedValue, 4> elements;
            assert(output->getNumElements() == input->getNumElements());
            for (unsigned i = 0, e = output->getNumElements(); i < e; ++i) {
              auto inputTy = input.getElementType(i);
              auto outputTy = output.getElementType(i);
              ManagedValue arg;
              if (auto outputTuple = dyn_cast<TupleType>(outputTy)) {
                auto inputTuple = cast<TupleType>(inputTy);
                arg = translateAndImplodeIntoValue(
                                    cast<TupleType>(lowered.getElementType(i)),
                                    inputTuple, outputTuple);
              } else {
                arg = claimNextInput();
                
              }
              
              if (arg.getType().isAddress())
                arg = SGF.emitLoad(Loc, arg.forward(SGF),
                                   SGF.getTypeLowering(arg.getType()),
                                   SGFContext(), IsTake);

              if (arg.getType().getSwiftRValueType() != lowered.getElementType(i))
                arg = translatePrimitive(AbstractionPattern(inputTy), inputTy,
                                         opaque, outputTy,
                                         arg);

              elements.push_back(arg);
            }
            SmallVector<SILValue, 4> forwarded;
            for (auto element : elements)
              forwarded.push_back(element.forward(SGF));
            
            auto tuple = SGF.B.createTuple(Loc,
                                       SILType::getPrimitiveObjectType(lowered),
                                       forwarded);
            return SGF.emitManagedRValueWithCleanup(tuple);
          };
        
        auto payload = translateAndImplodeIntoValue(
                                cast<TupleType>(loweredTy.getSwiftRValueType()),
                                inputTupleType,
                                outputTupleType);
        optionalTy = SGF.F.mapTypeIntoContext(optionalTy);
        auto optional = SGF.B.createEnum(Loc, payload.getValue(),
                                         someDecl, optionalTy);
        Outputs.push_back(ManagedValue(optional, payload.getCleanup()));
        return;
      } else {
        // Implode into a maximally-abstracted indirect buffer.
        auto optionalBuf = SGF.emitTemporaryAllocation(Loc, optionalTy);
        auto tupleBuf = SGF.B.createInitEnumDataAddr(Loc, optionalBuf, someDecl,
                                                     loweredTy);
        
        auto tupleTemp = SGF.useBufferAsTemporary(tupleBuf, loweredTL);

        std::function<void (CanTupleType,
                            CanTupleType,
                            CanTupleType,
                            TemporaryInitialization&)>
        translateAndImplodeIntoBuffer
          = [&](CanTupleType lowered,
                CanTupleType input,
                CanTupleType output,
                TemporaryInitialization &buf) {
            auto tupleAddr = buf.getAddress();
            SmallVector<CleanupHandle, 4> cleanups;
            
            for (unsigned i = 0, e = output->getNumElements(); i < e; ++i) {
              auto inputTy = input.getElementType(i);
              auto outputTy = output.getElementType(i);
              auto loweredOutputTy
                = SILType::getPrimitiveAddressType(lowered.getElementType(i));
              auto &loweredOutputTL = SGF.getTypeLowering(loweredOutputTy);
              auto eltAddr = SGF.B.createTupleElementAddr(Loc, tupleAddr, i,
                                                          loweredOutputTy);
              CleanupHandle eltCleanup
                = SGF.enterDormantTemporaryCleanup(eltAddr, loweredOutputTL);
              
              if (eltCleanup.isValid()) cleanups.push_back(eltCleanup);
              TemporaryInitialization eltInit(eltAddr, eltCleanup);
              
              if (auto outputTuple = dyn_cast<TupleType>(outputTy)) {
                auto inputTuple = cast<TupleType>(inputTy);
                translateAndImplodeIntoBuffer(
                             cast<TupleType>(loweredOutputTy.getSwiftRValueType()),
                             inputTuple, outputTuple, eltInit);
              } else {
                auto arg = claimNextInput();
                auto &argTL = SGF.getTypeLowering(arg.getType());
                if (arg.getType().isAddress() && argTL.isLoadable())
                  arg = SGF.emitLoad(Loc, arg.forward(SGF),
                                     argTL, SGFContext(), IsTake);
                
                if (arg.getType().getSwiftRValueType()
                      != loweredOutputTy.getSwiftRValueType()) {
                  arg = translatePrimitive(AbstractionPattern(inputTy), inputTy,
                                           opaque, outputTy,
                                           arg);
                }
                
                emitForceInto(SGF, Loc, arg, eltInit);
              }
            }
            
            // Deactivate the element cleanups and activate the tuple cleanup.
            for (auto cleanup : cleanups)
              SGF.Cleanups.forwardCleanup(cleanup);
            buf.finishInitialization(SGF);
          };
        translateAndImplodeIntoBuffer(
                                cast<TupleType>(loweredTy.getSwiftRValueType()),
                                inputTupleType,
                                outputTupleType,
                                *tupleTemp.get());
        SGF.B.createInjectEnumAddr(Loc, optionalBuf, someDecl);
        auto payload = tupleTemp->getManagedAddress();
        Outputs.push_back(ManagedValue(optionalBuf, payload.getCleanup()));
      }
    }
  
    /// Handle a tuple that has been exploded in both the input and
    /// the output.
    void translateParallelExploded(AbstractionPattern inputOrigType,
                                   CanTupleType inputSubstType,
                                   AbstractionPattern outputOrigType,
                                   CanTupleType outputSubstType) {
      assert(inputOrigType.matchesTuple(inputSubstType));
      assert(outputOrigType.matchesTuple(outputSubstType));
      // Non-materializable input and materializable output occurs
      // when witness method thunks re-abstract a non-mutating
      // witness for a mutating requirement. The inout self is just
      // loaded to produce a value in this case.
      assert(inputSubstType->hasInOut() ||
             !outputSubstType->hasInOut());
      assert(inputSubstType->getNumElements() ==
             outputSubstType->getNumElements());

      for (auto index : indices(outputSubstType.getElementTypes())) {
        translate(inputOrigType.getTupleElementType(index),
                  inputSubstType.getElementType(index),
                  outputOrigType.getTupleElementType(index),
                  outputSubstType.getElementType(index));
      }
    }

    /// Given that a tuple value is being passed indirectly in the
    /// input, explode it and translate the elements.
    void translateAndExplodeOutOf(AbstractionPattern inputOrigType,
                                  CanTupleType inputSubstType,
                                  AbstractionPattern outputOrigType,
                                  CanTupleType outputSubstType,
                                  ManagedValue inputTupleAddr) {
      assert(inputOrigType.isTypeParameter());
      assert(outputOrigType.matchesTuple(outputSubstType));
      assert(!inputSubstType->hasInOut() &&
             !outputSubstType->hasInOut());
      assert(inputSubstType->getNumElements() ==
             outputSubstType->getNumElements());

      SmallVector<ManagedValueAndType, 4> inputEltAddrs;
      explodeTuple(SGF, Loc, inputTupleAddr, inputEltAddrs);
      assert(inputEltAddrs.size() == outputSubstType->getNumElements());

      for (auto index : indices(outputSubstType.getElementTypes())) {
        auto inputEltOrigType = inputOrigType.getTupleElementType(index);
        auto inputEltSubstType = inputSubstType.getElementType(index);
        auto outputEltOrigType = outputOrigType.getTupleElementType(index);
        auto outputEltSubstType = outputSubstType.getElementType(index);
        auto inputEltAddr = inputEltAddrs[index].first;
        assert(inputEltAddr.getType().isAddress());

        if (auto outputEltTupleType = dyn_cast<TupleType>(outputEltSubstType)) {
          assert(outputEltOrigType.isTuple());
          auto inputEltTupleType = cast<TupleType>(inputEltSubstType);
          translateAndExplodeOutOf(inputEltOrigType,
                                   inputEltTupleType,
                                   outputEltOrigType,
                                   outputEltTupleType,
                                   inputEltAddr);
        } else {
          auto outputType = claimNextOutputType();
          translateSingle(inputEltOrigType,
                          inputEltSubstType,
                          outputEltOrigType,
                          outputEltSubstType,
                          inputEltAddr,
                          outputType);
        }
      }
    }

    /// Given that a tuple value is being passed indirectly in the
    /// output, translate the elements and implode it.
    void translateAndImplodeInto(AbstractionPattern inputOrigType,
                                 CanTupleType inputSubstType,
                                 AbstractionPattern outputOrigType,
                                 CanTupleType outputSubstType,
                                 TemporaryInitialization &tupleInit) {
      assert(inputOrigType.matchesTuple(inputSubstType));
      assert(outputOrigType.isTypeParameter());
      assert(!inputSubstType->hasInOut() &&
             !outputSubstType->hasInOut());
      assert(inputSubstType->getNumElements() ==
             outputSubstType->getNumElements());

      SmallVector<CleanupHandle, 4> cleanups;

      for (auto index : indices(outputSubstType.getElementTypes())) {
        auto inputEltOrigType = inputOrigType.getTupleElementType(index);
        auto inputEltSubstType = inputSubstType.getElementType(index);
        auto outputEltOrigType = outputOrigType.getTupleElementType(index);
        auto outputEltSubstType = outputSubstType.getElementType(index);
        auto eltAddr =
          SGF.B.createTupleElementAddr(Loc, tupleInit.getAddress(), index);

        auto &outputEltTL = SGF.getTypeLowering(eltAddr->getType());
        CleanupHandle eltCleanup =
          SGF.enterDormantTemporaryCleanup(eltAddr, outputEltTL);
        if (eltCleanup.isValid()) cleanups.push_back(eltCleanup);

        TemporaryInitialization eltInit(eltAddr, eltCleanup);
        if (auto outputEltTupleType = dyn_cast<TupleType>(outputEltSubstType)) {
          auto inputEltTupleType = cast<TupleType>(inputEltSubstType);
          translateAndImplodeInto(inputEltOrigType, inputEltTupleType,
                                  outputEltOrigType, outputEltTupleType,
                                  eltInit);
        } else {
          // Otherwise, we come from a single value.
          auto input = claimNextInput();
          translateSingleInto(inputEltOrigType, inputEltSubstType,
                              outputEltOrigType, outputEltSubstType,
                              input, eltInit);
        }
      }

      // Deactivate all the element cleanups and activate the tuple cleanup.
      for (auto cleanup : cleanups)
        SGF.Cleanups.forwardCleanup(cleanup);
      tupleInit.finishInitialization(SGF);
    }

    /// Translate a single value and add it as an output.
    void translateSingle(AbstractionPattern inputOrigType,
                         CanType inputSubstType,
                         AbstractionPattern outputOrigType,
                         CanType outputSubstType,
                         ManagedValue input,
                         SILParameterInfo result) {
      // Easy case: we want to pass exactly this value.
      if (input.getType() == result.getSILType()) {
        Outputs.push_back(input);
        return;
      }

      switch (result.getConvention()) {
      // Direct translation is relatively easy.
      case ParameterConvention::Direct_Owned:
      case ParameterConvention::Direct_Unowned:
      case ParameterConvention::Direct_Deallocating:
      case ParameterConvention::Direct_Guaranteed: {
        auto output = translatePrimitive(inputOrigType, inputSubstType,
                                         outputOrigType, outputSubstType,
                                         input);
        assert(output.getType() == result.getSILType());
        Outputs.push_back(output);
        return;
      }

      case ParameterConvention::Indirect_Inout: {
        // If it's inout, we need writeback.
        llvm::errs() << "inout writeback in abstraction difference thunk "
                        "not yet implemented\n";
        llvm::errs() << "input value ";
        input.getValue()->dump();
        llvm::errs() << "output type " << result.getSILType() << "\n";
        abort();
      }
      case ParameterConvention::Indirect_In:
      case ParameterConvention::Indirect_In_Guaranteed: {
        // We need to translate into a temporary.
        auto &outputTL = SGF.getTypeLowering(result.getSILType());
        auto temp = SGF.emitTemporary(Loc, outputTL);
        translateSingleInto(inputOrigType, inputSubstType,
                            outputOrigType, outputSubstType,
                            input, *temp.get());
        Outputs.push_back(temp->getManagedAddress());
        return;
      }
      case ParameterConvention::Indirect_InoutAliasable: {
        llvm_unreachable("abstraction difference in aliasable argument not "
                         "allowed");
      }
      }

      llvm_unreachable("Covered switch isn't covered?!");
    }

    /// Translate a single value and initialize the given temporary with it.
    void translateSingleInto(AbstractionPattern inputOrigType,
                             CanType inputSubstType,
                             AbstractionPattern outputOrigType,
                             CanType outputSubstType,
                             ManagedValue input,
                             TemporaryInitialization &temp) {
      auto output = translatePrimitive(inputOrigType, inputSubstType,
                                       outputOrigType, outputSubstType,
                                       input, SGFContext(&temp));
      forceInto(output, temp);
    }

    /// Apply primitive translation to the given value.
    ManagedValue translatePrimitive(AbstractionPattern inputOrigType,
                                    CanType inputSubstType,
                                    AbstractionPattern outputOrigType,
                                    CanType outputSubstType,
                                    ManagedValue input,
                                    SGFContext context = SGFContext()) {
      return SGF.emitTransformedValue(Loc, input,
                                      inputOrigType, inputSubstType,
                                      outputOrigType, outputSubstType,
                                      context);
    }

    /// Force the given result into the given initialization.
    void forceInto(ManagedValue result, TemporaryInitialization &temp) {
      emitForceInto(SGF, Loc, result, temp);
    }

    ManagedValue claimNextInput() {
      return claimNext(Inputs);
    }

    SILParameterInfo claimNextOutputType() {
      return claimNext(OutputTypes);
    }
  };
}

/// Forward arguments according to a function type's ownership conventions.
static void forwardFunctionArguments(SILGenFunction &gen,
                                     SILLocation loc,
                                     CanSILFunctionType fTy,
                                     ArrayRef<ManagedValue> managedArgs,
                                     SmallVectorImpl<SILValue> &forwardedArgs) {
  auto argTypes = fTy->getParameters();
  for (auto index : indices(managedArgs)) {
    auto &arg = managedArgs[index];
    auto argTy = argTypes[index];
    forwardedArgs.push_back(argTy.isConsumed() ? arg.forward(gen)
                                               : arg.getValue());
  }
}

namespace {

/// A helper class to translate the inner results to the outer results.
///
/// Creating a result-translation plan involves three basic things:
///   - building SILArguments for each of the outer indirect results
///   - building a list of SILValues for each of the inner indirect results
///   - building a list of Operations to perform which will reabstract
///     the inner results to match the outer.
class ResultPlanner {
  SILGenFunction &Gen;
  SILLocation Loc;

  /// A single result-translation operation.
  struct Operation {
    enum Kind {
      /// Take the last N direct outer results, tuple them, and make that a
      /// new direct outer result.
      ///
      /// Valid: NumElements, OuterResult
      TupleDirect,

      /// Take the last direct outer result, inject it into an optional
      /// type, and make that a new direct outer result.
      ///
      /// Valid: SomeDecl, OuterResult
      InjectOptionalDirect,

      /// Finish building an optional Some in the given address.
      ///
      /// Valid: SomeDecl, OuterResultAddr
      InjectOptionalIndirect,

      /// Take the next direct inner result and just make it a direct
      /// outer result.
      ///
      /// Valid: InnerResult, OuterResult.
      DirectToDirect,

      /// Take the next direct inner result and store it into an
      /// outer result address.
      ///
      /// Valid: InnerDirect, OuterResultAddr.
      DirectToIndirect,

      /// Take from an indirect inner result and make it the next outer
      /// direct result.
      ///
      /// Valid: InnerResultAddr, OuterResult.
      IndirectToDirect,

      /// Take from an indirect inner result into an outer indirect result.
      ///
      /// Valid: InnerResultAddr, OuterResultAddr.
      IndirectToIndirect,

      /// Take a value out of the source inner result address, reabstract
      /// it, and initialize the destination outer result address.
      ///
      /// Valid: reabstraction info, InnerAddress, OuterAddress.
      ReabstractIndirectToIndirect,

      /// Take a value out of the source inner result address, reabstract
      /// it, and add it as the next direct outer result.
      ///
      /// Valid: reabstraction info, InnerAddress, OuterResult.
      ReabstractIndirectToDirect,

      /// Take the next direct inner result, reabstract it, and initialize
      /// the destination outer result address.
      ///
      /// Valid: reabstraction info, InnerResult, OuterAddress.
      ReabstractDirectToIndirect,

      /// Take the next direct inner result, reabstract it, and add it as
      /// the next direct outer result.
      ///
      /// Valid: reabstraction info, InnerResult, OuterResult.
      ReabstractDirectToDirect,
    };

    Operation(Kind kind) : TheKind(kind) {}

    Kind TheKind;

    // Reabstraction information.  Only valid for reabstraction kinds.
    AbstractionPattern InnerOrigType = AbstractionPattern::getInvalid();
    AbstractionPattern OuterOrigType = AbstractionPattern::getInvalid();
    CanType InnerSubstType, OuterSubstType;

    union {
      SILValue InnerResultAddr;
      SILResultInfo InnerResult;
      unsigned NumElements;
      EnumElementDecl *SomeDecl;
    };

    union {
      SILValue OuterResultAddr;
      SILResultInfo OuterResult;
    };
  };

  struct PlanData {
    ArrayRef<SILResultInfo> OuterResults;
    ArrayRef<SILResultInfo> InnerResults;
    SmallVectorImpl<SILValue> &InnerIndirectResultAddrs;
    size_t NextOuterIndirectResultIndex;
  };

  SmallVector<Operation, 8> Operations;
public:
  ResultPlanner(SILGenFunction &gen, SILLocation loc) : Gen(gen), Loc(loc) {}

  void plan(AbstractionPattern innerOrigType, CanType innerSubstType,
            AbstractionPattern outerOrigType, CanType outerSubstType,
            CanSILFunctionType innerFnType, CanSILFunctionType outerFnType,
            SmallVectorImpl<SILValue> &innerIndirectResultAddrs) {
    // Assert that the indirect results are set up like we expect.
    assert(innerIndirectResultAddrs.empty());
    assert(Gen.F.begin()->bbarg_size() >= outerFnType->getNumIndirectResults());

    innerIndirectResultAddrs.reserve(innerFnType->getNumIndirectResults());

    PlanData data = {
      outerFnType->getAllResults(),
      innerFnType->getAllResults(),
      innerIndirectResultAddrs,
      0
    };

    // Recursively walk the result types.
    plan(innerOrigType, innerSubstType, outerOrigType, outerSubstType, data);

    // Assert that we consumed and produced all the indirect result
    // information we needed.
    assert(data.OuterResults.empty());
    assert(data.InnerResults.empty());
    assert(data.InnerIndirectResultAddrs.size() ==
             innerFnType->getNumIndirectResults());
    assert(data.NextOuterIndirectResultIndex ==
             outerFnType->getNumIndirectResults());
  }

  SILValue execute(SILValue innerResult);

private:
  void execute(ArrayRef<SILValue> innerDirectResults,
               SmallVectorImpl<SILValue> &outerDirectResults);

  void plan(AbstractionPattern innerOrigType, CanType innerSubstType,
            AbstractionPattern outerOrigType, CanType outerSubstType,
            PlanData &planData);

  void planIntoIndirectResult(AbstractionPattern innerOrigType,
                              CanType innerSubstType,
                              AbstractionPattern outerOrigType,
                              CanType outerSubstType,
                              PlanData &planData,
                              SILValue outerResultAddr);
  void planTupleIntoIndirectResult(AbstractionPattern innerOrigType,
                                   CanTupleType innerSubstType,
                                   AbstractionPattern outerOrigType,
                                   CanType outerSubstType,
                                   PlanData &planData,
                                   SILValue outerResultAddr);
  void planScalarIntoIndirectResult(AbstractionPattern innerOrigType,
                                    CanType innerSubstType,
                                    AbstractionPattern outerOrigType,
                                    CanType outerSubstType,
                                    PlanData &planData,
                                    SILResultInfo innerResult,
                                    SILValue outerResultAddr);

  void planIntoDirectResult(AbstractionPattern innerOrigType,
                            CanType innerSubstType,
                            AbstractionPattern outerOrigType,
                            CanType outerSubstType,
                            PlanData &planData,
                            SILResultInfo outerResult);
  void planScalarIntoDirectResult(AbstractionPattern innerOrigType,
                                  CanType innerSubstType,
                                  AbstractionPattern outerOrigType,
                                  CanType outerSubstType,
                                  PlanData &planData,
                                  SILResultInfo innerResult,
                                  SILResultInfo outerResult);
  void planTupleIntoDirectResult(AbstractionPattern innerOrigType,
                                 CanTupleType innerSubstType,
                                 AbstractionPattern outerOrigType,
                                 CanType outerSubstType,
                                 PlanData &planData,
                                 SILResultInfo outerResult);

  void planFromIndirectResult(AbstractionPattern innerOrigType,
                              CanType innerSubstType,
                              AbstractionPattern outerOrigType,
                              CanType outerSubstType,
                              PlanData &planData,
                              SILValue innerResultAddr);
  void planTupleFromIndirectResult(AbstractionPattern innerOrigType,
                                   CanTupleType innerSubstType,
                                   AbstractionPattern outerOrigType,
                                   CanTupleType outerSubstType,
                                   PlanData &planData,
                                   SILValue innerResultAddr);
  void planScalarFromIndirectResult(AbstractionPattern innerOrigType,
                                    CanType innerSubstType,
                                    AbstractionPattern outerOrigType,
                                    CanType outerSubstType,
                                    SILValue innerResultAddr,
                                    SILResultInfo outerResult,
                                    SILValue optOuterResultAddr);

  /// Claim the next inner result from the plan data.
  SILResultInfo claimNextInnerResult(PlanData &data) {
    return claimNext(data.InnerResults);
  }

  /// Claim the next outer result from the plan data.  If it's indirect,
  /// grab its SILArgument.
  std::pair<SILResultInfo, SILValue> claimNextOuterResult(PlanData &data) {
    SILResultInfo result = claimNext(data.OuterResults);

    SILValue resultAddr;
    if (result.isIndirect()) {
      resultAddr = Gen.F.begin()->getBBArg(data.NextOuterIndirectResultIndex++);
    }

    return { result, resultAddr };
  }

  /// Create a temporary address suitable for passing to the given inner
  /// indirect result and add it as an inner indirect result.
  SILValue addInnerIndirectResultTemporary(PlanData &data,
                                           SILResultInfo innerResult) {
    assert(innerResult.isIndirect());
    auto temporary =
      Gen.emitTemporaryAllocation(Loc, innerResult.getSILType());
    data.InnerIndirectResultAddrs.push_back(temporary);
    return temporary;
  }

  /// Cause the next inner indirect result to be emitted directly into
  /// the given outer result address.
  void addInPlace(PlanData &data, SILValue outerResultAddr) {
    data.InnerIndirectResultAddrs.push_back(outerResultAddr);
    // Does not require an Operation.
  }

  Operation &addOperation(Operation::Kind kind) {
    Operations.emplace_back(kind);
    return Operations.back();
  }

  void addDirectToDirect(SILResultInfo innerResult, SILResultInfo outerResult) {
    auto &op = addOperation(Operation::DirectToDirect);
    op.InnerResult = innerResult;
    op.OuterResult = outerResult;
  }

  void addDirectToIndirect(SILResultInfo innerResult,
                           SILValue outerResultAddr) {
    auto &op = addOperation(Operation::DirectToIndirect);
    op.InnerResult = innerResult;
    op.OuterResultAddr = outerResultAddr;
  }

  void addIndirectToDirect(SILValue innerResultAddr,
                           SILResultInfo outerResult) {
    auto &op = addOperation(Operation::IndirectToDirect);
    op.InnerResultAddr = innerResultAddr;
    op.OuterResult = outerResult;
  }

  void addIndirectToIndirect(SILValue innerResultAddr,
                             SILValue outerResultAddr) {
    auto &op = addOperation(Operation::IndirectToIndirect);
    op.InnerResultAddr = innerResultAddr;
    op.OuterResultAddr = outerResultAddr;
  }

  void addTupleDirect(unsigned numElements, SILResultInfo outerResult) {
    auto &op = addOperation(Operation::TupleDirect);
    op.NumElements = numElements;
    op.OuterResult = outerResult;
  }

  void addInjectOptionalDirect(EnumElementDecl *someDecl,
                               SILResultInfo outerResult) {
    auto &op = addOperation(Operation::InjectOptionalDirect);
    op.SomeDecl = someDecl;
    op.OuterResult = outerResult;
  }

  void addInjectOptionalIndirect(EnumElementDecl *someDecl,
                                 SILValue outerResultAddr) {
    auto &op = addOperation(Operation::InjectOptionalIndirect);
    op.SomeDecl = someDecl;
    op.OuterResultAddr = outerResultAddr;
  }

  void addReabstractDirectToDirect(AbstractionPattern innerOrigType,
                                   CanType innerSubstType,
                                   AbstractionPattern outerOrigType,
                                   CanType outerSubstType,
                                   SILResultInfo innerResult,
                                   SILResultInfo outerResult) {
    auto &op = addOperation(Operation::ReabstractDirectToDirect);
    op.InnerResult = innerResult;
    op.OuterResult = outerResult;
    op.InnerOrigType = innerOrigType;
    op.InnerSubstType = innerSubstType;
    op.OuterOrigType = outerOrigType;
    op.OuterSubstType = outerSubstType;
  }

  void addReabstractDirectToIndirect(AbstractionPattern innerOrigType,
                                     CanType innerSubstType,
                                     AbstractionPattern outerOrigType,
                                     CanType outerSubstType,
                                     SILResultInfo innerResult,
                                     SILValue outerResultAddr) {
    auto &op = addOperation(Operation::ReabstractDirectToIndirect);
    op.InnerResult = innerResult;
    op.OuterResultAddr = outerResultAddr;
    op.InnerOrigType = innerOrigType;
    op.InnerSubstType = innerSubstType;
    op.OuterOrigType = outerOrigType;
    op.OuterSubstType = outerSubstType;
  }

  void addReabstractIndirectToDirect(AbstractionPattern innerOrigType,
                                     CanType innerSubstType,
                                     AbstractionPattern outerOrigType,
                                     CanType outerSubstType,
                                     SILValue innerResultAddr,
                                     SILResultInfo outerResult) {
    auto &op = addOperation(Operation::ReabstractIndirectToDirect);
    op.InnerResultAddr = innerResultAddr;
    op.OuterResult = outerResult;
    op.InnerOrigType = innerOrigType;
    op.InnerSubstType = innerSubstType;
    op.OuterOrigType = outerOrigType;
    op.OuterSubstType = outerSubstType;
  }

  void addReabstractIndirectToIndirect(AbstractionPattern innerOrigType,
                                       CanType innerSubstType,
                                       AbstractionPattern outerOrigType,
                                       CanType outerSubstType,
                                       SILValue innerResultAddr,
                                       SILValue outerResultAddr) {
    auto &op = addOperation(Operation::ReabstractIndirectToIndirect);
    op.InnerResultAddr = innerResultAddr;
    op.OuterResultAddr = outerResultAddr;
    op.InnerOrigType = innerOrigType;
    op.InnerSubstType = innerSubstType;
    op.OuterOrigType = outerOrigType;
    op.OuterSubstType = outerSubstType;
  }
};

} // end anonymous namespace

/// Plan the reabstraction of a call result.
void ResultPlanner::plan(AbstractionPattern innerOrigType,
                         CanType innerSubstType,
                         AbstractionPattern outerOrigType,
                         CanType outerSubstType,
                         PlanData &planData) {
  // The substituted types must match up in tuple-ness and arity.
  // (Existential erasure could complicate this if we add that as a subtyping
  // relationship.)
  assert(isa<TupleType>(innerSubstType) == isa<TupleType>(outerSubstType) ||
         (isa<TupleType>(innerSubstType) &&
          outerSubstType->getAnyOptionalObjectType()));
  assert(!isa<TupleType>(outerSubstType) ||
         cast<TupleType>(innerSubstType)->getNumElements() ==
           cast<TupleType>(outerSubstType)->getNumElements());

  // If the inner abstraction pattern is a tuple, that result will be expanded.
  if (innerOrigType.isTuple()) {
    auto innerSubstTupleType = cast<TupleType>(innerSubstType);

    // If the outer abstraction pattern is also a tuple, that result will also
    // be expanded, in parallel with the inner pattern.
    if (outerOrigType.isTuple()) {
      auto outerSubstTupleType = cast<TupleType>(outerSubstType);
      assert(innerSubstTupleType->getNumElements()
               == outerSubstTupleType->getNumElements());

      // Otherwise, recursively descend into the tuples.
      for (auto eltIndex : indices(innerSubstTupleType.getElementTypes())) {
        plan(innerOrigType.getTupleElementType(eltIndex),
             innerSubstTupleType.getElementType(eltIndex),
             outerOrigType.getTupleElementType(eltIndex),
             outerSubstTupleType.getElementType(eltIndex),
             planData);
      }
      return;      
    }

    // Otherwise, the next outer result must be either opaque or optional.
    // In either case, it corresponds to a single result.
    auto outerResult = claimNextOuterResult(planData);

    // Base the plan on whether the single result is direct or indirect.
    if (outerResult.first.isIndirect()) {
      assert(outerResult.second);
      planTupleIntoIndirectResult(innerOrigType, innerSubstTupleType,
                                  outerOrigType, outerSubstType,
                                  planData, outerResult.second);
    } else {
      planTupleIntoDirectResult(innerOrigType, innerSubstTupleType,
                                outerOrigType, outerSubstType,
                                planData, outerResult.first);
    }
    return;
  }

  // Otherwise, the inner pattern is a scalar; claim the next inner result.
  SILResultInfo innerResult = claimNextInnerResult(planData);

  assert((!outerOrigType.isTuple() || innerResult.isIndirect()) &&
         "outer pattern is a tuple, inner pattern is not, but inner "
         "result is not indirect?");

  // If the inner result is a tuple, we need to expand from a temporary.
  if (innerResult.isIndirect() && outerOrigType.isTuple()) {
    SILValue innerResultAddr =
      addInnerIndirectResultTemporary(planData, innerResult);
    planTupleFromIndirectResult(innerOrigType, cast<TupleType>(innerSubstType),
                                outerOrigType, cast<TupleType>(outerSubstType),
                                planData, innerResultAddr);
    return;
  }

  // Otherwise, the outer pattern is a scalar; claim the next outer result.
  auto outerResult = claimNextOuterResult(planData);

  // If the outer result is indirect, plan to emit into that.
  if (outerResult.first.isIndirect()) {
    assert(outerResult.second);
    planScalarIntoIndirectResult(innerOrigType, innerSubstType,
                                 outerOrigType, outerSubstType,
                                 planData, innerResult, outerResult.second);

  } else {
    planScalarIntoDirectResult(innerOrigType, innerSubstType,
                               outerOrigType, outerSubstType,
                               planData, innerResult, outerResult.first);
  }
}

/// Plan the emission of a call result into an outer result address.
void ResultPlanner::planIntoIndirectResult(AbstractionPattern innerOrigType,
                                           CanType innerSubstType,
                                           AbstractionPattern outerOrigType,
                                           CanType outerSubstType,
                                           PlanData &planData,
                                           SILValue outerResultAddr) {
  assert(!outerOrigType.isTuple());

  // If the inner pattern is a tuple, expand it.
  if (innerOrigType.isTuple()) {
    planTupleIntoIndirectResult(innerOrigType, cast<TupleType>(innerSubstType),
                                outerOrigType, outerSubstType,
                                planData, outerResultAddr);

  // Otherwise, it's scalar.
  } else {
    // Claim the next inner result.
    SILResultInfo innerResult = claimNextInnerResult(planData);

    planScalarIntoIndirectResult(innerOrigType, innerSubstType,
                                 outerOrigType, outerSubstType,
                                 planData, innerResult, outerResultAddr);
  }
}

/// Plan the emission of a call result into an outer result address,
/// given that the inner abstraction pattern is a tuple.
void
ResultPlanner::planTupleIntoIndirectResult(AbstractionPattern innerOrigType,
                                           CanTupleType innerSubstType,
                                           AbstractionPattern outerOrigType,
                                           CanType outerSubstType,
                                           PlanData &planData,
                                           SILValue outerResultAddr) {
  assert(innerOrigType.isTuple());
  assert(!outerOrigType.isTuple());

  CanTupleType outerSubstTupleType = dyn_cast<TupleType>(outerSubstType);

  // If the outer type is not a tuple, it must be optional.
  if (!outerSubstTupleType) {
    // Figure out what kind of optional it is.
    OptionalTypeKind optKind;
    CanType outerSubstObjectType =
      outerSubstType.getAnyOptionalObjectType(optKind);
    assert(outerSubstObjectType &&
           "inner type was a tuple but outer type was neither a tuple nor "
           "optional");
    auto someDecl = Gen.getASTContext().getOptionalSomeDecl(optKind);

    // Prepare the value slot in the optional value.
    SILType outerObjectType =
      outerResultAddr->getType().getAnyOptionalObjectType(Gen.SGM.M, optKind);
    SILValue outerObjectResultAddr
      = Gen.B.createInitEnumDataAddr(Loc, outerResultAddr, someDecl,
                                     outerObjectType);

    // Emit into that address.
    planTupleIntoIndirectResult(innerOrigType, innerSubstType,
                                outerOrigType.getAnyOptionalObjectType(),
                                outerSubstObjectType,
                                planData, outerObjectResultAddr);

    // Add an operation to finish the enum initialization.
    addInjectOptionalIndirect(someDecl, outerResultAddr);
    return;
  }

  assert(innerSubstType->getNumElements()
           == outerSubstTupleType->getNumElements());

  for (auto eltIndex : indices(innerSubstType.getElementTypes())) {
    // Project the address of the element.
    SILValue outerEltResultAddr =
      Gen.B.createTupleElementAddr(Loc, outerResultAddr, eltIndex);

    // Plan to emit into that location.
    planIntoIndirectResult(innerOrigType.getTupleElementType(eltIndex),
                           innerSubstType.getElementType(eltIndex),
                           outerOrigType.getTupleElementType(eltIndex),
                           outerSubstTupleType.getElementType(eltIndex),
                           planData, outerEltResultAddr);
  }
}

/// Plan the emission of a call result as a single outer direct result.
void
ResultPlanner::planIntoDirectResult(AbstractionPattern innerOrigType,
                                    CanType innerSubstType,
                                    AbstractionPattern outerOrigType,
                                    CanType outerSubstType,
                                    PlanData &planData,
                                    SILResultInfo outerResult) {
  assert(!outerOrigType.isTuple());

  // If the inner pattern is a tuple, expand it.
  if (innerOrigType.isTuple()) {
    planTupleIntoDirectResult(innerOrigType, cast<TupleType>(innerSubstType),
                              outerOrigType, outerSubstType,
                              planData, outerResult);

  // Otherwise, it's scalar.
  } else {
    // Claim the next inner result.
    SILResultInfo innerResult = claimNextInnerResult(planData);

    planScalarIntoDirectResult(innerOrigType, innerSubstType,
                               outerOrigType, outerSubstType,
                               planData, innerResult, outerResult);
  }
}

/// Plan the emission of a call result as a single outer direct result,
/// given that the inner abstraction pattern is a tuple.
void
ResultPlanner::planTupleIntoDirectResult(AbstractionPattern innerOrigType,
                                         CanTupleType innerSubstType,
                                         AbstractionPattern outerOrigType,
                                         CanType outerSubstType,
                                         PlanData &planData,
                                         SILResultInfo outerResult) {
  assert(innerOrigType.isTuple());
  assert(!outerOrigType.isTuple());

  CanTupleType outerSubstTupleType = dyn_cast<TupleType>(outerSubstType);

  // If the outer type is not a tuple, it must be optional.
  if (!outerSubstTupleType) {
    OptionalTypeKind optKind;
    CanType outerSubstObjectType =
      outerSubstType.getAnyOptionalObjectType(optKind);
    assert(outerSubstObjectType &&
           "inner type was a tuple but outer type was neither a tuple nor "
           "optional");

    auto someDecl = Gen.getASTContext().getOptionalSomeDecl(optKind);
    SILType outerObjectType =
      outerResult.getSILType().getAnyOptionalObjectType(Gen.SGM.M, optKind);
    SILResultInfo outerObjectResult(outerObjectType.getSwiftRValueType(),
                                    outerResult.getConvention());

    // Plan to leave the tuple elements as a single direct outer result.
    planTupleIntoDirectResult(innerOrigType, innerSubstType,
                              outerOrigType.getAnyOptionalObjectType(),
                              outerSubstObjectType,
                              planData, outerObjectResult);

    // Take that result and inject it into an optional.
    addInjectOptionalDirect(someDecl, outerResult);
    return;
  }

  // Otherwise, the outer type is a tuple.
  assert(innerSubstType->getNumElements()
           == outerSubstTupleType->getNumElements());

  // Create direct outer results for each of the elements.
  for (auto eltIndex : indices(innerSubstType.getElementTypes())) {
    auto outerEltType = outerResult.getSILType().getTupleElementType(eltIndex);
    SILResultInfo outerEltResult(outerEltType.getSwiftRValueType(),
                                 outerResult.getConvention());

    planIntoDirectResult(innerOrigType.getTupleElementType(eltIndex),
                         innerSubstType.getElementType(eltIndex),
                         outerOrigType.getTupleElementType(eltIndex),
                         outerSubstTupleType.getElementType(eltIndex),
                         planData, outerEltResult);
  }

  // Bind them together into a single tuple.
  addTupleDirect(innerSubstType->getNumElements(), outerResult);
}

/// Plan the emission of a call result as a single outer direct result,
/// given that the inner abstraction pattern is not a tuple.
void ResultPlanner::planScalarIntoDirectResult(AbstractionPattern innerOrigType,
                                               CanType innerSubstType,
                                               AbstractionPattern outerOrigType,
                                               CanType outerSubstType,
                                               PlanData &planData,
                                               SILResultInfo innerResult,
                                               SILResultInfo outerResult) {
  assert(!innerOrigType.isTuple());
  assert(!outerOrigType.isTuple());

  // If the inner result is indirect, plan to emit from that.
  if (innerResult.isIndirect()) {
    SILValue innerResultAddr =
      addInnerIndirectResultTemporary(planData, innerResult);
    planScalarFromIndirectResult(innerOrigType, innerSubstType,
                                 outerOrigType, outerSubstType,
                                 innerResultAddr, outerResult, SILValue());
    return;
  }

  // Otherwise, we have two direct results.

  // If there's no abstraction difference, it's just returned directly.
  if (innerResult.getSILType() == outerResult.getSILType()) {
    addDirectToDirect(innerResult, outerResult);

  // Otherwise, we need to reabstract.
  } else {
    addReabstractDirectToDirect(innerOrigType, innerSubstType,
                                outerOrigType, outerSubstType,
                                innerResult, outerResult);
  }
}

/// Plan the emission of a call result into an outer result address,
/// given that the inner abstraction pattern is not a tuple.
void
ResultPlanner::planScalarIntoIndirectResult(AbstractionPattern innerOrigType,
                                            CanType innerSubstType,
                                            AbstractionPattern outerOrigType,
                                            CanType outerSubstType,
                                            PlanData &planData,
                                            SILResultInfo innerResult,
                                            SILValue outerResultAddr) {
  assert(!innerOrigType.isTuple());
  assert(!outerOrigType.isTuple());

  bool hasAbstractionDifference =
    (innerResult.getType() != outerResultAddr->getType().getSwiftRValueType());

  // If the inner result is indirect, we need some memory to emit it into.
  if (innerResult.isIndirect()) {
    // If there's no abstraction difference, that can just be
    // in-place into the outer result address.
    if (!hasAbstractionDifference) {
      addInPlace(planData, outerResultAddr);

    // Otherwise, we'll need a temporary.
    } else {
      SILValue innerResultAddr =
        addInnerIndirectResultTemporary(planData, innerResult);
      addReabstractIndirectToIndirect(innerOrigType, innerSubstType,
                                      outerOrigType, outerSubstType,
                                      innerResultAddr, outerResultAddr);
    }

  // Otherwise, the inner result is direct.
  } else {
    // If there's no abstraction difference, we just need to store.
    if (!hasAbstractionDifference) {
      addDirectToIndirect(innerResult, outerResultAddr);

    // Otherwise, we need to reabstract and store.
    } else {
      addReabstractDirectToIndirect(innerOrigType, innerSubstType,
                                    outerOrigType, outerSubstType,
                                    innerResult, outerResultAddr);      
    }
  }
}

/// Plan the emission of a call result from an inner result address.
void ResultPlanner::planFromIndirectResult(AbstractionPattern innerOrigType,
                                           CanType innerSubstType,
                                           AbstractionPattern outerOrigType,
                                           CanType outerSubstType,
                                           PlanData &planData,
                                           SILValue innerResultAddr) {
  assert(!innerOrigType.isTuple());

  if (outerOrigType.isTuple()) {
    planTupleFromIndirectResult(innerOrigType, cast<TupleType>(innerSubstType),
                                outerOrigType, cast<TupleType>(outerSubstType),
                                planData, innerResultAddr);
  } else {
    auto outerResult = claimNextOuterResult(planData);
    planScalarFromIndirectResult(innerOrigType, innerSubstType,
                                 outerOrigType, outerSubstType,
                                 innerResultAddr,
                                 outerResult.first, outerResult.second);
  }
}

/// Plan the emission of a call result from an inner result address, given
/// that the outer abstraction pattern is a tuple.
void
ResultPlanner::planTupleFromIndirectResult(AbstractionPattern innerOrigType,
                                           CanTupleType innerSubstType,
                                           AbstractionPattern outerOrigType,
                                           CanTupleType outerSubstType,
                                           PlanData &planData,
                                           SILValue innerResultAddr) {
  assert(!innerOrigType.isTuple());
  assert(innerSubstType->getNumElements() == outerSubstType->getNumElements());
  assert(outerOrigType.isTuple());

  for (auto eltIndex : indices(innerSubstType.getElementTypes())) {
    // Project the address of the element.
    SILValue innerEltResultAddr =
      Gen.B.createTupleElementAddr(Loc, innerResultAddr, eltIndex);

    // Plan to expand from that location.
    planFromIndirectResult(innerOrigType.getTupleElementType(eltIndex),
                           innerSubstType.getElementType(eltIndex),
                           outerOrigType.getTupleElementType(eltIndex),
                           outerSubstType.getElementType(eltIndex),
                           planData, innerEltResultAddr);
  }
}

/// Plan the emission of a call result from an inner result address,
/// given that the outer abstraction pattern is not a tuple.
void
ResultPlanner::planScalarFromIndirectResult(AbstractionPattern innerOrigType,
                                            CanType innerSubstType,
                                            AbstractionPattern outerOrigType,
                                            CanType outerSubstType,
                                            SILValue innerResultAddr,
                                            SILResultInfo outerResult,
                                            SILValue optOuterResultAddr) {
  assert(!innerOrigType.isTuple());
  assert(!outerOrigType.isTuple());
  assert(outerResult.isIndirect() == bool(optOuterResultAddr));

  bool hasAbstractionDifference =
    (innerResultAddr->getType().getSwiftRValueType() != outerResult.getType());

  // The outer result can be indirect, and it doesn't necessarily have an
  // abstraction difference.  Note that we should only end up in this path
  // in cases where simply forwarding the outer result address wasn't possible.

  if (outerResult.isIndirect()) {
    assert(optOuterResultAddr);
    if (!hasAbstractionDifference) {
      addIndirectToIndirect(innerResultAddr, optOuterResultAddr);
    } else {
      addReabstractIndirectToIndirect(innerOrigType, innerSubstType,
                                      outerOrigType, outerSubstType,
                                      innerResultAddr, optOuterResultAddr);
    }
  } else {
    if (!hasAbstractionDifference) {
      addIndirectToDirect(innerResultAddr, outerResult);
    } else {
      addReabstractIndirectToDirect(innerOrigType, innerSubstType,
                                    outerOrigType, outerSubstType,
                                    innerResultAddr, outerResult);
    }
  }
}

SILValue ResultPlanner::execute(SILValue innerResult) {
  // The code emission here assumes that we don't need to have
  // active cleanups for all the result values we're not actively
  // transforming.  In other words, it's not "exception-safe".

  // Explode the inner direct results.
  SmallVector<SILValue, 4> innerDirectResults;
  auto innerResultTupleType = innerResult->getType().getAs<TupleType>();
  if (!innerResultTupleType) {
    innerDirectResults.push_back(innerResult);
  } else {
    for (auto eltIndex : indices(innerResultTupleType.getElementTypes())) {
      auto elt = Gen.B.createTupleExtract(Loc, innerResult, eltIndex);
      innerDirectResults.push_back(elt);
    }
  }

  // Translate the result values.
  SmallVector<SILValue, 4> outerDirectResults;
  execute(innerDirectResults, outerDirectResults);

  // Implode the outer direct results.
  SILValue outerResult;
  if (outerDirectResults.size() == 1) {
    outerResult = outerDirectResults[0];
  } else {
    outerResult = Gen.B.createTuple(Loc, outerDirectResults);
  }

  return outerResult;
}

void ResultPlanner::execute(ArrayRef<SILValue> innerDirectResults,
                            SmallVectorImpl<SILValue> &outerDirectResults) {
  // A helper function to claim an inner direct result.
  auto claimNextInnerDirectResult = [&](SILResultInfo result) -> ManagedValue {
    auto resultValue = claimNext(innerDirectResults);
    assert(resultValue->getType() == result.getSILType());
    auto &resultTL = Gen.getTypeLowering(result.getSILType());
    switch (result.getConvention()) {
    case ResultConvention::Indirect:
      llvm_unreachable("claiming indirect result as direct!");
    case ResultConvention::Owned:
    case ResultConvention::Autoreleased:
      return Gen.emitManagedRValueWithCleanup(resultValue, resultTL);
    case ResultConvention::UnownedInnerPointer:
      // FIXME: We can't reasonably lifetime-extend an inner-pointer result
      // through a thunk. We don't know which parameter to the thunk was
      // originally 'self'.
      Gen.SGM.diagnose(Loc.getSourceLoc(), diag::not_implemented,
                       "reabstraction of returns_inner_pointer function");
      SWIFT_FALLTHROUGH;
    case ResultConvention::Unowned:
      resultTL.emitRetainValue(Gen.B, Loc, resultValue);
      return Gen.emitManagedRValueWithCleanup(resultValue, resultTL);
    }
    llvm_unreachable("bad result convention!");
  };

  // A helper function to add an outer direct result.
  auto addOuterDirectResult = [&](ManagedValue resultValue,
                                  SILResultInfo result) {
    assert(resultValue.getType() ==
           Gen.F.mapTypeIntoContext(result.getSILType()));
    outerDirectResults.push_back(resultValue.forward(Gen));
  };

  auto emitReabstract =
      [&](Operation &op, bool innerIsIndirect, bool outerIsIndirect) {
    // Set up the inner result.
    ManagedValue innerResult;
    if (innerIsIndirect) {
      innerResult = Gen.emitManagedBufferWithCleanup(op.InnerResultAddr);
    } else {
      innerResult = claimNextInnerDirectResult(op.InnerResult);
    }

    // Set up the context into which to emit the outer result.
    SGFContext outerResultCtxt;
    Optional<TemporaryInitialization> outerResultInit;
    if (outerIsIndirect) {
      outerResultInit.emplace(op.OuterResultAddr, CleanupHandle::invalid());
      outerResultCtxt = SGFContext(&*outerResultInit);
    }

    // Perform the translation.
    auto translated =
      Gen.emitTransformedValue(Loc, innerResult,
                               op.InnerOrigType, op.InnerSubstType,
                               op.OuterOrigType, op.OuterSubstType,
                               outerResultCtxt);

    // If the outer is indirect, force it into the context.
    if (outerIsIndirect) {
      if (!translated.isInContext()) {
        translated.forwardInto(Gen, Loc, op.OuterResultAddr);
      }

    // Otherwise, it's a direct result.
    } else {
      addOuterDirectResult(translated, op.OuterResult);
    }
  };

  // Execute each operation.
  for (auto &op : Operations) {
    switch (op.TheKind) {
    case Operation::DirectToDirect: {
      auto result = claimNextInnerDirectResult(op.InnerResult);
      addOuterDirectResult(result, op.OuterResult);
      continue;
    }

    case Operation::DirectToIndirect: {
      auto result = claimNextInnerDirectResult(op.InnerResult);
      Gen.B.createStore(Loc, result.forward(Gen), op.OuterResultAddr);
      continue;
    }

    case Operation::IndirectToDirect: {
      auto resultAddr = op.InnerResultAddr;
      auto &resultTL = Gen.getTypeLowering(resultAddr->getType());
      auto result = Gen.emitManagedRValueWithCleanup(
                                 Gen.B.createLoad(Loc, resultAddr), resultTL);
      addOuterDirectResult(result, op.OuterResult);
      continue;
    }

    case Operation::IndirectToIndirect: {
      // The type could be address-only; just take.
      Gen.B.createCopyAddr(Loc, op.InnerResultAddr, op.OuterResultAddr,
                           IsTake, IsInitialization);
      continue;
    }

    case Operation::ReabstractIndirectToIndirect:
      emitReabstract(op, /*indirect source*/ true, /*indirect dest*/ true);
      continue;
    case Operation::ReabstractIndirectToDirect:
      emitReabstract(op, /*indirect source*/ true, /*indirect dest*/ false);
      continue;
    case Operation::ReabstractDirectToIndirect:
      emitReabstract(op, /*indirect source*/ false, /*indirect dest*/ true);
      continue;
    case Operation::ReabstractDirectToDirect:
      emitReabstract(op, /*indirect source*/ false, /*indirect dest*/ false);
      continue;

    case Operation::TupleDirect: {
      auto firstEltIndex = outerDirectResults.size() - op.NumElements;
      auto elts = makeArrayRef(outerDirectResults).slice(firstEltIndex);
      auto tupleType = Gen.F.mapTypeIntoContext(op.OuterResult.getSILType());
      auto tuple = Gen.B.createTuple(Loc, tupleType, elts);
      outerDirectResults.resize(firstEltIndex);
      outerDirectResults.push_back(tuple);
      continue;
    }

    case Operation::InjectOptionalDirect: {
      SILValue value = outerDirectResults.pop_back_val();
      auto tupleType = Gen.F.mapTypeIntoContext(op.OuterResult.getSILType());
      SILValue optValue = Gen.B.createEnum(Loc, value, op.SomeDecl, tupleType);
      outerDirectResults.push_back(optValue);
      continue;
    }

    case Operation::InjectOptionalIndirect:
      Gen.B.createInjectEnumAddr(Loc, op.OuterResultAddr, op.SomeDecl);
      continue;
    }
    llvm_unreachable("bad operation kind");
  }

  assert(innerDirectResults.empty() && "didn't consume all inner results?");
}

/// Build the body of a transformation thunk.
///
/// \param inputOrigType Abstraction pattern of function value being thunked
/// \param inputSubstType Formal AST type of function value being thunked
/// \param outputOrigType Abstraction pattern of the thunk
/// \param outputSubstType Formal AST type of the thunk
static void buildThunkBody(SILGenFunction &gen, SILLocation loc,
                           AbstractionPattern inputOrigType,
                           CanAnyFunctionType inputSubstType,
                           AbstractionPattern outputOrigType,
                           CanAnyFunctionType outputSubstType) {
  PrettyStackTraceSILFunction stackTrace("emitting reabstraction thunk in",
                                         &gen.F);
  auto thunkType = gen.F.getLoweredFunctionType();

  FullExpr scope(gen.Cleanups, CleanupLocation::get(loc));

  SmallVector<ManagedValue, 8> params;
  // TODO: Could accept +0 arguments here when forwardFunctionArguments/
  // emitApply can.
  gen.collectThunkParams(loc, params, /*allowPlusZero*/ false);

  ManagedValue fnValue = params.pop_back_val();
  auto fnType = fnValue.getType().castTo<SILFunctionType>();
  assert(!fnType->isPolymorphic());
  auto argTypes = fnType->getParameters();

  // Translate the argument values.  Function parameters are
  // contravariant: we want to switch the direction of transformation
  // on them by flipping inputOrigType and outputOrigType.
  //
  // For example, a transformation of (Int,Int)->Int to (T,T)->T is
  // one that should take an (Int,Int)->Int value and make it be
  // abstracted like a (T,T)->T value.  This must be done with a thunk.
  // Within the thunk body, the result of calling the inner function
  // needs to be translated from Int to T (we receive a normal Int
  // and return it like a T), but the parameters are translated in the
  // other direction (the thunk receives an Int like a T, and passes it
  // like a normal Int when calling the inner function).
  SmallVector<ManagedValue, 8> args;
  TranslateArguments(gen, loc, params, args, argTypes)
    .translate(outputOrigType.getFunctionInputType(),
               outputSubstType.getInput(),
               inputOrigType.getFunctionInputType(),
               inputSubstType.getInput());

  SmallVector<SILValue, 8> argValues;

  // Plan the results.  This builds argument values for all the
  // inner indirect results.
  ResultPlanner resultPlanner(gen, loc);
  resultPlanner.plan(inputOrigType.getFunctionResultType(),
                     inputSubstType.getResult(),
                     outputOrigType.getFunctionResultType(),
                     outputSubstType.getResult(),
                     fnType, thunkType, argValues);

  // Add the rest of the arguments.
  forwardFunctionArguments(gen, loc, fnType, args, argValues);

  SILValue innerResult =
    gen.emitApplyWithRethrow(loc, fnValue.forward(gen),
                             /*substFnType*/ fnValue.getType(),
                             /*substitutions*/ {},
                             argValues);

  // Reabstract the result.
  SILValue outerResult = resultPlanner.execute(innerResult);

  scope.pop();
  gen.B.createReturn(loc, outerResult);
}

/// Build the type of a function transformation thunk.
CanSILFunctionType SILGenFunction::buildThunkType(
                                         ManagedValue fn,
                                         CanSILFunctionType expectedType,
                                         CanSILFunctionType &substFnType,
                                         SmallVectorImpl<Substitution> &subs) {
  auto sourceType = fn.getType().castTo<SILFunctionType>();

  assert(!expectedType->isPolymorphic());
  assert(!sourceType->isPolymorphic());
  // Can't build a thunk without context, so we require ownership semantics
  // on the result type.
  assert(expectedType->getExtInfo().hasContext());

  // Just use the generic signature from the context.
  // This isn't necessarily optimal.
  auto genericSig = F.getLoweredFunctionType()->getGenericSignature();
  auto subsArray = F.getForwardingSubstitutions();
  subs.append(subsArray.begin(), subsArray.end());

  // Add the function type as the parameter.
  SmallVector<SILParameterInfo, 4> params;
  params.append(expectedType->getParameters().begin(),
                expectedType->getParameters().end());
  params.push_back({sourceType,
                    sourceType->getExtInfo().hasContext()
                      ? DefaultThickCalleeConvention
                      : ParameterConvention::Direct_Unowned});
  
  auto extInfo = expectedType->getExtInfo()
    .withRepresentation(SILFunctionType::Representation::Thin);
  
  // Map the parameter and expected types out of context to get the interface
  // type of the thunk.
  SmallVector<SILParameterInfo, 4> interfaceParams;
  interfaceParams.reserve(params.size());
  for (auto &param : params) {
    interfaceParams.push_back(
      SILParameterInfo(
          F.mapTypeOutOfContext(param.getType())
              ->getCanonicalType(),
          param.getConvention()));
  }

  SmallVector<SILResultInfo, 4> interfaceResults;
  for (auto &result : expectedType->getAllResults()) {
    auto interfaceResult = result.getWithType(
        F.mapTypeOutOfContext(result.getType())
            ->getCanonicalType());
    interfaceResults.push_back(interfaceResult);
  }

  Optional<SILResultInfo> interfaceErrorResult;
  if (expectedType->hasErrorResult()) {
    interfaceErrorResult = SILResultInfo(
      F.mapTypeOutOfContext(expectedType->getErrorResult().getType())
          ->getCanonicalType(),
      expectedType->getErrorResult().getConvention());
  }
  
  // The type of the thunk function.
  auto thunkType = SILFunctionType::get(genericSig, extInfo,
                                        ParameterConvention::Direct_Unowned,
                                        interfaceParams, interfaceResults,
                                        interfaceErrorResult,
                                        getASTContext());

  // Define the substituted function type for partial_apply's purposes.
  if (!genericSig) {
    substFnType = thunkType;
  } else {
    substFnType = SILFunctionType::get(nullptr, extInfo,
                                       ParameterConvention::Direct_Unowned,
                                       params,
                                       expectedType->getAllResults(),
                                       expectedType->getOptionalErrorResult(),
                                       getASTContext());
  }

  return thunkType;
}

/// Create a reabstraction thunk.
static ManagedValue createThunk(SILGenFunction &gen,
                                SILLocation loc,
                                ManagedValue fn,
                                AbstractionPattern inputOrigType,
                                CanAnyFunctionType inputSubstType,
                                AbstractionPattern outputOrigType,
                                CanAnyFunctionType outputSubstType,
                                const TypeLowering &expectedTL) {
  auto expectedType = expectedTL.getLoweredType().castTo<SILFunctionType>();

  // We can't do bridging here.
  assert(expectedType->getLanguage() ==
         fn.getType().castTo<SILFunctionType>()->getLanguage() &&
         "bridging in re-abstraction thunk?");

  // Declare the thunk.
  SmallVector<Substitution, 4> substitutions;
  CanSILFunctionType substFnType;
  auto thunkType = gen.buildThunkType(fn, expectedType,
                                  substFnType, substitutions);
  auto thunk = gen.SGM.getOrCreateReabstractionThunk(
                                       gen.F.getContextGenericParams(),
                                       thunkType,
                                       fn.getType().castTo<SILFunctionType>(),
                                       expectedType,
                                       gen.F.isFragile());

  // Build it if necessary.
  if (thunk->empty()) {
    // Borrow the context archetypes from the enclosing function.
    thunk->setContextGenericParams(gen.F.getContextGenericParams());
    SILGenFunction thunkSGF(gen.SGM, *thunk);
    auto loc = RegularLocation::getAutoGeneratedLocation();
    buildThunkBody(thunkSGF, loc,
                   inputOrigType, inputSubstType,
                   outputOrigType, outputSubstType);
  }

  // Create it in our current function.
  auto thunkValue = gen.B.createFunctionRef(loc, thunk);
  auto thunkedFn = gen.B.createPartialApply(loc, thunkValue,
                              SILType::getPrimitiveObjectType(substFnType),
                                            substitutions, fn.forward(gen),
                              SILType::getPrimitiveObjectType(expectedType));
  return gen.emitManagedRValueWithCleanup(thunkedFn, expectedTL);
}

ManagedValue Transform::transformFunction(ManagedValue fn,
                                          AbstractionPattern inputOrigType,
                                          CanAnyFunctionType inputSubstType,
                                          AbstractionPattern outputOrigType,
                                          CanAnyFunctionType outputSubstType,
                                          const TypeLowering &expectedTL) {
  assert(fn.getType().isObject() &&
         "expected input to emitTransformedFunctionValue to be loaded");

  auto expectedFnType = expectedTL.getLoweredType().castTo<SILFunctionType>();

  auto fnType = fn.getType().castTo<SILFunctionType>();
  assert(expectedFnType->getExtInfo().hasContext()
         || !fnType->getExtInfo().hasContext());

  // If there's no abstraction difference, we're done.
  if (fnType == expectedFnType) {
    return fn;
  }

  // Check if we require a re-abstraction thunk.
  if (SGF.SGM.Types.checkForABIDifferences(fnType, expectedFnType) ==
        TypeConverter::ABIDifference::NeedsThunk) {
    assert(expectedFnType->getExtInfo().hasContext()
           && "conversion thunk will not be thin!");
    return createThunk(SGF, Loc, fn,
                       inputOrigType, inputSubstType,
                       outputOrigType, outputSubstType,
                       expectedTL);
  }

  // We do not, conversion is trivial.
  auto expectedEI = expectedFnType->getExtInfo();
  auto newEI = expectedEI.withRepresentation(fnType->getRepresentation());
  auto newFnType = adjustFunctionType(expectedFnType, newEI,
                                      fnType->getCalleeConvention());
  // Apply any ABI-compatible conversions before doing thin-to-thick.
  if (fnType != newFnType) {
    SILType resTy = SILType::getPrimitiveObjectType(newFnType);
    fn = ManagedValue(
        SGF.B.createConvertFunction(Loc, fn.getValue(), resTy),
        fn.getCleanup());
  }

  // Now do thin-to-thick if necessary.
  if (newFnType != expectedFnType) {
    assert(expectedEI.getRepresentation() ==
           SILFunctionTypeRepresentation::Thick &&
           "all other conversions should have been handled by "
           "FunctionConversionExpr");
    SILType resTy = SILType::getPrimitiveObjectType(expectedFnType);
    fn = SGF.emitManagedRValueWithCleanup(
        SGF.B.createThinToThickFunction(Loc, fn.forward(SGF), resTy));
  }

  return fn;
}

/// Given a value with the abstraction patterns of the original formal
/// type, give it the abstraction patterns of the substituted formal type.
ManagedValue
SILGenFunction::emitOrigToSubstValue(SILLocation loc, ManagedValue v,
                                     AbstractionPattern origType,
                                     CanType substType,
                                     SGFContext ctxt) {
  
  return emitTransformedValue(loc, v,
                              origType, substType,
                              AbstractionPattern(substType), substType,
                              ctxt);
}

/// Given a value with the abstraction patterns of the original formal
/// type, give it the abstraction patterns of the substituted formal type.
RValue SILGenFunction::emitOrigToSubstValue(SILLocation loc, RValue &&v,
                                            AbstractionPattern origType,
                                            CanType substType,
                                            SGFContext ctxt) {
  return emitTransformedValue(loc, std::move(v),
                              origType, substType,
                              AbstractionPattern(substType), substType,
                              ctxt);
}

/// Given a value with the abstraction patterns of the substituted
/// formal type, give it the abstraction patterns of the original
/// formal type.
ManagedValue
SILGenFunction::emitSubstToOrigValue(SILLocation loc, ManagedValue v,
                                     AbstractionPattern origType,
                                     CanType substType,
                                     SGFContext ctxt) {
  return emitTransformedValue(loc, v,
                              AbstractionPattern(substType), substType,
                              origType, substType,
                              ctxt);
}

/// Given a value with the abstraction patterns of the substituted
/// formal type, give it the abstraction patterns of the original
/// formal type.
RValue SILGenFunction::emitSubstToOrigValue(SILLocation loc, RValue &&v,
                                            AbstractionPattern origType,
                                            CanType substType,
                                            SGFContext ctxt) {
  return emitTransformedValue(loc, std::move(v),
                              AbstractionPattern(substType), substType,
                              origType, substType,
                              ctxt);
}

ManagedValue
SILGenFunction::emitMaterializedRValueAsOrig(Expr *expr,
                                             AbstractionPattern origType) {  
  // Create a temporary.
  auto &origTL = getTypeLowering(origType, expr->getType());
  auto temporary = emitTemporary(expr, origTL);

  // Emit the reabstracted r-value.
  auto result =
    emitRValueAsOrig(expr, origType, origTL, SGFContext(temporary.get()));

  // Force the result into the temporary.
  if (!result.isInContext()) {
    temporary->copyOrInitValueInto(*this, expr, result, /*init*/ true);
    temporary->finishInitialization(*this);
  }

  return temporary->getManagedAddress();
}

ManagedValue
SILGenFunction::emitRValueAsOrig(Expr *expr, AbstractionPattern origPattern,
                                 const TypeLowering &origTL, SGFContext ctxt) {
  auto outputSubstType = expr->getType()->getCanonicalType();
  auto &substTL = getTypeLowering(outputSubstType);
  if (substTL.getLoweredType() == origTL.getLoweredType())
    return emitRValueAsSingleValue(expr, ctxt);

  ManagedValue temp = emitRValueAsSingleValue(expr);
  return emitSubstToOrigValue(expr, temp, origPattern,
                              outputSubstType, ctxt);
}

ManagedValue
SILGenFunction::emitTransformedValue(SILLocation loc, ManagedValue v,
                                     CanType inputType,
                                     CanType outputType,
                                     SGFContext ctxt) {
  return emitTransformedValue(loc, v,
                              AbstractionPattern(inputType), inputType,
                              AbstractionPattern(outputType), outputType);
}

ManagedValue
SILGenFunction::emitTransformedValue(SILLocation loc, ManagedValue v,
                                     AbstractionPattern inputOrigType,
                                     CanType inputSubstType,
                                     AbstractionPattern outputOrigType,
                                     CanType outputSubstType,
                                     SGFContext ctxt) {
  return Transform(*this, loc).transform(v,
                                         inputOrigType,
                                         inputSubstType,
                                         outputOrigType,
                                         outputSubstType, ctxt);
}

RValue
SILGenFunction::emitTransformedValue(SILLocation loc, RValue &&v,
                                     AbstractionPattern inputOrigType,
                                     CanType inputSubstType,
                                     AbstractionPattern outputOrigType,
                                     CanType outputSubstType,
                                     SGFContext ctxt) {
  return Transform(*this, loc).transform(std::move(v),
                                         inputOrigType,
                                         inputSubstType,
                                         outputOrigType,
                                         outputSubstType, ctxt);
}

//===----------------------------------------------------------------------===//
// vtable thunks
//===----------------------------------------------------------------------===//

void
SILGenFunction::emitVTableThunk(SILDeclRef derived,
                                AbstractionPattern inputOrigType,
                                CanAnyFunctionType inputSubstType,
                                CanAnyFunctionType outputSubstType) {
  auto fd = cast<AbstractFunctionDecl>(derived.getDecl());

  SILLocation loc(fd);
  loc.markAutoGenerated();
  CleanupLocation cleanupLoc(fd);
  cleanupLoc.markAutoGenerated();
  Scope scope(Cleanups, cleanupLoc);

  auto implFn = SGM.getFunction(derived, NotForDefinition);
  auto fTy = implFn->getLoweredFunctionType();
  
  ArrayRef<Substitution> subs;
  if (auto context = fd->getGenericParamsOfContext()) {
    F.setContextGenericParams(context);
    subs = getForwardingSubstitutions();
    fTy = fTy->substGenericArgs(SGM.M, SGM.SwiftModule, subs);

    inputSubstType = cast<FunctionType>(
        cast<GenericFunctionType>(inputSubstType)
            ->substGenericArgs(SGM.SwiftModule, subs)->getCanonicalType());
    outputSubstType = cast<FunctionType>(
        cast<GenericFunctionType>(outputSubstType)
            ->substGenericArgs(SGM.SwiftModule, subs)->getCanonicalType());
  }

  // Emit the indirect return and arguments.
  auto thunkTy = F.getLoweredFunctionType();

  SmallVector<ManagedValue, 8> thunkArgs;
  collectThunkParams(loc, thunkArgs, /*allowPlusZero*/ true);

  SmallVector<ManagedValue, 8> substArgs;

  AbstractionPattern outputOrigType(outputSubstType);

  // Reabstract the arguments.
  TranslateArguments(*this, loc, thunkArgs, substArgs, fTy->getParameters())
    .translate(inputOrigType.getFunctionInputType(),
               inputSubstType.getInput(),
               outputOrigType.getFunctionInputType(),
               outputSubstType.getInput());
  
  // Collect the arguments to the implementation.
  SmallVector<SILValue, 8> args;

  // First, indirect results.
  ResultPlanner resultPlanner(*this, loc);
  resultPlanner.plan(outputOrigType.getFunctionResultType(),
                     outputSubstType.getResult(),
                     inputOrigType.getFunctionResultType(),
                     inputSubstType.getResult(),
                     fTy, thunkTy, args);

  // Then, the arguments.
  forwardFunctionArguments(*this, loc, fTy, substArgs, args);

  // Create the call.
  auto implRef = B.createFunctionRef(loc, implFn);
  SILValue implResult = emitApplyWithRethrow(loc, implRef,
                                SILType::getPrimitiveObjectType(fTy),
                                subs, args);

  // Reabstract the return.
  SILValue result = resultPlanner.execute(implResult);
  
  scope.pop();
  B.createReturn(loc, result);
}

//===----------------------------------------------------------------------===//
// Protocol witnesses
//===----------------------------------------------------------------------===//

enum class WitnessDispatchKind {
  Static,
  Dynamic,
  Class
};

static WitnessDispatchKind
getWitnessDispatchKind(Type selfType, SILDeclRef witness, bool isFree) {
  // Free functions are always statically dispatched...
  if (isFree)
    return WitnessDispatchKind::Static;

  // If we have a non-class, non-objc method or a class, objc method that is
  // final, we do not dynamic dispatch.
  ClassDecl *C = selfType->getClassOrBoundGenericClass();
  if (!C)
    return WitnessDispatchKind::Static;

  auto *decl = witness.getDecl();

  // If the witness is dynamic, go through dynamic dispatch.
  if (decl->getAttrs().hasAttribute<DynamicAttr>())
    return WitnessDispatchKind::Dynamic;

  bool isFinal = (decl->isFinal() || C->isFinal());
  if (auto fnDecl = dyn_cast<AbstractFunctionDecl>(witness.getDecl()))
    isFinal |= fnDecl->hasForcedStaticDispatch();

  bool isExtension = isa<ExtensionDecl>(decl->getDeclContext());

  // If we have a final method or a method from an extension that is not
  // Objective-C, emit a static reference.
  // A natively ObjC method witness referenced this way will end up going
  // through its native thunk, which will redispatch the method after doing
  // bridging just like we want.
  if (isFinal || isExtension || witness.isForeignToNativeThunk()
      // Hack--We emit a static thunk for ObjC allocating constructors.
      || (decl->hasClangNode() && witness.kind == SILDeclRef::Kind::Allocator))
    return WitnessDispatchKind::Static;

  // Otherwise emit a class method.
  return WitnessDispatchKind::Class;
}

static CanSILFunctionType
getWitnessFunctionType(SILGenModule &SGM,
                       SILDeclRef witness,
                       WitnessDispatchKind witnessKind) {
  switch (witnessKind) {
  case WitnessDispatchKind::Static:
  case WitnessDispatchKind::Dynamic:
    return SGM.Types.getConstantInfo(witness).SILFnType;
  case WitnessDispatchKind::Class:
    return SGM.Types.getConstantOverrideType(witness);
  }
}

static SILValue
getWitnessFunctionRef(SILGenFunction &gen,
                      SILDeclRef witness,
                      WitnessDispatchKind witnessKind,
                      SmallVectorImpl<ManagedValue> &witnessParams,
                      SILLocation loc) {
  SILGenModule &SGM = gen.SGM;

  switch (witnessKind) {
  case WitnessDispatchKind::Static:
    return gen.emitGlobalFunctionRef(loc, witness);
  case WitnessDispatchKind::Dynamic:
    return gen.emitDynamicMethodRef(loc, witness,
                                    SGM.Types.getConstantInfo(witness));
  case WitnessDispatchKind::Class:
    SILValue selfPtr = witnessParams.back().getValue();
    return gen.B.createClassMethod(loc, selfPtr, witness);
  }
}

static CanType dropLastElement(CanType type) {
  auto elts = cast<TupleType>(type)->getElements().drop_back();
  return TupleType::get(elts, type->getASTContext())->getCanonicalType();
}

static void addConformanceToSubstitutionMap(SILGenModule &SGM,
                                TypeSubstitutionMap &subs,
                                GenericParamList *context,
                                CanType base,
                                const ProtocolConformance *conformance) {
  conformance->forEachTypeWitness(nullptr, [&](AssociatedTypeDecl *assocTy,
                                               Substitution sub,
                                               TypeDecl *) -> bool {
    auto depTy =
      CanDependentMemberType::get(base, assocTy, SGM.getASTContext());
    auto replacement = sub.getReplacement()->getCanonicalType();
    replacement = ArchetypeBuilder::mapTypeOutOfContext(SGM.M.getSwiftModule(),
                                                        context,
                                                        replacement)
      ->getCanonicalType();
    subs.insert({depTy.getPointer(), replacement});
    for (auto conformance : sub.getConformances()) {
      if (conformance.isAbstract())
        continue;
      addConformanceToSubstitutionMap(SGM, subs, context,
                                      depTy, conformance.getConcrete());
    }
    return false;
  });
}

/// Substitute the `Self` type from a protocol conformance into a protocol
/// requirement's type to get the type of the witness.
CanAnyFunctionType SILGenModule::
substSelfTypeIntoProtocolRequirementType(CanGenericFunctionType reqtTy,
                                         ProtocolConformance *conformance) {
  // Build a substitution map to replace `self` and its associated types.
  auto &C = M.getASTContext();
  CanType selfParamTy = CanGenericTypeParamType::get(0, 0, C);
  
  TypeSubstitutionMap subs;
  subs.insert({selfParamTy.getPointer(), conformance->getInterfaceType()
                                                    ->getCanonicalType()});
  addConformanceToSubstitutionMap(*this, subs, conformance->getGenericParams(),
                                  selfParamTy, conformance);
  
  // Drop requirements rooted in the applied generic parameters.
  SmallVector<Requirement, 4> unappliedReqts;
  auto rootedInSelf = [&](Type t) -> bool {
    while (auto dmt = t->getAs<DependentMemberType>()) {
      t = dmt->getBase();
    }
    return t->isEqual(selfParamTy);
  };

  #if 0
  llvm::errs() << "--\n";
  for (auto &pair : subs) {
    pair.first->print(llvm::errs());
    llvm::errs() << " => ";
    pair.second->dump();
    llvm::errs() << "\n";
  }
  #endif

  // Get the unapplied params.
  auto unappliedParams = reqtTy->getGenericParams().slice(1);

  // Get the requirements that aren't rooted in the applied 'self' parameter.
  for (auto &reqt : reqtTy->getRequirements()) {
    switch (reqt.getKind()) {
    case RequirementKind::Conformance:
    case RequirementKind::Superclass:
    case RequirementKind::WitnessMarker:
      // Substituting the parameter eliminates conformance constraints rooted
      // in the parameter.
      if (rootedInSelf(reqt.getFirstType()))
        continue;
      break;
        
    case RequirementKind::SameType: {
      // Same-type constraints are eliminated if both sides of the constraint
      // are rooted in substituted parameters.
      if (rootedInSelf(reqt.getFirstType())
          && rootedInSelf(reqt.getSecondType()))
        continue;
        
      // Otherwise, substitute the constrained types.
      unappliedReqts.push_back(
        Requirement(RequirementKind::SameType,
                    reqt.getFirstType().subst(M.getSwiftModule(), subs,
                                              SubstFlags::IgnoreMissing),
                    reqt.getSecondType().subst(M.getSwiftModule(), subs,
                                               SubstFlags::IgnoreMissing)));
      continue;
    }
    }
    unappliedReqts.push_back(reqt);
  }

  auto input = reqtTy->getInput().subst(M.getSwiftModule(), subs,
                                        SubstFlags::IgnoreMissing)
    ->getCanonicalType();
  auto result = reqtTy->getResult().subst(M.getSwiftModule(), subs,
                                          SubstFlags::IgnoreMissing)
    ->getCanonicalType();

  if (!unappliedParams.empty() && !unappliedReqts.empty()) {
    auto sig = GenericSignature::get(unappliedParams,
                                     unappliedReqts)->getCanonicalSignature();
    
    return CanGenericFunctionType::get(sig, input, result, reqtTy->getExtInfo());
  } else {
    return CanFunctionType::get(input, result, reqtTy->getExtInfo());
  }
}

void SILGenFunction::emitProtocolWitness(Type selfType,
                                         AbstractionPattern reqtOrigTy,
                                         CanAnyFunctionType reqtSubstTy,
                                         SILDeclRef requirement,
                                         SILDeclRef witness,
                                         ArrayRef<Substitution> witnessSubs,
                                         IsFreeFunctionWitness_t isFree) {
  // FIXME: Disable checks that the protocol witness carries debug info.
  // Should we carry debug info for witnesses?
  F.setBare(IsBare);

  SILLocation loc(witness.getDecl());
  FullExpr scope(Cleanups, CleanupLocation::get(loc));
 
  auto witnessKind = getWitnessDispatchKind(selfType, witness, isFree);
  auto thunkTy = F.getLoweredFunctionType();

  SmallVector<ManagedValue, 8> origParams;
  // TODO: Should be able to accept +0 values here, once
  // forwardFunctionArguments/emitApply are able to.
  collectThunkParams(loc, origParams, /*allowPlusZero*/ false);

  // Handle special abstraction differences in "self".
  // If the witness is a free function, drop it completely.
  // WAY SPECULATIVE TODO: What if 'self' comprised multiple SIL-level params?
  if (isFree)
    origParams.pop_back();

  // Get the type of the witness.
  auto witnessInfo = getConstantInfo(witness);
  CanAnyFunctionType witnessSubstTy = witnessInfo.LoweredInterfaceType;
  if (!witnessSubs.empty()) {
    witnessSubstTy = cast<FunctionType>(
      cast<GenericFunctionType>(witnessSubstTy)
        ->substGenericArgs(SGM.M.getSwiftModule(), witnessSubs)
        ->getCanonicalType());
  }
  CanType reqtSubstInputTy = F.mapTypeIntoContext(reqtSubstTy.getInput())
      ->getCanonicalType();
  CanType reqtSubstResultTy = F.mapTypeIntoContext(reqtSubstTy.getResult())
      ->getCanonicalType();

  AbstractionPattern reqtOrigInputTy = reqtOrigTy.getFunctionInputType();
  // For a free function witness, discard the 'self' parameter of the
  // requirement.
  if (isFree) {
    reqtOrigInputTy = reqtOrigInputTy.dropLastTupleElement();
    reqtSubstInputTy = dropLastElement(reqtSubstInputTy);
  }

  // Translate the argument values from the requirement abstraction level to
  // the substituted signature of the witness.
  auto witnessFTy = getWitnessFunctionType(SGM, witness, witnessKind);
  if (!witnessSubs.empty())
    witnessFTy = witnessFTy->substGenericArgs(SGM.M, SGM.M.getSwiftModule(),
                                              witnessSubs);

  SmallVector<ManagedValue, 8> witnessParams;

  if (!isFree) {
    // If the requirement has a self parameter passed as an indirect +0 value,
    // and the witness takes it as a non-inout value, we must load and retain
    // the self pointer coming in.  This happens when class witnesses implement
    // non-mutating protocol requirements.
    auto reqConvention = thunkTy->getSelfParameter().getConvention();
    auto witnessConvention = witnessFTy->getSelfParameter().getConvention();
    
    bool inoutDifference;
    
    inoutDifference = reqConvention == ParameterConvention::Indirect_Inout &&
                    witnessConvention != ParameterConvention::Indirect_Inout;

    if (inoutDifference) {
      // If there is an inout difference in self, load the inout self parameter.
      ManagedValue &selfParam = origParams.back();
      SILValue selfAddr = selfParam.getUnmanagedValue();
      selfParam = emitLoad(loc, selfAddr,
                           getTypeLowering(selfType),
                           SGFContext(),
                           IsNotTake);
    }
  }

  AbstractionPattern witnessOrigTy(witnessInfo.LoweredInterfaceType);
  TranslateArguments(*this, loc,
                     origParams, witnessParams,
                     witnessFTy->getParameters())
    .translate(reqtOrigInputTy,
               reqtSubstInputTy,
               witnessOrigTy.getFunctionInputType(),
               witnessSubstTy.getInput());

  SILValue witnessFnRef = getWitnessFunctionRef(*this, witness, witnessKind,
                                                witnessParams, loc);

  // Collect the arguments.
  SmallVector<SILValue, 8> args;

  //   - indirect results
  ResultPlanner resultPlanner(*this, loc);
  resultPlanner.plan(witnessOrigTy.getFunctionResultType(),
                     witnessSubstTy.getResult(),
                     reqtOrigTy.getFunctionResultType(),
                     reqtSubstResultTy,
                     witnessFTy, thunkTy, args);

  //   - the rest of the arguments
  forwardFunctionArguments(*this, loc, witnessFTy, witnessParams, args);
  
  // Perform the call.
  SILType witnessSILTy = SILType::getPrimitiveObjectType(witnessFTy);
  SILValue witnessResultValue =
    emitApplyWithRethrow(loc, witnessFnRef, witnessSILTy, witnessSubs, args);

  // Reabstract the result value.
  SILValue reqtResultValue = resultPlanner.execute(witnessResultValue);

  scope.pop();
  B.createReturn(loc, reqtResultValue);
}
