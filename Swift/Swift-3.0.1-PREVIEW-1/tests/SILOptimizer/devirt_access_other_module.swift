// RUN: %target-swift-frontend -emit-sil -O %s | %FileCheck %s

// Check that this file does not crash a compiler.
//
// The devirtualizer should not devirtualize calls inside fragile functions,
// if the resulting direct call would refer to a private or hidden symbol.
//
// rdar://21408247

public class ExternalClass {
  fileprivate func foo() {}
}

public func getExternalClass() -> ExternalClass {
  return ExternalClass()
}

// Note: This will eventually be illegal (to have a public @_transparent function
// referring to a private method), but for now it lets us test what can and
// can't be optimized.
// CHECK-LABEL: sil [transparent] [fragile] @_TTSfq4g___TF26devirt_access_other_module9invokeFooFCS_13ExternalClassT_
// CHECK-NOT: function_ref
// CHECK: class_method
// CHECK-NOT: function_ref
// CHECK: apply
// CHECK-NOT: function_ref
// CHECK-NOT: checked_cast_br
// CHECK-NOT: bb1
// CHECK: return
@_transparent public func invokeFoo(_ obj: ExternalClass) {
  obj.foo()
}

private class PrivateSubclass : ExternalClass {
  override fileprivate func foo() {}
}

internal class InternalSubclass : ExternalClass {
  override fileprivate func foo() {}
}
