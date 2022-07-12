//===--- TypeChecker.h - Type Checking Class --------------------*- C++ -*-===//
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
//  This file defines the TypeChecking class.
//
//===----------------------------------------------------------------------===//

#ifndef TYPECHECKING_H
#define TYPECHECKING_H

#include "swift/Sema/TypeCheckRequest.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/AccessScope.h"
#include "swift/AST/AnyFunctionRef.h"
#include "swift/AST/Availability.h"
#include "swift/AST/DiagnosticsSema.h"
#include "swift/AST/KnownProtocols.h"
#include "swift/AST/LazyResolver.h"
#include "swift/AST/NameLookup.h"
#include "swift/AST/TypeRefinementContext.h"
#include "swift/Parse/Lexer.h"
#include "swift/Basic/OptionSet.h"
#include "swift/Config.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/TinyPtrVector.h"
#include <functional>

namespace swift {

class GenericSignatureBuilder;
class GenericTypeResolver;
class NominalTypeDecl;
class NormalProtocolConformance;
class TopLevelContext;
class TypeChecker;
class ExprPattern;

namespace constraints {
  enum class ConstraintKind : char;
  enum class SolutionKind : char;
  class ConstraintSystem;
  class Solution;
}

/// \brief A mapping from substitutable types to the protocol-conformance
/// mappings for those types.
typedef llvm::DenseMap<SubstitutableType *,
                       SmallVector<ProtocolConformance *, 2>> ConformanceMap;

/// \brief Used for recursive lookups into an expr that is already
/// being type-checked and the constraint system in which its type is
/// stored.
typedef std::pair<Expr *,
                  constraints::ConstraintSystem *> ExprAndConstraintSystem;

/// Special-case type checking semantics for certain declarations.
enum class DeclTypeCheckingSemantics {
  /// A normal declaration.
  Normal,
  
  /// The type(of:) declaration, which performs a "dynamic type" operation,
  /// with different behavior for existential and non-existential arguments.
  TypeOf,
  
  /// The withoutActuallyEscaping(_:do:) declaration, which makes a nonescaping
  /// closure temporarily escapable.
  WithoutActuallyEscaping,

  /// The _openExistential(_:do:) declaration, which extracts the value inside
  /// an existential and passes it as a value of its own dynamic type.
  OpenExistential,
};

/// The result of name lookup.
class LookupResult {
private:
  /// The set of results found.
  SmallVector<LookupResultEntry, 4> Results;

public:
  LookupResult() {}

  explicit
  LookupResult(const SmallVectorImpl<LookupResultEntry> &Results)
    : Results(Results.begin(), Results.end()) {}

  typedef SmallVectorImpl<LookupResultEntry>::iterator iterator;
  iterator begin() { return Results.begin(); }
  iterator end() { return Results.end(); }
  unsigned size() const { return Results.size(); }
  bool empty() const { return Results.empty(); }

  const LookupResultEntry& operator[](unsigned index) const {
    return Results[index];
  }

  LookupResultEntry front() const { return Results.front(); }
  LookupResultEntry back() const { return Results.back(); }

  /// Add a result to the set of results.
  void add(LookupResultEntry result) { Results.push_back(result); }

  void clear() { Results.clear(); }

  /// Determine whether the result set is nonempty.
  explicit operator bool() const {
    return !Results.empty();
  }

  TypeDecl *getSingleTypeResult() const {
    if (size() != 1)
      return nullptr;

    return dyn_cast<TypeDecl>(front().getValueDecl());
  }

  /// Filter out any results that aren't accepted by the given predicate.
  void filter(const std::function<bool(LookupResultEntry)> &pred);
};

/// The result of name lookup for types.
class LookupTypeResult {
  /// The set of results found.
  SmallVector<std::pair<TypeDecl *, Type>, 4> Results;

  friend class TypeChecker;

public:
  typedef SmallVectorImpl<std::pair<TypeDecl *, Type>>::iterator iterator;
  iterator begin() { return Results.begin(); }
  iterator end() { return Results.end(); }
  unsigned size() const { return Results.size(); }

  std::pair<TypeDecl *, Type> operator[](unsigned index) const {
    return Results[index];
  }

  std::pair<TypeDecl *, Type> front() const { return Results.front(); }
  std::pair<TypeDecl *, Type> back() const { return Results.back(); }

  /// Add a result to the set of results.
  void addResult(std::pair<TypeDecl *, Type> result) {
    Results.push_back(result);
  }

  /// \brief Determine whether this result set is ambiguous.
  bool isAmbiguous() const {
    return Results.size() > 1;
  }

  /// Determine whether the result set is nonempty.
  explicit operator bool() const {
    return !Results.empty();
  }
};

/// This specifies the purpose of the contextual type, when specified to
/// typeCheckExpression.  This is used for diagnostic generation to produce more
/// specified error messages when the conversion fails.
///
enum ContextualTypePurpose {
  CTP_Unused,           ///< No contextual type is specified.
  CTP_Initialization,   ///< Pattern binding initialization.
  CTP_ReturnStmt,       ///< Value specified to a 'return' statement.
  CTP_ThrowStmt,        ///< Value specified to a 'throw' statement.
  CTP_EnumCaseRawValue, ///< Raw value specified for "case X = 42" in enum.
  CTP_DefaultParameter, ///< Default value in parameter 'foo(a : Int = 42)'.

  CTP_CalleeResult,     ///< Constraint is placed on the result of a callee.
  CTP_CallArgument,     ///< Call to function or operator requires type.
  CTP_ClosureResult,    ///< Closure result expects a specific type.
  CTP_ArrayElement,     ///< ArrayExpr wants elements to have a specific type.
  CTP_DictionaryKey,    ///< DictionaryExpr keys should have a specific type.
  CTP_DictionaryValue,  ///< DictionaryExpr values should have a specific type.
  CTP_CoerceOperand,    ///< CoerceExpr operand coerced to specific type.
  CTP_AssignSource,     ///< AssignExpr source operand coerced to result type.

  CTP_CannotFail,       ///< Conversion can never fail. abort() if it does.
};



/// Flags that can be used to control name lookup.
enum class TypeCheckExprFlags {
  /// Whether we know that the result of the expression is discarded.  This
  /// disables constraints forcing an lvalue result to be loadable.
  IsDiscarded = 0x01,

  /// Whether the client wants to disable the structural syntactic restrictions
  /// that we force for style or other reasons.
  DisableStructuralChecks = 0x02,

  /// Set if the client wants diagnostics suppressed.
  SuppressDiagnostics = 0x04,

  /// If set, the client wants a best-effort solution to the constraint system,
  /// but can tolerate a solution where all of the constraints are solved, but
  /// not all type variables have been determined.  In this case, the constraint
  /// system is not applied to the expression AST, but the ConstraintSystem is
  /// left in-tact.
  AllowUnresolvedTypeVariables = 0x08,

  /// If set, the 'convertType' specified to typeCheckExpression should not
  /// produce a conversion constraint, but it should be used to guide the
  /// solution in terms of performance optimizations of the solver, and in terms
  /// of guiding diagnostics.
  ConvertTypeIsOnlyAHint = 0x10,

  /// If set, this expression isn't embedded in a larger expression or
  /// statement. This should only be used for syntactic restrictions, and should
  /// not affect type checking itself.
  IsExprStmt = 0x20,

  /// If set, this expression is being re-type checked as part of diagnostics,
  /// and so we should not visit bodies of non-single expression closures.
  SkipMultiStmtClosures = 0x40,

  /// Set if the client prefers fixits to be in the form of force unwrapping
  /// or optional chaining to return an optional.
  PreferForceUnwrapToOptional = 0x80,

  /// If set, don't apply a solution.
  SkipApplyingSolution = 0x100,
};

typedef OptionSet<TypeCheckExprFlags> TypeCheckExprOptions;

inline TypeCheckExprOptions operator|(TypeCheckExprFlags flag1,
                                      TypeCheckExprFlags flag2) {
  return TypeCheckExprOptions(flag1) | flag2;
}

/// Flags that can be used to control name lookup.
enum class NameLookupFlags {
  /// Whether we know that this lookup is always a private dependency.
  KnownPrivate = 0x01,
  /// Whether name lookup should be able to find protocol members.
  ProtocolMembers = 0x02,
  /// Whether we should map the requirement to the witness if we
  /// find a protocol member and the base type is a concrete type.
  ///
  /// If this is not set but ProtocolMembers is set, we will
  /// find protocol extension members, but not protocol requirements
  /// that do not yet have a witness (such as inferred associated
  /// types, or witnesses for derived conformances).
  PerformConformanceCheck = 0x04,
  /// Whether to perform 'dynamic' name lookup that finds @objc
  /// members of any class or protocol.
  DynamicLookup = 0x08,
  /// Whether to ignore access control for this lookup, allowing inaccessible
  /// results to be returned.
  IgnoreAccessControl = 0x10,
};

/// A set of options that control name lookup.
typedef OptionSet<NameLookupFlags> NameLookupOptions;

inline NameLookupOptions operator|(NameLookupFlags flag1,
                                   NameLookupFlags flag2) {
  return NameLookupOptions(flag1) | flag2;
}

/// Default options for member name lookup.
const NameLookupOptions defaultMemberLookupOptions
  = NameLookupFlags::DynamicLookup |
    NameLookupFlags::ProtocolMembers |
    NameLookupFlags::PerformConformanceCheck;

/// Default options for constructor lookup.
const NameLookupOptions defaultConstructorLookupOptions
  = NameLookupFlags::ProtocolMembers |
    NameLookupFlags::PerformConformanceCheck;

/// Default options for member type lookup.
const NameLookupOptions defaultMemberTypeLookupOptions
  = NameLookupFlags::ProtocolMembers |
    NameLookupFlags::PerformConformanceCheck;

/// Default options for unqualified name lookup.
const NameLookupOptions defaultUnqualifiedLookupOptions
  = NameLookupFlags::ProtocolMembers |
    NameLookupFlags::PerformConformanceCheck;

/// Describes the result of comparing two entities, of which one may be better
/// or worse than the other, or they are unordered.
enum class Comparison {
  /// Neither entity is better than the other.
  Unordered,
  /// The first entity is better than the second.
  Better,
  /// The first entity is worse than the second.
  Worse
};

/// Specify how we handle the binding of underconstrained (free) type variables
/// within a solution to a constraint system.
enum class FreeTypeVariableBinding {
  /// Disallow any binding of such free type variables.
  Disallow,
  /// Allow the free type variables to persist in the solution.
  Allow,
  /// Bind the type variables to UnresolvedType to represent the ambiguity.
  UnresolvedType
};

/// An abstract interface that can interact with the type checker during
/// the type checking of a particular expression.
class ExprTypeCheckListener {
public:
  virtual ~ExprTypeCheckListener();

  /// Callback invoked once the constraint system has been constructed.
  ///
  /// \param cs The constraint system that has been constructed.
  ///
  /// \param expr The pre-checked expression from which the constraint system
  /// was generated.
  ///
  /// \returns true if an error occurred that is not itself part of the
  /// constraint system, or false otherwise.
  virtual bool builtConstraints(constraints::ConstraintSystem &cs, Expr *expr);

  /// Callback invoked once a solution has been found.
  ///
  /// The callback may further alter the expression, returning either a
  /// new expression (to replace the result) or a null pointer to indicate
  /// failure.
  virtual Expr *foundSolution(constraints::Solution &solution, Expr *expr);

  /// Callback invokes once the chosen solution has been applied to the
  /// expression.
  ///
  /// The callback may further alter the expression, returning either a
  /// new expression (to replace the result) or a null pointer to indicate
  /// failure.
  virtual Expr *appliedSolution(constraints::Solution &solution,
                                Expr *expr);
};

/// A conditional conformance that implied some other requirements. That is, \c
/// ConformingType conforming to \c Protocol may have required additional
/// requirements to be satisfied.
///
/// This is designed to be used in a stack of such requirements, which can be
/// formatted with \c diagnoseConformanceStack.
struct ParentConditionalConformance {
  Type ConformingType;
  ProtocolType *Protocol;

