//===--- ClosedRange.swift ------------------------------------------------===//
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

// FIXME: swift-3-indexing-model: Generalize all tests to check both
// [Closed]Range and [Closed]CountableRange.

@_versioned
internal enum _ClosedRangeIndexRepresentation<Bound>
  where
  Bound : Strideable, Bound.Stride : BinaryInteger {
  case pastEnd
  case inRange(Bound)
}

// FIXME(ABI)#23 (Nesting types in generics): should be a nested type in
// `ClosedRange`.
/// A position in a `CountableClosedRange` instance.
@_fixed_layout
public struct ClosedRangeIndex<Bound: Strideable>
where Bound.Stride : SignedInteger {
  @_versioned
  internal var _value: _ClosedRangeIndexRepresentation<Bound>

  /// Creates the "past the end" position.
  @_inlineable
  @_versioned
  internal init() { _value = .pastEnd }

  /// Creates a position `p` for which `r[p] == x`.
  @_inlineable
  @_versioned
  internal init(_ x: Bound) { _value = .inRange(x) }

  @_inlineable
  @_versioned
  internal var _dereferenced: Bound {
    switch _value {
    case .inRange(let x): return x
    case .pastEnd: _preconditionFailure("Index out of range")
    }
  }
}

extension ClosedRangeIndex : Comparable {
  @_inlineable
  public static func == (
    lhs: ClosedRangeIndex<Bound>,
    rhs: ClosedRangeIndex<Bound>
  ) -> Bool {
    switch (lhs._value, rhs._value) {
    case (.inRange(let l), .inRange(let r)):
      return l == r
    case (.pastEnd, .pastEnd):
      return true
    default:
      return false
    }
  }

  @_inlineable
  public static func < (
    lhs: ClosedRangeIndex<Bound>,
    rhs: ClosedRangeIndex<Bound>
  ) -> Bool {
    switch (lhs._value, rhs._value) {
    case (.inRange(let l), .inRange(let r)):
      return l < r
    case (.inRange(_), .pastEnd):
      return true
    default:
      return false
    }
  }
}

extension ClosedRangeIndex : Hashable where Bound : Hashable {
  public var hashValue: Int {
    switch _value {
    case .inRange(let value):
      return value.hashValue
    case .pastEnd:
      return .max
    }
  }
}

/// A closed range that forms a collection of consecutive values.
///
/// You create a `CountableClosedRange` instance by using the closed range
/// operator (`...`).
///
///     let throughFive = 0...5
///
/// A `CountableClosedRange` instance contains both its lower bound and its
/// upper bound.
///
///     throughFive.contains(3)
///     // true
///     throughFive.contains(10)
///     // false
///     throughFive.contains(5)
///     // true
///
/// Because a closed range includes its upper bound, a closed range whose lower
/// bound is equal to the upper bound contains that value. Therefore, a
/// `CountableClosedRange` instance cannot represent an empty range.
///
///     let zeroInclusive = 0...0
///     zeroInclusive.contains(0)
///     // true
///     zeroInclusive.isEmpty
///     // false
///
/// You can use a `for`-`in` loop or any sequence or collection method with a
/// countable range. The elements of the range are the consecutive values from
/// its lower bound up to, and including, its upper bound.
///
///     for n in 3...5 {
///         print(n)
///     }
///     // Prints "3"
///     // Prints "4"
///     // Prints "5"
///
/// You can create a countable range over any type that conforms to the
/// `Strideable` protocol and uses an integer as its associated `Stride` type.
/// By default, Swift's integer and pointer types are usable as the bounds of
/// a countable range.
///
/// Because floating-point types such as `Float` and `Double` are their own
/// `Stride` types, they cannot be used as the bounds of a countable range. If
/// you need to test whether values are contained within a closed interval
/// bound by floating-point values, see the `ClosedRange` type. If you need to
/// iterate over consecutive floating-point values, see the
/// `stride(from:through:by:)` function.
@_fixed_layout
public struct CountableClosedRange<Bound: Strideable>
where Bound.Stride : SignedInteger {

  /// The range's lower bound.
  public let lowerBound: Bound

  /// The range's upper bound.
  ///
  /// `upperBound` is always reachable from `lowerBound` by zero or
  /// more applications of `index(after:)`.
  public let upperBound: Bound

  /// Creates an instance with the given bounds.
  ///
  /// Because this initializer does not perform any checks, it should be used
  /// as an optimization only when you are absolutely certain that `lower` is
  /// less than or equal to `upper`. Using the closed range operator (`...`)
  /// to form `CountableClosedRange` instances is preferred.
  ///
  /// - Parameter bounds: A tuple of the lower and upper bounds of the range.
  @_inlineable
  public init(uncheckedBounds bounds: (lower: Bound, upper: Bound)) {
    self.lowerBound = bounds.lower
    self.upperBound = bounds.upper
  }
}

