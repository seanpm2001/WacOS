//===--- CSGen.cpp - Constraint Generator ---------------------------------===//
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
// This file implements constraint generation for the type checker.
//
//===----------------------------------------------------------------------===//
#include "ConstraintGraph.h"
#include "ConstraintSystem.h"
#include "swift/AST/ASTVisitor.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/Expr.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/SubstitutionMap.h"
#include "swift/Sema/IDETypeChecking.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include <utility>

using namespace swift;
using namespace swift::constraints;

/// \brief Skip any implicit conversions applied to this expression.
static Expr *skipImplicitConversions(Expr *expr) {
  while (auto ice = dyn_cast<ImplicitConversionExpr>(expr))
    expr = ice->getSubExpr();
  return expr;
}

/// \brief Find the declaration directly referenced by this expression.
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

static bool isArithmeticOperatorDecl(ValueDecl *vd) {
  return vd && 
  (vd->getBaseName() == "+" ||
   vd->getBaseName() == "-" ||
   vd->getBaseName() == "*" ||
   vd->getBaseName() == "/" ||
   vd->getBaseName() == "%");
}

static bool mergeRepresentativeEquivalenceClasses(ConstraintSystem &CS,
                                                  TypeVariableType* tyvar1,
                                                  TypeVariableType* tyvar2) {
  if (tyvar1 && tyvar2) {
    auto rep1 = CS.getRepresentative(tyvar1);
    auto rep2 = CS.getRepresentative(tyvar2);

    if (rep1 != rep2) {
      auto fixedType2 = CS.getFixedType(rep2);

      // If the there exists fixed type associated with the second
      // type variable, and we simply merge two types together it would
      // mean that portion of the constraint graph previously associated
      // with that (second) variable is going to be disconnected from its
      // new equivalence class, which is going to lead to incorrect solutions,
      // so we need to make sure to re-bind fixed to the new representative.
      if (fixedType2) {
        CS.addConstraint(ConstraintKind::Bind, fixedType2, rep1,
                         rep1->getImpl().getLocator());
      }

      CS.mergeEquivalenceClasses(rep1, rep2, /*updateWorkList*/ false);
      return true;
    }
  }

  return false;
}

namespace {
  
  /// Internal struct for tracking information about types within a series
  /// of "linked" expressions. (Such as a chain of binary operator invocations.)
  struct LinkedTypeInfo {
    unsigned haveIntLiteral : 1;
    unsigned haveFloatLiteral : 1;
    unsigned haveStringLiteral : 1;

    llvm::SmallSet<TypeBase*, 16> collectedTypes;

    llvm::SmallVector<TypeVariableType *, 16> intLiteralTyvars;
    llvm::SmallVector<TypeVariableType *, 16> floatLiteralTyvars;
    llvm::SmallVector<TypeVariableType *, 16> stringLiteralTyvars;

    llvm::SmallVector<BinaryExpr *, 4> binaryExprs;

    // TODO: manage as a set of lists, to speed up addition of binding
    // constraints.
    llvm::SmallVector<DeclRefExpr *, 16> anonClosureParams;
    
    LinkedTypeInfo() {
      haveIntLiteral = false;
      haveFloatLiteral = false;
      haveStringLiteral = false;
    }

    bool hasLiteral() {
      return haveIntLiteral || haveFloatLiteral || haveStringLiteral;
    }
  };

  /// Walks an expression sub-tree, and collects information about expressions
  /// whose types are mutually dependent upon one another.
  class LinkedExprCollector : public ASTWalker {
    
    llvm::SmallVectorImpl<Expr*> &LinkedExprs;

  public:
    
    LinkedExprCollector(llvm::SmallVectorImpl<Expr*> &linkedExprs) :
        LinkedExprs(linkedExprs) {}
    
    std::pair<bool, Expr *> walkToExprPre(Expr *expr) override {
      
      // Store top-level binary exprs for further analysis.
      if (isa<BinaryExpr>(expr) ||
          
          // Literal exprs are contextually typed, so store them off as well.
          isa<LiteralExpr>(expr) ||

          // We'd like to take a look at implicit closure params, so store
          // them.
          isa<ClosureExpr>(expr) ||

          // We'd like to look at the elements of arrays and dictionaries.
          isa<ArrayExpr>(expr) ||
          isa<DictionaryExpr>(expr) ||

          // assignment expression can involve anonymous closure parameters
          // as source and destination, so it's beneficial for diagnostics if
          // we look at the assignment.
          isa<AssignExpr>(expr)) {
        LinkedExprs.push_back(expr);
        return {false, expr};
      }
      
      return { true, expr };
    }
    
    Expr *walkToExprPost(Expr *expr) override {
      return expr;
    }
    
    /// \brief Ignore statements.
    std::pair<bool, Stmt *> walkToStmtPre(Stmt *stmt) override {
      return { false, stmt };
    }
    
    /// \brief Ignore declarations.
    bool walkToDeclPre(Decl *decl) override { return false; }
  };
  
  /// Given a collection of "linked" expressions, analyzes them for
  /// commonalities regarding their types. This will help us compute a
  /// "best common type" from the expression types.
  class LinkedExprAnalyzer : public ASTWalker {
    
    LinkedTypeInfo &LTI;
    ConstraintSystem &CS;
    
  public:
    
    LinkedExprAnalyzer(LinkedTypeInfo &lti, ConstraintSystem &cs) :
        LTI(lti), CS(cs) {}
    
    std::pair<bool, Expr *> walkToExprPre(Expr *expr) override {
      
      if (isa<IntegerLiteralExpr>(expr)) {
        LTI.haveIntLiteral = true;
        auto tyvar = CS.getType(expr)->getAs<TypeVariableType>();

        if (tyvar) {
          LTI.intLiteralTyvars.push_back(tyvar);
        }
        
        return { false, expr };
      }
      
      if (isa<FloatLiteralExpr>(expr)) {
        LTI.haveFloatLiteral = true;
        auto tyvar = CS.getType(expr)->getAs<TypeVariableType>();

        if (tyvar) {
          LTI.floatLiteralTyvars.push_back(tyvar);
        }

        return { false, expr };
      }
      
      if (isa<StringLiteralExpr>(expr)) {
        LTI.haveStringLiteral = true;

        auto tyvar = CS.getType(expr)->getAs<TypeVariableType>();

        if (tyvar) {
          LTI.stringLiteralTyvars.push_back(tyvar);
        }

        return { false, expr };
      }

      if (isa<CollectionExpr>(expr)) {
        return { true, expr };
      }
      
      if (auto UDE = dyn_cast<UnresolvedDotExpr>(expr)) {
        
        if (CS.hasType(UDE))
          LTI.collectedTypes.insert(CS.getType(UDE).getPointer());
        
        // Don't recurse into the base expression.
        return { false, expr };
      }


      if (isa<ClosureExpr>(expr)) {
        return { true, expr };
      }

      if (auto FVE = dyn_cast<ForceValueExpr>(expr)) {
        LTI.collectedTypes.insert(CS.getType(FVE).getPointer());
        return { false, expr };
      }

      if (auto DRE = dyn_cast<DeclRefExpr>(expr)) {
        if (auto varDecl = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (varDecl->isAnonClosureParam()) {
            LTI.anonClosureParams.push_back(DRE);
          } else if (CS.hasType(DRE)) {
            LTI.collectedTypes.insert(CS.getType(DRE).getPointer());
          }
          return { false, expr };
        } 
      }             

      // In the case of a function application, we would have already captured
      // the return type during constraint generation, so there's no use in
      // looking any further.
      if (isa<ApplyExpr>(expr) &&
          !(isa<BinaryExpr>(expr) || isa<PrefixUnaryExpr>(expr) ||
            isa<PostfixUnaryExpr>(expr))) {      
        return { false, expr };
      }

      if (isa<BinaryExpr>(expr)) {
        LTI.binaryExprs.push_back(dyn_cast<BinaryExpr>(expr));
      }  
      
      if (auto favoredType = CS.getFavoredType(expr)) {
        LTI.collectedTypes.insert(favoredType);

        return { false, expr };
      }

      // Optimize branches of a conditional expression separately.
      if (auto IE = dyn_cast<IfExpr>(expr)) {
        CS.optimizeConstraints(IE->getCondExpr());
        CS.optimizeConstraints(IE->getThenExpr());
        CS.optimizeConstraints(IE->getElseExpr());
        return { false, expr };
      }      

      // TODO: The systems that we need to solve for interpolated string expressions
      // require bespoke logic that don't currently work with this approach.
      if (isa<InterpolatedStringLiteralExpr>(expr)) {
        return { false, expr };
      }

      // For exprs of a structural type that are not modeling argument lists,
      // avoid merging the type variables. (We need to allow for cases like
      // (Int, Int32).)
      if (isa<TupleExpr>(expr) && !isa<ApplyExpr>(Parent.getAsExpr())) {
        return { false, expr };
      }

      // Coercion exprs have a rigid type, so there's no use in gathering info
      // about them.
      if (isa<CoerceExpr>(expr)) {
        LTI.collectedTypes.insert(CS.getType(expr).getPointer());

        return { false, expr };
      }

      // Don't walk into subscript expressions - to do so would risk factoring
      // the index expression into edge contraction. (We don't want to do this
      // if the index expression is a literal type that differs from the return
      // type of the subscript operation.)
      if (isa<SubscriptExpr>(expr) || isa<DynamicLookupExpr>(expr)) {
        return { false, expr };
      }
      
      return { true, expr };
    }
    
    /// \brief Ignore statements.
    std::pair<bool, Stmt *> walkToStmtPre(Stmt *stmt) override {
      return { false, stmt };
    }
    
    /// \brief Ignore declarations.
    bool walkToDeclPre(Decl *decl) override { return false; }
  };
  
  /// For a given expression, given information that is global to the
  /// expression, attempt to derive a favored type for it.
  bool computeFavoredTypeForExpr(Expr *expr, ConstraintSystem &CS) {
    LinkedTypeInfo lti;
    
    expr->walk(LinkedExprAnalyzer(lti, CS));

    // Link anonymous closure params of the same index.
    // TODO: As stated above, we should bucket these whilst collecting the
    // exprs to avoid quadratic behavior.
    for (auto acp1 : lti.anonClosureParams) {
      for (auto acp2 : lti.anonClosureParams) {
        if (acp1 == acp2)
          continue;

        if (acp1->getDecl()->getBaseName() == acp2->getDecl()->getBaseName()) {

          auto tyvar1 = CS.getType(acp1)->getAs<TypeVariableType>();
          auto tyvar2 = CS.getType(acp2)->getAs<TypeVariableType>();

          mergeRepresentativeEquivalenceClasses(CS, tyvar1, tyvar2);
        }
      }
    }

    // Link integer literal tyvars.
    if (lti.intLiteralTyvars.size() > 1) {
      auto rep1 = CS.getRepresentative(lti.intLiteralTyvars[0]);

      for (size_t i = 1; i < lti.intLiteralTyvars.size(); i++) {
        auto rep2 = CS.getRepresentative(lti.intLiteralTyvars[i]);

        if (rep1 != rep2)
          CS.mergeEquivalenceClasses(rep1, rep2, /*updateWorkList*/ false);
      }
    }

    // Link float literal tyvars.
    if (lti.floatLiteralTyvars.size() > 1) {
      auto rep1 = CS.getRepresentative(lti.floatLiteralTyvars[0]);

      for (size_t i = 1; i < lti.floatLiteralTyvars.size(); i++) {
        auto rep2 = CS.getRepresentative(lti.floatLiteralTyvars[i]);
        
        if (rep1 != rep2)
          CS.mergeEquivalenceClasses(rep1, rep2, /*updateWorkList*/ false);
      }
    }

    // Link string literal tyvars.
    if (lti.stringLiteralTyvars.size() > 1) {
      auto rep1 = CS.getRepresentative(lti.stringLiteralTyvars[0]);

      for (size_t i = 1; i < lti.stringLiteralTyvars.size(); i++) {
        auto rep2 = CS.getRepresentative(lti.stringLiteralTyvars[i]);

        if (rep1 != rep2)
          CS.mergeEquivalenceClasses(rep1, rep2, /*updateWorkList*/ false);
      }
    }

    if (lti.collectedTypes.size() == 1) {
      // TODO: Compute the BCT.

      // It's only useful to favor the type instead of
      // binding it directly to arguments/result types,
      // which means in case it has been miscalculated
      // solver can still make progress.
      auto favoredTy = (*lti.collectedTypes.begin())->getWithoutSpecifierType();
      CS.setFavoredType(expr, favoredTy.getPointer());

      // If we have a chain of identical binop expressions with homogeneous
      // argument types, we can directly simplify the associated constraint
      // graph.
      auto simplifyBinOpExprTyVars = [&]() {
        // Don't attempt to do linking if there are
        // literals intermingled with other inferred types.
        if (lti.hasLiteral())
          return;

        for (auto binExp1 : lti.binaryExprs) {
          for (auto binExp2 : lti.binaryExprs) {
            if (binExp1 == binExp2)
              continue;

            auto fnTy1 = CS.getType(binExp1)->getAs<TypeVariableType>();
            auto fnTy2 = CS.getType(binExp2)->getAs<TypeVariableType>();

            if (!(fnTy1 && fnTy2))
              return;

            auto ODR1 = dyn_cast<OverloadedDeclRefExpr>(binExp1->getFn());
            auto ODR2 = dyn_cast<OverloadedDeclRefExpr>(binExp2->getFn());

            if (!(ODR1 && ODR2))
              return;

            // TODO: We currently limit this optimization to known arithmetic
            // operators, but we should be able to broaden this out to
            // logical operators as well.
            if (!isArithmeticOperatorDecl(ODR1->getDecls()[0]))
              return;

            if (ODR1->getDecls()[0]->getBaseName() !=
                ODR2->getDecls()[0]->getBaseName())
              return;

            // All things equal, we can merge the tyvars for the function
            // types.
            auto rep1 = CS.getRepresentative(fnTy1);
            auto rep2 = CS.getRepresentative(fnTy2);

            if (rep1 != rep2) {
              CS.mergeEquivalenceClasses(rep1, rep2,
                                         /*updateWorkList*/ false);
            }

            auto odTy1 = CS.getType(ODR1)->getAs<TypeVariableType>();
            auto odTy2 = CS.getType(ODR2)->getAs<TypeVariableType>();

            if (odTy1 && odTy2) {
              auto odRep1 = CS.getRepresentative(odTy1);
              auto odRep2 = CS.getRepresentative(odTy2);

              // Since we'll be choosing the same overload, we can merge
              // the overload tyvar as well.
              if (odRep1 != odRep2)
                CS.mergeEquivalenceClasses(odRep1, odRep2,
                                           /*updateWorkList*/ false);
            }
          }
        }
      };

      simplifyBinOpExprTyVars();       

      return true;
    }    
    
    if (lti.haveFloatLiteral) {
      if (auto floatProto =
            CS.TC.Context.getProtocol(
                                  KnownProtocolKind::ExpressibleByFloatLiteral)) {
        if (auto defaultType = CS.TC.getDefaultType(floatProto, CS.DC)) {
          if (!CS.getFavoredType(expr)) {
            CS.setFavoredType(expr, defaultType.getPointer());
          }
          return true;
        }
      }
    }
    
    if (lti.haveIntLiteral) {
      if (auto intProto =
            CS.TC.Context.getProtocol(
                                KnownProtocolKind::ExpressibleByIntegerLiteral)) {
        if (auto defaultType = CS.TC.getDefaultType(intProto, CS.DC)) {
          if (!CS.getFavoredType(expr)) {
            CS.setFavoredType(expr, defaultType.getPointer());
          }
          return true;
        }
      }
    }
    
    if (lti.haveStringLiteral) {
      if (auto stringProto =
          CS.TC.Context.getProtocol(
                                KnownProtocolKind::ExpressibleByStringLiteral)) {
        if (auto defaultType = CS.TC.getDefaultType(stringProto, CS.DC)) {
          if (!CS.getFavoredType(expr)) {
            CS.setFavoredType(expr, defaultType.getPointer());
          }
          return true;
        }
      }
    }
    
    return false;
  }
  
