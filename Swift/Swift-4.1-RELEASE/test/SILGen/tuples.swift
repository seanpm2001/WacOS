// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership %s | %FileCheck %s
class C {}

enum Foo {
  case X(C, Int)
}

// <rdar://problem/16020428>
// CHECK-LABEL: sil hidden @_T06tuples8matchFooyAA0C0O1x_tF
func matchFoo(x x: Foo) {
  switch x {
  case .X(let x):
    ()
  }
}

protocol P { func foo() }
struct A : P { func foo() {} }

func make_int() -> Int { return 0 }
func make_p() -> P { return A() }
func make_xy() -> (x: Int, y: P) { return (make_int(), make_p()) }

// CHECK-LABEL: sil hidden @_T06tuples17testShuffleOpaqueyyF
func testShuffleOpaque() {
  // CHECK: [[X:%.*]] = alloc_box ${ var P }
  // CHECK-NEXT: [[PBX:%.*]] = project_box [[X]]
  // CHECK: [[Y:%.*]] = alloc_box ${ var Int }
  // CHECK-NEXT: [[PBY:%.*]] = project_box [[Y]]

  // CHECK:      [[T0:%.*]] = function_ref @_T06tuples7make_xySi1x_AA1P_p1ytyF
  // CHECK-NEXT: [[T1:%.*]] = apply [[T0]]([[PBX]])
  // CHECK-NEXT: store [[T1]] to [trivial] [[PBY]]
  var (x,y) : (y:P, x:Int) = make_xy()

  // CHECK-NEXT: [[PAIR:%.*]] = alloc_box ${ var (y: P, x: Int) }
  // CHECK-NEXT: [[PBPAIR:%.*]] = project_box [[PAIR]]
  // CHECK-NEXT: [[PAIR_0:%.*]] = tuple_element_addr [[PBPAIR]] : $*(y: P, x: Int), 0
  // CHECK-NEXT: [[PAIR_1:%.*]] = tuple_element_addr [[PBPAIR]] : $*(y: P, x: Int), 1
  // CHECK-NEXT: // function_ref
  // CHECK-NEXT: [[T0:%.*]] = function_ref @_T06tuples7make_xySi1x_AA1P_p1ytyF
  // CHECK-NEXT: [[T1:%.*]] = apply [[T0]]([[PAIR_0]])
  // CHECK-NEXT: store [[T1]] to [trivial] [[PAIR_1]]
  var pair : (y:P, x:Int) = make_xy()

  // CHECK-NEXT: [[TEMP:%.*]] = alloc_stack $P
  // CHECK-NEXT: // function_ref
  // CHECK-NEXT: [[T0:%.*]] = function_ref @_T06tuples7make_xySi1x_AA1P_p1ytyF
  // CHECK-NEXT: [[T1:%.*]] = apply [[T0]]([[TEMP]])
  // CHECK-NEXT: [[WRITE:%.*]] = begin_access [modify] [unknown] [[PBPAIR]] : $*(y: P, x: Int)
  // CHECK-NEXT: [[PAIR_0:%.*]] = tuple_element_addr [[WRITE]] : $*(y: P, x: Int), 0
  // CHECK-NEXT: copy_addr [take] [[TEMP]] to [[PAIR_0]]
  // CHECK-NEXT: [[PAIR_1:%.*]] = tuple_element_addr [[WRITE]] : $*(y: P, x: Int), 1
  // CHECK-NEXT: assign [[T1]] to [[PAIR_1]]
  // CHECK-NEXT: end_access [[WRITE]] : $*(y: P, x: Int)
  // CHECK-NEXT: dealloc_stack [[TEMP]]
  pair = make_xy()
}

// CHECK-LABEL: testShuffleTuple
func testShuffleTuple() {
  // CHECK: [[X:%.*]] = alloc_box ${ var P }
  // CHECK-NEXT: [[PBX:%.*]] = project_box [[X]]
  // CHECK: [[Y:%.*]] = alloc_box ${ var Int }
  // CHECK-NEXT: [[PBY:%.*]] = project_box [[Y]]

  // CHECK:      [[T0:%.*]] = function_ref @_T06tuples8make_intSiyF
  // CHECK-NEXT: [[T1:%.*]] = apply [[T0]]()
  // CHECK-NEXT: store [[T1]] to [trivial] [[PBY]]
  // CHECK-NEXT: // function_ref
  // CHECK-NEXT: [[T0:%.*]] = function_ref @_T06tuples6make_pAA1P_pyF 
  // CHECK-NEXT: apply [[T0]]([[PBX]])
  var (x,y) : (y:P, x:Int) = (x: make_int(), y: make_p())

  // CHECK-NEXT: [[PAIR:%.*]] = alloc_box ${ var (y: P, x: Int) }
  // CHECK-NEXT: [[PBPAIR:%.*]] = project_box [[PAIR]]
  // CHECK-NEXT: [[PAIR_0:%.*]] = tuple_element_addr [[PBPAIR]] : $*(y: P, x: Int), 0
  // CHECK-NEXT: [[PAIR_1:%.*]] = tuple_element_addr [[PBPAIR]] : $*(y: P, x: Int), 1
  // CHECK-NEXT: // function_ref
  // CHECK:      [[T0:%.*]] = function_ref @_T06tuples8make_intSiyF
  // CHECK-NEXT: [[T1:%.*]] = apply [[T0]]()
  // CHECK-NEXT: store [[T1]] to [trivial] [[PAIR_1]]
  // CHECK-NEXT: // function_ref
  // CHECK-NEXT: [[T0:%.*]] = function_ref @_T06tuples6make_pAA1P_pyF 
    // CHECK-NEXT: apply [[T0]]([[PAIR_0]])
  var pair : (y:P, x:Int) = (x: make_int(), y: make_p())

  //   This isn't really optimal; we should be evaluating make_p directly
  //   into the temporary.
  // CHECK-NEXT: // function_ref
  // CHECK:      [[T0:%.*]] = function_ref @_T06tuples8make_intSiyF
  // CHECK-NEXT: [[INT:%.*]] = apply [[T0]]()
  // CHECK-NEXT: [[TEMP:%.*]] = alloc_stack $P
  // CHECK-NEXT: // function_ref
  // CHECK-NEXT: [[T0:%.*]] = function_ref @_T06tuples6make_pAA1P_pyF 
  // CHECK-NEXT: apply [[T0]]([[TEMP]])
  // CHECK-NEXT: [[WRITE:%.*]] = begin_access [modify] [unknown] [[PBPAIR]] : $*(y: P, x: Int)
  // CHECK-NEXT: [[PAIR_0:%.*]] = tuple_element_addr [[WRITE]] : $*(y: P, x: Int), 0
  // CHECK-NEXT: copy_addr [take] [[TEMP]] to [[PAIR_0]]
  // CHECK-NEXT: [[PAIR_1:%.*]] = tuple_element_addr [[WRITE]] : $*(y: P, x: Int), 1
  // CHECK-NEXT: assign [[INT]] to [[PAIR_1]]
  // CHECK-NEXT: end_access [[WRITE]] : $*(y: P, x: Int)
  // CHECK-NEXT: dealloc_stack [[TEMP]]
  pair = (x: make_int(), y: make_p())
}

