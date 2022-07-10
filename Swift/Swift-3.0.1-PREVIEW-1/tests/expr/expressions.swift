// RUN: %target-parse-verify-swift

//===----------------------------------------------------------------------===//
// Tests and samples.
//===----------------------------------------------------------------------===//

// Comment.  With unicode characters: ¡ç®åz¥!

func markUsed<T>(_: T) {}

// Various function types.
var func1 : () -> ()    // No input, no output.
var func2 : (Int) -> Int
var func3 : () -> () -> ()                   // Takes nothing, returns a fn.
var func3a : () -> (() -> ())                // same as func3
var func6 : (_ fn : (Int,Int) -> Int) -> ()    // Takes a fn, returns nothing.
var func7 : () -> (Int,Int,Int)              // Takes nothing, returns tuple.

// Top-Level expressions.  These are 'main' content.
func1()
_ = 4+7

var bind_test1 : () -> () = func1
var bind_test2 : Int = 4; func1 // expected-error {{expression resolves to an unused l-value}}

(func1, func2) // expected-error {{expression resolves to an unused l-value}}

func basictest() {
  // Simple integer variables.
  var x : Int
  var x2 = 4              // Simple Type inference.
  var x3 = 4+x*(4+x2)/97  // Basic Expressions.

  // Declaring a variable Void, aka (), is fine too.
  var v : Void

  var x4 : Bool = true
  var x5 : Bool =
        4 // expected-error {{cannot convert value of type 'Int' to specified type 'Bool'}}

  //var x6 : Float = 4+5

  var x7 = 4; 5 // expected-warning {{result of call to 'init(_builtinIntegerLiteral:)' is unused}}

  // Test implicit conversion of integer literal to non-Int64 type.
  var x8 : Int8 = 4
  x8 = x8 + 1
  _ = x8 + 1
  _ = 0 + x8
  1.0 + x8 // expected-error{{binary operator '+' cannot be applied to operands of type 'Double' and 'Int8'}}
  // expected-note @-1 {{overloads for '+' exist with these partially matching parameter lists:}}


  var x9 : Int16 = x8 + 1 // expected-error {{binary operator '+' cannot be applied to operands of type 'Int8' and 'Int'}} expected-note {{expected an argument list of type '(Int16, Int16)'}}

  // Various tuple types.
  var tuple1 : ()
  var tuple2 : (Int)
  var tuple3 : (Int, Int, ())
  var tuple2a : (a : Int) // expected-error{{cannot create a single-element tuple with an element label}}{{18-22=}}
  var tuple3a : (a : Int, b : Int, c : ())

  var tuple4 = (1, 2)        // Tuple literal.
  var tuple5 = (1, 2, 3, 4)  // Tuple literal.
  var tuple6 = (1 2)  // expected-error {{expected ',' separator}} {{18-18=,}}

  // Brace expressions.
  var brace3 = {
    var brace2 = 42  // variable shadowing.
    _ = brace2+7
  }

  // Function calls.
  var call1 : () = func1()
  var call2 = func2(1)
  var call3 : () = func3()()

  // Cannot call an integer.
  bind_test2() // expected-error {{cannot call value of non-function type 'Int'}}{{13-15=}}
}

// Infix operators and attribute lists.
infix operator %% : MinPrecedence
precedencegroup MinPrecedence {
  associativity: left
  lowerThan: AssignmentPrecedence
}

func %%(a: Int, b: Int) -> () {}
var infixtest : () = 4 % 2 + 27 %% 123



// The 'func' keyword gives a nice simplification for function definitions.
func funcdecl1(_ a: Int, _ y: Int) {}
func funcdecl2() {
  return funcdecl1(4, 2)
}
func funcdecl3() -> Int {
  return 12
}
func funcdecl4(_ a: ((Int) -> Int), b: Int) {}
func signal(_ sig: Int, f: (Int) -> Void) -> (Int) -> Void {}

// Doing fun things with named arguments.  Basic stuff first.
func funcdecl6(_ a: Int, b: Int) -> Int { return a+b }

// Can dive into tuples, 'b' is a reference to a whole tuple, c and d are
// fields in one.  Cannot dive into functions or through aliases.
func funcdecl7(_ a: Int, b: (c: Int, d: Int), third: (c: Int, d: Int)) -> Int {
  _ = a + b.0 + b.c + third.0 + third.1
  b.foo // expected-error {{value of tuple type '(c: Int, d: Int)' has no member 'foo'}}
}

