//===--- ParsePattern.cpp - Swift Language Parser for Patterns ------------===//
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
// Pattern Parsing and AST Building
//
//===----------------------------------------------------------------------===//

#include "swift/Parse/CodeCompletionCallbacks.h"
#include "swift/Parse/Parser.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/Initializer.h"
#include "swift/Basic/StringExtras.h"
#include "swift/Syntax/SyntaxFactory.h"
#include "swift/Syntax/TokenSyntax.h"
#include "swift/Syntax/SyntaxParsingContext.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace swift;
using namespace swift::syntax;

/// \brief Determine the kind of a default argument given a parsed
/// expression that has not yet been type-checked.
static DefaultArgumentKind getDefaultArgKind(Expr *init) {
  if (!init)
    return DefaultArgumentKind::None;

  auto magic = dyn_cast<MagicIdentifierLiteralExpr>(init);
  if (!magic)
    return DefaultArgumentKind::Normal;

  switch (magic->getKind()) {
  case MagicIdentifierLiteralExpr::Column:
    return DefaultArgumentKind::Column;
  case MagicIdentifierLiteralExpr::File:
    return DefaultArgumentKind::File;
  case MagicIdentifierLiteralExpr::Line:
    return DefaultArgumentKind::Line;
  case MagicIdentifierLiteralExpr::Function:
    return DefaultArgumentKind::Function;
  case MagicIdentifierLiteralExpr::DSOHandle:
    return DefaultArgumentKind::DSOHandle;
  }

  llvm_unreachable("Unhandled MagicIdentifierLiteralExpr in switch.");
}

void Parser::DefaultArgumentInfo::setFunctionContext(AbstractFunctionDecl *AFD){
  for (auto context : ParsedContexts) {
    context->changeFunction(AFD);
  }
}

static ParserStatus parseDefaultArgument(Parser &P,
                                   Parser::DefaultArgumentInfo *defaultArgs,
                                   unsigned argIndex,
                                   Expr *&init,
                                 Parser::ParameterContextKind paramContext) {
  SourceLoc equalLoc = P.consumeToken(tok::equal);

  // Enter a fresh default-argument context with a meaningless parent.
  // We'll change the parent to the function later after we've created
  // that declaration.
  auto initDC = new (P.Context) DefaultArgumentInitializer(P.CurDeclContext,
                                                           argIndex);
  Parser::ParseFunctionBody initScope(P, initDC);

  ParserResult<Expr> initR = P.parseExpr(diag::expected_init_value);

  // Record the default-argument context if we're supposed to accept default
  // arguments here.
  if (defaultArgs) {
    defaultArgs->ParsedContexts.push_back(initDC);
  }

  Diag<> diagID = { DiagID() };
  switch (paramContext) {
  case Parser::ParameterContextKind::Function:
  case Parser::ParameterContextKind::Operator:
  case Parser::ParameterContextKind::Initializer:
    break;
  case Parser::ParameterContextKind::Closure:
    diagID = diag::no_default_arg_closure;
    break;
  case Parser::ParameterContextKind::Subscript:
    diagID = diag::no_default_arg_subscript;
    break;
  case Parser::ParameterContextKind::Curried:
    diagID = diag::no_default_arg_curried;
    break;
  }
  
  assert((diagID.ID != DiagID()) == !defaultArgs &&
         "Default arguments specified for an unexpected parameter list kind");
  
  if (diagID.ID != DiagID()) {
    auto inFlight = P.diagnose(equalLoc, diagID);
    if (initR.isNonNull())
      inFlight.fixItRemove(SourceRange(equalLoc, initR.get()->getEndLoc()));
    return ParserStatus();
  }
  
  defaultArgs->HasDefaultArgument = true;

  if (initR.hasCodeCompletion())
    return makeParserCodeCompletionStatus();

  if (initR.isNull())
    return makeParserError();

  init = initR.get();
  return ParserStatus();
}

/// Determine whether we are at the start of a parameter name when
/// parsing a parameter.
static bool startsParameterName(Parser &parser, bool isClosure) {
  // '_' cannot be a type, so it must be a parameter name.
  if (parser.Tok.is(tok::kw__))
    return true;

  // To have a parameter name here, we need a name.
  if (!parser.Tok.canBeArgumentLabel())
    return false;

  // If the next token can be an argument label or is ':', this is a name.
  const auto &nextTok = parser.peekToken();
  if (nextTok.is(tok::colon) || nextTok.canBeArgumentLabel())
    return true;

  // The identifier could be a name or it could be a type. In a closure, we
  // assume it's a name, because the type can be inferred. Elsewhere, we
  // assume it's a type.
  return isClosure;
}

