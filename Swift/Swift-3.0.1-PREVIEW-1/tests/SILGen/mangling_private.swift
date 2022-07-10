// RUN: rm -rf %t && mkdir %t
// RUN: %target-swift-frontend -emit-module -o %t %S/Inputs/mangling_private_helper.swift
// RUN: %target-swift-frontend -emit-silgen %S/Inputs/mangling_private_helper.swift | %FileCheck %s -check-prefix=CHECK-BASE

// RUN: %target-swift-frontend %s -I %t -emit-silgen | %FileCheck %s

// RUN: cp %s %t
// RUN: %target-swift-frontend %t/mangling_private.swift -I %t -emit-silgen | %FileCheck %s

// RUN: cp %s %t/other_name.swift
// RUN: %target-swift-frontend %t/other_name.swift -I %t -emit-silgen -module-name mangling_private | %FileCheck %s -check-prefix=OTHER-NAME

import mangling_private_helper

// CHECK-LABEL: sil private @_TF16mangling_privateP33_A3CCBB841DB59E79A4AD4EE45865506811privateFuncFT_Si
// OTHER-NAME-LABEL: sil private @_TF16mangling_privateP33_CF726049E48876D30EA29D63CF139F1D11privateFuncFT_Si
private func privateFunc() -> Int {
  return 0
}

public struct PublicStruct {
  // CHECK-LABEL: sil private @_TZFV16mangling_private12PublicStructP33_A3CCBB841DB59E79A4AD4EE45865506813privateMethodfT_T_
  private static func privateMethod() {}
}

public struct InternalStruct {
  // CHECK-LABEL: sil private @_TZFV16mangling_private14InternalStructP33_A3CCBB841DB59E79A4AD4EE45865506813privateMethodfT_T_
  private static func privateMethod() {}
}

private struct PrivateStruct {
  // CHECK-LABEL: sil private @_TZFV16mangling_privateP33_A3CCBB841DB59E79A4AD4EE45865506813PrivateStruct13privateMethodfT_T_
  private static func privateMethod() {}

  struct Inner {
    // CHECK-LABEL: sil private @_TZFVV16mangling_privateP33_A3CCBB841DB59E79A4AD4EE45865506813PrivateStruct5Inner13privateMethodfT_T_
    private static func privateMethod() {}
  }
}

func localTypes() {
  struct LocalStruct {
    // CHECK-LABEL: sil shared @_TZFVF16mangling_private10localTypesFT_T_L_11LocalStruct13privateMethodfT_T_
    private static func privateMethod() {}
  }
}

extension PublicStruct {
  // CHECK-LABEL: sil private @_TFV16mangling_private12PublicStructP33_A3CCBB841DB59E79A4AD4EE45865506816extPrivateMethodfT_T_
  private func extPrivateMethod() {}
}
extension PrivateStruct {
  // CHECK-LABEL: sil private @_TFV16mangling_privateP33_A3CCBB841DB59E79A4AD4EE45865506813PrivateStruct16extPrivateMethodfT_T_
  private func extPrivateMethod() {}
}


// CHECK-LABEL: sil_vtable Sub {
class Sub : Base {
  // CHECK-BASE: #Base.privateMethod!1: _TFC23mangling_private_helper4BaseP33_0E108371B0D5773E608A345AC52C767413privateMethodfT_T_
  // CHECK-DAG: #Base.privateMethod!1: _TFC23mangling_private_helper4BaseP33_0E108371B0D5773E608A345AC52C767413privateMethodfT_T_

  // CHECK-DAG: #Sub.subMethod!1: _TFC16mangling_private3SubP33_A3CCBB841DB59E79A4AD4EE4586550689subMethodfT_T_
  private func subMethod() {}
} // CHECK: {{^[}]$}}

