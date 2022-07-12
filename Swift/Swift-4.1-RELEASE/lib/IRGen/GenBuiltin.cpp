//===--- GenBuiltin.cpp - IR Generation for calls to builtin functions ----===//
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
//  This file implements IR generation for the assorted operations that
//  are performed by builtin functions.
//
//===----------------------------------------------------------------------===//

#include "GenBuiltin.h"

#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/StringSwitch.h"
#include "swift/AST/Builtins.h"
#include "swift/AST/Types.h"
#include "swift/SIL/SILModule.h"
#include "clang/AST/ASTContext.h"

#include "Explosion.h"
#include "GenCall.h"
#include "GenCast.h"
#include "IRGenFunction.h"
#include "IRGenModule.h"
#include "LoadableTypeInfo.h"

using namespace swift;
using namespace irgen;

static void emitCastBuiltin(IRGenFunction &IGF, SILType destType,
                            Explosion &result,
                            Explosion &args,
                            llvm::Instruction::CastOps opcode) {
  llvm::Value *input = args.claimNext();
  assert(args.empty() && "wrong operands to cast operation");

  llvm::Type *destTy = IGF.IGM.getStorageType(destType);
  llvm::Value *output = IGF.Builder.CreateCast(opcode, input, destTy);
  result.add(output);
}

static void emitCastOrBitCastBuiltin(IRGenFunction &IGF,
                                     SILType destType,
                                     Explosion &result,
                                     Explosion &args,
                                     BuiltinValueKind BV) {
  llvm::Value *input = args.claimNext();
  assert(args.empty() && "wrong operands to cast operation");

  llvm::Type *destTy = IGF.IGM.getStorageType(destType);
  llvm::Value *output;
  switch (BV) {
  default: llvm_unreachable("Not a cast-or-bitcast operation");
  case BuiltinValueKind::TruncOrBitCast:
    output = IGF.Builder.CreateTruncOrBitCast(input, destTy); break;
  case BuiltinValueKind::ZExtOrBitCast:
    output = IGF.Builder.CreateZExtOrBitCast(input, destTy); break;
  case BuiltinValueKind::SExtOrBitCast:
    output = IGF.Builder.CreateSExtOrBitCast(input, destTy); break;
  }
  result.add(output);
}

static void emitCompareBuiltin(IRGenFunction &IGF, Explosion &result,
                               Explosion &args, llvm::CmpInst::Predicate pred) {
  llvm::Value *lhs = args.claimNext();
  llvm::Value *rhs = args.claimNext();
  
  llvm::Value *v;
  if (lhs->getType()->isFPOrFPVectorTy())
    v = IGF.Builder.CreateFCmp(pred, lhs, rhs);
  else
    v = IGF.Builder.CreateICmp(pred, lhs, rhs);
  
  result.add(v);
}

static void emitTypeTraitBuiltin(IRGenFunction &IGF,
                                 Explosion &out,
                                 Explosion &args,
                                 SubstitutionList substitutions,
                                 TypeTraitResult (TypeBase::*trait)()) {
  assert(substitutions.size() == 1
         && "type trait should have gotten single type parameter");
  args.claimNext();
  
  // Lower away the trait to a tristate 0 = no, 1 = yes, 2 = maybe.
  unsigned result;
  switch ((substitutions[0].getReplacement().getPointer()->*trait)()) {
  case TypeTraitResult::IsNot:
    result = 0;
    break;
  case TypeTraitResult::Is:
    result = 1;
    break;
  case TypeTraitResult::CanBe:
    result = 2;
    break;
  }

  out.add(llvm::ConstantInt::get(IGF.IGM.Int8Ty, result));
}

static std::pair<SILType, const TypeInfo &>
getLoweredTypeAndTypeInfo(IRGenModule &IGM, Type unloweredType) {
  auto lowered = IGM.getLoweredType(unloweredType);
  return {lowered, IGM.getTypeInfo(lowered)};
}