ParserStatus
Parser::parseParameterClause(SourceLoc &leftParenLoc,
                             SmallVectorImpl<ParsedParameter> &params,
                             SourceLoc &rightParenLoc,
                             DefaultArgumentInfo *defaultArgs,
                             ParameterContextKind paramContext) {
  assert(params.empty() && leftParenLoc.isInvalid() &&
         rightParenLoc.isInvalid() && "Must start with empty state");

  // Consume the starting '(';
  leftParenLoc = consumeToken(tok::l_paren);

  // Trivial case: empty parameter list.
  if (Tok.is(tok::r_paren)) {
    rightParenLoc = consumeToken(tok::r_paren);
    return ParserStatus();
  }

  // Parse the parameter list.
  bool isClosure = paramContext == ParameterContextKind::Closure;
  return parseList(tok::r_paren, leftParenLoc, rightParenLoc,
                      /*AllowSepAfterLast=*/false,
                      diag::expected_rparen_parameter,
                      SyntaxKind::Unknown,
                      [&]() -> ParserStatus {
    ParsedParameter param;
    ParserStatus status;
    SourceLoc StartLoc = Tok.getLoc();

    unsigned defaultArgIndex = defaultArgs ? defaultArgs->NextIndex++ : 0;

    // Attributes.
    bool FoundCCToken;
    parseDeclAttributeList(param.Attrs, FoundCCToken);
    if (FoundCCToken) {
      if (CodeCompletion) {
        CodeCompletion->completeDeclAttrKeyword(nullptr, isInSILMode(), true);
      } else {
        status |= makeParserCodeCompletionStatus();
      }
    }
    
    // ('inout' | 'let' | 'var' | '__shared' | '__owned')?
    bool hasSpecifier = false;
    while (Tok.isAny(tok::kw_inout, tok::kw_let, tok::kw_var) ||
           (Tok.is(tok::identifier) &&
            (Tok.getRawText().equals("__shared") ||
             Tok.getRawText().equals("__owned")))) {
      if (!hasSpecifier) {
        if (Tok.is(tok::kw_inout)) {
          // This case is handled later when mapping to ParamDecls for
          // better fixits.
          param.SpecifierKind = VarDecl::Specifier::InOut;
          param.SpecifierLoc = consumeToken();
        } else if (Tok.is(tok::identifier) &&
                   Tok.getRawText().equals("__shared")) {
          // This case is handled later when mapping to ParamDecls for
          // better fixits.
          param.SpecifierKind = VarDecl::Specifier::Shared;
          param.SpecifierLoc = consumeToken();
        } else {
          diagnose(Tok, diag::parameter_let_var_as_attr, Tok.getText())
            .fixItRemove(Tok.getLoc());
          consumeToken();
        }
        hasSpecifier = true;
      } else {
        // Redundant specifiers are fairly common, recognize, reject, and
        // recover from this gracefully.
        diagnose(Tok, diag::parameter_specifier_repeated)
          .fixItRemove(Tok.getLoc());
        consumeToken();
      }
    }

    if (startsParameterName(*this, isClosure)) {
      // identifier-or-none for the first name
      if (Tok.is(tok::kw__)) {
        param.FirstNameLoc = consumeToken();
      } else {
        assert(Tok.canBeArgumentLabel() && "startsParameterName() lied");
        Tok.setKind(tok::identifier);
        param.FirstName = Context.getIdentifier(Tok.getText());
        param.FirstNameLoc = consumeToken();
      }

      // identifier-or-none? for the second name
      if (Tok.canBeArgumentLabel()) {
        if (!Tok.is(tok::kw__)) {
          param.SecondName = Context.getIdentifier(Tok.getText());
          Tok.setKind(tok::identifier);
        }
        param.SecondNameLoc = consumeToken();
      }

      // Operators and closures cannot have API names.
      if ((paramContext == ParameterContextKind::Operator ||
           paramContext == ParameterContextKind::Closure) &&
          !param.FirstName.empty() &&
          param.SecondNameLoc.isValid()) {
        diagnose(param.FirstNameLoc, diag::parameter_operator_keyword_argument,
                 isClosure)
          .fixItRemoveChars(param.FirstNameLoc, param.SecondNameLoc);
        param.FirstName = param.SecondName;
        param.FirstNameLoc = param.SecondNameLoc;
        param.SecondName = Identifier();
        param.SecondNameLoc = SourceLoc();
      }

      // (':' type)?
      if (consumeIf(tok::colon)) {

        auto type = parseType(diag::expected_parameter_type);
        status |= type;
        param.Type = type.getPtrOrNull();

        // If we didn't parse a type, then we already diagnosed that the type
        // was invalid.  Remember that.
        if (type.isParseError() && !type.hasCodeCompletion())
          param.isInvalid = true;
      }
    } else {
      // Otherwise, we have invalid code.  Check to see if this looks like a
      // type.  If so, diagnose it as a common error.
      bool isBareType = false;
      {
        BacktrackingScope backtrack(*this);
        isBareType = canParseType() && Tok.isAny(tok::comma, tok::r_paren,
                                                 tok::equal);
      }

      if (isBareType) {
        // Otherwise, if this is a bare type, then the user forgot to name the
        // parameter, e.g. "func foo(Int) {}"
        SourceLoc typeStartLoc = Tok.getLoc();
        auto type = parseType(diag::expected_parameter_type, false);
        status |= type;
        param.Type = type.getPtrOrNull();

        // If this is a closure declaration, what is going
        // on is most likely argument destructuring, we are going
        // to diagnose that after all of the parameters are parsed.
        if (param.Type) {
          // Mark current parameter as invalid so it is possible
          // to diagnose it as destructuring of the closure parameter list.
          param.isInvalid = true;
          if (!isClosure) {
            // Unnamed parameters must be written as "_: Type".
            diagnose(typeStartLoc, diag::parameter_unnamed)
                .fixItInsert(typeStartLoc, "_: ");
          }
        }
      } else {
        // Otherwise, we're not sure what is going on, but this doesn't smell
        // like a parameter.
        diagnose(Tok, diag::expected_parameter_name);
        param.isInvalid = true;
        param.FirstNameLoc = Tok.getLoc();
        TokReceiver->registerTokenKindChange(param.FirstNameLoc,
                                             tok::identifier);
        status.setIsParseError();
      }
    }
                        
    // '...'?
    if (Tok.isEllipsis())
      param.EllipsisLoc = consumeToken();

    // ('=' expr)?
    if (Tok.is(tok::equal)) {
      SourceLoc EqualLoc = Tok.getLoc();
      status |= parseDefaultArgument(*this, defaultArgs, defaultArgIndex,
                                     param.DefaultArg, paramContext);

      if (param.EllipsisLoc.isValid() && param.DefaultArg) {
        // The range of the complete default argument.
        SourceRange defaultArgRange;
        if (auto init = param.DefaultArg)
          defaultArgRange = SourceRange(param.EllipsisLoc, init->getEndLoc());

        diagnose(EqualLoc, diag::parameter_vararg_default)
          .highlight(param.EllipsisLoc)
          .fixItRemove(defaultArgRange);
        param.isInvalid = true;
        param.DefaultArg = nullptr;
      }
    }

    // If we haven't made progress, don't add the parameter.
    if (Tok.getLoc() == StartLoc) {
      // If we took a default argument index for this parameter, but didn't add
      // one, then give it back.
      if (defaultArgs) defaultArgs->NextIndex--;
      return status;
    }

    params.push_back(param);
    return status;
  });
}

