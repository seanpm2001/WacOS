//===--- GenericSignatureBuilder.cpp - Generic Requirement Builder --------===//
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
// Support for collecting a set of generic requirements, both explicitly stated
// and inferred, and computing the archetypes and required witness tables from
// those requirements.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/GenericSignatureBuilder.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/DiagnosticsSema.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/ExistentialLayout.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/LazyResolver.h"
#include "swift/AST/Module.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/TypeMatcher.h"
#include "swift/AST/TypeRepr.h"
#include "swift/AST/TypeWalker.h"
#include "swift/Basic/Defer.h"
#include "swift/Basic/Statistic.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/SaveAndRestore.h"
#include <algorithm>

using namespace swift;
using llvm::DenseMap;

/// Define this to 1 to enable expensive assertions.
#define SWIFT_GSB_EXPENSIVE_ASSERTIONS 0

namespace {
  typedef GenericSignatureBuilder::RequirementSource RequirementSource;
  typedef GenericSignatureBuilder::FloatingRequirementSource
    FloatingRequirementSource;
  typedef GenericSignatureBuilder::ConstraintResult ConstraintResult;
  typedef GenericSignatureBuilder::PotentialArchetype PotentialArchetype;
  typedef GenericSignatureBuilder::ConcreteConstraint ConcreteConstraint;
  template<typename T> using Constraint =
    GenericSignatureBuilder::Constraint<T>;
  typedef GenericSignatureBuilder::EquivalenceClass EquivalenceClass;
  typedef EquivalenceClass::DerivedSameTypeComponent DerivedSameTypeComponent;
  typedef GenericSignatureBuilder::DelayedRequirement DelayedRequirement;
  typedef GenericSignatureBuilder::ResolvedType ResolvedType;
  typedef GenericSignatureBuilder::UnresolvedType GSBUnresolvedType;
  typedef GenericSignatureBuilder::RequirementRHS RequirementRHS;
} // end anonymous namespace

namespace llvm {
  // Equivalence classes are bump-ptr-allocated.
  template <> struct ilist_alloc_traits<EquivalenceClass> {
    static void deleteNode(EquivalenceClass *ptr) { ptr->~EquivalenceClass(); }
  };
}

#define DEBUG_TYPE "Generic signature builder"
STATISTIC(NumPotentialArchetypes, "# of potential archetypes");
STATISTIC(NumConformances, "# of conformances tracked");
STATISTIC(NumConformanceConstraints, "# of conformance constraints tracked");
STATISTIC(NumSameTypeConstraints, "# of same-type constraints tracked");
STATISTIC(NumConcreteTypeConstraints,
          "# of same-type-to-concrete constraints tracked");
STATISTIC(NumSuperclassConstraints, "# of superclass constraints tracked");
STATISTIC(NumSuperclassConstraintsExtra,
          "# of superclass constraints that add no information");
STATISTIC(NumLayoutConstraints, "# of layout constraints tracked");
STATISTIC(NumLayoutConstraintsExtra,
          "# of layout constraints  that add no information");
STATISTIC(NumSelfDerived, "# of self-derived constraints removed");
STATISTIC(NumArchetypeAnchorCacheHits,
          "# of hits in the archetype anchor cache");
STATISTIC(NumArchetypeAnchorCacheMisses,
          "# of misses in the archetype anchor cache");
STATISTIC(NumNestedTypeCacheHits,
         "# of hits in the equivalence class nested type cache");
STATISTIC(NumNestedTypeCacheMisses,
         "# of misses in the equivalence class nested type cache");
STATISTIC(NumProcessDelayedRequirements,
          "# of times we process delayed requirements");
STATISTIC(NumProcessDelayedRequirementsUnchanged,
          "# of times we process delayed requirements without change");
STATISTIC(NumDelayedRequirementConcrete,
          "Delayed requirements resolved as concrete");
STATISTIC(NumDelayedRequirementResolved,
          "Delayed requirements resolved");
STATISTIC(NumDelayedRequirementUnresolved,
          "Delayed requirements left unresolved");
STATISTIC(NumConditionalRequirementsAdded,
          "# of conditional requirements added");

struct GenericSignatureBuilder::Implementation {
  /// Allocator.
  llvm::BumpPtrAllocator Allocator;

  /// The generic parameters that this generic signature builder is working
  /// with.
  SmallVector<GenericTypeParamType *, 4> GenericParams;

  /// The potential archetypes for the generic parameters in \c GenericParams.
  SmallVector<PotentialArchetype *, 4> PotentialArchetypes;

  /// The requirement sources used in this generic signature builder.
  llvm::FoldingSet<RequirementSource> RequirementSources;

  /// The set of requirements that have been delayed for some reason.
  SmallVector<DelayedRequirement, 4> DelayedRequirements;

  /// The set of equivalence classes.
  llvm::iplist<EquivalenceClass> EquivalenceClasses;

  /// Equivalence classes that are not currently being used.
  std::vector<void *> FreeEquivalenceClasses;

  /// The generation number, which is incremented whenever we successfully
  /// introduce a new constraint.
  unsigned Generation = 0;

  /// The generation at which we last processed all of the delayed requirements.
  unsigned LastProcessedGeneration = 0;

  /// Whether we are currently processing delayed requirements.
  bool ProcessingDelayedRequirements = false;

  /// Tear down an implementation.
  ~Implementation();

  /// Allocate a new equivalence class with the given representative.
  EquivalenceClass *allocateEquivalenceClass(
                                       PotentialArchetype *representative);

  /// Deallocate the given equivalence class, returning it to the free list.
  void deallocateEquivalenceClass(EquivalenceClass *equivClass);

  /// Whether there were any errors.
  bool HadAnyError = false;

  /// FIXME: Hack to work around a small number of minimization bugs.
  bool HadAnyRedundantConstraints = false;

#ifndef NDEBUG
  /// Whether we've already finalized the builder.
  bool finalized = false;
#endif
};

#pragma mark Memory management
GenericSignatureBuilder::Implementation::~Implementation() {
  for (auto pa : PotentialArchetypes)
    pa->~PotentialArchetype();
}

EquivalenceClass *
GenericSignatureBuilder::Implementation::allocateEquivalenceClass(
                                          PotentialArchetype *representative) {
  void *mem;
  if (FreeEquivalenceClasses.empty()) {
    // Allocate a new equivalence class.
    mem = Allocator.Allocate<EquivalenceClass>();
  } else {
    // Take an equivalence class from the free list.
    mem = FreeEquivalenceClasses.back();
    FreeEquivalenceClasses.pop_back();
  }

  auto equivClass = new (mem) EquivalenceClass(representative);
  EquivalenceClasses.push_back(equivClass);
  return equivClass;
}

void GenericSignatureBuilder::Implementation::deallocateEquivalenceClass(
                                               EquivalenceClass *equivClass) {
  EquivalenceClasses.erase(equivClass);
  FreeEquivalenceClasses.push_back(equivClass);
}

#pragma mark GraphViz visualization
namespace {
  /// A node in the equivalence class, used for visualization.
  struct EquivalenceClassVizNode {
    const EquivalenceClass *first;
    PotentialArchetype *second;

    operator const void *() const { return second; }
  };

  /// Iterator through the adjacent nodes in an equivalence class, for
  /// visualization.
  class EquivalenceClassVizIterator {
    using BaseIterator = const Constraint<PotentialArchetype *> *;

    EquivalenceClassVizNode node;
    BaseIterator base;
    BaseIterator baseEnd;

    void advance() {
      while (base != baseEnd && base->value != node.second)
        ++base;
    }

  public:
    using difference_type = ptrdiff_t;
    using value_type = EquivalenceClassVizNode;
    using reference = value_type;
    using pointer = value_type*;
    using iterator_category = std::forward_iterator_tag;

    EquivalenceClassVizIterator(EquivalenceClassVizNode node,
                                BaseIterator base, BaseIterator baseEnd)
        : node(node), base(base), baseEnd(baseEnd) {
      advance();
    }

    BaseIterator &getBase() { return base; }
    const BaseIterator &getBase() const { return base; }

    reference operator*() const {
      return { node.first, getBase()->value };
    }

    EquivalenceClassVizIterator& operator++() {
      ++getBase();
      advance();
      return *this;
    }

    EquivalenceClassVizIterator operator++(int) {
      EquivalenceClassVizIterator result = *this;
      ++(*this);
      return result;
    }

    friend bool operator==(const EquivalenceClassVizIterator &lhs,
                           const EquivalenceClassVizIterator &rhs) {
      return lhs.getBase() == rhs.getBase();
    }

    friend bool operator!=(const EquivalenceClassVizIterator &lhs,
                           const EquivalenceClassVizIterator &rhs) {
      return !(lhs == rhs);
    }
  };
}

namespace std {
  // FIXME: Egregious hack to work around a bogus static_assert in
  // llvm::GraphWriter. Good thing nobody else cares about this trait...
  template<>
  struct is_pointer<EquivalenceClassVizNode>
    : std::integral_constant<bool, true> { };
}

namespace llvm {
  // Visualize the same-type constraints within an equivalence class.
  template<>
  struct GraphTraits<const EquivalenceClass *> {
    using NodeRef = EquivalenceClassVizNode;

    static NodeRef getEntryNode(const EquivalenceClass *equivClass) {
      return { equivClass, equivClass->members.front() };
    }

    class nodes_iterator {
      using BaseIterator = PotentialArchetype * const *;

      const EquivalenceClass *equivClass;
      BaseIterator base;

    public:
      using difference_type = ptrdiff_t;
      using value_type = EquivalenceClassVizNode;
      using reference = value_type;
      using pointer = value_type*;
      using iterator_category = std::forward_iterator_tag;

      nodes_iterator(const EquivalenceClass *equivClass, BaseIterator base)
        : equivClass(equivClass), base(base) { }

      BaseIterator &getBase() { return base; }
      const BaseIterator &getBase() const { return base; }

      reference operator*() const {
        return { equivClass, *getBase() };
      }

      nodes_iterator& operator++() {
        ++getBase();
        return *this;
      }

      nodes_iterator operator++(int) {
        nodes_iterator result = *this;
        ++(*this);
        return result;
      }

      friend bool operator==(const nodes_iterator &lhs,
                             const nodes_iterator &rhs) {
        return lhs.getBase() == rhs.getBase();
      }

      friend bool operator!=(const nodes_iterator &lhs,
                             const nodes_iterator &rhs) {
        return lhs.getBase() != rhs.getBase();
      }
    };

    static nodes_iterator nodes_begin(const EquivalenceClass *equivClass) {
      return nodes_iterator(equivClass, equivClass->members.begin());
    }

    static nodes_iterator nodes_end(const EquivalenceClass *equivClass) {
      return nodes_iterator(equivClass, equivClass->members.end());
    }

    static unsigned size(const EquivalenceClass *equivClass) {
      return equivClass->members.size();
    }

    using ChildIteratorType = EquivalenceClassVizIterator;

    static ChildIteratorType child_begin(NodeRef node) {
      auto base = node.first->sameTypeConstraints.data();
      auto baseEnd = base + node.first->sameTypeConstraints.size();
      return ChildIteratorType(node, base, baseEnd);
    }

    static ChildIteratorType child_end(NodeRef node) {
      auto base = node.first->sameTypeConstraints.data();
      auto baseEnd = base + node.first->sameTypeConstraints.size();
      return ChildIteratorType(node, baseEnd, baseEnd);
    }
  };

  template <>
  struct DOTGraphTraits<const EquivalenceClass *>
    : public DefaultDOTGraphTraits
  {
    DOTGraphTraits(bool = false) { }

    static std::string getGraphName(const EquivalenceClass *equivClass) {
      return "Equivalence class for '" +
        equivClass->members.front()->getDebugName() + "'";
    }

    std::string getNodeLabel(EquivalenceClassVizNode node,
                             const EquivalenceClass *equivClass) const {
      return node.second->getDebugName();
    }

    static std::string getEdgeAttributes(EquivalenceClassVizNode node,
                                         EquivalenceClassVizIterator iter,
                                         const EquivalenceClass *equivClass) {
      if (iter.getBase()->source->kind
            == RequirementSource::NestedTypeNameMatch)
        return "color=\"blue\"";

      if (iter.getBase()->source->isDerivedRequirement())
        return "color=\"gray\"";

      return "color=\"red\"";
    }
  };
} // end namespace llvm

namespace {
  /// Retrieve the type described by the given unresolved tyoe.
  Type getUnresolvedType(GSBUnresolvedType type,
                         ArrayRef<GenericTypeParamType *> genericParams) {
    if (auto concrete = type.dyn_cast<Type>())
      return concrete;

    if (auto pa = type.dyn_cast<PotentialArchetype *>())
      return pa->getDependentType(genericParams);

    return Type();
  }
}

#pragma mark Requirement sources

#ifndef NDEBUG
bool RequirementSource::isAcceptableStorageKind(Kind kind,
                                                StorageKind storageKind) {
  switch (kind) {
  case Explicit:
  case Inferred:
  case RequirementSignatureSelf:
  case NestedTypeNameMatch:
  case ConcreteTypeBinding:
  case EquivalentType:
    switch (storageKind) {
    case StorageKind::StoredType:
      return true;

    case StorageKind::ProtocolConformance:
    case StorageKind::AssociatedTypeDecl:
    case StorageKind::None:
      return false;
    }

  case Parent:
    switch (storageKind) {
    case StorageKind::AssociatedTypeDecl:
      return true;

    case StorageKind::StoredType:
    case StorageKind::ProtocolConformance:
    case StorageKind::None:
      return false;
    }

  case ProtocolRequirement:
  case InferredProtocolRequirement:
    switch (storageKind) {
    case StorageKind::StoredType:
      return true;

    case StorageKind::ProtocolConformance:
    case StorageKind::AssociatedTypeDecl:
    case StorageKind::None:
      return false;
    }

  case Superclass:
  case Concrete:
    switch (storageKind) {
    case StorageKind::ProtocolConformance:
      return true;

    case StorageKind::StoredType:
    case StorageKind::AssociatedTypeDecl:
    case StorageKind::None:
      return false;
    }

  case Derived:
    switch (storageKind) {
    case StorageKind::None:
      return true;

    case StorageKind::StoredType:
    case StorageKind::ProtocolConformance:
    case StorageKind::AssociatedTypeDecl:
      return false;
    }
  }

  llvm_unreachable("Unhandled RequirementSourceKind in switch.");
}
#endif

const void *RequirementSource::getOpaqueStorage1() const {
  switch (storageKind) {
  case StorageKind::None:
    return nullptr;

  case StorageKind::ProtocolConformance:
    return storage.conformance;

  case StorageKind::StoredType:
    return storage.type;

  case StorageKind::AssociatedTypeDecl:
    return storage.assocType;
  }

  llvm_unreachable("Unhandled StorageKind in switch.");
}

const void *RequirementSource::getOpaqueStorage2() const {
  if (numTrailingObjects(OverloadToken<ProtocolDecl *>()) == 1)
    return getTrailingObjects<ProtocolDecl *>()[0];
  if (numTrailingObjects(OverloadToken<WrittenRequirementLoc>()) == 1)
    return getTrailingObjects<WrittenRequirementLoc>()[0].getOpaqueValue();

  return nullptr;
}

const void *RequirementSource::getOpaqueStorage3() const {
  if (numTrailingObjects(OverloadToken<ProtocolDecl *>()) == 1 &&
      numTrailingObjects(OverloadToken<WrittenRequirementLoc>()) == 1)
    return getTrailingObjects<WrittenRequirementLoc>()[0].getOpaqueValue();

  return nullptr;
}

bool RequirementSource::isInferredRequirement() const {
  for (auto source = this; source; source = source->parent) {
    switch (source->kind) {
    case Inferred:
    case InferredProtocolRequirement:
    case NestedTypeNameMatch:
      return true;

    case ConcreteTypeBinding:
    case EquivalentType:
      return false;

    case Concrete:
    case Explicit:
    case Parent:
    case ProtocolRequirement:
    case RequirementSignatureSelf:
    case Superclass:
    case Derived:
      break;
    }
  }

  return false;
}

unsigned RequirementSource::classifyDiagKind() const {
  if (isInferredRequirement()) return 2;
  if (isDerivedRequirement()) return 1;
  return 0;
}

bool RequirementSource::isDerivedRequirement() const {
  switch (kind) {
  case Explicit:
  case Inferred:
    return false;

  case NestedTypeNameMatch:
  case ConcreteTypeBinding:
  case Parent:
  case Superclass:
  case Concrete:
  case RequirementSignatureSelf:
  case Derived:
  case EquivalentType:
    return true;

  case ProtocolRequirement:
  case InferredProtocolRequirement:
    // Requirements based on protocol requirements are derived unless they are
    // direct children of the requirement-signature source, in which case we
    // need to keep them for the requirement signature.
    return parent->kind != RequirementSignatureSelf;
  }

  llvm_unreachable("Unhandled RequirementSourceKind in switch.");
}

bool RequirementSource::shouldDiagnoseRedundancy(bool primary) const {
  return !isInferredRequirement() && getLoc().isValid() &&
         (!primary || !isDerivedRequirement());
}

bool RequirementSource::isSelfDerivedSource(GenericSignatureBuilder &builder,
                                            Type type,
                                            bool &derivedViaConcrete) const {
  return getMinimalConformanceSource(builder, type, /*proto=*/nullptr,
                                     derivedViaConcrete)
    != this;
}

/// Replace 'Self' in the given dependent type (\c depTy) with the given
/// potential archetype, producing a new potential archetype that refers to
/// the nested type. This limited operation makes sure that it does not
/// create any new potential archetypes along the way, so it should only be
/// used in cases where we're reconstructing something that we know exists.
static Type replaceSelfWithType(Type selfType, Type depTy) {
  if (auto depMemTy = depTy->getAs<DependentMemberType>()) {
    Type baseType = replaceSelfWithType(selfType, depMemTy->getBase());

    if (auto assocType = depMemTy->getAssocType())
      return DependentMemberType::get(baseType, assocType);

    return DependentMemberType::get(baseType, depMemTy->getName());
  }

  assert(depTy->is<GenericTypeParamType>() && "missing Self?");
  return selfType;
}

/// Determine whether the given protocol requirement is self-derived when it
/// occurs within the requirement signature of its own protocol.
static bool isSelfDerivedProtocolRequirementInProtocol(
                                             const RequirementSource *source,
                                             ProtocolDecl *selfProto,
                                             GenericSignatureBuilder &builder) {
  assert(source->isProtocolRequirement());

  // This can only happen if the requirement points comes from the protocol
  // itself.
  if (source->getProtocolDecl() != selfProto) return false;

  // This only applies if the parent is not the anchor for computing the
  // requirement signature. Anywhere else, we can use the protocol requirement.
  if (source->parent->kind == RequirementSource::RequirementSignatureSelf)
    return false;

  // If the relative type of the protocol requirement itself is in the
  // same equivalence class as what we've proven with this requirement,
  // it's a self-derived requirement.
  return
    builder.resolveEquivalenceClass(source->getAffectedType(),
                                    ArchetypeResolutionKind::WellFormed) ==
      builder.resolveEquivalenceClass(source->getStoredType(),
                                      ArchetypeResolutionKind::AlreadyKnown);
}

const RequirementSource *RequirementSource::getMinimalConformanceSource(
                                             GenericSignatureBuilder &builder,
                                             Type currentType,
                                             ProtocolDecl *proto,
                                             bool &derivedViaConcrete) const {
  derivedViaConcrete = false;

  // If it's not a derived requirement, it's not self-derived.
  if (!isDerivedRequirement()) return this;

  /// Keep track of all of the requirements we've seen along the way. If
  /// we see the same requirement twice, we have found a shorter path.
  llvm::DenseMap<std::pair<EquivalenceClass *, ProtocolDecl *>,
                 const RequirementSource *>
    constraintsSeen;

  /// Note that we've now seen a new constraint (described on an equivalence
  /// class).
  auto addConstraint = [&](EquivalenceClass *equivClass, ProtocolDecl *proto,
                           const RequirementSource *source)
      -> const RequirementSource * {
    auto &storedSource = constraintsSeen[{equivClass, proto}];
    if (storedSource) return storedSource;

    storedSource = source;
    return nullptr;
  };

  // Note that we've now seen a new constraint, returning true if we've seen
  // it before.
  auto addTypeConstraint = [&](Type type, ProtocolDecl *proto,
                           const RequirementSource *source)
      -> const RequirementSource * {
    auto equivClass =
        builder.resolveEquivalenceClass(type,
                                        ArchetypeResolutionKind::WellFormed);
    assert(equivClass && "Not a well-formed type?");
    return addConstraint(equivClass, proto, source);
  };

  bool sawProtocolRequirement = false;
  ProtocolDecl *requirementSignatureSelfProto = nullptr;

  Type rootType = nullptr;
  Optional<std::pair<const RequirementSource *, const RequirementSource *>>
    redundantSubpath;
  bool isSelfDerived = visitPotentialArchetypesAlongPath(
          [&](Type parentType, const RequirementSource *source) {
    switch (source->kind) {
    case ProtocolRequirement:
    case InferredProtocolRequirement: {
      // Note that we've seen a protocol requirement.
      sawProtocolRequirement = true;

      // If the base has been made concrete, note it.
      auto parentEquivClass =
          builder.resolveEquivalenceClass(parentType,
                                          ArchetypeResolutionKind::WellFormed);
      assert(parentEquivClass && "Not a well-formed type?");

      if (parentEquivClass->concreteType)
        derivedViaConcrete = true;

      // The parent potential archetype must conform to the protocol in which
      // this requirement resides. Add this constraint.
      if (auto startOfPath =
              addConstraint(parentEquivClass, source->getProtocolDecl(),
                            source->parent)) {
        // We found a redundant subpath; record it and stop the algorithm.
        assert(startOfPath != source->parent);
        redundantSubpath = { startOfPath, source->parent };
        return true;
      }

      // If this is a self-derived protocol requirement, fail.
      if (requirementSignatureSelfProto &&
          isSelfDerivedProtocolRequirementInProtocol(
                                               source,
                                               requirementSignatureSelfProto,
                                               builder))
        return true;

      // No redundancy thus far.
      return false;
    }

    case Parent:
      // FIXME: Ad hoc detection of recursive same-type constraints.
      return !proto &&
        builder.areInSameEquivalenceClass(parentType, currentType);

    case Concrete:
    case Superclass:
    case Derived:
    case EquivalentType:
      return false;

    case RequirementSignatureSelf:
      // Note the protocol whose requirement signature the requirement is
      // based on.
      requirementSignatureSelfProto = source->getProtocolDecl();
      LLVM_FALLTHROUGH;

    case Explicit:
    case Inferred:
    case NestedTypeNameMatch:
    case ConcreteTypeBinding:
      rootType = parentType;
      return false;
    }
  }).isNull();

  // If we didn't already find a redundancy, check our end state.
  if (!redundantSubpath && proto) {
    if (auto startOfPath = addTypeConstraint(currentType, proto, this)) {
      redundantSubpath = { startOfPath, this };
      assert(startOfPath != this);
      isSelfDerived = true;
    }
  }

  // If we saw a constraint twice, it's self-derived.
  if (redundantSubpath) {
    assert(isSelfDerived && "Not considered self-derived?");
    auto shorterSource =
      withoutRedundantSubpath(builder,
                              redundantSubpath->first,
                              redundantSubpath->second);
    return shorterSource
      ->getMinimalConformanceSource(builder, currentType, proto, derivedViaConcrete);
  }

  // It's self-derived but we don't have a redundant subpath to eliminate.
  if (isSelfDerived)
    return nullptr;

  // If we haven't seen a protocol requirement, we're done.
  if (!sawProtocolRequirement) return this;

  // The root archetype might be a nested type, which implies constraints
  // for each of the protocols of the associated types referenced (if any).
  for (auto depMemTy = rootType->getAs<DependentMemberType>(); depMemTy;
       depMemTy = depMemTy->getBase()->getAs<DependentMemberType>()) {
    if (auto assocType = depMemTy->getAssocType()) {
      if (addTypeConstraint(depMemTy->getBase(), assocType->getProtocol(), nullptr))
        return nullptr;
    }
  }

  return this;
}

#define REQUIREMENT_SOURCE_FACTORY_BODY(ProfileArgs, ConstructorArgs,      \
                                        NumProtocolDecls, WrittenReq)      \
  llvm::FoldingSetNodeID nodeID;                                           \
  Profile ProfileArgs;                                                     \
                                                                           \
  void *insertPos = nullptr;                                               \
  if (auto known =                                                         \
        builder.Impl->RequirementSources.FindNodeOrInsertPos(nodeID,       \
                                                             insertPos))   \
    return known;                                                          \
                                                                           \
  unsigned size =                                                          \
    totalSizeToAlloc<ProtocolDecl *, WrittenRequirementLoc>(               \
                                           NumProtocolDecls,               \
                                           WrittenReq.isNull()? 0 : 1);    \
  void *mem =                                                              \
    builder.Impl->Allocator.Allocate(size, alignof(RequirementSource));    \
  auto result = new (mem) RequirementSource ConstructorArgs;               \
  builder.Impl->RequirementSources.InsertNode(result, insertPos);          \
  return result

const RequirementSource *RequirementSource::forAbstract(
                                            GenericSignatureBuilder &builder,
                                            Type rootType) {
  REQUIREMENT_SOURCE_FACTORY_BODY(
                        (nodeID, Explicit, nullptr, rootType.getPointer(),
                         nullptr, nullptr),
                        (Explicit, rootType, nullptr, WrittenRequirementLoc()),
                        0, WrittenRequirementLoc());
}

const RequirementSource *RequirementSource::forExplicit(
                  GenericSignatureBuilder &builder,
                  Type rootType,
                  GenericSignatureBuilder::WrittenRequirementLoc writtenLoc) {
  REQUIREMENT_SOURCE_FACTORY_BODY(
                        (nodeID, Explicit, nullptr, rootType.getPointer(),
                         writtenLoc.getOpaqueValue(), nullptr),
                        (Explicit, rootType, nullptr, writtenLoc),
                        0, writtenLoc);
}

const RequirementSource *RequirementSource::forInferred(
                                              GenericSignatureBuilder &builder,
                                              Type rootType,
                                              const TypeRepr *typeRepr) {
  WrittenRequirementLoc writtenLoc = typeRepr;
  REQUIREMENT_SOURCE_FACTORY_BODY(
      (nodeID, Inferred, nullptr, rootType.getPointer(),
       writtenLoc.getOpaqueValue(), nullptr),
       (Inferred, rootType, nullptr, writtenLoc),
       0, writtenLoc);
}

const RequirementSource *RequirementSource::forRequirementSignature(
                                              GenericSignatureBuilder &builder,
                                              Type rootType,
                                              ProtocolDecl *protocol) {
  REQUIREMENT_SOURCE_FACTORY_BODY(
                        (nodeID, RequirementSignatureSelf, nullptr,
                         rootType.getPointer(), protocol, nullptr),
                        (RequirementSignatureSelf, rootType, protocol,
                         WrittenRequirementLoc()),
                        1, WrittenRequirementLoc());

}

const RequirementSource *RequirementSource::forNestedTypeNameMatch(
                                             GenericSignatureBuilder &builder,
                                             Type rootType) {
  REQUIREMENT_SOURCE_FACTORY_BODY(
                        (nodeID, NestedTypeNameMatch, nullptr,
                         rootType.getPointer(), nullptr, nullptr),
                        (NestedTypeNameMatch, rootType, nullptr,
                         WrittenRequirementLoc()),
                        0, WrittenRequirementLoc());
}

const RequirementSource *RequirementSource::forConcreteTypeBinding(
                                             GenericSignatureBuilder &builder,
                                             Type rootType) {
  REQUIREMENT_SOURCE_FACTORY_BODY(
                        (nodeID, ConcreteTypeBinding, nullptr,
                         rootType.getPointer(), nullptr, nullptr),
                        (ConcreteTypeBinding, rootType, nullptr,
                         WrittenRequirementLoc()),
                        0, WrittenRequirementLoc());
}