  /// Determine whether the given parameter type and argument should be
  /// "favored" because they match exactly.
  bool isFavoredParamAndArg(ConstraintSystem &CS,
                            Type paramTy,
                            Expr *arg,
                            Type argTy,
                            Type otherArgTy = Type()) {
    // Determine the argument type.
    argTy = argTy->getWithoutSpecifierType();

    // Do the types match exactly?
    if (paramTy->isEqual(argTy))
      return true;

    // If the argument is a literal, this is a favored param/arg pair if
    // the parameter is of that default type.
    auto &tc = CS.getTypeChecker();
    auto literalProto = tc.getLiteralProtocol(arg->getSemanticsProvidingExpr());
    if (!literalProto) return false;

    // Dig out the second argument type.
    if (otherArgTy)
      otherArgTy = otherArgTy->getWithoutSpecifierType();

    // If there is another, concrete argument, check whether it's type
    // conforms to the literal protocol and test against it directly.
    // This helps to avoid 'widening' the favored type to the default type for
    // the literal.
    if (otherArgTy && otherArgTy->getAnyNominal()) {
      return otherArgTy->isEqual(paramTy) &&
             tc.conformsToProtocol(otherArgTy, literalProto, CS.DC,
                                   ConformanceCheckFlags::InExpression);
    }

    // If there is a default type for the literal protocol, check whether
    // it is the same as the parameter type.
    // Check whether there is a default type to compare against.
    if (Type defaultType = tc.getDefaultType(literalProto, CS.DC))
      return paramTy->isEqual(defaultType);

    return false;
  }
  
  /// Favor certain overloads in a call based on some basic analysis
  /// of the overload set and call arguments.
  ///
  /// \param expr The application.
  /// \param isFavored Determine whether the given overload is favored.
  /// \param mustConsider If provided, a function to detect the presence of
  /// overloads which inhibit any overload from being favored.
  void favorCallOverloads(ApplyExpr *expr,
                          ConstraintSystem &CS,
                          std::function<bool(ValueDecl *)> isFavored,
                          std::function<bool(ValueDecl *)>
                              mustConsider = nullptr) {
    // Find the type variable associated with the function, if any.
    auto tyvarType = CS.getType(expr->getFn())->getAs<TypeVariableType>();
    if (!tyvarType)
      return;
    
    // This type variable is only currently associated with the function
    // being applied, and the only constraint attached to it should
    // be the disjunction constraint for the overload group.
    auto &CG = CS.getConstraintGraph();
    SmallVector<Constraint *, 4> constraints;
    CG.gatherConstraints(tyvarType, constraints,
                         ConstraintGraph::GatheringKind::EquivalenceClass);
    if (constraints.empty())
      return;
    
    // Look for the disjunction that binds the overload set.
    for (auto constraint : constraints) {
      if (constraint->getKind() != ConstraintKind::Disjunction)
        continue;
      
      auto oldConstraints = constraint->getNestedConstraints();
      auto csLoc = CS.getConstraintLocator(expr->getFn());
      
      // Only replace the disjunctive overload constraint.
      if (oldConstraints[0]->getKind() != ConstraintKind::BindOverload) {
        continue;
      }

      if (mustConsider) {
        bool hasMustConsider = false;
        for (auto oldConstraint : oldConstraints) {
          auto overloadChoice = oldConstraint->getOverloadChoice();
          if (overloadChoice.isDecl() &&
              mustConsider(overloadChoice.getDecl()))
            hasMustConsider = true;
        }
        if (hasMustConsider) {
          continue;
        }
      }

      // Copy over the existing bindings, dividing the constraints up
      // into "favored" and non-favored lists.
      SmallVector<Constraint *, 4> favoredConstraints;
      SmallVector<Constraint *, 4> fallbackConstraints;
      for (auto oldConstraint : oldConstraints) {
        if (!oldConstraint->getOverloadChoice().isDecl())
          continue;
        auto decl = oldConstraint->getOverloadChoice().getDecl();
        if (!decl->getAttrs().isUnavailable(CS.getASTContext()) &&
            isFavored(decl))
          favoredConstraints.push_back(oldConstraint);
        else
          fallbackConstraints.push_back(oldConstraint);
      }

      // If we did not find any favored constraints, we're done.
      if (favoredConstraints.empty()) break;

      if (favoredConstraints.size() == 1) {
        auto overloadChoice = favoredConstraints[0]->getOverloadChoice();
        auto overloadType = overloadChoice.getDecl()->getInterfaceType();
        auto resultType = overloadType->getAs<AnyFunctionType>()->getResult();
        CS.setFavoredType(expr, resultType.getPointer());
      }

      // Remove the original constraint from the inactive constraint
      // list and add the new one.
      CS.removeInactiveConstraint(constraint);
      
      // Create the disjunction of favored constraints.
      auto favoredConstraintsDisjunction =
          Constraint::createDisjunction(CS,
                                        favoredConstraints,
                                        csLoc);
      
      favoredConstraintsDisjunction->setFavored();
      
      llvm::SmallVector<Constraint *, 2> aggregateConstraints;
      aggregateConstraints.push_back(favoredConstraintsDisjunction);

      if (!fallbackConstraints.empty()) {
        // Find the disjunction of fallback constraints. If any
        // constraints were added here, create a new disjunction.
        Constraint *fallbackConstraintsDisjunction =
          Constraint::createDisjunction(CS, fallbackConstraints, csLoc);

        aggregateConstraints.push_back(fallbackConstraintsDisjunction);
      }

      CS.addDisjunctionConstraint(aggregateConstraints, csLoc);
      break;
    }
  }
  
  size_t getOperandCount(Type t) {
    size_t nOperands = 0;
    
    if (auto parenTy = dyn_cast<ParenType>(t.getPointer())) {
      if (parenTy->getDesugaredType())
        nOperands = 1;
    } else if (auto tupleTy = t->getAs<TupleType>()) {
      nOperands = tupleTy->getElementTypes().size();
    }
    
    return nOperands;
  }
  
  /// Return a pair, containing the total parameter count of a function, coupled
  /// with the number of non-default parameters.
  std::pair<size_t, size_t> getParamCount(ValueDecl *VD) {
    auto fTy = VD->getInterfaceType()->getAs<AnyFunctionType>();
    assert(fTy && "attempting to count parameters of a non-function type");
    
    size_t nOperands = fTy->getParams().size();
    size_t nNoDefault = 0;
    
    if (auto AFD = dyn_cast<AbstractFunctionDecl>(VD)) {
      for (auto params : AFD->getParameterLists()) {
        for (auto param : *params) {
          if (!param->isDefaultArgument())
            nNoDefault++;
        }
      }
    } else {
      nNoDefault = nOperands;
    }
    
    return { nOperands, nNoDefault };
  }
  
  /// Favor unary operator constraints where we have exact matches
  /// for the operand and contextual type.
  void favorMatchingUnaryOperators(ApplyExpr *expr,
                                   ConstraintSystem &CS) {
    // Determine whether the given declaration is favored.
    auto isFavoredDecl = [&](ValueDecl *value) -> bool {
      auto valueTy = value->getInterfaceType();
      
      auto fnTy = valueTy->getAs<AnyFunctionType>();
      if (!fnTy)
        return false;
      
      // Figure out the parameter type.
      if (value->getDeclContext()->isTypeContext()) {
        fnTy = fnTy->getResult()->castTo<AnyFunctionType>();
      }
      
      Type paramTy = fnTy->getInput();
      auto resultTy = fnTy->getResult();
      auto contextualTy = CS.getContextualType(expr);

      return isFavoredParamAndArg(
                 CS, paramTy, expr->getArg(),
                 CS.getType(expr->getArg())->getWithoutParens()) &&
             (!contextualTy || contextualTy->isEqual(resultTy));
    };
    
    favorCallOverloads(expr, CS, isFavoredDecl);
  }
  
  void favorMatchingOverloadExprs(ApplyExpr *expr,
                                  ConstraintSystem &CS) {
    // Find the argument type.
    size_t nArgs = getOperandCount(CS.getType(expr->getArg()));
    auto fnExpr = expr->getFn();
    
    // Check to ensure that we have an OverloadedDeclRef, and that we're not
    // favoring multiple overload constraints. (Otherwise, in this case
    // favoring is useless.
    if (auto ODR = dyn_cast<OverloadedDeclRefExpr>(fnExpr)) {
      bool haveMultipleApplicableOverloads = false;
      
      for (auto VD : ODR->getDecls()) {
        if (VD->getInterfaceType()->is<AnyFunctionType>()) {
          auto nParams = getParamCount(VD);
          
          if (nArgs == nParams.first) {
            if (haveMultipleApplicableOverloads) {
              return;
            } else {
              haveMultipleApplicableOverloads = true;
            }
          }
        }
      }
      
      // Determine whether the given declaration is favored.
      auto isFavoredDecl = [&](ValueDecl *value) -> bool {
        auto valueTy = value->getInterfaceType();
        
        if (!valueTy->is<AnyFunctionType>())
          return false;

        auto paramCount = getParamCount(value);
        
        return nArgs == paramCount.first ||
               nArgs == paramCount.second;
      };
      
      favorCallOverloads(expr, CS, isFavoredDecl);
      
    }
    
    if (auto favoredTy = CS.getFavoredType(expr->getArg())) {
      // Determine whether the given declaration is favored.
      auto isFavoredDecl = [&](ValueDecl *value) -> bool {
        auto valueTy = value->getInterfaceType();
        
        auto fnTy = valueTy->getAs<AnyFunctionType>();
        if (!fnTy)
          return false;

        // Figure out the parameter type, accounting for the implicit 'self' if
        // necessary.
        if (auto *FD = dyn_cast<AbstractFunctionDecl>(value)) {
          if (FD->getImplicitSelfDecl()) {
            if (auto resFnTy = fnTy->getResult()->getAs<AnyFunctionType>()) {
              fnTy = resFnTy;
            }
          }
        }
        Type paramTy = fnTy->getInput();
        return favoredTy->isEqual(paramTy);
      };

      // This is a hack to ensure we always consider the protocol requirement
      // itself when calling something that has a default implementation in an
      // extension. Otherwise, the extension method might be favored if we're
      // inside an extension context, since any archetypes in the parameter
      // list could match exactly.
      auto mustConsider = [&](ValueDecl *value) -> bool {
        return isa<ProtocolDecl>(value->getDeclContext());
      };

      favorCallOverloads(expr, CS,
                         isFavoredDecl,
                         mustConsider);
    }
  }
  
  /// Favor binary operator constraints where we have exact matches
  /// for the operands and contextual type.
  void favorMatchingBinaryOperators(ApplyExpr *expr,
                                    ConstraintSystem &CS) {
    // If we're generating constraints for a binary operator application,
    // there are two special situations to consider:
    //  1. If the type checker has any newly created functions with the
    //     operator's name. If it does, the overloads were created after the
    //     associated overloaded id expression was created, and we'll need to
    //     add a new disjunction constraint for the new set of overloads.
    //  2. If any component argument expressions (nested or otherwise) are
    //     literals, we can favor operator overloads whose argument types are
    //     identical to the literal type, or whose return types are identical
    //     to any contextual type associated with the application expression.
    
    // Find the argument types.
    auto argTy = CS.getType(expr->getArg());
    auto argTupleTy = argTy->castTo<TupleType>();
    auto argTupleExpr = dyn_cast<TupleExpr>(expr->getArg());

    Type firstArgTy = argTupleTy->getElement(0).getType()->getWithoutParens();
    Type secondArgTy = argTupleTy->getElement(1).getType()->getWithoutParens();

    auto isOptionalWithMatchingObjectType = [](Type optional,
                                               Type object) -> bool {
      if (auto objTy = optional->getRValueType()->getAnyOptionalObjectType())
        return objTy->getRValueType()->isEqual(object->getRValueType());

      return false;
    };

    auto isPotentialForcingOpportunity = [&](Type first, Type second) -> bool {
      return isOptionalWithMatchingObjectType(first, second) ||
             isOptionalWithMatchingObjectType(second, first);
    };

    // Determine whether the given declaration is favored.
    auto isFavoredDecl = [&](ValueDecl *value) -> bool {
      auto valueTy = value->getInterfaceType();
      
      auto fnTy = valueTy->getAs<AnyFunctionType>();
      if (!fnTy)
        return false;

      Expr *firstArg = argTupleExpr->getElement(0);
      auto firstFavoredTy = CS.getFavoredType(firstArg);
      Expr *secondArg = argTupleExpr->getElement(1);
      auto secondFavoredTy = CS.getFavoredType(secondArg);
      
      auto favoredExprTy = CS.getFavoredType(expr);
      
      if (isArithmeticOperatorDecl(value)) {
        // If the parent has been favored on the way down, propagate that
        // information to its children.
        // TODO: This is only valid for arithmetic expressions.
        if (!firstFavoredTy) {
          CS.setFavoredType(argTupleExpr->getElement(0), favoredExprTy);
          firstFavoredTy = favoredExprTy;
        }
        
        if (!secondFavoredTy) {
          CS.setFavoredType(argTupleExpr->getElement(1), favoredExprTy);
          secondFavoredTy = favoredExprTy;
        }
        
        if (firstFavoredTy && firstArgTy->is<TypeVariableType>()) {
          firstArgTy = firstFavoredTy;
        }
        
        if (secondFavoredTy && secondArgTy->is<TypeVariableType>()) {
          secondArgTy = secondFavoredTy;
        }
      }
      
      // Figure out the parameter type.
      if (value->getDeclContext()->isTypeContext()) {
        fnTy = fnTy->getResult()->castTo<AnyFunctionType>();
      }
      
      Type paramTy = fnTy->getInput();
      auto paramTupleTy = paramTy->getAs<TupleType>();
      if (!paramTupleTy || paramTupleTy->getNumElements() != 2)
        return false;
      
      auto firstParamTy = paramTupleTy->getElement(0).getType();
      auto secondParamTy = paramTupleTy->getElement(1).getType();
      
      auto resultTy = fnTy->getResult();
      auto contextualTy = CS.getContextualType(expr);

      return (isFavoredParamAndArg(CS, firstParamTy, firstArg, firstArgTy,
                                   secondArgTy) ||
              isFavoredParamAndArg(CS, secondParamTy, secondArg, secondArgTy,
                                   firstArgTy)) &&
             firstParamTy->isEqual(secondParamTy) &&
             !isPotentialForcingOpportunity(firstArgTy, secondArgTy) &&
             (!contextualTy || contextualTy->isEqual(resultTy));
    };
    
    favorCallOverloads(expr, CS, isFavoredDecl);
  }
  
  class ConstraintOptimizer : public ASTWalker {
    ConstraintSystem &CS;
    
  public:
    
    ConstraintOptimizer(ConstraintSystem &cs) :
      CS(cs) {}
    
    std::pair<bool, Expr *> walkToExprPre(Expr *expr) override {
      
      if (auto applyExpr = dyn_cast<ApplyExpr>(expr)) {
        if (isa<PrefixUnaryExpr>(applyExpr) ||
            isa<PostfixUnaryExpr>(applyExpr)) {
          favorMatchingUnaryOperators(applyExpr, CS);
        } else if (isa<BinaryExpr>(applyExpr)) {
          favorMatchingBinaryOperators(applyExpr, CS);
        } else {
          favorMatchingOverloadExprs(applyExpr, CS);
        }
      }
      
      // If the paren expr has a favored type, and the subExpr doesn't,
      // propagate downwards. Otherwise, propagate upwards.
      if (auto parenExpr = dyn_cast<ParenExpr>(expr)) {
        if (!CS.getFavoredType(parenExpr->getSubExpr())) {
          CS.setFavoredType(parenExpr->getSubExpr(),
                            CS.getFavoredType(parenExpr));
        } else if (!CS.getFavoredType(parenExpr)) {
          CS.setFavoredType(parenExpr,
                            CS.getFavoredType(parenExpr->getSubExpr()));
        }
      }
      
      return { true, expr };
    }
    
    Expr *walkToExprPost(Expr *expr) override {
      return expr;
    }
    
    /// \brief Ignore statements.
    std::pair<bool, Stmt *> walkToStmtPre(Stmt *stmt) override {
      return { false, stmt };
    }
    
    /// \brief Ignore declarations.
    bool walkToDeclPre(Decl *decl) override { return false; }
  };
} // end anonymous namespace

namespace {

  class ConstraintGenerator : public ExprVisitor<ConstraintGenerator, Type> {
    ConstraintSystem &CS;
    DeclContext *CurDC;
    SmallVector<DeclContext*, 4> DCStack;

    static const unsigned numEditorPlaceholderVariables = 2;

    /// A buffer of type variables used for editor placeholders. We only
    /// use a small number of these (rotating through), to prevent expressions
    /// with a large number of editor placeholders from flooding the constraint
    /// system with type variables.
    TypeVariableType *editorPlaceholderVariables[numEditorPlaceholderVariables]
      = { nullptr, nullptr };
    unsigned currentEditorPlaceholderVariable = 0;

