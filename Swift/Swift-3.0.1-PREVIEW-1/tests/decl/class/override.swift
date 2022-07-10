// RUN: %target-parse-verify-swift -parse-as-library

class A {
  func ret_sametype() -> Int { return 0 }
  func ret_subclass() -> A { return self }
  func ret_subclass_rev() -> B { return B() }
  func ret_nonclass_optional() -> Int? { return .none }
  func ret_nonclass_optional_rev() -> Int { return 0 }
  func ret_class_optional() -> B? { return .none }
  func ret_class_optional_rev() -> A { return self }
  func ret_class_uoptional() -> B! { return B() }
  func ret_class_uoptional_rev() -> A { return self }
  func ret_class_optional_uoptional() -> B? { return .none }
  func ret_class_optional_uoptional_rev() -> A! { return self }

  func param_sametype(_ x : Int) {}
  func param_subclass(_ x : B) {}
  func param_subclass_rev(_ x : A) {}
  func param_nonclass_optional(_ x : Int) {}
  func param_nonclass_optional_rev(_ x : Int?) {}
  func param_class_optional(_ x : B) {}
  func param_class_optional_rev(_ x : B?) {}
  func param_class_uoptional(_ x : B) {}
  func param_class_uoptional_rev(_ x : B!) {}
  func param_class_optional_uoptional(_ x : B!) {}
  func param_class_optional_uoptional_rev(_ x : B?) {}
}

class B : A {
  override func ret_sametype() -> Int { return 1 }
  override func ret_subclass() -> B { return self }
  func ret_subclass_rev() -> A { return self }
  override func ret_nonclass_optional() -> Int { return 0 }
  func ret_nonclass_optional_rev() -> Int? { return 0 }
  override func ret_class_optional() -> B { return self }
  func ret_class_optional_rev() -> A? { return self }
  override func ret_class_uoptional() -> B { return self }
  func ret_class_uoptional_rev() -> A! { return self }
  override func ret_class_optional_uoptional() -> B! { return self }
  override func ret_class_optional_uoptional_rev() -> A? { return self }

  override func param_sametype(_ x : Int) {}
  override func param_subclass(_ x : A) {}
  func param_subclass_rev(_ x : B) {}
  override func param_nonclass_optional(_ x : Int?) {}
  func param_nonclass_optional_rev(_ x : Int) {}
  override func param_class_optional(_ x : B?) {}
  func param_class_optional_rev(_ x : B) {}
  override func param_class_uoptional(_ x : B!) {}
  func param_class_uoptional_rev(_ x : B) {}
  override func param_class_optional_uoptional(_ x : B?) {}
  override func param_class_optional_uoptional_rev(_ x : B!) {}
}

class C<T> {
  func ret_T() -> T {} 
}

class D<T> : C<[T]> {
  override func ret_T() -> [T] {} 
}

class E {
  var var_sametype: Int { get { return 0 } set {} }
  var var_subclass: E { get { return self } set {} } // expected-note{{attempt to override property here}}
  var var_subclass_rev: F { get { return F() } set {} } // expected-note{{attempt to override property here}}
  var var_nonclass_optional: Int? { get { return .none } set {} } // expected-note{{attempt to override property here}}
  var var_nonclass_optional_rev: Int { get { return 0 } set {} } // expected-note{{attempt to override property here}}
  var var_class_optional: F? { get { return .none } set {} } // expected-note{{attempt to override property here}}
  var var_class_optional_rev: E { get { return self } set {} } // expected-note{{attempt to override property here}}
  var var_class_uoptional: F! { get { return F() } set {} } // expected-note{{attempt to override property here}}
  var var_class_uoptional_rev: E { get { return self } set {} } // expected-note{{attempt to override property here}}
  var var_class_optional_uoptional: F? { get { return .none } set {} }
  var var_class_optional_uoptional_rev: E! { get { return self } set {} }

  var ro_sametype: Int { return 0 }
  var ro_subclass: E { return self }
  var ro_subclass_rev: F { return F() }
  var ro_nonclass_optional: Int? { return 0 }
  var ro_nonclass_optional_rev: Int { return 0 } // expected-note{{attempt to override property here}}
  var ro_class_optional: F? { return .none }
  var ro_class_optional_rev: E { return self } // expected-note{{attempt to override property here}}
  var ro_class_uoptional: F! { return F() }
  var ro_class_uoptional_rev: E { return self } // expected-note{{attempt to override property here}}
  var ro_class_optional_uoptional: F? { return .none }
  var ro_class_optional_uoptional_rev: E! { return self }
}

