// RUN: %target-typecheck-verify-swift -enable-experimental-distributed -disable-availability-checking
// REQUIRES: concurrency
// REQUIRES: distributed

import _Distributed

/// Use the existential wrapper as the default actor transport.
typealias DefaultActorTransport = AnyActorTransport

@available(SwiftStdlib 5.6, *)
distributed actor D {

  func hello() {} // expected-note{{distributed actor-isolated instance method 'hello()' declared here}}
  func helloAsync() async {} // expected-note{{distributed actor-isolated instance method 'helloAsync()' declared here}}
  func helloAsyncThrows() async throws {} // expected-note{{distributed actor-isolated instance method 'helloAsyncThrows()' declared here}}

  distributed func distHello() { } // ok
  distributed func distHelloAsync() async { } // ok
  distributed func distHelloThrows() throws { } // ok
  distributed func distHelloAsyncThrows() async throws { } // ok
}

@available(SwiftStdlib 5.6, *)
func test_not_distributed_funcs(distributed: D) async {
  distributed.hello() // expected-error{{only 'distributed' instance methods can be called on a potentially remote distributed actor}}
  distributed.helloAsync() // expected-error{{only 'distributed' instance methods can be called on a potentially remote distributed actor}}
  // expected-error@-1{{expression is 'async' but is not marked with 'await'}}
  // expected-note@-2{{call is 'async'}}
  // {{expression is 'async' but is not marked with 'await'}}{{7-7=await }}
  distributed.helloAsyncThrows() // expected-error{{only 'distributed' instance methods can be called on a potentially remote distributed actor}}
  // expected-error@-1{{expression is 'async' but is not marked with 'await'}} // TODO: no need to diagnose this, it is impossible to call anyway
  // expected-note@-2{{call is 'async'}}
  // expected-error@-3{{call can throw, but it is not marked with 'try' and the error is not handled}} // TODO: no need to diagnose this, it is impossible to call anyway
}

@available(SwiftStdlib 5.6, *)
func test_outside(distributed: D) async throws {
  distributed.distHello() // expected-error{{expression is 'async' but is not marked with 'await'}}
  // expected-error@-1{{call can throw but is not marked with 'try'}}
  // expected-note@-2{{calls to distributed instance method 'distHello()' from outside of its actor context are implicitly asynchronous}}
  // expected-note@-3{{did you mean to use 'try'?}}
  // expected-note@-4{{did you mean to disable error propagation?}}
  // expected-note@-5{{did you mean to handle error as optional value?}}
  try distributed.distHello() // expected-error{{expression is 'async' but is not marked with 'await'}}
  // expected-note@-1{{calls to distributed instance method 'distHello()' from outside of its actor context are implicitly asynchronous}}
  await distributed.distHello() // expected-error{{call can throw but is not marked with 'try'}}
  // expected-note@-1{{did you mean to use 'try'?}}
  // expected-note@-2{{did you mean to disable error propagation?}}
  // expected-note@-3{{did you mean to handle error as optional value?}}
  try await distributed.distHello() // ok

  distributed.distHelloAsync()// expected-error{{expression is 'async' but is not marked with 'await'}}
  // expected-error@-1{{call can throw but is not marked with 'try'}}
  // expected-note@-2{{call is 'async'}}
  // expected-note@-3{{did you mean to use 'try'?}}
  // expected-note@-4{{did you mean to disable error propagation?}}
  // expected-note@-5{{did you mean to handle error as optional value?}}
  try distributed.distHelloAsync() // expected-error{{expression is 'async' but is not marked with 'await'}}
  // expected-note@-1{{call is 'async'}}
  await distributed.distHelloAsync() // expected-error{{call can throw but is not marked with 'try'}}
  // expected-note@-1{{did you mean to use 'try'?}}
  // expected-note@-2{{did you mean to disable error propagation?}}
  // expected-note@-3{{did you mean to handle error as optional value?}}
  try await distributed.distHelloAsync() // ok

  distributed.distHelloThrows() // expected-error{{expression is 'async' but is not marked with 'await'}}
  // expected-error@-1{{call can throw but is not marked with 'try'}}
  // expected-note@-2{{calls to distributed instance method 'distHelloThrows()' from outside of its actor context are implicitly asynchronous}}
  // expected-note@-3{{did you mean to use 'try'?}}
  // expected-note@-4{{did you mean to disable error propagation?}}
  // expected-note@-5{{did you mean to handle error as optional value?}}
  try distributed.distHelloThrows() // expected-error{{expression is 'async' but is not marked with 'await'}}
  // expected-note@-1{{calls to distributed instance method 'distHelloThrows()' from outside of its actor context are implicitly asynchronous}}
  await distributed.distHelloThrows() // expected-error{{call can throw but is not marked with 'try'}}
  // expected-note@-1{{did you mean to use 'try'?}}
  // expected-note@-2{{did you mean to disable error propagation?}}
  // expected-note@-3{{did you mean to handle error as optional value?}}
  try await distributed.distHelloThrows() // ok

  distributed.distHelloAsyncThrows() // expected-error{{expression is 'async' but is not marked with 'await'}}
  // expected-error@-1{{call can throw but is not marked with 'try'}}
  // expected-note@-2{{call is 'async'}}
  // expected-note@-3{{did you mean to use 'try'?}}
  // expected-note@-4{{did you mean to disable error propagation?}}
  // expected-note@-5{{did you mean to handle error as optional value?}}
  try distributed.distHelloAsyncThrows() // expected-error{{expression is 'async' but is not marked with 'await'}}
  // expected-note@-1{{call is 'async'}}
  await distributed.distHelloAsyncThrows() // expected-error{{call can throw but is not marked with 'try'}}
  // expected-note@-1{{did you mean to use 'try'?}}
  // expected-note@-2{{did you mean to disable error propagation?}}
  // expected-note@-3{{did you mean to handle error as optional value?}}
  try await distributed.distHelloAsyncThrows() // ok

  // special: the actorAddress may always be referred to
  _ = distributed.id // ok
  _ = distributed.actorTransport // ok
}

