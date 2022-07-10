// RUN: rm -rf %t
// RUN: mkdir %t
// RUN: %target-swift-frontend -emit-module -o %t %S/Inputs/def_func.swift
// RUN: llvm-bcanalyzer %t/def_func.swiftmodule | %FileCheck %s
// RUN: %target-swift-frontend -emit-silgen -I %t %s | %FileCheck %s -check-prefix=SIL

// CHECK-NOT: FALL_BACK_TO_TRANSLATION_UNIT
// CHECK-NOT: UnknownCode

import def_func

func useEq<T: EqualOperator>(_ x: T, y: T) -> Bool {
  return x == y
}

// SIL: sil @main
// SIL:   [[RAW:%.+]] = global_addr @_Tv8function3rawSi : $*Int
// SIL:   [[ZERO:%.+]] = function_ref @_TF8def_func7getZeroFT_Si : $@convention(thin) () -> Int
// SIL:   [[RESULT:%.+]] = apply [[ZERO]]() : $@convention(thin) () -> Int
// SIL:   store [[RESULT]] to [[RAW]] : $*Int
var raw = getZero()

// Check that 'raw' is an Int
var cooked : Int = raw


// SIL:   [[GET_INPUT:%.+]] = function_ref @_TF8def_func8getInputFT1xSi_Si : $@convention(thin) (Int) -> Int
// SIL:   {{%.+}} = apply [[GET_INPUT]]({{%.+}}) : $@convention(thin) (Int) -> Int
var raw2 = getInput(x: raw)

// SIL:   [[GET_SECOND:%.+]] = function_ref @_TF8def_func9getSecondFTSi1ySi_Si : $@convention(thin) (Int, Int) -> Int
// SIL:   {{%.+}} = apply [[GET_SECOND]]({{%.+}}, {{%.+}}) : $@convention(thin) (Int, Int) -> Int
var raw3 = getSecond(raw, y: raw2)

// SIL:   [[USE_NESTED:%.+]] = function_ref @_TF8def_func9useNestedFTT1xSi1ySi_1nSi_T_ : $@convention(thin) (Int, Int, Int) -> ()
// SIL:   {{%.+}} = apply [[USE_NESTED]]({{%.+}}, {{%.+}}, {{%.+}}) : $@convention(thin) (Int, Int, Int) -> ()
useNested((raw, raw2), n: raw3)

// SIL:   [[VARIADIC:%.+]] = function_ref @_TF8def_func8variadicFt1xSdGSaSi__T_ : $@convention(thin) (Double, @owned Array<Int>) -> ()
// SIL:   [[VA_SIZE:%.+]] = integer_literal $Builtin.Word, 2
// SIL:   {{%.+}} = apply {{%.*}}<Int>([[VA_SIZE]])
// SIL:   {{%.+}} = apply [[VARIADIC]]({{%.+}}, {{%.+}}) : $@convention(thin) (Double, @owned Array<Int>) -> ()
variadic(x: 2.5, 4, 5)

// SIL:   [[VARIADIC:%.+]] = function_ref @_TF8def_func9variadic2FTGSaSi_1xSd_T_ : $@convention(thin) (@owned Array<Int>, Double) -> ()
variadic2(1, 2, 3, x: 5.0)

// SIL:   [[SLICE:%.+]] = function_ref @_TF8def_func5sliceFT1xGSaSi__T_ : $@convention(thin) (@owned Array<Int>) -> ()
// SIL:   [[SLICE_SIZE:%.+]] = integer_literal $Builtin.Word, 3
// SIL:   {{%.+}} = apply {{%.*}}<Int>([[SLICE_SIZE]])
// SIL:   {{%.+}} = apply [[SLICE]]({{%.+}}) : $@convention(thin) (@owned Array<Int>) -> ()
slice(x: [2, 4, 5])

optional(x: .some(23))
optional(x: .none)


