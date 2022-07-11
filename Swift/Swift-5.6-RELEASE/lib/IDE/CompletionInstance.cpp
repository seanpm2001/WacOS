//===--- CompletionInstance.cpp -------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/IDE/CompletionInstance.h"

#include "swift/AST/ASTContext.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/DiagnosticsFrontend.h"
#include "swift/AST/Module.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/SourceFile.h"
#include "swift/Basic/Defer.h"
#include "swift/Basic/LangOptions.h"
#include "swift/Basic/PrettyStackTrace.h"
#include "swift/Basic/SourceManager.h"
#include "swift/ClangImporter/ClangModule.h"
#include "swift/Driver/FrontendUtil.h"
#include "swift/Frontend/Frontend.h"
#include "swift/IDE/CodeCompletion.h"
#include "swift/Parse/Lexer.h"
#include "swift/Parse/PersistentParserState.h"
#include "swift/Serialization/SerializedModuleLoader.h"
#include "swift/Subsystems.h"
#include "swift/SymbolGraphGen/SymbolGraphOptions.h"
#include "clang/AST/ASTContext.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace swift;
using namespace ide;

std::unique_ptr<llvm::MemoryBuffer>
swift::ide::makeCodeCompletionMemoryBuffer(const llvm::MemoryBuffer *origBuf,
                                           unsigned &Offset,
                                           StringRef bufferIdentifier) {

  auto origBuffSize = origBuf->getBufferSize();
  if (Offset > origBuffSize)
    Offset = origBuffSize;

  auto newBuffer = llvm::WritableMemoryBuffer::getNewUninitMemBuffer(
      origBuffSize + 1, bufferIdentifier);
  auto *pos = origBuf->getBufferStart() + Offset;
  auto *newPos =
      std::copy(origBuf->getBufferStart(), pos, newBuffer->getBufferStart());
  *newPos = '\0';
  std::copy(pos, origBuf->getBufferEnd(), newPos + 1);

  return std::unique_ptr<llvm::MemoryBuffer>(newBuffer.release());
}

