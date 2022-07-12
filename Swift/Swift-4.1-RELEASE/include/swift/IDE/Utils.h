//===--- Utils.h - Misc utilities -------------------------------*- C++ -*-===//
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

#ifndef SWIFT_IDE_UTILS_H
#define SWIFT_IDE_UTILS_H

#include "llvm/ADT/PointerIntPair.h"
#include "swift/Basic/LLVM.h"
#include "swift/AST/ASTNode.h"
#include "swift/AST/DeclNameLoc.h"
#include "swift/AST/Module.h"
#include "swift/AST/ASTPrinter.h"
#include "swift/IDE/SourceEntityWalker.h"
#include "swift/Parse/Token.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <string>
#include <functional>
#include <vector>

namespace llvm {
  template<typename Fn> class function_ref;
  class MemoryBuffer;
}

namespace clang {
  class Module;
  class NamedDecl;
}

namespace swift {
  class ModuleDecl;
  class ValueDecl;
  class ASTContext;
  class CompilerInvocation;
  class SourceFile;
  class TypeDecl;
  class SourceLoc;
  class Type;
  class Decl;
  class DeclContext;
  class ClangNode;
  class ClangImporter;
  class Token;

namespace ide {
struct SourceCompleteResult {
  // Set to true if the input source is fully formed, false otherwise.
  bool IsComplete;
  // The text to use as the indent string when auto indenting the next line.
  // This will contain the exactly what the client typed (any whitespaces and
  // tabs) and can be used to indent subsequent lines. It does not include
  // the current indent level, IDE clients should insert the correct indentation
  // with spaces or tabs to account for the current indent level. The indent
  // prefix will contain the leading space characters of the line that
  // contained the '{', '(' or '[' character that was unbalanced.
  std::string IndentPrefix;
  // Returns the indent level as an indentation count (number of indentations
  // to apply). Clients can translate this into the standard indentation that
  // is being used by the IDE (3 spaces? 1 tab?) and should use the indent
  // prefix string followed by the correct indentation.
  uint32_t IndentLevel;

  SourceCompleteResult() :
      IsComplete(false),
      IndentPrefix(),
      IndentLevel(0) {}
};

SourceCompleteResult
isSourceInputComplete(std::unique_ptr<llvm::MemoryBuffer> MemBuf);
SourceCompleteResult isSourceInputComplete(StringRef Text);

bool initInvocationByClangArguments(ArrayRef<const char *> ArgList,
                                    CompilerInvocation &Invok,
                                    std::string &Error);

/// Visits all overridden declarations exhaustively from VD, including protocol
/// conformances and clang declarations.
void walkOverriddenDecls(const ValueDecl *VD,
                         std::function<void(llvm::PointerUnion<
                             const ValueDecl*, const clang::NamedDecl*>)> Fn);

void collectModuleNames(StringRef SDKPath, std::vector<std::string> &Modules);

std::string getSDKName(StringRef Path);

std::string getSDKVersion(StringRef Path);

struct PlaceholderOccurrence {
  /// The complete placeholder string.
  StringRef FullPlaceholder;
  /// The inner string of the placeholder.
  StringRef PlaceholderContent;
  /// The dollar identifier that was used to replace the placeholder.
  StringRef IdentifierReplacement;
};

/// Replaces Xcode editor placeholders (<#such as this#>) with dollar
/// identifiers and returns a new memory buffer.
///
/// The replacement identifier will be the same size as the placeholder so that
/// the new buffer will have the same size as the input buffer.
std::unique_ptr<llvm::MemoryBuffer>
  replacePlaceholders(std::unique_ptr<llvm::MemoryBuffer> InputBuf,
              llvm::function_ref<void(const PlaceholderOccurrence &)> Callback);

std::unique_ptr<llvm::MemoryBuffer>
  replacePlaceholders(std::unique_ptr<llvm::MemoryBuffer> InputBuf,
                      bool *HadPlaceholder = nullptr);

void getLocationInfo(
    const ValueDecl *VD,
    llvm::Optional<std::pair<unsigned, unsigned>> &DeclarationLoc,
    StringRef &Filename);

void getLocationInfoForClangNode(ClangNode ClangNode,
                                 ClangImporter *Importer,
       llvm::Optional<std::pair<unsigned, unsigned>> &DeclarationLoc,
                                 StringRef &Filename);

Optional<std::pair<unsigned, unsigned>> parseLineCol(StringRef LineCol);

Decl *getDeclFromUSR(ASTContext &context, StringRef USR, std::string &error);
Decl *getDeclFromMangledSymbolName(ASTContext &context, StringRef mangledName,
                                   std::string &error);

Type getTypeFromMangledTypename(ASTContext &Ctx, StringRef mangledName,
                                std::string &error);

Type getTypeFromMangledSymbolname(ASTContext &Ctx, StringRef mangledName,
                                  std::string &error);

class XMLEscapingPrinter : public StreamPrinter {
  public:
  XMLEscapingPrinter(raw_ostream &OS) : StreamPrinter(OS){};
  void printText(StringRef Text) override;
  void printXML(StringRef Text);
};

enum class CursorInfoKind {
  Invalid,
  ValueRef,
  ModuleRef,
  ExprStart,
  StmtStart,
};

struct ResolvedCursorInfo {
  CursorInfoKind Kind = CursorInfoKind::Invalid;
  SourceFile *SF;
  SourceLoc Loc;
  ValueDecl *ValueD = nullptr;
  TypeDecl *CtorTyRef = nullptr;
  ExtensionDecl *ExtTyRef = nullptr;
  ModuleEntity Mod;
  bool IsRef = true;
  bool IsKeywordArgument = false;
  Type Ty;
  DeclContext *DC = nullptr;
  Type ContainerType;
  Stmt *TrailingStmt = nullptr;
  Expr *TrailingExpr = nullptr;

