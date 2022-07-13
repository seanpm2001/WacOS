// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership -primary-file %s | %FileCheck %s
// RUN: %target-swift-frontend -emit-sil -O -primary-file %s > /dev/null
// RUN: %target-swift-frontend -emit-ir -primary-file %s > /dev/null

extension Array where Element == Int {
  // CHECK-LABEL: sil @_T0Sa22constrained_extensionsSiRszlESaySiGyt1x_tcfC : $@convention(method) (@thin Array<Int>.Type) -> @owned Array<Int>
  public init(x: ()) {
    self.init()
  }

  // CHECK-LABEL: sil @_T0Sa22constrained_extensionsSiRszlE16instancePropertySivg : $@convention(method) (@guaranteed Array<Int>) -> Int
  // CHECK-LABEL: sil @_T0Sa22constrained_extensionsSiRszlE16instancePropertySivs : $@convention(method) (Int, @inout Array<Int>) -> ()
  // CHECK-LABEL: sil shared [transparent] [serialized] @_T0Sa22constrained_extensionsSiRszlE16instancePropertySivmytfU_ : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout Array<Int>, @thick Array<Int>.Type) -> ()
  // CHECK-LABEL: sil [transparent] [serialized] @_T0Sa22constrained_extensionsSiRszlE16instancePropertySivm : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout Array<Int>) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>)

  public var instanceProperty: Element {
    get {
      return self[0]
    }
    set {
      self[0] = newValue
    }
  }

  // CHECK-LABEL: sil @_T0Sa22constrained_extensionsSiRszlE14instanceMethodSiyF : $@convention(method) (@guaranteed Array<Int>) -> Int
  public func instanceMethod() -> Element {
    return instanceProperty
  }

  // CHECK-LABEL: sil @_T0Sa22constrained_extensionsSiRszlE14instanceMethodS2i1e_tF : $@convention(method) (Int, @guaranteed Array<Int>) -> Int
  public func instanceMethod(e: Element) -> Element {
    return e
  }

  // CHECK-LABEL: sil @_T0Sa22constrained_extensionsSiRszlE14staticPropertySivgZ : $@convention(method) (@thin Array<Int>.Type) -> Int
  public static var staticProperty: Element {
    return Array(x: ()).instanceProperty
  }

  // CHECK-LABEL: sil @_T0Sa22constrained_extensionsSiRszlE12staticMethodSiyFZ : $@convention(method) (@thin Array<Int>.Type) -> Int
  public static func staticMethod() -> Element {
    return staticProperty
  }

  // CHECK-LABEL: sil @_T0Sa22constrained_extensionsSiRszlE12staticMethodS2iSg1e_tFZfA_ : $@convention(thin) () -> Optional<Int>
  // CHECK-LABEL: sil @_T0Sa22constrained_extensionsSiRszlE12staticMethodS2iSg1e_tFZ : $@convention(method) (Optional<Int>, @thin Array<Int>.Type) -> Int
  public static func staticMethod(e: Element? = nil) -> Element {
    return e!
  }

  // CHECK-LABEL: sil @_T0Sa22constrained_extensionsSiRszlESiyt_tcig : $@convention(method) (@guaranteed Array<Int>) -> Int
  public subscript(i: ()) -> Element {
    return self[0]
  }

  // CHECK-LABEL: sil @_T0Sa22constrained_extensionsSiRszlE21inoutAccessOfPropertyyyF : $@convention(method) (@inout Array<Int>) -> ()
  public mutating func inoutAccessOfProperty() {
    func increment(x: inout Element) {
      x += 1
    }

    increment(x: &instanceProperty)
  }
}

