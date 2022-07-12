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

/// A type that provides subscript access to its elements, with forward
/// index traversal.
///
/// In most cases, it's best to ignore this protocol and use the `Collection`
/// protocol instead, because it has a more complete interface.
@available(*, deprecated, message: "it will be removed in Swift 4.0.  Please use 'Collection' instead")
public typealias IndexableBase = Collection

/// A type that provides subscript access to its elements, with forward index
/// traversal.
///
/// In most cases, it's best to ignore this protocol and use the `Collection`
/// protocol instead, because it has a more complete interface.
@available(*, deprecated, message: "it will be removed in Swift 4.0.  Please use 'Collection' instead")
public typealias Indexable = Collection

/// A type that iterates over a collection using its indices.
///
/// The `IndexingIterator` type is the default iterator for any collection that
/// doesn't declare its own. It acts as an iterator by using a collection's
/// indices to step over each value in the collection. Most collections in the
/// standard library use `IndexingIterator` as their iterator.
///
/// By default, any custom collection type you create will inherit a
/// `makeIterator()` method that returns an `IndexingIterator` instance,
/// making it unnecessary to declare your own. When creating a custom
/// collection type, add the minimal requirements of the `Collection`
/// protocol: starting and ending indices and a subscript for accessing
/// elements. With those elements defined, the inherited `makeIterator()`
/// method satisfies the requirements of the `Sequence` protocol.
///
/// Here's an example of a type that declares the minimal requirements for a
/// collection. The `CollectionOfTwo` structure is a fixed-size collection
/// that always holds two elements of a specific type.
///
///     struct CollectionOfTwo<Element>: Collection {
///         let elements: (Element, Element)
///
///         init(_ first: Element, _ second: Element) {
///             self.elements = (first, second)
///         }
///
///         var startIndex: Int { return 0 }
///         var endIndex: Int   { return 2 }
///
///         subscript(index: Int) -> Element {
///             switch index {
///             case 0: return elements.0
///             case 1: return elements.1
///             default: fatalError("Index out of bounds.")
///             }
///         }
///         
///         func index(after i: Int) -> Int {
///             precondition(i < endIndex, "Can't advance beyond endIndex")
///             return i + 1
///         }
///     }
///
/// Because `CollectionOfTwo` doesn't define its own `makeIterator()`
/// method or `Iterator` associated type, it uses the default iterator type,
/// `IndexingIterator`. This example shows how a `CollectionOfTwo` instance
/// can be created holding the values of a point, and then iterated over
/// using a `for`-`in` loop.
///
///     let point = CollectionOfTwo(15.0, 20.0)
///     for element in point {
///         print(element)
///     }
///     // Prints "15.0"
///     // Prints "20.0"
@_fixed_layout
public struct IndexingIterator<Elements : Collection> {
  @_versioned
  internal let _elements: Elements
  @_versioned
  internal var _position: Elements.Index

  @_inlineable
  @inline(__always)
  /// Creates an iterator over the given collection.
  public /// @testable
  init(_elements: Elements) {
    self._elements = _elements
    self._position = _elements.startIndex
  }

  @_inlineable
  @inline(__always)
  /// Creates an iterator over the given collection.
  public /// @testable
  init(_elements: Elements, _position: Elements.Index) {
    self._elements = _elements
    self._position = _position
  }
}

extension IndexingIterator: IteratorProtocol, Sequence {
  public typealias Element = Elements.Element
  public typealias Iterator = IndexingIterator<Elements>
  public typealias SubSequence = AnySequence<Element>

  /// Advances to the next element and returns it, or `nil` if no next element
  /// exists.
  ///
  /// Repeatedly calling this method returns all the elements of the underlying
  /// sequence in order. As soon as the sequence has run out of elements, all
  /// subsequent calls return `nil`.
  ///
  /// This example shows how an iterator can be used explicitly to emulate a
  /// `for`-`in` loop. First, retrieve a sequence's iterator, and then call
  /// the iterator's `next()` method until it returns `nil`.
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
  /// - Returns: The next element in the underlying sequence if a next element
  ///   exists; otherwise, `nil`.
  @_inlineable
  @inline(__always)
  public mutating func next() -> Elements.Element? {
    if _position == _elements.endIndex { return nil }
    let element = _elements[_position]
    _elements.formIndex(after: &_position)
    return element
  }
}

