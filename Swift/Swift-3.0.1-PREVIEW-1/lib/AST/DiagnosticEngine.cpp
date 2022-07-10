//===--- DiagnosticEngine.cpp - Diagnostic Display Engine -----------------===//
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
//  This file defines the DiagnosticEngine class, which manages any diagnostics
//  emitted by Swift.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/ASTPrinter.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Module.h"
#include "swift/AST/Pattern.h"
#include "swift/AST/PrintOptions.h"
#include "swift/AST/TypeRepr.h"
#include "swift/Basic/SourceManager.h"
#include "swift/Parse/Lexer.h" // bad dependency
#include "swift/Config.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"

using namespace swift;

namespace {
enum class DiagnosticOptions {
  /// No options.
  none,

  /// The location of this diagnostic points to the beginning of the first
  /// token that the parser considers invalid.  If this token is located at the
  /// beginning of the line, then the location is adjusted to point to the end
  /// of the previous token.
  ///
  /// This behaviour improves experience for "expected token X" diagnostics.
  PointsToFirstBadToken,

  /// After a fatal error subsequent diagnostics are suppressed.
  Fatal,
};
struct StoredDiagnosticInfo {
  DiagnosticKind kind : 2;
  bool pointsToFirstBadToken : 1;
  bool isFatal : 1;

  StoredDiagnosticInfo(DiagnosticKind k, bool firstBadToken, bool fatal)
      : kind(k), pointsToFirstBadToken(firstBadToken), isFatal(fatal) {}
  StoredDiagnosticInfo(DiagnosticKind k, DiagnosticOptions opts)
      : StoredDiagnosticInfo(k,
                             opts == DiagnosticOptions::PointsToFirstBadToken,
                             opts == DiagnosticOptions::Fatal) {}
};

// Reproduce the DiagIDs, as we want both the size and access to the raw ids
// themselves.
enum LocalDiagID : uint32_t {
#define DIAG(KIND, ID, Options, Text, Signature) ID,
#include "swift/AST/DiagnosticsAll.def"
  NumDiags
};
}

// TODO: categorization
static StoredDiagnosticInfo storedDiagnosticInfos[] = {
#define ERROR(ID, Options, Text, Signature)                                    \
  StoredDiagnosticInfo(DiagnosticKind::Error, DiagnosticOptions::Options),
#define WARNING(ID, Options, Text, Signature)                                  \
  StoredDiagnosticInfo(DiagnosticKind::Warning, DiagnosticOptions::Options),
#define NOTE(ID, Options, Text, Signature)                                     \
  StoredDiagnosticInfo(DiagnosticKind::Note, DiagnosticOptions::Options),
#include "swift/AST/DiagnosticsAll.def"
};
static_assert(sizeof(storedDiagnosticInfos) / sizeof(StoredDiagnosticInfo) ==
                  LocalDiagID::NumDiags,
              "array size mismatch");

static const char *diagnosticStrings[] = {
#define ERROR(ID, Options, Text, Signature) Text,
#define WARNING(ID, Options, Text, Signature) Text,
#define NOTE(ID, Options, Text, Signature) Text,
#include "swift/AST/DiagnosticsAll.def"
    "<not a diagnostic>",
};

DiagnosticState::DiagnosticState() {
  // Initialize our per-diagnostic state to default
  perDiagnosticBehavior.resize(LocalDiagID::NumDiags, Behavior::Unspecified);
}

static CharSourceRange toCharSourceRange(SourceManager &SM, SourceRange SR) {
  return CharSourceRange(SM, SR.Start, Lexer::getLocForEndOfToken(SM, SR.End));
}

static CharSourceRange toCharSourceRange(SourceManager &SM, SourceLoc Start,
                                         SourceLoc End) {
  return CharSourceRange(SM, Start, End);
}

InFlightDiagnostic &InFlightDiagnostic::highlight(SourceRange R) {
  assert(IsActive && "Cannot modify an inactive diagnostic");
  if (Engine && R.isValid())
    Engine->getActiveDiagnostic()
        .addRange(toCharSourceRange(Engine->SourceMgr, R));
  return *this;
}

InFlightDiagnostic &InFlightDiagnostic::highlightChars(SourceLoc Start,
                                                       SourceLoc End) {
  assert(IsActive && "Cannot modify an inactive diagnostic");
  if (Engine && Start.isValid())
    Engine->getActiveDiagnostic()
        .addRange(toCharSourceRange(Engine->SourceMgr, Start, End));
  return *this;
}