const RequirementSource *RequirementSource::viaProtocolRequirement(
            GenericSignatureBuilder &builder, Type dependentType,
            ProtocolDecl *protocol,
            bool inferred,
            GenericSignatureBuilder::WrittenRequirementLoc writtenLoc) const {
  REQUIREMENT_SOURCE_FACTORY_BODY(
                        (nodeID,
                         inferred ? InferredProtocolRequirement
                                  : ProtocolRequirement,
                         this,
                         dependentType.getPointer(), protocol,
                         writtenLoc.getOpaqueValue()),
                        (inferred ? InferredProtocolRequirement
                                  : ProtocolRequirement,
                         this, dependentType,
                         protocol, writtenLoc),
                        1, writtenLoc);
}

const RequirementSource *RequirementSource::viaSuperclass(
                                    GenericSignatureBuilder &builder,
                                    ProtocolConformanceRef conformance) const {
  REQUIREMENT_SOURCE_FACTORY_BODY(
                        (nodeID, Superclass, this, conformance.getOpaqueValue(),
                         nullptr, nullptr),
                        (Superclass, this, conformance),
                        0, WrittenRequirementLoc());
}

const RequirementSource *RequirementSource::viaConcrete(
                                    GenericSignatureBuilder &builder,
                                    ProtocolConformanceRef conformance) const {
  REQUIREMENT_SOURCE_FACTORY_BODY(
                        (nodeID, Concrete, this, conformance.getOpaqueValue(),
                         nullptr, nullptr),
                        (Concrete, this, conformance),
                        0, WrittenRequirementLoc());
}

const RequirementSource *RequirementSource::viaParent(
                                      GenericSignatureBuilder &builder,
                                      AssociatedTypeDecl *assocType) const {
  REQUIREMENT_SOURCE_FACTORY_BODY(
                        (nodeID, Parent, this, assocType, nullptr, nullptr),
                        (Parent, this, assocType),
                        0, WrittenRequirementLoc());
}

const RequirementSource *RequirementSource::viaDerived(
                           GenericSignatureBuilder &builder) const {
  REQUIREMENT_SOURCE_FACTORY_BODY(
                        (nodeID, Derived, this, nullptr, nullptr, nullptr),
                        (Derived, this),
                        0, WrittenRequirementLoc());
}

const RequirementSource *RequirementSource::viaEquivalentType(
                                           GenericSignatureBuilder &builder,
                                           Type newType) const {
  REQUIREMENT_SOURCE_FACTORY_BODY(
                        (nodeID, EquivalentType, this, newType.getPointer(),
                         nullptr, nullptr),
                        (EquivalentType, this, newType),
                        0, WrittenRequirementLoc());
}

#undef REQUIREMENT_SOURCE_FACTORY_BODY

const RequirementSource *RequirementSource::withoutRedundantSubpath(
                                        GenericSignatureBuilder &builder,
                                        const RequirementSource *start,
                                        const RequirementSource *end) const {
  // Replace the end with the start; the caller has guaranteed that they
  // produce the same thing.
  if (this == end) {
#ifndef NDEBUG
    // Sanity check: make sure the 'start' precedes the 'end'.
    bool foundStart = false;
    for (auto source = this; source; source = source->parent) {
      if (source == start) {
        foundStart = true;
        break;
      }
    }
    assert(foundStart && "Start doesn't precede end!");
#endif
    return start;
  }

  switch (kind) {
  case Explicit:
  case Inferred:
  case RequirementSignatureSelf:
  case NestedTypeNameMatch:
  case ConcreteTypeBinding:
    llvm_unreachable("Subpath end doesn't occur within path");

  case ProtocolRequirement:
    return parent->withoutRedundantSubpath(builder, start, end)
      ->viaProtocolRequirement(builder, getStoredType(),
                               getProtocolDecl(), /*inferred=*/false,
                               getWrittenRequirementLoc());

  case InferredProtocolRequirement:
    return parent->withoutRedundantSubpath(builder, start, end)
      ->viaProtocolRequirement(builder, getStoredType(),
                               getProtocolDecl(), /*inferred=*/true,
                               getWrittenRequirementLoc());

  case Concrete:
    return parent->withoutRedundantSubpath(builder, start, end)
      ->viaParent(builder, getAssociatedType());

  case Derived:
    return parent->withoutRedundantSubpath(builder, start, end)
      ->viaDerived(builder);

  case EquivalentType:
    return parent->withoutRedundantSubpath(builder, start, end)
      ->viaEquivalentType(builder, Type(storage.type));

  case Parent:
    return parent->withoutRedundantSubpath(builder, start, end)
      ->viaParent(builder, getAssociatedType());

  case Superclass:
    return parent->withoutRedundantSubpath(builder, start, end)
      ->viaSuperclass(builder, getProtocolConformance());
  }
}

const RequirementSource *RequirementSource::getRoot() const {
  auto root = this;
  while (auto parent = root->parent)
    root = parent;
  return root;
}

Type RequirementSource::getRootType() const {
  /// Find the root.
  auto root = getRoot();

  // We're at the root, so it's in the inline storage.
  assert(root->storageKind == StorageKind::StoredType);
  return Type(root->storage.type);
}

Type RequirementSource::getAffectedType() const {
  return visitPotentialArchetypesAlongPath(
                         [](Type, const RequirementSource *) {
                           return false;
                         });
}

Type
RequirementSource::visitPotentialArchetypesAlongPath(
     llvm::function_ref<bool(Type, const RequirementSource *)> visitor) const {
  switch (kind) {
  case RequirementSource::Parent: {
    Type parentType = parent->visitPotentialArchetypesAlongPath(visitor);
    if (!parentType) return nullptr;

    if (visitor(parentType, this)) return nullptr;

    return replaceSelfWithType(parentType,
                               getAssociatedType()->getDeclaredInterfaceType());
  }

  case RequirementSource::NestedTypeNameMatch:
  case RequirementSource::ConcreteTypeBinding:
  case RequirementSource::Explicit:
  case RequirementSource::Inferred:
  case RequirementSource::RequirementSignatureSelf: {
    Type rootType = getRootType();
    if (visitor(rootType, this)) return nullptr;

    return rootType;
  }

  case RequirementSource::Concrete:
  case RequirementSource::Superclass:
  case RequirementSource::Derived:
    return parent->visitPotentialArchetypesAlongPath(visitor);

  case RequirementSource::EquivalentType: {
    auto parentType = parent->visitPotentialArchetypesAlongPath(visitor);
    if (!parentType) return nullptr;

    if (visitor(parentType, this)) return nullptr;

    return Type(storage.type);
  }

  case RequirementSource::ProtocolRequirement:
  case RequirementSource::InferredProtocolRequirement: {
    Type parentType = parent->visitPotentialArchetypesAlongPath(visitor);
    if (!parentType) return nullptr;

    if (visitor(parentType, this)) return nullptr;

    return replaceSelfWithType(parentType, getStoredType());
  }
  }
}

Type RequirementSource::getStoredType() const {
  switch (storageKind) {
  case StorageKind::None:
  case StorageKind::ProtocolConformance:
  case StorageKind::AssociatedTypeDecl:
    return Type();

  case StorageKind::StoredType:
    return storage.type;
  }

  llvm_unreachable("Unhandled StorageKind in switch.");
}

ProtocolDecl *RequirementSource::getProtocolDecl() const {
  switch (storageKind) {
  case StorageKind::None:
    return nullptr;

  case StorageKind::StoredType:
    if (isProtocolRequirement() || kind == RequirementSignatureSelf)
      return getTrailingObjects<ProtocolDecl *>()[0];
    return nullptr;

  case StorageKind::ProtocolConformance:
    return getProtocolConformance().getRequirement();

  case StorageKind::AssociatedTypeDecl:
    return storage.assocType->getProtocol();
  }

  llvm_unreachable("Unhandled StorageKind in switch.");
}

SourceLoc RequirementSource::getLoc() const {
  // Don't produce locations for protocol requirements unless the parent is
  // the protocol self.
  // FIXME: We should have a better notion of when to emit diagnostics
  // for a particular requirement, rather than turning on/off location info.
  // Locations that fall into this category should be advisory, emitted via
  // notes rather than as the normal location.
  if (isProtocolRequirement() && parent &&
      parent->kind != RequirementSignatureSelf)
    return parent->getLoc();

  if (auto typeRepr = getTypeRepr())
    return typeRepr->getStartLoc();

  if (auto requirementRepr = getRequirementRepr()) {
    switch (requirementRepr->getKind()) {
    case RequirementReprKind::LayoutConstraint:
    case RequirementReprKind::TypeConstraint:
      return requirementRepr->getColonLoc();

    case RequirementReprKind::SameType:
      return requirementRepr->getEqualLoc();
    }
  }
  if (parent)
    return parent->getLoc();

  if (kind == RequirementSignatureSelf)
    return getProtocolDecl()->getLoc();

  return SourceLoc();
}

/// Compute the path length of a requirement source, counting only the number
/// of \c ProtocolRequirement elements.
static unsigned sourcePathLength(const RequirementSource *source) {
  unsigned count = 0;
  for (; source; source = source->parent) {
    if (source->isProtocolRequirement())
      ++count;
  }
  return count;
}

int RequirementSource::compare(const RequirementSource *other) const {
  // Prefer the derived option, if there is one.
  bool thisIsDerived = this->isDerivedRequirement();
  bool otherIsDerived = other->isDerivedRequirement();
  if (thisIsDerived != otherIsDerived)
    return thisIsDerived ? -1 : +1;

  // Prefer the shorter path.
  unsigned thisLength = sourcePathLength(this);
  unsigned otherLength = sourcePathLength(other);
  if (thisLength != otherLength)
    return thisLength < otherLength ? -1 : +1;

  // FIXME: Arbitrary hack to allow later requirement sources to stomp on
  // earlier ones. We need a proper ordering here.
  return +1;
}

void RequirementSource::dump() const {
  dump(llvm::errs(), nullptr, 0);
  llvm::errs() << "\n";
}

/// Dump the constraint source.
void RequirementSource::dump(llvm::raw_ostream &out, SourceManager *srcMgr,
                             unsigned indent) const {
  // FIXME: Implement for real, so we actually dump the structure.
  out.indent(indent);
  print(out, srcMgr);
}

void RequirementSource::print() const {
  print(llvm::errs(), nullptr);
}

void RequirementSource::print(llvm::raw_ostream &out,
                              SourceManager *srcMgr) const {
  if (parent) {
    parent->print(out, srcMgr);
    out << " -> ";
  } else {
    out << getRootType().getString() << ": ";
  }

  switch (kind) {
  case Concrete:
    out << "Concrete";
    break;

  case Explicit:
    out << "Explicit";
    break;

  case Inferred:
    out << "Inferred";
    break;

  case NestedTypeNameMatch:
    out << "Nested type match";
    break;

  case RequirementSource::ConcreteTypeBinding:
    out << "Concrete type binding";
    break;

  case Parent:
    out << "Parent";
    break;

  case ProtocolRequirement:
    out << "Protocol requirement";
    break;

  case InferredProtocolRequirement:
    out << "Inferred protocol requirement";
    break;

  case RequirementSignatureSelf:
    out << "Requirement signature self";
    break;

  case Superclass:
    out << "Superclass";
    break;

  case Derived:
    out << "Derived";
    break;

  case EquivalentType:
    out << "Equivalent type";
    break;
  }

  // Local function to dump a source location, if we can.
  auto dumpSourceLoc = [&](SourceLoc loc) {
    if (!srcMgr) return;
    if (loc.isInvalid()) return;

    unsigned bufferID = srcMgr->findBufferContainingLoc(loc);

    auto lineAndCol = srcMgr->getLineAndColumn(loc, bufferID);
    out << " @ " << lineAndCol.first << ':' << lineAndCol.second;
  };

  switch (storageKind) {
  case StorageKind::None:
    break;

  case StorageKind::StoredType:
    if (auto proto = getProtocolDecl()) {
      out << " (via " << storage.type->getString() << " in " << proto->getName()
          << ")";
    }
    break;

  case StorageKind::ProtocolConformance: {
    auto conformance = getProtocolConformance();
    if (conformance.isConcrete()) {
      out << " (" << conformance.getConcrete()->getType()->getString() << ": "
          << conformance.getConcrete()->getProtocol()->getName() << ")";
    } else {
      out << " (abstract " << conformance.getRequirement()->getName() << ")";
    }
    break;
  }

  case StorageKind::AssociatedTypeDecl:
    out << " (" << storage.assocType->getProtocol()->getName()
        << "::" << storage.assocType->getName() << ")";
    break;
  }

  if (getTypeRepr() || getRequirementRepr()) {
    dumpSourceLoc(getLoc());
  }
}

/// Form the dependent type such that the given protocol's \c Self can be
/// replaced by \c baseType to reach \c type.
static Type formProtocolRelativeType(ProtocolDecl *proto,
                                     Type baseType,
                                     Type type) {
  // Basis case: we've hit the base potential archetype.
  if (baseType->isEqual(type))
    return proto->getSelfInterfaceType();

  // Recursive case: form a dependent member type.
  auto depMemTy = type->castTo<DependentMemberType>();
  Type newBaseType = formProtocolRelativeType(proto, baseType,
                                              depMemTy->getBase());
  if (auto assocType = depMemTy->getAssocType())
    return DependentMemberType::get(newBaseType, assocType);

  return DependentMemberType::get(newBaseType, depMemTy->getName());
}

const RequirementSource *FloatingRequirementSource::getSource(
                                              GenericSignatureBuilder &builder,
                                              Type type) const {
  switch (kind) {
  case Resolved:
    return storage.get<const RequirementSource *>();

  case Explicit:
    if (auto requirementRepr = storage.dyn_cast<const RequirementRepr *>())
      return RequirementSource::forExplicit(builder, type, requirementRepr);
    if (auto typeRepr = storage.dyn_cast<const TypeRepr *>())
      return RequirementSource::forExplicit(builder, type, typeRepr);
    return RequirementSource::forAbstract(builder, type);

  case Inferred:
    return RequirementSource::forInferred(builder, type,
                                          storage.get<const TypeRepr *>());

  case AbstractProtocol: {
    // Derive the dependent type on which this requirement was written. It is
    // the path from the requirement source on which this requirement is based
    // to the potential archetype on which the requirement is being placed.
    auto baseSource = storage.get<const RequirementSource *>();
    auto baseSourceType = baseSource->getAffectedType();

    auto dependentType =
      formProtocolRelativeType(protocolReq.protocol, baseSourceType, type);

    return storage.get<const RequirementSource *>()
      ->viaProtocolRequirement(builder, dependentType,
                               protocolReq.protocol, protocolReq.inferred,
                               protocolReq.written);
  }

  case NestedTypeNameMatch:
    return RequirementSource::forNestedTypeNameMatch(builder, type);
  }

  llvm_unreachable("Unhandled FloatingPointRequirementSourceKind in switch.");
}

SourceLoc FloatingRequirementSource::getLoc() const {
  if (auto source = storage.dyn_cast<const RequirementSource *>())
    return source->getLoc();

  if (auto typeRepr = storage.dyn_cast<const TypeRepr *>())
    return typeRepr->getLoc();

  if (auto requirementRepr = storage.dyn_cast<const RequirementRepr *>()) {
    switch (requirementRepr->getKind()) {
    case RequirementReprKind::LayoutConstraint:
    case RequirementReprKind::TypeConstraint:
      return requirementRepr->getColonLoc();

    case RequirementReprKind::SameType:
      return requirementRepr->getEqualLoc();
    }
  }

  return SourceLoc();
}

bool FloatingRequirementSource::isExplicit() const {
  switch (kind) {
  case Explicit:
    return true;

  case Inferred:
  case NestedTypeNameMatch:
    return false;

  case AbstractProtocol:
    // Requirements implied by other protocol conformance requirements are
    // implicit, except when computing a requirement signature, where
    // non-inferred ones are explicit, to allow flagging of redundant
    // requirements.
    switch (storage.get<const RequirementSource *>()->kind) {
    case RequirementSource::RequirementSignatureSelf:
      return !protocolReq.inferred;

    case RequirementSource::Concrete:
    case RequirementSource::Explicit:
    case RequirementSource::Inferred:
    case RequirementSource::NestedTypeNameMatch:
    case RequirementSource::ConcreteTypeBinding:
    case RequirementSource::Parent:
    case RequirementSource::ProtocolRequirement:
    case RequirementSource::InferredProtocolRequirement:
    case RequirementSource::Superclass:
    case RequirementSource::Derived:
    case RequirementSource::EquivalentType:
      return false;
    }

  case Resolved:
    switch (storage.get<const RequirementSource *>()->kind) {
    case RequirementSource::Explicit:
      return true;

    case RequirementSource::ProtocolRequirement:
      return storage.get<const RequirementSource *>()->parent->kind
        == RequirementSource::RequirementSignatureSelf;

    case RequirementSource::Inferred:
    case RequirementSource::InferredProtocolRequirement:
    case RequirementSource::RequirementSignatureSelf:
    case RequirementSource::Concrete:
    case RequirementSource::NestedTypeNameMatch:
    case RequirementSource::ConcreteTypeBinding:
    case RequirementSource::Parent:
    case RequirementSource::Superclass:
    case RequirementSource::Derived:
    case RequirementSource::EquivalentType:
      return false;
    }
  }
}


FloatingRequirementSource FloatingRequirementSource::asInferred(
                                          const TypeRepr *typeRepr) const {
  switch (kind) {
  case Explicit:
    return forInferred(typeRepr);

  case Inferred:
  case Resolved:
  case NestedTypeNameMatch:
    return *this;

  case AbstractProtocol:
    return viaProtocolRequirement(storage.get<const RequirementSource *>(),
                                  protocolReq.protocol, typeRepr,
                                  /*inferred=*/true);
  }
}

bool FloatingRequirementSource::isRecursive(
                                    Type rootType,
                                    GenericSignatureBuilder &builder) const {
  llvm::SmallSet<std::pair<CanType, ProtocolDecl *>, 4> visitedAssocReqs;
  for (auto storedSource = storage.dyn_cast<const RequirementSource *>();
       storedSource; storedSource = storedSource->parent) {
    // FIXME: isRecursive() is completely misnamed
    if (storedSource->kind == RequirementSource::EquivalentType)
      return true;

    if (!storedSource->isProtocolRequirement())
      continue;

    if (!visitedAssocReqs.insert(
                          {storedSource->getStoredType()->getCanonicalType(),
                           storedSource->getProtocolDecl()}).second)
      return true;
  }

  return false;
}

GenericSignatureBuilder::PotentialArchetype::~PotentialArchetype() {
  ++NumPotentialArchetypes;

  for (const auto &nested : NestedTypes) {
    for (auto pa : nested.second) {
      pa->~PotentialArchetype();
    }
  }
}

std::string GenericSignatureBuilder::PotentialArchetype::getDebugName() const {
  llvm::SmallString<64> result;

  auto parent = getParent();
  if (!parent) {
    static const char *tau = u8"\u03C4_";

    llvm::raw_svector_ostream os(result);
    os << tau << getGenericParamKey().Depth << '_'
       << getGenericParamKey().Index;
    return os.str().str();
  }

  // Nested types.
  result += parent->getDebugName();

  // When building the name for debugging purposes, include the protocol into
  // which the associated type or type alias was resolved.
  ProtocolDecl *proto = nullptr;
  if (auto assocType = getResolvedAssociatedType()) {
    proto = assocType->getProtocol();
  } else if (auto concreteDecl = getConcreteTypeDecl()) {
    proto = concreteDecl->getDeclContext()
              ->getAsProtocolOrProtocolExtensionContext();
  }

  if (proto) {
    result.push_back('[');
    result.push_back('.');
    result.append(proto->getName().str().begin(), proto->getName().str().end());
    result.push_back(']');
  }

  result.push_back('.');
  result.append(getNestedName().str().begin(), getNestedName().str().end());

  return result.str().str();
}

unsigned GenericSignatureBuilder::PotentialArchetype::getNestingDepth() const {
  unsigned Depth = 0;
  for (auto P = getParent(); P; P = P->getParent())
    ++Depth;
  return Depth;
}

void EquivalenceClass::addMember(PotentialArchetype *pa) {
  members.push_back(pa);
  if (members.back()->getNestingDepth() < members.front()->getNestingDepth()) {
    MutableArrayRef<PotentialArchetype *> mutMembers = members;
    std::swap(mutMembers.front(), mutMembers.back());
  }
}

class GenericSignatureBuilder::ResolvedType {
  llvm::PointerUnion<PotentialArchetype *, Type> type;
  EquivalenceClass *equivClass;

  /// For a type that could not be resolved further unless the given
  /// equivalence class changes.
  ResolvedType(EquivalenceClass *equivClass)
    : type(), equivClass(equivClass) { }

public:
  /// A specific resolved potential archetype.
  ResolvedType(PotentialArchetype *pa)
    : type(pa), equivClass(pa->getEquivalenceClassIfPresent()) { }

  /// A resolved type within the given equivalence class.
  ResolvedType(Type type, EquivalenceClass *equivClass)
      : type(type), equivClass(equivClass) {
    assert(type->isTypeParameter() == static_cast<bool>(equivClass) &&
           "type parameters must have equivalence classes");
  }

  /// Return an unresolved result, which could be resolved when we
  /// learn more information about the given equivalence class.
  static ResolvedType forUnresolved(EquivalenceClass *equivClass) {
    return ResolvedType(equivClass);
  }

  /// Return a result for a concrete type.
  static ResolvedType forConcrete(Type concreteType) {
    return ResolvedType(concreteType, nullptr);
  }

  /// Determine whether this result was resolved.
  explicit operator bool() const { return !type.isNull(); }

  /// Retrieve the dependent type.
  Type getDependentType(GenericSignatureBuilder &builder) const;

  /// Retrieve the concrete type, or a null type if this result doesn't store
  /// a concrete type.
  Type getAsConcreteType() const {
    assert(*this && "Doesn't contain any result");
    if (equivClass) return Type();
    return type.dyn_cast<Type>();
  }

  /// Realize a potential archetype for this type parameter.
  PotentialArchetype *realizePotentialArchetype(
                                            GenericSignatureBuilder &builder);

  /// Retrieve the potential archetype, if already known.
  PotentialArchetype *getPotentialArchetypeIfKnown() const {
    return type.dyn_cast<PotentialArchetype *>();
  }

  /// Retrieve the equivalence class into which a resolved type refers.
  EquivalenceClass *getEquivalenceClass(
                     GenericSignatureBuilder &builder) const {
    assert(*this && "Only for resolved types");
    if (equivClass) return equivClass;

    // Create the equivalence class now.
    return type.get<PotentialArchetype *>()
             ->getOrCreateEquivalenceClass(builder);
  }

  /// Retrieve the unresolved result.
  EquivalenceClass *getUnresolvedEquivClass() const {
    assert(!*this);
    return equivClass;
  }

  /// Return an unresolved type.
  ///
  /// This loses equivalence-class information that could be useful, which
  /// is unfortunate.
  UnresolvedType getUnresolvedType() const {
    return type;
  }
};

bool EquivalenceClass::recordConformanceConstraint(
                                 GenericSignatureBuilder &builder,
                                 ResolvedType type,
                                 ProtocolDecl *proto,
                                 FloatingRequirementSource source) {
  // If we haven't seen a conformance to this protocol yet, add it.
  bool inserted = false;
  auto known = conformsTo.find(proto);
  if (known == conformsTo.end()) {
    known = conformsTo.insert({ proto, { }}).first;
    inserted = true;
    modified(builder);
    ++NumConformances;

    // If there is a concrete type that resolves this conformance requirement,
    // record the conformance.
    if (!builder.resolveConcreteConformance(type, proto)) {
      // Otherwise, determine whether there is a superclass constraint where the
      // superclass conforms to this protocol.
      (void)builder.resolveSuperConformance(type, proto);
    }
  }

  // Record this conformance source.
  known->second.push_back({type.getUnresolvedType(), proto,
                           source.getSource(builder,
                                            type.getDependentType(builder))});
  ++NumConformanceConstraints;

  return inserted;
}

bool EquivalenceClass::recordSameTypeConstraint(
                              PotentialArchetype *type1,
                              PotentialArchetype *type2,
                              const RequirementSource *source) {
  sameTypeConstraints.push_back({type1, type2, source});
  ++NumSameTypeConstraints;
  return type1->getEquivalenceClassIfPresent() !=
    type2->getEquivalenceClassIfPresent();
}

template<typename T>
Type Constraint<T>::getSubjectDependentType(
                        ArrayRef<GenericTypeParamType *> genericParams) const {
  if (auto type = subject.dyn_cast<Type>())
    return type;

  return subject.get<PotentialArchetype *>()->getDependentType(genericParams);
}

template<typename T>
bool Constraint<T>::isSubjectEqualTo(const PotentialArchetype *pa) const {
  if (auto subjectPA = subject.dyn_cast<PotentialArchetype *>())
    return subjectPA == pa;

  return getSubjectDependentType({ })->isEqual(pa->getDependentType({ }));
}

template<typename T>
bool Constraint<T>::hasSameSubjectAs(const Constraint<T> &other) const {
  if (auto subjectPA = subject.dyn_cast<PotentialArchetype *>()) {
    if (auto otherSubjectPA =
          other.subject.template dyn_cast<PotentialArchetype *>())
      return subjectPA == otherSubjectPA;
  }

  return getSubjectDependentType({ })
    ->isEqual(other.getSubjectDependentType({ }));
}

Optional<ConcreteConstraint>
EquivalenceClass::findAnyConcreteConstraintAsWritten(Type preferredType) const {
  // If we don't have a concrete type, there's no source.
  if (!concreteType) return None;

  // Go look for a source with source-location information.
  Optional<ConcreteConstraint> result;
  for (const auto &constraint : concreteTypeConstraints) {
    if (constraint.source->getLoc().isValid()) {
      result = constraint;
      if (!preferredType ||
          constraint.getSubjectDependentType({ })->isEqual(preferredType))
        return result;
    }
  }

  return result;
}

Optional<ConcreteConstraint>
EquivalenceClass::findAnySuperclassConstraintAsWritten(
                                                   Type preferredType) const {
  // If we don't have a superclass, there's no source.
  if (!superclass) return None;

  // Go look for a source with source-location information.
  Optional<ConcreteConstraint> result;
  for (const auto &constraint : superclassConstraints) {
    if (constraint.source->getLoc().isValid() &&
        constraint.value->isEqual(superclass)) {
      result = constraint;

      if (!preferredType ||
          constraint.getSubjectDependentType({ })->isEqual(preferredType))
        return result;
    }
  }

  return result;
}

bool EquivalenceClass::isConformanceSatisfiedBySuperclass(
                                                    ProtocolDecl *proto) const {
  auto known = conformsTo.find(proto);
  assert(known != conformsTo.end() && "doesn't conform to this protocol");
  for (const auto &constraint: known->second) {
    if (constraint.source->kind == RequirementSource::Superclass)
      return true;
  }

  return false;
}

/// Compare two associated types.
static int compareAssociatedTypes(AssociatedTypeDecl *assocType1,
                                  AssociatedTypeDecl *assocType2) {
  // - by name.
  if (int result = assocType1->getName().str().compare(
                                              assocType2->getName().str()))
    return result;

  // Prefer an associated type with no overrides (i.e., an anchor) to one
  // that has overrides.
  bool hasOverridden1 = !assocType1->getOverriddenDecls().empty();
  bool hasOverridden2 = !assocType2->getOverriddenDecls().empty();
  if (hasOverridden1 != hasOverridden2)
    return hasOverridden1 ? +1 : -1;

  // - by protocol, so t_n_m.`P.T` < t_n_m.`Q.T` (given P < Q)
  auto proto1 = assocType1->getProtocol();
  auto proto2 = assocType2->getProtocol();
  if (int compareProtocols = ProtocolType::compareProtocols(&proto1, &proto2))
    return compareProtocols;

  // Error case: if we have two associated types with the same name in the
  // same protocol, just tie-break based on address.
  if (assocType1 != assocType2)
    return assocType1 < assocType2 ? -1 : +1;

  return 0;
}

