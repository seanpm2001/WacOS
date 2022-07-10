// RUN: %target-parse-verify-swift

func statement_starts() {
  var f : (Int) -> ()
  f = { (x : Int) -> () in }

  f(0)
  f (0)
  f // expected-error{{expression resolves to an unused l-value}}
  (0) // expected-warning {{result of call to 'init(_builtinIntegerLiteral:)' is unused}}

  var a = [1,2,3]
  a[0] = 1
  a [0] = 1
  a // expected-error{{expression resolves to an unused l-value}}
  [0, 1, 2] // expected-warning {{expression of type '[Int]' is unused}}
}

// Within a function
func test(i: inout Int, j: inout Int) {
  // Okay
  let q : Int; i = j; j = i; _ = q

  if i != j { i = j }

  // Errors
  i = j j = i // expected-error{{consecutive statements}} {{8-8=;}}
  let r : Int i = j // expected-error{{consecutive statements}} {{14-14=;}}
  let s : Int let t : Int // expected-error{{consecutive statements}} {{14-14=;}}
  _ = r; _ = s; _ = t
}

struct X {
  // In a sequence of declarations.
  var a, b : Int func d() -> Int {} // expected-error{{consecutive declarations}} {{17-17=;}}

  var prop : Int { return 4
  } var other : Float // expected-error{{consecutive declarations}} {{4-4=;}}

  // Within property accessors
  subscript(i: Int) -> Float {
    get {
      var x = i x = i + x return Float(x) // expected-error{{consecutive statements}} {{16-16=;}} expected-error{{consecutive statements}} {{26-26=;}}
    }
    set {
      var x = i x = i + 1 // expected-error{{consecutive statements}} {{16-16=;}}
      _ = x
    }
  }
}

class C {
  // In a sequence of declarations.
  var a, b : Int func d() -> Int {} // expected-error{{consecutive declarations}} {{17-17=;}}
  init() {
    a = 0
    b = 0
  }
}

protocol P {
  func a() func b() // expected-error{{consecutive declarations}} {{11-11=;}}
}

enum Color {
  case Red case Blue // expected-error{{consecutive declarations}} {{11-11=;}}
  func a() {} func b() {} // expected-error{{consecutive declarations}} {{14-14=;}}
}

// At the top level
var i, j : Int i = j j = i // expected-error{{consecutive statements}} {{15-15=;}} expected-error{{consecutive statements}} {{21-21=;}}