namespace {
/// Returns index number of \p D in \p Decls . If it's not found, returns ~0.
template <typename Range>
unsigned findIndexInRange(Decl *D, const Range &Decls) {
  unsigned N = 0;
  for (auto I = Decls.begin(), E = Decls.end(); I != E; ++I) {
    if ((*I)->isImplicit())
      continue;
    if (*I == D)
      return N;
    ++N;
  }
  return ~0U;
}

/// Return the element at \p N in \p Decls .
template <typename Range> Decl *getElementAt(const Range &Decls, unsigned N) {
  for (auto I = Decls.begin(), E = Decls.end(); I != E; ++I) {
    if ((*I)->isImplicit())
      continue;
    if (N == 0)
      return *I;
    --N;
  }
  return nullptr;
}

/// Find the equivalent \c DeclContext with \p DC from \p SF AST.
/// This assumes the AST which contains \p DC has exact the same structure with
/// \p SF.
static DeclContext *getEquivalentDeclContextFromSourceFile(DeclContext *DC,
                                                           SourceFile *SF) {
  PrettyStackTraceDeclContext trace("getting equivalent decl context for", DC);
  auto *newDC = DC;
  // NOTE: Shortcut for DC->getParentSourceFile() == SF case is not needed
  // because they should be always different.

  // Get the index path in the current AST.
  SmallVector<unsigned, 4> IndexStack;
  do {
    auto *D = newDC->getAsDecl();
    if (!D)
      return nullptr;
    auto *parentDC = newDC->getParent();
    unsigned N = ~0U;

    if (auto accessor = dyn_cast<AccessorDecl>(D)) {
      // The AST for accessors is like:
      //   DeclContext -> AbstractStorageDecl -> AccessorDecl
      // We need to push the index of the accessor within the accessor list
      // of the storage.
      auto *storage = accessor->getStorage();
      if (!storage)
        return nullptr;
      auto accessorN = findIndexInRange(accessor, storage->getAllAccessors());
      IndexStack.push_back(accessorN);
      D = storage;
    }

    if (auto parentSF = dyn_cast<SourceFile>(parentDC)) {
      N = findIndexInRange(D, parentSF->getTopLevelDecls());
    } else if (auto parentIDC = dyn_cast_or_null<IterableDeclContext>(
                   parentDC->getAsDecl())) {
      N = findIndexInRange(D, parentIDC->getMembers());
    } else {
#ifndef NDEBUG
      llvm_unreachable("invalid DC kind for finding equivalent DC (indexpath)");
#endif
      return nullptr;
    }

    // Not found in the decl context tree.
    if (N == ~0U) {
      return nullptr;
    }

    IndexStack.push_back(N);
    newDC = parentDC;
  } while (!newDC->isModuleScopeContext());

  assert(isa<SourceFile>(newDC) && "DC should be in a SourceFile");

  // Query the equivalent decl context from the base SourceFile using the index
  // path.
  newDC = SF;
  do {
    auto N = IndexStack.pop_back_val();
    Decl *D = nullptr;
    if (auto parentSF = dyn_cast<SourceFile>(newDC))
      D = getElementAt(parentSF->getTopLevelDecls(), N);
    else if (auto parentIDC = dyn_cast<IterableDeclContext>(newDC->getAsDecl()))
      D = getElementAt(parentIDC->getMembers(), N);
    else
      llvm_unreachable("invalid DC kind for finding equivalent DC (query)");

    if (auto storage = dyn_cast_or_null<AbstractStorageDecl>(D)) {
      if (IndexStack.empty())
        return nullptr;
      auto accessorN = IndexStack.pop_back_val();
      D = getElementAt(storage->getAllAccessors(), accessorN);
    }

    newDC = dyn_cast_or_null<DeclContext>(D);
    if (!newDC)
      return nullptr;
  } while (!IndexStack.empty());

  assert(newDC->getContextKind() == DC->getContextKind());

  return newDC;
}

/// For each dependency file in \p CI, run \p callback until the callback
/// returns \c true. Returns \c true if any callback call returns \c true, \c
/// false otherwise.
static bool
forEachDependencyUntilTrue(CompilerInstance &CI, unsigned excludeBufferID,
                           llvm::function_ref<bool(StringRef)> callback) {
  // Check files in the current module.
  for (FileUnit *file : CI.getMainModule()->getFiles()) {
    StringRef filename;
    if (auto SF = dyn_cast<SourceFile>(file)) {
      if (SF->getBufferID() == excludeBufferID)
        continue;
      filename = SF->getFilename();
    } else if (auto LF = dyn_cast<LoadedFile>(file))
      filename = LF->getFilename();
    else
      continue;

    // Ignore synthesized files.
    if (filename.empty() || filename.front() == '<')
      continue;

    if (callback(filename))
      return true;
  }

  // Check other non-system depenencies (e.g. modules, headers).
  for (auto &dep : CI.getDependencyTracker()->getDependencies()) {
    if (callback(dep))
      return true;
  }
  for (auto dep : CI.getDependencyTracker()->getIncrementalDependencyPaths()) {
    if (callback(dep))
      return true;
  }

  return false;
}

/// Collect hash codes of the dependencies into \c Map.
static void cacheDependencyHashIfNeeded(CompilerInstance &CI,
                                        unsigned excludeBufferID,
                                        llvm::StringMap<llvm::hash_code> &Map) {
  auto &FS = CI.getFileSystem();
  forEachDependencyUntilTrue(
      CI, excludeBufferID, [&](StringRef filename) {
        if (Map.count(filename))
          return false;

        auto stat = FS.status(filename);
        if (!stat)
          return false;

        // We will check the hash only if the modification time of the dependecy
        // is zero. See 'areAnyDependentFilesInvalidated() below'.
        if (stat->getLastModificationTime() != llvm::sys::TimePoint<>())
          return false;

        auto buf = FS.getBufferForFile(filename);
        Map[filename] = llvm::hash_value(buf.get()->getBuffer());
        return false;
      });
}

/// Check if any dependent files are modified since \p timestamp.
static bool areAnyDependentFilesInvalidated(
    CompilerInstance &CI, llvm::vfs::FileSystem &FS,
    unsigned excludeBufferID, llvm::sys::TimePoint<> timestamp,
    llvm::StringMap<llvm::hash_code> &Map) {

  return forEachDependencyUntilTrue(
      CI, excludeBufferID, [&](StringRef filePath) {
        auto stat = FS.status(filePath);
        if (!stat)
          // Missing.
          return true;

        auto lastModTime = stat->getLastModificationTime();
        if (lastModTime > timestamp)
          // Modified.
          return true;

        // If the last modification time is zero, this file is probably from a
        // virtual file system. We need to check the content.
        if (lastModTime == llvm::sys::TimePoint<>()) {
          // Get the hash code of the last content.
          auto oldHashEntry = Map.find(filePath);
          if (oldHashEntry == Map.end())
            // Unreachable? Not virtual in old filesystem, but virtual in new
            // one.
            return true;
          auto oldHash = oldHashEntry->second;

          // Calculate the hash code of the current content.
          auto newContent = FS.getBufferForFile(filePath);
          if (!newContent)
            // Unreachable? stat succeeded, but coundn't get the content.
            return true;

          auto newHash = llvm::hash_value(newContent.get()->getBuffer());

          if (oldHash != newHash)
            return true;
        }

        return false;
      });
}

} // namespace

