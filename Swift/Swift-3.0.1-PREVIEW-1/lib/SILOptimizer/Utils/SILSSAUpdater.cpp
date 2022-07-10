//===--- SILSSAUpdater.cpp - Unstructured SSA Update Tool -----------------===//
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

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/SSAUpdaterImpl.h"
#include "swift/Basic/Malloc.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/SILBasicBlock.h"
#include "swift/SIL/SILBuilder.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILUndef.h"
#include "swift/SILOptimizer/Utils/CFG.h"
#include "swift/SILOptimizer/Utils/SILSSAUpdater.h"


using namespace swift;

using AvailableValsTy = llvm::DenseMap<SILBasicBlock *, SILValue>;

static AvailableValsTy &getAvailVals(void *AV) {
  return *static_cast<AvailableValsTy *>(AV);
}

void *SILSSAUpdater::allocate(unsigned Size, unsigned Align) const {
  return AlignedAlloc(Size, Align);
}

void SILSSAUpdater::deallocateSentinel(SILUndef *D) {
  AlignedFree(D);
}

SILSSAUpdater::SILSSAUpdater(SmallVectorImpl<SILArgument *> *PHIs)
    : AV(nullptr), PHISentinel(nullptr, deallocateSentinel),
      InsertedPHIs(PHIs) {}

SILSSAUpdater::~SILSSAUpdater() {
  delete static_cast<AvailableValsTy *>(AV);
}

void SILSSAUpdater::Initialize(SILType Ty) {
  ValType = Ty;

  PHISentinel = std::unique_ptr<SILUndef, void (*)(SILUndef *)>(
      SILUndef::getSentinelValue(Ty, this), SILSSAUpdater::deallocateSentinel);

  if (!AV)
    AV = new AvailableValsTy();
  else
    getAvailVals(AV).clear();
}

bool SILSSAUpdater::HasValueForBlock(SILBasicBlock *BB) const {
  return getAvailVals(AV).count(BB);
}

/// Indicate that a rewritten value is available in the specified block with the
/// specified value.
void SILSSAUpdater::AddAvailableValue(SILBasicBlock *BB, SILValue V) {
  getAvailVals(AV)[BB] = V;
}

/// Construct SSA form, materializing a value that is live at the end of the
/// specified block.
SILValue SILSSAUpdater::GetValueAtEndOfBlock(SILBasicBlock *BB) {
  return GetValueAtEndOfBlockInternal(BB);
}

/// Are all available values identicalTo each other.
bool areIdentical(AvailableValsTy &Avails) {
  auto *First = dyn_cast<SILInstruction>(Avails.begin()->second);
  if (!First)
    return false;
  for (auto Avail : Avails) {
    auto *Inst = dyn_cast<SILInstruction>(Avail.second);
    if (!Inst)
      return false;
    if (!Inst->isIdenticalTo(First))
      return false;
  }
  return true;
}

/// This should be called in top-down order of each def that needs its uses
/// rewrited. The order that we visit uses for a given def is irrelevant.
void SILSSAUpdater::RewriteUse(Operand &Op) {
  // Replicate function_refs to their uses. SILGen can't build phi nodes for
  // them and it would not make much sense anyways.
  if (auto *FR = dyn_cast<FunctionRefInst>(Op.get())) {
    assert(areIdentical(getAvailVals(AV)) &&
           "The function_refs need to have the same value");
    SILInstruction *User = Op.getUser();
    auto *NewFR = FR->clone(User);
    Op.set(NewFR);
    return;
  } else if (auto *IL = dyn_cast<IntegerLiteralInst>(Op.get()))
    if (areIdentical(getAvailVals(AV))) {
      // Some llvm intrinsics don't like phi nodes as their constant inputs (e.g
      // ctlz).
      SILInstruction *User = Op.getUser();
      auto *NewIL = IL->clone(User);
      Op.set(NewIL);
      return;
    }

  // Again we need to be careful here, because ssa construction (with the
  // existing representation) can change the operand from under us.
  UseWrapper UW(&Op);

  SILInstruction *User = Op.getUser();
  SILValue NewVal = GetValueInMiddleOfBlock(User->getParent());
  assert(NewVal && "Need a valid value");
 ((Operand *)UW)->set((SILValue)NewVal);
}


