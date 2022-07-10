// RUN: %target-swift-frontend -primary-file %s -emit-sil -emit-verbose-sil | %FileCheck %s

func searchForMe(_ x: Float) -> Float {
  return x
}

@_transparent func baz(_ x: Float) -> Float {
  return searchForMe(x);
}

@_transparent func bar(_ x: Float, _ b: Bool) -> Float {
  if b {
    return baz(x)
  }
  return x
  // CHECK-LABEL: _TF13sil_locations3bar
  // CHECK: function_ref @_TF13sil_locations11searchForMe{{.*}} line:13:12:minlined
  // CHECK: apply {{.*}} line:13:12:minlined
}

func testMandatoryInlining(_ x: Float, b: Bool) -> Float {
  return bar(x, b)
// CHECK-LABEL: _TF13sil_locations21testMandatoryInlining
// CHECK: function_ref @_TF13sil_locations11searchFor{{.*}} line:22:10:minlined
// CHECK: apply                                                  {{.*}} line:22:10:minlined
// CHECK: br                                                     {{.*}} line:22:10:minlined
// CHECK: br                                                     {{.*}} line:22:10:minlined
}