// Error recovery.
func testfunc2 (_: ((), Int) -> Int) -> Int {}
func errorRecovery() {
  testfunc2({ $0 + 1 }) // expected-error {{binary operator '+' cannot be applied to operands of type '((), Int)' and 'Int'}} expected-note {{expected an argument list of type '(Int, Int)'}}

  enum union1 {
    case bar
    case baz
  }
  var a: Int = .hello // expected-error {{type 'Int' has no member 'hello'}}
  var b: union1 = .bar // ok
  var c: union1 = .xyz  // expected-error {{type 'union1' has no member 'xyz'}}
  var d: (Int,Int,Int) = (1,2) // expected-error {{cannot convert value of type '(Int, Int)' to specified type '(Int, Int, Int)'}}
  var e: (Int,Int) = (1, 2, 3) // expected-error {{cannot convert value of type '(Int, Int, Int)' to specified type '(Int, Int)'}}
  var f: (Int,Int) = (1, 2, f : 3) // expected-error {{cannot convert value of type '(Int, Int, f: Int)' to specified type '(Int, Int)'}}
  
  // <rdar://problem/22426860> CrashTracer: [USER] swift at …mous_namespace::ConstraintGenerator::getTypeForPattern + 698
  var (g1, g2, g3) = (1, 2) // expected-error {{'(Int, Int)' is not convertible to '(_, _, _)', tuples have a different number of elements}}
}

func acceptsInt(_ x: Int) {}
acceptsInt(unknown_var) // expected-error {{use of unresolved identifier 'unknown_var'}}



var test1a: (Int) -> (Int) -> Int = { { $0 } } // expected-error{{contextual type for closure argument list expects 1 argument, which cannot be implicitly ignored}} {{38-38= _ in}}
var test1b = { 42 }
var test1c = { { 42 } }
var test1d = { { { 42 } } }

func test2(_ a: Int, b: Int) -> (c: Int) { // expected-error{{cannot create a single-element tuple with an element label}} {{34-37=}} expected-note {{did you mean 'a'?}} expected-note {{did you mean 'b'?}}
 _ = a+b
 a+b+c // expected-error{{use of unresolved identifier 'c'}}
 return a+b
}


func test3(_ arg1: Int, arg2: Int) -> Int {
  return 4
}

func test4() -> ((_ arg1: Int, _ arg2: Int) -> Int) {
  return test3
}

func test5() {
  let a: (Int, Int) = (1,2)
  var
     _: ((Int) -> Int, Int) = a  // expected-error {{cannot convert value of type '(Int, Int)' to specified type '((Int) -> Int, Int)'}}


  let c: (a: Int, b: Int) = (1,2)
  let _: (b: Int, a: Int) = c  // Ok, reshuffle tuple.
}


// Functions can obviously take and return values.
func w3(_ a: Int) -> Int { return a }
func w4(_: Int) -> Int { return 4 }



func b1() {}

func foo1(_ a: Int, b: Int) -> Int {}
func foo2(_ a: Int) -> (_ b: Int) -> Int {}
func foo3(_ a: Int = 2, b: Int = 3) {}

prefix operator ^^

prefix func ^^(a: Int) -> Int {
  return a + 1
}

func test_unary1() {
  var x: Int

  x = ^^(^^x)
  x = *x      // expected-error {{'*' is not a prefix unary operator}}
  x = x*      // expected-error {{'*' is not a postfix unary operator}}
  x = +(-x)
  x = + -x // expected-error {{unary operator cannot be separated from its operand}} {{8-9=}}
}
func test_unary2() {
  var x: Int
  // FIXME: second diagnostic is redundant.
  x = &; // expected-error {{expected expression after unary operator}} expected-error {{expected expression in assignment}}
}
func test_unary3() {
  var x: Int
  // FIXME: second diagnostic is redundant.
  x = &, // expected-error {{expected expression after unary operator}} expected-error {{expected expression in assignment}}
}

func test_as_1() {
  var _: Int
}
func test_as_2() {
  let x: Int = 1
  x as [] // expected-error {{expected element type}}
}

func test_lambda() {
  // A simple closure.
  var a = { (value: Int) -> () in markUsed(value+1) }

  // A recursive lambda.
  // FIXME: This should definitely be accepted.
  var fib = { (n: Int) -> Int in
    if (n < 2) {
      return n
    }
    
    return fib(n-1)+fib(n-2) // expected-error 2 {{variable used within its own initial value}}
  }
}

func test_lambda2() {
  { () -> protocol<Int> in
    // expected-warning @-1 {{'protocol<...>' composition syntax is deprecated and not needed here}} {{11-24=Int}}
    // expected-error @-2 {{non-protocol type 'Int' cannot be used within a protocol composition}}
    // expected-warning @-3 {{result of call is unused}}
    return 1
  }()
}

func test_floating_point() {
  _ = 0.0
  _ = 100.1
  var _: Float = 0.0
  var _: Double = 0.0
}

func test_nonassoc(_ x: Int, y: Int) -> Bool {
  // FIXME: the second error and note here should arguably disappear
  return x == y == x // expected-error {{adjacent operators are in non-associative precedence group 'ComparisonPrecedence'}}  expected-error {{binary operator '==' cannot be applied to operands of type 'Bool' and 'Int'}} expected-note {{overloads for '==' exist with these partially matching parameter lists:}}
}

// More realistic examples.

func fib(_ n: Int) -> Int {
  if (n < 2) {
    return n
  }

  return fib(n-2) + fib(n-1)
}