/// Map parsed parameters to a ParameterList.
static ParameterList *
mapParsedParameters(Parser &parser,
                    SourceLoc leftParenLoc,
                    MutableArrayRef<Parser::ParsedParameter> params,
                    SourceLoc rightParenLoc,
                    SmallVectorImpl<Identifier> *argNames,
                    Parser::ParameterContextKind paramContext) {
  auto &ctx = parser.Context;

  // Local function to create a pattern for a single parameter.
  auto createParam = [&](Parser::ParsedParameter &paramInfo,
                         Identifier argName, SourceLoc argNameLoc,
                         Identifier paramName, SourceLoc paramNameLoc)
  -> ParamDecl * {
    auto param = new (ctx) ParamDecl(paramInfo.SpecifierKind,
                                     paramInfo.SpecifierLoc,
                                     argNameLoc, argName,
                                     paramNameLoc, paramName, Type(),
                                     parser.CurDeclContext);    
    if (argNameLoc.isInvalid() && paramNameLoc.isInvalid())
      param->setImplicit();

    // If we diagnosed this parameter as a parse error, propagate to the decl.
    if (paramInfo.isInvalid)
      param->setInvalid();
    
    // If a type was provided, create the type for the parameter.
    if (auto type = paramInfo.Type) {
      // If 'inout' was specified, turn the type into an in-out type.
      if (paramInfo.SpecifierKind == VarDecl::Specifier::InOut) {
        auto InOutLoc = paramInfo.SpecifierLoc;
        if (isa<InOutTypeRepr>(type)) {
          parser.diagnose(InOutLoc, diag::parameter_specifier_repeated)
            .fixItRemove(InOutLoc);
        } else {
          parser.diagnose(InOutLoc, diag::inout_as_attr_disallowed, "'inout'")
            .fixItRemove(InOutLoc)
            .fixItInsert(type->getStartLoc(), "inout ");
          type = new (ctx) InOutTypeRepr(type, InOutLoc);
        }
      } else if (paramInfo.SpecifierKind == VarDecl::Specifier::Shared) {
        auto SpecifierLoc = paramInfo.SpecifierLoc;
        if (isa<SharedTypeRepr>(type)) {
          parser.diagnose(SpecifierLoc, diag::parameter_specifier_repeated)
            .fixItRemove(SpecifierLoc);
        } else {
          parser.diagnose(SpecifierLoc, diag::inout_as_attr_disallowed,
                          "'__shared'")
            .fixItRemove(SpecifierLoc)
            .fixItInsert(type->getStartLoc(), "__shared ");
          type = new (ctx) SharedTypeRepr(type, SpecifierLoc);
        }
      }
      param->getTypeLoc() = TypeLoc(type);
    } else if (paramContext != Parser::ParameterContextKind::Closure) {
      // Non-closure parameters require a type.
      if (!param->isInvalid())
        parser.diagnose(param->getLoc(), diag::missing_parameter_type);
      
      param->getTypeLoc() = TypeLoc::withoutLoc(ErrorType::get(ctx));
      param->setInvalid();
    } else if (paramInfo.SpecifierLoc.isValid()) {
      StringRef specifier;
      switch (paramInfo.SpecifierKind) {
      case VarDecl::Specifier::InOut:
        specifier = "'inout'";
        break;
      case VarDecl::Specifier::Shared:
        specifier = "'shared'";
        break;
      case VarDecl::Specifier::Let:
      case VarDecl::Specifier::Var:
        llvm_unreachable("can't have let or var here");
      }
      parser.diagnose(paramInfo.SpecifierLoc, diag::specifier_must_have_type,
                      specifier);
      paramInfo.SpecifierLoc = SourceLoc();
      paramInfo.SpecifierKind = VarDecl::Specifier::Owned;
    }
    return param;
  };

  // Collect the elements of the tuple patterns for argument and body
  // parameters.
  SmallVector<ParamDecl*, 4> elements;
  SourceLoc ellipsisLoc;

  for (auto &param : params) {
    // Whether the provided name is API by default depends on the parameter
    // context.
    bool isKeywordArgumentByDefault;
    switch (paramContext) {
    case Parser::ParameterContextKind::Closure:
    case Parser::ParameterContextKind::Subscript:
    case Parser::ParameterContextKind::Operator:
      isKeywordArgumentByDefault = false;
      break;
    case Parser::ParameterContextKind::Curried:
    case Parser::ParameterContextKind::Initializer:
    case Parser::ParameterContextKind::Function:
      isKeywordArgumentByDefault = true;
      break;
    }

    // Create the pattern.
    ParamDecl *result = nullptr;
    Identifier argName;
    Identifier paramName;
    if (param.SecondNameLoc.isValid()) {
      argName = param.FirstName;
      paramName = param.SecondName;

      // Both names were provided, so pass them in directly.
      result = createParam(param, argName, param.FirstNameLoc,
                           paramName, param.SecondNameLoc);

      // If the first and second names are equivalent and non-empty, and we
      // would have an argument label by default, complain.
      if (isKeywordArgumentByDefault && param.FirstName == param.SecondName
          && !param.FirstName.empty()) {
        parser.diagnose(param.FirstNameLoc,
                        diag::parameter_extraneous_double_up,
                        param.FirstName)
          .fixItRemoveChars(param.FirstNameLoc, param.SecondNameLoc);
      }
    } else {
      if (isKeywordArgumentByDefault)
        argName = param.FirstName;
      paramName = param.FirstName;

      result = createParam(param, argName, SourceLoc(),
                           param.FirstName, param.FirstNameLoc);
    }

    // Warn when an unlabeled parameter follows a variadic parameter
    if (ellipsisLoc.isValid() && elements.back()->isVariadic() &&
        param.FirstName.empty()) {
      auto message =
          parser.Context.isSwiftVersion3()
              ? diag::swift3_unlabeled_parameter_following_variadic_parameter
              : diag::unlabeled_parameter_following_variadic_parameter;
      parser.diagnose(param.FirstNameLoc, message);
    }
    
    // If this parameter had an ellipsis, check whether it's the last parameter.
    if (param.EllipsisLoc.isValid()) {
      if (ellipsisLoc.isValid()) {
        parser.diagnose(param.EllipsisLoc, diag::multiple_parameter_ellipsis)
          .highlight(ellipsisLoc)
          .fixItRemove(param.EllipsisLoc);

        param.EllipsisLoc = SourceLoc();
      } else if (!result->getTypeLoc().getTypeRepr()) {
        parser.diagnose(param.EllipsisLoc, diag::untyped_pattern_ellipsis)
          .highlight(result->getSourceRange());

        param.EllipsisLoc = SourceLoc();
      } else {
        ellipsisLoc = param.EllipsisLoc;
        result->setVariadic();
      }
    }

    if (param.DefaultArg) {
      assert((paramContext == Parser::ParameterContextKind::Function ||
              paramContext == Parser::ParameterContextKind::Operator ||
              paramContext == Parser::ParameterContextKind::Initializer) &&
             "Default arguments are only permitted on the first param clause");
      result->setDefaultArgumentKind(getDefaultArgKind(param.DefaultArg));
      result->setDefaultValue(param.DefaultArg);
    }

    elements.push_back(result);

    if (argNames)
      argNames->push_back(argName);
  }

  return ParameterList::create(ctx, leftParenLoc, elements, rightParenLoc);
}

