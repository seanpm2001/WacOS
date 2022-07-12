// RUN: %target-typecheck-verify-swift

func test15921520() {
    var x: Int = 0
    func f<T>(_ x: T) {}
    f(&x) // expected-error{{'&' used with non-inout argument of type 'Int'}} {{7-8=}}
}

func test20807269() {
    var x: Int = 0
    func f<T>(_ x: T) {}
    f(1, &x) // expected-error{{extra argument in call}}
}

func test15921530() {
    struct X {}

    func makef<T>() -> (T) -> () {
      return {
        x in ()
      }
    }
    var _: (inout X) -> () = makef() // expected-error{{cannot convert value of type '(X) -> ()' to specified type '(inout X) -> ()'}}
}
