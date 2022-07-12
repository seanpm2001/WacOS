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

#if _runtime(_ObjC)
@_inlineable // FIXME(sil-serialize-all)
@_versioned // FIXME(sil-serialize-all)
@_silgen_name("swift_stdlib_NSStringHashValue")
internal func _stdlib_NSStringHashValue(
  _ str: AnyObject, _ isASCII: Bool) -> Int

@_inlineable // FIXME(sil-serialize-all)
@_versioned // FIXME(sil-serialize-all)
@_silgen_name("swift_stdlib_NSStringHashValuePointer")
internal func _stdlib_NSStringHashValuePointer(
  _ str: OpaquePointer, _ isASCII: Bool) -> Int

@_inlineable // FIXME(sil-serialize-all)
@_versioned // FIXME(sil-serialize-all)
@_silgen_name("swift_stdlib_CFStringHashCString")
internal func _stdlib_CFStringHashCString(
  _ str: OpaquePointer, _ len: Int) -> Int
#endif

extension Unicode {
  // FIXME: cannot be marked @_versioned. See <rdar://problem/34438258>
  // @_inlineable // FIXME(sil-serialize-all)
  // @_versioned // FIXME(sil-serialize-all)
  internal static func hashASCII(
    _ string: UnsafeBufferPointer<UInt8>
  ) -> Int {
    let collationTable = _swift_stdlib_unicode_getASCIICollationTable()
    var hasher = _SipHash13Context(key: _Hashing.secretKey)
    for c in string {
      _precondition(c <= 127)
      let element = collationTable[Int(c)]
      // Ignore zero valued collation elements. They don't participate in the
      // ordering relation.
      if element != 0 {
        hasher.append(element)
      }
    }
    return hasher._finalizeAndReturnIntHash()
  }

  // FIXME: cannot be marked @_versioned. See <rdar://problem/34438258>
  // @_inlineable // FIXME(sil-serialize-all)
  // @_versioned // FIXME(sil-serialize-all)
  internal static func hashUTF16(
    _ string: UnsafeBufferPointer<UInt16>
  ) -> Int {
    let collationIterator = _swift_stdlib_unicodeCollationIterator_create(
      string.baseAddress!,
      UInt32(string.count))
    defer { _swift_stdlib_unicodeCollationIterator_delete(collationIterator) }

    var hasher = _SipHash13Context(key: _Hashing.secretKey)
    while true {
      var hitEnd = false
      let element =
        _swift_stdlib_unicodeCollationIterator_next(collationIterator, &hitEnd)
      if hitEnd {
        break
      }
      // Ignore zero valued collation elements. They don't participate in the
      // ordering relation.
      if element != 0 {
        hasher.append(element)
      }
    }
    return hasher._finalizeAndReturnIntHash()
  }
}

@_versioned // FIXME(sil-serialize-all)
@inline(never) // Hide the CF dependency
internal func _hashString(_ string: String) -> Int {
  let core = string._core
#if _runtime(_ObjC)
    // Mix random bits into NSString's hash so that clients don't rely on
    // Swift.String.hashValue and NSString.hash being the same.
#if arch(i386) || arch(arm)
    let hashOffset = Int(bitPattern: 0x88dd_cc21)
#else
    let hashOffset = Int(bitPattern: 0x429b_1266_88dd_cc21)
#endif
  // If we have a contiguous string then we can use the stack optimization.
  let isASCII = core.isASCII
  if core.hasContiguousStorage {
    if isASCII {
      return hashOffset ^ _stdlib_CFStringHashCString(
                              OpaquePointer(core.startASCII), core.count)
    } else {
      let stackAllocated = _NSContiguousString(core)
      return hashOffset ^ stackAllocated._unsafeWithNotEscapedSelfPointer {
        return _stdlib_NSStringHashValuePointer($0, false)
      }
    }
  } else {
    let cocoaString = unsafeBitCast(
      string._bridgeToObjectiveCImpl(), to: _NSStringCore.self)
    return hashOffset ^ _stdlib_NSStringHashValue(cocoaString, isASCII)
  }
#else
  if let asciiBuffer = core.asciiBuffer {
    return Unicode.hashASCII(UnsafeBufferPointer(
      start: asciiBuffer.baseAddress!,
      count: asciiBuffer.count))
  } else {
    return Unicode.hashUTF16(
      UnsafeBufferPointer(start: core.startUTF16, count: core.count))
  }
#endif
}


extension String : Hashable {
  /// The string's hash value.
  ///
  /// Hash values are not guaranteed to be equal across different executions of
  /// your program. Do not save hash values to use during a future execution.
  @_inlineable // FIXME(sil-serialize-all)
  public var hashValue: Int {
    return _hashString(self)
  }
}

