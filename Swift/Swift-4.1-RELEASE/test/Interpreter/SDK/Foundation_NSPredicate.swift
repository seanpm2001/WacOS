// RUN: %target-run-simple-swift | %FileCheck %s
// REQUIRES: executable_test
// rdar://16726530

// REQUIRES: objc_interop

import Foundation

// Test overlain variadic methods.
let s = NSPredicate(format: "(lastName like[cd] %@) AND (birthday > %@)", "LLLL", "BBBB")
print(s.predicateFormat)

// CHECK: lastName LIKE[cd] "LLLL" AND birthday > "BBBB"