bool CompletionInstance::performCachedOperationIfPossible(
    llvm::hash_code ArgsHash,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FileSystem,
    llvm::MemoryBuffer *completionBuffer, unsigned int Offset,
    DiagnosticConsumer *DiagC,
    std::shared_ptr<std::atomic<bool>> CancellationFlag,
    llvm::function_ref<void(CancellableResult<CompletionInstanceResult>)>
        Callback) {
  llvm::PrettyStackTraceString trace(
      "While performing cached completion if possible");

  // Check the invalidation first. Otherwise, in case no 'CacheCI' exists yet,
  // the flag will remain 'true' even after 'CachedCI' is populated.
  if (CachedCIShouldBeInvalidated.exchange(false))
    return false;
  if (!CachedCI)
    return false;
  if (CachedReuseCount >= Opts.MaxASTReuseCount)
    return false;
  if (CachedArgHash != ArgsHash)
    return false;

  auto &CI = *CachedCI;
  auto *oldSF = CI.getCodeCompletionFile();
  assert(oldSF->getBufferID());

  auto *oldState = oldSF->getDelayedParserState();
  assert(oldState->hasCodeCompletionDelayedDeclState());
  auto &oldInfo = oldState->getCodeCompletionDelayedDeclState();

  auto &SM = CI.getSourceMgr();
  auto bufferName = completionBuffer->getBufferIdentifier();
  if (SM.getIdentifierForBuffer(*oldSF->getBufferID()) != bufferName)
    return false;

  if (shouldCheckDependencies()) {
    if (areAnyDependentFilesInvalidated(
            CI, *FileSystem, *oldSF->getBufferID(),
            DependencyCheckedTimestamp, InMemoryDependencyHash))
      return false;
    DependencyCheckedTimestamp = std::chrono::system_clock::now();
  }

  // Parse the new buffer into temporary SourceFile.
  SourceManager tmpSM;
  auto tmpBufferID = tmpSM.addMemBufferCopy(completionBuffer);
  tmpSM.setCodeCompletionPoint(tmpBufferID, Offset);

  LangOptions langOpts = CI.getASTContext().LangOpts;
  TypeCheckerOptions typeckOpts = CI.getASTContext().TypeCheckerOpts;
  SILOptions silOpts = CI.getASTContext().SILOpts;
  SearchPathOptions searchPathOpts = CI.getASTContext().SearchPathOpts;
  DiagnosticEngine tmpDiags(tmpSM);
  ClangImporterOptions clangOpts;
  symbolgraphgen::SymbolGraphOptions symbolOpts;
  std::unique_ptr<ASTContext> tmpCtx(
      ASTContext::get(langOpts, typeckOpts, silOpts, searchPathOpts, clangOpts,
                      symbolOpts, tmpSM, tmpDiags));
  tmpCtx->CancellationFlag = CancellationFlag;
  registerParseRequestFunctions(tmpCtx->evaluator);
  registerIDERequestFunctions(tmpCtx->evaluator);
  registerTypeCheckerRequestFunctions(tmpCtx->evaluator);
  registerClangImporterRequestFunctions(tmpCtx->evaluator);
  registerSILGenRequestFunctions(tmpCtx->evaluator);
  ModuleDecl *tmpM = ModuleDecl::create(Identifier(), *tmpCtx);
  SourceFile *tmpSF = new (*tmpCtx)
      SourceFile(*tmpM, oldSF->Kind, tmpBufferID, oldSF->getParsingOptions());

  // FIXME: Since we don't setup module loaders on the temporary AST context,
  // 'canImport()' conditional compilation directive always fails. That causes
  // interface hash change and prevents fast-completion.

  // Parse and get the completion context.
  auto *newState = tmpSF->getDelayedParserState();
  // Couldn't find any completion token?
  if (!newState->hasCodeCompletionDelayedDeclState())
    return false;

  auto &newInfo = newState->getCodeCompletionDelayedDeclState();
  unsigned newBufferID;
  DeclContext *traceDC = nullptr;
  switch (newInfo.Kind) {
  case CodeCompletionDelayedDeclKind::FunctionBody: {
    // If the interface has changed, AST must be refreshed.
    // See if the inteface of the function and types visible from a function
    // body has changed since the last completion. If they haven't changed,
    // completion can reuse the existing AST of the source file.
    // \c getInterfaceHash() is not enough because it doesn't take the interface
    // of the type members into account. For example:
    //
    //   struct S {
    //     func foo() {}
    //   }
    //   func main(val: S) {
    //     val.<HERE>
    //   }
    //
    // In this case, we need to ensure that the interface of \c S hasn't
    // changed. Note that we don't care about local types (i.e. type
    // declarations inside function bodies, closures, or top level statement
    // bodies) because they are not visible from other functions where the
    // completion is happening.
    const auto oldInterfaceHash = oldSF->getInterfaceHashIncludingTypeMembers();
    const auto newInterfaceHash = tmpSF->getInterfaceHashIncludingTypeMembers();
    if (oldInterfaceHash != newInterfaceHash)
      return false;

    DeclContext *DC =
        getEquivalentDeclContextFromSourceFile(newInfo.ParentContext, oldSF);
    if (!DC || !isa<AbstractFunctionDecl>(DC))
      return false;

    // OK, we can perform fast completion for this. Update the orignal delayed
    // decl state.

    // Fast completion keeps the buffer in memory for multiple completions.
    // To reduce the consumption, slice the source buffer so it only holds
    // the portion that is needed for the second pass.
    auto startOffset = newInfo.StartOffset;
    if (newInfo.PrevOffset != ~0u)
      startOffset = newInfo.PrevOffset;
    auto startLoc = tmpSM.getLocForOffset(tmpBufferID, startOffset);
    startLoc = Lexer::getLocForStartOfLine(tmpSM, startLoc);
    startOffset = tmpSM.getLocOffsetInBuffer(startLoc, tmpBufferID);

    auto endOffset = newInfo.EndOffset;
    auto endLoc = tmpSM.getLocForOffset(tmpBufferID, endOffset);
    endLoc = Lexer::getLocForEndOfToken(tmpSM, endLoc);
    endOffset = tmpSM.getLocOffsetInBuffer(endLoc, tmpBufferID);

    newInfo.StartOffset -= startOffset;
    newInfo.EndOffset -= startOffset;
    if (newInfo.PrevOffset != ~0u)
      newInfo.PrevOffset -= startOffset;

    auto sourceText =
        completionBuffer->getBuffer().slice(startOffset, endOffset);
    auto newOffset = Offset - startOffset;

    newBufferID = SM.addMemBufferCopy(sourceText, bufferName);
    SM.openVirtualFile(SM.getLocForBufferStart(newBufferID),
                       tmpSM.getDisplayNameForLoc(startLoc),
                       tmpSM.getPresumedLineAndColumnForLoc(startLoc).first -
                           1);
    SM.setCodeCompletionPoint(newBufferID, newOffset);

    oldInfo.ParentContext = DC;
    oldInfo.StartOffset = newInfo.StartOffset;
    oldInfo.EndOffset = newInfo.EndOffset;
    oldInfo.PrevOffset = newInfo.PrevOffset;
    oldState->restoreCodeCompletionDelayedDeclState(oldInfo);

    auto newBufferStart = SM.getRangeForBuffer(newBufferID).getStart();
    SourceRange newBodyRange(newBufferStart.getAdvancedLoc(newInfo.StartOffset),
                             newBufferStart.getAdvancedLoc(newInfo.EndOffset));

    auto *AFD = cast<AbstractFunctionDecl>(DC);
    AFD->setBodyToBeReparsed(newBodyRange);
    SM.setReplacedRange({AFD->getOriginalBodySourceRange(), newBodyRange});
    oldSF->clearScope();

    traceDC = AFD;
    break;
  }
  case CodeCompletionDelayedDeclKind::Decl:
  case CodeCompletionDelayedDeclKind::TopLevelCodeDecl: {
    // Support decl/top-level code only if the completion happens in a single
    // file 'main' script (e.g. playground).
    auto *oldM = oldInfo.ParentContext->getParentModule();
    if (oldM->getFiles().size() != 1 || oldSF->Kind != SourceFileKind::Main)
      return false;

    // Perform fast completion.

    // Prepare the new buffer in the source manager.
    auto sourceText = completionBuffer->getBuffer();
    if (newInfo.Kind == CodeCompletionDelayedDeclKind::TopLevelCodeDecl) {
      // We don't need the source text after the top-level code.
      auto endOffset = newInfo.EndOffset;
      auto endLoc = tmpSM.getLocForOffset(tmpBufferID, endOffset);
      endLoc = Lexer::getLocForEndOfToken(tmpSM, endLoc);
      endOffset = tmpSM.getLocOffsetInBuffer(endLoc, tmpBufferID);
      sourceText = sourceText.slice(0, endOffset);
    }
    newBufferID = SM.addMemBufferCopy(sourceText, bufferName);
    SM.setCodeCompletionPoint(newBufferID, Offset);

    // Create a new module and a source file using the current AST context.
    auto &Ctx = oldM->getASTContext();
    auto *newM = ModuleDecl::createMainModule(Ctx, oldM->getName(),
                                              oldM->getImplicitImportInfo());
    newM->setABIName(oldM->getABIName());
    auto *newSF = new (Ctx) SourceFile(*newM, SourceFileKind::Main, newBufferID,
                                       oldSF->getParsingOptions());
    newM->addFile(*newSF);

    // Tell the compiler instance we've replaced the main module.
    CI.setMainModule(newM);

    // Re-process the whole file (parsing will be lazily triggered). Still
    // re-use imported modules.
    performImportResolution(*newSF);
    bindExtensions(*newM);

    traceDC = newM;
#ifndef NDEBUG
    const auto *reparsedState = newSF->getDelayedParserState();
    assert(reparsedState->hasCodeCompletionDelayedDeclState() &&
           "Didn't find completion token?");

    auto &reparsedInfo = reparsedState->getCodeCompletionDelayedDeclState();
    assert(reparsedInfo.Kind == newInfo.Kind);
#endif
    break;
  }
  }

  {
    PrettyStackTraceDeclContext trace("performing cached completion", traceDC);

    // The diagnostic engine is keeping track of state which might modify
    // parsing and type checking behaviour. Clear the flags.
    CI.getDiags().resetHadAnyError();
    CI.getASTContext().CancellationFlag = CancellationFlag;

    if (DiagC)
      CI.addDiagnosticConsumer(DiagC);

    if (CancellationFlag && CancellationFlag->load(std::memory_order_relaxed)) {
      Callback(CancellableResult<CompletionInstanceResult>::cancelled());
    } else {
      Callback(CancellableResult<CompletionInstanceResult>::success(
          {CI, /*reusingASTContext=*/true,
           /*DidFindCodeCompletionToken=*/true}));
    }

    if (DiagC)
      CI.removeDiagnosticConsumer(DiagC);
  }

  CachedReuseCount += 1;

  return true;
}

