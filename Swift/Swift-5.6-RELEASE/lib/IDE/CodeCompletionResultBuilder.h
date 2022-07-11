//===--- CodeCompletionResultBuilder.h - Build completion results ---------===//
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

#ifndef SWIFT_LIB_IDE_CODE_COMPLETION_RESULT_BUILDER_H
#define SWIFT_LIB_IDE_CODE_COMPLETION_RESULT_BUILDER_H

#include "swift/IDE/CodeCompletion.h"
#include "swift/AST/Types.h"
#include "swift/Basic/LLVM.h"
#include "swift/Basic/StringExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"

namespace clang {
class Module;
}

namespace {
class CodeCompletionStringPrinter;
}

namespace swift {
class Decl;
class ModuleDecl;

namespace ide {

/// The expected contextual type(s) for code-completion.
struct ExpectedTypeContext {
  /// Possible types of the code completion expression.
  llvm::SmallVector<Type, 4> possibleTypes;

  /// Pre typechecked type of the expression at the completion position.
  Type idealType;

  /// Whether the `ExpectedTypes` comes from a single-expression body, e.g.
  /// `foo({ here })`.
  ///
  /// Since the input may be incomplete, we take into account that the types are
  /// only a hint.
  bool isImplicitSingleExpressionReturn = false;
  bool preferNonVoid = false;

  bool empty() const { return possibleTypes.empty(); }
  bool requiresNonVoid() const {
    if (isImplicitSingleExpressionReturn)
      return false;
    if (preferNonVoid)
      return true;
    if (possibleTypes.empty())
      return false;
    return std::all_of(possibleTypes.begin(), possibleTypes.end(), [](Type Ty) {
      return !Ty->isVoid();
    });
  }

  ExpectedTypeContext() = default;
  ExpectedTypeContext(ArrayRef<Type> types, bool isImplicitSingleExprReturn)
      : possibleTypes(types.begin(), types.end()),
        isImplicitSingleExpressionReturn(isImplicitSingleExprReturn) {}
};

class CodeCompletionResultBuilder {
  friend CodeCompletionStringPrinter;
  
  CodeCompletionResultSink &Sink;
  CodeCompletionResult::ResultKind Kind;
  SemanticContextKind SemanticContext;
  CodeCompletionFlair Flair;
  unsigned NumBytesToErase = 0;
  const Decl *AssociatedDecl = nullptr;
  Optional<CodeCompletionLiteralKind> LiteralKind;
  CodeCompletionKeywordKind KeywordKind = CodeCompletionKeywordKind::None;
  unsigned CurrentNestingLevel = 0;
  SmallVector<CodeCompletionString::Chunk, 4> Chunks;
  llvm::PointerUnion<const ModuleDecl *, const clang::Module *>
      CurrentModule;
  ExpectedTypeContext declTypeContext;
  CodeCompletionResult::ExpectedTypeRelation ExpectedTypeRelation =
      CodeCompletionResult::ExpectedTypeRelation::Unknown;
  bool Cancelled = false;
  CodeCompletionResult::NotRecommendedReason NotRecReason =
      CodeCompletionResult::NotRecommendedReason::None;
  StringRef BriefDocComment;

  void addChunkWithText(CodeCompletionString::Chunk::ChunkKind Kind,
                        StringRef Text);

  void addChunkWithTextNoCopy(CodeCompletionString::Chunk::ChunkKind Kind,
                              StringRef Text) {
    Chunks.push_back(CodeCompletionString::Chunk::createWithText(
        Kind, CurrentNestingLevel, Text));
  }

  void addSimpleChunk(CodeCompletionString::Chunk::ChunkKind Kind) {
    Chunks.push_back(
        CodeCompletionString::Chunk::createSimple(Kind,
                                                  CurrentNestingLevel));
  }

  CodeCompletionString::Chunk &getLastChunk() {
    return Chunks.back();
  }

  CodeCompletionResult *takeResult();
  void finishResult();

public:
  CodeCompletionResultBuilder(CodeCompletionResultSink &Sink,
                              CodeCompletionResult::ResultKind Kind,
                              SemanticContextKind SemanticContext,
                              const ExpectedTypeContext &declTypeContext)
      : Sink(Sink), Kind(Kind), SemanticContext(SemanticContext),
        declTypeContext(declTypeContext) {}

  ~CodeCompletionResultBuilder() {
    finishResult();
  }

  void cancel() {
    Cancelled = true;
  }

  /// Annotated results are requested by the client.
  ///
  /// This affects the structure of the CodeCompletionString.
  bool shouldAnnotateResults() {
    return Sink.annotateResult;
  }

  void setNumBytesToErase(unsigned N) {
    NumBytesToErase = N;
  }

  void setAssociatedDecl(const Decl *D);

