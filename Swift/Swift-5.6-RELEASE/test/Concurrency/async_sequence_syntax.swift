// RUN: %target-typecheck-verify-swift  -disable-availability-checking
// REQUIRES: concurrency

// expected-note@+2{{add 'async' to function 'missingAsync' to make it asynchronous}}
@available(SwiftStdlib 5.1, *)
func missingAsync<T : AsyncSequence>(_ seq: T) throws { 
  for try await _ in seq { } // expected-error{{'async' in a function that does not support concurrency}}
}

@available(SwiftStdlib 5.1, *)
func missingThrows<T : AsyncSequence>(_ seq: T) async {
  for try await _ in seq { } // expected-error{{error is not handled because the enclosing function is not declared 'throws'}}
}

@available(SwiftStdlib 5.1, *)
func executeAsync(_ work: () async -> Void) { }
@available(SwiftStdlib 5.1, *)
func execute(_ work: () -> Void) { }

@available(SwiftStdlib 5.1, *)
func missingThrowingInBlock<T : AsyncSequence>(_ seq: T) { 
  executeAsync { // expected-error{{invalid conversion from throwing function of type '() async throws -> Void' to non-throwing function type '() async -> Void'}}
    for try await _ in seq { }
  }
}

@available(SwiftStdlib 5.1, *)
func missingTryInBlock<T : AsyncSequence>(_ seq: T) { 
  executeAsync { 
    for await _ in seq { } // expected-error{{call can throw, but the error is not handled}}
  }
}

@available(SwiftStdlib 5.1, *)
func missingAsyncInBlock<T : AsyncSequence>(_ seq: T) { 
  execute { // expected-error{{cannot pass function of type '() async -> Void' to parameter expecting synchronous function type}}
    do { 
      for try await _ in seq { } // expected-note {{'async' inferred from asynchronous operation used here}}
    } catch { }
  }
}

@available(SwiftStdlib 5.1, *)
func doubleDiagCheckGeneric<T : AsyncSequence>(_ seq: T) async {
  var it = seq.makeAsyncIterator()
  // expected-note@+2{{call is to 'rethrows' function, but a conformance has a throwing witness}}
  // expected-error@+1{{call can throw, but it is not marked with 'try' and the error is not handled}}
  let _ = await it.next()
}

@available(SwiftStdlib 5.1, *)
struct ThrowingAsyncSequence: AsyncSequence, AsyncIteratorProtocol {
  typealias Element = Int
  typealias AsyncIterator = Self
  mutating func next() async throws -> Int? {
    return nil
  }

  func makeAsyncIterator() -> Self { return self }
}

@available(SwiftStdlib 5.1, *)
func doubleDiagCheckConcrete(_ seq: ThrowingAsyncSequence) async {
  var it = seq.makeAsyncIterator()
  // expected-error@+1{{call can throw, but it is not marked with 'try' and the error is not handled}}
  let _ = await it.next()
}

// rdar://75274975
@available(SwiftStdlib 5.1, *)
func forAwaitInsideDoCatch<Source: AsyncSequence>(_ source: Source) async {
  do {
    for try await item in source {
      print(item)
    }
  } catch {} // no-warning
}

@available(SwiftStdlib 5.1, *)
func forAwaitWithConcreteType(_ seq: ThrowingAsyncSequence) throws { // expected-note {{add 'async' to function 'forAwaitWithConcreteType' to make it asynchronous}}
  for try await elt in seq { // expected-error {{'async' in a function that does not support concurrency}}
    _ = elt
  }
}
