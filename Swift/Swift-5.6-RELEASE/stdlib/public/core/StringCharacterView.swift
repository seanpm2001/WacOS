//===--- StringCharacterView.swift - String's Collection of Characters ----===//
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
//  String is-not-a Sequence or Collection, but it exposes a
//  collection of characters.
//
//===----------------------------------------------------------------------===//

// FIXME(ABI)#70 : The character string view should have a custom iterator type
// to allow performance optimizations of linear traversals.

import SwiftShims

extension String: BidirectionalCollection {
  /// A type that represents the number of steps between two `String.Index`
  /// values, where one value is reachable from the other.
  ///
  /// In Swift, *reachability* refers to the ability to produce one value from
  /// the other through zero or more applications of `index(after:)`.
  public typealias IndexDistance = Int

  public typealias SubSequence = Substring

  public typealias Element = Character

  /// The position of the first character in a nonempty string.
  ///
  /// In an empty string, `startIndex` is equal to `endIndex`.
  @inlinable @inline(__always)
  public var startIndex: Index { return _guts.startIndex }

  /// A string's "past the end" position---that is, the position one greater
  /// than the last valid subscript argument.
  ///
  /// In an empty string, `endIndex` is equal to `startIndex`.
  @inlinable @inline(__always)
  public var endIndex: Index { return _guts.endIndex }

  /// The number of characters in a string.
  @inline(__always)
  public var count: Int {
    return distance(from: startIndex, to: endIndex)
  }

  /// Returns the position immediately after the given index.
  ///
  /// - Parameter i: A valid index of the collection. `i` must be less than
  ///   `endIndex`.
  /// - Returns: The index value immediately after `i`.
  public func index(after i: Index) -> Index {
    _precondition(i < endIndex, "String index is out of bounds")

    // TODO: known-ASCII fast path, single-scalar-grapheme fast path, etc.
    let i = _guts.scalarAlign(i)
    let stride = _characterStride(startingAt: i)
    let nextOffset = i._encodedOffset &+ stride
    let nextStride = _characterStride(
      startingAt: Index(_encodedOffset: nextOffset)._scalarAligned)

    return Index(
      encodedOffset: nextOffset, characterStride: nextStride)._scalarAligned
  }

  /// Returns the position immediately before the given index.
  ///
  /// - Parameter i: A valid index of the collection. `i` must be greater than
  ///   `startIndex`.
  /// - Returns: The index value immediately before `i`.
  public func index(before i: Index) -> Index {
    _precondition(i > startIndex, "String index is out of bounds")

    // TODO: known-ASCII fast path, single-scalar-grapheme fast path, etc.
    let i = _guts.scalarAlign(i)
    let stride = _characterStride(endingAt: i)
    let priorOffset = i._encodedOffset &- stride
    return Index(
      encodedOffset: priorOffset, characterStride: stride)._scalarAligned
  }
  /// Returns an index that is the specified distance from the given index.
  ///
  /// The following example obtains an index advanced four positions from a
  /// string's starting index and then prints the character at that position.
  ///
  ///     let s = "Swift"
  ///     let i = s.index(s.startIndex, offsetBy: 4)
  ///     print(s[i])
  ///     // Prints "t"
  ///
  /// The value passed as `n` must not offset `i` beyond the bounds of the
  /// collection.
  ///
  /// - Parameters:
  ///   - i: A valid index of the collection.
  ///   - n: The distance to offset `i`.
  /// - Returns: An index offset by `n` from the index `i`. If `n` is positive,
  ///   this is the same value as the result of `n` calls to `index(after:)`.
  ///   If `n` is negative, this is the same value as the result of `-n` calls
  ///   to `index(before:)`.
  ///
  /// - Complexity: O(*n*), where *n* is the absolute value of `n`.
  @inlinable @inline(__always)
  public func index(_ i: Index, offsetBy n: IndexDistance) -> Index {
    // TODO: known-ASCII and single-scalar-grapheme fast path, etc.
    return _index(i, offsetBy: n)
  }

