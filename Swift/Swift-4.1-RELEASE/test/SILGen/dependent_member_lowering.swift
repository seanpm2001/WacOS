// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership %s | %FileCheck %s

protocol P {
  associatedtype A

  func f(_ x: A)
}
struct Foo<T>: P {
  typealias A = T.Type

  func f(_ t: T.Type) {}
  // CHECK-LABEL: sil private [transparent] [thunk] @_T025dependent_member_lowering3FooVyxGAA1PA2aEP1fy1AQzFTW : $@convention(witness_method: P) <τ_0_0> (@in @thick τ_0_0.Type, @in_guaranteed Foo<τ_0_0>) -> ()
  // CHECK:       bb0(%0 : @trivial $*@thick τ_0_0.Type, %1 : @trivial $*Foo<τ_0_0>):
}
struct Bar<T>: P {
  typealias A = (Int) -> T

  func f(_ t: @escaping (Int) -> T) {}
  // CHECK-LABEL: sil private [transparent] [thunk] @_T025dependent_member_lowering3BarVyxGAA1PA2aEP1fy1AQzFTW : $@convention(witness_method: P) <τ_0_0> (@in @callee_guaranteed (@in Int) -> @out τ_0_0, @in_guaranteed Bar<τ_0_0>) -> ()
  // CHECK:       bb0(%0 : @trivial $*@callee_guaranteed (@in Int) -> @out τ_0_0, %1 : @trivial $*Bar<τ_0_0>):
}
