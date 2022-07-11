//===--- TypeCheckCodeCompletion.cpp - Type Checking for Code Completion --===//
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
// This file implements various entry points for use by lib/IDE/.
//
//===----------------------------------------------------------------------===//

#include "swift/Subsystems.h"
#include "TypeChecker.h"
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
#include "swift/AST/ParameterList.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/SourceFile.h"
#include "swift/AST/Type.h"
#include "swift/AST/TypeCheckRequests.h"
#include "swift/Basic/Defer.h"
#include "swift/Basic/Statistic.h"
#include "swift/Basic/STLExtras.h"
#include "swift/Parse/Lexer.h"
#include "swift/Sema/IDETypeChecking.h"
#include "swift/Sema/CodeCompletionTypeChecking.h"
#include "swift/Sema/ConstraintSystem.h"
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
using namespace constraints;

/// Find the declaration directly referenced by this expression.
static std::pair<ValueDecl *, FunctionRefKind>
findReferencedDecl(Expr *expr, DeclNameLoc &loc) {
  do {
    expr = expr->getSemanticsProvidingExpr();

    if (auto ice = dyn_cast<ImplicitConversionExpr>(expr)) {
      expr = ice->getSubExpr();
      continue;
    }

    if (auto dre = dyn_cast<DeclRefExpr>(expr)) {
      loc = dre->getNameLoc();
      return { dre->getDecl(), dre->getFunctionRefKind() };
    }

    return { nullptr, FunctionRefKind::Unapplied };
  } while (true);
}

// Check if \p E is a call expression to curried thunk of "KeyPath as function".
// i.e. '{ `$kp$` in { $0[keyPath: $kp$] } }(keypath)'
static bool isKeyPathCurriedThunkCallExpr(Expr *E) {
  auto CE = dyn_cast<CallExpr>(E);
  if (!CE)
    return false;
  auto thunk = dyn_cast<AutoClosureExpr>(CE->getFn());
  if (!thunk)
    return false;
  if (thunk->getParameters()->size() != 1 ||
      thunk->getParameters()->get(0)->getParameterName().str() != "$kp$")
    return false;

  auto *unaryArg = CE->getArgs()->getUnlabeledUnaryExpr();
  if (!unaryArg)
    return false;
  return isa<KeyPathExpr>(unaryArg);
}

// Extract the keypath expression from the curried thunk expression.
static Expr *extractKeyPathFromCurryThunkCall(Expr *E) {
  assert(isKeyPathCurriedThunkCallExpr(E));
  return cast<CallExpr>(E)->getArgs()->getUnlabeledUnaryExpr();
}

namespace {

/// AST walker that "sanitizes" an expression for re-typechecking during
/// code completion.
///
/// FIXME: Remove this.
class SanitizeExpr : public ASTWalker {
  ASTContext &C;
  bool ShouldReusePrecheckedType;
  llvm::SmallDenseMap<OpaqueValueExpr *, Expr *, 4> OpenExistentials;

public:
  SanitizeExpr(ASTContext &C,
               bool shouldReusePrecheckedType)
    : C(C), ShouldReusePrecheckedType(shouldReusePrecheckedType) { }

  std::pair<bool, ArgumentList *>
  walkToArgumentListPre(ArgumentList *argList) override {
    // Return the argument list to the state prior to being rewritten. This will
    // strip default arguments and expand variadic args.
    return {true, argList->getOriginalArgs()};
  }

  std::pair<bool, Expr *> walkToExprPre(Expr *expr) override {
    while (true) {

      // If we should reuse pre-checked types, don't sanitize the expression
      // if it's already type-checked.
      if (ShouldReusePrecheckedType && expr->getType())
        return { false, expr };

      // OpenExistentialExpr contains OpaqueValueExpr in its sub expression.
      if (auto OOE = dyn_cast<OpenExistentialExpr>(expr)) {
        auto archetypeVal = OOE->getOpaqueValue();
        auto base = OOE->getExistentialValue();

        bool inserted = OpenExistentials.insert({archetypeVal, base}).second;
        assert(inserted && "OpaqueValue appears multiple times?");
        (void)inserted;
        SWIFT_DEFER { OpenExistentials.erase(archetypeVal); };

        // Walk to and return the base expression to erase any existentials
        // within it.
        return { false, OOE->getSubExpr()->walk(*this) };
      }

      // Hacky, this behaves just like an OpenedExistential in that it changes
      // the expr tree.
      if (auto ISLE = dyn_cast<InterpolatedStringLiteralExpr>(expr)) {
        if (auto subExpr = ISLE->getAppendingExpr()->getSubExpr()) {
          if (auto opaqueValue = dyn_cast<OpaqueValueExpr>(subExpr)) {
            ISLE->getAppendingExpr()->setSubExpr(nullptr);
          }
        }
      }

      // Substitute OpaqueValue with its representing existental.
      if (auto OVE = dyn_cast<OpaqueValueExpr>(expr)) {
        auto value = OpenExistentials.find(OVE);

        if (value != OpenExistentials.end()) {
          expr = value->second;
          continue;
        } else {
          assert(OVE->isPlaceholder() &&
                 "Didn't see this OVE in a containing OpenExistentialExpr?");
        }
      }

      // Skip any implicit conversions applied to this expression.
      if (auto ICE = dyn_cast<ImplicitConversionExpr>(expr)) {
        expr = ICE->getSubExpr();
        continue;
      }

      // MakeTemporarilyEscapableExpr is typechecked expression.
      if (auto MTEE = dyn_cast<MakeTemporarilyEscapableExpr>(expr)) {
        expr = MTEE->getOriginalExpr();
        continue;
      }

      // Extract keypath from '{ `$kp$` in { $0[keyPath: $kp$] } }(keypath)'
      if (isKeyPathCurriedThunkCallExpr(expr)) {
        expr = extractKeyPathFromCurryThunkCall(expr);
        continue;
      }

      // Restore '@autoclosure'd value.
      if (auto ACE = dyn_cast<AutoClosureExpr>(expr)) {
        // This is only valid if the closure doesn't have parameters.
        if (ACE->getParameters()->size() == 0) {
          expr = ACE->getSingleExpressionBody();
          continue;
        }
        llvm_unreachable("other AutoClosureExpr must be handled specially");
      }

      // Remove any semantic expression injected by typechecking.
      if (auto EPE = dyn_cast<EditorPlaceholderExpr>(expr)) {
        EPE->setSemanticExpr(nullptr);
      }

      // If this is a closure, only walk into its children if they
      // are type-checked in the context of the enclosing expression.
      if (auto closure = dyn_cast<ClosureExpr>(expr)) {
        // TODO: This has to be deleted once `EnableMultiStatementClosureInference`
        //       is enabled by default.
        if (!closure->hasSingleExpressionBody())
          return { false, expr };
        for (auto &Param : *closure->getParameters()) {
          Param->setSpecifier(swift::ParamSpecifier::Default);
        }
      }

      // Now, we're ready to walk into sub expressions.
      return {true, expr};
    }
  }

