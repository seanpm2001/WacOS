//===--- DependencyGraph.h - Track intra-module dependencies ----*- C++ -*-===//
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

#ifndef SWIFT_DRIVER_DEPENDENCYGRAPH_H
#define SWIFT_DRIVER_DEPENDENCYGRAPH_H

#include "swift/Basic/LLVM.h"
#include "swift/Basic/OptionSet.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <string>
#include <vector>

namespace llvm {
  class MemoryBuffer;
}

namespace swift {

/// The non-templated implementation of DependencyGraph.
///
/// \see DependencyGraph
class DependencyGraphImpl {
public:
  /// Possible dependency kinds.
  ///
  /// Clients of DependencyGraph should have no reason to use this type.
  /// It is only used in the implementation.
  enum class DependencyKind : uint8_t;

  /// Describes the result of loading a dependency file for a particular node.
  enum class LoadResult {
    /// There was an error loading the file; the entire graph should be
    /// considered suspect.
    HadError,

    /// The file was loaded successfully; with current information the node
    /// does not need to be rebuilt.
    UpToDate,

    /// The file was loaded successfully; anything that depends on the node
    /// should be considered out of date.
    AffectsDownstream
  };

  /// The non-templated implementation of DependencyGraph::MarkTracer.
  ///
  /// \see DependencyGraph::MarkTracer
  class MarkTracerImpl {
    class Entry;
    llvm::DenseMap<const void *, SmallVector<Entry, 4>> Table;

    friend class DependencyGraphImpl;
  protected:
    MarkTracerImpl();
    ~MarkTracerImpl();

    void printPath(raw_ostream &out, const void *item,
                   llvm::function_ref<void(const void *)> printItem) const;
  };

private:
  enum class DependencyFlags : uint8_t;
  using DependencyMaskTy = OptionSet<DependencyKind>;
  using DependencyFlagsTy = OptionSet<DependencyFlags>;

  struct DependencyEntryTy {
    const void *node;
    DependencyMaskTy kindMask;
    DependencyFlagsTy flags;
  };
  static_assert(std::is_move_constructible<DependencyEntryTy>::value, "");

  struct ProvidesEntryTy {
    std::string name;
    DependencyMaskTy kindMask;
  };
  static_assert(std::is_move_constructible<ProvidesEntryTy>::value, "");

  /// The "outgoing" edge map. This lists all outgoing (kind, string) edges
  /// representing satisfied dependencies from a particular node.
  ///
  /// For multiple outgoing edges with the same string, the kinds are combined
  /// into one field.
  ///
  /// \sa DependencyMaskTy
  llvm::DenseMap<const void *, std::vector<ProvidesEntryTy>> Provides;

  /// The "incoming" edge map. Semantically this maps incoming (kind, string)
  /// edges representing dependencies to the nodes that depend on them, as
  /// well as a flag marking whether that (kind, string) pair has been marked
  /// dirty.
  ///
  /// The representation is a map from strings to kind mask / node pairs, plus
  /// a mask of kinds that have been marked dirty. This is because it is
  /// unusual (though not impossible) for dependencies of different kinds to
  /// have the same strings. In the case of multiple incoming edges with the
  /// same string, the kinds are combined into the one field.
  ///
  /// \sa DependencyMaskTy
  llvm::StringMap<std::pair<std::vector<DependencyEntryTy>, DependencyMaskTy>> Dependencies;

  /// The set of marked nodes.
  llvm::SmallPtrSet<const void *, 16> Marked;

  /// A list of all "external" dependencies that cannot be resolved just from
  /// this dependency graph.
  llvm::StringSet<> ExternalDependencies;

  /// The interface hash for each node. This determines if the interface of
  /// a modified file has changed.
  ///
  /// \sa SourceFile::getInterfaceHash
  llvm::DenseMap<const void *, std::string> InterfaceHashes;

  LoadResult loadFromBuffer(const void *node, llvm::MemoryBuffer &buffer);

  // FIXME: We should be able to use llvm::mapped_iterator for this, but
  // StringMapConstIterator isn't quite an InputIterator (no ->).
  class StringSetIterator {
    llvm::StringSet<>::const_iterator I;
  public:
    typedef llvm::StringSet<>::const_iterator::value_type value_type;
    typedef std::input_iterator_tag iterator_category;
    typedef ptrdiff_t difference_type;
    typedef value_type &reference;
    typedef value_type *pointer;

    /*implicit*/ StringSetIterator(llvm::StringSet<>::const_iterator base)
       : I(base) {}

    StringSetIterator &operator++() {
      ++I;
      return *this;
    }

    StringRef operator*() const {
      return I->getKey();
    }

    bool operator==(StringSetIterator other) const { return I == other.I; }
    bool operator!=(StringSetIterator other) const { return I != other.I; }
  };

protected:
  LoadResult loadFromString(const void *node, StringRef data);
  LoadResult loadFromPath(const void *node, StringRef path);

  void addIndependentNode(const void *node) {
    bool newlyInserted = Provides.insert({node, {}}).second;
    assert(newlyInserted && "node is already in graph");
    (void)newlyInserted;
  }

  void markTransitive(SmallVectorImpl<const void *> &visited,
                      const void *node, MarkTracerImpl *tracer = nullptr);
  bool markIntransitive(const void *node) {
    assert(Provides.count(node) && "node is not in the graph");
    return Marked.insert(node).second;
  }
  void markExternal(SmallVectorImpl<const void *> &visited,
                    StringRef externalDependency);

