//===--- EpilogueARCAnalysis.h ----------------------------------*- C++ -*-===//
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
//  This is an analysis that determines the ref count identity (i.e. gc root) of
//  a pointer. Any values with the same ref count identity are able to be
//  retained and released interchangeably.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SILOPTIMIZER_ANALYSIS_EPILOGUEARCANALYSIS_H
#define SWIFT_SILOPTIMIZER_ANALYSIS_EPILOGUEARCANALYSIS_H

#include "swift/SIL/SILValue.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SILOptimizer/Analysis/Analysis.h"
#include "swift/SILOptimizer/Analysis/AliasAnalysis.h"
#include "swift/SILOptimizer/Analysis/ARCAnalysis.h"
#include "swift/SILOptimizer/Analysis/PostOrderAnalysis.h"
#include "swift/SILOptimizer/Analysis/RCIdentityAnalysis.h"
#include "swift/SILOptimizer/PassManager/PassManager.h"

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
  /// Current post-order we are using.
  LazyFunctionInfo<PostOrderAnalysis, PostOrderFunctionInfo> PO;

  /// Current alias analysis we are using.
  AliasAnalysis *AA;

  /// Current rc-identity we are using.
  LazyFunctionInfo<RCIdentityAnalysis, RCIdentityFunctionInfo> RCFI;

  // All state below this line must always be cleared by the reset routine.
  //
  // Are we finding retains or releases.
  EpilogueARCKind Kind;

  // The argument we are looking for epilogue ARC instruction for.
  SILValue Arg;

  /// A map from a block's post order index to block state.
  std::vector<EpilogueARCBlockState> IndexToStateMap;

  /// The epilogue retains or releases.
  llvm::SmallSetVector<SILInstruction *, 1> EpilogueARCInsts; 

  /// The exit blocks of the function.
  llvm::SmallPtrSet<SILBasicBlock *, 2> ExitBlocks;

  EpilogueARCBlockState &getState(SILBasicBlock *BB) {
    return IndexToStateMap[*PO->getPONumber(BB)];
  }

  /// Return true if this is a function exiting block this epilogue ARC
  /// matcher is interested in. 
  bool isInterestedFunctionExitingBlock(SILBasicBlock *BB) {
    if (EpilogueARCKind::Release == Kind)  
      return BB->getTerminator()->isFunctionExiting();

    return BB->getTerminator()->isFunctionExiting() &&
           BB->getTerminator()->getTermKind() != TermKind::ThrowInst;
  }

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

  SILValue getArg(SILBasicBlock *BB) {
    SILValue A = getState(BB).LocalArg;
    if (A)
      return A;
    return Arg;
  }

public:
  /// Constructor.
  EpilogueARCContext(SILFunction *F, PostOrderAnalysis *PO, AliasAnalysis *AA,
                     RCIdentityAnalysis *RCIA)
      : PO(F, PO), AA(AA), RCFI(F, RCIA) {}

  /// Run the data flow to find the epilogue retains or releases.
  bool run(EpilogueARCKind NewKind, SILValue NewArg) {
    Kind = NewKind;
    Arg = NewArg;

    // Initialize the epilogue arc data flow context.
    initializeDataflow();
    // Converge the data flow.
    if (!convergeDataflow())
      return false;
    // Lastly, find the epilogue ARC instructions.
    return computeEpilogueARC();
  }

  /// Reset the epilogue arc instructions. 
  llvm::SmallSetVector<SILInstruction *, 1> getEpilogueARCInsts() {
    return EpilogueARCInsts;
  }

  void reset() {
    IndexToStateMap.clear();
    EpilogueARCInsts.clear();
    ExitBlocks.clear();
    EpilogueARCInsts.clear();
  }

  /// Initialize the data flow.
  void initializeDataflow();

  /// Keep iterating until the data flow is converged.
  bool convergeDataflow();

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
    if (auto *AI = dyn_cast<ApplyInst>(II))
     if (AI->getCalleeFunction() == II->getParent()->getParent())
       return true;
    return false;
  } 

  /// This instruction prevents looking further for epilogue releases on the
  /// current path.
  bool mayBlockEpilogueRelease(SILInstruction *II, SILValue Ptr) { 
    // Check whether this instruction read reference count, i.e. uniqueness
    // check. Moving release past that may result in additional COW.
    return II->mayReleaseOrReadRefCount();
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
    if (auto *AI = dyn_cast<ApplyInst>(II))
     if (AI->getCalleeFunction() == II->getParent()->getParent())
       return true;
    // Check whether this is a retain instruction and the argument it
    // retains.
    return isRetainInstruction(II) &&
           RCFI->getRCIdentityRoot(II->getOperand(0)) ==
           RCFI->getRCIdentityRoot(getArg(II->getParent()));
  }
};

