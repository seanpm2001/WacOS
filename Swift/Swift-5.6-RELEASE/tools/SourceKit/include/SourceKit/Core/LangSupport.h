//===--- LangSupport.h - ----------------------------------------*- C++ -*-===//
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

#ifndef LLVM_SOURCEKIT_CORE_LANGSUPPORT_H
#define LLVM_SOURCEKIT_CORE_LANGSUPPORT_H

#include "SourceKit/Core/LLVM.h"
#include "SourceKit/Support/CancellationToken.h"
#include "SourceKit/Support/UIdent.h"
#include "swift/AST/Type.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <functional>
#include <memory>
#include <unordered_set>

namespace llvm {
  class MemoryBuffer;
}

namespace swift {
namespace syntax {
class SourceFileSyntax;
} // namespace syntax
} // namespace swift

namespace SourceKit {
class GlobalConfig;

struct EntityInfo {
  UIdent Kind;
  StringRef Name;
  StringRef USR;
  StringRef Group;
  StringRef ReceiverUSR;
  bool IsDynamic = false;
  bool IsTestCandidate = false;
  bool IsImplicit = false;
  unsigned Line = 0;
  unsigned Column = 0;
  ArrayRef<UIdent> Attrs;
  Optional<UIdent> EffectiveAccess;

  EntityInfo() = default;
};

class IndexingConsumer {
  virtual void anchor();

public:
  virtual ~IndexingConsumer() { }

  virtual void failed(StringRef ErrDescription) = 0;

  virtual bool startDependency(UIdent Kind,
                               StringRef Name,
                               StringRef Path,
                               bool IsSystem) = 0;

  virtual bool finishDependency(UIdent Kind) = 0;

  virtual bool startSourceEntity(const EntityInfo &Info) = 0;

  virtual bool recordRelatedEntity(const EntityInfo &Info) = 0;

  virtual bool finishSourceEntity(UIdent Kind) = 0;
};

struct CodeCompletionInfo {
  UIdent Kind;
  // We need a separate field to passthrough custom kinds that originally came
  // from the client, because we can't safely construct a UIdent for them.
  void *CustomKind = nullptr;
  StringRef Name;
  StringRef Description;
  StringRef SourceText;
  StringRef TypeName;
  StringRef ModuleName;
  StringRef DocBrief;
  StringRef AssocUSRs;
  UIdent SemanticContext;
  UIdent TypeRelation;
  Optional<uint8_t> ModuleImportDepth;
  bool NotRecommended;
  bool IsSystem;
  unsigned NumBytesToErase;

  struct IndexRange {
    unsigned begin = 0;
    unsigned end = 0;
    IndexRange() = default;
    IndexRange(unsigned begin, unsigned end) : begin(begin), end(end) {}
    unsigned length() const { return end - begin; }
    bool empty() const { return end == begin; }
  };

  struct ParameterStructure {
    IndexRange name;
    IndexRange afterColon;
    bool isLocalName = false;
    IndexRange range() const {
      if (!name.empty()) // if we have both, name comes before afterColon.
        return {name.begin, afterColon.empty() ? name.end : afterColon.end};
      return afterColon;
    }
  };

  struct DescriptionStructure {
    IndexRange baseName;
    IndexRange parameterRange;
  };

  Optional<DescriptionStructure> descriptionStructure;
  Optional<ArrayRef<ParameterStructure>> parametersStructure;
};

struct ExpressionType {
  unsigned ExprOffset;
  unsigned ExprLength;
  unsigned TypeOffset;
  std::vector<unsigned> ProtocolOffsets;
};

struct ExpressionTypesInFile {
  std::vector<ExpressionType> Results;
  StringRef TypeBuffer;
};

struct VariableType {
  /// The variable identifier's offset in the file.
  unsigned VarOffset;
  /// The variable identifier's length.
  unsigned VarLength;
  /// The offset of the type's string representation inside
  /// `VariableTypesInFile.TypeBuffer`.
  unsigned TypeOffset;
  /// Whether the variable declaration has an explicit type annotation.
  bool HasExplicitType;
};

struct VariableTypesInFile {
  /// The typed variable declarations in the file.
  std::vector<VariableType> Results;
  /// A String containing the printed representation of all types in
  /// `Results`. Entries in `Results` refer to their types by using
  /// an offset into this string.
  StringRef TypeBuffer;
};

class CodeCompletionConsumer {
  virtual void anchor();

public:
  virtual ~CodeCompletionConsumer() { }

