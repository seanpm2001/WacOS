//===--- CodeSynthesis.cpp - Type Checking for Declarations ---------------===//
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
// This file implements semantic analysis for declarations.
//
//===----------------------------------------------------------------------===//

#include "CodeSynthesis.h"

#include "TypeChecker.h"
#include "TypeCheckDecl.h"
#include "TypeCheckObjC.h"
#include "TypeCheckType.h"
#include "TypeCheckDistributed.h"
#include "swift/AST/ASTPrinter.h"
#include "swift/AST/Availability.h"
#include "swift/AST/Expr.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Initializer.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/SourceFile.h"
#include "swift/AST/TypeCheckRequests.h"
#include "swift/Basic/Defer.h"
#include "swift/ClangImporter/ClangModule.h"
#include "swift/Sema/ConstraintSystem.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
using namespace swift;

const bool IsImplicit = true;

Expr *swift::buildSelfReference(VarDecl *selfDecl,
                                SelfAccessorKind selfAccessorKind,
                                bool isLValue, Type convertTy) {
  auto &ctx = selfDecl->getASTContext();
  auto selfTy = selfDecl->getType();

  switch (selfAccessorKind) {
  case SelfAccessorKind::Peer:
    assert(!convertTy || convertTy->isEqual(selfTy));
    return new (ctx) DeclRefExpr(selfDecl, DeclNameLoc(), IsImplicit,
                                 AccessSemantics::Ordinary,
                                 isLValue ? LValueType::get(selfTy) : selfTy);

  case SelfAccessorKind::Super: {
    assert(!isLValue);

    // Get the superclass type of self, looking through a metatype if needed.
    auto isMetatype = false;
    if (auto *metaTy = selfTy->getAs<MetatypeType>()) {
      isMetatype = true;
      selfTy = metaTy->getInstanceType();
    }
    selfTy = selfTy->getSuperclass();
    if (!selfTy) {
      // Error recovery path. We end up here if getSuperclassDecl() succeeds
      // but getSuperclass() fails (because, for instance, a generic parameter
      // of a generic nominal type cannot be resolved).
      selfTy = ErrorType::get(ctx);
    }
    if (isMetatype)
      selfTy = MetatypeType::get(selfTy);

    auto *superRef =
        new (ctx) SuperRefExpr(selfDecl, SourceLoc(), IsImplicit, selfTy);

    // If no conversion type was specified, or we're already at that type, we're
    // done.
    if (!convertTy || convertTy->isEqual(selfTy) || selfTy->is<ErrorType>())
      return superRef;

    // Insert the appropriate expr to handle the upcast.
    if (isMetatype) {
      assert(convertTy->castTo<MetatypeType>()
                 ->getInstanceType()
                 ->isExactSuperclassOf(selfTy->getMetatypeInstanceType()));
      return new (ctx) MetatypeConversionExpr(superRef, convertTy);
    } else {
      assert(convertTy->isExactSuperclassOf(selfTy));
      return new (ctx) DerivedToBaseExpr(superRef, convertTy);
    }
  }
  }
  llvm_unreachable("bad self access kind");
}

/// Build an argument list that forwards references to the specified parameter
/// list.
ArgumentList *swift::buildForwardingArgumentList(ArrayRef<ParamDecl *> params,
                                                 ASTContext &ctx) {
  SmallVector<Argument, 4> args;
  for (auto *param : params) {
    auto type = param->getType();

    Expr *ref = new (ctx) DeclRefExpr(param, DeclNameLoc(), /*implicit*/ true);
    ref->setType(param->isInOut() ? LValueType::get(type) : type);

    if (param->isInOut()) {
      ref = new (ctx) InOutExpr(SourceLoc(), ref, type, /*isImplicit=*/true);
    } else if (param->isVariadic()) {
      ref = new (ctx) VarargExpansionExpr(ref, /*implicit*/ true);
      ref->setType(type);
    }
    args.emplace_back(SourceLoc(), param->getArgumentName(), ref);
  }
  return ArgumentList::createImplicit(ctx, args);
}

static void maybeAddMemberwiseDefaultArg(ParamDecl *arg, VarDecl *var,
                                         unsigned paramSize, ASTContext &ctx) {
  // First and foremost, if this is a constant don't bother.
  if (var->isLet())
    return;

  // If there's no parent pattern there's not enough structure to even perform
  // this analysis. Just bail.
  if (!var->getParentPattern())
    return;

  // We can only provide default values for patterns binding a single variable.
  // i.e. var (a, b) = getSomeTuple() is not allowed.
  if (!var->getParentPattern()->getSingleVar())
    return;

  // Whether we have explicit initialization.
  bool isExplicitlyInitialized = false;
  if (auto pbd = var->getParentPatternBinding()) {
    const auto i = pbd->getPatternEntryIndexForVarDecl(var);
    isExplicitlyInitialized = pbd->isExplicitlyInitialized(i);
  }

  // Whether we can default-initialize this property.
  auto binding = var->getParentPatternBinding();
  bool isDefaultInitializable =
      var->getAttrs().hasAttribute<LazyAttr>() ||
      (binding && binding->isDefaultInitializable());

  // If this is neither explicitly initialized nor
  // default-initializable, don't add anything.
  if (!isExplicitlyInitialized && !isDefaultInitializable)
    return;

  // We can add a default value now.

  // If the variable has a type T? and no initial value, return a nil literal
  // default arg. All lazy variables return a nil literal as well. *Note* that
  // the type will always be a sugared T? because we don't default init an
  // explicit Optional<T>.
  bool isNilInitialized =
    var->getAttrs().hasAttribute<LazyAttr>() ||
    (!isExplicitlyInitialized && isDefaultInitializable &&
     var->getValueInterfaceType()->isOptional() &&
     (var->getAttachedPropertyWrappers().empty() ||
      var->isPropertyMemberwiseInitializedWithWrappedType()));
  if (isNilInitialized) {
    arg->setDefaultArgumentKind(DefaultArgumentKind::NilLiteral);
    return;
  }

  // If there's a backing storage property, the memberwise initializer
  // will be in terms of that.
  VarDecl *backingStorageVar = var->getPropertyWrapperBackingProperty();

  // Set the default value to the variable. When we emit this in silgen
  // we're going to call the variable's initializer expression.
  arg->setStoredProperty(backingStorageVar ? backingStorageVar : var);
  arg->setDefaultArgumentKind(DefaultArgumentKind::StoredProperty);
}

/// Describes the kind of implicit constructor that will be
/// generated.
enum class ImplicitConstructorKind {
  /// The default constructor, which default-initializes each
  /// of the instance variables.
  Default,
  /// The default constructor of a distributed actor.
  /// Similarly to a Default one it initializes each of the instance variables,
  /// however it also implicitly gains an ActorTransport parameter.
  DefaultDistributedActor,
  /// The memberwise constructor, which initializes each of
  /// the instance variables from a parameter of the same type and
  /// name.
  Memberwise,
};

