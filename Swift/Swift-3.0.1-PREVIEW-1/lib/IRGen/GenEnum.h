//===--- GenEnum.h - Swift IR Generation For 'enum' Types -------*- C++ -*-===//
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

#ifndef __SWIFT_IRGen_GenEnum_H__
#define __SWIFT_IRGen_GenEnum_H__

#include "TypeInfo.h"

namespace llvm {
  class BasicBlock;
  class ConstantInt;
  class StructType;
  class Type;
  class Value;
}

namespace clang {
namespace CodeGen {
namespace swiftcall {
  class SwiftAggLowering;
}
}
}

namespace swift {
  class EnumElementDecl;
  
namespace irgen {
  class EnumPayload;
  class EnumPayloadSchema;
  class IRGenFunction;
  class TypeConverter;
  using clang::CodeGen::swiftcall::SwiftAggLowering;

/// \brief Emit the dispatch branch(es) for an address-only enum.
void emitSwitchAddressOnlyEnumDispatch(IRGenFunction &IGF,
                                        SILType enumTy,
                                        Address enumAddr,
                                        ArrayRef<std::pair<EnumElementDecl*,
                                                           llvm::BasicBlock*>> dests,
                                        llvm::BasicBlock *defaultDest);

/// \brief Injects a case and its associated data, if any, into a loadable enum
/// value.
void emitInjectLoadableEnum(IRGenFunction &IGF,
                             SILType enumTy,
                             EnumElementDecl *theCase,
                             Explosion &data,
                             Explosion &out);
  
/// \brief Extracts the associated data for an enum case. This is an unchecked
/// operation; the input enum value must be of the given case.
void emitProjectLoadableEnum(IRGenFunction &IGF,
                              SILType enumTy,
                              Explosion &inData,
                              EnumElementDecl *theCase,
                              Explosion &out);

/// \brief Projects the address of the associated data for a case inside a
/// enum, to which a new data value can be stored.
Address emitProjectEnumAddressForStore(IRGenFunction &IGF,
                                        SILType enumTy,
                                        Address enumAddr,
                                        EnumElementDecl *theCase);

/// \brief Projects the address of the associated data for a case inside a
/// enum, clearing any tag bits interleaved into the data area, so that the
/// value inside can be loaded. Does not check that the enum has a value of the
/// given case.
Address emitDestructiveProjectEnumAddressForLoad(IRGenFunction &IGF,
                                                  SILType enumTy,
                                                  Address enumAddr,
                                                  EnumElementDecl *theCase);

/// \brief Stores the tag bits for an enum case to the given address, overlaying
/// the data (if any) stored there.
void emitStoreEnumTagToAddress(IRGenFunction &IGF,
                                SILType enumTy,
                                Address enumAddr,
                                EnumElementDecl *theCase);
  
/// Interleave the occupiedValue and spareValue bits, taking a bit from one
/// or the other at each position based on the spareBits mask.
APInt
interleaveSpareBits(IRGenModule &IGM, const SpareBitVector &spareBits,
                    unsigned bits, unsigned spareValue, unsigned occupiedValue);

/// A version of the above where the tag value is dynamic.
EnumPayload interleaveSpareBits(IRGenFunction &IGF,
                                const EnumPayloadSchema &schema,
                                const SpareBitVector &spareBitVector,
                                llvm::Value *value);

/// Gather spare bits into the low bits of a smaller integer value.
llvm::Value *emitGatherSpareBits(IRGenFunction &IGF,
                                 const SpareBitVector &spareBitMask,
                                 llvm::Value *spareBits,
                                 unsigned resultLowBit,
                                 unsigned resultBitWidth);
/// Scatter spare bits from the low bits of a smaller integer value.
llvm::Value *emitScatterSpareBits(IRGenFunction &IGF,
                                  const SpareBitVector &spareBitMask,
                                  llvm::Value *packedBits,
                                  unsigned packedLowBit);
  
/// An implementation strategy for an enum, which handles how the enum is
/// laid out and how to perform TypeInfo operations on values of the enum.
class EnumImplStrategy {
public:
  struct Element {
    EnumElementDecl *decl;
    const TypeInfo *ti;
    const TypeInfo *origTI;
  };
  
