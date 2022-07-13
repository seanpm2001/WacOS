// RUN: %target-run-simple-swift | %FileCheck %s
// REQUIRES: executable_test

// REQUIRES: objc_interop

import Foundation

let key = "inquiens"
let value = "λαμπερός"

let s1 = NSLocalizedString(key, comment: "Hello")
// CHECK: key = inquiens s1 = inquiens
print("key = \(key) s1 = \(s1)")

let s2 = NSLocalizedString(key, tableName:nil, bundle: Bundle.main, value: value, comment: "Hello")
// CHECK: key = inquiens s2 = λαμπερός
print("key = \(key) s2 = \(s2)")

