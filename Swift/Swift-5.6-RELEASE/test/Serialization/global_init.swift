// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -emit-module -parse-as-library -o %t %s
// RUN: llvm-bcanalyzer %t/global_init.swiftmodule | %FileCheck %s -check-prefix=BCANALYZER
// RUN: %target-sil-opt -enable-sil-verify-all %t/global_init.swiftmodule | %FileCheck %s

// BCANALYZER-NOT: UnknownCode

// Swift globals are not currently serialized. However, addressor
// declarations are serialized when all these three flags are present:
// -emit-module -parse-as-library -sil-serialize-all
//
// The only way to inspect the serialized module is sil-opt. The swift
// driver will only output the SIL that it deserializes.

func initFunc() -> Int { return 42 }

@usableFromInline
let MyConst = initFunc()
@usableFromInline
var MyVar = 3

// CHECK: let MyConst: Int
// CHECK: var MyVar: Int

// CHECK-DAG: sil [global_init] [canonical] @$s11global_init7MyConstSivau : $@convention(thin) () -> Builtin.RawPointer
// CHECK-DAG: sil [global_init] [canonical] @$s11global_init5MyVarSivau : $@convention(thin) () -> Builtin.RawPointer

@inlinable
@usableFromInline
func getGlobals() -> Int {
  return MyVar + MyConst
}
