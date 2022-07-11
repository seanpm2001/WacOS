//===--- TypeCheckConcurrency.cpp - Concurrency ---------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file implements type checking support for Swift's concurrency model.
//
//===----------------------------------------------------------------------===//
#include "TypeCheckConcurrency.h"
#include "TypeCheckDistributed.h"
#include "TypeChecker.h"
#include "TypeCheckType.h"
#include "swift/Strings.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/ImportCache.h"
#include "swift/AST/Initializer.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/NameLookupRequests.h"
#include "swift/AST/TypeCheckRequests.h"
#include "swift/AST/ExistentialLayout.h"
#include "swift/Sema/IDETypeChecking.h"

using namespace swift;

/// Determine whether it makes sense to infer an attribute in the given
/// context.
static bool shouldInferAttributeInContext(const DeclContext *dc) {
  if (auto *file = dyn_cast<FileUnit>(dc->getModuleScopeContext())) {
    switch (file->getKind()) {
    case FileUnitKind::Source:
      // Check what kind of source file we have.
      if (auto sourceFile = dc->getParentSourceFile()) {
        switch (sourceFile->Kind) {
        case SourceFileKind::Interface:
          // Interfaces have explicitly called-out Sendable conformances.
          return false;

        case SourceFileKind::Library:
        case SourceFileKind::Main:
        case SourceFileKind::SIL:
          return true;
        }
      }
      break;

    case FileUnitKind::Builtin:
    case FileUnitKind::SerializedAST:
    case FileUnitKind::Synthesized:
      return false;

    case FileUnitKind::ClangModule:
    case FileUnitKind::DWARFModule:
      return true;
    }

    return true;
  }

  return false;
}

void swift::addAsyncNotes(AbstractFunctionDecl const* func) {
  assert(func);
  if (!isa<DestructorDecl>(func) && !isa<AccessorDecl>(func)) {
    auto note =
        func->diagnose(diag::note_add_async_to_function, func->getName());

    if (func->hasThrows()) {
      auto replacement = func->getAttrs().hasAttribute<RethrowsAttr>()
                        ? "async rethrows"
                        : "async throws";

      note.fixItReplace(SourceRange(func->getThrowsLoc()), replacement);
    } else if (func->getParameters()->getRParenLoc().isValid()) {
      note.fixItInsert(func->getParameters()->getRParenLoc().getAdvancedLoc(1),
                       " async");
    }
  }
}

bool IsActorRequest::evaluate(
    Evaluator &evaluator, NominalTypeDecl *nominal) const {
  // Protocols are actors if they inherit from `Actor`.
  if (auto protocol = dyn_cast<ProtocolDecl>(nominal)) {
    auto &ctx = protocol->getASTContext();
    auto *actorProtocol = ctx.getProtocol(KnownProtocolKind::Actor);
    return (protocol == actorProtocol ||
            protocol->inheritsFrom(actorProtocol));
  }

  // Class declarations are actors if they were declared with "actor".
  auto classDecl = dyn_cast<ClassDecl>(nominal);
  if (!classDecl)
    return false;

  return classDecl->isExplicitActor();
}

bool IsDefaultActorRequest::evaluate(
    Evaluator &evaluator, ClassDecl *classDecl, ModuleDecl *M,
    ResilienceExpansion expansion) const {
  // If the class isn't an actor, it's not a default actor.
  if (!classDecl->isActor())
    return false;

  // If the class is resilient from the perspective of the module
  // module, it's not a default actor.
  if (classDecl->isForeign() || classDecl->isResilient(M, expansion))
    return false;

  // Check whether the class has explicit custom-actor methods.

  // If we synthesized the unownedExecutor property, we should've
  // added a semantics attribute to it (if it was actually a default
  // actor).
  if (auto executorProperty = classDecl->getUnownedExecutorProperty())
    return executorProperty->getAttrs()
             .hasSemanticsAttr(SEMANTICS_DEFAULT_ACTOR);

  return true;
}

VarDecl *GlobalActorInstanceRequest::evaluate(
    Evaluator &evaluator, NominalTypeDecl *nominal) const {
  auto globalActorAttr = nominal->getAttrs().getAttribute<GlobalActorAttr>();
  if (!globalActorAttr)
    return nullptr;

  // Ensure that the actor protocol has been loaded.
  ASTContext &ctx = nominal->getASTContext();
  auto actorProto = ctx.getProtocol(KnownProtocolKind::Actor);
  if (!actorProto) {
    nominal->diagnose(diag::concurrency_lib_missing, "Actor");
    return nullptr;
  }

  // Non-final classes cannot be global actors.
  if (auto classDecl = dyn_cast<ClassDecl>(nominal)) {
    if (!classDecl->isSemanticallyFinal()) {
      nominal->diagnose(diag::global_actor_non_final_class, nominal->getName())
        .highlight(globalActorAttr->getRangeWithAt());
    }
  }

  // Global actors have a static property "shared" that provides an actor
  // instance. The value must be of Actor type, which is validated by
  // conformance to the 'GlobalActor' protocol.
  SmallVector<ValueDecl *, 4> decls;
  nominal->lookupQualified(
      nominal, DeclNameRef(ctx.Id_shared), NL_QualifiedDefault, decls);
  for (auto decl : decls) {
    auto var = dyn_cast<VarDecl>(decl);
    if (!var)
      continue;

    if (var->getDeclContext() == nominal && var->isStatic())
      return var;
  }

  return nullptr;
}

Optional<std::pair<CustomAttr *, NominalTypeDecl *>>
swift::checkGlobalActorAttributes(
    SourceLoc loc, DeclContext *dc, ArrayRef<CustomAttr *> attrs) {
  ASTContext &ctx = dc->getASTContext();

  CustomAttr *globalActorAttr = nullptr;
  NominalTypeDecl *globalActorNominal = nullptr;
  for (auto attr : attrs) {
    // Figure out which nominal declaration this custom attribute refers to.
    auto nominal = evaluateOrDefault(ctx.evaluator,
                                     CustomAttrNominalRequest{attr, dc},
                                     nullptr);

    // Ignore unresolvable custom attributes.
    if (!nominal)
      continue;

    // We are only interested in global actor types.
    if (!nominal->isGlobalActor())
      continue;

    // Only a single global actor can be applied to a given entity.
    if (globalActorAttr) {
      ctx.Diags.diagnose(
          loc, diag::multiple_global_actors, globalActorNominal->getName(),
          nominal->getName());
      continue;
    }

    globalActorAttr = const_cast<CustomAttr *>(attr);
    globalActorNominal = nominal;
  }

  if (!globalActorAttr)
    return None;

  return std::make_pair(globalActorAttr, globalActorNominal);
}

Optional<std::pair<CustomAttr *, NominalTypeDecl *>>
GlobalActorAttributeRequest::evaluate(
    Evaluator &evaluator,
    llvm::PointerUnion<Decl *, ClosureExpr *> subject) const {
  DeclContext *dc;
  DeclAttributes *declAttrs;
  SourceLoc loc;
  if (auto decl = subject.dyn_cast<Decl *>()) {
    dc = decl->getDeclContext();
    declAttrs = &decl->getAttrs();
    // HACK: `getLoc`, when querying the attr from  a serialized decl,
    // dependning on deserialization order, may launch into arbitrary
    // type-checking when querying interface types of such decls. Which,
    // in turn, may do things like query (to print) USRs. This ends up being
    // prone to request evaluator cycles.
    //
    // Because this only applies to serialized decls, we can be confident
    // that they already went through this type-checking as primaries, so,
    // for now, to avoid cycles, we simply ignore the locs on serialized decls
    // only.
    // This is a workaround for rdar://79563942
    loc = decl->getLoc(/* SerializedOK */ false);
  } else {
    auto closure = subject.get<ClosureExpr *>();
    dc = closure;
    declAttrs = &closure->getAttrs();
    loc = closure->getLoc();
  }

  // Collect the attributes.
  SmallVector<CustomAttr *, 2> attrs;
  for (auto attr : declAttrs->getAttributes<CustomAttr>()) {
    auto mutableAttr = const_cast<CustomAttr *>(attr);
    attrs.push_back(mutableAttr);
  }

  // Look for a global actor attribute.
  auto result = checkGlobalActorAttributes(loc, dc, attrs);
  if (!result)
    return None;

  // Closures can always have a global actor attached.
  if (auto closure = subject.dyn_cast<ClosureExpr *>()) {
    return result;
  }

  // Check that a global actor attribute makes sense on this kind of
  // declaration.
  auto decl = subject.get<Decl *>();
  auto globalActorAttr = result->first;
  if (auto nominal = dyn_cast<NominalTypeDecl>(decl)) {
    // Nominal types are okay...
    if (auto classDecl = dyn_cast<ClassDecl>(nominal)){
      if (classDecl->isActor()) {
        // ... except for actors.
        nominal->diagnose(diag::global_actor_on_actor_class, nominal->getName())
            .highlight(globalActorAttr->getRangeWithAt());
        return None;
      }
    }
  } else if (auto storage = dyn_cast<AbstractStorageDecl>(decl)) {
    // Subscripts and properties are fine...
    if (auto var = dyn_cast<VarDecl>(storage)) {
      if (var->getDeclContext()->isLocalContext()) {
        var->diagnose(diag::global_actor_on_local_variable, var->getName())
            .highlight(globalActorAttr->getRangeWithAt());
        return None;
      }
    }
  } else if (isa<ExtensionDecl>(decl)) {
    // Extensions are okay.
  } else if (isa<ConstructorDecl>(decl) || isa<FuncDecl>(decl)) {
    // Functions are okay.
  } else {
    // Everything else is disallowed.
    decl->diagnose(diag::global_actor_disallowed, decl->getDescriptiveKind());
    return None;
  }

  return result;
}

Type swift::getExplicitGlobalActor(ClosureExpr *closure) {
  // Look at the explicit attribute.
  auto globalActorAttr = evaluateOrDefault(
      closure->getASTContext().evaluator,
      GlobalActorAttributeRequest{closure}, None);
  if (!globalActorAttr)
    return Type();

  Type globalActor = evaluateOrDefault(
      closure->getASTContext().evaluator,
      CustomAttrTypeRequest{
        globalActorAttr->first, closure, CustomAttrTypeKind::GlobalActor},
        Type());
  if (!globalActor || globalActor->hasError())
    return Type();

  return globalActor;
}

/// Determine the isolation rules for a given declaration.
ActorIsolationRestriction ActorIsolationRestriction::forDeclaration(
    ConcreteDeclRef declRef, const DeclContext *fromDC, bool fromExpression) {
  auto decl = declRef.getDecl();

  switch (decl->getKind()) {
  case DeclKind::AssociatedType:
  case DeclKind::Class:
  case DeclKind::Enum:
  case DeclKind::Extension:
  case DeclKind::GenericTypeParam:
  case DeclKind::OpaqueType:
  case DeclKind::Protocol:
  case DeclKind::Struct:
  case DeclKind::TypeAlias:
    // Types are always available.
    return forUnrestricted();

  case DeclKind::EnumCase:
  case DeclKind::EnumElement:
    // Type-level entities don't require isolation.
    return forUnrestricted();

  case DeclKind::IfConfig:
  case DeclKind::Import:
  case DeclKind::InfixOperator:
  case DeclKind::MissingMember:
  case DeclKind::Module:
  case DeclKind::PatternBinding:
  case DeclKind::PostfixOperator:
  case DeclKind::PoundDiagnostic:
  case DeclKind::PrecedenceGroup:
  case DeclKind::PrefixOperator:
  case DeclKind::TopLevelCode:
    // Non-value entities don't require isolation.
    return forUnrestricted();

  case DeclKind::Destructor:
    // Destructors don't require isolation.
    return forUnrestricted();

  case DeclKind::Param:
  case DeclKind::Var:
  case DeclKind::Accessor:
  case DeclKind::Constructor:
  case DeclKind::Func:
  case DeclKind::Subscript: {
    // Local captures are checked separately.
    if (cast<ValueDecl>(decl)->isLocalCapture())
      return forUnrestricted();

    auto isolation = getActorIsolation(cast<ValueDecl>(decl));

    // 'let' declarations are immutable, so they can be accessed across
    // actors.
    bool isAccessibleAcrossActors = false;
    if (auto var = dyn_cast<VarDecl>(decl)) {
      // A 'let' declaration is accessible across actors if it is either
      // nonisolated or it is accessed from within the same module.
      if (var->isLet() &&
          (isolation == ActorIsolation::Independent ||
           var->getDeclContext()->getParentModule() ==
           fromDC->getParentModule())) {
        isAccessibleAcrossActors = true;
      }
    }

    // A function that provides an asynchronous context has no restrictions
    // on its access.
    //
    // FIXME: technically, synchronous functions are allowed to be cross-actor.
    // The call-sites are just conditionally async based on where they appear
    // (outside or inside the actor). This suggests that the implicitly-async
    // concept could be merged into the CrossActorSelf concept.
    if (auto func = dyn_cast<AbstractFunctionDecl>(decl)) {
      if (func->isAsyncContext())
        isAccessibleAcrossActors = true;
    }

    // Similarly, a computed property or subscript that has an 'async' getter
    // provides an asynchronous context, and has no restrictions.
    if (auto storageDecl = dyn_cast<AbstractStorageDecl>(decl)) {
      if (auto effectfulGetter = storageDecl->getEffectfulGetAccessor())
        if (effectfulGetter->hasAsync())
          isAccessibleAcrossActors = true;
    }

    // Determine the actor isolation of the given declaration.
    switch (isolation) {
    case ActorIsolation::ActorInstance:
      // Protected actor instance members can only be accessed on 'self'.
      return forActorSelf(isolation.getActor(),
          isAccessibleAcrossActors || isa<ConstructorDecl>(decl));

    case ActorIsolation::DistributedActorInstance:
      return forDistributedActorSelf(isolation.getActor(),
       /*isCrossActor*/ isAccessibleAcrossActors || isa<ConstructorDecl>(decl));

    case ActorIsolation::GlobalActorUnsafe:
    case ActorIsolation::GlobalActor: {
      // A global-actor-isolated function referenced within an expression
      // carries the global actor into its function type. The actual
      // reference to the function is therefore not restricted, because the
      // call to the function is.
      if (fromExpression && isa<AbstractFunctionDecl>(decl))
        return forUnrestricted();

      Type actorType = isolation.getGlobalActor();
      if (auto subs = declRef.getSubstitutions())
        actorType = actorType.subst(subs);

      return forGlobalActor(actorType, isAccessibleAcrossActors,
                            isolation == ActorIsolation::GlobalActorUnsafe);
    }

    case ActorIsolation::Independent:
      // While some synchronous, non-delegating actor inits are
      // nonisolated, they need cross-actor restrictions (e.g., for Sendable).
      if (auto *ctor = dyn_cast<ConstructorDecl>(decl))
        if (!ctor->isConvenienceInit())
          if (auto *parent = ctor->getParent()->getSelfClassDecl())
            if (parent->isAnyActor())
              return forActorSelf(parent, /*isCrossActor=*/true);

      // `nonisolated let` members are cross-actor as well.
      if (auto var = dyn_cast<VarDecl>(decl)) {
        if (var->isInstanceMember() && var->isLet()) {
          if (auto parent = var->getDeclContext()->getSelfClassDecl()) {
            if (parent->isActor() && !parent->isDistributedActor())
              return forActorSelf(parent, /*isCrossActor=*/true);
          }
        }
      }

      return forUnrestricted();

    case ActorIsolation::Unspecified:
      return isAccessibleAcrossActors ? forUnrestricted() : forUnsafe();
    }
  }
  }
}

namespace {
  /// Describes the important parts of a partial apply thunk.
  struct PartialApplyThunkInfo {
    Expr *base;
    Expr *fn;
    bool isEscaping;
  };
}

/// Try to decompose a call that might be an invocation of a partial apply
/// thunk.
static Optional<PartialApplyThunkInfo> decomposePartialApplyThunk(
    ApplyExpr *apply, Expr *parent) {
  // Check for a call to the outer closure in the thunk.
  auto outerAutoclosure = dyn_cast<AutoClosureExpr>(apply->getFn());
  if (!outerAutoclosure ||
      outerAutoclosure->getThunkKind()
        != AutoClosureExpr::Kind::DoubleCurryThunk)
    return None;

  auto *unarySelfArg = apply->getArgs()->getUnlabeledUnaryExpr();
  assert(unarySelfArg &&
         "Double curry should start with a unary (Self) -> ... arg");

  auto memberFn = outerAutoclosure->getUnwrappedCurryThunkExpr();
  if (!memberFn)
    return None;

  // Determine whether the partial apply thunk was immediately converted to
  // noescape.
  bool isEscaping = true;
  if (auto conversion = dyn_cast_or_null<FunctionConversionExpr>(parent)) {
    auto fnType = conversion->getType()->getAs<FunctionType>();
    isEscaping = fnType && !fnType->isNoEscape();
  }

  return PartialApplyThunkInfo{unarySelfArg, memberFn, isEscaping};
}

/// Find the immediate member reference in the given expression.
static Optional<std::pair<ConcreteDeclRef, SourceLoc>>
findMemberReference(Expr *expr) {
  if (auto declRef = dyn_cast<DeclRefExpr>(expr))
    return std::make_pair(declRef->getDeclRef(), declRef->getLoc());

  if (auto otherCtor = dyn_cast<OtherConstructorDeclRefExpr>(expr)) {
    return std::make_pair(otherCtor->getDeclRef(), otherCtor->getLoc());
  }

  return None;
}

/// Return true if the callee of an ApplyExpr is async
///
/// Note that this must be called after the implicitlyAsync flag has been set,
/// or implicitly async calls will not return the correct value.
static bool isAsyncCall(const ApplyExpr *call) {
  if (call->isImplicitlyAsync())
    return true;

  // Effectively the same as doing a
  // `cast_or_null<FunctionType>(call->getFn()->getType())`, check the
  // result of that and then checking `isAsync` if it's defined.
  Type funcTypeType = call->getFn()->getType();
  if (!funcTypeType)
    return false;
  AnyFunctionType *funcType = funcTypeType->getAs<AnyFunctionType>();
  if (!funcType)
    return false;
  return funcType->isAsync();
}

/// Determine whether we should diagnose data races within the current context.
///
/// By default, we do this only in code that makes use of concurrency
/// features.
static bool shouldDiagnoseExistingDataRaces(const DeclContext *dc);

/// Determine whether this closure should be treated as Sendable.
///
/// \param forActorIsolation Whether this check is for the purposes of
/// determining whether the closure must be non-isolated.
static bool isSendableClosure(
    const AbstractClosureExpr *closure, bool forActorIsolation) {
  if (auto explicitClosure = dyn_cast<ClosureExpr>(closure)) {
    if (forActorIsolation && explicitClosure->inheritsActorContext()) {
      return false;
    }
  }

  if (auto type = closure->getType()) {
    if (auto fnType = type->getAs<AnyFunctionType>())
      if (fnType->isSendable())
        return true;
  }

  return false;
}

/// Determine whether the given type is suitable as a concurrent value type.
bool swift::isSendableType(ModuleDecl *module, Type type) {
  auto proto = module->getASTContext().getProtocol(KnownProtocolKind::Sendable);
  if (!proto)
    return true;

  auto conformance = TypeChecker::conformsToProtocol(type, proto, module);
  if (conformance.isInvalid())
    return false;

  // Look for missing Sendable conformances.
  return !conformance.forEachMissingConformance(module,
      [](BuiltinProtocolConformance *missing) {
        return missing->getProtocol()->isSpecificProtocol(
            KnownProtocolKind::Sendable);
      });
}

/// Add Fix-It text for the given nominal type to adopt Sendable.
static void addSendableFixIt(
    const NominalTypeDecl *nominal, InFlightDiagnostic &diag, bool unchecked) {
  if (nominal->getInherited().empty()) {
    SourceLoc fixItLoc = nominal->getBraces().Start;
    if (unchecked)
      diag.fixItInsert(fixItLoc, ": @unchecked Sendable");
    else
      diag.fixItInsert(fixItLoc, ": Sendable");
  } else {
    ASTContext &ctx = nominal->getASTContext();
    SourceLoc fixItLoc = nominal->getInherited().back().getSourceRange().End;
    fixItLoc = Lexer::getLocForEndOfToken(ctx.SourceMgr, fixItLoc);
    if (unchecked)
      diag.fixItInsert(fixItLoc, ", @unchecked Sendable");
    else
      diag.fixItInsert(fixItLoc, ", Sendable");
  }
}

