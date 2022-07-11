//===--- SILDeclRef.cpp - Implements SILDeclRef ---------------------------===//
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

#include "swift/SIL/SILDeclRef.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/ASTMangler.h"
#include "swift/AST/AnyFunctionRef.h"
#include "swift/AST/Initializer.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/SourceFile.h"
#include "swift/ClangImporter/ClangImporter.h"
#include "swift/ClangImporter/ClangModule.h"
#include "swift/SIL/SILLinkage.h"
#include "swift/SIL/SILLocation.h"
#include "swift/SILOptimizer/Utils/SpecializationMangler.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Mangle.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
using namespace swift;

/// Get the method dispatch mechanism for a method.
MethodDispatch
swift::getMethodDispatch(AbstractFunctionDecl *method) {
  // Some methods are forced to be statically dispatched.
  if (method->hasForcedStaticDispatch())
    return MethodDispatch::Static;

  if (method->getAttrs().hasAttribute<DistributedActorAttr>())
    return MethodDispatch::Static;

  // Import-as-member declarations are always statically referenced.
  if (method->isImportAsMember())
    return MethodDispatch::Static;

  auto dc = method->getDeclContext();

  if (dc->getSelfClassDecl()) {
    if (method->shouldUseObjCDispatch()) {
      return MethodDispatch::Class;
    }

    // Final methods can be statically referenced.
    if (method->isFinal())
      return MethodDispatch::Static;

    // Imported class methods are dynamically dispatched.
    if (method->isObjC() && method->hasClangNode())
      return MethodDispatch::Class;

    // Members defined directly inside a class are dynamically dispatched.
    if (isa<ClassDecl>(dc)) {
      // Native convenience initializers are not dynamically dispatched unless
      // required.
      if (auto ctor = dyn_cast<ConstructorDecl>(method)) {
        if (!ctor->isRequired() && !ctor->isDesignatedInit()
            && !requiresForeignEntryPoint(ctor))
          return MethodDispatch::Static;
      }
      return MethodDispatch::Class;
    }
  }

  // Otherwise, it can be referenced statically.
  return MethodDispatch::Static;
}

bool swift::requiresForeignToNativeThunk(ValueDecl *vd) {
  // Functions imported from C, Objective-C methods imported from Objective-C,
  // as well as methods in @objc protocols (even protocols defined in Swift)
  // require a foreign to native thunk.
  auto dc = vd->getDeclContext();
  if (auto proto = dyn_cast<ProtocolDecl>(dc))
    if (proto->isObjC())
      return true;

  if (auto fd = dyn_cast<FuncDecl>(vd))
    return fd->hasClangNode();

  return false;
}

bool swift::requiresForeignEntryPoint(ValueDecl *vd) {
  assert(!isa<AbstractStorageDecl>(vd));

  if (vd->shouldUseObjCDispatch()) {
    return true;
  }

  if (vd->isObjC() && isa<ProtocolDecl>(vd->getDeclContext()))
    return true;

  if (vd->isImportAsMember())
    return true;

  if (vd->hasClangNode())
    return true;

  if (auto *accessor = dyn_cast<AccessorDecl>(vd)) {
    // Property accessors should be generated alongside the property.
    if (accessor->isGetterOrSetter()) {
      auto *asd = accessor->getStorage();
      if (asd->isObjC() && asd->hasClangNode())
        return true;
    }
  }

  return false;
}

SILDeclRef::SILDeclRef(ValueDecl *vd, SILDeclRef::Kind kind,
                       bool isForeign, bool isDistributed,
                       AutoDiffDerivativeFunctionIdentifier *derivativeId)
    : loc(vd), kind(kind), isForeign(isForeign), isDistributed(isDistributed),
      defaultArgIndex(0), pointer(derivativeId) {}

SILDeclRef::SILDeclRef(SILDeclRef::Loc baseLoc, bool asForeign, bool asDistributed)
    : defaultArgIndex(0),
      pointer((AutoDiffDerivativeFunctionIdentifier *)nullptr) {
  if (auto *vd = baseLoc.dyn_cast<ValueDecl*>()) {
    if (auto *fd = dyn_cast<FuncDecl>(vd)) {
      // Map FuncDecls directly to Func SILDeclRefs.
      loc = fd;
      kind = Kind::Func;
    }
    // Map ConstructorDecls to the Allocator SILDeclRef of the constructor.
    else if (auto *cd = dyn_cast<ConstructorDecl>(vd)) {
      loc = cd;
      kind = Kind::Allocator;
    }
    // Map EnumElementDecls to the EnumElement SILDeclRef of the element.
    else if (auto *ed = dyn_cast<EnumElementDecl>(vd)) {
      loc = ed;
      kind = Kind::EnumElement;
    }
    // VarDecl constants require an explicit kind.
    else if (isa<VarDecl>(vd)) {
      llvm_unreachable("must create SILDeclRef for VarDecl with explicit kind");
    }
    // Map DestructorDecls to the Deallocator of the destructor.
    else if (auto dtor = dyn_cast<DestructorDecl>(vd)) {
      loc = dtor;
      kind = Kind::Deallocator;
    }
    else {
      llvm_unreachable("invalid loc decl for SILDeclRef!");
    }
  } else if (auto *ACE = baseLoc.dyn_cast<AbstractClosureExpr *>()) {
    loc = ACE;
    kind = Kind::Func;
  } else {
    llvm_unreachable("impossible SILDeclRef loc");
  }

  isForeign = asForeign;
  isDistributed = asDistributed;
}

SILDeclRef::SILDeclRef(SILDeclRef::Loc baseLoc,
                       GenericSignature prespecializedSig)
    : SILDeclRef(baseLoc, false, false) {
  pointer = prespecializedSig.getPointer();
}

Optional<AnyFunctionRef> SILDeclRef::getAnyFunctionRef() const {
  switch (getLocKind()) {
  case LocKind::Decl:
    if (auto *afd = getAbstractFunctionDecl())
      return AnyFunctionRef(afd);
    return None;
  case LocKind::Closure:
    return AnyFunctionRef(getAbstractClosureExpr());
  case LocKind::File:
    return None;
  }
  llvm_unreachable("Unhandled case in switch");
}

