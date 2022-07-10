//===--- ASTNode.h - Swift Language ASTs ------------------------*- C++ -*-===//
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
// This file defines the ASTNode, which is a union of Stmt, Expr, and Decl.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_AST_NODE_H
#define SWIFT_AST_AST_NODE_H

#include "llvm/ADT/PointerUnion.h"
#include "swift/AST/TypeAlignments.h"

namespace swift {
  class Expr;
  class Stmt;
  class Decl;
  class SourceLoc;
  class SourceRange;
  class ASTWalker;
  
  struct ASTNode : public llvm::PointerUnion3<Expr*, Stmt*, Decl*> {
    // Inherit the constructors from PointerUnion.
    using PointerUnion3::PointerUnion3;
    
    SourceRange getSourceRange() const;

    /// \brief Return the location of the start of the statement.
    SourceLoc getStartLoc() const;
  
    /// \brief Return the location of the end of the statement.
    SourceLoc getEndLoc() const;

    void walk(ASTWalker &Walker);
    void walk(ASTWalker &&walker) { walk(walker); }
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
