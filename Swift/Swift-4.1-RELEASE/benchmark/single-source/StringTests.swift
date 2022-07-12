//===--- StringTests.swift ------------------------------------------------===//
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
import TestsUtils

public let StringTests = [
  BenchmarkInfo(name: "StringEqualPointerComparison", runFunction: run_StringEqualPointerComparison, tags: [.validation, .api, .String]),
  BenchmarkInfo(name: "StringHasPrefixAscii", runFunction: run_StringHasPrefixAscii, tags: [.validation, .api, .String]),
  BenchmarkInfo(name: "StringHasPrefixUnicode", runFunction: run_StringHasPrefixUnicode, tags: [.validation, .api, .String]),
  BenchmarkInfo(name: "StringHasSuffixAscii", runFunction: run_StringHasSuffixAscii, tags: [.validation, .api, .String]),
  BenchmarkInfo(name: "StringHasSuffixUnicode", runFunction: run_StringHasSuffixUnicode, tags: [.validation, .api, .String]),
]

// FIXME(string)
public func run_StringHasPrefixAscii(_ N: Int) {
#if _runtime(_ObjC)
  let prefix = "prefix"
  let testString = "prefixedString"
  for _ in 0 ..< N {
    for _ in 0 ..< 100_000 {
      CheckResults(testString.hasPrefix(getString(prefix)))
    }
  }
#endif
}

// FIXME(string)
public func run_StringHasSuffixAscii(_ N: Int) {
#if _runtime(_ObjC)
  let suffix = "Suffixed"
  let testString = "StringSuffixed"
  for _ in 0 ..< N {
    for _ in 0 ..< 100_000 {
      CheckResults(testString.hasSuffix(getString(suffix)))
    }
  }
#endif
}

// FIXME(string)
public func run_StringHasPrefixUnicode(_ N: Int) {
#if _runtime(_ObjC)
  let prefix = "❄️prefix"
  let testString = "❄️prefixedString"
  for _ in 0 ..< N {
    for _ in 0 ..< 100_000 {
      CheckResults(testString.hasPrefix(getString(prefix)))
    }
  }
#endif
}

// FIXME(string)
public func run_StringHasSuffixUnicode(_ N: Int) {
#if _runtime(_ObjC)
  let suffix = "❄️Suffixed"
  let testString = "String❄️Suffixed"
  for _ in 0 ..< N {
    for _ in 0 ..< 100_000 {
      CheckResults(testString.hasSuffix(getString(suffix)))
    }
  }
#endif
}

@inline(never)
internal func compareEqual(_ str1: String, _ str2: String) -> Bool {
  return str1 == str2
}

public func run_StringEqualPointerComparison(_ N: Int) {
  let str1 = "The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. "
  let str2 = str1
  for _ in 0 ..< N {
    for _ in 0 ..< 100_000 {
      CheckResults(compareEqual(str1, str2))
    }
  }
}
