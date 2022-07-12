//===--- Parser.cpp - Swift Language Parser -------------------------------===//
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
//
//  This file implements the Swift parser.
//
//===----------------------------------------------------------------------===//

#include "swift/Parse/Parser.h"
#include "swift/Subsystems.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/DiagnosticsParse.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/Basic/Defer.h"
#include "swift/Basic/SourceManager.h"
#include "swift/Basic/Timer.h"
#include "swift/Parse/Lexer.h"
#include "swift/Parse/CodeCompletionCallbacks.h"
#include "swift/Parse/DelayedParsingCallbacks.h"
#include "swift/Parse/ParseSILSupport.h"
#include "swift/Syntax/TokenSyntax.h"
#include "swift/Syntax/SyntaxParsingContext.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/Twine.h"

using namespace swift;
using namespace swift::syntax;

void DelayedParsingCallbacks::anchor() { }
void SILParserTUStateBase::anchor() { }

namespace {
/// A visitor that does delayed parsing of function bodies.
class ParseDelayedFunctionBodies : public ASTWalker {
  PersistentParserState &ParserState;
  CodeCompletionCallbacksFactory *CodeCompletionFactory;

public:
  ParseDelayedFunctionBodies(PersistentParserState &ParserState,
                             CodeCompletionCallbacksFactory *Factory)
    : ParserState(ParserState), CodeCompletionFactory(Factory) {}

  bool walkToDeclPre(Decl *D) override {
    if (auto AFD = dyn_cast<AbstractFunctionDecl>(D)) {
      if (AFD->getBodyKind() != FuncDecl::BodyKind::Unparsed)
        return false;
      parseFunctionBody(AFD);
      return true;
    }
    return true;
  }

private:
  void parseFunctionBody(AbstractFunctionDecl *AFD) {
    assert(AFD->getBodyKind() == FuncDecl::BodyKind::Unparsed);

    SourceFile &SF = *AFD->getDeclContext()->getParentSourceFile();
    SourceManager &SourceMgr = SF.getASTContext().SourceMgr;
    unsigned BufferID = SourceMgr.findBufferContainingLoc(AFD->getLoc());
    Parser TheParser(BufferID, SF, nullptr, &ParserState);
    TheParser.SyntaxContext->disable();
    std::unique_ptr<CodeCompletionCallbacks> CodeCompletion;
    if (CodeCompletionFactory) {
      CodeCompletion.reset(
          CodeCompletionFactory->createCodeCompletionCallbacks(TheParser));
      TheParser.setCodeCompletionCallbacks(CodeCompletion.get());
    }
    bool Parsed = false;
    if (auto FD = dyn_cast<FuncDecl>(AFD)) {
      if (FD->isAccessor()) {
        TheParser.parseAccessorBodyDelayed(AFD);
        Parsed = true;
      }
    }
    if (!Parsed && ParserState.hasFunctionBodyState(AFD))
      TheParser.parseAbstractFunctionBodyDelayed(AFD);

    if (CodeCompletion)
      CodeCompletion->doneParsing();
  }
};

static void parseDelayedDecl(
              PersistentParserState &ParserState,
              CodeCompletionCallbacksFactory *CodeCompletionFactory) {
  if (!ParserState.hasDelayedDecl())
    return;

  SourceFile &SF = *ParserState.getDelayedDeclContext()->getParentSourceFile();
  SourceManager &SourceMgr = SF.getASTContext().SourceMgr;
  unsigned BufferID =
    SourceMgr.findBufferContainingLoc(ParserState.getDelayedDeclLoc());
  Parser TheParser(BufferID, SF, nullptr, &ParserState);

  // Disable libSyntax creation in the delayed parsing.
  TheParser.SyntaxContext->disable();

  std::unique_ptr<CodeCompletionCallbacks> CodeCompletion;
  if (CodeCompletionFactory) {
    CodeCompletion.reset(
      CodeCompletionFactory->createCodeCompletionCallbacks(TheParser));
    TheParser.setCodeCompletionCallbacks(CodeCompletion.get());
  }

  switch (ParserState.getDelayedDeclKind()) {
  case PersistentParserState::DelayedDeclKind::TopLevelCodeDecl:
    TheParser.parseTopLevelCodeDeclDelayed();
    break;

  case PersistentParserState::DelayedDeclKind::Decl:
    TheParser.parseDeclDelayed();
    break;
  }

  if (CodeCompletion)
    CodeCompletion->doneParsing();
}
} // unnamed namespace


swift::Parser::BacktrackingScope::~BacktrackingScope() {
  if (Backtrack) {
    P.backtrackToPosition(PP);
    DT.abort();
  }
}

void swift::performDelayedParsing(
    DeclContext *DC, PersistentParserState &PersistentState,
    CodeCompletionCallbacksFactory *CodeCompletionFactory) {
  SharedTimer timer("Parsing");
  ParseDelayedFunctionBodies Walker(PersistentState,
                                    CodeCompletionFactory);
  DC->walkContext(Walker);

  if (CodeCompletionFactory)
    parseDelayedDecl(PersistentState, CodeCompletionFactory);
}

