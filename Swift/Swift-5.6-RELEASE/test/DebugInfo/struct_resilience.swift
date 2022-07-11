
// RUN: %empty-directory(%t)
//
// Compile the external swift module.
// RUN: %target-swift-frontend -g -emit-module -enable-library-evolution \
// RUN:   -emit-module-path=%t/resilient_struct.swiftmodule \
// RUN:   -module-name=resilient_struct %S/../Inputs/resilient_struct.swift
//
// RUN: %target-swift-frontend -g -I %t -emit-ir -enable-library-evolution %s \
// RUN:    -o - | %FileCheck %s
//
import resilient_struct

// CHECK-LABEL: define{{.*}} swiftcc void @"$s17struct_resilience9takesSizeyy010resilient_A00D0VF"(%swift.opaque* noalias nocapture %0)
// CHECK-LLDB-LABEL: define{{.*}} swiftcc void @"$s17struct_resilience9takesSizeyy010resilient_A00D0VF"(%T16resilient_struct4SizeV* noalias nocapture dereferenceable({{8|16}}) %0)
public func takesSize(_ s: Size) {}


// CHECK-LABEL: define{{.*}} swiftcc void @"$s17struct_resilience1fyyF"()
// CHECK-LLDB-LABEL: define{{.*}} swiftcc void @"$s17struct_resilience1fyyF"()
func f() {
  let s1 = Size(w: 1, h: 2)
  takesSize(s1)
  // CHECK: %[[ADDR:.*]] = alloca i8*
  // CHECK: call void @llvm.dbg.declare(metadata i8** %[[ADDR]],
  // CHECK-SAME:                        metadata ![[V1:[0-9]+]],
  // CHECK-SAME:                        metadata !DIExpression(DW_OP_deref))
  // CHECK: %[[S1:.*]] = alloca i8,
  // CHECK: store i8* %[[S1]], i8** %[[ADDR]]
}
f()

// CHECK: ![[TY:[0-9]+]] = !DICompositeType(tag: DW_TAG_structure_type, name: "Size",
// CHECK: ![[LET_TY:[0-9]+]] = !DIDerivedType(tag: DW_TAG_const_type,
// CHECK-SAME:                                baseType: ![[TY:[0-9]+]])
// CHECK: ![[V1]] = !DILocalVariable(name: "s1", {{.*}}type: ![[LET_TY]])
