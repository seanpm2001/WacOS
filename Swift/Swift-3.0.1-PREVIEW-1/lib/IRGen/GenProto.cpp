//===--- GenProto.cpp - Swift IR Generation for Protocols -----------------===//
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
//  This file implements IR generation for protocols in Swift.
//
//  Protocols serve two masters: generic algorithms and existential
//  types.  In either case, the size and structure of a type is opaque
//  to the code manipulating a value.  Local values of the type must
//  be stored in fixed-size buffers (which can overflow to use heap
//  allocation), and basic operations on the type must be dynamically
//  delegated to a collection of information that "witnesses" the
//  truth that a particular type implements the protocol.
//
//  In the comments throughout this file, three type names are used:
//    'B' is the type of a fixed-size buffer
//    'T' is the type which implements a protocol
//    'W' is the type of a witness to the protocol
//
//===----------------------------------------------------------------------===//

#include "swift/AST/ASTContext.h"
#include "swift/AST/CanTypeVisitor.h"
#include "swift/AST/Types.h"
#include "swift/AST/Decl.h"
#include "swift/AST/IRGenOptions.h"
#include "swift/SIL/SILDeclRef.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILValue.h"
#include "swift/SIL/SILWitnessVisitor.h"
#include "swift/SIL/TypeLowering.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

#include "CallEmission.h"
#include "EnumPayload.h"
#include "Explosion.h"
#include "FixedTypeInfo.h"
#include "Fulfillment.h"
#include "GenArchetype.h"
#include "GenClass.h"
#include "GenEnum.h"
#include "GenHeap.h"
#include "GenMeta.h"
#include "GenOpaque.h"
#include "GenPoly.h"
#include "GenType.h"
#include "IRGenDebugInfo.h"
#include "IRGenFunction.h"
#include "IRGenModule.h"
#include "Linking.h"
#include "MetadataPath.h"
#include "NecessaryBindings.h"
#include "ProtocolInfo.h"
#include "TypeInfo.h"

#include "GenProto.h"

using namespace swift;
using namespace irgen;

namespace {

/// A class for computing how to pass arguments to a polymorphic
/// function.  The subclasses of this are the places which need to
/// be updated if the convention changes.
class PolymorphicConvention {
protected:
  IRGenModule &IGM;
  ModuleDecl &M;
  CanSILFunctionType FnType;

  CanGenericSignature Generics;

  std::vector<MetadataSource> Sources;

  FulfillmentMap Fulfillments;

  GenericSignature::ConformsToArray getConformsTo(Type t) {
    return Generics->getConformsTo(t, M);
  }

public:
  PolymorphicConvention(IRGenModule &IGM, CanSILFunctionType fnType);

  ArrayRef<MetadataSource> getSources() const { return Sources; }

  using RequirementCallback =
    llvm::function_ref<void(GenericRequirement requirement)>;

  void enumerateRequirements(const RequirementCallback &callback);

  void enumerateUnfulfilledRequirements(const RequirementCallback &callback);

  /// Returns a Fulfillment for a type parameter requirement, or
  /// nullptr if it's unfulfilled.
  const Fulfillment *getFulfillmentForTypeMetadata(CanType type) const;

  /// Return the source of type metadata at a particular source index.
  const MetadataSource &getSource(size_t SourceIndex) const {
    return Sources[SourceIndex];
  }

private:
  void initGenerics();
  void considerNewTypeSource(MetadataSource::Kind kind, unsigned paramIndex,
                             CanType type, IsExact_t isExact);
  bool considerType(CanType type, IsExact_t isExact,
                    unsigned sourceIndex, MetadataPath &&path);

  /// Testify to generic parameters in the Self type of a protocol
  /// witness method.
  void considerWitnessSelf(CanSILFunctionType fnType);

  /// Testify to generic parameters in the Self type of an @objc
  /// generic or protocol method.
  void considerObjCGenericSelf(CanSILFunctionType fnType);

  void considerParameter(SILParameterInfo param, unsigned paramIndex,
                         bool isSelfParameter);

  void addSelfMetadataFulfillment(CanType arg);
  void addSelfWitnessTableFulfillment(CanType arg, ProtocolDecl *proto);

  void addPseudogenericFulfillments();
};

} // end anonymous namespace

PolymorphicConvention::PolymorphicConvention(IRGenModule &IGM,
                                             CanSILFunctionType fnType)
  : IGM(IGM), M(*IGM.getSwiftModule()), FnType(fnType) {
  initGenerics();

  if (fnType->isPseudogeneric()) {
    addPseudogenericFulfillments();
    return;
  }

  auto rep = fnType->getRepresentation();

  if (rep == SILFunctionTypeRepresentation::WitnessMethod) {
    // Protocol witnesses always derive all polymorphic parameter
    // information from the Self argument. We also *cannot* consider other
    // arguments; doing so would potentially make the signature
    // incompatible with other witnesses for the same method.
    considerWitnessSelf(fnType);
  } else if (rep == SILFunctionTypeRepresentation::ObjCMethod) {
    // Objective-C thunks for generic methods also always derive all
    // polymorphic parameter information from the Self argument.
    considerObjCGenericSelf(fnType);
  } else {
    // We don't need to pass anything extra as long as all of the
    // archetypes (and their requirements) are producible from
    // arguments.
    unsigned selfIndex = ~0U;
    auto params = fnType->getParameters();

    // Consider 'self' first.
    if (fnType->hasSelfParam()) {
      selfIndex = params.size() - 1;
      considerParameter(params[selfIndex], selfIndex, true);
    }

    // Now consider the rest of the parameters.
    for (auto index : indices(params)) {
      if (index != selfIndex)
        considerParameter(params[index], index, false);
    }
  }
}

void PolymorphicConvention::addPseudogenericFulfillments() {
  enumerateRequirements([&](GenericRequirement reqt) {
    MetadataPath path;
    path.addImpossibleComponent();

    unsigned sourceIndex = 0; // unimportant, since impossible
    Fulfillments.addFulfillment({reqt.TypeParameter, reqt.Protocol},
                                sourceIndex, std::move(path));
  });
}

void PolymorphicConvention::enumerateRequirements(const RequirementCallback &callback) {
  if (!Generics) return;

  // Note that the canonical mangling signature will sometimes use
  // different dependent type from Generics, apparently for no good
  // reason.
  auto minimized = Generics->getCanonicalManglingSignature(M);

  // Make a first pass to get all the type metadata.
  for (auto &reqt : minimized->getRequirements()) {
    switch (reqt.getKind()) {
        // Ignore these; they don't introduce extra requirements.
      case RequirementKind::Superclass:
      case RequirementKind::SameType:
      case RequirementKind::Conformance:
        continue;

      case RequirementKind::WitnessMarker: {
        CanType type = CanType(reqt.getFirstType());
        if (isa<GenericTypeParamType>(type))
          callback({type, nullptr});
        continue;
      }
    }
    llvm_unreachable("bad requirement kind");
  }

  // Make a second pass for all the protocol conformances.
  for (auto &reqt : minimized->getRequirements()) {
    switch (reqt.getKind()) {
        // Ignore these; they don't introduce extra requirements.
      case RequirementKind::Superclass:
      case RequirementKind::SameType:
      case RequirementKind::WitnessMarker:
        continue;

      case RequirementKind::Conformance: {
        auto type = CanType(reqt.getFirstType());
        auto protocol =
          cast<ProtocolType>(CanType(reqt.getSecondType()))->getDecl();
        if (Lowering::TypeConverter::protocolRequiresWitnessTable(protocol)) {
          callback({type, protocol});
        }
        continue;
      }
    }
    llvm_unreachable("bad requirement kind");
  }
}

void PolymorphicConvention::enumerateUnfulfilledRequirements(const RequirementCallback &callback) {
  enumerateRequirements([&](GenericRequirement requirement) {
    if (requirement.Protocol) {
      if (!Fulfillments.getWitnessTable(requirement.TypeParameter,
                                        requirement.Protocol)) {
        callback(requirement);
      }
    } else {
      if (!Fulfillments.getTypeMetadata(requirement.TypeParameter)) {
        callback(requirement);
      }
    }
  });
}

void PolymorphicConvention::initGenerics() {
  // The canonical mangling signature removes dependent types that are
  // equal to concrete types, but isn't necessarily parallel with
  // substitutions.
  Generics = FnType->getGenericSignature();
}

void PolymorphicConvention::considerNewTypeSource(MetadataSource::Kind kind,
                                                  unsigned paramIndex,
                                                  CanType type,
                                                  IsExact_t isExact) {
  if (!Fulfillments.isInterestingTypeForFulfillments(type)) return;

  // Prospectively add a source.
  Sources.emplace_back(kind, paramIndex, type);

  // Consider the source.
  if (!considerType(type, isExact, Sources.size() - 1, MetadataPath())) {
    // If it wasn't used in any fulfillments, remove it.
    Sources.pop_back();
  }
}

bool PolymorphicConvention::considerType(CanType type, IsExact_t isExact,
                                         unsigned sourceIndex,
                                         MetadataPath &&path) {
  struct Callback : FulfillmentMap::InterestingKeysCallback {
    PolymorphicConvention &Self;
    Callback(PolymorphicConvention &self) : Self(self) {}

    bool isInterestingType(CanType type) const override {
      return type->isTypeParameter();
    }
    bool hasInterestingType(CanType type) const override {
      return type->hasTypeParameter();
    }
    bool hasLimitedInterestingConformances(CanType type) const override {
      return true;
    }
    GenericSignature::ConformsToArray
    getInterestingConformances(CanType type) const override {
      return Self.getConformsTo(type);
    }
  } callbacks(*this);
  return Fulfillments.searchTypeMetadata(IGM, type, isExact, sourceIndex,
                                         std::move(path), callbacks);
}

void PolymorphicConvention::considerWitnessSelf(CanSILFunctionType fnType) {
  CanType selfTy = fnType->getSelfInstanceType();

  // First, bind type metadata for Self.
  Sources.emplace_back(MetadataSource::Kind::SelfMetadata,
                       MetadataSource::InvalidSourceIndex,
                       selfTy);

  if (auto *proto = fnType->getDefaultWitnessMethodProtocol(M)) {
    // The Self type is abstract, so we must pass in a witness table.
    addSelfMetadataFulfillment(selfTy);

    // Look at the witness table for the conformance.
    Sources.emplace_back(MetadataSource::Kind::SelfWitnessTable,
                         MetadataSource::InvalidSourceIndex,
                         selfTy);
    addSelfWitnessTableFulfillment(selfTy, proto);
  } else {
    // If the Self type is concrete, we have a witness thunk with a
    // fully substituted Self type. The witness table parameter is not
    // used.
    considerType(selfTy, IsInexact, Sources.size() - 1, MetadataPath());
  }
}

void PolymorphicConvention::considerObjCGenericSelf(CanSILFunctionType fnType) {
  // If this is a static method, get the instance type.
  CanType selfTy = fnType->getSelfInstanceType();
  unsigned paramIndex = fnType->getParameters().size() - 1;

  // Bind type metadata for Self.
  Sources.emplace_back(MetadataSource::Kind::ClassPointer, paramIndex,
                       selfTy);

  if (isa<GenericTypeParamType>(selfTy))
    addSelfMetadataFulfillment(selfTy);
  else
    considerType(selfTy, IsInexact,
                 Sources.size() - 1, MetadataPath());
}

void PolymorphicConvention::considerParameter(SILParameterInfo param,
                                              unsigned paramIndex,
                                              bool isSelfParameter) {
  auto type = param.getType();
  switch (param.getConvention()) {
      // Indirect parameters do give us a value we can use, but right now
      // we don't bother, for no good reason. But if this is 'self',
      // consider passing an extra metatype.
    case ParameterConvention::Indirect_In:
    case ParameterConvention::Indirect_In_Guaranteed:
    case ParameterConvention::Indirect_Inout:
    case ParameterConvention::Indirect_InoutAliasable:
      if (!isSelfParameter) return;
      if (type->getNominalOrBoundGenericNominal()) {
        considerNewTypeSource(MetadataSource::Kind::GenericLValueMetadata,
                              paramIndex, type, IsExact);
      }
      return;

    case ParameterConvention::Direct_Owned:
    case ParameterConvention::Direct_Unowned:
    case ParameterConvention::Direct_Guaranteed:
    case ParameterConvention::Direct_Deallocating:
      // Classes are sources of metadata.
      if (type->getClassOrBoundGenericClass()) {
        considerNewTypeSource(MetadataSource::Kind::ClassPointer,
                              paramIndex, type, IsInexact);
        return;
      }

      // Thick metatypes are sources of metadata.
      if (auto metatypeTy = dyn_cast<MetatypeType>(type)) {
        if (metatypeTy->getRepresentation() != MetatypeRepresentation::Thick)
          return;

        // Thick metatypes for Objective-C parameterized classes are not
        // sources of metadata.
        CanType objTy = metatypeTy.getInstanceType();
        if (auto classDecl = objTy->getClassOrBoundGenericClass())
          if (classDecl->usesObjCGenericsModel())
            return;

        considerNewTypeSource(MetadataSource::Kind::Metadata,
                              paramIndex, objTy, IsInexact);
        return;
      }

      return;
  }
  llvm_unreachable("bad parameter convention");
}

