// RUN: %target-typecheck-verify-swift -enable-experimental-distributed -disable-availability-checking
// REQUIRES: concurrency
// REQUIRES: distributed

import _Distributed

/// Use the existential wrapper as the default actor transport.
typealias DefaultActorTransport = AnyActorTransport

@available(SwiftStdlib 5.6, *)
distributed actor DA {

  let local: Int = 42
  // expected-note@-1{{distributed actor state is only available within the actor instance}}
  // expected-note@-2{{distributed actor state is only available within the actor instance}}

  nonisolated let nope: Int = 13
  // expected-error@-1{{'nonisolated' can not be applied to distributed actor stored properties}}

  nonisolated var computedNonisolated: Int {
    // nonisolated computed properties are outside of the actor and as such cannot access local
    _ = self.local // expected-error{{distributed actor-isolated property 'local' can only be referenced inside the distributed actor}}

    _ = self.id // ok, special handled and always available
    _ = self.actorTransport // ok, special handled and always available
  }

  distributed func dist() {}

  nonisolated func access() async throws {
    _ = self.id // ok
    _ = self.actorTransport // ok
    
    // self is a distributed actor self is NOT isolated
    _ = self.local // expected-error{{distributed actor-isolated property 'local' can only be referenced inside the distributed actor}}
    _ = try await self.dist() // ok, was made implicitly throwing and async
    _ = self.computedNonisolated // it's okay, only the body of computedNonisolated is wrong
  }

  nonisolated distributed func nonisolatedDistributed() async {
    // expected-error@-1{{cannot declare method 'nonisolatedDistributed()' as both 'nonisolated' and 'distributed'}}{{3-15=}}
    fatalError()
  }

  distributed nonisolated func distributedNonisolated() async {
    // expected-error@-1{{cannot declare method 'distributedNonisolated()' as both 'nonisolated' and 'distributed'}}{{15-27=}}
    fatalError()
  }

}
