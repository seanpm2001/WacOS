//===--- Projection.h - Utilities for working with Projections --*- C++ -*-===//
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
///
/// \file
///
/// This file defines the class Projection and related utilities. A projection
/// is a representation of type projections that is nominal, tuple
/// agnostic. These utilities are useful for working with aggregate type trees
/// at a high level.
///
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_PROJECTION_H
#define SWIFT_SIL_PROJECTION_H

#include "swift/Basic/NullablePtr.h"
#include "swift/Basic/PointerIntEnum.h"
#include "swift/AST/TypeAlignments.h"
#include "swift/SIL/SILValue.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SILOptimizer/Analysis/ARCAnalysis.h"
#include "swift/SILOptimizer/Analysis/RCIdentityAnalysis.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/Allocator.h"

namespace swift {

class SILBuilder;
class ProjectionPath;
using ProjectionPathSet = llvm::DenseSet<ProjectionPath>;
using ProjectionPathList = llvm::SmallVector<Optional<ProjectionPath>, 8>;

enum class SubSeqRelation_t : uint8_t {
  Unknown,
  LHSStrictSubSeqOfRHS,
  RHSStrictSubSeqOfLHS,
  Equal,
};

/// Returns true if Seq is either LHSStrictSubSeqOfRHS or
/// RHSStrictSubSeqOfLHS. Returns false if Seq is one of either Equal or
/// Unrelated.
inline bool isStrictSubSeqRelation(SubSeqRelation_t Seq) {
  switch (Seq) {
  case SubSeqRelation_t::Unknown:
  case SubSeqRelation_t::Equal:
    return false;
  case SubSeqRelation_t::LHSStrictSubSeqOfRHS:
  case SubSeqRelation_t::RHSStrictSubSeqOfLHS:
    return true;
  }
}

/// Extract an integer index from a SILValue.
///
/// Return true if IndexVal is a constant index representable as unsigned
/// int. We do not support symbolic projections yet.
bool getIntegerIndex(SILValue IndexVal, unsigned &IndexConst);

/// The kind of projection that we are representing.
///
/// We represent types that need to have a type pointer (i.e. casts) using only
/// up to 3 bits. This is because types are only aligned up to 8 bits. See
/// TypeAlignments.h.
///
/// Projection Kinds which can be represented via indices can use as many bits
/// as they want to represent the kind. When the index value is uses at most 11
/// bits, we represent it inline in the data structure. This is taking advantage
/// of the fact that on most modern OSes (including Darwin), the zero page is
/// not allocated. In the case where we have more than 11 bits of value
/// (i.e. the index is >= 2048), we malloc memory to represent the index. In
/// most cases this will not happen. For simplicity, we limit such kinds to use
/// no more than 4 bits.
///
/// The Projection class contains the logic to use ProjectionKind in this
/// manner.
enum class ProjectionKind : unsigned {
  // PointerProjectionKinds
  Upcast = 0,
  RefCast = 1,
  BitwiseCast = 2,
  FirstPointerKind = Upcast,
  LastPointerKind = BitwiseCast,

  // Index Projection Kinds
  FirstIndexKind = 7,
  Struct = PointerIntEnumIndexKindValue<0, ProjectionKind>::value,
  Tuple = PointerIntEnumIndexKindValue<1, ProjectionKind>::value,
  Index = PointerIntEnumIndexKindValue<2, ProjectionKind>::value,
  Class = PointerIntEnumIndexKindValue<3, ProjectionKind>::value,
  Enum = PointerIntEnumIndexKindValue<4, ProjectionKind>::value,
  Box = PointerIntEnumIndexKindValue<5, ProjectionKind>::value,
  LastIndexKind = Enum,
};

constexpr unsigned MaxPointerProjectionKind = ((1 << TypeAlignInBits) - 1);
/// Make sure that our tagged pointer assumptions are true. See comment above
/// the declaration of ProjectionKind.
static_assert(unsigned(ProjectionKind::LastPointerKind) <=
              unsigned(MaxPointerProjectionKind),
              "Too many projection kinds to fit in Projection");

static inline bool isCastProjectionKind(ProjectionKind Kind) {
  switch (Kind) {
  case ProjectionKind::Upcast:
  case ProjectionKind::RefCast:
  case ProjectionKind::BitwiseCast:
    return true;
  case ProjectionKind::Struct:
  case ProjectionKind::Tuple:
  case ProjectionKind::Index:
  case ProjectionKind::Class:
  case ProjectionKind::Enum:
  case ProjectionKind::Box:
    return false;
  }
}

/// Given a SIL value, capture its element index and the value of the aggregate
/// that immediately contains it.
///
/// This lightweight utility maps a SIL address projection to an index.
struct ProjectionIndex {
  SILValue Aggregate;
  unsigned Index;

