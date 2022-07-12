//===--- GenFunc.cpp - Swift IR Generation for Function Types -------------===//
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
//  This file implements IR generation for function types in Swift.  This
//  includes creating the IR type as well as capturing variables and
//  performing calls.
//
//  Swift supports three representations of functions:
//
//    - thin, which are just a function pointer;
//
//    - thick, which are a pair of a function pointer and
//      an optional ref-counted opaque context pointer; and
//
//    - block, which match the Apple blocks extension: a ref-counted
//      pointer to a mostly-opaque structure with the function pointer
//      stored at a fixed offset.
//
//  The order of function parameters is as follows:
//
//    - indirect return pointer
//    - block context parameter, if applicable
//    - expanded formal parameter types
//    - implicit generic parameters
//    - thick context parameter, if applicable
//    - error result out-parameter, if applicable
//    - witness_method generic parameters, if applicable
//
//  The context and error parameters are last because they are
//  optional: we'd like to be able to turn a thin function into a
//  thick function, or a non-throwing function into a throwing one,
//  without adding a thunk.  A thick context parameter is required
//  (but can be passed undef) if an error result is required.
//
//  The additional generic parameters for witness methods follow the
//  same logic: we'd like to be able to use non-generic method
//  implementations directly as protocol witnesses if the rest of the
//  ABI matches up.
//
//  Note that some of this business with context parameters and error
//  results is just IR formalism; on most of our targets, both of
//  these are passed in registers.  This is also why passing them
//  as the final argument isn't bad for performance.
//
//  For now, function pointer types are always stored as opaque
//  pointers in LLVM IR; using a well-typed function type is
//  very challenging because of issues with recursive type expansion,
//  which can potentially introduce infinite types.  For example:
//    struct A {
//      var fn: (A) -> ()
//    }
//  Our CC lowering expands the fields of A into the argument list
//  of A.fn, which is necessarily infinite.  Attempting to use better
//  types when not in a situation like this would just make the
//  compiler complacent, leading to a long tail of undiscovered
//  crashes.  So instead we always store as i8* and require the
//  bitcast whenever we change representations.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/ASTContext.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/Builtins.h"
#include "swift/AST/Decl.h"
#include "swift/AST/IRGenOptions.h"
#include "swift/AST/Module.h"
#include "swift/AST/Pattern.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/SubstitutionMap.h"
#include "swift/AST/Types.h"
#include "clang/CodeGen/CodeGenABITypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/StringSwitch.h"

#include "Callee.h"
#include "ConstantBuilder.h"
#include "EnumPayload.h"
#include "Explosion.h"
#include "FixedTypeInfo.h"
#include "GenCall.h"
#include "GenClass.h"
#include "GenFunc.h"
#include "GenHeap.h"
#include "GenMeta.h"
#include "GenObjC.h"
#include "GenPoly.h"
#include "GenProto.h"
#include "GenType.h"
#include "HeapTypeInfo.h"
#include "IRGenDebugInfo.h"
#include "IRGenFunction.h"
#include "IRGenModule.h"
#include "IndirectTypeInfo.h"
#include "NativeConventionSchema.h"
#include "ScalarTypeInfo.h"
#include "Signature.h"
#include "IRGenMangler.h"

using namespace swift;
using namespace irgen;

namespace {
  /// Information about the IR-level signature of a function type.
  class FuncSignatureInfo {
  private:
    /// The SIL function type being represented.
    const CanSILFunctionType FormalType;
    
    mutable Signature TheSignature;
    
  public:
    FuncSignatureInfo(CanSILFunctionType formalType)
      : FormalType(formalType) {}
    
    Signature getSignature(IRGenModule &IGM) const;
  };

  /// The @thin function type-info class.
  class ThinFuncTypeInfo : public PODSingleScalarTypeInfo<ThinFuncTypeInfo,
                                                          LoadableTypeInfo>,
                           public FuncSignatureInfo {
    ThinFuncTypeInfo(CanSILFunctionType formalType, llvm::Type *storageType,
                     Size size, Alignment align,
                     const SpareBitVector &spareBits)
      : PODSingleScalarTypeInfo(storageType, size, spareBits, align),
        FuncSignatureInfo(formalType)
    {
    }

  public:
    static const ThinFuncTypeInfo *create(CanSILFunctionType formalType,
                                          llvm::Type *storageType,
                                          Size size, Alignment align,
                                          const SpareBitVector &spareBits) {
      return new ThinFuncTypeInfo(formalType, storageType, size, align,
                                  spareBits);
    }

    bool mayHaveExtraInhabitants(IRGenModule &IGM) const override {
      return true;
    }

    unsigned getFixedExtraInhabitantCount(IRGenModule &IGM) const override {
      return getFunctionPointerExtraInhabitantCount(IGM);
    }

    APInt getFixedExtraInhabitantValue(IRGenModule &IGM,
                                       unsigned bits,
                                       unsigned index) const override {
      return getFunctionPointerFixedExtraInhabitantValue(IGM, bits, index, 0);
    }

    llvm::Value *getExtraInhabitantIndex(IRGenFunction &IGF, Address src,
                                         SILType T)
    const override {
      return getFunctionPointerExtraInhabitantIndex(IGF, src);
    }

    void storeExtraInhabitant(IRGenFunction &IGF, llvm::Value *index,
                              Address dest, SILType T) const override {
      return storeFunctionPointerExtraInhabitant(IGF, index, dest);
    }
  };