  Expr *walkToExprPost(Expr *expr) override {
    assert(!isa<ImplicitConversionExpr>(expr) &&
           "ImplicitConversionExpr should be eliminated in walkToExprPre");

    auto buildMemberRef = [&](Type memberType, Expr *base, SourceLoc dotLoc,
                              ConcreteDeclRef member, DeclNameLoc memberLoc,
                              bool implicit) -> Expr * {
      auto *memberRef = new (C)
          MemberRefExpr(base, dotLoc, member, memberLoc, implicit);

      if (memberType) {
        memberRef->setType(memberType);
        return memberRef;
      }

      return memberRef;
    };

    // A DotSyntaxCallExpr is a member reference that has already been
    // type-checked down to a call; turn it back into an overloaded
    // member reference expression.
    if (auto dotCall = dyn_cast<DotSyntaxCallExpr>(expr)) {
      DeclNameLoc memberLoc;
      auto memberAndFunctionRef = findReferencedDecl(dotCall->getFn(),
                                                     memberLoc);
      if (memberAndFunctionRef.first) {
        assert(!isa<ImplicitConversionExpr>(dotCall->getBase()));
        return buildMemberRef(dotCall->getType(),
                              dotCall->getBase(),
                              dotCall->getDotLoc(),
                              memberAndFunctionRef.first,
                              memberLoc, expr->isImplicit());
      }
    }

    if (auto *dynamicMember = dyn_cast<DynamicMemberRefExpr>(expr)) {
      if (auto memberRef = dynamicMember->getMember()) {
        assert(!isa<ImplicitConversionExpr>(dynamicMember->getBase()));
        return buildMemberRef(dynamicMember->getType(),
                              dynamicMember->getBase(),
                              dynamicMember->getDotLoc(),
                              memberRef,
                              dynamicMember->getNameLoc(),
                              expr->isImplicit());
      }
    }

    // A DotSyntaxBaseIgnoredExpr is a static member reference that has
    // already been type-checked down to a call where the argument doesn't
    // actually matter; turn it back into an overloaded member reference
    // expression.
    if (auto dotIgnored = dyn_cast<DotSyntaxBaseIgnoredExpr>(expr)) {
      DeclNameLoc memberLoc;
      auto memberAndFunctionRef = findReferencedDecl(dotIgnored->getRHS(),
                                                     memberLoc);
      if (memberAndFunctionRef.first) {
        assert(!isa<ImplicitConversionExpr>(dotIgnored->getLHS()));
        return buildMemberRef(dotIgnored->getType(),
                              dotIgnored->getLHS(),
                              dotIgnored->getDotLoc(),
                              memberAndFunctionRef.first,
                              memberLoc, expr->isImplicit());
      }
    }
    return expr;
  }

  /// Ignore declarations.
  bool walkToDeclPre(Decl *decl) override { return false; }
};

}  // end namespace

static Type
getTypeOfExpressionWithoutApplying(Expr *&expr, DeclContext *dc,
                                   ConcreteDeclRef &referencedDecl,
                                 FreeTypeVariableBinding allowFreeTypeVariables) {
  auto &Context = dc->getASTContext();

  expr = expr->walk(SanitizeExpr(Context,
                                 /*shouldReusePrecheckedType=*/false));

  FrontendStatsTracer StatsTracer(Context.Stats,
                                  "typecheck-expr-no-apply", expr);
  PrettyStackTraceExpr stackTrace(Context, "type-checking", expr);
  referencedDecl = nullptr;

  ConstraintSystemOptions options;
  options |= ConstraintSystemFlags::SuppressDiagnostics;
  options |= ConstraintSystemFlags::LeaveClosureBodyUnchecked;

  // Construct a constraint system from this expression.
  ConstraintSystem cs(dc, options);

  // Attempt to solve the constraint system.
  const Type originalType = expr->getType();
  const bool needClearType = originalType && originalType->hasError();
  const auto recoverOriginalType = [&] () {
    if (needClearType)
      expr->setType(originalType);
  };

  // If the previous checking gives the expr error type, clear the result and
  // re-check.
  if (needClearType)
    expr->setType(Type());
  SolutionApplicationTarget target(
      expr, dc, CTP_Unused, Type(), /*isDiscarded=*/false);
  auto viable = cs.solve(target, allowFreeTypeVariables);
  if (!viable) {
    recoverOriginalType();
    return Type();
  }

  // Get the expression's simplified type.
  expr = target.getAsExpr();
  auto &solution = (*viable)[0];
  auto &solutionCS = solution.getConstraintSystem();
  Type exprType = solution.simplifyType(solutionCS.getType(expr));

  assert(exprType && !exprType->hasTypeVariable() &&
         "free type variable with FreeTypeVariableBinding::GenericParameters?");
  assert(exprType && !exprType->hasPlaceholder() &&
         "type placeholder with FreeTypeVariableBinding::GenericParameters?");

  if (exprType->hasError()) {
    recoverOriginalType();
    return Type();
  }

  // Dig the declaration out of the solution.
  auto semanticExpr = expr->getSemanticsProvidingExpr();
  auto topLocator = cs.getConstraintLocator(semanticExpr);
  referencedDecl = solution.resolveLocatorToDecl(topLocator);

  if (!referencedDecl.getDecl()) {
    // Do another check in case we have a curried call from binding a function
    // reference to a variable, for example:
    //
    //   class C {
    //     func instanceFunc(p1: Int, p2: Int) {}
    //   }
    //   func t(c: C) {
    //     C.instanceFunc(c)#^COMPLETE^#
    //   }
    //
    // We need to get the referenced function so we can complete the argument
    // labels. (Note that the requirement to have labels in the curried call
    // seems inconsistent with the removal of labels from function types.
    // If this changes the following code could be removed).
    if (auto *CE = dyn_cast<CallExpr>(semanticExpr)) {
      if (auto *UDE = dyn_cast<UnresolvedDotExpr>(CE->getFn())) {
        if (isa<TypeExpr>(UDE->getBase())) {
          auto udeLocator = cs.getConstraintLocator(UDE);
          auto udeRefDecl = solution.resolveLocatorToDecl(udeLocator);
          if (auto *FD = dyn_cast_or_null<FuncDecl>(udeRefDecl.getDecl())) {
            if (FD->isInstanceMember())
              referencedDecl = udeRefDecl;
          }
        }
      }
    }
  }

  // Recover the original type if needed.
  recoverOriginalType();
  return exprType;
}

