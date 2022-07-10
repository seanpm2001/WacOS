//===--- IRGenModule.cpp - Swift Global LLVM IR Generation ----------------===//
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
//  This file implements IR generation for global declarations in Swift.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/ASTContext.h"
#include "swift/AST/Module.h"
#include "swift/AST/DiagnosticsIRGen.h"
#include "swift/AST/IRGenOptions.h"
#include "swift/Basic/Dwarf.h"
#include "swift/ClangImporter/ClangImporter.h"
#include "swift/Runtime/RuntimeFnWrappersGen.h"
#include "swift/Runtime/Config.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CodeGen/CodeGenABITypes.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Frontend/CodeGenOptions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MD5.h"

#include "GenEnum.h"
#include "GenType.h"
#include "IRGenModule.h"
#include "IRGenDebugInfo.h"
#include "Linking.h"

#include <initializer_list>

using namespace swift;
using namespace irgen;
using llvm::Attribute;

const unsigned DefaultAS = 0;

/// A helper for creating LLVM struct types.
static llvm::StructType *createStructType(IRGenModule &IGM,
                                          StringRef name,
                                  std::initializer_list<llvm::Type*> types) {
  return llvm::StructType::create(IGM.getLLVMContext(),
                                  ArrayRef<llvm::Type*>(types.begin(),
                                                        types.size()),
                                  name);
};

/// A helper for creating pointer-to-struct types.
static llvm::PointerType *createStructPointerType(IRGenModule &IGM,
                                                  StringRef name,
                                  std::initializer_list<llvm::Type*> types) {
  return createStructType(IGM, name, types)->getPointerTo(DefaultAS);
};

static clang::CodeGenerator *createClangCodeGenerator(ASTContext &Context,
                                                 llvm::LLVMContext &LLVMContext,
                                                      IRGenOptions &Opts,
                                                      StringRef ModuleName) {
  auto Loader = Context.getClangModuleLoader();
  auto *Importer = static_cast<ClangImporter*>(&*Loader);
  assert(Importer && "No clang module loader!");
  auto &ClangContext = Importer->getClangASTContext();

  auto &CGO = Importer->getClangCodeGenOpts();
  CGO.OptimizationLevel = Opts.Optimize ? 3 : 0;
  CGO.DisableFPElim = Opts.DisableFPElim;
  CGO.DiscardValueNames = !Opts.shouldProvideValueNames();
  switch (Opts.DebugInfoKind) {
  case IRGenDebugInfoKind::None:
    CGO.setDebugInfo(clang::codegenoptions::DebugInfoKind::NoDebugInfo);
    break;
  case IRGenDebugInfoKind::LineTables:
    CGO.setDebugInfo(clang::codegenoptions::DebugInfoKind::DebugLineTablesOnly);
    break;
  case IRGenDebugInfoKind::ASTTypes:
    // TODO: Enable -gmodules for the clang code generator.
  case IRGenDebugInfoKind::DwarfTypes:
    CGO.setDebugInfo(clang::codegenoptions::DebugInfoKind::FullDebugInfo);
    break;
  }
  if (Opts.DebugInfoKind > IRGenDebugInfoKind::None) {
    CGO.DebugCompilationDir = Opts.DebugCompilationDir;
    CGO.DwarfVersion = Opts.DWARFVersion;
    CGO.DwarfDebugFlags = Opts.DWARFDebugFlags;
  }

  auto &HSI = Importer->getClangPreprocessor()
                  .getHeaderSearchInfo()
                  .getHeaderSearchOpts();
  auto &PPO = Importer->getClangPreprocessor().getPreprocessorOpts();
  auto *ClangCodeGen = clang::CreateLLVMCodeGen(ClangContext.getDiagnostics(),
                                                ModuleName, HSI, PPO, CGO,
                                                LLVMContext);
  ClangCodeGen->Initialize(ClangContext);
  return ClangCodeGen;
}