/// A sequence whose elements can be traversed multiple times,
/// nondestructively, and accessed by an indexed subscript.
///
/// Collections are used extensively throughout the standard library. When you
/// use arrays, dictionaries, and other collections, you benefit from the
/// operations that the `Collection` protocol declares and implements. In
/// addition to the operations that collections inherit from the `Sequence`
/// protocol, you gain access to methods that depend on accessing an element
/// at a specific position in a collection.
///
/// For example, if you want to print only the first word in a string, you can
/// search for the index of the first space, and then create a substring up to
/// that position.
///
///     let text = "Buffalo buffalo buffalo buffalo."
///     if let firstSpace = text.index(of: " ") {
///         print(text[..<firstSpace])
///     }
///     // Prints "Buffalo"
///
/// The `firstSpace` constant is an index into the `text` string---the position
/// of the first space in the string. You can store indices in variables, and
/// pass them to collection algorithms or use them later to access the
/// corresponding element. In the example above, `firstSpace` is used to
/// extract the prefix that contains elements up to that index.
///
/// Accessing Individual Elements
/// =============================
///
/// You can access an element of a collection through its subscript by using
/// any valid index except the collection's `endIndex` property. This property
/// is a "past the end" index that does not correspond with any element of the
/// collection.
///
/// Here's an example of accessing the first character in a string through its
/// subscript:
///
///     let firstChar = text[text.startIndex]
///     print(firstChar)
///     // Prints "B"
///
/// The `Collection` protocol declares and provides default implementations for
/// many operations that depend on elements being accessible by their
/// subscript. For example, you can also access the first character of `text`
/// using the `first` property, which has the value of the first element of
/// the collection, or `nil` if the collection is empty.
///
///     print(text.first)
///     // Prints "Optional("B")"
///
/// You can pass only valid indices to collection operations. You can find a
/// complete set of a collection's valid indices by starting with the
/// collection's `startIndex` property and finding every successor up to, and
/// including, the `endIndex` property. All other values of the `Index` type,
/// such as the `startIndex` property of a different collection, are invalid
/// indices for this collection.
///
/// Saved indices may become invalid as a result of mutating operations. For
/// more information about index invalidation in mutable collections, see the
/// reference for the `MutableCollection` and `RangeReplaceableCollection`
/// protocols, as well as for the specific type you're using.
///
/// Accessing Slices of a Collection
/// ================================
///
/// You can access a slice of a collection through its ranged subscript or by
/// calling methods like `prefix(while:)` or `suffix(_:)`. A slice of a
/// collection can contain zero or more of the original collection's elements
/// and shares the original collection's semantics.
///
/// The following example creates a `firstWord` constant by using the
/// `prefix(while:)` method to get a slice of the `text` string.
///
///     let firstWord = text.prefix(while: { $0 != " " })
///     print(firstWord)
///     // Prints "Buffalo"
///
/// You can retrieve the same slice using the string's ranged subscript, which
/// takes a range expression.
///
///     if let firstSpace = text.index(of: " ") {
///         print(text[..<firstSpace]
///         // Prints "Buffalo"
///     }
///
/// The retrieved slice of `text` is equivalent in each of these cases.
///
/// Slices Share Indices
/// --------------------
///
/// A collection and its slices share the same indices. An element of a
/// collection is located under the same index in a slice as in the base
/// collection, as long as neither the collection nor the slice has been
/// mutated since the slice was created.
///
/// For example, suppose you have an array holding the number of absences from
/// each class during a session.
///
///     var absences = [0, 2, 0, 4, 0, 3, 1, 0]
///
/// You're tasked with finding the day with the most absences in the second
/// half of the session. To find the index of the day in question, follow
/// these steps:
///
/// 1) Create a slice of the `absences` array that holds the second half of the
///    days.
/// 2) Use the `max(by:)` method to determine the index of the day with the
///    most absences.
/// 3) Print the result using the index found in step 2 on the original
///    `absences` array.
///
/// Here's an implementation of those steps:
///
///     let secondHalf = absences.suffix(absences.count / 2)
///     if let i = secondHalf.indices.max(by: { secondHalf[$0] < secondHalf[$1] }) {
///         print("Highest second-half absences: \(absences[i])")
///     }
///     // Prints "Highest second-half absences: 3"
///
/// Slices Inherit Collection Semantics
/// -----------------------------------
///
/// A slice inherits the value or reference semantics of its base collection.
/// That is, when working with a slice of a mutable collection that has value
/// semantics, such as an array, mutating the original collection triggers a
/// copy of that collection and does not affect the contents of the slice.
///
/// For example, if you update the last element of the `absences` array from
/// `0` to `2`, the `secondHalf` slice is unchanged.
///
///     absences[7] = 2
///     print(absences)
///     // Prints "[0, 2, 0, 4, 0, 3, 1, 2]"
///     print(secondHalf)
///     // Prints "[0, 3, 1, 0]"
///
/// Traversing a Collection
/// =======================
///
/// Although a sequence can be consumed as it is traversed, a collection is
/// guaranteed to be *multipass*: Any element can be repeatedly accessed by
/// saving its index. Moreover, a collection's indices form a finite range of
/// the positions of the collection's elements. The fact that all collections
/// are finite guarantees the safety of many sequence operations, such as
/// using the `contains(_:)` method to test whether a collection includes an
/// element.
///
/// Iterating over the elements of a collection by their positions yields the
/// same elements in the same order as iterating over that collection using
/// its iterator. This example demonstrates that the `characters` view of a
/// string returns the same characters in the same order whether the view's
/// indices or the view itself is being iterated.
///
///     let word = "Swift"
///     for character in word {
///         print(character)
///     }
///     // Prints "S"
///     // Prints "w"
///     // Prints "i"
///     // Prints "f"
///     // Prints "t"
///
///     for i in word.indices {
///         print(word[i])
///     }
///     // Prints "S"
///     // Prints "w"
///     // Prints "i"
///     // Prints "f"
///     // Prints "t"
///
/// Conforming to the Collection Protocol
/// =====================================
///
/// If you create a custom sequence that can provide repeated access to its
/// elements, make sure that its type conforms to the `Collection` protocol in
/// order to give a more useful and more efficient interface for sequence and
/// collection operations. To add `Collection` conformance to your type, you
/// must declare at least the following requirements:
///
/// - The `startIndex` and `endIndex` properties
/// - A subscript that provides at least read-only access to your type's
///   elements
/// - The `index(after:)` method for advancing an index into your collection
///
/// Expected Performance
/// ====================
///
/// Types that conform to `Collection` are expected to provide the `startIndex`
/// and `endIndex` properties and subscript access to elements as O(1)
/// operations. Types that are not able to guarantee this performance must
/// document the departure, because many collection operations depend on O(1)
/// subscripting performance for their own performance guarantees.
///
/// The performance of some collection operations depends on the type of index
/// that the collection provides. For example, a random-access collection,
/// which can measure the distance between two indices in O(1) time, can
/// calculate its `count` property in O(1) time. Conversely, because a forward
/// or bidirectional collection must traverse the entire collection to count
/// the number of contained elements, accessing its `count` property is an
/// O(*n*) operation.
public protocol Collection: Sequence where SubSequence: Collection {
  // FIXME(ABI): Associated type inference requires this.
  associatedtype Element

  /// A type that represents a position in the collection.
  ///
  /// Valid indices consist of the position of every element and a
  /// "past the end" position that's not valid for use as a subscript
  /// argument.
  associatedtype Index : Comparable

  /// The position of the first element in a nonempty collection.
  ///
  /// If the collection is empty, `startIndex` is equal to `endIndex`.
  var startIndex: Index { get }
 
  /// The collection's "past the end" position---that is, the position one
  /// greater than the last valid subscript argument.
  ///
  /// When you need a range that includes the last element of a collection, use
  /// the half-open range operator (`..<`) with `endIndex`. The `..<` operator
  /// creates a range that doesn't include the upper bound, so it's always
  /// safe to use with `endIndex`. For example:
  ///
  ///     let numbers = [10, 20, 30, 40, 50]
  ///     if let index = numbers.index(of: 30) {
  ///         print(numbers[index ..< numbers.endIndex])
  ///     }
  ///     // Prints "[30, 40, 50]"
  ///
  /// If the collection is empty, `endIndex` is equal to `startIndex`.
  var endIndex: Index { get }

  /// A type that provides the collection's iteration interface and
  /// encapsulates its iteration state.
  ///
  /// By default, a collection conforms to the `Sequence` protocol by
  /// supplying `IndexingIterator` as its associated `Iterator`
  /// type.
  associatedtype Iterator = IndexingIterator<Self>

  // FIXME(ABI)#179 (Type checker): Needed here so that the `Iterator` is properly deduced from
  // a custom `makeIterator()` function.  Otherwise we get an
  // `IndexingIterator`. <rdar://problem/21539115>
  /// Returns an iterator over the elements of the collection.
  func makeIterator() -> Iterator

  /// A sequence that represents a contiguous subrange of the collection's
  /// elements.
  ///
  /// This associated type appears as a requirement in the `Sequence`
  /// protocol, but it is restated here with stricter constraints. In a
  /// collection, the subsequence should also conform to `Collection`.
  associatedtype SubSequence = Slice<Self> where SubSequence.Index == Index

  /// Accesses the element at the specified position.
  ///
  /// The following example accesses an element of an array through its
  /// subscript to print its value:
  ///
  ///     var streets = ["Adams", "Bryant", "Channing", "Douglas", "Evarts"]
  ///     print(streets[1])
  ///     // Prints "Bryant"
  ///
  /// You can subscript a collection with any valid index other than the
  /// collection's end index. The end index refers to the position one past
  /// the last element of a collection, so it doesn't correspond with an
  /// element.
  ///
  /// - Parameter position: The position of the element to access. `position`
  ///   must be a valid index of the collection that is not equal to the
  ///   `endIndex` property.
  ///
  /// - Complexity: O(1)
  subscript(position: Index) -> Element { get }

