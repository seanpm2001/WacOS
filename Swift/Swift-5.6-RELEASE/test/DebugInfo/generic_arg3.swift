// RUN: %target-swift-frontend %s -emit-ir -g -o - | %FileCheck %s

func apply<Type>(_ T : Type, fn: (Type) -> Type) -> Type { return fn(T) }

public func f<Type>(_ value : Type)
{
  // CHECK: define {{.*}}$s12generic_arg31fyyxlFxxXEfU_
  // CHECK: call void @llvm.dbg.declare(metadata %swift.opaque** %[[ALLOCA:[^,]+]],
  // CHECK-SAME: metadata ![[ARG:.*]], metadata !DIExpression(DW_OP_deref))
  // CHECK: store %swift.opaque* %1, %swift.opaque** %[[ALLOCA]], align
  // No deref here.
  // CHECK: ![[TY:.*]] = !DICompositeType({{.*}}identifier: "$sxD"
  // CHECK: ![[LET_TY:[0-9]+]] = !DIDerivedType(tag: DW_TAG_const_type,
  // CHECK-SAME:                                baseType: ![[TY]])
  // CHECK: ![[ARG]] = !DILocalVariable(name: "arg", arg: 1,
  // CHECK-SAME:                        line: [[@LINE+1]], type: ![[LET_TY]])
  apply(value) { arg in return arg }
}
