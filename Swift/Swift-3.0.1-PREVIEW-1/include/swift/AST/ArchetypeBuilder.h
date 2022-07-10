//===--- ArchetypeBuilder.h - Generic Archetype Builder ---------*- C++ -*-===//
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
// Support for collecting a set of generic requirements, both explicitly stated
// and inferred, and computing the archetypes and required witness tables from
// those requirements.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_ARCHETYPEBUILDER_H
#define SWIFT_ARCHETYPEBUILDER_H

#include "swift/AST/Identifier.h"
#include "swift/AST/Types.h"
#include "swift/AST/TypeLoc.h"
#include "swift/Basic/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/TinyPtrVector.h"
#include <functional>
#include <memory>

namespace swift {

class AbstractTypeParamDecl;
class ArchetypeType;
class AssociatedTypeDecl;
class DeclContext;
class DependentMemberType;
class GenericParamList;
class GenericSignature;
class GenericTypeParamDecl;
class GenericTypeParamType;
class LazyResolver;
class ModuleDecl;
class Pattern;
class ProtocolConformance;
class ProtocolDecl;
class Requirement;
class RequirementRepr;
class SILModule;
class SourceLoc;
class Type;
class TypeRepr;
class ASTContext;
class DiagnosticEngine;

/// Describes how a requirement was determined.
class RequirementSource {
public:
  enum Kind : unsigned char {
    /// The requirement was explicitly stated in the generic parameter
    /// clause.
    Explicit,
    /// The requirement was explicitly stated in the generic parameter clause
    /// but is redundant with some other requirement.
    Redundant,
    /// The requirement was part of a protocol requirement, e.g., an
    /// inherited protocol or a requirement on an associated type.
    Protocol,
    /// 
    /// The requirement was inferred from part of the signature.
    Inferred,
    /// The requirement came from an outer scope.
    /// FIXME: eliminate this in favor of keeping requirement sources in 
    /// GenericSignatures, at least non-canonical ones?
    OuterScope,
  };

  RequirementSource(Kind kind, SourceLoc loc) : StoredKind(kind), Loc(loc) { }

  /// Retrieve the kind of requirement source.
  Kind getKind() const { return StoredKind; }

  /// Set the kind of the requirement source.
  void setKind(Kind kind) { StoredKind = kind; }

  /// Retrieve the source location at which the requirement originated.
  SourceLoc getLoc() const { return Loc; }

  LLVM_ATTRIBUTE_DEPRECATED(
      void dump(SourceManager *srcMgr) const,
      "only for use within the debugger");

  /// Dump the requirement source.
  void dump(llvm::raw_ostream &out, SourceManager *srcMgr) const;

private:
  Kind StoredKind;
  SourceLoc Loc;
};

/// \brief Collects a set of requirements of generic parameters, both explicitly
/// stated and inferred, and determines the set of archetypes for each of
/// the generic parameters.
class ArchetypeBuilder {
public:
  /// Describes a potential archetype, which stands in for a generic parameter
  /// type or some type derived from it.
  class PotentialArchetype;

private:
  class InferRequirementsWalker;
  friend class InferRequirementsWalker;

  ModuleDecl &Mod;
  ASTContext &Context;
  DiagnosticEngine &Diags;
  struct Implementation;
  std::unique_ptr<Implementation> Impl;

  ArchetypeBuilder(const ArchetypeBuilder &) = delete;
  ArchetypeBuilder &operator=(const ArchetypeBuilder &) = delete;

  /// \brief Add a new conformance requirement specifying that the given
  /// potential archetype conforms to the given protocol.
  bool addConformanceRequirement(PotentialArchetype *T,
                                 ProtocolDecl *Proto,
                                 RequirementSource Source);

  bool addConformanceRequirement(PotentialArchetype *T,
                                 ProtocolDecl *Proto,
                                 RequirementSource Source,
                                llvm::SmallPtrSetImpl<ProtocolDecl *> &Visited);

public:
  /// \brief Add a new conformance requirement specifying that the given
  /// potential archetypes are equivalent.
  bool addSameTypeRequirementBetweenArchetypes(PotentialArchetype *T1,
                                               PotentialArchetype *T2,
                                               RequirementSource Source);
  