/// Create an implicit struct or class constructor.
///
/// \param decl The struct or class for which a constructor will be created.
/// \param ICK The kind of implicit constructor to create.
///
/// \returns The newly-created constructor, which has already been type-checked
/// (but has not been added to the containing struct or class).
static ConstructorDecl *createImplicitConstructor(NominalTypeDecl *decl,
                                                  ImplicitConstructorKind ICK,
                                                  ASTContext &ctx) {
  assert(!decl->hasClangNode());

  SourceLoc Loc = decl->getLoc();
  auto accessLevel = AccessLevel::Internal;

  // Determine the parameter type of the implicit constructor.
  SmallVector<ParamDecl*, 8> params;
  SmallVector<DefaultArgumentInitializer *, 8> defaultInits;
  if (ICK == ImplicitConstructorKind::Memberwise) {
    assert(isa<StructDecl>(decl) && "Only struct have memberwise constructor");

    for (auto member : decl->getMembers()) {
      auto var = dyn_cast<VarDecl>(member);
      if (!var)
        continue;

      if (!var->isMemberwiseInitialized(/*preferDeclaredProperties=*/true))
        continue;

      accessLevel = std::min(accessLevel, var->getFormalAccess());

      auto varInterfaceType = var->getValueInterfaceType();
      bool isAutoClosure = false;

      if (var->getAttrs().hasAttribute<LazyAttr>()) {
        // If var is a lazy property, its value is provided for the underlying
        // storage.  We thus take an optional of the property's type.  We only
        // need to do this because the implicit initializer is added before all
        // the properties are type checked.  Perhaps init() synth should be
        // moved later.
        varInterfaceType = OptionalType::get(varInterfaceType);
      } else if (Type backingPropertyType =
                     var->getPropertyWrapperBackingPropertyType()) {
        // For a property that has a wrapper, writing the initializer
        // with an '=' implies that the memberwise initializer should also
        // accept a value of the original property type. Otherwise, the
        // memberwise initializer will be in terms of the backing storage
        // type.
        if (var->isPropertyMemberwiseInitializedWithWrappedType()) {
          varInterfaceType = var->getPropertyWrapperInitValueInterfaceType();

          auto initInfo = var->getPropertyWrapperInitializerInfo();
          isAutoClosure = initInfo.getWrappedValuePlaceholder()->isAutoClosure();
        } else {
          varInterfaceType = backingPropertyType;
        }
      }

      Type resultBuilderType= var->getResultBuilderType();
      if (resultBuilderType) {
        // If the variable's type is structurally a function type, use that
        // type. Otherwise, form a non-escaping function type for the function
        // parameter.
        bool isStructuralFunctionType =
            varInterfaceType->lookThroughAllOptionalTypes()
              ->is<AnyFunctionType>();
        if (!isStructuralFunctionType) {
          auto extInfo = ASTExtInfoBuilder().withNoEscape().build();
          varInterfaceType = FunctionType::get({ }, varInterfaceType, extInfo);
        }
      }

      // Create the parameter.
      auto *arg = new (ctx)
          ParamDecl(SourceLoc(), Loc,
                    var->getName(), Loc, var->getName(), decl);
      arg->setSpecifier(ParamSpecifier::Default);
      arg->setInterfaceType(varInterfaceType);
      arg->setImplicit();
      arg->setAutoClosure(isAutoClosure);

      // Don't allow the parameter to accept temporary pointer conversions.
      arg->setNonEphemeralIfPossible();

      // Attach a result builder attribute if needed.
      if (resultBuilderType) {
        auto typeExpr = TypeExpr::createImplicit(resultBuilderType, ctx);
        auto attr = CustomAttr::create(
            ctx, SourceLoc(), typeExpr, /*implicit=*/true);
        arg->getAttrs().add(attr);
      }

      maybeAddMemberwiseDefaultArg(arg, var, params.size(), ctx);
      
      params.push_back(arg);
    }
  } else if (ICK == ImplicitConstructorKind::DefaultDistributedActor) {
    assert(isa<ClassDecl>(decl));
    assert(decl->isDistributedActor() &&
           "Only 'distributed actor' type can gain implicit distributed actor init");

    if (swift::ensureDistributedModuleLoaded(decl)) {
      // copy access level of distributed actor init from the nominal decl
      accessLevel = decl->getEffectiveAccess();

      // Create the parameter.
      auto *arg = new (ctx) ParamDecl(SourceLoc(), Loc, ctx.Id_transport, Loc,
                                      ctx.Id_transport, decl);
      arg->setSpecifier(ParamSpecifier::Default);
      arg->setInterfaceType(getDistributedActorTransportType(decl));
      arg->setImplicit();

      params.push_back(arg);
    }
  }

  auto paramList = ParameterList::create(ctx, params);
  
  // Create the constructor.
  DeclName name(ctx, DeclBaseName::createConstructor(), paramList);
  auto *ctor =
    new (ctx) ConstructorDecl(name, Loc,
                              /*Failable=*/false, /*FailabilityLoc=*/SourceLoc(),
                              /*Async=*/false, /*AsyncLoc=*/SourceLoc(),
                              /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                              paramList, /*GenericParams=*/nullptr, decl);

  // Mark implicit.
  ctor->setImplicit();
  ctor->setSynthesized();
  ctor->setAccess(accessLevel);

  if (ICK == ImplicitConstructorKind::Memberwise) {
    ctor->setIsMemberwiseInitializer();
  }

  // If we are defining a default initializer for a class that has a superclass,
  // it overrides the default initializer of its superclass. Add an implicit
  // 'override' attribute.
  if (auto classDecl = dyn_cast<ClassDecl>(decl)) {
    if (classDecl->getSuperclass())
      ctor->getAttrs().add(new (ctx) OverrideAttr(/*IsImplicit=*/true));
  }

  return ctor;
}

/// Create a stub body that emits a fatal error message.
static std::pair<BraceStmt *, bool>
synthesizeStubBody(AbstractFunctionDecl *fn, void *) {
  auto *ctor = cast<ConstructorDecl>(fn);
  auto &ctx = ctor->getASTContext();

  auto unimplementedInitDecl = ctx.getUnimplementedInitializer();
  auto classDecl = ctor->getDeclContext()->getSelfClassDecl();
  if (!unimplementedInitDecl) {
    ctx.Diags.diagnose(classDecl->getLoc(),
                       diag::missing_unimplemented_init_runtime);
    return { nullptr, true };
  }

  auto *staticStringDecl = ctx.getStaticStringDecl();
  auto staticStringType = staticStringDecl->getDeclaredInterfaceType();
  auto staticStringInit = ctx.getStringBuiltinInitDecl(staticStringDecl);

  auto *uintDecl = ctx.getUIntDecl();
  auto uintType = uintDecl->getDeclaredInterfaceType();
  auto uintInit = ctx.getIntBuiltinInitDecl(uintDecl);

  // Create a call to Swift._unimplementedInitializer
  auto loc = classDecl->getLoc();
  Expr *ref = new (ctx) DeclRefExpr(unimplementedInitDecl,
                                   DeclNameLoc(loc),
                                   /*Implicit=*/true);
  ref->setType(unimplementedInitDecl->getInterfaceType()
                                    ->removeArgumentLabels(1));

  llvm::SmallString<64> buffer;
  StringRef fullClassName = ctx.AllocateCopy(
                              (classDecl->getModuleContext()->getName().str() +
                               "." +
                               classDecl->getName().str()).toStringRef(buffer));

  auto *className = new (ctx) StringLiteralExpr(fullClassName, loc,
                                                /*Implicit=*/true);
  className->setBuiltinInitializer(staticStringInit);
  assert(isa<ConstructorDecl>(className->getBuiltinInitializer().getDecl()));
  className->setType(staticStringType);

  auto *initName = new (ctx) MagicIdentifierLiteralExpr(
    MagicIdentifierLiteralExpr::Function, loc, /*Implicit=*/true);
  initName->setType(staticStringType);
  initName->setBuiltinInitializer(staticStringInit);

  auto *file = new (ctx) MagicIdentifierLiteralExpr(
    MagicIdentifierLiteralExpr::FileID, loc, /*Implicit=*/true);
  file->setType(staticStringType);
  file->setBuiltinInitializer(staticStringInit);

  auto *line = new (ctx) MagicIdentifierLiteralExpr(
    MagicIdentifierLiteralExpr::Line, loc, /*Implicit=*/true);
  line->setType(uintType);
  line->setBuiltinInitializer(uintInit);

  auto *column = new (ctx) MagicIdentifierLiteralExpr(
    MagicIdentifierLiteralExpr::Column, loc, /*Implicit=*/true);
  column->setType(uintType);
  column->setBuiltinInitializer(uintInit);

  auto *argList = ArgumentList::forImplicitUnlabeled(
      ctx, {className, initName, file, line, column});
  auto *call = CallExpr::createImplicit(ctx, ref, argList);
  call->setType(ctx.getNeverType());
  call->setThrows(false);

  SmallVector<ASTNode, 2> stmts;
  stmts.push_back(call);
  stmts.push_back(new (ctx) ReturnStmt(SourceLoc(), /*Result=*/nullptr));
  return { BraceStmt::create(ctx, SourceLoc(), stmts, SourceLoc(),
                             /*implicit=*/true),
           /*isTypeChecked=*/true };
}