/// Determine whether there is an unavailable conformance here.
static bool hasUnavailableConformance(ProtocolConformanceRef conformance) {
  // Abstract conformances are never unavailable.
  if (!conformance.isConcrete())
    return false;

  // Check whether this conformance is on an unavailable extension.
  auto concrete = conformance.getConcrete();
  auto ext = dyn_cast<ExtensionDecl>(concrete->getDeclContext());
  if (ext && AvailableAttr::isUnavailable(ext))
    return true;

  // Check the conformances in the substitution map.
  auto module = concrete->getDeclContext()->getParentModule();
  auto subMap = concrete->getSubstitutions(module);
  for (auto subConformance : subMap.getConformances()) {
    if (hasUnavailableConformance(subConformance))
      return true;
  }

  return false;
}

static bool shouldDiagnoseExistingDataRaces(const DeclContext *dc) {
  if (dc->getParentModule()->isConcurrencyChecked())
    return true;

  return contextRequiresStrictConcurrencyChecking(dc, [](const AbstractClosureExpr *) {
    return Type();
  });
}

/// Determine the default diagnostic behavior for this language mode.
static DiagnosticBehavior defaultSendableDiagnosticBehavior(
    const LangOptions &langOpts) {
  // Prior to Swift 6, all Sendable-related diagnostics are warnings.
  if (!langOpts.isSwiftVersionAtLeast(6))
    return DiagnosticBehavior::Warning;

  return DiagnosticBehavior::Unspecified;
}

bool SendableCheckContext::isExplicitSendableConformance() const {
  if (!conformanceCheck)
    return false;

  switch (*conformanceCheck) {
  case SendableCheck::Explicit:
    return true;

  case SendableCheck::ImpliedByStandardProtocol:
  case SendableCheck::Implicit:
  case SendableCheck::ImplicitForExternallyVisible:
    return false;
  }
}

DiagnosticBehavior SendableCheckContext::defaultDiagnosticBehavior() const {
  // If we're not supposed to diagnose existing data races from this context,
  // ignore the diagnostic entirely.
  if (!isExplicitSendableConformance() &&
      !shouldDiagnoseExistingDataRaces(fromDC))
    return DiagnosticBehavior::Ignore;

  return defaultSendableDiagnosticBehavior(fromDC->getASTContext().LangOpts);
}

/// Determine whether the given nominal type that is within the current module
/// has an explicit Sendable.
static bool hasExplicitSendableConformance(NominalTypeDecl *nominal) {
  ASTContext &ctx = nominal->getASTContext();

  // Look for any conformance to `Sendable`.
  auto proto = ctx.getProtocol(KnownProtocolKind::Sendable);
  if (!proto)
    return false;

  // Look for a conformance. If it's present and not (directly) missing,
  // we're done.
  auto conformance = nominal->getParentModule()->lookupConformance(
      nominal->getDeclaredInterfaceType(), proto, /*allowMissing=*/true);
  return conformance &&
      !(isa<BuiltinProtocolConformance>(conformance.getConcrete()) &&
        cast<BuiltinProtocolConformance>(
          conformance.getConcrete())->isMissing());
}

/// Find the import that makes the given nominal declaration available.
static Optional<AttributedImport<ImportedModule>> findImportFor(
    NominalTypeDecl *nominal, const DeclContext *fromDC) {
  // If the nominal type is from the current module, there's no import.
  auto nominalModule = nominal->getParentModule();
  if (nominalModule == fromDC->getParentModule())
    return None;

  auto fromSourceFile = fromDC->getParentSourceFile();
  if (!fromSourceFile)
    return None;

  // Look to see if the owning module was directly imported.
  for (const auto &import : fromSourceFile->getImports()) {
    if (import.module.importedModule == nominalModule)
      return import;
  }

  // Now look for transitive imports.
  auto &importCache = nominal->getASTContext().getImportCache();
  for (const auto &import : fromSourceFile->getImports()) {
    auto &importSet = importCache.getImportSet(import.module.importedModule);
    for (const auto &transitive : importSet.getTransitiveImports()) {
      if (transitive.importedModule == nominalModule) {
        return import;
      }
    }
  }

  return None;
}

/// Determine the diagnostic behavior for a Sendable reference to the given
/// nominal type.
DiagnosticBehavior SendableCheckContext::diagnosticBehavior(
    NominalTypeDecl *nominal) const {
  // Determine whether the type was explicitly non-Sendable.
  auto nominalModule = nominal->getParentModule();
  bool isExplicitlyNonSendable = nominalModule->isConcurrencyChecked() ||
      hasExplicitSendableConformance(nominal);

  // Determine whether this nominal type is visible via a @preconcurrency
  // import.
  auto import = findImportFor(nominal, fromDC);

  // When the type is explicitly non-Sendable...
  auto sourceFile = fromDC->getParentSourceFile();
  if (isExplicitlyNonSendable) {
    // @preconcurrency imports downgrade the diagnostic to a warning in Swift 6,
    if (import && import->options.contains(ImportFlags::Preconcurrency)) {
      if (sourceFile)
        sourceFile->setImportUsedPreconcurrency(*import);

      return DiagnosticBehavior::Warning;
    }

    return defaultSendableDiagnosticBehavior(fromDC->getASTContext().LangOpts);
  }

  // When the type is implicitly non-Sendable...

  // @preconcurrency suppresses the diagnostic in Swift 5.x, and
  // downgrades it to a warning in Swift 6 and later.
  if (import && import->options.contains(ImportFlags::Preconcurrency)) {
    if (sourceFile)
      sourceFile->setImportUsedPreconcurrency(*import);

    return nominalModule->getASTContext().LangOpts.isSwiftVersionAtLeast(6)
        ? DiagnosticBehavior::Warning
        : DiagnosticBehavior::Ignore;
  }

  auto defaultBehavior = defaultDiagnosticBehavior();

  // If we're in Swift 5 without -warn-concurrency, suppress the diagnostic
  // entirely. This is the partial rollback of SE-0302
  ASTContext &ctx = fromDC->getASTContext();
  if (!ctx.isSwiftVersionAtLeast(6) && !ctx.LangOpts.WarnConcurrency &&
      (!conformanceCheck ||
       *conformanceCheck == SendableCheck::ImpliedByStandardProtocol))
    defaultBehavior = DiagnosticBehavior::Ignore;

  // If we are checking an implicit Sendable conformance, don't suppress
  // diagnostics for declarations in the same module. We want them so make
  // enclosing inferred types non-Sendable.
  if (defaultBehavior == DiagnosticBehavior::Ignore &&
      nominal->getParentSourceFile() &&
      conformanceCheck && isImplicitSendableCheck(*conformanceCheck))
    return DiagnosticBehavior::Warning;

  return defaultBehavior;
}

/// Produce a diagnostic for a single instance of a non-Sendable type where
/// a Sendable type is required.
static bool diagnoseSingleNonSendableType(
    Type type, SendableCheckContext fromContext, SourceLoc loc,
    llvm::function_ref<bool(Type, DiagnosticBehavior)> diagnose) {

  auto behavior = DiagnosticBehavior::Unspecified;

  auto module = fromContext.fromDC->getParentModule();
  ASTContext &ctx = module->getASTContext();
  auto nominal = type->getAnyNominal();
  if (nominal) {
    behavior = fromContext.diagnosticBehavior(nominal);
  } else {
    behavior = fromContext.defaultDiagnosticBehavior();

    // If we're in Swift 5 without -warn-concurrency, suppress the diagnostic
    // entirely. This is the partial rollback of SE-0302
    ASTContext &ctx = fromContext.fromDC->getASTContext();
    if (!ctx.isSwiftVersionAtLeast(6) && !ctx.LangOpts.WarnConcurrency &&
        (!fromContext.conformanceCheck ||
         *fromContext.conformanceCheck ==
            SendableCheck::ImpliedByStandardProtocol))
      behavior = DiagnosticBehavior::Ignore;
  }

  bool wasSuppressed = diagnose(type, behavior);

  if (behavior == DiagnosticBehavior::Ignore || wasSuppressed) {
    // Don't emit any other diagnostics.
  } else if (type->is<FunctionType>()) {
    ctx.Diags.diagnose(loc, diag::nonsendable_function_type);
  } else if (nominal && nominal->getParentModule() == module) {
    // If the nominal type is in the current module, suggest adding
    // `Sendable` if it might make sense. Otherwise, just complain.
    if (isa<StructDecl>(nominal) || isa<EnumDecl>(nominal)) {
      auto note = nominal->diagnose(
          diag::add_nominal_sendable_conformance,
          nominal->getDescriptiveKind(), nominal->getName());
      addSendableFixIt(nominal, note, /*unchecked=*/false);
    } else {
      nominal->diagnose(
          diag::non_sendable_nominal, nominal->getDescriptiveKind(),
          nominal->getName());
    }
  } else if (nominal) {
    // Note which nominal type does not conform to `Sendable`.
    nominal->diagnose(
        diag::non_sendable_nominal, nominal->getDescriptiveKind(),
        nominal->getName());

    // This type was imported from another module; try to find the
    // corresponding import.
    Optional<AttributedImport<swift::ImportedModule>> import;
    SourceFile *sourceFile = fromContext.fromDC->getParentSourceFile();
    if (sourceFile) {
      import = findImportFor(nominal, fromContext.fromDC);
    }

    // If we found the import that makes this nominal type visible, remark
    // that it can be @preconcurrency import.
    // Only emit this remark once per source file, because it can happen a
    // lot.
    if (import && !import->options.contains(ImportFlags::Preconcurrency) &&
        import->importLoc.isValid() && sourceFile &&
        !sourceFile->hasImportUsedPreconcurrency(*import)) {
      SourceLoc importLoc = import->importLoc;
      ctx.Diags.diagnose(
          importLoc, diag::add_predates_concurrency_import,
          ctx.LangOpts.isSwiftVersionAtLeast(6),
          nominal->getParentModule()->getName())
        .fixItInsert(importLoc, "@preconcurrency ");

      sourceFile->setImportUsedPreconcurrency(*import);
    }
  }

  return behavior == DiagnosticBehavior::Unspecified && !wasSuppressed;
}

bool swift::diagnoseNonSendableTypes(
    Type type, SendableCheckContext fromContext, SourceLoc loc,
    llvm::function_ref<bool(Type, DiagnosticBehavior)> diagnose) {
  auto module = fromContext.fromDC->getParentModule();

  // If the Sendable protocol is missing, do nothing.
  auto proto = module->getASTContext().getProtocol(KnownProtocolKind::Sendable);
  if (!proto)
    return false;

  // FIXME: More detail for unavailable conformances.
  auto conformance = TypeChecker::conformsToProtocol(type, proto, module);
  if (conformance.isInvalid() || hasUnavailableConformance(conformance)) {
    return diagnoseSingleNonSendableType(type, fromContext, loc, diagnose);
  }

  // Walk the conformance, diagnosing any missing Sendable conformances.
  bool anyMissing = false;
  conformance.forEachMissingConformance(module,
      [&](BuiltinProtocolConformance *missing) {
        if (diagnoseSingleNonSendableType(
                missing->getType(), fromContext, loc, diagnose)) {
          anyMissing = true;
        }

        return false;
      });

  return anyMissing;
}

bool swift::diagnoseNonSendableTypesInReference(
    ConcreteDeclRef declRef, const DeclContext *fromDC, SourceLoc loc,
    SendableCheckReason reason) {
  // For functions, check the parameter and result types.
  SubstitutionMap subs = declRef.getSubstitutions();
  if (auto function = dyn_cast<AbstractFunctionDecl>(declRef.getDecl())) {
    for (auto param : *function->getParameters()) {
      Type paramType = param->getInterfaceType().subst(subs);
      if (diagnoseNonSendableTypes(
              paramType, fromDC, loc, diag::non_sendable_param_type,
              (unsigned)reason, function->getDescriptiveKind(),
              function->getName(), getActorIsolation(function)))
        return true;
    }

    // Check the result type of a function.
    if (auto func = dyn_cast<FuncDecl>(function)) {
      Type resultType = func->getResultInterfaceType().subst(subs);
      if (diagnoseNonSendableTypes(
              resultType, fromDC, loc, diag::non_sendable_result_type,
              (unsigned)reason, func->getDescriptiveKind(), func->getName(),
              getActorIsolation(func)))
        return true;
    }

    return false;
  }

  if (auto var = dyn_cast<VarDecl>(declRef.getDecl())) {
    Type propertyType = var->isLocalCapture()
        ? var->getType()
        : var->getValueInterfaceType().subst(subs);
    if (diagnoseNonSendableTypes(
            propertyType, fromDC, loc,
            diag::non_sendable_property_type,
            var->getDescriptiveKind(), var->getName(),
            var->isLocalCapture(),
            (unsigned)reason,
            getActorIsolation(var)))
      return true;
  }

  if (auto subscript = dyn_cast<SubscriptDecl>(declRef.getDecl())) {
    for (auto param : *subscript->getIndices()) {
      Type paramType = param->getInterfaceType().subst(subs);
      if (diagnoseNonSendableTypes(
              paramType, fromDC, loc, diag::non_sendable_param_type,
              (unsigned)reason, subscript->getDescriptiveKind(),
              subscript->getName(), getActorIsolation(subscript)))
        return true;
    }

    // Check the element type of a subscript.
    Type resultType = subscript->getElementInterfaceType().subst(subs);
    if (diagnoseNonSendableTypes(
            resultType, fromDC, loc, diag::non_sendable_result_type,
            (unsigned)reason, subscript->getDescriptiveKind(),
            subscript->getName(), getActorIsolation(subscript)))
      return true;

    return false;
  }

  return false;
}

void swift::diagnoseMissingSendableConformance(
    SourceLoc loc, Type type, const DeclContext *fromDC) {
  diagnoseNonSendableTypes(
      type, fromDC, loc, diag::non_sendable_type);
}

namespace {
  template<typename Visitor>
  bool visitInstanceStorage(
      NominalTypeDecl *nominal, DeclContext *dc, Visitor &visitor);

  /// Infer Sendable from the instance storage of the given nominal type.
  /// \returns \c None if there is no way to make the type \c Sendable,
  /// \c true if \c Sendable needs to be @unchecked, \c false if it can be
  /// \c Sendable without the @unchecked.
  Optional<bool> inferSendableFromInstanceStorage(
      NominalTypeDecl *nominal, SmallVectorImpl<Requirement> &requirements) {
    struct Visitor {
      NominalTypeDecl *nominal;
      SmallVectorImpl<Requirement> &requirements;
      bool isUnchecked = false;
      ProtocolDecl *sendableProto = nullptr;

      Visitor(
          NominalTypeDecl *nominal, SmallVectorImpl<Requirement> &requirements
      ) : nominal(nominal), requirements(requirements) {
        ASTContext &ctx = nominal->getASTContext();
        sendableProto = ctx.getProtocol(KnownProtocolKind::Sendable);
      }

      bool operator()(VarDecl *var, Type propertyType) {
        // If we have a class with mutable state, only an @unchecked
        // conformance will work.
        if (isa<ClassDecl>(nominal) && var->supportsMutation())
          isUnchecked = true;

        return checkType(propertyType);
      }

      bool operator()(EnumElementDecl *element, Type elementType) {
        return checkType(elementType);
      }

      /// Check sendability of the given type, recording any requirements.
      bool checkType(Type type) {
        if (!sendableProto)
          return true;

        auto module = nominal->getParentModule();
        auto conformance = TypeChecker::conformsToProtocol(
            type, sendableProto, module);
        if (conformance.isInvalid())
          return true;

        // If there is an unavailable conformance here, fail.
        if (hasUnavailableConformance(conformance))
          return true;

        // Look for missing Sendable conformances.
        return conformance.forEachMissingConformance(module,
            [&](BuiltinProtocolConformance *missing) {
              // For anything other than Sendable, fail.
              if (missing->getProtocol() != sendableProto)
                return true;

              // If we have an archetype, capture the requirement
              // to make this type Sendable.
              if (missing->getType()->is<ArchetypeType>()) {
                requirements.push_back(
                    Requirement(
                      RequirementKind::Conformance,
                      missing->getType()->mapTypeOutOfContext(),
                      sendableProto->getDeclaredType()));
                return false;
              }

              return true;
            });
      }
    } visitor(nominal, requirements);

    return visitInstanceStorage(nominal, nominal, visitor);
  }
}

static bool checkSendableInstanceStorage(
    NominalTypeDecl *nominal, DeclContext *dc, SendableCheck check);

void swift::diagnoseMissingExplicitSendable(NominalTypeDecl *nominal) {
  // Only diagnose when explicitly requested.
  ASTContext &ctx = nominal->getASTContext();
  if (!ctx.LangOpts.RequireExplicitSendable)
    return;

  if (nominal->getLoc().isInvalid())
    return;

  // Protocols aren't checked.
  if (isa<ProtocolDecl>(nominal))
    return;

  // Actors are always Sendable.
  if (auto classDecl = dyn_cast<ClassDecl>(nominal))
    if (classDecl->isActor())
      return;

  // Only public/open types have this check.
  if (!nominal->getFormalAccessScope(
        /*useDC=*/nullptr,
        /*treatUsableFromInlineAsPublic=*/true).isPublic())
    return;

  // If the conformance is explicitly stated, do nothing.
  if (hasExplicitSendableConformance(nominal))
    return;

  // Diagnose it.
  nominal->diagnose(
      diag::public_decl_needs_sendable, nominal->getDescriptiveKind(),
      nominal->getName());

  // Note to add a Sendable conformance, possibly an unchecked one.
  {
    llvm::SmallVector<Requirement, 2> requirements;
    auto canMakeSendable = inferSendableFromInstanceStorage(
        nominal, requirements);

    // Non-final classes can only have @unchecked.
    bool isUnchecked = !canMakeSendable || *canMakeSendable;
    if (auto classDecl = dyn_cast<ClassDecl>(nominal)) {
      if (!classDecl->isFinal())
        isUnchecked = true;
    }

    auto note = nominal->diagnose(
        isUnchecked ? diag::explicit_unchecked_sendable
                    : diag::add_nominal_sendable_conformance,
        nominal->getDescriptiveKind(), nominal->getName());
    if (canMakeSendable && !requirements.empty()) {
      // Produce a Fix-It containing a conditional conformance to Sendable,
      // based on the requirements harvested from instance storage.

      // Form the where clause containing all of the requirements.
      std::string whereClause;
      {
        llvm::raw_string_ostream out(whereClause);
        llvm::interleaveComma(
            requirements, out,
            [&](const Requirement &req) {
              out << req.getFirstType().getString() << ": "
                  << req.getSecondType().getString();
            });
      }

      // Add a Fix-It containing the conditional extension text itself.
      auto insertionLoc = nominal->getBraces().End;
      note.fixItInsertAfter(
          insertionLoc,
          ("\n\nextension " + nominal->getName().str() + ": "
           + (isUnchecked? "@unchecked " : "") + "Sendable where " +
           whereClause + " { }\n").str());
    } else {
      addSendableFixIt(nominal, note, isUnchecked);
    }
  }

  // Note to disable the warning.
  {
    auto note = nominal->diagnose(
        diag::explicit_disable_sendable, nominal->getDescriptiveKind(),
        nominal->getName());
    auto insertionLoc = nominal->getBraces().End;
    note.fixItInsertAfter(
        insertionLoc,
        ("\n\n@available(*, unavailable)\nextension " + nominal->getName().str()
         + ": Sendable { }\n").str());
  }
}

/// Determine whether this is the main actor type.
/// FIXME: the diagnostics engine has a copy of this.
static bool isMainActor(Type type) {
  if (auto nominal = type->getAnyNominal()) {
    if (nominal->getName().is("MainActor") &&
        nominal->getParentModule()->getName() ==
          nominal->getASTContext().Id_Concurrency)
      return true;
  }

  return false;
}

