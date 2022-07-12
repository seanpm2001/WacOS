//===--- PassPipeline.cpp - Swift Compiler SIL Pass Entrypoints -----------===//
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
///
///  \file
///  \brief This file provides implementations of a few helper functions
///  which provide abstracted entrypoints to the SILPasses stage.
///
///  \note The actual SIL passes should be implemented in per-pass source files,
///  not in this file.
///
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "sil-passpipeline-plan"
#include "swift/SILOptimizer/PassManager/PassPipeline.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Module.h"
#include "swift/SIL/SILModule.h"
#include "swift/SILOptimizer/Analysis/Analysis.h"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/YAMLTraits.h"

using namespace swift;

static llvm::cl::opt<bool>
    SILViewCFG("sil-view-cfg", llvm::cl::init(false),
               llvm::cl::desc("Enable the sil cfg viewer pass"));

static llvm::cl::opt<bool> SILViewGuaranteedCFG(
    "sil-view-guaranteed-cfg", llvm::cl::init(false),
    llvm::cl::desc("Enable the sil cfg viewer pass after diagnostics"));

static llvm::cl::opt<bool> SILViewSILGenCFG(
    "sil-view-silgen-cfg", llvm::cl::init(false),
    llvm::cl::desc("Enable the sil cfg viewer pass before diagnostics"));

//===----------------------------------------------------------------------===//
//                          Diagnostic Pass Pipeline
//===----------------------------------------------------------------------===//

static void addCFGPrinterPipeline(SILPassPipelinePlan &P, StringRef Name) {
  P.startPipeline(Name);
  P.addCFGPrinter();
}

static void addMandatoryDebugSerialization(SILPassPipelinePlan &P) {
  P.startPipeline("Mandatory Debug Serialization");
  P.addOwnershipModelEliminator();
  P.addMandatoryInlining();
}

static void addOwnershipModelEliminatorPipeline(SILPassPipelinePlan &P) {
  P.startPipeline("Ownership Model Eliminator");
  P.addOwnershipModelEliminator();
}

static void addMandatoryOptPipeline(SILPassPipelinePlan &P,
                                    const SILOptions &Options) {
  P.startPipeline("Guaranteed Passes");
  if (Options.EnableMandatorySemanticARCOpts) {
    P.addSemanticARCOpts();
  }
  P.addDiagnoseStaticExclusivity();
  P.addCapturePromotion();

  // Select access kind after capture promotion and before stack promotion.
  // This guarantees that stack-promotable boxes have [static] enforcement.
  P.addAccessEnforcementSelection();

  P.addAllocBoxToStack();
  P.addNoReturnFolding();
  P.addMarkUninitializedFixup();
  P.addDefiniteInitialization();
  P.addOwnershipModelEliminator();
  P.addMandatoryInlining();
  P.addPredictableMemoryOptimizations();
  P.addDiagnosticConstantPropagation();
  P.addGuaranteedARCOpts();
  P.addDiagnoseUnreachable();
  P.addEmitDFDiagnostics();
  // Canonical swift requires all non cond_br critical edges to be split.
  P.addSplitNonCondBrCriticalEdges();
}

SILPassPipelinePlan
SILPassPipelinePlan::getDiagnosticPassPipeline(const SILOptions &Options) {
  SILPassPipelinePlan P;

  if (SILViewSILGenCFG) {
    addCFGPrinterPipeline(P, "SIL View SILGen CFG");
  }

  // If we are asked do debug serialization, instead of running all diagnostic
  // passes, just run mandatory inlining with dead transparent function cleanup
  // disabled.
  if (Options.DebugSerialization) {
    addMandatoryDebugSerialization(P);
    return P;
  }

  // Otherwise run the rest of diagnostics.
  addMandatoryOptPipeline(P, Options);

  if (SILViewGuaranteedCFG) {
    addCFGPrinterPipeline(P, "SIL View Guaranteed CFG");
  }
  return P;
}

//===----------------------------------------------------------------------===//
//                       Ownership Eliminator Pipeline
//===----------------------------------------------------------------------===//

SILPassPipelinePlan SILPassPipelinePlan::getOwnershipEliminatorPassPipeline() {
  SILPassPipelinePlan P;
  addOwnershipModelEliminatorPipeline(P);
  return P;
}

//===----------------------------------------------------------------------===//
//                         Performance Pass Pipeline
//===----------------------------------------------------------------------===//

namespace {

// Enumerates the optimization kinds that we do in SIL.
enum OptimizationLevelKind {
  LowLevel,
  MidLevel,
  HighLevel,
};

} // end anonymous namespace

