// RUN: %target-typecheck-verify-swift

var func5 : (_ fn : (Int,Int) -> ()) -> () 

// Default arguments for functions.
func foo3(a: Int = 2, b: Int = 3) {}
func functionCall() {
  foo3(a: 4)
  foo3()
  foo3(a : 4)
  foo3(b : 4)
  foo3(a : 2, b : 4)
}

func g() {}
func h(_ x: () -> () = g) { x() }

// Tuple types cannot have default values, but recover well here.
func tupleTypes() {
  typealias ta1 = (a : Int = ()) // expected-error{{default argument not permitted in a tuple type}}{{28-32=}}
  // expected-error @-1{{cannot create a single-element tuple with an element label}}{{20-24=}}
  var c1 : (a : Int, b : Int, c : Int = 3, // expected-error{{default argument not permitted in a tuple type}}{{39-42=}}
            d = 4) = (1, 2, 3, 4) // expected-error{{default argument not permitted in a tuple type}}{{15-18=}} expected-error{{use of undeclared type 'd'}}
}

func returnWithDefault() -> (a: Int, b: Int = 42) { // expected-error{{default argument not permitted in a tuple type}} {{45-49=}}
  return 5 // expected-error{{cannot convert return expression of type 'Int' to return type '(a: Int, b: Int)'}}
}

func selectorStyle(_ i: Int = 1, withFloat f: Float = 2) { }

// Default arguments of constructors.
struct Ctor {
  init (i : Int = 17, f : Float = 1.5) { }
}

Ctor() // expected-warning{{unused}}
Ctor(i: 12) // expected-warning{{unused}}
Ctor(f:12.5) // expected-warning{{unused}}

// Default arguments for nested constructors/functions.
struct Outer<T> {
  struct Inner {
    struct VeryInner {
      init (i : Int = 17, f : Float = 1.5) { }
      static func f(i: Int = 17, f: Float = 1.5) { }
      func g(i: Int = 17, f: Float = 1.5) { }
    }
  }
}
_ = Outer<Int>.Inner.VeryInner()
_ = Outer<Int>.Inner.VeryInner(i: 12)
_ = Outer<Int>.Inner.VeryInner(f:12.5)
Outer<Int>.Inner.VeryInner.f()
Outer<Int>.Inner.VeryInner.f(i: 12)
Outer<Int>.Inner.VeryInner.f(f:12.5)

var vi : Outer<Int>.Inner.VeryInner
vi.g()
vi.g(i: 12)
vi.g(f:12.5)

// <rdar://problem/14564964> crash on invalid
func foo(_ x: WonkaWibble = 17) { } // expected-error{{use of undeclared type 'WonkaWibble'}}

// Default arguments for initializers.
class SomeClass2 { 
  init(x: Int = 5) {}
}
class SomeDerivedClass2 : SomeClass2 {
  init() {
    super.init()
  }
}

func shouldNotCrash(_ a : UndefinedType, bar b : Bool = true) { // expected-error {{use of undeclared type 'UndefinedType'}}
}

// <rdar://problem/20749423> Compiler crashed while building simple subclass
// code
class SomeClass3 {
  init(x: Int = 5, y: Int = 5) {}
}
class SomeDerivedClass3 : SomeClass3 {}
_ = SomeDerivedClass3()

// Tuple types with default arguments are not materializable
func identity<T>(_ t: T) -> T { return t }
func defaultArgTuplesNotMaterializable(_ x: Int, y: Int = 0) {}

defaultArgTuplesNotMaterializable(identity(5))

// <rdar://problem/22333090> QoI: Propagate contextual information in a call to operands
defaultArgTuplesNotMaterializable(identity((5, y: 10)))
// expected-error@-1 {{cannot convert value of type '(Int, y: Int)' to expected argument type 'Int'}}


// rdar://problem/21799331
func foo<T>(_ x: T, y: Bool = true) {}

foo(true ? "foo" : "bar")

func foo2<T>(_ x: T, y: Bool = true) {}

extension Array {
  func bar(_ x: (Element) -> Bool) -> Int? { return 0 }
}

foo2([].bar { $0 == "c" }!)

// rdar://problem/21643052
let a = ["1", "2"].map { Int($0) }

// Default arguments for static members used via ".foo"
struct X<T> {
  static func foo(i: Int, j: Int = 0) -> X {
    return X()
  }

  static var bar: X { return X() }
}

let testXa: X<Int> = .foo(i: 0)
let testXb: X<Int> = .bar
