// RUN: rm -rf %t && mkdir %t
// RUN: %build-silgen-test-overlays
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) -emit-silgen %s | %FileCheck %s

// REQUIRES: objc_interop

import Foundation
import gizmo

func curry_pod(_ x: CurryTest) -> (Int) -> Int {
  return x.pod
}
// CHECK-LABEL: sil hidden @_TF13objc_currying9curry_podFCSo9CurryTestFSiSi : $@convention(thin) (@owned CurryTest) -> @owned @callee_owned (Int) -> Int
// CHECK:      bb0([[ARG1:%.*]] : $CurryTest):
// CHECK:         [[THUNK:%.*]] = function_ref [[THUNK_FOO_1:@_TTOFCSo9CurryTest3podFSiSi]] : $@convention(thin) (@owned CurryTest) -> @owned @callee_owned (Int) -> Int
// CHECK:         strong_retain [[ARG1]]
// CHECK:         [[FN:%.*]] = apply [[THUNK]](%0)
// CHECK:         strong_release [[ARG1]]
// CHECK:         return [[FN]]

// CHECK: sil shared [thunk] [[THUNK_FOO_1]] : $@convention(thin) (@owned CurryTest) -> @owned @callee_owned (Int) -> Int
// CHECK:   [[THUNK:%.*]] = function_ref [[THUNK_FOO_2:@_TTOFCSo9CurryTest3podfSiSi]]
// CHECK:   [[FN:%.*]] = partial_apply [[THUNK]](%0)
// CHECK:   return [[FN]]

// CHECK: sil shared [thunk] [[THUNK_FOO_2]] : $@convention(method) (Int, @guaranteed CurryTest) -> Int
// CHECK: bb0([[ARG1:%.*]] : $Int, [[ARG2:%.*]] : $CurryTest):
// CHECK:   strong_retain [[ARG2]]
// CHECK:   [[METHOD:%.*]] = class_method [volatile] %1 : $CurryTest, #CurryTest.pod!1.foreign
// CHECK:   [[RESULT:%.*]] = apply [[METHOD]](%0, %1)
// CHECK:   strong_release [[ARG2]]
// CHECK:   return [[RESULT]]

func curry_bridged(_ x: CurryTest) -> (String!) -> String! {
  return x.bridged
}
// CHECK-LABEL: sil hidden @_TF13objc_currying13curry_bridgedFCSo9CurryTestFGSQSS_GSQSS_ : $@convention(thin) (@owned CurryTest) -> @owned @callee_owned (@owned ImplicitlyUnwrappedOptional<String>) -> @owned ImplicitlyUnwrappedOptional<String>
// CHECK:         [[THUNK:%.*]] = function_ref [[THUNK_BAR_1:@_TTOFCSo9CurryTest7bridgedFGSQSS_GSQSS_]]
// CHECK:         [[FN:%.*]] = apply [[THUNK]](%0)
// CHECK:         return [[FN]]

// CHECK: sil shared [thunk] [[THUNK_BAR_1]] : $@convention(thin) (@owned CurryTest) -> @owned @callee_owned (@owned ImplicitlyUnwrappedOptional<String>) -> @owned ImplicitlyUnwrappedOptional<String>
// CHECK:   [[THUNK:%.*]] = function_ref [[THUNK_BAR_2:@_TTOFCSo9CurryTest7bridgedfGSQSS_GSQSS_]]
// CHECK:   [[FN:%.*]] = partial_apply [[THUNK]](%0)
// CHECK:   return [[FN]]

// CHECK: sil shared [thunk] [[THUNK_BAR_2]] : $@convention(method) (@owned ImplicitlyUnwrappedOptional<String>, @guaranteed CurryTest) -> @owned ImplicitlyUnwrappedOptional<String>
// CHECK:   function_ref @_TFE10FoundationSS19_bridgeToObjectiveCfT_CSo8NSString
// CHECK:   [[METHOD:%.*]] = class_method [volatile] %1 : $CurryTest, #CurryTest.bridged!1.foreign
// CHECK:   [[RES:%.*]] = apply [[METHOD]]({{%.*}}, %1) : $@convention(objc_method) (ImplicitlyUnwrappedOptional<NSString>, CurryTest) -> @autoreleased ImplicitlyUnwrappedOptional<NSString>
// CHECK:   function_ref @_TZFE10FoundationSS36_unconditionallyBridgeFromObjectiveCfGSqCSo8NSString_SS
// CHECK:   strong_release %1
// CHECK:   return {{%.*}} : $ImplicitlyUnwrappedOptional<String>

