// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -enable-sil-ownership -emit-silgen -swift-version 3 %s | %FileCheck %s
// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -enable-sil-ownership -emit-silgen -swift-version 3 %s | %FileCheck %s --check-prefix=NEGATIVE

// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -enable-sil-ownership -emit-silgen -swift-version 4 %s | %FileCheck %s
// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -enable-sil-ownership -emit-silgen -swift-version 4 %s | %FileCheck %s --check-prefix=NEGATIVE

// __FUNCTION__ used as top-level parameter produces the module name.
// CHECK-LABEL: sil @main
// CHECK:         string_literal utf16 "default_arguments"

// Default argument for first parameter.
// CHECK-LABEL: sil hidden @_T017default_arguments7defarg1ySi1i_Sd1dSS1stFfA_ : $@convention(thin) () -> Int
// CHECK: [[INT:%[0-9]+]] = metatype $@thin Int.Type
// CHECK: [[LIT:%[0-9]+]] = integer_literal $Builtin.Int2048, 17
// CHECK: [[CVT:%[0-9]+]] = function_ref @_T0S2i{{[_0-9a-zA-Z]*}}fC
// CHECK: [[RESULT:%[0-9]+]] = apply [[CVT]]([[LIT]], [[INT]]) : $@convention(method) (Builtin.Int2048, @thin Int.Type) -> Int
// CHECK: return [[RESULT]] : $Int

// Default argument for third parameter.
// CHECK-LABEL: sil hidden @_T017default_arguments7defarg1ySi1i_Sd1dSS1stFfA1_ : $@convention(thin) () -> @owned String
// CHECK: [[LIT:%[0-9]+]] = string_literal utf8 "Hello"
// CHECK: [[LEN:%[0-9]+]] = integer_literal $Builtin.Word, 5
// CHECK: [[STRING:%[0-9]+]] = metatype $@thin String.Type
// CHECK: [[CVT:%[0-9]+]] = function_ref @_T0S2S{{[_0-9a-zA-Z]*}}fC
// CHECK: [[RESULT:%[0-9]+]] = apply [[CVT]]([[LIT]], [[LEN]], {{[^,]+}}, [[STRING]]) : $@convention(method)
// CHECK: return [[RESULT]] : $String
func defarg1(i: Int = 17, d: Double, s: String = "Hello") { }

// CHECK-LABEL: sil hidden @_T017default_arguments15testDefaultArg1yyF
func testDefaultArg1() {
  // CHECK: [[FLOAT64:%[0-9]+]] = metatype $@thin Double.Type
  // CHECK: [[FLOATLIT:%[0-9]+]] = float_literal $Builtin.FPIEEE{{64|80}}, {{0x4009000000000000|0x4000C800000000000000}}
  // CHECK: [[LITFN:%[0-9]+]] = function_ref @_T0S2d{{[_0-9a-zA-Z]*}}fC
  // CHECK: [[FLOATVAL:%[0-9]+]] = apply [[LITFN]]([[FLOATLIT]], [[FLOAT64]])
  // CHECK: [[DEF0FN:%[0-9]+]] = function_ref @_T017default_arguments7defarg1{{.*}}A_
  // CHECK: [[DEF0:%[0-9]+]] = apply [[DEF0FN]]()
  // CHECK: [[DEF2FN:%[0-9]+]] = function_ref @_T017default_arguments7defarg1{{.*}}A1_
  // CHECK: [[DEF2:%[0-9]+]] = apply [[DEF2FN]]()
  // CHECK: [[FNREF:%[0-9]+]] = function_ref @_T017default_arguments7defarg1{{[_0-9a-zA-Z]*}}F
  // CHECK: apply [[FNREF]]([[DEF0]], [[FLOATVAL]], [[DEF2]])
  defarg1(d:3.125)
}

func defarg2(_ i: Int, d: Double = 3.125, s: String = "Hello") { }