/// Parse a single parameter-clause.
ParserResult<ParameterList>
Parser::parseSingleParameterClause(ParameterContextKind paramContext,
                                   SmallVectorImpl<Identifier> *namePieces,
                                   DefaultArgumentInfo *defaultArgs) {
  if (!Tok.is(tok::l_paren)) {
    // If we don't have the leading '(', complain.
    Diag<> diagID;
    bool skipIdentifier = false;
    switch (paramContext) {
    case ParameterContextKind::Function:
    case ParameterContextKind::Operator:
      diagID = diag::func_decl_without_paren;
      break;
    case ParameterContextKind::Subscript:
      skipIdentifier = Tok.is(tok::identifier) &&
                       peekToken().is(tok::l_paren);
      diagID = skipIdentifier ? diag::subscript_has_name
                              : diag::expected_lparen_subscript;
      break;
    case ParameterContextKind::Initializer:
      skipIdentifier = Tok.is(tok::identifier) &&
                       peekToken().is(tok::l_paren);
      diagID = skipIdentifier ? diag::initializer_has_name
                              : diag::expected_lparen_initializer;
      break;
    case ParameterContextKind::Closure:
    case ParameterContextKind::Curried:
      llvm_unreachable("should never be here");
    }

    {
      auto diag = diagnose(Tok, diagID);
      if (Tok.isAny(tok::l_brace, tok::arrow, tok::kw_throws, tok::kw_rethrows))
        diag.fixItInsertAfter(PreviousLoc, "()");

      if (skipIdentifier)
        diag.fixItRemove(Tok.getLoc());
    }

    // We might diagnose again down here, so make sure 'diag' is out of scope.
    if (skipIdentifier) {
      consumeToken();
      skipSingle();
    }
    
    // Create an empty parameter list to recover.
    return makeParserErrorResult(
        ParameterList::createEmpty(Context, PreviousLoc, PreviousLoc));
  }

  ParserStatus status;
  SmallVector<ParsedParameter, 4> params;
  SourceLoc leftParenLoc, rightParenLoc;
  
  // Parse the parameter clause.
  status |= parseParameterClause(leftParenLoc, params, rightParenLoc,
                                 defaultArgs, paramContext);
  
  // Turn the parameter clause into argument and body patterns.
  auto paramList = mapParsedParameters(*this, leftParenLoc, params,
                                       rightParenLoc, namePieces, paramContext);

  return makeParserResult(status, paramList);
}