static FunctionType *
getTypeOfCompletionOperatorImpl(DeclContext *DC, Expr *expr,
                                ConcreteDeclRef &referencedDecl) {
  auto &Context = DC->getASTContext();

  FrontendStatsTracer StatsTracer(Context.Stats,
                                  "typecheck-completion-operator", expr);
  PrettyStackTraceExpr stackTrace(Context, "type-checking", expr);

  expr = expr->walk(SanitizeExpr(Context,
                                 /*shouldReusePrecheckedType=*/true));

  ConstraintSystemOptions options;
  options |= ConstraintSystemFlags::SuppressDiagnostics;
  options |= ConstraintSystemFlags::ReusePrecheckedType;
  options |= ConstraintSystemFlags::LeaveClosureBodyUnchecked;

  // Construct a constraint system from this expression.
  ConstraintSystem CS(DC, options);
  expr = CS.generateConstraints(expr, DC);
  if (!expr)
    return nullptr;

  if (CS.isDebugMode()) {
    auto &log = llvm::errs();
    log << "---Initial constraints for the given expression---\n";
    expr->dump(log);
    log << "\n";
    CS.print(log);
  }

  // Attempt to solve the constraint system.
  SmallVector<Solution, 4> viable;
  if (CS.solve(viable, FreeTypeVariableBinding::Disallow))
    return nullptr;

  auto &solution = viable[0];
  if (CS.isDebugMode()) {
    auto &log = llvm::errs();
    log << "---Solution---\n";
    solution.dump(log);
  }

  // Fill the results.
  Expr *opExpr = cast<ApplyExpr>(expr)->getFn();
  referencedDecl =
      solution.resolveLocatorToDecl(CS.getConstraintLocator(opExpr));

  // Return '(ArgType[, ArgType]) -> ResultType' as a function type.
  // We don't use the type of the operator expression because we want the types
  // of the *arguments* instead of the types of the parameters.
  auto *args = cast<ApplyExpr>(expr)->getArgs();
  SmallVector<FunctionType::Param, 2> argTypes;
  for (auto arg : *args)
    argTypes.emplace_back(solution.simplifyType(CS.getType(arg.getExpr())));

  // FIXME: Verify ExtInfo state is correct, not working by accident.
  FunctionType::ExtInfo info;
  return FunctionType::get(argTypes, solution.simplifyType(CS.getType(expr)),
                           info);
}

/// Return the type of operator function for specified LHS, or a null
/// \c Type on error.
FunctionType *
TypeChecker::getTypeOfCompletionOperator(DeclContext *DC, Expr *LHS,
                                         Identifier opName, DeclRefKind refKind,
                                         ConcreteDeclRef &referencedDecl) {

  // For the infix operator, find the actual LHS from pre-folded LHS.
  if (refKind == DeclRefKind::BinaryOperator)
    LHS = TypeChecker::findLHS(DC, LHS, opName);

  if (!LHS)
    return nullptr;

  auto LHSTy = LHS->getType();

  // FIXME: 'UnresolvedType' still might be typechecked by an operator.
  if (!LHSTy || LHSTy->is<UnresolvedType>())
    return nullptr;

  // Meta types and function types cannot be a operand of operator expressions.
  if (LHSTy->is<MetatypeType>() || LHSTy->is<AnyFunctionType>())
    return nullptr;

  auto Loc = LHS->getEndLoc();

  // Build temporary expression to typecheck.
  // We allocate these expressions on the stack because we know they can't
  // escape and there isn't a better way to allocate scratch Expr nodes.
  UnresolvedDeclRefExpr UDRE(DeclNameRef(opName), refKind, DeclNameLoc(Loc));
  auto *opExpr = TypeChecker::resolveDeclRefExpr(
      &UDRE, DC, /*replaceInvalidRefsWithErrors=*/true);

  auto &ctx = DC->getASTContext();
  switch (refKind) {
  case DeclRefKind::PostfixOperator: {
    // (postfix_unary_expr
    //   (declref_expr name=<opName>)
    //   (argument_list
    //     (<LHS>)))
    auto *postfixExpr = PostfixUnaryExpr::create(ctx, opExpr, LHS);
    return getTypeOfCompletionOperatorImpl(DC, postfixExpr, referencedDecl);
  }

  case DeclRefKind::BinaryOperator: {
    // (binary_expr
    //   (declref_expr name=<opName>)
    //   (argument_list
    //     (<LHS>)
    //     (code_completion_expr)))
    CodeCompletionExpr dummyRHS(Loc);
    auto *binaryExpr = BinaryExpr::create(ctx, LHS, opExpr, &dummyRHS,
                                          /*implicit*/ true);
    return getTypeOfCompletionOperatorImpl(DC, binaryExpr, referencedDecl);
  }

  default:
    llvm_unreachable("Invalid DeclRefKind for operator completion");
  }
}

