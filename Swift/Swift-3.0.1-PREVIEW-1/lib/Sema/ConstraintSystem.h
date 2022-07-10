//===--- ConstraintSystem.h - Constraint-based Type Checking ----*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file provides the constraint-based type checker, anchored by the
// \c ConstraintSystem class, which provides type checking and type
// inference for expressions.
//
//===----------------------------------------------------------------------===//
#ifndef SWIFT_SEMA_CONSTRAINT_SYSTEM_H
#define SWIFT_SEMA_CONSTRAINT_SYSTEM_H

#include "TypeChecker.h"
#include "Constraint.h"
#include "ConstraintGraph.h"
#include "ConstraintGraphScope.h"
#include "ConstraintLocator.h"
#include "OverloadChoice.h"
#include "swift/Basic/LLVM.h"
#include "swift/Basic/OptionSet.h"
#include "swift/AST/ASTVisitor.h"
#include "swift/AST/NameLookup.h"
#include "swift/AST/Types.h"
#include "swift/AST/TypeCheckerDebugConsumer.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <functional>

namespace swift {

class Expr;

namespace constraints {

class ConstraintGraph;
class ConstraintGraphNode;
class ConstraintSystem;

} // end namespace constraints

} // end namespace swift

/// \brief Allocate memory within the given constraint system.
void *operator new(size_t bytes, swift::constraints::ConstraintSystem& cs,
                   size_t alignment = 8);

namespace swift {

namespace constraints {

/// \brief A handle that holds the saved state of a type variable, which
/// can be restored.
class SavedTypeVariableBinding {
  /// \brief The type variable and type variable options.
  llvm::PointerIntPair<TypeVariableType *, 3> TypeVarAndOptions;
  
  /// \brief The parent or fixed type.
  llvm::PointerUnion<TypeVariableType *, TypeBase *> ParentOrFixed;

public:
  explicit SavedTypeVariableBinding(TypeVariableType *typeVar);

  /// \brief Restore the state of the type variable to the saved state.
  void restore();

  TypeVariableType *getTypeVariable() { return TypeVarAndOptions.getPointer(); }
  unsigned getOptions() { return TypeVarAndOptions.getInt(); }
};

/// \brief A set of saved type variable bindings.
typedef SmallVector<SavedTypeVariableBinding, 16> SavedTypeVariableBindings;

class ConstraintLocator;

/// Describes a conversion restriction or a fix.
struct RestrictionOrFix {
  ConversionRestrictionKind Restriction;
  Fix TheFix;
  bool IsRestriction;

public:
  RestrictionOrFix(ConversionRestrictionKind restriction)
  : Restriction(restriction), IsRestriction(true) { }

  RestrictionOrFix(Fix fix) : TheFix(fix), IsRestriction(false) { }

  RestrictionOrFix(FixKind fix) : TheFix(fix), IsRestriction(false) { }

  Optional<ConversionRestrictionKind> getRestriction() const {
    if (IsRestriction)
      return Restriction;

    return None;
  }

  Optional<Fix> getFix() const {
    if (!IsRestriction)
      return TheFix;

    return None;
  }
};

} // end namespace constraints

/// Options that describe how a type variable can be used.
enum TypeVariableOptions {
  /// Whether the type variable can be bound to an lvalue type or not.
  TVO_CanBindToLValue = 0x01,

  /// Whether a more specific deduction for this type variable implies a
  /// better solution to the constraint system.
  TVO_PrefersSubtypeBinding = 0x02,

  /// Whether the variable must be bound to a materializable type.
  TVO_MustBeMaterializable = 0x04
};

/// \brief The implementation object for a type variable used within the
/// constraint-solving type checker.
///
/// The implementation object for a type variable contains information about
/// the type variable, where it was generated, what protocols it must conform
/// to, what specific types it might be and, eventually, the fixed type to
/// which it is assigned.
class TypeVariableType::Implementation {
  /// Type variable options.
  unsigned Options : 3;

  /// \brief The locator that describes where this type variable was generated.
  constraints::ConstraintLocator *locator;

  /// \brief Either the parent of this type variable within an equivalence
  /// class of type variables, or the fixed type to which this type variable
  /// type is bound.
  llvm::PointerUnion<TypeVariableType *, TypeBase *> ParentOrFixed;

  /// The corresponding node in the constraint graph.
  constraints::ConstraintGraphNode *GraphNode = nullptr;

  ///  Index into the list of type variables, as used by the
  ///  constraint graph.
  unsigned GraphIndex;

  friend class constraints::SavedTypeVariableBinding;

public:
  
  /// \brief If this type variable is an opened literal expression, keep track
  /// of the associated literal conformance for optimization and diagnostic
  /// purposes.
  ProtocolDecl *literalConformanceProto = nullptr;
  
  explicit Implementation(constraints::ConstraintLocator *locator,
                          unsigned options)
    : Options(options), locator(locator),
      ParentOrFixed(getTypeVariable()) { }

  /// \brief Retrieve the unique ID corresponding to this type variable.
  unsigned getID() const { return getTypeVariable()->getID(); }

  /// Whether this type variable can bind to an lvalue type.
  bool canBindToLValue() const { return Options & TVO_CanBindToLValue; }

  /// Whether this type variable prefers a subtype binding over a supertype
  /// binding.
  bool prefersSubtypeBinding() const {
    return Options & TVO_PrefersSubtypeBinding;
  }

  bool mustBeMaterializable() const {
    return Options & TVO_MustBeMaterializable;
  }

  /// \brief Retrieve the type variable associated with this implementation.
  TypeVariableType *getTypeVariable() {
    return reinterpret_cast<TypeVariableType *>(this) - 1;
  }

  /// \brief Retrieve the type variable associated with this implementation.
  const TypeVariableType *getTypeVariable() const {
    return reinterpret_cast<const TypeVariableType *>(this) - 1;
  }

  /// Retrieve the corresponding node in the constraint graph.
  constraints::ConstraintGraphNode *getGraphNode() const { return GraphNode; }

  /// Set the corresponding node in the constraint graph.
  void setGraphNode(constraints::ConstraintGraphNode *newNode) { 
    GraphNode = newNode; 
  }

  /// Retrieve the index into the constraint graph's list of type variables.
  unsigned getGraphIndex() const { 
    assert(GraphNode && "Graph node isn't set");
    return GraphIndex; 
  }

  /// Set the index into the constraint graph's list of type variables.
  void setGraphIndex(unsigned newIndex) { GraphIndex = newIndex; }
  
  /// \brief Check whether this type variable either has a representative that
  /// is not itself or has a fixed type binding.
  bool hasRepresentativeOrFixed() const {
    // If we have a fixed type, we're done.
    if (!ParentOrFixed.is<TypeVariableType *>())
      return true;

    // Check whether the representative is different from our own type
    // variable.
    return ParentOrFixed.get<TypeVariableType *>() != getTypeVariable();
  }

  /// \brief Record the current type-variable binding.
  void recordBinding(constraints::SavedTypeVariableBindings &record) {
    record.push_back(constraints::SavedTypeVariableBinding(getTypeVariable()));
  }

  /// \brief Retrieve the locator describing where this type variable was
  /// created.
  constraints::ConstraintLocator *getLocator() const {
    return locator;
  }

  /// \brief Retrieve the archetype opened by this type variable.
  ArchetypeType *getArchetype() const;

  /// \brief Retrieve the representative of the equivalence class to which this
  /// type variable belongs.
  ///
  /// \param record The record of changes made by retrieving the representative,
  /// which can happen due to path compression. If null, path compression is
  /// not performed.
  TypeVariableType *
  getRepresentative(constraints::SavedTypeVariableBindings *record) {
    // Find the representative type variable.
    auto result = getTypeVariable();
    Implementation *impl = this;
    while (impl->ParentOrFixed.is<TypeVariableType *>()) {
      // Extract the representative.
      auto nextTV = impl->ParentOrFixed.get<TypeVariableType *>();
      if (nextTV == result)
        break;

      result = nextTV;
      impl = &nextTV->getImpl();
    }

    if (impl == this || !record)
      return result;

    // Perform path compression.
    impl = this;
    while (impl->ParentOrFixed.is<TypeVariableType *>()) {
      // Extract the representative.
      auto nextTV = impl->ParentOrFixed.get<TypeVariableType *>();
      if (nextTV == result)
        break;

      // Record the state change.
      impl->recordBinding(*record);

      impl->ParentOrFixed = result;
      impl = &nextTV->getImpl();
    }

    return result;
  }

  /// \brief Merge the equivalence class of this type variable with the
  /// equivalence class of another type variable.
  ///
  /// \param other The type variable to merge with.
  ///
  /// \param record The record of state changes.
  void mergeEquivalenceClasses(TypeVariableType *other,
                               constraints::SavedTypeVariableBindings *record) {
    // Merge the equivalence classes corresponding to these two type
    // variables. Always merge 'up' the constraint stack, because it is simpler.
    if (getID() > other->getImpl().getID()) {
      other->getImpl().mergeEquivalenceClasses(getTypeVariable(), record);
      return;
    }

    auto otherRep = other->getImpl().getRepresentative(record);
    if (record)
      otherRep->getImpl().recordBinding(*record);
    otherRep->getImpl().ParentOrFixed = getTypeVariable();
    if (!mustBeMaterializable() && otherRep->getImpl().mustBeMaterializable()) {
      if (record)
        recordBinding(*record);
      Options |= TVO_MustBeMaterializable;
    }
  }

  /// \brief Retrieve the fixed type that corresponds to this type variable,
  /// if there is one.
  ///
  /// \returns the fixed type associated with this type variable, or a null
  /// type if there is no fixed type.
  ///
  /// \param record The record of changes made by retrieving the representative,
  /// which can happen due to path compression. If null, path compression is
  /// not performed.
  Type getFixedType(constraints::SavedTypeVariableBindings *record) {
    // Find the representative type variable.
    auto rep = getRepresentative(record);
    Implementation &repImpl = rep->getImpl();

    // Check whether it has a fixed type.
    if (auto type = repImpl.ParentOrFixed.dyn_cast<TypeBase *>())
      return type;

    return Type();
  }

