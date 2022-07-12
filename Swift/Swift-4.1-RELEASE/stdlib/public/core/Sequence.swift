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

/// A type that supplies the values of a sequence one at a time.
///
/// The `IteratorProtocol` protocol is tightly linked with the `Sequence`
/// protocol. Sequences provide access to their elements by creating an
/// iterator, which keeps track of its iteration process and returns one
/// element at a time as it advances through the sequence.
///
/// Whenever you use a `for`-`in` loop with an array, set, or any other
/// collection or sequence, you're using that type's iterator. Swift uses a
/// sequence's or collection's iterator internally to enable the `for`-`in`
/// loop language construct.
///
/// Using a sequence's iterator directly gives you access to the same elements
/// in the same order as iterating over that sequence using a `for`-`in` loop.
/// For example, you might typically use a `for`-`in` loop to print each of
/// the elements in an array.
///
///     let animals = ["Antelope", "Butterfly", "Camel", "Dolphin"]
///     for animal in animals {
///         print(animal)
///     }
///     // Prints "Antelope"
///     // Prints "Butterfly"
///     // Prints "Camel"
///     // Prints "Dolphin"
///
/// Behind the scenes, Swift uses the `animals` array's iterator to loop over
/// the contents of the array.
///
///     var animalIterator = animals.makeIterator()
///     while let animal = animalIterator.next() {
///         print(animal)
///     }
///     // Prints "Antelope"
///     // Prints "Butterfly"
///     // Prints "Camel"
///     // Prints "Dolphin"
///
/// The call to `animals.makeIterator()` returns an instance of the array's
/// iterator. Next, the `while` loop calls the iterator's `next()` method
/// repeatedly, binding each element that is returned to `animal` and exiting
/// when the `next()` method returns `nil`.
///
/// Using Iterators Directly
/// ========================
///
/// You rarely need to use iterators directly, because a `for`-`in` loop is the
/// more idiomatic approach to traversing a sequence in Swift. Some
/// algorithms, however, may call for direct iterator use.
///
/// One example is the `reduce1(_:)` method. Similar to the `reduce(_:_:)`
/// method defined in the standard library, which takes an initial value and a
/// combining closure, `reduce1(_:)` uses the first element of the sequence as
/// the initial value.
///
/// Here's an implementation of the `reduce1(_:)` method. The sequence's
/// iterator is used directly to retrieve the initial value before looping
/// over the rest of the sequence.
///
///     extension Sequence {
///         func reduce1(
///             _ nextPartialResult: (Element, Element) -> Element
///         ) -> Element?
///         {
///             var i = makeIterator()
///             guard var accumulated = i.next() else {
///                 return nil
///             }
///
///             while let element = i.next() {
///                 accumulated = nextPartialResult(accumulated, element)
///             }
///             return accumulated
///         }
///     }
///
/// The `reduce1(_:)` method makes certain kinds of sequence operations
/// simpler. Here's how to find the longest string in a sequence, using the
/// `animals` array introduced earlier as an example:
///
///     let longestAnimal = animals.reduce1 { current, element in
///         if current.count > element.count {
///             return current
///         } else {
///             return element
///         }
///     }
///     print(longestAnimal)
///     // Prints "Butterfly"
///
/// Using Multiple Iterators
/// ========================
///
/// Whenever you use multiple iterators (or `for`-`in` loops) over a single
/// sequence, be sure you know that the specific sequence supports repeated
/// iteration, either because you know its concrete type or because the
/// sequence is also constrained to the `Collection` protocol.
///
/// Obtain each separate iterator from separate calls to the sequence's
/// `makeIterator()` method rather than by copying. Copying an iterator is
/// safe, but advancing one copy of an iterator by calling its `next()` method
/// may invalidate other copies of that iterator. `for`-`in` loops are safe in
/// this regard.
///
/// Adding IteratorProtocol Conformance to Your Type
/// ================================================
///
/// Implementing an iterator that conforms to `IteratorProtocol` is simple.
/// Declare a `next()` method that advances one step in the related sequence
/// and returns the current element. When the sequence has been exhausted, the
/// `next()` method returns `nil`.
///
/// For example, consider a custom `Countdown` sequence. You can initialize the
/// `Countdown` sequence with a starting integer and then iterate over the
/// count down to zero. The `Countdown` structure's definition is short: It
/// contains only the starting count and the `makeIterator()` method required
/// by the `Sequence` protocol.
///
///     struct Countdown: Sequence {
///         let start: Int
///
///         func makeIterator() -> CountdownIterator {
///             return CountdownIterator(self)
///         }
///     }
///
/// The `makeIterator()` method returns another custom type, an iterator named
/// `CountdownIterator`. The `CountdownIterator` type keeps track of both the
/// `Countdown` sequence that it's iterating and the number of times it has
/// returned a value.
///
///     struct CountdownIterator: IteratorProtocol {
///         let countdown: Countdown
///         var times = 0
///
///         init(_ countdown: Countdown) {
///             self.countdown = countdown
///         }
///
///         mutating func next() -> Int? {
///             let nextNumber = countdown.start - times
///             guard nextNumber > 0
///                 else { return nil }
///
///             times += 1
///             return nextNumber
///         }
///     }
///
/// Each time the `next()` method is called on a `CountdownIterator` instance,
/// it calculates the new next value, checks to see whether it has reached
/// zero, and then returns either the number, or `nil` if the iterator is
/// finished returning elements of the sequence.
///
/// Creating and iterating over a `Countdown` sequence uses a
/// `CountdownIterator` to handle the iteration.
///
///     let threeTwoOne = Countdown(start: 3)
///     for count in threeTwoOne {
///         print("\(count)...")
///     }
///     // Prints "3..."
///     // Prints "2..."
///     // Prints "1..."
public protocol IteratorProtocol {
  /// The type of element traversed by the iterator.
  associatedtype Element

