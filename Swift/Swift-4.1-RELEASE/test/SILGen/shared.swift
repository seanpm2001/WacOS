// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -emit-silgen %s -disable-objc-attr-requires-foundation-module -enable-sil-ownership | %FileCheck %s

class RefAggregate {}
struct ValueAggregate { let x = RefAggregate() }


// CHECK-LABEL: sil hidden @_T06shared0A10_argumentsySih7trivial_AA14ValueAggregateVh5valueAA03RefE0Ch3reftF : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()
func shared_arguments(trivial : __shared Int, value : __shared ValueAggregate, ref : __shared RefAggregate) {
  // CHECK: bb0([[TRIVIAL_VAL:%[0-9]+]] : @trivial $Int, [[VALUE_VAL:%[0-9]+]] : @guaranteed $ValueAggregate, [[REF_VAL:%[0-9]+]] : @guaranteed $RefAggregate):
  // CHECK: [[COPY_VALUE_VAL:%[0-9]+]] = copy_value [[VALUE_VAL]] : $ValueAggregate
  // CHECK: [[COPY_REF_VAL:%[0-9]+]] = copy_value [[REF_VAL]] : $RefAggregate
  // CHECK: [[OWNED_FUNC:%[0-9]+]] = function_ref @_T06shared15owned_argumentsySi7trivial_AA14ValueAggregateV5valueAA03RefF0C3reftF
  // CHECK: {{%.*}} = apply [[OWNED_FUNC]]([[TRIVIAL_VAL]], [[COPY_VALUE_VAL]], [[COPY_REF_VAL]]) : $@convention(thin) (Int, @owned ValueAggregate, @owned RefAggregate) -> ()
  // CHECK: } // end sil function '_T06shared0A10_argumentsySih7trivial_AA14ValueAggregateVh5valueAA03RefE0Ch3reftF'
  return owned_arguments(trivial: trivial, value: value, ref: ref)
}

// CHECK-LABEL: sil hidden @_T06shared15owned_argumentsySi7trivial_AA14ValueAggregateV5valueAA03RefF0C3reftF : $@convention(thin) (Int, @owned ValueAggregate, @owned RefAggregate) -> ()
func owned_arguments(trivial : Int, value : ValueAggregate, ref : RefAggregate) {
  // CHECK: bb0([[TRIVIAL_VAL:%[0-9]+]] : @trivial $Int, [[VALUE_VAL:%[0-9]+]] : @owned $ValueAggregate, [[REF_VAL:%[0-9]+]] : @owned $RefAggregate):
  // CHECK: [[BORROW_VALUE_VAL:%[0-9]+]] = begin_borrow [[VALUE_VAL]] : $ValueAggregate
  // CHECK: [[BORROW_REF_VAL:%[0-9]+]] = begin_borrow [[REF_VAL]] : $RefAggregate
  // CHECK: [[SHARED_FUNC:%[0-9]+]] = function_ref @_T06shared0A10_argumentsySih7trivial_AA14ValueAggregateVh5valueAA03RefE0Ch3reftF
  // CHECK: {{%.*}} = apply [[SHARED_FUNC]]([[TRIVIAL_VAL]], [[BORROW_VALUE_VAL]], [[BORROW_REF_VAL]])
  // CHECK: end_borrow [[BORROW_REF_VAL]] from [[REF_VAL]] : $RefAggregate, $RefAggregate
  // CHECK: end_borrow [[BORROW_VALUE_VAL]] from [[VALUE_VAL]] : $ValueAggregate, $ValueAggregate
  // CHECK: destroy_value [[REF_VAL]] : $RefAggregate
  // CHECK: destroy_value [[VALUE_VAL]] : $ValueAggregate
  // CHECK: } // end sil function '_T06shared15owned_argumentsySi7trivial_AA14ValueAggregateV5valueAA03RefF0C3reftF'
  return shared_arguments(trivial: trivial, value: value, ref: ref)
}

