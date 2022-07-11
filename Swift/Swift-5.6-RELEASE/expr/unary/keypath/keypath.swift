// RUN: %target-swift-frontend -typecheck -parse-as-library %s -verify

struct Sub: Hashable {
  static func ==(_: Sub, _: Sub) -> Bool { return true }
  func hash(into hasher: inout Hasher) {}
}
struct OptSub: Hashable {
  static func ==(_: OptSub, _: OptSub) -> Bool { return true }
  func hash(into hasher: inout Hasher) {}
}
struct NonHashableSub {}

struct Prop {
  subscript(sub: Sub) -> A { get { return A() } set { } }
  subscript(optSub: OptSub) -> A? { get { return A() } set { } }
  subscript(nonHashableSub: NonHashableSub) -> A { get { return A() } set { } }
  subscript(a: Sub, b: Sub) -> A { get { return A() } set { } }
  subscript(a: Sub, b: NonHashableSub) -> A { get { return A() } set { } }

  var nonMutatingProperty: B {
    get { fatalError() }
    nonmutating set { fatalError() }
  }
}

struct A: Hashable {
  init() { fatalError() }

  var property: Prop
  var optProperty: Prop?
  let optLetProperty: Prop?

  subscript(sub: Sub) -> A { get { return self } set { } }

  static func ==(_: A, _: A) -> Bool { fatalError() }
  func hash(into hasher: inout Hasher) { fatalError() }
}
struct B {}
struct C<T> { // expected-note 4 {{'T' declared as parameter to type 'C'}}
  var value: T
  subscript() -> T { get { return value } }
  subscript(sub: Sub) -> T { get { return value } set { } }
  subscript<U: Hashable>(sub: U) -> U { get { return sub } set { } }
  subscript<X>(noHashableConstraint sub: X) -> X { get { return sub } set { } }
}

struct Unavailable {
  @available(*, unavailable)
  var unavailableProperty: Int
  // expected-note@-1 {{'unavailableProperty' has been explicitly marked unavailable here}}

  @available(*, unavailable)
  subscript(x: Sub) -> Int { get { } set { } }
  // expected-note@-1 {{'subscript(_:)' has been explicitly marked unavailable here}}
}

struct Deprecated {
  @available(*, deprecated)
  var deprecatedProperty: Int

  @available(*, deprecated)
  subscript(x: Sub) -> Int { get { } set { } }
}

@available(*, deprecated)
func getDeprecatedSub() -> Sub {
  return Sub()
}

extension Array where Element == A {
  var property: Prop { fatalError() }
}

protocol P { var member: String { get } }
extension B : P { var member : String { return "Member Value" } }

struct Exactly<T> {}

func expect<T>(_ x: inout T, toHaveType _: Exactly<T>.Type) {}

func testKeyPath(sub: Sub, optSub: OptSub,
                 nonHashableSub: NonHashableSub, x: Int) {
  var a = \A.property
  expect(&a, toHaveType: Exactly<WritableKeyPath<A, Prop>>.self)

  var b = \A.[sub]
  expect(&b, toHaveType: Exactly<WritableKeyPath<A, A>>.self)

  var c = \A.[sub].property
  expect(&c, toHaveType: Exactly<WritableKeyPath<A, Prop>>.self)

  var d = \A.optProperty?
  expect(&d, toHaveType: Exactly<KeyPath<A, Prop?>>.self)

  var e = \A.optProperty?[sub]
  expect(&e, toHaveType: Exactly<KeyPath<A, A?>>.self)

  var f = \A.optProperty!
  expect(&f, toHaveType: Exactly<WritableKeyPath<A, Prop>>.self)

  var g = \A.property[optSub]?.optProperty![sub]
  expect(&g, toHaveType: Exactly<KeyPath<A, A?>>.self)

  var h = \[A].property
  expect(&h, toHaveType: Exactly<KeyPath<[A], Prop>>.self)

  var i = \[A].property.nonMutatingProperty
  expect(&i, toHaveType: Exactly<ReferenceWritableKeyPath<[A], B>>.self)

  var j = \[A].[x]
  expect(&j, toHaveType: Exactly<WritableKeyPath<[A], A>>.self)

  var k = \[A: B].[A()]
  expect(&k, toHaveType: Exactly<WritableKeyPath<[A: B], B?>>.self)

  var l = \C<A>.value
  expect(&l, toHaveType: Exactly<WritableKeyPath<C<A>, A>>.self)

  // expected-error@+1{{generic parameter 'T' could not be inferred}}
  _ = \C.value

  // expected-error@+1{{}}
  _ = \(() -> ()).noMember

  let _: (A) -> Prop = \.property
  let _: PartialKeyPath<A> = \.property
  let _: KeyPath<A, Prop> = \.property
  let _: WritableKeyPath<A, Prop> = \.property
  let _: ReferenceWritableKeyPath<A, Prop> = \.property
  //expected-error@-1 {{cannot convert value of type 'WritableKeyPath<A, Prop>' to specified type 'ReferenceWritableKeyPath<A, Prop>'}}

  let _: (A) -> A = \.[sub]
  let _: PartialKeyPath<A> = \.[sub]
  let _: KeyPath<A, A> = \.[sub]
  let _: WritableKeyPath<A, A> = \.[sub]
  let _: ReferenceWritableKeyPath<A, A> = \.[sub]
  //expected-error@-1 {{cannot convert value of type 'WritableKeyPath<A, A>' to specified type 'ReferenceWritableKeyPath<A, A>'}}

  let _: (A) -> Prop? = \.optProperty?
  let _: PartialKeyPath<A> = \.optProperty?
  let _: KeyPath<A, Prop?> = \.optProperty?
  // expected-error@+1{{cannot convert}}
  let _: WritableKeyPath<A, Prop?> = \.optProperty?
  // expected-error@+1{{cannot convert}}
  let _: ReferenceWritableKeyPath<A, Prop?> = \.optProperty?

  let _: (A) -> A? = \.optProperty?[sub]
  let _: PartialKeyPath<A> = \.optProperty?[sub]
  let _: KeyPath<A, A?> = \.optProperty?[sub]
  // expected-error@+1{{cannot convert}}
  let _: WritableKeyPath<A, A?> = \.optProperty?[sub]
  // expected-error@+1{{cannot convert}}
  let _: ReferenceWritableKeyPath<A, A?> = \.optProperty?[sub]

  let _: KeyPath<A, Prop> = \.optProperty!
  let _: KeyPath<A, Prop> = \.optLetProperty!
  let _: KeyPath<A, Prop?> = \.property[optSub]?.optProperty!
  let _: KeyPath<A, A?> = \.property[optSub]?.optProperty![sub]

  let _: (C<A>) -> A = \.value
  let _: PartialKeyPath<C<A>> = \.value
  let _: KeyPath<C<A>, A> = \.value
  let _: WritableKeyPath<C<A>, A> = \.value
  let _: ReferenceWritableKeyPath<C<A>, A> = \.value
  // expected-error@-1 {{cannot convert value of type 'WritableKeyPath<C<A>, A>' to specified type 'ReferenceWritableKeyPath<C<A>, A>'}}

  let _: (C<A>) -> A = \C.value
  let _: PartialKeyPath<C<A>> = \C.value
  let _: KeyPath<C<A>, A> = \C.value
  let _: WritableKeyPath<C<A>, A> = \C.value
  // expected-error@+1{{cannot convert}}
  let _: ReferenceWritableKeyPath<C<A>, A> = \C.value

  let _: (Prop) -> B = \.nonMutatingProperty
  let _: PartialKeyPath<Prop> = \.nonMutatingProperty
  let _: KeyPath<Prop, B> = \.nonMutatingProperty
  let _: WritableKeyPath<Prop, B> = \.nonMutatingProperty
  let _: ReferenceWritableKeyPath<Prop, B> = \.nonMutatingProperty

  var m = [\A.property, \A.[sub], \A.optProperty!]
  expect(&m, toHaveType: Exactly<[PartialKeyPath<A>]>.self)

  // \.optProperty returns an optional of Prop and `\.[sub]` returns `A`, all this unifies into `[PartialKeyPath<A>]`
  var n = [\A.property, \.optProperty, \.[sub], \.optProperty!]
  expect(&n, toHaveType: Exactly<[PartialKeyPath<A>]>.self)

  let _: [PartialKeyPath<A>] = [\.property, \.optProperty, \.[sub], \.optProperty!]

  var o = [\A.property, \C<A>.value]
  expect(&o, toHaveType: Exactly<[AnyKeyPath]>.self)

  let _: AnyKeyPath = \A.property
  let _: AnyKeyPath = \C<A>.value
  let _: AnyKeyPath = \.property // expected-error {{'AnyKeyPath' does not provide enough context for root type to be inferred; consider explicitly specifying a root type}} {{24-24=<#Root#>}}
  let _: AnyKeyPath = \C.value // expected-error{{generic parameter 'T' could not be inferred}}
  let _: AnyKeyPath = \.value // expected-error {{'AnyKeyPath' does not provide enough context for root type to be inferred; consider explicitly specifying a root type}} {{24-24=<#Root#>}}

  let _ = \Prop.[nonHashableSub] // expected-error{{subscript index of type 'NonHashableSub' in a key path must be Hashable}}
  let _ = \Prop.[sub, sub]
  let _ = \Prop.[sub, nonHashableSub] // expected-error{{subscript index of type 'NonHashableSub' in a key path must be Hashable}}

  let _ = \C<Int>.[]
  let _ = \C<Int>.[sub]
  let _ = \C<Int>.[noHashableConstraint: sub]
  let _ = \C<Int>.[noHashableConstraint: nonHashableSub] // expected-error{{subscript index of type 'NonHashableSub' in a key path must be Hashable}}

  let _ = \Unavailable.unavailableProperty // expected-error {{'unavailableProperty' is unavailable}}
  let _ = \Unavailable.[sub] // expected-error {{'subscript(_:)' is unavailable}}

  let _ = \Deprecated.deprecatedProperty // expected-warning {{'deprecatedProperty' is deprecated}}
  let _ = \Deprecated.[sub] // expected-warning {{'subscript(_:)' is deprecated}}

  let _ = \A.[getDeprecatedSub()] // expected-warning {{'getDeprecatedSub()' is deprecated}}
}

