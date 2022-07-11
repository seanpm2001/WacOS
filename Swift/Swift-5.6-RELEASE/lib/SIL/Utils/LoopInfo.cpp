//===--- LoopInfo.cpp - SIL Loop Analysis ---------------------------------===//
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

#include "swift/SIL/LoopInfo.h"
#include "swift/SIL/SILBasicBlock.h"
#include "swift/SIL/Dominance.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/CFG.h"
#include "llvm/Analysis/LoopInfoImpl.h"
#include "llvm/Support/Debug.h"

using namespace swift;

// Instantiate template members.
template class llvm::LoopBase<SILBasicBlock, SILLoop>;
template class llvm::LoopInfoBase<SILBasicBlock, SILLoop>;


void SILLoop::dump() const {
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  print(llvm::dbgs());
#endif
}

SILLoopInfo::SILLoopInfo(SILFunction *F, DominanceInfo *DT) : Dominance(DT) {
  LI.analyze(*Dominance);
}

bool SILLoop::canDuplicate(SILInstruction *I) const {
  // The deallocation of a stack allocation must be in the loop, otherwise the
  // deallocation will be fed by a phi node of two allocations.
  if (I->isAllocatingStack()) {
    for (auto *UI : cast<SingleValueInstruction>(I)->getUses()) {
      if (UI->getUser()->isDeallocatingStack()) {
        if (!contains(UI->getUser()->getParent()))
          return false;
      }
    }
    return true;
  }
  if (I->isDeallocatingStack()) {
    SILInstruction *alloc = nullptr;
    if (auto *dealloc = dyn_cast<DeallocStackInst>(I)) {
      SILValue address = dealloc->getOperand();
      if (isa<AllocStackInst>(address) || isa<PartialApplyInst>(address))
        alloc = cast<SingleValueInstruction>(address);
    }
    if (auto *dealloc = dyn_cast<DeallocRefInst>(I))
      alloc = dyn_cast<AllocRefInst>(dealloc->getOperand());

    return alloc && contains(alloc);
  }

  // CodeGen can't build ssa for objc methods.
  if (auto *Method = dyn_cast<MethodInst>(I)) {
    if (Method->getMember().isForeign) {
      for (auto *UI : Method->getUses()) {
        if (!contains(UI->getUser()))
          return false;
      }
    }
    return true;
  }

  // We can't have a phi of two openexistential instructions of different UUID.
  if (isa<OpenExistentialAddrInst>(I) || isa<OpenExistentialRefInst>(I) ||
      isa<OpenExistentialMetatypeInst>(I) ||
      isa<OpenExistentialValueInst>(I) || isa<OpenExistentialBoxInst>(I) ||
      isa<OpenExistentialBoxValueInst>(I)) {
    SingleValueInstruction *OI = cast<SingleValueInstruction>(I);
    for (auto *UI : OI->getUses())
      if (!contains(UI->getUser()))
        return false;
    return true;
  }

  if (isa<ThrowInst>(I))
    return false;

  // The entire access must be within the loop.
  if (auto BAI = dyn_cast<BeginAccessInst>(I)) {
    for (auto *UI : BAI->getUses()) {
      if (!contains(UI->getUser()))
        return false;
    }
    return true;
  }
  // The entire coroutine execution must be within the loop.
  // Note that we don't have to worry about the reverse --- a loop which
  // contains an end_apply or abort_apply of an external begin_apply ---
  // because that wouldn't be structurally valid in the first place.
  if (auto BAI = dyn_cast<BeginApplyInst>(I)) {
    for (auto UI : BAI->getTokenResult()->getUses()) {
      auto User = UI->getUser();
      assert(isa<EndApplyInst>(User) || isa<AbortApplyInst>(User));
      if (!contains(User))
        return false;
    }
    return true;
  }

  if (isa<DynamicMethodBranchInst>(I))
    return false;

  // Can't duplicate get/await_async_continuation.
  if (isa<AwaitAsyncContinuationInst>(I) ||
      isa<GetAsyncContinuationAddrInst>(I) || isa<GetAsyncContinuationInst>(I))
    return false;

  // Some special cases above that aren't considered isTriviallyDuplicatable
  // return true early.
  assert(I->isTriviallyDuplicatable() &&
    "Code here must match isTriviallyDuplicatable in SILInstruction");
  return true;
}

void SILLoopInfo::verify() const {
  LI.verify(*Dominance);
}
