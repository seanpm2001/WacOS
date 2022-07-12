//===--- ClassMetadataVisitor.h - CRTP for class metadata -------*- C++ -*-===//
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
// A CRTP helper class for visiting all of the known fields in a class
// metadata object.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IRGEN_CLASSMETADATAVISITOR_H
#define SWIFT_IRGEN_CLASSMETADATAVISITOR_H

#include "swift/AST/ASTContext.h"
#include "swift/AST/SubstitutionMap.h"
#include "swift/SIL/SILDeclRef.h"
#include "swift/SIL/SILVTableVisitor.h"
#include "IRGen.h"
#include "NominalMetadataVisitor.h"

namespace swift {
namespace irgen {

class IRGenModule;

/// A CRTP class for laying out class metadata.  Note that this does
/// *not* handle the metadata template stuff.
template <class Impl> class ClassMetadataVisitor
    : public NominalMetadataVisitor<Impl>,
      public SILVTableVisitor<Impl> {
  typedef NominalMetadataVisitor<Impl> super;

protected:
  using super::IGM;
  using super::asImpl;

  /// The most-derived class.
  ClassDecl *const Target;

  ClassMetadataVisitor(IRGenModule &IGM, ClassDecl *target)
    : super(IGM), SILVTableVisitor<Impl>(IGM.getSILTypes()), Target(target) {}

public:
  void layout() {
    // HeapMetadata header.
    asImpl().addDestructorFunction();

    // Metadata header.
    super::layout();

    // ClassMetadata header.  In ObjCInterop mode, this must be
    // layout-compatible with an Objective-C class.  The superclass
    // pointer is useful regardless of mode, but the rest of the data
    // isn't necessary.
    // FIXME: Figure out what can be removed altogether in non-objc-interop
    // mode and remove it. rdar://problem/18801263
    asImpl().addSuperClass();
    asImpl().addClassCacheData();
    asImpl().addClassDataPointer();

    asImpl().addClassFlags();
    asImpl().addInstanceAddressPoint();
    asImpl().addInstanceSize();
    asImpl().addInstanceAlignMask();
    asImpl().addRuntimeReservedBits();
    asImpl().addClassSize();
    asImpl().addClassAddressPoint();
    asImpl().addNominalTypeDescriptor();
    asImpl().addIVarDestroyer();

    // Class members.
    addClassMembers(Target, Target->getDeclaredTypeInContext());
  }

  /// Notes the beginning of the field offset vector for a particular ancestor
  /// of a generic-layout class.
  void noteStartOfFieldOffsets(ClassDecl *whichClass) {}

  /// Notes the end of the field offset vector for a particular ancestor
  /// of a generic-layout class.
  void noteEndOfFieldOffsets(ClassDecl *whichClass) {}

private:
  /// Add fields associated with the given class and its bases.
  void addClassMembers(ClassDecl *theClass, Type type) {
    // Add any fields associated with the superclass.
    // NB: We don't apply superclass substitutions to members because we want
    // consistent metadata layout between generic superclasses and concrete
    // subclasses.
    if (Type superclass = type->getSuperclass()) {
      ClassDecl *superclassDecl = superclass->getClassOrBoundGenericClass();
      // Skip superclass fields if superclass is resilient.
      // FIXME: Needs runtime support to ensure the field offset vector is
      // populated correctly.
      if (!IGM.Context.LangOpts.EnableClassResilience ||
          !IGM.isResilient(superclassDecl, ResilienceExpansion::Maximal)) {
        addClassMembers(superclassDecl, superclass);
      }
    }

    // Add space for the generic parameters, if applicable.
    // Note that we only add references for the immediate parameters;
    // parameters for the parent context are handled by the parent.
    asImpl().addGenericFields(theClass, type, theClass);

    // Add vtable entries.
    asImpl().addVTableEntries(theClass);

    // A class only really *needs* a field-offset vector in the
    // metadata if:
    //   - it's in a generic context and
    //   - there might exist a context which
    //     - can access the class's field storage directly and
    //     - sees the class as having a possibly dependent layout.
    //
    // A context which knows that the class does not have a dependent
    // layout should be able to just use a direct field offset
    // (possibly a constant one).
    //
    // But we currently always give classes field-offset vectors,
    // whether they need them or not.
    asImpl().noteStartOfFieldOffsets(theClass);
    for (auto field :
           theClass->getStoredPropertiesAndMissingMemberPlaceholders()) {
      addFieldEntries(field);
    }
    asImpl().noteEndOfFieldOffsets(theClass);
  }
  
private:
  void addFieldEntries(Decl *field) {
    if (auto var = dyn_cast<VarDecl>(field)) {
      asImpl().addFieldOffset(var);
      return;
    }
    if (auto placeholder = dyn_cast<MissingMemberDecl>(field)) {
      asImpl().addFieldOffsetPlaceholders(placeholder);
      return;
    }
  }
};

/// An "implementation" of ClassMetadataVisitor that just scans through
/// the metadata layout, maintaining the offset of the next field.
template <class Impl>
class ClassMetadataScanner : public ClassMetadataVisitor<Impl> {
  typedef ClassMetadataVisitor<Impl> super;
protected:
  Size NextOffset = Size(0);

  ClassMetadataScanner(IRGenModule &IGM, ClassDecl *target)
    : super(IGM, target) {}

public:
  void addMetadataFlags() { addPointer(); }
  void addNominalTypeDescriptor() { addPointer(); }
  void addIVarDestroyer() { addPointer(); }
  void addValueWitnessTable() { addPointer(); }
  void addDestructorFunction() { addPointer(); }
  void addSuperClass() { addPointer(); }
  void addClassFlags() { addInt32(); }
  void addInstanceAddressPoint() { addInt32(); }
  void addInstanceSize() { addInt32(); }
  void addInstanceAlignMask() { addInt16(); }
  void addRuntimeReservedBits() { addInt16(); }
  void addClassSize() { addInt32(); }
  void addClassAddressPoint() { addInt32(); }
  void addClassCacheData() { addPointer(); addPointer(); }
  void addClassDataPointer() { addPointer(); }
  void addMethod(SILDeclRef declRef) {
    addPointer();
  }
  void addMethodOverride(SILDeclRef baseRef, SILDeclRef declRef) {}
  void addFieldOffset(VarDecl *var) { addPointer(); }
  void addFieldOffsetPlaceholders(MissingMemberDecl *mmd) {
    for (unsigned i = 0, e = mmd->getNumberOfFieldOffsetVectorEntries();
         i < e; ++i) {
      addPointer();
    }
  }
  void addGenericArgument(CanType argument, ClassDecl *forClass) {
    addPointer();
  }
  void addGenericWitnessTable(CanType argument, ProtocolConformanceRef conf,
                              ClassDecl *forClass) {
    addPointer();
  }
  void addPlaceholder(MissingMemberDecl *MMD) {
    for (auto i : range(MMD->getNumberOfVTableEntries())) {
      (void)i;
      addPointer();
    }
  }

private:
  // Our layout here assumes that there will never be unclaimed space
  // in the metadata.
  void addPointer() {
    NextOffset += super::IGM.getPointerSize();
  }
  void addInt32() {
    NextOffset += Size(4);
  }
  void addInt16() {
    NextOffset += Size(2);
  }
};

} // end namespace irgen
} // end namespace swift

#endif