/// \brief Tokenizes a string literal, taking into account string interpolation.
static void getStringPartTokens(const Token &Tok, const LangOptions &LangOpts,
                                const SourceManager &SM,
                                int BufID, std::vector<Token> &Toks) {
  assert(Tok.is(tok::string_literal));
  bool IsMultiline = Tok.IsMultilineString();
  unsigned QuoteLen = IsMultiline ? 3 : 1;
  SmallVector<Lexer::StringSegment, 4> Segments;
  Lexer::getStringLiteralSegments(Tok, Segments, /*Diags=*/nullptr);
  for (unsigned i = 0, e = Segments.size(); i != e; ++i) {
    Lexer::StringSegment &Seg = Segments[i];
    bool isFirst = i == 0;
    bool isLast = i == e-1;
    if (Seg.Kind == Lexer::StringSegment::Literal) {
      SourceLoc Loc = Seg.Loc;
      unsigned Len = Seg.Length;
      if (isFirst) {
        // Include the quote.
        Loc = Loc.getAdvancedLoc(-QuoteLen);
        Len += QuoteLen;
      }
      if (isLast) {
        // Include the quote.
        Len += QuoteLen;
      }

      StringRef Text = SM.extractText({ Loc, Len });
      Token NewTok;
      NewTok.setToken(tok::string_literal, Text, IsMultiline);
      Toks.push_back(NewTok);

    } else {
      assert(Seg.Kind == Lexer::StringSegment::Expr &&
             "new enumerator was introduced ?");
      unsigned Offset = SM.getLocOffsetInBuffer(Seg.Loc, BufID);
      unsigned EndOffset = Offset + Seg.Length;

      if (isFirst) {
        // Add a token for the quote character.
        StringRef Text = SM.extractText({ Seg.Loc.getAdvancedLoc(-2), 1 });
        Token NewTok;
        NewTok.setToken(tok::string_literal, Text);
        Toks.push_back(NewTok);
      }

      std::vector<Token> NewTokens = swift::tokenize(LangOpts, SM, BufID,
                                                     Offset, EndOffset,
                                                     /*KeepComments=*/true);
      Toks.insert(Toks.end(), NewTokens.begin(), NewTokens.end());

      if (isLast) {
        // Add a token for the quote character.
        StringRef Text = SM.extractText({ Seg.Loc.getAdvancedLoc(Seg.Length),
                                          1 });
        Token NewTok;
        NewTok.setToken(tok::string_literal, Text);
        Toks.push_back(NewTok);
      }
    }
  }
}

std::vector<Token> swift::tokenize(const LangOptions &LangOpts,
                                   const SourceManager &SM, unsigned BufferID,
                                   unsigned Offset, unsigned EndOffset,
                                   bool KeepComments,
                                   bool TokenizeInterpolatedString,
                                   ArrayRef<Token> SplitTokens) {
  if (Offset == 0 && EndOffset == 0)
    EndOffset = SM.getRangeForBuffer(BufferID).getByteLength();

  Lexer L(LangOpts, SM, BufferID, /*Diags=*/nullptr, /*InSILMode=*/false,
          KeepComments ? CommentRetentionMode::ReturnAsTokens
                       : CommentRetentionMode::AttachToNextToken,
          TriviaRetentionMode::WithoutTrivia,
          Offset, EndOffset);

  auto TokComp = [&] (const Token &A, const Token &B) {
    return SM.isBeforeInBuffer(A.getLoc(), B.getLoc());
  };

  std::set<Token, decltype(TokComp)> ResetTokens(TokComp);
  for (auto C = SplitTokens.begin(), E = SplitTokens.end(); C != E; ++C) {
    ResetTokens.insert(*C);
  }

  std::vector<Token> Tokens;
  Trivia LeadingTrivia, TrailingTrivia;
  do {
    Tokens.emplace_back();
    L.lex(Tokens.back(), LeadingTrivia, TrailingTrivia);

    // If the token has the same location as a reset location,
    // reset the token stream
    auto F = ResetTokens.find(Tokens.back());
    if (F != ResetTokens.end()) {
      Tokens.back() = *F;
      assert(Tokens.back().isNot(tok::string_literal));

      auto NewState = L.getStateForBeginningOfTokenLoc(
                                    F->getLoc().getAdvancedLoc(F->getLength()));
      L.restoreState(NewState);
      continue;
    }

    if (Tokens.back().is(tok::string_literal) && TokenizeInterpolatedString) {
      Token StrTok = Tokens.back();
      Tokens.pop_back();
      getStringPartTokens(StrTok, LangOpts, SM, BufferID, Tokens);
    }
  } while (Tokens.back().isNot(tok::eof));
  Tokens.pop_back(); // Remove EOF.
  return Tokens;
}

// TODO: Refactor into common implementation with swift::tokenize.
std::vector<std::pair<RC<syntax::RawTokenSyntax>,
                                 syntax::AbsolutePosition>>
