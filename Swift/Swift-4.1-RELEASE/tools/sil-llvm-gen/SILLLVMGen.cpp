//===--- SILLLVMGen.cpp ---------------------------------------------------===//
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
/// \file
///
/// This is a tool for reading sil files and running IRGen passes upon them. It
/// is not meant to be used to run llvm optimizations on llvm-ir.
///
//===----------------------------------------------------------------------===//

#include "swift/AST/DiagnosticsFrontend.h"
#include "swift/AST/SILOptions.h"
#include "swift/Basic/LLVMContext.h"
#include "swift/Basic/LLVMInitialize.h"
#include "swift/Frontend/DiagnosticVerifier.h"
#include "swift/Frontend/Frontend.h"
#include "swift/Frontend/PrintingDiagnosticConsumer.h"
#include "swift/IRGen/IRGenPublic.h"
#include "swift/IRGen/IRGenSILPasses.h"
#include "swift/SILOptimizer/Analysis/Analysis.h"
#include "swift/SILOptimizer/PassManager/PassManager.h"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/Serialization/SerializationOptions.h"
#include "swift/Serialization/SerializedModuleLoader.h"
#include "swift/Serialization/SerializedSILLoader.h"
#include "swift/Strings.h"
#include "swift/Subsystems.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include <cstdio>
using namespace swift;

static llvm::cl::opt<std::string> InputFilename(llvm::cl::desc("input file"),
                                                llvm::cl::init("-"),
                                                llvm::cl::Positional);

static llvm::cl::opt<std::string>
    OutputFilename("o", llvm::cl::init("-"), llvm::cl::desc("output filename"));

static llvm::cl::list<std::string>
    ImportPaths("I",
                llvm::cl::desc("add a directory to the import search path"));

static llvm::cl::list<std::string> FrameworkPaths(
    "F", llvm::cl::desc("add a directory to the framework search path"));

static llvm::cl::opt<std::string>
    ModuleName("module-name",
               llvm::cl::desc("The name of the module if processing"
                              " a module. Necessary for processing "
                              "stdin."));

static llvm::cl::opt<std::string> ResourceDir(
    "resource-dir",
    llvm::cl::desc("The directory that holds the compiler resource files"));

static llvm::cl::opt<std::string>
    SDKPath("sdk", llvm::cl::desc("The path to the SDK for use with the clang "
                                  "importer."),
            llvm::cl::init(""));

static llvm::cl::opt<std::string> Target("target",
                                         llvm::cl::desc("target triple"));

static llvm::cl::opt<bool>
    PrintStats("print-stats", llvm::cl::desc("Print various statistics"));

static llvm::cl::opt<std::string>
    ModuleCachePath("module-cache-path",
                    llvm::cl::desc("Clang module cache path"));

static llvm::cl::opt<bool>
    PerformWMO("wmo", llvm::cl::desc("Enable whole-module optimizations"));

static llvm::cl::opt<bool> AssumeUnqualifiedOwnershipWhenParsing(
    "assume-parsing-unqualified-ownership-sil", llvm::cl::Hidden,
    llvm::cl::init(false),
    llvm::cl::desc("Assume all parsed functions have unqualified ownership"));

static llvm::cl::opt<IRGenOutputKind>
    OutputKind("output-kind", llvm::cl::desc("Type of output to produce"),
               llvm::cl::values(clEnumValN(IRGenOutputKind::LLVMAssembly,
                                           "llvm-as", "Emit llvm assembly"),
                                clEnumValN(IRGenOutputKind::LLVMBitcode,
                                           "llvm-bc", "Emit llvm bitcode"),
                                clEnumValN(IRGenOutputKind::NativeAssembly,
                                           "as", "Emit native assembly"),
                                clEnumValN(IRGenOutputKind::ObjectFile,
                                           "object", "Emit an object file")),
               llvm::cl::init(IRGenOutputKind::ObjectFile));

// This function isn't referenced outside its translation unit, but it
// can't use the "static" keyword because its address is used for
// getMainExecutable (since some platforms don't support taking the
// address of main, and some platforms can't implement getMainExecutable
// without being given the address of a function in the main executable).
void anchorForGetMainExecutable() {}

