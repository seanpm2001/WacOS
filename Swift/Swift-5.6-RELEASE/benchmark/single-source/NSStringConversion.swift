//===--- NSStringConversion.swift -----------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

// <rdar://problem/19003201>
#if canImport(Darwin)

import TestsUtils
import Foundation

fileprivate var test:NSString = ""
fileprivate var mutableTest = ""

public let benchmarks = [
  BenchmarkInfo(name: "NSStringConversion",
                runFunction: run_NSStringConversion,
                tags: [.validation, .api, .String, .bridging]),
  BenchmarkInfo(name: "NSStringConversion.UTF8",
                runFunction: run_NSStringConversion_nonASCII,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { test = NSString(cString: "tëst", encoding: String.Encoding.utf8.rawValue)! }),
  BenchmarkInfo(name: "NSStringConversion.Mutable",
                runFunction: run_NSMutableStringConversion,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { test = NSMutableString(cString: "test", encoding: String.Encoding.ascii.rawValue)! }),
  BenchmarkInfo(name: "NSStringConversion.Medium",
                runFunction: run_NSStringConversion_medium,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { test = NSString(cString: "aaaaaaaaaaaaaaa", encoding: String.Encoding.ascii.rawValue)! } ),
  BenchmarkInfo(name: "NSStringConversion.Long",
                runFunction: run_NSStringConversion_long,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { test = NSString(cString: "The quick brown fox jumps over the lazy dog", encoding: String.Encoding.ascii.rawValue)! } ),
  BenchmarkInfo(name: "NSStringConversion.LongUTF8",
                runFunction: run_NSStringConversion_longNonASCII,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { test = NSString(cString: "Thë qüick bröwn föx jumps over the lazy dög", encoding: String.Encoding.utf8.rawValue)! } ),
  BenchmarkInfo(name: "NSStringConversion.Rebridge",
                runFunction: run_NSStringConversion_rebridge,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { test = NSString(cString: "test", encoding: String.Encoding.ascii.rawValue)! }),
  BenchmarkInfo(name: "NSStringConversion.Rebridge.UTF8",
                runFunction: run_NSStringConversion_nonASCII_rebridge,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { test = NSString(cString: "tëst", encoding: String.Encoding.utf8.rawValue)! }),
  BenchmarkInfo(name: "NSStringConversion.Rebridge.Mutable",
                runFunction: run_NSMutableStringConversion_rebridge,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { test = NSMutableString(cString: "test", encoding: String.Encoding.ascii.rawValue)! }),
  BenchmarkInfo(name: "NSStringConversion.Rebridge.Medium",
                runFunction: run_NSStringConversion_medium_rebridge,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { test = NSString(cString: "aaaaaaaaaaaaaaa", encoding: String.Encoding.ascii.rawValue)! } ),
  BenchmarkInfo(name: "NSStringConversion.Rebridge.Long",
                runFunction: run_NSStringConversion_long_rebridge,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { test = NSString(cString: "The quick brown fox jumps over the lazy dog", encoding: String.Encoding.ascii.rawValue)! } ),
  BenchmarkInfo(name: "NSStringConversion.Rebridge.LongUTF8",
                runFunction: run_NSStringConversion_longNonASCII_rebridge,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { test = NSString(cString: "Thë qüick bröwn föx jumps over the lazy dög", encoding: String.Encoding.utf8.rawValue)! } ),
  BenchmarkInfo(name: "NSStringConversion.MutableCopy.UTF8",
                runFunction: run_NSStringConversion_nonASCIIMutable,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { mutableTest = "tëst" }),
  BenchmarkInfo(name: "NSStringConversion.MutableCopy.Medium",
                runFunction: run_NSStringConversion_mediumMutable,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { mutableTest = "aaaaaaaaaaaaaaa" } ),
  BenchmarkInfo(name: "NSStringConversion.MutableCopy.Long",
                runFunction: run_NSStringConversion_longMutable,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { mutableTest = "The quick brown fox jumps over the lazy dog" } ),
  BenchmarkInfo(name: "NSStringConversion.MutableCopy.LongUTF8",
                runFunction: run_NSStringConversion_longNonASCIIMutable,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { mutableTest = "Thë qüick bröwn föx jumps over the lazy dög" } ),
  BenchmarkInfo(name: "NSStringConversion.MutableCopy.Rebridge",
                runFunction: run_NSStringConversion_rebridgeMutable,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { mutableTest = "test" }),
  BenchmarkInfo(name: "NSStringConversion.MutableCopy.Rebridge.UTF8",
                runFunction: run_NSStringConversion_nonASCII_rebridgeMutable,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { mutableTest = "tëst" }),
  BenchmarkInfo(name: "NSStringConversion.MutableCopy.Rebridge.Medium",
                runFunction: run_NSStringConversion_medium_rebridgeMutable,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { mutableTest = "aaaaaaaaaaaaaaa" } ),
  BenchmarkInfo(name: "NSStringConversion.MutableCopy.Rebridge.Long",
                runFunction: run_NSStringConversion_long_rebridgeMutable,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { mutableTest = "The quick brown fox jumps over the lazy dog" } ),
  BenchmarkInfo(name: "NSStringConversion.MutableCopy.Rebridge.LongUTF8",
                runFunction: run_NSStringConversion_longNonASCII_rebridgeMutable,
                tags: [.validation, .api, .String, .bridging],
                setUpFunction: { mutableTest = "Thë qüick bröwn föx jumps over the lazy dög" } )
]

