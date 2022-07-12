// RUN: %target-swift-frontend -primary-file %s -module-name Swift -g -module-link-name swiftCore -O -parse-as-library -parse-stdlib -emit-module -emit-module-path - -o /dev/null | %target-sil-opt -enable-sil-verify-all -module-name="Swift" | %FileCheck %s
// RUN: %target-swift-frontend -primary-file %s -module-name Swift -g -O -parse-as-library -parse-stdlib -emit-sib -o - | %target-sil-opt -enable-sil-verify-all -module-name="Swift" | %FileCheck %s -check-prefix=SIB-CHECK

// CHECK: import Builtin
// CHECK: import Swift

// CHECK: func unknown()

// CHECK: struct X {
// CHECK-NEXT:  @_inlineable func test()
// CHECK-NEXT:  @_inlineable init
// CHECK-NEXT: }

// CHECK: sil{{.*}} @unknown : $@convention(thin) () -> ()

// CHECK-LABEL: sil [serialized] @_T0s1XV4testyyF : $@convention(method) (X) -> ()
// CHECK: bb0
// CHECK-NEXT: function_ref
// CHECK-NEXT: function_ref @unknown : $@convention(thin) () -> ()
// CHECK-NEXT: apply
// CHECK-NEXT: tuple
// CHECK-NEXT: return

// CHECK-LABEL: sil [serialized] @_T0s1XVABycfC : $@convention(method) (@thin X.Type) -> X
// CHECK: bb0
// CHECK-NEXT: struct $X ()
// CHECK-NEXT: return


// SIB-CHECK: import Builtin
// SIB-CHECK: import Swift

// SIB-CHECK: func unknown()

// SIB-CHECK: struct X {
// SIB-CHECK-NEXT:  func test()
// SIB-CHECK-NEXT:  init
// SIB-CHECK-NEXT: }

// SIB-CHECK: sil @unknown : $@convention(thin) () -> ()

// SIB-CHECK-LABEL: sil [serialized] @_T0s1XV4testyyF : $@convention(method) (X) -> ()
// SIB-CHECK: bb0
// SIB-CHECK-NEXT: function_ref
// SIB-CHECK-NEXT: function_ref @unknown : $@convention(thin) () -> ()
// SIB-CHECK-NEXT: apply
// SIB-CHECK-NEXT: tuple
// SIB-CHECK-NEXT: return

// SIB-CHECK-LABEL: sil [serialized] @_T0s1XVABycfC : $@convention(method) (@thin X.Type) -> X
// SIB-CHECK: bb0
// SIB-CHECK-NEXT: struct $X ()
// SIB-CHECK-NEXT: return

@_silgen_name("unknown")
public func unknown() -> ()

// FIXME: Why does it need to be public?
public struct X {
  @_inlineable
  public func test() {
    unknown()
  }

  @_inlineable
  public init() {}
}
