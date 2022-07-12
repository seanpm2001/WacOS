//===--- Filter.swift -----------------------------------------*- swift -*-===//
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

/// An iterator over the elements traversed by some base iterator that also
/// satisfy a given predicate.
///
/// - Note: This is the associated `Iterator` of `LazyFilterSequence`
/// and `LazyFilterCollection`.
@_fixed_layout // FIXME(sil-serialize-all)
public struct LazyFilterIterator<
  Base : IteratorProtocol
> : IteratorProtocol, Sequence {
  /// Advances to the next element and returns it, or `nil` if no next element
  /// exists.
  ///
  /// Once `nil` has been returned, all subsequent calls return `nil`.
  ///
  /// - Precondition: `next()` has not been applied to a copy of `self`
  ///   since the copy was made.
  @_inlineable // FIXME(sil-serialize-all)
  public mutating func next() -> Base.Element? {
    while let n = _base.next() {
      if _predicate(n) {
        return n
      }
    }
    return nil
  }

  /// Creates an instance that produces the elements `x` of `base`
  /// for which `isIncluded(x) == true`.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned // FIXME(sil-serialize-all)
  internal init(
    _base: Base,
    _ isIncluded: @escaping (Base.Element) -> Bool
  ) {
    self._base = _base
    self._predicate = isIncluded
  }

  /// The underlying iterator whose elements are being filtered.
  public var base: Base { return _base }

  @_versioned // FIXME(sil-serialize-all)
  internal var _base: Base

  /// The predicate used to determine which elements produced by
  /// `base` are also produced by `self`.
  @_versioned // FIXME(sil-serialize-all)
  internal let _predicate: (Base.Element) -> Bool
}

/// A sequence whose elements consist of the elements of some base
/// sequence that also satisfy a given predicate.
///
/// - Note: `s.lazy.filter { ... }`, for an arbitrary sequence `s`,
///   is a `LazyFilterSequence`.
@_fixed_layout // FIXME(sil-serialize-all)
public struct LazyFilterSequence<Base : Sequence>
  : LazySequenceProtocol {

  /// Returns an iterator over the elements of this sequence.
  ///
  /// - Complexity: O(1).
  @_inlineable // FIXME(sil-serialize-all)
  public func makeIterator() -> LazyFilterIterator<Base.Iterator> {
    return LazyFilterIterator(
      _base: base.makeIterator(), _include)
  }

  @_inlineable
  public func _customContainsEquatableElement(
    _ element: Element
  ) -> Bool? {
    if !_include(element) {
      return false
    }
    if let baseContains = base._customContainsEquatableElement(element) {
      return baseContains
    }
    return nil
  }

  /// Creates an instance consisting of the elements `x` of `base` for
  /// which `isIncluded(x) == true`.
  @_inlineable // FIXME(sil-serialize-all)
  public // @testable
  init(
    _base base: Base,
    _ isIncluded: @escaping (Base.Element) -> Bool
  ) {
    self.base = base
    self._include = isIncluded
  }

  /// The underlying sequence whose elements are being filtered
  public let base: Base

  /// The predicate used to determine which elements of `base` are
  /// also elements of `self`.
  @_versioned // FIXME(sil-serialize-all)
  internal let _include: (Base.Element) -> Bool
}

/// The `Index` used for subscripting a `LazyFilterCollection`.
@available(swift, deprecated: 3.1, obsoleted: 4.0, message: "Use Base.Index")
public typealias LazyFilterIndex<Base : Collection> = Base.Index

/// A lazy `Collection` wrapper that includes the elements of an
/// underlying collection that satisfy a predicate.
///
/// - Note: The performance of accessing `startIndex`, `first`, any methods
///   that depend on `startIndex`, or of advancing an index depends
///   on how sparsely the filtering predicate is satisfied, and may not offer
///   the usual performance given by `Collection`. Be aware, therefore, that
///   general operations on `LazyFilterCollection` instances may not have the
///   documented complexity.
@_fixed_layout // FIXME(sil-serialize-all)
public struct LazyFilterCollection<Base : Collection> {
  @_versioned // FIXME(sil-serialize-all)
  internal var _base: Base
  @_versioned // FIXME(sil-serialize-all)
  internal let _predicate: (Base.Element) -> Bool