namespace {
  struct DesignatedInitOverrideInfo {
    GenericSignature GenericSig;
    GenericParamList *GenericParams;
    SubstitutionMap OverrideSubMap;
  };
}

static DesignatedInitOverrideInfo
computeDesignatedInitOverrideSignature(ASTContext &ctx,
                                       ClassDecl *classDecl,
                                       Type superclassTy,
                                       ConstructorDecl *superclassCtor) {
  auto *moduleDecl = classDecl->getParentModule();

  auto *superclassDecl = superclassTy->getAnyNominal();

  auto classSig = classDecl->getGenericSignature();
  auto superclassCtorSig = superclassCtor->getGenericSignature();
  auto superclassSig = superclassDecl->getGenericSignature();

  // These are our outputs.
  GenericSignature genericSig = classSig;
  auto *genericParams = superclassCtor->getGenericParams();
  auto subMap = superclassTy->getContextSubstitutionMap(
      moduleDecl, superclassDecl);

  if (superclassCtorSig.getPointer() != superclassSig.getPointer()) {
    // If the base initiliazer's generic signature is different
    // from that of the base class, the base class initializer either
    // has generic parameters or a 'where' clause.
    //
    // We need to "rebase" the requirements on top of the derived class's
    // generic signature.

    SmallVector<GenericTypeParamDecl *, 4> newParams;
    SmallVector<GenericTypeParamType *, 1> newParamTypes;

    // If genericParams is non-null, the base class initializer has its own
    // generic parameters. Otherwise, it is non-generic with a contextual
    // 'where' clause.
    if (genericParams) {
      // First, clone the base class initializer's generic parameter list,
      // but change the depth of the generic parameters to be one greater
      // than the depth of the subclass.
      unsigned depth = 0;
      if (auto genericSig = classDecl->getGenericSignature())
        depth = genericSig.getGenericParams().back()->getDepth() + 1;

      for (auto *param : genericParams->getParams()) {
        auto *newParam = new (ctx) GenericTypeParamDecl(
            classDecl, param->getName(), SourceLoc(), param->isTypeSequence(),
            depth, param->getIndex());
        newParams.push_back(newParam);
      }

      // We don't have to clone the RequirementReprs, because they're not
      // used for anything other than SIL mode.
      genericParams = GenericParamList::create(ctx,
                                               SourceLoc(),
                                               newParams,
                                               SourceLoc(),
                                               ArrayRef<RequirementRepr>(),
                                               SourceLoc());

      // Add the generic parameter types.
      for (auto *newParam : newParams) {
        newParamTypes.push_back(
            newParam->getDeclaredInterfaceType()->castTo<GenericTypeParamType>());
      }
    }

    // The depth at which the initializer's own generic parameters start, if any.
    unsigned superclassDepth = 0;
    if (superclassSig)
      superclassDepth = superclassSig.getGenericParams().back()->getDepth() + 1;

    // We're going to be substituting the requirements of the base class
    // initializer to form the requirements of the derived class initializer.
    auto substFn = [&](SubstitutableType *type) -> Type {
      auto *gp = cast<GenericTypeParamType>(type);
      // Generic parameters of the base class itself are mapped via the
      // substitution map of the superclass type.
      if (gp->getDepth() < superclassDepth)
        return Type(gp).subst(subMap);

      // Generic parameters added by the base class initializer map to the new
      // generic parameters of the derived initializer.
      return genericParams->getParams()[gp->getIndex()]
                 ->getDeclaredInterfaceType();
    };

    // If we don't have any new generic parameters and the derived class is
    // not generic, the base class initializer's 'where' clause should already
    // be fully satisfied, and we can just drop it.
    if (genericParams != nullptr || classSig) {
      auto lookupConformanceFn =
          [&](CanType depTy, Type substTy,
              ProtocolDecl *proto) -> ProtocolConformanceRef {
        if (depTy->getRootGenericParam()->getDepth() < superclassDepth)
          if (auto conf = subMap.lookupConformance(depTy, proto))
            return conf;

        return ProtocolConformanceRef(proto);
      };

      SmallVector<Requirement, 2> requirements;
      for (auto reqt : superclassCtorSig.getRequirements())
        if (auto substReqt = reqt.subst(substFn, lookupConformanceFn))
          requirements.push_back(*substReqt);

      // Now form the substitution map that will be used to remap parameter
      // types.
      subMap = SubstitutionMap::get(superclassCtorSig,
                                    substFn, lookupConformanceFn);

      genericSig = buildGenericSignature(ctx, classSig,
                                         std::move(newParamTypes),
                                         std::move(requirements));
    }
  }

  return DesignatedInitOverrideInfo{genericSig, genericParams, subMap};
}

