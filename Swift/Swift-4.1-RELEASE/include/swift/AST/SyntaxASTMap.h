//===--- SyntaxASTMap.h - Swift Container for Semantic Info -----*- C++ -*-===//
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
// This file declares the interface for SyntaxASTMap, a container mapping Syntax
// nodes to the Semantic AST.
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_SYNTAXASTMAP_H
#define SWIFT_AST_SYNTAXASTMAP_H

#include "swift/AST/ASTNode.h"
#include "swift/Syntax/Syntax.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"

namespace swift {

/// The top-level container and manager for semantic analysis.
///
/// Eventually, this should contain cached semantic information such as
/// resolved symbols and types for Syntax nodes. For now, it only maintains
/// a mapping from lib/AST nodes to lib/Syntax nodes while we integrate
/// the infrastructure into the compiler.
class SyntaxASTMap final {
  llvm::DenseMap<RC<syntax::SyntaxData>, ASTNode> SyntaxMap;
public:

  /// Record a syntax node -> semantic node mapping for later retrieval.
  ///
  /// This is a temporary measure to get a syntax node's Type or resolved
  /// underlying declaration reference after semantic analysis is done.
  void recordSyntaxMapping(RC<syntax::SyntaxData> FromNode,
                           ASTNode ToNode);

  /// Get the semantic node for a piece of syntax. This must have been
  /// previously recorded with a call to `recordSyntaxMapping`.
  llvm::Optional<ASTNode> getNodeForSyntax(syntax::Syntax SyntaxNode) const;

  /// Clear any associations between syntax nodes and semantic nodes.
  void clearSyntaxMap();

  /// Dump the entire syntax node -> semantic node map for debugging purposes.
  void dumpSyntaxMap() const;
};

} // end namespace swift

#endif // SWIFT_AST_SYNTAXASTMAP_H
