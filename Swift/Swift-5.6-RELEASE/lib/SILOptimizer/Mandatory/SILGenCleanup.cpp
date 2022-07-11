//===--- SILGenCleanup.cpp ------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
///
/// Perform peephole-style "cleanup" to aid SIL diagnostic passes.
///
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "silgen-cleanup"

#include "swift/Basic/Defer.h"
#include "swift/SIL/BasicBlockUtils.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/SILOptimizer/Utils/CanonicalizeInstruction.h"
#include "swift/SILOptimizer/Utils/InstOptUtils.h"

using namespace swift;

// Define a CanonicalizeInstruction subclass for use in SILGenCleanup.
struct SILGenCanonicalize final : CanonicalizeInstruction {
  bool changed = false;
  llvm::SmallPtrSet<SILInstruction *, 16> deadOperands;

  SILGenCanonicalize(DeadEndBlocks &deadEndBlocks)
      : CanonicalizeInstruction(DEBUG_TYPE, deadEndBlocks) {}

  void notifyNewInstruction(SILInstruction *) override { changed = true; }

  // Just delete the given 'inst' and record its operands. The callback isn't
  // allowed to mutate any other instructions.
  void killInstruction(SILInstruction *inst) override {
    deadOperands.erase(inst);
    for (auto &operand : inst->getAllOperands()) {
      if (auto *operInst = operand.get()->getDefiningInstruction())
        deadOperands.insert(operInst);
    }
    inst->eraseFromParent();
    changed = true;
  }

  void notifyHasNewUsers(SILValue) override { changed = true; }

  /// Delete trivially dead instructions in non-determistic order.
  ///
  /// We either have that nextII is endII or if nextII is not endII then endII
  /// is nextII->getParent()->end().
  SILBasicBlock::iterator deleteDeadOperands(SILBasicBlock::iterator nextII,
                                             SILBasicBlock::iterator endII) {
    auto callbacks = InstModCallbacks().onDelete([&](SILInstruction *deadInst) {
      LLVM_DEBUG(llvm::dbgs() << "Trivially dead: " << *deadInst);

      // If nextII is the instruction we are going to delete, move nextII past
      // it.
      if (deadInst->getIterator() == nextII)
        ++nextII;

      // Then remove the instruction from the set and delete it.
      deadOperands.erase(deadInst);
      deadInst->eraseFromParent();
    });

    while (!deadOperands.empty()) {
      SILInstruction *deadOperInst = *deadOperands.begin();

      // Make sure at least the first instruction is removed from the set.
      deadOperands.erase(deadOperInst);

      // Then delete this instruction/everything else that we can.
      eliminateDeadInstruction(deadOperInst, callbacks);
    }
    return nextII;
  }
};

//===----------------------------------------------------------------------===//
// SILGenCleanup: Top-Level Module Transform
//===----------------------------------------------------------------------===//

namespace {

// SILGenCleanup must run on all functions that will be seen by any analysis
// used by diagnostics before transforming the function that requires the
// analysis. e.g. Closures need to be cleaned up before the closure's parent can
// be diagnosed.
//
// TODO: This pass can be converted to a function transform if the mandatory
// pipeline runs in bottom-up closure order.
struct SILGenCleanup : SILModuleTransform {
  void run() override;
};

void SILGenCleanup::run() {
  auto &module = *getModule();
  for (auto &function : module) {
    LLVM_DEBUG(llvm::dbgs()
               << "\nRunning SILGenCleanup on " << function.getName() << "\n");

    DeadEndBlocks deadEndBlocks(&function);
    SILGenCanonicalize sgCanonicalize(deadEndBlocks);

    // Iterate over all blocks even if they aren't reachable. No phi-less
    // dataflow cycles should have been created yet, and these transformations
    // are simple enough they shouldn't be affected by cycles.
    for (auto &bb : function) {
      for (auto ii = bb.begin(), ie = bb.end(); ii != ie;) {
        ii = sgCanonicalize.canonicalize(&*ii);
        ii = sgCanonicalize.deleteDeadOperands(ii, ie);
      }
    }
    if (sgCanonicalize.changed) {
      auto invalidKind = SILAnalysis::InvalidationKind::Instructions;
      invalidateAnalysis(&function, invalidKind);
    }
  }
}

} // end anonymous namespace

SILTransform *swift::createSILGenCleanup() { return new SILGenCleanup(); }