  /// The @thick function type-info class.
  class FuncTypeInfo : public ScalarTypeInfo<FuncTypeInfo, ReferenceTypeInfo>,
                       public FuncSignatureInfo {
    FuncTypeInfo(CanSILFunctionType formalType, llvm::StructType *storageType,
                 Size size, Alignment align, SpareBitVector &&spareBits,
                 IsPOD_t pod)
      : ScalarTypeInfo(storageType, size, std::move(spareBits), align, pod),
        FuncSignatureInfo(formalType)
    {
    }

  public:
    static const FuncTypeInfo *create(CanSILFunctionType formalType,
                                      llvm::StructType *storageType,
                                      Size size, Alignment align,
                                      SpareBitVector &&spareBits,
                                      IsPOD_t pod) {
      return new FuncTypeInfo(formalType, storageType, size, align,
                              std::move(spareBits), pod);
    }
    
    // Function types do not satisfy allowsOwnership.
    const WeakTypeInfo *
    createWeakStorageType(TypeConverter &TC) const override {
      llvm_unreachable("[weak] function type");
    }
    const TypeInfo *
    createUnownedStorageType(TypeConverter &TC) const override {
      llvm_unreachable("[unowned] function type");
    }
    const TypeInfo *
    createUnmanagedStorageType(TypeConverter &TC) const override {
      llvm_unreachable("@unowned(unsafe) function type");
    }

    llvm::StructType *getStorageType() const {
      return cast<llvm::StructType>(TypeInfo::getStorageType());
    }

    unsigned getExplosionSize() const override {
      return 2;
    }

    void getSchema(ExplosionSchema &schema) const override {
      llvm::StructType *structTy = getStorageType();
      schema.add(ExplosionSchema::Element::forScalar(structTy->getElementType(0)));
      schema.add(ExplosionSchema::Element::forScalar(structTy->getElementType(1)));
    }

    void addToAggLowering(IRGenModule &IGM, SwiftAggLowering &lowering,
                          Size offset) const override {
      auto ptrSize = IGM.getPointerSize();
      llvm::StructType *structTy = getStorageType();
      addScalarToAggLowering(IGM, lowering, structTy->getElementType(0),
                             offset, ptrSize);
      addScalarToAggLowering(IGM, lowering, structTy->getElementType(1),
                             offset + ptrSize, ptrSize);
    }

    Address projectFunction(IRGenFunction &IGF, Address address) const {
      return IGF.Builder.CreateStructGEP(address, 0, Size(0),
                                         address->getName() + ".fn");
    }

    Address projectData(IRGenFunction &IGF, Address address) const {
      return IGF.Builder.CreateStructGEP(address, 1, IGF.IGM.getPointerSize(),
                                         address->getName() + ".data");
    }

    void loadAsCopy(IRGenFunction &IGF, Address address,
                    Explosion &e) const override {
      // Load the function.
      Address fnAddr = projectFunction(IGF, address);
      e.add(IGF.Builder.CreateLoad(fnAddr, fnAddr->getName()+".load"));

      Address dataAddr = projectData(IGF, address);
      auto data = IGF.Builder.CreateLoad(dataAddr);
      if (!isPOD(ResilienceExpansion::Maximal))
        IGF.emitNativeStrongRetain(data, IGF.getDefaultAtomicity());
      e.add(data);
    }

    void loadAsTake(IRGenFunction &IGF, Address addr,
                    Explosion &e) const override {
      // Load the function.
      Address fnAddr = projectFunction(IGF, addr);
      e.add(IGF.Builder.CreateLoad(fnAddr));

      Address dataAddr = projectData(IGF, addr);
      e.add(IGF.Builder.CreateLoad(dataAddr));
    }

    void assign(IRGenFunction &IGF, Explosion &e, Address address,
                bool isOutlined) const override {
      // Store the function pointer.
      Address fnAddr = projectFunction(IGF, address);
      IGF.Builder.CreateStore(e.claimNext(), fnAddr);

      Address dataAddr = projectData(IGF, address);
      auto context = e.claimNext();
      if (isPOD(ResilienceExpansion::Maximal))
        IGF.Builder.CreateStore(context, dataAddr);
      else
        IGF.emitNativeStrongAssign(context, dataAddr);
    }

    void initialize(IRGenFunction &IGF, Explosion &e, Address address,
                    bool isOutlined) const override {
      // Store the function pointer.
      Address fnAddr = projectFunction(IGF, address);
      IGF.Builder.CreateStore(e.claimNext(), fnAddr);

      // Store the data pointer, if any, transferring the +1.
      Address dataAddr = projectData(IGF, address);
      auto context = e.claimNext();
      if (isPOD(ResilienceExpansion::Maximal))
        IGF.Builder.CreateStore(context, dataAddr);
      else
        IGF.emitNativeStrongInit(context, dataAddr);
    }

    void copy(IRGenFunction &IGF, Explosion &src,
              Explosion &dest, Atomicity atomicity) const override {
      src.transferInto(dest, 1);
      auto data = src.claimNext();
      if (!isPOD(ResilienceExpansion::Maximal))
        IGF.emitNativeStrongRetain(data, atomicity);
      dest.add(data);
    }

    void consume(IRGenFunction &IGF, Explosion &src,
                 Atomicity atomicity) const override {
      src.claimNext();
      auto context = src.claimNext();
      if (!isPOD(ResilienceExpansion::Maximal))
        IGF.emitNativeStrongRelease(context, atomicity);
    }

    void fixLifetime(IRGenFunction &IGF, Explosion &src) const override {
      src.claimNext();      
      IGF.emitFixLifetime(src.claimNext());
    }

    void strongRetain(IRGenFunction &IGF, Explosion &e,
                      Atomicity atomicity) const override {
      e.claimNext();
      auto context = e.claimNext();
      if (!isPOD(ResilienceExpansion::Maximal))
        IGF.emitNativeStrongRetain(context, atomicity);
    }

    void strongRelease(IRGenFunction &IGF, Explosion &e,
                       Atomicity atomicity) const override {
      e.claimNext();
      auto context = e.claimNext();
      if (!isPOD(ResilienceExpansion::Maximal))
        IGF.emitNativeStrongRelease(context, atomicity);
    }

    void strongRetainUnowned(IRGenFunction &IGF, Explosion &e,
                             Atomicity atomicity) const override {
      llvm_unreachable("unowned references to functions are not supported");
    }

    void strongRetainUnownedRelease(IRGenFunction &IGF,
                                    Explosion &e,
                                    Atomicity atomicity) const override {
      llvm_unreachable("unowned references to functions are not supported");
    }

    void unownedRetain(IRGenFunction &IGF, Explosion &e,
                       Atomicity atomicity) const override {
      llvm_unreachable("unowned references to functions are not supported");
    }

    void unownedRelease(IRGenFunction &IGF, Explosion &e,
                        Atomicity atomicity) const override {
      llvm_unreachable("unowned references to functions are not supported");
    }

    void unownedLoadStrong(IRGenFunction &IGF, Address src,
                           Explosion &out) const override {
      llvm_unreachable("unowned references to functions are not supported");
    }

    void unownedTakeStrong(IRGenFunction &IGF, Address src,
                           Explosion &out) const override {
      llvm_unreachable("unowned references to functions are not supported");
    }

    void unownedInit(IRGenFunction &IGF, Explosion &in,
                     Address dest) const override {
      llvm_unreachable("unowned references to functions are not supported");
    }

    void unownedAssign(IRGenFunction &IGF, Explosion &in,
                       Address dest) const override {
      llvm_unreachable("unowned references to functions are not supported");
    }

    void destroy(IRGenFunction &IGF, Address addr, SILType T,
                 bool isOutlined) const override {
      auto data = IGF.Builder.CreateLoad(projectData(IGF, addr));
      if (!isPOD(ResilienceExpansion::Maximal))
        IGF.emitNativeStrongRelease(data, IGF.getDefaultAtomicity());
    }

    void packIntoEnumPayload(IRGenFunction &IGF,
                             EnumPayload &payload,
                             Explosion &src,
                             unsigned offset) const override {
      payload.insertValue(IGF, src.claimNext(), offset);
      payload.insertValue(IGF, src.claimNext(),
                          offset + IGF.IGM.getPointerSize().getValueInBits());
    }
    
    void unpackFromEnumPayload(IRGenFunction &IGF,
                               const EnumPayload &payload,
                               Explosion &dest,
                               unsigned offset) const override {
      auto storageTy = getStorageType();
      dest.add(payload.extractValue(IGF, storageTy->getElementType(0), offset));
      dest.add(payload.extractValue(IGF, storageTy->getElementType(1),
                          offset + IGF.IGM.getPointerSize().getValueInBits()));
    }

    bool mayHaveExtraInhabitants(IRGenModule &IGM) const override {
      return true;
    }

    unsigned getFixedExtraInhabitantCount(IRGenModule &IGM) const override {
      return getFunctionPointerExtraInhabitantCount(IGM);
    }

    APInt getFixedExtraInhabitantValue(IRGenModule &IGM,
                                       unsigned bits,
                                       unsigned index) const override {
      return getFunctionPointerFixedExtraInhabitantValue(IGM, bits, index, 0);
    }

    llvm::Value *getExtraInhabitantIndex(IRGenFunction &IGF, Address src,
                                         SILType T) const override {
      src = projectFunction(IGF, src);
      return getFunctionPointerExtraInhabitantIndex(IGF, src);
    }

    APInt getFixedExtraInhabitantMask(IRGenModule &IGM) const override {
      // Only the function pointer value is used for extra inhabitants.
      auto pointerSize = IGM.getPointerSize().getValueInBits();
      APInt bits = APInt::getAllOnesValue(pointerSize);
      bits = bits.zext(pointerSize * 2);
      return bits;
    }

    void storeExtraInhabitant(IRGenFunction &IGF, llvm::Value *index,
                              Address dest, SILType T) const override {
      dest = projectFunction(IGF, dest);
      return storeFunctionPointerExtraInhabitant(IGF, index, dest);
    }
  };

  /// The type-info class for ObjC blocks, which are represented by an ObjC
  /// heap pointer.
  class BlockTypeInfo : public HeapTypeInfo<BlockTypeInfo>,
                        public FuncSignatureInfo
  {
  public:
    BlockTypeInfo(CanSILFunctionType ty,
                  llvm::PointerType *storageType,
                  Size size, SpareBitVector spareBits, Alignment align)
      : HeapTypeInfo(storageType, size, spareBits, align),
        FuncSignatureInfo(ty)
    {
    }

    ReferenceCounting getReferenceCounting() const {
      return ReferenceCounting::Block;
    }
  };
  
  /// The type info class for the on-stack representation of an ObjC block.
  ///
  /// TODO: May not be fixed-layout if we capture generics.
  class BlockStorageTypeInfo final
    : public IndirectTypeInfo<BlockStorageTypeInfo, FixedTypeInfo>
  {
    Size CaptureOffset;
  public:
    BlockStorageTypeInfo(llvm::Type *type, Size size, Alignment align,
                         SpareBitVector &&spareBits,
                         IsPOD_t pod, IsBitwiseTakable_t bt, Size captureOffset)
      : IndirectTypeInfo(type, size, std::move(spareBits), align, pod, bt,
                         IsFixedSize),
        CaptureOffset(captureOffset)
    {}
    
    // The lowered type should be an LLVM struct comprising the block header
    // (IGM.ObjCBlockStructTy) as its first element and the capture as its
    // second.
    
    Address projectBlockHeader(IRGenFunction &IGF, Address storage) const {
      return IGF.Builder.CreateStructGEP(storage, 0, Size(0));
    }
    
    Address projectCapture(IRGenFunction &IGF, Address storage) const {
      return IGF.Builder.CreateStructGEP(storage, 1, CaptureOffset);
    }
    
    // TODO
    // The frontend will currently never emit copy_addr or destroy_addr for
    // block storage.

    void assignWithCopy(IRGenFunction &IGF, Address dest, Address src,
                        SILType T, bool isOutlined) const override {
      IGF.unimplemented(SourceLoc(), "copying @block_storage");
    }
    void initializeWithCopy(IRGenFunction &IGF, Address dest, Address src,
                            SILType T, bool isOutlined) const override {
      IGF.unimplemented(SourceLoc(), "copying @block_storage");
    }
    void destroy(IRGenFunction &IGF, Address addr, SILType T,
                 bool isOutlined) const override {
      IGF.unimplemented(SourceLoc(), "destroying @block_storage");
    }
  };
} // end anonymous namespace