func curry_returnsInnerPointer(_ x: CurryTest) -> () -> UnsafeMutableRawPointer! {
  return x.returnsInnerPointer
}
// CHECK-LABEL: sil hidden @_TF13objc_currying25curry_returnsInnerPointerFCSo9CurryTestFT_GSQSv_ : $@convention(thin) (@owned CurryTest) -> @owned @callee_owned () -> ImplicitlyUnwrappedOptional<UnsafeMutableRawPointer> {
// CHECK:         [[THUNK:%.*]] = function_ref [[THUNK_RETURNSINNERPOINTER:@_TTOFCSo9CurryTest19returnsInnerPointerFT_GSQSv_]]
// CHECK:         [[FN:%.*]] = apply [[THUNK]](%0)
// CHECK:         return [[FN]]

// CHECK: sil shared [thunk] [[THUNK_RETURNSINNERPOINTER]] : $@convention(thin) (@owned CurryTest) -> @owned @callee_owned () -> ImplicitlyUnwrappedOptional<UnsafeMutableRawPointer>
// CHECK:   [[THUNK:%.*]] = function_ref [[THUNK_RETURNSINNERPOINTER_2:@_TTOFCSo9CurryTest19returnsInnerPointerfT_GSQSv_]]
// CHECK:   [[FN:%.*]] = partial_apply [[THUNK]](%0)
// CHECK:   return [[FN]]

// CHECK: sil shared [thunk] @_TTOFCSo9CurryTest19returnsInnerPointerfT_GSQSv_ : $@convention(method) (@guaranteed CurryTest) -> ImplicitlyUnwrappedOptional<UnsafeMutableRawPointer>
// CHECK:  bb0([[ARG1:%.*]] : 
// CHECK:   strong_retain [[ARG1]]
// CHECK:   [[METHOD:%.*]] = class_method [volatile] %0 : $CurryTest, #CurryTest.returnsInnerPointer!1.foreign
// CHECK:   [[RES:%.*]] = apply [[METHOD]](%0) : $@convention(objc_method) (CurryTest) -> @unowned_inner_pointer ImplicitlyUnwrappedOptional<UnsafeMutableRawPointer>
// CHECK:   autorelease_value %0
// CHECK:   return [[RES]]

// CHECK-LABEL: sil hidden @_TF13objc_currying19curry_pod_AnyObjectFPs9AnyObject_FSiSi : $@convention(thin) (@owned AnyObject) -> @owned @callee_owned (Int) -> Int
// CHECK:         dynamic_method_br [[SELF:%.*]] : $@opened({{.*}}) AnyObject, #CurryTest.pod!1.foreign, [[HAS_METHOD:bb[0-9]+]]
// CHECK:       [[HAS_METHOD]]([[METHOD:%.*]] : $@convention(objc_method) (Int, @opened({{.*}}) AnyObject) -> Int):
// CHECK:         partial_apply [[METHOD]]([[SELF]])
func curry_pod_AnyObject(_ x: AnyObject) -> (Int) -> Int {
  return x.pod!
}

// normalOwnership requires a thunk to bring the method to Swift conventions
// CHECK-LABEL: sil hidden @_TF13objc_currying31curry_normalOwnership_AnyObjectFPs9AnyObject_FGSQCSo9CurryTest_GSQS1__
// CHECK:         dynamic_method_br [[SELF:%.*]] : $@opened({{.*}}) AnyObject, #CurryTest.normalOwnership!1.foreign, [[HAS_METHOD:bb[0-9]+]]
// CHECK:       [[HAS_METHOD]]([[METHOD:%.*]] : $@convention(objc_method) (ImplicitlyUnwrappedOptional<CurryTest>, @opened({{.*}}) AnyObject) -> @autoreleased ImplicitlyUnwrappedOptional<CurryTest>):
// CHECK:         [[PA:%.*]] = partial_apply [[METHOD]]([[SELF]])
// CHECK:         [[THUNK:%.*]] = function_ref @_TTRXFo_dGSQCSo9CurryTest__oGSQS___XFo_oGSQS___oGSQS___
// CHECK:         partial_apply [[THUNK]]([[PA]])
func curry_normalOwnership_AnyObject(_ x: AnyObject) -> (CurryTest!) -> CurryTest! {
  return x.normalOwnership!
}

