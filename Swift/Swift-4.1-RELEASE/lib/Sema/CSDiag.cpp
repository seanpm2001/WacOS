//===--- CSDiag.cpp - Constraint Diagnostics ------------------------------===//
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
// This file implements diagnostics for the type checker.
//
//===----------------------------------------------------------------------===//

#include "ConstraintSystem.h"
#include "CSDiag.h"
#include "CalleeCandidateInfo.h"
#include "MiscDiagnostics.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Initializer.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/TypeWalker.h"
#include "swift/AST/TypeMatcher.h"
#include "swift/Basic/Defer.h"
#include "swift/Basic/StringExtras.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace swift;
using namespace constraints;

namespace swift {
  std::string getTypeListString(Type type) {
    std::string result;
    
    // Always make sure to have at least one set of parens
    bool forceParens =
    !type->is<TupleType>() && !isa<ParenType>(type.getPointer());
    if (forceParens)
      result.push_back('(');
    
    llvm::raw_string_ostream OS(result);
    type->print(OS);
    OS.flush();
    
    if (forceParens)
      result.push_back(')');
    
    return result;
  }

  Type replaceTypeParametersWithUnresolved(Type ty) {
    if (!ty) return ty;
    
    if (!ty->hasTypeParameter() && !ty->hasArchetype()) return ty;
    
    auto &ctx = ty->getASTContext();
    
    return ty.transform([&](Type type) -> Type {
      if (type->is<ArchetypeType>() ||
          type->isTypeParameter())
        return ctx.TheUnresolvedType;
      return type;
    });
  }

  Type replaceTypeVariablesWithUnresolved(Type ty) {
    if (!ty) return ty;
    
    if (!ty->hasTypeVariable()) return ty;
    
    auto &ctx = ty->getASTContext();
    
    return ty.transform([&](Type type) -> Type {
      if (type->isTypeVariableOrMember())
        return ctx.TheUnresolvedType;
      return type;
    });
  }
};

static bool isUnresolvedOrTypeVarType(Type ty) {
  return ty->isTypeVariableOrMember() || ty->is<UnresolvedType>();
}

/// Given a subpath of an old locator, compute its summary flags.
static unsigned recomputeSummaryFlags(ConstraintLocator *oldLocator,
                                      ArrayRef<LocatorPathElt> path) {
  if (oldLocator->getSummaryFlags() != 0)
    return ConstraintLocator::getSummaryFlagsForPath(path);
  return 0;
}

ConstraintLocator *
constraints::simplifyLocator(ConstraintSystem &cs, ConstraintLocator *locator,
                             SourceRange &range,
                             ConstraintLocator **targetLocator) {
  // Clear out the target locator result.
  if (targetLocator)
    *targetLocator = nullptr;

  // The path to be tacked on to the target locator to identify the specific
  // target.
  Expr *targetAnchor;
  SmallVector<LocatorPathElt, 4> targetPath;

  auto path = locator->getPath();
  auto anchor = locator->getAnchor();
  simplifyLocator(anchor, path, targetAnchor, targetPath, range);


  // If we have a target anchor, build and simplify the target locator.
  if (targetLocator && targetAnchor) {
    SourceRange targetRange;
    unsigned targetFlags = recomputeSummaryFlags(locator, targetPath);
    auto loc = cs.getConstraintLocator(targetAnchor, targetPath, targetFlags);
    *targetLocator = simplifyLocator(cs, loc, targetRange);
  }

  // If we didn't simplify anything, just return the input.
  if (anchor == locator->getAnchor() &&
      path.size() == locator->getPath().size()) {
    return locator;
  }

  // Recompute the summary flags if we had any to begin with.  This is
  // necessary because we might remove e.g. tuple elements from the path.
  unsigned summaryFlags = recomputeSummaryFlags(locator, path);
  return cs.getConstraintLocator(anchor, path, summaryFlags);
}

void constraints::simplifyLocator(Expr *&anchor,
                                  ArrayRef<LocatorPathElt> &path,
                                  Expr *&targetAnchor,
                                  SmallVectorImpl<LocatorPathElt> &targetPath,
                                  SourceRange &range) {
  range = SourceRange();
  targetAnchor = nullptr;

  while (!path.empty()) {
    switch (path[0].getKind()) {
    case ConstraintLocator::ApplyArgument:
      // Extract application argument.
      if (auto applyExpr = dyn_cast<ApplyExpr>(anchor)) {
        // The target anchor is the function being called.
        targetAnchor = applyExpr->getFn();
        targetPath.push_back(path[0]);

        anchor = applyExpr->getArg();
        path = path.slice(1);
        continue;
      }

      if (auto objectLiteralExpr = dyn_cast<ObjectLiteralExpr>(anchor)) {
        targetAnchor = nullptr;
        targetPath.clear();

        anchor = objectLiteralExpr->getArg();
        path = path.slice(1);
        continue;
      }
      break;

    case ConstraintLocator::ApplyFunction:
      // Extract application function.
      if (auto applyExpr = dyn_cast<ApplyExpr>(anchor)) {
        // No additional target locator information.
        targetAnchor = nullptr;
        targetPath.clear();

        anchor = applyExpr->getFn();
        path = path.slice(1);
        continue;
      }

      // The unresolved member itself is the function.
      if (auto unresolvedMember = dyn_cast<UnresolvedMemberExpr>(anchor)) {
        if (unresolvedMember->getArgument()) {
          // No additional target locator information.
          targetAnchor = nullptr;
          targetPath.clear();

          anchor = unresolvedMember;
          path = path.slice(1);
          continue;
        }
      }

      break;

    case ConstraintLocator::Load:
    case ConstraintLocator::RvalueAdjustment:
    case ConstraintLocator::ScalarToTuple:
    case ConstraintLocator::UnresolvedMember:
      // Loads, rvalue adjustment, and scalar-to-tuple conversions are implicit.
      path = path.slice(1);
      continue;

    case ConstraintLocator::NamedTupleElement:
    case ConstraintLocator::TupleElement:
      // Extract tuple element.
      if (auto tupleExpr = dyn_cast<TupleExpr>(anchor)) {
        unsigned index = path[0].getValue();
        if (index < tupleExpr->getNumElements()) {
          // Append this extraction to the target locator path.
          if (targetAnchor) {
            targetPath.push_back(path[0]);
          }

          anchor = tupleExpr->getElement(index);
          path = path.slice(1);
          continue;
        }
      }
      break;

    case ConstraintLocator::ApplyArgToParam:
      // Extract tuple element.
      if (auto tupleExpr = dyn_cast<TupleExpr>(anchor)) {
        unsigned index = path[0].getValue();
        if (index < tupleExpr->getNumElements()) {
          // Append this extraction to the target locator path.
          if (targetAnchor) {
            targetPath.push_back(path[0]);
          }

          anchor = tupleExpr->getElement(index);
          path = path.slice(1);
          continue;
        }
      }

      // Extract subexpression in parentheses.
      if (auto parenExpr = dyn_cast<ParenExpr>(anchor)) {
        assert(path[0].getValue() == 0);

        // Append this extraction to the target locator path.
        if (targetAnchor) {
          targetPath.push_back(path[0]);
        }

        anchor = parenExpr->getSubExpr();
        path = path.slice(1);
        continue;
      }
      break;

    case ConstraintLocator::ConstructorMember:
      if (auto typeExpr = dyn_cast<TypeExpr>(anchor)) {
        // This is really an implicit 'init' MemberRef, so point at the base,
        // i.e. the TypeExpr.
        targetAnchor = nullptr;
        targetPath.clear();
        range = SourceRange();
        anchor = typeExpr;
        path = path.slice(1);
        continue;
      }
      LLVM_FALLTHROUGH;

    case ConstraintLocator::Member:
    case ConstraintLocator::MemberRefBase:
      if (auto UDE = dyn_cast<UnresolvedDotExpr>(anchor)) {
        // No additional target locator information.
        targetAnchor = nullptr;
        targetPath.clear();
        
        range = UDE->getNameLoc().getSourceRange();
        anchor = UDE->getBase();
        path = path.slice(1);
        continue;
      }
      break;

    case ConstraintLocator::SubscriptIndex:
      if (auto subscript = dyn_cast<SubscriptExpr>(anchor)) {
        targetAnchor = subscript->getBase();
        targetPath.clear();

        anchor = subscript->getIndex();
        path = path.slice(1);
        continue;
      }
      break;
    case ConstraintLocator::SubscriptMember:
      if (isa<SubscriptExpr>(anchor)) {
        targetAnchor = nullptr;
        targetPath.clear();
        path = path.slice(1);
        continue;
      }
      break;

    case ConstraintLocator::ClosureResult:
      if (auto CE = dyn_cast<ClosureExpr>(anchor)) {
        if (CE->hasSingleExpressionBody()) {
          targetAnchor = nullptr;
          targetPath.clear();
          anchor = CE->getSingleExpressionBody();
          path = path.slice(1);
          continue;
        }
      }
      break;
        
    default:
      // FIXME: Lots of other cases to handle.
      break;
    }

    // If we get here, we couldn't simplify the path further.
    break;
  }
}

/// Simplify the given locator down to a specific anchor expression,
/// if possible.
///
/// \returns the anchor expression if it fully describes the locator, or
/// null otherwise.
static Expr *simplifyLocatorToAnchor(ConstraintSystem &cs,
                                     ConstraintLocator *locator) {
  if (!locator || !locator->getAnchor())
    return nullptr;

  SourceRange range;
  locator = simplifyLocator(cs, locator, range);
  if (!locator->getAnchor() || !locator->getPath().empty())
    return nullptr;

  return locator->getAnchor();
}




/// \brief Determine the number of distinct overload choices in the
/// provided set.
static unsigned countDistinctOverloads(ArrayRef<OverloadChoice> choices) {
  llvm::SmallPtrSet<void *, 4> uniqueChoices;
  unsigned result = 0;
  for (auto choice : choices) {
    if (uniqueChoices.insert(choice.getOpaqueChoiceSimple()).second)
      ++result;
  }
  return result;
}

/// \brief Determine the name of the overload in a set of overload choices.
static DeclName getOverloadChoiceName(ArrayRef<OverloadChoice> choices) {
  DeclName name;
  for (auto choice : choices) {
    if (!choice.isDecl())
      continue;

    DeclName nextName = choice.getDecl()->getFullName();
    if (!name) {
      name = nextName;
      continue;
    }

    if (name != nextName) {
      // Assume all choices have the same base name and only differ in
      // argument labels. This may not be a great assumption, but we don't
      // really have a way to recover for diagnostics otherwise.
      return name.getBaseName();
    }
  }

  return name;
}

/// Returns true if any diagnostics were emitted.
static bool
tryDiagnoseTrailingClosureAmbiguity(TypeChecker &tc,
                                    const Expr *expr,
                                    const Expr *anchor,
                                    ArrayRef<OverloadChoice> choices) {
  auto *callExpr = dyn_cast<CallExpr>(expr);
  if (!callExpr)
    return false;
  if (!callExpr->hasTrailingClosure())
    return false;
  if (callExpr->getFn() != anchor)
    return false;

  llvm::SmallMapVector<Identifier, const ValueDecl *, 8> choicesByLabel;
  for (const OverloadChoice &choice : choices) {
    auto *callee = dyn_cast<AbstractFunctionDecl>(choice.getDecl());
    if (!callee)
      return false;

    const ParameterList *paramList = callee->getParameterLists().back();
    const ParamDecl *param = paramList->getArray().back();

    // Sanity-check that the trailing closure corresponds to this parameter.
    if (!param->getInterfaceType()->is<AnyFunctionType>())
      return false;

    Identifier trailingClosureLabel = param->getArgumentName();
    auto &choiceForLabel = choicesByLabel[trailingClosureLabel];

    // FIXME: Cargo-culted from diagnoseAmbiguity: apparently the same decl can
    // appear more than once?
    if (choiceForLabel == callee)
      continue;

    // If just providing the trailing closure label won't solve the ambiguity,
    // don't bother offering the fix-it.
    if (choiceForLabel != nullptr)
      return false;

    choiceForLabel = callee;
  }

  // If we got here, then all of the choices have unique labels. Offer them in
  // order.
  for (const auto &choicePair : choicesByLabel) {
    auto diag = tc.diagnose(expr->getLoc(),
                            diag::ambiguous_because_of_trailing_closure,
                            choicePair.first.empty(),
                            choicePair.second->getFullName());
    swift::fixItEncloseTrailingClosure(tc, diag, callExpr, choicePair.first);
  }

  return true;
}

static bool diagnoseAmbiguity(ConstraintSystem &cs,
                              ArrayRef<Solution> solutions,
                              Expr *expr) {
  // Produce a diff of the solutions.
  SolutionDiff diff(solutions);

  // Find the locators which have the largest numbers of distinct overloads.
  Optional<unsigned> bestOverload;
  // Overloads are scored by lexicographical comparison of (# of distinct
  // overloads, depth, *reverse* of the index). N.B. - cannot be used for the
  // reversing: the score version of index == 0 should be > than that of 1, but
  // -0 == 0 < UINT_MAX == -1, whereas ~0 == UINT_MAX > UINT_MAX - 1 == ~1.
  auto score = [](unsigned distinctOverloads, unsigned depth, unsigned index) {
    return std::make_tuple(distinctOverloads, depth, ~index);
  };
  auto bestScore = score(0, 0, std::numeric_limits<unsigned>::max());

  // Get a map of expressions to their depths and post-order traversal indices.
  // Heuristically, all other things being equal, we should complain about the
  // ambiguous expression that (1) has the most overloads, (2) is deepest, or
  // (3) comes earliest in the expression.
  auto depthMap = expr->getDepthMap();
  auto indexMap = expr->getPreorderIndexMap();

  for (unsigned i = 0, n = diff.overloads.size(); i != n; ++i) {
    auto &overload = diff.overloads[i];

    // If we can't resolve the locator to an anchor expression with no path,
    // we can't diagnose this well.
    auto *anchor = simplifyLocatorToAnchor(cs, overload.locator);
    if (!anchor)
      continue;
    auto it = indexMap.find(anchor);
    if (it == indexMap.end())
      continue;
    unsigned index = it->second;
    it = depthMap.find(anchor);
    if (it == depthMap.end())
      continue;
    unsigned depth = it->second;

    // If we don't have a name to hang on to, it'll be hard to diagnose this
    // overload.
    if (!getOverloadChoiceName(overload.choices))
      continue;

    unsigned distinctOverloads = countDistinctOverloads(overload.choices);

    // We need at least two overloads to make this interesting.
    if (distinctOverloads < 2)
      continue;

    // If we have more distinct overload choices for this locator than for
    // prior locators, just keep this locator.
    auto thisScore = score(distinctOverloads, depth, index);
    if (thisScore > bestScore) {
      bestScore = thisScore;
      bestOverload = i;
      continue;
    }

    // We have better results. Ignore this one.
  }

  // FIXME: Should be able to pick the best locator, e.g., based on some
  // depth-first numbering of expressions.
  if (bestOverload) {
    auto &overload = diff.overloads[*bestOverload];
    auto name = getOverloadChoiceName(overload.choices);
    auto anchor = simplifyLocatorToAnchor(cs, overload.locator);

    // Emit the ambiguity diagnostic.
    auto &tc = cs.getTypeChecker();
    tc.diagnose(anchor->getLoc(),
                name.isOperator() ? diag::ambiguous_operator_ref
                                  : diag::ambiguous_decl_ref,
                name);

    if (tryDiagnoseTrailingClosureAmbiguity(tc, expr, anchor, overload.choices))
      return true;

    // Emit candidates.  Use a SmallPtrSet to make sure only emit a particular
    // candidate once.  FIXME: Why is one candidate getting into the overload
    // set multiple times? (See also tryDiagnoseTrailingClosureAmbiguity.)
    SmallPtrSet<Decl*, 8> EmittedDecls;
    for (auto choice : overload.choices) {
      switch (choice.getKind()) {
      case OverloadChoiceKind::Decl:
      case OverloadChoiceKind::DeclViaDynamic:
      case OverloadChoiceKind::DeclViaBridge:
      case OverloadChoiceKind::DeclViaUnwrappedOptional:
        // FIXME: show deduced types, etc, etc.
        if (EmittedDecls.insert(choice.getDecl()).second)
          tc.diagnose(choice.getDecl(), diag::found_candidate);
        break;

      case OverloadChoiceKind::KeyPathApplication:
        // Skip key path applications, since we don't want them to noise up
        // unrelated subscript diagnostics.
        break;

      case OverloadChoiceKind::BaseType:
      case OverloadChoiceKind::TupleIndex:
        // FIXME: Actually diagnose something here.
        break;
      }
    }

    return true;
  }

  // FIXME: If we inferred different types for literals (for example),
  // could diagnose ambiguity that way as well.

  return false;
}

/// Given an expression that has a non-lvalue type, dig into it until we find
/// the part of the expression that prevents the entire subexpression from being
/// mutable.  For example, in a sequence like "x.v.v = 42" we want to complain
/// about "x" being a let property if "v.v" are both mutable.
///
/// This returns the base subexpression that looks immutable (or that can't be
/// analyzed any further) along with a decl extracted from it if we could.
///
static std::pair<Expr*, ValueDecl*>
resolveImmutableBase(Expr *expr, ConstraintSystem &CS) {
  expr = expr->getValueProvidingExpr();

  // Provide specific diagnostics for assignment to subscripts whose base expr
  // is known to be an rvalue.
  if (auto *SE = dyn_cast<SubscriptExpr>(expr)) {
    // If we found a decl for the subscript, check to see if it is a set-only
    // subscript decl.
    SubscriptDecl *member = nullptr;
    if (SE->hasDecl())
      member = dyn_cast_or_null<SubscriptDecl>(SE->getDecl().getDecl());
    
    if (!member) {
      auto loc = CS.getConstraintLocator(SE,ConstraintLocator::SubscriptMember);
      member = dyn_cast_or_null<SubscriptDecl>(CS.findResolvedMemberRef(loc));
    }

    // If it isn't settable, return it.
    if (member) {
      if (!member->isSettable() ||
          !member->isSetterAccessibleFrom(CS.DC))
        return { expr, member };
    }

    // If it is settable, then the base must be the problem, recurse.
    return resolveImmutableBase(SE->getBase(), CS);
  }

  // Look through property references.
  if (auto *UDE = dyn_cast<UnresolvedDotExpr>(expr)) {
    // If we found a decl for the UDE, check it.
    auto loc = CS.getConstraintLocator(UDE, ConstraintLocator::Member);
    
    // If we can resolve a member, we can determine whether it is settable in
    // this context.
    if (auto *member = CS.findResolvedMemberRef(loc)) {
      auto *memberVD = dyn_cast<VarDecl>(member);
      
      // If the member isn't a vardecl (e.g. its a funcdecl), or it isn't
      // settable, then it is the problem: return it.
      if (!memberVD ||
          !member->isSettable(nullptr) ||
          !memberVD->isSetterAccessibleFrom(CS.DC))
        return { expr, member };
    }

    // If we weren't able to resolve a member or if it is mutable, then the
    // problem must be with the base, recurse.
    return resolveImmutableBase(UDE->getBase(), CS);
  }

  if (auto *MRE = dyn_cast<MemberRefExpr>(expr)) {
    // If the member isn't settable, then it is the problem: return it.
    if (auto member = dyn_cast<AbstractStorageDecl>(MRE->getMember().getDecl()))
      if (!member->isSettable(nullptr) ||
          !member->isSetterAccessibleFrom(CS.DC))
        return { expr, member };

    // If we weren't able to resolve a member or if it is mutable, then the
    // problem must be with the base, recurse.
    return resolveImmutableBase(MRE->getBase(), CS);
  }

  if (auto *DRE = dyn_cast<DeclRefExpr>(expr))
    return { expr, DRE->getDecl() };

  // Look through x!
  if (auto *FVE = dyn_cast<ForceValueExpr>(expr))
    return resolveImmutableBase(FVE->getSubExpr(), CS);
  
  // Look through x?
  if (auto *BOE = dyn_cast<BindOptionalExpr>(expr))
    return resolveImmutableBase(BOE->getSubExpr(), CS);
  
  // Look through implicit conversions
  if (auto *ICE = dyn_cast<ImplicitConversionExpr>(expr))
    if (!isa<LoadExpr>(ICE->getSubExpr()))
      return resolveImmutableBase(ICE->getSubExpr(), CS);

  return { expr, nullptr };
}

static bool isLoadedLValue(Expr *expr) {
  expr = expr->getSemanticsProvidingExpr();
  if (isa<LoadExpr>(expr))
    return true;
  if (auto ifExpr = dyn_cast<IfExpr>(expr))
    return isLoadedLValue(ifExpr->getThenExpr())
        && isLoadedLValue(ifExpr->getElseExpr());
  return false;
}

static void diagnoseSubElementFailure(Expr *destExpr,
                                      SourceLoc loc,
                                      ConstraintSystem &CS,
                                      Diag<StringRef> diagID,
                                      Diag<Type> unknownDiagID) {
  auto &TC = CS.getTypeChecker();
  
  // Walk through the destination expression, resolving what the problem is.  If
  // we find a node in the lvalue path that is problematic, this returns it.
  auto immInfo = resolveImmutableBase(destExpr, CS);

  // Otherwise, we cannot resolve this because the available setter candidates
  // are all mutating and the base must be mutating.  If we dug out a
  // problematic decl, we can produce a nice tailored diagnostic.
  if (auto *VD = dyn_cast_or_null<VarDecl>(immInfo.second)) {
    std::string message = "'";
    message += VD->getName().str().str();
    message += "'";

    if (VD->isCaptureList())
      message += " is an immutable capture";
    else if (VD->isImplicit())
      message += " is immutable";
    else if (VD->isLet())
      message += " is a 'let' constant";
    else if (!VD->isSettable(CS.DC))
      message += " is a get-only property";
    else if (!VD->isSetterAccessibleFrom(CS.DC))
      message += " setter is inaccessible";
    else {
      message += " is immutable";
    }
    TC.diagnose(loc, diagID, message)
      .highlight(immInfo.first->getSourceRange());

    // If this is a simple variable marked with a 'let', emit a note to fixit
    // hint it to 'var'.
    VD->emitLetToVarNoteIfSimple(CS.DC);
    return;
  }

  // If the underlying expression was a read-only subscript, diagnose that.
  if (auto *SD = dyn_cast_or_null<SubscriptDecl>(immInfo.second)) {
    StringRef message;
    if (!SD->isSettable())
      message = "subscript is get-only";
    else if (!SD->isSetterAccessibleFrom(CS.DC))
      message = "subscript setter is inaccessible";
    else
      message = "subscript is immutable";

    TC.diagnose(loc, diagID, message)
      .highlight(immInfo.first->getSourceRange());
    return;
  }
  
  // If we're trying to set an unapplied method, say that.
  if (auto *VD = dyn_cast_or_null<ValueDecl>(immInfo.second)) {
    std::string message = "'";
    message += VD->getBaseName().getIdentifier().str();
    message += "'";
    
    if (auto *AFD = dyn_cast<AbstractFunctionDecl>(VD))
      message += AFD->getImplicitSelfDecl() ? " is a method" : " is a function";
    else
      message += " is not settable";
    
    TC.diagnose(loc, diagID, message)
      .highlight(immInfo.first->getSourceRange());
    return;
  }

  // If the expression is the result of a call, it is an rvalue, not a mutable
  // lvalue.
  if (auto *AE = dyn_cast<ApplyExpr>(immInfo.first)) {
    // Handle literals, which are a call to the conversion function.
    auto argsTuple =
      dyn_cast<TupleExpr>(AE->getArg()->getSemanticsProvidingExpr());
    if (isa<CallExpr>(AE) && AE->isImplicit() && argsTuple &&
        argsTuple->getNumElements() == 1 &&
        isa<LiteralExpr>(argsTuple->getElement(0)->
                         getSemanticsProvidingExpr())) {
      TC.diagnose(loc, diagID, "literals are not mutable");
      return;
    }

    std::string name = "call";
    if (isa<PrefixUnaryExpr>(AE) || isa<PostfixUnaryExpr>(AE))
      name = "unary operator";
    else if (isa<BinaryExpr>(AE))
      name = "binary operator";
    else if (isa<CallExpr>(AE))
      name = "function call";
    else if (isa<DotSyntaxCallExpr>(AE) || isa<DotSyntaxBaseIgnoredExpr>(AE))
      name = "method call";

    if (auto *DRE = dyn_cast<DeclRefExpr>(AE->getFn()->getValueProvidingExpr()))
      name = std::string("'") +
             DRE->getDecl()->getBaseName().getIdentifier().str().str() + "'";

    TC.diagnose(loc, diagID, name + " returns immutable value")
      .highlight(AE->getSourceRange());
    return;
  }
  
  if (auto *ICE = dyn_cast<ImplicitConversionExpr>(immInfo.first))
    if (isa<LoadExpr>(ICE->getSubExpr())) {
      TC.diagnose(loc, diagID,
                  "implicit conversion from '" +
                      CS.getType(ICE->getSubExpr())->getString() + "' to '" +
                      CS.getType(ICE)->getString() + "' requires a temporary")
          .highlight(ICE->getSourceRange());
      return;
    }

  if (auto IE = dyn_cast<IfExpr>(immInfo.first)) {
    if (isLoadedLValue(IE)) {
      TC.diagnose(loc, diagID,
                  "result of conditional operator '? :' is never mutable")
        .highlight(IE->getQuestionLoc())
        .highlight(IE->getColonLoc());
      return;
    }
  }

  TC.diagnose(loc, unknownDiagID, CS.getType(destExpr))
      .highlight(immInfo.first->getSourceRange());
}

/// Flags that can be used to control name lookup.
enum TCCFlags {
  /// Allow the result of the subexpression to be an lvalue.  If this is not
  /// specified, any lvalue will be forced to be loaded into an rvalue.
  TCC_AllowLValue = 0x01,
  
  /// Re-type-check the given subexpression even if the expression has already
  /// been checked already.  The client is asserting that infinite recursion is
  /// not possible because it has relaxed a constraint on the system.
  TCC_ForceRecheck = 0x02,
    
  /// tell typeCheckExpression that it is ok to produce an ambiguous result,
  /// it can just fill in holes with UnresolvedType and we'll deal with it.
  TCC_AllowUnresolvedTypeVariables = 0x04
};

typedef OptionSet<TCCFlags> TCCOptions;

inline TCCOptions operator|(TCCFlags flag1, TCCFlags flag2) {
  return TCCOptions(flag1) | flag2;
}


namespace {
/// If a constraint system fails to converge on a solution for a given
/// expression, this class can produce a reasonable diagnostic for the failure
/// by analyzing the remnants of the failed constraint system. (Specifically,
/// left-over inactive, active and failed constraints.)
/// This class does not tune its diagnostics for a specific expression kind,
/// for that, you'll want to use an instance of the FailureDiagnosis class.
class FailureDiagnosis :public ASTVisitor<FailureDiagnosis, /*exprresult*/bool>{
  friend class ASTVisitor<FailureDiagnosis, /*exprresult*/bool>;
  
  Expr *expr = nullptr;
  ConstraintSystem &CS;

public:
  FailureDiagnosis(Expr *expr, ConstraintSystem &cs) : expr(expr), CS(cs) {
    assert(expr);
  }

  template<typename ...ArgTypes>
  InFlightDiagnostic diagnose(ArgTypes &&...Args) {
    return CS.TC.diagnose(std::forward<ArgTypes>(Args)...);
  }

  /// Attempt to diagnose a failure without taking into account the specific
  /// kind of expression that could not be type checked.
  bool diagnoseConstraintFailure();

  /// Unless we've already done this, retypecheck the specified child of the
  /// current expression on its own, without including any contextual
  /// constraints or the parent expr nodes.  This is more likely to succeed than
  /// type checking the original expression.
  ///
  /// This mention may only be used on immediate children of the current expr
  /// node, because ClosureExpr parameters need to be treated specially.
  ///
  /// This can return a new expression (for e.g. when a UnresolvedDeclRef gets
  /// resolved) and returns null when the subexpression fails to typecheck.
  ///
  Expr *typeCheckChildIndependently(
      Expr *subExpr, Type convertType = Type(),
      ContextualTypePurpose convertTypePurpose = CTP_Unused,
      TCCOptions options = TCCOptions(),
      ExprTypeCheckListener *listener = nullptr,
      bool allowFreeTypeVariables = true);
  Expr *typeCheckChildIndependently(Expr *subExpr, TCCOptions options,
                                    bool allowFreeTypeVariables = true) {
    return typeCheckChildIndependently(subExpr, Type(), CTP_Unused, options,
                                       nullptr, allowFreeTypeVariables);
  }

  Type getTypeOfTypeCheckedChildIndependently(Expr *subExpr,
                                            TCCOptions options = TCCOptions()) {
    auto e = typeCheckChildIndependently(subExpr, options);
    return e ? CS.getType(e) : Type();
  }

  /// This is the same as typeCheckChildIndependently, but works on an arbitrary
  /// subexpression of the current node because it handles ClosureExpr parents
  /// of the specified node.
  Expr *typeCheckArbitrarySubExprIndependently(Expr *subExpr,
                                             TCCOptions options = TCCOptions());

  /// Special magic to handle inout exprs and tuples in argument lists.
  Expr *typeCheckArgumentChildIndependently(Expr *argExpr, Type argType,
                                        const CalleeCandidateInfo &candidates,
                                            TCCOptions options = TCCOptions());

  /// Diagnose common failures due to applications of an argument list to an
  /// ApplyExpr or SubscriptExpr.
  bool diagnoseParameterErrors(CalleeCandidateInfo &CCI,
                               Expr *fnExpr, Expr *argExpr,
                               ArrayRef<Identifier> argLabels);

  /// Attempt to diagnose a specific failure from the info we've collected from
  /// the failed constraint system.
  bool diagnoseExprFailure();

  /// Emit an ambiguity diagnostic about the specified expression.
  void diagnoseAmbiguity(Expr *E);

  /// Attempt to produce a diagnostic for a mismatch between an expression's
  /// type and its assumed contextual type.
  bool diagnoseContextualConversionError(Expr *expr, Type contextualType,
                                         ContextualTypePurpose CTP);

  /// For an expression being type checked with a CTP_CalleeResult contextual
  /// type, try to diagnose a problem.
  bool diagnoseCalleeResultContextualConversionError();

  /// Attempt to produce a diagnostic for a mismatch between a call's
  /// type and its assumed contextual type.
  bool diagnoseCallContextualConversionErrors(ApplyExpr *callEpxr,
                                              Type contextualType,
                                              ContextualTypePurpose CTP);

private:
  /// Validate potential contextual type for type-checking one of the
  /// sub-expressions, usually correct/valid types are the ones which
  /// either don't have type variables or are not generic, because
  /// generic types with left-over type variables or unresolved types
  /// degrade quality of diagnostics if allowed to be used as contextual.
  ///
  /// \param contextualType The candidate contextual type.
  /// \param CTP The contextual purpose attached to the given candidate.
  ///
  /// \returns Pair of validated type and it's purpose, potentially nullified
  /// if it wasn't an appropriate type to be used.
  std::pair<Type, ContextualTypePurpose>
  validateContextualType(Type contextualType, ContextualTypePurpose CTP);

  /// Check the specified closure to see if it is a multi-statement closure with
  /// an uninferred type.  If so, diagnose the problem with an error and return
  /// true.
  bool diagnoseAmbiguousMultiStatementClosure(ClosureExpr *closure);

  /// Check the associated constraint system to see if it has any archetypes
  /// not properly resolved or missing. If so, diagnose the problem with
  /// an error and return true.
  bool diagnoseArchetypeAmbiguity();

  /// Emit an error message about an unbound generic parameter existing, and
  /// emit notes referring to the target of a diagnostic, e.g., the function
  /// or parameter being used.
  void diagnoseUnboundArchetype(ArchetypeType *archetype, Expr *anchor);

  /// Produce a diagnostic for a general member-lookup failure (irrespective of
  /// the exact expression kind).
  bool diagnoseGeneralMemberFailure(Constraint *constraint);
  
  /// Diagnose the lookup of a static member or enum element as instance member.
  void diagnoseTypeMemberOnInstanceLookup(Type baseObjTy,
                                          Expr *baseExpr,
                                          DeclName memberName,
                                          DeclNameLoc nameLoc,
                                          ValueDecl *member,
                                          SourceLoc loc);

  /// Given a result of name lookup that had no viable results, diagnose the
  /// unviable ones.
  void diagnoseUnviableLookupResults(MemberLookupResult &lookupResults,
                                     Type baseObjTy, Expr *baseExpr,
                                     DeclName memberName, DeclNameLoc nameLoc,
                                     SourceLoc loc);
  
  /// Produce a diagnostic for a general overload resolution failure
  /// (irrespective of the exact expression kind).
  bool diagnoseGeneralOverloadFailure(Constraint *constraint);
  
  /// Produce a diagnostic for a general conversion failure (irrespective of the
  /// exact expression kind).
  bool diagnoseGeneralConversionFailure(Constraint *constraint);

  /// Produce a specialized diagnostic if this is an invalid conversion to Bool.
  bool diagnoseConversionToBool(Expr *expr, Type exprType);
  
  /// Produce a diagnostic for binary comparisons of the nil literal
  /// to other values.
  bool diagnoseNilLiteralComparison(Expr *lhsExpr, Expr *rhsExpr,
                                    CalleeCandidateInfo &calleeInfo,
                                    SourceLoc applyLoc);

  /// Produce diagnostic for failures related to attributes associated with
  /// candidate functions/methods e.g. mutability.
  bool diagnoseMethodAttributeFailures(ApplyExpr *expr,
                                       ArrayRef<Identifier> argLabels,
                                       bool hasTrailingClosure,
                                       CalleeCandidateInfo &candidates);

  /// Produce diagnostic for failures related to unfulfilled requirements
  /// of the generic parameters used as arguments.
  bool diagnoseArgumentGenericRequirements(TypeChecker &TC, Expr *callExpr,
                                           Expr *fnExpr, Expr *argExpr,
                                           CalleeCandidateInfo &candidates,
                                           ArrayRef<Identifier> argLabels);

  bool diagnoseMemberFailures(
      Expr *E, Expr *baseEpxr, ConstraintKind lookupKind, DeclName memberName,
      FunctionRefKind funcRefKind, ConstraintLocator *locator,
      Optional<std::function<bool(ArrayRef<OverloadChoice>)>> callback = None,
      bool includeInaccessibleMembers = true);

  bool diagnoseTrailingClosureErrors(ApplyExpr *expr);

  bool diagnoseClosureExpr(ClosureExpr *closureExpr, Type contextualType,
                           std::function<bool(Type, Type)> resultTypeProcessor);

  bool diagnoseSubscriptErrors(SubscriptExpr *SE, bool performingSet);

  bool visitExpr(Expr *E);
  bool visitIdentityExpr(IdentityExpr *E);
  bool visitTryExpr(TryExpr *E);
  bool visitTupleExpr(TupleExpr *E);
  
  bool visitUnresolvedMemberExpr(UnresolvedMemberExpr *E);
  bool visitUnresolvedDotExpr(UnresolvedDotExpr *UDE);
  bool visitArrayExpr(ArrayExpr *E);
  bool visitDictionaryExpr(DictionaryExpr *E);
  bool visitObjectLiteralExpr(ObjectLiteralExpr *E);

  bool visitForceValueExpr(ForceValueExpr *FVE);
  bool visitBindOptionalExpr(BindOptionalExpr *BOE);

  bool visitSubscriptExpr(SubscriptExpr *SE);
  bool visitApplyExpr(ApplyExpr *AE);
  bool visitAssignExpr(AssignExpr *AE);
  bool visitInOutExpr(InOutExpr *IOE);
  bool visitCoerceExpr(CoerceExpr *CE);
  bool visitIfExpr(IfExpr *IE);
  bool visitRebindSelfInConstructorExpr(RebindSelfInConstructorExpr *E);
  bool visitCaptureListExpr(CaptureListExpr *CLE);
  bool visitClosureExpr(ClosureExpr *CE);
  bool visitKeyPathExpr(KeyPathExpr *KPE);
};
} // end anonymous namespace



static bool isMemberConstraint(Constraint *C) {
  return C->getClassification() == ConstraintClassification::Member;
}

static bool isOverloadConstraint(Constraint *C) {
  if (C->getKind() == ConstraintKind::BindOverload)
    return true;

  if (C->getKind() != ConstraintKind::Disjunction)
    return false;
  
  return C->getNestedConstraints().front()->getKind() ==
    ConstraintKind::BindOverload;
}

/// Return true if this constraint is a conversion or requirement between two
/// types.
static bool isConversionConstraint(const Constraint *C) {
  return C->getClassification() == ConstraintClassification::Relational;
}

/// Attempt to diagnose a failure without taking into account the specific
/// kind of expression that could not be type checked.
bool FailureDiagnosis::diagnoseConstraintFailure() {
  // This is the priority order in which we handle constraints.  Things earlier
  // in the list are considered to have higher specificity (and thus, higher
  // priority) than things lower in the list.
  enum ConstraintRanking {
    CR_MemberConstraint,
    CR_ConversionConstraint,
    CR_OverloadConstraint,
    CR_OtherConstraint
  };

  // Start out by classifying all the constraints.
  typedef std::pair<Constraint*, ConstraintRanking> RCElt;
  std::vector<RCElt> rankedConstraints;

  // This is a predicate that classifies constraints according to our
  // priorities.
  std::function<void (Constraint*)> classifyConstraint = [&](Constraint *C) {
    if (isMemberConstraint(C))
      return rankedConstraints.push_back({C, CR_MemberConstraint});

    if (isOverloadConstraint(C))
      return rankedConstraints.push_back({C, CR_OverloadConstraint});

    if (isConversionConstraint(C))
      return rankedConstraints.push_back({C, CR_ConversionConstraint});

    // We occasionally end up with disjunction constraints containing an
    // original constraint along with one considered with a fix.  If we find
    // this situation, add the original one to our list for diagnosis.
    if (C->getKind() == ConstraintKind::Disjunction) {
      Constraint *Orig = nullptr;
      bool AllOthersHaveFixes = true;
      for (auto DC : C->getNestedConstraints()) {
        // If this is a constraint inside of the disjunction with a fix, ignore
        // it.
        if (DC->getFix())
          continue;

        // If we already found a candidate without a fix, we can't do this.
        if (Orig) {
          AllOthersHaveFixes = false;
          break;
        }

        // Remember this as the exemplar to use.
        Orig = DC;
      }

      if (Orig && AllOthersHaveFixes)
        return classifyConstraint(Orig);
      
      // If we got all the way down to a truly ambiguous disjunction constraint
      // with a conversion in it, the problem could be that none of the options
      // in the disjunction worked.
      //
      // We don't have a lot of great options here, so (if all else fails),
      // we'll attempt to diagnose the issue as though the first option was the
      // problem.
      rankedConstraints.push_back({
        C->getNestedConstraints()[0],
        CR_OtherConstraint
      });
      return;
    }

    return rankedConstraints.push_back({C, CR_OtherConstraint});
  };
  
  // Look at the failed constraint and the general constraint list.  Processing
  // the failed constraint first slightly biases it in the ranking ahead of
  // other failed constraints at the same level.
  if (CS.failedConstraint)
    classifyConstraint(CS.failedConstraint);
  for (auto &C : CS.getConstraints())
    classifyConstraint(&C);

  // Okay, now that we've classified all the constraints, sort them by their
  // priority and privilege the favored constraints.
  std::stable_sort(rankedConstraints.begin(), rankedConstraints.end(),
                   [&] (RCElt LHS, RCElt RHS) {
    // Rank things by their kind as the highest priority.
    if (LHS.second < RHS.second)
      return true;
    if (LHS.second > RHS.second)
      return false;
    // Next priority is favored constraints.
    if (LHS.first->isFavored() != RHS.first->isFavored())
      return LHS.first->isFavored();
    return false;
  });
 
  // Now that we have a sorted precedence of constraints to diagnose, charge
  // through them.
  for (auto elt : rankedConstraints) {
    auto C = elt.first;
    if (isMemberConstraint(C) && diagnoseGeneralMemberFailure(C))
      return true;

    if (isConversionConstraint(C) && diagnoseGeneralConversionFailure(C))
      return true;

    if (isOverloadConstraint(C) && diagnoseGeneralOverloadFailure(C))
      return true;
    

    // TODO: There can be constraints that aren't handled here!  When this
    // happens, we end up diagnosing them as ambiguities that don't make sense.
    // This isn't as bad as it seems though, because most of these will be
    // diagnosed by expr diagnostics.
  }
  
  // Otherwise, all the constraints look ok, diagnose this as an ambiguous
  // expression.
  return false;
}


