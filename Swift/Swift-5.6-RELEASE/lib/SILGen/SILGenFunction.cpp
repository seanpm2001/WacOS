//===--- SILGenFunction.cpp - Top-level lowering for functions ------------===//
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
//  This file defines the primary routines for creating and emitting
//  functions.
//
//===----------------------------------------------------------------------===//

#include "SILGenFunction.h"
#include "RValue.h"
#include "SILGenFunctionBuilder.h"
#include "Scope.h"
#include "swift/ABI/MetadataValues.h"
#include "swift/AST/ClangModuleLoader.h"
#include "swift/AST/DiagnosticsSIL.h"
#include "swift/AST/FileUnit.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Initializer.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/PropertyWrappers.h"
#include "swift/AST/SourceFile.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/SILProfiler.h"
#include "swift/SIL/SILUndef.h"

using namespace swift;
using namespace Lowering;

//===----------------------------------------------------------------------===//
// SILGenFunction Class implementation
//===----------------------------------------------------------------------===//

SILGenFunction::SILGenFunction(SILGenModule &SGM, SILFunction &F,
                               DeclContext *DC)
    : SGM(SGM), F(F), silConv(SGM.M), FunctionDC(DC),
      StartOfPostmatter(F.end()), B(*this),
      CurrentSILLoc(F.getLocation()), Cleanups(*this),
      StatsTracer(SGM.M.getASTContext().Stats,
                  "SILGen-function", &F) {
  assert(DC && "creating SGF without a DeclContext?");
  B.setInsertionPoint(createBasicBlock());
  B.setCurrentDebugScope(F.getDebugScope());
}

/// SILGenFunction destructor - called after the entire function's AST has been
/// visited.  This handles "falling off the end of the function" logic.
SILGenFunction::~SILGenFunction() {
  // If the end of the function isn't terminated, we screwed up somewhere.
  assert(!B.hasValidInsertionPoint() &&
         "SILGenFunction did not terminate function?!");

  // If we didn't clean up the rethrow destination, we screwed up somewhere.
  assert(!ThrowDest.isValid() &&
         "SILGenFunction did not emit throw destination");
}

//===----------------------------------------------------------------------===//
// Function emission
//===----------------------------------------------------------------------===//

// Get the #function name for a declaration.
DeclName SILGenModule::getMagicFunctionName(DeclContext *dc) {
  // For closures, use the parent name.
  if (auto closure = dyn_cast<AbstractClosureExpr>(dc)) {
    return getMagicFunctionName(closure->getParent());
  }
  if (auto absFunc = dyn_cast<AbstractFunctionDecl>(dc)) {
    // If this is an accessor, use the name of the storage.
    if (auto accessor = dyn_cast<AccessorDecl>(absFunc))
      return accessor->getStorage()->getName();
    if (auto func = dyn_cast<FuncDecl>(absFunc)) {
      // If this is a defer body, use the parent name.
      if (func->isDeferBody()) {
        return getMagicFunctionName(func->getParent());
      }
    }

    return absFunc->getName();
  }
  if (auto init = dyn_cast<Initializer>(dc)) {
    return getMagicFunctionName(init->getParent());
  }
  if (auto nominal = dyn_cast<NominalTypeDecl>(dc)) {
    return nominal->getName();
  }
  if (auto tl = dyn_cast<TopLevelCodeDecl>(dc)) {
    return tl->getModuleContext()->getName();
  }
  if (auto fu = dyn_cast<FileUnit>(dc)) {
    return fu->getParentModule()->getName();
  }
  if (auto m = dyn_cast<ModuleDecl>(dc)) {
    return m->getName();
  }
  if (auto e = dyn_cast<ExtensionDecl>(dc)) {
    assert(e->getExtendedNominal() && "extension for nonnominal");
    return e->getExtendedNominal()->getName();
  }
  if (auto EED = dyn_cast<EnumElementDecl>(dc)) {
    return EED->getName();
  }
  if (auto SD = dyn_cast<SubscriptDecl>(dc)) {
    return SD->getName();
  }
  llvm_unreachable("unexpected #function context");
}

DeclName SILGenModule::getMagicFunctionName(SILDeclRef ref) {
  switch (ref.kind) {
  case SILDeclRef::Kind::Func:
    if (auto closure = ref.getAbstractClosureExpr())
      return getMagicFunctionName(closure);
    return getMagicFunctionName(cast<FuncDecl>(ref.getDecl()));
  case SILDeclRef::Kind::Initializer:
  case SILDeclRef::Kind::Allocator:
    return getMagicFunctionName(cast<ConstructorDecl>(ref.getDecl()));
  case SILDeclRef::Kind::Deallocator:
  case SILDeclRef::Kind::Destroyer:
    return getMagicFunctionName(cast<DestructorDecl>(ref.getDecl()));
  case SILDeclRef::Kind::GlobalAccessor:
    return getMagicFunctionName(cast<VarDecl>(ref.getDecl())->getDeclContext());
  case SILDeclRef::Kind::DefaultArgGenerator:
    return getMagicFunctionName(cast<DeclContext>(ref.getDecl()));
  case SILDeclRef::Kind::StoredPropertyInitializer:
  case SILDeclRef::Kind::PropertyWrapperBackingInitializer:
    return getMagicFunctionName(cast<VarDecl>(ref.getDecl())->getDeclContext());
  case SILDeclRef::Kind::PropertyWrapperInitFromProjectedValue:
    return getMagicFunctionName(cast<VarDecl>(ref.getDecl())->getDeclContext());
  case SILDeclRef::Kind::IVarInitializer:
    return getMagicFunctionName(cast<ClassDecl>(ref.getDecl()));
  case SILDeclRef::Kind::IVarDestroyer:
    return getMagicFunctionName(cast<ClassDecl>(ref.getDecl()));
  case SILDeclRef::Kind::EnumElement:
    return getMagicFunctionName(cast<EnumElementDecl>(ref.getDecl())
                                  ->getDeclContext());
  case SILDeclRef::Kind::AsyncEntryPoint:
  case SILDeclRef::Kind::EntryPoint:
    auto *file = ref.getDecl()->getDeclContext()->getParentSourceFile();
    return getMagicFunctionName(file);
  }

  llvm_unreachable("Unhandled SILDeclRefKind in switch.");
}

