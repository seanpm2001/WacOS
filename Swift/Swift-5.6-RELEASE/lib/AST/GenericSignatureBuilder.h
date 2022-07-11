//===--- GenericSignatureBuilder.h - Generic signature builder --*- C++ -*-===//
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
// Support for collecting a set of generic requirements, whether they are
// explicitly stated, inferred from a type signature, or implied by other
// requirements, and computing the canonicalized, minimized generic signature
// from those requirements.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_GENERICSIGNATUREBUILDER_H
#define SWIFT_GENERICSIGNATUREBUILDER_H

#include "swift/AST/Decl.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/GenericParamList.h"
#include "swift/AST/GenericSignature.h"
#include "swift/AST/Identifier.h"
#include "swift/AST/ProtocolConformanceRef.h"
#include "swift/AST/Types.h"
#include "swift/AST/TypeLoc.h"
#include "swift/AST/TypeRepr.h"
#include "swift/Basic/Debug.h"
#include "swift/Basic/LLVM.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TrailingObjects.h"
#include <functional>
#include <memory>

namespace swift {

class DeclContext;
class DependentMemberType;
class GenericParamList;
class GenericSignatureBuilder;
class GenericTypeParamType;
class ModuleDecl;
class Pattern;
class ProtocolConformance;
class Requirement;
class RequirementRepr;
class SILModule;
class SourceLoc;
class SubstitutionMap;
class Type;
class TypeRepr;
class ASTContext;
class DiagnosticEngine;

/// Determines how to resolve a dependent type to a potential archetype.
enum class ArchetypeResolutionKind {
  /// Only create a potential archetype when it is well-formed (e.g., a nested
  /// type should exist) and make sure we have complete information about
  /// that potential archetype.
  CompleteWellFormed,

  /// Only create a new potential archetype to describe this dependent type
  /// if it is already known.
  AlreadyKnown,

  /// Only create a potential archetype when it is well-formed (i.e., we know
  /// that there is a nested type with that name), but (unlike \c AlreadyKnown)
  /// allow the creation of a new potential archetype.
  WellFormed,
};

/// Collects a set of requirements of generic parameters, both explicitly
/// stated and inferred, and determines the set of archetypes for each of
/// the generic parameters.
class GenericSignatureBuilder {
public:
  /// Describes a potential archetype, which stands in for a generic parameter
  /// type or some type derived from it.
  class PotentialArchetype;

  using UnresolvedType = llvm::PointerUnion<PotentialArchetype *, Type>;
  class ResolvedType;

  using UnresolvedRequirementRHS =
      llvm::PointerUnion<Type, PotentialArchetype *, LayoutConstraint>;

  using RequirementRHS =
    llvm::PointerUnion<Type, ProtocolDecl *, LayoutConstraint>;

  class RequirementSource;

  class FloatingRequirementSource;

  class DelayedRequirement;

  template<typename T> struct Constraint;

  /// Describes a concrete constraint on a potential archetype where, where the
  /// other parameter is a concrete type.
  typedef Constraint<Type> ConcreteConstraint;

  /// Describes an equivalence class of potential archetypes.
  struct EquivalenceClass : llvm::ilist_node<EquivalenceClass> {
    /// The list of protocols to which this equivalence class conforms.
    ///
    /// The keys form the (semantic) list of protocols to which this type
    /// conforms. The values are the conformance constraints as written on
    /// this equivalence class.
    llvm::MapVector<ProtocolDecl *, std::vector<Constraint<ProtocolDecl *>>>
      conformsTo;

    /// Same-type constraints within this equivalence class.
    std::vector<Constraint<Type>> sameTypeConstraints;

    /// Concrete type to which this equivalence class is equal.
    ///
    /// This is the semantic concrete type; the constraints as written
    /// (or implied) are stored in \c concreteTypeConstraints;
    Type concreteType;

    /// The same-type-to-concrete constraints written within this
    /// equivalence class.
    std::vector<ConcreteConstraint> concreteTypeConstraints;

    /// Superclass constraint, which requires that the type fulfilling the
    /// requirements of this equivalence class to be the same as or a subtype
    /// of this superclass.
    Type superclass;

    /// Superclass constraints written within this equivalence class.
    std::vector<ConcreteConstraint> superclassConstraints;

    /// \The layout constraint for this equivalence class.
    LayoutConstraint layout;

    /// Layout constraints written within this equivalence class.
    std::vector<Constraint<LayoutConstraint>> layoutConstraints;

    /// The members of the equivalence class.
    ///
    /// This list of members is slightly ordered, in that the first
    /// element always has a depth no greater than the depth of any other
    /// member.
    TinyPtrVector<PotentialArchetype *> members;

    /// Describes a component within the graph of same-type constraints within
    /// the equivalence class that is held together by derived constraints.
    struct DerivedSameTypeComponent {
      /// The type that acts as the anchor for this component.
      Type type;

      /// The (best) requirement source within the component that makes the
      /// potential archetypes in this component equivalent to the concrete
      /// type.
      const RequirementSource *concreteTypeSource;
    };

    /// The set of connected components within this equivalence class, using
    /// only the derived same-type constraints in the graph.
    std::vector<DerivedSameTypeComponent> derivedSameTypeComponents;

    /// Delayed requirements that could be resolved by a change to this
    /// equivalence class.
    std::vector<DelayedRequirement> delayedRequirements;

    /// Whether we have detected recursion during the substitution of
    /// the concrete type.
    unsigned recursiveConcreteType : 1;

    /// Whether we have an invalid concrete type.
    unsigned invalidConcreteType : 1;

    /// Whether we have detected recursion during the substitution of
    /// the superclass type.
    unsigned recursiveSuperclassType : 1;

    /// Construct a new equivalence class containing only the given
    /// potential archetype (which represents itself).
    EquivalenceClass(PotentialArchetype *representative);

    /// Note that this equivalence class has been modified.
    void modified(GenericSignatureBuilder &builder);

    EquivalenceClass(const EquivalenceClass &) = delete;
    EquivalenceClass(EquivalenceClass &&) = delete;
    EquivalenceClass &operator=(const EquivalenceClass &) = delete;
    EquivalenceClass &operator=(EquivalenceClass &&) = delete;

    /// Add a new member to this equivalence class.
    void addMember(PotentialArchetype *pa);

    /// Record the conformance of this equivalence class to the given
    /// protocol as found via the given requirement source.
    ///
    /// \returns true if this conformance is new to the equivalence class,
    /// and false otherwise.
    bool recordConformanceConstraint(GenericSignatureBuilder &builder,
                                     ResolvedType type,
                                     ProtocolDecl *proto,
                                     const RequirementSource *source);

    /// Find a source of the same-type constraint that maps a potential
    /// archetype in this equivalence class to a concrete type along with
    /// that concrete type as written.
    Optional<ConcreteConstraint>
    findAnyConcreteConstraintAsWritten(Type preferredType = Type()) const;

    /// Find a source of the superclass constraint in this equivalence class
    /// that has a type equivalence to \c superclass, along with that
    /// superclass type as written.
    Optional<ConcreteConstraint>
    findAnySuperclassConstraintAsWritten(Type preferredType = Type()) const;

    /// Determine whether conformance to the given protocol is satisfied by
    /// a superclass requirement.
    bool isConformanceSatisfiedBySuperclass(ProtocolDecl *proto) const;

