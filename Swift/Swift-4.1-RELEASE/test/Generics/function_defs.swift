// RUN: %target-typecheck-verify-swift

//===----------------------------------------------------------------------===//
// Type-check function definitions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Basic type checking
//===----------------------------------------------------------------------===//
protocol EqualComparable {
  func isEqual(_ other: Self) -> Bool
}

func doCompare<T : EqualComparable, U : EqualComparable>(_ t1: T, t2: T, u: U) -> Bool {
  var b1 = t1.isEqual(t2)
  if b1 {
    return true
  }

  return t1.isEqual(u) // expected-error {{cannot invoke 'isEqual' with an argument list of type '(U)'}} expected-note {{expected an argument list of type '(T)'}}
}

protocol MethodLessComparable {
  func isLess(_ other: Self) -> Bool
}

func min<T : MethodLessComparable>(_ x: T, y: T) -> T {
  if (y.isLess(x)) { return y }
  return x
}

//===----------------------------------------------------------------------===//
// Interaction with existential types
//===----------------------------------------------------------------------===//

func existential<T : EqualComparable, U : EqualComparable>(_ t1: T, t2: T, u: U) {
  var eqComp : EqualComparable = t1 // expected-error{{protocol 'EqualComparable' can only be used as a generic constraint}}
  eqComp = u
  if t1.isEqual(eqComp) {} // expected-error{{cannot invoke 'isEqual' with an argument list of type '(EqualComparable)'}}
  // expected-note @-1 {{expected an argument list of type '(T)'}}
  if eqComp.isEqual(t2) {} // expected-error{{member 'isEqual' cannot be used on value of protocol type 'EqualComparable'; use a generic constraint instead}}
}

protocol OtherEqualComparable {
  func isEqual(_ other: Self) -> Bool
}

func otherExistential<T : EqualComparable>(_ t1: T) {
  var otherEqComp : OtherEqualComparable = t1 // expected-error{{value of type 'T' does not conform to specified type 'OtherEqualComparable'}}
  otherEqComp = t1 // expected-error{{value of type 'T' does not conform to 'OtherEqualComparable' in assignment}}
  _ = otherEqComp
  
  var otherEqComp2 : OtherEqualComparable // expected-error{{protocol 'OtherEqualComparable' can only be used as a generic constraint}}
  otherEqComp2 = t1 // expected-error{{value of type 'T' does not conform to 'OtherEqualComparable' in assignment}}
  _ = otherEqComp2

  _ = t1 as EqualComparable & OtherEqualComparable // expected-error{{'T' is not convertible to 'EqualComparable & OtherEqualComparable'; did you mean to use 'as!' to force downcast?}} {{10-12=as!}} expected-error{{protocol 'OtherEqualComparable' can only be used as a generic constraint}} expected-error{{protocol 'EqualComparable' can only be used as a generic constraint}}
}

protocol Runcible {
  func runce<A>(_ x: A)
  func spoon(_ x: Self)
}

func testRuncible(_ x: Runcible) { // expected-error{{protocol 'Runcible' can only be used as a generic constraint}}
  x.runce(5)
}

//===----------------------------------------------------------------------===//
// Overloading
//===----------------------------------------------------------------------===//

protocol Overload {
  associatedtype A
  associatedtype B
  func getA() -> A
  func getB() -> B
  func f1(_: A) -> A
  func f1(_: B) -> B
  func f2(_: Int) -> A // expected-note{{found this candidate}}
  func f2(_: Int) -> B // expected-note{{found this candidate}}
  func f3(_: Int) -> Int // expected-note {{found this candidate}}
  func f3(_: Float) -> Float // expected-note {{found this candidate}}
  func f3(_: Self) -> Self // expected-note {{found this candidate}}

  var prop : Self { get }
}