std::tuple<ManagedValue, SILType>
SILGenFunction::emitSiblingMethodRef(SILLocation loc,
                                     SILValue selfValue,
                                     SILDeclRef methodConstant,
                                     SubstitutionMap subMap) {
  SILValue methodValue;

  // If the method is dynamic, access it through runtime-hookable virtual
  // dispatch (viz. objc_msgSend for now).
  if (methodConstant.hasDecl()
      && methodConstant.getDecl()->shouldUseObjCDispatch()) {
    methodValue =
        emitDynamicMethodRef(
            loc, methodConstant,
            SGM.Types.getConstantInfo(getTypeExpansionContext(), methodConstant)
                .SILFnType)
            .getValue();
  } else {
    methodValue = emitGlobalFunctionRef(loc, methodConstant);
  }

  SILType methodTy = methodValue->getType();

  // Specialize the generic method.
  methodTy =
      methodTy.substGenericArgs(SGM.M, subMap, getTypeExpansionContext());

  return std::make_tuple(ManagedValue::forUnmanaged(methodValue),
                         methodTy);
}

void SILGenFunction::emitCaptures(SILLocation loc,
                                  SILDeclRef closure,
                                  CaptureEmission purpose,
                                  SmallVectorImpl<ManagedValue> &capturedArgs) {
  auto captureInfo = SGM.Types.getLoweredLocalCaptures(closure);
  // For boxed captures, we need to mark the contained variables as having
  // escaped for DI diagnostics.
  SmallVector<SILValue, 2> escapesToMark;
  
  // Partial applications take ownership of the context parameters, so we'll
  // need to pass ownership rather than merely guaranteeing parameters.
  bool canGuarantee;
  bool captureCanEscape = true;
  switch (purpose) {
  case CaptureEmission::PartialApplication:
    canGuarantee = false;
    break;
  case CaptureEmission::ImmediateApplication:
    canGuarantee = true;
    break;
  case CaptureEmission::AssignByWrapper:
    canGuarantee = false;
    captureCanEscape = false;
    break;
  }

  auto expansion = getTypeExpansionContext();

  for (auto capture : captureInfo.getCaptures()) {
    if (capture.isDynamicSelfMetadata()) {
      // The parameter type is the static Self type, but the value we
      // want to pass is the dynamic Self type, so upcast it.
      auto dynamicSelfMetatype = MetatypeType::get(
        captureInfo.getDynamicSelfType());
      SILType dynamicSILType = getLoweredType(dynamicSelfMetatype);

      SILValue value = B.createMetatype(loc, dynamicSILType);
      capturedArgs.push_back(ManagedValue::forUnmanaged(value));
      continue;
    }

    if (capture.isOpaqueValue()) {
      OpaqueValueExpr *opaqueValue = capture.getOpaqueValue();
      capturedArgs.push_back(
          emitRValueAsSingleValue(opaqueValue).ensurePlusOne(*this, loc));
      continue;
    }

    auto *vd = cast<VarDecl>(capture.getDecl());
    auto type = FunctionDC->mapTypeIntoContext(
      vd->getInterfaceType());
    auto valueType = FunctionDC->mapTypeIntoContext(
      vd->getValueInterfaceType());

    //
    // If we haven't emitted the captured value yet, we're forming a closure
    // to a local function before all of its captures have been emitted. Eg,
    //
    // func f() { g() } // transitive capture of 'x'
    // f() // closure formed here
    // var x = 123 // 'x' defined here
    // func g() { print(x) } // 'x' captured here
    //
    auto found = VarLocs.find(vd);
    if (found == VarLocs.end()) {
      auto &Diags = getASTContext().Diags;

      SourceLoc loc;
      if (closure.kind == SILDeclRef::Kind::DefaultArgGenerator) {
        auto *param = getParameterAt(closure.getDecl(),
                                     closure.defaultArgIndex);
        loc = param->getLoc();
      } else {
        auto f = *closure.getAnyFunctionRef();
        loc = f.getLoc();
      }

      Diags.diagnose(loc, diag::capture_before_declaration,
                     vd->getBaseIdentifier());
      Diags.diagnose(vd->getLoc(), diag::captured_value_declared_here);
      Diags.diagnose(capture.getLoc(), diag::value_captured_here);

      // Emit an 'undef' of the correct type.
      switch (SGM.Types.getDeclCaptureKind(capture, expansion)) {
      case CaptureKind::Constant:
        capturedArgs.push_back(emitUndef(getLoweredType(type)));
        break;
      case CaptureKind::Immutable:
      case CaptureKind::StorageAddress:
        capturedArgs.push_back(emitUndef(getLoweredType(type).getAddressType()));
        break;
      case CaptureKind::Box: {
        auto boxTy = SGM.Types.getContextBoxTypeForCapture(
            vd,
            SGM.Types.getLoweredRValueType(TypeExpansionContext::minimal(),
                                           type),
            FunctionDC->getGenericEnvironmentOfContext(),
            /*mutable*/ true);
        capturedArgs.push_back(emitUndef(boxTy));
        break;
      }
      }
      continue;
    }

    // Get an address value for a SILValue if it is address only in an type
    // expansion context without opaque archetype substitution.
    auto getAddressValue = [&](SILValue entryValue) -> SILValue {
      if (SGM.Types
              .getTypeLowering(
                  valueType,
                  TypeExpansionContext::noOpaqueTypeArchetypesSubstitution(
                      expansion.getResilienceExpansion()))
              .isAddressOnly() &&
          !entryValue->getType().isAddress()) {

        auto addr = emitTemporaryAllocation(vd, entryValue->getType());
        auto val = B.emitCopyValueOperation(vd, entryValue);
        auto &lowering = getTypeLowering(entryValue->getType());
        lowering.emitStore(B, vd, val, addr, StoreOwnershipQualifier::Init);
        entryValue = addr;
        enterDestroyCleanup(addr);
      }
      return entryValue;
    };

    auto Entry = found->second;
    switch (SGM.Types.getDeclCaptureKind(capture, expansion)) {
    case CaptureKind::Constant: {
      // let declarations.
      auto &tl = getTypeLowering(valueType);
      SILValue Val = Entry.value;

      if (!Val->getType().isAddress()) {
        // Our 'let' binding can guarantee the lifetime for the callee,
        // if we don't need to do anything more to it.
        if (canGuarantee && !vd->getInterfaceType()->is<ReferenceStorageType>()) {
          auto guaranteed = ManagedValue::forUnmanaged(Val).borrow(*this, loc);
          capturedArgs.push_back(guaranteed);
          break;
        }
      
        // Just retain a by-val let.
        Val = B.emitCopyValueOperation(loc, Val);
      } else {
        // If we have a mutable binding for a 'let', such as 'self' in an
        // 'init' method, load it.
        Val = emitLoad(loc, Val, tl, SGFContext(), IsNotTake).forward(*this);
      }

      // If we're capturing an unowned pointer by value, we will have just
      // loaded it into a normal retained class pointer, but we capture it as
      // an unowned pointer.  Convert back now.
      if (vd->getInterfaceType()->is<ReferenceStorageType>())
        Val = emitConversionFromSemanticValue(loc, Val, getLoweredType(type));

      capturedArgs.push_back(emitManagedRValueWithCleanup(Val));
      break;
    }
    case CaptureKind::Immutable: {
      if (canGuarantee) {
        auto entryValue = getAddressValue(Entry.value);
        // No-escaping stored declarations are captured as the
        // address of the value.
        assert(entryValue->getType().isAddress() && "no address for captured var!");
        capturedArgs.push_back(ManagedValue::forLValue(entryValue));
      }
      else {
        auto entryValue = getAddressValue(Entry.value);
        // We cannot pass a valid SILDebugVariable while creating the temp here
        // See rdar://60425582
        auto addr = B.createAllocStack(loc, entryValue->getType().getObjectType());
        enterDeallocStackCleanup(addr);
        B.createCopyAddr(loc, entryValue, addr, IsNotTake, IsInitialization);
        capturedArgs.push_back(ManagedValue::forLValue(addr));
      }
      break;
    }
    case CaptureKind::StorageAddress: {
      auto entryValue = getAddressValue(Entry.value);
      // No-escaping stored declarations are captured as the
      // address of the value.
      assert(entryValue->getType().isAddress() && "no address for captured var!");
      capturedArgs.push_back(ManagedValue::forLValue(entryValue));
      break;
    }

    case CaptureKind::Box: {
      auto entryValue = getAddressValue(Entry.value);
      // LValues are captured as both the box owning the value and the
      // address of the value.
      assert(entryValue->getType().isAddress() && "no address for captured var!");
      // Boxes of opaque return values stay opaque.
      auto minimalLoweredType = SGM.Types.getLoweredRValueType(
          TypeExpansionContext::minimal(), type->getCanonicalType());
      // If this is a boxed variable, we can use it directly.
      if (Entry.box &&
          entryValue->getType().getASTType() == minimalLoweredType) {
        // We can guarantee our own box to the callee.
        if (canGuarantee) {
          capturedArgs.push_back(
              ManagedValue::forUnmanaged(Entry.box).borrow(*this, loc));
        } else {
          capturedArgs.push_back(emitManagedRetain(loc, Entry.box));
        }
        if (captureCanEscape)
          escapesToMark.push_back(entryValue);
      } else {
        // Address only 'let' values are passed by box.  This isn't great, in
        // that a variable captured by multiple closures will be boxed for each
        // one.  This could be improved by doing an "isCaptured" analysis when
        // emitting address-only let constants, and emit them into an alloc_box
        // like a variable instead of into an alloc_stack.
        //
        // TODO: This might not be profitable anymore with guaranteed captures,
        // since we could conceivably forward the copied value into the
        // closure context and pass it down to the partially applied function
        // in-place.
        // TODO: Use immutable box for immutable captures.
        auto boxTy = SGM.Types.getContextBoxTypeForCapture(
            vd, minimalLoweredType, FunctionDC->getGenericEnvironmentOfContext(),
            /*mutable*/ true);

        AllocBoxInst *allocBox = B.createAllocBox(loc, boxTy);
        ProjectBoxInst *boxAddress = B.createProjectBox(loc, allocBox, 0);
        B.createCopyAddr(loc, entryValue, boxAddress, IsNotTake,
                         IsInitialization);
        if (canGuarantee)
          capturedArgs.push_back(
              emitManagedRValueWithCleanup(allocBox).borrow(*this, loc));
        else
          capturedArgs.push_back(emitManagedRValueWithCleanup(allocBox));
      }

      break;
    }
    }
  }
  
  // Mark box addresses as captured for DI purposes. The values must have
  // been fully initialized before we close over them.
  if (!escapesToMark.empty()) {
    B.createMarkFunctionEscape(loc, escapesToMark);
  }
}

