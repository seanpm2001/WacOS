// RUN: %target-swift-frontend -parse-as-library -emit-silgen %s | %FileCheck %s

// CHECK: sil private @globalinit_[[T:.*]]_func0 : $@convention(thin) () -> () {
// CHECK:   alloc_global @_Tv12lazy_globals1xSi
// CHECK:   [[XADDR:%.*]] = global_addr @_Tv12lazy_globals1xSi : $*Int
// CHECK:   store {{%.*}} to [[XADDR]] : $*Int

// CHECK: sil hidden [global_init] @_TF12lazy_globalsau1xSi : $@convention(thin) () -> Builtin.RawPointer {
// CHECK:   [[TOKEN_ADDR:%.*]] = global_addr @globalinit_[[T]]_token0 : $*Builtin.Word
// CHECK:   [[TOKEN_PTR:%.*]] = address_to_pointer [[TOKEN_ADDR]] : $*Builtin.Word to $Builtin.RawPointer
// CHECK:   [[INIT_FUNC:%.*]] = function_ref @globalinit_[[T]]_func0 : $@convention(thin) () -> ()
// CHECK:   builtin "once"([[TOKEN_PTR]] : $Builtin.RawPointer, [[INIT_FUNC]] : $@convention(thin) () -> ()) : $()
// CHECK:   [[GLOBAL_ADDR:%.*]] = global_addr @_Tv12lazy_globals1xSi : $*Int
// CHECK:   [[GLOBAL_PTR:%.*]] = address_to_pointer [[GLOBAL_ADDR]] : $*Int to $Builtin.RawPointer
// CHECK:   return [[GLOBAL_PTR]] : $Builtin.RawPointer
// CHECK: }
var x: Int = 0

// CHECK: sil private @globalinit_[[T:.*]]_func1 : $@convention(thin) () -> () {
// CHECK:   alloc_global @_TZvV12lazy_globals3Foo3fooSi
// CHECK:   [[XADDR:%.*]] = global_addr @_TZvV12lazy_globals3Foo3fooSi : $*Int
// CHECK:   store {{.*}} to [[XADDR]] : $*Int
// CHECK:   return

struct Foo {
// CHECK: sil hidden [global_init] @_TFV12lazy_globals3Fooau3fooSi : $@convention(thin) () -> Builtin.RawPointer {
// CHECK:   [[TOKEN_ADDR:%.*]] = global_addr @globalinit_[[T]]_token1 : $*Builtin.Word
// CHECK:   [[TOKEN_PTR:%.*]] = address_to_pointer [[TOKEN_ADDR]] : $*Builtin.Word to $Builtin.RawPointer
// CHECK:   [[INIT_FUNC:%.*]] = function_ref @globalinit_[[T]]_func1 : $@convention(thin) () -> ()
// CHECK:   builtin "once"([[TOKEN_PTR]] : $Builtin.RawPointer, [[INIT_FUNC]] : $@convention(thin) () -> ()) : $()
// CHECK:   [[GLOBAL_ADDR:%.*]] = global_addr @_TZvV12lazy_globals3Foo3fooSi : $*Int
// CHECK:   [[GLOBAL_PTR:%.*]] = address_to_pointer [[GLOBAL_ADDR]] : $*Int to $Builtin.RawPointer
// CHECK:   return [[GLOBAL_PTR]] : $Builtin.RawPointer
  static var foo: Int = 22

  static var computed: Int {
    return 33
  }

  static var initialized: Int = 57
}

// CHECK: sil private @globalinit_[[T:.*]]_func3 : $@convention(thin) () -> () {
// CHECK:   alloc_global @_TZvO12lazy_globals3Bar3barSi
// CHECK:   [[XADDR:%.*]] = global_addr @_TZvO12lazy_globals3Bar3barSi : $*Int
// CHECK:   store {{.*}} to [[XADDR]] : $*Int
// CHECK:   return

enum Bar {
// CHECK: sil hidden [global_init] @_TFO12lazy_globals3Barau3barSi : $@convention(thin) () -> Builtin.RawPointer {
// CHECK:   [[TOKEN_ADDR:%.*]] = global_addr @globalinit_[[T]]_token3 : $*Builtin.Word
// CHECK:   [[TOKEN_PTR:%.*]] = address_to_pointer [[TOKEN_ADDR]] : $*Builtin.Word to $Builtin.RawPointer
// CHECK:   [[INIT_FUNC:%.*]] = function_ref @globalinit_[[T]]_func3 : $@convention(thin) () -> ()
// CHECK:   builtin "once"([[TOKEN_PTR]] : $Builtin.RawPointer, [[INIT_FUNC]] : $@convention(thin) () -> ()) : $()
// CHECK:   [[GLOBAL_ADDR:%.*]] = global_addr @_TZvO12lazy_globals3Bar3barSi : $*Int
// CHECK:   [[GLOBAL_PTR:%.*]] = address_to_pointer [[GLOBAL_ADDR]] : $*Int to $Builtin.RawPointer
// CHECK:   return [[GLOBAL_PTR]] : $Builtin.RawPointer
  static var bar: Int = 33
}

// We only emit one initializer function per pattern binding, which initializes
// all of the bound variables.

func f() -> (Int, Int) { return (1, 2) }

// CHECK: sil private @globalinit_[[T]]_func4 : $@convention(thin) () -> () {
// CHECK:   function_ref @_TF12lazy_globals1fFT_TSiSi_ : $@convention(thin) () -> (Int, Int)
// CHECK: sil hidden [global_init] @_TF12lazy_globalsau2a1Si : $@convention(thin) () -> Builtin.RawPointer
// CHECK:   function_ref @globalinit_[[T]]_func4 : $@convention(thin) () -> ()
// CHECK:   global_addr @_Tv12lazy_globals2a1Si : $*Int
// CHECK: sil hidden [global_init] @_TF12lazy_globalsau2b1Si : $@convention(thin) () -> Builtin.RawPointer {
// CHECK:   function_ref @globalinit_[[T]]_func4 : $@convention(thin) () -> ()
// CHECK:   global_addr @_Tv12lazy_globals2b1Si : $*Int
var (a1, b1) = f()

var computed: Int {
  return 44
}

var initialized: Int = 57

