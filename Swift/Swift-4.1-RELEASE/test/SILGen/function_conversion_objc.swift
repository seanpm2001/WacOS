// RUN: %target-swift-frontend -sdk %S/Inputs %s -I %S/Inputs -enable-sil-ownership -enable-source-import -emit-silgen -verify | %FileCheck %s

import Foundation

// REQUIRES: objc_interop

// ==== Metatype to object conversions

// CHECK-LABEL: sil hidden @_T024function_conversion_objc20convMetatypeToObjectySo8NSObjectCmADcF
func convMetatypeToObject(_ f: @escaping (NSObject) -> NSObject.Type) {
// CHECK:         function_ref @_T0So8NSObjectCABXMTIegxd_AByXlIegxo_TR
// CHECK:         partial_apply
  let _: (NSObject) -> AnyObject = f
}

// CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T0So8NSObjectCABXMTIegxd_AByXlIegxo_TR : $@convention(thin) (@owned NSObject, @guaranteed @callee_guaranteed (@owned NSObject) -> @thick NSObject.Type) -> @owned AnyObject {
// CHECK:         apply %1(%0)
// CHECK:         thick_to_objc_metatype {{.*}} : $@thick NSObject.Type to $@objc_metatype NSObject.Type
// CHECK:         objc_metatype_to_object {{.*}} : $@objc_metatype NSObject.Type to $AnyObject
// CHECK:         return

@objc protocol NSBurrito {}

// CHECK-LABEL: sil hidden @_T024function_conversion_objc31convExistentialMetatypeToObjectyAA9NSBurrito_pXpAaC_pcF
func convExistentialMetatypeToObject(_ f: @escaping (NSBurrito) -> NSBurrito.Type) {
// CHECK:         function_ref @_T024function_conversion_objc9NSBurrito_pAaB_pXmTIegxd_AaB_pyXlIegxo_TR
// CHECK:         partial_apply
  let _: (NSBurrito) -> AnyObject = f
}

// CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T024function_conversion_objc9NSBurrito_pAaB_pXmTIegxd_AaB_pyXlIegxo_TR : $@convention(thin) (@owned NSBurrito, @guaranteed @callee_guaranteed (@owned NSBurrito) -> @thick NSBurrito.Type) -> @owned AnyObject
// CHECK:         apply %1(%0)
// CHECK:         thick_to_objc_metatype {{.*}} : $@thick NSBurrito.Type to $@objc_metatype NSBurrito.Type
// CHECK:         objc_existential_metatype_to_object {{.*}} : $@objc_metatype NSBurrito.Type to $AnyObject
// CHECK:         return

// CHECK-LABEL: sil hidden @_T024function_conversion_objc28convProtocolMetatypeToObjectyAA9NSBurrito_pmycF
func convProtocolMetatypeToObject(_ f: @escaping () -> NSBurrito.Protocol) {
// CHECK:         function_ref @_T024function_conversion_objc9NSBurrito_pXMtIegd_So8ProtocolCIego_TR
// CHECK:         partial_apply
  let _: () -> Protocol = f
}

// CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T024function_conversion_objc9NSBurrito_pXMtIegd_So8ProtocolCIego_TR : $@convention(thin) (@guaranteed @callee_guaranteed () -> @thin NSBurrito.Protocol) -> @owned Protocol
// CHECK:         apply %0() : $@callee_guaranteed () -> @thin NSBurrito.Protocol
// CHECK:         objc_protocol #NSBurrito : $Protocol
// CHECK:         copy_value
// CHECK:         return

// ==== Representation conversions

