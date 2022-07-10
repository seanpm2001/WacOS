//===--- Concurrent.h - Concurrent Data Structures  -------------*- C++ -*-===//
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
#ifndef SWIFT_RUNTIME_CONCURRENTUTILS_H
#define SWIFT_RUNTIME_CONCURRENTUTILS_H
#include <iterator>
#include <atomic>
#include <stdint.h>

#if defined(__FreeBSD__)
#include <stdio.h>
#endif

/// This is a node in a concurrent linked list.
template <class ElemTy> struct ConcurrentListNode {
  ConcurrentListNode(ElemTy Elem) : Payload(Elem), Next(nullptr) {}
  ConcurrentListNode(const ConcurrentListNode &) = delete;
  ConcurrentListNode &operator=(const ConcurrentListNode &) = delete;

  /// The element.
  ElemTy Payload;
  /// Points to the next link in the chain.
  ConcurrentListNode<ElemTy> *Next;
};

/// This is a concurrent linked list. It supports insertion at the beginning
/// of the list and traversal using iterators.
/// This is a very simple implementation of a concurrent linked list
/// using atomic operations. The 'push_front' method allocates a new link
/// and attempts to compare and swap the old head pointer with pointer to
/// the new link. This operation may fail many times if there are other
/// contending threads, but eventually the head pointer is set to the new
/// link that already points to the old head value. Notice that the more
/// difficult feature of removing links is not supported.
/// See 'push_front' for more details.
template <class ElemTy> struct ConcurrentList {
  ConcurrentList() : First(nullptr) {}
  ~ConcurrentList() {
    clear();
  }

  /// Remove all of the links in the chain. This method leaves
  /// the list at a usable state and new links can be added.
  /// Notice that this operation is non-concurrent because
  /// we have no way of ensuring that no one is currently
  /// traversing the list.
  void clear() {
    // Iterate over the list and delete all the nodes.
    auto Ptr = First.load(std::memory_order_acquire);
    First.store(nullptr, std:: memory_order_release);

    while (Ptr) {
      auto N = Ptr->Next;
      delete Ptr;
      Ptr = N;
    }
  }

  ConcurrentList(const ConcurrentList &) = delete;
  ConcurrentList &operator=(const ConcurrentList &) = delete;

  /// A list iterator.
  struct ConcurrentListIterator :
      public std::iterator<std::forward_iterator_tag, ElemTy> {

    /// Points to the current link.
    ConcurrentListNode<ElemTy> *Ptr;
    /// C'tor.
    ConcurrentListIterator(ConcurrentListNode<ElemTy> *P) : Ptr(P) {}
    /// Move to the next element.
    ConcurrentListIterator &operator++() {
      Ptr = Ptr->Next;
      return *this;
    }
    /// Access the element.
    ElemTy &operator*() { return Ptr->Payload; }
    /// Same?
    bool operator==(const ConcurrentListIterator &o) const {
      return o.Ptr == Ptr;
    }
    /// Not the same?
    bool operator!=(const ConcurrentListIterator &o) const {
      return o.Ptr != Ptr;
    }
  };

  /// Iterator entry point.
  typedef ConcurrentListIterator iterator;
  /// Marks the beginning of the list.
  iterator begin() const {
    return ConcurrentListIterator(First.load(std::memory_order_acquire));
  }
  /// Marks the end of the list.
  iterator end() const { return ConcurrentListIterator(nullptr); }

  /// Add a new item to the list.
  void push_front(ElemTy Elem) {
    /// Allocate a new node.
    ConcurrentListNode<ElemTy> *N = new ConcurrentListNode<ElemTy>(Elem);
    // Point to the first element in the list.
    N->Next = First.load(std::memory_order_acquire);
    auto OldFirst = N->Next;
    // Try to replace the current First with the new node.
    while (!std::atomic_compare_exchange_weak_explicit(&First, &OldFirst, N,
                                               std::memory_order_release,
                                               std::memory_order_relaxed)) {
      // If we fail, update the new node to point to the new head and try to
      // insert before the new
      // first element.
      N->Next = OldFirst;
    }
  }

  /// Points to the first link in the list.
  std::atomic<ConcurrentListNode<ElemTy> *> First;
};

