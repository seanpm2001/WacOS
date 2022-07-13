// RUN: %target-swift-frontend -primary-file %s %S/Inputs/specialize_inherited_multifile.swift -O -emit-sil -sil-verify-all | %FileCheck %s

@_optimize(none) func takesBase<T : Base>(t: T) {}

@inline(never) func takesHasAssocType<T : HasAssocType>(t: T) {
  takesBase(t: t.value)
}

// Make sure the ConcreteDerived : Base conformance is available here.

// CHECK-LABEL: sil shared [noinline] @_T030specialize_inherited_multifile17takesHasAssocTypeyx1t_tAA0efG0RzlFAA08ConcreteefG0C_Tg5 : $@convention(thin) (@owned ConcreteHasAssocType) -> ()
// CHECK: [[FN:%.*]] = function_ref @_T030specialize_inherited_multifile9takesBaseyx1t_tAA0E0RzlF
// CHECK: apply [[FN]]<ConcreteDerived>({{%.*}})
// CHECK: return

public func takesConcreteHasAssocType(c: ConcreteHasAssocType) {
  takesHasAssocType(t: c)
}