/// Get the edge values from the terminator to the destination basic block.
static OperandValueArrayRef getEdgeValuesForTerminator(TermInst *TI,
                                                       SILBasicBlock *ToBB) {
  if (auto *BrInst = dyn_cast<BranchInst>(TI)) {
    assert(BrInst->getDestBB() == ToBB &&
           "Incoming edge block and phi block mismatch");
    return BrInst->getArgs();
  }
  if (auto *CondBrInst = dyn_cast<CondBranchInst>(TI)) {
    bool IsTrueEdge = CondBrInst->getTrueBB() == ToBB;
    assert(((IsTrueEdge && CondBrInst->getTrueBB() == ToBB) ||
            CondBrInst->getFalseBB() == ToBB) &&
           "Incoming edge block and phi block mismatch");
    return IsTrueEdge ? CondBrInst->getTrueArgs() : CondBrInst->getFalseArgs();
  }

  // We need a predecessor who is capable of holding outgoing branch
  // arguments.
  llvm_unreachable("Unrecognized terminator leading to phi block");
}

/// Check that the argument has the same incoming edge values as the value
/// map.
static bool
isEquivalentPHI(SILArgument *PHI,
                llvm::SmallDenseMap<SILBasicBlock *, SILValue, 8> &ValueMap) {
  SILBasicBlock *PhiBB = PHI->getParent();
  size_t Idx = PHI->getIndex();
  for (auto *PredBB : PhiBB->getPreds()) {
    auto DesiredVal = ValueMap[PredBB];
    OperandValueArrayRef EdgeValues =
        getEdgeValuesForTerminator(PredBB->getTerminator(), PhiBB);
    if (EdgeValues[Idx] != DesiredVal)
      return false;
  }
  return true;
}

SILValue SILSSAUpdater::GetValueInMiddleOfBlock(SILBasicBlock *BB) {
  // If this basic block does not define a value we can just use the value
  // live at the end of the block.
  if (!HasValueForBlock(BB))
    return GetValueAtEndOfBlock(BB);

  /// Otherwise, we have to build SSA for the value defined in this block and
  /// this block's predecessors.
  SILValue SingularValue;
  SmallVector<std::pair<SILBasicBlock*, SILValue>, 4> PredVals;
  bool FirstPred = true;

  // SSAUpdater can modify TerminatorInst and therefore invalidate the
  // predecessor iterator. Find all the predecessors before the SSA update.
  SmallVector<SILBasicBlock *, 4> Preds;
  for (auto *PredBB: BB->getPreds()) {
    Preds.push_back(PredBB);
  }

  for (auto *PredBB : Preds) {
    SILValue PredVal = GetValueAtEndOfBlock(PredBB);
    PredVals.push_back(std::make_pair(PredBB, PredVal));
    if (FirstPred) {
      SingularValue = PredVal;
      FirstPred = false;
    } else if (SingularValue != PredVal)
      SingularValue = SILValue();
  }

  // Return undef for blocks with predecessor.
  if (PredVals.empty())
    return SILUndef::get(ValType, BB->getModule());

  if (SingularValue)
    return SingularValue;

  // Check if we already have an equivalent phi.
  if (!BB->getBBArgs().empty()) {
    llvm::SmallDenseMap<SILBasicBlock *, SILValue, 8> ValueMap(PredVals.begin(),
                                                               PredVals.end());
    for (auto *Arg : BB->getBBArgs())
      if (isEquivalentPHI(Arg, ValueMap))
        return Arg;

  }

  // Create a new phi node.
  SILArgument *PHI(new (BB->getModule()) SILArgument(BB, ValType));
  for (auto &EV : PredVals)
    addNewEdgeValueToBranch(EV.first->getTerminator(), BB, EV.second);

  if (InsertedPHIs)
    InsertedPHIs->push_back(PHI);

  return PHI;
}

