// RUN: %target-swift-frontend -enable-sil-ownership -parse-stdlib -emit-silgen %s | %FileCheck %s

import Swift

final class D {}

// Make sure that we insert the borrow for a ref_element_addr lvalue in the
// proper place.
final class C {
  var d: D = D()
}

func useD(_ d: D) {}

// CHECK-LABEL: sil hidden @_T06borrow44lvalueBorrowShouldBeAtEndOfFormalAccessScope{{.*}} : $@convention(thin) () -> () {
// CHECK: bb0:
// CHECK:   [[BOX:%.*]] = alloc_box ${ var C }, var, name "c"
// CHECK:   [[PB_BOX:%.*]] = project_box [[BOX]]
// CHECK:   [[ACCESS:%.*]] = begin_access [read] [unknown] [[PB_BOX]] : $*C
// CHECK:   [[CLASS:%.*]] = load [copy] [[ACCESS]]
// CHECK:   [[BORROWED_CLASS:%.*]] = begin_borrow [[CLASS]]
// CHECK:   [[OFFSET:%.*]] = ref_element_addr [[BORROWED_CLASS]]
// CHECK:   [[ACCESS:%.*]] = begin_access [read] [dynamic] [[OFFSET]] : $*D
// CHECK:   [[LOADED_VALUE:%.*]] = load [copy] [[ACCESS]]
// CHECK:   end_borrow [[BORROWED_CLASS]] from [[CLASS]]
// CHECK:   [[FUNC:%.*]] = function_ref @_T06borrow4useD{{.*}} : $@convention(thin) (@owned D) -> ()
// CHECK:   apply [[FUNC]]([[LOADED_VALUE]])
// CHECK:   destroy_value [[CLASS]]
// CHECK:   destroy_value [[BOX]]
// CHECK: } // end sil function '_T06borrow44lvalueBorrowShouldBeAtEndOfFormalAccessScope{{.*}}'
func lvalueBorrowShouldBeAtEndOfFormalAccessScope() {
  var c = C()
  useD(c.d)
}
