// RUN: %empty-directory(%t)
// RUN: %build-silgen-test-overlays
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) -emit-module -o %t -I %S/../Inputs/ObjCBridging %S/../Inputs/ObjCBridging/Appliances.swift -I %t
// RUN: %target-swift-emit-silgen(mock-sdk: -sdk %S/Inputs -I %t) -I %S/../Inputs/ObjCBridging -disable-swift-bridge-attr -Xllvm -sil-full-demangle %s | %FileCheck %s

// REQUIRES: objc_interop

import Foundation
import Appliances

// This tests the -disable-swift-bridge-attr flag. Make sure we don't emit bridging code.

// CHECK-LABEL: sil hidden [ossa] @{{.*}}objc_disable_brigding16updateFridgeTemp
func updateFridgeTemp(_ home: APPHouse, delta: Double) {
  // CHECK-NOT: function_ref @{{.*}}BridgeFromObjectiveC
  home.fridge.temperature += delta
// CHECK: return
}