  /// Format the stack \c conformances as a series of notes that trace a path of
  /// conditional conformances that lead to some other failing requirement (that
  /// is not in \c conformances).
  ///
  /// The end of \c conformances is the active end of the stack, i.e. \c
  /// conformances[0] is a conditional conformance that requires \c
  /// conformances[1], etc.
  static void
  diagnoseConformanceStack(DiagnosticEngine &diags, SourceLoc location,
                           ArrayRef<ParentConditionalConformance> conformances);
};

/// An abstract interface that is used by `checkGenericArguments`.
class GenericRequirementsCheckListener {
public:
  virtual ~GenericRequirementsCheckListener();

  /// Callback invoked before trying to check generic requirement placed
  /// between given types. Note: if either of the types assigned to the
  /// requirement is generic parameter or dependent member, this callback
  /// method is going to get their substitutions.
  ///
  /// \param kind The kind of generic requirement to check.
  ///
  /// \param first The left-hand side type assigned to the requirement,
  /// possibly represented by its generic substitute.
  ///
  /// \param second The right-hand side type assigned to the requirement,
  /// possibly represented by its generic substitute.
  ///
  ///
  /// \returns true if it's ok to validate requirement, false otherwise.
  virtual bool shouldCheck(RequirementKind kind, Type first, Type second);

  /// Callback to report the result of a satisfied conformance requirement.
  ///
  /// \param depTy The dependent type, from the signature.
  /// \param replacementTy The type \c depTy was replaced with.
  /// \param conformance The conformance itself.
  virtual void satisfiedConformance(Type depTy, Type replacementTy,
                                    ProtocolConformanceRef conformance);

  /// Callback to diagnose problem with unsatisfied generic requirement.
  ///
  /// \param req The unsatisfied generic requirement.
  ///
  /// \param first The left-hand side type assigned to the requirement,
  /// possibly represented by its generic substitute.
  ///
  /// \param second The right-hand side type assigned to the requirement,
  /// possibly represented by its generic substitute.
  ///
  /// \returns true if problem has been diagnosed, false otherwise.
  virtual bool diagnoseUnsatisfiedRequirement(
      const Requirement &req, Type first, Type second,
      ArrayRef<ParentConditionalConformance> parents);
};

/// The result of `checkGenericRequirement`.
enum class RequirementCheckResult {
  Success, Failure, UnsatisfiedDependency, SubstitutionFailure
};

class ConformsToProtocolResult {
  Optional<ProtocolConformanceRef> Data;
  RequirementCheckResult State;

  ConformsToProtocolResult(Optional<ProtocolConformanceRef> data,
                           RequirementCheckResult state)
      : Data(data), State(state) {}

public:
  static ConformsToProtocolResult unsatisfiedDependency() {
    return ConformsToProtocolResult(
        None, RequirementCheckResult::UnsatisfiedDependency);
  }
  static ConformsToProtocolResult failure() {
    return ConformsToProtocolResult(None, RequirementCheckResult::Failure);
  }
  static ConformsToProtocolResult success(ProtocolConformanceRef ref) {
    return ConformsToProtocolResult(ref, RequirementCheckResult::Success);
  }

  RequirementCheckResult getStatus() const { return State; }
  bool hasUnsatisfiedDependency() const {
    return getStatus() == RequirementCheckResult::UnsatisfiedDependency;
  }
  ProtocolConformanceRef getConformance() const {
    assert(getStatus() == RequirementCheckResult::Success);
    return *Data;
  }
};

/// Flags that describe the context of type checking a pattern or
/// type.
enum class TypeResolutionFlags : unsigned {
  /// Whether to allow unspecified types within a pattern.
  AllowUnspecifiedTypes = 0x01,

  /// Whether the given type can override the type of a typed pattern.
  OverrideType = 0x04,

  /// Whether to allow unbound generic types.
  AllowUnboundGenerics = 0x08,

  /// Whether we are validating the type for SIL.
  SILType = 0x10,

  /// Whether we are parsing a SIL file.  Not the same as SILType,
  /// because the latter is not set if we're parsing an AST type.
  SILMode = 0x20,

  /// Whether we are in the input type of a function, or under one level of
  /// tuple type.  This is not set for multi-level tuple arguments.
  FunctionInput = 0x40,

  /// Whether this is the immediate input type to a function type,
  ImmediateFunctionInput = 0x80,

  /// Whether this is a variadic function input.
  VariadicFunctionInput = 0x100,

  /// Whether we are in the result type of a function body that is
  /// known to produce dynamic Self.
  DynamicSelfResult = 0x200,

  /// Whether this is a resolution based on a non-inferred type pattern.
  FromNonInferredPattern = 0x400,

  /// Whether we are the variable type in a for/in statement.
  EnumerationVariable = 0x800,

  /// Whether we are looking only in the generic signature of the context
  /// we're searching, rather than the entire context.
  GenericSignature = 0x1000,

  /// Whether an unavailable protocol can be referenced.
  AllowUnavailableProtocol = 0x2000,

  /// Whether this type is the value carried in an enum case.
  EnumCase = 0x4000,

  /// Whether this type is being used in an expression or local declaration.
  ///
  /// This affects what sort of dependencies are recorded when resolving the
  /// type.
  InExpression = 0x8000,

  /// Whether this type resolution is guaranteed not to affect downstream files.
  KnownNonCascadingDependency = 0x10000,

  /// Whether we should allow references to unavailable types.
  AllowUnavailable = 0x20000,

  /// Whether this is the payload subpattern of an enum pattern.
  EnumPatternPayload = 0x40000,

  /// Whether we are binding an extension declaration, which limits
  /// the lookup.
  ExtensionBinding = 0x80000,

  /// Whether we are in the inheritance clause of a nominal type declaration
  /// or extension.
  InheritanceClause = 0x100000,

  /// Whether we should resolve only the structure of the resulting
  /// type rather than its complete semantic properties.
  ResolveStructure = 0x200000,

  /// Whether this is the type of an editor placeholder.
  EditorPlaceholder = 0x400000,

  /// Whether we are in a type argument for an optional
  ImmediateOptionalTypeArgument = 0x800000,

  /// Whether we are checking the underlying type of a typealias.
  TypeAliasUnderlyingType = 0x1000000,

  /// Whether we are checking the parameter list of a subscript.
  SubscriptParameters = 0x2000000,

  /// Is it okay to resolve an IUO sigil ("!") here?
  AllowIUO = 0x4000000,
};

/// Option set describing how type resolution should work.
typedef OptionSet<TypeResolutionFlags> TypeResolutionOptions;

/// Strip the contextual options from the given type resolution options.
static inline TypeResolutionOptions
withoutContext(TypeResolutionOptions options, bool preserveSIL = false) {
  options -= TypeResolutionFlags::ImmediateFunctionInput;
  options -= TypeResolutionFlags::FunctionInput;
  options -= TypeResolutionFlags::VariadicFunctionInput;
  options -= TypeResolutionFlags::EnumCase;
  options -= TypeResolutionFlags::ImmediateOptionalTypeArgument;
  options -= TypeResolutionFlags::AllowIUO;
  if (!preserveSIL) options -= TypeResolutionFlags::SILType;
  return options;
}

/// Describes the reason why are we trying to apply @objc to a declaration.
///
/// Should only affect diagnostics. If you change this enum, also change
/// the OBJC_ATTR_SELECT macro in DiagnosticsSema.def.
enum class ObjCReason {
  /// Has the '@cdecl' attribute.
  ExplicitlyCDecl,
  /// Has the 'dynamic' modifier.
  ExplicitlyDynamic,
  /// Has an explicit '@objc' attribute.
  ExplicitlyObjC,
  /// Has an explicit '@IBOutlet' attribute.
  ExplicitlyIBOutlet,
  /// Has an explicit '@IBAction' attribute.
  ExplicitlyIBAction,
  /// Has an explicit '@NSManaged' attribute.
  ExplicitlyNSManaged,
  /// Is a member of an @objc protocol.
  MemberOfObjCProtocol,
  /// Implicitly-introduced @objc.
  ImplicitlyObjC,
  /// Is an override of an @objc member.
  OverridesObjC,
  /// Is a witness to an @objc protocol requirement.
  WitnessToObjC,
  /// Has an explicit '@IBInspectable' attribute.
  ExplicitlyIBInspectable,
  /// Has an explicit '@GKInspectable' attribute.
  ExplicitlyGKInspectable,
  /// Is it a member of an @objc extension of a class.
  MemberOfObjCExtension,
  /// Is it a member of an @objcMembers class.
  MemberOfObjCMembersClass,
  /// A member of an Objective-C-defined class or subclass.
  MemberOfObjCSubclass,
  /// An accessor to a property.
  Accessor,
};

/// Determine whether we should diagnose conflicts due to inferring @objc
/// with this particular reason.
static inline bool shouldDiagnoseObjCReason(ObjCReason reason,
                                            ASTContext &ctx) {
  switch(reason) {
  case ObjCReason::ExplicitlyCDecl:
  case ObjCReason::ExplicitlyDynamic:
  case ObjCReason::ExplicitlyObjC:
  case ObjCReason::ExplicitlyIBOutlet:
  case ObjCReason::ExplicitlyIBAction:
  case ObjCReason::ExplicitlyNSManaged:
  case ObjCReason::MemberOfObjCProtocol:
  case ObjCReason::OverridesObjC:
  case ObjCReason::WitnessToObjC:
  case ObjCReason::ImplicitlyObjC:
  case ObjCReason::MemberOfObjCExtension:
    return true;

  case ObjCReason::ExplicitlyIBInspectable:
  case ObjCReason::ExplicitlyGKInspectable:
    return !ctx.LangOpts.EnableSwift3ObjCInference;

  case ObjCReason::MemberOfObjCSubclass:
  case ObjCReason::MemberOfObjCMembersClass:
  case ObjCReason::Accessor:
    return false;
  }
}

/// Return the %select discriminator for the OBJC_ATTR_SELECT macro used to
/// complain about the correct attribute during @objc inference.
static inline unsigned getObjCDiagnosticAttrKind(ObjCReason reason) {
  switch (reason) {
  case ObjCReason::ExplicitlyCDecl:
  case ObjCReason::ExplicitlyDynamic:
  case ObjCReason::ExplicitlyObjC:
  case ObjCReason::ExplicitlyIBOutlet:
  case ObjCReason::ExplicitlyIBAction:
  case ObjCReason::ExplicitlyNSManaged:
  case ObjCReason::MemberOfObjCProtocol:
  case ObjCReason::OverridesObjC:
  case ObjCReason::WitnessToObjC:
  case ObjCReason::ImplicitlyObjC:
  case ObjCReason::ExplicitlyIBInspectable:
  case ObjCReason::ExplicitlyGKInspectable:
  case ObjCReason::MemberOfObjCExtension:
    return static_cast<unsigned>(reason);

  case ObjCReason::MemberOfObjCSubclass:
  case ObjCReason::MemberOfObjCMembersClass:
  case ObjCReason::Accessor:
    llvm_unreachable("should not diagnose this @objc reason");
  }
}

/// Flags that control protocol conformance checking.
enum class ConformanceCheckFlags {
  /// Whether we're performing the check from within an expression.
  InExpression = 0x01,
  /// Whether we will be using the conformance in the AST.
  ///
  /// This implies that the conformance will have to be complete.
  Used = 0x02,
  /// Whether to suppress dependency tracking entirely.
  ///
  /// FIXME: This deals with some oddities with the
  /// _ObjectiveCBridgeable conformances.
  SuppressDependencyTracking = 0x04,
  /// Whether to skip the check for any conditional conformances.
  ///
  /// When set, the caller takes responsibility for any
  /// conditional requirements required for the conformance to be
  /// correctly used. Otherwise (the default), all of the conditional
  /// requirements will be checked.
  SkipConditionalRequirements = 0x08,
};

/// Options that control protocol conformance checking.
typedef OptionSet<ConformanceCheckFlags> ConformanceCheckOptions;

inline ConformanceCheckOptions operator|(ConformanceCheckFlags lhs,
                                         ConformanceCheckFlags rhs) {
  return ConformanceCheckOptions(lhs) | rhs;
}

/// Describes the kind of checked cast operation being performed.
enum class CheckedCastContextKind {
  /// None: we're just establishing how to perform the checked cast. This
  /// is useful when we don't care to produce any diagnostics.
  None,
  /// A forced cast, with "as!".
  ForcedCast,
  /// A conditional cast, with "as?".
  ConditionalCast,
  /// An "is" expression.
  IsExpr,
  /// An "is" pattern.
  IsPattern,
  /// An enum-element pattern.
  EnumElementPattern,
};

/// The Swift type checker, which takes a parsed AST and performs name binding,
/// type checking, and semantic analysis to produce a type-annotated AST.
class TypeChecker final : public LazyResolver {
public:
  ASTContext &Context;
  DiagnosticEngine &Diags;

