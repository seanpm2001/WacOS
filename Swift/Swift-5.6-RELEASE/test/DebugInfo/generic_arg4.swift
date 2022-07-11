// RUN: %target-swift-frontend %s -emit-ir -g -o - | %FileCheck %s
// REQUIRES: objc_interop
public struct Q<T> {
  let x: T
}
// CHECK: define {{.*}}$s12generic_arg43fooyySayAA1QVyxGGlF
// CHECK: call void @llvm.dbg.declare
// CHECK: call void @llvm.dbg.declare(metadata %[[TY:.*]]** %[[ALLOCA:[^,]+]],
// CHECK-SAME:       metadata ![[ARG:.*]], metadata !DIExpression())
// CHECK: store %[[TY]]* %0, %[[TY]]** %[[ALLOCA]], align
// No deref here: the array argument is passed by value.
// CHECK: ![[DITY:.*]] = !DICompositeType({{.*}}identifier: "$sSay12generic_arg41QVyxGGD")
public func foo<T>(_ arg: [Q<T>]) {
// CHECK: ![[ARG]] = !DILocalVariable(name: "arg", arg: 1,
// CHECK-SAME:                        line: [[@LINE-2]], type: ![[DITY:.*]])
}