// CHECK-LABEL: sil hidden @_T017default_arguments15testDefaultArg2{{[_0-9a-zA-Z]*}}F
func testDefaultArg2() {
// CHECK:  [[INT64:%[0-9]+]] = metatype $@thin Int.Type
// CHECK:  [[INTLIT:%[0-9]+]] = integer_literal $Builtin.Int2048, 5
// CHECK:  [[LITFN:%[0-9]+]] = function_ref @_T0S2i{{[_0-9a-zA-Z]*}}fC
// CHECK:  [[I:%[0-9]+]] = apply [[LITFN]]([[INTLIT]], [[INT64]]) : $@convention(method) (Builtin.Int2048, @thin Int.Type) -> Int
// CHECK:  [[DFN:%[0-9]+]] = function_ref @_T017default_arguments7defarg2{{.*}}A0_ : $@convention(thin) () -> Double
// CHECK:  [[D:%[0-9]+]] = apply [[DFN]]() : $@convention(thin) () -> Double
// CHECK:  [[SFN:%[0-9]+]] = function_ref @_T017default_arguments7defarg2{{.*}}A1_ : $@convention(thin) () -> @owned String
// CHECK:  [[S:%[0-9]+]] = apply [[SFN]]() : $@convention(thin) () -> @owned String
// CHECK:  [[FNREF:%[0-9]+]] = function_ref @_T017default_arguments7defarg2{{[_0-9a-zA-Z]*}}F : $@convention(thin) (Int, Double, @owned String) -> ()
// CHECK:  apply [[FNREF]]([[I]], [[D]], [[S]]) : $@convention(thin) (Int, Double, @owned String) -> ()
  defarg2(5)
}