namespace {

class CompletionContextFinder : public ASTWalker {
  enum class ContextKind {
    FallbackExpression,
    StringInterpolation,
    SingleStmtClosure,
    MultiStmtClosure,
    ErrorExpression
  };

  struct Context {
    ContextKind Kind;
    Expr * E;
  };

  /// Stack of all "interesting" contexts up to code completion expression.
  llvm::SmallVector<Context, 4> Contexts;

  /// If we are completing inside an expression, the \c CodeCompletionExpr that
  /// represents the code completion token.

  /// The AST node that represents the code completion token, either as a
  /// \c CodeCompletionExpr or a \c KeyPathExpr which contains a code completion
  /// component.
  llvm::PointerUnion<CodeCompletionExpr *, const KeyPathExpr *> CompletionNode;

  Expr *InitialExpr = nullptr;
  DeclContext *InitialDC;

public:
  /// Finder for completion contexts within the provided initial expression.
  CompletionContextFinder(Expr *initialExpr, DeclContext *DC)
      : InitialExpr(initialExpr), InitialDC(DC) {
    assert(DC);
    initialExpr->walk(*this);
  };

  /// Finder for completion contexts within the outermost non-closure context of
  /// the code completion expression's direct context.
  CompletionContextFinder(DeclContext *completionDC): InitialDC(completionDC) {
    while (auto *ACE = dyn_cast<AbstractClosureExpr>(InitialDC))
      InitialDC = ACE->getParent();
    InitialDC->walkContext(*this);
  }

  std::pair<bool, Expr *> walkToExprPre(Expr *E) override {
    if (auto *closure = dyn_cast<ClosureExpr>(E)) {
      Contexts.push_back({closure->hasSingleExpressionBody()
                            ? ContextKind::SingleStmtClosure
                            : ContextKind::MultiStmtClosure,
                          closure});
    }

    if (isa<InterpolatedStringLiteralExpr>(E)) {
      Contexts.push_back({ContextKind::StringInterpolation, E});
    }

    if (isa<ApplyExpr>(E) || isa<SequenceExpr>(E)) {
      Contexts.push_back({ContextKind::FallbackExpression, E});
    }

    if (auto *Error = dyn_cast<ErrorExpr>(E)) {
      Contexts.push_back({ContextKind::ErrorExpression, E});
      if (auto *OrigExpr = Error->getOriginalExpr()) {
        OrigExpr->walk(*this);
        if (hasCompletionExpr())
          return std::make_pair(false, nullptr);
      }
    }

    if (auto *CCE = dyn_cast<CodeCompletionExpr>(E)) {
      CompletionNode = CCE;
      return std::make_pair(false, nullptr);
    }
    if (auto *KeyPath = dyn_cast<KeyPathExpr>(E)) {
      for (auto &component : KeyPath->getComponents()) {
        if (component.getKind() ==
            KeyPathExpr::Component::Kind::CodeCompletion) {
          CompletionNode = KeyPath;
          return std::make_pair(false, nullptr);
        }
      }
      // Code completion in key paths is modelled by a code completion component
      // Don't walk the key path's parsed expressions.
      return std::make_pair(false, E);
    }

    return std::make_pair(true, E);
  }

  Expr *walkToExprPost(Expr *E) override {
    if (isa<ClosureExpr>(E) || isa<InterpolatedStringLiteralExpr>(E) ||
        isa<ApplyExpr>(E) || isa<SequenceExpr>(E) || isa<ErrorExpr>(E)) {
      assert(Contexts.back().E == E);
      Contexts.pop_back();
    }
    return E;
  }

  /// Check whether code completion expression is located inside of a
  /// multi-statement closure.
  bool locatedInMultiStmtClosure() const {
    return hasContext(ContextKind::MultiStmtClosure);
  }

  bool locatedInStringIterpolation() const {
    return hasContext(ContextKind::StringInterpolation);
  }

  bool hasCompletionExpr() const {
    return CompletionNode.dyn_cast<CodeCompletionExpr *>() != nullptr;
  }

  CodeCompletionExpr *getCompletionExpr() const {
    assert(hasCompletionExpr());
    return CompletionNode.get<CodeCompletionExpr *>();
  }

  bool hasCompletionKeyPathComponent() const {
    return CompletionNode.dyn_cast<const KeyPathExpr *>() != nullptr;
  }

  /// If we are completing in a key path, returns the \c KeyPath that contains
  /// the code completion component.
  const KeyPathExpr *getKeyPathContainingCompletionComponent() const {
    assert(hasCompletionKeyPathComponent());
    return CompletionNode.get<const KeyPathExpr *>();
  }

  /// If we are completing in a key path, returns the index at which the key
  /// path has the code completion component.
  size_t getKeyPathCompletionComponentIndex() const {
    assert(hasCompletionKeyPathComponent());
    size_t ComponentIndex = 0;
    auto Components =
        getKeyPathContainingCompletionComponent()->getComponents();
    for (auto &Component : Components) {
      if (Component.getKind() == KeyPathExpr::Component::Kind::CodeCompletion) {
        break;
      } else {
        ComponentIndex++;
      }
    }
    assert(ComponentIndex < Components.size() &&
           "No completion component in the key path?");
    return ComponentIndex;
  }

  struct Fallback {
    Expr *E; ///< The fallback expression.
    DeclContext *DC; ///< The fallback expression's decl context.
    bool SeparatePrecheck; ///< True if the fallback may require prechecking.
  };

