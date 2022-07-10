//===--- GenStruct.cpp - Swift IR Generation For 'struct' Types -----------===//
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
//  This file implements IR generation for struct types.
//
//===----------------------------------------------------------------------===//

#include "GenStruct.h"

#include "swift/AST/Types.h"
#include "swift/AST/Decl.h"
#include "swift/AST/IRGenOptions.h"
#include "swift/AST/Pattern.h"
#include "swift/SIL/SILModule.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CodeGen/SwiftCallingConv.h"

#include "GenMeta.h"
#include "GenRecord.h"
#include "GenType.h"
#include "IRGenFunction.h"
#include "IRGenModule.h"
#include "Linking.h"
#include "IndirectTypeInfo.h"
#include "MemberAccessStrategy.h"
#include "NonFixedTypeInfo.h"
#include "ResilientTypeInfo.h"
#include "StructMetadataLayout.h"

#pragma clang diagnostic ignored "-Winconsistent-missing-override"

using namespace swift;
using namespace irgen;

/// The kinds of TypeInfos implementing struct types.
enum class StructTypeInfoKind {
  LoadableStructTypeInfo,
  FixedStructTypeInfo,
  ClangRecordTypeInfo,
  NonFixedStructTypeInfo,
  ResilientStructTypeInfo
};

static StructTypeInfoKind getStructTypeInfoKind(const TypeInfo &type) {
  return (StructTypeInfoKind) type.getSubclassKind();
}

namespace {
  class StructFieldInfo : public RecordField<StructFieldInfo> {
  public:
    StructFieldInfo(VarDecl *field, const TypeInfo &type)
      : RecordField(type), Field(field) {}

    /// The field.
    VarDecl * const Field;

    StringRef getFieldName() const {
      return Field->getName().str();
    }
    
    SILType getType(IRGenModule &IGM, SILType T) const {
      return T.getFieldType(Field, IGM.getSILModule());
    }
  };

  /// A field-info implementation for fields of Clang types.
  class ClangFieldInfo : public RecordField<ClangFieldInfo> {
  public:
    ClangFieldInfo(VarDecl *swiftField, const ElementLayout &layout,
                   unsigned explosionBegin, unsigned explosionEnd)
      : RecordField(layout, explosionBegin, explosionEnd),
        Field(swiftField) {}

    VarDecl * const Field;

    StringRef getFieldName() const {
      if (Field) return Field->getName().str();
      return "<unimported>";
    }

    SILType getType(IRGenModule &IGM, SILType T) const {
      if (Field)
        return T.getFieldType(Field, IGM.getSILModule());

      // The Swift-field-less cases use opaque storage, which is
      // guaranteed to ignore the type passed to it.
      return {};
    }
  };