  /// Advances to the next element and returns it, or `nil` if no next element
  /// exists.
  ///
  /// Repeatedly calling this method returns, in order, all the elements of the
  /// underlying sequence. As soon as the sequence has run out of elements, all
  /// subsequent calls return `nil`.
  ///
  /// You must not call this method if any other copy of this iterator has been
  /// advanced with a call to its `next()` method.
  ///
  /// The following example shows how an iterator can be used explicitly to
  /// emulate a `for`-`in` loop. First, retrieve a sequence's iterator, and
  /// then call the iterator's `next()` method until it returns `nil`.
  ///
  ///     let numbers = [2, 3, 5, 7]
  ///     var numbersIterator = numbers.makeIterator()
  ///
  ///     while let num = numbersIterator.next() {
  ///         print(num)
  ///     }
  ///     // Prints "2"
  ///     // Prints "3"
  ///     // Prints "5"
  ///     // Prints "7"
  ///
  /// - Returns: The next element in the underlying sequence, if a next element
  ///   exists; otherwise, `nil`.
  mutating func next() -> Element?
}

/// A type that provides sequential, iterated access to its elements.
///
/// A sequence is a list of values that you can step through one at a time. The
/// most common way to iterate over the elements of a sequence is to use a
/// `for`-`in` loop:
///
///     let oneTwoThree = 1...3
///     for number in oneTwoThree {
///         print(number)
///     }
///     // Prints "1"
///     // Prints "2"
///     // Prints "3"
///
/// While seemingly simple, this capability gives you access to a large number
/// of operations that you can perform on any sequence. As an example, to
/// check whether a sequence includes a particular value, you can test each
/// value sequentially until you've found a match or reached the end of the
/// sequence. This example checks to see whether a particular insect is in an
/// array.
///
///     let bugs = ["Aphid", "Bumblebee", "Cicada", "Damselfly", "Earwig"]
///     var hasMosquito = false
///     for bug in bugs {
///         if bug == "Mosquito" {
///             hasMosquito = true
///             break
///         }
///     }
///     print("'bugs' has a mosquito: \(hasMosquito)")
///     // Prints "'bugs' has a mosquito: false"
///
/// The `Sequence` protocol provides default implementations for many common
/// operations that depend on sequential access to a sequence's values. For
/// clearer, more concise code, the example above could use the array's
/// `contains(_:)` method, which every sequence inherits from `Sequence`,
/// instead of iterating manually:
///
///     if bugs.contains("Mosquito") {
///         print("Break out the bug spray.")
///     } else {
///         print("Whew, no mosquitos!")
///     }
///     // Prints "Whew, no mosquitos!"
///
/// Repeated Access
/// ===============
///
/// The `Sequence` protocol makes no requirement on conforming types regarding
/// whether they will be destructively consumed by iteration. As a
/// consequence, don't assume that multiple `for`-`in` loops on a sequence
/// will either resume iteration or restart from the beginning:
///
///     for element in sequence {
///         if ... some condition { break }
///     }
///
///     for element in sequence {
///         // No defined behavior
///     }
///
/// In this case, you cannot assume either that a sequence will be consumable
/// and will resume iteration, or that a sequence is a collection and will
/// restart iteration from the first element. A conforming sequence that is
/// not a collection is allowed to produce an arbitrary sequence of elements
/// in the second `for`-`in` loop.
///
/// To establish that a type you've created supports nondestructive iteration,
/// add conformance to the `Collection` protocol.
///
/// Conforming to the Sequence Protocol
/// ===================================
///
/// Making your own custom types conform to `Sequence` enables many useful
/// operations, like `for`-`in` looping and the `contains` method, without
/// much effort. To add `Sequence` conformance to your own custom type, add a
/// `makeIterator()` method that returns an iterator.
///
/// Alternatively, if your type can act as its own iterator, implementing the
/// requirements of the `IteratorProtocol` protocol and declaring conformance
/// to both `Sequence` and `IteratorProtocol` are sufficient.
///
/// Here's a definition of a `Countdown` sequence that serves as its own
/// iterator. The `makeIterator()` method is provided as a default
/// implementation.
///
///     struct Countdown: Sequence, IteratorProtocol {
///         var count: Int
///
///         mutating func next() -> Int? {
///             if count == 0 {
///                 return nil
///             } else {
///                 defer { count -= 1 }
///                 return count
///             }
///         }
///     }
///
///     let threeToGo = Countdown(count: 3)
///     for i in threeToGo {
///         print(i)
///     }
///     // Prints "3"
///     // Prints "2"
///     // Prints "1"
///
/// Expected Performance
/// ====================
///
/// A sequence should provide its iterator in O(1). The `Sequence` protocol
/// makes no other requirements about element access, so routines that
/// traverse a sequence should be considered O(*n*) unless documented
/// otherwise.
public protocol Sequence {
  /// A type representing the sequence's elements.
  associatedtype Element

  /// A type that provides the sequence's iteration interface and
  /// encapsulates its iteration state.
  associatedtype Iterator : IteratorProtocol where Iterator.Element == Element

  /// A type that represents a subsequence of some of the sequence's elements.
  associatedtype SubSequence : Sequence = AnySequence<Element>
    where Element == SubSequence.Element,
          SubSequence.SubSequence == SubSequence

  /// Returns an iterator over the elements of this sequence.
  func makeIterator() -> Iterator

  /// A value less than or equal to the number of elements in
  /// the sequence, calculated nondestructively.
  ///
  /// - Complexity: O(1)
  var underestimatedCount: Int { get }

  /// Returns an array containing the results of mapping the given closure
  /// over the sequence's elements.
  ///
  /// In this example, `map` is used first to convert the names in the array
  /// to lowercase strings and then to count their characters.
  ///
  ///     let cast = ["Vivien", "Marlon", "Kim", "Karl"]
  ///     let lowercaseNames = cast.map { $0.lowercased() }
  ///     // 'lowercaseNames' == ["vivien", "marlon", "kim", "karl"]
  ///     let letterCounts = cast.map { $0.count }
  ///     // 'letterCounts' == [6, 6, 3, 4]
  ///
  /// - Parameter transform: A mapping closure. `transform` accepts an
  ///   element of this sequence as its parameter and returns a transformed
  ///   value of the same or of a different type.
  /// - Returns: An array containing the transformed elements of this
  ///   sequence.
  func map<T>(
    _ transform: (Element) throws -> T
  ) rethrows -> [T]

