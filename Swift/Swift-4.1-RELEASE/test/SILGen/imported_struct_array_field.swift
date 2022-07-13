// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership -import-objc-header %S/Inputs/array_typedef.h %s | %FileCheck %s

// CHECK-LABEL: sil shared [transparent] [serializable] @_T0SC4NameV{{[_0-9a-zA-Z]*}}fC : $@convention(method) (UInt8, UInt8, UInt8, UInt8, @thin Name.Type) -> Name
func useImportedArrayTypedefInit() -> Name {
  return Name(name: (0, 0, 0, 0))
}
