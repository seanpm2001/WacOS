//===--- ARCAnalysis.h - SIL ARC Analysis -----------------------*- C++ -*-===//
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

#ifndef SWIFT_SILOPTIMIZER_ANALYSIS_ARCANALYSIS_H
#define SWIFT_SILOPTIMIZER_ANALYSIS_ARCANALYSIS_H

#include "swift/SIL/SILArgument.h"
#include "swift/SIL/SILValue.h"
#include "swift/SIL/SILBasicBlock.h"
#include "swift/SILOptimizer/Analysis/AliasAnalysis.h"
#include "swift/SILOptimizer/Analysis/PostOrderAnalysis.h"
#include "swift/SILOptimizer/Analysis/RCIdentityAnalysis.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/TinyPtrVector.h"

namespace swift {

class SILInstruction;
class AliasAnalysis;
class PostOrderAnalysis;
class RCIdentityAnalysis;
class RCIdentityFunctionInfo;
class LoopRegionFunctionInfo;
class SILLoopInfo;
class SILFunction;

} // end namespace swift

namespace swift {

using RetainList = llvm::SmallVector<SILInstruction *, 1>;
using ReleaseList = llvm::SmallVector<SILInstruction *, 1>;

/// \returns True if the user \p User decrements the ref count of \p Ptr.
bool mayDecrementRefCount(SILInstruction *User, SILValue Ptr,
                          AliasAnalysis *AA);

/// \returns True if the user \p User checks the ref count of a pointer.
bool mayCheckRefCount(SILInstruction *User);

/// \returns True if the \p User might use the pointer \p Ptr in a manner that
/// requires \p Ptr to be alive before Inst or the release of Ptr may use memory
/// accessed by \p User.
bool mayHaveSymmetricInterference(SILInstruction *User, SILValue Ptr,
                                 AliasAnalysis *AA);

/// \returns True if the \p User must use the pointer \p Ptr in a manner that
/// requires \p Ptr to be alive before Inst.
bool mustUseValue(SILInstruction *User, SILValue Ptr, AliasAnalysis *AA);

/// Returns true if User must use Ptr in a guaranteed way.
///
/// This means that assuming that everything is conservative, we can ignore the
/// ref count effects of User on Ptr since we will only remove things over
/// guaranteed parameters if we are known safe in both directions.
bool mustGuaranteedUseValue(SILInstruction *User, SILValue Ptr,
                            AliasAnalysis *AA);

/// Returns true if \p Inst can never conservatively decrement reference counts.
bool canNeverDecrementRefCounts(SILInstruction *Inst);

/// \returns True if \p User can never use a value in a way that requires the
/// value to be alive.
///
/// This is purposefully a negative query to contrast with canUseValue which is
/// about a specific value while this is about general values.
bool canNeverUseValues(SILInstruction *User);

/// \returns true if the user \p User may use \p Ptr in a manner that requires
/// Ptr's life to be guaranteed to exist at this point.
///
/// TODO: Better name.
bool mayGuaranteedUseValue(SILInstruction *User, SILValue Ptr,
                           AliasAnalysis *AA);

/// If \p Op has arc uses in the instruction range [Start, End), return the
/// first such instruction. Otherwise return None. We assume that
/// Start and End are both in the same basic block.
Optional<SILBasicBlock::iterator>
valueHasARCUsesInInstructionRange(SILValue Op,
                                  SILBasicBlock::iterator Start,
                                  SILBasicBlock::iterator End,
                                  AliasAnalysis *AA);

/// If \p Op has arc uses in the instruction range [Start, End), return the last
/// use of such instruction. Otherwise return None. We assume that Start and End
/// are both in the same basic block.
Optional<SILBasicBlock::iterator> valueHasARCUsesInReverseInstructionRange(
    SILValue Op, SILBasicBlock::iterator Start, SILBasicBlock::iterator End,
    AliasAnalysis *AA);

/// If \p Op has instructions in the instruction range (Start, End] which may
/// decrement it, return the first such instruction. Returns None
/// if no such instruction exists. We assume that Start and End are both in the
/// same basic block.
Optional<SILBasicBlock::iterator>
valueHasARCDecrementOrCheckInInstructionRange(SILValue Op,
                                              SILBasicBlock::iterator Start,
                                              SILBasicBlock::iterator End,
                                              AliasAnalysis *AA);

/// A class that attempts to match owned return value and corresponding
/// epilogue retains for a specific function.
///
/// If we can not find the retain in the return block, we will try to find
/// in the predecessors. 
///
/// The search stop when we encounter an instruction that may decrement
/// the return'ed value, as we do not want to create a lifetime gap once the
/// retain is moved.
class ConsumedResultToEpilogueRetainMatcher {
public:
  /// The state on how retains are found in a basic block.
  enum class FindRetainKind { 
    None,      ///< Did not find a retain.
    Found,     ///< Found a retain.
    Recursion, ///< Found a retain and its due to self-recursion.
    Blocked    ///< Found a blocking instructions, i.e. MayDecrement.
  };

