// RUN: %target-parse-verify-swift

func markUsed<T>(_ t: T) {}

struct X { }
var _x: X

class SomeClass {}

func takeTrailingClosure(_ fn: () -> ()) -> Int {}
func takeIntTrailingClosure(_ fn: () -> Int) -> Int {}

//===---
// Stored properties
//===---

var stored_prop_1: Int = 0

var stored_prop_2: Int = takeTrailingClosure {}

//===---
// Computed properties -- basic parsing
//===---

var a1: X {
  get {
    return _x
  }
}

var a2: X {
  get {
    return _x
  }
  set {
    _x = newValue
  }
}

var a3: X {
  get {
    return _x
  }
  set(newValue) {
    _x = newValue
  }
}

var a4: X {
  set {
    _x = newValue
  }
  get {
    return _x
  }
}

var a5: X {
  set(newValue) {
    _x = newValue
  }
  get {
    return _x
  }
}

// Reading/writing properties
func accept_x(_ x: X) { }
func accept_x_inout(_ x: inout X) { }

func test_global_properties(_ x: X) {
  accept_x(a1)
  accept_x(a2)
  accept_x(a3)
  accept_x(a4)
  accept_x(a5)

  a1 = x // expected-error {{cannot assign to value: 'a1' is a get-only property}}
  a2 = x
  a3 = x
  a4 = x
  a5 = x

  accept_x_inout(&a1) // expected-error {{cannot pass immutable value as inout argument: 'a1' is a get-only property}}
  accept_x_inout(&a2)
  accept_x_inout(&a3)
  accept_x_inout(&a4)
  accept_x_inout(&a5)
}

//===--- Implicit 'get'.

var implicitGet1: X {
  return _x
}

var implicitGet2: Int {
  var zzz = 0
  // For the purpose of this test, any other function attribute work as well.
  @inline(__always)
  func foo() {}
  return 0
}

var implicitGet3: Int {
  @inline(__always)
  func foo() {}
  return 0
}

// Here we used apply weak to the getter itself, not to the variable.
var x15: Int {
  // For the purpose of this test we need to use an attribute that cannot be
  // applied to the getter.
  weak
  var foo: SomeClass? = SomeClass()  // expected-warning {{variable 'foo' was written to, but never read}}
  return 0
}


// Disambiguated as stored property with a trailing closure in the initializer.
//
// FIXME: QoI could be much better here.
var disambiguateGetSet1a: Int = 0 {
  get {} // expected-error {{use of unresolved identifier 'get'}}
}
var disambiguateGetSet1b: Int = 0 {
  get { // expected-error {{use of unresolved identifier 'get'}}
    return 42
  }
}
var disambiguateGetSet1c: Int = 0 {
  set {} // expected-error {{use of unresolved identifier 'set'}}
}
var disambiguateGetSet1d: Int = 0 {
  set(newValue) {} // expected-error {{use of unresolved identifier 'set'}} expected-error {{use of unresolved identifier 'newValue'}}
}

// Disambiguated as stored property with a trailing closure in the initializer.
func disambiguateGetSet2() {
  func get(_ fn: () -> ()) {}
  var a: Int = takeTrailingClosure {
    get {}
  }
  // Check that the property is read-write.
  a = a + 42
}
func disambiguateGetSet2Attr() {
  func get(_ fn: () -> ()) {}
  var a: Int = takeTrailingClosure {
    @inline(__always)
    func foo() {}
    get {}
  }
  // Check that the property is read-write.
  a = a + 42
}

// Disambiguated as stored property with a trailing closure in the initializer.
func disambiguateGetSet3() {
  func set(_ fn: () -> ()) {}
  var a: Int = takeTrailingClosure {
    set {}
  }
  // Check that the property is read-write.
  a = a + 42
}
func disambiguateGetSet3Attr() {
  func set(_ fn: () -> ()) {}
  var a: Int = takeTrailingClosure {
    @inline(__always)
    func foo() {}
    set {}
  }
  // Check that the property is read-write.
  a = a + 42
}