void addSimplifyCFGSILCombinePasses(SILPassPipelinePlan &P) {
  P.addSimplifyCFG();
  P.addConditionForwarding();
  // Jump threading can expose opportunity for silcombine (enum -> is_enum_tag->
  // cond_br).
  P.addSILCombine();
  // Which can expose opportunity for simplifcfg.
  P.addSimplifyCFG();
}

/// Perform semantic annotation/loop base optimizations.
void addHighLevelLoopOptPasses(SILPassPipelinePlan &P) {
  // Perform classic SSA optimizations for cleanup.
  P.addLowerAggregateInstrs();
  P.addSILCombine();
  P.addSROA();
  P.addMem2Reg();
  P.addDCE();
  P.addSILCombine();
  addSimplifyCFGSILCombinePasses(P);

  // Run high-level loop opts.
  P.addLoopRotate();

  // Cleanup.
  P.addDCE();
  // Also CSE semantic calls.
  P.addHighLevelCSE();
  P.addSILCombine();
  P.addSimplifyCFG();
  P.addHighLevelLICM();
  // Start of loop unrolling passes.
  P.addArrayCountPropagation();
  // To simplify induction variable.
  P.addSILCombine();
  P.addLoopUnroll();
  P.addSimplifyCFG();
  P.addPerformanceConstantPropagation();
  P.addSimplifyCFG();
  P.addArrayElementPropagation();
  // End of unrolling passes.
  P.addRemovePins();
  P.addABCOpt();
  // Cleanup.
  P.addDCE();
  P.addCOWArrayOpts();
  // Cleanup.
  P.addDCE();
  P.addSwiftArrayOpts();
}

// Perform classic SSA optimizations.
void addSSAPasses(SILPassPipelinePlan &P, OptimizationLevelKind OpLevel) {
  // Promote box allocations to stack allocations.
  P.addAllocBoxToStack();

  // Propagate copies through stack locations.  Should run after
  // box-to-stack promotion since it is limited to propagating through
  // stack locations. Should run before aggregate lowering since that
  // splits up copy_addr.
  P.addCopyForwarding();

  // Split up opaque operations (copy_addr, retain_value, etc.).
  P.addLowerAggregateInstrs();

  // Split up operations on stack-allocated aggregates (struct, tuple).
  P.addSROA();

  // Promote stack allocations to values.
  P.addMem2Reg();

  // Cleanup, which is important if the inliner has restarted the pass pipeline.
  P.addPerformanceConstantPropagation();
  P.addSimplifyCFG();
  P.addSILCombine();

  // Mainly for Array.append(contentsOf) optimization.
  P.addArrayElementPropagation();
  
  // Run the devirtualizer, specializer, and inliner. If any of these
  // makes a change we'll end up restarting the function passes on the
  // current function (after optimizing any new callees).
  P.addDevirtualizer();
  P.addGenericSpecializer();
  // Run devirtualizer after the specializer, because many
  // class_method/witness_method instructions may use concrete types now.
  P.addDevirtualizer();

  switch (OpLevel) {
  case OptimizationLevelKind::HighLevel:
    // Does not inline functions with defined semantics.
    P.addEarlyInliner();
    break;
  case OptimizationLevelKind::MidLevel:
    P.addGlobalOpt();
    P.addLetPropertiesOpt();
    // It is important to serialize before any of the @_semantics
    // functions are inlined, because otherwise the information about
    // uses of such functions inside the module is lost,
    // which reduces the ability of the compiler to optimize clients
    // importing this module.
    P.addSerializeSILPass();
    // Does inline semantics-functions (except "availability"), but not
    // global-init functions.
    P.addPerfInliner();
    break;
  case OptimizationLevelKind::LowLevel:
    // Inlines everything
    P.addLateInliner();
    break;
  }

  // Promote stack allocations to values and eliminate redundant
  // loads.
  P.addMem2Reg();
  P.addPerformanceConstantPropagation();
  //  Do a round of CFG simplification, followed by peepholes, then
  //  more CFG simplification.

  // Jump threading can expose opportunity for SILCombine (enum -> is_enum_tag->
  // cond_br).
  P.addJumpThreadSimplifyCFG();
  P.addSILCombine();
  // SILCombine can expose further opportunities for SimplifyCFG.
  P.addSimplifyCFG();

  P.addCSE();
  P.addRedundantLoadElimination();

  P.addPerformanceConstantPropagation();
  P.addCSE();
  P.addDCE();

  // Perform retain/release code motion and run the first ARC optimizer.
  P.addEarlyCodeMotion();
  P.addReleaseHoisting();
  P.addARCSequenceOpts();

  P.addSimplifyCFG();
  if (OpLevel == OptimizationLevelKind::LowLevel) {
    // Remove retain/releases based on Builtin.unsafeGuaranteed
    P.addUnsafeGuaranteedPeephole();
    // Only hoist releases very late.
    P.addLateCodeMotion();
  } else
    P.addEarlyCodeMotion();

  P.addRetainSinking();
  // Retain sinking does not sink all retains in one round.
  // Let it run one more time time, because it can be beneficial.
  // FIXME: Improve the RetainSinking pass to sink more/all
  // retains in one go.
  P.addRetainSinking();
  P.addReleaseHoisting();
  P.addARCSequenceOpts();
  P.addRemovePins();
}