ASTContext &SILDeclRef::getASTContext() const {
  switch (getLocKind()) {
  case LocKind::Decl:
    return getDecl()->getASTContext();
  case LocKind::Closure:
    return getAbstractClosureExpr()->getASTContext();
  case LocKind::File:
    return getFileUnit()->getASTContext();
  }
  llvm_unreachable("Unhandled case in switch");
}

bool SILDeclRef::isThunk() const {
  return isForeignToNativeThunk() || isNativeToForeignThunk() || isDistributedThunk();
}

bool SILDeclRef::isClangImported() const {
  if (!hasDecl())
    return false;

  ValueDecl *d = getDecl();
  DeclContext *moduleContext = d->getDeclContext()->getModuleScopeContext();

  if (isa<ClangModuleUnit>(moduleContext)) {
    if (isClangGenerated())
      return true;

    if (isa<ConstructorDecl>(d) || isa<EnumElementDecl>(d))
      return !isForeign;

    if (auto *FD = dyn_cast<FuncDecl>(d))
      if (isa<AccessorDecl>(FD) ||
          isa<NominalTypeDecl>(d->getDeclContext()))
        return !isForeign;
  }
  return false;
}

bool SILDeclRef::isClangGenerated() const {
  if (!hasDecl())
    return false;

  return isClangGenerated(getDecl()->getClangNode());
}

// FIXME: this is a weird predicate.
bool SILDeclRef::isClangGenerated(ClangNode node) {
  if (auto nd = dyn_cast_or_null<clang::NamedDecl>(node.getAsDecl())) {
    // ie, 'static inline' functions for which we must ask Clang to emit a body
    // for explicitly
    if (!nd->isExternallyVisible())
      return true;
  }

  return false;
}

bool SILDeclRef::isImplicit() const {
  switch (getLocKind()) {
  case LocKind::Decl:
    return getDecl()->isImplicit();
  case LocKind::Closure:
    return getAbstractClosureExpr()->isImplicit();
  case LocKind::File:
    // Files are currently never considered implicit.
    return false;
  }
  llvm_unreachable("Unhandled case in switch");
}

SILLinkage SILDeclRef::getLinkage(ForDefinition_t forDefinition) const {

  // Prespecializations are public.
  if (getSpecializedSignature()) {
    return SILLinkage::Public;
  }

  if (getAbstractClosureExpr()) {
    return isSerialized() ? SILLinkage::Shared : SILLinkage::Private;
  }

  // The main entry-point is public.
  if (kind == Kind::EntryPoint)
    return SILLinkage::Public;
  if (kind == Kind::AsyncEntryPoint)
    return SILLinkage::Hidden;

  // Add External to the linkage (e.g. Public -> PublicExternal) if this is a
  // declaration not a definition.
  auto maybeAddExternal = [&](SILLinkage linkage) {
    return forDefinition ? linkage : addExternalToLinkage(linkage);
  };

  ValueDecl *d = getDecl();

  // Property wrapper generators of public functions have PublicNonABI linkage
  if (isPropertyWrapperBackingInitializer() && isa<ParamDecl>(d)) {
    if (isSerialized())
      return maybeAddExternal(SILLinkage::PublicNonABI);
  }

  // Function-local declarations have private linkage, unless serialized.
  DeclContext *moduleContext = d->getDeclContext();
  while (!moduleContext->isModuleScopeContext()) {
    if (moduleContext->isLocalContext()) {
      return isSerialized() ? SILLinkage::Shared : SILLinkage::Private;
    }
    moduleContext = moduleContext->getParent();
  }

  // Calling convention thunks have shared linkage.
  if (isForeignToNativeThunk())
    return SILLinkage::Shared;

  // Declarations imported from Clang modules have shared linkage.
  if (isClangImported())
    return SILLinkage::Shared;

  // Default argument generators of Public functions have PublicNonABI linkage
  // if the function was type-checked in Swift 4 mode.
  if (kind == SILDeclRef::Kind::DefaultArgGenerator) {
    if (isSerialized())
      return maybeAddExternal(SILLinkage::PublicNonABI);
  }

  enum class Limit {
    /// No limit.
    None,
    /// The declaration is emitted on-demand; it should end up with internal
    /// or shared linkage.
    OnDemand,
    /// The declaration should never be made public.
    NeverPublic,
    /// The declaration should always be emitted into the client,
    AlwaysEmitIntoClient,
  };
  auto limit = Limit::None;

  // @_alwaysEmitIntoClient declarations are like the default arguments of
  // public functions; they are roots for dead code elimination and have
  // serialized bodies, but no public symbol in the generated binary.
  if (d->getAttrs().hasAttribute<AlwaysEmitIntoClientAttr>())
    limit = Limit::AlwaysEmitIntoClient;
  if (auto accessor = dyn_cast<AccessorDecl>(d)) {
    auto *storage = accessor->getStorage();
    if (storage->getAttrs().hasAttribute<AlwaysEmitIntoClientAttr>())
      limit = Limit::AlwaysEmitIntoClient;
  }

  // ivar initializers and destroyers are completely contained within the class
  // from which they come, and never get seen externally.
  if (isIVarInitializerOrDestroyer()) {
    limit = Limit::NeverPublic;
  }

  // Stored property initializers get the linkage of their containing type.
  if (isStoredPropertyInitializer() || isPropertyWrapperBackingInitializer()) {
    // Three cases:
    //
    // 1) Type is formally @_fixed_layout/@frozen. Root initializers can be
    //    declared @inlinable. The property initializer must only reference
    //    public symbols, and is serialized, so we give it PublicNonABI linkage.
    //
    // 2) Type is not formally @_fixed_layout/@frozen and the module is not
    //    resilient. Root initializers can be declared @inlinable. This is the 
    //    annoying case. We give the initializer public linkage if the type is
    //    public.
    //
    // 3) Type is resilient. The property initializer is never public because
    //    root initializers cannot be @inlinable.
    //
    // FIXME: Get rid of case 2 somehow.
    if (isSerialized())
      return maybeAddExternal(SILLinkage::PublicNonABI);

    d = cast<NominalTypeDecl>(d->getDeclContext());

    // FIXME: This should always be true.
    if (d->getModuleContext()->isResilient())
      limit = Limit::NeverPublic;
  }

  // The global addressor is never public for resilient globals.
  if (kind == Kind::GlobalAccessor) {
    if (cast<VarDecl>(d)->isResilient()) {
      limit = Limit::NeverPublic;
    }
  }

  if (auto fn = dyn_cast<FuncDecl>(d)) {
    // Forced-static-dispatch functions are created on-demand and have
    // at best shared linkage.
    if (fn->hasForcedStaticDispatch()) {
      limit = Limit::OnDemand;
    }

    // Native-to-foreign thunks for top-level decls are created on-demand,
    // unless they are marked @_cdecl, in which case they expose a dedicated
    // entry-point with the visibility of the function.
    if (isNativeToForeignThunk() && !fn->getAttrs().hasAttribute<CDeclAttr>()) {
      if (fn->getDeclContext()->isModuleScopeContext())
        limit = Limit::OnDemand;
    }
  }

  if (isEnumElement()) {
    limit = Limit::OnDemand;
  }

  auto effectiveAccess = d->getEffectiveAccess();
  
  // Private setter implementations for an internal storage declaration should
  // be at least internal as well, so that a dynamically-writable
  // keypath can be formed from other files in the same module.
  if (auto accessor = dyn_cast<AccessorDecl>(d)) {
    if (accessor->isSetter()
       && accessor->getStorage()->getEffectiveAccess() >= AccessLevel::Internal)
      effectiveAccess = std::max(effectiveAccess, AccessLevel::Internal);
  }

  switch (effectiveAccess) {
  case AccessLevel::Private:
  case AccessLevel::FilePrivate:
    return SILLinkage::Private;

  case AccessLevel::Internal:
    if (limit == Limit::OnDemand)
      return SILLinkage::Shared;
    return maybeAddExternal(SILLinkage::Hidden);

  case AccessLevel::Public:
  case AccessLevel::Open:
    if (limit == Limit::OnDemand)
      return SILLinkage::Shared;
    if (limit == Limit::NeverPublic)
      return maybeAddExternal(SILLinkage::Hidden);
    if (limit == Limit::AlwaysEmitIntoClient)
      return maybeAddExternal(SILLinkage::PublicNonABI);
    return maybeAddExternal(SILLinkage::Public);
  }
  llvm_unreachable("unhandled access");
}