// Disambiguated as stored property with a trailing closure in the initializer.
func disambiguateGetSet4() {
  func set(_ x: Int, fn: () -> ()) {}
  let newValue: Int = 0
  var a: Int = takeTrailingClosure {
    set(newValue) {}
  }
  // Check that the property is read-write.
  a = a + 42
}
func disambiguateGetSet4Attr() {
  func set(_ x: Int, fn: () -> ()) {}
  var newValue: Int = 0
  var a: Int = takeTrailingClosure {
    @inline(__always)
    func foo() {}
    set(newValue) {}
  }
  // Check that the property is read-write.
  a = a + 42
}

// Disambiguated as stored property with a trailing closure in the initializer.
var disambiguateImplicitGet1: Int = 0 { // expected-error {{cannot call value of non-function type 'Int'}}
  return 42
}
var disambiguateImplicitGet2: Int = takeIntTrailingClosure {
  return 42
}


//===---
// Observed properties
//===---

class C {
  var prop1 = 42 {
    didSet { }
  }
  var prop2 = false {
    willSet { }
  }
  var prop3: Int? = nil {
    didSet { }
  }
  var prop4: Bool? = nil {
    willSet { }
  }
}

protocol TrivialInit {
  init()
}

class CT<T : TrivialInit> {
  var prop1 = 42 {
    didSet { }
  }
  var prop2 = false {
    willSet { }
  }
  var prop3: Int? = nil {
    didSet { }
  }
  var prop4: Bool? = nil {
    willSet { }
  }
  var prop5: T? = nil {
    didSet { }
  }
  var prop6: T? = nil {
    willSet { }
  }
  var prop7 = T() {
    didSet { }
  }
  var prop8 = T() {
    willSet { }
  }
}

//===---
// Parsing problems
//===---

var computed_prop_with_init_1: X {
  get {}
} = X()  // expected-error {{expected expression}} expected-error {{consecutive statements on a line must be separated by ';'}} {{2-2=;}}

// FIXME: Redundant error below
var x2 { // expected-error{{computed property must have an explicit type}} expected-error{{type annotation missing in pattern}}
  get {
    return _x
  }
}

var (x3): X { // expected-error{{getter/setter can only be defined for a single variable}}
  get {
    return _x
  }
}

var duplicateAccessors1: X {
  get { // expected-note {{previous definition of getter is here}}
    return _x
  }
  set { // expected-note {{previous definition of setter is here}}
    _x = value
  }
  get { // expected-error {{duplicate definition of getter}}
    return _x
  }
  set(v) { // expected-error {{duplicate definition of setter}}
    _x = v
  }
}

var duplicateAccessors2: Int = 0 {
  willSet { // expected-note {{previous definition of willSet is here}}
  }
  didSet { // expected-note {{previous definition of didSet is here}}
  }
  willSet { // expected-error {{duplicate definition of willSet}}
  }
  didSet { // expected-error {{duplicate definition of didSet}}
  }
}

var extraTokensInAccessorBlock1: X {
  get {}
  a // expected-error {{expected 'get', 'set', 'willSet', or 'didSet' keyword to start an accessor definition}}
}
var extraTokensInAccessorBlock2: X {
  get {}
  weak // expected-error {{expected 'get', 'set', 'willSet', or 'didSet' keyword to start an accessor definition}}
  a
}
var extraTokensInAccessorBlock3: X {
  get {}
  a = b // expected-error {{expected 'get', 'set', 'willSet', or 'didSet' keyword to start an accessor definition}}
  set {}
  get {}
}

var extraTokensInAccessorBlock4: X {
  get blah wibble // expected-error{{expected '{' to start getter definition}}
}
var extraTokensInAccessorBlock5: X {
  set blah wibble // expected-error{{expected '{' to start setter definition}}
}
var extraTokensInAccessorBlock6: X {
  willSet blah wibble // expected-error{{expected '{' to start willSet definition}}
}
var extraTokensInAccessorBlock7: X {
  didSet blah wibble // expected-error{{expected '{' to start didSet definition}}
}