//===----------------------------------------------------------------------===//
// Integer Literals
//===----------------------------------------------------------------------===//

// FIXME: Should warn about integer constants being too large <rdar://problem/14070127>
var
   il_a: Bool = 4  // expected-error {{cannot convert value of type 'Int' to specified type 'Bool'}}
var il_b: Int8
   = 123123
var il_c: Int8 = 4  // ok

struct int_test4 : ExpressibleByIntegerLiteral {
  typealias IntegerLiteralType = Int
  init(integerLiteral value: Int) {} // user type.
}

var il_g: int_test4 = 4



// This just barely fits in Int64.
var il_i: Int64  = 18446744073709551615

// This constant is too large to fit in an Int64, but it is fine for Int128.
// FIXME: Should warn about the first. <rdar://problem/14070127>
var il_j: Int64  = 18446744073709551616
// var il_k: Int128 = 18446744073709551616

var bin_literal: Int64 = 0b100101
var hex_literal: Int64 = 0x100101
var oct_literal: Int64 = 0o100101

// verify that we're not using C rules
var oct_literal_test: Int64 = 0123
assert(oct_literal_test == 123)

// ensure that we swallow random invalid chars after the first invalid char
var invalid_num_literal: Int64 = 0QWERTY  // expected-error{{expected a digit after integer literal prefix}}
var invalid_bin_literal: Int64 = 0bQWERTY // expected-error{{expected a digit after integer literal prefix}}
var invalid_hex_literal: Int64 = 0xQWERTY // expected-error{{expected a digit after integer literal prefix}}
var invalid_oct_literal: Int64 = 0oQWERTY // expected-error{{expected a digit after integer literal prefix}}
var invalid_exp_literal: Double = 1.0e+QWERTY // expected-error{{expected a digit in floating point exponent}}

// rdar://11088443
var negative_int32: Int32 = -1

// <rdar://problem/11287167>
var tupleelemvar = 1
markUsed((tupleelemvar, tupleelemvar).1)

func int_literals() {
  // Fits exactly in 64-bits - rdar://11297273
  _ = 1239123123123123
  // Overly large integer.
  // FIXME: Should warn about it. <rdar://problem/14070127>
  _ = 123912312312312312312
  
}

// <rdar://problem/12830375>
func tuple_of_rvalues(_ a:Int, b:Int) -> Int {
  return (a, b).1
}

extension Int {
  func testLexingMethodAfterIntLiteral() {}
  func _0() {}
  // Hex letters
  func ffa() {}
  // Hex letters + non hex.
  func describe() {}
  // Hex letters + 'p'.
  func eap() {}
  // Hex letters + 'p' + non hex.
  func fpValue() {}
}

123.testLexingMethodAfterIntLiteral()
0b101.testLexingMethodAfterIntLiteral()
0o123.testLexingMethodAfterIntLiteral()
0x1FFF.testLexingMethodAfterIntLiteral()

123._0()
0b101._0()
0o123._0()
0x1FFF._0()

0x1fff.ffa()
0x1FFF.describe()
0x1FFF.eap()
0x1FFF.fpValue()

var separator1: Int = 1_
var separator2: Int = 1_000
var separator4: Int = 0b1111_0000_
var separator5: Int = 0b1111_0000
var separator6: Int = 0o127_777_
var separator7: Int = 0o127_777
var separator8: Int = 0x12FF_FFFF
var separator9: Int = 0x12FF_FFFF_

//===----------------------------------------------------------------------===//
// Float Literals
//===----------------------------------------------------------------------===//

var fl_a = 0.0
var fl_b: Double = 1.0
var fl_c: Float = 2.0
// FIXME: crummy diagnostic
var fl_d: Float = 2.0.0 // expected-error {{expected named member of numeric literal}}
var fl_e: Float = 1.0e42
var fl_f: Float = 1.0e+  // expected-error {{expected a digit in floating point exponent}} 
var fl_g: Float = 1.0E+42
var fl_h: Float = 2e-42
var vl_i: Float = -.45   // expected-error {{'.45' is not a valid floating point literal; it must be written '0.45'}} {{20-20=0}}
var fl_j: Float = 0x1p0
var fl_k: Float = 0x1.0p0
var fl_l: Float = 0x1.0 // expected-error {{hexadecimal floating point literal must end with an exponent}}
var fl_m: Float = 0x1.FFFFFEP-2
var fl_n: Float = 0x1.fffffep+2
var fl_o: Float = 0x1.fffffep+ // expected-error {{expected a digit in floating point exponent}}
var fl_p: Float = 0x1p // expected-error {{expected a digit in floating point exponent}}
var fl_q: Float = 0x1p+ // expected-error {{expected a digit in floating point exponent}}
var fl_r: Float = 0x1.0fp // expected-error {{expected a digit in floating point exponent}}
var fl_s: Float = 0x1.0fp+ // expected-error {{expected a digit in floating point exponent}}
var fl_t: Float = 0x1.p // expected-error {{value of type 'Int' has no member 'p'}}
var fl_u: Float = 0x1.p2 // expected-error {{value of type 'Int' has no member 'p2'}}
var fl_v: Float = 0x1.p+ // expected-error {{'+' is not a postfix unary operator}}
var fl_w: Float = 0x1.p+2 // expected-error {{value of type 'Int' has no member 'p'}}

