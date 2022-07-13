// RUN: %target-swift-frontend -enable-sil-ownership -emit-silgen -sdk %S/Inputs -I %S/Inputs -enable-source-import %s | %FileCheck %s

// REQUIRES: objc_interop

import Foundation

class C {}

protocol CP : class {}

@objc protocol OP {}

// CHECK-LABEL: sil hidden @_T026metatype_object_conversion0A8ToObjectyXlAA1CCmF 
func metatypeToObject(_ x: C.Type) -> AnyObject {
  // CHECK: bb0([[THICK:%.*]] : @trivial $@thick C.Type):
  // CHECK:   [[OBJC:%.*]] = thick_to_objc_metatype [[THICK]]
  // CHECK:   [[OBJECT:%.*]] = objc_metatype_to_object [[OBJC]]
  // CHECK:   return [[OBJECT]]
  return x
}

// CHECK-LABEL: sil hidden @_T026metatype_object_conversion27existentialMetatypeToObjectyXlAA2CP_pXpF
func existentialMetatypeToObject(_ x: CP.Type) -> AnyObject {
  // CHECK: bb0([[THICK:%.*]] : @trivial $@thick CP.Type):
  // CHECK:   [[OBJC:%.*]] = thick_to_objc_metatype [[THICK]]
  // CHECK:   [[OBJECT:%.*]] = objc_existential_metatype_to_object [[OBJC]]
  // CHECK:   return [[OBJECT]]
  return x
}

// CHECK-LABEL: sil hidden @_T026metatype_object_conversion23protocolToProtocolClassSo0F0CyF
func protocolToProtocolClass() -> Protocol {
  // CHECK: [[PROTO:%.*]] = objc_protocol #OP
  // CHECK: [[COPIED_PROTO:%.*]] = copy_value [[PROTO]]
  // CHECK: return [[COPIED_PROTO]]
  return OP.self
}
