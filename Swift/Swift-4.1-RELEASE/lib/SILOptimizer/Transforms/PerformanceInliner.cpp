//===--- PerformanceInliner.cpp - Basic cost based performance inlining ---===//
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

#define DEBUG_TYPE "sil-inliner"
#include "swift/SIL/OptimizationRemark.h"
#include "swift/SILOptimizer/Analysis/SideEffectAnalysis.h"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/SILOptimizer/Utils/Devirtualize.h"
#include "swift/SILOptimizer/Utils/Generics.h"
#include "swift/SILOptimizer/Utils/PerformanceInlinerUtils.h"
#include "swift/Strings.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

using namespace swift;

STATISTIC(NumFunctionsInlined, "Number of functions inlined");

llvm::cl::opt<bool> PrintShortestPathInfo(
    "print-shortest-path-info", llvm::cl::init(false),
    llvm::cl::desc("Print shortest-path information for inlining"));

llvm::cl::opt<bool> EnableSILInliningOfGenerics(
  "sil-inline-generics", llvm::cl::init(false),
  llvm::cl::desc("Enable inlining of generics"));

//===----------------------------------------------------------------------===//
//                           Performance Inliner
//===----------------------------------------------------------------------===//

namespace {

using Weight = ShortestPathAnalysis::Weight;

class SILPerformanceInliner {
  /// Specifies which functions not to inline, based on @_semantics and
  /// global_init attributes.
  InlineSelection WhatToInline;

  DominanceAnalysis *DA;
  SILLoopAnalysis *LA;
  SideEffectAnalysis *SEA;

  // For keys of SILFunction and SILLoop.
  llvm::DenseMap<SILFunction *, ShortestPathAnalysis *> SPAs;
  llvm::SpecificBumpPtrAllocator<ShortestPathAnalysis> SPAAllocator;

  ColdBlockInfo CBI;

  OptRemark::Emitter &ORE;

  /// The following constants define the cost model for inlining. Some constants
  /// are also defined in ShortestPathAnalysis.
  enum {
    /// The base value for every call: it represents the benefit of removing the
    /// call overhead itself.
    RemovedCallBenefit = 20,

    /// The benefit if the operand of an apply gets constant, e.g. if a closure
    /// is passed to an apply instruction in the callee.
    RemovedClosureBenefit = RemovedCallBenefit + 50,

    /// The benefit if a load can (probably) eliminated because it loads from
    /// a stack location in the caller.
    RemovedLoadBenefit = RemovedCallBenefit + 5,

    /// The benefit if a store can (probably) eliminated because it stores to
    /// a stack location in the caller.
    RemovedStoreBenefit = RemovedCallBenefit + 10,

    /// The benefit if the condition of a terminator instruction gets constant
    /// due to inlining.
    RemovedTerminatorBenefit = RemovedCallBenefit + 10,

    /// The benefit if a retain/release can (probably) be eliminated after
    /// inlining.
    RefCountBenefit = RemovedCallBenefit + 20,

    /// The benefit of a onFastPath builtin.
    FastPathBuiltinBenefit = RemovedCallBenefit + 40,

    /// The benefit of being able to devirtualize a call.
    DevirtualizedCallBenefit = RemovedCallBenefit + 300,

    /// The benefit of being able to produce a generic
    /// specialization for a call.
    GenericSpecializationBenefit = RemovedCallBenefit + 300,

    /// Approximately up to this cost level a function can be inlined without
    /// increasing the code size.
    TrivialFunctionThreshold = 18,

    /// Configuration for the "soft" caller block limit.
    BlockLimitDenominator = 3000,

    /// No inlining is done if the caller has more than this number of blocks.
    OverallCallerBlockLimit = 400,

    /// The assumed execution length of a function call.
    DefaultApplyLength = 10
  };

  OptimizationMode OptMode;

#ifndef NDEBUG
  SILFunction *LastPrintedCaller = nullptr;
  void dumpCaller(SILFunction *Caller) {
    if (Caller != LastPrintedCaller) {
      llvm::dbgs() << "\nInline into caller: " << Caller->getName() << '\n';
      LastPrintedCaller = Caller;
    }
  }
#endif

  ShortestPathAnalysis *getSPA(SILFunction *F, SILLoopInfo *LI) {
    ShortestPathAnalysis *&SPA = SPAs[F];
    if (!SPA) {
      SPA = new (SPAAllocator.Allocate()) ShortestPathAnalysis(F, LI);
    }
    return SPA;
  }