var if1: Double = 1.0 + 4  // integer literal ok as double.
var if2: Float = 1.0 + 4  // integer literal ok as float.

var fl_separator1: Double = 1_.2_
var fl_separator2: Double = 1_000.2_
var fl_separator3: Double = 1_000.200_001
var fl_separator4: Double = 1_000.200_001e1_
var fl_separator5: Double = 1_000.200_001e1_000
var fl_separator6: Double = 1_000.200_001e1_000
var fl_separator7: Double = 0x1_.0FFF_p1_
var fl_separator8: Double = 0x1_0000.0FFF_ABCDp10_001

var fl_bad_separator1: Double = 1e_ // expected-error {{expected a digit in floating point exponent}}
var fl_bad_separator2: Double = 0x1p_ // expected-error {{expected a digit in floating point exponent}} expected-error{{'_' can only appear in a pattern or on the left side of an assignment}} expected-error {{consecutive statements on a line must be separated by ';'}} {{37-37=;}}

//===----------------------------------------------------------------------===//
// String Literals
//===----------------------------------------------------------------------===//

var st_a = ""
var st_b: String = ""
var st_c = "asdfasd    // expected-error {{unterminated string literal}}

var st_d = " \t\n\r\"\'\\  "  // Valid simple escapes
var st_e = " \u{12}\u{0012}\u{00000078} "  // Valid unicode escapes
var st_u1 = " \u{1} "
var st_u2 = " \u{123} "
var st_u3 = " \u{1234567} " // expected-error {{invalid unicode scalar}}
var st_u4 = " \q "  // expected-error {{invalid escape sequence in literal}}

var st_u5 = " \u{FFFFFFFF} "  // expected-error {{invalid unicode scalar}}
var st_u6 = " \u{D7FF} \u{E000} "  // Fencepost UTF-16 surrogate pairs.
var st_u7 = " \u{D800} "  // expected-error {{invalid unicode scalar}}
var st_u8 = " \u{DFFF} "  // expected-error {{invalid unicode scalar}}
var st_u10 = " \u{0010FFFD} "  // Last valid codepoint, 0xFFFE and 0xFFFF are reserved in each plane
var st_u11 = " \u{00110000} "  // expected-error {{invalid unicode scalar}}

func stringliterals(_ d: [String: Int]) {

  // rdar://11385385
  let x = 4
  "Hello \(x+1) world"  // expected-warning {{expression of type 'String' is unused}}
  
  "Error: \(x+1"; // expected-error {{unterminated string literal}}
  
  "Error: \(x+1   // expected-error {{unterminated string literal}}
  ;    // expected-error {{';' statements are not allowed}}

  // rdar://14050788 [DF] String Interpolations can't contain quotes
  "test \("nested")"
  "test \("\("doubly nested")")"
  "test \(d["hi"])"
  "test \("quoted-paren )")"
  "test \("quoted-paren (")"
  "test \("\\")"
  "test \("\n")"
  "test \("\")" // expected-error {{unterminated string literal}}

  "test \
  // expected-error @-1 {{unterminated string literal}} expected-error @-1 {{invalid escape sequence in literal}}
  "test \("\
  // expected-error @-1 {{unterminated string literal}}
  "test newline \("something" +
    "something else")"
  // expected-error @-2 {{unterminated string literal}} expected-error @-1 {{unterminated string literal}}

  // FIXME: bad diagnostics.
  // expected-warning @+1 {{initialization of variable 'x2' was never used; consider replacing with assignment to '_' or removing it}}
  /* expected-error {{unterminated string literal}} expected-error {{expected ',' separator}} expected-error {{expected ',' separator}} expected-note {{to match this opening '('}}  */ var x2 : () = ("hello" + "
  ; // expected-error {{expected expression in list of expressions}}
} // expected-error {{expected ')' in expression list}}

func testSingleQuoteStringLiterals() {
  _ = 'abc' // expected-error{{single-quoted string literal found, use '"'}}{{7-12="abc"}}
  _ = 'abc' + "def" // expected-error{{single-quoted string literal found, use '"'}}{{7-12="abc"}}

  _ = 'ab\nc' // expected-error{{single-quoted string literal found, use '"'}}{{7-14="ab\\nc"}}

  _ = "abc\('def')" // expected-error{{single-quoted string literal found, use '"'}}{{13-18="def"}}

  _ = "abc' // expected-error{{unterminated string literal}}
  _ = 'abc" // expected-error{{unterminated string literal}}
  _ = "a'c"

  _ = 'ab\'c' // expected-error{{single-quoted string literal found, use '"'}}{{7-14="ab'c"}}

  _ = 'ab"c' // expected-error{{single-quoted string literal found, use '"'}}{{7-13="ab\\"c"}}
  _ = 'ab\"c' // expected-error{{single-quoted string literal found, use '"'}}{{7-14="ab\\"c"}}
  _ = 'ab\\"c' // expected-error{{single-quoted string literal found, use '"'}}{{7-15="ab\\\\\\"c"}}
}

