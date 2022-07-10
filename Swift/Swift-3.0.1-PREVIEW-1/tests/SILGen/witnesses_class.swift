// RUN: %target-swift-frontend -emit-silgen %s | %FileCheck %s

protocol Fooable: class {
  func foo()
  static func bar()
  init()
}

class Foo: Fooable {
  
  func foo() { }
  // CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWC15witnesses_class3FooS_7FooableS_FS1_3foo
  // CHECK-NOT:     function_ref
  // CHECK:         class_method

  class func bar() {}
  // CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWC15witnesses_class3FooS_7FooableS_ZFS1_3bar
  // CHECK-NOT:     function_ref
  // CHECK:         class_method

  required init() {}
  // CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWC15witnesses_class3FooS_7FooableS_FS1_C
  // CHECK-NOT:     function_ref
  // CHECK:         class_method
}

// CHECK-LABEL: sil hidden @_TF15witnesses_class3gen
// CHECK:         bb0([[SELF:%.*]] : $T)
// CHECK:         [[METHOD:%.*]] = witness_method $T
// CHECK-NOT:     strong_retain [[SELF]]
// CHECK:         apply [[METHOD]]<T>([[SELF]])
// CHECK:         strong_release [[SELF]]
// CHECK-NOT:         strong_release [[SELF]]
// CHECK:         return
func gen<T: Fooable>(_ foo: T) {
  foo.foo()
}

// CHECK-LABEL: sil hidden @_TF15witnesses_class2exFPS_7Fooable_T_
// CHECK: bb0([[SELF:%[0-0]+]] : $Fooable):
// CHECK:         [[SELF_PROJ:%.*]] = open_existential_ref [[SELF]]
// CHECK:         [[METHOD:%.*]] = witness_method $[[OPENED:@opened(.*) Fooable]],
// CHECK-NOT:     strong_retain [[SELF_PROJ]] : $
// CHECK:         apply [[METHOD]]<[[OPENED]]>([[SELF_PROJ]])
// CHECK:         strong_release [[SELF]]
// CHECK-NOT:     strong_release [[SELF]]
// CHECK:         return
func ex(_ foo: Fooable) {
  foo.foo()
}