func testKeyPathInGenericContext<H: Hashable, X>(hashable: H, anything: X) {
  let _ = \C<Int>.[hashable]
  let _ = \C<Int>.[noHashableConstraint: hashable]
  let _ = \C<Int>.[noHashableConstraint: anything] // expected-error{{subscript index of type 'X' in a key path must be Hashable}}
}

func testDisembodiedStringInterpolation(x: Int) {
  \(x) // expected-error{{string interpolation can only appear inside a string literal}} 
  \(x, radix: 16) // expected-error{{string interpolation can only appear inside a string literal}} 
}

func testNoComponents() {
  let _: KeyPath<A, A> = \A // expected-error{{must have at least one component}}
  let _: KeyPath<C, A> = \C // expected-error{{must have at least one component}}
  // expected-error@-1 {{generic parameter 'T' could not be inferred}}
  let _: KeyPath<A, C> = \A // expected-error{{must have at least one component}} 
  // expected-error@-1 {{generic parameter 'T' could not be inferred}}
  _ = \A // expected-error {{key path must have at least one component}}
}

struct TupleStruct {
  var unlabeled: (Int, String)
  var labeled: (foo: Int, bar: String)
}

typealias UnlabeledGenericTuple<T, U> = (T, U)
typealias LabeledGenericTuple<T, U> = (a: T, b: U)

func tupleComponent<T, U>(_: T, _: U) {
  let _ = \(Int, String).0
  let _ = \(Int, String).1
  let _ = \TupleStruct.unlabeled.0
  let _ = \TupleStruct.unlabeled.1

  let _ = \(foo: Int, bar: String).0
  let _ = \(foo: Int, bar: String).1
  let _ = \(foo: Int, bar: String).foo
  let _ = \(foo: Int, bar: String).bar
  let _ = \TupleStruct.labeled.0
  let _ = \TupleStruct.labeled.1
  let _ = \TupleStruct.labeled.foo
  let _ = \TupleStruct.labeled.bar

  let _ = \(T, U).0
  let _ = \(T, U).1
  let _ = \UnlabeledGenericTuple<T, U>.0
  let _ = \UnlabeledGenericTuple<T, U>.1

  let _ = \(a: T, b: U).0
  let _ = \(a: T, b: U).1
  let _ = \(a: T, b: U).a
  let _ = \(a: T, b: U).b
  let _ = \LabeledGenericTuple<T, U>.0
  let _ = \LabeledGenericTuple<T, U>.1
  let _ = \LabeledGenericTuple<T, U>.a
  let _ = \LabeledGenericTuple<T, U>.b
}

func tuple_el_0<T, U>() -> KeyPath<(T, U), T> {
  return \.0
}

func tuple_el_1<T, U>() -> KeyPath<(T, U), U> {
  return \.1
}