class F : E {
  override var var_sametype: Int { get { return 0 } set {} }
  override var var_subclass: F { get { return self } set {} } // expected-error{{cannot override mutable property 'var_subclass' of type 'E' with covariant type 'F'}}
  override var var_subclass_rev: E { get { return F() } set {} } // expected-error{{property 'var_subclass_rev' with type 'E' cannot override a property with type 'F}}
  override var var_nonclass_optional: Int { get { return 0 } set {} } // expected-error{{cannot override mutable property 'var_nonclass_optional' of type 'Int?' with covariant type 'Int'}}
  override var var_nonclass_optional_rev: Int? { get { return 0 } set {} } // expected-error{{property 'var_nonclass_optional_rev' with type 'Int?' cannot override a property with type 'Int'}}
  override var var_class_optional: F { get { return self } set {} } // expected-error{{cannot override mutable property 'var_class_optional' of type 'F?' with covariant type 'F'}}
  override var var_class_optional_rev: E? { get { return self } set {} } // expected-error{{property 'var_class_optional_rev' with type 'E?' cannot override a property with type 'E'}}
  override var var_class_uoptional: F { get { return F() } set {} } // expected-error{{cannot override mutable property 'var_class_uoptional' of type 'F!' with covariant type 'F'}}
  override var var_class_uoptional_rev: E! { get { return self }  set {} } // expected-error{{property 'var_class_uoptional_rev' with type 'E!' cannot override a property with type 'E'}}
  override var var_class_optional_uoptional: F! { get { return .none } set {} }
  override var var_class_optional_uoptional_rev: E? { get { return self } set {} }

  override var ro_sametype: Int { return 0 }
  override var ro_subclass: E { return self }
  override var ro_subclass_rev: F { return F() }
  override var ro_nonclass_optional: Int { return 0 }
  override var ro_nonclass_optional_rev: Int? { return 0 } // expected-error{{property 'ro_nonclass_optional_rev' with type 'Int?' cannot override a property with type 'Int'}}
  override var ro_class_optional: F { return self }
  override var ro_class_optional_rev: E? { return self } // expected-error{{property 'ro_class_optional_rev' with type 'E?' cannot override a property with type 'E'}}
  override var ro_class_uoptional: F { return F() }
  override var ro_class_uoptional_rev: E! { return self } // expected-error{{property 'ro_class_uoptional_rev' with type 'E!' cannot override a property with type 'E'}}
  override var ro_class_optional_uoptional: F! { return .none }
  override var ro_class_optional_uoptional_rev: E? { return self }
}


class G {
  func f1(_: Int, int: Int) { }
  func f2(_: Int, int: Int) { }
  func f3(_: Int, int: Int) { }
  func f4(_: Int, int: Int) { }
  func f5(_: Int, int: Int) { }
  func f6(_: Int, int: Int) { }
  func f7(_: Int, int: Int) { }

  func g1(_: Int, string: String) { } // expected-note{{potential overridden instance method 'g1(_:string:)' here}} {{28-28=string }}
  func g1(_: Int, path: String) { } // expected-note{{potential overridden instance method 'g1(_:path:)' here}} {{28-28=path }}

  func g2(_: Int, string: String) { } // expected-note{{potential overridden instance method 'g2(_:string:)' here}} {{none}}
  func g2(_: Int, path: String) { }

  func g3(_: Int, _ another: Int) { }
  func g3(_: Int, path: String) { } // expected-note{{potential overridden instance method 'g3(_:path:)' here}} {{none}}

  func g4(_: Int, _ another: Int) { }
  func g4(_: Int, path: String) { }

  init(a: Int) {} // expected-note {{potential overridden initializer 'init(a:)' here}} {{none}}
  init(a: String) {} // expected-note {{potential overridden initializer 'init(a:)' here}} {{17-17=a }} expected-note {{potential overridden initializer 'init(a:)' here}} {{none}}
  init(b: String) {} // expected-note {{potential overridden initializer 'init(b:)' here}} {{17-17=b }} expected-note {{potential overridden initializer 'init(b:)' here}} {{none}}
}

