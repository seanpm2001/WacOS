// This file is also used by objc_properties_ios.swift.

// RUN: %swift -target x86_64-apple-macosx10.11 %s -disable-target-os-checking -emit-ir -disable-objc-attr-requires-foundation-module | %FileCheck -check-prefix=CHECK -check-prefix=CHECK-NEW %s
// RUN: %swift -target x86_64-apple-macosx10.10 %s -disable-target-os-checking -emit-ir -disable-objc-attr-requires-foundation-module | %FileCheck -check-prefix=CHECK -check-prefix=CHECK-OLD %s

// REQUIRES: OS=macosx
// REQUIRES: objc_interop

@objc class SomeObject {
  var readonly : SomeObject {
    get {
      return self
    }
  }

  var readwrite : SomeObject {
    get {
      return bareIvar
    }
    set {
      bareIvar = newValue
    }
  }

  var bareIvar : SomeObject

  @objc(wobble) var wibble : SomeObject

  init() { 
    bareIvar = SomeObject()
    wibble  = SomeObject()
  }

  static var sharedProp: Int64 = 0
}

extension SomeObject {
  var extensionProperty : SomeObject {
    get {
      return self
    }
    set {
      bareIvar = self
    }
  }

  class var extensionClassProp : SomeObject.Type {
    return self
  }
}

// <rdar://problem/16952186> Crash with @lazy in @objc class
@objc
class LazyPropertyCrash  {
  lazy var applicationFilesDirectory: LazyPropertyCrash = LazyPropertyCrash()
}

// <rdar://16909436>
@objc class Tree {
  weak var parent: Tree?
}


// <rdar://problem/17127126> swift compiler segfaults trying to generate a setter for a @lazy property in a subclass of NSObject
func test17127126(f : Class17127126) {
  f.x = 2   // this is the problem
}

@objc
class Class17127126 {
  lazy var x = 1
}

@objc protocol Proto {
  var value: Int { get }
  static var sharedInstance: AnyObject { get set }
}



// CHECK: [[READONLY_NAME:@.*]] = private unnamed_addr constant [9 x i8] c"readonly\00"
// CHECK: [[READONLY_ATTRS:@.*]] = private unnamed_addr constant [42 x i8] c"T@\22_TtC15objc_properties10SomeObject\22,N,R\00"

// CHECK: [[GETTER_SIGNATURE:@.*]] = private unnamed_addr constant [8 x i8] c"@16@0:8\00"

// CHECK: [[READWRITE_NAME:@.*]] = private unnamed_addr constant [10 x i8] c"readwrite\00"
// CHECK: [[READWRITE_ATTRS:@.*]] = private unnamed_addr constant [42 x i8] c"T@\22_TtC15objc_properties10SomeObject\22,N,&\00"

// CHECK: [[SETTER_SIGNATURE:@.*]] = private unnamed_addr constant [11 x i8] c"v24@0:8@16\00"

// CHECK: [[BAREIVAR_NAME:@.*]] = private unnamed_addr constant [9 x i8] c"bareIvar\00"
// CHECK: [[BAREIVAR_ATTRS:@.*]] = private unnamed_addr constant [52 x i8] c"T@\22_TtC15objc_properties10SomeObject\22,N,&,VbareIvar\00"

// CHECK: [[WIBBLE_NAME:@.*]] = private unnamed_addr constant [7 x i8] c"wobble\00"
// CHECK: [[WIBBLE_ATTRS:@.*]] = private unnamed_addr constant [50 x i8] c"T@\22_TtC15objc_properties10SomeObject\22,N,&,Vwibble\00"

// CHECK: [[SHARED_NAME:@.*]] = private unnamed_addr constant [11 x i8] c"sharedProp\00"
// CHECK: [[SHARED_ATTRS:@.*]] = private unnamed_addr constant [17 x i8] c"Tq,N,VsharedProp\00"

