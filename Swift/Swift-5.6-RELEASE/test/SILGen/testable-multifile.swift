// This test is paired with testable-multifile-other.swift.

// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -emit-module %S/Inputs/TestableMultifileHelper.swift -enable-testing -o %t

// RUN: %target-swift-emit-silgen -I %t %s %S/testable-multifile-other.swift -module-name main | %FileCheck %s
// RUN: %target-swift-emit-silgen -I %t %S/testable-multifile-other.swift %s -module-name main | %FileCheck %s
// RUN: %target-swift-emit-silgen -I %t -primary-file %s %S/testable-multifile-other.swift -module-name main | %FileCheck %s

// Just make sure we don't crash later on.
// RUN: %target-swift-emit-ir -I %t -primary-file %s %S/testable-multifile-other.swift -module-name main -o /dev/null
// RUN: %target-swift-emit-ir -I %t -O -primary-file %s %S/testable-multifile-other.swift -module-name main -o /dev/null

@testable import TestableMultifileHelper

public protocol Fooable {
  func foo()
}

struct FooImpl: Fooable, HasDefaultFoo {}
public struct PublicFooImpl: Fooable, HasDefaultFoo {}

// CHECK-LABEL: sil{{.*}} @$s4main7FooImplVAA7FooableA2aDP3fooyyFTW :
// CHECK: function_ref @$s23TestableMultifileHelper13HasDefaultFooPAAE3fooyyF
// CHECK: } // end sil function '$s4main7FooImplVAA7FooableA2aDP3fooyyFTW'

// CHECK-LABEL: sil{{.*}} @$s4main13PublicFooImplVAA7FooableA2aDP3fooyyFTW :
// CHECK: function_ref @$s23TestableMultifileHelper13HasDefaultFooPAAE3fooyyF
// CHECK: } // end sil function '$s4main13PublicFooImplVAA7FooableA2aDP3fooyyFTW'

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
// CHECK-NEXT:   #Base.foo: {{.*}} : @$s4main10PrivateSub33_F1525133BD493492AD72BF10FBCB1C52LLC3fooyyF
// CHECK-NEXT:   #Base.init!allocator: {{.*}} : @$s4main10PrivateSub33_F1525133BD493492AD72BF10FBCB1C52LLCADycfC
// CHECK-NEXT:   #PrivateSub.deinit!deallocator: @$s4main10PrivateSub33_F1525133BD493492AD72BF10FBCB1C52LLCfD
// CHECK-NEXT: }

// CHECK-LABEL: sil_vtable Sub {
// CHECK-NEXT:   #Base.foo: {{.*}} : @$s4main3SubC3fooyyF
// CHECK-NEXT:   #Base.init!allocator: {{.*}} : @$s4main3SubCACycfC
// CHECK-NEXT:   #Sub.deinit!deallocator: @$s4main3SubCfD
// CHECK-NEXT: }

// CHECK-LABEL: sil_vtable [serialized] PublicSub {
// CHECK-NEXT:   #Base.foo: {{.*}} : @$s4main9PublicSubC3fooyyF
// CHECK-NEXT:   #Base.init!allocator: {{.*}} : @$s4main9PublicSubCACycfC
// CHECK-NEXT:   #PublicSub.deinit!deallocator: @$s4main9PublicSubCfD
// CHECK-NEXT: }



// CHECK-LABEL: sil_witness_table hidden FooImpl: Fooable module main {
// CHECK-NEXT:  method #Fooable.foo: {{.*}} : @$s4main7FooImplVAA7FooableA2aDP3fooyyFTW
// CHECK-NEXT: }

// CHECK-LABEL: sil_witness_table [serialized] PublicFooImpl: Fooable module main {
// CHECK-NEXT:  method #Fooable.foo: {{.*}} : @$s4main13PublicFooImplVAA7FooableA2aDP3fooyyFTW
// CHECK-NEXT: }