    /// \brief Add constraints for a reference to a named member of the given
    /// base type, and return the type of such a reference.
    Type addMemberRefConstraints(Expr *expr, Expr *base, DeclName name,
                                 FunctionRefKind functionRefKind) {
      // The base must have a member of the given name, such that accessing
      // that member through the base returns a value convertible to the type
      // of this expression.
      auto baseTy = CS.getType(base);
      auto tv = CS.createTypeVariable(
                  CS.getConstraintLocator(expr, ConstraintLocator::Member),
                  TVO_CanBindToLValue |
                  TVO_CanBindToInOut);
      CS.addValueMemberConstraint(baseTy, name, tv, CurDC, functionRefKind,
        CS.getConstraintLocator(expr, ConstraintLocator::Member));
      return tv;
    }

    /// \brief Add constraints for a reference to a specific member of the given
    /// base type, and return the type of such a reference.
    Type addMemberRefConstraints(Expr *expr, Expr *base, ValueDecl *decl,
                                 FunctionRefKind functionRefKind) {
      // If we're referring to an invalid declaration, fail.
      if (!decl)
        return nullptr;
      
      CS.getTypeChecker().validateDecl(decl);
      if (decl->isInvalid())
        return nullptr;

      auto memberLocator =
        CS.getConstraintLocator(expr, ConstraintLocator::Member);
      auto tv = CS.createTypeVariable(memberLocator,
                                      TVO_CanBindToLValue |
                                      TVO_CanBindToInOut);

      OverloadChoice choice(CS.getType(base), decl, functionRefKind);
      auto locator = CS.getConstraintLocator(expr, ConstraintLocator::Member);
      CS.addBindOverloadConstraint(tv, choice, locator, CurDC);
      return tv;
    }

    /// \brief Add constraints for a subscript operation.
    Type addSubscriptConstraints(Expr *anchor, Type baseTy, Expr *index,
                                 ValueDecl *declOrNull,
                                 ConstraintLocator *memberLocator = nullptr,
                                 ConstraintLocator *indexLocator = nullptr) {
      // Locators used in this expression.
      if (!indexLocator)
        indexLocator
          = CS.getConstraintLocator(anchor, ConstraintLocator::SubscriptIndex);
      auto resultLocator
        = CS.getConstraintLocator(anchor, ConstraintLocator::SubscriptResult);
      
      Type outputTy;

      // The base type must have a subscript declaration with type
      // I -> inout? O, where I and O are fresh type variables. The index
      // expression must be convertible to I and the subscript expression
      // itself has type inout? O, where O may or may not be an lvalue.
      auto inputTv = CS.createTypeVariable(indexLocator, TVO_CanBindToInOut);
      
      // For an integer subscript expression on an array slice type, instead of
      // introducing a new type variable we can easily obtain the element type.
      if (isa<SubscriptExpr>(anchor)) {
        
        auto isLValueBase = false;
        auto baseObjTy = baseTy;
        if (baseObjTy->is<LValueType>()) {
          isLValueBase = true;
          baseObjTy = baseObjTy->getWithoutSpecifierType();
        }
        
        if (CS.isArrayType(baseObjTy.getPointer())) {

          if (auto arraySliceTy = 
                dyn_cast<ArraySliceType>(baseObjTy.getPointer())) {
            baseObjTy = arraySliceTy->getDesugaredType();
          }
          
          auto indexExpr = index;
          
          if (auto parenExpr = dyn_cast<ParenExpr>(indexExpr)) {
            indexExpr = parenExpr->getSubExpr();
          }
          
          if (isa<IntegerLiteralExpr>(indexExpr)) {
            
            outputTy = baseObjTy->getAs<BoundGenericType>()->getGenericArgs()[0];
            
            if (isLValueBase)
              outputTy = LValueType::get(outputTy);
          }
        } else if (auto dictTy = CS.isDictionaryType(baseObjTy)) {
          auto keyTy = dictTy->first;
          auto valueTy = dictTy->second;

          if (isFavoredParamAndArg(CS, keyTy, index, CS.getType(index))) {
            outputTy = OptionalType::get(valueTy);
            
            if (isLValueBase)
              outputTy = LValueType::get(outputTy);
          }
        }
      }
      
      if (outputTy.isNull()) {
        outputTy = CS.createTypeVariable(resultLocator,
                                         TVO_CanBindToLValue |
                                         TVO_CanBindToInOut);
      } else {
        // TODO: Generalize this for non-subscript-expr anchors, so that e.g.
        // keypath lookup benefits from the peephole as well.
        CS.setFavoredType(anchor, outputTy.getPointer());
      }

      if (!memberLocator)
        memberLocator
          = CS.getConstraintLocator(anchor, ConstraintLocator::SubscriptMember);

      // FIXME: This can only happen when diagnostics successfully type-checked
      // sub-expression of the subscript and mutated AST, but under normal
      // circumstances subscript should never have InOutExpr as a direct child
      // until type checking is complete and expression is re-written.
      // Proper fix for such situation requires preventing diagnostics from
      // re-writing AST after successful type checking of the sub-expressions.
      if (auto inoutTy = baseTy->getAs<InOutType>()) {
        baseTy = LValueType::get(inoutTy->getObjectType());
      }

      // Add the member constraint for a subscript declaration.
      // FIXME: weak name!
      auto fnTy = FunctionType::get(inputTv, outputTy);

      // FIXME: synthesizeMaterializeForSet() wants to statically dispatch to
      // a known subscript here. This might be cleaner if we split off a new
      // UnresolvedSubscriptExpr from SubscriptExpr.
      if (auto decl = declOrNull) {
        OverloadChoice choice(baseTy, decl, FunctionRefKind::DoubleApply);
        CS.addBindOverloadConstraint(fnTy, choice, memberLocator,
                                     CurDC);
      } else {
        CS.addValueMemberConstraint(baseTy, DeclBaseName::createSubscript(),
                                    fnTy, CurDC, FunctionRefKind::DoubleApply,
                                    memberLocator);
      }

      // Add the constraint that the index expression's type be convertible
      // to the input type of the subscript operator.
      CS.addConstraint(ConstraintKind::ArgumentTupleConversion,
                       CS.getType(index), inputTv, indexLocator);
      return outputTy;
    }

  public:
    ConstraintGenerator(ConstraintSystem &CS) : CS(CS), CurDC(CS.DC) { }
    virtual ~ConstraintGenerator() {
      // We really ought to have this assertion:
      //   assert(DCStack.empty() && CurDC == CS.DC);
      // Unfortunately, ASTWalker is really bad at letting us establish
      // invariants like this because walkToExprPost isn't called if
      // something early-aborts the walk.
    }

    ConstraintSystem &getConstraintSystem() const { return CS; }

    void enterClosure(ClosureExpr *closure) {
      DCStack.push_back(CurDC);
      CurDC = closure;
    }

    void exitClosure(ClosureExpr *closure) {
      assert(CurDC == closure);
      CurDC = DCStack.pop_back_val();
    }
    
    virtual Type visitErrorExpr(ErrorExpr *E) {
      // FIXME: Can we do anything with error expressions at this point?
      return nullptr;
    }

    virtual Type visitCodeCompletionExpr(CodeCompletionExpr *E) {
      // If the expression has already been assigned a type; just use that type.
      return E->getType();
    }

    Type visitLiteralExpr(LiteralExpr *expr) {
      // If the expression has already been assigned a type; just use that type.
      if (expr->getType() && !expr->getType()->hasTypeVariable())
        return expr->getType();

      auto protocol = CS.getTypeChecker().getLiteralProtocol(expr);
      if (!protocol)
        return nullptr;
      

      auto tv = CS.createTypeVariable(CS.getConstraintLocator(expr),
                                      TVO_PrefersSubtypeBinding |
                                      TVO_CanBindToInOut);
      CS.addConstraint(ConstraintKind::LiteralConformsTo, tv,
                       protocol->getDeclaredType(),
                       CS.getConstraintLocator(expr));
      return tv;
    }

    Type
    visitInterpolatedStringLiteralExpr(InterpolatedStringLiteralExpr *expr) {
      // Dig out the ExpressibleByStringInterpolation protocol.
      auto &tc = CS.getTypeChecker();
      auto interpolationProto
        = tc.getProtocol(expr->getLoc(),
                         KnownProtocolKind::ExpressibleByStringInterpolation);
      if (!interpolationProto) {
        tc.diagnose(expr->getStartLoc(), diag::interpolation_missing_proto);
        return nullptr;
      }

      // The type of the expression must conform to the
      // ExpressibleByStringInterpolation protocol.
      auto locator = CS.getConstraintLocator(expr);
      auto tv = CS.createTypeVariable(locator,
                                      TVO_PrefersSubtypeBinding |
                                      TVO_CanBindToInOut);
      CS.addConstraint(ConstraintKind::LiteralConformsTo, tv,
                       interpolationProto->getDeclaredType(),
                       locator);

      return tv;
    }

    Type visitMagicIdentifierLiteralExpr(MagicIdentifierLiteralExpr *expr) {
      switch (expr->getKind()) {
      case MagicIdentifierLiteralExpr::Column:
      case MagicIdentifierLiteralExpr::File:
      case MagicIdentifierLiteralExpr::Function:
      case MagicIdentifierLiteralExpr::Line:
        return visitLiteralExpr(expr);

      case MagicIdentifierLiteralExpr::DSOHandle: {
        // #dsohandle has type UnsafeMutableRawPointer.
        auto &tc = CS.getTypeChecker();
        if (tc.requirePointerArgumentIntrinsics(expr->getLoc()))
          return nullptr;

        auto unsafeRawPointer =
          CS.getASTContext().getUnsafeRawPointerDecl();
        return unsafeRawPointer->getDeclaredType();
      }
      }

      llvm_unreachable("Unhandled MagicIdentifierLiteralExpr in switch.");
    }

    Type visitObjectLiteralExpr(ObjectLiteralExpr *expr) {
      // If the expression has already been assigned a type; just use that type.
      if (expr->getType() && !expr->getType()->hasTypeVariable())
        return expr->getType();

      auto &tc = CS.getTypeChecker();
      auto protocol = tc.getLiteralProtocol(expr);
      if (!protocol) {
        tc.diagnose(expr->getLoc(), diag::use_unknown_object_literal_protocol,
                    expr->getLiteralKindPlainName());
        return nullptr;
      }

      auto tv = CS.createTypeVariable(CS.getConstraintLocator(expr),
                                      TVO_PrefersSubtypeBinding |
                                      TVO_CanBindToInOut);
      
      CS.addConstraint(ConstraintKind::LiteralConformsTo, tv,
                       protocol->getDeclaredType(),
                       CS.getConstraintLocator(expr));

      // The arguments are required to be argument-convertible to the
      // idealized parameter type of the initializer, which generally
      // simplifies the first label (e.g. "colorLiteralRed:") by stripping
      // all the redundant stuff about literals (leaving e.g. "red:").
      // Constraint application will quietly rewrite the type of 'args' to
      // use the right labels before forming the call to the initializer.
      DeclName constrName = tc.getObjectLiteralConstructorName(expr);
      assert(constrName);
      ArrayRef<ValueDecl *> constrs = protocol->lookupDirect(constrName);
      if (constrs.size() != 1 || !isa<ConstructorDecl>(constrs.front())) {
        tc.diagnose(protocol, diag::object_literal_broken_proto);
        return nullptr;
      }
      auto *constr = cast<ConstructorDecl>(constrs.front());
      auto constrParamType = tc.getObjectLiteralParameterType(expr, constr);
      CS.addConstraint(
          ConstraintKind::ArgumentTupleConversion, CS.getType(expr->getArg()),
          constrParamType,
          CS.getConstraintLocator(expr, ConstraintLocator::ApplyArgument));

      Type result = tv;
      if (constr->getFailability() != OTK_None) {
        result = OptionalType::get(constr->getFailability(), result);
      }

      return result;
    }

    Type visitDeclRefExpr(DeclRefExpr *E) {
      // If this is a ParamDecl for a closure argument that has an Unresolved
      // type, then this is a situation where CSDiags is trying to perform
      // error recovery within a ClosureExpr.  Just create a new type variable
      // for the decl that isn't bound to anything.  This will ensure that it
      // is considered ambiguous.
      if (auto *VD = dyn_cast<VarDecl>(E->getDecl())) {
        if (VD->hasInterfaceType() &&
            VD->getInterfaceType()->is<UnresolvedType>()) {
          return CS.createTypeVariable(CS.getConstraintLocator(E),
                                       TVO_CanBindToLValue |
                                       TVO_CanBindToInOut);
        }
      }

      // If we're referring to an invalid declaration, don't type-check.
      //
      // FIXME: If the decl is in error, we get no information from this.
      // We may, alternatively, want to use a type variable in that case,
      // and possibly infer the type of the variable that way.
      CS.getTypeChecker().validateDecl(E->getDecl());
      if (E->getDecl()->isInvalid()) {
        CS.setType(E, E->getDecl()->getInterfaceType());
        return nullptr;
      }

      auto locator = CS.getConstraintLocator(E);
      
      // Create an overload choice referencing this declaration and immediately
      // resolve it. This records the overload for use later.
      auto tv = CS.createTypeVariable(locator,
                                      TVO_CanBindToLValue);
      CS.resolveOverload(locator, tv,
                         OverloadChoice(Type(), E->getDecl(),
                                        E->getFunctionRefKind()),
                         CurDC);
      
      if (auto *VD = dyn_cast<VarDecl>(E->getDecl())) {
        if (VD->getInterfaceType() &&
            !VD->getInterfaceType()->is<TypeVariableType>()) {
          // FIXME: ParamDecls in closures shouldn't get an interface type
          // until the constraint system has been solved.
          auto type = VD->getInterfaceType();
          if (type->hasTypeParameter())
            type = VD->getDeclContext()->mapTypeIntoContext(type);
          CS.setFavoredType(E, type.getPointer());
        }
      }

      return tv;
    }

    Type visitOtherConstructorDeclRefExpr(OtherConstructorDeclRefExpr *E) {
      return E->getType();
    }

    Type visitSuperRefExpr(SuperRefExpr *E) {
      if (E->getType())
        return E->getType();

      // Resolve the super type of 'self'.
      return getSuperType(E->getSelf(), E->getLoc(),
                          diag::super_not_in_class_method,
                          diag::super_with_no_base_class);
    }

    Type resolveTypeReferenceInExpression(TypeRepr *rep) {
      TypeResolutionOptions options = TypeResolutionFlags::AllowUnboundGenerics;
      options |= TypeResolutionFlags::InExpression;
      return CS.TC.resolveType(rep, CS.DC, options);
    }

    Type visitTypeExpr(TypeExpr *E) {
      Type type;
      // If this is an implicit TypeExpr, don't validate its contents.
      if (auto *rep = E->getTypeRepr()) {
        type = resolveTypeReferenceInExpression(rep);
      } else {
        type = E->getTypeLoc().getType();
      }
      if (!type || type->hasError()) return Type();
      
      auto locator = CS.getConstraintLocator(E);
      type = CS.openUnboundGenericType(type, locator);
      E->getTypeLoc().setType(type, /*validated=*/true);
      return MetatypeType::get(type);
    }

    Type visitDotSyntaxBaseIgnoredExpr(DotSyntaxBaseIgnoredExpr *expr) {
      llvm_unreachable("Already type-checked");
    }

    Type visitOverloadedDeclRefExpr(OverloadedDeclRefExpr *expr) {
      // For a reference to an overloaded declaration, we create a type variable
      // that will be equal to different types depending on which overload
      // is selected.
      auto locator = CS.getConstraintLocator(expr);
      auto tv = CS.createTypeVariable(locator,
                                      TVO_CanBindToLValue |
                                      TVO_CanBindToInOut);
      ArrayRef<ValueDecl*> decls = expr->getDecls();
      SmallVector<OverloadChoice, 4> choices;
      
      for (unsigned i = 0, n = decls.size(); i != n; ++i) {
        // If the result is invalid, skip it.
        // FIXME: Note this as invalid, in case we don't find a solution,
        // so we don't let errors cascade further.
        CS.getTypeChecker().validateDecl(decls[i]);
        if (decls[i]->isInvalid())
          continue;

        choices.push_back(OverloadChoice(Type(), decls[i],
                                         expr->getFunctionRefKind()));
      }

      // If there are no valid overloads, give up.
      if (choices.empty())
        return nullptr;

      // Record this overload set.
      CS.addOverloadSet(tv, choices, CurDC, locator);
      return tv;
    }

