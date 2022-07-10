//===--- ArraySubscript.swift ---------------------------------------------===//
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

// This test checks the performance of modifying an array element.
import TestsUtils

@inline(never)
public func run_ArraySubscript(_ N: Int) {
  SRand()

  let numArrays = 200*N
  let numArrayElements = 100

  func bound(_ x: Int) -> Int { return min(x, numArrayElements-1) }

  var arrays = [[Int]](repeating: [], count: numArrays)
  for i in 0..<numArrays {
    for _ in 0..<numArrayElements {
      arrays[i].append(Int(truncatingBitPattern: Random()))
    }
  }

  // Do a max up the diagonal.
  for i in 1..<numArrays {
    arrays[i][bound(i)] =
      max(arrays[i-1][bound(i-1)], arrays[i][bound(i)])
  }
  CheckResults(arrays[0][0] <= arrays[numArrays-1][bound(numArrays-1)],
               "Incorrect results in QuickSort.")
}