void CompletionInstance::performNewOperation(
    Optional<llvm::hash_code> ArgsHash, swift::CompilerInvocation &Invocation,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FileSystem,
    llvm::MemoryBuffer *completionBuffer, unsigned int Offset,
    DiagnosticConsumer *DiagC,
    std::shared_ptr<std::atomic<bool>> CancellationFlag,
    llvm::function_ref<void(CancellableResult<CompletionInstanceResult>)>
        Callback) {
  llvm::PrettyStackTraceString trace("While performing new completion");

  // If ArgsHash is None we shouldn't cache the compiler instance.
  bool ShouldCacheCompilerInstance = ArgsHash.hasValue();

  auto TheInstance = std::make_unique<CompilerInstance>();

  // Track non-system dependencies in fast-completion mode to invalidate the
  // compiler instance if any dependent files are modified.
  Invocation.getFrontendOptions().IntermoduleDependencyTracking =
      IntermoduleDepTrackingMode::ExcludeSystem;

  {
    auto &CI = *TheInstance;
    if (DiagC)
      CI.addDiagnosticConsumer(DiagC);

    SWIFT_DEFER {
      if (DiagC)
        CI.removeDiagnosticConsumer(DiagC);
    };

    if (FileSystem != llvm::vfs::getRealFileSystem())
      CI.getSourceMgr().setFileSystem(FileSystem);

    Invocation.setCodeCompletionPoint(completionBuffer, Offset);

    if (CI.setup(Invocation)) {
      Callback(CancellableResult<CompletionInstanceResult>::failure(
          "failed to setup compiler instance"));
      return;
    }
    CI.getASTContext().CancellationFlag = CancellationFlag;
    registerIDERequestFunctions(CI.getASTContext().evaluator);

    // If we're expecting a standard library, but there either isn't one, or it
    // failed to load, let's bail early and hand back an empty completion
    // result to avoid any downstream crashes.
    if (CI.loadStdlibIfNeeded()) {
      Callback(CancellableResult<CompletionInstanceResult>::failure(
          "failed to load the standard library"));
      return;
    }

    CI.performParseAndResolveImportsOnly();

    bool DidFindCodeCompletionToken = CI.getCodeCompletionFile()
                                          ->getDelayedParserState()
                                          ->hasCodeCompletionDelayedDeclState();
    ShouldCacheCompilerInstance &= DidFindCodeCompletionToken;

    auto CancellationFlag = CI.getASTContext().CancellationFlag;
    if (CancellationFlag && CancellationFlag->load(std::memory_order_relaxed)) {
      Callback(CancellableResult<CompletionInstanceResult>::cancelled());
      // The completion instance may be in an invalid state when it's been
      // cancelled. Don't cache it.
      ShouldCacheCompilerInstance = false;
    } else {
      Callback(CancellableResult<CompletionInstanceResult>::success(
          {CI, /*ReuisingASTContext=*/false, DidFindCodeCompletionToken}));
      if (CancellationFlag &&
          CancellationFlag->load(std::memory_order_relaxed)) {
        ShouldCacheCompilerInstance = false;
      }
    }
  }

  // Cache the compiler instance if fast completion is enabled.
  // If we didn't find a code compleiton token, we can't cache the instance
  // because performCachedOperationIfPossible wouldn't have an old code
  // completion state to compare the new one to.
  if (ShouldCacheCompilerInstance)
    cacheCompilerInstance(std::move(TheInstance), *ArgsHash);
}

