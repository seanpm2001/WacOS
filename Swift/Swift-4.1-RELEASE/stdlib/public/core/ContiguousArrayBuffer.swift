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

import SwiftShims

/// Class used whose sole instance is used as storage for empty
/// arrays.  The instance is defined in the runtime and statically
/// initialized.  See stdlib/runtime/GlobalObjects.cpp for details.
/// Because it's statically referenced, it requires non-lazy realization
/// by the Objective-C runtime.
@_fixed_layout
@_versioned
@objc_non_lazy_realization
internal final class _EmptyArrayStorage
  : _ContiguousArrayStorageBase {

  @_inlineable
  @_versioned
  @nonobjc
  internal init(_doNotCallMe: ()) {
    _sanityCheckFailure("creating instance of _EmptyArrayStorage")
  }
  
#if _runtime(_ObjC)
  @_inlineable
  @_versioned
  override internal func _withVerbatimBridgedUnsafeBuffer<R>(
    _ body: (UnsafeBufferPointer<AnyObject>) throws -> R
  ) rethrows -> R? {
    return try body(UnsafeBufferPointer(start: nil, count: 0))
  }

  @_inlineable
  @_versioned
  @nonobjc
  override internal func _getNonVerbatimBridgedCount() -> Int {
    return 0
  }

  @_inlineable
  @_versioned
  override internal func _getNonVerbatimBridgedHeapBuffer() -> _HeapBuffer<Int, AnyObject> {
    return _HeapBuffer<Int, AnyObject>(
      _HeapBufferStorage<Int, AnyObject>.self, 0, 0)
  }
#endif

  @_inlineable
  @_versioned
  override internal func canStoreElements(ofDynamicType _: Any.Type) -> Bool {
    return false
  }

  /// A type that every element in the array is.
  @_inlineable
  @_versioned
  override internal var staticElementType: Any.Type {
    return Void.self
  }
}

/// The empty array prototype.  We use the same object for all empty
/// `[Native]Array<Element>`s.
@_inlineable
@_versioned
internal var _emptyArrayStorage : _EmptyArrayStorage {
  return Builtin.bridgeFromRawPointer(
    Builtin.addressof(&_swiftEmptyArrayStorage))
}

// The class that implements the storage for a ContiguousArray<Element>
@_fixed_layout // FIXME(sil-serialize-all)
@_versioned
internal final class _ContiguousArrayStorage<
  Element
