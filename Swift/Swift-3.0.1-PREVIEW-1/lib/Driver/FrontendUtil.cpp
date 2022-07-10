//===--- FrontendUtil.cpp - Driver Utilities for Frontend -----------------===//
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

#include "swift/Driver/FrontendUtil.h"

#include "swift/AST/DiagnosticsDriver.h"
#include "swift/Driver/Action.h"
#include "swift/Driver/Compilation.h"
#include "swift/Driver/Driver.h"
#include "swift/Driver/Job.h"
#include "swift/Frontend/Frontend.h"

using namespace swift;
using namespace swift::driver;

std::unique_ptr<CompilerInvocation>
swift::driver::createCompilerInvocation(ArrayRef<const char *> ArgList,
                                        DiagnosticEngine &Diags) {
  SmallVector<const char *, 16> Args;
  Args.push_back("<swiftc>"); // FIXME: Remove dummy argument.
  Args.insert(Args.end(), ArgList.begin(), ArgList.end());

  // When creating a CompilerInvocation, ensure that the driver creates a single
  // frontend command.
  Args.push_back("-force-single-frontend-invocation");

  // Force the driver into batch mode by specifying "swiftc" as the name.
  Driver TheDriver("swiftc", "swiftc", Args, Diags);

  // Don't check for the existence of input files, since the user of the
  // CompilerInvocation may wish to remap inputs to source buffers.
  TheDriver.setCheckInputFilesExist(false);

  std::unique_ptr<Compilation> C = TheDriver.buildCompilation(Args);
  if (!C || C->getJobs().empty())
    return nullptr; // Don't emit an error; one should already have been emitted

  SmallPtrSet<const Job *, 4> CompileCommands;
  for (const Job *Cmd : C->getJobs())
    if (isa<CompileJobAction>(Cmd->getSource()))
      CompileCommands.insert(Cmd);

  if (CompileCommands.size() != 1) {
    // TODO: include Jobs in the diagnostic.
    Diags.diagnose(SourceLoc(), diag::error_expected_one_frontend_job);
    return nullptr;
  }

  const Job *Cmd = *CompileCommands.begin();
  if (StringRef("-frontend") != Cmd->getArguments().front()) {
    Diags.diagnose(SourceLoc(), diag::error_expected_frontend_command);
    return nullptr;
  }

  std::unique_ptr<CompilerInvocation> Invocation(new CompilerInvocation());
  const llvm::opt::ArgStringList &BaseFrontendArgs = Cmd->getArguments();
  ArrayRef<const char *> FrontendArgs =
      llvm::makeArrayRef(BaseFrontendArgs.data() + 1,
                         BaseFrontendArgs.data() + BaseFrontendArgs.size());
  if (Invocation->parseArgs(FrontendArgs, Diags))
    return nullptr; // Don't emit an error; one should already have been emitted

  return Invocation;
}