var extraTokensInAccessorBlock8: X {
  foo // expected-error {{use of unresolved identifier 'foo'}}
  get {} // expected-error{{use of unresolved identifier 'get'}}
  set {} // expected-error{{use of unresolved identifier 'set'}}
}

var extraTokensInAccessorBlock9: Int {
  get // expected-error {{expected '{' to start getter definition}}
    var a = b
}

struct extraTokensInAccessorBlock10 {
  var x: Int {
    get // expected-error {{expected '{' to start getter definition}}
      var a = b
  }

  init() {}
}

var x9: X {
  get ( ) { // expected-error{{expected '{' to start getter definition}}
  }
}

var x10: X {
  set ( : ) { // expected-error{{expected setter parameter name}}
  }
  get {}
}

var x11 : X {
  set { // expected-error{{variable with a setter must also have a getter}}
  }
}

var x12: X {
  set(newValue %) { // expected-error {{expected ')' after setter parameter name}} expected-note {{to match this opening '('}}
  // expected-error@-1 {{expected '{' to start setter definition}}
  }
}

var x13: X {} // expected-error {{computed property must have accessors specified}}

// Type checking problems
struct Y { }
var y: Y
var x20: X {
  get {
    return y // expected-error{{cannot convert return expression of type 'Y' to return type 'X'}}
  }
  set {
    y = newValue // expected-error{{cannot assign value of type 'X' to type 'Y'}}
  }
}

var x21: X {
  get {
    return y // expected-error{{cannot convert return expression of type 'Y' to return type 'X'}}
  }
  set(v) {
    y = v // expected-error{{cannot assign value of type 'X' to type 'Y'}}
  }
}

var x23: Int, x24: Int { // expected-error{{'var' declarations with multiple variables cannot have explicit getters/setters}}
  return 42
}

var x25: Int { // expected-error{{'var' declarations with multiple variables cannot have explicit getters/setters}}
  return 42
}, x26: Int

// Properties of struct/enum/extensions
struct S {
  var _backed_x: X, _backed_x2: X
  var x: X {
    get {
      return _backed_x
    }
    mutating
    set(v) {
      _backed_x = v
    }
  }
}

extension S {
  var x2: X {
    get {
      return self._backed_x2
    }
    mutating
    set {
      _backed_x2 = newValue
    }
  }

  var x3: X {
    get {
      return self._backed_x2
    }
  }
}

struct StructWithExtension1 {
  var foo: Int
  static var fooStatic = 4
}
extension StructWithExtension1 {
  var fooExt: Int // expected-error {{extensions may not contain stored properties}}
  static var fooExtStatic = 4
}

class ClassWithExtension1 {
  var foo: Int = 0
  class var fooStatic = 4 // expected-error {{class stored properties not supported in classes; did you mean 'static'?}}
}
extension ClassWithExtension1 {
  var fooExt: Int // expected-error {{extensions may not contain stored properties}}
  class var fooExtStatic = 4 // expected-error {{class stored properties not supported in classes; did you mean 'static'?}}
}

enum EnumWithExtension1 {
  var foo: Int // expected-error {{enums may not contain stored properties}}
  static var fooStatic  = 4
}
extension EnumWithExtension1 {
  var fooExt: Int // expected-error {{extensions may not contain stored properties}}
  static var fooExtStatic = 4
}

protocol ProtocolWithExtension1 {
  var foo: Int { get }
  static var fooStatic : Int { get }
}
extension ProtocolWithExtension1 {
  final var fooExt: Int // expected-error{{extensions may not contain stored properties}}
  final static var fooExtStatic = 4 // expected-error{{static stored properties not supported in generic types}}
}

func getS() -> S {
  let s: S
  return s
}

