//===--- MetadataLayout.cpp - Metadata construct layout -------------------===//
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
//
//  This file includes code for laying out type metadata.
// 
//  It also implements certain low-level access routines for type metadata.
//  These routines are generally declared in one of two different places:
//
//    - Mid-level routines to extract data from metadata are declared in
//      GenMeta.h.  This file is a sort of sub-module of GenMeta.cpp.
//
//    - Low-level routines to project the addresses of fields in metadata
//      are declared in MetadataLayout.h.
//
//===----------------------------------------------------------------------===//

#include "MetadataLayout.h"
#include "GenMeta.h"

#include "ClassMetadataVisitor.h"
#include "EnumMetadataVisitor.h"
#include "IRGenFunction.h"
#include "StructMetadataVisitor.h"

#include "swift/Basic/LLVM.h"
#include "llvm/ADT/Optional.h"

using namespace swift;
using namespace irgen;

namespace {

template <class Impl, template <class> class Base>
class LayoutScanner : public Base<Impl> {
  Optional<Size> AddressPoint;

protected:
  template <class... As>
  LayoutScanner(As &&... args) : Base<Impl>(std::forward<As>(args)...) {}

public:
  using StoredOffset = MetadataLayout::StoredOffset;

  void noteAddressPoint() { AddressPoint = this->NextOffset; }
  StoredOffset getNextOffset() const {
    return StoredOffset(this->NextOffset - AddressPoint.getValue());
  }

  Size getAddressPoint() const {
    return *AddressPoint;
  }

  MetadataSize getMetadataSize() const {
    assert(AddressPoint.hasValue() && !AddressPoint->isInvalid()
           && "did not find address point?!");
    assert(*AddressPoint < this->NextOffset
           && "address point is after end?!");
    return {this->NextOffset, *AddressPoint};
  }
};

}

ClassMetadataLayout &IRGenModule::getMetadataLayout(ClassDecl *decl) {
  return cast<ClassMetadataLayout>(
                        getMetadataLayout(static_cast<NominalTypeDecl*>(decl)));
}

EnumMetadataLayout &IRGenModule::getMetadataLayout(EnumDecl *decl) {
  return cast<EnumMetadataLayout>(
                        getMetadataLayout(static_cast<NominalTypeDecl*>(decl)));
}

StructMetadataLayout &IRGenModule::getMetadataLayout(StructDecl *decl) {
  return cast<StructMetadataLayout>(
                        getMetadataLayout(static_cast<NominalTypeDecl*>(decl)));
}

NominalMetadataLayout &IRGenModule::getMetadataLayout(NominalTypeDecl *decl) {
  auto &entry = MetadataLayouts[decl];
  if (!entry) {
    if (auto theClass = dyn_cast<ClassDecl>(decl)) {
      entry = new ClassMetadataLayout(*this, theClass);
    } else if (auto theEnum = dyn_cast<EnumDecl>(decl)) {
      entry = new EnumMetadataLayout(*this, theEnum);
    } else if (auto theStruct = dyn_cast<StructDecl>(decl)) {
      entry = new StructMetadataLayout(*this, theStruct);
    } else {
      llvm_unreachable("bad nominal type!");
    }
  }
  return *cast<NominalMetadataLayout>(entry);
}

void IRGenModule::destroyMetadataLayoutMap() {
  for (auto &entry : MetadataLayouts) {
    entry.second->destroy();
  }
}

void MetadataLayout::destroy() const {
  switch (getKind()) {
  case Kind::Class:
    delete cast<ClassMetadataLayout>(this);
    return;

  case Kind::Struct:
    delete cast<StructMetadataLayout>(this);
    return;

  case Kind::Enum:
    delete cast<EnumMetadataLayout>(this);
    return;
  }
  llvm_unreachable("bad kind");
}

/******************************* NOMINAL TYPES ********************************/

Size
NominalMetadataLayout::getStaticGenericRequirementsOffset() const {
  assert(GenericRequirements.isValid());
  assert(GenericRequirements.isStatic() && "resilient metadata layout unsupported!");
  return GenericRequirements.getStaticOffset();
}

