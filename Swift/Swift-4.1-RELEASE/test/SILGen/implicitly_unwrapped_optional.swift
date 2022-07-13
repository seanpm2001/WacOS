// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership %s | %FileCheck %s

func foo(f f: (() -> ())!) {
  var f: (() -> ())! = f
  f?()
}
// CHECK: sil hidden @{{.*}}foo{{.*}} : $@convention(thin) (@owned Optional<@callee_guaranteed () -> ()>) -> () {
// CHECK: bb0([[T0:%.*]] : @owned $Optional<@callee_guaranteed () -> ()>):
// CHECK:   [[F:%.*]] = alloc_box ${ var Optional<@callee_guaranteed () -> ()> }
// CHECK:   [[PF:%.*]] = project_box [[F]]
// CHECK:   [[BORROWED_T0:%.*]] = begin_borrow [[T0]]
// CHECK:   [[T0_COPY:%.*]] = copy_value [[BORROWED_T0]]
// CHECK:   store [[T0_COPY]] to [init] [[PF]]
// CHECK:   end_borrow [[BORROWED_T0]] from [[T0]]
// CHECK:   [[READ:%.*]] = begin_access [read] [unknown] [[PF]] : $*Optional<@callee_guaranteed () -> ()>
// CHECK:   [[T1:%.*]] = select_enum_addr [[READ]]
// CHECK:   cond_br [[T1]], bb2, bb1
//   If it does, project and load the value out of the implicitly unwrapped
//   optional...
// CHECK:    bb2:
// CHECK-NEXT: [[FN0_ADDR:%.*]] = unchecked_take_enum_data_addr [[READ]]
// CHECK-NEXT: [[FN0:%.*]] = load [copy] [[FN0_ADDR]]
//   .... then call it
// CHECK:   [[B:%.*]] = begin_borrow [[FN0]]
// CHECK:   apply [[B]]() : $@callee_guaranteed () -> ()
// CHECK:   end_borrow [[B]]
// CHECK:   br bb3
// CHECK: bb3(
// CHECK:   destroy_value [[F]]
// CHECK:   destroy_value [[T0]]
// CHECK:   return
// CHECK: bb4:
// CHECK:   enum $Optional<()>, #Optional.none!enumelt
// CHECK:   br bb3
//   The rest of this is tested in optional.swift
// } // end sil function '{{.*}}foo{{.*}}'

func wrap<T>(x x: T) -> T! { return x }

// CHECK-LABEL: sil hidden @_T029implicitly_unwrapped_optional16wrap_then_unwrap{{[_0-9a-zA-Z]*}}F
func wrap_then_unwrap<T>(x x: T) -> T {
  // CHECK:   switch_enum_addr {{%.*}}, case #Optional.some!enumelt.1: [[OK:bb[0-9]+]], case #Optional.none!enumelt: [[FAIL:bb[0-9]+]]
  // CHECK: [[FAIL]]:
  // CHECK:   unreachable
  // CHECK: [[OK]]:
  return wrap(x: x)!
}

// CHECK-LABEL: sil hidden @_T029implicitly_unwrapped_optional10tuple_bindSSSgSQySi_SStG1x_tF : $@convention(thin) (@owned Optional<(Int, String)>) -> @owned Optional<String> {
func tuple_bind(x x: (Int, String)!) -> String? {
  return x?.1
  // CHECK:   cond_br {{%.*}}, [[NONNULL:bb[0-9]+]], [[NULL:bb[0-9]+]]
  // CHECK: [[NONNULL]]:
  // CHECK:   [[STRING:%.*]] = tuple_extract {{%.*}} : $(Int, String), 1
  // CHECK-NOT: destroy_value [[STRING]]
}

// CHECK-LABEL: sil hidden @_T029implicitly_unwrapped_optional011tuple_bind_a1_B0SSSQySi_SStG1x_tF
func tuple_bind_implicitly_unwrapped(x x: (Int, String)!) -> String {
  return x.1
}

func return_any() -> AnyObject! { return nil }
func bind_any() {
  let object : AnyObject? = return_any()
}

// CHECK-LABEL: sil hidden @_T029implicitly_unwrapped_optional6sr3758yyF
func sr3758() {
  // Verify that there are no additional reabstractions introduced.
  // CHECK: [[CLOSURE:%.+]] = function_ref @_T029implicitly_unwrapped_optional6sr3758yyFySQyypGcfU_ : $@convention(thin) (@in Optional<Any>) -> ()
  // CHECK: [[F:%.+]] = thin_to_thick_function [[CLOSURE]] : $@convention(thin) (@in Optional<Any>) -> () to $@callee_guaranteed (@in Optional<Any>) -> ()
  // CHECK: [[BORROWED_F:%.*]] = begin_borrow [[F]]
  // CHECK: [[CALLEE:%.+]] = copy_value [[BORROWED_F]] : $@callee_guaranteed (@in Optional<Any>) -> ()
  // CHECK: [[BORROWED_CALLEE:%.*]] = begin_borrow [[CALLEE]]
  // CHECK: = apply [[BORROWED_CALLEE]]({{%.+}}) : $@callee_guaranteed (@in Optional<Any>) -> ()
  // CHECK: end_borrow [[BORROWED_CALLEE]]
  // destroy_value [[CALLEE]]
  // CHECK: end_borrow [[BORROWED_F]] from [[F]]
  // CHECK: destroy_value [[F]]
  let f: ((Any?) -> Void) = { (arg: Any!) in }
  f(nil)
} // CHECK: end sil function '_T029implicitly_unwrapped_optional6sr3758yyF'
