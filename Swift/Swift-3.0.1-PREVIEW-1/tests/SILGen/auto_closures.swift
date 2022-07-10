// RUN: %target-swift-frontend -parse-stdlib -emit-silgen %s | %FileCheck %s

struct Bool {}
var false_ = Bool()

// CHECK-LABEL: sil hidden @_TF13auto_closures17call_auto_closure
func call_auto_closure(_ x: @autoclosure () -> Bool) -> Bool {
  // CHECK: [[RET:%.*]] = apply %0()
  // CHECK: return [[RET]]
  return x()
}

// CHECK-LABEL sil @_TF13auto_closures30test_auto_closure_with_capture
func test_auto_closure_with_capture(_ x: Bool) -> Bool {
  // CHECK: [[CLOSURE:%.*]] = function_ref @_TFF13auto_closures30test_auto_closure_with_capture
  // CHECK: [[WITHCAPTURE:%.*]] = partial_apply [[CLOSURE]](
  // CHECK: [[RET:%.*]] = apply {{%.*}}([[WITHCAPTURE]])
  // CHECK: return [[RET]]
  return call_auto_closure(x)
}

// CHECK-LABEL: sil hidden @_TF13auto_closures33test_auto_closure_without_capture
func test_auto_closure_without_capture() -> Bool {
  // CHECK: [[CLOSURE:%.*]] = function_ref @_TFF13auto_closures33test_auto_closure_without_capture
  // CHECK: [[THICK:%.*]] = thin_to_thick_function [[CLOSURE]] : $@convention(thin) () -> Bool to $@callee_owned () -> Bool
  // CHECK: [[RET:%.*]] = apply {{%.*}}([[THICK]])
  // CHECK: return [[RET]]
  return call_auto_closure(false_)
}

public class Base {
  var x: Bool { return false_ }
}

public class Sub : Base {
  // CHECK-LABEL: sil hidden @_TFC13auto_closures3Subg1xVS_4Bool : $@convention(method) (@guaranteed Sub) -> Bool {
  // CHECK: [[AUTOCLOSURE:%.*]] = function_ref @_TFFC13auto_closures3Subg1xVS_4Boolu_KT_S1_ : $@convention(thin) (@owned Sub) -> Bool
  // CHECK: = partial_apply [[AUTOCLOSURE]](%0)
  // CHECK: return {{%.*}} : $Bool
  // CHECK: }

  // CHECK-LABEL: sil shared [transparent] @_TFFC13auto_closures3Subg1xVS_4Boolu_KT_S1_ : $@convention(thin) (@owned Sub) -> Bool {
  // CHECK: [[SUPER:%[0-9]+]] = function_ref @_TFC13auto_closures4Baseg1xVS_4Bool : $@convention(method) (@guaranteed Base) -> Bool
  // CHECK: [[RET:%.*]] = apply [[SUPER]]({{%.*}})
  // CHECK: return [[RET]]
  override var x: Bool { return call_auto_closure(super.x) }
}

// CHECK-LABEL: sil hidden @_TF13auto_closures20closureInAutoclosureFTVS_4BoolS0__S0_ : $@convention(thin) (Bool, Bool) -> Bool {
// CHECK: }
// CHECK-LABEL: sil shared [transparent] @_TFF13auto_closures20closureInAutoclosureFTVS_4BoolS0__S0_u_KT_S0_ : $@convention(thin) (Bool, Bool) -> Bool {
// CHECK: }
// CHECK-LABEL: sil shared @_TFFF13auto_closures20closureInAutoclosureFTVS_4BoolS0__S0_u_KT_S0_U_FS0_S0_ : $@convention(thin) (Bool, Bool) -> Bool {
// CHECK: }
func compareBool(_ lhs: Bool, _ rhs: Bool) -> Bool { return false_ }
func testBool(_ x: Bool, _ pred: (Bool) -> Bool) -> Bool {
  return pred(x)
}
func delayBool(_ fn: @autoclosure () -> Bool) -> Bool {
  return fn()
}
func closureInAutoclosure(_ lhs: Bool, _ rhs: Bool) -> Bool {
  return delayBool(testBool(lhs, { compareBool($0, rhs) }))
}
