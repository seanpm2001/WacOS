// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership %s | %FileCheck %s

//////////////////
// Declarations //
//////////////////

class C {}

@_silgen_name("loopBodyEnd")
func loopBodyEnd() -> ()

@_silgen_name("condition")
func condition() -> Bool

@_silgen_name("loopContinueEnd")
func loopContinueEnd() -> ()

@_silgen_name("loopBreakEnd")
func loopBreakEnd() -> ()

@_silgen_name("funcEnd")
func funcEnd() -> ()

struct TrivialStruct {
  var value: Int32
}

struct NonTrivialStruct {
  var value: C
}

struct GenericStruct<T> {
  var value: T
  var value2: C
}

protocol P {}
protocol ClassP : class {}

protocol GenericCollection : Collection {

}

///////////
// Tests //
///////////

//===----------------------------------------------------------------------===//
// Trivial Struct
//===----------------------------------------------------------------------===//

// CHECK-LABEL: sil hidden @_T07foreach13trivialStructySaySiGF : $@convention(thin) (@owned Array<Int>) -> () {
// CHECK: bb0([[ARRAY:%.*]] : @owned $Array<Int>):
// CHECK:   [[ITERATOR_BOX:%.*]] = alloc_box ${ var IndexingIterator<Array<Int>> }, var, name "$x$generator"
// CHECK:   [[PROJECT_ITERATOR_BOX:%.*]] = project_box [[ITERATOR_BOX]]
// CHECK:   br [[LOOP_DEST:bb[0-9]+]]
//
// CHECK: [[LOOP_DEST]]:
// CHECK:   switch_enum [[IND_VAR:%.*]] : $Optional<Int>, case #Optional.some!enumelt.1: [[SOME_BB:bb[0-9]+]], case #Optional.none!enumelt: [[NONE_BB:bb[0-9]+]]
//
// CHECK: [[NONE_BB]]:
// CHECK:   br [[CONT_BLOCK:bb[0-9]+]]
//
// CHECK: [[SOME_BB]]([[VAR:%.*]] : @trivial $Int):
// CHECK:   [[LOOP_END_FUNC:%.*]] = function_ref @loopBodyEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_END_FUNC]]()
// CHECK:   br [[LOOP_DEST]]
//
// CHECK: [[CONT_BLOCK]]:
// CHECK:   destroy_value [[ITERATOR_BOX]]
// CHECK:   [[FUNC_END_FUNC:%.*]] = function_ref @funcEnd : $@convention(thin) () -> ()
// CHECK:   apply [[FUNC_END_FUNC]]()
// CHECK:   destroy_value [[ARRAY]]
// CHECK: } // end sil function '_T07foreach13trivialStructySaySiGF'
func trivialStruct(_ xx: [Int]) {
  for x in xx {
    loopBodyEnd()
  }
  funcEnd()
}

// TODO: Write this test
func trivialStructContinue(_ xx: [Int]) {
  for x in xx {
    if (condition()) {
      loopContinueEnd()
      continue
    }
    loopBodyEnd()
  }

  funcEnd()
}

// TODO: Write this test
func trivialStructBreak(_ xx: [Int]) {
  for x in xx {
    if (condition()) {
      loopBreakEnd()
      break
    }
    loopBodyEnd()
  }

  funcEnd()
}

