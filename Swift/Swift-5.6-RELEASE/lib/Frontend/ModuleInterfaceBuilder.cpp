//===----- ModuleInterfaceBuilder.cpp - Compiles .swiftinterface files ----===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "textual-module-interface"

#include "swift/Frontend/ModuleInterfaceLoader.h"
#include "ModuleInterfaceBuilder.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/DiagnosticsFrontend.h"
#include "swift/AST/DiagnosticsSema.h"
#include "swift/AST/FileSystem.h"
#include "swift/AST/Module.h"
#include "swift/Basic/Defer.h"
#include "swift/Frontend/Frontend.h"
#include "swift/Frontend/ModuleInterfaceSupport.h"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/Serialization/SerializationOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Support/xxhash.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/LockFileManager.h"
#include "llvm/ADT/STLExtras.h"

using namespace swift;
using FileDependency = SerializationOptions::FileDependency;
namespace path = llvm::sys::path;

/// If the file dependency in \p FullDepPath is inside the \p Base directory,
/// this returns its path relative to \p Base. Otherwise it returns None.
static Optional<StringRef> getRelativeDepPath(StringRef DepPath,
                                              StringRef Base) {
  // If Base is the root directory, or DepPath does not start with Base, bail.
  if (Base.size() <= 1 || !DepPath.startswith(Base)) {
    return None;
  }

  assert(DepPath.size() > Base.size() &&
      "should never depend on a directory");

  // Is the DepName something like ${Base}/foo.h"?
  if (path::is_separator(DepPath[Base.size()]))
    return DepPath.substr(Base.size() + 1);

  // Is the DepName something like "${Base}foo.h", where Base
  // itself contains a trailing slash?
  if (path::is_separator(Base.back()))
    return DepPath.substr(Base.size());

  // We have something next to Base, like "Base.h", that's somehow
  // become a dependency.
  return None;
}

bool ModuleInterfaceBuilder::collectDepsForSerialization(
    CompilerInstance &SubInstance, SmallVectorImpl<FileDependency> &Deps,
    bool IsHashBased) {
  llvm::vfs::FileSystem &fs = *sourceMgr.getFileSystem();

  auto &Opts = SubInstance.getASTContext().SearchPathOpts;
  SmallString<128> SDKPath(Opts.SDKPath);
  path::native(SDKPath);
  SmallString<128> ResourcePath(Opts.RuntimeResourcePath);
  path::native(ResourcePath);

  auto DTDeps = SubInstance.getDependencyTracker()->getDependencies();
  SmallVector<std::string, 16> InitialDepNames(DTDeps.begin(), DTDeps.end());
  auto IncDeps = SubInstance.getDependencyTracker()->getIncrementalDependencyPaths();
  InitialDepNames.append(IncDeps.begin(), IncDeps.end());
  InitialDepNames.push_back(interfacePath.str());
  for (const auto &extra : extraDependencies) {
    InitialDepNames.push_back(extra.str());
  }
  SmallString<128> Scratch;

  for (const auto &InitialDepName : InitialDepNames) {
    path::native(InitialDepName, Scratch);
    StringRef DepName = Scratch.str();

    assert(moduleCachePath.empty() || !DepName.startswith(moduleCachePath));

    // Serialize the paths of dependencies in the SDK relative to it.
    Optional<StringRef> SDKRelativePath = getRelativeDepPath(DepName, SDKPath);
    StringRef DepNameToStore = SDKRelativePath.getValueOr(DepName);
    bool IsSDKRelative = SDKRelativePath.hasValue();

    // Forwarding modules add the underlying prebuilt module to their
    // dependency list -- don't serialize that.
    if (!prebuiltCachePath.empty() && DepName.startswith(prebuiltCachePath))
      continue;
    // Don't serialize interface path if it's from the preferred interface dir.
    // This ensures the prebuilt module caches generated from these interfaces are
    // relocatable.
    if (!backupInterfaceDir.empty() && DepName.startswith(backupInterfaceDir))
      continue;
    if (dependencyTracker) {
      dependencyTracker->addDependency(DepName, /*isSystem*/IsSDKRelative);
    }

    // Don't serialize compiler-relative deps so the cache is relocatable.
    if (DepName.startswith(ResourcePath))
      continue;

    auto Status = fs.status(DepName);
    if (!Status)
      return true;

    /// Lazily load the dependency buffer if we need it. If we're not
    /// dealing with a hash-based dependencies, and if the dependency is
    /// not a .swiftmodule, we can avoid opening the buffer.
    std::unique_ptr<llvm::MemoryBuffer> DepBuf = nullptr;
    auto getDepBuf = [&]() -> llvm::MemoryBuffer * {
      if (DepBuf) return DepBuf.get();
      if (auto Buf = fs.getBufferForFile(DepName, /*FileSize=*/-1,
                                         /*RequiresNullTerminator=*/false)) {
        DepBuf = std::move(Buf.get());
        return DepBuf.get();
      }
      return nullptr;
    };

    if (IsHashBased) {
      auto buf = getDepBuf();
      if (!buf) return true;
      uint64_t hash = xxHash64(buf->getBuffer());
      Deps.push_back(
                     FileDependency::hashBased(DepNameToStore, IsSDKRelative,
                                               Status->getSize(), hash));
    } else {
      uint64_t mtime =
      Status->getLastModificationTime().time_since_epoch().count();
      Deps.push_back(
                     FileDependency::modTimeBased(DepNameToStore, IsSDKRelative,
                                                  Status->getSize(), mtime));
    }
  }
  return false;
}

