// RUN: %target-swift-frontend %s -emit-ir | %FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-%target-runtime

// REQUIRES: CPU=x86_64

// CHECK: [[A:%T13generic_types1AC]] = type <{ [[REF:%swift.refcounted]], [[INT:%TSi]] }>
// CHECK: [[INT]] = type <{ i64 }>
// CHECK: [[B:%T13generic_types1BC]] = type <{ [[REF:%swift.refcounted]], [[UNSAFE:%TSp]] }>
// CHECK: [[C:%T13generic_types1CC]] = type
// CHECK: [[D:%T13generic_types1DC]] = type

// CHECK-LABEL: @_T013generic_types1ACMP = internal global
// CHECK:   %swift.type* (%swift.type_pattern*, i8**)* @create_generic_metadata_A,
// CHECK-native-SAME: i32 160,
// CHECK-objc-SAME:   i32 344,
// CHECK-SAME:   i16 1,
// CHECK-SAME:   i16 16,
// CHECK-SAME:   [{{[0-9]+}} x i8*] zeroinitializer,
// CHECK-SAME:   void ([[A]]*)* @_T013generic_types1ACfD,
// CHECK-SAME:   i8** @_T0BoWV,
// CHECK-SAME:   i64 0,
// CHECK-SAME:   %swift.type* null,
// CHECK-native-SAME: %swift.opaque* null,
// CHECK-objc-SAME:   %swift.opaque* @_objc_empty_cache,
// CHECK-SAME:   %swift.opaque* null,
// CHECK-SAME:   i64 1,
// CHECK-SAME:   i32 3,
// CHECK-SAME:   i32 0,
// CHECK-SAME:   i32 24,
// CHECK-SAME:   i16 7,
// CHECK-SAME:   i16 0,
// CHECK-SAME:   i32 152,
// CHECK-SAME:   i32 16,
// CHECK-SAME:   i8* null,
// CHECK-SAME:   i8* null,
// CHECK-SAME:   i8* null
// CHECK-SAME: }
// CHECK-LABEL: @_T013generic_types1BCMP = internal global
// CHECK-SAME:   %swift.type* (%swift.type_pattern*, i8**)* @create_generic_metadata_B,
// CHECK-native-SAME: i32 152,
// CHECK-objc-SAME:   i32 336,
// CHECK-SAME:   i16 1,
// CHECK-SAME:   i16 16,
// CHECK-SAME:   [{{[0-9]+}} x i8*] zeroinitializer,
// CHECK-SAME:   void ([[B]]*)* @_T013generic_types1BCfD,
// CHECK-SAME:   i8** @_T0BoWV,
// CHECK-SAME:   i64 0,
// CHECK-SAME:   %swift.type* null,
// CHECK-native-SAME: %swift.opaque* null,
// CHECK-objc-SAME:   %swift.opaque* @_objc_empty_cache,
// CHECK-SAME:   %swift.opaque* null,
// CHECK-SAME:   i64 1,
// CHECK-SAME:   i32 3,
// CHECK-SAME:   i32 0,
// CHECK-SAME:   i32 24,
// CHECK-SAME:   i16 7,
// CHECK-SAME:   i16 0,
// CHECK-SAME:   i32 144,
// CHECK-SAME:   i32 16,
// CHECK-SAME:   %swift.type* null
// CHECK-SAME: }
// CHECK-LABEL: @_T013generic_types1CCMP = internal global
// CHECK-SAME:   void ([[C]]*)* @_T013generic_types1CCfD,
// CHECK-SAME:   i8** @_T0BoWV,
// CHECK-SAME:   i64 0,
// CHECK-SAME:   %swift.type* null,
// CHECK-native-SAME: %swift.opaque* null,
// CHECK-objc-SAME:   %swift.opaque* @_objc_empty_cache,
// CHECK-SAME:   %swift.opaque* null,
// CHECK-SAME:   i64 1,
// CHECK-SAME: }
// CHECK-LABEL: @_T013generic_types1DCMP = internal global
// CHECK-SAME:   void ([[D]]*)* @_T013generic_types1DCfD,
// CHECK-SAME:   i8** @_T0BoWV,
// CHECK-SAME:   i64 0,
// CHECK-SAME:   %swift.type* null,
// CHECK-native-SAME: %swift.opaque* null,
// CHECK-objc-SAME:   %swift.opaque* @_objc_empty_cache,
// CHECK-SAME:   %swift.opaque* null,
// CHECK-SAME:   i64 1,
// CHECK-SAME: }