static void addPerfDebugSerializationPipeline(SILPassPipelinePlan &P) {
  P.startPipeline("Performance Debug Serialization");
  P.addSILLinker();
}

static void addPerfEarlyModulePassPipeline(SILPassPipelinePlan &P) {
  P.startPipeline("EarlyModulePasses");

  // Get rid of apparently dead functions as soon as possible so that
  // we do not spend time optimizing them.
  P.addDeadFunctionElimination();
  // Start by cloning functions from stdlib.
  P.addSILLinker();

  // Cleanup after SILGen: remove trivial copies to temporaries.
  P.addTempRValueOpt();

  // Add the outliner pass (Osize).
  P.addOutliner();
}

static void addHighLevelEarlyLoopOptPipeline(SILPassPipelinePlan &P) {
  P.startPipeline("HighLevel+EarlyLoopOpt");
  // FIXME: update this to be a function pass.
  P.addEagerSpecializer();
  addSSAPasses(P, OptimizationLevelKind::HighLevel);
  addHighLevelLoopOptPasses(P);
}

static void addMidModulePassesStackPromotePassPipeline(SILPassPipelinePlan &P) {
  P.startPipeline("MidModulePasses+StackPromote");
  P.addDeadFunctionElimination();
  P.addSILLinker();
  P.addDeadObjectElimination();
  P.addGlobalPropertyOpt();

  // Do the first stack promotion on high-level SIL.
  P.addStackPromotion();
}

static void addMidLevelPassPipeline(SILPassPipelinePlan &P) {
  P.startPipeline("MidLevel");
  addSSAPasses(P, OptimizationLevelKind::MidLevel);

  // Specialize partially applied functions with dead arguments as a preparation
  // for CapturePropagation.
  P.addDeadArgSignatureOpt();

  // Run loop unrolling after inlining and constant propagation, because loop
  // trip counts may have became constant.
  P.addLoopUnroll();
}

static void addClosureSpecializePassPipeline(SILPassPipelinePlan &P) {
  P.startPipeline("ClosureSpecialize");
  P.addDeadFunctionElimination();
  P.addDeadObjectElimination();

  // Hoist globals out of loops.
  // Global-init functions should not be inlined GlobalOpt is done.
  P.addGlobalOpt();
  P.addLetPropertiesOpt();

  // Propagate constants into closures and convert to static dispatch.  This
  // should run after specialization and inlining because we don't want to
  // specialize a call that can be inlined. It should run before
  // ClosureSpecialization, because constant propagation is more effective.  At
  // least one round of SSA optimization and inlining should run after this to
  // take advantage of static dispatch.
  P.addCapturePropagation();

  // Specialize closure.
  P.addClosureSpecializer();

  // Do the second stack promotion on low-level SIL.
  P.addStackPromotion();

  // Speculate virtual call targets.
  P.addSpeculativeDevirtualization();

  // There should be at least one SILCombine+SimplifyCFG between the
  // ClosureSpecializer, etc. and the last inliner. Cleaning up after these
  // passes can expose more inlining opportunities.
  addSimplifyCFGSILCombinePasses(P);

  // We do this late since it is a pass like the inline caches that we only want
  // to run once very late. Make sure to run at least one round of the ARC
  // optimizer after this.
}

static void addLowLevelPassPipeline(SILPassPipelinePlan &P) {
  P.startPipeline("LowLevel");

  // Should be after FunctionSignatureOpts and before the last inliner.
  P.addReleaseDevirtualizer();

  addSSAPasses(P, OptimizationLevelKind::LowLevel);
  P.addDeadStoreElimination();

  // We've done a lot of optimizations on this function, attempt to FSO.
  P.addFunctionSignatureOpts();
}

