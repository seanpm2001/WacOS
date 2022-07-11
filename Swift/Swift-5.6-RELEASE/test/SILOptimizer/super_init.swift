// RUN: %target-swift-frontend -emit-sil %s | %FileCheck %s

// CHECK-LABEL: sil hidden [exact_self_class] [noinline] @$s10super_init3FooCyACSicfC : $@convention(method) (Int, @thick Foo.Type) -> @owned Foo
// CHECK-NOT:     class_method
// CHECK-NOT:     super_method
// CHECK:         [[SUPER_INIT:%.*]] = function_ref @$s10super_init3FooCyACSicfc
// CHECK:         [[NEW_SELF:%.*]] = apply [[SUPER_INIT]]

// CHECK-LABEL: sil hidden [noinline] @$s10super_init3BarC{{[_0-9a-zA-Z]*}}fc
// CHECK-NOT:     super_method
// CHECK:         function_ref @$s10super_init3FooCACycfc

class Foo {
  @inline(never)
  init() {}
  @inline(never)
  init(_ x: Foo) {}
  @inline(never)
  init(_ x: Int) {}
}

class Bar: Foo {
  @inline(never)
  override init() {
    super.init()
  }
}

extension Foo {
  @inline(never)
  convenience init(x: Int) {
    self.init()
  }
}

class Zim: Foo {
  var foo = Foo()
  // CHECK-LABEL: sil hidden @$s10super_init3ZimC{{[_0-9a-zA-Z]*}}fc
  // CHECK-NOT:     super_method {{%[0-9]+}} : $Zim, #Foo.init!initializer
  // CHECK:         function_ref @$s10super_init3FooCACycfC
  // CHECK:         function_ref @$s10super_init3FooCACycfc
}

class Zang: Foo {
  var foo: Foo

  @inline(never)
  override init() {
    foo = Foo()
    super.init()
  }
  // CHECK-LABEL: sil hidden [noinline] @$s10super_init4ZangCACycfc
  // CHECK-NOT:         super_method {{%[0-9]+}} : $Zang, #Foo.init!initializer
  // CHECK:             function_ref @$s10super_init3FooCACycfC
  // CHECK:             function_ref @$s10super_init3FooCACycfc
}

class Good: Foo {
  let x: Int

  // CHECK-LABEL: sil hidden [noinline] @$s10super_init4GoodCACycfc
  // CHECK-NOT:     super_method {{%[0-9]+}} : $Good, #Foo.init!initializer
  // CHECK:         [[SUPER_INIT:%.*]] = function_ref @$s10super_init3FooCyACSicfc
  // CHECK:         apply [[SUPER_INIT]]
  @inline(never)
  override init() {
    x = 10
    super.init(x)
  }
}
