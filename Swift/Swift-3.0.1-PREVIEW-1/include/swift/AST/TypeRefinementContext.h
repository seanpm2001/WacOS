//===--- TypeRefinementContext.h - Swift Refinement Context -----*- C++ -*-===//
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
// This file defines the TypeRefinementContext class.  A TypeRefinementContext
// is the semantic construct that refines a type within its lexical scope.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_TYPEREFINEMENTCONTEXT_H
#define SWIFT_TYPEREFINEMENTCONTEXT_H

#include "swift/AST/Identifier.h"
#include "swift/AST/Availability.h"
#include "swift/AST/Stmt.h" // for PoundAvailableInfo
#include "swift/Basic/LLVM.h"
#include "swift/Basic/SourceLoc.h"
#include "swift/Basic/STLExtras.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/ErrorHandling.h"

namespace swift {
  class BraceStmt;
  class Decl;
  class IfStmt;
  class GuardStmt;
  class SourceFile;
  class Stmt;
  class Expr;
  class StmtConditionElement;

/// Represents a lexical context in which types are refined. For now,
/// types are refined solely for API availability checking, based on
/// the operating system versions that the refined context may execute
/// upon.
///
/// These refinement contexts form a lexical tree parallel to the AST but much
/// more sparse: we only introduce refinement contexts when there is something
/// to refine.
class TypeRefinementContext {

public:
  /// Describes the reason a type refinement context was introduced.
  enum class Reason {
    /// The root refinement context.
    Root,

    /// The context was introduced by a declaration (e.g., the body of a
    /// function declaration or the contents of a class declaration).
    Decl,

    /// The context was introduced for the Then branch of an IfStmt.
    IfStmtThenBranch,

    /// The context was introduced for the Else branch of an IfStmt.
    IfStmtElseBranch,

    /// The context was introduced for the remaining StmtConditionElements
    /// following an #available(...) query in a StmtCondition.
    /// For example, in the IfStmt below, the optional binding let x = expr()
    /// would be contained in this kind of context:
    ///
    /// if #available(...),
    ///    let x = expr() {
    ///  }
    ConditionFollowingAvailabilityQuery,

    /// The context was introduced for the fallthrough flow of a guard
    /// statement.
    GuardStmtFallthrough,

    /// The context was introduced for the else flow of a guard
    /// statement.
    GuardStmtElseBranch,

    // The context was introduced for the body of a while statement.
    WhileStmtBody
  };

private:

  /// Represents the AST node that introduced a refinement context.
  class IntroNode {
    Reason IntroReason;
    union {
      SourceFile *SF;
      Decl *D;
      IfStmt *IS;
      PoundAvailableInfo *PAI;
      GuardStmt *GS;
      WhileStmt *WS;
    };

  public:
    IntroNode(SourceFile *SF) : IntroReason(Reason::Root), SF(SF) {}
    IntroNode(Decl *D) : IntroReason(Reason::Decl), D(D) {}
    IntroNode(IfStmt *IS, bool IsThen) :
    IntroReason(IsThen ? Reason::IfStmtThenBranch : Reason::IfStmtElseBranch),
                IS(IS) {}
    IntroNode(PoundAvailableInfo *PAI)
        : IntroReason(Reason::ConditionFollowingAvailabilityQuery), PAI(PAI) {}
    IntroNode(GuardStmt *GS, bool IsFallthrough)
        : IntroReason(IsFallthrough ? Reason::GuardStmtFallthrough
                                    : Reason::GuardStmtElseBranch),
          GS(GS) {}
    IntroNode(WhileStmt *WS) : IntroReason(Reason::WhileStmtBody), WS(WS) {}

    Reason getReason() const { return IntroReason; }

    SourceFile *getAsSourceFile() const {
      assert(IntroReason == Reason::Root);
      return SF;
    }

    Decl *getAsDecl() const {
      assert(IntroReason == Reason::Decl);
      return D;
    }

    IfStmt *getAsIfStmt() const {
      assert(IntroReason == Reason::IfStmtThenBranch ||
             IntroReason == Reason::IfStmtElseBranch);
      return IS;
    }

    PoundAvailableInfo *getAsPoundAvailableInfo() const {
      assert(IntroReason == Reason::ConditionFollowingAvailabilityQuery);
      return PAI;
    }

    GuardStmt *getAsGuardStmt() const {
      assert(IntroReason == Reason::GuardStmtFallthrough ||
             IntroReason == Reason::GuardStmtElseBranch);
      return GS;
    }

    WhileStmt *getAsWhileStmt() const {
      assert(IntroReason == Reason::WhileStmtBody);
      return WS;
    }
  };