TypeDecl *EquivalenceClass::lookupNestedType(
                             GenericSignatureBuilder &builder,
                             Identifier name,
                             SmallVectorImpl<TypeDecl *> *otherConcreteTypes) {
  // Populates the result structures from the given cache entry.
  auto populateResult = [&](const CachedNestedType &cache) -> TypeDecl * {
    if (otherConcreteTypes)
      otherConcreteTypes->clear();

    // If there aren't any types in the cache, we're done.
    if (cache.types.empty()) return nullptr;

    // The first type in the cache is always the final result.
    // Collect the rest in the concrete-declarations list, if needed.
    if (otherConcreteTypes) {
      for (auto type : ArrayRef<TypeDecl *>(cache.types).slice(1)) {
        otherConcreteTypes->push_back(type);
      }
    }

    return cache.types.front();
  };

  // If we have a cached value that is up-to-date, use that.
  auto cached = nestedTypeNameCache.find(name);
  if (cached != nestedTypeNameCache.end() &&
      cached->second.numConformancesPresent == conformsTo.size() &&
      (!superclass ||
       cached->second.superclassPresent == superclass->getCanonicalType())) {
    ++NumNestedTypeCacheHits;
    return populateResult(cached->second);
  }

  // Cache miss; go compute the result.
  ++NumNestedTypeCacheMisses;

  // Look for types with the given name in protocols that we know about.
  AssociatedTypeDecl *bestAssocType = nullptr;
  llvm::SmallSetVector<AssociatedTypeDecl *, 4> assocTypeAnchors;
  SmallVector<TypeDecl *, 4> concreteDecls;
  for (const auto &conforms : conformsTo) {
    ProtocolDecl *proto = conforms.first;

    // Look for an associated type and/or concrete type with this name.
    for (auto member : proto->lookupDirect(name,
                                           /*ignoreNewExtensions=*/true)) {
      // If this is an associated type, record whether it is the best
      // associated type we've seen thus far.
      if (auto assocType = dyn_cast<AssociatedTypeDecl>(member)) {
        // Retrieve the associated type anchor.
        assocType = assocType->getAssociatedTypeAnchor();
        assocTypeAnchors.insert(assocType);

        if (!bestAssocType ||
             compareAssociatedTypes(assocType, bestAssocType) < 0)
          bestAssocType = assocType;

        continue;
      }

      // If this is another type declaration, determine whether we should
      // record it.
      if (auto type = dyn_cast<TypeDecl>(member)) {
        // FIXME: Filter out type declarations that aren't in the same
        // module as the protocol itself. This is an unprincipled hack, but
        // provides consistent lookup semantics for the generic signature
        // builder in all contents.
        if (type->getDeclContext()->getParentModule()
              != proto->getParentModule())
          continue;

        // Resolve the signature of this type.
        if (!type->hasInterfaceType()) {
          type->getASTContext().getLazyResolver()->resolveDeclSignature(type);
          if (!type->hasInterfaceType())
            continue;
        }

        concreteDecls.push_back(type);
        continue;
      }
    }
  }

  // If we haven't found anything yet but have a superclass, look for a type
  // in the superclass.
  // FIXME: Shouldn't we always look in the superclass?
  if (!bestAssocType && concreteDecls.empty() && superclass) {
    if (auto classDecl = superclass->getClassOrBoundGenericClass()) {
      SmallVector<ValueDecl *, 2> superclassMembers;
      classDecl->getParentModule()->lookupQualified(
          superclass, name,
          NL_QualifiedDefault | NL_OnlyTypes | NL_ProtocolMembers, nullptr,
          superclassMembers);
      for (auto member : superclassMembers) {
        if (auto type = dyn_cast<TypeDecl>(member)) {
          // Resolve the signature of this type.
          if (!type->hasInterfaceType()) {
            type->getASTContext().getLazyResolver()->resolveDeclSignature(type);
            if (!type->hasInterfaceType())
              continue;
          }

          concreteDecls.push_back(type);
        }
      }
    }
  }

  // Infer same-type constraints among same-named associated type anchors.
  if (assocTypeAnchors.size() > 1) {
    auto anchorType = getAnchor(builder, builder.getGenericParams());
    auto inferredSource = FloatingRequirementSource::forInferred(nullptr);
    for (auto assocType : assocTypeAnchors) {
      if (assocType == bestAssocType) continue;

      builder.addRequirement(
        Requirement(RequirementKind::SameType,
                    DependentMemberType::get(anchorType, bestAssocType),
                    DependentMemberType::get(anchorType, assocType)),
        inferredSource, nullptr);
    }
  }

  // Form the new cache entry.
  CachedNestedType entry;
  entry.numConformancesPresent = conformsTo.size();
  entry.superclassPresent =
    superclass ? superclass->getCanonicalType() : CanType();
  if (bestAssocType) {
    entry.types.push_back(bestAssocType);
    entry.types.insert(entry.types.end(),
                       concreteDecls.begin(), concreteDecls.end());
    assert(bestAssocType->getOverriddenDecls().empty() &&
           "Lookup should never keep a non-anchor associated type");
  } else if (!concreteDecls.empty()) {
    // Find the best concrete type.
    auto bestConcreteTypeIter =
      std::min_element(concreteDecls.begin(), concreteDecls.end(),
                       [](TypeDecl *type1, TypeDecl *type2) {
                         return TypeDecl::compare(type1, type2) < 0;
                       });

    // Put the best concrete type first; the rest will follow.
    entry.types.push_back(*bestConcreteTypeIter);
    entry.types.insert(entry.types.end(),
                       concreteDecls.begin(), bestConcreteTypeIter);
    entry.types.insert(entry.types.end(),
                       bestConcreteTypeIter + 1, concreteDecls.end());
  }

  return populateResult((nestedTypeNameCache[name] = std::move(entry)));
}

/// Determine whether any part of this potential archetype's path to the
/// root contains the given equivalence class.
static bool pathContainsEquivalenceClass(GenericSignatureBuilder &builder,
                                         PotentialArchetype *pa,
                                         EquivalenceClass *equivClass) {
  // Chase the potential archetype up to the root.
  for (; pa; pa = pa->getParent()) {
    // Check whether this potential archetype is in the given equivalence
    // class.
    if (pa->getOrCreateEquivalenceClass(builder) == equivClass)
      return true;
  }

  return false;
}

Type EquivalenceClass::getAnchor(
                            GenericSignatureBuilder &builder,
                            ArrayRef<GenericTypeParamType *> genericParams) {
  // Check whether the cache is valid.
  if (archetypeAnchorCache.anchor &&
      archetypeAnchorCache.numMembers == members.size()) {
    ++NumArchetypeAnchorCacheHits;

    // Reparent the anchor using genericParams.
    return archetypeAnchorCache.anchor.subst(
             [&](SubstitutableType *dependentType) {
               if (auto gp = dyn_cast<GenericTypeParamType>(dependentType)) {
                 unsigned index =
                   GenericParamKey(gp).findIndexIn(genericParams);
                 return Type(genericParams[index]);
               }

               return Type(dependentType);
             },
             MakeAbstractConformanceForGenericType());
  }

  // Map the members of this equivalence class to the best associated type
  // within that equivalence class.
  llvm::SmallDenseMap<EquivalenceClass *, AssociatedTypeDecl *> nestedTypes;

  Type bestGenericParam;
  for (auto member : members) {
    // If the member is a generic parameter, keep the best generic parameter.
    if (member->isGenericParam()) {
      Type genericParamType = member->getDependentType(genericParams);
      if (!bestGenericParam ||
          compareDependentTypes(genericParamType, bestGenericParam) < 0)
        bestGenericParam = genericParamType;
      continue;
    }

    // If we saw a generic parameter, ignore any nested types.
    if (bestGenericParam) continue;

    // If the nested type doesn't have an associated type, skip it.
    auto assocType = member->getResolvedAssociatedType();
    if (!assocType) continue;

    // Dig out the equivalence class of the parent.
    auto parentEquivClass =
      member->getParent()->getOrCreateEquivalenceClass(builder);

    // If the path from this member to the root contains this equivalence
    // class, it cannot be part of the anchor.
    if (pathContainsEquivalenceClass(builder, member->getParent(), this))
      continue;

    // Take the best associated type for this equivalence class.
    assocType = assocType->getAssociatedTypeAnchor();
    auto &bestAssocType = nestedTypes[parentEquivClass];
    if (!bestAssocType ||
        compareAssociatedTypes(assocType, bestAssocType) < 0)
      bestAssocType = assocType;
  }

  // If we found a generic parameter, return that.
  if (bestGenericParam)
    return bestGenericParam;

  // Determine the best anchor among the parent equivalence classes.
  Type bestParentAnchor;
  AssociatedTypeDecl *bestAssocType = nullptr;
  std::pair<EquivalenceClass *, Identifier> bestNestedType;
  for (const auto &nestedType : nestedTypes) {
    auto parentAnchor = nestedType.first->getAnchor(builder, genericParams);
    if (!bestParentAnchor ||
        compareDependentTypes(parentAnchor, bestParentAnchor) < 0) {
      bestParentAnchor = parentAnchor;
      bestAssocType = nestedType.second;
    }
  }

  // Form the anchor type.
  Type anchorType = DependentMemberType::get(bestParentAnchor, bestAssocType);

  // Record the cache miss and update the cache.
  ++NumArchetypeAnchorCacheMisses;
  archetypeAnchorCache.anchor = anchorType;
  archetypeAnchorCache.numMembers = members.size();

  return anchorType;
}

Type EquivalenceClass::getTypeInContext(GenericSignatureBuilder &builder,
                                        GenericEnvironment *genericEnv) {
  ArrayRef<GenericTypeParamType *> genericParams =
    genericEnv->getGenericParams();

  // The anchor descr
  Type anchor = getAnchor(builder, genericParams);

  // If this equivalence class is mapped to a concrete type, produce that
  // type.
  if (concreteType) {
    if (recursiveConcreteType)
      return ErrorType::get(anchor);

    return genericEnv->mapTypeIntoContext(concreteType,
                                          builder.getLookupConformanceFn());
  }

  // Local function to check whether we have a generic parameter that has
  // already been recorded
  auto getAlreadyRecoveredGenericParam = [&]() -> Type {
    auto genericParam = anchor->getAs<GenericTypeParamType>();
    if (!genericParam) return Type();

    auto type = genericEnv->getMappingIfPresent(genericParam);
    if (!type) return Type();

    // We already have a mapping for this generic parameter in the generic
    // environment. Return it.
    return *type;
  };

  AssociatedTypeDecl *assocType = nullptr;
  ArchetypeType *parentArchetype = nullptr;
  if (auto depMemTy = anchor->getAs<DependentMemberType>()) {
    // Resolve the equivalence class of the parent.
    auto parentEquivClass =
      builder.resolveEquivalenceClass(
                          depMemTy->getBase(),
                          ArchetypeResolutionKind::CompleteWellFormed);
    if (!parentEquivClass)
      return ErrorType::get(anchor);

    // Map the parent type into this context.
    Type parentType = parentEquivClass->getTypeInContext(builder, genericEnv);

    // If the parent is concrete, handle the
    parentArchetype = parentType->getAs<ArchetypeType>();
    if (!parentArchetype) {
      // Resolve the member type.
      Type memberType =
        depMemTy->substBaseType(parentType, builder.getLookupConformanceFn());

      return genericEnv->mapTypeIntoContext(memberType,
                                            builder.getLookupConformanceFn());
    }

    // If we already have a nested type with this name, return it.
    assocType = depMemTy->getAssocType();
    if (auto nested =
          parentArchetype->getNestedTypeIfKnown(assocType->getName())) {
      return *nested;
    }

    // We will build the archetype below.
  } else if (auto result = getAlreadyRecoveredGenericParam()) {
    // Return already-contextualized generic type parameter.
    return result;
  }

  // Substitute into the superclass.
  Type superclass = this->recursiveSuperclassType ? Type() : this->superclass;
  if (superclass && superclass->hasTypeParameter()) {
    superclass = genericEnv->mapTypeIntoContext(
                                            superclass,
                                            builder.getLookupConformanceFn());
    if (superclass->is<ErrorType>())
      superclass = Type();

    // We might have recursively recorded the archetype; if so, return early.
    // FIXME: This should be detectable before we end up building archetypes.
    if (auto result = getAlreadyRecoveredGenericParam())
      return result;
  }

  // Build a new archetype.

  // Collect the protocol conformances for the archetype.
  SmallVector<ProtocolDecl *, 4> protos;
  for (const auto &conforms : conformsTo) {
    auto proto = conforms.first;

    if (!isConformanceSatisfiedBySuperclass(proto))
      protos.push_back(proto);
  }

  ArchetypeType *archetype;
  ASTContext &ctx = builder.getASTContext();
  if (parentArchetype) {
    // Create a nested archetype.
    auto *depMemTy = anchor->castTo<DependentMemberType>();
    archetype = ArchetypeType::getNew(ctx, parentArchetype, depMemTy, protos,
                                      superclass, layout);

    // Register this archetype with its parent.
    parentArchetype->registerNestedType(assocType->getName(), archetype);
  } else {
    // Create a top-level archetype.
    auto genericParam = anchor->castTo<GenericTypeParamType>();
    archetype = ArchetypeType::getNew(ctx, genericEnv, genericParam, protos,
                                      superclass, layout);

    // Register the archetype with the generic environment.
    genericEnv->addMapping(genericParam, archetype);
  }

  return archetype;
}

void EquivalenceClass::dump(llvm::raw_ostream &out) const {
  out << "Equivalence class represented by "
    << members.front()->getRepresentative()->getDebugName() << ":\n";
  out << "Members: ";
  interleave(members, [&](PotentialArchetype *pa) {
    out << pa->getDebugName();
  }, [&]() {
    out << ", ";
  });
  out << "\nConformances:";
  interleave(conformsTo,
             [&](const std::pair<
                        ProtocolDecl *,
                        std::vector<Constraint<ProtocolDecl *>>> &entry) {
               out << entry.first->getNameStr();
             },
             [&] { out << ", "; });
  out << "\nSame-type constraints:";
  interleave(sameTypeConstraints,
             [&](const Constraint<PotentialArchetype *> &constraint) {
               out << "\n  " << constraint.getSubjectDependentType({ })
                   << " == " << constraint.value->getDebugName();

               if (constraint.source->isDerivedRequirement())
                 out << " [derived]";
             }, [&] {
               out << ", ";
             });
  if (concreteType)
    out << "\nConcrete type: " << concreteType.getString();
  if (superclass)
    out << "\nSuperclass: " << superclass.getString();
  if (layout)
    out << "\nLayout: " << layout.getString();

  if (!delayedRequirements.empty()) {
    out << "\nDelayed requirements:";
    for (const auto &req : delayedRequirements) {
      out << "\n  ";
      req.dump(out);
    }
  }

  out << "\n";

  {
    out << "---GraphViz output for same-type constraints---\n";

    // Render the output
    std::string graphviz;
    {
      llvm::raw_string_ostream graphvizOut(graphviz);
      llvm::WriteGraph(graphvizOut, this);
    }

    // Clean up the output to turn it into an undirected graph.
    // FIXME: This is horrible, GraphWriter should be able to support
    // undirected graphs.
    auto digraphPos = graphviz.find("digraph");
    if (digraphPos != std::string::npos) {
      // digraph -> graph
      graphviz.erase(graphviz.begin() + digraphPos,
                     graphviz.begin() + digraphPos + 2);
    }

    // Directed edges to undirected edges: -> to --
    while (true) {
      auto arrowPos = graphviz.find("->");
      if (arrowPos == std::string::npos) break;

      graphviz.replace(arrowPos, 2, "--");
    }

    out << graphviz;
  }
}

void EquivalenceClass::dump() const {
  dump(llvm::errs());
}

void DelayedRequirement::dump(llvm::raw_ostream &out) const {
  // Print LHS.
  if (auto lhsPA = lhs.dyn_cast<PotentialArchetype *>())
    out << lhsPA->getDebugName();
  else
    lhs.get<swift::Type>().print(out);

  switch (kind) {
  case Type:
  case Layout:
    out << ": ";
      break;

  case SameType:
    out << " == ";
    break;
  }

  // Print RHS.
  if (auto rhsPA = rhs.dyn_cast<PotentialArchetype *>())
    out << rhsPA->getDebugName();
  else if (auto rhsType = rhs.dyn_cast<swift::Type>())
    rhsType.print(out);
  else
    rhs.get<LayoutConstraint>().print(out);
}

void DelayedRequirement::dump() const {
  dump(llvm::errs());
  llvm::errs() << "\n";
}

ConstraintResult GenericSignatureBuilder::handleUnresolvedRequirement(
                                   RequirementKind kind,
                                   UnresolvedType lhs,
                                   UnresolvedRequirementRHS rhs,
                                   FloatingRequirementSource source,
                                   EquivalenceClass *unresolvedEquivClass,
                                   UnresolvedHandlingKind unresolvedHandling) {
  // Record the delayed requirement.
  DelayedRequirement::Kind delayedKind;
  switch (kind) {
  case RequirementKind::Conformance:
  case RequirementKind::Superclass:
    delayedKind = DelayedRequirement::Type;
    break;

  case RequirementKind::Layout:
    delayedKind = DelayedRequirement::Layout;
    break;

  case RequirementKind::SameType:
    delayedKind = DelayedRequirement::SameType;
    break;
  }

  if (unresolvedEquivClass) {
    unresolvedEquivClass->delayedRequirements.push_back(
                                          {delayedKind, lhs, rhs, source});
  } else {
    Impl->DelayedRequirements.push_back({delayedKind, lhs, rhs, source});
  }

  switch (unresolvedHandling) {
  case UnresolvedHandlingKind::GenerateConstraints:
    return ConstraintResult::Resolved;

  case UnresolvedHandlingKind::GenerateUnresolved:
    return ConstraintResult::Unresolved;
  }
}

// Function for feeding through any other requirements that the conformance
// requires to be satisfied. These are things we're inferring.
static void addConditionalRequirements(GenericSignatureBuilder &builder,
                                       ProtocolConformanceRef conformance) {
  // Abstract conformances don't have associated decl-contexts/modules, but also
  // don't have conditional requirements.
  if (conformance.isConcrete()) {
    auto source = FloatingRequirementSource::forInferred(nullptr);
    for (auto requirement : conformance.getConditionalRequirements()) {
      builder.addRequirement(requirement, source, /*inferForModule=*/nullptr);
      ++NumConditionalRequirementsAdded;
    }
  }
}

const RequirementSource *
GenericSignatureBuilder::resolveConcreteConformance(ResolvedType type,
                                                    ProtocolDecl *proto) {
  auto equivClass = type.getEquivalenceClass(*this);
  auto concrete = equivClass->concreteType;
  if (!concrete) return nullptr;

  // Conformance to this protocol is redundant; update the requirement source
  // appropriately.
  const RequirementSource *concreteSource;
  if (auto writtenSource =
        equivClass->findAnyConcreteConstraintAsWritten(nullptr))
    concreteSource = writtenSource->source;
  else
    concreteSource = equivClass->concreteTypeConstraints.front().source;

  // Lookup the conformance of the concrete type to this protocol.
  auto conformance =
      lookupConformance(type.getDependentType(*this)->getCanonicalType(),
                        concrete,
                        proto->getDeclaredInterfaceType()
                          ->castTo<ProtocolType>());
  if (!conformance) {
    if (!concrete->hasError() && concreteSource->getLoc().isValid()) {
      Impl->HadAnyError = true;

      Diags.diagnose(concreteSource->getLoc(),
                     diag::requires_generic_param_same_type_does_not_conform,
                     concrete, proto->getName());
    }

    Impl->HadAnyError = true;
    equivClass->invalidConcreteType = true;
    return nullptr;
  }

  concreteSource = concreteSource->viaConcrete(*this, *conformance);
  equivClass->recordConformanceConstraint(*this, type, proto, concreteSource);
  addConditionalRequirements(*this, *conformance);
  return concreteSource;
}
const RequirementSource *GenericSignatureBuilder::resolveSuperConformance(
                                                        ResolvedType type,
                                                        ProtocolDecl *proto) {
  // Get the superclass constraint.
  auto equivClass = type.getEquivalenceClass(*this);
  Type superclass = equivClass->superclass;
  if (!superclass) return nullptr;

  // Lookup the conformance of the superclass to this protocol.
  auto conformance =
    lookupConformance(type.getDependentType(*this)->getCanonicalType(),
                      superclass,
                      proto->getDeclaredInterfaceType()
                        ->castTo<ProtocolType>());
  if (!conformance) return nullptr;

  // Conformance to this protocol is redundant; update the requirement source
  // appropriately.
  const RequirementSource *superclassSource;
  if (auto writtenSource =
        equivClass->findAnySuperclassConstraintAsWritten())
    superclassSource = writtenSource->source;
  else
    superclassSource = equivClass->superclassConstraints.front().source;

  superclassSource =
    superclassSource->viaSuperclass(*this, *conformance);
  equivClass->recordConformanceConstraint(*this, type, proto, superclassSource);
  addConditionalRequirements(*this, *conformance);
  return superclassSource;
}

/// Realize a potential archetype for this type parameter.
PotentialArchetype *ResolvedType::realizePotentialArchetype(
                                           GenericSignatureBuilder &builder) {
  // Realize and cache the potential archetype.
  return builder.realizePotentialArchetype(type);
}

Type ResolvedType::getDependentType(GenericSignatureBuilder &builder) const {
  // Already-resolved potential archetype.
  if (auto pa = type.dyn_cast<PotentialArchetype *>())
    return pa->getDependentType(builder.getGenericParams());

  Type result = type.get<Type>();
  return result->isTypeParameter() ? result : Type();
}

/// If there is a same-type requirement to be added for the given nested type
/// due to a superclass constraint on the parent type, add it now.
static void maybeAddSameTypeRequirementForNestedType(
                                          ResolvedType nested,
                                          const RequirementSource *superSource,
                                          GenericSignatureBuilder &builder) {
  // If there's no super conformance, we're done.
  if (!superSource) return;

  // If the nested type is already concrete, we're done.
  if (nested.getAsConcreteType()) return;

  // Dig out the associated type.
  AssociatedTypeDecl *assocType = nullptr;
  if (auto depMemTy =
        nested.getDependentType(builder)->getAs<DependentMemberType>())
    assocType = depMemTy->getAssocType();

  if (!assocType) return;

  // Dig out the type witness.
  auto superConformance = superSource->getProtocolConformance().getConcrete();
  auto concreteType =
    superConformance->getTypeWitness(assocType, builder.getLazyResolver());
  if (!concreteType) return;

  // We should only have interface types here.
  assert(!superConformance->getType()->hasArchetype());
  assert(!concreteType->hasArchetype());

  // Add the same-type constraint.
  auto nestedSource = superSource->viaParent(builder, assocType);

  builder.addSameTypeRequirement(
        nested.getUnresolvedType(), concreteType, nestedSource,
        GenericSignatureBuilder::UnresolvedHandlingKind::GenerateConstraints);
}

auto PotentialArchetype::getOrCreateEquivalenceClass(
                                       GenericSignatureBuilder &builder) const
    -> EquivalenceClass * {
  // The equivalence class is stored on the representative.
  auto representative = getRepresentative();
  if (representative != this)
    return representative->getOrCreateEquivalenceClass(builder);

  // If we already have an equivalence class, return it.
  if (auto equivClass = getEquivalenceClassIfPresent())
    return equivClass;

  // Create a new equivalence class.
  auto equivClass =
    builder.Impl->allocateEquivalenceClass(
      const_cast<PotentialArchetype *>(this));
  representativeOrEquivClass = equivClass;
  return equivClass;
}

auto PotentialArchetype::getRepresentative() const -> PotentialArchetype * {
  auto representative =
    representativeOrEquivClass.dyn_cast<PotentialArchetype *>();
  if (!representative)
    return const_cast<PotentialArchetype *>(this);

  // Find the representative.
  PotentialArchetype *result = representative;
  while (auto nextRepresentative =
           result->representativeOrEquivClass.dyn_cast<PotentialArchetype *>())
    result = nextRepresentative;

  // Perform (full) path compression.
  const PotentialArchetype *fixUp = this;
  while (auto nextRepresentative =
           fixUp->representativeOrEquivClass.dyn_cast<PotentialArchetype *>()) {
    fixUp->representativeOrEquivClass = nextRepresentative;
    fixUp = nextRepresentative;
  }

  return result;
}

/// Canonical ordering for dependent types.
int swift::compareDependentTypes(Type type1, Type type2) {
  // Fast-path check for equality.
  if (type1->isEqual(type2)) return 0;

  // Ordering is as follows:
  // - Generic params
  auto gp1 = type1->getAs<GenericTypeParamType>();
  auto gp2 = type2->getAs<GenericTypeParamType>();
  if (gp1 && gp2)
    return GenericParamKey(gp1) < GenericParamKey(gp2) ? -1 : +1;

  // A generic parameter is always ordered before a nested type.
  if (static_cast<bool>(gp1) != static_cast<bool>(gp2))
    return gp1 ? -1 : +1;

  // - Dependent members
  auto depMemTy1 = type1->castTo<DependentMemberType>();
  auto depMemTy2 = type2->castTo<DependentMemberType>();

  // - by base, so t_0_n.`P.T` < t_1_m.`P.T`
  if (int compareBases =
        compareDependentTypes(depMemTy1->getBase(), depMemTy2->getBase()))
    return compareBases;

  // - by name, so t_n_m.`P.T` < t_n_m.`P.U`
  if (int compareNames = depMemTy1->getName().str().compare(
                                                  depMemTy2->getName().str()))
    return compareNames;

  if (auto *assocType1 = depMemTy1->getAssocType()) {
    if (auto *assocType2 = depMemTy2->getAssocType()) {
      if (int result = compareAssociatedTypes(assocType1, assocType2))
        return result;
    } else {
      // A resolved archetype is always ordered before an unresolved one.
      return -1;
    }
  } else {
    // A resolved archetype is always ordered before an unresolved one.
    if (depMemTy2->getAssocType())
      return +1;
  }

  return 0;
}

namespace {
  /// Function object used to suppress conflict diagnoses when we know we'll
  /// see them again later.
  struct SameTypeConflictCheckedLater {
    void operator()(Type type1, Type type2) const { }
  };
} // end anonymous namespace

// Give a nested type the appropriately resolved concrete type, based off a
// parent PA that has a concrete type.
static void concretizeNestedTypeFromConcreteParent(
    GenericSignatureBuilder::PotentialArchetype *parent,
    GenericSignatureBuilder::PotentialArchetype *nestedPA,
    GenericSignatureBuilder &builder) {
  auto parentEquiv = parent->getEquivalenceClassIfPresent();
  assert(parentEquiv && "can't have a concrete type without an equiv class");
  auto concreteParent = parentEquiv->concreteType;
  assert(concreteParent &&
         "attempting to resolve concrete nested type of non-concrete PA");

  // These requirements are all implied based on the parent's concrete
  // conformance.
  auto assocType = nestedPA->getResolvedAssociatedType();
  if (!assocType) return;

  auto proto = assocType->getProtocol();

  // If we don't already have a conformance of the parent to this protocol,
  // add it now; it was elided earlier.
  if (parentEquiv->conformsTo.count(proto) == 0) {
    auto source = parentEquiv->concreteTypeConstraints.front().source;
    parentEquiv->recordConformanceConstraint(builder, parent, proto, source);
  }

  assert(parentEquiv->conformsTo.count(proto) > 0 &&
         "No conformance requirement");
  const RequirementSource *parentConcreteSource = nullptr;
  for (const auto &constraint : parentEquiv->conformsTo.find(proto)->second) {
    if (constraint.source->kind == RequirementSource::Concrete) {
      parentConcreteSource = constraint.source;
    }
  }

  // Error condition: parent did not conform to this protocol, so there is no
  // way to resolve the nested type via concrete conformance.
  if (!parentConcreteSource) return;

  auto source = parentConcreteSource->viaParent(builder, assocType);
  auto conformance = parentConcreteSource->getProtocolConformance();

  Type witnessType;
  if (conformance.isConcrete()) {
    witnessType =
      conformance.getConcrete()
        ->getTypeWitness(assocType, builder.getLazyResolver());
    if (!witnessType || witnessType->hasError())
      return; // FIXME: should we delay here?
  } else {
    witnessType = DependentMemberType::get(concreteParent, assocType);
  }

  builder.addSameTypeRequirement(
         nestedPA, witnessType, source,
         GenericSignatureBuilder::UnresolvedHandlingKind::GenerateConstraints,
         SameTypeConflictCheckedLater());
}