enum GenericEnum<T> {
  case one(T)

  static var callback: (T) -> Void { fatalError() }
}

// CHECK-LABEL: _T06tuples16testTupleUnsplatyyF
func testTupleUnsplat() {
  // CHECK: debug_value [[X:%.+]] : $Int, let, name "x"
  // CHECK: debug_value [[Y:%.+]] : $Int, let, name "y"
  let x = 1, y = 2

  // CHECK: [[TUPLE:%.+]] = tuple ([[X]] : $Int, [[Y]] : $Int)
  // CHECK: enum $GenericEnum<(Int, Int)>, #GenericEnum.one!enumelt.1, [[TUPLE]]
  _ = GenericEnum<(Int, Int)>.one((x, y))
  // CHECK: [[TUPLE:%.+]] = tuple ([[X]] : $Int, [[Y]] : $Int)
  // CHECK: enum $GenericEnum<(Int, Int)>, #GenericEnum.one!enumelt.1, [[TUPLE]]
  _ = GenericEnum<(Int, Int)>.one(x, y)

  // CHECK: [[THUNK:%.+]] = function_ref @_T0Si_SitIegi_S2iIegyy_TR
  // CHECK: [[REABSTRACTED:%.+]] = partial_apply [callee_guaranteed] [[THUNK]]({{%.+}})
  // CHECK: [[BORROW:%.*]] = begin_borrow [[REABSTRACTED]]
  // CHECK: apply [[BORROW]]([[X]], [[Y]])
  _ = GenericEnum<(Int, Int)>.callback((x, y))
  // CHECK: [[THUNK:%.+]] = function_ref @_T0Si_SitIegi_S2iIegyy_TR
  // CHECK: [[REABSTRACTED:%.+]] = partial_apply [callee_guaranteed] [[THUNK]]({{%.+}})
  // CHECK: [[BORROW:%.*]] = begin_borrow [[REABSTRACTED]]
  // CHECK: apply [[BORROW]]([[X]], [[Y]])
  _ = GenericEnum<(Int, Int)>.callback(x, y)
} // CHECK: end sil function '_T06tuples16testTupleUnsplatyyF'

// Make sure that we use a load_borrow instead of a load [take] when RValues are
// formed with isGuaranteed set.
extension P {
  // CHECK-LABEL: sil hidden @_T06tuples1PPAAE12immutableUseyAA1CC5index_x5valuet5tuple_tFZ
  // CHECK: bb0([[TUP0:%.*]] : @owned $C, [[TUP1:%.*]] : @trivial $*Self
  // Allocate space for the RValue.
  // CHECK:   [[RVALUE:%.*]] = alloc_stack $(index: C, value: Self), let, name "tuple"
  //
  // Initialize the RValue. (This is here to help pattern matching).
  // CHECK:   [[ZERO_ADDR:%.*]] = tuple_element_addr [[RVALUE]] : $*(index: C, value: Self), 0
  // CHECK:   store [[TUP0]] to [init] [[ZERO_ADDR]]
  // CHECK:   [[ONE_ADDR:%.*]] = tuple_element_addr [[RVALUE]] : $*(index: C, value: Self), 1
  // CHECK:   copy_addr [take] [[TUP1]] to [initialization] [[ONE_ADDR]]
  //
  // What we are actually trying to check. Note that there is no actual use of
  // LOADED_CLASS. This is b/c of the nature of the RValue we are working with.
  // CHECK:   [[ZERO_ADDR:%.*]] = tuple_element_addr [[RVALUE]] : $*(index: C, value: Self), 0
  // CHECK:   [[LOADED_CLASS:%.*]] = load_borrow [[ZERO_ADDR]]
  // CHECK:   [[ONE_ADDR:%.*]] = tuple_element_addr [[RVALUE]] : $*(index: C, value: Self), 1
  // CHECK:   apply {{.*}}([[ONE_ADDR]]) : $@convention(witness_method: P)
  // CHECK:   end_borrow [[LOADED_CLASS]] from [[ZERO_ADDR]]
  // CHECK:   destroy_addr [[RVALUE]]
  // CHECK:   dealloc_stack [[RVALUE]]
  public static func immutableUse(tuple: (index: C, value: Self)) -> () {
    return tuple.value.foo()
  }
}