  using RetainKindValue = std::pair<FindRetainKind, SILInstruction *>;

private:
  SILFunction *F;
  RCIdentityFunctionInfo *RCFI;
  AliasAnalysis *AA;
  // We use a list of instructions for now so that we can keep the same interface
  // and handle exploded retain_value later.
  RetainList EpilogueRetainInsts;

  /// Return true if all the successors of the EpilogueRetainInsts do not have
  /// a retain. 
  bool isTransitiveSuccessorsRetainFree(llvm::DenseSet<SILBasicBlock *> BBs);

  /// Finds matching releases in the provided block \p BB.
  RetainKindValue findMatchingRetainsInBasicBlock(SILBasicBlock *BB, SILValue V);
public:
  /// Finds matching releases in the return block of the function \p F.
  ConsumedResultToEpilogueRetainMatcher(RCIdentityFunctionInfo *RCFI,
                                        AliasAnalysis *AA,
                                        SILFunction *F);

  /// Finds matching releases in the provided block \p BB.
  void findMatchingRetains(SILBasicBlock *BB);

  RetainList getEpilogueRetains() { return EpilogueRetainInsts; }

  /// Recompute the mapping from argument to consumed arg.
  void recompute();

  using iterator = decltype(EpilogueRetainInsts)::iterator;
  using const_iterator = decltype(EpilogueRetainInsts)::const_iterator;
  iterator begin() { return EpilogueRetainInsts.begin(); }
  iterator end() { return EpilogueRetainInsts.end(); }
  const_iterator begin() const { return EpilogueRetainInsts.begin(); }
  const_iterator end() const { return EpilogueRetainInsts.end(); }

  using reverse_iterator = decltype(EpilogueRetainInsts)::reverse_iterator;
  using const_reverse_iterator = decltype(EpilogueRetainInsts)::const_reverse_iterator;
  reverse_iterator rbegin() { return EpilogueRetainInsts.rbegin(); }
  reverse_iterator rend() { return EpilogueRetainInsts.rend(); }
  const_reverse_iterator rbegin() const { return EpilogueRetainInsts.rbegin(); }
  const_reverse_iterator rend() const { return EpilogueRetainInsts.rend(); }

  unsigned size() const { return EpilogueRetainInsts.size(); }

  iterator_range<iterator> getRange() { return swift::make_range(begin(), end()); }
};

/// A class that attempts to match owned arguments and corresponding epilogue
/// releases for a specific function.
///
/// Only try to find the epilogue release in the return block.
class ConsumedArgToEpilogueReleaseMatcher {
public:
  enum class ExitKind { Return, Throw };

private:
  SILFunction *F;
  RCIdentityFunctionInfo *RCFI;
  ExitKind Kind;
  llvm::SmallMapVector<SILArgument *, ReleaseList, 8> ArgInstMap;

  /// Set to true if we found some releases but not all for the argument.
  llvm::DenseSet<SILArgument *> FoundSomeReleases;

  /// Eventually this will be used in place of HasBlock.
  SILBasicBlock *ProcessedBlock;

  /// Return true if we have seen releases to part or all of \p Derived in
  /// \p Insts.
  /// 
  /// NOTE: This function relies on projections to analyze the relation
  /// between the releases values in \p Insts and \p Derived, it also bails
  /// out and return true if projection path can not be formed between Base
  /// and any one the released values.
  bool isRedundantRelease(ReleaseList Insts, SILValue Base, SILValue Derived);

  /// Return true if we have a release instruction for all the reference
  /// semantics part of \p Argument.
  bool releaseArgument(ReleaseList Insts, SILValue Argument);

  /// Walk the basic block and find all the releases that match to function
  /// arguments. 
  void collectMatchingReleases(SILBasicBlock *BB);

  /// For every argument in the function, check to see whether all epilogue
  /// releases are found. Clear all releases for the argument if not all 
  /// epilogue releases are found.
  void processMatchingReleases();

public:
  /// Finds matching releases in the return block of the function \p F.
  ConsumedArgToEpilogueReleaseMatcher(RCIdentityFunctionInfo *RCFI,
                                      SILFunction *F,
                                      ExitKind Kind = ExitKind::Return);