  explicit ProjectionIndex(SILValue V) : Index(~0U) {
    switch (V->getKind()) {
    default:
      break;
    case ValueKind::IndexAddrInst: {
      IndexAddrInst *IA = cast<IndexAddrInst>(V);
      if (getIntegerIndex(IA->getIndex(), Index))
        Aggregate = IA->getBase();
      break;
    }
    case ValueKind::StructElementAddrInst: {
      StructElementAddrInst *SEA = cast<StructElementAddrInst>(V);
      Index = SEA->getFieldNo();
      Aggregate = SEA->getOperand();
      break;
    }
    case ValueKind::RefElementAddrInst: {
      RefElementAddrInst *REA = cast<RefElementAddrInst>(V);
      Index = REA->getFieldNo();
      Aggregate = REA->getOperand();
      break;
    }
    case ValueKind::ProjectBoxInst: {
      ProjectBoxInst *PBI = cast<ProjectBoxInst>(V);
      // A box has only a single payload.
      Index = 0;
      Aggregate = PBI->getOperand();
      break;
    }
    case ValueKind::TupleElementAddrInst: {
      TupleElementAddrInst *TEA = cast<TupleElementAddrInst>(V);
      Index = TEA->getFieldNo();
      Aggregate = TEA->getOperand();
      break;
    }
    case ValueKind::StructExtractInst: {
      StructExtractInst *SEA = cast<StructExtractInst>(V);
      Index = SEA->getFieldNo();
      Aggregate = SEA->getOperand();
      break;
    }
    case ValueKind::TupleExtractInst: {
      TupleExtractInst *TEA = cast<TupleExtractInst>(V);
      Index = TEA->getFieldNo();
      Aggregate = TEA->getOperand();
      break;
    }
    }
  }
  bool isValid() const { return (bool)Aggregate; }
};

/// An abstract representation of the index of a subtype of an aggregate
/// type. This is a value type.
///
/// The intention is it to be paired with a base SILValue in order to provide a
/// lightweight manner of working at a high level with object and address
/// projections.
///
/// It is intended to be pointer sized and trivially copyable so that memcpy can
/// be used to copy a Projection.
class Projection {

  friend ProjectionPath;

  static constexpr unsigned NumPointerKindBits = TypeAlignInBits;

  /// This is the number of bits that we currently use to represent indices at
  /// the top of our word.
  static constexpr unsigned NumIndexKindBits = 4;

  using ValueTy = PointerIntEnum<ProjectionKind, TypeBase *,
                                 NumPointerKindBits, NumIndexKindBits>;
  /// A pointer sized type that is used to store the kind of projection that is
  /// being represented and also all of the necessary information to convert a
  /// base SILType to a derived field SILType.
  ValueTy Value;

public:
  Projection() = delete;

  explicit Projection(SILValue V)
      : Projection(dyn_cast<SILInstruction>(V)) {}
  explicit Projection(SILInstruction *I);

  Projection(ProjectionKind Kind, unsigned NewIndex)
      : Value(Kind, NewIndex) {}

  Projection(ProjectionKind Kind, TypeBase *Ptr)
      : Value(Kind, Ptr) {}

  Projection(Projection &&P) = default;
  Projection(const Projection &P) = default;
  ~Projection() = default;

  Projection &operator=(const Projection &P) = default;

  Projection &operator=(Projection &&P) = default;

  bool isValid() const { return Value.isValid(); }

  /// Convenience method for getting the underlying index. Assumes that this
  /// projection is valid. Otherwise it asserts.
  unsigned getIndex() const {
    return Value.getIndex();
  }

  /// Determine if I is a value projection instruction whose corresponding
  /// projection equals this projection.
  bool matchesObjectProjection(SILInstruction *I) const {
    Projection P(I);
    return P.isValid() && P == *this;
  }

  /// If Base's type matches this Projections type ignoring Address vs Object
  /// type differences and this Projection is representable as a value
  /// projection, create the relevant value projection and return it. Otherwise,
  /// return nullptr.
  NullablePtr<SILInstruction>
  createObjectProjection(SILBuilder &B, SILLocation Loc, SILValue Base) const;