Offset
NominalMetadataLayout::getGenericRequirementsOffset(IRGenFunction &IGF) const {
  assert(GenericRequirements.isValid());
  assert(GenericRequirements.isStatic() && "resilient metadata layout unsupported!");
  return Offset(GenericRequirements.getStaticOffset());
}

static llvm::Value *emitLoadOfGenericRequirement(IRGenFunction &IGF,
                                                 llvm::Value *metadata,
                                                 NominalTypeDecl *decl,
                                                 unsigned reqtIndex,
                                                 llvm::Type *reqtTy) {
  auto offset =
    IGF.IGM.getMetadataLayout(decl).getGenericRequirementsOffset(IGF);
  offset = offset.offsetBy(IGF, Offset(reqtIndex * IGF.IGM.getPointerSize()));

  auto slot = IGF.emitAddressAtOffset(metadata, offset, reqtTy,
                                      IGF.IGM.getPointerAlignment());
  auto witness = IGF.emitInvariantLoad(slot);
  return witness;
}

/// Given a reference to nominal type metadata of the given type,
/// derive a reference to the nth argument metadata.  The type must
/// have generic arguments.
llvm::Value *irgen::emitArgumentMetadataRef(IRGenFunction &IGF,
                                            NominalTypeDecl *decl,
                                      const GenericTypeRequirements &reqts,
                                            unsigned reqtIndex,
                                            llvm::Value *metadata) {
  assert(reqts.getRequirements()[reqtIndex].Protocol == nullptr);
  return emitLoadOfGenericRequirement(IGF, metadata, decl, reqtIndex,
                                      IGF.IGM.TypeMetadataPtrTy);
}

/// Given a reference to nominal type metadata of the given type,
/// derive a reference to a protocol witness table for the nth
/// argument metadata.  The type must have generic arguments.
llvm::Value *irgen::emitArgumentWitnessTableRef(IRGenFunction &IGF,
                                                NominalTypeDecl *decl,
                                          const GenericTypeRequirements &reqts,
                                                unsigned reqtIndex,
                                                llvm::Value *metadata) {
  assert(reqts.getRequirements()[reqtIndex].Protocol != nullptr);
  return emitLoadOfGenericRequirement(IGF, metadata, decl, reqtIndex,
                                      IGF.IGM.WitnessTablePtrTy);
}

Address irgen::emitAddressOfFieldOffsetVector(IRGenFunction &IGF,
                                              llvm::Value *metadata,
                                              NominalTypeDecl *decl) {
  auto &layout = IGF.IGM.getMetadataLayout(decl);
  auto offset = [&]() {
    if (isa<ClassDecl>(decl)) {
      return cast<ClassMetadataLayout>(layout)
        .getFieldOffsetVectorOffset(IGF);
    } else {
      assert(isa<StructDecl>(decl));
      return cast<StructMetadataLayout>(layout)
        .getFieldOffsetVectorOffset();
    }
  }();

  return IGF.emitAddressAtOffset(metadata, offset, IGF.IGM.SizeTy,
                                 IGF.IGM.getPointerAlignment());
}

/********************************** CLASSES ***********************************/

