// RUN: %target-fail-simple-swift(-Xfrontend -disable-availability-checking -Xfrontend -enable-experimental-distributed -parse-as-library %import-libdispatch) 2>&1 | %FileCheck %s
//
// // TODO: could not figure out how to use 'not --crash' it never is used with target-run-simple-swift
// This test is intended to *crash*, so we're using target-fail-simple-swift
// which expects the exit code of the program to be non-zero;
// We then check stderr for the expected error message using filecheck as usual.

// UNSUPPORTED: back_deploy_concurrency
// REQUIRES: executable_test
// REQUIRES: concurrency
// REQUIRES: distributed

// FIXME(distributed): remote functions dont seem to work on windows?
// XFAIL: OS=windows-msvc

import _Distributed

@available(SwiftStdlib 5.6, *)
distributed actor SomeSpecificDistributedActor {
  distributed func hello() async throws -> String {
    "local impl"
  }
}

// ==== Fake Transport ---------------------------------------------------------

@available(SwiftStdlib 5.6, *)
struct ActorAddress: ActorIdentity {
  let address: String
  init(parse address : String) {
    self.address = address
  }
}

@available(SwiftStdlib 5.6, *)
struct FakeTransport: ActorTransport {
  func decodeIdentity(from decoder: Decoder) throws -> AnyActorIdentity {
    fatalError("not implemented:\(#function)")
  }

  func resolve<Act>(_ identity: AnyActorIdentity, as actorType: Act.Type) throws -> Act?
      where Act: DistributedActor {
    return nil
  }

  func assignIdentity<Act>(_ actorType: Act.Type) -> AnyActorIdentity
      where Act: DistributedActor {
    let id = ActorAddress(parse: "xxx")
    print("assign type:\(actorType), id:\(id)")
    return .init(id)
  }

  func actorReady<Act>(_ actor: Act) where Act: DistributedActor {
    print("ready actor:\(actor), id:\(actor.id)")
  }

  func resignIdentity(_ id: AnyActorIdentity) {
    print("ready id:\(id)")
  }
}

@available(SwiftStdlib 5.6, *)
typealias DefaultActorTransport = FakeTransport

// ==== Execute ----------------------------------------------------------------

@available(SwiftStdlib 5.6, *)
func test_remote() async {
  let address = ActorAddress(parse: "")
  let transport = FakeTransport()

  let remote = try! SomeSpecificDistributedActor.resolve(.init(address), using: transport)
  _ = try! await remote.hello() // let it crash!

  // CHECK: SOURCE_DIR/test/Distributed/Runtime/distributed_no_transport_boom.swift:{{[0-9]+}}: Fatal error: Invoked remote placeholder function '_remote_hello' on remote distributed actor of type 'main.SomeSpecificDistributedActor'. Configure an appropriate 'ActorTransport' for this actor to resolve this error (e.g. by depending on some specific transport library).
}

@available(SwiftStdlib 5.6, *)
@main struct Main {
  static func main() async {
    await test_remote()
  }
}