bool FailureDiagnosis::diagnoseGeneralMemberFailure(Constraint *constraint) {
  assert(isMemberConstraint(constraint));

  // Get the referenced base expression from the failed constraint, along with
  // the SourceRange for the member ref.  In "x.y", this returns the expr for x
  // and the source range for y.
  auto anchor = expr;
  SourceRange memberRange = anchor->getSourceRange();
  auto locator = constraint->getLocator();
  if (locator) {
    locator = simplifyLocator(CS, locator, memberRange);
    if (locator->getAnchor())
      anchor = locator->getAnchor();
  }

  // Check to see if this is a locator referring to something we cannot or do
  // here: in this case, we ignore paths that end on archetypes witnesses, or
  // associated types of the expression.
  if (locator && !locator->getPath().empty()) {
    // TODO: This should only ignore *unresolved* archetypes.  For resolved
    // archetypes
    return false;
  }

  return diagnoseMemberFailures(expr, anchor, constraint->getKind(),
                                constraint->getMember(),
                                constraint->getFunctionRefKind(), locator);
}

void FailureDiagnosis::
diagnoseTypeMemberOnInstanceLookup(Type baseObjTy,
                                   Expr *baseExpr,
                                   DeclName memberName,
                                   DeclNameLoc nameLoc,
                                   ValueDecl *member,
                                   SourceLoc loc) {
  SourceRange baseRange = baseExpr ? baseExpr->getSourceRange() : SourceRange();

  Optional<InFlightDiagnostic> Diag;

  // If the base of the lookup is a protocol metatype, suggest
  // to replace the metatype with 'Self'
  // error saying the lookup cannot be on a protocol metatype
  if (auto metatypeTy = baseObjTy->getAs<MetatypeType>()) {
    assert(metatypeTy->getInstanceType()->isExistentialType());

    // Give a customized message if we're accessing a member type
    // of a protocol -- otherwise a diagnostic talking about
    // static members doesn't make a whole lot of sense
    if (auto TAD = dyn_cast<TypeAliasDecl>(member)) {
      Diag.emplace(diagnose(loc,
                            diag::typealias_outside_of_protocol,
                            TAD->getName()));
    } else if (auto ATD = dyn_cast<AssociatedTypeDecl>(member)) {
      Diag.emplace(diagnose(loc,
                            diag::assoc_type_outside_of_protocol,
                            ATD->getName()));
    } else if (isa<ConstructorDecl>(member)) {
      Diag.emplace(diagnose(loc,
                            diag::construct_protocol_by_name,
                            metatypeTy->getInstanceType()));
    } else {
      Diag.emplace(diagnose(loc,
                            diag::could_not_use_type_member_on_protocol_metatype,
                            baseObjTy, memberName));
    }

    Diag->highlight(baseRange).highlight(nameLoc.getSourceRange());

    // See through function decl context
    if (auto parent = CS.DC->getInnermostTypeContext()) {
      // If we are in a protocol extension of 'Proto' and we see
      // 'Proto.static', suggest 'Self.static'
      if (auto extensionContext = parent->getAsProtocolExtensionContext()) {
        if (extensionContext->getDeclaredType()->isEqual(
                metatypeTy->getInstanceType())) {
          Diag->fixItReplace(baseRange, "Self");
        }
      }
    }

    return;
  }

  if (isa<EnumElementDecl>(member))
    Diag.emplace(diagnose(loc, diag::could_not_use_enum_element_on_instance,
                          memberName));
  else
    Diag.emplace(diagnose(loc, diag::could_not_use_type_member_on_instance,
                          baseObjTy, memberName));

  Diag->highlight(nameLoc.getSourceRange());

  // No fix-it if the lookup was qualified
  if (baseExpr && !baseExpr->isImplicit())
    return;

  // Determine the contextual type of the expression
  Type contextualType;
  for (auto iterateCS = &CS; contextualType.isNull() && iterateCS;
       iterateCS = iterateCS->baseCS) {
    contextualType = iterateCS->getContextualType();
  }

  // Try to provide a fix-it that only contains a '.'
  if (contextualType) {
    if (baseObjTy->isEqual(contextualType)) {
      Diag->fixItInsert(loc, ".");
      return;
    }
  }

  // Check if the expression is the matching operator ~=, most often used in
  // case statements. If so, try to provide a single dot fix-it
  const Expr *contextualTypeNode = nullptr;
  ConstraintSystem *lastCS = nullptr;
  for (auto iterateCS = &CS; iterateCS; iterateCS = iterateCS->baseCS) {
    lastCS = iterateCS;
    contextualTypeNode = iterateCS->getContextualTypeNode();
  }

  // The '~=' operator is an overloaded decl ref inside a binaryExpr
  if (auto binaryExpr = dyn_cast<BinaryExpr>(contextualTypeNode)) {
    if (auto overloadedFn
          = dyn_cast<OverloadedDeclRefExpr>(binaryExpr->getFn())) {
      if (overloadedFn->getDecls().size() > 0) {
        // Fetch any declaration to check if the name is '~='
        ValueDecl *decl0 = overloadedFn->getDecls()[0];

        if (decl0->getBaseName() == decl0->getASTContext().Id_MatchOperator) {
          assert(binaryExpr->getArg()->getElements().size() == 2);

          // If the rhs of '~=' is the enum type, a single dot suffixes
          // since the type can be inferred
          Type secondArgType =
              lastCS->getType(binaryExpr->getArg()->getElement(1));
          if (secondArgType->isEqual(baseObjTy)) {
            Diag->fixItInsert(loc, ".");
            return;
          }
        }
      }
    }
  }

  // Fall back to a fix-it with a full type qualifier
  auto nominal =
      member->getDeclContext()
      ->getAsNominalTypeOrNominalTypeExtensionContext();
  SmallString<32> typeName;
  llvm::raw_svector_ostream typeNameStream(typeName);
  typeNameStream << nominal->getSelfInterfaceType() << ".";

  Diag->fixItInsert(loc, typeNameStream.str());
  return;
}

/// When a user refers a enum case with a wrong member name, we try to find a enum
/// element whose name differs from the wrong name only in convention; meaning their
/// lower case counterparts are identical.
///   - DeclName is valid when such a correct case is found; invalid otherwise.
static DeclName
findCorrectEnumCaseName(Type Ty, LookupResult &Result,
                        DeclName memberName) {
  if (!memberName.isSimpleName())
    return DeclName();
  if (!Ty->is<EnumType>() &&
      !Ty->is<BoundGenericEnumType>())
    return DeclName();
  llvm::SmallVector<DeclName, 4> candidates;
  for (auto &correction : Result) {
    DeclName correctName = correction.getValueDecl()->getFullName();
    if (!correctName.isSimpleName())
      continue;
    if (!isa<EnumElementDecl>(correction.getValueDecl()))
      continue;
    if (correctName.getBaseIdentifier().str().equals_lower(
            memberName.getBaseIdentifier().str()))
      candidates.push_back(correctName);
  }
  if (candidates.size() == 1)
    return candidates.front();
  return DeclName();
}

/// Given a result of name lookup that had no viable results, diagnose the
/// unviable ones.
void FailureDiagnosis::
diagnoseUnviableLookupResults(MemberLookupResult &result, Type baseObjTy,
                              Expr *baseExpr,
                              DeclName memberName, DeclNameLoc nameLoc,
                              SourceLoc loc) {
  SourceRange baseRange = baseExpr ? baseExpr->getSourceRange() : SourceRange();
  
  // If we found no results at all, mention that fact.
  if (result.UnviableCandidates.empty()) {
    LookupResult correctionResults;
    auto tryTypoCorrection = [&] {
      CS.TC.performTypoCorrection(CS.DC, DeclRefKind::Ordinary, baseObjTy,
                                  memberName, nameLoc.getBaseNameLoc(),
                                  defaultMemberLookupOptions,
                                  correctionResults);
    };

    // TODO: This should handle tuple member lookups, like x.1231 as well.
    if (memberName.getBaseName().getKind() == DeclBaseName::Kind::Subscript) {
      diagnose(loc, diag::type_not_subscriptable, baseObjTy)
        .highlight(baseRange);
    } else if (memberName.getBaseName() == "deinit") {
      // Specialised diagnostic if trying to access deinitialisers
      diagnose(loc, diag::destructor_not_accessible).highlight(baseRange);
    } else if (auto metatypeTy = baseObjTy->getAs<MetatypeType>()) {
      auto instanceTy = metatypeTy->getInstanceType();
      tryTypoCorrection();

      if (DeclName rightName = findCorrectEnumCaseName(instanceTy,
                                                       correctionResults,
                                                       memberName)) {
        diagnose(loc, diag::could_not_find_enum_case, instanceTy,
                 memberName, rightName)
          .fixItReplace(nameLoc.getBaseNameLoc(),
                        rightName.getBaseIdentifier().str());
        return;
      }
      diagnose(loc, diag::could_not_find_type_member, instanceTy, memberName)
        .highlight(baseRange).highlight(nameLoc.getSourceRange());
    } else if (auto moduleTy = baseObjTy->getAs<ModuleType>()) {
      diagnose(baseExpr->getLoc(), diag::no_member_of_module,
               moduleTy->getModule()->getName(), memberName)
          .highlight(baseRange)
          .highlight(nameLoc.getSourceRange());
      return;
    } else {
      diagnose(loc, diag::could_not_find_value_member,
               baseObjTy, memberName)
        .highlight(baseRange).highlight(nameLoc.getSourceRange());
      tryTypoCorrection();
      
      // Check for a few common cases that can cause missing members.
      if (baseObjTy->is<EnumType>() && memberName.isSimpleName("rawValue")) {
        auto loc = baseObjTy->castTo<EnumType>()->getDecl()->getNameLoc();
        if (loc.isValid()) {
          diagnose(loc, diag::did_you_mean_raw_type);
          return; // Always prefer this over typo corrections.
        }
      } else if (baseObjTy->isAny()) {
        diagnose(loc, diag::any_as_anyobject_fixit)
          .fixItInsert(baseExpr->getStartLoc(), "(")
          .fixItInsertAfter(baseExpr->getEndLoc(), " as AnyObject)");
        return;
      }
    }

    // Note all the correction candidates.
    for (auto &correction : correctionResults) {
      CS.TC.noteTypoCorrection(memberName, nameLoc,
                               correction.getValueDecl());
    }

    // TODO: recover?
    return;
  }

  
  // Otherwise, we have at least one (and potentially many) viable candidates
  // sort them out.  If all of the candidates have the same problem (commonly
  // because there is exactly one candidate!) diagnose this.
  bool sameProblem = true;
  auto firstProblem = result.UnviableCandidates[0].second;
  ValueDecl *member = nullptr;
  for (auto cand : result.UnviableCandidates) {
    if (member == nullptr)
      member = cand.first.getDecl();
    sameProblem &= cand.second == firstProblem;
  }
  
  auto instanceTy = baseObjTy;
  if (auto *MTT = instanceTy->getAs<AnyMetatypeType>())
    instanceTy = MTT->getInstanceType();
  
  if (sameProblem) {
    switch (firstProblem) {
    case MemberLookupResult::UR_LabelMismatch:
      break;
    case MemberLookupResult::UR_UnavailableInExistential:
      diagnose(loc, diag::could_not_use_member_on_existential,
               instanceTy, memberName)
        .highlight(baseRange).highlight(nameLoc.getSourceRange());
      return;
    case MemberLookupResult::UR_InstanceMemberOnType: {
      // If the base is an implicit self type reference, and we're in a
      // an initializer, then the user wrote something like:
      //
      //   class Foo { let x = 1, y = x }
      //
      // which runs in type context, not instance context, or
      //
      //   class Bar {
      //     let otherwise = 1              // instance member
      //     var x: Int
      //     func init(x: Int =otherwise) { // default parameter
      //       self.x = x
      //     }
      //   }
      //
      // in which an instance member is used as a default value for a
      // parameter.
      //
      // Produce a tailored diagnostic for these cases since this
      // comes up and is otherwise non-obvious what is going on.
      if (baseExpr && baseExpr->isImplicit() && isa<Initializer>(CS.DC)) {
        auto *TypeDC = CS.DC->getParent();
        bool propertyInitializer = true;
        // If the parent context is not a type context, we expect it
        // to be a defaulted parameter in a function declaration.
        if (!TypeDC->isTypeContext()) {
          assert(TypeDC->getContextKind() ==
                     DeclContextKind::AbstractFunctionDecl &&
                 "Expected function decl context for initializer!");
          TypeDC = TypeDC->getParent();
          propertyInitializer = false;
        }
        assert(TypeDC->isTypeContext() && "Expected type decl context!");

        if (TypeDC->getAsNominalTypeOrNominalTypeExtensionContext() ==
            instanceTy->getAnyNominal()) {
          if (propertyInitializer)
            CS.TC.diagnose(nameLoc, diag::instance_member_in_initializer,
                           memberName);
          else
            CS.TC.diagnose(nameLoc, diag::instance_member_in_default_parameter,
                           memberName);
          return;
        }
      }

      // Check whether the instance member is declared on parent context and if so
      // provide more specialized message.
      auto memberTypeContext = member->getDeclContext()->getInnermostTypeContext();
      auto currentTypeContext = CS.DC->getInnermostTypeContext();
      if (memberTypeContext && currentTypeContext &&
          memberTypeContext->getSemanticDepth() <
          currentTypeContext->getSemanticDepth()) {
        diagnose(loc, diag::could_not_use_instance_member_on_type,
                 currentTypeContext->getDeclaredInterfaceType(), memberName,
                 memberTypeContext->getDeclaredInterfaceType(),
                 true)
          .highlight(baseRange).highlight(nameLoc.getSourceRange());
      } else {
        diagnose(loc, diag::could_not_use_instance_member_on_type,
                 instanceTy, memberName,
                 instanceTy,
                 false)
         .highlight(baseRange).highlight(nameLoc.getSourceRange());
      }
      return;
    }

    case MemberLookupResult::UR_TypeMemberOnInstance:
      diagnoseTypeMemberOnInstanceLookup(baseObjTy, baseExpr,
                                         memberName, nameLoc,
                                         member, loc);
      return;
        
    case MemberLookupResult::UR_MutatingMemberOnRValue:
    case MemberLookupResult::UR_MutatingGetterOnRValue: {
      auto diagIDsubelt = diag::cannot_pass_rvalue_mutating_subelement;
      auto diagIDmember = diag::cannot_pass_rvalue_mutating;
      if (firstProblem == MemberLookupResult::UR_MutatingGetterOnRValue) {
        diagIDsubelt = diag::cannot_pass_rvalue_mutating_getter_subelement;
        diagIDmember = diag::cannot_pass_rvalue_mutating_getter;
      }
      assert(baseExpr && "Cannot have a mutation failure without a base");
      diagnoseSubElementFailure(baseExpr, loc, CS, diagIDsubelt, diagIDmember);
      return;
    }
        
    case MemberLookupResult::UR_Inaccessible: {
      auto decl = result.UnviableCandidates[0].first.getDecl();
      // FIXME: What if the unviable candidates have different levels of access?
      diagnose(nameLoc, diag::candidate_inaccessible, decl->getBaseName(),
               decl->getFormalAccess());
      for (auto cand : result.UnviableCandidates)
        diagnose(cand.first.getDecl(), diag::decl_declared_here, memberName);
        
      return;
    }
    }
  }

  // FIXME: Emit candidate set....
  
  
  // Otherwise, we don't have a specific issue to diagnose.  Just say the vague
  // 'cannot use' diagnostic.
  if (!baseObjTy->isEqual(instanceTy))
    diagnose(loc, diag::could_not_use_type_member,
             instanceTy, memberName)
    .highlight(baseRange).highlight(nameLoc.getSourceRange());
  else
    diagnose(loc, diag::could_not_use_value_member,
             baseObjTy, memberName)
    .highlight(baseRange).highlight(nameLoc.getSourceRange());
  return;
}

// In the absence of a better conversion constraint failure, point out the
// inability to find an appropriate overload.
bool FailureDiagnosis::diagnoseGeneralOverloadFailure(Constraint *constraint) {
  Constraint *bindOverload = constraint;
  if (constraint->getKind() == ConstraintKind::Disjunction)
    bindOverload = constraint->getNestedConstraints().front();

  auto overloadChoice = bindOverload->getOverloadChoice();
  auto overloadName = overloadChoice.getName();

  // Get the referenced expression from the failed constraint.
  auto anchor = expr;
  if (auto locator = bindOverload->getLocator()) {
    anchor = simplifyLocatorToAnchor(CS, locator);
    if (!anchor)
      return false;
  }

  // The anchor for the constraint is almost always an OverloadedDeclRefExpr or
  // UnresolvedDotExpr.  Look at the parent node in the AST to find the Apply to
  // give a better diagnostic.
  Expr *call = expr->getParentMap()[anchor];
  // We look through some simple things that get in between the overload set
  // and the apply.
  while (call &&
         (isa<IdentityExpr>(call) ||
          isa<TryExpr>(call) || isa<ForceTryExpr>(call))) {
    call = expr->getParentMap()[call];
  }
  
  // FIXME: This is only needed because binops don't respect contextual types.
  if (call && isa<ApplyExpr>(call))
    return false;

  // This happens, for example, with ambiguous OverloadedDeclRefExprs. We should
  // just implement visitOverloadedDeclRefExprs and nuke this.
  
  // If we couldn't resolve an argument, then produce a generic "ambiguity"
  // diagnostic.
  diagnose(anchor->getLoc(), diag::ambiguous_member_overload_set,
           overloadName)
    .highlight(anchor->getSourceRange());

  if (constraint->getKind() == ConstraintKind::Disjunction) {
    for (auto elt : constraint->getNestedConstraints()) {
      if (elt->getKind() != ConstraintKind::BindOverload) continue;
      if (!elt->getOverloadChoice().isDecl()) continue;
      auto candidate = elt->getOverloadChoice().getDecl();
      diagnose(candidate, diag::found_candidate);
    }
  }

  return true;
}

/// Produce a specialized diagnostic if this is an invalid conversion to Bool.
bool FailureDiagnosis::diagnoseConversionToBool(Expr *expr, Type exprType) {
  
  // Check for "=" converting to Bool.  The user probably meant ==.
  if (auto *AE = dyn_cast<AssignExpr>(expr->getValueProvidingExpr())) {
    diagnose(AE->getEqualLoc(), diag::use_of_equal_instead_of_equality)
      .fixItReplace(AE->getEqualLoc(), "==")
      .highlight(AE->getDest()->getLoc())
      .highlight(AE->getSrc()->getLoc());
    return true;
  }
  
  // If we're trying to convert something from optional type to Bool, then a
  // comparison against nil was probably expected.
  // TODO: It would be nice to handle "!x" --> x == false, but we have no way
  // to get to the parent expr at present.
  if (exprType->getAnyOptionalObjectType()) {
    StringRef prefix = "((";
    StringRef suffix = ") != nil)";
    
    // Check if we need the inner parentheses.
    // Technically we only need them if there's something in 'expr' with
    // lower precedence than '!=', but the code actually comes out nicer
    // in most cases with parens on anything non-trivial.
    if (expr->canAppendPostfixExpression()) {
      prefix = prefix.drop_back();
      suffix = suffix.drop_front();
    }
    // FIXME: The outer parentheses may be superfluous too.
    
    diagnose(expr->getLoc(), diag::optional_used_as_boolean, exprType)
      .fixItInsert(expr->getStartLoc(), prefix)
      .fixItInsertAfter(expr->getEndLoc(), suffix);
    return true;
  }

  return false;
}


bool FailureDiagnosis::diagnoseGeneralConversionFailure(Constraint *constraint){
  auto anchor = expr;
  bool resolvedAnchorToExpr = false;
  
  if (auto locator = constraint->getLocator()) {
    anchor = simplifyLocatorToAnchor(CS, locator);
    if (anchor)
      resolvedAnchorToExpr = true;
    else
      anchor = locator->getAnchor();    
  }

  Type fromType =
      CS.simplifyType(constraint->getFirstType())->getWithoutImmediateLabel();

  if (fromType->hasTypeVariable() && resolvedAnchorToExpr) {
    TCCOptions options;
    
    // If we know we're removing a contextual constraint, then we can force a
    // type check of the subexpr because we know we're eliminating that
    // constraint.
    if (CS.getContextualTypePurpose() != CTP_Unused)
      options |= TCC_ForceRecheck;
      
    auto sub = typeCheckArbitrarySubExprIndependently(anchor, options);
    if (!sub) return true;
    fromType = CS.getType(sub);
  }

  // Bail on constraints that don't relate two types.
  if (constraint->getKind() == ConstraintKind::Disjunction
      || constraint->getKind() == ConstraintKind::BindOverload)
    return false;

  fromType = fromType->getRValueType();
  auto toType =
      CS.simplifyType(constraint->getSecondType())->getWithoutImmediateLabel();

  // Try to simplify irrelevant details of function types.  For example, if
  // someone passes a "() -> Float" function to a "() throws -> Int"
  // parameter, then uttering the "throws" may confuse them into thinking that
  // that is the problem, even though there is a clear subtype relation.
  if (auto srcFT = fromType->getAs<FunctionType>())
    if (auto destFT = toType->getAs<FunctionType>()) {
      auto destExtInfo = destFT->getExtInfo();

      if (!srcFT->isNoEscape()) destExtInfo = destExtInfo.withNoEscape(false);
      if (!srcFT->throws()) destExtInfo = destExtInfo.withThrows(false);
      if (destExtInfo != destFT->getExtInfo())
        toType = FunctionType::get(destFT->getInput(),
                                           destFT->getResult(), destExtInfo);

      // If this is a function conversion that discards throwability or
      // noescape, emit a specific diagnostic about that.
      if (srcFT->throws() && !destFT->throws()) {
        diagnose(expr->getLoc(), diag::throws_functiontype_mismatch,
                 fromType, toType)
        .highlight(expr->getSourceRange());
        return true;
      }

      if (srcFT->isNoEscape() && !destFT->isNoEscape()) {
        diagnose(expr->getLoc(), diag::noescape_functiontype_mismatch,
                 fromType, toType)
        .highlight(expr->getSourceRange());
        return true;
      }
    }

  // If this is a callee that mismatches an expected return type, we can emit a
  // very nice and specific error.  In this case, what we'll generally see is
  // a failed conversion constraint of "A -> B" to "_ -> C", where the error is
  // that B isn't convertible to C.
  if (CS.getContextualTypePurpose() == CTP_CalleeResult) {
    auto destFT = toType->getAs<FunctionType>();
    auto srcFT = fromType->getAs<FunctionType>();
    if (destFT && srcFT && !isUnresolvedOrTypeVarType(srcFT->getResult())) {
      // Otherwise, the error is that the result types mismatch.
      diagnose(expr->getLoc(), diag::invalid_callee_result_type,
               srcFT->getResult(), destFT->getResult())
        .highlight(expr->getSourceRange());
      return true;
    }
  }
  
  
  // If simplification has turned this into the same types, then this isn't the
  // broken constraint that we're looking for.
  if (fromType->isEqual(toType) &&
      constraint->getKind() != ConstraintKind::ConformsTo &&
      constraint->getKind() != ConstraintKind::LiteralConformsTo)
    return false;
  
  
  // If we have two tuples with mismatching types, produce a tailored
  // diagnostic.
  if (auto fromTT = fromType->getAs<TupleType>())
    if (auto toTT = toType->getAs<TupleType>()) {
      if (fromTT->getNumElements() != toTT->getNumElements()) {
        diagnose(anchor->getLoc(), diag::tuple_types_not_convertible_nelts,
                 fromTT, toTT)
        .highlight(anchor->getSourceRange());
        return true;
      }
     
      SmallVector<TupleTypeElt, 4> FromElts;
      auto voidTy = CS.getASTContext().TheUnresolvedType;

      for (unsigned i = 0, e = fromTT->getNumElements(); i != e; ++i)
        FromElts.push_back({ voidTy, fromTT->getElement(i).getName() });
      auto TEType = TupleType::get(FromElts, CS.getASTContext());

      SmallVector<int, 4> sources;
      SmallVector<unsigned, 4> variadicArgs;
      
      // If the shuffle conversion is invalid (e.g. incorrect element labels),
      // then we have a type error.
      if (computeTupleShuffle(TEType->castTo<TupleType>()->getElements(),
                              toTT->getElements(), sources, variadicArgs)) {
        diagnose(anchor->getLoc(), diag::tuple_types_not_convertible,
                 fromTT, toTT)
        .highlight(anchor->getSourceRange());
        return true;
      }
    }
  
  
  // If the second type is a type variable, the expression itself is
  // ambiguous.  Bail out so the general ambiguity diagnosing logic can handle
  // it.
  if (fromType->hasUnresolvedType() || fromType->hasTypeVariable() ||
      toType->hasUnresolvedType() || toType->hasTypeVariable() ||
      // FIXME: Why reject unbound generic types here?
      fromType->is<UnboundGenericType>())
    return false;

  
  // Check for various issues converting to Bool.
  if (toType->isBool() && diagnoseConversionToBool(anchor, fromType))
    return true;
  
  
  if (auto PT = toType->getAs<ProtocolType>()) {
    if (isa<NilLiteralExpr>(expr->getValueProvidingExpr())) {
      diagnose(expr->getLoc(), diag::cannot_use_nil_with_this_type, toType)
        .highlight(expr->getSourceRange());
      return true;
    }

    // Emit a conformance error through conformsToProtocol.
    if (auto conformance = CS.TC.conformsToProtocol(
            fromType, PT->getDecl(), CS.DC, ConformanceCheckFlags::InExpression,
            expr->getLoc())) {
      if (conformance->isAbstract() ||
          !conformance->getConcrete()->isInvalid())
        return false;
    }

    return true;
  }

  // Due to migration reasons, types used to conform to BooleanType, which
  // contain a member var 'boolValue', now does not convert to Bool. This block
  // tries to add a specific diagnosis/fixit to explicitly invoke 'boolValue'.
  if (toType->isBool() &&
      fromType->mayHaveMembers()) {
    auto LookupResult = CS.TC.lookupMember(
        CS.DC, fromType, DeclName(CS.TC.Context.getIdentifier("boolValue")));
    if (!LookupResult.empty()) {
      if (isa<VarDecl>(LookupResult.begin()->getValueDecl())) {
        if (anchor->canAppendPostfixExpression())
          diagnose(anchor->getLoc(), diag::types_not_convertible_use_bool_value,
                   fromType, toType).fixItInsertAfter(anchor->getEndLoc(),
                                                      ".boolValue");
        else
          diagnose(anchor->getLoc(), diag::types_not_convertible_use_bool_value,
            fromType, toType).fixItInsert(anchor->getStartLoc(), "(").
              fixItInsertAfter(anchor->getEndLoc(), ").boolValue");
        return true;
      }
    }
  }
  
  diagnose(anchor->getLoc(), diag::types_not_convertible,
           constraint->getKind() == ConstraintKind::Subtype,
           fromType, toType)
    .highlight(anchor->getSourceRange());

  // Check to see if this constraint came from a cast instruction. If so,
  // and if this conversion constraint is different than the types being cast,
  // produce a note that talks about the overall expression.
  //
  // TODO: Using parentMap would be more general, rather than requiring the
  // issue to be related to the root of the expr under study.
  if (auto ECE = dyn_cast<ExplicitCastExpr>(expr))
    if (constraint->getLocator() &&
        constraint->getLocator()->getAnchor() == ECE->getSubExpr()) {
      if (!toType->isEqual(ECE->getCastTypeLoc().getType()))
        diagnose(expr->getLoc(), diag::in_cast_expr_types,
                 CS.getType(ECE->getSubExpr())->getRValueType(),
                 ECE->getCastTypeLoc().getType()->getRValueType())
            .highlight(ECE->getSubExpr()->getSourceRange())
            .highlight(ECE->getCastTypeLoc().getSourceRange());
  }

  return true;
}

namespace {
  class ExprTypeSaverAndEraser {
    llvm::DenseMap<Expr*, Type> ExprTypes;
    llvm::DenseMap<TypeLoc*, std::pair<Type, bool>> TypeLocTypes;
    llvm::DenseMap<Pattern*, Type> PatternTypes;
    llvm::DenseMap<ParamDecl*, Type> ParamDeclTypes;
    llvm::DenseMap<ParamDecl*, Type> ParamDeclInterfaceTypes;
    llvm::DenseMap<CollectionExpr*, Expr*> CollectionSemanticExprs;
    llvm::DenseSet<ValueDecl*> PossiblyInvalidDecls;
    ExprTypeSaverAndEraser(const ExprTypeSaverAndEraser&) = delete;
    void operator=(const ExprTypeSaverAndEraser&) = delete;
  public:

    ExprTypeSaverAndEraser(Expr *E) {
      struct TypeSaver : public ASTWalker {
        ExprTypeSaverAndEraser *TS;
        TypeSaver(ExprTypeSaverAndEraser *TS) : TS(TS) {}
        
        std::pair<bool, Expr *> walkToExprPre(Expr *expr) override {
          TS->ExprTypes[expr] = expr->getType();

          SWIFT_DEFER {
            assert((!expr->getType() || !expr->getType()->hasTypeVariable()
                    // FIXME: We shouldn't allow these, either.
                    || isa<LiteralExpr>(expr)) &&
                   "Type variable didn't get erased!");
          };

          // Preserve module expr type data to prevent further lookups.
          if (auto *declRef = dyn_cast<DeclRefExpr>(expr))
            if (isa<ModuleDecl>(declRef->getDecl()))
              return { false, expr };
          
          // Don't strip type info off OtherConstructorDeclRefExpr, because
          // CSGen doesn't know how to reconstruct it.
          if (isa<OtherConstructorDeclRefExpr>(expr))
            return { false, expr };
          
          // If a literal has a Builtin.Int or Builtin.FP type on it already,
          // then sema has already expanded out a call to
          //   Init.init(<builtinliteral>)
          // and we don't want it to make
          //   Init.init(Init.init(<builtinliteral>))
          // preserve the type info to prevent this from happening.
          if (isa<LiteralExpr>(expr) && !isa<InterpolatedStringLiteralExpr>(expr) &&
              !(expr->getType() && expr->getType()->hasError()))
            return { false, expr };

          // If a ClosureExpr's parameter list has types on the decls, then
          // remove them so that they'll get regenerated from the
          // associated TypeLocs or resynthesized as fresh typevars.
          if (auto *CE = dyn_cast<ClosureExpr>(expr))
            for (auto P : *CE->getParameters()) {
              if (P->hasType()) {
                TS->ParamDeclTypes[P] = P->getType();
                P->setType(Type());
              }
              if (P->hasInterfaceType()) {
                TS->ParamDeclInterfaceTypes[P] = P->getInterfaceType();
                P->setInterfaceType(Type());
              }
              TS->PossiblyInvalidDecls.insert(P);
              
              if (P->isInvalid())
                P->setInvalid(false);
            }
          
          // If we have a CollectionExpr with a type checked SemanticExpr,
          // remove it so we can recalculate a new semantic form.
          if (auto *CE = dyn_cast<CollectionExpr>(expr)) {
            if (auto SE = CE->getSemanticExpr()) {
              TS->CollectionSemanticExprs[CE] = SE;
              CE->setSemanticExpr(nullptr);
            }
          }
          
          expr->setType(nullptr);
          expr->clearLValueAccessKind();

          return { true, expr };
        }
        
        // If we find a TypeLoc (e.g. in an as? expr), save and erase it.
        bool walkToTypeLocPre(TypeLoc &TL) override {
          if (TL.getTypeRepr() && TL.getType()) {
            TS->TypeLocTypes[&TL] = { TL.getType(), TL.wasValidated() };
            TL.setType(Type(), /*was validated*/false);
          }
          return true;
        }
        
        std::pair<bool, Pattern*> walkToPatternPre(Pattern *P) override {
          if (P->hasType()) {
            TS->PatternTypes[P] = P->getType();
            P->setType(Type());
          }
          return { true, P };
        }
        
        // Don't walk into statements.  This handles the BraceStmt in
        // non-single-expr closures, so we don't walk into their body.
        std::pair<bool, Stmt *> walkToStmtPre(Stmt *S) override {
          return { false, S };
        }
      };
      
      E->walk(TypeSaver(this));
    }
    
    void restore() {
      for (auto exprElt : ExprTypes)
        exprElt.first->setType(exprElt.second);
      
      for (auto typelocElt : TypeLocTypes)
        typelocElt.first->setType(typelocElt.second.first,
                                  typelocElt.second.second);
      
      for (auto patternElt : PatternTypes)
        patternElt.first->setType(patternElt.second);
      
      for (auto paramDeclElt : ParamDeclTypes) {
        assert(!paramDeclElt.first->isLet() || !paramDeclElt.second->is<InOutType>());
        paramDeclElt.first->setType(paramDeclElt.second->getInOutObjectType());
      }
      
      for (auto paramDeclIfaceElt : ParamDeclInterfaceTypes) {
        assert(!paramDeclIfaceElt.first->isLet() || !paramDeclIfaceElt.second->is<InOutType>());
        paramDeclIfaceElt.first->setInterfaceType(paramDeclIfaceElt.second->getInOutObjectType());
      }
      
      for (auto CSE : CollectionSemanticExprs)
        CSE.first->setSemanticExpr(CSE.second);
      
      if (!PossiblyInvalidDecls.empty())
        for (auto D : PossiblyInvalidDecls)
          if (D->hasInterfaceType())
            D->setInvalid(D->getInterfaceType()->hasError());
      
      // Done, don't do redundant work on destruction.
      ExprTypes.clear();
      TypeLocTypes.clear();
      PatternTypes.clear();
      PossiblyInvalidDecls.clear();
    }
    
    // On destruction, if a type got wiped out, reset it from null to its
    // original type.  This is helpful because type checking a subexpression
    // can lead to replacing the nodes in that subexpression.  However, the
    // failed ConstraintSystem still has locators pointing to the old nodes,
    // and if expr-specific diagnostics fail to turn up anything useful to say,
    // we go digging through failed constraints, and expect their locators to
    // still be meaningful.
    ~ExprTypeSaverAndEraser() {
      for (auto CSE : CollectionSemanticExprs)
        if (!CSE.first->getType())
          CSE.first->setSemanticExpr(CSE.second);

      for (auto exprElt : ExprTypes)
        if (!exprElt.first->getType())
          exprElt.first->setType(exprElt.second);
      
      for (auto typelocElt : TypeLocTypes)
        if (!typelocElt.first->getType())
          typelocElt.first->setType(typelocElt.second.first,
                                    typelocElt.second.second);
      
      for (auto patternElt : PatternTypes)
        if (!patternElt.first->hasType())
          patternElt.first->setType(patternElt.second);
      
      for (auto paramDeclElt : ParamDeclTypes)
        if (!paramDeclElt.first->hasType()) {
          paramDeclElt.first->setType(getParamBaseType(paramDeclElt));
        }

      for (auto paramDeclIfaceElt : ParamDeclInterfaceTypes)
        if (!paramDeclIfaceElt.first->hasInterfaceType()) {
          paramDeclIfaceElt.first->setInterfaceType(
              getParamBaseType(paramDeclIfaceElt));
        }

      if (!PossiblyInvalidDecls.empty())
        for (auto D : PossiblyInvalidDecls)
          if (D->hasInterfaceType())
            D->setInvalid(D->getInterfaceType()->hasError());
    }

  private:
    static Type getParamBaseType(std::pair<ParamDecl *, Type> &storedParam) {
      ParamDecl *param;
      Type storedType;

      std::tie(param, storedType) = storedParam;

      // FIXME: We are currently in process of removing `InOutType`
      //        so `VarDecl::get{Interface}Type` is going to wrap base
      //        type into `InOutType` if its flag indicates that it's
      //        an `inout` parameter declaration. But such type can't
      //        be restored directly using `VarDecl::set{Interface}Type`
      //        caller needs additional logic to extract base type.
      if (auto *IOT = storedType->getAs<InOutType>()) {
        assert(param->isInOut());
        return IOT->getObjectType();
      }

      return storedType;
    }
  };
} // end anonymous namespace

/// Erase an expression tree's open existentials after a re-typecheck operation.
///
/// This is done in the case of a typecheck failure, after we re-typecheck
/// partially-typechecked subexpressions in a context-free manner.
///
static void eraseOpenedExistentials(Expr *&expr, ConstraintSystem &CS) {

  class ExistentialEraser : public ASTWalker {
    ConstraintSystem &CS;
    llvm::SmallDenseMap<OpaqueValueExpr *, Expr *, 4> OpenExistentials;

  public:
    ExistentialEraser(ConstraintSystem &CS) : CS(CS) {}

    std::pair<bool, Expr *> walkToExprPre(Expr *expr) override {
      if (auto OOE = dyn_cast<OpenExistentialExpr>(expr)) {
        auto archetypeVal = OOE->getOpaqueValue();
        auto base = OOE->getExistentialValue();
        
        // Walk the base expression to ensure we erase any existentials within
        // it.
        base = base->walk(*this);
        
        bool inserted = OpenExistentials.insert({archetypeVal, base}).second;
        assert(inserted && "OpaqueValue appears multiple times?");
        (void)inserted;
        return { true, OOE->getSubExpr() };
      }
      
      if (auto OVE = dyn_cast<OpaqueValueExpr>(expr)) {
        auto value = OpenExistentials.find(OVE);
        assert(value != OpenExistentials.end() &&
               "didn't see this OVE in a containing OpenExistentialExpr?");
        return { true, value->second };
      }

      // Handle collection upcasts specially so that we don't blow up on
      // their embedded OVEs.
      if (auto CDE = dyn_cast<CollectionUpcastConversionExpr>(expr)) {
        if (auto result = CDE->getSubExpr()->walk(*this)) {
          CDE->setSubExpr(result);
          return { false, CDE };
        } else {
          return { true, CDE };
        }
      }
      
      return { true, expr };
    }

    Expr *walkToExprPost(Expr *expr) override {
      if (!CS.hasType(expr))
        return expr;

      Type type = CS.getType(expr);
      if (!type->hasOpenedExistential())
        return expr;

      type = type.transform([&](Type type) -> Type {
        if (auto archetype = type->getAs<ArchetypeType>())
          if (auto existentialType = archetype->getOpenedExistentialType())
            return existentialType;

        return type;
      });
      CS.setType(expr, type);

      return expr;
    }
    
    // Don't walk into statements.  This handles the BraceStmt in
    // non-single-expr closures, so we don't walk into their body.
    std::pair<bool, Stmt *> walkToStmtPre(Stmt *S) override {
      return { false, S };
    }
  };

  expr = expr->walk(ExistentialEraser(CS));
}

