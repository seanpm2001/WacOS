// RUN: %target-swift-frontend  -O -sil-inline-threshold 0 -emit-sil -primary-file %s | %FileCheck %s

// rdar://problem/20338028
protocol PA: class { }
protocol PB { associatedtype B: PA }

class CA: PA { }
class CB: PB { typealias B = CA }

struct S<A: PB> {
  @_transparent
  func crash() -> Bool {
    let a: A.B? = nil
    return a === a
  }
}

// CHECK-LABEL: sil hidden @_TF20specialize_anyobject6callit
func callit(s: S<CB>) {
  // CHECK: function_ref @_TFsoi3eeeFTGSqPs9AnyObject__GSqPS____Sb : $@convention(thin) (@owned Optional<AnyObject>, @owned Optional<AnyObject>) -> Bool
  s.crash()
}
