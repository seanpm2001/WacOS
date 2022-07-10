// RUN: rm -rf %t
// RUN: mkdir %t
// RUN: %target-swift-frontend -emit-module -sil-serialize-all -o %t %S/Inputs/def_always_inline.swift
// RUN: llvm-bcanalyzer %t/def_always_inline.swiftmodule | %FileCheck %s
// RUN: %target-swift-frontend -emit-silgen -sil-link-all -I %t %s | %FileCheck %s -check-prefix=SIL

// CHECK-NOT: UnknownCode

import def_always_inline

// SIL-LABEL: sil @main
// SIL: [[RAW:%.+]] = global_addr @_Tv13always_inline3rawSb : $*Bool
// SIL: [[FUNC:%.+]] = function_ref @_TF17def_always_inline16testAlwaysInlineFT1xSb_Sb : $@convention(thin) (Bool) -> Bool
// SIL: [[RESULT:%.+]] = apply [[FUNC]]({{%.+}}) : $@convention(thin) (Bool) -> Bool
// SIL: store [[RESULT]] to [[RAW]] : $*Bool
var raw = testAlwaysInline(x: false)

// SIL: [[FUNC2:%.+]] = function_ref @_TFV17def_always_inline22AlwaysInlineInitStructCfT1xSb_S0_ : $@convention(method) (Bool, @thin AlwaysInlineInitStruct.Type) -> AlwaysInlineInitStruct
// SIL: apply [[FUNC2]]({{%.+}}, {{%.+}}) : $@convention(method) (Bool, @thin AlwaysInlineInitStruct.Type) -> AlwaysInlineInitStruct

var a = AlwaysInlineInitStruct(x: false)

// SIL-LABEL: [always_inline] @_TF17def_always_inline16testAlwaysInlineFT1xSb_Sb : $@convention(thin) (Bool) -> Bool

// SIL-LABEL: sil public_external [fragile] [always_inline] @_TFV17def_always_inline22AlwaysInlineInitStructCfT1xSb_S0_ : $@convention(method) (Bool, @thin AlwaysInlineInitStruct.Type) -> AlwaysInlineInitStruct {

