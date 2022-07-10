// RUN: %target-swift-frontend -sdk %S/Inputs -I %S/Inputs -enable-source-import %s -emit-silgen | %FileCheck %s

// REQUIRES: objc_interop

import gizmo

class X { }
func onDestruct() { }

@requires_stored_property_inits
class SwiftGizmo : Gizmo {
  var x = X()

  // CHECK-LABEL: sil hidden @_TFC12objc_dealloc10SwiftGizmoc
  // CHECK: bb0([[SELF_PARAM:%[0-9]+]] : $SwiftGizmo):
  override init() {
    // CHECK:   [[SELF_UNINIT:%[0-9]+]] = mark_uninitialized [derivedselfonly]
    // CHECK-NOT: ref_element_addr
    // CHECK:   upcast
    // CHECK-NEXT:   super_method
    // CHECK:   return
    super.init()
  }

  // CHECK-LABEL: sil hidden @_TFC12objc_dealloc10SwiftGizmoD : $@convention(method) (@owned SwiftGizmo) -> ()
  deinit {
    // CHECK: bb0([[SELF:%[0-9]+]] : $SwiftGizmo):
    // Call onDestruct()
    // CHECK:   [[ONDESTRUCT_REF:%[0-9]+]] = function_ref @_TF12objc_dealloc10onDestructFT_T_ : $@convention(thin) () -> ()
    // CHECK:   [[ONDESTRUCT_RESULT:%[0-9]+]] = apply [[ONDESTRUCT_REF]]() : $@convention(thin) () -> ()
    onDestruct()

    // Note: don't destroy instance variables
    // CHECK-NOT: ref_element_addr

    // Call super -dealloc.
    // CHECK:   [[SUPER_DEALLOC:%[0-9]+]] = super_method [[SELF]] : $SwiftGizmo, #Gizmo.deinit!deallocator.foreign : (Gizmo) -> () -> () , $@convention(objc_method) (Gizmo) -> ()
    // CHECK:   [[SUPER:%[0-9]+]] = upcast [[SELF]] : $SwiftGizmo to $Gizmo
    // CHECK:   [[SUPER_DEALLOC_RESULT:%[0-9]+]] = apply [[SUPER_DEALLOC]]([[SUPER]]) : $@convention(objc_method) (Gizmo) -> ()
    // CHECK:   [[RESULT:%[0-9]+]] = tuple ()
    // CHECK:   return [[RESULT]] : $()
  }

  // Objective-C deallocation deinit thunk (i.e., -dealloc).
  // CHECK-LABEL: sil hidden [thunk] @_TToFC12objc_dealloc10SwiftGizmoD : $@convention(objc_method) (SwiftGizmo) -> ()
  // CHECK: bb0([[SELF:%[0-9]+]] : $SwiftGizmo):
  // CHECK:   strong_retain

  // CHECK:   [[GIZMO_DTOR:%[0-9]+]] = function_ref @_TFC12objc_dealloc10SwiftGizmoD : $@convention(method) (@owned SwiftGizmo) -> ()
  // CHECK:   [[RESULT:%[0-9]+]] = apply [[GIZMO_DTOR]]([[SELF]]) : $@convention(method) (@owned SwiftGizmo) -> ()
  // CHECK:   return [[RESULT]] : $()

  // Objective-C IVar initializer (i.e., -.cxx_construct)
  // CHECK-LABEL: sil hidden @_TToFC12objc_dealloc10SwiftGizmoe : $@convention(objc_method) (@owned SwiftGizmo) -> @owned SwiftGizmo
  // CHECK: bb0([[SELF_PARAM:%[0-9]+]] : $SwiftGizmo):
  // CHECK-NEXT:   debug_value [[SELF_PARAM]] : $SwiftGizmo, let, name "self"
  // CHECK-NEXT:   [[SELF:%[0-9]+]] = mark_uninitialized [rootself] [[SELF_PARAM]] : $SwiftGizmo
  // CHECK:        [[XCTOR:%[0-9]+]] = function_ref @_TFC12objc_dealloc1XC
  // CHECK-NEXT:   [[XMETA:%[0-9]+]] = metatype $@thick X.Type
  // CHECK-NEXT:   [[XOBJ:%[0-9]+]] = apply [[XCTOR]]([[XMETA]]) : $@convention(method) (@thick X.Type) -> @owned X
  // CHECK-NEXT:   [[X:%[0-9]+]] = ref_element_addr [[SELF]] : $SwiftGizmo, #SwiftGizmo.x
  // CHECK-NEXT:   assign [[XOBJ]] to [[X]] : $*X
  // CHECK-NEXT:   return [[SELF]] : $SwiftGizmo

  // Objective-C IVar destroyer (i.e., -.cxx_destruct)
  // CHECK-LABEL: sil hidden @_TToFC12objc_dealloc10SwiftGizmoE : $@convention(objc_method) (SwiftGizmo) -> ()
  // CHECK:      bb0([[SELF:%[0-9]+]] : $SwiftGizmo):
  // CHECK-NEXT:  debug_value [[SELF]] : $SwiftGizmo, let, name "self"
  // CHECK-NEXT: [[X:%[0-9]+]] = ref_element_addr [[SELF]] : $SwiftGizmo, #SwiftGizmo.x
  // CHECK-NEXT: destroy_addr [[X]] : $*X
  // CHECK-NEXT: [[RESULT:%[0-9]+]] = tuple ()
  // CHECK-NEXT: return [[RESULT]] : $()
}

// CHECK-NOT: sil hidden [thunk] @_TToFCSo11SwiftGizmo2E : $@convention(objc_method) (SwiftGizmo2) -> ()
class SwiftGizmo2 : Gizmo {
}
