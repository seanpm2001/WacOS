//===--- ASTNode.h - Swift Language ASTs ------------------------*- C++ -*-===//
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
// This file defines the ASTNode, which is a union of Stmt, Expr, and Decl.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_AST_NODE_H
#define SWIFT_AST_AST_NODE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerUnion.h"
#include "swift/Basic/Debug.h"
#include "swift/AST/TypeAlignments.h"

namespace llvm {
  class raw_ostream;
}

namespace swift {
  class Expr;
  class Stmt;
  class Decl;
  class Pattern;
  class TypeRepr;
  class DeclContext;
  class SourceLoc;
  class SourceRange;
  class ASTWalker;
  class StmtConditionElement;
  class CaseLabelItem;
  enum class ExprKind : uint8_t;
  enum class DeclKind : uint8_t;
  enum class PatternKind : uint8_t;
  enum class StmtKind;

  using StmtCondition = llvm::MutableArrayRef<StmtConditionElement>;

  struct ASTNode
      : public llvm::PointerUnion<Expr *, Stmt *, Decl *, Pattern *, TypeRepr *,
                                  StmtCondition *, CaseLabelItem *> {
    // Inherit the constructors from PointerUnion.
    using PointerUnion::PointerUnion;

    SourceRange getSourceRange() const;

    /// Return the location of the start of the statement.
    SourceLoc getStartLoc() const;
  
    /// Return the location of the end of the statement.
    SourceLoc getEndLoc() const;

    void walk(ASTWalker &Walker);
    void walk(ASTWalker &&walker) { walk(walker); }

    /// get the underlying entity as a decl context if it is one,
    /// otherwise, return nullptr;
    DeclContext *getAsDeclContext() const;

    /// Provides some utilities to decide detailed node kind.
#define FUNC(T) bool is##T(T##Kind Kind) const;
    FUNC(Stmt)
    FUNC(Expr)
    FUNC(Decl)
    FUNC(Pattern)
#undef FUNC
    
    SWIFT_DEBUG_DUMP;
    void dump(llvm::raw_ostream &OS, unsigned Indent = 0) const;

    /// Whether the AST node is implicit.
    bool isImplicit() const;
  };
} // namespace swift

namespace llvm {
  using swift::ASTNode;
  template <> struct DenseMapInfo<ASTNode> {
    static inline ASTNode getEmptyKey() {
      return DenseMapInfo<swift::Expr *>::getEmptyKey();
    }
    static inline ASTNode getTombstoneKey() {
      return DenseMapInfo<swift::Expr *>::getTombstoneKey();
    }
    static unsigned getHashValue(const ASTNode Val) {
      return DenseMapInfo<void *>::getHashValue(Val.getOpaqueValue());
    }
    static bool isEqual(const ASTNode LHS, const ASTNode RHS) {
      return LHS.getOpaqueValue() == RHS.getOpaqueValue();
    }
  };
}

#endif // LLVM_SWIFT_AST_AST_NODE_H