const TypeInfo *TypeConverter::convertBlockStorageType(SILBlockStorageType *T) {
  // The block storage consists of the block header (ObjCBlockStructTy)
  // followed by the lowered type of the capture.
  auto &capture = IGM.getTypeInfoForLowered(T->getCaptureType());
  
  // TODO: Support dynamic-sized captures.
  const auto *fixedCapture = dyn_cast<FixedTypeInfo>(&capture);
  llvm::Type *fixedCaptureTy;
  // The block header is pointer aligned. The capture may be worse aligned.
  Alignment align = IGM.getPointerAlignment();
  Size captureOffset(
    IGM.DataLayout.getStructLayout(IGM.ObjCBlockStructTy)->getSizeInBytes());
  Size size = captureOffset;
  SpareBitVector spareBits =
    SpareBitVector::getConstant(size.getValueInBits(), false);
  IsPOD_t pod = IsNotPOD;
  IsBitwiseTakable_t bt = IsNotBitwiseTakable;
  if (!fixedCapture) {
    IGM.unimplemented(SourceLoc(), "dynamic @block_storage capture");
    fixedCaptureTy = llvm::StructType::get(IGM.getLLVMContext(), {});
  } else {
    fixedCaptureTy = cast<FixedTypeInfo>(capture).getStorageType();
    align = std::max(align, fixedCapture->getFixedAlignment());
    captureOffset = captureOffset.roundUpToAlignment(align);
    spareBits.extendWithSetBits(captureOffset.getValueInBits());
    size = captureOffset + fixedCapture->getFixedSize();
    spareBits.append(fixedCapture->getSpareBits());
    pod = fixedCapture->isPOD(ResilienceExpansion::Maximal);
    bt = fixedCapture->isBitwiseTakable(ResilienceExpansion::Maximal);
  }
  
  llvm::Type *storageElts[] = {
    IGM.ObjCBlockStructTy,
    fixedCaptureTy,
  };
  
  auto storageTy = llvm::StructType::get(IGM.getLLVMContext(), storageElts,
                                         /*packed*/ false);
  return new BlockStorageTypeInfo(storageTy, size, align, std::move(spareBits),
                                  pod, bt, captureOffset);
}

Address irgen::projectBlockStorageCapture(IRGenFunction &IGF,
                                          Address storageAddr,
                                          CanSILBlockStorageType storageTy) {
  auto &tl = IGF.getTypeInfoForLowered(storageTy).as<BlockStorageTypeInfo>();
  return tl.projectCapture(IGF, storageAddr);
}

const TypeInfo *TypeConverter::convertFunctionType(SILFunctionType *T) {
  switch (T->getRepresentation()) {
  case SILFunctionType::Representation::Block:
    return new BlockTypeInfo(CanSILFunctionType(T),
                             IGM.ObjCBlockPtrTy,
                             IGM.getPointerSize(),
                             IGM.getHeapObjectSpareBits(),
                             IGM.getPointerAlignment());
      
  case SILFunctionType::Representation::Thin:
  case SILFunctionType::Representation::Method:
  case SILFunctionType::Representation::WitnessMethod:
  case SILFunctionType::Representation::ObjCMethod:
  case SILFunctionType::Representation::CFunctionPointer:
  case SILFunctionType::Representation::Closure:
    return ThinFuncTypeInfo::create(CanSILFunctionType(T),
                                    IGM.FunctionPtrTy,
                                    IGM.getPointerSize(),
                                    IGM.getPointerAlignment(),
                                    IGM.getFunctionPointerSpareBits());

  case SILFunctionType::Representation::Thick: {
    SpareBitVector spareBits;
    spareBits.append(IGM.getFunctionPointerSpareBits());
    spareBits.append(IGM.getHeapObjectSpareBits());
    
    return FuncTypeInfo::create(CanSILFunctionType(T),
                                IGM.FunctionPairTy,
                                IGM.getPointerSize() * 2,
                                IGM.getPointerAlignment(),
                                std::move(spareBits),
                                IsNotPOD);
  }
  }
  llvm_unreachable("bad function type representation");
}

Signature FuncSignatureInfo::getSignature(IRGenModule &IGM) const {
  // If it's already been filled in, we're done.
  if (TheSignature.isValid())
    return TheSignature;

  // Update the cache and return.
  TheSignature = Signature::getUncached(IGM, FormalType);
  assert(TheSignature.isValid());
  return TheSignature;
}

static const FuncSignatureInfo &
getFuncSignatureInfoForLowered(IRGenModule &IGM, CanSILFunctionType type) {
  auto &ti = IGM.getTypeInfoForLowered(type);
  switch (type->getRepresentation()) {
  case SILFunctionType::Representation::Block:
    return ti.as<BlockTypeInfo>();
  case SILFunctionType::Representation::Thin:
  case SILFunctionType::Representation::CFunctionPointer:
  case SILFunctionType::Representation::Method:
  case SILFunctionType::Representation::WitnessMethod:
  case SILFunctionType::Representation::ObjCMethod:
  case SILFunctionType::Representation::Closure:
    return ti.as<ThinFuncTypeInfo>();
  case SILFunctionType::Representation::Thick:
    return ti.as<FuncTypeInfo>();
  }
  llvm_unreachable("bad function type representation");
}

Signature
IRGenModule::getSignature(CanSILFunctionType type) {
  auto &sigInfo = getFuncSignatureInfoForLowered(*this, type);
  return sigInfo.getSignature(*this);
}

llvm::FunctionType *
IRGenModule::getFunctionType(CanSILFunctionType type,
                             llvm::AttributeList &attrs,
                             ForeignFunctionInfo *foreignInfo) {
  auto &sigInfo = getFuncSignatureInfoForLowered(*this, type);
  Signature sig = sigInfo.getSignature(*this);
  attrs = sig.getAttributes();
  if (foreignInfo) *foreignInfo = sig.getForeignInfo();
  return sig.getType();
}

ForeignFunctionInfo
IRGenModule::getForeignFunctionInfo(CanSILFunctionType type) {
  if (type->getLanguage() == SILFunctionLanguage::Swift)
    return ForeignFunctionInfo();

  auto &sigInfo = getFuncSignatureInfoForLowered(*this, type);
  return sigInfo.getSignature(*this).getForeignInfo();
}

static void emitApplyArgument(IRGenFunction &IGF,
                              SILParameterInfo origParam,
                              SILParameterInfo substParam,
                              Explosion &in,
                              Explosion &out) {
  auto silConv = IGF.IGM.silConv;

  bool isSubstituted =
      (silConv.getSILType(substParam) != silConv.getSILType(origParam));

  // For indirect arguments, we just need to pass a pointer.
  if (silConv.isSILIndirect(origParam)) {
    // This address is of the substituted type.
    auto addr = in.claimNext();
    
    // If a substitution is in play, just bitcast the address.
    if (isSubstituted) {
      auto origType =
          IGF.IGM.getStoragePointerType(silConv.getSILType(origParam));
      addr = IGF.Builder.CreateBitCast(addr, origType);
    }
    
    out.add(addr);
    return;
  }
  assert(!silConv.isSILIndirect(origParam)
         && "Unexpected opaque apply parameter.");

  // Otherwise, it's an explosion, which we may need to translate,
  // both in terms of explosion level and substitution levels.

  // Handle the last unsubstituted case.
  if (!isSubstituted) {
    auto &substArgTI =
        cast<LoadableTypeInfo>(IGF.getTypeInfo(silConv.getSILType(substParam)));
    substArgTI.reexplode(IGF, in, out);
    return;
  }

  reemitAsUnsubstituted(IGF, silConv.getSILType(origParam),
                        silConv.getSILType(substParam), in, out);
}

static CanType getArgumentLoweringType(CanType type,
                                       SILParameterInfo paramInfo) {
  switch (paramInfo.getConvention()) {
  // Capture value parameters by value, consuming them.
  case ParameterConvention::Direct_Owned:
  case ParameterConvention::Direct_Unowned:
  case ParameterConvention::Direct_Guaranteed:
  case ParameterConvention::Indirect_In:
  case ParameterConvention::Indirect_In_Constant:
  case ParameterConvention::Indirect_In_Guaranteed:
    return type;

  // Capture inout parameters by pointer.
  case ParameterConvention::Indirect_Inout:
  case ParameterConvention::Indirect_InoutAliasable:
    return CanInOutType::get(type);
  }
}