  /// Returns an array containing, in order, the elements of the sequence
  /// that satisfy the given predicate.
  ///
  /// In this example, `filter(_:)` is used to include only names shorter than
  /// five characters.
  ///
  ///     let cast = ["Vivien", "Marlon", "Kim", "Karl"]
  ///     let shortNames = cast.filter { $0.count < 5 }
  ///     print(shortNames)
  ///     // Prints "["Kim", "Karl"]"
  ///
  /// - Parameter isIncluded: A closure that takes an element of the
  ///   sequence as its argument and returns a Boolean value indicating
  ///   whether the element should be included in the returned array.
  /// - Returns: An array of the elements that `isIncluded` allowed.
  func filter(
    _ isIncluded: (Element) throws -> Bool
  ) rethrows -> [Element]

  /// Calls the given closure on each element in the sequence in the same order
  /// as a `for`-`in` loop.
  ///
  /// The two loops in the following example produce the same output:
  ///
  ///     let numberWords = ["one", "two", "three"]
  ///     for word in numberWords {
  ///         print(word)
  ///     }
  ///     // Prints "one"
  ///     // Prints "two"
  ///     // Prints "three"
  ///
  ///     numberWords.forEach { word in
  ///         print(word)
  ///     }
  ///     // Same as above
  ///
  /// Using the `forEach` method is distinct from a `for`-`in` loop in two
  /// important ways:
  ///
  /// 1. You cannot use a `break` or `continue` statement to exit the current
  ///    call of the `body` closure or skip subsequent calls.
  /// 2. Using the `return` statement in the `body` closure will exit only from
  ///    the current call to `body`, not from any outer scope, and won't skip
  ///    subsequent calls.
  ///
  /// - Parameter body: A closure that takes an element of the sequence as a
  ///   parameter.
  func forEach(_ body: (Element) throws -> Void) rethrows

  // Note: The complexity of Sequence.dropFirst(_:) requirement
  // is documented as O(n) because Collection.dropFirst(_:) is
  // implemented in O(n), even though the default
  // implementation for Sequence.dropFirst(_:) is O(1).
  /// Returns a subsequence containing all but the given number of initial
  /// elements.
  ///
  /// If the number of elements to drop exceeds the number of elements in
  /// the sequence, the result is an empty subsequence.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.dropFirst(2))
  ///     // Prints "[3, 4, 5]"
  ///     print(numbers.dropFirst(10))
  ///     // Prints "[]"
  ///
  /// - Parameter n: The number of elements to drop from the beginning of
  ///   the sequence. `n` must be greater than or equal to zero.
  /// - Returns: A subsequence starting after the specified number of
  ///   elements.
  ///
  /// - Complexity: O(*n*), where *n* is the number of elements to drop from
  ///   the beginning of the sequence.
  func dropFirst(_ n: Int) -> SubSequence

  /// Returns a subsequence containing all but the specified number of final
  /// elements.
  ///
  /// The sequence must be finite. If the number of elements to drop exceeds
  /// the number of elements in the sequence, the result is an empty
  /// subsequence.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.dropLast(2))
  ///     // Prints "[1, 2, 3]"
  ///     print(numbers.dropLast(10))
  ///     // Prints "[]"
  ///
  /// - Parameter n: The number of elements to drop off the end of the
  ///   sequence. `n` must be greater than or equal to zero.
  /// - Returns: A subsequence leaving off the specified number of elements.
  ///
  /// - Complexity: O(*n*), where *n* is the length of the sequence.
  func dropLast(_ n: Int) -> SubSequence

  /// Returns a subsequence by skipping elements while `predicate` returns
  /// `true` and returning the remaining elements.
  ///
  /// - Parameter predicate: A closure that takes an element of the
  ///   sequence as its argument and returns a Boolean value indicating
  ///   whether the element is a match.
  ///
  /// - Complexity: O(*n*), where *n* is the length of the collection.
  func drop(
    while predicate: (Element) throws -> Bool
  ) rethrows -> SubSequence

  /// Returns a subsequence, up to the specified maximum length, containing
  /// the initial elements of the sequence.
  ///
  /// If the maximum length exceeds the number of elements in the sequence,
  /// the result contains all the elements in the sequence.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.prefix(2))
  ///     // Prints "[1, 2]"
  ///     print(numbers.prefix(10))
  ///     // Prints "[1, 2, 3, 4, 5]"
  ///
  /// - Parameter maxLength: The maximum number of elements to return.
  ///   `maxLength` must be greater than or equal to zero.
  /// - Returns: A subsequence starting at the beginning of this sequence
  ///   with at most `maxLength` elements.
  func prefix(_ maxLength: Int) -> SubSequence

  /// Returns a subsequence containing the initial, consecutive elements that
  /// satisfy the given predicate.
  ///
  /// The following example uses the `prefix(while:)` method to find the
  /// positive numbers at the beginning of the `numbers` array. Every element
  /// of `numbers` up to, but not including, the first negative value is
  /// included in the result.
  ///
  ///     let numbers = [3, 7, 4, -2, 9, -6, 10, 1]
  ///     let positivePrefix = numbers.prefix(while: { $0 > 0 })
  ///     // positivePrefix == [3, 7, 4]
  ///
  /// If `predicate` matches every element in the sequence, the resulting
  /// sequence contains every element of the sequence.
  ///
  /// - Parameter predicate: A closure that takes an element of the sequence as
  ///   its argument and returns a Boolean value indicating whether the
  ///   element should be included in the result.
  /// - Returns: A subsequence of the initial, consecutive elements that
  ///   satisfy `predicate`.
  ///
  /// - Complexity: O(*n*), where *n* is the length of the collection.
  func prefix(
    while predicate: (Element) throws -> Bool
  ) rethrows -> SubSequence

  /// Returns a subsequence, up to the given maximum length, containing the
  /// final elements of the sequence.
  ///
  /// The sequence must be finite. If the maximum length exceeds the number
  /// of elements in the sequence, the result contains all the elements in
  /// the sequence.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.suffix(2))
  ///     // Prints "[4, 5]"
  ///     print(numbers.suffix(10))
  ///     // Prints "[1, 2, 3, 4, 5]"
  ///
  /// - Parameter maxLength: The maximum number of elements to return. The
  ///   value of `maxLength` must be greater than or equal to zero.
  /// - Returns: A subsequence terminating at the end of this sequence with
  ///   at most `maxLength` elements.
  ///
  /// - Complexity: O(*n*), where *n* is the length of the sequence.
  func suffix(_ maxLength: Int) -> SubSequence