/// \brief Add an insertion fix-it to the currently-active diagnostic.  The
/// text is inserted immediately *after* the token specified.
///
InFlightDiagnostic &InFlightDiagnostic::fixItInsertAfter(SourceLoc L,
                                                         StringRef Str) {
  L = Lexer::getLocForEndOfToken(Engine->SourceMgr, L);
  return fixItInsert(L, Str);
}

/// \brief Add a token-based removal fix-it to the currently-active
/// diagnostic.
InFlightDiagnostic &InFlightDiagnostic::fixItRemove(SourceRange R) {
  assert(IsActive && "Cannot modify an inactive diagnostic");
  if (R.isInvalid() || !Engine) return *this;

  // Convert from a token range to a CharSourceRange, which points to the end of
  // the token we want to remove.
  auto &SM = Engine->SourceMgr;
  auto charRange = toCharSourceRange(SM, R);

  // If we're removing something (e.g. a keyword), do a bit of extra work to
  // make sure that we leave the code in a good place, without extraneous white
  // space around its hole.  Specifically, check to see there is whitespace
  // before and after the end of range.  If so, nuke the space afterward to keep
  // things consistent.
  if (SM.extractText({charRange.getEnd(), 1}) == " ") {
    // Check before the string, we have to be careful not to go off the front of
    // the buffer.
    auto bufferRange =
      SM.getRangeForBuffer(SM.findBufferContainingLoc(charRange.getStart()));
    bool ShouldRemove = false;
    if (bufferRange.getStart() == charRange.getStart())
      ShouldRemove = true;
    else {
      auto beforeChars =
        SM.extractText({charRange.getStart().getAdvancedLoc(-1), 1});
      ShouldRemove = !beforeChars.empty() && isspace(beforeChars[0]);
    }
    if (ShouldRemove) {
      charRange = CharSourceRange(charRange.getStart(),
                                  charRange.getByteLength()+1);
    }
  }
  Engine->getActiveDiagnostic().addFixIt(Diagnostic::FixIt(charRange, {}));
  return *this;
}


InFlightDiagnostic &InFlightDiagnostic::fixItReplace(SourceRange R,
                                                     StringRef Str) {
  if (Str.empty())
    return fixItRemove(R);

  assert(IsActive && "Cannot modify an inactive diagnostic");
  if (Engine && R.isValid())
    Engine->getActiveDiagnostic().addFixIt(
        Diagnostic::FixIt(toCharSourceRange(Engine->SourceMgr, R), Str));
  return *this;
}

InFlightDiagnostic &InFlightDiagnostic::fixItReplaceChars(SourceLoc Start,
                                                          SourceLoc End,
                                                          StringRef Str) {
  assert(IsActive && "Cannot modify an inactive diagnostic");
  if (Engine && Start.isValid())
    Engine->getActiveDiagnostic().addFixIt(Diagnostic::FixIt(
        toCharSourceRange(Engine->SourceMgr, Start, End), Str));
  return *this;
}

InFlightDiagnostic &InFlightDiagnostic::fixItExchange(SourceRange R1,
                                                      SourceRange R2) {
  assert(IsActive && "Cannot modify an inactive diagnostic");

  auto &SM = Engine->SourceMgr;
  // Convert from a token range to a CharSourceRange
  auto charRange1 = toCharSourceRange(SM, R1);
  auto charRange2 = toCharSourceRange(SM, R2);
  // Extract source text.
  auto text1 = SM.extractText(charRange1);
  auto text2 = SM.extractText(charRange2);

  Engine->getActiveDiagnostic()
    .addFixIt(Diagnostic::FixIt(charRange1, text2));
  Engine->getActiveDiagnostic()
    .addFixIt(Diagnostic::FixIt(charRange2, text1));
  return *this;
}

void InFlightDiagnostic::flush() {
  if (!IsActive)
    return;
  
  IsActive = false;
  if (Engine)
    Engine->flushActiveDiagnostic();
}

bool DiagnosticEngine::isDiagnosticPointsToFirstBadToken(DiagID ID) const {
  return storedDiagnosticInfos[(unsigned) ID].pointsToFirstBadToken;
}

