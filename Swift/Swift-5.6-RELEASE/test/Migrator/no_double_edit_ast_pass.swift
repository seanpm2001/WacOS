// REQUIRES: OS=macosx
// REQUIRES: objc_interop
// RUN: %target-swift-frontend -typecheck %s -F %S/mock-sdk
// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -c -F %S/mock-sdk -api-diff-data-file %S/Inputs/DoubleEditAPI.json -update-code -primary-file %s -emit-migrated-file-path %t/no_double_edit_ast_pass.result -o /dev/null
// RUN: diff -u %s.expected %t/no_double_edit_ast_pass.result

import Bar

class Derived : WillOverrideWithTypeChange {
  func doThing(_ thing: SomeItemSet) -> SomeItemSet {
    return SomeItemSet()
  }
}