extension Dictionary where Key == Int {
  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlEABySiq_Gyt1x_tcfC : $@convention(method) <Key, Value where Key == Int> (@thin Dictionary<Int, Value>.Type) -> @owned Dictionary<Int, Value> {
  public init(x: ()) {
    self.init()
  }

  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlE16instancePropertyq_vg : $@convention(method) <Key, Value where Key == Int> (@guaranteed Dictionary<Int, Value>) -> @out Value
  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlE16instancePropertyq_vs : $@convention(method) <Key, Value where Key == Int> (@in Value, @inout Dictionary<Int, Value>) -> ()
  // CHECK-LABEL: sil shared [transparent] [serialized] @_T0s10DictionaryV22constrained_extensionsSiRszrlE16instancePropertyq_vmytfU_ : $@convention(method) <Key, Value where Key == Int> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout Dictionary<Int, Value>, @thick Dictionary<Int, Value>.Type) -> ()
  // CHECK-LABEL: sil [transparent] [serialized] @_T0s10DictionaryV22constrained_extensionsSiRszrlE16instancePropertyq_vm : $@convention(method) <Key, Value where Key == Int> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout Dictionary<Int, Value>) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>)
  public var instanceProperty: Value {
    get {
      return self[0]!
    }
    set {
      self[0] = newValue
    }
  }

  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlE14instanceMethodq_yF : $@convention(method) <Key, Value where Key == Int> (@guaranteed Dictionary<Int, Value>) -> @out Value
  public func instanceMethod() -> Value {
    return instanceProperty
  }

  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlE14instanceMethodq_q_1v_tF : $@convention(method) <Key, Value where Key == Int> (@in Value, @guaranteed Dictionary<Int, Value>) -> @out Value
  public func instanceMethod(v: Value) -> Value {
    return v
  }

  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlE12staticMethodSiyFZ : $@convention(method) <Key, Value where Key == Int> (@thin Dictionary<Int, Value>.Type) -> Int
  public static func staticMethod() -> Key {
    return staticProperty
  }

  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlE14staticPropertySivgZ : $@convention(method) <Key, Value where Key == Int> (@thin Dictionary<Int, Value>.Type) -> Int
  public static var staticProperty: Key {
    return 0
  }

  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlE12staticMethodq_SiSg1k_q_Sg1vtFZfA_ : $@convention(thin) <Key, Value where Key == Int> () -> Optional<Int>
  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlE12staticMethodq_SiSg1k_q_Sg1vtFZfA0_ : $@convention(thin) <Key, Value where Key == Int> () -> @out Optional<Value>
  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlE12staticMethodq_SiSg1k_q_Sg1vtFZ : $@convention(method) <Key, Value where Key == Int> (Optional<Int>, @in Optional<Value>, @thin Dictionary<Int, Value>.Type) -> @out Value
  public static func staticMethod(k: Key? = nil, v: Value? = nil) -> Value {
    return v!
  }

  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlE17callsStaticMethodq_yFZ : $@convention(method) <Key, Value where Key == Int> (@thin Dictionary<Int, Value>.Type) -> @out Value
  public static func callsStaticMethod() -> Value {
    return staticMethod()
  }

  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlE16callsConstructorq_yFZ : $@convention(method) <Key, Value where Key == Int> (@thin Dictionary<Int, Value>.Type) -> @out Value
  public static func callsConstructor() -> Value {
    return Dictionary(x: ()).instanceMethod()
  }

  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlEq_yt_tcig : $@convention(method) <Key, Value where Key == Int> (@guaranteed Dictionary<Int, Value>) -> @out Value
  public subscript(i: ()) -> Value {
    return self[0]!
  }

  // CHECK-LABEL: sil @_T0s10DictionaryV22constrained_extensionsSiRszrlE21inoutAccessOfPropertyyyF : $@convention(method) <Key, Value where Key == Int> (@inout Dictionary<Int, Value>) -> ()
  public mutating func inoutAccessOfProperty() {
    func increment(x: inout Value) { }

    increment(x: &instanceProperty)
  }
}

public class GenericClass<X, Y> {}

extension GenericClass where Y == () {
  // CHECK-LABEL: sil @_T022constrained_extensions12GenericClassCAAytRs_rlE5valuexvg : $@convention(method) <X, Y where Y == ()> (@guaranteed GenericClass<X, ()>) -> @out X
  // CHECK-LABEL: sil @_T022constrained_extensions12GenericClassCAAytRs_rlE5valuexvs : $@convention(method) <X, Y where Y == ()> (@in X, @guaranteed GenericClass<X, ()>) -> ()
  // CHECK-LABEL: sil shared [transparent] [serialized] @_T022constrained_extensions12GenericClassCAAytRs_rlE5valuexvmytfU_ : $@convention(method) <X, Y where Y == ()> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout GenericClass<X, ()>, @thick GenericClass<X, ()>.Type) -> ()
  // CHECK-LABEL: sil [transparent] [serialized] @_T022constrained_extensions12GenericClassCAAytRs_rlE5valuexvm : $@convention(method) <X, Y where Y == ()> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @guaranteed GenericClass<X, ()>) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>)
  public var value: X {
    get { while true {} }
    set {}
  }