func tupleGeneric<T, U>(_ v: (T, U)) {
  _ = (1, "hello")[keyPath: tuple_el_0()]
  _ = (1, "hello")[keyPath: tuple_el_1()]

  _ = v[keyPath: tuple_el_0()]
  _ = v[keyPath: tuple_el_1()]

  _ = ("tuple", "too", "big")[keyPath: tuple_el_1()]
  // expected-note@-12 {{}}
  // expected-error@-2 {{generic parameter 'T' could not be inferred}}
  // expected-error@-3 {{generic parameter 'U' could not be inferred}}
}

struct Z { }

func testKeyPathSubscript(readonly: Z, writable: inout Z,
                          kp: KeyPath<Z, Int>,
                          wkp: WritableKeyPath<Z, Int>,
                          rkp: ReferenceWritableKeyPath<Z, Int>) {
  var sink: Int
  sink = readonly[keyPath: kp]
  sink = writable[keyPath: kp]
  sink = readonly[keyPath: wkp]
  sink = writable[keyPath: wkp]
  sink = readonly[keyPath: rkp]
  sink = writable[keyPath: rkp]

  readonly[keyPath: kp] = sink // expected-error{{cannot assign through subscript: 'kp' is a read-only key path}}
  writable[keyPath: kp] = sink // expected-error{{cannot assign through subscript: 'kp' is a read-only key path}}
  readonly[keyPath: wkp] = sink // expected-error{{cannot assign through subscript: 'readonly' is a 'let' constant}}
  writable[keyPath: wkp] = sink
  readonly[keyPath: rkp] = sink
  writable[keyPath: rkp] = sink

  let pkp: PartialKeyPath = rkp

  var anySink1 = readonly[keyPath: pkp]
  expect(&anySink1, toHaveType: Exactly<Any>.self)
  var anySink2 = writable[keyPath: pkp]
  expect(&anySink2, toHaveType: Exactly<Any>.self)

  readonly[keyPath: pkp] = anySink1 // expected-error{{cannot assign through subscript: 'pkp' is a read-only key path}}
  writable[keyPath: pkp] = anySink2 // expected-error{{cannot assign through subscript: 'pkp' is a read-only key path}}

  let akp: AnyKeyPath = pkp

  var anyqSink1 = readonly[keyPath: akp]
  expect(&anyqSink1, toHaveType: Exactly<Any?>.self)
  var anyqSink2 = writable[keyPath: akp]
  expect(&anyqSink2, toHaveType: Exactly<Any?>.self)

  readonly[keyPath: akp] = anyqSink1 // expected-error{{cannot assign through subscript: 'readonly' is a 'let' constant}}
  writable[keyPath: akp] = anyqSink2 // expected-error{{cannot assign through subscript: 'writable' is immutable}}
}

struct ZwithSubscript {
  subscript(keyPath kp: KeyPath<ZwithSubscript, Int>) -> Int { return 0 }
  subscript(keyPath kp: WritableKeyPath<ZwithSubscript, Int>) -> Int { return 0 }
  subscript(keyPath kp: ReferenceWritableKeyPath<ZwithSubscript, Int>) -> Int { return 0 }
  subscript(keyPath kp: PartialKeyPath<ZwithSubscript>) -> Any { return 0 }
}

struct NotZ {}

func testKeyPathSubscript(readonly: ZwithSubscript, writable: inout ZwithSubscript,
                          wrongType: inout NotZ,
                          kp: KeyPath<ZwithSubscript, Int>,
                          wkp: WritableKeyPath<ZwithSubscript, Int>,
                          rkp: ReferenceWritableKeyPath<ZwithSubscript, Int>) {
  var sink: Int
  sink = readonly[keyPath: kp]
  sink = writable[keyPath: kp]
  sink = readonly[keyPath: wkp]
  sink = writable[keyPath: wkp]
  sink = readonly[keyPath: rkp]
  sink = writable[keyPath: rkp]
  readonly[keyPath: kp] = sink  // expected-error {{cannot assign through subscript: subscript is get-only}}
  writable[keyPath: kp] = sink  // expected-error {{cannot assign through subscript: subscript is get-only}}
  readonly[keyPath: wkp] = sink // expected-error {{cannot assign through subscript: subscript is get-only}}
  // FIXME: silently falls back to keypath application, which seems inconsistent
  writable[keyPath: wkp] = sink
  // FIXME: silently falls back to keypath application, which seems inconsistent
  readonly[keyPath: rkp] = sink
  // FIXME: silently falls back to keypath application, which seems inconsistent
  writable[keyPath: rkp] = sink

  let pkp: PartialKeyPath = rkp

  var anySink1 = readonly[keyPath: pkp]
  expect(&anySink1, toHaveType: Exactly<Any>.self)
  var anySink2 = writable[keyPath: pkp]
  expect(&anySink2, toHaveType: Exactly<Any>.self)

  readonly[keyPath: pkp] = anySink1 // expected-error{{cannot assign through subscript: subscript is get-only}}
  writable[keyPath: pkp] = anySink2 // expected-error{{cannot assign through subscript: subscript is get-only}}

  let akp: AnyKeyPath = pkp

  var anyqSink1 = readonly[keyPath: akp]
  expect(&anyqSink1, toHaveType: Exactly<Any?>.self)
  var anyqSink2 = writable[keyPath: akp]
  expect(&anyqSink2, toHaveType: Exactly<Any?>.self)

  // FIXME: silently falls back to keypath application, which seems inconsistent
  readonly[keyPath: akp] = anyqSink1 // expected-error{{cannot assign through subscript: 'readonly' is a 'let' constant}}
  // FIXME: silently falls back to keypath application, which seems inconsistent
  writable[keyPath: akp] = anyqSink2 // expected-error{{cannot assign through subscript: 'writable' is immutable}}

  _ = wrongType[keyPath: kp] // expected-error{{cannot be applied}}
  _ = wrongType[keyPath: wkp] // expected-error{{cannot be applied}}
  _ = wrongType[keyPath: rkp] // expected-error{{cannot be applied}}
  _ = wrongType[keyPath: pkp] // expected-error{{cannot be applied}}
  _ = wrongType[keyPath: akp]
}

func testKeyPathSubscriptMetatype(readonly: Z.Type, writable: inout Z.Type,
                                  kp: KeyPath<Z.Type, Int>,
                                  wkp: WritableKeyPath<Z.Type, Int>,
                                  rkp: ReferenceWritableKeyPath<Z.Type, Int>) {
  var sink: Int
  sink = readonly[keyPath: kp]
  sink = writable[keyPath: kp]
  sink = readonly[keyPath: wkp]
  sink = writable[keyPath: wkp]
  sink = readonly[keyPath: rkp]
  sink = writable[keyPath: rkp]

  readonly[keyPath: kp] = sink // expected-error{{cannot assign through subscript: 'kp' is a read-only key path}}
  writable[keyPath: kp] = sink // expected-error{{cannot assign through subscript: 'kp' is a read-only key path}}
  readonly[keyPath: wkp] = sink // expected-error{{cannot assign through subscript: 'readonly' is a 'let' constant}}
  writable[keyPath: wkp] = sink
  readonly[keyPath: rkp] = sink
  writable[keyPath: rkp] = sink
}

