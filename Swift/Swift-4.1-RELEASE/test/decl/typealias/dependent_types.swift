// RUN: %target-typecheck-verify-swift

protocol P {
  associatedtype Assoc = Self
}

struct X : P {
}

class Y<T: P> {
  typealias Assoc = T.Assoc
}

func f<T: P>(_ x: T, y: Y<T>.Assoc) {
}

protocol P1 {
  associatedtype A = Int
}

struct X1<T> : P1 {
  init(_: X1.A) {
  }
}

struct GenericStruct<T> { // expected-note 2{{generic type 'GenericStruct' declared here}}
  typealias Alias = T
  typealias MetaAlias = T.Type

  typealias Concrete = Int

  func methodOne() -> Alias.Type {}
  func methodTwo() -> MetaAlias {}

  func methodOne() -> Alias.BadType {}
  // expected-error@-1 {{'BadType' is not a member type of 'GenericStruct.Alias'}}
  func methodTwo() -> MetaAlias.BadType {}
  // expected-error@-1 {{'BadType' is not a member type of 'GenericStruct.MetaAlias'}}

  var propertyOne: Alias.BadType
  // expected-error@-1 {{'BadType' is not a member type of 'T'}}
  var propertyTwo: MetaAlias.BadType
  // expected-error@-1 {{'BadType' is not a member type of 'T.Type'}}
}

// This was accepted in Swift 3.0 and sort of worked... but we can't
// implement it correctly. In Swift 3.1 it triggered an assert.
// Make sure it's banned now with a proper diagnostic.

func foo() -> Int {}
func metaFoo() -> Int.Type {}

let _: GenericStruct.Alias = foo()
// expected-error@-1 {{reference to generic type 'GenericStruct' requires arguments in <...>}}
let _: GenericStruct.MetaAlias = metaFoo()
// expected-error@-1 {{reference to generic type 'GenericStruct' requires arguments in <...>}}

// ... but if the typealias has a fully concrete underlying type,
// we are OK.
let _: GenericStruct.Concrete = foo()