// CHECK-LABEL: sil hidden @_T06shared0A17_argument_captureySih7trivial_AA14ValueAggregateVh5valueAA03RefF0Ch3reftF : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()
func shared_argument_capture(trivial : __shared Int, value : __shared ValueAggregate, ref : __shared RefAggregate) {
  // CHECK: bb0([[TRIVIAL_VAL:%[0-9]+]] : @trivial $Int, [[VALUE_VAL:%[0-9]+]] : @guaranteed $ValueAggregate, [[REF_VAL:%[0-9]+]] : @guaranteed $RefAggregate):
  // CHECK: [[CLO_1:%[0-9]+]] = function_ref @_T06shared0A17_argument_captureySih7trivial_AA14ValueAggregateVh5valueAA03RefF0Ch3reftFyycfU_
  // CHECK: {{%.*}} = apply [[CLO_1]]([[TRIVIAL_VAL]], [[VALUE_VAL]], [[REF_VAL]]) : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()
  _ = {
    return shared_arguments(trivial: trivial, value: value, ref: ref)
  }()
  
  // CHECK: [[CLO_2:%[0-9]+]] = function_ref @_T06shared0A17_argument_captureySih7trivial_AA14ValueAggregateVh5valueAA03RefF0Ch3reftFyycfU0_ : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()
  // CHECK: {{%.*}} = apply [[CLO_2]]([[TRIVIAL_VAL]], [[VALUE_VAL]], [[REF_VAL]]) : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()
  _ = {
    return owned_arguments(trivial: trivial, value: value, ref: ref)
  }()
  
  // CHECK: } // end sil function '_T06shared0A17_argument_captureySih7trivial_AA14ValueAggregateVh5valueAA03RefF0Ch3reftF'
  
  // ======== FIRST CLOSURE ==========

  // CHECK-LABEL: sil private @_T06shared0A17_argument_captureySih7trivial_AA14ValueAggregateVh5valueAA03RefF0Ch3reftFyycfU_ : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()
  // CHECK: bb0([[TRIVIAL_VAL:%[0-9]+]] : @trivial $Int, [[VALUE_VAL:%[0-9]+]] : @guaranteed $ValueAggregate, [[REF_VAL:%[0-9]+]] : @guaranteed $RefAggregate):
  // CHECK: [[SHARED_CALL:%[0-9]+]] = function_ref @_T06shared0A10_argumentsySih7trivial_AA14ValueAggregateVh5valueAA03RefE0Ch3reftF : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()
  // CHECK: {{%.*}} = apply [[SHARED_CALL]]([[TRIVIAL_VAL]], [[VALUE_VAL]], [[REF_VAL]]) : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()
  // CHECK: } // end sil function '_T06shared0A17_argument_captureySih7trivial_AA14ValueAggregateVh5valueAA03RefF0Ch3reftFyycfU_'
  
  // ======== SECOND CLOSURE ==========
  
  // CHECK-LABEL:  sil private @_T06shared0A17_argument_captureySih7trivial_AA14ValueAggregateVh5valueAA03RefF0Ch3reftFyycfU0_ : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> () {
  // CHECK: bb0([[TRIVIAL_VAL:%[0-9]+]] : @trivial $Int, [[VALUE_VAL:%[0-9]+]] : @guaranteed $ValueAggregate, [[REF_VAL:%[0-9]+]] : @guaranteed $RefAggregate):
  // CHECK: [[COPY_BORROW_VALUE_VAL:%[0-9]+]] = copy_value [[VALUE_VAL]] : $ValueAggregate
  // CHECK: [[COPY_BORROW_REF_VAL:%[0-9]+]] = copy_value [[REF_VAL]] : $RefAggregate
  // CHECK: [[OWNED_CALL:%[0-9]+]] = function_ref @_T06shared15owned_argumentsySi7trivial_AA14ValueAggregateV5valueAA03RefF0C3reftF : $@convention(thin) (Int, @owned ValueAggregate, @owned RefAggregate) -> ()
  // CHECK: {{%.*}} = apply [[OWNED_CALL]]([[TRIVIAL_VAL]], [[COPY_BORROW_VALUE_VAL]], [[COPY_BORROW_REF_VAL]]) : $@convention(thin) (Int, @owned ValueAggregate, @owned RefAggregate) -> ()
  // CHECK: } // end sil function '_T06shared0A17_argument_captureySih7trivial_AA14ValueAggregateVh5valueAA03RefF0Ch3reftFyycfU0_'
}

