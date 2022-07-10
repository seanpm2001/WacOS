// RUN: rm -rf %t && mkdir %t
// RUN: %build-irgen-test-overlays
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) %s -emit-ir | %FileCheck %s

// REQUIRES: objc_interop

import Foundation

func foo() -> Int64 {
  // CHECK-LABEL: define internal i64 @_TToFCF10objc_local3fooFT_Vs5Int64L_3Bar10returnFivefT_S0_
  class Bar: NSObject {
    @objc func returnFive() -> Int64 { return 6 }
  }
  return 0
}


