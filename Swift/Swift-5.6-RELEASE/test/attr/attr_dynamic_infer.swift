// RUN: %target-swift-ide-test -print-ast-typechecked -source-filename=%s -print-implicit-attrs -disable-objc-attr-requires-foundation-module | %FileCheck %s
// REQUIRES: objc_interop

@objc class Super {
  @objc dynamic func baseFoo() {}
}

// CHECK: extension Super {
extension Super {
  // CHECK:  @objc dynamic func foo
  @objc func foo() { }

  // CHECK: @objc dynamic var prop: Super
  @objc var prop: Super {
    // CHECK: @objc get
    get { return Super() }
    // CHECK: @objc set
    set { }
  }

  // CHECK: @objc dynamic subscript(sup: Super) -> Super
  @objc subscript(sup: Super) -> Super {
    // CHECK: @objc get
    get { return sup }
    // CHECK: @objc set
    set { }
  }
}


@objc class Sub : Super { }

// CHECK: extension Sub
extension Sub {
  // CHECK: @objc override dynamic func foo
  override func foo() { }

  // CHECK: @objc override dynamic var prop: Super
  override var prop: Super {
    // CHECK: @objc get
    get { return Super() }
    // CHECK: @objc set
    set { }
  }

  // CHECK: @objc override dynamic subscript(sup: Super) -> Super
  override subscript(sup: Super) -> Super {
    // CHECK: @objc get
    get { return sup }
    // CHECK: @objc set
    set { }
  }

  // CHECK: @objc override dynamic func baseFoo
  override func baseFoo() {
  }
}


@objc class FinalTests {}

extension FinalTests {
  // CHECK: @objc final func foo
  @objc final func foo() { }

  // CHECK: @objc final var prop: Super
  @objc final var prop: Super {
    // CHECK: get
    get { return Super() }
    // CHECK: set
    set { }
  }

  // CHECK: @objc final subscript(sup: Super) -> Super
  @objc final subscript(sup: Super) -> Super {
    // CHECK: get
    get { return sup }
    // CHECK: set
    set { }
  }

  // CHECK: @objc @_hasInitialValue static var x
  @objc static var x: Int = 0

  // CHECK: @objc static func bar
  @objc static func bar() { }
}