  /// Finds matching releases in the provided block \p BB.
  void findMatchingReleases(SILBasicBlock *BB);

  bool hasBlock() const { return ProcessedBlock != nullptr; }

  bool isEpilogueRelease(SILInstruction *I) const {
    // This is not a release instruction in the epilogue block.
    if (I->getParent() != ProcessedBlock)
      return false;
    for (auto &X : ArgInstMap) {
      // Either did not find epilogue release or found exploded epilogue
      // releases.
      if (X.second.size() != 1)
        continue;
      if (*X.second.begin() == I) 
        return true;
    }
    return false;
  }

  /// Return true if we've found some epilogue releases for the argument
  /// but not all.
  bool hasSomeReleasesForArgument(SILArgument *Arg) {
    return FoundSomeReleases.find(Arg) != FoundSomeReleases.end();
  }

  bool isSingleRelease(SILArgument *Arg) const {
    auto Iter = ArgInstMap.find(Arg);
    assert(Iter != ArgInstMap.end() && "Failed to get release list for argument");
    return Iter->second.size() == 1;
  }

  SILInstruction *getSingleReleaseForArgument(SILArgument *Arg) {
    auto I = ArgInstMap.find(Arg);
    if (I == ArgInstMap.end())
      return nullptr;
    if (!isSingleRelease(Arg))
      return nullptr;
    return *I->second.begin();
  }

  SILInstruction *getSingleReleaseForArgument(SILValue V) {
    auto *Arg = dyn_cast<SILArgument>(V);
    if (!Arg)
      return nullptr;
    return getSingleReleaseForArgument(Arg);
  }

  ReleaseList getReleasesForArgument(SILArgument *Arg) {
    ReleaseList Releases;
    auto I = ArgInstMap.find(Arg);
    if (I == ArgInstMap.end())
      return Releases;
    return I->second; 
  }

  ReleaseList getReleasesForArgument(SILValue V) {
    ReleaseList Releases;
    auto *Arg = dyn_cast<SILArgument>(V);
    if (!Arg)
      return Releases;
    return getReleasesForArgument(Arg);
  }

  /// Recompute the mapping from argument to consumed arg.
  void recompute();

  bool isSingleReleaseMatchedToArgument(SILInstruction *Inst) {
    auto Pred = [&Inst](const std::pair<SILArgument *,
                                        ReleaseList> &P) -> bool {
      if (P.second.size() > 1)
        return false;
      return *P.second.begin() == Inst;
    };
    return count_if(ArgInstMap, Pred);
  }

  using iterator = decltype(ArgInstMap)::iterator;
  using const_iterator = decltype(ArgInstMap)::const_iterator;
  iterator begin() { return ArgInstMap.begin(); }
  iterator end() { return ArgInstMap.end(); }
  const_iterator begin() const { return ArgInstMap.begin(); }
  const_iterator end() const { return ArgInstMap.end(); }

  using reverse_iterator = decltype(ArgInstMap)::reverse_iterator;
  using const_reverse_iterator = decltype(ArgInstMap)::const_reverse_iterator;
  reverse_iterator rbegin() { return ArgInstMap.rbegin(); }
  reverse_iterator rend() { return ArgInstMap.rend(); }
  const_reverse_iterator rbegin() const { return ArgInstMap.rbegin(); }
  const_reverse_iterator rend() const { return ArgInstMap.rend(); }

  unsigned size() const { return ArgInstMap.size(); }

  iterator_range<iterator> getRange() { return swift::make_range(begin(), end()); }
};

class ReleaseTracker {
  llvm::SmallSetVector<SILInstruction *, 4> TrackedUsers;
  llvm::SmallSetVector<SILInstruction *, 4> FinalReleases;
  std::function<bool(SILInstruction *)> AcceptableUserQuery;

public:
  ReleaseTracker(std::function<bool(SILInstruction *)> AcceptableUserQuery)
      : TrackedUsers(), FinalReleases(),
        AcceptableUserQuery(AcceptableUserQuery) {}

  void trackLastRelease(SILInstruction *Inst) { FinalReleases.insert(Inst); }

  bool isUserAcceptable(SILInstruction *User) const {
    return AcceptableUserQuery(User);
  }

  void trackUser(SILInstruction *User) { TrackedUsers.insert(User); }

  using range = iterator_range<llvm::SmallSetVector<SILInstruction *, 4>::iterator>;

  range getTrackedUsers() { return {TrackedUsers.begin(), TrackedUsers.end()}; }