static bool isABIIgnoredParameterWithoutStorage(IRGenModule &IGM,
                                                IRGenFunction &IGF,
                                                CanSILFunctionType substType,
                                                unsigned paramIdx) {
  auto param = substType->getParameters()[paramIdx];
  SILType argType = IGM.silConv.getSILType(param);
  auto argLoweringTy =
    getArgumentLoweringType(argType.getSwiftRValueType(), param);
  auto &ti = IGF.getTypeInfoForLowered(argLoweringTy);
  // Empty values don't matter.
  return ti.getSchema().size() == 0 && !param.isFormalIndirect();
}

/// Find the parameter index for the one (assuming there was only one) partially
/// applied argument ignoring empty types that are not passed as part of the
/// ABI.
static unsigned findSinglePartiallyAppliedParameterIndexIgnoringEmptyTypes(
    IRGenFunction &IGF, CanSILFunctionType substType,
    CanSILFunctionType outType) {
  auto substParameters = substType->getParameters();
  auto outParamters = outType->getParameters();
  unsigned firstNonEmpty = -1U;
  for (unsigned paramIdx = outParamters.size() ; paramIdx != substParameters.size(); ++paramIdx) {
    bool isEmpty =
        isABIIgnoredParameterWithoutStorage(IGF.IGM, IGF, substType, paramIdx);
    assert((isEmpty || firstNonEmpty == -1U) && "Expect at most one partially "
                                                "applied that is passed as an "
                                                "ABI argument");
    if (!isEmpty)
      firstNonEmpty = paramIdx;
  }
  assert(firstNonEmpty != -1U);
  return firstNonEmpty;
}