/// If this DeclContext is an actor, or an extension on an actor, return the
/// NominalTypeDecl, otherwise return null.
static NominalTypeDecl *getSelfActorDecl(const DeclContext *dc) {
  auto nominal = dc->getSelfNominalTypeDecl();
  return nominal && nominal->isActor() ? nominal : nullptr;
}

namespace {
  /// Describes a referenced actor variable and whether it is isolated.
  struct ReferencedActor {
    /// Describes whether the actor variable is isolated or, if it is not
    /// isolated, why it is not isolated.
    enum Kind {
      /// It is isolated.
      Isolated = 0,

      /// It is not an isolated parameter at all.
      NonIsolatedParameter,

      // It is within a Sendable function.
      SendableFunction,

      // It is within a Sendable closure.
      SendableClosure,

      // It is within an 'async let' initializer.
      AsyncLet,

      // It is within a global actor.
      GlobalActor,

      // It is within the main actor.
      MainActor,

      // It is within a nonisolated context.
      NonIsolatedContext,

    };

    VarDecl * const actor;
    /// The outer scope is known to be running on an actor.
    /// We may be isolated to the actor or not, depending on the exact expression
    const bool isPotentiallyIsolated;
    const Kind kind;
    const Type globalActor;

    ReferencedActor(VarDecl *actor, bool isPotentiallyIsolated, Kind kind, Type globalActor = Type())
      : actor(actor),
        isPotentiallyIsolated(isPotentiallyIsolated),
        kind(kind),
        globalActor(globalActor) {}

    static ReferencedActor forGlobalActor(VarDecl *actor,
                                          bool isPotentiallyIsolated,
                                          Type globalActor) {
      Kind kind = isMainActor(globalActor) ? MainActor : GlobalActor;
      return ReferencedActor(actor, isPotentiallyIsolated, kind, globalActor);
    }

    bool isIsolated() const { return kind == Isolated; }

    /// Whether the variable is the "self" of an actor method.
    bool isActorSelf() const {
      if (!actor)
        return false;
      return actor->isActorSelf();
    }

    explicit operator bool() const { return isIsolated(); }
  };

  /// Check for adherence to the actor isolation rules, emitting errors
  /// when actor-isolated declarations are used in an unsafe manner.
  class ActorIsolationChecker : public ASTWalker {
    ASTContext &ctx;
    SmallVector<const DeclContext *, 4> contextStack;
    SmallVector<ApplyExpr*, 4> applyStack;
    SmallVector<std::pair<OpaqueValueExpr *, Expr *>, 4> opaqueValues;

    /// Keeps track of the capture context of variables that have been
    /// explicitly captured in closures.
    llvm::SmallDenseMap<VarDecl *, TinyPtrVector<const DeclContext *>>
      captureContexts;

    using MutableVarSource
        = llvm::PointerUnion<DeclRefExpr *, InOutExpr *, LookupExpr *>;

    using MutableVarParent
        = llvm::PointerUnion<InOutExpr *, LoadExpr *, AssignExpr *>;

    /// Mapping from mutable variable reference exprs, or inout expressions,
    /// to the parent expression, when that parent is either a load or
    /// an inout expr.
    llvm::SmallDenseMap<MutableVarSource, MutableVarParent, 4>
      mutableLocalVarParent;

    /// The values for each case in this enum correspond to %select numbers
    /// in a diagnostic, so be sure to update it if you add new cases.
    enum class VarRefUseEnv {
      Read = 0,
      Mutating = 1,
      Inout = 2 // means Mutating; having a separate kind helps diagnostics
    };

    static bool isPropOrSubscript(ValueDecl const* decl) {
      return isa<VarDecl>(decl) || isa<SubscriptDecl>(decl);
    }

    /// In the given expression \c use that refers to the decl, this
    /// function finds the kind of environment tracked by
    /// \c mutableLocalVarParent that corresponds to that \c use.
    ///
    /// Note that an InoutExpr is not considered a use of the decl!
    ///
    /// @returns None if the context expression is either an InOutExpr,
    ///               not tracked, or if the decl is not a property or subscript
    Optional<VarRefUseEnv> kindOfUsage(ValueDecl *decl, Expr *use) const {
      // we need a use for lookup.
      if (!use)
        return None;

      // must be a property or subscript
      if (!isPropOrSubscript(decl))
        return None;

      if (auto lookup = dyn_cast<DeclRefExpr>(use))
        return usageEnv(lookup);
      else if (auto lookup = dyn_cast<LookupExpr>(use))
        return usageEnv(lookup);

      return None;
    }

    /// @returns the kind of environment in which this expression appears, as
    ///          tracked by \c mutableLocalVarParent
    VarRefUseEnv usageEnv(MutableVarSource src) const {
      auto result = mutableLocalVarParent.find(src);
      if (result != mutableLocalVarParent.end()) {
        MutableVarParent parent = result->second;
        assert(!parent.isNull());
        if (parent.is<LoadExpr*>())
          return VarRefUseEnv::Read;
        else if (parent.is<AssignExpr*>())
          return VarRefUseEnv::Mutating;
        else if (auto inout = parent.dyn_cast<InOutExpr*>())
          return inout->isImplicit() ? VarRefUseEnv::Mutating
                                     : VarRefUseEnv::Inout;
        else
          llvm_unreachable("non-exhaustive case match");
      }
      return VarRefUseEnv::Read; // assume if it's not tracked, it's only read.
    }

    const DeclContext *getDeclContext() const {
      return contextStack.back();
    }

    ModuleDecl *getParentModule() const {
      return getDeclContext()->getParentModule();
    }

    /// In Swift 6, global-actor isolation is not carried-over to the
    /// initializing expressions of non-static instance properties.
    /// The actual change happens in \c getActorIsolationOfContext ,
    /// but this function exists to warn users of Swift 5 about this
    /// isolation change, so that they can prepare ahead-of-time.
    void warnAboutGlobalActorIsoChangeInSwift6(const ActorIsolation &reqIso,
                                               const Expr *user) {
      if (ctx.isSwiftVersionAtLeast(6))
        return;

      // Check our context stack for a PatternBindingInitializer environment.
      DeclContext const* withinDC = nullptr;
      for (auto dc = contextStack.rbegin(); dc != contextStack.rend(); dc++) {
        if (isa<PatternBindingInitializer>(*dc)) {
          withinDC = *dc;
          break;
        }
      }

      // Not within a relevant decl context.
      if (!withinDC)
        return;

      // Check if this PatternBindingInitializer's isolation would change
      // in Swift 6+
      if (auto *var = withinDC->getNonLocalVarDecl()) {
        if (var->isInstanceMember() &&
            !var->getAttrs().hasAttribute<LazyAttr>()) {
          // At this point, we know the isolation will change in Swift 6.
          // So, let's check if that change will cause an error.

          auto dcIso = getActorIsolationOfContext(
                         const_cast<DeclContext*>(withinDC));

          // If the isolation granted in Swift 5 is for a global actor, and
          // the expression requires that global actor's isolation, then it will
          // become an error in Swift 6.
          if (dcIso.isGlobalActor() && dcIso == reqIso) {
            ctx.Diags.diagnose(user->getLoc(),
                               diag::global_actor_from_initializing_expr,
                               reqIso.getGlobalActor(),
                               var->getDescriptiveKind(), var->getName())
            .highlight(user->getSourceRange())
            // make it a warning and attach the "this will become an error..."
            // to the message. The error in Swift 6 will not be this diagnostic.
            .warnUntilSwiftVersion(6);
          }
        }
      }
    }

    /// Determine whether code in the given use context might execute
    /// concurrently with code in the definition context.
    bool mayExecuteConcurrentlyWith(
        const DeclContext *useContext, const DeclContext *defContext);

    /// If the subexpression is a reference to a mutable local variable from a
    /// different context, record its parent. We'll query this as part of
    /// capture semantics in concurrent functions.
    ///
    /// \returns true if we recorded anything, false otherwise.
    bool recordMutableVarParent(MutableVarParent parent, Expr *subExpr) {
      subExpr = subExpr->getValueProvidingExpr();

      if (auto declRef = dyn_cast<DeclRefExpr>(subExpr)) {
        auto var = dyn_cast_or_null<VarDecl>(declRef->getDecl());
        if (!var)
          return false;

        // Only mutable variables matter.
        if (!var->supportsMutation())
          return false;

        // Only mutable variables outside of the current context. This is an
        // optimization, because the parent map won't be queried in this case,
        // and it is the most common case for variables to be referenced in
        // their own context.
        if (var->getDeclContext() == getDeclContext())
          return false;

        assert(mutableLocalVarParent[declRef].isNull());
        mutableLocalVarParent[declRef] = parent;
        return true;
      }

      // For a member reference, try to record a parent for the base expression.
      if (auto memberRef = dyn_cast<MemberRefExpr>(subExpr)) {
        // Record the parent of this LookupExpr too.
        mutableLocalVarParent[memberRef] = parent;
        return recordMutableVarParent(parent, memberRef->getBase());
      }

      // For a subscript, try to record a parent for the base expression.
      if (auto subscript = dyn_cast<SubscriptExpr>(subExpr)) {
        // Record the parent of this LookupExpr too.
        mutableLocalVarParent[subscript] = parent;
        return recordMutableVarParent(parent, subscript->getBase());
      }

      // Look through postfix '!'.
      if (auto force = dyn_cast<ForceValueExpr>(subExpr)) {
        return recordMutableVarParent(parent, force->getSubExpr());
      }

      // Look through postfix '?'.
      if (auto bindOpt = dyn_cast<BindOptionalExpr>(subExpr)) {
        return recordMutableVarParent(parent, bindOpt->getSubExpr());
      }

      if (auto optEval = dyn_cast<OptionalEvaluationExpr>(subExpr)) {
        return recordMutableVarParent(parent, optEval->getSubExpr());
      }

      // & expressions can be embedded for references to mutable variables
      // or subscribes inside a struct/enum.
      if (auto inout = dyn_cast<InOutExpr>(subExpr)) {
        // Record the parent of the inout so we don't look at it again later.
        mutableLocalVarParent[inout] = parent;
        return recordMutableVarParent(parent, inout->getSubExpr());
      }

      return false;
    }

    /// Check closure captures for Sendable violations.
    void checkClosureCaptures(AbstractClosureExpr *closure) {
      if (!isSendableClosure(closure, /*forActorIsolation=*/false))
        return;

      SmallVector<CapturedValue, 2> captures;
      closure->getCaptureInfo().getLocalCaptures(captures);
      for (const auto &capture : captures) {
        if (capture.isDynamicSelfMetadata())
          continue;
        if (capture.isOpaqueValue())
          continue;

        auto decl = capture.getDecl();
        Type type = getDeclContext()
            ->mapTypeIntoContext(decl->getInterfaceType())
            ->getReferenceStorageReferent();
        diagnoseNonSendableTypes(
            type, getDeclContext(), capture.getLoc(),
            diag::non_sendable_capture, decl->getName());
      }
    }

  public:
    ActorIsolationChecker(const DeclContext *dc) : ctx(dc->getASTContext()) {
      contextStack.push_back(dc);
    }

    /// Searches the applyStack from back to front for the inner-most CallExpr
    /// and marks that CallExpr as implicitly async.
    ///
    /// NOTE: Crashes if no CallExpr was found.
    ///
    /// For example, for global actor function `curryAdd`, if we have:
    ///     ((curryAdd 1) 2)
    /// then we want to mark the inner-most CallExpr, `(curryAdd 1)`.
    ///
    /// The same goes for calls to member functions, such as calc.add(1, 2),
    /// aka ((add calc) 1 2), looks like this:
    ///
    ///  (call_expr
    ///    (dot_syntax_call_expr
    ///      (declref_expr add)
    ///      (declref_expr calc))
    ///    (tuple_expr
    ///      ...))
    ///
    /// and we reach up to mark the CallExpr.
    void markNearestCallAsImplicitly(
        Optional<ImplicitActorHopTarget> setAsync,
        bool setThrows = false, bool setDistributedThunk = false) {
      assert(applyStack.size() > 0 && "not contained within an Apply?");

      const auto End = applyStack.rend();
      for (auto I = applyStack.rbegin(); I != End; ++I)
        if (auto call = dyn_cast<CallExpr>(*I)) {
          if (setAsync) call->setImplicitlyAsync(*setAsync);
          if (setThrows) call->setImplicitlyThrows(true);
          if (setDistributedThunk) call->setShouldApplyDistributedThunk(true);
          return;
        }
      llvm_unreachable("expected a CallExpr in applyStack!");
    }

    bool shouldWalkCaptureInitializerExpressions() override { return true; }

    bool shouldWalkIntoTapExpression() override { return true; }

    bool walkToDeclPre(Decl *decl) override {
      if (auto func = dyn_cast<AbstractFunctionDecl>(decl)) {
        contextStack.push_back(func);
      }

      return true;
    }

    bool walkToDeclPost(Decl *decl) override {
      if (auto func = dyn_cast<AbstractFunctionDecl>(decl)) {
        assert(contextStack.back() == func);
        contextStack.pop_back();
      }

      return true;
    }

    std::pair<bool, Expr *> walkToExprPre(Expr *expr) override {
      if (auto *openExistential = dyn_cast<OpenExistentialExpr>(expr)) {
        opaqueValues.push_back({
            openExistential->getOpaqueValue(),
            openExistential->getExistentialValue()});
        return { true, expr };
      }

      if (auto *closure = dyn_cast<AbstractClosureExpr>(expr)) {
        closure->setActorIsolation(determineClosureIsolation(closure));
        checkClosureCaptures(closure);
        contextStack.push_back(closure);
        return { true, expr };
      }

      if (auto inout = dyn_cast<InOutExpr>(expr)) {
        if (!applyStack.empty())
          diagnoseInOutArg(applyStack.back(), inout, false);

        if (mutableLocalVarParent.count(inout) == 0)
          recordMutableVarParent(inout, inout->getSubExpr());
      }

      if (auto assign = dyn_cast<AssignExpr>(expr)) {
        // mark vars in the destination expr as being part of the Assign.
        if (auto destExpr = assign->getDest())
          recordMutableVarParent(assign, destExpr);

        return {true, expr };
      }

      if (auto load = dyn_cast<LoadExpr>(expr))
        recordMutableVarParent(load, load->getSubExpr());

      if (auto lookup = dyn_cast<LookupExpr>(expr)) {
        checkMemberReference(lookup->getBase(), lookup->getMember(),
                             lookup->getLoc(),
                             /*partialApply*/None,
                             lookup);
        return { true, expr };
      }

      if (auto declRef = dyn_cast<DeclRefExpr>(expr)) {
        checkNonMemberReference(
            declRef->getDeclRef(), declRef->getLoc(), declRef);
        return { true, expr };
      }

      if (auto apply = dyn_cast<ApplyExpr>(expr)) {
        applyStack.push_back(apply);  // record this encounter

        // Check the call itself.
        (void)checkApply(apply);

        // If this is a call to a partial apply thunk, decompose it to check it
        // like based on the original written syntax, e.g., "self.method".
        if (auto partialApply = decomposePartialApplyThunk(
                apply, Parent.getAsExpr())) {
          if (auto memberRef = findMemberReference(partialApply->fn)) {
            // NOTE: partially-applied thunks are never annotated as
            // implicitly async, regardless of whether they are escaping.
            checkMemberReference(
                partialApply->base, memberRef->first, memberRef->second,
                partialApply);

            partialApply->base->walk(*this);

            // manual clean-up since normal traversal is skipped
            assert(applyStack.back() == apply);
            applyStack.pop_back();

            return { false, expr };
          }
        }
      }

      // NOTE: SelfApplyExpr is a subtype of ApplyExpr
      if (auto call = dyn_cast<SelfApplyExpr>(expr)) {
        Expr *fn = call->getFn()->getValueProvidingExpr();
        if (auto memberRef = findMemberReference(fn)) {
          checkMemberReference(
              call->getBase(), memberRef->first, memberRef->second,
              /*partialApply=*/None, call);

          call->getBase()->walk(*this);

          if (applyStack.size() >= 2) {
            ApplyExpr *outerCall = applyStack[applyStack.size() - 2];
            if (isAsyncCall(outerCall)) {
              // This call is a partial application within an async call.
              // If the partial application take a value inout, it is bad.
              if (InOutExpr *inoutArg = dyn_cast<InOutExpr>(
                      call->getBase()->getSemanticsProvidingExpr()))
                diagnoseInOutArg(outerCall, inoutArg, true);
            }
          }

          // manual clean-up since normal traversal is skipped
          assert(applyStack.back() == dyn_cast<ApplyExpr>(expr));
          applyStack.pop_back();

          return { false, expr };
        }
      }


      if (auto keyPath = dyn_cast<KeyPathExpr>(expr))
        checkKeyPathExpr(keyPath);

      // The children of #selector expressions are not evaluated, so we do not
      // need to do isolation checking there. This is convenient because such
      // expressions tend to violate restrictions on the use of instance
      // methods.
      if (isa<ObjCSelectorExpr>(expr))
        return { false, expr };

      // Track the capture contexts for variables.
      if (auto captureList = dyn_cast<CaptureListExpr>(expr)) {
        auto *closure = captureList->getClosureBody();
        for (const auto &entry : captureList->getCaptureList()) {
          captureContexts[entry.getVar()].push_back(closure);
        }
      }

      return { true, expr };
    }

    Expr *walkToExprPost(Expr *expr) override {
      if (auto *openExistential = dyn_cast<OpenExistentialExpr>(expr)) {
        assert(opaqueValues.back().first == openExistential->getOpaqueValue());
        opaqueValues.pop_back();
        return expr;
      }

      if (auto *closure = dyn_cast<AbstractClosureExpr>(expr)) {
        assert(contextStack.back() == closure);
        contextStack.pop_back();
      }

      if (auto *apply = dyn_cast<ApplyExpr>(expr)) {
        assert(applyStack.back() == apply);
        applyStack.pop_back();
      }

      // Clear out the mutable local variable parent map on the way out.
      if (auto *declRefExpr = dyn_cast<DeclRefExpr>(expr))
        mutableLocalVarParent.erase(declRefExpr);
      else if (auto *lookupExpr = dyn_cast<LookupExpr>(expr))
        mutableLocalVarParent.erase(lookupExpr);
      else if (auto *inoutExpr = dyn_cast<InOutExpr>(expr))
        mutableLocalVarParent.erase(inoutExpr);

      // Remove the tracked capture contexts.
      if (auto captureList = dyn_cast<CaptureListExpr>(expr)) {
        for (const auto &entry : captureList->getCaptureList()) {
          auto &contexts = captureContexts[entry.getVar()];
          assert(contexts.back() == captureList->getClosureBody());
          contexts.pop_back();
          if (contexts.empty())
            captureContexts.erase(entry.getVar());
        }
      }

      return expr;
    }

  private:
    /// Find the directly-referenced parameter or capture of a parameter for
    /// for the given expression.
    VarDecl *getReferencedParamOrCapture(Expr *expr) {
      return ::getReferencedParamOrCapture(
          expr, [&](OpaqueValueExpr *opaqueValue) -> Expr * {
            for (const auto &known : opaqueValues) {
              if (known.first == opaqueValue) {
                return known.second;
              }
            }
            return nullptr;
          });
    }

