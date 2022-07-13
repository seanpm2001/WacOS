// RUN: rm -rf %t
// RUN: mkdir -p %t
// RUN: %target-build-swift -swift-version 4 %s -o %t/a.out -enforce-exclusivity=checked -Onone
//
// RUN: %target-run %t/a.out
// REQUIRES: executable_test

// Tests for traps at run time when enforcing exclusive access.

import StdlibUnittest
import SwiftPrivatePthreadExtras

struct X {
  var i = 7
}

/// Calling this function will begin a read access to the variable referred to
/// in the first parameter that lasts for the duration of the call. Any
/// accesses in the closure will therefore be nested inside the outer read.
func readAndPerform<T>(_ _: UnsafePointer<T>, closure: () ->()) {
  closure()
}

/// Begin a modify access to the first parameter and call the closure inside it.
func modifyAndPerform<T>(_ _: UnsafeMutablePointer<T>, closure: () ->()) {
  closure()
}

var globalX = X()

var ExclusiveAccessTestSuite = TestSuite("ExclusiveAccess")

ExclusiveAccessTestSuite.test("Read") {
  let l = globalX // no-trap
  _blackHole(l)
}

// It is safe for a read access to overlap with a read.
ExclusiveAccessTestSuite.test("ReadInsideRead") {
  readAndPerform(&globalX) {
    let l = globalX // no-trap
    _blackHole(l)
  }
}

ExclusiveAccessTestSuite.test("ModifyInsideRead")
  .skip(.custom(
    { _isFastAssertConfiguration() },
    reason: "this trap is not guaranteed to happen in -Ounchecked"))
  .crashOutputMatches("Previous access (a read) started at")
  .crashOutputMatches("Current access (a modification) started at")
  .code
{
  readAndPerform(&globalX) {
    expectCrashLater()
    globalX = X()
  }
}

ExclusiveAccessTestSuite.test("ReadInsideModify")
  .skip(.custom(
    { _isFastAssertConfiguration() },
    reason: "this trap is not guaranteed to happen in -Ounchecked"))
  .crashOutputMatches("Previous access (a modification) started at")
  .crashOutputMatches("Current access (a read) started at")
  .code
{
  modifyAndPerform(&globalX) {
    expectCrashLater()
    let l = globalX
    _blackHole(l)
  }
}

ExclusiveAccessTestSuite.test("ModifyInsideModify")
  .skip(.custom(
    { _isFastAssertConfiguration() },
    reason: "this trap is not guaranteed to happen in -Ounchecked"))
  .crashOutputMatches("Previous access (a modification) started at")
  .crashOutputMatches("Current access (a modification) started at")
  .code
{
  modifyAndPerform(&globalX) {
    expectCrashLater()
    globalX.i = 12
  }
}

var globalOtherX = X()

// It is safe for two modifications of different variables
// to overlap.
ExclusiveAccessTestSuite.test("ModifyInsideModifyOfOther") {
  modifyAndPerform(&globalOtherX) {
    globalX.i = 12 // no-trap
  }
}

// The access durations for these two modifications do not overlap
ExclusiveAccessTestSuite.test("ModifyFollowedByModify") {
  globalX = X()
  _blackHole(())

  globalX = X() // no-trap
}

// FIXME: This should be covered by static diagnostics.
// Once this radar is fixed, confirm that a it is covered by a static diagnostic
// (-verify) test in exclusivity_static_diagnostics.sil.
// <rdar://problem/32061282> Enforce exclusive access in noescape closures.
//
//ExclusiveAccessTestSuite.test("ClosureCaptureModifyModify")
//.skip(.custom(
//    { _isFastAssertConfiguration() },
//    reason: "this trap is not guaranteed to happen in -Ounchecked"))
//  .crashOutputMatches("Previous access (a modification) started at")
//  .crashOutputMatches("Current access (a modification) started at")
//  .code
//{
//  var x = X()
//  modifyAndPerform(&x) {
//    expectCrashLater()
//    x.i = 12
//  }
//}

// FIXME: This should be covered by static diagnostics.
// Once this radar is fixed, confirm that a it is covered by a static diagnostic
// (-verify) test in exclusivity_static_diagnostics.sil.
// <rdar://problem/32061282> Enforce exclusive access in noescape closures.
//
//ExclusiveAccessTestSuite.test("ClosureCaptureReadModify")
//.skip(.custom(
//    { _isFastAssertConfiguration() },
//    reason: "this trap is not guaranteed to happen in -Ounchecked"))
//  .crashOutputMatches("Previous access (a read) started at")
//  .crashOutputMatches("Current access (a modification) started at")
//  .code
//{
//  var x = X()
//  modifyAndPerform(&x) {
//    expectCrashLater()
//    _blackHole(x.i)
//  }
//}