func test_extension_properties(_ s: inout S, x: inout X) {
  accept_x(s.x)
  accept_x(s.x2)
  accept_x(s.x3)

  accept_x(getS().x)
  accept_x(getS().x2)
  accept_x(getS().x3)

  s.x = x
  s.x2 = x
  s.x3 = x // expected-error{{cannot assign to property: 'x3' is a get-only property}}

  getS().x = x // expected-error{{cannot assign to property: 'getS' returns immutable value}}
  getS().x2 = x // expected-error{{cannot assign to property: 'getS' returns immutable value}}
  getS().x3 = x // expected-error{{cannot assign to property: 'x3' is a get-only property}}

  accept_x_inout(&getS().x) // expected-error{{cannot pass immutable value as inout argument: 'getS' returns immutable value}}
  accept_x_inout(&getS().x2) // expected-error{{cannot pass immutable value as inout argument: 'getS' returns immutable value}}
  accept_x_inout(&getS().x3) // expected-error{{cannot pass immutable value as inout argument: 'x3' is a get-only property}}

  x = getS().x
  x = getS().x2
  x = getS().x3

  accept_x_inout(&s.x)
  accept_x_inout(&s.x2)
  accept_x_inout(&s.x3) // expected-error{{cannot pass immutable value as inout argument: 'x3' is a get-only property}}
}

extension S {
  mutating
  func test(other_x: inout X) {
    x = other_x
    x2 = other_x
    x3 = other_x // expected-error{{cannot assign to property: 'x3' is a get-only property}}

    other_x = x
    other_x = x2
    other_x = x3
  }
}

// Accessor on non-settable type

struct Aleph {
  var b: Beth {
    get {
      return Beth(c: 1)
    }
  }
}

struct Beth {
  var c: Int
}

func accept_int_inout(_ c: inout Int) { }
func accept_int(_ c: Int) { }

func test_settable_of_nonsettable(_ a: Aleph) {
  a.b.c = 1 // expected-error{{cannot assign}}
  let x:Int = a.b.c
  _ = x

  accept_int(a.b.c)
  accept_int_inout(&a.b.c) // expected-error {{cannot pass immutable value as inout argument: 'b' is a get-only property}}
}

// TODO: Static properties are only implemented for nongeneric structs yet.

struct MonoStruct {
  static var foo: Int = 0
  static var (bar, bas): (String, UnicodeScalar) = ("zero", "0")

  static var zim: UInt8 {
    return 0
  }

  static var zang = UnicodeScalar("\0")

  static var zung: UInt16 {
    get {
      return 0
    }
    set {}
  }

  var a: Double
  var b: Double
}

struct MonoStructOneProperty {
  static var foo: Int = 22
}

enum MonoEnum {
  static var foo: Int = 0

  static var zim: UInt8 {
    return 0
  }
}

struct GenStruct<T> {
  static var foo: Int = 0 // expected-error{{static stored properties not supported in generic types}}
}

class MonoClass {
  class var foo: Int = 0 // expected-error{{class stored properties not supported in classes; did you mean 'static'?}}
}

protocol Proto {
  static var foo: Int { get }
}

func staticPropRefs() -> (Int, Int, String, UnicodeScalar, UInt8) {
  return (MonoStruct.foo, MonoEnum.foo, MonoStruct.bar, MonoStruct.bas,
          MonoStruct.zim)
}

func staticPropRefThroughInstance(_ foo: MonoStruct) -> Int {
  return foo.foo //expected-error{{static member 'foo' cannot be used on instance of type 'MonoStruct'}}
}

func memberwiseInitOnlyTakesInstanceVars() -> MonoStruct {
  return MonoStruct(a: 1.2, b: 3.4)
}

func getSetStaticProperties() -> (UInt8, UInt16) {
  MonoStruct.zim = 12 // expected-error{{cannot assign}}
  MonoStruct.zung = 34

  return (MonoStruct.zim, MonoStruct.zung)
}


var selfRefTopLevel: Int {
  return selfRefTopLevel // expected-warning {{attempting to access 'selfRefTopLevel' within its own getter}}
}
var selfRefTopLevelSetter: Int {
  get {
    return 42
  }
  set {
    markUsed(selfRefTopLevelSetter) // no-warning
    selfRefTopLevelSetter = newValue // expected-warning {{attempting to modify 'selfRefTopLevelSetter' within its own setter}}
  }
}
var selfRefTopLevelSilenced: Int {
  get {
    return properties.selfRefTopLevelSilenced // no-warning
  }
  set {
    properties.selfRefTopLevelSilenced = newValue // no-warning
  }
}