  /// As a fallback sometimes its useful to not only type-check
  /// code completion expression directly but instead add some
  /// of the enclosing context e.g. when completion is an argument
  /// to a call.
  Optional<Fallback> getFallbackCompletionExpr() const {
    if (!hasCompletionExpr()) {
      // Creating a fallback expression only makes sense if we are completing in
      // an expression, not when we're completing in a key path.
      return None;
    }

    Optional<Fallback> fallback;
    bool separatePrecheck = false;
    DeclContext *fallbackDC = InitialDC;

    // Find the outermost fallback expression within the innermost error
    // expression or multi-statement closure, keeping track of its decl context.
    for (auto context: Contexts) {
      switch (context.Kind) {
      case ContextKind::StringInterpolation:
        LLVM_FALLTHROUGH;
      case ContextKind::FallbackExpression:
        if (!fallback && context.E != InitialExpr)
          fallback = Fallback{context.E, fallbackDC, separatePrecheck};
        continue;

      case ContextKind::SingleStmtClosure:
        if (!fallback && context.E != InitialExpr)
          fallback = Fallback{context.E, fallbackDC, separatePrecheck};
        fallbackDC = cast<AbstractClosureExpr>(context.E);
        continue;

      case ContextKind::MultiStmtClosure:
        fallbackDC = cast<AbstractClosureExpr>(context.E);
        LLVM_FALLTHROUGH;
      case ContextKind::ErrorExpression:;
        fallback = None;
        separatePrecheck = true;
        continue;
      }
    }

    if (fallback)
      return fallback;

    if (getCompletionExpr()->getBase() && getCompletionExpr() != InitialExpr)
      return Fallback{getCompletionExpr(), fallbackDC, separatePrecheck};
    return None;
  }

private:
  bool hasContext(ContextKind kind) const {
    return llvm::find_if(Contexts, [&kind](const Context &currContext) {
             return currContext.Kind == kind;
           }) != Contexts.end();
  }
};

} // end namespace

/// Remove any solutions from the provided vector that both require fixes and
/// have a score worse than the best.
static void filterSolutions(SolutionApplicationTarget &target,
                            SmallVectorImpl<Solution> &solutions,
                            CodeCompletionExpr *completionExpr) {
  if (solutions.size() <= 1)
    return;

  Score minScore = std::min_element(solutions.begin(), solutions.end(),
                                    [](const Solution &a, const Solution &b) {
    return a.getFixedScore() < b.getFixedScore();
  })->getFixedScore();

  llvm::erase_if(solutions, [&](const Solution &S) {
    return S.getFixedScore().Data[SK_Fix] != 0 &&
        S.getFixedScore() > minScore;
  });
}

bool TypeChecker::typeCheckForCodeCompletion(
    SolutionApplicationTarget &target, bool needsPrecheck,
    llvm::function_ref<void(const Solution &)> callback) {
  auto *DC = target.getDeclContext();
  auto &Context = DC->getASTContext();

  auto *expr = target.getAsExpr();
  if (!expr)
    return false;

  // First of all, let's check whether given target expression
  // does indeed have the code completion location in it.
  {
    auto range = expr->getSourceRange();
    if (range.isInvalid() ||
        !Context.SourceMgr.rangeContainsCodeCompletionLoc(range))
      return false;
  }

  FrontendStatsTracer StatsTracer(Context.Stats,
                                  "typecheck-for-code-completion", expr);
  PrettyStackTraceExpr stackTrace(Context, "code-completion", expr);

  expr = expr->walk(SanitizeExpr(Context,
                                 /*shouldReusePrecheckedType=*/false));
  target.setExpr(expr);

  CompletionContextFinder contextAnalyzer(expr, DC);

  // If there was no completion expr (e.g. if the code completion location was
  // among tokens that were skipped over during parser error recovery) bail.
  if (!contextAnalyzer.hasCompletionExpr() &&
      !contextAnalyzer.hasCompletionKeyPathComponent())
    return false;

  // Interpolation components are type-checked separately.
  if (contextAnalyzer.locatedInStringIterpolation())
    return false;

  // FIXME: There is currently no way to distinguish between
  // multi-statement closures which are result builder bodies
  // (that are type-checked together with enclosing context)
  // and regular closures which are type-checked separately.

  if (needsPrecheck) {
    // First, pre-check the expression, validating any types that occur in the
    // expression and folding sequence expressions.
    auto failedPreCheck = ConstraintSystem::preCheckExpression(
        expr, DC,
        /*replaceInvalidRefsWithErrors=*/true,
        /*leaveClosureBodiesUnchecked=*/true);

    target.setExpr(expr);

    if (failedPreCheck)
      return false;
  }

  enum class CompletionResult { Ok, NotApplicable, Fallback };

  auto solveForCodeCompletion =
      [&](SolutionApplicationTarget &target) -> CompletionResult {
    ConstraintSystemOptions options;
    options |= ConstraintSystemFlags::AllowFixes;
    options |= ConstraintSystemFlags::SuppressDiagnostics;
    options |= ConstraintSystemFlags::ForCodeCompletion;
    options |= ConstraintSystemFlags::LeaveClosureBodyUnchecked;


    ConstraintSystem cs(DC, options);

    llvm::SmallVector<Solution, 4> solutions;

    // If solve failed to generate constraints or with some other
    // issue, we need to fallback to type-checking a sub-expression.
    if (!cs.solveForCodeCompletion(target, solutions))
      return CompletionResult::Fallback;

    // FIXME: instead of filtering, expose the score and viability to clients.
    // Remove any solutions that both require fixes and have a score that is
    // worse than the best.
    CodeCompletionExpr *completionExpr = nullptr;
    if (contextAnalyzer.hasCompletionExpr()) {
      completionExpr = contextAnalyzer.getCompletionExpr();
    }
    filterSolutions(target, solutions, completionExpr);

    // Similarly, if the type-check didn't produce any solutions, fall back
    // to type-checking a sub-expression in isolation.
    if (solutions.empty())
      return CompletionResult::Fallback;

    // If code completion expression resides inside of multi-statement
    // closure body it could either be type-checked together with the context
    // or not, it's impossible to say without checking.
    if (contextAnalyzer.locatedInMultiStmtClosure()) {
      auto &solution = solutions.front();

      bool HasTypeForCompletionNode = false;
      if (completionExpr) {
        HasTypeForCompletionNode = solution.hasType(completionExpr);
      } else {
        assert(contextAnalyzer.hasCompletionKeyPathComponent());
        HasTypeForCompletionNode = solution.hasType(
            contextAnalyzer.getKeyPathContainingCompletionComponent(),
            contextAnalyzer.getKeyPathCompletionComponentIndex());
      }

      if (!HasTypeForCompletionNode) {
        // At this point we know the code completion node wasn't checked with
        // the closure's surrounding context, so can defer to regular
        // type-checking for the current call to typeCheckExpression. If that
        // succeeds we will get a second call to typeCheckExpression for the
        // body of the closure later and can gather completions then. If it
        // doesn't we rely on the fallback typechecking in the subclasses of
        // TypeCheckCompletionCallback that considers in isolation a
        // sub-expression of the closure that contains the completion location.
        return CompletionResult::NotApplicable;
      }
    }

    llvm::for_each(solutions, callback);
    return CompletionResult::Ok;
  };

  switch (solveForCodeCompletion(target)) {
  case CompletionResult::Ok:
    return true;

  case CompletionResult::NotApplicable:
    return false;

  case CompletionResult::Fallback:
    break;
  }

  // Determine the best subexpression to use based on the collected context
  // of the code completion expression.
  if (auto fallback = contextAnalyzer.getFallbackCompletionExpr()) {
    assert(fallback->E != expr);
    SolutionApplicationTarget completionTarget(fallback->E,
                                               fallback->DC, CTP_Unused,
                                               /*contextualType=*/Type(),
                                               /*isDiscarded=*/true);
    typeCheckForCodeCompletion(completionTarget, fallback->SeparatePrecheck,
                               callback);
  }
  return true;
}