  // CHECK-LABEL: sil @_T022constrained_extensions12GenericClassCAAytRs_rlE5emptyytvg : $@convention(method) <X, Y where Y == ()> (@guaranteed GenericClass<X, ()>) -> ()
  // CHECK-LABEL: sil @_T022constrained_extensions12GenericClassCAAytRs_rlE5emptyytvs : $@convention(method) <X, Y where Y == ()> (@guaranteed GenericClass<X, ()>) -> ()
  // CHECK-LABEL: sil shared [transparent] [serialized] @_T022constrained_extensions12GenericClassCAAytRs_rlE5emptyytvmytfU_ : $@convention(method) <X, Y where Y == ()> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout GenericClass<X, ()>, @thick GenericClass<X, ()>.Type) -> ()
  // CHECK-LABEL: sil [transparent] [serialized] @_T022constrained_extensions12GenericClassCAAytRs_rlE5emptyytvm : $@convention(method) <X, Y where Y == ()> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @guaranteed GenericClass<X, ()>) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>)
  public var empty: Y {
    get { return () }
    set {}
  }

  // CHECK-LABEL: sil @_T022constrained_extensions12GenericClassCAAytRs_rlExyt_tcig : $@convention(method) <X, Y where Y == ()> (@guaranteed GenericClass<X, ()>) -> @out X
  // CHECK-LABEL: sil @_T022constrained_extensions12GenericClassCAAytRs_rlExyt_tcis : $@convention(method) <X, Y where Y == ()> (@in X, @guaranteed GenericClass<X, ()>) -> ()
  // CHECK-LABEL: sil shared [transparent] [serialized] @_T022constrained_extensions12GenericClassCAAytRs_rlExyt_tcimytfU_ : $@convention(method) <X, Y where Y == ()> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout GenericClass<X, ()>, @thick GenericClass<X, ()>.Type) -> ()
  // CHECK-LABEL: sil [transparent] [serialized] @_T022constrained_extensions12GenericClassCAAytRs_rlExyt_tcim : $@convention(method) <X, Y where Y == ()> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @guaranteed GenericClass<X, ()>) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>)
  public subscript(_: Y) -> X {
    get { while true {} }
    set {}
  }

  // CHECK-LABEL: sil @_T022constrained_extensions12GenericClassCAAytRs_rlEyxcig : $@convention(method) <X, Y where Y == ()> (@in X, @guaranteed GenericClass<X, ()>) -> ()
  // CHECK-LABEL: sil @_T022constrained_extensions12GenericClassCAAytRs_rlEyxcis : $@convention(method) <X, Y where Y == ()> (@in X, @guaranteed GenericClass<X, ()>) -> ()
  // CHECK-LABEL: sil shared [transparent] [serialized] @_T022constrained_extensions12GenericClassCAAytRs_rlEyxcimytfU_ : $@convention(method) <X, Y where Y == ()> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout GenericClass<X, ()>, @thick GenericClass<X, ()>.Type) -> ()
  // CHECK-LABEL: sil [transparent] [serialized] @_T022constrained_extensions12GenericClassCAAytRs_rlEyxcim : $@convention(method) <X, Y where Y == ()> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @in X, @guaranteed GenericClass<X, ()>) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>)
  public subscript(_: X) -> Y {
    get { while true {} }
    set {}
  }
}

protocol VeryConstrained {}

struct AnythingGoes<T> {
  // CHECK-LABEL: sil hidden [transparent] @_T022constrained_extensions12AnythingGoesV13meaningOfLifexSgvpfi : $@convention(thin) <T> () -> @out Optional<T>
  var meaningOfLife: T? = nil
}

extension AnythingGoes where T : VeryConstrained {
  // CHECK-LABEL: sil hidden @_T022constrained_extensions12AnythingGoesVA2A15VeryConstrainedRzlEACyxGyt13fromExtension_tcfC : $@convention(method) <T where T : VeryConstrained> (@thin AnythingGoes<T>.Type) -> @out AnythingGoes<T> {