void CompletionInstance::cacheCompilerInstance(
    std::unique_ptr<CompilerInstance> CI, llvm::hash_code ArgsHash) {
  CachedCI = std::move(CI);
  CachedArgHash = ArgsHash;
  auto now = std::chrono::system_clock::now();
  DependencyCheckedTimestamp = now;
  CachedReuseCount = 0;
  InMemoryDependencyHash.clear();
  cacheDependencyHashIfNeeded(
      *CachedCI,
      CachedCI->getASTContext().SourceMgr.getCodeCompletionBufferID(),
      InMemoryDependencyHash);
}

bool CompletionInstance::shouldCheckDependencies() const {
  assert(CachedCI);
  using namespace std::chrono;
  auto now = system_clock::now();
  auto threshold = DependencyCheckedTimestamp +
                   seconds(Opts.DependencyCheckIntervalSecond);
  return threshold <= now;
}

void CompletionInstance::markCachedCompilerInstanceShouldBeInvalidated() {
  CachedCIShouldBeInvalidated = true;
}

void CompletionInstance::setOptions(CompletionInstance::Options NewOpts) {
  std::lock_guard<std::mutex> lock(mtx);
  Opts = NewOpts;
}

void swift::ide::CompletionInstance::performOperation(
    swift::CompilerInvocation &Invocation, llvm::ArrayRef<const char *> Args,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FileSystem,
    llvm::MemoryBuffer *completionBuffer, unsigned int Offset,
    DiagnosticConsumer *DiagC,
    std::shared_ptr<std::atomic<bool>> CancellationFlag,
    llvm::function_ref<void(CancellableResult<CompletionInstanceResult>)>
        Callback) {
  // Compute the signature of the invocation.
  llvm::hash_code ArgsHash(0);
  for (auto arg : Args)
    ArgsHash = llvm::hash_combine(ArgsHash, StringRef(arg));

  // Concurrent completions will block so that they have higher chance to use
  // the cached completion instance.
  std::lock_guard<std::mutex> lock(mtx);

  if (performCachedOperationIfPossible(ArgsHash, FileSystem, completionBuffer,
                                       Offset, DiagC, CancellationFlag,
                                       Callback)) {
    // We were able to reuse a cached AST. Callback has already been invoked
    // and we don't need to build a new AST. We are done.
    return;
  }

  // Always disable source location resolutions from .swiftsourceinfo file
  // because they're somewhat heavy operations and aren't needed for completion.
  Invocation.getFrontendOptions().IgnoreSwiftSourceInfo = true;

  // Disable to build syntax tree because code-completion skips some portion of
  // source text. That breaks an invariant of syntax tree building.
  Invocation.getLangOptions().BuildSyntaxTree = false;

  // We don't need token list.
  Invocation.getLangOptions().CollectParsedToken = false;

  performNewOperation(ArgsHash, Invocation, FileSystem, completionBuffer,
                      Offset, DiagC, CancellationFlag, Callback);
}