static Optional<Type> getTypeOfCompletionContextExpr(
                        DeclContext *DC,
                        CompletionTypeCheckKind kind,
                        Expr *&parsedExpr,
                        ConcreteDeclRef &referencedDecl) {
  if (constraints::ConstraintSystem::preCheckExpression(
          parsedExpr, DC,
          /*replaceInvalidRefsWithErrors=*/true,
          /*leaveClosureBodiesUnchecked=*/true))
    return None;

  switch (kind) {
  case CompletionTypeCheckKind::Normal:
    // Handle below.
    break;

  case CompletionTypeCheckKind::KeyPath:
    referencedDecl = nullptr;
    if (auto keyPath = dyn_cast<KeyPathExpr>(parsedExpr)) {
      auto components = keyPath->getComponents();
      if (!components.empty()) {
        auto &last = components.back();
        if (last.isResolved()) {
          if (last.getKind() == KeyPathExpr::Component::Kind::Property)
            referencedDecl = last.getDeclRef();
          Type lookupTy = last.getComponentType();
          ASTContext &Ctx = DC->getASTContext();
          if (auto bridgedClass = Ctx.getBridgedToObjC(DC, lookupTy))
            return bridgedClass;
          return lookupTy;
        }
      }
    }

    return None;
  }

  Type originalType = parsedExpr->getType();
  if (auto T = getTypeOfExpressionWithoutApplying(parsedExpr, DC,
                 referencedDecl, FreeTypeVariableBinding::UnresolvedType))
    return T;

  // Try to recover if we've made any progress.
  if (parsedExpr &&
      !isa<ErrorExpr>(parsedExpr) &&
      parsedExpr->getType() &&
      !parsedExpr->getType()->hasError() &&
      (originalType.isNull() ||
       !parsedExpr->getType()->isEqual(originalType))) {
    return parsedExpr->getType();
  }

  return None;
}

/// Return the type of an expression parsed during code completion, or
/// a null \c Type on error.
Optional<Type> swift::getTypeOfCompletionContextExpr(
                        ASTContext &Ctx,
                        DeclContext *DC,
                        CompletionTypeCheckKind kind,
                        Expr *&parsedExpr,
                        ConcreteDeclRef &referencedDecl) {
  DiagnosticSuppression suppression(Ctx.Diags);

  // Try to solve for the actual type of the expression.
  return ::getTypeOfCompletionContextExpr(DC, kind, parsedExpr,
                                          referencedDecl);
}

/// Return the type of operator function for specified LHS, or a null
/// \c Type on error.
FunctionType *
swift::getTypeOfCompletionOperator(DeclContext *DC, Expr *LHS,
                                   Identifier opName, DeclRefKind refKind,
                                   ConcreteDeclRef &referencedDecl) {
  auto &ctx = DC->getASTContext();
  DiagnosticSuppression suppression(ctx.Diags);
  return TypeChecker::getTypeOfCompletionOperator(DC, LHS, opName, refKind,
                                                  referencedDecl);
}

bool swift::typeCheckExpression(DeclContext *DC, Expr *&parsedExpr) {
  auto &ctx = DC->getASTContext();

  parsedExpr = parsedExpr->walk(SanitizeExpr(ctx, /*shouldReusePrecheckedType=*/false));

  DiagnosticSuppression suppression(ctx.Diags);
  auto resultTy = TypeChecker::typeCheckExpression(
      parsedExpr, DC,
      /*contextualInfo=*/{}, TypeCheckExprFlags::LeaveClosureBodyUnchecked);
  return !resultTy;
}

LookupResult
swift::lookupSemanticMember(DeclContext *DC, Type ty, DeclName name) {
  return TypeChecker::lookupMember(DC, ty, DeclNameRef(name), None);
}

void DotExprTypeCheckCompletionCallback::fallbackTypeCheck() {
  assert(!gotCallback());

  // Default to checking the completion expression in isolation.
  Expr *fallbackExpr = CompletionExpr;
  DeclContext *fallbackDC = DC;

  CompletionContextFinder finder(DC);
  if (finder.hasCompletionExpr()) {
    if (auto fallback = finder.getFallbackCompletionExpr()) {
      fallbackExpr = fallback->E;
      fallbackDC = fallback->DC;
    }
  }

  SolutionApplicationTarget completionTarget(fallbackExpr, fallbackDC,
                                             CTP_Unused, Type(),
                                             /*isDiscared=*/true);

  TypeChecker::typeCheckForCodeCompletion(
      completionTarget, /*needsPrecheck*/true,
      [&](const Solution &S) { sawSolution(S); });
}

