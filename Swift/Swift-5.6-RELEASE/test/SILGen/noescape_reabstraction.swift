
// RUN: %target-swift-emit-silgen -module-name noescape_reabstraction %s | %FileCheck %s

struct S {}

func noescape_concrete(_ x: (S) -> S) {
  noescape_generic(x)
}

func noescape_generic<T>(_ x: (T) -> T) {
}

// CHECK-LABEL: sil hidden [ossa] @$s22noescape_reabstraction0A9_concreteyyAA1SVADXEF
// CHECK:         function_ref [[REABSTRACTION_THUNK:@\$s22noescape_reabstraction1SVACIgyd_A2CIegnr_TR]]

func concrete(_ x: (S) -> S) {
  noescape_generic(x)
}

func generic<T>(_ x: (T) -> T) {
}

// CHECK-LABEL: sil hidden [ossa] @$s22noescape_reabstraction8concreteyyAA1SVADXEF
// CHECK:         function_ref [[REABSTRACTION_THUNK]]