  /// \brief Assign a fixed type to this equivalence class.
  void assignFixedType(Type type,
                       constraints::SavedTypeVariableBindings *record) {
    assert((!getFixedType(0) || getFixedType(0)->isEqual(type)) &&
           "Already has a fixed type!");
    auto rep = getRepresentative(record);
    if (record)
      rep->getImpl().recordBinding(*record);
    rep->getImpl().ParentOrFixed = type.getPointer();
  }

  void setMustBeMaterializable(constraints::SavedTypeVariableBindings *record) {
    auto rep = getRepresentative(record);
    if (!rep->getImpl().mustBeMaterializable()) {
      if (record)
        rep->getImpl().recordBinding(*record);
      rep->getImpl().Options |= TVO_MustBeMaterializable;
    }
  }

  /// \brief Print the type variable to the given output stream.
  void print(llvm::raw_ostream &OS);
};

namespace constraints {

struct ResolvedOverloadSetListItem;

/// \brief The kind of type matching to perform in matchTypes().
enum class TypeMatchKind : char {
  /// \brief Bind the types together directly.
  BindType,
  /// \brief Binds to a pointer element type.
  BindToPointerType,
  /// \brief Bind the type of a function parameter to the type of a reference
  /// to it from within the function body. The param type is an inout type iff
  /// the reference type is an lvalue type with the same object type.
  /// Otherwise, the two types must be the same type.
  BindParamType,
  /// \brief Require the types to match exactly, but strips lvalueness from
  /// a type when binding to a type variable.
  SameType,
  /// \brief Require that the first type conform to the second type (which
  /// must be a protocol).
  ConformsTo,
  /// \brief Require the first type to be a subtype of the second type
  /// (or be an exact match or trivial subtype).
  Subtype,
  /// \brief Requires the first type to be convertible to the second type,
  /// which includes exact matches and both forms of subtyping.
  Conversion,
  /// \brief Requires the first type to be explicitly convertible to the second
  /// type, which includes all forms of conversion included in Conversion, in
  /// addition to bridging conversions.
  ExplicitConversion,
  /// \brief Requires the first type to be the element of an argument tuple
  /// that is convertible to the second type, which represents the corresponding
  /// element of the parameter tuple.
  ArgumentConversion,
  /// Requires the first type to be an argument type (tuple) that can be
  /// converted to the second type, which represents the parameter type (tuple).
  ArgumentTupleConversion,
  /// \brief Requires the first type to be convertible to the second type,
  /// which includes exact matches, both forms of subtyping, and inserting
  /// address-of to convert implicit lvalues to inout arguments.  This is
  /// used by assignment operators.
  OperatorArgumentConversion,
  /// Like \c ArgumentTupleConversion, but for operators.
  OperatorArgumentTupleConversion,
};

/// \brief The result of comparing two constraint systems that are a solutions
/// to the given set of constraints.
enum class SolutionCompareResult {
  /// \brief The two solutions are incomparable, because, e.g., because one
  /// solution has some better decisions and some worse decisions than the
  /// other.
  Incomparable,
  /// \brief The two solutions are identical.
  Identical,
  /// \brief The first solution is better than the second.
  Better,
  /// \brief The second solution is better than the first.
  Worse
};

/// An overload that has been selected in a particular solution.
///
/// A selected overload captures the specific overload choice (e.g., a
/// particular declaration) as well as the type to which the reference to the
/// declaration was opened, which may involve type variables.
struct SelectedOverload {
  /// The overload choice.
  OverloadChoice choice;

  /// The opened type of the base of the reference to this overload, if
  /// we're referencing a member.
  Type openedFullType;

  /// The opened type produced by referring to this overload.
  Type openedType;
};

/// Describes an aspect of a solution that affects its overall score, i.e., a
/// user-defined conversions.
enum ScoreKind {
  // These values are used as indices into a Score value.

  /// A reference to an @unavailable declaration.
  SK_Unavailable,
  /// A fix needs to be applied to the source.
  SK_Fix,
  /// An implicit force of an implicitly unwrapped optional value.
  SK_ForceUnchecked,
  /// A user-defined conversion.
  SK_UserConversion,
  /// A non-trivial function conversion.
  SK_FunctionConversion,
  /// A literal expression bound to a non-default literal type.
  SK_NonDefaultLiteral,
  /// An implicit bridged conversion between collection types.
  SK_CollectionBridgedConversion,
  /// An implicit upcast conversion between collection types.
  SK_CollectionUpcastConversion,
  /// A value-to-optional conversion.
  SK_ValueToOptional,
  /// A conversion from an inout to a pointer of matching element type.
  SK_ScalarPointerConversion,
  /// A conversion from an array to a pointer of matching element type.
  SK_ArrayPointerConversion,
  /// A conversion to an empty existential type ('Any' or '{}').
  SK_EmptyExistentialConversion,
  
  SK_LastScoreKind = SK_EmptyExistentialConversion,
};

/// The number of score kinds.
const unsigned NumScoreKinds = SK_LastScoreKind + 1;

/// Describes the fixed score of a solution to the constraint system.
struct Score {
  unsigned Data[NumScoreKinds] = {};

  friend Score &operator+=(Score &x, const Score &y) {
    for (unsigned i = 0; i != NumScoreKinds; ++i) {
      x.Data[i] += y.Data[i];
    }
    return x;
  }

  friend Score operator+(const Score &x, const Score &y) {
    Score result;
    for (unsigned i = 0; i != NumScoreKinds; ++i) {
      result.Data[i] = x.Data[i] + y.Data[i];
    }
    return result;
  }

  friend Score operator-(const Score &x, const Score &y) {
    Score result;
    for (unsigned i = 0; i != NumScoreKinds; ++i) {
      result.Data[i] = x.Data[i] - y.Data[i];
    }
    return result;
  }

  friend Score &operator-=(Score &x, const Score &y) {
    for (unsigned i = 0; i != NumScoreKinds; ++i) {
      x.Data[i] -= y.Data[i];
    }
    return x;
  }

  friend bool operator==(const Score &x, const Score &y) {
    for (unsigned i = 0; i != NumScoreKinds; ++i) {
      if (x.Data[i] != y.Data[i])
        return false;
    }

    return true;
  }

  friend bool operator!=(const Score &x, const Score &y) {
    return !(x == y);
  }

  friend bool operator<(const Score &x, const Score &y) {
    for (unsigned i = 0; i != NumScoreKinds; ++i) {
      if (x.Data[i] < y.Data[i])
        return true;

      if (x.Data[i] > y.Data[i])
        return false;
    }

    return false;
  }

  friend bool operator<=(const Score &x, const Score &y) {
    return !(y < x);
  }

  friend bool operator>(const Score &x, const Score &y) {
    return y < x;
  }

  friend bool operator>=(const Score &x, const Score &y) {
    return !(x < y);
  }

};

/// Display a score.
llvm::raw_ostream &operator<<(llvm::raw_ostream &out, const Score &score);

/// Describes a dependent type that has been opened to a particular type
/// variable.
typedef std::pair<CanType, TypeVariableType *> OpenedType;

/// \brief A complete solution to a constraint system.
///
/// A solution to a constraint system consists of type variable bindings to
/// concrete types for every type variable that is used in the constraint
/// system along with a set of mappings from each constraint locator
/// involving an overload set to the selected overload.
class Solution {
  /// \brief The constraint system this solution solves.
  ConstraintSystem *constraintSystem;

  /// \brief The fixed score for this solution.
  Score FixedScore;

public:
  /// \brief Create a solution for the given constraint system.
  Solution(ConstraintSystem &cs, const Score &score)
    : constraintSystem(&cs), FixedScore(score) {}

  // Solution is a non-copyable type for performance reasons.
  Solution(const Solution &other) = delete;
  Solution &operator=(const Solution &other) = delete;

  Solution(Solution &&other) = default;
  Solution &operator=(Solution &&other) = default;

  /// \brief Retrieve the constraint system that this solution solves.
  ConstraintSystem &getConstraintSystem() const { return *constraintSystem; }

  /// \brief The set of type bindings.
  llvm::SmallDenseMap<TypeVariableType *, Type> typeBindings;
  
  /// \brief The set of overload choices along with their types.
  llvm::SmallDenseMap<ConstraintLocator *, SelectedOverload> overloadChoices;

  /// The set of constraint restrictions used to arrive at this restriction,
  /// which informs constraint application.
  llvm::SmallDenseMap<std::pair<CanType, CanType>, ConversionRestrictionKind>
    ConstraintRestrictions;

  /// The list of fixes that need to be applied to the initial expression
  /// to make the solution work.
  llvm::SmallVector<std::pair<Fix, ConstraintLocator *>, 4> Fixes;

  /// The set of disjunction choices used to arrive at this solution,
  /// which informs constraint application.
  llvm::SmallDenseMap<ConstraintLocator *, unsigned> DisjunctionChoices;

  /// The set of opened types for a given locator.
  llvm::SmallDenseMap<ConstraintLocator *, ArrayRef<OpenedType>> OpenedTypes;

  /// The opened existential type for a given locator.
  llvm::SmallDenseMap<ConstraintLocator *, ArchetypeType *>
    OpenedExistentialTypes;

  /// The type variables that were bound via a Defaultable constraint.
  llvm::SmallPtrSet<TypeVariableType *, 8> DefaultedTypeVariables;

  /// \brief Simplify the given type by substituting all occurrences of
  /// type variables for their fixed types.
  Type simplifyType(TypeChecker &tc, Type type) const;