/// Unless we've already done this, retypecheck the specified subexpression on
/// its own, without including any contextual constraints or parent expr
/// nodes.  This is more likely to succeed than type checking the original
/// expression.
///
/// This can return a new expression (for e.g. when a UnresolvedDeclRef gets
/// resolved) and returns null when the subexpression fails to typecheck.
Expr *FailureDiagnosis::typeCheckChildIndependently(
    Expr *subExpr, Type convertType, ContextualTypePurpose convertTypePurpose,
    TCCOptions options, ExprTypeCheckListener *listener,
    bool allowFreeTypeVariables) {

  // If this sub-expression is currently being diagnosed, refuse to recheck the
  // expression (which may lead to infinite recursion).  If the client is
  // telling us that it knows what it is doing, then believe it.
  if (!options.contains(TCC_ForceRecheck)) {
    if (CS.TC.isExprBeingDiagnosed(subExpr)) {
      auto exprAndCS = CS.TC.getExprBeingDiagnosed(subExpr);
      auto *savedExpr = exprAndCS.first;
      if (subExpr == savedExpr)
        return subExpr;

      auto *oldCS = exprAndCS.second;

      // The types on the result might have already been cached into
      // another CS, but likely not this one.
      if (oldCS != &CS)
        CS.transferExprTypes(oldCS, savedExpr);

      return savedExpr;
    }

    CS.TC.addExprForDiagnosis(subExpr, std::make_pair(subExpr, &CS));
  }

  // Validate contextual type before trying to use it.
  std::tie(convertType, convertTypePurpose) =
      validateContextualType(convertType, convertTypePurpose);

  // If we have no contextual type information and the subexpr is obviously a
  // overload set, don't recursively simplify this.  The recursive solver will
  // sometimes pick one based on arbitrary ranking behavior (e.g. like
  // which is the most specialized) even then all the constraints are being
  // fulfilled by UnresolvedType, which doesn't tell us anything.
  if (convertTypePurpose == CTP_Unused &&
      (isa<OverloadedDeclRefExpr>(subExpr->getValueProvidingExpr()))) {
    return subExpr;
  }
  
  // Save any existing type data of the subexpr tree, and reset it to null in
  // prep for re-type-checking the tree.  If things fail, we can revert the
  // types back to their original state.
  ExprTypeSaverAndEraser SavedTypeData(subExpr);
  
  // Store off the sub-expression, in case a new one is provided via the
  // type check operation.
  Expr *preCheckedExpr = subExpr;
  
  // Disable structural checks, because we know that the overall expression
  // has type constraint problems, and we don't want to know about any
  // syntactic issues in a well-typed subexpression (which might be because
  // the context is missing).
  TypeCheckExprOptions TCEOptions = TypeCheckExprFlags::DisableStructuralChecks;

  // Don't walk into non-single expression closure bodies, because
  // ExprTypeSaver and TypeNullifier skip them too.
  TCEOptions |= TypeCheckExprFlags::SkipMultiStmtClosures;

  // Claim that the result is discarded to preserve the lvalue type of
  // the expression.
  if (options.contains(TCC_AllowLValue))
    TCEOptions |= TypeCheckExprFlags::IsDiscarded;

  // If there is no contextual type available, tell typeCheckExpression that it
  // is ok to produce an ambiguous result, it can just fill in holes with
  // UnresolvedType and we'll deal with it.
  if ((!convertType || options.contains(TCC_AllowUnresolvedTypeVariables)) &&
      allowFreeTypeVariables)
    TCEOptions |= TypeCheckExprFlags::AllowUnresolvedTypeVariables;
  
  // If we're not passing down contextual type information this time, but the
  // original failure had type info that wasn't an optional type,
  // then set the flag to prefer fixits with force unwrapping.
  if (!convertType) {
    auto previousType = CS.getContextualType();
    if (previousType && previousType->getOptionalObjectType().isNull())
      TCEOptions |= TypeCheckExprFlags::PreferForceUnwrapToOptional;
  }

  // Ensure that the expression we're about to type-check doesn't have
  // anything that the type-checker doesn't expect to see.  This can happen
  // because of repeated type-checking; the removal below, while independently
  // important, isn't itself sufficient because of AST mutation.
  eraseOpenedExistentials(subExpr, CS);

  auto resultTy = CS.TC.typeCheckExpression(
      subExpr, CS.DC, TypeLoc::withoutLoc(convertType), convertTypePurpose,
      TCEOptions, listener, &CS);

  CS.cacheExprTypes(subExpr);

  // This is a terrible hack to get around the fact that typeCheckExpression()
  // might change subExpr to point to a new OpenExistentialExpr. In that case,
  // since the caller passed subExpr by value here, they would be left
  // holding on to an expression containing open existential types but
  // no OpenExistentialExpr, which breaks invariants enforced by the
  // ASTChecker.
  eraseOpenedExistentials(subExpr, CS);
  
  // If recursive type checking failed, then an error was emitted.  Return
  // null to indicate this to the caller.
  if (!resultTy)
    return nullptr;

  // If we type checked the result but failed to get a usable output from it,
  // just pretend as though nothing happened.
  if (resultTy->is<ErrorType>()) {
    subExpr = preCheckedExpr;
    if (subExpr->getType())
      CS.cacheType(subExpr);
    SavedTypeData.restore();
  }

  CS.TC.addExprForDiagnosis(preCheckedExpr, std::make_pair(subExpr, &CS));

  return subExpr;
}

/// This is the same as typeCheckChildIndependently, but works on an arbitrary
/// subexpression of the current node because it handles ClosureExpr parents
/// of the specified node.
Expr *FailureDiagnosis::
typeCheckArbitrarySubExprIndependently(Expr *subExpr, TCCOptions options) {
  if (subExpr == expr)
    return typeCheckChildIndependently(subExpr, options);
  
  // Construct a parent map for the expr tree we're investigating.
  auto parentMap = expr->getParentMap();
  
  ClosureExpr *NearestClosure = nullptr;
  
  // Walk the parents of the specified expression, handling any ClosureExprs.
  for (Expr *node = parentMap[subExpr]; node; node = parentMap[node]) {
    auto *CE = dyn_cast<ClosureExpr>(node);
    if (!CE) continue;
    
    // Keep track of the innermost closure we see that we're jumping into.
    if (!NearestClosure)
      NearestClosure = CE;
    
    // If we have a ClosureExpr parent of the specified node, check to make sure
    // none of its arguments are type variables.  If so, these type variables
    // would be accessible to name lookup of the subexpression and may thus leak
    // in.  Reset them to UnresolvedTypes for safe measures.
    for (auto param : *CE->getParameters()) {
      auto VD = param;
      if (VD->getType()->hasTypeVariable() || VD->getType()->hasError()) {
        VD->setType(CS.getASTContext().TheUnresolvedType);
        VD->setInterfaceType(VD->getType());
      }
    }
  }

  // When we're type checking a single-expression closure, we need to reset the
  // DeclContext to this closure for the recursive type checking.  Otherwise,
  // if there is a closure in the subexpression, we can violate invariants.
  auto newDC = NearestClosure ? NearestClosure : CS.DC;
  llvm::SaveAndRestore<DeclContext *> SavedDC(CS.DC, newDC);

  // Otherwise, we're ok to type check the subexpr.
  return typeCheckChildIndependently(subExpr, options);
}

/// For an expression being type checked with a CTP_CalleeResult contextual
/// type, try to diagnose a problem.
bool FailureDiagnosis::diagnoseCalleeResultContextualConversionError() {
  // Try to dig out the conversion constraint in question to find the contextual
  // result type being specified.
  Type contextualResultType;
  for (auto &c : CS.getConstraints()) {
    if (!isConversionConstraint(&c) || !c.getLocator() ||
        c.getLocator()->getAnchor() != expr)
      continue;
    
    // If we found our contextual type, then we know we have a conversion to
    // some function type, and that the result type is concrete.  If not,
    // ignore it.
    auto toType = CS.simplifyType(c.getSecondType());
    if (auto *FT = toType->getAs<AnyFunctionType>())
      if (!isUnresolvedOrTypeVarType(FT->getResult())) {
        contextualResultType = FT->getResult();
        break;
      }
  }
  if (!contextualResultType)
    return false;

  // Retypecheck the callee expression without a contextual type to resolve
  // whatever we can in it.
  auto callee = typeCheckChildIndependently(expr, TCC_ForceRecheck);
  if (!callee)
    return true;
  
  // Based on that, compute an overload set.
  CalleeCandidateInfo calleeInfo(callee, /*hasTrailingClosure*/false, CS);

  switch (calleeInfo.size()) {
  case 0:
    // If we found no overloads, then there is something else going on here.
    return false;
      
  case 1:
    // If the callee isn't of function type, then something else has gone wrong.
    if (!calleeInfo[0].getResultType())
      return false;
      
    diagnose(expr->getLoc(), diag::candidates_no_match_result_type,
             calleeInfo.declName, calleeInfo[0].getResultType(),
             contextualResultType);
    return true;
  default:
    // Check to see if all of the viable candidates produce the same result,
    // this happens for things like "==" and "&&" operators.
    if (auto resultTy = calleeInfo[0].getResultType()) {
      for (unsigned i = 1, e = calleeInfo.size(); i != e; ++i)
        if (auto ty = calleeInfo[i].getResultType())
          if (!resultTy->isEqual(ty)) {
            resultTy = Type();
            break;
          }
      if (resultTy) {
        diagnose(expr->getLoc(), diag::candidates_no_match_result_type,
                 calleeInfo.declName, calleeInfo[0].getResultType(),
                 contextualResultType);
        return true;
      }
    }

    // Otherwise, produce a candidate set.
    diagnose(expr->getLoc(), diag::no_candidates_match_result_type,
             calleeInfo.declName, contextualResultType);
    calleeInfo.suggestPotentialOverloads(expr->getLoc(), /*isResult*/true);
    return true;
  }
}


/// Return true if the given type conforms to a known protocol type.
static bool conformsToKnownProtocol(Type fromType, KnownProtocolKind kind,
                                    const ConstraintSystem &CS) {
  auto proto = CS.TC.getProtocol(SourceLoc(), kind);
  if (!proto)
    return false;

  if (CS.TC.conformsToProtocol(fromType, proto, CS.DC,
                               ConformanceCheckFlags::InExpression)) {
    return true;
  }

  return false;
}

static bool isIntegerType(Type fromType, const ConstraintSystem &CS) {
  return conformsToKnownProtocol(fromType,
                                 KnownProtocolKind::ExpressibleByIntegerLiteral,
                                 CS);
}

/// Return true if the given type conforms to RawRepresentable.
static Type isRawRepresentable(Type fromType, const ConstraintSystem &CS) {
  auto rawReprType =
      CS.TC.getProtocol(SourceLoc(), KnownProtocolKind::RawRepresentable);
  if (!rawReprType)
    return Type();

  auto conformance = CS.TC.conformsToProtocol(
      fromType, rawReprType, CS.DC, ConformanceCheckFlags::InExpression);
  if (!conformance)
    return Type();

  Type rawTy = ProtocolConformanceRef::getTypeWitnessByName(
      fromType, *conformance, CS.getASTContext().Id_RawValue, &CS.TC);
  return rawTy;
}

/// Return true if the given type conforms to RawRepresentable, with an
/// underlying type conforming to the given known protocol.
static Type isRawRepresentable(Type fromType, KnownProtocolKind kind,
                               const ConstraintSystem &CS) {
  Type rawTy = isRawRepresentable(fromType, CS);
  if (!rawTy || !conformsToKnownProtocol(rawTy, kind, CS))
    return Type();

  return rawTy;
}

/// Return true if the conversion from fromType to toType is an invalid string
/// index operation.
static bool isIntegerToStringIndexConversion(Type fromType, Type toType,
                                             ConstraintSystem &CS) {
  auto kind = KnownProtocolKind::ExpressibleByIntegerLiteral;
  return (conformsToKnownProtocol(fromType, kind, CS) &&
          toType->getCanonicalType().getString() == "String.CharacterView.Index");
}

/// Attempts to add fix-its for these two mistakes:
///
/// - Passing an integer where a type conforming to RawRepresentable is
///   expected, by wrapping the expression in a call to the contextual
///   type's initializer
///
/// - Passing a type conforming to RawRepresentable where an integer is
///   expected, by wrapping the expression in a call to the rawValue
///   accessor
///
/// - Return true on the fixit is added, false otherwise.
///
/// This helps migration with SDK changes.
static bool tryRawRepresentableFixIts(InFlightDiagnostic &diag,
                                      const ConstraintSystem &CS, Type fromType,
                                      Type toType, KnownProtocolKind kind,
                                      const Expr *expr) {
  // The following fixes apply for optional destination types as well.
  bool toTypeIsOptional = !toType->getAnyOptionalObjectType().isNull();
  toType = toType->lookThroughAllAnyOptionalTypes();

  Type fromTypeUnwrapped = fromType->getAnyOptionalObjectType();
  bool fromTypeIsOptional = !fromTypeUnwrapped.isNull();
  if (fromTypeIsOptional)
    fromType = fromTypeUnwrapped;

  auto fixIt = [&](StringRef convWrapBefore, StringRef convWrapAfter) {
    SourceRange exprRange = expr->getSourceRange();
    if (fromTypeIsOptional && toTypeIsOptional) {
      // Use optional's map function to convert conditionally, like so:
      //   expr.map{ T(rawValue: $0) }
      bool needsParens = !expr->canAppendPostfixExpression();
      std::string mapCodeFix;
      if (needsParens) {
        diag.fixItInsert(exprRange.Start, "(");
        mapCodeFix += ")";
      }
      mapCodeFix += ".map { ";
      mapCodeFix += convWrapBefore;
      mapCodeFix += "$0";
      mapCodeFix += convWrapAfter;
      mapCodeFix += " }";
      diag.fixItInsertAfter(exprRange.End, mapCodeFix);
    } else if (!fromTypeIsOptional) {
      diag.fixItInsert(exprRange.Start, convWrapBefore);
      diag.fixItInsertAfter(exprRange.End, convWrapAfter);
    } else {
      SmallString<16> fixItBefore(convWrapBefore);
      SmallString<16> fixItAfter;

      if (!expr->canAppendPostfixExpression(true)) {
        fixItBefore += "(";
        fixItAfter = ")";
      }

      fixItAfter += "!" + convWrapAfter.str();

      diag.flush();
      CS.TC
          .diagnose(expr->getLoc(),
                    diag::construct_raw_representable_from_unwrapped_value,
                    toType, fromType)
          .highlight(exprRange)
          .fixItInsert(exprRange.Start, fixItBefore)
          .fixItInsertAfter(exprRange.End, fixItAfter);
    }
  };

  if (conformsToKnownProtocol(fromType, kind, CS)) {
    if (auto rawTy = isRawRepresentable(toType, kind, CS)) {
      // Produce before/after strings like 'Result(rawValue: RawType(<expr>))'
      // or just 'Result(rawValue: <expr>)'.
      std::string convWrapBefore = toType.getString();
      convWrapBefore += "(rawValue: ";
      std::string convWrapAfter = ")";
      if (!isa<LiteralExpr>(expr) &&
          !CS.TC.isConvertibleTo(fromType, rawTy, CS.DC)) {
        // Only try to insert a converting construction if the protocol is a
        // literal protocol and not some other known protocol.
        switch (kind) {
#define EXPRESSIBLE_BY_LITERAL_PROTOCOL_WITH_NAME(name, _) \
        case KnownProtocolKind::name: break;
#define PROTOCOL_WITH_NAME(name, _) \
        case KnownProtocolKind::name: return false;
#include "swift/AST/KnownProtocols.def"
        }
        convWrapBefore += rawTy->getString();
        convWrapBefore += "(";
        convWrapAfter += ")";
      }
      fixIt(convWrapBefore, convWrapAfter);
      return true;
    }
  }

  if (auto rawTy = isRawRepresentable(fromType, kind, CS)) {
    if (conformsToKnownProtocol(toType, kind, CS)) {
      std::string convWrapBefore;
      std::string convWrapAfter = ".rawValue";
      if (!CS.TC.isConvertibleTo(rawTy, toType, CS.DC)) {
        // Only try to insert a converting construction if the protocol is a
        // literal protocol and not some other known protocol.
        switch (kind) {
#define EXPRESSIBLE_BY_LITERAL_PROTOCOL_WITH_NAME(name, _) \
        case KnownProtocolKind::name: break;
#define PROTOCOL_WITH_NAME(name, _) \
        case KnownProtocolKind::name: return false;
#include "swift/AST/KnownProtocols.def"
        }
        convWrapBefore += toType->getString();
        convWrapBefore += "(";
        convWrapAfter += ")";
      }
      fixIt(convWrapBefore, convWrapAfter);
      return true;
    }
  }
  return false;
}

/// Try to add a fix-it when converting between a collection and its slice type,
/// such as String <-> Substring or (eventually) Array <-> ArraySlice
static bool trySequenceSubsequenceConversionFixIts(InFlightDiagnostic &diag,
                                                   ConstraintSystem &CS,
                                                   Type fromType, Type toType,
                                                   Expr *expr) {
  if (CS.TC.Context.getStdlibModule() == nullptr)
    return false;

  auto String = CS.TC.getStringType(CS.DC);
  auto Substring = CS.TC.getSubstringType(CS.DC);

  if (!String || !Substring)
    return false;

  /// FIXME: Remove this flag when void subscripts are implemented.
  /// Make this unconditional and remove the if statement.
  if (CS.TC.getLangOpts().FixStringToSubstringConversions) {
    // String -> Substring conversion
    // Add '[]' void subscript call to turn the whole String into a Substring
    if (fromType->isEqual(String)) {
      if (toType->isEqual(Substring)) {
        diag.fixItInsertAfter(expr->getEndLoc (), "[]");
        return true;
      }
    }
  }

  // Substring -> String conversion
  // Wrap in String.init
  if (fromType->isEqual(Substring)) {
    if (toType->isEqual(String)) {
      auto range = expr->getSourceRange();
      diag.fixItInsert(range.Start, "String(");
      diag.fixItInsertAfter(range.End, ")");
      return true;
    }
  }

  return false;
}

/// Attempts to add fix-its for these two mistakes:
///
/// - Passing an integer with the right type but which is getting wrapped with a
///   different integer type unnecessarily. The fixit removes the cast.
///
/// - Passing an integer but expecting different integer type. The fixit adds
///   a wrapping cast.
///
/// - Return true on the fixit is added, false otherwise.
///
/// This helps migration with SDK changes.
static bool tryIntegerCastFixIts(InFlightDiagnostic &diag, ConstraintSystem &CS,
                                 Type fromType, Type toType, Expr *expr) {
  if (!isIntegerType(fromType, CS) || !isIntegerType(toType, CS))
    return false;

  auto getInnerCastedExpr = [&]() -> Expr* {
    auto *CE = dyn_cast<CallExpr>(expr);
    if (!CE)
      return nullptr;
    if (!isa<ConstructorRefCallExpr>(CE->getFn()))
      return nullptr;
    auto *parenE = dyn_cast<ParenExpr>(CE->getArg());
    if (!parenE)
      return nullptr;
    return parenE->getSubExpr();
  };

  if (Expr *innerE = getInnerCastedExpr()) {
    Type innerTy = CS.getType(innerE);
    if (CS.TC.isConvertibleTo(innerTy, toType, CS.DC)) {
      // Remove the unnecessary cast.
      diag.fixItRemoveChars(expr->getLoc(), innerE->getStartLoc())
        .fixItRemove(expr->getEndLoc());
      return true;
    }
  }

  // Add a wrapping integer cast.
  std::string convWrapBefore = toType.getString();
  convWrapBefore += "(";
  std::string convWrapAfter = ")";
  SourceRange exprRange = expr->getSourceRange();
  diag.fixItInsert(exprRange.Start, convWrapBefore);
  diag.fixItInsertAfter(exprRange.End, convWrapAfter);
  return true;
}

static bool addTypeCoerceFixit(InFlightDiagnostic &diag, ConstraintSystem &CS,
                               Type fromType, Type toType, Expr *expr) {
  // Look through optional types; casts can add them, but can't remove extra
  // ones.
  toType = toType->lookThroughAllAnyOptionalTypes();

  CheckedCastKind Kind = CS.getTypeChecker().typeCheckCheckedCast(
      fromType, toType, CheckedCastContextKind::None, CS.DC, SourceLoc(),
      nullptr, SourceRange());
  if (Kind != CheckedCastKind::Unresolved) {
    SmallString<32> buffer;
    llvm::raw_svector_ostream OS(buffer);
    toType->print(OS);
    bool canUseAs = Kind == CheckedCastKind::Coercion ||
      Kind == CheckedCastKind::BridgingCoercion;
    diag.fixItInsert(
        Lexer::getLocForEndOfToken(CS.DC->getASTContext().SourceMgr,
                                   expr->getEndLoc()),
        (llvm::Twine(canUseAs ? " as " : " as! ") + OS.str()).str());
    return true;
  }
  return false;
}

/// Try to diagnose common errors involving implicitly non-escaping parameters
/// of function type, giving more specific and simpler diagnostics, attaching
/// notes on the parameter, and offering fixits to insert @escaping. Returns
/// true if it detects and issues an error, false if it does nothing.
static bool tryDiagnoseNonEscapingParameterToEscaping(
    Expr *expr, Type srcType, Type dstType, ContextualTypePurpose dstPurpose,
    ConstraintSystem &CS) {
  assert(expr);
  // Need to be referencing a parameter of function type
  auto declRef = dyn_cast<DeclRefExpr>(expr);
  if (!declRef || !isa<ParamDecl>(declRef->getDecl()) ||
      !CS.getType(declRef)->is<AnyFunctionType>())
    return false;

  // Must be from non-escaping function to escaping function. For the
  // destination type, we read through optionality to give better diagnostics in
  // the event of an implicit promotion.
  auto srcFT = srcType->getAs<AnyFunctionType>();
  auto dstFT =
      dstType->lookThroughAllAnyOptionalTypes()->getAs<AnyFunctionType>();

  if (!srcFT || !dstFT || !srcFT->isNoEscape() || dstFT->isNoEscape())
    return false;

  // Pick a specific diagnostic for the specific use
  auto paramDecl = cast<ParamDecl>(declRef->getDecl());
  switch (dstPurpose) {
  case CTP_CallArgument:
    CS.TC.diagnose(declRef->getLoc(), diag::passing_noescape_to_escaping,
                   paramDecl->getName());
    break;
  case CTP_AssignSource:
    CS.TC.diagnose(declRef->getLoc(), diag::assigning_noescape_to_escaping,
                   paramDecl->getName());
    break;

  default:
    CS.TC.diagnose(declRef->getLoc(), diag::general_noescape_to_escaping,
                   paramDecl->getName());
    break;
  }

  // Give a note and fixit
  InFlightDiagnostic note = CS.TC.diagnose(
      paramDecl->getLoc(), diag::noescape_parameter, paramDecl->getName());

  if (!srcFT->isAutoClosure()) {
    note.fixItInsert(paramDecl->getTypeLoc().getSourceRange().Start,
                     "@escaping ");
  } // TODO: add in a fixit for autoclosure

  return true;
}

bool FailureDiagnosis::diagnoseContextualConversionError(
    Expr *expr, Type contextualType, ContextualTypePurpose CTP) {
  // If the constraint system has a contextual type, then we can test to see if
  // this is the problem that prevents us from solving the system.
  if (!contextualType) {
    // This contextual conversion constraint doesn't install an actual type.
    if (CTP == CTP_CalleeResult)
      return diagnoseCalleeResultContextualConversionError();
 
    return false;
  }

  // Try re-type-checking the expression without the contextual type to see if
  // it can work without it.  If so, the contextual type is the problem.  We
  // force a recheck, because "expr" is likely in our table with the extra
  // contextual constraint that we know we are relaxing.
  TCCOptions options = TCC_ForceRecheck;
  if (contextualType->is<InOutType>())
    options |= TCC_AllowLValue;

  auto recheckedExpr = typeCheckChildIndependently(expr, options);
  auto exprType = recheckedExpr ? CS.getType(recheckedExpr) : Type();

  // If it failed and diagnosed something, then we're done.
  if (!exprType) return true;

  // If we contextually had an inout type, and got a non-lvalue result, then
  // we fail with a mutability error.
  if (contextualType->is<InOutType>() && !exprType->is<LValueType>()) {
    diagnoseSubElementFailure(recheckedExpr, recheckedExpr->getLoc(), CS,
                              diag::cannot_pass_rvalue_inout_subelement,
                              diag::cannot_pass_rvalue_inout);
    return true;
  }

  // Try to find the contextual type in a variety of ways.  If the constraint
  // system had a contextual type specified, we use it - it will have a purpose
  // indicator which allows us to give a very "to the point" diagnostic.
  Diag<Type, Type> diagID;
  Diag<Type, Type> diagIDProtocol;
  Diag<Type> nilDiag;
  std::function<void(void)> nilFollowup;

  // If this is conversion failure due to a return statement with an argument
  // that cannot be coerced to the result type of the function, emit a
  // specific error.
  switch (CTP) {
  case CTP_Unused:
  case CTP_CannotFail:
    llvm_unreachable("These contextual type purposes cannot fail with a "
                     "conversion type specified!");
  case CTP_CalleeResult:
    llvm_unreachable("CTP_CalleeResult does not actually install a "
                     "contextual type");
  case CTP_Initialization:
    diagID = diag::cannot_convert_initializer_value;
    diagIDProtocol = diag::cannot_convert_initializer_value_protocol;
    nilDiag = diag::cannot_convert_initializer_value_nil;
    nilFollowup = [this] {
      TypeRepr *patternTR = CS.getContextualTypeLoc().getTypeRepr();
      if (!patternTR)
        return;
      auto diag = diagnose(patternTR->getLoc(), diag::note_make_optional,
                           OptionalType::get(CS.getContextualType()));
      if (patternTR->isSimple()) {
        diag.fixItInsertAfter(patternTR->getEndLoc(), "?");
      } else {
        diag.fixItInsert(patternTR->getStartLoc(), "(");
        diag.fixItInsertAfter(patternTR->getEndLoc(), ")?");
      }
    };
    break;
  case CTP_ReturnStmt:
    // Special case the "conversion to void" case.
    if (contextualType->isVoid()) {
      diagnose(expr->getLoc(), diag::cannot_return_value_from_void_func)
        .highlight(expr->getSourceRange());
      return true;
    }

    diagID = diag::cannot_convert_to_return_type;
    diagIDProtocol = diag::cannot_convert_to_return_type_protocol;
    nilDiag = diag::cannot_convert_to_return_type_nil;
    break;
  case CTP_ThrowStmt: {
    if (isa<NilLiteralExpr>(expr->getValueProvidingExpr())) {
      diagnose(expr->getLoc(), diag::cannot_throw_nil);
      return true;
    }

    if (isUnresolvedOrTypeVarType(exprType) ||
        exprType->isEqual(contextualType))
      return false;

    // If we tried to throw the error code of an error type, suggest object
    // construction.
    auto &TC = CS.getTypeChecker();
    if (auto errorCodeProtocol =
            TC.Context.getProtocol(KnownProtocolKind::ErrorCodeProtocol)) {
      if (auto conformance =
              TC.conformsToProtocol(CS.getType(expr), errorCodeProtocol, CS.DC,
                                    ConformanceCheckFlags::InExpression)) {
        Type errorCodeType = CS.getType(expr);
        Type errorType =
          ProtocolConformanceRef::getTypeWitnessByName(errorCodeType, *conformance,
                                                       TC.Context.Id_ErrorType,
                                                       &TC)->getCanonicalType();
        if (errorType) {
          auto diag = diagnose(expr->getLoc(), diag::cannot_throw_error_code,
                               errorCodeType, errorType);
          if (auto unresolvedDot = dyn_cast<UnresolvedDotExpr>(expr)) {
            diag.fixItInsert(unresolvedDot->getDotLoc(), "(");
            diag.fixItInsertAfter(unresolvedDot->getEndLoc(), ")");
          }
          return true;
        }
      }
    }

    // The conversion destination of throw is always ErrorType (at the moment)
    // if this ever expands, this should be a specific form like () is for
    // return.
    diagnose(expr->getLoc(), diag::cannot_convert_thrown_type, exprType)
      .highlight(expr->getSourceRange());
    return true;
  }

  case CTP_EnumCaseRawValue:
    diagID = diag::cannot_convert_raw_initializer_value;
    diagIDProtocol = diag::cannot_convert_raw_initializer_value;
    nilDiag = diag::cannot_convert_raw_initializer_value_nil;
    break;
  case CTP_DefaultParameter:
    diagID = diag::cannot_convert_default_arg_value;
    diagIDProtocol = diag::cannot_convert_default_arg_value_protocol;
    nilDiag = diag::cannot_convert_default_arg_value_nil;
    break;

  case CTP_CallArgument:
    diagID = diag::cannot_convert_argument_value;
    diagIDProtocol = diag::cannot_convert_argument_value_protocol;
    nilDiag = diag::cannot_convert_argument_value_nil;
    break;
  case CTP_ClosureResult:
    diagID = diag::cannot_convert_closure_result;
    diagIDProtocol = diag::cannot_convert_closure_result_protocol;
    nilDiag = diag::cannot_convert_closure_result_nil;
    break;
  case CTP_ArrayElement:
    diagID = diag::cannot_convert_array_element;
    diagIDProtocol = diag::cannot_convert_array_element_protocol;
    nilDiag = diag::cannot_convert_array_element_nil;
    break;
  case CTP_DictionaryKey:
    diagID = diag::cannot_convert_dict_key;
    diagIDProtocol = diag::cannot_convert_dict_key_protocol;
    nilDiag = diag::cannot_convert_dict_key_nil;
    break;
  case CTP_DictionaryValue:
    diagID = diag::cannot_convert_dict_value;
    diagIDProtocol = diag::cannot_convert_dict_value_protocol;
    nilDiag = diag::cannot_convert_dict_value_nil;
    break;
  case CTP_CoerceOperand:
    diagID = diag::cannot_convert_coerce;
    diagIDProtocol = diag::cannot_convert_coerce_protocol;
    nilDiag = diag::cannot_convert_coerce_nil;
    break;
  case CTP_AssignSource:
    diagID = diag::cannot_convert_assign;
    diagIDProtocol = diag::cannot_convert_assign_protocol;
    nilDiag = diag::cannot_convert_assign_nil;
    break;
  }

  // If we're diagnostic an issue with 'nil', produce a specific diagnostic,
  // instead of uttering ExpressibleByNilLiteral.
  if (isa<NilLiteralExpr>(expr->getValueProvidingExpr())) {
    diagnose(expr->getLoc(), nilDiag, contextualType);
    if (nilFollowup)
      nilFollowup();
    return true;
  }
  
  // If we don't have a type for the expression, then we cannot use it in
  // conversion constraint diagnostic generation.  If the types match, then it
  // must not be the contextual type that is the problem.
  if (isUnresolvedOrTypeVarType(exprType) ||
      exprType->isEqual(contextualType)) {
    return false;
  }
  
  // If we're trying to convert something of type "() -> T" to T, then we
  // probably meant to call the value.
  if (auto srcFT = exprType->getAs<AnyFunctionType>()) {
    if (srcFT->getInput()->isVoid() &&
        !isUnresolvedOrTypeVarType(srcFT->getResult()) &&
        CS.TC.isConvertibleTo(srcFT->getResult(), contextualType, CS.DC)) {
      diagnose(expr->getLoc(), diag::missing_nullary_call, srcFT->getResult())
        .highlight(expr->getSourceRange())
        .fixItInsertAfter(expr->getEndLoc(), "()");
      return true;
    }
  }

  // If this is a conversion from T to () in a call argument context, it is
  // almost certainly an extra argument being passed in.
  if (CTP == CTP_CallArgument && contextualType->isVoid()) {
    diagnose(expr->getLoc(), diag::extra_argument_to_nullary_call)
      .highlight(expr->getSourceRange());
    return true;
  }
  
  // If we're trying to convert something to Bool, check to see if it is for
  // a known reason.
  if (contextualType->isBool() && diagnoseConversionToBool(expr, exprType))
    return true;
  
  exprType = exprType->getRValueType();

  // Special case of some common conversions involving Swift.String
  // indexes, catching cases where people attempt to index them with an integer.
  if (isIntegerToStringIndexConversion(exprType, contextualType, CS)) {
    diagnose(expr->getLoc(), diag::string_index_not_integer,
             exprType->getRValueType())
      .highlight(expr->getSourceRange());
    diagnose(expr->getLoc(), diag::string_index_not_integer_note);
    return true;
  }

  // When converting from T to [T] or UnsafePointer<T>, we can offer fixit to wrap
  // the expr with brackets.
  auto *genericType = contextualType->getAs<BoundGenericType>();
  if (genericType) {
    auto *contextDecl = genericType->getDecl();
    if (contextDecl == CS.TC.Context.getArrayDecl()) {
      for (Type arg : genericType->getGenericArgs()) {
        if (arg->isEqual(exprType)) {
          diagnose(expr->getLoc(), diagID, exprType, contextualType)
              .fixItInsert(expr->getStartLoc(), "[")
              .fixItInsert(Lexer::getLocForEndOfToken(CS.TC.Context.SourceMgr,
                                                      expr->getEndLoc()),
                           "]");
          return true;
        }
      }
    } else if (contextDecl == CS.TC.Context.getUnsafePointerDecl() ||
               contextDecl == CS.TC.Context.getUnsafeMutablePointerDecl() ||
               contextDecl == CS.TC.Context.getUnsafeRawPointerDecl() ||
               contextDecl == CS.TC.Context.getUnsafeMutableRawPointerDecl()) {
      for (Type arg : genericType->getGenericArgs()) {
        if (arg->isEqual(exprType) && CS.getType(expr)->hasLValueType()) {
          diagnose(expr->getLoc(), diagID, exprType, contextualType).
            fixItInsert(expr->getStartLoc(), "&");
          return true;
        }
      }
    }
  }

  // Try for better/more specific diagnostics for non-escaping to @escaping
  if (tryDiagnoseNonEscapingParameterToEscaping(expr, exprType, contextualType,
                                                CTP, CS))
    return true;

  // Don't attempt fixits if we have an unsolved type variable, since
  // the recovery path's recursion into the type checker via typeCheckCast()
  // will confuse matters.
  if (exprType->hasTypeVariable())
    return false;

  // When complaining about conversion to a protocol type, complain about
  // conformance instead of "conversion".
  if (contextualType->is<ProtocolType>() ||
      contextualType->is<ProtocolCompositionType>())
    diagID = diagIDProtocol;
  
  // Try to simplify irrelevant details of function types.  For example, if
  // someone passes a "() -> Float" function to a "() throws -> Int"
  // parameter, then uttering the "throws" may confuse them into thinking that
  // that is the problem, even though there is a clear subtype relation.
  if (auto srcFT = exprType->getAs<FunctionType>())
    if (auto destFT = contextualType->getAs<FunctionType>()) {
      auto destExtInfo = destFT->getExtInfo();
      
      if (!srcFT->isNoEscape()) destExtInfo = destExtInfo.withNoEscape(false);
      if (!srcFT->throws()) destExtInfo = destExtInfo.withThrows(false);
      if (destExtInfo != destFT->getExtInfo())
        contextualType = FunctionType::get(destFT->getInput(),
                                           destFT->getResult(), destExtInfo);

      // If this is a function conversion that discards throwability or
      // noescape, emit a specific diagnostic about that.
      if (srcFT->throws() && !destFT->throws())
        diagID = diag::throws_functiontype_mismatch;
      else if (srcFT->isNoEscape() && !destFT->isNoEscape())
        diagID = diag::noescape_functiontype_mismatch;
    }

  InFlightDiagnostic diag = diagnose(expr->getLoc(), diagID,
                                     exprType, contextualType);
  diag.highlight(expr->getSourceRange());

  // Try to convert between a sequence and its subsequence, notably
  // String <-> Substring.
  if (trySequenceSubsequenceConversionFixIts(diag, CS, exprType, contextualType,
                                             expr)) {
    return true;
  }

  // Attempt to add a fixit for the error.
  switch (CTP) {
  case CTP_CallArgument:
  case CTP_ArrayElement:
  case CTP_DictionaryKey:
  case CTP_DictionaryValue:
  case CTP_AssignSource:
  case CTP_Initialization:
  case CTP_ReturnStmt:
    tryRawRepresentableFixIts(diag, CS, exprType, contextualType,
                              KnownProtocolKind::ExpressibleByIntegerLiteral,
                              expr) ||
    tryRawRepresentableFixIts(diag, CS, exprType, contextualType,
                              KnownProtocolKind::ExpressibleByStringLiteral,
                              expr) ||
    tryIntegerCastFixIts(diag, CS, exprType, contextualType, expr) ||
    addTypeCoerceFixit(diag, CS, exprType, contextualType, expr);
    break;

  default:
    // FIXME: Other contextual conversions too?
    break;
  }

  return true;
}


/// When an assignment to an expression is detected and the destination is
/// invalid, emit a detailed error about the condition.
void ConstraintSystem::diagnoseAssignmentFailure(Expr *dest, Type destTy,
                                                 SourceLoc equalLoc) {
  auto &TC = getTypeChecker();

  // Diagnose obvious assignments to literals.
  if (isa<LiteralExpr>(dest->getValueProvidingExpr())) {
    TC.diagnose(equalLoc, diag::cannot_assign_to_literal);
    return;
  }

  // Diagnose assignments to let-properties in delegating initializers.
  if (auto *member = dyn_cast<UnresolvedDotExpr>(dest)) {
    if (auto *ctor = dyn_cast<ConstructorDecl>(DC)) {
      if (auto *baseRef = dyn_cast<DeclRefExpr>(member->getBase())) {
        if (baseRef->getDecl() == ctor->getImplicitSelfDecl() &&
            ctor->getDelegatingOrChainedInitKind(nullptr) ==
              ConstructorDecl::BodyInitKind::Delegating) {
          auto resolved = resolveImmutableBase(member, *this);
          assert(resolved.first == member);
          TC.diagnose(equalLoc, diag::assignment_let_property_delegating_init,
                      member->getName());
          if (resolved.second) {
            TC.diagnose(resolved.second, diag::decl_declared_here,
                        member->getName());
          }
          return;
        }
      }
    }
  }

  Diag<StringRef> diagID;
  if (isa<DeclRefExpr>(dest))
    diagID = diag::assignment_lhs_is_immutable_variable;
  else if (isa<ForceValueExpr>(dest))
    diagID = diag::assignment_bang_has_immutable_subcomponent;
  else if (isa<UnresolvedDotExpr>(dest) || isa<MemberRefExpr>(dest))
    diagID = diag::assignment_lhs_is_immutable_property;
  else if (isa<SubscriptExpr>(dest))
    diagID = diag::assignment_subscript_has_immutable_base;
  else {
    diagID = diag::assignment_lhs_is_immutable_variable;
  }

  diagnoseSubElementFailure(dest, equalLoc, *this, diagID,
                            diag::assignment_lhs_not_lvalue);
}

//===----------------------------------------------------------------------===//
// Diagnose assigning variable to itself.
//===----------------------------------------------------------------------===//

static Decl *findSimpleReferencedDecl(const Expr *E) {
  if (auto *LE = dyn_cast<LoadExpr>(E))
    E = LE->getSubExpr();

  if (auto *DRE = dyn_cast<DeclRefExpr>(E))
    return DRE->getDecl();

  return nullptr;
}

