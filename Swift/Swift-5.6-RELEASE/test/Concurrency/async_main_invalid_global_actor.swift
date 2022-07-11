// RUN: %target-typecheck-verify-swift -disable-availability-checking -parse-as-library
// REQUIRES: concurrency

@globalActor
actor Kat {
  static let shared = Kat()
}

@Kat
var poof: Int = 1337 // expected-note{{var declared here}}

@main struct Doggo {
  @Kat
  static func main() { // expected-error{{main() must be '@MainActor'}}
    // expected-error@+1{{var 'poof' isolated to global actor 'Kat' can not be referenced from different global actor 'MainActor' in a synchronous context}}
    print("Kat value: \(poof)")
  }
}

struct Bunny {
  // Bunnies are not @main, so they can have a "main" function that is on
  // another actor. It's not actually the main function, so it's fine.
  @Kat
  static func main() {
  }
}