  enum TypeInfoKind {
    Opaque,   ///< The enum has a NonFixedTypeInfo.
    Fixed,    ///< The enum has a FixedTypeInfo.
    Loadable, ///< The enum has a LoadableTypeInfo.
  };
  
protected:
  std::vector<Element> ElementsWithPayload;
  std::vector<Element> ElementsWithNoPayload;
  IRGenModule &IGM;
  const TypeInfo *TI = nullptr;
  TypeInfoKind TIK;
  IsFixedSize_t AlwaysFixedSize;
  unsigned NumElements;
  
  EnumImplStrategy(IRGenModule &IGM,
                   TypeInfoKind tik,
                   IsFixedSize_t alwaysFixedSize,
                   unsigned NumElements,
                   std::vector<Element> &&ElementsWithPayload,
                   std::vector<Element> &&ElementsWithNoPayload)
  : ElementsWithPayload(std::move(ElementsWithPayload)),
    ElementsWithNoPayload(std::move(ElementsWithNoPayload)),
    IGM(IGM), TIK(tik), AlwaysFixedSize(alwaysFixedSize),
    NumElements(NumElements)
  {}
  
  /// Save the TypeInfo created for the enum.
  TypeInfo *registerEnumTypeInfo(TypeInfo *mutableTI) {
    TI = mutableTI;
    return mutableTI;
  }
  
  /// Constructs a TypeInfo for an enum of the best possible kind for its
  /// layout, FixedEnumTypeInfo or LoadableEnumTypeInfo.
  TypeInfo *getFixedEnumTypeInfo(llvm::StructType *T, Size S, SpareBitVector SB,
                                 Alignment A, IsPOD_t isPOD,
                                 IsBitwiseTakable_t isBT);
  
public:
  virtual ~EnumImplStrategy() { }
  
  /// Construct a layout strategy appropriate to the enum type.
  static std::unique_ptr<EnumImplStrategy> get(TypeConverter &TC,
                                               SILType Type,
                                               EnumDecl *theEnum);
  
  /// Given an incomplete StructType for the enum, completes layout of the
  /// storage type, calculates its size and alignment, and produces the
  /// TypeInfo for the enum.
  virtual TypeInfo *completeEnumTypeLayout(TypeConverter &TC,
                                           SILType Type,
                                           EnumDecl *theEnum,
                                           llvm::StructType *enumTy) = 0;

  const TypeInfo &getTypeInfo() const {
    assert(TI);
    return *TI;
  }
  
  llvm::StructType *getStorageType() const {
    return cast<llvm::StructType>(getTypeInfo().getStorageType());
  }
  
  IsPOD_t isPOD(ResilienceExpansion expansion) const {
    return getTypeInfo().isPOD(expansion);
  }
  
  /// \group Query enum layout
  ///
  /// These APIs assume a fixed layout; they will not work on generic
  /// enum types that have not been fully instantiated.
  
  /// Return the list of cases determined to have a payload.
  /// The payloads are discriminated using the bits in the mask returned by
  /// getTagBits().
  ArrayRef<Element> getElementsWithPayload() const {
    return ElementsWithPayload;
  }
  
  /// Return the list of cases determined to have no payload.
  /// Each no-payload case is represented by a specific bit pattern which can
  /// be queried using getBitPatternForNoPayloadElement.
  ArrayRef<Element> getElementsWithNoPayload() const {
    return ElementsWithNoPayload;
  }

  /// Return a tag index in the range [0..NumElements].
  unsigned getTagIndex(EnumElementDecl *Case) const;

  /// Return a tag index in the range
  /// [-ElementsWithPayload..ElementsWithNoPayload-1].
  int getResilientTagIndex(EnumElementDecl *Case) const;

  /// Map the given element to the appropriate index in the
  /// discriminator type.
  /// Returns -1 if this is not supported by the enum implementation.
  virtual int64_t getDiscriminatorIndex(EnumElementDecl *target) const {
    return -1;
  }

  /// Emit field names for enum reflection.
  virtual llvm::Constant *emitCaseNames() const;

  /// \brief Return the bits used for discriminators for payload cases.
  ///
  /// These bits are populated in increasing value according to the order of
  /// the getElementsWithPayload() array, starting from zero for the first
  /// element with payload.
  virtual ClusteredBitVector getTagBitsForPayloads() const = 0;
  
  /// Return the bit pattern used for the given no-payload case.
  virtual ClusteredBitVector
  getBitPatternForNoPayloadElement(EnumElementDecl *theCase) const = 0;

