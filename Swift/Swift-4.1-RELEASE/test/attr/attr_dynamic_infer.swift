// RUN: %target-swift-ide-test -print-ast-typechecked -source-filename=%s -print-implicit-attrs -disable-objc-attr-requires-foundation-module | %FileCheck %s

@objc class Super {
  func baseFoo() {}
}

// CHECK: extension Super {
extension Super {
  // CHECK:  @objc dynamic func foo
  func foo() { }

  // CHECK: @objc dynamic var prop: Super
  var prop: Super {
    // CHECK: @objc dynamic get
    get { return Super() }
    // CHECK: @objc dynamic set
    set { }
  }

  // CHECK: @objc dynamic subscript(sup: Super) -> Super
  subscript(sup: Super) -> Super {
    // CHECK: @objc dynamic get
    get { return sup }
    // CHECK: @objc dynamic set
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
    // CHECK: @objc override dynamic get
    get { return Super() }
    // CHECK: @objc override dynamic set
    set { }
  }

  // CHECK: @objc override dynamic subscript(sup: Super) -> Super
  override subscript(sup: Super) -> Super {
    // CHECK: @objc override dynamic get
    get { return sup }
    // CHECK: @objc override dynamic set
    set { }
  }

  // CHECK: @objc override dynamic func baseFoo
  override func baseFoo() {
  }
}


@objc class FinalTests {}

extension FinalTests {
  // CHECK: @objc final func foo
  final func foo() { }

  // CHECK: @objc final var prop: Super
  final var prop: Super {
    // CHECK: @objc final get
    get { return Super() }
    // CHECK: @objc final set
    set { }
  }

  // CHECK: @objc final subscript(sup: Super) -> Super
  final subscript(sup: Super) -> Super {
    // CHECK: @objc final get
    get { return sup }
    // CHECK: @objc final set
    set { }
  }

  // CHECK: @objc static var x
  static var x: Int = 0

  // CHECK: @objc static func bar
  static func bar() { }
}

