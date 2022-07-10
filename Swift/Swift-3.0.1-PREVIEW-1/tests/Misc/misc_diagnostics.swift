// RUN: %target-parse-verify-swift

// REQUIRES: objc_interop

import Foundation
import CoreGraphics

var roomName : String? = nil

if let realRoomName = roomName as! NSString { // expected-error {{initializer for conditional binding must have Optional type, not 'NSString'}} expected-warning {{cast from 'String?' to unrelated type 'NSString' always fails}}
			
}

var pi = 3.14159265358979
var d: CGFloat = 2.0
var dpi:CGFloat = d*pi // expected-error{{binary operator '*' cannot be applied to operands of type 'CGFloat' and 'Double'}} // expected-note{{expected an argument list of type '(CGFloat, CGFloat)'}}

let ff: CGFloat = floorf(20.0) // expected-error{{cannot convert value of type 'Float' to specified type 'CGFloat'}}

let total = 15.0
let count = 7
let median = total / count // expected-error {{binary operator '/' cannot be applied to operands of type 'Double' and 'Int'}} expected-note {{overloads for '/' exist with these partially matching parameter lists: (Int, Int), (Double, Double)}}

if (1) {} // expected-error{{'Int' is not convertible to 'Bool'}}
if 1 {} // expected-error {{'Int' is not convertible to 'Bool'}}

var a: [String] = [1] // expected-error{{cannot convert value of type 'Int' to expected element type 'String'}}
var b: Int = [1, 2, 3] // expected-error{{contextual type 'Int' cannot be used with array literal}}

var f1: Float = 2.0
var f2: Float = 3.0

var dd: Double = f1 - f2 // expected-error{{binary operator '-' cannot be applied to two 'Float' operands}} // expected-note{{expected an argument list of type '(Double, Double)'}}

func f() -> Bool {
  return 1 + 1 // expected-error{{no '+' candidates produce the expected contextual result type 'Bool'}}
  // expected-note @-1 {{overloads for '+' exist with these result types: UInt8, Int8, UInt16, Int16, UInt32, Int32, UInt64, Int64, UInt, Int, Float, Double}}
}

// Test that nested diagnostics are properly surfaced.
func takesInt(_ i: Int) {}
func noParams() -> Int { return 0 }
func takesAndReturnsInt(_ i: Int) -> Int { return 0 }

takesInt(noParams(1)) // expected-error{{argument passed to call that takes no arguments}}

takesInt(takesAndReturnsInt("")) // expected-error{{cannot convert value of type 'String' to expected argument type 'Int'}}

// Test error recovery for type expressions.
struct MyArray<Element> {}
class A {
    var a: MyArray<Int>
    init() {
        a = MyArray<Int // expected-error{{'<' produces 'Bool', not the expected contextual result type 'MyArray<Int>'}}
    }
}

func retV() { return true } // expected-error {{unexpected non-void return value in void function}}

func retAI() -> Int {
    let a = [""]
    let b = [""]
    return (a + b) // expected-error{{binary operator '+' cannot be applied to two '[String]' operands}} // expected-note{{expected an argument list of type '(Int, Int)'}}
}

func bad_return1() {
  return 42  // expected-error {{unexpected non-void return value in void function}}
}

func bad_return2() -> (Int, Int) {
  return 42  // expected-error {{cannot convert return expression of type 'Int' to return type '(Int, Int)'}}
}

// <rdar://problem/14096697> QoI: Diagnostics for trying to return values from void functions
func bad_return3(lhs:Int, rhs:Int) {
  return lhs != 0  // expected-error {{'!=' produces 'Bool', not the expected contextual result type '()'}}
}

class MyBadReturnClass {
  static var intProperty = 42
}

func ==(lhs:MyBadReturnClass, rhs:MyBadReturnClass) {
  return MyBadReturnClass.intProperty == MyBadReturnClass.intProperty  // expected-error{{binary operator '==' cannot be applied to two 'Int' operands}} // expected-note{{expected an argument list of type '(MyBadReturnClass, MyBadReturnClass)'}}
}


func testIS1() -> Int { return 0 }
let _: String = testIS1() // expected-error {{cannot convert value of type 'Int' to specified type 'String'}}

func insertA<T>(array : inout [T], elt : T) {
  array.append(T); // expected-error {{cannot invoke 'append' with an argument list of type '((T).Type)'}} expected-note {{expected an argument list of type '(T)'}}
}

// <rdar://problem/17875634> can't append to array of tuples
func test17875634() {
  var match: [(Int, Int)] = []
  var row = 1
  var col = 2
  var coord = (row, col)

  match += (1, 2) // expected-error{{argument type '(Int, Int)' does not conform to expected type 'Sequence'}}

  match += (row, col) // expected-error{{argument type '(@lvalue Int, @lvalue Int)' does not conform to expected type 'Sequence'}}

  match += coord // expected-error{{argument type '@lvalue (Int, Int)' does not conform to expected type 'Sequence'}}

  match.append(row, col) // expected-error{{extra argument in call}}

  match.append(1, 2) // expected-error{{extra argument in call}}

  match.append(coord)
  match.append((1, 2))

  // Make sure the behavior matches the non-generic case.
  struct FakeNonGenericArray {
    func append(_ p: (Int, Int)) {}
  }
  let a2 = FakeNonGenericArray()
  a2.append(row, col) // expected-error{{extra argument in call}}
  a2.append(1, 2) // expected-error{{extra argument in call}}
  a2.append(coord)
  a2.append((1, 2))
}

// <rdar://problem/20770032> Pattern matching ranges against tuples crashes the compiler
func test20770032() {
  if case let 1...10 = (1, 1) { // expected-warning{{'let' pattern has no effect; sub-pattern didn't bind any variables}} {{11-15=}} expected-error{{expression pattern of type 'CountableClosedRange<Int>' cannot match values of type '(Int, Int)'}}
  }
}



func tuple_splat1(_ a : Int, _ b : Int) { // expected-note {{'tuple_splat1' declared here}}
  let x = (1,2)
  tuple_splat1(x)          // expected-error {{passing 2 arguments to a callee as a single tuple value has been removed in Swift 3}}
  tuple_splat1(1, 2)       // Ok.
  tuple_splat1((1, 2))     // expected-error {{missing argument for parameter #2 in call}}
}

// This take a tuple as a value, so it isn't a tuple splat.
func tuple_splat2(_ q : (a : Int, b : Int)) {
  let x = (1,2)
  tuple_splat2(x)          // Ok
  let y = (1, b: 2)
  tuple_splat2(y)          // Ok
  tuple_splat2((1, b: 2))  // Ok.
  tuple_splat2(1, b: 2)    // expected-error {{extra argument 'b' in call}}
}

// SR-1612: Type comparison of foreign types is always true.
func is_foreign(a: AnyObject) -> Bool {
  return a is CGColor // expected-warning {{'is' test is always true because 'CGColor' is a Core Foundation type}}
}

