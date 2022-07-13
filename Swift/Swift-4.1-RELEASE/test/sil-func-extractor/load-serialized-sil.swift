// RUN: %target-swift-frontend -primary-file %s -module-name Swift -g -module-link-name swiftCore -O -parse-as-library -parse-stdlib -emit-module -emit-module-path - -o /dev/null | %target-sil-func-extractor -module-name="Swift" -func="_T0s1XV4testyyF" | %FileCheck %s
// RUN: %target-swift-frontend -primary-file %s -module-name Swift -g -O -parse-as-library -parse-stdlib -emit-sib -o - | %target-sil-func-extractor -module-name="Swift" -func="_T0s1XV4testyyF" | %FileCheck %s -check-prefix=SIB-CHECK

// CHECK: import Builtin
// CHECK: import Swift

// CHECK: func unknown()

// CHECK: struct X {
// CHECK-NEXT:  @_versioned
// CHECK-NEXT:  @_inlineable func test()
// CHECK-NEXT:  init
// CHECK-NEXT: }

// CHECK: sil @unknown : $@convention(thin) () -> ()

// CHECK-LABEL: sil [serialized] @_T0s1XV4testyyF : $@convention(method) (X) -> ()
// CHECK: bb0
// CHECK-NEXT: function_ref
// CHECK-NEXT: function_ref @unknown : $@convention(thin) () -> ()
// CHECK-NEXT: apply
// CHECK-NEXT: tuple
// CHECK-NEXT: return

// CHECK-NOT: sil {{.*}} @_T0s1XVABycfC : $@convention(thin) (@thin X.Type) -> X


// SIB-CHECK: import Builtin
// SIB-CHECK: import Swift

// SIB-CHECK: func unknown()

// SIB-CHECK: struct X {
// SIB-CHECK-NEXT:  @_versioned
// SIB-CHECK-NEXT:  @_inlineable func test()
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

// SIB-CHECK-NOT: sil {{.*}} @_T0s1XVABycfC : $@convention(thin) (@thin X.Type) -> X

@_silgen_name("unknown")
public func unknown() -> ()

public struct X {
  @_versioned
  @_inlineable
  func test() {
    unknown()
  }
}