func testKeyPathSubscriptTuple(readonly: (Z,Z), writable: inout (Z,Z),
                               kp: KeyPath<(Z,Z), Int>,
                               wkp: WritableKeyPath<(Z,Z), Int>,
                               rkp: ReferenceWritableKeyPath<(Z,Z), Int>) {
  var sink: Int
  sink = readonly[keyPath: kp]
  sink = writable[keyPath: kp]
  sink = readonly[keyPath: wkp]
  sink = writable[keyPath: wkp]
  sink = readonly[keyPath: rkp]
  sink = writable[keyPath: rkp]

  readonly[keyPath: kp] = sink // expected-error{{cannot assign through subscript: 'kp' is a read-only key path}}
  writable[keyPath: kp] = sink // expected-error{{cannot assign through subscript: 'kp' is a read-only key path}}
  readonly[keyPath: wkp] = sink // expected-error{{cannot assign through subscript: 'readonly' is a 'let' constant}}
  writable[keyPath: wkp] = sink
  readonly[keyPath: rkp] = sink
  writable[keyPath: rkp] = sink
}

func testKeyPathSubscriptLValue(base: Z, kp: inout KeyPath<Z, Z>) {
  _ = base[keyPath: kp]
}

func testKeyPathSubscriptExistentialBase(concreteBase: inout B,
                                         existentialBase: inout P,
                                         kp: KeyPath<P, String>,
                                         wkp: WritableKeyPath<P, String>,
                                         rkp: ReferenceWritableKeyPath<P, String>,
                                         pkp: PartialKeyPath<P>,
                                         s: String) {
  _ = concreteBase[keyPath: kp]
  _ = concreteBase[keyPath: wkp]
  _ = concreteBase[keyPath: rkp]
  _ = concreteBase[keyPath: pkp]

  concreteBase[keyPath: kp] = s // expected-error {{cannot assign through subscript: 'kp' is a read-only key path}}
  concreteBase[keyPath: wkp] = s // expected-error {{key path with root type 'P' cannot be applied to a base of type 'B'}}
  concreteBase[keyPath: rkp] = s
  concreteBase[keyPath: pkp] = s // expected-error {{cannot assign through subscript: 'pkp' is a read-only key path}}

  _ = existentialBase[keyPath: kp]
  _ = existentialBase[keyPath: wkp]
  _ = existentialBase[keyPath: rkp]
  _ = existentialBase[keyPath: pkp]

  existentialBase[keyPath: kp] = s // expected-error {{cannot assign through subscript: 'kp' is a read-only key path}}
  existentialBase[keyPath: wkp] = s
  existentialBase[keyPath: rkp] = s
  existentialBase[keyPath: pkp] = s // expected-error {{cannot assign through subscript: 'pkp' is a read-only key path}}
}

struct AA {
  subscript(x: Int) -> Int { return x }
  subscript(labeled x: Int) -> Int { return x }
  var c: CC? = CC()
}

class CC {
  var i = 0
}

func testKeyPathOptional() {
  _ = \AA.c?.i
  _ = \AA.c!.i

  // SR-6198
  let path: KeyPath<CC,Int>! = \CC.i
  let cc = CC()
  _ = cc[keyPath: path]
}

func testLiteralInAnyContext() {
  let _: AnyKeyPath = \A.property
  let _: AnyObject = \A.property
  let _: Any = \A.property
  let _: Any? = \A.property
}

func testMoreGeneralContext<T, U>(_: KeyPath<T, U>, with: T.Type) {}

func testLiteralInMoreGeneralContext() {
  testMoreGeneralContext(\.property, with: A.self)
}

func testLabeledSubscript() {
  let _: KeyPath<AA, Int> = \AA.[labeled: 0]
  let _: KeyPath<AA, Int> = \.[labeled: 0]
  let k = \AA.[labeled: 0]

  // TODO: These ought to work without errors.
  let _ = \AA.[keyPath: k]
  // expected-error@-1 {{cannot convert value of type 'KeyPath<AA, Int>' to expected argument type 'Int'}}

  let _ = \AA.[keyPath: \AA.[labeled: 0]] // expected-error {{extraneous argument label 'keyPath:' in call}}
  // expected-error@-1 {{cannot convert value of type 'KeyPath<AA, Int>' to expected argument type 'Int'}}
}

func testInvalidKeyPathComponents() {
  let _ = \.{return 0} // expected-error* {{}}
}

class X {
  class var a: Int { return 1 }
  static var b = 2
}

func testStaticKeyPathComponent() {
  _ = \X.a // expected-error{{cannot refer to static member}}
  _ = \X.Type.a // expected-error{{cannot refer to static member}}
  _ = \X.b // expected-error{{cannot refer to static member}}
  _ = \X.Type.b // expected-error{{cannot refer to static member}}
}

class Bass: Hashable {
  static func ==(_: Bass, _: Bass) -> Bool { return false }
  func hash(into hasher: inout Hasher) {}
}

class Treble: Bass { }

struct BassSubscript {
  subscript(_: Bass) -> Int { fatalError() }
  subscript(_: @autoclosure () -> String) -> Int { fatalError() }
}

func testImplicitConversionInSubscriptIndex() {
  _ = \BassSubscript.[Treble()]
  _ = \BassSubscript.["hello"] // expected-error{{must be Hashable}}
}

// Crash in diagnostics + SR-11438
struct UnambiguousSubscript {
  subscript(sub: Sub) -> Int { get { } set { } }
  subscript(y y: Sub) -> Int { get { } set { } }
}

func useUnambiguousSubscript(_ sub: Sub) {
  let _: PartialKeyPath<UnambiguousSubscript> = \.[sub]
}

struct BothUnavailableSubscript {
  @available(*, unavailable)
  subscript(sub: Sub) -> Int { get { } set { } } // expected-note {{'subscript(_:)' has been explicitly marked unavailable here}}

  @available(*, unavailable)
  subscript(y y: Sub) -> Int { get { } set { } }
}

func useBothUnavailableSubscript(_ sub: Sub) {
  let _: PartialKeyPath<BothUnavailableSubscript> = \.[sub]
  // expected-error@-1 {{'subscript(_:)' is unavailable}}
}

// SR-6106
func sr6106() {
  class B {}
  class A {
    var b: B? = nil
  }
  class C {
    var a: A?
    func myFunc() {
      let _ = \C.a?.b
    }
  }
}

