//===--- KnuthBendix.cpp - Confluent completion procedure -----------------===//
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
//
// This implements completion in the rewriting system sense, not code
// completion.
//
// We use a variation of the Knuth-Bendix algorithm 
// (https://en.wikipedia.org/wiki/Knuth–Bendix_completion_algorithm).
//
// The goal is to find 'overlapping' rules which would allow the same term to
// be rewritten in two different ways. These two different irreducible
// reductions are called a 'critical pair'; the completion procedure introduces
// new rewrite rules to eliminate critical pairs by rewriting one side of the
// pair to the other. This can introduce more overlaps with existing rules, and
// the process iterates until fixed point.
//
// This implementation also extends Knuth-Bendix to introduce new _generators_,
// in addition to new relations as in the standard algorithm. See the comment at
// the top of RewriteSystem::processMergedAssociatedTypes() for a description.
//
//===----------------------------------------------------------------------===//

#include "swift/Basic/Range.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <deque>
#include <vector>

#include "RewriteContext.h"
#include "RewriteSystem.h"

using namespace swift;
using namespace rewriting;

/// For a superclass or concrete type symbol
///
///   [concrete: Foo<X1, ..., Xn>]
///   [superclass: Foo<X1, ..., Xn>]
///
/// Return a new symbol where the prefix T is prepended to each of the
/// substitutions:
///
///   [concrete: Foo<T.X1, ..., T.Xn>]
///   [superclass: Foo<T.X1, ..., T.Xn>]
///
/// Asserts if this is not a superclass or concrete type symbol.
Symbol Symbol::prependPrefixToConcreteSubstitutions(
    const MutableTerm &prefix,
    RewriteContext &ctx) const {
  if (prefix.empty())
    return *this;

  return transformConcreteSubstitutions(
    [&](Term term) -> Term {
      MutableTerm mutTerm;
      mutTerm.append(prefix);
      mutTerm.append(term);

      return Term::get(mutTerm, ctx);
    }, ctx);
}

/// If we have two symbols [P:T] and [Q:T], produce a merged symbol:
///
/// - If P inherits from Q, this is just [P:T].
/// - If Q inherits from P, this is just [Q:T].
/// - If P and Q are unrelated, this is [P&Q:T].
Symbol RewriteContext::mergeAssociatedTypes(Symbol lhs, Symbol rhs) {
  auto key = std::make_pair(lhs, rhs);

  auto found = MergedAssocTypes.find(key);
  if (found != MergedAssocTypes.end())
    return found->second;

  // Check preconditions that were established by RewriteSystem::addRule().
  assert(lhs.getKind() == Symbol::Kind::AssociatedType);
  assert(rhs.getKind() == Symbol::Kind::AssociatedType);
  assert(lhs.getName() == rhs.getName());
  assert(lhs.compare(rhs, *this) > 0);

  auto protos = lhs.getProtocols();
  auto otherProtos = rhs.getProtocols();

  // This must follow from lhs > rhs.
  assert(getProtocolSupport(protos) <= getProtocolSupport(otherProtos));

  // Compute sorted and merged list of protocols, with duplicates.
  llvm::TinyPtrVector<const ProtocolDecl *> newProtos;
  std::merge(protos.begin(), protos.end(),
             otherProtos.begin(), otherProtos.end(),
             std::back_inserter(newProtos),
             [&](const ProtocolDecl *lhs,
                 const ProtocolDecl *rhs) -> int {
               return compareProtocols(lhs, rhs) < 0;
             });

  // Prune duplicates and protocols that are inherited by other
  // protocols.
  llvm::TinyPtrVector<const ProtocolDecl *> minimalProtos;
  for (const auto *newProto : newProtos) {
    auto inheritsFrom = [&](const ProtocolDecl *thisProto) {
      if (thisProto == newProto)
        return true;

      const auto &inherited = getInheritedProtocols(thisProto);
      return std::find(inherited.begin(),
                       inherited.end(),
                       newProto) != inherited.end();
    };

    if (std::find_if(minimalProtos.begin(), minimalProtos.end(),
                     inheritsFrom)
        == minimalProtos.end()) {
      minimalProtos.push_back(newProto);
    }
  }

  // The two input sets are minimal already, so the merged set
  // should have at least as many elements as the smallest
  // input set.
  assert(minimalProtos.size() >= std::min(protos.size(), otherProtos.size()));

  // The merged set cannot contain more elements than the union
  // of the two sets.
  assert(minimalProtos.size() <= protos.size() + otherProtos.size());

  auto result = Symbol::forAssociatedType(minimalProtos, lhs.getName(), *this);
  auto inserted = MergedAssocTypes.insert(std::make_pair(key, result));
  assert(inserted.second);
  (void) inserted;

  return result;
}

