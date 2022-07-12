// RUN: %target-run-simple-swift | %FileCheck %s
// REQUIRES: executable_test

// REQUIRES: objc_interop

import Foundation

var hello : NSString = "Hello, world!"
// CHECK: Hello, world!
print(hello)

// CHECK: ello,
var helloStr: String = hello as String
let i1 = helloStr.index(helloStr.startIndex, offsetBy: 1)
let i2 = helloStr.index(helloStr.startIndex, offsetBy: 6)
print(String(helloStr[i1..<i2]))

var upperHello = hello.uppercased
// CHECK: HELLO, WORLD!
print(upperHello)

// Note: easy way to create an NSDictionary
var strings : NSString = "\"A\" = \"Foo\";\n\"B\" = \"Bar\";\n"
var dict  = strings.propertyListFromStringsFileFormat()!

// Subscripting an NSDictionary.
// CHECK: A -> Foo
print("A -> " + (dict["A"] as! String))
// CHECK: B -> Bar
print("B -> " + (dict["B"] as! String))

// Creating and subscripting an NSMutableArray
var array = NSMutableArray(capacity: 2)
hello = "Hello"
array[0] = hello
array[1] = "world" as NSString

// FIXME: NSString string interpolation doesn't work due to lack of
// overload resolution.
// CHECK: Hello, world!
print(array[0] as! NSString, terminator: "")
print(", ", terminator: "");
print(array[1] as! NSString, terminator: "")
print("!")

// Selectors
assert(NSString.instancesRespond(to: "init"))
assert(!NSString.instancesRespond(to: "wobble"))

// Array of strings
var array2 : NSArray = [hello, hello]

// Switch on strings
switch ("world" as NSString).uppercased {
case "WORLD":
  print("Found it\n", terminator: "")

default:
  assert(false, "didn't match")
}

// Test string comparisons.
func testComparisons() {
  let ns1: NSString = "foo"
  let ns2: NSString = "bar"

  let nms1: NSMutableString = "foo"
  let nms2: NSMutableString = "bar"

  let s1: String = "foo"
  let s2: String = "bar"

  // Test String [==,!=] String.
  if s1 == s2 { print("s1 == s2") }
  if s1 != s2 { print("s1 != s2") }
  if s1 == s1 { print("s1 == s1") }
  if s1 != s1 { print("s1 != s1") }

  // Test String [==,!=] NSString.
  // This test does not compile, as comparing these 2 types is ambiguous.
  // if s1 == ns2 { print("s1 == ns2") }
  // if s1 != ns2 { print("s1 != ns2") }
  // if s1 == ns1 { print("s1 == ns1") }
  // if s1 != ns1 { print("s1 != ns1") }

  // Test NSString [==,!=] String.
  // This test does not compile, as comparing these 2 types is ambiguous.
  // if ns1 == s2 { print("ns1 == s2") }
  // if ns1 != s2 { print("ns1 != s2") }
  // if ns1 == s1 { print("ns1 == s1") }
  // if ns1 != s1 { print("ns1 != s1") }

  // Test NString [==,!=] NString.
  if ns1 == ns2 { print("ns1 == ns2") }
  if ns1 != ns2 { print("ns1 != ns2") }
  if ns1 == ns1 { print("ns1 == ns1") }
  if ns1 != ns1 { print("ns1 != ns1") }

  // Test NSMutableString [==,!=] String.
  // This test does not compile, as comparing these 2 types is ambiguous.
  // if nms1 == s2 { print("nms1 == s2") }
  // if nms1 != s2 { print("nms1 != s2") }
  // if nms1 == s1 { print("nms1 == s1") }
  // if nms1 != s1 { print("nms1 != s1") }

  // Test NSMutableString [==,!=] NSMutableString.
  if nms1 == nms2 { print("nms1 == nms2") }
  if nms1 != nms2 { print("nms1 != nms2") }
  if nms1 == nms1 { print("nms1 == nms1") }
  if nms1 != nms1 { print("nms1 != nms1") }

  // Test NString [==,!=] NSMutableString.
  if ns1 == nms2 { print("ns1 == nms2") }
  if ns1 != nms2 { print("ns1 != nms2") }
  if ns1 == nms1 { print("ns1 == nms1") }
  if ns1 != nms1 { print("ns1 != nms1") }

  // Test NSMutableString [==,!=] NSString.
  if nms1 == ns2 { print("nms1 == ns2") }
  if nms1 != ns2 { print("nms1 != ns2") }
  if nms1 == ns1 { print("nms1 == ns1") }
  if nms1 != ns1 { print("nms1 != ns1") }
}

// CHECK: s1 != s2
// CHECK: s1 == s1
// CHECK: ns1 != ns2
// CHECK: ns1 == ns1
// CHECK: nms1 != nms2
// CHECK: nms1 == nms1
// CHECK: ns1 != nms2
// CHECK: ns1 == nms1
// CHECK: nms1 != ns2
// CHECK: nms1 == ns1
testComparisons()

// Test overlain variadic methods.
// CHECK-LABEL: Variadic methods:
print("Variadic methods:")
// Check that it works with bridged Strings.
// CHECK-NEXT: x y
print(NSString(format: "%@ %@", "x", "y"))
// Check that it works with bridged Arrays and Dictionaries.
// CHECK-NEXT: (
// CHECK-NEXT:   x
// CHECK-NEXT: ) {
// CHECK-NEXT:   y = z;
// CHECK-NEXT: }
print(NSString(format: "%@ %@", ["x"], ["y": "z"]))
// CHECK-NEXT: 1{{.*}}024,25
print(NSString(
  format: "%g",
  locale: Locale(identifier: "fr_FR"),
  1024.25
))
// CHECK-NEXT: x y z
print(("x " as NSString).appendingFormat("%@ %@", "y", "z"))
// CHECK-NEXT: a b c
let s = NSMutableString(string: "a ")
s.appendFormat("%@ %@", "b", "c")
print(s)

let m = NSMutableString.localizedStringWithFormat("<%@ %@>", "q", "r")
// CHECK-NEXT: <q r>
print(m)
m.append(" lever")
// CHECK-NEXT: <q r> lever
print(m)