  /// If Base's type matches this Projections type ignoring Address vs Object
  /// type differences and this projection is representable as an address
  /// projection, create the relevant address projection and return
  /// it. Otherwise, return nullptr.
  NullablePtr<SILInstruction>
  createAddressProjection(SILBuilder &B, SILLocation Loc, SILValue Base) const;

  NullablePtr<SILInstruction>
  createProjection(SILBuilder &B, SILLocation Loc, SILValue Base) const {
    if (Base->getType().isAddress()) {
      return createAddressProjection(B, Loc, Base);
    } else if (Base->getType().isObject()) {
      return createObjectProjection(B, Loc, Base);
    } else {
      llvm_unreachable("Unsupported SILValueCategory");
    }
  }

  /// Apply this projection to \p BaseType and return the relevant subfield's
  /// SILType if BaseField has less subtypes than projection's offset.
  SILType getType(SILType BaseType, SILModule &M) const {
    assert(isValid());
    switch (getKind()) {
    case ProjectionKind::Struct:
    case ProjectionKind::Class:
      return BaseType.getFieldType(getVarDecl(BaseType), M);
    case ProjectionKind::Enum:
      return BaseType.getEnumElementType(getEnumElementDecl(BaseType), M);
    case ProjectionKind::Box:
      return SILType::getPrimitiveAddressType(BaseType.castTo<SILBoxType>()->
                                              getBoxedType());
    case ProjectionKind::Tuple:
      return BaseType.getTupleElementType(getIndex());
    case ProjectionKind::Upcast:
    case ProjectionKind::RefCast:
    case ProjectionKind::BitwiseCast:
      return getCastType(BaseType);
    case ProjectionKind::Index:
      // Index types do not change the underlying type.
      return BaseType;
    }
  }

  VarDecl *getVarDecl(SILType BaseType) const {
    assert(isValid());
    assert((getKind() == ProjectionKind::Struct ||
            getKind() == ProjectionKind::Class));
    assert(BaseType.getNominalOrBoundGenericNominal() &&
           "This should only be called with a nominal type");
    auto *NDecl = BaseType.getNominalOrBoundGenericNominal();
    auto Iter = NDecl->getStoredProperties().begin();
    std::advance(Iter, getIndex());
    return *Iter;
  }

  EnumElementDecl *getEnumElementDecl(SILType BaseType) const {
    assert(isValid());
    assert(getKind() == ProjectionKind::Enum);
    assert(BaseType.getEnumOrBoundGenericEnum() && "Expected enum type");
    auto Iter = BaseType.getEnumOrBoundGenericEnum()->getAllElements().begin();
    std::advance(Iter, getIndex());
    return *Iter;
  }

  SILType getCastType(SILType BaseType) const {
    assert(isValid());
    assert(getKind() == ProjectionKind::Upcast ||
           getKind() == ProjectionKind::RefCast ||
           getKind() == ProjectionKind::BitwiseCast);
    auto *Ty = getPointer();
    assert(Ty->isCanonical());
    return SILType::getPrimitiveType(Ty->getCanonicalType(),
                                     BaseType.getCategory());
  }

  bool operator<(const Projection &Other) const;

  bool operator==(const Projection &Other) const {
    return Value == Other.Value;
  }

  bool operator!=(const Projection &Other) const {
    return !(*this == Other);
  }

  /// Returns the operand of a struct, tuple or enum instruction which is
  /// associated with this projection. Returns an invalid SILValue if \p I is
  /// not a matching aggregate instruction.
  SILValue getOperandForAggregate(SILInstruction *I) const;

  /// Convenience method for getting the raw underlying kind.
  ProjectionKind getKind() const { return *Value.getKind(); }

  /// Returns true if this instruction projects from an address type to an
  /// address subtype.
  static bool isAddressProjection(SILValue V) {
    switch (V->getKind()) {
    default:
      return false;
    case ValueKind::IndexAddrInst: {
      unsigned Scalar;
      return getIntegerIndex(cast<IndexAddrInst>(V)->getIndex(), Scalar);
    }
    case ValueKind::StructElementAddrInst:
    case ValueKind::RefElementAddrInst:
    case ValueKind::ProjectBoxInst:
    case ValueKind::TupleElementAddrInst:
    case ValueKind::UncheckedTakeEnumDataAddrInst:
      return true;
    }
  }