  /// Creates an instance containing the elements of `base` that
  /// satisfy `isIncluded`.
  @_inlineable // FIXME(sil-serialize-all)
  public // @testable
  init(
    _base: Base,
    _ isIncluded: @escaping (Base.Element) -> Bool
  ) {
    self._base = _base
    self._predicate = isIncluded
  }
}

extension LazyFilterCollection : Sequence {
  public typealias SubSequence = LazyFilterCollection<Base.SubSequence>
  public typealias Element = Base.Element

  // Any estimate of the number of elements that pass `_predicate` requires
  // iterating the collection and evaluating each element, which can be costly,
  // is unexpected, and usually doesn't pay for itself in saving time through
  // preventing intermediate reallocations. (SR-4164)
  @_inlineable // FIXME(sil-serialize-all)
  public var underestimatedCount: Int { return 0 }

  @_inlineable // FIXME(sil-serialize-all)
  public func _copyToContiguousArray()
    -> ContiguousArray<Base.Iterator.Element> {

    // The default implementation of `_copyToContiguousArray` queries the
    // `count` property, which evaluates `_predicate` for every element --
    // see the note above `underestimatedCount`. Here we treat `self` as a
    // sequence and only rely on underestimated count.
    return _copySequenceToContiguousArray(self)
  }

  /// Returns an iterator over the elements of this sequence.
  ///
  /// - Complexity: O(1).
  @_inlineable // FIXME(sil-serialize-all)
  public func makeIterator() -> LazyFilterIterator<Base.Iterator> {
    return LazyFilterIterator(
      _base: _base.makeIterator(), _predicate)
  }

  @_inlineable
  public func _customContainsEquatableElement(
    _ element: Element
  ) -> Bool? {
    if !_predicate(element) {
      return false
    }
    if let baseContains = _base._customContainsEquatableElement(element) {
      return baseContains
    }
    return nil
  }
}

extension LazyFilterCollection : LazyCollectionProtocol, Collection {
  /// A type that represents a valid position in the collection.
  ///
  /// Valid indices consist of the position of every element and a
  /// "past the end" position that's not valid for use as a subscript.
  public typealias Index = Base.Index

  /// The position of the first element in a non-empty collection.
  ///
  /// In an empty collection, `startIndex == endIndex`.
  ///
  /// - Complexity: O(*n*), where *n* is the ratio between unfiltered and
  ///   filtered collection counts.
  @_inlineable // FIXME(sil-serialize-all)
  public var startIndex: Index {
    var index = _base.startIndex
    while index != _base.endIndex && !_predicate(_base[index]) {
      _base.formIndex(after: &index)
    }
    return index
  }

  /// The collection's "past the end" position---that is, the position one
  /// greater than the last valid subscript argument.
  ///
  /// `endIndex` is always reachable from `startIndex` by zero or more
  /// applications of `index(after:)`.
  @_inlineable // FIXME(sil-serialize-all)
  public var endIndex: Index {
    return _base.endIndex
  }

