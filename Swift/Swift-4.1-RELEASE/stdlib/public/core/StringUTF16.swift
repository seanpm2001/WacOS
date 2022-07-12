//===--- StringUTF16.swift ------------------------------------------------===//
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

// FIXME(ABI)#71 : The UTF-16 string view should have a custom iterator type to
// allow performance optimizations of linear traversals.

extension String {
  /// A view of a string's contents as a collection of UTF-16 code units.
  ///
  /// You can access a string's view of UTF-16 code units by using its `utf16`
  /// property. A string's UTF-16 view encodes the string's Unicode scalar
  /// values as 16-bit integers.
  ///
  ///     let flowers = "Flowers 💐"
  ///     for v in flowers.utf16 {
  ///         print(v)
  ///     }
  ///     // 70
  ///     // 108
  ///     // 111
  ///     // 119
  ///     // 101
  ///     // 114
  ///     // 115
  ///     // 32
  ///     // 55357
  ///     // 56464
  ///
  /// Unicode scalar values that make up a string's contents can be up to 21
  /// bits long. The longer scalar values may need two `UInt16` values for
  /// storage. Those "pairs" of code units are called *surrogate pairs*.
  ///
  ///     let flowermoji = "💐"
  ///     for v in flowermoji.unicodeScalars {
  ///         print(v, v.value)
  ///     }
  ///     // 💐 128144
  ///
  ///     for v in flowermoji.utf16 {
  ///         print(v)
  ///     }
  ///     // 55357
  ///     // 56464
  ///
  /// To convert a `String.UTF16View` instance back into a string, use the
  /// `String` type's `init(_:)` initializer.
  ///
  ///     let favemoji = "My favorite emoji is 🎉"
  ///     if let i = favemoji.utf16.index(where: { $0 >= 128 }) {
  ///         let asciiPrefix = String(favemoji.utf16[..<i])
  ///         print(asciiPrefix)
  ///     }
  ///     // Prints "My favorite emoji is "
  ///
  /// UTF16View Elements Match NSString Characters
  /// ============================================
  ///
  /// The UTF-16 code units of a string's `utf16` view match the elements
  /// accessed through indexed `NSString` APIs.
  ///
  ///     print(flowers.utf16.count)
  ///     // Prints "10"
  ///
  ///     let nsflowers = flowers as NSString
  ///     print(nsflowers.length)
  ///     // Prints "10"
  ///
  /// Unlike `NSString`, however, `String.UTF16View` does not use integer
  /// indices. If you need to access a specific position in a UTF-16 view, use
  /// Swift's index manipulation methods. The following example accesses the
  /// fourth code unit in both the `flowers` and `nsflowers` strings:
  ///
  ///     print(nsflowers.character(at: 3))
  ///     // Prints "119"
  ///
  ///     let i = flowers.utf16.index(flowers.utf16.startIndex, offsetBy: 3)
  ///     print(flowers.utf16[i])
  ///     // Prints "119"
  ///
  /// Although the Swift overlay updates many Objective-C methods to return
  /// native Swift indices and index ranges, some still return instances of
  /// `NSRange`. To convert an `NSRange` instance to a range of
  /// `String.Index`, use the `Range(_:in:)` initializer, which takes an
  /// `NSRange` and a string as arguments.
  ///
  ///     let snowy = "❄️ Let it snow! ☃️"
  ///     let nsrange = NSRange(location: 3, length: 12)
  ///     if let range = Range(nsrange, in: snowy) {
  ///         print(snowy[range])
  ///     }
  ///     // Prints "Let it snow!"
  @_fixed_layout // FIXME(sil-serialize-all)
  public struct UTF16View
    : BidirectionalCollection,
    CustomStringConvertible,
    CustomDebugStringConvertible {

    public typealias Index = String.Index

    /// The position of the first code unit if the `String` is
    /// nonempty; identical to `endIndex` otherwise.
    @_inlineable // FIXME(sil-serialize-all)
    public var startIndex: Index {
      return Index(encodedOffset: _offset)
    }

    /// The "past the end" position---that is, the position one greater than
    /// the last valid subscript argument.
    ///
    /// In an empty UTF-16 view, `endIndex` is equal to `startIndex`.
    @_inlineable // FIXME(sil-serialize-all)
    public var endIndex: Index {
      return Index(encodedOffset: _offset + _length)
    }

    @_fixed_layout // FIXME(sil-serialize-all)
    public struct Indices {
      @_inlineable // FIXME(sil-serialize-all)
      @_versioned // FIXME(sil-serialize-all)
      internal init(
        _elements: String.UTF16View, _startIndex: Index, _endIndex: Index
      ) {
        self._elements = _elements
        self._startIndex = _startIndex
        self._endIndex = _endIndex
      }
      @_versioned // FIXME(sil-serialize-all)
      internal var _elements: String.UTF16View
      @_versioned // FIXME(sil-serialize-all)
      internal var _startIndex: Index
      @_versioned // FIXME(sil-serialize-all)
      internal var _endIndex: Index
    }

    @_inlineable // FIXME(sil-serialize-all)
    public var indices: Indices {
      return Indices(
        _elements: self, startIndex: startIndex, endIndex: endIndex)
    }

    // TODO: swift-3-indexing-model - add docs
    @_inlineable // FIXME(sil-serialize-all)
    public func index(after i: Index) -> Index {
      // FIXME: swift-3-indexing-model: range check i?
      return Index(encodedOffset: _unsafePlus(i.encodedOffset, 1))
    }

    // TODO: swift-3-indexing-model - add docs
    @_inlineable // FIXME(sil-serialize-all)
    public func index(before i: Index) -> Index {
      // FIXME: swift-3-indexing-model: range check i?
      return Index(encodedOffset: _unsafeMinus(i.encodedOffset, 1))
    }

    // TODO: swift-3-indexing-model - add docs
    @_inlineable // FIXME(sil-serialize-all)
    public func index(_ i: Index, offsetBy n: Int) -> Index {
      // FIXME: swift-3-indexing-model: range check i?
      return Index(encodedOffset: i.encodedOffset.advanced(by: n))
    }

    // TODO: swift-3-indexing-model - add docs
    @_inlineable // FIXME(sil-serialize-all)
    public func index(
      _ i: Index, offsetBy n: Int, limitedBy limit: Index
    ) -> Index? {
      // FIXME: swift-3-indexing-model: range check i?
      let d = i.encodedOffset.distance(to: limit.encodedOffset)
      if (d >= 0) ? (d < n) : (d > n) {
        return nil
      }
      return Index(encodedOffset: i.encodedOffset.advanced(by: n))
    }

    // TODO: swift-3-indexing-model - add docs
    @_inlineable // FIXME(sil-serialize-all)
    public func distance(from start: Index, to end: Index) -> Int {
      // FIXME: swift-3-indexing-model: range check start and end?
      return start.encodedOffset.distance(to: end.encodedOffset)
    }

    @_inlineable // FIXME(sil-serialize-all)
    @_versioned // FIXME(sil-serialize-all)
    internal func _internalIndex(at i: Int) -> Int {
      return _core.startIndex + i
    }

    /// Accesses the code unit at the given position.
    ///
    /// The following example uses the subscript to print the value of a
    /// string's first UTF-16 code unit.
    ///
    ///     let greeting = "Hello, friend!"
    ///     let i = greeting.utf16.startIndex
    ///     print("First character's UTF-16 code unit: \(greeting.utf16[i])")
    ///     // Prints "First character's UTF-16 code unit: 72"
    ///
    /// - Parameter position: A valid index of the view. `position` must be
    ///   less than the view's end index.
    @_inlineable // FIXME(sil-serialize-all)
    public subscript(i: Index) -> UTF16.CodeUnit {
      _precondition(i >= startIndex && i < endIndex,
          "out-of-range access on a UTF16View")

      let index = _internalIndex(at: i.encodedOffset)
      let u = _core[index]
      if _fastPath((u &>> 11) != 0b1101_1) {
        // Neither high-surrogate, nor low-surrogate -- well-formed sequence
        // of 1 code unit.
        return u
      }

      if (u &>> 10) == 0b1101_10 {
        // `u` is a high-surrogate.  Sequence is well-formed if it
        // is followed by a low-surrogate.
        if _fastPath(
               index + 1 < _core.count &&
               (_core[index + 1] &>> 10) == 0b1101_11) {
          return u
        }
        return 0xfffd
      }

      // `u` is a low-surrogate.  Sequence is well-formed if
      // previous code unit is a high-surrogate.
      if _fastPath(index != 0 && (_core[index - 1] &>> 10) == 0b1101_10) {
        return u
      }
      return 0xfffd
    }

#if _runtime(_ObjC)
    // These may become less important once <rdar://problem/19255291> is addressed.

    @available(
      *, unavailable,
      message: "Indexing a String's UTF16View requires a String.UTF16View.Index, which can be constructed from Int when Foundation is imported")
    public subscript(i: Int) -> UTF16.CodeUnit {
      Builtin.unreachable()
    }

    @available(
      *, unavailable,
      message: "Slicing a String's UTF16View requires a Range<String.UTF16View.Index>, String.UTF16View.Index can be constructed from Int when Foundation is imported")
    public subscript(bounds: Range<Int>) -> UTF16View {
      Builtin.unreachable()
    }
#endif

    @_inlineable // FIXME(sil-serialize-all)
    @_versioned // FIXME(sil-serialize-all)
    internal init(_ _core: _StringCore) {
      self.init(_core, offset: 0, length: _core.count)
    }

    @_inlineable // FIXME(sil-serialize-all)
    @_versioned // FIXME(sil-serialize-all)
    internal init(_ _core: _StringCore, offset: Int, length: Int) {
      self._offset = offset
      self._length = length
      self._core = _core
    }

    @_inlineable // FIXME(sil-serialize-all)
    public var description: String {
      let start = _internalIndex(at: _offset)
      let end = _internalIndex(at: _offset + _length)
      return String(_core[start..<end])
    }

    @_inlineable // FIXME(sil-serialize-all)
    public var debugDescription: String {
      return "StringUTF16(\(self.description.debugDescription))"
    }

    @_versioned // FIXME(sil-serialize-all)
    internal var _offset: Int
    @_versioned // FIXME(sil-serialize-all)
    internal var _length: Int
    @_versioned // FIXME(sil-serialize-all)
    internal let _core: _StringCore
  }