// CHECK-NEW: @_CLASS_PROPERTIES__TtC15objc_properties10SomeObject = private constant { {{.*}}] } {
// CHECK-NEW:   i32 16,
// CHECK-NEW:   i32 1,
// CHECK-NEW:   [1 x { i8*, i8* }] [{
// CHECK-NEW:     i8* getelementptr inbounds ([11 x i8], [11 x i8]* [[SHARED_NAME]], i64 0, i64 0),
// CHECK-NEW:     i8* getelementptr inbounds ([17 x i8], [17 x i8]* [[SHARED_ATTRS]], i64 0, i64 0)
// CHECK-NEW:   }]
// CHECK-NEW: }, section "__DATA, __objc_const", align 8

// CHECK: @_METACLASS_DATA__TtC15objc_properties10SomeObject = private constant { {{.*}} } {
// CHECK-SAME:   i32 {{[0-9]+}}, i32 {{[0-9]+}}, i32 {{[0-9]+}}, i32 {{[0-9]+}},
// CHECK-SAME:   i8* null,
// CHECK-SAME:   i8* getelementptr inbounds ([{{.+}} x i8], [{{.+}} x i8]* {{@.+}}, i64 0, i64 0),
// CHECK-SAME:   { {{.+}} }* @_CLASS_METHODS__TtC15objc_properties10SomeObject,
// CHECK-SAME:   i8* null, i8* null, i8* null,
// CHECK-NEW-SAME:   { {{.+}} }* @_CLASS_PROPERTIES__TtC15objc_properties10SomeObject
// CHECK-OLD-SAME:   i8* null
// CHECK-SAME: }, section "__DATA, __objc_const", align 8

// CHECK: @_INSTANCE_METHODS__TtC15objc_properties10SomeObject = private constant { {{.*}}] } {
// CHECK:   i32 24,
// CHECK:   i32 8,
// CHECK:   [8 x { i8*, i8*, i8* }] [{
// CHECK:     i8* getelementptr inbounds ([9 x i8], [9 x i8]* @"\01L_selector_data(readonly)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([8 x i8], [8 x i8]* [[GETTER_SIGNATURE]], i64 0, i64 0),
// CHECK:     i8* bitcast ([[OPAQUE0:%.*]]* ([[OPAQUE1:%.*]]*, i8*)* @_TToFC15objc_properties10SomeObjectg8readonlyS0_ to i8*)
// CHECK:   }, {
// CHECK:     i8* getelementptr inbounds ([10 x i8], [10 x i8]* @"\01L_selector_data(readwrite)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([8 x i8], [8 x i8]* [[GETTER_SIGNATURE]], i64 0, i64 0),
// CHECK:     i8* bitcast ([[OPAQUE0]]* ([[OPAQUE1]]*, i8*)* @_TToFC15objc_properties10SomeObjectg9readwriteS0_ to i8*)
// CHECK:   }, {
// CHECK:     i8* getelementptr inbounds ([14 x i8], [14 x i8]* @"\01L_selector_data(setReadwrite:)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([11 x i8], [11 x i8]* [[SETTER_SIGNATURE]], i64 0, i64 0),
// CHECK:     i8* bitcast (void ([[OPAQUE3:%.*]]*, i8*, [[OPAQUE4:%.*]]*)* @_TToFC15objc_properties10SomeObjects9readwriteS0_ to i8*)
// CHECK:   }, {
// CHECK:     i8* getelementptr inbounds ([9 x i8], [9 x i8]* @"\01L_selector_data(bareIvar)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([8 x i8], [8 x i8]* [[GETTER_SIGNATURE]], i64 0, i64 0),
// CHECK:     i8* bitcast ([[OPAQUE0]]* ([[OPAQUE1]]*, i8*)* @_TToFC15objc_properties10SomeObjectg8bareIvarS0_ to i8*)
// CHECK:   }, {
// CHECK:     i8* getelementptr inbounds ([13 x i8], [13 x i8]* @"\01L_selector_data(setBareIvar:)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([11 x i8], [11 x i8]* [[SETTER_SIGNATURE]], i64 0, i64 0),
// CHECK:     i8* bitcast (void ([[OPAQUE3]]*, i8*, [[OPAQUE4]]*)* @_TToFC15objc_properties10SomeObjects8bareIvarS0_ to i8*)
// CHECK:   }, {
// CHECK:     i8* getelementptr inbounds ([7 x i8], [7 x i8]* @"\01L_selector_data(wobble)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([8 x i8], [8 x i8]* [[GETTER_SIGNATURE]], i64 0, i64 0),
// CHECK:     i8* bitcast (%0* (%0*, i8*)* @_TToFC15objc_properties10SomeObjectg6wibbleS0_ to i8*)
// CHECK:   }, {
// CHECK:     i8* getelementptr inbounds ([11 x i8], [11 x i8]* @"\01L_selector_data(setWobble:)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([11 x i8], [11 x i8]* [[SETTER_SIGNATURE]], i64 0, i64 0),
// CHECK:     i8* bitcast (void (%0*, i8*, %0*)* @_TToFC15objc_properties10SomeObjects6wibbleS0_ to i8*)
// CHECK:   }, {
// CHECK:     i8* getelementptr inbounds ([5 x i8], [5 x i8]* @"\01L_selector_data(init)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([8 x i8], [8 x i8]* [[GETTER_SIGNATURE]], i64 0, i64 0),
// CHECK:     i8* bitcast ([[OPAQUE5:%.*]]* ([[OPAQUE6:%.*]]*, i8*)* @_TToFC15objc_properties10SomeObjectcfT_S0_ to i8*)
// CHECK:   }]
// CHECK: }, section "__DATA, __objc_const", align 8