  /// A common base class for structs.
  template <class Impl, class Base, class FieldInfoType = StructFieldInfo>
  class StructTypeInfoBase :
     public RecordTypeInfo<Impl, Base, FieldInfoType> {
    typedef RecordTypeInfo<Impl, Base, FieldInfoType> super;

  protected:
    template <class... As>
    StructTypeInfoBase(StructTypeInfoKind kind, As &&...args)
      : super(std::forward<As>(args)...) {
      super::setSubclassKind((unsigned) kind);
    }

    using super::asImpl;

  public:
    const FieldInfoType &getFieldInfo(VarDecl *field) const {
      // FIXME: cache the physical field index in the VarDecl.
      for (auto &fieldInfo : asImpl().getFields()) {
        if (fieldInfo.Field == field)
          return fieldInfo;
      }
      llvm_unreachable("field not in struct?");
    }

    /// Given a full struct explosion, project out a single field.
    void projectFieldFromExplosion(IRGenFunction &IGF,
                                   Explosion &in,
                                   VarDecl *field,
                                   Explosion &out) const {
      auto &fieldInfo = getFieldInfo(field);

      // If the field requires no storage, there's nothing to do.
      if (fieldInfo.isEmpty())
        return;
  
      // Otherwise, project from the base.
      auto fieldRange = fieldInfo.getProjectionRange();
      auto elements = in.getRange(fieldRange.first, fieldRange.second);
      out.add(elements);
    }

    /// Given the address of a tuple, project out the address of a
    /// single element.
    Address projectFieldAddress(IRGenFunction &IGF,
                                Address addr,
                                SILType T,
                                VarDecl *field) const {
      auto &fieldInfo = getFieldInfo(field);
      if (fieldInfo.isEmpty())
        return fieldInfo.getTypeInfo().getUndefAddress();

      auto offsets = asImpl().getNonFixedOffsets(IGF, T);
      return fieldInfo.projectAddress(IGF, addr, offsets);
    }
       
    /// Return the constant offset of a field as a SizeTy, or nullptr if the
    /// field is not at a fixed offset.
    llvm::Constant *getConstantFieldOffset(IRGenModule &IGM,
                                           VarDecl *field) const {
      auto &fieldInfo = getFieldInfo(field);
      if (fieldInfo.getKind() == ElementLayout::Kind::Fixed) {
        return llvm::ConstantInt::get(IGM.SizeTy,
                                    fieldInfo.getFixedByteOffset().getValue());
      }
      return nullptr;
    }

    MemberAccessStrategy getFieldAccessStrategy(IRGenModule &IGM,
                                             SILType T, VarDecl *field) const {
      auto &fieldInfo = getFieldInfo(field);
      switch (fieldInfo.getKind()) {
      case ElementLayout::Kind::Fixed:
      case ElementLayout::Kind::Empty:
        return MemberAccessStrategy::getDirectFixed(
                                               fieldInfo.getFixedByteOffset());
      case ElementLayout::Kind::InitialNonFixedSize:
        return MemberAccessStrategy::getDirectFixed(Size(0));
      case ElementLayout::Kind::NonFixed:
        return asImpl().getNonFixedFieldAccessStrategy(IGM, T, fieldInfo);
      }
      llvm_unreachable("bad field layout kind");
    }

    // For now, just use extra inhabitants from the first field.
    // FIXME: generalize
    bool mayHaveExtraInhabitants(IRGenModule &IGM) const override {
      if (asImpl().getFields().empty()) return false;
      return asImpl().getFields()[0].getTypeInfo().mayHaveExtraInhabitants(IGM);
    }

    // This is dead code in NonFixedStructTypeInfo.
    unsigned getFixedExtraInhabitantCount(IRGenModule &IGM) const {
      if (asImpl().getFields().empty()) return 0;
      auto &fieldTI = cast<FixedTypeInfo>(asImpl().getFields()[0].getTypeInfo());
      return fieldTI.getFixedExtraInhabitantCount(IGM);
    }

    // This is dead code in NonFixedStructTypeInfo.
    APInt getFixedExtraInhabitantValue(IRGenModule &IGM,
                                       unsigned bits,
                                       unsigned index) const {
      auto &fieldTI = cast<FixedTypeInfo>(asImpl().getFields()[0].getTypeInfo());
      return fieldTI.getFixedExtraInhabitantValue(IGM, bits, index);
    }

    // This is dead code in NonFixedStructTypeInfo.
    APInt getFixedExtraInhabitantMask(IRGenModule &IGM) const {
      if (asImpl().getFields().empty())
        return APInt();
      
      // Currently we only use the first field's extra inhabitants. The other
      // fields can be ignored.
      const FixedTypeInfo &fieldTI
        = cast<FixedTypeInfo>(asImpl().getFields()[0].getTypeInfo());
      auto targetSize = asImpl().getFixedSize().getValueInBits();
      
      if (fieldTI.isKnownEmpty(ResilienceExpansion::Maximal))
        return APInt(targetSize, 0);
      
      APInt fieldMask = fieldTI.getFixedExtraInhabitantMask(IGM);
      if (targetSize > fieldMask.getBitWidth())
        fieldMask = fieldMask.zext(targetSize);
      return fieldMask;
    }

    llvm::Value *getExtraInhabitantIndex(IRGenFunction &IGF,
                                         Address structAddr,
                                         SILType structType) const override {
      auto &field = asImpl().getFields()[0];
      Address fieldAddr =
        asImpl().projectFieldAddress(IGF, structAddr, structType, field.Field);
      return field.getTypeInfo().getExtraInhabitantIndex(IGF, fieldAddr,
                                          field.getType(IGF.IGM, structType));
    }

    void storeExtraInhabitant(IRGenFunction &IGF,
                              llvm::Value *index,
                              Address structAddr,
                              SILType structType) const override {
      auto &field = asImpl().getFields()[0];
      Address fieldAddr =
        asImpl().projectFieldAddress(IGF, structAddr, structType, field.Field);
      field.getTypeInfo().storeExtraInhabitant(IGF, index, fieldAddr,
                                          field.getType(IGF.IGM, structType));
    }
  };

