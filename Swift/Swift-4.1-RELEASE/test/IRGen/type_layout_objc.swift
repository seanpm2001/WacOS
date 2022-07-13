// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -emit-ir %s | %FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-%target-ptrsize
// REQUIRES: objc_interop

import Foundation

class C {}
class O: NSObject {}

struct SSing { var x: Int64 }
struct SMult { var x, y: Int64 }
struct SMult2 { var x, y: C }
struct SMult3 { var x, y: C }

struct GSing<T> { var x: T }
struct GMult<T> { var x, y: T }

enum ESing { case X(Int64) }
enum EMult { case X(Int64), Y(Int64) }

@_alignment(4)
struct CommonLayout { var x,y,z,w: Int8 }

// CHECK:       @_T016type_layout_objc14TypeLayoutTestVMP = internal global {{.*}} @create_generic_metadata_TypeLayoutTest
// CHECK:       define private %swift.type* @create_generic_metadata_TypeLayoutTest
struct TypeLayoutTest<T> {
  // -- dynamic layout, projected from metadata
  // CHECK:       [[T0:%.*]] = bitcast %swift.type* %T to i8***
  // CHECK:       [[T1:%.*]] = getelementptr inbounds i8**, i8*** [[T0]], {{i32|i64}} -1
  // CHECK:       [[T_VALUE_WITNESSES:%.*]] = load i8**, i8*** [[T1]]
  // CHECK:       [[T_LAYOUT:%.*]] = getelementptr inbounds i8*, i8** [[T_VALUE_WITNESSES]], i32 9
  // CHECK:       store i8** [[T_LAYOUT]]
  var z: T
  // -- native class, use standard NativeObject value witness
  // CHECK:       store i8** getelementptr inbounds (i8*, i8** @_T0BoWV, i32 9)
  var a: C
  // -- ObjC class, use standard UnknownObject value witness
  // CHECK:       store i8** getelementptr inbounds (i8*, i8** @_T0BOWV, i32 9)
  var b: O
  // -- Single-element struct, shares layout of its field (Builtin.Int64)
  // CHECK:       store i8** getelementptr inbounds (i8*, i8** @_T0Bi64_WV, i32 9)
  var c: SSing
  // -- Multi-element structs use open-coded layouts
  // CHECK:    store i8** getelementptr inbounds ([3 x i8*], [3 x i8*]* @type_layout_16_8_0_pod, i32 0, i32 0)
  var d: SMult
  // CHECK-64:    store i8** getelementptr inbounds ([4 x i8*], [4 x i8*]* @type_layout_16_8_7fffffff_bt, i32 0, i32 0)
  // CHECK-32:    store i8** getelementptr inbounds ([4 x i8*], [4 x i8*]* @type_layout_8_4_1000_bt, i32 0, i32 0)
  var e: SMult2
  // CHECK-64:    store i8** getelementptr inbounds ([4 x i8*], [4 x i8*]* @type_layout_16_8_7fffffff_bt, i32 0, i32 0)
  // CHECK-32:    store i8** getelementptr inbounds ([4 x i8*], [4 x i8*]* @type_layout_8_4_1000_bt, i32 0, i32 0)
  var f: SMult3
  // -- Single-case enum, shares layout of its field (Builtin.Int64)
  // CHECK:       store i8** getelementptr inbounds (i8*, i8** @_T0Bi64_WV, i32 9)
  var g: ESing
  // -- Multi-case enum, open-coded layout
  // CHECK:    store i8** getelementptr inbounds ([3 x i8*], [3 x i8*]* @type_layout_9_8_0_pod, i32 0, i32 0)
  var h: EMult
  // -- Single-element generic struct, shares layout of its field (T)
  // CHECK:       [[T_LAYOUT:%.*]] = getelementptr inbounds i8*, i8** [[T_VALUE_WITNESSES]], i32 9
  // CHECK:       store i8** [[T_LAYOUT]]
  var i: GSing<T>
  // -- Multi-element generic struct, need to derive from metadata
  // CHECK:       [[METADATA:%.*]] = call %swift.type* @_T016type_layout_objc5GMultVMa(%swift.type* %T)
  // CHECK:       [[T0:%.*]] = bitcast %swift.type* [[METADATA]] to i8***
  // CHECK:       [[T1:%.*]] = getelementptr inbounds i8**, i8*** [[T0]], {{i32|i64}} -1
  // CHECK:       [[VALUE_WITNESSES:%.*]] = load i8**, i8*** [[T1]]
  // CHECK:       [[LAYOUT:%.*]] = getelementptr inbounds i8*, i8** [[VALUE_WITNESSES]], i32 9
  // CHECK:       store i8** [[LAYOUT]]
  var j: GMult<T>
  // -- Common layout, reuse common value witness table layout
  // CHECK:       store i8** getelementptr (i8*, i8** @_T0Bi32_WV, i32 9)
  var k: CommonLayout
}
