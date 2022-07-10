// RUN: rm -rf %t && mkdir %t
// RUN: %build-irgen-test-overlays
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) -primary-file %s -emit-ir -disable-objc-attr-requires-foundation-module | %FileCheck %s

// REQUIRES: CPU=x86_64

// FIXME: rdar://problem/19648117 Needs splitting objc parts out
// XFAIL: linux

import Foundation
import gizmo

// -- Protocol records for cast-to ObjC protocols
// CHECK: @_PROTOCOL__TtP13generic_casts10ObjCProto1_ = private constant
// CHECK: @"\01l_OBJC_LABEL_PROTOCOL_$__TtP13generic_casts10ObjCProto1_" = weak hidden global i8* bitcast ({{.*}} @_PROTOCOL__TtP13generic_casts10ObjCProto1_ to i8*), section "__DATA,__objc_protolist,coalesced,no_dead_strip"
// CHECK: @"\01l_OBJC_PROTOCOL_REFERENCE_$__TtP13generic_casts10ObjCProto1_" = weak hidden global i8* bitcast ({{.*}} @_PROTOCOL__TtP13generic_casts10ObjCProto1_ to i8*), section "__DATA,__objc_protorefs,coalesced,no_dead_strip"

// CHECK: @_PROTOCOL_NSRuncing = private constant
// CHECK: @"\01l_OBJC_LABEL_PROTOCOL_$_NSRuncing" = weak hidden global i8* bitcast ({{.*}} @_PROTOCOL_NSRuncing to i8*), section "__DATA,__objc_protolist,coalesced,no_dead_strip"
// CHECK: @"\01l_OBJC_PROTOCOL_REFERENCE_$_NSRuncing" = weak hidden global i8* bitcast ({{.*}} @_PROTOCOL_NSRuncing to i8*), section "__DATA,__objc_protorefs,coalesced,no_dead_strip"

// CHECK: @_PROTOCOLS__TtC13generic_casts10ObjCClass2 = private constant { i64, [1 x i8*] } {
// CHECK:   i64 1, 
// CHECK:   @_PROTOCOL__TtP13generic_casts10ObjCProto2_
// CHECK: }

// CHECK: @_DATA__TtC13generic_casts10ObjCClass2 = private constant {{.*}} @_PROTOCOLS__TtC13generic_casts10ObjCClass2

// CHECK: @_PROTOCOL_PROTOCOLS__TtP13generic_casts10ObjCProto2_ = private constant { i64, [1 x i8*] } {
// CHECK:   i64 1, 
// CHECK:   @_PROTOCOL__TtP13generic_casts10ObjCProto1_
// CHECK: }

// CHECK: define hidden i64 @_TF13generic_casts8allToInt{{.*}}(%swift.opaque* noalias nocapture, %swift.type* %T)
func allToInt<T>(_ x: T) -> Int {
  return x as! Int
  // CHECK: [[BUF:%.*]] = alloca [[BUFFER:.24 x i8.]],
  // CHECK: [[INT_TEMP:%.*]] = alloca %Si,
  // CHECK: [[TEMP:%.*]] = call %swift.opaque* {{.*}}([[BUFFER]]* [[BUF]], %swift.opaque* %0, %swift.type* %T)
  // CHECK: [[T0:%.*]] = bitcast %Si* [[INT_TEMP]] to %swift.opaque*
  // CHECK: call i1 @rt_swift_dynamicCast(%swift.opaque* [[T0]], %swift.opaque* [[TEMP]], %swift.type* %T, %swift.type* @_TMSi, i64 7)
  // CHECK: [[T0:%.*]] = getelementptr inbounds %Si, %Si* [[INT_TEMP]], i32 0, i32 0
  // CHECK: [[INT_RESULT:%.*]] = load i64, i64* [[T0]],
  // CHECK: ret i64 [[INT_RESULT]]
}

// CHECK: define hidden void @_TF13generic_casts8intToAll{{.*}}(%swift.opaque* noalias nocapture sret, i64, %swift.type* %T) {{.*}} {
func intToAll<T>(_ x: Int) -> T {
  // CHECK: [[INT_TEMP:%.*]] = alloca %Si,
  // CHECK: [[T0:%.*]] = getelementptr inbounds %Si, %Si* [[INT_TEMP]], i32 0, i32 0
  // CHECK: store i64 %1, i64* [[T0]],
  // CHECK: [[T0:%.*]] = bitcast %Si* [[INT_TEMP]] to %swift.opaque*
  // CHECK: call i1 @rt_swift_dynamicCast(%swift.opaque* %0, %swift.opaque* [[T0]], %swift.type* @_TMSi, %swift.type* %T, i64 7)
  return x as! T
}

// CHECK: define hidden i64 @_TF13generic_casts8anyToInt{{.*}}(%Any* noalias nocapture dereferenceable({{.*}})) {{.*}} {
func anyToInt(_ x: Any) -> Int {
  return x as! Int
}

@objc protocol ObjCProto1 {
  static func forClass()
  static func forInstance()

  var prop: NSObject { get }
}

@objc protocol ObjCProto2 : ObjCProto1 {}

@objc class ObjCClass {}

// CHECK: define hidden %objc_object* @_TF13generic_casts9protoCast{{.*}}(%C13generic_casts9ObjCClass*) {{.*}} {
func protoCast(_ x: ObjCClass) -> ObjCProto1 & NSRuncing {
  // CHECK: load i8*, i8** @"\01l_OBJC_PROTOCOL_REFERENCE_$__TtP13generic_casts10ObjCProto1_"
  // CHECK: load i8*, i8** @"\01l_OBJC_PROTOCOL_REFERENCE_$_NSRuncing"
  // CHECK: call %objc_object* @swift_dynamicCastObjCProtocolUnconditional(%objc_object* {{%.*}}, i64 2, i8** {{%.*}})
  return x as! ObjCProto1 & NSRuncing
}

@objc class ObjCClass2 : NSObject, ObjCProto2 {
  class func forClass() {}
  class func forInstance() {}

  var prop: NSObject { return self }
}

// <rdar://problem/15313840>
// Class existential to opaque archetype cast
// CHECK: define hidden void @_TF13generic_casts33classExistentialToOpaqueArchetype{{.*}}(%swift.opaque* noalias nocapture sret, %objc_object*, %swift.type* %T)
func classExistentialToOpaqueArchetype<T>(_ x: ObjCProto1) -> T {
  var x = x
  // CHECK: [[X:%.*]] = alloca %P13generic_casts10ObjCProto1_
  // CHECK: [[LOCAL:%.*]] = alloca %P13generic_casts10ObjCProto1_
  // CHECK: [[LOCAL_OPAQUE:%.*]] = bitcast %P13generic_casts10ObjCProto1_* [[LOCAL]] to %swift.opaque*
  // CHECK: [[PROTO_TYPE:%.*]] = call %swift.type* @_TMaP13generic_casts10ObjCProto1_()
  // CHECK: call i1 @rt_swift_dynamicCast(%swift.opaque* %0, %swift.opaque* [[LOCAL_OPAQUE]], %swift.type* [[PROTO_TYPE]], %swift.type* %T, i64 7)
  return x as! T
}

protocol P {}
protocol Q {}

// CHECK: define hidden void @_TF13generic_casts19compositionToMemberFPS_1PS_1Q_PS0__{{.*}}(%P13generic_casts1P_* noalias nocapture sret, %"_TP13generic_casts1P&_TP13generic_casts1Q"* noalias nocapture dereferenceable({{.*}})) {{.*}} {
func compositionToMember(_ a: P & Q) -> P {
  return a
}