/// Consider the following example:
///
///   protocol P1 { associatedtype T : P1 }
///   protocol P2 { associatedtype T : P2 }
///   struct G<T : P1 & P2> {}
///
/// We start with these rewrite rules:
///
///   [P1].T => [P1:T]
///   [P2].T => [P2:T]
///   [P1:T].[P1] => [P1:T]
///   [P2:T].[P1] => [P2:T]
///   τ_0_0.[P1] => τ_0_0
///   τ_0_0.[P2] => τ_0_0
///   τ_0_0.T => τ_0_0.[P1:T]
///   τ_0_0.[P2:T] => τ_0_0.[P1:T]
///
/// The completion procedure ends up adding an infinite series of rules of the
/// form
///
///   τ_0_0.[P1:T].[P2]                 => τ_0_0.[P1:T]
///   τ_0_0.[P1:T].[P2:T]               => τ_0_0.[P1:T].[P1:T]
///
///   τ_0_0.[P1:T].[P1:T].[P2]          => τ_0_0.[P1:T].[P1:T]
///   τ_0_0.[P1:T].[P1:T].[P2:T]        => τ_0_0.[P1:T].[P1:T].[P1:T]
///
///   τ_0_0.[P1:T].[P1:T].[P1:T].[P2]   => τ_0_0.[P1:T].[P1:T].[P1.T]
///   τ_0_0.[P1:T].[P1:T].[P1:T].[P2:T] => τ_0_0.[P1:T].[P1:T].[P1:T].[P1.T]
///
/// The difficulty here stems from the fact that an arbitrary sequence of
/// [P1:T] following a τ_0_0 is known to conform to P2, but P1:T itself
/// does not conform to P2.
///
/// We use a heuristic to compute a completion in this case by using
/// merged associated type terms.
///
/// The key is the following rewrite rule:
///
///   τ_0_0.[P2:T] => τ_0_0.[P1:T]
///
/// When we add this rule, we introduce a new merged symbol [P1&P2:T] and
/// a new rule:
///
///   τ_0_0.[P1:T] => τ_0_0.[P1&P2:T]
///
/// We also look for any existing rules of the form [P1:T].[Q] => [P1:T]
/// or [P2:T].[Q] => [P2:T], and introduce a new rule:
///
///   [P1&P2:T].[Q] => [P1&P2:T]
///
/// In the above example, we have such a rule for Q == P1 and Q == P2, so
/// in total we end up adding the following four rules:
///
///   τ_0_0.[P1:T] => τ_0_0.[P1&P2:T]
///   [P1&P2:T].[P1] => [P1&P2:T]
///   [P1&P2:T].[P2] => [P1&P2:T]
///
/// Intuitively, since the conformance requirements on the merged term
/// are not prefixed by the root τ_0_0, they apply at any level; we've
/// "tied off" the recursion, and the rewrite system is now convergent.
void RewriteSystem::processMergedAssociatedTypes() {
  if (MergedAssociatedTypes.empty())
    return;

  unsigned i = 0;

  // Chase the end of the vector, since addRule() might add new elements below.
  while (i < MergedAssociatedTypes.size()) {
    // Copy the entry out, since addRule() might add new elements below.
    auto entry = MergedAssociatedTypes[i++];

    if (Debug.contains(DebugFlags::Merge)) {
      llvm::dbgs() << "## Processing associated type merge with ";
      llvm::dbgs() << entry.rhs << ", ";
      llvm::dbgs() << entry.lhsSymbol << ", ";
      llvm::dbgs() << entry.mergedSymbol << "\n";
    }

    // If we have X.[P2:T] => Y.[P1:T], add a new rule:
    // X.[P1:T] => X.[P1&P2:T]
    MutableTerm lhs(entry.rhs);

    // Build the term X.[P1&P2:T].
    MutableTerm rhs(entry.rhs);
    rhs.back() = entry.mergedSymbol;

    // Add the rule X.[P1:T] => X.[P1&P2:T].
    addRule(lhs, rhs);

    // Collect new rules here so that we're not adding rules while traversing
    // the trie.
    SmallVector<std::pair<MutableTerm, MutableTerm>, 2> inducedRules;

    // Look for conformance requirements on [P1:T] and [P2:T].
    auto visitRule = [&](unsigned ruleID) {
      const auto &otherRule = getRule(ruleID);
      const auto &otherLHS = otherRule.getLHS();
      if (otherLHS.size() == 2 &&
          otherLHS[1].getKind() == Symbol::Kind::Protocol) {
        if (otherLHS[0] == entry.lhsSymbol ||
            otherLHS[0] == entry.rhs.back()) {
          // We have a rule of the form
          //
          //   [P1:T].[Q] => [P1:T]
          //
          // or
          //
          //   [P2:T].[Q] => [P2:T]
          if (Debug.contains(DebugFlags::Merge)) {
            llvm::dbgs() << "### Lifting conformance rule " << otherRule << "\n";
          }

          // We know that [P1:T] or [P2:T] conforms to Q, therefore the
          // merged type [P1&P2:T] must conform to Q as well. Add a new rule
          // of the form:
          //
          //   [P1&P2:T].[Q] => [P1&P2:T]
          //
          MutableTerm newLHS;
          newLHS.add(entry.mergedSymbol);
          newLHS.add(otherLHS[1]);

          MutableTerm newRHS;
          newRHS.add(entry.mergedSymbol);

          inducedRules.emplace_back(newLHS, newRHS);
        }
      }
    };

    // Visit rhs first to preserve the ordering of protocol requirements in the
    // the property map. This is just for aesthetic purposes in the debug dump,
    // it doesn't change behavior.
    Trie.findAll(entry.rhs.back(), visitRule);
    Trie.findAll(entry.lhsSymbol, visitRule);

    // Now add the new rules.
    for (const auto &pair : inducedRules)
      addRule(pair.first, pair.second);
  }

  MergedAssociatedTypes.clear();
}