void PolymorphicConvention::addSelfMetadataFulfillment(CanType arg) {
  unsigned source = Sources.size() - 1;
  Fulfillments.addFulfillment({arg, nullptr}, source, MetadataPath());
}

void PolymorphicConvention::addSelfWitnessTableFulfillment(CanType arg, ProtocolDecl *proto) {
  unsigned source = Sources.size() - 1;
  Fulfillments.addFulfillment({arg, proto}, source, MetadataPath());
}

const Fulfillment *
PolymorphicConvention::getFulfillmentForTypeMetadata(CanType type) const {
  return Fulfillments.getTypeMetadata(type);
}

void irgen::enumerateGenericParamFulfillments(IRGenModule &IGM,
                                  CanSILFunctionType fnType,
                                  GenericParamFulfillmentCallback callback) {
  PolymorphicConvention convention(IGM, fnType);

  // Check if any requirements were fulfilled by metadata stored inside a
  // captured value.
  auto generics = fnType->getGenericSignature();

  for (auto genericParam : generics->getGenericParams()) {
    auto genericParamType = genericParam->getCanonicalType();

    auto fulfillment
      = convention.getFulfillmentForTypeMetadata(genericParamType);
    if (fulfillment == nullptr)
      continue;

    auto &source = convention.getSource(fulfillment->SourceIndex);
    callback(genericParamType, source, fulfillment->Path);
  }
}

namespace {

/// A class for binding type parameters of a generic function.
class EmitPolymorphicParameters : public PolymorphicConvention {
  IRGenFunction &IGF;
  SILFunction &Fn;

public:
  EmitPolymorphicParameters(IRGenFunction &IGF, SILFunction &Fn);

  void emit(Explosion &in, WitnessMetadata *witnessMetadata,
            const GetParameterFn &getParameter);

private:
  CanType getTypeInContext(CanType type) const;

  CanType getArgTypeInContext(unsigned paramIndex) const;

  /// Fulfill local type data from any extra information associated with
  /// the given source.
  void bindExtraSource(const MetadataSource &source, Explosion &in,
                       WitnessMetadata *witnessMetadata);

  void bindParameterSources(const GetParameterFn &getParameter);

  void bindParameterSource(SILParameterInfo param, unsigned paramIndex,
                           const GetParameterFn &getParameter) ;
  // Did the convention decide that the parameter at the given index
  // was a class-pointer source?
  bool isClassPointerSource(unsigned paramIndex);
};

} // end anonymous namespace

EmitPolymorphicParameters::EmitPolymorphicParameters(IRGenFunction &IGF,
                          SILFunction &Fn)
  : PolymorphicConvention(IGF.IGM, Fn.getLoweredFunctionType()),
    IGF(IGF), Fn(Fn) {}


CanType EmitPolymorphicParameters::getTypeInContext(CanType type) const {
  return Fn.mapTypeIntoContext(type)->getCanonicalType();
}

CanType EmitPolymorphicParameters::getArgTypeInContext(unsigned paramIndex) const {
  return getTypeInContext(FnType->getParameters()[paramIndex].getType());
}

void EmitPolymorphicParameters::bindExtraSource(const MetadataSource &source,
                                                Explosion &in,
                                         WitnessMetadata *witnessMetadata) {
  switch (source.getKind()) {
    case MetadataSource::Kind::Metadata:
    case MetadataSource::Kind::ClassPointer:
      // Ignore these, we'll get to them when we walk the parameter list.
      return;

    case MetadataSource::Kind::GenericLValueMetadata: {
      CanType argTy = getArgTypeInContext(source.getParamIndex());

      llvm::Value *metadata = in.claimNext();
      setTypeMetadataName(IGF.IGM, metadata, argTy);

      IGF.bindLocalTypeDataFromTypeMetadata(argTy, IsExact, metadata);
      return;
    }

    case MetadataSource::Kind::SelfMetadata: {
      assert(witnessMetadata && "no metadata for witness method");
      llvm::Value *metadata = witnessMetadata->SelfMetadata;
      assert(metadata && "no Self metadata for witness method");

      // Mark this as the cached metatype for Self.
      auto selfTy = FnType->getSelfInstanceType();
      CanType argTy = getTypeInContext(selfTy);
      setTypeMetadataName(IGF.IGM, metadata, argTy);
      auto *CD = selfTy.getClassOrBoundGenericClass();
      // The self metadata here corresponds to the conforming type.
      // For an inheritable conformance, that may be a subclass of the static
      // type, and so the self metadata will be inexact. Currently, all
      // conformances are inheritable.
      IGF.bindLocalTypeDataFromTypeMetadata(
          argTy, (!CD || CD->isFinal()) ? IsExact : IsInexact, metadata);
      return;
    }

    case MetadataSource::Kind::SelfWitnessTable: {
      assert(witnessMetadata && "no metadata for witness method");
      llvm::Value *wtable = witnessMetadata->SelfWitnessTable;
      assert(wtable && "no Self witness table for witness method");

      // Mark this as the cached witness table for Self.

      if (auto *proto = FnType->getDefaultWitnessMethodProtocol(M)) {
        auto selfTy = FnType->getSelfInstanceType();
        CanType argTy = getTypeInContext(selfTy);
        auto archetype = cast<ArchetypeType>(argTy);

        setProtocolWitnessTableName(IGF.IGM, wtable, argTy, proto);
        IGF.setUnscopedLocalTypeData(archetype,
                                     LocalTypeDataKind::forAbstractProtocolWitnessTable(proto),
                                     wtable);
      }
      return;
    }
  }
  llvm_unreachable("bad source kind!");
}

void EmitPolymorphicParameters::bindParameterSources(const GetParameterFn &getParameter) {
  auto params = FnType->getParameters();

  // Bind things from 'self' preferentially.
  if (FnType->hasSelfParam()) {
    bindParameterSource(params.back(), params.size() - 1, getParameter);
    params = params.drop_back();
  }

  for (unsigned index : indices(params)) {
    bindParameterSource(params[index], index, getParameter);
  }
}

void EmitPolymorphicParameters::bindParameterSource(SILParameterInfo param, unsigned paramIndex,
                         const GetParameterFn &getParameter) {
  // Ignore indirect parameters for now.  This is potentially dumb.
  if (param.isIndirect()) return;

  CanType paramType = getArgTypeInContext(paramIndex);

  // If the parameter is a thick metatype, bind it directly.
  // TODO: objc metatypes?
  if (auto metatype = dyn_cast<MetatypeType>(paramType)) {
    if (metatype->getRepresentation() == MetatypeRepresentation::Thick) {
      paramType = metatype.getInstanceType();
      llvm::Value *metadata = getParameter(paramIndex);
      IGF.bindLocalTypeDataFromTypeMetadata(paramType, IsInexact, metadata);
    }
    return;
  }

  // If the parameter is a class type, we only consider it interesting
  // if the convention decided it was actually a source.
  // TODO: if the class pointer is guaranteed, we can do this lazily,
  // at which point it might make sense to do it for a wider selection
  // of types.
  if (isClassPointerSource(paramIndex)) {
    llvm::Value *instanceRef = getParameter(paramIndex);
    SILType instanceType = SILType::getPrimitiveObjectType(paramType);
    llvm::Value *metadata =
    emitDynamicTypeOfHeapObject(IGF, instanceRef, instanceType);
    IGF.bindLocalTypeDataFromTypeMetadata(paramType, IsInexact, metadata);
    return;
  }
}

bool EmitPolymorphicParameters::isClassPointerSource(unsigned paramIndex) {
  for (auto &source : getSources()) {
    if (source.getKind() == MetadataSource::Kind::ClassPointer &&
        source.getParamIndex() == paramIndex) {
      return true;
    }
  }
  return false;
}

static bool shouldSetName(IRGenModule &IGM, llvm::Value *value, CanType type) {
  // If value names are globally disabled, honor that.
  if (!IGM.EnableValueNames) return false;

  // Suppress value names for values with opened existentials.
  if (type->hasOpenedExistential()) return false;

  // If the value already has a name, honor that.
  if (value->hasName()) return false;

  // Only do this for local values.
  return (isa<llvm::Instruction>(value) || isa<llvm::Argument>(value));
}

void irgen::setTypeMetadataName(IRGenModule &IGM, llvm::Value *metadata,
                                CanType type) {
  if (!shouldSetName(IGM, metadata, type)) return;

  SmallString<128> name; {
    llvm::raw_svector_ostream out(name);
    type.print(out);
  }
  metadata->setName(type->getString());
}

void irgen::setProtocolWitnessTableName(IRGenModule &IGM, llvm::Value *wtable,
                                        CanType type,
                                        ProtocolDecl *requirement) {
  if (!shouldSetName(IGM, wtable, type)) return;

  SmallString<128> name; {
    llvm::raw_svector_ostream out(name);
    type.print(out);
    out << '.' << requirement->getNameStr();
  }
  wtable->setName(name);
}

/// Return the index of the given dependent type in the list of all
/// dependent types.
///
/// This will be its index in the list of substitutions.
static unsigned getDependentTypeIndex(CanGenericSignature generics,
                                      ModuleDecl &M, CanType type) {
  assert(type->isTypeParameter());

  // Make a pass over all the dependent types.
  unsigned index = 0;
  for (auto depTy : generics->getAllDependentTypes()) {
    // Unfortunately, we can't rely on either depTy or type actually
    // being the marked witness type in the generic signature, so we have
    // to ask the generic signature whether the types are equal.
    if (generics->areSameTypeParameterInContext(depTy, type, M))
      return index;
    index++;
  }

  llvm_unreachable("didn't find dependent type in all-dependent-types list");
}

/// Return the index of the given protocol conformance in the list of all
/// protocol conformances for the given dependent type in the given signature.
///
/// This will be its index in the list of protocol conformances on the
/// dependent type's substitution.
static unsigned
getProtocolConformanceIndex(CanGenericSignature generics, ModuleDecl &M,
                            CanType type, ProtocolDecl *protocol) {
  auto conformsTo = generics->getConformsTo(type, M);
  auto it = std::find(conformsTo.begin(), conformsTo.end(), protocol);
  assert(it != conformsTo.end() && "didn't find protocol in conformances");
  return (it - conformsTo.begin());
}

namespace {
  /// A concrete witness table, together with its known layout.
  class WitnessTable {
    llvm::Value *Table;
    const ProtocolInfo &Info;
  public:
    WitnessTable(llvm::Value *wtable, const ProtocolInfo &info)
      : Table(wtable), Info(info) {}

    llvm::Value *getTable() const { return Table; }
    const ProtocolInfo &getInfo() const { return Info; }
  };

  /// A class which lays out a witness table in the abstract.
  class WitnessTableLayout : public SILWitnessVisitor<WitnessTableLayout> {
    unsigned NumWitnesses = 0;
    SmallVector<WitnessTableEntry, 16> Entries;

    WitnessIndex getNextIndex() {
      return WitnessIndex(NumWitnesses++, /*isPrefix=*/false);
    }

  public:
    /// The next witness is an out-of-line base protocol.
    void addOutOfLineBaseProtocol(ProtocolDecl *baseProto) {
      Entries.push_back(
             WitnessTableEntry::forOutOfLineBase(baseProto, getNextIndex()));
    }

    void addMethod(FuncDecl *func) {
      Entries.push_back(WitnessTableEntry::forFunction(func, getNextIndex()));
    }

    void addConstructor(ConstructorDecl *ctor) {
      Entries.push_back(WitnessTableEntry::forFunction(ctor, getNextIndex()));
    }

    void addAssociatedType(AssociatedTypeDecl *ty,
                           ArrayRef<ProtocolDecl *> protos) {
      // An associated type takes up a spot for the type metadata and for the
      // witnesses to all its conformances.
      Entries.push_back(
                      WitnessTableEntry::forAssociatedType(ty, getNextIndex()));
      for (auto *proto : protos)
        if (Lowering::TypeConverter::protocolRequiresWitnessTable(proto))
          ++NumWitnesses;
    }

    unsigned getNumWitnesses() const { return NumWitnesses; }
    ArrayRef<WitnessTableEntry> getEntries() const { return Entries; }
  };

  /// A path through a protocol hierarchy.
  class ProtocolPath {
    IRGenModule &IGM;

    /// The destination protocol.
    ProtocolDecl *Dest;

    /// The path from the selected origin down to the destination
    /// protocol.
    SmallVector<WitnessIndex, 8> ReversePath;

    /// The origin index to use.
    unsigned OriginIndex;

    /// The best path length we found.
    unsigned BestPathLength;

  public:
    /// Find a path from the given set of origins to the destination
    /// protocol.
    ///
    /// T needs to provide a couple of member functions:
    ///   ProtocolDecl *getProtocol() const;
    ///   const ProtocolInfo &getInfo() const;
    template <class T>
    ProtocolPath(IRGenModule &IGM, ArrayRef<T> origins, ProtocolDecl *dest)
      : IGM(IGM), Dest(dest), BestPathLength(~0U) {

      // Consider each of the origins in turn, breaking out if any of
      // them yields a zero-length path.
      for (unsigned i = 0, e = origins.size(); i != e; ++i) {
        auto &origin = origins[i];
        if (considerOrigin(origin.getProtocol(), origin.getInfo(), i))
          break;
      }

      // Sanity check that we actually found a path at all.
      assert(BestPathLength != ~0U);
      assert(BestPathLength == ReversePath.size());
    }

