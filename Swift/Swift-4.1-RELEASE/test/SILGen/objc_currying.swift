// RUN: %empty-directory(%t)
// RUN: %build-silgen-test-overlays
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) -enable-sil-ownership -emit-silgen %s | %FileCheck %s

// REQUIRES: objc_interop

import Foundation
import gizmo

func curry_pod(_ x: CurryTest) -> (Int) -> Int {
  return x.pod
}
// CHECK-LABEL: sil hidden @_T013objc_currying9curry_podS2icSo9CurryTestCF : $@convention(thin) (@owned CurryTest) -> @owned @callee_guaranteed (Int) -> Int
// CHECK:      bb0([[ARG1:%.*]] : @owned $CurryTest):
// CHECK:         [[BORROWED_ARG1:%.*]] = begin_borrow [[ARG1]]
// CHECK:         [[COPIED_ARG1:%.*]] = copy_value [[BORROWED_ARG1]]
// CHECK:         [[THUNK:%.*]] = function_ref @[[THUNK_FOO_1:_T0So9CurryTestC3podS2iFTcTO]] : $@convention(thin) (@owned CurryTest) -> @owned @callee_guaranteed (Int) -> Int
// CHECK:         [[FN:%.*]] = apply [[THUNK]]([[COPIED_ARG1]])
// CHECK:         end_borrow [[BORROWED_ARG1]] from [[ARG1]]
// CHECK:         destroy_value [[ARG1]]
// CHECK:         return [[FN]]
// CHECK: } // end sil function '_T013objc_currying9curry_podS2icSo9CurryTestCF'

// CHECK: sil shared [serializable] [thunk] @[[THUNK_FOO_1]] : $@convention(thin) (@owned CurryTest) -> @owned @callee_guaranteed (Int) -> Int
// CHECK:   [[THUNK:%.*]] = function_ref @[[THUNK_FOO_2:_T0So9CurryTestC3podS2iFTO]]
// CHECK:   [[FN:%.*]] = partial_apply [callee_guaranteed] [[THUNK]](%0)
// CHECK:   return [[FN]]
// CHECK: } // end sil function '[[THUNK_FOO_1]]'

// CHECK: sil shared [serializable] [thunk] @[[THUNK_FOO_2]] : $@convention(method) (Int, @guaranteed CurryTest) -> Int
// CHECK: bb0([[ARG1:%.*]] : @trivial $Int, [[ARG2:%.*]] : @guaranteed $CurryTest):
// CHECK:   [[COPIED_ARG2:%.*]] = copy_value [[ARG2]]
// CHECK:   [[METHOD:%.*]] = objc_method [[COPIED_ARG2]] : $CurryTest, #CurryTest.pod!1.foreign
// CHECK:   [[RESULT:%.*]] = apply [[METHOD]]([[ARG1]], [[COPIED_ARG2]])
// CHECK:   destroy_value [[COPIED_ARG2]]
// CHECK:   return [[RESULT]]
// CHECK: } // end sil function '[[THUNK_FOO_2]]'

func curry_bridged(_ x: CurryTest) -> (String?) -> String? {
  return x.bridged
}
// CHECK-LABEL: sil hidden @_T013objc_currying13curry_bridgedSSSgACcSo9CurryTestCF : $@convention(thin) (@owned CurryTest) -> @owned @callee_guaranteed (@owned Optional<String>) -> @owned Optional<String>
// CHECK: bb0([[ARG1:%.*]] : @owned $CurryTest):
// CHECK:   [[BORROWED_ARG1:%.*]] = begin_borrow [[ARG1]]
// CHECK:   [[ARG1_COPY:%.*]] = copy_value [[BORROWED_ARG1]]
// CHECK:   [[THUNK:%.*]] = function_ref @[[THUNK_BAR_1:_T0So9CurryTestC7bridgedSQySSGADFTcTO]]
// CHECK:   [[FN:%.*]] = apply [[THUNK]]([[ARG1_COPY]])
// CHECK:   end_borrow [[BORROWED_ARG1]] from [[ARG1]]
// CHECK:   destroy_value [[ARG1]]
// CHECK:   return [[FN]]
// CHECK: } // end sil function '_T013objc_currying13curry_bridgedSSSgACcSo9CurryTestCF'