ManagedValue
SILGenFunction::emitClosureValue(SILLocation loc, SILDeclRef constant,
                                 CanType expectedType,
                                 SubstitutionMap subs,
                                 bool alreadyConverted) {
  auto loweredCaptureInfo = SGM.Types.getLoweredLocalCaptures(constant);

  auto constantInfo = getConstantInfo(getTypeExpansionContext(), constant);
  SILValue functionRef = emitGlobalFunctionRef(loc, constant, constantInfo);
  SILType functionTy = functionRef->getType();

  // Apply substitutions.
  auto pft = constantInfo.SILFnType;

  auto closure = *constant.getAnyFunctionRef();
  auto *dc = closure.getAsDeclContext()->getParent();
  if (dc->isLocalContext() && !loweredCaptureInfo.hasGenericParamCaptures()) {
    // If the lowered function type is not polymorphic but we were given
    // substitutions, we have a closure in a generic context which does not
    // capture generic parameters. Just drop the substitutions.
    subs = { };
  } else if (closure.getAbstractClosureExpr()) {
    // If we have a closure expression in generic context, Sema won't give
    // us substitutions, so we just use the forwarding substitutions from
    // context.
    subs = getForwardingSubstitutionMap();
  }

  bool wasSpecialized = false;
  if (!subs.empty()) {
    auto specialized =
        pft->substGenericArgs(F.getModule(), subs, getTypeExpansionContext());
    functionTy = SILType::getPrimitiveObjectType(specialized);
    wasSpecialized = true;
  }

  // If we're in top-level code, we don't need to physically capture script
  // globals, but we still need to mark them as escaping so that DI can flag
  // uninitialized uses.
  if (this == SGM.TopLevelSGF) {
    auto captureInfo = closure.getCaptureInfo();
    SGM.emitMarkFunctionEscapeForTopLevelCodeGlobals(
        loc, captureInfo);
  }
  
  if (loweredCaptureInfo.getCaptures().empty() && !wasSpecialized) {
    auto result = ManagedValue::forUnmanaged(functionRef);
    if (!alreadyConverted)
      result = emitOrigToSubstValue(loc, result,
                                    AbstractionPattern(expectedType),
                                    expectedType);
    return result;
  }

  SmallVector<ManagedValue, 4> capturedArgs;
  emitCaptures(loc, constant, CaptureEmission::PartialApplication,
               capturedArgs);

  // The partial application takes ownership of the context parameters.
  SmallVector<SILValue, 4> forwardedArgs;
  for (auto capture : capturedArgs)
    forwardedArgs.push_back(capture.forward(*this));

  auto calleeConvention = ParameterConvention::Direct_Guaranteed;

  auto toClosure =
    B.createPartialApply(loc, functionRef, subs, forwardedArgs,
                         calleeConvention);
  auto result = emitManagedRValueWithCleanup(toClosure);

  // Get the lowered AST types:
  //  - the original type
  auto origFormalType = AbstractionPattern(constantInfo.LoweredType);

  // - the substituted type
  auto substFormalType = expectedType;

  // Generalize if necessary.
  if (!alreadyConverted)
    result = emitOrigToSubstValue(loc, result, origFormalType,
                                  substFormalType);

  return result;
}