SILDeclRef SILDeclRef::getDefaultArgGenerator(Loc loc,
                                              unsigned defaultArgIndex) {
  SILDeclRef result;
  result.loc = loc;
  result.kind = Kind::DefaultArgGenerator;
  result.defaultArgIndex = defaultArgIndex;
  return result;
}

SILDeclRef SILDeclRef::getMainDeclEntryPoint(ValueDecl *decl) {
  auto *file = cast<FileUnit>(decl->getDeclContext()->getModuleScopeContext());
  assert(file->getMainDecl() == decl);
  SILDeclRef result;
  result.loc = decl;
  result.kind = Kind::EntryPoint;
  return result;
}

SILDeclRef SILDeclRef::getAsyncMainDeclEntryPoint(ValueDecl *decl) {
  auto *file = cast<FileUnit>(decl->getDeclContext()->getModuleScopeContext());
  assert(file->getMainDecl() == decl);
  SILDeclRef result;
  result.loc = decl;
  result.kind = Kind::AsyncEntryPoint;
  return result;
}

SILDeclRef SILDeclRef::getMainFileEntryPoint(FileUnit *file) {
  assert(file->hasEntryPoint() && !file->getMainDecl());
  SILDeclRef result;
  result.loc = file;
  result.kind = Kind::EntryPoint;
  return result;
}

bool SILDeclRef::hasClosureExpr() const {
  return loc.is<AbstractClosureExpr *>()
    && isa<ClosureExpr>(getAbstractClosureExpr());
}

bool SILDeclRef::hasAutoClosureExpr() const {
  return loc.is<AbstractClosureExpr *>()
    && isa<AutoClosureExpr>(getAbstractClosureExpr());
}

bool SILDeclRef::hasFuncDecl() const {
  return loc.is<ValueDecl *>() && isa<FuncDecl>(getDecl());
}

ClosureExpr *SILDeclRef::getClosureExpr() const {
  return dyn_cast<ClosureExpr>(getAbstractClosureExpr());
}
AutoClosureExpr *SILDeclRef::getAutoClosureExpr() const {
  return dyn_cast<AutoClosureExpr>(getAbstractClosureExpr());
}

FuncDecl *SILDeclRef::getFuncDecl() const {
  return dyn_cast<FuncDecl>(getDecl());
}

bool SILDeclRef::isSetter() const {
  if (!hasDecl())
    return false;
  if (auto accessor = dyn_cast<AccessorDecl>(getDecl()))
    return accessor->isSetter();
  return false;
}

AbstractFunctionDecl *SILDeclRef::getAbstractFunctionDecl() const {
  return dyn_cast<AbstractFunctionDecl>(getDecl());
}

/// True if the function should be treated as transparent.
bool SILDeclRef::isTransparent() const {
  if (isEnumElement())
    return true;

  if (isStoredPropertyInitializer())
    return true;

  if (hasAutoClosureExpr()) {
    auto *ace = getAutoClosureExpr();
    switch (ace->getThunkKind()) {
    case AutoClosureExpr::Kind::None:
      return true;

    case AutoClosureExpr::Kind::AsyncLet:
    case AutoClosureExpr::Kind::DoubleCurryThunk:
    case AutoClosureExpr::Kind::SingleCurryThunk:
      break;
    }
  }

  if (hasDecl()) {
    if (auto *AFD = dyn_cast<AbstractFunctionDecl>(getDecl()))
      return AFD->isTransparent();

    if (auto *ASD = dyn_cast<AbstractStorageDecl>(getDecl()))
      return ASD->isTransparent();
  }

  return false;
}

