// RUN: %target-typecheck-verify-swift -enable-experimental-distributed -disable-availability-checking
// REQUIRES: concurrency
// REQUIRES: distributed

import _Distributed

/// Use the existential wrapper as the default actor transport.
typealias DefaultActorTransport = AnyActorTransport

@available(SwiftStdlib 5.6, *)
extension Actor {
    func f() -> String { "Life is Study!" }
}

@available(SwiftStdlib 5.6, *)
func g<A: Actor>(a: A) async { // expected-note{{where 'A' = 'MA'}}
    print(await a.f())
}

@available(SwiftStdlib 5.6, *)
distributed actor MA {
}

@available(SwiftStdlib 5.6, *)
func h(ma: MA) async {
    // this would have been a bug, a non distributed instance method might have been called here,
    // so we must not allow for it, because if the actor was remote calling a non-distributed func
    // would result in a hard crash (as there is no local actor to safely call the function on).
    await g(a: ma) // expected-error{{global function 'g(a:)' requires that 'MA' conform to 'Actor'}}
}