> : _ContiguousArrayStorageBase {

  @_inlineable // FIXME(sil-serialize-all)
  @_versioned // FIXME(sil-serialize-all)
  deinit {
    _elementPointer.deinitialize(count: countAndCapacity.count)
    _fixLifetime(self)
  }

#if _runtime(_ObjC)
  /// If the `Element` is bridged verbatim, invoke `body` on an
  /// `UnsafeBufferPointer` to the elements and return the result.
  /// Otherwise, return `nil`.
  @_inlineable
  @_versioned
  internal final override func _withVerbatimBridgedUnsafeBuffer<R>(
    _ body: (UnsafeBufferPointer<AnyObject>) throws -> R
  ) rethrows -> R? {
    var result: R?
    try self._withVerbatimBridgedUnsafeBufferImpl {
      result = try body($0)
    }
    return result
  }

  /// If `Element` is bridged verbatim, invoke `body` on an
  /// `UnsafeBufferPointer` to the elements.
  @_inlineable
  @_versioned
  internal final func _withVerbatimBridgedUnsafeBufferImpl(
    _ body: (UnsafeBufferPointer<AnyObject>) throws -> Void
  ) rethrows {
    if _isBridgedVerbatimToObjectiveC(Element.self) {
      let count = countAndCapacity.count
      let elements = UnsafeRawPointer(_elementPointer)
        .assumingMemoryBound(to: AnyObject.self)
      defer { _fixLifetime(self) }
      try body(UnsafeBufferPointer(start: elements, count: count))
    }
  }

  /// Returns the number of elements in the array.
  ///
  /// - Precondition: `Element` is bridged non-verbatim.
  @_inlineable
  @_versioned
  @nonobjc
  override internal func _getNonVerbatimBridgedCount() -> Int {
    _sanityCheck(
      !_isBridgedVerbatimToObjectiveC(Element.self),
      "Verbatim bridging should be handled separately")
    return countAndCapacity.count
  }

  /// Bridge array elements and return a new buffer that owns them.
  ///
  /// - Precondition: `Element` is bridged non-verbatim.
  @_inlineable
  @_versioned
  override internal func _getNonVerbatimBridgedHeapBuffer() ->
    _HeapBuffer<Int, AnyObject> {
    _sanityCheck(
      !_isBridgedVerbatimToObjectiveC(Element.self),
      "Verbatim bridging should be handled separately")
    let count = countAndCapacity.count
    let result = _HeapBuffer<Int, AnyObject>(
      _HeapBufferStorage<Int, AnyObject>.self, count, count)
    let resultPtr = result.baseAddress
    let p = _elementPointer
    for i in 0..<count {
      (resultPtr + i).initialize(to: _bridgeAnythingToObjectiveC(p[i]))
    }
    _fixLifetime(self)
    return result
  }
#endif

  /// Returns `true` if the `proposedElementType` is `Element` or a subclass of
  /// `Element`.  We can't store anything else without violating type
  /// safety; for example, the destructor has static knowledge that
  /// all of the elements can be destroyed as `Element`.
  @_inlineable
  @_versioned
  internal override func canStoreElements(
    ofDynamicType proposedElementType: Any.Type
  ) -> Bool {
#if _runtime(_ObjC)
    return proposedElementType is Element.Type
#else
    // FIXME: Dynamic casts don't currently work without objc. 
    // rdar://problem/18801510
    return false
#endif
  }

  /// A type that every element in the array is.
  @_inlineable
  @_versioned
  internal override var staticElementType: Any.Type {
    return Element.self
  }

  @_inlineable
  @_versioned
  internal final var _elementPointer : UnsafeMutablePointer<Element> {
    return UnsafeMutablePointer(Builtin.projectTailElems(self, Element.self))
  }
}

@_versioned
@_fixed_layout
internal struct _ContiguousArrayBuffer<Element> : _ArrayBufferProtocol {

  /// Make a buffer with uninitialized elements.  After using this
  /// method, you must either initialize the `count` elements at the
  /// result's `.firstElementAddress` or set the result's `.count`
  /// to zero.
  @_inlineable
  @_versioned
  internal init(
    _uninitializedCount uninitializedCount: Int,
    minimumCapacity: Int
  ) {
    let realMinimumCapacity = Swift.max(uninitializedCount, minimumCapacity)
    if realMinimumCapacity == 0 {
      self = _ContiguousArrayBuffer<Element>()
    }
    else {
      _storage = Builtin.allocWithTailElems_1(
         _ContiguousArrayStorage<Element>.self,
         realMinimumCapacity._builtinWordValue, Element.self)

      let storageAddr = UnsafeMutableRawPointer(Builtin.bridgeToRawPointer(_storage))
      let endAddr = storageAddr + _stdlib_malloc_size(storageAddr)
      let realCapacity = endAddr.assumingMemoryBound(to: Element.self) - firstElementAddress

      _initStorageHeader(
        count: uninitializedCount, capacity: realCapacity)
    }
  }

  /// Initialize using the given uninitialized `storage`.
  /// The storage is assumed to be uninitialized. The returned buffer has the
  /// body part of the storage initialized, but not the elements.
  ///
  /// - Warning: The result has uninitialized elements.
  /// 
  /// - Warning: storage may have been stack-allocated, so it's
  ///   crucial not to call, e.g., `malloc_size` on it.
  @_inlineable
  @_versioned
  internal init(count: Int, storage: _ContiguousArrayStorage<Element>) {
    _storage = storage

    _initStorageHeader(count: count, capacity: count)
  }

  @_inlineable
  @_versioned
  internal init(_ storage: _ContiguousArrayStorageBase) {
    _storage = storage
  }