  ResolvedCursorInfo() = default;
  ResolvedCursorInfo(SourceFile *SF) : SF(SF) {}
  
  void setValueRef(ValueDecl *ValueD,
                   TypeDecl *CtorTyRef,
                   ExtensionDecl *ExtTyRef,
                   bool IsRef,
                   Type Ty,
                   Type ContainerType) {
    Kind = CursorInfoKind::ValueRef;
    this->ValueD = ValueD;
    this->CtorTyRef = CtorTyRef;
    this->ExtTyRef = ExtTyRef;
    this->IsRef = IsRef;
    this->Ty = Ty;
    this->DC = ValueD->getDeclContext();
    this->ContainerType = ContainerType;
  }
  void setModuleRef(ModuleEntity Mod) {
    Kind = CursorInfoKind::ModuleRef;
    this->Mod = Mod;
  }
  void setTrailingStmt(Stmt *TrailingStmt) {
    Kind = CursorInfoKind::StmtStart;
    this->TrailingStmt = TrailingStmt;
  }
  void setTrailingExpr(Expr* TrailingExpr) {
    Kind = CursorInfoKind::ExprStart;
    this->TrailingExpr = TrailingExpr;
  }

  bool isValid() const { return !isInvalid(); }
  bool isInvalid() const { return Kind == CursorInfoKind::Invalid; }
};

class CursorInfoResolver : public SourceEntityWalker {
  SourceFile &SrcFile;
  SourceLoc LocToResolve;
  ResolvedCursorInfo CursorInfo;
  Type ContainerType;
  llvm::SmallVector<Expr*, 4> TrailingExprStack;

public:
  explicit CursorInfoResolver(SourceFile &SrcFile) :
    SrcFile(SrcFile), CursorInfo(&SrcFile) {}
  ResolvedCursorInfo resolve(SourceLoc Loc);
  SourceManager &getSourceMgr() const;
private:
  bool walkToExprPre(Expr *E) override;
  bool walkToExprPost(Expr *E) override;
  bool walkToDeclPre(Decl *D, CharSourceRange Range) override;
  bool walkToDeclPost(Decl *D) override;
  bool walkToStmtPre(Stmt *S) override;
  bool walkToStmtPost(Stmt *S) override;
  bool visitDeclReference(ValueDecl *D, CharSourceRange Range,
                          TypeDecl *CtorTyRef, ExtensionDecl *ExtTyRef, Type T,
                          ReferenceMetaData Data) override;
  bool visitCallArgName(Identifier Name, CharSourceRange Range,
                        ValueDecl *D) override;
  bool visitDeclarationArgumentName(Identifier Name, SourceLoc StartLoc,
                                    ValueDecl *D) override;
  bool visitModuleReference(ModuleEntity Mod, CharSourceRange Range) override;
  bool rangeContainsLoc(SourceRange Range) const;
  bool rangeContainsLoc(CharSourceRange Range) const;
  bool isDone() const { return CursorInfo.isValid(); }
  bool tryResolve(ValueDecl *D, TypeDecl *CtorTyRef, ExtensionDecl *ExtTyRef,
                  SourceLoc Loc, bool IsRef, Type Ty = Type());
  bool tryResolve(ModuleEntity Mod, SourceLoc Loc);
  bool tryResolve(Stmt *St);
  bool visitSubscriptReference(ValueDecl *D, CharSourceRange Range,
                               Optional<AccessKind> AccKind,
                               bool IsOpenBracket) override;
};

struct UnresolvedLoc {
  SourceLoc Loc;
  bool ResolveArgLocs;
};

enum class LabelRangeType {
  None,
  CallArg,    // foo([a: ]2) or .foo([a: ]String)
  Param,  // func([a b]: Int)
  NoncollapsibleParam, // subscript([a a]: Int)
  Selector,   // #selector(foo.func([a]:))
};

struct ResolvedLoc {
  ASTWalker::ParentTy Node;
  CharSourceRange Range;
  std::vector<CharSourceRange> LabelRanges;
  LabelRangeType LabelType;
  bool IsActive;
  bool IsInSelector;
};


/// Finds the parse-only AST nodes and corresponding name and param/argument
/// label ranges for a given list of input name start locations
///
/// Resolved locations also indicate the nature of the matched occurrence (e.g.
/// whether it is within active/inactive code, or a selector or string literal).
class NameMatcher: public ASTWalker {
  SourceFile &SrcFile;
  std::vector<UnresolvedLoc> LocsToResolve;
  std::vector<ResolvedLoc> ResolvedLocs;
  ArrayRef<Token> TokensToCheck;
  unsigned InactiveConfigRegionNestings = 0;
  unsigned SelectorNestings = 0;