void UnresolvedMemberTypeCheckCompletionCallback::
fallbackTypeCheck(DeclContext *DC) {
  assert(!gotCallback());

  CompletionContextFinder finder(DC);
  if (!finder.hasCompletionExpr())
    return;

  auto fallback = finder.getFallbackCompletionExpr();
  if (!fallback)
    return;


  SolutionApplicationTarget completionTarget(fallback->E, fallback->DC,
                                             CTP_Unused, Type(),
                                             /*isDiscared=*/true);
  TypeChecker::typeCheckForCodeCompletion(
      completionTarget, /*needsPrecheck*/true,
      [&](const Solution &S) { sawSolution(S); });
}

static Type getTypeForCompletion(const constraints::Solution &S, Expr *E) {
  if (!S.hasType(E)) {
    assert(false && "Expression wasn't type checked?");
    return nullptr;
  }

  auto &CS = S.getConstraintSystem();

  // To aid code completion, we need to attempt to convert type placeholders
  // back into underlying generic parameters if possible, since type
  // of the code completion expression is used as "expected" (or contextual)
  // type so it's helpful to know what requirements it has to filter
  // the list of possible member candidates e.g.
  //
  // \code
  // func test<T: P>(_: [T]) {}
  //
  // test(42.#^MEMBERS^#)
  // \code
  //
  // It's impossible to resolve `T` in this case but code completion
  // expression should still have a type of `[T]` instead of `[<<hole>>]`
  // because it helps to produce correct contextual member list based on
  // a conformance requirement associated with generic parameter `T`.
  if (isa<CodeCompletionExpr>(E)) {
    auto completionTy = S.getType(E).transform([&](Type type) -> Type {
      if (auto *typeVar = type->getAs<TypeVariableType>())
        return S.getFixedType(typeVar);
      return type;
    });

    return S.simplifyType(completionTy.transform([&](Type type) {
      if (auto *placeholder = type->getAs<PlaceholderType>()) {
        if (auto *typeVar =
                placeholder->getOriginator().dyn_cast<TypeVariableType *>()) {
          if (auto *GP = typeVar->getImpl().getGenericParameter()) {
            // Code completion depends on generic parameter type being
            // represented in terms of `ArchetypeType` since it's easy
            // to extract protocol requirements from it.
            if (auto *GPD = GP->getDecl())
              return GPD->getInnermostDeclContext()->mapTypeIntoContext(GP);
          }
        }

        return Type(CS.getASTContext().TheUnresolvedType);
      }

      return type;
    }));
  }

  return S.getResolvedType(E);
}

/// Whether the given completion expression is the only expression in its
/// containing closure or function body and its value is implicitly returned.
///
/// If these conditions are met, code completion needs to avoid penalizing
/// completion results that don't match the expected return type when computing
/// type relations, as since no return statement was explicitly written by the
/// user, it's possible they intend the single expression not as the return
/// value but merely the first entry in a multi-statement body they just haven't
/// finished writing yet.
static bool isImplicitSingleExpressionReturn(ConstraintSystem &CS,
                                             Expr *CompletionExpr) {
  Expr *ParentExpr = CS.getParentExpr(CompletionExpr);
  if (!ParentExpr)
    return CS.getContextualTypePurpose(CompletionExpr) == CTP_ReturnSingleExpr;

  if (auto *ParentCE = dyn_cast<ClosureExpr>(ParentExpr)) {
    if (ParentCE->hasSingleExpressionBody() &&
        ParentCE->getSingleExpressionBody() == CompletionExpr) {
      ASTNode Last = ParentCE->getBody()->getLastElement();
      return !Last.isStmt(StmtKind::Return) || Last.isImplicit();
    }
  }
  return false;
}

void DotExprTypeCheckCompletionCallback::
sawSolution(const constraints::Solution &S) {
  GotCallback = true;
  auto &CS = S.getConstraintSystem();
  auto *ParsedExpr = CompletionExpr->getBase();
  auto *SemanticExpr = ParsedExpr->getSemanticsProvidingExpr();

  auto BaseTy = getTypeForCompletion(S, ParsedExpr);
  // If base type couldn't be determined (e.g. because base expression
  // is an invalid reference), let's not attempt to do a lookup since
  // it wouldn't produce any useful results anyway.
  if (!BaseTy || BaseTy->getRValueType()->is<UnresolvedType>())
    return;

  auto *Locator = CS.getConstraintLocator(SemanticExpr);
  Type ExpectedTy = getTypeForCompletion(S, CompletionExpr);
  Expr *ParentExpr = CS.getParentExpr(CompletionExpr);
  if (!ParentExpr)
    ExpectedTy = CS.getContextualType(CompletionExpr, /*forConstraint=*/false);

  auto *CalleeLocator = S.getCalleeLocator(Locator);
  ValueDecl *ReferencedDecl = nullptr;
  if (auto SelectedOverload = S.getOverloadChoiceIfAvailable(CalleeLocator))
    ReferencedDecl = SelectedOverload->choice.getDeclOrNull();

  auto Key = std::make_pair(BaseTy, ReferencedDecl);
  auto Ret = BaseToSolutionIdx.insert({Key, Results.size()});
  if (Ret.second) {
    bool ISDMT = S.isStaticallyDerivedMetatype(ParsedExpr);
    bool ImplicitReturn = isImplicitSingleExpressionReturn(CS, CompletionExpr);
    bool DisallowVoid = ExpectedTy
                            ? !ExpectedTy->isVoid()
                            : !ParentExpr && CS.getContextualTypePurpose(
                                                 CompletionExpr) != CTP_Unused;

    Results.push_back(
        {BaseTy, ReferencedDecl, {}, DisallowVoid, ISDMT, ImplicitReturn});
    if (ExpectedTy)
      Results.back().ExpectedTypes.push_back(ExpectedTy);
  } else if (ExpectedTy) {
    auto &ExpectedTys = Results[Ret.first->getSecond()].ExpectedTypes;
    auto IsEqual = [&](Type Ty) { return ExpectedTy->isEqual(Ty); };
    if (!llvm::any_of(ExpectedTys, IsEqual))
      ExpectedTys.push_back(ExpectedTy);
  }
}