/// Parse function arguments.
///   func-arguments:
///     curried-arguments | selector-arguments
///   curried-arguments:
///     parameter-clause+
///   selector-arguments:
///     '(' selector-element ')' (identifier '(' selector-element ')')+
///   selector-element:
///      identifier '(' pattern-atom (':' type)? ('=' expr)? ')'
///
ParserStatus
Parser::parseFunctionArguments(SmallVectorImpl<Identifier> &NamePieces,
                               SmallVectorImpl<ParameterList*> &BodyParams,
                               ParameterContextKind paramContext,
                               DefaultArgumentInfo &DefaultArgs) {
  // Parse parameter-clauses.
  ParserStatus status;
  unsigned FirstBodyPatternIndex = BodyParams.size();

  auto FirstParameterClause
    = parseSingleParameterClause(paramContext, &NamePieces, &DefaultArgs);
  status |= FirstParameterClause;
  BodyParams.push_back(FirstParameterClause.get());

  while (Tok.is(tok::l_paren)) {
    auto CurriedParameterClause
      = parseSingleParameterClause(ParameterContextKind::Curried);
    status |= CurriedParameterClause;
    BodyParams.push_back(CurriedParameterClause.get());
  }

  // If the decl uses currying syntax, complain that that syntax has gone away.
  if (BodyParams.size() - FirstBodyPatternIndex > 1) {
    SourceRange allPatternsRange(
      BodyParams[FirstBodyPatternIndex]->getStartLoc(),
      BodyParams.back()->getEndLoc());
    auto diag = diagnose(allPatternsRange.Start,
                         diag::parameter_curry_syntax_removed);
    diag.highlight(allPatternsRange);
    bool seenArg = false;
    for (unsigned i = FirstBodyPatternIndex; i < BodyParams.size() - 1; i++) {
      // Replace ")(" with ", ", so "(x: Int)(y: Int)" becomes
      // "(x: Int, y: Int)". But just delete them if they're not actually
      // separating any arguments, e.g. in "()(y: Int)".
      StringRef replacement(", ");
      auto *leftParamList = BodyParams[i];
      auto *rightParamList = BodyParams[i + 1];
      if (leftParamList->size() != 0)
        seenArg = true;
      if (!seenArg || rightParamList->size() == 0)
        replacement = "";
    
      diag.fixItReplace(SourceRange(leftParamList->getEndLoc(),
                                    rightParamList->getStartLoc()),
                        replacement);
    }
  }

  return status;
}

/// Parse a function definition signature.
///   func-signature:
///     func-arguments func-throws? func-signature-result?
///   func-signature-result:
///     '->' type
///
/// Note that this leaves retType as null if unspecified.
ParserStatus
Parser::parseFunctionSignature(Identifier SimpleName,
                               DeclName &FullName,
                               SmallVectorImpl<ParameterList*> &bodyParams,
                               DefaultArgumentInfo &defaultArgs,
                               SourceLoc &throwsLoc,
                               bool &rethrows,
                               TypeRepr *&retType) {
  SmallVector<Identifier, 4> NamePieces;
  ParserStatus Status;

  ParameterContextKind paramContext = SimpleName.isOperator() ?
    ParameterContextKind::Operator : ParameterContextKind::Function;
  Status |= parseFunctionArguments(NamePieces, bodyParams, paramContext,
                                   defaultArgs);
  FullName = DeclName(Context, SimpleName, NamePieces);
  
  // Check for the 'throws' keyword.
  rethrows = false;
  if (Tok.is(tok::kw_throws)) {
    throwsLoc = consumeToken();
  } else if (Tok.is(tok::kw_rethrows)) {
    throwsLoc = consumeToken();
    rethrows = true;
  } else if (Tok.is(tok::kw_throw)) {
    throwsLoc = consumeToken();
    diagnose(throwsLoc, diag::throw_in_function_type)
      .fixItReplace(throwsLoc, "throws");
  }

  SourceLoc arrowLoc;

  auto diagnoseInvalidThrows = [&]() -> Optional<InFlightDiagnostic> {
    if (throwsLoc.isValid())
      return None;

    if (Tok.is(tok::kw_throws)) {
      throwsLoc = consumeToken();
    } else if (Tok.is(tok::kw_rethrows)) {
      throwsLoc = consumeToken();
      rethrows = true;
    }

    if (!throwsLoc.isValid())
      return None;

    auto diag = rethrows ? diag::rethrows_in_wrong_position
                         : diag::throws_in_wrong_position;
    return diagnose(Tok, diag);
  };

  // If there's a trailing arrow, parse the rest as the result type.
  if (Tok.isAny(tok::arrow, tok::colon)) {
    if (!consumeIf(tok::arrow, arrowLoc)) {
      // FixIt ':' to '->'.
      diagnose(Tok, diag::func_decl_expected_arrow)
          .fixItReplace(Tok.getLoc(), " -> ");
      arrowLoc = consumeToken(tok::colon);
    }

    // Check for 'throws' and 'rethrows' after the arrow, but
    // before the type, and correct it.
    if (auto diagOpt = diagnoseInvalidThrows()) {
      assert(arrowLoc.isValid());
      assert(throwsLoc.isValid());
      (*diagOpt).fixItExchange(SourceRange(arrowLoc),
                               SourceRange(throwsLoc));
    }

    ParserResult<TypeRepr> ResultType =
      parseType(diag::expected_type_function_result);
    if (ResultType.hasCodeCompletion())
      return ResultType;
    retType = ResultType.getPtrOrNull();
    if (!retType) {
      Status.setIsParseError();
      return Status;
    }
  } else {
    // Otherwise, we leave retType null.
    retType = nullptr;
  }

  // Check for 'throws' and 'rethrows' after the type and correct it.
  if (auto diagOpt = diagnoseInvalidThrows()) {
    assert(arrowLoc.isValid());
    assert(retType);
    SourceLoc typeEndLoc = Lexer::getLocForEndOfToken(SourceMgr,
                                                      retType->getEndLoc());
    SourceLoc throwsEndLoc = Lexer::getLocForEndOfToken(SourceMgr, throwsLoc);
    (*diagOpt).fixItInsert(arrowLoc, rethrows ? "rethrows " : "throws ")
              .fixItRemoveChars(typeEndLoc, throwsEndLoc);
  }

  return Status;
}