  /// Returns the longest possible subsequences of the sequence, in order, that
  /// don't contain elements satisfying the given predicate.
  ///
  /// The resulting array consists of at most `maxSplits + 1` subsequences.
  /// Elements that are used to split the sequence are not returned as part of
  /// any subsequence.
  ///
  /// The following examples show the effects of the `maxSplits` and
  /// `omittingEmptySubsequences` parameters when splitting a string using a
  /// closure that matches spaces. The first use of `split` returns each word
  /// that was originally separated by one or more spaces.
  ///
  ///     let line = "BLANCHE:   I don't want realism. I want magic!"
  ///     print(line.split(whereSeparator: { $0 == " " })
  ///               .map(String.init))
  ///     // Prints "["BLANCHE:", "I", "don\'t", "want", "realism.", "I", "want", "magic!"]"
  ///
  /// The second example passes `1` for the `maxSplits` parameter, so the
  /// original string is split just once, into two new strings.
  ///
  ///     print(
  ///         line.split(maxSplits: 1, whereSeparator: { $0 == " " })
  ///             .map(String.init))
  ///     // Prints "["BLANCHE:", "  I don\'t want realism. I want magic!"]"
  ///
  /// The final example passes `false` for the `omittingEmptySubsequences`
  /// parameter, so the returned array contains empty strings where spaces
  /// were repeated.
  ///
  ///     print(line.split(omittingEmptySubsequences: false,
  ///                      whereSeparator: { $0 == " " })
  ///          ).map(String.init))
  ///     // Prints "["BLANCHE:", "", "", "I", "don\'t", "want", "realism.", "I", "want", "magic!"]"
  ///
  /// - Parameters:
  ///   - maxSplits: The maximum number of times to split the sequence, or one
  ///     less than the number of subsequences to return. If `maxSplits + 1`
  ///     subsequences are returned, the last one is a suffix of the original
  ///     sequence containing the remaining elements. `maxSplits` must be
  ///     greater than or equal to zero. The default value is `Int.max`.
  ///   - omittingEmptySubsequences: If `false`, an empty subsequence is
  ///     returned in the result for each pair of consecutive elements
  ///     satisfying the `isSeparator` predicate and for each element at the
  ///     start or end of the sequence satisfying the `isSeparator` predicate.
  ///     If `true`, only nonempty subsequences are returned. The default
  ///     value is `true`.
  ///   - isSeparator: A closure that returns `true` if its argument should be
  ///     used to split the sequence; otherwise, `false`.
  /// - Returns: An array of subsequences, split from this sequence's elements.
  func split(
    maxSplits: Int, omittingEmptySubsequences: Bool,
    whereSeparator isSeparator: (Element) throws -> Bool
  ) rethrows -> [SubSequence]

  func _customContainsEquatableElement(
    _ element: Element
  ) -> Bool?

  /// If `self` is multi-pass (i.e., a `Collection`), invoke `preprocess` and
  /// return its result.  Otherwise, return `nil`.
  func _preprocessingPass<R>(
    _ preprocess: () throws -> R
  ) rethrows -> R?

  /// Create a native array buffer containing the elements of `self`,
  /// in the same order.
  func _copyToContiguousArray() -> ContiguousArray<Element>

  /// Copy `self` into an unsafe buffer, returning a partially-consumed
  /// iterator with any elements that didn't fit remaining.
  func _copyContents(
    initializing ptr: UnsafeMutableBufferPointer<Element>
  ) -> (Iterator,UnsafeMutableBufferPointer<Element>.Index)
}

// Provides a default associated type witness for Iterator when the
// Self type is both a Sequence and an Iterator.
extension Sequence where Self: IteratorProtocol {
  // @_implements(Sequence, Iterator)
  public typealias _Default_Iterator = Self
}

/// A default makeIterator() function for `IteratorProtocol` instances that
/// are declared to conform to `Sequence`
extension Sequence where Self.Iterator == Self {
  /// Returns an iterator over the elements of this sequence.
  @_inlineable
  public func makeIterator() -> Self {
    return self
  }
}

/// A sequence that lazily consumes and drops `n` elements from an underlying
/// `Base` iterator before possibly returning the first available element.
///
/// The underlying iterator's sequence may be infinite.
@_versioned
@_fixed_layout
internal struct _DropFirstSequence<Base : IteratorProtocol>
    : Sequence, IteratorProtocol {

  @_versioned
  internal var _iterator: Base
  @_versioned
  internal let _limit: Int
  @_versioned
  internal var _dropped: Int

  @_versioned
  @_inlineable
  internal init(_iterator: Base, limit: Int, dropped: Int = 0) {
    self._iterator = _iterator
    self._limit = limit
    self._dropped = dropped
  }

  @_versioned
  @_inlineable
  internal func makeIterator() -> _DropFirstSequence<Base> {
    return self
  }

  @_versioned
  @_inlineable
  internal mutating func next() -> Base.Element? {
    while _dropped < _limit {
      if _iterator.next() == nil {
        _dropped = _limit
        return nil
      }
      _dropped += 1
    }
    return _iterator.next()
  }

  @_versioned
  @_inlineable
  internal func dropFirst(_ n: Int) -> AnySequence<Base.Element> {
    // If this is already a _DropFirstSequence, we need to fold in
    // the current drop count and drop limit so no data is lost.
    //
    // i.e. [1,2,3,4].dropFirst(1).dropFirst(1) should be equivalent to
    // [1,2,3,4].dropFirst(2).
    return AnySequence(
      _DropFirstSequence(
        _iterator: _iterator, limit: _limit + n, dropped: _dropped))
  }
}

/// A sequence that only consumes up to `n` elements from an underlying
/// `Base` iterator.
///
/// The underlying iterator's sequence may be infinite.
@_fixed_layout
@_versioned
internal struct _PrefixSequence<Base : IteratorProtocol>
    : Sequence, IteratorProtocol {
  @_versioned
  internal let _maxLength: Int
  @_versioned
  internal var _iterator: Base
  @_versioned
  internal var _taken: Int

  @_versioned
  @_inlineable
  internal init(_iterator: Base, maxLength: Int, taken: Int = 0) {
    self._iterator = _iterator
    self._maxLength = maxLength
    self._taken = taken
  }

  @_versioned
  @_inlineable
  internal func makeIterator() -> _PrefixSequence<Base> {
    return self
  }

  @_versioned
  @_inlineable
  internal mutating func next() -> Base.Element? {
    if _taken >= _maxLength { return nil }
    _taken += 1

    if let next = _iterator.next() {
      return next
    }

    _taken = _maxLength
    return nil
  }

  @_versioned
  @_inlineable
  internal func prefix(_ maxLength: Int) -> AnySequence<Base.Element> {
    return AnySequence(
      _PrefixSequence(
        _iterator: _iterator,
        maxLength: Swift.min(maxLength, self._maxLength),
        taken: _taken))
  }
}