/// A concurrent map that is implemented using a binary tree. It supports
/// concurrent insertions but does not support removals or rebalancing of
/// the tree.
///
/// The entry type must provide the following operations:
///
///   /// For debugging purposes only. Summarize this key as an integer value.
///   long getKeyIntValueForDump() const;
///
///   /// A ternary comparison.  KeyTy is the type of the key provided
///   /// to find or getOrInsert.
///   int compareWithKey(KeyTy key) const;
///
///   /// Return the amount of extra trailing space required by an entry,
///   /// where KeyTy is the type of the first argument to getOrInsert and
///   /// ArgTys is the type of the remaining arguments.
///   static size_t getExtraAllocationSize(KeyTy key, ArgTys...)
template <class EntryTy> class ConcurrentMap {
  struct Node {
    std::atomic<Node*> Left;
    std::atomic<Node*> Right;
    EntryTy Payload;

    template <class... Args>
    Node(Args &&... args)
      : Left(nullptr), Right(nullptr), Payload(std::forward<Args>(args)...) {}

    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;

    ~Node() {
      // These can be relaxed accesses because there is no safe way for
      // another thread to race an access to this node with our destruction
      // of it.
      ::delete Left.load(std::memory_order_relaxed);
      ::delete Right.load(std::memory_order_relaxed);
    }

#ifndef NDEBUG
    void dump() const {
      auto L = Left.load(std::memory_order_acquire);
      auto R = Right.load(std::memory_order_acquire);
      printf("\"%p\" [ label = \" {<f0> %08lx | {<f1> | <f2>}}\" "
             "style=\"rounded\" shape=\"record\"];\n",
             this, (long) Payload.getKeyValueForDump());

      if (L) {
        L->dump();
        printf("\"%p\":f1 -> \"%p\":f0;\n", this, L);
      }
      if (R) {
        R->dump();
        printf("\"%p\":f2 -> \"%p\":f0;\n", this, R);
      }
    }
#endif
  };

  /// The root of the tree.
  std::atomic<Node*> Root;

  /// This member stores the address of the last node that was found by the
  /// search procedure. We cache the last search to accelerate code that
  /// searches the same value in a loop.
  std::atomic<Node*> LastSearch;

public:
  constexpr ConcurrentMap() : Root(nullptr), LastSearch(nullptr) {}

  ConcurrentMap(const ConcurrentMap &) = delete;
  ConcurrentMap &operator=(const ConcurrentMap &) = delete;

  ~ConcurrentMap() {
    ::delete Root.load(std::memory_order_relaxed);
  }

#ifndef NDEBUG
  void dump() const {
    auto R = Root.load(std::memory_order_acquire);
    printf("digraph g {\n"
           "graph [ rankdir = \"TB\"];\n"
           "node  [ fontsize = \"16\" ];\n"
           "edge  [ ];\n");
    if (R) {
      R->dump();
    }
    printf("\n}\n");
  }
#endif

  /// Search for a value by key \p Key.
  /// \returns a pointer to the value or null if the value is not in the map.
  template <class KeyTy>
  EntryTy *find(const KeyTy &key) {
    // Check if we are looking for the same key that we looked for in the last
    // time we called this function.
    if (Node *last = LastSearch.load(std::memory_order_acquire)) {
      if (last->Payload.compareWithKey(key) == 0)
        return &last->Payload;
    }

    // Search the tree, starting from the root.
    Node *node = Root.load(std::memory_order_acquire);
    while (node) {
      int comparisonResult = node->Payload.compareWithKey(key);
      if (comparisonResult == 0) {
        LastSearch.store(node, std::memory_order_release);
        return &node->Payload;
      } else if (comparisonResult < 0) {
        node = node->Left.load(std::memory_order_acquire);
      } else {
        node = node->Right.load(std::memory_order_acquire);
      }
    }

    return nullptr;
  }

  /// Get or create an entry in the map.
  ///
  /// \returns the entry in the map and whether a new node was added (true)
  ///   or already existed (false)
  template <class KeyTy, class... ArgTys>
  std::pair<EntryTy*, bool> getOrInsert(KeyTy key, ArgTys &&... args) {
    // Check if we are looking for the same key that we looked for the
    // last time we called this function.
    if (Node *last = LastSearch.load(std::memory_order_acquire)) {
      if (last && last->Payload.compareWithKey(key) == 0)
        return { &last->Payload, false };
    }

    // The node we allocated.
    Node *newNode = nullptr;

    // Start from the root.
    auto edge = &Root;

    while (true) {
      // Load the edge.
      Node *node = edge->load(std::memory_order_acquire);

      // If there's a node there, it's either a match or we're going to
      // one of its children.
      if (node) {
      searchFromNode:

        // Compare our key against the node's key.
        int comparisonResult = node->Payload.compareWithKey(key);

        // If it's equal, we can use this node.
        if (comparisonResult == 0) {
          // Destroy the node we allocated before if we're carrying one around.
          ::delete newNode;

          // Cache and report that we found an existing node.
          LastSearch.store(node, std::memory_order_release);
          return { &node->Payload, false };
        }

        // Otherwise, select the appropriate child edge and descend.
        edge = (comparisonResult < 0 ? &node->Left : &node->Right);
        continue;
      }

      // Create a new node.
      if (!newNode) {
        size_t allocSize =
          sizeof(Node) + EntryTy::getExtraAllocationSize(key, args...);
        void *memory = ::operator new(allocSize);
        newNode = ::new (memory) Node(key, std::forward<ArgTys>(args)...);
      }

      // Try to set the edge to the new node.
      if (std::atomic_compare_exchange_strong_explicit(edge, &node, newNode,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
        // If that succeeded, cache and report that we created a new node.
        LastSearch.store(newNode, std::memory_order_release);
        return { &newNode->Payload, true };
      }

      // Otherwise, we lost the race because some other thread initialized
      // the edge before us.  node will be set to the current value;
      // repeat the search from there.
      assert(node && "spurious failure from compare_exchange_strong?");
      goto searchFromNode;
    }
  }
};

#endif // SWIFT_RUNTIME_CONCURRENTUTILS_H