  virtual void failed(StringRef ErrDescription) = 0;
  virtual void cancelled() = 0;

  virtual void setCompletionKind(UIdent kind) {};
  virtual void setReusingASTContext(bool) = 0;
  virtual void setAnnotatedTypename(bool) = 0;
  virtual bool handleResult(const CodeCompletionInfo &Info) = 0;
};

class GroupedCodeCompletionConsumer : public CodeCompletionConsumer {
public:
  virtual void startGroup(UIdent kind, StringRef name) = 0;
  virtual void endGroup() = 0;
  virtual void setNextRequestStart(unsigned offset) = 0;
};

struct CustomCompletionInfo {
  std::string Name;
  void *Kind;

  enum Context {
    Stmt = 1 << 0,
    Expr = 1 << 1,
    Type = 1 << 2,
    ForEachSequence = 1 << 3,
  };
  swift::OptionSet<Context> Contexts;
};

struct FilterRule {
  enum Kind {
    Everything,
    Module,
    Keyword,
    Literal,
    CustomCompletion,
    Identifier,
    Description,
  };
  Kind kind;
  bool hide;
  std::vector<StringRef> names; ///< Must be null-terminated.
  std::vector<UIdent> uids;
};

enum class DiagnosticSeverityKind {
  Warning,
  Error
};

enum class DiagnosticCategory {
  Deprecation,
  NoUsage
};

struct DiagnosticEntryInfoBase {
  struct Fixit {
    unsigned Offset;
    unsigned Length;
    std::string Text;
  };

  std::string ID;
  std::string Description;
  unsigned Offset = 0;
  unsigned Line = 0;
  unsigned Column = 0;
  std::string Filename;
  SmallVector<DiagnosticCategory, 1> Categories;
  SmallVector<std::pair<unsigned, unsigned>, 2> Ranges;
  SmallVector<Fixit, 2> Fixits;
  SmallVector<std::string, 1> EducationalNotePaths;
};

struct DiagnosticEntryInfo : DiagnosticEntryInfoBase {
  DiagnosticSeverityKind Severity = DiagnosticSeverityKind::Error;
  SmallVector<DiagnosticEntryInfoBase, 1> Notes;
};

struct SourceFileRange {
  /// The byte offset at which the range begins
  uintptr_t Start;
  /// The byte offset at which the end ends
  uintptr_t End;
};

enum class SyntaxTreeTransferMode {
  /// Don't transfer the syntax tree
  Off,
  /// Transfer the entire syntax tree
  Full
};

class EditorConsumer {
  virtual void anchor();
public:
  virtual ~EditorConsumer() { }

  virtual bool needsSemanticInfo() { return true; }

  virtual void handleRequestError(const char *Description) = 0;

  virtual bool syntaxMapEnabled() = 0;
  virtual void handleSyntaxMap(unsigned Offset, unsigned Length,
                               UIdent Kind) = 0;

  virtual bool documentStructureEnabled() = 0;

  virtual void handleSemanticAnnotation(unsigned Offset, unsigned Length,
                                        UIdent Kind, bool isSystem) = 0;

  virtual void beginDocumentSubStructure(unsigned Offset, unsigned Length,
                                         UIdent Kind, UIdent AccessLevel,
                                         UIdent SetterAccessLevel,
                                         unsigned NameOffset,
                                         unsigned NameLength,
                                         unsigned BodyOffset,
                                         unsigned BodyLength,
                                         unsigned DocOffset,
                                         unsigned DocLength,
                                         StringRef DisplayName,
                                         StringRef TypeName,
                                         StringRef RuntimeName,
                                         StringRef SelectorName,
                                         ArrayRef<StringRef> InheritedTypes,
                                         ArrayRef<std::tuple<UIdent, unsigned, unsigned>> Attrs) = 0;

