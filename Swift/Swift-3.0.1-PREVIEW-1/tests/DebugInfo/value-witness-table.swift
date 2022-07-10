// RUN: %target-swift-frontend -g -emit-ir %s | %FileCheck %s --check-prefix=CHECK-%target-ptrsize --check-prefix=CHECK

// Test that we don't generate debug info for hidden global variables.

// CHECK: @_TWVO4main2E1 ={{.*}} constant
// CHECK-NOT: !DIGlobalVariable
public enum E1 { case E }
public func f() -> E1 { return .E }