// CHECK: sil shared [serializable] [thunk] @[[THUNK_BAR_1]] : $@convention(thin) (@owned CurryTest) -> @owned @callee_guaranteed (@owned Optional<String>) -> @owned Optional<String>
// CHECK: bb0([[ARG1:%.*]] : @owned $CurryTest):
// CHECK:   [[THUNK:%.*]] = function_ref @[[THUNK_BAR_2:_T0So9CurryTestC7bridgedSQySSGADFTO]]
// CHECK:   [[FN:%.*]] = partial_apply [callee_guaranteed] [[THUNK]]([[ARG1]])
// CHECK:   return [[FN]]
// CHECK: } // end sil function '[[THUNK_BAR_1]]'

// CHECK: sil shared [serializable] [thunk] @[[THUNK_BAR_2]] : $@convention(method) (@owned Optional<String>, @guaranteed CurryTest) -> @owned Optional<String>
// CHECK: bb0([[OPT_STRING:%.*]] : @owned $Optional<String>, [[SELF:%.*]] : @guaranteed $CurryTest):
// CHECK:   switch_enum [[OPT_STRING]] : $Optional<String>, case #Optional.some!enumelt.1: [[SOME_BB:bb[0-9]+]],
//
// CHECK: [[SOME_BB]]([[STRING:%.*]] : @owned $String):
// CHECK:   [[BRIDGING_FUNC:%.*]] = function_ref @_T0SS10FoundationE19_bridgeToObjectiveCSo8NSStringCyF
// CHECK:   [[BORROWED_STRING:%.*]] = begin_borrow [[STRING]]
// CHECK:   [[NSSTRING:%.*]] = apply [[BRIDGING_FUNC]]([[BORROWED_STRING]])
// CHECK:   [[OPT_NSSTRING:%.*]] = enum $Optional<NSString>, #Optional.some!enumelt.1, [[NSSTRING]] : $NSString
// CHECK:   end_borrow [[BORROWED_STRING]] from [[STRING]]
// CHECK:   destroy_value [[STRING]]
// CHECK:   br bb3([[OPT_NSSTRING]] : $Optional<NSString>)

// CHECK: bb2:
// CHECK:   [[OPT_NONE:%.*]] = enum $Optional<NSString>, #Optional.none!enumelt
// CHECK:   br bb3([[OPT_NONE]] : $Optional<NSString>)

// CHECK: bb3([[OPT_NSSTRING:%.*]] : @owned $Optional<NSString>):
// CHECK:   [[SELF_COPY:%.*]] = copy_value [[SELF]]
// CHECK:   [[METHOD:%.*]] = objc_method [[SELF_COPY]] : $CurryTest, #CurryTest.bridged!1.foreign
// CHECK:   [[RESULT_OPT_NSSTRING:%.*]] = apply [[METHOD]]([[OPT_NSSTRING]], [[SELF_COPY]]) : $@convention(objc_method) (Optional<NSString>, CurryTest) -> @autoreleased Optional<NSString>
// CHECK:   switch_enum [[RESULT_OPT_NSSTRING]] : $Optional<NSString>, case #Optional.some!enumelt.1: [[SOME_BB:bb[0-9]+]],

// CHECK: [[SOME_BB]]([[RESULT_NSSTRING:%.*]] : @owned $NSString):
// CHECK:   [[BRIDGE_FUNC:%.*]] = function_ref @_T0SS10FoundationE36_unconditionallyBridgeFromObjectiveCSSSo8NSStringCSgFZ
// CHECK:   [[REWRAP_RESULT_NSSTRING:%.*]] = enum $Optional<NSString>, #Optional.some!enumelt.1, [[RESULT_NSSTRING]]
// CHECK:   [[RESULT_STRING:%.*]] = apply [[BRIDGE_FUNC]]([[REWRAP_RESULT_NSSTRING]]
// CHECK:   [[WRAPPED_RESULT_STRING:%.*]] = enum $Optional<String>, #Optional.some!enumelt.1, [[RESULT_STRING]]
// CHECK:   br bb6([[WRAPPED_RESULT_STRING]] : $Optional<String>)

// CHECK: bb5:
// CHECK:   [[OPT_NONE:%.*]] = enum $Optional<String>, #Optional.none!enumelt
// CHECK:   br bb6([[OPT_NONE]] : $Optional<String>)

// CHECK: bb6([[FINAL_RESULT:%.*]] : @owned $Optional<String>):
// CHECK:   destroy_value [[SELF_COPY]]
// CHECK:   destroy_value [[OPT_NSSTRING]]
// CHECK:   return [[FINAL_RESULT]] : $Optional<String>
// CHECK: } // end sil function '[[THUNK_BAR_2]]'