  range getFinalReleases() {
    return {FinalReleases.begin(), FinalReleases.end()};
  }
};

/// Return true if we can find a set of post-dominating final releases. Returns
/// false otherwise. The FinalRelease set is placed in the out parameter
/// FinalRelease.
bool getFinalReleasesForValue(SILValue Value, ReleaseTracker &Tracker);

/// Match a call to a trap BB with no ARC relevant side effects.
bool isARCInertTrapBB(const SILBasicBlock *BB);

/// Get the two result values of the builtin "unsafeGuaranteed" instruction.
///
/// Gets the (GuaranteedValue, Token) tuple from a call to "unsafeGuaranteed"
/// if the tuple elements are identified by a single tuple_extract use.
/// Otherwise, returns a (nullptr, nullptr) tuple.
std::pair<SILInstruction *, SILInstruction *>
getSingleUnsafeGuaranteedValueResult(BuiltinInst *UnsafeGuaranteedInst);

/// Get the single builtin "unsafeGuaranteedEnd" user of a builtin
/// "unsafeGuaranteed"'s token.
BuiltinInst *getUnsafeGuaranteedEndUser(SILInstruction *UnsafeGuaranteedToken);

/// Walk backwards from an unsafeGuaranteedEnd builtin instruction looking for a
/// release on the reference returned by the matching unsafeGuaranteed builtin
/// ignoring releases on the way.
/// Return nullptr if no release is found.
///
///    %4 = builtin "unsafeGuaranteed"<Foo>(%0 : $Foo) : $(Foo, Builtin.Int8)
///    %5 = tuple_extract %4 : $(Foo, Builtin.Int8), 0
///    %6 = tuple_extract %4 : $(Foo, Builtin.Int8), 1
///    strong_release %5 : $Foo // <-- Matching release.
///    strong_release %6 : $Foo // Ignore.
///    %12 = builtin "unsafeGuaranteedEnd"(%6 : $Builtin.Int8) : $()
///
/// Alternatively, look for the release after the unsafeGuaranteedEnd.
SILInstruction *findReleaseToMatchUnsafeGuaranteedValue(
    SILInstruction *UnsafeGuaranteedEndI, SILInstruction *UnsafeGuaranteedI,
    SILValue UnsafeGuaranteedValue, SILBasicBlock &BB,
    RCIdentityFunctionInfo &RCFI);

} // end namespace swift


namespace swift {

/// EpilogueARCBlockState - Keep track of whether an epilogue ARC instruction
/// has been found.
struct EpilogueARCBlockState {
  /// Keep track of whether an epilogue release has been found before and after
  /// this basic block.
  bool BBSetIn;
  /// The basic block local SILValue we are interested to find epilogue ARC in.
  SILValue LocalArg;
  /// Constructor, we only compute epilogue ARC instruction for 1 argument at
  /// a time.
  /// Optimistic data flow.
  EpilogueARCBlockState() { BBSetIn = true; LocalArg = SILValue(); }
};

/// EpilogueARCContext - This class implements a data flow with which epilogue
/// retains or releases for a SILValue are found.
///
/// NOTE:
/// In case of release finder, this function assumes the SILArgument has
/// @owned semantic.
/// In case of retain finder, this class assumes Arg is one of the return value
/// of the function.
class EpilogueARCContext {
public:
  enum EpilogueARCKind { Retain = 0, Release = 1 };

private:
  // Are we finding retains or releases.
  EpilogueARCKind Kind;

  // The argument we are looking for epilogue ARC instruction for.
  SILValue Arg;

  /// The allocator we are currently using.
  llvm::SpecificBumpPtrAllocator<EpilogueARCBlockState> BPA;

  /// Current function we are analyzing.
  SILFunction *F;

  /// Current post-order we are using.
  PostOrderFunctionInfo *PO;

  /// Current alias analysis we are using.
  AliasAnalysis *AA;

  /// Current rc-identity we are using.
  RCIdentityFunctionInfo *RCFI;

  /// The epilogue retains or releases.
  llvm::SmallVector<SILInstruction *, 1> EpilogueARCInsts; 

  /// All the retain/release block state for all the basic blocks in the function. 
  llvm::DenseMap<SILBasicBlock *, EpilogueARCBlockState *> EpilogueARCBlockStates;

  /// The exit blocks of the function.
  llvm::SmallPtrSet<SILBasicBlock *, 2> ExitBlocks;

  /// Return true if this is a function exit block.
  bool isExitBlock(SILBasicBlock *BB) {
    return ExitBlocks.count(BB);
  }