static void addLateLoopOptPassPipeline(SILPassPipelinePlan &P) {
  P.startPipeline("LateLoopOpt");

  // Delete dead code and drop the bodies of shared functions.
  P.addDeadFunctionElimination();

  // Perform the final lowering transformations.
  P.addCodeSinking();
  P.addLICM();

  // Optimize overflow checks.
  P.addRedundantOverflowCheckRemoval();
  P.addMergeCondFails();

  // Remove dead code.
  P.addDCE();
  P.addSILCombine();
  P.addSimplifyCFG();

  // Try to hoist all releases, including epilogue releases. This should be
  // after FSO.
  P.addLateReleaseHoisting();

  // Has only an effect if the -assume-single-thread option is specified.
  P.addAssumeSingleThreaded();
}

static void addSILDebugInfoGeneratorPipeline(SILPassPipelinePlan &P) {
  P.startPipeline("SIL Debug Info Generator");
  P.addSILDebugInfoGenerator();
}

/// Mandatory IRGen preparation. It is the caller's job to set the set stage to
/// "lowered" after running this pipeline.
SILPassPipelinePlan
SILPassPipelinePlan::getLoweringPassPipeline() {
  SILPassPipelinePlan P;
  P.startPipeline("Address Lowering");
  P.addSILCleanup();
  P.addAddressLowering();

  return P;
}

SILPassPipelinePlan
SILPassPipelinePlan::getIRGenPreparePassPipeline(const SILOptions &Options) {
  SILPassPipelinePlan P;
  P.startPipeline("IRGen Preparation");
  // Insert SIL passes to run during IRGen.
  // Hoist generic alloc_stack instructions to the entry block to enable better
  // llvm-ir generation for dynamic alloca instructions.
  P.addAllocStackHoisting();
  if (Options.EnableLargeLoadableTypes) {
    P.addLoadableByAddress();
  }
  return P;
}

SILPassPipelinePlan
SILPassPipelinePlan::getSILOptPreparePassPipeline(const SILOptions &Options) {
  SILPassPipelinePlan P;

  if (Options.DebugSerialization) {
    addPerfDebugSerializationPipeline(P);
    return P;
  }

  P.startPipeline("SILOpt Prepare Passes");
  P.addAccessMarkerElimination();

  return P;
}

SILPassPipelinePlan
SILPassPipelinePlan::getPerformancePassPipeline(const SILOptions &Options) {
  SILPassPipelinePlan P;

  if (Options.DebugSerialization) {
    addPerfDebugSerializationPipeline(P);
    return P;
  }

  // Eliminate immediately dead functions and then clone functions from the
  // stdlib.
  addPerfEarlyModulePassPipeline(P);

  // Then run an iteration of the high-level SSA passes.
  addHighLevelEarlyLoopOptPipeline(P);
  addMidModulePassesStackPromotePassPipeline(P);

  // Run an iteration of the mid-level SSA passes.
  addMidLevelPassPipeline(P);

  // Perform optimizations that specialize.
  addClosureSpecializePassPipeline(P);

  // Run another iteration of the SSA optimizations to optimize the
  // devirtualized inline caches and constants propagated into closures
  // (CapturePropagation).
  addLowLevelPassPipeline(P);

  addLateLoopOptPassPipeline(P);

  // Has only an effect if the -gsil option is specified.
  addSILDebugInfoGeneratorPipeline(P);

  // Call the CFG viewer.
  if (SILViewCFG) {
    addCFGPrinterPipeline(P, "SIL Before IRGen View CFG");
  }

  return P;
}

//===----------------------------------------------------------------------===//
//                            Onone Pass Pipeline
//===----------------------------------------------------------------------===//

SILPassPipelinePlan SILPassPipelinePlan::getOnonePassPipeline() {
  SILPassPipelinePlan P;

  // First specialize user-code.
  P.startPipeline("Prespecialization");
  P.addUsePrespecialized();

  P.startPipeline("Rest of Onone");

  // Has only an effect if the -assume-single-thread option is specified.
  P.addAssumeSingleThreaded();

  // Has only an effect if the -gsil option is specified.
  P.addSILDebugInfoGenerator();

  return P;
}

//===----------------------------------------------------------------------===//
//                          Inst Count Pass Pipeline
//===----------------------------------------------------------------------===//

SILPassPipelinePlan SILPassPipelinePlan::getInstCountPassPipeline() {
  SILPassPipelinePlan P;
  P.startPipeline("Inst Count");
  P.addInstCount();
  return P;
}