  bool profileBasedDecision(
      const FullApplySite &AI, int Benefit, SILFunction *Callee, int CalleeCost,
      int &NumCallerBlocks,
      const llvm::DenseMapIterator<
          swift::SILBasicBlock *, uint64_t,
          llvm::DenseMapInfo<swift::SILBasicBlock *>,
          llvm::detail::DenseMapPair<swift::SILBasicBlock *, uint64_t>, true>
          &bbIt);

  bool isProfitableToInline(
      FullApplySite AI, Weight CallerWeight, ConstantTracker &callerTracker,
      int &NumCallerBlocks,
      const llvm::DenseMap<SILBasicBlock *, uint64_t> &BBToWeightMap);

  bool decideInWarmBlock(
      FullApplySite AI, Weight CallerWeight, ConstantTracker &callerTracker,
      int &NumCallerBlocks,
      const llvm::DenseMap<SILBasicBlock *, uint64_t> &BBToWeightMap);

  bool decideInColdBlock(FullApplySite AI, SILFunction *Callee);

  void visitColdBlocks(SmallVectorImpl<FullApplySite> &AppliesToInline,
                       SILBasicBlock *root, DominanceInfo *DT);

  void collectAppliesToInline(SILFunction *Caller,
                              SmallVectorImpl<FullApplySite> &Applies);

public:
  SILPerformanceInliner(InlineSelection WhatToInline, DominanceAnalysis *DA,
                        SILLoopAnalysis *LA, SideEffectAnalysis *SEA,
                        OptimizationMode OptMode, OptRemark::Emitter &ORE)
      : WhatToInline(WhatToInline), DA(DA), LA(LA), SEA(SEA), CBI(DA), ORE(ORE),
        OptMode(OptMode) {}

  bool inlineCallsIntoFunction(SILFunction *F);
};

} // end anonymous namespace

// Returns true if it is possible to perform a generic
// specialization for a given call.
static bool canSpecializeGeneric(ApplySite AI, SILFunction *F,
                                 SubstitutionList Subs) {
  return ReabstractionInfo::canBeSpecialized(AI, F, Subs);
}

bool SILPerformanceInliner::profileBasedDecision(
    const FullApplySite &AI, int Benefit, SILFunction *Callee, int CalleeCost,
    int &NumCallerBlocks,
    const llvm::DenseMapIterator<
        swift::SILBasicBlock *, uint64_t,
        llvm::DenseMapInfo<swift::SILBasicBlock *>,
        llvm::detail::DenseMapPair<swift::SILBasicBlock *, uint64_t>, true>
        &bbIt) {
  if (CalleeCost < TrivialFunctionThreshold) {
    // We do not increase code size below this threshold
    return true;
  }
  auto callerCount = bbIt->getSecond();
  if (callerCount < 1) {
    // Never called - do not inline
    DEBUG(dumpCaller(AI.getFunction()); llvm::dbgs()
                                        << "profiled decision: NO"
                                        << ", reason= Never Called." << '\n';);
    return false;
  }
  auto calleeCount = Callee->getEntryCount();
  if (calleeCount) {
    // If we have Callee count - use SI heuristic:
    auto calleCountVal = calleeCount.getValue();
    auto percent = (long double)callerCount / (long double)calleCountVal;
    if (percent < 0.8) {
      DEBUG(dumpCaller(AI.getFunction());
            llvm::dbgs() << "profiled decision: NO"
                         << ", reason=SI " << std::to_string(percent) << "%"
                         << '\n';);
      return false;
    }
    DEBUG(dumpCaller(AI.getFunction()); llvm::dbgs() << "profiled decision: YES"
                                                     << ", reason=SI "
                                                     << std::to_string(percent)
                                                     << "%" << '\n';);
  } else {
    // No callee count - use a "modified" aggressive IHF for now
    if (CalleeCost > Benefit && callerCount < 100) {
      DEBUG(dumpCaller(AI.getFunction());
            llvm::dbgs() << "profiled decision: NO"
                         << ", reason=IHF " << callerCount << '\n';);
      return false;
    }
    DEBUG(dumpCaller(AI.getFunction()); llvm::dbgs() << "profiled decision: YES"
                                                     << ", reason=IHF "
                                                     << callerCount << '\n';);
  }
  // We're gonna inline!
  NumCallerBlocks += Callee->size();
  return true;
}