  virtual void endDocumentSubStructure() = 0;

  virtual void handleDocumentSubStructureElement(UIdent Kind, unsigned Offset,
                                                 unsigned Length) = 0;

  virtual void recordAffectedRange(unsigned Offset, unsigned Length) = 0;

  virtual void recordAffectedLineRange(unsigned Line, unsigned Length) = 0;

  virtual void recordFormattedText(StringRef Text) = 0;

  virtual bool diagnosticsEnabled() = 0;

  virtual void setDiagnosticStage(UIdent DiagStage) = 0;
  virtual void handleDiagnostic(const DiagnosticEntryInfo &Info,
                                UIdent DiagStage) = 0;

  virtual void handleSourceText(StringRef Text) = 0;

  virtual void
  handleSyntaxTree(const swift::syntax::SourceFileSyntax &SyntaxTree) = 0;
  virtual bool syntaxTreeEnabled() {
    return syntaxTreeTransferMode() != SyntaxTreeTransferMode::Off;
  }
  virtual SyntaxTreeTransferMode syntaxTreeTransferMode() = 0;

  virtual void finished() {}
};

class OptionsDictionary {
  virtual void anchor();
public:
  virtual ~OptionsDictionary() {}

  virtual bool valueForOption(UIdent Key, unsigned &Val) = 0;
  virtual bool valueForOption(UIdent Key, bool &Val) = 0;
  virtual bool valueForOption(UIdent Key, StringRef &Val) = 0;
  virtual bool forEach(UIdent key, llvm::function_ref<bool(OptionsDictionary &)> applier) = 0;
};

struct Statistic;
typedef std::function<void(ArrayRef<Statistic *> stats)> StatisticsReceiver;

/// Options for configuring a virtual file system provider.
struct VFSOptions {
  /// The name of the virtual file system to use.
  std::string name;

  /// Arguments for the virtual file system provider (may be null).
  // FIXME: the lifetime is actually limited by the RequestDict.
  std::unique_ptr<OptionsDictionary> options;
};

/// Used to wrap the result of a request. There are three possibilities:
/// - The request succeeded (`value` is valid)
/// - The request was cancelled
/// - The request failed (with an `error`)
///
/// NOTE: This type does not own its `value` or `error`. Therefore, it's not
/// safe to store this type, nor is it safe to store its `value` or `error`.
/// Instead, any needed information should be fetched and stored (e.g. reading
/// properties from `value` or getting a `std::string` from `error`).
template <typename T>
class RequestResult {
  enum Type {
    Value,
    Error,
    Cancelled
  };
  union {
    const T *data;
    StringRef error;
  };
  RequestResult::Type type;

  RequestResult(const T &V): data(&V), type(Value) {}
  RequestResult(StringRef E): error(E), type(Error) {}
  RequestResult(): type(Cancelled) {}

public:
  static RequestResult fromResult(const T &value) {
    return RequestResult(value);
  }
  static RequestResult fromError(StringRef error) {
    return RequestResult(error);
  }
  static RequestResult cancelled() {
    return RequestResult();
  }

  const T &value() const {
    assert(type == Value);
    return *data;
  }
  bool isError() const {
    return type == Error;
  }
  StringRef getError() const {
    assert(type == Error);
    return error;
  }
  bool isCancelled() const {
    return type == Cancelled;
  }
};

struct RefactoringInfo {
  UIdent Kind;
  StringRef KindName;
  StringRef UnavailableReason;

  RefactoringInfo(UIdent Kind, StringRef KindName, StringRef UnavailableReason)
      : Kind(Kind), KindName(KindName), UnavailableReason(UnavailableReason) {}
};

struct ParentInfo {
  StringRef Title;
  StringRef KindName;
  StringRef USR;

