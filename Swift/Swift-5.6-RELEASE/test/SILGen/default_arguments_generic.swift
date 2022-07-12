
// RUN: %target-swift-emit-silgen -module-name default_arguments_generic -swift-version 4 %s | %FileCheck %s

func foo<T: ExpressibleByIntegerLiteral>(_: T.Type, x: T = 0) { }

struct Zim<T: ExpressibleByIntegerLiteral> {
  init(x: T = 0) { }
  init<U: ExpressibleByFloatLiteral>(_ x: T = 0, y: U = 0.5) { }

  static func zim(x: T = 0) { }
  static func zang<U: ExpressibleByFloatLiteral>(_: U.Type, _ x: T = 0, y: U = 0.5) { }
}

// CHECK-LABEL: sil hidden [ossa] @$s25default_arguments_generic3baryyF : $@convention(thin) () -> () {
func bar() {
  // CHECK: [[FOO_DFLT:%.*]] = function_ref @$s25default_arguments_generic3foo
  // CHECK: apply [[FOO_DFLT]]<Int>
  foo(Int.self)
  // CHECK: [[ZIM_DFLT:%.*]] = function_ref @$s25default_arguments_generic3ZimV3zim
  // CHECK: apply [[ZIM_DFLT]]<Int>
  Zim<Int>.zim()
  // CHECK: [[ZANG_DFLT_0:%.*]] = function_ref @$s25default_arguments_generic3ZimV4zang{{.*}}A0_
  // CHECK: apply [[ZANG_DFLT_0]]<Int, Double>
  // CHECK: [[ZANG_DFLT_1:%.*]] = function_ref @$s25default_arguments_generic3ZimV4zang{{.*}}A1_
  // CHECK: apply [[ZANG_DFLT_1]]<Int, Double>
  Zim<Int>.zang(Double.self)
  // CHECK: [[ZANG_DFLT_1:%.*]] = function_ref @$s25default_arguments_generic3ZimV4zang{{.*}}A1_
  // CHECK: apply [[ZANG_DFLT_1]]<Int, Double>
  Zim<Int>.zang(Double.self, 22)
}

protocol Initializable {
  init()
}
struct Generic<T: Initializable> {
  init(_ value: T = T()) {}
}
struct InitializableImpl: Initializable {
  init() {}
}
// CHECK-LABEL: sil hidden [ossa] @$s25default_arguments_generic17testInitializableyyF
func testInitializable() {
  // Previously the metatype construction crashed in the type checker
  // and the ".init" form crashed in SILGen. Test both forms.

  // CHECK: function_ref @$s25default_arguments_generic7GenericVyACyxGxcfcfA_ : $@convention(thin) <τ_0_0 where τ_0_0 : Initializable> () -> @out τ_0_0
  // CHECK: [[INIT:%.+]] = function_ref @$s25default_arguments_generic7GenericVyACyxGxcfC
  // CHECK: apply [[INIT]]<InitializableImpl>({{%.+}}, {{%.+}}) : $@convention(method) <τ_0_0 where τ_0_0 : Initializable> (@in τ_0_0, @thin Generic<τ_0_0>.Type) -> Generic<τ_0_0>
  _ = Generic<InitializableImpl>()

  // CHECK: function_ref @$s25default_arguments_generic7GenericVyACyxGxcfcfA_ : $@convention(thin) <τ_0_0 where τ_0_0 : Initializable> () -> @out τ_0_0
  // CHECK: [[INIT:%.+]] = function_ref @$s25default_arguments_generic7GenericVyACyxGxcfC
  // CHECK: apply [[INIT]]<InitializableImpl>({{%.+}}, {{%.+}}) : $@convention(method) <τ_0_0 where τ_0_0 : Initializable> (@in τ_0_0, @thin Generic<τ_0_0>.Type) -> Generic<τ_0_0>
  _ = Generic<InitializableImpl>.init()

} // CHECK: end sil function '$s25default_arguments_generic17testInitializableyyF'

// Local generic functions with default arguments

// CHECK-LABEL: sil hidden [ossa] @$s25default_arguments_generic5outer1tyx_tlF : $@convention(thin) <T> (@in_guaranteed T) -> ()
func outer<T>(t: T) {
  func inner1(x: Int = 0) {}

  // CHECK: [[ARG_GENERATOR:%.*]] = function_ref @$s25default_arguments_generic5outer1tyx_tlF6inner1L_1xySi_tlFfA_ : $@convention(thin) () -> Int
  // CHECK: [[ARG:%.*]] = apply [[ARG_GENERATOR]]() : $@convention(thin) () -> Int
  _ = inner1()

  func inner2(x: Int = 0) { _ = T.self }

  // CHECK: [[ARG_GENERATOR:%.*]] = function_ref @$s25default_arguments_generic5outer1tyx_tlF6inner2L_1xySi_tlFfA_ : $@convention(thin) () -> Int
  // CHECK: [[ARG:%.*]] = apply [[ARG_GENERATOR]]() : $@convention(thin) () -> Int
  _ = inner2()
}

protocol StaticIntValue {
  static var intValue: Int { get }
}

func f<T : StaticIntValue>(_: T) {
  // CHECK-LABEL: sil private [ossa] @$s25default_arguments_generic1fyyxAA14StaticIntValueRzlF5innerL_1xySi_tAaCRzlFfA_ : $@convention(thin) <T where T : StaticIntValue> () -> Int
  // CHECK-LABEL: sil private [ossa] @$s25default_arguments_generic1fyyxAA14StaticIntValueRzlF5innerL_1xySi_tAaCRzlF : $@convention(thin) <T where T : StaticIntValue> (Int) -> ()
  func inner(x: Int = T.intValue) {}

  // CHECK-LABEL: sil private [ossa] @$s25default_arguments_generic1fyyxAA14StaticIntValueRzlF5otherL_yyAaCRzlF : $@convention(thin) <T where T : StaticIntValue> () -> ()
  func other() { inner() }
}

func g<T>(_: T) {
  { inner() }()

  func inner() {
    _ = T.self
  }
}
