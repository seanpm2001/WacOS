//===--- LoadStoreOptUtils.h ------------------------------------*- C++ -*-===//
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
///
/// This file defines LSBase, a class containing a SILValue base
/// and a ProjectionPath. It is used as the base class for LSLocation and
/// LSValue.
///
/// In the case of LSLocation, the base represents the base of the allocated
/// objects and the ProjectionPath tells which field in the object the
/// LSLocation represents.
///
/// In the case of LSValue, the base represents the root of loaded or stored
/// value it represents. And the ProjectionPath represents the field in the
/// loaded/store value the LSValue represents.
///
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_LSBASE_H
#define SWIFT_SIL_LSBASE_H

#include "swift/SIL/InstructionUtils.h"
#include "swift/SIL/Projection.h"
#include "swift/SILOptimizer/Analysis/AliasAnalysis.h"
#include "swift/SILOptimizer/Analysis/EscapeAnalysis.h"
#include "swift/SILOptimizer/Analysis/TypeExpansionAnalysis.h"
#include "swift/SILOptimizer/Analysis/ValueTracking.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include <utility> 

namespace swift {

class LSBase;
class LSLocation;
class LSValue;

//===----------------------------------------------------------------------===//
//                           Load Store Base
//===----------------------------------------------------------------------===//

class LSBase {
public:
  enum KeyKind : uint8_t { Empty = 0, Tombstone, Normal };

protected:
  /// The base of the object.
  SILValue Base;
  /// Empty key, tombstone key or normal key.
  KeyKind Kind;
  /// The path to reach the accessed field of the object.
  Optional<ProjectionPath> Path;

public:
  /// Constructors.
  LSBase() : Base(), Kind(Normal) {}
  LSBase(KeyKind Kind) : Base(), Kind(Kind) {}
  LSBase(SILValue B) : Base(B), Kind(Normal) {}
  LSBase(SILValue B, const Optional<ProjectionPath> &P, KeyKind Kind = Normal)
      : Base(B), Kind(Kind), Path(P) {}

  /// Virtual destructor.
  virtual ~LSBase() {}

  /// Copy constructor.
  LSBase(const LSBase &RHS) {
    Base = RHS.Base;
    Kind = RHS.Kind;
    Path = RHS.Path;
  }

  /// Assignment operator.
  LSBase &operator=(const LSBase &RHS) {
    Base = RHS.Base;
    Kind = RHS.Kind;
    Path = RHS.Path;
    return *this;
  }

  /// Getters for LSBase.
  KeyKind getKind() const { return Kind; }
  SILValue getBase() const { return Base; }
  const Optional<ProjectionPath> &getPath() const { return Path; }

  /// Reset the LSBase, i.e. clear base and path.
  void reset() {
    Base = SILValue();
    Kind = Normal;
    Path.reset();
  }

  /// Returns whether the LSBase has been initialized properly.
  virtual bool isValid() const { return Base && Path.hasValue(); }

  /// Returns true if the LSBase has a non-empty projection path.
  bool hasEmptyProjectionPath() const { return !Path.getValue().size(); }

  /// return true if that the two objects have the same base but access different
  /// fields of the base object.
  bool hasNonEmptySymmetricPathDifference(const LSBase &RHS) const {
    const ProjectionPath &P = RHS.Path.getValue();
    return Path.getValue().hasNonEmptySymmetricDifference(P);
  }

  /// Subtract the given path from the ProjectionPath.
  void removePathPrefix(Optional<ProjectionPath> &P) {
    if (!P.hasValue())
      return;
    // Remove prefix does not modify the Path in-place.
    Path = ProjectionPath::removePrefix(Path.getValue(), P.getValue());
  }

  /// Return true if the RHS have identical projection paths.
  ///
  /// If both LSBase have empty paths, they are treated as having
  /// identical projection path.
  bool hasIdenticalProjectionPath(const LSBase &RHS) const {
    // If both Paths have no value, then the 2 LSBases are
    // different.
    if (!Path.hasValue() && !RHS.Path.hasValue())
      return false;
    // If 1 Path has value while the other does not, then the 2
    // LSBases are different.
    if (Path.hasValue() != RHS.Path.hasValue())
      return false;
    // If both Paths are empty, then the 2 LSBases are the same.
    if (Path.getValue().empty() && RHS.Path.getValue().empty())
      return true;
    // If both Paths have different values, then the 2 LSBases are
    // different.
    if (Path.getValue() != RHS.Path.getValue())
      return false;
    // Otherwise, the 2 LSBases are the same.
    return true;
  }

  /// Comparisons.
  bool operator!=(const LSBase &RHS) const {
    return !(*this == RHS);
  }
  bool operator==(const LSBase &RHS) const {
    // If type is not the same, then LSBases different.
    if (Kind != RHS.Kind)
      return false;
    // Return true if this is a Tombstone or Empty.
    if (Kind == Empty || Kind == Tombstone)
      return true;
    // If Base is different, then LSBases different.
    if (Base != RHS.Base)
      return false;
    // If the projection paths are different, then LSBases are
    // different.
    if (!hasIdenticalProjectionPath(RHS))
      return false;
    // These LSBases represent the same memory location.
    return true;
  }