static std::pair<Decl *, Decl *> findReferencedDecl(const Expr *E) {
  E = E->getValueProvidingExpr();

  if (auto *LE = dyn_cast<LoadExpr>(E))
    return findReferencedDecl(LE->getSubExpr());

  if (auto *AE = dyn_cast<AssignExpr>(E))
    return findReferencedDecl(AE->getDest());

  if (auto *D = findSimpleReferencedDecl(E))
    return std::make_pair(nullptr, D);

  if (auto *MRE = dyn_cast<MemberRefExpr>(E)) {
    if (auto *BaseDecl = findSimpleReferencedDecl(MRE->getBase()))
      return std::make_pair(BaseDecl, MRE->getMember().getDecl());
  }

  return std::make_pair(nullptr, nullptr);
}

bool TypeChecker::diagnoseSelfAssignment(const Expr *E) {
  auto AE = dyn_cast<AssignExpr>(E);
  if (!AE)
    return false;

  auto LHSDecl = findReferencedDecl(AE->getDest());
  auto RHSDecl = findReferencedDecl(AE->getSrc());

  if (LHSDecl.second && LHSDecl == RHSDecl) {
    diagnose(AE->getLoc(), LHSDecl.first ? diag::self_assignment_prop
                                         : diag::self_assignment_var)
    .highlight(AE->getDest()->getSourceRange())
    .highlight(AE->getSrc()->getSourceRange());
    return true;
  }

  return false;
}

static bool isSymmetricBinaryOperator(const CalleeCandidateInfo &CCI) {
  // If we don't have at least one known candidate, don't trigger.
  if (CCI.candidates.empty()) return false;

  for (auto &candidate : CCI.candidates) {
    // Each candidate must be a non-assignment operator function.
    auto decl = dyn_cast_or_null<FuncDecl>(candidate.getDecl());
    if (!decl) return false;
    auto op = dyn_cast_or_null<InfixOperatorDecl>(decl->getOperatorDecl());
    if (!op || !op->getPrecedenceGroup() ||
        op->getPrecedenceGroup()->isAssignment())
      return false;

    // It must have exactly two parameters.
    auto params = decl->getParameterLists().back();
    if (params->size() != 2) return false;

    // Require the types to be the same.
    if (!params->get(0)->getInterfaceType()->isEqual(
          params->get(1)->getInterfaceType()))
      return false;
  }

  return true;
}

/// Determine whether any of the given callee candidates have a default value.
static bool candidatesHaveAnyDefaultValues(
    const CalleeCandidateInfo &candidates) {
  for (const auto &cand : candidates.candidates) {
    auto function = dyn_cast_or_null<AbstractFunctionDecl>(cand.getDecl());
    if (!function) continue;

    auto paramLists = function->getParameterLists();
    if (cand.level >= paramLists.size()) continue;

    auto paramList = paramLists[cand.level];
    for (auto param : *paramList) {
      if (param->getDefaultArgumentKind() != DefaultArgumentKind::None)
        return true;
    }
  }

  return false;
}

/// Find the tuple element that can be initialized by a scalar.
static Optional<unsigned> getElementForScalarInitOfArg(
    const TupleType *tupleTy,
    const CalleeCandidateInfo &candidates) {
  // Empty tuples cannot be initialized with a scalar.
  if (tupleTy->getNumElements() == 0) return None;
  
  auto getElementForScalarInitSimple =
      [](const TupleType *tupleTy) -> Optional<unsigned> {
    int index = tupleTy->getElementForScalarInit();
    if (index < 0) return None;
    return index;    
  };

  // If there aren't any candidates, we're done.
  if (candidates.empty()) return getElementForScalarInitSimple(tupleTy);

  // Dig out the candidate.
  const auto &cand = candidates[0];
  auto function = dyn_cast_or_null<AbstractFunctionDecl>(cand.getDecl());
  if (!function) return getElementForScalarInitSimple(tupleTy);

  auto paramLists = function->getParameterLists();
  if (cand.level >= paramLists.size())
    return getElementForScalarInitSimple(tupleTy);

  auto paramList = paramLists[cand.level];
  if (tupleTy->getNumElements() != paramList->size()) 
    return getElementForScalarInitSimple(tupleTy);

  // Find a tuple element without a default.
  Optional<unsigned> elementWithoutDefault;
  for (unsigned i : range(tupleTy->getNumElements())) {
    auto param = paramList->get(i);

    // Skip parameters with default arguments.
    if (param->getDefaultArgumentKind() != DefaultArgumentKind::None)
      continue;

    // If we already have an element without a default, check whether there are
    // two fields that need initialization.
    if (elementWithoutDefault) {
      // Variadic fields are okay; they'll just end up being empty.
      if (param->isVariadic()) continue;

      // If the element we saw before was variadic, it can be empty as well.
      auto priorParam = paramList->get(*elementWithoutDefault);
      if (!priorParam->isVariadic()) return None;
    }

    elementWithoutDefault = i;
  }

  if (elementWithoutDefault) return elementWithoutDefault;

  // All of the fields have default values; initialize the first one.
  return 0;
}

/// Return true if the argument of a CallExpr (or related node) has a trailing
/// closure.
static bool callArgHasTrailingClosure(Expr *E) {
  if (!E) return false;
  if (auto *PE = dyn_cast<ParenExpr>(E))
    return PE->hasTrailingClosure();
  else if (auto *TE = dyn_cast<TupleExpr>(E))
    return TE->hasTrailingClosure();
  return false;
}

/// Special magic to handle inout exprs and tuples in argument lists.
Expr *FailureDiagnosis::
typeCheckArgumentChildIndependently(Expr *argExpr, Type argType,
                                    const CalleeCandidateInfo &candidates,
                                    TCCOptions options) {
  // Grab one of the candidates (if present) and get its input list to help
  // identify operators that have implicit inout arguments.
  Type exampleInputType;
  if (!candidates.empty()) {
    exampleInputType = candidates[0].getArgumentType();

    // If we found a single candidate, and have no contextually known argument
    // type information, use that one candidate as the type information for
    // subexpr checking.
    //
    // TODO: If all candidates have the same type for some argument, we could
    // pass down partial information.
    if (candidates.size() == 1 && !argType)
      argType = candidates[0].getArgumentType();
  }
  
  // If our candidates are instance members at curry level #0, then the argument
  // being provided is the receiver type for the instance.  We produce better
  // diagnostics when we don't force the self type down.
  if (argType && !candidates.empty())
    if (auto decl = candidates[0].getDecl())
      if (decl->isInstanceMember() && candidates[0].level == 0 &&
          !isa<SubscriptDecl>(decl))
        argType = Type();

  // Similarly, we get better results when we don't push argument types down
  // to symmetric operators.
  if (argType && isSymmetricBinaryOperator(candidates))
    argType = Type();
  

  // FIXME: This should all just be a matter of getting the type of the
  // sub-expression, but this doesn't work well when typeCheckChildIndependently
  // is over-conservative w.r.t. TupleExprs.
  auto *TE = dyn_cast<TupleExpr>(argExpr);
  if (!TE) {
    // If the argument isn't a tuple, it is some scalar value for a
    // single-argument call.
    if (exampleInputType && exampleInputType->is<InOutType>())
      options |= TCC_AllowLValue;

    // If the argtype is a tuple type with default arguments, or a labeled tuple
    // with a single element, pull the scalar element type for the subexpression
    // out.  If we can't do that and the tuple has default arguments, we have to
    // punt on passing down the type information, since type checking the
    // subexpression won't be able to find the default argument provider.
    if (argType) {
      if (auto argTT = argType->getAs<TupleType>()) {
        if (auto scalarElt = getElementForScalarInitOfArg(argTT, candidates)) {
          // If we found the single argument being initialized, use it.
          auto &arg = argTT->getElement(*scalarElt);
          
          // If the argument being specified is actually varargs, then we're
          // just specifying one element of a variadic list.  Use the type of
          // the individual varargs argument, not the overall array type.
          if (arg.isVararg())
            argType = arg.getVarargBaseTy();
          else
            argType = arg.getType();
        } else if (candidatesHaveAnyDefaultValues(candidates)) {
          argType = Type();
        }
      } else if (candidatesHaveAnyDefaultValues(candidates)) {
        argType = Type();
      }
    }

    auto CTPurpose = argType ? CTP_CallArgument : CTP_Unused;
    return typeCheckChildIndependently(argExpr, argType, CTPurpose, options);
  }

  // If we know the requested argType to use, use computeTupleShuffle to produce
  // the shuffle of input arguments to destination values.  It requires a
  // TupleType to compute the mapping from argExpr.  Conveniently, it doesn't
  // care about the actual types though, so we can just use 'void' for them.
  // FIXME: This doesn't need to be limited to tuple types.
  if (argType && argType->is<TupleType>()) {
    // Decompose the parameter type.
    SmallVector<AnyFunctionType::Param, 4> params;
    AnyFunctionType::decomposeInput(argType, params);
    
    // If we have a candidate function around, compute the position of its
    // default arguments.
    SmallVector<bool, 4> defaultMap;
    if (candidates.empty()) {
      defaultMap.assign(params.size(), false);
    } else {
      computeDefaultMap(argType, candidates[0].getDecl(),
                        candidates[0].level, defaultMap);
    }

    // Form a set of call arguments, using a dummy type (Void), because the
    // argument/parameter matching code doesn't need it.
    auto voidTy = CS.getASTContext().TheEmptyTupleType;
    SmallVector<AnyFunctionType::Param, 4> args;
    for (unsigned i = 0, e = TE->getNumElements(); i != e; ++i) {
      args.push_back(AnyFunctionType::Param(voidTy, TE->getElementName(i), {}));
    }

    /// Use a match call argument listener that allows relabeling.
    struct RelabelMatchCallArgumentListener : MatchCallArgumentListener {
      bool relabelArguments(ArrayRef<Identifier> newNames) override {
        return false;
      }
    } listener;

    SmallVector<ParamBinding, 4> paramBindings;
    if (!matchCallArguments(args, params, defaultMap,
                            callArgHasTrailingClosure(argExpr),
                            /*allowFixes=*/true,
                            listener, paramBindings)) {
      SmallVector<Expr*, 4> resultElts(TE->getNumElements(), nullptr);
      SmallVector<TupleTypeElt, 4> resultEltTys(TE->getNumElements(), voidTy);

      // Perform analysis of the input elements.
      for (unsigned paramIdx : range(paramBindings.size())) {
        // Extract the parameter.
        const auto &param = params[paramIdx];

        // Determine the parameter type.
        if (param.isInOut())
          options |= TCC_AllowLValue;

        // Look at each of the arguments assigned to this parameter.
        auto currentParamType = param.getType();
        for (auto inArgNo : paramBindings[paramIdx]) {
          // Determine the argument type.
          auto currentArgType = TE->getElement(inArgNo);

          auto exprResult =
            typeCheckChildIndependently(currentArgType, currentParamType,
                                        CTP_CallArgument, options);

          // If there was an error type checking this argument, then we're done.
          if (!exprResult)
            return nullptr;

          // If the caller expected something inout, but we didn't have
          // something of inout type, diagnose it.
          if (auto IOE =
                dyn_cast<InOutExpr>(exprResult->getSemanticsProvidingExpr())) {
            if (!param.isInOut()) {
              diagnose(exprResult->getLoc(), diag::extra_address_of,
                       CS.getType(exprResult)->getInOutObjectType())
                  .highlight(exprResult->getSourceRange())
                  .fixItRemove(IOE->getStartLoc());
              return nullptr;
            }
          }

          auto resultTy = CS.getType(exprResult);
          resultElts[inArgNo] = exprResult;
          resultEltTys[inArgNo] = {resultTy->getInOutObjectType(),
                                   TE->getElementName(inArgNo),
                                   ParameterTypeFlags().withInOut(resultTy->is<InOutType>())};
        }
      }

      auto TT = TupleType::get(resultEltTys, CS.getASTContext());
      return CS.cacheType(TupleExpr::create(
          CS.getASTContext(), TE->getLParenLoc(), resultElts,
          TE->getElementNames(), TE->getElementNameLocs(), TE->getRParenLoc(),
          TE->hasTrailingClosure(), TE->isImplicit(), TT));
    }
  }
  
  // Get the simplified type of each element and rebuild the aggregate.
  SmallVector<TupleTypeElt, 4> resultEltTys;
  SmallVector<Expr*, 4> resultElts;

  TupleType *exampleInputTuple = nullptr;
  if (exampleInputType)
    exampleInputTuple = exampleInputType->getAs<TupleType>();

  for (unsigned i = 0, e = TE->getNumElements(); i != e; i++) {
    if (exampleInputTuple && i < exampleInputTuple->getNumElements() &&
        exampleInputTuple->getElement(i).isInOut())
      options |= TCC_AllowLValue;

    auto elExpr = typeCheckChildIndependently(TE->getElement(i), options);
    if (!elExpr) return nullptr; // already diagnosed.
    
    resultElts.push_back(elExpr);
    auto resFlags =
        ParameterTypeFlags().withInOut(elExpr->isSemanticallyInOutExpr());
    resultEltTys.push_back({CS.getType(elExpr)->getInOutObjectType(),
                            TE->getElementName(i), resFlags});
  }

  auto TT = TupleType::get(resultEltTys, CS.getASTContext());
  return CS.cacheType(TupleExpr::create(
      CS.getASTContext(), TE->getLParenLoc(), resultElts, TE->getElementNames(),
      TE->getElementNameLocs(), TE->getRParenLoc(), TE->hasTrailingClosure(),
      TE->isImplicit(), TT));
}

static bool diagnoseImplicitSelfErrors(Expr *fnExpr, Expr *argExpr,
                                       CalleeCandidateInfo &CCI,
                                       ArrayRef<Identifier> argLabels,
                                       ConstraintSystem &CS) {
  // If candidate list is empty it means that problem is somewhere else,
  // since we need to have candidates which might be shadowing other funcs.
  if (CCI.empty() || !CCI[0].getDecl())
    return false;

  auto &TC = CS.TC;
  // Call expression is formed as 'foo.bar' where 'foo' might be an
  // implicit "Self" reference, such use wouldn't provide good diagnostics
  // for situations where instance members have equal names to functions in
  // Swift Standard Library e.g. min/max.
  auto UDE = dyn_cast<UnresolvedDotExpr>(fnExpr);
  if (!UDE)
    return false;

  auto baseExpr = dyn_cast<DeclRefExpr>(UDE->getBase());
  if (!baseExpr)
    return false;

  auto baseDecl = baseExpr->getDecl();
  if (!baseExpr->isImplicit() || baseDecl->getFullName() != TC.Context.Id_self)
    return false;

  // Our base expression is an implicit 'self.' reference e.g.
  //
  // extension Sequence {
  //   func test() -> Int {
  //     return max(1, 2)
  //   }
  // }
  //
  // In this example the Sequence class already has two methods named 'max'
  // none of which accept two arguments, but there is a function in
  // Swift Standard Library called 'max' which does accept two arguments,
  // so user might have called that by mistake without realizing that
  // compiler would add implicit 'self.' prefix to the call of 'max'.
  ExprCleaner cleanup(argExpr);

  auto argType = CS.getType(argExpr);
  // If argument wasn't properly type-checked, let's retry without changing AST.
  if (!argType || argType->hasUnresolvedType() || argType->hasTypeVariable() ||
      argType->hasTypeParameter()) {
    auto *argTuple = dyn_cast<TupleExpr>(argExpr);
    if (!argTuple) {
      // Bail out if we don't have a well-formed argument list.
      return false;
    }
    
    // Let's type check individual argument expressions without any
    // contextual information to try to recover an argument type that
    // matches what the user actually wrote instead of what the typechecker
    // expects.
    SmallVector<TupleTypeElt, 4> elts;
    for (unsigned i = 0, e = argTuple->getNumElements(); i < e; ++i) {
      ConcreteDeclRef ref = nullptr;
      auto *el = argTuple->getElement(i);
      auto typeResult =
        TC.getTypeOfExpressionWithoutApplying(el, CS.DC, ref);
      if (!typeResult)
        return false;
      auto flags = ParameterTypeFlags().withInOut(typeResult->is<InOutType>());
      elts.push_back(TupleTypeElt(typeResult->getInOutObjectType(),
                                  argTuple->getElementName(i),
                                  flags));
    }

    argType = TupleType::get(elts, CS.getASTContext());
  }

  auto typeKind = argType->getKind();
  if (typeKind != TypeKind::Tuple && typeKind != TypeKind::Paren)
    return false;

  // If argument type couldn't be properly resolved or has errors,
  // we can't diagnose anything in here, it points to the different problem.
  if (isUnresolvedOrTypeVarType(argType) || argType->hasError())
    return false;

  auto context = CS.DC;
  using CandidateMap =
      llvm::SmallDenseMap<ValueDecl *, llvm::SmallVector<OverloadChoice, 2>>;

  auto getBaseKind = [](ValueDecl *base) -> DescriptiveDeclKind {
    DescriptiveDeclKind kind = DescriptiveDeclKind::Module;
    if (!base)
      return kind;

    auto context = base->getDeclContext();
    do {
      if (isa<ExtensionDecl>(context))
        return DescriptiveDeclKind::Extension;

      if (auto nominal = dyn_cast<NominalTypeDecl>(context)) {
        kind = nominal->getDescriptiveKind();
        break;
      }

      context = context->getParent();
    } while (context);

    return kind;
  };

  auto getBaseName = [](DeclContext *context) -> DeclName {
    if (auto generic =
          context->getAsNominalTypeOrNominalTypeExtensionContext()) {
      return generic->getName();
    } else if (context->isModuleScopeContext())
      return context->getParentModule()->getName();
    else
      llvm_unreachable("Unsupported base");
  };

  auto diagnoseShadowing = [&](ValueDecl *base,
                               ArrayRef<OverloadChoice> candidates) -> bool {
    CalleeCandidateInfo calleeInfo(base ? base->getInterfaceType() : nullptr,
                                   candidates, CCI.hasTrailingClosure, CS,
                                   base);

    calleeInfo.filterList(argType, argLabels);

    auto diagnostic = diag::member_shadows_global_function_near_match;
    switch (calleeInfo.closeness) {
    case CC_Unavailable:
    case CC_Inaccessible:
    case CC_SelfMismatch:
    case CC_ArgumentLabelMismatch:
    case CC_ArgumentCountMismatch:
    case CC_GeneralMismatch:
      return false;

    case CC_NonLValueInOut:
    case CC_OneArgumentNearMismatch:
    case CC_OneArgumentMismatch:
    case CC_OneGenericArgumentNearMismatch:
    case CC_OneGenericArgumentMismatch:
    case CC_ArgumentNearMismatch:
    case CC_ArgumentMismatch:
    case CC_GenericNonsubstitutableMismatch:
      break; // Near match cases

    case CC_ExactMatch:
      diagnostic = diag::member_shadows_global_function;
      break;
    }

    auto choice = calleeInfo.candidates[0].getDecl();
    auto baseKind = getBaseKind(base);
    auto baseName = getBaseName(choice->getDeclContext());

    auto origCandidate = CCI[0].getDecl();
    TC.diagnose(UDE->getLoc(), diagnostic, UDE->getName(),
                origCandidate->getDescriptiveKind(),
                origCandidate->getFullName(), choice->getDescriptiveKind(),
                choice->getFullName(), baseKind, baseName);

    auto topLevelDiag = diag::fix_unqualified_access_top_level;
    if (baseKind == DescriptiveDeclKind::Module)
      topLevelDiag = diag::fix_unqualified_access_top_level_multi;

    auto name = baseName.getBaseIdentifier();
    SmallString<32> namePlusDot = name.str();
    namePlusDot.push_back('.');

    TC.diagnose(UDE->getLoc(), topLevelDiag, namePlusDot,
                choice->getDescriptiveKind(), name)
        .fixItInsert(UDE->getStartLoc(), namePlusDot);

    for (auto &candidate : calleeInfo.candidates) {
      if (auto decl = candidate.getDecl())
        TC.diagnose(decl, diag::decl_declared_here, decl->getFullName());
    }

    return true;
  };

  // For each of the parent contexts, let's try to find any candidates
  // which have the same name and the same number of arguments as callee.
  while (context->getParent()) {
    auto result = TC.lookupUnqualified(context, UDE->getName(), UDE->getLoc());
    context = context->getParent();

    if (!result || result.empty())
      continue;

    CandidateMap candidates;
    for (const auto &candidate : result) {
      auto base = candidate.getBaseDecl();
      auto decl = candidate.getValueDecl();
      if ((base && base->isInvalid()) || decl->isInvalid())
        continue;

      // If base is present but it doesn't represent a valid nominal,
      // we can't use current candidate as one of the choices.
      if (base && !base->getInterfaceType()->getNominalOrBoundGenericNominal())
        continue;

      auto context = decl->getDeclContext();
      // We are only interested in static or global functions, because
      // there is no way to call anything else properly.
      if (!decl->isStatic() && !context->isModuleScopeContext())
        continue;

      OverloadChoice choice(base ? base->getInterfaceType() : nullptr,
                            decl, UDE->getFunctionRefKind());

      if (base) { // Let's group all of the candidates have a common base.
        candidates[base].push_back(choice);
        continue;
      }

      // If there is no base, it means this is one of the global functions,
      // let's try to diagnose its shadowing inline.
      if (diagnoseShadowing(base, choice))
        return true;
    }

    if (candidates.empty())
      continue;

    for (const auto &candidate : candidates) {
      if (diagnoseShadowing(candidate.getFirst(), candidate.getSecond()))
        return true;
    }
  }

  return false;
}

// It is a somewhat common error to try to access an instance method as a
// curried member on the type, instead of using an instance, e.g. the user
// wrote:
//
//   Foo.doThing(42, b: 19)
//
// instead of:
//
//   myFoo.doThing(42, b: 19)
//
// Check for this situation and handle it gracefully.
static bool
diagnoseInstanceMethodAsCurriedMemberOnType(CalleeCandidateInfo &CCI,
                                            Expr *fnExpr, Expr *argExpr) {
  for (auto &candidate : CCI.candidates) {
    auto argTy = candidate.getArgumentType();
    if (!argTy)
      return false;

    auto *decl = candidate.getDecl();
    if (!decl)
      return false;

    // If this is an exact match at the level 1 of the parameters, but
    // there is still something wrong with the expression nevertheless
    // it might be worth while to check if it's instance method as curried
    // member of type problem.
    if (CCI.closeness == CC_ExactMatch &&
        (decl->isInstanceMember() && candidate.level == 1))
      continue;

    auto params = candidate.getUncurriedFunctionType()->getParams();
    SmallVector<bool, 4> defaultMap;
    computeDefaultMap(argTy, decl, candidate.level, defaultMap);
    // If one of the candidates is an instance method with a single parameter
    // at the level 0, this might be viable situation for calling instance
    // method as curried member of type problem.
    if (params.size() != 1 || !decl->isInstanceMember() || candidate.level > 0)
      return false;
  }

  auto &TC = CCI.CS.TC;

  if (auto UDE = dyn_cast<UnresolvedDotExpr>(fnExpr)) {
    auto baseExpr = UDE->getBase();
    auto baseType = CCI.CS.getType(baseExpr);
    if (auto *MT = baseType->getAs<MetatypeType>()) {
      auto DC = CCI.CS.DC;
      auto instanceType = MT->getInstanceType();

      // If the base is an implicit self type reference, and we're in a
      // an initializer, then the user wrote something like:
      //
      //   class Foo { let val = initFn() }
      // or
      //   class Bar { func something(x: Int = initFn()) }
      //
      // which runs in type context, not instance context.  Produce a tailored
      // diagnostic since this comes up and is otherwise non-obvious what is
      // going on.
      if (baseExpr->isImplicit() && isa<Initializer>(DC)) {
        auto *TypeDC = DC->getParent();
        bool propertyInitializer = true;
        // If the parent context is not a type context, we expect it
        // to be a defaulted parameter in a function declaration.
        if (!TypeDC->isTypeContext()) {
          assert(TypeDC->getContextKind() ==
                     DeclContextKind::AbstractFunctionDecl &&
                 "Expected function decl context for initializer!");
          TypeDC = TypeDC->getParent();
          propertyInitializer = false;
        }
        assert(TypeDC->isTypeContext() && "Expected type decl context!");

        if (TypeDC->getAsNominalTypeOrNominalTypeExtensionContext() ==
            instanceType->getAnyNominal()) {
          if (propertyInitializer)
            TC.diagnose(UDE->getLoc(), diag::instance_member_in_initializer,
                        UDE->getName());
          else
            TC.diagnose(UDE->getLoc(),
                        diag::instance_member_in_default_parameter,
                        UDE->getName());
          return true;
        }
      }

      // If this is a situation like this `self.foo(A())()` and self != A
      // let's say that `self` is not convertible to A.
      if (auto nominalType = CCI.CS.getType(argExpr)->getAs<NominalType>()) {
        if (!instanceType->isEqual(nominalType)) {
          TC.diagnose(argExpr->getStartLoc(), diag::types_not_convertible,
                      false, nominalType, instanceType);
          return true;
        }
      }

      // Otherwise, complain about use of instance value on type.
      if (isa<TypeExpr>(baseExpr)) {
        TC.diagnose(UDE->getLoc(), diag::instance_member_use_on_type,
                    instanceType, UDE->getName())
          .highlight(baseExpr->getSourceRange());
      } else {
        TC.diagnose(UDE->getLoc(), diag::could_not_use_instance_member_on_type,
                    instanceType, UDE->getName(), instanceType, false)
          .highlight(baseExpr->getSourceRange());
      }
      return true;
    }
  }

  return false;
}

static bool diagnoseTupleParameterMismatch(CalleeCandidateInfo &CCI,
                                           Type paramType, Type argType,
                                           Expr *fnExpr, Expr *argExpr,
                                           bool isTopLevel = true) {
  // Try to diagnose function call tuple parameter splat only if
  // there is no trailing or argument closure, because
  // FailureDiagnosis::visitClosureExpr will produce better
  // diagnostic and fix-it for trailing closure case.
  if (isTopLevel) {
    if (CCI.hasTrailingClosure)
      return false;

    if (auto *parenExpr = dyn_cast<ParenExpr>(argExpr)) {
      if (isa<ClosureExpr>(parenExpr->getSubExpr()))
        return false;
    }
  }

  if (auto *paramFnType = paramType->getAs<AnyFunctionType>()) {
    // Only if both of the parameter and argument types are functions
    // let's recur into diagnosing their arguments.
    if (auto *argFnType = argType->getAs<AnyFunctionType>())
      return diagnoseTupleParameterMismatch(CCI, paramFnType->getInput(),
                                            argFnType->getInput(), fnExpr,
                                            argExpr, /* isTopLevel */ false);
    return false;
  }

  unsigned parameterCount = 1, argumentCount = 1;

  // Don't try to desugar ParenType which is going to result in incorrect
  // inferred argument/parameter count.

  if (auto *paramTypeTy = dyn_cast<TupleType>(paramType.getPointer()))
    parameterCount = paramTypeTy->getNumElements();

  if (auto *argTupleTy = dyn_cast<TupleType>(argType.getPointer()))
    argumentCount = argTupleTy->getNumElements();

  if (parameterCount == 1 && argumentCount > 1) {
    // Let's see if inferred argument is actually a tuple inside of Paren.
    auto *paramTupleTy = paramType->getAs<TupleType>();
    if (!paramTupleTy)
      return false;

    // Looks like the number of tuple elements matches number
    // of function arguments, which means we can we can emit an
    // error about an attempt to make use of tuple splat or tuple
    // destructuring, unfortunately we can't provide a fix-it for
    // this case.
    if (paramTupleTy->getNumElements() == argumentCount) {
      auto &TC = CCI.CS.TC;
      if (isTopLevel) {
        if (auto *decl = CCI[0].getDecl()) {
          Identifier name;
          auto kind = decl->getDescriptiveKind();
          // Constructors/descructors and subscripts don't really have names.
          if (!(isa<ConstructorDecl>(decl) || isa<DestructorDecl>(decl) ||
                isa<SubscriptDecl>(decl))) {
            name = decl->getBaseName().getIdentifier();
          }

          TC.diagnose(argExpr->getLoc(), diag::single_tuple_parameter_mismatch,
                      kind, name, paramType, !name.empty())
              .highlight(argExpr->getSourceRange())
              .fixItInsertAfter(argExpr->getStartLoc(), "(")
              .fixItInsert(argExpr->getEndLoc(), ")");
        } else {
          TC.diagnose(argExpr->getLoc(),
                      diag::unknown_single_tuple_parameter_mismatch, paramType)
              .highlight(argExpr->getSourceRange())
              .fixItInsertAfter(argExpr->getStartLoc(), "(")
              .fixItInsert(argExpr->getEndLoc(), ")");
        }
      } else {
        TC.diagnose(argExpr->getLoc(),
                    diag::nested_tuple_parameter_destructuring, paramType,
                    CCI.CS.getType(fnExpr));
      }
      return true;
    }
  }

  return false;
}

class ArgumentMatcher : public MatchCallArgumentListener {
  TypeChecker &TC;
  Expr *FnExpr;
  Expr *ArgExpr;
  ArrayRef<AnyFunctionType::Param> &Parameters;
  SmallVectorImpl<bool> &DefaultMap;
  SmallVectorImpl<AnyFunctionType::Param> &Arguments;

  CalleeCandidateInfo CandidateInfo;

  // Indicates if problem has been found and diagnostic was emitted.
  bool Diagnosed = false;
  // Indicates if functions we are trying to call is a subscript.
  bool IsSubscript;

  // Stores parameter bindings determined by call to matchCallArguments.
  SmallVector<ParamBinding, 4> Bindings;

public:
  ArgumentMatcher(Expr *fnExpr, Expr *argExpr,
                  ArrayRef<AnyFunctionType::Param> &params,
                  SmallVectorImpl<bool> &defaultMap,
                  SmallVectorImpl<AnyFunctionType::Param> &args,
                  CalleeCandidateInfo &CCI, bool isSubscript)
      : TC(CCI.CS.TC), FnExpr(fnExpr), ArgExpr(argExpr), Parameters(params),
        DefaultMap(defaultMap), Arguments(args), CandidateInfo(CCI),
        IsSubscript(isSubscript) {}

  void extraArgument(unsigned extraArgIdx) override {
    auto name = Arguments[extraArgIdx].getLabel();
    Expr *arg = ArgExpr;

    auto tuple = dyn_cast<TupleExpr>(ArgExpr);
    if (tuple)
      arg = tuple->getElement(extraArgIdx);

    auto loc = arg->getLoc();
    if (tuple && extraArgIdx == tuple->getNumElements() - 1 &&
        tuple->hasTrailingClosure())
      TC.diagnose(loc, diag::extra_trailing_closure_in_call)
          .highlight(arg->getSourceRange());
    else if (Parameters.empty()) {
      auto Paren = dyn_cast<ParenExpr>(ArgExpr);
      Expr *SubExpr = nullptr;
      if (Paren) {
        SubExpr = Paren->getSubExpr();
      }

      if (SubExpr && CandidateInfo.CS.getType(SubExpr) &&
          CandidateInfo.CS.getType(SubExpr)->isVoid()) {
        TC.diagnose(loc, diag::extra_argument_to_nullary_call)
            .fixItRemove(SubExpr->getSourceRange());
      } else {
        TC.diagnose(loc, diag::extra_argument_to_nullary_call)
            .highlight(ArgExpr->getSourceRange());
      }
    } else if (name.empty())
      TC.diagnose(loc, diag::extra_argument_positional)
          .highlight(arg->getSourceRange());
    else
      TC.diagnose(loc, diag::extra_argument_named, name)
          .highlight(arg->getSourceRange());

    Diagnosed = true;
  }

  void missingArgument(unsigned missingParamIdx) override {
    auto &param = Parameters[missingParamIdx];
    Identifier name = param.getLabel();

    // Search insertion index.
    unsigned argIdx = 0;
    for (int Idx = missingParamIdx - 1; Idx >= 0; --Idx) {
      if (Bindings[Idx].empty())
        continue;
      argIdx = Bindings[Idx].back() + 1;
      break;
    }

    unsigned insertableEndIdx = Arguments.size();
    if (CandidateInfo.hasTrailingClosure)
      insertableEndIdx -= 1;

    // Build argument string for fix-it.
    SmallString<32> insertBuf;
    llvm::raw_svector_ostream insertText(insertBuf);

    if (argIdx != 0)
      insertText << ", ";
    if (!name.empty())
      insertText << name.str() << ": ";
    Type Ty = param.getType();
    // Explode inout type.
    if (param.isInOut()) {
      insertText << "&";
      Ty = param.getType()->getInOutObjectType();
    }
    // @autoclosure; the type should be the result type.
    if (auto FT = param.getType()->getAs<AnyFunctionType>())
      if (FT->isAutoClosure())
        Ty = FT->getResult();
    insertText << "<#" << Ty << "#>";
    if (argIdx == 0 && insertableEndIdx != 0)
      insertText << ", ";

    SourceLoc insertLoc;
    if (argIdx > insertableEndIdx) {
      // Unreachable for now.
      // FIXME: matchCallArguments() doesn't detect "missing argument after
      // trailing closure". E.g.
      //   func fn(x: Int, y: () -> Int, z: Int) { ... }
      //   fn(x: 1) { return 1 }
      // is diagnosed as "missing argument for 'y'" (missingParamIdx 1).
      // It should be "missing argument for 'z'" (missingParamIdx 2).
    } else if (auto *TE = dyn_cast<TupleExpr>(ArgExpr)) {
      // fn():
      //   fn([argMissing])
      // fn(argX, argY):
      //   fn([argMissing, ]argX, argY)
      //   fn(argX[, argMissing], argY)
      //   fn(argX, argY[, argMissing])
      // fn(argX) { closure }:
      //   fn([argMissing, ]argX) { closure }
      //   fn(argX[, argMissing]) { closure }
      //   fn(argX[, closureLabel: ]{closure}[, argMissing)] // Not impl.
      if (insertableEndIdx == 0)
        insertLoc = TE->getRParenLoc();
      else if (argIdx != 0)
        insertLoc = Lexer::getLocForEndOfToken(
            TC.Context.SourceMgr, TE->getElement(argIdx - 1)->getEndLoc());
      else {
        insertLoc = TE->getElementNameLoc(0);
        if (insertLoc.isInvalid())
          insertLoc = TE->getElement(0)->getStartLoc();
      }
    } else if (auto *PE = dyn_cast<ParenExpr>(ArgExpr)) {
      assert(argIdx <= 1);
      if (PE->getRParenLoc().isValid()) {
        // fn(argX):
        //   fn([argMissing, ]argX)
        //   fn(argX[, argMissing])
        // fn() { closure }:
        //   fn([argMissing]) {closure}
        //   fn([closureLabel: ]{closure}[, argMissing]) // Not impl.
        if (insertableEndIdx == 0)
          insertLoc = PE->getRParenLoc();
        else if (argIdx == 0)
          insertLoc = PE->getSubExpr()->getStartLoc();
        else
          insertLoc = Lexer::getLocForEndOfToken(TC.Context.SourceMgr,
                                                 PE->getSubExpr()->getEndLoc());
      } else {
        // fn { closure }:
        //   fn[(argMissing)] { closure }
        //   fn[(closureLabel:] { closure }[, missingArg)]  // Not impl.
        assert(!IsSubscript && "bracket less subscript");
        assert(PE->hasTrailingClosure() &&
               "paren less ParenExpr without trailing closure");
        insertBuf.insert(insertBuf.begin(), '(');
        insertBuf.insert(insertBuf.end(), ')');
        insertLoc = Lexer::getLocForEndOfToken(TC.Context.SourceMgr,
                                               FnExpr->getEndLoc());
      }
    } else {
      auto &CS = CandidateInfo.CS;
      (void)CS;
      // FIXME: Due to a quirk of CSApply, we can end up without a
      // ParenExpr if the argument has an '@lvalue TupleType'.
      assert((isa<TupleType>(CS.getType(ArgExpr).getPointer()) ||
              isa<ParenType>(CS.getType(ArgExpr).getPointer())) &&
             "unexpected argument expression type");
      insertLoc = ArgExpr->getLoc();

      // Can't be TupleShuffleExpr because this argExpr is not yet resolved.
    }

    assert(insertLoc.isValid() && "missing argument after trailing closure?");

    if (name.empty())
      TC.diagnose(insertLoc, diag::missing_argument_positional,
                  missingParamIdx + 1)
          .fixItInsert(insertLoc, insertText.str());
    else
      TC.diagnose(insertLoc, diag::missing_argument_named, name)
          .fixItInsert(insertLoc, insertText.str());

    auto candidate = CandidateInfo[0];
    if (candidate.getDecl())
      TC.diagnose(candidate.getDecl(), diag::decl_declared_here,
                  candidate.getDecl()->getFullName());

    Diagnosed = true;
  }

  void missingLabel(unsigned paramIdx) override {
    auto tuple = cast<TupleExpr>(ArgExpr);
    TC.diagnose(tuple->getElement(paramIdx)->getStartLoc(),
                diag::missing_argument_labels, false,
                Parameters[paramIdx].getLabel().str(), IsSubscript);

    Diagnosed = true;
  }

  void outOfOrderArgument(unsigned argIdx, unsigned prevArgIdx) override {
    auto tuple = cast<TupleExpr>(ArgExpr);
    Identifier first = tuple->getElementName(argIdx);
    Identifier second = tuple->getElementName(prevArgIdx);

    // Build a mapping from arguments to parameters.
    SmallVector<unsigned, 4> argBindings(tuple->getNumElements());
    for (unsigned paramIdx = 0; paramIdx != Bindings.size(); ++paramIdx) {
      for (auto argIdx : Bindings[paramIdx])
        argBindings[argIdx] = paramIdx;
    }

    auto argRange = [&](unsigned argIdx, Identifier label) -> SourceRange {
      auto range = tuple->getElement(argIdx)->getSourceRange();
      if (!label.empty())
        range.Start = tuple->getElementNameLoc(argIdx);

      unsigned paramIdx = argBindings[argIdx];
      if (Bindings[paramIdx].size() > 1)
        range.End = tuple->getElement(Bindings[paramIdx].back())->getEndLoc();

      return range;
    };

    auto firstRange = argRange(argIdx, first);
    auto secondRange = argRange(prevArgIdx, second);

    SourceLoc diagLoc = firstRange.Start;

    auto addFixIts = [&](InFlightDiagnostic diag) {
      diag.highlight(firstRange).highlight(secondRange);

      // Move the misplaced argument by removing it from one location and
      // inserting it in another location. To maintain argument comma
      // separation, since the argument is always moving to an earlier index
      // the preceding comma and whitespace is removed and a new trailing
      // comma and space is inserted with the moved argument.
      auto &SM = TC.Context.SourceMgr;
      auto text = SM.extractText(
          Lexer::getCharSourceRangeFromSourceRange(SM, firstRange));

      auto removalRange =
          SourceRange(Lexer::getLocForEndOfToken(
                          SM, tuple->getElement(argIdx - 1)->getEndLoc()),
                      firstRange.End);
      diag.fixItRemove(removalRange);
      diag.fixItInsert(secondRange.Start, text.str() + ", ");
    };

    // There are 4 diagnostic messages variations depending on
    // labeled/unlabeled arguments.
    if (first.empty() && second.empty()) {
      addFixIts(TC.diagnose(diagLoc,
                            diag::argument_out_of_order_unnamed_unnamed,
                            argIdx + 1, prevArgIdx + 1));
    } else if (first.empty() && !second.empty()) {
      addFixIts(TC.diagnose(diagLoc, diag::argument_out_of_order_unnamed_named,
                            argIdx + 1, second));
    } else if (!first.empty() && second.empty()) {
      addFixIts(TC.diagnose(diagLoc, diag::argument_out_of_order_named_unnamed,
                            first, prevArgIdx + 1));
    } else {
      addFixIts(TC.diagnose(diagLoc, diag::argument_out_of_order_named_named,
                            first, second));
    }

    Diagnosed = true;
  }