void SILGenFunction::emitFunction(FuncDecl *fd) {
  MagicFunctionName = SILGenModule::getMagicFunctionName(fd);

  auto captureInfo = SGM.M.Types.getLoweredLocalCaptures(SILDeclRef(fd));
  emitProfilerIncrement(fd->getTypecheckedBody());
  emitProlog(captureInfo, fd->getParameters(), fd->getImplicitSelfDecl(), fd,
             fd->getResultInterfaceType(), fd->hasThrows(), fd->getThrowsLoc());

  if (fd->isDistributedActorFactory()) {
    // Synthesize the factory function body
    emitDistributedActorFactory(fd);
  } else {
    prepareEpilog(fd->getResultInterfaceType(),
                  fd->hasThrows(), CleanupLocation(fd));
    
    // Emit the actual function body as usual
    emitStmt(fd->getTypecheckedBody());

    emitEpilog(fd);
  }

  mergeCleanupBlocks();
}

void SILGenFunction::emitClosure(AbstractClosureExpr *ace) {
  MagicFunctionName = SILGenModule::getMagicFunctionName(ace);
  OrigFnType = SGM.M.Types.getConstantAbstractionPattern(SILDeclRef(ace));
  
  auto resultIfaceTy = ace->getResultType()->mapTypeOutOfContext();
  auto captureInfo = SGM.M.Types.getLoweredLocalCaptures(
    SILDeclRef(ace));
  emitProfilerIncrement(ace);
  emitProlog(captureInfo, ace->getParameters(), /*selfParam=*/nullptr,
             ace, resultIfaceTy, ace->isBodyThrowing(), ace->getLoc(),
             SGM.M.Types.getConstantAbstractionPattern(SILDeclRef(ace)));
  prepareEpilog(resultIfaceTy, ace->isBodyThrowing(), CleanupLocation(ace));

  if (auto *ce = dyn_cast<ClosureExpr>(ace)) {
    emitStmt(ce->getBody());
  } else {
    auto *autoclosure = cast<AutoClosureExpr>(ace);
    // Closure expressions implicitly return the result of their body
    // expression.
    if (B.hasValidInsertionPoint()) {
      emitReturnExpr(ImplicitReturnLocation(ace),
                     autoclosure->getSingleExpressionBody());
    }
  }
  emitEpilog(ace);
}

ManagedValue emitBuiltinCreateAsyncTask(SILGenFunction &SGF, SILLocation loc,
                                        SubstitutionMap subs,
                                        ArrayRef<ManagedValue> args,
                                        SGFContext C);