  /// \brief Coerce the given expression to the given type.
  ///
  /// This operation cannot fail.
  ///
  /// \param expr The expression to coerce.
  /// \param toType The type to coerce the expression to.
  /// \param locator Locator used to describe the location of this expression.
  ///
  /// \param ignoreTopLevelInjection Whether to suppress diagnostics
  /// on a suspicious top-level optional injection (because the caller already
  /// diagnosed it).
  ///
  /// \param typeFromPattern Optionally, the caller can specify the pattern
  /// from where the toType is derived, so that we can deliver better fixit.
  ///
  /// \returns the coerced expression, which will have type \c ToType.
  Expr *coerceToType(Expr *expr, Type toType,
                     ConstraintLocator *locator,
                     bool ignoreTopLevelInjection = false,
                     Optional<Pattern*> typeFromPattern = None) const;

  /// \brief Convert the given expression to a logic value.
  ///
  /// This operation cannot fail.
  ///
  /// \param expr The expression to coerce. The type of this expression
  /// must conform to the LogicValue protocol.
  ///
  /// \param locator Locator used to describe the location of this expression.
  ///
  /// \returns the expression converted to a logic value (Builtin i1).
  Expr *convertBooleanTypeToBuiltinI1(Expr *expr,
                                      ConstraintLocator *locator) const;

  /// \brief Convert the given optional-producing expression to a Bool
  /// indicating whether the optional has a value.
  ///
  /// This operation cannot fail.
  ///
  /// \param expr The expression to coerce. The type of this expression
  /// must be T?.
  ///
  /// \param locator Locator used to describe the location of this expression.
  ///
  /// \returns a Bool expression indicating whether the optional
  /// contains a value.
  Expr *convertOptionalToBool(Expr *expr, ConstraintLocator *locator) const;

  /// Compute the set of substitutions required to map the given type
  /// to the provided "opened" type.
  ///
  /// Either the generic type (\c origType) must be a \c GenericFunctionType,
  /// in which case it's generic requirements will be used to compute the
  /// required substitutions, or \c dc must be a generic context, in which
  /// case it's generic requirements will be used.
  ///
  /// \param origType The generic type.
  ///
  /// \param openedType The type to which this reference to the given
  /// generic function type was opened.
  ///
  /// \param dc The declaration context that owns the generic type
  ///
  /// \param locator The locator that describes where the substitutions came
  /// from.
  ///
  /// \param substitutions Will be populated with the set of substitutions
  /// to be applied to the generic function type.
  ///
  /// \returns The opened type after applying the computed substitutions.
  Type computeSubstitutions(Type origType,
                            DeclContext *dc,
                            Type openedType,
                            ConstraintLocator *locator,
                            SmallVectorImpl<Substitution> &substitutions) const;

  /// Return the disjunction choice for the given constraint location.
  unsigned getDisjunctionChoice(ConstraintLocator *locator) const {
    assert(DisjunctionChoices.count(locator));
    return DisjunctionChoices.find(locator)->second;
  }

  /// \brief Retrieve the fixed score of this solution
  const Score &getFixedScore() const { return FixedScore; }

  /// \brief Retrieve the fixed score of this solution
  Score &getFixedScore() { return FixedScore; }

  /// \brief Retrieve the fixed type for the given type variable.
  Type getFixedType(TypeVariableType *typeVar) const;

  /// Try to resolve the given locator to a declaration within this
  /// solution.
  ConcreteDeclRef resolveLocatorToDecl(ConstraintLocator *locator) const;

  LLVM_ATTRIBUTE_DEPRECATED(
      void dump() const LLVM_ATTRIBUTE_USED,
      "only for use within the debugger");

  /// \brief Dump this solution.
  void dump(raw_ostream &OS) const LLVM_ATTRIBUTE_USED;
};

/// \brief Describes the differences between several solutions to the same
/// constraint system.
class SolutionDiff {
public:
  /// \brief A difference between two overloads.
  struct OverloadDiff {
    /// \brief The locator that describes where the overload comes from.
    ConstraintLocator *locator;

    /// \brief The choices that each solution made.
    SmallVector<OverloadChoice, 2> choices;
  };

  /// \brief A difference between two type variable bindings.
  struct TypeBindingDiff {
    /// \brief The type variable.
    TypeVariableType *typeVar;

    /// \brief The bindings that each solution made.
    SmallVector<Type, 2> bindings;
  };

  /// \brief The differences between the overload choices between the
  /// solutions.
  SmallVector<OverloadDiff, 4> overloads;

  /// \brief The differences between the type variable bindings of the
  /// solutions.
  SmallVector<TypeBindingDiff, 4> typeBindings;

  /// \brief Compute the differences between the given set of solutions.
  ///
  /// \param solutions The set of solutions.
  explicit SolutionDiff(ArrayRef<Solution> solutions);
};

/// Describes one resolved overload set within the list of overload sets
/// resolved by the solver.
struct ResolvedOverloadSetListItem {
  /// The previously resolved overload set in the list.
  ResolvedOverloadSetListItem *Previous;

  /// The type that this overload binds.
  Type BoundType;

  /// The overload choice.
  OverloadChoice Choice;

  /// The locator for this choice.
  ConstraintLocator *Locator;

  /// The type of the fully-opened base, if any.
  Type OpenedFullType;

  /// The type of the referenced choice.
  Type ImpliedType;

  // Make vanilla new/delete illegal for overload set items.
  void *operator new(size_t Bytes) = delete;
  void operator delete(void *Data) = delete;

  // Only allow allocation of list items using the allocator in the
  // constraint system.
  void *operator new(size_t bytes, ConstraintSystem &cs,
                     unsigned alignment
                       = alignof(ResolvedOverloadSetListItem));
};
  


/// Identifies a specific conversion from
struct SpecificConstraint {
  CanType First;
  CanType Second;
  ConstraintKind Kind;
};

/// An intrusive, doubly-linked list of constraints.
typedef llvm::ilist<Constraint> ConstraintList;

enum class ConstraintSystemFlags {
  /// Whether we allow the solver to attempt fixes to the system.
  AllowFixes = 0x01,
  
  /// Set if the client prefers fixits to be in the form of force unwrapping
  /// or optional chaining to return an optional.
  PreferForceUnwrapToOptional = 0x02,
};

/// Options that affect the constraint system as a whole.
typedef OptionSet<ConstraintSystemFlags> ConstraintSystemOptions;

/// This struct represents the results of a member lookup of
struct MemberLookupResult {
  enum {
    /// This result indicates that we cannot begin to solve this, because the
    /// base expression is a type variable.
    Unsolved,
    
    /// This result indicates that the member reference is erroneous, but was
    /// already diagnosed.  Don't emit another error.
    ErrorAlreadyDiagnosed,
    
    /// This result indicates that the lookup produced candidate lists,
    /// potentially of viable results, potentially of error candidates, and
    /// potentially empty lists, indicating that there were no matches.
    HasResults
  } OverallResult;
  
  /// This is a list of viable candidates that were matched.
  ///
  SmallVector<OverloadChoice, 4> ViableCandidates;
  
  /// If there is a favored candidate in the viable list, this indicates its
  /// index.
  unsigned FavoredChoice = ~0U;
  
  
  /// This enum tracks reasons why a candidate is not viable.
  enum UnviableReason {
    /// Argument labels don't match.
    UR_LabelMismatch,
    
    /// This uses a type like Self in its signature that cannot be used on an
    /// existential box.
    UR_UnavailableInExistential,
    
    /// This is an instance member being accessed through something of metatype
    /// type.
    UR_InstanceMemberOnType,
    
    /// This is a static/class member being accessed through an instance.
    UR_TypeMemberOnInstance,
    
    /// This is a mutating member, being used on an rvalue.
    UR_MutatingMemberOnRValue,
    
    /// The getter for this subscript or computed property is mutating and we
    /// only have an rvalue base.  This is more specific than the former one.
    UR_MutatingGetterOnRValue,
    
    /// The member is inaccessible (e.g. a private member in another file).
    UR_Inaccessible,
  };
  
  /// This is a list of considered, but rejected, candidates, along with a
  /// reason for their rejection.
  SmallVector<std::pair<ValueDecl*, UnviableReason>, 4> UnviableCandidates;


  /// Mark this as being an already-diagnosed error and return itself.
  MemberLookupResult &markErrorAlreadyDiagnosed() {
    OverallResult = ErrorAlreadyDiagnosed;
    return *this;
  }
  
  void addViable(OverloadChoice candidate) {
    ViableCandidates.push_back(candidate);
  }
  
  void addUnviable(ValueDecl *VD, UnviableReason reason) {
    UnviableCandidates.push_back({VD, reason});
  }
  
  OverloadChoice *getFavoredChoice() {
    if (FavoredChoice == ~0U) return nullptr;
    return &ViableCandidates[FavoredChoice];
  }
  
};
  
  
/// \brief Describes a system of constraints on type variables, the
/// solution of which assigns concrete types to each of the type variables.
/// Constraint systems are typically generated given an (untyped) expression.
class ConstraintSystem {
public:
  TypeChecker &TC;
  DeclContext *DC;
  ConstraintSystemOptions Options;
  
  friend class Fix;
  friend class OverloadChoice;
  friend class ConstraintGraph;

  class SolverScope;

  Constraint *failedConstraint = nullptr;

  /// Expressions that are known to be unevaluated.
  /// Note: this is only used to support ObjCSelectorExpr at the moment.
  llvm::SmallPtrSet<Expr *, 2> UnevaluatedRootExprs;

  /// The original CS if this CS was created as a simplification of another CS
  ConstraintSystem *baseCS = nullptr;

private:

  /// \brief Allocator used for all of the related constraint systems.
  llvm::BumpPtrAllocator Allocator;

  /// \brief Arena used for memory management of constraint-checker-related
  /// allocations.
  ConstraintCheckerArenaRAII Arena;

  /// \brief Counter for type variables introduced.
  unsigned TypeCounter = 0;
  
  /// \brief The expression being solved has exceeded the solver's memory
  /// threshold.
  bool expressionExceededThreshold = false;

  /// \brief Cached member lookups.
  llvm::DenseMap<std::pair<Type, DeclName>, Optional<LookupResult>>
    MemberLookups;

  /// Cached sets of "alternative" literal types.
  Optional<ArrayRef<Type>> AlternativeLiteralTypes[12];