  bool relabelArguments(ArrayRef<Identifier> newNames) override {
    assert(!newNames.empty() && "No arguments were re-labeled");

    // Let's diagnose labeling problem but only related to corrected ones.
    if (diagnoseArgumentLabelError(TC, ArgExpr, newNames, IsSubscript))
      Diagnosed = true;

    return true;
  }

  bool diagnose() {
    // Use matchCallArguments to determine how close the argument list is (in
    // shape) to the specified candidates parameters.  This ignores the
    // concrete types of the arguments, looking only at the argument labels.
    matchCallArguments(Arguments, Parameters, DefaultMap,
                       CandidateInfo.hasTrailingClosure,
                       /*allowFixes:*/ true, *this, Bindings);

    return Diagnosed;
  }
};

/// Emit a class of diagnostics that we only know how to generate when
/// there is exactly one candidate we know about.  Return true if an error
/// is emitted.
static bool
diagnoseSingleCandidateFailures(CalleeCandidateInfo &CCI, Expr *fnExpr,
                                Expr *argExpr,
                                ArrayRef<Identifier> argLabels) {
  // We only handle the situation where there is exactly one candidate
  // here.
  if (CCI.size() != 1)
    return false;

  auto candidate = CCI[0];
  auto &TC = CCI.CS.TC;

  auto argTy = candidate.getArgumentType();
  if (!argTy)
    return false;

  auto params = candidate.getUncurriedFunctionType()->getParams();
  SmallVector<bool, 4> defaultMap;
  computeDefaultMap(argTy, candidate.getDecl(), candidate.level,
                    defaultMap);
  auto args = decomposeArgType(CCI.CS.getType(argExpr), argLabels);

  // Check the case where a raw-representable type is constructed from an
  // argument with the same type:
  //
  //    MyEnumType(MyEnumType.foo)
  //
  // This is missing 'rawValue:' label, but a better fix is to just remove
  // the unnecessary constructor call:
  //
  //    MyEnumType.foo
  //
  if (params.size() == 1 && args.size() == 1 && candidate.getDecl() &&
      isa<ConstructorDecl>(candidate.getDecl()) && candidate.level == 1) {
    AnyFunctionType::Param &arg = args[0];
    auto resTy =
        candidate.getResultType()->lookThroughAllAnyOptionalTypes();
    auto rawTy = isRawRepresentable(resTy, CCI.CS);
    if (rawTy && arg.getType() && resTy->isEqual(arg.getType())) {
      auto getInnerExpr = [](Expr *E) -> Expr * {
        auto *parenE = dyn_cast<ParenExpr>(E);
        if (!parenE)
          return nullptr;
        return parenE->getSubExpr();
      };
      Expr *innerE = getInnerExpr(argExpr);

      InFlightDiagnostic diag = TC.diagnose(
          fnExpr->getLoc(),
          diag::invalid_initialization_parameter_same_type, resTy);
      diag.highlight((innerE ? innerE : argExpr)->getSourceRange());
      if (innerE) {
        // Remove the unnecessary constructor call.
        diag.fixItRemoveChars(fnExpr->getLoc(), innerE->getStartLoc())
            .fixItRemove(argExpr->getEndLoc());
      }
      return true;
    }
  }

  if (diagnoseTupleParameterMismatch(CCI, candidate.getArgumentType(),
                                     CCI.CS.getType(argExpr), fnExpr,
                                     argExpr))
    return true;

  // We only handle structural errors here.
  if (CCI.closeness != CC_ArgumentLabelMismatch &&
      CCI.closeness != CC_ArgumentCountMismatch)
    return false;

  // If we have a single candidate that failed to match the argument list,
  // attempt to use matchCallArguments to diagnose the problem.
  return ArgumentMatcher(fnExpr, argExpr, params, defaultMap, args, CCI,
                         isa<SubscriptExpr>(fnExpr))
      .diagnose();
}

static bool isRawRepresentableMismatch(Type fromType, Type toType,
                                       KnownProtocolKind kind,
                                       const ConstraintSystem &CS) {
  toType = toType->lookThroughAllAnyOptionalTypes();
  fromType = fromType->lookThroughAllAnyOptionalTypes();

  // First check if this is an attempt to convert from something to
  // raw representable.
  if (conformsToKnownProtocol(fromType, kind, CS)) {
    if (isRawRepresentable(toType, kind, CS))
      return true;
  }

  // Otherwise, it might be an attempt to convert from raw representable
  // to its raw value.
  if (isRawRepresentable(fromType, kind, CS)) {
    if (conformsToKnownProtocol(toType, kind, CS))
      return true;
  }

  return false;
}

static bool diagnoseRawRepresentableMismatch(CalleeCandidateInfo &CCI,
                                             Expr *argExpr,
                                             ArrayRef<Identifier> argLabels) {
  // We are only interested in cases which are
  // unrelated to argument count or label mismatches.
  switch (CCI.closeness) {
    case CC_OneArgumentNearMismatch:
    case CC_OneArgumentMismatch:
    case CC_OneGenericArgumentNearMismatch:
    case CC_OneGenericArgumentMismatch:
    case CC_ArgumentNearMismatch:
    case CC_ArgumentMismatch:
      break;

    default:
      return false;
  }

  auto argType = CCI.CS.getType(argExpr);
  if (!argType || argType->hasTypeVariable() || argType->hasUnresolvedType())
    return false;

  ArrayRef<KnownProtocolKind> rawRepresentableProtocols = {
      KnownProtocolKind::ExpressibleByStringLiteral,
      KnownProtocolKind::ExpressibleByIntegerLiteral};

  const auto &CS = CCI.CS;
  auto arguments = decomposeArgType(argType, argLabels);
  auto *tupleArgs = dyn_cast<TupleExpr>(argExpr);

  for (auto &candidate : CCI.candidates) {
    auto *decl = candidate.getDecl();
    if (!decl)
      continue;

    auto parameters = candidate.getUncurriedFunctionType()->getParams();
    SmallVector<bool, 4> defaultMap;
    computeDefaultMap(candidate.getArgumentType(), decl,
                      candidate.level, defaultMap);

    if (parameters.size() != arguments.size())
      continue;

    for (unsigned i = 0, n = parameters.size(); i != n; ++i) {
      auto paramType = parameters[i].getType();
      auto argType = arguments[i].getType();

      for (auto kind : rawRepresentableProtocols) {
        // If trying to convert from raw type to raw representable,
        // or vice versa from raw representable (e.g. enum) to raw type.
        if (!isRawRepresentableMismatch(argType, paramType, kind, CS))
          continue;

        auto *expr = argExpr;
        if (tupleArgs)
          expr = tupleArgs->getElement(i);

        auto diag =
            CS.TC.diagnose(expr->getLoc(), diag::cannot_convert_argument_value,
                           argType, paramType);

        tryRawRepresentableFixIts(diag, CS, argType, paramType, kind, expr);
        return true;
      }
    }
  }

  return false;
}

// Extract expression for failed argument number
static Expr *getFailedArgumentExpr(CalleeCandidateInfo CCI, Expr *argExpr) {
  if (auto *TE = dyn_cast<TupleExpr>(argExpr))
    return TE->getElement(CCI.failedArgument.argumentNumber);
  else if (auto *PE = dyn_cast<ParenExpr>(argExpr)) {
    assert(CCI.failedArgument.argumentNumber == 0 &&
           "Unexpected argument #");
    return PE->getSubExpr();
  } else {
    assert(CCI.failedArgument.argumentNumber == 0 &&
           "Unexpected argument #");
    return argExpr;
  }
}

/// If the candidate set has been narrowed down to a specific structural
/// problem, e.g. that there are too few parameters specified or that argument
/// labels don't match up, diagnose that error and return true.
bool FailureDiagnosis::diagnoseParameterErrors(CalleeCandidateInfo &CCI,
                                               Expr *fnExpr, Expr *argExpr,
                                               ArrayRef<Identifier> argLabels) {
  if (auto *MTT = CS.getType(fnExpr)->getAs<MetatypeType>()) {
    auto instTy = MTT->getInstanceType();
    if (instTy->getAnyNominal()) {
      // If we are invoking a constructor on a nominal type and there are
      // absolutely no candidates, then they must all be private.
      if (CCI.size() == 0 || (CCI.size() == 1 && CCI.candidates[0].getDecl() &&
                              isa<ProtocolDecl>(CCI.candidates[0].getDecl()))) {
        CS.TC.diagnose(fnExpr->getLoc(), diag::no_accessible_initializers,
                       instTy);
        return true;
      }
      // continue below
    } else if (!instTy->is<TupleType>()) {
      // If we are invoking a constructor on a non-nominal type, the expression
      // is malformed.
      SourceRange initExprRange(fnExpr->getSourceRange().Start,
                                argExpr->getSourceRange().End);
      CS.TC.diagnose(fnExpr->getLoc(), instTy->isExistentialType() ?
                     diag::construct_protocol_by_name :
                     diag::non_nominal_no_initializers, instTy)
          .highlight(initExprRange);
      return true;
    }
  }

  // Try to diagnose errors related to the use of implicit self reference.
  if (diagnoseImplicitSelfErrors(fnExpr, argExpr, CCI, argLabels, CS))
    return true;

  if (diagnoseInstanceMethodAsCurriedMemberOnType(CCI, fnExpr, argExpr))
    return true;

  // Do all the stuff that we only have implemented when there is a single
  // candidate.
  if (diagnoseSingleCandidateFailures(CCI, fnExpr, argExpr, argLabels))
    return true;

  // If we have a failure where the candidate set differs on exactly one
  // argument, and where we have a consistent mismatch across the candidate set
  // (often because there is only one candidate in the set), then diagnose this
  // as a specific problem of passing something of the wrong type into a
  // parameter.
  //
  // We don't generally want to use this path to diagnose calls to
  // symmetrically-typed binary operators because it's likely that both
  // operands contributed to the type.
  if ((CCI.closeness == CC_OneArgumentMismatch ||
       CCI.closeness == CC_OneArgumentNearMismatch ||
       CCI.closeness == CC_OneGenericArgumentMismatch ||
       CCI.closeness == CC_OneGenericArgumentNearMismatch ||
       CCI.closeness == CC_GenericNonsubstitutableMismatch) &&
      CCI.failedArgument.isValid() &&
      !isSymmetricBinaryOperator(CCI)) {
    // Map the argument number into an argument expression.
    TCCOptions options = TCC_ForceRecheck;
    if (CCI.failedArgument.parameterType->is<InOutType>())
      options |= TCC_AllowLValue;

    // It could be that the argument doesn't conform to an archetype.
    Expr *badArgExpr = getFailedArgumentExpr(CCI, argExpr);
    if (CCI.diagnoseGenericParameterErrors(badArgExpr))
      return true;

    // Re-type-check the argument with the expected type of the candidate set.
    // This should produce a specific and tailored diagnostic saying that the
    // type mismatches with expectations.
    Type paramType = CCI.failedArgument.parameterType;
    if (!typeCheckChildIndependently(badArgExpr, paramType,
                                     CTP_CallArgument, options))
      return true;
  }
  
  return false;
}

bool FailureDiagnosis::diagnoseSubscriptErrors(SubscriptExpr *SE, bool inAssignmentDestination) {
  auto baseExpr = typeCheckChildIndependently(SE->getBase());
  if (!baseExpr) return true;
  auto baseType = CS.getType(baseExpr);

  if (isa<NilLiteralExpr>(baseExpr)) {
    diagnose(baseExpr->getLoc(), diag::cannot_subscript_nil_literal)
      .highlight(baseExpr->getSourceRange());
    return true;
  }

  std::function<bool(ArrayRef<OverloadChoice>)> callback =
      [&](ArrayRef<OverloadChoice> candidates) -> bool {
    CalleeCandidateInfo calleeInfo(Type(), candidates, SE->hasTrailingClosure(),
                                   CS, /*selfAlreadyApplied*/ false);

    // We're about to typecheck the index list, which needs to be processed with
    // self already applied.
    for (unsigned i = 0, e = calleeInfo.size(); i != e; ++i)
      ++calleeInfo.candidates[i].level;

    auto indexExpr =
        typeCheckArgumentChildIndependently(SE->getIndex(), Type(), calleeInfo);
    if (!indexExpr)
      return true;

    // Back to analyzing the candidate list with self applied.
    for (unsigned i = 0, e = calleeInfo.size(); i != e; ++i)
      --calleeInfo.candidates[i].level;

    ArrayRef<Identifier> argLabels = SE->getArgumentLabels();
    if (diagnoseParameterErrors(calleeInfo, SE, indexExpr, argLabels))
      return true;

    auto indexType = CS.getType(indexExpr);

    auto decomposedBaseType = decomposeArgType(baseType, {Identifier()});
    auto decomposedIndexType = decomposeArgType(indexType, argLabels);
    calleeInfo.filterList(
        [&](UncurriedCandidate cand) -> CalleeCandidateInfo::ClosenessResultTy {
          // Classify how close this match is.  Non-subscript decls don't match.
          auto subscriptDecl = dyn_cast_or_null<SubscriptDecl>(cand.getDecl());
          if (!subscriptDecl ||
              (inAssignmentDestination && !subscriptDecl->isSettable()))
            return {CC_GeneralMismatch, {}};

          // Check whether the self type matches.
          auto selfConstraint = CC_ExactMatch;
          if (calleeInfo.evaluateCloseness(cand, decomposedBaseType).first !=
              CC_ExactMatch)
            selfConstraint = CC_SelfMismatch;

          // Increase the uncurry level to look past the self argument to the
          // indices.
          cand.level++;

          // Explode out multi-index subscripts to find the best match.
          auto indexResult =
              calleeInfo.evaluateCloseness(cand, decomposedIndexType);
          if (selfConstraint > indexResult.first)
            return {selfConstraint, {}};
          return indexResult;
        });

    // If the closest matches all mismatch on self, we either have something
    // that cannot be subscripted, or an ambiguity.
    if (calleeInfo.closeness == CC_SelfMismatch) {
      diagnose(SE->getLoc(), diag::cannot_subscript_base, baseType)
          .highlight(SE->getBase()->getSourceRange());
      // FIXME: Should suggest overload set, but we're not ready for that until
      // it points to candidates and identifies the self type in the diagnostic.
      // calleeInfo.suggestPotentialOverloads(SE->getLoc());
      return true;
    }

    // Any other failures relate to the index list.
    for (unsigned i = 0, e = calleeInfo.size(); i != e; ++i)
      ++calleeInfo.candidates[i].level;

    // TODO: Is there any reason to check for CC_NonLValueInOut here?

    if (calleeInfo.closeness == CC_ExactMatch) {
      auto message = diag::ambiguous_subscript;

      // If there is an exact match on the argument with
      // a single candidate, let's type-check subscript
      // as a whole to figure out if there is any structural
      // problem after all.
      if (calleeInfo.size() == 1) {
        Expr *expr = SE;
        ConcreteDeclRef decl = nullptr;
        message = diag::cannot_subscript_with_index;

        if (CS.TC.getTypeOfExpressionWithoutApplying(expr, CS.DC, decl))
          return false;

        // If we are down to a single candidate but with an unresolved
        // index type, we can substitute in the base type to get a simpler
        // and more concrete expected type for this subscript decl, in order
        // to diagnose a better error.
        if (baseType && indexType->hasUnresolvedType()) {
          UncurriedCandidate cand = calleeInfo.candidates[0];
          auto candType = baseType->getTypeOfMember(CS.DC->getParentModule(),
                                                    cand.getDecl(), nullptr);
          auto paramsType = candType->getAs<FunctionType>()->getInput();
          if (!typeCheckChildIndependently(indexExpr, paramsType,
                                           CTP_CallArgument, TCC_ForceRecheck))
            return true;
        }
      }

      diagnose(SE->getLoc(), message, baseType, indexType)
          .highlight(indexExpr->getSourceRange())
          .highlight(baseExpr->getSourceRange());

      // FIXME: suggestPotentialOverloads should do this.
      // calleeInfo.suggestPotentialOverloads(SE->getLoc());
      for (auto candidate : calleeInfo.candidates)
        if (auto decl = candidate.getDecl())
          diagnose(decl, diag::found_candidate);
        else
          diagnose(candidate.getExpr()->getLoc(), diag::found_candidate);

      return true;
    }

    if (diagnoseParameterErrors(calleeInfo, SE, indexExpr, argLabels))
      return true;

    // Diagnose some simple and common errors.
    if (calleeInfo.diagnoseSimpleErrors(SE))
      return true;

    // If we haven't found a diagnostic yet, and we are in an assignment's
    // destination, continue with diagnosing the assignment rather than giving
    // a last resort diagnostic here.
    if (inAssignmentDestination)
      return false;

    diagnose(SE->getLoc(), diag::cannot_subscript_with_index, baseType,
             indexType);

    calleeInfo.suggestPotentialOverloads(SE->getLoc());
    return true;
  };

  auto locator =
      CS.getConstraintLocator(SE, ConstraintLocator::SubscriptMember);

  return diagnoseMemberFailures(SE, baseExpr, ConstraintKind::ValueMember,
                                DeclBaseName::createSubscript(),
                                FunctionRefKind::DoubleApply, locator,
                                callback);
}

bool FailureDiagnosis::visitSubscriptExpr(SubscriptExpr *SE) {
  return diagnoseSubscriptErrors(SE, /* inAssignmentDestination = */ false);
}

namespace {
  /// Type checking listener for pattern binding initializers.
  class CalleeListener : public ExprTypeCheckListener {
    Type contextualType;
  public:
    explicit CalleeListener(Type contextualType)
      : contextualType(contextualType) { }

    bool builtConstraints(ConstraintSystem &cs, Expr *expr) override {
      // If we have no contextual type, there is nothing to do.
      if (!contextualType) return false;

      // If the expression is obviously something that produces a metatype,
      // then don't put a constraint on it.
      auto semExpr = expr->getValueProvidingExpr();
      if (isa<TypeExpr>(semExpr))
        return false;
      
      // We're making the expr have a function type, whose result is the same
      // as our contextual type.
      auto inputLocator =
        cs.getConstraintLocator(expr, ConstraintLocator::FunctionResult);

      auto tv = cs.createTypeVariable(inputLocator,
                                      TVO_CanBindToLValue |
                                      TVO_CanBindToInOut |
                                      TVO_PrefersSubtypeBinding);

      // In order to make this work, we pick the most general function type and
      // use a conversion constraint.  This gives us:
      //    "$T0 throws -> contextualType"
      // this allows things that are throws and not throws, and allows escape
      // and noescape functions.
      auto extInfo = FunctionType::ExtInfo().withThrows();
      auto fTy = FunctionType::get(tv, contextualType, extInfo);

      auto locator = cs.getConstraintLocator(expr);

      // Add a conversion constraint between the types.
      cs.addConstraint(ConstraintKind::Conversion, cs.getType(expr), fTy,
                       locator, /*isFavored*/ true);
      return false;
    }
  };
} // end anonymous namespace

/// Return true if this function name is a comparison operator.  This is a
/// simple heuristic used to guide comparison related diagnostics.
static bool isNameOfStandardComparisonOperator(StringRef opName) {
  return opName == "=="  || opName == "!=" ||
         opName == "===" || opName == "!==" ||
         opName == "<"   || opName == ">" ||
         opName == "<="  || opName == ">=";
}

bool FailureDiagnosis::diagnoseNilLiteralComparison(
    Expr *lhsExpr, Expr *rhsExpr, CalleeCandidateInfo &calleeInfo,
    SourceLoc applyLoc) {

  auto overloadName = calleeInfo.declName;

  // Only diagnose for comparison operators.
  if (!isNameOfStandardComparisonOperator(overloadName))
    return false;

  Expr *otherExpr = lhsExpr;
  Expr *nilExpr = rhsExpr;

  // Swap if we picked the wrong side as the nil literal.
  if (!isa<NilLiteralExpr>(nilExpr->getValueProvidingExpr()))
    std::swap(otherExpr, nilExpr);

  // Bail if neither side is a nil literal.
  if (!isa<NilLiteralExpr>(nilExpr->getValueProvidingExpr()))
    return false;

  // Bail if both sides are a nil literal.
  if (isa<NilLiteralExpr>(otherExpr->getValueProvidingExpr()))
    return false;

  auto otherType = CS.getType(otherExpr)->getRValueType();

  // Bail if we were unable to determine the other type.
  if (isUnresolvedOrTypeVarType(otherType))
    return false;

  // Regardless of whether the type has reference or value semantics,
  // comparison with nil is illegal, albeit for different reasons spelled
  // out by the diagnosis.
  if (otherType->getAnyOptionalObjectType() &&
      (overloadName == "!==" || overloadName == "===")) {
    auto revisedName = overloadName;
    revisedName.pop_back();

    // If we made it here, then we're trying to perform a comparison with
    // reference semantics rather than value semantics.  The fixit will
    // lop off the extra '=' in the operator.
    diagnose(applyLoc,
             diag::value_type_comparison_with_nil_illegal_did_you_mean,
             otherType)
        .fixItReplace(applyLoc, revisedName);
  } else {
    diagnose(applyLoc, diag::value_type_comparison_with_nil_illegal, otherType)
        .highlight(otherExpr->getSourceRange());
  }

  return true;
}

bool FailureDiagnosis::diagnoseMethodAttributeFailures(
    swift::ApplyExpr *callExpr, ArrayRef<Identifier> argLabels,
    bool hasTrailingClosure, CalleeCandidateInfo &candidates) {
  auto UDE = dyn_cast<UnresolvedDotExpr>(callExpr->getFn());
  if (!UDE)
    return false;

  auto argExpr = callExpr->getArg();
  auto argType = CS.getType(argExpr);

  // If type of the argument hasn't been established yet, we can't diagnose.
  if (!argType || isUnresolvedOrTypeVarType(argType))
    return false;

  // Let's filter our candidate list based on that type.
  candidates.filterList(argType, argLabels);

  if (candidates.closeness == CC_ExactMatch)
    return false;

  // And if filtering didn't give an exact match, such means that problem
  // might be related to function attributes which is best diagnosed by
  // unviable member candidates, if any.
  auto base = UDE->getBase();
  auto baseType = CS.getType(base);

  // This handles following situation:
  // struct S {
  //   mutating func f(_ i: Int) {}
  //   func f(_ f: Float) {}
  // }
  //
  // Given struct has an overloaded method "f" with a single argument of
  // multiple different types, one of the overloads is marked as
  // "mutating", which means it can only be applied on LValue base type.
  // So when struct is used like this:
  //
  // let answer: Int = 42
  // S().f(answer)
  //
  // Constraint system generator is going to pick `f(_ f: Float)` as
  // only possible overload candidate because "base" of the call is immutable
  // and contextual information about argument type is not available yet.
  // Such leads to incorrect contextual conversion failure diagnostic because
  // type of the argument is going to resolved as (Int) no matter what.
  // To workaround that fact and improve diagnostic of such cases we are going
  // to try and collect all unviable candidates for a given call and check if
  // at least one of them matches established argument type before even trying
  // to re-check argument expression.
  auto results = CS.performMemberLookup(
      ConstraintKind::ValueMember, UDE->getName(), baseType,
      UDE->getFunctionRefKind(), CS.getConstraintLocator(UDE),
      /*includeInaccessibleMembers=*/false);

  if (results.UnviableCandidates.empty())
    return false;

  SmallVector<OverloadChoice, 2> choices;
  for (auto &unviable : results.UnviableCandidates)
    choices.push_back(OverloadChoice(baseType, unviable.first.getDecl(),
                                     UDE->getFunctionRefKind()));

  CalleeCandidateInfo unviableCandidates(baseType, choices, hasTrailingClosure,
                                         CS);

  // Filter list of the unviable candidates based on the
  // already established type of the argument expression.
  unviableCandidates.filterList(argType, argLabels);

  // If one of the unviable candidates matches arguments exactly,
  // that means that actual problem is related to function attributes.
  if (unviableCandidates.closeness == CC_ExactMatch) {
    diagnoseUnviableLookupResults(results, baseType, base, UDE->getName(),
                                  UDE->getNameLoc(), UDE->getLoc());
    return true;
  }

  return false;
}

bool FailureDiagnosis::diagnoseArgumentGenericRequirements(
    TypeChecker &TC, Expr *callExpr, Expr *fnExpr, Expr *argExpr,
    CalleeCandidateInfo &candidates, ArrayRef<Identifier> argLabels) {
  if (candidates.closeness != CC_ExactMatch || candidates.size() != 1)
    return false;

  AbstractFunctionDecl *AFD = nullptr;
  if (auto *DRE = dyn_cast<DeclRefExpr>(fnExpr)) {
    AFD = dyn_cast<AbstractFunctionDecl>(DRE->getDecl());
  } else if (auto *candidate = candidates[0].getDecl()) {
    AFD = dyn_cast<AbstractFunctionDecl>(candidate);
  }

  if (!AFD || !AFD->getGenericSignature() || !AFD->hasInterfaceType())
    return false;

  auto env = AFD->getGenericEnvironment();
  if (!env)
    return false;

  auto const &candidate = candidates.candidates[0];
  auto params = candidate.getUncurriedFunctionType()->getParams();
  SmallVector<bool, 4> defaultMap;
  computeDefaultMap(candidate.getArgumentType(), candidate.getDecl(),
                    candidate.level, defaultMap);

  auto args = decomposeArgType(CS.getType(argExpr), argLabels);

  SmallVector<ParamBinding, 4> bindings;
  MatchCallArgumentListener listener;
  if (matchCallArguments(args, params, defaultMap,
                         candidates.hasTrailingClosure,
                         /*allowFixes=*/false, listener, bindings))
    return false;

  TypeSubstitutionMap substitutions;
  // First, let's collect all of the archetypes and their substitutions,
  // that's going to help later on if there are cross-archetype
  // requirements e.g. <A, B where A.Element == B.Element>.
  for (unsigned i = 0, e = bindings.size(); i != e; ++i) {
    auto param = params[i];
    auto paramType = param.getType()->getInOutObjectType();

    auto archetype = paramType->getAs<ArchetypeType>();
    if (!archetype)
      continue;

    // Bindings specify the arguments that source the parameter. The only case
    // this returns a non-singular value is when there are varargs in play.
    for (auto argNo : bindings[i]) {
      auto argType = args[argNo]
                         .getType()
                         ->getWithoutSpecifierType()
                         ->getRValueObjectType();

      if (argType->is<ArchetypeType>()) {
        diagnoseUnboundArchetype(archetype, fnExpr);
        return true;
      }

      if (isUnresolvedOrTypeVarType(argType) || argType->hasError())
        return false;

      // Record substitution from generic parameter to the argument type.
      substitutions[archetype->getInterfaceType()->getCanonicalType()
                        ->castTo<SubstitutableType>()] = argType;
    }
  }

  if (substitutions.empty())
    return false;

  class RequirementsListener : public GenericRequirementsCheckListener {
    ConstraintSystem &CS;
    AbstractFunctionDecl *Candidate;
    TypeSubstitutionFn Substitutions;

    Expr *CallExpr;
    Expr *FnExpr;
    Expr *ArgExpr;

  public:
    RequirementsListener(ConstraintSystem &cs, AbstractFunctionDecl *AFD,
                         TypeSubstitutionFn subs,
                         Expr *callExpr, Expr *fnExpr, Expr *argExpr)
        : CS(cs), Candidate(AFD), Substitutions(subs), CallExpr(callExpr),
          FnExpr(fnExpr), ArgExpr(argExpr) {}

    bool shouldCheck(RequirementKind kind, Type first, Type second) override {
      // This means that we have encountered requirement which references
      // generic parameter not used in the arguments, we can't diagnose it here.
      return !(first->hasTypeParameter() || first->isTypeVariableOrMember());
    }

    bool diagnoseUnsatisfiedRequirement(
        const Requirement &req, Type first, Type second,
        ArrayRef<ParentConditionalConformance> parents) override {
      Diag<Type, Type, Type, Type, StringRef> note;
      switch (req.getKind()) {
      case RequirementKind::Conformance:
      case RequirementKind::Layout:
        return false;

      case RequirementKind::Superclass:
        note = diag::candidate_types_inheritance_requirement;
        break;

      case RequirementKind::SameType:
        note = diag::candidate_types_equal_requirement;
        break;
      }

      TypeChecker &TC = CS.TC;
      SmallVector<char, 8> scratch;
      auto overloadName = Candidate->getFullName().getString(scratch);

      if (isa<BinaryExpr>(CallExpr) && isa<TupleExpr>(ArgExpr)) {
        auto argTuple = cast<TupleExpr>(ArgExpr);
        auto lhsExpr = argTuple->getElement(0),
             rhsExpr = argTuple->getElement(1);
        auto lhsType = CS.getType(lhsExpr)->getRValueType();
        auto rhsType = CS.getType(rhsExpr)->getRValueType();

        TC.diagnose(FnExpr->getLoc(), diag::cannot_apply_binop_to_args,
                    overloadName, lhsType, rhsType)
            .highlight(lhsExpr->getSourceRange())
            .highlight(rhsExpr->getSourceRange());
      } else if (isa<PrefixUnaryExpr>(CallExpr) ||
                 isa<PostfixUnaryExpr>(CallExpr)) {
        TC.diagnose(ArgExpr->getLoc(), diag::cannot_apply_unop_to_arg,
                    overloadName, CS.getType(ArgExpr));
      } else {
        bool isInitializer = isa<ConstructorDecl>(Candidate);
        TC.diagnose(ArgExpr->getLoc(), diag::cannot_call_with_params,
                    overloadName, getTypeListString(CS.getType(ArgExpr)),
                    isInitializer);
      }

      auto rawFirstType = req.getFirstType();
      auto rawSecondType = req.getSecondType();
      auto *genericSig = Candidate->getGenericSignature();

      TC.diagnose(Candidate, note, first, second,
                  rawFirstType, rawSecondType,
                  TypeChecker::gatherGenericParamBindingsText(
                    {rawFirstType, rawSecondType},
                    genericSig->getGenericParams(),
                    Substitutions));

      ParentConditionalConformance::diagnoseConformanceStack(
          TC.Diags, Candidate->getLoc(), parents);

      return true;
    }
  };

  auto *dc = env->getOwningDeclContext();
  auto substitutionFn = QueryTypeSubstitutionMap{substitutions};
  RequirementsListener genericReqListener(CS, AFD, substitutionFn,
                                          callExpr, fnExpr, argExpr);

  auto result = TC.checkGenericArguments(
      dc, callExpr->getLoc(), fnExpr->getLoc(), AFD->getInterfaceType(),
      env->getGenericSignature()->getGenericParams(),
      env->getGenericSignature()->getRequirements(),
      substitutionFn,
      LookUpConformanceInModule{dc->getParentModule()}, nullptr,
      ConformanceCheckFlags::SuppressDependencyTracking, &genericReqListener);

  assert(result != RequirementCheckResult::UnsatisfiedDependency);

  // Note: If result is RequirementCheckResult::SubstitutionFailure, we did
  // not emit a diagnostic, so we must return false in that case.
  return result == RequirementCheckResult::Failure;
}

/// When initializing Unsafe[Mutable]Pointer<T> from Unsafe[Mutable]RawPointer,
/// issue a diagnostic that refers to the API for binding memory to a type.
static bool isCastToTypedPointer(ConstraintSystem &CS, const Expr *Fn,
                                 const Expr *Arg) {
  auto &Ctx = CS.DC->getASTContext();
  auto *TypeExp = dyn_cast<TypeExpr>(Fn);
  auto *ParenExp = dyn_cast<ParenExpr>(Arg);
  if (!TypeExp || !ParenExp)
    return false;

  auto InitType = CS.getInstanceType(TypeExp);
  auto ArgType = CS.getType(ParenExp->getSubExpr());
  if (InitType.isNull() || ArgType.isNull())
    return false;

  // unwrap one level of Optional
  if (auto ArgOptType = ArgType->getOptionalObjectType())
    ArgType = ArgOptType;

  auto *InitNom = InitType->getAnyNominal();
  if (!InitNom)
    return false;

  if (InitNom != Ctx.getUnsafeMutablePointerDecl()
      && InitNom != Ctx.getUnsafePointerDecl()) {
    return false;
  }
  auto *ArgNom = ArgType->getAnyNominal();
  if (!ArgNom)
    return false;

  if (ArgNom != Ctx.getUnsafeMutableRawPointerDecl()
      && ArgNom != Ctx.getUnsafeRawPointerDecl()) {
    return false;
  }
  return true;
}

static bool diagnoseClosureExplicitParameterMismatch(
    ConstraintSystem &CS, SourceLoc loc,
    ArrayRef<AnyFunctionType::Param> params,
    ArrayRef<AnyFunctionType::Param> args) {
  // We are not trying to diagnose structural problems with top-level
  // arguments here.
  if (params.size() != args.size())
    return false;

  for (unsigned i = 0, n = params.size(); i != n; ++i) {
    auto paramType = params[i].getType();
    auto argType = args[i].getType();

    if (auto paramFnType = paramType->getAs<AnyFunctionType>()) {
      if (auto argFnType = argType->getAs<AnyFunctionType>())
        return diagnoseClosureExplicitParameterMismatch(
            CS, loc, paramFnType->getParams(), argFnType->getParams());
    }

    if (!paramType || !argType || isUnresolvedOrTypeVarType(paramType) ||
        isUnresolvedOrTypeVarType(argType))
      continue;

    if (!CS.TC.isConvertibleTo(argType, paramType, CS.DC)) {
      CS.TC.diagnose(loc, diag::types_not_convertible, false, paramType,
                     argType);
      return true;
    }
  }

  return false;
}

bool FailureDiagnosis::diagnoseTrailingClosureErrors(ApplyExpr *callExpr) {
  if (!callExpr->hasTrailingClosure())
    return false;

  auto *DC = CS.DC;
  auto *fnExpr = callExpr->getFn();
  auto *argExpr = callExpr->getArg();

  ClosureExpr *closureExpr = nullptr;
  if (auto *PE = dyn_cast<ParenExpr>(argExpr)) {
    closureExpr = dyn_cast<ClosureExpr>(PE->getSubExpr());
  } else {
    return false;
  }

  if (!closureExpr)
    return false;

  class CallResultListener : public ExprTypeCheckListener {
    Type expectedResultType;

  public:
    explicit CallResultListener(Type resultType)
        : expectedResultType(resultType) {}

    bool builtConstraints(ConstraintSystem &cs, Expr *expr) override {
      if (!expectedResultType)
        return false;

      auto resultType = cs.getType(expr);
      auto *locator = cs.getConstraintLocator(expr);

      // Since we know that this is trailing closure, format of the
      // type could be like this - ((Input) -> Result) -> ClosureResult
      // which we can leverage to create specific conversion for
      // result type of the call itself, this might help us gain
      // some valuable contextual information.
      if (auto *fnType = resultType->getAs<AnyFunctionType>()) {
        cs.addConstraint(ConstraintKind::Conversion, fnType->getResult(),
                         expectedResultType, locator);
      } else if (auto *typeVar = resultType->getAs<TypeVariableType>()) {
        auto tv =
            cs.createTypeVariable(cs.getConstraintLocator(expr),
                                  TVO_CanBindToLValue | TVO_CanBindToInOut |
                                      TVO_PrefersSubtypeBinding);

        auto extInfo = FunctionType::ExtInfo().withThrows();
        auto fTy = FunctionType::get(ParenType::get(cs.getASTContext(), tv),
                                     expectedResultType, extInfo);

        // Add a conversion constraint between the types.
        cs.addConstraint(ConstraintKind::Conversion, typeVar, fTy, locator,
                         /*isFavored*/ true);
      }

      return false;
    }
  };

  SmallVector<Type, 4> possibleTypes;
  auto currentType = CS.getType(fnExpr);

  // If current type has type variables or unresolved types
  // let's try to re-typecheck it to see if we can get some
  // more information about what is going on.
  if (currentType->hasTypeVariable() || currentType->hasUnresolvedType()) {
    auto contextualType = CS.getContextualType();
    CallResultListener listener(contextualType);
    CS.TC.getPossibleTypesOfExpressionWithoutApplying(
        fnExpr, CS.DC, possibleTypes, FreeTypeVariableBinding::UnresolvedType,
        &listener);

    // Looks like there is there a contextual mismatch
    // related to function type, let's try to diagnose it.
    if (possibleTypes.empty() && contextualType &&
        !contextualType->hasUnresolvedType())
      return diagnoseContextualConversionError(callExpr, contextualType,
                                               CS.getContextualTypePurpose());
  } else {
    possibleTypes.push_back(currentType);
  }

  for (auto type : possibleTypes) {
    auto *fnType = type->getAs<AnyFunctionType>();
    if (!fnType)
      continue;

    auto paramType = fnType->getInput();
    switch (paramType->getKind()) {
    case TypeKind::Tuple: {
      auto tuple = paramType->getAs<TupleType>();
      if (tuple->getNumElements() != 1)
        continue;

      paramType = tuple->getElement(0).getType();
      break;
    }

    case TypeKind::Paren:
      paramType = paramType->getWithoutParens();
      break;

    default:
      return false;
    }

    if (auto paramFnType = paramType->getAs<AnyFunctionType>()) {
      auto closureType = CS.getType(closureExpr);
      if (auto *argFnType = closureType->getAs<AnyFunctionType>()) {
        auto *params = closureExpr->getParameters();
        auto loc = params ? params->getStartLoc() : closureExpr->getStartLoc();
        if (diagnoseClosureExplicitParameterMismatch(
                CS, loc, argFnType->getParams(), paramFnType->getParams()))
          return true;
      }
    }

    auto processor = [&](Type resultType, Type expectedResultType) -> bool {
      if (resultType && expectedResultType) {
        if (!resultType->isEqual(expectedResultType)) {
          CS.TC.diagnose(closureExpr->getEndLoc(),
                         diag::cannot_convert_closure_result, resultType,
                         expectedResultType);
          return true;
        }

        // Looks like both actual and expected result types match,
        // there is nothing we can diagnose in this case.
        return false;
      }

      // If we got a result type, let's re-typecheck the function using it,
      // maybe we can find a problem where contextually we expect one type
      // but trailing closure produces completely different one.
      auto fnType = paramType->getAs<AnyFunctionType>();
      if (!fnType)
        return false;

      class ClosureCalleeListener : public ExprTypeCheckListener {
        Type InputType;
        Type ResultType;

      public:
        explicit ClosureCalleeListener(Type inputType, Type resultType)
            : InputType(inputType), ResultType(resultType) {}

        bool builtConstraints(ConstraintSystem &cs, Expr *expr) override {
          if (!InputType || !ResultType)
            return false;

          auto expectedType = FunctionType::get(InputType, ResultType);
          cs.addConstraint(ConstraintKind::Conversion, cs.getType(expr),
                           expectedType, cs.getConstraintLocator(expr),
                           /*isFavored*/ true);
          return false;
        }
      };

      auto expectedArgType = FunctionType::get(fnType->getInput(), resultType,
                                               fnType->getExtInfo());

      llvm::SaveAndRestore<DeclContext *> SavedDC(CS.DC, DC);
      ClosureCalleeListener listener(expectedArgType, CS.getContextualType());
      return !typeCheckChildIndependently(callExpr->getFn(), Type(),
                                          CTP_CalleeResult, TCC_ForceRecheck,
                                          &listener);
    };

    // Let's see if there are any structural problems with closure itself.
    if (diagnoseClosureExpr(closureExpr, paramType, processor))
      return true;
  }

  return false;
}