void swift::ide::CompletionInstance::codeComplete(
    swift::CompilerInvocation &Invocation, llvm::ArrayRef<const char *> Args,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FileSystem,
    llvm::MemoryBuffer *completionBuffer, unsigned int Offset,
    DiagnosticConsumer *DiagC, ide::CodeCompletionContext &CompletionContext,
    std::shared_ptr<std::atomic<bool>> CancellationFlag,
    llvm::function_ref<void(CancellableResult<CodeCompleteResult>)> Callback) {
  using ResultType = CancellableResult<CodeCompleteResult>;

  struct ConsumerToCallbackAdapter
      : public SimpleCachingCodeCompletionConsumer {
    SwiftCompletionInfo SwiftContext;
    std::shared_ptr<std::atomic<bool>> CancellationFlag;
    llvm::function_ref<void(ResultType)> Callback;
    bool HandleResultsCalled = false;

    ConsumerToCallbackAdapter(
        std::shared_ptr<std::atomic<bool>> CancellationFlag,
        llvm::function_ref<void(ResultType)> Callback)
        : CancellationFlag(CancellationFlag), Callback(Callback) {}

    void setContext(swift::ASTContext *context,
                    const swift::CompilerInvocation *invocation,
                    swift::ide::CodeCompletionContext *completionContext) {
      SwiftContext.swiftASTContext = context;
      SwiftContext.invocation = invocation;
      SwiftContext.completionContext = completionContext;
    }
    void clearContext() { SwiftContext = SwiftCompletionInfo(); }

    void handleResults(CodeCompletionContext &context) override {
      HandleResultsCalled = true;
      if (CancellationFlag &&
          CancellationFlag->load(std::memory_order_relaxed)) {
        Callback(ResultType::cancelled());
      } else {
        MutableArrayRef<CodeCompletionResult *> Results = context.takeResults();
        assert(SwiftContext.swiftASTContext);
        Callback(ResultType::success({Results, SwiftContext}));
      }
    }
  };

  performOperation(
      Invocation, Args, FileSystem, completionBuffer, Offset, DiagC,
      CancellationFlag,
      [&](CancellableResult<CompletionInstanceResult> CIResult) {
        CIResult.mapAsync<CodeCompleteResult>(
            [&CompletionContext, &CancellationFlag](auto &Result,
                                                    auto DeliverTransformed) {
              CompletionContext.ReusingASTContext = Result.DidReuseAST;
              CompilerInstance &CI = Result.CI;
              ConsumerToCallbackAdapter Consumer(CancellationFlag,
                                                 DeliverTransformed);

              std::unique_ptr<CodeCompletionCallbacksFactory> callbacksFactory(
                  ide::makeCodeCompletionCallbacksFactory(CompletionContext,
                                                          Consumer));

              if (!Result.DidFindCodeCompletionToken) {
                SwiftCompletionInfo Info{&CI.getASTContext(),
                                         &CI.getInvocation(),
                                         &CompletionContext};
                DeliverTransformed(ResultType::success({/*Results=*/{}, Info}));
                return;
              }

              Consumer.setContext(&CI.getASTContext(), &CI.getInvocation(),
                                  &CompletionContext);
              performCodeCompletionSecondPass(*CI.getCodeCompletionFile(),
                                              *callbacksFactory);
              Consumer.clearContext();
              if (!Consumer.HandleResultsCalled) {
                // If we didn't receive a handleResult call from the second
                // pass, we didn't receive any results. To make sure Callback
                // gets called exactly once, call it manually with no results
                // here.
                SwiftCompletionInfo Info{&CI.getASTContext(),
                                         &CI.getInvocation(),
                                         &CompletionContext};
                DeliverTransformed(ResultType::success({/*Results=*/{}, Info}));
              }
            },
            Callback);
      });
}

