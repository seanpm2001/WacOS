// RUN: %empty-directory(%t)
// RUN: %build-irgen-test-overlays
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) -emit-module -o %t %S/Inputs/objc_protocols_Bas.swift
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) -primary-file %s -emit-ir | %FileCheck %s
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) %s -emit-ir -num-threads 8 | %FileCheck %s

// REQUIRES: objc_interop

import gizmo

public protocol Runcible {
  func runce()
}

// CHECK-LABEL: @"$sSo6NSRectV33protocol_conformance_records_objc8RuncibleACMc" = constant %swift.protocol_conformance_descriptor {
// -- protocol descriptor
// CHECK-SAME:           [[RUNCIBLE:@"\$s33protocol_conformance_records_objc8RuncibleMp"]]
// -- nominal type descriptor
// CHECK-SAME:           @"$sSo6NSRectVMn"
// -- witness table
// CHECK-SAME:           @"$sSo6NSRectV33protocol_conformance_records_objc8RuncibleACWP"
// -- flags
// CHECK-SAME:           i32 0
// CHECK-SAME:         },
extension NSRect: Runcible {
  public func runce() {}
}

// CHECK-LABEL:         @"$sSo5GizmoC33protocol_conformance_records_objc8RuncibleACMc" = constant %swift.protocol_conformance_descriptor {
// -- protocol descriptor
// CHECK-SAME:           [[RUNCIBLE]]
// -- class name reference
// CHECK-SAME:           @[[GIZMO_NAME:[0-9]+]]
// -- witness table
// CHECK-SAME:           @"$sSo5GizmoC33protocol_conformance_records_objc8RuncibleACWP"
// -- flags
// CHECK-SAME:           i32 16
// CHECK-SAME:         }
extension Gizmo: Runcible {
  public func runce() {}
}

// CHECK: @[[GIZMO_NAME]] = private constant [6 x i8] c"Gizmo\00"

// CHECK-LABEL: @"$sSo6NSRectV33protocol_conformance_records_objc8RuncibleACHc" = private constant
// CHECK-LABEL: @"$sSo5GizmoC33protocol_conformance_records_objc8RuncibleACHc" = private constant