/// Check if there failure associated with expression is related
/// to given contextual type.
bool FailureDiagnosis::diagnoseCallContextualConversionErrors(
    ApplyExpr *callExpr, Type contextualType, ContextualTypePurpose CTP) {
  if (!contextualType || contextualType->hasUnresolvedType())
    return false;

  auto &TC = CS.TC;
  auto *DC = CS.DC;

  auto typeCheckExpr = [](TypeChecker &TC, Expr *expr, DeclContext *DC,
                          SmallVectorImpl<Type> &types,
                          Type contextualType = Type()) {
    CalleeListener listener(contextualType);
    TC.getPossibleTypesOfExpressionWithoutApplying(
        expr, DC, types, FreeTypeVariableBinding::Disallow, &listener);
  };

  // First let's type-check expression without contextual type, and
  // see if that's going to produce a type, if so, let's type-check
  // again, this time using given contextual type.
  SmallVector<Type, 4> withoutContextual;
  typeCheckExpr(TC, callExpr, DC, withoutContextual);

  // If there are no types returned, it means that problem was
  // nothing to do with contextual information, probably parameter/argument
  // mismatch.
  if (withoutContextual.empty())
    return false;

  SmallVector<Type, 4> withContextual;
  typeCheckExpr(TC, callExpr, DC, withContextual, contextualType);
  // If type-checking with contextual type didn't produce any results
  // it means that we have a contextual mismatch.
  if (withContextual.empty())
    return diagnoseContextualConversionError(callExpr, contextualType, CTP);

  // If call produces a single type when type-checked with contextual
  // expression, it means that the problem is elsewhere, any other
  // outcome is ambiguous.
  return false;
}

// Check if there is a structural problem in the function expression
// by performing type checking with the option to allow unresolved
// type variables. If that is going to produce a function type with
// unresolved result let's not re-typecheck the function expression,
// because it might produce unrelated diagnostics due to lack of
// contextual information.
static bool shouldTypeCheckFunctionExpr(TypeChecker &TC, DeclContext *DC,
                                        Expr *fnExpr) {
  if (!isa<UnresolvedDotExpr>(fnExpr))
    return true;

  SmallVector<Type, 4> fnTypes;
  TC.getPossibleTypesOfExpressionWithoutApplying(fnExpr, DC, fnTypes,
                                       FreeTypeVariableBinding::UnresolvedType);

  if (fnTypes.size() == 1) {
    // Some member types depend on the arguments to produce a result type,
    // type-checking such expressions without associated arguments is
    // going to produce unrelated diagnostics.
    if (auto fn = fnTypes[0]->getAs<AnyFunctionType>()) {
      auto resultType = fn->getResult();
      if (resultType->hasUnresolvedType() || resultType->hasTypeVariable())
        return false;
    }
  }

  // Might be a structural problem related to the member itself.
  return true;
}

bool FailureDiagnosis::visitApplyExpr(ApplyExpr *callExpr) {
  // If this call involves trailing closure as an argument,
  // let's treat it specially, because re-typecheck of the
  // either function or arguments might results in diagnosing
  // of the unrelated problems due to luck of context.
  if (diagnoseTrailingClosureErrors(callExpr))
    return true;

  if (diagnoseCallContextualConversionErrors(callExpr, CS.getContextualType(),
                                             CS.getContextualTypePurpose()))
    return true;

  auto *fnExpr = callExpr->getFn();
  auto originalFnType = CS.getType(callExpr->getFn());

  if (shouldTypeCheckFunctionExpr(CS.TC, CS.DC, fnExpr)) {
    // Type check the function subexpression to resolve a type for it if
    // possible.
    fnExpr = typeCheckChildIndependently(callExpr->getFn());
    if (!fnExpr)
      return true;
  }

  SWIFT_DEFER {
    if (!fnExpr) return;

    // If it's a member operator reference, put the operator back.
    if (auto operatorRef = fnExpr->getMemberOperatorRef())
      callExpr->setFn(operatorRef);
  };

  auto getFuncType = [](Type type) -> Type {
    auto fnType = type->getRValueType();
    if (auto objectType = fnType->getImplicitlyUnwrappedOptionalObjectType())
      return objectType;
    return fnType;
  };

  auto fnType = getFuncType(CS.getType(fnExpr));

  // Let's see if this has to do with member vs. property error
  // because sometimes when there is a member and a property declared
  // on the nominal type with the same name. Type-checking function
  // expression separately from arguments might produce solution for
  // the property instead of the member.
  if (!fnType->is<AnyFunctionType>() &&
    isa<UnresolvedDotExpr>(callExpr->getFn())) {
    fnExpr = callExpr->getFn();

    SmallVector<Type, 4> types;
    CS.TC.getPossibleTypesOfExpressionWithoutApplying(fnExpr, CS.DC, types);

    auto isFunctionType = [getFuncType](Type type) -> bool {
      return type && getFuncType(type)->is<AnyFunctionType>();
    };

    auto fnTypes = std::find_if(types.begin(), types.end(), isFunctionType);
    if (fnTypes != types.end()) {
      auto funcType = getFuncType(*fnTypes);
      // If there is only one function type, let's use it.
      if (std::none_of(std::next(fnTypes), types.end(), isFunctionType))
        fnType = funcType;
    } else {
      fnType = getFuncType(originalFnType);
    }
  }

  // If we have a contextual type, and if we have an ambiguously typed function
  // result from our previous check, we re-type-check it using this contextual
  // type to inform the result type of the callee.
  //
  // We only do this as a second pass because the first pass we just did may
  // return something of obviously non-function-type.  If this happens, we
  // produce better diagnostics below by diagnosing this here rather than trying
  // to peel apart the failed conversion to function type.
  if (CS.getContextualType() &&
      (isUnresolvedOrTypeVarType(fnType) ||
       (fnType->is<AnyFunctionType>() && fnType->hasUnresolvedType()))) {
    // FIXME: Prevent typeCheckChildIndependently from transforming expressions,
    // because if we try to typecheck OSR expression with contextual type,
    // it'll end up converting it into DeclRefExpr based on contextual info,
    // instead let's try to get a type without applying and filter callee
    // candidates later on.
    CalleeListener listener(CS.getContextualType());

    if (isa<OverloadSetRefExpr>(fnExpr)) {
      assert(!cast<OverloadSetRefExpr>(fnExpr)->getReferencedDecl() &&
             "unexpected declaration reference");

      ConcreteDeclRef decl = nullptr;
      Type type = CS.TC.getTypeOfExpressionWithoutApplying(
          fnExpr, CS.DC, decl, FreeTypeVariableBinding::UnresolvedType,
          &listener);

      if (type)
        fnType = getFuncType(type);
    } else {
      fnExpr = typeCheckChildIndependently(callExpr->getFn(), Type(),
                                           CTP_CalleeResult, TCC_ForceRecheck,
                                           &listener);
      if (!fnExpr)
        return true;

      fnType = getFuncType(CS.getType(fnExpr));
    }
  }

  // If we resolved a concrete expression for the callee, and it has
  // non-function/non-metatype type, then we cannot call it!
  if (!isUnresolvedOrTypeVarType(fnType) &&
      !fnType->is<AnyFunctionType>() && !fnType->is<MetatypeType>()) {
    
    auto arg = callExpr->getArg();

    if (fnType->is<ExistentialMetatypeType>()) {
      auto diag = diagnose(arg->getStartLoc(),
                           diag::missing_init_on_metatype_initialization);
      diag.highlight(fnExpr->getSourceRange());
    } else {
      auto diag = diagnose(arg->getStartLoc(),
                           diag::cannot_call_non_function_value, fnType);
      diag.highlight(fnExpr->getSourceRange());

      // If the argument is an empty tuple, then offer a
      // fix-it to remove the empty tuple and use the value
      // directly.
      if (auto tuple = dyn_cast<TupleExpr>(arg)) {
        if (tuple->getNumElements() == 0) {
          diag.fixItRemove(arg->getSourceRange());
        }
      }
    }

    // If the argument is a trailing ClosureExpr (i.e. {....}) and it is on
    // the line after the callee, then it's likely the user forgot to
    // write "do" before their brace stmt.
    // Note that line differences of more than 1 are diagnosed during parsing.
    if (auto *PE = dyn_cast<ParenExpr>(arg))
      if (PE->hasTrailingClosure() && isa<ClosureExpr>(PE->getSubExpr())) {
        auto *closure = cast<ClosureExpr>(PE->getSubExpr());
        auto &SM = CS.getASTContext().SourceMgr;
        if (closure->hasAnonymousClosureVars() &&
            closure->getParameters()->size() == 0 &&
            1 + SM.getLineNumber(callExpr->getFn()->getEndLoc()) ==
            SM.getLineNumber(closure->getStartLoc())) {
          diagnose(closure->getStartLoc(), diag::brace_stmt_suggest_do)
            .fixItInsert(closure->getStartLoc(), "do ");
        }
      }

    return true;
  }
  
  bool hasTrailingClosure = callArgHasTrailingClosure(callExpr->getArg());
  
  // Collect a full candidate list of callees based on the partially type
  // checked function.
  CalleeCandidateInfo calleeInfo(fnExpr, hasTrailingClosure, CS);

  // Filter list of the candidates based on the known function type.
  if (auto fn = fnType->getAs<AnyFunctionType>()) {
    using Closeness = CalleeCandidateInfo::ClosenessResultTy;

    calleeInfo.filterList([&](UncurriedCandidate candidate) -> Closeness {
      auto resultType = candidate.getResultType();
      if (!resultType)
        return {CC_GeneralMismatch, {}};

      // FIXME: Handle matching of the generic types properly.
      // Currently we don't filter result types containing generic parameters
      // because there is no easy way to do that, and candidate set is going
      // to be pruned by matching of the argument types later on anyway, so
      // it's better to over report than to be too conservative.
      if (resultType->isEqual(fn->getResult()))
        return {CC_ExactMatch, {}};

      return {CC_GeneralMismatch, {}};
    });
  }

  // Filter the candidate list based on the argument we may or may not have.
  calleeInfo.filterContextualMemberList(callExpr->getArg());

  SmallVector<Identifier, 2> argLabelsScratch;
  ArrayRef<Identifier> argLabels =
    callExpr->getArgumentLabels(argLabelsScratch);
  if (diagnoseParameterErrors(calleeInfo, callExpr->getFn(),
                              callExpr->getArg(), argLabels))
    return true;

  // There might be a candidate with correct argument types but it's not
  // used by constraint solver because it doesn't have correct attributes,
  // let's try to diagnose such situation there right before type checking
  // argument expression, because that would overwrite original argument types.
  if (diagnoseMethodAttributeFailures(callExpr, argLabels, hasTrailingClosure,
                                      calleeInfo))
    return true;

  Type argType;  // Type of the argument list, if knowable.
  if (auto FTy = fnType->getAs<AnyFunctionType>())
    argType = FTy->getInput();
  else if (auto MTT = fnType->getAs<AnyMetatypeType>()) {
    // If we are constructing a tuple with initializer syntax, the expected
    // argument list is the tuple type itself - and there is no initdecl.
    auto instanceTy = MTT->getInstanceType();
    if (auto tupleTy = instanceTy->getAs<TupleType>()) {
      argType = tupleTy;
    }
  }

  // If there is a failing constraint associated with current constraint
  // system which points to the argument/parameter mismatch, let's use
  // that information while re-typechecking argument expression, this
  // makes it a lot easier to determine contextual mismatch.
  if (CS.failedConstraint && !hasTrailingClosure) {
    auto *constraint = CS.failedConstraint;
    if (constraint->getKind() == ConstraintKind::ArgumentTupleConversion) {
      if (auto *locator = constraint->getLocator()) {
        if (locator->getAnchor() == callExpr) {
          argType = constraint->getSecondType();
          if (auto *typeVar = argType->getAs<TypeVariableType>())
            argType = CS.getFixedType(typeVar);
        }
      }
    }
  }

  // Get the expression result of type checking the arguments to the call
  // independently, so we have some idea of what we're working with.
  //
  auto argExpr = typeCheckArgumentChildIndependently(callExpr->getArg(),
                                                     argType, calleeInfo,
                                             TCC_AllowUnresolvedTypeVariables);
  if (!argExpr)
    return true; // already diagnosed.

  calleeInfo.filterList(CS.getType(argExpr), argLabels);

  if (diagnoseParameterErrors(calleeInfo, callExpr->getFn(), argExpr,
                              argLabels))
    return true;

  // Diagnose some simple and common errors.
  if (calleeInfo.diagnoseSimpleErrors(callExpr))
    return true;

  // Force recheck of the arg expression because we allowed unresolved types
  // before, and that turned out not to help, and now we want any diagnoses
  // from disallowing them.
  argExpr = typeCheckArgumentChildIndependently(callExpr->getArg(), argType,
                                                calleeInfo, TCC_ForceRecheck);
  if (!argExpr)
    return true; // already diagnosed.
  
  // A common error is to apply an operator that only has inout forms (e.g. +=)
  // to non-lvalues (e.g. a local let).  Produce a nice diagnostic for this
  // case.
  if (calleeInfo.closeness == CC_NonLValueInOut) {
    Diag<StringRef> subElementDiagID;
    Diag<Type> rvalueDiagID;
    Expr *diagExpr = nullptr;
    
    if (isa<PrefixUnaryExpr>(callExpr) || isa<PostfixUnaryExpr>(callExpr)) {
      subElementDiagID = diag::cannot_apply_lvalue_unop_to_subelement;
      rvalueDiagID = diag::cannot_apply_lvalue_unop_to_rvalue;
      diagExpr = argExpr;
    } else if (isa<BinaryExpr>(callExpr)) {
      subElementDiagID = diag::cannot_apply_lvalue_binop_to_subelement;
      rvalueDiagID = diag::cannot_apply_lvalue_binop_to_rvalue;
      
      if (auto argTuple = dyn_cast<TupleExpr>(argExpr))
        diagExpr = argTuple->getElement(0);
    }
    
    if (diagExpr) {
      diagnoseSubElementFailure(diagExpr, callExpr->getFn()->getLoc(), CS,
                                subElementDiagID, rvalueDiagID);
      return true;
    }
  }
  
  // Handle argument label mismatches when we have multiple candidates.
  if (calleeInfo.closeness == CC_ArgumentLabelMismatch) {
    auto args = decomposeArgType(CS.getType(argExpr), argLabels);

    // If we have multiple candidates that we fail to match, just say we have
    // the wrong labels and list the candidates out.
    
    // TODO: It would be nice to use an analog of getTypeListString that
    // doesn't include the argument types.
    diagnose(callExpr->getLoc(), diag::wrong_argument_labels_overload,
             getParamListAsString(args))
      .highlight(argExpr->getSourceRange());

    // Did the user intend on invoking a different overload?
    calleeInfo.suggestPotentialOverloads(fnExpr->getLoc());
    return true;
  }

  auto overloadName = calleeInfo.declName;

  // Local function to check if the error with argument type is
  // related to contextual type information of the enclosing expression
  // rather than resolution of argument expression itself.
  auto isContextualConversionFailure = [&](Expr *argExpr) -> bool {
    // If we found an exact match, this must be a problem with a conversion from
    // the result of the call to the expected type. Diagnose this as a
    // conversion failure.
    if (calleeInfo.closeness == CC_ExactMatch)
      return true;

    if (!CS.getContextualType() ||
        (calleeInfo.closeness != CC_ArgumentMismatch &&
         calleeInfo.closeness != CC_OneGenericArgumentMismatch))
      return false;

    CalleeCandidateInfo candidates(fnExpr, hasTrailingClosure, CS);

    // Filter original list of choices based on the deduced type of
    // argument expression after force re-check.
    candidates.filterContextualMemberList(argExpr);

    // One of the candidates matches exactly, which means that
    // this is a contextual type conversion failure, we can't diagnose here.
    return candidates.closeness == CC_ExactMatch;
  };

  // Otherwise, we have a generic failure.  Diagnose it with a generic error
  // message now.
  if (isa<BinaryExpr>(callExpr) && isa<TupleExpr>(argExpr)) {
    auto argTuple = cast<TupleExpr>(argExpr);
    auto lhsExpr = argTuple->getElement(0), rhsExpr = argTuple->getElement(1);
    auto lhsType = CS.getType(lhsExpr)->getRValueType();
    auto rhsType = CS.getType(rhsExpr)->getRValueType();

    // Diagnose any comparisons with the nil literal.
    if (diagnoseNilLiteralComparison(lhsExpr, rhsExpr, calleeInfo,
                                     callExpr->getLoc()))
      return true;

    if (callExpr->isImplicit() && overloadName == "~=") {
      // This binop was synthesized when typechecking an expression pattern.
      auto diag = lhsType->is<UnresolvedType>()
        ? diagnose(lhsExpr->getLoc(),
                   diag::cannot_match_unresolved_expr_pattern_with_value,
                   rhsType)
        : diagnose(lhsExpr->getLoc(),
                   diag::cannot_match_expr_pattern_with_value,
                   lhsType, rhsType);
      diag.highlight(lhsExpr->getSourceRange());
      diag.highlight(rhsExpr->getSourceRange());
      if (auto optUnwrappedType = rhsType->getOptionalObjectType()) {
        if (lhsType->isEqual(optUnwrappedType)) {
          diag.fixItInsertAfter(lhsExpr->getEndLoc(), "?");
        }
      }
      return true;
    }

    // Diagnose attempts to compare reference equality of certain types.
    if (overloadName == "===" || overloadName == "!==") {
      // Functions.
      if (lhsType->is<AnyFunctionType>() || rhsType->is<AnyFunctionType>()) {
        diagnose(callExpr->getLoc(), diag::cannot_reference_compare_types,
                 overloadName, lhsType, rhsType)
          .highlight(lhsExpr->getSourceRange())
          .highlight(rhsExpr->getSourceRange());
        return true;
      }
    }

    if (diagnoseArgumentGenericRequirements(CS.TC, callExpr, fnExpr, argExpr,
                                            calleeInfo, argLabels))
      return true;

    if (isContextualConversionFailure(argTuple))
      return false;

    if (diagnoseRawRepresentableMismatch(calleeInfo, argExpr, argLabels))
      return true;

    if (!lhsType->isEqual(rhsType)) {
      diagnose(callExpr->getLoc(), diag::cannot_apply_binop_to_args,
               overloadName, lhsType, rhsType)
      .highlight(lhsExpr->getSourceRange())
      .highlight(rhsExpr->getSourceRange());
    } else {
      diagnose(callExpr->getLoc(), diag::cannot_apply_binop_to_same_args,
               overloadName, lhsType)
      .highlight(lhsExpr->getSourceRange())
      .highlight(rhsExpr->getSourceRange());
    }

    if (lhsType->isEqual(rhsType) &&
        isNameOfStandardComparisonOperator(overloadName) &&
        lhsType->is<EnumType>() &&
        !lhsType->getAs<EnumType>()->getDecl()
          ->hasOnlyCasesWithoutAssociatedValues()) {
      diagnose(callExpr->getLoc(),
               diag::no_binary_op_overload_for_enum_with_payload,
               overloadName);
    } else {
      calleeInfo.suggestPotentialOverloads(callExpr->getLoc());
    }

    return true;
  }

  // If all of the arguments are a perfect match, let's check if there
  // are problems with requirements placed on generic parameters, because
  // CalleeCandidateInfo validates only conformance of the parameters
  // to their protocol types (if any) but it doesn't check additional
  // requirements placed on e.g. nested types or between parameters.
  if (diagnoseArgumentGenericRequirements(CS.TC, callExpr, fnExpr, argExpr,
                                          calleeInfo, argLabels))
    return true;

  // If we have a failure where closeness is an exact match, but there is
  // still a failed argument, it is because one (or more) of the arguments
  // types are unresolved.
  if (calleeInfo.closeness == CC_ExactMatch && calleeInfo.failedArgument.isValid()) {
    diagnoseAmbiguity(getFailedArgumentExpr(calleeInfo, argExpr));
    return true;
  }

  if (isContextualConversionFailure(argExpr))
    return false;

  // Generate specific error messages for unary operators.
  if (isa<PrefixUnaryExpr>(callExpr) || isa<PostfixUnaryExpr>(callExpr)) {
    assert(!overloadName.empty());
    diagnose(argExpr->getLoc(), diag::cannot_apply_unop_to_arg, overloadName,
             CS.getType(argExpr));

    calleeInfo.suggestPotentialOverloads(argExpr->getLoc());
    return true;
  }

  if (CS.getType(argExpr)->hasUnresolvedType())
    return false;

  if (diagnoseRawRepresentableMismatch(calleeInfo, argExpr, argLabels))
    return true;

  std::string argString = getTypeListString(CS.getType(argExpr));

  // If we couldn't get the name of the callee, then it must be something of a
  // more complex "value of function type".
  if (overloadName.empty()) {
    // If we couldn't infer the result type of the closure expr, then we have
    // some sort of ambiguity, let the ambiguity diagnostic stuff handle this.
    if (auto ffty = fnType->getAs<AnyFunctionType>())
      if (ffty->getResult()->hasTypeVariable()) {
        diagnoseAmbiguity(fnExpr);
        return true;
      }
    
    // The most common unnamed value of closure type is a ClosureExpr, so
    // special case it.
    if (isa<ClosureExpr>(fnExpr->getValueProvidingExpr())) {
      if (fnType->hasTypeVariable())
        diagnose(argExpr->getStartLoc(), diag::cannot_invoke_closure, argString)
          .highlight(fnExpr->getSourceRange());
      else
        diagnose(argExpr->getStartLoc(), diag::cannot_invoke_closure_type,
                 fnType, argString)
          .highlight(fnExpr->getSourceRange());
      
    } else if (fnType->hasTypeVariable()) {
      diagnose(argExpr->getStartLoc(), diag::cannot_call_function_value,
               argString)
        .highlight(fnExpr->getSourceRange());
    } else {
      diagnose(argExpr->getStartLoc(), diag::cannot_call_value_of_function_type,
                fnType, argString)
        .highlight(fnExpr->getSourceRange());
    }
    
    return true;
  }

  if (auto MTT = fnType->getAs<MetatypeType>()) {
    if (MTT->getInstanceType()->isExistentialType()) {
      diagnose(fnExpr->getLoc(), diag::construct_protocol_value, fnType);
      return true;
    }
  }
  
  // If we have an argument list (i.e., a scalar, or a non-zero-element tuple)
  // then diagnose with some specificity about the arguments.
  bool isInitializer = isa<TypeExpr>(fnExpr);
  if (isa<TupleExpr>(argExpr) &&
      cast<TupleExpr>(argExpr)->getNumElements() == 0) {
    // Emit diagnostics that say "no arguments".
    diagnose(fnExpr->getLoc(), diag::cannot_call_with_no_params,
             overloadName, isInitializer);
  } else {
    diagnose(fnExpr->getLoc(), diag::cannot_call_with_params,
             overloadName, argString, isInitializer);
  }

  if (isCastToTypedPointer(CS, fnExpr, argExpr)) {
    diagnose(fnExpr->getLoc(), diag::pointer_init_to_type)
      .highlight(argExpr->getSourceRange());
  }
  
  // Did the user intend on invoking a different overload?
  calleeInfo.suggestPotentialOverloads(fnExpr->getLoc());
  return true;
}

bool FailureDiagnosis::visitAssignExpr(AssignExpr *assignExpr) {
  // Diagnose obvious assignments to literals.
  if (isa<LiteralExpr>(assignExpr->getDest()->getValueProvidingExpr())) {
    diagnose(assignExpr->getLoc(), diag::cannot_assign_to_literal);
    return true;
  }

  if (CS.TC.diagnoseSelfAssignment(assignExpr))
    return true;

  // Type check the destination first, so we can coerce the source to it.
  auto destExpr = typeCheckChildIndependently(assignExpr->getDest(),
                                              TCC_AllowLValue);
  if (!destExpr) return true;

  auto destType = CS.getType(destExpr);
  if (destType->is<UnresolvedType>() || destType->hasTypeVariable()) {
    // Look closer into why destination has unresolved types since such
    // means that destination has diagnosable structural problems, and it's
    // better to diagnose destination (if possible) before moving on to
    // the source of the assignment.
    destExpr = typeCheckChildIndependently(
        destExpr, TCC_AllowLValue | TCC_ForceRecheck, false);
    if (!destExpr)
      return true;

    // If re-checking destination didn't produce diagnostic, let's just type
    // check the source without contextual information.  If it succeeds, then we
    // win, but if it fails, we'll have to diagnose this another way.
    return !typeCheckChildIndependently(assignExpr->getSrc());
  }

  // If the result type is a non-lvalue, then we are failing because it is
  // immutable and that's not a great thing to assign to.
  if (!destType->hasLValueType()) {
    // If the destination is a subscript, the problem may actually be that we
    // incorrectly decided on a get-only subscript overload, and we may be able
    // to come up with a better diagnosis by looking only at subscript candidates
    // that are set-able.
    if (auto subscriptExpr = dyn_cast<SubscriptExpr>(destExpr)) {
      if (diagnoseSubscriptErrors(subscriptExpr, /* inAssignmentDestination = */ true))
        return true;
    }
    CS.diagnoseAssignmentFailure(destExpr, destType, assignExpr->getLoc());
    return true;
  }

  auto *srcExpr = assignExpr->getSrc();
  auto contextualType = destType->getRValueType();

  // Let's try to type-check assignment source expression without using
  // destination as a contextual type, that allows us to diagnose
  // contextual problems related to source much easier.
  //
  // If source expression requires contextual type to be present,
  // let's avoid this step because it's always going to fail.
  {
    auto *srcExpr = assignExpr->getSrc();
    ExprTypeSaverAndEraser eraser(srcExpr);

    ConcreteDeclRef ref = nullptr;
    auto type = CS.TC.getTypeOfExpressionWithoutApplying(srcExpr, CS.DC, ref);

    if (type && !type->isEqual(contextualType))
      return diagnoseContextualConversionError(
          assignExpr->getSrc(), contextualType, CTP_AssignSource);
  }

  srcExpr = typeCheckChildIndependently(assignExpr->getSrc(), contextualType,
                                        CTP_AssignSource);
  if (!srcExpr)
    return true;

  // If we are assigning to _ and have unresolved types on the RHS, then we have
  // an ambiguity problem.
  if (isa<DiscardAssignmentExpr>(destExpr->getSemanticsProvidingExpr()) &&
      CS.getType(srcExpr)->hasUnresolvedType()) {
    diagnoseAmbiguity(srcExpr);
    return true;
  }

  return false;
}


/// Return true if this type is known to be an ArrayType.
static bool isKnownToBeArrayType(Type ty) {
  if (!ty) return false;

  auto bgt = ty->getAs<BoundGenericType>();
  if (!bgt) return false;

  auto &ctx = bgt->getASTContext();
  return bgt->getDecl() == ctx.getArrayDecl();
}

bool FailureDiagnosis::visitInOutExpr(InOutExpr *IOE) {
  // If we have a contextual type, it must be an inout type.
  auto contextualType = CS.getContextualType();
  if (contextualType) {
    // If the contextual type is one of the UnsafePointer<T> types, then the
    // contextual type of the subexpression must be T.
    Type unwrappedType = contextualType;
    if (auto unwrapped = contextualType->getAnyOptionalObjectType())
      unwrappedType = unwrapped;

    PointerTypeKind pointerKind;
    if (auto pointerEltType =
          unwrappedType->getAnyPointerElementType(pointerKind)) {

      // If the element type is Void, then we allow any input type, since
      // everything is convertible to UnsafeRawPointer
      if (pointerEltType->isVoid())
        contextualType = Type();
      else
        contextualType = pointerEltType;
      
      // Furthermore, if the subexpr type is already known to be an array type,
      // then we must have an attempt at an array to pointer conversion.
      if (isKnownToBeArrayType(CS.getType(IOE->getSubExpr()))) {
        // If we're converting to an UnsafeMutablePointer, then the pointer to
        // the first element is being passed in.  The array is ok, so long as
        // it is mutable.
        if (pointerKind == PTK_UnsafeMutablePointer) {
          contextualType = ArraySliceType::get(contextualType);
        } else if (pointerKind == PTK_UnsafePointer || pointerKind == PTK_UnsafeRawPointer) {
          // If we're converting to an UnsafePointer, then the programmer
          // specified an & unnecessarily.  Produce a fixit hint to remove it.
          diagnose(IOE->getLoc(), diag::extra_address_of_unsafepointer,
                   unwrappedType)
            .highlight(IOE->getSourceRange())
            .fixItRemove(IOE->getStartLoc());
          return true;
        }
      }
    } else if (contextualType->is<InOutType>()) {
      contextualType = contextualType->getInOutObjectType();
    } else {
      // If the caller expected something inout, but we didn't have
      // something of inout type, diagnose it.
      diagnose(IOE->getLoc(), diag::extra_address_of, contextualType)
        .highlight(IOE->getSourceRange())
        .fixItRemove(IOE->getStartLoc());
      return true;
    }
  }

  auto subExpr = typeCheckChildIndependently(IOE->getSubExpr(), contextualType,
                                             CS.getContextualTypePurpose(),
                                             TCC_AllowLValue);
  if (!subExpr) return true;

  auto subExprType = CS.getType(subExpr);

  // The common cause is that the operand is not an lvalue.
  if (!subExprType->hasLValueType()) {
    diagnoseSubElementFailure(subExpr, IOE->getLoc(), CS,
                              diag::cannot_pass_rvalue_inout_subelement,
                              diag::cannot_pass_rvalue_inout);
    return true;
  }
  
  return false;
}

bool FailureDiagnosis::visitCoerceExpr(CoerceExpr *CE) {
  // Coerce the input to whatever type is specified by the CoerceExpr.
  auto expr = typeCheckChildIndependently(CE->getSubExpr(),
                                          CE->getCastTypeLoc().getType(),
                                          CTP_CoerceOperand);
  if (!expr)
    return true;

  auto ref = expr->getReferencedDecl();
  if (auto *decl = ref.getDecl()) {
    // Without explicit coercion we might end up
    // type-checking sub-expression as unavaible
    // declaration, let's try to diagnose that here.
    if (AvailableAttr::isUnavailable(decl))
      return CS.TC.diagnoseExplicitUnavailability(
          decl, expr->getSourceRange(), CS.DC, dyn_cast<ApplyExpr>(expr));
  }

  return false;
}

bool FailureDiagnosis::visitForceValueExpr(ForceValueExpr *FVE) {
  auto argExpr = typeCheckChildIndependently(FVE->getSubExpr());
  if (!argExpr) return true;
  auto argType = CS.getType(argExpr);

  // If the subexpression type checks as a non-optional type, then that is the
  // error.  Produce a specific diagnostic about this.
  if (!isUnresolvedOrTypeVarType(argType) &&
      argType->getAnyOptionalObjectType().isNull()) {
    diagnose(FVE->getLoc(), diag::invalid_force_unwrap, argType)
      .fixItRemove(FVE->getExclaimLoc())
      .highlight(FVE->getSourceRange());
    return true;
  }
  
  return false;
}

bool FailureDiagnosis::visitBindOptionalExpr(BindOptionalExpr *BOE) {
  auto argExpr = typeCheckChildIndependently(BOE->getSubExpr());
  if (!argExpr) return true;
  auto argType = CS.getType(argExpr);

  // If the subexpression type checks as a non-optional type, then that is the
  // error.  Produce a specific diagnostic about this.
  if (!isUnresolvedOrTypeVarType(argType) &&
      argType->getAnyOptionalObjectType().isNull()) {
    diagnose(BOE->getQuestionLoc(), diag::invalid_optional_chain, argType)
      .highlight(BOE->getSourceRange())
      .fixItRemove(BOE->getQuestionLoc());
    return true;
  }

  return false;
}

bool FailureDiagnosis::visitIfExpr(IfExpr *IE) {
  auto typeCheckClauseExpr = [&](Expr *clause, Type contextType = Type(),
                                 ContextualTypePurpose convertPurpose =
                                     CTP_Unused) -> Expr * {
    // Provide proper contextual type when type conversion is specified.
    return typeCheckChildIndependently(clause, contextType, convertPurpose,
                                       TCCOptions(), nullptr, false);
  };
  // Check all of the subexpressions independently.
  auto condExpr = typeCheckClauseExpr(IE->getCondExpr());
  if (!condExpr) return true;
  auto trueExpr = typeCheckClauseExpr(IE->getThenExpr(), CS.getContextualType(),
                                      CS.getContextualTypePurpose());
  if (!trueExpr) return true;
  auto falseExpr = typeCheckClauseExpr(
      IE->getElseExpr(), CS.getContextualType(), CS.getContextualTypePurpose());
  if (!falseExpr) return true;

  // If the true/false values already match, it must be a contextual problem.
  if (CS.getType(trueExpr)->isEqual(CS.getType(falseExpr)))
    return false;
  
  // Otherwise, the true/false result types must not be matching.
  diagnose(IE->getColonLoc(), diag::if_expr_cases_mismatch,
           CS.getType(trueExpr), CS.getType(falseExpr))
      .highlight(trueExpr->getSourceRange())
      .highlight(falseExpr->getSourceRange());
  return true;
}


bool FailureDiagnosis::
visitRebindSelfInConstructorExpr(RebindSelfInConstructorExpr *E) {
  // Don't walk the children for this node, it leads to multiple diagnostics
  // because of how sema injects this node into the type checker.
  return false;
}

bool FailureDiagnosis::visitCaptureListExpr(CaptureListExpr *CLE) {
  // Always walk into the closure of a capture list expression.
  return visitClosureExpr(CLE->getClosureBody());
}

static bool isInvalidClosureResultType(Type resultType) {
  return !resultType || resultType->hasUnresolvedType() ||
          resultType->hasTypeVariable() || resultType->hasArchetype();
}

bool FailureDiagnosis::visitClosureExpr(ClosureExpr *CE) {
  return diagnoseClosureExpr(
      CE, CS.getContextualType(),
      [&](Type resultType, Type expectedResultType) -> bool {
        if (isInvalidClosureResultType(expectedResultType))
          return false;

        // Following situations are possible:
        // * No result type - possible structurable problem in the body;
        // * Function result type - possible use of function without calling it,
        //   which is properly diagnosed by actual type-check call.
        if (resultType && !resultType->getRValueType()->is<AnyFunctionType>()) {
          if (!resultType->isEqual(expectedResultType)) {
            diagnose(CE->getEndLoc(), diag::cannot_convert_closure_result,
                     resultType, expectedResultType);
            return true;
          }
        }
        return false;
      });
}