func testOverload<Ovl : Overload, OtherOvl : Overload>(_ ovl: Ovl, ovl2: Ovl,
                                                       other: OtherOvl) {
  var a = ovl.getA()
  var b = ovl.getB()

  // Overloading based on arguments
  _ = ovl.f1(a)
  a = ovl.f1(a)
  _ = ovl.f1(b)
  b = ovl.f1(b)

  // Overloading based on return type
  a = ovl.f2(17)
  b = ovl.f2(17)
  ovl.f2(17) // expected-error{{ambiguous use of 'f2'}}

  // Check associated types from different objects/different types.
  a = ovl2.f2(17)
  a = ovl2.f1(a)

  other.f1(a) // expected-error{{cannot invoke 'f1' with an argument list of type '(Ovl.A)'}}
  // expected-note @-1 {{overloads for 'f1' exist with these partially matching parameter lists: (Self.A), (Self.B)}}
                                                        
  // Overloading based on context
  var f3i : (Int) -> Int = ovl.f3
  var f3f : (Float) -> Float = ovl.f3
  var f3ovl_1 : (Ovl) -> Ovl = ovl.f3
  var f3ovl_2 : (Ovl) -> Ovl = ovl2.f3
  var f3ovl_3 : (Ovl) -> Ovl = other.f3 // expected-error{{ambiguous reference to member 'f3'}}

  var f3i_unbound : (Ovl) -> (Int) -> Int = Ovl.f3
  var f3f_unbound : (Ovl) -> (Float) -> Float = Ovl.f3
  var f3f_unbound2 : (OtherOvl) -> (Float) -> Float = OtherOvl.f3
  var f3ovl_unbound_1 : (Ovl) -> (Ovl) -> Ovl = Ovl.f3
  var f3ovl_unbound_2 : (OtherOvl) -> (OtherOvl) -> OtherOvl = OtherOvl.f3
}

//===----------------------------------------------------------------------===//
// Subscripting
//===----------------------------------------------------------------------===//
protocol Subscriptable {
  associatedtype Index
  associatedtype Value

  func getIndex() -> Index
  func getValue() -> Value

  subscript (index : Index) -> Value { get set }
}

protocol IntSubscriptable {
  associatedtype ElementType

  func getElement() -> ElementType

  subscript (index : Int) -> ElementType { get  }
}

func subscripting<T : Subscriptable & IntSubscriptable>(_ t: T) {
  var index = t.getIndex()
  var value = t.getValue()
  var element = t.getElement()

  value = t[index]
  t[index] = value // expected-error{{cannot assign through subscript: 't' is a 'let' constant}}
  element = t[17]
  t[42] = element // expected-error{{cannot assign through subscript: subscript is get-only}}

  // Suggests the Int form because we prefer concrete matches to generic matches in diagnosis.
  t[value] = 17 // expected-error{{cannot convert value of type 'T.Value' to expected argument type 'Int'}}
}

//===----------------------------------------------------------------------===//
// Static functions
//===----------------------------------------------------------------------===//
protocol StaticEq {
  static func isEqual(_ x: Self, y: Self) -> Bool
}

func staticEqCheck<T : StaticEq, U : StaticEq>(_ t: T, u: U) {
  if t.isEqual(t, t) { return } // expected-error{{static member 'isEqual' cannot be used on instance of type 'T'}}

  if T.isEqual(t, y: t) { return }
  if U.isEqual(u, y: u) { return }

  T.isEqual(t, y: u) // expected-error{{cannot invoke 'isEqual' with an argument list of type '(T, y: U)'}} expected-note {{expected an argument list of type '(T, y: T)'}}
}

//===----------------------------------------------------------------------===//
// Operators
//===----------------------------------------------------------------------===//
protocol Ordered {
  static func <(lhs: Self, rhs: Self) -> Bool
}

func testOrdered<T : Ordered>(_ x: T, y: Int) {
  if y < 100 || 500 < y { return }
  if x < x { return }
}

//===----------------------------------------------------------------------===//
// Requires clauses
//===----------------------------------------------------------------------===//
func conformanceViaRequires<T>(_ t1: T, t2: T) -> Bool
    where T : EqualComparable, T : MethodLessComparable {
  let b1 = t1.isEqual(t2)
  if b1 || t1.isLess(t2) {
    return true
  }
}

