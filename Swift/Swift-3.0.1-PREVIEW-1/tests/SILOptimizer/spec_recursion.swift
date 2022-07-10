// RUN: %target-swift-frontend -O -emit-sil %s | %FileCheck %s

// Make sure we are not looping forever.

extension Array {
  mutating func new_method(_ predicate: (Element, Element) -> Bool, left : Int, right : Int) {
      new_method(predicate, left: left, right: right);
  }
}
var x1 = [1]
x1.new_method(<, left: 0, right: 1)


struct Test<T> {
  init() {}
  func recursive(x x : T) {
    return recursive(x: x)
  }
}

// Make sure that the specialized function calls itself.
//CHECK:   sil shared @_TTSg5Si___TFV14spec_recursion4Test9recursive
//CHECK: function_ref @_TTSg5Si___TFV14spec_recursion4Test9recursive
//CHECK: return
var x2 = Test<Int>()
x2.recursive(x: 3)


