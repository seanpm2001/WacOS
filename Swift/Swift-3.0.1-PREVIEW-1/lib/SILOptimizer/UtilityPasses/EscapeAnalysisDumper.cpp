//===--- EscapeAnalysisDumper.cpp - Dumps the escape analysis -------------===//
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

#define DEBUG_TYPE "dump-ea"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/SILOptimizer/Analysis/EscapeAnalysis.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"

using namespace swift;

namespace {

/// Dumps the escape information of all functions in the module.
/// Only dumps if the compiler is built with assertions.
/// For details see EscapeAnalysis.
class EscapeAnalysisDumper : public SILModuleTransform {

  void run() override {
    DEBUG(llvm::dbgs() << "** EscapeAnalysisDumper **\n");

#ifndef NDEBUG
    auto *EA = PM->getAnalysis<EscapeAnalysis>();

    llvm::outs() << "Escape information of module\n";
    for (auto &F : *getModule()) {
      if (!F.isExternalDeclaration()) {
        auto *ConnectionGraph = EA->getConnectionGraph(&F);
        ConnectionGraph->print(llvm::outs());
      }
    }
#endif
  }

  StringRef getName() override { return "EscapeAnalysisDumper"; }
};

} // end anonymous namespace

SILTransform *swift::createEscapeAnalysisDumper() {
  return new EscapeAnalysisDumper();
}