void SILGenFunction::emitArtificialTopLevel(Decl *mainDecl) {
  // Create the argc and argv arguments.
  auto entry = B.getInsertionBB();
  auto paramTypeIter = F.getConventions()
                           .getParameterSILTypes(getTypeExpansionContext())
                           .begin();

  SILValue argc;
  SILValue argv;
  const bool isAsyncFunc =
      isa<FuncDecl>(mainDecl) && static_cast<FuncDecl *>(mainDecl)->hasAsync();
  if (!isAsyncFunc) {
    argc = entry->createFunctionArgument(*paramTypeIter);
    argv = entry->createFunctionArgument(*std::next(paramTypeIter));
  }

  switch (mainDecl->getArtificialMainKind()) {
  case ArtificialMainKind::UIApplicationMain: {
    // Emit a UIKit main.
    // return UIApplicationMain(C_ARGC, C_ARGV, nil, ClassName);

    auto *mainClass = cast<NominalTypeDecl>(mainDecl);

    CanType NSStringTy = SGM.Types.getNSStringType();
    CanType OptNSStringTy
      = OptionalType::get(NSStringTy)->getCanonicalType();

    // Look up UIApplicationMain.
    // FIXME: Doing an AST lookup here is gross and not entirely sound;
    // we're getting away with it because the types are guaranteed to already
    // be imported.
    ASTContext &ctx = getASTContext();
    
    ImportPath::Element UIKitName =
      {ctx.getIdentifier("UIKit"), SourceLoc()};
    
    ModuleDecl *UIKit = ctx
      .getClangModuleLoader()
      ->loadModule(SourceLoc(),
                   ImportPath::Module(llvm::makeArrayRef(UIKitName)));
    assert(UIKit && "couldn't find UIKit objc module?!");
    SmallVector<ValueDecl *, 1> results;
    UIKit->lookupQualified(UIKit,
                           DeclNameRef(ctx.getIdentifier("UIApplicationMain")),
                           NL_QualifiedDefault,
                           results);
    assert(results.size() == 1
           && "couldn't find a unique UIApplicationMain in the UIKit ObjC "
              "module?!");

    ValueDecl *UIApplicationMainDecl = results.front();

    auto mainRef = SILDeclRef(UIApplicationMainDecl).asForeign();
    SILGenFunctionBuilder builder(SGM);
    auto UIApplicationMainFn =
        builder.getOrCreateFunction(mainClass, mainRef, NotForDefinition);
    auto fnTy = UIApplicationMainFn->getLoweredFunctionType();
    SILFunctionConventions fnConv(fnTy, SGM.M);

    // Get the class name as a string using NSStringFromClass.
    CanType mainClassTy = mainClass->getDeclaredInterfaceType()
        ->getCanonicalType();
    CanType mainClassMetaty = CanMetatypeType::get(mainClassTy,
                                                   MetatypeRepresentation::ObjC);
    CanType anyObjectTy = ctx.getAnyObjectType();
    CanType anyObjectMetaTy = CanExistentialMetatypeType::get(anyObjectTy,
                                                  MetatypeRepresentation::ObjC);

    auto paramConvention = ParameterConvention::Direct_Unowned;
    auto params = {SILParameterInfo(anyObjectMetaTy, paramConvention)};
    std::array<SILResultInfo, 1> resultInfos = {
        SILResultInfo(OptNSStringTy, ResultConvention::Autoreleased)};
    auto repr = SILFunctionType::Representation::CFunctionPointer;
    auto *clangFnType =
        ctx.getCanonicalClangFunctionType(params, resultInfos[0], repr);
    auto extInfo = SILFunctionType::ExtInfoBuilder()
                       .withRepresentation(repr)
                       .withClangFunctionType(clangFnType)
                       .build();

    auto NSStringFromClassType = SILFunctionType::get(
        nullptr, extInfo, SILCoroutineKind::None, paramConvention, params,
        /*yields*/ {}, resultInfos, /*error result*/ None, SubstitutionMap(),
        SubstitutionMap(), ctx);

    auto NSStringFromClassFn = builder.getOrCreateFunction(
        mainClass, "NSStringFromClass", SILLinkage::PublicExternal,
        NSStringFromClassType, IsBare, IsTransparent, IsNotSerialized,
        IsNotDynamic);
    auto NSStringFromClass = B.createFunctionRef(mainClass, NSStringFromClassFn);
    SILValue metaTy = B.createMetatype(mainClass,
                             SILType::getPrimitiveObjectType(mainClassMetaty));
    metaTy = B.createInitExistentialMetatype(mainClass, metaTy,
                          SILType::getPrimitiveObjectType(anyObjectMetaTy),
                          {});
    SILValue optNameValue = B.createApply(
        mainClass, NSStringFromClass, {}, metaTy);
    ManagedValue optName = emitManagedRValueWithCleanup(optNameValue);

    // Fix up the string parameters to have the right type.
    SILType nameArgTy =
        fnConv.getSILArgumentType(3, B.getTypeExpansionContext());
    assert(nameArgTy ==
           fnConv.getSILArgumentType(2, B.getTypeExpansionContext()));
    (void)nameArgTy;
    assert(optName.getType() == nameArgTy);
    SILValue nilValue =
        getOptionalNoneValue(mainClass, getTypeLowering(OptNSStringTy));

    // Fix up argv to have the right type.
    auto argvTy = fnConv.getSILArgumentType(1, B.getTypeExpansionContext());

    SILType unwrappedTy = argvTy;
    if (Type innerTy = argvTy.getASTType()->getOptionalObjectType()) {
      auto canInnerTy = innerTy->getCanonicalType();
      unwrappedTy = SILType::getPrimitiveObjectType(canInnerTy);
    }

    auto managedArgv = ManagedValue::forUnmanaged(argv);

    if (unwrappedTy != argv->getType()) {
      auto converted =
          emitPointerToPointer(mainClass, managedArgv,
                               argv->getType().getASTType(),
                               unwrappedTy.getASTType());
      managedArgv = std::move(converted).getAsSingleValue(*this, mainClass);
    }

    if (unwrappedTy != argvTy) {
      managedArgv = getOptionalSomeValue(mainClass, managedArgv,
                                         getTypeLowering(argvTy));
    }

    auto UIApplicationMain = B.createFunctionRef(mainClass, UIApplicationMainFn);

    SILValue args[] = {argc, managedArgv.getValue(), nilValue,
                       optName.getValue()};

    B.createApply(mainClass, UIApplicationMain, SubstitutionMap(), args);
    SILValue r = B.createIntegerLiteral(mainClass,
                        SILType::getBuiltinIntegerType(32, ctx), 0);
    auto rType =
        F.getConventions().getSingleSILResultType(B.getTypeExpansionContext());
    if (r->getType() != rType)
      r = B.createStruct(mainClass, rType, r);

    Cleanups.emitCleanupsForReturn(mainClass, NotForUnwind);
    B.createReturn(mainClass, r);
    return;
  }

  case ArtificialMainKind::NSApplicationMain: {
    // Emit an AppKit main.
    // return NSApplicationMain(C_ARGC, C_ARGV);

    auto *mainClass = cast<NominalTypeDecl>(mainDecl);

    SILParameterInfo argTypes[] = {
      SILParameterInfo(argc->getType().getASTType(),
                       ParameterConvention::Direct_Unowned),
      SILParameterInfo(argv->getType().getASTType(),
                       ParameterConvention::Direct_Unowned),
    };
    auto NSApplicationMainType = SILFunctionType::get(
        nullptr,
        // Should be C calling convention, but NSApplicationMain
        // has an overlay to fix the type of argv.
        SILFunctionType::ExtInfo::getThin(), SILCoroutineKind::None,
        ParameterConvention::Direct_Unowned, argTypes,
        /*yields*/ {},
        SILResultInfo(argc->getType().getASTType(), ResultConvention::Unowned),
        /*error result*/ None, SubstitutionMap(), SubstitutionMap(),
        getASTContext());

    SILGenFunctionBuilder builder(SGM);
    auto NSApplicationMainFn = builder.getOrCreateFunction(
        mainClass, "NSApplicationMain", SILLinkage::PublicExternal,
        NSApplicationMainType, IsBare, IsTransparent, IsNotSerialized,
        IsNotDynamic);

    auto NSApplicationMain = B.createFunctionRef(mainClass, NSApplicationMainFn);
    SILValue args[] = { argc, argv };

    B.createApply(mainClass, NSApplicationMain, SubstitutionMap(), args);
    SILValue r = B.createIntegerLiteral(mainClass,
                        SILType::getBuiltinIntegerType(32, getASTContext()), 0);
    auto rType =
        F.getConventions().getSingleSILResultType(B.getTypeExpansionContext());
    if (r->getType() != rType)
      r = B.createStruct(mainClass, rType, r);
    B.createReturn(mainClass, r);
    return;
  }

  case ArtificialMainKind::TypeMain: {
    // Emit a call to the main static function.
    // return Module.$main();
    auto *mainFunc = cast<FuncDecl>(mainDecl);
    auto moduleLoc = RegularLocation::getModuleLocation();
    auto *entryBlock = B.getInsertionBB();

    SILDeclRef mainFunctionDeclRef(mainFunc, SILDeclRef::Kind::Func);
    SILFunction *mainFunction =
        SGM.getFunction(mainFunctionDeclRef, NotForDefinition);

    ExtensionDecl *mainExtension =
        dyn_cast<ExtensionDecl>(mainFunc->getDeclContext());

    NominalTypeDecl *mainType;
    if (mainExtension) {
      mainType = mainExtension->getExtendedNominal();
    } else {
      mainType = cast<NominalTypeDecl>(mainFunc->getDeclContext());
    }
    auto metatype = B.createMetatype(mainType, getLoweredType(mainType->getInterfaceType()));

    auto mainFunctionRef = B.createFunctionRef(moduleLoc, mainFunction);

    auto builtinInt32Type = SILType::getBuiltinIntegerType(32, getASTContext());

    auto *exitBlock = createBasicBlock();
    SILValue exitCode =
        exitBlock->createPhiArgument(builtinInt32Type, OwnershipKind::None);
    B.setInsertionPoint(exitBlock);

    if (!mainFunc->hasAsync()) {
      auto returnType = F.getConventions().getSingleSILResultType(
          B.getTypeExpansionContext());
      if (exitCode->getType() != returnType)
        exitCode = B.createStruct(moduleLoc, returnType, exitCode);
      B.createReturn(moduleLoc, exitCode);
    } else {
      FuncDecl *exitFuncDecl = SGM.getExit();
      assert(exitFuncDecl && "Failed to find exit function declaration");
      SILFunction *exitSILFunc = SGM.getFunction(
          SILDeclRef(exitFuncDecl, SILDeclRef::Kind::Func, /*isForeign*/ true),
          NotForDefinition);

      SILFunctionType &funcType =
          *exitSILFunc->getLoweredType().getAs<SILFunctionType>();
      SILType retType = SILType::getPrimitiveObjectType(
          funcType.getParameters().front().getInterfaceType());
      exitCode = B.createStruct(moduleLoc, retType, exitCode);
      SILValue exitCall = B.createFunctionRef(moduleLoc, exitSILFunc);
      B.createApply(moduleLoc, exitCall, {}, {exitCode});
      B.createUnreachable(moduleLoc);
    }

    if (mainFunc->hasThrows()) {
      auto *successBlock = createBasicBlock();
      B.setInsertionPoint(successBlock);
      successBlock->createPhiArgument(SGM.Types.getEmptyTupleType(),
                                      OwnershipKind::None);
      SILValue zeroReturnValue =
          B.createIntegerLiteral(moduleLoc, builtinInt32Type, 0);
      B.createBranch(moduleLoc, exitBlock, {zeroReturnValue});

      auto *failureBlock = createBasicBlock();
      B.setInsertionPoint(failureBlock);
      SILValue error = failureBlock->createPhiArgument(
          SILType::getExceptionType(getASTContext()), OwnershipKind::Owned);
      // Log the error.
      B.createBuiltin(moduleLoc, getASTContext().getIdentifier("errorInMain"),
                      SGM.Types.getEmptyTupleType(), {}, {error});
      B.createEndLifetime(moduleLoc, error);
      SILValue oneReturnValue =
          B.createIntegerLiteral(moduleLoc, builtinInt32Type, 1);
      B.createBranch(moduleLoc, exitBlock, {oneReturnValue});

      B.setInsertionPoint(entryBlock);
      B.createTryApply(moduleLoc, mainFunctionRef, SubstitutionMap(),
                       {metatype}, successBlock, failureBlock);
    } else {
      B.setInsertionPoint(entryBlock);
      B.createApply(moduleLoc, mainFunctionRef, SubstitutionMap(), {metatype});
      SILValue returnValue =
          B.createIntegerLiteral(moduleLoc, builtinInt32Type, 0);
      B.createBranch(moduleLoc, exitBlock, {returnValue});
    }
    return;
  }
  }
}