  /// Accesses a contiguous subrange of the collection's elements.
  ///
  /// For example, using a `PartialRangeFrom` range expression with an array
  /// accesses the subrange from the start of the range expression until the
  /// end of the array.
  ///
  ///     let streets = ["Adams", "Bryant", "Channing", "Douglas", "Evarts"]
  ///     let streetsSlice = streets[2...]
  ///     print(streetsSlice)
  ///     // ["Channing", "Douglas", "Evarts"]
  ///
  /// The accessed slice uses the same indices for the same elements as the
  /// original collection. This example searches `streetsSlice` for one of the
  /// strings in the slice, and then uses that index in the original array.
  ///
  ///     let index = streetsSlice.index(of: "Evarts")!    // 4
  ///     print(streets[index])
  ///     // "Evarts"
  ///
  /// Always use the slice's `startIndex` property instead of assuming that its
  /// indices start at a particular value. Attempting to access an element by
  /// using an index outside the bounds of the slice may result in a runtime
  /// error, even if that index is valid for the original collection.
  ///
  ///     print(streetsSlice.startIndex)
  ///     // 2
  ///     print(streetsSlice[2])
  ///     // "Channing"
  ///
  ///     print(streetsSlice[0])
  ///     // error: Index out of bounds
  ///
  /// - Parameter bounds: A range of the collection's indices. The bounds of
  ///   the range must be valid indices of the collection.
  ///
  /// - Complexity: O(1)
  subscript(bounds: Range<Index>) -> SubSequence { get }

  /// A type that represents the indices that are valid for subscripting the
  /// collection, in ascending order.
  associatedtype Indices : Collection = DefaultIndices<Self>
    where Indices.Element == Index, 
          Indices.Index == Index,
          Indices.SubSequence == Indices
        
  /// The indices that are valid for subscripting the collection, in ascending
  /// order.
  ///
  /// A collection's `indices` property can hold a strong reference to the
  /// collection itself, causing the collection to be nonuniquely referenced.
  /// If you mutate the collection while iterating over its indices, a strong
  /// reference can result in an unexpected copy of the collection. To avoid
  /// the unexpected copy, use the `index(after:)` method starting with
  /// `startIndex` to produce indices instead.
  ///
  ///     var c = MyFancyCollection([10, 20, 30, 40, 50])
  ///     var i = c.startIndex
  ///     while i != c.endIndex {
  ///         c[i] /= 5
  ///         i = c.index(after: i)
  ///     }
  ///     // c == MyFancyCollection([2, 4, 6, 8, 10])
  var indices: Indices { get }

  /// Returns a subsequence from the start of the collection up to, but not
  /// including, the specified position.
  ///
  /// The resulting subsequence *does not include* the element at the position
  /// `end`. The following example searches for the index of the number `40`
  /// in an array of integers, and then prints the prefix of the array up to,
  /// but not including, that index:
  ///
  ///     let numbers = [10, 20, 30, 40, 50, 60]
  ///     if let i = numbers.index(of: 40) {
  ///         print(numbers.prefix(upTo: i))
  ///     }
  ///     // Prints "[10, 20, 30]"
  ///
  /// Passing the collection's starting index as the `end` parameter results in
  /// an empty subsequence.
  ///
  ///     print(numbers.prefix(upTo: numbers.startIndex))
  ///     // Prints "[]"
  ///
  /// Using the `prefix(upTo:)` method is equivalent to using a partial
  /// half-open range as the collection's subscript. The subscript notation is
  /// preferred over `prefix(upTo:)`.
  ///
  ///     if let i = numbers.index(of: 40) {
  ///         print(numbers[..<i])
  ///     }
  ///     // Prints "[10, 20, 30]"
  ///
  /// - Parameter end: The "past the end" index of the resulting subsequence.
  ///   `end` must be a valid index of the collection.
  /// - Returns: A subsequence up to, but not including, the `end` position.
  ///
  /// - Complexity: O(1)
  func prefix(upTo end: Index) -> SubSequence

  /// Returns a subsequence from the specified position to the end of the
  /// collection.
  ///
  /// The following example searches for the index of the number `40` in an
  /// array of integers, and then prints the suffix of the array starting at
  /// that index:
  ///
  ///     let numbers = [10, 20, 30, 40, 50, 60]
  ///     if let i = numbers.index(of: 40) {
  ///         print(numbers.suffix(from: i))
  ///     }
  ///     // Prints "[40, 50, 60]"
  ///
  /// Passing the collection's `endIndex` as the `start` parameter results in
  /// an empty subsequence.
  ///
  ///     print(numbers.suffix(from: numbers.endIndex))
  ///     // Prints "[]"
  ///
  /// Using the `suffix(from:)` method is equivalent to using a partial range
  /// from the index as the collection's subscript. The subscript notation is
  /// preferred over `suffix(from:)`.
  ///
  ///     if let i = numbers.index(of: 40) {
  ///         print(numbers[i...])
  ///     }
  ///     // Prints "[40, 50, 60]"
  ///
  /// - Parameter start: The index at which to start the resulting subsequence.
  ///   `start` must be a valid index of the collection.
  /// - Returns: A subsequence starting at the `start` position.
  ///
  /// - Complexity: O(1)
  func suffix(from start: Index) -> SubSequence

  /// Returns a subsequence from the start of the collection through the
  /// specified position.
  ///
  /// The resulting subsequence *includes* the element at the position `end`. 
  /// The following example searches for the index of the number `40` in an
  /// array of integers, and then prints the prefix of the array up to, and
  /// including, that index:
  ///
  ///     let numbers = [10, 20, 30, 40, 50, 60]
  ///     if let i = numbers.index(of: 40) {
  ///         print(numbers.prefix(through: i))
  ///     }
  ///     // Prints "[10, 20, 30, 40]"
  ///
  /// Using the `prefix(through:)` method is equivalent to using a partial
  /// closed range as the collection's subscript. The subscript notation is
  /// preferred over `prefix(through:)`.
  ///
  ///     if let i = numbers.index(of: 40) {
  ///         print(numbers[...i])
  ///     }
  ///     // Prints "[10, 20, 30, 40]"
  ///
  /// - Parameter end: The index of the last element to include in the
  ///   resulting subsequence. `end` must be a valid index of the collection
  ///   that is not equal to the `endIndex` property.
  /// - Returns: A subsequence up to, and including, the `end` position.
  ///
  /// - Complexity: O(1)
  func prefix(through position: Index) -> SubSequence

  /// A Boolean value indicating whether the collection is empty.
  ///
  /// When you need to check whether your collection is empty, use the
  /// `isEmpty` property instead of checking that the `count` property is
  /// equal to zero. For collections that don't conform to
  /// `RandomAccessCollection`, accessing the `count` property iterates
  /// through the elements of the collection.
  ///
  ///     let horseName = "Silver"
  ///     if horseName.isEmpty {
  ///         print("I've been through the desert on a horse with no name.")
  ///     } else {
  ///         print("Hi ho, \(horseName)!")
  ///     }
  ///     // Prints "Hi ho, Silver!")
  ///
  /// - Complexity: O(1)
  var isEmpty: Bool { get }

  /// The number of elements in the collection.
  ///
  /// To check whether a collection is empty, use its `isEmpty` property
  /// instead of comparing `count` to zero. Unless the collection guarantees
  /// random-access performance, calculating `count` can be an O(*n*)
  /// operation.
  ///
  /// - Complexity: O(1) if the collection conforms to
  ///   `RandomAccessCollection`; otherwise, O(*n*), where *n* is the length
  ///   of the collection.
  var count: Int { get }
  
  // The following requirement enables dispatching for index(of:) when
  // the element type is Equatable.
  /// Returns `Optional(Optional(index))` if an element was found
  /// or `Optional(nil)` if an element was determined to be missing;
  /// otherwise, `nil`.
  ///
  /// - Complexity: O(*n*)
  func _customIndexOfEquatableElement(_ element: Element) -> Index??

