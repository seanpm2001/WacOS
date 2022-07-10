//===--- Stmt.cpp - Swift Language Statement ASTs -------------------------===//
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
//  This file implements the Stmt class and subclasses.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/Stmt.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Expr.h"
#include "swift/AST/Pattern.h"
#include "llvm/ADT/PointerUnion.h"

using namespace swift;

//===----------------------------------------------------------------------===//
// Stmt methods.
//===----------------------------------------------------------------------===//

// Only allow allocation of Stmts using the allocator in ASTContext.
void *Stmt::operator new(size_t Bytes, ASTContext &C,
                         unsigned Alignment) {
  return C.Allocate(Bytes, Alignment);
}

StringRef Stmt::getKindName(StmtKind K) {
  switch (K) {
#define STMT(Id, Parent) case StmtKind::Id: return #Id;
#include "swift/AST/StmtNodes.def"
  }
  llvm_unreachable("bad StmtKind");
}

// Helper functions to check statically whether a method has been
// overridden from its implementation in Stmt.  The sort of thing you
// need when you're avoiding v-tables.
namespace {
  template <typename ReturnType, typename Class>
  constexpr bool isOverriddenFromStmt(ReturnType (Class::*)() const) {
    return true;
  }
  template <typename ReturnType>
  constexpr bool isOverriddenFromStmt(ReturnType (Stmt::*)() const) {
    return false;
  }

  template <bool IsOverridden> struct Dispatch;

  /// Dispatch down to a concrete override.
  template <> struct Dispatch<true> {
    template <class T> static SourceLoc getStartLoc(const T *S) {
      return S->getStartLoc();
    }
    template <class T> static SourceLoc getEndLoc(const T *S) {
      return S->getEndLoc();
    }
    template <class T> static SourceRange getSourceRange(const T *S) {
      return S->getSourceRange();
    }
  };

  /// Default implementations for when a method isn't overridden.
  template <> struct Dispatch<false> {
    template <class T> static SourceLoc getStartLoc(const T *S) {
      return S->getSourceRange().Start;
    }
    template <class T> static SourceLoc getEndLoc(const T *S) {
      return S->getSourceRange().End;
    }
    template <class T> static SourceRange getSourceRange(const T *S) {
      return { S->getStartLoc(), S->getEndLoc() };
    }
  };
}