  ParentInfo(StringRef Title, StringRef KindName, StringRef USR)
      : Title(Title), KindName(KindName), USR(USR) {}
};

struct ReferencedDeclInfo {
  StringRef USR;
  UIdent DeclarationLang;
  StringRef AccessLevel;
  StringRef FilePath;
  StringRef ModuleName;
  bool IsSystem;
  bool IsSPI;
  ArrayRef<ParentInfo> ParentContexts;

  ReferencedDeclInfo(StringRef USR, UIdent DeclLang, StringRef AccessLevel,
                     StringRef FilePath, StringRef ModuleName, bool System,
                     bool SPI, ArrayRef<ParentInfo> Parents)
      : USR(USR), DeclarationLang(DeclLang), AccessLevel(AccessLevel),
        FilePath(FilePath), ModuleName(ModuleName), IsSystem(System),
        IsSPI(SPI), ParentContexts(Parents) {}
};

struct LocationInfo {
  StringRef Filename;
  unsigned Offset = 0;
  unsigned Length = 0;
  unsigned Line = 0;
  unsigned Column = 0;
};

struct CursorSymbolInfo {
  UIdent Kind;
  UIdent DeclarationLang;
  StringRef Name;
  StringRef USR;
  StringRef TypeName;
  StringRef TypeUSR;
  StringRef ContainerTypeUSR;
  StringRef DocComment;
  StringRef GroupName;
  /// A key for documentation comment localization, if it exists in the doc
  /// comment for the declaration.
  StringRef LocalizationKey;
  /// Annotated XML pretty printed declaration.
  StringRef AnnotatedDeclaration;
  /// Fully annotated XML pretty printed declaration.
  /// FIXME: this should eventually replace \c AnnotatedDeclaration.
  StringRef FullyAnnotatedDeclaration;
  /// The SymbolGraph JSON for this declaration.
  StringRef SymbolGraph;
  /// Non-empty if the symbol was imported from a clang module.
  StringRef ModuleName;
  /// Non-empty if a generated interface editor document has previously been
  /// opened for the module the symbol came from.
  StringRef ModuleInterfaceName;
  /// Filename is non-empty if there's a source location.
  LocationInfo Location;
  /// For methods this lists the USRs of the overrides in the class hierarchy.
  ArrayRef<StringRef> OverrideUSRs;
  /// Related declarations, overloaded functions etc., in annotated XML form.
  ArrayRef<StringRef> AnnotatedRelatedDeclarations;
  /// All groups of the module name under cursor.
  ArrayRef<StringRef> ModuleGroupArray;
  /// Stores the Symbol Graph title, kind, and USR of the parent contexts of the
  /// symbol under the cursor.
  ArrayRef<ParentInfo> ParentContexts;
  /// The set of decls referenced in the symbol graph declaration fragments.
  ArrayRef<ReferencedDeclInfo> ReferencedSymbols;
  /// For calls this lists the USRs of the receiver types (multiple only in the
  /// case that the base is a protocol composition).
  ArrayRef<StringRef> ReceiverUSRs;

  bool IsSystem = false;
  bool IsDynamic = false;
  bool IsSynthesized = false;

  llvm::Optional<unsigned> ParentNameOffset;
};

struct CursorInfoData {
  // If nonempty, a proper Info could not be resolved (and the rest of the Info
  // will be empty). Clients can potentially use this to show a diagnostic
  // message to the user in lieu of using the empty response.
  StringRef InternalDiagnostic;
  llvm::ArrayRef<CursorSymbolInfo> Symbols;
  /// All available actions on the code under cursor.
  llvm::ArrayRef<RefactoringInfo> AvailableActions;
};

/// The result type of `LangSupport::getDiagnostics`
typedef ArrayRef<DiagnosticEntryInfo> DiagnosticsResult;

struct RangeInfo {
  UIdent RangeKind;
  StringRef ExprType;
  StringRef RangeContent;
};

struct NameTranslatingInfo {
  // If nonempty, a proper Info could not be resolved (and the rest of the Info
  // will be empty). Clients can potentially use this to show a diagnostic
  // message to the user in lieu of using the empty response.
  StringRef InternalDiagnostic;

