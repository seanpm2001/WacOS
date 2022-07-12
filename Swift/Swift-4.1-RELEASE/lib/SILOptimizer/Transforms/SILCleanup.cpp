//===--- SILCleanup.cpp - Removes diagnostics instructions ----------------===//
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
// Cleanup SIL to make it suitable for IRGen. Specifically, removes the calls to
// Builtin.staticReport(), which are not needed post SIL.
//
// FIXME: This pass is mandatory so should probably be in
// SILOptimizer/Mandatory.
//
//===----------------------------------------------------------------------===//

#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILModule.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"

using namespace swift;

static void cleanFunction(SILFunction &Fn) {
  for (auto &BB : Fn) {
    auto I = BB.begin(), E = BB.end();
    while (I != E) {
      // Make sure there is no iterator invalidation if the inspected
      // instruction gets removed from the block.
      SILInstruction *Inst = &*I;
      ++I;

      // Remove calls to Builtin.staticReport().
      if (auto *BI = dyn_cast<BuiltinInst>(Inst)) {
        const BuiltinInfo &B = BI->getBuiltinInfo();
        if (B.ID == BuiltinValueKind::StaticReport) {
          // The call to the builtin should get removed before we reach
          // IRGen.
          recursivelyDeleteTriviallyDeadInstructions(BI, /* Force */true);
        }
      }
    }
  }
}

namespace {
class SILCleanup : public swift::SILFunctionTransform {

  /// The entry point to the transformation.
  void run() override {
    cleanFunction(*getFunction());
    invalidateAnalysis(SILAnalysis::InvalidationKind::FunctionBody);
  }

};
} // end anonymous namespace


SILTransform *swift::createSILCleanup() {
  return new SILCleanup();
}
