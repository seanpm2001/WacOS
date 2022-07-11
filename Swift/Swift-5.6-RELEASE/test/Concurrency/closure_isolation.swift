// RUN: %target-swift-frontend -dump-ast %s  -disable-availability-checking | %FileCheck %s
// REQUIRES: concurrency

func acceptClosure<T>(_: () -> T) { }
func acceptSendableClosure<T>(_: @Sendable () -> T) { }

func acceptAsyncClosure<T>(_: () async -> T) { }
func acceptEscapingAsyncClosure<T>(_: @escaping () async -> T) { }

actor MyActor {
  func method() async -> String { "" }
  func syncMethod() -> String { "" }
}

extension MyActor {
  // CHECK-LABEL: testClosureIsolation
  func testClosureIsolation() async {
    // CHECK: acceptClosure
    // CHECK: closure_expr
    // CHECK: actor-isolated
    acceptClosure { self.syncMethod() }

    // CHECK: acceptSendableClosure
    // CHECK: closure_expr
    // CHECK-NOT: actor-isolated
    acceptSendableClosure { print(self) }

    // CHECK: acceptAsyncClosure
    // CHECK: closure_expr
    // CHECK-SAME: actor-isolated=closure_isolation.(file).MyActor extension.testClosureIsolation().self
    acceptAsyncClosure { await method() }

    // CHECK: acceptAsyncClosure
    // CHECK: closure_expr
    // CHECK-NOT: actor-isolated
    acceptAsyncClosure { () async in print() }

    // CHECK: acceptEscapingAsyncClosure
    // CHECK: closure_expr
    // CHECK: actor-isolated
    acceptEscapingAsyncClosure { self.syncMethod() }

    // CHECK: acceptEscapingAsyncClosure
    // CHECK: closure_expr
    // CHECK: actor-isolated
    acceptEscapingAsyncClosure { () async in print(self) }
  }
}

actor SomeActor { }

@globalActor
struct SomeGlobalActor {
  static let shared = SomeActor()
}

func someAsyncFunc() async { }

@SomeGlobalActor func getGlobal7() -> Int { 7 }

// CHECK-LABEL: someGlobalActorFunc
@SomeGlobalActor func someGlobalActorFunc() async {
  // CHECK: acceptAsyncClosure
  // CHECK: closure_expr
  // CHECK-SAME: global-actor-isolated=SomeGlobalActor
  acceptAsyncClosure { await someAsyncFunc() }

  // CHECK: acceptAsyncClosure
  // CHECK: closure_expr
  // CHECK-SAME: global-actor-isolated=SomeGlobalActor
  acceptAsyncClosure { () async in print("hello") }

  // CHECK: acceptEscapingAsyncClosure
  // CHECK: closure_expr
  // CHECK: actor-isolated
  acceptEscapingAsyncClosure { await someAsyncFunc() }

  // CHECK: acceptEscapingAsyncClosure
  // CHECK: closure_expr
  // CHECK: actor-isolated
  acceptEscapingAsyncClosure { () async in print("hello") }
}