    /// Returns the index of the origin protocol we chose.
    unsigned getOriginIndex() const { return OriginIndex; }

    /// Apply the path to the given witness table.
    llvm::Value *apply(IRGenFunction &IGF, llvm::Value *wtable) const {
      for (unsigned i = ReversePath.size(); i != 0; --i) {
        wtable = emitInvariantLoadOfOpaqueWitness(IGF, wtable,
                                                  ReversePath[i-1]);
        wtable = IGF.Builder.CreateBitCast(wtable, IGF.IGM.WitnessTablePtrTy);
      }
      return wtable;
    }

  private:
    /// Consider paths starting from a new origin protocol.
    /// Returns true if there's no point in considering other origins.
    bool considerOrigin(ProtocolDecl *origin, const ProtocolInfo &originInfo,
                        unsigned originIndex) {
      assert(BestPathLength != 0);

      // If the origin *is* the destination, we can stop here.
      if (origin == Dest) {
        OriginIndex = originIndex;
        BestPathLength = 0;
        ReversePath.clear();
        return true;
      }

      // Otherwise, if the origin gives rise to a better path, that's
      // also cool.
      if (findBetterPath(origin, originInfo, 0)) {
        OriginIndex = originIndex;
        return BestPathLength == 0;
      }

      return false;
    }

    /// Consider paths starting at the given protocol.
    bool findBetterPath(ProtocolDecl *proto, const ProtocolInfo &protoInfo,
                        unsigned lengthSoFar) {
      assert(lengthSoFar < BestPathLength);
      assert(proto != Dest);

      // Keep track of whether we found a better path than the
      // previous best.
      bool foundBetter = false;
      for (auto base : proto->getInheritedProtocols(nullptr)) {
        // ObjC protocols do not have witnesses.
        if (!Lowering::TypeConverter::protocolRequiresWitnessTable(base))
          continue;

        auto &baseEntry = protoInfo.getWitnessEntry(base);
        assert(baseEntry.isBase());

        // Compute the length down to this base.
        unsigned lengthToBase = lengthSoFar;
        if (baseEntry.isOutOfLineBase()) {
          lengthToBase++;

          // Don't consider this path if we reach a length that can't
          // possibly be better than the best so far.
          if (lengthToBase == BestPathLength) continue;
        }
        assert(lengthToBase < BestPathLength);

        // If this base *is* the destination, go ahead and start
        // building the path into ReversePath.
        if (base == Dest) {
          // Reset the collected best-path information.
          BestPathLength = lengthToBase;
          ReversePath.clear();

        // Otherwise, if there isn't a better path through this base,
        // don't accumulate anything in the path.
        } else if (!findBetterPath(base, IGM.getProtocolInfo(base),
                                   lengthToBase)) {
          continue;
        }

        // Okay, we've found a better path, and ReversePath contains a
        // path leading from base to Dest.
        assert(BestPathLength >= lengthToBase);
        foundBetter = true;

        // Add the link from proto to base if necessary.
        if (baseEntry.isOutOfLineBase()) {
          ReversePath.push_back(baseEntry.getOutOfLineBaseIndex());

        // If it isn't necessary, then we might be able to
        // short-circuit considering the bases of this protocol.
        } else {
          if (lengthSoFar == BestPathLength)
            return true;
        }
      }

      return foundBetter;
    }
  };

} // end anonymous namespace

/// Return true if the witness table requires runtime instantiation to
/// handle resiliently-added requirements with default implementations.
static bool isResilientConformance(const NormalProtocolConformance *conformance) {
  // If the protocol is not resilient, the conformance is not resilient
  // either.
  if (conformance->getProtocol()->hasFixedLayout())
    return false;

  // If the protocol is in the same module as the conformance, we're
  // not resilient.
  if (conformance->getDeclContext()->getParentModule()
      == conformance->getProtocol()->getParentModule())
    return false;

  // We have a resilient conformance.
  return true;
}

/// Is there anything about the given conformance that requires witness
/// tables to be dependently-generated?
static bool isDependentConformance(IRGenModule &IGM,
                             const NormalProtocolConformance *conformance,
                                   ResilienceExpansion expansion) {
  // If the conformance is resilient, this is always true.
  if (isResilientConformance(conformance))
    return true;

  // Check whether any of the inherited protocols are dependent.
  for (auto &entry : conformance->getInheritedConformances()) {
    if (isDependentConformance(IGM, entry.second->getRootNormalConformance(),
                               expansion)) {
      return true;
    }
  }

  // If the conforming type isn't dependent, the below check is never true.
  if (!conformance->getDeclContext()->isGenericContext())
    return false;

  // Check whether any of the associated types are dependent.
  if (conformance->forEachTypeWitness(nullptr,
        [&](AssociatedTypeDecl *requirement, const Substitution &sub,
            TypeDecl *explicitDecl) -> bool {
          // RESILIENCE: this could be an opaque conformance
          return sub.getReplacement()->hasArchetype();
       })) {
    return true;
  }

  return false;
}

/// Detail about how an object conforms to a protocol.
class irgen::ConformanceInfo {
  friend class ProtocolInfo;
public:
  virtual ~ConformanceInfo() {}
  virtual llvm::Value *getTable(IRGenFunction &IGF,
                                CanType conformingType,
                               llvm::Value **conformingMetadataCache) const = 0;
  /// Try to get this table as a constant pointer.  This might just
  /// not be supportable at all.
  virtual llvm::Constant *tryGetConstantTable(IRGenModule &IGM,
                                              CanType conformingType) const = 0;
};

static llvm::Value *
emitWitnessTableAccessorCall(IRGenFunction &IGF,
                             const NormalProtocolConformance *conformance,
                             CanType conformingType,
                             llvm::Value **srcMetadataCache) {
  auto accessor =
    IGF.IGM.getAddrOfWitnessTableAccessFunction(conformance, NotForDefinition);

  // If the conformance is generic, the accessor takes the metatype
  // as an argument.
  llvm::CallInst *call;
  if (conformance->getDeclContext()->isGenericContext()) {
    // Emit the source metadata if we haven't yet.
    if (!*srcMetadataCache) {
      *srcMetadataCache = IGF.emitTypeMetadataRef(conformingType);
    }
    call = IGF.Builder.CreateCall(accessor, {*srcMetadataCache});
  } else {
    call = IGF.Builder.CreateCall(accessor, {});
  }

  call->setCallingConv(IGF.IGM.DefaultCC);
  call->setDoesNotAccessMemory();
  call->setDoesNotThrow();

  return call;
}

/// Fetch the lazy access function for the given conformance of the
/// given type.
static llvm::Function *
getWitnessTableLazyAccessFunction(IRGenModule &IGM,
                                  const NormalProtocolConformance *conformance,
                                  CanType conformingType) {
  assert(!conformingType->hasArchetype());
  llvm::Function *accessor =
    IGM.getAddrOfWitnessTableLazyAccessFunction(conformance, conformingType,
                                                ForDefinition);

  // If we're not supposed to define the accessor, or if we already
  // have defined it, just return the pointer.
  if (!accessor->empty())
    return accessor;

  // Okay, define the accessor.
  auto cacheVariable = cast<llvm::GlobalVariable>(
    IGM.getAddrOfWitnessTableLazyCacheVariable(conformance, conformingType,
                                               ForDefinition));
  emitLazyCacheAccessFunction(IGM, accessor, cacheVariable,
                              [&](IRGenFunction &IGF) -> llvm::Value* {
    llvm::Value *conformingMetadataCache = nullptr;
    return emitWitnessTableAccessorCall(IGF, conformance, conformingType,
                                        &conformingMetadataCache);
  });

  return accessor;
}

namespace {

/// Conformance info for a witness table that can be directly generated.
class DirectConformanceInfo : public ConformanceInfo {
  friend class ProtocolInfo;

  const NormalProtocolConformance *RootConformance;
public:
  DirectConformanceInfo(const NormalProtocolConformance *C)
    : RootConformance(C) {}

  llvm::Value *getTable(IRGenFunction &IGF, CanType conformingType,
                        llvm::Value **conformingMetadataCache) const override {
    return IGF.IGM.getAddrOfWitnessTable(RootConformance);
  }

  llvm::Constant *tryGetConstantTable(IRGenModule &IGM,
                                      CanType conformingType) const override {
    return IGM.getAddrOfWitnessTable(RootConformance);
  }
};

/// Conformance info for a witness table that is (or may be) dependent.
class AccessorConformanceInfo : public ConformanceInfo {
  friend class ProtocolInfo;

  const NormalProtocolConformance *Conformance;
public:
  AccessorConformanceInfo(const NormalProtocolConformance *C)
    : Conformance(C) {}

  llvm::Value *getTable(IRGenFunction &IGF, CanType type,
                        llvm::Value **typeMetadataCache) const override {
    // If we're looking up a dependent type, we can't cache the result.
    if (type->hasArchetype()) {
      return emitWitnessTableAccessorCall(IGF, Conformance, type,
                                          typeMetadataCache);
    }

    // Otherwise, call a lazy-cache function.
    auto accessor =
      getWitnessTableLazyAccessFunction(IGF.IGM, Conformance, type);
    llvm::CallInst *call = IGF.Builder.CreateCall(accessor, {});
    call->setCallingConv(IGF.IGM.DefaultCC);
    call->setDoesNotAccessMemory();
    call->setDoesNotThrow();

    return call;
  }

  llvm::Constant *tryGetConstantTable(IRGenModule &IGM,
                                      CanType conformingType) const override {
    return nullptr;
  }
};

  /// A class which lays out a specific conformance to a protocol.
  class WitnessTableBuilder : public SILWitnessVisitor<WitnessTableBuilder> {
    IRGenModule &IGM;
    SmallVectorImpl<llvm::Constant*> &Table;
    CanType ConcreteType;
    const NormalProtocolConformance &Conformance;
    ArrayRef<SILWitnessTable::Entry> SILEntries;
    const ProtocolInfo &PI;
    Optional<FulfillmentMap> Fulfillments;
    SmallVector<std::pair<size_t, const ConformanceInfo *>, 4>
      SpecializedBaseConformances;
    // Metadata caches are stored at negative offsets.
    unsigned NextCacheIndex = 0;
    bool RequiresSpecialization = false;

  public:
    WitnessTableBuilder(IRGenModule &IGM,
                        SmallVectorImpl<llvm::Constant*> &table,
                        SILWitnessTable *SILWT)
      : IGM(IGM), Table(table),
        ConcreteType(SILWT->getConformance()->getType()->getCanonicalType()),
        Conformance(*SILWT->getConformance()),
        SILEntries(SILWT->getEntries()),
        PI(IGM.getProtocolInfo(SILWT->getConformance()->getProtocol()))
    {
      // TODO: in conditional conformances, allocate space for the assumed
      // conformances here.

      // If the conformance is resilient, we require runtime instantiation.
      if (isResilientConformance(&Conformance))
        RequiresSpecialization = true;
    }

    /// The top-level entry point.
    void build();

    /// Create the access function.
    void buildAccessFunction(llvm::Constant *wtable);

    /// A base protocol is witnessed by a pointer to the conformance
    /// of this type to that protocol.
    void addOutOfLineBaseProtocol(ProtocolDecl *baseProto) {
#ifndef NDEBUG
      auto &entry = SILEntries.front();
      assert(entry.getKind() == SILWitnessTable::BaseProtocol
             && "sil witness table does not match protocol");
      assert(entry.getBaseProtocolWitness().Requirement == baseProto
             && "sil witness table does not match protocol");
      auto piEntry = PI.getWitnessEntry(baseProto);
      assert(piEntry.getOutOfLineBaseIndex().getValue() == Table.size()
             && "offset doesn't match ProtocolInfo layout");
#endif
      
      SILEntries = SILEntries.slice(1);

      // TODO: Use the witness entry instead of falling through here.

      // Look for a protocol type info.
      const ProtocolInfo &basePI = IGM.getProtocolInfo(baseProto);
      const ProtocolConformance *astConf
        = Conformance.getInheritedConformance(baseProto);
      const ConformanceInfo &conf =
        basePI.getConformance(IGM, baseProto, astConf);

      // If we can emit the base witness table as a constant, do so.
      llvm::Constant *baseWitness = conf.tryGetConstantTable(IGM, ConcreteType);
      if (baseWitness) {
        Table.push_back(baseWitness);
        return;
      }

      // Otherwise, we'll need to derive it at instantiation time.
      RequiresSpecialization = true;
      SpecializedBaseConformances.push_back({Table.size(), &conf});
      Table.push_back(llvm::ConstantPointerNull::get(IGM.WitnessTablePtrTy));
    }

    void addMethodFromSILWitnessTable(AbstractFunctionDecl *requirement) {
      auto &entry = SILEntries.front();
      SILEntries = SILEntries.slice(1);

      // Handle missing optional requirements.
      if (entry.getKind() == SILWitnessTable::MissingOptional) {
        Table.push_back(llvm::ConstantPointerNull::get(IGM.Int8PtrTy));
        return;
      }

#ifndef NDEBUG
      assert(entry.getKind() == SILWitnessTable::Method
             && "sil witness table does not match protocol");
      assert(entry.getMethodWitness().Requirement.getDecl() == requirement
             && "sil witness table does not match protocol");
      auto piEntry = PI.getWitnessEntry(requirement);
      assert(piEntry.getFunctionIndex().getValue() == Table.size()
             && "offset doesn't match ProtocolInfo layout");
#endif

      SILFunction *Func = entry.getMethodWitness().Witness;
      llvm::Constant *witness = nullptr;
      if (Func) {
        witness = IGM.getAddrOfSILFunction(Func, NotForDefinition);
      } else {
        // The method is removed by dead method elimination.
        // It should be never called. We add a pointer to an error function.
        witness = IGM.getDeletedMethodErrorFn();
      }
      Table.push_back(witness);
      return;
    }