  /// Initialize the body part of our storage.
  ///
  /// - Warning: does not initialize elements
  @_inlineable
  @_versioned
  internal func _initStorageHeader(count: Int, capacity: Int) {
#if _runtime(_ObjC)
    let verbatim = _isBridgedVerbatimToObjectiveC(Element.self)
#else
    let verbatim = false
#endif

    // We can initialize by assignment because _ArrayBody is a trivial type,
    // i.e. contains no references.
    _storage.countAndCapacity = _ArrayBody(
      count: count,
      capacity: capacity,
      elementTypeIsBridgedVerbatim: verbatim)
  }

  /// True, if the array is native and does not need a deferred type check.
  @_inlineable
  @_versioned
  internal var arrayPropertyIsNativeTypeChecked: Bool {
    return true
  }

  /// A pointer to the first element.
  @_inlineable
  @_versioned
  internal var firstElementAddress: UnsafeMutablePointer<Element> {
    return UnsafeMutablePointer(Builtin.projectTailElems(_storage,
                                                         Element.self))
  }

  @_inlineable
  @_versioned
  internal var firstElementAddressIfContiguous: UnsafeMutablePointer<Element>? {
    return firstElementAddress
  }

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

  //===--- _ArrayBufferProtocol conformance -----------------------------------===//
  /// Create an empty buffer.
  @_inlineable
  @_versioned
  internal init() {
    _storage = _emptyArrayStorage
  }

  @_inlineable
  @_versioned
  internal init(_buffer buffer: _ContiguousArrayBuffer, shiftedToStartIndex: Int) {
    _sanityCheck(shiftedToStartIndex == 0, "shiftedToStartIndex must be 0")
    self = buffer
  }

  @_inlineable
  @_versioned
  internal mutating func requestUniqueMutableBackingBuffer(
    minimumCapacity: Int
  ) -> _ContiguousArrayBuffer<Element>? {
    if _fastPath(isUniquelyReferenced() && capacity >= minimumCapacity) {
      return self
    }
    return nil
  }

  @_inlineable
  @_versioned
  internal mutating func isMutableAndUniquelyReferenced() -> Bool {
    return isUniquelyReferenced()
  }

  @_inlineable
  @_versioned
  internal mutating func isMutableAndUniquelyReferencedOrPinned() -> Bool {
    return isUniquelyReferencedOrPinned()
  }

  /// If this buffer is backed by a `_ContiguousArrayBuffer`
  /// containing the same number of elements as `self`, return it.
  /// Otherwise, return `nil`.
  @_inlineable
  @_versioned
  internal func requestNativeBuffer() -> _ContiguousArrayBuffer<Element>? {
    return self
  }

  @_inlineable
  @_versioned
  @inline(__always)
  internal func getElement(_ i: Int) -> Element {
    _sanityCheck(i >= 0 && i < count, "Array index out of range")
    return firstElementAddress[i]
  }

  /// Get or set the value of the ith element.
  @_inlineable
  @_versioned
  internal subscript(i: Int) -> Element {
    @inline(__always)
    get {
      return getElement(i)
    }
    @inline(__always)
    nonmutating set {
      _sanityCheck(i >= 0 && i < count, "Array index out of range")

      // FIXME: Manually swap because it makes the ARC optimizer happy.  See
      // <rdar://problem/16831852> check retain/release order
      // firstElementAddress[i] = newValue
      var nv = newValue
      let tmp = nv
      nv = firstElementAddress[i]
      firstElementAddress[i] = tmp
    }
  }

  /// The number of elements the buffer stores.
  @_inlineable
  @_versioned
  internal var count: Int {
    get {
      return _storage.countAndCapacity.count
    }
    nonmutating set {
      _sanityCheck(newValue >= 0)

      _sanityCheck(
        newValue <= capacity,
        "Can't grow an array buffer past its capacity")

      _storage.countAndCapacity.count = newValue
    }
  }

  /// Traps unless the given `index` is valid for subscripting, i.e.
  /// `0 ≤ index < count`.
  @_inlineable
  @_versioned
  @inline(__always)
  internal func _checkValidSubscript(_ index : Int) {
    _precondition(
      (index >= 0) && (index < count),
      "Index out of range"
    )
  }

