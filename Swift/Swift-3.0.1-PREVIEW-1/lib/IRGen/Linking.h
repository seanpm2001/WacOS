//===--- Linking.h - Common declarations for link information ---*- C++ -*-===//
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
// This file defines structures and routines used when creating global
// entities that are placed in the LLVM module, potentially with
// external linkage.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IRGEN_LINKING_H
#define SWIFT_IRGEN_LINKING_H

#include "swift/AST/Types.h"
#include "swift/AST/Decl.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILGlobalVariable.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/GlobalValue.h"
#include "DebugTypeInfo.h"
#include "IRGen.h"
#include "IRGenModule.h"
#include "ValueWitness.h"

namespace llvm {
  class AttributeSet;
  class Value;
  class FunctionType;
}

namespace swift {  
namespace irgen {
class TypeInfo;
class IRGenModule;

/// Selector for type metadata symbol kinds.
enum class TypeMetadataAddress {
  AddressPoint,
  FullMetadata,
};

/// A link entity is some sort of named declaration, combined with all
/// the information necessary to distinguish specific implementations
/// of the declaration from each other.
///
/// For example, functions may be uncurried at different levels, each of
/// which potentially creates a different top-level function.
class LinkEntity {
  /// ValueDecl*, SILFunction*, or TypeBase*, depending on Kind.
  void *Pointer;

  /// ProtocolConformance*, depending on Kind.
  void *SecondaryPointer;

  /// A hand-rolled bitfield with the following layout:
  unsigned Data;

  enum : unsigned {
    KindShift = 0, KindMask = 0xFF,

    // These fields appear in decl kinds.
    UncurryLevelShift = 8, UncurryLevelMask = 0xFF00,

    // This field appears in the ValueWitness kind.
    ValueWitnessShift = 8, ValueWitnessMask = 0xFF00,

    // These fields appear in the FieldOffset kind.
    IsIndirectShift = 8, IsIndirectMask = 0x0100,

    // These fields appear in the TypeMetadata kind.
    MetadataAddressShift = 8, MetadataAddressMask = 0x0300,
    IsPatternShift = 10, IsPatternMask = 0x0400,

    // This field appears in associated type access function kinds.
    AssociatedTypeIndexShift = 8, AssociatedTypeIndexMask = ~KindMask,
  };
#define LINKENTITY_SET_FIELD(field, value) (value << field##Shift)
#define LINKENTITY_GET_FIELD(value, field) ((value & field##Mask) >> field##Shift)

  enum class Kind {
    /// A function.
    /// The pointer is a FuncDecl*.
    Function,
    
    /// The offset to apply to a witness table or metadata object
    /// in order to find the information for a declaration.  The
    /// pointer is a ValueDecl*.
    WitnessTableOffset,

    /// A field offset.  The pointer is a VarDecl*.
    FieldOffset,

    /// An Objective-C class reference.  The pointer is a ClassDecl*.
    ObjCClass,

    /// An Objective-C class reference reference.  The pointer is a ClassDecl*.
    ObjCClassRef,

    /// An Objective-C metaclass reference.  The pointer is a ClassDecl*.
    ObjCMetaclass,

    /// A swift metaclass-stub reference.  The pointer is a ClassDecl*.
    SwiftMetaclassStub,

    /// The nominal type descriptor for a nominal type.
    /// The pointer is a NominalTypeDecl*.
    NominalTypeDescriptor,
    
    /// The protocol descriptor for a protocol type.
    /// The pointer is a ProtocolDecl*.
    ProtocolDescriptor,

    /// Some other kind of declaration.
    /// The pointer is a Decl*.
    Other,

    /// A SIL function. The pointer is a SILFunction*.
    SILFunction,
    
    /// A SIL global variable. The pointer is a SILGlobalVariable*.
    SILGlobalVariable,

    // These next few are protocol-conformance kinds.

    /// A direct protocol witness table. The secondary pointer is a
    /// ProtocolConformance*.
    DirectProtocolWitnessTable,

    /// A witness accessor function. The secondary pointer is a
    /// ProtocolConformance*.
    ProtocolWitnessTableAccessFunction,