// CHECK-LABEL: sil hidden @_T024function_conversion_objc11funcToBlockyyXByycF : $@convention(thin) (@owned @callee_guaranteed () -> ()) -> @owned @convention(block) () -> ()
// CHECK:         [[BLOCK_STORAGE:%.*]] = alloc_stack $@block_storage
// CHECK:         [[BLOCK:%.*]] = init_block_storage_header [[BLOCK_STORAGE]]
// CHECK:         [[COPY:%.*]] = copy_block [[BLOCK]] : $@convention(block) () -> ()
// CHECK:         return [[COPY]]
func funcToBlock(_ x: @escaping () -> ()) -> @convention(block) () -> () {
  return x
}

// CHECK-LABEL: sil hidden @_T024function_conversion_objc11blockToFuncyycyyXBF : $@convention(thin) (@owned @convention(block) () -> ()) -> @owned @callee_guaranteed () -> ()
// CHECK: bb0([[ARG:%.*]] : @owned $@convention(block) () -> ()):
// CHECK:   [[COPIED:%.*]] = copy_block [[ARG]]
// CHECK:   [[BORROWED_COPIED:%.*]] = begin_borrow [[COPIED]]
// CHECK:   [[COPIED_2:%.*]] = copy_value [[BORROWED_COPIED]]
// CHECK:   [[THUNK:%.*]] = function_ref @_T0IeyB_Ieg_TR
// CHECK:   [[FUNC:%.*]] = partial_apply [callee_guaranteed] [[THUNK]]([[COPIED_2]])
// CHECK:   end_borrow [[BORROWED_COPIED]] from [[COPIED]]
// CHECK:   destroy_value [[COPIED]]
// CHECK:   destroy_value [[ARG]]
// CHECK:   return [[FUNC]]
func blockToFunc(_ x: @escaping @convention(block) () -> ()) -> () -> () {
  return x
}

// ==== Representation change + function type conversion

// CHECK-LABEL: sil hidden @_T024function_conversion_objc22blockToFuncExistentialypycSiyXBF : $@convention(thin) (@owned @convention(block) () -> Int) -> @owned @callee_guaranteed () -> @out Any
// CHECK:         function_ref @_T0SiIeyBd_SiIegd_TR
// CHECK:         partial_apply
// CHECK:         function_ref @_T0SiIegd_ypIegr_TR
// CHECK:         partial_apply
// CHECK:         return
func blockToFuncExistential(_ x: @escaping @convention(block) () -> Int) -> () -> Any {
  return x
}

// CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T0SiIeyBd_SiIegd_TR : $@convention(thin) (@guaranteed @convention(block) () -> Int) -> Int

// CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T0SiIegd_ypIegr_TR : $@convention(thin) (@guaranteed @callee_guaranteed () -> Int) -> @out Any

// C function pointer conversions

class A : NSObject {}
class B : A {}

// CHECK-LABEL: sil hidden @_T024function_conversion_objc18cFuncPtrConversionyAA1BCXCyAA1ACXCF
func cFuncPtrConversion(_ x: @escaping @convention(c) (A) -> ()) -> @convention(c) (B) -> () {
// CHECK:         convert_function %0 : $@convention(c) (A) -> () to $@convention(c) (B) -> ()
// CHECK:         return
  return x
}

func cFuncPtr(_ a: A) {}

// CHECK-LABEL: sil hidden @_T024function_conversion_objc19cFuncDeclConversionyAA1BCXCyF
func cFuncDeclConversion() -> @convention(c) (B) -> () {
// CHECK:         function_ref @_T024function_conversion_objc8cFuncPtryAA1ACFTo : $@convention(c) (A) -> ()
// CHECK:         convert_function %0 : $@convention(c) (A) -> () to $@convention(c) (B) -> ()
// CHECK:         return
  return cFuncPtr
}

func cFuncPtrConversionUnsupported(_ x: @escaping @convention(c) (@convention(block) () -> ()) -> ())
    -> @convention(c) (@convention(c) () -> ()) -> () {
  return x  // expected-error{{C function pointer signature '@convention(c) (@convention(block) () -> ()) -> ()' is not compatible with expected type '@convention(c) (@convention(c) () -> ()) -> ()'}}
}
