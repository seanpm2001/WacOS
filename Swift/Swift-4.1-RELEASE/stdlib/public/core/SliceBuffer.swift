//===--- SliceBuffer.swift - Backing storage for ArraySlice<Element> ------===//
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

/// Buffer type for `ArraySlice<Element>`.
@_fixed_layout
@_versioned
internal struct _SliceBuffer<Element>
  : _ArrayBufferProtocol,
    RandomAccessCollection
{
  internal typealias NativeStorage = _ContiguousArrayStorage<Element>
  internal typealias NativeBuffer = _ContiguousArrayBuffer<Element>

  @_inlineable
  @_versioned
  internal init(
    owner: AnyObject, subscriptBaseAddress: UnsafeMutablePointer<Element>,
    indices: Range<Int>, hasNativeBuffer: Bool
  ) {
    self.owner = owner
    self.subscriptBaseAddress = subscriptBaseAddress
    self.startIndex = indices.lowerBound
    let bufferFlag = UInt(hasNativeBuffer ? 1 : 0)
    self.endIndexAndFlags = (UInt(indices.upperBound) << 1) | bufferFlag
    _invariantCheck()
  }

  @_inlineable
  @_versioned
  internal init() {
    let empty = _ContiguousArrayBuffer<Element>()
    self.owner = empty.owner
    self.subscriptBaseAddress = empty.firstElementAddress
    self.startIndex = empty.startIndex
    self.endIndexAndFlags = 1
    _invariantCheck()
  }

  @_inlineable
  @_versioned
  internal init(_buffer buffer: NativeBuffer, shiftedToStartIndex: Int) {
    let shift = buffer.startIndex - shiftedToStartIndex
    self.init(
      owner: buffer.owner,
      subscriptBaseAddress: buffer.subscriptBaseAddress + shift,
      indices: shiftedToStartIndex..<shiftedToStartIndex + buffer.count,
      hasNativeBuffer: true)
  }

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal func _invariantCheck() {
    let isNative = _hasNativeBuffer
    let isNativeStorage: Bool = owner is _ContiguousArrayStorageBase
    _sanityCheck(isNativeStorage == isNative)
    if isNative {
      _sanityCheck(count <= nativeBuffer.count)
    }
  }

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal var _hasNativeBuffer: Bool {
    return (endIndexAndFlags & 1) != 0
  }

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal var nativeBuffer: NativeBuffer {
    _sanityCheck(_hasNativeBuffer)
    return NativeBuffer(
      owner as? _ContiguousArrayStorageBase ?? _emptyArrayStorage)
  }

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal var nativeOwner: AnyObject {
    _sanityCheck(_hasNativeBuffer, "Expect a native array")
    return owner
  }

  /// Replace the given subRange with the first newCount elements of
  /// the given collection.
  ///
  /// - Precondition: This buffer is backed by a uniquely-referenced
  ///   `_ContiguousArrayBuffer` and
  ///   `insertCount <= numericCast(newValues.count)`.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal mutating func replaceSubrange<C>(
    _ subrange: Range<Int>,
    with insertCount: Int,
    elementsOf newValues: C
  ) where C : Collection, C.Element == Element {

    _invariantCheck()
    _sanityCheck(insertCount <= numericCast(newValues.count))

    _sanityCheck(_hasNativeBuffer)
    _sanityCheck(isUniquelyReferenced())

    let eraseCount = subrange.count
    let growth = insertCount - eraseCount
    let oldCount = count

    var native = nativeBuffer
    let hiddenElementCount = firstElementAddress - native.firstElementAddress

    _sanityCheck(native.count + growth <= native.capacity)

    let start = subrange.lowerBound - startIndex + hiddenElementCount
    let end = subrange.upperBound - startIndex + hiddenElementCount
    native.replaceSubrange(
      start..<end,
      with: insertCount,
      elementsOf: newValues)

    self.endIndex = self.startIndex + oldCount + growth

    _invariantCheck()
  }

  /// A value that identifies the storage used by the buffer.  Two
  /// buffers address the same elements when they have the same
  /// identity and count.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal var identity: UnsafeRawPointer {
    return UnsafeRawPointer(firstElementAddress)
  }

  /// An object that keeps the elements stored in this buffer alive.
  @_versioned
  internal var owner: AnyObject
  @_versioned
  internal let subscriptBaseAddress: UnsafeMutablePointer<Element>

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal var firstElementAddress: UnsafeMutablePointer<Element> {
    return subscriptBaseAddress + startIndex
  }

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal var firstElementAddressIfContiguous: UnsafeMutablePointer<Element>? {
    return firstElementAddress
  }

  /// [63:1: 63-bit index][0: has a native buffer]
  @_versioned
  internal var endIndexAndFlags: UInt

  //===--- Non-essential bits ---------------------------------------------===//

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal mutating func requestUniqueMutableBackingBuffer(
    minimumCapacity: Int
  ) -> NativeBuffer? {
    _invariantCheck()
    // This is a performance optimization that was put in to ensure that at
    // -Onone, copy of self we make to call _hasNativeBuffer is destroyed before
    // we call isUniquelyReferenced. Otherwise, isUniquelyReferenced will always
    // fail causing us to always copy.
    //
    // if _fastPath(_hasNativeBuffer && isUniquelyReferenced) {
    //
    // SR-6437
    let native = _hasNativeBuffer
    let unique = isUniquelyReferenced()
    if _fastPath(native && unique) {
      if capacity >= minimumCapacity {
        // Since we have the last reference, drop any inaccessible
        // trailing elements in the underlying storage.  That will
        // tend to reduce shuffling of later elements.  Since this
        // function isn't called for subscripting, this won't slow
        // down that case.
        var native = nativeBuffer
        let offset = self.firstElementAddress - native.firstElementAddress
        let backingCount = native.count
        let myCount = count

        if _slowPath(backingCount > myCount + offset) {
          native.replaceSubrange(
            (myCount+offset)..<backingCount,
            with: 0,
            elementsOf: EmptyCollection())
        }
        _invariantCheck()
        return native
      }
    }
    return nil
  }

  @_inlineable
  @_versioned
  internal mutating func isMutableAndUniquelyReferenced() -> Bool {
    // This is a performance optimization that ensures that the copy of self
    // that occurs at -Onone is destroyed before we call
    // isUniquelyReferencedOrPinned. This code used to be:
    //
    //   return _hasNativeBuffer && isUniquelyReferenced()
    //
    // SR-6437
    if !_hasNativeBuffer {
      return false
    }
    return isUniquelyReferenced()
  }

  @_inlineable
  @_versioned
  internal mutating func isMutableAndUniquelyReferencedOrPinned() -> Bool {
    // This is a performance optimization that ensures that the copy of self
    // that occurs at -Onone is destroyed before we call
    // isUniquelyReferencedOrPinned. This code used to be:
    //
    //   return _hasNativeBuffer && isUniquelyReferencedOrPinned()
    //
    // SR-6437
    if !_hasNativeBuffer {
      return false
    }
    return isUniquelyReferencedOrPinned()
  }

  /// If this buffer is backed by a `_ContiguousArrayBuffer`
  /// containing the same number of elements as `self`, return it.
  /// Otherwise, return `nil`.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal func requestNativeBuffer() -> _ContiguousArrayBuffer<Element>? {
    _invariantCheck()
    if _fastPath(_hasNativeBuffer && nativeBuffer.count == count) {
      return nativeBuffer
    }
    return nil
  }

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  @discardableResult
  internal func _copyContents(
    subRange bounds: Range<Int>,
    initializing target: UnsafeMutablePointer<Element>
  ) -> UnsafeMutablePointer<Element> {
    _invariantCheck()
    _sanityCheck(bounds.lowerBound >= startIndex)
    _sanityCheck(bounds.upperBound >= bounds.lowerBound)
    _sanityCheck(bounds.upperBound <= endIndex)
    let c = bounds.count
    target.initialize(from: subscriptBaseAddress + bounds.lowerBound, count: c)
    return target + c
  }

  /// True, if the array is native and does not need a deferred type check.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal var arrayPropertyIsNativeTypeChecked: Bool {
    return _hasNativeBuffer
  }

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal var count: Int {
    get {
      return endIndex - startIndex
    }
    set {
      let growth = newValue - count
      if growth != 0 {
        nativeBuffer.count += growth
        self.endIndex += growth
      }
      _invariantCheck()
    }
  }

  /// Traps unless the given `index` is valid for subscripting, i.e.
  /// `startIndex ≤ index < endIndex`
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal func _checkValidSubscript(_ index : Int) {
    _precondition(
      index >= startIndex && index < endIndex, "Index out of bounds")
  }

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal var capacity: Int {
    let count = self.count
    if _slowPath(!_hasNativeBuffer) {
      return count
    }
    let n = nativeBuffer
    let nativeEnd = n.firstElementAddress + n.count
    if (firstElementAddress + count) == nativeEnd {
      return count + (n.capacity - n.count)
    }
    return count
  }

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal mutating func isUniquelyReferenced() -> Bool {
    return isKnownUniquelyReferenced(&owner)
  }

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal mutating func isUniquelyReferencedOrPinned() -> Bool {
    return _isKnownUniquelyReferencedOrPinned(&owner)
  }

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal func getElement(_ i: Int) -> Element {
    _sanityCheck(i >= startIndex, "slice index is out of range (before startIndex)")
    _sanityCheck(i < endIndex, "slice index is out of range")
    return subscriptBaseAddress[i]
  }

  /// Access the element at `position`.
  ///
  /// - Precondition: `position` is a valid position in `self` and
  ///   `position != endIndex`.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned // FIXME(sil-serialize-all)
  internal subscript(position: Int) -> Element {
    get {
      return getElement(position)
    }
    nonmutating set {
      _sanityCheck(position >= startIndex, "slice index is out of range (before startIndex)")
      _sanityCheck(position < endIndex, "slice index is out of range")
      subscriptBaseAddress[position] = newValue
    }
  }

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal subscript(bounds: Range<Int>) -> _SliceBuffer {
    get {
      _sanityCheck(bounds.lowerBound >= startIndex)
      _sanityCheck(bounds.upperBound >= bounds.lowerBound)
      _sanityCheck(bounds.upperBound <= endIndex)
      return _SliceBuffer(
        owner: owner,
        subscriptBaseAddress: subscriptBaseAddress,
        indices: bounds,
        hasNativeBuffer: _hasNativeBuffer)
    }
    set {
      fatalError("not implemented")
    }
  }

  //===--- Collection conformance -------------------------------------===//
  /// The position of the first element in a non-empty collection.
  ///
  /// In an empty collection, `startIndex == endIndex`.
  @_versioned
  internal var startIndex: Int

  /// The collection's "past the end" position---that is, the position one
  /// greater than the last valid subscript argument.
  ///
  /// `endIndex` is always reachable from `startIndex` by zero or more
  /// applications of `index(after:)`.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  internal var endIndex: Int {
    get {
      return Int(endIndexAndFlags >> 1)
    }
    set {
      endIndexAndFlags = (UInt(newValue) << 1) | (_hasNativeBuffer ? 1 : 0)
    }
  }

  internal typealias Indices = CountableRange<Int>

  //===--- misc -----------------------------------------------------------===//
  /// Call `body(p)`, where `p` is an `UnsafeBufferPointer` over the
  /// underlying contiguous storage.
  @_inlineable
  @_versioned
  internal func withUnsafeBufferPointer<R>(
    _ body: (UnsafeBufferPointer<Element>) throws -> R
  ) rethrows -> R {
    defer { _fixLifetime(self) }
    return try body(UnsafeBufferPointer(start: firstElementAddress,
      count: count))
  }

  /// Call `body(p)`, where `p` is an `UnsafeMutableBufferPointer`
  /// over the underlying contiguous storage.
  @_inlineable
  @_versioned
  internal mutating func withUnsafeMutableBufferPointer<R>(
    _ body: (UnsafeMutableBufferPointer<Element>) throws -> R
  ) rethrows -> R {
    defer { _fixLifetime(self) }
    return try body(
      UnsafeMutableBufferPointer(start: firstElementAddress, count: count))
  }
}

extension _SliceBuffer {
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned // FIXME(sil-serialize-all)
  internal func _copyToContiguousArray() -> ContiguousArray<Element> {
    if _hasNativeBuffer {
      let n = nativeBuffer
      if count == n.count {
        return ContiguousArray(_buffer: n)
      }
    }

    let result = _ContiguousArrayBuffer<Element>(
      _uninitializedCount: count,
      minimumCapacity: 0)
    result.firstElementAddress.initialize(
      from: firstElementAddress, count: count)
    return ContiguousArray(_buffer: result)
  }
}