// <rdar://problem/17128913>
var s = ""
s.append(contentsOf: ["x"])

//===----------------------------------------------------------------------===//
// InOut arguments
//===----------------------------------------------------------------------===//

func takesInt(_ x: Int) {}
func takesExplicitInt(_ x: inout Int) { }

func testInOut(_ arg: inout Int) {
  var x: Int
  takesExplicitInt(x) // expected-error{{passing value of type 'Int' to an inout parameter requires explicit '&'}} {{20-20=&}}
  takesExplicitInt(&x)
  takesInt(&x) // expected-error{{'&' used with non-inout argument of type 'Int'}}
  var y = &x // expected-error{{'&' can only appear immediately in a call argument list}} \
             // expected-error {{type 'inout Int' of variable is not materializable}}
  var z = &arg // expected-error{{'&' can only appear immediately in a call argument list}} \
             // expected-error {{type 'inout Int' of variable is not materializable}}

  takesExplicitInt(5) // expected-error {{cannot pass immutable value as inout argument: literals are not mutable}}
}

//===----------------------------------------------------------------------===//
// Conversions
//===----------------------------------------------------------------------===//

var pi_f: Float
var pi_d: Double

struct SpecialPi {} // Type with no implicit construction.

var pi_s: SpecialPi

func getPi() -> Float {} // expected-note 3 {{found this candidate}}
func getPi() -> Double {} // expected-note 3 {{found this candidate}}
func getPi() -> SpecialPi {}

enum Empty { }

extension Empty {
  init(_ f: Float) { }
}

func conversionTest(_ a: inout Double, b: inout Int) {
  var f: Float
  var d: Double
  a = Double(b)
  a = Double(f)
  a = Double(d) // no-warning
  b = Int(a)
  f = Float(b)

  var pi_f1 = Float(pi_f)
  var pi_d1 = Double(pi_d)
  var pi_s1 = SpecialPi(pi_s) // expected-error {{argument passed to call that takes no arguments}}

  var pi_f2 = Float(getPi()) // expected-error {{ambiguous use of 'getPi()'}}
  var pi_d2 = Double(getPi()) // expected-error {{ambiguous use of 'getPi()'}}
  var pi_s2: SpecialPi = getPi() // no-warning
  
  var float = Float.self
  var pi_f3 = float.init(getPi()) // expected-error {{ambiguous use of 'getPi()'}}
  var pi_f4 = float.init(pi_f)

  var e = Empty(f)
  var e2 = Empty(d) // expected-error{{cannot convert value of type 'Double' to expected argument type 'Float'}}
  var e3 = Empty(Float(d))
}

struct Rule { // expected-note {{'init(target:dependencies:)' declared here}}
  var target: String
  var dependencies: String
}

var ruleVar: Rule
ruleVar = Rule("a") // expected-error {{missing argument for parameter 'dependencies' in call}}


class C {
  var x: C?
  init(other: C?) { x = other }

  func method() {}
}

_ = C(3) // expected-error {{missing argument label 'other:' in call}}
_ = C(other: 3) // expected-error {{cannot convert value of type 'Int' to expected argument type 'C?'}}

//===----------------------------------------------------------------------===//
// Unary Operators
//===----------------------------------------------------------------------===//

func unaryOps(_ i8: inout Int8, i64: inout Int64) {
  i8 = ~i8
  i64 += 1
  i8 -= 1

  Int64(5) += 1 // expected-error{{left side of mutating operator isn't mutable: function call returns immutable value}}
  
  // <rdar://problem/17691565> attempt to modify a 'let' variable with ++ results in typecheck error not being able to apply ++ to Float
  let a = i8 // expected-note {{change 'let' to 'var' to make it mutable}} {{3-6=var}}
  a += 1 // expected-error {{left side of mutating operator isn't mutable: 'a' is a 'let' constant}}
  
  var b : Int { get { }}
  b += 1  // expected-error {{left side of mutating operator isn't mutable: 'b' is a get-only property}}
}

//===----------------------------------------------------------------------===//
// Iteration
//===----------------------------------------------------------------------===//

func..<(x: Double, y: Double) -> Double {
   return x + y
}

func iterators() {
  _ = 0..<42
  _ = 0.0..<42.0
}

//===----------------------------------------------------------------------===//
// Magic literal expressions
//===----------------------------------------------------------------------===//

