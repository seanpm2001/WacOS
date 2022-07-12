//===--- RawSyntax.cpp - Swift Raw Syntax Implementation ------------------===//
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

#include "swift/Basic/ColorUtils.h"
#include "swift/Syntax/RawSyntax.h"
#include "swift/Syntax/TokenSyntax.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>

using llvm::dyn_cast;
using namespace swift::syntax;

namespace {
static bool isTrivialSyntaxKind(SyntaxKind Kind) {
  if (isUnknownKind(Kind))
    return true;
  if (isCollectionKind(Kind))
    return true;
  switch(Kind) {
  case SyntaxKind::SourceFile:
  case SyntaxKind::TopLevelCodeDecl:
  case SyntaxKind::ExpressionStmt:
  case SyntaxKind::DeclarationStmt:
    return true;
  default:
    return false;
  }
}

static void printSyntaxKind(SyntaxKind Kind, llvm::raw_ostream &OS,
                            SyntaxPrintOptions Opts, bool Open) {
  std::unique_ptr<swift::OSColor> Color;
  if (Opts.Visual) {
    Color.reset(new swift::OSColor(OS, llvm::raw_ostream::GREEN));
  }
  OS << "<";
  if (!Open)
    OS << "/";
  dumpSyntaxKind(OS, Kind);
  OS << ">";
}

} // end of anonymous namespace
void RawSyntax::print(llvm::raw_ostream &OS, SyntaxPrintOptions Opts) const {
  if (isMissing())
    return;

  const bool PrintKind = Opts.PrintSyntaxKind && !isToken() &&
    (Opts.PrintTrivialNodeKind || !isTrivialSyntaxKind(Kind));

  if (PrintKind) {
    printSyntaxKind(Kind, OS, Opts, true);
  }

  if (const auto Tok = dyn_cast<RawTokenSyntax>(this)) {
    Tok->print(OS);
  }

  for (const auto &LE : Layout) {
    LE->print(OS, Opts);
  }
  if (PrintKind) {
    printSyntaxKind(Kind, OS, Opts, false);
  }
}

void RawSyntax::dump() const {
  return RawSyntax::dump(llvm::errs(), /*Indent*/ 0);
}

void RawSyntax::dump(llvm::raw_ostream &OS, unsigned Indent) const {
  auto indent = [&](unsigned Amount) {
    for (decltype(Amount) i = 0; i < Amount; ++i) {
      OS << ' ';
    }
  };

  indent(Indent);
  OS << '(';

  dumpSyntaxKind(OS, Kind);

  if (isMissing())
    OS << " [missing] ";

  OS << '\n';
  for (auto LE = Layout.begin(); LE != Layout.end(); ++LE) {
    if (LE != Layout.begin()) {
      OS << '\n';
    }
    switch ((*LE)->Kind) {
    case SyntaxKind::Token:
      llvm::cast<RawTokenSyntax>(*LE)->dump(OS, Indent + 1);
      break;
    default:
      (*LE)->dump(OS, Indent + 1);
      break;
    }
  }
  OS << ')';
}

bool RawSyntax::accumulateAbsolutePosition(
    AbsolutePosition &Pos, const RawSyntax *UpToTargetNode) const {
  auto Found = this == UpToTargetNode;
  for (auto LE : Layout) {
    switch (LE->Kind) {
    case SyntaxKind::Token: {
      auto Tok = llvm::cast<RawTokenSyntax>(LE);
      for (auto Leader : Tok->LeadingTrivia) {
        Leader.accumulateAbsolutePosition(Pos);
      }

      if (Found) {
        return true;
      }

      Pos.addText(Tok->getText());

      for (auto Trailer : Tok->TrailingTrivia) {
        Trailer.accumulateAbsolutePosition(Pos);
      }
      break;
    }
    default:
      if (Found)
        return true;
      LE->accumulateAbsolutePosition(Pos, UpToTargetNode);
      break;
    }
  }
  return false;
}

AbsolutePosition RawSyntax::getAbsolutePosition(RC<RawSyntax> Root) const {
  AbsolutePosition Pos;
  Root->accumulateAbsolutePosition(Pos, this);
  return Pos;
}

void AbsolutePosition::printLineAndColumn(llvm::raw_ostream &OS) const {
  OS << getLine() << ':' << getColumn();
}

void AbsolutePosition::dump(llvm::raw_ostream &OS) const {
  OS << "(absolute_position ";
  OS << "offset=" << getOffset() << " ";
  OS << "line=" << getLine() << " ";
  OS << "column=" << getColumn();
  OS << ')';
}

swift::RC<RawSyntax>
RawSyntax::append(RC<RawSyntax> NewLayoutElement) const {
  auto NewLayout = Layout;
  NewLayout.push_back(NewLayoutElement);
  return RawSyntax::make(Kind, NewLayout, SourcePresence::Present);
}
