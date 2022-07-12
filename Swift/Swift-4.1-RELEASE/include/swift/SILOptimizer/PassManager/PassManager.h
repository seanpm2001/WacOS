//===--- PassManager.h  - Swift Pass Manager --------------------*- C++ -*-===//
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

#include "swift/SILOptimizer/Analysis/Analysis.h"
#include "swift/SILOptimizer/PassManager/PassPipeline.h"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <vector>

#ifndef SWIFT_SILOPTIMIZER_PASSMANAGER_PASSMANAGER_H
#define SWIFT_SILOPTIMIZER_PASSMANAGER_PASSMANAGER_H

namespace swift {

class SILFunction;
class SILFunctionTransform;
class SILModule;
class SILModuleTransform;
class SILOptions;
class SILTransform;

namespace irgen {
class IRGenModule;
}

/// \brief The SIL pass manager.
class SILPassManager {
  /// The module that the pass manager will transform.
  SILModule *Mod;

  /// An optional IRGenModule associated with this PassManager.
  irgen::IRGenModule *IRMod;

  /// The list of transformations to run.
  llvm::SmallVector<SILTransform *, 16> Transformations;

  /// A list of registered analysis.
  llvm::SmallVector<SILAnalysis *, 16> Analysis;

  /// An entry in the FunctionWorkList.
  struct WorklistEntry {
    WorklistEntry(SILFunction *F) : F(F) { }

    SILFunction *F;

    /// The current position in the transform-list.
    unsigned PipelineIdx = 0;

    /// How many times the pipeline was restarted for the function.
    unsigned NumRestarts = 0;
  };

  /// The worklist of functions to be processed by function passes.
  std::vector<WorklistEntry> FunctionWorklist;

  // Name of the current optimization stage for diagnostics.
  std::string StageName;

  /// The number of passes run so far.
  unsigned NumPassesRun = 0;

  /// Number of optimization iterations run.
  unsigned NumOptimizationIterations = 0;

  /// A mask which has one bit for each pass. A one for a pass-bit means that
  /// the pass doesn't need to run, because nothing has changed since the
  /// previous run of that pass.
  typedef std::bitset<(size_t)PassKind::AllPasses_Last + 1> CompletedPasses;
  
  /// A completed-passes mask for each function.
  llvm::DenseMap<SILFunction *, CompletedPasses> CompletedPassesMap;

  /// Stores for each function the number of levels of specializations it is
  /// derived from an original function. E.g. if a function is a signature
  /// optimized specialization of a generic specialization, it has level 2.
  /// This is used to avoid an infinite amount of functions pushed on the
  /// worklist (e.g. caused by a bug in a specializing optimization).
  llvm::DenseMap<SILFunction *, int> DerivationLevels;

  /// Set to true when a pass invalidates an analysis.
  bool CurrentPassHasInvalidated = false;

  /// True if we need to stop running passes and restart again on the
  /// same function.
  bool RestartPipeline = false;

  /// If true, passes are also run for functions which have
  /// OptimizationMode::NoOptimization.
  bool isMandatoryPipeline = false;

  /// The IRGen SIL passes. These have to be dynamically added by IRGen.
  llvm::DenseMap<unsigned, SILTransform *> IRGenPasses;

public:
  /// C'tor. It creates and registers all analysis passes, which are defined
  /// in Analysis.def.
  ///
  /// If \p isMandatoryPipeline is true, passes are also run for functions
  /// which have OptimizationMode::NoOptimization.
  SILPassManager(SILModule *M, llvm::StringRef Stage = "",
                 bool isMandatoryPipeline = false);

  /// C'tor. It creates an IRGen pass manager. Passes can query for the
  /// IRGenModule.
  SILPassManager(SILModule *M, irgen::IRGenModule *IRMod,
                 llvm::StringRef Stage = "",
                 bool isMandatoryPipeline = false);

  const SILOptions &getOptions() const;

  /// \brief Searches for an analysis of type T in the list of registered
  /// analysis. If the analysis is not found, the program terminates.
  template<typename T>
  T *getAnalysis() {
    for (SILAnalysis *A : Analysis)
      if (auto *R = llvm::dyn_cast<T>(A))
        return R;

    llvm_unreachable("Unable to find analysis for requested type.");
  }

  /// \returns the module that the pass manager owns.
  SILModule *getModule() { return Mod; }

  /// \returns the associated IGenModule or null if this is not an IRGen
  /// pass manager.
  irgen::IRGenModule *getIRGenModule() { return IRMod; }

  /// \brief Run one iteration of the optimization pipeline.
  void runOneIteration();

  /// \brief Restart the function pass pipeline on the same function
  /// that is currently being processed.
  void restartWithCurrentFunction(SILTransform *T);
  void clearRestartPipeline() { RestartPipeline = false; }
  bool shouldRestartPipeline() { return RestartPipeline; }

  /// \brief Iterate over all analysis and invalidate them.
  void invalidateAllAnalysis() {
    // Invalidate the analysis (unless they are locked)
    for (auto AP : Analysis)
      if (!AP->isLocked())
        AP->invalidate();

    CurrentPassHasInvalidated = true;

    // Assume that all functions have changed. Clear all masks of all functions.
    CompletedPassesMap.clear();
  }