// SIL:   [[MAKE_PAIR:%.+]] = function_ref @_TF8def_func8makePair{{.*}} : $@convention(thin) <τ_0_0, τ_0_1> (@in τ_0_0, @in τ_0_1) -> (@out τ_0_0, @out τ_0_1)
// SIL:   {{%.+}} = apply [[MAKE_PAIR]]<Int, Double>({{%.+}}, {{%.+}})

var pair : (Int, Double) = makePair(a: 1, b: 2.5)

// SIL:   [[DIFFERENT_A:%.+]] = function_ref @_TF8def_func9different{{.*}} : $@convention(thin) <τ_0_0 where τ_0_0 : Equatable> (@in τ_0_0, @in τ_0_0) -> Bool
// SIL:   [[DIFFERENT_B:%.+]] = function_ref @_TF8def_func9different{{.*}} : $@convention(thin) <τ_0_0 where τ_0_0 : Equatable> (@in τ_0_0, @in τ_0_0) -> Bool

_ = different(a: 1, b: 2)
_ = different(a: false, b: false)

// SIL:   [[DIFFERENT2_A:%.+]] = function_ref @_TF8def_func10different2{{.*}} : $@convention(thin) <τ_0_0 where τ_0_0 : Equatable> (@in τ_0_0, @in τ_0_0) -> Bool
// SIL:   [[DIFFERENT2_B:%.+]] = function_ref @_TF8def_func10different2{{.*}} : $@convention(thin) <τ_0_0 where τ_0_0 : Equatable> (@in τ_0_0, @in τ_0_0) -> Bool
_ = different2(a: 1, b: 2)
_ = different2(a: false, b: false)


struct IntWrapper1 : Wrapped {
  typealias Value = Int
  func getValue() -> Int { return 1 }
}
struct IntWrapper2 : Wrapped {
  typealias Value = Int
  func getValue() -> Int { return 2 }
}

// SIL:   [[DIFFERENT_WRAPPED:%.+]] = function_ref @_TF8def_func16differentWrapped{{.*}} : $@convention(thin) <τ_0_0, τ_0_1 where τ_0_0 : Wrapped, τ_0_1 : Wrapped, τ_0_0.Value : Equatable, τ_0_0.Value == τ_0_1.Value> (@in τ_0_0, @in τ_0_1) -> Bool

_ = differentWrapped(a: IntWrapper1(), b: IntWrapper2())


// SIL:   {{%.+}} = function_ref @_TF8def_func10overloadedFT1xSi_T_ : $@convention(thin) (Int) -> ()
// SIL:   {{%.+}} = function_ref @_TF8def_func10overloadedFT1xSb_T_ : $@convention(thin) (Bool) -> ()

overloaded(x: 1)
overloaded(x: false)


// SIL:   {{%.+}} = function_ref @primitive : $@convention(thin) () -> ()
primitive()


if raw == 5 {
  testNoReturnAttr()
  testNoReturnAttrPoly(x: 5)
}
// SIL: {{%.+}} = function_ref @_TF8def_func16testNoReturnAttrFT_Os5Never : $@convention(thin) () -> Never
// SIL: {{%.+}} = function_ref @_TF8def_func20testNoReturnAttrPoly{{.*}} : $@convention(thin) <τ_0_0> (@in τ_0_0) -> Never


// SIL: sil @_TF8def_func16testNoReturnAttrFT_Os5Never : $@convention(thin) () -> Never
// SIL: sil @_TF8def_func20testNoReturnAttrPoly{{.*}} : $@convention(thin) <τ_0_0> (@in τ_0_0) -> Never

do {
  try throws1()
  _ = try throws2(1)
} catch _ {}
// SIL: sil @_TF8def_func7throws1FzT_T_ : $@convention(thin) () -> @error Error
// SIL: sil @_TF8def_func7throws2{{.*}} : $@convention(thin) <τ_0_0> (@in τ_0_0) -> (@out τ_0_0, @error Error)

// LLVM: }