IRGenModule::IRGenModule(IRGenerator &irgen,
                         std::unique_ptr<llvm::TargetMachine> &&target,
                         SourceFile *SF,
                         llvm::LLVMContext &LLVMContext,
                         StringRef ModuleName,
                         StringRef OutputFilename)
  : IRGen(irgen),
    Context(irgen.SIL.getASTContext()),
    ClangCodeGen(createClangCodeGenerator(Context, LLVMContext,
                                          irgen.Opts, ModuleName)),
    Module(*ClangCodeGen->GetModule()),
    LLVMContext(Module.getContext()),
    DataLayout(target->createDataLayout()),
    Triple(Context.LangOpts.Target),
    TargetMachine(std::move(target)),
    OutputFilename(OutputFilename),
    TargetInfo(SwiftTargetInfo::get(*this)),
    DebugInfo(0), ModuleHash(nullptr),
    ObjCInterop(Context.LangOpts.EnableObjCInterop),
    Types(*new TypeConverter(*this))
{
  irgen.addGenModule(SF, this);

  auto &opts = irgen.Opts;

  EnableValueNames = opts.shouldProvideValueNames();
  
  VoidTy = llvm::Type::getVoidTy(getLLVMContext());
  Int1Ty = llvm::Type::getInt1Ty(getLLVMContext());
  Int8Ty = llvm::Type::getInt8Ty(getLLVMContext());
  Int16Ty = llvm::Type::getInt16Ty(getLLVMContext());
  Int32Ty = llvm::Type::getInt32Ty(getLLVMContext());
  Int64Ty = llvm::Type::getInt64Ty(getLLVMContext());
  Int8PtrTy = llvm::Type::getInt8PtrTy(getLLVMContext());
  Int8PtrPtrTy = Int8PtrTy->getPointerTo(0);
  SizeTy = DataLayout.getIntPtrType(getLLVMContext(), /*addrspace*/ 0);

  auto CI = static_cast<ClangImporter*>(&*Context.getClangModuleLoader());
  assert(CI && "no clang module loader");
  auto &clangASTContext = CI->getClangASTContext();

  ObjCBoolTy = Int1Ty;
  if (clangASTContext.getTargetInfo().useSignedCharForObjCBool())
    ObjCBoolTy = Int8Ty;

  RefCountedStructTy =
    llvm::StructType::create(getLLVMContext(), "swift.refcounted");
  RefCountedPtrTy = RefCountedStructTy->getPointerTo(/*addrspace*/ 0);
  RefCountedNull = llvm::ConstantPointerNull::get(RefCountedPtrTy);

  // For now, native weak references are just a pointer.
  WeakReferencePtrTy =
    createStructPointerType(*this, "swift.weak", { RefCountedPtrTy });

  // Native unowned references are just a pointer.
  UnownedReferencePtrTy =
    createStructPointerType(*this, "swift.unowned", { RefCountedPtrTy });

  // A type metadata record is the structure pointed to by the canonical
  // address point of a type metadata.  This is at least one word, and
  // potentially more than that, past the start of the actual global
  // structure.
  TypeMetadataStructTy = createStructType(*this, "swift.type", {
    MetadataKindTy          // MetadataKind Kind;
  });
  TypeMetadataPtrTy = TypeMetadataStructTy->getPointerTo(DefaultAS);

  // A protocol descriptor describes a protocol. It is not type metadata in
  // and of itself, but is referenced in the structure of existential type
  // metadata records.
  ProtocolDescriptorStructTy = createStructType(*this, "swift.protocol", {
    Int8PtrTy,              // objc isa
    Int8PtrTy,              // name
    Int8PtrTy,              // inherited protocols
    Int8PtrTy,              // required objc instance methods
    Int8PtrTy,              // required objc class methods
    Int8PtrTy,              // optional objc instance methods
    Int8PtrTy,              // optional objc class methods
    Int8PtrTy,              // objc properties
    Int32Ty,                // size
    Int32Ty,                // flags
    Int16Ty,                // minimum witness count
    Int16Ty,                // default witness count
    Int32Ty                 // padding
  });
  
  ProtocolDescriptorPtrTy = ProtocolDescriptorStructTy->getPointerTo();
  
  // A tuple type metadata record has a couple extra fields.
  auto tupleElementTy = createStructType(*this, "swift.tuple_element_type", {
    TypeMetadataPtrTy,      // Metadata *Type;
    SizeTy                  // size_t Offset;
  });
  TupleTypeMetadataPtrTy = createStructPointerType(*this, "swift.tuple_type", {
    TypeMetadataStructTy,   // (base)
    SizeTy,                 // size_t NumElements;
    Int8PtrTy,              // const char *Labels;
    llvm::ArrayType::get(tupleElementTy, 0) // Element Elements[];
  });

  // A full type metadata record is basically just an adjustment to the
  // address point of a type metadata.  Resilience may cause
  // additional data to be laid out prior to this address point.
  FullTypeMetadataStructTy = createStructType(*this, "swift.full_type", {
    WitnessTablePtrTy,
    TypeMetadataStructTy
  });
  FullTypeMetadataPtrTy = FullTypeMetadataStructTy->getPointerTo(DefaultAS);

  // A metadata pattern is a structure from which generic type
  // metadata are allocated.  We leave this struct type intentionally
  // opaque, because the compiler basically never needs to access
  // anything from one.
  TypeMetadataPatternStructTy =
    llvm::StructType::create(getLLVMContext(), "swift.type_pattern");
  TypeMetadataPatternPtrTy =
    TypeMetadataPatternStructTy->getPointerTo(DefaultAS);

  DeallocatingDtorTy = llvm::FunctionType::get(VoidTy, RefCountedPtrTy, false);
  llvm::Type *dtorPtrTy = DeallocatingDtorTy->getPointerTo();

  // A full heap metadata is basically just an additional small prefix
  // on a full metadata, used for metadata corresponding to heap
  // allocations.
  FullHeapMetadataStructTy =
                  createStructType(*this, "swift.full_heapmetadata", {
    dtorPtrTy,
    WitnessTablePtrTy,
    TypeMetadataStructTy
  });
  FullHeapMetadataPtrTy = FullHeapMetadataStructTy->getPointerTo(DefaultAS);

  // A full box metadata is non-type heap metadata for a heap allocation of a
  // single value. The box tracks the offset to the value inside the box.
  FullBoxMetadataStructTy =
                  createStructType(*this, "swift.full_boxmetadata", {
    dtorPtrTy,
    WitnessTablePtrTy,
    TypeMetadataStructTy,
    Int32Ty,
    CaptureDescriptorPtrTy,
  });
  FullBoxMetadataPtrTy = FullBoxMetadataStructTy->getPointerTo(DefaultAS);


  llvm::Type *refCountedElts[] = { TypeMetadataPtrTy, Int32Ty, Int32Ty };
  RefCountedStructTy->setBody(refCountedElts);

  PtrSize = Size(DataLayout.getPointerSize(DefaultAS));

  FunctionPairTy = createStructType(*this, "swift.function", {
    FunctionPtrTy,
    RefCountedPtrTy,
  });
  WitnessFunctionPairTy = createStructType(*this, "swift.function", {
    FunctionPtrTy,
    WitnessTablePtrTy,
  });
  
  OpaquePtrTy = llvm::StructType::create(LLVMContext, "swift.opaque")
                  ->getPointerTo(DefaultAS);

  ProtocolConformanceRecordTy
    = createStructType(*this, "swift.protocol_conformance", {
      RelativeAddressTy,
      RelativeAddressTy,
      RelativeAddressTy,
      Int32Ty
    });
  ProtocolConformanceRecordPtrTy
    = ProtocolConformanceRecordTy->getPointerTo(DefaultAS);

  NominalTypeDescriptorTy
    = llvm::StructType::create(LLVMContext, "swift.type_descriptor");
  NominalTypeDescriptorPtrTy
    = NominalTypeDescriptorTy->getPointerTo(DefaultAS);

  TypeMetadataRecordTy
    = createStructType(*this, "swift.type_metadata_record", {
      RelativeAddressTy,
      Int32Ty
    });
  TypeMetadataRecordPtrTy
    = TypeMetadataRecordTy->getPointerTo(DefaultAS);

  FieldDescriptorTy
    = llvm::StructType::create(LLVMContext, "swift.field_descriptor");
  FieldDescriptorPtrTy = FieldDescriptorTy->getPointerTo(DefaultAS);

  FixedBufferTy = nullptr;
  for (unsigned i = 0; i != MaxNumValueWitnesses; ++i)
    ValueWitnessTys[i] = nullptr;

  ObjCPtrTy = llvm::StructType::create(getLLVMContext(), "objc_object")
                ->getPointerTo(DefaultAS);
  BridgeObjectPtrTy = llvm::StructType::create(getLLVMContext(), "swift.bridge")
                ->getPointerTo(DefaultAS);

  ObjCClassStructTy = llvm::StructType::create(LLVMContext, "objc_class");
  ObjCClassPtrTy = ObjCClassStructTy->getPointerTo(DefaultAS);
  llvm::Type *objcClassElts[] = {
    ObjCClassPtrTy,
    ObjCClassPtrTy,
    OpaquePtrTy,
    OpaquePtrTy,
    IntPtrTy
  };
  ObjCClassStructTy->setBody(objcClassElts);

  ObjCSuperStructTy = llvm::StructType::create(LLVMContext, "objc_super");
  ObjCSuperPtrTy = ObjCSuperStructTy->getPointerTo(DefaultAS);
  llvm::Type *objcSuperElts[] = {
    ObjCPtrTy,
    ObjCClassPtrTy
  };
  ObjCSuperStructTy->setBody(objcSuperElts);
  
  ObjCBlockStructTy = llvm::StructType::create(LLVMContext, "objc_block");
  ObjCBlockPtrTy = ObjCBlockStructTy->getPointerTo(DefaultAS);
  llvm::Type *objcBlockElts[] = {
    ObjCClassPtrTy, // isa
    Int32Ty,        // flags
    Int32Ty,        // reserved
    FunctionPtrTy,  // invoke function pointer
    Int8PtrTy,      // TODO: block descriptor pointer.
                    // We will probably need a struct type for that at some
                    // point too.
  };
  ObjCBlockStructTy->setBody(objcBlockElts);
  
  auto ErrorStructTy = llvm::StructType::create(LLVMContext, "swift.error");
  // ErrorStruct is currently opaque to the compiler.
  ErrorPtrTy = ErrorStructTy->getPointerTo(DefaultAS);
  
  llvm::Type *openedErrorTriple[] = {
    OpaquePtrTy,
    TypeMetadataPtrTy,
    WitnessTablePtrTy,
  };
  OpenedErrorTripleTy = llvm::StructType::get(getLLVMContext(),
                                              openedErrorTriple,
                                              /*packed*/ false);
  OpenedErrorTriplePtrTy = OpenedErrorTripleTy->getPointerTo(DefaultAS);
  
  InvariantMetadataID = LLVMContext.getMDKindID("invariant.load");
  InvariantNode = llvm::MDNode::get(LLVMContext, {});
  DereferenceableID = LLVMContext.getMDKindID("dereferenceable");
  
  C_CC = llvm::CallingConv::C;
  // TODO: use "tinycc" on platforms that support it
  DefaultCC = SWIFT_LLVM_CC(DefaultCC);
  // If it is an interpreter, don't use try to use any
  // advanced calling conventions and use instead a
  // more conservative C calling convention. This
  // makes sure that none of the registers eventually
  // used by the dynamic linker are used by generated code.
  // TODO: Check that the deployment target supports the new
  // calling convention. Older versions of the runtime library
  // may not contain the entries using the new calling convention.

  // Only use the new calling conventions on platforms that support it.
  auto Arch = Triple.getArch();
  if (SWIFT_RT_USE_RegisterPreservingCC &&
      Arch == llvm::Triple::ArchType::aarch64)
    RegisterPreservingCC = SWIFT_LLVM_CC(RegisterPreservingCC);
  else
    RegisterPreservingCC = DefaultCC;

  if (IRGen.Opts.DebugInfoKind > IRGenDebugInfoKind::None)
    DebugInfo = new IRGenDebugInfo(IRGen.Opts, *CI, *this, Module, SF);

  initClangTypeConverter();
}