  /// A type implementation for loadable record types imported from Clang.
  class ClangRecordTypeInfo final :
    public StructTypeInfoBase<ClangRecordTypeInfo, LoadableTypeInfo,
                              ClangFieldInfo> {
    const clang::RecordDecl *ClangDecl;
  public:
    ClangRecordTypeInfo(ArrayRef<ClangFieldInfo> fields,
                        unsigned explosionSize,
                        llvm::Type *storageType, Size size,
                        SpareBitVector &&spareBits, Alignment align,
                        const clang::RecordDecl *clangDecl)
      : StructTypeInfoBase(StructTypeInfoKind::ClangRecordTypeInfo,
                           fields, explosionSize,
                           storageType, size, std::move(spareBits),
                           align, IsPOD, IsFixedSize),
        ClangDecl(clangDecl) {
    }

    void initializeFromParams(IRGenFunction &IGF, Explosion &params,
                              Address addr, SILType T) const override {
      ClangRecordTypeInfo::initialize(IGF, params, addr);
    }

    void addToAggLowering(IRGenModule &IGM, SwiftAggLowering &lowering,
                          Size offset) const override {
      lowering.addTypedData(ClangDecl, offset.asCharUnits());
    }

    llvm::NoneType getNonFixedOffsets(IRGenFunction &IGF) const {
      return None;
    }
    llvm::NoneType getNonFixedOffsets(IRGenFunction &IGF, SILType T) const {
      return None;
    }
    MemberAccessStrategy
    getNonFixedFieldAccessStrategy(IRGenModule &IGM, SILType T,
                                   const ClangFieldInfo &field) const {
      llvm_unreachable("non-fixed field in Clang type?");
    }
  };

  /// A type implementation for loadable struct types.
  class LoadableStructTypeInfo final
      : public StructTypeInfoBase<LoadableStructTypeInfo, LoadableTypeInfo> {
  public:
    // FIXME: Spare bits between struct members.
    LoadableStructTypeInfo(ArrayRef<StructFieldInfo> fields,
                           unsigned explosionSize,
                           llvm::Type *storageType, Size size,
                           SpareBitVector &&spareBits,
                           Alignment align, IsPOD_t isPOD,
                           IsFixedSize_t alwaysFixedSize)
      : StructTypeInfoBase(StructTypeInfoKind::LoadableStructTypeInfo,
                           fields, explosionSize,
                           storageType, size, std::move(spareBits),
                           align, isPOD, alwaysFixedSize)
    {}

    void addToAggLowering(IRGenModule &IGM, SwiftAggLowering &lowering,
                          Size offset) const override {
      for (auto &field : getFields()) {
        auto fieldOffset = offset + field.getFixedByteOffset();
        cast<LoadableTypeInfo>(field.getTypeInfo())
          .addToAggLowering(IGM, lowering, fieldOffset);
      }
    }

    void initializeFromParams(IRGenFunction &IGF, Explosion &params,
                              Address addr, SILType T) const override {
      LoadableStructTypeInfo::initialize(IGF, params, addr);
    }
    llvm::NoneType getNonFixedOffsets(IRGenFunction &IGF) const {
      return None;
    }
    llvm::NoneType getNonFixedOffsets(IRGenFunction &IGF, SILType T) const {
      return None;
    }
    MemberAccessStrategy
    getNonFixedFieldAccessStrategy(IRGenModule &IGM, SILType T,
                                   const StructFieldInfo &field) const {
      llvm_unreachable("non-fixed field in loadable type?");
    }
  };

  /// A type implementation for non-loadable but fixed-size struct types.
  class FixedStructTypeInfo final
      : public StructTypeInfoBase<FixedStructTypeInfo,
                                  IndirectTypeInfo<FixedStructTypeInfo,
                                                   FixedTypeInfo>> {
  public:
    // FIXME: Spare bits between struct members.
    FixedStructTypeInfo(ArrayRef<StructFieldInfo> fields, llvm::Type *T,
                        Size size, SpareBitVector &&spareBits,
                        Alignment align, IsPOD_t isPOD, IsBitwiseTakable_t isBT,
                        IsFixedSize_t alwaysFixedSize)
      : StructTypeInfoBase(StructTypeInfoKind::FixedStructTypeInfo,
                           fields, T, size, std::move(spareBits), align,
                           isPOD, isBT, alwaysFixedSize)
    {}
    llvm::NoneType getNonFixedOffsets(IRGenFunction &IGF) const {
      return None;
    }
    llvm::NoneType getNonFixedOffsets(IRGenFunction &IGF, SILType T) const {
      return None;
    }
    MemberAccessStrategy
    getNonFixedFieldAccessStrategy(IRGenModule &IGM, SILType T,
                                   const StructFieldInfo &field) const {
      llvm_unreachable("non-fixed field in fixed struct?");
    }
  };