  /// The first element of the collection.
  ///
  /// If the collection is empty, the value of this property is `nil`.
  /// 
  ///     let numbers = [10, 20, 30, 40, 50]
  ///     if let firstNumber = numbers.first {
  ///         print(firstNumber)
  ///     }
  ///     // Prints "10"
  var first: Element? { get }

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
  ///   - n: The distance to offset `i`. `n` must not be negative unless the
  ///     collection conforms to the `BidirectionalCollection` protocol.
  /// - Returns: An index offset by `n` from the index `i`. If `n` is positive,
  ///   this is the same value as the result of `n` calls to `index(after:)`.
  ///   If `n` is negative, this is the same value as the result of `-n` calls
  ///   to `index(before:)`.
  ///
  /// - Complexity: O(1) if the collection conforms to
  ///   `RandomAccessCollection`; otherwise, O(*n*), where *n* is the absolute
  ///   value of `n`.
  func index(_ i: Index, offsetBy n: Int) -> Index

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
  ///   - n: The distance to offset `i`. `n` must not be negative unless the
  ///     collection conforms to the `BidirectionalCollection` protocol.
  ///   - limit: A valid index of the collection to use as a limit. If `n > 0`,
  ///     a limit that is less than `i` has no effect. Likewise, if `n < 0`, a
  ///     limit that is greater than `i` has no effect.
  /// - Returns: An index offset by `n` from the index `i`, unless that index
  ///   would be beyond `limit` in the direction of movement. In that case,
  ///   the method returns `nil`.
  ///
  /// - Complexity: O(1) if the collection conforms to
  ///   `RandomAccessCollection`; otherwise, O(*n*), where *n* is the absolute
  ///   value of `n`.
  func index(
    _ i: Index, offsetBy n: Int, limitedBy limit: Index
  ) -> Index?

  /// Returns the distance between two indices.
  ///
  /// Unless the collection conforms to the `BidirectionalCollection` protocol,
  /// `start` must be less than or equal to `end`.
  ///
  /// - Parameters:
  ///   - start: A valid index of the collection.
  ///   - end: Another valid index of the collection. If `end` is equal to
  ///     `start`, the result is zero.
  /// - Returns: The distance between `start` and `end`. The result can be
  ///   negative only if the collection conforms to the
  ///   `BidirectionalCollection` protocol.
  ///
  /// - Complexity: O(1) if the collection conforms to
  ///   `RandomAccessCollection`; otherwise, O(*n*), where *n* is the
  ///   resulting distance.
  func distance(from start: Index, to end: Index) -> Int

  /// Performs a range check in O(1), or a no-op when a range check is not
  /// implementable in O(1).
  ///
  /// The range check, if performed, is equivalent to:
  ///
  ///     precondition(bounds.contains(index))
  ///
  /// Use this function to perform a cheap range check for QoI purposes when
  /// memory safety is not a concern.  Do not rely on this range check for
  /// memory safety.
  ///
  /// The default implementation for forward and bidirectional indices is a
  /// no-op.  The default implementation for random access indices performs a
  /// range check.
  ///
  /// - Complexity: O(1).
  func _failEarlyRangeCheck(_ index: Index, bounds: Range<Index>)

  func _failEarlyRangeCheck(_ index: Index, bounds: ClosedRange<Index>)

  /// Performs a range check in O(1), or a no-op when a range check is not
  /// implementable in O(1).
  ///
  /// The range check, if performed, is equivalent to:
  ///
  ///     precondition(
  ///       bounds.contains(range.lowerBound) ||
  ///       range.lowerBound == bounds.upperBound)
  ///     precondition(
  ///       bounds.contains(range.upperBound) ||
  ///       range.upperBound == bounds.upperBound)
  ///
  /// Use this function to perform a cheap range check for QoI purposes when
  /// memory safety is not a concern.  Do not rely on this range check for
  /// memory safety.
  ///
  /// The default implementation for forward and bidirectional indices is a
  /// no-op.  The default implementation for random access indices performs a
  /// range check.
  ///
  /// - Complexity: O(1).
  func _failEarlyRangeCheck(_ range: Range<Index>, bounds: Range<Index>)

  /// Returns the position immediately after the given index.
  ///
  /// The successor of an index must be well defined. For an index `i` into a
  /// collection `c`, calling `c.index(after: i)` returns the same index every
  /// time.
  ///
  /// - Parameter i: A valid index of the collection. `i` must be less than
  ///   `endIndex`.
  /// - Returns: The index value immediately after `i`.
  func index(after i: Index) -> Index

  /// Replaces the given index with its successor.
  ///
  /// - Parameter i: A valid index of the collection. `i` must be less than
  ///   `endIndex`.
  func formIndex(after i: inout Index)

  @available(*, deprecated, message: "all index distances are now of type Int")
  typealias IndexDistance = Int
}

/// Default implementation for forward collections.
extension Collection {
  /// Replaces the given index with its successor.
  ///
  /// - Parameter i: A valid index of the collection. `i` must be less than
  ///   `endIndex`.
  @_inlineable // FIXME(sil-serialize-all)
  @inline(__always)
  public func formIndex(after i: inout Index) {
    i = index(after: i)
  }

  @_inlineable
  public func _failEarlyRangeCheck(_ index: Index, bounds: Range<Index>) {
    // FIXME: swift-3-indexing-model: tests.
    _precondition(
      bounds.lowerBound <= index,
      "Out of bounds: index < startIndex")
    _precondition(
      index < bounds.upperBound,
      "Out of bounds: index >= endIndex")
  }

  @_inlineable
  public func _failEarlyRangeCheck(_ index: Index, bounds: ClosedRange<Index>) {
    // FIXME: swift-3-indexing-model: tests.
    _precondition(
      bounds.lowerBound <= index,
      "Out of bounds: index < startIndex")
    _precondition(
      index <= bounds.upperBound,
      "Out of bounds: index > endIndex")
  }

