//===--- ASTNode.cpp - Swift Language ASTs --------------------------------===//
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
// This file implements the ASTNode, which is a union of Stmt, Expr, and Decl.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/ASTNode.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Expr.h"
#include "swift/AST/Stmt.h"
#include "swift/AST/Pattern.h"
#include "swift/AST/TypeRepr.h"
#include "swift/Basic/SourceLoc.h"

using namespace swift;

SourceRange ASTNode::getSourceRange() const {
  if (const auto *E = this->dyn_cast<Expr*>())
    return E->getSourceRange();
  if (const auto *S = this->dyn_cast<Stmt*>())
    return S->getSourceRange();
  if (const auto *D = this->dyn_cast<Decl*>())
    return D->getSourceRange();
  if (const auto *P = this->dyn_cast<Pattern*>())
    return P->getSourceRange();
  if (const auto *T = this->dyn_cast<TypeRepr *>())
    return T->getSourceRange();
  if (const auto *C = this->dyn_cast<StmtCondition *>()) {
    if (C->empty())
      return SourceRange();

    auto first = C->front();
    auto last = C->back();

    return {first.getStartLoc(), last.getEndLoc()};
  }
  if (const auto *I = this->dyn_cast<CaseLabelItem *>()) {
    return I->getSourceRange();
  }
  llvm_unreachable("unsupported AST node");
}

/// Return the location of the start of the statement.
SourceLoc ASTNode::getStartLoc() const {
  return getSourceRange().Start;
}

/// Return the location of the end of the statement.
SourceLoc ASTNode::getEndLoc() const {
  return getSourceRange().End;
}

DeclContext *ASTNode::getAsDeclContext() const {
  if (auto *E = this->dyn_cast<Expr*>()) {
    if (isa<AbstractClosureExpr>(E))
      return static_cast<AbstractClosureExpr*>(E);
  } else if (is<Stmt*>()) {
    return nullptr;
  } else if (auto *D = this->dyn_cast<Decl*>()) {
    if (isa<DeclContext>(D))
      return cast<DeclContext>(D);
  } else if (getOpaqueValue())
    llvm_unreachable("unsupported AST node");
  return nullptr;
}

bool ASTNode::isImplicit() const {
  if (const auto *E = this->dyn_cast<Expr*>())
    return E->isImplicit();
  if (const auto *S = this->dyn_cast<Stmt*>())
    return S->isImplicit();
  if (const auto *D = this->dyn_cast<Decl*>())
    return D->isImplicit();
  if (const auto *P = this->dyn_cast<Pattern*>())
    return P->isImplicit();
  if (const auto *T = this->dyn_cast<TypeRepr*>())
    return false;
  if (const auto *C = this->dyn_cast<StmtCondition *>())
    return false;
  if (const auto *I = this->dyn_cast<CaseLabelItem *>())
    return false;
  llvm_unreachable("unsupported AST node");
}

void ASTNode::walk(ASTWalker &Walker) {
  if (auto *E = this->dyn_cast<Expr*>())
    E->walk(Walker);
  else if (auto *S = this->dyn_cast<Stmt*>())
    S->walk(Walker);
  else if (auto *D = this->dyn_cast<Decl*>())
    D->walk(Walker);
  else if (auto *P = this->dyn_cast<Pattern*>())
    P->walk(Walker);
  else if (auto *T = this->dyn_cast<TypeRepr*>())
    T->walk(Walker);
  else if (auto *C = this->dyn_cast<StmtCondition *>()) {
    for (auto &elt : *C)
      elt.walk(Walker);
  } else if (auto *I = this->dyn_cast<CaseLabelItem *>()) {
    if (auto *P = I->getPattern())
      P->walk(Walker);

    if (auto *G = I->getGuardExpr())
      G->walk(Walker);
  } else
    llvm_unreachable("unsupported AST node");
}

void ASTNode::dump(raw_ostream &OS, unsigned Indent) const {
  if (auto S = dyn_cast<Stmt*>())
    S->dump(OS, /*context=*/nullptr, Indent);
  else if (auto E = dyn_cast<Expr*>())
    E->dump(OS, Indent);
  else if (auto D = dyn_cast<Decl*>())
    D->dump(OS, Indent);
  else if (auto P = dyn_cast<Pattern*>())
    P->dump(OS, Indent);
  else if (auto T = dyn_cast<TypeRepr*>())
    T->print(OS);
  else if (auto C = dyn_cast<StmtCondition *>()) {
    OS.indent(Indent) << "(statement conditions)";
  } else if (auto *I = dyn_cast<CaseLabelItem *>()) {
    OS.indent(Indent) << "(case label item)";
  } else
    llvm_unreachable("unsupported AST node");
}

void ASTNode::dump() const {
  dump(llvm::errs());
}

#define FUNC(T)                                                               \
bool ASTNode::is##T(T##Kind Kind) const {                                     \
  if (!is<T*>())                                                              \
    return false;                                                             \
  return get<T*>()->getKind() == Kind;                                        \
}
FUNC(Stmt)
FUNC(Expr)
FUNC(Decl)
FUNC(Pattern)
#undef FUNC
