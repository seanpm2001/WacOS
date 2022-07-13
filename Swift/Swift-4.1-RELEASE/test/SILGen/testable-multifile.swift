// This test is paired with testable-multifile-other.swift.

// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -emit-module %S/Inputs/TestableMultifileHelper.swift -enable-testing -enable-sil-ownership -o %t

// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership -I %t %s %S/testable-multifile-other.swift -module-name main | %FileCheck %s
// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership -I %t %S/testable-multifile-other.swift %s -module-name main | %FileCheck %s
// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership -I %t -primary-file %s %S/testable-multifile-other.swift -module-name main | %FileCheck %s

// Just make sure we don't crash later on.
// RUN: %target-swift-frontend -enable-sil-ownership -emit-ir -I %t -primary-file %s %S/testable-multifile-other.swift -module-name main -o /dev/null
// RUN: %target-swift-frontend -enable-sil-ownership -emit-ir -I %t -O -primary-file %s %S/testable-multifile-other.swift -module-name main -o /dev/null

@testable import TestableMultifileHelper

public protocol Fooable {
  func foo()
}

struct FooImpl: Fooable, HasDefaultFoo {}
public struct PublicFooImpl: Fooable, HasDefaultFoo {}

// CHECK-LABEL: sil{{.*}} @_T04main7FooImplVAA7FooableA2aDP3fooyyFTW : $@convention(witness_method: Fooable) (@in_guaranteed FooImpl) -> () {
// CHECK: function_ref @_T023TestableMultifileHelper13HasDefaultFooPAAE3fooyyF
// CHECK: } // end sil function '_T04main7FooImplVAA7FooableA2aDP3fooyyFTW'

// CHECK-LABEL: sil{{.*}} @_T04main13PublicFooImplVAA7FooableA2aDP3fooyyFTW : $@convention(witness_method: Fooable) (@in_guaranteed PublicFooImpl) -> () {
// CHECK: function_ref @_T023TestableMultifileHelper13HasDefaultFooPAAE3fooyyF
// CHECK: } // end sil function '_T04main13PublicFooImplVAA7FooableA2aDP3fooyyFTW'

private class PrivateSub: Base {
  fileprivate override func foo() {}
}
class Sub: Base {
  internal override func foo() {}
}
public class PublicSub: Base {
  public override func foo() {}
}

// CHECK-LABEL: sil_vtable PrivateSub {
// CHECK-NEXT:   #Base.foo!1: {{.*}} : _T04main10PrivateSub33_F1525133BD493492AD72BF10FBCB1C52LLC3fooyyF
// CHECK-NEXT:   #Base.init!initializer.1: {{.*}} : _T04main10PrivateSub33_F1525133BD493492AD72BF10FBCB1C52LLCADycfc
// CHECK-NEXT:   #PrivateSub.deinit!deallocator: _T04main10PrivateSub33_F1525133BD493492AD72BF10FBCB1C52LLCfD
// CHECK-NEXT: }

// CHECK-LABEL: sil_vtable Sub {
// CHECK-NEXT:   #Base.foo!1: {{.*}} : _T04main3SubC3fooyyF
// CHECK-NEXT:   #Base.init!initializer.1: {{.*}} : _T04main3SubCACycfc
// CHECK-NEXT:   #Sub.deinit!deallocator: _T04main3SubCfD
// CHECK-NEXT: }

// CHECK-LABEL: sil_vtable [serialized] PublicSub {
// CHECK-NEXT:   #Base.foo!1: {{.*}} : _T04main9PublicSubC3fooyyF
// CHECK-NEXT:   #Base.init!initializer.1: {{.*}} : _T04main9PublicSubCACycfc
// CHECK-NEXT:   #PublicSub.deinit!deallocator: _T04main9PublicSubCfD
// CHECK-NEXT: }



// CHECK-LABEL: sil_witness_table hidden FooImpl: Fooable module main {
// CHECK-NEXT:  method #Fooable.foo!1: {{.*}} : @_T04main7FooImplVAA7FooableA2aDP3fooyyFTW
// CHECK-NEXT: }

// CHECK-LABEL: sil_witness_table [serialized] PublicFooImpl: Fooable module main {
// CHECK-NEXT:  method #Fooable.foo!1: {{.*}} : @_T04main13PublicFooImplVAA7FooableA2aDP3fooyyFTW
// CHECK-NEXT: }
