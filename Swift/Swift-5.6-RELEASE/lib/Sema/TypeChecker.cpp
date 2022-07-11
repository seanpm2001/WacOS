//===--- TypeChecker.cpp - Type Checking ----------------------------------===//
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
// This file implements the swift::performTypeChecking entry point for
// semantic analysis.
//
//===----------------------------------------------------------------------===//

#include "swift/Subsystems.h"
#include "TypeChecker.h"
#include "TypeCheckDecl.h"
#include "TypeCheckObjC.h"
#include "TypeCheckType.h"
#include "CodeSynthesis.h"
#include "MiscDiagnostics.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/ASTVisitor.h"
#include "swift/AST/Attr.h"
#include "swift/AST/DiagnosticSuppression.h"
#include "swift/AST/ExistentialLayout.h"
#include "swift/AST/Identifier.h"
#include "swift/AST/ImportCache.h"
#include "swift/AST/Initializer.h"
#include "swift/AST/ModuleLoader.h"
#include "swift/AST/NameLookup.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/SourceFile.h"
#include "swift/AST/TypeCheckRequests.h"
#include "swift/Basic/Statistic.h"
#include "swift/Basic/STLExtras.h"
#include "swift/Parse/Lexer.h"
#include "swift/Sema/IDETypeChecking.h"
#include "swift/Strings.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/Twine.h"
#include <algorithm>

using namespace swift;

ProtocolDecl *TypeChecker::getProtocol(ASTContext &Context, SourceLoc loc,
                                       KnownProtocolKind kind) {
  auto protocol = Context.getProtocol(kind);
  if (!protocol && loc.isValid()) {
    Context.Diags.diagnose(loc, diag::missing_protocol,
                           Context.getIdentifier(getProtocolName(kind)));
  }

  if (protocol && protocol->isInvalid()) {
    return nullptr;
  }

  return protocol;
}

ProtocolDecl *TypeChecker::getLiteralProtocol(ASTContext &Context, Expr *expr) {
  if (isa<ArrayExpr>(expr))
    return TypeChecker::getProtocol(
        Context, expr->getLoc(), KnownProtocolKind::ExpressibleByArrayLiteral);

  if (isa<DictionaryExpr>(expr))
    return TypeChecker::getProtocol(
        Context, expr->getLoc(),
        KnownProtocolKind::ExpressibleByDictionaryLiteral);

  if (!isa<LiteralExpr>(expr))
    return nullptr;
  
  if (isa<NilLiteralExpr>(expr))
    return TypeChecker::getProtocol(Context, expr->getLoc(),
                                    KnownProtocolKind::ExpressibleByNilLiteral);

  if (isa<IntegerLiteralExpr>(expr))
    return TypeChecker::getProtocol(
        Context, expr->getLoc(),
        KnownProtocolKind::ExpressibleByIntegerLiteral);

  if (isa<FloatLiteralExpr>(expr))
    return TypeChecker::getProtocol(
        Context, expr->getLoc(), KnownProtocolKind::ExpressibleByFloatLiteral);

  if (isa<BooleanLiteralExpr>(expr))
    return TypeChecker::getProtocol(
        Context, expr->getLoc(),
        KnownProtocolKind::ExpressibleByBooleanLiteral);

  if (const auto *SLE = dyn_cast<StringLiteralExpr>(expr)) {
    if (SLE->isSingleUnicodeScalar())
      return TypeChecker::getProtocol(
          Context, expr->getLoc(),
          KnownProtocolKind::ExpressibleByUnicodeScalarLiteral);

    if (SLE->isSingleExtendedGraphemeCluster())
      return getProtocol(
          Context, expr->getLoc(),
          KnownProtocolKind::ExpressibleByExtendedGraphemeClusterLiteral);

    return TypeChecker::getProtocol(
        Context, expr->getLoc(), KnownProtocolKind::ExpressibleByStringLiteral);
  }

  if (isa<InterpolatedStringLiteralExpr>(expr))
    return TypeChecker::getProtocol(
        Context, expr->getLoc(),
        KnownProtocolKind::ExpressibleByStringInterpolation);

  if (auto E = dyn_cast<MagicIdentifierLiteralExpr>(expr)) {
    switch (E->getKind()) {
#define MAGIC_STRING_IDENTIFIER(NAME, STRING, SYNTAX_KIND) \
    case MagicIdentifierLiteralExpr::NAME: \
      return TypeChecker::getProtocol( \
          Context, expr->getLoc(), \
          KnownProtocolKind::ExpressibleByStringLiteral);

#define MAGIC_INT_IDENTIFIER(NAME, STRING, SYNTAX_KIND) \
    case MagicIdentifierLiteralExpr::NAME: \
      return TypeChecker::getProtocol( \
          Context, expr->getLoc(), \
          KnownProtocolKind::ExpressibleByIntegerLiteral);

#define MAGIC_POINTER_IDENTIFIER(NAME, STRING, SYNTAX_KIND) \
    case MagicIdentifierLiteralExpr::NAME: \
      return nullptr;

#include "swift/AST/MagicIdentifierKinds.def"
    }
  }

  if (auto E = dyn_cast<ObjectLiteralExpr>(expr)) {
    switch (E->getLiteralKind()) {
#define POUND_OBJECT_LITERAL(Name, Desc, Protocol)                             \
  case ObjectLiteralExpr::Name:                                                \
    return TypeChecker::getProtocol(Context, expr->getLoc(),                   \
                                    KnownProtocolKind::Protocol);
#include "swift/Syntax/TokenKinds.def"
    }
  }

  return nullptr;
}

