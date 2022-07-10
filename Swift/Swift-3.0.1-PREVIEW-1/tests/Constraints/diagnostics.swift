// RUN: %target-parse-verify-swift

protocol P {
  associatedtype SomeType
}

protocol P2 { 
  func wonka()
}

extension Int : P {
  typealias SomeType = Int
}

extension Double : P {
  typealias SomeType = Double
}

func f0(_ x: Int, 
        _ y: Float) { }

func f1(_: @escaping (Int, Float) -> Int) { }

func f2(_: (_: (Int) -> Int)) -> Int {}

func f3(_: @escaping (_: @escaping (Int) -> Float) -> Int) {}

func f4(_ x: Int) -> Int { }

func f5<T : P2>(_ : T) { }

func f6<T : P, U : P>(_ t: T, _ u: U) where T.SomeType == U.SomeType {}

var i : Int
var d : Double

// Check the various forms of diagnostics the type checker can emit.

// Tuple size mismatch.
f1(
   f4 // expected-error {{cannot convert value of type '(Int) -> Int' to expected argument type '(Int, Float) -> Int'}}
   ) 

// Tuple element unused.
f0(i, i,
   i) // expected-error{{extra argument in call}}


// Position mismatch
f5(f4)  // expected-error {{argument type '(Int) -> Int' does not conform to expected type 'P2'}}

// Tuple element not convertible.
f0(i,
   d  // expected-error {{cannot convert value of type 'Double' to expected argument type 'Float'}}
   )

// Function result not a subtype.
f1(
   f0 // expected-error {{cannot convert value of type '(Int, Float) -> ()' to expected argument type '(Int, Float) -> Int'}}
   )

f3(
   f2 // expected-error {{cannot convert value of type '((@escaping (Int) -> Int)) -> Int' to expected argument type '(@escaping (Int) -> Float) -> Int'}}
   )

f4(i, d) // expected-error {{extra argument in call}}

// Missing member.
i.wobble() // expected-error{{value of type 'Int' has no member 'wobble'}}

// Generic member does not conform.
extension Int {
  func wibble<T: P2>(_ x: T, _ y: T) -> T { return x }
  func wubble<T>(_ x: (Int) -> T) -> T { return x(self) }
}
i.wibble(3, 4) // expected-error {{argument type 'Int' does not conform to expected type 'P2'}}

// Generic member args correct, but return type doesn't match.
struct A : P2 {
  func wonka() {}
}
let a = A()
for j in i.wibble(a, a) { // expected-error {{type 'A' does not conform to protocol 'Sequence'}}
}

// Generic as part of function/tuple types
func f6<T:P2>(_ g: (Void) -> T) -> (c: Int, i: T) {
  return (c: 0, i: g())
}

func f7() -> (c: Int, v: A) {
  let g: (Void) -> A = { return A() }
  return f6(g) // expected-error {{cannot convert return expression of type '(c: Int, i: A)' to return type '(c: Int, v: A)'}}
}

func f8<T:P2>(_ n: T, _ f: @escaping (T) -> T) {}
f8(3, f4) // expected-error {{in argument type '(Int) -> Int', 'Int' does not conform to expected type 'P2'}}
typealias Tup = (Int, Double)
func f9(_ x: Tup) -> Tup { return x }
f8((1,2.0), f9) // expected-error {{in argument type '(Tup) -> Tup', 'Tup' (aka '(Int, Double)') does not conform to expected type 'P2'}}

// <rdar://problem/19658691> QoI: Incorrect diagnostic for calling nonexistent members on literals
1.doesntExist(0)  // expected-error {{value of type 'Int' has no member 'doesntExist'}}
[1, 2, 3].doesntExist(0)  // expected-error {{value of type '[Int]' has no member 'doesntExist'}}
"awfawf".doesntExist(0)   // expected-error {{value of type 'String' has no member 'doesntExist'}}

// Does not conform to protocol.
f5(i)  // expected-error {{argument type 'Int' does not conform to expected type 'P2'}}

// Make sure we don't leave open existentials when diagnosing.
// <rdar://problem/20598568>
func pancakes(_ p: P2) {
  f4(p.wonka) // expected-error{{cannot convert value of type '() -> ()' to expected argument type 'Int'}}
  f4(p.wonka()) // expected-error{{cannot convert value of type '()' to expected argument type 'Int'}}
}

protocol Shoes {
  static func select(_ subject: Shoes) -> Self
}

// Here the opaque value has type (metatype_type (archetype_type ... ))
func f(_ x: Shoes, asType t: Shoes.Type) {
  return t.select(x) // expected-error{{unexpected non-void return value in void function}}
}

