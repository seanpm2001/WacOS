//===--- Action.cpp - Abstract compilation steps --------------------------===//
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

#include "swift/Driver/Action.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"

using namespace swift::driver;
using namespace llvm::opt;

JobAction::~JobAction() {
  if (getOwnsInputs()) {
    llvm::DeleteContainerPointers(Inputs);
  }
}

const char *Action::getClassName(ActionClass AC) {
  switch (AC) {
    case Input: return "input";
    case CompileJob: return "compile";
    case InterpretJob: return "interpret";
    case BackendJob: return "backend";
    case MergeModuleJob: return "merge-module";
    case ModuleWrapJob: return "modulewrap";
    case AutolinkExtractJob: return "swift-autolink-extract";
    case REPLJob: return "repl";
    case LinkJob: return "link";
    case GenerateDSYMJob: return "generate-dSYM";
  }

  llvm_unreachable("invalid class");
}

void InputAction::anchor() {}

void JobAction::anchor() {}

void CompileJobAction::anchor() {}

void InterpretJobAction::anchor() {}

void BackendJobAction::anchor() {}

void MergeModuleJobAction::anchor() {}

void ModuleWrapJobAction::anchor() {}

void AutolinkExtractJobAction::anchor() {}

void REPLJobAction::anchor() {}

void LinkJobAction::anchor() {}

void GenerateDSYMJobAction::anchor() {}