extension CountableClosedRange: RandomAccessCollection {
  /// The element type of the range; the same type as the range's bounds.
  public typealias Element = Bound

  /// A type that represents a position in the range.
  public typealias Index = ClosedRangeIndex<Bound>

  /// The position of the first element in the range.
  @_inlineable
  public var startIndex: ClosedRangeIndex<Bound> {
    return ClosedRangeIndex(lowerBound)
  }

  /// The range's "past the end" position---that is, the position one greater
  /// than the last valid subscript argument.
  @_inlineable
  public var endIndex: ClosedRangeIndex<Bound> {
    return ClosedRangeIndex()
  }

  @_inlineable
  public func index(after i: Index) -> Index {
    switch i._value {
    case .inRange(let x):
      return x == upperBound
        ? ClosedRangeIndex() 
        : ClosedRangeIndex(x.advanced(by: 1))
    case .pastEnd: 
      _preconditionFailure("Incrementing past end index")
    }
  }

  @_inlineable
  public func index(before i: Index) -> Index {
    switch i._value {
    case .inRange(let x):
      _precondition(x > lowerBound, "Incrementing past start index")
      return ClosedRangeIndex(x.advanced(by: -1))
    case .pastEnd: 
      _precondition(upperBound >= lowerBound, "Incrementing past start index")
      return ClosedRangeIndex(upperBound)
    }
  }

  @_inlineable
  public func index(_ i: Index, offsetBy n: Int) -> Index {
    switch i._value {
    case .inRange(let x):
      let d = x.distance(to: upperBound)
      if n <= d {
        let newPosition = x.advanced(by: numericCast(n))
        _precondition(newPosition >= lowerBound,
          "Advancing past start index")
        return ClosedRangeIndex(newPosition)
      }
      if d - -1 == n { return ClosedRangeIndex() }
      _preconditionFailure("Advancing past end index")
    case .pastEnd:
      if n == 0 {
        return i
      } 
      if n < 0 {
        return index(ClosedRangeIndex(upperBound), offsetBy: numericCast(n + 1))
      }
      _preconditionFailure("Advancing past end index")
    }
  }

  @_inlineable
  public func distance(from start: Index, to end: Index) -> Int {
    switch (start._value, end._value) {
    case let (.inRange(left), .inRange(right)):
      // in range <--> in range
      return numericCast(left.distance(to: right))
    case let (.inRange(left), .pastEnd):
      // in range --> end
      return numericCast(1 + left.distance(to: upperBound))
    case let (.pastEnd, .inRange(right)):
      // in range <-- end
      return numericCast(upperBound.distance(to: right) - 1)
    case (.pastEnd, .pastEnd):
      // end <--> end
      return 0
    }
  }

  /// Accesses the element at specified position.
  ///
  /// You can subscript a collection with any valid index other than the
  /// collection's end index. The end index refers to the position one past
  /// the last element of a collection, so it doesn't correspond with an
  /// element.
  ///
  /// - Parameter position: The position of the element to access. `position`
  ///   must be a valid index of the range, and must not equal the range's end
  ///   index.
  @_inlineable
  public subscript(position: ClosedRangeIndex<Bound>) -> Bound {
    // FIXME: swift-3-indexing-model: range checks and tests.
    return position._dereferenced
  }

  @_inlineable
  public subscript(bounds: Range<Index>)
    -> Slice<CountableClosedRange<Bound>> {
    return Slice(base: self, bounds: bounds)
  }

  @_inlineable
  public func _customContainsEquatableElement(_ element: Bound) -> Bool? {
    return element >= self.lowerBound && element <= self.upperBound
  }

  /// A Boolean value indicating whether the range contains no elements.
  ///
  /// Because a closed range cannot represent an empty range, this property is
  /// always `false`.
  @_inlineable
  public var isEmpty: Bool {
    return false
  }

  /// Returns a Boolean value indicating whether the given element is contained
  /// within the range.
  ///
  /// A `CountableClosedRange` instance contains both its lower and upper bound.
  /// `element` is contained in the range if it is between the two bounds or
  /// equal to either bound.
  ///
  /// - Parameter element: The element to check for containment.
  /// - Returns: `true` if `element` is contained in the range; otherwise,
  ///   `false`.
  @_inlineable
  public func contains(_ element: Bound) -> Bool {
    return element >= self.lowerBound && element <= self.upperBound
  }
}

