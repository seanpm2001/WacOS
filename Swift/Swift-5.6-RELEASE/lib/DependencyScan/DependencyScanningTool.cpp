//===------------ DependencyScanningTool.cpp - Swift Compiler -------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/DependencyScan/DependencyScanningTool.h"
#include "swift/DependencyScan/SerializedModuleDependencyCacheFormat.h"
#include "swift/DependencyScan/StringUtils.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/DiagnosticsFrontend.h"
#include "swift/Basic/LLVMInitialize.h"
#include "swift/Basic/TargetInfo.h"
#include "swift/DependencyScan/DependencyScanImpl.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"

#include <sstream>

namespace swift {
namespace dependencies {

llvm::ErrorOr<swiftscan_string_ref_t> getTargetInfo(ArrayRef<const char *> Command) {
  // We must reset option occurences because we are handling an unrelated
  // command-line to those possibly parsed parsed before using the same tool.
  // We must do so because LLVM options parsing is done using a managed
  // static `GlobalParser`.
  llvm::cl::ResetAllOptionOccurrences();
  // Parse arguments.
  std::string CommandString;
  for (const auto *c : Command) {
    CommandString.append(c);
    CommandString.append(" ");
  }
  SmallVector<const char *, 4> Args;
  llvm::BumpPtrAllocator Alloc;
  llvm::StringSaver Saver(Alloc);
  llvm::cl::TokenizeGNUCommandLine(CommandString, Saver, Args);
  SourceManager dummySM;
  DiagnosticEngine DE(dummySM);
  CompilerInvocation Invocation;
  if (Invocation.parseArgs(Args, DE)) {
    return std::make_error_code(std::errc::invalid_argument);
  }

  // Store the result to a string.
  std::string ResultStr;
  llvm::raw_string_ostream StrOS(ResultStr);
  swift::targetinfo::printTargetInfo(Invocation, StrOS);
  return create_clone(ResultStr.c_str());
}

DependencyScanningTool::DependencyScanningTool()
    : SharedCache(std::make_unique<GlobalModuleDependenciesCache>()),
      VersionedPCMInstanceCacheCache(
          std::make_unique<CompilerArgInstanceCacheMap>()),
      PDC(), Alloc(), Saver(Alloc) {}

llvm::ErrorOr<swiftscan_dependency_graph_t>
DependencyScanningTool::getDependencies(
    ArrayRef<const char *> Command,
    const llvm::StringSet<> &PlaceholderModules) {
  // The primary instance used to scan the query Swift source-code
  auto InstanceOrErr = initScannerForAction(Command);
  if (std::error_code EC = InstanceOrErr.getError())
    return EC;
  auto Instance = std::move(*InstanceOrErr);

  // Local scan cache instance, wrapping the shared global cache.
  ModuleDependenciesCache cache(*SharedCache);
  // Execute the scanning action, retreiving the in-memory result
  auto DependenciesOrErr = performModuleScan(*Instance.get(), cache);
  if (DependenciesOrErr.getError())
    return std::make_error_code(std::errc::not_supported);
  auto Dependencies = std::move(*DependenciesOrErr);

  return Dependencies;
}

llvm::ErrorOr<swiftscan_import_set_t>
DependencyScanningTool::getImports(ArrayRef<const char *> Command) {
  // The primary instance used to scan the query Swift source-code
  auto InstanceOrErr = initScannerForAction(Command);
  if (std::error_code EC = InstanceOrErr.getError())
    return EC;
  auto Instance = std::move(*InstanceOrErr);

  // Execute the scanning action, retreiving the in-memory result
  auto DependenciesOrErr = performModulePrescan(*Instance.get());
  if (DependenciesOrErr.getError())
    return std::make_error_code(std::errc::not_supported);
  auto Dependencies = std::move(*DependenciesOrErr);

  return Dependencies;
}

std::vector<llvm::ErrorOr<swiftscan_dependency_graph_t>>
DependencyScanningTool::getDependencies(
    ArrayRef<const char *> Command,
    const std::vector<BatchScanInput> &BatchInput,
    const llvm::StringSet<> &PlaceholderModules) {
  // The primary instance used to scan Swift modules
  auto InstanceOrErr = initScannerForAction(Command);
  if (std::error_code EC = InstanceOrErr.getError())
    return std::vector<llvm::ErrorOr<swiftscan_dependency_graph_t>>(
        BatchInput.size(), std::make_error_code(std::errc::invalid_argument));
  auto Instance = std::move(*InstanceOrErr);

  // Local scan cache instance, wrapping the shared global cache.
  ModuleDependenciesCache cache(*SharedCache);
  auto BatchScanResults = performBatchModuleScan(
      *Instance.get(), cache, VersionedPCMInstanceCacheCache.get(),
      Saver, BatchInput);

  return BatchScanResults;
}

void DependencyScanningTool::serializeCache(llvm::StringRef path) {
  SourceManager SM;
  DiagnosticEngine Diags(SM);
  Diags.addConsumer(PDC);
  module_dependency_cache_serialization::writeInterModuleDependenciesCache(
      Diags, path, *SharedCache);
}

bool DependencyScanningTool::loadCache(llvm::StringRef path) {
  SourceManager SM;
  DiagnosticEngine Diags(SM);
  Diags.addConsumer(PDC);
  SharedCache = std::make_unique<GlobalModuleDependenciesCache>();
  bool readFailed =
      module_dependency_cache_serialization::readInterModuleDependenciesCache(
          path, *SharedCache);
  if (readFailed) {
    Diags.diagnose(SourceLoc(), diag::warn_scaner_deserialize_failed, path);
  }
  return readFailed;
}

void DependencyScanningTool::resetCache() {
  SharedCache.reset(new GlobalModuleDependenciesCache());
}

llvm::ErrorOr<std::unique_ptr<CompilerInstance>>
DependencyScanningTool::initScannerForAction(
    ArrayRef<const char *> Command) {
  auto instanceOrErr = initCompilerInstanceForScan(Command);
  if (instanceOrErr.getError())
    return instanceOrErr;
  SharedCache->configureForTriple((*instanceOrErr)->getInvocation()
                                  .getLangOptions().Target.str());
  return instanceOrErr;
}

llvm::ErrorOr<std::unique_ptr<CompilerInstance>>
DependencyScanningTool::initCompilerInstanceForScan(
    ArrayRef<const char *> Command) {
  // State unique to an individual scan
  auto Instance = std::make_unique<CompilerInstance>();
  Instance->addDiagnosticConsumer(&PDC);

  // Basic error checking on the arguments
  if (Command.empty()) {
    Instance->getDiags().diagnose(SourceLoc(), diag::error_no_frontend_args);
    return std::make_error_code(std::errc::invalid_argument);
  }

  CompilerInvocation Invocation;
  SmallString<128> WorkingDirectory;
  llvm::sys::fs::current_path(WorkingDirectory);

  // We must reset option occurences because we are handling an unrelated
  // command-line to those possibly parsed parsed before using the same tool.
  // We must do so because LLVM options parsing is done using a managed
  // static `GlobalParser`.
  llvm::cl::ResetAllOptionOccurrences();

  // Parse arguments.
  std::string CommandString;
  for (const auto *c : Command) {
    CommandString.append(c);
    CommandString.append(" ");
  }
  SmallVector<const char *, 4> Args;
  llvm::cl::TokenizeGNUCommandLine(CommandString, Saver, Args);
  if (Invocation.parseArgs(Args, Instance->getDiags())) {
    return std::make_error_code(std::errc::invalid_argument);
  }

  // Setup the instance
  Instance->setup(Invocation);
  (void)Instance->getMainModule();

  return Instance;
}

} // namespace dependencies
} // namespace swift