struct ErrorDowngradeConsumerRAII: DiagnosticConsumer {
  DiagnosticEngine &Diag;
  std::vector<DiagnosticConsumer *> allConsumers;
  bool SeenError;
  ErrorDowngradeConsumerRAII(DiagnosticEngine &Diag): Diag(Diag),
      allConsumers(Diag.takeConsumers()), SeenError(false) {
    Diag.addConsumer(*this);
  }
  ~ErrorDowngradeConsumerRAII() {
    for (auto *consumer: allConsumers) {
      Diag.addConsumer(*consumer);
    }
    Diag.removeConsumer(*this);
  }
  void handleDiagnostic(SourceManager &SM, const DiagnosticInfo &Info) override {
    DiagnosticInfo localInfo(Info);
    if (localInfo.Kind == DiagnosticKind::Error) {
      localInfo.Kind = DiagnosticKind::Warning;
      SeenError = true;
      for (auto *consumer: allConsumers) {
        consumer->handleDiagnostic(SM, localInfo);
      }
    }
  }
};

bool ModuleInterfaceBuilder::buildSwiftModuleInternal(
    StringRef OutPath, bool ShouldSerializeDeps,
    std::unique_ptr<llvm::MemoryBuffer> *ModuleBuffer,
    ArrayRef<std::string> CompiledCandidates) {

  auto outerPrettyStackState = llvm::SavePrettyStackState();

  bool SubError = false;
  static const size_t ThreadStackSize = 8 << 20; // 8 MB.
  bool RunSuccess = llvm::CrashRecoveryContext().RunSafelyOnThread([&] {
    // Pretend we're on the original thread for pretty-stack-trace purposes.
    auto savedInnerPrettyStackState = llvm::SavePrettyStackState();
    llvm::RestorePrettyStackState(outerPrettyStackState);
    SWIFT_DEFER {
      llvm::RestorePrettyStackState(savedInnerPrettyStackState);
    };

    SubError = (bool)subASTDelegate.runInSubCompilerInstance(moduleName,
                                                             interfacePath,
                                                             OutPath,
                                                             diagnosticLoc,
                                           [&](SubCompilerInstanceInfo &info) {
    auto &SubInstance = *info.Instance;
    auto subInvocation = SubInstance.getInvocation();
    // Try building forwarding module first. If succeed, return.
    if (SubInstance.getASTContext().getModuleInterfaceChecker()
          ->tryEmitForwardingModule(moduleName, interfacePath,
                                    CompiledCandidates, OutPath)) {
      return std::error_code();
    }
    FrontendOptions &FEOpts = subInvocation.getFrontendOptions();
    bool isTypeChecking =
        (FEOpts.RequestedAction == FrontendOptions::ActionType::Typecheck);
    const auto &InputInfo = FEOpts.InputsAndOutputs.firstInput();
    StringRef InPath = InputInfo.getFileName();
    const auto &OutputInfo =
    InputInfo.getPrimarySpecificPaths().SupplementaryOutputs;
    StringRef OutPath = OutputInfo.ModuleOutputPath;

    // Bail out if we're going to use the standard library but can't load it. If
    // we don't do this before we try to build the interface, we could end up
    // trying to rebuild a broken standard library dozens of times due to
    // multiple calls to `ASTContext::getStdlibModule()`.
    if (SubInstance.loadStdlibIfNeeded())
      return std::make_error_code(std::errc::not_supported);

    // Build the .swiftmodule; this is a _very_ abridged version of the logic
    // in performCompile in libFrontendTool, specialized, to just the one
    // module-serialization task we're trying to do here.
    LLVM_DEBUG(llvm::dbgs() << "Setting up instance to compile "
               << InPath << " to " << OutPath << "\n");

    LLVM_DEBUG(llvm::dbgs() << "Performing sema\n");
    if (isTypeChecking && FEOpts.DowngradeInterfaceVerificationError) {
      ErrorDowngradeConsumerRAII R(SubInstance.getDiags());
      SubInstance.performSema();
      return std::error_code();
    }
    SWIFT_DEFER {
      // Make sure to emit a generic top-level error if a module fails to
      // load. This is not only good for users; it also makes sure that we've
      // emitted an error in the parent diagnostic engine, which is what
      // determines whether the process exits with a proper failure status.
      if (SubInstance.getASTContext().hadError()) {
        auto builtByCompiler =
            getSwiftInterfaceCompilerVersionForCurrentCompiler(
                SubInstance.getASTContext());
        StringRef emittedByCompiler = info.CompilerVersion;
        if (!isTypeChecking && emittedByCompiler != builtByCompiler) {
          diagnose(diag::module_interface_build_failed_mismatching_compiler,
                   moduleName, emittedByCompiler, builtByCompiler);
        } else {
          diagnose(diag::module_interface_build_failed, isTypeChecking,
                   moduleName, emittedByCompiler == builtByCompiler,
                   emittedByCompiler, builtByCompiler);
        }
      }
    };

    SubInstance.performSema();
    if (SubInstance.getASTContext().hadError()) {
      LLVM_DEBUG(llvm::dbgs() << "encountered errors\n");
      return std::make_error_code(std::errc::not_supported);
    }
    // If we are just type-checking the interface, we are done.
    if (isTypeChecking)
      return std::error_code();

    SILOptions &SILOpts = subInvocation.getSILOptions();
    auto Mod = SubInstance.getMainModule();
    auto &TC = SubInstance.getSILTypes();
    auto SILMod = performASTLowering(Mod, TC, SILOpts);
    if (!SILMod) {
      LLVM_DEBUG(llvm::dbgs() << "SILGen did not produce a module\n");
      return std::make_error_code(std::errc::not_supported);
    }

    // Setup the callbacks for serialization, which can occur during the
    // optimization pipeline.
    SerializationOptions SerializationOpts;
    std::string OutPathStr = OutPath.str();
    SerializationOpts.OutputPath = OutPathStr.c_str();
    SerializationOpts.ModuleLinkName = FEOpts.ModuleLinkName;
    SerializationOpts.AutolinkForceLoad =
      !subInvocation.getIRGenOptions().ForceLoadSymbolName.empty();
    SerializationOpts.UserModuleVersion = FEOpts.UserModuleVersion;

    // Record any non-SDK module interface files for the debug info.
    StringRef SDKPath = SubInstance.getASTContext().SearchPathOpts.SDKPath;
    if (!getRelativeDepPath(InPath, SDKPath))
      SerializationOpts.ModuleInterface = InPath;

    SerializationOpts.SDKName = SubInstance.getASTContext().LangOpts.SDKName;
    SerializationOpts.ABIDescriptorPath = ABIDescriptorPath.str();
    SmallVector<FileDependency, 16> Deps;
    bool serializeHashes = FEOpts.SerializeModuleInterfaceDependencyHashes;
    if (collectDepsForSerialization(SubInstance, Deps, serializeHashes)) {
      return std::make_error_code(std::errc::not_supported);
    }
    if (ShouldSerializeDeps)
      SerializationOpts.Dependencies = Deps;
    SerializationOpts.IsOSSA = SILOpts.EnableOSSAModules;

    SILMod->setSerializeSILAction([&]() {
      // We don't want to serialize module docs in the cache -- they
      // will be serialized beside the interface file.
      serializeToBuffers(Mod, SerializationOpts, ModuleBuffer,
                         /*ModuleDocBuffer*/nullptr,
                         /*SourceInfoBuffer*/nullptr,
                         SILMod.get());
    });

    LLVM_DEBUG(llvm::dbgs() << "Running SIL processing passes\n");
    if (SubInstance.performSILProcessing(SILMod.get())) {
      LLVM_DEBUG(llvm::dbgs() << "encountered errors\n");
      return std::make_error_code(std::errc::not_supported);
    }
    if (SubInstance.getDiags().hadAnyError()) {
      return std::make_error_code(std::errc::not_supported);
    }
    return std::error_code();
    });
  }, ThreadStackSize);
  return !RunSuccess || SubError;
}

