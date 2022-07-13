// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -enable-sil-ownership -emit-module -o %t %S/Inputs/mangling_private_helper.swift
// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership %S/Inputs/mangling_private_helper.swift | %FileCheck %s -check-prefix=CHECK-BASE

// RUN: %target-swift-frontend %s -I %t -emit-silgen -enable-sil-ownership | %FileCheck %s

// RUN: cp %s %t
// RUN: %target-swift-frontend %t/mangling_private.swift -I %t -emit-silgen -enable-sil-ownership | %FileCheck %s

// RUN: cp %s %t/other_name.swift
// RUN: %target-swift-frontend %t/other_name.swift -I %t -emit-silgen -enable-sil-ownership -module-name mangling_private | %FileCheck %s -check-prefix=OTHER-NAME

import mangling_private_helper

// CHECK-LABEL: sil private @_T016mangling_private0B4Func33_A3CCBB841DB59E79A4AD4EE458655068LLSiyF
// OTHER-NAME-LABEL: sil private @_T016mangling_private0B4Func33_CF726049E48876D30EA29D63CF139F1DLLSiyF
private func privateFunc() -> Int {
  return 0
}

public struct PublicStruct {
  // CHECK-LABEL: sil private @_T016mangling_private12PublicStructV0B6Method33_A3CCBB841DB59E79A4AD4EE458655068LLyyFZ
  private static func privateMethod() {}

  // CHECK-LABEL: sil private @_T016mangling_private12PublicStructVACSi1x_tc33_A3CCBB841DB59E79A4AD4EE458655068LlfC
  private init(x: Int) {}
}

public struct InternalStruct {
  // CHECK-LABEL: sil private @_T016mangling_private14InternalStructV0B6Method33_A3CCBB841DB59E79A4AD4EE458655068LLyyFZ
  private static func privateMethod() {}

  // CHECK-LABEL: sil private @_T016mangling_private14InternalStructVACSi1x_tc33_A3CCBB841DB59E79A4AD4EE458655068LlfC
  private init(x: Int) {}
}

private struct PrivateStruct {
  // CHECK-LABEL: sil private @_T016mangling_private13PrivateStruct33_A3CCBB841DB59E79A4AD4EE458655068LLV0B6MethodyyFZ
  private static func privateMethod() {}

  // CHECK-LABEL: sil private @_T016mangling_private13PrivateStruct33_A3CCBB841DB59E79A4AD4EE458655068LLVADSi1x_tcfC
  private init(x: Int) {}

  struct Inner {
    // CHECK-LABEL: sil private @_T016mangling_private13PrivateStruct33_A3CCBB841DB59E79A4AD4EE458655068LLV5InnerV0B6MethodyyFZ
    private static func privateMethod() {}

    // CHECK-LABEL: sil private @_T016mangling_private13PrivateStruct33_A3CCBB841DB59E79A4AD4EE458655068LLV5InnerVAFSi1x_tcfC
    private init(x: Int) {}
  }
}

func localTypes() {
  struct LocalStruct {
    private static func privateMethod() {}
  }
}

extension PublicStruct {
  // CHECK-LABEL: sil private @_T016mangling_private12PublicStructV16extPrivateMethod33_A3CCBB841DB59E79A4AD4EE458655068LLyyF
  private func extPrivateMethod() {}

  // CHECK-LABEL: sil private @_T016mangling_private12PublicStructVACSi3ext_tc33_A3CCBB841DB59E79A4AD4EE458655068LlfC
  private init(ext: Int) {}
}
extension PrivateStruct {
  // CHECK-LABEL: sil private @_T016mangling_private13PrivateStruct33_A3CCBB841DB59E79A4AD4EE458655068LLV03extC6MethodyyF
  private func extPrivateMethod() {}

  // CHECK-LABEL: sil private @_T016mangling_private13PrivateStruct33_A3CCBB841DB59E79A4AD4EE458655068LLVADSi3ext_tcfC
  private init(ext: Int) {}
}

// CHECK-LABEL: sil private @_T016mangling_private10localTypesyyF11LocalStructL_V0B6MethodyyFZ

// CHECK-LABEL: sil_vtable Sub {
class Sub : Base {
  // CHECK-BASE: #Base.privateMethod!1: {{.*}} : _T023mangling_private_helper4BaseC0B6Method33_0E108371B0D5773E608A345AC52C7674LLyyF
  // CHECK-DAG: #Base.privateMethod!1: {{.*}} : _T023mangling_private_helper4BaseC0B6Method33_0E108371B0D5773E608A345AC52C7674LLyyF

  // CHECK-DAG: #Sub.subMethod!1: {{.*}} : _T016mangling_private3SubC9subMethod33_A3CCBB841DB59E79A4AD4EE458655068LLyyF
  private func subMethod() {}
} // CHECK: {{^[}]$}}

