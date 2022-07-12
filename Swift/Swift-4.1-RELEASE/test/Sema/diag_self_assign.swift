// RUN: %target-typecheck-verify-swift

var sa1_global: Int
sa1_global = sa1_global // expected-error {{assigning a variable to itself}}

class SA1 {
  var foo: Int = 0
  init(fooi: Int) {
    var foo = fooi
    foo = foo // expected-error {{assigning a variable to itself}}
    self.foo = self.foo // expected-error {{assigning a property to itself}}
    foo = self.foo // no-error
    self.foo = foo // no-error
  }
  func f(fooi: Int) {
    var foo = fooi
    foo = foo // expected-error {{assigning a variable to itself}}
    self.foo = self.foo // expected-error {{assigning a property to itself}}
    foo = self.foo // no-error
    self.foo = foo // no-error
  }
}

class SA2 {
  var foo: Int {
    get {
      return 0
    }
    set {}
  }
  init(fooi: Int) {
    var foo = fooi
    foo = foo // expected-error {{assigning a variable to itself}}
    self.foo = self.foo // expected-error {{assigning a property to itself}}
    foo = self.foo // no-error
    self.foo = foo // no-error
  }
  func f(fooi: Int) {
    var foo = fooi
    foo = foo // expected-error {{assigning a variable to itself}}
    self.foo = self.foo // expected-error {{assigning a property to itself}}
    foo = self.foo // no-error
    self.foo = foo // no-error
  }
}

class SA3 {
  var foo: Int {
    get {
      return foo // expected-warning {{attempting to access 'foo' within its own getter}} expected-note{{access 'self' explicitly to silence this warning}} {{14-14=self.}}
    }
    set {
      foo = foo // expected-error {{assigning a property to itself}} expected-warning {{attempting to modify 'foo' within its own setter}} expected-note{{access 'self' explicitly to silence this warning}} {{7-7=self.}}
      self.foo = self.foo // expected-error {{assigning a property to itself}}
      foo = self.foo // expected-error {{assigning a property to itself}} expected-warning {{attempting to modify 'foo' within its own setter}} expected-note{{access 'self' explicitly to silence this warning}} {{7-7=self.}}
      self.foo = foo // expected-error {{assigning a property to itself}}
    }
  }
}

class SA4 {
  var foo: Int {
    get {
      return foo // expected-warning {{attempting to access 'foo' within its own getter}} expected-note{{access 'self' explicitly to silence this warning}} {{14-14=self.}}
    }
    set(value) {
      value = value // expected-error {{cannot assign to value: 'value' is a 'let' constant}}
    }
  }
}

class SA5 {
  var foo: Int = 0
}
func SA5_test(a: SA4, b: SA4) {
  a.foo = a.foo // expected-error {{assigning a property to itself}}
  a.foo = b.foo
}

class SA_Deep1 {
  class Foo {
    var aThing = String()
  }

  class Bar {
    var aFoo =  Foo()
  }

  var aFoo = Foo()

  func test() {
    let aBar = Bar()
    aBar.aFoo = Foo()
    aBar.aFoo.aThing = self.aFoo.aThing // no-error
  }
}

