// RUN: %target-swift-ide-test -print-module -module-to-print=FreeFunctionsSecondHeader -I %S/Inputs -source-filename=x -enable-cxx-interop | %FileCheck %s

// TODO: This file doesn't really test anything because functions need not be defined.
// CHECK: enum FunctionsNS1 {
// cHECK:   static func definedInDefs() -> UnsafePointer<CChar>!
// CHECK: }