void SILGenFunction::emitAsyncMainThreadStart(SILDeclRef entryPoint) {
  auto moduleLoc = RegularLocation::getModuleLocation();
  auto *entryBlock = B.getInsertionBB();
  auto paramTypeIter = F.getConventions()
                           .getParameterSILTypes(getTypeExpansionContext())
                           .begin();

  entryBlock->createFunctionArgument(*paramTypeIter);            // argc
  entryBlock->createFunctionArgument(*std::next(paramTypeIter)); // argv

  // Lookup necessary functions
  swift::ASTContext &ctx = entryPoint.getDecl()->getASTContext();

  B.setInsertionPoint(entryBlock);

  auto wrapCallArgs = [this, &moduleLoc](SILValue originalValue, FuncDecl *fd,
                            uint32_t paramIndex) -> SILValue {
    Type parameterType = fd->getParameters()->get(paramIndex)->getType();
    SILType paramSILType = SILType::getPrimitiveObjectType(parameterType->getCanonicalType());
    // If the types are the same, we don't need to do anything!
    if (paramSILType == originalValue->getType())
      return originalValue;
    return this->B.createStruct(moduleLoc, paramSILType, originalValue);
  };

  // Call CreateAsyncTask
  FuncDecl *builtinDecl = cast<FuncDecl>(getBuiltinValueDecl(
      getASTContext(),
      ctx.getIdentifier(getBuiltinName(BuiltinValueKind::CreateAsyncTask))));
  auto subs = SubstitutionMap::get(builtinDecl->getGenericSignature(),
                                   {TupleType::getEmpty(ctx)},
                                   ArrayRef<ProtocolConformanceRef>{});

  SILValue mainFunctionRef = emitGlobalFunctionRef(moduleLoc, entryPoint);

  // Emit the CreateAsyncTask builtin
  TaskCreateFlags taskCreationFlagMask(0);
  taskCreationFlagMask.setInheritContext(true);
  SILValue taskFlags =
      emitWrapIntegerLiteral(moduleLoc, getLoweredType(ctx.getIntType()),
                             taskCreationFlagMask.getOpaqueValue());

  SILValue task =
      emitBuiltinCreateAsyncTask(*this, moduleLoc, subs,
                                 {ManagedValue::forUnmanaged(taskFlags),
                                  ManagedValue::forUnmanaged(mainFunctionRef)},
                                 {})
          .forward(*this);
  DestructureTupleInst *structure = B.createDestructureTuple(moduleLoc, task);
  task = structure->getResult(0);

  // Get swiftJobRun
  FuncDecl *swiftJobRunFuncDecl = SGM.getSwiftJobRun();
  assert(swiftJobRunFuncDecl && "Failed to find swift_job_run function decl");
  SILFunction *swiftJobRunSILFunc =
      SGM.getFunction(SILDeclRef(swiftJobRunFuncDecl, SILDeclRef::Kind::Func),
                      NotForDefinition);
  SILValue swiftJobRunFunc =
      B.createFunctionRefFor(moduleLoc, swiftJobRunSILFunc);

  // Convert task to job
  SILType JobType = SILType::getPrimitiveObjectType(
      getBuiltinType(ctx, "Job")->getCanonicalType());
  SILValue jobResult = B.createBuiltin(
      moduleLoc,
      ctx.getIdentifier(getBuiltinName(BuiltinValueKind::ConvertTaskToJob)),
      JobType, {}, {task});
  jobResult = wrapCallArgs(jobResult, swiftJobRunFuncDecl, 0);

  // Get main executor
  FuncDecl *getMainExecutorFuncDecl = SGM.getGetMainExecutor();
  if (!getMainExecutorFuncDecl) {
    // If it doesn't exist due to an SDK-compiler mismatch, we can conjure one
    // up instead of crashing:
    // @available(SwiftStdlib 5.1, *)
    // @_silgen_name("swift_task_getMainExecutor")
    // internal func _getMainExecutor() -> Builtin.Executor

    ParameterList *emptyParams = ParameterList::createEmpty(getASTContext());
    getMainExecutorFuncDecl = FuncDecl::createImplicit(
        getASTContext(), StaticSpellingKind::None,
        DeclName(
            getASTContext(),
            DeclBaseName(getASTContext().getIdentifier("_getMainExecutor")),
            /*Arguments*/ emptyParams),
        {}, /*async*/ false, /*throws*/ false, {}, emptyParams,
        getASTContext().TheExecutorType,
        entryPoint.getDecl()->getModuleContext());
    getMainExecutorFuncDecl->getAttrs().add(
        new (getASTContext())
            SILGenNameAttr("swift_task_getMainExecutor", /*implicit*/ true));
  }

  SILFunction *getMainExeutorSILFunc = SGM.getFunction(
      SILDeclRef(getMainExecutorFuncDecl, SILDeclRef::Kind::Func),
      NotForDefinition);
  SILValue getMainExeutorFunc =
      B.createFunctionRefFor(moduleLoc, getMainExeutorSILFunc);
  SILValue mainExecutor = B.createApply(moduleLoc, getMainExeutorFunc, {}, {});
  mainExecutor = wrapCallArgs(mainExecutor, swiftJobRunFuncDecl, 1);

  // Run first part synchronously
  B.createApply(moduleLoc, swiftJobRunFunc, {}, {jobResult, mainExecutor});

  // Start Main loop!
  FuncDecl *drainQueueFuncDecl = SGM.getAsyncMainDrainQueue();
  if (!drainQueueFuncDecl) {
    // If it doesn't exist, we can conjure one up instead of crashing
    // @available(SwiftStdlib 5.5, *)
    // @_silgen_name("swift_task_asyncMainDrainQueue")
    // internal func _asyncMainDrainQueue() -> Never
    ParameterList *emptyParams = ParameterList::createEmpty(getASTContext());
    drainQueueFuncDecl = FuncDecl::createImplicit(
        getASTContext(), StaticSpellingKind::None,
        DeclName(
            getASTContext(),
            DeclBaseName(getASTContext().getIdentifier("_asyncMainDrainQueue")),
            /*Arguments*/ emptyParams),
        {}, /*async*/ false, /*throws*/ false, {}, emptyParams,
        getASTContext().getNeverType(),
        entryPoint.getDecl()->getModuleContext());
    drainQueueFuncDecl->getAttrs().add(new (getASTContext()) SILGenNameAttr(
        "swift_task_asyncMainDrainQueue", /*implicit*/ true));
  }

  SILFunction *drainQueueSILFunc = SGM.getFunction(
      SILDeclRef(drainQueueFuncDecl, SILDeclRef::Kind::Func), NotForDefinition);
  SILValue drainQueueFunc =
      B.createFunctionRefFor(moduleLoc, drainQueueSILFunc);
  B.createApply(moduleLoc, drainQueueFunc, {}, {});
  B.createUnreachable(moduleLoc);
  return;
}