// CHECK-LABEL: sil hidden @_T07foreach26trivialStructContinueBreakySaySiGF : $@convention(thin) (@owned Array<Int>) -> () {
// CHECK: bb0([[ARRAY:%.*]] : @owned $Array<Int>):
// CHECK:   [[ITERATOR_BOX:%.*]] = alloc_box ${ var IndexingIterator<Array<Int>> }, var, name "$x$generator"
// CHECK:   [[PROJECT_ITERATOR_BOX:%.*]] = project_box [[ITERATOR_BOX]]
// CHECK:   [[BORROWED_ARRAY:%.*]] = begin_borrow [[ARRAY]]
// CHECK:   [[BORROWED_ARRAY_STACK:%.*]] = alloc_stack $Array<Int>
// CHECK:   store_borrow [[BORROWED_ARRAY]] to [[BORROWED_ARRAY_STACK]]
// CHECK:   [[MAKE_ITERATOR_FUNC:%.*]] = function_ref @_T0s10CollectionPss16IndexingIteratorVyxG0C0RtzrlE04makeC0AEyF : $@convention(method) <τ_0_0 where τ_0_0 : Collection, τ_0_0.Iterator == IndexingIterator<τ_0_0>> (@in_guaranteed τ_0_0) -> @out IndexingIterator<τ_0_0>
// CHECK:   apply [[MAKE_ITERATOR_FUNC]]<[Int]>([[PROJECT_ITERATOR_BOX]], [[BORROWED_ARRAY_STACK]])
// CHECK:   br [[LOOP_DEST:bb[0-9]+]]
//
// CHECK: [[LOOP_DEST]]:
// CHECK:   [[GET_ELT_STACK:%.*]] = alloc_stack $Optional<Int>
// CHECK:   [[WRITE:%.*]] = begin_access [modify] [unknown] [[PROJECT_ITERATOR_BOX]] : $*IndexingIterator<Array<Int>>
// CHECK:   [[FUNC_REF:%.*]] = function_ref @_T0s16IndexingIteratorV4next7ElementQzSgyF : $@convention(method)
// CHECK:   apply [[FUNC_REF]]<[Int]>([[GET_ELT_STACK]], [[WRITE]])
// CHECK:   [[IND_VAR:%.*]] = load [trivial] [[GET_ELT_STACK]]
// CHECK:   switch_enum [[IND_VAR]] : $Optional<Int>, case #Optional.some!enumelt.1: [[SOME_BB:bb[0-9]+]], case #Optional.none!enumelt: [[NONE_BB:bb[0-9]+]]
//
// CHECK: [[NONE_BB]]:
// CHECK:   br [[CONT_BLOCK_JUMP:bb[0-9]+]]
//
// CHECK: [[SOME_BB]]([[VAR:%.*]] : @trivial $Int):
// CHECK:   cond_br {{%.*}}, [[LOOP_BREAK_END_BLOCK:bb[0-9]+]], [[CONTINUE_CHECK_BLOCK:bb[0-9]+]]
//
// CHECK: [[LOOP_BREAK_END_BLOCK]]:
// CHECK:   [[LOOP_BREAK_FUNC:%.*]] = function_ref @loopBreakEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_BREAK_FUNC]]()
// CHECK:   br [[CONT_BLOCK:bb[0-9]+]]
//
// CHECK: [[CONTINUE_CHECK_BLOCK]]:
// CHECK:   cond_br {{%.*}}, [[LOOP_CONTINUE_END:bb[0-9]+]], [[LOOP_END_BLOCK:bb[0-9]+]]
//
// CHECK: [[LOOP_CONTINUE_END]]:
// CHECK:   [[LOOP_CONTINUE_FUNC:%.*]] = function_ref @loopContinueEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_CONTINUE_FUNC]]() : $@convention(thin) () -> ()
// CHECK:   br [[LOOP_DEST]]
//
// CHECK: [[LOOP_END_BLOCK]]:
// CHECK:   [[LOOP_BODY_FUNC:%.*]] = function_ref @loopBodyEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_BODY_FUNC]]()
// CHECK:   br [[LOOP_DEST]]
//
// CHECK: [[CONT_BLOCK_JUMP]]:
// CHECK:   br [[CONT_BLOCK]]
//
// CHECK: [[CONT_BLOCK]]
// CHECK:   destroy_value [[ITERATOR_BOX]] : ${ var IndexingIterator<Array<Int>> }
// CHECK:   [[FUNC_END_FUNC:%.*]] = function_ref @funcEnd : $@convention(thin) () -> ()
// CHECK:   apply [[FUNC_END_FUNC]]()
// CHECK:   destroy_value [[ARRAY]]
// CHECK: } // end sil function '_T07foreach26trivialStructContinueBreakySaySiGF'
func trivialStructContinueBreak(_ xx: [Int]) {
  for x in xx {
    if (condition()) {
      loopBreakEnd()
      break
    }

    if (condition()) {
      loopContinueEnd()
      continue
    }
    loopBodyEnd()
  }

  funcEnd()
}


//===----------------------------------------------------------------------===//
// Existential
//===----------------------------------------------------------------------===//

func existential(_ xx: [P]) {
  for x in xx {
    loopBodyEnd()
  }
  funcEnd()
}

