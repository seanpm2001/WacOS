// RUN: %target-typecheck-verify-swift -debug-generic-signatures -requirement-machine-protocol-signatures=on 2>&1 | %FileCheck %s
// RUN: %target-typecheck-verify-swift -debug-generic-signatures -requirement-machine-protocol-signatures=off 2>&1 | %FileCheck %s

// https://bugs.swift.org/browse/SR-13850

// CHECK: Requirement signature: <Self where Self.A : P2>
protocol P1 {
  associatedtype A: P2
}

// CHECK: Requirement signature: <Self>
protocol P2 {
  associatedtype A
}

// Neither one of 'P3', 'P4' or 'f()' should have diagnosed
// redundant conformance requirements.

// CHECK: Requirement signature: <Self where Self : P2, Self == Self.A.A, Self.A : P1>
protocol P3 : P2 where Self.A: P1, Self.A.A == Self { }

// CHECK: Requirement signature: <Self where Self.X : P2, Self.X == Self.X.A.A, Self.X.A : P1>
protocol P4 {
  associatedtype X where X : P2, X.A: P1, X.A.A == X
}

// CHECK: Generic signature: <T where T : P2, T == T.A.A, T.A : P1>
func f<T : P2>(_: T) where T.A : P1, T.A.A == T { }