precedencegroup Starry {
  associativity: left
  higherThan: MultiplicationPrecedence
}

infix operator **** : Starry

func ****(_: Int, _: String) { }
i **** i // expected-error{{cannot convert value of type 'Int' to expected argument type 'String'}}

infix operator ***~ : Starry

func ***~(_: Int, _: String) { }
i ***~ i // expected-error{{cannot convert value of type 'Int' to expected argument type 'String'}}

@available(*, unavailable, message: "call the 'map()' method on the sequence")
public func myMap<C : Collection, T>(
  _ source: C, _ transform: (C.Iterator.Element) -> T
) -> [T] {
  fatalError("unavailable function can't be called")
}

@available(*, unavailable, message: "call the 'map()' method on the optional value")
public func myMap<T, U>(_ x: T?, _ f: (T) -> U) -> U? {
  fatalError("unavailable function can't be called")
}

// <rdar://problem/20142523>
// FIXME: poor diagnostic, to be fixed in 20142462. For now, we just want to
// make sure that it doesn't crash.
func rdar20142523() {
  myMap(0..<10, { x in // expected-error{{ambiguous reference to member '..<'}}
    ()
    return x
  })
}

// <rdar://problem/21080030> Bad diagnostic for invalid method call in boolean expression: (_, ExpressibleByIntegerLiteral)' is not convertible to 'ExpressibleByIntegerLiteral
func rdar21080030() {
  var s = "Hello"
  if s.characters.count() == 0 {} // expected-error{{cannot call value of non-function type 'String.CharacterView.IndexDistance'}}{{24-26=}}
}

// <rdar://problem/21248136> QoI: problem with return type inference mis-diagnosed as invalid arguments
func r21248136<T>() -> T { preconditionFailure() } // expected-note 2 {{in call to function 'r21248136'}}

r21248136()            // expected-error {{generic parameter 'T' could not be inferred}}
let _ = r21248136()    // expected-error {{generic parameter 'T' could not be inferred}}


// <rdar://problem/16375647> QoI: Uncallable funcs should be compile time errors
func perform<T>() {}  // expected-error {{generic parameter 'T' is not used in function signature}}

// <rdar://problem/17080659> Error Message QOI - wrong return type in an overload
func recArea(_ h: Int, w : Int) {
  return h * w  // expected-error {{no '*' candidates produce the expected contextual result type '()'}}
  // expected-note @-1 {{overloads for '*' exist with these result types: UInt8, Int8, UInt16, Int16, UInt32, Int32, UInt64, Int64, UInt, Int, Float, Double}}
}

// <rdar://problem/17224804> QoI: Error In Ternary Condition is Wrong
func r17224804(_ monthNumber : Int) {
  // expected-error @+2 {{binary operator '+' cannot be applied to operands of type 'String' and 'Int'}}
  // expected-note @+1 {{overloads for '+' exist with these partially matching parameter lists: (Int, Int), (String, String), (UnsafeMutablePointer<Pointee>, Int), (UnsafePointer<Pointee>, Int)}}
  let monthString = (monthNumber <= 9) ? ("0" + monthNumber) : String(monthNumber)
}

// <rdar://problem/17020197> QoI: Operand of postfix '!' should have optional type; type is 'Int?'
func r17020197(_ x : Int?, y : Int) {
  if x! {  }  // expected-error {{'Int' is not convertible to 'Bool'}}

  // <rdar://problem/12939553> QoI: diagnostic for using an integer in a condition is utterly terrible
  if y {}    // expected-error {{'Int' is not convertible to 'Bool'}}
}

// <rdar://problem/20714480> QoI: Boolean expr not treated as Bool type when function return type is different
func validateSaveButton(_ text: String) {
  return (text.characters.count > 0) ? true : false  // expected-error {{unexpected non-void return value in void function}}
}

// <rdar://problem/20201968> QoI: poor diagnostic when calling a class method via a metatype
class r20201968C {
  func blah() {
    r20201968C.blah()  // expected-error {{use of instance member 'blah' on type 'r20201968C'; did you mean to use a value of type 'r20201968C' instead?}}
  }
}


// <rdar://problem/21459429> QoI: Poor compilation error calling assert
func r21459429(_ a : Int) {
  assert(a != nil, "ASSERT COMPILATION ERROR")
  // expected-warning @-1 {{comparing non-optional value of type 'Int' to nil always returns true}}
}


// <rdar://problem/21362748> [WWDC Lab] QoI: cannot subscript a value of type '[Int]?' with an index of type 'Int'
struct StructWithOptionalArray {
  var array: [Int]?
}

