// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -emit-silgen %s | %FileCheck %s

func markUsed<T>(_ t: T) {}

func trap() -> Never {
  fatalError()
}


// CHECK-LABEL: sil @main
// CHECK: bb0({{%.*}} : $Int32, {{%.*}} : $UnsafeMutablePointer<Optional<UnsafeMutablePointer<Int8>>>):

// -- initialize x
// CHECK: alloc_global @_Tv8toplevel1xSi
// CHECK: [[X:%[0-9]+]] = global_addr @_Tv8toplevel1xSi : $*Int
// CHECK: integer_literal $Builtin.Int2048, 999
// CHECK: store {{.*}} to [[X]]

var x = 999

func print_x() {
  markUsed(x)
}

// -- assign x
// CHECK: integer_literal $Builtin.Int2048, 0
// CHECK: assign {{.*}} to [[X]]
// CHECK: [[PRINT_X:%[0-9]+]] = function_ref @_TF8toplevel7print_xFT_T_ :
// CHECK: apply [[PRINT_X]]


x = 0
print_x()

// <rdar://problem/19770775> Deferred initialization of let bindings rejected at top level in playground
// CHECK: alloc_global @_Tv8toplevel5countSi
// CHECK: [[COUNTADDR:%[0-9]+]] = global_addr @_Tv8toplevel5countSi : $*Int
// CHECK-NEXT: [[COUNTMUI:%[0-9]+]] = mark_uninitialized [var] [[COUNTADDR]] : $*Int
let count: Int
// CHECK: cond_br
if x == 5 {
  count = 0
  // CHECK: assign {{.*}} to [[COUNTMUI]]
  // CHECK: br [[MERGE:bb[0-9]+]]
} else {
  count = 10
  // CHECK: assign {{.*}} to [[COUNTMUI]]
  // CHECK: br [[MERGE]]
}

// CHECK: [[MERGE]]:
// CHECK: load [[COUNTMUI]]
markUsed(count)



var y : Int

func print_y() {
  markUsed(y)
}


// -- assign y
// CHECK: alloc_global @_Tv8toplevel1ySi
// CHECK: [[Y1:%[0-9]+]] = global_addr @_Tv8toplevel1ySi : $*Int
// CHECK: [[Y:%[0-9]+]] = mark_uninitialized [var] [[Y1]]
// CHECK: assign {{.*}} to [[Y]]
// CHECK: [[PRINT_Y:%[0-9]+]] = function_ref @_TF8toplevel7print_yFT_T_
y = 1
print_y()

// -- treat 'guard' vars as locals
// CHECK-LABEL: function_ref toplevel.A.__allocating_init
// CHECK: switch_enum {{%.+}} : $Optional<A>, case #Optional.some!enumelt.1: [[SOME_CASE:.+]], default
// CHECK: [[SOME_CASE]]([[VALUE:%.+]] : $A):
// CHECK: store [[VALUE]] to [[BOX:%.+]] : $*A
// CHECK-NOT: release
// CHECK: [[SINK:%.+]] = function_ref @_TF8toplevel8markUsedurFxT_
// CHECK-NOT: release
// CHECK: apply [[SINK]]<A>({{%.+}})
class A {}
guard var a = Optional(A()) else { trap() }
markUsed(a)


// CHECK: alloc_global @_Tv8toplevel21NotInitializedIntegerSi
// CHECK-NEXT: [[VARADDR:%[0-9]+]] = global_addr @_Tv8toplevel21NotInitializedIntegerSi
// CHECK-NEXT: [[VARMUI:%[0-9]+]] = mark_uninitialized [var] [[VARADDR]] : $*Int
// CHECK-NEXT: mark_function_escape [[VARMUI]] : $*Int


// <rdar://problem/21753262> Bug in DI when it comes to initialization of global "let" variables
let NotInitializedInteger : Int
func fooUsesUninitializedValue() {
  _ = NotInitializedInteger
}

fooUsesUninitializedValue()
NotInitializedInteger = 10
fooUsesUninitializedValue()




// CHECK: [[RET:%[0-9]+]] = struct $Int32
// CHECK: return [[RET]]




// CHECK-LABEL: sil hidden @_TF8toplevel7print_xFT_T_

// CHECK-LABEL: sil hidden @_TF8toplevel7print_yFT_T_

// CHECK: sil hidden @_TF8toplevel13testGlobalCSEFT_Si
// CHECK-NOT: global_addr
// CHECK: %0 = global_addr @_Tv8toplevel1xSi : $*Int
// CHECK-NOT: global_addr
// CHECK: return
func testGlobalCSE() -> Int {
  // We should only emit one global_addr in this function.
  return x + x
}