    /// Lookup a nested type with the given name within this equivalence
    /// class.
    TypeDecl *lookupNestedType(
                   GenericSignatureBuilder &builder,
                   Identifier name);

    /// Retrieve the "anchor" type that canonically describes this equivalence
    /// class, for use in the canonical type.
    Type getAnchor(GenericSignatureBuilder &builder,
                   TypeArrayView<GenericTypeParamType> genericParams);

    /// Dump a debugging representation of this equivalence class,
    void dump(llvm::raw_ostream &out,
              GenericSignatureBuilder *builder = nullptr) const;

    SWIFT_DEBUG_DUMPER(dump(GenericSignatureBuilder *builder = nullptr));

    /// Caches.

    /// The cached archetype anchor.
    struct {
      /// The cached anchor itself.
      Type anchor;

      /// The generation at which the anchor was last computed.
      unsigned lastGeneration;
    } archetypeAnchorCache;

    /// Describes a cached nested type.
    struct CachedNestedType {
      unsigned numConformancesPresent;
      CanType superclassPresent;
      CanType concreteTypePresent;
      TypeDecl *type = nullptr;
    };

    /// Cached nested-type information, which contains the best declaration
    /// for a given name.
    llvm::SmallDenseMap<Identifier, CachedNestedType> nestedTypeNameCache;
  };

  friend class RequirementSource;

  /// The result of introducing a new constraint.
  enum class ConstraintResult {
    /// The constraint was resolved and the relative potential archetypes
    /// have been updated.
    Resolved,

    /// The constraint was written directly on a concrete type.
    Concrete,

    /// The constraint conflicted with existing constraints in some way;
    /// the generic signature is ill-formed.
    Conflicting,

    /// The constraint could not be resolved immediately.
    Unresolved,
  };

  /// Enum used to indicate how we should handle a constraint that cannot be
  /// processed immediately for some reason.
  enum class UnresolvedHandlingKind : char {
    /// Generate a new, unresolved constraint and consider the constraint
    /// "resolved" at this point.
    GenerateConstraints = 0,

    /// Generate an unresolved constraint but still return
    /// \c ConstraintResult::Unresolved so the caller knows what happened.
    GenerateUnresolved = 1,
  };
  
  /// The set of constraints that are invalid because the constraint
  /// type isn't constrained to a protocol or a class
  std::vector<Constraint<Type>> invalidIsaConstraints;

private:
  class InferRequirementsWalker;
  friend class InferRequirementsWalker;
  friend class GenericSignature;

  ASTContext &Context;
  DiagnosticEngine &Diags;
  struct Implementation;
  std::unique_ptr<Implementation> Impl;

  GenericSignatureBuilder(const GenericSignatureBuilder &) = delete;
  GenericSignatureBuilder &operator=(const GenericSignatureBuilder &) = delete;

  /// When a particular requirement cannot be resolved due to, e.g., a
  /// currently-unresolvable or nested type, this routine should be
  /// called to cope with the unresolved requirement.
  ///
  /// \returns \c ConstraintResult::Resolved or ConstraintResult::Delayed,
  /// as appropriate based on \c unresolvedHandling.
  ConstraintResult handleUnresolvedRequirement(RequirementKind kind,
                                   UnresolvedType lhs,
                                   UnresolvedRequirementRHS rhs,
                                   FloatingRequirementSource source,
                                   EquivalenceClass *unresolvedEquivClass,
                                   UnresolvedHandlingKind unresolvedHandling);

  /// Add any conditional requirements from the given conformance.
  void addConditionalRequirements(ProtocolConformanceRef conformance,
                                  ModuleDecl *inferForModule);

  /// Resolve the conformance of the given type to the given protocol when the
  /// potential archetype is known to be equivalent to a concrete type.
  ///
  /// \returns the requirement source for the resolved conformance, or nullptr
  /// if the conformance could not be resolved.
  const RequirementSource *resolveConcreteConformance(ResolvedType type,
                                                      ProtocolDecl *proto,
                                                      bool explicitConformance);

  /// Retrieve the constraint source conformance for the superclass constraint
  /// of the given potential archetype (if present) to the given protocol.
  ///
  /// \param type The type whose superclass constraint is being queried.
  ///
  /// \param proto The protocol to which we are establishing conformance.
  const RequirementSource *resolveSuperConformance(ResolvedType type,
                                                   ProtocolDecl *proto,
                                                   bool explicitConformance);

public:
  /// Add a new conformance requirement specifying that the given
  /// type conforms to the given protocol.
  ConstraintResult addConformanceRequirement(ResolvedType type,
                                             ProtocolDecl *proto,
                                             FloatingRequirementSource source);

  /// "Expand" the conformance of the given \c pa to the protocol \c proto,
  /// adding the requirements from its requirement signature, rooted at
  /// the given requirement \c source.
  ConstraintResult expandConformanceRequirement(
                                      ResolvedType selfType,
                                      ProtocolDecl *proto,
                                      const RequirementSource *source,
                                      bool onlySameTypeConstraints);

  /// Add a new same-type requirement between two fully resolved types
  /// (output of \c GenericSignatureBuilder::resolve).
  ///
  /// If the types refer to two concrete types that are fundamentally
  /// incompatible (e.g. \c Foo<Bar<T>> and \c Foo<Baz>), \c diagnoseMismatch is
  /// called with the two types that don't match (\c Bar<T> and \c Baz for the
  /// previous example).
  ConstraintResult
  addSameTypeRequirementDirect(
                         ResolvedType paOrT1, ResolvedType paOrT2,
                         FloatingRequirementSource Source,
                         llvm::function_ref<void(Type, Type)> diagnoseMismatch);

  /// Add a new same-type requirement between two unresolved types.
  ///
  /// The types are resolved with \c GenericSignatureBuilder::resolve, and must
  /// not be incompatible concrete types.
  ConstraintResult addSameTypeRequirement(
                                    UnresolvedType paOrT1,
                                    UnresolvedType paOrT2,
                                    FloatingRequirementSource Source,
                                    UnresolvedHandlingKind unresolvedHandling);

  /// Add a new same-type requirement between two unresolved types.
  ///
  /// The types are resolved with \c GenericSignatureBuilder::resolve. \c
  /// diagnoseMismatch is called if the two types refer to incompatible concrete
  /// types.
  ConstraintResult
  addSameTypeRequirement(UnresolvedType paOrT1, UnresolvedType paOrT2,
                         FloatingRequirementSource Source,
                         UnresolvedHandlingKind unresolvedHandling,
                         llvm::function_ref<void(Type, Type)> diagnoseMismatch);

  /// Update the superclass for the equivalence class of \c T.
  ///
  /// This assumes that the constraint has already been recorded.
  ///
  /// \returns true if anything in the equivalence class changed, false
  /// otherwise.
  bool updateSuperclass(ResolvedType type,
                        Type superclass,
                        FloatingRequirementSource source);

  /// Update the layout constraint for the equivalence class of \c T.
  ///
  /// This assumes that the constraint has already been recorded.
  ///
  /// \returns true if anything in the equivalence class changed, false
  /// otherwise.
  bool updateLayout(ResolvedType type,
                    LayoutConstraint layout);

private:
  /// Add a new superclass requirement specifying that the given
  /// potential archetype has the given type as an ancestor.
  ConstraintResult addSuperclassRequirementDirect(
                                              ResolvedType type,
                                              Type superclass,
                                              FloatingRequirementSource source);