template <class T> static SourceRange getSourceRangeImpl(const T *S) {
  static_assert(isOverriddenFromStmt(&T::getSourceRange) ||
                (isOverriddenFromStmt(&T::getStartLoc) &&
                 isOverriddenFromStmt(&T::getEndLoc)),
                "Stmt subclass must implement either getSourceRange() "
                "or getStartLoc()/getEndLoc()");
  return Dispatch<isOverriddenFromStmt(&T::getSourceRange)>::getSourceRange(S);
}
SourceRange Stmt::getSourceRange() const {
  switch (getKind()) {
#define STMT(ID, PARENT)                                           \
  case StmtKind::ID: return getSourceRangeImpl(cast<ID##Stmt>(this));
#include "swift/AST/StmtNodes.def"
  }
  
  llvm_unreachable("statement type not handled!");
}

template <class T> static SourceLoc getStartLocImpl(const T *S) {
  return Dispatch<isOverriddenFromStmt(&T::getStartLoc)>::getStartLoc(S);
}
SourceLoc Stmt::getStartLoc() const {
  switch (getKind()) {
#define STMT(ID, PARENT)                                           \
  case StmtKind::ID: return getStartLocImpl(cast<ID##Stmt>(this));
#include "swift/AST/StmtNodes.def"
  }

  llvm_unreachable("statement type not handled!");
}

template <class T> static SourceLoc getEndLocImpl(const T *S) {
  return Dispatch<isOverriddenFromStmt(&T::getEndLoc)>::getEndLoc(S);
}
SourceLoc Stmt::getEndLoc() const {
  switch (getKind()) {
#define STMT(ID, PARENT)                                           \
  case StmtKind::ID: return getEndLocImpl(cast<ID##Stmt>(this));
#include "swift/AST/StmtNodes.def"
  }

  llvm_unreachable("statement type not handled!");
}

BraceStmt::BraceStmt(SourceLoc lbloc, ArrayRef<ASTNode> elts,
                     SourceLoc rbloc, Optional<bool> implicit)
  : Stmt(StmtKind::Brace, getDefaultImplicitFlag(implicit, lbloc)),
    NumElements(elts.size()), LBLoc(lbloc), RBLoc(rbloc)
{
  std::uninitialized_copy(elts.begin(), elts.end(),
                          getTrailingObjects<ASTNode>());
}

BraceStmt *BraceStmt::create(ASTContext &ctx, SourceLoc lbloc,
                             ArrayRef<ASTNode> elts, SourceLoc rbloc,
                             Optional<bool> implicit) {
  assert(std::none_of(elts.begin(), elts.end(),
                      [](ASTNode node) -> bool { return node.isNull(); }) &&
         "null element in BraceStmt");
  void *Buffer = ctx.Allocate(totalSizeToAlloc<ASTNode>(elts.size()),
                              alignof(BraceStmt));
  return ::new(Buffer) BraceStmt(lbloc, elts, rbloc, implicit);
}

SourceLoc ReturnStmt::getStartLoc() const {
  if (ReturnLoc.isInvalid() && Result)
    return Result->getStartLoc();
  return ReturnLoc;
}
SourceLoc ReturnStmt::getEndLoc() const {
  if (Result && Result->getEndLoc().isValid())
    return Result->getEndLoc();
  return ReturnLoc;
}

SourceLoc ThrowStmt::getEndLoc() const { return SubExpr->getEndLoc(); }


SourceLoc DeferStmt::getEndLoc() const {
  return tempDecl->getBody()->getEndLoc();
}

/// Dig the original user's body of the defer out for AST fidelity.
BraceStmt *DeferStmt::getBodyAsWritten() const {
  return tempDecl->getBody();
}

bool LabeledStmt::isPossibleContinueTarget() const {
  switch (getKind()) {
#define LABELED_STMT(ID, PARENT)
#define STMT(ID, PARENT) case StmtKind::ID:
#include "swift/AST/StmtNodes.def"
    llvm_unreachable("not a labeled statement");

  // Sema has diagnostics with hard-coded expectations about what
  // statements return false from this method.
  case StmtKind::If:
  case StmtKind::Guard:
  case StmtKind::Switch:
    return false;

  case StmtKind::Do:
  case StmtKind::DoCatch:
  case StmtKind::RepeatWhile:
  case StmtKind::For:
  case StmtKind::ForEach:
  case StmtKind::While:
    return true;
  }
  llvm_unreachable("statement kind unhandled!");
}

bool LabeledStmt::requiresLabelOnJump() const {
  switch (getKind()) {
#define LABELED_STMT(ID, PARENT)
#define STMT(ID, PARENT) case StmtKind::ID:
#include "swift/AST/StmtNodes.def"
    llvm_unreachable("not a labeled statement");

  case StmtKind::If:
  case StmtKind::Do:
  case StmtKind::DoCatch:
  case StmtKind::Guard: // Guard doesn't allow labels, so no break/continue.
    return true;

  case StmtKind::RepeatWhile:
  case StmtKind::For:
  case StmtKind::ForEach:
  case StmtKind::Switch:
  case StmtKind::While:
    return false;
  }
  llvm_unreachable("statement kind unhandled!");
}

void ForEachStmt::setPattern(Pattern *p) {
  Pat = p;
  Pat->markOwnedByStatement(this);
}

void CatchStmt::setErrorPattern(Pattern *pattern) {
  ErrorPattern = pattern;
  ErrorPattern->markOwnedByStatement(this);
}


DoCatchStmt *DoCatchStmt::create(ASTContext &ctx, LabeledStmtInfo labelInfo,
                                 SourceLoc doLoc, Stmt *body,
                                 ArrayRef<CatchStmt*> catches,
                                 Optional<bool> implicit) {
  void *mem = ctx.Allocate(totalSizeToAlloc<CatchStmt*>(catches.size()),
                           alignof(DoCatchStmt));
  return ::new (mem) DoCatchStmt(labelInfo, doLoc, body, catches, implicit);
}

bool DoCatchStmt::isSyntacticallyExhaustive() const {
  for (auto clause : getCatches()) {
    if (clause->isSyntacticallyExhaustive())
      return true;
  }
  return false;
}

void LabeledConditionalStmt::setCond(StmtCondition e) {
  // When set a condition into a Conditional Statement, inform each of the
  // variables bound in any patterns that this is the owning statement for the
  // pattern.
  for (auto &elt : e)
    if (auto pat = elt.getPatternOrNull())
      pat->markOwnedByStatement(this);
  
  Cond = e;
}

bool CatchStmt::isSyntacticallyExhaustive() const {
  // It cannot have a guard expression and the pattern cannot be refutable.
  return getGuardExpr() == nullptr &&
         !getErrorPattern()->isRefutablePattern();
}


PoundAvailableInfo *PoundAvailableInfo::create(ASTContext &ctx,
                                               SourceLoc PoundLoc,
                                       ArrayRef<AvailabilitySpec *> queries,
                                                     SourceLoc RParenLoc) {
  unsigned size = totalSizeToAlloc<AvailabilitySpec *>(queries.size());
  void *Buffer = ctx.Allocate(size, alignof(PoundAvailableInfo));
  return ::new (Buffer) PoundAvailableInfo(PoundLoc, queries, RParenLoc);
}

SourceLoc PoundAvailableInfo::getEndLoc() const {
  if (RParenLoc.isInvalid()) {
    if (NumQueries == 0) {
      return PoundLoc;
    }
    return getQueries()[NumQueries - 1]->getSourceRange().End;
  }
  return RParenLoc;
}

void PoundAvailableInfo::
getPlatformKeywordLocs(SmallVectorImpl<SourceLoc> &PlatformLocs) {
  for (unsigned i = 0; i < NumQueries; i++) {
    auto *VersionSpec =
      dyn_cast<VersionConstraintAvailabilitySpec>(getQueries()[i]);
    if (!VersionSpec)
      continue;
    
    PlatformLocs.push_back(VersionSpec->getPlatformLoc());
  }
}



SourceRange StmtConditionElement::getSourceRange() const {
  switch (getKind()) {
  case StmtConditionElement::CK_Boolean:
    return getBoolean()->getSourceRange();
  case StmtConditionElement::CK_Availability:
    return getAvailability()->getSourceRange();
  case StmtConditionElement::CK_PatternBinding:
    SourceLoc Start;
    if (IntroducerLoc.isValid())
      Start = IntroducerLoc;
    else
      Start = getPattern()->getStartLoc();

    return SourceRange(Start, getInitializer()->getEndLoc());
  }
}

SourceLoc StmtConditionElement::getStartLoc() const {
  switch (getKind()) {
  case StmtConditionElement::CK_Boolean:
    return getBoolean()->getStartLoc();
  case StmtConditionElement::CK_Availability:
    return getAvailability()->getStartLoc();
  case StmtConditionElement::CK_PatternBinding:
    if (IntroducerLoc.isValid())
      return IntroducerLoc;
    return getPattern()->getStartLoc();
  }
}

SourceLoc StmtConditionElement::getEndLoc() const {
  switch (getKind()) {
  case StmtConditionElement::CK_Boolean:
    return getBoolean()->getEndLoc();
  case StmtConditionElement::CK_Availability:
    return getAvailability()->getEndLoc();
  case StmtConditionElement::CK_PatternBinding:
    return getInitializer()->getEndLoc();
  }
}

static StmtCondition exprToCond(Expr *C, ASTContext &Ctx) {
  StmtConditionElement Arr[] = { StmtConditionElement(C) };
  return Ctx.AllocateCopy(Arr);
}

IfStmt::IfStmt(SourceLoc IfLoc, Expr *Cond, Stmt *Then, SourceLoc ElseLoc,
               Stmt *Else, Optional<bool> implicit, ASTContext &Ctx)
  : IfStmt(LabeledStmtInfo(), IfLoc, exprToCond(Cond, Ctx), Then, ElseLoc, Else,
           implicit) {
}

GuardStmt::GuardStmt(SourceLoc GuardLoc, Expr *Cond, Stmt *Body,
                     Optional<bool> implicit, ASTContext &Ctx)
  : GuardStmt(GuardLoc, exprToCond(Cond, Ctx), Body, implicit) {
    
}
  


SourceLoc RepeatWhileStmt::getEndLoc() const { return Cond->getEndLoc(); }

SourceRange CaseLabelItem::getSourceRange() const {
  if (auto *E = getGuardExpr())
    return { CasePattern->getStartLoc(), E->getEndLoc() };
  return CasePattern->getSourceRange();
}
SourceLoc CaseLabelItem::getStartLoc() const {
  return CasePattern->getStartLoc();
}
SourceLoc CaseLabelItem::getEndLoc() const {
  if (auto *E = getGuardExpr())
    return E->getEndLoc();
  return CasePattern->getEndLoc();
}

CaseStmt::CaseStmt(SourceLoc CaseLoc, ArrayRef<CaseLabelItem> CaseLabelItems,
                   bool HasBoundDecls, SourceLoc ColonLoc, Stmt *Body,
                   Optional<bool> Implicit)
    : Stmt(StmtKind::Case, getDefaultImplicitFlag(Implicit, CaseLoc)),
      CaseLoc(CaseLoc), ColonLoc(ColonLoc),
      BodyAndHasBoundDecls(Body, HasBoundDecls),
      NumPatterns(CaseLabelItems.size()) {
  assert(NumPatterns > 0 && "case block must have at least one pattern");
  MutableArrayRef<CaseLabelItem> Items{ getTrailingObjects<CaseLabelItem>(),
                                        NumPatterns };

  for (unsigned i = 0; i < NumPatterns; ++i) {
    new (&Items[i]) CaseLabelItem(CaseLabelItems[i]);
    Items[i].getPattern()->markOwnedByStatement(this);
  }
}

CaseStmt *CaseStmt::create(ASTContext &C, SourceLoc CaseLoc,
                           ArrayRef<CaseLabelItem> CaseLabelItems,
                           bool HasBoundDecls, SourceLoc ColonLoc, Stmt *Body,
                           Optional<bool> Implicit) {
  void *Mem = C.Allocate(totalSizeToAlloc<CaseLabelItem>(CaseLabelItems.size()),
                         alignof(CaseStmt));
  return ::new (Mem) CaseStmt(CaseLoc, CaseLabelItems, HasBoundDecls, ColonLoc,
                              Body, Implicit);
}

SwitchStmt *SwitchStmt::create(LabeledStmtInfo LabelInfo, SourceLoc SwitchLoc,
                               Expr *SubjectExpr,
                               SourceLoc LBraceLoc,
                               ArrayRef<CaseStmt *> Cases,
                               SourceLoc RBraceLoc,
                               ASTContext &C) {
  void *p = C.Allocate(totalSizeToAlloc<CaseStmt *>(Cases.size()),
                       alignof(SwitchStmt));
  SwitchStmt *theSwitch = ::new (p) SwitchStmt(LabelInfo, SwitchLoc,
                                               SubjectExpr, LBraceLoc,
                                               Cases.size(), RBraceLoc);
  std::uninitialized_copy(Cases.begin(), Cases.end(),
                          theSwitch->getTrailingObjects<CaseStmt *>());
  return theSwitch;
}
