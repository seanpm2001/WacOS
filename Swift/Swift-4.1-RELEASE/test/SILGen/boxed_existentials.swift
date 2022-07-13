// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -enable-sil-ownership -emit-silgen %s | %FileCheck %s
// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -enable-sil-ownership -emit-silgen %s | %FileCheck %s --check-prefix=GUARANTEED

func test_type_lowering(_ x: Error) { }
// CHECK-LABEL: sil hidden @_T018boxed_existentials18test_type_loweringys5Error_pF : $@convention(thin) (@owned Error) -> () {
// CHECK:         destroy_value %0 : $Error

class Document {}

enum ClericalError: Error {
  case MisplacedDocument(Document)

  var _domain: String { return "" }
  var _code: Int { return 0 }
}

func test_concrete_erasure(_ x: ClericalError) -> Error {
  return x
}
// CHECK-LABEL: sil hidden @_T018boxed_existentials21test_concrete_erasures5Error_pAA08ClericalF0OF
// CHECK:       bb0([[ARG:%.*]] : @owned $ClericalError):
// CHECK:         [[EXISTENTIAL:%.*]] = alloc_existential_box $Error, $ClericalError
// CHECK:         [[ADDR:%.*]] = project_existential_box $ClericalError in [[EXISTENTIAL]] : $Error
// CHECK:         [[BORROWED_ARG:%.*]] = begin_borrow [[ARG]]
// CHECK:         [[ARG_COPY:%.*]] = copy_value [[BORROWED_ARG]]
// CHECK:         store [[ARG_COPY]] to [init] [[ADDR]] : $*ClericalError
// CHECK:         end_borrow [[BORROWED_ARG]] from [[ARG]]
// CHECK:         destroy_value [[ARG]]
// CHECK:         return [[EXISTENTIAL]] : $Error

protocol HairType {}

func test_composition_erasure(_ x: HairType & Error) -> Error {
  return x
}
// CHECK-LABEL: sil hidden @_T018boxed_existentials24test_composition_erasures5Error_psAC_AA8HairTypepF
// CHECK:         [[VALUE_ADDR:%.*]] = open_existential_addr immutable_access [[OLD_EXISTENTIAL:%.*]] : $*Error & HairType to $*[[VALUE_TYPE:@opened\(.*\) Error & HairType]]
// CHECK:         [[NEW_EXISTENTIAL:%.*]] = alloc_existential_box $Error, $[[VALUE_TYPE]]
// CHECK:         [[ADDR:%.*]] = project_existential_box $[[VALUE_TYPE]] in [[NEW_EXISTENTIAL]] : $Error
// CHECK:         copy_addr [[VALUE_ADDR]] to [initialization] [[ADDR]]
// CHECK:         destroy_addr [[OLD_EXISTENTIAL]]
// CHECK:         return [[NEW_EXISTENTIAL]]

protocol HairClass: class {}

func test_class_composition_erasure(_ x: HairClass & Error) -> Error {
  return x
}
// CHECK-LABEL: sil hidden @_T018boxed_existentials30test_class_composition_erasures5Error_psAC_AA9HairClasspF
// CHECK:         [[VALUE:%.*]] = open_existential_ref [[OLD_EXISTENTIAL:%.*]] : $Error & HairClass to $[[VALUE_TYPE:@opened\(.*\) Error & HairClass]]
// CHECK:         [[NEW_EXISTENTIAL:%.*]] = alloc_existential_box $Error, $[[VALUE_TYPE]]
// CHECK:         [[ADDR:%.*]] = project_existential_box $[[VALUE_TYPE]] in [[NEW_EXISTENTIAL]] : $Error
// CHECK:         [[COPIED_VALUE:%.*]] = copy_value [[VALUE]]
// CHECK:         store [[COPIED_VALUE]] to [init] [[ADDR]]
// CHECK:         return [[NEW_EXISTENTIAL]]

func test_property(_ x: Error) -> String {
  return x._domain
}
// CHECK-LABEL: sil hidden @_T018boxed_existentials13test_propertySSs5Error_pF
// CHECK: bb0([[ARG:%.*]] : @owned $Error):
// CHECK:         [[BORROWED_ARG:%.*]] = begin_borrow [[ARG]]
// CHECK:         [[VALUE:%.*]] = open_existential_box [[BORROWED_ARG]] : $Error to $*[[VALUE_TYPE:@opened\(.*\) Error]]
// FIXME: Extraneous copy here
// CHECK-NEXT:    [[COPY:%[0-9]+]] = alloc_stack $[[VALUE_TYPE]]
// CHECK-NEXT:    copy_addr [[VALUE]] to [initialization] [[COPY]] : $*[[VALUE_TYPE]]
// CHECK:         [[METHOD:%.*]] = witness_method $[[VALUE_TYPE]], #Error._domain!getter.1
// -- self parameter of witness is @in_guaranteed; no need to copy since
//    value in box is immutable and box is guaranteed
// CHECK:         [[RESULT:%.*]] = apply [[METHOD]]<[[VALUE_TYPE]]>([[COPY]])
// CHECK:         end_borrow [[BORROWED_ARG]] from [[ARG]]
// CHECK:         destroy_value [[ARG]]
// CHECK:         return [[RESULT]]