  /// Print the LSBase.
  virtual void print(llvm::raw_ostream &os, SILModule *Mod) { 
    os << Base;
    Path.getValue().print(os, *Mod);
  }

  virtual void dump(SILModule *Mod) { print(llvm::dbgs(), Mod); }
};

static inline llvm::hash_code hash_value(const LSBase &S) {
  const SILValue Base = S.getBase();
  const Optional<ProjectionPath> &Path = S.getPath();
  llvm::hash_code HC = llvm::hash_combine(Base.getOpaqueValue());
  if (!Path.hasValue())
    return HC;
  return llvm::hash_combine(HC, hash_value(Path.getValue()));
}

//===----------------------------------------------------------------------===//
//                            Load Store Value
//===----------------------------------------------------------------------===//
using LSLocationValueMap = llvm::DenseMap<LSLocation, LSValue>;
using LSValueList = llvm::SmallVector<LSValue, 8>;
using LSValueIndexMap = llvm::SmallDenseMap<LSValue, unsigned, 32>;
using ValueTableMap = llvm::SmallMapVector<unsigned, unsigned, 8>;

/// A LSValue is an abstraction of an object field value in program. It
/// consists of a base that is the tracked SILValue, and a projection path to
/// the represented field.
class LSValue : public LSBase {
  /// If this is a covering value, we need to go to each predecessor to
  /// materialize the value.
  bool CoveringValue;
public:
  /// Constructors.
  LSValue() : LSBase(), CoveringValue(false) {}
  LSValue(KeyKind Kind) : LSBase(Kind), CoveringValue(false) {}
  LSValue(bool CSVal) : LSBase(Normal), CoveringValue(CSVal) {}
  LSValue(SILValue B, const ProjectionPath &P)
      : LSBase(B, P), CoveringValue(false) {}

  /// Copy constructor.
  LSValue(const LSValue &RHS) : LSBase(RHS) {
    CoveringValue = RHS.CoveringValue;
  }

  /// Assignment operator.
  LSValue &operator=(const LSValue &RHS) {
    LSBase::operator=(RHS);
    CoveringValue = RHS.CoveringValue;
    return *this;
  }

  /// Comparisons.
  bool operator!=(const LSValue &RHS) const { return !(*this == RHS); }
  bool operator==(const LSValue &RHS) const {
    if (CoveringValue && RHS.isCoveringValue())
      return true;
    if (CoveringValue != RHS.isCoveringValue())
      return false;
    return LSBase::operator==(RHS);
  }

  /// Returns whether the LSValue has been initialized properly.
  bool isValid() const {
    if (CoveringValue)
      return true;
    return LSBase::isValid();
  }

  /// Take the last level projection off. Return the modified LSValue.
  LSValue &stripLastLevelProjection() {
    Path.getValue().pop_back();
    return *this;
  }

  /// Returns true if this LSValue is a covering value.
  bool isCoveringValue() const { return CoveringValue; }

  /// Materialize the SILValue that this LSValue represents.
  ///
  /// In the case where we have a single value this can be materialized by
  /// applying Path to the Base.
  SILValue materialize(SILInstruction *Inst) {
    if (CoveringValue)
      return SILValue();
    return Path.getValue().createExtract(Base, Inst, true);
  }

  void print(llvm::raw_ostream &os, SILModule *Mod) {
    if (CoveringValue) {
      os << "Covering Value";
      return;
    }
    LSBase::print(os, Mod);
  }

  /// Expand this SILValue to all individual fields it contains.
  static void expand(SILValue Base, SILModule *Mod, LSValueList &Vals,
                     TypeExpansionAnalysis *TE);