  /// Returns true if this instruction projects from an object type to an object
  /// subtype.
  static bool isObjectProjection(SILValue V) {
    switch (V->getKind()) {
    default:
      return false;
    case ValueKind::StructExtractInst:
    case ValueKind::TupleExtractInst:
    case ValueKind::UncheckedEnumDataInst:
      return true;
    }
  }

  /// Returns true if this instruction projects from an object type into an
  /// address subtype.
  static bool isObjectToAddressProjection(SILValue V) {
    return isa<RefElementAddrInst>(V) || isa<ProjectBoxInst>(V);
  }

  /// Given a specific SILType, return all first level projections if it is an
  /// aggregate.
  static void getFirstLevelProjections(SILType V, SILModule &Mod,
                                       llvm::SmallVectorImpl<Projection> &Out);

  /// Is this cast which only allows for equality?
  ///
  /// If we enforce strict type based aliasing when computing access paths this
  /// will not be necessary. For now we are conservative since I don't have time
  /// to track down potential miscompiles. If we introduce such a thing, we will
  /// need a new instruction that a frontend can use to introduce aliasing
  /// relationships when TBAA would say that aliasing can not occur.
  bool isAliasingCast() const {
    switch (getKind()) {
    case ProjectionKind::RefCast:
    case ProjectionKind::BitwiseCast:
      return true;
    case ProjectionKind::Upcast:
    case ProjectionKind::Struct:
    case ProjectionKind::Tuple:
    case ProjectionKind::Index:
    case ProjectionKind::Class:
    case ProjectionKind::Enum:
    case ProjectionKind::Box:
      return false;
    }
  }

  bool isNominalKind() const {
    switch (getKind()) {
    case ProjectionKind::Class:
    case ProjectionKind::Enum:
    case ProjectionKind::Struct:
      return true;
    case ProjectionKind::BitwiseCast:
    case ProjectionKind::Index:
    case ProjectionKind::RefCast:
    case ProjectionKind::Tuple:
    case ProjectionKind::Upcast:
    case ProjectionKind::Box:
      return false;
    }
  }

  /// Form an aggregate of type BaseType using the SILValue Values. Returns the
  /// aggregate on success if this is a case we handle or an empty SILValue
  /// otherwise.
  ///
  /// This can be used with getFirstLevelProjections to project out/reform
  /// values. We do not need to use the original projections here since to build
  /// aggregate instructions the order is the only important thing.
  static NullablePtr<SILInstruction>
  createAggFromFirstLevelProjections(SILBuilder &B, SILLocation Loc,
                                     SILType BaseType,
                                     llvm::SmallVectorImpl<SILValue> &Values);
private:
  /// Convenience method for getting the raw underlying index as a pointer.
  TypeBase *getPointer() const {
    return Value.getPointer();
  }
};

/// This is to make sure that new projection is never bigger than a
/// pointer. This is just for performance.
static_assert(sizeof(Projection) == sizeof(uintptr_t),
              "IndexType should be pointer sized");

/// A "path" of projections abstracting either value or aggregate projections
/// upon a value.
///
/// The main purpose of this class is to enable one to reason about iterated
/// chains of projections. Some example usages are:
///
/// 1. Converting value projections to aggregate projections or vis-a-versa.
/// 2. Performing tuple operations on two paths (using the mathematical
///    definition of tuples as ordered sets).
class ProjectionPath {
public:
  using PathTy = llvm::SmallVector<Projection, 4>;

private:
  SILType BaseType;
  SILType MostDerivedType;
  /// A path from base to most-derived.
  PathTy Path;

public:
  /// Create an empty path which serves as a stack. Use push_back() to populate
  /// the stack with members.
  ProjectionPath(SILType Base) 
     : BaseType(Base), MostDerivedType(SILType()), Path() {}
  ProjectionPath(SILType Base, SILType End) 
     : BaseType(Base), MostDerivedType(End), Path() {}
  ~ProjectionPath() = default;

  /// Do not allow copy construction. The only way to get one of these is from
  /// getProjectionPath.
  ProjectionPath(const ProjectionPath &Other) {
    BaseType = Other.BaseType;
    MostDerivedType = Other.MostDerivedType;
    Path = Other.Path;
  } 

  ProjectionPath &operator=(const ProjectionPath &O) {
    BaseType = O.BaseType;
    MostDerivedType = O.MostDerivedType;
    Path = O.Path;
    return *this;
  }

  /// We only allow for moves of ProjectionPath since we only want them to be
  /// able to be constructed by calling our factory method.
  ProjectionPath(ProjectionPath &&O) {
    BaseType = O.BaseType;
    MostDerivedType = O.MostDerivedType;
    Path = O.Path;
    O.BaseType = SILType();
    O.MostDerivedType = SILType();
    O.Path.clear();
  }