func testStructWithOptionalArray(_ foo: StructWithOptionalArray) -> Int {
  return foo.array[0]  // expected-error {{value of optional type '[Int]?' not unwrapped; did you mean to use '!' or '?'?}} {{19-19=!}}
}


// <rdar://problem/19774755> Incorrect diagnostic for unwrapping non-optional bridged types
var invalidForceUnwrap = Int()! // expected-error {{cannot force unwrap value of non-optional type 'Int'}} {{31-32=}}


// <rdar://problem/20905802> Swift using incorrect diagnostic sometimes on String().asdf
String().asdf  // expected-error {{value of type 'String' has no member 'asdf'}}


// <rdar://problem/21553065> Spurious diagnostic: '_' can only appear in a pattern or on the left side of an assignment
protocol r21553065Protocol {}
class r21553065Class<T : AnyObject> {}
_ = r21553065Class<r21553065Protocol>()  // expected-error {{type 'r21553065Protocol' does not conform to protocol 'AnyObject'}}

// Type variables not getting erased with nested closures
struct Toe {
  let toenail: Nail // expected-error {{use of undeclared type 'Nail'}}

  func clip() {
    toenail.inspect { x in
      toenail.inspect { y in }
    }
  }
}

// <rdar://problem/21447318> dot'ing through a partially applied member produces poor diagnostic
class r21447318 {
  var x = 42
  func doThing() -> r21447318 { return self }
}

func test21447318(_ a : r21447318, b : () -> r21447318) {
  a.doThing.doThing()  // expected-error {{method 'doThing' was used as a property; add () to call it}} {{12-12=()}}
  
  b.doThing() // expected-error {{function 'b' was used as a property; add () to call it}} {{4-4=()}}
}

// <rdar://problem/20409366> Diagnostics for init calls should print the class name
class r20409366C {
  init(a : Int) {}
  init?(a : r20409366C) {
    let req = r20409366C(a: 42)?  // expected-error {{cannot use optional chaining on non-optional value of type 'r20409366C'}} {{32-33=}}
  }
}


// <rdar://problem/18800223> QoI: wrong compiler error when swift ternary operator branches don't match
func r18800223(_ i : Int) {
  // 20099385
  _ = i == 0 ? "" : i  // expected-error {{result values in '? :' expression have mismatching types 'String' and 'Int'}}

  // 19648528
  _ = true ? [i] : i // expected-error {{result values in '? :' expression have mismatching types '[Int]' and 'Int'}}

  
  var buttonTextColor: String?
  _ = (buttonTextColor != nil) ? 42 : {$0}; // expected-error {{result values in '? :' expression have mismatching types 'Int' and '(_) -> _'}}
}

// <rdar://problem/21883806> Bogus "'_' can only appear in a pattern or on the left side of an assignment" is back
_ = { $0 }  // expected-error {{unable to infer closure type in the current context}}



_ = 4()   // expected-error {{cannot call value of non-function type 'Int'}}{{6-8=}}
_ = 4(1)  // expected-error {{cannot call value of non-function type 'Int'}}


// <rdar://problem/21784170> Incongruous `unexpected trailing closure` error in `init` function which is cast and called without trailing closure.
func rdar21784170() {
  let initial = (1.0 as Double, 2.0 as Double)
  (Array.init as (Double...) -> Array<Double>)(initial as (Double, Double)) // expected-error {{cannot convert value of type '(Double, Double)' to expected argument type 'Double'}}
}

// <rdar://problem/21829141> BOGUS: unexpected trailing closure
func expect<T, U>(_: T) -> (U.Type) -> Int { return { a in 0 } }
func expect<T, U>(_: T, _: Int = 1) -> (U.Type) -> String { return { a in "String" } }
let expectType1 = expect(Optional(3))(Optional<Int>.self)
let expectType1Check: Int = expectType1

// <rdar://problem/19804707> Swift Enum Scoping Oddity
func rdar19804707() {
  enum Op {
    case BinaryOperator((Double, Double) -> Double)
  }
  var knownOps : Op
  knownOps = Op.BinaryOperator({$1 - $0})
  knownOps = Op.BinaryOperator(){$1 - $0}
  knownOps = Op.BinaryOperator{$1 - $0}

  knownOps = .BinaryOperator({$1 - $0})

  // rdar://19804707 - trailing closures for contextual member references.
  knownOps = .BinaryOperator(){$1 - $0}
  knownOps = .BinaryOperator{$1 - $0}

  _ = knownOps
}


func f7(_ a: Int) -> (_ b: Int) -> Int {
  return { b in a+b }
}

_ = f7(1)(1)
f7(1.0)(2)       // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}