/// An interval over a comparable type, from a lower bound up to, and
/// including, an upper bound.
///
/// You create instances of `ClosedRange` by using the closed range operator
/// (`...`).
///
///     let lowercase = "a"..."z"
///
/// You can use a `ClosedRange` instance to quickly check if a value is
/// contained in a particular range of values. For example:
///
///     print(lowercase.contains("c"))      // Prints "true"
///     print(lowercase.contains("5"))      // Prints "false"
///     print(lowercase.contains("z"))      // Prints "true"
///
/// Unlike `Range`, instances of `ClosedRange` cannot represent an empty
/// interval.
///
///     let lowercaseA = "a"..."a"
///     print(lowercaseA.isEmpty)
///     // Prints "false"
@_fixed_layout
public struct ClosedRange<Bound : Comparable> {

  /// The range's lower bound.
  public let lowerBound: Bound

  /// The range's upper bound.
  public let upperBound: Bound

  /// Creates an instance with the given bounds.
  ///
  /// Because this initializer does not perform any checks, it should be used
  /// as an optimization only when you are absolutely certain that `lower` is
  /// less than or equal to `upper`. Using the closed range operator (`...`)
  /// to form `ClosedRange` instances is preferred.
  ///
  /// - Parameter bounds: A tuple of the lower and upper bounds of the range.
  @inline(__always)
  public init(uncheckedBounds bounds: (lower: Bound, upper: Bound)) {
    self.lowerBound = bounds.lower
    self.upperBound = bounds.upper
  }

  /// Returns a Boolean value indicating whether the given element is contained
  /// within the range.
  ///
  /// A `ClosedRange` instance contains both its lower and upper bound.
  /// `element` is contained in the range if it is between the two bounds or
  /// equal to either bound.
  ///
  /// - Parameter element: The element to check for containment.
  /// - Returns: `true` if `element` is contained in the range; otherwise,
  ///   `false`.
  @_inlineable
  public func contains(_ element: Bound) -> Bool {
    return element >= self.lowerBound && element <= self.upperBound
  }

  /// A Boolean value indicating whether the range contains no elements.
  ///
  /// Because a closed range cannot represent an empty range, this property is
  /// always `false`.
  @_inlineable
  public var isEmpty: Bool {
    return false
  }
}

extension Comparable {  
  /// Returns a closed range that contains both of its bounds.
  ///
  /// Use the closed range operator (`...`) to create a closed range of any type
  /// that conforms to the `Comparable` protocol. This example creates a
  /// `ClosedRange<Character>` from "a" up to, and including, "z".
  ///
  ///     let lowercase = "a"..."z"
  ///     print(lowercase.contains("z"))
  ///     // Prints "true"
  ///
  /// - Parameters:
  ///   - minimum: The lower bound for the range.
  ///   - maximum: The upper bound for the range.
  @_inlineable // FIXME(sil-serialize-all)
  @_transparent
  public static func ... (minimum: Self, maximum: Self) -> ClosedRange<Self> {
    _precondition(
      minimum <= maximum, "Can't form Range with upperBound < lowerBound")
    return ClosedRange(uncheckedBounds: (lower: minimum, upper: maximum))
  }
}

extension Strideable where Stride: SignedInteger {  
  /// Returns a countable closed range that contains both of its bounds.
  ///
  /// Use the closed range operator (`...`) to create a closed range of any type
  /// that conforms to the `Strideable` protocol with an associated signed
  /// integer `Stride` type, such as any of the standard library's integer
  /// types. This example creates a `CountableClosedRange<Int>` from zero up to,
  /// and including, nine.
  ///
  ///     let singleDigits = 0...9
  ///     print(singleDigits.contains(9))
  ///     // Prints "true"
  ///
  /// You can use sequence or collection methods on the `singleDigits` range.
  ///
  ///     print(singleDigits.count)
  ///     // Prints "10"
  ///     print(singleDigits.last)
  ///     // Prints "9"
  ///
  /// - Parameters:
  ///   - minimum: The lower bound for the range.
  ///   - maximum: The upper bound for the range.
  @_inlineable // FIXME(sil-serialize-all)
  @_transparent
  public static func ... (
    minimum: Self, maximum: Self
  ) -> CountableClosedRange<Self> {
    // FIXME: swift-3-indexing-model: tests for traps.
    _precondition(
      minimum <= maximum, "Can't form Range with upperBound < lowerBound")
    return CountableClosedRange(uncheckedBounds: (lower: minimum, upper: maximum))
  }
}
