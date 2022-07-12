//===--- SILOptions.h - Swift Language SILGen and SIL options ---*- C++ -*-===//
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
// This file defines the options which control the generation, processing,
// and optimization of SIL.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_SILOPTIONS_H
#define SWIFT_AST_SILOPTIONS_H

#include "swift/Basic/Sanitizers.h"
#include "swift/Basic/OptionSet.h"
#include "swift/Basic/OptimizationMode.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <climits>

namespace swift {

class SILOptions {
public:
  /// Controls the aggressiveness of the performance inliner.
  int InlineThreshold = -1;

  /// Controls the aggressiveness of the performance inliner for Osize.
  int CallerBaseBenefitReductionFactor = 2;

  /// Controls the aggressiveness of the loop unroller.
  int UnrollThreshold = 250;

  /// The number of threads for multi-threaded code generation.
  int NumThreads = 0;
  
  enum LinkingMode {
    /// Skip SIL linking.
    LinkNone,

    /// Perform normal SIL linking.
    LinkNormal,

    /// Link all functions during SIL linking.
    LinkAll
  };

  /// Controls how to perform SIL linking.
  LinkingMode LinkMode = LinkNormal;

  /// Controls whether to pull in SIL from partial modules during the
  /// merge modules step. Could perhaps be merged with the link mode
  /// above but the interactions between all the flags are tricky.
  bool MergePartialModules = false;

  /// Remove all runtime assertions during optimizations.
  bool RemoveRuntimeAsserts = false;

  /// Controls whether the SIL ARC optimizations are run.
  bool EnableARCOptimizations = true;

  /// Should we run any SIL performance optimizations
  ///
  /// Useful when you want to enable -O LLVM opts but not -O SIL opts.
  bool DisableSILPerfOptimizations = false;

  /// Controls whether or not paranoid verification checks are run.
  bool VerifyAll = false;

  /// Are we debugging sil serialization.
  bool DebugSerialization = false;

  /// Whether to dump verbose SIL with scope and location information.
  bool EmitVerboseSIL = false;

  /// Optimization mode being used.
  OptimizationMode OptMode = OptimizationMode::NotSet;

  enum AssertConfiguration: unsigned {
    // Used by standard library code to distinguish between a debug and release
    // build.
    Debug = 0,     // Enables all asserts.
    Release = 1,   // Disables asserts.
    Unchecked = 2, // Disables asserts, preconditions, and runtime checks.

    // Leave the assert_configuration instruction around.
    DisableReplacement = UINT_MAX
  };

  /// The assert configuration controls how assertions behave.
  unsigned AssertConfig = Debug;

  /// Should we print out instruction counts if -print-stats is passed in?
  bool PrintInstCounts = false;

  /// Instrument code to generate profiling information.
  bool GenerateProfile = false;

  /// Path to the profdata file to be used for PGO, or the empty string.
  std::string UseProfile = "";

  /// Emit a mapping of profile counters for use in coverage.
  bool EmitProfileCoverageMapping = false;

  /// Should we use a pass pipeline passed in via a json file? Null by default.
  llvm::StringRef ExternalPassPipelineFilename;

  /// Emit normal function arguments using the +0 guaranteed convention.
  bool EnableGuaranteedNormalArguments = false;

  /// Don't generate code using partial_apply in SIL generation.
  bool DisableSILPartialApply = false;

  /// The name of the SIL outputfile if compiled with SIL debugging (-gsil).
  std::string SILOutputFileNameForDebugging;

  /// If set to true, compile with the SIL Ownership Model enabled.
  bool EnableSILOwnership = false;

  /// When parsing SIL, assume unqualified ownership.
  bool AssumeUnqualifiedOwnershipWhenParsing = false;

  /// Assume that code will be executed in a single-threaded environment.
  bool AssumeSingleThreaded = false;

  /// Indicates which sanitizer is turned on.
  OptionSet<SanitizerKind> Sanitizers;

  /// Emit compile-time diagnostics when the law of exclusivity is violated.
  bool EnforceExclusivityStatic = true;

  /// Emit checks to trap at run time when the law of exclusivity is violated.
  bool EnforceExclusivityDynamic = true;

  /// Enable the mandatory semantic arc optimizer.
  bool EnableMandatorySemanticARCOpts = false;

  /// \brief Enable large loadable types IRGen pass.
  bool EnableLargeLoadableTypes = true;

  /// The name of the file to which the backend should save YAML optimization
  /// records.
  std::string OptRecordFile;

  SILOptions() {}

  /// Return a hash code of any components from these options that should
  /// contribute to a Swift Bridging PCH hash.
  llvm::hash_code getPCHHashComponents() const {
    return llvm::hash_value(0);
  }

  bool shouldOptimize() const {
    return OptMode > OptimizationMode::NoOptimization;
  }
};

} // end namespace swift

#endif