/// Check if we have a rule of the form
///
///   X.[P1:T] => X.[P2:T]
///
/// If so, record this rule for later. We'll try to merge the associated
/// types in RewriteSystem::processMergedAssociatedTypes().
void RewriteSystem::checkMergedAssociatedType(Term lhs, Term rhs) {
  // FIXME: Figure out 3-cell representation for merged associated types
  if (RecordLoops)
    return;

  if (lhs.size() == rhs.size() &&
      std::equal(lhs.begin(), lhs.end() - 1, rhs.begin()) &&
      lhs.back().getKind() == Symbol::Kind::AssociatedType &&
      rhs.back().getKind() == Symbol::Kind::AssociatedType &&
      lhs.back().getName() == rhs.back().getName()) {
    if (Debug.contains(DebugFlags::Merge)) {
      llvm::dbgs() << "## Associated type merge candidate ";
      llvm::dbgs() << lhs << " => " << rhs << "\n\n";
    }

    auto mergedSymbol = Context.mergeAssociatedTypes(lhs.back(), rhs.back());
    if (Debug.contains(DebugFlags::Merge)) {
      llvm::dbgs() << "### Merged symbol " << mergedSymbol << "\n";
    }

    // We must have mergedSymbol <= rhs < lhs, therefore mergedSymbol != lhs.
    assert(lhs.back() != mergedSymbol &&
           "Left hand side should not already end with merged symbol?");
    assert(mergedSymbol.compare(rhs.back(), Context) <= 0);
    assert(rhs.back().compare(lhs.back(), Context) < 0);

    // If the merge didn't actually produce a new symbol, there is nothing else
    // to do.
    if (rhs.back() == mergedSymbol) {
      if (Debug.contains(DebugFlags::Merge)) {
        llvm::dbgs() << "### Skipping\n";
      }

      return;
    }

    MergedAssociatedTypes.push_back({rhs, lhs.back(), mergedSymbol});
  }
}

