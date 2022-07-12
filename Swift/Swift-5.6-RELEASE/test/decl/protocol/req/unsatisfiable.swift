// RUN: %target-typecheck-verify-swift -swift-version 4

protocol P {
  associatedtype A
  associatedtype B

  func f<T: P>(_: T) where T.A == Self.A, T.A == Self.B // expected-error{{instance method requirement 'f' cannot add constraint 'Self.A == Self.B' on 'Self'}}
  // expected-note@-1 {{protocol requires function 'f' with type '<T> (T) -> ()'; do you want to add a stub?}}
}

extension P {
  func f<T: P>(_: T) where T.A == Self.A, T.A == Self.B { }
  // expected-note@-1 {{candidate would match if 'X' was the same type as 'X.B' (aka 'Int')}}
}

struct X : P { // expected-error {{type 'X' does not conform to protocol 'P'}}
  typealias A = X
  typealias B = Int
}

protocol P2 {
  associatedtype A

  func f<T: P2>(_: T) where T.A == Self.A, T.A: P2 // expected-error{{instance method requirement 'f' cannot add constraint 'Self.A: P2' on 'Self'}}
}

class C { }

protocol P3 {
  associatedtype A

  func f<T: P3>(_: T) where T.A == Self.A, T.A: C // expected-error{{instance method requirement 'f' cannot add constraint 'Self.A: C' on 'Self'}}
  func g<T: P3>(_: T) where T.A: C, T.A == Self.A  // expected-error{{instance method requirement 'g' cannot add constraint 'Self.A: C' on 'Self'}}
}

protocol Base {
  associatedtype Assoc
}

protocol Sub1: Base {
  associatedtype SubAssoc: Assoc
  // expected-error@-1 {{type 'Self.SubAssoc' constrained to non-protocol, non-class type 'Self.Assoc'}}
}

// FIXME: This error is incorrect in what it states.
protocol Sub2: Base {
  associatedtype SubAssoc where SubAssoc: Assoc // expected-error {{type 'Self.SubAssoc' constrained to non-protocol, non-class type 'Self.Assoc'}}
}

struct S {}

// FIX-ME: One of these errors is redundant.
protocol P4 {
  associatedtype X : S
  // expected-error@-1 {{type 'Self.X' constrained to non-protocol, non-class type 'S'}}
}

protocol P5 {
  associatedtype Y where Y : S // expected-error {{type 'Self.Y' constrained to non-protocol, non-class type 'S'}}
}

protocol P6 {
  associatedtype T
  associatedtype U

  func foo() where T == U
  // expected-error@-1 {{instance method requirement 'foo()' cannot add constraint 'Self.T == Self.U' on 'Self'}}
  // expected-note@-2 {{protocol requires function 'foo()' with type '() -> ()'; do you want to add a stub?}}
}

struct S2 : P6 {
  // expected-error@-1 {{type 'S2' does not conform to protocol 'P6'}}
  typealias T = Int
  typealias U = String

  func foo() {}
  // expected-note@-1 {{candidate has non-matching type '() -> ()'}}

  // FIXME: This error is bogus and should be omitted on account of the protocol requirement itself
  // being invalid.
}