  /// \brief Add the function \p F to the function pass worklist.
  /// If not null, the function \p DerivedFrom is the function from which \p F
  /// is derived. This is used to avoid an infinite amount of functions pushed
  /// on the worklist (e.g. caused by a bug in a specializing optimization).
  void addFunctionToWorklist(SILFunction *F, SILFunction *DerivedFrom);
  
  /// \brief Iterate over all analysis and notify them of the function.
  /// This function does not necessarily have to be newly created function. It
  /// is the job of the analysis to make sure no extra work is done if the
  /// particular analysis has been done on the function.
  void notifyAnalysisOfFunction(SILFunction *F) {
    for (auto AP : Analysis)
      AP->notifyAddFunction(F);
  }

  /// \brief Broadcast the invalidation of the function to all analysis.
  void invalidateAnalysis(SILFunction *F,
                          SILAnalysis::InvalidationKind K) {
    // Invalidate the analysis (unless they are locked)
    for (auto AP : Analysis)
      if (!AP->isLocked())
        AP->invalidate(F, K);
    
    CurrentPassHasInvalidated = true;
    // Any change let all passes run again.
    CompletedPassesMap[F].reset();
  }

  /// \brief Iterate over all analysis and notify them of a change in witness-
  /// or vtables.
  void invalidateFunctionTables() {
    // Invalidate the analysis (unless they are locked)
    for (auto AP : Analysis)
      if (!AP->isLocked())
        AP->invalidateFunctionTables();

    CurrentPassHasInvalidated = true;

    // Assume that all functions have changed. Clear all masks of all functions.
    CompletedPassesMap.clear();
  }

  /// \brief Iterate over all analysis and notify them of a deleted function.
  void notifyDeleteFunction(SILFunction *F) {
    // Invalidate the analysis (unless they are locked)
    for (auto AP : Analysis)
      if (!AP->isLocked())
        AP->notifyDeleteFunction(F);
    
    CurrentPassHasInvalidated = true;
    // Any change let all passes run again.
    CompletedPassesMap[F].reset();
  }

  /// \brief Reset the state of the pass manager and remove all transformation
  /// owned by the pass manager. Analysis passes will be kept.
  void resetAndRemoveTransformations();

  /// \brief Set the name of the current optimization stage.
  ///
  /// This is useful for debugging.
  void setStageName(llvm::StringRef NextStage = "");

  /// \brief Get the name of the current optimization stage.
  ///
  /// This is useful for debugging.
  StringRef getStageName() const;

  /// D'tor.
  ~SILPassManager();

  /// Verify all analyses.
  void verifyAnalyses() const {
    for (auto *A : Analysis) {
      A->verify();
    }
  }

  /// Verify all analyses, limiting the verification to just this one function
  /// if possible.
  ///
  /// Discussion: We leave it up to the analyses to decide how to implement
  /// this. If no override is provided the SILAnalysis should just call the
  /// normal verify method.
  void verifyAnalyses(SILFunction *F) const {
    for (auto *A : Analysis) {
      A->verify(F);
    }
  }

  void executePassPipelinePlan(const SILPassPipelinePlan &Plan) {
    for (const SILPassPipeline &Pipeline : Plan.getPipelines()) {
      setStageName(Pipeline.Name);
      resetAndRemoveTransformations();
      for (PassKind Kind : Plan.getPipelinePasses(Pipeline)) {
        addPass(Kind);
      }
      execute();
    }
  }

  void registerIRGenPass(PassKind Kind, SILTransform *Transform) {
    assert(IRGenPasses.find(unsigned(Kind)) == IRGenPasses.end() &&
           "Pass already registered");
    assert(
        IRMod &&
        "Attempting to register an IRGen pass with a non-IRGen pass manager");
    IRGenPasses[unsigned(Kind)] = Transform;
  }

private:
  void execute() {
    runOneIteration();
  }

  /// Add a pass of a specific kind.
  void addPass(PassKind Kind);

  /// Add a pass with a given name.
  void addPassForName(StringRef Name);

  /// Run the SIL module transform \p SMT over all the functions in
  /// the module.
  void runModulePass(SILModuleTransform *SMT);

  /// Run the pass \p SFT on the function \p F.
  void runPassOnFunction(SILFunctionTransform *SFT, SILFunction *F);

  /// Run the passes in \p FuncTransforms. Return true
  /// if the pass manager requested to stop the execution
  /// of the optimization cycle (this is a debug feature).
  void runFunctionPasses(ArrayRef<SILFunctionTransform *> FuncTransforms);

  /// A helper function that returns (based on SIL stage and debug
  /// options) whether we should continue running passes.
  bool continueTransforming();

  /// Return true if all analyses are unlocked.
  bool analysesUnlocked();

  /// Displays the call graph in an external dot-viewer.
  /// This function is meant for use from the debugger.
  /// When asserts are disabled, this is a NoOp.
  void viewCallGraph();
};

} // end namespace swift

#endif