class H : G {
  override func f1(_: Int, _: Int) { } // expected-error{{argument names for method 'f1' do not match those of overridden method 'f1(_:int:)'}}{{28-28=int }}
  override func f2(_: Int, value: Int) { } // expected-error{{argument names for method 'f2(_:value:)' do not match those of overridden method 'f2(_:int:)'}}{{28-28=int }}
  override func f3(_: Int, value int: Int) { } // expected-error{{argument names for method 'f3(_:value:)' do not match those of overridden method 'f3(_:int:)'}}{{28-34=}}
  override func f4(_: Int, _ int: Int) { } // expected-error{{argument names for method 'f4' do not match those of overridden method 'f4(_:int:)'}}{{28-30=}}
  override func f5(_: Int, value inValue: Int) { } // expected-error{{argument names for method 'f5(_:value:)' do not match those of overridden method 'f5(_:int:)'}}{{28-33=int}}
  override func f6(_: Int, _ inValue: Int) { } // expected-error{{argument names for method 'f6' do not match those of overridden method 'f6(_:int:)'}}{{28-29=int}}

  override func f7(_: Int, int value: Int) { } // okay

  override func g1(_: Int, s: String) { } // expected-error{{declaration 'g1(_:s:)' has different argument names from any potential overrides}}{{none}}
  override func g2(_: Int, string: Int) { } // expected-error{{method does not override any method from its superclass}} {{none}}
  override func g3(_: Int, path: Int) { } // expected-error{{method does not override any method from its superclass}} {{none}}
  override func g4(_: Int, string: Int) { } // expected-error{{argument names for method 'g4(_:string:)' do not match those of overridden method 'g4'}} {{28-28=_ }}

  override init(x: Int) {} // expected-error{{argument names for initializer 'init(x:)' do not match those of overridden initializer 'init(a:)'}} {{17-17=a }}
  override init(x: String) {} // expected-error{{declaration 'init(x:)' has different argument names from any potential overrides}} {{none}}
  override init(a: Double) {} // expected-error{{initializer does not override a designated initializer from its superclass}} {{none}}
  override init(b: Double) {} // expected-error{{initializer does not override a designated initializer from its superclass}} {{none}}
}

@objc class IUOTestBaseClass {
  func none() {}

  func oneA(_: AnyObject) {}
  func oneB(x: AnyObject) {}
  func oneC(_ x: AnyObject) {}

  func manyA(_: AnyObject, _: AnyObject) {}
  func manyB(_ a: AnyObject, b: AnyObject) {}
  func manyC(var a: AnyObject,  // expected-error {{parameters may not have the 'var' specifier}} {{14-17=}}
             var b: AnyObject) {} // expected-error {{parameters may not have the 'var' specifier}} {{14-18=}}

  func result() -> AnyObject? { return nil }
  func both(_ x: AnyObject) -> AnyObject? { return x }

  init(_: AnyObject) {}
  init(one: AnyObject) {}
  init(a: AnyObject, b: AnyObject) {}
}

class IUOTestSubclass : IUOTestBaseClass {
  override func oneA(_: AnyObject!) {} // expected-warning {{overriding instance method parameter of type 'AnyObject' with implicitly unwrapped optional type 'AnyObject!'}}
  // expected-note@-1 {{remove '!' to make the parameter required}} {{34-35=}}
  // expected-note@-2 {{add parentheses to silence this warning}} {{25-25=(}} {{35-35=)}}
  override func oneB(x: AnyObject!) {} // expected-warning {{overriding instance method parameter of type 'AnyObject' with implicitly unwrapped optional type 'AnyObject!'}}
  // expected-note@-1 {{remove '!' to make the parameter required}} {{34-35=}}
  // expected-note@-2 {{add parentheses to silence this warning}} {{25-25=(}} {{35-35=)}}
  override func oneC(_ x: AnyObject!) {} // expected-warning {{overriding instance method parameter of type 'AnyObject' with implicitly unwrapped optional type 'AnyObject!'}}
  // expected-note@-1 {{remove '!' to make the parameter required}} {{36-37=}}
  // expected-note@-2 {{add parentheses to silence this warning}} {{27-27=(}} {{37-37=)}}

  override func manyA(_: AnyObject!, _: AnyObject!) {} // expected-warning 2 {{overriding instance method parameter of type 'AnyObject' with implicitly unwrapped optional type 'AnyObject!'}}
  // expected-note@-1 2 {{remove '!' to make the parameter required}}
  // expected-note@-2 2 {{add parentheses to silence this warning}}
  override func manyB(_ a: AnyObject!, b: AnyObject!) {} // expected-warning 2 {{overriding instance method parameter of type 'AnyObject' with implicitly unwrapped optional type 'AnyObject!'}}
  // expected-note@-1 2 {{remove '!' to make the parameter required}} 
  // expected-note@-2 2 {{add parentheses to silence this warning}}