  // TODO: swift-3-indexing-model - add docs
  @_inlineable // FIXME(sil-serialize-all)
  public func index(after i: Index) -> Index {
    var i = i
    formIndex(after: &i)
    return i
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func formIndex(after i: inout Index) {
    // TODO: swift-3-indexing-model: _failEarlyRangeCheck i?
    var index = i
    _precondition(index != _base.endIndex, "Can't advance past endIndex")
    repeat {
      _base.formIndex(after: &index)
    } while index != _base.endIndex && !_predicate(_base[index])
    i = index
  }

  @inline(__always)
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned // FIXME(sil-serialize-all)
  internal func _advanceIndex(_ i: inout Index, step: Int) {
      repeat {
        _base.formIndex(&i, offsetBy: step)
      } while i != _base.endIndex && !_predicate(_base[i])
  }

  @inline(__always)
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned // FIXME(sil-serialize-all)
  internal func _ensureBidirectional(step: Int) {
    // FIXME: This seems to be the best way of checking whether _base is
    // forward only without adding an extra protocol requirement.
    // index(_:offsetBy:limitedBy:) is chosen becuase it is supposed to return
    // nil when the resulting index lands outside the collection boundaries,
    // and therefore likely does not trap in these cases.
    if step < 0 {
      _ = _base.index(
        _base.endIndex, offsetBy: step, limitedBy: _base.startIndex)
    }
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func distance(from start: Index, to end: Index) -> Int {
    // The following line makes sure that distance(from:to:) is invoked on the
    // _base at least once, to trigger a _precondition in forward only
    // collections.
    _ = _base.distance(from: start, to: end)
    var _start: Index
    let _end: Index
    let step: Int
    if start > end {
      _start = end
      _end = start
      step = -1
    }
    else {
      _start = start
      _end = end
      step = 1
    }
    var count = 0
    while _start != _end {
      count += step
      formIndex(after: &_start)
    }
    return count
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func index(_ i: Index, offsetBy n: Int) -> Index {
    var i = i
    let step = n.signum()
    // The following line makes sure that index(_:offsetBy:) is invoked on the
    // _base at least once, to trigger a _precondition in forward only
    // collections.
    _ensureBidirectional(step: step)
    for _ in 0 ..< abs(numericCast(n)) {
      _advanceIndex(&i, step: step)
    }
    return i
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func formIndex(_ i: inout Index, offsetBy n: Int) {
    i = index(i, offsetBy: n)
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func index(
    _ i: Index, offsetBy n: Int, limitedBy limit: Index
  ) -> Index? {
    var i = i
    let step = n.signum()
    // The following line makes sure that index(_:offsetBy:limitedBy:) is
    // invoked on the _base at least once, to trigger a _precondition in
    // forward only collections.
    _ensureBidirectional(step: step)
    for _ in 0 ..< abs(numericCast(n)) {
      if i == limit {
        return nil
      }
      _advanceIndex(&i, step: step)
    }
    return i
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func formIndex(
    _ i: inout Index, offsetBy n: Int, limitedBy limit: Index
  ) -> Bool {
    if let advancedIndex = index(i, offsetBy: n, limitedBy: limit) {
      i = advancedIndex
      return true
    }
    i = limit
    return false
  }

  /// Accesses the element at `position`.
  ///
  /// - Precondition: `position` is a valid position in `self` and
  /// `position != endIndex`.
  @_inlineable // FIXME(sil-serialize-all)
  public subscript(position: Index) -> Base.Element {
    return _base[position]
  }

  @_inlineable // FIXME(sil-serialize-all)
  public subscript(bounds: Range<Index>) -> SubSequence {
    return SubSequence(_base: _base[bounds], _predicate)
  }
}

extension LazyFilterCollection : BidirectionalCollection
  where Base : BidirectionalCollection {

  @_inlineable // FIXME(sil-serialize-all)
  public func index(before i: Index) -> Index {
    var i = i
    formIndex(before: &i)
    return i
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func formIndex(before i: inout Index) {
    // TODO: swift-3-indexing-model: _failEarlyRangeCheck i?
    var index = i
    _precondition(index != _base.startIndex, "Can't retreat before startIndex")
    repeat {
      _base.formIndex(before: &index)
    } while !_predicate(_base[index])
    i = index
  }
}

extension LazySequenceProtocol {
  /// Returns the elements of `self` that satisfy `isIncluded`.
  ///
  /// - Note: The elements of the result are computed on-demand, as
  ///   the result is used. No buffering storage is allocated and each
  ///   traversal step invokes `predicate` on one or more underlying
  ///   elements.
  @_inlineable // FIXME(sil-serialize-all)
  public func filter(
    _ isIncluded: @escaping (Elements.Element) -> Bool
  ) -> LazyFilterSequence<Self.Elements> {
    return LazyFilterSequence(_base: self.elements, isIncluded)
  }
}

extension LazyCollectionProtocol {
  /// Returns the elements of `self` that satisfy `predicate`.
  ///
  /// - Note: The elements of the result are computed on-demand, as
  ///   the result is used. No buffering storage is allocated and each
  ///   traversal step invokes `predicate` on one or more underlying
  ///   elements.
  @_inlineable // FIXME(sil-serialize-all)
  public func filter(
    _ isIncluded: @escaping (Elements.Element) -> Bool
  ) -> LazyFilterCollection<Self.Elements> {
    return LazyFilterCollection(_base: self.elements, isIncluded)
  }
}

@available(*, deprecated, renamed: "LazyFilterCollection")
public typealias LazyFilterBidirectionalCollection<T> = LazyFilterCollection<T> where T : BidirectionalCollection
