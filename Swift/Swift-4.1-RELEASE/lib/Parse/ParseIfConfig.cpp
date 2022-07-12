//===--- ParseIfConfig.cpp - Swift Language Parser for #if directives -----===//
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
// Conditional Compilation Block Parsing and AST Building
//
//===----------------------------------------------------------------------===//

#include "swift/AST/ASTVisitor.h"
#include "swift/Parse/Parser.h"
#include "swift/Basic/Defer.h"
#include "swift/Basic/LangOptions.h"
#include "swift/Basic/Version.h"
#include "swift/Parse/Lexer.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace swift;

namespace {

/// Get PlatformConditionKind from platform condition name.
static
Optional<PlatformConditionKind> getPlatformConditionKind(StringRef Name) {
  return llvm::StringSwitch<llvm::Optional<PlatformConditionKind>>(Name)
    .Case("os", PlatformConditionKind::OS)
    .Case("arch", PlatformConditionKind::Arch)
    .Case("_endian", PlatformConditionKind::Endianness)
    .Case("_runtime", PlatformConditionKind::Runtime)
    .Case("canImport", PlatformConditionKind::CanImport)
    .Case("targetEnvironment", PlatformConditionKind::TargetEnvironment)
    .Default(None);
}

/// Extract source text of the expression.
static StringRef extractExprSource(SourceManager &SM, Expr *E) {
  CharSourceRange Range =
    Lexer::getCharSourceRangeFromSourceRange(SM, E->getSourceRange());
  return SM.extractText(Range);
}


/// The condition validator.
class ValidateIfConfigCondition :
  public ExprVisitor<ValidateIfConfigCondition, Expr*> {
  ASTContext &Ctx;
  DiagnosticEngine &D;

  bool HasError;

  /// Get the identifier string of the UnresolvedDeclRefExpr.
  llvm::Optional<StringRef> getDeclRefStr(Expr *E, DeclRefKind Kind) {
    auto UDRE = dyn_cast<UnresolvedDeclRefExpr>(E);
    if (!UDRE ||
        !UDRE->hasName() ||
        UDRE->getRefKind() != Kind)
      return None;
    if (UDRE->getName().isCompoundName()) {
      if (!Ctx.isSwiftVersion3())
        return None;
      // Swift3 used to accept compound names; warn and return the basename.
      D.diagnose(UDRE->getNameLoc().getLParenLoc(),
                 diag::swift3_conditional_compilation_expression_compound)
        .fixItRemove({ UDRE->getNameLoc().getLParenLoc(),
                       UDRE->getNameLoc().getRParenLoc() });
    }

    return UDRE->getName().getBaseIdentifier().str();
  }

  Expr *diagnoseUnsupportedExpr(Expr *E) {
    D.diagnose(E->getLoc(),
               diag::unsupported_conditional_compilation_expression_type);
    return nullptr;
  }

  // Support '||' and '&&' operator. The procedence of '&&' is higher than '||'.
  // Invalid operator and the next operand are diagnosed and removed from AST.
  Expr *foldSequence(Expr *LHS, ArrayRef<Expr*> &S, bool isRecurse = false) {
    assert(!S.empty() && ((S.size() & 1) == 0));

    auto getNextOperator = [&]() -> llvm::Optional<StringRef> {
      assert((S.size() & 1) == 0);
      while (!S.empty()) {
        auto Name = getDeclRefStr(S[0], DeclRefKind::BinaryOperator);
        if (Name.hasValue() && (*Name == "||" || *Name == "&&"))
          return Name;

        auto DiagID = isa<UnresolvedDeclRefExpr>(S[0])
          ? diag::unsupported_conditional_compilation_binary_expression
          : diag::unsupported_conditional_compilation_expression_type;
        D.diagnose(S[0]->getLoc(), DiagID);
        HasError |= true;
        // Consume invalid operator and the immediate RHS.
        S = S.slice(2);
      }
      return None;
    };

    // Extract out the first operator name.
    auto OpName = getNextOperator();
    if (!OpName.hasValue())
      // If failed, it's not a sequence anymore.
      return LHS;
    Expr *Op = S[0];
  
    // We will definitely be consuming at least one operator.
    // Pull out the prospective RHS and slice off the first two elements.
    Expr *RHS = validate(S[1]);
    S = S.slice(2);

    while (true) {
      // Pull out the next binary operator.
      auto NextOpName = getNextOperator();
      bool IsEnd = !NextOpName.hasValue();
      if (!IsEnd && *OpName == "||" && *NextOpName == "&&") {
        RHS = foldSequence(RHS, S, /*isRecurse*/true);
        continue;
      }

      // Apply the operator with left-associativity by folding the first two
      // operands.
      TupleExpr *Arg = TupleExpr::create(Ctx, SourceLoc(), { LHS, RHS },
                                         { }, { }, SourceLoc(),
                                         /*HasTrailingClosure=*/false,
                                         /*Implicit=*/true);
      LHS = new (Ctx) BinaryExpr(Op, Arg, /*implicit*/false);

      // If we don't have the next operator, we're done.
      if (IsEnd)
        break;
      if (isRecurse && *OpName == "&&" && *NextOpName == "||")
        break;

      OpName = NextOpName;
      Op = S[0];
      RHS = validate(S[1]);
      S = S.slice(2);
    }

    return LHS;
  }

  // In Swift3 mode, leave sequence as a sequence because it has strange
  // evaluation rule. See 'EvaluateIfConfigCondition::visitSequenceExpr'.
  Expr *validateSequence(ArrayRef<Expr *> &S) {
    assert(Ctx.isSwiftVersion3());

    SmallVector<Expr *, 3> Filtered;
    SmallVector<unsigned, 2> AndIdxs;
    Filtered.push_back(validate(S[0]));
    S = S.slice(1);

    while (!S.empty()) {
      auto OpName = getDeclRefStr(S[0], DeclRefKind::BinaryOperator);
      if (!OpName.hasValue() || (*OpName != "||" && *OpName != "&&")) {
        // Warning and ignore in Swift3 mode.
        D.diagnose(
            S[0]->getLoc(),
            diag::swift3_unsupported_conditional_compilation_expression_type)
          .highlight({ S[0]->getLoc(), S[1]->getEndLoc() });
      } else {
        // Remember the start and end of '&&' sequence.
        bool InAnd = (AndIdxs.size() & 1) == 1;
        if ((*OpName == "&&" && !InAnd) || (*OpName == "||" && InAnd))
          AndIdxs.push_back(Filtered.size() - 1);

        Filtered.push_back(S[0]);
        Filtered.push_back(validate(S[1]));
      }
      S = S.slice(2);
    }
    assert((Filtered.size() & 1) == 1);

    // If the last OpName is '&&', close it with a parenthesis, except if the
    // operators are '&&' only.
    if ((1 == (AndIdxs.size() & 1)) && AndIdxs.back() > 0)
      AndIdxs.push_back(Filtered.size() - 1);
    // Emit fix-its to make this sequence compatilble with Swift >=4 even in
    // Swift3 mode.
    if (AndIdxs.size() >= 2) {
      assert((AndIdxs.size() & 1) == 0);
      auto diag = D.diagnose(
          Filtered[AndIdxs[0]]->getStartLoc(),
          diag::swift3_conditional_compilation_expression_precedence);
      for (unsigned i = 0, e = AndIdxs.size(); i < e; i += 2) {
        diag.fixItInsert(Filtered[AndIdxs[i]]->getStartLoc(), "(");
        diag.fixItInsertAfter(Filtered[AndIdxs[i + 1]]->getEndLoc(), ")");
      }
    }

    if (Filtered.size() == 1)
      return Filtered[0];
    return SequenceExpr::create(Ctx, Filtered);
  }

public:
  ValidateIfConfigCondition(ASTContext &Ctx, DiagnosticEngine &D)
    : Ctx(Ctx), D(D), HasError(false) {}

  // Explicit configuration flag.
  Expr *visitUnresolvedDeclRefExpr(UnresolvedDeclRefExpr *E) {
    if (!getDeclRefStr(E, DeclRefKind::Ordinary).hasValue())
      return diagnoseUnsupportedExpr(E);
    return E;
  }

  // 'true' or 'false' constant.
  Expr *visitBooleanLiteralExpr(BooleanLiteralExpr *E) {
    return E;
  }

  // '0' and '1' are warned, but we accept it.
  Expr *visitIntegerLiteralExpr(IntegerLiteralExpr *E) {
    if (E->isNegative() ||
        (E->getDigitsText() != "0" && E->getDigitsText() != "1")) {
      return diagnoseUnsupportedExpr(E);
    }
    // "#if 0" isn't valid, but it is common, so recognize it and handle it
    // with a fixit.
    StringRef replacement = E->getDigitsText() == "0" ? "false" :"true";
    D.diagnose(E->getLoc(), diag::unsupported_conditional_compilation_integer,
               E->getDigitsText(), replacement)
      .fixItReplace(E->getLoc(), replacement);
    return E;
  }

  // Platform conditions.
  Expr *visitCallExpr(CallExpr *E) {
    auto KindName = getDeclRefStr(E->getFn(), DeclRefKind::Ordinary);
    if (!KindName.hasValue()) {
      D.diagnose(E->getLoc(), diag::unsupported_platform_condition_expression);
      return nullptr;
    }

    auto *ArgP = dyn_cast<ParenExpr>(E->getArg());
    if (!ArgP) {
      D.diagnose(E->getLoc(), diag::platform_condition_expected_one_argument);
      return nullptr;
    }
    Expr *Arg = ArgP->getSubExpr();

    // '_compiler_version' '(' string-literal ')'
    if (*KindName == "_compiler_version") {
      auto SLE = dyn_cast<StringLiteralExpr>(Arg);
      if (!SLE) {
        D.diagnose(Arg->getLoc(),
                   diag::unsupported_platform_condition_argument,
                   "string literal");
        return nullptr;
      }

      auto ValStr = SLE->getValue();
      if (ValStr.empty()) {
        D.diagnose(SLE->getLoc(), diag::empty_version_string);
        return nullptr;
      }

      auto Val = version::Version::parseCompilerVersionString(
          SLE->getValue(), SLE->getLoc(), &D);
      if (!Val.hasValue())
        return nullptr;
      return E;
    }

    // 'swift' '(' '>=' float-literal ( '.' integer-literal )* ')'
    if (*KindName == "swift") {
      auto PUE = dyn_cast<PrefixUnaryExpr>(Arg);
      llvm::Optional<StringRef> PrefixName = PUE ?
        getDeclRefStr(PUE->getFn(), DeclRefKind::PrefixOperator) : None;
      if (!PrefixName || *PrefixName != ">=") {
        D.diagnose(Arg->getLoc(),
                   diag::unsupported_platform_condition_argument,
                   "a unary comparison, such as '>=2.2'");
        return nullptr;
      }
      auto versionString = extractExprSource(Ctx.SourceMgr, PUE->getArg());
      auto Val = version::Version::parseVersionString(
          versionString, PUE->getArg()->getStartLoc(), &D);
      if (!Val.hasValue())
        return nullptr;
      return E;
    }

    // ( 'os' | 'arch' | '_endian' | '_runtime' | 'canImport') '(' identifier ')''
    auto Kind = getPlatformConditionKind(*KindName);
    if (!Kind.hasValue()) {
      D.diagnose(E->getLoc(), diag::unsupported_platform_condition_expression);
      return nullptr;
    }

    auto ArgStr = getDeclRefStr(Arg, DeclRefKind::Ordinary);
    if (!ArgStr.hasValue()) {
      D.diagnose(E->getLoc(), diag::unsupported_platform_condition_argument,
                 "identifier");
      return nullptr;
    }

    std::vector<StringRef> suggestions;
    if (!LangOptions::checkPlatformConditionSupported(*Kind, *ArgStr,
                                                      suggestions)) {
      if (Kind == PlatformConditionKind::Runtime) {
        // Error for _runtime()
        D.diagnose(Arg->getLoc(),
                   diag::unsupported_platform_runtime_condition_argument);
        return nullptr;
      }

      // Just a warning for other unsupported arguments.
      StringRef DiagName;
      switch (*Kind) {
      case PlatformConditionKind::OS:
        DiagName = "operating system"; break;
      case PlatformConditionKind::Arch:
        DiagName = "architecture"; break;
      case PlatformConditionKind::Endianness:
        DiagName = "endianness"; break;
      case PlatformConditionKind::CanImport:
        DiagName = "import conditional"; break;
      case PlatformConditionKind::TargetEnvironment:
        DiagName = "target environment"; break;
      case PlatformConditionKind::Runtime:
        llvm_unreachable("handled above");
      }
      auto Loc = Arg->getLoc();
      D.diagnose(Loc, diag::unknown_platform_condition_argument,
                 DiagName, *KindName);
      for (auto suggestion : suggestions)
        D.diagnose(Loc, diag::note_typo_candidate, suggestion)
          .fixItReplace(Arg->getSourceRange(), suggestion);
    }

    return E;
  }

  // Grouped condition. e.g. '(FLAG)'
  Expr *visitParenExpr(ParenExpr *E) {
    E->setSubExpr(validate(E->getSubExpr()));
    return E;
  }

  // Prefix '!'. Other prefix operators are rejected.
  Expr *visitPrefixUnaryExpr(PrefixUnaryExpr *E) {
    auto OpName = getDeclRefStr(E->getFn(), DeclRefKind::PrefixOperator);
    if (!OpName.hasValue() || *OpName != "!") {
      D.diagnose(E->getLoc(),
                 diag::unsupported_conditional_compilation_unary_expression);
      return nullptr;
    }
    E->setArg(validate(E->getArg()));
    return E;
  }

  // Fold sequence expression for non-Swift3 mode.
  Expr *visitSequenceExpr(SequenceExpr *E) {
    ArrayRef<Expr*> Elts = E->getElements();
    Expr *foldedExpr;
    if (Ctx.isSwiftVersion3()) {
      foldedExpr = validateSequence(Elts);
    } else {
      auto LHS = validate(Elts[0]);
      Elts = Elts.slice(1);
      foldedExpr = foldSequence(LHS, Elts);
    }
    assert(Elts.empty());
    return foldedExpr;
  }

  // Other expression types are unsupported.
  Expr *visitExpr(Expr *E) {
    return diagnoseUnsupportedExpr(E);
  }

  Expr *validate(Expr *E) {
    if (auto E2 = visit(E))
      return E2;
    HasError |= true;
    return E;
  }

  bool hasError() const {
    return HasError;
  }
};

/// Validate and modify the condition expression.
/// Returns \c true if the condition contains any error.
static bool validateIfConfigCondition(Expr *&condition,
                                      ASTContext &Context,
                                      DiagnosticEngine &D) {
  ValidateIfConfigCondition Validator(Context, D);
  condition = Validator.validate(condition);
  return Validator.hasError();
}

/// The condition evaluator.
/// The condition must be validated with validateIfConfigCondition().
class EvaluateIfConfigCondition :
  public ExprVisitor<EvaluateIfConfigCondition, bool> {
  ASTContext &Ctx;

  /// Get the identifier string from an \c Expr assuming it's an
  /// \c UnresolvedDeclRefExpr.
  StringRef getDeclRefStr(Expr *E) {
    return cast<UnresolvedDeclRefExpr>(E)->getName().getBaseIdentifier().str();
  }

public:
  EvaluateIfConfigCondition(ASTContext &Ctx) : Ctx(Ctx) {}

  bool visitBooleanLiteralExpr(BooleanLiteralExpr *E) {
    return E->getValue();
  }

  bool visitIntegerLiteralExpr(IntegerLiteralExpr *E) {
    return E->getDigitsText() != "0";
  }

  bool visitUnresolvedDeclRefExpr(UnresolvedDeclRefExpr *E) {
    auto Name = getDeclRefStr(E);
    return Ctx.LangOpts.isCustomConditionalCompilationFlagSet(Name);
  }

  bool visitCallExpr(CallExpr *E) {
    auto KindName = getDeclRefStr(E->getFn());
    auto *Arg = cast<ParenExpr>(E->getArg())->getSubExpr();

    if (KindName == "_compiler_version") {
      auto Str = cast<StringLiteralExpr>(Arg)->getValue();
      auto Val = version::Version::parseCompilerVersionString(
          Str, SourceLoc(), nullptr).getValue();
      auto thisVersion = version::Version::getCurrentCompilerVersion();
      return thisVersion >= Val;
    } else if (KindName == "swift") {
      auto PUE = cast<PrefixUnaryExpr>(Arg);
      auto Str = extractExprSource(Ctx.SourceMgr, PUE->getArg());
      auto Val = version::Version::parseVersionString(
          Str, SourceLoc(), nullptr).getValue();
      auto thisVersion = Ctx.LangOpts.EffectiveLanguageVersion;
      return thisVersion >= Val;
    } else if (KindName == "canImport") {
      auto Str = extractExprSource(Ctx.SourceMgr, Arg);
      return Ctx.canImportModule({ Ctx.getIdentifier(Str) , E->getLoc()  });
    }

    auto Val = getDeclRefStr(Arg);
    auto Kind = getPlatformConditionKind(KindName).getValue();
    return Ctx.LangOpts.checkPlatformCondition(Kind, Val);
  }

  bool visitPrefixUnaryExpr(PrefixUnaryExpr *E) {
    return !visit(E->getArg());
  }

  bool visitParenExpr(ParenExpr *E) {
    return visit(E->getSubExpr());
  }

  bool visitBinaryExpr(BinaryExpr *E) {
    assert(!Ctx.isSwiftVersion3() && "BinaryExpr in Swift3 mode");
    auto OpName = getDeclRefStr(E->getFn());
    auto Args = E->getArg()->getElements();
    if (OpName == "||") return visit(Args[0]) || visit(Args[1]);
    if (OpName == "&&") return visit(Args[0]) && visit(Args[1]);
    llvm_unreachable("unsupported binary operator");
  }

  bool visitSequenceExpr(SequenceExpr *E) {
    assert(Ctx.isSwiftVersion3() && "SequenceExpr in non-Swift3 mode");
    ArrayRef<Expr *> Elems = E->getElements();
    auto Result = visit(Elems[0]);
    Elems = Elems.slice(1);
    while (!Elems.empty()) {
      auto OpName = getDeclRefStr(Elems[0]);

      if (OpName == "||") {
        Result = Result || visit(Elems[1]);
        if (Result)
          // Note that this is the Swift3 behavior.
          // e.g. 'false || true && false' evaluates to 'true'.
          return true;
      } else if (OpName == "&&") {
        Result = Result && visit(Elems[1]);
        if (!Result)
          // Ditto.
          // e.g. 'false && true || true' evaluates to 'false'.
          return false;
      } else {
        llvm_unreachable("must be removed in validation phase");
      }
      Elems = Elems.slice(2);
    }
    return Result;
  }

  bool visitExpr(Expr *E) { llvm_unreachable("Unvalidated condition?"); }
};

/// Evaluate the condition.
/// \c true if success, \c false if failed.
static bool evaluateIfConfigCondition(Expr *Condition, ASTContext &Context) {
  return EvaluateIfConfigCondition(Context).visit(Condition);
}

/// Version condition checker.
class IsVersionIfConfigCondition :
  public ExprVisitor<IsVersionIfConfigCondition, bool> {

  /// Get the identifier string from an \c Expr assuming it's an
  /// \c UnresolvedDeclRefExpr.
  StringRef getDeclRefStr(Expr *E) {
    return cast<UnresolvedDeclRefExpr>(E)->getName().getBaseIdentifier().str();
  }

public:
  IsVersionIfConfigCondition() {}

  bool visitBinaryExpr(BinaryExpr *E) {
    auto OpName = getDeclRefStr(E->getFn());
    auto Args = E->getArg()->getElements();
    if (OpName == "||") return visit(Args[0]) && visit(Args[1]);
    if (OpName == "&&") return visit(Args[0]) || visit(Args[1]);
    llvm_unreachable("unsupported binary operator");
  }

  bool visitCallExpr(CallExpr *E) {
    auto KindName = getDeclRefStr(E->getFn());
    return KindName == "_compiler_version" || KindName == "swift";
  }

  bool visitPrefixUnaryExpr(PrefixUnaryExpr *E) { return visit(E->getArg()); }
  bool visitParenExpr(ParenExpr *E) { return visit(E->getSubExpr()); }
  bool visitExpr(Expr *E) { return false; }
};

/// Returns \c true if the condition is a version check.
static bool isVersionIfConfigCondition(Expr *Condition) {
  return IsVersionIfConfigCondition().visit(Condition);
}

/// Get the identifier string from an \c Expr if it's an
/// \c UnresolvedDeclRefExpr, otherwise the empty string.
static StringRef getDeclRefStr(Expr *E) {
  if (auto *UDRE = dyn_cast<UnresolvedDeclRefExpr>(E)) {
    return UDRE->getName().getBaseIdentifier().str();
  }
  return "";
}

static bool isPlatformConditionDisjunction(Expr *E, PlatformConditionKind Kind,
                                           ArrayRef<StringRef> Vals) {
  if (auto *Or = dyn_cast<BinaryExpr>(E)) {
    if (getDeclRefStr(Or->getFn()) == "||") {
      auto Args = Or->getArg()->getElements();
      return (isPlatformConditionDisjunction(Args[0], Kind, Vals) &&
              isPlatformConditionDisjunction(Args[1], Kind, Vals));
    }
  } else if (auto *P = dyn_cast<ParenExpr>(E)) {
    return isPlatformConditionDisjunction(P->getSubExpr(), Kind, Vals);
  } else if (auto *C = dyn_cast<CallExpr>(E)) {
    if (getPlatformConditionKind(getDeclRefStr(C->getFn())) != Kind)
      return false;
    if (auto *ArgP = dyn_cast<ParenExpr>(C->getArg())) {
      if (auto *Arg = ArgP->getSubExpr()) {
        auto ArgStr = getDeclRefStr(Arg);
        for (auto V : Vals) {
          if (ArgStr == V)
            return true;
        }
      }
    }
  }
  return false;
}

// Search for the first occurrence of a _likely_ (but not definite) implicit
// simulator-environment platform condition, or negation thereof. This is
// defined as any logical conjunction of one or more os() platform conditions
// _strictly_ from the set {iOS, tvOS, watchOS} and one or more arch() platform
// conditions _strictly_ from the set {i386, x86_64}.
//
// These are (at the time of writing) defined as de-facto simulators in
// Platform.cpp, and if a user is testing them they're _likely_ looking for
// simulator-ness indirectly. If there is anything else in the condition aside
// from these conditions (or the negation of such a conjunction), we
// conservatively assume the user is testing something other than
// simulator-ness.
static Expr *findAnyLikelySimulatorEnvironmentTest(Expr *Condition) {

  if (!Condition)
    return nullptr;

  if (auto *N = dyn_cast<PrefixUnaryExpr>(Condition)) {
    return findAnyLikelySimulatorEnvironmentTest(N->getArg());
  } else if (auto *P = dyn_cast<ParenExpr>(Condition)) {
    return findAnyLikelySimulatorEnvironmentTest(P->getSubExpr());
  }

  // We assume the user is writing the condition in CNF -- say (os(iOS) ||
  // os(tvOS)) && (arch(i386) || arch(x86_64)) -- rather than DNF, as the former
  // is exponentially more terse, and these conditions are already quite
  // unwieldy. If field evidence shows people using other variants, possibly add
  // them here.

  auto isSimulatorPlatformOSTest = [](Expr *E) -> bool {
    return isPlatformConditionDisjunction(
      E, PlatformConditionKind::OS, {"iOS", "tvOS", "watchOS"});
  };

  auto isSimulatorPlatformArchTest = [](Expr *E) -> bool {
    return isPlatformConditionDisjunction(
      E, PlatformConditionKind::Arch, {"i386", "x86_64"});
  };

  if (auto *And = dyn_cast<BinaryExpr>(Condition)) {
    if (getDeclRefStr(And->getFn()) == "&&") {
      auto Args = And->getArg()->getElements();
      if ((isSimulatorPlatformOSTest(Args[0]) &&
           isSimulatorPlatformArchTest(Args[1])) ||
          (isSimulatorPlatformOSTest(Args[1]) &&
           isSimulatorPlatformArchTest(Args[0]))) {
        return And;
      }
    }
  }
  return nullptr;
}

} // end anonymous namespace