    /// A generic protocol witness table cache.  The secondary pointer is a
    /// ProtocolConformance*.
    GenericProtocolWitnessTableCache,

    /// The instantiation function for a generic protocol witness table.
    /// The secondary pointer is a ProtocolConformance*.
    GenericProtocolWitnessTableInstantiationFunction,
    
    /// A function which returns the type metadata for the associated type
    /// of a protocol.  The secondary pointer is a ProtocolConformance*.
    /// The index of the associated type declaration is stored in the data.
    AssociatedTypeMetadataAccessFunction,

    /// A function which returns the witness table for a protocol-constrained
    /// associated type of a protocol.  The secondary pointer is a
    /// ProtocolConformance*.  The primary pointer is a ProtocolDecl*.
    /// The index of the associated type declaration is stored in the data.
    AssociatedTypeWitnessTableAccessFunction,

    // These are both type kinds and protocol-conformance kinds.

    /// A lazy protocol witness accessor function. The pointer is a
    /// canonical TypeBase*, and the secondary pointer is a
    /// ProtocolConformance*.
    ProtocolWitnessTableLazyAccessFunction,

    /// A lazy protocol witness cache variable. The pointer is a
    /// canonical TypeBase*, and the secondary pointer is a
    /// ProtocolConformance*.
    ProtocolWitnessTableLazyCacheVariable,
        
    // Everything following this is a type kind.

    /// A value witness for a type.
    /// The pointer is a canonical TypeBase*.
    ValueWitness,

    /// The value witness table for a type.
    /// The pointer is a canonical TypeBase*.
    ValueWitnessTable,

    /// The metadata or metadata template for a type.
    /// The pointer is a canonical TypeBase*.
    TypeMetadata,

    /// An access function for type metadata.
    /// The pointer is a canonical TypeBase*.
    TypeMetadataAccessFunction,

    /// A lazy cache variable for type metadata.
    /// The pointer is a canonical TypeBase*.
    TypeMetadataLazyCacheVariable,

    /// A foreign type metadata candidate.
    /// The pointer is a canonical TypeBase*.
    ForeignTypeMetadataCandidate,
    
    /// A type which is being mangled just for its string.
    /// The pointer is a canonical TypeBase*.
    TypeMangling,
  };
  friend struct llvm::DenseMapInfo<LinkEntity>;

  static bool isFunction(ValueDecl *decl) {
    return (isa<FuncDecl>(decl) || isa<EnumElementDecl>(decl) ||
            isa<ConstructorDecl>(decl));
  }

  static bool hasGetterSetter(ValueDecl *decl) {
    return (isa<VarDecl>(decl) || isa<SubscriptDecl>(decl));
  }

  Kind getKind() const {
    return Kind(LINKENTITY_GET_FIELD(Data, Kind));
  }

  static bool isDeclKind(Kind k) {
    return k <= Kind::Other;
  }
  static bool isTypeKind(Kind k) {
    return k >= Kind::ProtocolWitnessTableLazyAccessFunction;
  }
  
  static bool isProtocolConformanceKind(Kind k) {
    return k >= Kind::DirectProtocolWitnessTable
      && k <= Kind::ProtocolWitnessTableLazyCacheVariable;
  }

  void setForDecl(Kind kind, ValueDecl *decl, unsigned uncurryLevel) {
    assert(isDeclKind(kind));
    Pointer = decl;
    SecondaryPointer = nullptr;
    Data = LINKENTITY_SET_FIELD(Kind, unsigned(kind))
         | LINKENTITY_SET_FIELD(UncurryLevel, uncurryLevel);
  }

  void setForProtocolConformance(Kind kind, const ProtocolConformance *c) {
    assert(isProtocolConformanceKind(kind) && !isTypeKind(kind));
    Pointer = nullptr;
    SecondaryPointer = const_cast<void*>(static_cast<const void*>(c));
    Data = LINKENTITY_SET_FIELD(Kind, unsigned(kind));
  }

  void setForProtocolConformanceAndType(Kind kind, const ProtocolConformance *c,
                                        CanType type) {
    assert(isProtocolConformanceKind(kind) && isTypeKind(kind));
    Pointer = type.getPointer();
    SecondaryPointer = const_cast<void*>(static_cast<const void*>(c));
    Data = LINKENTITY_SET_FIELD(Kind, unsigned(kind));
  }

