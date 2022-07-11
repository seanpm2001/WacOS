// RUN: %target-swift-frontend -emit-sil -O %s | %FileCheck %s

protocol Bar {
  associatedtype Element
}

class FooImplBase<OutputElement> {
  func virtualMethod() -> FooImplBase<OutputElement> {
    fatalError("implement")
  }
}

final class FooImplDerived<Input : Bar> : FooImplBase<Input.Element> {

  init(_ input: Input) {}

  override func virtualMethod() -> FooImplBase<Input.Element> {
    return self
  }
}

struct BarImpl : Bar {
  typealias Element = Int
}

// Check that all calls can be devirtualized and later on inlined.
// CHECK-LABEL: sil {{.*}}zzz
// CHECK: bb0
// CHECK-NOT: bb1
// CHECK-NOT: function_ref
// CHECK-NOT: class_method
// CHECK-NOT: apply
// CHECK-NOT: bb1
// CHECK: return
public func zzz() {
  let xs = BarImpl()
  var source: FooImplBase<Int> = FooImplDerived(xs)
  source = source.virtualMethod()
  source = source.virtualMethod()
  source = source.virtualMethod()
}