f7(1)(1.0)       // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}
f7(1)(b: 1.0)    // expected-error{{extraneous argument label 'b:' in call}}   

let f8 = f7(2)
_ = f8(1)
f8(10)          // expected-warning {{result of call is unused, but produces 'Int'}}
f8(1.0)         // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}
f8(b: 1.0)         // expected-error {{extraneous argument label 'b:' in call}}


class CurriedClass {
  func method1() {}
  func method2(_ a: Int) -> (_ b : Int) -> () { return { b in () } }
  func method3(_ a: Int, b : Int) {}  // expected-note 5 {{'method3(_:b:)' declared here}}
}

let c = CurriedClass()
_ = c.method1
c.method1(1)         // expected-error {{argument passed to call that takes no arguments}}
_ = c.method2(1)
_ = c.method2(1.0)   // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}
c.method2(1)(2)
c.method2(1)(c: 2)   // expected-error {{extraneous argument label 'c:' in call}}
c.method2(1)(c: 2.0) // expected-error {{extraneous argument label 'c:' in call}}
c.method2(1)(2.0) // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}
c.method2(1.0)(2) // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}
c.method2(1.0)(2.0) // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}

CurriedClass.method1(c)()
_ = CurriedClass.method1(c)
CurriedClass.method1(c)(1)         // expected-error {{argument passed to call that takes no arguments}}
CurriedClass.method1(2.0)(1)       // expected-error {{use of instance member 'method1' on type 'CurriedClass'; did you mean to use a value of type 'CurriedClass' instead?}}

CurriedClass.method2(c)(32)(b: 1) // expected-error{{extraneous argument label 'b:' in call}}
_ = CurriedClass.method2(c)
_ = CurriedClass.method2(c)(32)
_ = CurriedClass.method2(1,2)      // expected-error {{use of instance member 'method2' on type 'CurriedClass'; did you mean to use a value of type 'CurriedClass' instead?}}
CurriedClass.method2(c)(1.0)(b: 1) // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}
CurriedClass.method2(c)(1)(1.0) // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}
CurriedClass.method2(c)(2)(c: 1.0) // expected-error {{extraneous argument label 'c:'}}

CurriedClass.method3(c)(32, b: 1)
_ = CurriedClass.method3(c)
_ = CurriedClass.method3(c)(1, 2)        // expected-error {{missing argument label 'b:' in call}} {{32-32=b: }}
_ = CurriedClass.method3(c)(1, b: 2)(32) // expected-error {{cannot call value of non-function type '()'}}
_ = CurriedClass.method3(1, 2)           // expected-error {{use of instance member 'method3' on type 'CurriedClass'; did you mean to use a value of type 'CurriedClass' instead?}}
CurriedClass.method3(c)(1.0, b: 1)       // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}
CurriedClass.method3(c)(1)               // expected-error {{missing argument for parameter 'b' in call}}

CurriedClass.method3(c)(c: 1.0)          // expected-error {{missing argument for parameter 'b' in call}}


extension CurriedClass {
  func f() {
    method3(1, b: 2)
    method3()            // expected-error {{missing argument for parameter #1 in call}}
    method3(42)          // expected-error {{missing argument for parameter 'b' in call}}
    method3(self)        // expected-error {{missing argument for parameter 'b' in call}}
  }
}

extension CurriedClass {
  func m1(_ a : Int, b : Int) {}
  
  func m2(_ a : Int) {}
}

// <rdar://problem/23718816> QoI: "Extra argument" error when accidentally currying a method
CurriedClass.m1(2, b: 42)   // expected-error {{use of instance member 'm1' on type 'CurriedClass'; did you mean to use a value of type 'CurriedClass' instead?}}


// <rdar://problem/22108559> QoI: Confusing error message when calling an instance method as a class method
CurriedClass.m2(12)  // expected-error {{use of instance member 'm2' on type 'CurriedClass'; did you mean to use a value of type 'CurriedClass' instead?}}




// <rdar://problem/20491794> Error message does not tell me what the problem is
enum Color {
  case Red
  case Unknown(description: String)

  static func rainbow() -> Color {}
  
  static func overload(a : Int) -> Color {}
  static func overload(b : Int) -> Color {}
  
  static func frob(_ a : Int, b : inout Int) -> Color {}
}
let _: (Int, Color) = [1,2].map({ ($0, .Unknown("")) }) // expected-error {{'map' produces '[T]', not the expected contextual result type '(Int, Color)'}}
let _: [(Int, Color)] = [1,2].map({ ($0, .Unknown("")) })// expected-error {{missing argument label 'description:' in call}} {{51-51=description: }}
let _: [Color] = [1,2].map { _ in .Unknown("") }// expected-error {{missing argument label 'description:' in call}} {{44-44=description: }}

