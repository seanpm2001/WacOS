// RUN: %target-swift-emit-silgen -primary-file %s %S/Inputs/struct_with_initializer.swift -module-name extensions_multifile | %FileCheck %s --check-prefix=FRAGILE --check-prefix=CHECK
// RUN: %target-swift-emit-silgen -primary-file %s %S/Inputs/struct_with_initializer.swift -module-name extensions_multifile -enable-library-evolution | %FileCheck %s --check-prefix=RESILIENT --check-prefix=CHECK

// CHECK-LABEL: sil hidden [ossa] @$s20extensions_multifile12HasInitValueV1zACSi_tcfC : $@convention(method) (Int, @thin HasInitValue.Type) -> @owned HasInitValue {
// CHECK: function_ref @$s20extensions_multifile12HasInitValueV1xSivpfi : $@convention(thin) () -> Int

// CHECK-LABEL: sil hidden_external [transparent] @$s20extensions_multifile12HasInitValueV1xSivpfi : $@convention(thin) () -> Int

extension HasInitValue {
  init(z: Int) {}
}

// CHECK-LABEL: sil hidden_external [transparent] @$s20extensions_multifile19HasPrivateInitValueV1x33_0A683B047698EED319CF48214D7F519DLLSivpfi : $@convention(thin) () -> Int

extension HasPrivateInitValue {
  init(z: Int) {}
}

// FRAGILE-LABEL: sil [transparent] @$s20extensions_multifile24PublicStructHasInitValueV1xSivpfi : $@convention(thin) () -> Int

// RESILIENT-LABEL: sil hidden_external [transparent] @$s20extensions_multifile24PublicStructHasInitValueV1xSivpfi : $@convention(thin) () -> Int

extension PublicStructHasInitValue {
  init(z: Int) {}
}
