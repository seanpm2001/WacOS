// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -parse -verify -disable-objc-attr-requires-foundation-module %s
// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -print-usrs -source-filename %s | %FileCheck %s -strict-whitespace

import macros

// CHECK: [[@LINE+1]]:8 s:V14swift_ide_test1S{{$}}
struct S {
  // CHECK: [[@LINE+1]]:7 s:vV14swift_ide_test1S1xSi{{$}}
  var x : Int
}

// CHECK: [[@LINE+1]]:11 s:14swift_ide_test6MyGInt{{$}}
typealias MyGInt = Int

// CHECK: [[@LINE+1]]:7 s:C14swift_ide_test5MyCls{{$}}
class MyCls {
  // CHECK: [[@LINE+1]]:13 s:C14swift_ide_test5MyCls2TA{{$}}
  typealias TA = Int
  // CHECK: [[@LINE+1]]:7 s:vC14swift_ide_test5MyCls3wwwSi{{$}}
  var www : Int = 0
  // CHECK: [[@LINE+1]]:8 s:FC14swift_ide_test5MyCls3fooFSiT_{{$}}
  func foo(_ x : Int) {}
  // CHECK: [[@LINE+1]]:3 s:iC14swift_ide_test5MyCls9subscriptFSiSf{{$}}
  subscript(i: Int) -> Float {
    // CHECK: [[@LINE+1]]:5 s:FC14swift_ide_test5MyClsg9subscriptFSiSf{{$}}
    get { return 0.0 }
    // CHECK: [[@LINE+1]]:5 s:FC14swift_ide_test5MyClss9subscriptFSiSf{{$}}
    set {}
  }
}

// CHECK: [[@LINE+1]]:7 s:C14swift_ide_test12GenericClass{{$}}
class GenericClass {

  // CHECK: [[@LINE+1]]:13 s:C14swift_ide_test12GenericClass2TA{{$}}
  typealias TA = Int

  // CHECK: [[@LINE+1]]:7 s:vC14swift_ide_test12GenericClass11instanceVarSi{{$}}
  var instanceVar: Int = 0

  // CHECK: [[@LINE+1]]:8 s:FC14swift_ide_test12GenericClass12instanceFuncFT_T_{{$}}
  func instanceFunc() {
    // CHECK: [[@LINE+2]]:18 s:ZFC14swift_ide_test12GenericClass9classFuncFS0_T_{{$}}
    // CHECK: [[@LINE+1]]:28 s:vFC14swift_ide_test12GenericClass12instanceFuncFT_T_L_4selfS0_{{$}}
    GenericClass.classFunc(self)
  }

  // CHECK: [[@LINE+2]]:3 s:iC14swift_ide_test12GenericClass9subscriptFSiSf{{$}}
  // CHECK: [[@LINE+1]]:13 s:vC14swift_ide_test12GenericClassL_1iSi{{$}}
  subscript(i: Int) -> Float {
    // CHECK: [[@LINE+1]]:5 s:FC14swift_ide_test12GenericClassg9subscriptFSiSf{{$}}
    get { return 0.0 }
    // CHECK: [[@LINE+1]]:5 s:FC14swift_ide_test12GenericClasss9subscriptFSiSf{{$}}
    set {}
  }

  // CHECK: [[@LINE+1]]:3 s:FC14swift_ide_test12GenericClassd{{$}}
  deinit {
    // CHECK: [[@LINE+2]]:18 s:ZFC14swift_ide_test12GenericClass9classFuncFS0_T_{{$}}
    // CHECK: [[@LINE+1]]:28 ERROR:no-usr{{$}}
    GenericClass.classFunc(self)
  }

  // CHECK: [[@LINE+2]]:14 s:ZFC14swift_ide_test12GenericClass9classFuncFS0_T_{{$}}
  // CHECK: [[@LINE+1]]:26 s:vZFC14swift_ide_test12GenericClass9classFuncFS0_T_L_1aS0_{{$}}
  class func classFunc(_ a: GenericClass) {}
}

// CHECK: [[@LINE+1]]:10 s:P14swift_ide_test4Prot{{$}}
protocol Prot {
  // CHECK: [[@LINE+1]]:18 s:P14swift_ide_test4Prot5Blarg{{$}}
  associatedtype Blarg
  // CHECK: [[@LINE+1]]:8 s:FP14swift_ide_test4Prot8protMethFwx5BlargwxS1_{{$}}
  func protMeth(_ x: Blarg) -> Blarg
  // CHECK: [[@LINE+2]]:7 s:vP14swift_ide_test4Prot17protocolProperty1Si{{$}}
  // CHECK: [[@LINE+1]]:32 s:FP14swift_ide_test4Protg17protocolProperty1Si{{$}}
  var protocolProperty1: Int { get }
}
protocol Prot2 {}