/// Compute a critical pair from the left hand sides of two rewrite rules,
/// where \p rhs begins at \p from, which must be an iterator pointing
/// into \p lhs.
///
/// The resulting pair is pushed onto \p result only if it is non-trivial,
/// that is, the left hand side and right hand side are not equal.
///
/// Returns true if the pair was non-trivial, false if it was trivial.
///
/// There are two cases:
///
/// 1) lhs == TUV -> X, rhs == U -> Y. The overlapped term is TUV;
///    applying lhs and rhs, respectively, yields the critical pair
///    (X, TYV).
///
/// 2) lhs == TU -> X, rhs == UV -> Y. The overlapped term is once
///    again TUV; applying lhs and rhs, respectively, yields the
///    critical pair (XV, TY).
///
/// If lhs and rhs have identical left hand sides, either case could
/// apply, but we arbitrarily pick case 1.
///
/// There is also an additional wrinkle. If we're in case 2, and the
/// last symbol of V is a superclass or concrete type symbol A, we prepend
/// T to each substitution of A.
///
/// For example, suppose we have the following two rules:
///
/// A.B -> C
/// B.[concrete: Foo<X>] -> B
///
/// The overlapped term is A.B.[concrete: Foo<X>], so the critical pair
/// is (C.[concrete: Foo<A.X>], A.B). We prepended 'A' to the
/// concrete substitution 'X' to get 'A.X'; the new concrete term
/// is now rooted at the same level as A.B in the rewrite system,
/// not just B.
bool
RewriteSystem::computeCriticalPair(ArrayRef<Symbol>::const_iterator from,
                                   const Rule &lhs, const Rule &rhs,
                                   std::vector<std::pair<MutableTerm,
                                                         MutableTerm>> &pairs,
                                   std::vector<RewritePath> &paths,
                                   std::vector<RewriteLoop> &loops) const {
  auto end = lhs.getLHS().end();
  if (from + rhs.getLHS().size() < end) {
    // lhs == TUV -> X, rhs == U -> Y.

    // Note: This includes the case where the two rules have exactly
    // equal left hand sides; that is, lhs == U -> X, rhs == U -> Y.
    //
    // In this case, T and V are both empty.

    // Compute the terms T and V.
    MutableTerm t(lhs.getLHS().begin(), from);
    MutableTerm v(from + rhs.getLHS().size(), lhs.getLHS().end());

    // Compute the term TYV.
    MutableTerm tyv(t);
    tyv.append(rhs.getRHS());
    tyv.append(v);

    MutableTerm x(lhs.getRHS());

    // Compute a path from X to TYV: (X => TUV) ⊗ T.(U => Y).V
    RewritePath path;

    // (1) First, apply the left hand side rule in the reverse direction:
    //
    //     (X => TUV)
    path.add(RewriteStep::forRewriteRule(/*startOffset=*/0,
                                         /*endOffset=*/0,
                                         getRuleID(lhs),
                                         /*inverse=*/true));
    // (2) Now, apply the right hand side in the forward direction:
    //
    //     T.(U => Y).V 
    path.add(RewriteStep::forRewriteRule(t.size(), v.size(),
                                         getRuleID(rhs),
                                         /*inverse=*/false));

    // If X == TYV, we have a trivial overlap.
    if (x == tyv) {
      loops.emplace_back(x, path);
      return false;
    }

    // Add the pair (X, TYV).
    pairs.emplace_back(x, tyv);
    paths.push_back(path);
  } else {
    // lhs == TU -> X, rhs == UV -> Y.

    // Compute the terms T and V.
    MutableTerm t(lhs.getLHS().begin(), from);
    MutableTerm v(rhs.getLHS().begin() + (lhs.getLHS().end() - from),
                  rhs.getLHS().end());

    // Compute the term XV.
    MutableTerm xv(lhs.getRHS());
    xv.append(v);

    // Compute the term TY.
    MutableTerm ty(t);
    ty.append(rhs.getRHS());

    // Compute a path from XV to TY: (X => TU).V ⊗ (σ - T) ⊗ T.(UV => Y)
    RewritePath path;

    // (1) First, apply the left hand side rule in the reverse direction:
    //
    //     (X => TU).V
    path.add(RewriteStep::forRewriteRule(/*startOffset=*/0, v.size(),
                                         getRuleID(lhs),
                                         /*inverse=*/true));

    // (2) Next, if the right hand side rule ends with a concrete type symbol,
    // perform the concrete type adjustment:
    //
    //     (σ - T)
    if (xv.back().isSuperclassOrConcreteType() &&
        !xv.back().getSubstitutions().empty() &&
        t.size() > 0) {
      path.add(RewriteStep::forAdjustment(t.size(), /*inverse=*/true));

      xv.back() = xv.back().prependPrefixToConcreteSubstitutions(
          t, Context);
    }

    // (3) Finally, apply the right hand side in the forward direction:
    //
    //     T.(UV => Y)
    path.add(RewriteStep::forRewriteRule(t.size(), /*endOffset=*/0,
                                         getRuleID(rhs),
                                         /*inverse=*/false));

    // If XV == TY, we have a trivial overlap.
    if (xv == ty) {
      loops.emplace_back(xv, path);
      return false;
    }

    // Add the pair (XV, TY).
    pairs.emplace_back(xv, ty);
    paths.push_back(path);
  }

  return true;
}