  void setLiteralKind(CodeCompletionLiteralKind kind) { LiteralKind = kind; }
  void setKeywordKind(CodeCompletionKeywordKind kind) { KeywordKind = kind; }
  void setNotRecommended(CodeCompletionResult::NotRecommendedReason Reason) {
    NotRecReason = Reason;
  }

  void setSemanticContext(SemanticContextKind Kind) {
    SemanticContext = Kind;
  }

  void addFlair(CodeCompletionFlair Options) {
    Flair |= Options;
  }

  void
  setExpectedTypeRelation(CodeCompletionResult::ExpectedTypeRelation relation) {
    ExpectedTypeRelation = relation;
  }

  void withNestedGroup(CodeCompletionString::Chunk::ChunkKind Kind,
                  llvm::function_ref<void()> body);

  void addAccessControlKeyword(AccessLevel Access) {
    switch (Access) {
    case AccessLevel::Private:
      addChunkWithTextNoCopy(
          CodeCompletionString::Chunk::ChunkKind::AccessControlKeyword,
          "private ");
      break;
    case AccessLevel::FilePrivate:
      addChunkWithTextNoCopy(
          CodeCompletionString::Chunk::ChunkKind::AccessControlKeyword,
          "fileprivate ");
      break;
    case AccessLevel::Internal:
      // 'internal' is the default, don't add it.
      break;
    case AccessLevel::Public:
      addChunkWithTextNoCopy(
          CodeCompletionString::Chunk::ChunkKind::AccessControlKeyword,
          "public ");
      break;
    case AccessLevel::Open:
      addChunkWithTextNoCopy(
          CodeCompletionString::Chunk::ChunkKind::AccessControlKeyword,
          "open ");
      break;
    }
  }

  void addRequiredKeyword() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::AccessControlKeyword,
        "required ");
  }