// CHECK: @_PROPERTIES__TtC15objc_properties10SomeObject = private constant { {{.*}}] } {
// CHECK:   i32 16,
// CHECK:   i32 4,
// CHECK:   [4 x { i8*, i8* }] [{
// CHECK:     i8* getelementptr inbounds ([9 x i8], [9 x i8]* [[READONLY_NAME]], i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([42 x i8], [42 x i8]* [[READONLY_ATTRS]], i64 0, i64 0)
// CHECK:   }, {
// CHECK:     i8* getelementptr inbounds ([10 x i8], [10 x i8]* [[READWRITE_NAME]], i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([42 x i8], [42 x i8]* [[READWRITE_ATTRS]], i64 0, i64 0)
// CHECK:   }, {
// CHECK:     i8* getelementptr inbounds ([9 x i8], [9 x i8]* [[BAREIVAR_NAME]], i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([52 x i8], [52 x i8]* [[BAREIVAR_ATTRS]], i64 0, i64 0)
// CHECK:   }, {
// CHECK:     i8* getelementptr inbounds ([7 x i8], [7 x i8]* [[WIBBLE_NAME]], i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([50 x i8], [50 x i8]* [[WIBBLE_ATTRS]], i64 0, i64 0)
// CHECK:   }]
// CHECK: }, section "__DATA, __objc_const", align 8

// CHECK: @_DATA__TtC15objc_properties10SomeObject = private constant { {{.+}} } {
// CHECK:   i32 {{[0-9]+}}, i32 {{[0-9]+}}, i32 {{[0-9]+}}, i32 {{[0-9]+}},
// CHECK:   i8* null,
// CHECK:   i8* getelementptr inbounds ([{{.+}} x i8], [{{.+}} x i8]* {{@.+}}, i64 0, i64 0),
// CHECK:   { {{.+}} }* @_INSTANCE_METHODS__TtC15objc_properties10SomeObject,
// CHECK:   i8* null,
// CHECK:   { {{.+}} }* @_IVARS__TtC15objc_properties10SomeObject,
// CHECK:   i8* null,
// CHECK:   { {{.+}} }* @_PROPERTIES__TtC15objc_properties10SomeObject
// CHECK: }, section "__DATA, __objc_const", align 8

// CHECK: [[EXTENSIONPROPERTY_NAME:@.*]] = private unnamed_addr constant [18 x i8] c"extensionProperty\00"

// CHECK: [[EXTENSIONCLASSPROPERTY_NAME:@.*]] = private unnamed_addr constant [19 x i8] c"extensionClassProp\00"
// CHECK: [[EXTENSIONCLASSPROPERTY_ATTRS:@.*]] = private unnamed_addr constant [7 x i8] c"T#,N,R\00"