/// \brief Skip forward to one of the given delimiters.
///
/// \param Text The text to search through, which will be updated to point
/// just after the delimiter.
///
/// \param Delim1 The first character delimiter to search for.
///
/// \param Delim2 The second character delimiter to search for.
///
/// \returns The string leading up to the delimiter, or the empty string
/// if no delimiter is found.
static StringRef 
skipToDelimiter(StringRef &Text, char Delim1, char Delim2 = 0) {
  unsigned Depth = 0;

  unsigned I = 0;
  for (unsigned N = Text.size(); I != N; ++I) {
    if (Text[I] == '{') {
      ++Depth;
      continue;
    }
    if (Depth > 0) {
      if (Text[I] == '}')
        --Depth;
      continue;
    }
    
    if (Text[I] == Delim1 || Text[I] == Delim2)
      break;
  }

  assert(Depth == 0 && "Unbalanced {} set in diagnostic text");
  StringRef Result = Text.substr(0, I);
  Text = Text.substr(I + 1);
  return Result;
}

static void formatDiagnosticText(StringRef InText, 
                                 ArrayRef<DiagnosticArgument> Args,
                                 llvm::raw_ostream &Out);

/// Handle the integer 'select' modifier.  This is used like this:
/// %select{foo|bar|baz}2.  This means that the integer argument "%2" has a
/// value from 0-2.  If the value is 0, the diagnostic prints 'foo'.
/// If the value is 1, it prints 'bar'.  If it has the value 2, it prints 'baz'.
/// This is very useful for certain classes of variant diagnostics.
static void formatSelectionArgument(StringRef ModifierArguments,
                                    ArrayRef<DiagnosticArgument> Args,
                                    unsigned SelectedIndex,
                                    llvm::raw_ostream &Out) {
  do {
    StringRef Text = skipToDelimiter(ModifierArguments, '|');
    if (SelectedIndex == 0) {
      formatDiagnosticText(Text, Args, Out);
      break;
    }
    --SelectedIndex;
  } while (true);
  
}

