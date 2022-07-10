// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -emit-silgen %s | %FileCheck %s

struct Foo {
  mutating           // used to test writeback.
  func foo() {}

  subscript(x: Int) -> Foo {
    get {
      return Foo()
    }
    set {}
  }
}

var x: Foo {
  get {
    return Foo()
  }
  set {}
}

var y: Foo {
  get {
    return Foo()
  }
  set {}
}

var z: Foo {
  get {
    return Foo()
  }
  set {}
}

var readonly: Foo {
  get {
    return Foo()
  }
}

func bar(x x: inout Foo) {}

// Writeback to value type 'self' argument
x.foo()
// CHECK: [[FOO:%.*]] = function_ref @_TFV9writeback3Foo3foo{{.*}} : $@convention(method) (@inout Foo) -> ()
// CHECK: [[X_TEMP:%.*]] = alloc_stack $Foo
// CHECK: [[GET_X:%.*]] = function_ref @_TF9writebackg1xVS_3Foo : $@convention(thin) () -> Foo
// CHECK: [[X:%.*]] = apply [[GET_X]]() : $@convention(thin) () -> Foo
// CHECK: store [[X]] to [[X_TEMP]]
// CHECK: apply [[FOO]]([[X_TEMP]]) : $@convention(method) (@inout Foo) -> ()
// CHECK: [[X1:%.*]] = load [[X_TEMP]] : $*Foo
// CHECK: [[SET_X:%.*]] = function_ref @_TF9writebacks1xVS_3Foo : $@convention(thin) (Foo) -> ()
// CHECK: apply [[SET_X]]([[X1]]) : $@convention(thin) (Foo) -> ()
// CHECK: dealloc_stack [[X_TEMP]] : $*Foo

// Writeback to inout argument
bar(x: &x)
// CHECK: [[BAR:%.*]] = function_ref @_TF9writeback3barFT1xRVS_3Foo_T_ : $@convention(thin) (@inout Foo) -> ()
// CHECK: [[X_TEMP:%.*]] = alloc_stack $Foo
// CHECK: [[GET_X:%.*]] = function_ref @_TF9writebackg1xVS_3Foo : $@convention(thin) () -> Foo
// CHECK: [[X:%.*]] = apply [[GET_X]]() : $@convention(thin) () -> Foo
// CHECK: store [[X]] to [[X_TEMP]] : $*Foo
// CHECK: apply [[BAR]]([[X_TEMP]]) : $@convention(thin) (@inout Foo) -> ()
// CHECK: [[X1:%.*]] = load [[X_TEMP]] : $*Foo
// CHECK: [[SET_X:%.*]] = function_ref @_TF9writebacks1xVS_3Foo : $@convention(thin) (Foo) -> ()
// CHECK: apply [[SET_X]]([[X1]]) : $@convention(thin) (Foo) -> ()
// CHECK: dealloc_stack [[X_TEMP]] : $*Foo

func zang(x x: Foo) {}

// No writeback for pass-by-value argument
zang(x: x)
// CHECK:  function_ref @_TF9writeback4zangFT1xVS_3Foo_T_ : $@convention(thin) (Foo) -> ()
// CHECK-NOT: @_TF9writebacks1xVS_3Foo
zang(x: readonly)
// CHECK:  function_ref @_TF9writeback4zangFT1xVS_3Foo_T_ : $@convention(thin) (Foo) -> ()
// CHECK-NOT: @_TF9writebacks8readonlyVS_3Foo

func zung() -> Int { return 0 }