/// True if the function should have its body serialized.
IsSerialized_t SILDeclRef::isSerialized() const {
  if (auto closure = getAbstractClosureExpr()) {
    // Ask the AST if we're inside an @inlinable context.
    if (closure->getResilienceExpansion() == ResilienceExpansion::Minimal) {
      if (isForeign)
        return IsSerializable;

      return IsSerialized;
    }

    return IsNotSerialized;
  }

  if (kind == Kind::EntryPoint || kind == Kind::AsyncEntryPoint)
    return IsNotSerialized;

  if (isIVarInitializerOrDestroyer())
    return IsNotSerialized;

  auto *d = getDecl();

  // Default and property wrapper argument generators are serialized if the
  // containing declaration is public.
  if (isDefaultArgGenerator() || (isPropertyWrapperBackingInitializer() &&
                                  isa<ParamDecl>(d))) {
    if (isPropertyWrapperBackingInitializer()) {
      if (auto *func = dyn_cast_or_null<ValueDecl>(d->getDeclContext()->getAsDecl())) {
        d = func;
      }
    }

    // Ask the AST if we're inside an @inlinable context.
    if (d->getDeclContext()->getResilienceExpansion()
          == ResilienceExpansion::Minimal) {
      return IsSerialized;
    }

    // Otherwise, check if the owning declaration is public.
    auto scope =
      d->getFormalAccessScope(/*useDC=*/nullptr,
                              /*treatUsableFromInlineAsPublic=*/true);

    if (scope.isPublic())
      return IsSerialized;
    return IsNotSerialized;
  }

  // Stored property initializers are inlinable if the type is explicitly
  // marked as @frozen.
  if (isStoredPropertyInitializer() || (isPropertyWrapperBackingInitializer() &&
                                        d->getDeclContext()->isTypeContext())) {
    auto *nominal = cast<NominalTypeDecl>(d->getDeclContext());
    auto scope =
      nominal->getFormalAccessScope(/*useDC=*/nullptr,
                                    /*treatUsableFromInlineAsPublic=*/true);
    if (!scope.isPublic())
      return IsNotSerialized;
    if (nominal->isFormallyResilient())
      return IsNotSerialized;
    return IsSerialized;
  }

  // Note: if 'd' is a function, then 'dc' is the function itself, not
  // its parent context.
  auto *dc = d->getInnermostDeclContext();

  // Local functions are serializable if their parent function is
  // serializable.
  if (d->getDeclContext()->isLocalContext()) {
    if (dc->getResilienceExpansion() == ResilienceExpansion::Minimal)
      return IsSerializable;

    return IsNotSerialized;
  }

  // Anything else that is not public is not serializable.
  if (d->getEffectiveAccess() < AccessLevel::Public)
    return IsNotSerialized;

  // Enum element constructors are serializable if the enum is
  // @usableFromInline or public.
  if (isEnumElement())
    return IsSerializable;

  // 'read' and 'modify' accessors synthesized on-demand are serialized if
  // visible outside the module.
  if (auto fn = dyn_cast<FuncDecl>(d))
    if (!isClangImported() &&
        fn->hasForcedStaticDispatch())
      return IsSerialized;

  if (isForeignToNativeThunk())
    return IsSerializable;

  // The allocating entry point for designated initializers are serialized
  // if the class is @usableFromInline or public.
  if (kind == SILDeclRef::Kind::Allocator) {
    auto *ctor = cast<ConstructorDecl>(d);
    if (ctor->isDesignatedInit() &&
        ctor->getDeclContext()->getSelfClassDecl()) {
      if (!ctor->hasClangNode())
        return IsSerialized;
    }
  }

  if (isForeign) {
    // @objc thunks for methods are not serializable since they're only
    // referenced from the method table.
    if (d->getDeclContext()->isTypeContext())
      return IsNotSerialized;

    // @objc thunks for top-level functions are serializable since they're
    // referenced from @convention(c) conversions inside inlinable
    // functions.
    return IsSerializable;
  }

  // Declarations imported from Clang modules are serialized if
  // referenced from an inlinable context.
  if (isClangImported())
    return IsSerializable;

  // Otherwise, ask the AST if we're inside an @inlinable context.
  if (dc->getResilienceExpansion() == ResilienceExpansion::Minimal)
    return IsSerialized;

  return IsNotSerialized;
}

/// True if the function has an @inline(never) attribute.
bool SILDeclRef::isNoinline() const {
  if (!hasDecl())
    return false;

  auto *decl = getDecl();
  if (auto *attr = decl->getAttrs().getAttribute<InlineAttr>())
    if (attr->getKind() == InlineKind::Never)
      return true;

  if (auto *accessorDecl = dyn_cast<AccessorDecl>(decl)) {
    auto *storage = accessorDecl->getStorage();
    if (auto *attr = storage->getAttrs().getAttribute<InlineAttr>())
      if (attr->getKind() == InlineKind::Never)
        return true;
  }

  return false;
}

/// True if the function has the @inline(__always) attribute.
bool SILDeclRef::isAlwaysInline() const {
  if (!hasDecl())
    return false;

  auto *decl = getDecl();
  if (auto attr = decl->getAttrs().getAttribute<InlineAttr>())
    if (attr->getKind() == InlineKind::Always)
      return true;

  if (auto *accessorDecl = dyn_cast<AccessorDecl>(decl)) {
    auto *storage = accessorDecl->getStorage();
    if (auto *attr = storage->getAttrs().getAttribute<InlineAttr>())
      if (attr->getKind() == InlineKind::Always)
        return true;
  }

  return false;
}

bool SILDeclRef::hasEffectsAttribute() const {
  if (!hasDecl())
    return false;
  return getDecl()->getAttrs().hasAttribute<EffectsAttr>();
}

EffectsKind SILDeclRef::getEffectsAttribute() const {
  assert(hasEffectsAttribute());
  EffectsAttr *MA = getDecl()->getAttrs().getAttribute<EffectsAttr>();
  return MA->getKind();
}

bool SILDeclRef::isAnyThunk() const {
  return isForeignToNativeThunk() ||
    isNativeToForeignThunk() ||
    isDistributedThunk();
}