/// A sequence that lazily consumes and drops `n` elements from an underlying
/// `Base` iterator before possibly returning the first available element.
///
/// The underlying iterator's sequence may be infinite.
@_fixed_layout
@_versioned
internal struct _DropWhileSequence<Base : IteratorProtocol>
    : Sequence, IteratorProtocol {

      typealias Element = Base.Element

  @_versioned
  internal var _iterator: Base
  @_versioned
  internal var _nextElement: Base.Element?

  @_versioned
  @_inlineable
  internal init(
    iterator: Base,
    nextElement: Base.Element?,
    predicate: (Base.Element) throws -> Bool
  ) rethrows {
    self._iterator = iterator
    self._nextElement = nextElement ?? _iterator.next()

    while try _nextElement.flatMap(predicate) == true {
      _nextElement = _iterator.next()
    }
  }

  @_versioned
  @_inlineable
  internal func makeIterator() -> _DropWhileSequence<Base> {
    return self
  }

  @_versioned
  @_inlineable
  internal mutating func next() -> Element? {
    guard _nextElement != nil else {
      return _iterator.next()
    }

    let next = _nextElement
    _nextElement = nil
    return next
  }

  @_versioned
  @_inlineable
  internal func drop(
    while predicate: (Element) throws -> Bool
  ) rethrows -> AnySequence<Element> {
    // If this is already a _DropWhileSequence, avoid multiple
    // layers of wrapping and keep the same iterator.
    return try AnySequence(
      _DropWhileSequence(
        iterator: _iterator, nextElement: _nextElement, predicate: predicate))
  }
}

//===----------------------------------------------------------------------===//
// Default implementations for Sequence
//===----------------------------------------------------------------------===//

extension Sequence {
  /// Returns an array containing the results of mapping the given closure
  /// over the sequence's elements.
  ///
  /// In this example, `map` is used first to convert the names in the array
  /// to lowercase strings and then to count their characters.
  ///
  ///     let cast = ["Vivien", "Marlon", "Kim", "Karl"]
  ///     let lowercaseNames = cast.map { $0.lowercased() }
  ///     // 'lowercaseNames' == ["vivien", "marlon", "kim", "karl"]
  ///     let letterCounts = cast.map { $0.count }
  ///     // 'letterCounts' == [6, 6, 3, 4]
  ///
  /// - Parameter transform: A mapping closure. `transform` accepts an
  ///   element of this sequence as its parameter and returns a transformed
  ///   value of the same or of a different type.
  /// - Returns: An array containing the transformed elements of this
  ///   sequence.
  @_inlineable
  public func map<T>(
    _ transform: (Element) throws -> T
  ) rethrows -> [T] {
    let initialCapacity = underestimatedCount
    var result = ContiguousArray<T>()
    result.reserveCapacity(initialCapacity)

    var iterator = self.makeIterator()

    // Add elements up to the initial capacity without checking for regrowth.
    for _ in 0..<initialCapacity {
      result.append(try transform(iterator.next()!))
    }
    // Add remaining elements, if any.
    while let element = iterator.next() {
      result.append(try transform(element))
    }
    return Array(result)
  }

  /// Returns an array containing, in order, the elements of the sequence
  /// that satisfy the given predicate.
  ///
  /// In this example, `filter(_:)` is used to include only names shorter than
  /// five characters.
  ///
  ///     let cast = ["Vivien", "Marlon", "Kim", "Karl"]
  ///     let shortNames = cast.filter { $0.count < 5 }
  ///     print(shortNames)
  ///     // Prints "["Kim", "Karl"]"
  ///
  /// - Parameter isIncluded: A closure that takes an element of the
  ///   sequence as its argument and returns a Boolean value indicating
  ///   whether the element should be included in the returned array.
  /// - Returns: An array of the elements that `isIncluded` allowed.
  @_inlineable
  public func filter(
    _ isIncluded: (Element) throws -> Bool
  ) rethrows -> [Element] {
    return try _filter(isIncluded)
  }

  @_transparent
  public func _filter(
    _ isIncluded: (Element) throws -> Bool
  ) rethrows -> [Element] {

    var result = ContiguousArray<Element>()

    var iterator = self.makeIterator()

    while let element = iterator.next() {
      if try isIncluded(element) {
        result.append(element)
      }
    }

    return Array(result)
  }

  /// Returns a value less than or equal to the number of elements in
  /// the sequence, nondestructively.
  ///
  /// - Complexity: O(*n*)
  @_inlineable
  public var underestimatedCount: Int {
    return 0
  }

  @_inlineable
  public func _preprocessingPass<R>(
    _ preprocess: () throws -> R
  ) rethrows -> R? {
    return nil
  }

  @_inlineable
  public func _customContainsEquatableElement(
    _ element: Iterator.Element
  ) -> Bool? {
    return nil
  }

  /// Calls the given closure on each element in the sequence in the same order
  /// as a `for`-`in` loop.
  ///
  /// The two loops in the following example produce the same output:
  ///
  ///     let numberWords = ["one", "two", "three"]
  ///     for word in numberWords {
  ///         print(word)
  ///     }
  ///     // Prints "one"
  ///     // Prints "two"
  ///     // Prints "three"
  ///
  ///     numberWords.forEach { word in
  ///         print(word)
  ///     }
  ///     // Same as above
  ///
  /// Using the `forEach` method is distinct from a `for`-`in` loop in two
  /// important ways:
  ///
  /// 1. You cannot use a `break` or `continue` statement to exit the current
  ///    call of the `body` closure or skip subsequent calls.
  /// 2. Using the `return` statement in the `body` closure will exit only from
  ///    the current call to `body`, not from any outer scope, and won't skip
  ///    subsequent calls.
  ///
  /// - Parameter body: A closure that takes an element of the sequence as a
  ///   parameter.
  @_inlineable
  public func forEach(
    _ body: (Element) throws -> Void
  ) rethrows {
    for element in self {
      try body(element)
    }
  }
}