  UIdent NameKind;
  StringRef BaseName;
  std::vector<StringRef> ArgNames;
  bool IsZeroArgSelector = false;
};

enum class SemanticRefactoringKind {
  None,
#define SEMANTIC_REFACTORING(KIND, NAME, ID) KIND,
#include "swift/IDE/RefactoringKinds.def"
};

struct SemanticRefactoringInfo {
  SemanticRefactoringKind Kind;
  unsigned Line;
  unsigned Column;
  unsigned Length;
  StringRef PreferredName;
};

struct RelatedIdentsInfo {
  /// (Offset,Length) pairs.
  ArrayRef<std::pair<unsigned, unsigned>> Ranges;
};

/// Filled out by LangSupport::findInterfaceDocument().
struct InterfaceDocInfo {
  /// Non-empty if a generated interface editor document has previously been
  /// opened for the requested module name.
  StringRef ModuleInterfaceName;
  /// The subset of compiler arguments that are relevant for the interface
  /// generation.
  ArrayRef<StringRef> CompilerArgs;
};

struct DocGenericParam {
  std::string Name;
  std::string Inherits;
};

struct DocEntityInfo {
  UIdent Kind;
  llvm::SmallString<32> Name;
  llvm::SmallString<32> SubModuleName;
  llvm::SmallString<32> Argument;
  llvm::SmallString<64> USR;
  llvm::SmallString<64> OriginalUSR;
  llvm::SmallString<64> ProvideImplementationOfUSR;
  llvm::SmallString<64> DocComment;
  llvm::SmallString<64> FullyAnnotatedDecl;
  llvm::SmallString<64> FullyAnnotatedGenericSig;
  llvm::SmallString<64> LocalizationKey;
  std::vector<DocGenericParam> GenericParams;
  std::vector<std::string> GenericRequirements;
  std::vector<std::string> RequiredBystanders;
  unsigned Offset = 0;
  unsigned Length = 0;
  bool IsUnavailable = false;
  bool IsDeprecated = false;
  bool IsOptional = false;
  bool IsAsync = false;
  swift::Type Ty;
};

struct AvailableAttrInfo {
  UIdent AttrKind;
  bool IsUnavailable = false;
  bool IsDeprecated = false;
  UIdent Platform;
  llvm::SmallString<32> Message;
  llvm::Optional<llvm::VersionTuple> Introduced;
  llvm::Optional<llvm::VersionTuple> Deprecated;
  llvm::Optional<llvm::VersionTuple> Obsoleted;
};

struct NoteRegion {
  UIdent Kind;
  unsigned StartLine;
  unsigned StartColumn;
  unsigned EndLine;
  unsigned EndColumn;
  llvm::Optional<unsigned> ArgIndex;
};

struct Edit {
  unsigned StartLine;
  unsigned StartColumn;
  unsigned EndLine;
  unsigned EndColumn;
  std::string NewText;
  SmallVector<NoteRegion, 2> RegionsWithNote;
};

struct CategorizedEdits {
  UIdent Category;
  ArrayRef<Edit> Edits;
};

struct RenameRangeDetail {
  unsigned StartLine;
  unsigned StartColumn;
  unsigned EndLine;
  unsigned EndColumn;
  UIdent Kind;
  Optional<unsigned> ArgIndex;
};

struct CategorizedRenameRanges {
  UIdent Category;
  std::vector<RenameRangeDetail> Ranges;
};

enum class RenameType {
  Unknown,
  Definition,
  Reference,
  Call
};

struct RenameLocation {
  unsigned Line;
  unsigned Column;
  RenameType Type;
};

struct RenameLocations {
  StringRef OldName;
  StringRef NewName;
  const bool IsFunctionLike;
  const bool IsNonProtocolType;
  std::vector<RenameLocation> LineColumnLocs;
};

typedef std::function<void(RequestResult<ArrayRef<CategorizedEdits>> Result)>
    CategorizedEditsReceiver;
typedef std::function<void(RequestResult<ArrayRef<CategorizedRenameRanges>> Result)>
    CategorizedRenameRangesReceiver;

class DocInfoConsumer {
  virtual void anchor();

public:
  virtual ~DocInfoConsumer() { }