swift::tokenizeWithTrivia(const LangOptions &LangOpts,
                          const SourceManager &SM,
                          unsigned BufferID,
                          unsigned Offset,
                          unsigned EndOffset) {
  if (Offset == 0 && EndOffset == 0)
    EndOffset = SM.getRangeForBuffer(BufferID).getByteLength();

  Lexer L(LangOpts, SM, BufferID, /*Diags=*/nullptr, /*InSILMode=*/false,
          CommentRetentionMode::AttachToNextToken,
          TriviaRetentionMode::WithTrivia,
          Offset, EndOffset);
  std::vector<std::pair<RC<syntax::RawTokenSyntax>,
                        syntax::AbsolutePosition>> Tokens;
  syntax::AbsolutePosition RunningPos;
  Token Tok;
  Trivia LeadingTrivia, TrailingTrivia;
  do {
    L.lex(Tok, LeadingTrivia, TrailingTrivia);
    auto ThisToken = RawTokenSyntax::make(Tok.getKind(), Tok.getText(),
                                      SourcePresence::Present, LeadingTrivia,
                                      TrailingTrivia);

    auto ThisTokenPos = ThisToken->accumulateAbsolutePosition(RunningPos);
    Tokens.push_back({ThisToken, ThisTokenPos});
  } while (Tokens.back().first->isNot(tok::eof));

  return Tokens;
}

//===----------------------------------------------------------------------===//
// Setup and Helper Methods
//===----------------------------------------------------------------------===//

Parser::Parser(unsigned BufferID, SourceFile &SF, SILParserTUStateBase *SIL,
               PersistentParserState *PersistentState)
    : Parser(
          std::unique_ptr<Lexer>(new Lexer(
              SF.getASTContext().LangOpts, SF.getASTContext().SourceMgr,
              BufferID, &SF.getASTContext().Diags,
              /*InSILMode=*/SIL != nullptr,
              SF.getASTContext().LangOpts.AttachCommentsToDecls
                  ? CommentRetentionMode::AttachToNextToken
                  : CommentRetentionMode::None,
              SF.shouldBuildSyntaxTree()
                  ? TriviaRetentionMode::WithTrivia
                  : TriviaRetentionMode::WithoutTrivia)),
          SF, SIL, PersistentState) {}

namespace {

/// This is the token receiver that helps SourceFile to keep track of its
/// underlying corrected token stream.
class TokenRecorder: public ConsumeTokenReceiver {
  ASTContext &Ctx;
  SourceManager &SM;

  // Token list ordered by their appearance in the source file.
  std::vector<Token> &Bag;
  unsigned BufferID;

  // Registered token kind change. These changes are regiestered before the
  // token is consumed, so we need to keep track of them here.
  llvm::DenseMap<const void*, tok> TokenKindChangeMap;

  std::vector<Token>::iterator lower_bound(SourceLoc Loc) {
    return token_lower_bound(Bag, Loc);
  }

  std::vector<Token>::iterator lower_bound(Token Tok) {
    return lower_bound(Tok.getLoc());
  }

  void relexComment(CharSourceRange CommentRange,
                    llvm::SmallVectorImpl<Token> &Scracth) {
    Lexer L(Ctx.LangOpts, Ctx.SourceMgr, BufferID, nullptr, /*InSILMode=*/false,
            CommentRetentionMode::ReturnAsTokens,
            TriviaRetentionMode::WithoutTrivia,
            SM.getLocOffsetInBuffer(CommentRange.getStart(), BufferID),
            SM.getLocOffsetInBuffer(CommentRange.getEnd(), BufferID));
    while(true) {
      Token Result;
      L.lex(Result);
      if (Result.is(tok::eof))
        break;
      assert(Result.is(tok::comment));
      Scracth.push_back(Result);
    }
  }

public:
  TokenRecorder(SourceFile &SF):
  Ctx(SF.getASTContext()),
  SM(SF.getASTContext().SourceMgr),
  Bag(SF.getTokenVector()),
  BufferID(SF.getBufferID().getValue()) {};

  void finalize() override {

    // We should consume the comments at the end of the file that don't attach
    // to any tokens.
    SourceLoc TokEndLoc;
    if (!Bag.empty()) {
      Token Last = Bag.back();
      TokEndLoc = Last.getLoc().getAdvancedLoc(Last.getLength());
    } else {

      // Special case: the file contains nothing but comments.
      TokEndLoc = SM.getLocForBufferStart(BufferID);
    }
    llvm::SmallVector<Token, 4> Scratch;
    relexComment(CharSourceRange(SM, TokEndLoc,
                                 SM.getRangeForBuffer(BufferID).getEnd()),
                 Scratch);
    // Accept these orphaned comments.
    Bag.insert(Bag.end(), Scratch.begin(), Scratch.end());
  }

  void registerTokenKindChange(SourceLoc Loc, tok NewKind) override {
    // If a token with the same location is already in the bag, update its kind.
    auto Pos = lower_bound(Loc);
    if (Pos != Bag.end() && Pos->getLoc().getOpaquePointerValue() ==
        Loc.getOpaquePointerValue()) {
      Pos->setKind(NewKind);
      return;
    }

    // Save the update for later.
    TokenKindChangeMap[Loc.getOpaquePointerValue()] = NewKind;
  }

