// RUN: %target-swift-emit-silgen %s | %FileCheck %s

// CHECK-LABEL: sil hidden [ossa] @$s16generic_literals0A14IntegerLiteral1xyx_ts013ExpressibleBycD0RzlF : $@convention(thin) <T where T : ExpressibleByIntegerLiteral> (@in_guaranteed T) -> () {
func genericIntegerLiteral<T : ExpressibleByIntegerLiteral>(x: T) {
  var x = x
  // CHECK: [[INTLIT:%.*]] = integer_literal $Builtin.IntLiteral, 17
  // CHECK: [[LITMETA:%.*]] = metatype $@thick T.IntegerLiteralType.Type
  // CHECK: [[LITVAR:%.*]] = alloc_stack $T.IntegerLiteralType
  // CHECK: [[BUILTINCONV:%.*]] = witness_method $T.IntegerLiteralType, #_ExpressibleByBuiltinIntegerLiteral.init!allocator
  // CHECK: [[LIT:%.*]] = apply [[BUILTINCONV]]<T.IntegerLiteralType>([[LITVAR]], [[INTLIT]], [[LITMETA]]) : $@convention(witness_method: _ExpressibleByBuiltinIntegerLiteral) <τ_0_0 where τ_0_0 : _ExpressibleByBuiltinIntegerLiteral> (Builtin.IntLiteral, @thick τ_0_0.Type) -> @out τ_0_0
  // CHECK: [[TMETA:%.*]] = metatype $@thick T.Type
  // CHECK: [[ADDR:%.*]] = alloc_stack $T
  // CHECK: [[TCONV:%.*]] = witness_method $T, #ExpressibleByIntegerLiteral.init!allocator
  // CHECK: apply [[TCONV]]<T>([[ADDR]], [[LITVAR]], [[TMETA]]) : $@convention(witness_method: ExpressibleByIntegerLiteral) <τ_0_0 where τ_0_0 : ExpressibleByIntegerLiteral> (@in τ_0_0.IntegerLiteralType, @thick τ_0_0.Type) -> @out τ_0_0

  x = 17
}

// CHECK-LABEL: sil hidden [ossa] @$s16generic_literals0A15FloatingLiteral1xyx_ts018ExpressibleByFloatD0RzlF : $@convention(thin) <T where T : ExpressibleByFloatLiteral> (@in_guaranteed T) -> () {
func genericFloatingLiteral<T : ExpressibleByFloatLiteral>(x: T) {
  var x = x
  // CHECK: [[LIT_VALUE:%.*]] = float_literal $Builtin.FPIEEE{{64|80}}, {{0x4004000000000000|0x4000A000000000000000}}
  // CHECK: [[TFLT_META:%.*]] = metatype $@thick T.FloatLiteralType.Type
  // CHECK: [[FLT_VAL:%.*]] = alloc_stack $T.FloatLiteralType
  // CHECK: [[BUILTIN_CONV:%.*]] = witness_method $T.FloatLiteralType, #_ExpressibleByBuiltinFloatLiteral.init!allocator
  // CHECK: apply [[BUILTIN_CONV]]<T.FloatLiteralType>([[FLT_VAL]], [[LIT_VALUE]], [[TFLT_META]]) : $@convention(witness_method: _ExpressibleByBuiltinFloatLiteral) <τ_0_0 where τ_0_0 : _ExpressibleByBuiltinFloatLiteral> (Builtin.FPIEEE{{64|80}}, @thick τ_0_0.Type) -> @out τ_0_0
  // CHECK: [[TMETA:%.*]] = metatype $@thick T.Type
  // CHECK: [[TVAL:%.*]] = alloc_stack $T
  // CHECK: [[CONV:%.*]] = witness_method $T, #ExpressibleByFloatLiteral.init!allocator
  // CHECK: apply [[CONV]]<T>([[TVAL]], [[FLT_VAL]], [[TMETA]]) : $@convention(witness_method: ExpressibleByFloatLiteral) <τ_0_0 where τ_0_0 : ExpressibleByFloatLiteral> (@in τ_0_0.FloatLiteralType, @thick τ_0_0.Type) -> @out τ_0_0

  x = 2.5
}

// CHECK-LABEL: sil hidden [ossa] @$s16generic_literals0A13StringLiteral1xyx_ts013ExpressibleBycD0RzlF : $@convention(thin) <T where T : ExpressibleByStringLiteral> (@in_guaranteed T) -> () {

func genericStringLiteral<T : ExpressibleByStringLiteral>(x: T) {
  var x = x
  // CHECK: [[LIT_VALUE:%.*]] = string_literal utf8 "hello"
  // CHECK: [[TSTR_META:%.*]] = metatype $@thick T.StringLiteralType.Type
  // CHECK: [[STR_VAL:%.*]] = alloc_stack $T.StringLiteralType
  // CHECK: [[BUILTIN_CONV:%.*]] = witness_method $T.StringLiteralType, #_ExpressibleByBuiltinStringLiteral.init!allocator
  // CHECK: apply [[BUILTIN_CONV]]<T.StringLiteralType>([[STR_VAL]], [[LIT_VALUE]], {{.*}}, [[TSTR_META]]) : $@convention(witness_method: _ExpressibleByBuiltinStringLiteral) <τ_0_0 where τ_0_0 : _ExpressibleByBuiltinStringLiteral> (Builtin.RawPointer, Builtin.Word, Builtin.Int1, @thick τ_0_0.Type) -> @out τ_0_0
  // CHECK: [[TMETA:%.*]] = metatype $@thick T.Type
  // CHECK: [[TVAL:%.*]] = alloc_stack $T
  // CHECK: [[CONV:%.*]] = witness_method $T, #ExpressibleByStringLiteral.init!allocator
  // CHECK: apply [[CONV]]<T>([[TVAL]], [[STR_VAL]], [[TMETA]]) : $@convention(witness_method: ExpressibleByStringLiteral) <τ_0_0 where τ_0_0 : ExpressibleByStringLiteral> (@in τ_0_0.StringLiteralType, @thick τ_0_0.Type) -> @out τ_0_0

  x = "hello"
}