IRGenModule::~IRGenModule() {
  destroyClangTypeConverter();
  delete &Types;
  delete DebugInfo;
}

static bool isReturnAttribute(llvm::Attribute::AttrKind Attr);

// Explicitly listing these constants is an unfortunate compromise for
// making the database file much more compact.
//
// They have to be non-local because otherwise we'll get warnings when
// a particular x-macro expansion doesn't use one.
namespace RuntimeConstants {
  const auto ReadNone = llvm::Attribute::ReadNone;
  const auto ReadOnly = llvm::Attribute::ReadOnly;
  const auto NoReturn = llvm::Attribute::NoReturn;
  const auto NoUnwind = llvm::Attribute::NoUnwind;
  const auto ZExt = llvm::Attribute::ZExt;
}

// We don't use enough attributes to justify generalizing the
// RuntimeFunctions.def FUNCTION macro. Instead, special case the one attribute
// associated with the return type not the function type.
static bool isReturnAttribute(llvm::Attribute::AttrKind Attr) {
  return Attr == llvm::Attribute::ZExt;
}

llvm::Constant *swift::getRuntimeFn(llvm::Module &Module,
                      llvm::Constant *&cache,
                      char const *name,
                      llvm::CallingConv::ID cc,
                      llvm::ArrayRef<llvm::Type*> retTypes,
                      llvm::ArrayRef<llvm::Type*> argTypes,
                      ArrayRef<Attribute::AttrKind> attrs) {
  if (cache)
    return cache;
  
  llvm::Type *retTy;
  if (retTypes.size() == 1)
    retTy = *retTypes.begin();
  else
    retTy = llvm::StructType::get(Module.getContext(),
                                  {retTypes.begin(), retTypes.end()},
                                  /*packed*/ false);
  auto fnTy = llvm::FunctionType::get(retTy,
                                      {argTypes.begin(), argTypes.end()},
                                      /*isVararg*/ false);

  cache = Module.getOrInsertFunction(name, fnTy);

  // Add any function attributes and set the calling convention.
  if (auto fn = dyn_cast<llvm::Function>(cache)) {
    fn->setCallingConv(cc);

    if (llvm::Triple(Module.getTargetTriple()).isOSBinFormatCOFF() &&
        (fn->getLinkage() == llvm::GlobalValue::ExternalLinkage ||
         fn->getLinkage() == llvm::GlobalValue::AvailableExternallyLinkage))
      fn->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);

    llvm::AttrBuilder buildFnAttr;
    llvm::AttrBuilder buildRetAttr;

    for (auto Attr : attrs) {
      if (isReturnAttribute(Attr))
        buildRetAttr.addAttribute(Attr);
      else
        buildFnAttr.addAttribute(Attr);
    }
    // FIXME: getting attributes here without setting them does
    // nothing. This cannot be fixed until the attributes are correctly specified.
    fn->getAttributes().
      addAttributes(Module.getContext(),
                    llvm::AttributeSet::FunctionIndex,
                    llvm::AttributeSet::get(Module.getContext(),
                                            llvm::AttributeSet::FunctionIndex,
                                            buildFnAttr));
    fn->getAttributes().
      addAttributes(Module.getContext(),
                    llvm::AttributeSet::ReturnIndex,
                    llvm::AttributeSet::get(Module.getContext(),
                                            llvm::AttributeSet::ReturnIndex,
                                            buildRetAttr));
  }

  return cache;
}