/// \brief Format a single diagnostic argument and write it to the given
/// stream.
static void formatDiagnosticArgument(StringRef Modifier, 
                                     StringRef ModifierArguments,
                                     ArrayRef<DiagnosticArgument> Args,
                                     unsigned ArgIndex,
                                     llvm::raw_ostream &Out) {
  const DiagnosticArgument &Arg = Args[ArgIndex];
  switch (Arg.getKind()) {
  case DiagnosticArgumentKind::Integer:
    if (Modifier == "select") {
      assert(Arg.getAsInteger() >= 0 && "Negative selection index");
      formatSelectionArgument(ModifierArguments, Args, Arg.getAsInteger(), 
                              Out);
    } else if (Modifier == "s") {
      if (Arg.getAsInteger() != 1)
        Out << 's';
    } else {
      assert(Modifier.empty() && "Improper modifier for integer argument");
      Out << Arg.getAsInteger();
    }
    break;

  case DiagnosticArgumentKind::Unsigned:
    if (Modifier == "select") {
      formatSelectionArgument(ModifierArguments, Args, Arg.getAsUnsigned(), 
                              Out);
    } else if (Modifier == "s") {
      if (Arg.getAsUnsigned() != 1)
        Out << 's';
    } else {
      assert(Modifier.empty() && "Improper modifier for unsigned argument");
      Out << Arg.getAsUnsigned();
    }
    break;

  case DiagnosticArgumentKind::String:
    assert(Modifier.empty() && "Improper modifier for string argument");
    Out << Arg.getAsString();
    break;

  case DiagnosticArgumentKind::Identifier:
    assert(Modifier.empty() && "Improper modifier for identifier argument");
    Out << '\'';
    Arg.getAsIdentifier().printPretty(Out);
    Out << '\'';
    break;

  case DiagnosticArgumentKind::ObjCSelector:
    assert(Modifier.empty() && "Improper modifier for selector argument");
    Out << '\'' << Arg.getAsObjCSelector() << '\'';
    break;

  case DiagnosticArgumentKind::Type: {
    assert(Modifier.empty() && "Improper modifier for Type argument");
    
    // Strip extraneous parentheses; they add no value.
    auto type = Arg.getAsType()->getWithoutParens();
    std::string typeName = type->getString();
    Out << '\'' << typeName << '\'';


    // Decide whether to show the desugared type or not.  We filter out some
    // cases to avoid too much noise.
    bool showAKA = !type->isCanonical();

    // Substituted types are uninteresting sugar that prevents the heuristics
    // below from kicking in.
    if (showAKA)
      if (auto *ST = dyn_cast<SubstitutedType>(type.getPointer()))
        type = ST->getReplacementType();

    // If we're complaining about a function type, don't "aka" just because of
    // differences in the argument or result types.
    if (showAKA && type->is<FunctionType>() &&
        isa<FunctionType>(type.getPointer()))
      showAKA = false;

    // Don't unwrap intentional sugar types like T? or [T].
    if (showAKA && (isa<SyntaxSugarType>(type.getPointer()) ||
                    isa<DictionaryType>(type.getPointer()) ||
                    type->is<BuiltinType>()))
      showAKA = false;

    // If they are textually the same, don't show them.  This can happen when
    // they are actually different types, because they exist in different scopes
    // (e.g. everyone names their type parameters 'T').
    if (showAKA && typeName == type->getCanonicalType()->getString())
      showAKA = false;

    // Don't show generic type parameters.
    if (showAKA && type->getCanonicalType()->hasTypeParameter())
      showAKA = false;

    if (showAKA)
      Out << " (aka '" << type->getCanonicalType() << "')";
    break;
  }
  case DiagnosticArgumentKind::TypeRepr:
    assert(Modifier.empty() && "Improper modifier for TypeRepr argument");
    Out << '\'' << Arg.getAsTypeRepr() << '\'';
    break;
  case DiagnosticArgumentKind::PatternKind:
    assert(Modifier.empty() && "Improper modifier for PatternKind argument");
    Out << Arg.getAsPatternKind();
    break;
  case DiagnosticArgumentKind::StaticSpellingKind:
    if (Modifier == "select") {
      formatSelectionArgument(ModifierArguments, Args,
                              unsigned(Arg.getAsStaticSpellingKind()), Out);
    } else {
      assert(Modifier.empty() &&
             "Improper modifier for StaticSpellingKind argument");
      Out << Arg.getAsStaticSpellingKind();
    }
    break;

  case DiagnosticArgumentKind::DescriptiveDeclKind:
    assert(Modifier.empty() &&
           "Improper modifier for DescriptiveDeclKind argument");
    Out << Decl::getDescriptiveKindName(Arg.getAsDescriptiveDeclKind());
    break;

  case DiagnosticArgumentKind::DeclAttribute:
    assert(Modifier.empty() &&
           "Improper modifier for DeclAttribute argument");
    if (Arg.getAsDeclAttribute()->isDeclModifier())
      Out << '\'' << Arg.getAsDeclAttribute()->getAttrName() << '\'';
    else
      Out << '@' << Arg.getAsDeclAttribute()->getAttrName();
    break;

  case DiagnosticArgumentKind::VersionTuple:
    assert(Modifier.empty() &&
           "Improper modifier for VersionTuple argument");
    Out << Arg.getAsVersionTuple().getAsString();
    break;
  }
}

/// \brief Format the given diagnostic text and place the result in the given
/// buffer.
static void formatDiagnosticText(StringRef InText, 
                                 ArrayRef<DiagnosticArgument> Args,
                                 llvm::raw_ostream &Out) {
  while (!InText.empty()) {
    size_t Percent = InText.find('%');
    if (Percent == StringRef::npos) {
      // Write the rest of the string; we're done.
      Out.write(InText.data(), InText.size());
      break;
    }
    
    // Write the string up to (but not including) the %, then drop that text
    // (including the %).
    Out.write(InText.data(), Percent);
    InText = InText.substr(Percent + 1);
    
    // '%%' -> '%'.
    if (InText[0] == '%') {
      Out.write('%');
      InText = InText.substr(1);
      continue;
    }

    // Parse an optional modifier.
    StringRef Modifier;
    {
      unsigned Length = 0;
      while (isalpha(InText[Length]))
        ++Length;
      Modifier = InText.substr(0, Length);
      InText = InText.substr(Length);
    }
    
    // Parse the optional argument list for a modifier, which is brace-enclosed.
    StringRef ModifierArguments;
    if (InText[0] == '{') {
      InText = InText.substr(1);
      ModifierArguments = skipToDelimiter(InText, '}');
    }
    
    // Find the digit sequence.
    unsigned Length = 0;
    for (size_t N = InText.size(); Length != N; ++Length) {
      if (!isdigit(InText[Length]))
        break;
    }
      
    // Parse the digit sequence into an argument index.
    unsigned ArgIndex;      
    bool Result = InText.substr(0, Length).getAsInteger(10, ArgIndex);
    assert(!Result && "Unparseable argument index value?");
    (void)Result;
    assert(ArgIndex < Args.size() && "Out-of-range argument index");
    InText = InText.substr(Length);

    // Convert the argument to a string.
    formatDiagnosticArgument(Modifier, ModifierArguments, Args, ArgIndex, Out);
  }
}