  /// The number of elements the buffer can store without reallocation.
  @_inlineable
  @_versioned
  internal var capacity: Int {
    return _storage.countAndCapacity.capacity
  }

  /// Copy the elements in `bounds` from this buffer into uninitialized
  /// memory starting at `target`.  Return a pointer "past the end" of the
  /// just-initialized memory.
  @_inlineable
  @_versioned
  @discardableResult
  internal func _copyContents(
    subRange bounds: Range<Int>,
    initializing target: UnsafeMutablePointer<Element>
  ) -> UnsafeMutablePointer<Element> {
    _sanityCheck(bounds.lowerBound >= 0)
    _sanityCheck(bounds.upperBound >= bounds.lowerBound)
    _sanityCheck(bounds.upperBound <= count)

    let initializedCount = bounds.upperBound - bounds.lowerBound
    target.initialize(
      from: firstElementAddress + bounds.lowerBound, count: initializedCount)
    _fixLifetime(owner)
    return target + initializedCount
  }

  /// Returns a `_SliceBuffer` containing the given `bounds` of values
  /// from this buffer.
  @_inlineable
  @_versioned
  internal subscript(bounds: Range<Int>) -> _SliceBuffer<Element> {
    get {
      return _SliceBuffer(
        owner: _storage,
        subscriptBaseAddress: subscriptBaseAddress,
        indices: bounds,
        hasNativeBuffer: true)
    }
    set {
      fatalError("not implemented")
    }
  }

  /// Returns `true` iff this buffer's storage is uniquely-referenced.
  ///
  /// - Note: This does not mean the buffer is mutable.  Other factors
  ///   may need to be considered, such as whether the buffer could be
  ///   some immutable Cocoa container.
  @_inlineable
  @_versioned
  internal mutating func isUniquelyReferenced() -> Bool {
    return _isUnique(&_storage)
  }

  /// Returns `true` iff this buffer's storage is either
  /// uniquely-referenced or pinned.  NOTE: this does not mean
  /// the buffer is mutable; see the comment on isUniquelyReferenced.
  @_inlineable
  @_versioned
  internal mutating func isUniquelyReferencedOrPinned() -> Bool {
    return _isUniqueOrPinned(&_storage)
  }

#if _runtime(_ObjC)
  /// Convert to an NSArray.
  ///
  /// - Precondition: `Element` is bridged to Objective-C.
  ///
  /// - Complexity: O(1).
  @_inlineable
  @_versioned
  internal func _asCocoaArray() -> _NSArrayCore {
    if count == 0 {
      return _emptyArrayStorage
    }
    if _isBridgedVerbatimToObjectiveC(Element.self) {
      return _storage
    }
    return _SwiftDeferredNSArray(_nativeStorage: _storage)
  }
#endif

  /// An object that keeps the elements stored in this buffer alive.
  @_inlineable
  @_versioned
  internal var owner: AnyObject {
    return _storage
  }

  /// An object that keeps the elements stored in this buffer alive.
  @_inlineable
  @_versioned
  internal var nativeOwner: AnyObject {
    return _storage
  }

  /// A value that identifies the storage used by the buffer.
  ///
  /// Two buffers address the same elements when they have the same
  /// identity and count.
  @_inlineable
  @_versioned
  internal var identity: UnsafeRawPointer {
    return UnsafeRawPointer(firstElementAddress)
  }
  
  /// Returns `true` iff we have storage for elements of the given
  /// `proposedElementType`.  If not, we'll be treated as immutable.
  @_inlineable
  @_versioned
  func canStoreElements(ofDynamicType proposedElementType: Any.Type) -> Bool {
    return _storage.canStoreElements(ofDynamicType: proposedElementType)
  }

  /// Returns `true` if the buffer stores only elements of type `U`.
  ///
  /// - Precondition: `U` is a class or `@objc` existential.
  ///
  /// - Complexity: O(*n*)
  @_inlineable
  @_versioned
  internal func storesOnlyElementsOfType<U>(
    _: U.Type
  ) -> Bool {
    _sanityCheck(_isClassOrObjCExistential(U.self))

    if _fastPath(_storage.staticElementType is U.Type) {
      // Done in O(1)
      return true
    }

    // Check the elements
    for x in self {
      if !(x is U) {
        return false
      }
    }
    return true
  }