  SourceManager &getSourceMgr() const;

  SourceLoc nextLoc() const;
  bool isDone() const { return LocsToResolve.empty(); };
  bool isActive() const { return !InactiveConfigRegionNestings; };
  bool isInSelector() const { return SelectorNestings; };
  bool checkComments();
  void skipLocsBefore(SourceLoc Start);
  bool shouldSkip(Expr *E);
  bool shouldSkip(SourceRange Range);
  bool shouldSkip(CharSourceRange Range);
  bool tryResolve(ASTWalker::ParentTy Node, SourceLoc NameLoc);
  bool tryResolve(ASTWalker::ParentTy Node, DeclNameLoc NameLoc, Expr *Arg,
                  bool checkParentForLabels = false);
  bool tryResolve(ASTWalker::ParentTy Node, SourceLoc NameLoc, LabelRangeType RangeType,
                  ArrayRef<CharSourceRange> LabelLocs);

  std::pair<bool, Expr*> walkToExprPre(Expr *E) override;
  Expr* walkToExprPost(Expr *E) override;
  bool walkToDeclPre(Decl *D) override;
  bool walkToDeclPost(Decl *D) override;
  std::pair<bool, Stmt*> walkToStmtPre(Stmt *S) override;
  Stmt* walkToStmtPost(Stmt *S) override;
  bool walkToTypeLocPre(TypeLoc &TL) override;
  bool walkToTypeLocPost(TypeLoc &TL) override;
  bool walkToTypeReprPre(TypeRepr *T) override;
  bool walkToTypeReprPost(TypeRepr *T) override;
  std::pair<bool, Pattern*> walkToPatternPre(Pattern *P) override;
  bool shouldWalkIntoGenericParams() override { return true; }


public:
  explicit NameMatcher(SourceFile &SrcFile) : SrcFile(SrcFile) { }
  std::vector<ResolvedLoc> resolve(ArrayRef<UnresolvedLoc> Locs, ArrayRef<Token> Tokens);
};

enum class RangeKind : int8_t {
  Invalid = -1,
  SingleExpression,
  SingleStatement,
  SingleDecl,