    void addMethod(FuncDecl *requirement) {
      return addMethodFromSILWitnessTable(requirement);
    }

    void addConstructor(ConstructorDecl *requirement) {
      return addMethodFromSILWitnessTable(requirement);
    }

    void addAssociatedType(AssociatedTypeDecl *requirement,
                           ArrayRef<ProtocolDecl *> protos) {
#ifndef NDEBUG
      auto &entry = SILEntries.front();
      assert(entry.getKind() == SILWitnessTable::AssociatedType
             && "sil witness table does not match protocol");
      assert(entry.getAssociatedTypeWitness().Requirement == requirement
             && "sil witness table does not match protocol");
      auto piEntry = PI.getWitnessEntry(requirement);
      assert(piEntry.getAssociatedTypeIndex().getValue() == Table.size()
             && "offset doesn't match ProtocolInfo layout");
#endif

      SILEntries = SILEntries.slice(1);

      const Substitution &sub =
        Conformance.getTypeWitness(requirement, nullptr);
      assert(protos.size() == sub.getConformances().size());

      // This type will be expressed in terms of the archetypes
      // of the conforming context.
      CanType associate = sub.getReplacement()->getCanonicalType();
      assert(!associate->hasTypeParameter());

      llvm::Constant *metadataAccessFunction =
        getAssociatedTypeMetadataAccessFunction(requirement, associate);
      Table.push_back(metadataAccessFunction);

      // FIXME: Add static witness tables for type conformances.
      for (auto index : indices(protos)) {
        ProtocolDecl *protocol = protos[index];
        auto associatedConformance = sub.getConformances()[index];

        if (!Lowering::TypeConverter::protocolRequiresWitnessTable(protocol))
          continue;

#ifndef NDEBUG
        auto &entry = SILEntries.front();
        (void)entry;
        assert(entry.getKind() == SILWitnessTable::AssociatedTypeProtocol
               && "sil witness table does not match protocol");
        auto associatedWitness = entry.getAssociatedTypeProtocolWitness();
        assert(associatedWitness.Requirement == requirement
               && "sil witness table does not match protocol");
        assert(associatedWitness.Protocol == protocol
               && "sil witness table does not match protocol");
#endif

        SILEntries = SILEntries.slice(1);

        llvm::Constant *wtableAccessFunction = 
          getAssociatedTypeWitnessTableAccessFunction(requirement, associate,
                                            protocol, associatedConformance);
        Table.push_back(wtableAccessFunction);
      }
    }

  private:
    llvm::Constant *buildInstantiationFunction();

    llvm::Constant *
    getAssociatedTypeMetadataAccessFunction(AssociatedTypeDecl *requirement,
                                            CanType associatedType);

    llvm::Constant *
    getAssociatedTypeWitnessTableAccessFunction(AssociatedTypeDecl *requirement,
                                                CanType associatedType,
                                                ProtocolDecl *protocol,
                                        ProtocolConformanceRef conformance);

    void emitReturnOfCheckedLoadFromCache(IRGenFunction &IGF,
                                          Address destTable,
                                          llvm::Value *selfMetadata,
                                    llvm::function_ref<llvm::Value*()> body);

    /// Allocate another word of private data storage in the conformance table.
    unsigned getNextCacheIndex() {
      RequiresSpecialization = true;
      return NextCacheIndex++;
    }

    const FulfillmentMap &getFulfillmentMap() {
      if (Fulfillments) return *Fulfillments;

      Fulfillments.emplace();
      if (ConcreteType->hasArchetype()) {
        struct Callback : FulfillmentMap::InterestingKeysCallback {
          bool isInterestingType(CanType type) const override {
            return isa<ArchetypeType>(type);
          }
          bool hasInterestingType(CanType type) const override {
            return type->hasArchetype();
          }
          bool hasLimitedInterestingConformances(CanType type) const override {
            return false;
          }
          GenericSignature::ConformsToArray
          getInterestingConformances(CanType type) const override {
            llvm_unreachable("no limits");
          }
        } callback;
        Fulfillments->searchTypeMetadata(IGM, ConcreteType, IsExact,
                                         /*sourceIndex*/ 0, MetadataPath(),
                                         callback);
      }
      return *Fulfillments;
    }

    void bindArchetypeAccessPathsInConformance(IRGenFunction &IGF) {
      auto declCtx = Conformance.getDeclContext();
      if (auto generics = declCtx->getGenericSignatureOfContext()) {
        auto getInContext = [&](CanType type) -> CanType {
          return ArchetypeBuilder::mapTypeIntoContext(declCtx, type, nullptr)
              ->getCanonicalType();
        };
        bindArchetypeAccessPaths(IGF, generics, getInContext);
      }
    }
  };
}

/// Build the witness table.
void WitnessTableBuilder::build() {
  visitProtocolDecl(Conformance.getProtocol());

  // Go through and convert all the entries to i8*.
  // TODO: the IR would be more legible if we made a struct instead.
  for (auto &entry : Table) {
    entry = llvm::ConstantExpr::getBitCast(entry, IGM.Int8PtrTy);
  }
}

/// Return the address of a function which will return the type metadata
/// for an associated type.
llvm::Constant *WitnessTableBuilder::
getAssociatedTypeMetadataAccessFunction(AssociatedTypeDecl *requirement,
                                        CanType associatedType) {
  // If the associated type is non-dependent, we can use an ordinary
  // metadata access function.  We'll just end up passing extra arguments.
  if (!associatedType->hasArchetype()) {
    return getOrCreateTypeMetadataAccessFunction(IGM, associatedType);
  }

  // Otherwise, emit an access function.
  llvm::Function *accessor =
    IGM.getAddrOfAssociatedTypeMetadataAccessFunction(&Conformance,
                                                      requirement);

  IRGenFunction IGF(IGM, accessor);
  if (IGM.DebugInfo)
    IGM.DebugInfo->emitArtificialFunction(IGF, accessor);

  Explosion parameters = IGF.collectParameters();

  llvm::Value *self = parameters.claimNext();
  setTypeMetadataName(IGM, self, ConcreteType);

  Address destTable(parameters.claimNext(), IGM.getPointerAlignment());
  setProtocolWitnessTableName(IGM, destTable.getAddress(), ConcreteType,
                              requirement->getProtocol());

  // If the associated type is directly fulfillable from the type,
  // we don't need a cache entry.
  // TODO: maybe we should have a cache entry anyway if the fulfillment
  // is expensive.
  if (auto fulfillment =
        getFulfillmentMap().getTypeMetadata(associatedType)) {
    llvm::Value *metadata =
      fulfillment->Path.followFromTypeMetadata(IGF, ConcreteType, self,
                                               /*cache*/ nullptr);
    IGF.Builder.CreateRet(metadata);
    return accessor;
  }

  // Bind local type data from the metadata argument.
  IGF.bindLocalTypeDataFromTypeMetadata(ConcreteType, IsExact, self);

  // Bind archetype access paths.
  bindArchetypeAccessPathsInConformance(IGF);

  // For now, assume that an associated type is cheap enough to access
  // that it doesn't need a new cache entry.
  if (auto archetype = dyn_cast<ArchetypeType>(associatedType)) {
    llvm::Value *metadata = emitArchetypeTypeMetadataRef(IGF, archetype);
    IGF.Builder.CreateRet(metadata);
    return accessor;
  }

  // Otherwise, we need a cache entry.
  emitReturnOfCheckedLoadFromCache(IGF, destTable, self,
                                   [&]() -> llvm::Value* {
    return IGF.emitTypeMetadataRef(associatedType);
  });

  return accessor;
}

/// Return a function which will return a particular witness table
/// conformance.  The function will be passed the metadata for which
/// the conformance is being requested; it may ignore this (perhaps
/// implicitly by taking no arguments).
static llvm::Constant *
getOrCreateWitnessTableAccessFunction(IRGenModule &IGM, CanType type,
                                      ProtocolConformance *conformance) {
  assert(!type->hasArchetype() && "cannot do this for dependent type");

  // We always emit an access function for conformances, and in principle
  // it is always possible to just use that here directly.  However,
  // if it's dependent, doing so won't allow us to cache the result.
  // For the specific use case of an associated type conformance, we could
  // use a cache in the witness table; but that wastes space per conformance
  // and won't let us re-use the cache with other non-dependent uses in
  // the module.  Therefore, in this case, we use the address of the lazy-cache
  // function.
  //
  // FIXME: we will need to pass additional parameters if the target
  // conformance is conditional.
  auto rootConformance = conformance->getRootNormalConformance();
  if (rootConformance->getDeclContext()->isGenericContext()) {
    return getWitnessTableLazyAccessFunction(IGM, rootConformance, type);
  } else {
    return IGM.getAddrOfWitnessTableAccessFunction(
                                    conformance->getRootNormalConformance(),
                                                   NotForDefinition);
  }
}

llvm::Constant *WitnessTableBuilder::
getAssociatedTypeWitnessTableAccessFunction(AssociatedTypeDecl *requirement,
                                            CanType associatedType,
                                            ProtocolDecl *associatedProtocol,
                                ProtocolConformanceRef associatedConformance) {
  if (!associatedType->hasArchetype()) {
    assert(associatedConformance.isConcrete() &&
           "no concrete conformance for non-dependent type");
    return getOrCreateWitnessTableAccessFunction(IGM, associatedType,
                                          associatedConformance.getConcrete());
  }

  // Otherwise, emit an access function.
  llvm::Function *accessor =
    IGM.getAddrOfAssociatedTypeWitnessTableAccessFunction(&Conformance,
                                                          requirement,
                                                          associatedProtocol);

  IRGenFunction IGF(IGM, accessor);
  if (IGM.DebugInfo)
    IGM.DebugInfo->emitArtificialFunction(IGF, accessor);

  Explosion parameters = IGF.collectParameters();

  llvm::Value *associatedTypeMetadata = parameters.claimNext();

  // We use a non-standard name for the type that states the association
  // requirement rather than the concrete type.
  if (IGM.EnableValueNames)
    associatedTypeMetadata->setName(Twine(ConcreteType->getString())
                                      + "." + requirement->getNameStr());

  llvm::Value *self = parameters.claimNext();
  setTypeMetadataName(IGM, self, ConcreteType);

  Address destTable(parameters.claimNext(), IGM.getPointerAlignment());
  setProtocolWitnessTableName(IGM, destTable.getAddress(), ConcreteType,
                              requirement->getProtocol());

  const ConformanceInfo *conformanceI = nullptr;
  if (associatedConformance.isConcrete()) {
    const ProtocolInfo &protocolI = IGM.getProtocolInfo(associatedProtocol);
    conformanceI =
      &protocolI.getConformance(IGM, associatedProtocol,
                                associatedConformance.getConcrete());

    // If we can emit a constant table, do so.
    // In principle, any time we can do this, we should try to re-use this
    // function for other conformances.  But that should typically already
    // be covered by the !hasArchetype() check above.
    if (auto constantTable =
          conformanceI->tryGetConstantTable(IGM, associatedType)) {
      IGF.Builder.CreateRet(constantTable);
      return accessor;
    }
  }

  // If the witness table is directly fulfillable from the type,
  // we don't need a cache entry.
  // TODO: maybe we should have a cache entry anyway if the fulfillment
  // is expensive.
  if (auto fulfillment =
        getFulfillmentMap().getWitnessTable(associatedType,
                                            associatedProtocol)) {
    llvm::Value *wtable =
      fulfillment->Path.followFromTypeMetadata(IGF, ConcreteType, self,
                                               /*cache*/ nullptr);
    IGF.Builder.CreateRet(wtable);
    return accessor;
  }

  // Bind local type data from the metadata arguments.
  IGF.bindLocalTypeDataFromTypeMetadata(associatedType, IsExact,
                                        associatedTypeMetadata);
  IGF.bindLocalTypeDataFromTypeMetadata(ConcreteType, IsExact, self);

  // Bind archetype access paths.
  bindArchetypeAccessPathsInConformance(IGF);

  // For now, assume that finding an abstract conformance is always
  // fast enough that it's not worth caching.
  // TODO: provide an API to find the best metadata path to the conformance
  // and decide whether it's expensive enough to be worth caching.
  if (!conformanceI) {
    assert(associatedConformance.isAbstract());
    auto wtable =
      emitArchetypeWitnessTableRef(IGF, cast<ArchetypeType>(associatedType),
                                   associatedConformance.getAbstract());
    IGF.Builder.CreateRet(wtable);
    return accessor;
  }

  // Otherwise, we need a cache entry.
  emitReturnOfCheckedLoadFromCache(IGF, destTable, self,
                                   [&]() -> llvm::Value* {
    return conformanceI->getTable(IGF, associatedType, &associatedTypeMetadata);
  });

  return accessor;
}

