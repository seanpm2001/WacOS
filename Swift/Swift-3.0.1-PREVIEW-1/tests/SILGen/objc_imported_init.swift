// RUN: %target-swift-frontend -verify -emit-silgen -sdk %S/Inputs -I %S/Inputs -enable-source-import %s | %FileCheck %s

// REQUIRES: objc_interop

import Foundation

// Ensure we emit allocating constructor thunks for ObjC initializers that
// were inherited.
// CHECK-LABEL: sil shared @_TFCSo3FooCfT_S_ : $@convention(method) (@thick Foo.Type) -> @owned Foo {
func foo() {
  _ = Foo()
}