  ProjectionPath &operator=(ProjectionPath &&O) {
    BaseType = O.BaseType;
    MostDerivedType = O.MostDerivedType;
    Path = O.Path;
    O.BaseType = SILType();
    O.MostDerivedType = SILType();
    O.Path.clear();
    return *this;
  }

  /// Append the projection \p P onto this.
  ProjectionPath &append(const Projection &P) {
    push_back(P);
    // Invalidate most derived type.
    MostDerivedType = SILType();
    return *this;
  }

  /// Append the projections in \p Other onto this.
  ProjectionPath &append(const ProjectionPath &Other) {
    for (auto &X : Other.Path) {
      push_back(X);
    }
    // Invalidate most derived type.
    MostDerivedType = SILType();
    return *this;
  }

  /// Create a path of AddrProjection or ValueProjection with the given VA
  /// and Path.
  SILValue createExtract(SILValue VA, SILInstruction *Inst, bool IsVal) const;

  /// Create a new projection path from the SILValue Start to End.  Returns
  /// Nothing::None if there is no such path.
  ///
  /// *NOTE* This method allows for transitions from object types to address
  /// types via ref_element_addr. If Start is an address type though, End will
  /// always also be an address type.
  static Optional<ProjectionPath> getProjectionPath(SILValue Start,
                                                    SILValue End);

  /// Treating a projection path as an ordered set, if RHS is a prefix of LHS,
  /// return the projection path with that prefix removed.
  ///
  /// An example of this transformation would be:
  ///
  /// LHS = [A, B, C, D, E], RHS = [A, B, C] => Result = [D, E]
  static Optional<ProjectionPath>
  removePrefix(const ProjectionPath &Path, const ProjectionPath &Prefix);

  /// Given the SILType Base, expand every leaf nodes in the type tree.
  ///
  /// NOTE: this function returns a single empty projection path if the BaseType
  /// is a leaf node in the type tree.
  static void expandTypeIntoLeafProjectionPaths(SILType BaseType,
                                                SILModule *Mod,
                                                ProjectionPathList &P);

  /// Return true if the given projection paths in \p CPaths does not cover
  /// all the fields with non-trivial semantics, false otherwise.
  static bool hasUncoveredNonTrivials(SILType B, SILModule *Mod,
                                      ProjectionPathSet &CPaths);

  /// Returns true if the two paths have a non-empty symmetric
  /// difference.
  ///
  /// This means that the two objects have the same base but access different
  /// fields of the base object.
  bool hasNonEmptySymmetricDifference(const ProjectionPath &RHS) const;

  /// Compute the subsequence relation in between LHS and RHS which tells the
  /// user whether or not the two sequences are unrelated, equal, or if one is a
  /// subsequence of the other.
  SubSeqRelation_t computeSubSeqRelation(const ProjectionPath &RHS) const;

  /// Pushes an element to the path.
  void push_back(const Projection &Proj) { Path.push_back(Proj); }

  /// Removes the last element from the path.
  void pop_back() { Path.pop_back(); }

  /// Returns the last element of the path.
  const Projection &back() const { return Path.back(); }

  /// Returns true if LHS and RHS have all the same projections in the same
  /// order.
  bool operator==(const ProjectionPath &RHS) const {
    return computeSubSeqRelation(RHS) == SubSeqRelation_t::Equal;
  }

  bool operator!=(const ProjectionPath &RHS) const {
    return !(*this == RHS);
  }

  /// Returns the base type of the ProjectionPath.
  SILType getBaseType() const { return BaseType; }

  /// Returns the most derived type of the projection path.
  SILType getMostDerivedType(SILModule &M) {
    if (Path.empty())
      return getBaseType();
    if (MostDerivedType)
      return MostDerivedType;
    MostDerivedType = getDerivedType(Path.size(), M);
    return MostDerivedType;
  }

  /// Returns the ith derived type of the path. This is zero indexed with 0
  /// being the base type and n consisting of applying the up to n projections
  /// to the base type.
  SILType getDerivedType(unsigned i, SILModule &M) const {
    assert(i <= Path.size());
    SILType IterTy = getBaseType();
    if (i == 0)
      return IterTy;
    for (unsigned j : range(i)) {
      auto &Proj = Path[j];
      IterTy = Proj.getType(IterTy, M);
    }
    return IterTy;
  }