bool FailureDiagnosis::diagnoseClosureExpr(
    ClosureExpr *CE, Type contextualType,
    std::function<bool(Type, Type)> resultTypeProcessor) {
  // Look through IUO because it doesn't influence
  // neither parameter nor return type diagnostics itself,
  // but if we have function type inside, that might
  // signficantly improve diagnostic quality.
  if (contextualType) {
    if (auto IUO =
            CS.lookThroughImplicitlyUnwrappedOptionalType(contextualType))
      contextualType = IUO;
  }

  Type expectedResultType;

  // If we have a contextual type available for this closure, apply it to the
  // ParamDecls in our parameter list.  This ensures that any uses of them get
  // appropriate types.
  if (contextualType && contextualType->is<AnyFunctionType>()) {
    auto fnType = contextualType->getAs<AnyFunctionType>();
    auto *params = CE->getParameters();
    Type inferredArgType = fnType->getInput();
    
    // It is very common for a contextual type to disagree with the argument
    // list built into the closure expr.  This can be because the closure expr
    // had an explicitly specified pattern, a la:
    //    { a,b in ... }
    // or could be because the closure has an implicitly generated one:
    //    { $0 + $1 }
    // in either case, we want to produce nice and clear diagnostics.
    unsigned actualArgCount = params->size();
    unsigned inferredArgCount = 1;
    // Don't try to desugar ParenType which is going to result in incorrect
    // inferred argument count.
    if (auto *argTupleTy = dyn_cast<TupleType>(inferredArgType.getPointer()))
      inferredArgCount = argTupleTy->getNumElements();

    if (actualArgCount != inferredArgCount) {
      // If the closure didn't specify any arguments and it is in a context that
      // needs some, produce a fixit to turn "{...}" into "{ _,_ in ...}".
      if (actualArgCount == 0 && CE->getInLoc().isInvalid()) {
        auto diag =
          diagnose(CE->getStartLoc(), diag::closure_argument_list_missing,
                   inferredArgCount);
        StringRef fixText;  // We only handle the most common cases.
        if (inferredArgCount == 1)
          fixText = " _ in ";
        else if (inferredArgCount == 2)
          fixText = " _,_ in ";
        else if (inferredArgCount == 3)
          fixText = " _,_,_ in ";
        
        if (!fixText.empty()) {
          // Determine if there is already a space after the { in the closure to
          // make sure we introduce the right whitespace.
          auto afterBrace = CE->getStartLoc().getAdvancedLoc(1);
          auto text = CS.TC.Context.SourceMgr.extractText({afterBrace, 1});
          if (text.size() == 1 && text == " ")
            fixText = fixText.drop_back();
          else
            fixText = fixText.drop_front();
          diag.fixItInsertAfter(CE->getStartLoc(), fixText);
        }
        return true;
      }

      if (inferredArgCount == 1 && actualArgCount > 1) {
        // Let's see if inferred argument is actually a tuple inside of Paren.
        if (auto *argTupleTy = inferredArgType->getAs<TupleType>()) {
          // Looks like the number of closure parameters matches number
          // of inferred arguments, which means we can we can emit an
          // error about an attempt to make use of tuple splat or tuple
          // destructuring and provide a proper fix-it.
          if (argTupleTy->getNumElements() == actualArgCount) {
            // In case of implicit parameters e.g. $0, $1 we
            // can't really provide good fix-it because
            // structure of parameter type itself is unclear.
            for (auto *param : params->getArray()) {
              if (param->isImplicit()) {
                diagnose(params->getStartLoc(),
                         diag::closure_tuple_parameter_destructuring_implicit,
                         argTupleTy);
                return true;
              }
            }

            auto diag = diagnose(params->getStartLoc(),
                                 diag::closure_tuple_parameter_destructuring,
                                 argTupleTy);
            Type actualArgType;
            if (auto *actualFnType = CS.getType(CE)->getAs<AnyFunctionType>())
              actualArgType = actualFnType->getInput();

            auto *closureBody = CE->getBody();
            if (!closureBody)
              return true;

            auto &sourceMgr = CS.getASTContext().SourceMgr;
            auto bodyStmts = closureBody->getElements();

            SourceLoc bodyLoc;
            // If the body is empty let's put the cursor
            // right after "in", otherwise make it start
            // location of the first statement in the body.
            if (bodyStmts.empty())
              bodyLoc = Lexer::getLocForEndOfToken(sourceMgr, CE->getInLoc());
            else
              bodyLoc = bodyStmts.front().getStartLoc();

            SmallString<64> fixIt;
            llvm::raw_svector_ostream OS(fixIt);

            // If this is multi-line closure we'd have to insert new lines
            // in the suggested 'let' to keep the structure of the code intact,
            // otherwise just use ';' to keep everything on the same line.
            auto inLine = sourceMgr.getLineNumber(CE->getInLoc());
            auto bodyLine = sourceMgr.getLineNumber(bodyLoc);
            auto isMultiLineClosure = bodyLine > inLine;
            auto indent = bodyStmts.empty() ? "" : Lexer::getIndentationForLine(
                                                       sourceMgr, bodyLoc);

            SmallString<16> parameter;
            llvm::raw_svector_ostream parameterOS(parameter);

            parameterOS << "(";
            interleave(params->getArray(),
                       [&](const ParamDecl *param) {
                         parameterOS << param->getNameStr();
                       },
                       [&] { parameterOS << ", "; });
            parameterOS << ")";

            // Check if there are any explicit types associated
            // with parameters, if there are, we'll have to add
            // type information to the replacement argument.
            bool explicitTypes = false;
            for (auto *param : params->getArray()) {
              if (param->getTypeLoc().getTypeRepr()) {
                explicitTypes = true;
                break;
              }
            }

            if (isMultiLineClosure)
              OS << '\n' << indent;

            // Let's form 'let <name> : [<type>]? = arg' expression.
            OS << "let " << parameterOS.str() << " = arg"
               << (isMultiLineClosure ? "\n" + indent : "; ");

            SmallString<64> argName;
            llvm::raw_svector_ostream nameOS(argName);
            if (explicitTypes) {
              nameOS << "(arg: " << argTupleTy->getString() << ")";
            } else {
              nameOS << "(arg)";
            }

            if (CE->hasSingleExpressionBody()) {
              // Let's see if we need to add result type to the argument/fix-it:
              //  - if the there is a result type associated with the closure;
              //  - and it's not a void type;
              //  - and it hasn't been explicitly written.
              auto resultType = fnType->getResult();
              auto hasResult = [](Type resultType) -> bool {
                return resultType && !resultType->isVoid();
              };

              auto isValidType = [](Type resultType) -> bool {
                return resultType && !resultType->hasUnresolvedType() &&
                       !resultType->hasTypeVariable();
              };

              // If there an expected result type but it hasn't been explicitly
              // provided, let's add it to the argument.
              if (hasResult(resultType) && !CE->hasExplicitResultType()) {
                nameOS << " -> ";
                if (isValidType(resultType))
                  nameOS << resultType->getString();
                else
                  nameOS << "<#Result#>";
              }

              if (auto stmt = bodyStmts.front().get<Stmt *>()) {
                // If the body is a single expression with implicit return.
                if (isa<ReturnStmt>(stmt) && stmt->isImplicit()) {
                  // And there is non-void expected result type,
                  // because we add 'let' expression to the body
                  // we need to make such 'return' explicit.
                  if (hasResult(resultType))
                    OS << "return ";
                }
              }
            }

            diag.fixItReplace(params->getSourceRange(), nameOS.str())
                .fixItInsert(bodyLoc, OS.str());

            return true;
          }
        }
      }

      bool onlyAnonymousParams =
      std::all_of(params->begin(), params->end(), [](ParamDecl *param) {
        return !param->hasName();
      });

      // Okay, the wrong number of arguments was used, complain about that.
      // Before doing so, strip attributes off the function type so that they
      // don't confuse the issue.
      fnType = FunctionType::get(fnType->getInput(), fnType->getResult());
      auto diag = diagnose(
          params->getStartLoc(), diag::closure_argument_list_tuple, fnType,
          inferredArgCount, actualArgCount, (actualArgCount == 1));

      // If closure expects no parameters but N was given,
      // and all of them are anonymous let's suggest removing them.
      if (inferredArgCount == 0 && onlyAnonymousParams) {
        auto inLoc = CE->getInLoc();
        auto &sourceMgr = CS.getASTContext().SourceMgr;

        if (inLoc.isValid())
          diag.fixItRemoveChars(params->getStartLoc(),
                                Lexer::getLocForEndOfToken(sourceMgr, inLoc));
        return true;
      }

      // If the number of parameters is less than number of inferred
      // and all of the parameters are anonymous, let's suggest a fix-it
      // with the rest of the missing parameters.
      if (actualArgCount < inferredArgCount) {
        SmallString<32> fixIt;
        llvm::raw_svector_ostream OS(fixIt);

        OS << ",";
        auto numMissing = inferredArgCount - actualArgCount;
        for (unsigned i = 0; i != numMissing; ++i) {
          OS << ((onlyAnonymousParams) ? "_" : "<#arg#>");
          OS << ((i == numMissing - 1) ? " " : ",");
        }

        diag.fixItInsertAfter(params->getEndLoc(), OS.str());
      }
      return true;
    }

    // Coerce parameter types here only if there are no unresolved
    if (CS.TC.coerceParameterListToType(params, CE, fnType))
      return true;

    for (auto param : *params) {
      auto paramType = param->getType();
      // If this is unresolved 'inout' parameter, it's better to drop
      // 'inout' from type because that might help to diagnose actual problem
      // e.g. type inference doesn't give us much information anyway.
      if (param->isInOut() && paramType->hasUnresolvedType()) {
        assert(!param->isLet() || !paramType->is<InOutType>());
        param->setType(CS.getASTContext().TheUnresolvedType);
        param->setInterfaceType(paramType->getInOutObjectType());
        param->setSpecifier(swift::VarDecl::Specifier::Owned);
      }
    }

    expectedResultType = fnType->getResult();
  } else {
    // Defend against type variables from our constraint system leaking into
    // recursive constraints systems formed when checking the body of the
    // closure.  These typevars come into them when the body does name
    // lookups against the parameter decls.
    //
    // Handle this by rewriting the arguments to UnresolvedType().
    for (auto VD : *CE->getParameters()) {
      if (VD->getType()->hasTypeVariable() || VD->getType()->hasError()) {
        VD->setType(CS.getASTContext().TheUnresolvedType);
        VD->setInterfaceType(VD->getType()->getInOutObjectType());
      }
    }
  }

  // If this is a complex leaf closure, there is nothing more we can do.
  if (!CE->hasSingleExpressionBody())
    return false;

  if (isInvalidClosureResultType(expectedResultType))
    expectedResultType = Type();

  // When we're type checking a single-expression closure, we need to reset the
  // DeclContext to this closure for the recursive type checking.  Otherwise,
  // if there is a closure in the subexpression, we can violate invariants.
  {
    llvm::SaveAndRestore<DeclContext *> SavedDC(CS.DC, CE);

    // Explicitly disallow to produce solutions with unresolved type variables,
    // because there is no auxiliary logic which would handle that and it's
    // better to allow failure diagnosis to run directly on the closure body.
    // Note that presence of contextual type implicitly forbids such solutions,
    // but it's not always reset.

    if (expectedResultType && !CE->hasExplicitResultType()) {
      ExprCleaner cleaner(CE);

      auto closure = CE->getSingleExpressionBody();
      ConcreteDeclRef decl = nullptr;
      // Let's try to compute result type without mutating AST and
      // using expected (contextual) result type, that's going to help
      // diagnose situations where contextual type expected one result
      // type but actual closure produces a different one without explicitly
      // declaring it (e.g. by using anonymous parameters).
      auto type = CS.TC.getTypeOfExpressionWithoutApplying(
          closure, CS.DC, decl, FreeTypeVariableBinding::Disallow);

      if (type && resultTypeProcessor(type, expectedResultType))
        return true;
    }

    // If the closure had an expected result type, use it.
    if (CE->hasExplicitResultType())
      expectedResultType = CE->getExplicitResultTypeLoc().getType();

    // If we couldn't diagnose anything related to the contextual result type
    // let's run proper type-check with expected type and try to verify it.

    auto CTP = expectedResultType ? CTP_ClosureResult : CTP_Unused;
    auto *bodyExpr = typeCheckChildIndependently(CE->getSingleExpressionBody(),
                                                 expectedResultType, CTP,
                                                 TCCOptions(), nullptr, false);

    if (!bodyExpr)
      return true;

    if (resultTypeProcessor(CS.getType(bodyExpr), expectedResultType))
      return true;
  }

  // If the body of the closure looked ok, then look for a contextual type
  // error.  This is necessary because FailureDiagnosis::diagnoseExprFailure
  // doesn't do this for closures.
  if (contextualType) {
    auto fnType = contextualType->getAs<AnyFunctionType>();
    if (!fnType || fnType->isEqual(CS.getType(CE)))
      return false;

    auto contextualResultType = fnType->getResult();
    // If the result type was unknown, it doesn't really make
    // sense to diagnose from expected to unknown here.
    if (isInvalidClosureResultType(contextualResultType))
      return false;

    // If the closure had an explicitly written return type incompatible with
    // the contextual type, diagnose that.
    if (CE->hasExplicitResultType() &&
        CE->getExplicitResultTypeLoc().getTypeRepr()) {
      auto explicitResultTy = CE->getExplicitResultTypeLoc().getType();
      if (fnType && !explicitResultTy->isEqual(contextualResultType)) {
        auto repr = CE->getExplicitResultTypeLoc().getTypeRepr();
        diagnose(repr->getStartLoc(), diag::incorrect_explicit_closure_result,
                 explicitResultTy, fnType->getResult())
          .fixItReplace(repr->getSourceRange(),fnType->getResult().getString());
        return true;
      }
    }
  }

  // Otherwise, we can't produce a specific diagnostic.
  return false;
}

static bool diagnoseKeyPathUnsupportedOperations(TypeChecker &TC,
                                                 KeyPathExpr *KPE) {
  if (KPE->isObjC())
    return false;

  using ComponentKind = KeyPathExpr::Component::Kind;
  const auto components = KPE->getComponents();

  if (auto *rootType = KPE->getRootType()) {
    if (isa<TupleTypeRepr>(rootType)) {
      auto first = components.front();
      if (first.getKind() == ComponentKind::UnresolvedProperty) {
        TC.diagnose(first.getLoc(),
                    diag::unsupported_keypath_tuple_element_reference);
        return true;
      }
    }
  }

  return false;
}

// Ported version of TypeChecker::checkObjCKeyPathExpr which works
// with new Smart KeyPath feature.
static bool diagnoseKeyPathComponents(ConstraintSystem &CS, KeyPathExpr *KPE,
                                      Type rootType) {
  auto &TC = CS.TC;

  // The key path string we're forming.
  SmallString<32> keyPathScratch;
  llvm::raw_svector_ostream keyPathOS(keyPathScratch);

  // Captures the state of semantic resolution.
  enum State {
    Beginning,
    ResolvingType,
    ResolvingProperty,
    ResolvingArray,
    ResolvingSet,
    ResolvingDictionary,
  } state = Beginning;

  /// Determine whether we are currently resolving a property.
  auto isResolvingProperty = [&] {
    switch (state) {
    case Beginning:
    case ResolvingType:
      return false;

    case ResolvingProperty:
    case ResolvingArray:
    case ResolvingSet:
    case ResolvingDictionary:
      return true;
    }

    llvm_unreachable("Unhandled State in switch.");
  };

  // The type of AnyObject, which is used whenever we don't have
  // sufficient type information.
  Type anyObjectType = TC.Context.getAnyObjectType();

  // Local function to update the state after we've resolved a
  // component.
  Type currentType = rootType;
  auto updateState = [&](bool isProperty, Type newType) {
    // Strip off optionals.
    newType = newType->lookThroughAllAnyOptionalTypes();

    // If updating to a type, just set the new type; there's nothing
    // more to do.
    if (!isProperty) {
      assert(state == Beginning || state == ResolvingType);
      state = ResolvingType;
      currentType = newType;
      return;
    }

    // We're updating to a property. Determine whether we're looking
    // into a bridged Swift collection of some sort.
    if (auto boundGeneric = newType->getAs<BoundGenericType>()) {
      auto nominal = boundGeneric->getDecl();

      // Array<T>
      if (nominal == TC.Context.getArrayDecl()) {
        // Further lookups into the element type.
        state = ResolvingArray;
        currentType = boundGeneric->getGenericArgs()[0];
        return;
      }

      // Set<T>
      if (nominal == TC.Context.getSetDecl()) {
        // Further lookups into the element type.
        state = ResolvingSet;
        currentType = boundGeneric->getGenericArgs()[0];
        return;
      }

      // Dictionary<K, V>
      if (nominal == TC.Context.getDictionaryDecl()) {
        // Key paths look into the keys of a dictionary; further
        // lookups into the value type.
        state = ResolvingDictionary;
        currentType = boundGeneric->getGenericArgs()[1];
        return;
      }
    }

    // Determine whether we're looking into a Foundation collection.
    if (auto classDecl = newType->getClassOrBoundGenericClass()) {
      if (classDecl->isObjC() && classDecl->hasClangNode()) {
        SmallString<32> scratch;
        StringRef objcClassName = classDecl->getObjCRuntimeName(scratch);

        // NSArray
        if (objcClassName == "NSArray") {
          // The element type is unknown, so use AnyObject.
          state = ResolvingArray;
          currentType = anyObjectType;
          return;
        }

        // NSSet
        if (objcClassName == "NSSet") {
          // The element type is unknown, so use AnyObject.
          state = ResolvingSet;
          currentType = anyObjectType;
          return;
        }

        // NSDictionary
        if (objcClassName == "NSDictionary") {
          // Key paths look into the keys of a dictionary; there's no
          // type to help us here.
          state = ResolvingDictionary;
          currentType = anyObjectType;
          return;
        }
      }
    }

    // It's just a property.
    state = ResolvingProperty;
    currentType = newType;
  };

  // Local function to perform name lookup for the current index.
  auto performLookup = [&](DeclBaseName componentName, SourceLoc componentNameLoc,
                           Type &lookupType) -> LookupResult {
    assert(currentType && "Non-beginning state must have a type");
    if (!currentType->mayHaveMembers())
      return LookupResult();

    // Determine the type in which the lookup should occur. If we have
    // a bridged value type, this will be the Objective-C class to
    // which it is bridged.
    if (auto bridgedClass = TC.Context.getBridgedToObjC(CS.DC, currentType))
      lookupType = bridgedClass;
    else
      lookupType = currentType;

    // Look for a member with the given name within this type.
    return TC.lookupMember(CS.DC, lookupType, componentName);
  };

  // Local function to print a component to the string.
  bool needDot = false;
  auto printComponent = [&](DeclBaseName component) {
    if (needDot)
      keyPathOS << ".";
    else
      needDot = true;

    keyPathOS << component;
  };

  bool isInvalid = false;
  SmallVector<KeyPathExpr::Component, 4> resolvedComponents;

  for (auto &component : KPE->getComponents()) {
    auto componentNameLoc = component.getLoc();
    DeclBaseName componentName;

    switch (auto kind = component.getKind()) {
    case KeyPathExpr::Component::Kind::UnresolvedProperty: {
      auto componentFullName = component.getUnresolvedDeclName();
      componentName = componentFullName.getBaseIdentifier();
      break;
    }

    case KeyPathExpr::Component::Kind::UnresolvedSubscript:
      componentName = DeclBaseName::createSubscript();
      break;

    case KeyPathExpr::Component::Kind::Invalid:
    case KeyPathExpr::Component::Kind::OptionalChain:
    case KeyPathExpr::Component::Kind::OptionalForce:
      // FIXME: Diagnose optional chaining and forcing properly.
      return false;

    case KeyPathExpr::Component::Kind::OptionalWrap:
    case KeyPathExpr::Component::Kind::Property:
    case KeyPathExpr::Component::Kind::Subscript:
      llvm_unreachable("already resolved!");
    }

    // If we are resolving into a dictionary, any component is
    // well-formed because the keys are unknown dynamically.
    if (state == ResolvingDictionary) {
      // Just print the component unchanged; there's no checking we
      // can do here.
      printComponent(componentName);

      // From here, we're resolving a property. Use the current type.
      updateState(/*isProperty=*/true, currentType);

      continue;
    }

    // Look for this component.
    Type lookupType;
    LookupResult lookup =
        performLookup(componentName, componentNameLoc, lookupType);

    // If we didn't find anything, try to apply typo-correction.
    bool resultsAreFromTypoCorrection = false;
    if (!lookup) {
      TC.performTypoCorrection(CS.DC, DeclRefKind::Ordinary, lookupType,
                               componentName, componentNameLoc,
                               (lookupType ? defaultMemberTypeLookupOptions
                                           : defaultUnqualifiedLookupOptions),
                               lookup);

      if (currentType)
        TC.diagnose(componentNameLoc, diag::could_not_find_type_member,
                    currentType, componentName);
      else
        TC.diagnose(componentNameLoc, diag::use_unresolved_identifier,
                    componentName, false);

      // Note all the correction candidates.
      for (auto &result : lookup) {
        TC.noteTypoCorrection(componentName, DeclNameLoc(componentNameLoc),
                              result.getValueDecl());
      }

      isInvalid = true;
      if (!lookup)
        break;

      // Remember that these are from typo correction.
      resultsAreFromTypoCorrection = true;
    }

    // If we have more than one result, filter out unavailable or
    // obviously unusable candidates.
    if (lookup.size() > 1) {
      lookup.filter([&](LookupResultEntry result) -> bool {
        // Drop unavailable candidates.
        if (result.getValueDecl()->getAttrs().isUnavailable(TC.Context))
          return false;

        // Drop non-property, non-type candidates.
        if (!isa<VarDecl>(result.getValueDecl()) &&
            !isa<TypeDecl>(result.getValueDecl()))
          return false;

        return true;
      });
    }

    // If we *still* have more than one result, fail.
    if (lookup.size() > 1) {
      // Don't diagnose ambiguities if the results are from typo correction.
      if (resultsAreFromTypoCorrection)
        break;

      if (lookupType)
        TC.diagnose(componentNameLoc, diag::ambiguous_member_overload_set,
                    componentName);
      else
        TC.diagnose(componentNameLoc, diag::ambiguous_decl_ref, componentName);

      for (auto result : lookup) {
        TC.diagnose(result.getValueDecl(), diag::decl_declared_here,
                    result.getValueDecl()->getFullName());
      }
      isInvalid = true;
      break;
    }

    auto found = lookup.front().getValueDecl();

    // Handle property references.
    if (auto var = dyn_cast<VarDecl>(found)) {
      TC.validateDecl(var);

      // Resolve this component to the variable we found.
      auto varRef = ConcreteDeclRef(var);
      auto resolved =
          KeyPathExpr::Component::forProperty(varRef, Type(), componentNameLoc);
      resolvedComponents.push_back(resolved);
      updateState(/*isProperty=*/true,
                  var->getInterfaceType()->getRValueObjectType());

      continue;
    }

    // Handle type references.
    if (auto type = dyn_cast<TypeDecl>(found)) {
      // We cannot refer to a type via a property.
      if (isResolvingProperty()) {
        TC.diagnose(componentNameLoc, diag::expr_keypath_type_of_property,
                    componentName, currentType);
        isInvalid = true;
        break;
      }

      // We cannot refer to a generic type.
      if (type->getDeclaredInterfaceType()->hasTypeParameter()) {
        TC.diagnose(componentNameLoc, diag::expr_keypath_generic_type,
                    componentName);
        isInvalid = true;
        break;
      }

      Type newType;
      if (lookupType && !lookupType->isAnyObject()) {
        newType = lookupType->getTypeOfMember(CS.DC->getParentModule(), type,
                                              type->getDeclaredInterfaceType());
      } else {
        newType = type->getDeclaredInterfaceType();
      }
      if (!newType) {
        isInvalid = true;
        break;
      }

      updateState(/*isProperty=*/false, newType);
      continue;
    }

    continue;
  }

  return isInvalid;
}

bool FailureDiagnosis::visitKeyPathExpr(KeyPathExpr *KPE) {
  if (diagnoseKeyPathUnsupportedOperations(CS.TC, KPE))
    return true;

  auto contextualType = CS.getContextualType();

  auto components = KPE->getComponents();
  assert(!components.empty() && "smart key path components cannot be empty.");

  auto &firstComponent = components.front();
  using ComponentKind = KeyPathExpr::Component::Kind;

  ClassDecl *klass;
  Type parentType, rootType, valueType;
  switch (firstComponent.getKind()) {
  case ComponentKind::UnresolvedProperty:
  case ComponentKind::UnresolvedSubscript: {
    // If there is no contextual type we can't really do anything,
    // as in case of unresolved member expression, which relies on
    // contextual information.
    if (!contextualType)
      return false;

    if (auto *BGT = contextualType->getAs<BoundGenericClassType>()) {
      auto genericArgs = BGT->getGenericArgs();
      klass = BGT->getDecl();
      parentType = BGT->getParent();

      // Smart Key Path can either have 1 argument - root type or
      // two arguments - root and value type.
      assert(genericArgs.size() == 1 || genericArgs.size() == 2);

      rootType = genericArgs.front();
      if (genericArgs.size() == 2)
        valueType = genericArgs.back();
    }
    break;
  }

  default:
    return false;
  }

  // If there is no root type associated with expression we can't
  // really diagnose anything here, it's most likely ambiguity.
  if (!rootType)
    return false;

  // If we know value type, it might be contextual mismatch between
  // the actual type of the path vs. given by the caller.
  if (valueType && !valueType->hasUnresolvedType()) {
    struct KeyPathListener : public ExprTypeCheckListener {
      ClassDecl *Decl;
      Type ParentType;
      Type RootType;

      KeyPathListener(ClassDecl *decl, Type parent, Type root)
          : Decl(decl), ParentType(parent), RootType(root) {}

      bool builtConstraints(ConstraintSystem &cs, Expr *expr) override {
        auto *locator = cs.getConstraintLocator(expr);
        auto valueType = cs.createTypeVariable(locator, TVO_CanBindToInOut);

        auto keyPathType =
            BoundGenericClassType::get(Decl, ParentType, {RootType, valueType});

        cs.addConstraint(ConstraintKind::Conversion, cs.getType(expr),
                         keyPathType, locator, /*isFavored*/ true);
        return false;
      }
    };

    Expr *expr = KPE;
    KeyPathListener listener(klass, parentType, rootType);
    ConcreteDeclRef concreteDecl;

    auto derivedType = CS.TC.getTypeOfExpressionWithoutApplying(
        expr, CS.DC, concreteDecl, FreeTypeVariableBinding::Disallow,
        &listener);

    if (derivedType) {
      if (auto *BGT = derivedType->getAs<BoundGenericClassType>()) {
        auto derivedValueType = BGT->getGenericArgs().back();
        if (!CS.TC.isConvertibleTo(valueType, derivedValueType, CS.DC)) {
          diagnose(KPE->getLoc(),
                   diag::expr_smart_keypath_value_covert_to_contextual_type,
                   derivedValueType, valueType);
          return true;
        }
      }
    }
  }

  // Looks like this is not a problem with contextual value type, let's see
  // if there is something wrong with the path itself, maybe one of the
  // components is incorrectly typed or doesn't exist...
  return diagnoseKeyPathComponents(CS, KPE, rootType);
}

static bool isDictionaryLiteralCompatible(Type ty, ConstraintSystem &CS,
                                          SourceLoc loc) {
  auto DLC =
      CS.TC.getProtocol(loc, KnownProtocolKind::ExpressibleByDictionaryLiteral);
  if (!DLC) return false;
  return CS.TC
      .conformsToProtocol(ty, DLC, CS.DC, ConformanceCheckFlags::InExpression)
      .hasValue();
}

bool FailureDiagnosis::visitArrayExpr(ArrayExpr *E) {
  Type contextualElementType;
  auto elementTypePurpose = CTP_Unused;

  // If we had a contextual type, then it either conforms to
  // ExpressibleByArrayLiteral or it is an invalid contextual type.
  if (auto contextualType = CS.getContextualType()) {
    // If our contextual type is an optional, look through them, because we're
    // surely initializing whatever is inside.
    contextualType = contextualType->lookThroughAllAnyOptionalTypes();

    // Validate that the contextual type conforms to ExpressibleByArrayLiteral and
    // figure out what the contextual element type is in place.
    auto ALC = CS.TC.getProtocol(E->getLoc(),
                                 KnownProtocolKind::ExpressibleByArrayLiteral);
    if (!ALC)
      return visitExpr(E);

    // Check to see if the contextual type conforms.
    auto Conformance = CS.TC.conformsToProtocol(
        contextualType, ALC, CS.DC, ConformanceCheckFlags::InExpression);

    // If not, we may have an implicit conversion going on.  If the contextual
    // type is an UnsafePointer or UnsafeMutablePointer, then that is probably
    // what is happening.
    if (!Conformance) {
      // TODO: Not handling various string conversions or void conversions.
      Type unwrappedTy = contextualType;
      if (Type unwrapped = contextualType->getAnyOptionalObjectType())
        unwrappedTy = unwrapped;
      PointerTypeKind pointerKind;
      if (Type pointeeTy = unwrappedTy->getAnyPointerElementType(pointerKind)) {
        if (pointerKind == PTK_UnsafePointer) {
          auto arrayTy = ArraySliceType::get(pointeeTy);
          Conformance = CS.TC.conformsToProtocol(
              arrayTy, ALC, CS.DC, ConformanceCheckFlags::InExpression);

          if (Conformance)
            contextualType = arrayTy;
        }
      }
    }

    auto numElements = E->getNumElements();
    if (!Conformance) {
      // If the contextual type conforms to ExpressibleByDictionaryLiteral and
      // this is an empty array, then they meant "[:]".
      if (numElements == 0 &&
          isDictionaryLiteralCompatible(contextualType, CS, E->getLoc())) {
        diagnose(E->getStartLoc(), diag::should_use_empty_dictionary_literal)
          .fixItInsert(E->getEndLoc(), ":");
        return true;
      }


      diagnose(E->getStartLoc(), diag::type_is_not_array, contextualType)
        .highlight(E->getSourceRange());

      // If the contextual type conforms to ExpressibleByDictionaryLiteral, then
      // they wrote "x = [1,2]" but probably meant "x = [1:2]".
      if ((numElements & 1) == 0 && numElements > 0 &&
          isDictionaryLiteralCompatible(contextualType, CS, E->getLoc())) {
        auto diag = diagnose(E->getStartLoc(), diag::meant_dictionary_lit);

        // Change every other comma into a colon, only if the number
        // of commas present matches the number of elements, because
        // otherwise it might a structural problem with the expression
        // e.g. ["a""b": 1].
        const auto commaLocs = E->getCommaLocs();
        if (commaLocs.size() == numElements - 1) {
          for (unsigned i = 0, e = numElements / 2; i != e; ++i)
            diag.fixItReplace(commaLocs[i*2], ":");
        }
      }

      return true;
    }

    contextualElementType =
        ProtocolConformanceRef::getTypeWitnessByName(
            contextualType, *Conformance,
            CS.getASTContext().Id_ArrayLiteralElement, &CS.TC)
            ->getDesugaredType();
    elementTypePurpose = CTP_ArrayElement;
  }

  // Type check each of the subexpressions in place, passing down the contextual
  // type information if we have it.
  for (auto elt : E->getElements()) {
    if (typeCheckChildIndependently(elt, contextualElementType,
                                    elementTypePurpose) == nullptr)
      return true;
  }

  // If that didn't turn up an issue, then we don't know what to do.
  // TODO: When a contextual type is missing, we could try to diagnose cases
  // where the element types mismatch... but theoretically they should type
  // unify to Any, so that could never happen?
  return false;
}

bool FailureDiagnosis::visitDictionaryExpr(DictionaryExpr *E) {
  Type contextualKeyType, contextualValueType;
  auto keyTypePurpose = CTP_Unused, valueTypePurpose = CTP_Unused;

  // If we had a contextual type, then it either conforms to
  // ExpressibleByDictionaryLiteral or it is an invalid contextual type.
  if (auto contextualType = CS.getContextualType()) {
    // If our contextual type is an optional, look through them, because we're
    // surely initializing whatever is inside.
    contextualType = contextualType->lookThroughAllAnyOptionalTypes();

    auto DLC = CS.TC.getProtocol(
        E->getLoc(), KnownProtocolKind::ExpressibleByDictionaryLiteral);
    if (!DLC) return visitExpr(E);

    // Validate the contextual type conforms to ExpressibleByDictionaryLiteral
    // and figure out what the contextual Key/Value types are in place.
    auto Conformance = CS.TC.conformsToProtocol(
        contextualType, DLC, CS.DC, ConformanceCheckFlags::InExpression);
    if (!Conformance) {
      diagnose(E->getStartLoc(), diag::type_is_not_dictionary, contextualType)
        .highlight(E->getSourceRange());
      return true;
    }

    contextualKeyType =
        ProtocolConformanceRef::getTypeWitnessByName(
            contextualType, *Conformance, CS.getASTContext().Id_Key, &CS.TC)
            ->getDesugaredType();

    contextualValueType =
        ProtocolConformanceRef::getTypeWitnessByName(
            contextualType, *Conformance, CS.getASTContext().Id_Value, &CS.TC)
            ->getDesugaredType();

    assert(contextualKeyType && contextualValueType &&
           "Could not find Key/Value DictionaryLiteral associated types from"
           " contextual type conformance");
    
    keyTypePurpose = CTP_DictionaryKey;
    valueTypePurpose = CTP_DictionaryValue;
  }
  
  // Type check each of the subexpressions in place, passing down the contextual
  // type information if we have it.
  for (auto elt : E->getElements()) {
    auto TE = dyn_cast<TupleExpr>(elt);
    if (!TE || TE->getNumElements() != 2) continue;

    if (!typeCheckChildIndependently(TE->getElement(0),
                                     contextualKeyType, keyTypePurpose))
      return true;
    if (!typeCheckChildIndependently(TE->getElement(1),
                                     contextualValueType, valueTypePurpose))
      return true;
  }

  // If that didn't turn up an issue, then we don't know what to do.
  // TODO: When a contextual type is missing, we could try to diagnose cases
  // where the element types mismatch.  There is no Any equivalent since they
  // keys need to be hashable.
  return false;
}

/// When an object literal fails to typecheck because its protocol's
/// corresponding default type has not been set in the global namespace (e.g.
/// _ColorLiteralType), suggest that the user import the appropriate module for
/// the target.
bool FailureDiagnosis::visitObjectLiteralExpr(ObjectLiteralExpr *E) {
  auto &TC = CS.getTypeChecker();

  // Type check the argument first.
  auto protocol = TC.getLiteralProtocol(E);
  if (!protocol)
    return false;
  DeclName constrName = TC.getObjectLiteralConstructorName(E);
  assert(constrName);
  ArrayRef<ValueDecl *> constrs = protocol->lookupDirect(constrName);
  if (constrs.size() != 1 || !isa<ConstructorDecl>(constrs.front()))
    return false;
  auto *constr = cast<ConstructorDecl>(constrs.front());
  auto paramType = TC.getObjectLiteralParameterType(E, constr);
  if (!typeCheckChildIndependently(
        E->getArg(), paramType, CTP_CallArgument))
    return true;

  // Conditions for showing this diagnostic:
  // * The object literal protocol's default type is unimplemented
  if (TC.getDefaultType(protocol, CS.DC))
    return false;
  // * The object literal has no contextual type
  if (CS.getContextualType())
    return false;

  // Figure out what import to suggest.
  auto &Ctx = CS.getASTContext();
  const auto &target = Ctx.LangOpts.Target;
  StringRef importModule;
  StringRef importDefaultTypeName;
  if (protocol == Ctx.getProtocol(KnownProtocolKind::ExpressibleByColorLiteral)) {
    if (target.isMacOSX()) {
      importModule = "AppKit";
      importDefaultTypeName = "NSColor";
    } else if (target.isiOS() || target.isTvOS()) {
      importModule = "UIKit";
      importDefaultTypeName = "UIColor";
    }
  } else if (protocol == Ctx.getProtocol(
               KnownProtocolKind::ExpressibleByImageLiteral)) {
    if (target.isMacOSX()) {
      importModule = "AppKit";
      importDefaultTypeName = "NSImage";
    } else if (target.isiOS() || target.isTvOS()) {
      importModule = "UIKit";
      importDefaultTypeName = "UIImage";
    }
  } else if (protocol == Ctx.getProtocol( 
               KnownProtocolKind::ExpressibleByFileReferenceLiteral)) {
    importModule = "Foundation";
    importDefaultTypeName = "URL";
  }

  // Emit the diagnostic.
  const auto plainName = E->getLiteralKindPlainName();
  TC.diagnose(E->getLoc(), diag::object_literal_default_type_missing,
              plainName);
  if (!importModule.empty()) {
    TC.diagnose(E->getLoc(), diag::object_literal_resolve_import,
                importModule, importDefaultTypeName, plainName);
  }
  return true;
}

bool FailureDiagnosis::visitUnresolvedMemberExpr(UnresolvedMemberExpr *E) {
  // If we have no contextual type, there is no way to resolve this.  Just
  // diagnose this as an ambiguity.
  if (!CS.getContextualType())
    return false;

  // OTOH, if we do have a contextual type, we can provide a more specific
  // error.  Dig out the UnresolvedValueMember constraint for this expr node.
  Constraint *memberConstraint = nullptr;
  auto checkConstraint = [&](Constraint *C) {
    if (C->getKind() == ConstraintKind::UnresolvedValueMember &&
        simplifyLocatorToAnchor(CS, C->getLocator()) == E)
      memberConstraint = C;
  };

  if (CS.failedConstraint)
    checkConstraint(CS.failedConstraint);
  for (auto &C : CS.getConstraints()) {
    if (memberConstraint) break;
    checkConstraint(&C);
  }
  
  // If we can't find the member constraint in question, then we failed.
  if (!memberConstraint)
    return false;

  std::function<bool(ArrayRef<OverloadChoice>)> callback = [&](
      ArrayRef<OverloadChoice> candidates) {
    bool hasTrailingClosure = callArgHasTrailingClosure(E->getArgument());

    // Dump all of our viable candidates into a CalleeCandidateInfo & sort it
    // out.
    CalleeCandidateInfo candidateInfo(Type(), candidates, hasTrailingClosure,
                                      CS);

    // Filter the candidate list based on the argument we may or may not have.
    candidateInfo.filterContextualMemberList(E->getArgument());

    // If we have multiple candidates, then we have an ambiguity.
    if (candidateInfo.size() != 1) {
      SourceRange argRange;
      if (auto arg = E->getArgument())
        argRange = arg->getSourceRange();
      diagnose(E->getNameLoc(), diag::ambiguous_member_overload_set,
               E->getName())
          .highlight(argRange);
      candidateInfo.suggestPotentialOverloads(E->getNameLoc().getBaseNameLoc());
      return true;
    }

    auto *argExpr = E->getArgument();
    auto candidateArgTy = candidateInfo[0].getArgumentType();

    // Depending on how we matched, produce tailored diagnostics.
    switch (candidateInfo.closeness) {
    case CC_NonLValueInOut: // First argument is inout but no lvalue present.
    case CC_OneArgumentMismatch: // All arguments except one match.
    case CC_OneArgumentNearMismatch:
    case CC_OneGenericArgumentMismatch:
    case CC_OneGenericArgumentNearMismatch:
    case CC_GenericNonsubstitutableMismatch:
    case CC_SelfMismatch:         // Self argument mismatches.
    case CC_ArgumentNearMismatch: // Argument list mismatch.
    case CC_ArgumentMismatch:     // Argument list mismatch.
      assert(0 && "These aren't produced by filterContextualMemberList");
      return false;

    case CC_ExactMatch: { // This is a perfect match for the arguments.

      // If we have an exact match, then we must have an argument list, check
      // it.
      if (candidateArgTy) {
        assert(argExpr && "Exact match without argument?");
        if (!typeCheckArgumentChildIndependently(argExpr, candidateArgTy,
                                                 candidateInfo))
          return true;
      }

      // If the argument is a match, then check the result type.  We might have
      // looked up a contextual member whose result type disagrees with the
      // expected result type.
      auto resultTy = candidateInfo[0].getResultType();
      if (!resultTy)
        resultTy = candidateInfo[0].getUncurriedType();

      if (resultTy && !CS.getContextualType()->is<UnboundGenericType>() &&
          !CS.TC.isConvertibleTo(resultTy, CS.getContextualType(), CS.DC)) {
        diagnose(E->getNameLoc(), diag::expected_result_in_contextual_member,
                 E->getName(), resultTy, CS.getContextualType());
        return true;
      }

      // Otherwise, this is an exact match, return false to diagnose this as an
      // ambiguity.  It must be some other problem, such as failing to infer a
      // generic argument on the enum type.
      return false;
    }

    case CC_Unavailable:
    case CC_Inaccessible:
      // Diagnose some simple and common errors.
      return candidateInfo.diagnoseSimpleErrors(E);

    case CC_ArgumentLabelMismatch:
    case CC_ArgumentCountMismatch: {
      // If we have no argument, the candidates must have expected one.
      if (!argExpr) {
        if (!candidateArgTy)
          return false; // Candidate must be incorrect for some other reason.

        // Pick one of the arguments that are expected as an exemplar.
        if (candidateArgTy->isVoid()) {
          // If this member is () -> T, suggest adding parentheses.
          diagnose(E->getNameLoc(), diag::expected_parens_in_contextual_member,
                   E->getName())
              .fixItInsertAfter(E->getEndLoc(), "()");
        } else {
          diagnose(E->getNameLoc(),
                   diag::expected_argument_in_contextual_member, E->getName(),
                   candidateArgTy);
        }
        return true;
      }

      assert(argExpr && candidateArgTy && "Exact match without an argument?");
      return diagnoseSingleCandidateFailures(candidateInfo, E, argExpr,
                                             E->getArgumentLabels());
    }

    case CC_GeneralMismatch: { // Something else is wrong.
      // If an argument value was specified, but this member expects no
      // arguments,
      // then we fail with a nice error message.
      if (!candidateArgTy) {
        if (CS.getType(argExpr)->isVoid()) {
          diagnose(E->getNameLoc(),
                   diag::unexpected_parens_in_contextual_member, E->getName())
              .fixItRemove(E->getArgument()->getSourceRange());
        } else {
          diagnose(E->getNameLoc(),
                   diag::unexpected_argument_in_contextual_member, E->getName())
              .highlight(E->getArgument()->getSourceRange());
        }
        return true;
      }

      return false;
    }
    }

    llvm_unreachable("all cases should be handled");
  };

  return diagnoseMemberFailures(E, nullptr, memberConstraint->getKind(),
                                memberConstraint->getMember(),
                                memberConstraint->getFunctionRefKind(),
                                memberConstraint->getLocator(), callback);
}

