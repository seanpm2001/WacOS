// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership %s | %FileCheck %s
// RUN: %target-swift-frontend -enable-sil-ownership -emit-ir %s

protocol Fooable {
  associatedtype Bar

  func foo<T: Fooable where T.Bar == Self.Bar>(x x: T) -> Self.Bar
}

struct X {}

// Ensure that the protocol witness for requirements with same-type constraints
// is set correctly. <rdar://problem/16369105>
// CHECK-LABEL: sil private [transparent] [thunk] @_T017witness_same_type3FooVAA7FooableA2aDP3foo3BarQzqd__1x_tAaDRd__AGQyd__AHRSlFTW : $@convention(witness_method: Fooable) <τ_0_0 where τ_0_0 : Fooable, τ_0_0.Bar == X> (@in τ_0_0, @in_guaranteed Foo) -> @out X
struct Foo: Fooable {
  typealias Bar = X

  func foo<T: Fooable where T.Bar == X>(x x: T) -> X { return X() }
}

// rdar://problem/19049566
// CHECK-LABEL: sil shared [transparent] [serialized] [thunk] @_T017witness_same_type14LazySequenceOfVyxq_Gs0E0AAsAEP12makeIterator0H0QzyFTW : $@convention(witness_method: Sequence) <τ_0_0, τ_0_1 where τ_0_0 : Sequence, τ_0_1 == τ_0_0.Element> (@in_guaranteed LazySequenceOf<τ_0_0, τ_0_1>) -> @out AnyIterator<τ_0_1>
public struct LazySequenceOf<SS : Sequence, A where SS.Iterator.Element == A> : Sequence {
  public func makeIterator() -> AnyIterator<A> { 
    var opt: AnyIterator<A>?
    return opt!
  }
	public subscript(i : Int) -> A { 
    get { 
      var opt: A?
      return opt!
    } 
  }
}