DeclName TypeChecker::getObjectLiteralConstructorName(ASTContext &Context,
                                                      ObjectLiteralExpr *expr) {
  switch (expr->getLiteralKind()) {
  case ObjectLiteralExpr::colorLiteral: {
    return DeclName(Context, DeclBaseName::createConstructor(),
                    { Context.getIdentifier("_colorLiteralRed"),
                      Context.getIdentifier("green"),
                      Context.getIdentifier("blue"),
                      Context.getIdentifier("alpha") });
  }
  case ObjectLiteralExpr::imageLiteral: {
    return DeclName(Context, DeclBaseName::createConstructor(),
                    { Context.getIdentifier("imageLiteralResourceName") });
  }
  case ObjectLiteralExpr::fileLiteral: {
    return DeclName(Context, DeclBaseName::createConstructor(),
            { Context.getIdentifier("fileReferenceLiteralResourceName") });
  }
  }
  llvm_unreachable("unknown literal constructor");
}

ModuleDecl *TypeChecker::getStdlibModule(const DeclContext *dc) {
  if (auto *stdlib = dc->getASTContext().getStdlibModule()) {
    return stdlib;
  }

  return dc->getParentModule();
}

/// Bind the given extension to the given nominal type.
static void bindExtensionToNominal(ExtensionDecl *ext,
                                   NominalTypeDecl *nominal) {
  if (ext->alreadyBoundToNominal())
    return;

  nominal->addExtension(ext);
}

void swift::bindExtensions(ModuleDecl &mod) {
  // Utility function to try and resolve the extended type without diagnosing.
  // If we succeed, we go ahead and bind the extension. Otherwise, return false.
  auto tryBindExtension = [&](ExtensionDecl *ext) -> bool {
    assert(!ext->canNeverBeBound() &&
           "Only extensions that can ever be bound get here.");
    if (auto nominal = ext->computeExtendedNominal()) {
      bindExtensionToNominal(ext, nominal);
      return true;
    }

    return false;
  };

  // Phase 1 - try to bind each extension, adding those whose type cannot be
  // resolved to a worklist.
  SmallVector<ExtensionDecl *, 8> worklist;

  for (auto file : mod.getFiles()) {
    auto *SF = dyn_cast<SourceFile>(file);
    if (!SF)
      continue;

    auto visitTopLevelDecl = [&](Decl *D) {
      if (auto ED = dyn_cast<ExtensionDecl>(D))
        if (!tryBindExtension(ED))
          worklist.push_back(ED);;
    };

    for (auto *D : SF->getTopLevelDecls())
      visitTopLevelDecl(D);

    for (auto *D : SF->getHoistedDecls())
      visitTopLevelDecl(D);
  }

  // Phase 2 - repeatedly go through the worklist and attempt to bind each
  // extension there, removing it from the worklist if we succeed.
  bool changed;
  do {
    changed = false;

    auto last = std::remove_if(worklist.begin(), worklist.end(),
                               tryBindExtension);
    if (last != worklist.end()) {
      worklist.erase(last, worklist.end());
      changed = true;
    }
  } while(changed);

  // Any remaining extensions are invalid. They will be diagnosed later by
  // typeCheckDecl().
}

