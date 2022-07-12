// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -module-name protocol_overrides -emit-module -enable-library-evolution -emit-module-path=%t/protocol_overrides.swiftmodule %S/Inputs/protocol_overrides.swift
// RUN: %target-swift-emit-silgen %s -I %t | %FileCheck %s

// Check that keypath formation properly records the point at which the witness
// table entry for a protocol requirement is introduced.
import protocol_overrides

// CHECK-LABEL: sil hidden [ossa] @$s25keypath_witness_overrides18getWritableKeyPath_5indexs03AnyfG0Cx_5IndexQzt09protocol_C015OverridesSetterRzSHAGRQlF
func getWritableKeyPath<OS: OverridesSetter>(_ c: OS, index: OS.Index) -> AnyKeyPath
where OS.Index: Hashable {
  // CHECK: keypath $WritableKeyPath<OS, OS.Element>,
  // CHECK-SAME: id #OverridesSetter.subscript!getter
  // CHECK-SAME: getter @$s18protocol_overrides14OriginalGetterPy7ElementQz5IndexQzcipAA15OverridesSetterRzSHAGRQlxxTK
  // CHECK-SAME: setter @$s18protocol_overrides10AddsSetterPy7ElementQz5IndexQzcipAA09OverridesD0RzSHAGRQlxxTk
  // CHECK-SAME: indices_equals @$s5Index18protocol_overrides14OriginalGetterPQzAB15OverridesSetterRzSHAARpzlTH
  // CHECK-SAME: indices_hash @$s5Index18protocol_overrides14OriginalGetterPQzAB15OverridesSetterRzSHAARpzlTh
  let keypath = \OS.[index]
  return keypath
}

// CHECK-LABEL: sil shared [thunk] [ossa] @$s18protocol_overrides14OriginalGetterPy7ElementQz5IndexQzcipAA15OverridesSetterRzSHAGRQlxxTK
// CHECK: witness_method $OS, #OriginalGetter.subscript!getter

// CHECK-LABEL: sil shared [thunk] [ossa] @$s18protocol_overrides10AddsSetterPy7ElementQz5IndexQzcipAA09OverridesD0RzSHAGRQlxxTk
// CHECK-LABEL: witness_method $OS, #AddsSetter.subscript!setter
