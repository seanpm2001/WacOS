// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -emit-silgen %s -disable-objc-attr-requires-foundation-module | %FileCheck %s

protocol Fooable {
  init()
  func foo(_ x: Int)
  mutating func bar()
  mutating func bas()

  var prop1: Int { get set }
  var prop2: Int { get set }
  var prop3: Int { get nonmutating set }
}

protocol Barrable: class {
  init()
  func foo(_ x: Int)
  func bar()
  func bas()

  var prop1: Int { get set }
  var prop2: Int { get set }
  var prop3: Int { get set }
}

struct S: Fooable {
  var x: C? // Make the type nontrivial, so +0/+1 is observable.

  // CHECK-LABEL: sil hidden @_TFV15guaranteed_self1SC{{.*}} : $@convention(method) (@thin S.Type) -> @owned S
  init() {}
  // TODO: Way too many redundant r/r pairs here. Should use +0 rvalues.
  // CHECK-LABEL: sil hidden @_TFV15guaranteed_self1S3foo{{.*}} : $@convention(method) (Int, @guaranteed S) -> () {
  // CHECK:       bb0({{.*}} [[SELF:%.*]] : $S):
  // CHECK-NOT:     retain_value [[SELF]]
  // CHECK-NOT:     release_value [[SELF]]
  func foo(_ x: Int) {
    self.foo(x)
  }

  func foooo(_ x: (Int, Bool)) {
    self.foooo(x)
  }

  // CHECK-LABEL: sil hidden @_TFV15guaranteed_self1S3bar{{.*}} : $@convention(method) (@inout S) -> ()
  // CHECK:       bb0([[SELF:%.*]] : $*S):
  // CHECK-NOT:     destroy_addr [[SELF]]
  mutating func bar() {
    self.bar()
  }
  // CHECK-LABEL: sil hidden @_TFV15guaranteed_self1S3bas{{.*}} : $@convention(method) (@guaranteed S) -> ()
  // CHECK:       bb0([[SELF:%.*]] : $S):
  // CHECK-NOT:     retain_value [[SELF]]
  // CHECK-NOT:     release_value [[SELF]]
  func bas() {
    self.bas()
  }

  var prop1: Int = 0

  var prop2: Int {
    // CHECK-LABEL: sil hidden @_TFV15guaranteed_self1Sg5prop2Si : $@convention(method) (@guaranteed S) -> Int
    // CHECK:       bb0([[SELF:%.*]] : $S):
    // CHECK-NOT:     release_value [[SELF]]
    get { return 0 }
    // CHECK-LABEL: sil hidden @_TFV15guaranteed_self1Ss5prop2Si : $@convention(method) (Int, @inout S) -> ()
    // CHECK-LABEL: sil hidden [transparent] @_TFV15guaranteed_self1Sm5prop2Si : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout S) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>)
    set { }
  }

  var prop3: Int {
    // CHECK-LABEL: sil hidden @_TFV15guaranteed_self1Sg5prop3Si : $@convention(method) (@guaranteed S) -> Int
    // CHECK:       bb0([[SELF:%.*]] : $S):
    // CHECK-NOT:     release_value [[SELF]]
    get { return 0 }
    // CHECK-LABEL: sil hidden @_TFV15guaranteed_self1Ss5prop3Si : $@convention(method) (Int, @guaranteed S) -> ()
    // CHECK:       bb0({{.*}} [[SELF:%.*]] : $S):
    // CHECK-NOT:     release_value [[SELF]]
    // CHECK-LABEL: sil hidden [transparent] @_TFV15guaranteed_self1Sm5prop3Si : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @guaranteed S) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>)
    // CHECK:       bb0({{.*}} [[SELF:%.*]] : $S):
    // CHECK-NOT:     release_value [[SELF]]
    nonmutating set { }
  }

  // Getter for prop1
  // CHECK-LABEL: sil hidden [transparent] @_TFV15guaranteed_self1Sg5prop1Si : $@convention(method) (@guaranteed S) -> Int
  // CHECK:       bb0([[SELF:%.*]] : $S):
  // CHECK-NOT:     release_value [[SELF]]

