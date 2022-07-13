// Part of the multi-file.swift test.

import ambiguous_right
import tilde_tilde_high_precedence

class DefaultInit {
  var x: DefaultInit! // = nil by default
}

func test2() {
  funcOrVar = 1
  var _: () -> () = funcOrVar // expected-error{{cannot convert value of type 'Int' to specified type '() -> ()'}}

  _ = SomeStruct(value: 42) // use the SomeStruct from multi-file.swift

  var _: Bool = true && 1 ~~ 2 && false // true && (1 ~~ 2) && false
}

func testOverriding(sub: Subclass) {
  sub.method()
}


class Base {
  func foo() {}
  var prop: Int = 0
}

class Sub : Base {
  override func foo() {}
  override var prop: Int {
    didSet { print("hi") }
  }
}
