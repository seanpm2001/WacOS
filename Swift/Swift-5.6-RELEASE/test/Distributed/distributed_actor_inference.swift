// RUN: %target-typecheck-verify-swift -enable-experimental-distributed -disable-availability-checking
// REQUIRES: concurrency
// REQUIRES: distributed

import _Distributed

/// Use the existential wrapper as the default actor transport.
typealias DefaultActorTransport = AnyActorTransport

actor SomeActor { }

// ==== ------------------------------------------------------------------------
// MARK: Declaring distributed actors
// GOOD:
@available(SwiftStdlib 5.6, *)
distributed actor SomeDistributedActor_0 { }

// BAD:
@available(SwiftStdlib 5.6, *)
distributed class SomeDistributedActor_1 { } // expected-error{{'distributed' can only be applied to 'actor' definitions, and distributed actor-isolated async functions}}
@available(SwiftStdlib 5.6, *)
distributed struct SomeDistributedActor_2 { } // expected-error{{'distributed' modifier cannot be applied to this declaration}}
@available(SwiftStdlib 5.6, *)
distributed enum SomeDistributedActor_3 { } // expected-error{{'distributed' modifier cannot be applied to this declaration}}

// ==== ------------------------------------------------------------------------
// MARK: Declaring distributed functions
// NOTE: not distributed actor, so cannot have any distributed functions

@available(SwiftStdlib 5.6, *)
struct SomeNotActorStruct_2 {
  distributed func nopeAsyncThrows() async throws -> Int { 42 } // expected-error{{'distributed' method can only be declared within 'distributed actor'}}
}

@available(SwiftStdlib 5.6, *)
class SomeNotActorClass_3 {
  distributed func nopeAsyncThrows() async throws -> Int { 42 } // expected-error{{'distributed' method can only be declared within 'distributed actor'}}
}

@available(SwiftStdlib 5.6, *)
actor SomeNotDistributedActor_4 {
  distributed func notInDistActorAsyncThrowing() async throws -> Int { 42 } // expected-error{{'distributed' method can only be declared within 'distributed actor'}}
}

protocol DP {
  distributed func hello()  // expected-error{{'distributed' method can only be declared within 'distributed actor'}}
}

@available(SwiftStdlib 5.6, *)
protocol DPOK: DistributedActor {
  distributed func hello()  // ok
}

@available(SwiftStdlib 5.6, *)
protocol DPOK2: DPOK {
  distributed func again()  // ok
}

@available(SwiftStdlib 5.6, *)
enum SomeNotActorEnum_5 {
  distributed func nopeAsyncThrows() async throws -> Int { 42 } // expected-error{{'distributed' method can only be declared within 'distributed actor'}}
}

@available(SwiftStdlib 5.6, *)
distributed actor SomeDistributedActor_6 {
  distributed func yay() async throws -> Int { 42 } // ok
}

@available(SwiftStdlib 5.6, *)
distributed actor SomeDistributedActor_7 {
  distributed func dont_1() async throws -> Int { 42 } // expected-error{{distributed instance method's 'dont_1' remote counterpart '_remote_dont_1' cannot not be implemented manually.}}
  distributed func dont_2() async throws -> Int { 42 } // expected-error{{distributed instance method's 'dont_2' remote counterpart '_remote_dont_2' cannot not be implemented manually.}}
  distributed func dont_3() async throws -> Int { 42 } // expected-error{{distributed instance method's 'dont_3' remote counterpart '_remote_dont_3' cannot not be implemented manually.}}
}

@available(SwiftStdlib 5.6, *)
extension SomeDistributedActor_7 {

  // TODO: we should diagnose a bit more precisely here

  static func _remote_dont_1(actor: SomeDistributedActor_6) async throws -> Int {
    fatalError()
  }
  static func _remote_dont_2(actor: SomeDistributedActor_6) -> Int {
    fatalError()
  }
  static func _remote_dont_3(actor: SomeDistributedActor_6) -> Int {
    fatalError()
  }

  func _remote_dont_3(actor: SomeDistributedActor_6) -> Int {
    fatalError()
  }
  func _remote_dont_4() -> Int {
    fatalError()
  }
}

@available(SwiftStdlib 5.6, *)
distributed actor BadValuesDistributedActor_7 {
  distributed var varItNope: Int { 13 } // expected-error{{'distributed' modifier cannot be applied to this declaration}}
  distributed let letItNope: Int = 13 // expected-error{{'distributed' modifier cannot be applied to this declaration}}
  distributed lazy var lazyVarNope: Int = 13 // expected-error{{'distributed' modifier cannot be applied to this declaration}}
  distributed subscript(nope: Int) -> Int { nope * 2 } // expected-error{{'distributed' modifier cannot be applied to this declaration}}
  distributed static let staticLetNope: Int = 13 // expected-error{{'distributed' modifier cannot be applied to this declaration}}
  distributed static var staticVarNope: Int { 13 } // expected-error{{'distributed' modifier cannot be applied to this declaration}}
  distributed static func staticNope() async throws -> Int { 13 } // expected-error{{'distributed' method cannot be 'static'}}
}