    Type visitUnresolvedDeclRefExpr(UnresolvedDeclRefExpr *expr) {
      // This is an error case, where we're trying to use type inference
      // to help us determine which declaration the user meant to refer to.
      // FIXME: Do we need to note that we're doing some kind of recovery?
      return CS.createTypeVariable(CS.getConstraintLocator(expr),
                                   TVO_CanBindToLValue |
                                   TVO_CanBindToInOut);
    }
    
    Type visitMemberRefExpr(MemberRefExpr *expr) {
      return addMemberRefConstraints(expr, expr->getBase(),
                                     expr->getMember().getDecl(),
                                     /*FIXME:*/FunctionRefKind::DoubleApply);
    }
    
    Type visitDynamicMemberRefExpr(DynamicMemberRefExpr *expr) {
      return addMemberRefConstraints(expr, expr->getBase(),
                                     expr->getMember().getDecl(),
                                     /*FIXME:*/FunctionRefKind::DoubleApply);
    }
    
    virtual Type visitUnresolvedMemberExpr(UnresolvedMemberExpr *expr) {
      auto baseLocator = CS.getConstraintLocator(
                            expr,
                            ConstraintLocator::MemberRefBase);
      FunctionRefKind functionRefKind =
        expr->getArgument() ? FunctionRefKind::DoubleApply
                            : FunctionRefKind::Compound;

      auto memberLocator
        = CS.getConstraintLocator(expr, ConstraintLocator::UnresolvedMember);
      auto baseTy = CS.createTypeVariable(baseLocator, TVO_CanBindToInOut);
      auto memberTy = CS.createTypeVariable(memberLocator,
                                            TVO_CanBindToLValue |
                                            TVO_CanBindToInOut);

      // An unresolved member expression '.member' is modeled as a value member
      // constraint
      //
      //   T0.Type[.member] == T1
      //
      // for fresh type variables T0 and T1, which pulls out a static
      // member, i.e., an enum case or a static variable.
      auto baseMetaTy = MetatypeType::get(baseTy);
      CS.addUnresolvedValueMemberConstraint(baseMetaTy, expr->getName(),
                                            memberTy, CurDC, functionRefKind,
                                            memberLocator);

      // If there is an argument, apply it.
      if (auto arg = expr->getArgument()) {
        // The result type of the function must be convertible to the base type.
        // TODO: we definitely want this to include ImplicitlyUnwrappedOptional; does it
        // need to include everything else in the world?
        auto outputTy
          = CS.createTypeVariable(
              CS.getConstraintLocator(expr, ConstraintLocator::ApplyFunction),
              TVO_CanBindToInOut);
        CS.addConstraint(ConstraintKind::Conversion, outputTy, baseTy,
          CS.getConstraintLocator(expr, ConstraintLocator::RvalueAdjustment));

        // The function/enum case must be callable with the given argument.
        auto funcTy = FunctionType::get(CS.getType(arg), outputTy);
        CS.addConstraint(ConstraintKind::ApplicableFunction, funcTy,
          memberTy,
          CS.getConstraintLocator(expr, ConstraintLocator::ApplyFunction));
        
        return baseTy;
      }

      // Otherwise, the member needs to be convertible to the base type.
      CS.addConstraint(ConstraintKind::Conversion, memberTy, baseTy,
        CS.getConstraintLocator(expr, ConstraintLocator::RvalueAdjustment));
      
      // The member type also needs to be convertible to the context type, which
      // preserves lvalue-ness.
      auto resultTy = CS.createTypeVariable(memberLocator,
                                            TVO_CanBindToLValue |
                                            TVO_CanBindToInOut);
      CS.addConstraint(ConstraintKind::Conversion, memberTy, resultTy,
                       memberLocator);
      CS.addConstraint(ConstraintKind::Equal, resultTy, baseTy,
                       memberLocator);
      return resultTy;
    }

    Type visitUnresolvedDotExpr(UnresolvedDotExpr *expr) {
      // If this is Builtin.type_join*, just return any type and move
      // on since we're going to discard this, and creating any type
      // variables for the reference will cause problems.
      auto typeOperation = getTypeOperation(expr, CS.getASTContext());
      if (typeOperation != TypeOperation::None)
        return CS.getASTContext().TheAnyType;

      // Open a member constraint for constructor delegations on the
      // subexpr type.
      if (CS.TC.getSelfForInitDelegationInConstructor(CS.DC, expr)) {
        auto baseTy = CS.getType(expr->getBase())
                        ->getWithoutSpecifierType();

        // 'self' or 'super' will reference an instance, but the constructor
        // is semantically a member of the metatype. This:
        //   self.init()
        //   super.init()
        // is really more like:
        //   self = Self.init()
        //   self.super = Super.init()
        baseTy = MetatypeType::get(baseTy, CS.getASTContext());

        auto argsTy = CS.createTypeVariable(
                        CS.getConstraintLocator(expr),
                          TVO_CanBindToLValue |
                          TVO_PrefersSubtypeBinding |
                          TVO_CanBindToInOut);
        auto resultTy = CS.createTypeVariable(CS.getConstraintLocator(expr),
                                              TVO_CanBindToInOut);
        auto methodTy = FunctionType::get(argsTy, resultTy);
        CS.addValueMemberConstraint(baseTy, expr->getName(),
          methodTy, CurDC, expr->getFunctionRefKind(),
          CS.getConstraintLocator(expr, ConstraintLocator::ConstructorMember));

        // The result of the expression is the partial application of the
        // constructor to the subexpression.
        return methodTy;
      }

      return addMemberRefConstraints(expr, expr->getBase(), expr->getName(),
                                     expr->getFunctionRefKind());
    }
    
    Type visitUnresolvedSpecializeExpr(UnresolvedSpecializeExpr *expr) {
      auto baseTy = CS.getType(expr->getSubExpr());
      
      // We currently only support explicit specialization of generic types.
      // FIXME: We could support explicit function specialization.
      auto &tc = CS.getTypeChecker();
      if (baseTy->is<AnyFunctionType>()) {
        tc.diagnose(expr->getSubExpr()->getLoc(),
                    diag::cannot_explicitly_specialize_generic_function);
        tc.diagnose(expr->getLAngleLoc(),
                    diag::while_parsing_as_left_angle_bracket);
        return Type();
      }
      
      if (AnyMetatypeType *meta = baseTy->getAs<AnyMetatypeType>()) {
        if (BoundGenericType *bgt
              = meta->getInstanceType()->getAs<BoundGenericType>()) {
          ArrayRef<Type> typeVars = bgt->getGenericArgs();
          MutableArrayRef<TypeLoc> specializations = expr->getUnresolvedParams();

          // If we have too many generic arguments, complain.
          if (specializations.size() > typeVars.size()) {
            tc.diagnose(expr->getSubExpr()->getLoc(),
                        diag::type_parameter_count_mismatch,
                        bgt->getDecl()->getName(),
                        typeVars.size(), specializations.size(),
                        false)
              .highlight(SourceRange(expr->getLAngleLoc(),
                                     expr->getRAngleLoc()));
            tc.diagnose(bgt->getDecl(), diag::generic_type_declared_here,
                        bgt->getDecl()->getName());
            return Type();
          }

          // Bind the specified generic arguments to the type variables in the
          // open type.
          auto locator = CS.getConstraintLocator(expr);
          for (size_t i = 0, size = specializations.size(); i < size; ++i) {
            TypeResolutionOptions options = TypeResolutionFlags::InExpression;
            options |= TypeResolutionFlags::AllowUnboundGenerics;
            if (tc.validateType(specializations[i], CS.DC, options))
              return Type();

            CS.addConstraint(ConstraintKind::Equal,
                             typeVars[i], specializations[i].getType(),
                             locator);
          }
          
          return baseTy;
        } else {
          tc.diagnose(expr->getSubExpr()->getLoc(), diag::not_a_generic_type,
                      meta->getInstanceType());
          tc.diagnose(expr->getLAngleLoc(),
                      diag::while_parsing_as_left_angle_bracket);
          return Type();
        }
      }

      // FIXME: If the base type is a type variable, constrain it to a metatype
      // of a bound generic type.
      tc.diagnose(expr->getSubExpr()->getLoc(),
                  diag::not_a_generic_definition);
      tc.diagnose(expr->getLAngleLoc(),
                  diag::while_parsing_as_left_angle_bracket);
      return Type();
    }
    
    Type visitSequenceExpr(SequenceExpr *expr) {
      // If a SequenceExpr survived until CSGen, then there was an upstream
      // error that was already reported.
      return Type();
    }

    Type visitArrowExpr(ArrowExpr *expr) {
      // If an ArrowExpr survived until CSGen, then there was an upstream
      // error that was already reported.
      return Type();
    }

    Type visitIdentityExpr(IdentityExpr *expr) {
      return CS.getType(expr->getSubExpr());
    }

    Type visitAnyTryExpr(AnyTryExpr *expr) {
      return CS.getType(expr->getSubExpr());
    }

    Type visitOptionalTryExpr(OptionalTryExpr *expr) {
      auto valueTy = CS.createTypeVariable(CS.getConstraintLocator(expr),
                                           TVO_PrefersSubtypeBinding |
                                           TVO_CanBindToInOut);

      Type optTy = getOptionalType(expr->getSubExpr()->getLoc(), valueTy);
      if (!optTy)
        return Type();

      CS.addConstraint(ConstraintKind::OptionalObject,
                       optTy, CS.getType(expr->getSubExpr()),
                       CS.getConstraintLocator(expr));
      return optTy;
    }

    virtual Type visitParenExpr(ParenExpr *expr) {
      if (auto favoredTy = CS.getFavoredType(expr->getSubExpr())) {
        CS.setFavoredType(expr, favoredTy);
      }

      auto &ctx = CS.getASTContext();
      auto parenType = CS.getType(expr->getSubExpr())->getInOutObjectType();
      auto parenFlags = ParameterTypeFlags().withInOut(expr->isSemanticallyInOutExpr());
      return ParenType::get(ctx, parenType, parenFlags);
    }

    Type visitTupleExpr(TupleExpr *expr) {
      // The type of a tuple expression is simply a tuple of the types of
      // its subexpressions.
      SmallVector<TupleTypeElt, 4> elements;
      elements.reserve(expr->getNumElements());
      for (unsigned i = 0, n = expr->getNumElements(); i != n; ++i) {
        auto *elt = expr->getElement(i);
        auto ty = CS.getType(elt);
        auto flags = ParameterTypeFlags().withInOut(elt->isSemanticallyInOutExpr());
        elements.push_back(TupleTypeElt(ty->getInOutObjectType(),
                                        expr->getElementName(i), flags));
      }

      return TupleType::get(elements, CS.getASTContext());
    }

    Type visitSubscriptExpr(SubscriptExpr *expr) {
      ValueDecl *decl = nullptr;
      if (expr->hasDecl()) {
        decl = expr->getDecl().getDecl();
        if (decl->isInvalid())
          return Type();
      }
      return addSubscriptConstraints(expr, CS.getType(expr->getBase()),
                                     expr->getIndex(),
                                     decl);
    }
    
    Type visitArrayExpr(ArrayExpr *expr) {
      // An array expression can be of a type T that conforms to the
      // ExpressibleByArrayLiteral protocol.
      auto &tc = CS.getTypeChecker();
      ProtocolDecl *arrayProto
        = tc.getProtocol(expr->getLoc(),
                         KnownProtocolKind::ExpressibleByArrayLiteral);
      if (!arrayProto) {
        return Type();
      }

      // Assume that ExpressibleByArrayLiteral contains a single associated type.
      AssociatedTypeDecl *elementAssocTy = nullptr;
      for (auto decl : arrayProto->getMembers()) {
        if ((elementAssocTy = dyn_cast<AssociatedTypeDecl>(decl)))
          break;
      }
      if (!elementAssocTy)
        return Type();

      auto locator = CS.getConstraintLocator(expr);
      auto contextualType = CS.getContextualType(expr);
      Type contextualArrayType = nullptr;
      Type contextualArrayElementType = nullptr;
      
      // If a contextual type exists for this expression, apply it directly.
      Optional<Type> arrayElementType;
      if (contextualType &&
          (arrayElementType = ConstraintSystem::isArrayType(contextualType))) {
        // Is the array type a contextual type
        contextualArrayType = contextualType;
        contextualArrayElementType = *arrayElementType;

        CS.addConstraint(ConstraintKind::LiteralConformsTo, contextualType,
                         arrayProto->getDeclaredType(),
                         locator);
        
        unsigned index = 0;
        for (auto element : expr->getElements()) {
          CS.addConstraint(ConstraintKind::Conversion,
                           CS.getType(element),
                           contextualArrayElementType,
                           CS.getConstraintLocator(expr,
                                                   LocatorPathElt::
                                                    getTupleElement(index++)));
        }
        
        return contextualArrayType;
      }
      
      auto arrayTy = CS.createTypeVariable(locator,
                                           TVO_PrefersSubtypeBinding |
                                           TVO_CanBindToInOut);

      // The array must be an array literal type.
      CS.addConstraint(ConstraintKind::LiteralConformsTo, arrayTy,
                       arrayProto->getDeclaredType(),
                       locator);
      
      // Its subexpression should be convertible to a tuple (T.Element...).
      Type arrayElementTy = DependentMemberType::get(arrayTy, elementAssocTy);

      // Introduce conversions from each element to the element type of the
      // array.
      ConstraintLocatorBuilder builder(locator);
      unsigned index = 0;
      for (auto element : expr->getElements()) {
        CS.addConstraint(ConstraintKind::Conversion,
                         CS.getType(element),
                         arrayElementTy,
                         CS.getConstraintLocator(
                           expr,
                           LocatorPathElt::getTupleElement(index++)));
      }

      // The array element type defaults to 'Any'.
      if (arrayElementTy->isTypeVariableOrMember()) {
        CS.addConstraint(ConstraintKind::Defaultable, arrayElementTy,
                         tc.Context.TheAnyType, locator);
      }

      return arrayTy;
    }

    static bool isMergeableValueKind(Expr *expr) {
      return isa<StringLiteralExpr>(expr) || isa<IntegerLiteralExpr>(expr) ||
             isa<FloatLiteralExpr>(expr);
    }

