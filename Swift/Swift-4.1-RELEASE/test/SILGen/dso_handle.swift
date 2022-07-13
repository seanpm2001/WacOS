// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -enable-sil-ownership -emit-silgen %s | %FileCheck %s

// CHECK: sil_global [[DSO:@__dso_handle]] : $Builtin.RawPointer

// CHECK-LABEL: sil @main : $@convention(c)
// CHECK: bb0
// CHECK: [[DSOAddr:%[0-9]+]] = global_addr [[DSO]] : $*Builtin.RawPointer
// CHECK-NEXT: [[DSOPtr:%[0-9]+]] = address_to_pointer [[DSOAddr]] : $*Builtin.RawPointer to $Builtin.RawPointer
// CHECK-NEXT: [[DSOPtrStruct:[0-9]+]] = struct $UnsafeRawPointer ([[DSOPtr]] : $Builtin.RawPointer)

func printDSOHandle(dso: UnsafeRawPointer = #dsohandle) -> UnsafeRawPointer {
  print(dso)
  return dso
}

@_inlineable public func printDSOHandleInlineable(dso: UnsafeRawPointer = #dsohandle) -> UnsafeRawPointer {
  return dso
}

@_inlineable public func callsPrintDSOHandleInlineable() {
  printDSOHandleInlineable()
}

_ = printDSOHandle()