    /// Find the isolated actor instance to which the given expression refers.
    ReferencedActor getIsolatedActor(Expr *expr) {
      // Check whether this expression is an isolated parameter or a reference
      // to a capture thereof.
      auto var = getReferencedParamOrCapture(expr);
      bool isPotentiallyIsolated = isPotentiallyIsolatedActor(var);

      // Walk the scopes between the variable reference and the variable
      // declaration to determine whether it is still isolated.
      auto dc = const_cast<DeclContext *>(getDeclContext());
      for (; dc; dc = dc->getParent()) {
        // If we hit the context in which the parameter is declared, we're done.
        if (var && dc == var->getDeclContext()) {
          if (isPotentiallyIsolated) {
            return ReferencedActor(var, isPotentiallyIsolated, ReferencedActor::Isolated);
          }
        }

        // If we've hit a module or type boundary, we're done.
        if (dc->isModuleScopeContext() || dc->isTypeContext())
          break;

        if (auto closure = dyn_cast<AbstractClosureExpr>(dc)) {
          auto isolation = closure->getActorIsolation();
          switch (isolation) {
          case ClosureActorIsolation::Independent:
            if (isSendableClosure(closure, /*forActorIsolation=*/true)) {
              return ReferencedActor(var, isPotentiallyIsolated, ReferencedActor::SendableClosure);
            }

            return ReferencedActor(var, isPotentiallyIsolated, ReferencedActor::NonIsolatedContext);

          case ClosureActorIsolation::ActorInstance:
            // If the closure is isolated to the same variable, we're all set.
            if (isPotentiallyIsolated &&
                (var == isolation.getActorInstance() ||
                 (var->isSelfParamCapture() &&
                  (isolation.getActorInstance()->isSelfParameter() ||
                   isolation.getActorInstance()->isSelfParamCapture())))) {
              return ReferencedActor(var, isPotentiallyIsolated, ReferencedActor::Isolated);
            }

            return ReferencedActor(var, isPotentiallyIsolated, ReferencedActor::NonIsolatedContext);

          case ClosureActorIsolation::GlobalActor:
            return ReferencedActor::forGlobalActor(
                var, isPotentiallyIsolated, isolation.getGlobalActor());
          }
        }

        // Check for an 'async let' autoclosure.
        if (auto autoclosure = dyn_cast<AutoClosureExpr>(dc)) {
          switch (autoclosure->getThunkKind()) {
          case AutoClosureExpr::Kind::AsyncLet:
            return ReferencedActor(var, isPotentiallyIsolated, ReferencedActor::AsyncLet);

          case AutoClosureExpr::Kind::DoubleCurryThunk:
          case AutoClosureExpr::Kind::SingleCurryThunk:
          case AutoClosureExpr::Kind::None:
            break;
          }
        }

        // Look through defers.
        // FIXME: should this be covered automatically by the logic below?
        if (auto func = dyn_cast<FuncDecl>(dc))
          if (func->isDeferBody())
            continue;

        if (auto func = dyn_cast<AbstractFunctionDecl>(dc)) {
          // @Sendable functions are nonisolated.
          if (func->isSendable())
            return ReferencedActor(var, isPotentiallyIsolated, ReferencedActor::SendableFunction);
        }

        // Check isolation of the context itself. We do this separately
        // from the closure check because closures capture specific variables
        // while general isolation is declaration-based.
        switch (auto isolation = getActorIsolationOfContext(dc)) {
        case ActorIsolation::Independent:
        case ActorIsolation::Unspecified:
          // Local functions can capture an isolated parameter.
          // FIXME: This really should be modeled by getActorIsolationOfContext.
          if (isa<FuncDecl>(dc) && cast<FuncDecl>(dc)->isLocalCapture()) {
            // FIXME: Local functions could presumably capture an isolated
            // parameter that isn't 'self'.
            if (isPotentiallyIsolated &&
                (var->isSelfParameter() || var->isSelfParamCapture()))
              continue;
          }

          return ReferencedActor(var, isPotentiallyIsolated, ReferencedActor::NonIsolatedContext);

        case ActorIsolation::GlobalActor:
        case ActorIsolation::GlobalActorUnsafe:
          return ReferencedActor::forGlobalActor(
              var, isPotentiallyIsolated, isolation.getGlobalActor());

        case ActorIsolation::ActorInstance:
        case ActorIsolation::DistributedActorInstance:
          break;
        }
      }

      if (isPotentiallyIsolated)
        return ReferencedActor(var, isPotentiallyIsolated, ReferencedActor::NonIsolatedContext);

      return ReferencedActor(var, isPotentiallyIsolated, ReferencedActor::NonIsolatedParameter);
    }

    /// If the expression is a reference to `self`, the `self` declaration.
    VarDecl *getReferencedSelf(Expr *expr) {
      if (auto selfVar = getReferencedParamOrCapture(expr))
        if (selfVar->isSelfParameter() || selfVar->isSelfParamCapture())
          return selfVar;

      // Not a self reference.
      return nullptr;
    }

    static FuncDecl *findAnnotatableFunction(DeclContext *dc) {
      auto fn = dyn_cast<FuncDecl>(dc);
      if (!fn) return nullptr;
      if (fn->isDeferBody())
        return findAnnotatableFunction(fn->getDeclContext());
      return fn;
    }

    /// Note when the enclosing context could be put on a global actor.
    void noteGlobalActorOnContext(DeclContext *dc, Type globalActor) {
      // If we are in a synchronous function on the global actor,
      // suggest annotating with the global actor itself.
      if (auto fn = findAnnotatableFunction(dc)) {
        // Suppress this for accesssors because you can't change the
        // actor isolation of an individual accessor.  Arguably we could
        // add this to the entire storage declaration, though.
        // Suppress this for async functions out of caution; but don't
        // suppress it if we looked through a defer.
        if (!isa<AccessorDecl>(fn) &&
            (!fn->isAsyncContext() || fn != dc)) {
          switch (getActorIsolation(fn)) {
          case ActorIsolation::ActorInstance:
          case ActorIsolation::DistributedActorInstance:
          case ActorIsolation::GlobalActor:
          case ActorIsolation::GlobalActorUnsafe:
          case ActorIsolation::Independent:
            return;

          case ActorIsolation::Unspecified:
            fn->diagnose(diag::note_add_globalactor_to_function,
                globalActor->getWithoutParens().getString(),
                fn->getDescriptiveKind(),
                fn->getName(),
                globalActor)
              .fixItInsert(fn->getAttributeInsertionLoc(false),
                diag::insert_globalactor_attr, globalActor);
              return;
          }
        }
      }
    }

    /// Note that the given actor member is isolated.
    /// @param context is allowed to be null if no context is appropriate.
    void noteIsolatedActorMember(ValueDecl *decl, Expr *context) {
      // detect if it is a distributed actor, to provide better isolation notes

      auto isDistributedActor = false;
      if (auto nominal = decl->getDeclContext()->getSelfNominalTypeDecl())
        isDistributedActor = nominal->isDistributedActor();

      // FIXME: Make this diagnostic more sensitive to the isolation context of
      // the declaration.
      if (isDistributedActor) {
        if (dyn_cast<VarDecl>(decl)) {
          // Distributed actor properties are never accessible externally.
          decl->diagnose(diag::distributed_actor_isolated_property);
        } else {
          // it's a function or subscript
          decl->diagnose(diag::note_distributed_actor_isolated_method,
                         decl->getDescriptiveKind(),
                         decl->getName());
        }
      } else if (auto func = dyn_cast<AbstractFunctionDecl>(decl)) {
        func->diagnose(diag::actor_isolated_sync_func,
          decl->getDescriptiveKind(),
          decl->getName());

        // was it an attempt to mutate an actor instance's isolated state?
      } else if (auto environment = kindOfUsage(decl, context)) {

        if (environment.getValue() == VarRefUseEnv::Read)
          decl->diagnose(diag::kind_declared_here, decl->getDescriptiveKind());
        else
          decl->diagnose(diag::actor_mutable_state, decl->getDescriptiveKind());

      } else {
        decl->diagnose(diag::kind_declared_here, decl->getDescriptiveKind());
      }
    }

    // Retrieve the nearest enclosing actor context.
    static NominalTypeDecl *getNearestEnclosingActorContext(
        const DeclContext *dc) {
      while (!dc->isModuleScopeContext()) {
        if (dc->isTypeContext()) {
          // FIXME: Protocol extensions need specific handling here.
          if (auto nominal = dc->getSelfNominalTypeDecl()) {
            if (nominal->isActor())
              return nominal;
          }
        }

        dc = dc->getParent();
      }

      return nullptr;
    }

    /// Diagnose a reference to an unsafe entity.
    ///
    /// \returns true if we diagnosed the entity, \c false otherwise.
    bool diagnoseReferenceToUnsafeGlobal(ValueDecl *value, SourceLoc loc) {
      if (!getDeclContext()->getParentModule()->isConcurrencyChecked())
        return false;

      // Only diagnose direct references to mutable global state.
      auto var = dyn_cast<VarDecl>(value);
      if (!var || var->isLet())
        return false;

      if (!var->getDeclContext()->isModuleScopeContext() && !var->isStatic())
        return false;

      ctx.Diags.diagnose(
          loc, diag::shared_mutable_state_access,
          value->getDescriptiveKind(), value->getName());
      value->diagnose(diag::kind_declared_here, value->getDescriptiveKind());
      return true;
    }

    /// Diagnose an inout argument passed into an async call
    ///
    /// \returns true if we diagnosed the entity, \c false otherwise.
    bool diagnoseInOutArg(const ApplyExpr *call, const InOutExpr *arg,
                          bool isPartialApply) {
      // check that the call is actually async
      if (!isAsyncCall(call))
        return false;

      bool result = false;
      auto checkDiagnostic = [this, call, isPartialApply,
                              &result](ValueDecl *decl, SourceLoc argLoc) {
        auto isolation = ActorIsolationRestriction::forDeclaration(
            decl, getDeclContext());
        switch (isolation) {
        case ActorIsolationRestriction::Unrestricted:
        case ActorIsolationRestriction::Unsafe:
          break;
        case ActorIsolationRestriction::GlobalActorUnsafe:
          // If we're not supposed to diagnose existing data races here,
          // we're done.
          if (!shouldDiagnoseExistingDataRaces(getDeclContext()))
            break;

          LLVM_FALLTHROUGH;

        case ActorIsolationRestriction::GlobalActor: {
          ctx.Diags.diagnose(argLoc, diag::actor_isolated_inout_state,
                             decl->getDescriptiveKind(), decl->getName(),
                             call->isImplicitlyAsync().hasValue());
          decl->diagnose(diag::kind_declared_here, decl->getDescriptiveKind());
          result = true;
          break;
        }
        case ActorIsolationRestriction::CrossActorSelf:
        case ActorIsolationRestriction::ActorSelf: {
          if (isPartialApply) {
            // The partially applied InoutArg is a property of actor. This
            // can really only happen when the property is a struct with a
            // mutating async method.
            if (auto partialApply = dyn_cast<ApplyExpr>(call->getFn())) {
              ValueDecl *fnDecl =
                  cast<DeclRefExpr>(partialApply->getFn())->getDecl();
              ctx.Diags.diagnose(call->getLoc(),
                                 diag::actor_isolated_mutating_func,
                                 fnDecl->getName(), decl->getDescriptiveKind(),
                                 decl->getName());
              result = true;
            }
          } else {
            ctx.Diags.diagnose(argLoc, diag::actor_isolated_inout_state,
                               decl->getDescriptiveKind(), decl->getName(),
                               call->isImplicitlyAsync().hasValue());
            result = true;
          }
          break;
        }
        }
      };
      auto expressionWalker = [baseArg = arg->getSubExpr(),
                               checkDiagnostic](Expr *expr) -> Expr * {
        if (isa<InOutExpr>(expr))
          return nullptr; // AST walker will hit this again
        if (LookupExpr *lookup = dyn_cast<LookupExpr>(expr)) {
          if (isa<DeclRefExpr>(lookup->getBase())) {
            checkDiagnostic(lookup->getMember().getDecl(), baseArg->getLoc());
            return nullptr; // Diagnosed. Don't keep walking
          }
        }
        if (DeclRefExpr *declRef = dyn_cast<DeclRefExpr>(expr)) {
          checkDiagnostic(declRef->getDecl(), baseArg->getLoc());
          return nullptr; // Diagnosed. Don't keep walking
        }
        return expr;
      };
      arg->getSubExpr()->forEachChildExpr(expressionWalker);
      return result;
    }

    /// Get the actor isolation of the innermost relevant context.
    ActorIsolation getInnermostIsolatedContext(DeclContext *dc) {
      // Retrieve the actor isolation of the context.
      switch (auto isolation = getActorIsolationOfContext(dc)) {
      case ActorIsolation::ActorInstance:
      case ActorIsolation::DistributedActorInstance:
      case ActorIsolation::Independent:
      case ActorIsolation::Unspecified:
        return isolation;

      case ActorIsolation::GlobalActor:
      case ActorIsolation::GlobalActorUnsafe:
        return ActorIsolation::forGlobalActor(
            dc->mapTypeIntoContext(isolation.getGlobalActor()),
            isolation == ActorIsolation::GlobalActorUnsafe);
      }
    }

    enum class AsyncMarkingResult {
      FoundAsync, // successfully marked an implicitly-async operation
      NotFound,  // fail: no valid implicitly-async operation was found
      SyncContext, // fail: a valid implicitly-async op, but in sync context
      NotSendable,  // fail: valid op and context, but not Sendable
      NotDistributed, // fail: non-distributed declaration in distributed actor
    };

    /// Determine whether we can access the given declaration that is
    /// isolated to a distributed actor from a location that is potentially not
    /// local to this process.
    ///
    /// \returns the (setThrows, isDistributedThunk) bits to implicitly
    /// mark the access/call with on success, or emits an error and returns
    /// \c None.
    Optional<std::pair<bool, bool>>
    checkDistributedAccess(SourceLoc declLoc, ValueDecl *decl,
                           Expr *context) {
      // Cannot reference properties or subscripts of distributed actors.
      if (isPropOrSubscript(decl)) {
        ctx.Diags.diagnose(
            declLoc, diag::distributed_actor_isolated_non_self_reference,
            decl->getDescriptiveKind(), decl->getName());
        noteIsolatedActorMember(decl, context);
        return None;
      }

      // Check that we have a distributed function.
      auto func = dyn_cast<AbstractFunctionDecl>(decl);
      if (!func || !func->isDistributed()) {
        ctx.Diags.diagnose(declLoc,
                           diag::distributed_actor_isolated_method)
          .fixItInsert(decl->getAttributeInsertionLoc(true), "distributed ");

        noteIsolatedActorMember(decl, context);
        return None;
      }

      return std::make_pair(!func->hasThrows(), true);
    }

    /// Attempts to identify and mark a valid cross-actor use of a synchronous
    /// actor-isolated member (e.g., sync function application, property access)
    AsyncMarkingResult tryMarkImplicitlyAsync(SourceLoc declLoc,
                                              ConcreteDeclRef concDeclRef,
                                              Expr* context,
                                              ImplicitActorHopTarget target,
                                              bool isDistributed) {
      ValueDecl *decl = concDeclRef.getDecl();
      AsyncMarkingResult result = AsyncMarkingResult::NotFound;
      bool isAsyncCall = false;

      // is it an access to a property?
      if (isPropOrSubscript(decl)) {
        // Cannot reference properties or subscripts of distributed actors.
        if (isDistributed && !checkDistributedAccess(declLoc, decl, context))
          return AsyncMarkingResult::NotDistributed;

        if (auto declRef = dyn_cast_or_null<DeclRefExpr>(context)) {
          if (usageEnv(declRef) == VarRefUseEnv::Read) {
            if (!getDeclContext()->isAsyncContext())
              return AsyncMarkingResult::SyncContext;

            declRef->setImplicitlyAsync(target);
            result = AsyncMarkingResult::FoundAsync;
          }
        } else if (auto lookupExpr = dyn_cast_or_null<LookupExpr>(context)) {
          if (usageEnv(lookupExpr) == VarRefUseEnv::Read) {

            if (!getDeclContext()->isAsyncContext())
              return AsyncMarkingResult::SyncContext;

            lookupExpr->setImplicitlyAsync(target);
            result = AsyncMarkingResult::FoundAsync;
          }
        }

      } else if (isa_and_nonnull<SelfApplyExpr>(context) &&
          isa<AbstractFunctionDecl>(decl)) {
        // actor-isolated non-isolated-self calls are implicitly async
        // and thus OK.

        if (!getDeclContext()->isAsyncContext())
          return AsyncMarkingResult::SyncContext;

        isAsyncCall = true;
      } else if (!applyStack.empty()) {
        // Check our applyStack metadata from the traversal.
        // Our goal is to identify whether the actor reference appears
        // as the called value of the enclosing ApplyExpr. We cannot simply
        // inspect Parent here because of expressions like (callee)()
        // and the fact that the reference may be just an argument to an apply
        ApplyExpr *apply = applyStack.back();
        Expr *fn = apply->getFn()->getValueProvidingExpr();
        if (auto memberRef = findMemberReference(fn)) {
          auto concDecl = memberRef->first;
          if (decl == concDecl.getDecl() && !apply->isImplicitlyAsync()) {

            if (!getDeclContext()->isAsyncContext())
              return AsyncMarkingResult::SyncContext;

            // then this ValueDecl appears as the called value of the ApplyExpr.
            isAsyncCall = true;
          }
        }
      }

      // Set up an implicit async call.
      if (isAsyncCall) {
        // If we're calling to a distributed actor, make sure the function
        // is actually 'distributed'.
        bool setThrows = false;
        bool usesDistributedThunk = false;
        if (isDistributed) {
          if (auto access = checkDistributedAccess(declLoc, decl, context))
            std::tie(setThrows, usesDistributedThunk) = *access;
          else
            return AsyncMarkingResult::NotDistributed;
        }

        // Mark call as implicitly 'async', and also potentially as
        // throwing and using a distributed thunk.
        markNearestCallAsImplicitly(
            /*setAsync=*/target, setThrows, usesDistributedThunk);
        result = AsyncMarkingResult::FoundAsync;
      }

      if (result == AsyncMarkingResult::FoundAsync) {
        // Check for non-sendable types.
        bool problemFound =
            diagnoseNonSendableTypesInReference(
              concDeclRef, getDeclContext(), declLoc,
              SendableCheckReason::SynchronousAsAsync);
        if (problemFound)
          result = AsyncMarkingResult::NotSendable;
      }

      return result;
    }

    /// Check actor isolation for a particular application.
    bool checkApply(ApplyExpr *apply) {
      auto fnExprType = apply->getFn()->getType();
      if (!fnExprType)
        return false;

      auto fnType = fnExprType->getAs<FunctionType>();
      if (!fnType)
        return false;

      // The isolation of the context we're in.
      Optional<ActorIsolation> contextIsolation;
      auto getContextIsolation = [&]() -> ActorIsolation {
        if (contextIsolation)
          return *contextIsolation;

        auto declContext = const_cast<DeclContext *>(getDeclContext());
        contextIsolation = getInnermostIsolatedContext(declContext);
        return *contextIsolation;
      };

      // If the function type is global-actor-qualified, determine whether
      // we are within that global actor already.
      Optional<ActorIsolation> unsatisfiedIsolation;
      if (Type globalActor = fnType->getGlobalActor()) {
        if (getContextIsolation().isGlobalActor() &&
            getContextIsolation().getGlobalActor()->isEqual(globalActor)) {
          warnAboutGlobalActorIsoChangeInSwift6(
              ActorIsolation::forGlobalActor(globalActor, false),
              apply);
        } else {
          unsatisfiedIsolation = ActorIsolation::forGlobalActor(
              globalActor, /*unsafe=*/false);
        }
      }

      if (isa<SelfApplyExpr>(apply) && !unsatisfiedIsolation)
        return false;

      // Check for isolated parameters.
      Optional<unsigned> isolatedParamIdx;
      for (unsigned paramIdx : range(fnType->getNumParams())) {
        // We only care about isolated parameters.
        if (!fnType->getParams()[paramIdx].isIsolated())
          continue;

        auto *args = apply->getArgs();
        if (paramIdx >= args->size())
          continue;

        auto *arg = args->getExpr(paramIdx);
        if (getIsolatedActor(arg))
          continue;

        // An isolated parameter was provided with a non-isolated argument.
        // FIXME: The modeling of unsatisfiedIsolation is not great here.
        // We'd be better off using something more like closure isolation
        // that can talk about specific parameters.
        auto nominal = arg->getType()->getAnyNominal();
        if (!nominal) {
          nominal = arg->getType()->getASTContext().getProtocol(
              KnownProtocolKind::Actor);
        }

        unsatisfiedIsolation = ActorIsolation::forActorInstance(nominal);
        isolatedParamIdx = paramIdx;
        break;
      }

      // If there was no unsatisfied actor isolation, we're done.
      if (!unsatisfiedIsolation)
        return false;

      // If we are not in an asynchronous context, complain.
      if (!getDeclContext()->isAsyncContext()) {
        if (auto calleeDecl = apply->getCalledValue()) {
          ctx.Diags.diagnose(
              apply->getLoc(), diag::actor_isolated_call_decl,
              *unsatisfiedIsolation,
              calleeDecl->getDescriptiveKind(), calleeDecl->getName(),
              getContextIsolation());
          calleeDecl->diagnose(
              diag::actor_isolated_sync_func, calleeDecl->getDescriptiveKind(),
              calleeDecl->getName());
        } else {
          ctx.Diags.diagnose(
              apply->getLoc(), diag::actor_isolated_call, *unsatisfiedIsolation,
              getContextIsolation());
        }

        if (unsatisfiedIsolation->isGlobalActor()) {
          noteGlobalActorOnContext(
              const_cast<DeclContext *>(getDeclContext()),
              unsatisfiedIsolation->getGlobalActor());
        }

        return true;
      }

      // Mark as implicitly async.
      if (!fnType->getExtInfo().isAsync()) {
        switch (*unsatisfiedIsolation) {
        case ActorIsolation::GlobalActor:
        case ActorIsolation::GlobalActorUnsafe:
          apply->setImplicitlyAsync(
              ImplicitActorHopTarget::forGlobalActor(
                unsatisfiedIsolation->getGlobalActor()));
          break;

        case ActorIsolation::DistributedActorInstance:
        case ActorIsolation::ActorInstance:
          apply->setImplicitlyAsync(
            ImplicitActorHopTarget::forIsolatedParameter(*isolatedParamIdx));
          break;

        case ActorIsolation::Unspecified:
        case ActorIsolation::Independent:
          llvm_unreachable("Not actor-isolated");
        }
      }

      // Check for sendability of the parameter types.
      auto params = fnType->getParams();
      for (unsigned paramIdx : indices(params)) {
        const auto &param = params[paramIdx];

        // Dig out the location of the argument.
        SourceLoc argLoc = apply->getLoc();
        if (auto argList = apply->getArgs()) {
          auto arg = argList->get(paramIdx);
          if (arg.getStartLoc().isValid())
            argLoc = arg.getStartLoc();
        }

        if (diagnoseNonSendableTypes(
                param.getParameterType(), getDeclContext(), argLoc,
                diag::non_sendable_call_param_type,
                apply->isImplicitlyAsync().hasValue(),
                *unsatisfiedIsolation))
          return true;
      }

      // Check for sendability of the result type.
      if (diagnoseNonSendableTypes(
             fnType->getResult(), getDeclContext(), apply->getLoc(),
             diag::non_sendable_call_result_type,
             apply->isImplicitlyAsync().hasValue(),
             *unsatisfiedIsolation))
        return true;

      return false;
    }