    Type visitDictionaryExpr(DictionaryExpr *expr) {
      ASTContext &C = CS.getASTContext();
      // A dictionary expression can be of a type T that conforms to the
      // ExpressibleByDictionaryLiteral protocol.
      // FIXME: This isn't actually used for anything at the moment.
      auto &tc = CS.getTypeChecker();
      ProtocolDecl *dictionaryProto
        = tc.getProtocol(expr->getLoc(),
                         KnownProtocolKind::ExpressibleByDictionaryLiteral);
      if (!dictionaryProto) {
        return Type();
      }

      // FIXME: Protect against broken standard library.
      auto keyAssocTy = cast<AssociatedTypeDecl>(
                          dictionaryProto->lookupDirect(
                            C.getIdentifier("Key")).front());
      auto valueAssocTy = cast<AssociatedTypeDecl>(
                            dictionaryProto->lookupDirect(
                              C.getIdentifier("Value")).front());

      auto locator = CS.getConstraintLocator(expr);
      auto contextualType = CS.getContextualType(expr);
      Type contextualDictionaryType = nullptr;
      Type contextualDictionaryKeyType = nullptr;
      Type contextualDictionaryValueType = nullptr;
      
      // If a contextual type exists for this expression, apply it directly.
      Optional<std::pair<Type, Type>> dictionaryKeyValue;
      if (contextualType &&
          (dictionaryKeyValue = ConstraintSystem::isDictionaryType(contextualType))) {
        // Is the contextual type a dictionary type?
        contextualDictionaryType = contextualType;
        std::tie(contextualDictionaryKeyType,
                 contextualDictionaryValueType) = *dictionaryKeyValue;
        
        // Form an explicit tuple type from the contextual type's key and value types.
        TupleTypeElt tupleElts[2] = { TupleTypeElt(contextualDictionaryKeyType),
                                      TupleTypeElt(contextualDictionaryValueType) };
        Type contextualDictionaryElementType = TupleType::get(tupleElts, C);
        
        CS.addConstraint(ConstraintKind::LiteralConformsTo, contextualType,
                         dictionaryProto->getDeclaredType(),
                         locator);
        
        unsigned index = 0;
        for (auto element : expr->getElements()) {
          CS.addConstraint(ConstraintKind::Conversion,
                           CS.getType(element),
                           contextualDictionaryElementType,
                           CS.getConstraintLocator(expr,
                                                   LocatorPathElt::
                                                    getTupleElement(index++)));
        }
        
        return contextualDictionaryType;
      }
      
      auto dictionaryTy = CS.createTypeVariable(locator,
                                                TVO_PrefersSubtypeBinding |
                                                TVO_CanBindToInOut);

      // The dictionary must be a dictionary literal type.
      CS.addConstraint(ConstraintKind::LiteralConformsTo, dictionaryTy,
                       dictionaryProto->getDeclaredType(),
                       locator);


      // Its subexpression should be convertible to a tuple ((T.Key,T.Value)...).
      ConstraintLocatorBuilder locatorBuilder(locator);
      auto dictionaryKeyTy = DependentMemberType::get(dictionaryTy,
                                                      keyAssocTy);
      auto dictionaryValueTy = DependentMemberType::get(dictionaryTy,
                                                        valueAssocTy);
      TupleTypeElt tupleElts[2] = { TupleTypeElt(dictionaryKeyTy),
                                    TupleTypeElt(dictionaryValueTy) };
      Type elementTy = TupleType::get(tupleElts, C);

      // Keep track of which elements have been "merged". This way, we won't create
      // needless conversion constraints for elements whose equivalence classes have
      // been merged.
      llvm::DenseSet<Expr *> mergedElements;

      // If no contextual type is present, Merge equivalence classes of key 
      // and value types as necessary.
      if (!CS.getContextualType(expr)) {
        for (auto element1 : expr->getElements()) {
          for (auto element2 : expr->getElements()) {
            if (element1 == element2)
              continue;

            auto tty1 = CS.getType(element1)->getAs<TupleType>();
            auto tty2 = CS.getType(element2)->getAs<TupleType>();

            if (tty1 && tty2) {
              auto mergedKey = false;
              auto mergedValue = false;

              auto keyTyvar1 = tty1->getElementTypes()[0]->
                                getAs<TypeVariableType>();
              auto keyTyvar2 = tty2->getElementTypes()[0]->
                                getAs<TypeVariableType>();

              auto keyExpr1 = cast<TupleExpr>(element1)->getElements()[0];
              auto keyExpr2 = cast<TupleExpr>(element2)->getElements()[0];

              if (keyExpr1->getKind() == keyExpr2->getKind() &&
                  isMergeableValueKind(keyExpr1)) {
                mergedKey = mergeRepresentativeEquivalenceClasses(CS,
                            keyTyvar1, keyTyvar2);
              }

              auto valueTyvar1 = tty1->getElementTypes()[1]->
                                  getAs<TypeVariableType>();
              auto valueTyvar2 = tty2->getElementTypes()[1]->
                                  getAs<TypeVariableType>();

              auto elemExpr1 = cast<TupleExpr>(element1)->getElements()[1];
              auto elemExpr2 = cast<TupleExpr>(element2)->getElements()[1];

              if (elemExpr1->getKind() == elemExpr2->getKind() &&
                isMergeableValueKind(elemExpr1)) {
                mergedValue = mergeRepresentativeEquivalenceClasses(CS, 
                                valueTyvar1, valueTyvar2);
              }

              if (mergedKey && mergedValue)
                mergedElements.insert(element2);
            }
          }
        }
      }      

      // Introduce conversions from each element to the element type of the
      // dictionary. (If the equivalence class of an element has already been
      // merged with a previous one, skip it.)
      unsigned index = 0;
      for (auto element : expr->getElements()) {
        if (!mergedElements.count(element))
          CS.addConstraint(ConstraintKind::Conversion,
                           CS.getType(element),
                           elementTy,
                           CS.getConstraintLocator(
                             expr,
                             LocatorPathElt::getTupleElement(index++)));
      }

      // The dictionary key type defaults to 'AnyHashable'.
      if (dictionaryKeyTy->isTypeVariableOrMember() &&
          tc.Context.getAnyHashableDecl()) {
        auto anyHashable = tc.Context.getAnyHashableDecl();
        tc.validateDecl(anyHashable);
        CS.addConstraint(ConstraintKind::Defaultable, dictionaryKeyTy,
                         anyHashable->getDeclaredInterfaceType(), locator);
      }

      // The dictionary value type defaults to 'Any'.
      if (dictionaryValueTy->isTypeVariableOrMember()) {
        CS.addConstraint(ConstraintKind::Defaultable, dictionaryValueTy,
                         tc.Context.TheAnyType, locator);
      }

      return dictionaryTy;
    }

    Type visitDynamicSubscriptExpr(DynamicSubscriptExpr *expr) {
      return addSubscriptConstraints(expr, CS.getType(expr->getBase()),
                                     expr->getIndex(),
                                     nullptr);
    }

    Type visitTupleElementExpr(TupleElementExpr *expr) {
      ASTContext &context = CS.getASTContext();
      Identifier name
        = context.getIdentifier(llvm::utostr(expr->getFieldNumber()));
      return addMemberRefConstraints(expr, expr->getBase(), name,
                                     FunctionRefKind::Unapplied);
    }

    /// Give each parameter in a ClosureExpr a fresh type variable if parameter
    /// types were not specified, and return the eventual function type.
    Type getTypeForParameterList(ClosureExpr *closureExpr) {
      auto *params = closureExpr->getParameters();
      for (auto i : indices(params->getArray())) {
        auto *param = params->get(i);
        auto *locator = CS.getConstraintLocator(
            closureExpr, LocatorPathElt::getTupleElement(i));

        // If a type was explicitly specified, use its opened type.
        if (auto type = param->getTypeLoc().getType()) {
          // FIXME: Need a better locator for a pattern as a base.
          Type openedType = CS.openUnboundGenericType(type, locator);
          assert(!param->isLet() || !openedType->is<InOutType>());
          param->setType(openedType->getInOutObjectType());
          param->setInterfaceType(openedType->getInOutObjectType());
          continue;
        }

        // Otherwise, create a fresh type variable.
        Type ty = CS.createTypeVariable(locator, TVO_CanBindToInOut);

        param->setType(ty);
        param->setInterfaceType(ty);
      }
      
      return params->getType(CS.getASTContext());
    }

    
    /// \brief Produces a type for the given pattern, filling in any missing
    /// type information with fresh type variables.
    ///
    /// \param pattern The pattern.
    Type getTypeForPattern(Pattern *pattern, ConstraintLocatorBuilder locator) {
      switch (pattern->getKind()) {
      case PatternKind::Paren:
        // Parentheses don't affect the type.
        return getTypeForPattern(cast<ParenPattern>(pattern)->getSubPattern(),
                                 locator);
      case PatternKind::Var:
        // Var doesn't affect the type.
        return getTypeForPattern(cast<VarPattern>(pattern)->getSubPattern(),
                                 locator);
      case PatternKind::Any:
        // For a pattern of unknown type, create a new type variable.
        return CS.createTypeVariable(CS.getConstraintLocator(locator),
                                     TVO_CanBindToInOut);

      case PatternKind::Named: {
        auto var = cast<NamedPattern>(pattern)->getDecl();
        
        auto boundExpr = locator.trySimplifyToExpr();
        auto haveBoundCollectionLiteral = boundExpr &&
                                            !var->hasNonPatternBindingInit() &&
                                            (isa<ArrayExpr>(boundExpr) ||
                                             isa<DictionaryExpr>(boundExpr));

        // For a named pattern without a type, create a new type variable
        // and use it as the type of the variable.
        //
        // FIXME: For now, substitute in the bound type for literal collection
        // exprs that would otherwise result in a simple conversion constraint
        // being placed between two type variables. (The bound type and the
        // collection type, which will always be the same in this case.)
        // This will avoid exponential typecheck behavior in the case of nested
        // array and dictionary literals.
        Type ty = haveBoundCollectionLiteral ?
                    CS.getType(boundExpr) :
                    CS.createTypeVariable(CS.getConstraintLocator(locator),
                                          TVO_CanBindToInOut);

        // For weak variables, use Optional<T>.
        if (auto *OA = var->getAttrs().getAttribute<OwnershipAttr>())
          if (OA->get() == Ownership::Weak) {
            ty = CS.getTypeChecker().getOptionalType(var->getLoc(), ty);
            if (!ty) return Type();
          }

        return ty;
      }

      case PatternKind::Typed: {
        auto typedPattern = cast<TypedPattern>(pattern);
        // FIXME: Need a better locator for a pattern as a base.
        Type openedType = CS.openUnboundGenericType(typedPattern->getType(),
                                                    locator);

        // For a typed pattern, simply return the opened type of the pattern.
        // FIXME: Error recovery if the type is an error type?
        return openedType;
      }

      case PatternKind::Tuple: {
        auto tuplePat = cast<TuplePattern>(pattern);
        SmallVector<TupleTypeElt, 4> tupleTypeElts;
        tupleTypeElts.reserve(tuplePat->getNumElements());
        for (unsigned i = 0, e = tuplePat->getNumElements(); i != e; ++i) {
          auto &tupleElt = tuplePat->getElement(i);
          Type eltTy = getTypeForPattern(tupleElt.getPattern(),
                                         locator.withPathElement(
                                           LocatorPathElt::getTupleElement(i)));
          tupleTypeElts.push_back(TupleTypeElt(eltTy, tupleElt.getLabel()));
        }
        return TupleType::get(tupleTypeElts, CS.getASTContext());
      }
      
      // Refutable patterns occur when checking the PatternBindingDecls in an
      // if/let or while/let condition.  They always require an initial value,
      // so they always allow unspecified types.
#define PATTERN(Id, Parent)
#define REFUTABLE_PATTERN(Id, Parent) case PatternKind::Id:
#include "swift/AST/PatternNodes.def"
        // TODO: we could try harder here, e.g. for enum elements to provide the
        // enum type.
        return CS.createTypeVariable(CS.getConstraintLocator(locator),
                                     TVO_CanBindToInOut);
      }

      llvm_unreachable("Unhandled pattern kind");
    }

    Type visitCaptureListExpr(CaptureListExpr *expr) {
      // The type of the capture list is just the type of its closure.
      return CS.getType(expr->getClosureBody());
    }

    /// \brief Walk a closure body to determine if it's possible for
    /// it to return with a non-void result.
    static bool closureHasNoResult(ClosureExpr *expr) {
      // A walker that looks for 'return' statements that aren't
      // nested within closures or nested declarations.
      class FindReturns : public ASTWalker {
        bool FoundResultReturn = false;
        bool FoundNoResultReturn = false;

        std::pair<bool, Expr *> walkToExprPre(Expr *expr) override {
          return { false, expr };
        }
        bool walkToDeclPre(Decl *decl) override {
          return false;
        }
        std::pair<bool, Stmt *> walkToStmtPre(Stmt *stmt) override {
          // Record return statements.
          if (auto ret = dyn_cast<ReturnStmt>(stmt)) {
            // If it has a result, remember that we saw one, but keep
            // traversing in case there's a no-result return somewhere.
            if (ret->hasResult()) {
              FoundResultReturn = true;

            // Otherwise, stop traversing.
            } else {
              FoundNoResultReturn = true;
              return { false, nullptr };
            }
          }
          return { true, stmt };
        }
      public:
        bool hasNoResult() const {
          return FoundNoResultReturn || !FoundResultReturn;
        }
      };

      // Don't apply this to single-expression-body closures.
      if (expr->hasSingleExpressionBody())
        return false;

      auto body = expr->getBody();
      if (!body) return false;

      FindReturns finder;
      body->walk(finder);
      return finder.hasNoResult();
    }
    
    /// \brief Walk a closure AST to determine if it can throw.
    bool closureCanThrow(ClosureExpr *expr) {
      // A walker that looks for 'try' or 'throw' expressions
      // that aren't nested within closures, nested declarations,
      // or exhaustive catches.
      class FindInnerThrows : public ASTWalker {
        ConstraintSystem &CS;
        bool FoundThrow = false;
        
        std::pair<bool, Expr *> walkToExprPre(Expr *expr) override {
          // If we've found a 'try', record it and terminate the traversal.
          if (isa<TryExpr>(expr)) {
            FoundThrow = true;
            return { false, nullptr };
          }

          // Don't walk into a 'try!' or 'try?'.
          if (isa<ForceTryExpr>(expr) || isa<OptionalTryExpr>(expr)) {
            return { false, expr };
          }
          
          // Do not recurse into other closures.
          if (isa<ClosureExpr>(expr))
            return { false, expr };
          
          return { true, expr };
        }
        
        bool walkToDeclPre(Decl *decl) override {
          // Do not walk into function or type declarations.
          if (!isa<PatternBindingDecl>(decl))
            return false;
          
          return true;
        }

        bool isSyntacticallyExhaustive(DoCatchStmt *stmt) {
          for (auto catchClause : stmt->getCatches()) {
            if (isSyntacticallyExhaustive(catchClause))
              return true;
          }

          return false;
        }

        bool isSyntacticallyExhaustive(CatchStmt *clause) {
          // If it's obviously non-exhaustive, great.
          if (clause->getGuardExpr())
            return false;

          // If we can show that it's exhaustive without full
          // type-checking, great.
          if (clause->isSyntacticallyExhaustive())
            return true;

          // Okay, resolve the pattern.
          Pattern *pattern = clause->getErrorPattern();
          pattern = CS.TC.resolvePattern(pattern, CS.DC,
                                         /*isStmtCondition*/false);
          if (!pattern) return false;

          // Save that aside while we explore the type.
          clause->setErrorPattern(pattern);

          // Require the pattern to have a particular shape: a number
          // of is-patterns applied to an irrefutable pattern.
          pattern = pattern->getSemanticsProvidingPattern();
          while (auto isp = dyn_cast<IsPattern>(pattern)) {
            if (CS.TC.validateType(isp->getCastTypeLoc(), CS.DC,
                                   TypeResolutionFlags::InExpression))
              return false;

            if (!isp->hasSubPattern()) {
              pattern = nullptr;
              break;
            } else {
              pattern = isp->getSubPattern()->getSemanticsProvidingPattern();
            }
          }
          if (pattern && pattern->isRefutablePattern()) {
            return false;
          }

          // Okay, now it should be safe to coerce the pattern.
          // Pull the top-level pattern back out.
          pattern = clause->getErrorPattern();
          Type exnType = CS.TC.getExceptionType(CS.DC, clause->getCatchLoc());
          if (!exnType ||
              CS.TC.coercePatternToType(pattern, CS.DC, exnType,
                                        TypeResolutionFlags::InExpression)) {
            return false;
          }

          clause->setErrorPattern(pattern);
          return clause->isSyntacticallyExhaustive();
        }
        
        std::pair<bool, Stmt *> walkToStmtPre(Stmt *stmt) override {
          // If we've found a 'throw', record it and terminate the traversal.
          if (isa<ThrowStmt>(stmt)) {
            FoundThrow = true;
            return { false, nullptr };
          }

          // Handle do/catch differently.
          if (auto doCatch = dyn_cast<DoCatchStmt>(stmt)) {
            // Only walk into the 'do' clause of a do/catch statement
            // if the catch isn't syntactically exhaustive.
            if (!isSyntacticallyExhaustive(doCatch)) {
              if (!doCatch->getBody()->walk(*this))
                return { false, nullptr };
            }

            // Walk into all the catch clauses.
            for (auto catchClause : doCatch->getCatches()) {
              if (!catchClause->walk(*this))
                return { false, nullptr };
            }

            // We've already walked all the children we care about.
            return { false, stmt };
          }
          
          return { true, stmt };
        }
        
      public:
        FindInnerThrows(ConstraintSystem &cs) : CS(cs) {}

        bool foundThrow() { return FoundThrow; }
      };
      
      if (expr->getThrowsLoc().isValid())
        return true;
      
      auto body = expr->getBody();
      
      if (!body)
        return false;
      
      auto tryFinder = FindInnerThrows(CS);
      body->walk(tryFinder);
      return tryFinder.foundThrow();
    }
    