llvm::Constant *swift::getWrapperFn(llvm::Module &Module,
                                    llvm::Constant *&cache,
                                    char const *name,
                                    char const *symbol,
                                    llvm::CallingConv::ID cc,
                                    llvm::ArrayRef<llvm::Type *> retTypes,
                                    llvm::ArrayRef<llvm::Type *> argTypes,
                                    ArrayRef<Attribute::AttrKind> attrs) {
  assert(symbol && "Symbol name should be defined for a wrapper function");
  // Wrapper names should have a prefix to distinguish them from the
  // actual runtime functions.
  llvm::SmallString<128> wrapperNameStr;
  llvm::raw_svector_ostream buffer(wrapperNameStr);
  buffer << SWIFT_WRAPPER_PREFIX;
  buffer << name;
  const char *wrapperName = wrapperNameStr.c_str();
  auto fn = getRuntimeFn(Module, cache, wrapperName, cc,
                         retTypes, argTypes, attrs);
  auto *fun = dyn_cast<llvm::Function>(fn);
  assert(fun && "Wrapper should be an llvm::Function");
  // Do not inline wrappers, because this would result in a code size increase.
  fun->addAttribute(llvm::AttributeSet::FunctionIndex,
                    llvm::Attribute::NoInline);
  assert(fun->hasFnAttribute(llvm::Attribute::NoInline) &&
         "Wrappers should not be inlined");
  if (fun->empty()) {
    // All wrappers should have ODR linkage so that a linker can detect them
    // and leave only one copy.
    fun->setLinkage(llvm::Function::LinkOnceODRLinkage);
    fun->setVisibility(llvm::Function::HiddenVisibility);
    fun->setDLLStorageClass(llvm::Function::DefaultStorageClass);
    fun->setDoesNotThrow();

    // Add the body of a wrapper.
    // It simply invokes the actual implementation of the runtime entry
    // by means of indirect call through a pointer stored in the
    // global variable.
    llvm::IRBuilder<> Builder(Module.getContext());
    llvm::BasicBlock *bb =
        llvm::BasicBlock::Create(Module.getContext(), "entry", fun);
    Builder.SetInsertPoint(bb);
    auto fnTy = fun->getFunctionType();
    auto fnPtrTy = llvm::PointerType::getUnqual(fnTy);

    auto *globalFnPtr =
        new llvm::GlobalVariable(Module, fnPtrTy, false,
                                 llvm::GlobalValue::ExternalLinkage, 0, symbol);
    if (llvm::Triple(Module.getTargetTriple()).isOSBinFormatCOFF())
      globalFnPtr->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);

    // Forward all arguments.
    llvm::SmallVector<llvm::Value *, 4> args;
    for (auto &arg: fun->args()) {
      args.push_back(&arg);
    }


    auto fnPtr = Builder.CreateLoad(globalFnPtr, "load");
    auto call = Builder.CreateCall(fnPtr, args);
    call->setCallingConv(cc);
    call->setTailCall(true);

    auto VoidTy = llvm::Type::getVoidTy(Module.getContext());
    if (retTypes.size() == 1 && *retTypes.begin() == VoidTy)
      Builder.CreateRetVoid();
    else
      Builder.CreateRet(call);
  }

  return fn;
}

#define QUOTE(...) __VA_ARGS__
#define STR(X)     #X

#define FUNCTION_FOR_CONV_DefaultCC(ID, NAME, CC, RETURNS, ARGS, ATTRS)        \
  FUNCTION_IMPL(ID, NAME, CC, QUOTE(RETURNS), QUOTE(ARGS), QUOTE(ATTRS))

#define FUNCTION_FOR_CONV_C_CC(ID, NAME, CC, RETURNS, ARGS, ATTRS)             \
  FUNCTION_IMPL(ID, NAME, CC, QUOTE(RETURNS), QUOTE(ARGS), QUOTE(ATTRS))