  struct GetStartOfFieldOffsets
    : StructMetadataScanner<GetStartOfFieldOffsets>
  {
    GetStartOfFieldOffsets(IRGenModule &IGM, StructDecl *target)
      : StructMetadataScanner(IGM, target) {}

    Size StartOfFieldOffsets = Size::invalid();

    void noteAddressPoint() {
      assert(StartOfFieldOffsets == Size::invalid()
             && "found field offsets before address point?");
      NextOffset = Size(0);
    }
    void noteStartOfFieldOffsets() { StartOfFieldOffsets = NextOffset; }
  };
  
  /// Find the beginning of the field offset vector in a struct's metadata.
  static Address
  emitAddressOfFieldOffsetVector(IRGenFunction &IGF,
                                 StructDecl *S,
                                 llvm::Value *metadata) {
    // Find where the field offsets begin.
    GetStartOfFieldOffsets scanner(IGF.IGM, S);
    scanner.layout();
    assert(scanner.StartOfFieldOffsets != Size::invalid()
           && "did not find start of field offsets?!");
    
    Size StartOfFieldOffsets = scanner.StartOfFieldOffsets;
    
    // Find that offset into the metadata.
    llvm::Value *fieldVector
      = IGF.Builder.CreateBitCast(metadata, IGF.IGM.SizeTy->getPointerTo());
    return IGF.Builder.CreateConstArrayGEP(
                            Address(fieldVector, IGF.IGM.getPointerAlignment()),
                            StartOfFieldOffsets / IGF.IGM.getPointerSize(),
                            StartOfFieldOffsets);
  }
  
  /// Accessor for the non-fixed offsets of a struct type.
  class StructNonFixedOffsets : public NonFixedOffsetsImpl {
    SILType TheStruct;
  public:
    StructNonFixedOffsets(SILType type) : TheStruct(type) {
      assert(TheStruct.getStructOrBoundGenericStruct());
    }
    
    llvm::Value *getOffsetForIndex(IRGenFunction &IGF, unsigned index) {
      // Get the field offset vector from the struct metadata.
      llvm::Value *metadata = IGF.emitTypeMetadataRefForLayout(TheStruct);
      Address fieldVector = emitAddressOfFieldOffsetVector(IGF,
                                    TheStruct.getStructOrBoundGenericStruct(),
                                    metadata);
      
      // Grab the indexed offset.
      fieldVector = IGF.Builder.CreateConstArrayGEP(fieldVector, index,
                                                    IGF.IGM.getPointerSize());
      return IGF.Builder.CreateLoad(fieldVector);
    }

    MemberAccessStrategy getFieldAccessStrategy(IRGenModule &IGM,
                                                unsigned nonFixedIndex) {
      GetStartOfFieldOffsets scanner(IGM,
                                     TheStruct.getStructOrBoundGenericStruct());
      scanner.layout();

      Size indirectOffset =
        scanner.StartOfFieldOffsets + IGM.getPointerSize() * nonFixedIndex;

      return MemberAccessStrategy::getIndirectFixed(indirectOffset,
                               MemberAccessStrategy::OffsetKind::Bytes_Word);
    }
  };