void swift::ide::CompletionInstance::typeContextInfo(
    swift::CompilerInvocation &Invocation, llvm::ArrayRef<const char *> Args,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FileSystem,
    llvm::MemoryBuffer *completionBuffer, unsigned int Offset,
    DiagnosticConsumer *DiagC,
    std::shared_ptr<std::atomic<bool>> CancellationFlag,
    llvm::function_ref<void(CancellableResult<TypeContextInfoResult>)>
        Callback) {
  using ResultType = CancellableResult<TypeContextInfoResult>;

  struct ConsumerToCallbackAdapter : public ide::TypeContextInfoConsumer {
    bool ReusingASTContext;
    std::shared_ptr<std::atomic<bool>> CancellationFlag;
    llvm::function_ref<void(ResultType)> Callback;
    bool HandleResultsCalled = false;

    ConsumerToCallbackAdapter(
        bool ReusingASTContext,
        std::shared_ptr<std::atomic<bool>> CancellationFlag,
        llvm::function_ref<void(ResultType)> Callback)
        : ReusingASTContext(ReusingASTContext),
          CancellationFlag(CancellationFlag), Callback(Callback) {}

    void handleResults(ArrayRef<ide::TypeContextInfoItem> Results) override {
      HandleResultsCalled = true;
      if (CancellationFlag &&
          CancellationFlag->load(std::memory_order_relaxed)) {
        Callback(ResultType::cancelled());
      } else {
        Callback(ResultType::success({Results, ReusingASTContext}));
      }
    }
  };

  performOperation(
      Invocation, Args, FileSystem, completionBuffer, Offset, DiagC,
      CancellationFlag,
      [&](CancellableResult<CompletionInstanceResult> CIResult) {
        CIResult.mapAsync<TypeContextInfoResult>(
            [&CancellationFlag](auto &Result, auto DeliverTransformed) {
              ConsumerToCallbackAdapter Consumer(
                  Result.DidReuseAST, CancellationFlag, DeliverTransformed);
              std::unique_ptr<CodeCompletionCallbacksFactory> callbacksFactory(
                  ide::makeTypeContextInfoCallbacksFactory(Consumer));

              if (!Result.DidFindCodeCompletionToken) {
                // Deliver empty results if we didn't find a code completion
                // token.
                DeliverTransformed(
                    ResultType::success({/*Results=*/{}, Result.DidReuseAST}));
              }

              performCodeCompletionSecondPass(
                  *Result.CI.getCodeCompletionFile(), *callbacksFactory);
              if (!Consumer.HandleResultsCalled) {
                // If we didn't receive a handleResult call from the second
                // pass, we didn't receive any results. To make sure Callback
                // gets called exactly once, call it manually with no results
                // here.
                DeliverTransformed(
                    ResultType::success({/*Results=*/{}, Result.DidReuseAST}));
              }
            },
            Callback);
      });
}

