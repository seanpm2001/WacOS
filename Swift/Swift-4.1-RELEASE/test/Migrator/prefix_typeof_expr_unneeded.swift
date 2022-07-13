// RUN: %empty-directory(%t) && %target-swift-frontend -typecheck -update-code -primary-file %s -emit-migrated-file-path %t.result
// RUN: diff -u %s.expected %t.result
// RUN: %target-swift-frontend -typecheck %t.result -swift-version 4

class HasTypeMethod {
  var type: Int {
    _ = type(of: 1)
    return 1
  }
}

class NoTypeMethod {
  func meth() {
    _ = type(of: 1) // Don't need to add prefix
  }
}