/// SSAUpdaterTraits<MachineSSAUpdater> - Traits for the SSAUpdaterImpl
/// template, specialized for MachineSSAUpdater.
namespace llvm {
template<>
class SSAUpdaterTraits<SILSSAUpdater> {
public:
  typedef SILBasicBlock BlkT;
  typedef SILValue ValT;
  typedef SILArgument PhiT;

  typedef SILBasicBlock::succ_iterator BlkSucc_iterator;
  static BlkSucc_iterator BlkSucc_begin(BlkT *BB) { return BB->succ_begin(); }
  static BlkSucc_iterator BlkSucc_end(BlkT *BB) { return BB->succ_end(); }

  /// Iterator over the arguments (phis) of a basic block.
  /// Defines an implicit cast operator on the iterator. So that this iterator
  /// can be used in the SSAUpdaterImpl.
  class PhiIt {
  private:
    SILBasicBlock::bbarg_iterator It;
  public:
    explicit PhiIt(SILBasicBlock *B) // begin iterator
        : It(B->bbarg_begin()) {}
    PhiIt(SILBasicBlock *B, bool) // end iterator
        : It(B->bbarg_end()) {}
    PhiIt &operator++() { ++It; return *this; }

    operator SILArgument *() { return *It; }
    bool operator==(const PhiIt& x) const { return It == x.It; }
    bool operator!=(const PhiIt& x) const { return !operator==(x); }
  };

  typedef PhiIt PhiItT;
  static PhiItT PhiItT_begin(BlkT *BB) { return PhiIt(BB); }
  static PhiItT PhiItT_end(BlkT *BB) { return PhiIt(BB, true); }

  /// Iterator for PHI operands.
  class PHI_iterator {
  private:
    SILBasicBlock::pred_iterator PredIt;
    SILBasicBlock *BB;
    size_t Idx;

  public:
    explicit PHI_iterator(SILArgument *P) // begin iterator
        : PredIt(P->getParent()->pred_begin()),
          BB(P->getParent()),
          Idx(P->getIndex()) {}
    PHI_iterator(SILArgument *P, bool) // end iterator
        : PredIt(P->getParent()->pred_end()),
          BB(P->getParent()),
          Idx(P->getIndex()) {}

    PHI_iterator &operator++() { ++PredIt; return *this; }
    bool operator==(const PHI_iterator& x) const { return PredIt == x.PredIt; }
    bool operator!=(const PHI_iterator& x) const { return !operator==(x); }

    SILValue getValueForBlock(size_t Idx, SILBasicBlock *BB, TermInst *TI) {
      OperandValueArrayRef Args = getEdgeValuesForTerminator(TI, BB);
      assert(Idx < Args.size() && "Not enough values on incoming edge");
      return Args[Idx];
    }

    SILValue getIncomingValue() {
      return getValueForBlock(Idx, BB, (*PredIt)->getTerminator());
    }

    SILBasicBlock *getIncomingBlock() {
      return *PredIt;
    }
  };

  static inline PHI_iterator PHI_begin(PhiT *PHI) { return PHI_iterator(PHI); }
  static inline PHI_iterator PHI_end(PhiT *PHI) {
    return PHI_iterator(PHI, true);
  }

  /// Put the predecessors of BB into the Preds vector.
  static void FindPredecessorBlocks(SILBasicBlock *BB,
                                    SmallVectorImpl<SILBasicBlock*> *Preds){
    for (SILBasicBlock::pred_iterator PI = BB->pred_begin(), E = BB->pred_end();
         PI != E; ++PI)
      Preds->push_back(*PI);
  }

  static SILValue GetUndefVal(SILBasicBlock *BB,
                              SILSSAUpdater *Updater) {
    return SILUndef::get(Updater->ValType, &BB->getModule());
  }

  /// Add an Argument to the basic block.
  static SILValue CreateEmptyPHI(SILBasicBlock *BB, unsigned NumPreds,
                                 SILSSAUpdater *Updater) {
    // Add the argument to the block.
    SILValue PHI(new (BB->getModule()) SILArgument(BB, Updater->ValType));

    // Mark all predecessor blocks with the sentinel undef value.
    SmallVector<SILBasicBlock*, 4> Preds(BB->pred_begin(), BB->pred_end());
    for (auto *PredBB: Preds) {
      TermInst *TI = PredBB->getTerminator();
      addNewEdgeValueToBranch(TI, BB, Updater->PHISentinel.get());
    }
    return PHI;
  }