func existentialContinue(_ xx: [P]) {
  for x in xx {
    if (condition()) {
      loopContinueEnd()
      continue
    }
    loopBodyEnd()
  }

  funcEnd()
}

func existentialBreak(_ xx: [P]) {
  for x in xx {
    if (condition()) {
      loopBreakEnd()
      break
    }
    loopBodyEnd()
  }

  funcEnd()
}

// CHECK-LABEL: sil hidden @_T07foreach24existentialContinueBreakySayAA1P_pGF : $@convention(thin) (@owned Array<P>) -> () {
// CHECK: bb0([[ARRAY:%.*]] : @owned $Array<P>):
// CHECK:   [[ITERATOR_BOX:%.*]] = alloc_box ${ var IndexingIterator<Array<P>> }, var, name "$x$generator"
// CHECK:   [[PROJECT_ITERATOR_BOX:%.*]] = project_box [[ITERATOR_BOX]]
// CHECK:   [[BORROWED_ARRAY:%.*]] = begin_borrow [[ARRAY]]
// CHECK:   [[BORROWED_ARRAY_STACK:%.*]] = alloc_stack $Array<P>
// CHECK:   store_borrow [[BORROWED_ARRAY]] to [[BORROWED_ARRAY_STACK]]
// CHECK:   [[MAKE_ITERATOR_FUNC:%.*]] = function_ref @_T0s10CollectionPss16IndexingIteratorVyxG0C0RtzrlE04makeC0AEyF : $@convention(method) <τ_0_0 where τ_0_0 : Collection, τ_0_0.Iterator == IndexingIterator<τ_0_0>> (@in_guaranteed τ_0_0) -> @out IndexingIterator<τ_0_0>
// CHECK:   apply [[MAKE_ITERATOR_FUNC]]<[P]>([[PROJECT_ITERATOR_BOX]], [[BORROWED_ARRAY_STACK]])
// CHECK:   [[ELT_STACK:%.*]] = alloc_stack $Optional<P>
// CHECK:   br [[LOOP_DEST:bb[0-9]+]]
//
// CHECK: [[LOOP_DEST]]:
// CHECK:   [[WRITE:%.*]] = begin_access [modify] [unknown] [[PROJECT_ITERATOR_BOX]] : $*IndexingIterator<Array<P>> // users: %16, %15
// CHECK:   [[FUNC_REF:%.*]] = function_ref @_T0s16IndexingIteratorV4next7ElementQzSgyF : $@convention(method)
// CHECK:   apply [[FUNC_REF]]<[P]>([[ELT_STACK]], [[WRITE]])
// CHECK:   switch_enum_addr [[ELT_STACK]] : $*Optional<P>, case #Optional.some!enumelt.1: [[SOME_BB:bb[0-9]+]], case #Optional.none!enumelt: [[NONE_BB:bb[0-9]+]]
//
// CHECK: [[NONE_BB]]:
// CHECK:   br [[CONT_BLOCK_JUMP:bb[0-9]+]]
//
// CHECK: [[SOME_BB]]:
// CHECK:   [[T0:%.*]] = alloc_stack $P, let, name "x"
// CHECK:   [[ELT_STACK_TAKE:%.*]] = unchecked_take_enum_data_addr [[ELT_STACK]] : $*Optional<P>, #Optional.some!enumelt.1
// CHECK:   copy_addr [take] [[ELT_STACK_TAKE]] to [initialization] [[T0]]
// CHECK:   cond_br {{%.*}}, [[LOOP_BREAK_END_BLOCK:bb[0-9]+]], [[CONTINUE_CHECK_BLOCK:bb[0-9]+]]
//
// CHECK: [[LOOP_BREAK_END_BLOCK]]:
// CHECK:   [[LOOP_BREAK_FUNC:%.*]] = function_ref @loopBreakEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_BREAK_FUNC]]()
// CHECK:   destroy_addr [[T0]]
// CHECK:   dealloc_stack [[T0]]
// CHECK:   br [[CONT_BLOCK:bb[0-9]+]]
//
// CHECK: [[CONTINUE_CHECK_BLOCK]]:
// CHECK:   cond_br {{%.*}}, [[LOOP_CONTINUE_END:bb[0-9]+]], [[LOOP_END_BLOCK:bb[0-9]+]]
//
// CHECK: [[LOOP_CONTINUE_END]]:
// CHECK:   [[LOOP_CONTINUE_FUNC:%.*]] = function_ref @loopContinueEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_CONTINUE_FUNC]]() : $@convention(thin) () -> ()
// CHECK:   destroy_addr [[T0]]
// CHECK:   dealloc_stack [[T0]]
// CHECK:   br [[LOOP_DEST]]
//
// CHECK: [[LOOP_END_BLOCK]]:
// CHECK:   [[LOOP_BODY_FUNC:%.*]] = function_ref @loopBodyEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_BODY_FUNC]]()
// CHECK:   destroy_addr [[T0]]
// CHECK:   dealloc_stack [[T0]]
// CHECK:   br [[LOOP_DEST]]
//
// CHECK: [[CONT_BLOCK_JUMP]]:
// CHECK:   br [[CONT_BLOCK]]
//
// CHECK: [[CONT_BLOCK]]
// CHECK:   dealloc_stack [[ELT_STACK]]
// CHECK:   destroy_value [[ITERATOR_BOX]] : ${ var IndexingIterator<Array<P>> }
// CHECK:   [[FUNC_END_FUNC:%.*]] = function_ref @funcEnd : $@convention(thin) () -> ()
// CHECK:   apply [[FUNC_END_FUNC]]()
// CHECK:   destroy_value [[ARRAY]]
// CHECK: } // end sil function '_T07foreach24existentialContinueBreakySayAA1P_pGF'
func existentialContinueBreak(_ xx: [P]) {
  for x in xx {
    if (condition()) {
      loopBreakEnd()
      break
    }

    if (condition()) {
      loopContinueEnd()
      continue
    }
    loopBodyEnd()
  }

  funcEnd()
}

