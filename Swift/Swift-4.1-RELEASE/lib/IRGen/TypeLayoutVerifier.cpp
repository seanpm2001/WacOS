//===--- TypeLayoutVerifier.cpp -------------------------------------------===//
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
// This file defines a generator that produces code to verify that IRGen's
// static assumptions about data layout for a Swift type correspond to the
// runtime's understanding of data layout.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Types.h"
#include "EnumPayload.h"
#include "IRGenDebugInfo.h"
#include "IRGenFunction.h"
#include "IRGenModule.h"
#include "GenOpaque.h"
#include "GenType.h"
#include "FixedTypeInfo.h"

using namespace swift;
using namespace irgen;

IRGenTypeVerifierFunction::IRGenTypeVerifierFunction(IRGenModule &IGM,
                                                     llvm::Function *f)
: IRGenFunction(IGM, f), VerifierFn(IGM.getVerifyTypeLayoutAttributeFn()) {
  // Verifier functions are always artificial.
  if (IGM.DebugInfo)
    IGM.DebugInfo->emitArtificialFunction(*this, f);
}

void
IRGenTypeVerifierFunction::emit(ArrayRef<CanType> formalTypes) {
  auto getSizeConstant = [&](Size sz) -> llvm::Constant * {
    return llvm::ConstantInt::get(IGM.SizeTy, sz.getValue());
  };
  auto getAlignmentMaskConstant = [&](Alignment a) -> llvm::Constant * {
    return llvm::ConstantInt::get(IGM.SizeTy, a.getValue() - 1);
  };
  auto getBoolConstant = [&](bool b) -> llvm::Constant * {
    return llvm::ConstantInt::get(IGM.Int1Ty, b);
  };

  SmallString<20> numberBuf;

  for (auto formalType : formalTypes) {
    // Runtime type metadata always represents the maximal abstraction level of
    // the type.
    auto maxAbstraction = AbstractionPattern::getOpaque();
    auto layoutType = IGM.getLoweredType(maxAbstraction, formalType);
    auto &ti = getTypeInfo(layoutType);
    auto metadata = emitTypeMetadataRef(formalType);

    // Check type metrics for fixed-layout types.
    // If there's no fixed type info, we rely on the runtime for type metrics,
    // so there's no compile-time values to validate against.
    if (auto *fixedTI = dyn_cast<FixedTypeInfo>(&ti)) {
      // Check that the fixed layout matches the runtime layout.
      verifyValues(metadata,
             emitLoadOfSize(*this, layoutType),
             getSizeConstant(fixedTI->getFixedSize()),
             "size");
      verifyValues(metadata,
             emitLoadOfAlignmentMask(*this, layoutType),
             getAlignmentMaskConstant(fixedTI->getFixedAlignment()),
             "alignment mask");
      verifyValues(metadata,
             emitLoadOfStride(*this, layoutType),
             getSizeConstant(fixedTI->getFixedStride()),
             "stride");
      verifyValues(metadata,
             emitLoadOfIsInline(*this, layoutType),
             getBoolConstant(fixedTI->getFixedPacking(IGM)
                               == FixedPacking::OffsetZero),
             "is-inline bit");
      verifyValues(metadata,
             emitLoadOfIsPOD(*this, layoutType),
             getBoolConstant(fixedTI->isPOD(ResilienceExpansion::Maximal)),
             "is-POD bit");
      verifyValues(metadata,
             emitLoadOfIsBitwiseTakable(*this, layoutType),
             getBoolConstant(fixedTI->isBitwiseTakable(ResilienceExpansion::Maximal)),
             "is-bitwise-takable bit");
      unsigned xiCount = fixedTI->getFixedExtraInhabitantCount(IGM);
      verifyValues(metadata,
             emitLoadOfHasExtraInhabitants(*this, layoutType),
             getBoolConstant(xiCount != 0),
             "has-extra-inhabitants bit");

      // Check extra inhabitants.
      if (xiCount > 0) {
        verifyValues(metadata,
               emitLoadOfExtraInhabitantCount(*this, layoutType),
               getSizeConstant(Size(xiCount)),
               "extra inhabitant count");
        
        // Verify that the extra inhabitant representations are consistent.
        
        // TODO: Update for EnumPayload implementation changes.
        auto xiBuf = createAlloca(fixedTI->getStorageType(),
                                      fixedTI->getFixedAlignment(),
                                      "extra-inhabitant");
        auto fixedXIBuf = createAlloca(fixedTI->getStorageType(),
                                           fixedTI->getFixedAlignment(),
                                           "extra-inhabitant");
        auto xiOpaque = Builder.CreateBitCast(xiBuf, IGM.OpaquePtrTy);
        auto fixedXIOpaque = Builder.CreateBitCast(fixedXIBuf,
                                                       IGM.OpaquePtrTy);
        auto xiMask = fixedTI->getFixedExtraInhabitantMask(IGM);
        auto xiSchema = EnumPayloadSchema::withBitSize(xiMask.getBitWidth());

        // TODO: Randomize the set of extra inhabitants we check.
        unsigned bits = fixedTI->getFixedSize().getValueInBits();
        for (unsigned i = 0, e = std::min(xiCount, 256u);
             i < e; ++i) {
          // Initialize the buffer with junk, to help ensure we're insensitive to
          // insignificant bits.
          // TODO: Randomize the filler.
          Builder.CreateMemSet(xiBuf.getAddress(),
                                   llvm::ConstantInt::get(IGM.Int8Ty, 0x5A),
                                   fixedTI->getFixedSize().getValue(),
                                   fixedTI->getFixedAlignment().getValue());
          
          // Ask the runtime to store an extra inhabitant.
          auto index = llvm::ConstantInt::get(IGM.Int32Ty, i);
          emitStoreExtraInhabitantCall(*this, layoutType, index, xiOpaque);
          
          // Compare the stored extra inhabitant against the fixed extra
          // inhabitant pattern.
          auto fixedXIValue
             = fixedTI->getFixedExtraInhabitantValue(IGM, bits, i);
          auto fixedXIPayload =
            EnumPayload::fromBitPattern(IGM, fixedXIValue,
                                        xiSchema);
          fixedXIPayload.store(*this, fixedXIBuf);
          
          auto runtimeXIPayload = EnumPayload::load(*this, xiBuf, xiSchema);
          runtimeXIPayload.emitApplyAndMask(*this, xiMask);
          runtimeXIPayload.store(*this, xiBuf);

          numberBuf.clear();
          {
            llvm::raw_svector_ostream os(numberBuf);
            os << i;
          }
          
          verifyBuffers(metadata, xiBuf, fixedXIBuf, fixedTI->getFixedSize(),
                 llvm::Twine("stored extra inhabitant ") + numberBuf.str());
          
          // Now ask the runtime to identify the fixed extra inhabitant value.
          // Mask in junk to make sure the runtime correctly ignores it.
          // TODO: Randomize the filler.
          auto xiFill = ~APInt(fixedXIValue.getBitWidth(), 0);
          xiFill &= ~xiMask;
          fixedXIValue |= xiFill;

          auto maskedXIPayload = EnumPayload::fromBitPattern(IGM,
            fixedXIValue, xiSchema);
          maskedXIPayload.store(*this, fixedXIBuf);
          
          auto runtimeIndex = emitGetExtraInhabitantIndexCall(*this, layoutType,
                                                              fixedXIOpaque);
          verifyValues(metadata,
                       runtimeIndex, index,
                       llvm::Twine("extra inhabitant index calculation ")
                         + numberBuf.str());
        }
      }
    }
    ti.verify(*this, metadata, layoutType);
  }

  Builder.CreateRetVoid();
}

