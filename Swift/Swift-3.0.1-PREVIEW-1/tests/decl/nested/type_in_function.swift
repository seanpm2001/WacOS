// RUN: %target-parse-verify-swift -parse-as-library

// Generic class locally defined in non-generic function (rdar://problem/20116710)
func f3() {
  class B<T> {}
}

protocol Racoon {
  associatedtype Stripes
}

// Types inside generic functions -- not supported yet

func outerGenericFunction<T>(_ t: T) {
  struct InnerNonGeneric { // expected-error{{type 'InnerNonGeneric' cannot be nested in generic function 'outerGenericFunction'}}
    func nonGenericMethod(_ t: T) {}
    func genericMethod<V>(_ t: T) -> V where V : Racoon, V.Stripes == T {}
  }

  struct InnerGeneric<U> { // expected-error{{type 'InnerGeneric' cannot be nested in generic function 'outerGenericFunction'}}
    func nonGenericMethod(_ t: T, u: U) {}
    func genericMethod<V>(_ t: T, u: U) -> V where V : Racoon, V.Stripes == T {}
  }
}

class OuterNonGenericClass {
  func genericFunction<T>(_ t: T) {
    class InnerNonGenericClass : OuterNonGenericClass { // expected-error {{type 'InnerNonGenericClass' cannot be nested in generic function 'genericFunction'}}
      let t: T

      init(t: T) { super.init(); self.t = t }
    }

    class InnerGenericClass<U> : OuterNonGenericClass // expected-error {{type 'InnerGenericClass' cannot be nested in generic function 'genericFunction'}}
        where U : Racoon, U.Stripes == T {
      let t: T

      init(t: T) { super.init(); self.t = t }
    }
  }
}

class OuterGenericClass<T> {
  func genericFunction<U>(_ t: U) {
    class InnerNonGenericClass1 : OuterGenericClass { // expected-error {{type 'InnerNonGenericClass1' cannot be nested in generic function 'genericFunction'}}
      let t: T

      init(t: T) { super.init(); self.t = t }
    }

    class InnerNonGenericClass2 : OuterGenericClass<Int> { // expected-error {{type 'InnerNonGenericClass2' cannot be nested in generic function 'genericFunction'}}
      let t: T

      init(t: T) { super.init(); self.t = t }
    }

    class InnerNonGenericClass3 : OuterGenericClass<T> { // expected-error {{type 'InnerNonGenericClass3' cannot be nested in generic function 'genericFunction'}}
      let t: T

      init(t: T) { super.init(); self.t = t }
    }

    class InnerGenericClass<U> : OuterGenericClass<U> // expected-error {{type 'InnerGenericClass' cannot be nested in generic function 'genericFunction'}}
      where U : Racoon, U.Stripes == T {
      let t: T

      init(t: T) { super.init(); self.t = t }
    }
  }
}

// Name lookup within local classes.
func f5<T, U>(x: T, y: U) {
  struct Local { // expected-error {{type 'Local' cannot be nested in generic function 'f5'}}
    func f() {
      _ = 17 as T // expected-error{{'Int' is not convertible to 'T'}} {{14-16=as!}}
      _ = 17 as U // okay: refers to 'U' declared within the local class
    }
    typealias U = Int
  }
}
