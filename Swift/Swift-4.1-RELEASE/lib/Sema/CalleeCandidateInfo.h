//===--- CallerCandidateInfo.h - Failure Diagnosis Info -------------*- C++ -*-===//
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
// This file represents an analyzed function pointer to determine the
// candidates that could be called, or the one concrete decl that will be
// called if not ambiguous.
//
//===----------------------------------------------------------------------===//


#ifndef SWIFT_SEMA_CALLEECANDIDATEINFO_H
#define SWIFT_SEMA_CALLEECANDIDATEINFO_H

namespace swift {
  
  using namespace constraints;

  /// Each match in an ApplyExpr is evaluated for how close of a match it is.
  /// The result is captured in this enum value, where the earlier entries are
  /// most specific.
  enum CandidateCloseness {
    CC_ExactMatch,              ///< This is a perfect match for the arguments.
    CC_Unavailable,             ///< Marked unavailable with @available.
    CC_Inaccessible,            ///< Not accessible from the current context.
    CC_NonLValueInOut,          ///< First arg is inout but no lvalue present.
    CC_SelfMismatch,            ///< Self argument mismatches.
    CC_OneArgumentNearMismatch, ///< All arguments except one match, near miss.
    CC_OneArgumentMismatch,     ///< All arguments except one match.
    CC_OneGenericArgumentNearMismatch, ///< All arguments except one match, guessing generic binding, near miss.
    CC_OneGenericArgumentMismatch,     ///< All arguments except one match, guessing generic binding.
    CC_ArgumentNearMismatch,    ///< Argument list mismatch, near miss.
    CC_ArgumentMismatch,        ///< Argument list mismatch.
    CC_GenericNonsubstitutableMismatch, ///< Arguments match each other, but generic binding not substitutable.
    CC_ArgumentLabelMismatch,   ///< Argument label mismatch.
    CC_ArgumentCountMismatch,   ///< This candidate has wrong # arguments.
    CC_GeneralMismatch          ///< Something else is wrong.
  };

  /// This is a candidate for a callee, along with an uncurry level.
  ///
  /// The uncurry level specifies how far much of a curried value has already
  /// been applied.  For example, in a funcdecl of:
  ///     func f(a:Int)(b:Double) -> Int
  /// Uncurry level of 0 indicates that we're looking at the "a" argument, an
  /// uncurry level of 1 indicates that we're looking at the "b" argument.
  ///
  /// entityType specifies a specific type to use for this decl/expr that may be
  /// more resolved than the concrete type.  For example, it may have generic
  /// arguments substituted in.
  ///
  struct UncurriedCandidate {
    PointerUnion<ValueDecl *, Expr*> declOrExpr;
    unsigned level;
    Type entityType;
    
    // If true, entityType is written in terms of caller archetypes,
    // with any unbound generic arguments remaining as interface
    // type parameters in terms of the callee generic signature.
    //
    // If false, entityType is written in terms of callee archetypes.
    //
    // FIXME: Clean this up.
    bool substituted;
    
    UncurriedCandidate(ValueDecl *decl, unsigned level);
    UncurriedCandidate(Expr *expr, Type type)
    : declOrExpr(expr), level(0), entityType(type), substituted(true) {}
    
    ValueDecl *getDecl() const {
      return declOrExpr.dyn_cast<ValueDecl*>();
    }
    
    Expr *getExpr() const {
      return declOrExpr.dyn_cast<Expr*>();
    }
    
    Type getUncurriedType() const {
      // Start with the known type of the decl.
      auto type = entityType;
      for (unsigned i = 0, e = level; i != e; ++i) {
        auto funcTy = type->getAs<AnyFunctionType>();
        if (!funcTy) return Type();
        type = funcTy->getResult();
      }
      
      return type;
    }
    
    AnyFunctionType *getUncurriedFunctionType() const {
      if (auto type = getUncurriedType())
        return type->getAs<AnyFunctionType>();
      return nullptr;
    }
    
    /// Given a function candidate with an uncurry level, return the parameter
    /// type at the specified uncurry level.  If there is an error getting to
    /// the specified input, this returns a null Type.
    Type getArgumentType() const {
      if (auto *funcTy = getUncurriedFunctionType())
        return funcTy->getInput();
      return Type();
    }
    
    /// Given a function candidate with an uncurry level, return the parameter
    /// type at the specified uncurry level.  If there is an error getting to
    /// the specified input, this returns a null Type.
    Type getResultType() const {
      if (auto *funcTy = getUncurriedFunctionType())
        return funcTy->getResult();
      return Type();
    }
    
