//===--- TestOptions.h - ----------------------------------------*- C++ -*-===//
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

#ifndef LLVM_SOURCEKITD_TEST_TESTOPTIONS_H
#define LLVM_SOURCEKITD_TEST_TESTOPTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringMap.h"
#include <string>

namespace sourcekitd_test {

enum class SourceKitRequest {
  None,
  ProtocolVersion,
  CompilerVersion,
  DemangleNames,
  MangleSimpleClasses,
  Index,
  CodeComplete,
  CodeCompleteOpen,
  CodeCompleteClose,
  CodeCompleteUpdate,
  CodeCompleteCacheOnDisk,
  CodeCompleteSetPopularAPI,
  TypeContextInfo,
  ConformingMethodList,
  CursorInfo,
  RangeInfo,
  RelatedIdents,
  SyntaxMap,
  Structure,
  Format,
  ExpandPlaceholder,
  DocInfo,
  SemanticInfo,
  InterfaceGen,
  InterfaceGenOpen,
  FindUSR,
  FindInterfaceDoc,
  Open,
  Close,
  Edit,
  PrintAnnotations,
  PrintDiags,
  ExtractComment,
  ModuleGroups,
  SyntacticRename,
  FindRenameRanges,
  FindLocalRenameRanges,
  NameTranslation,
  MarkupToXML,
  Statistics,
  SyntaxTree,
  EnableCompileNotifications,
  CollectExpresstionType,
  CollectVariableType,
  GlobalConfiguration,
  DependencyUpdated,
  Diagnostics,
#define SEMANTIC_REFACTORING(KIND, NAME, ID) KIND,
#include "swift/IDE/RefactoringKinds.def"
};

struct TestOptions {
  SourceKitRequest Request = SourceKitRequest::None;
  std::vector<std::string> Inputs;
  std::string SourceFile;
  std::string TextInputFile;
  std::string JsonRequestPath;
  std::string RenameSpecPath;
  llvm::Optional<std::string> SourceText;
  std::string ModuleGroupName;
  std::string InterestedUSR;
  unsigned Line = 0;
  unsigned Col = 0;
  unsigned EndLine = 0;
  unsigned EndCol = 0;
  unsigned Offset = 0;
  unsigned Length = 0;
  std::string SwiftVersion;
  bool PassVersionAsString = false;
  llvm::Optional<std::string> ReplaceText;
  std::string ModuleName;
  std::string HeaderPath;
  bool PassAsSourceText = false;
  std::string CachePath;
  llvm::SmallVector<std::string, 4> RequestOptions;
  llvm::ArrayRef<const char *> CompilerArgs;
  std::string ModuleCachePath;
  bool UsingSwiftArgs;
  std::string USR;
  std::string SwiftName;
  std::string ObjCName;
  std::string ObjCSelector;
  std::string Name;
  /// An ID that can be used to cancel this request.
  std::string RequestId;
  /// If not empty, all requests with this ID should be cancelled.
  std::string CancelRequest;
  /// If set, simulate that the request takes x ms longer than it actually
  /// does. The request can be cancelled while waiting this duration.
  llvm::Optional<uint64_t> SimulateLongRequest;
  bool CheckInterfaceIsASCII = false;
  bool UsedSema = false;
  bool PrintRequest = true;
  bool PrintResponseAsJSON = false;
  bool PrintRawResponse = false;
  bool PrintResponse = true;
  bool SimplifiedDemangling = false;
  bool SynthesizedExtensions = false;
  bool CollectActionables = false;
  bool isAsyncRequest = false;
  bool timeRequest = false;
  bool measureInstructions = false;
  bool DisableImplicitConcurrencyModuleImport = false;
  llvm::Optional<unsigned> CompletionCheckDependencyInterval;
  unsigned repeatRequest = 1;
  struct VFSFile {
    std::string path;
    bool passAsSourceText;
    VFSFile(std::string path, bool passAsSourceText)
        : path(std::move(path)), passAsSourceText(passAsSourceText) {}
  };
  llvm::StringMap<VFSFile> VFSFiles;
  llvm::Optional<std::string> VFSName;
  llvm::Optional<bool> CancelOnSubsequentRequest;
  bool ShellExecution = false;
  bool parseArgs(llvm::ArrayRef<const char *> Args);
  void printHelp(bool ShowHidden) const;
};

}

#endif