void SILGenFunction::emitGeneratorFunction(SILDeclRef function, Expr *value,
                                           bool EmitProfilerIncrement) {
  auto *dc = function.getDecl()->getInnermostDeclContext();
  MagicFunctionName = SILGenModule::getMagicFunctionName(function);

  RegularLocation Loc(value);
  Loc.markAutoGenerated();

  // If a default argument or stored property initializer value is a noescape
  // function type, strip the escape to noescape function conversion.
  if (function.kind == SILDeclRef::Kind::DefaultArgGenerator ||
      function.kind == SILDeclRef::Kind::StoredPropertyInitializer) {
    if (auto funType = value->getType()->getAs<AnyFunctionType>()) {
      if (funType->getExtInfo().isNoEscape()) {
        auto conv = cast<FunctionConversionExpr>(value);
        value = conv->getSubExpr();
        assert(funType->withExtInfo(funType->getExtInfo().withNoEscape(false))
                   ->isEqual(value->getType()));
      }
    }
  }

  // For a property wrapper backing initializer, form a parameter list
  // containing the wrapped or projected value.
  ParameterList *params = nullptr;
  if (function.kind == SILDeclRef::Kind::PropertyWrapperBackingInitializer ||
      function.kind == SILDeclRef::Kind::PropertyWrapperInitFromProjectedValue) {
    auto &ctx = getASTContext();
    auto param = new (ctx) ParamDecl(SourceLoc(), SourceLoc(),
                                     ctx.getIdentifier("$input_value"),
                                     SourceLoc(),
                                     ctx.getIdentifier("$input_value"),
                                     dc);
    param->setSpecifier(ParamSpecifier::Owned);
    param->setImplicit();
    auto vd = cast<VarDecl>(function.getDecl());
    if (function.kind == SILDeclRef::Kind::PropertyWrapperBackingInitializer) {
      param->setInterfaceType(vd->getPropertyWrapperInitValueInterfaceType());
    } else {
      auto *placeholder =
          vd->getPropertyWrapperInitializerInfo().getProjectedValuePlaceholder();
      auto interfaceType = placeholder->getType();
      if (interfaceType->hasArchetype())
        interfaceType = interfaceType->mapTypeOutOfContext();

      param->setInterfaceType(interfaceType);
    }

    params = ParameterList::create(ctx, SourceLoc(), {param}, SourceLoc());
  }

  auto captureInfo = SGM.M.Types.getLoweredLocalCaptures(function);
  auto interfaceType = value->getType()->mapTypeOutOfContext();
  emitProlog(captureInfo, params, /*selfParam=*/nullptr,
             dc, interfaceType, /*throws=*/false, SourceLoc());
  if (EmitProfilerIncrement)
    emitProfilerIncrement(value);
  prepareEpilog(interfaceType, false, CleanupLocation(Loc));

  {
    llvm::Optional<SILGenFunction::OpaqueValueRAII> opaqueValue;

    // For a property wrapper backing initializer, bind the opaque value used
    // in the initializer expression to the given parameter.
    if (function.kind == SILDeclRef::Kind::PropertyWrapperBackingInitializer) {
      auto var = cast<VarDecl>(function.getDecl());
      auto initInfo = var->getPropertyWrapperInitializerInfo();
      auto param = params->get(0);
      auto *placeholder = initInfo.getWrappedValuePlaceholder();
      opaqueValue.emplace(
          *this, placeholder->getOpaqueValuePlaceholder(),
          maybeEmitValueOfLocalVarDecl(param, AccessKind::Read));

      assert(value == initInfo.getInitFromWrappedValue());
    } else if (function.kind == SILDeclRef::Kind::PropertyWrapperInitFromProjectedValue) {
      auto var = cast<VarDecl>(function.getDecl());
      auto initInfo = var->getPropertyWrapperInitializerInfo();
      auto param = params->get(0);
      auto *placeholder = initInfo.getProjectedValuePlaceholder();
      opaqueValue.emplace(
          *this, placeholder->getOpaqueValuePlaceholder(),
          maybeEmitValueOfLocalVarDecl(param, AccessKind::Read));

      assert(value == initInfo.getInitFromProjectedValue());
    }

    emitReturnExpr(Loc, value);
  }

  emitEpilog(Loc);
  mergeCleanupBlocks();
}

