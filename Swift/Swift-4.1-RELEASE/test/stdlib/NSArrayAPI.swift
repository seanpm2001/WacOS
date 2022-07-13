// RUN: %target-run-simple-swift
// REQUIRES: executable_test

// REQUIRES: objc_interop

import StdlibUnittest


import Foundation

var NSArrayAPI = TestSuite("NSArrayAPI")

NSArrayAPI.test("mixed types with AnyObject") {
  do {
    let result: AnyObject = [1, "two"] as NSArray
    let expect: NSArray = [1, "two"]
    expectEqual(expect, result as! NSArray)
  }
  do {
    let result: AnyObject = [1, 2] as NSArray
    let expect: NSArray = [1, 2]
    expectEqual(expect, result as! NSArray)
  }
}

NSArrayAPI.test("CustomStringConvertible") {
  let result = String(describing: NSArray(objects:"A", "B", "C", "D"))
  let expect = "(\n    A,\n    B,\n    C,\n    D\n)"
  expectEqual(expect, result)
}

NSArrayAPI.test("copy construction") {
  let expected = ["A", "B", "C", "D"]
  let x = NSArray(array: expected as NSArray)
  expectEqual(expected, x as! Array)
  let y = NSMutableArray(array: expected as NSArray)
  expectEqual(expected, y as NSArray as! Array)
}

var NSMutableArrayAPI = TestSuite("NSMutableArrayAPI")

NSMutableArrayAPI.test("CustomStringConvertible") {
  let result = String(describing: NSMutableArray(objects:"A", "B", "C", "D"))
  let expect = "(\n    A,\n    B,\n    C,\n    D\n)"
  expectEqual(expect, result)
}

runAllTests()
