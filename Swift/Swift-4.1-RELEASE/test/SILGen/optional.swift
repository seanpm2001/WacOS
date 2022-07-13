// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership %s | %FileCheck %s

func testCall(_ f: (() -> ())?) {
  f?()
}
// CHECK:    sil hidden @{{.*}}testCall{{.*}}
// CHECK:    bb0([[T0:%.*]] : @owned $Optional<@callee_guaranteed () -> ()>):
// CHECK:      [[BORROWED_T0:%.*]] = begin_borrow [[T0]]
// CHECK:      [[T0_COPY:%.*]] = copy_value [[BORROWED_T0]]
// CHECK:      [[T1:%.*]] = select_enum [[T0_COPY]]
// CHECK-NEXT: cond_br [[T1]], [[SOME:bb[0-9]+]], [[NONE:bb[0-9]+]]

// CHECK: [[NONE]]:
// CHECK:   end_borrow [[BORROWED_T0]] from [[T0]]
// CHECK:   br [[NOTHING_BLOCK_EXIT:bb[0-9]+]]

//   If it does, project and load the value out of the implicitly unwrapped
//   optional...

// CHECK: [[SOME]]:
// CHECK-NEXT: [[FN0:%.*]] = unchecked_enum_data [[T0_COPY]] : $Optional<@callee_guaranteed () -> ()>, #Optional.some!enumelt.1
//   .... then call it
// CHECK-NEXT: [[B:%.*]] = begin_borrow [[FN0]]
// CHECK-NEXT: apply [[B]]()
// CHECK:      destroy_value [[FN0]]
// CHECK:      end_borrow [[BORROWED_T0]] from [[T0]]
// CHECK:      br [[EXIT:bb[0-9]+]](

//   (first nothing block)
// CHECK:    [[NOTHING_BLOCK_EXIT]]:
// CHECK-NEXT: enum $Optional<()>, #Optional.none!enumelt
// CHECK-NEXT: br [[EXIT]]
// CHECK: } // end sil function '_T08optional8testCallyyycSgF'

func testAddrOnlyCallResult<T>(_ f: (() -> T)?) {
  var f = f
  var x = f?()
}
// CHECK-LABEL: sil hidden @{{.*}}testAddrOnlyCallResult{{.*}} : $@convention(thin) <T> (@owned Optional<@callee_guaranteed () -> @out T>) -> ()
// CHECK:    bb0([[T0:%.*]] : @owned $Optional<@callee_guaranteed () -> @out T>):
// CHECK: [[F:%.*]] = alloc_box $<τ_0_0> { var Optional<@callee_guaranteed () -> @out τ_0_0> } <T>, var, name "f"
// CHECK-NEXT: [[PBF:%.*]] = project_box [[F]]
// CHECK: [[BORROWED_T0:%.*]] = begin_borrow [[T0]]
// CHECK: [[T0_COPY:%.*]] = copy_value [[BORROWED_T0]]
// CHECK: store [[T0_COPY]] to [init] [[PBF]]
// CHECK: end_borrow [[BORROWED_T0]] from [[T0]]
// CHECK-NEXT: [[X:%.*]] = alloc_box $<τ_0_0> { var Optional<τ_0_0> } <T>, var, name "x"
// CHECK-NEXT: [[PBX:%.*]] = project_box [[X]]
// CHECK-NEXT: [[TEMP:%.*]] = init_enum_data_addr [[PBX]]
// CHECK-NEXT: [[READ:%.*]] = begin_access [read] [unknown] [[PBF]]
//   Check whether 'f' holds a value.
// CHECK:      [[T1:%.*]] = select_enum_addr [[READ]]
// CHECK-NEXT: cond_br [[T1]], bb2, bb1
//   If so, pull out the value...
// CHECK:    bb2:
// CHECK-NEXT: [[T1:%.*]] = unchecked_take_enum_data_addr [[READ]]
// CHECK-NEXT: [[T0:%.*]] = load [copy] [[T1]]
// CHECK-NEXT: end_access [[READ]]
//   ...evaluate the rest of the suffix...
// CHECK:     [[B:%.*]] = begin_borrow [[T0]]
// CHECK-NEXT: apply [[B]]([[TEMP]])
//   ...and coerce to T?
// CHECK: inject_enum_addr [[PBX]] {{.*}}some
// CHECK:     destroy_value [[T0]]
// CHECK-NEXT: br bb3
//   Continuation block.
// CHECK:    bb3
// CHECK-NEXT: destroy_value [[X]]
// CHECK-NEXT: destroy_value [[F]]
// CHECK-NEXT: destroy_value %0
// CHECK-NEXT: [[T0:%.*]] = tuple ()
// CHECK-NEXT: return [[T0]] : $()

//   Nothing block.
// CHECK:    bb4:
// CHECK-NEXT: inject_enum_addr [[PBX]] {{.*}}none
// CHECK-NEXT: br bb3


// <rdar://problem/15180622>

func wrap<T>(_ x: T) -> T? { return x }

// CHECK-LABEL: sil hidden @_T08optional16wrap_then_unwrap{{[_0-9a-zA-Z]*}}F
func wrap_then_unwrap<T>(_ x: T) -> T {
  // CHECK:   switch_enum_addr {{.*}}, case #Optional.some!enumelt.1: [[OK:bb[0-9]+]], case #Optional.none!enumelt: [[FAIL:bb[0-9]+]]
  // CHECK: [[FAIL]]:
  // CHECK:   unreachable
  // CHECK: [[OK]]:
  // CHECK:   unchecked_take_enum_data_addr
  return wrap(x)!
}

// CHECK-LABEL: sil hidden @_T08optional10tuple_bind{{[_0-9a-zA-Z]*}}F
func tuple_bind(_ x: (Int, String)?) -> String? {
  return x?.1
  // CHECK:   cond_br {{%.*}}, [[NONNULL:bb[0-9]+]], [[NULL:bb[0-9]+]]
  // CHECK: [[NONNULL]]:
  // CHECK:   [[STRING:%.*]] = tuple_extract {{%.*}} : $(Int, String), 1
  // CHECK-NOT: destroy_value [[STRING]]
}

// rdar://21883752 - We were crashing on this function because the deallocation happened
// out of scope.
// CHECK-LABEL: sil hidden @_T08optional16crash_on_deallocys10DictionaryVySiSaySiGGFfA_
func crash_on_dealloc(_ dict : [Int : [Int]] = [:]) {
  var dict = dict
  dict[1]?.append(2)
}