#define FUNCTION_FOR_CONV_RegisterPreservingCC(ID, NAME, CC, RETURNS, ARGS,    \
                                               ATTRS)                          \
  FUNCTION_WITH_GLOBAL_SYMBOL_IMPL(ID, NAME, SWIFT_RT_ENTRY_REF(NAME), CC,     \
                                   QUOTE(RETURNS), QUOTE(ARGS), QUOTE(ATTRS))

#define FUNCTION(ID, NAME, CC, RETURNS, ARGS, ATTRS)                           \
  FUNCTION_FOR_CONV_##CC(ID, NAME, CC, QUOTE(RETURNS), QUOTE(ARGS),            \
                         QUOTE(ATTRS))

#define FUNCTION_WITH_GLOBAL_SYMBOL_FOR_CONV_DefaultCC(ID, NAME, SYMBOL, CC,   \
                                                       RETURNS, ARGS, ATTRS)   \
  FUNCTION_IMPL(ID, NAME, CC, QUOTE(RETURNS), QUOTE(ARGS), QUOTE(ATTRS))

#define FUNCTION_WITH_GLOBAL_SYMBOL_FOR_CONV_C_CC(ID, NAME, SYMBOL, CC,        \
                                                  RETURNS, ARGS, ATTRS)        \
  FUNCTION_IMPL(ID, NAME, CC, QUOTE(RETURNS), QUOTE(ARGS), QUOTE(ATTRS))

#define FUNCTION_WITH_GLOBAL_SYMBOL_FOR_CONV_RegisterPreservingCC(             \
    ID, NAME, SYMBOL, CC, RETURNS, ARGS, ATTRS)                                \
  FUNCTION_WITH_GLOBAL_SYMBOL_IMPL(ID, NAME, SYMBOL, CC, QUOTE(RETURNS),       \
                                   QUOTE(ARGS), QUOTE(ATTRS))

#define FUNCTION_WITH_GLOBAL_SYMBOL_AND_IMPL(ID, NAME, SYMBOL, IMPL, CC,       \
                                             RETURNS, ARGS, ATTRS)             \
  FUNCTION_WITH_GLOBAL_SYMBOL_FOR_CONV_##CC(                                   \
      ID, NAME, SYMBOL, CC, QUOTE(RETURNS), QUOTE(ARGS), QUOTE(ATTRS))

#define RETURNS(...)                                                           \
  { __VA_ARGS__ }
#define ARGS(...) { __VA_ARGS__ }
#define NO_ARGS {}
#define ATTRS(...) { __VA_ARGS__ }
#define NO_ATTRS {}

#define FUNCTION_IMPL(ID, NAME, CC, RETURNS, ARGS, ATTRS)                      \
  llvm::Constant *IRGenModule::get##ID##Fn() {                                 \
    using namespace RuntimeConstants;                                          \
    return getRuntimeFn(Module, ID##Fn, #NAME, CC, RETURNS, ARGS, ATTRS);      \
  }

#define FUNCTION_WITH_GLOBAL_SYMBOL_IMPL(ID, NAME, SYMBOL_NAME, CC, RETURNS,   \
                                         ARGS, ATTRS)                          \
  llvm::Constant *IRGenModule::get##ID##Fn() {                                 \
    using namespace RuntimeConstants;                                          \
    return getWrapperFn(Module, ID##Fn, #NAME, STR(SYMBOL_NAME), CC, RETURNS,  \
                        ARGS, ATTRS);                                          \
  }

#include "swift/Runtime/RuntimeFunctions.def"

std::pair<llvm::GlobalVariable *, llvm::Constant *>
IRGenModule::createStringConstant(StringRef Str,
  bool willBeRelativelyAddressed, StringRef sectionName) {
  // If not, create it.  This implicitly adds a trailing null.
  auto init = llvm::ConstantDataArray::getString(LLVMContext, Str);
  auto global = new llvm::GlobalVariable(Module, init->getType(), true,
                                         llvm::GlobalValue::PrivateLinkage,
                                         init);
  // FIXME: ld64 crashes resolving relative references to coalesceable symbols.
  // rdar://problem/22674524
  // If we intend to relatively address this string, don't mark it with
  // unnamed_addr to prevent it from going into the cstrings section and getting
  // coalesced.
  if (!willBeRelativelyAddressed)
    global->setUnnamedAddr(true);

  if (!sectionName.empty())
    global->setSection(sectionName);

  // Drill down to make an i8*.
  auto zero = llvm::ConstantInt::get(SizeTy, 0);
  llvm::Constant *indices[] = { zero, zero };
  auto address = llvm::ConstantExpr::getInBoundsGetElementPtr(
    global->getValueType(), global, indices);

  return { global, address };
}

llvm::Constant *IRGenModule::getEmptyTupleMetadata() {
  if (EmptyTupleMetadata)
    return EmptyTupleMetadata;

  EmptyTupleMetadata =
      Module.getOrInsertGlobal("_TMT_", FullTypeMetadataStructTy);
  if (Triple.isOSBinFormatCOFF())
    cast<llvm::GlobalVariable>(EmptyTupleMetadata)
        ->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
  return EmptyTupleMetadata;
}

llvm::Constant *IRGenModule::getObjCEmptyCachePtr() {
  if (ObjCEmptyCachePtr)
    return ObjCEmptyCachePtr;

  if (ObjCInterop) {
    // struct objc_cache _objc_empty_cache;
    ObjCEmptyCachePtr = Module.getOrInsertGlobal("_objc_empty_cache",
                                                 OpaquePtrTy->getElementType());
    if (Triple.isOSBinFormatCOFF())
      cast<llvm::GlobalVariable>(ObjCEmptyCachePtr)
          ->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
  } else {
    // FIXME: Remove even the null value per rdar://problem/18801263
    ObjCEmptyCachePtr = llvm::ConstantPointerNull::get(OpaquePtrTy);
  }
  return ObjCEmptyCachePtr;
}