  @_inlineable
  public func _failEarlyRangeCheck(_ range: Range<Index>, bounds: Range<Index>) {
    // FIXME: swift-3-indexing-model: tests.
    _precondition(
      bounds.lowerBound <= range.lowerBound,
      "Out of bounds: range begins before startIndex")
    _precondition(
      range.lowerBound <= bounds.upperBound,
      "Out of bounds: range ends after endIndex")
    _precondition(
      bounds.lowerBound <= range.upperBound,
      "Out of bounds: range ends before bounds.lowerBound")
    _precondition(
      range.upperBound <= bounds.upperBound,
      "Out of bounds: range begins after bounds.upperBound")
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
  ///   - n: The distance to offset `i`. `n` must not be negative unless the
  ///     collection conforms to the `BidirectionalCollection` protocol.
  /// - Returns: An index offset by `n` from the index `i`. If `n` is positive,
  ///   this is the same value as the result of `n` calls to `index(after:)`.
  ///   If `n` is negative, this is the same value as the result of `-n` calls
  ///   to `index(before:)`.
  ///
  /// - Complexity: O(1) if the collection conforms to
  ///   `RandomAccessCollection`; otherwise, O(*n*), where *n* is the absolute
  ///   value of `n`.
  @_inlineable
  public func index(_ i: Index, offsetBy n: Int) -> Index {
    return self._advanceForward(i, by: n)
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
  ///   - n: The distance to offset `i`. `n` must not be negative unless the
  ///     collection conforms to the `BidirectionalCollection` protocol.
  ///   - limit: A valid index of the collection to use as a limit. If `n > 0`,
  ///     a limit that is less than `i` has no effect. Likewise, if `n < 0`, a
  ///     limit that is greater than `i` has no effect.
  /// - Returns: An index offset by `n` from the index `i`, unless that index
  ///   would be beyond `limit` in the direction of movement. In that case,
  ///   the method returns `nil`.
  ///
  /// - Complexity: O(1) if the collection conforms to
  ///   `RandomAccessCollection`; otherwise, O(*n*), where *n* is the absolute
  ///   value of `n`.
  @_inlineable
  public func index(
    _ i: Index, offsetBy n: Int, limitedBy limit: Index
  ) -> Index? {
    return self._advanceForward(i, by: n, limitedBy: limit)
  }

  /// Offsets the given index by the specified distance.
  ///
  /// The value passed as `n` must not offset `i` beyond the bounds of the
  /// collection.
  ///
  /// - Parameters:
  ///   - i: A valid index of the collection.
  ///   - n: The distance to offset `i`. `n` must not be negative unless the
  ///     collection conforms to the `BidirectionalCollection` protocol.
  ///
  /// - Complexity: O(1) if the collection conforms to
  ///   `RandomAccessCollection`; otherwise, O(*n*), where *n* is the absolute
  ///   value of `n`.
  @_inlineable
  public func formIndex(_ i: inout Index, offsetBy n: Int) {
    i = index(i, offsetBy: n)
  }

  /// Offsets the given index by the specified distance, or so that it equals
  /// the given limiting index.
  ///
  /// The value passed as `n` must not offset `i` beyond the bounds of the
  /// collection, unless the index passed as `limit` prevents offsetting
  /// beyond those bounds.
  ///
  /// - Parameters:
  ///   - i: A valid index of the collection.
  ///   - n: The distance to offset `i`. `n` must not be negative unless the
  ///     collection conforms to the `BidirectionalCollection` protocol.
  ///   - limit: A valid index of the collection to use as a limit. If `n > 0`,
  ///     a limit that is less than `i` has no effect. Likewise, if `n < 0`, a
  ///     limit that is greater than `i` has no effect.
  /// - Returns: `true` if `i` has been offset by exactly `n` steps without
  ///   going beyond `limit`; otherwise, `false`. When the return value is
  ///   `false`, the value of `i` is equal to `limit`.
  ///
  /// - Complexity: O(1) if the collection conforms to
  ///   `RandomAccessCollection`; otherwise, O(*n*), where *n* is the absolute
  ///   value of `n`.
  @_inlineable
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

  /// Returns the distance between two indices.
  ///
  /// Unless the collection conforms to the `BidirectionalCollection` protocol,
  /// `start` must be less than or equal to `end`.
  ///
  /// - Parameters:
  ///   - start: A valid index of the collection.
  ///   - end: Another valid index of the collection. If `end` is equal to
  ///     `start`, the result is zero.
  /// - Returns: The distance between `start` and `end`. The result can be
  ///   negative only if the collection conforms to the
  ///   `BidirectionalCollection` protocol.
  ///
  /// - Complexity: O(1) if the collection conforms to
  ///   `RandomAccessCollection`; otherwise, O(*n*), where *n* is the
  ///   resulting distance.
  @_inlineable
  public func distance(from start: Index, to end: Index) -> Int {
    _precondition(start <= end,
      "Only BidirectionalCollections can have end come before start")

    var start = start
    var count = 0
    while start != end {
      count = count + 1
      formIndex(after: &start)
    }
    return count
  }

  /// Do not use this method directly; call advanced(by: n) instead.
  @_inlineable
  @_versioned
  @inline(__always)
  internal func _advanceForward(_ i: Index, by n: Int) -> Index {
    _precondition(n >= 0,
      "Only BidirectionalCollections can be advanced by a negative amount")

    var i = i
    for _ in stride(from: 0, to: n, by: 1) {
      formIndex(after: &i)
    }
    return i
  }

  /// Do not use this method directly; call advanced(by: n, limit) instead.
  @_inlineable
  @_versioned
  @inline(__always)
  internal func _advanceForward(
    _ i: Index, by n: Int, limitedBy limit: Index
  ) -> Index? {
    _precondition(n >= 0,
      "Only BidirectionalCollections can be advanced by a negative amount")

    var i = i
    for _ in stride(from: 0, to: n, by: 1) {
      if i == limit {
        return nil
      }
      formIndex(after: &i)
    }
    return i
  }
}

/// Supply the default `makeIterator()` method for `Collection` models
/// that accept the default associated `Iterator`,
/// `IndexingIterator<Self>`.
extension Collection where Iterator == IndexingIterator<Self> {
  /// Returns an iterator over the elements of the collection.
  @_inlineable // FIXME(sil-serialize-all)
  @inline(__always)
  public func makeIterator() -> IndexingIterator<Self> {
    return IndexingIterator(_elements: self)
  }
}

/// Supply the default "slicing" `subscript` for `Collection` models
/// that accept the default associated `SubSequence`, `Slice<Self>`.
extension Collection where SubSequence == Slice<Self> {
  /// Accesses a contiguous subrange of the collection's elements.
  ///
  /// The accessed slice uses the same indices for the same elements as the
  /// original collection. Always use the slice's `startIndex` property
  /// instead of assuming that its indices start at a particular value.
  ///
  /// This example demonstrates getting a slice of an array of strings, finding
  /// the index of one of the strings in the slice, and then using that index
  /// in the original array.
  ///
  ///     let streets = ["Adams", "Bryant", "Channing", "Douglas", "Evarts"]
  ///     let streetsSlice = streets[2 ..< streets.endIndex]
  ///     print(streetsSlice)
  ///     // Prints "["Channing", "Douglas", "Evarts"]"
  ///
  ///     let index = streetsSlice.index(of: "Evarts")    // 4
  ///     print(streets[index!])
  ///     // Prints "Evarts"
  ///
  /// - Parameter bounds: A range of the collection's indices. The bounds of
  ///   the range must be valid indices of the collection.
  ///
  /// - Complexity: O(1)
  @_inlineable
  public subscript(bounds: Range<Index>) -> Slice<Self> {
    _failEarlyRangeCheck(bounds, bounds: startIndex..<endIndex)
    return Slice(base: self, bounds: bounds)
  }
}

extension Collection where SubSequence == Self {
  /// Removes and returns the first element of the collection.
  ///
  /// - Returns: The first element of the collection if the collection is
  ///   not empty; otherwise, `nil`.
  ///
  /// - Complexity: O(1)
  @_inlineable
  public mutating func popFirst() -> Element? {
    // TODO: swift-3-indexing-model - review the following
    guard !isEmpty else { return nil }
    let element = first!
    self = self[index(after: startIndex)..<endIndex]
    return element
  }
}

/// Default implementations of core requirements
extension Collection {
  /// A Boolean value indicating whether the collection is empty.
  ///
  /// When you need to check whether your collection is empty, use the
  /// `isEmpty` property instead of checking that the `count` property is
  /// equal to zero. For collections that don't conform to
  /// `RandomAccessCollection`, accessing the `count` property iterates
  /// through the elements of the collection.
  ///
  ///     let horseName = "Silver"
  ///     if horseName.isEmpty {
  ///         print("I've been through the desert on a horse with no name.")
  ///     } else {
  ///         print("Hi ho, \(horseName)!")
  ///     }
  ///     // Prints "Hi ho, Silver!")
  ///
  /// - Complexity: O(1)
  @_inlineable
  public var isEmpty: Bool {
    return startIndex == endIndex
  }

  /// The first element of the collection.
  ///
  /// If the collection is empty, the value of this property is `nil`.
  ///
  ///     let numbers = [10, 20, 30, 40, 50]
  ///     if let firstNumber = numbers.first {
  ///         print(firstNumber)
  ///     }
  ///     // Prints "10"
  @_inlineable
  public var first: Element? {
    @inline(__always)
    get {
      // NB: Accessing `startIndex` may not be O(1) for some lazy collections,
      // so instead of testing `isEmpty` and then returning the first element,
      // we'll just rely on the fact that the iterator always yields the
      // first element first.
      var i = makeIterator()
      return i.next()
    }
  }
  
  // TODO: swift-3-indexing-model - uncomment and replace above ready (or should we still use the iterator one?)
  /// Returns the first element of `self`, or `nil` if `self` is empty.
  ///
  /// - Complexity: O(1)
  //  public var first: Element? {
  //    return isEmpty ? nil : self[startIndex]
  //  }

  /// A value less than or equal to the number of elements in the collection.
  ///
  /// - Complexity: O(1) if the collection conforms to
  ///   `RandomAccessCollection`; otherwise, O(*n*), where *n* is the length
  ///   of the collection.
  @_inlineable
  public var underestimatedCount: Int {
    // TODO: swift-3-indexing-model - review the following
    return numericCast(count)
  }

  /// The number of elements in the collection.
  ///
  /// To check whether a collection is empty, use its `isEmpty` property
  /// instead of comparing `count` to zero. Unless the collection guarantees
  /// random-access performance, calculating `count` can be an O(*n*)
  /// operation.
  ///
  /// - Complexity: O(1) if the collection conforms to
  ///   `RandomAccessCollection`; otherwise, O(*n*), where *n* is the length
  ///   of the collection.
  @_inlineable
  public var count: Int {
    return distance(from: startIndex, to: endIndex)
  }

  // TODO: swift-3-indexing-model - rename the following to _customIndexOfEquatable(element)?
  /// Customization point for `Collection.index(of:)`.
  ///
  /// Define this method if the collection can find an element in less than
  /// O(*n*) by exploiting collection-specific knowledge.
  ///
  /// - Returns: `nil` if a linear search should be attempted instead,
  ///   `Optional(nil)` if the element was not found, or
  ///   `Optional(Optional(index))` if an element was found.
  ///
  /// - Complexity: O(`count`).
  @_inlineable
  public // dispatching
  func _customIndexOfEquatableElement(_: Iterator.Element) -> Index?? {
    return nil
  }
}

//===----------------------------------------------------------------------===//
// Default implementations for Collection
//===----------------------------------------------------------------------===//

extension Collection {
  /// Returns an array containing the results of mapping the given closure
  /// over the sequence's elements.
  ///
  /// In this example, `map` is used first to convert the names in the array
  /// to lowercase strings and then to count their characters.
  ///
  ///     let cast = ["Vivien", "Marlon", "Kim", "Karl"]
  ///     let lowercaseNames = cast.map { $0.lowercaseString }
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
    // TODO: swift-3-indexing-model - review the following
    let count: Int = numericCast(self.count)
    if count == 0 {
      return []
    }

    var result = ContiguousArray<T>()
    result.reserveCapacity(count)

    var i = self.startIndex

    for _ in 0..<count {
      result.append(try transform(self[i]))
      formIndex(after: &i)
    }

    _expectEnd(of: self, is: i)
    return Array(result)
  }

  /// Returns a subsequence containing all but the given number of initial
  /// elements.
  ///
  /// If the number of elements to drop exceeds the number of elements in
  /// the collection, the result is an empty subsequence.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.dropFirst(2))
  ///     // Prints "[3, 4, 5]"
  ///     print(numbers.dropFirst(10))
  ///     // Prints "[]"
  ///
  /// - Parameter n: The number of elements to drop from the beginning of
  ///   the collection. `n` must be greater than or equal to zero.
  /// - Returns: A subsequence starting after the specified number of
  ///   elements.
  ///
  /// - Complexity: O(*n*), where *n* is the number of elements to drop from
  ///   the beginning of the collection.
  @_inlineable
  public func dropFirst(_ n: Int) -> SubSequence {
    _precondition(n >= 0, "Can't drop a negative number of elements from a collection")
    let start = index(startIndex,
      offsetBy: numericCast(n), limitedBy: endIndex) ?? endIndex
    return self[start..<endIndex]
  }

  /// Returns a subsequence containing all but the specified number of final
  /// elements.
  ///
  /// If the number of elements to drop exceeds the number of elements in the
  /// collection, the result is an empty subsequence.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.dropLast(2))
  ///     // Prints "[1, 2, 3]"
  ///     print(numbers.dropLast(10))
  ///     // Prints "[]"
  ///
  /// - Parameter n: The number of elements to drop off the end of the
  ///   collection. `n` must be greater than or equal to zero.
  /// - Returns: A subsequence that leaves off the specified number of elements
  ///   at the end.
  ///
  /// - Complexity: O(*n*), where *n* is the length of the collection.
  @_inlineable
  public func dropLast(_ n: Int) -> SubSequence {
    _precondition(
      n >= 0, "Can't drop a negative number of elements from a collection")
    let amount = Swift.max(0, numericCast(count) - n)
    let end = index(startIndex,
      offsetBy: numericCast(amount), limitedBy: endIndex) ?? endIndex
    return self[startIndex..<end]
  }
  
  /// Returns a subsequence by skipping elements while `predicate` returns
  /// `true` and returning the remaining elements.
  ///
  /// - Parameter predicate: A closure that takes an element of the
  ///   sequence as its argument and returns `true` if the element should
  ///   be skipped or `false` if it should be included. Once the predicate
  ///   returns `false` it will not be called again.
  ///
  /// - Complexity: O(*n*), where *n* is the length of the collection.
  @_inlineable
  public func drop(
    while predicate: (Element) throws -> Bool
  ) rethrows -> SubSequence {
    var start = startIndex
    while try start != endIndex && predicate(self[start]) {
      formIndex(after: &start)
    } 
    return self[start..<endIndex]
  }

  /// Returns a subsequence, up to the specified maximum length, containing
  /// the initial elements of the collection.
  ///
  /// If the maximum length exceeds the number of elements in the collection,
  /// the result contains all the elements in the collection.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.prefix(2))
  ///     // Prints "[1, 2]"
  ///     print(numbers.prefix(10))
  ///     // Prints "[1, 2, 3, 4, 5]"
  ///
  /// - Parameter maxLength: The maximum number of elements to return.
  ///   `maxLength` must be greater than or equal to zero.
  /// - Returns: A subsequence starting at the beginning of this collection
  ///   with at most `maxLength` elements.
  @_inlineable
  public func prefix(_ maxLength: Int) -> SubSequence {
    _precondition(
      maxLength >= 0,
      "Can't take a prefix of negative length from a collection")
    let end = index(startIndex,
      offsetBy: numericCast(maxLength), limitedBy: endIndex) ?? endIndex
    return self[startIndex..<end]
  }
  
  /// Returns a subsequence containing the initial elements until `predicate`
  /// returns `false` and skipping the remaining elements.
  ///
  /// - Parameter predicate: A closure that takes an element of the
  ///   sequence as its argument and returns `true` if the element should
  ///   be included or `false` if it should be excluded. Once the predicate
  ///   returns `false` it will not be called again.
  ///
  /// - Complexity: O(*n*), where *n* is the length of the collection.
  @_inlineable
  public func prefix(
    while predicate: (Element) throws -> Bool
  ) rethrows -> SubSequence {
    var end = startIndex
    while try end != endIndex && predicate(self[end]) {
      formIndex(after: &end)
    }
    return self[startIndex..<end]
  }

  /// Returns a subsequence, up to the given maximum length, containing the
  /// final elements of the collection.
  ///
  /// If the maximum length exceeds the number of elements in the collection,
  /// the result contains all the elements in the collection.
  ///
  ///     let numbers = [1, 2, 3, 4, 5]
  ///     print(numbers.suffix(2))
  ///     // Prints "[4, 5]"
  ///     print(numbers.suffix(10))
  ///     // Prints "[1, 2, 3, 4, 5]"
  ///
  /// - Parameter maxLength: The maximum number of elements to return. The
  ///   value of `maxLength` must be greater than or equal to zero.
  /// - Returns: A subsequence terminating at the end of the collection with at
  ///   most `maxLength` elements.
  ///
  /// - Complexity: O(*n*), where *n* is the length of the collection.
  @_inlineable
  public func suffix(_ maxLength: Int) -> SubSequence {
    _precondition(
      maxLength >= 0,
      "Can't take a suffix of negative length from a collection")
    let amount = Swift.max(0, numericCast(count) - maxLength)
    let start = index(startIndex,
      offsetBy: numericCast(amount), limitedBy: endIndex) ?? endIndex
    return self[start..<endIndex]
  }

  /// Returns a subsequence from the start of the collection up to, but not
  /// including, the specified position.
  ///
  /// The resulting subsequence *does not include* the element at the position
  /// `end`. The following example searches for the index of the number `40`
  /// in an array of integers, and then prints the prefix of the array up to,
  /// but not including, that index:
  ///
  ///     let numbers = [10, 20, 30, 40, 50, 60]
  ///     if let i = numbers.index(of: 40) {
  ///         print(numbers.prefix(upTo: i))
  ///     }
  ///     // Prints "[10, 20, 30]"
  ///
  /// Passing the collection's starting index as the `end` parameter results in
  /// an empty subsequence.
  ///
  ///     print(numbers.prefix(upTo: numbers.startIndex))
  ///     // Prints "[]"
  ///
  /// Using the `prefix(upTo:)` method is equivalent to using a partial
  /// half-open range as the collection's subscript. The subscript notation is
  /// preferred over `prefix(upTo:)`.
  ///
  ///     if let i = numbers.index(of: 40) {
  ///         print(numbers[..<i])
  ///     }
  ///     // Prints "[10, 20, 30]"
  ///
  /// - Parameter end: The "past the end" index of the resulting subsequence.
  ///   `end` must be a valid index of the collection.
  /// - Returns: A subsequence up to, but not including, the `end` position.
  ///
  /// - Complexity: O(1)
  @_inlineable
  public func prefix(upTo end: Index) -> SubSequence {
    return self[startIndex..<end]
  }

  /// Returns a subsequence from the specified position to the end of the
  /// collection.
  ///
  /// The following example searches for the index of the number `40` in an
  /// array of integers, and then prints the suffix of the array starting at
  /// that index:
  ///
  ///     let numbers = [10, 20, 30, 40, 50, 60]
  ///     if let i = numbers.index(of: 40) {
  ///         print(numbers.suffix(from: i))
  ///     }
  ///     // Prints "[40, 50, 60]"
  ///
  /// Passing the collection's `endIndex` as the `start` parameter results in
  /// an empty subsequence.
  ///
  ///     print(numbers.suffix(from: numbers.endIndex))
  ///     // Prints "[]"
  ///
  /// Using the `suffix(from:)` method is equivalent to using a partial range
  /// from the index as the collection's subscript. The subscript notation is
  /// preferred over `suffix(from:)`.
  ///
  ///     if let i = numbers.index(of: 40) {
  ///         print(numbers[i...])
  ///     }
  ///     // Prints "[40, 50, 60]"
  ///
  /// - Parameter start: The index at which to start the resulting subsequence.
  ///   `start` must be a valid index of the collection.
  /// - Returns: A subsequence starting at the `start` position.
  ///
  /// - Complexity: O(1)
  @_inlineable
  public func suffix(from start: Index) -> SubSequence {
    return self[start..<endIndex]
  }

  /// Returns a subsequence from the start of the collection through the
  /// specified position.
  ///
  /// The resulting subsequence *includes* the element at the position `end`. 
  /// The following example searches for the index of the number `40` in an
  /// array of integers, and then prints the prefix of the array up to, and
  /// including, that index:
  ///
  ///     let numbers = [10, 20, 30, 40, 50, 60]
  ///     if let i = numbers.index(of: 40) {
  ///         print(numbers.prefix(through: i))
  ///     }
  ///     // Prints "[10, 20, 30, 40]"
  ///
  /// Using the `prefix(through:)` method is equivalent to using a partial
  /// closed range as the collection's subscript. The subscript notation is
  /// preferred over `prefix(through:)`.
  ///
  ///     if let i = numbers.index(of: 40) {
  ///         print(numbers[...i])
  ///     }
  ///     // Prints "[10, 20, 30, 40]"
  ///
  /// - Parameter end: The index of the last element to include in the
  ///   resulting subsequence. `end` must be a valid index of the collection
  ///   that is not equal to the `endIndex` property.
  /// - Returns: A subsequence up to, and including, the `end` position.
  ///
  /// - Complexity: O(1)
  @_inlineable
  public func prefix(through position: Index) -> SubSequence {
    return prefix(upTo: index(after: position))
  }

  /// Returns the longest possible subsequences of the collection, in order,
  /// that don't contain elements satisfying the given predicate.
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
  ///     print(line.split(whereSeparator: { $0 == " " }))
  ///     // Prints "["BLANCHE:", "I", "don\'t", "want", "realism.", "I", "want", "magic!"]"
  ///
  /// The second example passes `1` for the `maxSplits` parameter, so the
  /// original string is split just once, into two new strings.
  ///
  ///     print(line.split(maxSplits: 1, whereSeparator: { $0 == " " }))
  ///     // Prints "["BLANCHE:", "  I don\'t want realism. I want magic!"]"
  ///
  /// The final example passes `false` for the `omittingEmptySubsequences`
  /// parameter, so the returned array contains empty strings where spaces
  /// were repeated.
  ///
  ///     print(line.split(omittingEmptySubsequences: false, whereSeparator: { $0 == " " }))
  ///     // Prints "["BLANCHE:", "", "", "I", "don\'t", "want", "realism.", "I", "want", "magic!"]"
  ///
  /// - Parameters:
  ///   - maxSplits: The maximum number of times to split the collection, or
  ///     one less than the number of subsequences to return. If
  ///     `maxSplits + 1` subsequences are returned, the last one is a suffix
  ///     of the original collection containing the remaining elements.
  ///     `maxSplits` must be greater than or equal to zero. The default value
  ///     is `Int.max`.
  ///   - omittingEmptySubsequences: If `false`, an empty subsequence is
  ///     returned in the result for each pair of consecutive elements
  ///     satisfying the `isSeparator` predicate and for each element at the
  ///     start or end of the collection satisfying the `isSeparator`
  ///     predicate. The default value is `true`.
  ///   - isSeparator: A closure that takes an element as an argument and
  ///     returns a Boolean value indicating whether the collection should be
  ///     split at that element.
  /// - Returns: An array of subsequences, split from this collection's
  ///   elements.
  @_inlineable
  public func split(
    maxSplits: Int = Int.max,
    omittingEmptySubsequences: Bool = true,
    whereSeparator isSeparator: (Element) throws -> Bool
  ) rethrows -> [SubSequence] {
    // TODO: swift-3-indexing-model - review the following
    _precondition(maxSplits >= 0, "Must take zero or more splits")

    var result: [SubSequence] = []
    var subSequenceStart: Index = startIndex

    func appendSubsequence(end: Index) -> Bool {
      if subSequenceStart == end && omittingEmptySubsequences {
        return false
      }
      result.append(self[subSequenceStart..<end])
      return true
    }

    if maxSplits == 0 || isEmpty {
      _ = appendSubsequence(end: endIndex)
      return result
    }

    var subSequenceEnd = subSequenceStart
    let cachedEndIndex = endIndex
    while subSequenceEnd != cachedEndIndex {
      if try isSeparator(self[subSequenceEnd]) {
        let didAppend = appendSubsequence(end: subSequenceEnd)
        formIndex(after: &subSequenceEnd)
        subSequenceStart = subSequenceEnd
        if didAppend && result.count == maxSplits {
          break
        }
        continue
      }
      formIndex(after: &subSequenceEnd)
    }

    if subSequenceStart != cachedEndIndex || !omittingEmptySubsequences {
      result.append(self[subSequenceStart..<cachedEndIndex])
    }

    return result
  }
}

extension Collection where Element : Equatable {
  /// Returns the longest possible subsequences of the collection, in order,
  /// around elements equal to the given element.
  ///
  /// The resulting array consists of at most `maxSplits + 1` subsequences.
  /// Elements that are used to split the collection are not returned as part
  /// of any subsequence.
  ///
  /// The following examples show the effects of the `maxSplits` and
  /// `omittingEmptySubsequences` parameters when splitting a string at each
  /// space character (" "). The first use of `split` returns each word that
  /// was originally separated by one or more spaces.
  ///
  ///     let line = "BLANCHE:   I don't want realism. I want magic!"
  ///     print(line.split(separator: " "))
  ///     // Prints "["BLANCHE:", "I", "don\'t", "want", "realism.", "I", "want", "magic!"]"
  ///
  /// The second example passes `1` for the `maxSplits` parameter, so the
  /// original string is split just once, into two new strings.
  ///
  ///     print(line.split(separator: " ", maxSplits: 1))
  ///     // Prints "["BLANCHE:", "  I don\'t want realism. I want magic!"]"
  ///
  /// The final example passes `false` for the `omittingEmptySubsequences`
  /// parameter, so the returned array contains empty strings where spaces
  /// were repeated.
  ///
  ///     print(line.split(separator: " ", omittingEmptySubsequences: false))
  ///     // Prints "["BLANCHE:", "", "", "I", "don\'t", "want", "realism.", "I", "want", "magic!"]"
  ///
  /// - Parameters:
  ///   - separator: The element that should be split upon.
  ///   - maxSplits: The maximum number of times to split the collection, or
  ///     one less than the number of subsequences to return. If
  ///     `maxSplits + 1` subsequences are returned, the last one is a suffix
  ///     of the original collection containing the remaining elements.
  ///     `maxSplits` must be greater than or equal to zero. The default value
  ///     is `Int.max`.
  ///   - omittingEmptySubsequences: If `false`, an empty subsequence is
  ///     returned in the result for each consecutive pair of `separator`
  ///     elements in the collection and for each instance of `separator` at
  ///     the start or end of the collection. If `true`, only nonempty
  ///     subsequences are returned. The default value is `true`.
  /// - Returns: An array of subsequences, split from this collection's
  ///   elements.
  @_inlineable
  public func split(
    separator: Element,
    maxSplits: Int = Int.max,
    omittingEmptySubsequences: Bool = true
  ) -> [SubSequence] {
    // TODO: swift-3-indexing-model - review the following
    return split(
      maxSplits: maxSplits,
      omittingEmptySubsequences: omittingEmptySubsequences,
      whereSeparator: { $0 == separator })
  }
}

extension Collection where SubSequence == Self {
  /// Removes and returns the first element of the collection.
  ///
  /// The collection must not be empty.
  ///
  /// - Returns: The first element of the collection.
  ///
  /// - Complexity: O(1)
  @_inlineable
  @discardableResult
  public mutating func removeFirst() -> Element {
    // TODO: swift-3-indexing-model - review the following
    _precondition(!isEmpty, "Can't remove items from an empty collection")
    let element = first!
    self = self[index(after: startIndex)..<endIndex]
    return element
  }