static DiagnosticKind toDiagnosticKind(DiagnosticState::Behavior behavior) {
  switch (behavior) {
  case DiagnosticState::Behavior::Unspecified:
    llvm_unreachable("unspecified behavior");
  case DiagnosticState::Behavior::Ignore:
    llvm_unreachable("trying to map an ignored diagnostic");
  case DiagnosticState::Behavior::Error:
  case DiagnosticState::Behavior::Fatal:
    return DiagnosticKind::Error;
  case DiagnosticState::Behavior::Note:
    return DiagnosticKind::Note;
  case DiagnosticState::Behavior::Warning:
    return DiagnosticKind::Warning;
  }
}

DiagnosticState::Behavior DiagnosticState::determineBehavior(DiagID id) {
  auto set = [this](DiagnosticState::Behavior lvl) {
    if (lvl == Behavior::Fatal) {
      fatalErrorOccurred = true;
      anyErrorOccurred = true;
    } else if (lvl == Behavior::Error) {
      anyErrorOccurred = true;
    }

    previousBehavior = lvl;
    return lvl;
  };

  // We determine how to handle a diagnostic based on the following rules
  //   1) If current state dictates a certain behavior, follow that
  //   2) If the user provided a behavior for this specific diagnostic, follow
  //      that
  //   3) If the user provided a behavior for this diagnostic's kind, follow
  //      that
  //   4) Otherwise remap the diagnostic kind

  auto diagInfo = storedDiagnosticInfos[(unsigned)id];
  bool isNote = diagInfo.kind == DiagnosticKind::Note;

  //   1) If current state dictates a certain behavior, follow that

  // Notes relating to ignored diagnostics should also be ignored
  if (previousBehavior == Behavior::Ignore && isNote)
    return set(Behavior::Ignore);

  // Suppress diagnostics when in a fatal state, except for follow-on notes
  if (fatalErrorOccurred)
    if (!showDiagnosticsAfterFatalError && !isNote)
      return set(Behavior::Ignore);

  //   2) If the user provided a behavior for this specific diagnostic, follow
  //      that

  if (perDiagnosticBehavior[(unsigned)id] != Behavior::Unspecified)
    return set(perDiagnosticBehavior[(unsigned)id]);

  //   3) If the user provided a behavior for this diagnostic's kind, follow
  //      that
  if (diagInfo.kind == DiagnosticKind::Warning) {
    if (suppressWarnings)
      return set(Behavior::Ignore);
    if (warningsAsErrors)
      return set(Behavior::Error);
  }

  //   4) Otherwise remap the diagnostic kind
  switch (diagInfo.kind) {
  case DiagnosticKind::Note:
    return set(Behavior::Note);
  case DiagnosticKind::Error:
    return set(diagInfo.isFatal ? Behavior::Fatal : Behavior::Error);
  case DiagnosticKind::Warning:
    return set(Behavior::Warning);
  }
}

void DiagnosticEngine::flushActiveDiagnostic() {
  assert(ActiveDiagnostic && "No active diagnostic to flush");
  if (TransactionCount == 0) {
    emitDiagnostic(*ActiveDiagnostic);
  } else {
    TentativeDiagnostics.emplace_back(std::move(*ActiveDiagnostic));
  }
  ActiveDiagnostic.reset();
}

void DiagnosticEngine::emitTentativeDiagnostics() {
  for (auto &diag : TentativeDiagnostics) {
    emitDiagnostic(diag);
  }
  TentativeDiagnostics.clear();
}