    /// Check a reference to an entity within a global actor.
    bool checkGlobalActorReference(
        ConcreteDeclRef valueRef, SourceLoc loc, Type globalActor,
        bool isCrossActor,
        Expr *context) {
      ValueDecl *value = valueRef.getDecl();
      auto declContext = const_cast<DeclContext *>(getDeclContext());

      // Check whether we are within the same isolation context, in which
      // case there is nothing further to check,
      auto contextIsolation = getInnermostIsolatedContext(declContext);
      if (contextIsolation.isGlobalActor() &&
          contextIsolation.getGlobalActor()->isEqual(globalActor)) {

        warnAboutGlobalActorIsoChangeInSwift6(contextIsolation, context);
        return false;
      }

      // A cross-actor access requires types to be concurrent-safe.
      if (isCrossActor) {
        return diagnoseNonSendableTypesInReference(
            valueRef, getDeclContext(), loc,
            SendableCheckReason::CrossActor);
      }

      // Call is implicitly asynchronous.
      auto result = tryMarkImplicitlyAsync(
        loc, valueRef, context,
        ImplicitActorHopTarget::forGlobalActor(globalActor),
        /*FIXME if we get global distributed actors*/false);
      if (result == AsyncMarkingResult::FoundAsync)
        return false;

      // Diagnose failures.
      switch (contextIsolation) {
      case ActorIsolation::DistributedActorInstance:
      case ActorIsolation::ActorInstance: {
        auto useKind = static_cast<unsigned>(
            kindOfUsage(value, context).getValueOr(VarRefUseEnv::Read));

        ctx.Diags.diagnose(loc, diag::global_actor_from_instance_actor_context,
                           value->getDescriptiveKind(), value->getName(),
                           globalActor, contextIsolation.getActor()->getName(),
                           useKind, result == AsyncMarkingResult::SyncContext);
        noteIsolatedActorMember(value, context);
        return true;
      }

      case ActorIsolation::GlobalActor:
      case ActorIsolation::GlobalActorUnsafe: {
        auto useKind = static_cast<unsigned>(
            kindOfUsage(value, context).getValueOr(VarRefUseEnv::Read));

        // Otherwise, this is a problematic global actor decl reference.
        ctx.Diags.diagnose(
            loc, diag::global_actor_from_other_global_actor_context,
            value->getDescriptiveKind(), value->getName(), globalActor,
            contextIsolation.getGlobalActor(), useKind,
            result == AsyncMarkingResult::SyncContext);
        noteIsolatedActorMember(value, context);
        return true;
      }

      case ActorIsolation::Independent: {
        auto useKind = static_cast<unsigned>(
            kindOfUsage(value, context).getValueOr(VarRefUseEnv::Read));

        ctx.Diags.diagnose(loc, diag::global_actor_from_nonactor_context,
                           value->getDescriptiveKind(), value->getName(),
                           globalActor,
                           /*actorIndependent=*/true, useKind,
                           result == AsyncMarkingResult::SyncContext);
        noteIsolatedActorMember(value, context);
        return true;
      }

      case ActorIsolation::Unspecified: {
        // Diagnose the reference.
        auto useKind = static_cast<unsigned>(
            kindOfUsage(value, context).getValueOr(VarRefUseEnv::Read));
        ctx.Diags.diagnose(
          loc, diag::global_actor_from_nonactor_context,
          value->getDescriptiveKind(), value->getName(), globalActor,
          /*actorIndependent=*/false, useKind,
          result == AsyncMarkingResult::SyncContext);
        noteGlobalActorOnContext(declContext, globalActor);
        noteIsolatedActorMember(value, context);

        return true;
      } // end Unspecified case
      } // end switch
      llvm_unreachable("unhandled actor isolation kind!");
    }

    /// Find the innermost context in which this declaration was explicitly
    /// captured.
    const DeclContext *findCapturedDeclContext(ValueDecl *value) {
      assert(value->isLocalCapture());
      auto var = dyn_cast<VarDecl>(value);
      if (!var)
        return value->getDeclContext();

      auto knownContexts = captureContexts.find(var);
      if (knownContexts == captureContexts.end())
        return value->getDeclContext();

      return knownContexts->second.back();
    }

    /// Check a reference to a local capture.
    bool checkLocalCapture(
        ConcreteDeclRef valueRef, SourceLoc loc, DeclRefExpr *declRefExpr) {
      auto value = valueRef.getDecl();

      // Check whether we are in a context that will not execute concurrently
      // with the context of 'self'. If not, it's safe.
      if (!mayExecuteConcurrentlyWith(
              getDeclContext(), findCapturedDeclContext(value)))
        return false;

      // Check whether this is a local variable, in which case we can
      // determine whether it was safe to access concurrently.
      if (auto var = dyn_cast<VarDecl>(value)) {
        // Ignore interpolation variables.
        if (var->getBaseName() == ctx.Id_dollarInterpolation)
          return false;

        auto parent = mutableLocalVarParent[declRefExpr];

        // If the variable is immutable, it's fine so long as it involves
        // Sendable types.
        //
        // When flow-sensitive concurrent captures are enabled, we also
        // allow reads, depending on a SIL diagnostic pass to identify the
        // remaining race conditions.
        if (!var->supportsMutation() ||
            (ctx.LangOpts.EnableExperimentalFlowSensitiveConcurrentCaptures &&
             parent.dyn_cast<LoadExpr *>())) {
          return false;
        }

        // Otherwise, we have concurrent access. Complain.
        ctx.Diags.diagnose(
            loc, diag::concurrent_access_of_local_capture,
            parent.dyn_cast<LoadExpr *>(),
            var->getDescriptiveKind(), var->getName());
        return true;
      }

      if (auto func = dyn_cast<FuncDecl>(value)) {
        if (func->isSendable())
          return false;

        func->diagnose(
            diag::local_function_executed_concurrently,
            func->getDescriptiveKind(), func->getName())
          .fixItInsert(func->getAttributeInsertionLoc(false), "@Sendable ");

        // Add the @Sendable attribute implicitly, so we don't diagnose
        // again.
        const_cast<FuncDecl *>(func)->getAttrs().add(
            new (ctx) SendableAttr(true));
        return true;
      }

      // Concurrent access to some other local.
      ctx.Diags.diagnose(
          loc, diag::concurrent_access_local,
          value->getDescriptiveKind(), value->getName());
      value->diagnose(
          diag::kind_declared_here, value->getDescriptiveKind());
      return true;
    }

    ///
    /// \return true iff a diagnostic was emitted
    bool checkKeyPathExpr(KeyPathExpr *keyPath) {
      bool diagnosed = false;

      // returns None if it is not a 'let'-bound var decl. Otherwise,
      // the bool indicates whether a diagnostic was emitted.
      auto checkLetBoundVarDecl = [&](KeyPathExpr::Component const& component)
                                                            -> Optional<bool> {
        auto decl = component.getDeclRef().getDecl();
        if (auto varDecl = dyn_cast<VarDecl>(decl)) {
          if (varDecl->isLet()) {
            auto type = component.getComponentType();
            if (shouldDiagnoseExistingDataRaces(getDeclContext()) &&
                diagnoseNonSendableTypes(
                    type, getDeclContext(), component.getLoc(),
                    diag::non_sendable_keypath_access))
              return true;

            return false;
          }
        }
        return None;
      };

      // check the components of the keypath.
      for (const auto &component : keyPath->getComponents()) {
        // The decl referred to by the path component cannot be within an actor.
        if (component.hasDeclRef()) {
          auto concDecl = component.getDeclRef();
          auto isolation = ActorIsolationRestriction::forDeclaration(
              concDecl, getDeclContext());

          switch (isolation.getKind()) {
          case ActorIsolationRestriction::Unsafe:
          case ActorIsolationRestriction::Unrestricted:
            break; // OK. Does not refer to an actor-isolated member.

          case ActorIsolationRestriction::GlobalActorUnsafe:
            // Only check if we're in code that's adopted concurrency features.
            if (!shouldDiagnoseExistingDataRaces(getDeclContext()))
              break; // do not check

            LLVM_FALLTHROUGH; // otherwise, perform checking

          case ActorIsolationRestriction::GlobalActor:
            // Disable global actor checking for now.
            if (!ctx.LangOpts.isSwiftVersionAtLeast(6))
              break;

            LLVM_FALLTHROUGH; // otherwise, it's invalid so diagnose it.

          case ActorIsolationRestriction::CrossActorSelf:
            // 'let'-bound decls with this isolation are OK, just check them.
            if (auto wasLetBound = checkLetBoundVarDecl(component)) {
              diagnosed = wasLetBound.getValue();
              break;
            }
            LLVM_FALLTHROUGH; // otherwise, it's invalid so diagnose it.

          case ActorIsolationRestriction::ActorSelf: {
            auto decl = concDecl.getDecl();
            ctx.Diags.diagnose(component.getLoc(),
                               diag::actor_isolated_keypath_component,
                               isolation.getKind() ==
                                  ActorIsolationRestriction::CrossActorSelf,
                               decl->getDescriptiveKind(), decl->getName());
            diagnosed = true;
            break;
          }
          }; // end switch
        }

        // Captured values in a path component must conform to Sendable.
        // These captured values appear in Subscript, such as \Type.dict[k]
        // where k is a captured dictionary key.
        if (auto *args = component.getSubscriptArgs()) {
          for (auto arg : *args) {
            auto type = arg.getExpr()->getType();
            if (type &&
                shouldDiagnoseExistingDataRaces(getDeclContext()) &&
                diagnoseNonSendableTypes(
                    type, getDeclContext(), component.getLoc(),
                    diag::non_sendable_keypath_capture))
              diagnosed = true;
          }
        }
      }

      return diagnosed;
    }

    static AbstractFunctionDecl const *
    isActorInitOrDeInitContext(const DeclContext *dc) {
      return ::isActorInitOrDeInitContext(
          dc, [](const AbstractClosureExpr *closure) {
            return isSendableClosure(closure, /*forActorIsolation=*/false);
          });
    }

    static bool isConvenienceInit(AbstractFunctionDecl const* fn) {
      if (auto ctor = dyn_cast_or_null<ConstructorDecl>(fn))
        return ctor->isConvenienceInit();

      return false;
    }

    /// Check a reference to a local or global.
    bool checkNonMemberReference(
        ConcreteDeclRef valueRef, SourceLoc loc, DeclRefExpr *declRefExpr) {
      if (!valueRef)
        return false;

      auto value = valueRef.getDecl();

      if (value->isLocalCapture())
        return checkLocalCapture(valueRef, loc, declRefExpr);

      switch (auto isolation =
                  ActorIsolationRestriction::forDeclaration(
                    valueRef, getDeclContext())) {
      case ActorIsolationRestriction::Unrestricted:
        return false;

      case ActorIsolationRestriction::CrossActorSelf:
      case ActorIsolationRestriction::ActorSelf:
        llvm_unreachable("non-member reference into an actor");

      case ActorIsolationRestriction::GlobalActorUnsafe:
        // Only complain if we're in code that's adopted concurrency features.
        if (!shouldDiagnoseExistingDataRaces(getDeclContext()))
          return false;

        LLVM_FALLTHROUGH;

      case ActorIsolationRestriction::GlobalActor:
        return checkGlobalActorReference(
            valueRef, loc, isolation.getGlobalActor(), isolation.isCrossActor,
            declRefExpr);

      case ActorIsolationRestriction::Unsafe:
        return diagnoseReferenceToUnsafeGlobal(value, loc);
      }
      llvm_unreachable("unhandled actor isolation kind!");
    }

    /// Check a reference with the given base expression to the given member.
    /// Returns true iff the member reference refers to actor-isolated state
    /// in an invalid or unsafe way such that a diagnostic was emitted.
    bool checkMemberReference(
        Expr *base, ConcreteDeclRef memberRef, SourceLoc memberLoc,
        Optional<PartialApplyThunkInfo> partialApply = None,
        Expr *context = nullptr) {
      if (!base || !memberRef)
        return false;

      auto member = memberRef.getDecl();
      switch (auto isolation =
                  ActorIsolationRestriction::forDeclaration(
                    memberRef, getDeclContext())) {
      case ActorIsolationRestriction::Unrestricted:
        return false;

      case ActorIsolationRestriction::CrossActorSelf: {
        // If a cross-actor reference is to an isolated actor, it's not
        // crossing actors.
        auto isolatedActor = getIsolatedActor(base);
        if (isolatedActor)
          return false;

        // If we have a distributed actor that might be remote, check that
        // we are referencing a properly-distributed member.
        bool performDistributedChecks =
            isolation.getActorType()->isDistributedActor() &&
            !isolatedActor.isPotentiallyIsolated &&
            !isa<ConstructorDecl>(member) &&
            !isActorInitOrDeInitContext(getDeclContext());
        if (performDistributedChecks) {
          if (auto access = checkDistributedAccess(memberLoc, member, context)){
            // This is a distributed access, so mark it as throwing or
            // using a distributed thunk as appropriate.
            markNearestCallAsImplicitly(None, access->first, access->second);
          } else {
            return true;
          }
        }

        return diagnoseNonSendableTypesInReference(
            memberRef, getDeclContext(), memberLoc,
            SendableCheckReason::CrossActor);
      }

      case ActorIsolationRestriction::ActorSelf: {
        // Check whether the base is a reference to an isolated actor instance.
        // If so, there's nothing more to check.
        auto isolatedActor = getIsolatedActor(base);
        if (isolatedActor)
          return false;

        // An instance member of an actor can be referenced from an actor's
        // designated initializer or deinitializer.
        if (isolatedActor.isActorSelf() && member->isInstanceMember())
          if (auto fn = isActorInitOrDeInitContext(getDeclContext()))
            if (!isConvenienceInit(fn))
              return false;

        // An escaping partial application of something that is part of
        // the actor's isolated state is never permitted.
        if (partialApply && partialApply->isEscaping) {
          ctx.Diags.diagnose(
              memberLoc, diag::actor_isolated_partial_apply,
              member->getDescriptiveKind(),
              member->getName());
          return true;
        }

        // Try implicit asynchronous access.
        bool isDistributed = isolation.getActorType()->isDistributedActor() &&
            !isolatedActor.isPotentiallyIsolated;
        auto implicitAsyncResult = tryMarkImplicitlyAsync(
            memberLoc, memberRef, context,
            ImplicitActorHopTarget::forInstanceSelf(),
            isDistributed);

        switch (implicitAsyncResult) {
        case AsyncMarkingResult::FoundAsync:
          return false;

        case AsyncMarkingResult::NotSendable:
        case AsyncMarkingResult::NotDistributed:
          return true;

        case AsyncMarkingResult::NotFound:
        case AsyncMarkingResult::SyncContext:
          // Diagnose below.
          break;
        }

        // Complain about access outside of the isolation domain.
        auto useKind = static_cast<unsigned>(
            kindOfUsage(member, context).getValueOr(VarRefUseEnv::Read));

        ctx.Diags.diagnose(
            memberLoc, diag::actor_isolated_non_self_reference,
            member->getDescriptiveKind(),
            member->getName(),
            useKind,
            isolatedActor.kind - 1,
            isolatedActor.globalActor);

        noteIsolatedActorMember(member, context);
        // FIXME: If isolatedActor has a variable in it, refer to that with
        // more detail?
        return true;
      }

      case ActorIsolationRestriction::GlobalActorUnsafe:
        // Only complain if we're in code that's adopted concurrency features.
        if (!shouldDiagnoseExistingDataRaces(getDeclContext()))
          return false;

        LLVM_FALLTHROUGH;

      case ActorIsolationRestriction::GlobalActor: {
        const bool isInitDeInit = isa<ConstructorDecl>(getDeclContext()) ||
                                  isa<DestructorDecl>(getDeclContext());
        // If we are within an initializer or deinitilizer and are referencing a
        // stored property on "self", we are not crossing actors.
        if (isInitDeInit && isa<VarDecl>(member) &&
            cast<VarDecl>(member)->hasStorage() && getReferencedSelf(base))
          return false;
        return checkGlobalActorReference(
            memberRef, memberLoc, isolation.getGlobalActor(),
            isolation.isCrossActor, context);
      }
      case ActorIsolationRestriction::Unsafe:
        // This case is hit when passing actor state inout to functions in some
        // cases. The error is emitted by diagnoseInOutArg.
        auto nominal = member->getDeclContext()->getSelfNominalTypeDecl();
        if (nominal && nominal->isDistributedActor()) {
          auto funcDecl = dyn_cast<AbstractFunctionDecl>(member);
          if (funcDecl && !funcDecl->isStatic()) {
            member->diagnose(diag::distributed_actor_isolated_method);
            return true;
          }
        }

        return false;
      }
      llvm_unreachable("unhandled actor isolation kind!");
    }

    // Attempt to resolve the global actor type of a closure.
    Type resolveGlobalActorType(ClosureExpr *closure) {
      // Check whether the closure's type has a global actor already.
      if (Type closureType = closure->getType()) {
        if (auto closureFnType = closureType->getAs<FunctionType>()) {
          if (Type globalActor = closureFnType->getGlobalActor())
            return globalActor;
        }
      }

      // Look for an explicit attribute.
      return getExplicitGlobalActor(closure);
    }