// SR-6744
func sr6744() {
    struct ABC {
        let value: Int
        func value(adding i: Int) -> Int { return value + i }
    }

    let abc = ABC(value: 0)
    func get<T>(for kp: KeyPath<ABC, T>) -> T {
        return abc[keyPath: kp]
    }
    _ = get(for: \.value)
}

func sr7380() {
  _ = ""[keyPath: \.count]
  _ = ""[keyPath: \String.count]

  let arr1 = [1]
  _ = arr1[keyPath: \.[0]]
  _ = arr1[keyPath: \[Int].[0]]

  let dic1 = [1:"s"]
  _ = dic1[keyPath: \.[1]]
  _ = dic1[keyPath: \[Int: String].[1]]

  var arr2 = [1]
  arr2[keyPath: \.[0]] = 2
  arr2[keyPath: \[Int].[0]] = 2

  var dic2 = [1:"s"]
  dic2[keyPath: \.[1]] = ""
  dic2[keyPath: \[Int: String].[1]] = ""

  _ = [""][keyPath: \.[0]]
  _ = [""][keyPath: \[String].[0]]

  _ = ["": ""][keyPath: \.["foo"]]
  _ = ["": ""][keyPath: \[String: String].["foo"]]

  class A {
    var a: String = ""
  }
  _ = A()[keyPath: \.a]
  _ = A()[keyPath: \A.a]
  A()[keyPath: \.a] = ""
  A()[keyPath: \A.a] = ""
}

struct VisibilityTesting {
  private(set) var x: Int
  fileprivate(set) var y: Int
  let z: Int

  // Key path exprs should not get special dispensation to write to lets
  // in init contexts
  init() {
    var xRef = \VisibilityTesting.x
    var yRef = \VisibilityTesting.y
    var zRef = \VisibilityTesting.z
    expect(&xRef,
      toHaveType: Exactly<WritableKeyPath<VisibilityTesting, Int>>.self)
    expect(&yRef,
      toHaveType: Exactly<WritableKeyPath<VisibilityTesting, Int>>.self)
    // Allow WritableKeyPath for Swift 3/4 only.
    expect(&zRef,
      toHaveType: Exactly<WritableKeyPath<VisibilityTesting, Int>>.self)
  }

  func inPrivateContext() {
    var xRef = \VisibilityTesting.x
    var yRef = \VisibilityTesting.y
    var zRef = \VisibilityTesting.z
    expect(&xRef,
      toHaveType: Exactly<WritableKeyPath<VisibilityTesting, Int>>.self)
    expect(&yRef,
      toHaveType: Exactly<WritableKeyPath<VisibilityTesting, Int>>.self)
    expect(&zRef,
      toHaveType: Exactly<KeyPath<VisibilityTesting, Int>>.self)
  }
}

struct VisibilityTesting2 {
  func inFilePrivateContext() {
    var xRef = \VisibilityTesting.x
    var yRef = \VisibilityTesting.y
    var zRef = \VisibilityTesting.z
    // Allow WritableKeyPath for Swift 3/4 only.
    expect(&xRef,
      toHaveType: Exactly<WritableKeyPath<VisibilityTesting, Int>>.self)
    expect(&yRef,
      toHaveType: Exactly<WritableKeyPath<VisibilityTesting, Int>>.self)
    expect(&zRef,
      toHaveType: Exactly<KeyPath<VisibilityTesting, Int>>.self)
  }
}

protocol PP {}
class Base : PP { var i: Int = 0 }
class Derived : Base {}

func testSubtypeKeypathClass(_ keyPath: ReferenceWritableKeyPath<Base, Int>) {
  testSubtypeKeypathClass(\Derived.i)
}

func testSubtypeKeypathProtocol(_ keyPath: ReferenceWritableKeyPath<PP, Int>) {
  testSubtypeKeypathProtocol(\Base.i)
  // expected-error@-1 {{cannot convert value of type 'ReferenceWritableKeyPath<Base, Int>' to expected argument type 'ReferenceWritableKeyPath<PP, Int>'}}
  // expected-note@-2 {{arguments to generic parameter 'Root' ('Base' and 'PP') are expected to be equal}}
}

// rdar://problem/32057712
struct Container {
  let base: Base? = Base()
}

var rdar32057712 = \Container.base?.i

var identity1 = \Container.self
var identity2: WritableKeyPath = \Container.self
var identity3: WritableKeyPath<Container, Container> = \Container.self
var identity4: WritableKeyPath<Container, Container> = \.self
var identity5: KeyPath = \Container.self
var identity6: KeyPath<Container, Container> = \Container.self
var identity7: KeyPath<Container, Container> = \.self
var identity8: PartialKeyPath = \Container.self
var identity9: PartialKeyPath<Container> = \Container.self
var identity10: PartialKeyPath<Container> = \.self
var identity11: AnyKeyPath = \Container.self
var identity12: (Container) -> Container = \Container.self
var identity13: (Container) -> Container = \.self

var interleavedIdentityComponents = \Container.self.base.self?.self.i.self

protocol P_With_Static_Members {
  static var x: Int { get }
  static var arr: [Int] { get }
}

func test_keypath_with_static_members(_ p: P_With_Static_Members) {
  let _ = p[keyPath: \.x]
  // expected-error@-1 {{key path cannot refer to static member 'x'}}
  let _: KeyPath<P_With_Static_Members, Int> = \.x
  // expected-error@-1 {{key path cannot refer to static member 'x'}}
  let _ = \P_With_Static_Members.arr.count
  // expected-error@-1 {{key path cannot refer to static member 'arr'}}
  let _ = p[keyPath: \.arr.count]
  // expected-error@-1 {{key path cannot refer to static member 'arr'}}

  struct S {
    static var foo: String = "Hello"
    var bar: Bar
  }

  struct Bar {
    static var baz: Int = 42
  }

  func foo(_ s: S) {
    let _ = \S.Type.foo
    // expected-error@-1 {{key path cannot refer to static member 'foo'}}
    let _ = s[keyPath: \.foo]
    // expected-error@-1 {{key path cannot refer to static member 'foo'}}
    let _: KeyPath<S, String> = \.foo
    // expected-error@-1 {{key path cannot refer to static member 'foo'}}
    let _ = \S.foo
    // expected-error@-1 {{key path cannot refer to static member 'foo'}}
    let _ = \S.bar.baz
    // expected-error@-1 {{key path cannot refer to static member 'baz'}}
    let _ = s[keyPath: \.bar.baz]
    // expected-error@-1 {{key path cannot refer to static member 'baz'}}
  }
}