  void receive(Token Tok) override {
    // We filter out all tokens without valid location
    if(Tok.getLoc().isInvalid())
      return;

    // If a token with the same location is already in the bag, skip this token.
    auto Pos = lower_bound(Tok);
    if (Pos != Bag.end() && Pos->getLoc().getOpaquePointerValue() ==
        Tok.getLoc().getOpaquePointerValue()) {
      return;
    }

    // Update Token kind if a kind update was regiestered before.
    auto Found = TokenKindChangeMap.find(Tok.getLoc().
                                         getOpaquePointerValue());
    if (Found != TokenKindChangeMap.end()) {
      Tok.setKind(Found->getSecond());
    }

    // If the token has comment attached to it, re-lexing these comments and
    // consume them as separate tokens.
    llvm::SmallVector<Token, 4> TokensToConsume;
    if (Tok.hasComment()) {
      relexComment(Tok.getCommentRange(), TokensToConsume);
    }

    TokensToConsume.push_back(Tok);
    Bag.insert(Pos, TokensToConsume.begin(), TokensToConsume.end());
  }
};
} // End of an anonymous namespace.

Parser::Parser(std::unique_ptr<Lexer> Lex, SourceFile &SF,
               SILParserTUStateBase *SIL,
               PersistentParserState *PersistentState)
  : SourceMgr(SF.getASTContext().SourceMgr),
    Diags(SF.getASTContext().Diags),
    SF(SF),
    L(Lex.release()),
    SIL(SIL),
    CurDeclContext(&SF),
    Context(SF.getASTContext()),
    TokReceiver(SF.shouldCollectToken() ?
                new TokenRecorder(SF) :
                new ConsumeTokenReceiver()),
    SyntaxContext(new SyntaxParsingContext(SyntaxContext, SF)) {
  State = PersistentState;
  if (!State) {
    OwnedState.reset(new PersistentParserState());
    State = OwnedState.get();
  }

  // Set the token to a sentinel so that we know the lexer isn't primed yet.
  // This cannot be tok::unknown, since that is a token the lexer could produce.
  Tok.setKind(tok::NUM_TOKENS);

  auto ParserPos = State->takeParserPosition();
  if (ParserPos.isValid() &&
      SourceMgr.findBufferContainingLoc(ParserPos.Loc) == L->getBufferID()) {
    auto BeginParserPosition = getParserPosition(ParserPos);
    restoreParserPosition(BeginParserPosition);
    InPoundLineEnvironment = State->InPoundLineEnvironment;
  }
}

Parser::~Parser() {
  if (Tok.is(tok::eof))
    SyntaxContext->addToken(Tok, LeadingTrivia, TrailingTrivia);

  delete L;
  delete TokReceiver;
  delete SyntaxContext;
}

const Token &Parser::peekToken() {
  return L->peekNextToken();
}

SourceLoc Parser::consumeTokenWithoutFeedingReceiver() {
  SourceLoc Loc = Tok.getLoc();
  assert(Tok.isNot(tok::eof) && "Lexing past eof!");

  if (IsParsingInterfaceTokens && !Tok.getText().empty()) {
    SF.recordInterfaceToken(Tok.getText());
  }
  L->lex(Tok, LeadingTrivia, TrailingTrivia);
  PreviousLoc = Loc;
  return Loc;
}

void Parser::consumeExtraToken(Token Extra) {
  TokReceiver->receive(Extra);
}

SourceLoc Parser::consumeToken() {
  TokReceiver->receive(Tok);
  SyntaxContext->addToken(Tok, LeadingTrivia, TrailingTrivia);
  return consumeTokenWithoutFeedingReceiver();
}

SourceLoc Parser::getEndOfPreviousLoc() {
  return Lexer::getLocForEndOfToken(SourceMgr, PreviousLoc);
}

SourceLoc Parser::consumeStartingCharacterOfCurrentToken(tok Kind, size_t Len) {
  // Consumes prefix of token and returns its location.
  // (like '?', '<', '>' or '!' immediately followed by '<') 
  assert(Len >= 1);

  // Current token can be either one-character token we want to consume...
  if (Tok.getLength() == Len) {
    Tok.setKind(Kind);
    return consumeToken();
  }

  auto Loc = Tok.getLoc();

  // ... or a multi-character token with the first N characters being the one
  // that we want to consume as a separate token.
  assert(Tok.getLength() > Len);
  markSplitToken(Kind, Tok.getText().substr(0, Len));

  auto NewState = L->getStateForBeginningOfTokenLoc(Loc.getAdvancedLoc(Len));
  restoreParserPosition(ParserPosition(NewState, Loc),
                        /*enableDiagnostics=*/true);
  return PreviousLoc;
}

void Parser::markSplitToken(tok Kind, StringRef Txt) {
  SplitTokens.emplace_back();
  SplitTokens.back().setToken(Kind, Txt);
  Trivia EmptyTrivia;
  SyntaxContext->addToken(SplitTokens.back(), LeadingTrivia, EmptyTrivia);
  LeadingTrivia.empty();
  TokReceiver->receive(SplitTokens.back());
}

SourceLoc Parser::consumeStartingLess() {
  assert(startsWithLess(Tok) && "Token does not start with '<'");
  return consumeStartingCharacterOfCurrentToken(tok::l_angle);
}

