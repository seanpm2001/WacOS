// RUN: %target-swift-frontend %s -emit-ir | %FileCheck %s

// Check that we emit all local decls, not just the first one.
func test1() {
  class a {
    var x, y : Int
    init() {
      x = 0
      y = 0
    }
  }
  class b {
    var a, b : Int
    init() {
      a = 0
      b = 0
    }
  }
  let x : a, y : b
}

// Check that we emit nominal type descriptors for all types.
// CHECK-DAG: @"$s5decls5test1yyF1aL_CMn" = internal constant
// CHECK-DAG: @"$s5decls5test1yyF1bL_CMn" = internal constant

