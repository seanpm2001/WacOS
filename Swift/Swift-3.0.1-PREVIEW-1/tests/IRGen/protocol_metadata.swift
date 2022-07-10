// RUN: %target-swift-frontend -primary-file %s -emit-ir -disable-objc-attr-requires-foundation-module | %FileCheck %s

// REQUIRES: CPU=x86_64
// REQUIRES: objc_interop

protocol A { func a() }
protocol B { func b() }
protocol C : class { func c() }
@objc protocol O { func o() }
@objc protocol OPT { 
  @objc optional func opt()
  @objc optional static func static_opt()

  @objc optional var prop: O { get }
  @objc optional subscript (x: O) -> O { get }
}

protocol AB : A, B { func ab() }
protocol ABO : A, B, O { func abo() }

// CHECK: @_TMp17protocol_metadata1A = hidden constant %swift.protocol {
// -- size 72
// -- flags: 1 = Swift | 2 = Not Class-Constrained | 4 = Needs Witness Table
// CHECK:   i32 72, i32 7,
// CHECK:   i16 0, i16 0
// CHECK: }

// CHECK: @_TMp17protocol_metadata1B = hidden constant %swift.protocol {
// CHECK:   i32 72, i32 7,
// CHECK:   i16 0, i16 0
// CHECK: }

// CHECK: @_TMp17protocol_metadata1C = hidden constant %swift.protocol {
// -- flags: 1 = Swift | 4 = Needs Witness Table
// CHECK:   i32 72, i32 5,
// CHECK:   i16 0, i16 0
// CHECK: }

// -- @objc protocol O uses ObjC symbol mangling and layout
// CHECK: @_PROTOCOL__TtP17protocol_metadata1O_ = private constant { {{.*}} i32, { [1 x i8*] }*, i8*, i8* } {
// CHECK:   @_PROTOCOL_INSTANCE_METHODS__TtP17protocol_metadata1O_,
// -- size, flags: 1 = Swift
// CHECK:   i32 96, i32 1
// CHECK: }
// CHECK: @_PROTOCOL_METHOD_TYPES__TtP17protocol_metadata1O_
// CHECK: }

// -- @objc protocol OPT uses ObjC symbol mangling and layout
// CHECK: @_PROTOCOL__TtP17protocol_metadata3OPT_ = private constant { {{.*}} i32, { [4 x i8*] }*, i8*, i8* } {
// CHECK:   @_PROTOCOL_INSTANCE_METHODS_OPT__TtP17protocol_metadata3OPT_,
// CHECK:   @_PROTOCOL_CLASS_METHODS_OPT__TtP17protocol_metadata3OPT_,
// -- size, flags: 1 = Swift
// CHECK:   i32 96, i32 1
// CHECK: }
// CHECK: @_PROTOCOL_METHOD_TYPES__TtP17protocol_metadata3OPT_
// CHECK: }

// -- inheritance lists for refined protocols

// CHECK: [[AB_INHERITED:@.*]] = private constant { {{.*}}* } {
// CHECK:   i64 2,
// CHECK:   %swift.protocol* @_TMp17protocol_metadata1A,
// CHECK:   %swift.protocol* @_TMp17protocol_metadata1B
// CHECK: }
// CHECK: @_TMp17protocol_metadata2AB = hidden constant %swift.protocol { 
// CHECK:   [[AB_INHERITED]]
// CHECK:   i32 72, i32 7,
// CHECK:   i16 0, i16 0
// CHECK: }

// CHECK: [[ABO_INHERITED:@.*]] = private constant { {{.*}}* } {
// CHECK:   i64 3,
// CHECK:   %swift.protocol* @_TMp17protocol_metadata1A,
// CHECK:   %swift.protocol* @_TMp17protocol_metadata1B,
// CHECK:   {{.*}}* @_PROTOCOL__TtP17protocol_metadata1O_
// CHECK: }

func reify_metadata<T>(_ x: T) {}

// CHECK: define hidden void @_TF17protocol_metadata14protocol_types
func protocol_types(_ a: A,
                    abc: A & B & C,
                    abco: A & B & C & O) {
  // CHECK: store %swift.protocol* @_TMp17protocol_metadata1A
  // CHECK: call %swift.type* @rt_swift_getExistentialTypeMetadata(i64 1, %swift.protocol** {{%.*}})
  reify_metadata(a)
  // CHECK: store %swift.protocol* @_TMp17protocol_metadata1A
  // CHECK: store %swift.protocol* @_TMp17protocol_metadata1B
  // CHECK: store %swift.protocol* @_TMp17protocol_metadata1C
  // CHECK: call %swift.type* @rt_swift_getExistentialTypeMetadata(i64 3, %swift.protocol** {{%.*}})
  reify_metadata(abc)
  // CHECK: store %swift.protocol* @_TMp17protocol_metadata1A
  // CHECK: store %swift.protocol* @_TMp17protocol_metadata1B
  // CHECK: store %swift.protocol* @_TMp17protocol_metadata1C
  // CHECK: [[O_REF:%.*]] = load i8*, i8** @"\01l_OBJC_PROTOCOL_REFERENCE_$__TtP17protocol_metadata1O_"
  // CHECK: [[O_REF_BITCAST:%.*]] = bitcast i8* [[O_REF]] to %swift.protocol*
  // CHECK: store %swift.protocol* [[O_REF_BITCAST]]
  // CHECK: call %swift.type* @rt_swift_getExistentialTypeMetadata(i64 4, %swift.protocol** {{%.*}})
  reify_metadata(abco)
}

