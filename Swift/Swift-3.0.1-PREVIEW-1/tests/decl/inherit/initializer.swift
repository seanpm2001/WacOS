// RUN: %target-parse-verify-swift

class A {
  init(int i: Int) { }
  init(double d: Double) { }
  convenience init(float f: Float) {
    self.init(double: Double(f))
  }
}

// Implicit overriding of subobject initializers
class B : A {
  var x = "Hello"
  var (y, z) = (1, 2)
}

func testB() {
  _ = B(int: 5)
  _ = B(double: 2.71828)
  _ = B(float: 3.14159)
}

// Okay to have nothing
class C : B {
}

func testC() {
  _ = C(int: 5)
  _ = C(double: 2.71828)
  _ = C(float: 3.14159)
}

// Okay to add convenience initializers.
class D : C {
  convenience init(string s: String) {
    self.init(int: Int(s)!)
  }
}

func testD() {
  _ = D(int: 5)
  _ = D(double: 2.71828)
  _ = D(float: 3.14159)
  _ = D(string: "3.14159")
}

// Adding a subobject initializer prevents inheritance of subobject
// initializers.
class NotInherited1 : D {
  override init(int i: Int) {
    super.init(int: i)
  }

  convenience init(float f: Float) {
    self.init(int: Int(f))
  }
}

func testNotInherited1() {
  var n1 = NotInherited1(int: 5)
  var n2 = NotInherited1(double: 2.71828) // expected-error{{argument labels '(double:)' do not match any available overloads}}
  // expected-note @-1 {{overloads for 'NotInherited1' exist with these partially matching parameter lists: (int: Int), (float: Float)}}
}

class NotInherited1Sub : NotInherited1 {
  override init(int i: Int) {
    super.init(int: i)
  }
}

func testNotInherited1Sub() {
  var n1 = NotInherited1Sub(int: 5)
  var n2 = NotInherited1Sub(float: 3.14159)
  var n3 = NotInherited1Sub(double: 2.71828) // expected-error{{argument labels '(double:)' do not match any available overloads}}
  // expected-note @-1 {{overloads for 'NotInherited1Sub' exist with these partially matching parameter lists: (int: Int), (float: Float)}}
}

// Having a stored property without an initial value prevents
// inheritance of initializers.
class NotInherited2 : D { // expected-error{{class 'NotInherited2' has no initializers}}
  var a: Int // expected-note{{stored property 'a' without initial value prevents synthesized initializers}}{{13-13= = 0}}
  var b: Int // expected-note{{stored property 'b' without initial value prevents synthesized initializers}}{{13-13= = 0}}
    , c: Int  // expected-note{{stored property 'c' without initial value prevents synthesized initializers}}{{13-13= = 0}}
  
  var (d, e): (Int, String) // expected-note{{stored properties 'd' and 'e' without initial values prevent synthesized initializers}}{{28-28= = (0, "")}}
  var (f, (g, h)): (Int?, (Float, String)) // expected-note{{stored properties 'f', 'g', and 'h' without initial values prevent synthesized initializers}}{{43-43= = (nil, (0.0, ""))}}
  var (w, i, (j, k)): (Int?, Float, (String, D)) // expected-note{{stored properties 'w', 'i', 'j', and others without initial values prevent synthesized initializers}}
}

func testNotInherited2() {
  var n1 = NotInherited2(int: 5) // expected-error{{'NotInherited2' cannot be constructed because it has no accessible initializers}}
  var n2 = NotInherited2(double: 2.72828) // expected-error{{'NotInherited2' cannot be constructed because it has no accessible initializers}}
}

// Inheritance of unnamed parameters.
class SuperUnnamed {
  init(int _: Int) { }
  init(_ : Double) { }

  init(string _: String) { }
  init(_ : Float) { }
}

class SubUnnamed : SuperUnnamed { }

func testSubUnnamed(_ i: Int, d: Double, s: String, f: Float) {
  _ = SubUnnamed(int: i)
  _ = SubUnnamed(d)
  _ = SubUnnamed(string: s)
  _ = SubUnnamed(f)
}

// rdar://problem/17960407 - Inheritance of generic initializers
class ConcreteBase {
  required init(i: Int) {}
}

class GenericDerived<T> : ConcreteBase {}

class GenericBase<T> {
  required init(t: T) {}
}

class GenericDerived2<U> : GenericBase<(U, U)> {}

class ConcreteDerived : GenericBase<Int> {}

func testGenericInheritance() {
  _ = GenericDerived<Int>(i: 10)
  _ = GenericDerived2<Int>(t: (10, 100))
  _ = ConcreteDerived(t: 1000)
}

// FIXME: <rdar://problem/16331406> Implement inheritance of variadic designated initializers
class SuperVariadic {
  init(ints: Int...) { } // expected-note{{variadic superclass initializer defined here}}
  init(_ : Double...) { } // expected-note{{variadic superclass initializer defined here}}

  init(s: String, ints: Int...) { } // expected-note{{variadic superclass initializer defined here}}
  init(s: String, _ : Double...) { } // expected-note{{variadic superclass initializer defined here}}
}

class SubVariadic : SuperVariadic { } // expected-warning 4{{synthesizing a variadic inherited initializer for subclass 'SubVariadic' is unsupported}}

// Don't crash with invalid nesting of class in generic function

func testClassInGenericFunc<T>(t: T) {
  class A { init(t: T) {} } // expected-error {{type 'A' cannot be nested in generic function 'testClassInGenericFunc'}}
  class B : A {} // expected-error {{type 'B' cannot be nested in generic function 'testClassInGenericFunc'}}

  _ = B(t: t)
}