PotentialArchetype *PotentialArchetype::updateNestedTypeForConformance(
                                              GenericSignatureBuilder &builder,
                                              TypeDecl *type,
                                              ArchetypeResolutionKind kind) {
  if (!type) return nullptr;

  AssociatedTypeDecl *assocType = dyn_cast<AssociatedTypeDecl>(type);

  // Always refer to the archetype anchor.
  if (assocType)
    assocType = assocType->getAssociatedTypeAnchor();

  TypeDecl *concreteDecl = assocType ? nullptr : type;

  // If we were asked for a complete, well-formed archetype, make sure we
  // process delayed requirements if anything changed.
  SWIFT_DEFER {
    if (kind == ArchetypeResolutionKind::CompleteWellFormed)
      builder.processDelayedRequirements();
  };

  Identifier name = assocType ? assocType->getName() : concreteDecl->getName();
  ProtocolDecl *proto =
    assocType ? assocType->getProtocol()
              : concreteDecl->getDeclContext()
                  ->getAsProtocolOrProtocolExtensionContext();

  // Look for either an unresolved potential archetype (which we can resolve
  // now) or a potential archetype with the appropriate associated type or
  // concrete type.
  PotentialArchetype *resultPA = nullptr;
  auto knownNestedTypes = NestedTypes.find(name);
  bool shouldUpdatePA = false;
  if (knownNestedTypes != NestedTypes.end()) {
    for (auto existingPA : knownNestedTypes->second) {
      // Do we have an associated-type match?
      if (assocType && existingPA->getResolvedAssociatedType() == assocType) {
        resultPA = existingPA;
        break;
      }

      // Do we have a concrete type match?
      if (concreteDecl && existingPA->getConcreteTypeDecl() == concreteDecl) {
        resultPA = existingPA;
        break;
      }
    }
  }

  // If we don't have a result potential archetype yet, we may need to add one.
  if (!resultPA) {
    switch (kind) {
    case ArchetypeResolutionKind::CompleteWellFormed:
    case ArchetypeResolutionKind::WellFormed: {
      // Creating a new potential archetype in an equivalence class is a
      // modification.
      getOrCreateEquivalenceClass(builder)->modified(builder);

      void *mem = builder.Impl->Allocator.Allocate<PotentialArchetype>();
      if (assocType)
        resultPA = new (mem) PotentialArchetype(this, assocType);
      else
        resultPA = new (mem) PotentialArchetype(this, concreteDecl);

      NestedTypes[name].push_back(resultPA);
      builder.addedNestedType(resultPA);
      shouldUpdatePA = true;
      break;
    }

    case ArchetypeResolutionKind::AlreadyKnown:
      return nullptr;
    }
  }

  // If we have a potential archetype that requires more processing, do so now.
  if (shouldUpdatePA) {
    // For concrete types, introduce a same-type requirement to the aliased
    // type.
    if (concreteDecl) {
      // FIXME (recursive decl validation): if the alias doesn't have an
      // interface type when getNestedType is called while building a
      // protocol's generic signature (i.e. during validation), then it'll
      // fail completely, because building that alias's interface type
      // requires the protocol to be validated. This seems to occur when the
      // alias's RHS involves archetypes from the protocol.
      if (!concreteDecl->hasInterfaceType())
        builder.getLazyResolver()->resolveDeclSignature(concreteDecl);
      if (concreteDecl->hasInterfaceType()) {
        // The protocol concrete type has an underlying type written in terms
        // of the protocol's 'Self' type.
        auto type = concreteDecl->getDeclaredInterfaceType();

        if (proto) {
          // Substitute in the type of the current PotentialArchetype in
          // place of 'Self' here.
          auto subMap = SubstitutionMap::getProtocolSubstitutions(
            proto, getDependentType(builder.getGenericParams()),
            ProtocolConformanceRef(proto));
          type = type.subst(subMap, SubstFlags::UseErrorType);
          if (!type)
            type = ErrorType::get(proto->getASTContext());
        } else {
          // Substitute in the superclass type.
          auto superclass = getEquivalenceClassIfPresent()->superclass;
          auto superclassDecl = superclass->getClassOrBoundGenericClass();
          type = superclass->getTypeOfMember(
                   superclassDecl->getParentModule(), concreteDecl,
                   concreteDecl->getDeclaredInterfaceType());
        }

        builder.addSameTypeRequirement(
                         UnresolvedType(resultPA),
                         UnresolvedType(type),
                         RequirementSource::forConcreteTypeBinding(
                           builder,
                           resultPA->getDependentType(builder.getGenericParams())),
                         UnresolvedHandlingKind::GenerateConstraints);
      }
    }

    // If there's a superclass constraint that conforms to the protocol,
    // add the appropriate same-type relationship.
    if (proto) {
      if (auto superSource = builder.resolveSuperConformance(this, proto)) {
        maybeAddSameTypeRequirementForNestedType(resultPA, superSource,
                                                 builder);
      }
    }

    // We know something concrete about the parent PA, so we need to propagate
    // that information to this new archetype.
    if (isConcreteType()) {
      concretizeNestedTypeFromConcreteParent(this, resultPA, builder);
    }
  }

  return resultPA;
}

void ArchetypeType::resolveNestedType(
                                    std::pair<Identifier, Type> &nested) const {
  auto genericEnv = getGenericEnvironment();
  auto &builder = *genericEnv->getGenericSignatureBuilder();

  Type interfaceType = getInterfaceType();
  Type memberInterfaceType =
    DependentMemberType::get(interfaceType, nested.first);
  auto equivClass =
    builder.resolveEquivalenceClass(
                                  memberInterfaceType,
                                  ArchetypeResolutionKind::CompleteWellFormed);
  auto result = equivClass->getTypeInContext(builder, genericEnv);
  assert(!nested.second ||
         nested.second->isEqual(result) ||
         (nested.second->hasError() && result->hasError()));
  nested.second = result;
}

Type GenericSignatureBuilder::PotentialArchetype::getDependentType(
                        ArrayRef<GenericTypeParamType *> genericParams) const {
  if (auto parent = getParent()) {
    Type parentType = parent->getDependentType(genericParams);
    if (parentType->hasError())
      return parentType;

    // If we've resolved to an associated type, use it.
    if (auto assocType = getResolvedAssociatedType())
      return DependentMemberType::get(parentType, assocType);

    return DependentMemberType::get(parentType, getNestedName());
  }
  
  assert(isGenericParam() && "Not a generic parameter?");

  if (genericParams.empty()) {
    return GenericTypeParamType::get(getGenericParamKey().Depth,
                                     getGenericParamKey().Index,
                                     getASTContext());
  }

  unsigned index = getGenericParamKey().findIndexIn(genericParams);
  return genericParams[index];
}

ASTContext &PotentialArchetype::getASTContext() const {
  if (auto context = parentOrContext.dyn_cast<ASTContext *>())
    return *context;

  return getResolvedType()->getASTContext();
}

void GenericSignatureBuilder::PotentialArchetype::dump() const {
  dump(llvm::errs(), nullptr, 0);
}

void GenericSignatureBuilder::PotentialArchetype::dump(llvm::raw_ostream &Out,
                                                       SourceManager *SrcMgr,
                                                       unsigned Indent) const {
  // Print name.
  if (Indent == 0 || isGenericParam())
    Out << getDebugName();
  else
    Out.indent(Indent) << getNestedName();

  auto equivClass = getEquivalenceClassIfPresent();

  // Print superclass.
  if (equivClass && equivClass->superclass) {
    for (const auto &constraint : equivClass->superclassConstraints) {
      if (!constraint.isSubjectEqualTo(this)) continue;

      Out << " : ";
      constraint.value.print(Out);

      Out << " ";
      if (!constraint.source->isDerivedRequirement())
        Out << "*";
      Out << "[";
      constraint.source->print(Out, SrcMgr);
      Out << "]";
    }
  }

  // Print concrete type.
  if (equivClass && equivClass->concreteType) {
    for (const auto &constraint : equivClass->concreteTypeConstraints) {
      if (!constraint.isSubjectEqualTo(this)) continue;

      Out << " == ";
      constraint.value.print(Out);

      Out << " ";
      if (!constraint.source->isDerivedRequirement())
        Out << "*";
      Out << "[";
      constraint.source->print(Out, SrcMgr);
      Out << "]";
    }
  }

  // Print requirements.
  if (equivClass) {
    bool First = true;
    for (const auto &entry : equivClass->conformsTo) {
      for (const auto &constraint : entry.second) {
        if (!constraint.isSubjectEqualTo(this)) continue;

        if (First) {
          First = false;
          Out << ": ";
        } else {
          Out << " & ";
        }

        Out << constraint.value->getName().str() << " ";
        if (!constraint.source->isDerivedRequirement())
          Out << "*";
        Out << "[";
        constraint.source->print(Out, SrcMgr);
        Out << "]";
      }
    }
  }

  if (getRepresentative() != this) {
    Out << " [represented by " << getRepresentative()->getDebugName() << "]";
  }

  if (getEquivalenceClassMembers().size() > 1) {
    Out << " [equivalence class ";
    bool isFirst = true;
    for (auto equiv : getEquivalenceClassMembers()) {
      if (equiv == this) continue;

      if (isFirst) isFirst = false;
      else Out << ", ";

      Out << equiv->getDebugName();
    }
    Out << "]";
  }

  Out << "\n";

  // Print nested types.
  for (const auto &nestedVec : NestedTypes) {
    for (auto nested : nestedVec.second) {
      nested->dump(Out, SrcMgr, Indent + 2);
    }
  }
}

#pragma mark Equivalence classes
EquivalenceClass::EquivalenceClass(PotentialArchetype *representative)
  : recursiveConcreteType(false), invalidConcreteType(false),
    recursiveSuperclassType(false)
{
  members.push_back(representative);
}

void EquivalenceClass::modified(GenericSignatureBuilder &builder) {
  builder.Impl->Generation++;

  // Transfer any delayed requirements to the primary queue, because they
  // might be resolvable now.
  builder.Impl->DelayedRequirements.append(delayedRequirements.begin(),
                                           delayedRequirements.end());
  delayedRequirements.clear();
}

GenericSignatureBuilder::GenericSignatureBuilder(
                               ASTContext &ctx)
  : Context(ctx), Diags(Context.Diags), Impl(new Implementation) {
  if (Context.Stats)
    Context.Stats->getFrontendCounters().NumGenericSignatureBuilders++;
}

GenericSignatureBuilder::GenericSignatureBuilder(
                                         GenericSignatureBuilder &&other)
  : Context(other.Context), Diags(other.Diags), Impl(std::move(other.Impl))
{
  other.Impl.reset();

  if (Impl) {
    // Update the generic parameters to their canonical types.
    for (auto &gp : Impl->GenericParams) {
      gp = gp->getCanonicalType()->castTo<GenericTypeParamType>();
    }
  }
}

GenericSignatureBuilder::~GenericSignatureBuilder() = default;

auto
GenericSignatureBuilder::getLookupConformanceFn()
    -> LookUpConformanceInBuilder {
  return LookUpConformanceInBuilder(this);
}

Optional<ProtocolConformanceRef>
GenericSignatureBuilder::lookupConformance(CanType dependentType,
                                           Type conformingReplacementType,
                                           ProtocolType *conformedProtocol) {
  if (conformingReplacementType->isTypeParameter())
    return ProtocolConformanceRef(conformedProtocol->getDecl());

  // Figure out which module to look into.
  // FIXME: When lookupConformance() starts respecting modules, we'll need
  // to do some filtering here.
  ModuleDecl *searchModule = conformedProtocol->getDecl()->getParentModule();
  auto result = searchModule->lookupConformance(conformingReplacementType,
                                                conformedProtocol->getDecl());
  if (result && getLazyResolver())
    getLazyResolver()->markConformanceUsed(*result, searchModule);

  return result;
}

LazyResolver *GenericSignatureBuilder::getLazyResolver() const { 
  return Context.getLazyResolver();
}

/// Resolve any unresolved dependent member types using the given builder.
static Type resolveDependentMemberTypes(GenericSignatureBuilder &builder,
                                        Type type) {
  if (!type->hasTypeParameter()) return type;

  return type.transformRec([&builder](TypeBase *type) -> Optional<Type> {
    if (!type->isTypeParameter())
      return None;

    // Map the type parameter to an equivalence class.
    auto equivClass =
      builder.resolveEquivalenceClass(Type(type),
                                      ArchetypeResolutionKind::WellFormed);
    if (!equivClass)
      return ErrorType::get(Type(type));

    // If there is a concrete type in this equivalence class, use that.
    if (equivClass->concreteType) {
      // .. unless it's recursive.
      if (equivClass->recursiveConcreteType)
        return ErrorType::get(Type(type));

      return resolveDependentMemberTypes(builder, equivClass->concreteType);
    }

    return equivClass->getAnchor(builder, builder.getGenericParams());
  });
}

PotentialArchetype *GenericSignatureBuilder::realizePotentialArchetype(
                                                     UnresolvedType &type) {
  if (auto pa = type.dyn_cast<PotentialArchetype *>())
    return pa;

  auto pa = maybeResolveEquivalenceClass(type.get<Type>(),
                                         ArchetypeResolutionKind::WellFormed,
                                         /*wantExactPotentialArchetype=*/true)
    .getPotentialArchetypeIfKnown();
  if (pa) type = pa;

  return pa;
}

ResolvedType GenericSignatureBuilder::maybeResolveEquivalenceClass(
                                    Type type,
                                    ArchetypeResolutionKind resolutionKind,
                                    bool wantExactPotentialArchetype) {
  // The equivalence class of a generic type is known directly.
  if (auto genericParam = type->getAs<GenericTypeParamType>()) {
    unsigned index = GenericParamKey(genericParam).findIndexIn(
                                                           Impl->GenericParams);
    if (index < Impl->GenericParams.size()) {
      return ResolvedType(Impl->PotentialArchetypes[index]);
    }

    return ResolvedType::forUnresolved(nullptr);
  }

  // The equivalence class of a dependent member type is determined by its
  // base equivalence class.
  if (auto depMemTy = type->getAs<DependentMemberType>()) {
    // Find the equivalence class of the base.
    auto resolvedBase =
      maybeResolveEquivalenceClass(depMemTy->getBase(),
                                   resolutionKind,
                                   wantExactPotentialArchetype);
    if (!resolvedBase) return resolvedBase;

    // Find the nested type declaration for this.
    auto baseEquivClass = resolvedBase.getEquivalenceClass(*this);
    TypeDecl *nestedTypeDecl;
    SmallVector<TypeDecl *, 4> concreteDecls;
    if (auto assocType = depMemTy->getAssocType()) {
      // Check whether this associated type references a protocol to which
      // the base conforms. If not, it's unresolved.
      if (baseEquivClass->conformsTo.find(assocType->getProtocol())
            == baseEquivClass->conformsTo.end())
        return ResolvedType::forUnresolved(baseEquivClass);

      nestedTypeDecl = assocType;
    } else {
      nestedTypeDecl =
        baseEquivClass->lookupNestedType(*this, depMemTy->getName(),
                                         &concreteDecls);

      if (!nestedTypeDecl) {
        return ResolvedType::forUnresolved(baseEquivClass);
      }
    }

    // Retrieve the "smallest" type in the equivalence class, by depth, and
    // use that to find a nested potential archetype. We used the smallest
    // type by depth to limit expansion of the type graph.
    PotentialArchetype *basePA;
    if (wantExactPotentialArchetype) {
      basePA = resolvedBase.getPotentialArchetypeIfKnown();
      if (!basePA) return ResolvedType::forUnresolved(baseEquivClass);
    } else {
      basePA = baseEquivClass->members.front();
    }

    auto nestedPA =
      basePA->updateNestedTypeForConformance(*this, nestedTypeDecl,
                                             resolutionKind);
    if (!nestedPA)
      return ResolvedType::forUnresolved(baseEquivClass);

    if (resolutionKind != ArchetypeResolutionKind::AlreadyKnown) {
      // Update for all of the concrete decls with this name, which will
      // introduce various same-type constraints.
      for (auto concreteDecl : concreteDecls) {
        (void)basePA->updateNestedTypeForConformance(*this, concreteDecl,
                                                     resolutionKind);
      }
    }

    // If base resolved to the anchor, then the nested potential archetype
    // we found is the resolved potential archetype. Return it directly,
    // so it doesn't need to be resolved again.
    if (basePA == resolvedBase.getPotentialArchetypeIfKnown())
      return ResolvedType(nestedPA);

    // Compute the resolved dependent type to return.
    Type resolvedBaseType = resolvedBase.getDependentType(*this);
    Type resolvedMemberType;
    if (auto assocType = dyn_cast<AssociatedTypeDecl>(nestedTypeDecl)) {
      resolvedMemberType =
        DependentMemberType::get(resolvedBaseType, assocType);
    } else {
      // Note: strange case that might not even really be dependent.
      resolvedMemberType =
        DependentMemberType::get(resolvedBaseType, depMemTy->getName());
    }

    return ResolvedType(resolvedMemberType,
                         nestedPA->getOrCreateEquivalenceClass(*this));
  }

  // If it's not a type parameter, it won't directly resolve to one.
  // FIXME: Generic typealiases contradict the assumption above.
  // If there is a type parameter somewhere in this type, resolve it.
  if (type->hasTypeParameter()) {
    Type resolved = resolveDependentMemberTypes(*this, type);
    if (resolved->hasError() && !type->hasError())
      return ResolvedType::forUnresolved(nullptr);

    type = resolved;
  }

  return ResolvedType::forConcrete(type);
}

EquivalenceClass *GenericSignatureBuilder::resolveEquivalenceClass(
                                    Type type,
                                    ArchetypeResolutionKind resolutionKind) {
  if (auto resolved =
        maybeResolveEquivalenceClass(type, resolutionKind,
                                     /*wantExactPotentialArchetype=*/false))
    return resolved.getEquivalenceClass(*this);

  return nullptr;
}

auto GenericSignatureBuilder::resolve(UnresolvedType paOrT,
                                      FloatingRequirementSource source)
    -> ResolvedType {
  if (auto pa = paOrT.dyn_cast<PotentialArchetype *>())
    return ResolvedType(pa);

  // Determine what kind of resolution we want.
    Type type = paOrT.dyn_cast<Type>();
  ArchetypeResolutionKind resolutionKind =
    ArchetypeResolutionKind::WellFormed;
  if (!source.isExplicit() && source.isRecursive(type, *this))
    resolutionKind = ArchetypeResolutionKind::AlreadyKnown;

  return maybeResolveEquivalenceClass(type, resolutionKind,
                                      /*wantExactPotentialArchetype=*/true);
}

bool GenericSignatureBuilder::areInSameEquivalenceClass(Type type1,
                                                        Type type2) {
  return resolveEquivalenceClass(type1, ArchetypeResolutionKind::WellFormed)
    == resolveEquivalenceClass(type2, ArchetypeResolutionKind::WellFormed);
}

ArrayRef<GenericTypeParamType *>
GenericSignatureBuilder::getGenericParams() const {
  return Impl->GenericParams;
}

void GenericSignatureBuilder::addGenericParameter(GenericTypeParamDecl *GenericParam) {
  addGenericParameter(
     GenericParam->getDeclaredInterfaceType()->castTo<GenericTypeParamType>());
}

bool GenericSignatureBuilder::addGenericParameterRequirements(
                                           GenericTypeParamDecl *GenericParam) {
  GenericParamKey Key(GenericParam);
  auto PA = Impl->PotentialArchetypes[Key.findIndexIn(Impl->GenericParams)];
  
  // Add the requirements from the declaration.
  return isErrorResult(
           addInheritedRequirements(GenericParam, PA, nullptr,
                                    GenericParam->getModuleContext()));
}

void GenericSignatureBuilder::addGenericParameter(GenericTypeParamType *GenericParam) {
  GenericParamKey Key(GenericParam);
  assert(Impl->GenericParams.empty() ||
         ((Key.Depth == Impl->GenericParams.back()->getDepth() &&
           Key.Index == Impl->GenericParams.back()->getIndex() + 1) ||
          (Key.Depth > Impl->GenericParams.back()->getDepth() &&
           Key.Index == 0)));

  // Create a potential archetype for this type parameter.
  void *mem = Impl->Allocator.Allocate<PotentialArchetype>();
  auto PA = new (mem) PotentialArchetype(getASTContext(), GenericParam);
  Impl->GenericParams.push_back(GenericParam);
  Impl->PotentialArchetypes.push_back(PA);
}

/// Visit all of the types that show up in the list of inherited
/// types.
static ConstraintResult visitInherited(
         ArrayRef<TypeLoc> inheritedTypes,
         llvm::function_ref<ConstraintResult(Type, const TypeRepr *)> visitType) {
  // Local function that (recursively) adds inherited types.
  ConstraintResult result = ConstraintResult::Resolved;
  std::function<void(Type, const TypeRepr *)> visitInherited;

  // FIXME: Should this whole thing use getExistentialLayout() instead?

  visitInherited = [&](Type inheritedType, const TypeRepr *typeRepr) {
    // Decompose explicitly-written protocol compositions.
    if (auto composition = dyn_cast_or_null<CompositionTypeRepr>(typeRepr)) {
      if (auto compositionType
            = inheritedType->getAs<ProtocolCompositionType>()) {
        unsigned index = 0;
        for (auto memberType : compositionType->getMembers()) {
          visitInherited(memberType, composition->getTypes()[index]);
          index++;
        }

        return;
      }
    }

    auto recursiveResult = visitType(inheritedType, typeRepr);
    if (isErrorResult(recursiveResult) && !isErrorResult(result))
      result = recursiveResult;
  };

  // Visit all of the inherited types.
  for (auto inherited : inheritedTypes) {
    visitInherited(inherited.getType(), inherited.getTypeRepr());
  }

  return result;
}