/// Emit the forwarding stub function for a partial application.
///
/// If 'layout' is null, there is a single captured value of
/// Swift-refcountable type that is being used directly as the
/// context object.
static llvm::Function *emitPartialApplicationForwarder(IRGenModule &IGM,
                                   const Optional<FunctionPointer> &staticFnPtr,
                                   bool calleeHasContext,
                                   const Signature &origSig,
                                   CanSILFunctionType origType,
                                   CanSILFunctionType substType,
                                   CanSILFunctionType outType,
                                   SubstitutionList subs,
                                   HeapLayout const *layout,
                                   ArrayRef<ParameterConvention> conventions) {
  auto outSig = IGM.getSignature(outType);
  llvm::AttributeList outAttrs = outSig.getAttributes();
  llvm::FunctionType *fwdTy = outSig.getType();
  SILFunctionConventions outConv(outType, IGM.getSILModule());

  StringRef FnName;
  if (staticFnPtr)
    FnName = staticFnPtr->getPointer()->getName();

  IRGenMangler Mangler;
  std::string thunkName = Mangler.manglePartialApplyForwarder(FnName);

  // FIXME: Maybe cache the thunk by function and closure types?.
  llvm::Function *fwd =
    llvm::Function::Create(fwdTy, llvm::Function::InternalLinkage,
                           llvm::StringRef(thunkName), &IGM.Module);
  fwd->setCallingConv(outSig.getCallingConv());

  fwd->setAttributes(outAttrs);
  // Merge initial attributes with outAttrs.
  llvm::AttrBuilder b;
  IGM.constructInitialFnAttributes(b);
  fwd->addAttributes(llvm::AttributeList::FunctionIndex, b);

  IRGenFunction subIGF(IGM, fwd);
  if (IGM.DebugInfo)
    IGM.DebugInfo->emitArtificialFunction(subIGF, fwd);
  
  Explosion origParams = subIGF.collectParameters();

  // Create a new explosion for potentially reabstracted parameters.
  Explosion args;

  Address resultValueAddr;

  {
    // Lower the forwarded arguments in the original function's generic context.
    GenericContextScope scope(IGM, origType->getGenericSignature());

    SILFunctionConventions origConv(origType, IGM.getSILModule());
    auto &outResultTI = IGM.getTypeInfo(outConv.getSILResultType());
    auto &nativeResultSchema = outResultTI.nativeReturnValueSchema(IGM);
    auto &origResultTI = IGM.getTypeInfo(origConv.getSILResultType());
    auto &origNativeSchema = origResultTI.nativeReturnValueSchema(IGM);

    // Forward the indirect return values. We might have to reabstract the
    // return value.
    if (nativeResultSchema.requiresIndirect()) {
      assert(origNativeSchema.requiresIndirect());
      auto resultAddr = origParams.claimNext();
      resultAddr = subIGF.Builder.CreateBitCast(
          resultAddr, IGM.getStoragePointerType(origConv.getSILResultType()));
      args.add(resultAddr);
    } else if (origNativeSchema.requiresIndirect()) {
      assert(!nativeResultSchema.requiresIndirect());
      auto stackAddr = outResultTI.allocateStack(
          subIGF, outConv.getSILResultType(), false, "return.temp");
      resultValueAddr = stackAddr.getAddress();
      auto resultAddr = subIGF.Builder.CreateBitCast(
          resultValueAddr,
          IGM.getStoragePointerType(origConv.getSILResultType()));
      args.add(resultAddr.getAddress());
    }

    for (auto resultType : origConv.getIndirectSILResultTypes()) {
      auto addr = origParams.claimNext();
      addr = subIGF.Builder.CreateBitCast(
          addr, IGM.getStoragePointerType(resultType));
      args.add(addr);
    }
    
    // Reemit the parameters as unsubstituted.
    for (unsigned i = 0; i < outType->getParameters().size(); ++i) {
      auto origParamInfo = origType->getParameters()[i];
      auto &ti = IGM.getTypeInfoForLowered(origParamInfo.getType());
      auto schema = ti.getSchema();
      
      auto origParamSILType = IGM.silConv.getSILType(origParamInfo);
      // Forward the address of indirect value params.
      auto &nativeSchemaOrigParam = ti.nativeParameterValueSchema(IGM);
      bool isIndirectParam = origConv.isSILIndirect(origParamInfo);
      if (!isIndirectParam && nativeSchemaOrigParam.requiresIndirect()) {
        auto addr = origParams.claimNext();
        if (addr->getType() != ti.getStorageType()->getPointerTo())
          addr = subIGF.Builder.CreateBitCast(addr,
                                           ti.getStorageType()->getPointerTo());
        args.add(addr);
        continue;
      }

      auto outTypeParamInfo = outType->getParameters()[i];
      // Indirect parameters need no mapping through the native calling
      // convention.
      if (isIndirectParam) {
        emitApplyArgument(subIGF, origParamInfo, outTypeParamInfo, origParams,
                          args);
        continue;
      }

      // Map from the native calling convention into the explosion schema.
      auto outTypeParamSILType = IGM.silConv.getSILType(origParamInfo);
      auto &nativeSchemaOutTypeParam =
          IGM.getTypeInfo(outTypeParamSILType).nativeParameterValueSchema(IGM);
      Explosion nativeParam;
      origParams.transferInto(nativeParam, nativeSchemaOutTypeParam.size());

      bindPolymorphicParameter(subIGF, origType, substType, nativeParam, i);

      Explosion nonNativeParam = nativeSchemaOutTypeParam.mapFromNative(
          subIGF.IGM, subIGF, nativeParam, outTypeParamSILType);
      assert(nativeParam.empty());

      // Emit unsubstituted argument for call.
      Explosion nonNativeApplyArg;
      emitApplyArgument(subIGF, origParamInfo, outTypeParamInfo, nonNativeParam,
                        nonNativeApplyArg);
      assert(nonNativeParam.empty());
      // Map back from the explosion scheme to the native calling convention for
      // the call.
      Explosion nativeApplyArg = nativeSchemaOrigParam.mapIntoNative(
          subIGF.IGM, subIGF, nonNativeApplyArg, origParamSILType, false);
      assert(nonNativeApplyArg.empty());
      nativeApplyArg.transferInto(args, nativeApplyArg.size());
    }
  }

  struct AddressToDeallocate {
    SILType Type;
    const TypeInfo &TI;
    StackAddress Addr;
  };
  SmallVector<AddressToDeallocate, 4> addressesToDeallocate;

  bool dependsOnContextLifetime = false;
  bool consumesContext;
  bool needsAllocas = false;
  
  switch (outType->getCalleeConvention()) {
  case ParameterConvention::Direct_Owned:
    consumesContext = true;
    break;
  case ParameterConvention::Direct_Unowned:
  case ParameterConvention::Direct_Guaranteed:
    consumesContext = false;
    break;
  case ParameterConvention::Indirect_Inout:
  case ParameterConvention::Indirect_InoutAliasable:
  case ParameterConvention::Indirect_In:
  case ParameterConvention::Indirect_In_Constant:
  case ParameterConvention::Indirect_In_Guaranteed:
    llvm_unreachable("indirect callables not supported");
  }

  // Lower the captured arguments in the original function's generic context.
  GenericContextScope scope(IGM, origType->getGenericSignature());

  // This is where the context parameter appears.
  llvm::Value *rawData = nullptr;
  Address data;
  unsigned nextCapturedField = 0;
  if (!layout) {
    rawData = origParams.claimNext();
  } else if (!layout->isKnownEmpty()) {
    rawData = origParams.claimNext();
    data = layout->emitCastTo(subIGF, rawData);

    // Restore type metadata bindings, if we have them.
    if (layout->hasBindings()) {
      auto bindingLayout = layout->getElement(nextCapturedField++);
      // The bindings should be fixed-layout inside the object, so we can
      // pass None here. If they weren't, we'd have a chicken-egg problem.
      auto bindingsAddr = bindingLayout.project(subIGF, data, /*offsets*/ None);
      layout->getBindings().restore(subIGF, bindingsAddr);
    }

  // There's still a placeholder to claim if the target type is thick
  // or there's an error result.
  } else if (outType->getRepresentation()==SILFunctionTypeRepresentation::Thick
             || outType->hasErrorResult()) {
    llvm::Value *contextPtr = origParams.claimNext(); (void)contextPtr;
    assert(contextPtr->getType() == IGM.RefCountedPtrTy);
  }

  Explosion polyArgs;

  // Emit the polymorphic arguments.
  assert((subs.empty() != hasPolymorphicParameters(origType) ||
         (subs.empty() && origType->getRepresentation() ==
             SILFunctionTypeRepresentation::WitnessMethod))
         && "should have substitutions iff original function is generic");
  WitnessMetadata witnessMetadata;

  // If we have a layout we might have to bind polymorphic arguments from the
  // captured arguments which we will do later. Otherwise, we have to
  // potentially bind polymorphic arguments from the context if it was a
  // partially applied argument.
  bool hasPolymorphicParams = hasPolymorphicParameters(origType);
  if (!layout && hasPolymorphicParams) {
    assert(conventions.size() == 1);
    // We could have either partially applied an argument from the function
    // signature or otherwise we could have a closure context to forward. We only
    // care for the former for the purpose of reconstructing polymorphic
    // parameters from regular arguments.
    if (!calleeHasContext) {
      unsigned paramI =
          findSinglePartiallyAppliedParameterIndexIgnoringEmptyTypes(
              subIGF, substType, outType);
      auto paramInfo = substType->getParameters()[paramI];
      auto &ti = IGM.getTypeInfoForLowered(paramInfo.getType());
      Explosion param;
      param.add(subIGF.Builder.CreateBitCast(rawData, ti.getStorageType()));
      bindPolymorphicParameter(subIGF, origType, substType, param, paramI);
      (void)param.claimAll();
    }

    SubstitutionMap subMap;
    if (auto genericSig = origType->getGenericSignature())
      subMap = genericSig->getSubstitutionMap(subs);
    emitPolymorphicArguments(subIGF, origType, subMap,
                             &witnessMetadata, polyArgs);
  }

  auto haveContextArgument =
      calleeHasContext || hasSelfContextParameter(origType);

  // Witness method calls expect self, followed by the self type followed by,
  // the witness table at the end of the parameter list. But polymorphic
  // arguments come before this.
  bool isWitnessMethodCallee = origType->getRepresentation() ==
      SILFunctionTypeRepresentation::WitnessMethod;
  Explosion witnessMethodSelfValue;

  // If there's a data pointer required, but it's a swift-retainable
  // value being passed as the context, just forward it down.
  if (!layout) {
    assert(conventions.size() == 1);

    // We need to retain the parameter if:
    //   - we received at +0 (either) and are passing as owned
    //   - we received as unowned and are passing as guaranteed
    auto argConvention = conventions[nextCapturedField++];
    switch (argConvention) {
    case ParameterConvention::Indirect_In:
    case ParameterConvention::Indirect_In_Constant:
    case ParameterConvention::Direct_Owned:
      if (!consumesContext) subIGF.emitNativeStrongRetain(rawData, subIGF.getDefaultAtomicity());
      break;

    case ParameterConvention::Indirect_In_Guaranteed:
    case ParameterConvention::Direct_Guaranteed:
      dependsOnContextLifetime = true;
      if (outType->getCalleeConvention() ==
            ParameterConvention::Direct_Unowned) {
        subIGF.emitNativeStrongRetain(rawData, subIGF.getDefaultAtomicity());
        consumesContext = true;
      }
      break;

    case ParameterConvention::Direct_Unowned:
      // Make sure we release later if we received at +1.
      if (consumesContext)
        dependsOnContextLifetime = true;
      break;

    case ParameterConvention::Indirect_Inout:
    case ParameterConvention::Indirect_InoutAliasable:
      llvm_unreachable("should never happen!");
    }

    // FIXME: The naming and documentation here isn't ideal. This
    // parameter is always present which is evident since we always
    // grab a type to cast to, but sometimes after the polymorphic
    // arguments. This is just following the lead of existing (and not
    // terribly easy to follow) code.

    // If there is a context argument, it comes after the polymorphic
    // arguments.
    auto argIndex = args.size();
    if (haveContextArgument)
      argIndex += polyArgs.size();

    llvm::Type *expectedArgTy = origSig.getType()->getParamType(argIndex);

    llvm::Value *argValue;
    if (isIndirectFormalParameter(argConvention)) {
      // We can use rawData's type for the alloca because it is a swift
      // retainable value. Defensively, give it that type. We can't use the
      // expectedArgType because it might be a generic parameter and therefore
      // have opaque storage.
      auto RetainableValue = rawData;
      if (RetainableValue->getType() != subIGF.IGM.RefCountedPtrTy)
        RetainableValue = subIGF.Builder.CreateBitCast(
            RetainableValue, subIGF.IGM.RefCountedPtrTy);
      auto temporary = subIGF.createAlloca(RetainableValue->getType(),
                                           subIGF.IGM.getPointerAlignment(),
                                           "partial-apply.context");
      subIGF.Builder.CreateStore(RetainableValue, temporary);
      argValue = temporary.getAddress();
      argValue = subIGF.Builder.CreateBitCast(argValue, expectedArgTy);
    } else {
      argValue = subIGF.Builder.CreateBitCast(rawData, expectedArgTy);
    }
    args.add(argValue);

  // If there's a data pointer required, grab it and load out the
  // extra, previously-curried parameters.
  } else {
    unsigned origParamI = outType->getParameters().size();
    assert(layout->getElements().size() == conventions.size()
           && "conventions don't match context layout");

    // Calculate non-fixed field offsets.
    HeapNonFixedOffsets offsets(subIGF, *layout);

    // Perform the loads.
    for (unsigned n = layout->getElements().size();
         nextCapturedField < n;
         ++nextCapturedField) {
      auto &fieldLayout = layout->getElement(nextCapturedField);
      auto &fieldTy = layout->getElementTypes()[nextCapturedField];
      auto fieldConvention = conventions[nextCapturedField];
      Address fieldAddr = fieldLayout.project(subIGF, data, offsets);
      auto &fieldTI = fieldLayout.getType();
      auto fieldSchema = fieldTI.getSchema();
      
      Explosion param;
      switch (fieldConvention) {
      case ParameterConvention::Indirect_In:
      case ParameterConvention::Indirect_In_Constant: {
        // The +1 argument is passed indirectly, so we need to copy into a
        // temporary.
        needsAllocas = true;
        auto stackAddr = fieldTI.allocateStack(subIGF, fieldTy, false, "arg.temp");
        auto addressPointer = stackAddr.getAddress().getAddress();
        fieldTI.initializeWithCopy(subIGF, stackAddr.getAddress(), fieldAddr,
                                   fieldTy, false);
        param.add(addressPointer);
        
        // Remember to deallocate later.
        addressesToDeallocate.push_back(
            AddressToDeallocate{fieldTy, fieldTI, stackAddr});

        break;
      }
      case ParameterConvention::Indirect_In_Guaranteed:
        // The argument is +0, so we can use the address of the param in
        // the context directly.
        param.add(fieldAddr.getAddress());
        dependsOnContextLifetime = true;
        break;
      case ParameterConvention::Indirect_Inout:
      case ParameterConvention::Indirect_InoutAliasable:
        // Load the address of the inout parameter.
        cast<LoadableTypeInfo>(fieldTI).loadAsCopy(subIGF, fieldAddr, param);
        break;
      case ParameterConvention::Direct_Guaranteed:
      case ParameterConvention::Direct_Unowned:
        // If the type is nontrivial, keep the context alive since the field
        // depends on the context to not be deallocated.
        if (!fieldTI.isPOD(ResilienceExpansion::Maximal))
          dependsOnContextLifetime = true;

        // Load these parameters directly. We can "take" since the parameter is
        // +0. This can happen since the context will keep the parameter alive.
        cast<LoadableTypeInfo>(fieldTI).loadAsTake(subIGF, fieldAddr, param);
        break;
      case ParameterConvention::Direct_Owned:
        // Copy the value out at +1.
        cast<LoadableTypeInfo>(fieldTI).loadAsCopy(subIGF, fieldAddr, param);
        break;
      }
      
      // Reemit the capture params as unsubstituted.

      // Skip empty parameters.
      while (origParamI < origType->getParameters().size()) {
        if (!isABIIgnoredParameterWithoutStorage(IGM, subIGF, substType,
                                                 origParamI))
          break;
        origParamI++;
      }

      if (origParamI < origType->getParameters().size()) {
        Explosion origParam;
        auto origParamInfo = origType->getParameters()[origParamI];
        if (hasPolymorphicParams)
          bindPolymorphicParameter(subIGF, origType, substType, param,
                                   origParamI);
        emitApplyArgument(subIGF, origParamInfo,
                          substType->getParameters()[origParamI],
                          param, origParam);
        bool isWitnessMethodCalleeSelf = (isWitnessMethodCallee &&
            origParamI + 1 == origType->getParameters().size());
        needsAllocas |= addNativeArgument(
            subIGF, origParam, origParamInfo,
            isWitnessMethodCalleeSelf ? witnessMethodSelfValue : args, false);
        ++origParamI;
      } else {
        args.add(param.claimAll());
      }
      
    }
    
    // If the parameters can live independent of the context, release it now
    // so we can tail call. The safety of this assumes that neither this release
    // nor any of the loads can throw.
    if (consumesContext && !dependsOnContextLifetime && rawData)
      subIGF.emitNativeStrongRelease(rawData, subIGF.getDefaultAtomicity());

    // Now that we have bound generic parameters from the captured arguments
    // emit the polymorphic arguments.
    if (hasPolymorphicParameters(origType)) {
      SubstitutionMap subMap;
      if (auto genericSig = origType->getGenericSignature())
        subMap = genericSig->getSubstitutionMap(subs);
      emitPolymorphicArguments(subIGF, origType, subMap,
                               &witnessMetadata, polyArgs);
    }
  }

  // Derive the callee function pointer.
  auto fnTy = origSig.getType()->getPointerTo();
  FunctionPointer fnPtr = [&]() -> FunctionPointer {
    // If we found a function pointer statically, great.
    if (staticFnPtr) {
      assert(staticFnPtr->getPointer()->getType() == fnTy &&
             "static function type mismatch?!");
      return *staticFnPtr;
    }

    // Otherwise, it was the last thing we added to the layout.

    // The dynamic function pointer is packed "last" into the context,
    // and we pulled it out as an argument.  Just pop it off.
    auto fnPtr = args.takeLast();

    // It comes out of the context as an i8*. Cast to the function type.
    fnPtr = subIGF.Builder.CreateBitCast(fnPtr, fnTy);

    return FunctionPointer(fnPtr, origSig);
  }();

  // Derive the context argument if needed.  This is either:
  //   - the saved context argument, in which case it was the last
  //     thing we added to the layout other than a possible non-static
  //     function pointer (which we already popped off of 'args'); or
  //   - 'self', in which case it was the last formal argument.
  // In either case, it's the last thing in 'args'.
  llvm::Value *fnContext = nullptr;
  if (haveContextArgument)
    fnContext = args.takeLast();

  polyArgs.transferInto(args, polyArgs.size());

  // If we have a witness method call, the inner context is the
  // witness table. Metadata for Self is derived inside the partial
  // application thunk and doesn't need to be stored in the outer
  // context.
  if (isWitnessMethodCallee) {
    assert(fnContext->getType() == IGM.Int8PtrTy);
    llvm::Value *wtable = subIGF.Builder.CreateBitCast(
        fnContext, IGM.WitnessTablePtrTy);
    assert(wtable->getType() == IGM.WitnessTablePtrTy);
    witnessMetadata.SelfWitnessTable = wtable;

  // Okay, this is where the callee context goes.
  } else if (fnContext) {
    args.add(fnContext);

  // Pass a placeholder for thin function calls.
  } else if (origType->hasErrorResult()) {
    args.add(llvm::UndefValue::get(IGM.RefCountedPtrTy));
  }

  // Add the witness methods self argument before the error parameter after the
  // polymorphic arguments.
  if (isWitnessMethodCallee)
    witnessMethodSelfValue.transferInto(args, witnessMethodSelfValue.size());

  // Pass down the error result.
  if (origType->hasErrorResult()) {
    llvm::Value *errorResultPtr = origParams.claimNext();
    args.add(errorResultPtr);
  }

  assert(origParams.empty());

  if (isWitnessMethodCallee) {
    assert(witnessMetadata.SelfMetadata->getType() == IGM.TypeMetadataPtrTy);
    args.add(witnessMetadata.SelfMetadata);
    assert(witnessMetadata.SelfWitnessTable->getType() == IGM.WitnessTablePtrTy);
    args.add(witnessMetadata.SelfWitnessTable);
  }

  llvm::CallInst *call = subIGF.Builder.CreateCall(fnPtr, args.claimAll());
  
  if (addressesToDeallocate.empty() && !needsAllocas &&
      (!consumesContext || !dependsOnContextLifetime))
    call->setTailCall();

  // Deallocate everything we allocated above.
  // FIXME: exceptions?
  for (auto &entry : addressesToDeallocate) {
    entry.TI.deallocateStack(subIGF, entry.Addr, entry.Type);
  }
  
  // If the parameters depended on the context, consume the context now.
  if (rawData && consumesContext && dependsOnContextLifetime)
    subIGF.emitNativeStrongRelease(rawData, subIGF.getDefaultAtomicity());

  // Reabstract the result value as substituted.
  SILFunctionConventions origConv(origType, IGM.getSILModule());
  auto &outResultTI = IGM.getTypeInfo(outConv.getSILResultType());
  auto &nativeResultSchema = outResultTI.nativeReturnValueSchema(IGM);
  if (call->getType()->isVoidTy()) {
    if (!resultValueAddr.isValid())
      subIGF.Builder.CreateRetVoid();
    else {
      // Okay, we have called a function that expects an indirect return type
      // but the partially applied return type is direct.
      assert(!nativeResultSchema.requiresIndirect());
      Explosion loadedResult;
      cast<LoadableTypeInfo>(outResultTI)
          .loadAsTake(subIGF, resultValueAddr, loadedResult);
      Explosion nativeResult = nativeResultSchema.mapIntoNative(
          IGM, subIGF, loadedResult, outConv.getSILResultType(), false);
      outResultTI.deallocateStack(subIGF, resultValueAddr,
                                  outConv.getSILResultType());
      if (nativeResult.size() == 1)
        subIGF.Builder.CreateRet(nativeResult.claimNext());
      else {
        llvm::Value *nativeAgg =
            llvm::UndefValue::get(nativeResultSchema.getExpandedType(IGM));
        for (unsigned i = 0, e = nativeResult.size(); i != e; ++i) {
          auto *elt = nativeResult.claimNext();
          nativeAgg = subIGF.Builder.CreateInsertValue(nativeAgg, elt, i);
        }
        subIGF.Builder.CreateRet(nativeAgg);
      }
    }
  } else {
    llvm::Value *callResult = call;
    // If the result type is dependent on a type parameter we might have to
    // cast to the result type - it could be substituted.
    if (origConv.getSILResultType().hasTypeParameter()) {
      auto ResType = fwd->getReturnType();
      callResult = subIGF.Builder.CreateBitCast(callResult, ResType);
    }
    subIGF.Builder.CreateRet(callResult);
  }

  return fwd;
}

