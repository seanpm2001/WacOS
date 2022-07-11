// RUN: %target-swift-frontend -primary-file %s -emit-ir -enable-objc-interop -disable-objc-attr-requires-foundation-module | %FileCheck %s -check-prefix=CHECK -check-prefix=CHECK-%target-ptrauth

// REQUIRES: PTRSIZE=64

// CHECK: [[INDEXED_GETTER_ENCODING:@.+]] = private unnamed_addr constant [11 x i8] c"@24@0:8q16\00"
// CHECK: [[INDEXED_SETTER_ENCODING:@.+]] = private unnamed_addr constant [14 x i8] c"v32@0:8@16q24\00"
// CHECK: [[KEYED_GETTER_ENCODING:@.+]] = private unnamed_addr constant [11 x i8] c"q24@0:8@16\00"
// CHECK: [[KEYED_SETTER_ENCODING:@.+]] = private unnamed_addr constant [14 x i8] c"v32@0:8q16@24\00"

// CHECK: @_INSTANCE_METHODS__TtC15objc_subscripts10SomeObject = 
// CHECK:   internal constant { i32, i32, [5 x { i8*, i8*, i8* }] } 
// CHECK:   { i32 24, i32 5, [5 x { i8*, i8*, i8* }] 
// CHECK:     [
// CHECK:       { i8*, i8*, i8* } 
// CHECK:         { 
// CHECK:           i8* getelementptr inbounds ([26 x i8], [26 x i8]* @"\01L_selector_data(objectAtIndexedSubscript:)", i64 0, i64 0), 
// CHECK:           i8* getelementptr inbounds ([11 x i8], [11 x i8]* [[INDEXED_GETTER_ENCODING]], i64 0, i64 0), 
// CHECK-noptrauth: i8* bitcast ([[OPAQUE0:%.*]]* ([[OPAQUE1:%.*]]*, i8*, i64)* @"$s15objc_subscripts10SomeObjectCyACSicigTo" to i8*)
// CHECK-ptrauth:   i8* bitcast ({ i8*, i32, i64, i64 }* @"$s15objc_subscripts10SomeObjectCyACSicigTo.ptrauth" to i8*)
// CHECK:         }, 
// CHECK:       { i8*, i8*, i8* } 
// CHECK:         { 
// CHECK:           i8* getelementptr inbounds ([30 x i8], [30 x i8]* @"\01L_selector_data(setObject:atIndexedSubscript:)", i64 0, i64 0), 
// CHECK:           i8* getelementptr inbounds ([14 x i8], [14 x i8]* [[INDEXED_SETTER_ENCODING]], i64 0, i64 0), 
// CHECK-noptrauth: i8* bitcast (void ([[OPAQUE2:%.*]]*, i8*, [[OPAQUE3:%.*]]*, i64)* @"$s15objc_subscripts10SomeObjectCyACSicisTo" to i8*)
// CHECK-ptrauth:   i8* bitcast ({ i8*, i32, i64, i64 }* @"$s15objc_subscripts10SomeObjectCyACSicisTo.ptrauth" to i8*)
// CHECK:         },
// CHECK:       { i8*, i8*, i8* } 
// CHECK:         { 
// CHECK:           i8* getelementptr inbounds ([25 x i8], [25 x i8]* @"\01L_selector_data(objectForKeyedSubscript:)", i64 0, i64 0), 
// CHECK:           i8* getelementptr inbounds ([11 x i8], [11 x i8]* [[KEYED_GETTER_ENCODING]], i64 0, i64 0), 
// CHECK-noptrauth: i8* bitcast (i64 ([[OPAQUE4:%.*]]*, i8*, [[OPAQUE5:%.*]]*)* @"$s15objc_subscripts10SomeObjectCySiACcigTo" to i8*)
// CHECK-ptrauth:   i8* bitcast ({ i8*, i32, i64, i64 }* @"$s15objc_subscripts10SomeObjectCySiACcigTo.ptrauth" to i8*)
// CHECK:         }, 
// CHECK:       { i8*, i8*, i8* } 
// CHECK:         { 
// CHECK:           i8* getelementptr inbounds ([29 x i8], [29 x i8]* @"\01L_selector_data(setObject:forKeyedSubscript:)", i64 0, i64 0), 
// CHECK:           i8* getelementptr inbounds ([14 x i8], [14 x i8]* [[KEYED_SETTER_ENCODING]], i64 0, i64 0), 
// CHECK-noptrauth: i8* bitcast (void ([[OPAQUE6:%.*]]*, i8*, i64, [[OPAQUE7:%.*]]*)* @"$s15objc_subscripts10SomeObjectCySiACcisTo" to i8*)
// CHECK-ptrauth:   i8* bitcast ({ i8*, i32, i64, i64 }* @"$s15objc_subscripts10SomeObjectCySiACcisTo.ptrauth" to i8*)
// CHECK:         },
// CHECK:       { i8*, i8*, i8* } 
// CHECK:         {
// CHECK:           i8* getelementptr inbounds ([5 x i8], [5 x i8]* @"\01L_selector_data(init)", i64 0, i64 0),
// CHECK:           i8* getelementptr inbounds ([8 x i8], [8 x i8]* @{{[0-9]+}}, i64 0, i64 0),
// CHECK-noptrauth: i8* bitcast ([[OPAQUE8:%.*]]* ([[OPAQUE9:%.*]]*, i8*)* @"$s15objc_subscripts10SomeObjectCACycfcTo" to i8*)
// CHECK-ptrauth:   i8* bitcast ({ i8*, i32, i64, i64 }* @"$s15objc_subscripts10SomeObjectCACycfcTo.ptrauth" to i8*)
// CHECK:         }
// CHECK:    ]
// CHECK:  }

@objc class SomeObject {
  @objc subscript (i : Int) -> SomeObject {
    // CHECK-noptrauth: define internal [[OPAQUE0:%.*]]* @"$s15objc_subscripts10SomeObjectCyACSicigTo"([[OPAQUE1]]* %0, i8* %1, i64 %2) {{[#0-9]*}} {
    // CHECK-ptrauth:   define internal [[OPAQUE0:%.*]]* @"$s15objc_subscripts10SomeObjectCyACSicigTo"([[OPAQUE1:%.*]]* %0, i8* %1, i64 %2) {{[#0-9]*}} {
    get {
      // CHECK: call swiftcc %T15objc_subscripts10SomeObjectC* @"$s15objc_subscripts10SomeObjectCyACSicig"
      return self
    }

    // CHECK-LABEL: define internal void @"$s15objc_subscripts10SomeObjectCyACSicisTo"
    set {
      // CHECK: swiftcc void @"$s15objc_subscripts10SomeObjectCyACSicis"
    }
  }

  @objc subscript (s : SomeObject) -> Int {
  // CHECK-LABEL: define internal i64 @"$s15objc_subscripts10SomeObjectCySiACcigTo"
    get {
      // CHECK: call swiftcc i64 @"$s15objc_subscripts10SomeObjectCySiACcig"
      return 5
    }

    // CHECK-LABEL: define internal void @"$s15objc_subscripts10SomeObjectCySiACcisTo"
    set {
      // CHECK: call swiftcc void @"$s15objc_subscripts10SomeObjectCySiACcis"
    }
  }

  @objc init() {}
}