bool SILPerformanceInliner::isProfitableToInline(
    FullApplySite AI, Weight CallerWeight, ConstantTracker &callerTracker,
    int &NumCallerBlocks,
    const llvm::DenseMap<SILBasicBlock *, uint64_t> &BBToWeightMap) {
  SILFunction *Callee = AI.getReferencedFunction();
  bool IsGeneric = !AI.getSubstitutions().empty();

  assert(EnableSILInliningOfGenerics || !IsGeneric);

  // Start with a base benefit.
  int BaseBenefit = RemovedCallBenefit;

  // Osize heuristic.
  if (OptMode == OptimizationMode::ForSize) {
    // Don't inline into thunks.
    if (AI.getFunction()->isThunk())
      return false;

    // Don't inline class methods.
    if (Callee->hasSelfParam()) {
      auto SelfTy = Callee->getLoweredFunctionType()->getSelfInstanceType();
      if (SelfTy->mayHaveSuperclass() &&
          Callee->getRepresentation() == SILFunctionTypeRepresentation::Method)
        return false;
    }

    // Use command line option to control inlining in Osize mode.
    const uint64_t CallerBaseBenefitReductionFactor = AI.getFunction()->getModule().getOptions().CallerBaseBenefitReductionFactor;
    BaseBenefit = BaseBenefit / CallerBaseBenefitReductionFactor;
  }

  // It is always OK to inline a simple call.
  // TODO: May be consider also the size of the callee?
  if (isPureCall(AI, SEA))
    return true;

  // Bail out if this generic call can be optimized by means of
  // the generic specialization, because we prefer generic specialization
  // to inlining of generics.
  if (IsGeneric && canSpecializeGeneric(AI, Callee, AI.getSubstitutions())) {
    return false;
  }

  SILLoopInfo *LI = LA->get(Callee);
  ShortestPathAnalysis *SPA = getSPA(Callee, LI);
  assert(SPA->isValid());

  ConstantTracker constTracker(Callee, &callerTracker, AI);
  DominanceInfo *DT = DA->get(Callee);
  SILBasicBlock *CalleeEntry = &Callee->front();
  DominanceOrder domOrder(CalleeEntry, DT, Callee->size());

  // Calculate the inlining cost of the callee.
  int CalleeCost = 0;
  int Benefit = 0;

  SubstitutionMap CalleeSubstMap;
  if (IsGeneric) {
    CalleeSubstMap = Callee->getLoweredFunctionType()
      ->getGenericSignature()
      ->getSubstitutionMap(AI.getSubstitutions());
  }

  CallerWeight.updateBenefit(Benefit, BaseBenefit);
  //  Benefit = 1;

  // Go through all blocks of the function, accumulate the cost and find
  // benefits.
  while (SILBasicBlock *block = domOrder.getNext()) {
    constTracker.beginBlock();
    Weight BlockW = SPA->getWeight(block, CallerWeight);

    for (SILInstruction &I : *block) {
      constTracker.trackInst(&I);

      CalleeCost += (int)instructionInlineCost(I);

      if (FullApplySite FAI = FullApplySite::isa(&I)) {
        // Check if the callee is passed as an argument. If so, increase the
        // threshold, because inlining will (probably) eliminate the closure.
        SILInstruction *def = constTracker.getDefInCaller(FAI.getCallee());
        if (def && (isa<FunctionRefInst>(def) || isa<PartialApplyInst>(def)))
          BlockW.updateBenefit(Benefit, RemovedClosureBenefit);
        // Check if inlining the callee would allow for further
        // optimizations like devirtualization or generic specialization. 
        if (!def)
          def = dyn_cast_or_null<SingleValueInstruction>(FAI.getCallee());

        if (!def)
          continue;

        auto Subs = FAI.getSubstitutions();

        // Bail if it is not a generic call or inlining of generics is forbidden.
        if (!EnableSILInliningOfGenerics || Subs.empty())
          continue;

        if (!isa<FunctionRefInst>(def) && !isa<ClassMethodInst>(def) &&
            !isa<WitnessMethodInst>(def))
          continue;

        // It is a generic call inside the callee. Check if after inlining
        // it will be possible to perform a generic specialization or
        // devirtualization of this call.

        // Create the list of substitutions as they will be after
        // inlining.
        auto Sig = FAI.getOrigCalleeType()->getGenericSignature();
        auto SubMap = Sig->getSubstitutionMap(Subs);
        SubMap = SubMap.subst(CalleeSubstMap);

        SmallVector<Substitution, 4> NewSubs;
        Sig->getSubstitutions(SubMap, NewSubs);

        // Check if the call can be devirtualized.
        if (isa<ClassMethodInst>(def) || isa<WitnessMethodInst>(def) ||
            isa<SuperMethodInst>(def)) {
          // TODO: Take AI.getSubstitutions() into account.
          if (canDevirtualizeApply(FAI, nullptr)) {
            DEBUG(llvm::dbgs() << "Devirtualization will be possible after "
                                  "inlining for the call:\n";
                  FAI.getInstruction()->dumpInContext());
            BlockW.updateBenefit(Benefit, DevirtualizedCallBenefit);
          }
        }

        // Check if a generic specialization would be possible.
        if (isa<FunctionRefInst>(def)) {
          auto CalleeF = FAI.getCalleeFunction();
          if (!canSpecializeGeneric(FAI, CalleeF, NewSubs))
            continue;
          DEBUG(llvm::dbgs() << "Generic specialization will be possible after "
                                "inlining for the call:\n";
                FAI.getInstruction()->dumpInContext());
          BlockW.updateBenefit(Benefit, GenericSpecializationBenefit);
        }
      } else if (auto *LI = dyn_cast<LoadInst>(&I)) {
        // Check if it's a load from a stack location in the caller. Such a load
        // might be optimized away if inlined.
        if (constTracker.isStackAddrInCaller(LI->getOperand()))
          BlockW.updateBenefit(Benefit, RemovedLoadBenefit);
      } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        // Check if it's a store to a stack location in the caller. Such a load
        // might be optimized away if inlined.
        if (constTracker.isStackAddrInCaller(SI->getDest()))
          BlockW.updateBenefit(Benefit, RemovedStoreBenefit);
      } else if (isa<StrongReleaseInst>(&I) || isa<ReleaseValueInst>(&I)) {
        SILValue Op = stripCasts(I.getOperand(0));
        if (auto *Arg = dyn_cast<SILFunctionArgument>(Op)) {
          if (Arg->getArgumentConvention() ==
              SILArgumentConvention::Direct_Guaranteed) {
            BlockW.updateBenefit(Benefit, RefCountBenefit);
          }
        }
      } else if (auto *BI = dyn_cast<BuiltinInst>(&I)) {
        if (BI->getBuiltinInfo().ID == BuiltinValueKind::OnFastPath)
          BlockW.updateBenefit(Benefit, FastPathBuiltinBenefit);
      }
    }
    // Don't count costs in blocks which are dead after inlining.
    SILBasicBlock *takenBlock = constTracker.getTakenBlock(block->getTerminator());
    if (takenBlock) {
      BlockW.updateBenefit(Benefit, RemovedTerminatorBenefit);
      domOrder.pushChildrenIf(block, [=](SILBasicBlock *child) {
        return child->getSinglePredecessorBlock() != block ||
               child == takenBlock;
      });
    } else {
      domOrder.pushChildren(block);
    }
  }

  if (AI.getFunction()->isThunk()) {
    // Only inline trivial functions into thunks (which will not increase the
    // code size).
    if (CalleeCost > TrivialFunctionThreshold) {
      return false;
    }

    DEBUG(
      dumpCaller(AI.getFunction());
      llvm::dbgs() << "    decision {" << CalleeCost << " into thunk} " <<
          Callee->getName() << '\n';
    );
    return true;
  }

  // We reduce the benefit if the caller is too large. For this we use a
  // cubic function on the number of caller blocks. This starts to prevent
  // inlining at about 800 - 1000 caller blocks.
  int blockMinus =
    (NumCallerBlocks * NumCallerBlocks) / BlockLimitDenominator *
                        NumCallerBlocks / BlockLimitDenominator;
  Benefit -= blockMinus;

  // If we have profile info - use it for final inlining decision.
  auto *bb = AI.getInstruction()->getParent();
  auto bbIt = BBToWeightMap.find(bb);
  if (bbIt != BBToWeightMap.end()) {
    return profileBasedDecision(AI, Benefit, Callee, CalleeCost,
                                NumCallerBlocks, bbIt);
  }

  // This is the final inlining decision.
  if (CalleeCost > Benefit) {
    ORE.emit([&]() {
      using namespace OptRemark;
      return RemarkMissed("NoInlinedCost", *AI.getInstruction())
             << "Not profitable to inline function " << NV("Callee", Callee)
             << " (cost = " << NV("Cost", CalleeCost)
             << ", benefit = " << NV("Benefit", Benefit) << ")";
    });
    return false;
  }

  NumCallerBlocks += Callee->size();

  DEBUG(
    dumpCaller(AI.getFunction());
    llvm::dbgs() << "    decision {c=" << CalleeCost << ", b=" << Benefit <<
        ", l=" << SPA->getScopeLength(CalleeEntry, 0) <<
        ", c-w=" << CallerWeight << ", bb=" << Callee->size() <<
        ", c-bb=" << NumCallerBlocks << "} " << Callee->getName() << '\n';
  );
  ORE.emit([&]() {
    using namespace OptRemark;
    return RemarkPassed("Inlined", *AI.getInstruction())
           << NV("Callee", Callee) << " inlined into "
           << NV("Caller", AI.getFunction())
           << " (cost = " << NV("Cost", CalleeCost)
           << ", benefit = " << NV("Benefit", Benefit) << ")";
  });

  return true;
}