void DiagnosticEngine::emitDiagnostic(const Diagnostic &diagnostic) {
  auto behavior = state.determineBehavior(diagnostic.getID());
  if (behavior == DiagnosticState::Behavior::Ignore)
    return;

  // Figure out the source location.
  SourceLoc loc = diagnostic.getLoc();
  if (loc.isInvalid() && diagnostic.getDecl()) {
    const Decl *decl = diagnostic.getDecl();
    // If a declaration was provided instead of a location, and that declaration
    // has a location we can point to, use that location.
    loc = decl->getLoc();

    if (loc.isInvalid()) {
      // There is no location we can point to. Pretty-print the declaration
      // so we can point to it.
      SourceLoc ppLoc = PrettyPrintedDeclarations[decl];
      if (ppLoc.isInvalid()) {
        class TrackingPrinter : public StreamPrinter {
          SmallVectorImpl<std::pair<const Decl *, uint64_t>> &Entries;

        public:
          TrackingPrinter(
              SmallVectorImpl<std::pair<const Decl *, uint64_t>> &Entries,
              raw_ostream &OS) :
            StreamPrinter(OS), Entries(Entries) {}

          void printDeclLoc(const Decl *D) override {
            Entries.push_back({ D, OS.tell() });
          }
        };
        SmallVector<std::pair<const Decl *, uint64_t>, 8> entries;
        llvm::SmallString<128> buffer;
        llvm::SmallString<128> bufferName;
        {
          // Figure out which declaration to print. It's the top-most
          // declaration (not a module).
          const Decl *ppDecl = decl;
          auto dc = decl->getDeclContext();

          // FIXME: Horrible, horrible hackaround. We're not getting a
          // DeclContext everywhere we should.
          if (!dc) {
            return;
          }

          while (!dc->isModuleContext()) {
            switch (dc->getContextKind()) {
            case DeclContextKind::Module:
              llvm_unreachable("Not in a module context!");
              break;

            case DeclContextKind::FileUnit:
            case DeclContextKind::TopLevelCodeDecl:
              break;

            case DeclContextKind::ExtensionDecl:
              ppDecl = cast<ExtensionDecl>(dc);
              break;

            case DeclContextKind::GenericTypeDecl:
              ppDecl = cast<GenericTypeDecl>(dc);
              break;

            case DeclContextKind::SerializedLocal:
            case DeclContextKind::Initializer:
            case DeclContextKind::AbstractClosureExpr:
            case DeclContextKind::AbstractFunctionDecl:
            case DeclContextKind::SubscriptDecl:
              break;
            }

            dc = dc->getParent();
          }

          // Build the module name path (in reverse), which we use to
          // build the name of the buffer.
          SmallVector<StringRef, 4> nameComponents;
          while (dc) {
            nameComponents.push_back(cast<Module>(dc)->getName().str());
            dc = dc->getParent();
          }

          for (unsigned i = nameComponents.size(); i; --i) {
            bufferName += nameComponents[i-1];
            bufferName += '.';
          }

          if (auto value = dyn_cast<ValueDecl>(ppDecl)) {
            bufferName += value->getNameStr();
          } else if (auto ext = dyn_cast<ExtensionDecl>(ppDecl)) {
            bufferName += ext->getExtendedType().getString();
          }

          // Pretty-print the declaration we've picked.
          llvm::raw_svector_ostream out(buffer);
          TrackingPrinter printer(entries, out);
          ppDecl->print(printer, PrintOptions::printForDiagnostics());
        }

        // Build a buffer with the pretty-printed declaration.
        auto bufferID = SourceMgr.addMemBufferCopy(buffer, bufferName);
        auto memBufferStartLoc = SourceMgr.getLocForBufferStart(bufferID);

        // Go through all of the pretty-printed entries and record their
        // locations.
        for (auto entry : entries) {
          PrettyPrintedDeclarations[entry.first] =
              memBufferStartLoc.getAdvancedLoc(entry.second);
        }

        // Grab the pretty-printed location.
        ppLoc = PrettyPrintedDeclarations[decl];
      }

      loc = ppLoc;
    }
  }

  // Actually substitute the diagnostic arguments into the diagnostic text.
  llvm::SmallString<256> Text;
  {
    llvm::raw_svector_ostream Out(Text);
    formatDiagnosticText(diagnosticStrings[(unsigned)diagnostic.getID()],
                         diagnostic.getArgs(), Out);
  }

  // Pass the diagnostic off to the consumer.
  DiagnosticInfo Info;
  Info.ID = diagnostic.getID();
  Info.Ranges = diagnostic.getRanges();
  Info.FixIts = diagnostic.getFixIts();
  for (auto &Consumer : Consumers) {
    Consumer->handleDiagnostic(SourceMgr, loc, toDiagnosticKind(behavior), Text,
                               Info);
  }
}

