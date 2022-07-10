// RUN: %target-swift-frontend  %s -Onone  -emit-sil | %FileCheck %s

// REQUIRES: optimized_stdlib

// Check that pre-specialization works at -Onone.
// This test requires the standard library to be compiled with pre-specializations!

// CHECK-LABEL: sil [noinline] @_TF13prespecialize4testFTRGSaSi_4sizeSi_T_ 
//
// function_ref specialized Collection<A where ...>.makeIterator() -> IndexingIterator<A>
// CHECK: function_ref @_TTSgq5GVs14CountableRangeSi_GS_Si_s10Collections___TFesRxs10Collectionwx8IteratorzGVs16IndexingIteratorx_wx8_ElementzWxS0_7Element_rS_12makeIteratorfT_GS1_x_
//
// function_ref specialized IndexingIterator.next() -> A._Element?
// CHECK: function_ref @_TTSgq5GVs14CountableRangeSi_GS_Si_s13IndexableBases___TFVs16IndexingIterator4nextfT_GSqwx8_Element_
//
// Look for generic specialization <Swift.Int> of Swift.Array.subscript.getter : (Swift.Int) -> A
// CHECK: function_ref {{@_TTSgq5Si___TFSag9subscriptFSix|@_TTSg5Si___TFSaap9subscriptFSix}}
// CHECK: return
@inline(never)
public func test(_ a: inout [Int], size: Int) {
  for i in 0..<size {
    for j in 0..<size {
      a[i] = a[j]
    }
  }
}

// CHECK-LABEL: sil [noinline] @_TF13prespecialize3runFT_T_
// Look for generic specialization <Swift.Int> of Swift.Array.init (repeating : A, count : Swift.Int) -> Swift.Array<A>
// CHECK: function_ref @_TTSgq5Si___TFSaCfT9repeatingx5countSi_GSax_
// CHECK: return
@inline(never)
public func run() {
  let size = 10000
  var p = [Int](repeating: 0, count: size)
  for i in 0..<size {
    p[i] = i
  }
  test(&p, size: size)
}

run()