  /// \brief Folding set containing all of the locators used in this
  /// constraint system.
  llvm::FoldingSetVector<ConstraintLocator> ConstraintLocators;

  /// \brief The overload sets that have been resolved along the current path.
  ResolvedOverloadSetListItem *resolvedOverloadSets = nullptr;

  /// The current fixed score for this constraint system and the (partial)
  /// solution it represents.
  Score CurrentScore;

  /// Whether this constraint system is processing a favored
  /// constraint.
  bool HandlingFavoredConstraint = false;

  SmallVector<TypeVariableType *, 16> TypeVariables;

  /// Maps expressions to types for choosing a favored overload
  /// type in a disjunction constraint.
  llvm::DenseMap<Expr *, TypeBase *> FavoredTypes;

  /// There can only be a single contextual type on the root of the expression
  /// being checked.  If specified, this holds its type along with the base
  /// expression, and the purpose of it.
  TypeLoc contextualType;
  Expr *contextualTypeNode = nullptr;
  ContextualTypePurpose contextualTypePurpose = CTP_Unused;
  
  /// \brief The set of constraint restrictions used to reach the
  /// current constraint system.
  ///
  /// Constraint restrictions help describe which path the solver took when
  /// there are multiple ways in which one type could convert to another, e.g.,
  /// given class types A and B, the solver might choose either a superclass
  /// conversion or a user-defined conversion.
  SmallVector<std::tuple<Type, Type, ConversionRestrictionKind>, 32>
      ConstraintRestrictions;

  /// The set of fixes applied to make the solution work.
  ///
  /// Each fix is paired with a locator that describes where the fix occurs.
  SmallVector<std::pair<Fix, ConstraintLocator *>, 4> Fixes;

  /// Types used in fixes.
  std::vector<Type> FixedTypes;

  /// \brief The set of remembered disjunction choices used to reach
  /// the current constraint system.
  SmallVector<std::pair<ConstraintLocator*, unsigned>, 32>
      DisjunctionChoices;

  /// The worklist of "active" constraints that should be revisited
  /// due to a change.
  ConstraintList ActiveConstraints;

  /// The list of "inactive" constraints that still need to be solved,
  /// but will not be revisited until one of their inputs changes.
  ConstraintList InactiveConstraints;

  /// The constraint graph.
  ConstraintGraph &CG;

  /// A mapping from constraint locators to the set of opened types associated
  /// with that locator.
  SmallVector<std::pair<ConstraintLocator *, ArrayRef<OpenedType>>, 4>
    OpenedTypes;

  /// A mapping from constraint locators to the opened existential archetype
  /// used for the 'self' of an existential type.
  SmallVector<std::pair<ConstraintLocator *, ArchetypeType *>, 4>
    OpenedExistentialTypes;

public:
  /// The type variables that were bound via a Defaultable constraint.
  SmallVector<TypeVariableType *, 8> DefaultedTypeVariables;

  /// The type variable used to describe the element type of the given array
  /// literal.
  llvm::SmallDenseMap<ArrayExpr *, TypeVariableType *>
    ArrayElementTypeVariables;


  /// The type variables used to describe the key and value types of the given
  /// dictionary literal.
  llvm::SmallDenseMap<DictionaryExpr *,
                      std::pair<TypeVariableType *, TypeVariableType *>>
    DictionaryElementTypeVariables;

private:
  /// \brief Describes the current solver state.
  struct SolverState {
    SolverState(ConstraintSystem &cs);
    ~SolverState();

    /// The constraint system.
    ConstraintSystem &CS;

    /// Old value of DebugConstraintSolver.
    /// FIXME: Move the "debug constraint solver" bit into the constraint 
    /// system itself.
    bool OldDebugConstraintSolver;

    /// \brief Depth of the solution stack.
    unsigned depth = 0;

    /// \brief Whether to record failures or not.
    bool recordFixes = false;

    /// The list of constraints that have been retired along the
    /// current path.
    ConstraintList retiredConstraints;

    /// The current set of generated constraints.
    SmallVector<Constraint *, 4> generatedConstraints;

    /// \brief The set of type variable bindings that have changed while
    /// processing this constraint system.
    SavedTypeVariableBindings savedBindings;

     /// The best solution computed so far.
    Optional<Score> BestScore;

    /// The number of the solution attempt we're looking at.
    unsigned SolutionAttempt;

    /// Refers to the innermost partial solution scope.
    SolverScope *PartialSolutionScope = nullptr;

    // Statistics
    #define CS_STATISTIC(Name, Description) unsigned Name = 0;
    #include "ConstraintSolverStats.def"
  };

public:
  /// \brief The current solver state.
  ///
  /// This will be non-null when we're actively solving the constraint
  /// system, and carries temporary state related to the current path
  /// we're exploring. 
  SolverState *solverState = nullptr;

  struct ArgumentLabelState {
    ArrayRef<Identifier> Labels;
    bool HasTrailingClosure;
  };

  /// A mapping from the constraint locators for references to various
  /// names (e.g., member references, normal name references, possible
  /// constructions) to the argument labels provided in the call to
  /// that locator.
  llvm::DenseMap<ConstraintLocator *, ArgumentLabelState> ArgumentLabels;

  /// FIXME: This is a workaround for the way we perform protocol
  /// conformance checking for generic requirements, where we re-use
  /// the archetypes of the requirement (rather than, say, building
  /// new archetypes) to match up with the witness. Those archetypes
  /// can end up with references to the outer archetypes (of the
  /// associated types), which need to be replaced.
  TypeVariableType *SelfTypeVar = nullptr;

  /// \brief The set of additional attributes inferred for a FunctionType.  Note
  /// that this is not state kept as part of SolverState, so it only supports
  /// function attributes that need to be set invariant of the actual typing of
  /// the solution.
  llvm::SmallDenseMap<FunctionType*, FunctionType::ExtInfo> extraFunctionAttrs;

  ResolvedOverloadSetListItem *getResolvedOverloadSets() const {
    return resolvedOverloadSets;
  }

private:
  unsigned assignTypeVariableID() {
    return TypeCounter++;
  }

public:
  /// \brief Introduces a new solver scope, which any changes to the
  /// solver state or constraint system are temporary and will be undone when
  /// this object is destroyed.
  ///
  ///
  class SolverScope {
    ConstraintSystem &cs;

    /// \brief The current resolved overload set list.
    ResolvedOverloadSetListItem *resolvedOverloadSets;

    /// \brief The length of \c TypeVariables.
    unsigned numTypeVariables;

    /// \brief The length of \c SavedBindings.
    unsigned numSavedBindings;

    /// The length of generatedConstraints.
    unsigned numGeneratedConstraints;

    /// \brief The last retired constraint in the list.
    ConstraintList::iterator firstRetired;

    /// \brief The length of \c ConstraintRestrictions.
    unsigned numConstraintRestrictions;

    /// \brief The length of \c Fixes.
    unsigned numFixes;

    /// \brief The length of \c DisjunctionChoices.
    unsigned numDisjunctionChoices;

    /// The length of \c OpenedTypes.
    unsigned numOpenedTypes;

    /// The length of \c OpenedExistentialTypes.
    unsigned numOpenedExistentialTypes;

    /// The length of \c DefaultedTypeVariables.
    unsigned numDefaultedTypeVariables;

    /// The previous score.
    Score PreviousScore;

    /// Constraint graph scope associated with this solver scope.
    ConstraintGraphScope CGScope;

    SolverScope(const SolverScope &) = delete;
    SolverScope &operator=(const SolverScope &) = delete;

    friend class ConstraintSystem;

  public:
    explicit SolverScope(ConstraintSystem &cs);
    ~SolverScope();
  };

  ConstraintSystem(TypeChecker &tc, DeclContext *dc,
                   ConstraintSystemOptions options);
  ~ConstraintSystem();

  /// Retrieve the type checker associated with this constraint system.
  TypeChecker &getTypeChecker() const { return TC; }

  /// Retrieve the constraint graph associated with this constraint system.
  ConstraintGraph &getConstraintGraph() const { return CG; }

  /// \brief Retrieve the AST context.
  ASTContext &getASTContext() const { return TC.Context; }

  /// \brief Determine whether this constraint system has any free type
  /// variables.
  bool hasFreeTypeVariables();

private:
  /// \brief Finalize this constraint system; we're done attempting to solve
  /// it.
  ///
  /// \returns the solution.
  Solution finalize(FreeTypeVariableBinding allowFreeTypeVariables);

  /// \brief Apply the given solution to the current constraint system.
  ///
  /// This operation is used to take a solution computed based on some
  /// subset of the constraints and then apply it back to the
  /// constraint system for further exploration.
  void applySolution(const Solution &solution);

  /// Emit the fixes computed as part of the solution, returning true if we were
  /// able to emit an error message, or false if none of the fixits worked out.
  bool applySolutionFixes(Expr *E, const Solution &solution);

  /// \brief Apply the specified Fix # to this solution, producing a fixit hint
  /// diagnostic for it and returning true.  If the fixit hint turned out to be
  /// bogus, this returns false and doesn't emit anything.
  bool applySolutionFix(Expr *expr, const Solution &solution, unsigned fixNo);
  
  
  /// \brief Restore the type variable bindings to what they were before
  /// we attempted to solve this constraint system.
  ///
  /// \param numBindings The number of bindings to restore, from the end of
  /// the saved-binding stack.
  void restoreTypeVariableBindings(unsigned numBindings);

  /// \brief Retrieve the set of saved type variable bindings, if available.
  ///
  /// \returns null when we aren't currently solving the system.
  SavedTypeVariableBindings *getSavedBindings() const {
    return solverState? &solverState->savedBindings : nullptr;
  }

  /// Add a new type variable that was already created.
  void addTypeVariable(TypeVariableType *typeVar);

public:
  /// \brief Lookup for a member with the given name in the given base type.
  ///
  /// This routine caches the results of member lookups in the top constraint
  /// system, to avoid.
  ///
  /// FIXME: This caching should almost certainly be performed at the
  /// module level, since type checking occurs after name binding,
  /// and no new names are introduced after name binding.
  ///
  /// \returns A reference to the member-lookup result.
  LookupResult &lookupMember(Type base, DeclName name);