ConstraintResult GenericSignatureBuilder::expandConformanceRequirement(
                                            ResolvedType selfType,
                                            ProtocolDecl *proto,
                                            const RequirementSource *source,
                                            bool onlySameTypeConstraints) {
  auto protocolSubMap = SubstitutionMap::getProtocolSubstitutions(
      proto, selfType.getDependentType(*this), ProtocolConformanceRef(proto));

  // Use the requirement signature to avoid rewalking the entire protocol.  This
  // cannot compute the requirement signature directly, because that may be
  // infinitely recursive: this code is also used to construct it.
  if (proto->isRequirementSignatureComputed()) {
    auto innerSource =
      FloatingRequirementSource::viaProtocolRequirement(source, proto,
                                                        /*inferred=*/false);
    for (const auto &req : proto->getRequirementSignature()) {
      // If we're only looking at same-type constraints, skip everything else.
      if (onlySameTypeConstraints && req.getKind() != RequirementKind::SameType)
        continue;

      auto substReq = req.subst(protocolSubMap);
      auto reqResult = substReq
                           ? addRequirement(*substReq, innerSource, nullptr)
                           : ConstraintResult::Conflicting;
      if (isErrorResult(reqResult)) return reqResult;
    }

    return ConstraintResult::Resolved;
  }

  if (!onlySameTypeConstraints) {
    // Add all of the inherited protocol requirements, recursively.
    if (auto resolver = getLazyResolver())
      resolver->resolveInheritedProtocols(proto);

    auto inheritedReqResult =
      addInheritedRequirements(proto, selfType.getUnresolvedType(), source,
                               /*inferForModule=*/nullptr);
    if (isErrorResult(inheritedReqResult))
      return inheritedReqResult;
  }

  // Add any requirements in the where clause on the protocol.
  if (auto WhereClause = proto->getTrailingWhereClause()) {
    for (auto &req : WhereClause->getRequirements()) {
      // If we're only looking at same-type constraints, skip everything else.
      if (onlySameTypeConstraints &&
          req.getKind() != RequirementReprKind::SameType)
        continue;

      auto innerSource = FloatingRequirementSource::viaProtocolRequirement(
          source, proto, &req, /*inferred=*/false);
      addRequirement(&req, innerSource, &protocolSubMap,
                     /*inferForModule=*/nullptr);
    }
  }

  // Remaining logic is not relevant in ObjC protocol cases.
  if (proto->isObjC())
    return ConstraintResult::Resolved;

  // Collect all of the inherited associated types and typealiases in the
  // inherited protocols (recursively).
  llvm::MapVector<DeclName, TinyPtrVector<TypeDecl *>> inheritedTypeDecls;
  {
    proto->walkInheritedProtocols(
        [&](ProtocolDecl *inheritedProto) -> TypeWalker::Action {
      if (inheritedProto == proto) return TypeWalker::Action::Continue;

      for (auto req : inheritedProto->getMembers()) {
        if (auto typeReq = dyn_cast<TypeDecl>(req))
          inheritedTypeDecls[typeReq->getFullName()].push_back(typeReq);
      }
      return TypeWalker::Action::Continue;
    });
  }

  // Local function to find the insertion point for the protocol's "where"
  // clause, as well as the string to start the insertion ("where" or ",");
  auto getProtocolWhereLoc = [&]() -> std::pair<SourceLoc, const char *> {
    // Already has a trailing where clause.
    if (auto trailing = proto->getTrailingWhereClause())
      return { trailing->getRequirements().back().getSourceRange().End, ", " };

    // Inheritance clause.
    return { proto->getInherited().back().getSourceRange().End, " where " };
  };

  // Retrieve the set of requirements that a given associated type declaration
  // produces, in the form that would be seen in the where clause.
  auto getAssociatedTypeReqs = [&](AssociatedTypeDecl *assocType,
                                   const char *start) {
    std::string result;
    {
      llvm::raw_string_ostream out(result);
      out << start;
      interleave(assocType->getInherited(), [&](TypeLoc inheritedType) {
        out << assocType->getFullName() << ": ";
        if (auto inheritedTypeRepr = inheritedType.getTypeRepr())
          inheritedTypeRepr->print(out);
        else
          inheritedType.getType().print(out);
      }, [&] {
        out << ", ";
      });
    }
    return result;
  };

  // Retrieve the requirement that a given typealias introduces when it
  // overrides an inherited associated type with the same name, as a string
  // suitable for use in a where clause.
  auto getConcreteTypeReq = [&](TypeDecl *type, const char *start) {
    std::string result;
    {
      llvm::raw_string_ostream out(result);
      out << start;
      out << type->getFullName() << " == ";
      if (auto typealias = dyn_cast<TypeAliasDecl>(type)) {
        if (auto underlyingTypeRepr =
              typealias->getUnderlyingTypeLoc().getTypeRepr())
          underlyingTypeRepr->print(out);
        else
          typealias->getUnderlyingTypeLoc().getType().print(out);
      } else {
        type->print(out);
      }
    }
    return result;
  };

  // Form an unsubstituted type referring to the given type declaration,
  // for use in an inferred same-type requirement.
  auto formUnsubstitutedType = [&](TypeDecl *typeDecl) -> Type {
    if (auto assocType = dyn_cast<AssociatedTypeDecl>(typeDecl)) {
      return DependentMemberType::get(
                               assocType->getProtocol()->getSelfInterfaceType(),
                               assocType);
    }

    // Resolve the underlying type, if we haven't done so yet.
    if (!typeDecl->hasInterfaceType()) {
      getLazyResolver()->resolveDeclSignature(typeDecl);
    }

    if (auto typealias = dyn_cast<TypeAliasDecl>(typeDecl)) {
      return typealias->getUnderlyingTypeLoc().getType();
    }

    return typeDecl->getDeclaredInterfaceType();
  };

  // An inferred same-type requirement between the two type declarations
  // within this protocol or a protocol it inherits.
  auto addInferredSameTypeReq = [&](TypeDecl *first, TypeDecl *second) {
    Type firstType = formUnsubstitutedType(first);
    if (!firstType) return;

    Type secondType = formUnsubstitutedType(second);
    if (!secondType) return;

    auto inferredSameTypeSource =
      FloatingRequirementSource::viaProtocolRequirement(
                    source, proto, WrittenRequirementLoc(), /*inferred=*/true);

    auto rawReq = Requirement(RequirementKind::SameType, firstType, secondType);
    if (auto req = rawReq.subst(protocolSubMap))
      addRequirement(*req, inferredSameTypeSource, proto->getParentModule());
  };

  // Add requirements for each of the associated types.
  for (auto Member : proto->getMembers()) {
    if (auto assocTypeDecl = dyn_cast<AssociatedTypeDecl>(Member)) {
      // Add requirements placed directly on this associated type.
      Type assocType =
        DependentMemberType::get(selfType.getDependentType(*this), assocTypeDecl);
      if (!onlySameTypeConstraints) {
        auto assocResult =
          addInheritedRequirements(assocTypeDecl, assocType, source,
                                   /*inferForModule=*/nullptr);
        if (isErrorResult(assocResult))
          return assocResult;
      }

      // Add requirements from this associated type's where clause.
      if (auto WhereClause = assocTypeDecl->getTrailingWhereClause()) {
        for (auto &req : WhereClause->getRequirements()) {
          // If we're only looking at same-type constraints, skip everything
          // else.
          if (onlySameTypeConstraints &&
              req.getKind() != RequirementReprKind::SameType)
            continue;

          auto innerSource =
            FloatingRequirementSource::viaProtocolRequirement(
                                      source, proto, &req, /*inferred=*/false);
          addRequirement(&req, innerSource, &protocolSubMap,
                         /*inferForModule=*/nullptr);
        }
      }

      // Check whether we inherited any types with the same name.
      auto knownInherited =
        inheritedTypeDecls.find(assocTypeDecl->getFullName());
      if (knownInherited == inheritedTypeDecls.end()) continue;

      bool shouldWarnAboutRedeclaration =
        source->kind == RequirementSource::RequirementSignatureSelf &&
        assocTypeDecl->getDefaultDefinitionLoc().isNull() &&
        (!assocTypeDecl->getInherited().empty() ||
         assocTypeDecl->getTrailingWhereClause());
      for (auto inheritedType : knownInherited->second) {
        // If we have inherited associated type...
        if (auto inheritedAssocTypeDecl =
              dyn_cast<AssociatedTypeDecl>(inheritedType)) {
          // Complain about the first redeclaration.
          if (shouldWarnAboutRedeclaration) {
            auto inheritedFromProto = inheritedAssocTypeDecl->getProtocol();
            auto fixItWhere = getProtocolWhereLoc();
            Diags.diagnose(assocTypeDecl,
                           diag::inherited_associated_type_redecl,
                           assocTypeDecl->getFullName(),
                           inheritedFromProto->getDeclaredInterfaceType())
              .fixItInsertAfter(
                       fixItWhere.first,
                       getAssociatedTypeReqs(assocTypeDecl, fixItWhere.second))
              .fixItRemove(assocTypeDecl->getSourceRange());

            Diags.diagnose(inheritedAssocTypeDecl, diag::decl_declared_here,
                           inheritedAssocTypeDecl->getFullName());

            shouldWarnAboutRedeclaration = false;
          }

          continue;
        }

        // We inherited a type; this associated type will be identical
        // to that typealias.
        if (source->kind == RequirementSource::RequirementSignatureSelf) {
          auto inheritedOwningDecl =
            inheritedType->getDeclContext()
              ->getAsNominalTypeOrNominalTypeExtensionContext();
          Diags.diagnose(assocTypeDecl,
                         diag::associated_type_override_typealias,
                         assocTypeDecl->getFullName(),
                         inheritedOwningDecl->getDescriptiveKind(),
                         inheritedOwningDecl->getDeclaredInterfaceType());
        }

        addInferredSameTypeReq(assocTypeDecl, inheritedType);
      }

      inheritedTypeDecls.erase(knownInherited);
      continue;
    }
  }

  // Check all remaining inherited type declarations to determine if
  // this protocol has a non-associated-type type with the same name.
  inheritedTypeDecls.remove_if(
    [&](const std::pair<DeclName, TinyPtrVector<TypeDecl *>> &inherited) {
      auto name = inherited.first;
      for (auto found : proto->lookupDirect(name)) {
        // We only want concrete type declarations.
        auto type = dyn_cast<TypeDecl>(found);
        if (!type || isa<AssociatedTypeDecl>(type)) continue;

        // ... from the same module as the protocol.
        if (type->getModuleContext() != proto->getModuleContext()) continue;

        // Or is constrained.
        if (auto ext = dyn_cast<ExtensionDecl>(type->getDeclContext())) {
          if (ext->isConstrainedExtension()) continue;
        }

        // We found something.
        bool shouldWarnAboutRedeclaration =
          source->kind == RequirementSource::RequirementSignatureSelf;

        for (auto inheritedType : inherited.second) {
          // If we have inherited associated type...
          if (auto inheritedAssocTypeDecl =
                dyn_cast<AssociatedTypeDecl>(inheritedType)) {
            // Infer a same-type requirement between the typealias' underlying
            // type and the inherited associated type.
            addInferredSameTypeReq(inheritedAssocTypeDecl, type);

            // Warn that one should use where clauses for this.
            if (shouldWarnAboutRedeclaration) {
              auto inheritedFromProto = inheritedAssocTypeDecl->getProtocol();
              auto fixItWhere = getProtocolWhereLoc();
              Diags.diagnose(type,
                             diag::typealias_override_associated_type,
                             name,
                             inheritedFromProto->getDeclaredInterfaceType())
                .fixItInsertAfter(fixItWhere.first,
                                  getConcreteTypeReq(type, fixItWhere.second))
                .fixItRemove(type->getSourceRange());
              Diags.diagnose(inheritedAssocTypeDecl, diag::decl_declared_here,
                             inheritedAssocTypeDecl->getFullName());

              shouldWarnAboutRedeclaration = false;
            }

            continue;
          }

          // Two typealiases that should be the same.
          addInferredSameTypeReq(inheritedType, type);
        }

        // We can remove this entry.
        return true;
      }

      return false;
  });

  // Infer same-type requirements among inherited type declarations.
  for (auto &entry : inheritedTypeDecls) {
    if (entry.second.size() < 2) continue;

    auto firstDecl = entry.second.front();
    for (auto otherDecl : ArrayRef<TypeDecl *>(entry.second).slice(1)) {
      addInferredSameTypeReq(firstDecl, otherDecl);
    }
  }

  return ConstraintResult::Resolved;
}

ConstraintResult GenericSignatureBuilder::addConformanceRequirement(
                               ResolvedType type,
                               ProtocolDecl *proto,
                               FloatingRequirementSource source) {
  // Add the conformance requirement, bailing out earlier if we've already
  // seen it.
  auto equivClass = type.getEquivalenceClass(*this);
  if (!equivClass->recordConformanceConstraint(*this, type, proto, source))
    return ConstraintResult::Resolved;

  auto resolvedSource = source.getSource(*this,
                                         type.getDependentType(*this));
  return expandConformanceRequirement(type, proto, resolvedSource,
                                      /*onlySameTypeRequirements=*/false);
}

ConstraintResult GenericSignatureBuilder::addLayoutRequirementDirect(
                                             ResolvedType type,
                                             LayoutConstraint layout,
                                             FloatingRequirementSource source) {
  auto equivClass = type.getEquivalenceClass(*this);

  // Update the layout in the equivalence class, if we didn't have one already.
  bool anyChanges = false;
  if (!equivClass->layout) {
    equivClass->layout = layout;
    anyChanges = true;
  } else {
    // Try to merge layout constraints.
    auto mergedLayout = equivClass->layout.merge(layout);
    if (mergedLayout->isKnownLayout() && mergedLayout != equivClass->layout) {
      equivClass->layout = mergedLayout;
      anyChanges = true;
    }
  }

  // Record this layout constraint.
  equivClass->layoutConstraints.push_back({type.getUnresolvedType(),
    layout, source.getSource(*this, type.getDependentType(*this))});
  equivClass->modified(*this);
  ++NumLayoutConstraints;
  if (!anyChanges) ++NumLayoutConstraintsExtra;

  return ConstraintResult::Resolved;
}

ConstraintResult GenericSignatureBuilder::addLayoutRequirement(
                                             UnresolvedType subject,
                                             LayoutConstraint layout,
                                             FloatingRequirementSource source,
                                             UnresolvedHandlingKind unresolvedHandling) {
  // Resolve the subject.
  auto resolvedSubject = resolve(subject, source);
  if (!resolvedSubject) {
    return handleUnresolvedRequirement(
                               RequirementKind::Layout, subject,
                               layout, source,
                               resolvedSubject.getUnresolvedEquivClass(),
                               unresolvedHandling);
  }

  // If this layout constraint applies to a concrete type, we can fully
  // resolve it now.
  if (auto concreteType = resolvedSubject.getAsConcreteType()) {
    // If a layout requirement was explicitly written on a concrete type,
    // complain.
    if (source.isExplicit() && source.getLoc().isValid()) {
      Impl->HadAnyError = true;

      Diags.diagnose(source.getLoc(), diag::requires_not_suitable_archetype,
                     TypeLoc::withoutLoc(concreteType));
      return ConstraintResult::Concrete;
    }

    // FIXME: Check whether the layout constraint makes sense for this
    // concrete type!

    return ConstraintResult::Resolved;
  }

  return addLayoutRequirementDirect(resolvedSubject, layout, source);
}

bool GenericSignatureBuilder::updateSuperclass(
                                           ResolvedType type,
                                           Type superclass,
                                           FloatingRequirementSource source) {
  auto equivClass = type.getEquivalenceClass(*this);

  // Local function to handle the update of superclass conformances
  // when the superclass constraint changes.
  auto updateSuperclassConformances = [&] {
    for (const auto &conforms : equivClass->conformsTo) {
      (void)resolveSuperConformance(type, conforms.first);
    }
  };

  // If we haven't yet recorded a superclass constraint for this equivalence
  // class, do so now.
  if (!equivClass->superclass) {
    equivClass->superclass = superclass;
    updateSuperclassConformances();

    // Presence of a superclass constraint implies a _Class layout
    // constraint.
    auto layoutReqSource =
      source.getSource(*this,
                       type.getDependentType(*this))->viaDerived(*this);
    addLayoutRequirementDirect(type,
                         LayoutConstraint::getLayoutConstraint(
                             superclass->getClassOrBoundGenericClass()->isObjC()
                                 ? LayoutConstraintKind::Class
                                 : LayoutConstraintKind::NativeClass,
                             getASTContext()),
                         layoutReqSource);
    return true;
  }

  // T already has a superclass; make sure it's related.
  auto existingSuperclass = equivClass->superclass;
  // TODO: In principle, this could be isBindableToSuperclassOf instead of
  // isExactSubclassOf. If you had:
  //
  //   class Foo<T>
  //   class Bar: Foo<Int>
  //
  //   func foo<T, U where U: Foo<T>, U: Bar>(...) { ... }
  //
  // then the second constraint should be allowed, constraining U to Bar
  // and secondarily imposing a T == Int constraint.
  if (existingSuperclass->isExactSuperclassOf(superclass)) {
    equivClass->superclass = superclass;

    // We've strengthened the bound, so update superclass conformances.
    updateSuperclassConformances();
    return true;
  }

  return false;
}

ConstraintResult GenericSignatureBuilder::addSuperclassRequirementDirect(
                                            ResolvedType type,
                                            Type superclass,
                                            FloatingRequirementSource source) {
  auto resolvedSource =
    source.getSource(*this, type.getDependentType(*this));

  // Record the constraint.
  auto equivClass = type.getEquivalenceClass(*this);
  equivClass->superclassConstraints.push_back(
    ConcreteConstraint{type.getUnresolvedType(), superclass, resolvedSource});
  equivClass->modified(*this);
  ++NumSuperclassConstraints;

  // Update the equivalence class with the constraint.
  if (!updateSuperclass(type, superclass, source))
    ++NumSuperclassConstraintsExtra;

  return ConstraintResult::Resolved;
}

/// Map an unresolved type to a requirement right-hand-side.
static GenericSignatureBuilder::UnresolvedRequirementRHS
toUnresolvedRequirementRHS(GenericSignatureBuilder::UnresolvedType unresolved) {
  if (auto pa = unresolved.dyn_cast<PotentialArchetype *>())
    return pa;

  return unresolved.dyn_cast<Type>();
}

ConstraintResult GenericSignatureBuilder::addTypeRequirement(
    UnresolvedType subject, UnresolvedType constraint,
    FloatingRequirementSource source, UnresolvedHandlingKind unresolvedHandling,
    ModuleDecl *inferForModule) {
  // Resolve the constraint.
  auto resolvedConstraint = resolve(constraint, source);
  if (!resolvedConstraint) {
    return handleUnresolvedRequirement(
                             RequirementKind::Conformance, subject,
                             toUnresolvedRequirementRHS(constraint), source,
                             resolvedConstraint.getUnresolvedEquivClass(),
                             unresolvedHandling);
  }

  // The right-hand side needs to be concrete.
  Type constraintType = resolvedConstraint.getAsConcreteType();
  if (!constraintType) {
    constraintType = resolvedConstraint.getDependentType(*this);
    assert(constraintType && "No type to express resolved constraint?");
  }

  // Check whether we have a reasonable constraint type at all.
  if (!constraintType->isExistentialType() &&
      !constraintType->getClassOrBoundGenericClass()) {
    if (source.getLoc().isValid() && !constraintType->hasError()) {
      auto subjectType = subject.dyn_cast<Type>();
      if (!subjectType)
        subjectType = subject.get<PotentialArchetype *>()
                        ->getDependentType(getGenericParams());

      Impl->HadAnyError = true;
      Diags.diagnose(source.getLoc(), diag::requires_conformance_nonprotocol,
                     TypeLoc::withoutLoc(subjectType),
                     TypeLoc::withoutLoc(constraintType));
    }

    return ConstraintResult::Conflicting;
  }

  // Resolve the subject. If we can't, delay the constraint.
  auto resolvedSubject = resolve(subject, source);
  if (!resolvedSubject) {
    auto recordedKind =
      constraintType->isExistentialType()
        ? RequirementKind::Conformance
        : RequirementKind::Superclass;
    return handleUnresolvedRequirement(
                               recordedKind, subject, constraintType,
                               source,
                               resolvedSubject.getUnresolvedEquivClass(),
                               unresolvedHandling);
  }

  // If the resolved subject is a type, there may be things we can infer (if it
  // conditionally conforms to the protocol), and we can probably perform
  // diagnostics here.
  if (auto subjectType = resolvedSubject.getAsConcreteType()) {
    if (constraintType->isExistentialType()) {
      auto layout = constraintType->getExistentialLayout();
      for (auto *proto : layout.getProtocols()) {
        // We have a pure concrete type, and there's no guarantee of dependent
        // type that makes sense to use here, but, in practice, all
        // getLookupConformanceFns used in here don't use that parameter anyway.
        auto dependentType = CanType();
        auto conformance =
            getLookupConformanceFn()(dependentType, subjectType, proto);

        // FIXME: diagnose if there's no conformance.
        if (conformance) {
          auto inferredSource = FloatingRequirementSource::forInferred(nullptr);
          for (auto req : conformance->getConditionalRequirements()) {
            addRequirement(req, inferredSource, inferForModule);
          }
        }
      }
    }

    // One cannot explicitly write a constraint on a concrete type.
    if (source.isExplicit()) {
      if (source.getLoc().isValid()) {
        Impl->HadAnyError = true;
        Diags.diagnose(source.getLoc(), diag::requires_not_suitable_archetype,
                       TypeLoc::withoutLoc(subjectType));
      }

      return ConstraintResult::Concrete;
    }

    return ConstraintResult::Resolved;
  }

  // Protocol requirements.
  if (constraintType->isExistentialType()) {
    bool anyErrors = false;
    auto layout = constraintType->getExistentialLayout();

    if (auto layoutConstraint = layout.getLayoutConstraint()) {
      if (isErrorResult(addLayoutRequirementDirect(resolvedSubject,
                                                   layoutConstraint,
                                                   source)))
        anyErrors = true;
    }

    if (layout.superclass) {
      if (isErrorResult(addSuperclassRequirementDirect(resolvedSubject,
                                                       layout.superclass,
                                                       source)))
        anyErrors = true;
    }

    for (auto *proto : layout.getProtocols()) {
      auto *protoDecl = proto->getDecl();
      if (isErrorResult(addConformanceRequirement(resolvedSubject, protoDecl,
                                                  source)))
        anyErrors = true;
    }

    return anyErrors ? ConstraintResult::Conflicting
                     : ConstraintResult::Resolved;
  }

  // Superclass constraint.
  return addSuperclassRequirementDirect(resolvedSubject, constraintType,
                                        source);
}

void GenericSignatureBuilder::addedNestedType(PotentialArchetype *nestedPA) {
  // If there was already another type with this name within the parent
  // potential archetype, equate this type with that one.
  auto parentPA = nestedPA->getParent();
  auto &allNested = parentPA->NestedTypes[nestedPA->getNestedName()];
  assert(!allNested.empty());
  assert(allNested.back() == nestedPA);
  if (allNested.size() > 1) {
    auto firstPA = allNested.front();
    auto inferredSource =
      FloatingRequirementSource::forInferred(nullptr);

    addSameTypeRequirement(firstPA, nestedPA, inferredSource,
                           UnresolvedHandlingKind::GenerateConstraints);
    return;
  }

  // If our parent type is not the representative, equate this nested
  // potential archetype to the equivalent nested type within the
  // representative.
  auto parentRepPA = parentPA->getRepresentative();
  if (parentPA == parentRepPA) return;

  PotentialArchetype *existingPA =
    parentRepPA->updateNestedTypeForConformance(
                                        *this,
                                        nestedPA->getResolvedType(),
                                        ArchetypeResolutionKind::WellFormed);

  auto sameNamedSource =
    FloatingRequirementSource::forNestedTypeNameMatch(
                                                nestedPA->getNestedName());
  addSameTypeRequirement(existingPA, nestedPA, sameNamedSource,
                         UnresolvedHandlingKind::GenerateConstraints);
}

ConstraintResult
GenericSignatureBuilder::addSameTypeRequirementBetweenArchetypes(
       PotentialArchetype *OrigT1,
       PotentialArchetype *OrigT2,
       const RequirementSource *Source) 
{
  // Record the same-type constraint, and bail out if it was already known.
  if (!OrigT1->getOrCreateEquivalenceClass(*this)
        ->recordSameTypeConstraint(OrigT1, OrigT2, Source))
    return ConstraintResult::Resolved;

  // Operate on the representatives
  auto T1 = OrigT1->getRepresentative();
  auto T2 = OrigT2->getRepresentative();

  // Decide which potential archetype is to be considered the representative.
  // We prefer potential archetypes with lower nesting depths, because it
  // prevents us from unnecessarily building deeply nested potential archetypes.
  unsigned nestingDepth1 = T1->getNestingDepth();
  unsigned nestingDepth2 = T2->getNestingDepth();
  if (nestingDepth2 < nestingDepth1) {
    std::swap(T1, T2);
    std::swap(OrigT1, OrigT2);
  }

  // Merge the equivalence classes.
  auto equivClass = T1->getOrCreateEquivalenceClass(*this);
  equivClass->modified(*this);

  auto equivClass1Members = equivClass->members;
  auto equivClass2Members = T2->getEquivalenceClassMembers();
  for (auto equiv : equivClass2Members)
    equivClass->addMember(equiv);

  // Grab the old equivalence class, if present. We'll deallocate it at the end.
  auto equivClass2 = T2->getEquivalenceClassIfPresent();
  SWIFT_DEFER {
    if (equivClass2)
      Impl->deallocateEquivalenceClass(equivClass2);
  };

  // Consider the second equivalence class to be modified.
  if (equivClass2)
    equivClass->modified(*this);

  // Same-type requirements.
  if (equivClass2) {
    equivClass->sameTypeConstraints.insert(
                                   equivClass->sameTypeConstraints.end(),
                                   equivClass2->sameTypeConstraints.begin(),
                                   equivClass2->sameTypeConstraints.end());
  }

  // Same-type-to-concrete requirements.
  bool t1IsConcrete = !equivClass->concreteType.isNull();
  bool t2IsConcrete = equivClass2 && !equivClass2->concreteType.isNull();
  if (t2IsConcrete) {
    if (t1IsConcrete) {
      (void)addSameTypeRequirement(equivClass->concreteType,
                                   equivClass2->concreteType, Source,
                                   UnresolvedHandlingKind::GenerateConstraints,
                                   SameTypeConflictCheckedLater());
    } else {
      equivClass->concreteType = equivClass2->concreteType;
      equivClass->invalidConcreteType = equivClass2->invalidConcreteType;
    }

    equivClass->concreteTypeConstraints.insert(
                                 equivClass->concreteTypeConstraints.end(),
                                 equivClass2->concreteTypeConstraints.begin(),
                                 equivClass2->concreteTypeConstraints.end());
  }

  // Make T1 the representative of T2, merging the equivalence classes.
  T2->representativeOrEquivClass = T1;

  // Superclass requirements.
  if (equivClass2 && equivClass2->superclass) {
    const RequirementSource *source2;
    if (auto existingSource2 =
          equivClass2->findAnySuperclassConstraintAsWritten(
            OrigT2->getDependentType(getGenericParams())))
      source2 = existingSource2->source;
    else
      source2 = equivClass2->superclassConstraints.front().source;

    // Add the superclass constraints from the second equivalence class.
    equivClass->superclassConstraints.insert(
                                   equivClass->superclassConstraints.end(),
                                   equivClass2->superclassConstraints.begin(),
                                   equivClass2->superclassConstraints.end());

    (void)updateSuperclass(T1, equivClass2->superclass, source2);
  }

  // Add all of the protocol conformance requirements of T2 to T1.
  if (equivClass2) {
    for (const auto &entry : equivClass2->conformsTo) {
      equivClass->recordConformanceConstraint(*this, T1, entry.first,
                                              entry.second.front().source);

      auto &constraints1 = equivClass->conformsTo[entry.first];
      // FIXME: Go through recordConformanceConstraint()?
      constraints1.insert(constraints1.end(),
                          entry.second.begin() + 1,
                          entry.second.end());
    }
  }

  // Recursively merge the associated types of T2 into T1.
  auto dependentT1 = T1->getDependentType(getGenericParams());
  for (auto equivT2 : equivClass2Members) {
    for (auto T2Nested : equivT2->NestedTypes) {
      // If T1 is concrete but T2 is not, concretize the nested types of T2.
      if (t1IsConcrete && !t2IsConcrete) {
        concretizeNestedTypeFromConcreteParent(T1, T2Nested.second.front(),
                                               *this);
        continue;
      }

      // Otherwise, make the nested types equivalent.
      AssociatedTypeDecl *assocTypeT2 = nullptr;
      for (auto T2 : T2Nested.second) {
        assocTypeT2 = T2->getResolvedAssociatedType();
        if (assocTypeT2) break;
      }

      if (!assocTypeT2) continue;

      Type nestedT1 = DependentMemberType::get(dependentT1, assocTypeT2);
      if (isErrorResult(
            addSameTypeRequirement(
               nestedT1, T2Nested.second.front(),
               FloatingRequirementSource::forNestedTypeNameMatch(
                                                      assocTypeT2->getName()),
               UnresolvedHandlingKind::GenerateConstraints)))
        return ConstraintResult::Conflicting;
    }
  }

  // If T2 is concrete but T1 was not, concretize the nested types of T1.
  if (t2IsConcrete && !t1IsConcrete) {
    for (auto equivT1 : equivClass1Members) {
      for (auto T1Nested : equivT1->NestedTypes) {
        concretizeNestedTypeFromConcreteParent(T2, T1Nested.second.front(),
                                               *this);
      }
    }
  }

  return ConstraintResult::Resolved;
}

ConstraintResult GenericSignatureBuilder::addSameTypeRequirementToConcrete(
                                           ResolvedType type,
                                           Type concrete,
                                           const RequirementSource *source) {
auto equivClass = type.getEquivalenceClass(*this);

  // Record the concrete type and its source.
  equivClass->concreteTypeConstraints.push_back(
    ConcreteConstraint{type.getUnresolvedType(), concrete, source});
  equivClass->modified(*this);
  ++NumConcreteTypeConstraints;

  // If we've already been bound to a type, match that type.
  if (equivClass->concreteType) {
    return addSameTypeRequirement(equivClass->concreteType, concrete, source,
                                  UnresolvedHandlingKind::GenerateConstraints,
                                  SameTypeConflictCheckedLater());

  }

  // Record the requirement.
  equivClass->concreteType = concrete;

  // Make sure the concrete type fulfills the conformance requirements of
  // this equivalence class.
  for (const auto &conforms : equivClass->conformsTo) {
    if (!resolveConcreteConformance(type, conforms.first))
      return ConstraintResult::Conflicting;
  }

  // Eagerly resolve any existing nested types to their concrete forms (others
  // will be "concretized" as they are constructed, in getNestedType).
  for (auto equivT : equivClass->members) {
    for (auto nested : equivT->getNestedTypes()) {
      concretizeNestedTypeFromConcreteParent(equivT, nested.second.front(),
                                             *this);
    }
  }

  return ConstraintResult::Resolved;
}

ConstraintResult GenericSignatureBuilder::addSameTypeRequirementBetweenConcrete(
    Type type1, Type type2, FloatingRequirementSource source,
    llvm::function_ref<void(Type, Type)> diagnoseMismatch) {
  // Local class to handle matching the two sides of the same-type constraint.
  class ReqTypeMatcher : public TypeMatcher<ReqTypeMatcher> {
    GenericSignatureBuilder &builder;
    FloatingRequirementSource source;
    Type outerType1, outerType2;
    llvm::function_ref<void(Type, Type)> diagnoseMismatch;

  public:
    ReqTypeMatcher(GenericSignatureBuilder &builder,
                   FloatingRequirementSource source,
                   Type outerType1, Type outerType2,
                   llvm::function_ref<void(Type, Type)> diagnoseMismatch)
        : builder(builder), source(source), outerType1(outerType1),
          outerType2(outerType2), diagnoseMismatch(diagnoseMismatch) {}

    bool mismatch(TypeBase *firstType, TypeBase *secondType,
                  Type sugaredFirstType) {
      // If the mismatch was in the first layer (i.e. what was fed to
      // addSameTypeRequirementBetweenConcrete), then this is a fundamental
      // mismatch, and we need to diagnose it. This is what breaks the mutual
      // recursion between addSameTypeRequirement and
      // addSameTypeRequirementBetweenConcrete.
      if (outerType1->isEqual(firstType) && outerType2->isEqual(secondType)) {
        diagnoseMismatch(sugaredFirstType, secondType);
        return false;
      }

      auto failed = builder.addSameTypeRequirement(
          sugaredFirstType, Type(secondType), source,
          UnresolvedHandlingKind::GenerateConstraints, diagnoseMismatch);
      return !isErrorResult(failed);
    }
  } matcher(*this, source, type1, type2, diagnoseMismatch);

  return matcher.match(type1, type2) ? ConstraintResult::Resolved
                                     : ConstraintResult::Conflicting;
}

ConstraintResult GenericSignatureBuilder::addSameTypeRequirement(
                                 UnresolvedType paOrT1,
                                 UnresolvedType paOrT2,
                                 FloatingRequirementSource source,
                                 UnresolvedHandlingKind unresolvedHandling) {
  return addSameTypeRequirement(paOrT1, paOrT2, source, unresolvedHandling,
                                [&](Type type1, Type type2) {
      Impl->HadAnyError = true;
      if (source.getLoc().isValid()) {
        Diags.diagnose(source.getLoc(), diag::requires_same_concrete_type,
                       type1, type2);
      }
    });
}

