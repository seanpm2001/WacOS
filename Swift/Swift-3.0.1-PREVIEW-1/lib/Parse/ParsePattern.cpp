//===--- ParsePattern.cpp - Swift Language Parser for Patterns ------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// Pattern Parsing and AST Building
//
//===----------------------------------------------------------------------===//

#include "swift/Parse/CodeCompletionCallbacks.h"
#include "swift/Parse/Parser.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/ExprHandle.h"
#include "swift/Basic/StringExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/SaveAndRestore.h"
using namespace swift;

/// \brief Determine the kind of a default argument given a parsed
/// expression that has not yet been type-checked.
static DefaultArgumentKind getDefaultArgKind(ExprHandle *init) {
  if (!init || !init->getExpr())
    return DefaultArgumentKind::None;

  auto magic = dyn_cast<MagicIdentifierLiteralExpr>(init->getExpr());
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
}

void Parser::DefaultArgumentInfo::setFunctionContext(DeclContext *DC) {
  assert(DC->isLocalContext());
  for (auto context : ParsedContexts) {
    context->changeFunction(DC);
  }
}

static ParserStatus parseDefaultArgument(Parser &P,
                                   Parser::DefaultArgumentInfo *defaultArgs,
                                   unsigned argIndex,
                                   ExprHandle *&init,
                                 Parser::ParameterContextKind paramContext) {
  SourceLoc equalLoc = P.consumeToken(tok::equal);

  // Enter a fresh default-argument context with a meaningless parent.
  // We'll change the parent to the function later after we've created
  // that declaration.
  auto initDC =
    P.Context.createDefaultArgumentContext(P.CurDeclContext, argIndex);
  Parser::ParseFunctionBody initScope(P, initDC);

  ParserResult<Expr> initR = P.parseExpr(diag::expected_init_value);

  // Give back the default-argument context if we didn't need it.
  if (!initScope.hasClosures()) {
    P.Context.destroyDefaultArgumentContext(initDC);

  // Otherwise, record it if we're supposed to accept default
  // arguments here.
  } else if (defaultArgs) {
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
  
  assert(((diagID.ID != DiagID()) == !defaultArgs ||
          // Sometimes curried method parameter lists get default arg info.
          // Remove this when they go away.
          paramContext == Parser::ParameterContextKind::Curried) &&
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

  init = ExprHandle::get(P.Context, initR.get());
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
  return parseList(tok::r_paren, leftParenLoc, rightParenLoc, tok::comma,
                      /*OptionalSep=*/false, /*AllowSepAfterLast=*/false,
                      diag::expected_rparen_parameter,
                      [&]() -> ParserStatus {
    ParsedParameter param;
    ParserStatus status;
    SourceLoc StartLoc = Tok.getLoc();

    unsigned defaultArgIndex = defaultArgs ? defaultArgs->NextIndex++ : 0;

    // Attributes.
    bool FoundCCToken;
    parseDeclAttributeList(param.Attrs, FoundCCToken,
                          /*stop at type attributes*/true, true);
    if (FoundCCToken) {
      if (CodeCompletion) {
        CodeCompletion->completeDeclAttrKeyword(nullptr, isInSILMode(), true);
      } else {
        status |= makeParserCodeCompletionStatus();
      }
    }
    
    // ('inout' | 'let' | 'var')?
    bool hasSpecifier = false;
    while (Tok.isAny(tok::kw_inout, tok::kw_let, tok::kw_var)) {
      if (!hasSpecifier) {
        if (Tok.is(tok::kw_let)) {
          diagnose(Tok, diag::parameter_let_as_attr)
          .fixItRemove(Tok.getLoc());
          param.isInvalid = true;
        } else {
          // We handle the var error in sema for a better fixit and inout is
          // handled later in this function for better fixits.
          param.SpecifierKind = Tok.is(tok::kw_inout) ? ParsedParameter::InOut :
                                                        ParsedParameter::Var;
        }
        param.LetVarInOutLoc = consumeToken();
        hasSpecifier = true;
      } else {
        // Redundant specifiers are fairly common, recognize, reject, and recover
        // from this gracefully.
        diagnose(Tok, diag::parameter_inout_var_let_repeated)
        .fixItRemove(Tok.getLoc());
        consumeToken();
        param.isInvalid = true;
      }
    }

    if (startsParameterName(*this, isClosure)) {
      // identifier-or-none for the first name
      if (Tok.is(tok::kw__)) {
        param.FirstNameLoc = consumeToken();
      } else {
        assert(Tok.canBeArgumentLabel() && "startsParameterName() lied");
        param.FirstName = Context.getIdentifier(Tok.getText());
        param.FirstNameLoc = consumeToken();
      }

      // identifier-or-none? for the second name
      if (Tok.canBeArgumentLabel()) {
        if (!Tok.is(tok::kw__))
          param.SecondName = Context.getIdentifier(Tok.getText());

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

      // (':' ('inout')? type)?
      if (consumeIf(tok::colon)) {

        SourceLoc postColonLoc = Tok.getLoc();

        bool hasDeprecatedInOut =
          param.SpecifierKind == ParsedParameter::InOut;
        bool hasValidInOut = false;
        
        while (Tok.is(tok::kw_inout)) {
          hasValidInOut = true;
          if (hasSpecifier) {
            diagnose(Tok.getLoc(), diag::parameter_inout_var_let_repeated)
            .fixItRemove(param.LetVarInOutLoc);
            consumeToken(tok::kw_inout);
            param.isInvalid = true;
          } else {
            hasSpecifier = true;
            param.LetVarInOutLoc = consumeToken(tok::kw_inout);
            param.SpecifierKind = ParsedParameter::InOut;
          }
        }
        if (!hasValidInOut && hasDeprecatedInOut) {
          diagnose(Tok.getLoc(), diag::inout_as_attr_disallowed)
          .fixItRemove(param.LetVarInOutLoc)
          .fixItInsert(postColonLoc, "inout ");
          param.isInvalid = true;
        }
        
        auto type = parseType(diag::expected_parameter_type);
        status |= type;
        param.Type = type.getPtrOrNull();

        if (param.SpecifierKind == ParsedParameter::InOut) {
          if (auto *fnTR = dyn_cast_or_null<FunctionTypeRepr>(param.Type)) {
            // If the input to the function isn't parenthesized, apply the inout
            // to the first (only) parameter, as we would in Swift 2. (This
            // syntax is deprecated in Swift 3.)
            TypeRepr *argsTR = fnTR->getArgsTypeRepr();
            if (!isa<TupleTypeRepr>(argsTR)) {
              auto *newArgsTR =
                  new (Context) InOutTypeRepr(argsTR, param.LetVarInOutLoc);
              auto *newTR =
                  new (Context) FunctionTypeRepr(fnTR->getGenericParams(),
                                                 newArgsTR,
                                                 fnTR->getThrowsLoc(),
                                                 fnTR->getArrowLoc(),
                                                 fnTR->getResultTypeRepr());
              newTR->setGenericSignature(fnTR->getGenericSignature());
              param.Type = newTR;
              param.SpecifierKind = ParsedParameter::Let;
              param.LetVarInOutLoc = SourceLoc();
            }
          }
        }

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

        // Unnamed parameters must be written as "_: Type".
        if (param.Type) {
          diagnose(typeStartLoc, diag::parameter_unnamed)
            .fixItInsert(typeStartLoc, "_: ");
        } else {
          param.isInvalid = true;
        }
      } else {
        // Otherwise, we're not sure what is going on, but this doesn't smell
        // like a parameter.
        diagnose(Tok, diag::expected_parameter_name);
        param.isInvalid = true;
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
        if (auto init = param.DefaultArg->getExpr())
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
                    bool isFirstParameterClause,
                    SmallVectorImpl<Identifier> *argNames,
                    Parser::ParameterContextKind paramContext) {
  auto &ctx = parser.Context;

  // Local function to create a pattern for a single parameter.
  auto createParam = [&](Parser::ParsedParameter &paramInfo,
                         Identifier argName, SourceLoc argNameLoc,
                         Identifier paramName, SourceLoc paramNameLoc)
  -> ParamDecl * {
    auto specifierKind = paramInfo.SpecifierKind;
    bool isLet = specifierKind == Parser::ParsedParameter::Let;
    auto param = new (ctx) ParamDecl(isLet, paramInfo.LetVarInOutLoc,
                                     argNameLoc, argName,
                                     paramNameLoc, paramName, Type(),
                                     parser.CurDeclContext);
    param->getAttrs() = paramInfo.Attrs;
    
    if (argNameLoc.isInvalid() && paramNameLoc.isInvalid())
      param->setImplicit();

    // If we diagnosed this parameter as a parse error, propagate to the decl.
    if (paramInfo.isInvalid)
      param->setInvalid();
    
    // If a type was provided, create the type for the parameter.
    if (auto type = paramInfo.Type) {
      // If 'inout' was specified, turn the type into an in-out type.
      if (specifierKind == Parser::ParsedParameter::InOut) {
        type = new (ctx) InOutTypeRepr(type, paramInfo.LetVarInOutLoc);
        param->setInOut(true);
      }

      param->getTypeLoc() = TypeLoc(type);
    } else if (paramContext != Parser::ParameterContextKind::Closure) {
      // Non-closure parameters require a type.
      if (!param->isInvalid())
        parser.diagnose(param->getLoc(), diag::missing_parameter_type);
      
      param->getTypeLoc() = TypeLoc::withoutLoc(ErrorType::get(ctx));
      param->setInvalid();
    } else if (specifierKind == Parser::ParsedParameter::InOut) {
      parser.diagnose(paramInfo.LetVarInOutLoc, diag::inout_must_have_type);
      paramInfo.LetVarInOutLoc = SourceLoc();
      specifierKind = Parser::ParsedParameter::Let;
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
      isKeywordArgumentByDefault = !isFirstParameterClause;
      break;
    case Parser::ParameterContextKind::Curried:
    case Parser::ParameterContextKind::Initializer:
      isKeywordArgumentByDefault = true;
      break;
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
      assert(isFirstParameterClause &&
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
ParserResult<ParameterList> Parser::parseSingleParameterClause(
                                ParameterContextKind paramContext,
                                SmallVectorImpl<Identifier> *namePieces) {
  ParserStatus status;
  SmallVector<ParsedParameter, 4> params;
  SourceLoc leftParenLoc, rightParenLoc;
  
  // Parse the parameter clause.
  status |= parseParameterClause(leftParenLoc, params, rightParenLoc,
                                 /*defaultArgs=*/nullptr, paramContext);
  
  // Turn the parameter clause into argument and body patterns.
  auto paramList = mapParsedParameters(*this, leftParenLoc, params,
                                     rightParenLoc, true, namePieces,
                                     paramContext);

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
  bool isFirstParameterClause = true;
  unsigned FirstBodyPatternIndex = BodyParams.size();
  while (Tok.is(tok::l_paren)) {
    SmallVector<ParsedParameter, 4> params;
    SourceLoc leftParenLoc, rightParenLoc;

    // Parse the parameter clause.
    status |= parseParameterClause(leftParenLoc, params, rightParenLoc,
                                   &DefaultArgs, paramContext);

    // Turn the parameter clause into argument and body patterns.
    auto pattern = mapParsedParameters(*this, leftParenLoc, params,
                                       rightParenLoc, 
                                       isFirstParameterClause,
                                       isFirstParameterClause ? &NamePieces
                                                              : nullptr,
                                       paramContext);
    BodyParams.push_back(pattern);
    isFirstParameterClause = false;
    paramContext = ParameterContextKind::Curried;
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
  NamePieces.push_back(SimpleName);
  FullName = SimpleName;
  
  ParserStatus Status;
  // We force first type of a func declaration to be a tuple for consistency.
  if (Tok.is(tok::l_paren)) {
    ParameterContextKind paramContext;
    if (SimpleName.isOperator())
      paramContext = ParameterContextKind::Operator;
    else
      paramContext = ParameterContextKind::Function;

    Status = parseFunctionArguments(NamePieces, bodyParams, paramContext,
                                    defaultArgs);
    FullName = DeclName(Context, SimpleName, 
                        llvm::makeArrayRef(NamePieces.begin() + 1,
                                           NamePieces.end()));

    if (bodyParams.empty()) {
      // If we didn't get anything, add a () pattern to avoid breaking
      // invariants.
      assert(Status.hasCodeCompletion() || Status.isError());
      bodyParams.push_back(ParameterList::createEmpty(Context));
    }
  } else {
    diagnose(Tok, diag::func_decl_without_paren);
    Status = makeParserError();

    // Recover by creating a '() -> ?' signature.
    bodyParams.push_back(ParameterList::createEmpty(Context, PreviousLoc,
                                                    PreviousLoc));
    FullName = DeclName(Context, SimpleName, bodyParams.back());
  }
  
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
          .fixItReplace(SourceRange(Tok.getLoc()), "->");
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

ParserStatus
Parser::parseConstructorArguments(DeclName &FullName,
                                  ParameterList *&BodyParams,
                                  DefaultArgumentInfo &DefaultArgs) {
  // If we don't have the leading '(', complain.
  if (!Tok.is(tok::l_paren)) {
    // Complain that we expected '('.
    {
      auto diag = diagnose(Tok, diag::expected_lparen_initializer);
      if (Tok.is(tok::l_brace))
        diag.fixItInsert(Tok.getLoc(), "() ");
    }

    // Create an empty parameter list to recover.
    BodyParams = ParameterList::createEmpty(Context, PreviousLoc, PreviousLoc);
    FullName = DeclName(Context, Context.Id_init, BodyParams);
    return makeParserError();
  }

  // Parse the parameter-clause.
  SmallVector<ParsedParameter, 4> params;
  SourceLoc leftParenLoc, rightParenLoc;
  
  // Parse the parameter clause.
  ParserStatus status 
    = parseParameterClause(leftParenLoc, params, rightParenLoc,
                           &DefaultArgs, ParameterContextKind::Initializer);

  // Turn the parameter clause into argument and body patterns.
  llvm::SmallVector<Identifier, 2> namePieces;
  BodyParams = mapParsedParameters(*this, leftParenLoc, params,
                                   rightParenLoc,
                                   /*isFirstParameterClause=*/true,
                                   &namePieces,
                                   ParameterContextKind::Initializer);

  FullName = DeclName(Context, Context.Id_init, namePieces);
  return status;
}


/// Parse a pattern with an optional type annotation.
///
///  typed-pattern ::= pattern (':' type)?
///
ParserResult<Pattern> Parser::parseTypedPattern() {
  auto result = parsePattern();
  
  // Now parse an optional type annotation.
  if (Tok.is(tok::colon)) {
    SourceLoc pastEndOfPrevLoc = getEndOfPreviousLoc();
    SourceLoc colonLoc = consumeToken(tok::colon);
    SourceLoc startOfNextLoc = Tok.getLoc();
    
    if (result.isNull())  // Recover by creating AnyPattern.
      result = makeParserErrorResult(new (Context) AnyPattern(PreviousLoc));
    
    ParserResult<TypeRepr> Ty = parseType();
    if (Ty.hasCodeCompletion())
      return makeParserCodeCompletionResult<Pattern>();
    if (!Ty.isNull()) {
      // Attempt to diagnose initializer calls incorrectly written
      // as typed patterns, such as "var x: [Int]()".
      if (Tok.isFollowingLParen()) {
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
                                            trailingClosure);
        if (status.isSuccess()) {
          backtrack.cancelBacktrack();
          
          // Suggest replacing ':' with '=' (ensuring proper whitespace).
          
          bool needSpaceBefore = (pastEndOfPrevLoc == colonLoc);
          bool needSpaceAfter =
            SourceMgr.getByteDistance(colonLoc, startOfNextLoc) <= 1;
          
          StringRef replacement = " = ";
          if (!needSpaceBefore) replacement = replacement.drop_front();
          if (!needSpaceAfter)  replacement = replacement.drop_back();
          
          diagnose(lParenLoc, diag::initializer_as_typed_pattern)
            .highlight({Ty.get()->getStartLoc(), rParenLoc})
            .fixItReplace(colonLoc, replacement);
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
    bool isLet = InVarOrLetPattern != IVOLP_InVar;
    return makeParserResult(createBindingFromPattern(loc, name, isLet));
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
                                          bool isLet) {
  VarDecl *var;
  if (ArgumentIsParameter) {
    var = new (Context) ParamDecl(isLet, SourceLoc(), loc, name, loc, name,
                                  Type(), CurDeclContext);
  } else {
    var = new (Context) VarDecl(/*static*/ false, /*IsLet*/ isLet,
                                loc, name, Type(), CurDeclContext);
  }
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
    parseList(tok::r_paren, LPLoc, RPLoc, tok::comma, /*OptionalSep=*/false,
              /*AllowSepAfterLast=*/false,
              diag::expected_rparen_tuple_pattern_list,
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
  if (result.isNull())  // Recover by creating AnyPattern.
    P = new (Context) AnyPattern(Tok.getLoc());
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
    repr = new (Context) OptionalTypeRepr(repr, Tok.getLoc());

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
                                                    nullptr));
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