/// If the code completion variable occurs in a pattern matching position, we
/// have an AST that looks like this.
/// \code
/// (binary_expr implicit type='$T3'
///   (overloaded_decl_ref_expr function_ref=compound decls=[
///     Swift.(file).~=,
///     Swift.(file).Optional extension.~=])
///   (argument_list implicit
///     (argument
///       (code_completion_expr implicit type='$T1'))
///     (argument
///       (declref_expr implicit decl=swift_ide_test.(file).foo(x:).$match))))
/// \endcode
/// If the code completion expression occurs in such an AST, return the
/// declaration of the \c $match variable, otherwise return \c nullptr.
VarDecl *getMatchVarIfInPatternMatch(CodeCompletionExpr *CompletionExpr,
                                     ConstraintSystem &CS) {
  auto &Context = CS.getASTContext();

  auto *Binary = dyn_cast_or_null<BinaryExpr>(CS.getParentExpr(CompletionExpr));
  if (!Binary || !Binary->isImplicit() || Binary->getLHS() != CompletionExpr) {
    return nullptr;
  }

  auto CalledOperator = Binary->getFn();
  if (!CalledOperator || !CalledOperator->isImplicit()) {
    return nullptr;
  }
  // The reference to the ~= operator might be an OverloadedDeclRefExpr or a
  // DeclRefExpr, depending on how many ~= operators are viable.
  if (auto Overloaded =
          dyn_cast_or_null<OverloadedDeclRefExpr>(CalledOperator)) {
    if (!llvm::all_of(Overloaded->getDecls(), [&Context](ValueDecl *D) {
          return D->getBaseName() == Context.Id_MatchOperator;
        })) {
      return nullptr;
    }
  } else if (auto Ref = dyn_cast_or_null<DeclRefExpr>(CalledOperator)) {
    if (Ref->getDecl()->getBaseName() != Context.Id_MatchOperator) {
      return nullptr;
    }
  } else {
    return nullptr;
  }

  auto MatchArg = dyn_cast_or_null<DeclRefExpr>(Binary->getRHS());
  if (!MatchArg || !MatchArg->isImplicit()) {
    return nullptr;
  }

  auto MatchVar = MatchArg->getDecl();
  if (MatchVar && MatchVar->isImplicit() &&
      MatchVar->getBaseName() == Context.Id_PatternMatchVar) {
    return dyn_cast<VarDecl>(MatchVar);
  } else {
    return nullptr;
  }
}

void UnresolvedMemberTypeCheckCompletionCallback::
sawSolution(const constraints::Solution &S) {
  GotCallback = true;

  auto &CS = S.getConstraintSystem();
  Type ExpectedTy = getTypeForCompletion(S, CompletionExpr);
  // If the type couldn't be determined (e.g. because there isn't any context
  // to derive it from), let's not attempt to do a lookup since it wouldn't
  // produce any useful results anyway.
  if (ExpectedTy && !ExpectedTy->is<UnresolvedType>()) {
    // If ExpectedTy is a duplicate of any other result, ignore this solution.
    if (!llvm::any_of(ExprResults, [&](const ExprResult &R) {
          return R.ExpectedTy->isEqual(ExpectedTy);
        })) {
      bool SingleExprBody =
          isImplicitSingleExpressionReturn(CS, CompletionExpr);
      ExprResults.push_back({ExpectedTy, SingleExprBody});
    }
  }

  if (auto MatchVar = getMatchVarIfInPatternMatch(CompletionExpr, CS)) {
    Type MatchVarType;
    // If the MatchVar has an explicit type, it's not part of the solution. But
    // we can look it up in the constraint system directly.
    if (auto T = S.getConstraintSystem().getVarType(MatchVar)) {
      MatchVarType = T;
    } else {
      MatchVarType = S.getResolvedType(MatchVar);
    }
    if (MatchVarType && !MatchVarType->is<UnresolvedType>()) {
      if (!llvm::any_of(EnumPatternTypes, [&](const Type &R) {
            return R->isEqual(MatchVarType);
          })) {
        EnumPatternTypes.push_back(MatchVarType);
      }
    }
  }
}

void KeyPathTypeCheckCompletionCallback::sawSolution(
    const constraints::Solution &S) {
  // Determine the code completion.
  size_t ComponentIndex = 0;
  for (auto &Component : KeyPath->getComponents()) {
    if (Component.getKind() == KeyPathExpr::Component::Kind::CodeCompletion) {
      break;
    } else {
      ComponentIndex++;
    }
  }
  assert(ComponentIndex < KeyPath->getComponents().size() &&
         "Didn't find a code compleiton component?");

  Type BaseType;
  if (ComponentIndex == 0) {
    // We are completing on the root and need to extract the key path's root
    // type.
    if (KeyPath->getRootType()) {
      BaseType = S.getResolvedType(KeyPath->getRootType());
    } else {
      // The key path doesn't have a root TypeRepr set, so we can't look the key
      // path's root up through it. Build a constraint locator and look the
      // root type up through it.
      // FIXME: Improve the linear search over S.typeBindings when it's possible
      // to look up type variables by their locators.
      auto RootLocator =
          S.getConstraintLocator(KeyPath, {ConstraintLocator::KeyPathRoot});
      auto BaseVariableTypeBinding =
          llvm::find_if(S.typeBindings, [&RootLocator](const auto &Entry) {
            return Entry.first->getImpl().getLocator() == RootLocator;
          });
      if (BaseVariableTypeBinding != S.typeBindings.end()) {
        BaseType = S.simplifyType(BaseVariableTypeBinding->getSecond());
      }
    }
  } else {
    // We are completing after a component. Get the previous component's result
    // type.
    BaseType = S.simplifyType(S.getType(KeyPath, ComponentIndex - 1));
  }
  if (BaseType.isNull()) {
    return;
  }

  // If ExpectedTy is a duplicate of any other result, ignore this solution.
  if (llvm::any_of(Results, [&](const Result &R) {
    return R.BaseType->isEqual(BaseType);
  })) {
    return;
  }
  Results.push_back({BaseType, /*OnRoot=*/(ComponentIndex == 0)});
}