  /// Retrieve the set of "alternative" literal types that we'll explore
  /// for a given literal protocol kind.
  ArrayRef<Type> getAlternativeLiteralTypes(KnownProtocolKind kind);

  /// \brief Create a new type variable.
  TypeVariableType *createTypeVariable(ConstraintLocator *locator,
                                       unsigned options) {
    auto tv = TypeVariableType::getNew(TC.Context, assignTypeVariableID(),
                                       locator, options);
    addTypeVariable(tv);
    return tv;
  }

  /// Retrieve the set of active type variables.
  ArrayRef<TypeVariableType *> getTypeVariables() const {
    return TypeVariables;
  }
  
  TypeBase* getFavoredType(Expr *E) {
    return this->FavoredTypes[E];
  }
  void setFavoredType(Expr *E, TypeBase *T) {
    this->FavoredTypes[E] = T;
  }
 
  void setContextualType(Expr *E, TypeLoc T, ContextualTypePurpose purpose) {
    contextualTypeNode = E;
    contextualType = T;
    contextualTypePurpose = purpose;
  }

  Type getContextualType(Expr *E) const {
    return E == contextualTypeNode ? contextualType.getType() : Type();
  }
  Type getContextualType() const {
    return contextualType.getType();
  }

  TypeLoc getContextualTypeLoc() const {
    return contextualType;
  }

  const Expr *getContextualTypeNode() const {
    return contextualTypeNode;
  }

  ContextualTypePurpose getContextualTypePurpose() const {
    return contextualTypePurpose;
  }
  
  /// \brief Retrieve the constraint locator for the given anchor and
  /// path, uniqued.
  ConstraintLocator *
  getConstraintLocator(Expr *anchor,
                       ArrayRef<ConstraintLocator::PathElement> path,
                       unsigned summaryFlags);

  /// \brief Retrieve the constraint locator for the given anchor and
  /// an empty path, uniqued.
  ConstraintLocator *getConstraintLocator(Expr *anchor) {
    return getConstraintLocator(anchor, {}, 0);
  }

  /// \brief Retrieve the constraint locator for the given anchor and
  /// path element.
  ConstraintLocator *
  getConstraintLocator(Expr *anchor, ConstraintLocator::PathElement pathElt) {
    return getConstraintLocator(anchor, llvm::makeArrayRef(pathElt),
                                pathElt.getNewSummaryFlags());
  }

  /// \brief Extend the given constraint locator with a path element.
  ConstraintLocator *
  getConstraintLocator(ConstraintLocator *locator,
                       ConstraintLocator::PathElement pathElt) {
    return getConstraintLocator(ConstraintLocatorBuilder(locator)
                                  .withPathElement(pathElt));
  }

  /// \brief Retrieve the constraint locator described by the given
  /// builder.
  ConstraintLocator *
  getConstraintLocator(const ConstraintLocatorBuilder &builder);

public:

  /// \brief Whether we should attempt to fix problems.
  bool shouldAttemptFixes() {
    if (!(Options & ConstraintSystemFlags::AllowFixes))
      return false;

    return !solverState || solverState->recordFixes;
  }

  /// \brief Log and record the application of the fix. Return true iff any
  /// subsequent solution would be worse than the best known solution.
  bool recordFix(Fix fix, ConstraintLocatorBuilder locator);
  
  /// \brief Try to salvage the constraint system by applying (speculative)
  /// fixes to the underlying expression.
  ///
  /// \param viable the set of viable solutions produced by the initial
  /// solution attempt.
  ///
  /// \param expr the expression we're trying to salvage.
  ///
  /// \returns false if we were able to salvage the system, in which case
  /// \c viable[0] contains the resulting solution. Otherwise, emits a
  /// diagnostic and returns true.
  bool salvage(SmallVectorImpl<Solution> &viable, Expr *expr);

  /// When an assignment to an expression is detected and the destination is
  /// invalid, emit a detailed error about the condition.
  void diagnoseAssignmentFailure(Expr *dest, Type destTy, SourceLoc equalLoc);

  
  /// \brief Mine the active and inactive constraints in the constraint
  /// system to generate a plausible diagnosis of why the system could not be
  /// solved.
  ///
  /// \param expr The expression whose constraints we're investigating for a
  /// better diagnostic.
  ///
  /// Assuming that this constraint system is actually erroneous, this *always*
  /// emits an error message.
  void diagnoseFailureForExpr(Expr *expr);

  /// \brief Add a newly-allocated constraint after attempting to simplify
  /// it.
  ///
  /// \param isExternallySolved Whether this constraint is being solved
  /// as an eager simplification, outside of the simplify() loop.
  ///
  /// \param simplifyExisting Whether we're simplifying an existing
  /// constraint rather than introducing a new constraint.
  ///
  /// \returns true if this constraint was solved.
  bool addConstraint(Constraint *constraint,
                     bool isExternallySolved = false,
                     bool simplifyExisting = false);

  /// \brief Add a constraint to the constraint system.
  void addConstraint(ConstraintKind kind, Type first, Type second,
                     ConstraintLocator *locator, bool isFavored = false) {
    assert(first && "Missing first type");
    assert(second && "Missing second type");
    auto c = Constraint::create(*this, kind, first, second, DeclName(),
                                FunctionRefKind::Compound, locator);
    if (isFavored) c->setFavored();
    addConstraint(c);
  }

  /// Add a constraint that binds an overload set to a specific choice.
  void addBindOverloadConstraint(Type boundTy, OverloadChoice choice,
                                 ConstraintLocator *locator) {
    addConstraint(Constraint::createBindOverload(*this, boundTy, choice, 
                                                 locator));
  }

  /// \brief Add a value member constraint to the constraint system.
  void addValueMemberConstraint(Type baseTy, DeclName name, Type memberTy,
                                FunctionRefKind functionRefKind,
                                ConstraintLocator *locator) {
    assert(baseTy);
    assert(memberTy);
    assert(name);
    addConstraint(Constraint::create(*this, ConstraintKind::ValueMember,
                                     baseTy, memberTy, name, functionRefKind,
                                     locator));
  }

  /// \brief Add a value member constraint for an UnresolvedMemberRef
  /// to the constraint system.
  void addUnresolvedValueMemberConstraint(Type baseTy, DeclName name,
                                          Type memberTy,
                                          FunctionRefKind functionRefKind,
                                          ConstraintLocator *locator) {
    assert(baseTy);
    assert(memberTy);
    assert(name);
    addConstraint(Constraint::create(*this,
                                     ConstraintKind::UnresolvedValueMember,
                                     baseTy, memberTy, name, functionRefKind,
                                     locator));
  }

  /// \brief Add an archetype constraint.
  void addArchetypeConstraint(Type baseTy, ConstraintLocator *locator) {
    assert(baseTy);
    addConstraint(Constraint::create(*this, ConstraintKind::Archetype,
                                     baseTy, Type(), DeclName(),
                                     FunctionRefKind::Compound, locator));
  }
  
  /// \brief Remove an inactive constraint from the current constraint graph.
  void removeInactiveConstraint(Constraint *constraint) {
    CG.removeConstraint(constraint);
    InactiveConstraints.erase(constraint);
  }

  /// Retrieve the type that corresponds to the given member of the
  /// given base type, which may be a newly-created type variable.
  ///
  /// \param baseTypeVar The base type variable whose member is being queried.
  ///
  /// \param assocType The associated type we're referencing.
  ///
  /// \param locator The location used to describe this member access.
  ///
  /// \param options Options to be supplied to type variable creation if 
  /// a new type is created.
  ///
  /// \returns the type variable representing the member type.
  TypeVariableType *getMemberType(TypeVariableType *baseTypeVar, 
                                  AssociatedTypeDecl *assocType,
                                  ConstraintLocatorBuilder locator,
                                  unsigned options);

  /// Retrieve the list of inactive constraints.
  ConstraintList &getConstraints() { return InactiveConstraints; }

  /// The worklist of "active" constraints that should be revisited
  /// due to a change.
  ConstraintList &getActiveConstraints() { return ActiveConstraints; }

  /// \brief Retrieve the representative of the equivalence class containing
  /// this type variable.
  TypeVariableType *getRepresentative(TypeVariableType *typeVar) {
    return typeVar->getImpl().getRepresentative(getSavedBindings());
  }

  /// \brief Merge the equivalence sets of the two type variables.
  ///
  /// Note that both \c typeVar1 and \c typeVar2 must be the
  /// representatives of their equivalence classes, and must be
  /// distinct.
  void mergeEquivalenceClasses(TypeVariableType *typeVar1,
                               TypeVariableType *typeVar2,
                               bool updateWorkList = true);

  /// \brief Retrieve the fixed type corresponding to the given type variable,
  /// or a null type if there is no fixed type.
  Type getFixedType(TypeVariableType *typeVar) {
    return typeVar->getImpl().getFixedType(getSavedBindings());
  }

  /// Retrieve the fixed type corresponding to a given type variable,
  /// recursively, until we hit something that isn't a type variable
  /// or a type variable that doesn't have a fixed type.
  ///
  /// \param type The type to simplify.
  ///
  /// \param typeVar Will receive the type variable at which simplification 
  /// stopped, which has no fixed type.
  ///
  /// \param wantRValue Whether this routine should look through
  /// lvalues at each step.
  ///
  /// param retainParens Whether to retain parentheses.
  Type getFixedTypeRecursive(Type type, TypeVariableType *&typeVar,
                             bool wantRValue,
                             bool retainParens = false);

  /// \brief Assign a fixed type to the given type variable.
  ///
  /// \param typeVar The type variable to bind.
  ///
  /// \param type The fixed type to which the type variable will be bound.
  ///
  /// \param updateState Whether to update the state based on this binding.
  /// False when we're only assigning a type as part of reconstructing 
  /// a complete solution from partial solutions.
  void assignFixedType(TypeVariableType *typeVar, Type type,
                       bool updateState = true);