/// Parse and populate a #if ... #endif directive.
/// Delegate callback function to parse elements in the blocks.
ParserResult<IfConfigDecl> Parser::parseIfConfig(
    llvm::function_ref<void(SmallVectorImpl<ASTNode> &, bool)> parseElements) {

  SmallVector<IfConfigClause, 4> Clauses;
  Parser::StructureMarkerRAII ParsingDecl(
      *this, Tok.getLoc(), Parser::StructureMarkerKind::IfConfig);

  bool foundActive = false;
  bool isVersionCondition = false;
  while (1) {
    bool isElse = Tok.is(tok::pound_else);
    SourceLoc ClauseLoc = consumeToken();
    Expr *Condition = nullptr;
    bool isActive = false;

    // Parse the condition.  Evaluate it to determine the active
    // clause unless we're doing a parse-only pass.
    if (isElse) {
      isActive = !foundActive && State->PerformConditionEvaluation;
    } else {
      llvm::SaveAndRestore<bool> S(InPoundIfEnvironment, true);
      ParserResult<Expr> Result = parseExprSequence(diag::expected_expr,
                                                      /*isBasic*/true,
                                                      /*isForDirective*/true);
      if (Result.isNull())
        return makeParserError();
      Condition = Result.get();
      if (validateIfConfigCondition(Condition, Context, Diags)) {
        // Error in the condition;
        isActive = false;
        isVersionCondition = false;
      } else if (!foundActive && State->PerformConditionEvaluation) {
        // Evaluate the condition only if we haven't found any active one and
        // we're not in parse-only mode.
        isActive = evaluateIfConfigCondition(Condition, Context);
        isVersionCondition = isVersionIfConfigCondition(Condition);
      }
    }

    foundActive |= isActive;

    if (!Tok.isAtStartOfLine() && Tok.isNot(tok::eof)) {
      diagnose(Tok.getLoc(),
               diag::extra_tokens_conditional_compilation_directive);
    }

    if (Expr *Test = findAnyLikelySimulatorEnvironmentTest(Condition)) {
      diagnose(Test->getLoc(),
               diag::likely_simulator_platform_condition)
        .fixItReplace(Test->getSourceRange(),
                      "targetEnvironment(simulator)");
    }

    // Parse elements
    SmallVector<ASTNode, 16> Elements;
    if (isActive || !isVersionCondition) {
      parseElements(Elements, isActive);
    } else {
      DiagnosticTransaction DT(Diags);
      skipUntilConditionalBlockClose();
      DT.abort();
    }

    Clauses.emplace_back(ClauseLoc, Condition,
                         Context.AllocateCopy(Elements), isActive);

    if (Tok.isNot(tok::pound_elseif, tok::pound_else))
      break;

    if (isElse)
      diagnose(Tok, diag::expected_close_after_else_directive);
  }

  SourceLoc EndLoc;
  bool HadMissingEnd = parseEndIfDirective(EndLoc);

  auto *ICD = new (Context) IfConfigDecl(CurDeclContext,
                                         Context.AllocateCopy(Clauses),
                                         EndLoc, HadMissingEnd);
  return makeParserResult(ICD);
}
