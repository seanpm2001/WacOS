//===--- AliasAnalysis.h - SIL Alias Analysis -------------------*- C++ -*-===//
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

#ifndef SWIFT_SILOPTIMIZER_ANALYSIS_ALIASANALYSIS_H
#define SWIFT_SILOPTIMIZER_ANALYSIS_ALIASANALYSIS_H

#include "swift/SIL/ApplySite.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SILOptimizer/Analysis/Analysis.h"
#include "llvm/ADT/DenseMap.h"

namespace swift {

class SideEffectAnalysis;
class EscapeAnalysis;

/// This class is a simple wrapper around an alias analysis cache. This is
/// needed since we do not have an "analysis" infrastructure.
class AliasAnalysis {
public:

  /// This enum describes the different kinds of aliasing relations between
  /// pointers.
  ///
  /// NoAlias: There is never dependence between memory referenced by the two
  ///          pointers. Example: Two pointers pointing to non-overlapping
  ///          memory ranges.
  ///
  /// MayAlias: Two pointers might refer to the same memory location.
  ///
  ///
  /// PartialAlias: The two memory locations are known to be overlapping
  ///               but do not start at the same address.
  ///
  ///
  /// MustAlias: The two memory locations always start at exactly the same
  ///            location. The pointers are equal.
  ///
  enum class AliasResult : unsigned {
    NoAlias=0,      ///< The two values have no dependencies on each
                    ///  other.
    MayAlias,       ///< The two values cannot be proven to alias or
                    ///  not alias. Anything could happen.
    PartialAlias,   ///< The two values overlap in a partial manner.
    MustAlias,      ///< The two values are equal.
  };

private:
  /// A key used for the AliasAnalysis cache.
  ///
  /// This struct represents the argument list to the method 'alias'.
  struct AliasCacheKey {
    // The SILValue pair:
    SILValue V1, V2;
    // The TBAAType pair:
    void *T1, *T2;
  };

  friend struct ::llvm::DenseMapInfo<swift::AliasAnalysis::AliasCacheKey>;

  /// A key used for the MemoryBehavior Analysis cache.
  using MemBehaviorCacheKey = std::pair<SILValue, SILInstruction *>;

  using ScopeCacheKey = std::pair<SILInstruction *, SILInstruction *>;

  using TBAACacheKey = std::pair<SILType, SILType>;

  SideEffectAnalysis *SEA;
  EscapeAnalysis *EA;

  /// A cache for the computation of TBAA. True means that the types may
  /// alias. False means that the types must not alias.
  ///
  /// We don't need to invalidate this cache because type aliasing relations
  /// never change.
  llvm::DenseMap<TBAACacheKey, bool> TypesMayAliasCache;

  /// AliasAnalysis value cache.
  ///
  /// The alias() method uses this map to cache queries.
  llvm::DenseMap<AliasCacheKey, AliasResult> AliasCache;

  using MemoryBehavior = SILInstruction::MemoryBehavior;

  /// MemoryBehavior value cache.
  ///
  /// The computeMemoryBehavior() method uses this map to cache queries.
  llvm::DenseMap<MemBehaviorCacheKey, MemoryBehavior> MemoryBehaviorCache;

  /// Set of instructions inside immutable-scopes.
  ///
  /// Contains pairs of intructions: the first instruction is the begin-scope
  /// instruction (e.g. begin_access), the second instruction is an
  /// instruction inside the scope (only may-write instructions are considered).
  llvm::DenseSet<ScopeCacheKey> instsInImmutableScopes;

  /// Computed immutable scopes.
  ///
  /// Contains the begin-scope instructions (e.g. begin_access) of all computed
  /// scopes.
  llvm::SmallPtrSet<SILInstruction *, 16> immutableScopeComputed;

  AliasResult aliasAddressProjection(SILValue V1, SILValue V2,
                                     SILValue O1, SILValue O2);

  /// Perform an alias query to see if V1, V2 refer to the same values.
  AliasResult aliasInner(SILValue V1, SILValue V2,
                         SILType TBAAType1 = SILType(),
                         SILType TBAAType2 = SILType());  

  /// Returns True if memory of type \p T1 and \p T2 may alias.
  bool typesMayAlias(SILType T1, SILType T2, const SILFunction &F);

  void computeImmutableScope(SingleValueInstruction *beginScopeInst);

  bool isInImmutableScope(SILInstruction *inst, SILValue V);

public:
  AliasAnalysis(SideEffectAnalysis *SEA, EscapeAnalysis *EA)
    : SEA(SEA), EA(EA) {}

  static SILAnalysisKind getAnalysisKind() { return SILAnalysisKind::Alias; }

  /// Perform an alias query to see if V1, V2 refer to the same values.
  AliasResult alias(SILValue V1, SILValue V2, SILType TBAAType1 = SILType(),
                    SILType TBAAType2 = SILType());

