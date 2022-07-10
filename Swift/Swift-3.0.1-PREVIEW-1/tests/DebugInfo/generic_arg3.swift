// RUN: %target-swift-frontend %s -emit-ir -g -o - | %FileCheck %s

func apply<Type>(_ T : Type, fn: (Type) -> Type) -> Type { return fn(T) }

public func f<Type>(_ value : Type)
{
  // CHECK: define {{.*}}_TFF12generic_arg31furFxT_U_FQ_Q_
  // CHECK: store %swift.opaque* %1, %swift.opaque** %[[ALLOCA:.*]], align
  // CHECK: call void @llvm.dbg.declare(metadata %swift.opaque** %[[ALLOCA]],
  // CHECK-SAME:       metadata ![[ARG:.*]], metadata ![[EXPR:.*]])
  // No deref here: The argument is an Archetype and this implicitly indirect.
  // CHECK: ![[EXPR]] = !DIExpression()
  // CHECK: ![[TY:.*]] = !DICompositeType({{.*}}identifier: "_TtQq_F12generic_arg31furFxT_"
  // CHECK: ![[ARG]] = !DILocalVariable(name: "arg", arg: 1,
  // CHECK-SAME:                        line: [[@LINE+1]], type: ![[TY]])
  apply(value) { arg in return arg }
}
