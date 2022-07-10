// RUN: %target-swift-frontend -emit-silgen %s | %FileCheck %s

// CHECK-LABEL: sil hidden @_TF15optional_lvalue22assign_optional_lvalueFTRGSqSi_Si_T_
// CHECK:         [[SHADOW:%.*]] = alloc_box $Optional<Int>
// CHECK:         [[PB:%.*]] = project_box [[SHADOW]]
// CHECK:         [[PRECOND:%.*]] = function_ref @_TFs30_diagnoseUnexpectedNilOptional
// CHECK:         apply [[PRECOND]](
// CHECK:         [[PAYLOAD:%.*]] = unchecked_take_enum_data_addr [[PB]] : $*Optional<Int>, #Optional.some!enumelt.1
// CHECK:         assign {{%.*}} to [[PAYLOAD]]
func assign_optional_lvalue(_ x: inout Int?, _ y: Int) {
  x! = y
}

// CHECK-LABEL: sil hidden @_TF15optional_lvalue17assign_iuo_lvalueFTRGSQSi_Si_T_
// CHECK:         [[SHADOW:%.*]] = alloc_box $ImplicitlyUnwrappedOptional<Int>
// CHECK:         [[PB:%.*]] = project_box [[SHADOW]]
// CHECK:         [[PRECOND:%.*]] = function_ref @_TFs30_diagnoseUnexpectedNilOptional
// CHECK:         apply [[PRECOND]](
// CHECK:         [[PAYLOAD:%.*]] = unchecked_take_enum_data_addr [[PB]] : $*ImplicitlyUnwrappedOptional<Int>, #ImplicitlyUnwrappedOptional.some!enumelt.1
// CHECK:         assign {{%.*}} to [[PAYLOAD]]
func assign_iuo_lvalue(_ x: inout Int!, _ y: Int) {
  x! = y
}

struct S {
  var x: Int

  var computed: Int {
    get {}
    set {}
  }
}

// CHECK-LABEL: sil hidden @_TF15optional_lvalue26assign_iuo_lvalue_implicitFTRGSQVS_1S_Si_T_
// CHECK:         [[SHADOW:%.*]] = alloc_box
// CHECK:         [[PB:%.*]] = project_box [[SHADOW]]
// CHECK:         [[SOME:%.*]] = unchecked_take_enum_data_addr [[PB]]
// CHECK:         [[X:%.*]] = struct_element_addr [[SOME]]
func assign_iuo_lvalue_implicit(_ s: inout S!, _ y: Int) {
  s.x = y
}

// CHECK-LABEL: sil hidden @_TF15optional_lvalue35assign_optional_lvalue_reabstractedFTRGSqFSiSi_FSiSi_T_
// CHECK:         [[REABSTRACT:%.*]] = function_ref @_TTRXFo_dSi_dSi_XFo_iSi_iSi_
// CHECK:         [[REABSTRACTED:%.*]] = partial_apply [[REABSTRACT]]
// CHECK:         assign [[REABSTRACTED]] to {{%.*}} : $*@callee_owned (@in Int) -> @out Int
func assign_optional_lvalue_reabstracted(_ x: inout ((Int) -> Int)?,
                                         _ y: @escaping (Int) -> Int) {
  x! = y
}

// CHECK-LABEL: sil hidden @_TF15optional_lvalue31assign_optional_lvalue_computedFTRGSqVS_1S_Si_Si
// CHECK:         function_ref @_TFV15optional_lvalue1Ss8computedSi
// CHECK:         function_ref @_TFV15optional_lvalue1Sg8computedSi
func assign_optional_lvalue_computed(_ x: inout S?, _ y: Int) -> Int {
  x!.computed = y
  return x!.computed
}

func generate_int() -> Int { return 0 }

// CHECK-LABEL: sil hidden @_TF15optional_lvalue28assign_bound_optional_lvalueFRGSqSi_T_
// CHECK:         select_enum_addr
// CHECK:         cond_br {{%.*}}, [[SOME:bb[0-9]+]], [[NONE:bb[0-9]+]]
// CHECK:       [[SOME]]:
// CHECK:         [[PAYLOAD:%.*]] = unchecked_take_enum_data_addr
// CHECK:         [[FN:%.*]] = function_ref
// CHECK:         [[T0:%.*]] = apply [[FN]]()
// CHECK:         assign [[T0]] to [[PAYLOAD]]
func assign_bound_optional_lvalue(_ x: inout Int?) {
  x? = generate_int()
}
