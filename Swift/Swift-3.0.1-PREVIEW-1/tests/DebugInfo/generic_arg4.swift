// RUN: %target-swift-frontend %s -emit-ir -g -o - | %FileCheck %s
// REQUIRES: objc_interop
public struct Q<T> {
  let x: T
}
  // CHECK: define {{.*}}_TF12generic_arg43foourFGSaGVS_1Qx__T_
  // CHECK: store %[[TY:.*]]* %0, %[[TY]]** %[[ALLOCA:.*]], align
  // CHECK: call void @llvm.dbg.declare(metadata %[[TY]]** %[[ALLOCA]],
  // CHECK-SAME:       metadata ![[ARG:.*]], metadata ![[EXPR:.*]])
  // No deref here: the array argument is passed by value.
  // CHECK: ![[EXPR]] = !DIExpression()
  // CHECK: ![[ARG]] = !DILocalVariable(name: "arg", arg: 1,
  // CHECK-SAME:                        line: [[@LINE+2]], type: ![[TY:.*]])
  // CHECK: ![[TY]] = !DICompositeType({{.*}}identifier: "_TtGSaGV12generic_arg41QQq_FS_3foourFGSaGS0_x__T___")
public func foo<T>(_ arg: [Q<T>]) {
}
