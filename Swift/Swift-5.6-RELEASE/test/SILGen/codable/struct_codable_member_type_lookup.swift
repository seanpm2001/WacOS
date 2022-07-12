// RUN: %target-swift-frontend -emit-silgen %s | %FileCheck %s

// Make sure we have an int, not a float.
//
// CHECK-LABEL: sil hidden [ossa] @$s33struct_codable_member_type_lookup32StaticInstanceNameDisambiguationV6encode2to{{.*}}F : $@convention(method) (@in_guaranteed Encoder, StaticInstanceNameDisambiguation) -> @error Error {
// CHECK: bb0([[ENCODER:%.*]] : $*Encoder, [[INPUT:%.*]] : $StaticInstanceNameDisambiguation):
// CHECK:   [[INT_VALUE:%.*]] = struct_extract [[INPUT]]
// CHECK:   [[FUNC:%.*]] = function_ref @$ss22KeyedEncodingContainerV6encode_6forKeyySi_xtKF : $@convention(method) <τ_0_0 where τ_0_0 : CodingKey> (Int, @in_guaranteed τ_0_0, @inout KeyedEncodingContainer<τ_0_0>) -> @error Error
// CHECK:   try_apply [[FUNC]]<StaticInstanceNameDisambiguation.CodingKeys>([[INT_VALUE]],
// CHECK: } // end sil function '$s33struct_codable_member_type_lookup32StaticInstanceNameDisambiguationV6encode2toys7Encoder_p_tKF'
struct StaticInstanceNameDisambiguation : Codable {
  static let version: Float = 0.42
  let version: Int = 42
}