// CHECK-LABEL: sil hidden @_T06shared0A20_to_owned_conversionyySih_AA14ValueAggregateVhAA03RefF0ChtcF : $@convention(thin) (@owned @noescape @callee_guaranteed (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()) -> ()
func shared_to_owned_conversion(_ f : (__shared Int, __shared ValueAggregate, __shared RefAggregate) -> Void) {
  // CHECK: bb0([[UNUSED_FUNC:%[0-9]+]] : @owned $@noescape @callee_guaranteed (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()):
  // CHECK: [[OWNED_THUNK:%[0-9]+]] = function_ref @_T06shared0A20_to_owned_conversionyySih_AA14ValueAggregateVhAA03RefF0ChtcFySi_AdFtcfU_ : $@convention(thin) (Int, @owned ValueAggregate, @owned RefAggregate) -> ()
  // CHECK: [[CONVERT:%.*]] = convert_function [[OWNED_THUNK]]
  // CHECK: [[THICK_OWNED_THUNK:%[0-9]+]] = thin_to_thick_function [[CONVERT]] : $@convention(thin) @noescape (Int, @owned ValueAggregate, @owned RefAggregate) -> () to $@noescape @callee_guaranteed (Int, @owned ValueAggregate, @owned RefAggregate) -> ()
  // CHECK: [[GUARANTEED_TO_OWNED_THUNK:%[0-9]+]] =  function_ref @_T0Si6shared14ValueAggregateVAA03RefC0CIgyxx_SiAcEIgygg_TR : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate, @guaranteed @noescape @callee_guaranteed (Int, @owned ValueAggregate, @owned RefAggregate) -> ()) -> ()
  // CHECK: [[APPLIED_THUNK:%[0-9]+]] = partial_apply [callee_guaranteed] [[GUARANTEED_TO_OWNED_THUNK]]([[THICK_OWNED_THUNK]]) : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate, @guaranteed @noescape @callee_guaranteed (Int, @owned ValueAggregate, @owned RefAggregate) -> ()) -> ()
  // CHECK: [[CONVERT:%.*]] = convert_function [[APPLIED_THUNK]]
  // CHECK: [[RECUR_FN:%[0-9]+]] = function_ref @_T06shared0A20_to_owned_conversionyySih_AA14ValueAggregateVhAA03RefF0ChtcF : $@convention(thin) (@owned @noescape @callee_guaranteed (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()) -> ()
  // CHECK: {{%.*}} = apply [[RECUR_FN]]([[CONVERT]]) : $@convention(thin) (@owned @noescape @callee_guaranteed (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()) -> ()
  // CHECK: destroy_value [[UNUSED_FUNC]] : $@noescape @callee_guaranteed (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()
  // CHECK: } // end sil function '_T06shared0A20_to_owned_conversionyySih_AA14ValueAggregateVhAA03RefF0ChtcF'
  
  // ======== REABSTRACTION THUNK =========
  
  // CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T0Si6shared14ValueAggregateVAA03RefC0CIgyxx_SiAcEIgygg_TR : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate, @guaranteed @noescape @callee_guaranteed (Int, @owned ValueAggregate, @owned RefAggregate) -> ()) -> ()
  // CHECK: bb0([[TRIVIAL_VAL:%[0-9]+]] : @trivial $Int, [[VALUE_VAL:%[0-9]+]] : @guaranteed $ValueAggregate, [[REF_VAL:%[0-9]+]] : @guaranteed $RefAggregate, [[FUNC:%[0-9]+]] : @guaranteed $@noescape @callee_guaranteed (Int, @owned ValueAggregate, @owned RefAggregate) -> ()):
  // CHECK: [[COPY_VALUE_VAL:%[0-9]+]] = copy_value [[VALUE_VAL]] : $ValueAggregate
  // CHECK: [[COPY_REF_VAL:%[0-9]+]] = copy_value [[REF_VAL]] : $RefAggregate
  // CHECK: {{%.*}} = apply [[FUNC]]([[TRIVIAL_VAL]], [[COPY_VALUE_VAL]], [[COPY_REF_VAL]]) : $@noescape @callee_guaranteed (Int, @owned ValueAggregate, @owned RefAggregate) -> ()
  // CHECK: } // end sil function '_T0Si6shared14ValueAggregateVAA03RefC0CIgyxx_SiAcEIgygg_TR'
  
  return shared_to_owned_conversion { (trivial : Int, val : ValueAggregate, ref : RefAggregate) in }
}