static void
configureInheritedDesignatedInitAttributes(ClassDecl *classDecl,
                                           ConstructorDecl *ctor,
                                           ConstructorDecl *superclassCtor,
                                           ASTContext &ctx) {
  assert(ctor->getDeclContext() == classDecl);

  AccessLevel access = classDecl->getFormalAccess();
  access = std::max(access, AccessLevel::Internal);
  access = std::min(access, superclassCtor->getFormalAccess());

  ctor->setAccess(access);

  AccessScope superclassInliningAccessScope =
      superclassCtor->getFormalAccessScope(/*useDC*/nullptr,
                                           /*usableFromInlineAsPublic=*/true);

  if (superclassInliningAccessScope.isPublic()) {
    if (superclassCtor->getAttrs().hasAttribute<InlinableAttr>()) {
      // Inherit the @inlinable attribute.
      auto *clonedAttr = new (ctx) InlinableAttr(/*implicit=*/true);
      ctor->getAttrs().add(clonedAttr);

    } else if (access == AccessLevel::Internal && !superclassCtor->isDynamic()){
      // Inherit the @usableFromInline attribute.
      auto *clonedAttr = new (ctx) UsableFromInlineAttr(/*implicit=*/true);
      ctor->getAttrs().add(clonedAttr);
    }
  }

  // Inherit the @discardableResult attribute.
  if (superclassCtor->getAttrs().hasAttribute<DiscardableResultAttr>()) {
    auto *clonedAttr = new (ctx) DiscardableResultAttr(/*implicit=*/true);
    ctor->getAttrs().add(clonedAttr);
  }

  // Inherit the rethrows attribute.
  if (superclassCtor->getAttrs().hasAttribute<RethrowsAttr>()) {
    auto *clonedAttr = new (ctx) RethrowsAttr(/*implicit=*/true);
    ctor->getAttrs().add(clonedAttr);
  }

  // If the superclass has its own availability, make sure the synthesized
  // constructor is only as available as its superclass's constructor.
  if (superclassCtor->getAttrs().hasAttribute<AvailableAttr>()) {
    SmallVector<const Decl *, 2> asAvailableAs;

    // We don't have to look at enclosing contexts of the superclass constructor,
    // because designated initializers must always be defined in the superclass
    // body, and we already enforce that a superclass is at least as available as
    // a subclass.
    asAvailableAs.push_back(superclassCtor);
    if (auto *parentDecl = classDecl->getInnermostDeclWithAvailability()) {
      asAvailableAs.push_back(parentDecl);
    }
    AvailabilityInference::applyInferredAvailableAttrs(
        ctor, asAvailableAs, ctx);
  }

  // Wire up the overrides.
  ctor->setOverriddenDecl(superclassCtor);

  if (superclassCtor->isRequired())
    ctor->getAttrs().add(new (ctx) RequiredAttr(/*IsImplicit=*/false));
  else
    ctor->getAttrs().add(new (ctx) OverrideAttr(/*IsImplicit=*/false));

  // If the superclass constructor is @objc but the subclass constructor is
  // not representable in Objective-C, add @nonobjc implicitly.
  Optional<ForeignAsyncConvention> asyncConvention;
  Optional<ForeignErrorConvention> errorConvention;
  if (superclassCtor->isObjC() &&
      !isRepresentableInObjC(ctor, ObjCReason::MemberOfObjCSubclass,
                             asyncConvention, errorConvention))
    ctor->getAttrs().add(new (ctx) NonObjCAttr(/*isImplicit=*/true));
}

static std::pair<BraceStmt *, bool>
synthesizeDesignatedInitOverride(AbstractFunctionDecl *fn, void *context) {
  auto *ctor = cast<ConstructorDecl>(fn);
  auto &ctx = ctor->getASTContext();

  auto *superclassCtor = (ConstructorDecl *) context;

  // Reference to super.init.
  auto *selfDecl = ctor->getImplicitSelfDecl();
  auto *superRef = buildSelfReference(selfDecl, SelfAccessorKind::Super,
                                      /*isLValue=*/false);

  SubstitutionMap subs;
  if (auto *genericEnv = fn->getGenericEnvironment())
    subs = genericEnv->getForwardingSubstitutionMap();
  subs = SubstitutionMap::getOverrideSubstitutions(superclassCtor, fn, subs);
  ConcreteDeclRef ctorRef(superclassCtor, subs);

  auto type = superclassCtor->getInitializerInterfaceType().subst(subs);
  auto *ctorRefExpr =
      new (ctx) OtherConstructorDeclRefExpr(ctorRef, DeclNameLoc(),
                                            IsImplicit, type);

  if (auto *funcTy = type->getAs<FunctionType>())
    type = funcTy->getResult();
  auto *superclassCtorRefExpr =
      DotSyntaxCallExpr::create(ctx, ctorRefExpr, SourceLoc(), superRef, type);
  superclassCtorRefExpr->setThrows(false);

  auto *bodyParams = ctor->getParameters();
  auto *ctorArgs = buildForwardingArgumentList(bodyParams->getArray(), ctx);
  auto *superclassCallExpr =
      CallExpr::createImplicit(ctx, superclassCtorRefExpr, ctorArgs);

  if (auto *funcTy = type->getAs<FunctionType>())
    type = funcTy->getResult();
  superclassCallExpr->setType(type);
  superclassCallExpr->setThrows(superclassCtor->hasThrows());

  Expr *expr = superclassCallExpr;

  if (superclassCtor->hasAsync()) {
    expr = new (ctx) AwaitExpr(SourceLoc(), expr, type, /*implicit=*/true);
  }
  if (superclassCtor->hasThrows()) {
    expr = new (ctx) TryExpr(SourceLoc(), expr, type, /*implicit=*/true);
  }

  auto *rebindSelfExpr =
    new (ctx) RebindSelfInConstructorExpr(expr, selfDecl);

  SmallVector<ASTNode, 2> stmts;
  stmts.push_back(rebindSelfExpr);
  stmts.push_back(new (ctx) ReturnStmt(SourceLoc(), /*Result=*/nullptr));
  return { BraceStmt::create(ctx, SourceLoc(), stmts, SourceLoc(),
                            /*implicit=*/true),
           /*isTypeChecked=*/true };
}

/// The kind of designated initializer to synthesize.
enum class DesignatedInitKind {
  /// A stub initializer, which is not visible to name lookup and
  /// merely aborts at runtime.
  Stub,

  /// An initializer that simply chains to the corresponding
  /// superclass initializer.
  Chaining
};

