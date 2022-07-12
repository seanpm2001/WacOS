//===--- IterateData.swift ------------------------------------------------===//
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
import Foundation

public let IterateData = BenchmarkInfo(
  name: "IterateData",
  runFunction: run_IterateData,
  tags: [.validation, .api])

@inline(never)
func generateData() -> Data {
  var data = Data(count: 16 * 1024)
  data.withUnsafeMutableBytes { (ptr: UnsafeMutablePointer<UInt8>) -> () in
    for i in 0..<data.count {
      ptr[i] = UInt8(i % 23)
    }
  }
  return data
}

@inline(never)
public func run_IterateData(_ N: Int) {
  let data = generateData()

  for _ in 0...10*N {
    _ = data.reduce(0, &+)
  }
}