func magic_literals() {
  _ = __FILE__  // expected-error {{__FILE__ has been replaced with #file in Swift 3}}
  _ = __LINE__  // expected-error {{__LINE__ has been replaced with #line in Swift 3}}
  _ = __COLUMN__  // expected-error {{__COLUMN__ has been replaced with #column in Swift 3}}
  _ = __DSO_HANDLE__  // expected-error {{__DSO_HANDLE__ has been replaced with #dsohandle in Swift 3}}

  _ = #file
  _ = #line + #column
  var _: UInt8 = #line + #column
}

//===----------------------------------------------------------------------===//
// lvalue processing
//===----------------------------------------------------------------------===//


infix operator +-+=
@discardableResult
func +-+= (x: inout Int, y: Int) -> Int { return 0}

func lvalue_processing() {
  var i = 0
  i += 1   // obviously ok

  var fn = (+-+=)

  var n = 42
  fn(n, 12)  // expected-error {{passing value of type 'Int' to an inout parameter requires explicit '&'}} {{6-6=&}}
  fn(&n, 12) // expected-warning {{result of call is unused, but produces 'Int'}}

  n +-+= 12

  (+-+=)(&n, 12)  // ok.
  (+-+=)(n, 12) // expected-error {{passing value of type 'Int' to an inout parameter requires explicit '&'}} {{10-10=&}}
}

struct Foo {
  func method() {}
}

func test() {
  var x = Foo()

  // rdar://15708430
  (&x).method()  // expected-error {{'inout Foo' is not convertible to 'Foo'}}
}


// Unused results.
func unusedExpressionResults() {
  // Unused l-value
  _ // expected-error{{'_' can only appear in a pattern or on the left side of an assignment}}


  // <rdar://problem/20749592> Conditional Optional binding hides compiler error
  let optionalc:C? = nil
  optionalc?.method()  // ok
  optionalc?.method  // expected-error {{expression resolves to an unused function}}
}




//===----------------------------------------------------------------------===//
// Collection Literals
//===----------------------------------------------------------------------===//

func arrayLiterals() { 
  let _ = [1,2,3]
  let _ : [Int] = []
  let _ = []  // expected-error {{empty collection literal requires an explicit type}}
}

func dictionaryLiterals() {
  let _ = [1 : "foo",2 : "bar",3 : "baz"]
  let _: Dictionary<Int, String> = [:]
  let _ = [:]  // expected-error {{empty collection literal requires an explicit type}}
}

