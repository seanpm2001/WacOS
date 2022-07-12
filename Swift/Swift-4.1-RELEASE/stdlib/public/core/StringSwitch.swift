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

// This file contains compiler intrinsics for optimized string switch
// implementations. All functions and types declared in this file are not
// intended to be used by switch source directly.

/// The compiler intrinsic which is called to lookup a string in a table
/// of static string case values.
@_semantics("findStringSwitchCase")
public // COMPILER_INTRINSIC
func _findStringSwitchCase(
  cases: [StaticString],
  string: String) -> Int {

  for (idx, s) in cases.enumerated() {
    if String(_builtinStringLiteral: s.utf8Start._rawValue,
              utf8CodeUnitCount: s._utf8CodeUnitCount,
              isASCII: s.isASCII._value) == string {
      return idx
    }
  }
  return -1
}

@_fixed_layout // needs known size for static allocation
public // used by COMPILER_INTRINSIC
struct _OpaqueStringSwitchCache {
  var a: Builtin.Word
  var b: Builtin.Word
}

internal typealias _StringSwitchCache = Dictionary<String, Int>

@_fixed_layout // FIXME(sil-serialize-all)
@_versioned // FIXME(sil-serialize-all)
internal struct _StringSwitchContext {
  @_inlineable // FIXME(sil-serialize-all)
  @_versioned // FIXME(sil-serialize-all)
  internal init(
    cases: [StaticString],
    cachePtr: UnsafeMutablePointer<_StringSwitchCache>
  ){
    self.cases = cases
    self.cachePtr = cachePtr
  }

  @_versioned // FIXME(sil-serialize-all)
  internal let cases: [StaticString]
  @_versioned // FIXME(sil-serialize-all)
  internal let cachePtr: UnsafeMutablePointer<_StringSwitchCache>
}

/// The compiler intrinsic which is called to lookup a string in a table
/// of static string case values.
///
/// The first time this function is called, a cache is built and stored
/// in \p cache. Consecutive calls use the cache for faster lookup.
/// The \p cases array must not change between subsequent calls with the
/// same \p cache.
@_semantics("findStringSwitchCaseWithCache")
public // COMPILER_INTRINSIC
func _findStringSwitchCaseWithCache(
  cases: [StaticString],
  string: String,
  cache: inout _OpaqueStringSwitchCache) -> Int {

  return withUnsafeMutableBytes(of: &cache) {
    (bufPtr: UnsafeMutableRawBufferPointer) -> Int in

    let oncePtr = bufPtr.baseAddress!
    let cacheRawPtr = oncePtr + MemoryLayout<Builtin.Word>.stride
    let cachePtr = cacheRawPtr.bindMemory(to: _StringSwitchCache.self, capacity: 1)
    var context = _StringSwitchContext(cases: cases, cachePtr: cachePtr)
    withUnsafeMutablePointer(to: &context) { (context) -> () in
      Builtin.onceWithContext(oncePtr._rawValue, _createStringTableCache,
                              context._rawValue)
    }
    let cache = cachePtr.pointee;
    if let idx = cache[string] {
      return idx
    }
    return -1
  }
}

/// Builds the string switch case.
@_inlineable // FIXME(sil-serialize-all)
@_versioned // FIXME(sil-serialize-all)
internal func _createStringTableCache(_ cacheRawPtr: Builtin.RawPointer) {
  let context = UnsafePointer<_StringSwitchContext>(cacheRawPtr).pointee
  var cache = _StringSwitchCache()
  cache.reserveCapacity(context.cases.count)
  assert(MemoryLayout<_StringSwitchCache>.size <= MemoryLayout<Builtin.Word>.size)

  for (idx, s) in context.cases.enumerated() {
    let key = String(_builtinStringLiteral: s.utf8Start._rawValue,
                     utf8CodeUnitCount: s._utf8CodeUnitCount,
                     isASCII: s.isASCII._value)
    cache[key] = idx
  }
  context.cachePtr.initialize(to: cache)
}