  virtual void failed(StringRef ErrDescription) = 0;

  virtual bool handleSourceText(StringRef Text) = 0;

  virtual bool handleAnnotation(const DocEntityInfo &Info) = 0;

  virtual bool startSourceEntity(const DocEntityInfo &Info) = 0;

  virtual bool handleInheritsEntity(const DocEntityInfo &Info) = 0;
  virtual bool handleConformsToEntity(const DocEntityInfo &Info) = 0;
  virtual bool handleExtendsEntity(const DocEntityInfo &Info) = 0;

  virtual bool handleAvailableAttribute(const AvailableAttrInfo &Info) = 0;

  virtual bool finishSourceEntity(UIdent Kind) = 0;

  virtual bool handleDiagnostic(const DiagnosticEntryInfo &Info) = 0;
};

struct TypeContextInfoItem {
  StringRef TypeName;
  StringRef TypeUSR;

  struct Member {
    StringRef Name;
    StringRef Description;
    StringRef SourceText;
    StringRef DocBrief;
  };
  ArrayRef<Member> ImplicitMembers;
};

class TypeContextInfoConsumer {
  virtual void anchor();

public:
  virtual ~TypeContextInfoConsumer() {}

  virtual void handleResult(const TypeContextInfoItem &Result) = 0;
  virtual void failed(StringRef ErrDescription) = 0;
  virtual void cancelled() = 0;
  virtual void setReusingASTContext(bool flag) = 0;
};

struct ConformingMethodListResult {
  StringRef TypeName;
  StringRef TypeUSR;

  struct Member {
    StringRef Name;
    StringRef TypeName;
    StringRef TypeUSR;
    StringRef Description;
    StringRef SourceText;
    StringRef DocBrief;
  };
  ArrayRef<Member> Members;
};

class ConformingMethodListConsumer {
  virtual void anchor();

public:
  virtual ~ConformingMethodListConsumer() {}

  virtual void handleResult(const ConformingMethodListResult &Result) = 0;
  virtual void setReusingASTContext(bool flag) = 0;
  virtual void failed(StringRef ErrDescription) = 0;
  virtual void cancelled() = 0;
};

class LangSupport {
  virtual void anchor();

public:
  /// A separator between parts in a synthesized usr.
  const static std::string SynthesizedUSRSeparator;

  virtual ~LangSupport() { }

  virtual void globalConfigurationUpdated(std::shared_ptr<GlobalConfig> Config) {};

  virtual void dependencyUpdated() {}

  virtual void indexSource(StringRef Filename,
                           IndexingConsumer &Consumer,
                           ArrayRef<const char *> Args) = 0;

  virtual void codeComplete(llvm::MemoryBuffer *InputBuf, unsigned Offset,
                            OptionsDictionary *options,
                            CodeCompletionConsumer &Consumer,
                            ArrayRef<const char *> Args,
                            Optional<VFSOptions> vfsOptions,
                            SourceKitCancellationToken CancellationToken) = 0;

  virtual void
  codeCompleteOpen(StringRef name, llvm::MemoryBuffer *inputBuf,
                   unsigned offset, OptionsDictionary *options,
                   ArrayRef<FilterRule> filterRules,
                   GroupedCodeCompletionConsumer &consumer,
                   ArrayRef<const char *> args, Optional<VFSOptions> vfsOptions,
                   SourceKitCancellationToken CancellationToken) = 0;

  virtual void codeCompleteClose(StringRef name, unsigned offset,
                                 GroupedCodeCompletionConsumer &consumer) = 0;

  virtual void codeCompleteUpdate(StringRef name, unsigned offset,
                                  OptionsDictionary *options,
                                  SourceKitCancellationToken CancellationToken,
                                  GroupedCodeCompletionConsumer &consumer) = 0;

  virtual void codeCompleteCacheOnDisk(StringRef path) = 0;