static void typeCheckDelayedFunctions(SourceFile &SF) {
  unsigned currentFunctionIdx = 0;

  while (currentFunctionIdx < SF.DelayedFunctions.size()) {
    auto *AFD = SF.DelayedFunctions[currentFunctionIdx];
    assert(!AFD->getDeclContext()->isLocalContext());
    (void) AFD->getTypecheckedBody();
    ++currentFunctionIdx;
  }

  SF.DelayedFunctions.clear();
}

void swift::performTypeChecking(SourceFile &SF) {
  return (void)evaluateOrDefault(SF.getASTContext().evaluator,
                                 TypeCheckSourceFileRequest{&SF}, {});
}

/// If any of the imports in this source file was @preconcurrency but
/// there were no diagnostics downgraded or suppressed due to that
/// @preconcurrency, suggest that the attribute be removed.
static void diagnoseUnnecessaryPreconcurrencyImports(SourceFile &sf) {
  ASTContext &ctx = sf.getASTContext();
  for (const auto &import : sf.getImports()) {
    if (import.options.contains(ImportFlags::Preconcurrency) &&
        import.importLoc.isValid() &&
        !sf.hasImportUsedPreconcurrency(import)) {
      ctx.Diags.diagnose(
          import.importLoc, diag::remove_predates_concurrency_import,
          import.module.importedModule->getName())
        .fixItRemove(import.preconcurrencyRange);
    }
  }
}

evaluator::SideEffect
TypeCheckSourceFileRequest::evaluate(Evaluator &eval, SourceFile *SF) const {
  assert(SF && "Source file cannot be null!");
  assert(SF->ASTStage != SourceFile::TypeChecked &&
         "Should not be re-typechecking this file!");

  // Eagerly build the top-level scopes tree before type checking
  // because type-checking expressions mutates the AST and that throws off the
  // scope-based lookups. Only the top-level scopes because extensions have not
  // been bound yet.
  auto &Ctx = SF->getASTContext();
  SF->getScope()
      .buildEnoughOfTreeForTopLevelExpressionsButDontRequestGenericsOrExtendedNominals();

  BufferIndirectlyCausingDiagnosticRAII cpr(*SF);

  // Could build scope maps here because the AST is stable now.

  {
    FrontendStatsTracer tracer(Ctx.Stats,
                               "Type checking and Semantic analysis");

    if (!Ctx.LangOpts.DisableAvailabilityChecking) {
      // Build the type refinement hierarchy for the primary
      // file before type checking.
      TypeChecker::buildTypeRefinementContextHierarchy(*SF);
    }

    // Type check the top-level elements of the source file.
    for (auto D : SF->getTopLevelDecls()) {
      if (auto *TLCD = dyn_cast<TopLevelCodeDecl>(D)) {
        TypeChecker::typeCheckTopLevelCodeDecl(TLCD);
        TypeChecker::contextualizeTopLevelCode(TLCD);
      } else {
        TypeChecker::typeCheckDecl(D);
      }
    }

    typeCheckDelayedFunctions(*SF);
  }

  diagnoseUnnecessaryPreconcurrencyImports(*SF);

  // Check to see if there's any inconsistent @_implementationOnly imports.
  evaluateOrDefault(
      Ctx.evaluator,
      CheckInconsistentImplementationOnlyImportsRequest{SF->getParentModule()},
      {});

  // Perform various AST transforms we've been asked to perform.
  if (!Ctx.hadError() && Ctx.LangOpts.DebuggerTestingTransform)
    performDebuggerTestingTransform(*SF);

  if (!Ctx.hadError() && Ctx.LangOpts.PCMacro)
    performPCMacro(*SF);

  // Playground transform knows to look out for PCMacro's changes and not
  // to playground log them.
  if (!Ctx.hadError() && Ctx.LangOpts.PlaygroundTransform)
    performPlaygroundTransform(*SF, Ctx.LangOpts.PlaygroundHighPerformance);

  return std::make_tuple<>();
}