/// Checks if a given generic apply should be inlined unconditionally, i.e.
/// without any complex analysis using e.g. a cost model.
/// It returns true if a function should be inlined.
/// It returns false if a function should not be inlined.
/// It returns None if the decision cannot be made without a more complex
/// analysis.
static Optional<bool> shouldInlineGeneric(FullApplySite AI) {
  assert(!AI.getSubstitutions().empty() &&
         "Expected a generic apply");

  SILFunction *Callee = AI.getReferencedFunction();

  // Do not inline @_semantics functions when compiling the stdlib,
  // because they need to be preserved, so that the optimizer
  // can properly optimize a user code later.
  auto ModuleName = Callee->getModule().getSwiftModule()->getName().str();
  if (Callee->hasSemanticsAttrThatStartsWith("array.") &&
      (ModuleName == STDLIB_NAME || ModuleName == SWIFT_ONONE_SUPPORT))
    return false;

  // Do not inline into thunks.
  if (AI.getFunction()->isThunk())
    return false;

  // Always inline generic functions which are marked as
  // AlwaysInline or transparent.
  if (Callee->getInlineStrategy() == AlwaysInline || Callee->isTransparent())
    return true;

  // All other generic functions should not be inlined if this kind of inlining
  // is disabled.
  if (!EnableSILInliningOfGenerics)
    return false;

  // If all substitutions are concrete, then there is no need to perform the
  // generic inlining. Let the generic specializer create a specialized
  // function and then decide if it is beneficial to inline it.
  if (!hasArchetypes(AI.getSubstitutions()))
    return false;

  // It is not clear yet if this function should be decided or not.
  return None;
}