  /// Given a memory location and a map between the expansions of the location
  /// and their corresponding values, try to come up with a single SILValue this
  /// location holds. This may involve extracting and aggregating available
  /// values.
  static void reduceInner(LSLocation &B, SILModule *M, LSLocationValueMap &Vals,
                          SILInstruction *InsertPt);
  static SILValue reduce(LSLocation &B, SILModule *M, LSLocationValueMap &Vals,
                         SILInstruction *InsertPt);
};

static inline llvm::hash_code hash_value(const LSValue &V) {
  llvm::hash_code HC = llvm::hash_combine(V.isCoveringValue());
  if (V.isCoveringValue())
    return HC;
  return llvm::hash_combine(HC, hash_value((LSBase)V));
}

//===----------------------------------------------------------------------===//
//                            Load Store Location
//===----------------------------------------------------------------------===//
using LSLocationSet = llvm::DenseSet<LSLocation>;
using LSLocationList = llvm::SmallVector<LSLocation, 8>;
using LSLocationIndexMap = llvm::SmallDenseMap<LSLocation, unsigned, 32>;
using LSLocationBaseMap = llvm::DenseMap<SILValue, LSLocation>;

/// This class represents a field in an allocated object. It consists of a
/// base that is the tracked SILValue, and a projection path to the
/// represented field.
class LSLocation : public LSBase {
public:
  /// Constructors.
  LSLocation() {}
  LSLocation(SILValue B, const Optional<ProjectionPath> &P, KeyKind K = Normal)
      : LSBase(B, P, K) {}
  LSLocation(KeyKind Kind) : LSBase(Kind) {}
  /// Use the concatenation of the 2 ProjectionPaths as the Path.
  LSLocation(SILValue B, const ProjectionPath &BP, const ProjectionPath &AP)
      : LSBase(B) {
    ProjectionPath P((*Base).getType());
    P.append(BP);
    P.append(AP);
    Path = P;
  }

  /// Initialize a location with a new set of base, projectionpath and kind.
  void init(SILValue B, const Optional<ProjectionPath> &P, KeyKind K= Normal) {
    Base = B;
    Path = P;
    Kind = K;
  }

  /// Copy constructor.
  LSLocation(const LSLocation &RHS) : LSBase(RHS) {}

  /// Assignment operator.
  LSLocation &operator=(const LSLocation &RHS) {
    LSBase::operator=(RHS);
    return *this;
  }

  /// Returns the type of the object the LSLocation represents.
  SILType getType(SILModule *M) {
    return Path.getValue().getMostDerivedType(*M);
  }

  /// Get the first level locations based on this location's first level
  /// projection.
  void getNextLevelLSLocations(LSLocationList &Locs, SILModule *Mod);

  /// Check whether the 2 LSLocations may alias each other or not.
  bool isMayAliasLSLocation(const LSLocation &RHS, AliasAnalysis *AA);

  /// Check whether the 2 LSLocations must alias each other or not.
  bool isMustAliasLSLocation(const LSLocation &RHS, AliasAnalysis *AA);

  /// Check whether the LSLocation can escape the current function.
  bool isNonEscapingLocalLSLocation(SILFunction *Fn, EscapeAnalysis *EA);

  /// Expand this location to all individual fields it contains.
  ///
  /// In SIL, we can have a store to an aggregate and loads from its individual
  /// fields. Therefore, we expand all the operations on aggregates onto
  /// individual fields and process them separately.
  static void expand(LSLocation Base, SILModule *Mod, LSLocationList &Locs,
                     TypeExpansionAnalysis *TE);

  /// Given a set of locations derived from the same base, try to merge/reduce
  /// them into smallest number of LSLocations possible.
  static bool reduce(LSLocation Base, SILModule *Mod, LSLocationSet &Locs);

  /// Enumerate the given Mem LSLocation.
  static void enumerateLSLocation(SILModule *M, SILValue Mem,
                                  std::vector<LSLocation> &LSLocationVault,
                                  LSLocationIndexMap &LocToBit,
                                  LSLocationBaseMap &BaseToLoc,
                                  TypeExpansionAnalysis *TE);

  /// Enumerate all the locations in the function.
  static void enumerateLSLocations(SILFunction &F,
                                   std::vector<LSLocation> &LSLocationVault,
                                   LSLocationIndexMap &LocToBit,
                                   LSLocationBaseMap &BaseToLoc,
                                   TypeExpansionAnalysis *TE,
                                   std::pair<int, int> &LSCount);
};

static inline llvm::hash_code hash_value(const LSLocation &L) {
  return llvm::hash_combine(hash_value((LSBase)L));
}

} // end swift namespace

/// LSLocation and LSValue are used in DenseMap.
namespace llvm {
using swift::LSBase;
using swift::LSLocation;
using swift::LSValue;

template <> struct DenseMapInfo<LSValue> {
  static inline LSValue getEmptyKey() {
    return LSValue(LSBase::Empty);
  }
  static inline LSValue getTombstoneKey() {
    return LSValue(LSBase::Tombstone);
  }
  static inline unsigned getHashValue(const LSValue &Val) {
    return hash_value(Val);
  }
  static bool isEqual(const LSValue &LHS, const LSValue &RHS) {
    return LHS == RHS;
  }
};

template <> struct DenseMapInfo<LSLocation> {
  static inline LSLocation getEmptyKey() {
    return LSLocation(LSBase::Empty);
  }
  static inline LSLocation getTombstoneKey() {
    return LSLocation(LSBase::Tombstone);
  }
  static inline unsigned getHashValue(const LSLocation &Loc) {
    return hash_value(Loc);
  }
  static bool isEqual(const LSLocation &LHS, const LSLocation &RHS) {
    return LHS == RHS;
  }
};

} // namespace llvm

#endif // SWIFT_SIL_LSBASE_H