/// Emit a partial application thunk for a function pointer applied to a partial
/// set of argument values.
void irgen::emitFunctionPartialApplication(
    IRGenFunction &IGF, SILFunction &SILFn, const FunctionPointer &fn,
    llvm::Value *fnContext, Explosion &args, ArrayRef<SILParameterInfo> params,
    SubstitutionList subs, CanSILFunctionType origType,
    CanSILFunctionType substType, CanSILFunctionType outType, Explosion &out,
    bool isOutlined) {
  // If we have a single Swift-refcounted context value, we can adopt it
  // directly as our closure context without creating a box and thunk.
  enum HasSingleSwiftRefcountedContext { Maybe, Yes, No, Thunkable }
    hasSingleSwiftRefcountedContext = Maybe;
  Optional<ParameterConvention> singleRefcountedConvention;
  
  SmallVector<const TypeInfo *, 4> argTypeInfos;
  SmallVector<SILType, 4> argValTypes;
  SmallVector<ParameterConvention, 4> argConventions;

  // Reserve space for polymorphic bindings.
  SubstitutionMap subMap;
  if (auto genericSig = origType->getGenericSignature())
    subMap = genericSig->getSubstitutionMap(subs);
  auto bindings = NecessaryBindings::forFunctionInvocations(IGF.IGM,
                                                            origType, subMap);
  if (!bindings.empty()) {
    hasSingleSwiftRefcountedContext = No;
    auto bindingsSize = bindings.getBufferSize(IGF.IGM);
    auto &bindingsTI = IGF.IGM.getOpaqueStorageTypeInfo(bindingsSize,
                                                 IGF.IGM.getPointerAlignment());
    argValTypes.push_back(SILType());
    argTypeInfos.push_back(&bindingsTI);
    argConventions.push_back(ParameterConvention::Direct_Unowned);
  }

  // Collect the type infos for the context parameters.
  for (auto param : params) {
    SILType argType = IGF.IGM.silConv.getSILType(param);

    auto argLoweringTy =
        getArgumentLoweringType(argType.getSwiftRValueType(), param);

    auto &ti = IGF.getTypeInfoForLowered(argLoweringTy);

    // Empty values don't matter.
    auto schema = ti.getSchema();
    if (schema.size() == 0 && !param.isFormalIndirect())
      continue;

    argValTypes.push_back(argType);
    argConventions.push_back(param.getConvention());
    argTypeInfos.push_back(&ti);

    // Update the single-swift-refcounted check, unless we already ruled that
    // out.
    if (hasSingleSwiftRefcountedContext == No)
      continue;
    
    
    // Adding nonempty values when we already have a single refcounted pointer
    // means we don't have a single value anymore.
    if (hasSingleSwiftRefcountedContext != Maybe) {
      hasSingleSwiftRefcountedContext = No;
      continue;
    }
      
    if (ti.isSingleSwiftRetainablePointer(ResilienceExpansion::Maximal)) {
      hasSingleSwiftRefcountedContext = Yes;
      singleRefcountedConvention = param.getConvention();
    } else {
      hasSingleSwiftRefcountedContext = No;
    }
  }

  // We can't just bitcast if there's an error parameter to forward.
  // This is an unfortunate restriction arising from the fact that a
  // thin throwing function will have the signature:
  //   %result (%arg*, %context*, %error*)
  // but the output signature needs to be
  //   %result (%context*, %error*)
  //
  // 'swifterror' fixes this physically, but there's still a risk of
  // miscompiles because the LLVM optimizer may forward arguments
  // positionally without considering 'swifterror'.
  //
  // Note, however, that we will override this decision below if the
  // only thing we have to forward is already a context pointer.
  // That's fine.
  //
  // The proper long-term fix is that closure functions should be
  // emitted with a convention that takes the closure box as the
  // context parameter.  When we do that, all of this code will
  // disappear.
  if (hasSingleSwiftRefcountedContext == Yes &&
      origType->hasErrorResult()) {
    hasSingleSwiftRefcountedContext = Thunkable;
  }
  
  // If the function pointer is a witness method call, include the witness
  // table in the context.
  if (origType->getRepresentation() ==
        SILFunctionTypeRepresentation::WitnessMethod) {
    llvm::Value *wtable = fnContext;
    assert(wtable->getType() == IGF.IGM.WitnessTablePtrTy);

    // TheRawPointerType lowers as i8*, not i8**.
    args.add(IGF.Builder.CreateBitCast(wtable, IGF.IGM.Int8PtrTy));

    argValTypes.push_back(SILType::getRawPointerType(IGF.IGM.Context));
    argTypeInfos.push_back(
         &IGF.getTypeInfoForLowered(IGF.IGM.Context.TheRawPointerType));
    argConventions.push_back(ParameterConvention::Direct_Unowned);
    hasSingleSwiftRefcountedContext = No;

  // Otherwise, we might have a reference-counted context pointer.
  } else if (fnContext) {
    args.add(fnContext);
    argValTypes.push_back(SILType::getNativeObjectType(IGF.IGM.Context));
    argConventions.push_back(origType->getCalleeConvention());
    argTypeInfos.push_back(
         &IGF.getTypeInfoForLowered(IGF.IGM.Context.TheNativeObjectType));
    // If this is the only context argument we end up with, we can just share
    // it.
    if (args.size() == 1) {
      assert(bindings.empty());
      hasSingleSwiftRefcountedContext = Yes;
      singleRefcountedConvention = origType->getCalleeConvention();
    }
  }
  
  // If we have a single refcounted pointer context (and no polymorphic args
  // to capture), and the dest ownership semantics match the parameter's,
  // skip building the box and thunk and just take the pointer as
  // context.
  // TODO: We can only do this and use swiftself if all our swiftcc emit the
  // last parameter that fits into a register as swiftself.
  // We should get this optimization back using the @convention(closure) whose
  // box argument should just be swift self.
  if (/* DISABLES CODE */ (false) &&
      !origType->isPolymorphic() &&
      hasSingleSwiftRefcountedContext == Yes &&
      outType->getCalleeConvention() == *singleRefcountedConvention) {
    assert(args.size() == 1);
    auto fnPtr = fn.getPointer();
    fnPtr = IGF.Builder.CreateBitCast(fnPtr, IGF.IGM.Int8PtrTy);
    out.add(fnPtr);
    llvm::Value *ctx = args.claimNext();
    ctx = IGF.Builder.CreateBitCast(ctx, IGF.IGM.RefCountedPtrTy);
    out.add(ctx);
    return;
  }
  
  Optional<FunctionPointer> staticFn;
  if (fn.isConstant()) staticFn = fn;

  // If the function pointer is dynamic, include it in the context.
  size_t nonStaticFnIndex = ~size_t(0);
  if (!staticFn) {
    nonStaticFnIndex = argTypeInfos.size();
    argValTypes.push_back(SILType::getRawPointerType(IGF.IGM.Context));
    argTypeInfos.push_back(
         &IGF.getTypeInfoForLowered(IGF.IGM.Context.TheRawPointerType));
    argConventions.push_back(ParameterConvention::Direct_Unowned);
    hasSingleSwiftRefcountedContext = No;
  }

  // If we only need to capture a single Swift-refcounted object, we
  // still need to build a thunk, but we don't need to allocate anything.
  if ((hasSingleSwiftRefcountedContext == Yes ||
       hasSingleSwiftRefcountedContext == Thunkable) &&
      *singleRefcountedConvention != ParameterConvention::Indirect_Inout &&
      *singleRefcountedConvention !=
        ParameterConvention::Indirect_InoutAliasable) {
    assert(bindings.empty());
    assert(args.size() == 1);

    auto origSig = IGF.IGM.getSignature(origType);

    llvm::Value *forwarder =
      emitPartialApplicationForwarder(IGF.IGM, staticFn, fnContext != nullptr,
                                      origSig, origType, substType,
                                      outType, subs, nullptr, argConventions);
    forwarder = IGF.Builder.CreateBitCast(forwarder, IGF.IGM.Int8PtrTy);
    out.add(forwarder);

    llvm::Value *ctx = args.claimNext();
    if (isIndirectFormalParameter(*singleRefcountedConvention))
      ctx = IGF.Builder.CreateLoad(ctx, IGF.IGM.getPointerAlignment());
    ctx = IGF.Builder.CreateBitCast(ctx, IGF.IGM.RefCountedPtrTy);
    out.add(ctx);
    return;
  }

  // Store the context arguments on the heap.
  assert(argValTypes.size() == argTypeInfos.size()
         && argTypeInfos.size() == argConventions.size()
         && "argument info lists out of sync");
  HeapLayout layout(IGF.IGM, LayoutStrategy::Optimal, argValTypes, argTypeInfos,
                    /*typeToFill*/ nullptr,
                    std::move(bindings));

  auto descriptor = IGF.IGM.getAddrOfCaptureDescriptor(SILFn, origType,
                                                       substType, subs,
                                                       layout);

  llvm::Value *data;
  if (args.empty() && layout.isKnownEmpty()) {
    data = IGF.IGM.RefCountedNull;
  } else {
    // Allocate a new object.
    HeapNonFixedOffsets offsets(IGF, layout);

    data = IGF.emitUnmanagedAlloc(layout, "closure", descriptor, &offsets);
    Address dataAddr = layout.emitCastTo(IGF, data);
    
    unsigned i = 0;
    
    // Store necessary bindings, if we have them.
    if (layout.hasBindings()) {
      auto &bindingsLayout = layout.getElement(i);
      Address bindingsAddr = bindingsLayout.project(IGF, dataAddr, offsets);
      layout.getBindings().save(IGF, bindingsAddr);
      ++i;
    }
    
    // Store the context arguments.
    for (unsigned end = layout.getElements().size(); i < end; ++i) {
      auto &fieldLayout = layout.getElement(i);
      auto &fieldTy = layout.getElementTypes()[i];
      Address fieldAddr = fieldLayout.project(IGF, dataAddr, offsets);

      // We don't add non-constant function pointers to the explosion above,
      // so we need to handle them specially now.
      if (i == nonStaticFnIndex) {
        llvm::Value *fnPtr = fn.getPointer();
        fnPtr = IGF.Builder.CreateBitCast(fnPtr, IGF.IGM.Int8PtrTy);
        IGF.Builder.CreateStore(fnPtr, fieldAddr);
        continue;
      }

      switch (argConventions[i]) {
      // Take indirect value arguments out of memory.
      case ParameterConvention::Indirect_In:
      case ParameterConvention::Indirect_In_Constant:
      case ParameterConvention::Indirect_In_Guaranteed: {
        auto addr = fieldLayout.getType().getAddressForPointer(args.claimNext());
        fieldLayout.getType().initializeWithTake(IGF, fieldAddr, addr, fieldTy,
                                                 isOutlined);
        break;
      }
      // Take direct value arguments and inout pointers by value.
      case ParameterConvention::Direct_Unowned:
      case ParameterConvention::Direct_Owned:
      case ParameterConvention::Direct_Guaranteed:
      case ParameterConvention::Indirect_Inout:
      case ParameterConvention::Indirect_InoutAliasable:
        cast<LoadableTypeInfo>(fieldLayout.getType())
            .initialize(IGF, args, fieldAddr, isOutlined);
        break;
      }
    }
  }
  assert(args.empty() && "unused args in partial application?!");
  
  // Create the forwarding stub.
  auto origSig = IGF.IGM.getSignature(origType);

  llvm::Value *forwarder = emitPartialApplicationForwarder(IGF.IGM,
                                                              staticFn,
                                                          fnContext != nullptr,
                                                              origSig,
                                                              origType,
                                                              substType,
                                                              outType,
                                                              subs,
                                                              &layout,
                                                              argConventions);
  forwarder = IGF.Builder.CreateBitCast(forwarder, IGF.IGM.Int8PtrTy);
  out.add(forwarder);
  out.add(data);
}

