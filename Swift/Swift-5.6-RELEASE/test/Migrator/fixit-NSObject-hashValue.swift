// RUN: %empty-directory(%t)
// RUN: not %target-swift-frontend -typecheck -update-code -primary-file %s -emit-migrated-file-path %t/fixit-NSObject-hashValue.result
// RUN: diff -u %s.expected %t/fixit-NSObject-hashValue.result
// RUN: %target-swift-frontend -typecheck %s.expected

// REQUIRES: objc_interop

import ObjectiveC

class MyClass: NSObject {
  override var hashValue: Int {
    return 42
  }
}