  /// \brief The list of function definitions we've encountered.
  std::vector<AbstractFunctionDecl *> definedFunctions;

  /// The list of protocol conformances that were "used" and will need to be
  /// completed before type checking is considered complete.
  llvm::SetVector<NormalProtocolConformance *> UsedConformances;

  /// The list of protocol conformances whose requirements could not be
  /// fully checked and, therefore, should be checked again at the top
  /// level.
  llvm::SetVector<NormalProtocolConformance *> PartiallyCheckedConformances;

  /// The list of declarations that we've done at least partial validation
  /// of during type-checking, but which will need to be finalized before
  /// we can hand them off to SILGen etc.
  llvm::SetVector<ValueDecl *> DeclsToFinalize;

  /// The list of protocols that need their requirement signatures computed,
  /// because they were first validated by validateDeclForNameLookup(),
  /// which skips this step.
  llvm::SetVector<ProtocolDecl *> DelayedRequirementSignatures;

  /// The list of types whose circularity checks were delayed.
  SmallVector<NominalTypeDecl*, 8> DelayedCircularityChecks;

  using TypeAccessScopeCacheMap = llvm::DenseMap<const ValueDecl *, AccessScope>;

  /// Caches the outermost scope where a particular declaration can be used,
  /// relative to a particular file.
  ///
  /// The file is used to handle things like \c \@testable. A null-but-present
  /// value means the type is public.
  llvm::DenseMap<const SourceFile *, TypeAccessScopeCacheMap>
    TypeAccessScopeCache;

  // Caches whether a given declaration is "as specialized" as another.
  llvm::DenseMap<std::pair<ValueDecl*, ValueDecl*>, bool> 
    specializedOverloadComparisonCache;
  
  // We delay validation of C and Objective-C type-bridging functions in the
  // standard library until we encounter a declaration that requires one. This
  // flag is set to 'true' once the bridge functions have been checked.
  bool HasCheckedBridgeFunctions = false;

  /// A list of closures for the most recently type-checked function, which we
  /// will need to compute captures for.
  std::vector<AnyFunctionRef> ClosuresWithUncomputedCaptures;

  /// Local functions that have been captured before their definitions.
  ///
  /// We need this to guard against functions that would transitively capture
  /// variables before their definition, e.g.:
  ///
  /// func outer() {
  ///   func first() {
  ///     second()
  ///   }
  ///   second()
  ///   var x
  ///   func second() {
  ///     use(x)
  ///   }
  /// }

  llvm::SmallDenseMap<AnyFunctionRef, SmallVector<AnyFunctionRef, 4>, 4>
    ForwardCapturedFuncs;

  /// A set of local functions from which C function pointers are derived.
  ///
  /// This is used to diagnose the use of local functions with captured context
  /// as C function pointers when the function's captures have not yet been
  /// computed.
  llvm::DenseMap<AnyFunctionRef, std::vector<Expr*>> LocalCFunctionPointers;

private:
  /// Return statements with functions as return values.
  llvm::DenseMap<AbstractFunctionDecl *, llvm::DenseSet<ReturnStmt *>>
    FunctionAsReturnValue;

  /// Function apply expressions with a certain function as an argument.
  llvm::DenseMap<AbstractFunctionDecl *, llvm::DenseSet<ApplyExpr *>>
    FunctionAsEscapingArg;

  /// The # of times we have performed typo correction.
  unsigned NumTypoCorrections = 0;

public:
  /// Record an occurrence of a function that captures inout values as an
  /// argument.
  ///
  /// \param decl the function that occurs as an argument.
  ///
  /// \param apply the expression in which the function appears.
  void addEscapingFunctionAsArgument(AbstractFunctionDecl *decl,
                                     ApplyExpr *apply) {
    FunctionAsEscapingArg[decl].insert(apply);
  }

  /// Find occurrences of a function that captures inout values as arguments.
  ///
  /// \param decl the function that occurs as an argument.
  ///
  /// \returns Expressions in which the function appears as arguments.
  llvm::DenseSet<ApplyExpr *> &
  getEscapingFunctionAsArgument(AbstractFunctionDecl *decl) {
    return FunctionAsEscapingArg[decl];
  }

  /// Record an occurrence of a function that captures inout values as a return
  /// value
  ///
  /// \param decl the function that occurs as a return value.
  ///
  /// \param stmt the expression in which the function appears.
  void addEscapingFunctionAsReturnValue(AbstractFunctionDecl *decl,
                                        ReturnStmt *stmt) {
    FunctionAsReturnValue[decl].insert(stmt);
  }

  /// Find occurrences of a function that captures inout values as return
  /// values.
  ///
  /// \param decl the function that occurs as a return value.
  ///
  /// \returns Expressions in which the function appears as arguments.
  llvm::DenseSet<ReturnStmt *> &
  getEscapingFunctionAsReturnValue(AbstractFunctionDecl *decl) {
    return FunctionAsReturnValue[decl];
  }

private:
  Type IntLiteralType;
  Type FloatLiteralType;
  Type BooleanLiteralType;
  Type UnicodeScalarType;
  Type ExtendedGraphemeClusterType;
  Type StringLiteralType;
  Type ArrayLiteralType;
  Type DictionaryLiteralType;
  Type ColorLiteralType;
  Type ImageLiteralType;
  Type FileReferenceLiteralType;
  Type StringType;
  Type SubstringType;
  Type IntType;
  Type Int8Type;
  Type UInt8Type;
  Type NSObjectType;
  Type NSErrorType;
  Type NSNumberType;
  Type NSValueType;
  Type ObjCSelectorType;
  Type ExceptionType;

  /// The \c Swift.UnsafeMutablePointer<T> declaration.
  Optional<NominalTypeDecl *> ArrayDecl;

  /// A set of types that can be mapped to C integer types.
  llvm::DenseSet<CanType> CIntegerTypes;

  /// The set of expressions currently being analyzed for failures.
  llvm::DenseMap<Expr*, ExprAndConstraintSystem> DiagnosedExprs;

  ModuleDecl *StdlibModule = nullptr;

  /// The index of the next response metavariable to bind to a REPL result.
  unsigned NextResponseVariableIndex = 0;

  /// If non-zero, warn when a function body takes longer than this many
  /// milliseconds to type-check.
  ///
  /// Intended for debugging purposes only.
  unsigned WarnLongFunctionBodies = 0;

  /// If non-zero, warn when type-checking an expression takes longer
  /// than this many milliseconds.
  ///
  /// Intended for debugging purposes only.
  unsigned WarnLongExpressionTypeChecking = 0;

  /// If non-zero, abort the expression type checker if it takes more
  /// than this many seconds.
  unsigned ExpressionTimeoutThreshold = 600;

  /// If true, the time it takes to type-check each function will be dumped
  /// to llvm::errs().
  bool DebugTimeFunctionBodies = false;

  /// If true, the time it takes to type-check each expression will be
  /// dumped to llvm::errs().
  bool DebugTimeExpressions = false;

  /// Indicate that the type checker is checking code that will be
  /// immediately executed. This will suppress certain warnings
  /// when executing scripts.
  bool InImmediateMode = false;

  /// A helper to construct and typecheck call to super.init().
  ///
  /// \returns NULL if the constructed expression does not typecheck.
  Expr* constructCallToSuperInit(ConstructorDecl *ctor, ClassDecl *ClDecl);

public:
  TypeChecker(ASTContext &Ctx) : TypeChecker(Ctx, Ctx.Diags) { }
  TypeChecker(ASTContext &Ctx, DiagnosticEngine &Diags);
  ~TypeChecker();

  LangOptions &getLangOpts() const { return Context.LangOpts; }

  /// Dump the time it takes to type-check each function to llvm::errs().
  void enableDebugTimeFunctionBodies() {
    DebugTimeFunctionBodies = true;
  }

  /// Dump the time it takes to type-check each function to llvm::errs().
  void enableDebugTimeExpressions() {
    DebugTimeExpressions = true;
  }

  bool getDebugTimeExpressions() {
    return DebugTimeExpressions;
  }

  /// If \p timeInMS is non-zero, warn when a function body takes longer than
  /// this many milliseconds to type-check.
  ///
  /// Intended for debugging purposes only.
  void setWarnLongFunctionBodies(unsigned timeInMS) {
    WarnLongFunctionBodies = timeInMS;
  }

  /// If \p timeInMS is non-zero, warn when type-checking an expression
  /// takes longer than this many milliseconds.
  ///
  /// Intended for debugging purposes only.
  void setWarnLongExpressionTypeChecking(unsigned timeInMS) {
    WarnLongExpressionTypeChecking = timeInMS;
  }

  /// Return the current setting for the number of milliseconds
  /// threshold we use to determine whether to warn about an
  /// expression taking a long time.
  unsigned getWarnLongExpressionTypeChecking() {
    return WarnLongExpressionTypeChecking;
  }

  /// Set the threshold that determines the upper bound for the number
  /// of seconds we'll let the expression type checker run before
  /// considering an expression "too complex".
  void setExpressionTimeoutThreshold(unsigned timeInSeconds) {
    ExpressionTimeoutThreshold = timeInSeconds;
  }

  /// Return the current setting for the threshold that determines
  /// the upper bound for the number of seconds we'll let the
  /// expression type checker run before considering an expression
  /// "too complex".
  unsigned getExpressionTimeoutThresholdInSeconds() {
    return ExpressionTimeoutThreshold;
  }

  bool getInImmediateMode() {
    return InImmediateMode;
  }

  void setInImmediateMode(bool InImmediateMode) {
    this->InImmediateMode = InImmediateMode;
  }

  template<typename ...ArgTypes>
  InFlightDiagnostic diagnose(ArgTypes &&...Args) {
    return Diags.diagnose(std::forward<ArgTypes>(Args)...);
  }

  Type getArraySliceType(SourceLoc loc, Type elementType);
  Type getDictionaryType(SourceLoc loc, Type keyType, Type valueType);
  Type getOptionalType(SourceLoc loc, Type elementType);
  Type getImplicitlyUnwrappedOptionalType(SourceLoc loc, Type elementType);
  Type getUnsafePointerType(SourceLoc loc, Type pointeeType);
  Type getUnsafeMutablePointerType(SourceLoc loc, Type pointeeType);
  Type getStringType(DeclContext *dc);
  Type getSubstringType(DeclContext *dc);
  Type getIntType(DeclContext *dc);
  Type getInt8Type(DeclContext *dc);
  Type getUInt8Type(DeclContext *dc);
  Type getNSObjectType(DeclContext *dc);
  Type getNSErrorType(DeclContext *dc);
  Type getObjCSelectorType(DeclContext *dc);
  Type getExceptionType(DeclContext *dc, SourceLoc loc);
  
  /// \brief Try to resolve an IdentTypeRepr, returning either the referenced
  /// Type or an ErrorType in case of error.
  Type resolveIdentifierType(DeclContext *DC,
                             IdentTypeRepr *IdType,
                             TypeResolutionOptions options,
                             bool diagnoseErrors,
                             GenericTypeResolver *resolver,
                             UnsatisfiedDependency *unsatisfiedDependency);

  /// Bind an UnresolvedDeclRefExpr by performing name lookup and
  /// returning the resultant expression.  Context is the DeclContext used
  /// for the lookup.
  Expr *resolveDeclRefExpr(UnresolvedDeclRefExpr *UDRE, DeclContext *Context);