/// Create a new initializer that overrides the given designated
/// initializer.
///
/// \param classDecl The subclass in which the new initializer will
/// be declared.
///
/// \param superclassCtor The superclass initializer for which this
/// routine will create an override.
///
/// \param kind The kind of initializer to synthesize.
///
/// \returns the newly-created initializer that overrides \p
/// superclassCtor.
static ConstructorDecl *
createDesignatedInitOverride(ClassDecl *classDecl,
                             ConstructorDecl *superclassCtor,
                             DesignatedInitKind kind,
                             ASTContext &ctx) {
  // Lookup will sometimes give us initializers that are from the ancestors of
  // our immediate superclass.  So, from the superclass constructor, we look
  // one level up to the enclosing type context which will either be a class
  // or an extension.  We can use the type declared in that context to check
  // if it's our immediate superclass and give up if we didn't.
  //
  // FIXME: Remove this when lookup of initializers becomes restricted to our
  // immediate superclass.
  auto *superclassCtorDecl =
      superclassCtor->getDeclContext()->getSelfNominalTypeDecl();
  Type superclassTy = classDecl->getSuperclass();
  NominalTypeDecl *superclassDecl = superclassTy->getAnyNominal();
  if (superclassCtorDecl != superclassDecl) {
    return nullptr;
  }

  auto overrideInfo =
      computeDesignatedInitOverrideSignature(ctx,
                                             classDecl,
                                             superclassTy,
                                             superclassCtor);

  if (auto superclassCtorSig = superclassCtor->getGenericSignature()) {
    auto *genericEnv = overrideInfo.GenericSig.getGenericEnvironment();

    // If the base class initializer has a 'where' clause, it might impose
    // requirements on the base class's own generic parameters that are not
    // satisfied by the derived class. In this case, we don't want to inherit
    // this initializer; there's no way to call it on the derived class.
    auto checkResult = TypeChecker::checkGenericArguments(
        classDecl->getParentModule(),
        superclassCtorSig.getRequirements(),
        [&](Type type) -> Type {
          auto substType = type.subst(overrideInfo.OverrideSubMap);
          return GenericEnvironment::mapTypeIntoContext(
            genericEnv, substType);
        });
    if (checkResult != RequirementCheckResult::Success)
      return nullptr;
  }

  // Create the initializer parameter patterns.
  OptionSet<ParameterList::CloneFlags> options
    = (ParameterList::Implicit |
       ParameterList::Inherited |
       ParameterList::NamedArguments);
  auto *superclassParams = superclassCtor->getParameters();
  auto *bodyParams = superclassParams->clone(ctx, options);

  // If the superclass is generic, we need to map the superclass constructor's
  // parameter types into the generic context of our class.
  //
  // We might have to apply substitutions, if for example we have a declaration
  // like 'class A : B<Int>'.
  for (unsigned idx : range(superclassParams->size())) {
    auto *superclassParam = superclassParams->get(idx);
    auto *bodyParam = bodyParams->get(idx);

    auto paramTy = superclassParam->getInterfaceType();
    auto substTy = paramTy.subst(overrideInfo.OverrideSubMap);

    bodyParam->setInterfaceType(substTy);
  }

  // Create the initializer declaration, inheriting the name,
  // failability, and throws from the superclass initializer.
  auto ctor =
    new (ctx) ConstructorDecl(superclassCtor->getName(),
                              classDecl->getBraces().Start,
                              superclassCtor->isFailable(),
                              /*FailabilityLoc=*/SourceLoc(),
                              /*Async=*/superclassCtor->hasAsync(),
                              /*AsyncLoc=*/SourceLoc(),
                              /*Throws=*/superclassCtor->hasThrows(),
                              /*ThrowsLoc=*/SourceLoc(),
                              bodyParams, overrideInfo.GenericParams,
                              classDecl);

  ctor->setImplicit();

  // Set the interface type of the initializer.
  ctor->setGenericSignature(overrideInfo.GenericSig);

  ctor->setImplicitlyUnwrappedOptional(
    superclassCtor->isImplicitlyUnwrappedOptional());

  configureInheritedDesignatedInitAttributes(classDecl, ctor,
                                             superclassCtor, ctx);

  if (kind == DesignatedInitKind::Stub) {
    // Make this a stub implementation.
    ctor->setBodySynthesizer(synthesizeStubBody);

    // Note that this is a stub implementation.
    ctor->setStubImplementation(true);

    return ctor;
  }

  // Form the body of a chaining designated initializer.
  assert(kind == DesignatedInitKind::Chaining);
  ctor->setBodySynthesizer(synthesizeDesignatedInitOverride, superclassCtor);

  return ctor;
}

/// Diagnose a missing required initializer.
static void diagnoseMissingRequiredInitializer(
              ClassDecl *classDecl,
              ConstructorDecl *superInitializer,
              ASTContext &ctx) {
  // Find the location at which we should insert the new initializer.
  SourceLoc insertionLoc;
  SourceLoc indentationLoc;
  for (auto member : classDecl->getMembers()) {
    // If we don't have an indentation location yet, grab one from this
    // member.
    if (indentationLoc.isInvalid()) {
      indentationLoc = member->getLoc();
    }

    // We only want to look at explicit constructors.
    auto ctor = dyn_cast<ConstructorDecl>(member);
    if (!ctor)
      continue;

    if (ctor->isImplicit())
      continue;

    insertionLoc = ctor->getEndLoc();
    indentationLoc = ctor->getLoc();
  }

  // If no initializers were listed, start at the opening '{' for the class.
  if (insertionLoc.isInvalid()) {
    insertionLoc = classDecl->getBraces().Start;
  }
  if (indentationLoc.isInvalid()) {
    indentationLoc = classDecl->getBraces().End;
  }

  // Adjust the insertion location to point at the end of this line (i.e.,
  // the start of the next line).
  insertionLoc = Lexer::getLocForEndOfLine(ctx.SourceMgr,
                                           insertionLoc);

  // Find the indentation used on the indentation line.
  StringRef extraIndentation;
  StringRef indentation = Lexer::getIndentationForLine(
      ctx.SourceMgr, indentationLoc, &extraIndentation);

  // Pretty-print the superclass initializer into a string.
  // FIXME: Form a new initializer by performing the appropriate
  // substitutions of subclass types into the superclass types, so that
  // we get the right generic parameters.
  std::string initializerText;
  {
    PrintOptions options;
    options.PrintImplicitAttrs = false;

    // Render the text.
    llvm::raw_string_ostream out(initializerText);
    {
      ExtraIndentStreamPrinter printer(out, indentation);
      printer.printNewline();

      // If there is no explicit 'required', print one.
      bool hasExplicitRequiredAttr = false;
      if (auto requiredAttr
            = superInitializer->getAttrs().getAttribute<RequiredAttr>())
          hasExplicitRequiredAttr = !requiredAttr->isImplicit();

      if (!hasExplicitRequiredAttr)
        printer << "required ";

      superInitializer->print(printer, options);
    }

    // Add a dummy body.
    out << " {\n";
    out << indentation << extraIndentation << "fatalError(\"";
    superInitializer->getName().printPretty(out);
    out << " has not been implemented\")\n";
    out << indentation << "}\n";
  }

  // Complain.
  ctx.Diags.diagnose(insertionLoc, diag::required_initializer_missing,
                     superInitializer->getName(),
                     superInitializer->getDeclContext()->getDeclaredInterfaceType())
    .fixItInsert(insertionLoc, initializerText);

  ctx.Diags.diagnose(findNonImplicitRequiredInit(superInitializer),
                     diag::required_initializer_here);
}

bool AreAllStoredPropertiesDefaultInitableRequest::evaluate(
    Evaluator &evaluator, NominalTypeDecl *decl) const {
  assert(!decl->hasClangNode());

  for (auto member : decl->getMembers()) {
    // If a stored property lacks an initial value and if there is no way to
    // synthesize an initial value (e.g. for an optional) then we suppress
    // generation of the default initializer.
    if (auto pbd = dyn_cast<PatternBindingDecl>(member)) {
      // Static variables are irrelevant.
      if (pbd->isStatic()) {
        continue;
      }

      for (auto idx : range(pbd->getNumPatternEntries())) {
        bool HasStorage = false;
        bool CheckDefaultInitializer = true;
        pbd->getPattern(idx)->forEachVariable([&](VarDecl *VD) {
          // If one of the bound variables is @NSManaged, go ahead no matter
          // what.
          if (VD->getAttrs().hasAttribute<NSManagedAttr>())
            CheckDefaultInitializer = false;

          if (VD->hasStorage())
            HasStorage = true;
          auto *backing = VD->getPropertyWrapperBackingProperty();
          if (backing && backing->hasStorage())
            HasStorage = true;
        });

        if (!HasStorage) continue;

        if (pbd->isInitialized(idx)) continue;

        // If we cannot default initialize the property, we cannot
        // synthesize a default initializer for the class.
        if (CheckDefaultInitializer && !pbd->isDefaultInitializable())
          return false;
      }
    }
  }

  return true;
}