func curry_returnsInnerPointer(_ x: CurryTest) -> () -> UnsafeMutableRawPointer? {
  return x.returnsInnerPointer
}
// CHECK-LABEL: sil hidden @_T013objc_currying25curry_returnsInnerPointerSvSgycSo9CurryTestCF : $@convention(thin) (@owned CurryTest) -> @owned @callee_guaranteed () -> Optional<UnsafeMutableRawPointer> {
// CHECK: bb0([[SELF:%.*]] : @owned $CurryTest):
// CHECK:   [[BORROWED_SELF:%.*]] = begin_borrow [[SELF]]
// CHECK:   [[SELF_COPY:%.*]] = copy_value [[BORROWED_SELF]]
// CHECK:   [[THUNK:%.*]] = function_ref @[[THUNK_RETURNSINNERPOINTER:_T0So9CurryTestC19returnsInnerPointerSQySvGyFTcTO]]
// CHECK:   [[FN:%.*]] = apply [[THUNK]]([[SELF_COPY]])
// CHECK:   end_borrow [[BORROWED_SELF]] from [[SELF]]
// CHECK:   destroy_value [[SELF]]
// CHECK:   return [[FN]]
// CHECK: } // end sil function '_T013objc_currying25curry_returnsInnerPointerSvSgycSo9CurryTestCF'

// CHECK: sil shared [serializable] [thunk] @[[THUNK_RETURNSINNERPOINTER]] : $@convention(thin) (@owned CurryTest) -> @owned @callee_guaranteed () -> Optional<UnsafeMutableRawPointer>
// CHECK:   [[THUNK:%.*]] = function_ref @[[THUNK_RETURNSINNERPOINTER_2:_T0So9CurryTestC19returnsInnerPointerSQySvGyFTO]]
// CHECK:   [[FN:%.*]] = partial_apply [callee_guaranteed] [[THUNK]](%0)
// CHECK:   return [[FN]]
// CHECK: } // end sil function '[[THUNK_RETURNSINNERPOINTER]]'

// CHECK: sil shared [serializable] [thunk] @[[THUNK_RETURNSINNERPOINTER_2]] : $@convention(method) (@guaranteed CurryTest) -> Optional<UnsafeMutableRawPointer>
// CHECK:  bb0([[ARG1:%.*]] : @guaranteed $CurryTest):
// CHECK:   [[ARG1_COPY:%.*]] = copy_value [[ARG1]]
// CHECK:   [[METHOD:%.*]] = objc_method [[ARG1_COPY]] : $CurryTest, #CurryTest.returnsInnerPointer!1.foreign
// CHECK:   [[RES:%.*]] = apply [[METHOD]]([[ARG1_COPY]]) : $@convention(objc_method) (CurryTest) -> @unowned_inner_pointer Optional<UnsafeMutableRawPointer>
// CHECK:   autorelease_value 
// CHECK:   return [[RES]]
// CHECK: } // end sil function '[[THUNK_RETURNSINNERPOINTER_2]]'

// CHECK-LABEL: sil hidden @_T013objc_currying19curry_pod_AnyObjectS2icyXlF : $@convention(thin) (@owned AnyObject) -> @owned @callee_guaranteed (Int) -> Int
// CHECK: bb0([[ANY:%.*]] : @owned $AnyObject):
// CHECK:   [[BORROWED_ANY:%.*]] = begin_borrow [[ANY]]
// CHECK:   [[OPENED_ANY:%.*]] = open_existential_ref [[BORROWED_ANY]]
// CHECK:   [[OPENED_ANY_COPY:%.*]] = copy_value [[OPENED_ANY]]
// CHECK:   dynamic_method_br [[OPENED_ANY_COPY]] : $@opened({{.*}}) AnyObject, #CurryTest.pod!1.foreign, [[HAS_METHOD:bb[0-9]+]]
// CHECK:   [[HAS_METHOD]]([[METHOD:%.*]] : @trivial $@convention(objc_method) (Int, @opened({{.*}}) AnyObject) -> Int):
// CHECK:   [[OPENED_ANY_COPY_2:%.*]] = copy_value [[OPENED_ANY_COPY]]
// CHECK:   partial_apply [callee_guaranteed] [[METHOD]]([[OPENED_ANY_COPY_2]])
// CHECK: } // end sil function '_T013objc_currying19curry_pod_AnyObjectS2icyXlF'
func curry_pod_AnyObject(_ x: AnyObject) -> (Int) -> Int {
  return x.pod!
}

