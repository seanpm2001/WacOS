// RUN: rm -rf %t
// RUN: mkdir %t
// RUN: %target-swift-frontend -emit-module %S/Inputs/sil_witness_tables_external_input.swift -o %t/Swift.swiftmodule -parse-stdlib -parse-as-library -module-name Swift -sil-serialize-all -module-link-name swiftCore
// RUN: %target-swift-frontend -I %t -primary-file %s -emit-ir | %FileCheck %s

import Swift

// CHECK: @_TMVs1X = external global %swift.type
// CHECK: @_TWPVs1Xs1Ps = external global i8*

func doSomething<T : P>(_ t : T) -> Y {
  return t.doSomething()
}

func done() -> Y {
  let x = X()
  return doSomething(x)
}
