// RUN: %target-swift-frontend -import-objc-header %S/Inputs/c_function_pointer_in_c_struct.h -emit-silgen -enable-sil-ownership %s | %FileCheck %s

// CHECK-LABEL: sil hidden @_T027lying_about_optional_return0C37ChainingForeignFunctionTypeProperties{{[_0-9a-zA-Z]*}}F
func optionalChainingForeignFunctionTypeProperties(a: SomeCallbacks?) {
  // CHECK: enum $Optional<()>, #Optional.some!enumelt.1, {{%.*}} : $()
  let _: ()? = voidReturning()
  // CHECK: unchecked_trivial_bit_cast {{%.*}} : $UnsafeMutableRawPointer to $Optional<UnsafeMutableRawPointer>
  let _: UnsafeMutableRawPointer? = voidPointerReturning()
  // CHECK: unchecked_trivial_bit_cast {{%.*}} : $UnsafeMutablePointer<Int8> to $Optional<UnsafeMutablePointer<Int8>>
  let _: UnsafeMutablePointer<Int8>? = pointerReturning()
  // CHECK: unchecked_trivial_bit_cast {{%.*}} : $UnsafePointer<Int8> to $Optional<UnsafePointer<Int8>>
  let _: UnsafePointer<Int8>? = constPointerReturning()
  // CHECK: unchecked_trivial_bit_cast {{%.*}} : $OpaquePointer to $Optional<OpaquePointer>
  let _: OpaquePointer? = opaquePointerReturning()

  // CHECK: enum $Optional<()>, #Optional.some!enumelt.1, {{%.*}} : $()
  a?.voidReturning()
  // CHECK: unchecked_trivial_bit_cast {{%.*}} : $UnsafeMutableRawPointer to $Optional<UnsafeMutableRawPointer>
  a?.voidPointerReturning()
  // CHECK: unchecked_trivial_bit_cast {{%.*}} : $UnsafeMutablePointer<Int8> to $Optional<UnsafeMutablePointer<Int8>>
  a?.pointerReturning()
  // CHECK: unchecked_trivial_bit_cast {{%.*}} : $UnsafePointer<Int8> to $Optional<UnsafePointer<Int8>>
  a?.constPointerReturning()
  // CHECK: unchecked_trivial_bit_cast {{%.*}} : $OpaquePointer to $Optional<OpaquePointer>
  a?.opaquePointerReturning()
}