  /// A UTF-16 encoding of `self`.
  @_inlineable // FIXME(sil-serialize-all)
  public var utf16: UTF16View {
    get {
      return UTF16View(_core)
    }
    set {
      self = String(describing: newValue)
    }
  }

  /// Creates a string corresponding to the given sequence of UTF-16 code units.
  ///
  /// If `utf16` contains unpaired UTF-16 surrogates, the result is `nil`.
  ///
  /// You can use this initializer to create a new string from a slice of
  /// another string's `utf16` view.
  ///
  ///     let picnicGuest = "Deserving porcupine"
  ///     if let i = picnicGuest.utf16.index(of: 32) {
  ///         let adjective = String(picnicGuest.utf16[..<i])
  ///         print(adjective)
  ///     }
  ///     // Prints "Optional(Deserving)"
  ///
  /// The `adjective` constant is created by calling this initializer with a
  /// slice of the `picnicGuest.utf16` view.
  ///
  /// - Parameter utf16: A UTF-16 code sequence.
  @_inlineable // FIXME(sil-serialize-all)
  @available(swift, deprecated: 3.2, obsoleted: 4.0)
  public init?(_ utf16: UTF16View) {
    // Attempt to recover the whole string, the better to implement the actual
    // Swift 3.1 semantics, which are not as documented above!  Full Swift 3.1
    // semantics may be impossible to preserve in the case of string literals,
    // since we no longer have access to the length of the original string when
    // there is no owner and elements are dropped from the end.
    let wholeString = utf16._core.nativeBuffer.map { String(_StringCore($0)) }
       ?? String(utf16._core)

    guard
      let start = UTF16Index(_offset: utf16._offset)
        .samePosition(in: wholeString),
      let end = UTF16Index(_offset: utf16._offset + utf16._length)
        .samePosition(in: wholeString)
      else
    {
        return nil
    }
    self = wholeString[start..<end]

  }

