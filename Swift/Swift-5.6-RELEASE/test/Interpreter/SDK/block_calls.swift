// RUN: %target-run-simple-swift | %FileCheck %s
// REQUIRES: executable_test

// REQUIRES: objc_interop

import Foundation

class Foo : NSObject {
  let foo: Int

  init(_ foo: Int) {
    self.foo = foo
    super.init()
  }

  @objc func applyBlock(_ f: (Int) -> (), withInt x: Int) {
    f(foo)
    f(x)
  }

  @objc func applyBlock(_ f: (Int) -> (), withFoo x: Foo) {
    f(foo)
    f(x.foo)
  }
}

// CHECK: 123
// CHECK: 22
Foo(123).applyBlock({ print($0) }, withInt:22)
// CHECK: 321
// CHECK: 44
Foo(321).applyBlock({ print($0) }, withFoo: Foo(44))