void
IRGenTypeVerifierFunction::verifyValues(llvm::Value *typeMetadata,
                                        llvm::Value *runtimeVal,
                                        llvm::Value *staticVal,
                                        const llvm::Twine &description) {
  assert(runtimeVal->getType() == staticVal->getType());
  // Get or create buffers for the arguments.
  VerifierArgumentBuffers bufs;
  auto foundBufs = VerifierArgBufs.find(runtimeVal->getType());
  if (foundBufs != VerifierArgBufs.end()) {
    bufs = foundBufs->second;
  } else {
    Address runtimeBuf = createAlloca(runtimeVal->getType(),
                                          IGM.getPointerAlignment(),
                                          "runtime");
    Address staticBuf = createAlloca(staticVal->getType(),
                                         IGM.getPointerAlignment(),
                                         "static");
    bufs = {runtimeBuf, staticBuf};
    VerifierArgBufs[runtimeVal->getType()] = bufs;
  }
  
  Builder.CreateStore(runtimeVal, bufs.runtimeBuf);
  Builder.CreateStore(staticVal, bufs.staticBuf);
  
  auto runtimePtr = Builder.CreateBitCast(bufs.runtimeBuf.getAddress(),
                                              IGM.Int8PtrTy);
  auto staticPtr = Builder.CreateBitCast(bufs.staticBuf.getAddress(),
                                             IGM.Int8PtrTy);
  auto count = llvm::ConstantInt::get(IGM.SizeTy,
                IGM.DataLayout.getTypeStoreSize(runtimeVal->getType()));
  auto msg
    = IGM.getAddrOfGlobalString(description.str());
  
  Builder.CreateCall(
      VerifierFn, {typeMetadata, runtimePtr, staticPtr, count, msg});
}

void
IRGenTypeVerifierFunction::verifyBuffers(llvm::Value *typeMetadata,
                                         Address runtimeBuf,
                                         Address staticBuf,
                                         Size size,
                                         const llvm::Twine &description) {
  auto runtimePtr = Builder.CreateBitCast(runtimeBuf.getAddress(),
                                          IGM.Int8PtrTy);
  auto staticPtr = Builder.CreateBitCast(staticBuf.getAddress(),
                                         IGM.Int8PtrTy);
  auto count = llvm::ConstantInt::get(IGM.SizeTy,
                                      size.getValue());
  auto msg
    = IGM.getAddrOfGlobalString(description.str());

  Builder.CreateCall(
      VerifierFn, {typeMetadata, runtimePtr, staticPtr, count, msg});
};
