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
#include "swift/SIL/SILLocation.h"
#include "swift/AST/AnyFunctionRef.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/ASTMangler.h"
#include "swift/AST/Initializer.h"
#include "swift/AST/ParameterList.h"
#include "swift/ClangImporter/ClangImporter.h"
#include "swift/ClangImporter/ClangModule.h"
#include "swift/SIL/SILLinkage.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
using namespace swift;

/// Get the method dispatch mechanism for a method.
MethodDispatch
swift::getMethodDispatch(AbstractFunctionDecl *method) {
  // Final methods can be statically referenced.
  if (method->isFinal())
    return MethodDispatch::Static;
  // Some methods are forced to be statically dispatched.
  if (method->hasForcedStaticDispatch())
    return MethodDispatch::Static;

  // Import-as-member declarations are always statically referenced.
  if (method->isImportAsMember())
    return MethodDispatch::Static;

  // If this declaration is in a class but not marked final, then it is
  // always dynamically dispatched.
  auto dc = method->getDeclContext();
  if (isa<ClassDecl>(dc))
    return MethodDispatch::Class;

  // Class extension methods are only dynamically dispatched if they're
  // dispatched by objc_msgSend, which happens if they're foreign or dynamic.
  if (dc->getAsClassOrClassExtensionContext()) {
    if (method->hasClangNode())
      return MethodDispatch::Class;
    if (auto fd = dyn_cast<FuncDecl>(method)) {
      if (fd->isAccessor() && fd->getAccessorStorageDecl()->hasClangNode())
        return MethodDispatch::Class;
    }
    if (method->isDynamic())
      return MethodDispatch::Class;
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

/// FIXME: merge requiresForeignEntryPoint() into getMethodDispatch() and add
/// an ObjectiveC case to the MethodDispatch enum.
bool swift::requiresForeignEntryPoint(ValueDecl *vd) {
  if (vd->isImportAsMember())
    return true;

  // Final functions never require ObjC dispatch.
  if (vd->isFinal())
    return false;

  if (requiresForeignToNativeThunk(vd))
    return true;

  if (auto *fd = dyn_cast<FuncDecl>(vd)) {
  
    // Property accessors should be generated alongside the property.
    if (fd->isGetterOrSetter())
      return requiresForeignEntryPoint(fd->getAccessorStorageDecl());

    return fd->isDynamic();
  }

  if (auto *cd = dyn_cast<ConstructorDecl>(vd)) {
    if (cd->hasClangNode())
      return true;

    return cd->isDynamic();
  }

  if (auto *asd = dyn_cast<AbstractStorageDecl>(vd))
    return asd->requiresForeignGetterAndSetter();

  return vd->isDynamic();
}

/// TODO: We should consult the cached LoweredLocalCaptures the SIL
/// TypeConverter calculates, but that would require plumbing SILModule&
/// through every SILDeclRef constructor. Since this is only used to determine
/// "natural uncurry level", and "uncurry level" is a concept we'd like to
/// phase out, it's not worth it.
static bool hasLoweredLocalCaptures(AnyFunctionRef AFR,
                                    llvm::DenseSet<AnyFunctionRef> &visited) {
  if (!AFR.getCaptureInfo().hasLocalCaptures())
    return false;
  
  // Scan for local, non-function captures.
  bool functionCapturesToRecursivelyCheck = false;
  auto addFunctionCapture = [&](AnyFunctionRef capture) {
    if (visited.find(capture) == visited.end())
      functionCapturesToRecursivelyCheck = true;
  };
  for (auto &capture : AFR.getCaptureInfo().getCaptures()) {
    if (!capture.getDecl()->getDeclContext()->isLocalContext())
      continue;
    // We transitively capture a local function's captures.
    if (auto func = dyn_cast<AbstractFunctionDecl>(capture.getDecl())) {
      addFunctionCapture(func);
      continue;
    }
    // We may either directly capture properties, or capture through their
    // accessors.
    if (auto var = dyn_cast<VarDecl>(capture.getDecl())) {
      switch (var->getStorageKind()) {
      case VarDecl::StoredWithTrivialAccessors:
        llvm_unreachable("stored local variable with trivial accessors?");

      case VarDecl::InheritedWithObservers:
        llvm_unreachable("inherited local variable?");

      case VarDecl::StoredWithObservers:
      case VarDecl::Addressed:
      case VarDecl::AddressedWithTrivialAccessors:
      case VarDecl::AddressedWithObservers:
      case VarDecl::ComputedWithMutableAddress:
        // Directly capture storage if we're supposed to.
        if (capture.isDirect())
          return true;

        // Otherwise, transitively capture the accessors.
        LLVM_FALLTHROUGH;

      case VarDecl::Computed:
        addFunctionCapture(var->getGetter());
        if (auto setter = var->getSetter())
          addFunctionCapture(setter);
        continue;
      
      case VarDecl::Stored:
        return true;
      }
    }
    // Anything else is directly captured.
    return true;
  }
  
  // Recursively consider function captures, since we didn't have any direct
  // captures.
  auto captureHasLocalCaptures = [&](AnyFunctionRef capture) -> bool {
    if (visited.insert(capture).second)
      return hasLoweredLocalCaptures(capture, visited);
    return false;
  };
  
  if (functionCapturesToRecursivelyCheck) {
    for (auto &capture : AFR.getCaptureInfo().getCaptures()) {
      if (!capture.getDecl()->getDeclContext()->isLocalContext())
        continue;
      if (auto func = dyn_cast<AbstractFunctionDecl>(capture.getDecl())) {
        if (captureHasLocalCaptures(func))
          return true;
        continue;
      }
      if (auto var = dyn_cast<VarDecl>(capture.getDecl())) {
        switch (var->getStorageKind()) {
        case VarDecl::StoredWithTrivialAccessors:
          llvm_unreachable("stored local variable with trivial accessors?");
          
        case VarDecl::InheritedWithObservers:
          llvm_unreachable("inherited local variable?");
          
        case VarDecl::StoredWithObservers:
        case VarDecl::Addressed:
        case VarDecl::AddressedWithTrivialAccessors:
        case VarDecl::AddressedWithObservers:
        case VarDecl::ComputedWithMutableAddress:
          assert(!capture.isDirect() && "should have short circuited out");
          // Otherwise, transitively capture the accessors.
          LLVM_FALLTHROUGH;
          
        case VarDecl::Computed:
          if (captureHasLocalCaptures(var->getGetter()))
            return true;
          if (auto setter = var->getSetter())
            if (captureHasLocalCaptures(setter))
              return true;
          continue;
        
        case VarDecl::Stored:
          llvm_unreachable("should have short circuited out");
        }
      }
      llvm_unreachable("should have short circuited out");
    }
  }
  
  return false;
}

SILDeclRef::SILDeclRef(ValueDecl *vd, SILDeclRef::Kind kind,
                       ResilienceExpansion expansion,
                       bool isCurried, bool isForeign)
  : loc(vd), kind(kind), Expansion(unsigned(expansion)),
    isCurried(isCurried), isForeign(isForeign),
    isDirectReference(0), defaultArgIndex(0)
{}

SILDeclRef::SILDeclRef(SILDeclRef::Loc baseLoc,
                       ResilienceExpansion expansion,
                       bool isCurried, bool asForeign) 
  : isCurried(isCurried), isDirectReference(0), defaultArgIndex(0)
{
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
    assert(ACE->getParameterLists().size() >= 1 &&
           "no param patterns for function?!");
  } else {
    llvm_unreachable("impossible SILDeclRef loc");
  }

  Expansion = (unsigned) expansion;
  isForeign = asForeign;
}

Optional<AnyFunctionRef> SILDeclRef::getAnyFunctionRef() const {
  if (auto vd = loc.dyn_cast<ValueDecl*>()) {
    if (auto afd = dyn_cast<AbstractFunctionDecl>(vd)) {
      return AnyFunctionRef(afd);
    } else {
      return None;
    }
  }
  return AnyFunctionRef(loc.get<AbstractClosureExpr*>());
}

bool SILDeclRef::isThunk() const {
  return isCurried || isForeignToNativeThunk() || isNativeToForeignThunk();
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
      if (FD->isAccessor() ||
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
  if (hasDecl())
    return getDecl()->isImplicit();
  return getAbstractClosureExpr()->isImplicit();
}

SILLinkage SILDeclRef::getLinkage(ForDefinition_t forDefinition) const {
  if (getAbstractClosureExpr()) {
    if (isSerialized())
      return SILLinkage::Shared;
    return SILLinkage::Private;
  }

  // Add External to the linkage (e.g. Public -> PublicExternal) if this is a
  // declaration not a definition.
  auto maybeAddExternal = [&](SILLinkage linkage) {
    return forDefinition ? linkage : addExternalToLinkage(linkage);
  };

  // Native function-local declarations have shared linkage.
  // FIXME: @objc declarations should be too, but we currently have no way
  // of marking them "used" other than making them external. 
  ValueDecl *d = getDecl();
  DeclContext *moduleContext = d->getDeclContext();
  while (!moduleContext->isModuleScopeContext()) {
    if (!isForeign && moduleContext->isLocalContext()) {
      if (isSerialized())
        return SILLinkage::Shared;
      return SILLinkage::Private;
    }
    moduleContext = moduleContext->getParent();
  }

  // Enum constructors and curry thunks either have private or shared
  // linkage, dependings are essentially the same as thunks, they are
  // emitted by need and have shared linkage.
  if (isEnumElement() || isCurried) {
    switch (d->getEffectiveAccess()) {
    case AccessLevel::Private:
    case AccessLevel::FilePrivate:
      return maybeAddExternal(SILLinkage::Private);

    default:
      return SILLinkage::Shared;
    }
  }

  // ivar initializers and destroyers are completely contained within the class
  // from which they come, and never get seen externally.
  if (isIVarInitializerOrDestroyer()) {
    switch (d->getEffectiveAccess()) {
    case AccessLevel::Private:
    case AccessLevel::FilePrivate:
      return maybeAddExternal(SILLinkage::Private);
    default:
      return maybeAddExternal(SILLinkage::Hidden);
    }
  }

  // Calling convention thunks have shared linkage.
  if (isForeignToNativeThunk())
    return SILLinkage::Shared;

  // If a function declares a @_cdecl name, its native-to-foreign thunk
  // is exported with the visibility of the function.
  if (isNativeToForeignThunk() && !d->getAttrs().hasAttribute<CDeclAttr>())
    return SILLinkage::Shared;

  // Declarations imported from Clang modules have shared linkage.
  if (isClangImported())
    return SILLinkage::Shared;

  // Stored property initializers get the linkage of their containing type.
  if (isStoredPropertyInitializer()) {
    // If the property is public, the initializer needs to be public, because
    // it might be referenced from an inlineable initializer.
    //
    // Note that we don't serialize the presence of an initializer, so there's
    // no way to reference one from another module except for this case.
    //
    // This is silly, and we need a proper resilience story here.
    if (d->getEffectiveAccess() == AccessLevel::Public)
      return maybeAddExternal(SILLinkage::Public);

    d = cast<NominalTypeDecl>(d->getDeclContext());

    // Otherwise, use the visibility of the type itself, because even if the
    // property is private, we might reference the initializer from another
    // file.
    switch (d->getEffectiveAccess()) {
    case AccessLevel::Private:
    case AccessLevel::FilePrivate:
      return maybeAddExternal(SILLinkage::Private);

    default:
      return maybeAddExternal(SILLinkage::Hidden);
    }
  }

  // Otherwise, we have external linkage.
  switch (d->getEffectiveAccess()) {
    case AccessLevel::Private:
    case AccessLevel::FilePrivate:
      return maybeAddExternal(SILLinkage::Private);

    case AccessLevel::Internal:
      return maybeAddExternal(SILLinkage::Hidden);

    default:
      return maybeAddExternal(SILLinkage::Public);
  }
}

SILDeclRef SILDeclRef::getDefaultArgGenerator(Loc loc,
                                              unsigned defaultArgIndex) {
  SILDeclRef result;
  result.loc = loc;
  result.kind = Kind::DefaultArgGenerator;
  result.defaultArgIndex = defaultArgIndex;
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
  if (!hasFuncDecl())
    return false;
  return getFuncDecl()->isSetter();
}

AbstractFunctionDecl *SILDeclRef::getAbstractFunctionDecl() const {
  return dyn_cast<AbstractFunctionDecl>(getDecl());
}

/// \brief True if the function should be treated as transparent.
bool SILDeclRef::isTransparent() const {
  if (isEnumElement())
    return true;

  if (isStoredPropertyInitializer())
    return true;

  if (hasAutoClosureExpr())
    return true;

  if (hasDecl()) {
    if (auto *AFD = dyn_cast<AbstractFunctionDecl>(getDecl()))
      return AFD->isTransparent();

    if (auto *ASD = dyn_cast<AbstractStorageDecl>(getDecl()))
      return ASD->isTransparent();
  }

  return false;
}

/// \brief True if the function should have its body serialized.
IsSerialized_t SILDeclRef::isSerialized() const {
  DeclContext *dc;
  if (auto closure = getAbstractClosureExpr())
    dc = closure->getLocalContext();
  else {
    auto *d = getDecl();

    // Default argument generators are serialized if the function was
    // type-checked in Swift 4 mode.
    if (kind == SILDeclRef::Kind::DefaultArgGenerator) {
      auto *afd = cast<AbstractFunctionDecl>(d);
      switch (afd->getDefaultArgumentResilienceExpansion()) {
      case ResilienceExpansion::Minimal:
        return IsSerialized;
      case ResilienceExpansion::Maximal:
        return IsNotSerialized;
      }
    }

    dc = getDecl()->getInnermostDeclContext();

    // Enum element constructors are serialized if the enum is
    // @_versioned or public.
    if (isEnumElement())
      if (d->getEffectiveAccess() >= AccessLevel::Public)
        return IsSerialized;

    // Currying thunks are serialized if referenced from an inlinable
    // context -- Sema's semantic checks ensure the serialization of
    // such a thunk is valid, since it must in turn reference a public
    // symbol, or dispatch via class_method or witness_method.
    if (isCurried)
      if (d->getEffectiveAccess() >= AccessLevel::Public)
        return IsSerializable;

    if (isForeignToNativeThunk())
      return IsSerializable;

    // The allocating entry point for designated initializers are serialized
    // if the class is @_versioned or public.
    if (kind == SILDeclRef::Kind::Allocator) {
      auto *ctor = cast<ConstructorDecl>(d);
      if (ctor->isDesignatedInit() &&
          ctor->getDeclContext()->getAsClassOrClassExtensionContext()) {
        if (ctor->getEffectiveAccess() >= AccessLevel::Public &&
            !ctor->hasClangNode())
          return IsSerialized;
      }
    }
  }

  // Declarations imported from Clang modules are serialized if
  // referenced from an inlineable context.
  if (isClangImported())
    return IsSerializable;

  // Otherwise, ask the AST if we're inside an @_inlineable context.
  if (dc->getResilienceExpansion() == ResilienceExpansion::Minimal)
    return IsSerialized;

  return IsNotSerialized;
}

/// \brief True if the function has noinline attribute.
bool SILDeclRef::isNoinline() const {
  if (!hasDecl())
    return false;
  if (auto InlineA = getDecl()->getAttrs().getAttribute<InlineAttr>())
    if (InlineA->getKind() == InlineKind::Never)
      return true;
   return false;
}

/// \brief True if the function has noinline attribute.
bool SILDeclRef::isAlwaysInline() const {
  if (!hasDecl())
    return false;
  if (auto InlineA = getDecl()->getAttrs().getAttribute<InlineAttr>())
    if (InlineA->getKind() == InlineKind::Always)
      return true;
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

bool SILDeclRef::isForeignToNativeThunk() const {
  // Non-decl entry points are never natively foreign, so they would never
  // have a foreign-to-native thunk.
  if (!hasDecl())
    return false;
  if (requiresForeignToNativeThunk(getDecl()))
    return !isForeign;
  // ObjC initializing constructors and factories are foreign.
  // We emit a special native allocating constructor though.
  if (isa<ConstructorDecl>(getDecl())
      && (kind == Kind::Initializer
          || cast<ConstructorDecl>(getDecl())->isFactoryInit())
      && getDecl()->hasClangNode())
    return !isForeign;
  return false;
}

bool SILDeclRef::isNativeToForeignThunk() const {
  // We can have native-to-foreign thunks over closures.
  if (!hasDecl())
    return isForeign;
  // We can have native-to-foreign thunks over global or local native functions.
  // TODO: Static functions too.
  if (auto func = dyn_cast<FuncDecl>(getDecl())) {
    if (!func->getDeclContext()->isTypeContext()
        && !func->hasClangNode())
      return isForeign;
  }
  return false;
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

  // As a special case, Clang functions and globals don't get mangled at all.
  if (hasDecl()) {
    if (auto clangDecl = getDecl()->getClangDecl()) {
      if (!isForeignToNativeThunk() && !isNativeToForeignThunk()
          && !isCurried) {
        if (auto namedClangDecl = dyn_cast<clang::DeclaratorDecl>(clangDecl)) {
          if (auto asmLabel = namedClangDecl->getAttr<clang::AsmLabelAttr>()) {
            std::string s(1, '\01');
            s += asmLabel->getLabel();
            return s;
          } else if (namedClangDecl->hasAttr<clang::OverloadableAttr>()) {
            std::string storage;
            llvm::raw_string_ostream SS(storage);
            // FIXME: When we can import C++, use Clang's mangler all the time.
            mangleClangDecl(SS, namedClangDecl, getDecl()->getASTContext());
            return SS.str();
          }
          return namedClangDecl->getName();
        }
      }
    }
  }

  ASTMangler::SymbolKind SKind = ASTMangler::SymbolKind::Default;
  switch (MKind) {
    case SILDeclRef::ManglingKind::Default:
      if (isForeign) {
        SKind = ASTMangler::SymbolKind::SwiftAsObjCThunk;
      } else if (isDirectReference) {
        SKind = ASTMangler::SymbolKind::DirectMethodReferenceThunk;
      } else if (isForeignToNativeThunk()) {
        SKind = ASTMangler::SymbolKind::ObjCAsSwiftThunk;
      }
      break;
    case SILDeclRef::ManglingKind::DynamicThunk:
      SKind = ASTMangler::SymbolKind::DynamicThunk;
      break;
    case SILDeclRef::ManglingKind::SwiftDispatchThunk:
      assert(!isForeign && !isDirectReference && !isCurried);
      SKind = ASTMangler::SymbolKind::SwiftDispatchThunk;
      break;
  }

  switch (kind) {
  case SILDeclRef::Kind::Func:
    if (!hasDecl())
      return mangler.mangleClosureEntity(getAbstractClosureExpr(), SKind);

    // As a special case, functions can have manually mangled names.
    // Use the SILGen name only for the original non-thunked, non-curried entry
    // point.
    if (auto NameA = getDecl()->getAttrs().getAttribute<SILGenNameAttr>())
      if (!NameA->Name.empty() &&
          !isForeignToNativeThunk() && !isNativeToForeignThunk()
          && !isCurried) {
        return NameA->Name;
      }
      
    // Use a given cdecl name for native-to-foreign thunks.
    if (auto CDeclA = getDecl()->getAttrs().getAttribute<CDeclAttr>())
      if (isNativeToForeignThunk()) {
        return CDeclA->Name;
      }

    // Otherwise, fall through into the 'other decl' case.
    LLVM_FALLTHROUGH;

  case SILDeclRef::Kind::EnumElement:
    return mangler.mangleEntity(getDecl(), isCurried, SKind);

  case SILDeclRef::Kind::Deallocator:
    assert(!isCurried);
    return mangler.mangleDestructorEntity(cast<DestructorDecl>(getDecl()),
                                          /*isDeallocating*/ true,
                                          SKind);

  case SILDeclRef::Kind::Destroyer:
    assert(!isCurried);
    return mangler.mangleDestructorEntity(cast<DestructorDecl>(getDecl()),
                                          /*isDeallocating*/ false,
                                          SKind);

  case SILDeclRef::Kind::Allocator:
    return mangler.mangleConstructorEntity(cast<ConstructorDecl>(getDecl()),
                                           /*allocating*/ true,
                                           isCurried,
                                           SKind);

  case SILDeclRef::Kind::Initializer:
    return mangler.mangleConstructorEntity(cast<ConstructorDecl>(getDecl()),
                                           /*allocating*/ false,
                                           isCurried,
                                           SKind);

  case SILDeclRef::Kind::IVarInitializer:
  case SILDeclRef::Kind::IVarDestroyer:
    assert(!isCurried);
    return mangler.mangleIVarInitDestroyEntity(cast<ClassDecl>(getDecl()),
                                  kind == SILDeclRef::Kind::IVarDestroyer,
                                  SKind);

  case SILDeclRef::Kind::GlobalAccessor:
    assert(!isCurried);
    return mangler.mangleAccessorEntity(AccessorKind::IsMutableAddressor,
                                        AddressorKind::Unsafe,
                                        cast<AbstractStorageDecl>(getDecl()),
                                        /*isStatic*/ false,
                                        SKind);

  case SILDeclRef::Kind::GlobalGetter:
    assert(!isCurried);
    return mangler.mangleGlobalGetterEntity(getDecl(), SKind);

  case SILDeclRef::Kind::DefaultArgGenerator:
    assert(!isCurried);
    return mangler.mangleDefaultArgumentEntity(
                                        cast<AbstractFunctionDecl>(getDecl()),
                                        defaultArgIndex,
                                        SKind);

  case SILDeclRef::Kind::StoredPropertyInitializer:
    assert(!isCurried);
    return mangler.mangleInitializerEntity(cast<VarDecl>(getDecl()), SKind);
  }

  llvm_unreachable("bad entity kind!");
}

bool SILDeclRef::requiresNewVTableEntry() const {
  if (cast<AbstractFunctionDecl>(getDecl())->needsNewVTableEntry())
    return true;
  if (kind == SILDeclRef::Kind::Allocator) {
    auto *cd = cast<ConstructorDecl>(getDecl());
    if (cd->isRequired()) {
      auto *baseCD = cd->getOverriddenDecl();
      if(!baseCD ||
         !baseCD->isRequired() ||
         baseCD->hasClangNode())
        return true;
    }
  }
  return false;
}

SILDeclRef SILDeclRef::getOverridden() const {
  if (!hasDecl())
    return SILDeclRef();
  auto overridden = getDecl()->getOverriddenDecl();
  if (!overridden)
    return SILDeclRef();

  return SILDeclRef(overridden, kind, getResilienceExpansion(), isCurried);
}

SILDeclRef SILDeclRef::getNextOverriddenVTableEntry() const {
  if (auto overridden = getOverridden()) {
    // If we overrode a foreign decl, a dynamic method, this is an
    // accessor for a property that overrides an ObjC decl, or if it is an
    // @NSManaged property, then it won't be in the vtable.
    if (overridden.getDecl()->hasClangNode())
      return SILDeclRef();

    // If we overrode a non-required initializer, there won't be a vtable
    // slot for the allocator.
    if (overridden.kind == SILDeclRef::Kind::Allocator) {
      if (!cast<ConstructorDecl>(overridden.getDecl())->isRequired())
        return SILDeclRef();
    } else if (overridden.getDecl()->isDynamic()) {
      return SILDeclRef();
    }
    
    if (auto *ovFD = dyn_cast<FuncDecl>(overridden.getDecl()))
      if (auto *asd = ovFD->getAccessorStorageDecl()) {
        if (asd->hasClangNode())
          return SILDeclRef();
        if (asd->isDynamic())
          return SILDeclRef();
      }

    // If we overrode a decl from an extension, it won't be in a vtable
    // either. This can occur for extensions to ObjC classes.
    if (isa<ExtensionDecl>(overridden.getDecl()->getDeclContext()))
      return SILDeclRef();

    return overridden;
  }
  return SILDeclRef();
}

SILLocation SILDeclRef::getAsRegularLocation() const {
  if (hasDecl())
    return RegularLocation(getDecl());
  return RegularLocation(getAbstractClosureExpr());
}

SubclassScope SILDeclRef::getSubclassScope() const {
  if (!hasDecl())
    return SubclassScope::NotApplicable;

  // If this declaration is a function which goes into a vtable, then it's
  // symbol must be as visible as its class. Derived classes even have to put
  // all less visible methods of the base class into their vtables.

  auto *FD = dyn_cast<AbstractFunctionDecl>(getDecl());
  if (!FD)
    return SubclassScope::NotApplicable;

  DeclContext *context = FD->getDeclContext();

  // Methods from extensions don't go into vtables (yet).
  if (context->isExtensionContext())
    return SubclassScope::NotApplicable;

  auto *classType = context->getAsClassOrClassExtensionContext();
  if (!classType || classType->isFinal())
    return SubclassScope::NotApplicable;

  if (FD->isFinal())
    return SubclassScope::NotApplicable;

  assert(FD->getEffectiveAccess() <= classType->getEffectiveAccess() &&
         "class must be as visible as its members");

  switch (classType->getEffectiveAccess()) {
  case AccessLevel::Private:
  case AccessLevel::FilePrivate:
    return SubclassScope::NotApplicable;
  case AccessLevel::Internal:
  case AccessLevel::Public:
    return SubclassScope::Internal;
  case AccessLevel::Open:
    return SubclassScope::External;
  }

  llvm_unreachable("Unhandled access level in switch.");
}

unsigned SILDeclRef::getParameterListCount() const {
  if (isCurried || !hasDecl() || kind == Kind::DefaultArgGenerator)
    return 1;

  auto *vd = getDecl();

  if (auto *func = dyn_cast<AbstractFunctionDecl>(vd)) {
    return func->getParameterLists().size();
  } else if (auto *ed = dyn_cast<EnumElementDecl>(vd)) {
    return ed->hasAssociatedValues() ? 2 : 1;
  } else if (isa<DestructorDecl>(vd)) {
    return 1;
  } else if (isa<ClassDecl>(vd)) {
    return 2;
  } else if (isa<VarDecl>(vd)) {
    return 1;
  } else {
    llvm_unreachable("Unhandled ValueDecl for SILDeclRef");
  }
}
