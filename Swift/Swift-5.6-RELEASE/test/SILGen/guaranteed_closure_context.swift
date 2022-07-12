// RUN: %target-swift-emit-silgen -parse-as-library  %s | %FileCheck %s

func use<T>(_: T) {}

func escape(_ f: () -> ()) {}

protocol P {}
class C: P {}
struct S {}

// CHECK-LABEL: sil hidden [ossa] @$s26guaranteed_closure_context0A9_capturesyyF
func guaranteed_captures() {
  // CHECK: [[MUTABLE_TRIVIAL_BOX:%.*]] = alloc_box ${ var S }
  var mutableTrivial = S()
  // CHECK: [[MUTABLE_RETAINABLE_BOX:%.*]] = alloc_box ${ var C }
  var mutableRetainable = C()
  // CHECK: [[MUTABLE_ADDRESS_ONLY_BOX:%.*]] = alloc_box ${ var P }
  var mutableAddressOnly: P = C()

  // CHECK: [[IMMUTABLE_TRIVIAL:%.*]] = apply {{.*}} -> S
  let immutableTrivial = S()
  // CHECK: [[IMMUTABLE_RETAINABLE:%.*]] = apply {{.*}} -> @owned C
  // CHECK: [[B_IMMUTABLE_RETAINABLE:%.*]] = begin_borrow [lexical] [[IMMUTABLE_RETAINABLE]] : $C
  let immutableRetainable = C()
  // CHECK: [[IMMUTABLE_ADDRESS_ONLY:%.*]] = alloc_stack [lexical] $P
  let immutableAddressOnly: P = C()

  func captureEverything() {
    use((mutableTrivial, mutableRetainable, mutableAddressOnly,
         immutableTrivial, immutableRetainable, immutableAddressOnly))
  }

  // CHECK-NOT: copy_value [[MUTABLE_TRIVIAL_BOX]]
  // CHECK-NOT: copy_value [[MUTABLE_RETAINABLE_BOX]]
  // CHECK-NOT: copy_value [[MUTABLE_ADDRESS_ONLY_BOX]]
  // CHECK-NOT: copy_value [[IMMUTABLE_RETAINABLE]]

  // CHECK:     [[B_MUTABLE_TRIVIAL_BOX:%.*]] = begin_borrow [[MUTABLE_TRIVIAL_BOX]] : ${ var S }
  // CHECK:     [[B_MUTABLE_RETAINABLE_BOX:%.*]] = begin_borrow [[MUTABLE_RETAINABLE_BOX]] : ${ var C }
  // CHECK:     [[B_MUTABLE_ADDRESS_ONLY_BOX:%.*]] = begin_borrow [[MUTABLE_ADDRESS_ONLY_BOX]] : ${ var P }

  // CHECK: [[FN:%.*]] = function_ref [[FN_NAME:@\$s26guaranteed_closure_context0A9_capturesyyF17captureEverythingL_yyF]]
  // CHECK: apply [[FN]]([[B_MUTABLE_TRIVIAL_BOX]], [[B_MUTABLE_RETAINABLE_BOX]], [[B_MUTABLE_ADDRESS_ONLY_BOX]], [[IMMUTABLE_TRIVIAL]], [[B_IMMUTABLE_RETAINABLE]], [[IMMUTABLE_ADDRESS_ONLY]])
  captureEverything()

  // CHECK-NOT: copy_value [[MUTABLE_TRIVIAL_BOX]]
  // CHECK-NOT: copy_value [[MUTABLE_RETAINABLE_BOX]]
  // CHECK-NOT: copy_value [[MUTABLE_ADDRESS_ONLY_BOX]]
  // CHECK-NOT: copy_value [[IMMUTABLE_RETAINABLE]]

  // -- partial_apply still takes ownership of its arguments.
  // CHECK: [[FN:%.*]] = function_ref [[FN_NAME]]
  // CHECK: [[MUTABLE_TRIVIAL_BOX_COPY:%.*]] = copy_value [[MUTABLE_TRIVIAL_BOX]]
  // CHECK: [[MUTABLE_RETAINABLE_BOX_COPY:%.*]] = copy_value [[MUTABLE_RETAINABLE_BOX]]
  // CHECK: [[MUTABLE_ADDRESS_ONLY_BOX_COPY:%.*]] = copy_value [[MUTABLE_ADDRESS_ONLY_BOX]]
  // CHECK: [[IMMUTABLE_RETAINABLE_COPY:%.*]] = copy_value [[B_IMMUTABLE_RETAINABLE]]
  // CHECK: [[IMMUTABLE_ADDRESS:%.*]] = alloc_stack $P
  // CHECK: [[CLOSURE:%.*]] = partial_apply {{.*}}([[MUTABLE_TRIVIAL_BOX_COPY]], [[MUTABLE_RETAINABLE_BOX_COPY]], [[MUTABLE_ADDRESS_ONLY_BOX_COPY]], [[IMMUTABLE_TRIVIAL]], [[IMMUTABLE_RETAINABLE_COPY]], [[IMMUTABLE_ADDRESS]])
  // CHECK: [[CONVERT:%.*]] = convert_escape_to_noescape [not_guaranteed] [[CLOSURE]]
  // CHECK: apply {{.*}}[[CONVERT]]

  // CHECK-NOT: copy_value [[MUTABLE_TRIVIAL_BOX]]
  // CHECK-NOT: copy_value [[MUTABLE_RETAINABLE_BOX]]
  // CHECK-NOT: copy_value [[MUTABLE_ADDRESS_ONLY_BOX]]
  // CHECK-NOT: copy_value [[IMMUTABLE_RETAINABLE]]

  escape(captureEverything)
}

// CHECK: sil private [ossa] [[FN_NAME]] : $@convention(thin) (@guaranteed { var S }, @guaranteed { var C }, @guaranteed { var P }, S, @guaranteed C, @in_guaranteed P)