  /// Add a new type requirement specifying that the given
  /// type conforms-to or is a superclass of the second type.
  ///
  /// \param inferForModule Infer additional requirements from the types
  /// relative to the given module.
  ConstraintResult addTypeRequirement(UnresolvedType subject,
                                      UnresolvedType constraint,
                                      FloatingRequirementSource source,
                                      UnresolvedHandlingKind unresolvedHandling,
                                      ModuleDecl *inferForModule);

  /// Note that we have added the nested type nestedPA
  void addedNestedType(PotentialArchetype *nestedPA);

  /// Add a rewrite rule from that makes the two types equivalent.
  ///
  /// \returns true if a new rewrite rule was added, and false otherwise.
  bool addSameTypeRewriteRule(CanType type1, CanType type2);

  /// Add a same-type requirement between two types that are known to
  /// refer to type parameters.
  ConstraintResult addSameTypeRequirementBetweenTypeParameters(
                                         ResolvedType type1, ResolvedType type2,
                                         const RequirementSource *source);
  
  /// Add a new conformance requirement specifying that the given
  /// potential archetype is bound to a concrete type.
  ConstraintResult addSameTypeRequirementToConcrete(ResolvedType type,
                                        Type concrete,
                                        const RequirementSource *Source);

  /// Add a new same-type requirement specifying that the given two
  /// types should be the same.
  ///
  /// \param diagnoseMismatch Callback invoked when the types in the same-type
  /// requirement mismatch.
  ConstraintResult addSameTypeRequirementBetweenConcrete(
      Type T1, Type T2, FloatingRequirementSource Source,
      llvm::function_ref<void(Type, Type)> diagnoseMismatch);

  /// Add a new layout requirement directly on the potential archetype.
  ///
  /// \returns true if this requirement makes the set of requirements
  /// inconsistent, in which case a diagnostic will have been issued.
  ConstraintResult addLayoutRequirementDirect(ResolvedType type,
                                              LayoutConstraint layout,
                                              FloatingRequirementSource source);

  /// Add a new layout requirement to the subject.
  ConstraintResult addLayoutRequirement(
                                    UnresolvedType subject,
                                    LayoutConstraint layout,
                                    FloatingRequirementSource source,
                                    UnresolvedHandlingKind unresolvedHandling);

  /// Add the requirements placed on the given type parameter
  /// to the given potential archetype.
  ///
  /// \param inferForModule Infer additional requirements from the types
  /// relative to the given module.
  ConstraintResult addInheritedRequirements(
                                TypeDecl *decl,
                                UnresolvedType type,
                                const RequirementSource *parentSource,
                                ModuleDecl *inferForModule);

public:
  /// Construct a new generic signature builder.
  explicit GenericSignatureBuilder(ASTContext &ctx);
  GenericSignatureBuilder(GenericSignatureBuilder &&);
  ~GenericSignatureBuilder();

  /// Retrieve the AST context.
  ASTContext &getASTContext() const { return Context; }

  /// Functor class suitable for use as a \c LookupConformanceFn to look up a
  /// conformance in a generic signature builder.
  class LookUpConformanceInBuilder {
    GenericSignatureBuilder *builder;
  public:
    explicit LookUpConformanceInBuilder(GenericSignatureBuilder *builder)
      : builder(builder) {}

    ProtocolConformanceRef operator()(CanType dependentType,
                                      Type conformingReplacementType,
                                      ProtocolDecl *conformedProtocol) const;
  };

  /// Retrieve a function that can perform conformance lookup for this
  /// builder.
  LookUpConformanceInBuilder getLookupConformanceFn();

  /// Lookup a protocol conformance in a module-agnostic manner.
  ProtocolConformanceRef lookupConformance(Type conformingReplacementType,
                                           ProtocolDecl *conformedProtocol);

  /// Enumerate the requirements that describe the signature of this
  /// generic signature builder.
  void enumerateRequirements(
                    TypeArrayView<GenericTypeParamType> genericParams,
                    SmallVectorImpl<Requirement> &requirements);

  /// Retrieve the generic parameters used to describe the generic
  /// signature being built.
  TypeArrayView<GenericTypeParamType> getGenericParams() const;

  /// Add a new generic parameter for which there may be requirements.
  void addGenericParameter(GenericTypeParamDecl *GenericParam);

  /// Add the requirements placed on the given abstract type parameter
  /// to the given potential archetype.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool addGenericParameterRequirements(GenericTypeParamDecl *GenericParam);

  /// Add a new generic parameter for which there may be requirements.
  void addGenericParameter(GenericTypeParamType *GenericParam);
  
  /// Add a new requirement.
  ///
  /// \param inferForModule Infer additional requirements from the types
  /// relative to the given module.
  ///
  /// \returns true if this requirement makes the set of requirements
  /// inconsistent, in which case a diagnostic will have been issued.
  ConstraintResult addRequirement(const Requirement &req,
                                  FloatingRequirementSource source,
                                  ModuleDecl *inferForModule);

  /// Add an already-checked requirement.
  ///
  /// Adding an already-checked requirement cannot fail. This is used to
  /// re-inject requirements from outer contexts.
  ///
  /// \param inferForModule Infer additional requirements from the types
  /// relative to the given module.
  ///
  /// \returns true if this requirement makes the set of requirements
  /// inconsistent, in which case a diagnostic will have been issued.
  ConstraintResult addRequirement(const Requirement &req,
                                  const RequirementRepr *reqRepr,
                                  FloatingRequirementSource source,
                                  const SubstitutionMap *subMap,
                                  ModuleDecl *inferForModule);

  /// Add all of a generic signature's parameters and requirements.
  void addGenericSignature(GenericSignature sig);

  /// Infer requirements from the given type, recursively.
  ///
  /// This routine infers requirements from a type that occurs within the
  /// signature of a generic function. For example, given:
  ///
  /// \code
  /// func f<K, V>(dict : Dictionary<K, V>) { ... }
  /// \endcode
  ///
  /// where \c Dictionary requires that its key type be \c Hashable,
  /// the requirement \c K : Hashable is inferred from the parameter type,
  /// because the type \c Dictionary<K,V> cannot be formed without it.
  void inferRequirements(ModuleDecl &module,
                         Type type,
                         FloatingRequirementSource source);

  /// Infer requirements from the given pattern, recursively.
  ///
  /// This routine infers requirements from a type that occurs within the
  /// signature of a generic function. For example, given:
  ///
  /// \code
  /// func f<K, V>(dict : Dictionary<K, V>) { ... }
  /// \endcode
  ///
  /// where \c Dictionary requires that its key type be \c Hashable,
  /// the requirement \c K : Hashable is inferred from the parameter type,
  /// because the type \c Dictionary<K,V> cannot be formed without it.
  void inferRequirements(ModuleDecl &module, ParameterList *params);

  GenericSignature rebuildSignatureWithoutRedundantRequirements(
                      bool allowConcreteGenericParams,
                      const ProtocolDecl *requirementSignatureSelfProto) &&;

  bool hadAnyError() const;

  /// Finalize the set of requirements and compute the generic
  /// signature.
  ///
  /// After this point, one cannot introduce new requirements, and the
  /// generic signature builder no longer has valid state.
  GenericSignature computeGenericSignature(
                      bool allowConcreteGenericParams = false,
                      const ProtocolDecl *requirementSignatureSelfProto = nullptr) &&;

private:
  /// Finalize the set of requirements, performing any remaining checking
  /// required before generating archetypes.
  ///
  /// \param allowConcreteGenericParams If true, allow generic parameters to
  /// be made concrete.
  void finalize(TypeArrayView<GenericTypeParamType> genericParams,
                bool allowConcreteGenericParams,
                const ProtocolDecl *requirementSignatureSelfProto);

public:
  /// Process any delayed requirements that can be handled now.
  void processDelayedRequirements();