  /// A type implementation for non-fixed struct types.
  class NonFixedStructTypeInfo final
      : public StructTypeInfoBase<NonFixedStructTypeInfo,
                                  WitnessSizedTypeInfo<NonFixedStructTypeInfo>>
  {
  public:
    NonFixedStructTypeInfo(ArrayRef<StructFieldInfo> fields, llvm::Type *T,
                           Alignment align,
                           IsPOD_t isPOD, IsBitwiseTakable_t isBT)
      : StructTypeInfoBase(StructTypeInfoKind::NonFixedStructTypeInfo,
                           fields, T, align, isPOD, isBT) {
    }

    // We have an indirect schema.
    void getSchema(ExplosionSchema &s) const override {
      s.add(ExplosionSchema::Element::forAggregate(getStorageType(),
                                                   getBestKnownAlignment()));
    }

    StructNonFixedOffsets
    getNonFixedOffsets(IRGenFunction &IGF, SILType T) const {
      return StructNonFixedOffsets(T);
    }

    MemberAccessStrategy
    getNonFixedFieldAccessStrategy(IRGenModule &IGM, SILType T,
                                   const StructFieldInfo &field) const {
      return StructNonFixedOffsets(T).getFieldAccessStrategy(IGM,
                                              field.getNonFixedElementIndex());
    }

    void initializeMetadata(IRGenFunction &IGF,
                            llvm::Value *metadata,
                            llvm::Value *vwtable,
                            SILType T) const override {
      // Get the field offset vector.
      llvm::Value *fieldVector = emitAddressOfFieldOffsetVector(IGF,
                                              T.getStructOrBoundGenericStruct(),
                                              metadata).getAddress();

      // Collect the stored properties of the type.
      llvm::SmallVector<VarDecl*, 4> storedProperties;
      for (auto prop : T.getStructOrBoundGenericStruct()
                        ->getStoredProperties()) {
        storedProperties.push_back(prop);
      }
      // Fill out an array with the field type layout records.
      Address fields = IGF.createAlloca(
                       llvm::ArrayType::get(IGF.IGM.Int8PtrPtrTy,
                                            storedProperties.size()),
                       IGF.IGM.getPointerAlignment(), "structFields");
      IGF.Builder.CreateLifetimeStart(fields,
                            IGF.IGM.getPointerSize() * storedProperties.size());

      fields = IGF.Builder.CreateStructGEP(fields, 0, Size(0));
      unsigned index = 0;
      for (auto prop : storedProperties) {
        auto propTy = T.getFieldType(prop, IGF.getSILModule());
        llvm::Value *metadata = IGF.emitTypeLayoutRef(propTy);
        Address field = IGF.Builder.CreateConstArrayGEP(fields, index,
                                                      IGF.IGM.getPointerSize());
        IGF.Builder.CreateStore(metadata, field);
        ++index;
      }
      
      // Ask the runtime to lay out the struct.
      auto numFields = llvm::ConstantInt::get(IGF.IGM.SizeTy,
                                              storedProperties.size());
      IGF.Builder.CreateCall(IGF.IGM.getInitStructMetadataUniversalFn(),
                             {numFields, fields.getAddress(),
                              fieldVector, vwtable});
      IGF.Builder.CreateLifetimeEnd(fields,
                            IGF.IGM.getPointerSize() * storedProperties.size());
    }
  };

  class StructTypeBuilder :
    public RecordTypeBuilder<StructTypeBuilder, StructFieldInfo, VarDecl*> {

    llvm::StructType *StructTy;
    CanType TheStruct;
  public:
    StructTypeBuilder(IRGenModule &IGM, llvm::StructType *structTy,
                      CanType type) :
      RecordTypeBuilder(IGM), StructTy(structTy), TheStruct(type) {
    }

    LoadableStructTypeInfo *createLoadable(ArrayRef<StructFieldInfo> fields,
                                           StructLayout &&layout,
                                           unsigned explosionSize) {
      return LoadableStructTypeInfo::create(fields,
                                            explosionSize,
                                            layout.getType(),
                                            layout.getSize(),
                                            std::move(layout.getSpareBits()),
                                            layout.getAlignment(),
                                            layout.isPOD(),
                                            layout.isAlwaysFixedSize());
    }

    FixedStructTypeInfo *createFixed(ArrayRef<StructFieldInfo> fields,
                                     StructLayout &&layout) {
      return FixedStructTypeInfo::create(fields, layout.getType(),
                                         layout.getSize(),
                                         std::move(layout.getSpareBits()),
                                         layout.getAlignment(),
                                         layout.isPOD(),
                                         layout.isBitwiseTakable(),
                                         layout.isAlwaysFixedSize());
    }

    NonFixedStructTypeInfo *createNonFixed(ArrayRef<StructFieldInfo> fields,
                                           StructLayout &&layout) {
      return NonFixedStructTypeInfo::create(fields, layout.getType(),
                                            layout.getAlignment(),
                                            layout.isPOD(),
                                            layout.isBitwiseTakable());
    }

    StructFieldInfo getFieldInfo(unsigned index,
                                 VarDecl *field, const TypeInfo &fieldTI) {
      return StructFieldInfo(field, fieldTI);
    }

    SILType getType(VarDecl *field) {
      assert(field->getDeclContext() == TheStruct->getAnyNominal());
      auto silType = SILType::getPrimitiveAddressType(TheStruct);
      return silType.getFieldType(field, IGM.getSILModule());
    }

    StructLayout performLayout(ArrayRef<const TypeInfo *> fieldTypes) {
      return StructLayout(IGM, TheStruct, LayoutKind::NonHeapObject,
                          LayoutStrategy::Optimal, fieldTypes, StructTy);
    }
  };

/// A class for lowering Clang records.
class ClangRecordLowering {
  IRGenModule &IGM;
  StructDecl *SwiftDecl;
  SILType SwiftType;
  const clang::RecordDecl *ClangDecl;
  const clang::ASTContext &ClangContext;
  const clang::ASTRecordLayout &ClangLayout;
  const Size TotalStride;
  const Alignment TotalAlignment;
  SpareBitVector SpareBits;

