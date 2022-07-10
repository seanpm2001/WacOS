// RUN: %target-swift-frontend -sdk %S/Inputs -I %S/Inputs -enable-source-import %s -emit-silgen -disable-objc-attr-requires-foundation-module | %FileCheck %s

// REQUIRES: objc_interop

import gizmo

@objc class ObjCClass {}

class A {
  // CHECK-LABEL: sil hidden @_TFC14objc_metatypes1A3foo

  // CHECK-LABEL: sil hidden [thunk] @_TToFC14objc_metatypes1A3foo
  dynamic func foo(_ m: ObjCClass.Type) -> ObjCClass.Type {
    // CHECK: bb0([[M:%[0-9]+]] : $@objc_metatype ObjCClass.Type, [[SELF:%[0-9]+]] : $A):
    // CHECK:   strong_retain [[SELF]] : $A
    // CHECK:   [[M_AS_THICK:%[0-9]+]] = objc_to_thick_metatype [[M]] : $@objc_metatype ObjCClass.Type to $@thick ObjCClass.Type

    // CHECK:   [[NATIVE_FOO:%[0-9]+]] = function_ref @_TFC14objc_metatypes1A3foo
    // CHECK:   [[NATIVE_RESULT:%[0-9]+]] = apply [[NATIVE_FOO]]([[M_AS_THICK]], [[SELF]]) : $@convention(method) (@thick ObjCClass.Type, @guaranteed A) -> @thick ObjCClass.Type
    // CHECK:   [[OBJC_RESULT:%[0-9]+]] = thick_to_objc_metatype [[NATIVE_RESULT]] : $@thick ObjCClass.Type to $@objc_metatype ObjCClass.Type
    // CHECK:   return [[OBJC_RESULT]] : $@objc_metatype ObjCClass.Type
    return m
  }

  // CHECK-LABEL: sil hidden @_TZFC14objc_metatypes1A3bar

  // CHECK-LABEL: sil hidden [thunk] @_TToZFC14objc_metatypes1A3bar
  // CHECK: bb0([[SELF:%[0-9]+]] : $@objc_metatype A.Type):
  // CHECK-NEXT:   [[OBJC_SELF:%[0-9]+]] = objc_to_thick_metatype [[SELF]] : $@objc_metatype A.Type to $@thick A.Type
  // CHECK:   [[BAR:%[0-9]+]] = function_ref @_TZFC14objc_metatypes1A3bar
  // CHECK-NEXT:   [[RESULT:%[0-9]+]] = apply [[BAR]]([[OBJC_SELF]]) : $@convention(method) (@thick A.Type) -> ()
  // CHECK-NEXT:   return [[RESULT]] : $()
  dynamic class func bar() { }

  dynamic func takeGizmo(_ g: Gizmo.Type) { }

  // CHECK-LABEL: sil hidden @_TFC14objc_metatypes1A7callFoo
  func callFoo() {
    // Make sure we peephole Type/thick_to_objc_metatype.
    // CHECK-NOT: thick_to_objc_metatype
    // CHECK: metatype $@objc_metatype ObjCClass.Type
    foo(ObjCClass.self)
    // CHECK: return
  }
}