    Type visitClosureExpr(ClosureExpr *expr) {
      
      // If a contextual function type exists, we can use that to obtain the
      // expected return type, rather than allocating a fresh type variable.
      auto contextualType = CS.getContextualType(expr);
      Type crt;
      
      if (contextualType) {
        if (auto cft = contextualType->getAs<AnyFunctionType>()) {
          crt = cft->getResult();
        }
      }
      
      // Closure expressions always have function type. In cases where a
      // parameter or return type is omitted, a fresh type variable is used to
      // stand in for that parameter or return type, allowing it to be inferred
      // from context.
      Type funcTy;
      if (expr->hasExplicitResultType() &&
          expr->getExplicitResultTypeLoc().getType()) {
        funcTy = expr->getExplicitResultTypeLoc().getType();
        CS.setFavoredType(expr, funcTy.getPointer());
      } else if (!crt.isNull()) {
        funcTy = crt;
      } else{
        auto locator =
          CS.getConstraintLocator(expr, ConstraintLocator::ClosureResult);

        // If no return type was specified, create a fresh type
        // variable for it.
        funcTy = CS.createTypeVariable(locator, TVO_CanBindToInOut);

        // Allow it to default to () if there are no return statements.
        if (closureHasNoResult(expr)) {
          CS.addConstraint(ConstraintKind::Defaultable,
                           funcTy,
                           TupleType::getEmpty(CS.getASTContext()),
                           locator);
        }
      }

      // Give each parameter in a ClosureExpr a fresh type variable if parameter
      // types were not specified, and return the eventual function type.
      auto paramTy = getTypeForParameterList(expr);
      auto extInfo = FunctionType::ExtInfo();
      
      if (closureCanThrow(expr))
        extInfo = extInfo.withThrows();
      
      // FIXME: If we want keyword arguments for closures, add them here.
      funcTy = FunctionType::get(paramTy, funcTy, extInfo);

      return funcTy;
    }

    Type visitAutoClosureExpr(AutoClosureExpr *expr) {
      // AutoClosureExpr is introduced by CSApply.
      llvm_unreachable("Already type-checked");
    }

    Type visitInOutExpr(InOutExpr *expr) {
      // The address-of operator produces an explicit inout T from an lvalue T.
      // We model this with the constraint
      //
      //     S < lvalue T
      //
      // where T is a fresh type variable.
      auto lvalue = CS.createTypeVariable(CS.getConstraintLocator(expr),
                                          TVO_CanBindToInOut);
      auto bound = LValueType::get(lvalue);
      auto result = InOutType::get(lvalue);
      CS.addConstraint(ConstraintKind::Conversion,
                       CS.getType(expr->getSubExpr()), bound,
                       CS.getConstraintLocator(expr->getSubExpr()));
      return result;
    }

    Type visitDynamicTypeExpr(DynamicTypeExpr *expr) {
      auto tv = CS.createTypeVariable(CS.getConstraintLocator(expr),
                                      TVO_CanBindToInOut);
      CS.addConstraint(ConstraintKind::DynamicTypeOf, tv,
                       CS.getType(expr->getBase()),
           CS.getConstraintLocator(expr, ConstraintLocator::RvalueAdjustment));
      return tv;
    }

    Type visitOpaqueValueExpr(OpaqueValueExpr *expr) {
      return expr->getType();
    }

    Type visitApplyExpr(ApplyExpr *expr) {
      Type outputTy;

      auto fnExpr = expr->getFn();

      if (auto *UDE = dyn_cast<UnresolvedDotExpr>(fnExpr)) {
        auto typeOperation = getTypeOperation(UDE, CS.getASTContext());
        if (typeOperation != TypeOperation::None)
          return resultOfTypeOperation(typeOperation, expr->getArg());
      }

      if (isa<DeclRefExpr>(fnExpr)) {
        if (auto fnType = CS.getType(fnExpr)->getAs<AnyFunctionType>()) {
          outputTy = fnType->getResult();
        }
      } else if (auto OSR = dyn_cast<OverloadedDeclRefExpr>(fnExpr)) {
        // Determine if the overloads are all functions that share a common
        // return type.
        Type commonType;
        for (auto OD : OSR->getDecls()) {
          auto OFD = dyn_cast<AbstractFunctionDecl>(OD);
          if (!OFD) {
            commonType = Type();
            break;
          }

          auto OFT = OFD->getInterfaceType()->getAs<AnyFunctionType>();
          if (!OFT) {
            commonType = Type();
            break;
          }

          // Look past the self parameter.
          if (OFD->getDeclContext()->isTypeContext()) {
            OFT = OFT->getResult()->getAs<AnyFunctionType>();
            if (!OFT) {
              commonType = Type();
              break;
            }
          }

          Type resultType = OFT->getResult();

          // If there are any type parameters in the result,
          if (resultType->hasTypeParameter()) {
            commonType = Type();
            break;
          }

          if (commonType.isNull()) {
            commonType = resultType;
          } else if (!commonType->isEqual(resultType)) {
            commonType = Type();
            break;
          }
        }

        if (commonType) {
          outputTy = commonType;
        }
      }
      
      // The function subexpression has some rvalue type T1 -> T2 for fresh
      // variables T1 and T2.
      if (outputTy.isNull()) {
        outputTy = CS.createTypeVariable(
                     CS.getConstraintLocator(expr,
                                             ConstraintLocator::ApplyFunction),
                                             TVO_CanBindToInOut);
      } else {
        // Since we know what the output type is, we can set it as the favored
        // type of this expression.
        CS.setFavoredType(expr, outputTy.getPointer());
      }

      // A direct call to a ClosureExpr makes it noescape.
      FunctionType::ExtInfo extInfo;
      if (isa<ClosureExpr>(fnExpr->getSemanticsProvidingExpr()))
        extInfo = extInfo.withNoEscape();
      
      auto funcTy = FunctionType::get(CS.getType(expr->getArg()), outputTy,
                                      extInfo);

      CS.addConstraint(ConstraintKind::ApplicableFunction, funcTy,
                       CS.getType(expr->getFn()),
        CS.getConstraintLocator(expr, ConstraintLocator::ApplyFunction));

      return outputTy;
    }

    Type getSuperType(VarDecl *selfDecl,
                      SourceLoc diagLoc,
                      Diag<> diag_not_in_class,
                      Diag<> diag_no_base_class) {
      DeclContext *typeContext = selfDecl->getDeclContext()->getParent();
      assert(typeContext && "constructor without parent context?!");

      auto &tc = CS.getTypeChecker();
      ClassDecl *classDecl = typeContext->getAsClassOrClassExtensionContext();
      if (!classDecl) {
        tc.diagnose(diagLoc, diag_not_in_class);
        return Type();
      }
      if (!classDecl->hasSuperclass()) {
        tc.diagnose(diagLoc, diag_no_base_class);
        return Type();
      }

      // If the 'self' parameter is not configured, something went
      // wrong elsewhere and should have been diagnosed already.
      if (!selfDecl->hasInterfaceType())
        return ErrorType::get(tc.Context);

      auto selfTy = CS.DC->mapTypeIntoContext(
        typeContext->getDeclaredInterfaceType());
      auto superclassTy = selfTy->getSuperclass();

      if (selfDecl->getInterfaceType()->is<MetatypeType>())
        superclassTy = MetatypeType::get(superclassTy);

      return superclassTy;
    }
    
    Type visitRebindSelfInConstructorExpr(RebindSelfInConstructorExpr *expr) {
      // The result is void.
      return TupleType::getEmpty(CS.getASTContext());
    }
    
    Type visitIfExpr(IfExpr *expr) {
      // The conditional expression must conform to LogicValue.
      auto boolDecl = CS.getASTContext().getBoolDecl();
      if (!boolDecl)
        return Type();

      // Condition must convert to Bool.
      CS.addConstraint(ConstraintKind::Conversion,
                       CS.getType(expr->getCondExpr()),
                       boolDecl->getDeclaredType(),
                       CS.getConstraintLocator(expr->getCondExpr()));

      // The branches must be convertible to a common type.
      auto resultTy = CS.createTypeVariable(CS.getConstraintLocator(expr),
                                            TVO_PrefersSubtypeBinding |
                                            TVO_CanBindToInOut);
      CS.addConstraint(ConstraintKind::Conversion,
                       CS.getType(expr->getThenExpr()), resultTy,
                       CS.getConstraintLocator(expr->getThenExpr()));
      CS.addConstraint(ConstraintKind::Conversion,
                       CS.getType(expr->getElseExpr()), resultTy,
                       CS.getConstraintLocator(expr->getElseExpr()));
      return resultTy;
    }
    
    virtual Type visitImplicitConversionExpr(ImplicitConversionExpr *expr) {
      llvm_unreachable("Already type-checked");
    }

    Type visitForcedCheckedCastExpr(ForcedCheckedCastExpr *expr) {
      auto &tc = CS.getTypeChecker();
      auto fromExpr = expr->getSubExpr();
      if (!fromExpr) // Either wasn't constructed correctly or wasn't folded.
        return nullptr;

      // Validate the resulting type.
      TypeResolutionOptions options = TypeResolutionFlags::AllowUnboundGenerics;
      options |= TypeResolutionFlags::InExpression;
      if (tc.validateType(expr->getCastTypeLoc(), CS.DC, options))
        return nullptr;

      // Open the type we're casting to.
      auto toType = CS.openUnboundGenericType(expr->getCastTypeLoc().getType(),
                                              CS.getConstraintLocator(expr));
      expr->getCastTypeLoc().setType(toType, /*validated=*/true);

      auto fromType = CS.getType(fromExpr);
      auto locator = CS.getConstraintLocator(fromExpr);

      // The source type can be checked-cast to the destination type.
      CS.addConstraint(ConstraintKind::CheckedCast, fromType, toType, locator);

      return toType;
    }

    Type visitCoerceExpr(CoerceExpr *expr) {
      auto &tc = CS.getTypeChecker();
      
      // Validate the resulting type.
      TypeResolutionOptions options = TypeResolutionFlags::AllowUnboundGenerics;
      options |= TypeResolutionFlags::InExpression;
      if (tc.validateType(expr->getCastTypeLoc(), CS.DC, options))
        return nullptr;

      // Open the type we're casting to.
      auto toType = CS.openUnboundGenericType(expr->getCastTypeLoc().getType(),
                                              CS.getConstraintLocator(expr));
      expr->getCastTypeLoc().setType(toType, /*validated=*/true);

      auto fromType = CS.getType(expr->getSubExpr());
      auto locator = CS.getConstraintLocator(expr);

      CS.addExplicitConversionConstraint(fromType, toType, /*allowFixes=*/true,
                                         locator);
      return toType;
    }

    Type visitConditionalCheckedCastExpr(ConditionalCheckedCastExpr *expr) {
      auto &tc = CS.getTypeChecker();
      auto fromExpr = expr->getSubExpr();
      if (!fromExpr) // Either wasn't constructed correctly or wasn't folded.
        return nullptr;

      // Validate the resulting type.
      TypeResolutionOptions options = TypeResolutionFlags::AllowUnboundGenerics;
      options |= TypeResolutionFlags::InExpression;
      if (tc.validateType(expr->getCastTypeLoc(), CS.DC, options))
        return nullptr;

      // Open the type we're casting to.
      auto toType = CS.openUnboundGenericType(expr->getCastTypeLoc().getType(),
                                              CS.getConstraintLocator(expr));
      expr->getCastTypeLoc().setType(toType, /*validated=*/true);

      auto fromType = CS.getType(fromExpr);
      auto locator = CS.getConstraintLocator(fromExpr);
      CS.addConstraint(ConstraintKind::CheckedCast, fromType, toType, locator);
      return OptionalType::get(toType);
    }

    Type visitIsExpr(IsExpr *expr) {
      // Validate the type.
      auto &tc = CS.getTypeChecker();
      TypeResolutionOptions options = TypeResolutionFlags::AllowUnboundGenerics;
      options |= TypeResolutionFlags::InExpression;
      if (tc.validateType(expr->getCastTypeLoc(), CS.DC, options))
        return nullptr;

      // Open up the type we're checking.
      // FIXME: Locator for the cast type?
      auto toType = CS.openUnboundGenericType(expr->getCastTypeLoc().getType(),
                                              CS.getConstraintLocator(expr));
      expr->getCastTypeLoc().setType(toType, /*validated=*/true);

      // Add a checked cast constraint.
      auto fromType = CS.getType(expr->getSubExpr());
      
      CS.addConstraint(ConstraintKind::CheckedCast, fromType, toType,
                       CS.getConstraintLocator(expr));

      // The result is Bool.
      return CS.getTypeChecker().lookupBoolType(CS.DC);
    }

    Type visitDiscardAssignmentExpr(DiscardAssignmentExpr *expr) {
      auto locator = CS.getConstraintLocator(expr);
      auto typeVar = CS.createTypeVariable(locator, /*options=*/0);
      return LValueType::get(typeVar);
    }
    
    Type visitAssignExpr(AssignExpr *expr) {
      // Handle invalid code.
      if (!expr->getDest() || !expr->getSrc())
        return Type();
      
      // Compute the type to which the source must be converted to allow
      // assignment to the destination.
      auto destTy = CS.computeAssignDestType(expr->getDest(), expr->getLoc());
      if (!destTy)
        return Type();
      if (destTy->is<UnresolvedType>()) {
        return CS.createTypeVariable(CS.getConstraintLocator(expr),
                                     /*options=*/0);
      }
      
      // The source must be convertible to the destination.
      CS.addConstraint(ConstraintKind::Conversion,
                       CS.getType(expr->getSrc()), destTy,
                       CS.getConstraintLocator(expr->getSrc()));

      return TupleType::getEmpty(CS.getASTContext());
    }
    
    Type visitUnresolvedPatternExpr(UnresolvedPatternExpr *expr) {
      // If there are UnresolvedPatterns floating around after name binding,
      // they are pattern productions in invalid positions. However, we will
      // diagnose that condition elsewhere; to avoid unnecessary noise errors,
      // just plop an open type variable here.
      
      auto locator = CS.getConstraintLocator(expr);
      auto typeVar = CS.createTypeVariable(locator,
                                           TVO_CanBindToLValue |
                                           TVO_CanBindToInOut);
      return typeVar;
    }

    /// Get the type T?
    ///
    ///  This is not the ideal source location, but it's only used for
    /// diagnosing ill-formed standard libraries, so it really isn't
    /// worth QoI efforts.
    Type getOptionalType(SourceLoc optLoc, Type valueTy) {
      auto optTy = CS.getTypeChecker().getOptionalType(optLoc, valueTy);
      if (!optTy || CS.getTypeChecker().requireOptionalIntrinsics(optLoc))
        return Type();

      return optTy;
    }

    Type visitBindOptionalExpr(BindOptionalExpr *expr) {
      // The operand must be coercible to T?, and we will have type T.
      auto locator = CS.getConstraintLocator(expr);

      auto objectTy = CS.createTypeVariable(locator,
                                            TVO_PrefersSubtypeBinding |
                                            TVO_CanBindToLValue |
                                            TVO_CanBindToInOut);
      
      // The result is the object type of the optional subexpression.
      CS.addConstraint(ConstraintKind::OptionalObject,
                       CS.getType(expr->getSubExpr()), objectTy,
                       locator);
      return objectTy;
    }
    
    Type visitOptionalEvaluationExpr(OptionalEvaluationExpr *expr) {
      // The operand must be coercible to T? for some type T.  We'd
      // like this to be the smallest possible nesting level of
      // optional types, e.g. T? over T??; otherwise we don't really
      // have a preference.
      auto valueTy = CS.createTypeVariable(CS.getConstraintLocator(expr),
                                           TVO_PrefersSubtypeBinding |
                                           TVO_CanBindToInOut);

      Type optTy = getOptionalType(expr->getSubExpr()->getLoc(), valueTy);
      if (!optTy)
        return Type();

      CS.addConstraint(ConstraintKind::Conversion,
                       CS.getType(expr->getSubExpr()), optTy,
                       CS.getConstraintLocator(expr));
      return optTy;
    }

    Type visitForceValueExpr(ForceValueExpr *expr) {
      // Force-unwrap an optional of type T? to produce a T.
      auto locator = CS.getConstraintLocator(expr);

      auto objectTy = CS.createTypeVariable(locator,
                                            TVO_PrefersSubtypeBinding |
                                            TVO_CanBindToLValue |
                                            TVO_CanBindToInOut);
      
      // The result is the object type of the optional subexpression.
      CS.addConstraint(ConstraintKind::OptionalObject,
                       CS.getType(expr->getSubExpr()), objectTy,
                       locator);
      return objectTy;
    }

