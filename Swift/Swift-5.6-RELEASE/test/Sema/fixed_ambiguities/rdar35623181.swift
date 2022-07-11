// RUN: %target-swift-frontend -emit-sil -verify %s | %FileCheck %s

extension Sequence where Element == String {
  func record() -> String {
    // CHECK: function_ref @$ss20LazySequenceProtocolPsE3mapys0a3MapB0Vy8ElementsQzqd__Gqd__7ElementQzclF : $@convention(method) <τ_0_0 where τ_0_0 : LazySequenceProtocol><τ_1_0> (@guaranteed @callee_guaranteed @substituted <τ_0_0, τ_0_1> (@in_guaranteed τ_0_0) -> @out τ_0_1 for <τ_0_0.Element, τ_1_0>, @in_guaranteed τ_0_0) -> @out LazyMapSequence<τ_0_0.Elements, τ_1_0
    return lazy.map({ $0 }).joined(separator: ",")
  }
}