/// Parse a pattern with an optional type annotation.
///
///  typed-pattern ::= pattern (':' type)?
///
ParserResult<Pattern> Parser::parseTypedPattern() {
  auto result = parsePattern();
  
  // Now parse an optional type annotation.
  if (Tok.is(tok::colon)) {
    SourceLoc colonLoc = consumeToken(tok::colon);
    
    if (result.isNull())  // Recover by creating AnyPattern.
      result = makeParserErrorResult(new (Context) AnyPattern(colonLoc));
    
    ParserResult<TypeRepr> Ty = parseType();
    if (Ty.hasCodeCompletion())
      return makeParserCodeCompletionResult<Pattern>();
    if (!Ty.isNull()) {
      // Attempt to diagnose initializer calls incorrectly written
      // as typed patterns, such as "var x: [Int]()".
      // Disable this tentative parse when in code-completion mode, otherwise
      // code-completion may enter the delayed-decl state twice.
      if (Tok.isFollowingLParen() && !L->isCodeCompletion()) {
        BacktrackingScope backtrack(*this);
        
        // Create a local context if needed so we can parse trailing closures.
        LocalContext dummyContext;
        Optional<ContextChange> contextChange;
        if (!CurLocalContext) {
          contextChange.emplace(*this, CurDeclContext, &dummyContext);
        }
        
        SourceLoc lParenLoc, rParenLoc;
        SmallVector<Expr *, 2> args;
        SmallVector<Identifier, 2> argLabels;
        SmallVector<SourceLoc, 2> argLabelLocs;
        Expr *trailingClosure;
        ParserStatus status = parseExprList(tok::l_paren, tok::r_paren,
                                            /*isPostfix=*/true,
                                            /*isExprBasic=*/false,
                                            lParenLoc, args, argLabels,
                                            argLabelLocs, rParenLoc,
                                            trailingClosure,
                                            SyntaxKind::Unknown);
        if (status.isSuccess()) {
          backtrack.cancelBacktrack();
          
          // Suggest replacing ':' with '='
          diagnose(lParenLoc, diag::initializer_as_typed_pattern)
            .highlight({Ty.get()->getStartLoc(), rParenLoc})
            .fixItReplace(colonLoc, " = ");
          result.setIsParseError();
        }
      }
    } else {
      Ty = makeParserResult(new (Context) ErrorTypeRepr(PreviousLoc));
    }
    
    result = makeParserResult(result,
                            new (Context) TypedPattern(result.get(), Ty.get()));
  }
  
  return result;
}