ConstraintResult GenericSignatureBuilder::addSameTypeRequirement(
    UnresolvedType paOrT1, UnresolvedType paOrT2,
    FloatingRequirementSource source,
    UnresolvedHandlingKind unresolvedHandling,
    llvm::function_ref<void(Type, Type)> diagnoseMismatch) {

  auto resolved1 = resolve(paOrT1, source);
  if (!resolved1) {
    return handleUnresolvedRequirement(RequirementKind::SameType, paOrT1,
                                       toUnresolvedRequirementRHS(paOrT2),
                                       source,
                                       resolved1.getUnresolvedEquivClass(),
                                       unresolvedHandling);
  }

  auto resolved2 = resolve(paOrT2, source);
  if (!resolved2) {
    return handleUnresolvedRequirement(RequirementKind::SameType, paOrT1,
                                       toUnresolvedRequirementRHS(paOrT2),
                                       source,
                                       resolved2.getUnresolvedEquivClass(),
                                       unresolvedHandling);
  }

  return addSameTypeRequirementDirect(resolved1, resolved2, source,
                                      diagnoseMismatch);
}

ConstraintResult GenericSignatureBuilder::addSameTypeRequirementDirect(
    ResolvedType type1, ResolvedType type2,
    FloatingRequirementSource source,
    llvm::function_ref<void(Type, Type)> diagnoseMismatch) {
  auto concreteType1 = type1.getAsConcreteType();
  auto concreteType2 = type2.getAsConcreteType();

  // If both sides of the requirement are concrete, equate them.
  if (concreteType1 && concreteType2) {
    return addSameTypeRequirementBetweenConcrete(concreteType1,
                                                 concreteType2, source,
                                                 diagnoseMismatch);
  }

  // If one side is concrete, map the other side to that concrete type.
  if (concreteType1) {
    return addSameTypeRequirementToConcrete(type2, concreteType1,
                       source.getSource(*this, type2.getDependentType(*this)));
  }

  if (concreteType2) {
    return addSameTypeRequirementToConcrete(type1, concreteType2,
                        source.getSource(*this, type1.getDependentType(*this)));
  }

  // Both sides are type parameters; equate them.
  // FIXME: Realizes potential archetypes far too early.
  auto pa1 = type1.realizePotentialArchetype(*this);
  auto pa2 = type2.realizePotentialArchetype(*this);

  return addSameTypeRequirementBetweenArchetypes(
                     pa1, pa2,
                     source.getSource(*this,
                                      type2.getDependentType(*this)));
}

ConstraintResult GenericSignatureBuilder::addInheritedRequirements(
                             TypeDecl *decl,
                             UnresolvedType type,
                             const RequirementSource *parentSource,
                             ModuleDecl *inferForModule) {
  if (isa<AssociatedTypeDecl>(decl) &&
      decl->hasInterfaceType() &&
      decl->getInterfaceType()->is<ErrorType>())
    return ConstraintResult::Resolved;

  // Walk the 'inherited' list to identify requirements.
  if (auto resolver = getLazyResolver())
    resolver->resolveInheritanceClause(decl);

  // Local function to get the source.
  auto getFloatingSource = [&](const TypeRepr *typeRepr, bool forInferred) {
    if (parentSource) {
      if (auto assocType = dyn_cast<AssociatedTypeDecl>(decl)) {
        auto proto = assocType->getProtocol();
        return FloatingRequirementSource::viaProtocolRequirement(
          parentSource, proto, typeRepr, forInferred);
      }

      auto proto = cast<ProtocolDecl>(decl);
      return FloatingRequirementSource::viaProtocolRequirement(
        parentSource, proto, typeRepr, forInferred);
    }

    // We are inferring requirements.
    if (forInferred) {
      return FloatingRequirementSource::forInferred(typeRepr);
    }

    // Explicit requirement.
    if (typeRepr)
      return FloatingRequirementSource::forExplicit(typeRepr);

    // An abstract explicit requirement.
    return FloatingRequirementSource::forAbstract();
  };

  auto visitType = [&](Type inheritedType, const TypeRepr *typeRepr) {
    if (inferForModule) {
      inferRequirements(*inferForModule,
                        TypeLoc(const_cast<TypeRepr *>(typeRepr),
                                inheritedType),
                        getFloatingSource(typeRepr, /*forInferred=*/true));
    }

    return addTypeRequirement(
        type, inheritedType, getFloatingSource(typeRepr,
                                               /*forInferred=*/false),
        UnresolvedHandlingKind::GenerateConstraints, inferForModule);
  };

  return visitInherited(decl->getInherited(), visitType);
}

ConstraintResult GenericSignatureBuilder::addRequirement(
                                                 const RequirementRepr *req,
                                                 ModuleDecl *inferForModule) {
  return addRequirement(req,
                        FloatingRequirementSource::forExplicit(req),
                        nullptr,
                        inferForModule);
}

ConstraintResult GenericSignatureBuilder::addRequirement(
                                             const RequirementRepr *Req,
                                             FloatingRequirementSource source,
                                             const SubstitutionMap *subMap,
                                             ModuleDecl *inferForModule) {
  auto subst = [&](Type t) {
    if (subMap)
      return t.subst(*subMap, SubstFlags::UseErrorType);

    return t;
  };

  auto getInferredTypeLoc = [=](Type type, TypeLoc existingTypeLoc) {
    if (subMap) return TypeLoc::withoutLoc(type);
    return existingTypeLoc;
  };

  switch (Req->getKind()) {
  case RequirementReprKind::LayoutConstraint: {
    auto subject = subst(Req->getSubject());
    if (inferForModule) {
      inferRequirements(*inferForModule,
                        getInferredTypeLoc(subject, Req->getSubjectLoc()),
                        source.asInferred(Req->getSubjectLoc().getTypeRepr()));
    }

    return addLayoutRequirement(subject,
                                Req->getLayoutConstraint(),
                                source,
                                UnresolvedHandlingKind::GenerateConstraints);
  }

  case RequirementReprKind::TypeConstraint: {
    auto subject = subst(Req->getSubject());
    auto constraint = subst(Req->getConstraint());
    if (inferForModule) {
      inferRequirements(*inferForModule,
                        getInferredTypeLoc(subject, Req->getSubjectLoc()),
                        source.asInferred(Req->getSubjectLoc().getTypeRepr()));
      inferRequirements(*inferForModule,
                        getInferredTypeLoc(constraint,
                                           Req->getConstraintLoc()),
                        source.asInferred(
                                      Req->getConstraintLoc().getTypeRepr()));
    }
    return addTypeRequirement(subject, constraint, source,
                              UnresolvedHandlingKind::GenerateConstraints,
                              inferForModule);
  }

  case RequirementReprKind::SameType: {
    // Warn if neither side of the requirement contains a type parameter.
    if (!Req->getFirstType()->hasTypeParameter() &&
        !Req->getSecondType()->hasTypeParameter() &&
        !Req->getFirstType()->hasError() &&
        !Req->getSecondType()->hasError()) {
      Diags.diagnose(Req->getEqualLoc(),
                     diag::requires_no_same_type_archetype,
                     Req->getFirstType(), Req->getSecondType())
        .highlight(Req->getFirstTypeLoc().getSourceRange())
        .highlight(Req->getSecondTypeLoc().getSourceRange());
    }

    auto firstType = subst(Req->getFirstType());
    auto secondType = subst(Req->getSecondType());
    if (inferForModule) {
      inferRequirements(*inferForModule,
                        getInferredTypeLoc(firstType, Req->getFirstTypeLoc()),
                        source.asInferred(
                                        Req->getFirstTypeLoc().getTypeRepr()));
      inferRequirements(*inferForModule,
                        getInferredTypeLoc(secondType,
                                           Req->getSecondTypeLoc()),
                        source.asInferred(
                                        Req->getSecondTypeLoc().getTypeRepr()));
    }
    return addRequirement(Requirement(RequirementKind::SameType,
                                      firstType, secondType),
                          source, nullptr);
  }
  }

  llvm_unreachable("Unhandled requirement?");
}

ConstraintResult
GenericSignatureBuilder::addRequirement(const Requirement &req,
                                        FloatingRequirementSource source,
                                        ModuleDecl *inferForModule) {
  auto firstType = req.getFirstType();

  switch (req.getKind()) {
  case RequirementKind::Superclass:
  case RequirementKind::Conformance: {
    auto secondType = req.getSecondType();

    if (inferForModule) {
      inferRequirements(*inferForModule, TypeLoc::withoutLoc(firstType),
                        FloatingRequirementSource::forInferred(
                            nullptr));
      inferRequirements(*inferForModule, TypeLoc::withoutLoc(secondType),
                        FloatingRequirementSource::forInferred(
                            nullptr));
    }

    return addTypeRequirement(firstType, secondType, source,
                              UnresolvedHandlingKind::GenerateConstraints,
                              inferForModule);
  }

  case RequirementKind::Layout: {
    if (inferForModule) {
      inferRequirements(*inferForModule, TypeLoc::withoutLoc(firstType),
                        FloatingRequirementSource::forInferred(nullptr));
    }

    return addLayoutRequirement(firstType, req.getLayoutConstraint(), source,
                                UnresolvedHandlingKind::GenerateConstraints);
  }

  case RequirementKind::SameType: {
    auto secondType = req.getSecondType();

    if (inferForModule) {
      inferRequirements(*inferForModule, TypeLoc::withoutLoc(firstType),
                        FloatingRequirementSource::forInferred(
                            nullptr));
      inferRequirements(*inferForModule, TypeLoc::withoutLoc(secondType),
                        FloatingRequirementSource::forInferred(
                            nullptr));
    }

    return addSameTypeRequirement(
        firstType, secondType, source,
        UnresolvedHandlingKind::GenerateConstraints,
        [&](Type type1, Type type2) {
          Impl->HadAnyError = true;
          if (source.getLoc().isValid()) {
            Diags.diagnose(source.getLoc(), diag::requires_same_concrete_type,
                           type1, type2);
          }
        });
  }
  }

  llvm_unreachable("Unhandled requirement?");
}

/// AST walker that infers requirements from type representations.
class GenericSignatureBuilder::InferRequirementsWalker : public TypeWalker {
  ModuleDecl &module;
  GenericSignatureBuilder &Builder;
  FloatingRequirementSource source;

public:
  InferRequirementsWalker(ModuleDecl &module,
                          GenericSignatureBuilder &builder,
                          FloatingRequirementSource source)
    : module(module), Builder(builder), source(source) { }

  Action walkToTypePost(Type ty) override {
    auto boundGeneric = ty->getAs<BoundGenericType>();
    if (!boundGeneric)
      return Action::Continue;

    auto *decl = boundGeneric->getDecl();
    auto genericSig = decl->getGenericSignature();
    if (!genericSig)
      return Action::Stop;

    /// Retrieve the substitution.
    auto subMap = boundGeneric->getContextSubstitutionMap(
      &module, decl, decl->getGenericEnvironment());

    // Handle the requirements.
    // FIXME: Inaccurate TypeReprs.
    for (const auto &rawReq : genericSig->getRequirements()) {
      if (auto req = rawReq.subst(subMap))
        Builder.addRequirement(*req, source, nullptr);
    }

    return Action::Continue;
  }
};

void GenericSignatureBuilder::inferRequirements(
                                          ModuleDecl &module,
                                          TypeLoc type,
                                          FloatingRequirementSource source) {
  if (!type.getType())
    return;
  // FIXME: Crummy source-location information.
  InferRequirementsWalker walker(module, *this, source);
  type.getType().walk(walker);
}

void GenericSignatureBuilder::inferRequirements(
                                          ModuleDecl &module,
                                          ParameterList *params,
                                          GenericParamList *genericParams) {
  if (genericParams == nullptr)
    return;

  for (auto P : *params) {
    inferRequirements(module, P->getTypeLoc(),
                      FloatingRequirementSource::forInferred(
                          P->getTypeLoc().getTypeRepr()));
  }
}

namespace swift {
  template<typename T>
  bool operator<(const Constraint<T> &lhs, const Constraint<T> &rhs) {
    // FIXME: Awful.
    auto lhsSource = lhs.getSubjectDependentType({ });
    auto rhsSource = rhs.getSubjectDependentType({ });
    if (int result = compareDependentTypes(lhsSource, rhsSource))
      return result < 0;

    if (int result = lhs.source->compare(rhs.source))
      return result < 0;

    return false;
  }

  template<typename T>
  bool operator==(const Constraint<T> &lhs, const Constraint<T> &rhs){
    return lhs.hasSameSubjectAs(rhs) &&
           lhs.value == rhs.value &&
           lhs.source == rhs.source;
  }

  template<>
  bool operator==(const Constraint<Type> &lhs, const Constraint<Type> &rhs){
    return lhs.hasSameSubjectAs(rhs) &&
           lhs.value->isEqual(rhs.value) &&
           lhs.source == rhs.source;
  }
} // namespace swift

namespace {
  /// Retrieve the representative constraint that will be used for diagnostics.
  template<typename T>
  Optional<Constraint<T>> findRepresentativeConstraint(
                            ArrayRef<Constraint<T>> constraints,
                            llvm::function_ref<bool(const Constraint<T> &)>
                                                   isSuitableRepresentative) {
    // Find a representative constraint.
    Optional<Constraint<T>> representativeConstraint;
    for (const auto &constraint : constraints) {
      // If this isn't a suitable representative constraint, ignore it.
      if (!isSuitableRepresentative(constraint))
        continue;

      // Check whether this constraint is better than the best we've seen so far
      // at being the representative constraint against which others will be
      // compared.
      if (!representativeConstraint) {
        representativeConstraint = constraint;
        continue;
      }

      // We prefer derived constraints to non-derived constraints.
      bool thisIsDerived = constraint.source->isDerivedRequirement();
      bool representativeIsDerived =
        representativeConstraint->source->isDerivedRequirement();
      if (thisIsDerived != representativeIsDerived) {
        if (thisIsDerived)
          representativeConstraint = constraint;

        continue;
      }

      // We prefer constraints that are explicit to inferred constraints.
      bool thisIsInferred = constraint.source->isInferredRequirement();
      bool representativeIsInferred =
        representativeConstraint->source->isInferredRequirement();
      if (thisIsInferred != representativeIsInferred) {
        if (thisIsInferred)
          representativeConstraint = constraint;

        continue;
      }

      // We prefer constraints with locations to constraints without locations.
      bool thisHasValidSourceLoc = constraint.source->getLoc().isValid();
      bool representativeHasValidSourceLoc =
        representativeConstraint->source->getLoc().isValid();
      if (thisHasValidSourceLoc != representativeHasValidSourceLoc) {
        if (thisHasValidSourceLoc)
          representativeConstraint = constraint;

        continue;
      }

      // Otherwise, order via the constraint itself.
      if (constraint < *representativeConstraint)
        representativeConstraint = constraint;
    }

    return representativeConstraint;
  }
} // end anonymous namespace

/// For each potential archetype within the given equivalence class that is
/// an associated type, expand the protocol requirements for the enclosing
/// protocol.
static void expandSameTypeConstraints(GenericSignatureBuilder &builder,
                                      EquivalenceClass *equivClass) {
  auto genericParams = builder.getGenericParams();
  auto existingMembers = equivClass->members;
  for (auto pa : existingMembers) {
    // Make sure that there are only associated types that chain up to the
    // parent.
    bool foundNonAssociatedType = false;
    for (auto currentPA = pa; auto parentPA = currentPA->getParent();
         currentPA = parentPA){
      if (!currentPA->getResolvedAssociatedType()) {
        foundNonAssociatedType = true;
        break;
      }
    }
    if (foundNonAssociatedType) continue;

    auto dependentType = pa->getDependentType(genericParams);
    for (const auto &conforms : equivClass->conformsTo) {
      auto proto = conforms.first;

      // Check whether we already have a conformance constraint for this
      // potential archetype.
      bool alreadyFound = false;
      const RequirementSource *conformsSource = nullptr;
      for (const auto &constraint : conforms.second) {
        if (constraint.source->getAffectedType()->isEqual(dependentType)) {
          alreadyFound = true;
          break;
        }

        // Capture the source for later use, skipping
        if (!conformsSource &&
            constraint.source->kind
              != RequirementSource::RequirementSignatureSelf)
          conformsSource = constraint.source;
      }

      if (alreadyFound) continue;
      if (!conformsSource) continue;

      // Pick a source at random and reseat it on this potential archetype.
      auto source = conformsSource->viaEquivalentType(builder, dependentType);

      // Expand same-type constraints.
      builder.expandConformanceRequirement(pa, proto, source,
                                           /*onlySameTypeConstraints=*/true);
    }
  }
}

void
GenericSignatureBuilder::finalize(SourceLoc loc,
                           ArrayRef<GenericTypeParamType *> genericParams,
                           bool allowConcreteGenericParams) {
  // Process any delayed requirements that we can handle now.
  processDelayedRequirements();

  assert(!Impl->finalized && "Already finalized builder");
#ifndef NDEBUG
  Impl->finalized = true;
#endif

  // Local function (+ cache) describing the set of equivalence classes
  // directly referenced by the concrete same-type constraint of the given
  // equivalence class.
  llvm::DenseMap<EquivalenceClass *,
                 SmallPtrSet<EquivalenceClass *, 4>> concreteEquivClasses;
  auto getConcreteReferencedEquivClasses
      = [&](EquivalenceClass *equivClass)
          -> SmallPtrSet<EquivalenceClass *, 4> {
    auto known = concreteEquivClasses.find(equivClass);
    if (known != concreteEquivClasses.end())
      return known->second;

    SmallPtrSet<EquivalenceClass *, 4> referencedEquivClasses;
    if (!equivClass->concreteType ||
        !equivClass->concreteType->hasTypeParameter())
      return referencedEquivClasses;

    equivClass->concreteType.visit([&](Type type) {
      if (type->isTypeParameter()) {
        if (auto referencedEquivClass =
              resolveEquivalenceClass(type,
                                      ArchetypeResolutionKind::AlreadyKnown)) {
          referencedEquivClasses.insert(referencedEquivClass);
        }
      }
    });

    concreteEquivClasses[equivClass] = referencedEquivClasses;
    return referencedEquivClasses;
  };

  /// Check whether the given type references the archetype.
  auto isRecursiveConcreteType = [&](EquivalenceClass *equivClass,
                                     bool isSuperclass) {
    SmallPtrSet<EquivalenceClass *, 4> visited;
    SmallVector<EquivalenceClass *, 4> stack;
    stack.push_back(equivClass);
    visited.insert(equivClass);

    // Check whether the specific type introduces recursion.
    auto checkTypeRecursion = [&](Type type) {
      if (!type->hasTypeParameter()) return false;

      return type.findIf([&](Type type) {
        if (type->isTypeParameter()) {
          if (auto referencedEquivClass =
                resolveEquivalenceClass(
                                    type,
                                    ArchetypeResolutionKind::AlreadyKnown)) {
            if (referencedEquivClass == equivClass) return true;

            if (visited.insert(referencedEquivClass).second)
              stack.push_back(referencedEquivClass);
          }
        }

        return false;
      });
    };

    while (!stack.empty()) {
      auto currentEquivClass = stack.back();
      stack.pop_back();

      // If we're checking superclasses, do so now.
      if (isSuperclass && currentEquivClass->superclass &&
          checkTypeRecursion(currentEquivClass->superclass)) {
        return true;
      }

      // Otherwise, look for the equivalence classes referenced by
      // same-type constraints.
      for (auto referencedEquivClass :
             getConcreteReferencedEquivClasses(currentEquivClass)) {
        // If we found a reference to the original archetype, it's recursive.
        if (referencedEquivClass == equivClass) return true;

        if (visited.insert(referencedEquivClass).second)
          stack.push_back(referencedEquivClass);
      }
    }

    return false;
  };

  // Check for recursive or conflicting same-type bindings and superclass
  // constraints.
  for (auto &equivClass : Impl->EquivalenceClasses) {
    if (equivClass.concreteType) {
      // Check for recursive same-type bindings.
      if (isRecursiveConcreteType(&equivClass, /*isSuperclass=*/false)) {
        if (auto constraint =
              equivClass.findAnyConcreteConstraintAsWritten()) {
          Impl->HadAnyError = true;

          Diags.diagnose(constraint->source->getLoc(),
                         diag::recursive_same_type_constraint,
                         constraint->getSubjectDependentType(genericParams),
                         constraint->value);
        }

        equivClass.recursiveConcreteType = true;
      } else {
        checkConcreteTypeConstraints(genericParams, &equivClass);
      }
    }

    // Check for recursive superclass bindings.
    if (equivClass.superclass) {
      if (isRecursiveConcreteType(&equivClass, /*isSuperclass=*/true)) {
        if (auto source = equivClass.findAnySuperclassConstraintAsWritten()) {
          Impl->HadAnyError = true;

          Diags.diagnose(source->source->getLoc(),
                         diag::recursive_superclass_constraint,
                         source->getSubjectDependentType(genericParams),
                         equivClass.superclass);
        }

        equivClass.recursiveSuperclassType = true;
      } else {
        checkSuperclassConstraints(genericParams, &equivClass);
      }
    }

    checkConformanceConstraints(genericParams, &equivClass);
    checkLayoutConstraints(genericParams, &equivClass);
  };

  // FIXME: Expand all conformance requirements. This is expensive :(
  for (auto &equivClass : Impl->EquivalenceClasses) {
      expandSameTypeConstraints(*this, &equivClass);
  }

  // Check same-type constraints.
  for (auto &equivClass : Impl->EquivalenceClasses) {
    checkSameTypeConstraints(genericParams, &equivClass);
  }

  // Check for generic parameters which have been made concrete or equated
  // with each other.
  if (!allowConcreteGenericParams) {
    SmallPtrSet<PotentialArchetype *, 4> visited;
    
    unsigned depth = 0;
    for (const auto &gp : Impl->GenericParams)
      depth = std::max(depth, gp->getDepth());

    for (const auto pa : Impl->PotentialArchetypes) {
      auto rep = pa->getRepresentative();

      if (pa->getRootGenericParamKey().Depth < depth)
        continue;

      if (!visited.insert(rep).second)
        continue;

      // Don't allow a generic parameter to be equivalent to a concrete type,
      // because then we don't actually have a parameter.
      auto equivClass = rep->getOrCreateEquivalenceClass(*this);
      if (equivClass->concreteType) {
        if (auto constraint = equivClass->findAnyConcreteConstraintAsWritten()){
          Impl->HadAnyError = true;

          Diags.diagnose(constraint->source->getLoc(),
                         diag::requires_generic_param_made_equal_to_concrete,
                         rep->getDependentType(genericParams));
        }
        continue;
      }

      // Don't allow two generic parameters to be equivalent, because then we
      // don't actually have two parameters.
      for (auto other : rep->getEquivalenceClassMembers()) {
        // If it isn't a generic parameter, skip it.
        if (other == pa || !other->isGenericParam()) continue;

        // Try to find an exact constraint that matches 'other'.
        auto repConstraint =
          findRepresentativeConstraint<PotentialArchetype *>(
            equivClass->sameTypeConstraints,
            [pa, other](const Constraint<PotentialArchetype *> &constraint) {
              return (constraint.isSubjectEqualTo(pa) &&
                      constraint.value == other) ||
                (constraint.isSubjectEqualTo(other) &&
                 constraint.value == pa);
            });


         // Otherwise, just take any old constraint.
        if (!repConstraint) {
          repConstraint =
            findRepresentativeConstraint<PotentialArchetype *>(
              equivClass->sameTypeConstraints,
              [](const Constraint<PotentialArchetype *> &constraint) {
                return true;
              });
        }

        if (repConstraint && repConstraint->source->getLoc().isValid()) {
          Impl->HadAnyError = true;

          Diags.diagnose(repConstraint->source->getLoc(),
                         diag::requires_generic_params_made_equal,
                         pa->getDependentType(genericParams),
                         other->getDependentType(genericParams));
        }
        break;
      }
    }
  }
}

/// Turn a requirement right-hand side into an unresolved type.
static GenericSignatureBuilder::UnresolvedType asUnresolvedType(
                        GenericSignatureBuilder::UnresolvedRequirementRHS rhs) {
  if (auto pa = rhs.dyn_cast<PotentialArchetype *>())
    return GenericSignatureBuilder::UnresolvedType(pa);

  return GenericSignatureBuilder::UnresolvedType(rhs.get<Type>());
}

void GenericSignatureBuilder::processDelayedRequirements() {
  // If we're already up-to-date, do nothing.
  if (Impl->Generation == Impl->LastProcessedGeneration) { return; }

  // If there are no delayed requirements, do nothing.
  if (Impl->DelayedRequirements.empty()) { return; }

  if (Impl->ProcessingDelayedRequirements) { return; }

  ++NumProcessDelayedRequirements;

  llvm::SaveAndRestore<bool> processing(Impl->ProcessingDelayedRequirements,
                                        true);
  bool anyChanges = false;
  SWIFT_DEFER {
    Impl->LastProcessedGeneration = Impl->Generation;
    if (!anyChanges)
      ++NumProcessDelayedRequirementsUnchanged;
  };

  bool anySolved;
  do {
    // Steal the delayed requirements so we can reprocess them.
    anySolved = false;
    auto delayed = std::move(Impl->DelayedRequirements);
    Impl->DelayedRequirements.clear();

    // Process delayed requirements.
    for (const auto &req : delayed) {
      // Reprocess the delayed requirement.
      ConstraintResult reqResult;
      switch (req.kind) {
      case DelayedRequirement::Type:
        reqResult =
            addTypeRequirement(req.lhs, asUnresolvedType(req.rhs), req.source,
                               UnresolvedHandlingKind::GenerateUnresolved,
                               /*inferForModule=*/nullptr);
        break;

      case DelayedRequirement::Layout:
        reqResult = addLayoutRequirement(
                           req.lhs, req.rhs.get<LayoutConstraint>(), req.source,
                           UnresolvedHandlingKind::GenerateUnresolved);
        break;

      case DelayedRequirement::SameType:
        reqResult = addSameTypeRequirement(
                               req.lhs, asUnresolvedType(req.rhs), req.source,
                               UnresolvedHandlingKind::GenerateUnresolved);
        break;
      }

      // Update our state based on what happened.
      switch (reqResult) {
      case ConstraintResult::Concrete:
        ++NumDelayedRequirementConcrete;
        anySolved = true;
        break;

      case ConstraintResult::Conflicting:
        anySolved = true;
        break;

      case ConstraintResult::Resolved:
        ++NumDelayedRequirementResolved;
        anySolved = true;
        break;

      case ConstraintResult::Unresolved:
        // Add the requirement back.
        ++NumDelayedRequirementUnresolved;
        break;
      }
    }

    if (anySolved) {
      anyChanges = true;
    }
  } while (anySolved);
}

template<typename T>
Constraint<T> GenericSignatureBuilder::checkConstraintList(
                           ArrayRef<GenericTypeParamType *> genericParams,
                           std::vector<Constraint<T>> &constraints,
                           llvm::function_ref<bool(const Constraint<T> &)>
                             isSuitableRepresentative,
                           llvm::function_ref<
                             ConstraintRelation(const Constraint<T>&)>
                               checkConstraint,
                           Optional<Diag<unsigned, Type, T, T>>
                             conflictingDiag,
                           Diag<Type, T> redundancyDiag,
                           Diag<unsigned, Type, T> otherNoteDiag) {
  return checkConstraintList<T, T>(genericParams, constraints,
                                   isSuitableRepresentative, checkConstraint,
                                   conflictingDiag, redundancyDiag,
                                   otherNoteDiag,
                                   [](const T& value) { return value; },
                                   /*removeSelfDerived=*/true);
}

