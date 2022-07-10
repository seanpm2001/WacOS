// RUN: %target-parse-verify-swift

// Deduction of associated types.
protocol Fooable {
  associatedtype AssocType
  func foo(_ x : AssocType)
}

struct X : Fooable {
  func foo(_ x: Float) {}
}

struct Y<T> : Fooable {
  func foo(_ x: T) {}
}

struct Z : Fooable {
  func foo(_ x: Float) {}

  func blah() {
    var a : AssocType // expected-warning {{variable 'a' was never used; consider replacing with '_' or removing it}} {{9-10=_}}
  }

  // FIXME: We should be able to find this.
  func blarg() -> AssocType {} // expected-error{{use of undeclared type 'AssocType'}}

  func wonka() -> Z.AssocType {}
}

var xa : X.AssocType = Float()
var yf : Y<Float>.AssocType = Float()
var yd : Y<Double>.AssocType = Double()

var f : Float
f = xa
f = yf

var d : Double
d = yd

protocol P1 {
  associatedtype Assoc1
  func foo() -> Assoc1
}

struct S1 : P1 {
  func foo() -> X {}
}

prefix operator %

protocol P2 {
  associatedtype Assoc2
  static prefix func %(target: Self) -> Assoc2
}

prefix func % <P:P1>(target: P) -> P.Assoc1 {
}

extension S1 : P2 {
  typealias Assoc2 = X
}

// <rdar://problem/14418181>
protocol P3 {
  associatedtype Assoc3
  func foo() -> Assoc3
}

protocol P4 : P3 {
  associatedtype Assoc4
  func bar() -> Assoc4
}

func takeP4<T : P4>(_ x: T) { }

struct S4<T> : P3, P4 {
  func foo() -> Int {}
  func bar() -> Double {}
}

takeP4(S4<Int>())

// <rdar://problem/14680393>
infix operator ~>

protocol P5 { }

struct S7a {}

protocol P6 {
  func foo<Target: P5>(_ target: inout Target)
}

protocol P7 : P6 {
  associatedtype Assoc : P6
  static func ~> (x: Self, _: S7a) -> Assoc
}

func ~> <T:P6>(x: T, _: S7a) -> S7b { return S7b() }

struct S7b : P7 {
  typealias Assoc = S7b
  func foo<Target: P5>(_ target: inout Target) {}
}

// <rdar://problem/14685674>
struct zip<A : IteratorProtocol, B : IteratorProtocol>
  : IteratorProtocol, Sequence {

  func next() -> (A.Element, B.Element)? { }

  typealias Generator = zip
  func makeIterator() -> zip { }
}

protocol P8 { }

protocol P9 {
  associatedtype A1 : P8
}

protocol P10 {
  associatedtype A1b : P8
  associatedtype A2 : P9

  func f()
  func g(_ a: A1b)
  func h(_ a: A2)
}

struct X8 : P8 { }

struct Y9 : P9 {
  typealias A1 = X8
}

struct Z10 : P10 {
  func f() { }
  func g(_ a: X8) { }
  func h(_ a: Y9) { }
}


struct W : Fooable {
  func foo(_ x: String) {}
}
struct V<T> : Fooable {
  func foo(_ x: T) {}
}

// FIXME: <rdar://problem/16123805> associated Inferred types can't be used in expression contexts
var w = W.AssocType()
var v = V<String>.AssocType()

//
// SR-427
protocol A {
  func c()
}

protocol B : A {
  associatedtype e : A = C<Self> // expected-note {{default type 'C<C<a>>' for associated type 'e' (from protocol 'B') does not conform to 'A'}}
}

extension B {
  func c() {
  }
}

struct C<a : B> : B { // expected-error {{type 'C<a>' does not conform to protocol 'B'}}
}

// SR-511
protocol sr511 {
  typealias Foo // expected-error {{typealias is missing an assigned type; use 'associatedtype' to define an associated type requirement}}
}

associatedtype Foo = Int // expected-error {{associated types can only be defined in a protocol; define a type or introduce a 'typealias' to satisfy an associated type requirement}}