/// emitBuiltinCall - Emit a call to a builtin function.
void irgen::emitBuiltinCall(IRGenFunction &IGF, const BuiltinInfo &Builtin,
                            Identifier FnId, SILType resultType,
                            Explosion &args, Explosion &out,
                            SubstitutionList substitutions) {
  if (Builtin.ID == BuiltinValueKind::UnsafeGuaranteedEnd) {
    // Just consume the incoming argument.
    assert(args.size() == 1 && "Expecting one incoming argument");
    (void)args.claimAll();
    return;
  }

  if (Builtin.ID == BuiltinValueKind::UnsafeGuaranteed) {
    // Just forward the incoming argument.
    assert(args.size() == 1 && "Expecting one incoming argument");
    out = std::move(args);
    // This is a token.
    out.add(llvm::ConstantInt::get(IGF.IGM.Int8Ty, 0));
    return;
  }

  if (Builtin.ID == BuiltinValueKind::OnFastPath) {
    // The onFastPath builtin has only an effect on SIL level, so we lower it
    // to a no-op.
    return;
  }

  // These builtins don't care about their argument:
  if (Builtin.ID == BuiltinValueKind::Sizeof) {
    (void)args.claimAll();
    auto valueTy = getLoweredTypeAndTypeInfo(IGF.IGM,
                                             substitutions[0].getReplacement());
    out.add(valueTy.second.getSize(IGF, valueTy.first));
    return;
  }

  if (Builtin.ID == BuiltinValueKind::Strideof) {
    (void)args.claimAll();
    auto valueTy = getLoweredTypeAndTypeInfo(IGF.IGM,
                                             substitutions[0].getReplacement());
    out.add(valueTy.second.getStride(IGF, valueTy.first));
    return;
  }

  if (Builtin.ID == BuiltinValueKind::Alignof) {
    (void)args.claimAll();
    auto valueTy = getLoweredTypeAndTypeInfo(IGF.IGM,
                                             substitutions[0].getReplacement());
    // The alignof value is one greater than the alignment mask.
    out.add(IGF.Builder.CreateAdd(
                           valueTy.second.getAlignmentMask(IGF, valueTy.first),
                           IGF.IGM.getSize(Size(1))));
    return;
  }

  if (Builtin.ID == BuiltinValueKind::IsPOD) {
    (void)args.claimAll();
    auto valueTy = getLoweredTypeAndTypeInfo(IGF.IGM,
                                             substitutions[0].getReplacement());
    out.add(valueTy.second.getIsPOD(IGF, valueTy.first));
    return;
  }


  // addressof expects an lvalue argument.
  if (Builtin.ID == BuiltinValueKind::AddressOf) {
    llvm::Value *address = args.claimNext();
    llvm::Value *value = IGF.Builder.CreateBitCast(address,
                                                   IGF.IGM.Int8PtrTy);
    out.add(value);
    return;
  }

  // Everything else cares about the (rvalue) argument.

  // If this is an LLVM IR intrinsic, lower it to an intrinsic call.
  const IntrinsicInfo &IInfo = IGF.getSILModule().getIntrinsicInfo(FnId);
  llvm::Intrinsic::ID IID = IInfo.ID;

  // Calls to the int_instrprof_increment intrinsic are emitted during SILGen.
  // At that stage, the function name GV used by the profiling pass is hidden.
  // Fix the intrinsic call here by pointing it to the correct GV.
  if (IID == llvm::Intrinsic::instrprof_increment) {
    // If we import profiling intrinsics from a swift module but profiling is
    // not enabled, ignore the increment.
    if (!IGF.getSILModule().getOptions().GenerateProfile)
      return;

    // Extract the PGO function name.
    auto *NameGEP = cast<llvm::User>(args.claimNext());
    auto *NameGV = dyn_cast<llvm::GlobalVariable>(NameGEP->stripPointerCasts());
    if (NameGV) {
      auto *NameC = NameGV->getInitializer();
      StringRef Name = cast<llvm::ConstantDataArray>(NameC)->getRawDataValues();
      StringRef PGOFuncName = Name.rtrim(StringRef("\0", 1));

      // Point the increment call to the right function name variable.
      std::string PGOFuncNameVar = llvm::getPGOFuncNameVarName(
          PGOFuncName, llvm::GlobalValue::LinkOnceAnyLinkage);
      auto *FuncNamePtr = IGF.IGM.Module.getNamedGlobal(PGOFuncNameVar);
      if (!FuncNamePtr)
        FuncNamePtr = llvm::createPGOFuncNameVar(
            *IGF.IGM.getModule(), llvm::GlobalValue::LinkOnceAnyLinkage,
            PGOFuncName);

      llvm::SmallVector<llvm::Value *, 2> Indices(2, NameGEP->getOperand(1));
      NameGEP = llvm::ConstantExpr::getGetElementPtr(
          ((llvm::PointerType *)FuncNamePtr->getType())->getElementType(),
          FuncNamePtr, makeArrayRef(Indices));
    }

    // Replace the placeholder value with the new GEP.
    Explosion replacement;
    replacement.add(NameGEP);
    replacement.add(args.claimAll());
    args = std::move(replacement);
  }

  if (IID != llvm::Intrinsic::not_intrinsic) {
    SmallVector<llvm::Type*, 4> ArgTys;
    for (auto T : IInfo.Types)
      ArgTys.push_back(IGF.IGM.getStorageTypeForLowered(T->getCanonicalType()));
      
    auto F = llvm::Intrinsic::getDeclaration(&IGF.IGM.Module,
                                             (llvm::Intrinsic::ID)IID, ArgTys);
    llvm::FunctionType *FT = F->getFunctionType();
    SmallVector<llvm::Value*, 8> IRArgs;
    for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i)
      IRArgs.push_back(args.claimNext());
    llvm::Value *TheCall = IGF.Builder.CreateCall(F, IRArgs);

    if (!TheCall->getType()->isVoidTy())
      extractScalarResults(IGF, TheCall->getType(), TheCall, out);

    return;
  }

  // TODO: A linear series of ifs is suboptimal.
#define BUILTIN_SIL_OPERATION(id, name, overload) \
  if (Builtin.ID == BuiltinValueKind::id) \
    llvm_unreachable(name " builtin should be lowered away by SILGen!");

#define BUILTIN_CAST_OPERATION(id, name, attrs) \
  if (Builtin.ID == BuiltinValueKind::id) \
    return emitCastBuiltin(IGF, resultType, out, args, \
                           llvm::Instruction::id);

#define BUILTIN_CAST_OR_BITCAST_OPERATION(id, name, attrs) \
  if (Builtin.ID == BuiltinValueKind::id) \
    return emitCastOrBitCastBuiltin(IGF, resultType, out, args, \
                                    BuiltinValueKind::id);
  
