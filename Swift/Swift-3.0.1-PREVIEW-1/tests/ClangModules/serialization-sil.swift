// RUN: rm -rf %t && mkdir -p %t
// RUN: %target-swift-frontend -emit-module-path %t/Test.swiftmodule -emit-sil -o /dev/null -module-name Test %s -sdk "" -import-objc-header %S/Inputs/serialization-sil.h
// RUN: %target-sil-extract %t/Test.swiftmodule -func=_TF4Test16testPartialApplyFPSo4Test_T_ -o - | %FileCheck %s

// REQUIRES: objc_interop

// @_transparent to force serialization.
@_transparent
public func testPartialApply(_ obj: Test) {
  // CHECK-LABEL: @_TF4Test16testPartialApplyFPSo4Test_T_ : $@convention(thin) (@owned Test) -> () {
  if let curried1 = obj.normalObject {
    // CHECK: dynamic_method_br [[CURRIED1_OBJ:%.+]] : $@opened([[CURRIED1_EXISTENTIAL:.+]]) Test, #Test.normalObject!1.foreign, [[CURRIED1_TRUE:[^,]+]], [[CURRIED1_FALSE:[^,]+]]
    // CHECK: [[CURRIED1_FALSE]]:
    // CHECK: [[CURRIED1_TRUE]]([[CURRIED1_METHOD:%.+]] : $@convention(objc_method) (@opened([[CURRIED1_EXISTENTIAL]]) Test) -> @autoreleased AnyObject):
    // CHECK: [[CURRIED1_PARTIAL:%.+]] = partial_apply [[CURRIED1_METHOD]]([[CURRIED1_OBJ]]) : $@convention(objc_method) (@opened([[CURRIED1_EXISTENTIAL]]) Test) -> @autoreleased AnyObject
    // CHECK: [[CURRIED1_THUNK:%.+]] = function_ref @_TTRXFo__oPs9AnyObject__XFo__iP__ : $@convention(thin) (@owned @callee_owned () -> @owned AnyObject) -> @out Any
    // CHECK: = partial_apply [[CURRIED1_THUNK]]([[CURRIED1_PARTIAL]])
    curried1()
  }
  if let curried2 = obj.innerPointer {
    // CHECK: dynamic_method_br [[CURRIED2_OBJ:%.+]] : $@opened([[CURRIED2_EXISTENTIAL:.+]]) Test, #Test.innerPointer!1.foreign, [[CURRIED2_TRUE:[^,]+]], [[CURRIED2_FALSE:[^,]+]]
    // CHECK: [[CURRIED2_FALSE]]:
    // CHECK: [[CURRIED2_TRUE]]([[CURRIED2_METHOD:%.+]] : $@convention(objc_method) (@opened([[CURRIED2_EXISTENTIAL]]) Test) -> @unowned_inner_pointer UnsafeMutableRawPointer):
    // CHECK: [[CURRIED2_PARTIAL:%.+]] = partial_apply [[CURRIED2_METHOD]]([[CURRIED2_OBJ]]) : $@convention(objc_method) (@opened([[CURRIED2_EXISTENTIAL]]) Test) -> @unowned_inner_pointer UnsafeMutableRawPointer
    // CHECK: [[CURRIED2_THUNK:%.+]] = function_ref @_TTRXFo__dSv_XFo_iT__iSv_ : $@convention(thin) (@in (), @owned @callee_owned () -> UnsafeMutableRawPointer) -> @out UnsafeMutableRawPointer
    // CHECK: = partial_apply [[CURRIED2_THUNK]]([[CURRIED2_PARTIAL]]) : $@convention(thin) (@in (), @owned @callee_owned () -> UnsafeMutableRawPointer) -> @out UnsafeMutableRawPointer
    curried2()
  }
  if let prop1 = obj.normalObjectProp {
    // CHECK: dynamic_method_br [[PROP1_OBJ:%.+]] : $@opened([[PROP1_EXISTENTIAL:.+]]) Test, #Test.normalObjectProp!getter.1.foreign, [[PROP1_TRUE:[^,]+]], [[PROP1_FALSE:[^,]+]]
    // CHECK: [[PROP1_FALSE]]:
    // CHECK: [[PROP1_TRUE]]([[PROP1_METHOD:%.+]] : $@convention(objc_method) (@opened([[PROP1_EXISTENTIAL]]) Test) -> @autoreleased AnyObject):
    // CHECK: [[PROP1_PARTIAL:%.+]] = partial_apply [[PROP1_METHOD]]([[PROP1_OBJ]]) : $@convention(objc_method) (@opened([[PROP1_EXISTENTIAL]]) Test) -> @autoreleased AnyObject
    // CHECK: = apply [[PROP1_PARTIAL]]() : $@callee_owned () -> @owned AnyObject
    _ = prop1
  }
  if let prop2 = obj.innerPointerProp {
    // CHECK: dynamic_method_br [[PROP2_OBJ:%.+]] : $@opened([[PROP2_EXISTENTIAL:.+]]) Test, #Test.innerPointerProp!getter.1.foreign, [[PROP2_TRUE:[^,]+]], [[PROP2_FALSE:[^,]+]]
    // CHECK: [[PROP2_FALSE]]:
    // CHECK: [[PROP2_TRUE]]([[PROP2_METHOD:%.+]] : $@convention(objc_method) (@opened([[PROP2_EXISTENTIAL]]) Test) -> @unowned_inner_pointer UnsafeMutableRawPointer):
    // CHECK: [[PROP2_PARTIAL:%.+]] = partial_apply [[PROP2_METHOD]]([[PROP2_OBJ]]) : $@convention(objc_method) (@opened([[PROP2_EXISTENTIAL]]) Test) -> @unowned_inner_pointer UnsafeMutableRawPointer
    // CHECK: = apply [[PROP2_PARTIAL]]() : $@callee_owned () -> UnsafeMutableRawPointer
    _ = prop2
  }
} // CHECK: {{^}$}}
