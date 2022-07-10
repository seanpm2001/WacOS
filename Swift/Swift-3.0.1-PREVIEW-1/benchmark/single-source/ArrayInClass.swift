//===--- ArrayInClass.swift -----------------------------------------------===//
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

class ArrayContainer {
  final var arr : [Int]

  init() {
    arr = [Int] (repeating: 0, count: 100_000)
  }

  func runLoop(_ N: Int) {
    for _ in 0 ..< N {
      for i in 0 ..< arr.count {
        arr[i] = arr[i] + 1
      }
    }
  }
}

@inline(never)
func getArrayContainer() -> ArrayContainer {
  return ArrayContainer()
}

@inline(never)
public func run_ArrayInClass(_ N: Int) {
  let a = getArrayContainer()
  a.runLoop(N)
}