int main(int argc, char **argv) {
  INITIALIZE_LLVM(argc, argv);

  llvm::cl::ParseCommandLineOptions(argc, argv, "Swift LLVM IR Generator\n");

  if (PrintStats)
    llvm::EnableStatistics();

  CompilerInvocation Invocation;

  Invocation.setMainExecutablePath(llvm::sys::fs::getMainExecutable(
      argv[0], reinterpret_cast<void *>(&anchorForGetMainExecutable)));

  // Give the context the list of search paths to use for modules.
  Invocation.setImportSearchPaths(ImportPaths);
  std::vector<SearchPathOptions::FrameworkSearchPath> FramePaths;
  for (const auto &path : FrameworkPaths) {
    FramePaths.push_back({path, /*isSystem=*/false});
  }
  Invocation.setFrameworkSearchPaths(FramePaths);
  // Set the SDK path and target if given.
  if (SDKPath.getNumOccurrences() == 0) {
    const char *SDKROOT = getenv("SDKROOT");
    if (SDKROOT)
      SDKPath = SDKROOT;
  }
  if (!SDKPath.empty())
    Invocation.setSDKPath(SDKPath);
  if (!Target.empty())
    Invocation.setTargetTriple(Target);
  if (!ResourceDir.empty())
    Invocation.setRuntimeResourcePath(ResourceDir);
  // Set the module cache path. If not passed in we use the default swift module
  // cache.
  Invocation.getClangImporterOptions().ModuleCachePath = ModuleCachePath;
  Invocation.setParseStdlib();

  // Setup the language options
  auto &LangOpts = Invocation.getLangOptions();
  LangOpts.DisableAvailabilityChecking = true;
  LangOpts.EnableAccessControl = false;
  LangOpts.EnableObjCAttrRequiresFoundation = false;
  LangOpts.EnableObjCInterop = LangOpts.Target.isOSDarwin();

  // Setup the SIL Options.
  SILOptions &SILOpts = Invocation.getSILOptions();
  SILOpts.AssumeUnqualifiedOwnershipWhenParsing =
      AssumeUnqualifiedOwnershipWhenParsing;

  // Setup the IRGen Options.
  IRGenOptions &Opts = Invocation.getIRGenOptions();
  Opts.MainInputFilename = InputFilename;
  Opts.OutputFilenames.push_back(OutputFilename);
  Opts.OutputKind = OutputKind;

  serialization::ExtendedValidationInfo extendedInfo;
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> FileBufOrErr =
      Invocation.setUpInputForSILTool(InputFilename, ModuleName, false,
                                      extendedInfo);
  if (!FileBufOrErr) {
    fprintf(stderr, "Error! Failed to open file: %s\n", InputFilename.c_str());
    exit(-1);
  }

  CompilerInstance CI;
  PrintingDiagnosticConsumer PrintDiags;
  CI.addDiagnosticConsumer(&PrintDiags);

  if (!PerformWMO) {
    Invocation.getFrontendOptions().Inputs.setPrimaryInputForInputFilename(
        InputFilename);
  }

  if (CI.setup(Invocation))
    return 1;

  CI.performSema();

  // If parsing produced an error, don't run any passes.
  if (CI.getASTContext().hadError())
    return 1;

  // Load the SIL if we have a module. We have to do this after SILParse
  // creating the unfortunate double if statement.
  if (Invocation.hasSerializedAST()) {
    assert(!CI.hasSILModule() &&
           "performSema() should not create a SILModule.");
    CI.setSILModule(SILModule::createEmptyModule(
        CI.getMainModule(), CI.getSILOptions(), PerformWMO));
    std::unique_ptr<SerializedSILLoader> SL = SerializedSILLoader::create(
        CI.getASTContext(), CI.getSILModule(), nullptr);

    if (extendedInfo.isSIB())
      SL->getAllForModule(CI.getMainModule()->getName(), nullptr);
    else
      SL->getAll();
  }

  std::unique_ptr<llvm::Module> Mod = performIRGeneration(
      Opts, CI.getMainModule(), CI.takeSILModule(),
      CI.getMainModule()->getName().str(), getGlobalLLVMContext());
  return CI.getASTContext().hadError();
}
