//===--- Builtins.swift - Tests for our Builtin wrappers ------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
// RUN: rm -rf %t && mkdir -p %t
//   note: building with -Onone to test debug-mode-only safety checks
// RUN: %target-build-swift %s -parse-stdlib -Xfrontend -disable-access-control -Onone -o %t/Builtins
// RUN: %target-run %t/Builtins
// REQUIRES: executable_test

import Swift
import SwiftShims
import StdlibUnittest


#if _runtime(_ObjC)
import Foundation
#endif

var tests = TestSuite("Builtins")

class X {}

tests.test("_isUnique/NativeObject") {
  var a: Builtin.NativeObject = Builtin.castToNativeObject(X())
  expectNotEqual(false, _isUnique_native(&a))
  var b = a
  expectFalse(_isUnique_native(&a))
  expectFalse(_isUnique_native(&b))
}

tests.test("_isUniquelyReferenced/OptionalNativeObject") {
  var a: Builtin.NativeObject? = Builtin.castToNativeObject(X())
  StdlibUnittest.expectTrue(_getBool(Builtin.isUnique(&a)))
  var b = a
  expectFalse(_getBool(Builtin.isUnique(&a)))
  expectFalse(_getBool(Builtin.isUnique(&b)))
  var x: Builtin.NativeObject? = nil
  expectFalse(_getBool(Builtin.isUnique(&x)))
}

#if _runtime(_ObjC)
class XObjC : NSObject {}

tests.test("_isUnique_native/SpareBitTrap")
  .skip(.custom(
    { !_isStdlibInternalChecksEnabled() },
    reason: "sanity checks are disabled in this build of stdlib"))
  .code {
  // Fake an ObjC pointer.
  var b = _makeObjCBridgeObject(X())
  expectCrashLater()
  _ = _isUnique_native(&b)
}

tests.test("_isUniqueOrPinned_native/SpareBitTrap")
  .skip(.custom(
    { !_isStdlibInternalChecksEnabled() },
    reason: "sanity checks are disabled in this build of stdlib"))
  .code {
  // Fake an ObjC pointer.
  var b = _makeObjCBridgeObject(X())
  expectCrashLater()
  _ = _isUniqueOrPinned_native(&b)
}

tests.test("_isUnique_native/NonNativeTrap")
  .skip(.custom(
    { !_isStdlibInternalChecksEnabled() },
    reason: "sanity checks are disabled in this build of stdlib"))
  .code {
  var x = XObjC()
  expectCrashLater()
  _ = _isUnique_native(&x)
}

tests.test("_isUniqueOrPinned_native/NonNativeTrap")
  .skip(.custom(
    { !_isStdlibInternalChecksEnabled() },
    reason: "sanity checks are disabled in this build of stdlib"))
  .code {
  var x = XObjC()
  expectCrashLater()
  _ = _isUniqueOrPinned_native(&x)
}
#endif // _ObjC

var x = 27

@inline(never)
func genint() -> Int {
  return x
}

tests.test("_assumeNonNegative") {
  let r = _assumeNonNegative(genint())
  expectEqual(r, 27)
}

var NoisyLifeCount = 0
var NoisyDeathCount = 0

protocol P {}

class Noisy : P {
  init() { NoisyLifeCount += 1 }
  deinit { NoisyDeathCount += 1}
}

struct Large : P {
  var a, b, c, d: Noisy

  init() {
    self.a = Noisy()
    self.b = Noisy()
    self.c = Noisy()
    self.d = Noisy()
  }
}

struct ContainsP { var p: P }

func exerciseArrayValueWitnesses<T>(_ value: T) {
  let buf = UnsafeMutablePointer<T>.allocate(capacity: 5)

  (buf + 0).initialize(to: value)
  (buf + 1).initialize(to: value)
  
  Builtin.copyArray(T.self, (buf + 2)._rawValue, buf._rawValue, 2._builtinWordValue)
  Builtin.takeArrayBackToFront(T.self, (buf + 1)._rawValue, buf._rawValue, 4._builtinWordValue)
  Builtin.takeArrayFrontToBack(T.self, buf._rawValue, (buf + 1)._rawValue, 4._builtinWordValue)
  Builtin.destroyArray(T.self, buf._rawValue, 4._builtinWordValue)

  buf.deallocate(capacity: 5)
}