  /// Removes the specified number of elements from the beginning of the
  /// collection.
  ///
  /// - Parameter n: The number of elements to remove. `n` must be greater than
  ///   or equal to zero, and must be less than or equal to the number of
  ///   elements in the collection.
  ///
  /// - Complexity: O(1) if the collection conforms to
  ///   `RandomAccessCollection`; otherwise, O(*n*).
  @_inlineable
  public mutating func removeFirst(_ n: Int) {
    if n == 0 { return }
    _precondition(n >= 0, "Number of elements to remove should be non-negative")
    _precondition(count >= numericCast(n),
      "Can't remove more items from a collection than it contains")
    self = self[index(startIndex, offsetBy: numericCast(n))..<endIndex]
  }
}

extension Collection {
  @_inlineable
  public func _preprocessingPass<R>(
    _ preprocess: () throws -> R
  ) rethrows -> R? {
    return try preprocess()
  }
}

extension Collection {
  // FIXME: <rdar://problem/34142121>
  // This typealias should be removed as it predates the source compatibility
  // guarantees of Swift 3, but it cannot due to a bug.
  @available(*, unavailable, renamed: "Iterator")
  public typealias Generator = Iterator

  @available(swift, deprecated: 3.2, renamed: "Element")
  public typealias _Element = Element

  @available(*, deprecated, message: "all index distances are now of type Int")
  public func index<T: BinaryInteger>(_ i: Index, offsetBy n: T) -> Index {
    return index(i, offsetBy: Int(n))
  }
  @available(*, deprecated, message: "all index distances are now of type Int")
  public func formIndex<T: BinaryInteger>(_ i: inout Index, offsetBy n: T) {
    return formIndex(&i, offsetBy: Int(n))
  }
  @available(*, deprecated, message: "all index distances are now of type Int")
  public func index<T: BinaryInteger>(_ i: Index, offsetBy n: T, limitedBy limit: Index) -> Index? {
    return index(i, offsetBy: Int(n), limitedBy: limit)
  }
  @available(*, deprecated, message: "all index distances are now of type Int")
  public func formIndex<T: BinaryInteger>(_ i: inout Index, offsetBy n: T, limitedBy limit: Index) -> Bool {
    return formIndex(&i, offsetBy: Int(n), limitedBy: limit)
  }
  @available(*, deprecated, message: "all index distances are now of type Int")
  public func distance<T: BinaryInteger>(from start: Index, to end: Index) -> T {
    return numericCast(distance(from: start, to: end) as Int)
  }
}