SourceLoc Parser::consumeStartingGreater() {
  assert(startsWithGreater(Tok) && "Token does not start with '>'");
  return consumeStartingCharacterOfCurrentToken(tok::r_angle);
}

void Parser::skipSingle() {
  switch (Tok.getKind()) {
  case tok::l_paren:
    consumeToken();
    skipUntil(tok::r_paren);
    consumeIf(tok::r_paren);
    break;
  case tok::l_brace:
    consumeToken();
    skipUntil(tok::r_brace);
    consumeIf(tok::r_brace);
    break;
  case tok::l_square:
    consumeToken();
    skipUntil(tok::r_square);
    consumeIf(tok::r_square);
    break;
  case tok::pound_if:
  case tok::pound_else:
  case tok::pound_elseif:
    consumeToken();
    // skipUntil also implicitly stops at tok::pound_endif.
    skipUntil(tok::pound_else, tok::pound_elseif);
      
    if (Tok.isAny(tok::pound_else, tok::pound_elseif))
      skipSingle();
    else
      consumeIf(tok::pound_endif);
    break;
      
  default:
    consumeToken();
    break;
  }
}

void Parser::skipUntil(tok T1, tok T2) {
  // tok::NUM_TOKENS is a sentinel that means "don't skip".
  if (T1 == tok::NUM_TOKENS && T2 == tok::NUM_TOKENS) return;
  
  while (Tok.isNot(T1, T2, tok::eof, tok::pound_endif, tok::code_complete))
    skipSingle();
}

void Parser::skipUntilAnyOperator() {
  while (Tok.isNot(tok::eof, tok::pound_endif, tok::code_complete) &&
         Tok.isNotAnyOperator())
    skipSingle();
}

/// \brief Skip until a token that starts with '>', and consume it if found.
/// Applies heuristics that are suitable when trying to find the end of a list
/// of generic parameters, generic arguments, or list of types in a protocol
/// composition.
SourceLoc Parser::skipUntilGreaterInTypeList(bool protocolComposition) {
  SourceLoc lastLoc = PreviousLoc;
  while (true) {
    switch (Tok.getKind()) {
    case tok::eof:
    case tok::l_brace:
    case tok::r_brace:
    case tok::code_complete:
      return lastLoc;

#define KEYWORD(X) case tok::kw_##X:
#define POUND_KEYWORD(X) case tok::pound_##X:
#include "swift/Syntax/TokenKinds.def"
    // 'Self' can appear in types, skip it.
    if (Tok.is(tok::kw_Self))
      break;
    if (isStartOfStmt() || isStartOfDecl() || Tok.is(tok::pound_endif))
      return lastLoc;
    break;

    case tok::l_paren:
    case tok::r_paren:
    case tok::l_square:
    case tok::r_square:
      // In generic type parameter list, skip '[' ']' '(' ')', because they
      // can appear in types.
      if (protocolComposition)
        return lastLoc;
      break;

    default:
      if (Tok.isAnyOperator() && startsWithGreater(Tok))
        return consumeStartingGreater();
      
      break;
    }
    skipSingle();
    lastLoc = PreviousLoc;
  }
}

void Parser::skipUntilDeclRBrace() {
  while (Tok.isNot(tok::eof, tok::r_brace, tok::pound_endif,
                   tok::code_complete) &&
         !isStartOfDecl())
    skipSingle();
}

void Parser::skipUntilDeclStmtRBrace(tok T1) {
  while (Tok.isNot(T1, tok::eof, tok::r_brace, tok::pound_endif,
                   tok::code_complete) &&
         !isStartOfStmt() && !isStartOfDecl()) {
    skipSingle();
  }
}

void Parser::skipUntilDeclStmtRBrace(tok T1, tok T2) {
  while (Tok.isNot(T1, T2, tok::eof, tok::r_brace, tok::pound_endif,
                   tok::code_complete) &&
         !isStartOfStmt() && !isStartOfDecl()) {
    skipSingle();
  }
}

void Parser::skipUntilDeclRBrace(tok T1, tok T2) {
  while (Tok.isNot(T1, T2, tok::eof, tok::r_brace, tok::pound_endif) &&
         !isStartOfDecl()) {
    skipSingle();
  }
}

void Parser::skipUntilConditionalBlockClose() {
  while (Tok.isNot(tok::pound_else, tok::pound_elseif, tok::pound_endif,
                   tok::eof)) {
    skipSingle();
  }
}

bool Parser::parseEndIfDirective(SourceLoc &Loc) {
  Loc = Tok.getLoc();
  if (parseToken(tok::pound_endif, diag::expected_close_to_if_directive)) {
    Loc = PreviousLoc;
    skipUntilConditionalBlockClose();
    return true;
  } else if (!Tok.isAtStartOfLine() && Tok.isNot(tok::eof))
    diagnose(Tok.getLoc(),
             diag::extra_tokens_conditional_compilation_directive);
  return false;
}