let _: (Int) -> (Int, Color) = { ($0, .Unknown("")) } // expected-error {{missing argument label 'description:' in call}} {{48-48=description: }}
let _: Color = .Unknown("") // expected-error {{missing argument label 'description:' in call}} {{25-25=description: }}
let _: Color = .Unknown // expected-error {{contextual member 'Unknown' expects argument of type '(description: String)'}}
let _: Color = .Unknown(42) // expected-error {{cannot convert value of type 'Int' to expected argument type 'String'}}
let _ : Color = .rainbow(42)  // expected-error {{argument passed to call that takes no arguments}}

let _ : (Int, Float) = (42.0, 12)  // expected-error {{cannot convert value of type 'Double' to specified type 'Int'}}

let _ : Color = .rainbow  // expected-error {{contextual member 'rainbow' expects argument of type '()'}}

let _: Color = .overload(a : 1.0)  // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}
let _: Color = .overload(1.0)  // expected-error {{ambiguous reference to member 'overload'}}
// expected-note @-1 {{overloads for 'overload' exist with these partially matching parameter lists: (a: Int), (b: Int)}}
let _: Color = .overload(1)  // expected-error {{ambiguous reference to member 'overload'}}
// expected-note @-1 {{overloads for 'overload' exist with these partially matching parameter lists: (a: Int), (b: Int)}}
let _: Color = .frob(1.0, &i) // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}
let _: Color = .frob(1, i)  // expected-error {{passing value of type 'Int' to an inout parameter requires explicit '&'}}
let _: Color = .frob(1, b: i)  // expected-error {{passing value of type 'Int' to an inout parameter requires explicit '&'}}
let _: Color = .frob(1, &d) // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}
let _: Color = .frob(1, b: &d) // expected-error {{cannot convert value of type 'Double' to expected argument type 'Int'}}
var someColor : Color = .red // expected-error {{enum type 'Color' has no case 'red'; did you mean 'Red'}}
someColor = .red  // expected-error {{enum type 'Color' has no case 'red'; did you mean 'Red'}}

func testTypeSugar(_ a : Int) {
  typealias Stride = Int

  let x = Stride(a)
  x+"foo"            // expected-error {{binary operator '+' cannot be applied to operands of type 'Stride' (aka 'Int') and 'String'}}
// expected-note @-1 {{overloads for '+' exist with these partially matching parameter lists: (Int, Int), (String, String), (Int, UnsafeMutablePointer<Pointee>), (Int, UnsafePointer<Pointee>)}}
}

// <rdar://problem/21974772> SegFault in FailureDiagnosis::visitInOutExpr
func r21974772(_ y : Int) {
  let x = &(1.0 + y)  // expected-error {{binary operator '+' cannot be applied to operands of type 'Double' and 'Int'}}
   //expected-note @-1 {{overloads for '+' exist with these partially matching parameter lists: (Int, Int), (Double, Double), (UnsafeMutablePointer<Pointee>, Int), (UnsafePointer<Pointee>, Int)}}
}

// <rdar://problem/22020088> QoI: missing member diagnostic on optional gives worse error message than existential/bound generic/etc
protocol r22020088P {}

func r22020088Foo<T>(_ t: T) {}

func r22020088bar(_ p: r22020088P?) {
  r22020088Foo(p.fdafs)  // expected-error {{value of type 'r22020088P?' has no member 'fdafs'}}
}

// <rdar://problem/22288575> QoI: poor diagnostic involving closure, bad parameter label, and mismatch return type
func f(_ arguments: [String]) -> [ArraySlice<String>] {
  return arguments.split(maxSplits: 1, omittingEmptySubsequences: false, whereSeparator: { $0 == "--" })
}



struct AOpts : OptionSet {
  let rawValue : Int
}

class B {
  func function(_ x : Int8, a : AOpts) {}
  func f2(_ a : AOpts) {}
  static func f1(_ a : AOpts) {}
}


func test(_ a : B) {
  B.f1(nil)    // expected-error {{nil is not compatible with expected argument type 'AOpts'}}
  a.function(42, a: nil) //expected-error {{nil is not compatible with expected argument type 'AOpts'}}
  a.function(42, nil) //expected-error {{missing argument label 'a:' in call}}
  a.f2(nil)  // expected-error {{nil is not compatible with expected argument type 'AOpts'}}
}

// <rdar://problem/21684487> QoI: invalid operator use inside a closure reported as a problem with the closure
typealias MyClosure = ([Int]) -> Bool
func r21684487() {
  var closures = Array<MyClosure>()
  let testClosure = {(list: [Int]) -> Bool in return true}
  
  let closureIndex = closures.index{$0 === testClosure} // expected-error {{cannot check reference equality of functions; operands here have types '_' and '([Int]) -> Bool'}}
}