@_versioned
@_fixed_layout
internal enum _StopIteration : Error {
  case stop
}

extension Sequence {
  /// Returns the first element of the sequence that satisfies the given
  /// predicate.
  ///
  /// The following example uses the `first(where:)` method to find the first
  /// negative number in an array of integers:
  ///
  ///     let numbers = [3, 7, 4, -2, 9, -6, 10, 1]
  ///     if let firstNegative = numbers.first(where: { $0 < 0 }) {
  ///         print("The first negative number is \(firstNegative).")
  ///     }
  ///     // Prints "The first negative number is -2."
  ///
  /// - Parameter predicate: A closure that takes an element of the sequence as
  ///   its argument and returns a Boolean value indicating whether the
  ///   element is a match.
  /// - Returns: The first element of the sequence that satisfies `predicate`,
  ///   or `nil` if there is no element that satisfies `predicate`.
  @_inlineable
  public func first(
    where predicate: (Element) throws -> Bool
  ) rethrows -> Element? {
    var foundElement: Element?
    do {
      try self.forEach {
        if try predicate($0) {
          foundElement = $0
          throw _StopIteration.stop
        }
      }
    } catch is _StopIteration { }
    return foundElement
  }
}

extension Sequence where Element : Equatable {
  /// Returns the longest possible subsequences of the sequence, in order,
  /// around elements equal to the given element.
  ///
  /// The resulting array consists of at most `maxSplits + 1` subsequences.
  /// Elements that are used to split the sequence are not returned as part of
  /// any subsequence.
  ///
  /// The following examples show the effects of the `maxSplits` and
  /// `omittingEmptySubsequences` parameters when splitting a string at each
  /// space character (" "). The first use of `split` returns each word that
  /// was originally separated by one or more spaces.
  ///
  ///     let line = "BLANCHE:   I don't want realism. I want magic!"
  ///     print(line.split(separator: " ")
  ///               .map(String.init))
  ///     // Prints "["BLANCHE:", "I", "don\'t", "want", "realism.", "I", "want", "magic!"]"
  ///
  /// The second example passes `1` for the `maxSplits` parameter, so the
  /// original string is split just once, into two new strings.
  ///
  ///     print(line.split(separator: " ", maxSplits: 1)
  ///               .map(String.init))
  ///     // Prints "["BLANCHE:", "  I don\'t want realism. I want magic!"]"
  ///
  /// The final example passes `false` for the `omittingEmptySubsequences`
  /// parameter, so the returned array contains empty strings where spaces
  /// were repeated.
  ///
  ///     print(line.split(separator: " ", omittingEmptySubsequences: false)
  ///               .map(String.init))
  ///     // Prints "["BLANCHE:", "", "", "I", "don\'t", "want", "realism.", "I", "want", "magic!"]"
  ///
  /// - Parameters:
  ///   - separator: The element that should be split upon.
  ///   - maxSplits: The maximum number of times to split the sequence, or one
  ///     less than the number of subsequences to return. If `maxSplits + 1`
  ///     subsequences are returned, the last one is a suffix of the original
  ///     sequence containing the remaining elements. `maxSplits` must be
  ///     greater than or equal to zero. The default value is `Int.max`.
  ///   - omittingEmptySubsequences: If `false`, an empty subsequence is
  ///     returned in the result for each consecutive pair of `separator`
  ///     elements in the sequence and for each instance of `separator` at the
  ///     start or end of the sequence. If `true`, only nonempty subsequences
  ///     are returned. The default value is `true`.
  /// - Returns: An array of subsequences, split from this sequence's elements.
  @_inlineable
  public func split(
    separator: Element,
    maxSplits: Int = Int.max,
    omittingEmptySubsequences: Bool = true
  ) -> [SubSequence] {
    return split(
      maxSplits: maxSplits,
      omittingEmptySubsequences: omittingEmptySubsequences,
      whereSeparator: { $0 == separator })
  }
}

extension Sequence where SubSequence == AnySequence<Element> {

  /// Returns the longest possible subsequences of the sequence, in order, that
  /// don't contain elements satisfying the given predicate. Elements that are
  /// used to split the sequence are not returned as part of any subsequence.
  ///
  /// The following examples show the effects of the `maxSplits` and
  /// `omittingEmptySubsequences` parameters when splitting a string using a
  /// closure that matches spaces. The first use of `split` returns each word
  /// that was originally separated by one or more spaces.
  ///
  ///     let line = "BLANCHE:   I don't want realism. I want magic!"
  ///     print(line.split(whereSeparator: { $0 == " " })
  ///               .map(String.init))
  ///     // Prints "["BLANCHE:", "I", "don\'t", "want", "realism.", "I", "want", "magic!"]"
  ///
  /// The second example passes `1` for the `maxSplits` parameter, so the
  /// original string is split just once, into two new strings.
  ///
  ///     print(
  ///        line.split(maxSplits: 1, whereSeparator: { $0 == " " })
  ///                       .map(String.init))
  ///     // Prints "["BLANCHE:", "  I don\'t want realism. I want magic!"]"
  ///
  /// The final example passes `true` for the `allowEmptySlices` parameter, so
  /// the returned array contains empty strings where spaces were repeated.
  ///
  ///     print(
  ///         line.split(
  ///             omittingEmptySubsequences: false,
  ///             whereSeparator: { $0 == " " }
  ///         ).map(String.init))
  ///     // Prints "["BLANCHE:", "", "", "I", "don\'t", "want", "realism.", "I", "want", "magic!"]"
  ///
  /// - Parameters:
  ///   - maxSplits: The maximum number of times to split the sequence, or one
  ///     less than the number of subsequences to return. If `maxSplits + 1`
  ///     subsequences are returned, the last one is a suffix of the original
  ///     sequence containing the remaining elements. `maxSplits` must be
  ///     greater than or equal to zero. The default value is `Int.max`.
  ///   - omittingEmptySubsequences: If `false`, an empty subsequence is
  ///     returned in the result for each pair of consecutive elements
  ///     satisfying the `isSeparator` predicate and for each element at the
  ///     start or end of the sequence satisfying the `isSeparator` predicate.
  ///     If `true`, only nonempty subsequences are returned. The default
  ///     value is `true`.
  ///   - isSeparator: A closure that returns `true` if its argument should be
  ///     used to split the sequence; otherwise, `false`.
  /// - Returns: An array of subsequences, split from this sequence's elements.
  @_inlineable
  public func split(
    maxSplits: Int = Int.max,
    omittingEmptySubsequences: Bool = true,
    whereSeparator isSeparator: (Element) throws -> Bool
  ) rethrows -> [AnySequence<Element>] {
    _precondition(maxSplits >= 0, "Must take zero or more splits")
    var result: [AnySequence<Element>] = []
    var subSequence: [Element] = []

    @discardableResult
    func appendSubsequence() -> Bool {
      if subSequence.isEmpty && omittingEmptySubsequences {
        return false
      }
      result.append(AnySequence(subSequence))
      subSequence = []
      return true
    }

    if maxSplits == 0 {
      // We aren't really splitting the sequence.  Convert `self` into an
      // `Array` using a fast entry point.
      subSequence = Array(self)
      appendSubsequence()
      return result
    }

    var iterator = self.makeIterator()
    while let element = iterator.next() {
      if try isSeparator(element) {
        if !appendSubsequence() {
          continue
        }
        if result.count == maxSplits {
          break
        }
      } else {
        subSequence.append(element)
      }
    }
    while let element = iterator.next() {
      subSequence.append(element)
    }
    appendSubsequence()
    return result
  }