/// Parse a pattern.
///   pattern ::= identifier
///   pattern ::= '_'
///   pattern ::= pattern-tuple
///   pattern ::= 'var' pattern
///   pattern ::= 'let' pattern
///
ParserResult<Pattern> Parser::parsePattern() {
  switch (Tok.getKind()) {
  case tok::l_paren:
    return parsePatternTuple();
    
  case tok::kw__:
    return makeParserResult(new (Context) AnyPattern(consumeToken(tok::kw__)));
    
  case tok::identifier: {
    Identifier name;
    SourceLoc loc = consumeIdentifier(&name);
    bool isLet = (InVarOrLetPattern != IVOLP_InVar);
    auto specifier = isLet
                   ? VarDecl::Specifier::Let
                   : VarDecl::Specifier::Var;
    if (Tok.isIdentifierOrUnderscore() && !Tok.isContextualDeclKeyword())
      diagnoseConsecutiveIDs(name.str(), loc, isLet ? "constant" : "variable");

    return makeParserResult(createBindingFromPattern(loc, name, specifier));
  }
    
  case tok::code_complete:
    if (!CurDeclContext->getAsNominalTypeOrNominalTypeExtensionContext()) {
      // This cannot be an overridden property, so just eat the token. We cannot
      // code complete anything here -- we expect an identifier.
      consumeToken(tok::code_complete);
    }
    return nullptr;
    
  case tok::kw_var:
  case tok::kw_let: {
    bool isLet = Tok.is(tok::kw_let);
    SourceLoc varLoc = consumeToken();
    
    // 'var' and 'let' patterns shouldn't nest.
    if (InVarOrLetPattern == IVOLP_InLet ||
        InVarOrLetPattern == IVOLP_InVar)
      diagnose(varLoc, diag::var_pattern_in_var, unsigned(isLet));
    
    // 'let' isn't valid inside an implicitly immutable context, but var is.
    if (isLet && InVarOrLetPattern == IVOLP_ImplicitlyImmutable)
      diagnose(varLoc, diag::let_pattern_in_immutable_context);
    
    // In our recursive parse, remember that we're in a var/let pattern.
    llvm::SaveAndRestore<decltype(InVarOrLetPattern)>
    T(InVarOrLetPattern, isLet ? IVOLP_InLet : IVOLP_InVar);
    
    ParserResult<Pattern> subPattern = parsePattern();
    if (subPattern.hasCodeCompletion())
      return makeParserCodeCompletionResult<Pattern>();
    if (subPattern.isNull())
      return nullptr;
    return makeParserResult(new (Context) VarPattern(varLoc, isLet,
                                                     subPattern.get()));
  }
      
  default:
    if (Tok.isKeyword() &&
        (peekToken().is(tok::colon) || peekToken().is(tok::equal))) {
      diagnose(Tok, diag::keyword_cant_be_identifier, Tok.getText());
      diagnose(Tok, diag::backticks_to_escape)
        .fixItReplace(Tok.getLoc(), "`" + Tok.getText().str() + "`");
      SourceLoc Loc = Tok.getLoc();
      consumeToken();
      return makeParserErrorResult(new (Context) AnyPattern(Loc));
    }
    diagnose(Tok, diag::expected_pattern);
    return nullptr;
  }
}

Pattern *Parser::createBindingFromPattern(SourceLoc loc, Identifier name,
                                          VarDecl::Specifier specifier) {
  auto var = new (Context) VarDecl(/*IsStatic*/false, specifier,
                                   /*IsCaptureList*/false, loc, name, Type(),
                                   CurDeclContext);
  return new (Context) NamedPattern(var);
}

/// Parse an element of a tuple pattern.
///
///   pattern-tuple-element:
///     (identifier ':')? pattern
std::pair<ParserStatus, Optional<TuplePatternElt>>
Parser::parsePatternTupleElement() {
  // If this element has a label, parse it.
  Identifier Label;
  SourceLoc LabelLoc;

  // If the tuple element has a label, parse it.
  if (Tok.is(tok::identifier) && peekToken().is(tok::colon)) {
    LabelLoc = consumeIdentifier(&Label);
    consumeToken(tok::colon);
  }

  // Parse the pattern.
  ParserResult<Pattern>  pattern = parsePattern();
  if (pattern.hasCodeCompletion())
    return std::make_pair(makeParserCodeCompletionStatus(), None);
  if (pattern.isNull())
    return std::make_pair(makeParserError(), None);

  auto Elt = TuplePatternElt(Label, LabelLoc, pattern.get());
  return std::make_pair(makeParserSuccess(), Elt);
}

/// Parse a tuple pattern.
///
///   pattern-tuple:
///     '(' pattern-tuple-body? ')'
///   pattern-tuple-body:
///     pattern-tuple-element (',' pattern-tuple-body)*
ParserResult<Pattern> Parser::parsePatternTuple() {
  StructureMarkerRAII ParsingPatternTuple(*this, Tok);
  SourceLoc LPLoc = consumeToken(tok::l_paren);
  SourceLoc RPLoc;

  // Parse all the elements.
  SmallVector<TuplePatternElt, 8> elts;
  ParserStatus ListStatus =
    parseList(tok::r_paren, LPLoc, RPLoc,
              /*AllowSepAfterLast=*/false,
              diag::expected_rparen_tuple_pattern_list,
              SyntaxKind::Unknown,
              [&] () -> ParserStatus {
    // Parse the pattern tuple element.
    ParserStatus EltStatus;
    Optional<TuplePatternElt> elt;
    std::tie(EltStatus, elt) = parsePatternTupleElement();
    if (EltStatus.hasCodeCompletion())
      return makeParserCodeCompletionStatus();
    if (!elt)
      return makeParserError();

    // Add this element to the list.
    elts.push_back(*elt);
    return makeParserSuccess();
  });

  return makeParserResult(
           ListStatus,
           TuplePattern::createSimple(Context, LPLoc, elts, RPLoc));
}