// CHECK: @"_CATEGORY_INSTANCE_METHODS__TtC15objc_properties10SomeObject_$_objc_properties" = private constant { {{.*}}] } {
// CHECK:   i32 24,
// CHECK:   i32 2,
// CHECK:   [2 x { i8*, i8*, i8* }] [{
// CHECK:     { i8* getelementptr inbounds ([18 x i8], [18 x i8]* @"\01L_selector_data(extensionProperty)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([8 x i8], [8 x i8]* [[GETTER_SIGNATURE]], i64 0, i64 0),
// CHECK:     i8* bitcast ([[OPAQUE0]]* ([[OPAQUE1]]*, i8*)* @_TToFC15objc_properties10SomeObjectg17extensionPropertyS0_ to i8*)
// CHECK:   }, {
// CHECK:     i8* getelementptr inbounds ([22 x i8], [22 x i8]* @"\01L_selector_data(setExtensionProperty:)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([11 x i8], [11 x i8]* [[SETTER_SIGNATURE]], i64 0, i64 0),
// CHECK:     i8* bitcast (void ([[OPAQUE3]]*, i8*, [[OPAQUE4]]*)* @_TToFC15objc_properties10SomeObjects17extensionPropertyS0_ to i8*)
// CHECK:   }]
// CHECK: }, section "__DATA, __objc_const", align 8

// CHECK: @"_CATEGORY_PROPERTIES__TtC15objc_properties10SomeObject_$_objc_properties" = private constant { {{.*}}] } {
// CHECK:   i32 16,
// CHECK:   i32 1,
// CHECK:   [1 x { i8*, i8* }] [{
// CHECK:     i8* getelementptr inbounds ([18 x i8], [18 x i8]* [[EXTENSIONPROPERTY_NAME]], i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([42 x i8], [42 x i8]* [[READWRITE_ATTRS]], i64 0, i64 0)
// CHECK:   }]
// CHECK: }, section "__DATA, __objc_const", align 8

// CHECK-NEW: @"_CATEGORY_CLASS_PROPERTIES__TtC15objc_properties10SomeObject_$_objc_properties" = private constant { {{.*}}] } {
// CHECK-NEW:   i32 16,
// CHECK-NEW:   i32 1,
// CHECK-NEW:   [1 x { i8*, i8* }] [{
// CHECK-NEW:     i8* getelementptr inbounds ([19 x i8], [19 x i8]* [[EXTENSIONCLASSPROPERTY_NAME]], i64 0, i64 0),
// CHECK-NEW:     i8* getelementptr inbounds ([7 x i8], [7 x i8]* [[EXTENSIONCLASSPROPERTY_ATTRS]], i64 0, i64 0)
// CHECK-NEW:   }]
// CHECK-NEW: }, section "__DATA, __objc_const", align 8

// CHECK: @"_CATEGORY__TtC15objc_properties10SomeObject_$_objc_properties" = private constant { {{.+}} } {
// CHECK:   i8* getelementptr inbounds ([{{.+}} x i8], [{{.+}} x i8]* {{@.+}}, i64 0, i64 0),
// CHECK:   %swift.type* bitcast (i64* getelementptr inbounds (<{ {{.+}} }>* @_TMfC15objc_properties10SomeObject, i32 0, i32 2) to %swift.type*),
// CHECK:   { {{.+}} }* @"_CATEGORY_INSTANCE_METHODS__TtC15objc_properties10SomeObject_$_objc_properties",
// CHECK:   { {{.+}} }* @"_CATEGORY_CLASS_METHODS__TtC15objc_properties10SomeObject_$_objc_properties",
// CHECK:   i8* null,
// CHECK:   { {{.+}} }* @"_CATEGORY_PROPERTIES__TtC15objc_properties10SomeObject_$_objc_properties", 
// CHECK-NEW:   { {{.+}} }* @"_CATEGORY_CLASS_PROPERTIES__TtC15objc_properties10SomeObject_$_objc_properties",
// CHECK-OLD:   i8* null,
// CHECK:   i32 60
// CHECK: }, section "__DATA, __objc_const", align 8