    Type visitOpenExistentialExpr(OpenExistentialExpr *expr) {
      llvm_unreachable("Already type-checked");
    }
    Type visitMakeTemporarilyEscapableExpr(MakeTemporarilyEscapableExpr *expr) {
      llvm_unreachable("Already type-checked");
    }
    Type visitKeyPathApplicationExpr(KeyPathApplicationExpr *expr) {
      // This should only appear in already-type-checked solutions, but we may
      // need to re-check for failure diagnosis.
      auto locator = CS.getConstraintLocator(expr);
      auto projectedTy = CS.createTypeVariable(locator,
                                               TVO_CanBindToLValue);
      CS.addKeyPathApplicationConstraint(expr->getKeyPath()->getType(),
                                         expr->getBase()->getType(),
                                         projectedTy,
                                         locator);
      return projectedTy;
    }
    
    Type visitEnumIsCaseExpr(EnumIsCaseExpr *expr) {
      return CS.getASTContext().getBoolDecl()->getDeclaredType();
    }

    Type visitEditorPlaceholderExpr(EditorPlaceholderExpr *E) {
      if (E->getTypeLoc().isNull()) {
        auto locator = CS.getConstraintLocator(E);

        // A placeholder may have any type, but default to Void type if
        // otherwise unconstrained.
        auto &placeholderTy
          = editorPlaceholderVariables[currentEditorPlaceholderVariable];
        if (!placeholderTy) {
          placeholderTy = CS.createTypeVariable(locator, TVO_CanBindToInOut);

          CS.addConstraint(ConstraintKind::Defaultable,
                           placeholderTy,
                           TupleType::getEmpty(CS.getASTContext()),
                           locator);
        }

        // Move to the next placeholder variable.
        currentEditorPlaceholderVariable
          = (currentEditorPlaceholderVariable + 1) %
              numEditorPlaceholderVariables;

        return placeholderTy;
      }

      // NOTE: The type loc may be there but have failed to validate, in which
      // case we return the null type.
      return E->getType();
    }

    Type visitObjCSelectorExpr(ObjCSelectorExpr *E) {
      // #selector only makes sense when we have the Objective-C
      // runtime.
      auto &tc = CS.getTypeChecker();
      if (!tc.Context.LangOpts.EnableObjCInterop) {
        tc.diagnose(E->getLoc(), diag::expr_selector_no_objc_runtime);
        return nullptr;
      }

      
      // Make sure we can reference ObjectiveC.Selector.
      // FIXME: Fix-It to add the import?
      auto type = CS.getTypeChecker().getObjCSelectorType(CS.DC);
      if (!type) {
        tc.diagnose(E->getLoc(), diag::expr_selector_module_missing);
        return nullptr;
      }

      return type;
    }

    Type visitKeyPathExpr(KeyPathExpr *E) {
      if (E->isObjC())
        return CS.getType(E->getObjCStringLiteralExpr());
      
      auto kpDecl = CS.getASTContext().getKeyPathDecl();
      
      if (!kpDecl) {
        CS.TC.diagnose(E->getLoc(), diag::expr_keypath_no_keypath_type);
        return ErrorType::get(CS.getASTContext());
      }
      
      // For native key paths, traverse the key path components to set up
      // appropriate type relationships at each level.
      auto locator = CS.getConstraintLocator(E);
      Type root = CS.createTypeVariable(locator, TVO_CanBindToInOut);
      
      // If a root type was explicitly given, then resolve it now.
      if (auto rootRepr = E->getRootType()) {
        auto rootObjectTy = resolveTypeReferenceInExpression(rootRepr);
        if (!rootObjectTy || rootObjectTy->hasError())
          return Type();
        rootObjectTy = CS.openUnboundGenericType(rootObjectTy, locator);
        CS.addConstraint(ConstraintKind::Bind, root, rootObjectTy,
                         locator);
      }
      
      bool didOptionalChain = false;
      // We start optimistically from an lvalue base.
      Type base = LValueType::get(root);
      
      for (unsigned i : indices(E->getComponents())) {
        auto &component = E->getComponents()[i];
        switch (auto kind = component.getKind()) {
        case KeyPathExpr::Component::Kind::Invalid:
          break;
        
        case KeyPathExpr::Component::Kind::UnresolvedProperty:
        // This should only appear in resolved ASTs, but we may need to
        // re-type-check the constraints during failure diagnosis.
        case KeyPathExpr::Component::Kind::Property: {
          auto memberTy = CS.createTypeVariable(locator,
                                                TVO_CanBindToLValue |
                                                TVO_CanBindToInOut);
          auto lookupName = kind == KeyPathExpr::Component::Kind::UnresolvedProperty
            ? component.getUnresolvedDeclName()
            : component.getDeclRef().getDecl()->getFullName();
          
          auto refKind = lookupName.isSimpleName()
            ? FunctionRefKind::Unapplied
            : FunctionRefKind::Compound;
          auto memberLocator = CS.getConstraintLocator(E,
                        ConstraintLocator::PathElement::getKeyPathComponent(i));
          CS.addValueMemberConstraint(base, lookupName,
                                      memberTy,
                                      CurDC,
                                      refKind,
                                      memberLocator);
          base = memberTy;
          break;
        }
          
        case KeyPathExpr::Component::Kind::UnresolvedSubscript:
        // Subscript should only appear in resolved ASTs, but we may need to
        // re-type-check the constraints during failure diagnosis.
        case KeyPathExpr::Component::Kind::Subscript: {

          auto memberLocator = CS.getConstraintLocator(E,
                        ConstraintLocator::PathElement::getKeyPathComponent(i));
          base = addSubscriptConstraints(E, base, component.getIndexExpr(),
                                         /*decl*/ nullptr, memberLocator,
                                         /*index locator*/ memberLocator);
          break;
        }
        
        case KeyPathExpr::Component::Kind::OptionalChain: {
          didOptionalChain = true;
          
          // We can't assign an optional back through an optional chain
          // today. Force the base to an rvalue.
          auto rvalueTy = CS.createTypeVariable(locator, 0);
          CS.addConstraint(ConstraintKind::Equal, base, rvalueTy, locator);
          base = rvalueTy;
          LLVM_FALLTHROUGH;
        }
        case KeyPathExpr::Component::Kind::OptionalForce: {
          auto optionalObjTy = CS.createTypeVariable(locator,
                                                     TVO_CanBindToLValue |
                                                     TVO_CanBindToInOut);
          
          CS.addConstraint(ConstraintKind::OptionalObject, base, optionalObjTy,
                           locator);
          
          base = optionalObjTy;
          break;
        }
        
        case KeyPathExpr::Component::Kind::OptionalWrap: {
          // This should only appear in resolved ASTs, but we may need to
          // re-type-check the constraints during failure diagnosis.
          base = OptionalType::get(base);
          break;
        }
        }
      }
      
      // If there was an optional chaining component, the end result must be
      // optional.
      if (didOptionalChain) {
        auto objTy = CS.createTypeVariable(locator, TVO_CanBindToInOut);
        auto optTy = OptionalType::get(objTy);
        CS.addConstraint(ConstraintKind::Conversion, base, optTy,
                         locator);
        base = optTy;
      }
      
      auto rvalueBase = CS.createTypeVariable(locator, TVO_CanBindToInOut);
      CS.addConstraint(ConstraintKind::Equal, base, rvalueBase, locator);
      
      // The result is a KeyPath from the root to the end component.
      Type kpTy;
      if (didOptionalChain) {
        // Optional-chaining key paths are always read-only.
        kpTy = BoundGenericType::get(kpDecl, Type(), {root, rvalueBase});
      } else {
        // The type of key path depends on the overloads chosen for the key
        // path components.
        kpTy = CS.createTypeVariable(CS.getConstraintLocator(E),
                                     TVO_CanBindToInOut);
        CS.addKeyPathConstraint(kpTy, root, rvalueBase,
                                CS.getConstraintLocator(E));
      }
      return kpTy;
    }

    Type visitKeyPathDotExpr(KeyPathDotExpr *E) {
      llvm_unreachable("found KeyPathDotExpr in CSGen");
    }

    enum class TypeOperation { None, Join, JoinInout, JoinMeta };

    static TypeOperation getTypeOperation(UnresolvedDotExpr *UDE,
                                          ASTContext &Context) {
      auto *DRE = dyn_cast<DeclRefExpr>(UDE->getBase());
      if (!DRE)
        return TypeOperation::None;

      if (DRE->getDecl() != Context.TheBuiltinModule)
        return TypeOperation::None;

      return llvm::StringSwitch<TypeOperation>(
                 UDE->getName().getBaseIdentifier().str())
          .Case("type_join", TypeOperation::Join)
          .Case("type_join_inout", TypeOperation::JoinInout)
          .Case("type_join_meta", TypeOperation::JoinMeta)
          .Default(TypeOperation::None);
    }

    Type resultOfTypeOperation(TypeOperation op, Expr *Arg) {
      auto *tuple = dyn_cast<TupleExpr>(Arg);
      assert(tuple && "Expected argument tuple for join operations!");

      auto *lhs = tuple->getElement(0);
      auto *rhs = tuple->getElement(1);

      switch (op) {
      case TypeOperation::None:
        llvm_unreachable(
            "We should have a valid type operation at this point!");

      case TypeOperation::Join: {
        auto lhsMeta = CS.getType(lhs)->getAs<MetatypeType>();
        auto rhsMeta = CS.getType(rhs)->getAs<MetatypeType>();
        if (!lhsMeta || !rhsMeta)
          llvm_unreachable("Unexpected argument types for Builtin.type_join!");

        auto &ctx = lhsMeta->getASTContext();

        auto join =
            Type::join(lhsMeta->getInstanceType(), rhsMeta->getInstanceType());

        // We will hit this assert today due to implemenation details
        // of Type::join, but the intent is to define join to always
        // result in a type.
        assert(join && "Unexpected null type returned from Type::join!");

        if (!join)
          return ErrorType::get(ctx);

        return MetatypeType::get(join, ctx)->getCanonicalType();
      }

      case TypeOperation::JoinInout: {
        auto lhsInOut = CS.getType(lhs)->getAs<InOutType>();
        auto rhsMeta = CS.getType(rhs)->getAs<MetatypeType>();
        if (!lhsInOut || !rhsMeta)
          llvm_unreachable("Unexpected argument types for Builtin.type_join!");

        auto &ctx = lhsInOut->getASTContext();

        auto join =
            Type::join(lhsInOut, rhsMeta->getInstanceType());

        // We will hit this assert today due to implemenation details
        // of Type::join, but the intent is to define join to always
        // result in a type.
        assert(join && "Unexpected null type returned from Type::join!");

        if (!join)
          return ErrorType::get(ctx);

        return MetatypeType::get(join, ctx)->getCanonicalType();
      }

      case TypeOperation::JoinMeta: {
        auto lhsMeta = CS.getType(lhs)->getAs<MetatypeType>();
        auto rhsMeta = CS.getType(rhs)->getAs<MetatypeType>();
        if (!lhsMeta || !rhsMeta)
          llvm_unreachable("Unexpected argument types for Builtin.type_join!");

        auto &ctx = lhsMeta->getASTContext();

        auto join = Type::join(lhsMeta, rhsMeta);

        // We will hit this assert today due to implemenation details
        // of Type::join, but the intent is to define join to always
        // result in a type.
        assert(join && "Unexpected null type returned from Type::join!");

        if (!join)
          return ErrorType::get(ctx);

        return join;
      }
      }
    }
  };

  /// \brief AST walker that "sanitizes" an expression for the
  /// constraint-based type checker.
  ///
  /// This is only necessary because Sema fills in too much type information
  /// before the type-checker runs, causing redundant work.
  class SanitizeExpr : public ASTWalker {
    TypeChecker &TC;
  public:
    SanitizeExpr(TypeChecker &tc) : TC(tc) { }

    std::pair<bool, Expr *> walkToExprPre(Expr *expr) override {
      // Let's check if condition of the IfExpr looks properly
      // type-checked, and roll it back to the original state,
      // because otherwise, since condition is implicitly Int1,
      // we'll have to handle multiple ways of type-checking
      // IfExprs in both ConstraintGenerator and ExprRewriter,
      // so it's less error prone to do it once here.
      if (auto IE = dyn_cast<IfExpr>(expr)) {
        auto condition = IE->getCondExpr();
        if (!condition)
          return {true, expr};

        if (auto call = dyn_cast<CallExpr>(condition)) {
          if (!call->isImplicit())
            return {true, expr};

          if (auto DSCE = dyn_cast<DotSyntaxCallExpr>(call->getFn())) {
            if (DSCE->isImplicit())
              IE->setCondExpr(DSCE->getBase());
          }
        }
      }

      return {true, expr};
    }

    Expr *walkToExprPost(Expr *expr) override {
      if (auto implicit = dyn_cast<ImplicitConversionExpr>(expr)) {
        // Skip implicit conversions completely.
        return implicit->getSubExpr();
      }

      if (auto dotCall = dyn_cast<DotSyntaxCallExpr>(expr)) {
        // A DotSyntaxCallExpr is a member reference that has already been
        // type-checked down to a call; turn it back into an overloaded
        // member reference expression.
        DeclNameLoc memberLoc;
        auto memberAndFunctionRef = findReferencedDecl(dotCall->getFn(),
                                                       memberLoc);
        if (memberAndFunctionRef.first) {
          auto base = skipImplicitConversions(dotCall->getArg());
          return new (TC.Context) MemberRefExpr(base,
                                    dotCall->getDotLoc(),
                                    memberAndFunctionRef.first,
                                    memberLoc,
                                    expr->isImplicit());
        }
      }

      if (auto dotIgnored = dyn_cast<DotSyntaxBaseIgnoredExpr>(expr)) {
        // A DotSyntaxBaseIgnoredExpr is a static member reference that has
        // already been type-checked down to a call where the argument doesn't
        // actually matter; turn it back into an overloaded member reference
        // expression.
        DeclNameLoc memberLoc;
        auto memberAndFunctionRef = findReferencedDecl(dotIgnored->getRHS(),
                                                       memberLoc);
        if (memberAndFunctionRef.first) {
          auto base = skipImplicitConversions(dotIgnored->getLHS());
          return new (TC.Context) MemberRefExpr(base,
                                    dotIgnored->getDotLoc(),
                                    memberAndFunctionRef.first,
                                    memberLoc, expr->isImplicit());
        }
      }

      return expr;
    }

    /// \brief Ignore declarations.
    bool walkToDeclPre(Decl *decl) override { return false; }
  };

  class ConstraintWalker : public ASTWalker {
    ConstraintGenerator &CG;

  public:
    ConstraintWalker(ConstraintGenerator &CG) : CG(CG) { }

    std::pair<bool, Expr *> walkToExprPre(Expr *expr) override {
      // Note that the subexpression of a #selector expression is
      // unevaluated.
      if (auto sel = dyn_cast<ObjCSelectorExpr>(expr)) {
        CG.getConstraintSystem().UnevaluatedRootExprs.insert(sel->getSubExpr());
      }

      // Check an objc key-path expression, which fills in its semantic
      // expression as a string literal.
      if (auto keyPath = dyn_cast<KeyPathExpr>(expr)) {
        if (keyPath->isObjC()) {
          auto &cs = CG.getConstraintSystem();
          (void)cs.getTypeChecker().checkObjCKeyPathExpr(cs.DC, keyPath);
        }
      }

      // For closures containing only a single expression, the body participates
      // in type checking.
      if (auto closure = dyn_cast<ClosureExpr>(expr)) {
        auto &CS = CG.getConstraintSystem();
        if (closure->hasSingleExpressionBody()) {
          CG.enterClosure(closure);

          // Visit the closure itself, which produces a function type.
          auto funcTy = CG.visit(expr)->castTo<FunctionType>();
          CS.setType(expr, funcTy);
        }

        return { true, expr };
      }

      // Don't visit CoerceExpr with an empty sub expression. They may occur
      // if the body of a closure was not visited while pre-checking because
      // of an error in the closure's signature.
      if (auto coerceExpr = dyn_cast<CoerceExpr>(expr)) {
        if (!coerceExpr->getSubExpr()) {
          return { false, expr };
        }
      }

      // Don't visit IfExpr with empty sub expressions. They may occur
      // if the body of a closure was not visited while pre-checking because
      // of an error in the closure's signature.
      if (auto ifExpr = dyn_cast<IfExpr>(expr)) {
        if (!ifExpr->getThenExpr() || !ifExpr->getElseExpr())
          return { false, expr };
      }

      return { true, expr };
    }