bool SILPerformanceInliner::decideInWarmBlock(
    FullApplySite AI, Weight CallerWeight, ConstantTracker &callerTracker,
    int &NumCallerBlocks,
    const llvm::DenseMap<SILBasicBlock *, uint64_t> &BBToWeightMap) {
  if (!AI.getSubstitutions().empty()) {
    // Only inline generics if definitively clear that it should be done.
    auto ShouldInlineGeneric = shouldInlineGeneric(AI);
    if (ShouldInlineGeneric.hasValue())
      return ShouldInlineGeneric.getValue();
  }

  SILFunction *Callee = AI.getReferencedFunction();

  if (Callee->getInlineStrategy() == AlwaysInline || Callee->isTransparent()) {
    DEBUG(
      dumpCaller(AI.getFunction());
      llvm::dbgs() << "    always-inline decision " << Callee->getName() << '\n';
    );
    return true;
  }

  return isProfitableToInline(AI, CallerWeight, callerTracker, NumCallerBlocks,
                              BBToWeightMap);
}

/// Return true if inlining this call site into a cold block is profitable.
bool SILPerformanceInliner::decideInColdBlock(FullApplySite AI,
                                              SILFunction *Callee) {
  if (!AI.getSubstitutions().empty()) {
    // Only inline generics if definitively clear that it should be done.
    auto ShouldInlineGeneric = shouldInlineGeneric(AI);
    if (ShouldInlineGeneric.hasValue())
      return ShouldInlineGeneric.getValue();

    return false;
  }

  if (Callee->getInlineStrategy() == AlwaysInline || Callee->isTransparent()) {
    DEBUG(
      dumpCaller(AI.getFunction());
      llvm::dbgs() << "    always-inline decision " << Callee->getName() << '\n';
      );
    return true;
  }

  int CalleeCost = 0;

  for (SILBasicBlock &Block : *Callee) {
    for (SILInstruction &I : Block) {
      CalleeCost += int(instructionInlineCost(I));
      if (CalleeCost > TrivialFunctionThreshold)
        return false;
    }
  }
  DEBUG(
    dumpCaller(AI.getFunction());
    llvm::dbgs() << "    cold decision {" << CalleeCost << "} " <<
              Callee->getName() << '\n';
  );
  return true;
}