Parser::StructureMarkerRAII::StructureMarkerRAII(Parser &parser,
                                                 const Token &tok)
  : P(parser)
{
  switch (tok.getKind()) {
  case tok::l_brace:
    P.StructureMarkers.push_back({tok.getLoc(),
                                  StructureMarkerKind::OpenBrace,
                                  None});
    break;

  case tok::l_paren:
    P.StructureMarkers.push_back({tok.getLoc(),
                                  StructureMarkerKind::OpenParen,
                                  None});
    break;

  case tok::l_square:
    P.StructureMarkers.push_back({tok.getLoc(),
                                  StructureMarkerKind::OpenSquare,
                                  None});
    break;

  default:
    llvm_unreachable("Not a matched token");
  }
}

void Parser::StructureMarkerRAII::diagnoseOverflow() {
  auto Loc = P.StructureMarkers.back().Loc;
  P.diagnose(Loc, diag::structure_overflow, MaxDepth);
}

//===----------------------------------------------------------------------===//
// Primitive Parsing
//===----------------------------------------------------------------------===//

bool Parser::parseIdentifier(Identifier &Result, SourceLoc &Loc,
                             const Diagnostic &D) {
  switch (Tok.getKind()) {
  case tok::kw_throws:
  case tok::kw_rethrows:
    if (!Context.isSwiftVersion3())
      break;
    // Swift3 accepts 'throws' and 'rethrows'
    LLVM_FALLTHROUGH;
  case tok::kw_self:
  case tok::kw_Self:
  case tok::identifier:
    Loc = consumeIdentifier(&Result);
    return false;
  default:
    break;
  }
  checkForInputIncomplete();
  diagnose(Tok, D);
  return true;
}

bool Parser::parseSpecificIdentifier(StringRef expected, SourceLoc &loc,
                                     const Diagnostic &D) {
  if (Tok.getText() != expected) {
    diagnose(Tok, D);
    return true;
  }
  loc = consumeToken(tok::identifier);
  return false;
}

/// parseAnyIdentifier - Consume an identifier or operator if present and return
/// its name in Result.  Otherwise, emit an error and return true.
bool Parser::parseAnyIdentifier(Identifier &Result, SourceLoc &Loc,
                                const Diagnostic &D) {
  if (Tok.is(tok::identifier) || Tok.isAnyOperator()) {
    Result = Context.getIdentifier(Tok.getText());
    Loc = Tok.getLoc();
    consumeToken();
    return false;
  }

  // When we know we're supposed to get an identifier or operator, map the
  // postfix '!' to an operator name.
  if (Tok.is(tok::exclaim_postfix)) {
    Result = Context.getIdentifier(Tok.getText());
    Loc = Tok.getLoc();
    consumeToken(tok::exclaim_postfix);
    return false;
  }

  checkForInputIncomplete();

  if (Tok.isKeyword()) {
    diagnose(Tok, diag::keyword_cant_be_identifier, Tok.getText());
    diagnose(Tok, diag::backticks_to_escape)
      .fixItReplace(Tok.getLoc(), "`" + Tok.getText().str() + "`");
  } else {
    diagnose(Tok, D);
  }

  return true;
}

/// parseToken - The parser expects that 'K' is next in the input.  If so, it is
/// consumed and false is returned.
///
/// If the input is malformed, this emits the specified error diagnostic.
bool Parser::parseToken(tok K, SourceLoc &TokLoc, const Diagnostic &D) {
  if (Tok.is(K)) {
    TokLoc = consumeToken(K);
    return false;
  }

  checkForInputIncomplete();
  diagnose(Tok, D);
  return true;
}

/// parseMatchingToken - Parse the specified expected token and return its
/// location on success.  On failure, emit the specified error diagnostic, and a
/// note at the specified note location.
bool Parser::parseMatchingToken(tok K, SourceLoc &TokLoc, Diag<> ErrorDiag,
                                SourceLoc OtherLoc) {
  Diag<> OtherNote;
  switch (K) {
  case tok::r_paren:  OtherNote = diag::opening_paren;    break;
  case tok::r_square: OtherNote = diag::opening_bracket;  break;
  case tok::r_brace:  OtherNote = diag::opening_brace;    break;
  default:            llvm_unreachable("unknown matching token!"); break;
  }
  if (parseToken(K, TokLoc, ErrorDiag)) {
    diagnose(OtherLoc, OtherNote);

    TokLoc = PreviousLoc;
    return true;
  }

  return false;
}

static SyntaxKind getListElementKind(SyntaxKind ListKind) {
  switch (ListKind) {
  case SyntaxKind::FunctionCallArgumentList:
    return SyntaxKind::FunctionCallArgument;
  case SyntaxKind::ArrayElementList:
    return SyntaxKind::ArrayElement;
  case SyntaxKind::DictionaryElementList:
    return SyntaxKind::DictionaryElement;
  case SyntaxKind::TupleElementList:
    return SyntaxKind::TupleElement;
  default:
    return SyntaxKind::Unknown;
  }
}

