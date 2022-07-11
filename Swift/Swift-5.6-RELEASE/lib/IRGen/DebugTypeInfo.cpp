//===--- DebugTypeInfo.cpp - Type Info for Debugging ----------------------===//
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
// This file defines the data structure that holds all the debug info
// we want to emit for types.
//
//===----------------------------------------------------------------------===//

#include "DebugTypeInfo.h"
#include "FixedTypeInfo.h"
#include "swift/SIL/SILGlobalVariable.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace swift;
using namespace irgen;

DebugTypeInfo::DebugTypeInfo(swift::Type Ty, llvm::Type *FragmentStorageTy,
                             Optional<Size> size, Alignment align,
                             bool HasDefaultAlignment, bool IsMetadata,
                             bool SizeIsFragmentSize)
    : Type(Ty.getPointer()), FragmentStorageType(FragmentStorageTy), size(size),
      align(align), DefaultAlignment(HasDefaultAlignment),
      IsMetadataType(IsMetadata), SizeIsFragmentSize(SizeIsFragmentSize) {
  assert(align.getValue() != 0);
}

/// Determine whether this type has a custom @_alignment attribute.
static bool hasDefaultAlignment(swift::Type Ty) {
  if (auto CanTy = Ty->getCanonicalType())
    if (auto *TyDecl = CanTy.getNominalOrBoundGenericNominal())
      if (TyDecl->getAttrs().getAttribute<AlignmentAttr>())
        return false;
  return true;
}

DebugTypeInfo DebugTypeInfo::getFromTypeInfo(swift::Type Ty,
                                             const TypeInfo &Info,
                                             bool IsFragmentTypeInfo) {
  Optional<Size> size;
  if (Info.isFixedSize()) {
    const FixedTypeInfo &FixTy = *cast<const FixedTypeInfo>(&Info);
    size = FixTy.getFixedSize();
  }
  assert(Info.getStorageType() && "StorageType is a nullptr");
  return DebugTypeInfo(Ty.getPointer(), Info.getStorageType(), size,
                       Info.getBestKnownAlignment(), ::hasDefaultAlignment(Ty),
                       false, IsFragmentTypeInfo);
}

DebugTypeInfo DebugTypeInfo::getLocalVariable(VarDecl *Decl, swift::Type Ty,
                                              const TypeInfo &Info,
                                              bool IsFragmentTypeInfo) {

  auto DeclType = Decl->getInterfaceType();
  auto RealType = Ty;

  // DynamicSelfType is also sugar as far as debug info is concerned.
  auto Sugared = DeclType;
  if (auto DynSelfTy = DeclType->getAs<DynamicSelfType>())
    Sugared = DynSelfTy->getSelfType();

  // Prefer the original, potentially sugared version of the type if
  // the type hasn't been mucked with by an optimization pass.
  auto *Type = Sugared->isEqual(RealType) ? DeclType.getPointer()
                                          : RealType.getPointer();
  return getFromTypeInfo(Type, Info, IsFragmentTypeInfo);
}

DebugTypeInfo DebugTypeInfo::getMetadata(swift::Type Ty, llvm::Type *StorageTy,
                                         Size size, Alignment align) {
  DebugTypeInfo DbgTy(Ty.getPointer(), StorageTy, size,
                      align, true, false, false);
  assert(StorageTy && "StorageType is a nullptr");
  assert(!DbgTy.isContextArchetype() &&
         "type metadata cannot contain an archetype");
  return DbgTy;
}

DebugTypeInfo DebugTypeInfo::getArchetype(swift::Type Ty, llvm::Type *StorageTy,
                                          Size size, Alignment align) {
  DebugTypeInfo DbgTy(Ty.getPointer(), StorageTy, size,
                      align, true, true, false);
  assert(StorageTy && "StorageType is a nullptr");
  assert(!DbgTy.isContextArchetype() &&
         "type metadata cannot contain an archetype");
  return DbgTy;
}

DebugTypeInfo DebugTypeInfo::getForwardDecl(swift::Type Ty) {
  DebugTypeInfo DbgTy(Ty.getPointer(), nullptr, {}, Alignment(1), true,
                      false, false);
  return DbgTy;
}

DebugTypeInfo DebugTypeInfo::getGlobal(SILGlobalVariable *GV,
                                       llvm::Type *StorageTy, Size size,
                                       Alignment align) {
  // Prefer the original, potentially sugared version of the type if
  // the type hasn't been mucked with by an optimization pass.
  auto LowTy = GV->getLoweredType().getASTType();
  auto *Type = LowTy.getPointer();
  if (auto *Decl = GV->getDecl()) {
    auto DeclType = Decl->getType();
    if (DeclType->isEqual(LowTy))
      Type = DeclType.getPointer();
  }
  DebugTypeInfo DbgTy(Type, StorageTy, size, align, ::hasDefaultAlignment(Type),
                      false, false);
  assert(StorageTy && "FragmentStorageType is a nullptr");
  assert(!DbgTy.isContextArchetype() &&
         "type of global variable cannot be an archetype");
  assert(align.getValue() != 0);
  return DbgTy;
}

DebugTypeInfo DebugTypeInfo::getObjCClass(ClassDecl *theClass,
                                          llvm::Type *FragmentStorageType,
                                          Size size, Alignment align) {
  DebugTypeInfo DbgTy(theClass->getInterfaceType().getPointer(),
                      FragmentStorageType, size, align, true, false, false);
  assert(FragmentStorageType && "FragmentStorageType is a nullptr");
  assert(!DbgTy.isContextArchetype() &&
         "type of objc class cannot be an archetype");
  return DbgTy;
}

DebugTypeInfo DebugTypeInfo::getErrorResult(swift::Type Ty,
                                            llvm::Type *StorageType, Size size,
                                            Alignment align) {
  assert(StorageType && "FragmentStorageType is a nullptr");
  return {Ty, StorageType, size, align, true, false, false};
}

bool DebugTypeInfo::operator==(DebugTypeInfo T) const {
  return (getType() == T.getType() &&
          size == T.size &&
          align == T.align);
}

bool DebugTypeInfo::operator!=(DebugTypeInfo T) const { return !operator==(T); }

TypeDecl *DebugTypeInfo::getDecl() const {
  if (auto *N = dyn_cast<NominalType>(Type))
    return N->getDecl();
  if (auto *BTA = dyn_cast<TypeAliasType>(Type))
    return BTA->getDecl();
  if (auto *UBG = dyn_cast<UnboundGenericType>(Type))
    return UBG->getDecl();
  if (auto *BG = dyn_cast<BoundGenericType>(Type))
    return BG->getDecl();
  if (auto *E = dyn_cast<ExistentialType>(Type))
    return E->getConstraintType()->getAnyNominal();
  return nullptr;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void DebugTypeInfo::dump() const {
  llvm::errs() << "[";
  if (size)
    llvm::errs() << "Size " << size->getValue() << " ";
  llvm::errs() << "Alignment " << align.getValue() << "] ";
  getType()->dump(llvm::errs());

  if (FragmentStorageType) {
    llvm::errs() << "FragmentStorageType=";
    FragmentStorageType->dump();
  } else
    llvm::errs() << "forward-declared\n";
}
#endif
