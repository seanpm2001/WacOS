// RUN: %swift %s -emit-ir | %FileCheck %s
// RUN: %swift -target x86_64-apple-macosx10.12 %s -emit-ir | %FileCheck -check-prefix=CHECK-SPECIFIC %s
// RUN: %swift -target x86_64-apple-darwin16 %s -emit-ir | %FileCheck -check-prefix=CHECK-SPECIFIC %s

// REQUIRES: OS=macosx

// CHECK: target triple = "x86_64-apple-macosx10.
// CHECK-SPECIFIC: target triple = "x86_64-apple-macosx10.12"

public func anchor() {}
anchor()