/// Record additional weight increases.
///
/// Why can't we just add the weight when we call isProfitableToInline? Because
/// the additional weight is for _another_ function than the current handled
/// callee.
static void addWeightCorrection(FullApplySite FAS,
                        llvm::DenseMap<FullApplySite, int> &WeightCorrections) {
  SILFunction *Callee = FAS.getReferencedFunction();
  if (Callee && Callee->hasSemanticsAttr("array.uninitialized")) {
    // We want to inline the argument to an array.uninitialized call, because
    // this argument is most likely a call to a function which contains the
    // buffer allocation for the array. It is essential to inline it for stack
    // promotion of the array buffer.
    SILValue BufferArg = FAS.getArgument(0);
    SILValue Base = stripValueProjections(stripCasts(BufferArg));
    if (auto BaseApply = FullApplySite::isa(Base))
      WeightCorrections[BaseApply] += 6;
  }
}

static bool containsWeight(TermInst *inst) {
  for (auto &succ : inst->getSuccessors()) {
    if (succ.getCount()) {
      return true;
    }
  }
  return false;
}

static void
addToBBCounts(llvm::DenseMap<SILBasicBlock *, uint64_t> &BBToWeightMap,
              uint64_t numToAdd, swift::TermInst *termInst) {
  for (auto &succ : termInst->getSuccessors()) {
    auto *currBB = succ.getBB();
    assert(BBToWeightMap.find(currBB) != BBToWeightMap.end() &&
           "Expected to find block in map");
    BBToWeightMap[currBB] += numToAdd;
  }
}

static void
calculateBBWeights(SILFunction *Caller, DominanceInfo *DT,
                   llvm::DenseMap<SILBasicBlock *, uint64_t> &BBToWeightMap) {
  auto entryCount = Caller->getEntryCount();
  if (!entryCount) {
    // No profile for function - return
    return;
  }
  // Add all blocks to BBToWeightMap without count 0
  for (auto &block : Caller->getBlocks()) {
    BBToWeightMap[&block] = 0;
  }
  BBToWeightMap[Caller->getEntryBlock()] = entryCount.getValue();
  DominanceOrder domOrder(&Caller->front(), DT, Caller->size());
  while (SILBasicBlock *block = domOrder.getNext()) {
    auto bbIt = BBToWeightMap.find(block);
    assert(bbIt != BBToWeightMap.end() && "Expected to find block in map");
    auto bbCount = bbIt->getSecond();
    auto *termInst = block->getTerminator();
    if (containsWeight(termInst)) {
      // Instruction already contains accurate counters - use them as-is
      uint64_t countSum = 0;
      uint64_t blocksWithoutCount = 0;
      for (auto &succ : termInst->getSuccessors()) {
        auto *currBB = succ.getBB();
        assert(BBToWeightMap.find(currBB) != BBToWeightMap.end() &&
               "Expected to find block in map");
        auto currCount = succ.getCount();
        if (!currCount) {
          ++blocksWithoutCount;
          continue;
        }
        auto currCountVal = currCount.getValue();
        countSum += currCountVal;
        BBToWeightMap[currBB] += currCountVal;
      }
      if (countSum < bbCount) {
        // inaccurate profile - fill in the gaps for BBs without a count:
        if (blocksWithoutCount > 0) {
          auto numToAdd = (bbCount - countSum) / blocksWithoutCount;
          for (auto &succ : termInst->getSuccessors()) {
            auto *currBB = succ.getBB();
            auto currCount = succ.getCount();
            if (!currCount) {
              BBToWeightMap[currBB] += numToAdd;
            }
          }
        }
      } else {
        auto numOfSucc = termInst->getSuccessors().size();
        assert(numOfSucc > 0 && "Expected successors > 0");
        auto numToAdd = (countSum - bbCount) / numOfSucc;
        addToBBCounts(BBToWeightMap, numToAdd, termInst);
      }
    } else {
      // Fill counters speculatively
      auto numOfSucc = termInst->getSuccessors().size();
      if (numOfSucc == 0) {
        // No successors to fill
        continue;
      }
      auto numToAdd = bbCount / numOfSucc;
      addToBBCounts(BBToWeightMap, numToAdd, termInst);
    }
    domOrder.pushChildrenIf(block, [&](SILBasicBlock *child) { return true; });
  }
}