//===----------------------------------------------------------------------===//
//                          Pass Kind List Pipeline
//===----------------------------------------------------------------------===//

void SILPassPipelinePlan::addPasses(ArrayRef<PassKind> PassKinds) {
  for (auto K : PassKinds) {
    // We could add to the Kind list directly, but we want to allow for
    // additional code to be added to add* without this code needing to be
    // updated.
    switch (K) {
// Each pass gets its own add-function.
#define PASS(ID, TAG, NAME)                                                    \
  case PassKind::ID: {                                                         \
    add##ID();                                                                 \
    break;                                                                     \
  }
#include "swift/SILOptimizer/PassManager/Passes.def"
    case PassKind::invalidPassKind:
      llvm_unreachable("Unhandled pass kind?!");
    }
  }
}

SILPassPipelinePlan
SILPassPipelinePlan::getPassPipelineForKinds(ArrayRef<PassKind> PassKinds) {
  SILPassPipelinePlan P;
  P.startPipeline("Pass List Pipeline");
  P.addPasses(PassKinds);
  return P;
}

//===----------------------------------------------------------------------===//
//                Dumping And Loading Pass Pipelines from Yaml
//===----------------------------------------------------------------------===//

void SILPassPipelinePlan::dump() {
  print(llvm::errs());
  llvm::errs() << '\n';
}

void SILPassPipelinePlan::print(llvm::raw_ostream &os) {
  // Our pipelines yaml representation is simple, we just output it ourselves
  // rather than use the yaml writer interface. We want to use the yaml reader
  // interface to be resilient against slightly different forms of yaml.
  os << "[\n";
  interleave(getPipelines(),
             [&](const SILPassPipeline &Pipeline) {
               os << "    [\n";

               os << "        \"" << Pipeline.Name << "\"";
               for (PassKind Kind : getPipelinePasses(Pipeline)) {
                 os << ",\n        [\"" << PassKindID(Kind) << "\","
                    << "\"" << PassKindTag(Kind) << "\"]";
               }
             },
             [&] { os << "\n    ],\n"; });
  os << "\n    ]\n";
  os << ']';
}

SILPassPipelinePlan
SILPassPipelinePlan::getPassPipelineFromFile(StringRef Filename) {
  namespace yaml = llvm::yaml;
  DEBUG(llvm::dbgs() << "Parsing Pass Pipeline from " << Filename << "\n");

  // Load the input file.
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> FileBufOrErr =
      llvm::MemoryBuffer::getFileOrSTDIN(Filename);
  if (!FileBufOrErr) {
    llvm_unreachable("Failed to read yaml file");
  }

  StringRef Buffer = FileBufOrErr->get()->getBuffer();
  llvm::SourceMgr SM;
  yaml::Stream Stream(Buffer, SM);
  yaml::document_iterator DI = Stream.begin();
  assert(DI != Stream.end() && "Failed to read a document");
  yaml::Node *N = DI->getRoot();
  assert(N && "Failed to find a root");

  SILPassPipelinePlan P;

  auto *RootList = cast<yaml::SequenceNode>(N);
  llvm::SmallVector<PassKind, 32> Passes;
  for (yaml::Node &PipelineNode :
       make_range(RootList->begin(), RootList->end())) {
    Passes.clear();
    DEBUG(llvm::dbgs() << "New Pipeline:\n");

    auto *Desc = cast<yaml::SequenceNode>(&PipelineNode);
    yaml::SequenceNode::iterator DescIter = Desc->begin();
    StringRef Name = cast<yaml::ScalarNode>(&*DescIter)->getRawValue();
    DEBUG(llvm::dbgs() << "    Name: \"" << Name << "\"\n");
    ++DescIter;

    for (auto DescEnd = Desc->end(); DescIter != DescEnd; ++DescIter) {
      auto *InnerPassList = cast<yaml::SequenceNode>(&*DescIter);
      auto *FirstNode = &*InnerPassList->begin();
      StringRef PassName = cast<yaml::ScalarNode>(FirstNode)->getRawValue();
      unsigned Size = PassName.size() - 2;
      PassName = PassName.substr(1, Size);
      DEBUG(llvm::dbgs() << "    Pass: \"" << PassName << "\"\n");
      auto Kind = PassKindFromString(PassName);
      assert(Kind != PassKind::invalidPassKind && "Found invalid pass kind?!");
      Passes.push_back(Kind);
    }

    P.startPipeline(Name);
    P.addPasses(Passes);
  }

  return P;
}