func test_keypath_with_mutating_getter() {
  struct S {
    var foo: Int {
      mutating get { return 42 }
    }

    subscript(_: Int) -> [Int] {
      mutating get { return [] }
    }
  }

  _ = \S.foo
  // expected-error@-1 {{key path cannot refer to 'foo', which has a mutating getter}}
  let _: KeyPath<S, Int> = \.foo
  // expected-error@-1 {{key path cannot refer to 'foo', which has a mutating getter}}
  _ = \S.[0]
  // expected-error@-1 {{key path cannot refer to 'subscript(_:)', which has a mutating getter}}
  _ = \S.[0].count
  // expected-error@-1 {{key path cannot refer to 'subscript(_:)', which has a mutating getter}}

  func test_via_subscript(_ s: S) {
    _ = s[keyPath: \.foo]
    // expected-error@-1 {{key path cannot refer to 'foo', which has a mutating getter}}
    _ = s[keyPath: \.[0].count]
    // expected-error@-1 {{key path cannot refer to 'subscript(_:)', which has a mutating getter}}
  }
}

func test_keypath_with_method_refs() {
  struct S {
    func foo() -> Int { return 42 }
    static func bar() -> Int { return 0 }
  }

  let _: KeyPath<S, Int> = \.foo // expected-error {{key path cannot refer to instance method 'foo()'}}
  // expected-error@-1 {{key path value type '() -> Int' cannot be converted to contextual type 'Int'}}
  let _: KeyPath<S, Int> = \.bar // expected-error {{key path cannot refer to static member 'bar()'}}
  let _ = \S.Type.bar // expected-error {{key path cannot refer to static method 'bar()'}}

  struct A {
    func foo() -> B { return B() }
    static func faz() -> B { return B() }
  }

  struct B {
    var bar: Int = 42
  }

  let _: KeyPath<A, Int> = \.foo.bar // expected-error {{key path cannot refer to instance method 'foo()'}}
  let _: KeyPath<A, Int> = \.faz.bar // expected-error {{key path cannot refer to static member 'faz()'}}
  let _ = \A.foo.bar // expected-error {{key path cannot refer to instance method 'foo()'}}
  let _ = \A.Type.faz.bar // expected-error {{key path cannot refer to static method 'faz()'}}
}

// SR-12519: Compiler crash on invalid method reference in key path.
protocol Zonk {
  func wargle()
}
typealias Blatz = (gloop: String, zoop: Zonk?)

func sr12519(fleep: [Blatz]) {
  fleep.compactMap(\.zoop?.wargle) // expected-error {{key path cannot refer to instance method 'wargle()'}}
}

// SR-10467 - Argument type 'KeyPath<String, Int>' does not conform to expected type 'Any'
func test_keypath_in_any_context() {
  func foo(_: Any) {}
  foo(\String.count) // Ok
}

protocol PWithTypeAlias {
  typealias Key = WritableKeyPath<Self, Int?>
  static var fooKey: Key? { get }
  static var barKey: Key! { get }
  static var fazKey: Key?? { get }
  static var bazKey: Key?! { get }
}

func test_keypath_inference_with_optionals() {
  final class S : PWithTypeAlias {
    static var fooKey: Key? { return \.foo }
    static var barKey: Key! { return \.foo }
    static var fazKey: Key?? { return \.foo }
    static var bazKey: Key?! { return \.foo }

    var foo: Int? = nil
  }
}

func sr11562() {
  struct S1 {
    subscript(x x: Int) -> Int { x }
  }

  _ = \S1.[5] // expected-error {{missing argument label 'x:' in call}} {{12-12=x: }}

  struct S2 {
    subscript(x x: Int) -> Int { x } // expected-note {{incorrect labels for candidate (have: '(_:)', expected: '(x:)')}}
    subscript(y y: Int) -> Int { y } // expected-note {{incorrect labels for candidate (have: '(_:)', expected: '(y:)')}}
  }

  _ = \S2.[5] // expected-error {{no exact matches in call to subscript}}

  struct S3 {
    subscript(x x: Int, y y: Int) -> Int { x }
  }

  _ = \S3.[y: 5, x: 5] // expected-error {{argument 'x' must precede argument 'y'}}

  struct S4 {
    subscript(x: (Int, Int)) -> Int { x.0 }
  }

  _ = \S4.[1, 4] // expected-error {{subscript expects a single parameter of type '(Int, Int)'}} {{12-12=(}} {{16-16=)}}
  // expected-error@-1 {{subscript index of type '(Int, Int)' in a key path must be Hashable}}
}

// SR-12290: Ban keypaths with contextual root and without a leading dot
struct SR_12290 {
  let property: [Int] = []
  let kp1: KeyPath<SR_12290, Int> = \property.count // expected-error {{a Swift key path with contextual root must begin with a leading dot}}{{38-38=.}}
  let kp2: KeyPath<SR_12290, Int> = \.property.count // Ok
  let kp3: KeyPath<SR_12290, Int> = \SR_12290.property.count // Ok

  func foo1(_: KeyPath<SR_12290, Int> = \property.count) {} // expected-error {{a Swift key path with contextual root must begin with a leading dot}}{{42-42=.}}
  func foo2(_: KeyPath<SR_12290, Int> = \.property.count) {} // Ok
  func foo3(_: KeyPath<SR_12290, Int> = \SR_12290.property.count) {} // Ok

  func foo4<T>(_: KeyPath<SR_12290, T>) {}
  func useFoo4() {
    foo4(\property.count) // expected-error {{a Swift key path with contextual root must begin with a leading dot}}{{11-11=.}}
    foo4(\.property.count) // Ok
    foo4(\SR_12290.property.count) // Ok
  }
}

func testKeyPathHole() {
  _ = \.x // expected-error {{cannot infer key path type from context; consider explicitly specifying a root type}} {{8-8=<#Root#>}}
  _ = \.x.y // expected-error {{cannot infer key path type from context; consider explicitly specifying a root type}} {{8-8=<#Root#>}}

  let _ : AnyKeyPath = \.x 
  // expected-error@-1 {{'AnyKeyPath' does not provide enough context for root type to be inferred; consider explicitly specifying a root type}} {{25-25=<#Root#>}}
  let _ : AnyKeyPath = \.x.y
  // expected-error@-1 {{'AnyKeyPath' does not provide enough context for root type to be inferred; consider explicitly specifying a root type}} {{25-25=<#Root#>}}

  func f(_ i: Int) {}
  f(\.x) // expected-error {{cannot infer key path type from context; consider explicitly specifying a root type}} {{6-6=<#Root#>}}
  f(\.x.y) // expected-error {{cannot infer key path type from context; consider explicitly specifying a root type}} {{6-6=<#Root#>}}

  func provideValueButNotRoot<T>(_ fn: (T) -> String) {} 
  provideValueButNotRoot(\.x) // expected-error {{cannot infer key path type from context; consider explicitly specifying a root type}}
  provideValueButNotRoot(\.x.y) // expected-error {{cannot infer key path type from context; consider explicitly specifying a root type}}
  provideValueButNotRoot(\String.foo) // expected-error {{value of type 'String' has no member 'foo'}}

  func provideKPValueButNotRoot<T>(_ kp: KeyPath<T, String>) {} // expected-note {{in call to function 'provideKPValueButNotRoot'}}
  provideKPValueButNotRoot(\.x) // expected-error {{cannot infer key path type from context; consider explicitly specifying a root type}}
  provideKPValueButNotRoot(\.x.y) // expected-error {{cannot infer key path type from context; consider explicitly specifying a root type}}

  provideKPValueButNotRoot(\String.foo)
  // expected-error@-1 {{value of type 'String' has no member 'foo'}}
  // expected-error@-2 {{generic parameter 'T' could not be inferred}}
}