  /// Returns a subsequence, up to the given maximum length, containing the
  /// final elements of the sequence.
  ///
  /// The sequence must be finite. If the maximum length exceeds the number of
  /// elements in the sequence, the result contains all the elements in the
  /// sequence.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.suffix(2))
  ///     // Prints "[4, 5]"
  ///     print(numbers.suffix(10))
  ///     // Prints "[1, 2, 3, 4, 5]"
  ///
  /// - Parameter maxLength: The maximum number of elements to return. The
  ///   value of `maxLength` must be greater than or equal to zero.
  /// - Complexity: O(*n*), where *n* is the length of the sequence.
  @_inlineable
  public func suffix(_ maxLength: Int) -> AnySequence<Element> {
    _precondition(maxLength >= 0, "Can't take a suffix of negative length from a sequence")
    if maxLength == 0 { return AnySequence([]) }
    // FIXME: <rdar://problem/21885650> Create reusable RingBuffer<T>
    // Put incoming elements into a ring buffer to save space. Once all
    // elements are consumed, reorder the ring buffer into an `Array`
    // and return it. This saves memory for sequences particularly longer
    // than `maxLength`.
    var ringBuffer: [Element] = []
    ringBuffer.reserveCapacity(Swift.min(maxLength, underestimatedCount))

    var i = ringBuffer.startIndex

    for element in self {
      if ringBuffer.count < maxLength {
        ringBuffer.append(element)
      } else {
        ringBuffer[i] = element
        i += 1
        i %= maxLength
      }
    }

    if i != ringBuffer.startIndex {
      let s0 = ringBuffer[i..<ringBuffer.endIndex]
      let s1 = ringBuffer[0..<i]
      return AnySequence([s0, s1].joined())
    }
    return AnySequence(ringBuffer)
  }

  /// Returns a subsequence containing all but the given number of initial
  /// elements.
  ///
  /// If the number of elements to drop exceeds the number of elements in
  /// the sequence, the result is an empty subsequence.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.dropFirst(2))
  ///     // Prints "[3, 4, 5]"
  ///     print(numbers.dropFirst(10))
  ///     // Prints "[]"
  ///
  /// - Parameter n: The number of elements to drop from the beginning of
  ///   the sequence. `n` must be greater than or equal to zero.
  /// - Returns: A subsequence starting after the specified number of
  ///   elements.
  ///
  /// - Complexity: O(1).
  @_inlineable
  public func dropFirst(_ n: Int) -> AnySequence<Element> {
    _precondition(n >= 0, "Can't drop a negative number of elements from a sequence")
    if n == 0 { return AnySequence(self) }
    return AnySequence(_DropFirstSequence(_iterator: makeIterator(), limit: n))
  }

  /// Returns a subsequence containing all but the given number of final
  /// elements.
  ///
  /// The sequence must be finite. If the number of elements to drop exceeds
  /// the number of elements in the sequence, the result is an empty
  /// subsequence.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.dropLast(2))
  ///     // Prints "[1, 2, 3]"
  ///     print(numbers.dropLast(10))
  ///     // Prints "[]"
  ///
  /// - Parameter n: The number of elements to drop off the end of the
  ///   sequence. `n` must be greater than or equal to zero.
  /// - Returns: A subsequence leaving off the specified number of elements.
  ///
  /// - Complexity: O(*n*), where *n* is the length of the sequence.
  @_inlineable
  public func dropLast(_ n: Int) -> AnySequence<Element> {
    _precondition(n >= 0, "Can't drop a negative number of elements from a sequence")
    if n == 0 { return AnySequence(self) }

    // FIXME: <rdar://problem/21885650> Create reusable RingBuffer<T>
    // Put incoming elements from this sequence in a holding tank, a ring buffer
    // of size <= n. If more elements keep coming in, pull them out of the
    // holding tank into the result, an `Array`. This saves
    // `n` * sizeof(Element) of memory, because slices keep the entire
    // memory of an `Array` alive.
    var result: [Element] = []
    var ringBuffer: [Element] = []
    var i = ringBuffer.startIndex

    for element in self {
      if ringBuffer.count < n {
        ringBuffer.append(element)
      } else {
        result.append(ringBuffer[i])
        ringBuffer[i] = element
        i = ringBuffer.index(after: i) % n
      }
    }
    return AnySequence(result)
  }

