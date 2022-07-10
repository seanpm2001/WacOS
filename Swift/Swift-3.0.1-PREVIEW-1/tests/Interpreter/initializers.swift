// RUN: %target-run-simple-swift | %FileCheck %s
// REQUIRES: executable_test

// Test initialization and initializer inheritance.
var depth = 0

func printAtDepth(_ s: String) {
  for i in 0..<depth { print("*", terminator: "") }
  print(s)
}

class A {
  var int: Int
  var string: String

  convenience init() {
    printAtDepth("Starting A.init()")
    depth += 1
    self.init(int:5)
    depth -= 1
    printAtDepth("Ending A.init()")
  }

  convenience init(int i:Int) {
    printAtDepth("Starting A.init withInt(\(i))")
    depth += 1
    self.init(int:i, string:"hello")
    depth -= 1
    printAtDepth("Ending A.init withInt(\(i))")
  }

  init(int i:Int, string: String) {
    printAtDepth("Starting A.init withInt(\(i)) string(\(string))")
    self.int = i
    self.string = string
    printAtDepth("Ending A.init withInt(\(i)) string(\(string))")
  }

  deinit {
    printAtDepth("A.deinit")
  }
}

class B : A {
  var double: Double

  convenience override init(int i:Int, string: String) {
    printAtDepth("Starting B.init withInt(\(i)) string(\(string))")
    depth += 1
    self.init(int: i, string:string, double:3.14159)
    depth -= 1
    printAtDepth("Ending B.init withInt(\(i)) string(\(string))")
  }

  init(int i:Int, string: String, double:Double) {
    printAtDepth("Starting B.init withInt(\(i)) string(\(string)) double(\(double))")
    self.double = double
    depth += 1
    super.init(int: i, string: string)
    depth -= 1
    printAtDepth("Ending B.init withInt(\(i)) string(\(string)) double(\(double))")
  }

  deinit {
    printAtDepth("B.deinit")
  }
}

class C : B {
  override init(int i:Int, string: String, double: Double) {
    printAtDepth("Starting C.init withInt(\(i)) string(\(string)) double(\(double))")
    depth += 1
    super.init(int: i, string: string, double: double)
    depth -= 1
    printAtDepth("Ending C.init withInt(\(i)) string(\(string)) double(\(double))")
  }

  deinit {
    printAtDepth("C.deinit")
  }
}

print("-----Constructing C()-----")
// CHECK: Starting A.init()
// CHECK: *Starting A.init withInt(5)
// CHECK: **Starting B.init withInt(5) string(hello)
// CHECK: ***Starting C.init withInt(5) string(hello) double(3.14159)
// CHECK: ****Starting B.init withInt(5) string(hello) double(3.14159)
// CHECK: *****Starting A.init withInt(5) string(hello)
// CHECK: *****Ending A.init withInt(5) string(hello)
// CHECK: ****Ending B.init withInt(5) string(hello) double(3.14159)
// CHECK: ***Ending C.init withInt(5) string(hello) double(3.14159)
// CHECK: **Ending B.init withInt(5) string(hello)
// CHECK: *Ending A.init withInt(5)
// CHECK: Ending A.init()
// CHECK: C.deinit
// CHECK: B.deinit
// CHECK: A.deinit
C()

// rdar://problem/18877135

class Foo: ExpressibleByFloatLiteral {
  required init(floatLiteral: Float) { }

  func identify() { print("Foo") }
}

class Bar: Foo {
  override func identify() { print("Bar") }
}

let x: Bar = 1.0
x.identify() // CHECK-LABEL: Bar