  public:
    /// Determine the isolation of a particular closure.
    ///
    /// This function assumes that enclosing closures have already had their
    /// isolation checked.
    ClosureActorIsolation determineClosureIsolation(
        AbstractClosureExpr *closure) {
      // If the closure specifies a global actor, use it.
      if (auto explicitClosure = dyn_cast<ClosureExpr>(closure)) {
        if (Type globalActorType = resolveGlobalActorType(explicitClosure))
          return ClosureActorIsolation::forGlobalActor(globalActorType);
      }

      // If a closure has an isolated parameter, it is isolated to that
      // parameter.
      for (auto param : *closure->getParameters()) {
        if (param->isIsolated())
          return ClosureActorIsolation::forActorInstance(param);
      }

      // Sendable closures are actor-independent unless the closure has
      // specifically opted into inheriting actor isolation.
      if (isSendableClosure(closure, /*forActorIsolation=*/true))
        return ClosureActorIsolation::forIndependent();

      // A non-Sendable closure gets its isolation from its context.
      auto parentIsolation = getActorIsolationOfContext(closure->getParent());

      // We must have parent isolation determined to get here.
      switch (parentIsolation) {
      case ActorIsolation::Independent:
      case ActorIsolation::Unspecified:
        return ClosureActorIsolation::forIndependent();

      case ActorIsolation::GlobalActor:
      case ActorIsolation::GlobalActorUnsafe: {
        Type globalActorType = closure->mapTypeIntoContext(
            parentIsolation.getGlobalActor()->mapTypeOutOfContext());
        return ClosureActorIsolation::forGlobalActor(globalActorType);
      }

      case ActorIsolation::ActorInstance:
      case ActorIsolation::DistributedActorInstance: {
        if (auto param = closure->getCaptureInfo().getIsolatedParamCapture())
          return ClosureActorIsolation::forActorInstance(param);

        return ClosureActorIsolation::forIndependent();
      }
    }
    }

    /// Determine whether the given reference is to a method on
    /// a remote distributed actor in the given context.
    bool isDistributedThunk(ConcreteDeclRef ref, Expr *context,
                            bool isInAsyncLetInitializer);
  };
}

bool ActorIsolationChecker::mayExecuteConcurrentlyWith(
    const DeclContext *useContext, const DeclContext *defContext) {
  // Walk the context chain from the use to the definition.
  while (useContext != defContext) {
    // If we find a concurrent closure... it can be run concurrently.
    if (auto closure = dyn_cast<AbstractClosureExpr>(useContext)) {
      if (isSendableClosure(closure, /*forActorIsolation=*/false))
        return true;
    }

    if (auto func = dyn_cast<FuncDecl>(useContext)) {
      if (func->isLocalCapture()) {
        // If the function is @Sendable... it can be run concurrently.
        if (func->isSendable())
          return true;
      }
    }

    // If we hit a module-scope or type context context, it's not
    // concurrent.
    useContext = useContext->getParent();
    if (useContext->isModuleScopeContext() || useContext->isTypeContext())
      return false;
  }

  // We hit the same context, so it won't execute concurrently.
  return false;
}

void swift::checkTopLevelActorIsolation(TopLevelCodeDecl *decl) {
  ActorIsolationChecker checker(decl);
  decl->getBody()->walk(checker);
}

void swift::checkFunctionActorIsolation(AbstractFunctionDecl *decl) {
  // Disable this check for @LLDBDebuggerFunction functions.
  if (decl->getAttrs().hasAttribute<LLDBDebuggerFunctionAttr>())
    return;

  ActorIsolationChecker checker(decl);
  if (auto body = decl->getBody()) {
    body->walk(checker);
  }
  if (auto ctor = dyn_cast<ConstructorDecl>(decl)) {
    if (auto superInit = ctor->getSuperInitCall())
      superInit->walk(checker);
  }
  if (auto attr = decl->getAttrs().getAttribute<DistributedActorAttr>()) {
    if (auto func = dyn_cast<FuncDecl>(decl)) {
      checkDistributedFunction(func, /*diagnose=*/true);
    }
  }
}

void swift::checkInitializerActorIsolation(Initializer *init, Expr *expr) {
  ActorIsolationChecker checker(init);
  expr->walk(checker);
}

void swift::checkEnumElementActorIsolation(
    EnumElementDecl *element, Expr *expr) {
  ActorIsolationChecker checker(element);
  expr->walk(checker);
}

void swift::checkPropertyWrapperActorIsolation(
    VarDecl *wrappedVar, Expr *expr) {
  ActorIsolationChecker checker(wrappedVar->getDeclContext());
  expr->walk(checker);
}

ClosureActorIsolation
swift::determineClosureActorIsolation(AbstractClosureExpr *closure) {
  ActorIsolationChecker checker(closure->getParent());
  return checker.determineClosureIsolation(closure);
}

/// Determine actor isolation solely from attributes.
///
/// \returns the actor isolation determined from attributes alone (with no
/// inference rules). Returns \c None if there were no attributes on this
/// declaration.
static Optional<ActorIsolation> getIsolationFromAttributes(
    const Decl *decl, bool shouldDiagnose = true, bool onlyExplicit = false) {
  // Look up attributes on the declaration that can affect its actor isolation.
  // If any of them are present, use that attribute.
  auto nonisolatedAttr = decl->getAttrs().getAttribute<NonisolatedAttr>();
  auto globalActorAttr = decl->getGlobalActorAttr();

  // Remove implicit attributes if we only care about explicit ones.
  if (onlyExplicit) {
    if (nonisolatedAttr && nonisolatedAttr->isImplicit())
      nonisolatedAttr = nullptr;
    if (globalActorAttr && globalActorAttr->first->isImplicit())
      globalActorAttr = None;
  }

  unsigned numIsolationAttrs =
    (nonisolatedAttr ? 1 : 0) + (globalActorAttr ? 1 : 0);
  if (numIsolationAttrs == 0)
    return None;

  // Only one such attribute is valid, but we only actually care of one of
  // them is a global actor.
  if (numIsolationAttrs > 1) {
    DeclName name;
    if (auto value = dyn_cast<ValueDecl>(decl)) {
      name = value->getName();
    } else if (auto ext = dyn_cast<ExtensionDecl>(decl)) {
      if (auto selfTypeDecl = ext->getSelfNominalTypeDecl())
        name = selfTypeDecl->getName();
    }

    if (globalActorAttr) {
      if (shouldDiagnose) {
        decl->diagnose(
            diag::actor_isolation_multiple_attr, decl->getDescriptiveKind(),
            name, nonisolatedAttr->getAttrName(),
            globalActorAttr->second->getName().str())
          .highlight(nonisolatedAttr->getRangeWithAt())
          .highlight(globalActorAttr->first->getRangeWithAt());
      }
    }
  }

  // If the declaration is explicitly marked 'nonisolated', report it as
  // independent.
  if (nonisolatedAttr) {
    return ActorIsolation::forIndependent();
  }

  // If the declaration is marked with a global actor, report it as being
  // part of that global actor.
  if (globalActorAttr) {
    ASTContext &ctx = decl->getASTContext();
    auto dc = decl->getInnermostDeclContext();
    Type globalActorType = evaluateOrDefault(
        ctx.evaluator,
        CustomAttrTypeRequest{
          globalActorAttr->first, dc, CustomAttrTypeKind::GlobalActor},
        Type());
    if (!globalActorType || globalActorType->hasError())
      return ActorIsolation::forUnspecified();

    // Handle @<global attribute type>(unsafe).
    bool isUnsafe = globalActorAttr->first->isArgUnsafe();
    if (globalActorAttr->first->hasArgs() && !isUnsafe) {
      ctx.Diags.diagnose(
          globalActorAttr->first->getLocation(),
          diag::global_actor_non_unsafe_init, globalActorType);
    }

    // If the declaration predates concurrency, it has unsafe actor isolation.
    if (decl->preconcurrency())
      isUnsafe = true;

    return ActorIsolation::forGlobalActor(
        globalActorType->mapTypeOutOfContext(), isUnsafe);
  }

  llvm_unreachable("Forgot about an attribute?");
}

/// Infer isolation from witnessed protocol requirements.
static Optional<ActorIsolation> getIsolationFromWitnessedRequirements(
    ValueDecl *value) {
  auto dc = value->getDeclContext();
  auto idc = dyn_cast_or_null<IterableDeclContext>(dc->getAsDecl());
  if (!idc)
    return None;

  if (dc->getSelfProtocolDecl())
    return None;

  // Walk through each of the conformances in this context, collecting any
  // requirements that have actor isolation.
  auto conformances = idc->getLocalConformances(
      ConformanceLookupKind::NonStructural);
  using IsolatedRequirement =
      std::tuple<ProtocolConformance *, ActorIsolation, ValueDecl *>;
  SmallVector<IsolatedRequirement, 2> isolatedRequirements;
  for (auto conformance : conformances) {
    auto protocol = conformance->getProtocol();
    for (auto found : protocol->lookupDirect(value->getName())) {
      if (!isa<ProtocolDecl>(found->getDeclContext()))
        continue;

      auto requirement = dyn_cast<ValueDecl>(found);
      if (!requirement || isa<TypeDecl>(requirement))
        continue;

      auto requirementIsolation = getActorIsolation(requirement);
      switch (requirementIsolation) {
      case ActorIsolation::ActorInstance:
      case ActorIsolation::DistributedActorInstance:
      case ActorIsolation::Unspecified:
        continue;

      case ActorIsolation::GlobalActor:
      case ActorIsolation::GlobalActorUnsafe:
      case ActorIsolation::Independent:
        break;
      }

      auto witness = conformance->getWitnessDecl(requirement);
      if (witness != value)
        continue;

      isolatedRequirements.push_back(
          IsolatedRequirement{conformance, requirementIsolation, requirement});
    }
  }

  // Filter out duplicate actors.
  SmallPtrSet<CanType, 2> globalActorTypes;
  bool sawActorIndependent = false;
  isolatedRequirements.erase(
      std::remove_if(isolatedRequirements.begin(), isolatedRequirements.end(),
                     [&](IsolatedRequirement &isolated) {
    auto isolation = std::get<1>(isolated);
    switch (isolation) {
      case ActorIsolation::ActorInstance:
      case ActorIsolation::DistributedActorInstance:
        llvm_unreachable("protocol requirements cannot be actor instances");

      case ActorIsolation::Independent:
        // We only need one nonisolated.
        if (sawActorIndependent)
          return true;

        sawActorIndependent = true;
        return false;

      case ActorIsolation::Unspecified:
        return true;

      case ActorIsolation::GlobalActor:
      case ActorIsolation::GlobalActorUnsafe: {
        // Substitute into the global actor type.
        auto conformance = std::get<0>(isolated);
        auto requirementSubs = SubstitutionMap::getProtocolSubstitutions(
            conformance->getProtocol(), dc->getSelfTypeInContext(),
            ProtocolConformanceRef(conformance));
        Type globalActor = isolation.getGlobalActor().subst(requirementSubs);
        if (!globalActorTypes.insert(globalActor->getCanonicalType()).second)
          return true;

        // Update the global actor type, now that we've done this substitution.
        std::get<1>(isolated) = ActorIsolation::forGlobalActor(
            globalActor, isolation == ActorIsolation::GlobalActorUnsafe);
        return false;
      }
      }
      }),
      isolatedRequirements.end());

  if (isolatedRequirements.size() != 1)
    return None;

  return std::get<1>(isolatedRequirements.front());
}

/// Compute the isolation of a nominal type from the conformances that
/// are directly specified on the type.
static Optional<ActorIsolation> getIsolationFromConformances(
    NominalTypeDecl *nominal) {
  if (isa<ProtocolDecl>(nominal))
    return None;

  Optional<ActorIsolation> foundIsolation;
  for (auto proto :
       nominal->getLocalProtocols(ConformanceLookupKind::NonStructural)) {
    switch (auto protoIsolation = getActorIsolation(proto)) {
    case ActorIsolation::ActorInstance:
    case ActorIsolation::DistributedActorInstance:
    case ActorIsolation::Unspecified:
    case ActorIsolation::Independent:
      break;

    case ActorIsolation::GlobalActor:
    case ActorIsolation::GlobalActorUnsafe:
      if (!foundIsolation) {
        foundIsolation = protoIsolation;
        continue;
      }

      if (*foundIsolation != protoIsolation)
        return None;

      break;
    }
  }

  return foundIsolation;
}

/// Compute the isolation of a nominal type from the property wrappers on
/// any stored properties.
static Optional<ActorIsolation> getIsolationFromWrappers(
    NominalTypeDecl *nominal) {
  if (!isa<StructDecl>(nominal) && !isa<ClassDecl>(nominal))
    return None;

  if (!nominal->getParentSourceFile())
    return None;
  
  Optional<ActorIsolation> foundIsolation;
  for (auto member : nominal->getMembers()) {
    auto var = dyn_cast<VarDecl>(member);
    if (!var || !var->isInstanceMember())
      continue;

    auto info = var->getAttachedPropertyWrapperTypeInfo(0);
    if (!info)
      continue;

    auto isolation = getActorIsolation(info.valueVar);

    // Inconsistent wrappedValue/projectedValue isolation disables inference.
    if (info.projectedValueVar &&
        getActorIsolation(info.projectedValueVar) != isolation)
      continue;

    switch (isolation) {
    case ActorIsolation::ActorInstance:
    case ActorIsolation::DistributedActorInstance:
    case ActorIsolation::Unspecified:
    case ActorIsolation::Independent:
      break;

    case ActorIsolation::GlobalActor:
    case ActorIsolation::GlobalActorUnsafe:
      if (!foundIsolation) {
        foundIsolation = isolation;
        continue;
      }

      if (*foundIsolation != isolation)
        return None;

      break;
    }
  }

  return foundIsolation;
}

namespace {

/// Describes how actor isolation is propagated to a member, if at all.
enum class MemberIsolationPropagation {
  GlobalActor,
  AnyIsolation
};

}
/// Determine how the given member can receive its isolation from its type
/// context.
static Optional<MemberIsolationPropagation> getMemberIsolationPropagation(
    const ValueDecl *value) {
  if (!value->getDeclContext()->isTypeContext())
    return None;

  switch (value->getKind()) {
  case DeclKind::Import:
  case DeclKind::Extension:
  case DeclKind::TopLevelCode:
  case DeclKind::InfixOperator:
  case DeclKind::PrefixOperator:
  case DeclKind::PostfixOperator:
  case DeclKind::IfConfig:
  case DeclKind::PoundDiagnostic:
  case DeclKind::PrecedenceGroup:
  case DeclKind::MissingMember:
  case DeclKind::Class:
  case DeclKind::Enum:
  case DeclKind::Protocol:
  case DeclKind::Struct:
  case DeclKind::TypeAlias:
  case DeclKind::GenericTypeParam:
  case DeclKind::AssociatedType:
  case DeclKind::OpaqueType:
  case DeclKind::Param:
  case DeclKind::Module:
  case DeclKind::Destructor:
    return None;

  case DeclKind::PatternBinding:
  case DeclKind::EnumCase:
  case DeclKind::EnumElement:
    return MemberIsolationPropagation::GlobalActor;

  case DeclKind::Constructor:
    return MemberIsolationPropagation::AnyIsolation;

  case DeclKind::Func:
  case DeclKind::Accessor:
  case DeclKind::Subscript:
  case DeclKind::Var:
    return value->isInstanceMember() ? MemberIsolationPropagation::AnyIsolation
                                     : MemberIsolationPropagation::GlobalActor;
  }
}

/// Given a property, determine the isolation when it part of a wrapped
/// property.
static ActorIsolation getActorIsolationFromWrappedProperty(VarDecl *var) {
  // If this is a variable with a property wrapper, infer from the property
  // wrapper's wrappedValue.
  if (auto wrapperInfo = var->getAttachedPropertyWrapperTypeInfo(0)) {
    if (auto wrappedValue = wrapperInfo.valueVar) {
      if (auto isolation = getActorIsolation(wrappedValue))
        return isolation;
    }
  }

  // If this is the backing storage for a property wrapper, infer from the
  // type of the outermost property wrapper.
  if (auto originalVar = var->getOriginalWrappedProperty(
          PropertyWrapperSynthesizedPropertyKind::Backing)) {
    if (auto backingType =
            originalVar->getPropertyWrapperBackingPropertyType()) {
      if (auto backingNominal = backingType->getAnyNominal()) {
        if (!isa<ClassDecl>(backingNominal) ||
            !cast<ClassDecl>(backingNominal)->isActor()) {
          if (auto isolation = getActorIsolation(backingNominal))
            return isolation;
        }
      }
    }
  }

  // If this is the projected property for a property wrapper, infer from
  // the property wrapper's projectedValue.
  if (auto originalVar = var->getOriginalWrappedProperty(
          PropertyWrapperSynthesizedPropertyKind::Projection)) {
    if (auto wrapperInfo =
            originalVar->getAttachedPropertyWrapperTypeInfo(0)) {
      if (auto projectedValue = wrapperInfo.projectedValueVar) {
        if (auto isolation = getActorIsolation(projectedValue))
          return isolation;
      }
    }
  }

  return ActorIsolation::forUnspecified();
}

static Optional<ActorIsolation>
getActorIsolationForMainFuncDecl(FuncDecl *fnDecl) {
  // Ensure that the base type that this function is declared in has @main
  // attribute
  NominalTypeDecl *declContext =
      dyn_cast<NominalTypeDecl>(fnDecl->getDeclContext());
  if (ExtensionDecl *exDecl =
          dyn_cast<ExtensionDecl>(fnDecl->getDeclContext())) {
    declContext = exDecl->getExtendedNominal();
  }

  // We're not even in a nominal decl type, this can't be the main function decl
  if (!declContext)
    return {};
  const bool isMainDeclContext =
      declContext->getAttrs().hasAttribute<MainTypeAttr>();

  ASTContext &ctx = fnDecl->getASTContext();

  const bool isMainMain = fnDecl->isMainTypeMainMethod();
  const bool isMainInternalMain =
      fnDecl->getBaseIdentifier() == ctx.getIdentifier("$main") &&
      !fnDecl->isInstanceMember() &&
      fnDecl->getResultInterfaceType()->isVoid() &&
      fnDecl->getParameters()->size() == 0;
  const bool isMainFunction =
      isMainDeclContext && (isMainMain || isMainInternalMain);
  const bool hasMainActor = !ctx.getMainActorType().isNull();

  return isMainFunction && hasMainActor
             ? ActorIsolation::forGlobalActor(
                   ctx.getMainActorType()->mapTypeOutOfContext(),
                   /*isUnsafe*/ false)
             : Optional<ActorIsolation>();
}

/// Check rules related to global actor attributes on a class declaration.
///
/// \returns true if an error occurred.
static bool checkClassGlobalActorIsolation(
    ClassDecl *classDecl, ActorIsolation isolation) {
  assert(isolation.isGlobalActor());

  // A class can only be annotated with a global actor if it has no
  // superclass, the superclass is annotated with the same global actor, or
  // the superclass is NSObject. A subclass of a global-actor-annotated class
  // must be isolated to the same global actor.
  auto superclassDecl = classDecl->getSuperclassDecl();
  if (!superclassDecl)
    return false;

  if (superclassDecl->isNSObject())
    return false;

  // Ignore actors outright. They'll be diagnosed later.
  if (classDecl->isActor() || superclassDecl->isActor())
    return false;

  // Check the superclass's isolation.
  auto superIsolation = getActorIsolation(superclassDecl);
  switch (superIsolation) {
  case ActorIsolation::Unspecified:
  case ActorIsolation::Independent:
    return false;

  case ActorIsolation::ActorInstance:
  case ActorIsolation::DistributedActorInstance:
    // This is an error that will be diagnosed later. Ignore it here.
    return false;

  case ActorIsolation::GlobalActor:
  case ActorIsolation::GlobalActorUnsafe: {
    // If the the global actors match, we're fine.
    Type superclassGlobalActor = superIsolation.getGlobalActor();
    auto module = classDecl->getParentModule();
    SubstitutionMap subsMap = classDecl->getDeclaredInterfaceType()
      ->getSuperclassForDecl(superclassDecl)
      ->getContextSubstitutionMap(module, superclassDecl);
    Type superclassGlobalActorInSub = superclassGlobalActor.subst(subsMap);
    if (isolation.getGlobalActor()->isEqual(superclassGlobalActorInSub))
      return false;

    break;
  }
  }

  // Complain about the mismatch.
  classDecl->diagnose(
      diag::actor_isolation_superclass_mismatch, isolation,
      classDecl->getName(), superIsolation, superclassDecl->getName());
  return true;
}