class SubCls : MyCls, Prot {
  // CHECK: [[@LINE+1]]:13 s:C14swift_ide_test6SubCls5Blarg{{$}}
  typealias Blarg = Prot2
  // CHECK: [[@LINE+1]]:8 s:FC14swift_ide_test6SubCls8protMethFPS_5Prot2_PS1__{{$}}
  func protMeth(_ x: Blarg) -> Blarg {}
  // CHECK: [[@LINE+1]]:7 s:vC14swift_ide_test6SubCls17protocolProperty1Si{{$}}
  var protocolProperty1 = 0
}

// CHECK: [[@LINE+1]]:6 s:F14swift_ide_test5genFnuRxS_4Protwx5BlargS_5Prot2rFxSi{{$}}
func genFn<T : Prot>(_ p : T) -> Int where T.Blarg : Prot2 {}

// CHECK: [[@LINE+1]]:6 s:F14swift_ide_test3barFSiTSiSf_{{$}}
func bar(_ x: Int) -> (Int, Float) {}

// CHECK: [[@LINE+1]]:7 s:C14swift_ide_test6GenCls{{$}}
class GenCls<T> {
  // CHECK: [[@LINE+1]]:3 s:FC14swift_ide_test6GenClscFT_GS0_x_{{$}}
  init() {}
  // CHECK: [[@LINE+1]]:3 s:FC14swift_ide_test6GenClsd{{$}}
  deinit {}
  // CHECK: [[@LINE+1]]:14 s:ZFC14swift_ide_test6GenCls4cfooFT_T_{{$}}
  class func cfoo() {}

  // CHECK: [[@LINE+1]]:3 s:iC14swift_ide_test6GenCls9subscriptFTSiSi_Si{{$}}
  subscript (i : Int, j : Int) -> Int {
    // CHECK: [[@LINE+1]]:5 s:FC14swift_ide_test6GenClsg9subscriptFTSiSi_Si{{$}}
    get {
      return i + j
    }
    // CHECK: [[@LINE+1]]:5 s:FC14swift_ide_test6GenClss9subscriptFTSiSi_Si{{$}}
    set(v) {
      _ = v + i - j
    }
  }
}

class C4 {
  // CHECK: [[@LINE+1]]:9 s:CC14swift_ide_test2C42In{{$}}
  class In {
    // CHECK: [[@LINE+1]]:16 s:ZFCC14swift_ide_test2C42In3gooFT_T_{{$}}
    class func goo() {}
  }
}

class C5 {}
extension C5 {
  // CHECK: [[@LINE+1]]:8 s:FC14swift_ide_test2C55extFnFT_T_{{$}}
  func extFn() {}
}

class Observers {
  func doit() {}

  // CHECK: [[@LINE+1]]:7 s:vC14swift_ide_test9Observers2p1Si{{$}}
  var p1 : Int = 0 {
    // CHECK: [[@LINE+1]]:5 s:FC14swift_ide_test9Observersw2p1Si{{$}}
    willSet(newValue) { doit() }
    // CHECK: [[@LINE+1]]:5 s:FC14swift_ide_test9ObserversW2p1Si{{$}}
    didSet { doit() }
  }

  // CHECK: [[@LINE+1]]:7 s:vC14swift_ide_test9Observers2p2Si{{$}}
  var p2 = 42 {
    // CHECK: [[@LINE+1]]:5 s:FC14swift_ide_test9Observersw2p2Si{{$}}
    willSet(newValue) { doit() }
    // CHECK: [[@LINE+1]]:5 s:FC14swift_ide_test9ObserversW2p2Si{{$}}
    didSet { doit() }
  }
}

// CHECK: [[@LINE+2]]:7 s:C14swift_ide_test10ObjCClass1{{$}}
@objc
class ObjCClass1 {
  // CHECK: [[@LINE+1]]:8 s:FC14swift_ide_test10ObjCClass113instanceFunc1FSiT_{{$}}
  func instanceFunc1(_ a: Int) {}
  // CHECK: [[@LINE+1]]:14 s:ZFC14swift_ide_test10ObjCClass111staticFunc1FSiT_{{$}}
  class func staticFunc1(_ a: Int) {}
}

// CHECK: [[@LINE+1]]:6 s:O14swift_ide_test5Suits{{$}}
enum Suits {
  // CHECK: [[@LINE+1]]:8 s:FO14swift_ide_test5Suits5ClubsFMS0_S0_{{$}}
  case Clubs
  // CHECK: [[@LINE+1]]:8 s:FO14swift_ide_test5Suits8DiamondsFMS0_S0_{{$}}
  case Diamonds

  // CHECK: [[@LINE+1]]:8 s:FO14swift_ide_test5Suits5enfooFT_T_{{$}}
  func enfoo() {}
}

func importedMacros() {
  // CHECK: [[@LINE+1]]:12 c:@macro@M_PI{{$}}
  let m1 = M_PI
  // CHECK: [[@LINE+1]]:12 c:@macro@MACRO_FROM_IMPL{{$}}
  let m2 = MACRO_FROM_IMPL
  // CHECK: [[@LINE+1]]:12 c:@macro@USES_MACRO_FROM_OTHER_MODULE_1{{$}}
  let m3 = USES_MACRO_FROM_OTHER_MODULE_1
  
  _ = m1; _ = m2; _ = m3
}