void swift::performWholeModuleTypeChecking(SourceFile &SF) {
  auto &Ctx = SF.getASTContext();
  FrontendStatsTracer tracer(Ctx.Stats,
                             "perform-whole-module-type-checking");
  switch (SF.Kind) {
  case SourceFileKind::Library:
  case SourceFileKind::Main:
    diagnoseObjCMethodConflicts(SF);
    diagnoseObjCUnsatisfiedOptReqConflicts(SF);
    diagnoseUnintendedObjCMethodOverrides(SF);
    diagnoseAttrsAddedByAccessNote(SF);
    return;
  case SourceFileKind::SIL:
  case SourceFileKind::Interface:
    // SIL modules and .swiftinterface files don't benefit from whole-module
    // ObjC checking - skip it.
    return;
  }
}

bool swift::isAdditiveArithmeticConformanceDerivationEnabled(SourceFile &SF) {
  auto &ctx = SF.getASTContext();
  // Return true if `AdditiveArithmetic` derived conformances are explicitly
  // enabled.
  if (ctx.LangOpts.EnableExperimentalAdditiveArithmeticDerivedConformances)
    return true;
  // Otherwise, return true iff differentiable programming is enabled.
  // Differentiable programming depends on `AdditiveArithmetic` derived
  // conformances.
  return isDifferentiableProgrammingEnabled(SF);
}

Type swift::performTypeResolution(TypeRepr *TyR, ASTContext &Ctx,
                                  bool isSILMode, bool isSILType,
                                  GenericEnvironment *GenericEnv,
                                  GenericParamList *GenericParams,
                                  DeclContext *DC, bool ProduceDiagnostics) {
  TypeResolutionOptions options = None;
  if (isSILMode)
    options |= TypeResolutionFlags::SILMode;
  if (isSILType)
    options |= TypeResolutionFlags::SILType;

  Optional<DiagnosticSuppression> suppression;
  if (!ProduceDiagnostics)
    suppression.emplace(Ctx.Diags);

  return TypeResolution::forInterface(
             DC, GenericEnv, options,
             [](auto unboundTy) {
               // FIXME: Don't let unbound generic types escape type resolution.
               // For now, just return the unbound generic type.
               return unboundTy;
             },
             // FIXME: Don't let placeholder types escape type resolution.
             // For now, just return the placeholder type.
             PlaceholderType::get)
      .resolveType(TyR, GenericParams);
}

namespace {
  class BindGenericParamsWalker : public ASTWalker {
    DeclContext *dc;
    GenericParamList *params;

  public:
    BindGenericParamsWalker(DeclContext *dc,
                            GenericParamList *params)
        : dc(dc), params(params) {}

    bool walkToTypeReprPre(TypeRepr *T) override {
      if (auto *ident = dyn_cast<IdentTypeRepr>(T)) {
        auto firstComponent = ident->getComponentRange().front();
        auto name = firstComponent->getNameRef().getBaseIdentifier();
        if (auto *paramDecl = params->lookUpGenericParam(name))
          firstComponent->setValue(paramDecl, dc);
      }

      return true;
    }
  };
}

/// Expose TypeChecker's handling of GenericParamList to SIL parsing.
GenericEnvironment *
swift::handleSILGenericParams(GenericParamList *genericParams,
                              DeclContext *DC) {
  if (genericParams == nullptr)
    return nullptr;

  SmallVector<GenericParamList *, 2> nestedList;
  for (auto *innerParams = genericParams;
       innerParams != nullptr;
       innerParams = innerParams->getOuterParameters()) {
    nestedList.push_back(innerParams);
  }

  std::reverse(nestedList.begin(), nestedList.end());

  BindGenericParamsWalker walker(DC, genericParams);

  for (unsigned i = 0, e = nestedList.size(); i < e; ++i) {
    auto genericParams = nestedList[i];
    genericParams->setDepth(i);

    genericParams->walk(walker);
  }

  auto request = InferredGenericSignatureRequest{
      DC->getParentModule(), /*parentSig=*/nullptr,
      nestedList.back(), WhereClauseOwner(),
      {}, {}, /*allowConcreteGenericParams=*/true};
  auto sig = evaluateOrDefault(DC->getASTContext().evaluator, request,
                               GenericSignatureWithError()).getPointer();

  return sig.getGenericEnvironment();
}