  /// \brief Add a new conformance requirement specifying that the given
  /// potential archetype is bound to a concrete type.
  bool addSameTypeRequirementToConcrete(PotentialArchetype *T,
                                        Type Concrete,
                                        RequirementSource Source);

private:
  /// \brief Add a new superclass requirement specifying that the given
  /// potential archetype has the given type as an ancestor.
  bool addSuperclassRequirement(PotentialArchetype *T, 
                                Type Superclass,
                                RequirementSource Source);

  /// \brief Add a new same-type requirement specifying that the given potential
  /// archetypes should map to the equivalent archetype.
  bool addSameTypeRequirement(Type T1, Type T2, RequirementSource Source);

  /// Add the requirements placed on the given abstract type parameter
  /// to the given potential archetype.
  bool addAbstractTypeParamRequirements(
         AbstractTypeParamDecl *decl,
         PotentialArchetype *pa,
         RequirementSource::Kind kind,
         llvm::SmallPtrSetImpl<ProtocolDecl *> &visited);

  /// Visit all of the types that show up in the list of inherited
  /// types.
  ///
  /// \returns true if any of the invocations of \c visitor returned true.
  bool visitInherited(ArrayRef<TypeLoc> inheritedTypes,
                      llvm::function_ref<bool(Type, SourceLoc)> visitor);

  /// Visit all of the potential archetypes.
  template<typename F>
  void visitPotentialArchetypes(F f);

public:
  /// Construct a new archetype builder.
  ///
  /// \param mod The module in which the builder will create archetypes.
  ///
  /// \param diags The diagnostics entity to use.
  ArchetypeBuilder(ModuleDecl &mod, DiagnosticEngine &diags);

  ArchetypeBuilder(ArchetypeBuilder &&);
  ~ArchetypeBuilder();

  /// Retrieve the AST context.
  ASTContext &getASTContext() const { return Context; }

  /// Retrieve the module.
  ModuleDecl &getModule() const { return Mod; }

  /// Retrieve the lazy resolver, if there is one.
  LazyResolver *getLazyResolver() const;

  /// Enumerate the requirements that describe the signature of this
  /// archetype builder.
  ///
  /// \param f A function object that will be passed each requirement
  /// and requirement source.
  void enumerateRequirements(llvm::function_ref<
                      void (RequirementKind kind,
                            PotentialArchetype *archetype,
                            llvm::PointerUnion<Type, PotentialArchetype *> type,
                            RequirementSource source)> f);
  

private:
  PotentialArchetype *addGenericParameter(GenericTypeParamType *GenericParam,
                                          ProtocolDecl *RootProtocol,
                                          Identifier ParamName);

public:
  /// \brief Add a new generic parameter for which there may be requirements.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool addGenericParameter(GenericTypeParamDecl *GenericParam);

  /// Add the requirements placed on the given abstract type parameter
  /// to the given potential archetype.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool addGenericParameterRequirements(GenericTypeParamDecl *GenericParam);

  /// \brief Add a new generic parameter for which there may be requirements.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool addGenericParameter(GenericTypeParamType *GenericParam);
  
  /// \brief Add a new requirement.
  ///
  /// \returns true if this requirement makes the set of requirements
  /// inconsistent, in which case a diagnostic will have been issued.
  bool addRequirement(const RequirementRepr &Req);

  /// \brief Add an already-checked requirement.
  ///
  /// Adding an already-checked requirement cannot fail. This is used to
  /// re-inject requirements from outer contexts.
  void addRequirement(const Requirement &req, RequirementSource source);
  
  /// \brief Add all of a generic signature's parameters and requirements.
  ///
  /// FIXME: Requirements from the generic signature are treated as coming from
  /// an outer scope in order to avoid disturbing the AllDependentTypes.
  /// Setting \c treatRequirementsAsExplicit to true disables this behavior.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool addGenericSignature(GenericSignature *sig, bool adoptArchetypes,
                           bool treatRequirementsAsExplicit = false);

  /// \brief Get a generic signature based on the provided complete list
  /// of generic parameter types.
  ///
  /// \returns a generic signature build based on the provided list of
  ///          generic parameter types.
  GenericSignature *
  getGenericSignature(ArrayRef<GenericTypeParamType *> genericParamsTypes);

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
  ///
  /// \returns true if an error occurred, false otherwise.
  bool inferRequirements(TypeLoc type, GenericParamList *genericParams);

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
  ///
  /// \returns true if an error occurred, false otherwise.
  bool inferRequirements(ParameterList *params,GenericParamList *genericParams);

  /// Finalize the set of requirements, performing any remaining checking
  /// required before generating archetypes.
  ///
  /// \returns true if an error occurs, false otherwise.
  bool finalize(SourceLoc loc);

