//===--- Compilation.h - Compilation Task Data Structure --------*- C++ -*-===//
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
//
// TODO: Document me
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_DRIVER_COMPILATION_H
#define SWIFT_DRIVER_COMPILATION_H

#include "swift/Driver/Job.h"
#include "swift/Driver/Util.h"
#include "swift/Basic/ArrayRefView.h"
#include "swift/Basic/LLVM.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/TimeValue.h"

#include <memory>
#include <vector>

namespace llvm {
namespace opt {
  class InputArgList;
  class DerivedArgList;
}
}

namespace swift {
  class DiagnosticEngine;

namespace driver {
  class Driver;
  class ToolChain;

/// An enum providing different levels of output which should be produced
/// by a Compilation.
enum class OutputLevel {
  /// Indicates that normal output should be produced.
  Normal,

  /// Indicates that verbose output should be produced. (-v)
  Verbose,

  /// Indicates that parseable output should be produced.
  Parseable,
};

class Compilation {
private:
  /// The DiagnosticEngine to which this Compilation should emit diagnostics.
  DiagnosticEngine &Diags;

  /// The OutputLevel at which this Compilation should generate output.
  OutputLevel Level;

  /// The Jobs which will be performed by this compilation.
  SmallVector<std::unique_ptr<const Job>, 32> Jobs;

  /// The original (untranslated) input argument list.
  ///
  /// This is only here for lifetime management. Any inspection of
  /// command-line arguments should use #getArgs().
  std::unique_ptr<llvm::opt::InputArgList> RawInputArgs;

  /// The translated input arg list.
  std::unique_ptr<llvm::opt::DerivedArgList> TranslatedArgs;

  /// A list of input files and their associated types.
  InputFileList InputFilesWithTypes;

  /// When non-null, a temporary file containing all input .swift files.
  /// Used for large compilations to avoid overflowing argv.
  const char *AllSourceFilesPath = nullptr;

  /// Temporary files that should be cleaned up after the compilation finishes.
  ///
  /// These apply whether the compilation succeeds or fails.
  std::vector<std::string> TempFilePaths;

  /// Write information about this compilation to this file.
  ///
  /// This is used for incremental builds.
  std::string CompilationRecordPath;

  /// A hash representing all the arguments that could trigger a full rebuild.
  std::string ArgsHash;

  /// When the build was started.
  ///
  /// This should be as close as possible to when the driver was invoked, since
  /// it's used as a lower bound.
  llvm::sys::TimeValue BuildStartTime;

  /// The time of the last build.
  ///
  /// If unknown, this will be some time in the past.
  llvm::sys::TimeValue LastBuildTime = llvm::sys::TimeValue::MinTime();

  /// The number of commands which this compilation should attempt to run in
  /// parallel.
  unsigned NumberOfParallelCommands;

  /// Indicates whether this Compilation should use skip execution of
  /// subtasks during performJobs() by using a dummy TaskQueue.
  ///
  /// \note For testing purposes only; similar user-facing features should be
  /// implemented separately, as the dummy TaskQueue may provide faked output.
  bool SkipTaskExecution;

  /// Indicates whether this Compilation should continue execution of subtasks
  /// even if they returned an error status.
  bool ContinueBuildingAfterErrors = false;

  /// Indicates whether tasks should only be executed if their output is out
  /// of date.
  bool EnableIncrementalBuild;

  /// True if temporary files should not be deleted.
  bool SaveTemps;

  /// When true, dumps information about why files are being scheduled to be
  /// rebuilt.
  bool ShowIncrementalBuildDecisions = false;

  static const Job *unwrap(const std::unique_ptr<const Job> &p) {
    return p.get();
  }
  
public:
  Compilation(DiagnosticEngine &Diags, OutputLevel Level,
              std::unique_ptr<llvm::opt::InputArgList> InputArgs,
              std::unique_ptr<llvm::opt::DerivedArgList> TranslatedArgs,
              InputFileList InputsWithTypes,
              StringRef ArgsHash, llvm::sys::TimeValue StartTime,
              unsigned NumberOfParallelCommands = 1,
              bool EnableIncrementalBuild = false,
              bool SkipTaskExecution = false,
              bool SaveTemps = false);
  ~Compilation();

  ArrayRefView<std::unique_ptr<const Job>, const Job *, Compilation::unwrap>
  getJobs() const {
    return llvm::makeArrayRef(Jobs);
  }
  Job *addJob(std::unique_ptr<Job> J);

  void addTemporaryFile(StringRef file) {
    TempFilePaths.push_back(file.str());
  }

  bool isTemporaryFile(StringRef file) {
    // TODO: Use a set instead of a linear search.
    return std::find(TempFilePaths.begin(), TempFilePaths.end(), file) !=
             TempFilePaths.end();
  }

  const llvm::opt::DerivedArgList &getArgs() const { return *TranslatedArgs; }
  ArrayRef<InputPair> getInputFiles() const { return InputFilesWithTypes; }

  unsigned getNumberOfParallelCommands() const {
    return NumberOfParallelCommands;
  }

  bool getIncrementalBuildEnabled() const {
    return EnableIncrementalBuild;
  }
  void disableIncrementalBuild() {
    EnableIncrementalBuild = false;
  }
  
  bool getContinueBuildingAfterErrors() const {
    return ContinueBuildingAfterErrors;
  }
  void setContinueBuildingAfterErrors(bool Value = true) {
    ContinueBuildingAfterErrors = Value;
  }

  void setShowsIncrementalBuildDecisions(bool value = true) {
    ShowIncrementalBuildDecisions = value;
  }

  void setCompilationRecordPath(StringRef path) {
    assert(CompilationRecordPath.empty() && "already set");
    CompilationRecordPath = path;
  }

  void setLastBuildTime(llvm::sys::TimeValue time) {
    LastBuildTime = time;
  }

  /// Requests the path to a file containing all input source files. This can
  /// be shared across jobs.
  ///
  /// If this is never called, the Compilation does not bother generating such
  /// a file.
  ///
  /// \sa types::isPartOfSwiftCompilation
  const char *getAllSourcesPath() const;

  /// Asks the Compilation to perform the Jobs which it knows about.
  /// \returns result code for the Compilation's Jobs; 0 indicates success and
  /// -2 indicates that one of the Compilation's Jobs crashed during execution
  int performJobs();

private:
  /// \brief Perform all jobs.
  ///
  /// \returns exit code of the first failed Job, or 0 on success. A return
  /// value of -2 indicates that a Job crashed during execution.
  int performJobsImpl();

  /// \brief Performs a single Job by executing in place, if possible.
  ///
  /// \param Cmd the Job which should be performed.
  ///
  /// \returns Typically, this function will not return, as the current process
  /// will no longer exist, or it will call exit() if the program was
  /// successfully executed. In the event of an error, this function will return
  /// a negative value indicating a failure to execute.
  int performSingleCommand(const Job *Cmd);
};

} // end namespace driver
} // end namespace swift

#endif
