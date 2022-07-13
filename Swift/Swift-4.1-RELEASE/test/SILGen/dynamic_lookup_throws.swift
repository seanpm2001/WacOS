// RUN: %empty-directory(%t)
// RUN: %build-clang-importer-objc-overlays

// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk-nosource -I %t) -emit-silgen -parse-as-library %s | %FileCheck %s

// REQUIRES: objc_interop

import Foundation

class Blub : NSObject {
   func blub() throws {}
}

// CHECK-LABEL: sil hidden @_T021dynamic_lookup_throws8testBlubyyXl1a_tKF : $@convention(thin) (@owned AnyObject) -> @error Error
// CHECK: bb0([[ARG:%.*]] : $AnyObject):
func testBlub(a: AnyObject) throws {
  // CHECK:   [[BORROWED_ARG:%.*]] = begin_borrow [[ARG]]
  // CHECK:   [[ANYOBJECT_REF:%.*]] = open_existential_ref [[BORROWED_ARG]] : $AnyObject to $@opened("[[OPENED:.*]]") AnyObject
  // CHECK:   [[ANYOBJECT_REF_COPY:%.*]] = copy_value [[ANYOBJECT_REF]]
  // CHECK:   objc_method [[ANYOBJECT_REF_COPY]] : $@opened("[[OPENED]]") AnyObject, #Blub.blub!1.foreign : (Blub) -> () throws -> (), $@convention(objc_method) (Optional<AutoreleasingUnsafeMutablePointer<Optional<NSError>>>, @opened("[[OPENED]]") AnyObject) -> ObjCBool
  // CHECK:   cond_br {{%.*}}, bb1, bb2

  // CHECK: bb1
  // CHECK:   return

  // CHECK: bb2
  // CHECK:   function_ref @_T010Foundation22_convertNSErrorToErrors0E0_pSo0C0CSgF
  // CHECK:   throw {{%.*}} : $Error
  try a.blub()
}