  /// Creates a string corresponding to the given sequence of UTF-16 code units.
  @_inlineable // FIXME(sil-serialize-all)
  @available(swift, introduced: 4.0)
  public init(_ utf16: UTF16View) {
    self = String(utf16._core)
  }

  /// The index type for subscripting a string.
  public typealias UTF16Index = UTF16View.Index
}

extension String.UTF16View : _SwiftStringView {
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned // FIXME(sil-serialize-all)
  internal var _ephemeralContent : String { return _persistentContent }
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned // FIXME(sil-serialize-all)
  internal var _persistentContent : String { return String(self._core) }
}

// Index conversions
extension String.UTF16View.Index {
  /// Creates an index in the given UTF-16 view that corresponds exactly to the
  /// specified string position.
  ///
  /// If the index passed as `sourcePosition` represents either the start of a
  /// Unicode scalar value or the position of a UTF-16 trailing surrogate,
  /// then the initializer succeeds. If `sourcePosition` does not have an
  /// exact corresponding position in `target`, then the result is `nil`. For
  /// example, an attempt to convert the position of a UTF-8 continuation byte
  /// results in `nil`.
  ///
  /// The following example finds the position of a space in a string and then
  /// converts that position to an index in the string's `utf16` view.
  ///
  ///     let cafe = "Café 🍵"
  ///
  ///     let stringIndex = cafe.index(of: "é")!
  ///     let utf16Index = String.Index(stringIndex, within: cafe.utf16)!
  ///
  ///     print(cafe.utf16[...utf16Index])
  ///     // Prints "Café"
  ///
  /// - Parameters:
  ///   - sourcePosition: A position in at least one of the views of the string
  ///     shared by `target`.
  ///   - target: The `UTF16View` in which to find the new position.
  @_inlineable // FIXME(sil-serialize-all)
  public init?(
    _ sourcePosition: String.Index, within target: String.UTF16View
  ) {
    guard sourcePosition._transcodedOffset == 0 else { return nil }
    self.init(encodedOffset: sourcePosition.encodedOffset)
  }

