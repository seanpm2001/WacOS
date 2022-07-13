// RUN: %target-swift-frontend -typecheck -verify %s

// Ensure that key path exprs can tolerate being re-type-checked when necessary
// to diagnose other errors in adjacent exprs.

struct P<T: K> { }

struct S {
    init<B>(_ a: P<B>) {
        fatalError()
    }
}

protocol K { }

func + <Object>(lhs: KeyPath<A, Object>, rhs: String) -> P<Object> {
    fatalError()
}

// expected-error@+1{{}}
func + (lhs: KeyPath<A, String>, rhs: String) -> P<String> {
    fatalError()
}

struct A {
    let id: String
}

extension A: K {
    static let j = S(\A.id + "id") // expected-error {{'+' cannot be applied}} expected-note {{}}
}

// SR-5034

struct B {
    let v: String
    func f1<T, E>(block: (T) -> E) -> B {
        return self
    }

    func f2<T, E: Equatable>(keyPath: KeyPath<T, E>) {
    }
}
func f3() {
    B(v: "").f1(block: { _ in }).f2(keyPath: \B.v) // expected-error{{}}
}

// SR-5375

protocol Bindable: class { }

extension Bindable {
  func test<Value>(to targetKeyPath: ReferenceWritableKeyPath<Self, Value>, change: Value?) {
    if self[keyPath:targetKeyPath] != change {  // expected-error{{}}
      // expected-note@-1{{overloads for '!=' exist with these partially matching parameter lists: (Self, Self), (_OptionalNilComparisonType, Wrapped?)}}
      self[keyPath: targetKeyPath] = change!
    }
  }
}