  virtual void
  codeCompleteSetPopularAPI(ArrayRef<const char *> popularAPI,
                            ArrayRef<const char *> unpopularAPI) = 0;

  virtual void
  codeCompleteSetCustom(ArrayRef<CustomCompletionInfo> completions) = 0;

  virtual void
  editorOpen(StringRef Name, llvm::MemoryBuffer *Buf, EditorConsumer &Consumer,
             ArrayRef<const char *> Args, Optional<VFSOptions> vfsOptions) = 0;

  virtual void editorOpenInterface(EditorConsumer &Consumer,
                                   StringRef Name,
                                   StringRef ModuleName,
                                   Optional<StringRef> Group,
                                   ArrayRef<const char *> Args,
                                   bool SynthesizedExtensions,
                                   Optional<StringRef> InterestedUSR) = 0;

  virtual void editorOpenTypeInterface(EditorConsumer &Consumer,
                                       ArrayRef<const char *> Args,
                                       StringRef TypeUSR) = 0;

  virtual void editorOpenHeaderInterface(EditorConsumer &Consumer,
                                         StringRef Name,
                                         StringRef HeaderName,
                                         ArrayRef<const char *> Args,
                                         bool UsingSwiftArgs,
                                         bool SynthesizedExtensions,
                                         StringRef swiftVersion) = 0;

  virtual void
  editorOpenSwiftSourceInterface(StringRef Name, StringRef SourceName,
                                 ArrayRef<const char *> Args,
                                 SourceKitCancellationToken CancellationToken,
                                 std::shared_ptr<EditorConsumer> Consumer) = 0;

  virtual void editorClose(StringRef Name, bool RemoveCache) = 0;

  virtual void editorReplaceText(StringRef Name, llvm::MemoryBuffer *Buf,
                                 unsigned Offset, unsigned Length,
                                 EditorConsumer &Consumer) = 0;

  virtual void editorApplyFormatOptions(StringRef Name,
                                        OptionsDictionary &FmtOptions) = 0;

  virtual void editorFormatText(StringRef Name, unsigned Line, unsigned Length,
                                EditorConsumer &Consumer) = 0;

  virtual void editorExtractTextFromComment(StringRef Source,
                                            EditorConsumer &Consumer) = 0;

  virtual void editorConvertMarkupToXML(StringRef Source,
                                        EditorConsumer &Consumer) = 0;

  virtual void editorExpandPlaceholder(StringRef Name, unsigned Offset,
                                       unsigned Length,
                                       EditorConsumer &Consumer) = 0;

  virtual void getCursorInfo(
      StringRef Filename, unsigned Offset, unsigned Length, bool Actionables,
      bool SymbolGraph, bool CancelOnSubsequentRequest,
      ArrayRef<const char *> Args, Optional<VFSOptions> vfsOptions,
      SourceKitCancellationToken CancellationToken,
      std::function<void(const RequestResult<CursorInfoData> &)> Receiver) = 0;

  virtual void
  getDiagnostics(StringRef InputFile, ArrayRef<const char *> Args,
                 Optional<VFSOptions> VfsOptions,
                 SourceKitCancellationToken CancellationToken,
                 std::function<void(const RequestResult<DiagnosticsResult> &)>
                     Receiver) = 0;

  virtual void
  getNameInfo(StringRef Filename, unsigned Offset, NameTranslatingInfo &Input,
              ArrayRef<const char *> Args,
              SourceKitCancellationToken CancellationToken,
              std::function<void(const RequestResult<NameTranslatingInfo> &)>
                  Receiver) = 0;

  virtual void getRangeInfo(
      StringRef Filename, unsigned Offset, unsigned Length,
      bool CancelOnSubsequentRequest, ArrayRef<const char *> Args,
      SourceKitCancellationToken CancellationToken,
      std::function<void(const RequestResult<RangeInfo> &)> Receiver) = 0;