  MultiStatement,
  PartOfExpression,

  MultiTypeMemberDecl,
};

struct DeclaredDecl {
  ValueDecl *VD;
  bool ReferredAfterRange;
  DeclaredDecl(ValueDecl* VD) : VD(VD), ReferredAfterRange(false) {}
  DeclaredDecl(): DeclaredDecl(nullptr) {}
  bool operator==(const DeclaredDecl& other);
};

struct ReferencedDecl {
  ValueDecl *VD;
  Type Ty;
  ReferencedDecl(ValueDecl* VD, Type Ty) : VD(VD), Ty(Ty) {}
  ReferencedDecl() : ReferencedDecl(nullptr, Type()) {}
};

enum class OrphanKind : int8_t {
  None,
  Break,
  Continue,
};

enum class ExitState: int8_t {
  Positive,
  Negative,
  Unsure,
};

struct ReturnInfo {
  TypeBase* ReturnType;
  ExitState Exit;
  ReturnInfo(): ReturnInfo(nullptr, ExitState::Unsure) {}
  ReturnInfo(TypeBase* ReturnType, ExitState Exit):
    ReturnType(ReturnType), Exit(Exit) {}
  ReturnInfo(ASTContext &Ctx, ArrayRef<ReturnInfo> Branches);
};

struct ResolvedRangeInfo {
  RangeKind Kind;
  ReturnInfo ExitInfo;
  ArrayRef<Token> TokensInRange;
  CharSourceRange ContentRange;
  bool HasSingleEntry;
  bool ThrowingUnhandledError;
  OrphanKind Orphan;

  // The topmost ast nodes contained in the given range.
  ArrayRef<ASTNode> ContainedNodes;
  ArrayRef<DeclaredDecl> DeclaredDecls;
  ArrayRef<ReferencedDecl> ReferencedDecls;
  DeclContext* RangeContext;
  Expr* CommonExprParent;