static bool areAllStoredPropertiesDefaultInitializable(Evaluator &eval,
                                                       NominalTypeDecl *decl) {
  if (decl->hasClangNode())
    return true;

  return evaluateOrDefault(
      eval, AreAllStoredPropertiesDefaultInitableRequest{decl}, false);
}

bool
HasUserDefinedDesignatedInitRequest::evaluate(Evaluator &evaluator,
                                              NominalTypeDecl *decl) const {
  assert(!decl->hasClangNode());

  auto results = decl->lookupDirect(DeclBaseName::createConstructor());
  for (auto *member : results) {
    if (isa<ExtensionDecl>(member->getDeclContext()))
      continue;

    auto *ctor = cast<ConstructorDecl>(member);
    if (ctor->isDesignatedInit() && !ctor->isSynthesized())
      return true;
  }

  return false;
}

static bool hasUserDefinedDesignatedInit(Evaluator &eval,
                                         NominalTypeDecl *decl) {
  // Imported decls don't have a designated initializer defined by the user.
  if (decl->hasClangNode())
    return false;

  return evaluateOrDefault(eval, HasUserDefinedDesignatedInitRequest{decl},
                           false);
}

static bool canInheritDesignatedInits(Evaluator &eval, ClassDecl *decl) {
  // We can only inherit designated initializers if the user hasn't defined
  // a designated init of their own, and all the stored properties have initial
  // values.
  return !hasUserDefinedDesignatedInit(eval, decl) &&
         areAllStoredPropertiesDefaultInitializable(eval, decl);
}

static void collectNonOveriddenSuperclassInits(
    ClassDecl *subclass, SmallVectorImpl<ConstructorDecl *> &results) {
  auto *superclassDecl = subclass->getSuperclassDecl();
  assert(superclassDecl);

  // Record all of the initializers the subclass has overriden, excluding stub
  // overrides, which we don't want to consider as viable delegates for
  // convenience inits.
  llvm::SmallPtrSet<ConstructorDecl *, 4> overriddenInits;

  auto ctors = subclass->lookupDirect(DeclBaseName::createConstructor());
  for (auto *member : ctors) {
    if (isa<ExtensionDecl>(member->getDeclContext()))
      continue;

    auto *ctor = cast<ConstructorDecl>(member);
    if (!ctor->hasStubImplementation())
      if (auto overridden = ctor->getOverriddenDecl())
        overriddenInits.insert(overridden);
  }

  superclassDecl->synthesizeSemanticMembersIfNeeded(
    DeclBaseName::createConstructor());

  NLOptions subOptions = (NL_QualifiedDefault | NL_IgnoreAccessControl);
  SmallVector<ValueDecl *, 4> lookupResults;
  subclass->lookupQualified(
      superclassDecl, DeclNameRef::createConstructor(),
      subOptions, lookupResults);

  for (auto decl : lookupResults) {
    auto superclassCtor = cast<ConstructorDecl>(decl);

    // Skip invalid superclass initializers.
    if (superclassCtor->isInvalid())
      continue;

    // Skip unavailable superclass initializers.
    if (AvailableAttr::isUnavailable(superclassCtor))
      continue;

    if (!overriddenInits.count(superclassCtor))
      results.push_back(superclassCtor);
  }
}

/// For a class with a superclass, automatically define overrides
/// for all of the superclass's designated initializers.
static void addImplicitInheritedConstructorsToClass(ClassDecl *decl) {
  // Bail out if we're validating one of our constructors already;
  // we'll revisit the issue later.
  auto results = decl->lookupDirect(DeclBaseName::createConstructor());
  for (auto *member : results) {
    if (isa<ExtensionDecl>(member->getDeclContext()))
      continue;

    if (member->isRecursiveValidation())
      return;
  }

  decl->setAddedImplicitInitializers();

  // We can only inherit initializers if we have a superclass.
  if (!decl->getSuperclassDecl() || !decl->getSuperclass())
    return;

  // Check whether the user has defined a designated initializer for this class,
  // and whether all of its stored properties have initial values.
  auto &ctx = decl->getASTContext();
  bool foundDesignatedInit = hasUserDefinedDesignatedInit(ctx.evaluator, decl);
  bool defaultInitable =
      areAllStoredPropertiesDefaultInitializable(ctx.evaluator, decl);

  // We can't define these overrides if we have any uninitialized
  // stored properties.
  if (!defaultInitable && !foundDesignatedInit)
    return;

  SmallVector<ConstructorDecl *, 4> nonOverridenSuperclassCtors;
  collectNonOveriddenSuperclassInits(decl, nonOverridenSuperclassCtors);

  bool inheritDesignatedInits = canInheritDesignatedInits(ctx.evaluator, decl);
  for (auto *superclassCtor : nonOverridenSuperclassCtors) {
    // We only care about required or designated initializers.
    if (!superclassCtor->isDesignatedInit()) {
      if (superclassCtor->isRequired()) {
        assert(superclassCtor->isInheritable() &&
               "factory initializers cannot be 'required'");
        if (!decl->inheritsSuperclassInitializers())
          diagnoseMissingRequiredInitializer(decl, superclassCtor, ctx);
      }
      continue;
    }

    // If the superclass initializer is not accessible from the derived
    // class, don't synthesize an override, since we cannot reference the
    // superclass initializer's method descriptor at all.
    //
    // FIXME: This should be checked earlier as part of calculating
    // canInheritInitializers.
    if (!superclassCtor->isAccessibleFrom(decl))
      continue;

    // Diagnose a missing override of a required initializer.
    if (superclassCtor->isRequired() && !inheritDesignatedInits) {
      diagnoseMissingRequiredInitializer(decl, superclassCtor, ctx);
      continue;
    }

    // A designated or required initializer has not been overridden.

    bool alreadyDeclared = false;

    auto results = decl->lookupDirect(DeclBaseName::createConstructor());
    for (auto *member : results) {
      if (isa<ExtensionDecl>(member->getDeclContext()))
        continue;

      auto *ctor = cast<ConstructorDecl>(member);

      // Skip any invalid constructors.
      if (ctor->isInvalid())
        continue;

      auto type = swift::getMemberTypeForComparison(ctor, nullptr);
      if (isOverrideBasedOnType(ctor, type, superclassCtor)) {
        alreadyDeclared = true;
        break;
      }
    }

    // If we have already introduced an initializer with this parameter type,
    // don't add one now.
    if (alreadyDeclared)
      continue;

    // If we're inheriting initializers, create an override delegating
    // to 'super.init'. Otherwise, create a stub which traps at runtime.
    auto kind = inheritDesignatedInits ? DesignatedInitKind::Chaining
                                       : DesignatedInitKind::Stub;

    if (auto ctor = createDesignatedInitOverride(
                      decl, superclassCtor, kind, ctx)) {
      decl->addMember(ctor);
    }
  }
}