/// Emit the block copy helper for a block.
static llvm::Function *emitBlockCopyHelper(IRGenModule &IGM,
                                           CanSILBlockStorageType blockTy,
                                           const BlockStorageTypeInfo &blockTL){
  // See if we've produced a block copy helper for this type before.
  // TODO
  
  // Create the helper.
  llvm::Type *args[] = {
    blockTL.getStorageType()->getPointerTo(),
    blockTL.getStorageType()->getPointerTo(),
  };
  auto copyTy = llvm::FunctionType::get(IGM.VoidTy, args, /*vararg*/ false);
  // TODO: Give these predictable mangled names and shared linkage.
  auto func = llvm::Function::Create(copyTy, llvm::GlobalValue::InternalLinkage,
                                     "block_copy_helper",
                                     IGM.getModule());
  func->setAttributes(IGM.constructInitialAttributes());
  IRGenFunction IGF(IGM, func);
  if (IGM.DebugInfo)
    IGM.DebugInfo->emitArtificialFunction(IGF, func);
  
  // Copy the captures from the source to the destination.
  Explosion params = IGF.collectParameters();
  auto dest = Address(params.claimNext(), blockTL.getFixedAlignment());
  auto src = Address(params.claimNext(), blockTL.getFixedAlignment());
  
  auto destCapture = blockTL.projectCapture(IGF, dest);
  auto srcCapture = blockTL.projectCapture(IGF, src);
  auto &captureTL = IGM.getTypeInfoForLowered(blockTy->getCaptureType());
  captureTL.initializeWithCopy(IGF, destCapture, srcCapture,
                               blockTy->getCaptureAddressType(), false);

  IGF.Builder.CreateRetVoid();
  
  return func;
}