  // CHECK: [[INIT:%.*]] = function_ref @_T022constrained_extensions12AnythingGoesV13meaningOfLifexSgvpfi : $@convention(thin) <τ_0_0> () -> @out Optional<τ_0_0>
  // CHECK: [[RESULT:%.*]] = alloc_stack $Optional<T>
  // CHECK: apply [[INIT]]<T>([[RESULT]]) : $@convention(thin) <τ_0_0> () -> @out Optional<τ_0_0>
  // CHECK: return
  init(fromExtension: ()) {}
}

extension Array where Element == Int {
  struct Nested {
    // CHECK-LABEL: sil hidden [transparent] @_T0Sa22constrained_extensionsSiRszlE6NestedV1eSiSgvpfi : $@convention(thin) () -> Optional<Int>
    var e: Element? = nil

    // CHECK-LABEL: sil hidden @_T0Sa22constrained_extensionsSiRszlE6NestedV10hasDefaultySiSg1e_tFfA_ : $@convention(thin) () -> Optional<Int>
    // CHECK-LABEL: sil hidden @_T0Sa22constrained_extensionsSiRszlE6NestedV10hasDefaultySiSg1e_tF : $@convention(method) (Optional<Int>, @inout Array<Int>.Nested) -> ()
    mutating func hasDefault(e: Element? = nil) {
      self.e = e
    }
  }
}

extension Array where Element == AnyObject {
  class NestedClass {
    // CHECK-LABEL: sil hidden @_T0Sa22constrained_extensionsyXlRszlE11NestedClassCfd : $@convention(method) (@guaranteed Array<AnyObject>.NestedClass) -> @owned Builtin.NativeObject
    // CHECK-LABEL: sil hidden @_T0Sa22constrained_extensionsyXlRszlE11NestedClassCfD : $@convention(method) (@owned Array<AnyObject>.NestedClass) -> ()
    deinit { }

    // CHECK-LABEL: sil hidden @_T0Sa22constrained_extensionsyXlRszlE11NestedClassCACyyXl_GycfC : $@convention(method) (@thick Array<AnyObject>.NestedClass.Type) -> @owned Array<AnyObject>.NestedClass
    // CHECK-LABEL: sil hidden @_T0Sa22constrained_extensionsyXlRszlE11NestedClassCACyyXl_Gycfc : $@convention(method) (@owned Array<AnyObject>.NestedClass) -> @owned Array<AnyObject>.NestedClass
  }

  class DerivedClass : NestedClass {
    // CHECK-LABEL: sil hidden [transparent] @_T0Sa22constrained_extensionsyXlRszlE12DerivedClassC1eyXlSgvpfi : $@convention(thin) () -> @owned Optional<AnyObject>
    // CHECK-LABEL: sil hidden @_T0Sa22constrained_extensionsyXlRszlE12DerivedClassCfE : $@convention(method) (@guaranteed Array<AnyObject>.DerivedClass) -> ()
    var e: Element? = nil
  }
}

func referenceNestedTypes() {
  _ = Array<AnyObject>.NestedClass()
  _ = Array<AnyObject>.DerivedClass()
}