// Ensure that subscripts are only evaluated once.
bar(x: &x[zung()])
// CHECK: [[BAR:%.*]] = function_ref @_TF9writeback3barFT1xRVS_3Foo_T_ : $@convention(thin) (@inout Foo) -> ()
// CHECK: [[ZUNG:%.*]] = function_ref @_TF9writeback4zungFT_Si : $@convention(thin) () -> Int
// CHECK: [[INDEX:%.*]] = apply [[ZUNG]]() : $@convention(thin) () -> Int
// CHECK: [[GET_X:%.*]] = function_ref @_TF9writebackg1xVS_3Foo : $@convention(thin) () -> Foo
// CHECK: [[GET_SUBSCRIPT:%.*]] = function_ref @_TFV9writeback3Foog9subscript{{.*}} : $@convention(method) (Int, Foo) -> Foo
// CHECK: apply [[GET_SUBSCRIPT]]([[INDEX]], {{%.*}}) : $@convention(method) (Int, Foo) -> Foo
// CHECK: apply [[BAR]]({{%.*}}) : $@convention(thin) (@inout Foo) -> ()
// CHECK: [[SET_SUBSCRIPT:%.*]] = function_ref @_TFV9writeback3Foos9subscript{{.*}} : $@convention(method) (Foo, Int, @inout Foo) -> ()
// CHECK: apply [[SET_SUBSCRIPT]]({{%.*}}, [[INDEX]], {{%.*}}) : $@convention(method) (Foo, Int, @inout Foo) -> ()
// CHECK: function_ref @_TF9writebacks1xVS_3Foo : $@convention(thin) (Foo) -> ()

protocol Fungible {}
extension Foo : Fungible {}

var addressOnly: Fungible {
  get {
    return Foo()
  }
  set {}
}

func funge(x x: inout Fungible) {}

funge(x: &addressOnly)
// CHECK: [[FUNGE:%.*]] = function_ref @_TF9writeback5fungeFT1xRPS_8Fungible__T_ : $@convention(thin) (@inout Fungible) -> ()
// CHECK: [[TEMP:%.*]] = alloc_stack $Fungible
// CHECK: [[GET:%.*]] = function_ref @_TF9writebackg11addressOnlyPS_8Fungible_ : $@convention(thin) () -> @out Fungible
// CHECK: apply [[GET]]([[TEMP]]) : $@convention(thin) () -> @out Fungible
// CHECK: apply [[FUNGE]]([[TEMP]]) : $@convention(thin) (@inout Fungible) -> ()
// CHECK: [[SET:%.*]] = function_ref @_TF9writebacks11addressOnlyPS_8Fungible_ : $@convention(thin) (@in Fungible) -> ()
// CHECK: apply [[SET]]([[TEMP]]) : $@convention(thin) (@in Fungible) -> ()
// CHECK: dealloc_stack [[TEMP]] : $*Fungible

// Test that writeback occurs with generic properties.
// <rdar://problem/16525257> 

protocol Runcible {
  associatedtype Frob: Frobable

  var frob: Frob { get set }
}

protocol Frobable {
  associatedtype Anse
  
  var anse: Anse { get set }
}

// CHECK-LABEL: sil hidden @_TF9writeback12test_generic 
// CHECK:         witness_method $Runce, #Runcible.frob!materializeForSet.1
// CHECK:         witness_method $Runce.Frob, #Frobable.anse!setter.1
func test_generic<Runce: Runcible>(runce runce: inout Runce, anse: Runce.Frob.Anse) {
  runce.frob.anse = anse
}

// We should *not* write back when referencing decls or members as rvalues.
// <rdar://problem/16530235>
// CHECK-LABEL: sil hidden @_TF9writeback15loadAddressOnlyFT_PS_8Fungible_ : $@convention(thin) () -> @out Fungible {
func loadAddressOnly() -> Fungible {
  // CHECK:       function_ref writeback.addressOnly.getter
  // CHECK-NOT:   function_ref writeback.addressOnly.setter
  return addressOnly
}

// CHECK-LABEL: sil hidden @_TF9writeback10loadMember
// CHECK:         witness_method $Runce, #Runcible.frob!getter.1
// CHECK:         witness_method $Runce.Frob, #Frobable.anse!getter.1
// CHECK-NOT:     witness_method $Runce.Frob, #Frobable.anse!setter.1
// CHECK-NOT:     witness_method $Runce, #Runcible.frob!setter.1
func loadMember<Runce: Runcible>(runce runce: Runce) -> Runce.Frob.Anse {
  return runce.frob.anse
}