  ResolvedRangeInfo(RangeKind Kind, ReturnInfo ExitInfo,
                    ArrayRef<Token> TokensInRange,
                    DeclContext* RangeContext,
                    Expr *CommonExprParent, bool HasSingleEntry,
                    bool ThrowingUnhandledError,
                    OrphanKind Orphan, ArrayRef<ASTNode> ContainedNodes,
                    ArrayRef<DeclaredDecl> DeclaredDecls,
                    ArrayRef<ReferencedDecl> ReferencedDecls): Kind(Kind),
                      ExitInfo(ExitInfo),
                      TokensInRange(TokensInRange),
                      ContentRange(calculateContentRange(TokensInRange)),
                      HasSingleEntry(HasSingleEntry),
                      ThrowingUnhandledError(ThrowingUnhandledError),
                      Orphan(Orphan), ContainedNodes(ContainedNodes),
                      DeclaredDecls(DeclaredDecls),
                      ReferencedDecls(ReferencedDecls),
                      RangeContext(RangeContext),
                      CommonExprParent(CommonExprParent) {}
  ResolvedRangeInfo(ArrayRef<Token> TokensInRange) :
  ResolvedRangeInfo(RangeKind::Invalid, {nullptr, ExitState::Unsure},
                    TokensInRange, nullptr, /*Commom Expr Parent*/nullptr,
                    /*Single entry*/true, /*unhandled error*/false,
                    OrphanKind::None, {}, {}, {}) {}
  void print(llvm::raw_ostream &OS);
  ExitState exit() const { return ExitInfo.Exit; }
  Type getType() const { return ExitInfo.ReturnType; }

private:
  static CharSourceRange calculateContentRange(ArrayRef<Token> Tokens);
};

class RangeResolver : public SourceEntityWalker {
  struct Implementation;
  std::unique_ptr<Implementation> Impl;
  bool walkToExprPre(Expr *E) override;
  bool walkToExprPost(Expr *E) override;
  bool walkToStmtPre(Stmt *S) override;
  bool walkToStmtPost(Stmt *S) override;
  bool walkToDeclPre(Decl *D, CharSourceRange Range) override;
  bool walkToDeclPost(Decl *D) override;
  bool visitDeclReference(ValueDecl *D, CharSourceRange Range,
                          TypeDecl *CtorTyRef, ExtensionDecl *ExtTyRef, Type T,
                          ReferenceMetaData Data) override;
public:
  RangeResolver(SourceFile &File, SourceLoc Start, SourceLoc End);
  RangeResolver(SourceFile &File, unsigned Offset, unsigned Length);
  ~RangeResolver();
  ResolvedRangeInfo resolve();
};

/// This provides a utility to view a printed name by parsing the components
/// of that name. The components include a base name and an array of argument
/// labels.
class DeclNameViewer {
  StringRef BaseName;
  SmallVector<StringRef, 4> Labels;
  bool IsValid;
  bool HasParen;
public:
  DeclNameViewer(StringRef Text);
  DeclNameViewer() : DeclNameViewer(StringRef()) {}
  operator bool() const { return !BaseName.empty(); }
  StringRef base() const { return BaseName; }
  llvm::ArrayRef<StringRef> args() const { return llvm::makeArrayRef(Labels); }
  unsigned argSize() const { return Labels.size(); }
  unsigned partsCount() const { return 1 + Labels.size(); }
  unsigned commonPartsCount(DeclNameViewer &Other) const;
  bool isValid() const { return IsValid; }
  bool isFunction() const { return HasParen; }
};

/// This provide a utility for writing to an underlying string buffer multiple
/// string pieces and retrieve them later when the underlying buffer is stable.
class DelayedStringRetriever : public raw_ostream {
    SmallVectorImpl<char> &OS;
    llvm::raw_svector_ostream Underlying;
    SmallVector<std::pair<unsigned, unsigned>, 4> StartEnds;
    unsigned CurrentStart;

public:
    explicit DelayedStringRetriever(SmallVectorImpl<char> &OS) : OS(OS),
                                                              Underlying(OS) {}
    void startPiece() {
      CurrentStart = OS.size();
    }
    void endPiece() {
      StartEnds.emplace_back(CurrentStart, OS.size());
    }
    void write_impl(const char *ptr, size_t size) override {
      Underlying.write(ptr, size);
    }
    uint64_t current_pos() const override {
      return Underlying.tell();
    }
    size_t preferred_buffer_size() const override {
      return 0;
    }
    void retrieve(llvm::function_ref<void(StringRef)> F) const {
      for (auto P : StartEnds) {
        F(StringRef(OS.begin() + P.first, P.second - P.first));
      }
    }
    StringRef operator[](unsigned I) const {
      auto P = StartEnds[I];
      return StringRef(OS.begin() + P.first, P.second - P.first);
    }
};

enum class RegionType {
  Unmatched,
  Mismatch,
  ActiveCode,
  InactiveCode,
  String,
  Selector,
  Comment,
};

enum class RefactoringRangeKind {
  BaseName,                    // func [foo](a b: Int)
  KeywordBaseName,             // [init](a: Int)
  ParameterName,               // func foo(a[ b]: Int)
  NoncollapsibleParameterName, // subscript(a[ a]: Int)
  DeclArgumentLabel,           // func foo([a] b: Int)
  CallArgumentLabel,           // foo([a]: 1)
  CallArgumentColon,           // foo(a[: ]1)
  CallArgumentCombined,        // foo([]1) could expand to foo([a: ]1)
  SelectorArgumentLabel,       // foo([a]:)
};

struct NoteRegion {
  RefactoringRangeKind Kind;

