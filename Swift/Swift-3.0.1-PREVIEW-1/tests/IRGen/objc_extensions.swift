// RUN: rm -rf %t && mkdir %t
// RUN: %build-irgen-test-overlays
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) -disable-objc-attr-requires-foundation-module -emit-module %S/Inputs/objc_extension_base.swift -o %t
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) -primary-file %s -emit-ir | %FileCheck %s

// REQUIRES: CPU=x86_64
// REQUIRES: objc_interop

import Foundation
import gizmo
import objc_extension_base

// Check that metadata for nested enums added in extensions to imported classes
// gets emitted concretely.
// CHECK: @_TWPOE15objc_extensionsCSo8NSObjectP33_1F05E59585E0BB585FCA206FBFF1A92D8SomeEnums9EquatableS_ =

// CHECK: [[CATEGORY_NAME:@.*]] = private unnamed_addr constant [16 x i8] c"objc_extensions\00"
// CHECK: [[METHOD_TYPE:@.*]] = private unnamed_addr constant [8 x i8] c"v16@0:8\00"

// CHECK-LABEL: @"_CATEGORY_PROTOCOLS_Gizmo_$_objc_extensions" = private constant
// CHECK:   i64 1,
// CHECK:   @_PROTOCOL__TtP15objc_extensions11NewProtocol_

// CHECK-LABEL: @"_CATEGORY_Gizmo_$_objc_extensions" = private constant
// CHECK:   i8* getelementptr inbounds ([16 x i8], [16 x i8]* [[CATEGORY_NAME]], i64 0, i64 0),
// CHECK:   %objc_class* @"OBJC_CLASS_$_Gizmo",
// CHECK:   @"_CATEGORY_INSTANCE_METHODS_Gizmo_$_objc_extensions",
// CHECK:   @"_CATEGORY_CLASS_METHODS_Gizmo_$_objc_extensions",
// CHECK:   @"_CATEGORY_PROTOCOLS_Gizmo_$_objc_extensions",
// CHECK:   i8* null
// CHECK: }, section "__DATA, __objc_const", align 8

@objc protocol NewProtocol {
  func brandNewInstanceMethod()
}

extension NSObject {
  func someMethod() -> String { return "Hello" }
}

extension Gizmo: NewProtocol {
  func brandNewInstanceMethod() {
  }

  class func brandNewClassMethod() {
  }

  // Overrides an instance method of NSObject
  override func someMethod() -> String {
    return super.someMethod()
  }

  // Overrides a class method of NSObject
  open override class func initialize() {
  }
}

/*
 * Make sure that two extensions of the same ObjC class in the same module can
 * coexist by having different category names.
 */

// CHECK: [[CATEGORY_NAME_1:@.*]] = private unnamed_addr constant [17 x i8] c"objc_extensions1\00"

// CHECK: @"_CATEGORY_Gizmo_$_objc_extensions1" = private constant
// CHECK:   i8* getelementptr inbounds ([17 x i8], [17 x i8]* [[CATEGORY_NAME_1]], i64 0, i64 0),
// CHECK:   %objc_class* @"OBJC_CLASS_$_Gizmo",
// CHECK:   {{.*}} @"_CATEGORY_INSTANCE_METHODS_Gizmo_$_objc_extensions1",
// CHECK:   {{.*}} @"_CATEGORY_CLASS_METHODS_Gizmo_$_objc_extensions1",
// CHECK:   i8* null,
// CHECK:   i8* null
// CHECK: }, section "__DATA, __objc_const", align 8

extension Gizmo {
  func brandSpankingNewInstanceMethod() {
  }

  class func brandSpankingNewClassMethod() {
  }
}

/*
 * Check that extensions of Swift subclasses of ObjC objects get categories.
 */

class Hoozit : NSObject {
}

// CHECK-LABEL: @"_CATEGORY_INSTANCE_METHODS__TtC15objc_extensions6Hoozit_$_objc_extensions" = private constant
// CHECK:   i32 24,
// CHECK:   i32 1,
// CHECK:   [1 x { i8*, i8*, i8* }] [{ i8*, i8*, i8* } {
// CHECK:     i8* getelementptr inbounds ([8 x i8], [8 x i8]* @"\01L_selector_data(blibble)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([8 x i8], [8 x i8]* [[STR:@.*]], i64 0, i64 0),
// CHECK:     i8* bitcast (void ([[OPAQUE:%.*]]*, i8*)* @_TToFC15objc_extensions6Hoozit7blibblefT_T_ to i8*)
// CHECK:   }]
// CHECK: }, section "__DATA, __objc_const", align 8

