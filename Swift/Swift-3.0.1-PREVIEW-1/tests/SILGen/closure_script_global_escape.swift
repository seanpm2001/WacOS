// RUN: %target-swift-frontend -module-name foo -emit-silgen %s | %FileCheck %s
// RUN: %target-swift-frontend -module-name foo -emit-sil -verify %s

// CHECK-LABEL: sil @main

// CHECK: [[GLOBAL:%.*]] = global_addr @_Tv3foo4flagSb
// CHECK: [[MARK:%.*]] = mark_uninitialized [var] [[GLOBAL]]
var flag: Bool // expected-note* {{defined here}}

// CHECK: mark_function_escape [[MARK]]
func useFlag() { // expected-error{{'flag' used by function definition before being initialized}}
  _ = flag
}

// CHECK: [[CLOSURE:%.*]] = function_ref @_TF3fooU_FT_T_
// CHECK: mark_function_escape [[MARK]]
// CHECK: thin_to_thick_function [[CLOSURE]]
_ = { _ = flag } // expected-error{{'flag' captured by a closure before being initialized}}

// CHECK: mark_function_escape [[MARK]]
// CHECK: [[CLOSURE:%.*]] = function_ref @_TF3fooU0_FT_T_
// CHECK: apply [[CLOSURE]]
_ = { _ = flag }() // expected-error{{'flag' captured by a closure before being initialized}}

flag = true

// CHECK: mark_function_escape [[MARK]]
func useFlag2() {
  _ = flag
}

// CHECK: [[CLOSURE:%.*]] = function_ref @_TF3fooU1_FT_T_
// CHECK: mark_function_escape [[MARK]]
// CHECK: thin_to_thick_function [[CLOSURE]]
_ = { _ = flag }

// CHECK: mark_function_escape [[MARK]]
// CHECK: [[CLOSURE:%.*]] = function_ref @_TF3fooU2_FT_T_
// CHECK: apply [[CLOSURE]]
_ = { _ = flag }()