  SmallVector<llvm::Type *, 8> LLVMFields;
  SmallVector<ClangFieldInfo, 8> FieldInfos;
  Size NextOffset = Size(0);
  unsigned NextExplosionIndex = 0;
public:
  ClangRecordLowering(IRGenModule &IGM, StructDecl *swiftDecl,
                      const clang::RecordDecl *clangDecl,
                      SILType swiftType)
    : IGM(IGM), SwiftDecl(swiftDecl), SwiftType(swiftType),
      ClangDecl(clangDecl), ClangContext(clangDecl->getASTContext()),
      ClangLayout(ClangContext.getASTRecordLayout(clangDecl)),
      TotalStride(Size(ClangLayout.getSize().getQuantity())),
      TotalAlignment(Alignment(ClangLayout.getAlignment().getQuantity())) {
    SpareBits.reserve(TotalStride.getValue() * 8);
  }

  void collectRecordFields() {
    if (ClangDecl->isUnion()) {
      collectUnionFields();
    } else {
      collectStructFields();
    }
  }

  const TypeInfo *createTypeInfo(llvm::StructType *llvmType) {
    llvmType->setBody(LLVMFields, /*packed*/ true);
    return ClangRecordTypeInfo::create(FieldInfos, NextExplosionIndex,
                                       llvmType, TotalStride,
                                       std::move(SpareBits), TotalAlignment,
                                       ClangDecl);
  }

private:
  /// Collect all the fields of a union.
  void collectUnionFields() {
    addOpaqueField(Size(0), TotalStride);
  }

  static bool isImportOfClangField(VarDecl *swiftField,
                                   const clang::FieldDecl *clangField) {
    assert(swiftField->hasClangNode());
    return (swiftField->getClangNode().castAsDecl() == clangField);
  }

  void collectStructFields() {
    auto cfi = ClangDecl->field_begin(), cfe = ClangDecl->field_end();
    auto swiftProperties = SwiftDecl->getStoredProperties();
    auto sfi = swiftProperties.begin(), sfe = swiftProperties.end();

    while (cfi != cfe) {
      const clang::FieldDecl *clangField = *cfi++;

      // Bitfields are currently never mapped, but that doesn't mean
      // we don't have to copy them.
      if (clangField->isBitField()) {
        // Collect all of the following bitfields.
        unsigned bitStart =
          ClangLayout.getFieldOffset(clangField->getFieldIndex());
        unsigned bitEnd = bitStart + clangField->getBitWidthValue(ClangContext);

        while (cfi != cfe && (*cfi)->isBitField()) {
          clangField = *cfi++;
          unsigned nextStart =
            ClangLayout.getFieldOffset(clangField->getFieldIndex());
          assert(nextStart >= bitEnd && "laying out bit-fields out of order?");

          // In a heuristic effort to reduce the number of weird-sized
          // fields, whenever we see a bitfield starting on a 32-bit
          // boundary, start a new storage unit.
          if (nextStart % 32 == 0) {
            addOpaqueBitField(bitStart, bitEnd);
            bitStart = nextStart;
          }

          bitEnd = nextStart + clangField->getBitWidthValue(ClangContext);
        }

        addOpaqueBitField(bitStart, bitEnd);
        continue;
      }

      VarDecl *swiftField;
      if (sfi != sfe) {
        swiftField = *sfi;
        if (isImportOfClangField(swiftField, clangField)) {
          ++sfi;
        } else {
          swiftField = nullptr;
        }
      } else {
        swiftField = nullptr;
      }

      // Try to position this field.  If this fails, it's because we
      // didn't lay out padding correctly.
      addStructField(clangField, swiftField);
    }

    assert(sfi == sfe && "more Swift fields than there were Clang fields?");

    // We never take advantage of tail padding, because that would prevent
    // us from passing the address of the object off to C, which is a pretty
    // likely scenario for imported C types.
    assert(NextOffset <= TotalStride);
    assert(SpareBits.size() <= TotalStride.getValueInBits());
    if (NextOffset < TotalStride) {
      addPaddingField(TotalStride);
    }
  }