  /// Returns an index that is the specified distance from the given index,
  /// unless that distance is beyond a given limiting index.
  ///
  /// The following example obtains an index advanced four positions from a
  /// string's starting index and then prints the character at that position.
  /// The operation doesn't require going beyond the limiting `s.endIndex`
  /// value, so it succeeds.
  ///
  ///     let s = "Swift"
  ///     if let i = s.index(s.startIndex, offsetBy: 4, limitedBy: s.endIndex) {
  ///         print(s[i])
  ///     }
  ///     // Prints "t"
  ///
  /// The next example attempts to retrieve an index six positions from
  /// `s.startIndex` but fails, because that distance is beyond the index
  /// passed as `limit`.
  ///
  ///     let j = s.index(s.startIndex, offsetBy: 6, limitedBy: s.endIndex)
  ///     print(j)
  ///     // Prints "nil"
  ///
  /// The value passed as `n` must not offset `i` beyond the bounds of the
  /// collection, unless the index passed as `limit` prevents offsetting
  /// beyond those bounds.
  ///
  /// - Parameters:
  ///   - i: A valid index of the collection.
  ///   - n: The distance to offset `i`.
  ///   - limit: A valid index of the collection to use as a limit. If `n > 0`,
  ///     a limit that is less than `i` has no effect. Likewise, if `n < 0`, a
  ///     limit that is greater than `i` has no effect.
  /// - Returns: An index offset by `n` from the index `i`, unless that index
  ///   would be beyond `limit` in the direction of movement. In that case,
  ///   the method returns `nil`.
  ///
  /// - Complexity: O(*n*), where *n* is the absolute value of `n`.
  @inlinable @inline(__always)
  public func index(
    _ i: Index, offsetBy n: IndexDistance, limitedBy limit: Index
  ) -> Index? {
    // TODO: known-ASCII and single-scalar-grapheme fast path, etc.
    return _index(i, offsetBy: n, limitedBy: limit)
  }

  /// Returns the distance between two indices.
  ///
  /// - Parameters:
  ///   - start: A valid index of the collection.
  ///   - end: Another valid index of the collection. If `end` is equal to
  ///     `start`, the result is zero.
  /// - Returns: The distance between `start` and `end`.
  ///
  /// - Complexity: O(*n*), where *n* is the resulting distance.
  @inlinable @inline(__always)
  public func distance(from start: Index, to end: Index) -> IndexDistance {
    // TODO: known-ASCII and single-scalar-grapheme fast path, etc.
    return _distance(from: _guts.scalarAlign(start), to: _guts.scalarAlign(end))
  }

  /// Accesses the character at the given position.
  ///
  /// You can use the same indices for subscripting a string and its substring.
  /// For example, this code finds the first letter after the first space:
  ///
  ///     let str = "Greetings, friend! How are you?"
  ///     let firstSpace = str.firstIndex(of: " ") ?? str.endIndex
  ///     let substr = str[firstSpace...]
  ///     if let nextCapital = substr.firstIndex(where: { $0 >= "A" && $0 <= "Z" }) {
  ///         print("Capital after a space: \(str[nextCapital])")
  ///     }
  ///     // Prints "Capital after a space: H"
  ///
  /// - Parameter i: A valid index of the string. `i` must be less than the
  ///   string's end index.
  @inlinable @inline(__always)
  public subscript(i: Index) -> Character {
    _boundsCheck(i)

    let i = _guts.scalarAlign(i)
    let distance = _characterStride(startingAt: i)

    return _guts.errorCorrectedCharacter(
      startingAt: i._encodedOffset, endingAt: i._encodedOffset &+ distance)
  }

  @inlinable @inline(__always)
  internal func _characterStride(startingAt i: Index) -> Int {
    _internalInvariant_5_1(i._isScalarAligned)

    // Fast check if it's already been measured, otherwise check resiliently
    if let d = i.characterStride { return d }

    if i == endIndex { return 0 }

    return _guts._opaqueCharacterStride(startingAt: i._encodedOffset)
  }

  @inlinable @inline(__always)
  internal func _characterStride(endingAt i: Index) -> Int {
    _internalInvariant_5_1(i._isScalarAligned)

    if i == startIndex { return 0 }

    return _guts._opaqueCharacterStride(endingAt: i._encodedOffset)
  }
}

extension String {
  @frozen
  public struct Iterator: IteratorProtocol, Sendable {
    @usableFromInline
    internal var _guts: _StringGuts

    @usableFromInline
    internal var _position: Int = 0

    @usableFromInline
    internal var _end: Int

    @inlinable
    internal init(_ guts: _StringGuts) {
      self._end = guts.count
      self._guts = guts
    }

    @inlinable
    public mutating func next() -> Character? {
      guard _fastPath(_position < _end) else { return nil }

      let len = _guts._opaqueCharacterStride(startingAt: _position)
      let nextPosition = _position &+ len
      let result = _guts.errorCorrectedCharacter(
        startingAt: _position, endingAt: nextPosition)
      _position = nextPosition
      return result
    }
  }

  @inlinable
  public __consuming func makeIterator() -> Iterator {
    return Iterator(_guts)
  }
}

