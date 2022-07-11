// RUN: %target-typecheck-verify-swift -enable-experimental-distributed -disable-availability-checking
// REQUIRES: concurrency
// REQUIRES: distributed

import _Distributed

// ==== -----------------------------------------------------------------------

actor A: Actor {} // ok

class C: Actor, UnsafeSendable {
  // expected-error@-1{{non-actor type 'C' cannot conform to the 'Actor' protocol}} {{1-6=actor}}
  // expected-warning@-2{{'UnsafeSendable' is deprecated: Use @unchecked Sendable instead}}
  nonisolated var unownedExecutor: UnownedSerialExecutor {
    fatalError()
  }
}

struct S: Actor {
  // expected-error@-1{{non-class type 'S' cannot conform to class protocol 'Actor'}}
  nonisolated var unownedExecutor: UnownedSerialExecutor {
    fatalError()
  }
}

struct E: Actor {
  // expected-error@-1{{non-class type 'E' cannot conform to class protocol 'Actor'}}
  nonisolated var unownedExecutor: UnownedSerialExecutor {
    fatalError()
  }
}

// ==== -----------------------------------------------------------------------

distributed actor DA: DistributedActor {
  typealias Transport = AnyActorTransport
}

actor A2: DistributedActor {
  // expected-error@-1{{non-distributed actor type 'A2' cannot conform to the 'DistributedActor' protocol}} {{1-1=distributed }}
  nonisolated var id: AnyActorIdentity {
    fatalError()
  }
  nonisolated var actorTransport: AnyActorTransport {
    fatalError()
  }

  init(transport: AnyActorTransport) {
    fatalError()
  }

  static func resolve(_ identity: AnyActorIdentity, using transport: AnyActorTransport) throws -> Self {
    fatalError()
  }
}

final class C2: DistributedActor {
  // expected-error@-1{{non-actor type 'C2' cannot conform to the 'Actor' protocol}}
  nonisolated var id: AnyActorIdentity {
    fatalError()
  }
  nonisolated var actorTransport: AnyActorTransport {
    fatalError()
  }

  required init(transport: AnyActorTransport) {
    fatalError()
  }
  static func resolve(_ identity: AnyActorIdentity, using transport: AnyActorTransport) throws -> Self {
    fatalError()
  }
}

struct S2: DistributedActor {
  // expected-error@-1{{non-class type 'S2' cannot conform to class protocol 'DistributedActor'}}
  // expected-error@-2{{non-class type 'S2' cannot conform to class protocol 'AnyActor'}}
  // expected-error@-3{{type 'S2' does not conform to protocol 'Identifiable'}}
}