// normalOwnership requires a thunk to bring the method to Swift conventions
// CHECK-LABEL: sil hidden @_T013objc_currying31curry_normalOwnership_AnyObjectSo9CurryTestCSgAEcyXlF : $@convention(thin) (@owned AnyObject) -> @owned @callee_guaranteed (@owned Optional<CurryTest>) -> @owned Optional<CurryTest> {
// CHECK: bb0([[ANY:%.*]] : @owned $AnyObject):
// CHECK:   [[BORROWED_ANY:%.*]] = begin_borrow [[ANY]]
// CHECK:   [[OPENED_ANY:%.*]] = open_existential_ref [[BORROWED_ANY]]
// CHECK:   [[OPENED_ANY_COPY:%.*]] = copy_value [[OPENED_ANY]]
// CHECK:   dynamic_method_br [[OPENED_ANY_COPY]] : $@opened({{.*}}) AnyObject, #CurryTest.normalOwnership!1.foreign, [[HAS_METHOD:bb[0-9]+]]
// CHECK: [[HAS_METHOD]]([[METHOD:%.*]] : @trivial $@convention(objc_method) (Optional<CurryTest>, @opened({{.*}}) AnyObject) -> @autoreleased Optional<CurryTest>):
// CHECK:   [[OPENED_ANY_COPY_2:%.*]] = copy_value [[OPENED_ANY_COPY]]
// CHECK:   [[PA:%.*]] = partial_apply [callee_guaranteed] [[METHOD]]([[OPENED_ANY_COPY_2]])
// CHECK:   [[THUNK:%.*]] = function_ref @_T0So9CurryTestCSgACIegyo_A2CIegxo_TR
// CHECK:   partial_apply [callee_guaranteed] [[THUNK]]([[PA]])
// CHECK: } // end sil function '_T013objc_currying31curry_normalOwnership_AnyObjectSo9CurryTestCSgAEcyXlF'
func curry_normalOwnership_AnyObject(_ x: AnyObject) -> (CurryTest?) -> CurryTest? {
  return x.normalOwnership!
}

// weirdOwnership is NS_RETURNS_RETAINED and NS_CONSUMES_SELF so already
// follows Swift conventions
// CHECK-LABEL: sil hidden @_T013objc_currying30curry_weirdOwnership_AnyObjectSo9CurryTestCSgAEcyXlF : $@convention(thin) (@owned AnyObject) -> @owned @callee_guaranteed (@owned Optional<CurryTest>) -> @owned Optional<CurryTest>
// CHECK: bb0([[ANY:%.*]] : @owned $AnyObject):
// CHECK:   [[BORROWED_ANY:%.*]] = begin_borrow [[ANY]]
// CHECK:   [[OPENED_ANY:%.*]] = open_existential_ref [[BORROWED_ANY]]
// CHECK:   [[OPENED_ANY_COPY:%.*]] = copy_value [[OPENED_ANY]]
// CHECK:   dynamic_method_br [[SELF:%.*]] : $@opened({{.*}}) AnyObject, #CurryTest.weirdOwnership!1.foreign, [[HAS_METHOD:bb[0-9]+]]
// CHECK: bb1([[METHOD:%.*]] : @trivial $@convention(objc_method) (@owned Optional<CurryTest>, @owned @opened({{.*}}) AnyObject) -> @owned Optional<CurryTest>):
// CHECK:   [[OPENED_ANY_COPY_2:%.*]] = copy_value [[OPENED_ANY_COPY]]
// CHECK:   partial_apply [callee_guaranteed] [[METHOD]]([[OPENED_ANY_COPY_2]])
// CHECK: } // end sil function '_T013objc_currying30curry_weirdOwnership_AnyObjectSo9CurryTestCSgAEcyXlF'
func curry_weirdOwnership_AnyObject(_ x: AnyObject) -> (CurryTest?) -> CurryTest? {
  return x.weirdOwnership!
}

