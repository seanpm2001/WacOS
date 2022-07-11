// RUN: %target-typecheck-verify-swift

struct YieldRValue {
  var property: String {
    _read {
      yield "hello"
    }
  }
}

struct YieldVariables {
  var property: String {
    _read {
      yield ""
    }
    _modify {
      var x = ""
      yield &x
    }
  }

  var wrongTypes: String {
    _read {
      yield 0 // expected-error {{cannot convert value of type 'Int' to expected yield type 'String'}}
    }
    _modify {
      var x = 0
      yield &x // expected-error {{cannot yield reference to storage of type 'Int' as an inout yield of type 'String'}}
    }
  }

  var rvalue: String {
    get {}
    _modify {
      yield &"" // expected-error {{cannot yield immutable value of type 'String' as an inout yield}}
    }
  }

  var missingAmp: String {
    get {}
    _modify {
      var x = ""
      yield x // expected-error {{yielding mutable value of type 'String' requires explicit '&'}}
    }
  }
}

protocol HasProperty {
  associatedtype Value
  var property: Value { get set }
}

struct GenericTypeWithYields<T> : HasProperty {
  var storedProperty: T?

  var property: T {
    _read {
      yield storedProperty!
    }
    _modify {
      yield &storedProperty!
    }
  }

  subscript<U>(u: U) -> (T,U) {
    _read {
      yield ((storedProperty!, u))
    }
    _modify {
      var temp = (storedProperty!, u)
      yield &temp
    }
  }
}

// 'yield' is a context-sensitive keyword.
func yield(_: Int) {}
func call_yield() {
  yield(0)
}

struct YieldInDefer {
  var property: String {
    _read {
      defer { // expected-warning {{'defer' statement at end of scope always executes immediately}}{{7-12=do}}
        // FIXME: this recovery is terrible
        yield ""
        // expected-error@-1 {{function is unused}}
        // expected-error@-2 {{consecutive statements on a line must be separated by ';'}}
        // expected-warning@-3 {{string literal is unused}}
      }
    }
  }
}

// SR-15066
struct InvalidYieldParsing {
  var property: String {
    _read {
      yield(x: "test") // expected-error {{unexpected label in 'yield' statement}} {{13-16=}}
      yield(x: "test", y:   {0}) // expected-error {{expected 1 yield value(s)}}
      // expected-error@-1 {{unexpected label in 'yield' statement}} {{13-16=}}
      // expected-error@-2 {{unexpected label in 'yield' statement}} {{24-29=}}
      yield(_: "test") // expected-error {{unexpected label in 'yield' statement}} {{13-16=}}
    }
  }
}