  /// Returns true if the contained projection path is empty.
  bool empty() const { return Path.empty(); }

  /// Returns the number of path elements in the given projection path.
  unsigned size() const { return Path.size(); }

  using iterator = PathTy::iterator;
  using const_iterator = PathTy::const_iterator;
  using reverse_iterator = PathTy::reverse_iterator;
  using const_reverse_iterator = PathTy::const_reverse_iterator;

  iterator begin() { return Path.begin(); }
  iterator end() { return Path.end(); }
  const_iterator begin() const { return Path.begin(); }
  const_iterator end() const { return Path.end(); }

  reverse_iterator rbegin() { return Path.rbegin(); }
  reverse_iterator rend() { return Path.rend(); }
  const_reverse_iterator rbegin() const { return Path.rbegin(); }
  const_reverse_iterator rend() const { return Path.rend(); }

  void verify(SILModule &M);

  raw_ostream &print(raw_ostream &OS, SILModule &M);
  raw_ostream &printProjections(raw_ostream &OS, SILModule &M) const;
  void dump(SILModule &M);
  void dumpProjections(SILModule &M) const;
};

/// Returns the hashcode for the new projection path.
static inline llvm::hash_code hash_value(const ProjectionPath &P) {
  return llvm::hash_combine_range(P.begin(), P.end());
}

/// Returns the hashcode for the projection path.
static inline llvm::hash_code hash_value(const Projection &P) {
  return llvm::hash_combine(static_cast<unsigned>(P.getKind()));
}

class ProjectionTree;

class ProjectionTreeNode {
  friend class ProjectionTree;

  /// The index of the current node in the tree. Can be used to lookup this node
  /// from the ProjectionTree. The reason why we use an Index instead of a
  /// pointer is that the SmallVector that we use to store these can reallocate
  /// invalidating our pointers.
  unsigned Index;

  /// The type this projection tree node represents.
  SILType NodeType;

  /// The projection that this node represents. None in the root.
  llvm::Optional<Projection> Proj;

  /// The index of the parent of this projection tree node in the projection
  /// tree. None in the root.
  llvm::Optional<unsigned> Parent;

  /// The list of 'non-projection' users of this projection.
  ///
  /// *NOTE* This also includes projections like enums we do not handle.
  llvm::SmallVector<Operand *, 4> NonProjUsers;

  /// The indices of the child projections of this node. Each one of these
  /// projections is associated with a field type from BaseType and will contain
  /// references to
  llvm::SmallVector<unsigned, 4> ChildProjections;

  /// Flag to see if ChildProjections have been initialized.
  bool Initialized;

  /// The index to the first ancestor of this node in the projection tree with a
  /// non-projection user.
  ///
  /// The reason that we track this information is that all nodes below such an
  /// ancestor must necessarily be alive. This makes it easy to fold together
  /// "levels" of leaf nodes recursively to create aggregate structures until we
  /// get to this ancestor.
  bool IsLive;

  enum {
    RootIndex = 0
  };

  /// Constructor for the root of the tree.
  ProjectionTreeNode(SILType NodeTy)
    : Index(0), NodeType(NodeTy), Proj(), Parent(), NonProjUsers(),
      ChildProjections(), Initialized(false), IsLive(false) {}

  // Normal constructor for non-root nodes.
  ProjectionTreeNode(ProjectionTreeNode *Parent, unsigned Index, SILType NodeTy,
                     Projection P)
    : Index(Index), NodeType(NodeTy), Proj(P),
      Parent(Parent->getIndex()), NonProjUsers(), ChildProjections(),
      Initialized(false), IsLive(false) {}

public:
  class NewAggregateBuilder;

  ~ProjectionTreeNode() = default;
  ProjectionTreeNode(const ProjectionTreeNode &) = default;

  llvm::ArrayRef<unsigned> getChildProjections() {
     return llvm::makeArrayRef(ChildProjections);
  }

  llvm::Optional<Projection> &getProjection() { return Proj; }

  llvm::SmallVector<Operand *, 4> getNonProjUsers() const {
    return NonProjUsers;
  };

  SILType getType() const { return NodeType; }

  bool isRoot() const {
    // Root does not have a parent. So if we have a parent, we cannot be root.
    if (Parent.hasValue()) {
      assert(Proj.hasValue() && "If parent is not none, then P should be not "
             "none");
      assert(Index != RootIndex && "If parent is not none, we cannot be root");
      return false;
    } else {
      assert(!Proj.hasValue() && "If parent is none, then P should be none");
      assert(Index == RootIndex && "Index must be root index");
      return true;
    }
  }

