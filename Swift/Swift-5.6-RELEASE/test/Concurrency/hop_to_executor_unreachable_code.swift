// RUN: %target-swift-frontend -disable-availability-checking -emit-sil -verify %s

// REQUIRES: concurrency

// Check that the inserted hop-to-executor instructions don't cause a false
// "unreachable code" warning.

@MainActor
func bye() -> Never {
    print("bye")
    fatalError()
}

func testit() async {
    await bye()
}