tests.test("array value witnesses") {
  NoisyLifeCount = 0
  NoisyDeathCount = 0
  do {
    exerciseArrayValueWitnesses(44)
    exerciseArrayValueWitnesses(Noisy())
    exerciseArrayValueWitnesses(Noisy() as P)
    exerciseArrayValueWitnesses(Large())
    exerciseArrayValueWitnesses(Large() as P)
    exerciseArrayValueWitnesses(ContainsP(p: Noisy()))
    exerciseArrayValueWitnesses(ContainsP(p: Large()))
  }
  expectEqual(NoisyLifeCount, NoisyDeathCount)
}

protocol Classy : class {}
class A : Classy {}
class B : A {}
class C : B {}

tests.test("_getSuperclass") {
  expectEmpty(_getSuperclass(A.self))
  expectEmpty(_getSuperclass(Classy.self))
  expectNotEmpty(_getSuperclass(B.self))
  expectNotEmpty(_getSuperclass(C.self))
  expectTrue(_getSuperclass(B.self)! == A.self)
  expectTrue(_getSuperclass(C.self)! == B.self)
}

tests.test("type comparison") {
  class B {}
  class D : B {}
  
  let t1 = B.self
  let t1o = Optional(t1)
  let t2 = D.self
  let t2o = Optional(t2)

  expectTrue(t1 == t1)
  expectFalse(t1 != t1)
  expectTrue(t2 == t2)
  expectFalse(t2 != t2)
  
  expectFalse(t1 == t2)
  expectFalse(t2 == t1)
  expectTrue(t1 != t2)
  expectTrue(t2 != t1)

  expectTrue(t1 == t1o)
  expectFalse(t1 != t1o)
  expectTrue(t2 == t2o)
  expectFalse(t2 != t2o)
  
  expectFalse(t1 == t2o)
  expectFalse(t2 == t1o)
  expectTrue(t1 != t2o)
  expectTrue(t2 != t1o)
  
  expectTrue(t1o == t1)
  expectFalse(t1o != t1)
  expectTrue(t2o == t2)
  expectFalse(t2o != t2)
  
  expectFalse(t1o == t2)
  expectFalse(t2o == t1)
  expectTrue(t1o != t2)
  expectTrue(t2o != t1)

  expectTrue(t1o == t1o)
  expectFalse(t1o != t1o)
  expectTrue(t2o == t2o)
  expectFalse(t2o != t2o)
  
  expectFalse(t1o == t2o)
  expectFalse(t2o == t1o)
  expectTrue(t1o != t2o)
  expectTrue(t2o != t1o)

  let nil1 : B.Type? = nil
  let nil2 : D.Type? = nil

  expectTrue(nil1 == nil2)
  expectTrue(nil1 == nil1)
  
  expectTrue(nil1 == nil)
  expectTrue(nil == nil1)
  
  expectTrue(nil2 == nil)
  expectTrue(nil == nil2)
  
  expectFalse(t1 == nil1)
  expectFalse(nil1 == t1)
  
  expectFalse(t2 == nil1)
  expectFalse(nil1 == t2)
  
  expectFalse(t1 == nil2)
  expectFalse(nil2 == t1)
  
  expectFalse(t2 == nil2)
  expectFalse(nil2 == t2)

  expectFalse(t1o == nil)
  expectFalse(nil == t1o)
  
  expectFalse(t2o == nil)
  expectFalse(nil == t2o)
  
  expectFalse(t1o == nil1)
  expectFalse(nil1 == t1o)
  
  expectFalse(t2o == nil1)
  expectFalse(nil1 == t2o)
  
  expectFalse(t1o == nil2)
  expectFalse(nil2 == t1o)
  
  expectFalse(t2o == nil2)
  expectFalse(nil2 == t2o)
}

tests.test("_isPOD") {
  expectTrue(_isPOD(Int.self))
  expectFalse(_isPOD(X.self))
  expectFalse(_isPOD(P.self))
}

tests.test("_isOptional") {
  expectTrue(_isOptional(Optional<Int>.self))
  expectTrue(_isOptional(Optional<X>.self))
  expectTrue(_isOptional(Optional<P>.self))
  expectTrue(_isOptional(ImplicitlyUnwrappedOptional<P>.self))
  expectFalse(_isOptional(Int.self))
  expectFalse(_isOptional(X.self))
  expectFalse(_isOptional(P.self))
}

runAllTests()