  // \brief Set the TVO_MustBeMaterializable bit on all type variables
  // necessary to ensure that the type in question is materializable in a
  // viable solution.
  void setMustBeMaterializableRecursive(Type type);
  
  /// \brief Determine if the type in question is an Array<T>.
  bool isArrayType(Type t);

  /// Determine whether the given type is a dictionary and, if so, provide the
  /// key and value types for the dictionary.
  Optional<std::pair<Type, Type>> isDictionaryType(Type type);

  /// \brief Determine if the type in question is a Set<T>.
  bool isSetType(Type t);

  /// \brief Determine if the type in question is AnyHashable.
  bool isAnyHashableType(Type t);

private:
  /// Introduce the constraints associated with the given type variable
  /// into the worklist.
  void addTypeVariableConstraintsToWorkList(TypeVariableType *typeVar);

public:

  /// \brief "Open" the given type by replacing any occurrences of generic
  /// parameter types and dependent member types with fresh type variables.
  ///
  /// \param type The type to open.
  ///
  /// \returns The opened type.
  Type openType(Type type, ConstraintLocatorBuilder locator) {
    llvm::DenseMap<CanType, TypeVariableType *> replacements;
    return openType(type, locator, replacements);
  }

  /// \brief "Open" the given type by replacing any occurrences of generic
  /// parameter types and dependent member types with fresh type variables.
  ///
  /// \param type The type to open.
  ///
  /// \param replacements The mapping from opened types to the type
  /// variables to which they were opened.
  ///
  /// \returns The opened type, or \c type if there are no archetypes in it.
  Type openType(Type type,
                ConstraintLocatorBuilder locator,
                llvm::DenseMap<CanType, TypeVariableType *> &replacements);

  /// \brief "Open" the given function type.
  ///
  /// If the function type is non-generic, this is equivalent to calling
  /// openType(). Otherwise, it calls openGeneric() on the generic
  /// function's signature first.
  ///
  /// \param funcType The function type to open.
  ///
  /// \param replacements The mapping from opened types to the type
  /// variables to which they were opened.
  ///
  /// \param innerDC The generic context from which the type originates.
  ///
  /// \param outerDC The generic context containing the declaration.
  ///
  /// \param skipProtocolSelfConstraint Whether to skip the constraint on a
  /// protocol's 'Self' type.
  ///
  /// \returns The opened type, or \c type if there are no archetypes in it.
  Type openFunctionType(
      AnyFunctionType *funcType,
      unsigned numArgumentLabelsToRemove,
      ConstraintLocatorBuilder locator,
      llvm::DenseMap<CanType, TypeVariableType *> &replacements,
      DeclContext *innerDC,
      DeclContext *outerDC,
      bool skipProtocolSelfConstraint);

  /// \brief "Open" the given binding type by replacing any occurrences of
  /// archetypes (including those implicit in unbound generic types) with
  /// fresh type variables.
  ///
  /// This variant of \c openType() tweaks the result from \c openType() to
  /// prefer arrays to slices.
  /// FIXME: This is a bit of a hack.
  ///
  /// \param type The type to open.
  /// \returns The opened type, or \c type if there are no archetypes in it.
  Type openBindingType(Type type, ConstraintLocatorBuilder locator);

  /// Open the generic parameter list and its requirements, creating
  /// type variables for each of the type parameters.
  void openGeneric(DeclContext *innerDC,
                   DeclContext *outerDC,
                   GenericSignature *signature,
                   bool skipProtocolSelfConstraint,
                   ConstraintLocatorBuilder locator,
                   llvm::DenseMap<CanType, TypeVariableType *> &replacements);
  void openGeneric(DeclContext *innerDC,
                   DeclContext *outerDC,
                   ArrayRef<GenericTypeParamType *> params,
                   ArrayRef<Requirement> requirements,
                   bool skipProtocolSelfConstraint,
                   ConstraintLocatorBuilder locator,
                   llvm::DenseMap<CanType, TypeVariableType *> &replacements);

  /// Record the set of opened types for the given locator.
  void recordOpenedTypes(
         ConstraintLocatorBuilder locator,
         const llvm::DenseMap<CanType, TypeVariableType *> &replacements);

  /// \brief Retrieve the type of a reference to the given value declaration.
  ///
  /// For references to polymorphic function types, this routine "opens up"
  /// the type by replacing each instance of an archetype with a fresh type
  /// variable.
  ///
  /// \param decl The declarations whose type is being computed.
  ///
  /// \param isTypeReference Whether it's a reference to this declaration
  /// as a type.
  ///
  /// \param isSpecialized Whether this declaration is immediately specialized.
  ///
  /// \returns a pair containing the full opened type (if applicable) and
  /// opened type of a reference to declaration.
  std::pair<Type, Type> getTypeOfReference(
                          ValueDecl *decl,
                          bool isTypeReference,
                          bool isSpecialized,
                          FunctionRefKind functionRefKind,
                          ConstraintLocatorBuilder locator,
                          const DeclRefExpr *base = nullptr);

  /// Replace the 'Self' type in the archetype with the appropriate
  /// type variable, if needed.
  /// FIXME: See the documentation of SelfTypeVar.
  Type replaceSelfTypeInArchetype(ArchetypeType *archetype);

  /// \brief Retrieve the type of a reference to the given value declaration,
  /// as a member with a base of the given type.
  ///
  /// For references to generic function types or members of generic types,
  /// this routine "opens up" the type by replacing each instance of a generic
  /// parameter with a fresh type variable.
  ///
  /// \param isTypeReference Indicates that we want to refer to the declared
  /// type of the type declaration rather than referring to it as a value.
  ///
  /// \param isDynamicResult Indicates that this declaration was found via
  /// dynamic lookup.
  ///
  /// \returns a pair containing the full opened type (which includes the opened
  /// base) and opened type of a reference to this member.
  std::pair<Type, Type> getTypeOfMemberReference(
                          Type baseTy, ValueDecl *decl,
                          bool isTypeReference,
                          bool isDynamicResult,
                          FunctionRefKind functionRefKind,
                          ConstraintLocatorBuilder locator,
                          const DeclRefExpr *base = nullptr,
                          llvm::DenseMap<CanType, TypeVariableType *>
                            *replacements = nullptr);

  /// \brief Add a new overload set to the list of unresolved overload
  /// sets.
  void addOverloadSet(Type boundType, ArrayRef<OverloadChoice> choices,
                      ConstraintLocator *locator,
                      OverloadChoice *favored = nullptr);

  /// If the given type is ImplicitlyUnwrappedOptional<T>, and we're in a context
  /// that should transparently look through ImplicitlyUnwrappedOptional types,
  /// return T.
  Type lookThroughImplicitlyUnwrappedOptionalType(Type type);

  /// \brief Retrieve the allocator used by this constraint system.
  llvm::BumpPtrAllocator &getAllocator() { return Allocator; }

  template <typename It>
  ArrayRef<typename std::iterator_traits<It>::value_type>
  allocateCopy(It start, It end) {
    typedef typename std::iterator_traits<It>::value_type T;
    T *result = (T*)getAllocator().Allocate(sizeof(T)*(end-start),
                                            __alignof__(T));
    unsigned i;
    for (i = 0; start != end; ++start, ++i)
      new (result+i) T(*start);
    return ArrayRef<T>(result, i);
  }

  template<typename T>
  ArrayRef<T> allocateCopy(ArrayRef<T> array) {
    return allocateCopy(array.begin(), array.end());
  }

  template<typename T>
  ArrayRef<T> allocateCopy(SmallVectorImpl<T> &vec) {
    return allocateCopy(vec.begin(), vec.end());
  }

  /// \brief Generate constraints for the given (unchecked) expression.
  ///
  /// \returns a possibly-sanitized expression, or null if an error occurred.
  Expr *generateConstraints(Expr *E);

  /// \brief Generate constraints for the given top-level expression,
  /// assuming that its children are already type-checked.
  ///
  /// \returns a possibly-sanitized expression, or null if an error occurred.
  Expr *generateConstraintsShallow(Expr *E);

  /// \brief Generate constraints for binding the given pattern to the
  /// value of the given expression.
  ///
  /// \returns a possibly-sanitized initializer, or null if an error occurred.
  Type generateConstraints(Pattern *P, ConstraintLocatorBuilder locator);

  /// \brief The result of attempting to resolve a constraint or set of
  /// constraints.
  enum class SolutionKind : char {
    /// \brief The constraint has been solved completely, and provides no
    /// more information.
    Solved,
    /// \brief The constraint could not be solved at this point.
    Unsolved,
    /// \brief The constraint uncovers an inconsistency in the system.
    Error
  };

  /// \brief Compute the rvalue type of the given expression, which is the
  /// destination of an assignment statement.
  Type computeAssignDestType(Expr *dest, SourceLoc equalLoc);

public:
  /// \brief Flags that direct type matching.
  enum TypeMatchFlags {
    TMF_None = 0,

    /// \brief Indicates that we are in a context where we should be
    /// generating constraints for any unsolvable problems.
    ///
    /// This flag is automatically introduced when type matching destructures
    /// a type constructor (tuple, function type, etc.), solving that
    /// constraint while potentially generating others.
    TMF_GenerateConstraints = 0x01,

    /// Indicates that we are applying a fix.
    TMF_ApplyingFix = 0x02,
    
    /// Indicates we're matching an operator parameter.
    TMF_ApplyingOperatorParameter = 0x4,
    
    /// Indicates we're unwrapping an optional type for a value-to-optional
    /// conversion.
    TMF_UnwrappingOptional = 0x8,
  };

private:
  /// \brief Subroutine of \c matchTypes(), which matches up two tuple types.
  ///
  /// \returns the result of performing the tuple-to-tuple conversion.
  SolutionKind matchTupleTypes(TupleType *tuple1, TupleType *tuple2,
                                         TypeMatchKind kind, unsigned flags,
                                         ConstraintLocatorBuilder locator);