ParserStatus
Parser::parseList(tok RightK, SourceLoc LeftLoc, SourceLoc &RightLoc,
                  bool AllowSepAfterLast, Diag<> ErrorDiag, SyntaxKind Kind,
                  std::function<ParserStatus()> callback) {
  llvm::Optional<SyntaxParsingContext> ListContext;
  ListContext.emplace(SyntaxContext, Kind);

  SyntaxKind ElementKind = getListElementKind(Kind);

  if (Tok.is(RightK)) {
    RightLoc = consumeToken(RightK);
    return makeParserSuccess();
  }

  ParserStatus Status;
  while (true) {
    while (Tok.is(tok::comma)) {
      diagnose(Tok, diag::unexpected_separator, ",")
        .fixItRemove(SourceRange(Tok.getLoc()));
      consumeToken();
    }
    SourceLoc StartLoc = Tok.getLoc();

    SyntaxParsingContext ElementContext(SyntaxContext, ElementKind);

    Status |= callback();
    if (Tok.is(RightK))
      break;
    // If the lexer stopped with an EOF token whose spelling is ")", then this
    // is actually the tuple that is a string literal interpolation context.
    // Just accept the ")" and build the tuple as we usually do.
    if (Tok.is(tok::eof) && Tok.getText() == ")" && RightK == tok::r_paren) {
      RightLoc = Tok.getLoc();
      return Status;
    }
    // If we haven't made progress, or seeing any error, skip ahead.
    if (Tok.getLoc() == StartLoc || Status.isError()) {
      assert(Status.isError() && "no progress without error");
      skipUntilDeclRBrace(RightK, tok::comma);
      if (Tok.is(RightK) || Tok.isNot(tok::comma))
        break;
    }
    if (consumeIf(tok::comma)) {
      if (Tok.isNot(RightK))
        continue;
      if (!AllowSepAfterLast) {
        diagnose(Tok, diag::unexpected_separator, ",")
          .fixItRemove(SourceRange(PreviousLoc));
      }
      break;
    }
    // If we're in a comma-separated list, the next token is at the
    // beginning of a new line and can never start an element, break.
    if (Tok.isAtStartOfLine() &&
        (Tok.is(tok::r_brace) || isStartOfDecl() || isStartOfStmt())) {
      break;
    }
    // If we found EOF or such, bailout.
    if (Tok.isAny(tok::eof, tok::pound_endif)) {
      IsInputIncomplete = true;
      break;
    }

    diagnose(Tok, diag::expected_separator, ",")
      .fixItInsertAfter(PreviousLoc, ",");
    Status.setIsParseError();
  }

  ListContext.reset();

  if (Status.isError()) {
    // If we've already got errors, don't emit missing RightK diagnostics.
    RightLoc = Tok.is(RightK) ? consumeToken() : PreviousLoc;
  } else if (parseMatchingToken(RightK, RightLoc, ErrorDiag, LeftLoc)) {
    Status.setIsParseError();
  }

  return Status;
}

/// diagnoseRedefinition - Diagnose a redefinition error, with a note
/// referring back to the original definition.

void Parser::diagnoseRedefinition(ValueDecl *Prev, ValueDecl *New) {
  assert(New != Prev && "Cannot conflict with self");
  diagnose(New->getLoc(), diag::decl_redefinition, New->isDefinition());
  diagnose(Prev->getLoc(), diag::previous_decldef, Prev->isDefinition(),
           Prev->getBaseName());
}

struct ParserUnit::Implementation {
  LangOptions LangOpts;
  SearchPathOptions SearchPathOpts;
  DiagnosticEngine Diags;
  ASTContext Ctx;
  SourceFile *SF;
  std::unique_ptr<Parser> TheParser;

  Implementation(SourceManager &SM, unsigned BufferID,
                 const LangOptions &Opts, StringRef ModuleName)
    : LangOpts(Opts),
      Diags(SM),
      Ctx(LangOpts, SearchPathOpts, SM, Diags),
      SF(new (Ctx) SourceFile(
            *ModuleDecl::create(Ctx.getIdentifier(ModuleName), Ctx),
            SourceFileKind::Main, BufferID,
            SourceFile::ImplicitModuleImportKind::None,
            Opts.CollectParsedToken,
            Opts.BuildSyntaxTree)) {
  }
};

ParserUnit::ParserUnit(SourceManager &SM, unsigned BufferID)
  : ParserUnit(SM, BufferID, LangOptions(), "input") {
}

ParserUnit::ParserUnit(SourceManager &SM, unsigned BufferID,
                       const LangOptions &LangOpts, StringRef ModuleName)
  : Impl(*new Implementation(SM, BufferID, LangOpts, ModuleName)) {

  Impl.TheParser.reset(new Parser(BufferID, *Impl.SF, nullptr));
}

ParserUnit::ParserUnit(SourceManager &SM, unsigned BufferID,
             unsigned Offset, unsigned EndOffset)
  : Impl(*new Implementation(SM, BufferID, LangOptions(), "input")) {

  std::unique_ptr<Lexer> Lex;
  Lex.reset(new Lexer(Impl.LangOpts, SM,
                      BufferID, &Impl.Diags,
                      /*InSILMode=*/false,
                      CommentRetentionMode::None,
                      TriviaRetentionMode::WithoutTrivia,
                      Offset, EndOffset));
  Impl.TheParser.reset(new Parser(std::move(Lex), *Impl.SF));
}