// CHECK-LABEL: define{{( protected)?}} private %swift.type* @create_generic_metadata_A(%swift.type_pattern*, i8**) {{.*}} {
// CHECK:   [[T0:%.*]] = bitcast i8** %1 to %swift.type**
// CHECK:   %T = load %swift.type*, %swift.type** [[T0]],
// CHECK-native: [[METADATA:%.*]] = call %swift.type* @swift_allocateGenericClassMetadata(%swift.type_pattern* %0, i8** %1, %objc_class* null)
// CHECK-objc:   [[T0:%.*]] = load %objc_class*, %objc_class** @"OBJC_CLASS_REF_$_SwiftObject"
// CHECK-objc:   [[SUPER:%.*]] = call %objc_class* @swift_rt_swift_getInitializedObjCClass(%objc_class* [[T0]])
// CHECK-objc:   [[METADATA:%.*]] = call %swift.type* @swift_allocateGenericClassMetadata(%swift.type_pattern* %0, i8** %1, %objc_class* [[SUPER]])
// CHECK:   [[SELF_ARRAY:%.*]] = bitcast %swift.type* [[METADATA]] to i8**
// CHECK:   [[T1:%.*]] = getelementptr inbounds i8*, i8** [[SELF_ARRAY]], i32 10
// CHECK:   [[T0:%.*]] = bitcast %swift.type* %T to i8*
// CHECK:   store i8* [[T0]], i8** [[T1]], align 8
// CHECK:   ret %swift.type* [[METADATA]]
// CHECK: }

// CHECK-LABEL: define{{( protected)?}} private %swift.type* @create_generic_metadata_B(%swift.type_pattern*, i8**) {{.*}} {
// CHECK:   [[T0:%.*]] = bitcast i8** %1 to %swift.type**
// CHECK:   %T = load %swift.type*, %swift.type** [[T0]],
// CHECK-native: [[METADATA:%.*]] = call %swift.type* @swift_allocateGenericClassMetadata(%swift.type_pattern* %0, i8** %1, %objc_class* null)
// CHECK-objc:   [[T0:%.*]] = load %objc_class*, %objc_class** @"OBJC_CLASS_REF_$_SwiftObject"
// CHECK-objc:   [[SUPER:%.*]] = call %objc_class* @swift_rt_swift_getInitializedObjCClass(%objc_class* [[T0]])
// CHECK-objc:   [[METADATA:%.*]] = call %swift.type* @swift_allocateGenericClassMetadata(%swift.type_pattern* %0, i8** %1, %objc_class* [[SUPER]])
// CHECK:   [[SELF_ARRAY:%.*]] = bitcast %swift.type* [[METADATA]] to i8**
// CHECK:   [[T1:%.*]] = getelementptr inbounds i8*, i8** [[SELF_ARRAY]], i32 10
// CHECK:   [[T0:%.*]] = bitcast %swift.type* %T to i8*
// CHECK:   store i8* [[T0]], i8** [[T1]], align 8
// CHECK:   ret %swift.type* [[METADATA]]
// CHECK: }

class A<T> {
  var x = 0

  func run(_ t: T) {}
  init(y : Int) {}
}

class B<T> {
  var ptr : UnsafeMutablePointer<T>
  init(ptr: UnsafeMutablePointer<T>) {
    self.ptr = ptr
  }
  deinit {
    ptr.deinitialize(count: 1)
  }
}

class C<T> : A<Int> {}

class D<T> : A<Int> {
  override func run(_ t: Int) {}
}

struct E<T> {
  var x : Int
  func foo() { bar() }
  func bar() {}
}

class ClassA {}
class ClassB {}

// This type is fixed-size across specializations, but it needs to use
// a different implementation in IR-gen so that types match up.
// It just asserts if we get it wrong.
struct F<T: AnyObject> {
  var value: T
}
func testFixed() {
  var a = F(value: ClassA()).value
  var b = F(value: ClassB()).value
}