void SILPerformanceInliner::collectAppliesToInline(
    SILFunction *Caller, SmallVectorImpl<FullApplySite> &Applies) {
  DominanceInfo *DT = DA->get(Caller);
  SILLoopInfo *LI = LA->get(Caller);

  llvm::DenseMap<FullApplySite, int> WeightCorrections;

  // Compute the shortest-path analysis for the caller.
  ShortestPathAnalysis *SPA = getSPA(Caller, LI);
  SPA->analyze(CBI, [&](FullApplySite FAS) -> int {
  
    // This closure returns the length of a called function.

    // At this occasion we record additional weight increases.
    addWeightCorrection(FAS, WeightCorrections);

    if (SILFunction *Callee = getEligibleFunction(FAS, WhatToInline)) {
      // Compute the shortest-path analysis for the callee.
      SILLoopInfo *CalleeLI = LA->get(Callee);
      ShortestPathAnalysis *CalleeSPA = getSPA(Callee, CalleeLI);
      if (!CalleeSPA->isValid()) {
        CalleeSPA->analyze(CBI, [](FullApplySite FAS) {
          // We don't compute SPA for another call-level. Functions called from
          // the callee are assumed to have DefaultApplyLength.
          return DefaultApplyLength;
        });
      }
      int CalleeLength = CalleeSPA->getScopeLength(&Callee->front(), 0);
      // Just in case the callee is a noreturn function.
      if (CalleeLength >= ShortestPathAnalysis::InitialDist)
        return DefaultApplyLength;
      return CalleeLength;
    }
    // Some unknown function.
    return DefaultApplyLength;
  });

#ifndef NDEBUG
  if (PrintShortestPathInfo) {
    SPA->dump();
  }
#endif

  ConstantTracker constTracker(Caller);
  DominanceOrder domOrder(&Caller->front(), DT, Caller->size());
  int NumCallerBlocks = (int)Caller->size();

  llvm::DenseMap<SILBasicBlock *, uint64_t> BBToWeightMap;
  calculateBBWeights(Caller, DT, BBToWeightMap);

  // Go through all instructions and find candidates for inlining.
  // We do this in dominance order for the constTracker.
  SmallVector<FullApplySite, 8> InitialCandidates;
  while (SILBasicBlock *block = domOrder.getNext()) {
    constTracker.beginBlock();
    Weight BlockWeight;

    for (auto I = block->begin(), E = block->end(); I != E; ++I) {
      constTracker.trackInst(&*I);

      if (!FullApplySite::isa(&*I))
        continue;

      FullApplySite AI = FullApplySite(&*I);

      auto *Callee = getEligibleFunction(AI, WhatToInline);
      if (Callee) {
        if (!BlockWeight.isValid())
          BlockWeight = SPA->getWeight(block, Weight(0, 0));

        // The actual weight including a possible weight correction.
        Weight W(BlockWeight, WeightCorrections.lookup(AI));

        if (decideInWarmBlock(AI, W, constTracker, NumCallerBlocks,
                              BBToWeightMap))
          InitialCandidates.push_back(AI);
      }
    }
    if (NumCallerBlocks > OverallCallerBlockLimit)
      break;

    domOrder.pushChildrenIf(block, [&] (SILBasicBlock *child) {
      if (CBI.isSlowPath(block, child)) {
        // Handle cold blocks separately.
        visitColdBlocks(InitialCandidates, child, DT);
        return false;
      }
      return true;
    });
  }

  // Calculate how many times a callee is called from this caller.
  llvm::DenseMap<SILFunction *, unsigned> CalleeCount;
  for (auto AI : InitialCandidates) {
    SILFunction *Callee = AI.getReferencedFunction();
    assert(Callee && "apply_inst does not have a direct callee anymore");
    CalleeCount[Callee]++;
  }

  // Now copy each candidate callee that has a small enough number of
  // call sites into the final set of call sites.
  for (auto AI : InitialCandidates) {
    SILFunction *Callee = AI.getReferencedFunction();
    assert(Callee && "apply_inst does not have a direct callee anymore");

    const unsigned CallsToCalleeThreshold = 1024;
    if (CalleeCount[Callee] <= CallsToCalleeThreshold) {
      Applies.push_back(AI);
    }
  }
}

