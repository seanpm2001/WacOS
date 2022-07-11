// RUN: %target-run-simple-swift( -Xfrontend -disable-availability-checking %import-libdispatch -parse-as-library) | %FileCheck %s

// REQUIRES: executable_test
// REQUIRES: concurrency
// REQUIRES: libdispatch

// rdar://76038845
// REQUIRES: concurrency_runtime
// UNSUPPORTED: back_deployment_runtime

struct Boom: Error {}

func boom() throws -> Int {
  throw Boom()
}

@available(SwiftStdlib 5.1, *)
func test() async {
  async let result = boom()

  do {
    _ = try await result
  } catch {
    print("error: \(error)") // CHECK: error: Boom()
  }
}

@available(SwiftStdlib 5.1, *)
@main struct Main {
  static func main() async {
    await test()
  }
}