  /// Return the bit mask used to test for no-payload cases.
  virtual ClusteredBitVector
  getBitMaskForNoPayloadElements() const = 0;

  /// \group Indirect enum operations
  
  /// Return the enum case tag for the given value. Payload cases come first,
  /// followed by non-payload cases. Used for the getEnumTag value witness.
  virtual llvm::Value *emitGetEnumTag(IRGenFunction &IGF,
                                      SILType T,
                                      Address enumAddr) const = 0;

  /// Project the address of the data for a case. Does not check or modify
  /// the referenced enum value.
  /// Corresponds to the SIL 'init_enum_data_addr' instruction.
  Address projectDataForStore(IRGenFunction &IGF,
                              EnumElementDecl *elt,
                              Address enumAddr) const;

  /// Overlay the tag value for a case onto a data value in memory.
  /// Corresponds to the SIL 'inject_enum_addr' instruction.
  virtual void storeTag(IRGenFunction &IGF,
                        SILType T,
                        Address enumAddr,
                        EnumElementDecl *elt) const = 0;
  
  /// Overlay a dynamic case tag onto a data value in memory. Used when
  /// generating the destructiveInjectEnumTag value witness.
  virtual void emitStoreTag(IRGenFunction &IGF,
                            SILType T,
                            Address enumAddr,
                            llvm::Value *tag) const = 0;
  
  /// Clears tag bits from within the payload of an enum in memory and
  /// projects the address of the data for a case. Does not check
  /// the referenced enum value.
  /// Performs the block argument binding for a SIL
  /// 'switch_enum_addr' instruction.
  /// Also used when generating the destructiveProjectEnumData value
  /// witness.
  Address destructiveProjectDataForLoad(IRGenFunction &IGF,
                                        SILType T,
                                        Address enumAddr,
                                        EnumElementDecl *Case) const;

  /// Clears tag bits from within the payload of an enum in memory and
  /// projects the address of the data for a case. Does not check
  /// the referenced enum value.
  virtual void destructiveProjectDataForLoad(IRGenFunction &IGF,
                                             SILType T,
                                             Address enumAddr) const = 0;

  /// Return an i1 value that indicates whether the specified indirect enum
  /// value holds the specified case.  This is a light-weight form of a switch.
  virtual llvm::Value *emitIndirectCaseTest(IRGenFunction &IGF,
                                            SILType T,
                                            Address enumAddr,
                                            EnumElementDecl *Case) const = 0;
  
  /// Emit a branch on the case contained by an enum explosion.
  /// Performs the branching for a SIL 'switch_enum_addr'
  /// instruction.
  virtual void emitIndirectSwitch(IRGenFunction &IGF,
                                  SILType T,
                                  Address enumAddr,
                                  ArrayRef<std::pair<EnumElementDecl*,
                                                     llvm::BasicBlock*>> dests,
                                  llvm::BasicBlock *defaultDest) const = 0;

  /// Emit code to extract the discriminator as an integer value.
  /// Returns null if this is not supported by the enum implementation.
  ///
  /// FIXME: is this the same as emitGetEnumTag()? Seems like it could be,
  /// except that CCompatibleEnumImplStrategy maps cases to their raw
  /// values rather than indices.
  virtual llvm::Value *emitExtractDiscriminator(IRGenFunction &IGF,
                                                Explosion &value) const {
    return nullptr;
  }

  /// \group Loadable enum operations
  
  /// Emit the construction sequence for an enum case into an explosion.
  /// Corresponds to the SIL 'enum' instruction.
  virtual void emitValueInjection(IRGenFunction &IGF,
                                  EnumElementDecl *elt,
                                  Explosion &params,
                                  Explosion &out) const = 0;
  
  /// Return an i1 value that indicates whether the specified loadable enum
  /// value holds the specified case.  This is a light-weight form of a switch.
  virtual llvm::Value *emitValueCaseTest(IRGenFunction &IGF,
                                         Explosion &value,
                                         EnumElementDecl *Case) const = 0;

  /// Emit a branch on the case contained by an enum explosion.
  /// Performs the branching for a SIL 'switch_enum' instruction.
  virtual void emitValueSwitch(IRGenFunction &IGF,
                               Explosion &value,
                               ArrayRef<std::pair<EnumElementDecl*,
                                                  llvm::BasicBlock*>> dests,
                               llvm::BasicBlock *defaultDest) const = 0;
  