bool ModuleInterfaceBuilder::buildSwiftModule(StringRef OutPath,
                                              bool ShouldSerializeDeps,
                          std::unique_ptr<llvm::MemoryBuffer> *ModuleBuffer,
                          llvm::function_ref<void()> RemarkRebuild,
                          ArrayRef<std::string> CompiledCandidates) {
  auto build = [&]() {
    if (RemarkRebuild) {
      RemarkRebuild();
    }
    return buildSwiftModuleInternal(OutPath, ShouldSerializeDeps, ModuleBuffer,
                                    CompiledCandidates);
  };
  if (disableInterfaceFileLock) {
    return build();
  }
  while (1) {
  // Attempt to lock the interface file. Only one process is allowed to build
  // module from the interface so we don't consume too much memory when multiple
  // processes are doing the same.
  // FIXME: We should surface the module building step to the build system so
  // we don't need to synchronize here.
  llvm::LockFileManager Locked(OutPath);
  switch (Locked) {
  case llvm::LockFileManager::LFS_Error:{
    // ModuleInterfaceBuilder takes care of correctness and locks are only
    // necessary for performance. Fallback to building the module in case of any lock
    // related errors.
    if (RemarkRebuild) {
      diagnose(diag::interface_file_lock_failure);
    }
    // Clear out any potential leftover.
    Locked.unsafeRemoveLockFile();
    LLVM_FALLTHROUGH;
  }
  case llvm::LockFileManager::LFS_Owned: {
    return build();
  }
  case llvm::LockFileManager::LFS_Shared: {
    // Someone else is responsible for building the module. Wait for them to
    // finish.
    switch (Locked.waitForUnlock(256)) {
    case llvm::LockFileManager::Res_Success: {
      // This process may have a different module output path. If the other
      // process doesn't build the interface to this output path, we should try
      // building ourselves.
      auto bufferOrError = llvm::MemoryBuffer::getFile(OutPath);
      if (!bufferOrError)
        continue;
      if (ModuleBuffer)
        *ModuleBuffer = std::move(bufferOrError.get());
      return false;
    }
    case llvm::LockFileManager::Res_OwnerDied: {
      continue; // try again to get the lock.
    }
    case llvm::LockFileManager::Res_Timeout: {
      // Since ModuleInterfaceBuilder takes care of correctness, we try waiting for
      // another process to complete the build so swift does not do it done
      // twice. If case of timeout, build it ourselves.
      if (RemarkRebuild) {
        diagnose(diag::interface_file_lock_timed_out, interfacePath);
      }
      // Clear the lock file so that future invocations can make progress.
      Locked.unsafeRemoveLockFile();
      continue;
    }
    }
    break;
  }
  }
  }
}