  /// \brief Resolve the given type to the potential archetype it names.
  ///
  /// This routine will synthesize nested types as required to refer to a
  /// potential archetype, even in cases where no requirement specifies the
  /// requirement for such an archetype. FIXME: The failure to include such a
  /// requirement will be diagnosed at some point later (when the types in the
  /// signature are fully resolved).
  ///
  /// For any type that cannot refer to an archetype, this routine returns null.
  PotentialArchetype *resolveArchetype(Type type);

  /// \brief Resolve the given dependent type using our context archetypes.
  ///
  /// Given an arbitrary type, this will substitute dependent type parameters
  /// structurally with their corresponding archetypes and resolve dependent
  /// member types to the appropriate associated types.
  Type substDependentType(Type type);
  
  /// \brief Retrieve the archetype that corresponds to the given generic
  /// parameter.
  ArchetypeType *getArchetype(GenericTypeParamDecl *GenericParam);

  /// \brief Retrieve the array of all of the archetypes produced during
  /// archetype assignment. The 'primary' archetypes will occur first in this
  /// list.
  ArrayRef<ArchetypeType *> getAllArchetypes();
  
  /// Map an interface type to a contextual type.
  static Type mapTypeIntoContext(const DeclContext *dc, Type type,
                                 LazyResolver *resolver = nullptr);

  /// Map an interface type to a contextual type.
  static Type mapTypeIntoContext(ModuleDecl *M,
                                 GenericParamList *genericParams,
                                 Type type,
                                 LazyResolver *resolver = nullptr);

  /// Map a contextual type to an interface type.
  static Type mapTypeOutOfContext(const DeclContext *dc, Type type);

  /// Map a contextual type to an interface type.
  static Type mapTypeOutOfContext(ModuleDecl *M,
                                  GenericParamList *genericParams,
                                  Type type);

  using SameTypeRequirement
    = std::pair<PotentialArchetype *,
                PointerUnion<Type, PotentialArchetype*>>;
  
  /// Retrieve the set of same-type requirements that apply to the potential
  /// archetypes known to this builder.
  ArrayRef<SameTypeRequirement> getSameTypeRequirements() const;

  // FIXME: Compute the set of 'extra' witness tables needed to express this
  // requirement set.

  /// \brief Dump all of the requirements, both specified and inferred.
  LLVM_ATTRIBUTE_DEPRECATED(
      void dump(),
      "only for use within the debugger");

  /// Dump all of the requirements to the given output stream.
  void dump(llvm::raw_ostream &out);

  // In SILFunction.cpp:
  
  /// \brief Resolve the given dependent type using our context archetypes.
  ///
  /// Given an arbitrary type, this will substitute dependent type parameters
  /// structurally with their corresponding archetypes and resolve dependent
  /// member types to the appropriate associated types. It will reabstract
  /// dependent types according to the abstraction level of their associated
  /// type requirements.
  SILType substDependentType(SILModule &M,
                             SILType type);
};

class ArchetypeBuilder::PotentialArchetype {
  /// Either the parent of this potential archetype (for an associated
  /// type) or the generic type parameter type to which this potential
  /// archetype corresponds.
  llvm::PointerUnion<PotentialArchetype*, GenericTypeParamType*> ParentOrParam;

  /// The root protocol with which this potential archetype is associated.
  ProtocolDecl *RootProtocol = nullptr;

  /// \brief The name of this potential archetype or, for an
  /// associated type, the declaration of the associated type to which
  /// this potential archetype has been resolved. Or, for a type alias,
  /// the type alias decl.
  llvm::PointerUnion3<Identifier, AssociatedTypeDecl *,
                      TypeAliasDecl *> NameOrAssociatedType;

  /// \brief The representative of the equivalent class of potential archetypes
  /// to which this potential archetype belongs.
  PotentialArchetype *Representative;

  /// \brief The source of a same-type requirement.
  Optional<RequirementSource> SameTypeSource;

  /// \brief The superclass of this archetype, if specified.
  Type Superclass;

  /// The source of the superclass requirement.
  Optional<RequirementSource> SuperclassSource;

  /// \brief The list of protocols to which this archetype will conform.
  llvm::MapVector<ProtocolDecl *, RequirementSource> ConformsTo;