  void setForProtocolConformanceAndAssociatedType(Kind kind,
                                                  const ProtocolConformance *c,
                                                  AssociatedTypeDecl *associate,
                                   ProtocolDecl *associatedProtocol = nullptr) {
    assert(isProtocolConformanceKind(kind));
    Pointer = associatedProtocol;
    SecondaryPointer = const_cast<void*>(static_cast<const void*>(c));
    Data = LINKENTITY_SET_FIELD(Kind, unsigned(kind)) |
           LINKENTITY_SET_FIELD(AssociatedTypeIndex,
                                getAssociatedTypeIndex(c, associate));
  }

  // We store associated types using their index in their parent protocol
  // in order to avoid bloating LinkEntity out to three key pointers.
  static unsigned getAssociatedTypeIndex(const ProtocolConformance *conformance,
                                         AssociatedTypeDecl *associate) {
    assert(conformance->getProtocol() == associate->getProtocol());
    unsigned result = 0;
    for (auto requirement : associate->getProtocol()->getMembers()) {
      if (requirement == associate) return result;
      if (isa<AssociatedTypeDecl>(requirement)) result++;
    }
    llvm_unreachable("didn't find associated type in protocol?");
  }

  static AssociatedTypeDecl *
  getAssociatedTypeByIndex(const ProtocolConformance *conformance,
                           unsigned index) {
    for (auto requirement : conformance->getProtocol()->getMembers()) {
      if (auto associate = dyn_cast<AssociatedTypeDecl>(requirement)) {
        if (index == 0) return associate;
        index--;
      }
    }
    llvm_unreachable("didn't find associated type in protocol?");
  }

  void setForType(Kind kind, CanType type) {
    assert(isTypeKind(kind));
    Pointer = type.getPointer();
    SecondaryPointer = nullptr;
    Data = LINKENTITY_SET_FIELD(Kind, unsigned(kind));
  }

  LinkEntity() = default;

public:
  static LinkEntity forNonFunction(ValueDecl *decl) {
    assert(!isFunction(decl));

    LinkEntity entity;
    entity.setForDecl(Kind::Other, decl, 0);
    return entity;
  }

  static LinkEntity forWitnessTableOffset(ValueDecl *decl,
                                          unsigned uncurryLevel) {
    LinkEntity entity;
    entity.setForDecl(Kind::WitnessTableOffset, decl, uncurryLevel);
    return entity;
  }

  static LinkEntity forFieldOffset(VarDecl *decl, bool isIndirect) {
    LinkEntity entity;
    entity.Pointer = decl;
    entity.SecondaryPointer = nullptr;
    entity.Data = LINKENTITY_SET_FIELD(Kind, unsigned(Kind::FieldOffset))
                | LINKENTITY_SET_FIELD(IsIndirect, unsigned(isIndirect));
    return entity;
  }

  static LinkEntity forObjCClassRef(ClassDecl *decl) {
    LinkEntity entity;
    entity.setForDecl(Kind::ObjCClassRef, decl, 0);
    return entity;
  }

  static LinkEntity forObjCClass(ClassDecl *decl) {
    LinkEntity entity;
    entity.setForDecl(Kind::ObjCClass, decl, 0);
    return entity;
  }

  static LinkEntity forObjCMetaclass(ClassDecl *decl) {
    LinkEntity entity;
    entity.setForDecl(Kind::ObjCMetaclass, decl, 0);
    return entity;
  }

  static LinkEntity forSwiftMetaclassStub(ClassDecl *decl) {
    LinkEntity entity;
    entity.setForDecl(Kind::SwiftMetaclassStub, decl, 0);
    return entity;
  }

  static LinkEntity forTypeMetadata(CanType concreteType,
                                    TypeMetadataAddress addr,
                                    bool isPattern) {
    LinkEntity entity;
    entity.Pointer = concreteType.getPointer();
    entity.SecondaryPointer = nullptr;
    entity.Data = LINKENTITY_SET_FIELD(Kind, unsigned(Kind::TypeMetadata))
                | LINKENTITY_SET_FIELD(MetadataAddress, unsigned(addr))
                | LINKENTITY_SET_FIELD(IsPattern, unsigned(isPattern));
    return entity;
  }