  // The below are relative to the containing Replacement's Text
  unsigned StartLine;
  unsigned StartColumn;
  unsigned EndLine;
  unsigned EndColumn;
  Optional<unsigned> ArgIndex;
};

struct Replacement {
  CharSourceRange Range;
  StringRef Text;
  ArrayRef<NoteRegion> RegionsWorthNote;
};

class SourceEditConsumer {
public:
  virtual void accept(SourceManager &SM, RegionType RegionType, ArrayRef<Replacement> Replacements) = 0;
  virtual ~SourceEditConsumer() = default;
  void accept(SourceManager &SM, CharSourceRange Range, StringRef Text, ArrayRef<NoteRegion> SubRegions = {});
  void accept(SourceManager &SM, SourceLoc Loc, StringRef Text, ArrayRef<NoteRegion> SubRegions = {});
  void insertAfter(SourceManager &SM, SourceLoc Loc, StringRef Text, ArrayRef<NoteRegion> SubRegions = {});
  void accept(SourceManager &SM, Replacement Replacement) { accept(SM, RegionType::ActiveCode, {Replacement}); }
  void remove(SourceManager &SM, CharSourceRange Range);
};

/// This helper stream inserts text into a SourceLoc by calling functions in
/// SourceEditorConsumer when it is destroyed.
class EditorConsumerInsertStream: public raw_ostream {
  SourceEditConsumer &Consumer;
  SourceManager &SM;
  CharSourceRange Range;
  llvm::SmallString<64> Buffer;
  llvm::raw_svector_ostream OS;

public:
  explicit EditorConsumerInsertStream(SourceEditConsumer &Consumer,
                                      SourceManager &SM,
                                      CharSourceRange Range):
    Consumer(Consumer), SM(SM), Range(Range), Buffer(), OS(Buffer) {}

  explicit EditorConsumerInsertStream(SourceEditConsumer &Consumer,
                                      SourceManager &SM,
                                      SourceLoc Loc):
    EditorConsumerInsertStream(Consumer, SM, CharSourceRange(Loc, 0)) {}

  ~EditorConsumerInsertStream() {
    Consumer.accept(SM, Range, OS.str());
  }

  void write_impl(const char *ptr, size_t size) override {
    OS.write(ptr, size);
  }
  uint64_t current_pos() const override {
    return OS.tell();
  }
  size_t preferred_buffer_size() const override {
    return 0;
  }
};

class SourceEditJsonConsumer : public SourceEditConsumer {
  struct Implementation;
  Implementation &Impl;
public:
  SourceEditJsonConsumer(llvm::raw_ostream &OS);
  ~SourceEditJsonConsumer();
  void accept(SourceManager &SM, RegionType RegionType, ArrayRef<Replacement> Replacements) override;
};

class SourceEditOutputConsumer : public SourceEditConsumer {
  struct Implementation;
  Implementation &Impl;

public:
  SourceEditOutputConsumer(SourceManager &SM, unsigned BufferId, llvm::raw_ostream &OS);
  ~SourceEditOutputConsumer();
  void accept(SourceManager &SM, RegionType RegionType, ArrayRef<Replacement> Replacements) override;
};

enum class LabelRangeEndAt: int8_t {
  BeforeElemStart,
  LabelNameOnly,
};

struct CallArgInfo {
  Expr *ArgExp;
  CharSourceRange LabelRange;
  CharSourceRange getEntireCharRange(const SourceManager &SM) const;
};

std::vector<CallArgInfo>
getCallArgInfo(SourceManager &SM, Expr *Arg, LabelRangeEndAt EndKind);

// Get the ranges of argument labels from an Arg, either tuple or paren.
std::vector<CharSourceRange>
getCallArgLabelRanges(SourceManager &SM, Expr *Arg, LabelRangeEndAt EndKind);

} // namespace ide
} // namespace swift

#endif // SWIFT_IDE_UTILS_H