  class ExplicitRequirement;

  bool isRedundantExplicitRequirement(const ExplicitRequirement &req) const;

private:
  using GetKindAndRHS = llvm::function_ref<std::pair<RequirementKind, RequirementRHS>()>;
  void getBaseRequirements(
      GetKindAndRHS getKindAndRHS,
      const RequirementSource *source,
      const ProtocolDecl *requirementSignatureSelfProto,
      SmallVectorImpl<ExplicitRequirement> &result);

  /// Determine if an explicit requirement can be derived from the
  /// requirement given by \p otherSource and \p otherRHS, using the
  /// knowledge of any existing redundant requirements discovered so far.
  Optional<ExplicitRequirement>
  isValidRequirementDerivationPath(
    llvm::SmallDenseSet<ExplicitRequirement, 4> &visited,
    RequirementKind otherKind,
    const RequirementSource *otherSource,
    RequirementRHS otherRHS,
    const ProtocolDecl *requirementSignatureSelfProto);

  /// Determine if the explicit requirement \p req can be derived from any
  /// of the constraints in \p constraints, using the knowledge of any
  /// existing redundant requirements discovered so far.
  ///
  /// Use \p filter to screen out less-specific and conflicting constraints
  /// if the requirement is a superclass, concrete type or layout requirement.
  template<typename T, typename Filter>
  void checkIfRequirementCanBeDerived(
      const ExplicitRequirement &req,
      const std::vector<Constraint<T>> &constraints,
      const ProtocolDecl *requirementSignatureSelfProto,
      Filter filter);

  void computeRedundantRequirements(
      const ProtocolDecl *requirementSignatureSelfProto);

  void diagnoseProtocolRefinement(
      const ProtocolDecl *requirementSignatureSelfProto);

  void diagnoseRedundantRequirements(
      bool onlyDiagnoseExplicitConformancesImpliedByConcrete=false) const;

  void diagnoseConflictingConcreteTypeRequirements(
      const ProtocolDecl *requirementSignatureSelfProto);

  /// Describes the relationship between a given constraint and
  /// the canonical constraint of the equivalence class.
  enum class ConstraintRelation {
    /// The constraint is unrelated.
    ///
    /// This is a conservative result that can be used when, for example,
    /// we have incomplete information to make a determination.
    Unrelated,
    /// The constraint is redundant and can be removed without affecting the
    /// semantics.
    Redundant,
    /// The constraint conflicts, meaning that the signature is erroneous.
    Conflicting,
  };

  /// Check a list of constraints, removing self-derived constraints
  /// and diagnosing redundant constraints.
  ///
  /// \param isSuitableRepresentative Determines whether the given constraint
  /// is a suitable representative.
  ///
  /// \param checkConstraint Checks the given constraint against the
  /// canonical constraint to determine which diagnostics (if any) should be
  /// emitted.
  ///
  /// \returns the representative constraint.
  template<typename T>
  Constraint<T> checkConstraintList(
                           TypeArrayView<GenericTypeParamType> genericParams,
                           std::vector<Constraint<T>> &constraints,
                           RequirementKind kind,
                           llvm::function_ref<bool(const Constraint<T> &)>
                             isSuitableRepresentative,
                           llvm::function_ref<
                             ConstraintRelation(const Constraint<T>&)>
                               checkConstraint,
                           Optional<Diag<unsigned, Type, T, T>>
                             conflictingDiag,
                           Diag<Type, T> redundancyDiag,
                           Diag<unsigned, Type, T> otherNoteDiag);

  /// Check the concrete type constraints within the equivalence
  /// class of the given potential archetype.
  void checkConcreteTypeConstraints(
                            TypeArrayView<GenericTypeParamType> genericParams,
                            EquivalenceClass *equivClass);

  /// Check same-type constraints within the equivalence class of the
  /// given potential archetype.
  void checkSameTypeConstraints(
                            TypeArrayView<GenericTypeParamType> genericParams,
                            EquivalenceClass *equivClass);

public:
  /// Try to resolve the equivalence class of the given type.
  ///
  /// \param type The type to resolve.
  ///
  /// \param resolutionKind How to perform the resolution.
  ///
  /// \param wantExactPotentialArchetype Whether to return the precise
  /// potential archetype described by the type (vs. just the equivalence
  /// class and resolved type).
  ResolvedType maybeResolveEquivalenceClass(
                                      Type type,
                                      ArchetypeResolutionKind resolutionKind,
                                      bool wantExactPotentialArchetype);

  /// Resolve the equivalence class for the given type parameter,
  /// which provides information about that type.
  ///
  /// The \c resolutionKind parameter describes how resolution should be
  /// performed. If the potential archetype named by the given dependent type
  /// already exists, it will be always returned. If it doesn't exist yet,
  /// the \c resolutionKind dictates whether the potential archetype will
  /// be created or whether null will be returned.
  ///
  /// For any type that cannot refer to an equivalence class, this routine
  /// returns null.
  EquivalenceClass *resolveEquivalenceClass(
                      Type type,
                      ArchetypeResolutionKind resolutionKind);

  /// Resolve the given type as far as this Builder knows how.
  ///
  /// If successful, this returns either a non-typealias potential archetype
  /// or a Type, if \c type is concrete.
  /// If the type cannot be resolved, e.g., because it is "too" recursive
  /// given the source, returns an unresolved result containing the equivalence
  /// class that would need to change to resolve this type.
  ResolvedType resolve(UnresolvedType type, FloatingRequirementSource source);

  /// Determine whether the two given types are in the same equivalence class.
  bool areInSameEquivalenceClass(Type type1, Type type2);

  /// Simplify the given dependent type down to its canonical representation.
  Type getCanonicalTypeParameter(Type type);

  /// Replace any non-canonical dependent types in the given type with their
  /// canonical representation. This is not a canonical type in the AST sense;
  /// type sugar is preserved. The GenericSignature::getCanonicalTypeInContext()
  /// method combines this with a subsequent getCanonicalType() call.
  Type getCanonicalTypeInContext(Type type,
                            TypeArrayView<GenericTypeParamType> genericParams);

  /// Retrieve the conformance access path used to extract the conformance of
  /// interface \c type to the given \c protocol.
  ///
  /// \param type The interface type whose conformance access path is to be
  /// queried.
  /// \param protocol A protocol to which \c type conforms.
  ///
  /// \returns the conformance access path that starts at a requirement of
  /// this generic signature and ends at the conformance that makes \c type
  /// conform to \c protocol.
  ///
  /// \seealso ConformanceAccessPath
  ConformanceAccessPath getConformanceAccessPath(Type type,
                                                 ProtocolDecl *protocol,
                                                 GenericSignature sig);

  /// Dump all of the requirements, both specified and inferred. It cannot be
  /// statically proven that this doesn't modify the GSB.
  SWIFT_DEBUG_HELPER(void dump());