    /// \brief Once we've visited the children of the given expression,
    /// generate constraints from the expression.
    Expr *walkToExprPost(Expr *expr) override {

      // Handle the Builtin.type_join* family of calls by replacing
      // them with dot_self_expr of type_expr with the type being the
      // result of the join.
      if (auto *apply = dyn_cast<ApplyExpr>(expr)) {
        auto fnExpr = apply->getFn();
        if (auto *UDE = dyn_cast<UnresolvedDotExpr>(fnExpr)) {
          auto &CS = CG.getConstraintSystem();
          auto typeOperation =
              ConstraintGenerator::getTypeOperation(UDE, CS.getASTContext());

          if (typeOperation != ConstraintGenerator::TypeOperation::None) {
            auto joinMetaTy =
                CG.resultOfTypeOperation(typeOperation, apply->getArg());
            auto joinTy = joinMetaTy->castTo<MetatypeType>()->getInstanceType();

            auto *TE = TypeExpr::createImplicit(joinTy, CS.getASTContext());
            CS.cacheType(TE);

            auto *DSE = new (CS.getASTContext())
                DotSelfExpr(TE, SourceLoc(), SourceLoc(), CS.getType(TE));
            DSE->setImplicit();
            CS.cacheType(DSE);

            return DSE;
          }
        }
      }

      if (auto closure = dyn_cast<ClosureExpr>(expr)) {
        if (closure->hasSingleExpressionBody()) {
          CG.exitClosure(closure);

          auto &CS = CG.getConstraintSystem();
          Type closureTy = CS.getType(closure);

          // If the function type has an error in it, we don't want to solve the
          // system.
          if (closureTy && closureTy->hasError())
            return nullptr;

          CS.setType(closure, closureTy);

          // Visit the body. It's type needs to be convertible to the function's
          // return type.
          auto resultTy = closureTy->castTo<FunctionType>()->getResult();
          Type bodyTy = CS.getType(closure->getSingleExpressionBody());
          CG.getConstraintSystem().setFavoredType(expr, bodyTy.getPointer());
          CG.getConstraintSystem()
            .addConstraint(ConstraintKind::Conversion, bodyTy,
                           resultTy,
                           CG.getConstraintSystem()
                             .getConstraintLocator(
                               expr,
                               ConstraintLocator::ClosureResult));
          return expr;
        }
      }

      if (auto type = CG.visit(expr)) {
        auto &CS = CG.getConstraintSystem();
        auto simplifiedType = CS.simplifyType(type);

        CS.setType(expr, simplifiedType);

        return expr;
      }

      return nullptr;
    }

    /// \brief Ignore statements.
    std::pair<bool, Stmt *> walkToStmtPre(Stmt *stmt) override {
      return { false, stmt };
    }

    /// \brief Ignore declarations.
    bool walkToDeclPre(Decl *decl) override { return false; }
  };

  /// AST walker that records the keyword arguments provided at each
  /// call site.
  class ArgumentLabelWalker : public ASTWalker {
    ConstraintSystem &CS;
    llvm::DenseMap<Expr *, Expr *> ParentMap;

  public:
    ArgumentLabelWalker(ConstraintSystem &cs, Expr *expr) 
      : CS(cs), ParentMap(expr->getParentMap()) { }

    using State = ConstraintSystem::ArgumentLabelState;

    void associateArgumentLabels(Expr *fn, State labels,
                                 bool labelsArePermanent) {
      // Dig out the function, looking through, parentheses, ?, and !.
      do {
        fn = fn->getSemanticsProvidingExpr();

        if (auto force = dyn_cast<ForceValueExpr>(fn)) {
          fn = force->getSubExpr();
          continue;
        }

        if (auto bind = dyn_cast<BindOptionalExpr>(fn)) {
          fn = bind->getSubExpr();
          continue;
        }

        break;
      } while (true);

      // Record the labels.
      if (!labelsArePermanent)
        labels.Labels = CS.allocateCopy(labels.Labels);
      CS.ArgumentLabels[CS.getConstraintLocator(fn)] = labels;
    }

    std::pair<bool, Expr *> walkToExprPre(Expr *expr) override {
      if (auto call = dyn_cast<CallExpr>(expr)) {
        associateArgumentLabels(call->getFn(),
                                { call->getArgumentLabels(),
                                  call->hasTrailingClosure() },
                                /*labelsArePermanent=*/true);
        return { true, expr };
      }

      // FIXME: other expressions have argument labels, but this is an
      // optimization, so stage it in later.
      return { true, expr };
    }
  };

} // end anonymous namespace

Expr *ConstraintSystem::generateConstraints(Expr *expr) {
  // Remove implicit conversions from the expression.
  expr = expr->walk(SanitizeExpr(getTypeChecker()));

  // Walk the expression to associate labeled arguments.
  expr->walk(ArgumentLabelWalker(*this, expr));

  // Walk the expression, generating constraints.
  ConstraintGenerator cg(*this);
  ConstraintWalker cw(cg);
  
  Expr* result = expr->walk(cw);
  
  if (result)
    this->optimizeConstraints(result);

  return result;
}

Expr *ConstraintSystem::generateConstraintsShallow(Expr *expr) {
  // Sanitize the expression.
  expr = SanitizeExpr(getTypeChecker()).walkToExprPost(expr);

  cacheSubExprTypes(expr);

  // Visit the top-level expression generating constraints.
  ConstraintGenerator cg(*this);
  auto type = cg.visit(expr);
  if (!type)
    return nullptr;
  
  this->optimizeConstraints(expr);
  
  auto &CS = CG.getConstraintSystem();
  CS.setType(expr, type);

  return expr;
}

Type ConstraintSystem::generateConstraints(Pattern *pattern,
                                           ConstraintLocatorBuilder locator) {
  ConstraintGenerator cg(*this);
  return cg.getTypeForPattern(pattern, locator);
}

void ConstraintSystem::optimizeConstraints(Expr *e) {
  
  SmallVector<Expr *, 16> linkedExprs;
  
  // Collect any linked expressions.
  LinkedExprCollector collector(linkedExprs);
  e->walk(collector);
  
  // Favor types, as appropriate.
  for (auto linkedExpr : linkedExprs) {
    computeFavoredTypeForExpr(linkedExpr, *this);
  }
  
  // Optimize the constraints.
  ConstraintOptimizer optimizer(*this);
  e->walk(optimizer);
}

class InferUnresolvedMemberConstraintGenerator : public ConstraintGenerator {
  Expr *Target;
  TypeVariableType *VT;

  TypeVariableType *createFreeTypeVariableType(Expr *E) {
    auto &CS = getConstraintSystem();
    return CS.createTypeVariable(CS.getConstraintLocator(nullptr),
                                 TVO_CanBindToLValue |
                                 TVO_CanBindToInOut);
  }

public:
  InferUnresolvedMemberConstraintGenerator(Expr *Target, ConstraintSystem &CS) :
    ConstraintGenerator(CS), Target(Target), VT(nullptr) {};
  ~InferUnresolvedMemberConstraintGenerator() override = default;

  Type visitUnresolvedMemberExpr(UnresolvedMemberExpr *Expr) override {
    if (Target != Expr) {
      // If expr is not the target, do the default constraint generation.
      return ConstraintGenerator::visitUnresolvedMemberExpr(Expr);
    }
    // Otherwise, create a type variable saying we know nothing about this expr.
    assert(!VT && "cannot reassign type variable.");
    return VT = createFreeTypeVariableType(Expr);
  }

  Type visitParenExpr(ParenExpr *Expr) override {
    if (Target != Expr) {
      // If expr is not the target, do the default constraint generation.
      return ConstraintGenerator::visitParenExpr(Expr);
    }
    // Otherwise, create a type variable saying we know nothing about this expr.
    assert(!VT && "cannot reassign type variable.");
    return VT = createFreeTypeVariableType(Expr);
  }

  Type visitErrorExpr(ErrorExpr *Expr) override {
    return createFreeTypeVariableType(Expr);
  }

  Type visitCodeCompletionExpr(CodeCompletionExpr *Expr) override {
    return createFreeTypeVariableType(Expr);
  }

  Type visitImplicitConversionExpr(ImplicitConversionExpr *Expr) override {
    // We override this function to avoid assertion failures. Typically, we do have
    // a type-checked AST when trying to infer the types of unresolved members.
    return Expr->getType();
  }

  bool collectResolvedType(Solution &S, SmallVectorImpl<Type> &PossibleTypes) {
    if (auto Bind = S.typeBindings[VT]) {
      // We allow type variables in the overall solution, but must skip any
      // type variables in the binding for VT; these types must outlive the
      // constraint solver memory arena.
      if (!Bind->hasTypeVariable()) {
        PossibleTypes.push_back(Bind);
        return true;
      }
    }
    return false;
  }
};

bool swift::typeCheckUnresolvedExpr(DeclContext &DC,
                                    Expr *E, Expr *Parent,
                                    SmallVectorImpl<Type> &PossibleTypes) {
  PrettyStackTraceExpr stackTrace(DC.getASTContext(),
                                  "type-checking unresolved member", Parent);
  ConstraintSystemOptions Options = ConstraintSystemFlags::AllowFixes;
  auto *TC = static_cast<TypeChecker*>(DC.getASTContext().getLazyResolver());
  ConstraintSystem CS(*TC, &DC, Options);
  CleanupIllFormedExpressionRAII cleanup(TC->Context, Parent);
  InferUnresolvedMemberConstraintGenerator MCG(E, CS);
  ConstraintWalker cw(MCG);
  Parent->walk(cw);

  if (TC->getLangOpts().DebugConstraintSolver) {
    auto &log = DC.getASTContext().TypeCheckerDebug->getStream();
    log << "---Initial constraints for the given expression---\n";
    Parent->print(log);
    log << "\n";
    CS.print(log);
  }

  SmallVector<Solution, 3> solutions;
  if (CS.solve(Parent, solutions, FreeTypeVariableBinding::Allow)) {
    return false;
  }

  for (auto &S : solutions) {
    bool resolved = MCG.collectResolvedType(S, PossibleTypes);

    if (TC->getLangOpts().DebugConstraintSolver) {
      auto &log = DC.getASTContext().TypeCheckerDebug->getStream();
      log << "--- Solution ---\n";
      S.dump(log);
      if (resolved)
        log << "--- Resolved target type ---\n" << PossibleTypes.back() << "\n";
    }
  }
  return !PossibleTypes.empty();
}

bool swift::isExtensionApplied(DeclContext &DC, Type BaseTy,
                               const ExtensionDecl *ED) {
  ConstraintSystemOptions Options;
  NominalTypeDecl *Nominal = BaseTy->getNominalOrBoundGenericNominal();
  if (!Nominal || !BaseTy->isSpecialized() ||
      ED->getGenericRequirements().empty() ||
      // We'll crash if we leak type variables from one constraint
      // system into the new one created below.
      BaseTy->hasTypeVariable())
    return true;
  std::unique_ptr<TypeChecker> CreatedTC;
  // If the current ast context has no type checker, create one for it.
  auto *TC = static_cast<TypeChecker*>(DC.getASTContext().getLazyResolver());
  if (!TC) {
    CreatedTC.reset(new TypeChecker(DC.getASTContext()));
    TC = CreatedTC.get();
  }
  if (ED->getAsProtocolExtensionContext())
    return TC->isProtocolExtensionUsable(&DC, BaseTy, const_cast<ExtensionDecl*>(ED));
  ConstraintSystem CS(*TC, &DC, Options);
  auto Loc = CS.getConstraintLocator(nullptr);
  bool Failed = false;

  // Prepare type substitution map.
  SubstitutionMap Substitutions = BaseTy->getContextSubstitutionMap(
    DC.getParentModule(), ED);

  // For every requirement, add a constraint.
  for (auto Req : ED->getGenericRequirements()) {
    if (auto resolved = Req.subst(Substitutions)) {
      CS.addConstraint(*resolved, Loc);
    } else {
      Failed = true;
    }
  }
  if (Failed)
    return true;

  // Having a solution implies the extension's requirements have been fulfilled.
  return CS.solveSingle().hasValue();
}

static bool canSatisfy(Type type1, Type type2, bool openArchetypes,
                       ConstraintKind kind, DeclContext *dc) {
  std::unique_ptr<TypeChecker> CreatedTC;
  // If the current ASTContext has no type checker, create one for it.
  auto *TC = static_cast<TypeChecker*>(dc->getASTContext().getLazyResolver());
  if (!TC) {
    CreatedTC.reset(new TypeChecker(dc->getASTContext()));
    TC = CreatedTC.get();
  }
  return TC->typesSatisfyConstraint(type1, type2, openArchetypes, kind, dc,
                                    /*unwrappedIUO=*/nullptr);
}

bool swift::canPossiblyEqual(Type T1, Type T2, DeclContext &DC) {
  return canSatisfy(T1, T2, true, ConstraintKind::Equal, &DC);
}

bool swift::canPossiblyConvertTo(Type T1, Type T2, DeclContext &DC) {
  return canSatisfy(T1, T2, true, ConstraintKind::Conversion, &DC);
}

bool swift::isEqual(Type T1, Type T2, DeclContext &DC) {
  return T1->isEqual(T2);
}

bool swift::isConvertibleTo(Type T1, Type T2, DeclContext &DC) {
  return canSatisfy(T1, T2, false, ConstraintKind::Conversion, &DC);
}

struct ResolvedMemberResult::Implementation {
  llvm::SmallVector<ValueDecl*, 4> AllDecls;
  unsigned ViableStartIdx;
  Optional<unsigned> BestIdx;
};

ResolvedMemberResult::ResolvedMemberResult(): Impl(*new Implementation()) {};

ResolvedMemberResult::~ResolvedMemberResult() { delete &Impl; };

ResolvedMemberResult::operator bool() const {
  return !Impl.AllDecls.empty();
}

bool ResolvedMemberResult::
hasBestOverload() const { return Impl.BestIdx.hasValue(); }

ValueDecl* ResolvedMemberResult::
getBestOverload() const { return Impl.AllDecls[Impl.BestIdx.getValue()]; }

ArrayRef<ValueDecl*> ResolvedMemberResult::
getMemberDecls(InterestedMemberKind Kind) {
  auto Result = llvm::makeArrayRef(Impl.AllDecls);
  switch (Kind) {
  case InterestedMemberKind::Viable:
    return Result.slice(Impl.ViableStartIdx);
  case InterestedMemberKind::Unviable:
    return Result.slice(0, Impl.ViableStartIdx);
  case InterestedMemberKind::All:
    return Result;
  }
}

ResolvedMemberResult
swift::resolveValueMember(DeclContext &DC, Type BaseTy, DeclName Name) {
  ResolvedMemberResult Result;
  std::unique_ptr<TypeChecker> CreatedTC;
  // If the current ast context has no type checker, create one for it.
  auto *TC = static_cast<TypeChecker*>(DC.getASTContext().getLazyResolver());
  if (!TC) {
    CreatedTC.reset(new TypeChecker(DC.getASTContext()));
    TC = CreatedTC.get();
  }
  ConstraintSystem CS(*TC, &DC, None);

  // Look up all members of BaseTy with the given Name.
  MemberLookupResult LookupResult = CS.performMemberLookup(
    ConstraintKind::ValueMember, Name, BaseTy, FunctionRefKind::SingleApply,
    nullptr, false);

  // Keep track of all the unviable members.
  for (auto Can : LookupResult.UnviableCandidates)
    Result.Impl.AllDecls.push_back(Can.first.getDecl());

  // Keep track of the start of viable choices.
  Result.Impl.ViableStartIdx = Result.Impl.AllDecls.size();

  // If no viable members, we are done.
  if (LookupResult.ViableCandidates.empty())
    return Result;

  // Try to figure out the best overload.
  ConstraintLocator *Locator = CS.getConstraintLocator(nullptr);
  TypeVariableType *TV = CS.createTypeVariable(Locator,
                                               TVO_CanBindToLValue |
                                               TVO_CanBindToInOut);
  CS.addOverloadSet(TV, LookupResult.ViableCandidates, &DC, Locator);
  Optional<Solution> OpSolution = CS.solveSingle();
  ValueDecl *Selected = nullptr;
  if (OpSolution.hasValue()) {
    Selected = OpSolution.getValue().overloadChoices[Locator].choice.getDecl();
  }
  for (OverloadChoice& Choice : LookupResult.ViableCandidates) {
    ValueDecl *VD = Choice.getDecl();

    // If this VD is the best overload, keep track of its index.
    if (VD == Selected)
      Result.Impl.BestIdx = Result.Impl.AllDecls.size();
    Result.Impl.AllDecls.push_back(VD);
  }
  return Result;
}