  @_versioned
  internal var _storage: _ContiguousArrayStorageBase
}

/// Append the elements of `rhs` to `lhs`.
@_inlineable
@_versioned
internal func += <Element, C : Collection>(
  lhs: inout _ContiguousArrayBuffer<Element>, rhs: C
) where C.Element == Element {

  let oldCount = lhs.count
  let newCount = oldCount + numericCast(rhs.count)

  let buf: UnsafeMutableBufferPointer<Element>
  
  if _fastPath(newCount <= lhs.capacity) {
    buf = UnsafeMutableBufferPointer(start: lhs.firstElementAddress + oldCount, count: numericCast(rhs.count))
    lhs.count = newCount
  }
  else {
    var newLHS = _ContiguousArrayBuffer<Element>(
      _uninitializedCount: newCount,
      minimumCapacity: _growArrayCapacity(lhs.capacity))

    newLHS.firstElementAddress.moveInitialize(
      from: lhs.firstElementAddress, count: oldCount)
    lhs.count = 0
    (lhs, newLHS) = (newLHS, lhs)
    buf = UnsafeMutableBufferPointer(start: lhs.firstElementAddress + oldCount, count: numericCast(rhs.count))
  }

  var (remainders,writtenUpTo) = buf.initialize(from: rhs)

  // ensure that exactly rhs.count elements were written
  _precondition(remainders.next() == nil, "rhs underreported its count")
  _precondition(writtenUpTo == buf.endIndex, "rhs overreported its count")    
}

extension _ContiguousArrayBuffer : RandomAccessCollection {
  /// The position of the first element in a non-empty collection.
  ///
  /// In an empty collection, `startIndex == endIndex`.
  @_inlineable
  @_versioned
  internal var startIndex: Int {
    return 0
  }
  /// The collection's "past the end" position.
  ///
  /// `endIndex` is not a valid argument to `subscript`, and is always
  /// reachable from `startIndex` by zero or more applications of
  /// `index(after:)`.
  @_inlineable
  @_versioned
  internal var endIndex: Int {
    return count
  }

  internal typealias Indices = CountableRange<Int>
}

extension Sequence {
  @_inlineable
  public func _copyToContiguousArray() -> ContiguousArray<Element> {
    return _copySequenceToContiguousArray(self)
  }
}

@_inlineable
@_versioned
internal func _copySequenceToContiguousArray<
  S : Sequence
>(_ source: S) -> ContiguousArray<S.Element> {
  let initialCapacity = source.underestimatedCount
  var builder =
    _UnsafePartiallyInitializedContiguousArrayBuffer<S.Element>(
      initialCapacity: initialCapacity)

  var iterator = source.makeIterator()

  // FIXME(performance): use _copyContents(initializing:).

  // Add elements up to the initial capacity without checking for regrowth.
  for _ in 0..<initialCapacity {
    builder.addWithExistingCapacity(iterator.next()!)
  }

  // Add remaining elements, if any.
  while let element = iterator.next() {
    builder.add(element)
  }

  return builder.finish()
}

extension Collection {
  @_inlineable
  public func _copyToContiguousArray() -> ContiguousArray<Element> {
    return _copyCollectionToContiguousArray(self)
  }
}

extension _ContiguousArrayBuffer {
  @_inlineable
  @_versioned
  internal func _copyToContiguousArray() -> ContiguousArray<Element> {
    return ContiguousArray(_buffer: self)
  }
}

/// This is a fast implementation of _copyToContiguousArray() for collections.
///
/// It avoids the extra retain, release overhead from storing the
/// ContiguousArrayBuffer into
/// _UnsafePartiallyInitializedContiguousArrayBuffer. Since we do not support
/// ARC loops, the extra retain, release overhead cannot be eliminated which
/// makes assigning ranges very slow. Once this has been implemented, this code
/// should be changed to use _UnsafePartiallyInitializedContiguousArrayBuffer.
@_inlineable
@_versioned
internal func _copyCollectionToContiguousArray<
  C : Collection