  /// Dump all of the requirements to the given output stream. It cannot be
   /// statically proven that this doesn't modify the GSB.
  void dump(llvm::raw_ostream &out);
};

/// Describes how a generic signature determines a requirement, from its origin
/// in some requirement written in the source, inferred through a path of
/// other implications (e.g., introduced by a particular protocol).
///
/// Requirement sources are uniqued within a generic signature builder.
class GenericSignatureBuilder::RequirementSource final
  : public llvm::FoldingSetNode,
    private llvm::TrailingObjects<RequirementSource, ProtocolDecl *,
                                  SourceLoc> {

  friend class FloatingRequirementSource;
  friend class GenericSignature;

public:
  enum Kind : uint8_t {
    /// A requirement stated explicitly, e.g., in a where clause or type
    /// parameter declaration.
    ///
    /// Explicitly-stated requirement can be tied to a specific requirement
    /// in a 'where' clause (which stores a \c RequirementRepr), a type in an
    /// 'inheritance' clause (which stores a \c TypeRepr), or can be 'abstract',
    /// , e.g., due to canonicalization, deserialization, or other
    /// source-independent formulation.
    ///
    /// This is a root requirement source.
    Explicit,

    /// A requirement inferred from part of the signature of a declaration,
    /// e.g., the type of a generic function. For example:
    ///
    /// func f<T>(_: Set<T>) { } // infers T: Hashable
    ///
    /// This is a root requirement source, which can be described by a
    /// \c TypeRepr.
    Inferred,

    /// A requirement for the creation of the requirement signature of a
    /// protocol.
    ///
    /// This is a root requirement source, which is described by the protocol
    /// whose requirement signature is being computed.
    RequirementSignatureSelf,

    /// The requirement came from two nested types of the equivalent types whose
    /// names match.
    ///
    /// This is a root requirement source.
    NestedTypeNameMatch,

    /// The requirement is a protocol requirement.
    ///
    /// This stores the protocol that introduced the requirement as well as the
    /// dependent type (relative to that protocol) to which the conformance
    /// appertains.
    ProtocolRequirement,

    /// The requirement is a protocol requirement that is inferred from
    /// some part of the protocol definition.
    ///
    /// This stores the protocol that introduced the requirement as well as the
    /// dependent type (relative to that protocol) to which the conformance
    /// appertains.
    InferredProtocolRequirement,

    /// A requirement that was resolved via a superclass requirement.
    ///
    /// This stores the \c ProtocolConformanceRef used to resolve the
    /// requirement.
    Superclass,

    /// A requirement that was resolved for a nested type via its parent
    /// type.
    Parent,

    /// A requirement that was resolved for a nested type via a same-type-to-
    /// concrete constraint.
    ///
    /// This stores the \c ProtocolConformance* used to resolve the
    /// requirement.
    Concrete,

    /// A requirement that was resolved based on a layout requirement
    /// imposed by a superclass constraint.
    ///
    /// This stores the \c LayoutConstraint used to resolve the
    /// requirement.
    Layout,

    /// A requirement that was provided for another type in the
    /// same equivalence class, but which we want to "re-root" on a new
    /// type.
    EquivalentType,
  };

  /// The kind of requirement source.
  const Kind kind;

private:
  /// The kind of storage we have.
  enum class StorageKind : uint8_t {
    None,
    StoredType,
    ProtocolConformance,
    AssociatedTypeDecl,
  };

  /// The kind of storage we have.
  const StorageKind storageKind;

  /// Whether there is a trailing written requirement location.
  const bool hasTrailingSourceLoc;

private:
  /// The actual storage, described by \c storageKind.
  union {
    /// The type to which a requirement applies.
    TypeBase *type;

    /// A protocol conformance used to satisfy the requirement.
    void *conformance;

    /// An associated type to which a requirement is being applied.
    AssociatedTypeDecl *assocType;
  } storage;

  friend TrailingObjects;

  /// The trailing protocol declaration, if there is one.
  size_t numTrailingObjects(OverloadToken<ProtocolDecl *>) const {
    switch (kind) {
    case RequirementSignatureSelf:
    case ProtocolRequirement:
    case InferredProtocolRequirement:
      return 1;

    case Explicit:
    case Inferred:
    case NestedTypeNameMatch:
    case Superclass:
    case Parent:
    case Concrete:
    case Layout:
    case EquivalentType:
      return 0;
    }

    llvm_unreachable("Unhandled RequirementSourceKind in switch.");
  }

  /// The trailing written requirement location, if there is one.
  size_t numTrailingObjects(OverloadToken<SourceLoc>) const {
    return hasTrailingSourceLoc ? 1 : 0;
  }

#ifndef NDEBUG
  /// Determines whether we have been provided with an acceptable storage kind
  /// for the given requirement source kind.
  static bool isAcceptableStorageKind(Kind kind, StorageKind storageKind);
#endif

  /// Retrieve the opaque storage as a single pointer, for use in uniquing.
  const void *getOpaqueStorage1() const;

  /// Retrieve the second opaque storage as a single pointer, for use in
  /// uniquing.
  const void *getOpaqueStorage2() const;

  /// Retrieve the third opaque storage as a single pointer, for use in
  /// uniquing.
  const void *getOpaqueStorage3() const;

  /// Whether this kind of requirement source is a root.
  static bool isRootKind(Kind kind) {
    switch (kind) {
    case Explicit:
    case Inferred:
    case RequirementSignatureSelf:
    case NestedTypeNameMatch:
      return true;

    case ProtocolRequirement:
    case InferredProtocolRequirement:
    case Superclass:
    case Parent:
    case Concrete:
    case Layout:
    case EquivalentType:
      return false;
    }

    llvm_unreachable("Unhandled RequirementSourceKind in switch.");
  }

public:
  /// The "parent" of this requirement source.
  ///
  /// The chain of parent requirement sources will eventually terminate in a
  /// requirement source with one of the "root" kinds.
  const RequirementSource * const parent;

  RequirementSource(Kind kind, Type rootType,
                    ProtocolDecl *protocol,
                    SourceLoc writtenReqLoc)
    : kind(kind), storageKind(StorageKind::StoredType),
      hasTrailingSourceLoc(writtenReqLoc.isValid()),
      parent(nullptr) {
    assert(isAcceptableStorageKind(kind, storageKind) &&
           "RequirementSource kind/storageKind mismatch");

    storage.type = rootType.getPointer();
    if (kind == RequirementSignatureSelf)
      getTrailingObjects<ProtocolDecl *>()[0] = protocol;
    if (hasTrailingSourceLoc)
      getTrailingObjects<SourceLoc>()[0] = writtenReqLoc;
  }

  RequirementSource(Kind kind, const RequirementSource *parent,
                    Type type, ProtocolDecl *protocol,
                    SourceLoc writtenReqLoc)
    : kind(kind), storageKind(StorageKind::StoredType),
      hasTrailingSourceLoc(writtenReqLoc.isValid()),
      parent(parent) {
    assert((static_cast<bool>(parent) != isRootKind(kind)) &&
           "Root RequirementSource should not have parent (or vice versa)");
    assert(isAcceptableStorageKind(kind, storageKind) &&
           "RequirementSource kind/storageKind mismatch");

    storage.type = type.getPointer();
    if (isProtocolRequirement())
      getTrailingObjects<ProtocolDecl *>()[0] = protocol;
    if (hasTrailingSourceLoc)
      getTrailingObjects<SourceLoc>()[0] = writtenReqLoc;
  }

  RequirementSource(Kind kind, const RequirementSource *parent,
                    ProtocolConformanceRef conformance)
    : kind(kind), storageKind(StorageKind::ProtocolConformance),
      hasTrailingSourceLoc(false), parent(parent) {
    assert((static_cast<bool>(parent) != isRootKind(kind)) &&
           "Root RequirementSource should not have parent (or vice versa)");
    assert(isAcceptableStorageKind(kind, storageKind) &&
           "RequirementSource kind/storageKind mismatch");

    storage.conformance = conformance.getOpaqueValue();
  }

  RequirementSource(Kind kind, const RequirementSource *parent,
                    AssociatedTypeDecl *assocType)
    : kind(kind), storageKind(StorageKind::AssociatedTypeDecl),
      hasTrailingSourceLoc(false), parent(parent) {
    assert((static_cast<bool>(parent) != isRootKind(kind)) &&
           "Root RequirementSource should not have parent (or vice versa)");
    assert(isAcceptableStorageKind(kind, storageKind) &&
           "RequirementSource kind/storageKind mismatch");

    storage.assocType = assocType;
  }

  RequirementSource(Kind kind, const RequirementSource *parent)
    : kind(kind), storageKind(StorageKind::None),
      hasTrailingSourceLoc(false), parent(parent) {
    assert((static_cast<bool>(parent) != isRootKind(kind)) &&
           "Root RequirementSource should not have parent (or vice versa)");
    assert(isAcceptableStorageKind(kind, storageKind) &&
           "RequirementSource kind/storageKind mismatch");
  }

  RequirementSource(Kind kind, const RequirementSource *parent,
                    Type newType)
    : kind(kind), storageKind(StorageKind::StoredType),
      hasTrailingSourceLoc(false), parent(parent) {
    assert((static_cast<bool>(parent) != isRootKind(kind)) &&
           "Root RequirementSource should not have parent (or vice versa)");
    assert(isAcceptableStorageKind(kind, storageKind) &&
           "RequirementSource kind/storageKind mismatch");
    storage.type = newType.getPointer();
  }

public:
  /// Retrieve an abstract requirement source.
  static const RequirementSource *forAbstract(GenericSignatureBuilder &builder,
                                              Type rootType);

  /// Retrieve a requirement source representing an explicit requirement
  /// stated in an 'inheritance' or 'where' clause.
  static const RequirementSource *forExplicit(GenericSignatureBuilder &builder,
                                              Type rootType,
                                              SourceLoc writtenLoc);

  /// Retrieve a requirement source representing a requirement that is
  /// inferred from some part of a generic declaration's signature, e.g., the
  /// parameter or result type of a generic function.
  static const RequirementSource *forInferred(GenericSignatureBuilder &builder,
                                              Type rootType,
                                              SourceLoc writtenLoc);

  /// Retrieve a requirement source representing the requirement signature
  /// computation for a protocol.
  static const RequirementSource *forRequirementSignature(
                                              GenericSignatureBuilder &builder,
                                              Type rootType,
                                              ProtocolDecl *protocol);

  /// Retrieve a requirement source for nested type name matches.
  static const RequirementSource *forNestedTypeNameMatch(
                                      GenericSignatureBuilder &builder,
                                      Type rootType);

private:
  /// A requirement source that describes that a requirement comes from a
  /// requirement of the given protocol described by the parent.
  const RequirementSource *viaProtocolRequirement(
                             GenericSignatureBuilder &builder,
                             Type dependentType,
                             ProtocolDecl *protocol,
                             bool inferred,
                             SourceLoc writtenLoc =
                               SourceLoc()) const;
public:
  /// A requirement source that describes a conformance requirement resolved
  /// via a superclass requirement.
  const RequirementSource *viaSuperclass(
                                    GenericSignatureBuilder &builder,
                                    ProtocolConformanceRef conformance) const;

  /// A requirement source that describes a conformance requirement resolved
  /// via a concrete type requirement with a conforming nominal type.
  const RequirementSource *viaConcrete(
                                     GenericSignatureBuilder &builder,
                                     ProtocolConformanceRef conformance) const;

  /// A requirement source that describes that a requirement that is resolved
  /// via a concrete type requirement with an existential self-conforming type.
  const RequirementSource *viaConcrete(
                                     GenericSignatureBuilder &builder,
                                     Type existentialType) const;

  /// A constraint source that describes a layout constraint that was implied
  /// by a superclass requirement.
  const RequirementSource *viaLayout(GenericSignatureBuilder &builder,
                                     Type superclass) const;

  /// A constraint source that describes that a constraint that is resolved
  /// for a nested type via a constraint on its parent.
  ///
  /// \param assocType the associated type that
  const RequirementSource *viaParent(GenericSignatureBuilder &builder,
                                     AssociatedTypeDecl *assocType) const;

  /// A constraint source that describes a constraint that is structurally
  /// derived from another constraint but does not require further information.
  const RequirementSource *viaEquivalentType(GenericSignatureBuilder &builder,
                                             Type newType) const;

  /// Form a new requirement source without the subpath [start, end).
  ///
  /// Removes a redundant sub-path \c [start, end) from the requirement source,
  /// creating a new requirement source comprised on \c start followed by
  /// everything that follows \c end.
  /// It is the caller's responsibility to ensure that the path up to \c start
  /// and the path through \c start to \c end produce the same thing.
  const RequirementSource *withoutRedundantSubpath(
                                          GenericSignatureBuilder &builder,
                                          const RequirementSource *start,
                                          const RequirementSource *end) const;

  /// Retrieve the root requirement source.
  const RequirementSource *getRoot() const;

  /// Retrieve the type at the root.
  Type getRootType() const;

  /// Retrieve the type to which this source refers.
  Type getAffectedType() const;

  /// Visit each of the types along the path, from the root type
  /// each type named via (e.g.) a protocol requirement or parent source.
  ///
  /// \param visitor Called with each type along the path along
  /// with the requirement source that is being applied on top of that
  /// type. Can return \c true to halt the search.
  ///
  /// \returns a null type if any call to \c visitor returned true. Otherwise,
  /// returns the type to which the entire source refers.
  Type visitPotentialArchetypesAlongPath(
           llvm::function_ref<bool(Type,
                                   const RequirementSource *)> visitor) const;

  /// Whether this source is a requirement in a protocol.
  bool isProtocolRequirement() const {
    return kind == ProtocolRequirement || kind == InferredProtocolRequirement;
  }

  /// Whether the requirement is inferred or derived from an inferred
  /// requirement.
  bool isInferredRequirement() const;

  /// Classify the kind of this source for diagnostic purposes.
  unsigned classifyDiagKind() const;

  /// Whether the requirement can be derived from something in its path.
  ///
  /// Derived requirements will not be recorded in a minimized generic
  /// signature, because the information can be re-derived by following the
  /// path.
  bool isDerivedRequirement() const;

  /// Same as above, but we consider RequirementSignatureSelf to not be
  /// derived.
  bool isDerivedNonRootRequirement() const;

  /// Whether we should diagnose a redundant constraint based on this
  /// requirement source.
  ///
  /// \param primary Whether this is the "primary" requirement source, on which
  /// a "redundant constraint" warning would be emitted vs. the requirement
  /// source that would be used for the accompanying note.
  bool shouldDiagnoseRedundancy(bool primary) const;

  /// Determine whether the given derived requirement \c source, when rooted at
  /// the potential archetype \c pa, is actually derived from the same
  /// requirement. Such "self-derived" requirements do not make the original
  /// requirement redundant, because without said original requirement, the
  /// derived requirement ceases to hold.
  bool isSelfDerivedSource(GenericSignatureBuilder &builder,
                           Type type) const;

  /// For a requirement source that describes the requirement \c type:proto,
  /// retrieve the minimal subpath of this requirement source that will
  /// compute that requirement.
  ///
  /// When the result is different from (i.e., a subpath of) \c this or is
  /// nullptr (indicating an embedded, distinct self-derived subpath), the
  /// conformance requirement is considered to be "self-derived".
  const RequirementSource *getMinimalConformanceSource(
                                            GenericSignatureBuilder &builder,
                                            Type type,
                                            ProtocolDecl *proto) const;

  /// Retrieve a source location that corresponds to the requirement.
  SourceLoc getLoc() const;

  /// Compare two requirement sources to determine which has the more
  /// optimal path.
  ///
  /// \returns -1 if the \c this is better, 1 if the \c other is better, and 0
  /// if they are equivalent in length.
  int compare(const RequirementSource *other) const;

  /// Retrieve the written requirement location, if there is one.
  SourceLoc getSourceLoc() const {
    if (!hasTrailingSourceLoc) return SourceLoc();
    return getTrailingObjects<SourceLoc>()[0];
  }

  /// Retrieve the type stored in this requirement.
  Type getStoredType() const;

  /// Retrieve the protocol for this requirement, if there is one.
  ProtocolDecl *getProtocolDecl() const;

  /// Retrieve the protocol conformance for this requirement, if there is one.
  ProtocolConformanceRef getProtocolConformance() const {
    assert(storageKind == StorageKind::ProtocolConformance);
    return ProtocolConformanceRef::getFromOpaqueValue(storage.conformance);
  }

  /// Retrieve the associated type declaration for this requirement, if there
  /// is one.
  AssociatedTypeDecl *getAssociatedType() const {
    if (storageKind != StorageKind::AssociatedTypeDecl) return nullptr;
    return storage.assocType;
  }

  /// Profiling support for \c FoldingSet.
  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, kind, parent, getOpaqueStorage1(), getOpaqueStorage2(),
            getOpaqueStorage3());
  }

  /// Profiling support for \c FoldingSet.
  static void Profile(llvm::FoldingSetNodeID &ID, Kind kind,
                      const RequirementSource *parent, const void *storage1,
                      const void *storage2, const void *storage3) {
    ID.AddInteger(kind);
    ID.AddPointer(parent);
    ID.AddPointer(storage1);
    ID.AddPointer(storage2);
    ID.AddPointer(storage3);
  }

  SWIFT_DEBUG_DUMP;
  SWIFT_DEBUG_DUMPER(print());

  /// Dump the requirement source.
  void dump(llvm::raw_ostream &out, SourceManager *SrcMgr,
            unsigned indent) const;

  /// Print the requirement source (shorter form)
  void print(llvm::raw_ostream &out, SourceManager *SrcMgr) const;
};