    /// Retrieve the argument labels that should be used to invoke this
    /// candidate.
    ArrayRef<Identifier> getArgumentLabels(SmallVectorImpl<Identifier> &scratch);

    void dump() const LLVM_ATTRIBUTE_USED;
  };

  class CalleeCandidateInfo {
  public:
    ConstraintSystem &CS;
    
    /// This is the name of the callee as extracted from the call expression.
    /// This can be empty in cases like calls to closure exprs.
    std::string declName;
    
    /// True if the call site for this callee syntactically has a trailing
    /// closure specified.
    bool hasTrailingClosure;
    
    /// This is the list of candidates identified.
    SmallVector<UncurriedCandidate, 4> candidates;
    
    /// This tracks how close the candidates are, after filtering.
    CandidateCloseness closeness = CC_GeneralMismatch;
    
    /// When we have a candidate that differs by a single argument mismatch, we
    /// keep track of which argument passed to the call is failed, and what the
    /// expected type is.  If the candidate set disagrees, or if there is more
    /// than a single argument mismatch, then this is "{ -1, Type() }".
    struct FailedArgumentInfo {
      int argumentNumber = -1;      ///< Arg # at the call site.
      Type parameterType = Type();  ///< Expected type at the decl site.
      DeclContext *declContext = nullptr; ///< Context at the candidate declaration.
      
      bool isValid() const { return argumentNumber != -1; }
      
      bool operator!=(const FailedArgumentInfo &other) {
        if (argumentNumber != other.argumentNumber) return true;
        if (declContext != other.declContext) return true;
        // parameterType can be null, and isEqual doesn't handle this.
        if (!parameterType || !other.parameterType)
          return parameterType.getPointer() != other.parameterType.getPointer();
        return !parameterType->isEqual(other.parameterType);
      }
    };
    FailedArgumentInfo failedArgument = FailedArgumentInfo();
    
    /// Analyze a function expr and break it into a candidate set.  On failure,
    /// this leaves the candidate list empty.
    CalleeCandidateInfo(Expr *Fn, bool hasTrailingClosure, ConstraintSystem &CS)
    : CS(CS), hasTrailingClosure(hasTrailingClosure) {
      collectCalleeCandidates(Fn, /*implicitDotSyntax=*/false);
    }
    
    CalleeCandidateInfo(Type baseType, ArrayRef<OverloadChoice> candidates,
                        bool hasTrailingClosure, ConstraintSystem &CS,
                        bool selfAlreadyApplied = true);
    
    typedef std::pair<CandidateCloseness, FailedArgumentInfo> ClosenessResultTy;
    typedef const std::function<ClosenessResultTy(UncurriedCandidate)>
    &ClosenessPredicate;
    
    /// After the candidate list is formed, it can be filtered down to discard
    /// obviously mismatching candidates and compute a "closeness" for the
    /// resultant set.
    ClosenessResultTy
    evaluateCloseness(UncurriedCandidate candidate,
                      ArrayRef<AnyFunctionType::Param> actualArgs);
    
    void filterListArgs(ArrayRef<AnyFunctionType::Param> actualArgs);
    void filterList(Type actualArgsType, ArrayRef<Identifier> argLabels) {
      return filterListArgs(decomposeArgType(actualArgsType, argLabels));
    }
    void filterList(ClosenessPredicate predicate);
    void filterContextualMemberList(Expr *argExpr);
    
    bool empty() const { return candidates.empty(); }
    unsigned size() const { return candidates.size(); }
    UncurriedCandidate operator[](unsigned i) const {
      return candidates[i];
    }
    
    /// Given a set of parameter lists from an overload group, and a list of
    /// arguments, emit a diagnostic indicating any partially matching
    /// overloads.
    void suggestPotentialOverloads(SourceLoc loc, bool isResult = false);
    
    
    /// If the candidate set has been narrowed to a single parameter or single
    /// archetype that has argument type errors, diagnose that error and
    /// return true.
    bool diagnoseGenericParameterErrors(Expr *badArgExpr);
    
    /// Emit a diagnostic and return true if this is an error condition we can
    /// handle uniformly.  This should be called after filtering the candidate
    /// list.
    bool diagnoseSimpleErrors(const Expr *E);
    
    void dump() const LLVM_ATTRIBUTE_USED;
    
  private:
    void collectCalleeCandidates(Expr *fnExpr, bool implicitDotSyntax);
  };
} // end swift namespace

#endif /* SWIFT_SEMA_CALLEECANDIDATEINFO_H */