  ProjectionTreeNode *getChildForProjection(ProjectionTree &Tree,
                                               const Projection &P);

  NullablePtr<SILInstruction> createProjection(SILBuilder &B, SILLocation Loc,
                                               SILValue Arg) const;

  SILInstruction *
  createAggregate(SILBuilder &B, SILLocation Loc,
                  ArrayRef<SILValue> Args) const;

  unsigned getIndex() const { return Index; }

  ProjectionTreeNode *getParent(ProjectionTree &Tree);
  const ProjectionTreeNode *getParent(const ProjectionTree &Tree) const;

  ProjectionTreeNode *getParentOrNull(ProjectionTree &Tree) {
    if (!Parent.hasValue())
      return nullptr;
    return getParent(Tree);
  }

private:
  void addNonProjectionUser(Operand *Op) {
    IsLive = true;
    NonProjUsers.push_back(Op);
  }

  using ValueNodePair = std::pair<SILValue, ProjectionTreeNode *>;

  void processUsersOfValue(ProjectionTree &Tree,
                           llvm::SmallVectorImpl<ValueNodePair> &Worklist,
                           SILValue Value);

  void createNextLevelChildren(ProjectionTree &Tree);

  void createNextLevelChildrenForStruct(ProjectionTree &Tree, StructDecl *SD);

  void createNextLevelChildrenForTuple(ProjectionTree &Tree, TupleType *TT);
};

class ProjectionTree {
  friend class ProjectionTreeNode;

  SILModule &Mod;

  /// The allocator we use to allocate ProjectionTreeNodes in the tree.
  llvm::SpecificBumpPtrAllocator<ProjectionTreeNode> Allocator;

  // A common pattern is a 3 field struct.
  llvm::SmallVector<ProjectionTreeNode *, 4> ProjectionTreeNodes;
  llvm::SmallVector<unsigned, 3> LiveLeafIndices;

  using LeafValueMapTy = llvm::DenseMap<unsigned, SILValue>;

public:
  /// Construct a projection tree from BaseTy.
  ProjectionTree(SILModule &Mod, SILType BaseTy);
  /// Construct an uninitialized projection tree, which can then be
  /// initialized by initializeWithExistingTree.
  ProjectionTree(SILModule &Mod) : Mod(Mod) {}
  ~ProjectionTree();
  ProjectionTree(const ProjectionTree &) = delete;
  ProjectionTree(ProjectionTree &&) = default;
  ProjectionTree &operator=(const ProjectionTree &) = delete;
  ProjectionTree &operator=(ProjectionTree &&) = default;

  /// Compute liveness and use information in this projection tree using Base.
  /// All debug instructions (debug_value, debug_value_addr) are ignored.
  void computeUsesAndLiveness(SILValue Base);

  /// Create a root SILValue iout of the given leaf node values by walking on
  /// the projection tree.
  SILValue computeExplodedArgumentValue(SILBuilder &Builder,
                                        SILLocation Loc, 
                                        llvm::SmallVector<SILValue, 8> &LVs);
  SILValue computeExplodedArgumentValueInner(SILBuilder &Builder,
                                             SILLocation Loc, 
                                             ProjectionTreeNode *Node,
                                             LeafValueMapTy &LeafValues);

  /// Return the module associated with this tree.
  SILModule &getModule() const { return Mod; }

  llvm::ArrayRef<ProjectionTreeNode *> getProjectionTreeNodes() {
    return llvm::makeArrayRef(ProjectionTreeNodes);
  }

  /// Iterate over all values in the tree. The function should return false if
  /// it wants the iteration to end and true if it wants to continue.
  void visitProjectionTreeNodes(std::function<bool (ProjectionTreeNode &)> F);

  ProjectionTreeNode *getRoot() {
    return getNode(ProjectionTreeNode::RootIndex);
  }
  const ProjectionTreeNode *getRoot() const {
    return getNode(ProjectionTreeNode::RootIndex);
  }

  ProjectionTreeNode *getNode(unsigned i) {
    return ProjectionTreeNodes[i];
  }

  const ProjectionTreeNode *getNode(unsigned i) const {
    return ProjectionTreeNodes[i];
  }