bool FailureDiagnosis::diagnoseMemberFailures(
    Expr *E, Expr *baseExpr, ConstraintKind lookupKind, DeclName memberName,
    FunctionRefKind funcRefKind, ConstraintLocator *locator,
    Optional<std::function<bool(ArrayRef<OverloadChoice>)>> callback,
    bool includeInaccessibleMembers) {
  auto isInitializer = memberName.isSimpleName(CS.TC.Context.Id_init);

  // Get the referenced base expression from the failed constraint, along with
  // the SourceRange for the member ref.  In "x.y", this returns the expr for x
  // and the source range for y.
  SourceRange memberRange;
  SourceLoc BaseLoc;
  DeclNameLoc NameLoc;

  Type baseTy, baseObjTy;
  // UnresolvedMemberExpr doesn't have "base" expression,
  // it's represented as ".foo", which means that we need
  // to get base from the context.
  if (auto *UME = dyn_cast<UnresolvedMemberExpr>(E)) {
    memberRange = E->getSourceRange();
    BaseLoc = E->getLoc();
    NameLoc = UME->getNameLoc();
    baseTy = CS.getContextualType();
    if (!baseTy)
      return false;

    // If we succeeded, get ready to do the member lookup.
    baseObjTy = baseTy->getRValueType();

    // If the base object is already a metatype type, then something weird is
    // going on.  For now, just generate a generic error.
    if (baseObjTy->is<MetatypeType>())
      return false;

    baseTy = baseObjTy = MetatypeType::get(baseObjTy);
  } else {
    memberRange = baseExpr->getSourceRange();
    if (locator)
      locator = simplifyLocator(CS, locator, memberRange);

    BaseLoc = baseExpr->getLoc();
    NameLoc = DeclNameLoc(memberRange.Start);

    // Retypecheck the anchor type, which is the base of the member expression.
    baseExpr =
        typeCheckArbitrarySubExprIndependently(baseExpr, TCC_AllowLValue);
    if (!baseExpr)
      return true;

    baseTy = CS.getType(baseExpr)->getWithoutSpecifierType();
    baseObjTy = baseTy;
  }

  // If the base type is an IUO, look through it.  Odds are, the code is not
  // trying to find a member of it.
  if (auto objTy = CS.lookThroughImplicitlyUnwrappedOptionalType(baseObjTy))
    baseTy = baseObjTy = objTy;

  // If the base of this property access is a function that takes an empty
  // argument list, then the most likely problem is that the user wanted to
  // call the function, e.g. in "a.b.c" where they had to write "a.b().c".
  // Produce a specific diagnostic + fixit for this situation.
  if (auto baseFTy = baseObjTy->getAs<AnyFunctionType>()) {
    if (baseExpr && baseFTy->getInput()->isVoid()) {
      SourceLoc insertLoc = baseExpr->getEndLoc();

      if (auto *DRE = dyn_cast<DeclRefExpr>(baseExpr)) {
        diagnose(baseExpr->getLoc(), diag::did_not_call_function,
                 DRE->getDecl()->getBaseName().getIdentifier())
            .fixItInsertAfter(insertLoc, "()");
        return true;
      }

      if (auto *DSCE = dyn_cast<DotSyntaxCallExpr>(baseExpr))
        if (auto *DRE = dyn_cast<DeclRefExpr>(DSCE->getFn())) {
          diagnose(baseExpr->getLoc(), diag::did_not_call_method,
                   DRE->getDecl()->getBaseName().getIdentifier())
              .fixItInsertAfter(insertLoc, "()");
          return true;
        }

      diagnose(baseExpr->getLoc(), diag::did_not_call_function_value)
          .fixItInsertAfter(insertLoc, "()");
      return true;
    }
  }

  // If this is a tuple, then the index needs to be valid.
  if (auto tuple = baseObjTy->getAs<TupleType>()) {
    StringRef nameStr = memberName.getBaseIdentifier().str();
    int fieldIdx = -1;
    // Resolve a number reference into the tuple type.
    unsigned Value = 0;
    if (!nameStr.getAsInteger(10, Value) && Value < tuple->getNumElements()) {
      fieldIdx = Value;
    } else {
      fieldIdx = tuple->getNamedElementId(memberName.getBaseIdentifier());
    }

    if (fieldIdx != -1)
      return false; // Lookup is valid.

    diagnose(BaseLoc, diag::could_not_find_tuple_member, baseObjTy, memberName)
        .highlight(memberRange);
    return true;
  }

  // If this is initializer/constructor lookup we are dealing this.
  if (isInitializer) {
    // Let's check what is the base type we are trying to look it up on
    // because only MetatypeType is viable to find constructor on, as per
    // rules in ConstraintSystem::performMemberLookup.
    if (!baseTy->is<AnyMetatypeType>()) {
      baseTy = MetatypeType::get(baseTy, CS.getASTContext());
    }
  }

  // If base type has unresolved generic parameters, such might mean
  // that it's initializer with erroneous argument, otherwise this would
  // be a simple ambiguous archetype case, neither can be diagnosed here.
  if (baseTy->hasTypeParameter() && baseTy->hasUnresolvedType())
    return false;

  MemberLookupResult result =
      CS.performMemberLookup(lookupKind, memberName, baseTy, funcRefKind,
                             locator, includeInaccessibleMembers);

  switch (result.OverallResult) {
  case MemberLookupResult::Unsolved:
    // If we couldn't resolve a specific type for the base expression, then we
    // cannot produce a specific diagnostic.
    return false;

  case MemberLookupResult::ErrorAlreadyDiagnosed:
    // If an error was already emitted, then we're done, don't emit anything
    // redundant.
    return true;

  case MemberLookupResult::HasResults:
    break;
  }

  SmallVector<OverloadChoice, 4> viableCandidatesToReport;
  for (auto candidate : result.ViableCandidates)
    if (candidate.getKind() != OverloadChoiceKind::KeyPathApplication)
      viableCandidatesToReport.push_back(candidate);

  // Since the lookup was allowing inaccessible members, let's check
  // if it found anything of that sort, which is easy to diagnose.
  bool allUnavailable = !CS.TC.getLangOpts().DisableAvailabilityChecking;
  bool allInaccessible = true;
  for (auto &member : viableCandidatesToReport) {
    if (!member.isDecl()) {
      // if there is no declaration, this choice is implicitly available.
      allUnavailable = false;
      continue;
    }

    auto decl = member.getDecl();
    // Check availability of the found choice.
    if (!decl->getAttrs().isUnavailable(CS.getASTContext()))
      allUnavailable = false;

    if (decl->isAccessibleFrom(CS.DC))
      allInaccessible = false;
  }

  // diagnoseSimpleErrors() should have diagnosed this scenario.
  assert(!allInaccessible || viableCandidatesToReport.empty());

  if (result.UnviableCandidates.empty() && isInitializer &&
      !baseObjTy->is<AnyMetatypeType>()) {
    if (auto ctorRef = dyn_cast<UnresolvedDotExpr>(E)) {
      // Diagnose 'super.init', which can only appear inside another
      // initializer, specially.
      if (isa<SuperRefExpr>(ctorRef->getBase())) {
        diagnose(BaseLoc, diag::super_initializer_not_in_initializer);
        return true;
      }

      // Suggest inserting a call to 'type(of:)' to construct another object
      // of the same dynamic type.
      SourceRange fixItRng = ctorRef->getNameLoc().getSourceRange();

      // Surround the caller in `type(of:)`.
      diagnose(BaseLoc, diag::init_not_instance_member)
          .fixItInsert(fixItRng.Start, "type(of: ")
          .fixItInsertAfter(fixItRng.End, ")");
      return true;
    }
  }

  if (viableCandidatesToReport.empty()) {
    // If this was an optional type let's check if the base type
    // has requested member, if so - generate nice error saying that
    // optional was not unwrapped, otherwise say that type value has
    // no such member.
    if (auto *OT = dyn_cast<OptionalType>(baseObjTy.getPointer())) {
      auto optionalResult = CS.performMemberLookup(
          lookupKind, memberName, OT->getBaseType(), funcRefKind, locator,
          /*includeInaccessibleMembers*/ false);

      switch (optionalResult.OverallResult) {
      case MemberLookupResult::ErrorAlreadyDiagnosed:
        // If an error was already emitted, then we're done, don't emit anything
        // redundant.
        return true;

      case MemberLookupResult::Unsolved:
      case MemberLookupResult::HasResults:
        break;
      }

      if (!optionalResult.ViableCandidates.empty()) {
        // By default we assume that the LHS type is not optional.
        StringRef fixIt = "!";
        auto contextualType = CS.getContextualType();
        if (contextualType && isa<OptionalType>(contextualType.getPointer()))
          fixIt = "?";

        diagnose(BaseLoc, diag::missing_unwrap_optional, baseObjTy)
            .fixItInsertAfter(baseExpr->getEndLoc(), fixIt);
        return true;
      }
    }

    // FIXME: Dig out the property DeclNameLoc.
    diagnoseUnviableLookupResults(result, baseObjTy, baseExpr, memberName,
                                  NameLoc, BaseLoc);
    return true;
  }

  if (allUnavailable) {
    auto firstDecl = viableCandidatesToReport[0].getDecl();
    // FIXME: We need the enclosing CallExpr to rewrite the argument labels.
    if (CS.TC.diagnoseExplicitUnavailability(firstDecl, BaseLoc, CS.DC,
                                             /*call*/ nullptr))
      return true;
  }

  return callback.hasValue() ? (*callback)(viableCandidatesToReport) : false;
}

bool FailureDiagnosis::visitUnresolvedDotExpr(UnresolvedDotExpr *UDE) {
  auto *baseExpr = UDE->getBase();
  auto *locator = CS.getConstraintLocator(UDE, ConstraintLocator::Member);
  if (!locator)
    return false;

  return diagnoseMemberFailures(UDE, baseExpr, ConstraintKind::ValueMember,
                                UDE->getName(), UDE->getFunctionRefKind(),
                                locator);
}

/// A TupleExpr propagate contextual type information down to its children and
/// can be erroneous when there is a label mismatch etc.
bool FailureDiagnosis::visitTupleExpr(TupleExpr *TE) {
  // If we know the requested argType to use, use computeTupleShuffle to produce
  // the shuffle of input arguments to destination values.  It requires a
  // TupleType to compute the mapping from argExpr.  Conveniently, it doesn't
  // care about the actual types though, so we can just use 'void' for them.
  if (!CS.getContextualType() || !CS.getContextualType()->is<TupleType>())
    return visitExpr(TE);

  auto contextualTT = CS.getContextualType()->castTo<TupleType>();

  SmallVector<TupleTypeElt, 4> ArgElts;
  auto voidTy = CS.getASTContext().TheEmptyTupleType;

  for (unsigned i = 0, e = TE->getNumElements(); i != e; ++i)
    ArgElts.push_back({ voidTy, TE->getElementName(i) });
  auto TEType = TupleType::get(ArgElts, CS.getASTContext());

  if (!TEType->is<TupleType>())
    return visitExpr(TE);

  SmallVector<int, 4> sources;
  SmallVector<unsigned, 4> variadicArgs;
  
  // If the shuffle is invalid, then there is a type error.  We could diagnose
  // it specifically here, but the general logic does a fine job so we let it
  // do it.
  if (computeTupleShuffle(TEType->castTo<TupleType>()->getElements(),
                          contextualTT->getElements(), sources, variadicArgs))
    return visitExpr(TE);

  // If we got a correct shuffle, we can perform the analysis of all of
  // the input elements, with their expected types.
  for (unsigned i = 0, e = sources.size(); i != e; ++i) {
    // If the value is taken from a default argument, ignore it.
    if (sources[i] == TupleShuffleExpr::DefaultInitialize ||
        sources[i] == TupleShuffleExpr::Variadic ||
        sources[i] == TupleShuffleExpr::CallerDefaultInitialize)
      continue;
    
    assert(sources[i] >= 0 && "Unknown sources index");
    
    // Otherwise, it must match the corresponding expected argument type.
    unsigned inArgNo = sources[i];

    TCCOptions options;
    if (contextualTT->getElement(i).isInOut())
      options |= TCC_AllowLValue;

    auto actualType = contextualTT->getElementType(i);
    auto exprResult =
        typeCheckChildIndependently(TE->getElement(inArgNo), actualType,
                                    CS.getContextualTypePurpose(), options);
    // If there was an error type checking this argument, then we're done.
    if (!exprResult) return true;
    
    // If the caller expected something inout, but we didn't have
    // something of inout type, diagnose it.
    if (auto IOE =
          dyn_cast<InOutExpr>(exprResult->getSemanticsProvidingExpr())) {
      if (!contextualTT->getElement(i).isInOut()) {
        diagnose(exprResult->getLoc(), diag::extra_address_of,
                 CS.getType(exprResult)->getInOutObjectType())
            .highlight(exprResult->getSourceRange())
            .fixItRemove(IOE->getStartLoc());
        return true;
      }
    }
  }
  
  if (!variadicArgs.empty()) {
    auto varargsTy = contextualTT->getVarArgsBaseType();
    for (unsigned i = 0, e = variadicArgs.size(); i != e; ++i) {
      unsigned inArgNo = variadicArgs[i];

      auto expr = typeCheckChildIndependently(
          TE->getElement(inArgNo), varargsTy, CS.getContextualTypePurpose());
      // If there was an error type checking this argument, then we're done.
      if (!expr) return true;
    }
  }
  
  return false;
}

/// An IdentityExpr doesn't change its argument, but it *can* propagate its
/// contextual type information down.
bool FailureDiagnosis::visitIdentityExpr(IdentityExpr *E) {
  auto contextualType = CS.getContextualType();

  // If we have a paren expr and our contextual type is a ParenType, remove the
  // paren expr sugar.
  if (contextualType)
    contextualType = contextualType->getWithoutParens();
  if (!typeCheckChildIndependently(E->getSubExpr(), contextualType,
                                   CS.getContextualTypePurpose()))
    return true;
  return false;
}

/// A TryExpr doesn't change it's argument, nor does it change the contextual
/// type.
bool FailureDiagnosis::visitTryExpr(TryExpr *E) {
  return visit(E->getSubExpr());
}

bool FailureDiagnosis::visitExpr(Expr *E) {
  // Check each of our immediate children to see if any of them are
  // independently invalid.
  bool errorInSubExpr = false;
  
  E->forEachImmediateChildExpr([&](Expr *Child) -> Expr* {
    // If we already found an error, stop checking.
    if (errorInSubExpr) return Child;

    // Otherwise just type check the subexpression independently.  If that
    // succeeds, then we stitch the result back into our expression.
    if (typeCheckChildIndependently(Child, TCC_AllowLValue))
      return Child;

    // Otherwise, it failed, which emitted a diagnostic.  Keep track of this
    // so that we don't emit multiple diagnostics.
    errorInSubExpr = true;
    return Child;
  });
  
  // If any of the children were errors, we're done.
  if (errorInSubExpr)
    return true;
  
  // Otherwise, produce a more generic error.
  return false;
}


bool FailureDiagnosis::diagnoseExprFailure() {
  assert(expr);

  // Our general approach is to do a depth first traversal of the broken
  // expression tree, type checking as we go.  If we find a subtree that cannot
  // be type checked on its own (even to an incomplete type) then that is where
  // we focus our attention.  If we do find a type, we use it to check for
  // contextual type mismatches.
  return visit(expr);
}


/// Given a specific expression and the remnants of the failed constraint
/// system, produce a specific diagnostic.
///
/// This is guaranteed to always emit an error message.
///
void ConstraintSystem::diagnoseFailureForExpr(Expr *expr) {
  // Continue simplifying any active constraints left in the system.  We can end
  // up with them because the solver bails out as soon as it sees a Failure.  We
  // don't want to leave them around in the system because later diagnostics
  // will assume they are unsolvable and may otherwise leave the system in an
  // inconsistent state.
  simplify(/*ContinueAfterFailures*/true);

  // Look through RebindSelfInConstructorExpr to avoid weird Sema issues.
  if (auto *RB = dyn_cast<RebindSelfInConstructorExpr>(expr))
    expr = RB->getSubExpr();

  FailureDiagnosis diagnosis(expr, *this);

  // Now, attempt to diagnose the failure from the info we've collected.
  if (diagnosis.diagnoseExprFailure())
    return;

  // If this is a contextual conversion problem, dig out some information.
  if (diagnosis.diagnoseContextualConversionError(expr, getContextualType(),
                                                  getContextualTypePurpose()))
    return;

  // If we can diagnose a problem based on the constraints left laying around in
  // the system, do so now.
  if (diagnosis.diagnoseConstraintFailure())
    return;

  // If no one could find a problem with this expression or constraint system,
  // then it must be well-formed... but is ambiguous.  Handle this by diagnostic
  // various cases that come up.
  diagnosis.diagnoseAmbiguity(expr);
}

// FIXME: Instead of doing this, we should store the decl in the type
// variable, or in the locator.
static bool hasArchetype(const GenericTypeDecl *generic,
                         ArchetypeType *archetype) {
  assert(!archetype->getOpenedExistentialType() &&
         !archetype->getParent());

  auto genericEnv = generic->getGenericEnvironment();
  if (!genericEnv)
    return false;

  return archetype->getGenericEnvironment() == genericEnv;
}

static void noteArchetypeSource(const TypeLoc &loc, ArchetypeType *archetype,
                                ConstraintSystem &cs) {
  const GenericTypeDecl *FoundDecl = nullptr;
  const ComponentIdentTypeRepr *FoundGenericTypeBase = nullptr;

  // Walk the TypeRepr to find the type in question.
  if (auto typerepr = loc.getTypeRepr()) {
    struct FindGenericTypeDecl : public ASTWalker {
      const GenericTypeDecl *FoundDecl = nullptr;
      const ComponentIdentTypeRepr *FoundGenericTypeBase = nullptr;
      ArchetypeType *Archetype;

      FindGenericTypeDecl(ArchetypeType *Archetype)
          : Archetype(Archetype) {}
      
      bool walkToTypeReprPre(TypeRepr *T) override {
        // If we already emitted the note, we're done.
        if (FoundDecl) return false;
        
        if (auto ident = dyn_cast<ComponentIdentTypeRepr>(T)) {
          auto *generic =
              dyn_cast_or_null<GenericTypeDecl>(ident->getBoundDecl());
          if (generic && hasArchetype(generic, Archetype)) {
            FoundDecl = generic;
            FoundGenericTypeBase = ident;
            return false;
          }
        }
        // Keep walking.
        return true;
      }
    } findGenericTypeDecl(archetype);

    typerepr->walk(findGenericTypeDecl);
    FoundDecl = findGenericTypeDecl.FoundDecl;
    FoundGenericTypeBase = findGenericTypeDecl.FoundGenericTypeBase;
  }

  // If we didn't find the type in the TypeRepr, fall back to the type in the
  // type checked expression.
  if (!FoundDecl) {
    if (const GenericTypeDecl *generic = loc.getType()->getAnyGeneric())
      if (hasArchetype(generic, archetype))
        FoundDecl = generic;
  }

  auto &tc = cs.getTypeChecker();
  if (FoundDecl) {
    Type type;
    if (auto *nominal = dyn_cast<NominalTypeDecl>(FoundDecl))
      type = nominal->getDeclaredType();
    else if (auto *typeAlias = dyn_cast<TypeAliasDecl>(FoundDecl))
      type = typeAlias->getUnboundGenericType();
    else
      type = FoundDecl->getDeclaredInterfaceType();
    tc.diagnose(FoundDecl, diag::archetype_declared_in_type, archetype, type);
  }

  if (FoundGenericTypeBase && !isa<GenericIdentTypeRepr>(FoundGenericTypeBase)){
    assert(FoundDecl);

    // If we can, prefer using any types already fixed by the constraint system.
    // This lets us produce fixes like `Pair<Int, Any>` instead of defaulting to
    // `Pair<Any, Any>`.
    // Right now we only handle this when the type that's at fault is the
    // top-level type passed to this function.
    if (loc.getType().isNull()) {
      return;
    }
    
    ArrayRef<Type> genericArgs;
    if (auto *boundGenericTy = loc.getType()->getAs<BoundGenericType>()) {
      if (boundGenericTy->getDecl() == FoundDecl)
        genericArgs = boundGenericTy->getGenericArgs();
    }

    auto getPreferredType =
        [&](const GenericTypeParamDecl *genericParam) -> Type {
      // If we were able to get the generic arguments (i.e. the types used at
      // FoundDecl's use site), we can prefer those...
      if (genericArgs.empty())
        return Type();

      Type preferred = genericArgs[genericParam->getIndex()];
      if (!preferred || preferred->hasError())
        return Type();

      // ...but only if they were actually resolved by the constraint system
      // despite the failure.
      Type maybeFixedType = cs.getFixedTypeRecursive(preferred,
                                                     /*wantRValue*/true);
      if (maybeFixedType->hasTypeVariable() ||
          maybeFixedType->hasUnresolvedType()) {
        return Type();
      }
      return maybeFixedType;
    };

    SmallString<64> genericParamBuf;
    if (tc.getDefaultGenericArgumentsString(genericParamBuf, FoundDecl,
                                            getPreferredType)) {
      tc.diagnose(FoundGenericTypeBase->getLoc(),
                  diag::unbound_generic_parameter_explicit_fix)
        .fixItInsertAfter(FoundGenericTypeBase->getEndLoc(), genericParamBuf);
    }
  }
}

std::pair<Type, ContextualTypePurpose>
FailureDiagnosis::validateContextualType(Type contextualType,
                                         ContextualTypePurpose CTP) {
  if (!contextualType)
    return {contextualType, CTP};

  // If we're asked to convert to an autoclosure, then we really want to
  // convert to the result of it.
  if (auto *FT = contextualType->getAs<AnyFunctionType>())
    if (FT->isAutoClosure())
      contextualType = FT->getResult();

  // Since some of the contextual types might be tuples e.g. subscript argument
  // is a tuple or paren wrapping a tuple, it's required to recursively check
  // its elements to determine nullability of the contextual type, because it
  // might contain archetypes.
  std::function<bool(Type)> shouldNullifyType = [&](Type type) -> bool {
    switch (type->getDesugaredType()->getKind()) {
    case TypeKind::Archetype:
    case TypeKind::Unresolved:
      return true;

    case TypeKind::BoundGenericEnum:
    case TypeKind::BoundGenericClass:
    case TypeKind::BoundGenericStruct:
    case TypeKind::UnboundGeneric:
    case TypeKind::GenericFunction:
    case TypeKind::Metatype:
      return type->hasUnresolvedType();

    case TypeKind::Tuple: {
      auto tupleType = type->getAs<TupleType>();
      for (auto &element : tupleType->getElements()) {
        if (shouldNullifyType(element.getType()))
            return true;
      }
      break;
    }

    default:
      return false;
    }

    return false;
  };

  bool shouldNullify = false;
  if (auto objectType = contextualType->getWithoutSpecifierType()) {
    // Note that simply checking for `objectType->hasUnresolvedType()` is not
    // appropriate in this case standalone, because if it's in a function,
    // for example, or inout type, we still want to preserve it's skeleton
    /// because that helps to diagnose inout argument issues. Complete
    // nullification is only appropriate for generic types with unresolved
    // types or standalone archetypes because that's going to give
    // sub-expression solver a chance to try and compute type as it sees fit
    // and higher level code would have a chance to check it, which avoids
    // diagnostic messages like `cannot convert (_) -> _ to (Int) -> Void`.
    shouldNullify = shouldNullifyType(objectType);
  }

  // If the conversion type contains no info, drop it.
  if (shouldNullify)
    return {Type(), CTP_Unused};

  // Remove all of the potentially leftover type variables or type parameters
  // from the contextual type to be used by new solver.
  contextualType = replaceTypeParametersWithUnresolved(contextualType);
  contextualType = replaceTypeVariablesWithUnresolved(contextualType);

  return {contextualType, CTP};
}

/// Check the specified closure to see if it is a multi-statement closure with
/// an uninferred type.  If so, diagnose the problem with an error and return
/// true.
bool FailureDiagnosis::
diagnoseAmbiguousMultiStatementClosure(ClosureExpr *closure) {
  if (closure->hasSingleExpressionBody() ||
      closure->hasExplicitResultType())
    return false;

  auto closureType = CS.getType(closure)->getAs<AnyFunctionType>();
  if (!closureType ||
      !(closureType->getResult()->hasUnresolvedType() ||
        closureType->getResult()->hasTypeVariable()))
    return false;

  // Okay, we have a multi-statement closure expr that has no inferred result,
  // type, in the context of a larger expression.  The user probably expected
  // the compiler to infer the result type of the closure from the body of the
  // closure, which Swift doesn't do for multi-statement closures.  Try to be
  // helpful by digging into the body of the closure, looking for a return
  // statement, and inferring the result type from it.  If we can figure that
  // out, we can produce a fixit hint.
  class ReturnStmtFinder : public ASTWalker {
    SmallVectorImpl<ReturnStmt*> &returnStmts;
  public:
    ReturnStmtFinder(SmallVectorImpl<ReturnStmt*> &returnStmts)
      : returnStmts(returnStmts) {}

    // Walk through statements, so we find returns hiding in if/else blocks etc.
    std::pair<bool, Stmt *> walkToStmtPre(Stmt *S) override {
      // Keep track of any return statements we find.
      if (auto RS = dyn_cast<ReturnStmt>(S))
        returnStmts.push_back(RS);
      return { true, S };
    }
    
    // Don't walk into anything else, since they cannot contain statements
    // that can return from the current closure.
    std::pair<bool, Expr *> walkToExprPre(Expr *E) override {
      return { false, E };
    }
    std::pair<bool, Pattern*> walkToPatternPre(Pattern *P) override {
      return { false, P };
    }
    bool walkToDeclPre(Decl *D) override { return false; }
    bool walkToTypeLocPre(TypeLoc &TL) override { return false; }
    bool walkToTypeReprPre(TypeRepr *T) override { return false; }
    bool walkToParameterListPre(ParameterList *PL) override { return false; }
  };
  
  SmallVector<ReturnStmt*, 4> Returns;
  closure->getBody()->walk(ReturnStmtFinder(Returns));
  
  // If we found a return statement inside of the closure expression, then go
  // ahead and type check the body to see if we can determine a type.
  for (auto RS : Returns) {
    llvm::SaveAndRestore<DeclContext *> SavedDC(CS.DC, closure);

    // Otherwise, we're ok to type check the subexpr.
    Type resultType;
    if (RS->hasResult()) {
      auto resultExpr = RS->getResult();
      ConcreteDeclRef decl = nullptr;

      // If return expression uses closure parameters, which have/are
      // type variables, such means that we won't be able to
      // type-check result correctly and, unfortunately,
      // we are going to leak type variables from the parent
      // constraint system through declaration types.
      bool hasUnresolvedParams = false;
      resultExpr->forEachChildExpr([&](Expr *childExpr) -> Expr *{
        if (auto DRE = dyn_cast<DeclRefExpr>(childExpr)) {
          if (auto param = dyn_cast<ParamDecl>(DRE->getDecl())) {
            auto paramType = param->hasType() ? param->getType() : Type();
            if (!paramType || paramType->hasTypeVariable()) {
              hasUnresolvedParams = true;
              return nullptr;
            }
          }
        }
        return childExpr;
      });

      if (hasUnresolvedParams)
        continue;

      CS.TC.preCheckExpression(resultExpr, CS.DC);

      // Obtain type of the result expression without applying solutions,
      // because otherwise this might result in leaking of type variables,
      // since we are not resetting result statement and if expression is
      // successfully type-checked its type cleanup is going to be disabled
      // (we are allowing unresolved types), and as a side-effect it might
      // also be transformed e.g. OverloadedDeclRefExpr -> DeclRefExpr.
      auto type = CS.TC.getTypeOfExpressionWithoutApplying(
          resultExpr, CS.DC, decl, FreeTypeVariableBinding::UnresolvedType);
      if (type)
        resultType = type;
    }
    
    // If we found a type, presuppose it was the intended result and insert a
    // fixit hint.
    if (resultType && !isUnresolvedOrTypeVarType(resultType)) {
      std::string resultTypeStr = resultType->getString();
      
      // If there is a location for an 'in' token, then the argument list was
      // specified somehow but no return type was.  Insert a "-> ReturnType "
      // before the in token.
      if (closure->getInLoc().isValid()) {
        diagnose(closure->getLoc(), diag::cannot_infer_closure_result_type)
          .fixItInsert(closure->getInLoc(), "-> " + resultTypeStr + " ");
        return true;
      }
      
      // Otherwise, the closure must take zero arguments.  We know this
      // because the if one or more argument is specified, a multi-statement
      // closure *must* name them, or explicitly ignore them with "_ in".
      //
      // As such, we insert " () -> ReturnType in " right after the '{' that
      // starts the closure body.
      auto insertString = " () -> " + resultTypeStr + " " + "in ";
      diagnose(closure->getLoc(), diag::cannot_infer_closure_result_type)
        .fixItInsertAfter(closure->getBody()->getLBraceLoc(), insertString);
      return true;
    }
  }
  
  diagnose(closure->getLoc(), diag::cannot_infer_closure_result_type);
  return true;
}

/// Check the associated constraint system to see if it has any archetypes
/// not properly resolved or missing. If so, diagnose the problem with
/// an error and return true.
bool FailureDiagnosis::diagnoseArchetypeAmbiguity() {
  using Archetype = std::tuple<ArchetypeType *, ConstraintLocator *, unsigned>;

  llvm::SmallVector<Archetype, 2> unboundParams;
  // Check out all of the type variables lurking in the system.  If any are
  // unbound archetypes, then the problem is that it couldn't be resolved.
  for (auto tv : CS.getTypeVariables()) {
    auto &impl = tv->getImpl();

    if (impl.hasRepresentativeOrFixed())
      continue;

    // If this is a conversion to a type variable used to form an archetype,
    // Then diagnose this as a generic parameter that could not be resolved.
    auto archetype = impl.getArchetype();

    // Only diagnose archetypes that don't have a parent, i.e., ones
    // that correspond to generic parameters.
    if (!archetype || archetype->getParent())
      continue;

    // Number of constraints related to particular unbound parameter
    // is significant indicator of the problem, because if there are
    // no constraints associated with it, that means it can't ever be resolved,
    // such helps to diagnose situations like: struct S<A, B> { init(_ a: A) {}}
    // because type B would have no constraints associated with it.
    unsigned numConstraints = 0;
    {
      llvm::SmallVector<Constraint *, 2> constraints;
      CS.getConstraintGraph().gatherConstraints(
          tv, constraints, ConstraintGraph::GatheringKind::EquivalenceClass);

      for (auto constraint : constraints) {
        // We are not interested in ConformsTo constraints because
        // such constraints specify restrictions on the archetypes themselves.
        if (constraint->getKind() == ConstraintKind::ConformsTo)
          continue;

        // Some of the bind constraints specify relations between
        // parent type and it's member fields/types, we are not
        // interested in that, since it's not related to archetype resolution.
        if (constraint->getKind() == ConstraintKind::Bind) {
          if (auto locator = constraint->getLocator()) {
            auto anchor = locator->getAnchor();
            if (anchor && isa<UnresolvedDotExpr>(anchor))
              continue;
          }
        }

        numConstraints++;
      }
    }

    auto locator = impl.getLocator();
    unboundParams.push_back(
        std::make_tuple(archetype, locator, numConstraints));
  }

  // We've found unbound generic parameters, let's diagnose
  // based on the number of constraints each one is related to.
  if (!unboundParams.empty()) {
    // Let's prioritize archetypes that don't have any constraints associated.
    std::stable_sort(unboundParams.begin(), unboundParams.end(),
                     [](Archetype a, Archetype b) {
                       return std::get<2>(a) < std::get<2>(b);
                     });

    auto param = unboundParams.front();
    diagnoseUnboundArchetype(std::get<0>(param),
                             std::get<1>(param)->getAnchor());
    return true;
  }

  return false;
}

/// Emit an error message about an unbound generic parameter existing, and
/// emit notes referring to the target of a diagnostic, e.g., the function
/// or parameter being used.
void FailureDiagnosis::diagnoseUnboundArchetype(ArchetypeType *archetype,
                                                Expr *anchor) {
  auto &tc = CS.getTypeChecker();

  // The archetype may come from the explicit type in a cast expression.
  if (auto *ECE = dyn_cast_or_null<ExplicitCastExpr>(anchor)) {
    tc.diagnose(ECE->getLoc(), diag::unbound_generic_parameter_cast,
                archetype, ECE->getCastTypeLoc().getType())
      .highlight(ECE->getCastTypeLoc().getSourceRange());

    // Emit a note specifying where this came from, if we can find it.
    noteArchetypeSource(ECE->getCastTypeLoc(), archetype, CS);
    return;
  }

  // A very common cause of this diagnostic is a situation where a closure expr
  // has no inferred type, due to being a multiline closure.  Check to see if
  // this is the case and (if so), speculatively diagnose that as the problem.
  bool didDiagnose = false;
  expr->forEachChildExpr([&](Expr *subExpr) -> Expr*{
    auto closure = dyn_cast<ClosureExpr>(subExpr);
    if (!didDiagnose && closure)
      didDiagnose = diagnoseAmbiguousMultiStatementClosure(closure);
    
    return subExpr;
  });

  if (didDiagnose) return;

  
  // Otherwise, emit an error message on the expr we have, and emit a note
  // about where the archetype came from.
  tc.diagnose(expr->getLoc(), diag::unbound_generic_parameter, archetype);
  
  // If we have an anchor, drill into it to emit a
  // "note: archetype declared here".
  if (!anchor) return;


  if (auto TE = dyn_cast<TypeExpr>(anchor)) {
    // Emit a note specifying where this came from, if we can find it.
    noteArchetypeSource(TE->getTypeLoc(), archetype, CS);
    return;
  }

  ConcreteDeclRef resolved;
  
  // Simple case: direct reference to a declaration.
  if (auto dre = dyn_cast<DeclRefExpr>(anchor))
    resolved = dre->getDeclRef();
  
  // Simple case: direct reference to a declaration.
  if (auto MRE = dyn_cast<MemberRefExpr>(anchor))
    resolved = MRE->getMember();
  
  if (auto OCDRE = dyn_cast<OtherConstructorDeclRefExpr>(anchor))
    resolved = OCDRE->getDeclRef();

  
  // We couldn't resolve the locator to a declaration, so we're done.
  if (!resolved)
    return;
  
  auto decl = resolved.getDecl();
  if (auto FD = dyn_cast<FuncDecl>(decl)) {
    auto name = FD->getFullName();
    auto diagID = name.isOperator() ? diag::note_call_to_operator
                                    : diag::note_call_to_func;
    tc.diagnose(decl, diagID, name);
    return;
  }
  
  // FIXME: Specialize for implicitly-generated constructors.
  if (isa<ConstructorDecl>(decl)) {
    tc.diagnose(decl, diag::note_call_to_initializer);
    return;
  }
  
  if (auto PD = dyn_cast<ParamDecl>(decl)) {
    tc.diagnose(decl, diag::note_init_parameter, PD->getName());
    return;
  }
  
  // FIXME: Other decl types too.
}


/// Emit an ambiguity diagnostic about the specified expression.
void FailureDiagnosis::diagnoseAmbiguity(Expr *E) {
  // First, let's try to diagnose any problems related to ambiguous
  // archetypes (generic parameters) present in the constraint system.
  if (diagnoseArchetypeAmbiguity())
    return;

  // Unresolved/Anonymous ClosureExprs are common enough that we should give
  // them tailored diagnostics.
  if (auto CE = dyn_cast<ClosureExpr>(E->getValueProvidingExpr())) {
    // If this is a multi-statement closure with no explicit result type, emit
    // a note to clue the developer in.
    if (diagnoseAmbiguousMultiStatementClosure(CE))
      return;

    diagnose(E->getLoc(), diag::cannot_infer_closure_type)
      .highlight(E->getSourceRange());
    return;
  }

  // A DiscardAssignmentExpr (spelled "_") needs contextual type information to
  // infer its type. If we see one at top level, diagnose that it must be part
  // of an assignment so we don't get a generic "expression is ambiguous" error.
  if (isa<DiscardAssignmentExpr>(E)) {
    diagnose(E->getLoc(), diag::discard_expr_outside_of_assignment)
      .highlight(E->getSourceRange());
    return;
  }
  
  // Diagnose ".foo" expressions that lack context specifically.
  if (auto UME =
        dyn_cast<UnresolvedMemberExpr>(E->getSemanticsProvidingExpr())) {
    if (!CS.getContextualType()) {
      diagnose(E->getLoc(), diag::unresolved_member_no_inference,UME->getName())
        .highlight(SourceRange(UME->getDotLoc(),
                               UME->getNameLoc().getSourceRange().End));
      return;
    }
  }
  
  // Diagnose empty collection literals that lack context specifically.
  if (auto CE = dyn_cast<CollectionExpr>(E->getSemanticsProvidingExpr())) {
    if (CE->getNumElements() == 0) {
      diagnose(E->getLoc(), diag::unresolved_collection_literal)
        .highlight(E->getSourceRange());
      return;
    }
  }

  // Diagnose 'nil' without a contextual type.
  if (isa<NilLiteralExpr>(E->getSemanticsProvidingExpr())) {
    diagnose(E->getLoc(), diag::unresolved_nil_literal)
      .highlight(E->getSourceRange());
    return;
  }

  // A very common cause of this diagnostic is a situation where a closure expr
  // has no inferred type, due to being a multiline closure.  Check to see if
  // this is the case and (if so), speculatively diagnose that as the problem.
  bool didDiagnose = false;
  E->forEachChildExpr([&](Expr *subExpr) -> Expr*{
    auto closure = dyn_cast<ClosureExpr>(subExpr);
    if (!didDiagnose && closure)
      didDiagnose = diagnoseAmbiguousMultiStatementClosure(closure);
    
    return subExpr;
  });
  
  if (didDiagnose) return;
  

  
  // Attempt to re-type-check the entire expression, allowing ambiguity, but
  // ignoring a contextual type.
  if (expr == E) {
    auto exprType = getTypeOfTypeCheckedChildIndependently(expr);
    // If it failed and diagnosed something, then we're done.
    if (!exprType) return;

    // If we were able to find something more specific than "unknown" (perhaps
    // something like "[_:_]" for a dictionary literal), include it in the
    // diagnostic.
    if (!isUnresolvedOrTypeVarType(exprType)) {
      diagnose(E->getLoc(), diag::specific_type_of_expression_is_ambiguous,
               exprType)
        .highlight(E->getSourceRange());
      return;
    }
  }

  // If there are no posted constraints or failures, then there was
  // not enough contextual information available to infer a type for the
  // expression.
  diagnose(E->getLoc(), diag::type_of_expression_is_ambiguous)
    .highlight(E->getSourceRange());
}

/// If an UnresolvedDotExpr, SubscriptMember, etc has been resolved by the
/// constraint system, return the decl that it references.
ValueDecl *ConstraintSystem::findResolvedMemberRef(ConstraintLocator *locator) {
  auto *resolvedOverloadSets = this->getResolvedOverloadSets();
  if (!resolvedOverloadSets) return nullptr;
  
  // Search through the resolvedOverloadSets to see if we have a resolution for
  // this member.  This is an O(n) search, but only happens when producing an
  // error diagnostic.
  for (auto resolved = resolvedOverloadSets;
       resolved; resolved = resolved->Previous) {
    if (resolved->Locator != locator) continue;
    
    // We only handle the simplest decl binding.
    if (resolved->Choice.getKind() != OverloadChoiceKind::Decl)
      return nullptr;
    return resolved->Choice.getDecl();
  }
  
  return nullptr;
}


bool ConstraintSystem::salvage(SmallVectorImpl<Solution> &viable, Expr *expr) {
  if (TC.getLangOpts().DebugConstraintSolver) {
    auto &log = TC.Context.TypeCheckerDebug->getStream();
    log << "---Attempting to salvage and emit diagnostics---\n";
  }

  // Attempt to solve again, capturing all states that come from our attempts to
  // select overloads or bind type variables.
  //
  // FIXME: can this be removed?  We need to arrange for recordFixes to be
  // eliminated.
  viable.clear();

  {
    // Set up solver state.
    SolverState state(expr, *this);
    state.recordFixes = true;

    // Solve the system.
    solveRec(viable, FreeTypeVariableBinding::Disallow);

    // Check whether we have a best solution; this can happen if we found
    // a series of fixes that worked.
    if (auto best = findBestSolution(viable, state.ExprWeights,
                                     /*minimize=*/true)) {
      if (*best != 0)
        viable[0] = std::move(viable[*best]);
      viable.erase(viable.begin() + 1, viable.end());
      return false;
    }

    // FIXME: If we were able to actually fix things along the way,
    // we may have to hunt for the best solution. For now, we don't care.

    // Remove solutions that require fixes; the fixes in those systems should
    // be diagnosed rather than any ambiguity.
    auto hasFixes = [](const Solution &sol) { return !sol.Fixes.empty(); };
    auto newEnd = std::remove_if(viable.begin(), viable.end(), hasFixes);
    viable.erase(newEnd, viable.end());

    // If there are multiple solutions, try to diagnose an ambiguity.
    if (viable.size() > 1) {
      if (getASTContext().LangOpts.DebugConstraintSolver) {
        auto &log = getASTContext().TypeCheckerDebug->getStream();
        log << "---Ambiguity error: "
            << viable.size() << " solutions found---\n";
        int i = 0;
        for (auto &solution : viable) {
          log << "---Ambiguous solution #" << i++ << "---\n";
          solution.dump(log);
          log << "\n";
        }
      }        

      if (diagnoseAmbiguity(*this, viable, expr)) {
        return true;
      }
    }

    // Fall through to produce diagnostics.
  }

  if (getExpressionTooComplex(viable)) {
    TC.diagnose(expr->getLoc(), diag::expression_too_complex).
      highlight(expr->getSourceRange());
    return true;
  }

  // If all else fails, diagnose the failure by looking through the system's
  // constraints.
  diagnoseFailureForExpr(expr);
  return true;
}