void WitnessTableBuilder::
emitReturnOfCheckedLoadFromCache(IRGenFunction &IGF, Address destTable,
                                 llvm::Value *selfMetadata,
                                 llvm::function_ref<llvm::Value*()> body) {
  // Allocate a new cache slot and drill down to it.
  int cacheIndex = -1 - getNextCacheIndex();
  Address cache = IGF.Builder.CreateConstArrayGEP(destTable, cacheIndex,
                                                  IGM.getPointerSize());

  llvm::Type *expectedTy = IGF.CurFn->getReturnType();
  cache = IGF.Builder.CreateBitCast(cache, expectedTy->getPointerTo());

  // Load and check whether it was null.
  auto cachedResult = IGF.Builder.CreateLoad(cache);
  // TODO: When LLVM supports Consume, we should use it here.
  if (IGF.IGM.IRGen.Opts.Sanitize == SanitizerKind::Thread)
    cachedResult->setOrdering(llvm::AtomicOrdering::Acquire);
  auto cacheIsEmpty = IGF.Builder.CreateIsNull(cachedResult);
  llvm::BasicBlock *fetchBB = IGF.createBasicBlock("fetch");
  llvm::BasicBlock *contBB = IGF.createBasicBlock("cont");
  llvm::BasicBlock *entryBB = IGF.Builder.GetInsertBlock();
  IGF.Builder.CreateCondBr(cacheIsEmpty, fetchBB, contBB);

  // Create a phi in the continuation block and use the loaded value if
  // we branched directly here.  Note that we arrange blocks so that we
  // fall through into this.
  IGF.Builder.emitBlock(contBB);
  auto result = IGF.Builder.CreatePHI(expectedTy, 2);
  result->addIncoming(cachedResult, entryBB);
  IGF.Builder.CreateRet(result);

  // In the fetch block, bind the archetypes and evaluate the body.
  IGF.Builder.emitBlock(fetchBB);

  llvm::Value *fetchedResult = body();

  // Store the fetched result back to the cache.
  // We need to transitively ensure that any stores initializing the result
  // that are visible to us are visible to callers.
  IGF.Builder.CreateStore(fetchedResult, cache)->setOrdering(llvm::Release);

  auto fetchedResultBB = IGF.Builder.GetInsertBlock();
  IGF.Builder.CreateBr(contBB);
  result->addIncoming(fetchedResult, fetchedResultBB);
}

/// Emit the access function for this witness table.
void WitnessTableBuilder::buildAccessFunction(llvm::Constant *wtable) {
  llvm::Function *fn =
    IGM.getAddrOfWitnessTableAccessFunction(&Conformance, ForDefinition);

  IRGenFunction IGF(IGM, fn);
  if (IGM.DebugInfo)
    IGM.DebugInfo->emitArtificialFunction(IGF, fn);

  wtable = llvm::ConstantExpr::getBitCast(wtable, IGM.WitnessTablePtrTy);

  // If specialization isn't required, just return immediately.
  // TODO: allow dynamic specialization?
  if (!RequiresSpecialization) {
    IGF.Builder.CreateRet(wtable);
    return;
  }

  // The target metadata is the first argument.
  assert(isDependentConformance(IGF.IGM, &Conformance,
                                ResilienceExpansion::Maximal));

  Explosion params = IGF.collectParameters();
  llvm::Value *metadata;

  if (Conformance.getDeclContext()->isGenericContext())
    metadata = params.claimNext();
  else
    metadata = llvm::ConstantPointerNull::get(IGF.IGM.TypeMetadataPtrTy);

  // Okay, we need a cache.  Build the cache structure.
  //  struct GenericWitnessTable {
  //    /// The size of the witness table in words.
  //    uint16_t WitnessTableSizeInWords;
  //
  //    /// The amount to copy from the pattern in words.  The rest is zeroed.
  //    uint16_t WitnessTableSizeInWordsToCopy;
  //
  //    /// The protocol.
  //    RelativeIndirectablePointer<ProtocolDescriptor> Protocol;
  //
  //    /// The pattern.
  //    RelativeDirectPointer<WitnessTable> WitnessTable;
  //
  //    /// The instantiation function, which is called after the template is copied.
  //    RelativeDirectPointer<void(WitnessTable *, const Metadata *)> Instantiator;
  //
  //    void *PrivateData[swift::NumGenericMetadataPrivateDataWords];
  //  };

  // First, create the global.  We have to build this in two phases because
  // it contains relative pointers.
  auto cache = cast<llvm::GlobalVariable>(
    IGM.getAddrOfGenericWitnessTableCache(&Conformance, ForDefinition));

  // We need an instantiation function if the base conformance
  // is non-dependent.
  // TODO: the conformance might be conditional.
  llvm::Constant *instantiationFn;
  llvm::Value *instantiationArgs =
    llvm::ConstantPointerNull::get(IGM.Int8PtrPtrTy);
  if (SpecializedBaseConformances.empty()) {
    instantiationFn = llvm::ConstantInt::get(IGM.RelativeAddressTy, 0);    
  } else {
    llvm::Constant *fn = buildInstantiationFunction();
    instantiationFn = IGM.emitDirectRelativeReference(fn, cache, { 4 });
  }

  auto descriptorRef = IGM.getAddrOfLLVMVariableOrGOTEquivalent(
                LinkEntity::forProtocolDescriptor(Conformance.getProtocol()),
                IGM.getPointerAlignment(), IGM.ProtocolDescriptorStructTy);

  // Fill in the global.
  auto cacheTy = cast<llvm::StructType>(cache->getValueType());
  llvm::Constant *cacheData[] = {
    // WitnessTableSizeInWords
    llvm::ConstantInt::get(IGM.Int16Ty, Table.size()),
    // WitnessTablePrivateSizeInWords
    llvm::ConstantInt::get(IGM.Int16Ty, NextCacheIndex),
    // RelativeIndirectablePointer<ProtocolDescriptor>
    IGM.emitRelativeReference(descriptorRef, cache, { 2 }),
    // RelativePointer<WitnessTable>
    IGM.emitDirectRelativeReference(wtable, cache, { 3 }),
    // Instantiation function
    instantiationFn,
    // Private data
    llvm::Constant::getNullValue(cacheTy->getStructElementType(5))
  };
  cache->setInitializer(llvm::ConstantStruct::get(cacheTy, cacheData));

  auto call = IGF.Builder.CreateCall(IGM.getGetGenericWitnessTableFn(),
                                     { cache, metadata, instantiationArgs });
  call->setDoesNotThrow();

  IGF.Builder.CreateRet(call);
}

llvm::Constant *WitnessTableBuilder::buildInstantiationFunction() {
  llvm::Function *fn =
    IGM.getAddrOfGenericWitnessTableInstantiationFunction(&Conformance);
  IRGenFunction IGF(IGM, fn);
  if (IGM.DebugInfo)
    IGM.DebugInfo->emitArtificialFunction(IGF, fn);

  // Break out the parameters.
  Explosion params = IGF.collectParameters();
  Address wtable(params.claimNext(), IGM.getPointerAlignment());
  llvm::Value *metadata = params.claimNext();
  llvm::Value *instantiationArgs = params.claimNext();
  (void) instantiationArgs; // unused for now

  // TODO: store any required conditional-conformance information
  // in the private data.

  // Initialize all the specialized base conformances.
  for (auto &base : SpecializedBaseConformances) {
    // Ask the ConformanceInfo to emit the wtable.
    // TODO: we may need to bind extra information in the IGF in order
    // to make conditional conformances work.
    llvm::Value *baseWTable =
      base.second->getTable(IGF, ConcreteType, &metadata);
    baseWTable = IGF.Builder.CreateBitCast(baseWTable, IGM.Int8PtrTy);

    // Store that to the appropriate slot in the new witness table.
    Address slot = IGF.Builder.CreateConstArrayGEP(wtable, base.first,
                                                   IGM.getPointerSize());
    IGF.Builder.CreateStore(baseWTable, slot);
  }

  IGF.Builder.CreateRetVoid();
  return fn;
}

/// Do a memoized witness-table layout for a protocol.
const ProtocolInfo &IRGenModule::getProtocolInfo(ProtocolDecl *protocol) {
  return Types.getProtocolInfo(protocol);
}

/// Do a memoized witness-table layout for a protocol.
const ProtocolInfo &TypeConverter::getProtocolInfo(ProtocolDecl *protocol) {
  // Check whether we've already translated this protocol.
  auto it = Protocols.find(protocol);
  if (it != Protocols.end()) return *it->second;

  // If not, lay out the protocol's witness table, if it needs one.
  WitnessTableLayout layout;
  if (Lowering::TypeConverter::protocolRequiresWitnessTable(protocol))
    layout.visitProtocolDecl(protocol);

  // Create a ProtocolInfo object from the layout.
  ProtocolInfo *info = ProtocolInfo::create(layout.getNumWitnesses(),
                                            layout.getEntries());
  info->NextConverted = FirstProtocol;
  FirstProtocol = info;

  // Memoize.
  Protocols.insert(std::make_pair(protocol, info));

  // Done.
  return *info;
}

/// Allocate a new ProtocolInfo.
ProtocolInfo *ProtocolInfo::create(unsigned numWitnesses,
                                   ArrayRef<WitnessTableEntry> table) {
  size_t bufferSize = totalSizeToAlloc<WitnessTableEntry>(table.size());
  void *buffer = ::operator new(bufferSize);
  return new(buffer) ProtocolInfo(numWitnesses, table);
}

ProtocolInfo::~ProtocolInfo() {
  for (auto &conf : Conformances) {
    delete conf.second;
  }
}

/// Find the conformance information for a protocol.
const ConformanceInfo &
ProtocolInfo::getConformance(IRGenModule &IGM, ProtocolDecl *protocol,
                             const ProtocolConformance *conformance) const {
  assert(conformance->getProtocol() == protocol &&
         "conformance is for wrong protocol");

  // Drill down to the root normal conformance.
  auto normalConformance = conformance->getRootNormalConformance();

  // Check whether we've already cached this.
  auto it = Conformances.find(normalConformance);
  if (it != Conformances.end()) return *it->second;

  ConformanceInfo *info;

  // If the conformance is dependent in any way, we need to unique it.
  // TODO: maybe this should apply whenever it's out of the module?
  // TODO: actually enable this
  if (isDependentConformance(IGM, normalConformance,
                             ResilienceExpansion::Maximal)) {
    info = new AccessorConformanceInfo(normalConformance);

  // Otherwise, we can use a direct-referencing conformance.
  } else {
    info = new DirectConformanceInfo(normalConformance);
  }

  Conformances.insert({normalConformance, info});
  return *info;
}

void IRGenModule::emitSILWitnessTable(SILWitnessTable *wt) {
  // Don't emit a witness table if it is a declaration.
  if (wt->isDeclaration())
    return;

  // Don't emit a witness table that is available externally.
  // It can end up in having duplicate symbols for generated associated type
  // metadata access functions.
  // Also, it is not a big benefit for LLVM to emit such witness tables.
  if (isAvailableExternally(wt->getLinkage()))
    return;

  // Build the witnesses.
  SmallVector<llvm::Constant*, 32> witnesses;
  WitnessTableBuilder wtableBuilder(*this, witnesses, wt);
  wtableBuilder.build();
  
  assert(getProtocolInfo(wt->getConformance()->getProtocol())
           .getNumWitnesses() == witnesses.size()
         && "witness table size doesn't match ProtocolInfo");

  // Produce the initializer value.
  auto tableTy = llvm::ArrayType::get(FunctionPtrTy, witnesses.size());
  auto initializer = llvm::ConstantArray::get(tableTy, witnesses);

  auto global = cast<llvm::GlobalVariable>(
                         getAddrOfWitnessTable(wt->getConformance(), tableTy));
  global->setConstant(true);
  global->setInitializer(initializer);
  global->setAlignment(getWitnessTableAlignment().getValue());

  // FIXME: resilience; this should use the conformance's publishing scope.
  wtableBuilder.buildAccessFunction(global);

  // Behavior conformances can't be reflected.
  if (wt->getConformance()->isBehaviorConformance())
    return;

  addProtocolConformanceRecord(wt->getConformance());
}


/// True if a function's signature in LLVM carries polymorphic parameters.
/// Generic functions and protocol witnesses carry polymorphic parameters.
bool irgen::hasPolymorphicParameters(CanSILFunctionType ty) {
  switch (ty->getRepresentation()) {
  case SILFunctionTypeRepresentation::Block:
    // Should never be polymorphic.
    assert(!ty->isPolymorphic() && "polymorphic C function?!");
    return false;

  case SILFunctionTypeRepresentation::Thick:
  case SILFunctionTypeRepresentation::Thin:
  case SILFunctionTypeRepresentation::Method:
    return ty->isPolymorphic();

  case SILFunctionTypeRepresentation::CFunctionPointer:
  case SILFunctionTypeRepresentation::ObjCMethod:
    // May be polymorphic at the SIL level, but no type metadata is actually
    // passed.
    return false;

  case SILFunctionTypeRepresentation::WitnessMethod:
    // Always carries polymorphic parameters for the Self type.
    return true;
  }
}