  /// Returns the position in the given view of Unicode scalars that
  /// corresponds exactly to this index.
  ///
  /// This index must be a valid index of `String(unicodeScalars).utf16`.
  ///
  /// This example first finds the position of a space (UTF-16 code point `32`)
  /// in a string's `utf16` view and then uses this method to find the same
  /// position in the string's `unicodeScalars` view.
  ///
  ///     let cafe = "Café 🍵"
  ///     let i = cafe.utf16.index(of: 32)!
  ///     let j = i.samePosition(in: cafe.unicodeScalars)!
  ///     print(cafe.unicodeScalars[..<j])
  ///     // Prints "Café"
  ///
  /// - Parameter unicodeScalars: The view to use for the index conversion.
  ///   This index must be a valid index of at least one view of the string
  ///   shared by `unicodeScalars`.
  /// - Returns: The position in `unicodeScalars` that corresponds exactly to
  ///   this index. If this index does not have an exact corresponding
  ///   position in `unicodeScalars`, this method returns `nil`. For example,
  ///   an attempt to convert the position of a UTF-16 trailing surrogate
  ///   returns `nil`.
  @_inlineable // FIXME(sil-serialize-all)
  public func samePosition(
    in unicodeScalars: String.UnicodeScalarView
  ) -> String.UnicodeScalarIndex? {
    return String.UnicodeScalarIndex(self, within: unicodeScalars)
  }
}

// Reflection
extension String.UTF16View : CustomReflectable {
  /// Returns a mirror that reflects the UTF-16 view of a string.
  @_inlineable // FIXME(sil-serialize-all)
  public var customMirror: Mirror {
    return Mirror(self, unlabeledChildren: self)
  }
}

extension String.UTF16View : CustomPlaygroundQuickLookable {
  @_inlineable // FIXME(sil-serialize-all)
  @available(*, deprecated, message: "UTF16View.customPlaygroundQuickLook will be removed in a future Swift version")
  public var customPlaygroundQuickLook: PlaygroundQuickLook {
    return .text(description)
  }
}

extension String.UTF16View.Indices : BidirectionalCollection {
  public typealias Index = String.UTF16View.Index
  public typealias Indices = String.UTF16View.Indices
  public typealias SubSequence = String.UTF16View.Indices

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned // FIXME(sil-serialize-all)
  internal init(
    _elements: String.UTF16View,
    startIndex: Index,
    endIndex: Index
  ) {
    self._elements = _elements
    self._startIndex = startIndex
    self._endIndex = endIndex
  }

  @_inlineable // FIXME(sil-serialize-all)
  public var startIndex: Index {
    return _startIndex
  }

  @_inlineable // FIXME(sil-serialize-all)
  public var endIndex: Index {
    return _endIndex
  }

  @_inlineable // FIXME(sil-serialize-all)
  public var indices: Indices {
    return self
  }