// <rdar://problem/18397777> QoI: special case comparisons with nil
func r18397777(_ d : r21447318?) {
  let c = r21447318()

  if c != nil { // expected-warning {{comparing non-optional value of type 'r21447318' to nil always returns true}}
  }
  
  if d {  // expected-error {{optional type 'r21447318?' cannot be used as a boolean; test for '!= nil' instead}} {{6-6=(}} {{7-7= != nil)}}
  }
  
  if !d { // expected-error {{optional type 'r21447318?' cannot be used as a boolean; test for '!= nil' instead}} {{7-7=(}} {{8-8= != nil)}}

  }

  if !Optional(c) { // expected-error {{optional type 'Optional<r21447318>' cannot be used as a boolean; test for '!= nil' instead}} {{7-7=(}} {{18-18= != nil)}}
  }
}


// <rdar://problem/22255907> QoI: bad diagnostic if spurious & in argument list
func r22255907_1<T>(_ a : T, b : Int) {}
func r22255907_2<T>(_ x : Int, a : T, b: Int) {}

func reachabilityForInternetConnection() {
  var variable: Int = 42
  r22255907_1(&variable, b: 2.1) // expected-error {{'&' used with non-inout argument of type 'Int'}} {{15-16=}}
  r22255907_2(1, a: &variable, b: 2.1)// expected-error {{'&' used with non-inout argument of type 'Int'}} {{21-22=}}
}

// <rdar://problem/21601687> QoI: Using "=" instead of "==" in if statement leads to incorrect error message
if i = 6 { } // expected-error {{use of '=' in a boolean context, did you mean '=='?}} {{6-7===}}

_ = (i = 6) ? 42 : 57 // expected-error {{use of '=' in a boolean context, did you mean '=='?}} {{8-9===}}


// <rdar://problem/22263468> QoI: Not producing specific argument conversion diagnostic for tuple init
func r22263468(_ a : String?) {
  typealias MyTuple = (Int, String)
  _ = MyTuple(42, a) // expected-error {{value of optional type 'String?' not unwrapped; did you mean to use '!' or '?'?}} {{20-20=!}}
}


// rdar://22470302 - Crash with parenthesized call result.
class r22470302Class {
  func f() {}
}

func r22470302(_ c: r22470302Class) {
  print((c.f)(c))  // expected-error {{argument passed to call that takes no arguments}}
}



// <rdar://problem/21928143> QoI: Pointfree reference to generic initializer in generic context does not compile
extension String {
  @available(*, unavailable, message: "calling this is unwise")
  func unavail<T : Sequence> // expected-note 2 {{'unavail' has been explicitly marked unavailable here}}
    (_ a : T) -> String where T.Iterator.Element == String {}
}
extension Array {
  func g() -> String {
    return "foo".unavail([""])  // expected-error {{'unavail' is unavailable: calling this is unwise}}
  }
  
  func h() -> String {
    return "foo".unavail([0])  // expected-error {{'unavail' is unavailable: calling this is unwise}}
  }
}

// <rdar://problem/22519983> QoI: Weird error when failing to infer archetype
func safeAssign<T: RawRepresentable>(_ lhs: inout T) -> Bool {}
// expected-note @-1 {{in call to function 'safeAssign'}}
let a = safeAssign // expected-error {{generic parameter 'T' could not be inferred}}

// <rdar://problem/21692808> QoI: Incorrect 'add ()' fixit with trailing closure
struct Radar21692808<Element> {
  init(count: Int, value: Element) {}
}
func radar21692808() -> Radar21692808<Int> {
  return Radar21692808<Int>(count: 1) { // expected-error {{cannot invoke initializer for type 'Radar21692808<Int>' with an argument list of type '(count: Int, () -> Int)'}}
    // expected-note @-1 {{expected an argument list of type '(count: Int, value: Element)'}}
    return 1
  }
}

// <rdar://problem/17557899> - This shouldn't suggest calling with ().
func someOtherFunction() {}
func someFunction() -> () {
  // Producing an error suggesting that this
  return someOtherFunction  // expected-error {{unexpected non-void return value in void function}}
}

// <rdar://problem/23560128> QoI: trying to mutate an optional dictionary result produces bogus diagnostic
func r23560128() {
  var a : (Int,Int)?
  a.0 = 42  // expected-error {{value of optional type '(Int, Int)?' not unwrapped; did you mean to use '!' or '?'?}} {{4-4=?}}
}

