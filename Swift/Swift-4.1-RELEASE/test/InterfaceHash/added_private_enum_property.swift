// RUN: %empty-directory(%t)
// RUN: %utils/split_file.py -o %t %s
// RUN: %target-swift-frontend -dump-interface-hash %t/a.swift 2> %t/a.hash
// RUN: %target-swift-frontend -dump-interface-hash %t/b.swift 2> %t/b.hash
// RUN: not cmp %t/a.hash %t/b.hash

// BEGIN a.swift
enum A {
  case x, y
  func f2() -> Int {
    return 0
  }
}

// BEGIN b.swift
enum A {
  case x, y
  func f2() -> Int {
    return 0
  }

  private var foo: Int { return 0 }
}
