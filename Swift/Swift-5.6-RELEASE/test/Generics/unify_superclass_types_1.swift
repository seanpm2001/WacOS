// RUN: %target-typecheck-verify-swift -requirement-machine=verify -dump-requirement-machine 2>&1 | %FileCheck %s

class Base {}
class Derived : Base {
  func derivedMethod() {}
}

protocol P : Base {}

func takesDerived(_: Derived) {}

extension P where Self : Derived {
  func passesDerived() { derivedMethod() }
}

// CHECK-LABEL: Requirement machine for <τ_0_0 where τ_0_0 : Derived, τ_0_0 : P>
// CHECK-NEXT: Rewrite system: {
// CHECK-NEXT: - [P].[P] => [P] [permanent]
// CHECK-NEXT: - [superclass: Base].[layout: _NativeClass] => [superclass: Base] [permanent]
// CHECK-NEXT: - [superclass: Derived].[layout: _NativeClass] => [superclass: Derived] [permanent]
// CHECK-NEXT: - [P].[superclass: Base] => [P]
// CHECK-NEXT: - τ_0_0.[superclass: Derived] => τ_0_0
// CHECK-NEXT: - τ_0_0.[P] => τ_0_0
// CHECK-NEXT: - [P].[layout: _NativeClass] => [P]
// CHECK-NEXT: - τ_0_0.[layout: _NativeClass] => τ_0_0
// CHECK-NEXT: - τ_0_0.[superclass: Base] => τ_0_0
// CHECK-NEXT: }
// CHECK-NEXT: Rewrite loops: {
// CHECK:      }
// CHECK-NEXT: Property map: {
// CHECK-NEXT:   [P] => { layout: _NativeClass superclass: [superclass: Base] }
// CHECK-NEXT:   τ_0_0 => { conforms_to: [P] layout: _NativeClass superclass: [superclass: Derived] }
// CHECK-NEXT: }