  override func result() -> AnyObject! { return nil } // expected-warning {{overriding instance method optional result type 'AnyObject?' with implicitly unwrapped optional type 'AnyObject!'}}
  // expected-note@-1 {{use '?' to make the result optional}} {{38-39=?}}
  // expected-note@-2 {{add parentheses to silence this warning}} {{29-29=(}} {{39-39=)}}
  override func both(_ x: AnyObject!) -> AnyObject! { return x } // expected-warning {{overriding instance method optional result type 'AnyObject?' with implicitly unwrapped optional type 'AnyObject!'}} expected-warning {{overriding instance method parameter of type 'AnyObject' with implicitly unwrapped optional type 'AnyObject!'}}
  // expected-note@-1 {{use '?' to make the result optional}} {{51-52=?}} expected-note@-1 {{remove '!' to make the parameter required}} {{36-37=}}
  // expected-note@-2 2 {{add parentheses to silence this warning}}

  override init(_: AnyObject!) {} // expected-warning {{overriding initializer parameter of type 'AnyObject' with implicitly unwrapped optional type 'AnyObject!'}}
  // expected-note@-1 {{remove '!' to make the parameter required}} {{29-30=}}
  // expected-note@-2 {{add parentheses to silence this warning}} {{20-20=(}} {{30-30=)}}
  override init(one: AnyObject!) {} // expected-warning {{overriding initializer parameter of type 'AnyObject' with implicitly unwrapped optional type 'AnyObject!'}}
  // expected-note@-1 {{remove '!' to make the parameter required}} {{31-32=}}
  // expected-note@-2 {{add parentheses to silence this warning}} {{22-22=(}} {{32-32=)}}
  override init(a: AnyObject!, b: AnyObject!) {} // expected-warning 2 {{overriding initializer parameter of type 'AnyObject' with implicitly unwrapped optional type 'AnyObject!'}}
  // expected-note@-1 2 {{remove '!' to make the parameter required}}
  // expected-note@-2 2 {{add parentheses to silence this warning}}
}

class IUOTestSubclass2 : IUOTestBaseClass {
  override func oneA(_ x: AnyObject!) {} // expected-warning {{overriding instance method parameter of type 'AnyObject' with implicitly unwrapped optional type 'AnyObject!'}}
  // expected-note@-1 {{remove '!' to make the parameter required}} {{36-37=}}
  // expected-note@-2 {{add parentheses to silence this warning}} {{27-27=(}} {{37-37=)}}

  override func oneB(x: ImplicitlyUnwrappedOptional<AnyObject>) {}  // expected-warning {{overriding instance method parameter of type 'AnyObject' with implicitly unwrapped optional type 'ImplicitlyUnwrappedOptional<AnyObject>'}}
  // expected-note@-1 {{add parentheses to silence this warning}} {{25-25=(}} {{63-63=)}}

  override func oneC(_: AnyObject!) {} // expected-warning {{overriding instance method parameter of type 'AnyObject' with implicitly unwrapped optional type 'AnyObject!'}}
  // expected-note@-1 {{remove '!' to make the parameter required}} {{34-35=}}
  // expected-note@-2 {{add parentheses to silence this warning}} {{25-25=(}} {{35-35=)}}
  
}

class IUOTestSubclassOkay : IUOTestBaseClass {
  override func oneA(_: AnyObject?) {}
  override func oneB(x: (AnyObject!)) {}
  override func oneC(_ x: AnyObject) {}

  override func result() -> (AnyObject!) { return nil }
}

class GenericBase<T> {}
class ConcreteDerived: GenericBase<Int> {}

class OverriddenWithConcreteDerived<T> {
  func foo() -> GenericBase<T> {} // expected-note{{potential overridden instance method 'foo()' here}}
}
class OverridesWithMismatchedConcreteDerived<T>:
    OverriddenWithConcreteDerived<T> {
  override func foo() -> ConcreteDerived {} //expected-error{{does not override}}
}
class OverridesWithConcreteDerived:
    OverriddenWithConcreteDerived<Int> {
  override func foo() -> ConcreteDerived {}
}