  /// Returns a subsequence by skipping the initial, consecutive elements that
  /// satisfy the given predicate.
  ///
  /// The following example uses the `drop(while:)` method to skip over the
  /// positive numbers at the beginning of the `numbers` array. The result
  /// begins with the first element of `numbers` that does not satisfy
  /// `predicate`.
  ///
  ///     let numbers = [3, 7, 4, -2, 9, -6, 10, 1]
  ///     let startingWithNegative = numbers.drop(while: { $0 > 0 })
  ///     // startingWithNegative == [-2, 9, -6, 10, 1]
  ///
  /// If `predicate` matches every element in the sequence, the result is an
  /// empty sequence.
  ///
  /// - Parameter predicate: A closure that takes an element of the sequence as
  ///   its argument and returns a Boolean value indicating whether the
  ///   element should be included in the result.
  /// - Returns: A subsequence starting after the initial, consecutive elements
  ///   that satisfy `predicate`.
  ///
  /// - Complexity: O(*n*), where *n* is the length of the collection.
  @_inlineable
  public func drop(
    while predicate: (Element) throws -> Bool
  ) rethrows -> AnySequence<Element> {
    return try AnySequence(
      _DropWhileSequence(
        iterator: makeIterator(), nextElement: nil, predicate: predicate))
  }

  /// Returns a subsequence, up to the specified maximum length, containing the
  /// initial elements of the sequence.
  ///
  /// If the maximum length exceeds the number of elements in the sequence,
  /// the result contains all the elements in the sequence.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.prefix(2))
  ///     // Prints "[1, 2]"
  ///     print(numbers.prefix(10))
  ///     // Prints "[1, 2, 3, 4, 5]"
  ///
  /// - Parameter maxLength: The maximum number of elements to return. The
  ///   value of `maxLength` must be greater than or equal to zero.
  /// - Returns: A subsequence starting at the beginning of this sequence
  ///   with at most `maxLength` elements.
  ///
  /// - Complexity: O(1)
  @_inlineable
  public func prefix(_ maxLength: Int) -> AnySequence<Element> {
    _precondition(maxLength >= 0, "Can't take a prefix of negative length from a sequence")
    if maxLength == 0 {
      return AnySequence(EmptyCollection<Element>())
    }
    return AnySequence(
      _PrefixSequence(_iterator: makeIterator(), maxLength: maxLength))
  }

  /// Returns a subsequence containing the initial, consecutive elements that
  /// satisfy the given predicate.
  ///
  /// The following example uses the `prefix(while:)` method to find the
  /// positive numbers at the beginning of the `numbers` array. Every element
  /// of `numbers` up to, but not including, the first negative value is
  /// included in the result.
  ///
  ///     let numbers = [3, 7, 4, -2, 9, -6, 10, 1]
  ///     let positivePrefix = numbers.prefix(while: { $0 > 0 })
  ///     // positivePrefix == [3, 7, 4]
  ///
  /// If `predicate` matches every element in the sequence, the resulting
  /// sequence contains every element of the sequence.
  ///
  /// - Parameter predicate: A closure that takes an element of the sequence as
  ///   its argument and returns a Boolean value indicating whether the
  ///   element should be included in the result.
  /// - Returns: A subsequence of the initial, consecutive elements that
  ///   satisfy `predicate`.
  ///
  /// - Complexity: O(*n*), where *n* is the length of the collection.
  @_inlineable
  public func prefix(
    while predicate: (Element) throws -> Bool
  ) rethrows -> AnySequence<Element> {
    var result: [Element] = []

    for element in self {
      guard try predicate(element) else {
        break
      }
      result.append(element)
    }
    return AnySequence(result)
  }
}

extension Sequence {
  /// Returns a subsequence containing all but the first element of the
  /// sequence.
  ///
  /// The following example drops the first element from an array of integers.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.dropFirst())
  ///     // Prints "[2, 3, 4, 5]"
  ///
  /// If the sequence has no elements, the result is an empty subsequence.
  ///
  ///     let empty: [Int] = []
  ///     print(empty.dropFirst())
  ///     // Prints "[]"
  ///
  /// - Returns: A subsequence starting after the first element of the
  ///   sequence.
  ///
  /// - Complexity: O(1)
  @_inlineable
  public func dropFirst() -> SubSequence { return dropFirst(1) }

  /// Returns a subsequence containing all but the last element of the
  /// sequence.
  ///
  /// The sequence must be finite.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.dropLast())
  ///     // Prints "[1, 2, 3, 4]"
  ///
  /// If the sequence has no elements, the result is an empty subsequence.
  ///
  ///     let empty: [Int] = []
  ///     print(empty.dropLast())
  ///     // Prints "[]"
  ///
  /// - Returns: A subsequence leaving off the last element of the sequence.
  ///
  /// - Complexity: O(*n*), where *n* is the length of the sequence.
  @_inlineable
  public func dropLast() -> SubSequence  { return dropLast(1) }
}

extension Sequence {
  /// Copies `self` into the supplied buffer.
  ///
  /// - Precondition: The memory in `self` is uninitialized. The buffer must
  ///   contain sufficient uninitialized memory to accommodate `source.underestimatedCount`.
  ///
  /// - Postcondition: The `Pointee`s at `buffer[startIndex..<returned index]` are
  ///   initialized.
  @_inlineable
  public func _copyContents(
    initializing buffer: UnsafeMutableBufferPointer<Element>
  ) -> (Iterator,UnsafeMutableBufferPointer<Element>.Index) {
      var it = self.makeIterator()
      guard var ptr = buffer.baseAddress else { return (it,buffer.startIndex) }
      for idx in buffer.startIndex..<buffer.count {
        guard let x = it.next() else {
          return (it, idx)
        }
        ptr.initialize(to: x)
        ptr += 1
      }
      return (it,buffer.endIndex)
    }
}

// FIXME(ABI)#182
// Pending <rdar://problem/14011860> and <rdar://problem/14396120>,
// pass an IteratorProtocol through IteratorSequence to give it "Sequence-ness"
/// A sequence built around an iterator of type `Base`.
///
/// Useful mostly to recover the ability to use `for`...`in`,
/// given just an iterator `i`:
///
///     for x in IteratorSequence(i) { ... }
@_fixed_layout
public struct IteratorSequence<Base : IteratorProtocol> {
  @_versioned
  internal var _base: Base

  /// Creates an instance whose iterator is a copy of `base`.
  @_inlineable
  public init(_ base: Base) {
    _base = base
  }
}

extension IteratorSequence: IteratorProtocol, Sequence {
  /// Advances to the next element and returns it, or `nil` if no next element
  /// exists.
  ///
  /// Once `nil` has been returned, all subsequent calls return `nil`.
  ///
  /// - Precondition: `next()` has not been applied to a copy of `self`
  ///   since the copy was made.
  @_inlineable
  public mutating func next() -> Base.Element? {
    return _base.next()
  }
}