  /// Convenience method that returns true if V1 and V2 must alias.
  bool isMustAlias(SILValue V1, SILValue V2, SILType TBAAType1 = SILType(),
                   SILType TBAAType2 = SILType()) {
    return alias(V1, V2, TBAAType1, TBAAType2) == AliasResult::MustAlias;
  }

  /// Convenience method that returns true if V1 and V2 partially alias.
  bool isPartialAlias(SILValue V1, SILValue V2, SILType TBAAType1 = SILType(),
                      SILType TBAAType2 = SILType()) {
    return alias(V1, V2, TBAAType1, TBAAType2) == AliasResult::PartialAlias;
  }

  /// Convenience method that returns true if V1, V2 cannot alias.
  bool isNoAlias(SILValue V1, SILValue V2, SILType TBAAType1 = SILType(),
                 SILType TBAAType2 = SILType()) {
    return alias(V1, V2, TBAAType1, TBAAType2) == AliasResult::NoAlias;
  }

  /// Convenience method that returns true if V1, V2 may alias.
  bool isMayAlias(SILValue V1, SILValue V2, SILType TBAAType1 = SILType(),
                  SILType TBAAType2 = SILType()) {
    return alias(V1, V2, TBAAType1, TBAAType2) == AliasResult::MayAlias;
  }

  /// \returns True if the release of the \p releasedReference can access or
  /// free memory accessed by \p User.
  bool mayValueReleaseInterfereWithInstruction(SILInstruction *User,
                                               SILValue releasedReference);

  /// Use the alias analysis to determine the memory behavior of Inst with
  /// respect to V.
  MemoryBehavior computeMemoryBehavior(SILInstruction *Inst, SILValue V);

  /// Use the alias analysis to determine the memory behavior of Inst with
  /// respect to V.
  MemoryBehavior computeMemoryBehaviorInner(SILInstruction *Inst, SILValue V);

  /// Returns true if \p Inst may read from memory at address \p V.
  ///
  /// For details see SILInstruction::MemoryBehavior::MayRead.
  bool mayReadFromMemory(SILInstruction *Inst, SILValue V) {
    auto B = computeMemoryBehavior(Inst, V);
    return B == MemoryBehavior::MayRead ||
           B == MemoryBehavior::MayReadWrite ||
           B == MemoryBehavior::MayHaveSideEffects;
  }

  /// Returns true if \p Inst may write to memory or deinitialize memory at
  /// address \p V.
  ///
  /// For details see SILInstruction::MemoryBehavior::MayWrite.
  bool mayWriteToMemory(SILInstruction *Inst, SILValue V) {
    auto B = computeMemoryBehavior(Inst, V);
    return B == MemoryBehavior::MayWrite ||
           B == MemoryBehavior::MayReadWrite ||
           B == MemoryBehavior::MayHaveSideEffects;
  }

  /// Returns true if \p Inst may read from memory, write to memory or
  /// deinitialize memory at address \p V.
  ///
  /// For details see SILInstruction::MemoryBehavior.
  bool mayReadOrWriteMemory(SILInstruction *Inst, SILValue V) {
    auto B = computeMemoryBehavior(Inst, V);
    return MemoryBehavior::None != B;
  }

  /// Returns true if \p Ptr may be released in the function call \p FAS.
  bool canApplyDecrementRefCount(FullApplySite FAS, SILValue Ptr);

  /// Returns true if \p Ptr may be released by the builtin \p BI.
  bool canBuiltinDecrementRefCount(BuiltinInst *BI, SILValue Ptr);
};


llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                              AliasAnalysis::AliasResult R);

/// If this value is an address that obeys strict TBAA, return the address type.
/// Otherwise, return an empty type.
SILType computeTBAAType(SILValue V);

} // end namespace swift

namespace llvm {
template <> struct DenseMapInfo<swift::AliasAnalysis::AliasCacheKey> {
  using AliasCacheKey = swift::AliasAnalysis::AliasCacheKey;

  static inline AliasCacheKey getEmptyKey() {
    return {DenseMapInfo<swift::SILValue>::getEmptyKey(), swift::SILValue(),
            nullptr, nullptr};
  }
  static inline AliasCacheKey getTombstoneKey() {
    return {DenseMapInfo<swift::SILValue>::getTombstoneKey(), swift::SILValue(),
            nullptr, nullptr};
  }
  static unsigned getHashValue(const AliasCacheKey Val) {
    unsigned H = 0;
    H ^= DenseMapInfo<swift::SILValue>::getHashValue(Val.V1);
    H ^= DenseMapInfo<swift::SILValue>::getHashValue(Val.V2);
    H ^= DenseMapInfo<void *>::getHashValue(Val.T1);
    H ^= DenseMapInfo<void *>::getHashValue(Val.T2);
    return H;
  }
  static bool isEqual(const AliasCacheKey LHS, const AliasCacheKey RHS) {
    return LHS.V1 == RHS.V1 &&
           LHS.V2 == RHS.V2 &&
           LHS.T1 == RHS.T1 &&
           LHS.T2 == RHS.T2;
  }
};
}

#endif