void swift::ide::CompletionInstance::conformingMethodList(
    swift::CompilerInvocation &Invocation, llvm::ArrayRef<const char *> Args,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FileSystem,
    llvm::MemoryBuffer *completionBuffer, unsigned int Offset,
    DiagnosticConsumer *DiagC, ArrayRef<const char *> ExpectedTypeNames,
    std::shared_ptr<std::atomic<bool>> CancellationFlag,
    llvm::function_ref<void(CancellableResult<ConformingMethodListResults>)>
        Callback) {
  using ResultType = CancellableResult<ConformingMethodListResults>;

  struct ConsumerToCallbackAdapter
      : public swift::ide::ConformingMethodListConsumer {
    bool ReusingASTContext;
    std::shared_ptr<std::atomic<bool>> CancellationFlag;
    llvm::function_ref<void(ResultType)> Callback;
    bool HandleResultsCalled = false;

    ConsumerToCallbackAdapter(
        bool ReusingASTContext,
        std::shared_ptr<std::atomic<bool>> CancellationFlag,
        llvm::function_ref<void(ResultType)> Callback)
        : ReusingASTContext(ReusingASTContext),
          CancellationFlag(CancellationFlag), Callback(Callback) {}

    void handleResult(const ide::ConformingMethodListResult &result) override {
      HandleResultsCalled = true;
      if (CancellationFlag &&
          CancellationFlag->load(std::memory_order_relaxed)) {
        Callback(ResultType::cancelled());
      } else {
        Callback(ResultType::success({&result, ReusingASTContext}));
      }
    }
  };

  performOperation(
      Invocation, Args, FileSystem, completionBuffer, Offset, DiagC,
      CancellationFlag,
      [&](CancellableResult<CompletionInstanceResult> CIResult) {
        CIResult.mapAsync<ConformingMethodListResults>(
            [&ExpectedTypeNames, &CancellationFlag](auto &Result,
                                                    auto DeliverTransformed) {
              ConsumerToCallbackAdapter Consumer(
                  Result.DidReuseAST, CancellationFlag, DeliverTransformed);
              std::unique_ptr<CodeCompletionCallbacksFactory> callbacksFactory(
                  ide::makeConformingMethodListCallbacksFactory(
                      ExpectedTypeNames, Consumer));

              if (!Result.DidFindCodeCompletionToken) {
                DeliverTransformed(
                    ResultType::success({/*Results=*/{}, Result.DidReuseAST}));
              }

              performCodeCompletionSecondPass(
                  *Result.CI.getCodeCompletionFile(), *callbacksFactory);
              if (!Consumer.HandleResultsCalled) {
                // If we didn't receive a handleResult call from the second
                // pass, we didn't receive any results. To make sure Callback
                // gets called exactly once, call it manually with no results
                // here.
                DeliverTransformed(
                    ResultType::success({/*Results=*/{}, Result.DidReuseAST}));
              }
            },
            Callback);
      });
}