  // Setter for prop1
  // CHECK-LABEL: sil hidden [transparent] @_TFV15guaranteed_self1Ss5prop1Si : $@convention(method) (Int, @inout S) -> ()
  // CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*S):
  // CHECK-NOT:     load [[SELF_ADDR]]
  // CHECK-NOT:     destroy_addr [[SELF_ADDR]]

  // materializeForSet for prop1
  // CHECK-LABEL: sil hidden [transparent] @_TFV15guaranteed_self1Sm5prop1Si : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout S) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>)
  // CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*S):
  // CHECK-NOT:     load [[SELF_ADDR]]
  // CHECK-NOT:     destroy_addr [[SELF_ADDR]]
}

// Witness thunk for nonmutating 'foo'
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWV15guaranteed_self1SS_7FooableS_FS1_3foo{{.*}} : $@convention(witness_method) (Int, @in_guaranteed S) -> () {
// CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*S):
// CHECK:         [[SELF_COPY:%.*]] = alloc_stack $S
// CHECK:         copy_addr [[SELF_ADDR]] to [initialization] [[SELF_COPY]]
// CHECK:         [[SELF:%.*]] = load [[SELF_COPY]]
// CHECK:         release_value [[SELF]]
// CHECK-NOT:     release_value [[SELF]]
// CHECK-NOT:     destroy_addr [[SELF_COPY]]
// CHECK-NOT:     destroy_addr [[SELF_ADDR]]

// Witness thunk for mutating 'bar'
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWV15guaranteed_self1SS_7FooableS_FS1_3bar{{.*}} : $@convention(witness_method) (@inout S) -> () {
// CHECK:       bb0([[SELF_ADDR:%.*]] : $*S):
// CHECK-NOT:     load [[SELF_ADDR]]
// CHECK-NOT:     destroy_addr [[SELF_ADDR]]

// Witness thunk for 'bas', which is mutating in the protocol, but nonmutating
// in the implementation
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWV15guaranteed_self1SS_7FooableS_FS1_3bas{{.*}} : $@convention(witness_method) (@inout S) -> ()
// CHECK:       bb0([[SELF_ADDR:%.*]] : $*S):
// CHECK:         [[SELF:%.*]] = load [[SELF_ADDR]]
// CHECK:         retain_value [[SELF]]
// CHECK:         release_value [[SELF]]
// CHECK-NOT:     release_value [[SELF]]

// Witness thunk for prop1 getter
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWV15guaranteed_self1SS_7FooableS_FS1_g5prop1Si : $@convention(witness_method) (@in_guaranteed S) -> Int
// CHECK:       bb0([[SELF_ADDR:%.*]] : $*S):
// CHECK:         [[SELF_COPY:%.*]] = alloc_stack $S
// CHECK:         copy_addr [[SELF_ADDR]] to [initialization] [[SELF_COPY]]
// CHECK:         [[SELF:%.*]] = load [[SELF_COPY]]
// CHECK:         release_value [[SELF]]
// CHECK-NOT:     release_value [[SELF]]
// CHECK-NOT:     destroy_addr [[SELF_COPY]]
// CHECK-NOT:     destroy_addr [[SELF_ADDR]]

// Witness thunk for prop1 setter
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWV15guaranteed_self1SS_7FooableS_FS1_s5prop1Si : $@convention(witness_method) (Int, @inout S) -> () {
// CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*S):
// CHECK-NOT:     destroy_addr [[SELF_ADDR]]

// Witness thunk for prop1 materializeForSet
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWV15guaranteed_self1SS_7FooableS_FS1_m5prop1Si : $@convention(witness_method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout S) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*S):
// CHECK-NOT:     destroy_addr [[SELF_ADDR]]

// Witness thunk for prop2 getter
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWV15guaranteed_self1SS_7FooableS_FS1_g5prop2Si : $@convention(witness_method) (@in_guaranteed S) -> Int
// CHECK:       bb0([[SELF_ADDR:%.*]] : $*S):
// CHECK:         [[SELF_COPY:%.*]] = alloc_stack $S
// CHECK:         copy_addr [[SELF_ADDR]] to [initialization] [[SELF_COPY]]
// CHECK:         [[SELF:%.*]] = load [[SELF_COPY]]
// CHECK:         release_value [[SELF]]
// CHECK-NOT:     release_value [[SELF]]
// CHECK-NOT:     destroy_addr [[SELF_COPY]]
// CHECK-NOT:     destroy_addr [[SELF_ADDR]]

// Witness thunk for prop2 setter
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWV15guaranteed_self1SS_7FooableS_FS1_s5prop2Si : $@convention(witness_method) (Int, @inout S) -> () {
// CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*S):
// CHECK-NOT:     destroy_addr [[SELF_ADDR]]

// Witness thunk for prop2 materializeForSet
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWV15guaranteed_self1SS_7FooableS_FS1_m5prop2Si : $@convention(witness_method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout S) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*S):
// CHECK-NOT:     destroy_addr [[SELF_ADDR]]

// Witness thunk for prop3 getter
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWV15guaranteed_self1SS_7FooableS_FS1_g5prop3Si : $@convention(witness_method) (@in_guaranteed S) -> Int
// CHECK:       bb0([[SELF_ADDR:%.*]] : $*S):
// CHECK:         [[SELF_COPY:%.*]] = alloc_stack $S
// CHECK:         copy_addr [[SELF_ADDR]] to [initialization] [[SELF_COPY]]
// CHECK:         [[SELF:%.*]] = load [[SELF_COPY]]
// CHECK:         release_value [[SELF]]
// CHECK-NOT:     release_value [[SELF]]
// CHECK-NOT:     destroy_addr [[SELF_COPY]]
// CHECK-NOT:     destroy_addr [[SELF_ADDR]]

// Witness thunk for prop3 nonmutating setter
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWV15guaranteed_self1SS_7FooableS_FS1_s5prop3Si : $@convention(witness_method) (Int, @in_guaranteed S) -> ()
// CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*S):
// CHECK:         [[SELF_COPY:%.*]] = alloc_stack $S
// CHECK:         copy_addr [[SELF_ADDR]] to [initialization] [[SELF_COPY]]
// CHECK:         [[SELF:%.*]] = load [[SELF_COPY]]
// CHECK:         release_value [[SELF]]
// CHECK-NOT:     release_value [[SELF]]
// CHECK-NOT:     destroy_addr [[SELF_COPY]]
// CHECK-NOT:     destroy_addr [[SELF_ADDR]]

// Witness thunk for prop3 nonmutating materializeForSet
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWV15guaranteed_self1SS_7FooableS_FS1_m5prop3Si : $@convention(witness_method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout S) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>)
// CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*S):
// CHECK:         [[SELF:%.*]] = load [[SELF_ADDR]]
// CHECK:         retain_value [[SELF]]
// CHECK:         release_value [[SELF]]
// CHECK-NOT:     release_value [[SELF]]
// CHECK:       }

//
// TODO: Expected output for the other cases
//

struct AO<T>: Fooable {
  var x: T?

  init() {}
  // CHECK-LABEL: sil hidden @_TFV15guaranteed_self2AO3foo{{.*}} : $@convention(method) <T> (Int, @in_guaranteed AO<T>) -> ()
  // CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*AO<T>):
  // CHECK-NOT:     copy_addr
  // CHECK:         apply {{.*}} [[SELF_ADDR]]
  // CHECK-NOT:     destroy_addr [[SELF_ADDR]]
  // CHECK:       }
  func foo(_ x: Int) {
    self.foo(x)
  }
  mutating func bar() {
    self.bar()
  }
  // CHECK-LABEL: sil hidden @_TFV15guaranteed_self2AO3bas{{.*}} : $@convention(method) <T> (@in_guaranteed AO<T>) -> ()
  // CHECK:       bb0([[SELF_ADDR:%.*]] : $*AO<T>):
  // CHECK-NOT:     destroy_addr [[SELF_ADDR]]
  func bas() {
    self.bas()
  }


  var prop1: Int = 0
  var prop2: Int {
    // CHECK-LABEL: sil hidden @_TFV15guaranteed_self2AOg5prop2Si : $@convention(method) <T> (@in_guaranteed AO<T>) -> Int {
    // CHECK:       bb0([[SELF_ADDR:%.*]] : $*AO<T>):
    // CHECK-NOT:     destroy_addr [[SELF_ADDR]]
    get { return 0 }
    set { }
  }
  var prop3: Int {
    // CHECK-LABEL: sil hidden @_TFV15guaranteed_self2AOg5prop3Si : $@convention(method) <T> (@in_guaranteed AO<T>) -> Int
    // CHECK:       bb0([[SELF_ADDR:%.*]] : $*AO<T>):
    // CHECK-NOT:     destroy_addr [[SELF_ADDR]]
    get { return 0 }
    // CHECK-LABEL: sil hidden @_TFV15guaranteed_self2AOs5prop3Si : $@convention(method) <T> (Int, @in_guaranteed AO<T>) -> ()
    // CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*AO<T>):
    // CHECK-NOT:     destroy_addr [[SELF_ADDR]]
    // CHECK-LABEL: sil hidden [transparent] @_TFV15guaranteed_self2AOm5prop3Si : $@convention(method) <T> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @in_guaranteed AO<T>) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>)
    // CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*AO<T>):
    // CHECK-NOT:     destroy_addr [[SELF_ADDR]]
    // CHECK:       }
    nonmutating set { }
  }
}

// Witness for nonmutating 'foo'
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWurGV15guaranteed_self2AOx_S_7FooableS_FS1_3foo{{.*}} : $@convention(witness_method) <T> (Int, @in_guaranteed AO<T>) -> ()
// CHECK:       bb0({{.*}} [[SELF_ADDR:%.*]] : $*AO<T>):
// TODO: This copy isn't necessary.
// CHECK:         copy_addr [[SELF_ADDR]] to [initialization] [[SELF_COPY:%.*]] :
// CHECK:         apply {{.*}} [[SELF_COPY]]
// CHECK:         destroy_addr [[SELF_COPY]]
// CHECK-NOT:     destroy_addr [[SELF_ADDR]]

// Witness for 'bar', which is mutating in protocol but nonmutating in impl
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWurGV15guaranteed_self2AOx_S_7FooableS_FS1_3bar{{.*}} : $@convention(witness_method) <T> (@inout AO<T>) -> ()
// CHECK:       bb0([[SELF_ADDR:%.*]] : $*AO<T>):
// -- NB: This copy *is* necessary, unless we're willing to assume an inout
//        parameter is not mutably aliased.
// CHECK:         copy_addr [[SELF_ADDR]] to [initialization] [[SELF_COPY:%.*]] :
// CHECK:         apply {{.*}} [[SELF_COPY]]
// CHECK:         destroy_addr [[SELF_COPY]]
// CHECK-NOT:     destroy_addr [[SELF_ADDR]]

class C: Fooable, Barrable {
  // Allocating initializer
  // CHECK-LABEL: sil hidden @_TFC15guaranteed_self1CC{{.*}} : $@convention(method) (@thick C.Type) -> @owned C
  // CHECK:         [[SELF1:%.*]] = alloc_ref $C
  // CHECK-NOT:     [[SELF1]]
  // CHECK:         [[SELF2:%.*]] = apply {{.*}}([[SELF1]])
  // CHECK-NOT:     [[SELF2]]
  // CHECK:         return [[SELF2]]

  // Initializing constructors still have the +1 in, +1 out convention.
  // CHECK-LABEL: sil hidden @_TFC15guaranteed_self1Cc{{.*}} : $@convention(method) (@owned C) -> @owned C {
  // CHECK:       bb0([[SELF:%.*]] : $C):
  // CHECK:         [[MARKED_SELF:%.*]] = mark_uninitialized [rootself] [[SELF]]
  // CHECK-NOT:     [[SELF]]
  // CHECK-NOT:     strong_retain [[MARKED_SELF]]
  // CHECK-NOT:     strong_release [[MARKED_SELF]]
  // CHECK:         return [[MARKED_SELF]]

  // @objc thunk for initializing constructor
  // CHECK-LABEL: sil hidden [thunk] @_TToFC15guaranteed_self1Cc{{.*}} : $@convention(objc_method) (@owned C) -> @owned C
  // CHECK:       bb0([[SELF:%.*]] : $C):
  // CHECK-NOT:     retain{{.*}} [[SELF]]
  // CHECK:         [[SELF2:%.*]] = apply {{%.*}}([[SELF]])
  // CHECK-NOT:     release{{.*}} [[SELF]]
  // CHECK-NOT:     release{{.*}} [[SELF2]]
  // CHECK:         return [[SELF2]]
  @objc required init() {}


  // CHECK-LABEL: sil hidden @_TFC15guaranteed_self1C3foo{{.*}} : $@convention(method) (Int, @guaranteed C) -> ()
  // CHECK:       bb0({{.*}} [[SELF:%.*]] : $C):
  // CHECK-NOT:     retain
  // CHECK-NOT:     release

  // CHECK-LABEL: sil hidden [thunk] @_TToFC15guaranteed_self1C3foo{{.*}} : $@convention(objc_method) (Int, C) -> () {
  // CHECK:       bb0({{.*}} [[SELF:%.*]] : $C):
  // CHECK:         retain{{.*}} [[SELF]]
  // CHECK:         apply {{.*}} [[SELF]]
  // CHECK:         release{{.*}} [[SELF]]
  // CHECK-NOT:     release{{.*}} [[SELF]]
  @objc func foo(_ x: Int) {
    self.foo(x)
  }
  @objc func bar() {
    self.bar()
  }
  @objc func bas() {
    self.bas()
  }

  // CHECK-LABEL: sil hidden [transparent] [thunk] @_TToFC15guaranteed_self1Cg5prop1Si : $@convention(objc_method) (C) -> Int
  // CHECK:       bb0([[SELF:%.*]] : $C):
  // CHECK:         retain{{.*}} [[SELF]]
  // CHECK:         apply {{.*}}([[SELF]])
  // CHECK:         release{{.*}} [[SELF]]
  // CHECK-NOT:     release{{.*}} [[SELF]]

  // CHECK-LABEL: sil hidden [transparent] [thunk] @_TToFC15guaranteed_self1Cs5prop1Si : $@convention(objc_method) (Int, C) -> ()
  // CHECK:       bb0({{.*}} [[SELF:%.*]] : $C):
  // CHECK:         retain{{.*}} [[SELF]]
  // CHECK:         apply {{.*}} [[SELF]]
  // CHECK:         release{{.*}} [[SELF]]
  // CHECK-NOT:     release{{.*}} [[SELF]]
  // CHECK:       }
  @objc var prop1: Int = 0
  @objc var prop2: Int {
    get { return 0 }
    set {}
  }
  @objc var prop3: Int {
    get { return 0 }
    set {}
  }

}

class D: C {
  // CHECK-LABEL: sil hidden @_TFC15guaranteed_self1DC{{.*}} : $@convention(method) (@thick D.Type) -> @owned D
  // CHECK:         [[SELF1:%.*]] = alloc_ref $D
  // CHECK-NOT:     [[SELF1]]
  // CHECK:         [[SELF2:%.*]] = apply {{.*}}([[SELF1]])
  // CHECK-NOT:     [[SELF1]]
  // CHECK-NOT:     [[SELF2]]
  // CHECK:         return [[SELF2]]

  // CHECK-LABEL: sil hidden @_TFC15guaranteed_self1Dc{{.*}} : $@convention(method) (@owned D) -> @owned D
  // CHECK:       bb0([[SELF:%.*]] : $D):
  // CHECK:         [[SELF_BOX:%.*]] = alloc_box $D
  // CHECK-NEXT:    [[PB:%.*]] = project_box [[SELF_BOX]]
  // CHECK-NEXT:    [[SELF_ADDR:%.*]] = mark_uninitialized [derivedself] [[PB]]
  // CHECK-NEXT:    store [[SELF]] to [[SELF_ADDR]]
  // CHECK-NOT:     [[SELF_ADDR]]
  // CHECK:         [[SELF1:%.*]] = load [[SELF_ADDR]]
  // CHECK-NEXT:    [[SUPER1:%.*]] = upcast [[SELF1]]
  // CHECK-NOT:     [[SELF_ADDR]]
  // CHECK:         [[SUPER2:%.*]] = apply {{.*}}([[SUPER1]])
  // CHECK-NEXT:    [[SELF2:%.*]] = unchecked_ref_cast [[SUPER2]]
  // CHECK-NEXT:    store [[SELF2]] to [[SELF_ADDR]]
  // CHECK-NOT:     [[SELF_ADDR]]
  // CHECK-NOT:     [[SELF1]]
  // CHECK-NOT:     [[SUPER1]]
  // CHECK-NOT:     [[SELF2]]
  // CHECK-NOT:     [[SUPER2]]
  // CHECK:         [[SELF_FINAL:%.*]] = load [[SELF_ADDR]]
  // CHECK-NEXT:    retain{{.*}} [[SELF_FINAL]]
  // CHECK-NEXT:    release{{.*}} [[SELF_BOX]]
  // CHECK-NEXT:    return [[SELF_FINAL]]
  required init() {
    super.init()
  }

  // CHECK-LABEL: sil shared [transparent] [thunk] @_TTDFC15guaranteed_self1D3foo{{.*}} : $@convention(method) (Int, @guaranteed D) -> ()
  // CHECK:       bb0({{.*}} [[SELF:%.*]]):
  // CHECK:         retain{{.*}} [[SELF]]
  // CHECK:         release{{.*}} [[SELF]]
  // CHECK-NOT:     release{{.*}} [[SELF]]
  // CHECK:       }
  dynamic override func foo(_ x: Int) {
    self.foo(x)
  }
}

func S_curryThunk(_ s: S) -> ((S) -> (Int) -> ()/*, Int -> ()*/) {
  return (S.foo /*, s.foo*/)
}

func AO_curryThunk<T>(_ ao: AO<T>) -> ((AO<T>) -> (Int) -> ()/*, Int -> ()*/) {
  return (AO.foo /*, ao.foo*/)
}

// ----------------------------------------------------------------------------
// Make sure that we properly translate in_guaranteed parameters
// correctly if we are asked to.
// ----------------------------------------------------------------------------

// CHECK-LABEL: sil [transparent] [thunk] @_TTWV15guaranteed_self9FakeArrayS_8SequenceS_FS1_17_constrainElement{{.*}} : $@convention(witness_method) (@in FakeElement, @in_guaranteed FakeArray) -> () {
// CHECK: bb0([[ARG0_PTR:%.*]] : $*FakeElement, [[ARG1_PTR:%.*]] : $*FakeArray):
// CHECK: [[GUARANTEED_COPY_STACK_SLOT:%.*]] = alloc_stack $FakeArray
// CHECK: copy_addr [[ARG1_PTR]] to [initialization] [[GUARANTEED_COPY_STACK_SLOT]]
// CHECK: [[ARG0:%.*]] = load [[ARG0_PTR]]
// CHECK: function_ref (extension in guaranteed_self):guaranteed_self.SequenceDefaults._constrainElement
// CHECK: [[FUN:%.*]] = function_ref @_{{.*}}
// CHECK: apply [[FUN]]<FakeArray, FakeElement, FakeGenerator, FakeElement>([[ARG0]], [[GUARANTEED_COPY_STACK_SLOT]])
// CHECK: destroy_addr [[GUARANTEED_COPY_STACK_SLOT]]

class Z {}

public struct FakeGenerator {}
public struct FakeArray {
  var z = Z()
}
public struct FakeElement {}

public protocol FakeGeneratorProtocol {
  associatedtype Element
}

extension FakeGenerator : FakeGeneratorProtocol {
  public typealias Element = FakeElement
}

public protocol SequenceDefaults {
  associatedtype Element
  associatedtype Generator : FakeGeneratorProtocol
}

extension SequenceDefaults {
  public final func _constrainElement(_: FakeGenerator.Element) {}
}

public protocol Sequence : SequenceDefaults {
  func _constrainElement(_: Element)
}


extension FakeArray : Sequence {
  public typealias Element = FakeElement
  public typealias Generator = FakeGenerator

  func _containsElement(_: Element) {}
}

// -----------------------------------------------------------------------------
// Make sure that we do not emit extra retains when accessing let fields of
// guaranteed parameters.
// -----------------------------------------------------------------------------

class Kraken {
  func enrage() {}
}

func destroyShip(_ k: Kraken) {}

class LetFieldClass {
  let letk = Kraken()
  var vark = Kraken()

  // CHECK-LABEL: sil hidden @_TFC15guaranteed_self13LetFieldClass10letkMethod{{.*}} : $@convention(method) (@guaranteed LetFieldClass) -> () {
  // CHECK: bb0([[CLS:%.*]] : $LetFieldClass):
  // CHECK: [[KRAKEN_ADDR:%.*]] = ref_element_addr [[CLS]] : $LetFieldClass, #LetFieldClass.letk
  // CHECK-NEXT: [[KRAKEN:%.*]] = load [[KRAKEN_ADDR]]
  // CHECK-NEXT: [[KRAKEN_METH:%.*]] = class_method [[KRAKEN]]
  // CHECK-NEXT: apply [[KRAKEN_METH]]([[KRAKEN]])
  // CHECK-NEXT: [[KRAKEN_ADDR:%.*]] = ref_element_addr [[CLS]] : $LetFieldClass, #LetFieldClass.letk
  // CHECK-NEXT: [[KRAKEN:%.*]] = load [[KRAKEN_ADDR]]
  // CHECK-NEXT: strong_retain [[KRAKEN]]
  // CHECK: [[DESTROY_SHIP_FUN:%.*]] = function_ref @_TF15guaranteed_self11destroyShipFCS_6KrakenT_ : $@convention(thin) (@owned Kraken) -> ()
  // CHECK-NEXT: strong_retain [[KRAKEN]]
  // CHECK-NEXT: apply [[DESTROY_SHIP_FUN]]([[KRAKEN]])
  // CHECK-NEXT: [[KRAKEN_BOX:%.*]] = alloc_box $Kraken
  // CHECK-NEXT: [[PB:%.*]] = project_box [[KRAKEN_BOX]]
  // CHECK-NEXT: [[KRAKEN_ADDR:%.*]] = ref_element_addr [[CLS]] : $LetFieldClass, #LetFieldClass.letk
  // CHECK-NEXT: [[KRAKEN2:%.*]] = load [[KRAKEN_ADDR]]
  // CHECK-NEXT: strong_retain [[KRAKEN2]]
  // CHECK-NEXT: store [[KRAKEN2]] to [[PB]]
  // CHECK: [[DESTROY_SHIP_FUN:%.*]] = function_ref @_TF15guaranteed_self11destroyShipFCS_6KrakenT_ : $@convention(thin) (@owned Kraken) -> ()
  // CHECK-NEXT: [[KRAKEN_COPY:%.*]] = load [[PB]]
  // CHECK-NEXT: strong_retain [[KRAKEN_COPY]]
  // CHECK-NEXT: apply [[DESTROY_SHIP_FUN]]([[KRAKEN_COPY]])
  // CHECK-NEXT: strong_release [[KRAKEN_BOX]]
  // CHECK-NEXT: strong_release [[KRAKEN]]
  // CHECK-NEXT: tuple
  // CHECK-NEXT: return
  func letkMethod() {
    letk.enrage()
    let ll = letk
    destroyShip(ll)
    var lv = letk
    destroyShip(lv)
  }

  // CHECK-LABEL: sil hidden @_TFC15guaranteed_self13LetFieldClass10varkMethod{{.*}} : $@convention(method) (@guaranteed LetFieldClass) -> () {
  // CHECK: bb0([[CLS:%.*]] : $LetFieldClass):
  // CHECK: [[KRAKEN_GETTER_FUN:%.*]] = class_method [[CLS]] : $LetFieldClass, #LetFieldClass.vark!getter.1 : (LetFieldClass) -> () -> Kraken , $@convention(method) (@guaranteed LetFieldClass) -> @owned Kraken
  // CHECK-NEXT: [[KRAKEN:%.*]] = apply [[KRAKEN_GETTER_FUN]]([[CLS]])
  // CHECK-NEXT: [[KRAKEN_METH:%.*]] = class_method [[KRAKEN]]
  // CHECK-NEXT: apply [[KRAKEN_METH]]([[KRAKEN]])
  // CHECK-NEXT: strong_release [[KRAKEN]]
  // CHECK-NEXT: [[KRAKEN_GETTER_FUN:%.*]] = class_method [[CLS]] : $LetFieldClass, #LetFieldClass.vark!getter.1 : (LetFieldClass) -> () -> Kraken , $@convention(method) (@guaranteed LetFieldClass) -> @owned Kraken
  // CHECK-NEXT: [[KRAKEN:%.*]] = apply [[KRAKEN_GETTER_FUN]]([[CLS]])
  // CHECK: [[DESTROY_SHIP_FUN:%.*]] = function_ref @_TF15guaranteed_self11destroyShipFCS_6KrakenT_ : $@convention(thin) (@owned Kraken) -> ()
  // CHECK-NEXT: strong_retain [[KRAKEN]]
  // CHECK-NEXT: apply [[DESTROY_SHIP_FUN]]([[KRAKEN]])
  // CHECK-NEXT: [[KRAKEN_BOX:%.*]] = alloc_box $Kraken
  // CHECK-NEXT: [[PB:%.*]] = project_box [[KRAKEN_BOX]]
  // CHECK-NEXT: [[KRAKEN_GETTER_FUN:%.*]] = class_method [[CLS]] : $LetFieldClass, #LetFieldClass.vark!getter.1 : (LetFieldClass) -> () -> Kraken , $@convention(method) (@guaranteed LetFieldClass) -> @owned Kraken
  // CHECK-NEXT: [[KRAKEN2:%.*]] = apply [[KRAKEN_GETTER_FUN]]([[CLS]])
  // CHECK-NEXT: store [[KRAKEN2]] to [[PB]]
  // CHECK: [[DESTROY_SHIP_FUN:%.*]] = function_ref @_TF15guaranteed_self11destroyShipFCS_6KrakenT_ : $@convention(thin) (@owned Kraken) -> ()
  // CHECK-NEXT: [[KRAKEN_COPY:%.*]] = load [[PB]]
  // CHECK-NEXT: strong_retain [[KRAKEN_COPY]]
  // CHECK-NEXT: apply [[DESTROY_SHIP_FUN]]([[KRAKEN_COPY]])
  // CHECK-NEXT: strong_release [[KRAKEN_BOX]]
  // CHECK-NEXT: strong_release [[KRAKEN]]
  // CHECK-NEXT: tuple
  // CHECK-NEXT: return
  func varkMethod() {
    vark.enrage()
    let vl = vark
    destroyShip(vl)
    var vv = vark
    destroyShip(vv)
  }
}

// -----------------------------------------------------------------------------
// Make sure that in all of the following cases find has only one retain in it.
// -----------------------------------------------------------------------------

class ClassIntTreeNode {
  let value : Int
  let left, right : ClassIntTreeNode

  init() {}

  // CHECK-LABEL: sil hidden @_TFC15guaranteed_self16ClassIntTreeNode4find{{.*}} : $@convention(method) (Int, @guaranteed ClassIntTreeNode) -> @owned ClassIntTreeNode {
  // CHECK-NOT: strong_release
  // CHECK: strong_retain
  // CHECK-NOT: strong_retain
  // CHECK-NOT: strong_release
  // CHECK: return
  func find(_ v : Int) -> ClassIntTreeNode {
    if v == value { return self }
    if v < value { return left.find(v) }
    return right.find(v)
  }
}