  void addOverrideKeyword() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::OverrideKeyword, "override ");
  }

  void addDeclIntroducer(StringRef Text) {
    addChunkWithText(CodeCompletionString::Chunk::ChunkKind::DeclIntroducer,
                     Text);
  }

  void addBaseName(StringRef Text) {
    addChunkWithText(CodeCompletionString::Chunk::ChunkKind::BaseName, Text);
  }

  void addKeyword(StringRef Text) {
    addChunkWithText(CodeCompletionString::Chunk::ChunkKind::Keyword, Text);
  }

  void addTextChunk(StringRef Text) {
    addChunkWithText(CodeCompletionString::Chunk::ChunkKind::Text, Text);
  }

  void addAnnotatedTextChunk(StringRef Text) {
    addTextChunk(Text);
    getLastChunk().setIsAnnotation();
  }

  void addAnnotatedThrows() {
    addThrows();
    getLastChunk().setIsAnnotation();
  }

  void addThrows() {
    addChunkWithTextNoCopy(
       CodeCompletionString::Chunk::ChunkKind::EffectsSpecifierKeyword,
       " throws");
  }

  void addAnnotatedAsync() {
    addAsync();
    getLastChunk().setIsAnnotation();
  }

  void addAsync() {
    addChunkWithTextNoCopy(
       CodeCompletionString::Chunk::ChunkKind::EffectsSpecifierKeyword,
       " async");
  }

  void addAnnotatedRethrows() {
    addRethrows();
    getLastChunk().setIsAnnotation();
  }

  void addRethrows() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::EffectsSpecifierKeyword,
        " rethrows");
  }

  void addAnnotatedLeftParen() {
    addLeftParen();
    getLastChunk().setIsAnnotation();
  }

  void addLeftParen() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::LeftParen, "(");
  }

  void addAnnotatedRightParen() {
    addRightParen();
    getLastChunk().setIsAnnotation();
  }

  void addRightParen() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::RightParen, ")");
  }

  void addAnnotatedLeftBracket() {
    addLeftBracket();
    getLastChunk().setIsAnnotation();
  }

  void addLeftBracket() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::LeftBracket, "[");
  }

  void addAnnotatedRightBracket() {
    addRightBracket();
    getLastChunk().setIsAnnotation();
  }

  void addRightBracket() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::RightBracket, "]");
  }

  void addLeftAngle() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::LeftAngle, "<");
  }

  void addRightAngle() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::RightAngle, ">");
  }

  void addLeadingDot() {
    addDot();
  }

  void addDot() {
    addChunkWithTextNoCopy(CodeCompletionString::Chunk::ChunkKind::Dot, ".");
  }

  void addEllipsis() {
    addChunkWithTextNoCopy(CodeCompletionString::Chunk::ChunkKind::Ellipsis,
                           "...");
  }

  void addComma() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::Comma, ", ");
  }

  void addExclamationMark() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::ExclamationMark, "!");
  }

  void addQuestionMark() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::QuestionMark, "?");
  }

  void addEqual() {
    addChunkWithTextNoCopy(CodeCompletionString::Chunk::ChunkKind::Equal, "=");
  }

  void addDeclAttrParamKeyword(StringRef Name, StringRef Annotation,
                               bool NeedSpecify) {
    addChunkWithText(CodeCompletionString::Chunk::ChunkKind::
                     DeclAttrParamKeyword, Name);
    if (NeedSpecify)
      addChunkWithText(CodeCompletionString::Chunk::ChunkKind::
                       DeclAttrParamColon, ": ");
    if (!Annotation.empty())
      addTypeAnnotation(Annotation);
  }

  void addDeclAttrKeyword(StringRef Name, StringRef Annotation) {
    addChunkWithText(CodeCompletionString::Chunk::ChunkKind::
                     DeclAttrKeyword, Name);
    if (!Annotation.empty())
      addTypeAnnotation(Annotation);
  }

  void addAttributeKeyword(StringRef Name, StringRef Annotation) {
    addChunkWithText(CodeCompletionString::Chunk::ChunkKind::Attribute, Name);
    if (!Annotation.empty())
      addTypeAnnotation(Annotation);
  }

  StringRef escapeKeyword(StringRef Word, bool escapeAllKeywords,
                          llvm::SmallString<16> &EscapedKeyword) {
    EscapedKeyword.clear();
    bool shouldEscape = false;
    if (escapeAllKeywords) {
#define KEYWORD(kw) .Case(#kw, true)
      shouldEscape = llvm::StringSwitch<bool>(Word)
#include "swift/Syntax/TokenKinds.def"
        .Default(false);
    } else {
      shouldEscape = !canBeArgumentLabel(Word);
    }

    if (!shouldEscape)
      return Word;

    EscapedKeyword.append("`");
    EscapedKeyword.append(Word);
    EscapedKeyword.append("`");
    return EscapedKeyword;
  }

  void addCallParameterColon() {
    addChunkWithText(CodeCompletionString::Chunk::ChunkKind::
                     CallArgumentColon, ": ");
  }

  void addSimpleNamedParameter(StringRef name) {
    withNestedGroup(CodeCompletionString::Chunk::ChunkKind::CallArgumentBegin, [&] {
      // Use internal, since we don't want the name to be outside the
      // placeholder.
      addChunkWithText(
          CodeCompletionString::Chunk::ChunkKind::CallArgumentInternalName,
          name);
    });
  }

  void addSimpleTypedParameter(StringRef Annotation, bool IsVarArg = false) {
    withNestedGroup(CodeCompletionString::Chunk::ChunkKind::CallArgumentBegin, [&] {
      addChunkWithText(
          CodeCompletionString::Chunk::ChunkKind::CallArgumentType,
          Annotation);
      if (IsVarArg)
        addEllipsis();
    });
  }

  void addCallArgument(Identifier Name, Identifier LocalName, Type Ty,
                       Type ContextTy, bool IsVarArg, bool IsInOut, bool IsIUO,
                       bool isAutoClosure, bool useUnderscoreLabel,
                       bool isLabeledTrailingClosure);

  void addCallArgument(Identifier Name, Type Ty, Type ContextTy = Type()) {
    addCallArgument(Name, Identifier(), Ty, ContextTy,
                    /*IsVarArg=*/false, /*IsInOut=*/false, /*isIUO=*/false,
                    /*isAutoClosure=*/false, /*useUnderscoreLabel=*/false,
                    /*isLabeledTrailingClosure=*/false);
  }

  void addGenericParameter(StringRef Name) {
    withNestedGroup(CodeCompletionString::Chunk::ChunkKind::GenericParameterBegin,
               [&] {
      addChunkWithText(
        CodeCompletionString::Chunk::ChunkKind::GenericParameterName, Name);
    });
  }

  void addDynamicLookupMethodCallTail() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::DynamicLookupMethodCallTail,
        "!");
    getLastChunk().setIsAnnotation();
  }

  void addOptionalMethodCallTail() {
    addChunkWithTextNoCopy(
        CodeCompletionString::Chunk::ChunkKind::OptionalMethodCallTail, "?");
  }

  void addTypeAnnotation(StringRef Type) {
    addChunkWithText(
        CodeCompletionString::Chunk::ChunkKind::TypeAnnotation, Type);
    getLastChunk().setIsAnnotation();
  }

  void addTypeAnnotation(Type T, PrintOptions PO, StringRef suffix = "");

  void addBraceStmtWithCursor(StringRef Description = "") {
    addChunkWithText(
        CodeCompletionString::Chunk::ChunkKind::BraceStmtWithCursor,
        Description);
  }

  void addWhitespace(StringRef space) {
    addChunkWithText(
        CodeCompletionString::Chunk::ChunkKind::Whitespace, space);
  }

  void addAnnotatedWhitespace(StringRef space) {
    addWhitespace(space);
    getLastChunk().setIsAnnotation();
  }

  void setBriefDocComment(StringRef comment) {
    BriefDocComment = comment;
  }
};

} // namespace ide
} // namespace swift

#endif // SWIFT_LIB_IDE_CODE_COMPLETION_RESULT_BUILDER_H