/// A requirement source that potentially lacks a root \c PotentialArchetype.
/// The root will be supplied as soon as the appropriate dependent type is
/// resolved.
class GenericSignatureBuilder::FloatingRequirementSource {
  enum Kind : uint8_t {
    /// A fully-resolved requirement source, which does not need a root.
    Resolved,
    /// An explicit requirement in a generic signature.
    Explicit,
    /// A requirement inferred from a concrete type application in a
    /// generic signature.
    Inferred,
    /// An explicit requirement written inside a protocol.
    ProtocolRequirement,
    /// A requirement inferred from a concrete type application inside a
    /// protocol.
    InferredProtocolRequirement,
    /// A requirement source for a nested-type-name match introduced by
    /// the given source.
    NestedTypeNameMatch,
  } kind;

  const RequirementSource *source;
  SourceLoc loc;

  // Additional storage for an abstract protocol requirement.
  union {
    ProtocolDecl *protocol = nullptr;
    Identifier nestedName;
  };

  FloatingRequirementSource(Kind kind, const RequirementSource *source)
    : kind(kind), source(source) { }

public:
  /// Implicit conversion from a resolved requirement source.
  FloatingRequirementSource(const RequirementSource *source)
    : FloatingRequirementSource(Resolved, source) { }

  static FloatingRequirementSource forAbstract() {
    return { Explicit, nullptr };
  }