// <rdar://problem/21890157> QoI: wrong error message when accessing properties on optional structs without unwrapping
struct ExampleStruct21890157 {
  var property = "property"
}
var example21890157: ExampleStruct21890157?
example21890157.property = "confusing"  // expected-error {{value of optional type 'ExampleStruct21890157?' not unwrapped; did you mean to use '!' or '?'?}} {{16-16=?}}


struct UnaryOp {}

_ = -UnaryOp() // expected-error {{unary operator '-' cannot be applied to an operand of type 'UnaryOp'}}
// expected-note @-1 {{overloads for '-' exist with these partially matching parameter lists: (Float), (Double)}}


// <rdar://problem/23433271> Swift compiler segfault in failure diagnosis
func f23433271(_ x : UnsafePointer<Int>) {}
func segfault23433271(_ a : UnsafeMutableRawPointer) {
  f23433271(a[0])  // expected-error {{type 'UnsafeMutableRawPointer' has no subscript members}}
}



// <rdar://problem/23272739> Poor diagnostic due to contextual constraint
func r23272739(_ contentType: String) {
  let actualAcceptableContentTypes: Set<String> = []
  return actualAcceptableContentTypes.contains(contentType)  // expected-error {{unexpected non-void return value in void function}}
}

// <rdar://problem/23641896> QoI: Strings in Swift cannot be indexed directly with integer offsets
func r23641896() {
  var g = "Hello World"
  g.replaceSubrange(0...2, with: "ce")  // expected-error {{cannot invoke 'replaceSubrange' with an argument list of type '(CountableClosedRange<Int>, with: String)'}} expected-note {{overloads for 'replaceSubrange' exist}}

  _ = g[12]  // expected-error {{'subscript' is unavailable: cannot subscript String with an Int, see the documentation comment for discussion}}

}


// <rdar://problem/23718859> QoI: Incorrectly flattening ((Int,Int)) argument list to (Int,Int) when printing note
func test17875634() {
  var match: [(Int, Int)] = []
  var row = 1
  var col = 2
  
  match.append(row, col)  // expected-error {{extra argument in call}}
}

// <https://github.com/apple/swift/pull/1205> Improved diagnostics for enums with associated values
enum AssocTest {
  case one(Int)
}

if AssocTest.one(1) == AssocTest.one(1) {} // expected-error{{binary operator '==' cannot be applied to two 'AssocTest' operands}}
// expected-note @-1 {{binary operator '==' cannot be synthesized for enums with associated values}}


// <rdar://problem/24251022> Swift 2: Bad Diagnostic Message When Adding Different Integer Types
func r24251022() {
  var a = 1
  var b: UInt32 = 2
  a += a + // expected-error {{binary operator '+' cannot be applied to operands of type 'Int' and 'UInt32'}} expected-note {{expected an argument list of type '(Int, Int)'}}
    b
}

func overloadSetResultType(_ a : Int, b : Int) -> Int {
  // https://twitter.com/_jlfischer/status/712337382175952896
  // TODO: <rdar://problem/27391581> QoI: Nonsensical "binary operator '&&' cannot be applied to two 'Bool' operands"
  return a == b && 1 == 2  // expected-error {{binary operator '&&' cannot be applied to two 'Bool' operands}}
  // expected-note @-1 {{expected an argument list of type '(Bool, @autoclosure () throws -> Bool)'}}
}

// <rdar://problem/21523291> compiler error message for mutating immutable field is incorrect
func r21523291(_ bytes : UnsafeMutablePointer<UInt8>) {
  let i = 42   // expected-note {{change 'let' to 'var' to make it mutable}}
  let r = bytes[i++]  // expected-error {{cannot pass immutable value as inout argument: 'i' is a 'let' constant}}
}


// SR-1594: Wrong error description when using === on non-class types
class SR1594 {
  func sr1594(bytes : UnsafeMutablePointer<Int>, _ i : Int?) {
    _ = (i === nil) // expected-error {{value of type 'Int?' cannot be compared by reference; did you mean to compare by value?}} {{12-15===}}
    _ = (bytes === nil) // expected-error {{type 'UnsafeMutablePointer<Int>' is not optional, value can never be nil}}
    _ = (self === nil) // expected-warning {{comparing non-optional value of type 'AnyObject' to nil always returns false}}
    _ = (i !== nil) // expected-error {{value of type 'Int?' cannot be compared by reference; did you mean to compare by value?}} {{12-15=!=}}
    _ = (bytes !== nil) // expected-error {{type 'UnsafeMutablePointer<Int>' is not optional, value can never be nil}}
    _ = (self !== nil) // expected-warning {{comparing non-optional value of type 'AnyObject' to nil always returns true}}
  }
}