  static LinkEntity forTypeMetadataAccessFunction(CanType type) {
    LinkEntity entity;
    entity.setForType(Kind::TypeMetadataAccessFunction, type);
    return entity;
  }

  static LinkEntity forTypeMetadataLazyCacheVariable(CanType type) {
    LinkEntity entity;
    entity.setForType(Kind::TypeMetadataLazyCacheVariable, type);
    return entity;
  }

  static LinkEntity forForeignTypeMetadataCandidate(CanType type) {
    LinkEntity entity;
    entity.setForType(Kind::ForeignTypeMetadataCandidate, type);
    return entity;
  }
  
  static LinkEntity forNominalTypeDescriptor(NominalTypeDecl *decl) {
    LinkEntity entity;
    entity.setForDecl(Kind::NominalTypeDescriptor, decl, 0);
    return entity;
  }
  
  static LinkEntity forProtocolDescriptor(ProtocolDecl *decl) {
    LinkEntity entity;
    entity.setForDecl(Kind::ProtocolDescriptor, decl, 0);
    return entity;
  }

  static LinkEntity forValueWitness(CanType concreteType, ValueWitness witness) {
    LinkEntity entity;
    entity.Pointer = concreteType.getPointer();
    entity.Data = LINKENTITY_SET_FIELD(Kind, unsigned(Kind::ValueWitness))
                | LINKENTITY_SET_FIELD(ValueWitness, unsigned(witness));
    return entity;
  }

  static LinkEntity forValueWitnessTable(CanType type) {
    LinkEntity entity;
    entity.setForType(Kind::ValueWitnessTable, type);
    return entity;
  }

  static LinkEntity forTypeMangling(CanType type) {
    LinkEntity entity;
    entity.setForType(Kind::TypeMangling, type);
    return entity;
  }

  static LinkEntity forSILFunction(SILFunction *F)
  {
    LinkEntity entity;
    entity.Pointer = F;
    entity.SecondaryPointer = nullptr;
    entity.Data = LINKENTITY_SET_FIELD(Kind, unsigned(Kind::SILFunction));
    return entity;
  }
  
  static LinkEntity forSILGlobalVariable(SILGlobalVariable *G) {
    LinkEntity entity;
    entity.Pointer = G;
    entity.SecondaryPointer = nullptr;
    entity.Data = LINKENTITY_SET_FIELD(Kind, unsigned(Kind::SILGlobalVariable));
    return entity;
  }
  
  static LinkEntity
  forDirectProtocolWitnessTable(const ProtocolConformance *C) {
    LinkEntity entity;
    entity.setForProtocolConformance(Kind::DirectProtocolWitnessTable, C);
    return entity;
  }

  static LinkEntity
  forProtocolWitnessTableAccessFunction(const ProtocolConformance *C) {
    LinkEntity entity;
    entity.setForProtocolConformance(Kind::ProtocolWitnessTableAccessFunction,
                                     C);
    return entity;
  }

  static LinkEntity
  forGenericProtocolWitnessTableCache(const ProtocolConformance *C) {
    LinkEntity entity;
    entity.setForProtocolConformance(Kind::GenericProtocolWitnessTableCache, C);
    return entity;
  }

  static LinkEntity
  forGenericProtocolWitnessTableInstantiationFunction(
                                      const ProtocolConformance *C) {
    LinkEntity entity;
    entity.setForProtocolConformance(
                     Kind::GenericProtocolWitnessTableInstantiationFunction, C);
    return entity;
  }

  static LinkEntity
  forProtocolWitnessTableLazyAccessFunction(const ProtocolConformance *C,
                                            CanType type) {
    LinkEntity entity;
    entity.setForProtocolConformanceAndType(
             Kind::ProtocolWitnessTableLazyAccessFunction, C, type);
    return entity;
  }