static
void addPotentialArchetypeAccessPath(IRGenFunction &IGF,
                                     CanType targetDepType,
                                     CanType sourceDepType,
                                     GetTypeParameterInContextFn getInContext) {
  assert(targetDepType->isTypeParameter());
  assert(sourceDepType->isTypeParameter());

  // We can only break down an associated-type path.
  auto sourceDepMemberType = dyn_cast<DependentMemberType>(sourceDepType);
  if (!sourceDepMemberType) return;

  // We only really need to do this when there's a non-trivial set of
  // conformances, but we can't determine that just from this decl:
  // the associated type might gain conformances in a refining protocol.
  auto association = sourceDepMemberType->getAssocType();

  // These can end up as non-archetypes because of multiple levels of
  // equality.
  auto destArchetype =
    dyn_cast<ArchetypeType>(getInContext(targetDepType));
  if (!destArchetype) return;
  auto srcBaseArchetype =
    dyn_cast<ArchetypeType>(getInContext(sourceDepMemberType.getBase()));
  if (!srcBaseArchetype) return;

  IGF.addArchetypeAccessPath(destArchetype,
                             {srcBaseArchetype, association});
}

void irgen::bindArchetypeAccessPaths(IRGenFunction &IGF, GenericSignature *Generics,
                              GetTypeParameterInContextFn getInContext) {
  // Remember all the extra ways we have of reaching the parameter
  // archetypes due to type equality constraints.
  for (auto reqt : Generics->getRequirements()) {
    // Ignore non-same-type requirements in this pass.
    if (reqt.getKind() != RequirementKind::SameType) continue;

    // Ignore equality constraints to concrete types.  This is really
    // just a fast-path; we still have to handle this case later.
    // TODO: This might be a faster / better-cached way to materialize
    // local type data for the concrete type.
    if (!reqt.getSecondType()->isTypeParameter()) continue;

    auto firstType = reqt.getFirstType()->getCanonicalType();
    auto secondType = reqt.getSecondType()->getCanonicalType();

    addPotentialArchetypeAccessPath(IGF, firstType, secondType, getInContext);
    addPotentialArchetypeAccessPath(IGF, secondType, firstType, getInContext);
  }
}

/// Emit a polymorphic parameters clause, binding all the metadata necessary.
void EmitPolymorphicParameters::emit(Explosion &in,
                                     WitnessMetadata *witnessMetadata,
                                     const GetParameterFn &getParameter) {
  // Collect any early sources and bind local type data from them.
  for (auto &source : getSources()) {
    bindExtraSource(source, in, witnessMetadata);
  }
  
  auto getInContext = [&](CanType type) -> CanType {
    return getTypeInContext(type);
  };

  // Collect any concrete type metadata that's been passed separately.
  enumerateUnfulfilledRequirements([&](GenericRequirement requirement) {
    auto value = in.claimNext();
    bindGenericRequirement(IGF, requirement, value, getInContext);
  });

  // Bind all the fulfillments we can from the formal parameters.
  bindParameterSources(getParameter);

  if (!Generics) return;
  
  // Bind all the archetype access paths.
  bindArchetypeAccessPaths(IGF, Generics, getInContext);
}

void IRGenFunction::addArchetypeAccessPath(CanArchetypeType targetArchetype,
                                           ArchetypeAccessPath accessPath) {
  ArchetypeAccessPaths[targetArchetype].push_back(accessPath);
}

ArrayRef<IRGenFunction::ArchetypeAccessPath>
IRGenFunction::getArchetypeAccessPaths(CanArchetypeType targetArchetype) {
  auto it = ArchetypeAccessPaths.find(targetArchetype);
  if (it == ArchetypeAccessPaths.end()) {
    return {};
  } else {
    return it->second;
  }
}


llvm::Value *
MetadataPath::followFromTypeMetadata(IRGenFunction &IGF,
                                     CanType sourceType,
                                     llvm::Value *source,
                                     Map<llvm::Value*> *cache) const {
  LocalTypeDataKey key = {
    sourceType,
    LocalTypeDataKind::forTypeMetadata()
  };
  return follow(IGF, key, source, Path.begin(), Path.end(), cache);
}

llvm::Value *
MetadataPath::followFromWitnessTable(IRGenFunction &IGF,
                                     CanType conformingType,
                                     ProtocolConformanceRef conformance,
                                     llvm::Value *source,
                                     Map<llvm::Value*> *cache) const {
  LocalTypeDataKey key = {
    conformingType,
    LocalTypeDataKind::forProtocolWitnessTable(conformance)
  };
  return follow(IGF, key, source, Path.begin(), Path.end(), cache);
}

/// Follow this metadata path.
///
/// \param sourceKey - A description of the source value.  Not necessarily
///   an appropriate caching key.
/// \param cache - If given, this cache will be used to short-circuit
///   the lookup; otherwise, the global (but dominance-sensitive) cache
///   in the IRGenFunction will be used.  This caching system is somewhat
///   more efficient than what IGF provides, but it's less general, and it
///   should probably be removed.
llvm::Value *MetadataPath::follow(IRGenFunction &IGF,
                                  LocalTypeDataKey sourceKey,
                                  llvm::Value *source,
                                  iterator begin, iterator end,
                                  Map<llvm::Value*> *cache) {
  assert(source && "no source metadata value!");

  // The invariant is that this iterator starts a path from source and
  // that sourceKey is correctly describes it.
  iterator i = begin;

  // Before we begin emitting code to generate the actual path, try to find
  // the latest point in the path that we've cached a value for.

  // If the caller gave us a cache to use, check that.  This lookup is very
  // efficient and doesn't even require us to parse the prefix.
  if (cache) {
    auto result = cache->findPrefix(begin, end);
    if (result.first) {
      source = *result.first;

      // If that was the end, there's no more work to do; don't bother
      // adjusting the source key.
      if (result.second == end)
        return source;

      // Advance the source key past the cached prefix.
      while (i != result.second) {
        Component component = *i++;
        (void) followComponent(IGF, sourceKey, /*source*/ nullptr, component);
      }
    }

  // Otherwise, make a pass over the path looking for available concrete
  // entries in the IGF's local type data cache.
  } else {
    auto skipI = i;
    LocalTypeDataKey skipKey = sourceKey;
    while (skipI != end) {
      Component component = *skipI++;
      (void) followComponent(IGF, skipKey, /*source*/ nullptr, component);

      // Check the cache for a concrete value.  We don't want an abstract
      // entry because, if one exists, we'll just end up here again
      // recursively.
      if (auto skipSource =
            IGF.tryGetConcreteLocalTypeData(skipKey.getCachingKey())) {
        // If we found one, advance the info for the source to the current
        // point in the path, then continue the search.
        sourceKey = skipKey;
        source = skipSource;
        i = skipI;
      }
    }
  }

  // Drill in on the actual source value.
  while (i != end) {
    auto component = *i++;
    source = followComponent(IGF, sourceKey, source, component);

    // If we have a cache, remember this in the cache at the next position.
    if (cache) {
      cache->insertNew(begin, i, source);

    // Otherwise, insert it into the global cache.
    } else {
      IGF.setScopedLocalTypeData(sourceKey, source);
    }
  }

  return source;
}

/// Drill down on a single stage of component.
///
/// sourceType and sourceDecl will be adjusted to refer to the new
/// component.  Source can be null, in which case this will be the only
/// thing done.
llvm::Value *MetadataPath::followComponent(IRGenFunction &IGF,
                                           LocalTypeDataKey &sourceKey,
                                           llvm::Value *source,
                                           Component component) {
  switch (component.getKind()) {
  case Component::Kind::NominalTypeArgument:
  case Component::Kind::NominalTypeArgumentConformance: {
    assert(sourceKey.Kind == LocalTypeDataKind::forTypeMetadata());
    auto generic = cast<BoundGenericType>(sourceKey.Type);
    auto reqtIndex = component.getPrimaryIndex();

    GenericTypeRequirements requirements(IGF.IGM, generic->getDecl());
    auto &requirement = requirements.getRequirements()[reqtIndex];

    auto module = IGF.getSwiftModule();
    auto generics = generic->getDecl()->getGenericSignatureOfContext()
                                      ->getCanonicalSignature();

    auto argIndex =
      getDependentTypeIndex(generics, *module, requirement.TypeParameter);
    Substitution sub = generic->gatherAllSubstitutions(module, nullptr)[argIndex];

    // In either case, we need to change the type.
    sourceKey.Type = sub.getReplacement()->getCanonicalType();

    // If this is a type argument, we've fully updated sourceKey.
    if (component.getKind() == Component::Kind::NominalTypeArgument) {
      assert(!requirement.Protocol && "index mismatch!");
      if (source) {
        source = emitArgumentMetadataRef(IGF, generic->getDecl(),
                                         requirements, reqtIndex, source);
        setTypeMetadataName(IGF.IGM, source, sourceKey.Type);
      }

    // Otherwise, we need to switch sourceKey.Kind to the appropriate
    // conformance kind.
    } else {
      assert(requirement.Protocol && "index mismatch!");
      auto confIndex = getProtocolConformanceIndex(generics, *module,
                                                   requirement.TypeParameter,
                                                   requirement.Protocol);
      auto conformance = sub.getConformances()[confIndex];
      assert(conformance.getRequirement() == requirement.Protocol);
      sourceKey.Kind = LocalTypeDataKind::forProtocolWitnessTable(conformance);

      if (source) {
        auto protocol = conformance.getRequirement();
        source = emitArgumentWitnessTableRef(IGF, generic->getDecl(),
                                             requirements, reqtIndex, source);
        setProtocolWitnessTableName(IGF.IGM, source, sourceKey.Type, protocol);
      }
    }

    return source;
  }

  case Component::Kind::NominalParent: {
    assert(sourceKey.Kind == LocalTypeDataKind::forTypeMetadata());
    NominalTypeDecl *nominalDecl;
    if (auto nominal = dyn_cast<NominalType>(sourceKey.Type)) {
      nominalDecl = nominal->getDecl();
      sourceKey.Type = nominal.getParent();
    } else {
      auto generic = cast<BoundGenericType>(sourceKey.Type);
      nominalDecl = generic->getDecl();
      sourceKey.Type = generic.getParent();
    }

    if (source) {
      source = emitParentMetadataRef(IGF, nominalDecl, source);
      setTypeMetadataName(IGF.IGM, source, sourceKey.Type);
    }
    return source;
  }

  case Component::Kind::InheritedProtocol: {
    auto conformance = sourceKey.Kind.getProtocolConformance();
    auto protocol = conformance.getRequirement();
    auto inheritedProtocol =
      protocol->getInheritedProtocols(nullptr)[component.getPrimaryIndex()];

    sourceKey.Kind =
      LocalTypeDataKind::forAbstractProtocolWitnessTable(inheritedProtocol);
    if (conformance.isConcrete()) {
      auto inheritedConformance =
        conformance.getConcrete()->getInheritedConformance(inheritedProtocol);
      if (inheritedConformance) {
        sourceKey.Kind = LocalTypeDataKind::forConcreteProtocolWitnessTable(
                                                          inheritedConformance);
      }
    }

    if (source) {
      auto &pi = IGF.IGM.getProtocolInfo(protocol);
      auto &entry = pi.getWitnessEntry(inheritedProtocol);
      assert(entry.isOutOfLineBase());
      source = emitInvariantLoadOfOpaqueWitness(IGF, source,
                                                entry.getOutOfLineBaseIndex());
      source = IGF.Builder.CreateBitCast(source, IGF.IGM.WitnessTablePtrTy);
      setProtocolWitnessTableName(IGF.IGM, source, sourceKey.Type,
                                  inheritedProtocol);
    }
    return source;
  }

  case Component::Kind::Impossible:
    llvm_unreachable("following an impossible path!");

  } 
  llvm_unreachable("bad metadata path component");
}

void MetadataPath::dump() const {
  print(llvm::errs());
}
void MetadataPath::print(llvm::raw_ostream &out) const {
  for (auto i = Path.begin(), e = Path.end(); i != e; ++i) {
    if (i != Path.begin()) out << ".";
    auto component = *i;
    switch (component.getKind()) {
    case Component::Kind::InheritedProtocol:
      out << "inherited_protocol[" << component.getPrimaryIndex() << "]";
      break;
    case Component::Kind::NominalTypeArgument:
      out << "nominal_type_argument[" << component.getPrimaryIndex() << "]";
      break;
    case Component::Kind::NominalTypeArgumentConformance:
      out << "nominal_type_argument_conformance["
          << component.getPrimaryIndex() << "]";
      break;
    case Component::Kind::NominalParent:
      out << "nominal_parent";
      break;
    case Component::Kind::Impossible:
      out << "impossible";
      break;
    }
  }
}

/// Collect any required metadata for a witness method from the end of
/// the given parameter list.
void irgen::collectTrailingWitnessMetadata(IRGenFunction &IGF,
                                           SILFunction &fn,
                                           Explosion &params,
                                           WitnessMetadata &witnessMetadata) {
  assert(fn.getLoweredFunctionType()->getRepresentation()
           == SILFunctionTypeRepresentation::WitnessMethod);

  llvm::Value *wtable = params.takeLast();
  assert(wtable->getType() == IGF.IGM.WitnessTablePtrTy &&
         "parameter signature mismatch: witness metadata didn't "
         "end in witness table?");
  wtable->setName("SelfWitnessTable");
  witnessMetadata.SelfWitnessTable = wtable;

  llvm::Value *metatype = params.takeLast();
  assert(metatype->getType() == IGF.IGM.TypeMetadataPtrTy &&
         "parameter signature mismatch: witness metadata didn't "
         "end in metatype?");
  metatype->setName("Self");
  witnessMetadata.SelfMetadata = metatype;
}