void swift::typeCheckPatternBinding(PatternBindingDecl *PBD,
                                    unsigned bindingIndex,
                                    bool leaveClosureBodiesUnchecked) {
  assert(!PBD->isInitializerChecked(bindingIndex) &&
         PBD->getInit(bindingIndex));

  auto &Ctx = PBD->getASTContext();
  DiagnosticSuppression suppression(Ctx.Diags);

  TypeCheckExprOptions options;
  if (leaveClosureBodiesUnchecked)
    options |= TypeCheckExprFlags::LeaveClosureBodyUnchecked;

  TypeChecker::typeCheckPatternBinding(PBD, bindingIndex,
                                       /*patternType=*/Type(), options);
}

bool swift::typeCheckASTNodeAtLoc(DeclContext *DC, SourceLoc TargetLoc) {
  auto &Ctx = DC->getASTContext();
  DiagnosticSuppression suppression(Ctx.Diags);
  return !evaluateOrDefault(Ctx.evaluator,
                            TypeCheckASTNodeAtLocRequest{DC, TargetLoc},
                            true);
}

void TypeChecker::checkForForbiddenPrefix(ASTContext &C, DeclBaseName Name) {
  if (C.TypeCheckerOpts.DebugForbidTypecheckPrefix.empty())
    return;

  // Don't touch special names or empty names.
  if (Name.isSpecial() || Name.empty())
    return;

  StringRef Str = Name.getIdentifier().str();
  if (Str.startswith(C.TypeCheckerOpts.DebugForbidTypecheckPrefix)) {
    std::string Msg = "forbidden typecheck occurred: ";
    Msg += Str;
    llvm::report_fatal_error(Msg);
  }
}

DeclTypeCheckingSemantics
TypeChecker::getDeclTypeCheckingSemantics(ValueDecl *decl) {
  // Check for a @_semantics attribute.
  if (auto semantics = decl->getAttrs().getAttribute<SemanticsAttr>()) {
    if (semantics->Value.equals("typechecker.type(of:)"))
      return DeclTypeCheckingSemantics::TypeOf;
    if (semantics->Value.equals("typechecker.withoutActuallyEscaping(_:do:)"))
      return DeclTypeCheckingSemantics::WithoutActuallyEscaping;
    if (semantics->Value.equals("typechecker._openExistential(_:do:)"))
      return DeclTypeCheckingSemantics::OpenExistential;
  }
  return DeclTypeCheckingSemantics::Normal;
}

bool TypeChecker::isDifferentiable(Type type, bool tangentVectorEqualsSelf,
                                   DeclContext *dc,
                                   Optional<TypeResolutionStage> stage) {
  if (stage)
    type = dc->mapTypeIntoContext(type);
  auto tanSpace = type->getAutoDiffTangentSpace(
      LookUpConformanceInModule(dc->getParentModule()));
  if (!tanSpace)
    return false;
  // If no `Self == Self.TangentVector` requirement, return true.
  if (!tangentVectorEqualsSelf)
    return true;
  // Otherwise, return true if `Self == Self.TangentVector`.
  return type->getCanonicalType() == tanSpace->getCanonicalType();
}