/// \brief Attempt to inline all calls smaller than our threshold.
/// returns True if a function was inlined.
bool SILPerformanceInliner::inlineCallsIntoFunction(SILFunction *Caller) {
  // Don't optimize functions that are marked with the opt.never attribute.
  if (!Caller->shouldOptimize())
    return false;

  // First step: collect all the functions we want to inline.  We
  // don't change anything yet so that the dominator information
  // remains valid.
  SmallVector<FullApplySite, 8> AppliesToInline;
  collectAppliesToInline(Caller, AppliesToInline);

  if (AppliesToInline.empty())
    return false;

  // Second step: do the actual inlining.
  for (auto AI : AppliesToInline) {
    SILFunction *Callee = AI.getReferencedFunction();
    assert(Callee && "apply_inst does not have a direct callee anymore");

    if (!Callee->shouldOptimize()) {
      continue;
    }
    
    SmallVector<SILValue, 8> Args;
    for (const auto &Arg : AI.getArguments())
      Args.push_back(Arg);

    DEBUG(
      dumpCaller(Caller);
      llvm::dbgs() << "    inline [" << Callee->size() << "->" <<
          Caller->size() << "] " << Callee->getName() << "\n";
    );

    SILOpenedArchetypesTracker OpenedArchetypesTracker(Caller);
    Caller->getModule().registerDeleteNotificationHandler(&OpenedArchetypesTracker);
    // The callee only needs to know about opened archetypes used in
    // the substitution list.
    OpenedArchetypesTracker.registerUsedOpenedArchetypes(AI.getInstruction());

    SILInliner Inliner(*Caller, *Callee,
                       SILInliner::InlineKind::PerformanceInline,
                       AI.getSubstitutions(),
                       OpenedArchetypesTracker);

    // We've already determined we should be able to inline this, so
    // unconditionally inline the function.
    //
    // If for whatever reason we can not inline this function, inlineFunction
    // will assert, so we are safe making this assumption.
    Inliner.inlineFunction(AI, Args);
    recursivelyDeleteTriviallyDeadInstructions(AI.getInstruction(), true);

    NumFunctionsInlined++;
  }

  return true;
}

// Find functions in cold blocks which are forced to be inlined.
// All other functions are not inlined in cold blocks.
void SILPerformanceInliner::visitColdBlocks(
    SmallVectorImpl<FullApplySite> &AppliesToInline, SILBasicBlock *Root,
    DominanceInfo *DT) {
  DominanceOrder domOrder(Root, DT);
  while (SILBasicBlock *block = domOrder.getNext()) {
    for (SILInstruction &I : *block) {
      auto *AI = dyn_cast<ApplyInst>(&I);
      if (!AI)
        continue;

      auto *Callee = getEligibleFunction(AI, WhatToInline);
      if (Callee && decideInColdBlock(AI, Callee)) {
        AppliesToInline.push_back(AI);
      }
    }
    domOrder.pushChildren(block);
  }
}


//===----------------------------------------------------------------------===//
//                          Performance Inliner Pass
//===----------------------------------------------------------------------===//

namespace {
class SILPerformanceInlinerPass : public SILFunctionTransform {
  /// Specifies which functions not to inline, based on @_semantics and
  /// global_init attributes.
  InlineSelection WhatToInline;
  std::string PassName;

public:
  SILPerformanceInlinerPass(InlineSelection WhatToInline, StringRef LevelName):
    WhatToInline(WhatToInline), PassName(LevelName) {
    PassName.append(" Performance Inliner");
  }

  void run() override {
    DominanceAnalysis *DA = PM->getAnalysis<DominanceAnalysis>();
    SILLoopAnalysis *LA = PM->getAnalysis<SILLoopAnalysis>();
    SideEffectAnalysis *SEA = PM->getAnalysis<SideEffectAnalysis>();
    OptRemark::Emitter ORE(DEBUG_TYPE, getFunction()->getModule());

    if (getOptions().InlineThreshold == 0) {
      return;
    }

    auto OptMode = getFunction()->getEffectiveOptimizationMode();

    SILPerformanceInliner Inliner(WhatToInline, DA, LA, SEA, OptMode, ORE);

    assert(getFunction()->isDefinition() &&
           "Expected only functions with bodies!");

    // Inline things into this function, and if we do so invalidate
    // analyses for this function and restart the pipeline so that we
    // can further optimize this function before attempting to inline
    // in it again.
    if (Inliner.inlineCallsIntoFunction(getFunction())) {
      invalidateAnalysis(SILAnalysis::InvalidationKind::FunctionBody);
      restartPassPipeline();
    }
  }

};
} // end anonymous namespace

/// Create an inliner pass that does not inline functions that are marked with
/// the @_semantics, @effects or global_init attributes.
SILTransform *swift::createEarlyInliner() {
  return new SILPerformanceInlinerPass(
    InlineSelection::NoSemanticsAndGlobalInit, "Early");
}

/// Create an inliner pass that does not inline functions that are marked with
/// the global_init attribute or have an "availability" semantics attribute.
SILTransform *swift::createPerfInliner() {
  return new SILPerformanceInlinerPass(InlineSelection::NoGlobalInit, "Middle");
}

/// Create an inliner pass that inlines all functions that are marked with
/// the @_semantics, @effects or global_init attributes.
SILTransform *swift::createLateInliner() {
  return new SILPerformanceInlinerPass(InlineSelection::Everything, "Late");
}
