//===--- Filter.swift - tests for lazy filtering --------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
// RUN: %target-run-simple-swift
// REQUIRES: executable_test

import StdlibUnittest


let FilterTests = TestSuite("Filter")

// Check that the generic parameter is called 'Base'.
protocol TestProtocol1 {}

extension LazyFilterIterator where Base : TestProtocol1 {
  var _baseIsTestProtocol1: Bool {
    fatalError("not implemented")
  }
}

extension LazyFilterSequence where Base : TestProtocol1 {
  var _baseIsTestProtocol1: Bool {
    fatalError("not implemented")
  }
}

extension LazyFilterIndex where Base : TestProtocol1 {
  var _baseIsTestProtocol1: Bool {
    fatalError("not implemented")
  }
}

extension LazyFilterCollection where Base : TestProtocol1 {
  var _baseIsTestProtocol1: Bool {
    fatalError("not implemented")
  }
}

FilterTests.test("filtering collections") {
  let f0 = LazyFilterCollection(_base: 0..<30) { $0 % 7 == 0 }
  expectEqualSequence([0, 7, 14, 21, 28], f0)

  let f1 = LazyFilterCollection(_base: 1..<30) { $0 % 7 == 0 }
  expectEqualSequence([7, 14, 21, 28], f1)
}

FilterTests.test("filtering sequences") {
  let f0 = (0..<30).makeIterator().lazy.filter { $0 % 7 == 0 }
  expectEqualSequence([0, 7, 14, 21, 28], f0)

  let f1 = (1..<30).makeIterator().lazy.filter { $0 % 7 == 0 }
  expectEqualSequence([7, 14, 21, 28], f1)
}

runAllTests()