  static LinkEntity
  forProtocolWitnessTableLazyCacheVariable(const ProtocolConformance *C,
                                           CanType type) {
    LinkEntity entity;
    entity.setForProtocolConformanceAndType(
             Kind::ProtocolWitnessTableLazyCacheVariable, C, type);
    return entity;
  }

  static LinkEntity
  forAssociatedTypeMetadataAccessFunction(const ProtocolConformance *C,
                                          AssociatedTypeDecl *associate) {
    LinkEntity entity;
    entity.setForProtocolConformanceAndAssociatedType(
                     Kind::AssociatedTypeMetadataAccessFunction, C, associate);
    return entity;
  }

  static LinkEntity
  forAssociatedTypeWitnessTableAccessFunction(const ProtocolConformance *C,
                                              AssociatedTypeDecl *associate,
                                              ProtocolDecl *associateProtocol) {
    LinkEntity entity;
    entity.setForProtocolConformanceAndAssociatedType(
                Kind::AssociatedTypeWitnessTableAccessFunction, C, associate,
                                                      associateProtocol);
    return entity;
  }


  void mangle(llvm::raw_ostream &out) const;
  void mangle(SmallVectorImpl<char> &buffer) const;
  std::string mangleAsString() const;
  SILLinkage getLinkage(IRGenModule &IGM, ForDefinition_t isDefinition) const;

  /// Returns true if this function or global variable is potentially defined
  /// in a different module.
  ///
  bool isAvailableExternally(IRGenModule &IGM) const;

  /// Returns true if this function or global variable may be inlined into
  /// another module.
  ///
  bool isFragile(IRGenModule &IGM) const;

  ValueDecl *getDecl() const {
    assert(isDeclKind(getKind()));
    return reinterpret_cast<ValueDecl*>(Pointer);
  }
  
  SILFunction *getSILFunction() const {
    assert(getKind() == Kind::SILFunction);
    return reinterpret_cast<SILFunction*>(Pointer);
  }

  /// Returns true if this function is only serialized, but not necessarily
  /// code-gen'd. These are fragile transparent functions.
  bool isSILOnly() const {
    if (getKind() != Kind::SILFunction)
      return false;

    SILFunction *F = getSILFunction();
    return F->isTransparent() && F->isDefinition() && F->isFragile();
  }

  SILGlobalVariable *getSILGlobalVariable() const {
    assert(getKind() == Kind::SILGlobalVariable);
    return reinterpret_cast<SILGlobalVariable*>(Pointer);
  }
  
  const ProtocolConformance *getProtocolConformance() const {
    assert(isProtocolConformanceKind(getKind()));
    return reinterpret_cast<ProtocolConformance*>(SecondaryPointer);
  }

  AssociatedTypeDecl *getAssociatedType() const {
    assert(getKind() == Kind::AssociatedTypeMetadataAccessFunction ||
           getKind() == Kind::AssociatedTypeWitnessTableAccessFunction);
    return getAssociatedTypeByIndex(getProtocolConformance(),
                              LINKENTITY_GET_FIELD(Data, AssociatedTypeIndex));
  }

  ProtocolDecl *getAssociatedProtocol() const {
    assert(getKind() == Kind::AssociatedTypeWitnessTableAccessFunction);
    return reinterpret_cast<ProtocolDecl*>(Pointer);
  }

  unsigned getUncurryLevel() const {
    return LINKENTITY_GET_FIELD(Data, UncurryLevel);
  }

  bool isValueWitness() const { return getKind() == Kind::ValueWitness; }
  CanType getType() const {
    assert(isTypeKind(getKind()));
    return CanType(reinterpret_cast<TypeBase*>(Pointer));
  }
  ValueWitness getValueWitness() const {
    assert(getKind() == Kind::ValueWitness);
    return ValueWitness(LINKENTITY_GET_FIELD(Data, ValueWitness));
  }
  TypeMetadataAddress getMetadataAddress() const {
    assert(getKind() == Kind::TypeMetadata);
    return (TypeMetadataAddress)LINKENTITY_GET_FIELD(Data, MetadataAddress);
  }
  bool isMetadataPattern() const {
    assert(getKind() == Kind::TypeMetadata);
    return LINKENTITY_GET_FIELD(Data, IsPattern);
  }
  bool isForeignTypeMetadataCandidate() const {
    return getKind() == Kind::ForeignTypeMetadataCandidate;
  }