>(_ source: C) -> ContiguousArray<C.Element>
{
  let count: Int = numericCast(source.count)
  if count == 0 {
    return ContiguousArray()
  }

  let result = _ContiguousArrayBuffer<C.Element>(
    _uninitializedCount: count,
    minimumCapacity: 0)

  var p = result.firstElementAddress
  var i = source.startIndex
  for _ in 0..<count {
    // FIXME(performance): use _copyContents(initializing:).
    p.initialize(to: source[i])
    source.formIndex(after: &i)
    p += 1
  }
  _expectEnd(of: source, is: i)
  return ContiguousArray(_buffer: result)
}

/// A "builder" interface for initializing array buffers.
///
/// This presents a "builder" interface for initializing an array buffer
/// element-by-element. The type is unsafe because it cannot be deinitialized
/// until the buffer has been finalized by a call to `finish`.
@_versioned
@_fixed_layout
internal struct _UnsafePartiallyInitializedContiguousArrayBuffer<Element> {
  @_versioned
  internal var result: _ContiguousArrayBuffer<Element>
  @_versioned
  internal var p: UnsafeMutablePointer<Element>
  @_versioned
  internal var remainingCapacity: Int

  /// Initialize the buffer with an initial size of `initialCapacity`
  /// elements.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  @inline(__always) // For performance reasons.
  internal init(initialCapacity: Int) {
    if initialCapacity == 0 {
      result = _ContiguousArrayBuffer()
    } else {
      result = _ContiguousArrayBuffer(
        _uninitializedCount: initialCapacity,
        minimumCapacity: 0)
    }

    p = result.firstElementAddress
    remainingCapacity = result.capacity
  }

  /// Add an element to the buffer, reallocating if necessary.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  @inline(__always) // For performance reasons.
  internal mutating func add(_ element: Element) {
    if remainingCapacity == 0 {
      // Reallocate.
      let newCapacity = max(_growArrayCapacity(result.capacity), 1)
      var newResult = _ContiguousArrayBuffer<Element>(
        _uninitializedCount: newCapacity, minimumCapacity: 0)
      p = newResult.firstElementAddress + result.capacity
      remainingCapacity = newResult.capacity - result.capacity
      newResult.firstElementAddress.moveInitialize(
        from: result.firstElementAddress, count: result.capacity)
      result.count = 0
      (result, newResult) = (newResult, result)
    }
    addWithExistingCapacity(element)
  }

  /// Add an element to the buffer, which must have remaining capacity.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  @inline(__always) // For performance reasons.
  internal mutating func addWithExistingCapacity(_ element: Element) {
    _sanityCheck(remainingCapacity > 0,
      "_UnsafePartiallyInitializedContiguousArrayBuffer has no more capacity")
    remainingCapacity -= 1

    p.initialize(to: element)
    p += 1
  }

  /// Finish initializing the buffer, adjusting its count to the final
  /// number of elements.
  ///
  /// Returns the fully-initialized buffer. `self` is reset to contain an
  /// empty buffer and cannot be used afterward.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  @inline(__always) // For performance reasons.
  internal mutating func finish() -> ContiguousArray<Element> {
    // Adjust the initialized count of the buffer.
    result.count = result.capacity - remainingCapacity

    return finishWithOriginalCount()
  }

  /// Finish initializing the buffer, assuming that the number of elements
  /// exactly matches the `initialCount` for which the initialization was
  /// started.
  ///
  /// Returns the fully-initialized buffer. `self` is reset to contain an
  /// empty buffer and cannot be used afterward.
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned
  @inline(__always) // For performance reasons.
  internal mutating func finishWithOriginalCount() -> ContiguousArray<Element> {
    _sanityCheck(remainingCapacity == result.capacity - result.count,
      "_UnsafePartiallyInitializedContiguousArrayBuffer has incorrect count")
    var finalResult = _ContiguousArrayBuffer<Element>()
    (finalResult, result) = (result, finalResult)
    remainingCapacity = 0
    return ContiguousArray(_buffer: finalResult)
  }
}
