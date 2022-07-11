// RUN: %target-run-simple-swift( -Xfrontend -disable-availability-checking -parse-as-library) | %FileCheck %s

// REQUIRES: executable_test
// REQUIRES: concurrency

// rdar://76038845
// REQUIRES: concurrency_runtime
// UNSUPPORTED: back_deployment_runtime

struct Boom: Error {}
struct IgnoredBoom: Error {}

@available(SwiftStdlib 5.1, *)
func echo(_ i: Int) async -> Int { i }
@available(SwiftStdlib 5.1, *)
func boom() async throws -> Int { throw Boom() }

@available(SwiftStdlib 5.1, *)
func test_taskGroup_throws_rethrows() async {
  do {
    let got = try await withThrowingTaskGroup(of: Int.self, returning: Int.self) { group in
      group.addTask { await echo(1) }
      group.addTask { await echo(2) }
      group.addTask { try await boom() }

      do {
        while let r = try await group.next() {
          print("next: \(r)")
        }
      } catch {
        // CHECK: error caught and rethrown in group: Boom()
        print("error caught and rethrown in group: \(error)")
        throw error
      }

      print("should have thrown")
      return 0
    }

    print("Expected error to be thrown, but got: \(got)")
  } catch {
    // CHECK: rethrown: Boom()
    print("rethrown: \(error)")
  }
}


@available(SwiftStdlib 5.1, *)
@main struct Main {
  static func main() async {
    await test_taskGroup_throws_rethrows()
  }
}