  bool isOffsetIndirect() const {
    assert(getKind() == Kind::FieldOffset);
    return LINKENTITY_GET_FIELD(Data, IsIndirect);
  }

  /// Determine whether this entity will be weak-imported.
  bool isWeakImported(Module *module) const {
    if (getKind() == Kind::SILGlobalVariable &&
        getSILGlobalVariable()->getDecl())
      return getSILGlobalVariable()->getDecl()->isWeakImported(module);

    if (getKind() == Kind::SILFunction)
      if (auto clangOwner = getSILFunction()->getClangNodeOwner())
        return clangOwner->isWeakImported(module);

    if (!isDeclKind(getKind()))
      return false;

    return getDecl()->isWeakImported(module);
  }
#undef LINKENTITY_GET_FIELD
#undef LINKENTITY_SET_FIELD
};

/// Encapsulated information about the linkage of an entity.
class LinkInfo {
  LinkInfo() = default;

  llvm::SmallString<32> Name;
  llvm::GlobalValue::LinkageTypes Linkage;
  llvm::GlobalValue::VisibilityTypes Visibility;
  llvm::GlobalValue::DLLStorageClassTypes DLLStorageClass;
  ForDefinition_t ForDefinition;

public:
  /// Compute linkage information for the given 
  static LinkInfo get(IRGenModule &IGM, const LinkEntity &entity,
                      ForDefinition_t forDefinition);

  StringRef getName() const {
    return Name.str();
  }
  llvm::GlobalValue::LinkageTypes getLinkage() const {
    return Linkage;
  }
  llvm::GlobalValue::VisibilityTypes getVisibility() const {
    return Visibility;
  }
  llvm::GlobalValue::DLLStorageClassTypes getDLLStorage() const {
    return DLLStorageClass;
  }

  llvm::Function *createFunction(IRGenModule &IGM,
                                 llvm::FunctionType *fnType,
                                 llvm::CallingConv::ID cc,
                                 const llvm::AttributeSet &attrs,
                                 llvm::Function *insertBefore = nullptr);


  llvm::GlobalVariable *createVariable(IRGenModule &IGM,
                                  llvm::Type *objectType,
                                  Alignment alignment,
                                  DebugTypeInfo DebugType=DebugTypeInfo(),
                                  Optional<SILLocation> DebugLoc = None,
                                  StringRef DebugName = StringRef());

  bool isUsed() const {
    return ForDefinition && isUsed(Linkage, Visibility, DLLStorageClass);
  }

  static bool isUsed(llvm::GlobalValue::LinkageTypes Linkage,
                     llvm::GlobalValue::VisibilityTypes Visibility,
                     llvm::GlobalValue::DLLStorageClassTypes DLLStorage);
};

} // end namespace irgen
} // end namespace swift

/// Allow LinkEntity to be used as a key for a DenseMap.
template <> struct llvm::DenseMapInfo<swift::irgen::LinkEntity> {
  typedef swift::irgen::LinkEntity LinkEntity;
  static LinkEntity getEmptyKey() {
    LinkEntity entity;
    entity.Pointer = nullptr;
    entity.SecondaryPointer = nullptr;
    entity.Data = 0;
    return entity;
  }
  static LinkEntity getTombstoneKey() {
    LinkEntity entity;
    entity.Pointer = nullptr;
    entity.SecondaryPointer = nullptr;
    entity.Data = 1;
    return entity;
  }
  static unsigned getHashValue(const LinkEntity &entity) {
    return DenseMapInfo<void*>::getHashValue(entity.Pointer)
         ^ DenseMapInfo<void*>::getHashValue(entity.SecondaryPointer)
         ^ entity.Data;
  }
  static bool isEqual(const LinkEntity &LHS, const LinkEntity &RHS) {
    return LHS.Pointer == RHS.Pointer &&
           LHS.SecondaryPointer == RHS.SecondaryPointer &&
           LHS.Data == RHS.Data;
  }
};

#endif