  bool isMarked(const void *node) const {
    assert(Provides.count(node) && "node is not in the graph");
    return Marked.count(node);
  }

public:
  llvm::iterator_range<StringSetIterator> getExternalDependencies() const {
    return llvm::make_range(StringSetIterator(ExternalDependencies.begin()),
                            StringSetIterator(ExternalDependencies.end()));
  }
};

/// Tracks dependencies between opaque nodes.
///
/// This is implemented in terms of separate "depends" and "provides" sets
/// that together represent edges between nodes. Abstractly, each edge is
/// labeled with a (kind, string) pair, where the "kind" distinguishes
/// different kinds of dependencies. A node's "provides" set is matched up
/// with other nodes' "depends" sets to form a traversable directed graph.
/// Information on a particular node can be updated at any time, which will
/// affect any following operations. The "depends" entries can be "cascading"
/// or "non-cascading", which describes whether or not downstream nodes should
/// be traversed after following a particular dependency edge.
///
/// The graph also supports a "mark" operation, which is intended to track
/// nodes that have been not just visited but transitively marked through.
template <typename T>
class DependencyGraph : public DependencyGraphImpl {
  using Traits = llvm::PointerLikeTypeTraits<T>;
  static_assert(Traits::NumLowBitsAvailable >= 0, "not a pointer-like type");

  static void copyBack(SmallVectorImpl<T> &result,
                       ArrayRef<const void *> rawNodes) {
    result.reserve(result.size() + rawNodes.size());
    std::transform(rawNodes.begin(), rawNodes.end(), std::back_inserter(result),
                   [](const void *rawNode) {
      return Traits::getFromVoidPointer(const_cast<void *>(rawNode));
    });
  }

public:
  /// Traces the graph traversal performed in DependencyGraph::markTransitive.
  ///
  /// This is intended to be a debugging aid.
  class MarkTracer : public MarkTracerImpl {
  public:
    MarkTracer() = default;

    /// Dump the path that led to \p node.
    void printPath(raw_ostream &out, T node,
                   llvm::function_ref<void(raw_ostream &, T)> printItem) const {
      MarkTracerImpl::printPath(out, Traits::getAsVoidPointer(node),
                                [printItem, &out](const void *n) {
        printItem(out, Traits::getFromVoidPointer(n));
      });
    }
  };

  /// Load "depends" and "provides" data for \p node from the file at the given
  /// path.
  ///
  /// If \p node is already in the graph, outgoing edges ("provides") are
  /// cleared and replaced with the newly loaded data. Incoming edges
  /// ("depends") are not cleared; new dependencies are considered additive.
  ///
  /// If \p node has already been marked, only its outgoing edges are updated.
  LoadResult loadFromPath(T node, StringRef path) {
    return DependencyGraphImpl::loadFromPath(Traits::getAsVoidPointer(node),
                                             path);
  }

  /// Load "depends" and "provides" data for \p node from a plain string.
  ///
  /// This is only intended for testing purposes.
  ///
  /// \sa loadFromPath
  LoadResult loadFromString(T node, StringRef data) {
    return DependencyGraphImpl::loadFromString(Traits::getAsVoidPointer(node),
                                               data);
  }

  /// Adds \p node to the dependency graph without any connections.
  ///
  /// This can be used for new nodes that may be updated later.
  void addIndependentNode(T node) {
    return
        DependencyGraphImpl::addIndependentNode(Traits::getAsVoidPointer(node));
  }

  /// Marks \p node and all nodes that depend on \p node, and places any nodes
  /// that get transitively marked into \p visited.
  ///
  /// Nodes that have been previously marked are not included in \p newlyMarked,
  /// nor are their successors traversed, <em>even if their "provides" set has
  /// been updated since it was marked.</em> (However, nodes that depend on the
  /// given \p node are always traversed.)
  ///
  /// Nodes that are only reachable through "non-cascading" edges are added to
  /// the \p visited set, but are \em not added to the graph's marked set.
  ///
  /// If you want to see how each node gets added to \p visited, pass a local
  /// MarkTracer instance to \p tracer.
  template <unsigned N>
  void markTransitive(SmallVector<T, N> &visited, T node,
                      MarkTracer *tracer = nullptr) {
    SmallVector<const void *, N> rawMarked;
    DependencyGraphImpl::markTransitive(rawMarked,
                                        Traits::getAsVoidPointer(node),
                                        tracer);
    // FIXME: How can we avoid this copy?
    copyBack(visited, rawMarked);
  }

  template <unsigned N>
  void markExternal(SmallVector<T, N> &visited, StringRef externalDependency) {
    SmallVector<const void *, N> rawMarked;
    DependencyGraphImpl::markExternal(rawMarked, externalDependency);
    // FIXME: How can we avoid this copy?
    copyBack(visited, rawMarked);
  }

  /// Marks \p node without marking any dependencies.
  ///
  /// \returns true if the node is newly marked, false if not.
  ///
  /// \sa #markTransitive
  bool markIntransitive(T node) {
    return
        DependencyGraphImpl::markIntransitive(Traits::getAsVoidPointer(node));
  }

  /// Returns true if \p node has been marked (directly or transitively).
  bool isMarked(T node) const {
    return DependencyGraphImpl::isMarked(Traits::getAsVoidPointer(node));
  }
};

} // end namespace swift

#endif