/// Parse an optional type annotation on a pattern.
///
///  pattern-type-annotation ::= (':' type)?
///
ParserResult<Pattern> Parser::
parseOptionalPatternTypeAnnotation(ParserResult<Pattern> result,
                                   bool isOptional) {

  // Parse an optional type annotation.
  if (!consumeIf(tok::colon))
    return result;

  Pattern *P;
  if (result.isNull())
    return nullptr;
  else
    P = result.get();

  ParserResult<TypeRepr> Ty = parseType();
  if (Ty.hasCodeCompletion())
    return makeParserCodeCompletionResult<Pattern>();

  TypeRepr *repr = Ty.getPtrOrNull();
  if (!repr)
    repr = new (Context) ErrorTypeRepr(PreviousLoc);

  // In an if-let, the actual type of the expression is Optional of whatever
  // was written.
  if (isOptional)
    repr = new (Context) OptionalTypeRepr(repr, SourceLoc());

  return makeParserResult(new (Context) TypedPattern(P, repr));
}



/// matching-pattern ::= 'is' type
/// matching-pattern ::= matching-pattern-var
/// matching-pattern ::= expr
///
ParserResult<Pattern> Parser::parseMatchingPattern(bool isExprBasic) {
  // TODO: Since we expect a pattern in this position, we should optimistically
  // parse pattern nodes for productions shared by pattern and expression
  // grammar. For short-term ease of initial implementation, we always go
  // through the expr parser for ambiguous productions.

  // Parse productions that can only be patterns.
  if (Tok.isAny(tok::kw_var, tok::kw_let)) {
    assert(Tok.isAny(tok::kw_let, tok::kw_var) && "expects var or let");
    bool isLet = Tok.is(tok::kw_let);
    SourceLoc varLoc = consumeToken();
    
    return parseMatchingPatternAsLetOrVar(isLet, varLoc, isExprBasic);
  }
  
  // matching-pattern ::= 'is' type
  if (Tok.is(tok::kw_is)) {
    SourceLoc isLoc = consumeToken(tok::kw_is);
    ParserResult<TypeRepr> castType = parseType();
    if (castType.isNull() || castType.hasCodeCompletion())
      return nullptr;
    return makeParserResult(new (Context) IsPattern(isLoc, castType.get(),
                                                    nullptr,
                                                    CheckedCastKind::Unresolved));
  }

  // matching-pattern ::= expr
  // Fall back to expression parsing for ambiguous forms. Name lookup will
  // disambiguate.
  ParserResult<Expr> subExpr =
    parseExprImpl(diag::expected_pattern, isExprBasic);
  if (subExpr.hasCodeCompletion())
    return makeParserCodeCompletionStatus();
  if (subExpr.isNull())
    return nullptr;
  
  // The most common case here is to parse something that was a lexically
  // obvious pattern, which will come back wrapped in an immediate
  // UnresolvedPatternExpr.  Transform this now to simplify later code.
  if (auto *UPE = dyn_cast<UnresolvedPatternExpr>(subExpr.get()))
    return makeParserResult(UPE->getSubPattern());
  
  return makeParserResult(new (Context) ExprPattern(subExpr.get()));
}

ParserResult<Pattern> Parser::parseMatchingPatternAsLetOrVar(bool isLet,
                                                             SourceLoc varLoc,
                                                             bool isExprBasic) {
  // 'var' and 'let' patterns shouldn't nest.
  if (InVarOrLetPattern == IVOLP_InLet ||
      InVarOrLetPattern == IVOLP_InVar)
    diagnose(varLoc, diag::var_pattern_in_var, unsigned(isLet));
  
  // 'let' isn't valid inside an implicitly immutable context, but var is.
  if (isLet && InVarOrLetPattern == IVOLP_ImplicitlyImmutable)
    diagnose(varLoc, diag::let_pattern_in_immutable_context);

  // In our recursive parse, remember that we're in a var/let pattern.
  llvm::SaveAndRestore<decltype(InVarOrLetPattern)>
    T(InVarOrLetPattern, isLet ? IVOLP_InLet : IVOLP_InVar);

  ParserResult<Pattern> subPattern = parseMatchingPattern(isExprBasic);
  if (subPattern.isNull())
    return nullptr;
  auto *varP = new (Context) VarPattern(varLoc, isLet, subPattern.get());
  return makeParserResult(varP);
}


bool Parser::isOnlyStartOfMatchingPattern() {
  return Tok.isAny(tok::kw_var, tok::kw_let, tok::kw_is);
}


static bool canParsePatternTuple(Parser &P);

///   pattern ::= identifier
///   pattern ::= '_'
///   pattern ::= pattern-tuple
///   pattern ::= 'var' pattern
///   pattern ::= 'let' pattern
static bool canParsePattern(Parser &P) {
  switch (P.Tok.getKind()) {
  case tok::identifier:
  case tok::kw__:
    P.consumeToken();
    return true;
  case tok::kw_let:
  case tok::kw_var:
    P.consumeToken();
    return canParsePattern(P);
  case tok::l_paren:
    return canParsePatternTuple(P);

  default:
    return false;
  }
}


static bool canParsePatternTuple(Parser &P) {
  if (!P.consumeIf(tok::l_paren)) return false;

  if (P.Tok.isNot(tok::r_paren)) {
    do {
      if (!canParsePattern(P)) return false;
    } while (P.consumeIf(tok::comma));
  }

  return P.consumeIf(tok::r_paren);
}

///  typed-pattern ::= pattern (':' type)?
///
bool Parser::canParseTypedPattern() {
  if (!canParsePattern(*this)) return false;
  
  if (consumeIf(tok::colon))
    return canParseType();
  return true;
}


