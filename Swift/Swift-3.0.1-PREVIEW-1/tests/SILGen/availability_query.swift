// RUN: %target-swift-frontend -emit-sil %s -target x86_64-apple-macosx10.50 -verify
// RUN: %target-swift-frontend -emit-silgen %s -target x86_64-apple-macosx10.50 | %FileCheck %s

// REQUIRES: OS=macosx

// CHECK-LABEL: sil{{.+}}@main{{.*}} {

// CHECK: [[MAJOR:%.*]] = integer_literal $Builtin.Word, 10
// CHECK: [[MINOR:%.*]] = integer_literal $Builtin.Word, 53
// CHECK: [[PATCH:%.*]] = integer_literal $Builtin.Word, 8
// CHECK: [[FUNC:%.*]] = function_ref @_TFs26_stdlib_isOSVersionAtLeastFTBwBwBw_Bi1_ : $@convention(thin) (Builtin.Word, Builtin.Word, Builtin.Word) -> Builtin.Int1
// CHECK: [[QUERY_RESULT:%.*]] = apply [[FUNC]]([[MAJOR]], [[MINOR]], [[PATCH]]) : $@convention(thin) (Builtin.Word, Builtin.Word, Builtin.Word) -> Builtin.Int1
if #available(OSX 10.53.8, iOS 7.1, *) {
}

// CHECK: [[TRUE:%.*]] = integer_literal $Builtin.Int1, -1
// CHECK: cond_br [[TRUE]]
// Since we are compiling for an unmentioned platform (OS X), we check against the minimum
// deployment target, which is 10.50
if #available(iOS 7.1, *) {
}


// CHECK: [[MAJOR:%.*]] = integer_literal $Builtin.Word, 10
// CHECK: [[MINOR:%.*]] = integer_literal $Builtin.Word, 52
// CHECK: [[PATCH:%.*]] = integer_literal $Builtin.Word, 0
// CHECK: [[QUERY_FUNC:%.*]] = function_ref @_TFs26_stdlib_isOSVersionAtLeastFTBwBwBw_Bi1_ : $@convention(thin) (Builtin.Word, Builtin.Word, Builtin.Word) -> Builtin.Int1
// CHECK: [[QUERY_RESULT:%.*]] = apply [[QUERY_FUNC]]([[MAJOR]], [[MINOR]], [[PATCH]]) : $@convention(thin) (Builtin.Word, Builtin.Word, Builtin.Word) -> Builtin.Int1
if #available(OSX 10.52, *) {
}

// CHECK: [[MAJOR:%.*]] = integer_literal $Builtin.Word, 10
// CHECK: [[MINOR:%.*]] = integer_literal $Builtin.Word, 52
// CHECK: [[PATCH:%.*]] = integer_literal $Builtin.Word, 0
// CHECK: [[QUERY_FUNC:%.*]] = function_ref @_TFs26_stdlib_isOSVersionAtLeastFTBwBwBw_Bi1_ : $@convention(thin) (Builtin.Word, Builtin.Word, Builtin.Word) -> Builtin.Int1
// CHECK: [[QUERY_RESULT:%.*]] = apply [[QUERY_FUNC]]([[MAJOR]], [[MINOR]], [[PATCH]]) : $@convention(thin) (Builtin.Word, Builtin.Word, Builtin.Word) -> Builtin.Int1
if #available(macOS 10.52, *) {
}

// CHECK: [[MAJOR:%.*]] = integer_literal $Builtin.Word, 10
// CHECK: [[MINOR:%.*]] = integer_literal $Builtin.Word, 0
// CHECK: [[PATCH:%.*]] = integer_literal $Builtin.Word, 0
// CHECK: [[QUERY_FUNC:%.*]] = function_ref @_TFs26_stdlib_isOSVersionAtLeastFTBwBwBw_Bi1_ : $@convention(thin) (Builtin.Word, Builtin.Word, Builtin.Word) -> Builtin.Int1
// CHECK: [[QUERY_RESULT:%.*]] = apply [[QUERY_FUNC]]([[MAJOR]], [[MINOR]], [[PATCH]]) : $@convention(thin) (Builtin.Word, Builtin.Word, Builtin.Word) -> Builtin.Int1
if #available(OSX 10, *) {
}

// CHECK: }

func doThing() {}

func testUnreachableVersionAvailable(condition: Bool) {
  if #available(OSX 10.0, *) {
    doThing() // no-warning
    return
  } else {
    doThing() // FIXME-warning {{will never be executed}}
  }

  if true {
    doThing() // no-warning
  }
  if false { // expected-note {{condition always evaluates to false}}
    doThing() // expected-warning {{will never be executed}}
  }
}

func testUnreachablePlatformAvailable(condition: Bool) {
  if #available(iOS 7.1, *) {
    doThing() // no-warning
    return
  } else {
    doThing() // no-warning
  }

  if true {
    doThing() // no-warning
  }
  if false { // expected-note {{condition always evaluates to false}}
    doThing() // expected-warning {{will never be executed}}
  }
}

func testUnreachablePlatformAvailableGuard() {
  guard #available(iOS 7.1, *) else {
    doThing() // no-warning
    return
  }

  doThing() // no-warning
}
