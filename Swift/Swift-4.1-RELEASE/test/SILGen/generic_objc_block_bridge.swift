// RUN: %target-swift-frontend -emit-silgen -sdk %S/Inputs -I %S/Inputs -enable-source-import %s | %FileCheck %s

// REQUIRES: objc_interop

import Foundation

class Butt: NSObject {
  dynamic func butt(_ b: (Int) -> Int) {}
}

class Tubb<GenericParamName>: Butt {
  override func butt(_ b: (Int) -> Int) {
    super.butt(b)
  }
}

// CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T0S2iIyByd_S2iIgyd_TR : $@convention(thin) (Int, @guaranteed @convention(block) @noescape (Int) -> Int) -> Int {