protocol GeneratesAnElement {
  associatedtype Element : EqualComparable
  func makeIterator() -> Element
}

protocol AcceptsAnElement {
  associatedtype Element : MethodLessComparable
  func accept(_ e : Element)
}

func impliedSameType<T : GeneratesAnElement>(_ t: T)
    where T : AcceptsAnElement {
  t.accept(t.makeIterator())
  let e = t.makeIterator(), e2 = t.makeIterator()
  if e.isEqual(e2) || e.isLess(e2) {
    return
  }
}

protocol GeneratesAssoc1 {
  associatedtype Assoc1 : EqualComparable
  func get() -> Assoc1
}

protocol GeneratesAssoc2 {
  associatedtype Assoc2 : MethodLessComparable
  func get() -> Assoc2
}

func simpleSameType<T : GeneratesAssoc1, U : GeneratesAssoc2>
  (_ t: T, u: U) -> Bool
   where T.Assoc1 == U.Assoc2 {
  return t.get().isEqual(u.get()) || u.get().isLess(t.get())
}

protocol GeneratesMetaAssoc1 {
  associatedtype MetaAssoc1 : GeneratesAnElement
  func get() -> MetaAssoc1
}

protocol GeneratesMetaAssoc2 {
  associatedtype MetaAssoc2 : AcceptsAnElement
  func get() -> MetaAssoc2
}

func recursiveSameType
       <T : GeneratesMetaAssoc1, U : GeneratesMetaAssoc2, V : GeneratesAssoc1>
       (_ t: T, u: U, v: V)
  where T.MetaAssoc1 == V.Assoc1, V.Assoc1 == U.MetaAssoc2
{
  t.get().accept(t.get().makeIterator())
  let e = t.get().makeIterator(), e2 = t.get().makeIterator()
  if e.isEqual(e2) || e.isLess(e2) {
    return
  }
}

// <rdar://problem/13985164>
protocol P1 {
  associatedtype Element
}

protocol P2 {
  associatedtype AssocP1 : P1
  func getAssocP1() -> AssocP1
}

func beginsWith2<E0: P1, E1: P1>(_ e0: E0, _ e1: E1) -> Bool
where E0.Element == E1.Element,
      E0.Element : EqualComparable
{
}

func beginsWith3<S0: P2, S1: P2>(_ seq1: S0, _ seq2: S1) -> Bool
  where S0.AssocP1.Element == S1.AssocP1.Element,
        S1.AssocP1.Element : EqualComparable {
  return beginsWith2(seq1.getAssocP1(), seq2.getAssocP1())
}


// FIXME: Test same-type constraints that try to equate things we
// don't want to equate, e.g., T == U.

//===----------------------------------------------------------------------===//
// Bogus requirements
//===----------------------------------------------------------------------===//
func nonTypeReq<T>(_: T) where T : Wibble {} // expected-error{{use of undeclared type 'Wibble'}}
func badProtocolReq<T>(_: T) where T : Int {} // expected-error{{type 'T' constrained to non-protocol, non-class type 'Int'}}

func nonTypeSameType<T>(_: T) where T == Wibble {} // expected-error{{use of undeclared type 'Wibble'}}
func nonTypeSameType2<T>(_: T) where Wibble == T {} // expected-error{{use of undeclared type 'Wibble'}}
func sameTypeEq<T>(_: T) where T = T {} // expected-error{{use '==' for same-type requirements rather than '='}} {{34-35===}}
// expected-warning@-1{{redundant same-type constraint 'T' == 'T'}}

func badTypeConformance1<T>(_: T) where Int : EqualComparable {} // expected-error{{type 'Int' in conformance requirement does not refer to a generic parameter or associated type}}

func badTypeConformance2<T>(_: T) where T.Blarg : EqualComparable { } // expected-error{{'Blarg' is not a member type of 'T'}}

func badSameType<T, U : GeneratesAnElement, V>(_ : T, _ : U)
  where T == U.Element, U.Element == V {} // expected-error{{same-type requirement makes generic parameters 'T' and 'V' equivalent}}