class SelfRefProperties {
  var getter: Int {
    return getter // expected-warning {{attempting to access 'getter' within its own getter}}
    // expected-note@-1 {{access 'self' explicitly to silence this warning}} {{12-12=self.}}
  }
  var setter: Int {
    get {
      return 42
    }
    set {
      markUsed(setter) // no-warning
      var unused = setter + setter // expected-warning {{initialization of variable 'unused' was never used; consider replacing with assignment to '_' or removing it}} {{7-17=_}}
      setter = newValue // expected-warning {{attempting to modify 'setter' within its own setter}}
      // expected-note@-1 {{access 'self' explicitly to silence this warning}} {{7-7=self.}}
    }
  }
  var silenced: Int {
    get {
      return self.silenced // no-warning
    }
    set {
      self.silenced = newValue // no-warning
    }
  }

  var someOtherInstance: SelfRefProperties = SelfRefProperties()
  var delegatingVar: Int {
    // This particular example causes infinite access, but it's easily possible
    // for the delegating instance to do something else.
    return someOtherInstance.delegatingVar // no-warning
  }
}

func selfRefLocal() {
  var getter: Int {
    return getter // expected-warning {{attempting to access 'getter' within its own getter}}
  }
  var setter: Int {
    get {
      return 42
    }
    set {
      markUsed(setter) // no-warning
      setter = newValue // expected-warning {{attempting to modify 'setter' within its own setter}}
    }
  }
}


struct WillSetDidSetProperties {
  var a: Int {
    willSet {
      markUsed("almost")
    }
    didSet {
      markUsed("here")
    }
  }

  var b: Int {
    willSet {
      markUsed(b)
      markUsed(newValue)
    }
  }

  var c: Int {
    willSet(newC) {
      markUsed(c)
      markUsed(newC)
    }
  }

  var d: Int {
    didSet {
      markUsed("woot")
    }
    get { // expected-error {{didSet variable may not also have a get specifier}}
      return 4
    }
  }

  var e: Int {
    willSet {
      markUsed("woot")
    }
    set { // expected-error {{willSet variable may not also have a set specifier}}
      return 4
    }
  }

  var f: Int {
    willSet(5) {} // expected-error {{expected willSet parameter name}}
    didSet(^) {} // expected-error {{expected didSet parameter name}}
  }

  var g: Int {
    willSet(newValue 5) {} // expected-error {{expected ')' after willSet parameter name}} expected-note {{to match this opening '('}}
    // expected-error@-1 {{expected '{' to start willSet definition}}
  }
  var h: Int {
    didSet(oldValue ^) {} // expected-error {{expected ')' after didSet parameter name}} expected-note {{to match this opening '('}}
    // expected-error@-1 {{expected '{' to start didSet definition}}
  }

  // didSet/willSet with initializers.
  // Disambiguate trailing closures.
  var disambiguate1: Int = 42 {   // simple initializer, simple label
    didSet {
      markUsed("eek")
    }
  }

  var disambiguate2: Int = 42 {    // simple initializer, complex label
    willSet(v) {
      markUsed("eek")
    }
  }

  var disambiguate3: Int = takeTrailingClosure {} {  // Trailing closure case.
    willSet(v) {
      markUsed("eek")
    }
  }

  var disambiguate4: Int = 42 {
    willSet {}
  }

  var disambiguate5: Int = 42 {
    didSet {}
  }

  var disambiguate6: Int = takeTrailingClosure {
    @inline(__always)
    func f() {}
    return ()
  }

  var inferred1 = 42 {
    willSet {
      markUsed("almost")
    }
    didSet {
      markUsed("here")
    }
  }

  var inferred2 = 40 {
    willSet {
      markUsed(b)
      markUsed(newValue)
    }
  }