  /// Project a case value out of an enum explosion. This does not check that
  /// the explosion actually contains a value of the given case.
  /// Performs the block argument binding for a SIL 'switch_enum'
  /// instruction.
  virtual void emitValueProject(IRGenFunction &IGF,
                                Explosion &inEnum,
                                EnumElementDecl *theCase,
                                Explosion &out) const = 0;
  
  /// \group Delegated TypeInfo operations
  
  virtual void addToAggLowering(IRGenModule &IGM, SwiftAggLowering &lowering,
                                Size offset) const = 0;
  virtual void getSchema(ExplosionSchema &schema) const = 0;
  virtual void destroy(IRGenFunction &IGF, Address addr, SILType T) const = 0;

  virtual void initializeFromParams(IRGenFunction &IGF, Explosion &params,
                                    Address dest, SILType T) const;
  
  virtual void assignWithCopy(IRGenFunction &IGF, Address dest,
                              Address src, SILType T) const = 0;
  virtual void assignWithTake(IRGenFunction &IGF, Address dest,
                              Address src, SILType T) const = 0;
  virtual void initializeWithCopy(IRGenFunction &IGF, Address dest,
                                  Address src, SILType T) const = 0;
  virtual void initializeWithTake(IRGenFunction &IGF, Address dest,
                                  Address src, SILType T) const = 0;
  
  virtual void initializeMetadata(IRGenFunction &IGF,
                                  llvm::Value *metadata,
                                  llvm::Value *vwtable,
                                  SILType T) const = 0;

  virtual bool mayHaveExtraInhabitants(IRGenModule &IGM) const = 0;

  virtual llvm::Value *getExtraInhabitantIndex(IRGenFunction &IGF,
                                               Address src,
                                               SILType T) const = 0;
  virtual void storeExtraInhabitant(IRGenFunction &IGF,
                                    llvm::Value *index,
                                    Address dest,
                                    SILType T) const = 0;
  
  /// \group Delegated FixedTypeInfo operations
  
  virtual unsigned getFixedExtraInhabitantCount(IRGenModule &IGM) const = 0;
  
  virtual APInt
  getFixedExtraInhabitantValue(IRGenModule &IGM,
                               unsigned bits,
                               unsigned index) const = 0;
  
  virtual APInt
  getFixedExtraInhabitantMask(IRGenModule &IGM) const = 0;
  
  /// \group Delegated LoadableTypeInfo operations
  
  virtual unsigned getExplosionSize() const = 0;
  virtual void loadAsCopy(IRGenFunction &IGF, Address addr,
                          Explosion &e) const = 0;
  virtual void loadAsTake(IRGenFunction &IGF, Address addr,
                          Explosion &e) const = 0;
  virtual void assign(IRGenFunction &IGF, Explosion &e,
                      Address addr) const = 0;
  virtual void initialize(IRGenFunction &IGF, Explosion &e,
                          Address addr) const = 0;
  virtual void reexplode(IRGenFunction &IGF, Explosion &src,
                         Explosion &dest) const = 0;
  virtual void copy(IRGenFunction &IGF, Explosion &src,
                    Explosion &dest, Atomicity atomicity) const = 0;
  virtual void consume(IRGenFunction &IGF, Explosion &src,
                       Atomicity atomicity) const = 0;
  virtual void fixLifetime(IRGenFunction &IGF, Explosion &src) const = 0;
  virtual void packIntoEnumPayload(IRGenFunction &IGF,
                                   EnumPayload &payload,
                                   Explosion &in,
                                   unsigned offset) const = 0;
  
  virtual void unpackFromEnumPayload(IRGenFunction &IGF,
                                     const EnumPayload &payload,
                                     Explosion &dest,
                                     unsigned offset) const = 0;
  
  virtual bool needsPayloadSizeInMetadata() const = 0;
  virtual unsigned getPayloadSizeForMetadata() const;
  
  virtual llvm::Value *loadRefcountedPtr(IRGenFunction &IGF, SourceLoc loc,
                                         Address addr) const;

private:
  EnumImplStrategy(const EnumImplStrategy &) = delete;
  EnumImplStrategy &operator=(const EnumImplStrategy &) = delete;
};
  
/// Get the EnumImplStrategy for an enum type.
const EnumImplStrategy &getEnumImplStrategy(IRGenModule &IGM, CanType ty);
const EnumImplStrategy &getEnumImplStrategy(IRGenModule &IGM, SILType ty);

}
}

#endif
