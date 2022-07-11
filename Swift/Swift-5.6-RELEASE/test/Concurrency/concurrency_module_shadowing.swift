// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -emit-module -emit-module-path %t/ShadowsConcur.swiftmodule -module-name ShadowsConcur %S/Inputs/ShadowsConcur.swift
// RUN: %target-typecheck-verify-swift -I %t  -disable-availability-checking
// REQUIRES: concurrency


import ShadowsConcur

@available(SwiftStdlib 5.1, *)
func f(_ t : UnsafeCurrentTask) -> Bool {
  return t.someProperty == "123"
}

@available(SwiftStdlib 5.1, *)
func g(_: _Concurrency.UnsafeCurrentTask) {}