bool SILDeclRef::isForeignToNativeThunk() const {
  // If this isn't a native entry-point, it's not a foreign-to-native thunk.
  if (isForeign)
    return false;

  // Non-decl entry points are never natively foreign, so they would never
  // have a foreign-to-native thunk.
  if (!hasDecl())
    return false;
  if (requiresForeignToNativeThunk(getDecl()))
    return true;
  // ObjC initializing constructors and factories are foreign.
  // We emit a special native allocating constructor though.
  if (isa<ConstructorDecl>(getDecl())
      && (kind == Kind::Initializer
          || cast<ConstructorDecl>(getDecl())->isFactoryInit())
      && getDecl()->hasClangNode())
    return true;
  return false;
}

bool SILDeclRef::isNativeToForeignThunk() const {
  // If this isn't a foreign entry-point, it's not a native-to-foreign thunk.
  if (!isForeign)
    return false;

  switch (getLocKind()) {
  case LocKind::Decl:
    // A decl with a clang node doesn't have a native entry-point to forward
    // onto.
    if (getDecl()->hasClangNode())
      return false;

    // Only certain kinds of SILDeclRef can expose native-to-foreign thunks.
    return kind == Kind::Func || kind == Kind::Initializer ||
           kind == Kind::Deallocator;
  case LocKind::Closure:
    // We can have native-to-foreign thunks over closures.
    return true;
  case LocKind::File:
    return false;
  }
  llvm_unreachable("Unhandled case in switch");
}

bool SILDeclRef::isDistributedThunk() const {
  if (!isDistributed)
    return false;
  return kind == Kind::Func;
}

/// Use the Clang importer to mangle a Clang declaration.
static void mangleClangDecl(raw_ostream &buffer,
                            const clang::NamedDecl *clangDecl,
                            ASTContext &ctx) {
  auto *importer = static_cast<ClangImporter *>(ctx.getClangModuleLoader());
  importer->getMangledName(buffer, clangDecl);
}

std::string SILDeclRef::mangle(ManglingKind MKind) const {
  using namespace Mangle;
  ASTMangler mangler;

  if (auto *derivativeFunctionIdentifier = getDerivativeFunctionIdentifier()) {
    std::string originalMangled = asAutoDiffOriginalFunction().mangle(MKind);
    auto *silParameterIndices = autodiff::getLoweredParameterIndices(
        derivativeFunctionIdentifier->getParameterIndices(),
        getDecl()->getInterfaceType()->castTo<AnyFunctionType>());
    auto *resultIndices = IndexSubset::get(getDecl()->getASTContext(), 1, {0});
    AutoDiffConfig silConfig(
        silParameterIndices, resultIndices,
        derivativeFunctionIdentifier->getDerivativeGenericSignature());
    return mangler.mangleAutoDiffDerivativeFunction(
        asAutoDiffOriginalFunction().getAbstractFunctionDecl(),
        derivativeFunctionIdentifier->getKind(),
        silConfig);
  }

  // As a special case, Clang functions and globals don't get mangled at all.
  if (hasDecl()) {
    if (auto clangDecl = getDecl()->getClangDecl()) {
      if (!isForeignToNativeThunk() && !isNativeToForeignThunk()) {
        if (auto namedClangDecl = dyn_cast<clang::DeclaratorDecl>(clangDecl)) {
          if (auto asmLabel = namedClangDecl->getAttr<clang::AsmLabelAttr>()) {
            std::string s(1, '\01');
            s += asmLabel->getLabel();
            return s;
          } else if (namedClangDecl->hasAttr<clang::OverloadableAttr>() ||
                     getDecl()->getASTContext().LangOpts.EnableCXXInterop) {
            std::string storage;
            llvm::raw_string_ostream SS(storage);
            mangleClangDecl(SS, namedClangDecl, getDecl()->getASTContext());
            return SS.str();
          }
          return namedClangDecl->getName().str();
        } else if (auto objcDecl = dyn_cast<clang::ObjCMethodDecl>(clangDecl)) {
          if (objcDecl->isDirectMethod()) {
            std::string storage;
            llvm::raw_string_ostream SS(storage);
            clang::ASTContext &ctx = clangDecl->getASTContext();
            std::unique_ptr<clang::MangleContext> mangler(ctx.createMangleContext());
            mangler->mangleObjCMethodName(objcDecl, SS, /*includePrefixByte=*/true,
                                          /*includeCategoryNamespace=*/false);
            return SS.str();
          }
        }
      }
    }
  }

  // Mangle prespecializations.
  if (getSpecializedSignature()) {
    SILDeclRef nonSpecializedDeclRef = *this;
    nonSpecializedDeclRef.pointer =
        (AutoDiffDerivativeFunctionIdentifier *)nullptr;
    auto mangledNonSpecializedString = nonSpecializedDeclRef.mangle();
    auto *funcDecl = cast<AbstractFunctionDecl>(getDecl());
    auto genericSig = funcDecl->getGenericSignature();
    return GenericSpecializationMangler::manglePrespecialization(
        mangledNonSpecializedString, genericSig, getSpecializedSignature());
  }

  ASTMangler::SymbolKind SKind = ASTMangler::SymbolKind::Default;
  switch (MKind) {
    case SILDeclRef::ManglingKind::Default:
      if (isForeign) {
        SKind = ASTMangler::SymbolKind::SwiftAsObjCThunk;
      } else if (isForeignToNativeThunk()) {
        SKind = ASTMangler::SymbolKind::ObjCAsSwiftThunk;
      } else if (isDistributedThunk()) {
        SKind = ASTMangler::SymbolKind::DistributedThunk;
      }
      break;
    case SILDeclRef::ManglingKind::DynamicThunk:
      SKind = ASTMangler::SymbolKind::DynamicThunk;
      break;
  }

  switch (kind) {
  case SILDeclRef::Kind::Func:
    if (auto *ACE = getAbstractClosureExpr())
      return mangler.mangleClosureEntity(ACE, SKind);

    // As a special case, functions can have manually mangled names.
    // Use the SILGen name only for the original non-thunked, non-curried entry
    // point.
    if (auto NameA = getDecl()->getAttrs().getAttribute<SILGenNameAttr>())
      if (!NameA->Name.empty() && !isAnyThunk()) {
        return NameA->Name.str();
      }
      
    // Use a given cdecl name for native-to-foreign thunks.
    if (auto CDeclA = getDecl()->getAttrs().getAttribute<CDeclAttr>())
      if (isNativeToForeignThunk()) {
        return CDeclA->Name.str();
      }

    // Otherwise, fall through into the 'other decl' case.
    LLVM_FALLTHROUGH;

  case SILDeclRef::Kind::EnumElement:
    return mangler.mangleEntity(getDecl(), SKind);

  case SILDeclRef::Kind::Deallocator:
    return mangler.mangleDestructorEntity(cast<DestructorDecl>(getDecl()),
                                          /*isDeallocating*/ true,
                                          SKind);

  case SILDeclRef::Kind::Destroyer:
    return mangler.mangleDestructorEntity(cast<DestructorDecl>(getDecl()),
                                          /*isDeallocating*/ false,
                                          SKind);

  case SILDeclRef::Kind::Allocator:
    return mangler.mangleConstructorEntity(cast<ConstructorDecl>(getDecl()),
                                           /*allocating*/ true,
                                           SKind);

  case SILDeclRef::Kind::Initializer:
    return mangler.mangleConstructorEntity(cast<ConstructorDecl>(getDecl()),
                                           /*allocating*/ false,
                                           SKind);

  case SILDeclRef::Kind::IVarInitializer:
  case SILDeclRef::Kind::IVarDestroyer:
    return mangler.mangleIVarInitDestroyEntity(cast<ClassDecl>(getDecl()),
                                  kind == SILDeclRef::Kind::IVarDestroyer,
                                  SKind);

  case SILDeclRef::Kind::GlobalAccessor:
    return mangler.mangleAccessorEntity(AccessorKind::MutableAddress,
                                        cast<AbstractStorageDecl>(getDecl()),
                                        /*isStatic*/ false,
                                        SKind);

  case SILDeclRef::Kind::DefaultArgGenerator:
    return mangler.mangleDefaultArgumentEntity(
                                        cast<DeclContext>(getDecl()),
                                        defaultArgIndex,
                                        SKind);

  case SILDeclRef::Kind::StoredPropertyInitializer:
    return mangler.mangleInitializerEntity(cast<VarDecl>(getDecl()), SKind);

  case SILDeclRef::Kind::PropertyWrapperBackingInitializer:
    return mangler.mangleBackingInitializerEntity(cast<VarDecl>(getDecl()),
                                                  SKind);

  case SILDeclRef::Kind::PropertyWrapperInitFromProjectedValue:
    return mangler.mangleInitFromProjectedValueEntity(cast<VarDecl>(getDecl()),
                                                      SKind);

  case SILDeclRef::Kind::AsyncEntryPoint: {
    return "async_Main";
  }
  case SILDeclRef::Kind::EntryPoint: {
    return getASTContext().getEntryPointFunctionName();
  }
  }

  llvm_unreachable("bad entity kind!");
}

