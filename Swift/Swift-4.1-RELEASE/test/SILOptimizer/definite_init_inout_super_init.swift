// RUN: %target-swift-frontend -emit-sil -verify %s -o /dev/null

class B {
  init(x: inout Int) {}
}

class A : B {
  let x: Int

  init() {
    self.x = 12
    super.init(x: &x) // expected-error {{immutable value 'self.x' must not be passed inout}}
  }
}