ActorIsolation ActorIsolationRequest::evaluate(
    Evaluator &evaluator, ValueDecl *value) const {
  // If this declaration has actor-isolated "self", it's isolated to that
  // actor.
  if (evaluateOrDefault(evaluator, HasIsolatedSelfRequest{value}, false)) {
    auto actor = value->getDeclContext()->getSelfNominalTypeDecl();
    assert(actor && "could not find the actor that 'self' is isolated to");
    return actor->isDistributedActor()
        ? ActorIsolation::forDistributedActorInstance(actor)
        : ActorIsolation::forActorInstance(actor);
  }

  auto isolationFromAttr = getIsolationFromAttributes(value);
  if (FuncDecl *fd = dyn_cast<FuncDecl>(value)) {
    // Main.main() and Main.$main are implicitly MainActor-protected.
    // Any other isolation is an error.
    Optional<ActorIsolation> mainIsolation =
        getActorIsolationForMainFuncDecl(fd);
    if (mainIsolation) {
      if (isolationFromAttr && isolationFromAttr->isGlobalActor()) {
        if (!areTypesEqual(isolationFromAttr->getGlobalActor(),
                           mainIsolation->getGlobalActor())) {
          fd->getASTContext().Diags.diagnose(
              fd->getLoc(), diag::main_function_must_be_mainActor);
        }
      }
      return *mainIsolation;
    }
  }
  // If this declaration has one of the actor isolation attributes, report
  // that.
  if (isolationFromAttr) {
    // Classes with global actors have additional rules regarding inheritance.
    if (isolationFromAttr->isGlobalActor()) {
      if (auto classDecl = dyn_cast<ClassDecl>(value))
        checkClassGlobalActorIsolation(classDecl, *isolationFromAttr);
    }

    return *isolationFromAttr;
  }

  // Determine the default isolation for this declaration, which may still be
  // overridden by other inference rules.
  ActorIsolation defaultIsolation = ActorIsolation::forUnspecified();

  if (auto func = dyn_cast<AbstractFunctionDecl>(value)) {
    // A @Sendable function is assumed to be actor-independent.
    if (func->isSendable()) {
      defaultIsolation = ActorIsolation::forIndependent();
    }
  }

  // Every actor's convenience or synchronous init is
  // assumed to be actor-independent.
  if (auto nominal = value->getDeclContext()->getSelfNominalTypeDecl())
    if (nominal->isAnyActor())
      if (auto ctor = dyn_cast<ConstructorDecl>(value))
        if (ctor->isConvenienceInit() || !ctor->hasAsync())
          defaultIsolation = ActorIsolation::forIndependent();

  // Function used when returning an inferred isolation.
  auto inferredIsolation = [&](
      ActorIsolation inferred, bool onlyGlobal = false) {
    // Add an implicit attribute to capture the actor isolation that was
    // inferred, so that (e.g.) it will be printed and serialized.
    ASTContext &ctx = value->getASTContext();
    switch (inferred) {
    case ActorIsolation::Independent:
      // Stored properties cannot be non-isolated, so don't infer it.
      if (auto var = dyn_cast<VarDecl>(value)) {
        if (!var->isStatic() && var->hasStorage())
          return ActorIsolation::forUnspecified();
      }


      if (onlyGlobal)
        return ActorIsolation::forUnspecified();

      value->getAttrs().add(new (ctx) NonisolatedAttr(/*IsImplicit=*/true));
      break;

    case ActorIsolation::GlobalActorUnsafe:
    case ActorIsolation::GlobalActor: {
      auto typeExpr = TypeExpr::createImplicit(inferred.getGlobalActor(), ctx);
      auto attr = CustomAttr::create(
          ctx, SourceLoc(), typeExpr, /*implicit=*/true);
      if (inferred == ActorIsolation::GlobalActorUnsafe)
        attr->setArgIsUnsafe(true);
      value->getAttrs().add(attr);
      break;
    }

    case ActorIsolation::DistributedActorInstance:
    case ActorIsolation::ActorInstance:
    case ActorIsolation::Unspecified:
      if (onlyGlobal)
        return ActorIsolation::forUnspecified();

      // Nothing to do.
      break;
    }

    return inferred;
  };

  // If this is a local function, inherit the actor isolation from its
  // context if it global or was captured.
  if (auto func = dyn_cast<FuncDecl>(value)) {
    // If this is a defer body, inherit unconditionally; we don't
    // care if the enclosing function captures the isolated parameter.
    if (func->isDeferBody()) {
      auto enclosingIsolation =
                        getActorIsolationOfContext(func->getDeclContext());
      return inferredIsolation(enclosingIsolation);
    }

    if (func->isLocalCapture() && !func->isSendable()) {
      switch (auto enclosingIsolation =
                  getActorIsolationOfContext(func->getDeclContext())) {
      case ActorIsolation::Independent:
      case ActorIsolation::Unspecified:
        // Do nothing.
        break;

      case ActorIsolation::ActorInstance:
      case ActorIsolation::DistributedActorInstance:
        if (auto param = func->getCaptureInfo().getIsolatedParamCapture())
          return inferredIsolation(enclosingIsolation);
        break;

      case ActorIsolation::GlobalActor:
      case ActorIsolation::GlobalActorUnsafe:
        return inferredIsolation(enclosingIsolation);
      }
    }
  }

  // If the declaration overrides another declaration, it must have the same
  // actor isolation.
  if (auto overriddenValue = value->getOverriddenDecl()) {
    auto isolation = getActorIsolation(overriddenValue);
    SubstitutionMap subs;

    if (Type selfType = value->getDeclContext()->getSelfInterfaceType()) {
      subs = selfType->getMemberSubstitutionMap(
          value->getModuleContext(), overriddenValue);
    }

    return inferredIsolation(isolation.subst(subs));
  }

  // If this is an accessor, use the actor isolation of its storage
  // declaration.
  if (auto accessor = dyn_cast<AccessorDecl>(value)) {
    return getActorIsolation(accessor->getStorage());
  }

  if (auto var = dyn_cast<VarDecl>(value)) {
    if (auto isolation = getActorIsolationFromWrappedProperty(var))
      return inferredIsolation(isolation);
  }

  // If this is a dynamic replacement for another function, use the
  // actor isolation of the function it replaces.
  if (auto replacedDecl = value->getDynamicallyReplacedDecl()) {
    if (auto isolation = getActorIsolation(replacedDecl))
      return inferredIsolation(isolation);
  }

  if (shouldInferAttributeInContext(value->getDeclContext())) {
    // If the declaration witnesses a protocol requirement that is isolated,
    // use that.
    if (auto witnessedIsolation = getIsolationFromWitnessedRequirements(value)) {
      if (auto inferred = inferredIsolation(*witnessedIsolation))
        return inferred;
    }

    // If the declaration is a class with a superclass that has specified
    // isolation, use that.
    if (auto classDecl = dyn_cast<ClassDecl>(value)) {
      if (auto superclassDecl = classDecl->getSuperclassDecl()) {
        auto superclassIsolation = getActorIsolation(superclassDecl);
        if (!superclassIsolation.isUnspecified()) {
          if (superclassIsolation.requiresSubstitution()) {
            Type superclassType = classDecl->getSuperclass();
            if (!superclassType)
              return ActorIsolation::forUnspecified();

            SubstitutionMap subs = superclassType->getMemberSubstitutionMap(
                classDecl->getModuleContext(), classDecl);
            superclassIsolation = superclassIsolation.subst(subs);
          }

          if (auto inferred = inferredIsolation(superclassIsolation))
            return inferred;
        }
      }
    }

    if (auto nominal = dyn_cast<NominalTypeDecl>(value)) {
      // If the declaration is a nominal type and any of the protocols to which
      // it directly conforms is isolated to a global actor, use that.
      if (auto conformanceIsolation = getIsolationFromConformances(nominal))
        if (auto inferred = inferredIsolation(*conformanceIsolation))
          return inferred;

      // If the declaration is a nominal type and any property wrappers on
      // its stored properties require isolation, use that.
      if (auto wrapperIsolation = getIsolationFromWrappers(nominal)) {
        if (auto inferred = inferredIsolation(*wrapperIsolation))
          return inferred;
      }
    }
  }

  // Infer isolation for a member.
  if (auto memberPropagation = getMemberIsolationPropagation(value)) {
    // If were only allowed to propagate global actors, do so.
    bool onlyGlobal =
        *memberPropagation == MemberIsolationPropagation::GlobalActor;

    // If the declaration is in an extension that has one of the isolation
    // attributes, use that.
    if (auto ext = dyn_cast<ExtensionDecl>(value->getDeclContext())) {
      if (auto isolationFromAttr = getIsolationFromAttributes(ext)) {
        return inferredIsolation(*isolationFromAttr, onlyGlobal);
      }
    }

    // If the declaration is in a nominal type (or extension thereof) that
    // has isolation, use that.
    if (auto selfTypeDecl = value->getDeclContext()->getSelfNominalTypeDecl()) {
      if (auto selfTypeIsolation = getActorIsolation(selfTypeDecl))
        return inferredIsolation(selfTypeIsolation, onlyGlobal);
    }
  }

  // @IBAction implies @MainActor(unsafe).
  if (value->getAttrs().hasAttribute<IBActionAttr>()) {
    ASTContext &ctx = value->getASTContext();
    if (Type mainActor = ctx.getMainActorType()) {
      return inferredIsolation(
          ActorIsolation::forGlobalActor(mainActor, /*unsafe=*/true));
    }
  }

  // Default isolation for this member.
  return defaultIsolation;
}

bool HasIsolatedSelfRequest::evaluate(
    Evaluator &evaluator, ValueDecl *value) const {
  // Only ever applies to members of actors.
  auto dc = value->getDeclContext();
  auto selfTypeDecl = dc->getSelfNominalTypeDecl();
  if (!selfTypeDecl || !selfTypeDecl->isAnyActor())
    return false;

  // For accessors, consider the storage declaration.
  if (auto accessor = dyn_cast<AccessorDecl>(value))
    value = accessor->getStorage();

  // Check whether this member can be isolated to an actor at all.
  auto memberIsolation = getMemberIsolationPropagation(value);
  if (!memberIsolation)
    return false;

  switch (*memberIsolation) {
  case MemberIsolationPropagation::GlobalActor:
    return false;

  case MemberIsolationPropagation::AnyIsolation:
    break;
  }

  // Check whether the default isolation was overridden by any attributes on
  // this declaration.
  if (getIsolationFromAttributes(value))
    return false;

  // ... or its extension context.
  if (auto ext = dyn_cast<ExtensionDecl>(dc)) {
    if (getIsolationFromAttributes(ext))
      return false;
  }

  // If this is a variable, check for a property wrapper that alters its
  // isolation.
  if (auto var = dyn_cast<VarDecl>(value)) {
    switch (auto isolation = getActorIsolationFromWrappedProperty(var)) {
    case ActorIsolation::Independent:
    case ActorIsolation::Unspecified:
      break;

    case ActorIsolation::GlobalActor:
    case ActorIsolation::GlobalActorUnsafe:
      return false;

    case ActorIsolation::ActorInstance:
    case ActorIsolation::DistributedActorInstance:
      if (isolation.getActor() != selfTypeDecl)
        return false;
      break;
    }
  }

  if (auto ctor = dyn_cast<ConstructorDecl>(value)) {
    // In an actor's convenience or synchronous init, self is not isolated.
    if (ctor->isConvenienceInit() || !ctor->hasAsync())
      return false;
  }

  return true;
}

void swift::checkOverrideActorIsolation(ValueDecl *value) {
  if (isa<TypeDecl>(value))
    return;

  auto overridden = value->getOverriddenDecl();
  if (!overridden)
    return;

  // Determine the actor isolation of this declaration.
  auto isolation = getActorIsolation(value);

  // Determine the actor isolation of the overridden function.=
  auto overriddenIsolation = getActorIsolation(overridden);

  if (overriddenIsolation.requiresSubstitution()) {
    SubstitutionMap subs;
    if (Type selfType = value->getDeclContext()->getSelfInterfaceType()) {
      subs = selfType->getMemberSubstitutionMap(
          value->getModuleContext(), overridden);
    }

    overriddenIsolation = overriddenIsolation.subst(subs);
  }

  // If the isolation matches, we're done.
  if (isolation == overriddenIsolation)
    return;

  // If the overriding declaration is non-isolated, it's okay.
  if (isolation.isIndependent() || isolation.isUnspecified())
    return;

  // If both are actor-instance isolated, we're done. This wasn't caught by
  // the equality case above because the nominal type describing the actor
  // will differ when we're overriding.
  if (isolation.getKind() == overriddenIsolation.getKind() &&
      (isolation.getKind() == ActorIsolation::ActorInstance ||
       isolation.getKind() == ActorIsolation::DistributedActorInstance))
    return;

  // If the overridden declaration is from Objective-C with no actor annotation,
  // allow it.
  if (overridden->hasClangNode() && !overriddenIsolation)
    return;

  // Isolation mismatch. Diagnose it.
  value->diagnose(
      diag::actor_isolation_override_mismatch, isolation,
      value->getDescriptiveKind(), value->getName(), overriddenIsolation);
  overridden->diagnose(diag::overridden_here);
}

bool swift::contextRequiresStrictConcurrencyChecking(
    const DeclContext *dc,
    llvm::function_ref<Type(const AbstractClosureExpr *)> getType) {
  // If Swift >= 6, everything uses strict concurrency checking.
  if (dc->getASTContext().LangOpts.isSwiftVersionAtLeast(6))
    return true;

  while (!dc->isModuleScopeContext()) {
    if (auto closure = dyn_cast<AbstractClosureExpr>(dc)) {
      // A closure with an explicit global actor or nonindependent
      // uses concurrency features.
      if (auto explicitClosure = dyn_cast<ClosureExpr>(closure)) {
        if (getExplicitGlobalActor(const_cast<ClosureExpr *>(explicitClosure)))
          return true;

        if (auto type = getType(closure)) {
          if (auto fnType = type->getAs<AnyFunctionType>())
            if (fnType->isAsync() || fnType->isSendable())
              return true;
        }
      }

      // Async and @Sendable closures use concurrency features.
      if (closure->isBodyAsync() || closure->isSendable())
        return true;
    } else if (auto decl = dc->getAsDecl()) {
      // If any isolation attributes are present, we're using concurrency
      // features.
      if (getIsolationFromAttributes(
              decl, /*shouldDiagnose=*/false, /*onlyExplicit=*/true))
        return true;

      if (auto func = dyn_cast<AbstractFunctionDecl>(decl)) {
        // Async and concurrent functions use concurrency features.
        if (func->hasAsync() || func->isSendable())
          return true;

        // If we're in an accessor declaration, also check the storage
        // declaration.
        if (auto accessor = dyn_cast<AccessorDecl>(decl)) {
          if (getIsolationFromAttributes(
                  accessor->getStorage(), /*shouldDiagnose=*/false,
                  /*onlyExplicit=*/true))
            return true;
        }
      }
    }

    // If we're in an actor, we're using concurrency features.
    if (auto nominal = dc->getSelfNominalTypeDecl()) {
      if (nominal->isActor())
        return true;
    }

    // Keep looking.
    dc = dc->getParent();
  }

  return false;
}

namespace {
  /// Visit the instance storage of the given nominal type as seen through
  /// the given declaration context.
  ///
  /// \param visitor Called with each (stored property, property type) pair
  /// for classes/structs and with each (enum element, associated value type)
  /// pair for enums.
  ///
  /// \returns \c true if any call to the \c visitor returns \c true, and
  /// \c false otherwise.
  template<typename Visitor>
  bool visitInstanceStorage(
      NominalTypeDecl *nominal, DeclContext *dc, Visitor &visitor) {
    // Walk the stored properties of classes and structs.
    if (isa<StructDecl>(nominal) || isa<ClassDecl>(nominal)) {
      for (auto property : nominal->getStoredProperties()) {
        auto propertyType = dc->mapTypeIntoContext(property->getInterfaceType())
            ->getRValueType()->getReferenceStorageReferent();
        if (visitor(property, propertyType))
          return true;
      }

      return false;
    }

    // Walk the enum elements that have associated values.
    if (auto enumDecl = dyn_cast<EnumDecl>(nominal)) {
      for (auto caseDecl : enumDecl->getAllCases()) {
        for (auto element : caseDecl->getElements()) {
          if (!element->hasAssociatedValues())
            continue;

          // Check that the associated value type is Sendable.
          auto elementType = dc->mapTypeIntoContext(
              element->getArgumentInterfaceType());
          if (visitor(element, elementType))
            return true;
        }
      }

      return false;
    }

    return false;
  }
}

/// Check the instance storage of the given nominal type to verify whether
/// it is comprised only of Sendable instance storage.
static bool checkSendableInstanceStorage(
    NominalTypeDecl *nominal, DeclContext *dc, SendableCheck check) {
  // Stored properties of structs and classes must have
  // Sendable-conforming types.
  struct Visitor {
    bool invalid = false;
    NominalTypeDecl *nominal;
    DeclContext *dc;
    SendableCheck check;
    const LangOptions &langOpts;

    Visitor(NominalTypeDecl *nominal, DeclContext *dc, SendableCheck check)
      : nominal(nominal), dc(dc), check(check),
        langOpts(nominal->getASTContext().LangOpts) { }

    /// Handle a stored property.
    bool operator()(VarDecl *property, Type propertyType) {
      // Classes with mutable properties are not Sendable.
      if (property->supportsMutation() && isa<ClassDecl>(nominal)) {
        if (isImplicitSendableCheck(check)) {
          invalid = true;
          return true;
        }

        auto behavior = SendableCheckContext(
            dc, check).defaultDiagnosticBehavior();
        if (behavior != DiagnosticBehavior::Ignore) {
          property->diagnose(diag::concurrent_value_class_mutable_property,
                             property->getName(), nominal->getDescriptiveKind(),
                             nominal->getName())
              .limitBehavior(behavior);
        }
        invalid = invalid || (behavior == DiagnosticBehavior::Unspecified);
        return true;
      }

      // Check that the property type is Sendable.
      diagnoseNonSendableTypes(
          propertyType, SendableCheckContext(dc, check), property->getLoc(),
          [&](Type type, DiagnosticBehavior behavior) {
            if (isImplicitSendableCheck(check)) {
              // If this is for an externally-visible conformance, fail.
              if (check == SendableCheck::ImplicitForExternallyVisible) {
                invalid = true;
                return true;
              }

              // If we are to ignore this diagnostic, just continue.
              if (behavior == DiagnosticBehavior::Ignore)
                return false;

              invalid = true;
              return true;
            }

            property->diagnose(diag::non_concurrent_type_member,
                               propertyType, false, property->getName(),
                               nominal->getDescriptiveKind(),
                               nominal->getName())
                .limitBehavior(behavior);
            return false;
          });

      if (invalid) {
        // For implicit checks, bail out early if anything failed.
        if (isImplicitSendableCheck(check))
          return true;
      }

      return false;
    }

    /// Handle an enum associated value.
    bool operator()(EnumElementDecl *element, Type elementType) {
      diagnoseNonSendableTypes(
          elementType, SendableCheckContext(dc, check), element->getLoc(),
          [&](Type type, DiagnosticBehavior behavior) {
            if (isImplicitSendableCheck(check)) {
              // If this is for an externally-visible conformance, fail.
              if (check == SendableCheck::ImplicitForExternallyVisible) {
                invalid = true;
                return true;
              }

              // If we are to ignore this diagnostic, just continue.
              if (behavior == DiagnosticBehavior::Ignore)
                return false;

              invalid = true;
              return true;
            }

            element->diagnose(diag::non_concurrent_type_member, type,
                              true, element->getName(),
                              nominal->getDescriptiveKind(),
                              nominal->getName())
                .limitBehavior(behavior);
            return false;
          });

      if (invalid) {
        // For implicit checks, bail out early if anything failed.
        if (isImplicitSendableCheck(check))
          return true;
      }

      return false;
    }
  } visitor(nominal, dc, check);

  return visitInstanceStorage(nominal, dc, visitor) || visitor.invalid;
}

