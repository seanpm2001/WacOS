
// RUN: %target-swift-emit-silgen -module-name final %s | %FileCheck %s

class TestClass {

  final
  var finalProperty : Int { return 42 }

  final
  func finalMethod() -> Int { return 12 }


  func baseMethod() {}
}

class TestDerived : TestClass {
  final
  override func baseMethod() {}
}


// CHECK-LABEL: sil hidden [ossa] @{{.*}}testDirectDispatch{{.*}} : $@convention(thin) (@guaranteed TestClass) -> Int {
// CHECK: bb0([[ARG:%.*]] : @guaranteed $TestClass):
// CHECK: [[FINALMETH:%[0-9]+]] = function_ref @$s5final9TestClassC0A6Method{{[_0-9a-zA-Z]*}}F
// CHECK: apply [[FINALMETH]]([[ARG]])
// CHECK: [[FINALPROP:%[0-9]+]] = function_ref @$s5final9TestClassC0A8PropertySivg
// CHECK: apply [[FINALPROP]]([[ARG]])
func testDirectDispatch(c : TestClass) -> Int {
  return c.finalMethod()+c.finalProperty
}


// Verify that the non-overriding final methods don't get emitted to the vtable.
// CHECK-LABEL: sil_vtable TestClass {
// CHECK-NEXT:  #TestClass.baseMethod: {{.*}} : @$s5final9TestClassC10baseMethod{{[_0-9a-zA-Z]*}}F
// CHECK-NEXT:  #TestClass.init!allocator: {{.*}} : @$s5final9TestClassC{{[_0-9a-zA-Z]*}}fC
// CHECK-NEXT:  #TestClass.deinit!
// CHECK-NEXT: }

// Verify that overriding final methods don't get emitted to the vtable.
// CHECK-LABEL: sil_vtable TestDerived {
// CHECK-NEXT:  #TestClass.baseMethod: {{.*}} : @$s5final11TestDerivedC10baseMethod{{[_0-9a-zA-Z]*}}F
// CHECK-NEXT:  #TestClass.init!allocator: {{.*}} : @$s5final11TestDerivedC{{[_0-9a-zA-Z]*}}fC
// CHECK-NEXT:  #TestDerived.deinit!
// CHECK-NEXT: }