/// Perform all the bindings necessary to emit the given declaration.
void irgen::emitPolymorphicParameters(IRGenFunction &IGF,
                                      SILFunction &Fn,
                                      Explosion &in,
                                      WitnessMetadata *witnessMetadata,
                                      const GetParameterFn &getParameter) {
  EmitPolymorphicParameters(IGF, Fn).emit(in, witnessMetadata, getParameter);
}

Size NecessaryBindings::getBufferSize(IRGenModule &IGM) const {
  // We need one pointer for each archetype or witness table.
  return IGM.getPointerSize() * Requirements.size();
}

void NecessaryBindings::restore(IRGenFunction &IGF, Address buffer) const {
  bindFromGenericRequirementsBuffer(IGF, Requirements.getArrayRef(), buffer,
                                    [&](CanType type) { return type;});
}

void NecessaryBindings::save(IRGenFunction &IGF, Address buffer) const {
  emitInitOfGenericRequirementsBuffer(IGF, Requirements.getArrayRef(), buffer,
        [&](GenericRequirement requirement) -> llvm::Value* {
    CanType type = requirement.TypeParameter;
    if (auto protocol = requirement.Protocol) {
      auto wtable =
        emitArchetypeWitnessTableRef(IGF, cast<ArchetypeType>(type), protocol);
      return wtable;
    } else {
      auto metadata = IGF.emitTypeMetadataRef(type);
      return metadata;
    }
  });
}

void NecessaryBindings::addTypeMetadata(CanType type) {
  // Bindings are only necessary at all if the type is dependent.
  if (!type->hasArchetype()) return;

  // Break down structural types so that we don't eagerly pass metadata
  // for the structural type.  Future considerations for this:
  //   - If we have the structural type lying around in some cheap fashion,
  //     maybe we *should* just pass it.
  //   - Passing a structural type should remove the need to pass its
  //     components separately.
  if (auto tuple = dyn_cast<TupleType>(type)) {
    for (auto elt : tuple.getElementTypes())
      addTypeMetadata(elt);
    return;
  }
  if (auto fn = dyn_cast<FunctionType>(type)) {
    addTypeMetadata(fn.getInput());
    addTypeMetadata(fn.getResult());
    return;
  }
  if (auto inout = dyn_cast<InOutType>(type)) {
    addTypeMetadata(inout.getObjectType());
    return;
  }
  if (auto metatype = dyn_cast<MetatypeType>(type)) {
    addTypeMetadata(metatype.getInstanceType());
    return;
  }
  // Generic types are trickier, because they can require conformances.

  // Otherwise, just record the need for this metadata.
  Requirements.insert({type, nullptr});
}

void NecessaryBindings::addProtocolConformance(CanType type,
                                               ProtocolConformanceRef conf) {
  if (!conf.isAbstract()) return;
  assert(isa<ArchetypeType>(type));

  // TODO: pass something about the root conformance necessary to
  // reconstruct this.
  Requirements.insert({type, conf.getAbstract()});
}

llvm::Value *irgen::emitImpliedWitnessTableRef(IRGenFunction &IGF,
                                               ArrayRef<ProtocolEntry> entries,
                                               ProtocolDecl *target,
                                     const GetWitnessTableFn &getWitnessTable) {
  ProtocolPath path(IGF.IGM, entries, target);
  auto wtable = getWitnessTable(path.getOriginIndex());
  wtable = path.apply(IGF, wtable);
  return wtable;
}

llvm::Value *irgen::emitWitnessTableRef(IRGenFunction &IGF,
                                        CanType srcType,
                                        ProtocolConformanceRef conformance) {
  llvm::Value *srcMetadataCache = nullptr;
  return emitWitnessTableRef(IGF, srcType, &srcMetadataCache, conformance);
}

/// Emit a protocol witness table for a conformance.
llvm::Value *irgen::emitWitnessTableRef(IRGenFunction &IGF,
                                        CanType srcType,
                                        llvm::Value **srcMetadataCache,
                                        ProtocolConformanceRef conformance) {
  auto proto = conformance.getRequirement();
  assert(Lowering::TypeConverter::protocolRequiresWitnessTable(proto)
         && "protocol does not have witness tables?!");

  // If we don't have concrete conformance information, the type must be
  // an archetype and the conformance must be via one of the protocol
  // requirements of the archetype. Look at what's locally bound.
  if (conformance.isAbstract()) {
    auto archetype = cast<ArchetypeType>(srcType);
    return emitArchetypeWitnessTableRef(IGF, archetype, proto);
  }

  // All other source types should be concrete enough that we have
  // conformance info for them.  However, that conformance info might be
  // more concrete than we're expecting.
  // TODO: make a best effort to devirtualize, maybe?
  auto concreteConformance = conformance.getConcrete();
  if (concreteConformance->getProtocol() != proto) {
    concreteConformance = concreteConformance->getInheritedConformance(proto);
  }
  auto &protoI = IGF.IGM.getProtocolInfo(proto);
  auto &conformanceI =
    protoI.getConformance(IGF.IGM, proto, concreteConformance);
  return conformanceI.getTable(IGF, srcType, srcMetadataCache);
}

/// Emit the witness table references required for the given type
/// substitution.
void irgen::emitWitnessTableRefs(IRGenFunction &IGF,
                                 const Substitution &sub,
                                 llvm::Value **metadataCache,
                                 SmallVectorImpl<llvm::Value*> &out) {
  auto conformances = sub.getConformances();

  // We don't need to do anything if we have no protocols to conform to.
  if (conformances.empty()) return;

  // Look at the replacement type.
  CanType replType = sub.getReplacement()->getCanonicalType();

  for (auto &conformance : conformances) {
    auto *proto = conformance.getRequirement();
    if (!Lowering::TypeConverter::protocolRequiresWitnessTable(proto))
      continue;

    auto wtable = emitWitnessTableRef(IGF, replType, metadataCache,
                                      conformance);

    out.push_back(wtable);
  }
}

static CanType getSubstSelfType(CanSILFunctionType substFnType) {
  // Grab the apparent 'self' type.  If there isn't a 'self' type,
  // we're not going to try to access this anyway.
  assert(!substFnType->getParameters().empty());

  auto selfParam = substFnType->getParameters().back();
  CanType substInputType = selfParam.getType();
  // If the parameter is a direct metatype parameter, this is a static method
  // of the instance type. We can assume this because:
  // - metatypes cannot directly conform to protocols
  // - even if they could, they would conform as a value type 'self' and thus
  //   be passed indirectly as an @in or @inout parameter.
  if (auto meta = dyn_cast<MetatypeType>(substInputType)) {
    if (!selfParam.isIndirect())
      substInputType = meta.getInstanceType();
  }
  return substInputType;
}

namespace {
  class EmitPolymorphicArguments : public PolymorphicConvention {
    IRGenFunction &IGF;
  public:
    EmitPolymorphicArguments(IRGenFunction &IGF,
                             CanSILFunctionType polyFn)
      : PolymorphicConvention(IGF.IGM, polyFn), IGF(IGF) {}

    void emit(CanSILFunctionType substFnType, ArrayRef<Substitution> subs,
              WitnessMetadata *witnessMetadata, Explosion &out);

  private:
    void emitEarlySources(CanSILFunctionType substFnType, Explosion &out) {
      for (auto &source : getSources()) {
        switch (source.getKind()) {
        // Already accounted for in the parameters.
        case MetadataSource::Kind::ClassPointer:
        case MetadataSource::Kind::Metadata:
          continue;

        // Needs a special argument.
        case MetadataSource::Kind::GenericLValueMetadata: {
          out.add(IGF.emitTypeMetadataRef(getSubstSelfType(substFnType)));
          continue;
        }

        // Witness 'Self' arguments are added as a special case in
        // EmitPolymorphicArguments::emit.
        case MetadataSource::Kind::SelfMetadata:
        case MetadataSource::Kind::SelfWitnessTable:
          continue;
        }
        llvm_unreachable("bad source kind!");
      }
    }
  };
}

/// Pass all the arguments necessary for the given function.
void irgen::emitPolymorphicArguments(IRGenFunction &IGF,
                                     CanSILFunctionType origFnType,
                                     CanSILFunctionType substFnType,
                                     ArrayRef<Substitution> subs,
                                     WitnessMetadata *witnessMetadata,
                                     Explosion &out) {
  EmitPolymorphicArguments(IGF, origFnType).emit(substFnType, subs,
                                                 witnessMetadata, out);
}

void EmitPolymorphicArguments::emit(CanSILFunctionType substFnType,
                                    ArrayRef<Substitution> subs,
                                    WitnessMetadata *witnessMetadata,
                                    Explosion &out) {
  // Add all the early sources.
  emitEarlySources(substFnType, out);

  // For now, treat all archetypes independently.
  enumerateUnfulfilledRequirements([&](GenericRequirement requirement) {
    llvm::Value *requiredValue =
      emitGenericRequirementFromSubstitutions(IGF, Generics, M,
                                              requirement, subs);
    out.add(requiredValue);
  });

  // For a witness call, add the Self argument metadata arguments last.
  for (auto &source : getSources()) {
    switch (source.getKind()) {
    case MetadataSource::Kind::Metadata:
    case MetadataSource::Kind::ClassPointer:
      // Already accounted for in the arguments.
      continue;

    case MetadataSource::Kind::GenericLValueMetadata:
      // Added in the early phase.
      continue;

    case MetadataSource::Kind::SelfMetadata: {
      assert(witnessMetadata && "no metadata structure for witness method");
      auto self = IGF.emitTypeMetadataRef(getSubstSelfType(substFnType));
      witnessMetadata->SelfMetadata = self;
      continue;
    }

    case MetadataSource::Kind::SelfWitnessTable: {
      // Added later.
      continue;
    }
    }
    llvm_unreachable("bad source kind");
  }
}

NecessaryBindings
NecessaryBindings::forFunctionInvocations(IRGenModule &IGM,
                                          CanSILFunctionType origType,
                                          CanSILFunctionType substType,
                                          ArrayRef<Substitution> subs) {
  NecessaryBindings bindings;

  // Bail out early if we don't have polymorphic parameters.
  if (!hasPolymorphicParameters(origType))
    return bindings;

  // Figure out what we're actually required to pass:
  PolymorphicConvention convention(IGM, origType);

  //  - unfulfilled requirements
  convention.enumerateUnfulfilledRequirements(
                                        [&](GenericRequirement requirement) {
    auto depTyIndex =
      getDependentTypeIndex(origType->getGenericSignature(),
                            *IGM.getSwiftModule(),
                            requirement.TypeParameter);

    auto &sub = subs[depTyIndex];
    CanType type = sub.getReplacement()->getCanonicalType();

    if (requirement.Protocol) {
      auto confIndex =
        getProtocolConformanceIndex(origType->getGenericSignature(),
                                    *IGM.getSwiftModule(),
                                    requirement.TypeParameter,
                                    requirement.Protocol);
      auto conf = sub.getConformances()[confIndex];
      bindings.addProtocolConformance(type, conf);
    } else {
      bindings.addTypeMetadata(type);
    }
  });

  //   - extra sources
  for (auto &source : convention.getSources()) {
    switch (source.getKind()) {
    case MetadataSource::Kind::Metadata:
    case MetadataSource::Kind::ClassPointer:
      continue;

    case MetadataSource::Kind::GenericLValueMetadata:
      bindings.addTypeMetadata(getSubstSelfType(substType));
      continue;

    case MetadataSource::Kind::SelfMetadata:
      bindings.addTypeMetadata(getSubstSelfType(substType));
      continue;

    case MetadataSource::Kind::SelfWitnessTable:
      // We'll just pass undef in cases like this.
      continue;
    }
    llvm_unreachable("bad source kind");
  }

  return bindings;
}

/// The information we need to record in generic type metadata
/// is the information in the type's generic signature, minus the
/// information recoverable from the type's parent type.  This is
/// simply the information that would be passed to a generic function
/// that takes the (thick) parent metatype as an argument.
GenericTypeRequirements::GenericTypeRequirements(IRGenModule &IGM,
                                                 NominalTypeDecl *typeDecl)
    : TheDecl(typeDecl) {

  // We only need to do something here if the declaration context is
  // somehow generic.
  auto ncGenerics = typeDecl->getGenericSignatureOfContext();
  if (!ncGenerics) return;

  // Construct a representative function type.
  auto generics = ncGenerics->getCanonicalSignature();
  CanSILFunctionType fnType = [&]() -> CanSILFunctionType {
    CanType type = typeDecl->getDeclaredInterfaceType()->getCanonicalType();
    if (auto nominal = dyn_cast<NominalType>(type)) {
      ParentType = nominal.getParent();
    } else {
      ParentType = cast<BoundGenericType>(type).getParent();
    }

    // Ignore the existence of the parent type if it has no type parameters.
    if (ParentType && !ParentType->hasTypeParameter())
      ParentType = CanType();

    SmallVector<SILParameterInfo, 1> params;
    if (ParentType) {
      auto parentMetatype =
        CanMetatypeType::get(ParentType, MetatypeRepresentation::Thick);
      params.push_back(SILParameterInfo(parentMetatype,
                                        ParameterConvention::Direct_Unowned));
    }

    return SILFunctionType::get(generics, SILFunctionType::ExtInfo(),
                                /*callee*/ ParameterConvention::Direct_Unowned,
                                params, /*results*/ {}, /*error*/ None,
                                IGM.Context);
  }();

  // Figure out what we're actually still required to pass 
  PolymorphicConvention convention(IGM, fnType);
  convention.enumerateUnfulfilledRequirements([&](GenericRequirement reqt) {
    Requirements.push_back(reqt);
  });

  // We do not need to consider extra sources.
}

