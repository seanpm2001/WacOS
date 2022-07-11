// RUN: %target-resilience-test
// REQUIRES: executable_test

import StdlibUnittest
import class_resilient_superclass_methods


var SuperclassMethodsTest = TestSuite("SuperclassMethods")

SuperclassMethodsTest.test("AddInterposingMethod") {
  do {
    class Leaf : AddInterposingMethod {
      override func method() -> String {
        return super.method()
      }
      override class func classMethod() -> String {
        return super.classMethod()
      }
      func newMethod() -> String {
        return "still works"
      }
    }
    if getVersion() == 0 {
      expectEqual(Leaf().method(), "Base.method()")
      expectEqual(Leaf.classMethod(), "Base.classMethod()")
    } else {
      expectEqual(Leaf().method(), "AddInterposingMethod.method()")
      expectEqual(Leaf.classMethod(), "AddInterposingMethod.classMethod()")
    }
    expectEqual(Leaf().newMethod(), "still works")
  }
}

SuperclassMethodsTest.test("RemoveInterposingMethod") {
  do {
    class Leaf : RemoveInterposingMethod {
      override func method() -> String {
        return super.method()
      }
      override class func classMethod() -> String {
        return super.classMethod()
      }
      func newMethod() -> String {
        return "still works"
      }
    }
    if getVersion() == 0 {
      expectEqual(Leaf().method(), "RemoveInterposingMethod.method()")
      expectEqual(Leaf.classMethod(), "RemoveInterposingMethod.classMethod()")
    } else {
      expectEqual(Leaf().method(), "Base.method()")
      expectEqual(Leaf.classMethod(), "Base.classMethod()")
    }
    expectEqual(Leaf().newMethod(), "still works")
  }
}

SuperclassMethodsTest.test("InsertSuperclass") {
  do {
    class Leaf : InsertSuperclass {
      override func method() -> String {
        return super.method()
      }
      override class func classMethod() -> String {
        return super.classMethod()
      }
      func newMethod() -> String {
        return "still works"
      }
    }
    if getVersion() == 0 {
      expectEqual(Leaf().method(), "Base.method()")
      expectEqual(Leaf().nonOverriddenMethod(), "Base.nonOverriddenMethod()")
      expectEqual(Leaf.classMethod(), "Base.classMethod()")
    } else {
      expectEqual(Leaf().method(), "InBetween.method()")
      expectEqual(Leaf().nonOverriddenMethod(), "Base.nonOverriddenMethod()")
      expectEqual(Leaf.classMethod(), "InBetween.classMethod()")
    }
    expectEqual(Leaf().newMethod(), "still works")
  }
}

runAllTests()