func invalidDictionaryLiteral() {
  // FIXME: lots of unnecessary diagnostics.

  var a = [1: ; // expected-error {{expected value in dictionary literal}} expected-error 2{{expected ',' separator}} {{14-14=,}} {{14-14=,}} expected-error {{expected key expression in dictionary literal}} expected-error {{expected ']' in container literal expression}} expected-note {{to match this opening '['}}
  var b = [1: ;] // expected-error {{expected value in dictionary literal}} expected-error 2{{expected ',' separator}} {{14-14=,}} {{14-14=,}} expected-error {{expected key expression in dictionary literal}}
  var c = [1: "one" ;] // expected-error {{expected key expression in dictionary literal}} expected-error 2{{expected ',' separator}} {{20-20=,}} {{20-20=,}}
  var d = [1: "one", ;] // expected-error {{expected key expression in dictionary literal}} expected-error {{expected ',' separator}} {{21-21=,}}
  var e = [1: "one", 2] // expected-error {{expected ':' in dictionary literal}}
  var f = [1: "one", 2 ;] // expected-error 2{{expected ',' separator}} {{23-23=,}} {{23-23=,}} expected-error 1{{expected key expression in dictionary literal}} expected-error {{expected ':' in dictionary literal}}
  var g = [1: "one", 2: ;] // expected-error {{expected value in dictionary literal}} expected-error 2{{expected ',' separator}} {{24-24=,}} {{24-24=,}} expected-error {{expected key expression in dictionary literal}}
}

    
// FIXME: The issue here is a type compatibility problem, there is no ambiguity.
[4].joined(separator: [1]) // expected-error {{type of expression is ambiguous without more context}}
[4].joined(separator: [[[1]]]) // expected-error {{type of expression is ambiguous without more context}}

//===----------------------------------------------------------------------===//
// nil/metatype comparisons
//===----------------------------------------------------------------------===//
_ = Int.self == nil  // expected-warning {{comparing non-optional value of type 'Any.Type' to nil always returns false}}
_ = nil == Int.self  // expected-warning {{comparing non-optional value of type 'Any.Type' to nil always returns false}}
_ = Int.self != nil  // expected-warning {{comparing non-optional value of type 'Any.Type' to nil always returns true}}
_ = nil != Int.self  // expected-warning {{comparing non-optional value of type 'Any.Type' to nil always returns true}}

// <rdar://problem/19032294> Disallow postfix ? when not chaining
func testOptionalChaining(_ a : Int?, b : Int!, c : Int??) {
  _ = a?    // expected-error {{optional chain has no effect, expression already produces 'Int?'}} {{8-9=}}
  _ = a?.customMirror

  _ = b?   // expected-error {{'?' must be followed by a call, member lookup, or subscript}}
  _ = b?.customMirror

  var _: Int? = c?   // expected-error {{'?' must be followed by a call, member lookup, or subscript}}
}


// <rdar://problem/19657458> Nil Coalescing operator (??) should have a higher precedence
func testNilCoalescePrecedence(cond: Bool, a: Int?, r: CountableClosedRange<Int>?) {
  // ?? should have higher precedence than logical operators like || and comparisons.
  if cond || (a ?? 42 > 0) {}  // Ok.
  if (cond || a) ?? 42 > 0 {}  // expected-error {{cannot be used as a boolean}} {{15-15=(}} {{16-16= != nil)}}
  if (cond || a) ?? (42 > 0) {}  // expected-error {{cannot be used as a boolean}} {{15-15=(}} {{16-16= != nil)}}

  if cond || a ?? 42 > 0 {}    // Parses as the first one, not the others.


  // ?? should have lower precedence than range and arithmetic operators.
  let r1 = r ?? (0...42) // ok
  let r2 = (r ?? 0)...42 // not ok: expected-error {{binary operator '??' cannot be applied to operands of type 'CountableClosedRange<Int>?' and 'Int'}}
  // expected-note @-1 {{overloads for '??' exist with these partially matching parameter lists:}}
  let r3 = r ?? 0...42 // parses as the first one, not the second.
  
  
  // <rdar://problem/27457457> [Type checker] Diagnose unsavory optional injections
  // Accidental optional injection for ??.
  let i = 42
  _ = i ?? 17 // expected-warning {{left side of nil coalescing operator '??' has non-optional type 'Int', so the right side is never used}} {{9-15=}}
}

// <rdar://problem/19772570> Parsing of as and ?? regressed
func testOptionalTypeParsing(_ a : AnyObject) -> String {
  return a as? String ?? "default name string here"
}

func testParenExprInTheWay() {
  let x = 42
  
  if x & 4.0 {}  // expected-error {{binary operator '&' cannot be applied to operands of type 'Int' and 'Double'}} expected-note {{expected an argument list of type '(Int, Int)'}}

  if (x & 4.0) {}   // expected-error {{binary operator '&' cannot be applied to operands of type 'Int' and 'Double'}} expected-note {{expected an argument list of type '(Int, Int)'}}

  if !(x & 4.0) {}  // expected-error {{no '&' candidates produce the expected contextual result type 'Bool'}}
  //expected-note @-1 {{overloads for '&' exist with these result types: UInt8, Int8, UInt16, Int16, UInt32, Int32, UInt64, Int64, UInt, Int}}

  
  if x & x {} // expected-error {{'Int' is not convertible to 'Bool'}}
}

// <rdar://problem/21352576> Mixed method/property overload groups can cause a crash during constraint optimization
public struct TestPropMethodOverloadGroup {
    public typealias Hello = String
    public let apply:(Hello) -> Int
    public func apply(_ input:Hello) -> Int {
        return apply(input)
    }
}


// <rdar://problem/18496742> Passing ternary operator expression as inout crashes Swift compiler
func inoutTests(_ arr: inout Int) {
  var x = 1, y = 2
  (true ? &x : &y)  // expected-error 2 {{'&' can only appear immediately in a call argument list}}
  // expected-warning @-1 {{expression of type 'inout Int' is unused}}
  let a = (true ? &x : &y)  // expected-error 2 {{'&' can only appear immediately in a call argument list}}
  // expected-error @-1 {{type 'inout Int' of variable is not materializable}}

  inoutTests(true ? &x : &y);  // expected-error 2 {{'&' can only appear immediately in a call argument list}}

  &_ // expected-error {{expression type 'inout _' is ambiguous without more context}}

  inoutTests((&x))   // expected-error {{'&' can only appear immediately in a call argument list}}
  inoutTests(&x)
  
  // <rdar://problem/17489894> inout not rejected as operand to assignment operator
  &x += y  // expected-error {{'&' can only appear immediately in a call argument list}}

  // <rdar://problem/23249098>
  func takeAny(_ x: Any) {}
  takeAny(&x) // expected-error{{'&' used with non-inout argument of type 'Any'}}
  func takeManyAny(_ x: Any...) {}
  takeManyAny(&x) // expected-error{{'&' used with non-inout argument of type 'Any'}}
  takeManyAny(1, &x) // expected-error{{'&' used with non-inout argument of type 'Any'}}
  func takeIntAndAny(_ x: Int, _ y: Any) {}
  takeIntAndAny(1, &x) // expected-error{{'&' used with non-inout argument of type 'Any'}}
}


// <rdar://problem/20802757> Compiler crash in default argument & inout expr
var g20802757 = 2
func r20802757(_ z: inout Int = &g20802757) { // expected-error {{'&' can only appear immediately in a call argument list}}
  print(z)
}

_ = _.foo // expected-error {{type of expression is ambiguous without more context}}

// <rdar://problem/22211854> wrong arg list crashing sourcekit
func r22211854() {
    func f(_ x: Int, _ y: Int, _ z: String = "") {} // expected-note 2 {{'f' declared here}}
    func g<T>(_ x: T, _ y: T, _ z: String = "") {} // expected-note 2 {{'g' declared here}}

    f(1) // expected-error{{missing argument for parameter #2 in call}}
    g(1) // expected-error{{missing argument for parameter #2 in call}}
    func h() -> Int { return 1 }
    f(h() == 1) // expected-error{{missing argument for parameter #2 in call}}
    g(h() == 1) // expected-error{{missing argument for parameter #2 in call}}
}

// <rdar://problem/22348394> Compiler crash on invoking function with labeled defaulted param with non-labeled argument
func r22348394() {
  func f(x: Int = 0) { }
  f(Int(3)) // expected-error{{missing argument label 'x:' in call}}
}

// <rdar://problem/23185177> Compiler crashes in Assertion failed: ((AllowOverwrite || !E->hasLValueAccessKind()) && "l-value access kind has already been set"), function visit
protocol P { var y: String? { get } }
func r23185177(_ x: P?) -> [String] {
  return x?.y // expected-error{{cannot convert return expression of type 'String?' to return type '[String]'}}
}

// <rdar://problem/22913570> Miscompile: wrong argument parsing when calling a function in swift2.0
func r22913570() {
  func f(_ from: Int = 0, to: Int) {} // expected-note {{'f(_:to:)' declared here}}
  f(1 + 1) // expected-error{{missing argument for parameter 'to' in call}}
}


// <rdar://problem/23708702> Emit deprecation warnings for ++/-- in Swift 2.2
func swift22_deprecation_increment_decrement() {
  var i = 0
  var f = 1.0

  i++     // expected-error {{'++' is unavailable}} {{4-6= += 1}}
  --i     // expected-error {{'--' is unavailable}} {{3-5=}} {{6-6= -= 1}}
  _ = i++ // expected-error {{'++' is unavailable}}

  ++f     // expected-error {{'++' is unavailable}} {{3-5=}} {{6-6= += 1}}
  f--     // expected-error {{'--' is unavailable}} {{4-6= -= 1}}
  _ = f-- // expected-error {{'--' is unavailable}} {{none}}

  // <rdar://problem/24530312> Swift ++fix-it produces bad code in nested expressions
  // This should not get a fixit hint.
  var j = 2
  i = ++j   // expected-error {{'++' is unavailable}} {{none}}
}

// SR-628 mixing lvalues and rvalues in tuple expression
var x = 0
var y = 1
let _ = (x, x + 1).0
let _ = (x, 3).1
(x,y) = (2,3)
(x,4) = (1,2) // expected-error {{cannot assign to value: literals are not mutable}}
(x,y).1 = 7 // expected-error {{cannot assign to immutable expression of type 'Int'}}
x = (x,(3,y)).1.1


// SE-0101 sizeof family functions are reconfigured into MemoryLayout
protocol Pse0101 {
  associatedtype Value
  func getIt() -> Value
}
class Cse0101<U> {
  typealias T = U
  var val: U { fatalError() }
}
func se0101<P: Pse0101>(x: Cse0101<P>) {
  // Note: The first case is actually not allowed, but it is very common and can be compiled currently.
  _ = sizeof(P) // expected-error {{'sizeof' is unavailable: use MemoryLayout<T>.size instead.}} {{7-14=MemoryLayout<}} {{15-16=>.size}} {{none}}
                // expected-warning@-1 {{missing '.self' for reference to metatype of type 'P'}}
  _ = sizeof(P.self) // expected-error {{'sizeof' is unavailable: use MemoryLayout<T>.size instead.}} {{7-14=MemoryLayout<}} {{15-21=>.size}} {{none}}
  _ = sizeof(P.Value.self) // expected-error {{'sizeof' is unavailable: use MemoryLayout<T>.size instead.}} {{7-14=MemoryLayout<}} {{21-27=>.size}} {{none}}
  _ = sizeof(Cse0101<P>.self) // expected-error {{'sizeof' is unavailable: use MemoryLayout<T>.size instead.}} {{7-14=MemoryLayout<}} {{24-30=>.size}} {{none}}
  _ = alignof(Cse0101<P>.T.self) // expected-error {{'alignof' is unavailable: use MemoryLayout<T>.alignment instead.}} {{7-15=MemoryLayout<}} {{27-33=>.alignment}} {{none}}
  _ = strideof(P.Type.self) // expected-error {{'strideof' is unavailable: use MemoryLayout<T>.stride instead.}} {{7-16=MemoryLayout<}} {{22-28=>.stride}} {{none}}
  _ = sizeof(type(of: x)) // expected-error {{'sizeof' is unavailable: use MemoryLayout<T>.size instead.}} {{7-26=MemoryLayout<Cse0101<P>>.size}} {{none}}
}