  /// \brief Subroutine of \c matchTypes(), which matches a scalar type to
  /// a tuple type.
  ///
  /// \returns the result of performing the scalar-to-tuple conversion.
  SolutionKind matchScalarToTupleTypes(Type type1, TupleType *tuple2,
                                       TypeMatchKind kind, unsigned flags,
                                       ConstraintLocatorBuilder locator);

  /// \brief Subroutine of \c matchTypes(), which extracts a scalar value from
  /// a single-element tuple type.
  ///
  /// \returns the result of performing the tuple-to-scalar conversion.
  SolutionKind matchTupleToScalarTypes(TupleType *tuple1, Type type2,
                                       TypeMatchKind kind, unsigned flags,
                                       ConstraintLocatorBuilder locator);

  /// \brief Subroutine of \c matchTypes(), which matches up two function
  /// types.
  SolutionKind matchFunctionTypes(FunctionType *func1, FunctionType *func2,
                                  TypeMatchKind kind, unsigned flags,
                                  ConstraintLocatorBuilder locator);

  /// \brief Subroutine of \c matchTypes(), which matches up a value to a
  /// superclass.
  SolutionKind matchSuperclassTypes(Type type1, Type type2,
                                    TypeMatchKind kind, unsigned flags,
                                    ConstraintLocatorBuilder locator);

  /// \brief Subroutine of \c matchTypes(), which matches up two types that
  /// refer to the same declaration via their generic arguments.
  SolutionKind matchDeepEqualityTypes(Type type1, Type type2,
                                      ConstraintLocatorBuilder locator);

  /// \brief Subroutine of \c matchTypes(), which matches up a value to an
  /// existential type.
  ///
  /// \param kind Either ConstraintKind::SelfObjectOfProtocol or
  /// ConstraintKind::ConformsTo. Usually this uses SelfObjectOfProtocol,
  /// but when matching the instance type of a metatype with the instance type
  /// of an existential metatype, since we want an actual conformance check.
  SolutionKind matchExistentialTypes(Type type1, Type type2,
                                     ConstraintKind kind, unsigned flags,
                                     ConstraintLocatorBuilder locator);

public: // FIXME: public due to statics in CSSimplify.cpp
  /// \brief Attempt to match up types \c type1 and \c type2, which in effect
  /// is solving the given type constraint between these two types.
  ///
  /// \param type1 The first type, which is on the left of the type relation.
  ///
  /// \param type2 The second type, which is on the right of the type relation.
  ///
  /// \param kind The kind of type match being performed, e.g., exact match,
  /// trivial subtyping, subtyping, or conversion.
  ///
  /// \param flags A set of flags composed from the TMF_* constants, which
  /// indicates how
  ///
  /// \param locator The locator that will be used to track the location of
  /// the specific types being matched.
  ///
  /// \returns the result of attempting to solve this constraint.
  SolutionKind matchTypes(Type type1, Type type2, TypeMatchKind kind,
                          unsigned flags, ConstraintLocatorBuilder locator);

public:
  /// \brief Resolve the given overload set to the given choice.
  void resolveOverload(ConstraintLocator *locator, Type boundType,
                       OverloadChoice choice);

  /// \brief Simplify a type, by replacing type variables with either their
  /// fixed types (if available) or their representatives.
  ///
  /// The resulting types can be compared canonically, so long as additional
  /// type equivalence requirements aren't introduced between comparisons.
  Type simplifyType(Type type){
    llvm::SmallPtrSet<TypeVariableType *, 16> substituting;
   return simplifyType(type, substituting);
  }

  /// Given a ValueMember, UnresolvedValueMember, or TypeMember constraint,
  /// perform a lookup into the specified base type to find a candidate list.
  /// The list returned includes the viable candidates as well as the unviable
  /// ones (along with reasons why they aren't viable).
  ///
  /// If includeInaccessibleMembers is set to true, this burns compile time to
  /// try to identify and classify inaccessible members that may be being
  /// referenced.
  MemberLookupResult performMemberLookup(ConstraintKind constraintKind,
                                         DeclName memberName, Type baseTy,
                                         FunctionRefKind functionRefKind,
                                         ConstraintLocator *memberLocator,
                                         bool includeInaccessibleMembers);

private:

  /// \brief Simplify a type, by replacing type variables with either their
  /// fixed types (if available) or their representatives.
  ///
  /// \param type the type to be simplified.
  ///
  /// \param substituting the set of type variables that we're already
  /// substituting for. These type variables will not be substituted again,
  /// to avoid infinite recursion.
  ///
  /// The resulting types can be compared canonically, so long as additional
  /// type equivalence requirements aren't introduced between comparisons.
  Type simplifyType(Type type,
                    llvm::SmallPtrSet<TypeVariableType *, 16> &substituting);
  
  /// \brief Attempt to simplify the given construction constraint.
  ///
  /// \param valueType The type being constructed.
  ///
  /// \param fnType The argument type that will be the input to the
  /// valueType initializer and the result type will be the result of
  /// calling that initializer.
  ///
  /// \param flags Flags that indicate how the constraint should be
  /// simplified.
  /// 
  /// \param locator Locator describing where this construction
  /// occurred.
  SolutionKind simplifyConstructionConstraint(Type valueType, 
                                              FunctionType *fnType,
                                              unsigned flags,
                                              FunctionRefKind functionRefKind,
                                              ConstraintLocator *locator);

  /// \brief Attempt to simplify the given conformance constraint.
  ///
  /// \param type The type being testing.
  /// \param protocol The protocol to which the type should conform.
  /// \param kind Either ConstraintKind::SelfObjectOfProtocol or
  /// ConstraintKind::ConformsTo.
  /// \param locator Locator describing where this constraint occurred.
  SolutionKind simplifyConformsToConstraint(Type type, ProtocolDecl *protocol,
                                            ConstraintKind kind,
                                            ConstraintLocatorBuilder locator,
                                            unsigned flags);
  /// \brief Attempt to simplify the given conformance constraint.
  ///
  /// \param type The type being testing.
  /// \param protocol The protocol or protocol composition type to which the
  /// type should conform.
  /// \param locator Locator describing where this constraint occurred.
  ///
  /// \param kind If this is SelfTypeOfProtocol, we allow an existential type
  /// that contains the protocol but does not conform to it (eg, due to
  /// associated types).
  SolutionKind simplifyConformsToConstraint(Type type, Type protocol,
                                            ConstraintKind kind,
                                            ConstraintLocatorBuilder locator,
                                            unsigned flags);

  /// Attempt to simplify a checked-cast constraint.
  SolutionKind simplifyCheckedCastConstraint(Type fromType, Type toType,
                                             ConstraintLocatorBuilder locator);

  /// \brief Attempt to simplify the given member constraint.
  SolutionKind simplifyMemberConstraint(const Constraint &constraint);

  
  /// \brief Attempt to simplify the optional object constraint.
  SolutionKind simplifyOptionalObjectConstraint(const Constraint &constraint);

  /// \brief Attempt to simplify the ApplicableFunction constraint.
  SolutionKind simplifyApplicableFnConstraint(const Constraint &constraint);

  /// \brief Attempt to simplify the given DynamicTypeOf constraint.
  SolutionKind simplifyDynamicTypeOfConstraint(const Constraint &constraint);

  /// \brief Attempt to simplify the given archetype constraint.
  SolutionKind simplifyArchetypeConstraint(const Constraint &constraint);

  /// \brief Attempt to simplify the given class constraint.
  SolutionKind simplifyClassConstraint(const Constraint &constraint);
  
  /// \brief Attempt to simplify the given bridge constraint.
  SolutionKind simplifyBridgedToObjectiveCConstraint(const Constraint
                                                                &constraint);

  /// \brief Attempt to simplify the given defaultable constraint.
  SolutionKind simplifyDefaultableConstraint(const Constraint &c);

  /// \brief Simplify a conversion constraint by applying the given
  /// reduction rule, which is known to apply at the outermost level.
  SolutionKind simplifyRestrictedConstraint(ConversionRestrictionKind restriction,
                                            Type type1, Type type2,
                                            TypeMatchKind matchKind,
                                            unsigned flags,
                                            ConstraintLocatorBuilder locator);

public: // FIXME: Public for use by static functions.
  /// \brief Simplify a conversion constraint with a fix applied to it.
  SolutionKind simplifyFixConstraint(Fix fix,
                                     Type type1, Type type2,
                                     TypeMatchKind matchKind,
                                     unsigned flags,
                                     ConstraintLocatorBuilder locator);

public:
  /// \brief Simplify the system of constraints, by breaking down complex
  /// constraints into simpler constraints.
  ///
  /// The result of simplification is a constraint system that consisting of
  /// only simple constraints relating type variables to each other or
  /// directly to fixed types. There are no constraints that involve
  /// type constructors on both sides. the simplified constraint system may,
  /// of course, include type variables for which we have constraints but
  /// no fixed type. Such type variables are left to the solver to bind.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool simplify(bool ContinueAfterFailures = false);

  /// \brief Simplify the given constraint.
  SolutionKind simplifyConstraint(const Constraint &constraint);

private:
  /// \brief Solve the system of constraints after it has already been
  /// simplified.
  ///
  /// \param solutions The set of solutions to this system of constraints.
  ///
  /// \param allowFreeTypeVariables How to bind free type variables in
  /// the solution.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool solveSimplified(SmallVectorImpl<Solution> &solutions,
                       FreeTypeVariableBinding allowFreeTypeVariables);
 public:
  /// \brief Solve the system of constraints.
  ///
  /// \param solutions The set of solutions to this system of constraints.
  ///
  /// \param allowFreeTypeVariables How to bind free type variables in
  /// the solution.
  ///
  /// \returns true if an error occurred, false otherwise.  Note that multiple
  /// ambiguous solutions for the same constraint system are considered to be
  /// success by this API.
  bool solve(SmallVectorImpl<Solution> &solutions,
             FreeTypeVariableBinding allowFreeTypeVariables
               = FreeTypeVariableBinding::Disallow);