  /// Place the next struct field at its appropriate offset.
  void addStructField(const clang::FieldDecl *clangField,
                      VarDecl *swiftField) {
    unsigned fieldOffset = ClangLayout.getFieldOffset(clangField->getFieldIndex());
    assert(!clangField->isBitField());
    Size offset(fieldOffset / 8);

    // If we have a Swift import of this type, use our lowered information.
    if (swiftField) {
      auto &fieldTI = cast<LoadableTypeInfo>(
        IGM.getTypeInfo(SwiftType.getFieldType(swiftField, IGM.getSILModule())));
      addField(swiftField, offset, fieldTI);
      return;
    }

    // Otherwise, add it as an opaque blob.
    auto fieldSize = ClangContext.getTypeSizeInChars(clangField->getType());
    return addOpaqueField(offset, Size(fieldSize.getQuantity()));
  }

  /// Add opaque storage for bitfields spanning the given range of bits.
  void addOpaqueBitField(unsigned bitBegin, unsigned bitEnd) {
    assert(bitBegin <= bitEnd);

    // No need to add storage for zero-width bitfields.
    if (bitBegin == bitEnd) return;

    // Round up to an even number of bytes.
    assert(bitBegin % 8 == 0);
    Size offset = Size(bitBegin / 8);
    Size byteLength = Size((bitEnd - bitBegin + 7) / 8);

    addOpaqueField(offset, byteLength);
  }

  /// Add opaque storage at the given offset.
  void addOpaqueField(Size offset, Size fieldSize) {
    // No need to add storage for zero-size fields (e.g. incomplete array
    // decls).
    if (fieldSize.isZero()) return;

    auto &opaqueTI = IGM.getOpaqueStorageTypeInfo(fieldSize, Alignment(1));
    addField(nullptr, offset, opaqueTI);
  }

  /// Add storage for an (optional) Swift field at the given offset.
  void addField(VarDecl *swiftField, Size offset,
                const LoadableTypeInfo &fieldType) {
    assert(offset >= NextOffset && "adding fields out of order");

    // Add a padding field if required.
    if (offset != NextOffset)
      addPaddingField(offset);

    addFieldInfo(swiftField, fieldType);
  }

  /// Add information to track a value field at the current offset.
  void addFieldInfo(VarDecl *swiftField, const LoadableTypeInfo &fieldType) {
    unsigned explosionSize = fieldType.getExplosionSize();
    unsigned explosionBegin = NextExplosionIndex;
    NextExplosionIndex += explosionSize;
    unsigned explosionEnd = NextExplosionIndex;

    ElementLayout layout = ElementLayout::getIncomplete(fieldType);
    layout.completeFixed(fieldType.isPOD(ResilienceExpansion::Maximal),
                         NextOffset, LLVMFields.size());

    FieldInfos.push_back(
           ClangFieldInfo(swiftField, layout, explosionBegin, explosionEnd));
    LLVMFields.push_back(fieldType.getStorageType());
    NextOffset += fieldType.getFixedSize();
    SpareBits.append(fieldType.getSpareBits());
  }

  /// Add padding to get up to the given offset.
  void addPaddingField(Size offset) {
    assert(offset > NextOffset);
    Size count = offset - NextOffset;
    LLVMFields.push_back(llvm::ArrayType::get(IGM.Int8Ty, count.getValue()));
    NextOffset = offset;
    SpareBits.appendSetBits(count.getValueInBits());
  }
};

}  // end anonymous namespace.

/// A convenient macro for delegating an operation to all of the
/// various struct implementations.
#define FOR_STRUCT_IMPL(IGF, type, op, ...) do {                       \
  auto &structTI = IGF.getTypeInfo(type);                              \
  switch (getStructTypeInfoKind(structTI)) {                           \
  case StructTypeInfoKind::ClangRecordTypeInfo:                        \
    return structTI.as<ClangRecordTypeInfo>().op(IGF, __VA_ARGS__);    \
  case StructTypeInfoKind::LoadableStructTypeInfo:                     \
    return structTI.as<LoadableStructTypeInfo>().op(IGF, __VA_ARGS__); \
  case StructTypeInfoKind::FixedStructTypeInfo:                        \
    return structTI.as<FixedStructTypeInfo>().op(IGF, __VA_ARGS__);    \
  case StructTypeInfoKind::NonFixedStructTypeInfo:                     \
    return structTI.as<NonFixedStructTypeInfo>().op(IGF, __VA_ARGS__); \
  case StructTypeInfoKind::ResilientStructTypeInfo:                    \
    llvm_unreachable("resilient structs are opaque");                  \
  }                                                                    \
  llvm_unreachable("bad struct type info kind!");                      \
} while (0)

