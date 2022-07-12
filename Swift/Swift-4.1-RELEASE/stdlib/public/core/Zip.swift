//===----------------------------------------------------------------------===//
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

/// Creates a sequence of pairs built out of two underlying sequences.
///
/// In the `Zip2Sequence` instance returned by this function, the elements of
/// the *i*th pair are the *i*th elements of each underlying sequence. The
/// following example uses the `zip(_:_:)` function to iterate over an array
/// of strings and a countable range at the same time:
///
///     let words = ["one", "two", "three", "four"]
///     let numbers = 1...4
///
///     for (word, number) in zip(words, numbers) {
///         print("\(word): \(number)")
///     }
///     // Prints "one: 1"
///     // Prints "two: 2
///     // Prints "three: 3"
///     // Prints "four: 4"
///
/// If the two sequences passed to `zip(_:_:)` are different lengths, the
/// resulting sequence is the same length as the shorter sequence. In this
/// example, the resulting array is the same length as `words`:
///
///     let naturalNumbers = 1...Int.max
///     let zipped = Array(zip(words, naturalNumbers))
///     // zipped == [("one", 1), ("two", 2), ("three", 3), ("four", 4)]
///
/// - Parameters:
///   - sequence1: The first sequence or collection to zip.
///   - sequence2: The second sequence or collection to zip.
/// - Returns: A sequence of tuple pairs, where the elements of each pair are
///   corresponding elements of `sequence1` and `sequence2`.
@_inlineable // FIXME(sil-serialize-all)
public func zip<Sequence1, Sequence2>(
  _ sequence1: Sequence1, _ sequence2: Sequence2
) -> Zip2Sequence<Sequence1, Sequence2> {
  return Zip2Sequence(_sequence1: sequence1, _sequence2: sequence2)
}

/// An iterator for `Zip2Sequence`.
@_fixed_layout // FIXME(sil-serialize-all)
public struct Zip2Iterator<Iterator1: IteratorProtocol, Iterator2: IteratorProtocol> {
  /// The type of element returned by `next()`.
  public typealias Element = (Iterator1.Element, Iterator2.Element)

  @_versioned // FIXME(sil-serialize-all)
  internal var _baseStream1: Iterator1
  @_versioned // FIXME(sil-serialize-all)
  internal var _baseStream2: Iterator2
  @_versioned // FIXME(sil-serialize-all)
  internal var _reachedEnd: Bool = false

  /// Creates an instance around a pair of underlying iterators.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned // FIXME(sil-serialize-all)
  internal init(_ iterator1: Iterator1, _ iterator2: Iterator2) {
    (_baseStream1, _baseStream2) = (iterator1, iterator2)
  }
}

extension Zip2Iterator: IteratorProtocol {
  /// Advances to the next element and returns it, or `nil` if no next element
  /// exists.
  ///
  /// Once `nil` has been returned, all subsequent calls return `nil`.
  @_inlineable // FIXME(sil-serialize-all)
  public mutating func next() -> Element? {
    // The next() function needs to track if it has reached the end.  If we
    // didn't, and the first sequence is longer than the second, then when we
    // have already exhausted the second sequence, on every subsequent call to
    // next() we would consume and discard one additional element from the
    // first sequence, even though next() had already returned nil.

    if _reachedEnd {
      return nil
    }

    guard let element1 = _baseStream1.next(),
          let element2 = _baseStream2.next() else {
      _reachedEnd = true
      return nil
    }

    return (element1, element2)
  }
}

/// A sequence of pairs built out of two underlying sequences.
///
/// In a `Zip2Sequence` instance, the elements of the *i*th pair are the *i*th
/// elements of each underlying sequence. To create a `Zip2Sequence` instance,
/// use the `zip(_:_:)` function.
///
/// The following example uses the `zip(_:_:)` function to iterate over an
/// array of strings and a countable range at the same time:
///
///     let words = ["one", "two", "three", "four"]
///     let numbers = 1...4
///
///     for (word, number) in zip(words, numbers) {
///         print("\(word): \(number)")
///     }
///     // Prints "one: 1"
///     // Prints "two: 2
///     // Prints "three: 3"
///     // Prints "four: 4"
@_fixed_layout // FIXME(sil-serialize-all)
public struct Zip2Sequence<Sequence1 : Sequence, Sequence2 : Sequence> {
  @_versioned // FIXME(sil-serialize-all)
  internal let _sequence1: Sequence1
  @_versioned // FIXME(sil-serialize-all)
  internal let _sequence2: Sequence2

  @available(*, deprecated, renamed: "Sequence1.Iterator")
  public typealias Stream1 = Sequence1.Iterator
  @available(*, deprecated, renamed: "Sequence2.Iterator")
  public typealias Stream2 = Sequence2.Iterator

  /// Creates an instance that makes pairs of elements from `sequence1` and
  /// `sequence2`.
  @_inlineable // FIXME(sil-serialize-all)
  public // @testable
  init(_sequence1 sequence1: Sequence1, _sequence2 sequence2: Sequence2) {
    (_sequence1, _sequence2) = (sequence1, sequence2)
  }
}

extension Zip2Sequence: Sequence {
  /// A type whose instances can produce the elements of this
  /// sequence, in order.
  public typealias Iterator = Zip2Iterator<Sequence1.Iterator, Sequence2.Iterator>

  /// Returns an iterator over the elements of this sequence.
  @_inlineable // FIXME(sil-serialize-all)
  public func makeIterator() -> Iterator {
    return Iterator(
      _sequence1.makeIterator(),
      _sequence2.makeIterator())
  }
}