// bridged requires a thunk to handle bridging conversions
// CHECK-LABEL: sil hidden @_T013objc_currying23curry_bridged_AnyObjectSSSgACcyXlF : $@convention(thin) (@owned AnyObject) -> @owned @callee_guaranteed (@owned Optional<String>) -> @owned Optional<String>
// CHECK: bb0([[ANY:%.*]] : @owned $AnyObject):
// CHECK:    [[BORROWED_ANY:%.*]] = begin_borrow [[ANY]]
// CHECK:   [[OPENED_ANY:%.*]] = open_existential_ref [[BORROWED_ANY]]
// CHECK:   [[OPENED_ANY_COPY:%.*]] = copy_value [[OPENED_ANY]]
// CHECK:   dynamic_method_br [[OPENED_ANY_COPY]] : $@opened({{.*}}) AnyObject, #CurryTest.bridged!1.foreign, [[HAS_METHOD:bb[0-9]+]]
// CHECK: } // end sil function '_T013objc_currying23curry_bridged_AnyObjectSSSgACcyXlF'
func curry_bridged_AnyObject(_ x: AnyObject) -> (String?) -> String? {
  return x.bridged!
}

// check that we substitute Self = AnyObject correctly for Self-returning
// methods
// CHECK-LABEL: sil hidden @_T013objc_currying27curry_returnsSelf_AnyObjectyXlSgycyXlF : $@convention(thin) (@owned AnyObject) -> @owned @callee_guaranteed () -> @owned Optional<AnyObject> {
// CHECK: bb0([[ANY:%.*]] : @owned $AnyObject):
// CHECK:   [[BORROWED_ANY:%.*]] = begin_borrow [[ANY]]
// CHECK:   [[OPENED_ANY:%.*]] = open_existential_ref [[BORROWED_ANY]]
// CHECK:   [[OPENED_ANY_COPY:%.*]] = copy_value [[OPENED_ANY]]
// CHECK:   dynamic_method_br [[OPENED_ANY_COPY]] : $@opened({{.*}}) AnyObject, #CurryTest.returnsSelf!1.foreign, [[HAS_METHOD:bb[0-9]+]]
// CHECK: [[HAS_METHOD]]([[METHOD:%.*]] : @trivial $@convention(objc_method) (@opened({{.*}}) AnyObject) -> @autoreleased Optional<AnyObject>):
// CHECK:   [[OPENED_ANY_COPY_2:%.*]] = copy_value [[OPENED_ANY_COPY]]
// CHECK:   [[PA:%.*]] = partial_apply [callee_guaranteed] [[METHOD]]([[OPENED_ANY_COPY_2]])
// CHECK: } // end sil function '_T013objc_currying27curry_returnsSelf_AnyObjectyXlSgycyXlF'
func curry_returnsSelf_AnyObject(_ x: AnyObject) -> () -> AnyObject? {
  return x.returnsSelf!
}

// CHECK-LABEL: sil hidden @_T013objc_currying35curry_returnsInnerPointer_AnyObjectSvSgycyXlF : $@convention(thin) (@owned AnyObject) -> @owned @callee_guaranteed () -> Optional<UnsafeMutableRawPointer> {
// CHECK: bb0([[ANY:%.*]] : @owned $AnyObject):
// CHECK:   [[BORROWED_ANY:%.*]] = begin_borrow [[ANY]]
// CHECK:   [[OPENED_ANY:%.*]] = open_existential_ref [[BORROWED_ANY]]
// CHECK:   [[OPENED_ANY_COPY:%.*]] = copy_value [[OPENED_ANY]]
// CHECK:   dynamic_method_br [[OPENED_ANY_COPY]] : $@opened({{.*}}) AnyObject, #CurryTest.returnsInnerPointer!1.foreign, [[HAS_METHOD:bb[0-9]+]]
// CHECK: [[HAS_METHOD]]([[METHOD:%.*]] : @trivial $@convention(objc_method) (@opened({{.*}}) AnyObject) -> @unowned_inner_pointer Optional<UnsafeMutableRawPointer>):
// CHECK:   [[OPENED_ANY_COPY_2:%.*]] = copy_value [[OPENED_ANY_COPY]]
// CHECK:   [[PA:%.*]] = partial_apply [callee_guaranteed] [[METHOD]]([[OPENED_ANY_COPY_2]])
// CHECK: } // end sil function '_T013objc_currying35curry_returnsInnerPointer_AnyObjectSvSgycyXlF'

func curry_returnsInnerPointer_AnyObject(_ x: AnyObject) -> () -> UnsafeMutableRawPointer? {
  return x.returnsInnerPointer!
}