  var inferred3 = 50 {
    willSet(newC) {
      markUsed(c)
      markUsed(newC)
    }
  }
}

// Disambiguated as accessor.
struct WillSetDidSetDisambiguate1 {
  var willSet: Int
  var x: (() -> ()) -> Int = takeTrailingClosure {
    willSet = 42 // expected-error {{expected '{' to start willSet definition}}
  }
}
struct WillSetDidSetDisambiguate1Attr {
  var willSet: Int
  var x: (() -> ()) -> Int = takeTrailingClosure {
    willSet = 42 // expected-error {{expected '{' to start willSet definition}}
  }
}

// Disambiguated as accessor.
struct WillSetDidSetDisambiguate2 {
  func willSet(_: () -> Int) {}
  var x: (() -> ()) -> Int = takeTrailingClosure {
    willSet {}
  }
}
struct WillSetDidSetDisambiguate2Attr {
  func willSet(_: () -> Int) {}
  var x: (() -> ()) -> Int = takeTrailingClosure {
    willSet {}
  }
}

// No need to disambiguate -- this is clearly a function call.
func willSet(_: () -> Int) {}
struct WillSetDidSetDisambiguate3 {
  var x: Int = takeTrailingClosure({
    willSet { 42 }
  })
}

protocol ProtocolGetSet1 {
  var a: Int // expected-error {{property in protocol must have explicit { get } or { get set } specifier}}
}
protocol ProtocolGetSet2 {
  var a: Int {} // expected-error {{property in protocol must have explicit { get } or { get set } specifier}}
}
protocol ProtocolGetSet3 {
  var a: Int { get }
}
protocol ProtocolGetSet4 {
  var a: Int { set } // expected-error {{variable with a setter must also have a getter}}
}
protocol ProtocolGetSet5 {
  var a: Int { get set }
}
protocol ProtocolGetSet6 {
  var a: Int { set get }
}

protocol ProtocolWillSetDidSet1 {
  var a: Int { willSet } // expected-error {{property in protocol must have explicit { get } or { get set } specifier}} expected-error {{expected get or set in a protocol property}}
}
protocol ProtocolWillSetDidSet2 {
  var a: Int { didSet } // expected-error {{property in protocol must have explicit { get } or { get set } specifier}} expected-error {{expected get or set in a protocol property}}
}
protocol ProtocolWillSetDidSet3 {
  var a: Int { willSet didSet } // expected-error {{property in protocol must have explicit { get } or { get set } specifier}} expected-error {{expected get or set in a protocol property}}
}
protocol ProtocolWillSetDidSet4 {
  var a: Int { didSet willSet } // expected-error {{property in protocol must have explicit { get } or { get set } specifier}} expected-error {{expected get or set in a protocol property}}
}

var globalDidsetWillSet: Int {  // expected-error {{non-member observing properties require an initializer}}
  didSet {}
}
var globalDidsetWillSet2 : Int = 42 {
  didSet {}
}


class Box {
  var num: Int

  init(num: Int) {
    self.num = num
  }
}

func double(_ val: inout Int) {
  val *= 2
}

class ObservingPropertiesNotMutableInWillSet {
  var anotherObj : ObservingPropertiesNotMutableInWillSet
  
  init() {}
  var property: Int = 42 {
    willSet {
      // <rdar://problem/16826319> willSet immutability behavior is incorrect
      anotherObj.property = 19 // ok
      property = 19  // expected-warning {{attempting to store to property 'property' within its own willSet}}
      double(&property)  // expected-warning {{attempting to store to property 'property' within its own willSet}}
      double(&self.property)  // no-warning
    }
  }

  // <rdar://problem/21392221> - call to getter through BindOptionalExpr was not viewed as a load
  var _oldBox : Int
  weak var weakProperty: Box? {
    willSet {
      _oldBox = weakProperty?.num ?? -1
    }
  }

  func localCase() {
    var localProperty: Int = 42 {
      willSet {
        localProperty = 19   // expected-warning {{attempting to store to property 'localProperty' within its own willSet}}
      }
    }
  }
}