// Returns true if the given JVP/VJP SILDeclRef requires a new vtable entry.
// FIXME(SR-14131): Also consider derived declaration `@derivative` attributes.
static bool derivativeFunctionRequiresNewVTableEntry(SILDeclRef declRef) {
  assert(declRef.getDerivativeFunctionIdentifier() &&
         "Expected a derivative function SILDeclRef");
  auto overridden = declRef.getOverridden();
  if (!overridden)
    return false;
  // Get the derived `@differentiable` attribute.
  auto *derivedDiffAttr = *llvm::find_if(
      declRef.getDecl()->getAttrs().getAttributes<DifferentiableAttr>(),
      [&](const DifferentiableAttr *derivedDiffAttr) {
        return derivedDiffAttr->getParameterIndices() ==
               declRef.getDerivativeFunctionIdentifier()->getParameterIndices();
      });
  assert(derivedDiffAttr && "Expected `@differentiable` attribute");
  // Otherwise, if the base `@differentiable` attribute specifies a derivative
  // function, then the derivative is inherited and no new vtable entry is
  // needed. Return false.
  auto baseDiffAttrs =
      overridden.getDecl()->getAttrs().getAttributes<DifferentiableAttr>();
  for (auto *baseDiffAttr : baseDiffAttrs) {
    if (baseDiffAttr->getParameterIndices() ==
        declRef.getDerivativeFunctionIdentifier()->getParameterIndices())
      return false;
  }
  // Otherwise, if there is no base `@differentiable` attribute exists, then a
  // new vtable entry is needed. Return true.
  return true;
}

bool SILDeclRef::requiresNewVTableEntry() const {
  if (getDerivativeFunctionIdentifier())
    if (derivativeFunctionRequiresNewVTableEntry(*this))
      return true;
  if (!hasDecl())
    return false;
  if (isDistributedThunk())
    return false;
  auto fnDecl = dyn_cast<AbstractFunctionDecl>(getDecl());
  if (!fnDecl)
    return false;
  if (fnDecl->needsNewVTableEntry())
    return true;
  return false;
}

bool SILDeclRef::requiresNewWitnessTableEntry() const {
  return requiresNewWitnessTableEntry(cast<AbstractFunctionDecl>(getDecl()));
}

bool SILDeclRef::requiresNewWitnessTableEntry(AbstractFunctionDecl *func) {
  return func->getOverriddenDecls().empty();
}

SILDeclRef SILDeclRef::getOverridden() const {
  if (!hasDecl())
    return SILDeclRef();
  auto overridden = getDecl()->getOverriddenDecl();
  if (!overridden)
    return SILDeclRef();
  return withDecl(overridden);
}