// FIXME: This should be covered by static diagnostics.
// Once this radar is fixed, confirm that a it is covered by a static diagnostic
// (-verify) test in exclusivity_static_diagnostics.sil.
// <rdar://problem/32061282> Enforce exclusive access in noescape closures.
//
//ExclusiveAccessTestSuite.test("ClosureCaptureModifyRead")
//.skip(.custom(
//    { _isFastAssertConfiguration() },
//    reason: "this trap is not guaranteed to happen in -Ounchecked"))
//  .crashOutputMatches("Previous access (a modification) started at")
//  .crashOutputMatches("Current access (a read) started at")
//  .code
//{
//  var x = X()
//  readAndPerform(&x) {
//    expectCrashLater()
//    x.i = 12
//  }
//}

ExclusiveAccessTestSuite.test("ClosureCaptureReadRead") {
  var x = X()
  readAndPerform(&x) {
    _blackHole(x.i) // no-trap
  }
}

// Test for per-thread enforcement. Don't trap when two different threads
// have overlapping accesses
ExclusiveAccessTestSuite.test("PerThreadEnforcement") {
  modifyAndPerform(&globalX) {
    let (_, otherThread) = _stdlib_pthread_create_block(nil, { (_ : Void) -> () in
      globalX.i = 12 // no-trap
      return ()
    }, ())

    _ = _stdlib_pthread_join(otherThread!, Void.self)
  }
}

// Helpers
func doOne(_ f: () -> ()) { f() }

func doTwo(_ f1: ()->(), _ f2: ()->()) { f1(); f2() }

// No crash.
ExclusiveAccessTestSuite.test("WriteNoescapeWrite") {
  var x = 3
  let c = { x = 7 }
  // Inside may-escape closure `c`: [read] [dynamic]
  // Inside never-escape closure: [modify] [dynamic]
  doTwo(c, { x = 42 })
  _blackHole(x)
}

// No crash.
ExclusiveAccessTestSuite.test("InoutReadEscapeRead") {
  var x = 3
  let c = { let y = x; _blackHole(y) }
  readAndPerform(&x, closure: c)
  _blackHole(x)
}

ExclusiveAccessTestSuite.test("InoutReadEscapeWrite")
  .skip(.custom(
    { _isFastAssertConfiguration() },
    reason: "this trap is not guaranteed to happen in -Ounchecked"))
  .crashOutputMatches("Previous access (a read) started at")
  .crashOutputMatches("Current access (a modification) started at")
  .code
{
  var x = 3
  let c = { x = 42 }
  expectCrashLater()
  readAndPerform(&x, closure: c) 
  _blackHole(x)
}

ExclusiveAccessTestSuite.test("InoutWriteEscapeRead")
  .skip(.custom(
    { _isFastAssertConfiguration() },
    reason: "this trap is not guaranteed to happen in -Ounchecked"))
  .crashOutputMatches("Previous access (a modification) started at")
  .crashOutputMatches("Current access (a read) started at")
  .code
{
  var x = 3
  let c = { let y = x; _blackHole(y) }
  expectCrashLater()
  modifyAndPerform(&x, closure: c)
  _blackHole(x)
}

ExclusiveAccessTestSuite.test("InoutWriteEscapeWrite")
  .skip(.custom(
    { _isFastAssertConfiguration() },
    reason: "this trap is not guaranteed to happen in -Ounchecked"))
  .crashOutputMatches("Previous access (a modification) started at")
  .crashOutputMatches("Current access (a modification) started at")
  .code
{
  var x = 3
  let c = { x = 42 }
  expectCrashLater()
  modifyAndPerform(&x, closure: c)
  _blackHole(x)
}

// No crash.
ExclusiveAccessTestSuite.test("InoutReadNoescapeRead") {
  var x = 3
  let c = { let y = x; _blackHole(y) }
  doOne { readAndPerform(&x, closure: c) }
}

ExclusiveAccessTestSuite.test("InoutReadNoescapeWrite")
  .skip(.custom(
    { _isFastAssertConfiguration() },
    reason: "this trap is not guaranteed to happen in -Ounchecked"))
  .crashOutputMatches("Previous access (a read) started at")
  .crashOutputMatches("Current access (a modification) started at")
  .code
{
  var x = 3
  let c = { x = 7 }
  expectCrashLater()
  doOne { readAndPerform(&x, closure: c) }
}

ExclusiveAccessTestSuite.test("InoutWriteEscapeRead")
  .skip(.custom(
    { _isFastAssertConfiguration() },
    reason: "this trap is not guaranteed to happen in -Ounchecked"))
  .crashOutputMatches("Previous access (a modification) started at")
  .crashOutputMatches("Current access (a read) started at")
  .code
{
  var x = 3
  let c = { let y = x; _blackHole(y) }
  expectCrashLater()
  doOne { modifyAndPerform(&x, closure: c) }
}

ExclusiveAccessTestSuite.test("InoutWriteEscapeWrite")
  .skip(.custom(
    { _isFastAssertConfiguration() },
    reason: "this trap is not guaranteed to happen in -Ounchecked"))
  .crashOutputMatches("Previous access (a modification) started at")
  .crashOutputMatches("Current access (a modification) started at")
  .code
{
  var x = 3
  let c = { x = 7 }
  expectCrashLater()
  doOne { modifyAndPerform(&x, closure: c) }
}

runAllTests()
