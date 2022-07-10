// RUN: %target-swift-frontend -O -emit-sil %s | %FileCheck %s

// REQUIRES: objc_interop

// Check that casts between bridged types are replaced by more 
// efficient code sequences.
// 
// In particular, checked_cast_* and unconditional_checked_* instructions,
// which are pretty expensive at run-time (e.g. because they use
// runtime _dynamicCast calls and check conformances at run-time),
// should be replaced by invocations of specialized bridging functions,
// which make use of statically known compile-time conformances and 
// do not perform any conformance checks at run-time.

import Foundation

public func forcedCast<NS, T>(_ ns: NS) -> T {
  return ns as! T
}

public func condCast<NS, T>(_ ns: NS) -> T? {
  return ns as? T
}

// Check optimizations of casts from NSString to String

var nsString: NSString = "string"

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding29testForcedCastNStoSwiftStringFT_SS
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWSSs21_ObjectiveCBridgeable10FoundationZFS_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNStoSwiftString() -> String {
  var o: String = forcedCast(nsString)
  return o
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding27testCondCastNStoSwiftStringFT_GSqSS_
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWSSs21_ObjectiveCBridgeable10FoundationZFS_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNStoSwiftString() -> String? {
  var o: String? = condCast(nsString)
  return o
}


// Check optimizations of casts from NSNumber to Int

var nsIntNumber = NSNumber(value: 1)

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding32testForcedCastNSNumberToSwiftIntFT_Si
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWSis21_ObjectiveCBridgeable10FoundationZFS_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNSNumberToSwiftInt() -> Int {
  var o: Int = forcedCast(nsIntNumber)
  return o
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding30testCondCastNSNumberToSwiftIntFT_GSqSi_
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWSis21_ObjectiveCBridgeable10FoundationZFS_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNSNumberToSwiftInt() -> Int? {
  var o: Int? = condCast(nsIntNumber)
  return o
}

// Check optimizations of casts from NSNumber to Double

var nsDoubleNumber = NSNumber(value: 1.234)

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding35testForcedCastNSNumberToSwiftDoubleFT_Sd
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWSds21_ObjectiveCBridgeable10FoundationZFS_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNSNumberToSwiftDouble() -> Double {
  var o: Double = forcedCast(nsDoubleNumber)
  return o
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding33testCondCastNSNumberToSwiftDoubleFT_GSqSd_
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWSds21_ObjectiveCBridgeable10FoundationZFS_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNSNumberToSwiftDouble() -> Double? {
  var o: Double? = condCast(nsDoubleNumber)
  return o
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding38testForcedCastNSIntNumberToSwiftDoubleFT_Sd
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWSds21_ObjectiveCBridgeable10FoundationZFS_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNSIntNumberToSwiftDouble() -> Double {
  var o: Double = forcedCast(nsIntNumber)
  return o
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding36testCondCastNSIntNumberToSwiftDoubleFT_GSqSd_
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWSds21_ObjectiveCBridgeable10FoundationZFS_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNSIntNumberToSwiftDouble() -> Double? {
  var o: Double? = condCast(nsIntNumber)
  return o
}



// Check optimization of casts from NSArray to Swift Array

var nsArrInt: NSArray = [1, 2, 3, 4]
var nsArrDouble: NSArray = [1.1, 2.2, 3.3, 4.4]
var nsArrString: NSArray = ["One", "Two", "Three", "Four"]

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding31testForcedCastNStoSwiftArrayIntFT_GSaSi_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWurGSax_s21_ObjectiveCBridgeable10FoundationZFS_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNStoSwiftArrayInt() -> [Int] {
  var arr: [Int] = forcedCast(nsArrInt)
  return arr
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding29testCondCastNStoSwiftArrayIntFT_GSqGSaSi__
// CHECK-NOT: checked_cast
// CHECK: @_TTWurGSax_s21_ObjectiveCBridgeable10FoundationZFS_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNStoSwiftArrayInt() -> [Int]? {
  var arrOpt: [Int]? = condCast(nsArrInt)
  return arrOpt
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding34testForcedCastNStoSwiftArrayDoubleFT_GSaSd_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWurGSax_s21_ObjectiveCBridgeable10FoundationZFS_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNStoSwiftArrayDouble() -> [Double] {
  var arr: [Double] = forcedCast(nsArrDouble)
  return arr
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding32testCondCastNStoSwiftArrayDoubleFT_GSqGSaSd__
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWurGSax_s21_ObjectiveCBridgeable10FoundationZFS_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNStoSwiftArrayDouble() -> [Double]? {
  var arrOpt: [Double]? = condCast(nsArrDouble)
  return arrOpt
}


// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding34testForcedCastNStoSwiftArrayStringFT_GSaSS_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWurGSax_s21_ObjectiveCBridgeable10FoundationZFS_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNStoSwiftArrayString() -> [String] {
  var arr: [String] = forcedCast(nsArrString)
  return arr
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding32testCondCastNStoSwiftArrayStringFT_GSqGSaSS__
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWurGSax_s21_ObjectiveCBridgeable10FoundationZFS_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNStoSwiftArrayString() -> [String]? {
  var arrOpt: [String]? = condCast(nsArrString)
  return arrOpt
}



// Check optimization of casts from NSDictionary to Swift Dictionary

var nsDictInt: NSDictionary = [1:1, 2:2, 3:3, 4:4]
var nsDictDouble: NSDictionary = [1.1 : 1.1, 2.2 : 2.2, 3.3 : 3.3, 4.4 : 4.4]
var nsDictString: NSDictionary = ["One":"One", "Two":"Two", "Three":"Three", "Four":"Four"]

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding30testForcedCastNStoSwiftDictIntFT_GVs10DictionarySiSi_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWu0_Rxs8HashablerGVs10Dictionaryxq__s21_ObjectiveCBridgeable10FoundationZFS1_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNStoSwiftDictInt() -> [Int:Int] {
  var dict: [Int:Int] = forcedCast(nsDictInt)
  return dict
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding28testCondCastNStoSwiftDictIntFT_GSqGVs10DictionarySiSi__
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWu0_Rxs8HashablerGVs10Dictionaryxq__s21_ObjectiveCBridgeable10FoundationZFS1_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNStoSwiftDictInt() -> [Int:Int]? {
  var dictOpt: [Int:Int]? = condCast(nsDictInt)
  return dictOpt
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding33testForcedCastNStoSwiftDictDoubleFT_GVs10DictionarySdSd_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWu0_Rxs8HashablerGVs10Dictionaryxq__s21_ObjectiveCBridgeable10FoundationZFS1_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNStoSwiftDictDouble() -> [Double:Double] {
  var dict: [Double:Double] = forcedCast(nsDictDouble)
  return dict
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding31testCondCastNStoSwiftDictDoubleFT_GSqGVs10DictionarySdSd__
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWu0_Rxs8HashablerGVs10Dictionaryxq__s21_ObjectiveCBridgeable10FoundationZFS1_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNStoSwiftDictDouble() -> [Double:Double]? {
  var dictOpt: [Double:Double]? = condCast(nsDictDouble)
  return dictOpt
}


// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding33testForcedCastNStoSwiftDictStringFT_GVs10DictionarySSSS_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWu0_Rxs8HashablerGVs10Dictionaryxq__s21_ObjectiveCBridgeable10FoundationZFS1_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNStoSwiftDictString() -> [String:String] {
  var dict: [String:String] = forcedCast(nsDictString)
  return dict
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding31testCondCastNStoSwiftDictStringFT_GSqGVs10DictionarySSSS__
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWu0_Rxs8HashablerGVs10Dictionaryxq__s21_ObjectiveCBridgeable10FoundationZFS1_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNStoSwiftDictString() -> [String:String]? {
  var dictOpt: [String:String]? = condCast(nsDictString)
  return dictOpt
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding40testForcedCastNSDictStringtoSwiftDictIntFT_GVs10DictionarySiSi_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWu0_Rxs8HashablerGVs10Dictionaryxq__s21_ObjectiveCBridgeable10FoundationZFS1_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNSDictStringtoSwiftDictInt() -> [Int:Int] {
  var dictOpt: [Int:Int] = forcedCast(nsDictString)
  return dictOpt
}


// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding38testCondCastNSDictStringtoSwiftDictIntFT_GSqGVs10DictionarySiSi__
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWu0_Rxs8HashablerGVs10Dictionaryxq__s21_ObjectiveCBridgeable10FoundationZFS1_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNSDictStringtoSwiftDictInt() -> [Int:Int]? {
  var dictOpt: [Int:Int]? = condCast(nsDictString)
  return dictOpt
}


// Check optimization of casts from NSSet to Swift Set

var nsSetInt: NSSet = [1, 2, 3, 4]
var nsSetDouble: NSSet = [1.1, 2.2, 3.3, 4.4]
var nsSetString: NSSet = ["One", "Two", "Three", "Four"]

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding29testForcedCastNStoSwiftSetIntFT_GVs3SetSi_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWuRxs8HashablerGVs3Setx_s21_ObjectiveCBridgeable10FoundationZFS1_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNStoSwiftSetInt() -> Set<Int> {
  var set: Set<Int> = forcedCast(nsSetInt)
  return set
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding27testCondCastNStoSwiftSetIntFT_GSqGVs3SetSi__
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWuRxs8HashablerGVs3Setx_s21_ObjectiveCBridgeable10FoundationZFS1_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNStoSwiftSetInt() -> Set<Int>? {
  var setOpt: Set<Int>? = condCast(nsSetInt)
  return setOpt
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding32testForcedCastNStoSwiftSetDoubleFT_GVs3SetSd_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWuRxs8HashablerGVs3Setx_s21_ObjectiveCBridgeable10FoundationZFS1_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNStoSwiftSetDouble() -> Set<Double> {
  var set: Set<Double> = forcedCast(nsSetDouble)
  return set
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding30testCondCastNStoSwiftSetDoubleFT_GSqGVs3SetSd__
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWuRxs8HashablerGVs3Setx_s21_ObjectiveCBridgeable10FoundationZFS1_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNStoSwiftSetDouble() -> Set<Double>? {
  var setOpt: Set<Double>? = condCast(nsSetDouble)
  return setOpt
}


// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding32testForcedCastNStoSwiftSetStringFT_GVs3SetSS_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @_TTWuRxs8HashablerGVs3Setx_s21_ObjectiveCBridgeable10FoundationZFS1_26_forceBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__T_
// CHECK: return
@inline(never)
public func testForcedCastNStoSwiftSetString() -> Set<String> {
  var set: Set<String> = forcedCast(nsSetString)
  return set
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding30testCondCastNStoSwiftSetStringFT_GSqGVs3SetSS__
// CHECK-NOT: checked_cast
// CHECK: function_ref @_TTWuRxs8HashablerGVs3Setx_s21_ObjectiveCBridgeable10FoundationZFS1_34_conditionallyBridgeFromObjectiveCfTwx15_ObjectiveCType6resultRGSqx__Sb
// CHECK: return
@inline(never)
public func testCondCastNStoSwiftSetString() -> Set<String>? {
  var setOpt: Set<String>? = condCast(nsSetString)
  return setOpt
}


// Check optimizations of casts from String to NSString

var swiftString:String = "string"

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding29testForcedCastSwiftToNSStringFT_CSo8NSString
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testForcedCastSwiftToNSString() -> NSString {
  var o: NSString = forcedCast(swiftString)
  return o
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding27testCondCastSwiftToNSStringFT_GSqCSo8NSString_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testCondCastSwiftToNSString() -> NSString? {
  var o: NSString? = condCast(swiftString)
  return o
}


// Check optimizations of casts from Int to NSNumber

var swiftIntNumber: Int = 1

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding32testForcedCastSwiftIntToNSNumberFT_CSo8NSNumber
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testForcedCastSwiftIntToNSNumber() -> NSNumber {
  var o: NSNumber = forcedCast(swiftIntNumber)
  return o
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding30testCondCastSwiftIntToNSNumberFT_GSqCSo8NSNumber_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testCondCastSwiftIntToNSNumber() -> NSNumber? {
  var o: NSNumber? = condCast(swiftIntNumber)
  return o
}

// Check optimizations of casts from Double to NSNumber

var swiftDoubleNumber: Double = 1.234

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding35testForcedCastSwiftDoubleToNSNumberFT_CSo8NSNumber
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testForcedCastSwiftDoubleToNSNumber() -> NSNumber {
  var o: NSNumber = forcedCast(swiftDoubleNumber)
  return o
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding33testCondCastSwiftDoubleToNSNumberFT_GSqCSo8NSNumber_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testCondCastSwiftDoubleToNSNumber() -> NSNumber? {
  var o: NSNumber? = condCast(swiftDoubleNumber)
  return o
}


// Check optimization of casts from  Swift Array to NSArray

var arrInt: [Int] = [1, 2, 3, 4]
var arrDouble: [Double] = [1.1, 2.2, 3.3, 4.4]
var arrString: [String] = ["One", "Two", "Three", "Four"]

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding31testForcedCastSwiftToNSArrayIntFT_CSo7NSArray
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testForcedCastSwiftToNSArrayInt() -> NSArray {
  var arr: NSArray = forcedCast(arrInt)
  return arr
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding29testCondCastSwiftToNSArrayIntFT_GSqCSo7NSArray_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testCondCastSwiftToNSArrayInt() -> NSArray? {
  var arrOpt: NSArray? = condCast(arrInt)
  return arrOpt
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding34testForcedCastSwiftToNSArrayDoubleFT_CSo7NSArray
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testForcedCastSwiftToNSArrayDouble() -> NSArray {
  var arr: NSArray = forcedCast(arrDouble)
  return arr
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding32testCondCastSwiftToNSArrayDoubleFT_GSqCSo7NSArray_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testCondCastSwiftToNSArrayDouble() -> NSArray? {
  var arrOpt: NSArray? = condCast(arrDouble)
  return arrOpt
}


// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding34testForcedCastSwiftToNSArrayStringFT_CSo7NSArray
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testForcedCastSwiftToNSArrayString() -> NSArray {
  var arr: NSArray = forcedCast(arrString)
  return arr
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding32testCondCastSwiftToNSArrayStringFT_GSqCSo7NSArray_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testCondCastSwiftToNSArrayString() -> NSArray? {
  var arrOpt: NSArray? = condCast(arrString)
  return arrOpt
}


// Check optimization of casts from  Swift Dict to NSDict

var dictInt: [Int:Int] = [1:1, 2:2, 3:3, 4:4]
var dictDouble: [Double:Double] = [1.1 : 1.1, 2.2 : 2.2, 3.3 : 3.3, 4.4 : 4.4]
var dictString: [String:String] = ["One":"One", "Two":"Two", "Three":"Three", "Four":"Four"]

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding30testForcedCastSwiftToNSDictIntFT_CSo12NSDictionary
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testForcedCastSwiftToNSDictInt() -> NSDictionary {
  var dict: NSDictionary = forcedCast(dictInt)
  return dict
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding28testCondCastSwiftToNSDictIntFT_GSqCSo12NSDictionary_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testCondCastSwiftToNSDictInt() -> NSDictionary? {
  var dictOpt: NSDictionary? = condCast(dictInt)
  return dictOpt
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding33testForcedCastSwiftToNSDictDoubleFT_CSo12NSDictionary
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testForcedCastSwiftToNSDictDouble() -> NSDictionary {
  var dict: NSDictionary = forcedCast(dictDouble)
  return dict
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding31testCondCastSwiftToNSDictDoubleFT_GSqCSo12NSDictionary_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testCondCastSwiftToNSDictDouble() -> NSDictionary? {
  var dictOpt: NSDictionary? = condCast(dictDouble)
  return dictOpt
}


// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding33testForcedCastSwiftToNSDictStringFT_CSo12NSDictionary
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testForcedCastSwiftToNSDictString() -> NSDictionary {
  var dict: NSDictionary = forcedCast(dictString)
  return dict
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding31testCondCastSwiftToNSDictStringFT_GSqCSo12NSDictionary_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testCondCastSwiftToNSDictString() -> NSDictionary? {
  var dictOpt: NSDictionary? = condCast(dictString)
  return dictOpt
}


// Check optimization of casts from  Swift Set to NSSet

var setInt: Set<Int> = [1, 2, 3, 4]
var setDouble: Set<Double> = [1.1, 2.2, 3.3, 4.4]
var setString: Set<String> = ["One", "Two", "Three", "Four"]

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding29testForcedCastSwiftToNSSetIntFT_CSo5NSSet
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testForcedCastSwiftToNSSetInt() -> NSSet {
  var set: NSSet = forcedCast(setInt)
  return set
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding27testCondCastSwiftToNSSetIntFT_GSqCSo5NSSet_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testCondCastSwiftToNSSetInt() -> NSSet? {
  var setOpt: NSSet? = condCast(setInt)
  return setOpt
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding32testForcedCastSwiftToNSSetDoubleFT_CSo5NSSet
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testForcedCastSwiftToNSSetDouble() -> NSSet {
  var set: NSSet = forcedCast(setDouble)
  return set
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding30testCondCastSwiftToNSSetDoubleFT_GSqCSo5NSSet_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testCondCastSwiftToNSSetDouble() -> NSSet? {
  var setOpt: NSSet? = condCast(setDouble)
  return setOpt
}


// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding32testForcedCastSwiftToNSSetStringFT_CSo5NSSet
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testForcedCastSwiftToNSSetString() -> NSSet {
  var set: NSSet = forcedCast(setString)
  return set
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding30testCondCastSwiftToNSSetStringFT_GSqCSo5NSSet_
// CHECK-NOT: unconditional_checked
// CHECK: function_ref @{{.*}}_bridgeToObjectiveC
// CHECK: return
@inline(never)
public func testCondCastSwiftToNSSetString() -> NSSet? {
  var setOpt: NSSet? = condCast(setString)
  return setOpt
}

// Casts involving generics cannot be optimized.

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding25testForcedCastFromGeneric
// CHECK: unconditional_checked
// CHECK: return
@inline(never)
public func testForcedCastFromGeneric<T>(_ x: T) -> NSString {
  var set: NSString = x as! NSString
  return set
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding23testForcedCastToGeneric
// CHECK: unconditional_checked
// CHECK: return
@inline(never)
public func testForcedCastToGeneric<T>(_ x: T) -> T {
  var set: T = nsString as! T
  return set
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding23testCondCastFromGeneric
// CHECK: checked_cast_addr_br
// CHECK: return
@inline(never)
public func testCondCastFromGeneric<T>(_ x: T) -> NSString? {
  var setOpt: NSString? = x as? NSString
  return setOpt
}

// CHECK-LABEL: sil [noinline] @_TF21bridged_casts_folding21testCondCastToGeneric
// CHECK: checked_cast_addr_br
// CHECK: return
@inline(never)
public func testCondCastToGeneric<T>(_ x: T) -> T? {
  var setOpt: T? = nsString as? T
  return setOpt
}


// Run-time tests

//// ObjC -> Swift

// Arrays
print("NS to Swift arrays: Start")
print(testForcedCastNStoSwiftArrayInt())
print(testCondCastNStoSwiftArrayInt())

print(testForcedCastNStoSwiftArrayDouble())
print(testCondCastNStoSwiftArrayDouble())

print(testForcedCastNStoSwiftArrayString())
print(testCondCastNStoSwiftArrayString())
print("NS to Swift arrays: End")

// Dicts
print("NS to Swift dictionaries: Start")
print(testForcedCastNStoSwiftDictInt())
print(testCondCastNStoSwiftDictInt())

print(testForcedCastNStoSwiftDictDouble())
print(testCondCastNStoSwiftDictDouble())

print(testForcedCastNStoSwiftDictString())
print(testCondCastNStoSwiftDictString())
print(testCondCastNSDictStringtoSwiftDictInt())
// This line should crash at run-time
//print(testForcedCastNSDictStringtoSwiftDictInt())
print("NS to Swift dictionaries: End")

// Sets
print("NS to Swift sets: Start")
print(testForcedCastNStoSwiftSetInt())
print(testCondCastNStoSwiftSetInt())

print(testForcedCastNStoSwiftSetDouble())
print(testCondCastNStoSwiftSetDouble())

print(testForcedCastNStoSwiftSetString())
print(testCondCastNStoSwiftSetString())
print("NS to Swift sets: End")


// Basic types

print("NS to Swift basic types: Start")
print(testForcedCastNSNumberToSwiftInt())
print(testCondCastNSNumberToSwiftInt())

print(testForcedCastNSNumberToSwiftDouble())
print(testCondCastNSNumberToSwiftDouble())

print(testForcedCastNSIntNumberToSwiftDouble())
print(testCondCastNSIntNumberToSwiftDouble())

print(testForcedCastNStoSwiftString())
print(testCondCastNStoSwiftString())
print("NS to Swift basic types: End")

//// Swift -> ObjC

// Basic types

print("Swift to NS basic types: Start")
print(testForcedCastSwiftIntToNSNumber())
print(testCondCastSwiftIntToNSNumber())

print(testForcedCastSwiftDoubleToNSNumber())
print(testCondCastSwiftDoubleToNSNumber())

print(testForcedCastSwiftToNSString())
print(testCondCastSwiftToNSString())
print("Swift to NS basic types: End")

// Arrays
print("Swift to NS arrays: Start")

print(testForcedCastSwiftToNSArrayInt())
print(testCondCastSwiftToNSArrayInt())

print(testForcedCastSwiftToNSArrayDouble())
print(testCondCastSwiftToNSArrayDouble())

print(testForcedCastSwiftToNSArrayString())
print(testCondCastSwiftToNSArrayString())

print("Swift to NS arrays: End")


// Dicts
print("Swift to NS dictionaries: Start")

print(testForcedCastSwiftToNSDictInt())
print(testCondCastSwiftToNSDictInt())

print(testForcedCastSwiftToNSDictDouble())
print(testCondCastSwiftToNSDictDouble())

print(testForcedCastSwiftToNSDictString())
print(testCondCastSwiftToNSDictString())

print("Swift to NS dictionaries: End")

// Sets
print("Swift to NS sets: Start")

print(testForcedCastSwiftToNSSetInt())
print(testCondCastSwiftToNSSetInt())

print(testForcedCastSwiftToNSSetDouble())
print(testCondCastSwiftToNSSetDouble())

print(testForcedCastSwiftToNSSetString())
print(testCondCastSwiftToNSSetString())

print("Swift to NS sets: End")