bool
InheritsSuperclassInitializersRequest::evaluate(Evaluator &eval,
                                                ClassDecl *decl) const {
  // Check if we parsed the @_inheritsConvenienceInitializers attribute.
  if (decl->getAttrs().hasAttribute<InheritsConvenienceInitializersAttr>())
    return true;

  auto superclassDecl = decl->getSuperclassDecl();
  assert(superclassDecl);

  // If the superclass has known-missing designated initializers, inheriting
  // is unsafe.
  if (superclassDecl->getModuleContext() != decl->getParentModule() &&
      superclassDecl->hasMissingDesignatedInitializers())
    return false;

  // If we're allowed to inherit designated initializers, then we can inherit
  // convenience inits too.
  if (canInheritDesignatedInits(eval, decl))
    return true;

  // Otherwise we need to check whether the user has overriden all of the
  // superclass' designed inits.
  SmallVector<ConstructorDecl *, 4> nonOverridenSuperclassCtors;
  collectNonOveriddenSuperclassInits(decl, nonOverridenSuperclassCtors);

  auto allDesignatedInitsOverriden =
      llvm::none_of(nonOverridenSuperclassCtors, [](ConstructorDecl *ctor) {
        return ctor->isDesignatedInit();
      });
  return allDesignatedInitsOverriden;
}

static bool shouldAttemptInitializerSynthesis(const NominalTypeDecl *decl) {
  // Don't synthesize initializers for imported decls.
  if (decl->hasClangNode())
    return false;

  // Don't add implicit constructors in module interfaces.
  if (auto *SF = decl->getParentSourceFile())
    if (SF->Kind == SourceFileKind::Interface)
      return false;

  // Don't attempt if we know the decl is invalid.
  if (decl->isInvalid())
    return false;

  return true;
}

void TypeChecker::addImplicitConstructors(NominalTypeDecl *decl) {
  // If we already added implicit initializers, we're done.
  if (decl->addedImplicitInitializers())
    return;

  if (!shouldAttemptInitializerSynthesis(decl)) {
    decl->setAddedImplicitInitializers();
    return;
  }

  if (auto *classDecl = dyn_cast<ClassDecl>(decl)) {
    addImplicitInheritedConstructorsToClass(classDecl);
  }

  // Force the memberwise and default initializers if the type has them.
  // FIXME: We need to be more lazy about synthesizing constructors.
  (void)decl->getMemberwiseInitializer();
  (void)decl->getDefaultInitializer();
}

evaluator::SideEffect
ResolveImplicitMemberRequest::evaluate(Evaluator &evaluator,
                                       NominalTypeDecl *target,
                                       ImplicitMemberAction action) const {
  // FIXME: This entire request is a layering violation made of smaller,
  // finickier layering violations. See rdar://56844567

  // Checks whether the target conforms to the given protocol. If the
  // conformance is incomplete, force the conformance.
  //
  // Returns whether the target conforms to the protocol.
  auto evaluateTargetConformanceTo = [&](ProtocolDecl *protocol) {
    if (!protocol)
      return false;

    auto targetType = target->getDeclaredInterfaceType();
    auto ref = target->getParentModule()->lookupConformance(
        targetType, protocol);
    if (ref.isInvalid()) {
      return false;
    }

    if (auto *conformance = dyn_cast<NormalProtocolConformance>(
            ref.getConcrete()->getRootConformance())) {
      if (conformance->getState() == ProtocolConformanceState::Incomplete) {
        TypeChecker::checkConformance(conformance);
      }
    }

    return true;
  };

  auto &Context = target->getASTContext();
  switch (action) {
  case ImplicitMemberAction::ResolveImplicitInit:
    TypeChecker::addImplicitConstructors(target);
    break;
  case ImplicitMemberAction::ResolveCodingKeys: {
    // CodingKeys is a special type which may be synthesized as part of
    // Encodable/Decodable conformance. If the target conforms to either
    // protocol and would derive conformance to either, the type may be
    // synthesized.
    // If the target conforms to either and the conformance has not yet been
    // evaluated, then we should do that here.
    //
    // Try to synthesize Decodable first. If that fails, try to synthesize
    // Encodable. If either succeeds and CodingKeys should have been
    // synthesized, it will be synthesized.
    auto *decodableProto = Context.getProtocol(KnownProtocolKind::Decodable);
    auto *encodableProto = Context.getProtocol(KnownProtocolKind::Encodable);
    if (!evaluateTargetConformanceTo(decodableProto)) {
      (void)evaluateTargetConformanceTo(encodableProto);
    }
  }
    break;
  case ImplicitMemberAction::ResolveEncodable: {
    // encode(to:) may be synthesized as part of derived conformance to the
    // Encodable protocol.
    // If the target should conform to the Encodable protocol, check the
    // conformance here to attempt synthesis.
    auto *encodableProto = Context.getProtocol(KnownProtocolKind::Encodable);
    (void)evaluateTargetConformanceTo(encodableProto);
  }
    break;
  case ImplicitMemberAction::ResolveDecodable: {
    // init(from:) may be synthesized as part of derived conformance to the
    // Decodable protocol.
    // If the target should conform to the Decodable protocol, check the
    // conformance here to attempt synthesis.
    TypeChecker::addImplicitConstructors(target);
    auto *decodableProto = Context.getProtocol(KnownProtocolKind::Decodable);
    (void)evaluateTargetConformanceTo(decodableProto);
  }
    break;
  case ImplicitMemberAction::ResolveDistributedActor:
  case ImplicitMemberAction::ResolveDistributedActorTransport:
  case ImplicitMemberAction::ResolveDistributedActorIdentity: {
    // init(transport:) and init(resolve:using:) may be synthesized as part of
    // derived conformance to the DistributedActor protocol.
    // If the target should conform to the DistributedActor protocol, check the
    // conformance here to attempt synthesis.
    // FIXME(distributed): invoke the requirement adding explicitly here
     TypeChecker::addImplicitConstructors(target);
    auto *distributedActorProto =
        Context.getProtocol(KnownProtocolKind::DistributedActor);
    (void)evaluateTargetConformanceTo(distributedActorProto);
    break;
  }
  }
  return std::make_tuple<>();
}

bool
HasMemberwiseInitRequest::evaluate(Evaluator &evaluator,
                                   StructDecl *decl) const {
  if (!shouldAttemptInitializerSynthesis(decl))
    return false;

  // If the user has already defined a designated initializer, then don't
  // synthesize a memberwise init.
  if (hasUserDefinedDesignatedInit(evaluator, decl))
    return false;

  for (auto *member : decl->getMembers()) {
    if (auto *var = dyn_cast<VarDecl>(member)) {
      // If this is a backing storage property for a property wrapper,
      // skip it.
      if (var->getOriginalWrappedProperty())
        continue;

      if (var->isMemberwiseInitialized(/*preferDeclaredProperties=*/true))
        return true;
    }
  }
  return false;
}

ConstructorDecl *
SynthesizeMemberwiseInitRequest::evaluate(Evaluator &evaluator,
                                          NominalTypeDecl *decl) const {
  // Create the implicit memberwise constructor.
  auto &ctx = decl->getASTContext();
  auto ctor =
      createImplicitConstructor(decl, ImplicitConstructorKind::Memberwise, ctx);
  decl->addMember(ctor);
  return ctor;
}