func testMissingMember() {
  let _: KeyPath<String, String> = \.foo // expected-error {{value of type 'String' has no member 'foo'}}
  let _: KeyPath<String, String> = \.foo.bar // expected-error {{value of type 'String' has no member 'foo'}}

  let _: PartialKeyPath<String> = \.foo // expected-error {{value of type 'String' has no member 'foo'}}
  let _: PartialKeyPath<String> = \.foo.bar // expected-error {{value of type 'String' has no member 'foo'}}

  _ = \String.x.y // expected-error {{value of type 'String' has no member 'x'}}
}

// SR-5688
struct  SR5688_A {
    var b: SR5688_B?
}

struct SR5688_AA {
    var b: SR5688_B
}

struct SR5688_B {
    var m: Int
    var c: SR5688_C?
}

struct SR5688_C {
    var d: Int
}

struct SR5688_S {
  subscript(_ x: Int) -> String? { "" }
}

struct SR5688_O {
  struct Nested {
    var foo = ""
  }
}

func SR5688_KP(_ kp: KeyPath<String?, Int>) {}

func testMemberAccessOnOptionalKeyPathComponent() {

  _ = \SR5688_A.b.m
  // expected-error@-1 {{value of optional type 'SR5688_B?' must be unwrapped to refer to member 'm' of wrapped base type 'SR5688_B'}}
  // expected-note@-2 {{chain the optional using '?' to access member 'm' only for non-'nil' base values}} {{18-18=?}}
  // expected-note@-3 {{force-unwrap using '!' to abort execution if the optional value contains 'nil'}} {{18-18=!}}

  _ = \SR5688_A.b.c.d 
  // expected-error@-1 {{value of optional type 'SR5688_B?' must be unwrapped to refer to member 'c' of wrapped base type 'SR5688_B'}} 
  // expected-note@-2 {{chain the optional using '?' to access member 'c' only for non-'nil' base values}} {{18-18=?}}
  // expected-error@-3 {{value of optional type 'SR5688_C?' must be unwrapped to refer to member 'd' of wrapped base type 'SR5688_C'}} 
  // expected-note@-4 {{chain the optional using '?' to access member 'd' only for non-'nil' base values}} {{20-20=?}}
  // expected-note@-5 {{force-unwrap using '!' to abort execution if the optional value contains 'nil'}} {{20-20=!}}
  _ = \SR5688_A.b?.c.d 
  // expected-error@-1 {{value of optional type 'SR5688_C?' must be unwrapped to refer to member 'd' of wrapped base type 'SR5688_C'}}
  // expected-note@-2 {{chain the optional using '?' to access member 'd' only for non-'nil' base values}} {{21-21=?}}

  _ = \SR5688_AA.b.c.d
  // expected-error@-1 {{value of optional type 'SR5688_C?' must be unwrapped to refer to member 'd' of wrapped base type 'SR5688_C'}}
  // expected-note@-2 {{chain the optional using '?' to access member 'd' only for non-'nil' base values}} {{21-21=?}}
  // expected-note@-3 {{force-unwrap using '!' to abort execution if the optional value contains 'nil'}} {{21-21=!}}

  \String?.count 
  // expected-error@-1 {{value of optional type 'String?' must be unwrapped to refer to member 'count' of wrapped base type 'String'}}
  // expected-note@-2 {{use unwrapped type 'String' as key path root}} {{4-11=String}}
  
  \Optional<String>.count 
  // expected-error@-1 {{value of optional type 'Optional<String>' must be unwrapped to refer to member 'count' of wrapped base type 'String'}}
  // expected-note@-2 {{use unwrapped type 'String' as key path root}} {{4-20=String}}

  \SR5688_S.[5].count 
  // expected-error@-1 {{value of optional type 'String?' must be unwrapped to refer to member 'count' of wrapped base type 'String'}}
  // expected-note@-2 {{chain the optional using '?' to access member 'count' only for non-'nil' base values}}{{16-16=?}}
  // expected-note@-3 {{force-unwrap using '!' to abort execution if the optional value contains 'nil'}}{{16-16=!}}


  \SR5688_O.Nested?.foo.count
  // expected-error@-1 {{value of optional type 'SR5688_O.Nested?' must be unwrapped to refer to member 'foo' of wrapped base type 'SR5688_O.Nested'}}
  // expected-note@-2 {{use unwrapped type 'SR5688_O.Nested' as key path root}}{{4-20=SR5688_O.Nested}}
  
  \(Int, Int)?.0
  // expected-error@-1 {{value of optional type '(Int, Int)?' must be unwrapped to refer to member '0' of wrapped base type '(Int, Int)'}}
  // expected-note@-2 {{use unwrapped type '(Int, Int)' as key path root}}{{4-15=(Int, Int)}}
  
  SR5688_KP(\.count) // expected-error {{key path root inferred as optional type 'String?' must be unwrapped to refer to member 'count' of unwrapped type 'String'}}
  // expected-note@-1 {{chain the optional using '?.' to access unwrapped type member 'count'}} {{15-15=?.}}
  // expected-note@-2 {{unwrap the optional using '!.' to access unwrapped type member 'count'}} {{15-15=!.}}
  let _ : KeyPath<String?, Int> = \.count // expected-error {{key path root inferred as optional type 'String?' must be unwrapped to refer to member 'count' of unwrapped type 'String'}}
  // expected-note@-1 {{chain the optional using '?.' to access unwrapped type member 'count'}} {{37-37=?.}}
  // expected-note@-2 {{unwrap the optional using '!.' to access unwrapped type member 'count'}} {{37-37=!.}}

  let _ : KeyPath<String?, Int> = \.utf8.count 
  // expected-error@-1 {{key path root inferred as optional type 'String?' must be unwrapped to refer to member 'utf8' of unwrapped type 'String'}}
  // expected-note@-2 {{chain the optional using '?.' to access unwrapped type member 'utf8'}} {{37-37=?.}}
  // expected-note@-3 {{unwrap the optional using '!.' to access unwrapped type member 'utf8'}} {{37-37=!.}}
}