//===----------------------------------------------------------------------===//
// Class Constrainted Existential
//===----------------------------------------------------------------------===//

func existentialClass(_ xx: [ClassP]) {
  for x in xx {
    loopBodyEnd()
  }
  funcEnd()
}

func existentialClassContinue(_ xx: [ClassP]) {
  for x in xx {
    if (condition()) {
      loopContinueEnd()
      continue
    }
    loopBodyEnd()
  }

  funcEnd()
}

func existentialClassBreak(_ xx: [ClassP]) {
  for x in xx {
    if (condition()) {
      loopBreakEnd()
      break
    }
    loopBodyEnd()
  }

  funcEnd()
}

func existentialClassContinueBreak(_ xx: [ClassP]) {
  for x in xx {
    if (condition()) {
      loopBreakEnd()
      break
    }

    if (condition()) {
      loopContinueEnd()
      continue
    }
    loopBodyEnd()
  }

  funcEnd()
}

//===----------------------------------------------------------------------===//
// Generic Struct
//===----------------------------------------------------------------------===//

func genericStruct<T>(_ xx: [GenericStruct<T>]) {
  for x in xx {
    loopBodyEnd()
  }
  funcEnd()
}

func genericStructContinue<T>(_ xx: [GenericStruct<T>]) {
  for x in xx {
    if (condition()) {
      loopContinueEnd()
      continue
    }
    loopBodyEnd()
  }

  funcEnd()
}

func genericStructBreak<T>(_ xx: [GenericStruct<T>]) {
  for x in xx {
    if (condition()) {
      loopBreakEnd()
      break
    }
    loopBodyEnd()
  }

  funcEnd()
}