ClassMetadataLayout::ClassMetadataLayout(IRGenModule &IGM, ClassDecl *decl)
    : NominalMetadataLayout(Kind::Class) {

  struct Scanner : LayoutScanner<Scanner, ClassMetadataScanner> {
    using super = LayoutScanner;

    ClassMetadataLayout &Layout;
    Scanner(IRGenModule &IGM, ClassDecl *decl, ClassMetadataLayout &layout)
      : super(IGM, decl), Layout(layout) {}

    void addInstanceSize() {
      Layout.InstanceSize = getNextOffset();
      super::addInstanceSize();
    }

    void addInstanceAlignMask() {
      Layout.InstanceAlignMask = getNextOffset();
      super::addInstanceAlignMask();
    }

    void noteStartOfGenericRequirements(ClassDecl *forClass) {
      if (forClass == Target)
        Layout.GenericRequirements = getNextOffset();
      super::noteStartOfGenericRequirements(forClass);
    }

    void addMethod(SILDeclRef fn) {
      if (fn.getDecl()->getDeclContext() == Target)
        Layout.MethodInfos.try_emplace(fn, getNextOffset());
      super::addMethod(fn);
    }

    void noteStartOfFieldOffsets(ClassDecl *forClass) {
      if (forClass == Target)
        Layout.FieldOffsetVector = getNextOffset();
      super::noteStartOfFieldOffsets(forClass);
    }

    void addFieldOffset(VarDecl *field) {
      if (field->getDeclContext() == Target)
        Layout.FieldOffsets.try_emplace(field, getNextOffset());
      super::addFieldOffset(field);
    }

    void addVTableEntries(ClassDecl *forClass) {
      if (forClass == Target)
        Layout.VTableOffset = getNextOffset();
      super::addVTableEntries(forClass);
    }

    void layout() {
      super::layout();
      Layout.TheSize = getMetadataSize();
    }
  };

  Scanner(IGM, decl, *this).layout();
}

Size ClassMetadataLayout::getInstanceSizeOffset() const {
  assert(InstanceSize.isStatic());
  return InstanceSize.getStaticOffset();
}

Size ClassMetadataLayout::getInstanceAlignMaskOffset() const {
  assert(InstanceAlignMask.isStatic());
  return InstanceAlignMask.getStaticOffset();
}

ClassMetadataLayout::MethodInfo
ClassMetadataLayout::getMethodInfo(IRGenFunction &IGF, SILDeclRef method) const{
  auto &stored = getStoredMethodInfo(method);

  assert(stored.TheOffset.isStatic() &&
         "resilient class metadata layout unsupported!");
  auto offset = Offset(stored.TheOffset.getStaticOffset());

  return MethodInfo(offset);
}

Size ClassMetadataLayout::getStaticMethodOffset(SILDeclRef method) const{
  auto &stored = getStoredMethodInfo(method);

  assert(stored.TheOffset.isStatic() &&
         "resilient class metadata layout unsupported!");
  return stored.TheOffset.getStaticOffset();
}

Size
ClassMetadataLayout::getStaticVTableOffset() const {
  // TODO: if class is resilient, return the offset relative to the start
  // of immediate class metadata
  assert(VTableOffset.isStatic());
  return VTableOffset.getStaticOffset();
}

Offset
ClassMetadataLayout::getVTableOffset(IRGenFunction &IGF) const {
  // TODO: implement resilient metadata layout
  assert(VTableOffset.isStatic());
  return Offset(VTableOffset.getStaticOffset());
}

Offset ClassMetadataLayout::getFieldOffset(IRGenFunction &IGF,
                                           VarDecl *field) const {
  // TODO: implement resilient metadata layout
  return Offset(getStaticFieldOffset(field));
}
Size ClassMetadataLayout::getStaticFieldOffset(VarDecl *field) const {
  auto &stored = getStoredFieldOffset(field);
  assert(stored.isStatic() && "resilient class metadata layout unsupported!");
  return stored.getStaticOffset();
}

Size
ClassMetadataLayout::getStaticFieldOffsetVectorOffset() const {
  // TODO: if class is resilient, return the offset relative to the start
  // of immediate class metadata
  assert(FieldOffsetVector.isStatic());
  return FieldOffsetVector.getStaticOffset();
}

Offset
ClassMetadataLayout::getFieldOffsetVectorOffset(IRGenFunction &IGF) const {
  // TODO: implement resilient metadata layout
  assert(FieldOffsetVector.isStatic());
  return Offset(FieldOffsetVector.getStaticOffset());
}

Size irgen::getClassFieldOffsetOffset(IRGenModule &IGM, ClassDecl *theClass,
                                      VarDecl *field) {
  return IGM.getMetadataLayout(theClass).getStaticFieldOffset(field);
}

