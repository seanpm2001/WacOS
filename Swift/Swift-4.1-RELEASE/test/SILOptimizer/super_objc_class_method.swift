// RUN: %target-swift-frontend -emit-sil %s | %FileCheck %s
// RUN: %target-swift-frontend -emit-sil -I %S/Inputs -enable-source-import %s | %FileCheck %s

// REQUIRES: objc_interop

import Foundation
class MyFunkyDictionary: NSDictionary {
  // CHECK-LABEL: sil hidden @_T023super_objc_class_method17MyFunkyDictionaryC10initializeyyFZ
  // CHECK: objc_super_method %0 : $@thick MyFunkyDictionary.Type, #NSObject.initialize!1.foreign : (NSObject.Type) -> () -> ()
  override class func initialize() {
    super.initialize()
  }
}