// weirdOwnership is NS_RETURNS_RETAINED and NS_CONSUMES_SELF so already
// follows Swift conventions
// CHECK-LABEL: sil hidden @_TF13objc_currying30curry_weirdOwnership_AnyObjectFPs9AnyObject_FGSQCSo9CurryTest_GSQS1__ : $@convention(thin) (@owned AnyObject) -> @owned @callee_owned (@owned ImplicitlyUnwrappedOptional<CurryTest>) -> @owned ImplicitlyUnwrappedOptional<CurryTest> 
// CHECK:         dynamic_method_br [[SELF:%.*]] : $@opened({{.*}}) AnyObject, #CurryTest.weirdOwnership!1.foreign, [[HAS_METHOD:bb[0-9]+]]
// CHECK:       bb1([[METHOD:%.*]] : $@convention(objc_method) (@owned ImplicitlyUnwrappedOptional<CurryTest>, @owned @opened({{.*}}) AnyObject) -> @owned ImplicitlyUnwrappedOptional<CurryTest>):
// CHECK:         partial_apply [[METHOD]]([[SELF]])
func curry_weirdOwnership_AnyObject(_ x: AnyObject) -> (CurryTest!) -> CurryTest! {
  return x.weirdOwnership!
}

// bridged requires a thunk to handle bridging conversions
// CHECK-LABEL: sil hidden @_TF13objc_currying23curry_bridged_AnyObjectFPs9AnyObject_FGSQSS_GSQSS_ : $@convention(thin) (@owned AnyObject) -> @owned @callee_owned (@owned ImplicitlyUnwrappedOptional<String>) -> @owned ImplicitlyUnwrappedOptional<String>
// CHECK:         dynamic_method_br [[SELF:%.*]] : $@opened({{.*}}) AnyObject, #CurryTest.bridged!1.foreign, [[HAS_METHOD:bb[0-9]+]]
// CHECK:       [[HAS_METHOD]]([[METHOD:%.*]] : $@convention(objc_method) (ImplicitlyUnwrappedOptional<NSString>, @opened({{.*}}) AnyObject) -> @autoreleased ImplicitlyUnwrappedOptional<NSString>):
// CHECK:         [[PA:%.*]] = partial_apply [[METHOD]]([[SELF]])
// CHECK:         [[THUNK:%.*]] = function_ref @_TTRXFo_dGSQCSo8NSString__oGSQS___XFo_oGSQSS__oGSQSS__
// CHECK:         partial_apply [[THUNK]]([[PA]])
func curry_bridged_AnyObject(_ x: AnyObject) -> (String!) -> String! {
  return x.bridged!
}

// check that we substitute Self = AnyObject correctly for Self-returning
// methods
// CHECK-LABEL: sil hidden @_TF13objc_currying27curry_returnsSelf_AnyObjectFPs9AnyObject_FT_GSQPS0___
// CHECK:         dynamic_method_br [[SELF:%.*]] : $@opened({{.*}}) AnyObject, #CurryTest.returnsSelf!1.foreign, [[HAS_METHOD:bb[0-9]+]]
// CHECK:       [[HAS_METHOD]]([[METHOD:%.*]] : $@convention(objc_method) (@opened({{.*}}) AnyObject) -> @autoreleased ImplicitlyUnwrappedOptional<AnyObject>):
// CHECK:         [[PA:%.*]] = partial_apply [[METHOD]]([[SELF]])
// CHECK:         [[THUNK:%.*]] = function_ref @_TTRXFo__oGSQPs9AnyObject___XFo_iT__iGSQPS____
// CHECK:         partial_apply [[THUNK]]([[PA]])
func curry_returnsSelf_AnyObject(_ x: AnyObject) -> () -> AnyObject! {
  return x.returnsSelf!
}

// CHECK-LABEL: sil hidden @_TF13objc_currying35curry_returnsInnerPointer_AnyObjectFPs9AnyObject_FT_GSQSv_
// CHECK:         dynamic_method_br [[SELF:%.*]] : $@opened({{.*}}) AnyObject, #CurryTest.returnsInnerPointer!1.foreign, [[HAS_METHOD:bb[0-9]+]]
// CHECK:       [[HAS_METHOD]]([[METHOD:%.*]] : $@convention(objc_method) (@opened({{.*}}) AnyObject) -> @unowned_inner_pointer ImplicitlyUnwrappedOptional<UnsafeMutableRawPointer>):
// CHECK:         [[PA:%.*]] = partial_apply [[METHOD]]([[SELF]])
// CHECK:         [[PA]]{{.*}}@owned @callee_owned () -> ImplicitlyUnwrappedOptional<UnsafeMutableRawPointer>

func curry_returnsInnerPointer_AnyObject(_ x: AnyObject) -> () -> UnsafeMutableRawPointer! {
  return x.returnsInnerPointer!
}