  /// \brief Validate the given type.
  ///
  /// Type validation performs name binding, checking of generic arguments,
  /// and so on to determine whether the given type is well-formed and can
  /// be used as a type.
  ///
  /// \param Loc The type (with source location information) to validate.
  /// If the type has already been validated, returns immediately.
  ///
  /// \param DC The context that the type appears in.
  ///
  /// \param options Options that alter type resolution.
  ///
  /// \param resolver A resolver for generic types. If none is supplied, this
  /// routine will create a \c GenericTypeToArchetypeResolver to use.
  ///
  /// \returns true if type validation failed, or false otherwise.
  bool validateType(TypeLoc &Loc, DeclContext *DC,
                    TypeResolutionOptions options = None,
                    GenericTypeResolver *resolver = nullptr,
                    UnsatisfiedDependency *unsatisfiedDependency = nullptr);

  /// Check for unsupported protocol types in the given declaration.
  void checkUnsupportedProtocolType(Decl *decl);

  /// Check for unsupported protocol types in the given statement.
  void checkUnsupportedProtocolType(Stmt *stmt);

  /// Expose TypeChecker's handling of GenericParamList to SIL parsing.
  GenericEnvironment *handleSILGenericParams(GenericParamList *genericParams,
                                             DeclContext *DC);

  /// \brief Resolves a TypeRepr to a type.
  ///
  /// Performs name binding, checking of generic arguments, and so on in order
  /// to create a well-formed type.
  ///
  /// \param TyR The type representation to check.
  ///
  /// \param DC The context that the type appears in.
  ///
  /// \param options Options that alter type resolution.
  ///
  /// \param resolver A resolver for generic types. If none is supplied, this
  /// routine will create a \c GenericTypeToArchetypeResolver to use.
  ///
  /// \param unsatisfiedDependency When non-null, used to check whether
  /// dependencies have been satisfied appropriately.
  ///
  /// \returns a well-formed type or an ErrorType in case of an error.
  Type resolveType(TypeRepr *TyR, DeclContext *DC,
                   TypeResolutionOptions options,
                   GenericTypeResolver *resolver = nullptr,
                   UnsatisfiedDependency *unsatisfiedDependency = nullptr);

  void validateDecl(ValueDecl *D);
  void validateDecl(OperatorDecl *decl);
  void validateDecl(PrecedenceGroupDecl *decl);

  /// Perform just enough validation for looking up names using the Decl.
  void validateDeclForNameLookup(ValueDecl *D);

  /// Resolves the access control of the given declaration.
  void validateAccessControl(ValueDecl *D);

  /// Validate the given extension declaration, ensuring that it
  /// properly extends the nominal type it names.
  void validateExtension(ExtensionDecl *ext);

  /// Request that type containing the given member needs to have all
  /// members validated after everythign in the translation unit has
  /// been processed.
  void requestMemberLayout(ValueDecl *member);

  /// Request that the given class needs to have all members validated
  /// after everything in the translation unit has been processed.
  void requestNominalLayout(NominalTypeDecl *nominalDecl);

  /// Request that the superclass of the given class, if any, needs to have
  /// all members validated after everything in the translation unit has
  /// been processed.
  void requestSuperclassLayout(ClassDecl *classDecl);

  /// Perform final validation of a declaration after everything in the
  /// translation unit has been processed.
  void finalizeDecl(ValueDecl *D);

  /// \brief Force all members of an external decl, and also add its
  /// conformances.
  void forceExternalDeclMembers(NominalTypeDecl *NTD);

  /// Resolve a reference to the given type declaration within a particular
  /// context.
  ///
  /// This routine aids unqualified name lookup for types by performing the
  /// resolution necessary to rectify the declaration found by name lookup with
  /// the declaration context from which name lookup started.
  ///
  /// \param typeDecl The type declaration found by name lookup.
  /// \param fromDC The declaration context in which the name lookup occurred.
  /// \param isSpecialized Whether the type will have generic arguments applied.
  /// \param resolver The resolver for generic types.
  ///
  /// \returns the resolved type.
  Type resolveTypeInContext(TypeDecl *typeDecl,
                            DeclContext *foundDC,
                            DeclContext *fromDC,
                            TypeResolutionOptions options,
                            bool isSpecialized,
                            GenericTypeResolver *resolver = nullptr);

  /// Apply generic arguments to the given type.
  ///
  /// This function emits diagnostics about an invalid type or the wrong number
  /// of generic arguments, whereas applyUnboundGenericArguments requires this
  /// to be in a correct and valid form.
  ///
  /// \param type The generic type to which to apply arguments.
  /// \param decl The declaration of the type.
  /// \param loc The source location for diagnostic reporting.
  /// \param dc The context where the arguments are applied.
  /// \param generic The arguments to apply with the angle bracket range for
  /// diagnostics.
  /// \param options The type resolution context.
  /// \param resolver The generic type resolver.
  ///
  /// \returns A BoundGenericType bound to the given arguments, or null on
  /// error.
  ///
  /// \see applyUnboundGenericArguments
  Type applyGenericArguments(Type type, TypeDecl *decl, SourceLoc loc,
                             DeclContext *dc, GenericIdentTypeRepr *generic,
                             TypeResolutionOptions options,
                             GenericTypeResolver *resolver,
                             UnsatisfiedDependency *unsatisfiedDependency);

  /// Apply generic arguments to the given type.
  ///
  /// This function requires a valid unbound generic type with the correct
  /// number of generic arguments given, whereas applyGenericArguments emits
  /// diagnostics in those cases.
  ///
  /// \param unboundType The unbound generic type to which to apply arguments.
  /// \param decl The declaration of the type.
  /// \param loc The source location for diagnostic reporting.
  /// \param dc The context where the arguments are applied.
  /// \param genericArgs The list of generic arguments to apply to the type.
  /// \param options The type resolution context.
  /// \param resolver The generic type resolver.
  ///
  /// \returns A BoundGenericType bound to the given arguments, or null on
  /// error.
  ///
  /// \see applyGenericArguments
  Type applyUnboundGenericArguments(UnboundGenericType *unboundType,
                                    GenericTypeDecl *decl,
                                    SourceLoc loc, DeclContext *dc,
                                    MutableArrayRef<TypeLoc> genericArgs,
                                    TypeResolutionOptions options,
                                    GenericTypeResolver *resolver,
                                    UnsatisfiedDependency *unsatisfiedDependency);

  /// \brief Substitute the given base type into the type of the given nested type,
  /// producing the effective type that the nested type will have.
  ///
  /// \param module The module in which the substitution will be performed.
  /// \param member The member whose type projection is being computed.
  /// \param baseTy The base type that will be substituted for the 'Self' of the
  /// member.
  Type substMemberTypeWithBase(ModuleDecl *module, TypeDecl *member, Type baseTy);

  /// \brief Retrieve the superclass type of the given type, or a null type if
  /// the type has no supertype.
  Type getSuperClassOf(Type type);

  /// \brief Determine whether one type is a subtype of another.
  ///
  /// \param t1 The potential subtype.
  /// \param t2 The potential supertype.
  /// \param dc The context of the check.
  ///
  /// \returns true if \c t1 is a subtype of \c t2.
  bool isSubtypeOf(Type t1, Type t2, DeclContext *dc);

  /// \brief Determine whether one type is a subclass of another.
  ///
  /// \param t1 The potential subtype.
  /// \param t2 The potential supertype.
  /// \param dc The context of the check.
  ///
  /// \returns true if \c t1 is a subtype of \c t2.
  bool isSubclassOf(Type t1, Type t2, DeclContext *dc);
  
  /// \brief Determine whether one type is implicitly convertible to another.
  ///
  /// \param t1 The potential source type of the conversion.
  ///
  /// \param t2 The potential destination type of the conversion.
  ///
  /// \param dc The context of the conversion.
  ///
  /// \param unwrappedIUO If non-null, will be set to indicate whether the
  /// conversion force-unwrapped an implicitly-unwrapped optional.
  ///
  /// \returns true if \c t1 can be implicitly converted to \c t2.
  bool isConvertibleTo(Type t1, Type t2, DeclContext *dc,
                       bool *unwrappedIUO = nullptr);

  /// \brief Determine whether one type is explicitly convertible to another,
  /// i.e. using an 'as' expression.
  ///
  /// \param t1 The potential source type of the conversion.
  ///
  /// \param t2 The potential destination type of the conversion.
  ///
  /// \param dc The context of the conversion.
  ///
  /// \returns true if \c t1 can be explicitly converted to \c t2.
  bool isExplicitlyConvertibleTo(Type t1, Type t2, DeclContext *dc);

  /// \brief Determine whether one type is bridged to another type.
  ///
  /// \param t1 The potential source type of the conversion.
  ///
  /// \param t2 The potential destination type of the conversion.
  ///
  /// \param dc The context of the conversion.
  ///
  /// \param unwrappedIUO If non-null, will be set to indicate whether the
  /// conversion force-unwrapped an implicitly-unwrapped optional.
  ///
  /// \returns true if \c t1 can be explicitly converted to \c t2.
  bool isObjCBridgedTo(Type t1, Type t2, DeclContext *dc,
                       bool *unwrappedIUO = nullptr);

  /// \brief Return true if performing a checked cast from one type to another
  /// with the "as!" operator could possibly succeed.
  ///
  /// \param t1 The potential source type of the cast.
  ///
  /// \param t2 The potential destination type of the cast.
  ///
  /// \param dc The context of the cast.
  ///
  /// \returns true if a checked cast from \c t1 to \c t2 may succeed, and
  /// false if it will certainly fail, e.g. because the types are unrelated.
  bool checkedCastMaySucceed(Type t1, Type t2, DeclContext *dc);

  /// \brief Determine whether a constraint of the given kind can be satisfied
  /// by the two types.
  ///
  /// \param t1 The first type of the constraint.
  ///
  /// \param t2 The second type of the constraint.
  ///
  /// \param openArchetypes If true, archetypes are replaced with type
  /// variables, and the result can be interpreted as whether or not the
  /// two types can possibly equal at runtime.
  ///
  /// \param dc The context of the conversion.
  ///
  /// \param unwrappedIUO   If non-null, will be set to \c true if the coercion
  /// or bridge operation force-unwraps an implicitly-unwrapped optional.
  ///
  /// \returns true if \c t1 and \c t2 satisfy the constraint.
  bool typesSatisfyConstraint(Type t1, Type t2,
                              bool openArchetypes,
                              constraints::ConstraintKind kind,
                              DeclContext *dc,
                              bool *unwrappedIUO = nullptr);

  /// \brief Determine whether one type would be a valid substitution for an
  /// archetype.
  ///
  /// \param type The potential type.
  ///
  /// \param archetype The archetype for which type may (or may not) be
  /// substituted.
  ///
  /// \param dc The context of the check.
  ///
  /// \returns true if \c t1 is a valid substitution for \c t2.
  bool isSubstitutableFor(Type type, ArchetypeType *archetype, DeclContext *dc);

  /// If the inputs to an apply expression use a consistent "sugar" type
  /// (that is, a typealias or shorthand syntax) equivalent to the result type
  /// of the function, set the result type of the expression to that sugar type.
  Expr *substituteInputSugarTypeForResult(ApplyExpr *E);

  bool typeCheckAbstractFunctionBodyUntil(AbstractFunctionDecl *AFD,
                                          SourceLoc EndTypeCheckLoc);
  bool typeCheckAbstractFunctionBody(AbstractFunctionDecl *AFD);
  bool typeCheckFunctionBodyUntil(FuncDecl *FD, SourceLoc EndTypeCheckLoc);
  bool typeCheckConstructorBodyUntil(ConstructorDecl *CD,
                                     SourceLoc EndTypeCheckLoc);
  bool typeCheckDestructorBodyUntil(DestructorDecl *DD,
                                    SourceLoc EndTypeCheckLoc);

  bool typeCheckClosureBody(ClosureExpr *closure);

  void typeCheckTopLevelCodeDecl(TopLevelCodeDecl *TLCD);

  void processREPLTopLevel(SourceFile &SF, TopLevelContext &TLC,
                           unsigned StartElem);
  Identifier getNextResponseVariableName(DeclContext *DC);

  void typeCheckDecl(Decl *D, bool isFirstPass);

