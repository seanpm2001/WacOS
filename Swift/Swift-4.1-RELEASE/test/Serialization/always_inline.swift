// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -emit-module -o %t %S/Inputs/def_always_inline.swift
// RUN: llvm-bcanalyzer %t/def_always_inline.swiftmodule | %FileCheck %s
// RUN: %target-swift-frontend -emit-silgen -sil-link-all -I %t %s | %FileCheck %s -check-prefix=SIL

// CHECK-NOT: UnknownCode

import def_always_inline

// SIL-LABEL: sil @main
// SIL: [[RAW:%.+]] = global_addr @_T013always_inline3rawSbvp : $*Bool
// SIL: [[FUNC:%.+]] = function_ref @_T017def_always_inline16testAlwaysInlineS2b1x_tF : $@convention(thin) (Bool) -> Bool
// SIL: [[RESULT:%.+]] = apply [[FUNC]]({{%.+}}) : $@convention(thin) (Bool) -> Bool
// SIL: store [[RESULT]] to [trivial] [[RAW]] : $*Bool
var raw = testAlwaysInline(x: false)

// SIL: [[FUNC2:%.+]] = function_ref @_T017def_always_inline22AlwaysInlineInitStructVACSb1x_tcfC : $@convention(method) (Bool, @thin AlwaysInlineInitStruct.Type) -> AlwaysInlineInitStruct
// SIL: apply [[FUNC2]]({{%.+}}, {{%.+}}) : $@convention(method) (Bool, @thin AlwaysInlineInitStruct.Type) -> AlwaysInlineInitStruct

var a = AlwaysInlineInitStruct(x: false)

// SIL-LABEL: [always_inline] @_T017def_always_inline16testAlwaysInlineS2b1x_tF : $@convention(thin) (Bool) -> Bool

// SIL-LABEL: sil public_external [serialized] [always_inline] @_T017def_always_inline22AlwaysInlineInitStructVACSb1x_tcfC : $@convention(method) (Bool, @thin AlwaysInlineInitStruct.Type) -> AlwaysInlineInitStruct {