  /// Return true if this is a retain instruction.
  bool isRetainInstruction(SILInstruction *II) {
    return isa<RetainValueInst>(II) || isa<StrongRetainInst>(II);
  }

  /// Return true if this is a release instruction.
  bool isReleaseInstruction(SILInstruction *II) {
    return isa<ReleaseValueInst>(II) || isa<StrongReleaseInst>(II);
  }

  SILValue getArg(SILBasicBlock *B) {
    SILValue A = EpilogueARCBlockStates[B]->LocalArg;
    if (A)
      return A;
    return Arg;
  }

public:
  /// Constructor.
  EpilogueARCContext(EpilogueARCKind Kind, SILValue Arg, SILFunction *F,
                     PostOrderFunctionInfo *PO, AliasAnalysis *AA,
                     RCIdentityFunctionInfo *RCFI)
    : Kind(Kind), Arg(Arg), F(F), PO(PO), AA(AA), RCFI(RCFI) {}

  /// Run the data flow to find the epilogue retains or releases.
  bool run() {
    // Initialize the epilogue arc data flow context.
    initializeDataflow();
    // Converge the data flow.
    convergeDataflow();
    // Lastly, find the epilogue ARC instructions.
    return computeEpilogueARC();
  }

  /// Reset the epilogue arc instructions. 
  void resetEpilogueARCInsts() { EpilogueARCInsts.clear(); }
  llvm::SmallVector<SILInstruction *, 1> getEpilogueARCInsts() {
    return EpilogueARCInsts;
  }

  /// Initialize the data flow.
  void initializeDataflow();

  /// Keep iterating until the data flow is converged.
  void convergeDataflow();

  /// Find the epilogue ARC instructions.
  bool computeEpilogueARC();

  /// This instruction prevents looking further for epilogue retains on the
  /// current path.
  bool mayBlockEpilogueRetain(SILInstruction *II, SILValue Ptr) { 
    // reference decrementing instruction prevents any retain to be identified as
    // epilogue retains.
    if (mayDecrementRefCount(II, Ptr, AA))
      return true;
    // Handle self-recursion. A self-recursion can be considered a +1 on the
    // current argument.
    if (ApplyInst *AI = dyn_cast<ApplyInst>(II))
     if (AI->getCalleeFunction() == II->getParent()->getParent())
       return true;
    return false;
  } 

  /// This instruction prevents looking further for epilogue releases on the
  /// current path.
  bool mayBlockEpilogueRelease(SILInstruction *II, SILValue Ptr) { 
    // Check whether this instruction read reference count, i.e. uniqueness
    // check. Moving release past that may result in additional COW.
   if (II->mayReleaseOrReadRefCount())
      return true;
    return false;
  } 

  /// Does this instruction block the interested ARC instruction ?
  bool mayBlockEpilogueARC(SILInstruction *II, SILValue Ptr) { 
    if (Kind == EpilogueARCKind::Retain)
      return mayBlockEpilogueRetain(II, Ptr);
    return mayBlockEpilogueRelease(II, Ptr);
  }

  /// This is the type of instructions the data flow is interested in.
  bool isInterestedInstruction(SILInstruction *II) {
    // We are checking for release.
    if (Kind == EpilogueARCKind::Release)
      return isReleaseInstruction(II) &&
             RCFI->getRCIdentityRoot(II->getOperand(0)) ==
             RCFI->getRCIdentityRoot(getArg(II->getParent()));
    // We are checking for retain. If this is a self-recursion. call
    // to the function (which returns an owned value) can be treated as
    // the retain instruction.
    if (ApplyInst *AI = dyn_cast<ApplyInst>(II))
     if (AI->getCalleeFunction() == II->getParent()->getParent())
       return true;
    // Check whether this is a retain instruction and the argument it
    // retains.
    return isRetainInstruction(II) &&
           RCFI->getRCIdentityRoot(II->getOperand(0)) ==
           RCFI->getRCIdentityRoot(getArg(II->getParent()));
  }
};

/// Compute the epilogue ARC instructions for a given SILValue. Return an
/// empty set if no epilogue ARC instructions can be found.
///
/// NOTE: This function assumes Arg is has @owned semantic.
llvm::SmallVector<SILInstruction *, 1> 
computeEpilogueARCInstructions(EpilogueARCContext::EpilogueARCKind Kind,
                               SILValue Arg, SILFunction *F,
                               PostOrderFunctionInfo *PO, AliasAnalysis *AA,
                               RCIdentityFunctionInfo *RCFI); 

} // end namespace swift


#endif
