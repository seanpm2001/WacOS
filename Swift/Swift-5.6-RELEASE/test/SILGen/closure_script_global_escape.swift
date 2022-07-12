// RUN: %target-swift-emit-silgen -module-name foo %s | %FileCheck %s
// RUN: %target-swift-emit-sil -module-name foo -verify %s
// RUN: %target-swift-frontend -emit-sil -module-name foo -verify %s

// CHECK-LABEL: sil [ossa] @main

// CHECK: [[GLOBAL:%.*]] = global_addr @$s3foo4flagSbv
// CHECK: [[MARK:%.*]] = mark_uninitialized [var] [[GLOBAL]]
var flag: Bool // expected-note* {{defined here}}

// CHECK: mark_function_escape [[MARK]]
func useFlag() { // expected-error{{'flag' used by function definition before being initialized}}
  _ = flag
}

// CHECK: [[CLOSURE:%.*]] = function_ref @$s3fooyycfU_
// CHECK: mark_function_escape [[MARK]]
// CHECK: thin_to_thick_function [[CLOSURE]]
_ = { _ = flag } // expected-error{{'flag' captured by a closure before being initialized}}

// CHECK: mark_function_escape [[MARK]]
// CHECK: [[CLOSURE:%.*]] = function_ref @$s3fooyyXEfU0_
// CHECK: apply [[CLOSURE]]
_ = { _ = flag }() // expected-error{{'flag' captured by a closure before being initialized}}

flag = true

// CHECK: mark_function_escape [[MARK]]
func useFlag2() {
  _ = flag
}

// CHECK: [[CLOSURE:%.*]] = function_ref @$s3fooyycfU1_
// CHECK: mark_function_escape [[MARK]]
// CHECK: thin_to_thick_function [[CLOSURE]]
_ = { _ = flag }

// CHECK: mark_function_escape [[MARK]]
// CHECK: [[CLOSURE:%.*]] = function_ref @$s3fooyyXEfU2_
// CHECK: apply [[CLOSURE]]
_ = { _ = flag }()