/// Given a reference to class metadata of the given type,
/// compute the field offset for a stored property.
/// The type must have dependent generic layout.
llvm::Value *irgen::emitClassFieldOffset(IRGenFunction &IGF,
                                         ClassDecl *theClass,
                                         VarDecl *field,
                                         llvm::Value *metadata) {
  auto slot = emitAddressOfClassFieldOffset(IGF, metadata, theClass, field);
  return IGF.emitInvariantLoad(slot);
}

Address irgen::emitAddressOfClassFieldOffset(IRGenFunction &IGF,
                                             llvm::Value *metadata,
                                              ClassDecl *theClass,
                                              VarDecl *field) {
  auto offset = IGF.IGM.getMetadataLayout(theClass).getFieldOffset(IGF, field);
  auto slot = IGF.emitAddressAtOffset(metadata, offset, IGF.IGM.SizeTy,
                                      IGF.IGM.getPointerAlignment());
  return slot;
}

/*********************************** ENUMS ************************************/

EnumMetadataLayout::EnumMetadataLayout(IRGenModule &IGM, EnumDecl *decl)
    : NominalMetadataLayout(Kind::Enum) {

  struct Scanner : LayoutScanner<Scanner, EnumMetadataScanner> {
    using super = LayoutScanner;

    EnumMetadataLayout &Layout;
    Scanner(IRGenModule &IGM, EnumDecl *decl, EnumMetadataLayout &layout)
      : super(IGM, decl), Layout(layout) {}

    void addPayloadSize() {
      Layout.PayloadSizeOffset = getNextOffset();
      super::addPayloadSize();
    }

    void noteStartOfGenericRequirements() {
      Layout.GenericRequirements = getNextOffset();
      super::noteStartOfGenericRequirements();
    }

    void layout() {
      super::layout();
      Layout.TheSize = getMetadataSize();
    }
  };

  Scanner(IGM, decl, *this).layout();
}

Offset
EnumMetadataLayout::getPayloadSizeOffset() const {
  assert(PayloadSizeOffset.isStatic());
  return Offset(PayloadSizeOffset.getStaticOffset());
}

/********************************** STRUCTS ***********************************/

StructMetadataLayout::StructMetadataLayout(IRGenModule &IGM, StructDecl *decl)
    : NominalMetadataLayout(Kind::Struct) {

  struct Scanner : LayoutScanner<Scanner, StructMetadataScanner> {
    using super = LayoutScanner;

    StructMetadataLayout &Layout;
    Scanner(IRGenModule &IGM, StructDecl *decl, StructMetadataLayout &layout)
      : super(IGM, decl), Layout(layout) {}

    void noteStartOfGenericRequirements() {
      Layout.GenericRequirements = getNextOffset();
      super::noteStartOfGenericRequirements();
    }

    void noteStartOfFieldOffsets() {
      Layout.FieldOffsetVector = getNextOffset();
      super::noteStartOfFieldOffsets();
    }

    void addFieldOffset(VarDecl *field) {
      Layout.FieldOffsets.try_emplace(field, getNextOffset());
      super::addFieldOffset(field);
    }

    void layout() {
      super::layout();
      Layout.TheSize = getMetadataSize();
    }
  };

  Scanner(IGM, decl, *this).layout();
}

Offset StructMetadataLayout::getFieldOffset(IRGenFunction &IGF,
                                            VarDecl *field) const {
  // TODO: implement resilient metadata layout
  return Offset(getStaticFieldOffset(field));
}
Size StructMetadataLayout::getStaticFieldOffset(VarDecl *field) const {
  auto &stored = getStoredFieldOffset(field);
  assert(stored.isStatic() && "resilient struct metadata layout unsupported!");
  return stored.getStaticOffset();
}

Offset
StructMetadataLayout::getFieldOffsetVectorOffset() const {
  assert(FieldOffsetVector.isStatic());
  return Offset(FieldOffsetVector.getStaticOffset());
}