/// Computes the confluent completion using the Knuth-Bendix algorithm.
///
/// Returns a pair consisting of a status and number of iterations executed.
///
/// The status is CompletionResult::MaxIterations if we exceed \p maxIterations
/// iterations.
///
/// The status is CompletionResult::MaxDepth if we produce a rewrite rule whose
/// left hand side has a length exceeding \p maxDepth.
///
/// Otherwise, the status is CompletionResult::Success.
std::pair<CompletionResult, unsigned>
RewriteSystem::computeConfluentCompletion(unsigned maxIterations,
                                          unsigned maxDepth) {
  assert(Initialized);
  assert(!Minimized);

  // Complete might already be set, if we're re-running completion after
  // adding new rules in the property map's concrete type unification procedure.
  Complete = 1;

  unsigned steps = 0;

  bool again = false;

  std::vector<std::pair<MutableTerm, MutableTerm>> resolvedCriticalPairs;
  std::vector<RewritePath> resolvedPaths;
  std::vector<RewriteLoop> resolvedLoops;

  do {
    // For every rule, looking for other rules that overlap with this rule.
    for (unsigned i = 0, e = Rules.size(); i < e; ++i) {
      const auto &lhs = getRule(i);
      if (lhs.isSimplified())
        continue;

      // Look up every suffix of this rule in the trie using findAll(). This
      // will find both kinds of overlap:
      //
      // 1) rules whose left hand side is fully contained in [from,to)
      // 2) rules whose left hand side has a prefix equal to [from,to)
      auto from = lhs.getLHS().begin();
      auto to = lhs.getLHS().end();
      while (from < to) {
        Trie.findAll(from, to, [&](unsigned j) {
          // We don't have to consider the same pair of rules more than once,
          // since those critical pairs were already resolved.
          if (!CheckedOverlaps.insert(std::make_pair(i, j)).second)
            return;

          const auto &rhs = getRule(j);
          if (rhs.isSimplified())
            return;

          if (from == lhs.getLHS().begin()) {
            // While every rule will have an overlap of the first kind
            // with itself, it's not useful to consider since the
            // resulting critical pair is always trivial.
            if (i == j)
              return;

            // If the first rule's left hand side is a proper prefix
            // of the second rule's left hand side, don't do anything.
            //
            // We will find the 'opposite' overlap later, where the two
            // rules are swapped around. Then it becomes an overlap of
            // the first kind, and will be handled as such.
            if (rhs.getLHS().size() > lhs.getLHS().size())
              return;
          }

          // Try to repair the confluence violation by adding a new rule.
          if (computeCriticalPair(from, lhs, rhs,
                                  resolvedCriticalPairs,
                                  resolvedPaths,
                                  resolvedLoops)) {
            if (Debug.contains(DebugFlags::Completion)) {
              const auto &pair = resolvedCriticalPairs.back();
              const auto &path = resolvedPaths.back();

              llvm::dbgs() << "$ Overlapping rules: (#" << i << ") ";
              llvm::dbgs() << lhs << "\n";
              llvm::dbgs() << "                -vs- (#" << j << ") ";
              llvm::dbgs() << rhs << ":\n";
              llvm::dbgs() << "$$ First term of critical pair is "
                           << pair.first << "\n";
              llvm::dbgs() << "$$ Second term of critical pair is "
                           << pair.second << "\n\n";

              llvm::dbgs() << "$$ Resolved via path: ";
              path.dump(llvm::dbgs(), pair.first, *this);
              llvm::dbgs() << "\n\n";
            }
          } else {
            if (Debug.contains(DebugFlags::Completion)) {
              const auto &loop = resolvedLoops.back();

              llvm::dbgs() << "$ Trivially overlapping rules: (#" << i << ") ";
              llvm::dbgs() << lhs << "\n";
              llvm::dbgs() << "                          -vs- (#" << j << ") ";
              llvm::dbgs() << rhs << ":\n";

              llvm::dbgs() << "$$ Loop: ";
              loop.dump(llvm::dbgs(), *this);
              llvm::dbgs() << "\n\n";
            }
          }
        });

        ++from;
      }
    }

    simplifyRewriteSystem();

    assert(resolvedCriticalPairs.size() == resolvedPaths.size());

    again = false;
    for (unsigned index : indices(resolvedCriticalPairs)) {
      const auto &pair = resolvedCriticalPairs[index];
      const auto &path = resolvedPaths[index];

      // Check if we've already done too much work.
      if (Rules.size() > maxIterations)
        return std::make_pair(CompletionResult::MaxIterations, steps);

      if (!addRule(pair.first, pair.second, &path))
        continue;

      // Check if the new rule is too long.
      if (Rules.back().getDepth() > maxDepth)
        return std::make_pair(CompletionResult::MaxDepth, steps);

      // Only count a 'step' once we add a new rule.
      ++steps;
      again = true;
    }

    for (const auto &loop : resolvedLoops) {
      recordRewriteLoop(loop);
    }

    resolvedCriticalPairs.clear();
    resolvedPaths.clear();
    resolvedLoops.clear();

    // If the added rules merged any associated types, process the merges now
    // before we continue with the completion procedure. This is important
    // to perform incrementally since merging is required to repair confluence
    // violations.
    processMergedAssociatedTypes();
  } while (again);

  assert(MergedAssociatedTypes.empty() &&
         "Should have processed all merge candidates");

  return std::make_pair(CompletionResult::Success, steps);
}