func test_property_of_lvalue(_ x: Error) -> String {
  var x = x
  return x._domain
}

// CHECK-LABEL: sil hidden @_T018boxed_existentials23test_property_of_lvalueSSs5Error_pF :
// CHECK:       bb0([[ARG:%.*]] : @owned $Error):
// CHECK:         [[VAR:%.*]] = alloc_box ${ var Error }
// CHECK:         [[PVAR:%.*]] = project_box [[VAR]]
// CHECK:         [[BORROWED_ARG:%.*]] = begin_borrow [[ARG]]
// CHECK:         [[ARG_COPY:%.*]] = copy_value [[BORROWED_ARG]] : $Error
// CHECK:         store [[ARG_COPY]] to [init] [[PVAR]]
// CHECK:         [[ACCESS:%.*]] = begin_access [read] [unknown] [[PVAR]] : $*Error
// CHECK:         [[VALUE_BOX:%.*]] = load [copy] [[ACCESS]]
// CHECK:         [[VALUE:%.*]] = open_existential_box [[VALUE_BOX]] : $Error to $*[[VALUE_TYPE:@opened\(.*\) Error]]
// CHECK:         [[COPY:%.*]] = alloc_stack $[[VALUE_TYPE]]
// CHECK:         copy_addr [[VALUE]] to [initialization] [[COPY]]
// CHECK:         [[METHOD:%.*]] = witness_method $[[VALUE_TYPE]], #Error._domain!getter.1
// CHECK:         [[RESULT:%.*]] = apply [[METHOD]]<[[VALUE_TYPE]]>([[COPY]])
// CHECK:         destroy_addr [[COPY]]
// CHECK:         dealloc_stack [[COPY]]
// CHECK:         destroy_value [[VALUE_BOX]]
// CHECK:         destroy_value [[VAR]]
// CHECK:         destroy_value [[ARG]]
// CHECK:         return [[RESULT]]
// CHECK:      } // end sil function '_T018boxed_existentials23test_property_of_lvalueSSs5Error_pF'
extension Error {
  func extensionMethod() { }
}

// CHECK-LABEL: sil hidden @_T018boxed_existentials21test_extension_methodys5Error_pF
func test_extension_method(_ error: Error) {
  // CHECK: bb0([[ARG:%.*]] : @owned $Error):
  // CHECK: [[BORROWED_ARG:%.*]] = begin_borrow [[ARG]]
  // CHECK: [[VALUE:%.*]] = open_existential_box [[BORROWED_ARG]]
  // CHECK: [[METHOD:%.*]] = function_ref
  // CHECK-NOT: copy_addr
  // CHECK: apply [[METHOD]]<{{.*}}>([[VALUE]])
  // CHECK-NOT: destroy_addr [[COPY]]
  // CHECK-NOT: destroy_addr [[VALUE]]
  // CHECK-NOT: destroy_addr [[VALUE]]
  // -- destroy_value the owned argument
  // CHECK: destroy_value %0
  error.extensionMethod()
}

func plusOneError() -> Error { }