void SILGenFunction::emitGeneratorFunction(SILDeclRef function, VarDecl *var) {
  MagicFunctionName = SILGenModule::getMagicFunctionName(function);

  RegularLocation loc(var);
  loc.markAutoGenerated();

  auto decl = function.getAbstractFunctionDecl();
  auto *dc = decl->getInnermostDeclContext();
  auto interfaceType = var->getValueInterfaceType();

  // If this is the backing storage for a property with an attached
  // wrapper that was initialized with '=', the stored property initializer
  // will be in terms of the original property's type.
  if (auto originalProperty = var->getOriginalWrappedProperty()) {
    if (originalProperty->isPropertyMemberwiseInitializedWithWrappedType()) {
      interfaceType = originalProperty->getPropertyWrapperInitValueInterfaceType();

      if (auto fnType = interfaceType->getAs<AnyFunctionType>()) {
        auto newExtInfo = fnType->getExtInfo().withNoEscape(false);
        interfaceType = fnType->withExtInfo(newExtInfo);
      }
    }
  }

  emitBasicProlog(/*paramList*/ nullptr, /*selfParam*/ nullptr,
                  interfaceType, dc, /*throws=*/ false,SourceLoc());
  prepareEpilog(interfaceType, false, CleanupLocation(loc));

  auto pbd = var->getParentPatternBinding();
  const auto i = pbd->getPatternEntryIndexForVarDecl(var);
  auto *anchorVar = pbd->getAnchoringVarDecl(i);
  auto subs = getForwardingSubstitutionMap();
  auto contextualType = dc->mapTypeIntoContext(interfaceType);
  auto resultType = contextualType->getCanonicalType();
  auto origResultType = AbstractionPattern(resultType);

  SmallVector<SILValue, 4> directResults;

  if (F.getConventions().hasIndirectSILResults()) {
    Scope scope(Cleanups, CleanupLocation(var));

    SmallVector<CleanupHandle, 4> cleanups;
    auto init = prepareIndirectResultInit(AbstractionPattern(resultType),
                                          resultType, directResults, cleanups);

    emitApplyOfStoredPropertyInitializer(loc, anchorVar, subs, resultType,
                                         origResultType,
                                         SGFContext(init.get()));

    for (auto cleanup : cleanups) {
      Cleanups.forwardCleanup(cleanup);
    }
  } else {
    Scope scope(Cleanups, CleanupLocation(var));

    // If we have no indirect results, just return the result.
    auto result = emitApplyOfStoredPropertyInitializer(loc, anchorVar, subs,
                                                       resultType,
                                                       origResultType,
                                                       SGFContext())
                    .ensurePlusOne(*this, loc);
    std::move(result).forwardAll(*this, directResults);
  }

  Cleanups.emitBranchAndCleanups(ReturnDest, loc, directResults);
  emitEpilog(loc);
}

static SILLocation getLocation(ASTNode Node) {
  if (auto *E = Node.dyn_cast<Expr *>())
    return E;
  else if (auto *S = Node.dyn_cast<Stmt *>())
    return S;
  else if (auto *D = Node.dyn_cast<Decl *>())
    return D;
  else
    llvm_unreachable("unsupported ASTNode");
}

void SILGenFunction::emitProfilerIncrement(ASTNode N) {
  // Ignore functions which aren't set up for instrumentation.
  SILProfiler *SP = F.getProfiler();
  if (!SP)
    return;
  if (!SP->hasRegionCounters() || !getModule().getOptions().UseProfile.empty())
    return;

  auto &C = B.getASTContext();
  const auto &RegionCounterMap = SP->getRegionCounterMap();
  auto CounterIt = RegionCounterMap.find(N);

  // TODO: Assert that this cannot happen (rdar://42792053).
  if (CounterIt == RegionCounterMap.end())
    return;

  auto Int32Ty = getLoweredType(BuiltinIntegerType::get(32, C));
  auto Int64Ty = getLoweredType(BuiltinIntegerType::get(64, C));

  SILLocation Loc = getLocation(N);
  SILValue Args[] = {
      // The intrinsic must refer to the function profiling name var, which is
      // inaccessible during SILGen. Rely on irgen to rewrite the function name.
      B.createStringLiteral(Loc, SP->getPGOFuncName(),
                            StringLiteralInst::Encoding::UTF8),
      B.createIntegerLiteral(Loc, Int64Ty, SP->getPGOFuncHash()),
      B.createIntegerLiteral(Loc, Int32Ty, SP->getNumRegionCounters()),
      B.createIntegerLiteral(Loc, Int32Ty, CounterIt->second)};
  B.createBuiltin(
      Loc,
      C.getIdentifier(getBuiltinName(BuiltinValueKind::IntInstrprofIncrement)),
      SGM.Types.getEmptyTupleType(), {}, Args);
}

ProfileCounter SILGenFunction::loadProfilerCount(ASTNode Node) const {
  if (SILProfiler *SP = F.getProfiler())
    return SP->getExecutionCount(Node);
  return ProfileCounter();
}

Optional<ASTNode> SILGenFunction::getPGOParent(ASTNode Node) const {
  if (SILProfiler *SP = F.getProfiler())
    return SP->getPGOParent(Node);
  return None;
}

SILValue SILGenFunction::emitUnwrapIntegerResult(SILLocation loc,
                                                 SILValue value) {
  // This is a loop because we want to handle types that wrap integer types,
  // like ObjCBool (which may be Bool or Int8).
  while (!value->getType().is<BuiltinIntegerType>()) {
    auto structDecl = value->getType().getStructOrBoundGenericStruct();
    assert(structDecl && "value for error result wasn't of struct type!");
    assert(structDecl->getStoredProperties().size() == 1);
    auto property = structDecl->getStoredProperties()[0];
    value = B.createStructExtract(loc, value, property);
  }

  return value;
}

SILValue SILGenFunction::emitWrapIntegerLiteral(SILLocation loc,
                                                SILType ty,
                                                unsigned value) {
  // Create a builtin integer literal value.
  if (auto intTy = ty.getAs<BuiltinIntegerType>()) {
    return B.createIntegerLiteral(loc, ty, value);
  }
  
  // Or wrap a value in a struct, potentially multiple times to handle types
  // that wrap integer types like ObjCBool (which may be Bool or Int8).
  auto structDecl = ty.getStructOrBoundGenericStruct();
  assert(structDecl && "value for error result wasn't of struct type!");
  assert(structDecl->getStoredProperties().size() == 1);
  auto property = structDecl->getStoredProperties()[0];
  auto propertyTy = ty.getFieldType(property, SGM.Types, getTypeExpansionContext());
  auto propertyValue = emitWrapIntegerLiteral(loc, propertyTy, value);
  return B.createStruct(loc, ty, propertyValue);
}
