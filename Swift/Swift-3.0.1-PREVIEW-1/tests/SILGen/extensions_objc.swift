// RUN: %target-swift-frontend -sdk %S/Inputs %s -I %S/Inputs -enable-source-import -emit-silgen | %FileCheck %s
//
// REQUIRES: objc_interop

import Foundation

class Foo {}

extension Foo {
  dynamic func kay() {}
  dynamic var cox: Int { return 0 }
}

// CHECK-LABEL: sil hidden @_TF15extensions_objc19extensionReferencesFCS_3FooT_
func extensionReferences(_ x: Foo) {
  // dynamic extension methods are still dynamically dispatched.
  // CHECK: class_method [volatile] %0 : $Foo, #Foo.kay!1.foreign
  x.kay()
  // CHECK: class_method [volatile] %0 : $Foo, #Foo.cox!getter.1.foreign
  _ = x.cox

}

func extensionMethodCurrying(_ x: Foo) {
  _ = x.kay
}

// CHECK-LABEL: sil shared [thunk] @_TFC15extensions_objc3Foo3kayF
// CHECK:         function_ref @_TTDFC15extensions_objc3Foo3kayf
// CHECK:       sil shared [transparent] [thunk] @_TTDFC15extensions_objc3Foo3kayf
// CHECK:         class_method [volatile] %0 : $Foo, #Foo.kay!1.foreign