  /// Add the specified value as an operand of the PHI for the specified
  /// predecessor block.
  static void AddPHIOperand(SILArgument *PHI, SILValue Val,
                            SILBasicBlock *Pred) {
    auto *PHIBB = PHI->getParent();
    size_t PhiIdx = PHI->getIndex();
    auto *TI = Pred->getTerminator();
    changeEdgeValue(TI, PHIBB, PhiIdx, Val);
  }

  /// InstrIsPHI - Check if an instruction is a PHI.
  ///
  static SILArgument *InstrIsPHI(ValueBase *I) {
    SILArgument *Res = dyn_cast<SILArgument>(I);
    return Res;
  }

  /// ValueIsPHI - Check if the instruction that defines the specified register
  /// is a PHI instruction.
  static SILArgument *ValueIsPHI(SILValue V, SILSSAUpdater *Updater) {
    return InstrIsPHI(V);
  }

  /// Like ValueIsPHI but also check if the PHI has no source
  /// operands, i.e., it was just added.
  static SILArgument *ValueIsNewPHI(SILValue Val, SILSSAUpdater *Updater) {
    SILArgument *PHI = ValueIsPHI(Val, Updater);
    if (PHI) {
      auto *PhiBB = PHI->getParent();
      size_t PhiIdx = PHI->getIndex();

      // If all predecessor edges are 'not set' this is a new phi.
      for (auto *PredBB : PhiBB->getPreds()) {
        OperandValueArrayRef Edges =
            getEdgeValuesForTerminator(PredBB->getTerminator(), PhiBB);

        assert(PhiIdx < Edges.size() && "Not enough edges!");

        SILValue V = Edges[PhiIdx];
        // Check for the 'not set' sentinel.
        if (V != Updater->PHISentinel.get())
          return nullptr;
      }
      return PHI;
    }
    return nullptr;
  }

  static SILValue GetPHIValue(SILArgument *PHI) {
    return PHI;
  }
};

} // End llvm namespace

/// Check to see if AvailableVals has an entry for the specified BB and if so,
/// return it.  If not, construct SSA form by first calculating the required
/// placement of PHIs and then inserting new PHIs where needed.
SILValue SILSSAUpdater::GetValueAtEndOfBlockInternal(SILBasicBlock *BB){
  AvailableValsTy &AvailableVals = getAvailVals(AV);
  auto AI = AvailableVals.find(BB);
  if (AI != AvailableVals.end())
    return AI->second;

  llvm::SSAUpdaterImpl<SILSSAUpdater> Impl(this, &AvailableVals, InsertedPHIs);
  return Impl.GetValue(BB);
}

/// \brief Construct a use wrapper. For branches we store information so that we
/// can reconstruct the use after the branch has been modified.
///
/// When a branch is modified existing pointers to the operand
/// (ValueUseIterator) become invalid as they point to freed operands.  Instead
/// we store the branch's parent and the idx so that we can reconstruct the use.
UseWrapper::UseWrapper(Operand *Use) {
  U = nullptr;
  Type = kRegularUse;

  SILInstruction *User = Use->getUser();

  // Direct branch user.
  if (auto *Br = dyn_cast<BranchInst>(User)) {
    auto Opds = User->getAllOperands();
    for (unsigned i = 0, e = Opds.size(); i != e; ++i) {
      if (Use == &Opds[i]) {
        Idx = i;
        Type = kBranchUse;
        Parent = Br->getParent();
        return;
      }
    }
  }

  // Conditional branch user.
  if (auto *Br = dyn_cast<CondBranchInst>(User)) {
    auto Opds = User->getAllOperands();
    auto NumTrueArgs = Br->getTrueArgs().size();
    for (unsigned i = 0, e = Opds.size(); i != e; ++i) {
      if (Use == &Opds[i]) {
        // We treat the condition as part of the true args.
        if (i < NumTrueArgs + 1) {
          Idx = i;
          Type = kCondBranchUseTrue;
        } else {
          Idx = i - NumTrueArgs - 1;
          Type = kCondBranchUseFalse;
        }
        Parent = Br->getParent();
        return;
      }
    }
  }

  U = Use;
}

