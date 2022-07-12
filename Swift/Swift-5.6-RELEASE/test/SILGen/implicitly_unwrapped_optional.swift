
// RUN: %target-swift-emit-silgen -module-name implicitly_unwrapped_optional -disable-objc-attr-requires-foundation-module -enable-objc-interop %s | %FileCheck %s

func foo(f f: (() -> ())!) {
  var f: (() -> ())! = f
  f?()
}
// CHECK: sil hidden [ossa] @{{.*}}foo{{.*}} : $@convention(thin) (@guaranteed Optional<@callee_guaranteed () -> ()>) -> () {
// CHECK: bb0([[T0:%.*]] : @guaranteed $Optional<@callee_guaranteed () -> ()>):
// CHECK:   [[F:%.*]] = alloc_box ${ var Optional<@callee_guaranteed () -> ()> }
// CHECK:   [[PF:%.*]] = project_box [[F]]
// CHECK:   [[T0_COPY:%.*]] = copy_value [[T0]]
// CHECK:   store [[T0_COPY]] to [init] [[PF]]
// CHECK:   [[READ:%.*]] = begin_access [read] [unknown] [[PF]] : $*Optional<@callee_guaranteed () -> ()>
// CHECK:   [[HASVALUE:%.*]] = select_enum_addr [[READ]]
// CHECK:   cond_br [[HASVALUE]], bb1, bb3
//
//   If it does, project and load the value out of the implicitly unwrapped
//   optional...
// CHECK:    bb1:
// CHECK-NEXT: [[FN0_ADDR:%.*]] = unchecked_take_enum_data_addr [[READ]]
// CHECK-NEXT: [[FN0:%.*]] = load [copy] [[FN0_ADDR]]
//   .... then call it
// CHECK:   [[B:%.*]] = begin_borrow [[FN0]]
// CHECK:   apply [[B]]() : $@callee_guaranteed () -> ()
// CHECK:   end_borrow [[B]]
// CHECK:   br bb2
// CHECK: bb2(
// CHECK:   destroy_value [[F]]
// CHECK:   return
// CHECK: bb3:
// CHECK:   enum $Optional<()>, #Optional.none!enumelt
// CHECK:   br bb2
//   The rest of this is tested in optional.swift
// } // end sil function '{{.*}}foo{{.*}}'

func wrap<T>(x x: T) -> T! { return x }

// CHECK-LABEL: sil hidden [ossa] @$s29implicitly_unwrapped_optional16wrap_then_unwrap{{[_0-9a-zA-Z]*}}F
func wrap_then_unwrap<T>(x x: T) -> T {
  // CHECK:   switch_enum_addr {{%.*}}, case #Optional.some!enumelt: [[OK:bb[0-9]+]], case #Optional.none!enumelt: [[FAIL:bb[0-9]+]]
  // CHECK: [[FAIL]]:
  // CHECK:   unreachable
  // CHECK: [[OK]]:
  return wrap(x: x)!
}

// CHECK-LABEL: sil hidden [ossa] @$s29implicitly_unwrapped_optional10tuple_bind1xSSSgSi_SStSg_tF : $@convention(thin) (@guaranteed Optional<(Int, String)>) -> @owned Optional<String> {
func tuple_bind(x x: (Int, String)!) -> String? {
  return x?.1
  // CHECK:   switch_enum {{%.*}}, case #Optional.some!enumelt: [[NONNULL:bb[0-9]+]], case #Optional.none!enumelt: [[NULL:bb[0-9]+]]
  // CHECK: [[NONNULL]](
  // CHECK:   [[STRING:%.*]] = destructure_tuple {{%.*}} : $(Int, String)
  // CHECK-NOT: destroy_value [[STRING]]
}

// CHECK-LABEL: sil hidden [ossa] @$s29implicitly_unwrapped_optional011tuple_bind_a1_B01xSSSi_SStSg_tF
func tuple_bind_implicitly_unwrapped(x x: (Int, String)!) -> String {
  return x.1
}

func return_any() -> AnyObject! { return nil }
func bind_any() {
  let object : AnyObject? = return_any()
}

// CHECK-LABEL: sil hidden [ossa] @$s29implicitly_unwrapped_optional6sr3758yyF
func sr3758() {
  // Verify that there are no additional reabstractions introduced.
  // CHECK: [[CLOSURE:%.+]] = function_ref @$s29implicitly_unwrapped_optional6sr3758yyFyypSgcfU_ : $@convention(thin) (@in_guaranteed Optional<Any>) -> ()
  // CHECK: [[F:%.+]] = thin_to_thick_function [[CLOSURE]] : $@convention(thin) (@in_guaranteed Optional<Any>) -> () to $@callee_guaranteed (@in_guaranteed Optional<Any>) -> ()
  // CHECK: [[BORROWED_F:%.*]] = begin_borrow [[F]]
  // CHECK: = apply [[BORROWED_F]]({{%.+}}) : $@callee_guaranteed (@in_guaranteed Optional<Any>) -> ()
  // CHECK: end_borrow [[BORROWED_F]]
  let f: ((Any?) -> Void) = { (arg: Any!) in }
  f(nil)
} // CHECK: end sil function '$s29implicitly_unwrapped_optional6sr3758yyF'

// SR-10492: Make sure we can SILGen all of the below without crashing:
class SR_10492_C1 {
  init!() {}
}

class SR_10492_C2 {
  init(_ foo: SR_10492_C1) {}
}

@objc class C {
  @objc func foo() -> C! { nil }
}

struct S {
  var i: Int!
  func foo() -> Int! { nil }
  subscript() -> Int! { 0 }

  func testParend(_ anyObj: AnyObject) {
    let _: Int? = (foo)()
    let _: Int = (foo)()
    let _: Int? = foo.self()
    let _: Int = foo.self()
    let _: Int? = (self.foo.self)()
    let _: Int = (self.foo.self)()

    // Not really paren'd, but a previous version of the compiler modeled it
    // that way.
    let _ = SR_10492_C2(SR_10492_C1())

    let _: C = (anyObj.foo)!()
  }

  func testCurried() {
    let _: Int? = S.foo(self)()
    let _: Int = S.foo(self)()
    let _: Int? = (S.foo(self).self)()
    let _: Int = (S.foo(self).self)()
  }
}