SILDeclRef SILDeclRef::getNextOverriddenVTableEntry() const {
  if (auto overridden = getOverridden()) {
    // If we overrode a foreign decl or dynamic method, if this is an
    // accessor for a property that overrides an ObjC decl, or if it is an
    // @NSManaged property, then it won't be in the vtable.
    if (overridden.getDecl()->hasClangNode())
      return SILDeclRef();

    // Distributed thunks are not in the vtable.
    if (isDistributedThunk())
      return SILDeclRef();
    
    // An @objc convenience initializer can be "overridden" in the sense that
    // its selector is reclaimed by a subclass's convenience init with the
    // same name. The AST models this as an override for the purposes of
    // ObjC selector validation, but it isn't for Swift method dispatch
    // purposes.
    if (overridden.kind == SILDeclRef::Kind::Allocator) {
      auto overriddenCtor = cast<ConstructorDecl>(overridden.getDecl());
      if (!overriddenCtor->isDesignatedInit()
          && !overriddenCtor->isRequired())
        return SILDeclRef();
    }

    // Initializing entry points for initializers won't be in the vtable.
    // For Swift designated initializers, they're only used in super.init
    // chains, which can always be statically resolved. Other native Swift
    // initializers only have allocating entry points. ObjC initializers always
    // have the initializing entry point (corresponding to the -init method)
    // but those are never in the vtable.
    if (overridden.kind == SILDeclRef::Kind::Initializer) {
      return SILDeclRef();
    }

    // Overrides of @objc dynamic declarations are not in the vtable.
    if (overridden.getDecl()->shouldUseObjCDispatch()) {
      return SILDeclRef();
    }

    if (auto *accessor = dyn_cast<AccessorDecl>(overridden.getDecl())) {
      auto *asd = accessor->getStorage();
      if (asd->hasClangNode())
        return SILDeclRef();
      if (asd->shouldUseObjCDispatch()) {
        return SILDeclRef();
      }
    }

    // If we overrode a decl from an extension, it won't be in a vtable
    // either. This can occur for extensions to ObjC classes.
    if (isa<ExtensionDecl>(overridden.getDecl()->getDeclContext()))
      return SILDeclRef();

    // JVPs/VJPs are overridden only if the base declaration has a
    // `@differentiable` attribute with the same parameter indices.
    if (getDerivativeFunctionIdentifier()) {
      auto overriddenAttrs =
          overridden.getDecl()->getAttrs().getAttributes<DifferentiableAttr>();
      for (const auto *attr : overriddenAttrs) {
        if (attr->getParameterIndices() !=
            getDerivativeFunctionIdentifier()->getParameterIndices())
          continue;
        auto *overriddenDerivativeId =
            overridden.getDerivativeFunctionIdentifier();
        overridden.pointer =
            AutoDiffDerivativeFunctionIdentifier::get(
                overriddenDerivativeId->getKind(),
                overriddenDerivativeId->getParameterIndices(),
                attr->getDerivativeGenericSignature(),
                getDecl()->getASTContext());
        return overridden;
      }
      return SILDeclRef();
    }
    return overridden;
  }
  return SILDeclRef();
}

SILDeclRef SILDeclRef::getOverriddenWitnessTableEntry() const {
  auto bestOverridden =
    getOverriddenWitnessTableEntry(cast<AbstractFunctionDecl>(getDecl()));
  return withDecl(bestOverridden);
}

AbstractFunctionDecl *SILDeclRef::getOverriddenWitnessTableEntry(
                                                 AbstractFunctionDecl *func) {
  if (!isa<ProtocolDecl>(func->getDeclContext()))
    return func;

  AbstractFunctionDecl *bestOverridden = nullptr;

  SmallVector<AbstractFunctionDecl *, 4> stack;
  SmallPtrSet<AbstractFunctionDecl *, 4> visited;
  stack.push_back(func);
  visited.insert(func);

  while (!stack.empty()) {
    auto current = stack.back();
    stack.pop_back();

    auto overriddenDecls = current->getOverriddenDecls();
    if (overriddenDecls.empty()) {
      // This entry introduced a witness table entry. Determine whether it is
      // better than the best entry we've seen thus far.
      if (!bestOverridden ||
          ProtocolDecl::compare(
                        cast<ProtocolDecl>(current->getDeclContext()),
                        cast<ProtocolDecl>(bestOverridden->getDeclContext()))
            < 0) {
        bestOverridden = cast<AbstractFunctionDecl>(current);
      }

      continue;
    }

    // Add overridden declarations to the stack.
    for (auto overridden : overriddenDecls) {
      auto overriddenFunc = cast<AbstractFunctionDecl>(overridden);
      if (visited.insert(overriddenFunc).second)
        stack.push_back(overriddenFunc);
    }
  }

  return bestOverridden;
}

SILDeclRef SILDeclRef::getOverriddenVTableEntry() const {
  SILDeclRef cur = *this, next = *this;
  do {
    cur = next;
    if (cur.requiresNewVTableEntry())
      return cur;
    next = cur.getNextOverriddenVTableEntry();
  } while (next);

  return cur;
}

SILLocation SILDeclRef::getAsRegularLocation() const {
  switch (getLocKind()) {
  case LocKind::Decl:
    return RegularLocation(getDecl());
  case LocKind::Closure:
    return RegularLocation(getAbstractClosureExpr());
  case LocKind::File:
    return RegularLocation::getModuleLocation();
  }
  llvm_unreachable("Unhandled case in switch");
}

