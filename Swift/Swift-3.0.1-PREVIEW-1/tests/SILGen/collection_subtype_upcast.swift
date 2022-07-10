// RUN: %target-swift-frontend -enable-experimental-collection-casts -emit-silgen -sdk %S/Inputs %s | %FileCheck %s

struct S { var x, y: Int }

// CHECK-LABEL: sil hidden @_TF25collection_subtype_upcast12array_upcastFT5arrayGSaVS_1S__GSaP__ :
// CHECK:    bb0(%0 : $Array<S>):
// CHECK-NEXT: debug_value %0
// CHECK-NEXT: retain_value %0
// CHECK-NEXT: // function_ref
// CHECK-NEXT: [[FN:%.*]] = function_ref @_TFs15_arrayForceCastu0_rFGSax_GSaq__
// CHECK-NEXT: [[RESULT:%.*]] = apply [[FN]]<S, Any>(%0) : $@convention(thin) <τ_0_0, τ_0_1> (@owned Array<τ_0_0>) -> @owned Array<τ_0_1>
// CHECK-NEXT: release_value %0
// CHECK-NEXT: return [[RESULT]]
func array_upcast(array: [S]) -> [Any] {
  return array
}

extension S : Hashable {
  var hashValue : Int {
    return x + y
  }
}
func ==(lhs: S, rhs: S) -> Bool {
  return true
}

// FIXME: This entrypoint name should not be bridging-specific
// CHECK-LABEL:      sil hidden @_TF25collection_subtype_upcast11dict_upcastFT4dictGVs10DictionaryVS_1SSi__GS0_S1_P__ :
// CHECK:    bb0(%0 : $Dictionary<S, Int>):
// CHECK-NEXT: debug_value %0
// CHECK-NEXT: retain_value %0
// CHECK-NEXT: // function_ref
// CHECK-NEXT: [[FN:%.*]] = function_ref @_TFs17_dictionaryUpCastu2_Rxs8Hashable0_S_rFGVs10Dictionaryxq__GS0_q0_q1__
// CHECK-NEXT: [[RESULT:%.*]] = apply [[FN]]<S, Int, S, Any>(%0) : $@convention(thin) <τ_0_0, τ_0_1, τ_0_2, τ_0_3 where τ_0_0 : Hashable, τ_0_2 : Hashable> (@owned Dictionary<τ_0_0, τ_0_1>) -> @owned Dictionary<τ_0_2, τ_0_3>
// CHECK-NEXT: release_value %0
// CHECK-NEXT: return [[RESULT]]
func dict_upcast(dict: [S: Int]) -> [S: Any] {
  return dict
}

// It's not actually possible to test this for Sets independent of
// the bridging rules.