func testSyntaxErrors() {
  _ = \.  ; // expected-error{{expected member name following '.'}}
  _ = \.a ;
  _ = \[a ;
  _ = \[a];
  _ = \?  ;
  _ = \!  ;
  _ = \.  ; // expected-error{{expected member name following '.'}}
  _ = \.a ;
  _ = \[a ;
  _ = \[a,;
  _ = \[a:;
  _ = \[a];
  _ = \.a?;
  _ = \.a!;
  _ = \A     ;
  _ = \A,    ;
  _ = \A<    ;
  _ = \A.  ; // expected-error{{expected member name following '.'}}
  _ = \A.a ;
  _ = \A[a ;
  _ = \A[a];
  _ = \A?  ;
  _ = \A!  ;
  _ = \A.  ; // expected-error{{expected member name following '.'}}
  _ = \A.a ;
  _ = \A[a ;
  _ = \A[a,;
  _ = \A[a:;
  _ = \A[a];
  _ = \A.a?;
  _ = \A.a!;
}

// SR-14644
func sr14644() {
  _ = \Int.byteSwapped.signum() // expected-error {{invalid component of Swift key path}}
  _ = \Int.byteSwapped.init() // expected-error {{invalid component of Swift key path}}
  _ = \Int // expected-error {{key path must have at least one component}}
  _ = \Int? // expected-error {{key path must have at least one component}}
  _ = \Int. // expected-error {{invalid component of Swift key path}}
  // expected-error@-1 {{expected member name following '.'}}
}

// SR-13364 - keypath missing optional crashes compiler: "Inactive constraints left over?"
func sr13364() {
  let _: KeyPath<String?, Int?> = \.utf8.count // expected-error {{no exact matches in reference to property 'count'}}
  // expected-note@-1 {{found candidate with type 'Int'}}
}

// rdar://74711236 - crash due to incorrect member access in key path
func rdar74711236() {
  struct S {
    var arr: [V] = []
  }

  struct V : Equatable {
  }

  enum Type {
  case store
  }

  struct Context {
    func supported() -> [Type] {
      return []
    }
  }

  func test(context: Context?) {
    var s = S()

    s.arr = {
        if let type = context?.store { // expected-error {{value of type 'Context' has no member 'store'}}
        // `isSupported` should be an invalid declaration to trigger a crash in `map(\.option)`
        let isSupported = context!.supported().contains(type)
        return (isSupported ? [type] : []).map(\.option)
        // expected-error@-1 {{value of type 'Any' has no member 'option'}}
        // expected-note@-2 {{cast 'Any' to 'AnyObject' or use 'as!' to force downcast to a more specific type to access members}}
      }
      return []
    }()
  }
}

extension String {
  var filterOut : (Self) throws -> Bool {
    { $0.contains("a") }
  }
}

func test_kp_as_function_mismatch() {
  let a : [String] = [ "asd", "bcd", "def" ]

  let _ : (String) ->  Bool = \.filterOut // expected-error{{key path value type '(String) throws -> Bool' cannot be converted to contextual type 'Bool'}}
  _ = a.filter(\.filterOut) // expected-error{{key path value type '(String) throws -> Bool' cannot be converted to contextual type 'Bool'}}
  let _ : (String) ->  Bool = \String.filterOut // expected-error{{key path value type '(String) throws -> Bool' cannot be converted to contextual type 'Bool'}}
  _ = a.filter(\String.filterOut) // expected-error{{key path value type '(String) throws -> Bool' cannot be converted to contextual type 'Bool'}}

}

func test_partial_keypath_inference() {
  // rdar://problem/34144827

  struct S { var i: Int = 0 }
  enum E { case A(pkp: PartialKeyPath<S>) }

  _ = E.A(pkp: \.i) // Ok

  // rdar://problem/36472188

  class ThePath {
    var isWinding:Bool?
  }

  func walk<T>(aPath: T, forKey: PartialKeyPath<T>) {}
  func walkThePath(aPath: ThePath, forKey: PartialKeyPath<ThePath>) {}

  func test(path: ThePath) {
    walkThePath(aPath: path, forKey: \.isWinding) // Ok
    walk(aPath: path, forKey: \.isWinding) // Ok
  }
}

// SR-14499
struct SR14499_A { }
struct SR14499_B { }

func sr14499() {
  func reproduceA() -> [(SR14499_A, SR14499_B)] {
    [
      (true, .init(), SR14499_B.init()) // expected-error {{cannot infer contextual base in reference to member 'init'}}
    ]
    .filter(\.0) // expected-error {{value of type 'Any' has no member '0'}}
    // expected-note@-1 {{cast 'Any' to 'AnyObject' or use 'as!' to force downcast to a more specific type to access members}}
    .prefix(3)
    .map { ($0.1, $0.2) } // expected-error {{value of type 'Any' has no member '1'}} expected-error{{value of type 'Any' has no member '2'}}
    // expected-note@-1 2 {{cast 'Any' to 'AnyObject' or use 'as!' to force downcast to a more specific type to access members}}
  }

  func reproduceB() -> [(SR14499_A, SR14499_B)] {
    [
      (true, SR14499_A.init(), .init()) // expected-error {{cannot infer contextual base in reference to member 'init'}}
    ]
    .filter(\.0) // expected-error {{value of type 'Any' has no member '0'}}
    // expected-note@-1 {{cast 'Any' to 'AnyObject' or use 'as!' to force downcast to a more specific type to access members}}
    .prefix(3)
    .map { ($0.1, $0.2) } // expected-error {{value of type 'Any' has no member '1'}} expected-error{{value of type 'Any' has no member '2'}}
    // expected-note@-1 2 {{cast 'Any' to 'AnyObject' or use 'as!' to force downcast to a more specific type to access members}}
  }

  func reproduceC() -> [(SR14499_A, SR14499_B)] {
    [
      (true, .init(), .init()) // expected-error 2 {{cannot infer contextual base in reference to member 'init'}}
    ]
    .filter(\.0) // expected-error {{value of type 'Any' has no member '0'}}
    // expected-note@-1 {{cast 'Any' to 'AnyObject' or use 'as!' to force downcast to a more specific type to access members}}
    .prefix(3)
    .map { ($0.1, $0.2) } // expected-error {{value of type 'Any' has no member '1'}} expected-error{{value of type 'Any' has no member '2'}}
    // expected-note@-1 2 {{cast 'Any' to 'AnyObject' or use 'as!' to force downcast to a more specific type to access members}}
  }
}