  void checkDeclAttributesEarly(Decl *D);
  void checkDeclAttributes(Decl *D);
  void checkTypeModifyingDeclAttributes(VarDecl *var);

  void checkOwnershipAttr(VarDecl *D, OwnershipAttr *attr);

  void computeAccessLevel(ValueDecl *D);
  void computeDefaultAccessLevel(ExtensionDecl *D);

  virtual void resolveAccessControl(ValueDecl *VD) override {
    validateAccessControl(VD);
  }

  virtual void resolveDeclSignature(ValueDecl *VD) override {
    validateDeclForNameLookup(VD);
  }

  virtual void bindExtension(ExtensionDecl *ext) override;

  virtual void resolveExtension(ExtensionDecl *ext) override {
    validateExtension(ext);
    checkInheritanceClause(ext);
  }

  virtual void resolveImplicitConstructors(NominalTypeDecl *nominal) override {
    addImplicitConstructors(nominal);
  }

  virtual void resolveImplicitMember(NominalTypeDecl *nominal, DeclName member) override {
    synthesizeMemberForLookup(nominal, member);
  }

  virtual void
  resolveExternalDeclImplicitMembers(NominalTypeDecl *nominal) override {
    handleExternalDecl(nominal);
  }

  /// Infer default value witnesses for all requirements in the given protocol.
  void inferDefaultWitnesses(ProtocolDecl *proto);

  /// Determine whether the given (potentially constrained) protocol extension
  /// is usable for the given type.
  bool isProtocolExtensionUsable(DeclContext *dc, Type type,
                                 ExtensionDecl *protocolExtension) override;
 
  /// Perform semantic checks on the given generic parameter list.
  void prepareGenericParamList(GenericParamList *genericParams,
                               DeclContext *dc);

  /// Revert the dependent types within the given generic parameter list.
  void revertGenericParamList(GenericParamList *genericParams);

  /// Revert the dependent types within a set of requirements.
  void revertGenericRequirements(MutableArrayRef<RequirementRepr> requirements);

  /// Configure the interface type of a function declaration.
  void configureInterfaceType(AbstractFunctionDecl *func,
                              GenericSignature *sig);

  /// Validate the signature of a generic function.
  ///
  /// \param func The generic function.
  GenericSignature *validateGenericFuncSignature(AbstractFunctionDecl *func);

  /// Revert the signature of a generic function to its pre-type-checked state,
  /// so that it can be type checked again when we have resolved its generic
  /// parameters.
  void revertGenericFuncSignature(AbstractFunctionDecl *func);

  /// Check the generic parameters in the given generic parameter list (and its
  /// parent generic parameter lists) according to the given resolver.
  void checkGenericParamList(GenericSignatureBuilder *builder,
                             GenericParamList *genericParams,
                             GenericSignature *parentSig,
                             GenericTypeResolver *resolver);

  /// Validate the signature of a generic subscript.
  ///
  /// \param subscript The generic subscript.
  GenericSignature *validateGenericSubscriptSignature(SubscriptDecl *subscript);

  /// Revert the signature of a generic function to its pre-type-checked state,
  /// so that it can be type checked again when we have resolved its generic
  /// parameters.
  void revertGenericSubscriptSignature(SubscriptDecl *subscript);

  /// Configure the interface type of a subscript declaration.
  void configureInterfaceType(SubscriptDecl *subscript,
                              GenericSignature *sig);

  /// Construct a new generic environment for the given declaration context.
  ///
  /// \param genericParams The generic parameters to validate.
  ///
  /// \param dc The declaration context in which to perform the validation.
  ///
  /// \param outerSignature The generic signature of the outer
  /// context, if not available as part of the \c dc argument (used
  /// for SIL parsing).
  ///
  /// \param ext The extension for which we're checking the generic
  /// environment, or null if we're not checking an extension.
  ///
  /// \param inferRequirements When non-empty, callback that will be invoked
  /// to perform any additional requirement inference that contributes to the
  /// generic environment..
  ///
  /// \returns the resulting generic environment.
  GenericEnvironment *checkGenericEnvironment(
                        GenericParamList *genericParams,
                        DeclContext *dc,
                        GenericSignature *outerSignature,
                        bool allowConcreteGenericParams,
                        ExtensionDecl *ext,
                        llvm::function_ref<void(GenericSignatureBuilder &)>
                          inferRequirements);

  /// Construct a new generic environment for the given declaration context.
  ///
  /// \param genericParams The generic parameters to validate.
  ///
  /// \param dc The declaration context in which to perform the validation.
  ///
  /// \param outerSignature The generic signature of the outer
  /// context, if not available as part of the \c dc argument (used
  /// for SIL parsing).
  /// \returns the resulting generic environment.
  GenericEnvironment *checkGenericEnvironment(
                        GenericParamList *genericParams,
                        DeclContext *dc,
                        GenericSignature *outerSignature,
                        bool allowConcreteGenericParams,
                        ExtensionDecl *ext) {
    return checkGenericEnvironment(genericParams, dc, outerSignature,
                                   allowConcreteGenericParams, ext,
                                   [&](GenericSignatureBuilder &) { });
  }

  /// Validate the signature of a generic type.
  ///
  /// \param nominal The generic type.
  void validateGenericTypeSignature(GenericTypeDecl *nominal);

  bool validateRequirement(SourceLoc whereLoc, RequirementRepr &req,
                           DeclContext *lookupDC,
                           TypeResolutionOptions options = None,
                           GenericTypeResolver *resolver = nullptr);

  /// Validate the given requirements.
  void validateRequirements(SourceLoc whereLoc,
                            MutableArrayRef<RequirementRepr> requirements,
                            DeclContext *dc,
                            TypeResolutionOptions options,
                            GenericTypeResolver *resolver,
                            GenericSignatureBuilder *builder = nullptr);

  /// Create a text string that describes the bindings of generic parameters
  /// that are relevant to the given set of types, e.g.,
  /// "[with T = Bar, U = Wibble]".
  ///
  /// \param types The types that will be scanned for generic type parameters,
  /// which will be used in the resulting type.
  ///
  /// \param genericParams The generic parameters to use to resugar any
  /// generic parameters that occur within the types.
  ///
  /// \param substitutions The generic parameter -> generic argument
  /// substitutions that will have been applied to these types.
  /// These are used to produce the "parameter = argument" bindings in the test.
  static std::string
  gatherGenericParamBindingsText(ArrayRef<Type> types,
                                 ArrayRef<GenericTypeParamType *> genericParams,
                                 TypeSubstitutionFn substitutions);

  /// Check the given set of generic arguments against the requirements in a
  /// generic signature.
  ///
  /// \param dc The context in which the generic arguments should be checked.
  /// \param loc The location at which any diagnostics should be emitted.
  /// \param noteLoc The location at which any notes will be printed.
  /// \param owner The type that owns the generic signature.
  /// \param genericParams The generic parameters being substituted.
  /// \param requirements The requirements against which the generic arguments
  /// should be checked.
  /// \param substitutions Substitutions from interface types of the signature.
  /// \param unsatisfiedDependency Optional callback for reporting unsatisfied
  /// dependencies.
  /// \param conformanceOptions The flags to use when checking conformance
  /// requirement.
  /// \param listener The generic check listener used to pick requirements and
  /// notify callers about diagnosed errors.
  RequirementCheckResult checkGenericArguments(
      DeclContext *dc, SourceLoc loc, SourceLoc noteLoc, Type owner,
      ArrayRef<GenericTypeParamType *> genericParams,
      ArrayRef<Requirement> requirements,
      TypeSubstitutionFn substitutions,
      LookupConformanceFn conformances,
      UnsatisfiedDependency *unsatisfiedDependency,
      ConformanceCheckOptions conformanceOptions = ConformanceCheckFlags::Used,
      GenericRequirementsCheckListener *listener = nullptr,
      SubstOptions options = None);

  /// Resolve the superclass of the given class.
  void resolveSuperclass(ClassDecl *classDecl) override;

  /// Resolve the raw type of the given enum.
  void resolveRawType(EnumDecl *enumDecl) override;

  /// Resolve the inherited protocols of a given protocol.
  void resolveInheritedProtocols(ProtocolDecl *protocol) override;

  /// Validate a protocol's where clause, along with the where clauses of
  /// its associated types.
  void validateWhereClauses(ProtocolDecl *protocol,
                            GenericTypeResolver *resolver);

  /// Resolve the types in the inheritance clause of the given
  /// declaration context, which will be a nominal type declaration or
  /// extension declaration.
  void resolveInheritanceClause(
         llvm::PointerUnion<TypeDecl *, ExtensionDecl *> decl) override;

  /// Check the inheritance clause of the given declaration.
  void checkInheritanceClause(Decl *decl,
                              GenericTypeResolver *resolver = nullptr);

  /// Retrieve the set of inherited protocols for this protocol type.
  llvm::TinyPtrVector<ProtocolDecl *> getDirectConformsTo(ProtocolDecl *proto);

  /// \brief Add any implicitly-defined constructors required for the given
  /// struct or class.
  void addImplicitConstructors(NominalTypeDecl *typeDecl);

  /// \brief Add the RawOptionSet (todo:, Equatable, and Hashable) methods to an
  /// imported NS_OPTIONS struct.
  void addImplicitStructConformances(StructDecl *ED);

  /// \brief Add the RawRepresentable, Equatable, and Hashable methods to an
  /// enum with a raw type.
  void addImplicitEnumConformances(EnumDecl *ED);

  /// Synthesize the member with the given name on the target if applicable,
  /// i.e. if the member is synthesizable and has not yet been added to the
  /// target.
  void synthesizeMemberForLookup(NominalTypeDecl *target, DeclName member);

  /// The specified AbstractStorageDecl \c storage was just found to satisfy
  /// the protocol property \c requirement.  Ensure that it has the full
  /// complement of accessors.
  void synthesizeWitnessAccessorsForStorage(AbstractStorageDecl *requirement,
                                            AbstractStorageDecl *storage);

  /// Provide storage and accessor implementations for the given property,
  /// which must be lazy.
  void completeLazyVarImplementation(VarDecl *lazyVar);
  
  /// Instantiate the storage implementation for a behavior-backed property.
  void completePropertyBehaviorStorage(VarDecl *VD,
                               VarDecl *BehaviorStorage,
                               FuncDecl *DefaultInitStorage,
                               FuncDecl *ParamInitStorage,
                               Type SelfTy,
                               Type StorageTy,
                               NormalProtocolConformance *BehaviorConformance,
                               SubstitutionList SelfInterfaceSubs,
                               SubstitutionList SelfContextSubs);
  
  /// Instantiate the parameter implementation for a behavior-backed
  /// property.
  void completePropertyBehaviorParameter(VarDecl *VD,
                               FuncDecl *BehaviorParameter,
                               NormalProtocolConformance *BehaviorConformance,
                               SubstitutionList SelfInterfaceSubs,
                               SubstitutionList SelfContextSubs);
  
  /// Instantiate the accessor implementations for a behavior-backed
  /// property.
  void completePropertyBehaviorAccessors(VarDecl *VD,
                                     VarDecl *ValueImpl,
                                     Type valueTy,
                                     SubstitutionList SelfInterfaceSubs,
                                     SubstitutionList SelfContextSubs);

  /// Pre-check the expression, validating any types that occur in the
  /// expression and folding sequence expressions.
  bool preCheckExpression(Expr *&expr, DeclContext *dc);

  /// Sets up and solves the constraint system \p cs to type check the given
  /// expression.
  ///
  /// The expression should have already been pre-checked with
  /// preCheckExpression().
  ///
  /// \returns true if an error occurred, false otherwise.
  ///
  /// \see typeCheckExpression
  bool solveForExpression(Expr *&expr, DeclContext *dc, Type convertType,
                          FreeTypeVariableBinding allowFreeTypeVariables,
                          ExprTypeCheckListener *listener,
                          constraints::ConstraintSystem &cs,
                          SmallVectorImpl<constraints::Solution> &viable,
                          TypeCheckExprOptions options);

