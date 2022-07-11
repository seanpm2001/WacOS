//===--- Syntax.cpp - Swift Syntax Implementation -------------------------===//
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

#include "swift/Syntax/Syntax.h"
#include "swift/Syntax/SyntaxData.h"
#include "swift/Syntax/SyntaxVisitor.h"

using namespace swift;
using namespace swift::syntax;

const RawSyntax *Syntax::getRaw() const { return Data->getRaw(); }

SyntaxKind Syntax::getKind() const {
  return getRaw()->getKind();
}

void Syntax::print(llvm::raw_ostream &OS, SyntaxPrintOptions Opts) const {
  if (auto Raw = getRaw())
    Raw->print(OS, Opts);
}

void Syntax::dump() const {
  getRaw()->dump();
}

void Syntax::dump(llvm::raw_ostream &OS, unsigned Indent) const {
  getRaw()->dump(OS, 0);
}

bool Syntax::isType() const { return Data->isType(); }

bool Syntax::isDecl() const { return Data->isDecl(); }

bool Syntax::isStmt() const { return Data->isStmt(); }

bool Syntax::isExpr() const { return Data->isExpr(); }

bool Syntax::isToken() const {
  return getRaw()->isToken();
}

bool Syntax::isPattern() const { return Data->isPattern(); }

bool Syntax::isUnknown() const { return Data->isUnknown(); }

bool Syntax::isPresent() const {
  return getRaw()->isPresent();
}

bool Syntax::isMissing() const {
  return getRaw()->isMissing();
}

llvm::Optional<Syntax> Syntax::getParent() const {
  auto ParentData = getData()->getParent();
  if (!ParentData) {
    return None;
  }
  return Syntax(ParentData);
}

size_t Syntax::getNumChildren() const { return Data->getNumChildren(); }

llvm::Optional<Syntax> Syntax::getChild(const size_t N) const {
  auto ChildData = Data->getChild(N);
  if (!ChildData) {
    return None;
  }
  return Syntax(ChildData);
}
