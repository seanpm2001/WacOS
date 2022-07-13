// RUN: %target-typecheck-verify-swift

// Inheritable: method with 'Self' in its signature
protocol P1 {
  func f1(_ x: Self?) -> Bool
}

// Never inheritable: property with 'Self' in its signature.
protocol P2 {
  var prop2: Self { get }
}

// Never inheritable: subscript with 'Self' in its result type.
protocol P3 {
  subscript (i: Int) -> Self { get }
}

// Inheritable: subscript with 'Self' in its index type.
protocol P4 {
  subscript (s: Self) -> Int { get }
}

// Potentially inheritable: method returning Self
protocol P5 {
  func f5() -> Self
}

// Inheritable: method returning Self
protocol P6 {
  func f6() -> Self
}

// Inheritable: method involving associated type.
protocol P7 {
  associatedtype Assoc
  func f7() -> Assoc
}

// Inheritable: initializer requirement.
protocol P8 {
  init(int: Int)
}

// Inheritable: operator requirement.
protocol P9 {
  static func ==(x: Self, y: Self) -> Bool 
}

// Never inheritable: method with 'Self' in a non-contravariant position.
protocol P10 {
  func f10(_ arr: [Self])
}

// Never inheritable: method with 'Self' in curried position.
protocol P11 {
  func f11() -> (_ x: Self) -> Int
}

// Inheritable: parameter is a function returning 'Self'.
protocol P12 {
  func f12(_ s: () -> (Self, Self))
}

// Never inheritable: parameter is a function accepting 'Self'.
protocol P13 {
  func f13(_ s: (Self) -> ())
}

// Inheritable: parameter is a function accepting a function
// accepting 'Self'.
protocol P14 {
  func f14(_ s: ((Self) -> ()) -> ())
}

// Never inheritable: parameter is a function accepting a function
// returning 'Self'.
protocol P15 {
  func f15(_ s: (() -> Self) -> ())
}

// Class A conforms to everything that can be conformed to by a
// non-final class.
class A : P1, P2, P3, P4, P5, P6, P7, P8, P9, P10 {
  // P1
  func f1(_ x: A?) -> Bool { return true }

  // P2
  var prop2: A { // expected-error{{protocol 'P2' requirement 'prop2' cannot be satisfied by a non-final class ('A') because it uses 'Self' in a non-parameter, non-result type position}}
    return self
  }

  // P3
  subscript (i: Int) -> A { // expected-error{{protocol 'P3' requirement 'subscript' cannot be satisfied by a non-final class ('A') because it uses 'Self' in a non-parameter, non-result type position}}
    get {
     return self
    }
  }

  // P4
  subscript (a: A) -> Int { 
    get {
      return 5
    }
  }

  // P5
  func f5() -> A { return self } // expected-error{{method 'f5()' in non-final class 'A' must return `Self` to conform to protocol 'P5'}}

  // P6
  func f6() -> Self { return self }

  // P7
  func f7() -> Int { return 5 }

  // P8
  required init(int: Int) { }

  // P10
  func f10(_ arr: [A]) { } // expected-error{{protocol 'P10' requirement 'f10' cannot be satisfied by a non-final class ('A') because it uses 'Self' in a non-parameter, non-result type position}}

  // P11
  func f11() -> (_ x: A) -> Int { return { x in 5 } }
}

// P9
func ==(x: A, y: A) -> Bool { return true }

// Class B inherits A; gets all of its conformances.
class B : A { 
  required init(int: Int) { }
}

func testB(_ b: B) {
  var _: P1 = b // expected-error{{has Self or associated type requirements}}
  var _: P4 = b // expected-error{{has Self or associated type requirements}}
  var _: P5 = b
  var _: P6 = b
  var _: P7 = b // expected-error{{has Self or associated type requirements}}
  var _: P8 = b // okay
  var _: P9 = b // expected-error{{has Self or associated type requirements}}
}

// Class A5 conforms to P5 in an inheritable manner.
class A5 : P5 {
  // P5 
  func f5() -> Self { return self }
}

// Class B5 inherits A5; gets all of its conformances.
class B5 : A5 { }

func testB5(_ b5: B5) {
  var _: P5 = b5 // okay
}

// Class A8 conforms to P8 in an inheritable manner.
class A8 : P8 {
  required init(int: Int) { }
}

class B8 : A8 {
  required init(int: Int) { }
}

func testB8(_ b8: B8) {
  var _: P8 = b8 // okay
}

// Class A9 conforms to everything.
final class A9 : P1, P2, P3, P4, P5, P6, P7, P8, P9, P10 {
  // P1
  func f1(_ x: A9?) -> Bool { return true }

  // P2
  var prop2: A9 {
    return self
  }

  // P3
  subscript (i: Int) -> A9 {
    get {
     return self
    }
  }

  // P4
  subscript (a: A9) -> Int { 
    get {
      return 5
    }
  }

  // P5
  func f5() -> A9 { return self }

  // P6
  func f6() -> Self { return self }

  // P7
  func f7() -> Int { return 5 }

  // P8
  required init(int: Int) { }

  // P10
  func f10(_ arr: [A9]) { }

  // P11
  func f11() -> (_ x: A9) -> Int { return { x in 5 } }
}

// P9
func ==(x: A9, y: A9) -> Bool { return true }

class A12 : P12 {
  func f12(_ s: () -> (A12, A12)) {}
}

class A13 : P13 {
  func f13(_ s: (A13) -> ()) {} // expected-error{{protocol 'P13' requirement 'f13' cannot be satisfied by a non-final class ('A13') because it uses 'Self' in a non-parameter, non-result type position}}
}

class A14 : P14 {
  func f14(_ s: ((A14) -> ()) -> ()) {}
}

class A15 : P15 {
  func f15(_ s: (() -> A15) -> ()) {} // expected-error{{protocol 'P15' requirement 'f15' cannot be satisfied by a non-final class ('A15') because it uses 'Self' in a non-parameter, non-result type position}}
}