  /// \brief The set of nested types of this archetype.
  ///
  /// For a given nested type name, there may be multiple potential archetypes
  /// corresponding to different associated types (from different protocols)
  /// that share a name.
  llvm::MapVector<Identifier, llvm::TinyPtrVector<PotentialArchetype *>>
    NestedTypes;

  /// \brief The actual archetype, once it has been assigned, or the concrete
  /// type that the parameter was same-type constrained to.
  ArchetypeType::NestedType ArchetypeOrConcreteType;

  /// \brief Recursively conforms to itself.
  unsigned IsRecursive : 1;

  /// Whether this potential archetype is invalid, e.g., because it could not
  /// be resolved.
  unsigned Invalid : 1;

  /// Whether we are currently substituting into the concrete type of
  /// this potential archetype.
  unsigned SubstitutingConcreteType : 1;

  /// Whether we have detected recursion during the substitution of
  /// the concrete type.
  unsigned RecursiveConcreteType : 1;

  /// Whether we have detected recursion during the substitution of
  /// the superclass type.
  unsigned RecursiveSuperclassType : 1;

  /// Whether we have renamed this (nested) type due to typo correction.
  unsigned Renamed : 1;

  /// The equivalence class of this potential archetype.
  llvm::TinyPtrVector<PotentialArchetype *> EquivalenceClass;

  /// \brief Construct a new potential archetype for an unresolved
  /// associated type.
  PotentialArchetype(PotentialArchetype *Parent, Identifier Name)
    : ParentOrParam(Parent), NameOrAssociatedType(Name), Representative(this),
      IsRecursive(false), Invalid(false), SubstitutingConcreteType(false),
      RecursiveConcreteType(false), RecursiveSuperclassType(false),
      Renamed(false)
  { 
    assert(Parent != nullptr && "Not an associated type?");
    EquivalenceClass.push_back(this);
  }

  /// \brief Construct a new potential archetype for an associated type.
  PotentialArchetype(PotentialArchetype *Parent, AssociatedTypeDecl *AssocType)
    : ParentOrParam(Parent), NameOrAssociatedType(AssocType), 
      Representative(this), IsRecursive(false), Invalid(false),
      SubstitutingConcreteType(false), RecursiveConcreteType(false),
      RecursiveSuperclassType(false), Renamed(false)
  { 
    assert(Parent != nullptr && "Not an associated type?");
    EquivalenceClass.push_back(this);
  }

  /// \brief Construct a new potential archetype for a type alias.
  PotentialArchetype(PotentialArchetype *Parent, TypeAliasDecl *TypeAlias)
    : ParentOrParam(Parent), NameOrAssociatedType(TypeAlias),
      Representative(this), IsRecursive(false), Invalid(false),
      SubstitutingConcreteType(false), RecursiveConcreteType(false),
      RecursiveSuperclassType(false), Renamed(false)
  {
    assert(Parent != nullptr && "Not an associated type?");
    EquivalenceClass.push_back(this);
  }

  /// \brief Construct a new potential archetype for a generic parameter.
  PotentialArchetype(GenericTypeParamType *GenericParam, 
                     ProtocolDecl *RootProtocol,
                     Identifier Name)
    : ParentOrParam(GenericParam), RootProtocol(RootProtocol), 
      NameOrAssociatedType(Name), Representative(this), IsRecursive(false),
      Invalid(false), SubstitutingConcreteType(false),
      RecursiveConcreteType(false), RecursiveSuperclassType(false),
      Renamed(false)
  {
    EquivalenceClass.push_back(this);
  }

  /// \brief Recursively build the full name.
  void buildFullName(bool forDebug, SmallVectorImpl<char> &result) const;

public:
  ~PotentialArchetype();

  /// \brief Retrieve the name of this potential archetype.
  Identifier getName() const;

  /// \brief Retrieve the full display name of this potential archetype.
  std::string getFullName() const;

  /// \brief Retrieve the debug name of this potential archetype.
  std::string getDebugName() const;

  /// Retrieve the parent of this potential archetype, which will be non-null
  /// when this potential archetype is an associated type.
  PotentialArchetype *getParent() const { 
    return ParentOrParam.dyn_cast<PotentialArchetype *>(); 
  }

  /// Retrieve the generic parameter at the root of this potential archetype.
  GenericTypeParamType *getRootParam() const {
    if (auto parent = getParent())
      return parent->getRootParam();

    return getGenericParam();
  }

