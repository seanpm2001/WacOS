// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -sdk %S/Inputs -I %S/Inputs -enable-source-import %s -emit-silgen | %FileCheck %s

// REQUIRES: objc_interop

import gizmo

// Test mangling of Unicode identifiers.
// These examples are from RFC 3492, which defines the Punycode encoding used
// by name mangling.

// CHECK-LABEL: sil hidden @_TF8manglingX22egbpdajGbuEbxfgehfvwxnFT_T_
func ليهمابتكلموشعربي؟() { }
// CHECK-LABEL: sil hidden @_TF8manglingX24ihqwcrbEcvIaIdqgAFGpqjyeFT_T_
func 他们为什么不说中文() { }
// CHECK-LABEL: sil hidden @_TF8manglingX27ihqwctvzcJBfGFJdrssDxIboAybFT_T_
func 他們爲什麽不說中文() { }
// CHECK-LABEL: sil hidden @_TF8manglingX30Proprostnemluvesky_uybCEdmaEBaFT_T_
func Pročprostěnemluvíčesky() { }

// <rdar://problem/13757744> Variadic tuples need a different mangling from
// non-variadic tuples.

// CHECK-LABEL: sil hidden @_TF8mangling9r13757744FT1xGSaSi__T_
func r13757744(x x: [Int]) {}
// CHECK-LABEL: sil hidden @_TF8mangling9r13757744Ft1xGSaSi__T_
func r13757744(x x: Int...) {}

// <rdar://problem/13757750> Prefix, postfix, and infix operators need
// distinct manglings.

prefix operator +- {}
postfix operator +- {}
infix operator +- {}

// CHECK-LABEL: sil hidden @_TF8manglingop2psurFxT_
prefix func +- <T>(a: T) {}
// CHECK-LABEL: sil hidden @_TF8manglingoP2psurFxT_
postfix func +- <T>(a: T) {}

// CHECK-LABEL: sil hidden @_TF8manglingoi2psurFTxx_T_
func +- <T>(a: T, b: T) {}

// CHECK-LABEL: sil hidden @_TF8manglingop2psurFT1ax1bx_T_
prefix func +- <T>(_: (a: T, b: T)) {}
// CHECK-LABEL: sil hidden @_TF8manglingoP2psurFT1ax1bx_T_
postfix func +- <T>(_: (a: T, b: T)) {}

infix operator «+» {}

// CHECK-LABEL: sil hidden @_TF8manglingXoi7p_qcaDcFTSiSi_Si
func «+»(a: Int, b: Int) -> Int { return a + b }

protocol Foo {}
protocol Bar {}

// Ensure protocol list manglings are '_' terminated regardless of length
// CHECK-LABEL: sil hidden @_TF8mangling12any_protocolFP_T_
func any_protocol(_: Any) {}
// CHECK-LABEL: sil hidden @_TF8mangling12one_protocolFPS_3Foo_T_
func one_protocol(_: Foo) {}
// CHECK-LABEL: sil hidden @_TF8mangling18one_protocol_twiceFTPS_3Foo_PS0___T_
func one_protocol_twice(_: Foo, _: Foo) {}
// CHECK-LABEL: sil hidden @_TF8mangling12two_protocolFPS_3BarS_3Foo_T_
func two_protocol(_: Foo & Bar) {}

// Ensure archetype depths are mangled correctly.
class Zim<T> {
  // CHECK-LABEL: sil hidden @_TFC8mangling3Zim4zangurfTxqd___T_
  func zang<U>(_: T, _: U) {}
  // CHECK-LABEL: sil hidden @_TFC8mangling3Zim4zungurfTqd__x_T_
  func zung<U>(_: U, _: T) {}
}

// Don't crash mangling single-protocol "composition" types.
// CHECK-LABEL: sil hidden @_TF8mangling27single_protocol_compositionFT1xPS_3Foo__T_
func single_protocol_composition(x x: protocol<Foo>) {} // expected-warning {{'protocol<...>' composition syntax is deprecated; join the protocols using '&'}}

// Clang-imported classes and protocols get mangled into a magic 'So' context
// to make collisions into link errors. <rdar://problem/14221244>
// CHECK-LABEL: sil hidden @_TF8mangling28uses_objc_class_and_protocolFT1oCSo8NSObject1pPSo8NSAnsing__T_
func uses_objc_class_and_protocol(o o: NSObject, p: NSAnsing) {}

// Clang-imported structs get mangled using their Clang module name.
// FIXME: Temporarily mangles everything into the virtual module __C__
// <rdar://problem/14221244>
// CHECK-LABEL: sil hidden @_TF8mangling17uses_clang_structFT1rVSC6NSRect_T_
func uses_clang_struct(r r: NSRect) {}