ParserUnit::~ParserUnit() {
  delete &Impl;
}

Parser &ParserUnit::getParser() {
  return *Impl.TheParser;
}

DiagnosticEngine &ParserUnit::getDiagnosticEngine() {
  return Impl.Diags;
}

const LangOptions &ParserUnit::getLangOptions() const {
  return Impl.LangOpts;
}

SourceFile &ParserUnit::getSourceFile() {
  return *Impl.SF;
}

ParsedDeclName swift::parseDeclName(StringRef name) {
  if (name.empty()) return ParsedDeclName();

  // Local function to handle the parsing of the base name + context.
  //
  // Returns true if an error occurred, without recording the base name.
  ParsedDeclName result;
  auto parseBaseName = [&](StringRef text) -> bool {
    // Split the text into context name and base name.
    StringRef contextName, baseName;
    std::tie(contextName, baseName) = text.rsplit('.');
    if (baseName.empty()) {
      baseName = contextName;
      contextName = StringRef();
    } else if (contextName.empty()) {
      return true;
    }

    auto isValidIdentifier = [](StringRef text) -> bool {
      return Lexer::isIdentifier(text) && text != "_";
    };

    // Make sure we have an identifier for the base name.
    if (!isValidIdentifier(baseName))
      return true;

    // If we have a context, make sure it is an identifier, or a series of
    // dot-separated identifiers.
    // FIXME: What about generic parameters?
    if (!contextName.empty()) {
      StringRef first;
      StringRef rest = contextName;
      do {
        std::tie(first, rest) = rest.split('.');
        if (!isValidIdentifier(first))
          return true;
      } while (!rest.empty());
    }

    // Record the results.
    result.ContextName = contextName;
    result.BaseName = baseName;
    return false;
  };

  // If this is not a function name, just parse the base name and
  // we're done.
  if (name.back() != ')') {
    if (Lexer::isOperator(name))
      result.BaseName = name;
    else if (parseBaseName(name))
      return ParsedDeclName();
    return result;
  }

  // We have a function name.
  result.IsFunctionName = true;

  // Split the base name from the parameters.
  StringRef baseName, parameters;
  std::tie(baseName, parameters) = name.split('(');
  if (parameters.empty()) return ParsedDeclName();

  // If the base name is prefixed by "getter:" or "setter:", it's an
  // accessor.
  if (baseName.startswith("getter:")) {
    result.IsGetter = true;
    result.IsFunctionName = false;
    baseName = baseName.substr(7);
  } else if (baseName.startswith("setter:")) {
    result.IsSetter = true;
    result.IsFunctionName = false;
    baseName = baseName.substr(7);
  }

  // Parse the base name.
  if (parseBaseName(baseName)) return ParsedDeclName();

  parameters = parameters.drop_back(); // ')'
  if (parameters.empty()) return result;

  if (parameters.back() != ':')
    return ParsedDeclName();

  bool isMember = !result.ContextName.empty();
  do {
    StringRef NextParam;
    std::tie(NextParam, parameters) = parameters.split(':');

    if (!Lexer::isIdentifier(NextParam))
      return ParsedDeclName();
    if (NextParam == "_") {
      result.ArgumentLabels.push_back("");
    } else if (isMember && NextParam == "self") {
      // For a member, "self" indicates the self parameter. There can
      // only be one such parameter.
      if (result.SelfIndex) return ParsedDeclName();
      result.SelfIndex = result.ArgumentLabels.size();
    } else {
      result.ArgumentLabels.push_back(NextParam);
    }
  } while (!parameters.empty());

  // Drop the argument labels for a property accessor; they aren't used.
  if (result.isPropertyAccessor())
    result.ArgumentLabels.clear();

  return result;
}

DeclName ParsedDeclName::formDeclName(ASTContext &ctx) const {
  return swift::formDeclName(ctx, BaseName, ArgumentLabels, IsFunctionName);
}

DeclName swift::formDeclName(ASTContext &ctx,
                             StringRef baseName,
                             ArrayRef<StringRef> argumentLabels,
                             bool isFunctionName) {
  // We cannot import when the base name is not an identifier.
  if (baseName.empty())
    return DeclName();
  if (!Lexer::isIdentifier(baseName) && !Lexer::isOperator(baseName))
    return DeclName();

  // Get the identifier for the base name.
  Identifier baseNameId = ctx.getIdentifier(baseName);

  // For non-functions, just use the base name.
  if (!isFunctionName) return baseNameId;

  // For functions, we need to form a complete name.

  // Convert the argument names.
  SmallVector<Identifier, 4> argumentLabelIds;
  for (auto argName : argumentLabels) {
    if (argumentLabels.empty() || !Lexer::isIdentifier(argName)) {
      argumentLabelIds.push_back(Identifier());
      continue;
    }

    argumentLabelIds.push_back(ctx.getIdentifier(argName));
  }

  // Build the result.
  return DeclName(ctx, baseNameId, argumentLabelIds);
}

DeclName swift::parseDeclName(ASTContext &ctx, StringRef name) {
  return parseDeclName(name).formDeclName(ctx);
}
