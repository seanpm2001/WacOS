//===--- RewriteContext.h - Term rewriting allocation arena -----*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_REWRITECONTEXT_H
#define SWIFT_REWRITECONTEXT_H

#include "swift/AST/ASTContext.h"
#include "swift/AST/Types.h"
#include "swift/Basic/Statistic.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/Allocator.h"
#include "Debug.h"
#include "Histogram.h"
#include "Symbol.h"
#include "Term.h"

namespace swift {

namespace rewriting {

class RequirementMachine;

/// A global object that can be shared by multiple rewrite systems.
///
/// It stores uniqued symbols and terms.
///
/// Out-of-line methods are documented in RewriteContext.cpp.
class RewriteContext final {
  friend class Symbol;
  friend class Term;

  /// Allocator for uniquing symbols and terms.
  llvm::BumpPtrAllocator Allocator;

  /// Folding set for uniquing symbols.
  llvm::FoldingSet<Symbol::Storage> Symbols;

  /// Folding set for uniquing terms.
  llvm::FoldingSet<Term::Storage> Terms;

  /// Cache for transitive closure of inherited protocols.
  llvm::DenseMap<const ProtocolDecl *,
                 llvm::TinyPtrVector<const ProtocolDecl *>> AllInherited;

  /// Cached support of sets of protocols, which is the number of elements in
  /// the transitive closure of the set under protocol inheritance.
  llvm::DenseMap<ArrayRef<const ProtocolDecl *>, unsigned> Support;

  /// Cache for associated type declarations.
  llvm::DenseMap<Symbol, AssociatedTypeDecl *> AssocTypes;

  /// Cache for merged associated type symbols.
  llvm::DenseMap<std::pair<Symbol, Symbol>, Symbol> MergedAssocTypes;

  /// Requirement machines built from generic signatures.
  llvm::DenseMap<GenericSignature, RequirementMachine *> Machines;

  /// Stores information about a vertex in the protocol dependency graph.
  struct ProtocolNode {
    /// The 'index' value for Tarjan's algorithm.
    unsigned Index;

    /// The 'low link' value for Tarjan's algorithm.
    unsigned LowLink : 31;

    /// The 'on stack' flag for Tarjan's algorithm.
    unsigned OnStack : 1;

    /// The connected component index, which keys the 'Components' DenseMap
    /// below.
    unsigned ComponentID;

    ProtocolNode() {
      Index = 0;
      LowLink = 0;
      OnStack = 0;
      ComponentID = 0;
    }
  };

  /// A strongly-connected component in the protocol dependency graph.
  struct ProtocolComponent {
    /// The members of this connected component.
    ArrayRef<const ProtocolDecl *> Protos;

    /// Each connected component has a lazily-created requirement machine.
    RequirementMachine *Machine = nullptr;
  };

  /// The protocol dependency graph.
  llvm::DenseMap<const ProtocolDecl *, ProtocolNode> Protos;

  /// Used by Tarjan's algorithm.
  unsigned NextComponentIndex = 0;

  /// The connected components. Keys are the ComponentID fields of
  /// ProtocolNode.
  llvm::DenseMap<unsigned, ProtocolComponent> Components;

  ASTContext &Context;

  DebugOptions Debug;

  RewriteContext(const RewriteContext &) = delete;
  RewriteContext(RewriteContext &&) = delete;
  RewriteContext &operator=(const RewriteContext &) = delete;
  RewriteContext &operator=(RewriteContext &&) = delete;

  void getRequirementMachineRec(const ProtocolDecl *proto,
                                SmallVectorImpl<const ProtocolDecl *> &stack);

public:
  /// Statistics.
  UnifiedStatsReporter *Stats;

  /// Histograms.
  Histogram SymbolHistogram;
  Histogram TermHistogram;
  Histogram RuleTrieHistogram;
  Histogram RuleTrieRootHistogram;
  Histogram PropertyTrieHistogram;
  Histogram PropertyTrieRootHistogram;

  explicit RewriteContext(ASTContext &ctx);

  DebugOptions getDebugOptions() const { return Debug; }

  const llvm::TinyPtrVector<const ProtocolDecl *> &
  getInheritedProtocols(const ProtocolDecl *proto);

  unsigned getProtocolSupport(const ProtocolDecl *proto);

  unsigned getProtocolSupport(ArrayRef<const ProtocolDecl *> protos);

  int compareProtocols(const ProtocolDecl *lhs,
                       const ProtocolDecl *rhs);

  Term getTermForType(CanType paramType, const ProtocolDecl *proto);

  MutableTerm getMutableTermForType(CanType paramType,
                                    const ProtocolDecl *proto);

  ASTContext &getASTContext() const { return Context; }

  Type getTypeForTerm(Term term,
                      TypeArrayView<GenericTypeParamType> genericParams) const;

  Type getTypeForTerm(const MutableTerm &term,
                      TypeArrayView<GenericTypeParamType> genericParams) const;

  Type getRelativeTypeForTerm(
                      const MutableTerm &term, const MutableTerm &prefix) const;

  MutableTerm getRelativeTermForType(CanType typeWitness,
                                     ArrayRef<Term> substitutions);

  Type getTypeFromSubstitutionSchema(
                      Type schema,
                      ArrayRef<Term> substitutions,
                      TypeArrayView<GenericTypeParamType> genericParams,
                      const MutableTerm &prefix) const;

  AssociatedTypeDecl *getAssociatedTypeForSymbol(Symbol symbol);

  Symbol mergeAssociatedTypes(Symbol lhs, Symbol rhs);

  RequirementMachine *getRequirementMachine(CanGenericSignature sig);
  bool isRecursivelyConstructingRequirementMachine(CanGenericSignature sig);

  RequirementMachine *getRequirementMachine(const ProtocolDecl *proto);

  ~RewriteContext();
};

} // end namespace rewriting

} // end namespace swift

#endif