// CHECK-LABEL: sil hidden @_TF8mangling14uses_optionalsFT1xGSqSi__GSqSc_
func uses_optionals(x x: Int?) -> UnicodeScalar? { return nil }

enum GenericUnion<T> {
  // CHECK-LABEL: sil shared [transparent] @_TFO8mangling12GenericUnion3FoourfMGS0_x_FSiGS0_x_
  case Foo(Int)
}

func instantiateGenericUnionConstructor<T>(_ t: T) {
  _ = GenericUnion<T>.Foo
}

struct HasVarInit {
  static var state = true && false
}
// CHECK-LABEL: // function_ref static mangling.HasVarInit.(state : Swift.Bool).(variable initialization expression).(implicit closure #1)
// CHECK-NEXT:  function_ref @_TFIZvV8mangling10HasVarInit5stateSbiu_KzT_Sb

// auto_closures should not collide with the equivalent non-auto_closure
// function type.

// CHECK-LABEL: sil hidden @_TF8mangling19autoClosureOverloadFT1fKT_Si_T_ : $@convention(thin) (@owned @callee_owned () -> Int) -> () {
func autoClosureOverload(f f: @autoclosure () -> Int) {}
// CHECK-LABEL: sil hidden @_TF8mangling19autoClosureOverloadFT1fFT_Si_T_ : $@convention(thin) (@owned @callee_owned () -> Int) -> () {
func autoClosureOverload(f f: () -> Int) {}

// CHECK-LABEL: sil hidden @_TF8mangling24autoClosureOverloadCallsFT_T_ : $@convention(thin) () -> () {
func autoClosureOverloadCalls() {
  // CHECK: function_ref @_TF8mangling19autoClosureOverloadFT1fKT_Si_T_
  autoClosureOverload(f: 1)
  // CHECK: function_ref @_TF8mangling19autoClosureOverloadFT1fFT_Si_T_
  autoClosureOverload {1}
}

// <rdar://problem/16079822> Associated type requirements need to appear in the
// mangling.

protocol AssocReqt {}

protocol HasAssocType {
  associatedtype Assoc
}

// CHECK-LABEL: sil hidden @_TF8mangling4fooAuRxS_12HasAssocTyperFxT_ : $@convention(thin) <T where T : HasAssocType> (@in T) -> ()
func fooA<T: HasAssocType>(_: T) {}
// CHECK-LABEL: sil hidden @_TF8mangling4fooBuRxS_12HasAssocTypewx5AssocS_9AssocReqtrFxT_ : $@convention(thin) <T where T : HasAssocType, T.Assoc : AssocReqt> (@in T) -> ()
func fooB<T: HasAssocType where T.Assoc: AssocReqt>(_: T) {}

// CHECK-LABEL: sil hidden @_TF8manglingoi2qqFTSiSi_T_
func ??(x: Int, y: Int) {}

struct InstanceAndClassProperty {
  var property: Int {
    // CHECK-LABEL: sil hidden @_TFV8mangling24InstanceAndClassPropertyg8propertySi
    get { return 0 }
    // CHECK-LABEL: sil hidden @_TFV8mangling24InstanceAndClassPropertys8propertySi
    set {}
  }
  static var property: Int {
    // CHECK-LABEL: sil hidden @_TZFV8mangling24InstanceAndClassPropertyg8propertySi
    get { return 0 }
    // CHECK-LABEL: sil hidden @_TZFV8mangling24InstanceAndClassPropertys8propertySi
    set {}
  }
}

// CHECK-LABEL: sil hidden @_TF8mangling6curry1FT_T_ : $@convention(thin) () -> ()
func curry1() {

}

// CHECK-LABEL: sil hidden @_TF8mangling3barFzT_Si : $@convention(thin) () -> (Int, @error Error)
func bar() throws -> Int { return 0 }

// CHECK-LABEL: sil hidden @_TF8mangling12curry1ThrowsFzT_T_ : $@convention(thin) () -> @error Error
func curry1Throws() throws {

}

// CHECK-LABEL: sil hidden @_TF8mangling12curry2ThrowsFzT_FT_T_ : $@convention(thin) () -> (@owned @callee_owned () -> (), @error Error)
func curry2Throws() throws -> () -> () {
  return curry1
}

// CHECK-LABEL: sil hidden @_TF8mangling6curry3FT_FzT_T_ : $@convention(thin) () -> @owned @callee_owned () -> @error Error
func curry3() -> () throws -> () {
  return curry1Throws
}

// CHECK-LABEL: sil hidden @_TF8mangling12curry3ThrowsFzT_FzT_T_ : $@convention(thin) () -> (@owned @callee_owned () -> @error Error, @error Error)
func curry3Throws() throws -> () throws -> () {
  return curry1Throws
}