  /// \brief Solve the system of constraints.
  ///
  /// \param solutions The set of solutions to this system of constraints.
  ///
  /// \param allowFreeTypeVariables How to bind free type variables in
  /// the solution.
  ///
  /// \returns true if there are no solutions
  bool solveRec(SmallVectorImpl<Solution> &solutions,
             FreeTypeVariableBinding allowFreeTypeVariables);

  /// \brief Solve the system of constraints.
  ///
  /// \param allowFreeTypeVariables How to bind free type variables in
  /// the solution.
  ///
  /// \returns a solution if a single unambiguous one could be found, or None if
  /// ambiguous or unsolvable.
  Optional<Solution> solveSingle(FreeTypeVariableBinding allowFreeTypeVariables
                                    = FreeTypeVariableBinding::Disallow);

private:
  /// \brief Compare two solutions to the same set of constraints.
  ///
  /// \param cs The constraint system.
  /// \param solutions All of the solutions to the system.
  /// \param diff The differences among the solutions.
  /// \param idx1 The index of the first solution.
  /// \param idx2 The index of the second solution.
  static SolutionCompareResult compareSolutions(ConstraintSystem &cs,
                                                ArrayRef<Solution> solutions,
                                                const SolutionDiff &diff,
                                                unsigned idx1, unsigned idx2);

public:
  /// Increase the score of the given kind for the current (partial) solution
  /// along the.
  void increaseScore(ScoreKind kind);

  /// Determine whether this solution is guaranteed to be worse than the best
  /// solution found so far.
  bool worseThanBestSolution() const;

  /// \brief Given a set of viable solutions, find the best
  /// solution.
  ///
  /// \param solutions The set of viable solutions to consider.
  ///
  /// \param minimize If true, then in the case where there is no single
  /// best solution, minimize the set of solutions by removing any solutions
  /// that are identical to or worse than some other solution. This operation
  /// is quadratic.
  ///
  /// \returns The index of the best solution, or nothing if there was no
  /// best solution.
  Optional<unsigned> findBestSolution(SmallVectorImpl<Solution> &solutions,
                                      bool minimize);

  /// \brief Apply a given solution to the expression, producing a fully
  /// type-checked expression.
  ///
  /// \param convertType the contextual type to which the
  /// expression should be converted, if any.
  /// \param discardedExpr if true, the result of the expression
  /// is contextually ignored.
  /// \param skipClosures if true, don't descend into bodies of
  /// non-single expression closures.
  Expr *applySolution(Solution &solution, Expr *expr,
                      Type convertType, bool discardedExpr,
                      bool suppressDiagnostics,
                      bool skipClosures);

  /// \brief Apply a given solution to the expression to the top-level
  /// expression, producing a fully type-checked expression.
  Expr *applySolutionShallow(const Solution &solution, Expr *expr,
                             bool suppressDiagnostics);
  
  /// Extract the base type from an array or slice type.
  /// \param type The array type to inspect.
  /// \returns the base type of the array.
  Type getBaseTypeForArrayType(TypeBase *type);

  /// Extract the base type from a set type.
  /// \param type The set type to inspect.
  /// \returns the base type of the set.
  Type getBaseTypeForSetType(TypeBase *type);
  
  /// \brief Set whether or not the expression being solved is too complex and
  /// has exceeded the solver's memory threshold.
  void setExpressionTooComplex(bool tc) {
    expressionExceededThreshold = tc;
  }
  
  /// \brief Reorder the disjunctive clauses for a given expression to
  /// increase the likelihood that a favored constraint will be successfully
  /// resolved before any others.
  void optimizeConstraints(Expr *e);
  
  /// \brief Determine if the expression being solved has exceeded the solver's
  /// memory threshold.
  bool getExpressionTooComplex() {
    return expressionExceededThreshold;
  }

  LLVM_ATTRIBUTE_DEPRECATED(
      void dump() LLVM_ATTRIBUTE_USED,
      "only for use within the debugger");
  void print(raw_ostream &out);
};

/// \brief Compute the shuffle required to map from a given tuple type to
/// another tuple type.
///
/// \param fromTuple The tuple type we're converting from, as represented by its
/// TupleTypeElt members.
///
/// \param toTuple The tuple type we're converting to, as represented by its
/// TupleTypeElt members.
///
/// \param sources Will be populated with information about the source of each
/// of the elements for the result tuple. The indices into this array are the
/// indices of the tuple type we're converting to, while the values are
/// either one of the \c TupleShuffleExpr constants or are an index into the
/// source tuple.
///
/// \param variadicArgs Will be populated with all of the variadic arguments
/// that will be placed into the variadic tuple element (i.e., at the index
/// \c where \c consumed[i] is \c TupleShuffleExpr::Variadic). The values
/// are indices into the source tuple.
///
/// \returns true if no tuple conversion is possible, false otherwise.
bool computeTupleShuffle(ArrayRef<TupleTypeElt> fromTuple,
                         ArrayRef<TupleTypeElt> toTuple,
                         SmallVectorImpl<int> &sources,
                         SmallVectorImpl<unsigned> &variadicArgs);
static inline bool computeTupleShuffle(TupleType *fromTuple,
                                       TupleType *toTuple,
                                       SmallVectorImpl<int> &sources,
                                       SmallVectorImpl<unsigned> &variadicArgs){
  return computeTupleShuffle(fromTuple->getElements(), toTuple->getElements(),
                             sources, variadicArgs);
}

/// Describes the arguments to which a parameter binds.
/// FIXME: This is an awful data structure. We want the equivalent of a
/// TinyPtrVector for unsigned values.
typedef SmallVector<unsigned, 1> ParamBinding;

/// Class used as the base for listeners to the \c matchCallArguments process.
///
/// By default, none of the callbacks do anything.
class MatchCallArgumentListener {
public:
  virtual ~MatchCallArgumentListener();

  /// Indicates that the argument at the given index does not match any
  /// parameter.
  ///
  /// \param argIdx The index of the extra argument.
  virtual void extraArgument(unsigned argIdx);

  /// Indicates that no argument was provided for the parameter at the given
  /// indices.
  ///
  /// \param paramIdx The index of the parameter that is missing an argument.
  virtual void missingArgument(unsigned paramIdx);

  /// Indicates that an argument is out-of-order with respect to a previously-
  /// seen argument.
  ///
  /// \param argIdx The argument that came too late in the argument list.
  /// \param prevArgIdx The argument that the \c argIdx should have preceded.
  virtual void outOfOrderArgument(unsigned argIdx, unsigned prevArgIdx);

  /// Indicates that the arguments need to be relabeled to match the parameters.
  ///
  /// \returns true to indicate that this should cause a failure, false
  /// otherwise.
  virtual bool relabelArguments(ArrayRef<Identifier> newNames);
};

/// Match the call arguments (as described by the given argument type) to
/// the parameters (as described by the given parameter type).
///
/// \param argTuple The arguments.
/// \param paramTuple The parameters.
/// \param hasTrailingClosure Whether the last argument is a trailing closure.
/// \param allowFixes Whether to allow fixes when matching arguments.
///
/// \param listener Listener that will be notified when certain problems occur,
/// e.g., to produce a diagnostic.
///
/// \param parameterBindings Will be populated with the arguments that are
/// bound to each of the parameters.
/// \returns true if the call arguments could not be matched to the parameters.
bool matchCallArguments(ArrayRef<CallArgParam> argTuple,
                        ArrayRef<CallArgParam> paramTuple,
                        bool hasTrailingClosure,
                        bool allowFixes,
                        MatchCallArgumentListener &listener,
                        SmallVectorImpl<ParamBinding> &parameterBindings);

/// Attempt to prove that arguments with the given labels at the
/// given parameter depth cannot be used with the given value.
/// If this cannot be proven, conservatively returns true.
bool areConservativelyCompatibleArgumentLabels(ValueDecl *decl,
                                               unsigned parameterDepth,
                                               ArrayRef<Identifier> labels,
                                               bool hasTrailingClosure);

/// Simplify the given locator by zeroing in on the most specific
/// subexpression described by the locator.
///
/// This routine can also find the corresponding "target" locator, which
/// typically provides the other end of a relational constraint. For example,
/// if the primary locator refers to a function argument, the target locator
/// will be set to refer to the corresponding function parameter.
///
/// \param cs The constraint system in which the locator will be simplified.
///
/// \param locator The locator to simplify.
///
/// \param range Will be populated with an "interesting" range.
///
/// \param targetLocator If non-null, will be set to a locator that describes
/// the target of the input locator.
///
/// \return the simplified locator.
ConstraintLocator *simplifyLocator(ConstraintSystem &cs,
                                   ConstraintLocator *locator,
                                   SourceRange &range,
                                   ConstraintLocator **targetLocator = nullptr);

void simplifyLocator(Expr *&anchor,
                     ArrayRef<LocatorPathElt> &path,
                     Expr *&targetAnchor,
                     SmallVectorImpl<LocatorPathElt> &targetPath,
                     SourceRange &range);

} // end namespace constraints

template<typename ...Args>
TypeVariableType *TypeVariableType::getNew(const ASTContext &C, unsigned ID,
                                           Args &&...args) {
  // Allocate memory
  void *mem = C.Allocate(sizeof(TypeVariableType) + sizeof(Implementation),
                         alignof(TypeVariableType),
                         AllocationArena::ConstraintSolver);

  // Construct the type variable.
  auto *result = ::new (mem) TypeVariableType(C, ID);

  // Construct the implementation object.
  new (result+1) TypeVariableType::Implementation(std::forward<Args>(args)...);

  return result;
}

/// If the expression has the effect of a forced downcast, find the
/// underlying forced downcast expression.
ForcedCheckedCastExpr *findForcedDowncast(ASTContext &ctx, Expr *expr);

} // end namespace swift

#endif // LLVM_SWIFT_SEMA_CONSTRAINT_SYSTEM_H