Address irgen::projectPhysicalStructMemberAddress(IRGenFunction &IGF,
                                                  Address base,
                                                  SILType baseType,
                                                  VarDecl *field) {
  FOR_STRUCT_IMPL(IGF, baseType, projectFieldAddress, base,
                  baseType, field);
}

void irgen::projectPhysicalStructMemberFromExplosion(IRGenFunction &IGF,
                                                     SILType baseType,
                                                     Explosion &base,
                                                     VarDecl *field,
                                                     Explosion &out) {
  FOR_STRUCT_IMPL(IGF, baseType, projectFieldFromExplosion, base, field, out);
}

llvm::Constant *irgen::emitPhysicalStructMemberFixedOffset(IRGenModule &IGM,
                                                           SILType baseType,
                                                           VarDecl *field) {
  FOR_STRUCT_IMPL(IGM, baseType, getConstantFieldOffset, field);
}

MemberAccessStrategy
irgen::getPhysicalStructMemberAccessStrategy(IRGenModule &IGM,
                                             SILType baseType, VarDecl *field) {
  FOR_STRUCT_IMPL(IGM, baseType, getFieldAccessStrategy, baseType, field);
}

void IRGenModule::emitStructDecl(StructDecl *st) {
  emitStructMetadata(*this, st);
  emitNestedTypeDecls(st->getMembers());
  emitFieldMetadataRecord(st);
}

namespace {
  /// A type implementation for resilient struct types. This is not a
  /// StructTypeInfoBase at all, since we don't know anything about
  /// the struct's fields.
  class ResilientStructTypeInfo
      : public ResilientTypeInfo<ResilientStructTypeInfo>
  {
  public:
    ResilientStructTypeInfo(llvm::Type *T)
      : ResilientTypeInfo(T) {
      setSubclassKind((unsigned) StructTypeInfoKind::ResilientStructTypeInfo);
    }
  };
}

const TypeInfo *TypeConverter::convertResilientStruct() {
  llvm::Type *storageType = IGM.OpaquePtrTy->getElementType();
  return new ResilientStructTypeInfo(storageType);
}

const TypeInfo *TypeConverter::convertStructType(TypeBase *key, CanType type,
                                                 StructDecl *D) {
  // All resilient structs have the same opaque lowering, since they are
  // indistinguishable as values.
  if (IGM.isResilient(D, ResilienceExpansion::Maximal))
    return &getResilientStructTypeInfo();

  // Create the struct type.
  auto ty = IGM.createNominalType(type);

  // Register a forward declaration before we look at any of the child types.
  addForwardDecl(key, ty);

  // Use different rules for types imported from C.
  if (D->hasClangNode()) {
    const clang::Decl *clangDecl = D->getClangNode().getAsDecl();
    assert(clangDecl && "Swift struct from an imported C macro?");

    if (auto clangRecord = dyn_cast<clang::RecordDecl>(clangDecl)) {
      ClangRecordLowering lowering(IGM, D, clangRecord,
                                   SILType::getPrimitiveObjectType(type));
      lowering.collectRecordFields();
      return lowering.createTypeInfo(ty);

    } else if (isa<clang::EnumDecl>(clangDecl)) {
      // Fall back to Swift lowering for the enum's representation as a struct.
      assert(std::distance(D->getStoredProperties().begin(),
                           D->getStoredProperties().end()) == 1 &&
             "Struct representation of a Clang enum should wrap one value");
    } else if (clangDecl->hasAttr<clang::SwiftNewtypeAttr>()) {
      // Fall back to Swift lowering for the underlying type's
      // representation as a struct member.
      assert(std::distance(D->getStoredProperties().begin(),
                           D->getStoredProperties().end()) == 1 &&
             "Struct representation of a swift_newtype should wrap one value");
    } else {
      llvm_unreachable("Swift struct represents unexpected imported type");
    }
  }

  // Collect all the fields from the type.
  SmallVector<VarDecl*, 8> fields;
  for (VarDecl *VD : D->getStoredProperties())
    fields.push_back(VD);

  // Build the type.
  StructTypeBuilder builder(IGM, ty, type);
  return builder.layout(fields);
}