llvm::Constant *IRGenModule::getObjCEmptyVTablePtr() {
  // IMP _objc_empty_vtable;

  // On recent Darwin platforms, this symbol is defined at
  // runtime as an absolute symbol with the value of null. Older ObjCs
  // didn't guarantee _objc_empty_vtable to be nil, but Swift doesn't
  // deploy far enough back for that to be a concern.

  // FIXME: When !ObjCInterop, we should remove even the null value per
  // rdar://problem/18801263

  if (!ObjCEmptyVTablePtr)
    ObjCEmptyVTablePtr = llvm::ConstantPointerNull::get(OpaquePtrTy);

  return ObjCEmptyVTablePtr;
}

Address IRGenModule::getAddrOfObjCISAMask() {
  // This symbol is only exported by the runtime if the platform uses
  // isa masking.
  assert(TargetInfo.hasISAMasking());
  if (!ObjCISAMaskPtr) {
    ObjCISAMaskPtr = Module.getOrInsertGlobal("swift_isaMask", IntPtrTy);
    if (Triple.isOSBinFormatCOFF())
      cast<llvm::GlobalVariable>(ObjCISAMaskPtr)
          ->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
  }
  return Address(ObjCISAMaskPtr, getPointerAlignment());
}

ModuleDecl *IRGenModule::getSwiftModule() const {
  return IRGen.SIL.getSwiftModule();
}

Lowering::TypeConverter &IRGenModule::getSILTypes() const {
  return IRGen.SIL.Types;
}

llvm::Module *IRGenModule::getModule() const {
  return ClangCodeGen->GetModule();
}

llvm::Module *IRGenModule::releaseModule() {
  return ClangCodeGen->ReleaseModule();
}

llvm::AttributeSet IRGenModule::getAllocAttrs() {
  if (AllocAttrs.isEmpty()) {
    AllocAttrs = llvm::AttributeSet::get(LLVMContext,
                                         llvm::AttributeSet::ReturnIndex,
                                         llvm::Attribute::NoAlias);
    AllocAttrs = AllocAttrs.addAttribute(LLVMContext,
                               llvm::AttributeSet::FunctionIndex,
                               llvm::Attribute::NoUnwind);
  }
  return AllocAttrs;
}

/// Construct initial attributes from options.
llvm::AttributeSet IRGenModule::constructInitialAttributes() {
  llvm::AttributeSet attrsUpdated;
  // Add DisableFPElim. 
  if (!IRGen.Opts.DisableFPElim) {
    attrsUpdated = attrsUpdated.addAttribute(LLVMContext,
                     llvm::AttributeSet::FunctionIndex,
                     "no-frame-pointer-elim", "false");
  } else {
    attrsUpdated = attrsUpdated.addAttribute(
        LLVMContext, llvm::AttributeSet::FunctionIndex,
        "no-frame-pointer-elim", "true");
    attrsUpdated = attrsUpdated.addAttribute(
        LLVMContext, llvm::AttributeSet::FunctionIndex,
        "no-frame-pointer-elim-non-leaf");
  }

  // Add target-cpu and target-features if they are non-null.
  auto *Clang = static_cast<ClangImporter *>(Context.getClangModuleLoader());
  clang::TargetOptions &ClangOpts = Clang->getTargetInfo().getTargetOpts();

  std::string &CPU = ClangOpts.CPU;
  if (CPU != "")
    attrsUpdated = attrsUpdated.addAttribute(LLVMContext,
                     llvm::AttributeSet::FunctionIndex, "target-cpu", CPU);

  std::vector<std::string> &Features = ClangOpts.Features;
  if (!Features.empty()) {
    SmallString<64> allFeatures;
    interleave(Features, [&](const std::string &s) {
      allFeatures.append(s);
    }, [&]{
      allFeatures.push_back(',');
    });
    attrsUpdated = attrsUpdated.addAttribute(LLVMContext,
                     llvm::AttributeSet::FunctionIndex, "target-features",
                     allFeatures);
  }
  return attrsUpdated;
}

llvm::Constant *IRGenModule::getSize(Size size) {
  return llvm::ConstantInt::get(SizeTy, size.getValue());
}

static void appendEncodedName(raw_ostream &os, StringRef name) {
  if (clang::isValidIdentifier(name)) {
    os << "_" << name;
  } else {
    for (auto c : name)
      os.write_hex(static_cast<uint8_t>(c));
  }
}

static void appendEncodedName(llvm::SmallVectorImpl<char> &buf,
                              StringRef name) {
  llvm::raw_svector_ostream os{buf};
  appendEncodedName(os, name);
}

static StringRef encodeForceLoadSymbolName(llvm::SmallVectorImpl<char> &buf,
                                           StringRef name) {
  llvm::raw_svector_ostream os{buf};
  os << "_swift_FORCE_LOAD_$";
  appendEncodedName(os, name);
  return os.str();
}

llvm::SmallString<32> getTargetDependentLibraryOption(const llvm::Triple &T,
                                                      StringRef library) {
  llvm::SmallString<32> buffer;

  if (T.isWindowsMSVCEnvironment() || T.isWindowsItaniumEnvironment()) {
    bool quote = library.find(' ') != StringRef::npos;

    buffer += "/DEFAULTLIB:";
    if (quote)
      buffer += '"';
    buffer += library;
    if (!library.endswith_lower(".lib"))
      buffer += ".lib";
    if (quote)
      buffer += '"';
  } else if (T.isPS4()) {
    bool quote = library.find(' ') != StringRef::npos;

    buffer += "\01";
    if (quote)
      buffer += '"';
    buffer += library;
    if (quote)
      buffer += '"';
  } else {
    buffer += "-l";
    buffer += library;
  }

  return buffer;
}