  static FloatingRequirementSource forExplicit(SourceLoc loc) {
    FloatingRequirementSource result{ Explicit, nullptr };
    result.loc = loc;
    return result;
  }

  static FloatingRequirementSource forInferred(SourceLoc loc) {
    FloatingRequirementSource result{ Inferred, nullptr };
    result.loc = loc;
    return result;
  }

  static FloatingRequirementSource viaProtocolRequirement(
                                     const RequirementSource *base,
                                     ProtocolDecl *inProtocol,
                                     bool inferred) {
    auto kind = (inferred ? InferredProtocolRequirement : ProtocolRequirement);
    FloatingRequirementSource result{ kind, base };
    result.protocol = inProtocol;
    return result;
  }

  static FloatingRequirementSource viaProtocolRequirement(
                                     const RequirementSource *base,
                                     ProtocolDecl *inProtocol,
                                     SourceLoc written,
                                     bool inferred) {
    auto kind = (inferred ? InferredProtocolRequirement : ProtocolRequirement);
    FloatingRequirementSource result{ kind, base };
    result.protocol = inProtocol;
    result.loc = written;
    return result;
  }

  static FloatingRequirementSource forNestedTypeNameMatch(
                                     Identifier nestedName) {
    FloatingRequirementSource result{ NestedTypeNameMatch, nullptr };
    result.nestedName = nestedName;
    return result;
  };

  /// Retrieve the complete requirement source rooted at the given type.
  const RequirementSource *getSource(GenericSignatureBuilder &builder,
                                     ResolvedType type) const;

  /// Retrieve the source location for this requirement.
  SourceLoc getLoc() const;

  /// Whether this is an explicitly-stated requirement.
  bool isExplicit() const;

  /// Whether this is a derived requirement.
  bool isDerived() const;

  /// Whether this is a top-level requirement written in source.
  /// FIXME: This is a hack because expandConformanceRequirement()
  /// is too eager; we should remove this once we fix it properly.
  bool isTopLevel() const { return kind == Explicit; }

  /// Return the "inferred" version of this source, if it isn't already
  /// inferred.
  FloatingRequirementSource asInferred(const TypeRepr *typeRepr) const;

  /// Whether this requirement source is recursive.
  bool isRecursive(GenericSignatureBuilder &builder) const;
};

/// Describes a specific constraint on a particular type.
template<typename T>
struct GenericSignatureBuilder::Constraint {
  /// The specific subject of the constraint.
  ///
  /// This may either be a (resolved) dependent type or the potential
  /// archetype that it resolves to.
  mutable UnresolvedType subject;

  /// A value used to describe the constraint.
  T value;

  /// The requirement source used to derive this constraint.
  const RequirementSource *source;

  /// Retrieve the dependent type describing the subject of the constraint.
  Type getSubjectDependentType(
                       TypeArrayView<GenericTypeParamType> genericParams) const;

  /// Determine whether the subject is equivalence to the given type.
  bool isSubjectEqualTo(Type type) const;

  /// Determine whether the subject is equivalence to the given type.
  bool isSubjectEqualTo(const PotentialArchetype *pa) const;

  /// Determine whether this constraint has the same subject as the
  /// given constraint.
  bool hasSameSubjectAs(const Constraint<T> &other) const;
};