  bool isSingleton() const {
    // If we only have one root node, there is no interesting explosion
    // here. Exit early.
    if (ProjectionTreeNodes.size() == 1)
      return true;

    // Now we know that we have multiple level node hierarchy. See if we have a
    // linear list of nodes down to a singular leaf node.
    auto *Node = getRoot();
    while (Node->ChildProjections.size() <= 1) {
      // If this node does not have any child projections, then it is a leaf
      // node. If we have not existed at this point then all of our parents must
      // have only had one child as well. So return true.
      if (Node->ChildProjections.empty())
        return true;

      // Otherwise, Node only has one Child. Set Node to that child and continue
      // searching.
      Node = getNode(Node->ChildProjections[0]);
    }

    // Otherwise this Node has multiple children, return false.
    return false;
  }


  void getLeafTypes(llvm::SmallVectorImpl<SILType> &OutArray) const {
    for (unsigned LeafIndex : LiveLeafIndices) {
      const ProjectionTreeNode *Node = getNode(LeafIndex);
      assert(Node->IsLive && "We are only interested in leafs that are live");
      OutArray.push_back(Node->getType());
    }
  }

  void
  getLeafNodes(llvm::SmallVectorImpl<const ProjectionTreeNode *> &Out) const {
    for (unsigned LeafIndex : LiveLeafIndices) {
      const ProjectionTreeNode *Node = getNode(LeafIndex);
      assert(Node->IsLive && "We are only interested in leafs that are live");
      Out.push_back(Node);
    }
  }

  /// Return the number of live leafs in the projection.
  size_t liveLeafCount() const {
    return LiveLeafIndices.size();
  }

  void createTreeFromValue(SILBuilder &B, SILLocation Loc, SILValue NewBase,
                           llvm::SmallVectorImpl<SILValue> &Leafs) const;

  void
  replaceValueUsesWithLeafUses(SILBuilder &B, SILLocation Loc,
                               llvm::SmallVectorImpl<SILValue> &Leafs);
 
private:
  void createRoot(SILType BaseTy) {
    assert(ProjectionTreeNodes.empty() &&
           "Should only create root when ProjectionTreeNodes is empty");
    auto *Node = new (Allocator.Allocate()) ProjectionTreeNode(BaseTy);
    ProjectionTreeNodes.push_back(Node);
  }

  ProjectionTreeNode *createChild(ProjectionTreeNode *Parent,
                                     SILType BaseTy,
                                     const Projection &P) {
    unsigned Index = ProjectionTreeNodes.size();
    auto *Node = new (Allocator.Allocate()) ProjectionTreeNode(Parent, Index,
                                                               BaseTy, P);
    ProjectionTreeNodes.push_back(Node);
    return ProjectionTreeNodes[Index];
  }

  ProjectionTreeNode *
  createChildForStruct(ProjectionTreeNode *Parent, SILType Ty, ValueDecl *VD,
                       unsigned Index) {
    Projection P = Projection(ProjectionKind::Struct, Index);
    ProjectionTreeNode *N = createChild(Parent, Ty, P);
    return N;
  }

  ProjectionTreeNode *
  createChildForClass(ProjectionTreeNode *Parent, SILType Ty, ValueDecl *VD,
                      unsigned Index) {
    Projection P = Projection(ProjectionKind::Class, Index);
    ProjectionTreeNode *N = createChild(Parent, Ty, P);
    return N;
  }

  ProjectionTreeNode *
  createChildForTuple(ProjectionTreeNode *Parent, SILType Ty, unsigned Index) {
    Projection P = Projection(ProjectionKind::Tuple, Index);
    ProjectionTreeNode *N = createChild(Parent, Ty, P);
    return N;
  }
};

} // end swift namespace

namespace llvm {
using swift::ProjectionPath;
/// Allow ProjectionPath to be used in DenseMap.
template <> struct DenseMapInfo<ProjectionPath> {
  static inline ProjectionPath getEmptyKey() {
    return ProjectionPath(DenseMapInfo<swift::SILType>::getEmptyKey(),
                          DenseMapInfo<swift::SILType>::getEmptyKey());
  }
  static inline ProjectionPath getTombstoneKey() {
    return ProjectionPath(DenseMapInfo<swift::SILType>::getTombstoneKey(),
                          DenseMapInfo<swift::SILType>::getTombstoneKey());
  }
  static inline unsigned getHashValue(const ProjectionPath &Val) {
    return hash_value(Val);
  }
  static bool isEqual(const ProjectionPath &LHS, const ProjectionPath &RHS) {
    return LHS == RHS;
  }
};
} // namespace llvm

#endif