  @_inlineable // FIXME(sil-serialize-all)
  public subscript(i: Index) -> Index {
    // FIXME: swift-3-indexing-model: range check.
    return i
  }

  @_inlineable // FIXME(sil-serialize-all)
  public subscript(bounds: Range<Index>) -> String.UTF16View.Indices {
    // FIXME: swift-3-indexing-model: range check.
    return String.UTF16View.Indices(
      _elements: _elements,
      startIndex: bounds.lowerBound,
      endIndex: bounds.upperBound)
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func index(after i: Index) -> Index {
    // FIXME: swift-3-indexing-model: range check.
    return _elements.index(after: i)
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func formIndex(after i: inout Index) {
    // FIXME: swift-3-indexing-model: range check.
    _elements.formIndex(after: &i)
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func index(before i: Index) -> Index {
    // FIXME: swift-3-indexing-model: range check.
    return _elements.index(before: i)
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func formIndex(before i: inout Index) {
    // FIXME: swift-3-indexing-model: range check.
    _elements.formIndex(before: &i)
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func index(_ i: Index, offsetBy n: Int) -> Index {
    // FIXME: swift-3-indexing-model: range check i?
    return _elements.index(i, offsetBy: n)
  }

  @_inlineable // FIXME(sil-serialize-all)
  public func index(
    _ i: Index, offsetBy n: Int, limitedBy limit: Index
  ) -> Index? {
    // FIXME: swift-3-indexing-model: range check i?
    return _elements.index(i, offsetBy: n, limitedBy: limit)
  }

  // TODO: swift-3-indexing-model - add docs
  @_inlineable // FIXME(sil-serialize-all)
  public func distance(from start: Index, to end: Index) -> Int {
    // FIXME: swift-3-indexing-model: range check start and end?
    return _elements.distance(from: start, to: end)
  }
}

// backward compatibility for index interchange.  
extension String.UTF16View {
  @_inlineable // FIXME(sil-serialize-all)
  @available(
    swift, obsoleted: 4.0,
    message: "Any String view index conversion can fail in Swift 4; please unwrap the optional index")
  public func index(after i: Index?) -> Index {
    return index(after: i!)
  }
  @_inlineable // FIXME(sil-serialize-all)
  @available(
    swift, obsoleted: 4.0,
    message: "Any String view index conversion can fail in Swift 4; please unwrap the optional index")
  public func index(
    _ i: Index?, offsetBy n: Int) -> Index {
    return index(i!, offsetBy: n)
  }
  @_inlineable // FIXME(sil-serialize-all)
  @available(
    swift, obsoleted: 4.0,
    message: "Any String view index conversion can fail in Swift 4; please unwrap the optional indices")
  public func distance(from i: Index?, to j: Index?) -> Int {
    return distance(from: i!, to: j!)
  }
  @_inlineable // FIXME(sil-serialize-all)
  @available(
    swift, obsoleted: 4.0,
    message: "Any String view index conversion can fail in Swift 4; please unwrap the optional index")
  public subscript(i: Index?) -> Unicode.UTF16.CodeUnit {
    return self[i!]
  }
}

//===--- Slicing Support --------------------------------------------------===//
/// In Swift 3.2, in the absence of type context,
///
///   someString.utf16[someString.utf16.startIndex..<someString.utf16.endIndex]
///
/// was deduced to be of type `String.UTF16View`.  Provide a more-specific
/// Swift-3-only `subscript` overload that continues to produce
/// `String.UTF16View`.
extension String.UTF16View {
  public typealias SubSequence = Substring.UTF16View

  @_inlineable // FIXME(sil-serialize-all)
  @available(swift, introduced: 4)
  public subscript(r: Range<Index>) -> String.UTF16View.SubSequence {
    return String.UTF16View.SubSequence(self, _bounds: r)
  }

  @_inlineable // FIXME(sil-serialize-all)
  @available(swift, obsoleted: 4)
  public subscript(bounds: Range<Index>) -> String.UTF16View {
    return String.UTF16View(
      _core,
      offset: _internalIndex(at: bounds.lowerBound.encodedOffset),
      length: bounds.upperBound.encodedOffset - bounds.lowerBound.encodedOffset)
  }

  @_inlineable // FIXME(sil-serialize-all)
  @available(swift, obsoleted: 4)
  public subscript(bounds: ClosedRange<Index>) -> String.UTF16View {
    return self[bounds.relative(to: self)]
  }
}
