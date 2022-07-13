// RUN: %target-typecheck-verify-swift %s

precedencegroup BindingPrecedence {
  higherThan: DefaultPrecedence
}

infix operator ~>
infix operator ≈> : BindingPrecedence

struct M<L : P, R> {
  let f: L
  let b: (inout L.B) -> R

  init(f: L, b: @escaping (inout L.B) -> R) {
    self.f = f
    self.b = b
  }
}

protocol P {
  associatedtype A
  associatedtype B

  func `in`<R>(_ a: inout A, apply body: (inout B) -> R) -> R

  static func ~> (_: A, _: Self) -> B
}

extension P {
  static func ≈> <R>(f: Self,  b: @escaping (inout B) -> R) -> M<Self, R> {}
}

extension WritableKeyPath : P {
  typealias A = Root
  typealias B = Value

  func `in`<R>(_ a: inout A, apply body: (inout B) -> R) -> R {}

  static func ~> (a: A, path: WritableKeyPath) -> B {}
}

struct X { var y: Int = 0 }
var x = X()
x ~> \X.y ≈> { a in a += 1; return 3 }
// expected-error@-1 {{cannot convert call result type 'M<WritableKeyPath<X, Int>, _>' to expected type 'WritableKeyPath<_, _>'}}