namespace {
  /// Remove self-derived sources from the given vector of constraints.
  ///
  /// \returns true if any derived-via-concrete constraints were found.
  template<typename T>
  bool removeSelfDerived(GenericSignatureBuilder &builder,
                         std::vector<Constraint<T>> &constraints,
                         ProtocolDecl *proto,
                         bool dropDerivedViaConcrete = true,
                         bool allCanBeSelfDerived = false) {
    auto genericParams = builder.getGenericParams();
    bool anyDerivedViaConcrete = false;
    Optional<Constraint<T>> remainingConcrete;
    SmallVector<Constraint<T>, 4> minimalSources;
    constraints.erase(
      std::remove_if(constraints.begin(), constraints.end(),
        [&](const Constraint<T> &constraint) {
          bool derivedViaConcrete;
          auto minimalSource =
            constraint.source->getMinimalConformanceSource(
                         builder,
                         constraint.getSubjectDependentType(genericParams),
                         proto, derivedViaConcrete);
          if (minimalSource != constraint.source) {
            // The minimal source is smaller than the original source, so the
            // original source is self-derived.
            ++NumSelfDerived;

            // FIXME: "proto" check means we don't do this for same-type
            // constraints, where we still seem to get minimization wrong.
            if (minimalSource && proto) {
              // Record a constraint with a minimized source.
              minimalSources.push_back(
                           {constraint.subject,
                             constraint.value,
                             minimalSource});
            }

            return true;
          }

           if (!derivedViaConcrete)
             return false;

           anyDerivedViaConcrete = true;

           if (!dropDerivedViaConcrete)
             return false;

           // Drop derived-via-concrete requirements.
           if (!remainingConcrete)
             remainingConcrete = constraint;

           ++NumSelfDerived;
           return true;
         }),
      constraints.end());

    // If we found any minimal sources, add them now, avoiding introducing any
    // redundant sources.
    if (!minimalSources.empty()) {
      // Collect the sources we already know about.
      SmallPtrSet<const RequirementSource *, 4> sources;
      for (const auto &constraint : constraints) {
        sources.insert(constraint.source);
      }

      // Add any minimal sources we didn't know about.
      for (const auto &minimalSource : minimalSources) {
        if (sources.insert(minimalSource.source).second) {
          constraints.push_back(minimalSource);
        }
      }
    }

    // If we only had concrete conformances, put one back.
    if (constraints.empty() && remainingConcrete)
      constraints.push_back(*remainingConcrete);

    assert((!constraints.empty() || allCanBeSelfDerived) &&
           "All constraints were self-derived!");
    return anyDerivedViaConcrete;
  }
} // end anonymous namespace

template<typename T, typename DiagT>
Constraint<T> GenericSignatureBuilder::checkConstraintList(
                           ArrayRef<GenericTypeParamType *> genericParams,
                           std::vector<Constraint<T>> &constraints,
                           llvm::function_ref<bool(const Constraint<T> &)>
                             isSuitableRepresentative,
                           llvm::function_ref<
                             ConstraintRelation(const Constraint<T>&)>
                               checkConstraint,
                           Optional<Diag<unsigned, Type, DiagT, DiagT>>
                             conflictingDiag,
                           Diag<Type, DiagT> redundancyDiag,
                           Diag<unsigned, Type, DiagT> otherNoteDiag,
                           llvm::function_ref<DiagT(const T&)> diagValue,
                           bool removeSelfDerived) {
  assert(!constraints.empty() && "No constraints?");
  if (removeSelfDerived) {
    ::removeSelfDerived(*this, constraints, /*proto=*/nullptr);
  }

  // Sort the constraints, so we get a deterministic ordering of diagnostics.
  llvm::array_pod_sort(constraints.begin(), constraints.end());

  // Find a representative constraint.
  auto representativeConstraint =
    findRepresentativeConstraint<T>(constraints, isSuitableRepresentative);

  // Local function to provide a note describing the representative constraint.
  auto noteRepresentativeConstraint = [&] {
    if (representativeConstraint->source->getLoc().isInvalid()) return;

    Diags.diagnose(representativeConstraint->source->getLoc(),
                   otherNoteDiag,
                   representativeConstraint->source->classifyDiagKind(),
                   representativeConstraint->getSubjectDependentType(
                                                               genericParams),
                   diagValue(representativeConstraint->value));
  };

  // Go through the concrete constraints looking for redundancies.
  bool diagnosedConflictingRepresentative = false;
  for (const auto &constraint : constraints) {
    // Leave the representative alone.
    if (representativeConstraint && constraint == *representativeConstraint)
      continue;

    switch (checkConstraint(constraint)) {
    case ConstraintRelation::Unrelated:
      continue;

    case ConstraintRelation::Conflicting: {
      // Figure out what kind of subject we have; it will affect the
      // diagnostic.
      auto getSubjectType =
        [&](Type subjectType) -> std::pair<unsigned, Type> {
          unsigned kind;
          if (auto gp = subjectType->getAs<GenericTypeParamType>()) {
            if (gp->getDecl() &&
                isa<ProtocolDecl>(gp->getDecl()->getDeclContext())) {
              kind = 1;
              subjectType = cast<ProtocolDecl>(gp->getDecl()->getDeclContext())
                              ->getDeclaredInterfaceType();
            } else {
              kind = 0;
            }
          } else {
            kind = 2;
          }

          return std::make_pair(kind, subjectType);
        };


      // The requirement conflicts. If this constraint has a location, complain
      // about it.
      if (constraint.source->getLoc().isValid()) {
        Impl->HadAnyError = true;

        auto subject =
          getSubjectType(constraint.getSubjectDependentType(genericParams));
        Diags.diagnose(constraint.source->getLoc(), *conflictingDiag,
                       subject.first, subject.second,
                       diagValue(constraint.value),
                       diagValue(representativeConstraint->value));

        noteRepresentativeConstraint();
        break;
      }

      // If the representative itself conflicts and we haven't diagnosed it yet,
      // do so now.
      if (!diagnosedConflictingRepresentative &&
          representativeConstraint->source->getLoc().isValid()) {
        Impl->HadAnyError = true;

        auto subject =
          getSubjectType(
            representativeConstraint->getSubjectDependentType(genericParams));
        Diags.diagnose(representativeConstraint->source->getLoc(),
                       *conflictingDiag,
                       subject.first, subject.second,
                       diagValue(representativeConstraint->value),
                       diagValue(constraint.value));

        diagnosedConflictingRepresentative = true;
        break;
      }
      break;
    }

    case ConstraintRelation::Redundant:
      // If this requirement is not derived or inferred (but has a useful
      // location) complain that it is redundant.
      Impl->HadAnyRedundantConstraints = true;
      if (constraint.source->shouldDiagnoseRedundancy(true) &&
          representativeConstraint &&
          representativeConstraint->source->shouldDiagnoseRedundancy(false)) {
        Diags.diagnose(constraint.source->getLoc(),
                       redundancyDiag,
                       constraint.getSubjectDependentType(genericParams),
                       diagValue(constraint.value));

        noteRepresentativeConstraint();
      }
      break;
    }
  }

  return *representativeConstraint;
}

/// Determine whether this is a redundantly inheritable Objective-C protocol.
///
/// If we do have a redundantly inheritable Objective-C protocol, record that
/// the conformance was restated on the protocol whose requirement signature
/// we are computing.
///
/// At present, there is only one such protocol that we know about:
/// JavaScriptCore's JSExport.
static bool isRedundantlyInheritableObjCProtocol(
                                             ProtocolDecl *proto,
                                             const RequirementSource *source) {
  if (!proto->isObjC()) return false;

  // Only JSExport protocol behaves this way.
  if (!proto->getName().is("JSExport")) return false;

  // Only do this for the requirement signature computation.
  auto parentSource = source->parent;
  if (!parentSource ||
      parentSource->kind != RequirementSource::RequirementSignatureSelf)
    return false;

  // If the inheriting protocol already has @_restatedObjCConformance with
  // this protocol, we're done.
  auto inheritingProto = parentSource->getProtocolDecl();
  for (auto *attr : inheritingProto->getAttrs()
                      .getAttributes<RestatedObjCConformanceAttr>()) {
    if (attr->Proto == proto) return true;
  }

  // Otherwise, add @_restatedObjCConformance.
  auto &ctx = proto->getASTContext();
  inheritingProto->getAttrs().add(new (ctx) RestatedObjCConformanceAttr(proto));
  return true;
}

void GenericSignatureBuilder::checkConformanceConstraints(
                          ArrayRef<GenericTypeParamType *> genericParams,
                          EquivalenceClass *equivClass) {
  for (auto &entry : equivClass->conformsTo) {
    // Remove self-derived constraints.
    assert(!entry.second.empty() && "No constraints to work with?");

    // Remove any self-derived constraints.
    removeSelfDerived(*this, entry.second, entry.first);

    checkConstraintList<ProtocolDecl *, ProtocolDecl *>(
      genericParams, entry.second,
      [](const Constraint<ProtocolDecl *> &constraint) {
        return true;
      },
      [&](const Constraint<ProtocolDecl *> &constraint) {
        auto proto = constraint.value;
        assert(proto == entry.first && "Mixed up protocol constraints");

        // If this conformance requirement recursively makes a protocol
        // conform to itself, don't complain here.
        auto source = constraint.source;
        auto rootSource = source->getRoot();
        if (rootSource->kind == RequirementSource::RequirementSignatureSelf &&
            source != rootSource &&
            proto == rootSource->getProtocolDecl() &&
            areInSameEquivalenceClass(rootSource->getRootType(),
                                      source->getAffectedType())) {
          return ConstraintRelation::Unrelated;
        }

        // If this is a redundantly inherited Objective-C protocol, treat it
        // as "unrelated" to silence the warning about the redundant
        // conformance.
        if (isRedundantlyInheritableObjCProtocol(proto, constraint.source))
          return ConstraintRelation::Unrelated;

        return ConstraintRelation::Redundant;
      },
      None,
      diag::redundant_conformance_constraint,
      diag::redundant_conformance_here,
      [](ProtocolDecl *proto) { return proto; },
      /*removeSelfDerived=*/false);
  }
}

/// Compare dependent types for use in anchors.
///
/// FIXME: This is a hack that will go away once we eliminate potential
/// archetypes that refer to concrete type declarations.
static int compareAnchorDependentTypes(Type type1, Type type2) {
  // We don't want any unresolved dependent member types to be anchors, so
  // prefer that they don't get through.
  bool hasUnresolvedDependentMember1 =
    type1->findUnresolvedDependentMemberType() != nullptr;
  bool hasUnresolvedDependentMember2 =
    type2->findUnresolvedDependentMemberType() != nullptr;
  if (hasUnresolvedDependentMember1 != hasUnresolvedDependentMember2)
    return hasUnresolvedDependentMember2 ? -1 : +1;

  return compareDependentTypes(type1, type2);
}

namespace swift {
  bool operator<(const DerivedSameTypeComponent &lhs,
                 const DerivedSameTypeComponent &rhs) {
    return compareAnchorDependentTypes(getUnresolvedType(lhs.anchor, { }),
                                       getUnresolvedType(rhs.anchor, { })) < 0;
  }
} // namespace swift

/// Find the representative in a simple union-find data structure of
/// integral values.
static unsigned findRepresentative(SmallVectorImpl<unsigned> &parents,
                                   unsigned index) {
  if (parents[index] == index) return index;

  return parents[index] = findRepresentative(parents, parents[index]);
}

/// Union the same-type components denoted by \c index1 and \c index2.
///
/// \param successThreshold Returns true when two sets have been joined
/// and both representatives are below the threshold. The default of 0
/// is equivalent to \c successThreshold == parents.size().
///
/// \returns \c true if the two components were separate and have now
/// been joined; \c false if they were already in the same set.
static bool unionSets(SmallVectorImpl<unsigned> &parents,
                      unsigned index1, unsigned index2,
                      unsigned successThreshold = 0) {
  // Find the representatives of each component class.
  unsigned rep1 = findRepresentative(parents, index1);
  unsigned rep2 = findRepresentative(parents, index2);
  if (rep1 == rep2) return false;

  // Point at the lowest-numbered representative.
  if (rep1 < rep2)
    parents[rep2] = rep1;
  else
    parents[rep1] = rep2;

  return (successThreshold == 0) ||
    (rep1 < successThreshold && rep2 < successThreshold);
}

/// Computes the ordered set of archetype anchors required to form a minimum
/// spanning tree among the connected components formed by only the derived
/// same-type requirements within the equivalence class \c equivClass.
///
/// The equivalence class contains all potential archetypes that are made
/// equivalent by the known set of same-type constraints, which includes both
/// directly-stated same-type constraints (e.g., \c T.A == T.B) as well as
/// same-type constraints that are implied either because the names coincide
/// (e.g., \c T[.P1].A == T[.P2].A) or due to a requirement in a protocol.
///
/// The equivalence class of the given representative potential archetype
/// (\c rep) is formed from a graph whose vertices are the potential archetypes
/// and whose edges are the same-type constraints. These edges include both
/// directly-stated same-type constraints (e.g., \c T.A == T.B) as well as
/// same-type constraints that are implied either because the names coincide
/// (e.g., \c T[.P1].A == T[.P2].A) or due to a requirement in a protocol.
/// The equivalence class forms a single connected component.
///
/// Within that graph is a subgraph that includes only those edges that are
/// implied (and, therefore, excluding those edges that were explicitly stated).
/// The connected components within that subgraph describe the potential
/// archetypes that would be equivalence even with all of the (explicit)
/// same-type constraints removed.
///
/// The entire equivalence class can be restored by introducing edges between
/// the connected components. This function computes a minimal, canonicalized
/// set of edges (same-type constraints) needed to describe the equivalence
/// class, which is suitable for the generation of the canonical generic
/// signature.
///
/// The resulting set of "edges" is returned as a set of vertices, one per
/// connected component (of the subgraph). Each is the anchor for that
/// connected component (as determined by \c compareDependentTypes()), and the
/// set itself is ordered by \c compareDependentTypes(). The actual set of
/// canonical edges connects vertex i to vertex i+1 for i in 0..<size-1.
static void computeDerivedSameTypeComponents(
              GenericSignatureBuilder &builder,
              EquivalenceClass *equivClass,
              llvm::SmallDenseMap<CanType, unsigned> &componentOf){
  // Set up the array of "parents" in the union-find data structure.
  llvm::SmallDenseMap<CanType, unsigned> parentIndices;
  SmallVector<unsigned, 4> parents;
  for (unsigned i : indices(equivClass->members)) {
    Type depType = equivClass->members[i]->getDependentType({ });
    parentIndices[depType->getCanonicalType()] = parents.size();
    parents.push_back(i);
  }

  // Walk all of the same-type constraints, performing a union-find operation.
  for (const auto &constraint : equivClass->sameTypeConstraints) {
    // Treat nested-type-name-match constraints specially.
    if (constraint.source->getRoot()->kind ==
          RequirementSource::NestedTypeNameMatch)
      continue;

    // Skip non-derived constraints.
    if (!constraint.source->isDerivedRequirement()) continue;

    CanType source =
      constraint.getSubjectDependentType({ })->getCanonicalType();
    CanType target =
      constraint.value->getDependentType({ })->getCanonicalType();

    assert(parentIndices.count(source) == 1 && "Missing source");
    assert(parentIndices.count(target) == 1 && "Missing target");
    unionSets(parents, parentIndices[source], parentIndices[target]);
  }

  // Compute and record the components.
  auto &components = equivClass->derivedSameTypeComponents;
  for (unsigned i : indices(equivClass->members)) {
    auto pa = equivClass->members[i];
    CanType depType = pa->getDependentType({ })->getCanonicalType();

    // Find the representative of this set.
    assert(parentIndices.count(depType) == 1 && "Unknown member?");
    unsigned index = parentIndices[depType];
    unsigned representative = findRepresentative(parents, index);

    // If this is the representative, add a component for it.
    if (representative == index) {
      componentOf[depType] = components.size();
      components.push_back(DerivedSameTypeComponent{pa, nullptr});
      continue;
    }

    // This is not the representative; point at the component of the
    // representative.
    CanType representativeDepTy =
      equivClass->members[representative]->getDependentType({ })
        ->getCanonicalType();
    assert(componentOf.count(representativeDepTy) == 1 &&
           "Missing representative component?");
    unsigned componentIndex = componentOf[representativeDepTy];
    componentOf[depType] = componentIndex;

    // If this is a better anchor, record it.
    if (compareAnchorDependentTypes(
                depType,
                getUnresolvedType(components[componentIndex].anchor, { })) < 0)
      components[componentIndex].anchor = pa;
  }

  // If there is a concrete type, figure out the best concrete type anchor
  // per component.
auto genericParams = builder.getGenericParams();
  for (const auto &concrete : equivClass->concreteTypeConstraints) {
    // Dig out the component associated with constraint.
    Type subjectType = concrete.getSubjectDependentType(genericParams);
    assert(componentOf.count(subjectType->getCanonicalType()) > 0);
    auto &component = components[componentOf[subjectType->getCanonicalType()]];

    // FIXME: Skip self-derived sources. This means our attempts to "stage"
    // construction of self-derived sources really don't work, because we
    // discover more information later, so we need a more on-line or
    // iterative approach.
    bool derivedViaConcrete;
    if (concrete.source->isSelfDerivedSource(builder, subjectType,
                                             derivedViaConcrete))
      continue;

    // If it has a better source than we'd seen before for this component,
    // keep it.
    auto &bestConcreteTypeSource = component.concreteTypeSource;
    if (!bestConcreteTypeSource ||
        concrete.source->compare(bestConcreteTypeSource) < 0)
      bestConcreteTypeSource = concrete.source;
  }
}

namespace {
  /// An edge in the same-type constraint graph that spans two different
  /// components.
  struct IntercomponentEdge {
    unsigned source;
    unsigned target;
    Constraint<PotentialArchetype *> constraint;
    bool isSelfDerived = false;

    IntercomponentEdge(unsigned source, unsigned target,
                       const Constraint<PotentialArchetype *> &constraint)
      : source(source), target(target), constraint(constraint)
    {
      assert(source != target && "Not an intercomponent edge");
      if (this->source > this->target) std::swap(this->source, this->target);
    }

    friend bool operator<(const IntercomponentEdge &lhs,
                          const IntercomponentEdge &rhs) {
      if (lhs.source != rhs.source)
        return lhs.source < rhs.source;
      if (lhs.target != rhs.target)
        return lhs.target < rhs.target;

      // Prefer non-inferred requirement sources.
      bool lhsIsInferred = lhs.constraint.source->isInferredRequirement();
      bool rhsIsInferred = rhs.constraint.source->isInferredRequirement();
      if (lhsIsInferred != rhsIsInferred)
        return rhsIsInferred;;

      return lhs.constraint < rhs.constraint;
    }

    LLVM_ATTRIBUTE_DEPRECATED(void dump() const,
                              "only for use in the debugger");
  };
}

void IntercomponentEdge::dump() const {
  llvm::errs() << constraint.getSubjectDependentType({ }).getString() << " -- "
    << constraint.value->getDebugName() << ": ";
  constraint.source->print(llvm::errs(), nullptr);
  llvm::errs() << "\n";
}

/// Determine whether the removal of the given edge will disconnect the
/// nodes \c from and \c to within the given equivalence class.
static bool removalDisconnectsEquivalenceClass(
               EquivalenceClass *equivClass,
               llvm::SmallDenseMap<CanType, unsigned> &componentOf,
               std::vector<IntercomponentEdge> &sameTypeEdges,
               unsigned edgeIndex,
               CanType fromDepType,
               CanType toDepType) {
  // Which component are "from" and "to" in within the intercomponent edges?
  assert(componentOf.count(fromDepType) > 0);
  auto fromComponentIndex = componentOf[fromDepType];

  assert(componentOf.count(toDepType) > 0);
  auto toComponentIndex = componentOf[toDepType];

  // If they're in the same component, they're always connected (due to
  // derived edges).
  if (fromComponentIndex == toComponentIndex) return false;

  /// Describes the parents in the equivalance classes we're forming.
  SmallVector<unsigned, 4> parents;
  for (unsigned i : range(equivClass->derivedSameTypeComponents.size())) {
    parents.push_back(i);
  }

  for (const auto existingEdgeIndex : indices(sameTypeEdges)) {
    if (existingEdgeIndex == edgeIndex) continue;

    const auto &edge = sameTypeEdges[existingEdgeIndex];
    if (edge.isSelfDerived) continue;

    if (unionSets(parents, edge.source, edge.target) &&
        findRepresentative(parents, fromComponentIndex) ==
          findRepresentative(parents, toComponentIndex))
      return false;
  }

  const auto &edge = sameTypeEdges[edgeIndex];

  return !unionSets(parents, edge.source, edge.target) ||
    findRepresentative(parents, fromComponentIndex) !=
      findRepresentative(parents, toComponentIndex);
}

static bool isSelfDerivedNestedTypeNameMatchEdge(
              GenericSignatureBuilder &builder,
              EquivalenceClass *equivClass,
              llvm::SmallDenseMap<CanType, unsigned> &componentOf,
              std::vector<IntercomponentEdge> &sameTypeEdges,
              unsigned edgeIndex) {
  const auto &edge = sameTypeEdges[edgeIndex];
  auto genericParams = builder.getGenericParams();
  Type sourceType = edge.constraint.getSubjectDependentType(genericParams);
  PotentialArchetype *target = edge.constraint.value;

  DependentMemberType *sourceDepMemTy;
  while ((sourceDepMemTy = sourceType->getAs<DependentMemberType>()) &&
         target->getParent() &&
         sourceDepMemTy->getAssocType() &&
         sourceDepMemTy->getAssocType() == target->getResolvedAssociatedType()){
    sourceType = sourceDepMemTy->getBase();
    target = target->getParent();

    if (target->getEquivalenceClassIfPresent() == equivClass &&
        builder.maybeResolveEquivalenceClass(
                                     sourceType,
                                     ArchetypeResolutionKind::WellFormed,
                                     /*wantExactPotentialArchetype=*/false)
          .getEquivalenceClass(builder) == equivClass &&
        !removalDisconnectsEquivalenceClass(equivClass, componentOf,
                                            sameTypeEdges, edgeIndex,
                                            sourceType->getCanonicalType(),
                                            target->getDependentType({ })
                                              ->getCanonicalType()))
      return true;
  }

  return false;
}

/// Collapse same-type components using the "delayed" requirements of the
/// equivalence class.
///
/// This operation looks through the delayed requirements within the equivalence
/// class to find paths that connect existing potential archetypes.
static void collapseSameTypeComponentsThroughDelayedRequirements(
              GenericSignatureBuilder &builder,
              EquivalenceClass *equivClass,
              llvm::SmallDenseMap<CanType, unsigned> &componentOf,
              SmallVectorImpl<unsigned> &collapsedParents,
              unsigned &remainingComponents) {
  unsigned numCollapsedParents = collapsedParents.size();

  /// "Virtual" components for types that aren't resolve to potential
  /// archetypes.
  llvm::SmallDenseMap<CanType, unsigned> virtualComponents;

  /// Retrieve the component for a type representing a virtual component
  auto getTypeVirtualComponent = [&](Type type) {
    CanType canType = type->getCanonicalType();
    auto knownActual = componentOf.find(canType);
    if (knownActual != componentOf.end())
      return knownActual->second;

    auto knownVirtual = virtualComponents.find(canType);
    if (knownVirtual != virtualComponents.end())
      return knownVirtual->second;

    unsigned component = collapsedParents.size();
    collapsedParents.push_back(component);
    virtualComponents[canType] = component;
    return component;
  };

  /// Retrieve the component for the given potential archetype.
  auto genericParams = builder.getGenericParams();
  auto getPotentialArchetypeVirtualComponent = [&](PotentialArchetype *pa) {
    if (pa->getEquivalenceClassIfPresent() == equivClass)
      return getTypeVirtualComponent(pa->getDependentType(genericParams));

    // We found a potential archetype in another equivalence class. Treat it
    // as a "virtual" component representing that potential archetype's
    // equivalence class.
    return getTypeVirtualComponent(
             pa->getRepresentative()->getDependentType(genericParams));
  };

  for (const auto &delayedReq : equivClass->delayedRequirements) {
    // Only consider same-type requirements.
    if (delayedReq.kind != DelayedRequirement::SameType) continue;

    unsigned lhsComponent;
    if (auto lhsPA = delayedReq.lhs.dyn_cast<PotentialArchetype *>())
      lhsComponent = getPotentialArchetypeVirtualComponent(lhsPA);
    else
      lhsComponent = getTypeVirtualComponent(delayedReq.lhs.get<Type>());

    unsigned rhsComponent;
    if (auto rhsPA = delayedReq.rhs.dyn_cast<PotentialArchetype *>())
      rhsComponent = getPotentialArchetypeVirtualComponent(rhsPA);
    else
      rhsComponent = getTypeVirtualComponent(delayedReq.rhs.get<Type>());

    // Collapse the sets
    if (unionSets(collapsedParents, lhsComponent, rhsComponent,
                  numCollapsedParents) &&
        lhsComponent < numCollapsedParents &&
        rhsComponent < numCollapsedParents)
      --remainingComponents;
  }

  /// Remove any additional collapsed parents we added.
  collapsedParents.erase(collapsedParents.begin() + numCollapsedParents,
                         collapsedParents.end());
}

/// Collapse same-type components within an equivalence class, minimizing the
/// number of requirements required to express the equivalence class.
static void collapseSameTypeComponents(
              GenericSignatureBuilder &builder,
              EquivalenceClass *equivClass,
              llvm::SmallDenseMap<CanType, unsigned> &componentOf,
              std::vector<IntercomponentEdge> &sameTypeEdges) {
  SmallVector<unsigned, 4> collapsedParents;
  for (unsigned i : indices(equivClass->derivedSameTypeComponents)) {
    collapsedParents.push_back(i);
  }

  unsigned remainingComponents = equivClass->derivedSameTypeComponents.size();
  for (unsigned edgeIndex : indices(sameTypeEdges)) {
    auto &edge = sameTypeEdges[edgeIndex];

    // If this edge is self-derived, remove it.
    if (isSelfDerivedNestedTypeNameMatchEdge(builder, equivClass, componentOf,
                                             sameTypeEdges, edgeIndex)) {
      // Note that this edge is self-derived, so we don't consider it again.
      edge.isSelfDerived = true;

      auto &constraints = equivClass->sameTypeConstraints;
      auto known =
        std::find_if(constraints.begin(), constraints.end(),
                     [&](const Constraint<PotentialArchetype *> &existing) {
                       // Check the requirement source, first.
                       if (existing.source != edge.constraint.source)
                         return false;

                       return
                         (existing.hasSameSubjectAs(edge.constraint) &&
                          existing.value == edge.constraint.value) ||
                         (existing.isSubjectEqualTo(edge.constraint.value) &&
                          edge.constraint.isSubjectEqualTo(existing.value));
                     });
      assert(known != constraints.end());
      constraints.erase(known);
      continue;
    }

    // Otherwise, collapse the derived same-type components along this edge,
    // because it's derived.
    if (unionSets(collapsedParents, edge.source, edge.target))
      --remainingComponents;
  }

  if (remainingComponents > 1) {
    // Collapse same-type components by looking at the delayed requirements.
    collapseSameTypeComponentsThroughDelayedRequirements(
      builder, equivClass, componentOf, collapsedParents, remainingComponents);
  }

  // If needed, collapse the same-type components merged by a derived
  // nested-type-name-match edge.
  unsigned maxComponents = equivClass->derivedSameTypeComponents.size();
  if (remainingComponents < maxComponents) {
    std::vector<DerivedSameTypeComponent> newComponents;
    std::vector<unsigned> newIndices(maxComponents, maxComponents);

    for (unsigned oldIndex : range(0, maxComponents)) {
      auto &oldComponent = equivClass->derivedSameTypeComponents[oldIndex];
      unsigned oldRepresentativeIndex =
        findRepresentative(collapsedParents, oldIndex);

      // If this is the representative, it's a new component; record it.
      if (oldRepresentativeIndex == oldIndex) {
        assert(newIndices[oldIndex] == maxComponents &&
               "Already saw this component?");
        unsigned newIndex = newComponents.size();
        newIndices[oldIndex] = newIndex;
        newComponents.push_back(
          {oldComponent.anchor, oldComponent.concreteTypeSource});
        continue;
      }

      // This is not the representative; merge it into the representative
      // component.
      auto newRepresentativeIndex = newIndices[oldRepresentativeIndex];
      assert(newRepresentativeIndex != maxComponents &&
             "Representative should have come earlier");
      auto &newComponent = newComponents[newRepresentativeIndex];

      // If the old component has a better anchor, keep it.
      if (compareAnchorDependentTypes(
                            getUnresolvedType(oldComponent.anchor, { }),
                            getUnresolvedType(newComponent.anchor, { })) < 0)
        newComponent.anchor = oldComponent.anchor;

      // If the old component has a better concrete type source, keep it.
      if (!newComponent.concreteTypeSource ||
          (oldComponent.concreteTypeSource &&
           oldComponent.concreteTypeSource
             ->compare(newComponent.concreteTypeSource) < 0))
        newComponent.concreteTypeSource = oldComponent.concreteTypeSource;
    }

    // Move the new results into place.
    equivClass->derivedSameTypeComponents = std::move(newComponents);
  }

  // Sort the components.
  llvm::array_pod_sort(equivClass->derivedSameTypeComponents.begin(),
                       equivClass->derivedSameTypeComponents.end());
}