  /// \name Name lookup
  ///
  /// Routines that perform name lookup.
  ///
  /// During type checking, these routines should be used instead of
  /// \c MemberLookup and \c UnqualifiedLookup, because these routines will
  /// lazily introduce declarations and (FIXME: eventually) perform recursive
  /// type-checking that the AST-level lookup routines don't.
  ///
  /// @{
private:
  Optional<Type> boolType;

public:
  /// \brief Define the default constructor for the given struct or class.
  void defineDefaultConstructor(NominalTypeDecl *decl);

  /// \brief Fold the given sequence expression into an (unchecked) expression
  /// tree.
  Expr *foldSequence(SequenceExpr *expr, DeclContext *dc);

  /// \brief Type check the given expression.
  ///
  /// \param expr The expression to type-check, which will be modified in
  /// place.
  ///
  /// \param convertTypePurpose When convertType is specified, this indicates
  /// what the conversion is doing.  This allows diagnostics generation to
  /// produce more specific and helpful error messages when the conversion fails
  /// to be possible.
  ///
  /// \param convertType The type that the expression is being converted to,
  /// or null if the expression is standalone.  If the 'ConvertTypeIsOnlyAHint'
  /// option is specified, then this is only a hint, it doesn't produce a full
  /// conversion constraint. The location information is only used for
  /// diagnostics should the conversion fail; it is safe to pass a TypeLoc
  /// without location information.
  ///
  /// \param options Options that control how type checking is performed.
  ///
  /// \param listener If non-null, a listener that will be notified of important
  /// events in the type checking of this expression, and which can introduce
  /// additional constraints.
  ///
  /// \param baseCS If this type checking process is the simplification of
  /// another constraint system, set the original constraint system. \c null
  /// otherwise
  ///
  /// \returns The type of the top-level expression, or Type() if an
  ///          error occurred.
  Type
  typeCheckExpression(Expr *&expr, DeclContext *dc,
                      TypeLoc convertType = TypeLoc(),
                      ContextualTypePurpose convertTypePurpose = CTP_Unused,
                      TypeCheckExprOptions options = TypeCheckExprOptions(),
                      ExprTypeCheckListener *listener = nullptr,
                      constraints::ConstraintSystem *baseCS = nullptr);

  Type typeCheckExpression(Expr *&expr, DeclContext *dc,
                           ExprTypeCheckListener *listener) {
    return typeCheckExpression(expr, dc, TypeLoc(), CTP_Unused,
                               TypeCheckExprOptions(), listener);
  }


  /// \brief Type check the given expression and return its type without
  /// applying the solution.
  ///
  /// \param expr The expression to type-check.
  ///
  /// \param referencedDecl Will be set to the declaration that is referenced by
  /// the expression.
  ///
  /// \param allowFreeTypeVariables Whether free type variables are allowed in
  /// the solution, and what to do with them.
  ///
  /// \param listener If non-null, a listener that will be notified of important
  /// events in the type checking of this expression, and which can introduce
  /// additional constraints.
  ///
  /// \returns the type of \p expr on success, Type() otherwise.
  /// FIXME: expr may still be modified...
  Type getTypeOfExpressionWithoutApplying(
      Expr *&expr, DeclContext *dc,
      ConcreteDeclRef &referencedDecl,
      FreeTypeVariableBinding allowFreeTypeVariables =
                              FreeTypeVariableBinding::Disallow,
      ExprTypeCheckListener *listener = nullptr);

  void getPossibleTypesOfExpressionWithoutApplying(
      Expr *&expr, DeclContext *dc, SmallVectorImpl<Type> &types,
      FreeTypeVariableBinding allowFreeTypeVariables =
          FreeTypeVariableBinding::Disallow,
      ExprTypeCheckListener *listener = nullptr);

  bool typeCheckCompletionSequence(Expr *&expr, DeclContext *DC);

  /// \brief Type check the given expression assuming that its children
  /// have already been fully type-checked.
  ///
  /// \param expr The expression to type-check, which will be modified in
  /// place.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool typeCheckExpressionShallow(Expr *&expr, DeclContext *dc);

  /// Check the key-path expression.
  ///
  /// Returns the type of the last component of the key-path.
  Optional<Type> checkObjCKeyPathExpr(DeclContext *dc, KeyPathExpr *expr,
                                      bool requireResultType = false);

  /// \brief Type check whether the given type declaration includes members of
  /// unsupported recursive value types.
  ///
  /// \param decl The declaration to be type-checked. This process will not
  /// modify the declaration.
  void checkDeclCircularity(NominalTypeDecl *decl);

  /// \brief Type check whether the given switch statement exhaustively covers
  /// its domain.
  ///
  /// \param stmt The switch statement to be type-checked.  No modification of
  /// the statement occurs.
  /// \param limitChecking The checking process relies on the switch statement
  /// being well-formed.  If it is not, pass true to this flag to run a limited
  /// form of analysis.
  void checkSwitchExhaustiveness(SwitchStmt *stmt, bool limitChecking);

  /// \brief Type check the given expression as a condition, which converts
  /// it to a logic value.
  ///
  /// \param expr The expression to type-check, which will be modified in place
  /// to return a logic value (builtin i1).
  ///
  /// \returns true if an error occurred, false otherwise.
  bool typeCheckCondition(Expr *&expr, DeclContext *dc);

  /// \brief Type check the given 'if' or 'while' statement condition, which
  /// either converts an expression to a logic value or bind variables to the
  /// contents of an Optional.
  ///
  /// \param cond The condition to type-check, which will be modified in place.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool typeCheckStmtCondition(StmtCondition &cond, DeclContext *dc,
                              Diag<> diagnosticForAlwaysTrue);

  /// \brief Determine the semantics of a checked cast operation.
  ///
  /// \param fromType       The source type of the cast.
  /// \param toType         The destination type of the cast.
  /// \param dc             The context of the cast.
  /// \param diagLoc        The location at which to report diagnostics.
  /// \param fromExpr       The expression describing the input operand.
  /// \param diagToRange    The source range of the destination type.
  ///
  /// \returns a CheckedCastKind indicating the semantics of the cast. If the
  /// cast is invalid, Unresolved is returned. If the cast represents an implicit
  /// conversion, Coercion is returned.
  CheckedCastKind typeCheckCheckedCast(Type fromType,
                                       Type toType,
                                       CheckedCastContextKind contextKind,
                                       DeclContext *dc,
                                       SourceLoc diagLoc,
                                       Expr *fromExpr,
                                       SourceRange diagToRange);

  /// Find the Objective-C class that bridges between a value of the given
  /// dynamic type and the given value type.
  ///
  /// \param dc The declaration context from which we will look for
  /// bridging.
  ///
  /// \param dynamicType A dynamic type from which we are bridging. Class and
  /// Objective-C protocol types can be used for bridging.
  ///
  /// \param valueType The value type being queried, e.g., String.
  ///
  /// \returns the Objective-C class type that represents the value
  /// type as an Objective-C class, e.g., \c NSString represents \c
  /// String, or a null type if there is no such type or if the
  /// dynamic type isn't something we can start from.
  Type getDynamicBridgedThroughObjCClass(DeclContext *dc,
                                         Type dynamicType,
                                         Type valueType);

  /// \brief Resolve ambiguous pattern/expr productions inside a pattern using
  /// name lookup information. Must be done before type-checking the pattern.
  Pattern *resolvePattern(Pattern *P, DeclContext *dc,
                          bool isStmtCondition);

  /// Type check the given pattern.
  ///
  /// \param P The pattern to type check.
  /// \param dc The context in which type checking occurs.
  /// \param options Options that control type resolution.
  ///
  /// \returns true if any errors occurred during type checking.
  bool typeCheckPattern(Pattern *P, DeclContext *dc,
                        TypeResolutionOptions options);

  bool typeCheckCatchPattern(CatchStmt *S, DeclContext *dc);

  /// Request nominal layout for any types that could be sources of typemetadata
  /// or conformances.
  void requestRequiredNominalTypeLayoutForParameters(ParameterList *PL);

  /// Type check a parameter list.
  bool typeCheckParameterList(ParameterList *PL, DeclContext *dc,
                              TypeResolutionOptions options,
                              GenericTypeResolver &resolver);