func doLater(_ fn : () -> ()) {}

// rdar://<rdar://problem/16264989> property not mutable in closure inside of its willSet
class MutableInWillSetInClosureClass {
    var bounds: Int = 0 {
    willSet {
        let oldBounds = bounds
        doLater { self.bounds = oldBounds }
    }
    }
}


// <rdar://problem/16191398> add an 'oldValue' to didSet so you can implement "didChange" properties
var didSetPropertyTakingOldValue : Int = 0 {
  didSet(oldValue) {
    markUsed(oldValue)
    markUsed(didSetPropertyTakingOldValue)
  }
}



// rdar://16280138 - synthesized getter is defined in terms of archetypes, not interface types
protocol AbstractPropertyProtocol {
  associatedtype Index
  var a : Index { get }
}
struct AbstractPropertyStruct<T> : AbstractPropertyProtocol {
  typealias Index = T
  var a : T
}

// Allow _silgen_name accessors without bodies.
var _silgen_nameGet1: Int {
  @_silgen_name("get1") get
  set { }
}

var _silgen_nameGet2: Int {
  set { }
  @_silgen_name("get2") get
}

var _silgen_nameGet3: Int {
  @_silgen_name("get3") get
}

var _silgen_nameGetSet: Int {
  @_silgen_name("get4") get
  @_silgen_name("set4") set
}



// <rdar://problem/16375910> reject observing properties overriding readonly properties
class Base16375910 {
  var x : Int {   // expected-note {{attempt to override property here}}
    return 42
  }
  
  var y : Int {          // expected-note {{attempt to override property here}}
    get { return 4 }
    set {}
  }
}

class Derived16375910 : Base16375910 {
  override init() {}
  override var x : Int { // expected-error {{cannot observe read-only property 'x'; it can't change}}
    willSet {
      markUsed(newValue)
    }
  }
}

// <rdar://problem/16382967> Observing properties have no storage, so shouldn't prevent initializer synth
class Derived16382967 : Base16375910 {
  override var y : Int {
    willSet {
      markUsed(newValue)
    }
  }
}

// <rdar://problem/16659058> Read-write properties can be made read-only in a property override
class Derived16659058 : Base16375910 {
  override var y : Int {  // expected-error {{cannot override mutable property with read-only property 'y'}}
    get { return 42 }
  }
}



// <rdar://problem/16406886> Observing properties don't work with ownership types
struct PropertiesWithOwnershipTypes {
  unowned var p1 : SomeClass {
    didSet {
    }
  }

  init(res: SomeClass) {
    p1 = res
  }
}


// <rdar://problem/16608609> Assert (and incorrect error message) when defining a constant stored property with observers
class Test16608609 {
   let constantStored: Int = 0 {  // expected-error {{'let' declarations cannot be observing properties}}
      willSet {
      }
      didSet {
      }
   }
}

// <rdar://problem/16941124> Overriding property observers warn about using the property value "within its own getter"
class rdar16941124Base {
   var x = 0
}
class rdar16941124Derived : rdar16941124Base {
   var y = 0
   override var x: Int {
      didSet {
         y = x + 1  // no warning.
      }
   }
}


// Overrides of properties with custom ownership.
class OwnershipBase {
  class var defaultObject: AnyObject { fatalError("") }

  var strongVar: AnyObject? // expected-note{{overridden declaration is here}}
  weak var weakVar: AnyObject?

  // FIXME: These should be optional to properly test overriding.
  unowned var unownedVar: AnyObject = defaultObject
  unowned(unsafe) var unownedUnsafeVar: AnyObject = defaultObject // expected-note{{overridden declaration is here}}
}

class OwnershipExplicitSub : OwnershipBase {
  override var strongVar: AnyObject? {
    didSet {}
  }
  override weak var weakVar: AnyObject? {
    didSet {}
  }
  override unowned var unownedVar: AnyObject {
    didSet {}
  }
  override unowned(unsafe) var unownedUnsafeVar: AnyObject {
    didSet {}
  }
}