// CHECK-LABEL: @"_CATEGORY_CLASS_METHODS__TtC15objc_extensions6Hoozit_$_objc_extensions" = private constant
// CHECK:   i32 24,
// CHECK:   i32 1,
// CHECK:   [1 x { i8*, i8*, i8* }] [{ i8*, i8*, i8* } {
// CHECK:     i8* getelementptr inbounds ([8 x i8], [8 x i8]* @"\01L_selector_data(blobble)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([8 x i8], [8 x i8]* [[STR]], i64 0, i64 0),
// CHECK:     i8* bitcast (void (i8*, i8*)* @_TToZFC15objc_extensions6Hoozit7blobblefT_T_ to i8*)
// CHECK:   }]
// CHECK: }, section "__DATA, __objc_const", align 8

// CHECK-LABEL: @"_CATEGORY__TtC15objc_extensions6Hoozit_$_objc_extensions" = private constant
// CHECK:   i8* getelementptr inbounds ([16 x i8], [16 x i8]* [[CATEGORY_NAME]], i64 0, i64 0),
// CHECK:   %swift.type* {{.*}} @_TMfC15objc_extensions6Hoozit,
// CHECK:   {{.*}} @"_CATEGORY_INSTANCE_METHODS__TtC15objc_extensions6Hoozit_$_objc_extensions",
// CHECK:   {{.*}} @"_CATEGORY_CLASS_METHODS__TtC15objc_extensions6Hoozit_$_objc_extensions",
// CHECK:   i8* null,
// CHECK:   i8* null
// CHECK: }, section "__DATA, __objc_const", align 8

extension Hoozit {
  func blibble() { }
  class func blobble() { }
}

class SwiftOnly { }

// CHECK-LABEL: @"_CATEGORY_INSTANCE_METHODS__TtC15objc_extensions9SwiftOnly_$_objc_extensions" = private constant
// CHECK:   i32 24,
// CHECK:   i32 1,
// CHECK:   [1 x { i8*, i8*, i8* }] [{ i8*, i8*, i8* } {
// CHECK:     i8* getelementptr inbounds ([7 x i8], [7 x i8]* @"\01L_selector_data(wibble)", i64 0, i64 0),
// CHECK:     i8* getelementptr inbounds ([8 x i8], [8 x i8]* [[STR]], i64 0, i64 0),
// CHECK:     i8* bitcast (void (i8*, i8*)* @_TToFC15objc_extensions9SwiftOnly6wibblefT_T_ to i8*)
// CHECK:   }] }, section "__DATA, __objc_const", align 8
extension SwiftOnly {
  @objc func wibble() { }
}

class Wotsit: Hoozit {}

extension Hoozit {
  @objc func overriddenByExtensionInSubclass() {}
}

extension Wotsit {
  @objc override func overriddenByExtensionInSubclass() {}
}

extension NSObject {
  private enum SomeEnum { case X }
}

/*
 * Make sure that @NSManaged causes a category to be generated.
 */
class NSDogcow : NSObject {}

// CHECK: [[NAME:@.*]] = private unnamed_addr constant [5 x i8] c"woof\00"
// CHECK: [[ATTR:@.*]] = private unnamed_addr constant [7 x i8] c"Tq,N,D\00"
// CHECK: @"_CATEGORY_PROPERTIES__TtC15objc_extensions8NSDogcow_$_objc_extensions" = private constant {{.*}} [[NAME]], {{.*}} [[ATTR]], {{.*}}, section "__DATA, __objc_const", align 8
extension NSDogcow {
  @NSManaged var woof: Int
}

class  SwiftSubGizmo : SwiftBaseGizmo {

  // Don't crash on this call. Emit an objC method call to super.
  //
  // CHECK-LABEL: define {{.*}} @_TFC15objc_extensions13SwiftSubGizmo4frobfT_T_
  // CHECK: _TMaC15objc_extensions13SwiftSubGizmo
  // CHECK: objc_msgSendSuper2
  // CHECK: ret
  public override func frob() {
    super.frob()
  }
}