void IRGenModule::addLinkLibrary(const LinkLibrary &linkLib) {
  llvm::LLVMContext &ctx = Module.getContext();

  switch (linkLib.getKind()) {
  case LibraryKind::Library: {
    llvm::SmallString<32> opt =
        getTargetDependentLibraryOption(Triple, linkLib.getName());
    AutolinkEntries.push_back(
        llvm::MDNode::get(ctx, llvm::MDString::get(ctx, opt)));
    break;
  }
  case LibraryKind::Framework: {
    // If we're supposed to disable autolinking of this framework, bail out.
    auto &frameworks = IRGen.Opts.DisableAutolinkFrameworks;
    if (std::find(frameworks.begin(), frameworks.end(), linkLib.getName())
          != frameworks.end())
      return;

    llvm::Metadata *args[] = {
      llvm::MDString::get(ctx, "-framework"),
      llvm::MDString::get(ctx, linkLib.getName())
    };
    AutolinkEntries.push_back(llvm::MDNode::get(ctx, args));
    break;
  }
  }

  if (linkLib.shouldForceLoad()) {
    llvm::SmallString<64> buf;
    encodeForceLoadSymbolName(buf, linkLib.getName());
    auto symbolAddr = Module.getOrInsertGlobal(buf.str(), Int1Ty);
    if (Triple.isOSBinFormatCOFF())
      cast<llvm::GlobalVariable>(symbolAddr)
          ->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);

    buf += "_$";
    appendEncodedName(buf, IRGen.Opts.ModuleName);

    if (!Module.getGlobalVariable(buf.str())) {
      auto ref = new llvm::GlobalVariable(Module, symbolAddr->getType(),
                                          /*constant=*/true,
                                          llvm::GlobalValue::WeakAnyLinkage,
                                          symbolAddr, buf.str());
      ref->setVisibility(llvm::GlobalValue::HiddenVisibility);
      auto casted = llvm::ConstantExpr::getBitCast(ref, Int8PtrTy);
      LLVMUsed.push_back(casted);
    }
  }
}

static bool replaceModuleFlagsEntry(llvm::LLVMContext &Ctx,
                                    llvm::Module &Module, StringRef EntryName,
                                    llvm::Module::ModFlagBehavior Behavior,
                                    llvm::Metadata *Val) {
  auto *ModuleFlags = Module.getModuleFlagsMetadata();

  for (unsigned I = 0, E = ModuleFlags->getNumOperands(); I != E; ++I) {
    llvm::MDNode *Op = ModuleFlags->getOperand(I);
    llvm::MDString *ID = cast<llvm::MDString>(Op->getOperand(1));

    if (ID->getString().equals(EntryName)) {

      // Create the new entry.
      llvm::Type *Int32Ty = llvm::Type::getInt32Ty(Ctx);
      llvm::Metadata *Ops[3] = {llvm::ConstantAsMetadata::get(
                                    llvm::ConstantInt::get(Int32Ty, Behavior)),
                                llvm::MDString::get(Ctx, EntryName), Val};

      ModuleFlags->setOperand(I, llvm::MDNode::get(Ctx, Ops));
      return true;
    }
  }
  llvm_unreachable("Could not replace old linker options entry?");
}

void IRGenModule::emitAutolinkInfo() {

  // FIXME: This constant should be vended by LLVM somewhere.
  static const char * const LinkerOptionsFlagName = "Linker Options";

  // Collect the linker options already in the module (from ClangCodeGen).
  auto *LinkerOptions = Module.getModuleFlag(LinkerOptionsFlagName);
  if (LinkerOptions)
    for (const auto &LinkOption : cast<llvm::MDNode>(LinkerOptions)->operands())
      AutolinkEntries.push_back(LinkOption);

  // Remove duplicates.
  llvm::SmallPtrSet<llvm::Metadata*, 4> knownAutolinkEntries;
  AutolinkEntries.erase(std::remove_if(AutolinkEntries.begin(),
                                       AutolinkEntries.end(),
                                       [&](llvm::Metadata *entry) -> bool {
                                         return !knownAutolinkEntries.insert(
                                                   entry).second;
                                       }),
                        AutolinkEntries.end());

  if (TargetInfo.OutputObjectFormat == llvm::Triple::COFF ||
      TargetInfo.OutputObjectFormat == llvm::Triple::MachO ||
      Triple.isPS4()) {
    llvm::LLVMContext &ctx = Module.getContext();

    if (!LinkerOptions) {
      // Create a new linker flag entry.
      Module.addModuleFlag(llvm::Module::AppendUnique, LinkerOptionsFlagName,
                           llvm::MDNode::get(ctx, AutolinkEntries));
    } else {
      // Replace the old linker flag entry.
      bool FoundOldEntry = replaceModuleFlagsEntry(
          ctx, Module, LinkerOptionsFlagName, llvm::Module::AppendUnique,
          llvm::MDNode::get(ctx, AutolinkEntries));

      (void)FoundOldEntry;
      assert(FoundOldEntry && "Could not replace old linker options entry?");
    }
  } else {
    assert(TargetInfo.OutputObjectFormat == llvm::Triple::ELF &&
           "expected ELF output format");

    // Merge the entries into null-separated string.
    llvm::SmallString<64> EntriesString;
    for (auto &EntryNode : AutolinkEntries) {
      const llvm::MDNode *MD = cast<llvm::MDNode>(EntryNode);
      for (auto &Entry : MD->operands()) {
        const llvm::MDString *MS = cast<llvm::MDString>(Entry);
        EntriesString += MS->getString();
        EntriesString += '\0';
      }
    }
    auto EntriesConstant = llvm::ConstantDataArray::getString(
        LLVMContext, EntriesString, /*AddNull=*/false);

    auto var =
        new llvm::GlobalVariable(*getModule(), EntriesConstant->getType(), true,
                                 llvm::GlobalValue::PrivateLinkage,
                                 EntriesConstant, "_swift1_autolink_entries");
    var->setSection(".swift1_autolink_entries");
    var->setAlignment(getPointerAlignment().getValue());

    addUsedGlobal(var);
  }

  if (!IRGen.Opts.ForceLoadSymbolName.empty()) {
    llvm::SmallString<64> buf;
    encodeForceLoadSymbolName(buf, IRGen.Opts.ForceLoadSymbolName);
    auto symbol =
        new llvm::GlobalVariable(Module, Int1Ty, /*constant=*/false,
                                 llvm::GlobalValue::CommonLinkage,
                                 llvm::Constant::getNullValue(Int1Ty),
                                 buf.str());
    if (Triple.isOSBinFormatCOFF())
      symbol->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
  }
}