  /// Retrieve the associated type to which this potential archetype
  /// has been resolved.
  AssociatedTypeDecl *getResolvedAssociatedType() const {
    assert(getParent() && "Not an associated type");
    return NameOrAssociatedType.dyn_cast<AssociatedTypeDecl *>();
  }

  /// Resolve the potential archetype to the given associated type.
  void resolveAssociatedType(AssociatedTypeDecl *assocType,
                             ArchetypeBuilder &builder);

  /// Retrieve the generic type parameter for this potential
  /// archetype, if it corresponds to a generic parameter.
  GenericTypeParamType *getGenericParam() const {
    return ParentOrParam.dyn_cast<GenericTypeParamType *>(); 
  }
  
  /// Retrieve the type alias.
  TypeAliasDecl *getTypeAliasDecl() const {
    return NameOrAssociatedType.dyn_cast<TypeAliasDecl *>();
  }

  /// Retrieve the set of protocols to which this type conforms.
  const llvm::MapVector<ProtocolDecl *, RequirementSource> &
  getConformsTo() const {
    return ConformsTo;
  }

  /// Add a conformance to this potential archetype.
  ///
  /// \returns true if the conformance was new, false if it already existed.
  bool addConformance(ProtocolDecl *proto, const RequirementSource &source,
                      ArchetypeBuilder &builder);

  /// Retrieve the superclass of this archetype.
  Type getSuperclass() const { return Superclass; }

  /// Retrieve the requirement source for the superclass requirement.
  const RequirementSource &getSuperclassSource() const {
    return *SuperclassSource;
  } 

  /// Retrieve the set of nested types.
  const llvm::MapVector<Identifier, llvm::TinyPtrVector<PotentialArchetype *>> &
  getNestedTypes() const{
    return NestedTypes;
  }

  /// \brief Determine the nesting depth of this potential archetype, e.g.,
  /// the number of associated type references.
  unsigned getNestingDepth() const;

  /// \brief Retrieve the representative for this archetype, performing
  /// path compression on the way.
  PotentialArchetype *getRepresentative();

  /// Retrieve the equivalence class containing this potential archetype.
  ArrayRef<PotentialArchetype *> getEquivalenceClass() {
    return getRepresentative()->EquivalenceClass;
  }

  /// \brief Retrieve the potential archetype to be used as the anchor for
  /// potential archetype computations.
  PotentialArchetype *getArchetypeAnchor();

  /// Retrieve the source of the same-type constraint that applies to this
  /// potential archetype.
  const RequirementSource &getSameTypeSource() const {
    return *SameTypeSource;
  }

  /// \brief Retrieve (or create) a nested type with the given name.
  PotentialArchetype *getNestedType(Identifier Name,
                                    ArchetypeBuilder &builder);

  /// \brief Retrieve (or build) the type corresponding to the potential
  /// archetype.
  ArchetypeType::NestedType getType(ArchetypeBuilder &builder);

  /// Retrieve the dependent type that describes this potential
  /// archetype.
  Type getDependentType(ArchetypeBuilder &builder, bool allowUnresolved);

  /// True if the potential archetype has been bound by a concrete type
  /// constraint.
  bool isConcreteType() const {
    if (Representative != this)
      return Representative->isConcreteType();

    return ArchetypeOrConcreteType.isConcreteType();
  }
  
  /// Get the concrete type this potential archetype is constrained to.
  Type getConcreteType() const {
    assert(isConcreteType());
    if (Representative != this)
      return Representative->getConcreteType();
    return ArchetypeOrConcreteType.getAsConcreteType();
  }

  void setIsRecursive() { IsRecursive = true; }
  bool isRecursive() { return IsRecursive; }

  bool isInvalid() { return Invalid; }

  void setInvalid() { Invalid = true; }

  /// Determine whether this archetype was renamed due to typo
  /// correction. If so, \c getName() retrieves the new name.
  bool wasRenamed() { return Renamed; }

  /// Note that this potential archetype was renamed (due to typo
  /// correction), providing the new name.
  void setRenamed(Identifier newName) {
    NameOrAssociatedType = newName;
    Renamed = true;
  }

  /// Whether this potential archetype makes a better archetype anchor than
  /// the given archetype anchor.
  bool isBetterArchetypeAnchor(PotentialArchetype *other);

  void dump(llvm::raw_ostream &Out, SourceManager *SrcMgr,
            unsigned Indent);

  friend class ArchetypeBuilder;

private:
  bool hasConcreteTypeInPath() const;
};

} // end namespace swift

#endif
