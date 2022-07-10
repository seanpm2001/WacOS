//===--- BlotMapVector.h - Map vector with "blot" operation  ----*- C++ -*-===//
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

#ifndef SWIFT_BASIC_BLOTMAPVECTOR_H
#define SWIFT_BASIC_BLOTMAPVECTOR_H

#include "llvm/ADT/DenseMap.h"
#include "swift/Basic/LLVM.h"
#include "swift/Basic/Range.h"
#include <vector>

namespace swift {

template <typename KeyT, typename ValueT>
bool compareKeyAgainstDefaultKey(const std::pair<KeyT, ValueT> &Pair) {
  return Pair.first == KeyT();
}

/// \brief An associative container with fast insertion-order (deterministic)
/// iteration over its elements. Plus the special blot operation.
template <typename KeyT, typename ValueT,
          typename MapT = llvm::DenseMap<KeyT, size_t>,
          typename VectorT = std::vector<Optional<std::pair<KeyT, ValueT>>>>
class BlotMapVector {
    /// Map keys to indices in Vector.
    MapT Map;

    /// Keys and values.
    VectorT Vector;

  public:
    using iterator = typename VectorT::iterator;
    using const_iterator = typename VectorT::const_iterator;
    using key_type = KeyT;
    using mapped_type = ValueT;

    iterator begin() { return Vector.begin(); }
    iterator end() { return Vector.end(); }
    const_iterator begin() const { return Vector.begin(); }
    const_iterator end() const { return Vector.end(); }

    iterator_range<iterator> getItems() {
      return swift::make_range(begin(), end());
    }

    ValueT &operator[](const KeyT &Arg) {
      auto Pair = Map.insert(std::make_pair(Arg, size_t(0)));
      if (Pair.second) {
        size_t Num = Vector.size();
        Pair.first->second = Num;
        Vector.push_back({std::make_pair(Arg, ValueT())});
        return (*Vector[Num]).second;
      }
      return Vector[Pair.first->second].getValue().second;
    }

    std::pair<iterator, bool>
    insert(const std::pair<KeyT, ValueT> &InsertPair) {
      auto Pair = Map.insert(std::make_pair(InsertPair.first, size_t(0)));
      if (Pair.second) {
        size_t Num = Vector.size();
        Pair.first->second = Num;
        Vector.push_back(InsertPair);
        return std::make_pair(Vector.begin() + Num, true);
      }
      return std::make_pair(Vector.begin() + Pair.first->second, false);
    }

    iterator find(const KeyT &Key) {
      typename MapT::iterator It = Map.find(Key);
      if (It == Map.end()) return Vector.end();
      auto Iter = Vector.begin() + It->second;
      if (!Iter->hasValue())
        return Vector.end();
      return Iter;
    }

    const_iterator find(const KeyT &Key) const {
      return const_cast<BlotMapVector &>(*this)->find(Key);
    }

    /// This is similar to erase, but instead of removing the element from the
    /// vector, it just zeros out the key in the vector. This leaves iterators
    /// intact, but clients must be prepared for zeroed-out keys when iterating.
    void erase(const KeyT &Key) { blot(Key); }

    /// This is similar to erase, but instead of removing the element from the
    /// vector, it just zeros out the key in the vector. This leaves iterators
    /// intact, but clients must be prepared for zeroed-out keys when iterating.
    void erase(iterator I) { erase((*I)->first); }

    /// This is similar to erase, but instead of removing the element from the
    /// vector, it just zeros out the key in the vector. This leaves iterators
    /// intact, but clients must be prepared for zeroed-out keys when iterating.
    void blot(const KeyT &Key) {
      typename MapT::iterator It = Map.find(Key);
      if (It == Map.end()) return;
      Vector[It->second] = None;
      Map.erase(It);
    }

    void clear() {
      Map.clear();
      Vector.clear();
    }

    unsigned size() const { return Map.size(); }

    ValueT lookup(const KeyT &Val) const {
      auto Iter = Map.find(Val);
      if (Iter == Map.end())
        return ValueT();
      auto &P = Vector[Iter->second];
      if (!P.hasValue())
        return ValueT();
      return P->second;
    }

    size_t count(const KeyT &Val) const { return Map.count(Val); }

    bool empty() const { return Map.empty(); }
  };

  template <typename KeyT, typename ValueT, unsigned N,
            typename MapT = llvm::SmallDenseMap<KeyT, size_t, N>,
            typename VectorT =
                llvm::SmallVector<Optional<std::pair<KeyT, ValueT>>, N>>
  class SmallBlotMapVector : public BlotMapVector<KeyT, ValueT, MapT, VectorT> {
public:
  SmallBlotMapVector() {}
};

} // end namespace swift

#endif // SWIFT_BASIC_BLOTMAPVECTOR_H