void IRGenModule::cleanupClangCodeGenMetadata() {
  // Remove llvm.ident that ClangCodeGen might have left in the module.
  auto *LLVMIdent = Module.getNamedMetadata("llvm.ident");
  if (LLVMIdent)
    Module.eraseNamedMetadata(LLVMIdent);

  // LLVM's object-file emission collects a fixed set of keys for the
  // image info.
  // Using "Objective-C Garbage Collection" as the key here is a hack,
  // but LLVM's object-file emission isn't general enough to collect
  // arbitrary keys to put in the

  const char *ObjectiveCGarbageCollection = "Objective-C Garbage Collection";
  if (Module.getModuleFlag(ObjectiveCGarbageCollection)) {
    bool FoundOldEntry = replaceModuleFlagsEntry(
        Module.getContext(), Module, ObjectiveCGarbageCollection,
        llvm::Module::Override,
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(Int32Ty, (uint32_t)(swiftVersion << 8))));

    (void)FoundOldEntry;
    assert(FoundOldEntry && "Could not replace old module flag entry?");
  } else
    Module.addModuleFlag(llvm::Module::Override,
                         ObjectiveCGarbageCollection,
                         (uint32_t)(swiftVersion << 8));
}

bool IRGenModule::finalize() {
  const char *ModuleHashVarName = "llvm.swift_module_hash";
  if (IRGen.Opts.OutputKind == IRGenOutputKind::ObjectFile &&
      !Module.getGlobalVariable(ModuleHashVarName)) {
    // Create a global variable into which we will store the hash of the
    // module (used for incremental compilation).
    // We have to create the variable now (before we emit the global lists).
    // But we want to calculate the hash later because later we can do it
    // multi-threaded.
    llvm::MD5::MD5Result zero = { 0 };
    ArrayRef<uint8_t> ZeroArr(zero, sizeof(llvm::MD5::MD5Result));
    auto *ZeroConst = llvm::ConstantDataArray::get(Module.getContext(), ZeroArr);
    ModuleHash = new llvm::GlobalVariable(Module, ZeroConst->getType(), true,
                                          llvm::GlobalValue::PrivateLinkage,
                                          ZeroConst, ModuleHashVarName);
    switch (TargetInfo.OutputObjectFormat) {
    case llvm::Triple::MachO:
      // On Darwin the linker ignores the __LLVM segment.
      ModuleHash->setSection("__LLVM,__swift_modhash");
      break;
    case llvm::Triple::ELF:
    case llvm::Triple::COFF:
      ModuleHash->setSection(".swift_modhash");
      break;
    default:
      llvm_unreachable("Don't know how to emit the module hash for the selected"
                       "object format.");
    }
    addUsedGlobal(ModuleHash);
  }
  emitLazyPrivateDefinitions();

  // Finalize clang IR-generation.
  finalizeClangCodeGen();

  // If that failed, report failure up and skip the final clean-up.
  if (!ClangCodeGen->GetModule())
    return false;

  emitAutolinkInfo();
  emitGlobalLists();
  if (DebugInfo)
    DebugInfo->finalize();
  cleanupClangCodeGenMetadata();

  return true;
}

/// Emit lazy definitions that have to be emitted in this specific
/// IRGenModule.
void IRGenModule::emitLazyPrivateDefinitions() {
  emitLazyObjCProtocolDefinitions();
}

void IRGenModule::unimplemented(SourceLoc loc, StringRef message) {
  Context.Diags.diagnose(loc, diag::irgen_unimplemented, message);
}

void IRGenModule::fatal_unimplemented(SourceLoc loc, StringRef message) {
  Context.Diags.diagnose(loc, diag::irgen_unimplemented, message);
  llvm::report_fatal_error(llvm::Twine("unimplemented IRGen feature! ") +
                             message);
}

void IRGenModule::error(SourceLoc loc, const Twine &message) {
  SmallVector<char, 128> buffer;
  Context.Diags.diagnose(loc, diag::irgen_failure,
                         message.toStringRef(buffer));
}

void IRGenerator::addGenModule(SourceFile *SF, IRGenModule *IGM) {
  assert(GenModules.count(SF) == 0);
  GenModules[SF] = IGM;
  if (!PrimaryIGM) {
    PrimaryIGM = IGM;
  }
  Queue.push_back(IGM);
}

IRGenModule *IRGenerator::getGenModule(DeclContext *ctxt) {
  if (GenModules.size() == 1 || !ctxt) {
    return getPrimaryIGM();
  }
  SourceFile *SF = ctxt->getParentSourceFile();
  if (!SF) {
    return getPrimaryIGM();
  }
  IRGenModule *IGM = GenModules[SF];
  assert(IGM);
  return IGM;
}

IRGenModule *IRGenerator::getGenModule(SILFunction *f) {
  if (GenModules.size() == 1) {
    return getPrimaryIGM();
  }

  if (DeclContext *ctxt = f->getDeclContext()) {
    if (SourceFile *SF = ctxt->getParentSourceFile()) {
      IRGenModule *IGM = GenModules[SF];
      assert(IGM);
      return IGM;
    }
  }
  // We have no source file for the function.
  // Let's use the IGM from which the function is referenced the first time.
  if (IRGenModule *IGM = DefaultIGMForFunction[f])
    return IGM;

  return getPrimaryIGM();
}