  /// The AST node that introduced this context.
  IntroNode Node;

  SourceRange SrcRange;

  AvailabilityContext AvailabilityInfo;

  std::vector<TypeRefinementContext *> Children;

  TypeRefinementContext(ASTContext &Ctx, IntroNode Node,
                        TypeRefinementContext *Parent, SourceRange SrcRange,
                        const AvailabilityContext &Info);

public:
  
  /// Create the root refinement context for the given SourceFile.
  static TypeRefinementContext *createRoot(SourceFile *SF,
                                           const AvailabilityContext &Info);

  /// Create a refinement context for the given declaration.
  static TypeRefinementContext *createForDecl(ASTContext &Ctx, Decl *D,
                                              TypeRefinementContext *Parent,
                                              const AvailabilityContext &Info,
                                              SourceRange SrcRange);
  
  /// Create a refinement context for the Then branch of the given IfStmt.
  static TypeRefinementContext *
  createForIfStmtThen(ASTContext &Ctx, IfStmt *S, TypeRefinementContext *Parent,
                      const AvailabilityContext &Info);

  /// Create a refinement context for the Else branch of the given IfStmt.
  static TypeRefinementContext *
  createForIfStmtElse(ASTContext &Ctx, IfStmt *S, TypeRefinementContext *Parent,
                      const AvailabilityContext &Info);

  /// Create a refinement context for the true-branch control flow to
  /// further StmtConditionElements following a #available() query in
  /// a StmtCondition.
  static TypeRefinementContext *
  createForConditionFollowingQuery(ASTContext &Ctx, PoundAvailableInfo *PAI,
                                   const StmtConditionElement &LastElement,
                                   TypeRefinementContext *Parent,
                                   const AvailabilityContext &Info);

  /// Create a refinement context for the fallthrough of a GuardStmt.
  static TypeRefinementContext *
  createForGuardStmtFallthrough(ASTContext &Ctx, GuardStmt *RS,
                                  BraceStmt *ContainingBraceStmt,
                                  TypeRefinementContext *Parent,
                                  const AvailabilityContext &Info);

  /// Create a refinement context for the else branch of a GuardStmt.
  static TypeRefinementContext *
  createForGuardStmtElse(ASTContext &Ctx, GuardStmt *RS,
                         TypeRefinementContext *Parent,
                         const AvailabilityContext &Info);

  /// Create a refinement context for the body of a WhileStmt.
  static TypeRefinementContext *
  createForWhileStmtBody(ASTContext &Ctx, WhileStmt *WS,
                         TypeRefinementContext *Parent,
                         const AvailabilityContext &Info);

  /// Returns the reason this context was introduced.
  Reason getReason() const;
  
  /// Returns the AST node that introduced this refinement context. Note that
  /// this node may be different than the refined range. For example, a
  /// refinement context covering an IfStmt Then branch will have the
  /// IfStmt as the introduction node (and its reason as IfStmtThenBranch)
  /// but its source range will cover the Then branch.
  IntroNode getIntroductionNode() const { return Node; }
  
  /// Returns the location of the node that introduced this refinement context
  /// or an invalid location if the context reflects the minimum deployment
  // target.
  SourceLoc getIntroductionLoc() const;
  
  /// Returns the source range on which this context refines types.
  SourceRange getSourceRange() const { return SrcRange; }

  /// Returns the information on what can be assumed present at run time when
  /// running code contained in this context.
  const AvailabilityContext &getAvailabilityInfo() const {
    return AvailabilityInfo;
  }

  /// Adds a child refinement context.
  void addChild(TypeRefinementContext *Child) {
    assert(Child->getSourceRange().isValid());
    Children.push_back(Child);
  }

  /// Returns the inner-most TypeRefinementContext descendant of this context
  /// for the given source location.
  TypeRefinementContext *findMostRefinedSubContext(SourceLoc Loc,
                                                   SourceManager &SM);

  LLVM_ATTRIBUTE_DEPRECATED(
      void dump(SourceManager &SrcMgr) const LLVM_ATTRIBUTE_USED,
      "only for use within the debugger");
  void dump(raw_ostream &OS, SourceManager &SrcMgr) const;
  void print(raw_ostream &OS, SourceManager &SrcMgr, unsigned Indent = 0) const;
  
  static StringRef getReasonName(Reason R);
  
  // Only allow allocation of TypeRefinementContext using the allocator in
  // ASTContext.
  void *operator new(size_t Bytes, ASTContext &C,
                     unsigned Alignment = alignof(TypeRefinementContext));
};

} // end namespace swift

#endif