// CHECK: @_INSTANCE_METHODS__TtC15objc_properties4Tree =
// CHECK:    i8* getelementptr inbounds ([7 x i8], [7 x i8]* @"\01L_selector_data(parent)", i64 0, i64 0),
// CHECK:    i8* getelementptr inbounds ([8 x i8], [8 x i8]* [[GETTER_SIGNATURE]], i64 0, i64 0),
// CHECK:    i8* bitcast (%2* (%2*, i8*)* @_TToFC15objc_properties4Treeg6parentXwGSqS0__ to i8*)
// CHECK:    i8* getelementptr inbounds ([11 x i8], [11 x i8]* @"\01L_selector_data(setParent:)", i64 0, i64 0),
// CHECK:    i8* getelementptr inbounds ([11 x i8], [11 x i8]* [[SETTER_SIGNATURE]], i64 0, i64 0),
// CHECK:    i8* bitcast (void (%2*, i8*, %2*)* @_TToFC15objc_properties4Trees6parentXwGSqS0__ to i8*)

// CHECK: @_PROTOCOL__TtP15objc_properties5Proto_ = private constant { {{.+}} } {
// CHECK:   i8* null,
// CHECK:   i8* getelementptr inbounds ([{{.+}} x i8], [{{.+}} x i8]* {{@.+}}, i64 0, i64 0),
// CHECK:   i8* null,
// CHECK:   { {{.+}} }* @_PROTOCOL_INSTANCE_METHODS__TtP15objc_properties5Proto_,
// CHECK:   { {{.+}} }* @_PROTOCOL_CLASS_METHODS__TtP15objc_properties5Proto_,
// CHECK:   i8* null,
// CHECK:   i8* null,
// CHECK:   { {{.+}} }* @_PROTOCOL_PROPERTIES__TtP15objc_properties5Proto_,
// CHECK:   i32 96, i32 1,
// CHECK:   { {{.+}} }* @_PROTOCOL_METHOD_TYPES__TtP15objc_properties5Proto_,
// CHECK:   i8* null,
// CHECK-NEW:   { {{.+}} }* @_PROTOCOL_CLASS_PROPERTIES__TtP15objc_properties5Proto_
// CHECK-OLD:   i8* null
// CHECK: }, section "__DATA, __objc_const", align 8


// CHECK: [[PROTOCOLPROPERTY_NAME:@.+]] = private unnamed_addr constant [6 x i8] c"value\00"
// CHECK: [[PROTOCOLPROPERTY_ATTRS:@.+]] = private unnamed_addr constant [7 x i8] c"Tq,N,R\00"

// CHECK: [[PROTOCOLCLASSPROPERTY_NAME:@.+]] = private unnamed_addr constant [15 x i8] c"sharedInstance\00"
// CHECK: [[PROTOCOLCLASSPROPERTY_ATTRS:@.+]] = private unnamed_addr constant [7 x i8] c"T@,N,&\00"

// CHECK: @_PROTOCOL_PROPERTIES__TtP15objc_properties5Proto_ = private constant { {{.*}}] } {
// CHECK:   i32 16,
// CHECK:   i32 1,
// CHECK:   [1 x { i8*, i8* }] [{
// CHECK:     i8* getelementptr inbounds ([6 x i8], [6 x i8]* [[PROTOCOLPROPERTY_NAME]], i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([7 x i8], [7 x i8]* [[PROTOCOLPROPERTY_ATTRS]], i64 0, i64 0)
// CHECK:   }]
// CHECK: }, section "__DATA, __objc_const", align 8

// CHECK-NEW: @_PROTOCOL_CLASS_PROPERTIES__TtP15objc_properties5Proto_ = private constant { {{.*}}] } {
// CHECK-NEW:   i32 16,
// CHECK-NEW:   i32 1,
// CHECK-NEW:   [1 x { i8*, i8* }] [{
// CHECK-NEW:     i8* getelementptr inbounds ([15 x i8], [15 x i8]* [[PROTOCOLCLASSPROPERTY_NAME]], i64 0, i64 0),
// CHECK-NEW:     i8* getelementptr inbounds ([7 x i8], [7 x i8]* [[PROTOCOLCLASSPROPERTY_ATTRS]], i64 0, i64 0)
// CHECK-NEW:   }]
// CHECK-NEW: }, section "__DATA, __objc_const", align 8