bool TypeChecker::diagnoseInvalidFunctionType(FunctionType *fnTy, SourceLoc loc,
                                              Optional<FunctionTypeRepr *>repr,
                                              DeclContext *dc,
                                              Optional<TypeResolutionStage> stage) {
  // If the type has a placeholder, don't try to diagnose anything now since
  // we'll produce a better diagnostic when (if) the expression successfully
  // typechecks.
  if (fnTy->hasPlaceholder())
    return false;

  // If the type is a block or C function pointer, it must be representable in
  // ObjC.
  auto representation = fnTy->getRepresentation();
  auto extInfo = fnTy->getExtInfo();
  auto &ctx = dc->getASTContext();

  bool hadAnyError = false;

  switch (representation) {
  case AnyFunctionType::Representation::Block:
  case AnyFunctionType::Representation::CFunctionPointer:
    if (!fnTy->isRepresentableIn(ForeignLanguage::ObjectiveC, dc)) {
      StringRef strName =
        (representation == AnyFunctionType::Representation::Block)
        ? "block"
        : "c";
      auto extInfo2 =
        extInfo.withRepresentation(AnyFunctionType::Representation::Swift);
      auto simpleFnTy = FunctionType::get(fnTy->getParams(), fnTy->getResult(),
                                          extInfo2);
      ctx.Diags.diagnose(loc, diag::objc_convention_invalid,
                         simpleFnTy, strName);
      hadAnyError = true;
    }
    break;

  case AnyFunctionType::Representation::Thin:
  case AnyFunctionType::Representation::Swift:
    break;
  }

  // `@differentiable` function types must return a differentiable type and have
  // differentiable (or `@noDerivative`) parameters.
  if (extInfo.isDifferentiable() &&
      stage != TypeResolutionStage::Structural) {
    auto result = fnTy->getResult();
    auto params = fnTy->getParams();
    auto diffKind = extInfo.getDifferentiabilityKind();
    bool isLinear = diffKind == DifferentiabilityKind::Linear;

    // Check the params.

    // Emit `@noDerivative` fixit only if there is at least one valid
    // differentiability parameter. Otherwise, adding `@noDerivative` produces
    // an ill-formed function type.
    auto hasValidDifferentiabilityParam =
    llvm::find_if(params, [&](AnyFunctionType::Param param) {
      if (param.isNoDerivative())
        return false;
      return TypeChecker::isDifferentiable(param.getPlainType(),
                                           /*tangentVectorEqualsSelf*/ isLinear,
                                           dc, stage);
    }) != params.end();
    bool alreadyDiagnosedOneParam = false;
    for (unsigned i = 0, end = fnTy->getNumParams(); i != end; ++i) {
      auto param = params[i];
      if (param.isNoDerivative())
        continue;
      auto paramType = param.getPlainType();
      if (TypeChecker::isDifferentiable(paramType, isLinear, dc, stage))
        continue;
      auto diagLoc =
          repr ? (*repr)->getArgsTypeRepr()->getElement(i).Type->getLoc() : loc;
      auto paramTypeString = paramType->getString();
      auto diagnostic = ctx.Diags.diagnose(
          diagLoc, diag::differentiable_function_type_invalid_parameter,
          paramTypeString, isLinear, hasValidDifferentiabilityParam);
      alreadyDiagnosedOneParam = true;
      hadAnyError = true;
      if (hasValidDifferentiabilityParam)
        diagnostic.fixItInsert(diagLoc, "@noDerivative ");
    }
    // Reject the case where all parameters have '@noDerivative'.
    if (!alreadyDiagnosedOneParam && !hasValidDifferentiabilityParam) {
      auto diagLoc = repr ? (*repr)->getArgsTypeRepr()->getLoc() : loc;
      auto diag = ctx.Diags.diagnose(
          diagLoc,
          diag::differentiable_function_type_no_differentiability_parameters,
          isLinear);
      hadAnyError = true;

      if (repr) {
          diag.highlight((*repr)->getSourceRange());
      }
    }

    // Check the result
    bool differentiable = isDifferentiable(result,
                                           /*tangentVectorEqualsSelf*/ isLinear,
                                           dc, stage);
    if (!differentiable) {
      auto diagLoc = repr ? (*repr)->getResultTypeRepr()->getLoc() : loc;
      auto resultStr = fnTy->getResult()->getString();
      auto diag = ctx.Diags.diagnose(
          diagLoc, diag::differentiable_function_type_invalid_result, resultStr,
          isLinear);
      hadAnyError = true;

      if (repr) {
          diag.highlight((*repr)->getResultTypeRepr()->getSourceRange());
      }
    }
  }

  return hadAnyError;
}