void
GenericTypeRequirements::enumerateFulfillments(IRGenModule &IGM,
                                               ArrayRef<Substitution> subs,
                                               FulfillmentCallback callback) {
  if (empty()) return;

  auto signature =
    TheDecl->getGenericSignatureOfContext()->getCanonicalSignature();
  for (auto reqtIndex : indices(getRequirements())) {
    auto &reqt = getRequirements()[reqtIndex];
    auto typeIndex = getDependentTypeIndex(signature, *IGM.getSwiftModule(),
                                           reqt.TypeParameter);
    auto &sub = subs[typeIndex];
    CanType type = sub.getReplacement()->getCanonicalType();
    if (reqt.Protocol) {
      auto confIndex =
        getProtocolConformanceIndex(signature, *IGM.getSwiftModule(),
                                    reqt.TypeParameter, reqt.Protocol);
      auto conformance = sub.getConformances()[confIndex];
      callback(reqtIndex, type, conformance);
    } else {
      callback(reqtIndex, type, None);
    }
  }
}

void GenericTypeRequirements::emitInitOfBuffer(IRGenFunction &IGF,
                                               ArrayRef<Substitution> subs,
                                               Address buffer) {
  if (Requirements.empty()) return;

  auto generics =
    TheDecl->getGenericSignatureOfContext()->getCanonicalSignature();
  auto &module = *TheDecl->getParentModule();
  emitInitOfGenericRequirementsBuffer(IGF, Requirements, buffer,
                                      [&](GenericRequirement requirement) {
    return emitGenericRequirementFromSubstitutions(IGF, generics, module,
                                                   requirement, subs);
  });
}

void irgen::emitInitOfGenericRequirementsBuffer(IRGenFunction &IGF,
                               ArrayRef<GenericRequirement> requirements,
                               Address buffer,
                               EmitGenericRequirementFn emitRequirement) {
  if (requirements.empty()) return;

  // Cast the buffer to %type**.
  buffer = IGF.Builder.CreateElementBitCast(buffer, IGF.IGM.TypeMetadataPtrTy);

  for (auto index : indices(requirements)) {
    // GEP to the appropriate slot.
    Address slot = buffer;
    if (index != 0) {
      slot = IGF.Builder.CreateConstArrayGEP(slot, index,
                                             IGF.IGM.getPointerSize());
    }

    llvm::Value *value = emitRequirement(requirements[index]);
    if (requirements[index].Protocol) {
      slot = IGF.Builder.CreateElementBitCast(slot, IGF.IGM.WitnessTablePtrTy);
    }
    IGF.Builder.CreateStore(value, slot);
  }
}

llvm::Value *
irgen::emitGenericRequirementFromSubstitutions(IRGenFunction &IGF,
                                               CanGenericSignature generics,
                                               ModuleDecl &module,
                                               GenericRequirement requirement,
                                               ArrayRef<Substitution> subs) {
  CanType depTy = requirement.TypeParameter;
  auto typeIndex = getDependentTypeIndex(generics, module, depTy);
  const Substitution &sub = subs[typeIndex];
  CanType argType = sub.getReplacement()->getCanonicalType();

  if (!requirement.Protocol) {
    auto argMetadata = IGF.emitTypeMetadataRef(argType);
    return argMetadata;
  }

  auto proto = requirement.Protocol;
  auto protoIndex = getProtocolConformanceIndex(generics, module, depTy, proto);
  auto conformance = sub.getConformances()[protoIndex];
  assert(conformance.getRequirement() == proto);
  llvm::Value *metadata = nullptr;
  auto wtable = emitWitnessTableRef(IGF, argType, &metadata, conformance);
  return wtable;
}

void GenericTypeRequirements::bindFromBuffer(IRGenFunction &IGF,
                                             Address buffer,
                                    GetTypeParameterInContextFn getInContext) {
  bindFromGenericRequirementsBuffer(IGF, Requirements, buffer, getInContext);

  auto Generics = TheDecl->getGenericSignature();
  if (!Generics) return;

  // Bind all the archetype access paths in the signature's requirements.
  bindArchetypeAccessPaths(IGF, Generics, getInContext);
}

void irgen::bindFromGenericRequirementsBuffer(IRGenFunction &IGF,
                                    ArrayRef<GenericRequirement> requirements,
                                    Address buffer,
                                    GetTypeParameterInContextFn getInContext) {
  if (requirements.empty()) return;

  // Cast the buffer to %type**.
  buffer = IGF.Builder.CreateElementBitCast(buffer, IGF.IGM.TypeMetadataPtrTy);

  for (auto index : indices(requirements)) {
    // GEP to the appropriate slot.
    Address slot = buffer;
    if (index != 0) {
      slot = IGF.Builder.CreateConstArrayGEP(slot, index,
                                             IGF.IGM.getPointerSize());
    }

    // Cast if necessary.
    if (requirements[index].Protocol) {
      slot = IGF.Builder.CreateElementBitCast(slot, IGF.IGM.WitnessTablePtrTy);
    }

    llvm::Value *value = IGF.Builder.CreateLoad(slot);
    bindGenericRequirement(IGF, requirements[index], value, getInContext);
  }
}

void irgen::bindGenericRequirement(IRGenFunction &IGF,
                                   GenericRequirement requirement,
                                   llvm::Value *value,
                                   GetTypeParameterInContextFn getInContext) {
  // Get the corresponding context type.
  auto type = getInContext(requirement.TypeParameter);

  if (auto proto = requirement.Protocol) {
    assert(isa<ArchetypeType>(type));
    assert(value->getType() == IGF.IGM.WitnessTablePtrTy);
    setProtocolWitnessTableName(IGF.IGM, value, type, proto);
    auto kind = LocalTypeDataKind::forAbstractProtocolWitnessTable(proto);
    IGF.setUnscopedLocalTypeData(type, kind, value);
  } else {
    assert(value->getType() == IGF.IGM.TypeMetadataPtrTy);
    setTypeMetadataName(IGF.IGM, value, type);
    IGF.bindLocalTypeDataFromTypeMetadata(type, IsExact, value);
  }
}

namespace {
  /// A class for expanding a polymorphic signature.
  class ExpandPolymorphicSignature : public PolymorphicConvention {
  public:
    ExpandPolymorphicSignature(IRGenModule &IGM, CanSILFunctionType fn)
      : PolymorphicConvention(IGM, fn) {}

    void expand(SmallVectorImpl<llvm::Type*> &out) {
      for (auto &source : getSources())
        addEarlySource(source, out);

      enumerateUnfulfilledRequirements([&](GenericRequirement reqt) {
        out.push_back(reqt.Protocol ? IGM.WitnessTablePtrTy
                                    : IGM.TypeMetadataPtrTy);
      });
    }

  private:
    /// Add signature elements for the source metadata.
    void addEarlySource(const MetadataSource &source,
                        SmallVectorImpl<llvm::Type*> &out) {
      switch (source.getKind()) {
      case MetadataSource::Kind::ClassPointer: return; // already accounted for
      case MetadataSource::Kind::Metadata: return; // already accounted for
      case MetadataSource::Kind::GenericLValueMetadata:
        return out.push_back(IGM.TypeMetadataPtrTy);
      case MetadataSource::Kind::SelfMetadata:
      case MetadataSource::Kind::SelfWitnessTable:
        return; // handled as a special case in expand()
      }
      llvm_unreachable("bad source kind");
    }
  };
}

/// Given a generic signature, add the argument types required in order to call it.
void irgen::expandPolymorphicSignature(IRGenModule &IGM,
                                       CanSILFunctionType polyFn,
                                       SmallVectorImpl<llvm::Type*> &out) {
  ExpandPolymorphicSignature(IGM, polyFn).expand(out);
}

void irgen::expandTrailingWitnessSignature(IRGenModule &IGM,
                                           CanSILFunctionType polyFn,
                                           SmallVectorImpl<llvm::Type*> &out) {
  assert(polyFn->getRepresentation()
          == SILFunctionTypeRepresentation::WitnessMethod);

  assert(getTrailingWitnessSignatureLength(IGM, polyFn) == 2);

  // A witness method always provides Self.
  out.push_back(IGM.TypeMetadataPtrTy);

  // A witness method always provides the witness table for Self.
  out.push_back(IGM.WitnessTablePtrTy);
}

void
irgen::emitWitnessMethodValue(IRGenFunction &IGF,
                              CanType baseTy,
                              llvm::Value **baseMetadataCache,
                              SILDeclRef member,
                              ProtocolConformanceRef conformance,
                              Explosion &out) {
  auto fn = cast<AbstractFunctionDecl>(member.getDecl());
  auto fnProto = cast<ProtocolDecl>(fn->getDeclContext());
  
  conformance = conformance.getInherited(fnProto);

  // Find the witness table.
  // FIXME conformance for concrete type
  llvm::Value *wtable = emitWitnessTableRef(IGF, baseTy, baseMetadataCache,
                                            conformance);

  // Find the witness we're interested in.
  auto &fnProtoInfo = IGF.IGM.getProtocolInfo(conformance.getRequirement());
  auto index = fnProtoInfo.getWitnessEntry(fn).getFunctionIndex();
  llvm::Value *witness = emitInvariantLoadOfOpaqueWitness(IGF, wtable, index);
  
  // Cast the witness pointer to i8*.
  witness = IGF.Builder.CreateBitCast(witness, IGF.IGM.Int8PtrTy);
  
  // Build the value.
  out.add(witness);
  out.add(wtable);
}

llvm::FunctionType *IRGenModule::getAssociatedTypeMetadataAccessFunctionTy() {
  if (AssociatedTypeMetadataAccessFunctionTy)
    return AssociatedTypeMetadataAccessFunctionTy;

  auto accessorTy = llvm::FunctionType::get(TypeMetadataPtrTy,
                                            { TypeMetadataPtrTy,
                                              WitnessTablePtrTy },
                                            /*varargs*/ false);
  AssociatedTypeMetadataAccessFunctionTy = accessorTy;
  return accessorTy;
}

llvm::Value *irgen::emitAssociatedTypeMetadataRef(IRGenFunction &IGF,
                                                  llvm::Value *parentMetadata,
                                                  llvm::Value *wtable,
                                          AssociatedTypeDecl *associatedType) {
  auto &pi = IGF.IGM.getProtocolInfo(associatedType->getProtocol());
  auto index = pi.getWitnessEntry(associatedType).getAssociatedTypeIndex();
  llvm::Value *witness = emitInvariantLoadOfOpaqueWitness(IGF, wtable, index);

  // Cast the witness to the appropriate function type.
  auto witnessTy = IGF.IGM.getAssociatedTypeMetadataAccessFunctionTy();
  witness = IGF.Builder.CreateBitCast(witness, witnessTy->getPointerTo());

  // Call the accessor.
  auto call = IGF.Builder.CreateCall(witness, { parentMetadata, wtable });
  call->setDoesNotThrow();
  call->setCallingConv(IGF.IGM.DefaultCC);

  return call;
}

llvm::FunctionType *
IRGenModule::getAssociatedTypeWitnessTableAccessFunctionTy() {
  if (AssociatedTypeWitnessTableAccessFunctionTy)
    return AssociatedTypeWitnessTableAccessFunctionTy;

  // The associated type metadata is passed first so that this function is
  // CC-compatible with a conformance's witness table access function.
  auto accessorTy = llvm::FunctionType::get(WitnessTablePtrTy,
                                            { TypeMetadataPtrTy,
                                              TypeMetadataPtrTy,
                                              WitnessTablePtrTy },
                                            /*varargs*/ false);
  AssociatedTypeWitnessTableAccessFunctionTy = accessorTy;
  return accessorTy;
}

/// Call an associated-type witness table access function.  Does not do
/// any caching or drill down to implied protocols.
llvm::Value *
irgen::emitAssociatedTypeWitnessTableRef(IRGenFunction &IGF,
                                         llvm::Value *parentMetadata,
                                         llvm::Value *wtable,
                                         AssociatedTypeDecl *associatedType,
                                         llvm::Value *associatedTypeMetadata,
                                         ProtocolDecl *associatedProtocol) {
  auto &pi = IGF.IGM.getProtocolInfo(associatedType->getProtocol());
  auto index = pi.getWitnessEntry(associatedType)
                 .getAssociatedTypeWitnessTableIndex(associatedProtocol);
  llvm::Value *witness = emitInvariantLoadOfOpaqueWitness(IGF, wtable, index);

  // Cast the witness to the appropriate function type.
  auto witnessTy = IGF.IGM.getAssociatedTypeWitnessTableAccessFunctionTy();
  witness = IGF.Builder.CreateBitCast(witness, witnessTy->getPointerTo());

  // Call the accessor.
  auto call = IGF.Builder.CreateCall(witness,
                            { associatedTypeMetadata, parentMetadata, wtable });
  call->setDoesNotThrow();
  call->setCallingConv(IGF.IGM.DefaultCC);

  return call;
}