// CHECK-LABEL: sil hidden @_T07foreach26genericStructContinueBreakySayAA07GenericC0VyxGGlF : $@convention(thin) <T> (@owned Array<GenericStruct<T>>) -> () {
// CHECK: bb0([[ARRAY:%.*]] : @owned $Array<GenericStruct<T>>):
// CHECK:   [[ITERATOR_BOX:%.*]] = alloc_box $<τ_0_0> { var IndexingIterator<Array<GenericStruct<τ_0_0>>> } <T>, var, name "$x$generator"
// CHECK:   [[PROJECT_ITERATOR_BOX:%.*]] = project_box [[ITERATOR_BOX]]
// CHECK:   [[BORROWED_ARRAY:%.*]] = begin_borrow [[ARRAY]]
// CHECK:   [[BORROWED_ARRAY_STACK:%.*]] = alloc_stack $Array<GenericStruct<T>>
// CHECK:   store_borrow [[BORROWED_ARRAY]] to [[BORROWED_ARRAY_STACK]]
// CHECK:   [[MAKE_ITERATOR_FUNC:%.*]] = function_ref @_T0s10CollectionPss16IndexingIteratorVyxG0C0RtzrlE04makeC0AEyF : $@convention(method) <τ_0_0 where τ_0_0 : Collection, τ_0_0.Iterator == IndexingIterator<τ_0_0>> (@in_guaranteed τ_0_0) -> @out IndexingIterator<τ_0_0>
// CHECK:   apply [[MAKE_ITERATOR_FUNC]]<[GenericStruct<T>]>([[PROJECT_ITERATOR_BOX]], [[BORROWED_ARRAY_STACK]])
// CHECK:   [[ELT_STACK:%.*]] = alloc_stack $Optional<GenericStruct<T>>
// CHECK:   br [[LOOP_DEST:bb[0-9]+]]
//
// CHECK: [[LOOP_DEST]]:
// CHECK:   [[WRITE:%.*]] = begin_access [modify] [unknown] [[PROJECT_ITERATOR_BOX]] : $*IndexingIterator<Array<GenericStruct<T>>>
// CHECK:   [[FUNC_REF:%.*]] = function_ref @_T0s16IndexingIteratorV4next7ElementQzSgyF : $@convention(method)
// CHECK:   apply [[FUNC_REF]]<[GenericStruct<T>]>([[ELT_STACK]], [[WRITE]])
// CHECK:   switch_enum_addr [[ELT_STACK]] : $*Optional<GenericStruct<T>>, case #Optional.some!enumelt.1: [[SOME_BB:bb[0-9]+]], case #Optional.none!enumelt: [[NONE_BB:bb[0-9]+]]
//
// CHECK: [[NONE_BB]]:
// CHECK:   br [[CONT_BLOCK_JUMP:bb[0-9]+]]
//
// CHECK: [[SOME_BB]]:
// CHECK:   [[T0:%.*]] = alloc_stack $GenericStruct<T>, let, name "x"
// CHECK:   [[ELT_STACK_TAKE:%.*]] = unchecked_take_enum_data_addr [[ELT_STACK]] : $*Optional<GenericStruct<T>>, #Optional.some!enumelt.1
// CHECK:   copy_addr [take] [[ELT_STACK_TAKE]] to [initialization] [[T0]]
// CHECK:   cond_br {{%.*}}, [[LOOP_BREAK_END_BLOCK:bb[0-9]+]], [[CONTINUE_CHECK_BLOCK:bb[0-9]+]]
//
// CHECK: [[LOOP_BREAK_END_BLOCK]]:
// CHECK:   [[LOOP_BREAK_FUNC:%.*]] = function_ref @loopBreakEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_BREAK_FUNC]]()
// CHECK:   destroy_addr [[T0]]
// CHECK:   dealloc_stack [[T0]]
// CHECK:   br [[CONT_BLOCK:bb[0-9]+]]
//
// CHECK: [[CONTINUE_CHECK_BLOCK]]:
// CHECK:   cond_br {{%.*}}, [[LOOP_CONTINUE_END:bb[0-9]+]], [[LOOP_END_BLOCK:bb[0-9]+]]
//
// CHECK: [[LOOP_CONTINUE_END]]:
// CHECK:   [[LOOP_CONTINUE_FUNC:%.*]] = function_ref @loopContinueEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_CONTINUE_FUNC]]() : $@convention(thin) () -> ()
// CHECK:   destroy_addr [[T0]]
// CHECK:   dealloc_stack [[T0]]
// CHECK:   br [[LOOP_DEST]]
//
// CHECK: [[LOOP_END_BLOCK]]:
// CHECK:   [[LOOP_BODY_FUNC:%.*]] = function_ref @loopBodyEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_BODY_FUNC]]()
// CHECK:   destroy_addr [[T0]]
// CHECK:   dealloc_stack [[T0]]
// CHECK:   br [[LOOP_DEST]]
//
// CHECK: [[CONT_BLOCK_JUMP]]:
// CHECK:   br [[CONT_BLOCK]]
//
// CHECK: [[CONT_BLOCK]]
// CHECK:   dealloc_stack [[ELT_STACK]]
// CHECK:   destroy_value [[ITERATOR_BOX]]
// CHECK:   [[FUNC_END_FUNC:%.*]] = function_ref @funcEnd : $@convention(thin) () -> ()
// CHECK:   apply [[FUNC_END_FUNC]]()
// CHECK:   destroy_value [[ARRAY]]
// CHECK: } // end sil function '_T07foreach26genericStructContinueBreakySayAA07GenericC0VyxGGlF'
func genericStructContinueBreak<T>(_ xx: [GenericStruct<T>]) {
  for x in xx {
    if (condition()) {
      loopBreakEnd()
      break
    }

    if (condition()) {
      loopContinueEnd()
      continue
    }
    loopBodyEnd()
  }

  funcEnd()
}