// CHECK-LABEL: sil hidden @_T018boxed_existentials31test_open_existential_semanticsys5Error_p_sAC_ptF
// GUARANTEED-LABEL: sil hidden @_T018boxed_existentials31test_open_existential_semanticsys5Error_p_sAC_ptF
// CHECK: bb0([[ARG0:%.*]]: @owned $Error,
// GUARANTEED: bb0([[ARG0:%.*]]: @owned $Error,
func test_open_existential_semantics(_ guaranteed: Error,
                                     _ immediate: Error) {
  var immediate = immediate
  // CHECK: [[IMMEDIATE_BOX:%.*]] = alloc_box ${ var Error }
  // CHECK: [[PB:%.*]] = project_box [[IMMEDIATE_BOX]]
  // GUARANTEED: [[IMMEDIATE_BOX:%.*]] = alloc_box ${ var Error }
  // GUARANTEED: [[PB:%.*]] = project_box [[IMMEDIATE_BOX]]

  // CHECK-NOT: copy_value [[ARG0]]
  // CHECK: [[BORROWED_ARG0:%.*]] = begin_borrow [[ARG0]]
  // CHECK: [[VALUE:%.*]] = open_existential_box [[BORROWED_ARG0]]
  // CHECK: [[METHOD:%.*]] = function_ref
  // CHECK-NOT: copy_addr
  // CHECK: apply [[METHOD]]<{{.*}}>([[VALUE]])
  // CHECK: end_borrow [[BORROWED_ARG0]] from [[ARG0]]
  // CHECK-NOT: destroy_value [[ARG0]]

  // GUARANTEED-NOT: copy_value [[ARG0]]
  // GUARANTEED: [[BORROWED_ARG0:%.*]] = begin_borrow [[ARG0]]
  // GUARANTEED: [[VALUE:%.*]] = open_existential_box [[BORROWED_ARG0]]
  // GUARANTEED: [[METHOD:%.*]] = function_ref
  // GUARANTEED: apply [[METHOD]]<{{.*}}>([[VALUE]])
  // GUARANTEED: end_borrow [[BORROWED_ARG0]] from [[ARG0]]
  // GUARANTEED-NOT: destroy_addr [[VALUE]]
  // GUARANTEED-NOT: destroy_value [[ARG0]]
  guaranteed.extensionMethod()

  // CHECK: [[ACCESS:%.*]] = begin_access [read] [unknown] [[PB]] : $*Error
  // CHECK: [[IMMEDIATE:%.*]] = load [copy] [[ACCESS]]
  // -- need a copy_value to guarantee
  // CHECK: [[VALUE:%.*]] = open_existential_box [[IMMEDIATE]]
  // CHECK: [[METHOD:%.*]] = function_ref
  // CHECK-NOT: copy_addr
  // CHECK: apply [[METHOD]]<{{.*}}>([[VALUE]])
  // -- end the guarantee
  // -- TODO: could in theory do this sooner, after the value's been copied
  //    out.
  // CHECK: destroy_value [[IMMEDIATE]]

  // GUARANTEED: [[ACCESS:%.*]] = begin_access [read] [unknown] [[PB]] : $*Error
  // GUARANTEED: [[IMMEDIATE:%.*]] = load [copy] [[ACCESS]]
  // -- need a copy_value to guarantee
  // GUARANTEED: [[VALUE:%.*]] = open_existential_box [[IMMEDIATE]]
  // GUARANTEED: [[METHOD:%.*]] = function_ref
  // GUARANTEED: apply [[METHOD]]<{{.*}}>([[VALUE]])
  // GUARANTEED-NOT: destroy_addr [[VALUE]]
  // -- end the guarantee
  // GUARANTEED: destroy_value [[IMMEDIATE]]
  immediate.extensionMethod()

  // CHECK: [[F:%.*]] = function_ref {{.*}}plusOneError
  // CHECK: [[PLUS_ONE:%.*]] = apply [[F]]()
  // CHECK: [[VALUE:%.*]] = open_existential_box [[PLUS_ONE]]
  // CHECK: [[METHOD:%.*]] = function_ref
  // CHECK-NOT: copy_addr
  // CHECK: apply [[METHOD]]<{{.*}}>([[VALUE]])
  // CHECK: destroy_value [[PLUS_ONE]]

  // GUARANTEED: [[F:%.*]] = function_ref {{.*}}plusOneError
  // GUARANTEED: [[PLUS_ONE:%.*]] = apply [[F]]()
  // GUARANTEED: [[VALUE:%.*]] = open_existential_box [[PLUS_ONE]]
  // GUARANTEED: [[METHOD:%.*]] = function_ref
  // GUARANTEED: apply [[METHOD]]<{{.*}}>([[VALUE]])
  // GUARANTEED-NOT: destroy_addr [[VALUE]]
  // GUARANTEED: destroy_value [[PLUS_ONE]]
  plusOneError().extensionMethod()
}

// CHECK-LABEL: sil hidden @_T018boxed_existentials14erasure_to_anyyps5Error_p_sAC_ptF
// CHECK:       bb0([[OUT:%.*]] : @trivial $*Any, [[GUAR:%.*]] : @owned $Error,
func erasure_to_any(_ guaranteed: Error, _ immediate: Error) -> Any {
  var immediate = immediate
  // CHECK:       [[IMMEDIATE_BOX:%.*]] = alloc_box ${ var Error }
  // CHECK:       [[PB:%.*]] = project_box [[IMMEDIATE_BOX]]
  if true {
    // CHECK-NOT: copy_value [[GUAR]]
    // CHECK:     [[FROM_VALUE:%.*]] = open_existential_box [[GUAR:%.*]]
    // CHECK:     [[TO_VALUE:%.*]] = init_existential_addr [[OUT]]
    // CHECK:     copy_addr [[FROM_VALUE]] to [initialization] [[TO_VALUE]]
    // CHECK-NOT: destroy_value [[GUAR]]
    return guaranteed
  } else if true {
    // CHECK:     [[ACCESS:%.*]] = begin_access [read] [unknown] [[PB]]
    // CHECK:     [[IMMEDIATE:%.*]] = load [copy] [[ACCESS]]
    // CHECK:     [[FROM_VALUE:%.*]] = open_existential_box [[IMMEDIATE]]
    // CHECK:     [[TO_VALUE:%.*]] = init_existential_addr [[OUT]]
    // CHECK:     copy_addr [[FROM_VALUE]] to [initialization] [[TO_VALUE]]
    // CHECK:     destroy_value [[IMMEDIATE]]
    return immediate
  } else if true {
    // CHECK:     function_ref boxed_existentials.plusOneError
    // CHECK:     [[PLUS_ONE:%.*]] = apply
    // CHECK:     [[FROM_VALUE:%.*]] = open_existential_box [[PLUS_ONE]]
    // CHECK:     [[TO_VALUE:%.*]] = init_existential_addr [[OUT]]
    // CHECK:     copy_addr [[FROM_VALUE]] to [initialization] [[TO_VALUE]]
    // CHECK:     destroy_value [[PLUS_ONE]]

    return plusOneError()
  }
}