ConstructorDecl *
ResolveEffectiveMemberwiseInitRequest::evaluate(Evaluator &evaluator,
                                                NominalTypeDecl *decl) const {
  // Compute the access level for the memberwise initializer. The minimum of:
  // - Public, by default. This enables public nominal types to have public
  //   memberwise initializers.
  //   - The `public` default is important for synthesized member types, e.g.
  //     `TangentVector` structs synthesized during `Differentiable` derived
  //     conformances. Manually extending these types to define a public
  //     memberwise initializer causes a redeclaration error.
  // - The minimum access level of memberwise-initialized properties in the
  //   nominal type declaration.
  auto accessLevel = AccessLevel::Public;
  for (auto *member : decl->getMembers()) {
    auto *var = dyn_cast<VarDecl>(member);
    if (!var ||
        !var->isMemberwiseInitialized(/*preferDeclaredProperties*/ true))
      continue;
    accessLevel = std::min(accessLevel, var->getFormalAccess());
  }
  auto &ctx = decl->getASTContext();

  // If a memberwise initializer exists, set its access level and return it.
  if (auto *initDecl = decl->getMemberwiseInitializer()) {
    initDecl->overwriteAccess(accessLevel);
    return initDecl;
  }

  auto isEffectiveMemberwiseInitializer = [&](ConstructorDecl *initDecl) {
    // Check for `nullptr`.
    if (!initDecl)
      return false;
    // Get all stored properties, excluding `let` properties with initial
    // values.
    SmallVector<VarDecl *, 8> storedProperties;
    for (auto *vd : decl->getStoredProperties()) {
      if (vd->isLet() && vd->hasInitialValue())
        continue;
      storedProperties.push_back(vd);
    }
    auto initDeclType =
        initDecl->getMethodInterfaceType()->getAs<AnyFunctionType>();
    // Return false if initializer does not have a valid interface type.
    if (!initDeclType)
      return false;
    // Return false if stored property count does not have parameter count.
    if (storedProperties.size() != initDeclType->getNumParams())
      return false;
    // Return true if all stored property types/names match initializer
    // parameter types/labels.
    return llvm::all_of(
        llvm::zip(storedProperties, initDeclType->getParams()),
        [&](std::tuple<VarDecl *, AnyFunctionType::Param> pair) {
          auto *storedProp = std::get<0>(pair);
          auto param = std::get<1>(pair);
          return storedProp->getInterfaceType()->isEqual(
                     param.getPlainType()) &&
                 storedProp->getName() == param.getLabel();
        });
  };

  // Otherwise, look for a user-defined effective memberwise initializer.
  ConstructorDecl *memberwiseInitDecl = nullptr;
  auto initDecls = decl->lookupDirect(DeclBaseName::createConstructor());
  for (auto *decl : initDecls) {
    auto *initDecl = dyn_cast<ConstructorDecl>(decl);
    if (!isEffectiveMemberwiseInitializer(initDecl))
      continue;
    assert(!memberwiseInitDecl && "Memberwise initializer already found");
    memberwiseInitDecl = initDecl;
  }

  // Otherwise, create a memberwise initializer, set its access level, and
  // return it.
  if (!memberwiseInitDecl) {
    memberwiseInitDecl = createImplicitConstructor(
        decl, ImplicitConstructorKind::Memberwise, ctx);
    memberwiseInitDecl->overwriteAccess(accessLevel);
    decl->addMember(memberwiseInitDecl);
  }
  return memberwiseInitDecl;
}

bool
HasDefaultInitRequest::evaluate(Evaluator &evaluator,
                                NominalTypeDecl *decl) const {
  assert(isa<StructDecl>(decl) || isa<ClassDecl>(decl));

  if (!shouldAttemptInitializerSynthesis(decl))
    return false;

  if (auto *sd = dyn_cast<StructDecl>(decl)) {
    assert(!sd->hasUnreferenceableStorage() &&
           "User-defined structs cannot have unreferenceable storage");
    (void)sd;
  }

  // Don't synthesize a default for a subclass, it will attempt to inherit its
  // initializers from its superclass.
  if (auto *cd = dyn_cast<ClassDecl>(decl))
    if (cd->getSuperclassDecl())
      return false;

  // If the user has already defined a designated initializer, then don't
  // synthesize a default init.
  if (hasUserDefinedDesignatedInit(evaluator, decl))
    return false;

  // We can only synthesize a default init if all the stored properties have an
  // initial value.
  return areAllStoredPropertiesDefaultInitializable(evaluator, decl);
}

/// Synthesizer callback for a function body consisting of "return".
static std::pair<BraceStmt *, bool>
synthesizeSingleReturnFunctionBody(AbstractFunctionDecl *afd, void *) {
  ASTContext &ctx = afd->getASTContext();
  SmallVector<ASTNode, 1> stmts;
  stmts.push_back(new (ctx) ReturnStmt(afd->getLoc(), nullptr));
  return { BraceStmt::create(ctx, afd->getLoc(), stmts, afd->getLoc(), true),
           /*isTypeChecked=*/true };
}

ConstructorDecl *
SynthesizeDefaultInitRequest::evaluate(Evaluator &evaluator,
                                       NominalTypeDecl *decl) const {
  auto &ctx = decl->getASTContext();

  FrontendStatsTracer StatsTracer(ctx.Stats,
                                  "define-default-ctor", decl);
  PrettyStackTraceDecl stackTrace("defining default constructor for",
                                  decl);

  // Create the default constructor.
  auto ctorKind = decl->isDistributedActor() ?
      ImplicitConstructorKind::DefaultDistributedActor :
      ImplicitConstructorKind::Default;
  if (auto ctor = createImplicitConstructor(decl, ctorKind, ctx)) {
    // Add the constructor.
    decl->addMember(ctor);

    // Lazily synthesize an empty body for the default constructor.
    ctor->setBodySynthesizer(synthesizeSingleReturnFunctionBody);
    return ctor;
  }

  // no default init was synthesized
  return nullptr;
}

ValueDecl *swift::getProtocolRequirement(ProtocolDecl *protocol,
                                         Identifier name) {
  auto lookup = protocol->lookupDirect(name);
  // Erase declarations that are not protocol requirements.
  // This is important for removing default implementations of the same name.
  llvm::erase_if(lookup, [](ValueDecl *v) {
    return !isa<ProtocolDecl>(v->getDeclContext()) ||
           !v->isProtocolRequirement();
  });
  assert(lookup.size() == 1 && "Ambiguous protocol requirement");
  return lookup.front();
}

bool swift::hasLetStoredPropertyWithInitialValue(NominalTypeDecl *nominal) {
  return llvm::any_of(nominal->getStoredProperties(), [&](VarDecl *v) {
    return v->isLet() && v->hasInitialValue();
  });
}

void swift::addFixedLayoutAttr(NominalTypeDecl *nominal) {
  auto &C = nominal->getASTContext();
  // If nominal already has `@_fixed_layout`, return.
  if (nominal->getAttrs().hasAttribute<FixedLayoutAttr>())
    return;
  auto access = nominal->getEffectiveAccess();
  // If nominal does not have at least internal access, return.
  if (access < AccessLevel::Internal)
    return;
  // If nominal is internal, it should have the `@usableFromInline` attribute.
  if (access == AccessLevel::Internal &&
      !nominal->getAttrs().hasAttribute<UsableFromInlineAttr>()) {
    nominal->getAttrs().add(new (C) UsableFromInlineAttr(/*Implicit*/ true));
  }
  // Add `@_fixed_layout` to the nominal.
  nominal->getAttrs().add(new (C) FixedLayoutAttr(/*Implicit*/ true));
}