class OwnershipImplicitSub : OwnershipBase {
  override var strongVar: AnyObject? {
    didSet {}
  }
  override weak var weakVar: AnyObject? {
    didSet {}
  }
  override unowned var unownedVar: AnyObject {
    didSet {}
  }
  override unowned(unsafe) var unownedUnsafeVar: AnyObject {
    didSet {}
  }
}

class OwnershipBadSub : OwnershipBase {
  override weak var strongVar: AnyObject? { // expected-error {{cannot override strong property with weak property}}
    didSet {}
  }
  override unowned var weakVar: AnyObject? { // expected-error {{'unowned' may only be applied to class and class-bound protocol types, not 'AnyObject?'}}
    didSet {}
  }
  override weak var unownedVar: AnyObject { // expected-error {{'weak' variable should have optional type 'AnyObject?'}}
    didSet {}
  }
  override unowned var unownedUnsafeVar: AnyObject { // expected-error {{cannot override unowned(unsafe) property with unowned property}}
    didSet {}
  }
}



// <rdar://problem/17391625> Swift Compiler Crashes when Declaring a Variable and didSet in an Extension
class rdar17391625 {
  var prop = 42  // expected-note {{overridden declaration is here}}
}

extension rdar17391625 {
  var someStoredVar: Int       // expected-error {{extensions may not contain stored properties}}
  var someObservedVar: Int {   // expected-error {{extensions may not contain stored properties}}
  didSet {
  }
  }
}

class rdar17391625derived :  rdar17391625 {
}

extension rdar17391625derived {
  // Not a stored property, computed because it is an override.
  override var prop: Int { // expected-error {{declarations in extensions cannot override yet}}
  didSet {
  }
  }
}

// <rdar://problem/27671033> Crash when defining property inside an invalid extension
public protocol rdar27671033P {}
struct rdar27671033S<Key, Value> {}
extension rdar27671033S : rdar27671033P where Key == String { // expected-error {{extension of type 'rdar27671033S' with constraints cannot have an inheritance clause}}
  // expected-error@-1 {{same-type requirement makes generic parameter 'Key' non-generic}}
  let d = rdar27671033S<Int, Int>() // expected-error {{extensions may not contain stored properties}}
}

// <rdar://problem/19874152> struct memberwise initializer violates new sanctity of previously set `let` property
struct r19874152S1 {
  let number : Int = 42
}
_ = r19874152S1(number:64)  // expected-error {{argument passed to call that takes no arguments}}
_ = r19874152S1()  // Ok

struct r19874152S2 {
  var number : Int = 42
}
_ = r19874152S2(number:64)  // Ok, property is a var.
_ = r19874152S2()  // Ok

struct r19874152S3 { // expected-note {{'init(flavour:)' declared here}}
  let number : Int = 42
  let flavour : Int
}
_ = r19874152S3(number:64)  // expected-error {{incorrect argument label in call (have 'number:', expected 'flavour:')}} {{17-23=flavour}}
_ = r19874152S3(number:64, flavour: 17)  // expected-error {{extra argument 'number' in call}}
_ = r19874152S3(flavour: 17)  // ok
_ = r19874152S3()  // expected-error {{missing argument for parameter 'flavour' in call}}

struct r19874152S4 {
  let number : Int? = nil
}
_ = r19874152S4(number:64)  // expected-error {{argument passed to call that takes no arguments}}
_ = r19874152S4()  // Ok


struct r19874152S5 {
}
_ = r19874152S5()  // ok


struct r19874152S6 {
  let (a,b) = (1,2)   // Cannot handle implicit synth of this yet.
}
_ = r19874152S5()  // ok



// <rdar://problem/24314506> QoI: Fix-it for dictionary initializer on required class var suggests [] instead of [:]
class r24314506 {  // expected-error {{class 'r24314506' has no initializers}}
  var myDict: [String: AnyObject]  // expected-note {{stored property 'myDict' without initial value prevents synthesized initializers}} {{34-34= = [:]}}
}