void GenericSignatureBuilder::checkSameTypeConstraints(
                          ArrayRef<GenericTypeParamType *> genericParams,
                          EquivalenceClass *equivClass) {
  if (!equivClass->derivedSameTypeComponents.empty())
    return;

  bool anyDerivedViaConcrete = false;
  // Remove self-derived constraints.
  if (removeSelfDerived(*this, equivClass->sameTypeConstraints,
                        /*proto=*/nullptr,
                        /*dropDerivedViaConcrete=*/false,
                        /*allCanBeSelfDerived=*/true))
    anyDerivedViaConcrete = true;

  // Sort the constraints, so we get a deterministic ordering of diagnostics.
  llvm::array_pod_sort(equivClass->sameTypeConstraints.begin(),
                       equivClass->sameTypeConstraints.end());

  // Compute the components in the subgraph of the same-type constraint graph
  // that includes only derived constraints.
  llvm::SmallDenseMap<CanType, unsigned> componentOf;
  computeDerivedSameTypeComponents(*this, equivClass, componentOf);

  // Go through all of the same-type constraints, collecting all of the
  // non-derived constraints to put them into bins: intra-component and
  // inter-component.

  // Intra-component edges are stored per-component, so we can perform
  // diagnostics within each component.
  unsigned numComponents = equivClass->derivedSameTypeComponents.size();
  std::vector<std::vector<Constraint<PotentialArchetype *>>>
    intracomponentEdges(numComponents,
                        std::vector<Constraint<PotentialArchetype *>>());

  // Intercomponent edges are stored as one big list, which tracks the
  // source/target components.
  std::vector<IntercomponentEdge> intercomponentEdges;
  std::vector<IntercomponentEdge> nestedTypeNameMatchEdges;
  for (const auto &constraint : equivClass->sameTypeConstraints) {
    // If the source/destination are identical, complain.
    if (constraint.isSubjectEqualTo(constraint.value)) {
      if (constraint.source->shouldDiagnoseRedundancy(true)) {
        Diags.diagnose(constraint.source->getLoc(),
                       diag::redundant_same_type_constraint,
                       constraint.getSubjectDependentType(genericParams),
                       constraint.value->getDependentType(genericParams));
      }

      continue;
    }

    // Determine which component each of the source/destination fall into.
    CanType subjectType =
      constraint.getSubjectDependentType({ })->getCanonicalType();
    assert(componentOf.count(subjectType) > 0 &&
           "unknown potential archetype?");
    unsigned firstComponentIdx = componentOf[subjectType];
    assert(componentOf.count(
             constraint.value->getDependentType({ })->getCanonicalType()) > 0 &&
           "unknown potential archetype?");
    unsigned secondComponentIdx =
      componentOf[constraint.value->getDependentType({ })->getCanonicalType()];

    // Separately track nested-type-name-match constraints.
    if (constraint.source->getRoot()->kind ==
          RequirementSource::NestedTypeNameMatch) {
      // If this is an intercomponent edge, record it separately.
      if (firstComponentIdx != secondComponentIdx) {
        nestedTypeNameMatchEdges.push_back(
          IntercomponentEdge(firstComponentIdx, secondComponentIdx, constraint));
      }

      continue;
    }

    // If both vertices are within the same component, this is an
    // intra-component edge. Record it as such.
    if (firstComponentIdx == secondComponentIdx) {
      intracomponentEdges[firstComponentIdx].push_back(constraint);
      continue;
    }

    // Otherwise, it's an intercomponent edge, which is never derived.
    assert(!constraint.source->isDerivedRequirement() &&
           "Must not be derived");

    // Ignore inferred requirements; we don't want to diagnose them.
    intercomponentEdges.push_back(
      IntercomponentEdge(firstComponentIdx, secondComponentIdx, constraint));
  }

  // If there were any derived-via-concrete constraints, drop them now before
  // we emit other diagnostics.
  if (anyDerivedViaConcrete) {
    // Remove derived-via-concrete constraints.
    (void)removeSelfDerived(*this, equivClass->sameTypeConstraints,
                            /*proto=*/nullptr,
                            /*dropDerivedViaConcrete=*/true,
                            /*allCanBeSelfDerived=*/true);
  }

  // Walk through each of the components, checking the intracomponent edges.
  // This will diagnose any explicitly-specified requirements within a
  // component, all of which are redundant.
  for (auto &constraints : intracomponentEdges) {
    if (constraints.empty()) continue;

    checkConstraintList<PotentialArchetype *, Type>(
      genericParams, constraints,
      [](const Constraint<PotentialArchetype *> &) { return true; },
      [](const Constraint<PotentialArchetype *> &constraint) {
        // Ignore nested-type-name-match constraints.
        if (constraint.source->getRoot()->kind ==
              RequirementSource::NestedTypeNameMatch)
          return ConstraintRelation::Unrelated;

        return ConstraintRelation::Redundant;
      },
      None,
      diag::redundant_same_type_constraint,
      diag::previous_same_type_constraint,
      [&](PotentialArchetype *pa) {
        return pa->getDependentType(genericParams);
      },
      /*removeSelfDerived=*/false);
  }

  // Diagnose redundant same-type constraints across components. First,
  // sort the edges so that edges that between the same component pairs
  // occur next to each other.
  llvm::array_pod_sort(intercomponentEdges.begin(), intercomponentEdges.end());

  // Diagnose and erase any redundant edges between the same two components.
  intercomponentEdges.erase(
    std::unique(
      intercomponentEdges.begin(), intercomponentEdges.end(),
      [&](const IntercomponentEdge &lhs,
          const IntercomponentEdge &rhs) {
        // If either the source or target is different, we have
        // different elements.
        if (lhs.source != rhs.source || lhs.target != rhs.target)
          return false;

        // Check whethe we should diagnose redundancy for both constraints.
        if (!lhs.constraint.source->shouldDiagnoseRedundancy(true) ||
            !rhs.constraint.source->shouldDiagnoseRedundancy(false))
          return true;

        Diags.diagnose(lhs.constraint.source->getLoc(),
                       diag::redundant_same_type_constraint,
                       lhs.constraint.getSubjectDependentType(genericParams),
                       lhs.constraint.value->getDependentType(genericParams));
        Diags.diagnose(rhs.constraint.source->getLoc(),
                       diag::previous_same_type_constraint,
                       rhs.constraint.source->classifyDiagKind(),
                       rhs.constraint.getSubjectDependentType(genericParams),
                       rhs.constraint.value->getDependentType(genericParams));
        return true;
      }),
    intercomponentEdges.end());

  // If we have more intercomponent edges than are needed to form a spanning
  // tree, complain about redundancies. Note that the edges we have must
  // connect all of the components, or else we wouldn't have an equivalence
  // class.
  if (intercomponentEdges.size() > numComponents - 1) {
    std::vector<bool> connected(numComponents, false);
    const auto &firstEdge = intercomponentEdges.front();
    for (const auto &edge : intercomponentEdges) {
      // If both the source and target are already connected, this edge is
      // not part of the spanning tree.
      if (connected[edge.source] && connected[edge.target]) {
        if (edge.constraint.source->shouldDiagnoseRedundancy(true) &&
            firstEdge.constraint.source->shouldDiagnoseRedundancy(false)) {
          Diags.diagnose(edge.constraint.source->getLoc(),
                         diag::redundant_same_type_constraint,
                         edge.constraint.getSubjectDependentType(
                                                          genericParams),
                         edge.constraint.value->getDependentType(
                                                          genericParams));

          Diags.diagnose(firstEdge.constraint.source->getLoc(),
                         diag::previous_same_type_constraint,
                         firstEdge.constraint.source->classifyDiagKind(),
                         firstEdge.constraint.getSubjectDependentType(
                                                          genericParams),
                         firstEdge.constraint.value->getDependentType(
                                                          genericParams));
        }

        continue;
      }

      // Put the source and target into the spanning tree.
      connected[edge.source] = true;
      connected[edge.target] = true;
    }
  }

  collapseSameTypeComponents(*this, equivClass, componentOf,
                             nestedTypeNameMatchEdges);
}

void GenericSignatureBuilder::checkConcreteTypeConstraints(
                                 ArrayRef<GenericTypeParamType *> genericParams,
                                 EquivalenceClass *equivClass) {
  checkConstraintList<Type>(
    genericParams, equivClass->concreteTypeConstraints,
    [&](const ConcreteConstraint &constraint) {
      return constraint.value->isEqual(equivClass->concreteType);
    },
    [&](const Constraint<Type> &constraint) {
      Type concreteType = constraint.value;

      // If the concrete type is equivalent, the constraint is redundant.
      // FIXME: Should check this constraint after substituting in the
      // archetype anchors for each dependent type.
      if (concreteType->isEqual(equivClass->concreteType))
        return ConstraintRelation::Redundant;

      // If either has a type parameter, call them unrelated.
      if (concreteType->hasTypeParameter() ||
          equivClass->concreteType->hasTypeParameter())
        return ConstraintRelation::Unrelated;

      return ConstraintRelation::Conflicting;
    },
    diag::same_type_conflict,
    diag::redundant_same_type_to_concrete,
    diag::same_type_redundancy_here);

  // Resolve any thus-far-unresolved dependent types.
  equivClass->concreteType =
    resolveDependentMemberTypes(*this, equivClass->concreteType);
}

void GenericSignatureBuilder::checkSuperclassConstraints(
                                 ArrayRef<GenericTypeParamType *> genericParams,
                                 EquivalenceClass *equivClass) {
  assert(equivClass->superclass && "No superclass constraint?");

  // FIXME: We should be substituting in the canonical type in context so
  // we can resolve superclass requirements, e.g., if you had:
  //
  //   class Foo<T>
  //   class Bar: Foo<Int>
  //
  //   func foo<T, U where U: Bar, U: Foo<T>>(...) { ... }
  //
  // then the second `U: Foo<T>` constraint introduces a `T == Int`
  // constraint, and we will need to perform that substitution for this final
  // check.

  auto representativeConstraint =
    checkConstraintList<Type>(
      genericParams, equivClass->superclassConstraints,
      [&](const ConcreteConstraint &constraint) {
        return constraint.value->isEqual(equivClass->superclass);
      },
      [&](const Constraint<Type> &constraint) {
        Type superclass = constraint.value;

        // If this class is a superclass of the "best"
        if (superclass->isExactSuperclassOf(equivClass->superclass))
          return ConstraintRelation::Redundant;

        // Otherwise, it conflicts.
        return ConstraintRelation::Conflicting;
      },
      diag::requires_superclass_conflict,
      diag::redundant_superclass_constraint,
      diag::superclass_redundancy_here);

  // Resolve any this-far-unresolved dependent types.
  equivClass->superclass =
    resolveDependentMemberTypes(*this, equivClass->superclass);

  // If we have a concrete type, check it.
  // FIXME: Substitute into the concrete type.
  if (equivClass->concreteType) {
    auto existing = equivClass->findAnyConcreteConstraintAsWritten();
    // Make sure the concrete type fulfills the superclass requirement.
    if (!equivClass->superclass->isExactSuperclassOf(equivClass->concreteType)){
      Impl->HadAnyError = true;
      if (existing) {
        Diags.diagnose(existing->source->getLoc(), diag::type_does_not_inherit,
                       existing->getSubjectDependentType(getGenericParams()),
                       existing->value, equivClass->superclass);

        if (representativeConstraint.source->getLoc().isValid()) {
          Diags.diagnose(representativeConstraint.source->getLoc(),
                         diag::superclass_redundancy_here,
                         representativeConstraint.source->classifyDiagKind(),
                         representativeConstraint.getSubjectDependentType(
                                                              genericParams),
                         equivClass->superclass);
        }
      } else if (representativeConstraint.source->getLoc().isValid()) {
        Diags.diagnose(representativeConstraint.source->getLoc(),
                       diag::type_does_not_inherit,
                       representativeConstraint.getSubjectDependentType(
                                                              genericParams),
                       equivClass->concreteType, equivClass->superclass);
      }
    } else if (representativeConstraint.source->shouldDiagnoseRedundancy(true)
               && existing &&
               existing->source->shouldDiagnoseRedundancy(false)) {
      // It does fulfill the requirement; diagnose the redundancy.
      Diags.diagnose(representativeConstraint.source->getLoc(),
                     diag::redundant_superclass_constraint,
                     representativeConstraint.getSubjectDependentType(
                                                              genericParams),
                     representativeConstraint.value);

      Diags.diagnose(existing->source->getLoc(),
                     diag::same_type_redundancy_here,
                     existing->source->classifyDiagKind(),
                     existing->getSubjectDependentType(genericParams),
                     existing->value);
    }
  }
}

void GenericSignatureBuilder::checkLayoutConstraints(
                                ArrayRef<GenericTypeParamType *> genericParams,
                                EquivalenceClass *equivClass) {
  if (!equivClass->layout) return;

  checkConstraintList<LayoutConstraint>(
    genericParams, equivClass->layoutConstraints,
    [&](const Constraint<LayoutConstraint> &constraint) {
      return constraint.value == equivClass->layout;
    },
    [&](const Constraint<LayoutConstraint> &constraint) {
      auto layout = constraint.value;

      // If the layout constraints are mergable, i.e. compatible,
      // it is a redundancy.
      if (layout.merge(equivClass->layout)->isKnownLayout())
        return ConstraintRelation::Redundant;

      return ConstraintRelation::Conflicting;
    },
    diag::conflicting_layout_constraints,
    diag::redundant_layout_constraint,
    diag::previous_layout_constraint);
}

namespace {
  /// Retrieve the best requirement source from a set of constraints.
  template<typename T>
  Optional<const RequirementSource *>
  getBestConstraintSource(ArrayRef<Constraint<T>> constraints,
                          llvm::function_ref<bool(const T&)> matches) {
    Optional<const RequirementSource *> bestSource;
    for (const auto &constraint : constraints) {
      if (!matches(constraint.value)) continue;

      if (!bestSource || constraint.source->compare(*bestSource) < 0)
        bestSource = constraint.source;
    }

    return bestSource;
  }

  using SameTypeComponentRef = std::pair<EquivalenceClass *, unsigned>;

} // end anonymous namespace

static int compareSameTypeComponents(const SameTypeComponentRef *lhsPtr,
                                     const SameTypeComponentRef *rhsPtr){
  Type lhsType = getUnresolvedType(
      lhsPtr->first->derivedSameTypeComponents[lhsPtr->second].anchor,
      { });
  Type rhsType = getUnresolvedType(
      rhsPtr->first->derivedSameTypeComponents[rhsPtr->second].anchor,
      { });

  return compareAnchorDependentTypes(lhsType, rhsType);
}

void GenericSignatureBuilder::enumerateRequirements(
                   ArrayRef<GenericTypeParamType *> genericParams,
                   llvm::function_ref<
                     void (RequirementKind kind,
                           Type type,
                           RequirementRHS constraint,
                           const RequirementSource *source)> f) {
  // Collect all of the subject types that will be involved in constraints.
  SmallVector<SameTypeComponentRef, 8> subjects;
  for (auto &equivClass : Impl->EquivalenceClasses) {
    if (equivClass.derivedSameTypeComponents.empty()) {
      checkSameTypeConstraints(getGenericParams(), &equivClass);
    }

    for (unsigned i : indices(equivClass.derivedSameTypeComponents))
      subjects.push_back({&equivClass, i});
  }

  // Sort the subject types in canonical order.
  llvm::array_pod_sort(subjects.begin(), subjects.end(),
                       compareSameTypeComponents);

  for (const auto &subject : subjects) {
    // Dig out the subject type and its corresponding component.
    auto equivClass = subject.first;
    auto &component = equivClass->derivedSameTypeComponents[subject.second];
    Type subjectType = getUnresolvedType(component.anchor, genericParams);

    // If this equivalence class is bound to a concrete type, equate the
    // anchor with a concrete type.
    if (Type concreteType = equivClass->concreteType) {
      // If the parent of this anchor is also a concrete type, don't
      // create a requirement.
      if (!subjectType->is<GenericTypeParamType>() &&
          maybeResolveEquivalenceClass(
            subjectType->castTo<DependentMemberType>()->getBase(),
            ArchetypeResolutionKind::WellFormed,
            /*wantExactPotentialArchetype=*/false)
            .getEquivalenceClass(*this)->concreteType)
        continue;

      auto source =
        component.concreteTypeSource
          ? component.concreteTypeSource
          : RequirementSource::forAbstract(*this, subjectType);

      // Drop recursive and invalid concrete-type constraints.
      if (equivClass->recursiveConcreteType ||
          equivClass->invalidConcreteType)
        continue;

      f(RequirementKind::SameType, subjectType, concreteType, source);
      continue;
    }

    std::function<void()> deferredSameTypeRequirement;

    // If we're at the last anchor in the component, do nothing;
    if (subject.second + 1 != equivClass->derivedSameTypeComponents.size()) {
      // Form a same-type constraint from this anchor within the component
      // to the next.
      // FIXME: Distinguish between explicit and inferred here?
      auto &nextComponent =
        equivClass->derivedSameTypeComponents[subject.second + 1];
      Type otherSubjectType =
        getUnresolvedType(nextComponent.anchor, genericParams);
      deferredSameTypeRequirement =
        [&f, subjectType, otherSubjectType, this] {
          f(RequirementKind::SameType, subjectType, otherSubjectType,
            RequirementSource::forAbstract(*this, otherSubjectType));
        };
    }

    SWIFT_DEFER {
      if (deferredSameTypeRequirement) deferredSameTypeRequirement();
    };

    // If this is not the first component anchor in its equivalence class,
    // we're done.
    if (subject.second > 0)
      continue;

    // If we have a superclass, produce a superclass requirement
    if (equivClass->superclass && !equivClass->recursiveSuperclassType) {
      auto bestSource =
        getBestConstraintSource<Type>(equivClass->superclassConstraints,
           [&](const Type &type) {
             return type->isEqual(equivClass->superclass);
          });

      if (!bestSource)
        bestSource = RequirementSource::forAbstract(*this, subjectType);

      f(RequirementKind::Superclass, subjectType, equivClass->superclass,
        *bestSource);
    }

    // If we have a layout constraint, produce a layout requirement.
    if (equivClass->layout) {
      auto bestSource = getBestConstraintSource<LayoutConstraint>(
                          equivClass->layoutConstraints,
                          [&](const LayoutConstraint &layout) {
                            return layout == equivClass->layout;
                          });
      if (!bestSource)
        bestSource = RequirementSource::forAbstract(*this, subjectType);

      f(RequirementKind::Layout, subjectType, equivClass->layout, *bestSource);
    }

    // Enumerate conformance requirements.
    SmallVector<ProtocolDecl *, 4> protocols;
    DenseMap<ProtocolDecl *, const RequirementSource *> protocolSources;
    if (equivClass) {
      for (const auto &conforms : equivClass->conformsTo) {
        protocols.push_back(conforms.first);
        assert(protocolSources.count(conforms.first) == 0 && 
               "redundant protocol requirement?");

        protocolSources.insert(
          {conforms.first,
           *getBestConstraintSource<ProtocolDecl *>(conforms.second,
             [&](ProtocolDecl *proto) {
               return proto == conforms.first;
             })});
      }
    }

    // Sort the protocols in canonical order.
    llvm::array_pod_sort(protocols.begin(), protocols.end(), 
                         ProtocolType::compareProtocols);

    // Enumerate the conformance requirements.
    for (auto proto : protocols) {
      assert(protocolSources.count(proto) == 1 && "Missing conformance?");
      f(RequirementKind::Conformance, subjectType,
        proto->getDeclaredInterfaceType(),
        protocolSources.find(proto)->second);
    }
  };
}

void GenericSignatureBuilder::dump() {
  dump(llvm::errs());
}

void GenericSignatureBuilder::dump(llvm::raw_ostream &out) {
  out << "Requirements:";
  enumerateRequirements(getGenericParams(),
                        [&](RequirementKind kind,
                            Type type,
                            RequirementRHS constraint,
                            const RequirementSource *source) {
    switch (kind) {
    case RequirementKind::Conformance:
    case RequirementKind::Superclass:
      out << "\n  ";
      out << type.getString() << " : "
          << constraint.get<Type>().getString() << " [";
      source->print(out, &Context.SourceMgr);
      out << "]";
      break;
    case RequirementKind::Layout:
      out << "\n  ";
      out << type.getString() << " : "
          << constraint.get<LayoutConstraint>().getString() << " [";
      source->print(out, &Context.SourceMgr);
      out << "]";
      break;
    case RequirementKind::SameType:
      out << "\n  ";
      out << type.getString() << " == " ;
      if (auto secondType = constraint.dyn_cast<Type>()) {
        out << secondType.getString();
      } else {
        out << constraint.get<PotentialArchetype *>()->getDebugName();
      }
      out << " [";
      source->print(out, &Context.SourceMgr);
      out << "]";
      break;
    }
  });
  out << "\n";

  out << "Potential archetypes:\n";
  for (auto pa : Impl->PotentialArchetypes) {
    pa->dump(out, &Context.SourceMgr, 2);
  }
  out << "\n";
}

void GenericSignatureBuilder::addGenericSignature(GenericSignature *sig) {
  if (!sig) return;

  for (auto param : sig->getGenericParams())
    addGenericParameter(param);

  for (auto &reqt : sig->getRequirements())
    addRequirement(reqt, FloatingRequirementSource::forAbstract(), nullptr);
}

/// Collect the set of requirements placed on the given generic parameters and
/// their associated types.
static void collectRequirements(GenericSignatureBuilder &builder,
                                ArrayRef<GenericTypeParamType *> params,
                                SmallVectorImpl<Requirement> &requirements) {
  builder.enumerateRequirements(
      params,
      [&](RequirementKind kind,
          Type depTy,
          RequirementRHS type,
          const RequirementSource *source) {
    // Filter out derived requirements... except for concrete-type requirements
    // on generic parameters. The exception is due to the canonicalization of
    // generic signatures, which never eliminates generic parameters even when
    // they have been mapped to a concrete type.
    if (source->isDerivedRequirement() &&
        !(kind == RequirementKind::SameType &&
          depTy->is<GenericTypeParamType>() &&
          type.is<Type>()))
      return;

    if (depTy->hasError())
      return;

    assert(!depTy->findUnresolvedDependentMemberType() &&
           "Unresolved dependent member type in requirements");

    Type repTy;
    if (auto concreteTy = type.dyn_cast<Type>()) {
      // Maybe we were equated to a concrete or dependent type...
      repTy = concreteTy;

      // Drop requirements involving concrete types containing
      // unresolved associated types.
      if (repTy->findUnresolvedDependentMemberType())
        return;
    } else {
      auto layoutConstraint = type.get<LayoutConstraint>();
      requirements.push_back(Requirement(kind, depTy, layoutConstraint));
      return;
    }

    if (repTy->hasError())
      return;

    requirements.push_back(Requirement(kind, depTy, repTy));
  });
}

GenericSignature *GenericSignatureBuilder::computeGenericSignature(
                                          SourceLoc loc,
                                          bool allowConcreteGenericParams,
                                          bool allowBuilderToMove) && {
  // Finalize the builder, producing any necessary diagnostics.
  finalize(loc, Impl->GenericParams, allowConcreteGenericParams);

  // Collect the requirements placed on the generic parameter types.
  SmallVector<Requirement, 4> requirements;
  collectRequirements(*this, Impl->GenericParams, requirements);

  // Form the generic signature.
  auto sig = GenericSignature::get(Impl->GenericParams, requirements);

  // When we can, move this generic signature builder to make it the canonical
  // builder, rather than constructing a new generic signature builder that
  // will produce the same thing.
  //
  // We cannot do this when there were errors.
  // FIXME: The HadAnyRedundantConstraints bit is a hack because we are
  // over-minimizing.
  if (allowBuilderToMove && !Impl->HadAnyError &&
      !Impl->HadAnyRedundantConstraints) {
    // Register this generic signature builder as the canonical builder for the
    // given signature.
    Context.registerGenericSignatureBuilder(sig, std::move(*this));
  }

  // Wipe out the internal state, ensuring that nobody uses this builder for
  // anything more.
  Impl.reset();

  return sig;
}

GenericSignature *GenericSignatureBuilder::computeRequirementSignature(
                                                     ProtocolDecl *proto) {
  GenericSignatureBuilder builder(proto->getASTContext());

  // Add the 'self' parameter.
  auto selfType =
    proto->getSelfInterfaceType()->castTo<GenericTypeParamType>();
  builder.addGenericParameter(selfType);

  // Add the conformance of 'self' to the protocol.
  auto requirement =
    Requirement(RequirementKind::Conformance, selfType,
                proto->getDeclaredInterfaceType());

  builder.addRequirement(
                 requirement,
                 RequirementSource::forRequirementSignature(builder, selfType,
                                                            proto),
                 nullptr);

  return std::move(builder).computeGenericSignature(
           SourceLoc(),
           /*allowConcreteGenericPArams=*/false,
           /*allowBuilderToMove=*/false);
}

#pragma mark Generic signature verification

void GenericSignatureBuilder::verifyGenericSignature(ASTContext &context,
                                                     GenericSignature *sig) {
  llvm::errs() << "Validating generic signature: ";
  sig->print(llvm::errs());
  llvm::errs() << "\n";

  // Try removing each requirement in turn.
  auto genericParams = sig->getGenericParams();
  auto requirements = sig->getRequirements();
  for (unsigned victimIndex : indices(requirements)) {
    PrettyStackTraceGenericSignature debugStack("verifying", sig, victimIndex);

    // Form a new generic signature builder.
    GenericSignatureBuilder builder(context);

    // Add the generic parameters.
    for (auto gp : genericParams)
      builder.addGenericParameter(gp);

    // Add the requirements *except* the victim.
    auto source = FloatingRequirementSource::forAbstract();
    for (unsigned i : indices(requirements)) {
      if (i != victimIndex)
        builder.addRequirement(requirements[i], source, nullptr);
    }

    // Finalize the generic signature. If there were any errors, we formed
    // an invalid signature, so just continue.
    if (builder.Impl->HadAnyError) continue;

    // Form a generic signature from the result.
    auto newSig =
      std::move(builder).computeGenericSignature(
                                      SourceLoc(),
                                      /*allowConcreteGenericParams=*/true,
                                      /*allowBuilderToMove=*/true);

    // Check whether the removed requirement
    assert(!newSig->isRequirementSatisfied(requirements[victimIndex]) &&
           "Generic signature is not minimal");

    // Canonicalize the signature to check that it is canonical.
    (void)newSig->getCanonicalSignature();
  }
}

void GenericSignatureBuilder::verifyGenericSignaturesInModule(
                                                        ModuleDecl *module) {
  LoadedFile *loadedFile = nullptr;
  for (auto fileUnit : module->getFiles()) {
    loadedFile = dyn_cast<LoadedFile>(fileUnit);
    if (loadedFile) break;
  }

  if (!loadedFile) return;

  // Check all of the (canonical) generic signatures.
  SmallVector<GenericSignature *, 8> allGenericSignatures;
  SmallPtrSet<CanGenericSignature, 4> knownGenericSignatures;
  (void)loadedFile->getAllGenericSignatures(allGenericSignatures);
  ASTContext &context = module->getASTContext();
  for (auto genericSig : allGenericSignatures) {
    // Check whether this is the first time we've checked this (canonical)
    // signature.
    auto canGenericSig = genericSig->getCanonicalSignature();
    if (!knownGenericSignatures.insert(canGenericSig).second) continue;

    verifyGenericSignature(context, canGenericSig);
  }
}