/// This class is a simple wrapper around an identity cache.
class EpilogueARCFunctionInfo {
  using ARCInstructions = llvm::SmallSetVector<SILInstruction *, 1>;

  EpilogueARCContext Context;

  /// The epilogue retain cache.
  llvm::DenseMap<SILValue, ARCInstructions> EpilogueRetainInstCache;

  /// The epilogue release cache.
  llvm::DenseMap<SILValue, ARCInstructions> EpilogueReleaseInstCache;

public:
  void handleDeleteNotification(SILNode *node) {
    // Being conservative and clear everything for now.
    EpilogueRetainInstCache.clear();
    EpilogueReleaseInstCache.clear();
  }

  /// Constructor.
  EpilogueARCFunctionInfo(SILFunction *F, PostOrderAnalysis *PO,
                          AliasAnalysis *AA, RCIdentityAnalysis *RC)
      : Context(F, PO, AA, RC) {}

  /// Find the epilogue ARC instruction based on the given \p Kind and given
  /// \p Arg.
  llvm::SmallSetVector<SILInstruction *, 1>
  computeEpilogueARCInstructions(EpilogueARCContext::EpilogueARCKind Kind,
                                 SILValue Arg) {
    auto &ARCCache = Kind == EpilogueARCContext::EpilogueARCKind::Retain ?
                 EpilogueRetainInstCache :
                 EpilogueReleaseInstCache;
    auto Iter = ARCCache.find(Arg);
    if (Iter != ARCCache.end())
      return Iter->second;

    // Initialize and run the data flow. Clear the epilogue arc instructions if the
    // data flow is aborted in middle.
    if (!Context.run(Kind, Arg)) {
      Context.reset();
      return llvm::SmallSetVector<SILInstruction *, 1>();
    }

    auto Result = Context.getEpilogueARCInsts();
    Context.reset();
    ARCCache[Arg] = Result;
    return Result;
  }
};

class EpilogueARCAnalysis : public FunctionAnalysisBase<EpilogueARCFunctionInfo> {
  /// Current post order analysis we are using.
  PostOrderAnalysis *PO;
  /// Current alias analysis we are using.
  AliasAnalysis *AA;
  /// Current RC Identity analysis we are using.
  RCIdentityAnalysis *RC;

public:
  EpilogueARCAnalysis(SILModule *)
    : FunctionAnalysisBase<EpilogueARCFunctionInfo>(AnalysisKind::EpilogueARC),
      PO(nullptr), AA(nullptr), RC(nullptr) {}

  EpilogueARCAnalysis(const EpilogueARCAnalysis &) = delete;
  EpilogueARCAnalysis &operator=(const EpilogueARCAnalysis &) = delete;

  virtual void handleDeleteNotification(SILNode *node) override {
    // If the parent function of this instruction was just turned into an
    // external declaration, bail. This happens during SILFunction destruction.
    SILFunction *F = node->getFunction();
    if (F->isExternalDeclaration()) {
      return;
    }

    // If we do have an analysis, tell it to handle its delete notifications.
    if (auto A = maybeGet(F)) {
      A.get()->handleDeleteNotification(node);
    }
  }

  virtual bool needsNotifications() override { return true; }

  static bool classof(const SILAnalysis *S) {
    return S->getKind() == AnalysisKind::EpilogueARC;
  }

  virtual void initialize(SILPassManager *PM) override;
  
  virtual EpilogueARCFunctionInfo *newFunctionAnalysis(SILFunction *F) override {
    return new EpilogueARCFunctionInfo(F, PO, AA, RC);
  }

  virtual bool shouldInvalidate(SILAnalysis::InvalidationKind K) override {
    return true;
  }

 };

} // end swift namespace

#endif