//===----------------------------------------------------------------------===//
// Fully Generic Collection
//===----------------------------------------------------------------------===//

func genericCollection<T : Collection>(_ xx: T) {
  for x in xx {
    loopBodyEnd()
  }
  funcEnd()
}

func genericCollectionContinue<T : Collection>(_ xx: T) {
  for x in xx {
    if (condition()) {
      loopContinueEnd()
      continue
    }
    loopBodyEnd()
  }

  funcEnd()
}

func genericCollectionBreak<T : Collection>(_ xx: T) {
  for x in xx {
    if (condition()) {
      loopBreakEnd()
      break
    }
    loopBodyEnd()
  }

  funcEnd()
}

// CHECK-LABEL: sil hidden @_T07foreach30genericCollectionContinueBreakyxs0C0RzlF : $@convention(thin) <T where T : Collection> (@in T) -> () {
// CHECK: bb0([[COLLECTION:%.*]] : @trivial $*T):
// CHECK:   [[ITERATOR_BOX:%.*]] = alloc_box $<τ_0_0 where τ_0_0 : Collection> { var τ_0_0.Iterator } <T>, var, name "$x$generator"
// CHECK:   [[PROJECT_ITERATOR_BOX:%.*]] = project_box [[ITERATOR_BOX]]
// CHECK:   [[MAKE_ITERATOR_FUNC:%.*]] = witness_method $T, #Sequence.makeIterator!1 : <Self where Self : Sequence> (Self) -> () -> Self.Iterator : $@convention(witness_method: Sequence) <τ_0_0 where τ_0_0 : Sequence> (@in_guaranteed τ_0_0) -> @out τ_0_0.Iterator
// CHECK:   apply [[MAKE_ITERATOR_FUNC]]<T>([[PROJECT_ITERATOR_BOX]], [[COLLECTION]])
// CHECK:   [[ELT_STACK:%.*]] = alloc_stack $Optional<T.Element>
// CHECK:   br [[LOOP_DEST:bb[0-9]+]]
//
// CHECK: [[LOOP_DEST]]:
// CHECK:   [[WRITE:%.*]] = begin_access [modify] [unknown] [[PROJECT_ITERATOR_BOX]] : $*T.Iterator
// CHECK:   [[GET_NEXT_FUNC:%.*]] = witness_method $T.Iterator, #IteratorProtocol.next!1 : <Self where Self : IteratorProtocol> (inout Self) -> () -> Self.Element? : $@convention(witness_method: IteratorProtocol) <τ_0_0 where τ_0_0 : IteratorProtocol> (@inout τ_0_0) -> @out Optional<τ_0_0.Element>
// CHECK:   apply [[GET_NEXT_FUNC]]<T.Iterator>([[ELT_STACK]], [[WRITE]])
// CHECK:   switch_enum_addr [[ELT_STACK]] : $*Optional<T.Element>, case #Optional.some!enumelt.1: [[SOME_BB:bb[0-9]+]], case #Optional.none!enumelt: [[NONE_BB:bb[0-9]+]]
//
// CHECK: [[NONE_BB]]:
// CHECK:   br [[CONT_BLOCK_JUMP:bb[0-9]+]]
//
// CHECK: [[SOME_BB]]:
// CHECK:   [[T0:%.*]] = alloc_stack $T.Element, let, name "x"
// CHECK:   [[ELT_STACK_TAKE:%.*]] = unchecked_take_enum_data_addr [[ELT_STACK]] : $*Optional<T.Element>, #Optional.some!enumelt.1
// CHECK:   copy_addr [take] [[ELT_STACK_TAKE]] to [initialization] [[T0]]
// CHECK:   cond_br {{%.*}}, [[LOOP_BREAK_END_BLOCK:bb[0-9]+]], [[CONTINUE_CHECK_BLOCK:bb[0-9]+]]
//
// CHECK: [[LOOP_BREAK_END_BLOCK]]:
// CHECK:   [[LOOP_BREAK_FUNC:%.*]] = function_ref @loopBreakEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_BREAK_FUNC]]()
// CHECK:   destroy_addr [[T0]]
// CHECK:   dealloc_stack [[T0]]
// CHECK:   br [[CONT_BLOCK:bb[0-9]+]]
//
// CHECK: [[CONTINUE_CHECK_BLOCK]]:
// CHECK:   cond_br {{%.*}}, [[LOOP_CONTINUE_END:bb[0-9]+]], [[LOOP_END_BLOCK:bb[0-9]+]]
//
// CHECK: [[LOOP_CONTINUE_END]]:
// CHECK:   [[LOOP_CONTINUE_FUNC:%.*]] = function_ref @loopContinueEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_CONTINUE_FUNC]]() : $@convention(thin) () -> ()
// CHECK:   destroy_addr [[T0]]
// CHECK:   dealloc_stack [[T0]]
// CHECK:   br [[LOOP_DEST]]
//
// CHECK: [[LOOP_END_BLOCK]]:
// CHECK:   [[LOOP_BODY_FUNC:%.*]] = function_ref @loopBodyEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_BODY_FUNC]]()
// CHECK:   destroy_addr [[T0]]
// CHECK:   dealloc_stack [[T0]]
// CHECK:   br [[LOOP_DEST]]
//
// CHECK: [[CONT_BLOCK_JUMP]]:
// CHECK:   br [[CONT_BLOCK]]
//
// CHECK: [[CONT_BLOCK]]
// CHECK:   dealloc_stack [[ELT_STACK]]
// CHECK:   destroy_value [[ITERATOR_BOX]]
// CHECK:   [[FUNC_END_FUNC:%.*]] = function_ref @funcEnd : $@convention(thin) () -> ()
// CHECK:   apply [[FUNC_END_FUNC]]()
// CHECK:   destroy_addr [[COLLECTION]]
// CHECK: } // end sil function '_T07foreach30genericCollectionContinueBreakyxs0C0RzlF'
func genericCollectionContinueBreak<T : Collection>(_ xx: T) {
  for x in xx {
    if (condition()) {
      loopBreakEnd()
      break
    }

    if (condition()) {
      loopContinueEnd()
      continue
    }
    loopBodyEnd()
  }

  funcEnd()
}