/// Return the operand we wrap. Reconstructing branch operands.
UseWrapper::operator Operand *() {
  switch (Type) {
  case kRegularUse:
    return U;

  case kBranchUse: {
    auto *Br = cast<BranchInst>(Parent->getTerminator());
    assert(Idx < Br->getNumArgs());
    return &Br->getAllOperands()[Idx];
  }

  case kCondBranchUseTrue:
  case kCondBranchUseFalse: {
    auto *Br = cast<CondBranchInst>(Parent->getTerminator());
    unsigned IdxToUse =
        Type == kCondBranchUseTrue ? Idx : Br->getTrueArgs().size() + 1 + Idx;
    assert(IdxToUse < Br->getAllOperands().size());
    return &Br->getAllOperands()[IdxToUse];
  }
  }

  llvm_unreachable("uninitialize use type");
}

/// At least one value feeding the specified SILArgument is a Struct. Attempt to
/// replace the Argument with a new Struct in the same block.
///
/// When we handle more types of casts, this can become a template.
///
/// ArgValues are the values feeding the specified Argument from each
/// predecessor. They must be listed in order of Arg->getParent()->getPreds().
static StructInst *replaceBBArgWithStruct(
  SILArgument *Arg,
  SmallVectorImpl<SILValue> &ArgValues) {

  SILBasicBlock *PhiBB = Arg->getParent();
  auto *FirstSI = dyn_cast<StructInst>(ArgValues[0]);
  if (!FirstSI)
    return nullptr;

  // Collect the BBArg index of each struct oper.
  // e.g.
  //   struct(A, B)
  //   br (B, A)
  // : ArgIdxForOper => {1, 0}
  SmallVector<unsigned, 4> ArgIdxForOper;
  for (unsigned OperIdx : indices(FirstSI->getElements())) {
    bool FoundMatchingArgIdx = false;
    for (unsigned ArgIdx : indices(PhiBB->getBBArgs())) {
      SmallVectorImpl<SILValue>::const_iterator AVIter = ArgValues.begin();
      bool TryNextArgIdx = false;
      for (SILBasicBlock *PredBB : PhiBB->getPreds()) {
        // All argument values must be StructInst.
        auto *PredSI = dyn_cast<StructInst>(*AVIter++);
        if (!PredSI)
          return nullptr;
        OperandValueArrayRef EdgeValues =
          getEdgeValuesForTerminator(PredBB->getTerminator(), PhiBB);
        if (EdgeValues[ArgIdx] != PredSI->getElements()[OperIdx]) {
          TryNextArgIdx = true;
          break;
        }
      }
      if (!TryNextArgIdx) {
        assert(AVIter == ArgValues.end() && "# ArgValues does not match # BB preds");
        FoundMatchingArgIdx = true;
        ArgIdxForOper.push_back(ArgIdx);
        break;
      }
    }
    if (!FoundMatchingArgIdx)
      return nullptr;
  }

  SmallVector<SILValue, 4> StructArgs;
  for (auto ArgIdx : ArgIdxForOper)
    StructArgs.push_back(PhiBB->getBBArg(ArgIdx));

  SILBuilder Builder(PhiBB, PhiBB->begin());
  return Builder.createStruct(cast<StructInst>(ArgValues[0])->getLoc(),
                              Arg->getType(), StructArgs);
}

/// Canonicalize BB arguments, replacing argument-of-casts with
/// cast-of-arguments. This only eliminates existing arguments, replacing them
/// with casts. No new arguments are created. This allows downstream pattern
/// detection like induction variable analysis to succeed.
///
/// If Arg is replaced, return the cast instruction. Otherwise return nullptr.
SILInstruction *swift::replaceBBArgWithCast(SILArgument *Arg) {
  SmallVector<SILValue, 4> ArgValues;
  Arg->getIncomingValues(ArgValues);
  if (isa<StructInst>(ArgValues[0]))
    return replaceBBArgWithStruct(Arg, ArgValues);
  return nullptr;
}