class GenericSignatureBuilder::PotentialArchetype {
  /// The parent of this potential archetype, for a nested type.
  PotentialArchetype* parent;

  /// The representative of the equivalence class of potential archetypes
  /// to which this potential archetype belongs, or (for the representative)
  /// the equivalence class itself.
  mutable llvm::PointerUnion<PotentialArchetype *, EquivalenceClass *>
    representativeOrEquivClass;

  mutable CanType depType;

  /// A stored nested type.
  struct StoredNestedType {
    /// The potential archetypes describing this nested type, all of which
    /// are equivalent.
    llvm::TinyPtrVector<PotentialArchetype *> archetypes;

    typedef llvm::TinyPtrVector<PotentialArchetype *>::iterator iterator;
    iterator begin() { return archetypes.begin(); }
    iterator end() { return archetypes.end(); }

    typedef llvm::TinyPtrVector<PotentialArchetype *>::const_iterator
      const_iterator;
    const_iterator begin() const { return archetypes.begin(); }
    const_iterator end() const { return archetypes.end(); }

    PotentialArchetype *front() const { return archetypes.front(); }
    PotentialArchetype *back() const { return archetypes.back(); }

    unsigned size() const { return archetypes.size(); }
    bool empty() const { return archetypes.empty(); }

    void push_back(PotentialArchetype *pa) {
      archetypes.push_back(pa);
    }
  };

  /// The set of nested types of this archetype.
  ///
  /// For a given nested type name, there may be multiple potential archetypes
  /// corresponding to different associated types (from different protocols)
  /// that share a name.
  llvm::MapVector<Identifier, StoredNestedType> NestedTypes;

  /// Construct a new potential archetype for a concrete declaration.
  PotentialArchetype(PotentialArchetype *parent, AssociatedTypeDecl *assocType);

  /// Construct a new potential archetype for a generic parameter.
  explicit PotentialArchetype(GenericTypeParamType *genericParam);

public:
  /// Retrieve the representative for this archetype, performing
  /// path compression on the way.
  PotentialArchetype *getRepresentative() const;

  friend class GenericSignatureBuilder;
  friend class GenericSignature;

public:
  ~PotentialArchetype();

  /// Retrieve the debug name of this potential archetype.
  std::string getDebugName() const;

  /// Retrieve the parent of this potential archetype, which will be non-null
  /// when this potential archetype is an associated type.
  PotentialArchetype *getParent() const { 
    return parent;
  }

  /// Retrieve the type declaration to which this nested type was resolved.
  AssociatedTypeDecl *getResolvedType() const {
    return cast<DependentMemberType>(depType)->getAssocType();
  }

  /// Determine whether this is a generic parameter.
  bool isGenericParam() const {
    return parent == nullptr;
  }

  /// Retrieve the set of nested types.
  const llvm::MapVector<Identifier, StoredNestedType> &getNestedTypes() const {
    return NestedTypes;
  }

  /// Determine the nesting depth of this potential archetype, e.g.,
  /// the number of associated type references.
  unsigned getNestingDepth() const;

  /// Determine whether two potential archetypes are in the same equivalence
  /// class.
  bool isInSameEquivalenceClassAs(const PotentialArchetype *other) const {
    return getRepresentative() == other->getRepresentative();
  }

  /// Retrieve the equivalence class, if it's already present.
  ///
  /// Otherwise, return null.
  EquivalenceClass *getEquivalenceClassIfPresent() const {
    return getRepresentative()->representativeOrEquivClass
             .dyn_cast<EquivalenceClass *>();
  }

  /// Retrieve or create the equivalence class.
  EquivalenceClass *getOrCreateEquivalenceClass(
                                    GenericSignatureBuilder &builder) const;

  /// Retrieve the equivalence class containing this potential archetype.
  TinyPtrVector<PotentialArchetype *> getEquivalenceClassMembers() const {
    if (auto equivClass = getEquivalenceClassIfPresent())
      return equivClass->members;

    return TinyPtrVector<PotentialArchetype *>(
                                       const_cast<PotentialArchetype *>(this));
  }

  /// Update the named nested type when we know this type conforms to the given
  /// protocol.
  ///
  /// \returns the potential archetype associated with the associated
  /// type of the given protocol, unless the \c kind implies that
  /// a potential archetype should not be created if it's missing.
  PotentialArchetype *
  getOrCreateNestedType(GenericSignatureBuilder &builder,
                        AssociatedTypeDecl *assocType,
                        ArchetypeResolutionKind kind);

  /// Retrieve the dependent type that describes this potential
  /// archetype.
  CanType getDependentType() const {
    return depType;
  }

  /// Retrieve the dependent type that describes this potential
  /// archetype.
  ///
  /// \param genericParams The set of generic parameters to use in the resulting
  /// dependent type.
  Type getDependentType(TypeArrayView<GenericTypeParamType> genericParams) const;

  /// True if the potential archetype has been bound by a concrete type
  /// constraint.
  bool isConcreteType() const {
    if (auto equivClass = getEquivalenceClassIfPresent())
      return static_cast<bool>(equivClass->concreteType);

    return false;
  }

  SWIFT_DEBUG_DUMP;

  void dump(llvm::raw_ostream &Out, SourceManager *SrcMgr,
            unsigned Indent) const;

  friend class GenericSignatureBuilder;
};

template <typename C>
bool GenericSignatureBuilder::Constraint<C>::isSubjectEqualTo(Type T) const {
  return getSubjectDependentType({ })->isEqual(T);
}

template <typename T>
bool GenericSignatureBuilder::Constraint<T>::isSubjectEqualTo(const GenericSignatureBuilder::PotentialArchetype *PA) const {
  return getSubjectDependentType({ })->isEqual(PA->getDependentType({ }));
}

template <typename T>
bool GenericSignatureBuilder::Constraint<T>::hasSameSubjectAs(const GenericSignatureBuilder::Constraint<T> &C) const {
  return getSubjectDependentType({ })->isEqual(C.getSubjectDependentType({ }));
}

/// Describes a requirement whose processing has been delayed for some reason.
class GenericSignatureBuilder::DelayedRequirement {
public:
  enum Kind {
    /// A type requirement, which may be a conformance or a superclass
    /// requirement.
    Type,

    /// A layout requirement.
    Layout,

    /// A same-type requirement.
    SameType,
  };

  Kind kind;
  UnresolvedType lhs;
  UnresolvedRequirementRHS rhs;
  FloatingRequirementSource source;

  /// Dump a debugging representation of this delayed requirement class.
  void dump(llvm::raw_ostream &out) const;

  SWIFT_DEBUG_DUMP;
};

/// Whether the given constraint result signals an error.
inline bool isErrorResult(GenericSignatureBuilder::ConstraintResult result) {
  switch (result) {
  case GenericSignatureBuilder::ConstraintResult::Concrete:
  case GenericSignatureBuilder::ConstraintResult::Conflicting:
    return true;

  case GenericSignatureBuilder::ConstraintResult::Resolved:
  case GenericSignatureBuilder::ConstraintResult::Unresolved:
    return false;
  }
  llvm_unreachable("unhandled result");
}

template<typename T>
Type GenericSignatureBuilder::Constraint<T>::getSubjectDependentType(
                      TypeArrayView<GenericTypeParamType> genericParams) const {
  if (auto type = subject.dyn_cast<Type>())
    return type;

  return subject.get<PotentialArchetype *>()->getDependentType(genericParams);
}

} // end namespace swift

#endif