func autocloseFile(x: @autoclosure () -> String = #file,
                   y: @autoclosure () -> Int = #line) { }
// CHECK-LABEL: sil hidden @_T017default_arguments17testAutocloseFileyyF
func testAutocloseFile() {
  // CHECK-LABEL: sil private [transparent] @_T017default_arguments17testAutocloseFileyyFSSyXKfu_ : $@convention(thin) () -> @owned String
  // CHECK: string_literal utf16{{.*}}default_arguments.swift

  // CHECK-LABEL: sil private [transparent] @_T017default_arguments17testAutocloseFileyyFSiyXKfu0_ : $@convention(thin) () -> Int
  // CHECK: integer_literal $Builtin.Int2048, [[@LINE+1]]
  autocloseFile()
}

func testMagicLiterals(file: String = #file,
                       function: String = #function,
                       line: Int = #line,
                       column: Int = #column) {}

// Check that default argument generator functions don't leak information about
// user's source.
//
// NEGATIVE-NOT: sil hidden @_T017default_arguments17testMagicLiteralsySS4file_SS8functionSi4lineSi6columntFfA_
//
// NEGATIVE-NOT: sil hidden @_T017default_arguments17testMagicLiteralsySS4file_SS8functionSi4lineSi6columntFfA0_
//
// NEGATIVE-NOT: sil hidden @_T017default_arguments17testMagicLiteralsySS4file_SS8functionSi4lineSi6columntFfA1_
//
// NEGATIVE-NOT: sil hidden @_T017default_arguments17testMagicLiteralsySS4file_SS8functionSi4lineSi6columntFfA2_

func closure(_: () -> ()) {}
func autoclosure(_: @autoclosure () -> ()) {}

// CHECK-LABEL: sil hidden @_T017default_arguments25testCallWithMagicLiteralsyyF
// CHECK:         string_literal utf16 "testCallWithMagicLiterals()"
// CHECK:         string_literal utf16 "testCallWithMagicLiterals()"
// CHECK-LABEL: sil private @_T017default_arguments25testCallWithMagicLiteralsyyFyycfU_
// CHECK:         string_literal utf16 "testCallWithMagicLiterals()"
// CHECK-LABEL: sil private [transparent] @_T017default_arguments25testCallWithMagicLiteralsyyFyyXKfu_
// CHECK:         string_literal utf16 "testCallWithMagicLiterals()"
func testCallWithMagicLiterals() {
  testMagicLiterals()
  testMagicLiterals()
  closure { testMagicLiterals() }
  autoclosure(testMagicLiterals())
}

// CHECK-LABEL: sil hidden @_T017default_arguments25testPropWithMagicLiteralsSivg
// CHECK:         string_literal utf16 "testPropWithMagicLiterals"
var testPropWithMagicLiterals: Int {
  testMagicLiterals()
  closure { testMagicLiterals() }
  autoclosure(testMagicLiterals())
  return 0
}

class Foo {

  // CHECK-LABEL: sil hidden @_T017default_arguments3FooCACSi3int_SS6stringtcfc : $@convention(method) (Int, @owned String, @owned Foo) -> @owned Foo
  // CHECK:         string_literal utf16 "init(int:string:)"
  init(int: Int, string: String = #function) {
    testMagicLiterals()
    closure { testMagicLiterals() }
    autoclosure(testMagicLiterals())
  }

  // CHECK-LABEL: sil hidden @_T017default_arguments3FooCfd
  // CHECK:         string_literal utf16 "deinit"
  deinit {
    testMagicLiterals()
    closure { testMagicLiterals() }
    autoclosure(testMagicLiterals())
  }

  // CHECK-LABEL: sil hidden @_T017default_arguments3FooCS2icig
  // CHECK:         string_literal utf16 "subscript"
  subscript(x: Int) -> Int {
    testMagicLiterals()
    closure { testMagicLiterals() }
    autoclosure(testMagicLiterals())
    return x
  }
 
  // CHECK-LABEL: sil private @globalinit_33_E52D764B1F2009F2390B2B8DF62DAEB8_func0
  // CHECK:         string_literal utf16 "Foo" 
  static let x = Foo(int:0)

}

// Test at top level.
testMagicLiterals()
closure { testMagicLiterals() }
autoclosure(testMagicLiterals())

// CHECK: string_literal utf16 "default_arguments"
let y : String = #function 

// CHECK-LABEL: sil hidden @_T017default_arguments16testSelectorCallySi_Si17withMagicLiteralstF
// CHECK:         string_literal utf16 "testSelectorCall(_:withMagicLiterals:)"
func testSelectorCall(_ x: Int, withMagicLiterals y: Int) {
  testMagicLiterals()
}

// CHECK-LABEL: sil hidden @_T017default_arguments32testSelectorCallWithUnnamedPieceySi_SitF
// CHECK:         string_literal utf16 "testSelectorCallWithUnnamedPiece"
func testSelectorCallWithUnnamedPiece(_ x: Int, _ y: Int) {
  testMagicLiterals()
}

// Test default arguments in an inherited subobject initializer
class SuperDefArg {
  init(int i: Int = 10) { }
}

// CHECK: sil hidden @_T017default_arguments11SuperDefArgCACSi3int_tcfcfA_ : $@convention(thin) () -> Int

// CHECK-NOT: sil hidden @_T017default_arguments9SubDefArgCACSi3int_tcfcfA_ : $@convention(thin) () -> Int

class SubDefArg : SuperDefArg { }

// CHECK: sil hidden @_T017default_arguments13testSubDefArgAA0deF0CyF : $@convention(thin) () -> @owned SubDefArg
func testSubDefArg() -> SubDefArg {
  // CHECK: function_ref @_T017default_arguments11SuperDefArgCACSi3int_tcfcfA_
  // CHECK: function_ref @_T017default_arguments9SubDefArgC{{[_0-9a-zA-Z]*}}fC
  // CHECK: return
  return SubDefArg()
}

// CHECK-NOT: sil hidden @_T017default_arguments9SubDefArgCACSi3int_tcfcfA_ : $@convention(thin) () -> Int

// <rdar://problem/17379550>
func takeDefaultArgUnnamed(_ x: Int = 5) { }

// CHECK-LABEL: sil hidden @_T017default_arguments25testTakeDefaultArgUnnamed{{[_0-9a-zA-Z]*}}F
func testTakeDefaultArgUnnamed(_ i: Int) {
  // CHECK: bb0([[I:%[0-9]+]] : @trivial $Int):
  // CHECK:   [[FN:%[0-9]+]] = function_ref @_T017default_arguments21takeDefaultArgUnnamedySiF : $@convention(thin) (Int) -> ()
  // CHECK:   apply [[FN]]([[I]]) : $@convention(thin) (Int) -> ()
  takeDefaultArgUnnamed(i)
}

func takeDSOHandle(_ handle: UnsafeRawPointer = #dsohandle) { }

// CHECK-LABEL: sil hidden @_T017default_arguments13testDSOHandleyyF
func testDSOHandle() {
  // CHECK: [[DSO_HANDLE:%[0-9]+]] = global_addr @__dso_handle : $*Builtin.RawPointer
  takeDSOHandle()
}

// Test __FUNCTION__ in an extension initializer. rdar://problem/19792181
extension SuperDefArg {
  static let extensionInitializerWithClosure: Int = { return 22 }()
}


// <rdar://problem/19086357> SILGen crashes reabstracting default argument closure in members
class ReabstractDefaultArgument<T> {
  init(a: (T, T) -> Bool = { _, _ in true }) {
  }
}

// CHECK-LABEL: sil hidden @_T017default_arguments32testDefaultArgumentReabstractionyyF
// function_ref default_arguments.ReabstractDefaultArgument.__allocating_init <A>(default_arguments.ReabstractDefaultArgument<A>.Type)(a : (A, A) -> Swift.Bool) -> default_arguments.ReabstractDefaultArgument<A>
// CHECK: [[FN:%.*]] = function_ref @_T017default_arguments25ReabstractDefaultArgument{{.*}} : $@convention(thin) <τ_0_0> () -> @owned @noescape @callee_guaranteed (@in τ_0_0, @in τ_0_0) -> Bool
// CHECK-NEXT: [[RESULT:%.*]] = apply [[FN]]<Int>() : $@convention(thin) <τ_0_0> () -> @owned @noescape @callee_guaranteed (@in τ_0_0, @in τ_0_0) -> Bool
// CHECK-NEXT: function_ref reabstraction thunk helper from @callee_guaranteed (@in Swift.Int, @in Swift.Int) -> (@unowned Swift.Bool) to @callee_guaranteed (@unowned Swift.Int, @unowned Swift.Int) -> (@unowned Swift.Bool)
// CHECK-NEXT: [[THUNK:%.*]] = function_ref @_T0S2iSbIgiid_S2iSbIgyyd_TR : $@convention(thin) (Int, Int, @guaranteed @noescape @callee_guaranteed (@in Int, @in Int) -> Bool) -> Bool
// CHECK-NEXT: [[FN:%.*]] = partial_apply [callee_guaranteed] [[THUNK]]([[RESULT]]) : $@convention(thin) (Int, Int, @guaranteed @noescape @callee_guaranteed (@in Int, @in Int) -> Bool) -> Bool
// CHECK-NEXT: [[CONV_FN:%.*]] = convert_function [[FN]]
// function_ref reabstraction thunk helper from @callee_guaranteed (@unowned Swift.Int, @unowned Swift.Int) -> (@unowned Swift.Bool) to @callee_guaranteed (@in Swift.Int, @in Swift.Int) -> (@unowned Swift.Bool)
// CHECK: [[THUNK:%.*]] = function_ref @_T0S2iSbIgyyd_S2iSbIgiid_TR : $@convention(thin) (@in Int, @in Int, @guaranteed @noescape @callee_guaranteed (Int, Int) -> Bool) -> Bool
// CHECK-NEXT: [[FN:%.*]] = partial_apply [callee_guaranteed] [[THUNK]]([[CONV_FN]]) : $@convention(thin) (@in Int, @in Int, @guaranteed @noescape @callee_guaranteed (Int, Int) -> Bool) -> Bool
// CHECK-NEXT: [[CONV_FN:%.*]] = convert_function [[FN]]
// CHECK: [[INITFN:%[0-9]+]] = function_ref @_T017default_arguments25ReabstractDefaultArgumentC{{[_0-9a-zA-Z]*}}fC
// CHECK-NEXT: apply [[INITFN]]<Int>([[CONV_FN]], 

func testDefaultArgumentReabstraction() {
  _ = ReabstractDefaultArgument<Int>()
}

// <rdar://problem/20494437> SILGen crash handling default arguments
// CHECK-LABEL: sil hidden @_T017default_arguments18r20494437onSuccessyAA25r20494437ExecutionContext_pF
// CHECK: function_ref @_T017default_arguments19r20494437onCompleteyAA25r20494437ExecutionContext_pF
// <rdar://problem/20494437> SILGen crash handling default arguments
protocol r20494437ExecutionContext {}
let r20494437Default: r20494437ExecutionContext
func r20494437onComplete(_ executionContext: r20494437ExecutionContext = r20494437Default) {}
func r20494437onSuccess(_ a: r20494437ExecutionContext) {
  r20494437onComplete(a)
}

// <rdar://problem/18400194> Parenthesized function expression crashes the compiler
func r18400194(_ a: Int, x: Int = 97) {}

// CHECK-LABEL: sil hidden @_T017default_arguments9r18400194ySi_Si1xtFfA0_
// CHECK: integer_literal $Builtin.Int2048, 97

// CHECK-LABEL: sil hidden @_T017default_arguments14test_r18400194yyF
// CHECK: integer_literal $Builtin.Int2048, 1
// CHECK:  function_ref @_T017default_arguments9r18400194ySi_Si1xtFfA0_ : $@convention(thin) () -> Int
// CHECK: function_ref @_T017default_arguments9r18400194ySi_Si1xtF : $@convention(thin) (Int, Int) -> (){{.*}}
func test_r18400194() {
  (r18400194)(1)
}

// rdar://24242783
//   Don't add capture arguments to local default argument generators.
func localFunctionWithDefaultArg() {
  var z = 5
  func bar(_ x: Int? = nil) {
    z += 1
  }
  bar()
}
// CHECK-LABEL: sil private @_T017default_arguments27localFunctionWithDefaultArgyyF3barL_ySiSgFfA_
// CHECK-SAME: $@convention(thin) () -> Optional<Int>