  virtual void getCursorInfoFromUSR(
      StringRef Filename, StringRef USR, bool CancelOnSubsequentRequest,
      ArrayRef<const char *> Args, Optional<VFSOptions> vfsOptions,
      SourceKitCancellationToken CancellationToken,
      std::function<void(const RequestResult<CursorInfoData> &)> Receiver) = 0;

  virtual void findRelatedIdentifiersInFile(
      StringRef Filename, unsigned Offset, bool CancelOnSubsequentRequest,
      ArrayRef<const char *> Args, SourceKitCancellationToken CancellationToken,
      std::function<void(const RequestResult<RelatedIdentsInfo> &)>
          Receiver) = 0;

  virtual llvm::Optional<std::pair<unsigned, unsigned>>
      findUSRRange(StringRef DocumentName, StringRef USR) = 0;

  virtual void findInterfaceDocument(StringRef ModuleName,
                                     ArrayRef<const char *> Args,
                    std::function<void(const RequestResult<InterfaceDocInfo> &)> Receiver) = 0;

  virtual void findModuleGroups(StringRef ModuleName,
                                ArrayRef<const char *> Args,
                                std::function<void(const RequestResult<ArrayRef<StringRef>> &)> Receiver) = 0;

  virtual void syntacticRename(llvm::MemoryBuffer *InputBuf,
                               ArrayRef<RenameLocations> RenameLocations,
                               ArrayRef<const char*> Args,
                               CategorizedEditsReceiver Receiver) = 0;

  virtual void findRenameRanges(llvm::MemoryBuffer *InputBuf,
                                ArrayRef<RenameLocations> RenameLocations,
                                ArrayRef<const char *> Args,
                                CategorizedRenameRangesReceiver Receiver) = 0;
  virtual void
  findLocalRenameRanges(StringRef Filename, unsigned Line, unsigned Column,
                        unsigned Length, ArrayRef<const char *> Args,
                        SourceKitCancellationToken CancellationToken,
                        CategorizedRenameRangesReceiver Receiver) = 0;

  virtual void semanticRefactoring(StringRef Filename,
                                   SemanticRefactoringInfo Info,
                                   ArrayRef<const char *> Args,
                                   SourceKitCancellationToken CancellationToken,
                                   CategorizedEditsReceiver Receiver) = 0;

  virtual void collectExpressionTypes(
      StringRef FileName, ArrayRef<const char *> Args,
      ArrayRef<const char *> ExpectedProtocols, bool CanonicalType,
      SourceKitCancellationToken CancellationToken,
      std::function<void(const RequestResult<ExpressionTypesInFile> &)>
          Receiver) = 0;

  /// Collects variable types for a range defined by `Offset` and `Length` in
  /// the source file. If `Offset` or `Length` are empty, variable types for
  /// the entire document are collected.
  virtual void collectVariableTypes(
      StringRef FileName, ArrayRef<const char *> Args,
      Optional<unsigned> Offset, Optional<unsigned> Length,
      SourceKitCancellationToken CancellationToken,
      std::function<void(const RequestResult<VariableTypesInFile> &)>
          Receiver) = 0;

  virtual void getDocInfo(llvm::MemoryBuffer *InputBuf,
                          StringRef ModuleName,
                          ArrayRef<const char *> Args,
                          DocInfoConsumer &Consumer) = 0;

  virtual void getExpressionContextInfo(
      llvm::MemoryBuffer *inputBuf, unsigned Offset, OptionsDictionary *options,
      ArrayRef<const char *> Args, SourceKitCancellationToken CancellationToken,
      TypeContextInfoConsumer &Consumer, Optional<VFSOptions> vfsOptions) = 0;

  virtual void getConformingMethodList(
      llvm::MemoryBuffer *inputBuf, unsigned Offset, OptionsDictionary *options,
      ArrayRef<const char *> Args, ArrayRef<const char *> ExpectedTypes,
      SourceKitCancellationToken CancellationToken,
      ConformingMethodListConsumer &Consumer,
      Optional<VFSOptions> vfsOptions) = 0;

  virtual void getStatistics(StatisticsReceiver) = 0;
};
} // namespace SourceKit

#endif