/// Emit the block copy helper for a block.
static llvm::Function *emitBlockDisposeHelper(IRGenModule &IGM,
                                           CanSILBlockStorageType blockTy,
                                           const BlockStorageTypeInfo &blockTL){
  // See if we've produced a block destroy helper for this type before.
  // TODO
  
  // Create the helper.
  auto destroyTy = llvm::FunctionType::get(IGM.VoidTy,
                                       blockTL.getStorageType()->getPointerTo(),
                                       /*vararg*/ false);
  // TODO: Give these predictable mangled names and shared linkage.
  auto func = llvm::Function::Create(destroyTy,
                                     llvm::GlobalValue::InternalLinkage,
                                     "block_destroy_helper",
                                     IGM.getModule());
  func->setAttributes(IGM.constructInitialAttributes());
  IRGenFunction IGF(IGM, func);
  assert(!func->hasFnAttribute(llvm::Attribute::SanitizeThread));
  if (IGM.DebugInfo)
    IGM.DebugInfo->emitArtificialFunction(IGF, func);
  
  // Destroy the captures.
  Explosion params = IGF.collectParameters();
  auto storage = Address(params.claimNext(), blockTL.getFixedAlignment());
  auto capture = blockTL.projectCapture(IGF, storage);
  auto &captureTL = IGM.getTypeInfoForLowered(blockTy->getCaptureType());
  captureTL.destroy(IGF, capture, blockTy->getCaptureAddressType(),
                    false /*block storage code path: never outlined*/);
  IGF.Builder.CreateRetVoid();
  
  return func;
}

/// Emit the block header into a block storage slot.
void irgen::emitBlockHeader(IRGenFunction &IGF,
                            Address storage,
                            CanSILBlockStorageType blockTy,
                            llvm::Constant *invokeFunction,
                            CanSILFunctionType invokeTy,
                            ForeignFunctionInfo foreignInfo) {
  auto &storageTL
    = IGF.getTypeInfoForLowered(blockTy).as<BlockStorageTypeInfo>();

  Address headerAddr = storageTL.projectBlockHeader(IGF, storage);
  
  //
  // Initialize the "isa" pointer, which is _NSConcreteStackBlock.
  auto NSConcreteStackBlock =
      IGF.IGM.getModule()->getOrInsertGlobal("_NSConcreteStackBlock",
                                             IGF.IGM.ObjCClassStructTy);
  if (IGF.IGM.useDllStorage())
    cast<llvm::GlobalVariable>(NSConcreteStackBlock)
        ->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);

  //
  // Set the flags.
  // - HAS_COPY_DISPOSE unless the capture type is POD
  uint32_t flags = 0;
  auto &captureTL
    = IGF.getTypeInfoForLowered(blockTy->getCaptureType());
  bool isPOD = captureTL.isPOD(ResilienceExpansion::Maximal);
  if (!isPOD)
    flags |= 1 << 25;
  
  // - HAS_STRET, if the invoke function is sret
  assert(foreignInfo.ClangInfo);
  if (foreignInfo.ClangInfo->getReturnInfo().isIndirect())
    flags |= 1 << 29;
  
  // - HAS_SIGNATURE
  flags |= 1 << 30;
  
  auto flagsVal = llvm::ConstantInt::get(IGF.IGM.Int32Ty, flags);
  
  // Collect the reserved and invoke pointer fields.
  auto reserved = llvm::ConstantInt::get(IGF.IGM.Int32Ty, 0);
  auto invokeVal = llvm::ConstantExpr::getBitCast(invokeFunction,
                                                  IGF.IGM.FunctionPtrTy);
  
  // Build the block descriptor.
  ConstantInitBuilder builder(IGF.IGM);
  auto descriptorFields = builder.beginStruct();
  descriptorFields.addInt(IGF.IGM.IntPtrTy, 0);
  descriptorFields.addInt(IGF.IGM.IntPtrTy,
                          storageTL.getFixedSize().getValue());
  
  if (!isPOD) {
    // Define the copy and dispose helpers.
    descriptorFields.add(emitBlockCopyHelper(IGF.IGM, blockTy, storageTL));
    descriptorFields.add(emitBlockDisposeHelper(IGF.IGM, blockTy, storageTL));
  }
  
  // Build the descriptor signature.
  descriptorFields.add(getBlockTypeExtendedEncoding(IGF.IGM, invokeTy));
  
  // Create the descriptor.
  auto descriptor =
    descriptorFields.finishAndCreateGlobal("block_descriptor",
                                           IGF.IGM.getPointerAlignment(),
                                           /*constant*/ true);

  auto descriptorVal = llvm::ConstantExpr::getBitCast(descriptor,
                                                      IGF.IGM.Int8PtrTy);
  
  // Store the block header.
  auto layout = IGF.IGM.DataLayout.getStructLayout(IGF.IGM.ObjCBlockStructTy);
  IGF.Builder.CreateStore(NSConcreteStackBlock,
                          IGF.Builder.CreateStructGEP(headerAddr, 0, layout));
  IGF.Builder.CreateStore(flagsVal,
                          IGF.Builder.CreateStructGEP(headerAddr, 1, layout));
  IGF.Builder.CreateStore(reserved,
                          IGF.Builder.CreateStructGEP(headerAddr, 2, layout));
  IGF.Builder.CreateStore(invokeVal,
                          IGF.Builder.CreateStructGEP(headerAddr, 3, layout));
  IGF.Builder.CreateStore(descriptorVal,
                          IGF.Builder.CreateStructGEP(headerAddr, 4, layout));
}