func nilComparison(i: Int, o: AnyObject) {
  _ = i == nil // expected-warning {{comparing non-optional value of type 'Int' to nil always returns false}}
  _ = nil == i // expected-warning {{comparing non-optional value of type 'Int' to nil always returns false}}
  _ = i != nil // expected-warning {{comparing non-optional value of type 'Int' to nil always returns true}}
  _ = nil != i // expected-warning {{comparing non-optional value of type 'Int' to nil always returns true}}
  
  _ = i < nil  // expected-error {{type 'Int' is not optional, value can never be nil}}
  _ = nil < i  // expected-error {{type 'Int' is not optional, value can never be nil}}
  _ = i <= nil // expected-error {{type 'Int' is not optional, value can never be nil}}
  _ = nil <= i // expected-error {{type 'Int' is not optional, value can never be nil}}
  _ = i > nil  // expected-error {{type 'Int' is not optional, value can never be nil}}
  _ = nil > i  // expected-error {{type 'Int' is not optional, value can never be nil}}
  _ = i >= nil // expected-error {{type 'Int' is not optional, value can never be nil}}
  _ = nil >= i // expected-error {{type 'Int' is not optional, value can never be nil}}

  _ = o === nil // expected-warning {{comparing non-optional value of type 'AnyObject' to nil always returns false}}
  _ = o !== nil // expected-warning {{comparing non-optional value of type 'AnyObject' to nil always returns true}}
}

// FIXME: Bad diagnostic
func secondArgumentNotLabeled(a:Int, _ b: Int) { }
secondArgumentNotLabeled(10, 20)
// expected-error@-1 {{unnamed argument #2 must precede unnamed argument #1}}

// <rdar://problem/23709100> QoI: incorrect ambiguity error due to implicit conversion
func testImplConversion(a : Float?) -> Bool {}
func testImplConversion(a : Int?) -> Bool {
  let someInt = 42
  let a : Int = testImplConversion(someInt) // expected-error {{'testImplConversion' produces 'Bool', not the expected contextual result type 'Int'}}
}

// <rdar://problem/23752537> QoI: Bogus error message: Binary operator '&&' cannot be applied to two 'Bool' operands
class Foo23752537 {
  var title: String?
  var message: String?
}

extension Foo23752537 {
  func isEquivalent(other: Foo23752537) {
    // TODO: <rdar://problem/27391581> QoI: Nonsensical "binary operator '&&' cannot be applied to two 'Bool' operands"
    // expected-error @+1 {{binary operator '&&' cannot be applied to two 'Bool' operands}}
    return (self.title != other.title && self.message != other.message)
    // expected-note @-1 {{expected an argument list of type '(Bool, @autoclosure () throws -> Bool)'}}
  }
}


// <rdar://problem/22276040> QoI: not great error message with "withUnsafePointer" sametype constraints
func read2(_ p: UnsafeMutableRawPointer, maxLength: Int) {}
func read<T : Integer>() -> T? {
  var buffer : T 
  let n = withUnsafePointer(to: &buffer) { (p) in
    read2(UnsafePointer(p), maxLength: MemoryLayout<T>.size) // expected-error {{cannot convert value of type 'UnsafePointer<_>' to expected argument type 'UnsafeMutableRawPointer'}}
  }
}

func f23213302() {
  var s = Set<Int>()
  s.subtractInPlace(1) // expected-error {{cannot convert value of type 'Int' to expected argument type 'Set<Int>'}}
}

// <rdar://problem/24202058> QoI: Return of call to overloaded function in void-return context
func rdar24202058(a : Int) {
  return a <= 480 // expected-error {{'<=' produces 'Bool', not the expected contextual result type '()'}}
}

// SR-1752: Warning about unused result with ternary operator

struct SR1752 {
  func foo() {}
}

let sr1752: SR1752? = nil

true ? nil : sr1752?.foo() // don't generate a warning about unused result since foo returns Void

// <rdar://problem/27891805> QoI: FailureDiagnosis doesn't look through 'try'
struct rdar27891805 {
  init(contentsOf: String, encoding: String) throws {}
  init(contentsOf: String, usedEncoding: inout String) throws {}
  init<T>(_ t: T) {}
}

try rdar27891805(contentsOfURL: nil, usedEncoding: nil)
// expected-error@-1 {{argument labels '(contentsOfURL:, usedEncoding:)' do not match any available overloads}}
// expected-note@-2 {{overloads for 'rdar27891805' exist with these partially matching parameter lists: (contentsOf: String, encoding: String), (contentsOf: String, usedEncoding: inout String)}}