public func run_NSStringConversion(_ n: Int) {
let test:NSString = NSString(cString: "test", encoding: String.Encoding.ascii.rawValue)!
  for _ in 1...n * 10000 {
    //Doesn't test accessing the String contents to avoid changing historical benchmark numbers
    blackHole(identity(test) as String)
  }
}

fileprivate func innerLoop(_ str: NSString, _ n: Int, _ scale: Int = 5000) {
  for _ in 1...n * scale {
    for char in (identity(str) as String).utf8 {
      blackHole(char)
    }
  }
}

public func run_NSStringConversion_nonASCII(_ n: Int) {
  innerLoop(test, n, 2500)
}

public func run_NSMutableStringConversion(_ n: Int) {
  innerLoop(test, n)
}

public func run_NSStringConversion_medium(_ n: Int) {
  innerLoop(test, n, 1000)
}

public func run_NSStringConversion_long(_ n: Int) {
  innerLoop(test, n, 1000)
}

public func run_NSStringConversion_longNonASCII(_ n: Int) {
  innerLoop(test, n, 300)
}

fileprivate func innerMutableLoop(_ str: String, _ n: Int, _ scale: Int = 5000) {
  for _ in 1...n * scale {
    let copy = (str as NSString).mutableCopy() as! NSMutableString
    for char in (identity(copy) as String).utf8 {
      blackHole(char)
    }
  }
}

public func run_NSStringConversion_nonASCIIMutable(_ n: Int) {
  innerMutableLoop(mutableTest, n, 500)
}

public func run_NSStringConversion_mediumMutable(_ n: Int) {
  innerMutableLoop(mutableTest, n, 500)
}

public func run_NSStringConversion_longMutable(_ n: Int) {
  innerMutableLoop(mutableTest, n, 250)
}

public func run_NSStringConversion_longNonASCIIMutable(_ n: Int) {
  innerMutableLoop(mutableTest, n, 150)
}

fileprivate func innerRebridge(_ str: NSString, _ n: Int, _ scale: Int = 5000) {
  for _ in 1...n * scale {
    let bridged = identity(str) as String
    blackHole(bridged)
    blackHole(bridged as NSString)
  }
}

public func run_NSStringConversion_rebridge(_ n: Int) {
  innerRebridge(test, n, 2500)
}

public func run_NSStringConversion_nonASCII_rebridge(_ n: Int) {
  innerRebridge(test, n, 2500)
}

public func run_NSMutableStringConversion_rebridge(_ n: Int) {
  innerRebridge(test, n)
}

public func run_NSStringConversion_medium_rebridge(_ n: Int) {
  innerRebridge(test, n, 1000)
}

public func run_NSStringConversion_long_rebridge(_ n: Int) {
  innerRebridge(test, n, 1000)
}

public func run_NSStringConversion_longNonASCII_rebridge(_ n: Int) {
  innerRebridge(test, n, 300)
}

fileprivate func innerMutableRebridge(_ str: String, _ n: Int, _ scale: Int = 5000) {
  for _ in 1...n * scale {
    let copy = (str as NSString).mutableCopy() as! NSMutableString
    let bridged = identity(copy) as String
    blackHole(bridged)
    blackHole(bridged as NSString)
  }
}

public func run_NSStringConversion_rebridgeMutable(_ n: Int) {
  innerMutableRebridge(mutableTest, n, 1000)
}

public func run_NSStringConversion_nonASCII_rebridgeMutable(_ n: Int) {
  innerMutableRebridge(mutableTest, n, 500)
}

public func run_NSStringConversion_medium_rebridgeMutable(_ n: Int) {
  innerMutableRebridge(mutableTest, n, 500)
}

public func run_NSStringConversion_long_rebridgeMutable(_ n: Int) {
  innerMutableRebridge(mutableTest, n, 500)
}

public func run_NSStringConversion_longNonASCII_rebridgeMutable(_ n: Int) {
  innerMutableRebridge(mutableTest, n, 300)
}

#endif