  /// Coerce a pattern to the given type.
  ///
  /// \param P The pattern, which may be modified by this coercion.
  /// \param dc The context in which this pattern occurs.
  /// \param type the type to coerce the pattern to.
  /// \param options Options describing how to perform this coercion.
  /// \param resolver The generic resolver to use.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool coercePatternToType(Pattern *&P, DeclContext *dc, Type type,
                           TypeResolutionOptions options,
                           GenericTypeResolver *resolver = nullptr,
                           TypeLoc tyLoc = TypeLoc());
  bool typeCheckExprPattern(ExprPattern *EP, DeclContext *DC,
                            Type type);

  /// Coerce the specified parameter list of a ClosureExpr to the specified
  /// contextual type.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool coerceParameterListToType(ParameterList *P, ClosureExpr *CE, AnyFunctionType *FN);

  
  /// Type-check an initialized variable pattern declaration.
  bool typeCheckBinding(Pattern *&P, Expr *&Init, DeclContext *DC,
                        bool skipApplyingSolution);
  bool typeCheckPatternBinding(PatternBindingDecl *PBD, unsigned patternNumber,
                               bool skipApplyingSolution);

  /// Type-check a for-each loop's pattern binding and sequence together.
  bool typeCheckForEachBinding(DeclContext *dc, ForEachStmt *stmt);

  /// \brief Lazily diagnose conversions to C function pointers of closures
  /// with captures.
  void maybeDiagnoseCaptures(Expr *E, AnyFunctionRef AFR);

  /// \brief Compute the set of captures for the given function or closure.
  void computeCaptures(AnyFunctionRef AFR);

  /// \brief Change the context of closures in the given initializer
  /// expression to the given context.
  ///
  /// \returns true if any closures were found
  static bool contextualizeInitializer(Initializer *DC, Expr *init);
  static void contextualizeTopLevelCode(TopLevelContext &TLC,
                                        ArrayRef<Decl*> topLevelDecls);

  /// Return the type-of-reference of the given value.
  ///
  /// \param baseType if non-null, return the type of a member reference to
  ///   this value when the base has the given type
  ///
  /// \param UseDC The context of the access.  Some variables have different
  ///   types depending on where they are used.
  ///
  /// \param base The optional base expression of this value reference
  ///
  /// \param wantInterfaceType Whether we want the interface type, if available.
  Type getUnopenedTypeOfReference(VarDecl *value, Type baseType,
                                  DeclContext *UseDC,
                                  const DeclRefExpr *base = nullptr,
                                  bool wantInterfaceType = false);

  /// \brief Retrieve the default type for the given protocol.
  ///
  /// Some protocols, particularly those that correspond to literals, have
  /// default types associated with them. This routine retrieves that default
  /// type.
  ///
  /// \returns the default type, or null if there is no default type for
  /// this protocol.
  Type getDefaultType(ProtocolDecl *protocol, DeclContext *dc);

  /// \brief Convert the given expression to the given type.
  ///
  /// \param expr The expression, which will be updated in place.
  /// \param type The type to convert to.
  /// \param typeFromPattern Optionally, the caller can specify the pattern
  ///   from where the toType is derived, so that we can deliver better fixit.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool convertToType(Expr *&expr, Type type, DeclContext *dc,
                     Optional<Pattern*> typeFromPattern = None);

  /// \brief Coerce the given expression to materializable type, if it
  /// isn't already.
  Expr *coerceToRValue(Expr *expr,
                       llvm::function_ref<Type(Expr *)> getType
                         = [](Expr *expr) { return expr->getType(); },
                       llvm::function_ref<void(Expr *, Type)> setType
                         = [](Expr *expr, Type type) {
                           expr->setType(type);
                         });

  /// Require that the library intrinsics for working with Optional<T>
  /// exist.
  bool requireOptionalIntrinsics(SourceLoc loc);

  /// Require that the library intrinsics for working with
  /// UnsafeMutablePointer<T> exist.
  bool requirePointerArgumentIntrinsics(SourceLoc loc);

  /// Require that the library intrinsics for creating
  /// array literals exist.
  bool requireArrayLiteralIntrinsics(SourceLoc loc);

  /// \brief Retrieve the witness type with the given name.
  ///
  /// \param type The type that conforms to the given protocol.
  ///
  /// \param protocol The protocol through which we're looking.
  ///
  /// \param conformance The protocol conformance.
  ///
  /// \param name The name of the associated type.
  ///
  /// \param brokenProtocolDiag Diagnostic to emit if the type cannot be
  /// accessed.
  ///
  /// \return the witness type, or null if an error occurs or the type
  /// returned would contain an ErrorType.
  Type getWitnessType(Type type, ProtocolDecl *protocol,
                      ProtocolConformanceRef conformance,
                      Identifier name,
                      Diag<> brokenProtocolDiag);

  /// \brief Build a call to the witness with the given name and arguments.
  ///
  /// \param base The base expression, whose witness will be invoked.
  ///
  /// \param protocol The protocol to call through.
  ///
  /// \param conformance The conformance of the base type to the given
  /// protocol.
  ///
  /// \param name The name of the method to call.
  ///
  /// \param arguments The arguments to the witness.
  ///
  /// \param brokenProtocolDiag Diagnostic to emit if the protocol is broken.
  ///
  /// \returns a fully type-checked call, or null if the protocol was broken.
  Expr *callWitness(Expr *base, DeclContext *dc,
                    ProtocolDecl *protocol,
                    ProtocolConformanceRef conformance,
                    DeclName name,
                    MutableArrayRef<Expr *> arguments,
                    Diag<> brokenProtocolDiag);

  /// \brief Determine whether the given type contains the given protocol.
  ///
  /// \param DC The context in which to check conformance. This affects, for
  /// example, extension visibility.
  ///
  /// \param options Options that control the conformance check.
  ///
  /// \returns the conformance, if \c T conforms to the protocol \c Proto, or
  /// an empty optional.
  Optional<ProtocolConformanceRef> containsProtocol(
                                     Type T, ProtocolDecl *Proto,
                                     DeclContext *DC,
                                     ConformanceCheckOptions options);

  /// \brief Determine whether the given type conforms to the given protocol.
  ///
  /// Unlike subTypeOfProtocol(), this will return false for existentials of
  /// non-self conforming protocols.
  ///
  /// \param DC The context in which to check conformance. This affects, for
  /// example, extension visibility.
  ///
  /// \param options Options that control the conformance check.
  ///
  /// \param ComplainLoc If valid, then this function will emit diagnostics if
  /// T does not conform to the given protocol. The primary diagnostic will
  /// be placed at this location, with notes for each of the protocol
  /// requirements not satisfied.
  ///
  /// \returns The protocol conformance, if \c T conforms to the
  /// protocol \c Proto, or \c None.
  Optional<ProtocolConformanceRef> conformsToProtocol(
                                     Type T,
                                     ProtocolDecl *Proto,
                                     DeclContext *DC,
                                     ConformanceCheckOptions options,
                                     SourceLoc ComplainLoc = SourceLoc());

  /// A version of the above meant for use with the iterative type checker.
  ConformsToProtocolResult
  conformsToProtocol(Type T, ProtocolDecl *Proto, DeclContext *DC,
                     ConformanceCheckOptions options, SourceLoc ComplainLoc,
                     UnsatisfiedDependency *unsatisfiedDependency);

  /// Mark the given protocol conformance as "used" from the given declaration
  /// context.
  void markConformanceUsed(ProtocolConformanceRef conformance,
                           DeclContext *dc) override final;

  /// Functor class suitable for use as a \c LookupConformanceFn to look up a
  /// conformance through a particular declaration context using the given
  /// type checker.
  class LookUpConformance {
    TypeChecker &tc;
    DeclContext *dc;

  public:
    explicit LookUpConformance(TypeChecker &tc, DeclContext *dc)
      : tc(tc), dc(dc) { }

    Optional<ProtocolConformanceRef>
    operator()(CanType dependentType,
               Type conformingReplacementType,
               ProtocolType *conformedProtocol) const;
  };

  /// Completely check the given conformance.
  void checkConformance(NormalProtocolConformance *conformance);

  /// Check the requirement signature of the given conformance.
  void checkConformanceRequirements(NormalProtocolConformance *conformance);

  /// Check all of the conformances in the given context.
  void checkConformancesInContext(DeclContext *dc,
                                  IterableDeclContext *idc);

  /// Find the @objc requirement that are witnessed by the given
  /// declaration.
  ///
  /// \param anySingleRequirement If true, returns at most a single requirement,
  /// which might be any of the requirements that match.
  ///
  /// \returns the set of requirements to which the given witness is a
  /// witness.
  llvm::TinyPtrVector<ValueDecl *> findWitnessedObjCRequirements(
                                     const ValueDecl *witness,
                                     bool anySingleRequirement = false);

  /// Mark any _ObjectiveCBridgeable conformances in the given type as "used".
  bool useObjectiveCBridgeableConformances(
                        DeclContext *dc, Type type,
                        UnsatisfiedDependency *unsatisfiedDependency = nullptr);

  /// If this bound-generic type is bridged, mark any
  /// _ObjectiveCBridgeable conformances in the generic arguments of
  /// the given type as "used".
  bool useObjectiveCBridgeableConformancesOfArgs(
                        DeclContext *dc, BoundGenericType *bound,
                        UnsatisfiedDependency *unsatisfiedDependency = nullptr);

  /// Mark any _BridgedNSError/_BridgedStoredNSError/related
  /// conformances in the given type as "used".
  void useBridgedNSErrorConformances(DeclContext *dc, Type type);

  /// Derive an implicit declaration to satisfy a requirement of a derived
  /// protocol conformance.
  ///
  /// \param DC           The declaration context where the conformance was
  ///                     defined, either the type itself or an extension
  /// \param TypeDecl     The type for which the requirement is being derived.
  /// \param Requirement  The protocol requirement.
  ///
  /// \returns nullptr if the derivation failed, or the derived declaration
  ///          if it succeeded. If successful, the derived declaration is added
  ///          to TypeDecl's body.
  ValueDecl *deriveProtocolRequirement(DeclContext *DC,
                                       NominalTypeDecl *TypeDecl,
                                       ValueDecl *Requirement);

  /// Derive an implicit type witness for the given associated type in
  /// the conformance of the given nominal type to some known
  /// protocol.
  Type deriveTypeWitness(DeclContext *DC,
                         NominalTypeDecl *nominal,
                         AssociatedTypeDecl *assocType);

  /// Record the witness information into the given conformance that maps
  /// the given requirement to the given witness declaration.
  ///
  /// Use this routine only when the given witness is known to satisfy the
  /// requirement, e.g., because the witness itself was synthesized. This
  /// function is not allowed to fail.
  void recordKnownWitness(NormalProtocolConformance *conformance,
                          ValueDecl *req, ValueDecl *witness);

  /// Perform unqualified name lookup at the given source location
  /// within a particular declaration context.
  ///
  /// \param dc The declaration context in which to perform name lookup.
  /// \param name The name of the entity to look for.
  /// \param loc The source location at which name lookup occurs.
  /// \param options Options that control name lookup.
  LookupResult lookupUnqualified(DeclContext *dc, DeclName name, SourceLoc loc,
                                 NameLookupOptions options
                                   = defaultUnqualifiedLookupOptions);

  /// Perform unqualified type lookup at the given source location
  /// within a particular declaration context.
  ///
  /// \param dc The declaration context in which to perform name lookup.
  /// \param name The name of the entity to look for.
  /// \param loc The source location at which name lookup occurs.
  /// \param options Options that control name lookup.
  LookupResult
  lookupUnqualifiedType(DeclContext *dc, DeclName name, SourceLoc loc,
                        NameLookupOptions options
                          = defaultUnqualifiedLookupOptions);

  /// \brief Lookup a member in the given type.
  ///
  /// \param dc The context that needs the member.
  /// \param type The type in which we will look for a member.
  /// \param name The name of the member to look for.
  /// \param options Options that control name lookup.
  ///
  /// \returns The result of name lookup.
  LookupResult lookupMember(DeclContext *dc, Type type, DeclName name,
                            NameLookupOptions options
                              = defaultMemberLookupOptions);

  /// \brief Check whether the given declaration can be written as a
  /// member of the given base type.
  bool isUnsupportedMemberTypeAccess(Type type,
                                     TypeDecl *typeDecl);

  /// \brief Look up a member type within the given type.
  ///
  /// This routine looks for member types with the given name within the
  /// given type.
  ///
  /// \param dc The context that needs the member.
  /// \param type The type in which we will look for a member type.
  /// \param name The name of the member to look for.
  /// \param options Options that control name lookup.
  ///
  /// \returns The result of name lookup.
  LookupTypeResult lookupMemberType(DeclContext *dc, Type type,
                                    Identifier name,
                                    NameLookupOptions options
                                      = defaultMemberTypeLookupOptions);

  /// \brief Look up the constructors of the given type.
  ///
  /// \param dc The context that needs the constructor.
  /// \param type The type for which we will look for constructors.
  /// \param options Options that control name lookup.
  ///
  /// \returns the constructors found for this type.
  LookupResult lookupConstructors(DeclContext *dc, Type type,
                                  NameLookupOptions options
                                    = defaultConstructorLookupOptions);

  /// Given an expression that's known to be an infix operator,
  /// look up its precedence group.
  PrecedenceGroupDecl *
  lookupPrecedenceGroupForInfixOperator(DeclContext *dc, Expr *op);

  PrecedenceGroupDecl *lookupPrecedenceGroup(DeclContext *dc, Identifier name,
                                             SourceLoc nameLoc);

  /// \brief Look up the Bool type in the standard library.
  Type lookupBoolType(const DeclContext *dc);

  /// Diagnose an ambiguous member type lookup result.
  void diagnoseAmbiguousMemberType(Type baseTy, SourceRange baseRange,
                                   Identifier name, SourceLoc nameLoc,
                                   LookupTypeResult &lookup);

  void diagnoseUnavailableOverride(ValueDecl *override,
                                   const ValueDecl *base,
                                   const AvailableAttr *attr);

  /// Emit a diagnostic for references to declarations that have been
  /// marked as unavailable, either through "unavailable" or "obsoleted:".
  bool diagnoseExplicitUnavailability(const ValueDecl *D,
                                      SourceRange R,
                                      const DeclContext *DC,
                                      const ApplyExpr *call);

  /// Emit a diagnostic for references to declarations that have been
  /// marked as unavailable, either through "unavailable" or "obsoleted:".
  bool diagnoseExplicitUnavailability(
      const ValueDecl *D,
      SourceRange R,
      const DeclContext *DC,
      llvm::function_ref<void(InFlightDiagnostic &)> attachRenameFixIts);

  /// @}

  /// \name Overload resolution
  ///
  /// Routines that perform overload resolution or provide diagnostics related
  /// to overload resolution.
  /// @{

  /// Compare two declarations to determine whether one is more specialized
  /// than the other.
  ///
  /// A declaration is more specialized than another declaration if its type
  /// is a subtype of the other declaration's type (ignoring the 'self'
  /// parameter of function declarations) and if
  Comparison compareDeclarations(DeclContext *dc,
                                 ValueDecl *decl1,
                                 ValueDecl *decl2);

  /// \brief Build a type-checked reference to the given value.
  Expr *buildCheckedRefExpr(VarDecl *D, DeclContext *UseDC,
                            DeclNameLoc nameLoc, bool Implicit);

  /// \brief Build a reference to a declaration, where name lookup returned
  /// the given set of declarations.
  Expr *buildRefExpr(ArrayRef<ValueDecl *> Decls, DeclContext *UseDC,
                     DeclNameLoc NameLoc, bool Implicit,
                     FunctionRefKind functionRefKind);
  /// @}

  /// \brief Retrieve a specific, known protocol.
  ///
  /// \param loc The location at which we need to look for the protocol.
  /// \param kind The known protocol we're looking for.
  ///
  /// \returns null if the protocol is not available. This represents a
  /// problem with the Standard Library.
  ProtocolDecl *getProtocol(SourceLoc loc, KnownProtocolKind kind);

  /// \brief Retrieve the literal protocol for the given expression.
  ///
  /// \returns the literal protocol, if known and available, or null if the
  /// expression does not have an associated literal protocol.
  ProtocolDecl *getLiteralProtocol(Expr *expr);

  DeclName getObjectLiteralConstructorName(ObjectLiteralExpr *expr);

  Type getObjectLiteralParameterType(ObjectLiteralExpr *expr,
                                     ConstructorDecl *ctor);

  /// Get the module appropriate for looking up standard library types.
  ///
  /// This is "Swift", if that module is imported, or the current module if
  /// we're parsing the standard library.
  ModuleDecl *getStdlibModule(const DeclContext *dc);

  /// \name AST Mutation Listener Implementation
  /// @{
  void handleExternalDecl(Decl *decl);

  /// @}

  /// \name Lazy resolution.
  ///
  /// Routines that perform lazy resolution as required for AST operations.
  /// @{
  void resolveTypeWitness(const NormalProtocolConformance *conformance,
                          AssociatedTypeDecl *assocType) override;
  void resolveWitness(const NormalProtocolConformance *conformance,
                      ValueDecl *requirement) override;

  bool isCIntegerType(const DeclContext *DC, Type T);
  bool isRepresentableInObjC(const AbstractFunctionDecl *AFD,
                             ObjCReason Reason,
                             Optional<ForeignErrorConvention> &errorConvention);
  bool isRepresentableInObjC(const VarDecl *VD, ObjCReason Reason);
  bool isRepresentableInObjC(const SubscriptDecl *SD, ObjCReason Reason);

  bool canBeRepresentedInObjC(const ValueDecl *decl);

  void diagnoseTypeNotRepresentableInObjC(const DeclContext *DC,
                                          Type T, SourceRange TypeRange);

  void fillObjCRepresentableTypeCache(const DeclContext *DC);

  /// \name Resilience diagnostics

  void diagnoseInlineableLocalType(const NominalTypeDecl *NTD);

  bool diagnoseInlineableDeclRef(SourceLoc loc, const ValueDecl *D,
                                 const DeclContext *DC);

  /// Used in diagnostic %selects.
  enum class FragileFunctionKind : unsigned {
    Transparent,
    InlineAlways,
    Inlineable,
    DefaultArgument
  };

  /// Given that \p DC is within a fragile context for some reason, describe
  /// why.
  ///
  /// \see FragileFunctionKind
  FragileFunctionKind getFragileFunctionKind(const DeclContext *DC);

  /// \name Availability checking
  ///
  /// Routines that perform API availability checking and type checking of
  /// potentially unavailable API elements
  /// @{

  /// \brief Returns true if the availability of the overriding declaration
  /// makes it a safe override, given the availability of the base declaration.
  bool isAvailabilitySafeForOverride(ValueDecl *override, ValueDecl *base);

  /// \brief Returns true if the availability of the witness
  /// is sufficient to safely conform to the requirement in the context
  /// the provided conformance. On return, requiredAvailability holds th
  /// availability levels required for conformance.
  bool
  isAvailabilitySafeForConformance(ProtocolDecl *proto, ValueDecl *requirement,
                                   ValueDecl *witness, DeclContext *dc,
                                   AvailabilityContext &requiredAvailability);

  /// Returns an over-approximation of the range of operating system versions
  /// that could the passed-in location could be executing upon for
  /// the target platform. If MostRefined != nullptr, set to the most-refined
  /// TRC found while approximating.
  AvailabilityContext
  overApproximateAvailabilityAtLocation(SourceLoc loc, const DeclContext *DC,
                                        const TypeRefinementContext **MostRefined=nullptr);

  /// Walk the AST to build the hierarchy of TypeRefinementContexts
  ///
  /// \param StartElem Where to start for incremental building of refinement
  /// contexts
  void buildTypeRefinementContextHierarchy(SourceFile &SF,
                                           unsigned StartElem);

  /// Build the hierarchy of TypeRefinementContexts for the entire
  /// source file, if it has not already been built. Returns the root
  /// TypeRefinementContext for the source file.
  TypeRefinementContext *getOrBuildTypeRefinementContext(SourceFile *SF);

  /// Returns a diagnostic indicating why the declaration cannot be annotated
  /// with an @available() attribute indicating it is potentially unavailable
  /// or None if this is allowed.
  Optional<Diag<>>
  diagnosticIfDeclCannotBePotentiallyUnavailable(const Decl *D);

  /// Checks whether a declaration is available when referred to at the given
  /// location (this reference location must be in the passed-in
  /// reference DeclContext).
  /// If the declaration is available, return true.
  /// If the declaration is not available, return false and write the
  /// declaration's availability info to the out parameter
  /// \p OutAvailableRange.
  bool isDeclAvailable(const Decl *D, SourceLoc referenceLoc,
                       const DeclContext *referenceDC,
                       AvailabilityContext &OutAvailableRange);

  /// Checks whether a declaration should be considered unavailable when
  /// referred to at the given location and, if so, returns the reason why the
  /// declaration is unavailable. Returns None is the declaration is
  /// definitely available.
  Optional<UnavailabilityReason>
  checkDeclarationAvailability(const Decl *D, SourceLoc referenceLoc,
                               const DeclContext *referenceDC);

  /// Checks an "ignored" expression to see if it's okay for it to be ignored.
  ///
  /// An ignored expression is one that is not nested within a larger
  /// expression or statement.
  void checkIgnoredExpr(Expr *E);

  // Emits a diagnostic, if necessary, for a reference to a declaration
  // that is potentially unavailable at the given source location.
  void diagnosePotentialUnavailability(const ValueDecl *D,
                                       SourceRange ReferenceRange,
                                       const DeclContext *ReferenceDC,
                                       const UnavailabilityReason &Reason);

  // Emits a diagnostic, if necessary, for a reference to a declaration
  // that is potentially unavailable at the given source location, using
  // Name as the diagnostic name.
  void diagnosePotentialUnavailability(const Decl *D, DeclName Name,
                                       SourceRange ReferenceRange,
                                       const DeclContext *ReferenceDC,
                                       const UnavailabilityReason &Reason);

  /// Emits a diagnostic for a reference to a storage accessor that is
  /// potentially unavailable.
  void diagnosePotentialAccessorUnavailability(
      FuncDecl *Accessor, SourceRange ReferenceRange,
      const DeclContext *ReferenceDC, const UnavailabilityReason &Reason,
      bool ForInout);

  /// Returns true if the reference or any of its parents is an
  /// implicit function.
  bool isInsideImplicitFunction(SourceRange ReferenceRange,
                                const DeclContext *DC);

  /// Returns true if the reference or any of its parents is an
  /// unavailable (or obsoleted) declaration.
  bool isInsideUnavailableDeclaration(SourceRange ReferenceRange,
                                      const DeclContext *DC);

  /// Returns true if the reference or any of its parents is an
  /// unconditional unavailable declaration for the same platform.
  bool isInsideCompatibleUnavailableDeclaration(SourceRange ReferenceRange,
                                                const DeclContext *DC,
                                                const AvailableAttr *attr);

  /// Returns true if the reference is lexically contained in a declaration
  /// that is deprecated on all deployment targets.
  bool isInsideDeprecatedDeclaration(SourceRange ReferenceRange,
                                     const DeclContext *DC);

  /// Returns the availability attribute indicating deprecation if the
  /// declaration is deprecated or null otherwise.
  static const AvailableAttr *getDeprecated(const Decl *D);

  /// Emits a diagnostic for a reference to a declaration that is deprecated.
  /// Callers can provide a lambda that adds additional information (such as a
  /// fixit hint) to the deprecation diagnostic, if it is emitted.
  void diagnoseIfDeprecated(SourceRange SourceRange,
                            const DeclContext *ReferenceDC,
                            const ValueDecl *DeprecatedDecl,
                            const ApplyExpr *Call);
  /// @}

  /// If LangOptions::DebugForbidTypecheckPrefix is set and the given decl
  /// has a name with that prefix, an llvm fatal_error is triggered.
  /// This is for testing purposes.
  void checkForForbiddenPrefix(const Decl *D);
  void checkForForbiddenPrefix(const UnresolvedDeclRefExpr *E);
  void checkForForbiddenPrefix(Identifier Ident);
  void checkForForbiddenPrefix(StringRef Name);

  bool hasEnabledForbiddenTypecheckPrefix() const {
    return !Context.LangOpts.DebugForbidTypecheckPrefix.empty();
  }

  /// Check error handling in the given type-checked top-level code.
  void checkTopLevelErrorHandling(TopLevelCodeDecl *D);
  void checkFunctionErrorHandling(AbstractFunctionDecl *D);
  void checkInitializerErrorHandling(Initializer *I, Expr *E);
  void checkEnumElementErrorHandling(EnumElementDecl *D);

  void addExprForDiagnosis(Expr *E1, ExprAndConstraintSystem Result) {
    DiagnosedExprs[E1] = Result;
  }
  bool isExprBeingDiagnosed(Expr *E) {
    return DiagnosedExprs.count(E);
  }
  ExprAndConstraintSystem getExprBeingDiagnosed(Expr *E) {
    return DiagnosedExprs[E];
  }

  /// If an expression references 'self.init' or 'super.init' in an
  /// initializer context, returns the implicit 'self' decl of the constructor.
  /// Otherwise, return nil.
  VarDecl *getSelfForInitDelegationInConstructor(DeclContext *DC,
                                                 UnresolvedDotExpr *ctorRef);

  /// Diagnose assigning variable to itself.
  bool diagnoseSelfAssignment(const Expr *E);

  /// Builds a string representing a "default" generic argument list for
  /// \p typeDecl. In general, this means taking the bound of each generic
  /// parameter. The \p getPreferredType callback can be used to provide a
  /// different type from the bound.
  ///
  /// It may not always be possible to find a single appropriate type for a
  /// particular parameter (say, if it has two bounds). In this case, an
  /// Xcode-style placeholder will be used instead.
  ///
  /// Returns true if the arguments list could be constructed, false if for
  /// some reason it could not.
  bool getDefaultGenericArgumentsString(
      SmallVectorImpl<char> &buf,
      const GenericTypeDecl *typeDecl,
      llvm::function_ref<Type(const GenericTypeParamDecl *)> getPreferredType =
          [](const GenericTypeParamDecl *) { return Type(); });

  /// Attempt to omit needless words from the name of the given declaration.
  Optional<DeclName> omitNeedlessWords(AbstractFunctionDecl *afd);

  /// Attempt to omit needless words from the name of the given declaration.
  Optional<Identifier> omitNeedlessWords(VarDecl *var);

  /// Check for a typo correction.
  void performTypoCorrection(DeclContext *DC,
                             DeclRefKind refKind,
                             Type baseTypeOrNull,
                             DeclName name,
                             SourceLoc lookupLoc,
                             NameLookupOptions lookupOptions,
                             LookupResult &result,
                             GenericSignatureBuilder *gsb = nullptr,
                             unsigned maxResults = 4);

  void noteTypoCorrection(DeclName name, DeclNameLoc nameLoc,
                          ValueDecl *decl);
  
  /// Check if the given decl has a @_semantics attribute that gives it
  /// special case type-checking behavior.
  DeclTypeCheckingSemantics getDeclTypeCheckingSemantics(ValueDecl *decl);
};

/// \brief RAII object that cleans up the given expression if not explicitly
/// disabled.
class CleanupIllFormedExpressionRAII {
  ASTContext &Context;
  Expr **expr;

public:
  CleanupIllFormedExpressionRAII(ASTContext &Context, Expr *&expr)
    : Context(Context), expr(&expr) { }

  ~CleanupIllFormedExpressionRAII();

  static void doIt(Expr *expr, ASTContext &Context);

  /// \brief Disable the cleanup of this expression; it doesn't need it.
  void disable() {
    expr = nullptr;
  }
};

/// Temporary on-stack storage and unescaping for encoded diagnostic
/// messages.
///
///
class EncodedDiagnosticMessage {
  llvm::SmallString<128> Buf;

public:
  /// \param S A string with an encoded message
  EncodedDiagnosticMessage(StringRef S)
      : Message(Lexer::getEncodedStringSegment(S, Buf)) {}

  /// The unescaped message to display to the user.
  const StringRef Message;
};

} // end namespace swift

#endif