// CHECK-LABEL: sil hidden @_T06shared09owned_to_A11_conversionyySi_AA14ValueAggregateVAA03RefF0CtcF : $@convention(thin) (@owned @noescape @callee_guaranteed (Int, @owned ValueAggregate, @owned RefAggregate) -> ()) -> ()
func owned_to_shared_conversion(_ f : (Int, ValueAggregate, RefAggregate) -> Void) {
  // CHECK: bb0([[UNUSED_FUNC:%[0-9]+]] : @owned $@noescape @callee_guaranteed (Int, @owned ValueAggregate, @owned RefAggregate) -> ()):
  // CHECK: [[SHARED_THUNK:%[0-9]+]] = function_ref @_T06shared09owned_to_A11_conversionyySi_AA14ValueAggregateVAA03RefF0CtcFySih_ADhAFhtcfU_ : $@convention(thin) (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()
  // CHECK: [[CONVERT:%.*]] = convert_function [[SHARED_THUNK]]
  // CHECK: [[THICK_SHARED_THUNK:%[0-9]+]] = thin_to_thick_function [[CONVERT]] : $@convention(thin) @noescape (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> () to $@noescape @callee_guaranteed (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()
  // CHECK: [[OWNED_TO_GUARANTEED_THUNK:%[0-9]+]] = function_ref @_T0Si6shared14ValueAggregateVAA03RefC0CIgygg_SiAcEIgyxx_TR : $@convention(thin) (Int, @owned ValueAggregate, @owned RefAggregate, @guaranteed @noescape @callee_guaranteed (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()) -> ()
  // CHECK: [[APPLIED_THUNK:%[0-9]+]] = partial_apply [callee_guaranteed] [[OWNED_TO_GUARANTEED_THUNK]]([[THICK_SHARED_THUNK]]) : $@convention(thin) (Int, @owned ValueAggregate, @owned RefAggregate, @guaranteed @noescape @callee_guaranteed (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()) -> ()
  // CHECK: [[CONVERT:%.*]] = convert_function [[APPLIED_THUNK]]
  // CHECK: [[RECUR_FN:%[0-9]+]] = function_ref @_T06shared09owned_to_A11_conversionyySi_AA14ValueAggregateVAA03RefF0CtcF : $@convention(thin) (@owned @noescape @callee_guaranteed (Int, @owned ValueAggregate, @owned RefAggregate) -> ()) -> ()
  // CHECK: {{%.*}} = apply [[RECUR_FN]]([[CONVERT]]) : $@convention(thin) (@owned @noescape @callee_guaranteed (Int, @owned ValueAggregate, @owned RefAggregate) -> ()) -> ()
  // CHECK: destroy_value [[UNUSED_FUNC]] : $@noescape @callee_guaranteed (Int, @owned ValueAggregate, @owned RefAggregate) -> ()
  // CHECK: } // end sil function '_T06shared09owned_to_A11_conversionyySi_AA14ValueAggregateVAA03RefF0CtcF'

  // ======== REABSTRACTION THUNK =========

  // CHECK-LABEL: sil shared [transparent] [serializable] [reabstraction_thunk] @_T0Si6shared14ValueAggregateVAA03RefC0CIgygg_SiAcEIgyxx_TR : $@convention(thin) (Int, @owned ValueAggregate, @owned RefAggregate, @guaranteed @noescape @callee_guaranteed (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()) -> ()
  // CHECK: bb0([[TRIVIAL_VAL:%[0-9]+]] : @trivial $Int, [[VALUE_VAL:%[0-9]+]] : @owned $ValueAggregate, [[REF_VAL:%[0-9]+]] : @owned $RefAggregate, [[FUNC:%[0-9]+]] : @guaranteed $@noescape @callee_guaranteed (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()):
  // CHECK: [[BORROW_VALUE_VAL:%[0-9]+]] = begin_borrow [[VALUE_VAL]] : $ValueAggregate
  // CHECK: [[BORROW_REF_VAL:%[0-9]+]] = begin_borrow [[REF_VAL]] : $RefAggregate
  // CHECK: {{%.*}} = apply [[FUNC]]([[TRIVIAL_VAL]], [[BORROW_VALUE_VAL]], [[BORROW_REF_VAL]]) : $@noescape @callee_guaranteed (Int, @guaranteed ValueAggregate, @guaranteed RefAggregate) -> ()
  // CHECK: end_borrow [[BORROW_REF_VAL]] from [[REF_VAL]] : $RefAggregate, $RefAggregate
  // CHECK: end_borrow [[BORROW_VALUE_VAL]] from [[VALUE_VAL]] : $ValueAggregate, $ValueAggregate
  // CHECK: destroy_value [[REF_VAL]] : $RefAggregate
  // CHECK: destroy_value [[VALUE_VAL]] : $ValueAggregate
  // CHECK: } // end sil function '_T0Si6shared14ValueAggregateVAA03RefC0CIgygg_SiAcEIgyxx_TR'
  
  return owned_to_shared_conversion { (trivial : __shared Int, val : __shared ValueAggregate, ref : __shared RefAggregate) in }
}

// CHECK-LABEL: sil hidden @_T06shared0A17_closure_loweringyySi_AA14ValueAggregateVAA03RefE0CtchF : $@convention(thin) (@guaranteed @callee_guaranteed (Int, @owned ValueAggregate, @owned RefAggregate) -> ()) -> ()
func shared_closure_lowering(_ f : __shared (Int, ValueAggregate, RefAggregate) -> Void) {}