SubclassScope SILDeclRef::getSubclassScope() const {
  if (!hasDecl())
    return SubclassScope::NotApplicable;

  auto *decl = getDecl();

  if (!isa<AbstractFunctionDecl>(decl))
    return SubclassScope::NotApplicable;

  DeclContext *context = decl->getDeclContext();

  // Only methods in non-final classes go in the vtable.
  auto *classType = dyn_cast<ClassDecl>(context);
  if (!classType || classType->isFinal())
    return SubclassScope::NotApplicable;

  // If a method appears in the vtable of a class, we must give it's symbol
  // special consideration when computing visibility because the SIL-level
  // linkage does not map to the symbol's visibility in a straightforward
  // way.
  //
  // In particular, the rules are:
  // - If the class metadata is not resilient, then all method symbols must
  //   be visible from any translation unit where a subclass might be defined,
  //   because the subclass metadata will re-emit all vtable entries.
  //
  // - For resilient classes, we do the opposite: generally, a method's symbol
  //   can be hidden from other translation units, because we want to enforce
  //   that resilient access patterns are used for method calls and overrides.
  //
  //   Constructors and final methods are the exception here, because they can
  //   be called directly.

  // FIXME: This is too narrow. Any class with resilient metadata should
  // probably have this, at least for method overrides that don't add new
  // vtable entries.
  bool isResilientClass = classType->isResilient();

  if (auto *CD = dyn_cast<ConstructorDecl>(decl)) {
    if (isResilientClass)
      return SubclassScope::NotApplicable;
    // Initializing entry points do not appear in the vtable.
    if (kind == SILDeclRef::Kind::Initializer)
      return SubclassScope::NotApplicable;
    // Non-required convenience inits do not appear in the vtable.
    if (!CD->isRequired() && !CD->isDesignatedInit())
      return SubclassScope::NotApplicable;
  } else if (isa<DestructorDecl>(decl)) {
    // Destructors do not appear in the vtable.
    return SubclassScope::NotApplicable;
  } else {
    assert(isa<FuncDecl>(decl));
  }

  // Various forms of thunks don't go in the vtable.
  if (isThunk() || isForeign)
    return SubclassScope::NotApplicable;

  // Default arg generators don't go in the vtable.
  if (isDefaultArgGenerator())
    return SubclassScope::NotApplicable;

  if (decl->isFinal()) {
    // Final methods only go in the vtable if they override something.
    if (!decl->getOverriddenDecl())
      return SubclassScope::NotApplicable;

    // In the resilient case, we're going to be making symbols _less_
    // visible, so make sure we stop now; final methods can always be
    // called directly.
    if (isResilientClass)
      return SubclassScope::Internal;
  }

  assert(decl->getEffectiveAccess() <= classType->getEffectiveAccess() &&
         "class must be as visible as its members");

  if (isResilientClass) {
    // The symbol should _only_ be reached via the vtable, so we're
    // going to make it hidden.
    return SubclassScope::Resilient;
  }

  switch (classType->getEffectiveAccess()) {
  case AccessLevel::Private:
  case AccessLevel::FilePrivate:
    // If the class is private, it can only be subclassed from the same
    // SILModule, so we don't need to do anything.
    return SubclassScope::NotApplicable;
  case AccessLevel::Internal:
  case AccessLevel::Public:
    // If the class is internal or public, it can only be subclassed from
    // the same AST Module, but possibly a different SILModule.
    return SubclassScope::Internal;
  case AccessLevel::Open:
    // If the class is open, it can be subclassed from a different
    // AST Module. All method symbols are public.
    return SubclassScope::External;
  }

  llvm_unreachable("Unhandled access level in switch.");
}

unsigned SILDeclRef::getParameterListCount() const {
  // Only decls can introduce currying.
  if (!hasDecl())
    return 1;

  // Always uncurried even if the underlying function is curried.
  if (kind == Kind::DefaultArgGenerator || kind == Kind::EntryPoint ||
      kind == Kind::AsyncEntryPoint)
    return 1;

  auto *vd = getDecl();

  if (isa<AbstractFunctionDecl>(vd) || isa<EnumElementDecl>(vd)) {
    // For functions and enum elements, the number of parameter lists is the
    // same as in their interface type.
    return vd->getNumCurryLevels();
  } else if (isa<ClassDecl>(vd)) {
    return 2;
  } else if (isa<VarDecl>(vd)) {
    return 1;
  } else {
    llvm_unreachable("Unhandled ValueDecl for SILDeclRef");
  }
}

static bool isDesignatedConstructorForClass(ValueDecl *decl) {
  if (auto *ctor = dyn_cast_or_null<ConstructorDecl>(decl))
    if (ctor->getDeclContext()->getSelfClassDecl())
      return ctor->isDesignatedInit();
  return false;
}

bool SILDeclRef::canBeDynamicReplacement() const {
  // The foreign entry of a @dynamicReplacement(for:) of @objc method in a
  // generic class can't be a dynamic replacement.
  if (isForeign && hasDecl() && getDecl()->isNativeMethodReplacement())
    return false;
  if (isDistributedThunk())
    return false;
  if (kind == SILDeclRef::Kind::Destroyer ||
      kind == SILDeclRef::Kind::DefaultArgGenerator)
    return false;
  if (kind == SILDeclRef::Kind::Initializer)
    return isDesignatedConstructorForClass(getDecl());
  if (kind == SILDeclRef::Kind::Allocator)
    return !isDesignatedConstructorForClass(getDecl());
  return true;
}

bool SILDeclRef::isDynamicallyReplaceable() const {
  // The non-foreign entry of a @dynamicReplacement(for:) of @objc method in a
  // generic class can't be a dynamically replaced.
  if (!isForeign && hasDecl() && getDecl()->isNativeMethodReplacement())
    return false;

  if (isDistributedThunk())
    return false;

  if (kind == SILDeclRef::Kind::DefaultArgGenerator)
    return false;
  if (isStoredPropertyInitializer() || isPropertyWrapperBackingInitializer())
    return false;

  // Class allocators are not dynamic replaceable.
  if (kind == SILDeclRef::Kind::Allocator &&
      isDesignatedConstructorForClass(getDecl()))
    return false;

  if (kind == SILDeclRef::Kind::Destroyer ||
      (kind == SILDeclRef::Kind::Initializer &&
       !isDesignatedConstructorForClass(getDecl())) ||
      kind == SILDeclRef::Kind::GlobalAccessor) {
    return false;
  }

  if (!hasDecl())
    return false;

  auto decl = getDecl();

  if (isForeign)
    return false;

  // We can't generate categories for generic classes. So the standard mechanism
  // for replacing @objc dynamic methods in generic classes does not work.
  // Instead we mark the non @objc entry dynamically replaceable and replace
  // that.
  // For now, we only support this behavior if -enable-implicit-dynamic is
  // enabled.
  return decl->shouldUseNativeMethodReplacement();
}

bool SILDeclRef::hasAsync() const {
  if (isDistributedThunk())
    return true;

  if (hasDecl()) {
    if (auto afd = dyn_cast<AbstractFunctionDecl>(getDecl())) {
      return afd->hasAsync();
    }
    return false;
  }
  return getAbstractClosureExpr()->isBodyAsync();
}
