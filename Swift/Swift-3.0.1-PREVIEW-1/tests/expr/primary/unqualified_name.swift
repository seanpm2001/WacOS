// RUN: %target-parse-verify-swift

func f0(_ x: Int, y: Int, z: Int) { }
func f1(_ x: Int, while: Int) { }
func f2(_ x: Int, `let` _: Int) { }
func f3(_ x: Int, _ y: Int, z: Int) { } // expected-note{{did you mean 'f3'?}}

func test01() {
  _ = f0(_:y:z:)
  _ = f0(:y:z:) // expected-error{{an empty argument label is spelled with '_'}}{{10-10=_}}
  _ = f1(_:`while`:) // expected-warning{{keyword 'while' does not need to be escaped in argument list}}{{12-13=}}{{18-19=}}
  _ = f2(_:`let`:)
  _ = f3(_::z:) // expected-error{{an empty argument label is spelled with '_'}}{{12-12=_}}
}

struct S0 {
  func f0(_ x: Int, y: Int, z: Int) { }
  func f1(_ x: Int, while: Int) { }
  func f2(_ x: Int, `let` _: Int) { }

  func testS0() {
    _ = f0(_:y:z:)
    _ = f0(:y:z:) // expected-error{{an empty argument label is spelled with '_'}}{{12-12=_}}
    _ = f1(_:`while`:) // expected-warning{{keyword 'while' does not need to be escaped in argument list}}{{14-15=}}{{20-21=}}
    _ = f2(_:`let`:)
    
    _ = self.f0(_:y:z:)
    _ = self.f0(:y:z:) // expected-error{{an empty argument label is spelled with '_'}}{{17-17=_}}
    _ = self.f1(_:`while`:) // expected-warning{{keyword 'while' does not need to be escaped in argument list}}{{19-20=}}{{25-26=}}
    _ = self.f2(_:`let`:)

    _ = f3(_:y:z:) // expected-error{{use of unresolved identifier 'f3(_:y:z:)'}}
  }

  static func testStaticS0() {
    _ = f0(_:y:z:)
    _ = f3(_:y:z:)
  }

  static func f3(_ x: Int, y: Int, z: Int) -> S0 { return S0() }
}

// Determine context from type.
let s0_static: S0 = .f3(_:y:z:)(0, y: 0, z: 0)

class C0 {
  init(x: Int, y: Int, z: Int) { }

  convenience init(all: Int) {
    self.init(x:y:z:)(all, all, all)
  }

  func f0(_ x: Int, y: Int, z: Int) { }
  func f1(_ x: Int, while: Int) { }
  func f2(_ x: Int, `let` _: Int) { }
}

class C1 : C0 {
  init(all: Int) {
    super.init(x:y:z:)(all, all, all)
  }

  func testC0() {
    _ = f0(_:y:z:)
    _ = f0(:y:z:) // expected-error{{an empty argument label is spelled with '_'}}{{12-12=_}}
    _ = f1(_:`while`:) // expected-warning{{keyword 'while' does not need to be escaped in argument list}}{{14-15=}}{{20-21=}}
    _ = f2(_:`let`:)
    
    _ = super.f0(_:y:z:)
    _ = super.f0(:y:z:) // expected-error{{an empty argument label is spelled with '_'}}{{18-18=_}}
    _ = super.f1(_:`while`:) // expected-warning{{keyword 'while' does not need to be escaped in argument list}}{{20-21=}}{{26-27=}}
    _ = self.f2(_:`let`:)
  }
}

