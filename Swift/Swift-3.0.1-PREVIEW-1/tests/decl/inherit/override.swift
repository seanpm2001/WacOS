// RUN: %target-parse-verify-swift -parse-as-library

@objc class ObjCClassA {}
@objc class ObjCClassB : ObjCClassA {}

class A { 
  func f1() { } // expected-note{{overridden declaration is here}}
  func f2() -> A { } // expected-note{{overridden declaration is here}}

  @objc func f3() { }
  @objc func f4() -> ObjCClassA { }
}

extension A {
  func f5() { } // expected-note{{overridden declaration is here}}
  func f6() -> A { } // expected-note{{overridden declaration is here}}

  @objc func f7() { }
  @objc func f8() -> ObjCClassA { }
}

class B : A { }

extension B { 
  func f1() { }  // expected-error{{declarations in extensions cannot override yet}}
  func f2() -> B { } // expected-error{{declarations in extensions cannot override yet}}

  override func f3() { }
  override func f4() -> ObjCClassB { }

  func f5() { }  // expected-error{{declarations in extensions cannot override yet}}
  func f6() -> A { }  // expected-error{{declarations in extensions cannot override yet}}

  @objc override func f7() { }
  @objc override func f8() -> ObjCClassA { }
}

func callOverridden(_ b: B) {
  b.f3()
  _ = b.f4()
  b.f7()
  _ = b.f8()
}

@objc
class Base {
  func meth(_ x: Undeclared) {} // expected-error {{use of undeclared type 'Undeclared'}}
}
@objc
class Sub : Base {
  func meth(_ x: Undeclared) {} // expected-error {{use of undeclared type 'Undeclared'}}
}

// Objective-C method overriding

@objc class ObjCSuper {
  func method(_ x: Int, withInt y: Int) { }

  func method2(_ x: Sub, withInt y: Int) { }

  func method3(_ x: Base, withInt y: Int) { } // expected-note{{method 'method3(_:withInt:)' declared here}}
}

class ObjCSub : ObjCSuper {
  override func method(_ x: Int, withInt y: Int) { } // okay, overrides exactly

  override func method2(_ x: Base, withInt y: Int) { } // okay, overrides trivially

  func method3(_ x: Sub, withInt y: Int) { } // expected-error{{method3(_:withInt:)' with Objective-C selector 'method3:withInt:' conflicts with method 'method3(_:withInt:)' from superclass 'ObjCSuper' with the same Objective-C selector}}
}
