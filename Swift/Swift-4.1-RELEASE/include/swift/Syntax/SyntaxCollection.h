//===--- SyntaxCollection.h -------------------------------------*- C++ -*-===//
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

#ifndef SWIFT_SYNTAX_SYNTAXCOLLECTION_H
#define SWIFT_SYNTAX_SYNTAXCOLLECTION_H

#include "swift/Syntax/Syntax.h"

namespace swift {
namespace syntax {

template <SyntaxKind CollectionKind, typename Element>
class SyntaxCollection;

template <SyntaxKind CollectionKind, typename Element>
struct SyntaxCollectionIterator {
  const SyntaxCollection<CollectionKind, Element> &Collection;
  size_t Index;

  Element operator*() {
    return Collection[Index];
  }

  SyntaxCollectionIterator<CollectionKind, Element> &operator++() {
    ++Index;
    return *this;
  }

  bool
  operator==(const SyntaxCollectionIterator<CollectionKind, Element> &Other) {
    return Collection.hasSameIdentityAs(Other.Collection) &&
    Index == Other.Index;
  }

  bool
  operator!=(const SyntaxCollectionIterator<CollectionKind, Element> &Other) {
    return !operator==(Other);

  }
};

/// A generic unbounded collection of syntax nodes
template <SyntaxKind CollectionKind, typename Element>
class SyntaxCollection : public Syntax {
  friend struct SyntaxFactory;
  friend class Syntax;

private:
  static RC<SyntaxData>
  makeData(std::initializer_list<Element> &Elements) {
    RawSyntax::LayoutList List;
    for (auto &Elt : Elements) {
      List.push_back(Elt.getRaw());
    }
    auto Raw = RawSyntax::make(CollectionKind, List,
                               SourcePresence::Present);
    return SyntaxData::make(Raw);
  }
  SyntaxCollection(const RC<SyntaxData> Root): Syntax(Root, Root.get()) {}

public:

  SyntaxCollection(const RC<SyntaxData> Root, const SyntaxData *Data)
  : Syntax(Root, Data) {}

  SyntaxCollection(std::initializer_list<Element> list):
    SyntaxCollection(SyntaxCollection::makeData(list)) {}

  /// Returns true if the collection is empty.
  bool empty() const {
    return size() == 0;
  }

  /// Returns the number of elements in the collection.
  size_t size() const {
    return getRaw()->Layout.size();
  }

  SyntaxCollectionIterator<CollectionKind, Element> begin() const {
    return SyntaxCollectionIterator<CollectionKind, Element> {
      *this,
      0,
    };
  }

  SyntaxCollectionIterator<CollectionKind, Element> end() const {
    return SyntaxCollectionIterator<CollectionKind, Element> {
      *this,
      getRaw()->Layout.size(),
    };
  }

  /// Return the element at the given Index.
  ///
  /// Precondition: Index < size()
  /// Precondition: !empty()
  Element operator[](const size_t Index) const {
    assert(Index < size());
    assert(!empty());

    auto ChildData = Data->getChild(Index);
    return Element { Root, ChildData.get() };
  }

  /// Return a new collection with the given element added to the end.
  SyntaxCollection<CollectionKind, Element>
  appending(Element E) const {
    auto NewLayout = getRaw()->Layout;
    NewLayout.push_back(E.getRaw());
    auto Raw = RawSyntax::make(CollectionKind, NewLayout, getRaw()->Presence);
    return Data->replaceSelf<SyntaxCollection<CollectionKind, Element>>(Raw);
  }

  /// Return a new collection with an element removed from the end.
  ///
  /// Precondition: !empty()
  SyntaxCollection<CollectionKind, Element> removingLast() const {
    assert(!empty());
    auto NewLayout = getRaw()->Layout;
    NewLayout.pop_back();
    auto Raw = RawSyntax::make(CollectionKind, NewLayout, getRaw()->Presence);
    return Data->replaceSelf<SyntaxCollection<CollectionKind, Element>>(Raw);
  }

  /// Return a new collection with the given element appended to the front.
  SyntaxCollection<CollectionKind, Element>
  prepending(Element E) const {
    RawSyntax::LayoutList NewLayout = { E.getRaw() };
    std::copy(getRaw()->Layout.begin(),
              getRaw()->Layout.end(),
              std::back_inserter(NewLayout));
    auto Raw = RawSyntax::make(CollectionKind, NewLayout, getRaw()->Presence);
    return Data->replaceSelf<SyntaxCollection<CollectionKind, Element>>(Raw);
  }

  /// Return a new collection with an element removed from the end.
  ///
  /// Precondition: !empty()
  SyntaxCollection<CollectionKind, Element> removingFirst() const {
    assert(!empty());
    RawSyntax::LayoutList NewLayout;
    std::copy(getRaw()->Layout.begin() + 1,
              getRaw()->Layout.end(),
              std::back_inserter(NewLayout));
    auto Raw = RawSyntax::make(CollectionKind, NewLayout, getRaw()->Presence);
    return Data->replaceSelf<SyntaxCollection<CollectionKind, Element>>(Raw);
  }

  /// Return a new collection with the Element inserted at index i.
  ///
  /// Precondition: i <= size()
  SyntaxCollection<CollectionKind, Element>
  inserting(size_t i, Element E) const {
    assert(i <= size());
    RawSyntax::LayoutList NewLayout;
    std::copy(getRaw()->Layout.begin(), getRaw()->Layout.begin() + i,
              std::back_inserter(NewLayout));
    NewLayout.push_back(E.getRaw());
    std::copy(getRaw()->Layout.begin() + i, getRaw()->Layout.end(),
              std::back_inserter(NewLayout));
    auto Raw = RawSyntax::make(CollectionKind, NewLayout, getRaw()->Presence);
    return Data->replaceSelf<SyntaxCollection<CollectionKind, Element>>(Raw);
  }

  /// Return a new collection with the element removed at index i.
  SyntaxCollection<CollectionKind, Element> removing(size_t i) const {
    auto NewLayout = getRaw()->Layout;
    NewLayout.erase(NewLayout.begin() + i);
    auto Raw = RawSyntax::make(CollectionKind, NewLayout, getRaw()->Presence);
    return Data->replaceSelf<SyntaxCollection<CollectionKind, Element>>(Raw);
  }

  /// Return an empty syntax collection of this type.
  SyntaxCollection<CollectionKind, Element> cleared() const {
    auto Raw = RawSyntax::make(CollectionKind, {}, getRaw()->Presence);
    return Data->replaceSelf<SyntaxCollection<CollectionKind, Element>>(Raw);
  }

  static bool classof(const Syntax *S) {
    return S->getKind() == CollectionKind;
  }
};

} // end namespace syntax
} // end namespace swift

#endif // SWIFT_SYNTAX_SYNTAXCOLLECTION_H
