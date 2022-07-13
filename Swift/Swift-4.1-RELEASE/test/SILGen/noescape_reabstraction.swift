// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership %s | %FileCheck %s

struct S {}

func noescape_concrete(_ x: (S) -> S) {
  noescape_generic(x)
}

func noescape_generic<T>(_ x: (T) -> T) {
}

// CHECK-LABEL: sil hidden @_T022noescape_reabstraction0A9_concreteyAA1SVADcF
// CHECK:         function_ref [[REABSTRACTION_THUNK:@_T022noescape_reabstraction1SVACIgyd_A2CIgir_TR]]

func concrete(_ x: (S) -> S) {
  noescape_generic(x)
}

func generic<T>(_ x: (T) -> T) {
}

// CHECK-LABEL: sil hidden @_T022noescape_reabstraction8concreteyAA1SVADcF
// CHECK:         function_ref [[REABSTRACTION_THUNK]]