bool swift::checkSendableConformance(
    ProtocolConformance *conformance, SendableCheck check) {
  auto conformanceDC = conformance->getDeclContext();
  auto nominal = conformance->getType()->getAnyNominal();
  if (!nominal)
    return false;

  // If this is an always-unavailable conformance, there's nothing to check.
  if (auto ext = dyn_cast<ExtensionDecl>(conformanceDC)) {
    if (AvailableAttr::isUnavailable(ext))
      return false;
  }

  auto classDecl = dyn_cast<ClassDecl>(nominal);
  if (classDecl) {
    // Actors implicitly conform to Sendable and protect their state.
    if (classDecl->isActor())
      return false;
  }

  // Global-actor-isolated types can be Sendable. We do not check the
  // instance data because it's all isolated to the global actor.
  switch (getActorIsolation(nominal)) {
  case ActorIsolation::Unspecified:
  case ActorIsolation::ActorInstance:
  case ActorIsolation::DistributedActorInstance:
  case ActorIsolation::Independent:
    break;

  case ActorIsolation::GlobalActor:
  case ActorIsolation::GlobalActorUnsafe:
    return false;
  }

  // Sendable can only be used in the same source file.
  auto conformanceDecl = conformanceDC->getAsDecl();
  auto behavior = SendableCheckContext(conformanceDC, check)
      .defaultDiagnosticBehavior();
  if (conformanceDC->getParentSourceFile() &&
      conformanceDC->getParentSourceFile() != nominal->getParentSourceFile()) {
    conformanceDecl->diagnose(diag::concurrent_value_outside_source_file,
                              nominal->getDescriptiveKind(),
                              nominal->getName())
      .limitBehavior(behavior);

    if (behavior == DiagnosticBehavior::Unspecified)
      return true;
  }

  if (classDecl && classDecl->getParentSourceFile()) {
    bool isInherited = isa<InheritedProtocolConformance>(conformance);

    // An non-final class cannot conform to `Sendable`.
    if (!classDecl->isSemanticallyFinal()) {
      classDecl->diagnose(diag::concurrent_value_nonfinal_class,
                          classDecl->getName())
        .limitBehavior(behavior);

      if (behavior == DiagnosticBehavior::Unspecified)
        return true;
    }

    if (!isInherited) {
      // A 'Sendable' class cannot inherit from another class, although
      // we allow `NSObject` for Objective-C interoperability.
      if (auto superclassDecl = classDecl->getSuperclassDecl()) {
        if (!superclassDecl->isNSObject()) {
          classDecl->diagnose(
              diag::concurrent_value_inherit,
              nominal->getASTContext().LangOpts.EnableObjCInterop,
              classDecl->getName());
          return true;
        }
      }
    }
  }

  return checkSendableInstanceStorage(nominal, conformanceDC, check);
}

ProtocolConformance *GetImplicitSendableRequest::evaluate(
    Evaluator &evaluator, NominalTypeDecl *nominal) const {
  // Protocols never get implicit Sendable conformances.
  if (isa<ProtocolDecl>(nominal))
    return nullptr;

  // Actor types are always Sendable; they don't get it via this path.
  auto classDecl = dyn_cast<ClassDecl>(nominal);
  if (classDecl && classDecl->isActor())
    return nullptr;

  // Check whether we can infer conformance at all.
  if (auto *file = dyn_cast<FileUnit>(nominal->getModuleScopeContext())) {
    switch (file->getKind()) {
    case FileUnitKind::Source:
      // Check what kind of source file we have.
      if (auto sourceFile = nominal->getParentSourceFile()) {
        switch (sourceFile->Kind) {
        case SourceFileKind::Interface:
          // Interfaces have explicitly called-out Sendable conformances.
          return nullptr;

        case SourceFileKind::Library:
        case SourceFileKind::Main:
        case SourceFileKind::SIL:
          break;
        }
      }
      break;

    case FileUnitKind::Builtin:
    case FileUnitKind::SerializedAST:
    case FileUnitKind::Synthesized:
      // Explicitly-handled modules don't infer Sendable conformances.
      return nullptr;

    case FileUnitKind::ClangModule:
    case FileUnitKind::DWARFModule:
      // Infer conformances for imported modules.
      break;
    }
  } else {
    return nullptr;
  }

  ASTContext &ctx = nominal->getASTContext();
  auto proto = ctx.getProtocol(KnownProtocolKind::Sendable);
  if (!proto)
    return nullptr;

  // Local function to form the implicit conformance.
  auto formConformance = [&](const DeclAttribute *attrMakingUnavailable)
        -> NormalProtocolConformance * {
    DeclContext *conformanceDC = nominal;
    if (attrMakingUnavailable) {
      llvm::VersionTuple NoVersion;
      auto attr = new (ctx) AvailableAttr(SourceLoc(), SourceRange(),
                                          PlatformKind::none, "", "", nullptr,
                                          NoVersion, SourceRange(),
                                          NoVersion, SourceRange(),
                                          NoVersion, SourceRange(),
                                          PlatformAgnosticAvailabilityKind::Unavailable,
                                          false);

      // Conformance availability is currently tied to the declaring extension.
      // FIXME: This is a hack--we should give conformances real availability.
      auto inherits = ctx.AllocateCopy(makeArrayRef(
          InheritedEntry(TypeLoc::withoutLoc(proto->getDeclaredInterfaceType()),
                         /*isUnchecked*/true)));
      // If you change the use of AtLoc in the ExtensionDecl, make sure you
      // update isNonSendableExtension() in ASTPrinter.
      auto extension = ExtensionDecl::create(ctx, attrMakingUnavailable->AtLoc,
                                             nullptr, inherits,
                                             nominal->getModuleScopeContext(),
                                             nullptr);
      extension->setImplicit();
      extension->getAttrs().add(attr);

      ctx.evaluator.cacheOutput(ExtendedTypeRequest{extension},
                                nominal->getDeclaredType());
      ctx.evaluator.cacheOutput(ExtendedNominalRequest{extension},
                                std::move(nominal));
      nominal->addExtension(extension);

      // Make it accessible to getTopLevelDecls()
      if (auto file = dyn_cast<FileUnit>(nominal->getModuleScopeContext()))
        file->getOrCreateSynthesizedFile().addTopLevelDecl(extension);

      conformanceDC = extension;
    }

    auto conformance = ctx.getConformance(
        nominal->getDeclaredInterfaceType(), proto, nominal->getLoc(),
        conformanceDC, ProtocolConformanceState::Complete,
        /*isUnchecked=*/attrMakingUnavailable != nullptr);
    conformance->setSourceKindAndImplyingConformance(
        ConformanceEntryKind::Synthesized, nullptr);

    nominal->registerProtocolConformance(conformance, /*synthesized=*/true);
    return conformance;
  };

  // A non-protocol type with a global actor is implicitly Sendable.
  if (nominal->getGlobalActorAttr()) {
    // If this is a class, check the superclass. If it's already Sendable,
    // form an inherited conformance.
    if (classDecl) {
      if (Type superclass = classDecl->getSuperclass()) {
        auto classModule = classDecl->getParentModule();
        if (auto inheritedConformance = TypeChecker::conformsToProtocol(
                classDecl->mapTypeIntoContext(superclass),
                proto, classModule, /*allowMissing=*/false)) {
          inheritedConformance = inheritedConformance
              .mapConformanceOutOfContext();
          if (inheritedConformance.isConcrete()) {
            return ctx.getInheritedConformance(
                nominal->getDeclaredInterfaceType(),
                inheritedConformance.getConcrete());
          }
        }
      }
    }

    // Form the implicit conformance to Sendable.
    return formConformance(nullptr);
  }

  if (auto attr = nominal->getAttrs().getEffectiveSendableAttr()) {
    assert(!isa<SendableAttr>(attr) &&
           "Conformance should have been added by SynthesizedProtocolAttr!");
    return formConformance(cast<NonSendableAttr>(attr));
  }

  // Only structs and enums can get implicit Sendable conformances by
  // considering their instance data.
  if (!isa<StructDecl>(nominal) && !isa<EnumDecl>(nominal))
    return nullptr;

  SendableCheck check;

  // Okay to infer Sendable conformance for non-public types or when
  // specifically requested.
  if (nominal->getASTContext().LangOpts.EnableInferPublicSendable ||
      !nominal->getFormalAccessScope(
          /*useDC=*/nullptr, /*treatUsableFromInlineAsPublic=*/true)
            .isPublic()) {
    check = SendableCheck::Implicit;
  } else if (nominal->hasClangNode() ||
             nominal->getAttrs().hasAttribute<FixedLayoutAttr>() ||
             nominal->getAttrs().hasAttribute<FrozenAttr>()) {
    // @_frozen public types can also infer Sendable, but be more careful here.
    check = SendableCheck::ImplicitForExternallyVisible;
  } else {
    // No inference.
    return nullptr;
  }

  // Check the instance storage for Sendable conformance.
  if (checkSendableInstanceStorage(nominal, nominal, check))
    return nullptr;

  return formConformance(nullptr);
}

/// Apply @Sendable and/or @MainActor to the given parameter type.
static Type applyUnsafeConcurrencyToParameterType(
    Type type, bool sendable, bool mainActor) {
  if (Type objectType = type->getOptionalObjectType()) {
    return OptionalType::get(
        applyUnsafeConcurrencyToParameterType(objectType, sendable, mainActor));
  }

  auto fnType = type->getAs<FunctionType>();
  if (!fnType)
    return type;

  Type globalActor;
  if (mainActor)
    globalActor = type->getASTContext().getMainActorType();

  return fnType->withExtInfo(fnType->getExtInfo()
                               .withConcurrent(sendable)
                               .withGlobalActor(globalActor));
}

/// Determine whether the given name is that of a DispatchQueue operation that
/// takes a closure to be executed on the queue.
bool swift::isDispatchQueueOperationName(StringRef name) {
  return llvm::StringSwitch<bool>(name)
    .Case("sync", true)
    .Case("async", true)
    .Case("asyncAndWait", true)
    .Case("asyncAfter", true)
    .Case("concurrentPerform", true)
    .Default(false);
}

/// Determine whether this function is implicitly known to have its
/// parameters of function type be @_unsafeSendable.
///
/// This hard-codes knowledge of a number of functions that will
/// eventually have @_unsafeSendable and, eventually, @Sendable,
/// on their parameters of function type.
static bool hasKnownUnsafeSendableFunctionParams(AbstractFunctionDecl *func) {
  auto nominal = func->getDeclContext()->getSelfNominalTypeDecl();
  if (!nominal)
    return false;

  // DispatchQueue operations.
  auto nominalName = nominal->getName().str();
  if (nominalName == "DispatchQueue") {
    auto name = func->getBaseName().userFacingName();
    return isDispatchQueueOperationName(name);
  }

  return false;
}

Type swift::adjustVarTypeForConcurrency(
    Type type, VarDecl *var, DeclContext *dc,
    llvm::function_ref<Type(const AbstractClosureExpr *)> getType) {
  if (!var->preconcurrency())
    return type;

  if (contextRequiresStrictConcurrencyChecking(dc, getType))
    return type;

  bool isLValue = false;
  if (auto *lvalueType = type->getAs<LValueType>()) {
    type = lvalueType->getObjectType();
    isLValue = true;
  }

  type = type->stripConcurrency(/*recurse=*/false, /*dropGlobalActor=*/true);

  if (isLValue)
    type = LValueType::get(type);

  return type;
}

/// Adjust a function type for @_unsafeSendable, @_unsafeMainActor, and
/// @preconcurrency.
static AnyFunctionType *applyUnsafeConcurrencyToFunctionType(
    AnyFunctionType *fnType, ValueDecl *decl,
    bool inConcurrencyContext, unsigned numApplies, bool isMainDispatchQueue) {
  // Functions/subscripts/enum elements have function types to adjust.
  auto func = dyn_cast_or_null<AbstractFunctionDecl>(decl);
  auto subscript = dyn_cast_or_null<SubscriptDecl>(decl);

  if (!func && !subscript)
    return fnType;

  AnyFunctionType *outerFnType = nullptr;
  if (func && func->hasImplicitSelfDecl()) {
    outerFnType = fnType;
    fnType = outerFnType->getResult()->castTo<AnyFunctionType>();

    if (numApplies > 0)
      --numApplies;
  }

  SmallVector<AnyFunctionType::Param, 4> newTypeParams;
  auto typeParams = fnType->getParams();
  auto paramDecls = func ? func->getParameters() : subscript->getIndices();
  assert(typeParams.size() == paramDecls->size());
  bool knownUnsafeParams = func && hasKnownUnsafeSendableFunctionParams(func);
  bool stripConcurrency =
      decl->preconcurrency() && !inConcurrencyContext;
  for (unsigned index : indices(typeParams)) {
    auto param = typeParams[index];

    // Determine whether the resulting parameter should be @Sendable or
    // @MainActor. @Sendable occurs only in concurrency contents, while
    // @MainActor occurs in concurrency contexts or those where we have an
    // application.
    bool addSendable = knownUnsafeParams && inConcurrencyContext;
    bool addMainActor =
        (isMainDispatchQueue && knownUnsafeParams) &&
        (inConcurrencyContext || numApplies >= 1);
    Type newParamType = param.getPlainType();
    if (addSendable || addMainActor) {
      newParamType = applyUnsafeConcurrencyToParameterType(
        param.getPlainType(), addSendable, addMainActor);
    } else if (stripConcurrency) {
      newParamType = param.getPlainType()->stripConcurrency(
          /*recurse=*/false, /*dropGlobalActor=*/numApplies == 0);
    }

    if (!newParamType || newParamType->isEqual(param.getPlainType())) {
      // If any prior parameter has changed, record this one.
      if (!newTypeParams.empty())
        newTypeParams.push_back(param);

      continue;
    }

    // If this is the first parameter to have changed, copy all of the others
    // over.
    if (newTypeParams.empty()) {
      newTypeParams.append(typeParams.begin(), typeParams.begin() + index);
    }

    // Transform the parameter type.
    newTypeParams.push_back(param.withType(newParamType));
  }

  // Compute the new result type.
  Type newResultType = fnType->getResult();
  if (stripConcurrency) {
    newResultType = newResultType->stripConcurrency(
        /*recurse=*/false, /*dropGlobalActor=*/true);

    if (!newResultType->isEqual(fnType->getResult()) && newTypeParams.empty()) {
      newTypeParams.append(typeParams.begin(), typeParams.end());
    }
  }

  // If we didn't change any parameters, we're done.
  if (newTypeParams.empty() && newResultType->isEqual(fnType->getResult())) {
    return outerFnType ? outerFnType : fnType;
  }

  // Rebuild the (inner) function type.
  fnType = FunctionType::get(
      newTypeParams, newResultType, fnType->getExtInfo());

  if (!outerFnType)
    return fnType;

  // Rebuild the outer function type.
  if (auto genericFnType = dyn_cast<GenericFunctionType>(outerFnType)) {
    return GenericFunctionType::get(
        genericFnType->getGenericSignature(), outerFnType->getParams(),
        Type(fnType), outerFnType->getExtInfo());
  }

  return FunctionType::get(
      outerFnType->getParams(), Type(fnType), outerFnType->getExtInfo());
}

AnyFunctionType *swift::adjustFunctionTypeForConcurrency(
    AnyFunctionType *fnType, ValueDecl *decl, DeclContext *dc,
    unsigned numApplies, bool isMainDispatchQueue,
    llvm::function_ref<Type(const AbstractClosureExpr *)> getType) {
  // Apply unsafe concurrency features to the given function type.
  bool strictChecking = contextRequiresStrictConcurrencyChecking(dc, getType);
  fnType = applyUnsafeConcurrencyToFunctionType(
      fnType, decl, strictChecking, numApplies, isMainDispatchQueue);

  Type globalActorType;
  if (decl) {
    switch (auto isolation = getActorIsolation(decl)) {
    case ActorIsolation::ActorInstance:
    case ActorIsolation::DistributedActorInstance:
    case ActorIsolation::Independent:
    case ActorIsolation::Unspecified:
      return fnType;

    case ActorIsolation::GlobalActorUnsafe:
      // Only treat as global-actor-qualified within code that has adopted
      // Swift Concurrency features.
      if (!strictChecking)
        return fnType;

      LLVM_FALLTHROUGH;

    case ActorIsolation::GlobalActor:
      globalActorType = isolation.getGlobalActor();
      break;
    }
  }

  // If there's no implicit "self" declaration, apply the global actor to
  // the outermost function type.
  bool hasImplicitSelfDecl = decl && (isa<EnumElementDecl>(decl) ||
      (isa<AbstractFunctionDecl>(decl) &&
       cast<AbstractFunctionDecl>(decl)->hasImplicitSelfDecl()));
  if (!hasImplicitSelfDecl) {
    return fnType->withExtInfo(
        fnType->getExtInfo().withGlobalActor(globalActorType));
  }

  // Dig out the inner function type.
  auto innerFnType = fnType->getResult()->getAs<AnyFunctionType>();
  if (!innerFnType)
    return fnType;

  // Update the inner function type with the global actor.
  innerFnType = innerFnType->withExtInfo(
      innerFnType->getExtInfo().withGlobalActor(globalActorType));

  // Rebuild the outer function type around it.
  if (auto genericFnType = dyn_cast<GenericFunctionType>(fnType)) {
    return GenericFunctionType::get(
        genericFnType->getGenericSignature(), fnType->getParams(),
        Type(innerFnType), fnType->getExtInfo());
  }

  return FunctionType::get(
      fnType->getParams(), Type(innerFnType), fnType->getExtInfo());
}

bool swift::completionContextUsesConcurrencyFeatures(const DeclContext *dc) {
  return contextRequiresStrictConcurrencyChecking(
      dc, [](const AbstractClosureExpr *) {
        return Type();
      });
}

AbstractFunctionDecl const *swift::isActorInitOrDeInitContext(
    const DeclContext *dc,
    llvm::function_ref<bool(const AbstractClosureExpr *)> isSendable) {
  while (true) {
    // Non-Sendable closures are considered part of the enclosing context.
    if (auto *closure = dyn_cast<AbstractClosureExpr>(dc)) {
      if (isSendable(closure))
        return nullptr;

      dc = dc->getParent();
      continue;
    }

    if (auto func = dyn_cast<AbstractFunctionDecl>(dc)) {
      // If this is an initializer or deinitializer of an actor, we're done.
      if ((isa<ConstructorDecl>(func) || isa<DestructorDecl>(func)) &&
          getSelfActorDecl(dc->getParent()))
        return func;

      // Non-Sendable local functions are considered part of the enclosing
      // context.
      if (func->getDeclContext()->isLocalContext()) {
        if (auto fnType = func->getInterfaceType()->getAs<AnyFunctionType>()) {
          if (fnType->isSendable())
            return nullptr;

          dc = dc->getParent();
          continue;
        }
      }
    }

    return nullptr;
  }
}

/// Find the directly-referenced parameter or capture of a parameter for
/// for the given expression.
VarDecl *swift::getReferencedParamOrCapture(
    Expr *expr,
    llvm::function_ref<Expr *(OpaqueValueExpr *)> getExistentialValue) {
  // Look through identity expressions and implicit conversions.
  Expr *prior;

  do {
    prior = expr;

    expr = expr->getSemanticsProvidingExpr();

    if (auto conversion = dyn_cast<ImplicitConversionExpr>(expr))
      expr = conversion->getSubExpr();

    // Map opaque values.
    if (auto opaqueValue = dyn_cast<OpaqueValueExpr>(expr)) {
      if (auto *value = getExistentialValue(opaqueValue))
        expr = value;
    }
  } while (prior != expr);

  // 'super' references always act on a 'self' variable.
  if (auto super = dyn_cast<SuperRefExpr>(expr))
    return super->getSelf();

  // Declaration references to a variable.
  if (auto declRef = dyn_cast<DeclRefExpr>(expr))
    return dyn_cast<VarDecl>(declRef->getDecl());

  return nullptr;
}

bool swift::isPotentiallyIsolatedActor(
    VarDecl *var, llvm::function_ref<bool(ParamDecl *)> isIsolated) {
  if (!var)
    return false;

  if (var->getName().str().equals("__secretlyKnownToBeLocal")) {
    // FIXME(distributed): we did a dynamic check and know that this actor is
    // local,
    //  but we can't express that to the type system; the real implementation
    //  will have to mark 'self' as "known to be local" after an is-local check.
    return true;
  }

  if (auto param = dyn_cast<ParamDecl>(var))
    return isIsolated(param);

  // If this is a captured 'self', check whether the original 'self' is
  // isolated.
  if (var->isSelfParamCapture())
    return var->isSelfParamCaptureIsolated();

  return false;
}