#define BUILTIN_BINARY_OPERATION(id, name, attrs, overload) \
  if (Builtin.ID == BuiltinValueKind::id) { \
    llvm::Value *lhs = args.claimNext(); \
    llvm::Value *rhs = args.claimNext(); \
    llvm::Value *v = IGF.Builder.Create##id(lhs, rhs); \
    return out.add(v); \
  }

#define BUILTIN_RUNTIME_CALL(id, name, attrs)                                  \
  if (Builtin.ID == BuiltinValueKind::id) {                                    \
    auto *fn = cast<llvm::Function>(IGF.IGM.get##id##Fn());                    \
    llvm::CallInst *call = IGF.Builder.CreateCall(fn, args.claimNext());       \
    call->setCallingConv(fn->getCallingConv());                                \
    call->setAttributes(fn->getAttributes());                                  \
    return out.add(call);                                                      \
  }

#define BUILTIN_BINARY_OPERATION_WITH_OVERFLOW(id, name, uncheckedID, attrs, overload) \
if (Builtin.ID == BuiltinValueKind::id) { \
  SmallVector<llvm::Type*, 2> ArgTys; \
  auto opType = Builtin.Types[0]->getCanonicalType(); \
  ArgTys.push_back(IGF.IGM.getStorageTypeForLowered(opType)); \
  auto F = llvm::Intrinsic::getDeclaration(&IGF.IGM.Module, \
    getLLVMIntrinsicIDForBuiltinWithOverflow(Builtin.ID), ArgTys); \
  SmallVector<llvm::Value*, 2> IRArgs; \
  IRArgs.push_back(args.claimNext()); \
  IRArgs.push_back(args.claimNext()); \
  args.claimNext();\
  llvm::Value *TheCall = IGF.Builder.CreateCall(F, IRArgs); \
  extractScalarResults(IGF, TheCall->getType(), TheCall, out);  \
  return; \
}
  // FIXME: We could generate the code to dynamically report the overflow if the
  // third argument is true. Now, we just ignore it.

#define BUILTIN_BINARY_PREDICATE(id, name, attrs, overload) \
  if (Builtin.ID == BuiltinValueKind::id) \
    return emitCompareBuiltin(IGF, out, args, llvm::CmpInst::id);
  
#define BUILTIN_TYPE_TRAIT_OPERATION(id, name) \
  if (Builtin.ID == BuiltinValueKind::id) \
    return emitTypeTraitBuiltin(IGF, out, args, substitutions, &TypeBase::name);
  
#define BUILTIN(ID, Name, Attrs)  // Ignore the rest.
#include "swift/AST/Builtins.def"

  if (Builtin.ID == BuiltinValueKind::FNeg) {
    llvm::Value *rhs = args.claimNext();
    llvm::Value *lhs = llvm::ConstantFP::get(rhs->getType(), "-0.0");
    llvm::Value *v = IGF.Builder.CreateFSub(lhs, rhs);
    return out.add(v);
  }
  
  if (Builtin.ID == BuiltinValueKind::AssumeNonNegative) {
    llvm::Value *v = args.claimNext();
    // Set a value range on the load instruction, which must be the argument of
    // the builtin.
    if (isa<llvm::LoadInst>(v) || isa<llvm::CallInst>(v)) {
      // The load must be post-dominated by the builtin. Otherwise we would get
      // a wrong assumption in the else-branch in this example:
      //    x = f()
      //    if condition {
      //      y = assumeNonNegative(x)
      //    } else {
      //      // x might be negative here!
      //    }
      // For simplicity we just enforce that both the load and the builtin must
      // be in the same block.
      llvm::Instruction *I = static_cast<llvm::Instruction *>(v);
      if (I->getParent() == IGF.Builder.GetInsertBlock()) {
        llvm::LLVMContext &ctx = IGF.IGM.Module.getContext();
        auto *intType = dyn_cast<llvm::IntegerType>(v->getType());
        llvm::Metadata *rangeElems[] = {
          llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(intType, 0)),
          llvm::ConstantAsMetadata::get(
              llvm::ConstantInt::get(intType,
                  APInt::getSignedMaxValue(intType->getBitWidth())))
        };
        llvm::MDNode *range = llvm::MDNode::get(ctx, rangeElems);
        I->setMetadata(llvm::LLVMContext::MD_range, range);
      }
    }
    // Don't generate any code for the builtin.
    return out.add(v);
  }
  
  if (Builtin.ID == BuiltinValueKind::AllocRaw) {
    auto size = args.claimNext();
    auto align = args.claimNext();
    // Translate the alignment to a mask.
    auto alignMask = IGF.Builder.CreateSub(align, IGF.IGM.getSize(Size(1)));
    auto alloc = IGF.emitAllocRawCall(size, alignMask, "builtin-allocRaw");
    out.add(alloc);
    return;
  }

  if (Builtin.ID == BuiltinValueKind::DeallocRaw) {
    auto pointer = args.claimNext();
    auto size = args.claimNext();
    auto align = args.claimNext();
    // Translate the alignment to a mask.
    auto alignMask = IGF.Builder.CreateSub(align, IGF.IGM.getSize(Size(1)));
    IGF.emitDeallocRawCall(pointer, size, alignMask);
    return;
  }

  if (Builtin.ID == BuiltinValueKind::Fence) {
    SmallVector<Type, 4> Types;
    StringRef BuiltinName =
      getBuiltinBaseName(IGF.IGM.Context, FnId.str(), Types);
    BuiltinName = BuiltinName.drop_front(strlen("fence_"));
    // Decode the ordering argument, which is required.
    auto underscore = BuiltinName.find('_');
    auto ordering = decodeLLVMAtomicOrdering(BuiltinName.substr(0, underscore));
    assert(ordering != llvm::AtomicOrdering::NotAtomic);
    BuiltinName = BuiltinName.substr(underscore);
    
    // Accept singlethread if present.
    bool isSingleThread = BuiltinName.startswith("_singlethread");
    if (isSingleThread)
      BuiltinName = BuiltinName.drop_front(strlen("_singlethread"));
    assert(BuiltinName.empty() && "Mismatch with sema");
    
    IGF.Builder.CreateFence(ordering, isSingleThread
                                          ? llvm::SyncScope::SingleThread
                                          : llvm::SyncScope::System);
    return;
  }

  
  if (Builtin.ID == BuiltinValueKind::CmpXChg) {
    SmallVector<Type, 4> Types;
    StringRef BuiltinName =
      getBuiltinBaseName(IGF.IGM.Context, FnId.str(), Types);
    BuiltinName = BuiltinName.drop_front(strlen("cmpxchg_"));

    // Decode the success- and failure-ordering arguments, which are required.
    SmallVector<StringRef, 4> Parts;
    BuiltinName.split(Parts, "_");
    assert(Parts.size() >= 2 && "Mismatch with sema");
    auto successOrdering = decodeLLVMAtomicOrdering(Parts[0]);
    auto failureOrdering = decodeLLVMAtomicOrdering(Parts[1]);
    assert(successOrdering != llvm::AtomicOrdering::NotAtomic);
    assert(failureOrdering != llvm::AtomicOrdering::NotAtomic);
    auto NextPart = Parts.begin() + 2;

    // Accept weak, volatile, and singlethread if present.
    bool isWeak = false, isVolatile = false, isSingleThread = false;
    if (NextPart != Parts.end() && *NextPart == "weak") {
      isWeak = true;
      NextPart++;
    }
    if (NextPart != Parts.end() && *NextPart == "volatile") {
      isVolatile = true;
      NextPart++;
    }
    if (NextPart != Parts.end() && *NextPart == "singlethread") {
      isSingleThread = true;
      NextPart++;
    }
    assert(NextPart == Parts.end() && "Mismatch with sema");

    auto pointer = args.claimNext();
    auto cmp = args.claimNext();
    auto newval = args.claimNext();

    llvm::Type *origTy = cmp->getType();
    if (origTy->isPointerTy()) {
      cmp = IGF.Builder.CreatePtrToInt(cmp, IGF.IGM.IntPtrTy);
      newval = IGF.Builder.CreatePtrToInt(newval, IGF.IGM.IntPtrTy);
    }

    pointer = IGF.Builder.CreateBitCast(pointer,
                                  llvm::PointerType::getUnqual(cmp->getType()));
    llvm::Value *value = IGF.Builder.CreateAtomicCmpXchg(
        pointer, cmp, newval, successOrdering, failureOrdering,
        isSingleThread ? llvm::SyncScope::SingleThread
                       : llvm::SyncScope::System);
    cast<llvm::AtomicCmpXchgInst>(value)->setVolatile(isVolatile);
    cast<llvm::AtomicCmpXchgInst>(value)->setWeak(isWeak);

    auto valueLoaded = IGF.Builder.CreateExtractValue(value, {0});
    auto loadSuccessful = IGF.Builder.CreateExtractValue(value, {1});

    if (origTy->isPointerTy())
      valueLoaded = IGF.Builder.CreateIntToPtr(valueLoaded, origTy);

    out.add(valueLoaded);
    out.add(loadSuccessful);

    return;
  }
  
  if (Builtin.ID == BuiltinValueKind::AtomicRMW) {
    using namespace llvm;

    SmallVector<Type, 4> Types;
    StringRef BuiltinName = getBuiltinBaseName(IGF.IGM.Context,
                                               FnId.str(), Types);
    BuiltinName = BuiltinName.drop_front(strlen("atomicrmw_"));
    auto underscore = BuiltinName.find('_');
    StringRef SubOp = BuiltinName.substr(0, underscore);
    
    AtomicRMWInst::BinOp SubOpcode = StringSwitch<AtomicRMWInst::BinOp>(SubOp)
      .Case("xchg", AtomicRMWInst::Xchg)
      .Case("add",  AtomicRMWInst::Add)
      .Case("sub",  AtomicRMWInst::Sub)
      .Case("and",  AtomicRMWInst::And)
      .Case("nand", AtomicRMWInst::Nand)
      .Case("or",   AtomicRMWInst::Or)
      .Case("xor",  AtomicRMWInst::Xor)
      .Case("max",  AtomicRMWInst::Max)
      .Case("min",  AtomicRMWInst::Min)
      .Case("umax", AtomicRMWInst::UMax)
      .Case("umin", AtomicRMWInst::UMin);
    BuiltinName = BuiltinName.drop_front(underscore+1);
    
    // Decode the ordering argument, which is required.
    underscore = BuiltinName.find('_');
    auto ordering = decodeLLVMAtomicOrdering(BuiltinName.substr(0, underscore));
    assert(ordering != llvm::AtomicOrdering::NotAtomic);
    BuiltinName = BuiltinName.substr(underscore);
    
    // Accept volatile and singlethread if present.
    bool isVolatile = BuiltinName.startswith("_volatile");
    if (isVolatile) BuiltinName = BuiltinName.drop_front(strlen("_volatile"));
    
    bool isSingleThread = BuiltinName.startswith("_singlethread");
    if (isSingleThread)
      BuiltinName = BuiltinName.drop_front(strlen("_singlethread"));
    assert(BuiltinName.empty() && "Mismatch with sema");
    
    auto pointer = args.claimNext();
    auto val = args.claimNext();

    // Handle atomic ops on pointers by casting to intptr_t.
    llvm::Type *origTy = val->getType();
    if (origTy->isPointerTy())
      val = IGF.Builder.CreatePtrToInt(val, IGF.IGM.IntPtrTy);

    pointer = IGF.Builder.CreateBitCast(pointer,
                                  llvm::PointerType::getUnqual(val->getType()));
    llvm::Value *value = IGF.Builder.CreateAtomicRMW(
        SubOpcode, pointer, val, ordering,
        isSingleThread ? llvm::SyncScope::SingleThread
                       : llvm::SyncScope::System);
    cast<AtomicRMWInst>(value)->setVolatile(isVolatile);

    if (origTy->isPointerTy())
      value = IGF.Builder.CreateIntToPtr(value, origTy);

    out.add(value);
    return;
  }

  if (Builtin.ID == BuiltinValueKind::AtomicLoad
      || Builtin.ID == BuiltinValueKind::AtomicStore) {
    using namespace llvm;

    SmallVector<Type, 4> Types;
    StringRef BuiltinName = getBuiltinBaseName(IGF.IGM.Context,
                                               FnId.str(), Types);
    auto underscore = BuiltinName.find('_');
    BuiltinName = BuiltinName.substr(underscore+1);

    underscore = BuiltinName.find('_');
    auto ordering = decodeLLVMAtomicOrdering(BuiltinName.substr(0, underscore));
    assert(ordering != llvm::AtomicOrdering::NotAtomic);
    BuiltinName = BuiltinName.substr(underscore);

    // Accept volatile and singlethread if present.
    bool isVolatile = BuiltinName.startswith("_volatile");
    if (isVolatile) BuiltinName = BuiltinName.drop_front(strlen("_volatile"));

    bool isSingleThread = BuiltinName.startswith("_singlethread");
    if (isSingleThread)
      BuiltinName = BuiltinName.drop_front(strlen("_singlethread"));
    assert(BuiltinName.empty() && "Mismatch with sema");

    auto pointer = args.claimNext();
    auto &valueTI = IGF.getTypeInfoForUnlowered(Types[0]);
    auto schema = valueTI.getSchema();
    assert(schema.size() == 1 && "not a scalar type?!");
    auto origValueTy = schema[0].getScalarType();

    // If the type is floating-point, then we need to bitcast to integer.
    auto valueTy = origValueTy;
    if (valueTy->isFloatingPointTy()) {
      valueTy = llvm::IntegerType::get(IGF.IGM.LLVMContext,
                                       valueTy->getPrimitiveSizeInBits());
    }

    pointer = IGF.Builder.CreateBitCast(pointer, valueTy->getPointerTo());

    if (Builtin.ID == BuiltinValueKind::AtomicLoad) {
      auto load = IGF.Builder.CreateLoad(pointer,
                                         valueTI.getBestKnownAlignment());
      load->setAtomic(ordering, isSingleThread ? llvm::SyncScope::SingleThread
                                               : llvm::SyncScope::System);
      load->setVolatile(isVolatile);

      llvm::Value *value = load;
      if (valueTy != origValueTy)
        value = IGF.Builder.CreateBitCast(value, origValueTy);
      out.add(value);
      return;
    } else if (Builtin.ID == BuiltinValueKind::AtomicStore) {
      llvm::Value *value = args.claimNext();
      if (valueTy != origValueTy)
        value = IGF.Builder.CreateBitCast(value, valueTy);
      auto store = IGF.Builder.CreateStore(value, pointer,
                                           valueTI.getBestKnownAlignment());
      store->setAtomic(ordering, isSingleThread ? llvm::SyncScope::SingleThread
                                                : llvm::SyncScope::System);
      store->setVolatile(isVolatile);
      return;
    } else {
      llvm_unreachable("out of sync with outer conditional");
    }
  }

  if (Builtin.ID == BuiltinValueKind::ExtractElement) {
    using namespace llvm;

    auto vector = args.claimNext();
    auto index = args.claimNext();
    out.add(IGF.Builder.CreateExtractElement(vector, index));
    return;
  }

  if (Builtin.ID == BuiltinValueKind::InsertElement) {
    using namespace llvm;

    auto vector = args.claimNext();
    auto newValue = args.claimNext();
    auto index = args.claimNext();
    out.add(IGF.Builder.CreateInsertElement(vector, newValue, index));
    return;
  }

  if (Builtin.ID == BuiltinValueKind::SToSCheckedTrunc ||
      Builtin.ID == BuiltinValueKind::UToUCheckedTrunc ||
      Builtin.ID == BuiltinValueKind::SToUCheckedTrunc) {
    auto FromTy =
      IGF.IGM.getStorageTypeForLowered(Builtin.Types[0]->getCanonicalType());
    auto ToTy =
      IGF.IGM.getStorageTypeForLowered(Builtin.Types[1]->getCanonicalType());

    // Compute the result for SToSCheckedTrunc_IntFrom_IntTo(Arg):
    //   Res = trunc_IntTo(Arg)
    //   Ext = sext_IntFrom(Res)
    //   OverflowFlag = (Arg == Ext) ? 0 : 1
    //   return (resultVal, OverflowFlag)
    //
    // Compute the result for UToUCheckedTrunc_IntFrom_IntTo(Arg)
    // and SToUCheckedTrunc_IntFrom_IntTo(Arg):
    //   Res = trunc_IntTo(Arg)
    //   Ext = zext_IntFrom(Res)
    //   OverflowFlag = (Arg == Ext) ? 0 : 1
    //   return (Res, OverflowFlag)
    llvm::Value *Arg = args.claimNext();
    llvm::Value *Res = IGF.Builder.CreateTrunc(Arg, ToTy);
    bool Signed = (Builtin.ID == BuiltinValueKind::SToSCheckedTrunc);
    llvm::Value *Ext = Signed ? IGF.Builder.CreateSExt(Res, FromTy) :
                                IGF.Builder.CreateZExt(Res, FromTy);
    llvm::Value *OverflowCond = IGF.Builder.CreateICmpEQ(Arg, Ext);
    llvm::Value *OverflowFlag = IGF.Builder.CreateSelect(OverflowCond,
                                  llvm::ConstantInt::get(IGF.IGM.Int1Ty, 0),
                                  llvm::ConstantInt::get(IGF.IGM.Int1Ty, 1));
    // Return the tuple - the result + the overflow flag.
    out.add(Res);
    return out.add(OverflowFlag);
  }

  if (Builtin.ID == BuiltinValueKind::UToSCheckedTrunc) {
    auto FromTy =
      IGF.IGM.getStorageTypeForLowered(Builtin.Types[0]->getCanonicalType());
    auto ToTy =
      IGF.IGM.getStorageTypeForLowered(Builtin.Types[1]->getCanonicalType());
    llvm::Type *ToMinusOneTy =
      llvm::Type::getIntNTy(ToTy->getContext(), ToTy->getIntegerBitWidth() - 1);

    // Compute the result for UToSCheckedTrunc_IntFrom_IntTo(Arg):
    //   Res = trunc_IntTo(Arg)
    //   Trunc = trunc_'IntTo-1bit'(Arg)
    //   Ext = zext_IntFrom(Trunc)
    //   OverflowFlag = (Arg == Ext) ? 0 : 1
    //   return (Res, OverflowFlag)
    llvm::Value *Arg = args.claimNext();
    llvm::Value *Res = IGF.Builder.CreateTrunc(Arg, ToTy);
    llvm::Value *Trunc = IGF.Builder.CreateTrunc(Arg, ToMinusOneTy);
    llvm::Value *Ext = IGF.Builder.CreateZExt(Trunc, FromTy);
    llvm::Value *OverflowCond = IGF.Builder.CreateICmpEQ(Arg, Ext);
    llvm::Value *OverflowFlag = IGF.Builder.CreateSelect(OverflowCond,
                                  llvm::ConstantInt::get(IGF.IGM.Int1Ty, 0),
                                  llvm::ConstantInt::get(IGF.IGM.Int1Ty, 1));
    // Return the tuple: (the result, the overflow flag).
    out.add(Res);
    return out.add(OverflowFlag);
  }

  if (Builtin.ID == BuiltinValueKind::SUCheckedConversion ||
      Builtin.ID == BuiltinValueKind::USCheckedConversion) {
    auto Ty =
      IGF.IGM.getStorageTypeForLowered(Builtin.Types[0]->getCanonicalType());

    // Report a sign error if the input parameter is a negative number, when
    // interpreted as signed.
    llvm::Value *Arg = args.claimNext();
    llvm::Value *Zero = llvm::ConstantInt::get(Ty, 0);
    llvm::Value *OverflowFlag = IGF.Builder.CreateICmpSLT(Arg, Zero);

    // Return the tuple: (the result (same as input), the overflow flag).
    out.add(Arg);
    return out.add(OverflowFlag);
  }

  // We are currently emitting code for '_convertFromBuiltinIntegerLiteral',
  // which will call the builtin and pass it a non-compile-time-const parameter.
  if (Builtin.ID == BuiltinValueKind::IntToFPWithOverflow) {
    auto ToTy =
      IGF.IGM.getStorageTypeForLowered(Builtin.Types[1]->getCanonicalType());
    llvm::Value *Arg = args.claimNext();
    unsigned bitSize = Arg->getType()->getScalarSizeInBits();
    if (bitSize > 64) {
      // TODO: the integer literal bit size is 2048, but we only have a 64-bit
      // conversion function available (on all platforms).
      Arg = IGF.Builder.CreateTrunc(Arg, IGF.IGM.Int64Ty);
    } else if (bitSize < 64) {
      // Just for completeness. IntToFPWithOverflow is currently only used to
      // convert 2048 bit integer literals.
      Arg = IGF.Builder.CreateSExt(Arg, IGF.IGM.Int64Ty);
    }
    llvm::Value *V = IGF.Builder.CreateSIToFP(Arg, ToTy);
    return out.add(V);
  }

  if (Builtin.ID == BuiltinValueKind::Once
      || Builtin.ID == BuiltinValueKind::OnceWithContext) {
    // The input type is statically (Builtin.RawPointer, @convention(thin) () -> ()).
    llvm::Value *PredPtr = args.claimNext();
    // Cast the predicate to a OnceTy pointer.
    PredPtr = IGF.Builder.CreateBitCast(PredPtr, IGF.IGM.OnceTy->getPointerTo());
    llvm::Value *FnCode = args.claimNext();
    // Get the context if any.
    llvm::Value *Context;
    if (Builtin.ID == BuiltinValueKind::OnceWithContext) {
      Context = args.claimNext();
    } else {
      Context = llvm::UndefValue::get(IGF.IGM.Int8PtrTy);
    }
    
    // If we know the platform runtime's "done" value, emit the check inline.
    llvm::BasicBlock *doneBB = nullptr;

    if (auto ExpectedPred = IGF.IGM.TargetInfo.OnceDonePredicateValue) {
      auto PredValue = IGF.Builder.CreateLoad(PredPtr,
                                              IGF.IGM.getPointerAlignment());
      auto ExpectedPredValue = llvm::ConstantInt::getSigned(IGF.IGM.OnceTy,
                                                            *ExpectedPred);
      auto PredIsDone = IGF.Builder.CreateICmpEQ(PredValue, ExpectedPredValue);
      
      auto notDoneBB = IGF.createBasicBlock("once_not_done");
      doneBB = IGF.createBasicBlock("once_done");
      
      IGF.Builder.CreateCondBr(PredIsDone, doneBB, notDoneBB);
      IGF.Builder.emitBlock(notDoneBB);
    }
    
    // Emit the runtime "once" call.
    auto call
      = IGF.Builder.CreateCall(IGF.IGM.getOnceFn(), {PredPtr, FnCode, Context});
    call->setCallingConv(IGF.IGM.DefaultCC);
    
    // If we emitted the "done" check inline, join the branches.
    if (auto ExpectedPred = IGF.IGM.TargetInfo.OnceDonePredicateValue) {
      IGF.Builder.CreateBr(doneBB);
      IGF.Builder.emitBlock(doneBB);
      // We can assume the once predicate is in the "done" state now.
      auto PredValue = IGF.Builder.CreateLoad(PredPtr,
                                              IGF.IGM.getPointerAlignment());
      auto ExpectedPredValue = llvm::ConstantInt::getSigned(IGF.IGM.OnceTy,
                                                            *ExpectedPred);
      auto PredIsDone = IGF.Builder.CreateICmpEQ(PredValue, ExpectedPredValue);

      IGF.Builder.CreateAssumption(PredIsDone);
    }
    
    // No return value.
    return;
  }

  if (Builtin.ID == BuiltinValueKind::AssertConf) {
    // Replace the call to assert_configuration by the Debug configuration
    // value.
    // TODO: assert(IGF.IGM.getOptions().AssertConfig ==
    //              SILOptions::DisableReplacement);
    // Make sure this only happens in a mode where we build a library dylib.

    llvm::Value *DebugAssert = IGF.Builder.getInt32(SILOptions::Debug);
    out.add(DebugAssert);
    return;
  }
  
  if (Builtin.ID == BuiltinValueKind::DestroyArray) {
    // The input type is (T.Type, Builtin.RawPointer, Builtin.Word).
    /* metatype (which may be thin) */
    if (args.size() == 3)
      args.claimNext();
    llvm::Value *ptr = args.claimNext();
    llvm::Value *count = args.claimNext();
    
    auto valueTy = getLoweredTypeAndTypeInfo(IGF.IGM,
                                             substitutions[0].getReplacement());
    
    ptr = IGF.Builder.CreateBitCast(ptr,
                              valueTy.second.getStorageType()->getPointerTo());
    Address array = valueTy.second.getAddressForPointer(ptr);
    valueTy.second.destroyArray(IGF, array, count, valueTy.first);
    return;
  }

  if (Builtin.ID == BuiltinValueKind::CopyArray ||
      Builtin.ID == BuiltinValueKind::TakeArrayNoAlias ||
      Builtin.ID == BuiltinValueKind::TakeArrayFrontToBack ||
      Builtin.ID == BuiltinValueKind::TakeArrayBackToFront ||
      Builtin.ID == BuiltinValueKind::AssignCopyArrayNoAlias ||
      Builtin.ID == BuiltinValueKind::AssignCopyArrayFrontToBack ||
      Builtin.ID == BuiltinValueKind::AssignCopyArrayBackToFront ||
      Builtin.ID == BuiltinValueKind::AssignTakeArray) {
    // The input type is (T.Type, Builtin.RawPointer, Builtin.RawPointer, Builtin.Word).
    /* metatype (which may be thin) */
    if (args.size() == 4)
      args.claimNext();
    llvm::Value *dest = args.claimNext();
    llvm::Value *src = args.claimNext();
    llvm::Value *count = args.claimNext();
    
    auto valueTy = getLoweredTypeAndTypeInfo(IGF.IGM,
                                             substitutions[0].getReplacement());
    
    dest = IGF.Builder.CreateBitCast(dest,
                               valueTy.second.getStorageType()->getPointerTo());
    src = IGF.Builder.CreateBitCast(src,
                               valueTy.second.getStorageType()->getPointerTo());
    Address destArray = valueTy.second.getAddressForPointer(dest);
    Address srcArray = valueTy.second.getAddressForPointer(src);
    
    switch (Builtin.ID) {
    case BuiltinValueKind::CopyArray:
      valueTy.second.initializeArrayWithCopy(IGF, destArray, srcArray, count,
                                             valueTy.first);
      break;
    case BuiltinValueKind::TakeArrayNoAlias:
      valueTy.second.initializeArrayWithTakeNoAlias(IGF, destArray, srcArray,
                                                    count, valueTy.first);
      break;
    case BuiltinValueKind::TakeArrayFrontToBack:
      valueTy.second.initializeArrayWithTakeFrontToBack(IGF, destArray, srcArray,
                                                        count, valueTy.first);
      break;
    case BuiltinValueKind::TakeArrayBackToFront:
      valueTy.second.initializeArrayWithTakeBackToFront(IGF, destArray, srcArray,
                                                        count, valueTy.first);
      break;
    case BuiltinValueKind::AssignCopyArrayNoAlias:
      valueTy.second.assignArrayWithCopyNoAlias(IGF, destArray, srcArray, count,
                                                valueTy.first);
      break;
    case BuiltinValueKind::AssignCopyArrayFrontToBack:
      valueTy.second.assignArrayWithCopyFrontToBack(IGF, destArray, srcArray,
                                                    count, valueTy.first);
      break;
    case BuiltinValueKind::AssignCopyArrayBackToFront:
      valueTy.second.assignArrayWithCopyBackToFront(IGF, destArray, srcArray,
                                                    count, valueTy.first);
      break;
    case BuiltinValueKind::AssignTakeArray:
      valueTy.second.assignArrayWithTake(IGF, destArray, srcArray, count,
                                         valueTy.first);
      break;
    default:
      llvm_unreachable("out of sync with if condition");
    }    
    return;
  }
  
  if (Builtin.ID == BuiltinValueKind::CondUnreachable) {
    // conditionallyUnreachable is a no-op by itself. Since it's noreturn, there
    // should be a true unreachable terminator right after.
    return;
  }
  
  if (Builtin.ID == BuiltinValueKind::ZeroInitializer) {
    // Build a zero initializer of the result type.
    auto valueTy = getLoweredTypeAndTypeInfo(IGF.IGM,
                                             substitutions[0].getReplacement());
    auto schema = valueTy.second.getSchema();
    for (auto &elt : schema) {
      out.add(llvm::Constant::getNullValue(elt.getScalarType()));
    }
    return;
  }
  
  if (Builtin.ID == BuiltinValueKind::GetObjCTypeEncoding) {
    (void)args.claimAll();
    Type valueTy = substitutions[0].getReplacement();
    // Get the type encoding for the associated clang type.
    auto clangTy = IGF.IGM.getClangType(valueTy->getCanonicalType());
    std::string encoding;
    IGF.IGM.getClangASTContext().getObjCEncodingForType(clangTy, encoding);
    
    auto globalString = IGF.IGM.getAddrOfGlobalString(encoding);
    out.add(globalString);
    return;
  }

  if (Builtin.ID == BuiltinValueKind::TSanInoutAccess) {
    auto address = args.claimNext();
    IGF.emitTSanInoutAccessCall(address);
    return;
  }

  if (Builtin.ID == BuiltinValueKind::Swift3ImplicitObjCEntrypoint) {
    llvm::Value *entrypointArgs[7];
    auto argIter = IGF.CurFn->arg_begin();

    // self
    entrypointArgs[0] = &*argIter++;
    if (entrypointArgs[0]->getType() != IGF.IGM.ObjCPtrTy)
      entrypointArgs[0] = IGF.Builder.CreateBitCast(entrypointArgs[0], IGF.IGM.ObjCPtrTy);

    // _cmd
    entrypointArgs[1] = &*argIter;
    if (entrypointArgs[1]->getType() != IGF.IGM.ObjCSELTy)
      entrypointArgs[1] = IGF.Builder.CreateBitCast(entrypointArgs[1], IGF.IGM.ObjCSELTy);
    
    // Filename pointer
    entrypointArgs[2] = args.claimNext();
    // Filename length
    entrypointArgs[3] = args.claimNext();
    // Line
    entrypointArgs[4] = args.claimNext();
    // Column
    entrypointArgs[5] = args.claimNext();
    
    // Create a flag variable so that this invocation logs only once.
    auto flagStorageTy = llvm::ArrayType::get(IGF.IGM.Int8Ty,
                                        IGF.IGM.getAtomicBoolSize().getValue());
    auto flag = new llvm::GlobalVariable(IGF.IGM.Module, flagStorageTy,
                               /*constant*/ false,
                               llvm::GlobalValue::PrivateLinkage,
                               llvm::ConstantAggregateZero::get(flagStorageTy));
    flag->setAlignment(IGF.IGM.getAtomicBoolAlignment().getValue());
    entrypointArgs[6] = llvm::ConstantExpr::getBitCast(flag, IGF.IGM.Int8PtrTy);

    IGF.Builder.CreateCall(IGF.IGM.getSwift3ImplicitObjCEntrypointFn(),
                           entrypointArgs);
    return;
  }

  if (Builtin.ID == BuiltinValueKind::IsSameMetatype) {
    auto metatypeLHS = args.claimNext();
    auto metatypeRHS = args.claimNext();
    (void)args.claimAll();
    llvm::Value *metatypeLHSCasted =
        IGF.Builder.CreateBitCast(metatypeLHS, IGF.IGM.Int8PtrTy);
    llvm::Value *metatypeRHSCasted =
        IGF.Builder.CreateBitCast(metatypeRHS, IGF.IGM.Int8PtrTy);

    out.add(IGF.Builder.CreateICmpEQ(metatypeLHSCasted, metatypeRHSCasted));
    return;
  }

  llvm_unreachable("IRGen unimplemented for this builtin!");
}