//===----------------------------------------------------------------------===//
// Pattern Match Tests
//===----------------------------------------------------------------------===//

// CHECK-LABEL: sil hidden @_T07foreach13tupleElementsySayAA1CC_ADtGF
func tupleElements(_ xx: [(C, C)]) {
  // CHECK: bb3([[PAYLOAD:%.*]] : @owned $(C, C)):
  // CHECK: [[BORROWED_PAYLOAD:%.*]] = begin_borrow [[PAYLOAD]]
  // CHECK: [[A:%.*]] = tuple_extract [[BORROWED_PAYLOAD]] : $(C, C), 0
  // CHECK: [[COPY_A:%.*]] = copy_value [[A]]
  // CHECK: [[B:%.*]] = tuple_extract [[BORROWED_PAYLOAD]] : $(C, C), 1
  // CHECK: [[COPY_B:%.*]] = copy_value [[B]]
  // CHECK: end_borrow [[BORROWED_PAYLOAD]] from [[PAYLOAD]]
  // CHECK: destroy_value [[COPY_B]]
  // CHECK: destroy_value [[COPY_A]]
  // CHECK: destroy_value [[PAYLOAD]]
  for (a, b) in xx {}
  // CHECK: bb7([[PAYLOAD:%.*]] : @owned $(C, C)):
  // CHECK: [[BORROWED_PAYLOAD:%.*]] = begin_borrow [[PAYLOAD]]
  // CHECK: [[A:%.*]] = tuple_extract [[BORROWED_PAYLOAD]] : $(C, C), 0
  // CHECK: [[COPY_A:%.*]] = copy_value [[A]]
  // CHECK: [[B:%.*]] = tuple_extract [[BORROWED_PAYLOAD]] : $(C, C), 1
  // CHECK: [[COPY_B:%.*]] = copy_value [[B]]
  // CHECK: destroy_value [[COPY_B]]
  // CHECK: end_borrow [[BORROWED_PAYLOAD]] from [[PAYLOAD]]
  // CHECK: destroy_value [[COPY_A]]
  // CHECK: destroy_value [[PAYLOAD]]
  for (a, _) in xx {}
  // CHECK: bb11([[PAYLOAD:%.*]] : @owned $(C, C)):
  // CHECK: [[BORROWED_PAYLOAD:%.*]] = begin_borrow [[PAYLOAD]]
  // CHECK: [[A:%.*]] = tuple_extract [[BORROWED_PAYLOAD]] : $(C, C), 0
  // CHECK: [[COPY_A:%.*]] = copy_value [[A]]
  // CHECK: [[B:%.*]] = tuple_extract [[BORROWED_PAYLOAD]] : $(C, C), 1
  // CHECK: [[COPY_B:%.*]] = copy_value [[B]]
  // CHECK: destroy_value [[COPY_A]]
  // CHECK: end_borrow [[BORROWED_PAYLOAD]] from [[PAYLOAD]]
  // CHECK: destroy_value [[COPY_B]]
  // CHECK: destroy_value [[PAYLOAD]]
  for (_, b) in xx {}
  // CHECK: bb15([[PAYLOAD:%.*]] : @owned $(C, C)):
  // CHECK: [[BORROWED_PAYLOAD:%.*]] = begin_borrow [[PAYLOAD]]
  // CHECK: [[A:%.*]] = tuple_extract [[BORROWED_PAYLOAD]] : $(C, C), 0
  // CHECK: [[COPY_A:%.*]] = copy_value [[A]]
  // CHECK: [[B:%.*]] = tuple_extract [[BORROWED_PAYLOAD]] : $(C, C), 1
  // CHECK: [[COPY_B:%.*]] = copy_value [[B]]
  // CHECK: destroy_value [[COPY_B]]
  // CHECK: destroy_value [[COPY_A]]
  // CHECK: end_borrow [[BORROWED_PAYLOAD]] from [[PAYLOAD]]
  // CHECK: destroy_value [[PAYLOAD]]
  for (_, _) in xx {}
  // CHECK: bb19([[PAYLOAD:%.*]] : @owned $(C, C)):
  // CHECK: [[BORROWED_PAYLOAD:%.*]] = begin_borrow [[PAYLOAD]]
  // CHECK: [[A:%.*]] = tuple_extract [[BORROWED_PAYLOAD]] : $(C, C), 0
  // CHECK: [[COPY_A:%.*]] = copy_value [[A]]
  // CHECK: [[B:%.*]] = tuple_extract [[BORROWED_PAYLOAD]] : $(C, C), 1
  // CHECK: [[COPY_B:%.*]] = copy_value [[B]]
  // CHECK: destroy_value [[COPY_B]]
  // CHECK: destroy_value [[COPY_A]]
  // CHECK: end_borrow [[BORROWED_PAYLOAD]] from [[PAYLOAD]]
  // CHECK: destroy_value [[PAYLOAD]]
  for  _     in xx {}
}

// Make sure that when we have an unused value, we properly iterate over the
// loop rather than run through the loop once.
//
// CHECK-LABEL: sil hidden @_T07foreach16unusedArgPatternySaySiGF : $@convention(thin) (@owned Array<Int>) -> () {
// CHECK: bb0([[ARG:%.*]] : @owned $Array<Int>):
// CHECK:   br [[LOOP_DEST:bb[0-9]+]]
//
// CHECK: [[LOOP_DEST]]:
// CHECK:   switch_enum [[OPT_VAL:%.*]] : $Optional<Int>, case #Optional.some!enumelt.1: [[SOME_BB:bb[0-9]+]], case #Optional.none!enumelt: [[NONE_BB:bb[0-9]+]]
//
// CHECK: [[NONE_BB]]:
// CHECK:   br [[CONT_BB:bb[0-9]+]]
//
// CHECK: [[SOME_BB]]([[VAL:%.*]]  : @trivial $Int):
// CHECK:   [[LOOP_END_FUNC:%.*]] = function_ref @loopBodyEnd : $@convention(thin) () -> ()
// CHECK:   apply [[LOOP_END_FUNC]]
func unusedArgPattern(_ xx: [Int]) {
  for _ in xx {
    loopBodyEnd()
  }
}
