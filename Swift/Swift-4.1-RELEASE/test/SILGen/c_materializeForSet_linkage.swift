// RUN: %target-swift-frontend -sdk %S/Inputs -I %S/Inputs -enable-source-import %s -emit-silgen -enable-sil-ownership | %FileCheck %s

// REQUIRES: objc_interop

import AppKit

protocol Pointable {
  var x: Float { get set }
  var y: Float { get set }
}

extension NSPoint: Pointable {}

extension NSReferencePoint: Pointable {}

// Make sure synthesized materializeForSet and its callbacks have shared linkage
// for properties imported from Clang

// CHECK-LABEL: sil shared [transparent] [serializable] @_T0SC7NSPointV1xSfvm
// CHECK-LABEL: sil shared [transparent] [serializable] @_T0SC7NSPointV1ySfvm

// CHECK-LABEL: sil shared [serializable] @_T0So16NSReferencePointC1xSfvmytfU_
// CHECK-LABEL: sil shared [serializable] @_T0So16NSReferencePointC1xSfvm

// CHECK-LABEL: sil shared [serializable] @_T0So16NSReferencePointC1ySfvmytfU_
// CHECK-LABEL: sil shared [serializable] @_T0So16NSReferencePointC1ySfvm
