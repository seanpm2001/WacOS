// RUN: %target-swift-frontend -parse -verify %s -enable-id-as-any

// REQUIRES: objc_interop

import Foundation

public class BridgedClass : NSObject, NSCopying {
  @objc(copyWithZone:)
  public func copy(with zone: NSZone?) -> Any {
    return self
  }
}

public class BridgedClassSub : BridgedClass { }

// Attempt to bridge to a non-whitelisted type from another module.
extension LazyFilterIterator : _ObjectiveCBridgeable { // expected-error{{conformance of 'LazyFilterIterator' to '_ObjectiveCBridgeable' can only be written in module 'Swift'}}
  public typealias _ObjectiveCType = BridgedClassSub

  public func _bridgeToObjectiveC() -> _ObjectiveCType {
    return BridgedClassSub()
  }

  public static func _forceBridgeFromObjectiveC(
    _ source: _ObjectiveCType,
    result: inout LazyFilterIterator?
  ) { }

  public static func _conditionallyBridgeFromObjectiveC(
    _ source: _ObjectiveCType,
    result: inout LazyFilterIterator?
  ) -> Bool {
    return true
  }

  public static func _unconditionallyBridgeFromObjectiveC(_ source: _ObjectiveCType?)
      -> LazyFilterIterator {
    let result: LazyFilterIterator?
    return result!
  }
}


struct BridgedStruct : Hashable, _ObjectiveCBridgeable {
  var hashValue: Int { return 0 }

  func _bridgeToObjectiveC() -> BridgedClass {
    return BridgedClass()
  }

  static func _forceBridgeFromObjectiveC(
    _ x: BridgedClass,
    result: inout BridgedStruct?) {
  }

  static func _conditionallyBridgeFromObjectiveC(
    _ x: BridgedClass,
    result: inout BridgedStruct?
  ) -> Bool {
    return true
  }

  static func _unconditionallyBridgeFromObjectiveC(_ source: BridgedClass?)
      -> BridgedStruct {
    var result: BridgedStruct? = nil
    _forceBridgeFromObjectiveC(source!, result: &result)
    return result!
  }
}

func ==(x: BridgedStruct, y: BridgedStruct) -> Bool { return true }

struct NotBridgedStruct : Hashable { 
  var hashValue: Int { return 0 }
}

func ==(x: NotBridgedStruct, y: NotBridgedStruct) -> Bool { return true }

class OtherClass : Hashable { 
  var hashValue: Int { return 0 }
}
func ==(x: OtherClass, y: OtherClass) -> Bool { return true }

// Basic bridging
func bridgeToObjC(_ s: BridgedStruct) -> BridgedClass {
  return s // expected-error{{cannot convert return expression of type 'BridgedStruct' to return type 'BridgedClass'}}
  return s as BridgedClass
}

func bridgeToAnyObject(_ s: BridgedStruct) -> AnyObject {
  return s // expected-error{{return expression of type 'BridgedStruct' does not conform to 'AnyObject'}}
  return s as AnyObject
}

func bridgeFromObjC(_ c: BridgedClass) -> BridgedStruct {
  return c // expected-error{{'BridgedClass' is not implicitly convertible to 'BridgedStruct'; did you mean to use 'as' to explicitly convert?}}
  return c as BridgedStruct
}

func bridgeFromObjCDerived(_ s: BridgedClassSub) -> BridgedStruct {
  return s // expected-error{{'BridgedClassSub' is not implicitly convertible to 'BridgedStruct'; did you mean to use 'as' to explicitly convert?}}
  return s as BridgedStruct
}

// Array -> NSArray
func arrayToNSArray() {
  var nsa: NSArray

  nsa = [AnyObject]() // expected-error {{cannot assign value of type '[AnyObject]' to type 'NSArray'}}
  nsa = [BridgedClass]() // expected-error {{cannot assign value of type '[BridgedClass]' to type 'NSArray'}}
  nsa = [OtherClass]() // expected-error {{cannot assign value of type '[OtherClass]' to type 'NSArray'}}
  nsa = [BridgedStruct]() // expected-error {{cannot assign value of type '[BridgedStruct]' to type 'NSArray'}}
  nsa = [NotBridgedStruct]() // expected-error{{cannot assign value of type '[NotBridgedStruct]' to type 'NSArray'}}

  nsa = [AnyObject]() as NSArray
  nsa = [BridgedClass]() as NSArray
  nsa = [OtherClass]() as NSArray
  nsa = [BridgedStruct]() as NSArray
  nsa = [NotBridgedStruct]() as NSArray
  _ = nsa
}

// NSArray -> Array
func nsArrayToArray(_ nsa: NSArray) {
  var arr1: [AnyObject] = nsa // expected-error{{'NSArray' is not implicitly convertible to '[AnyObject]'; did you mean to use 'as' to explicitly convert?}} {{30-30= as [AnyObject]}}
  var _: [BridgedClass] = nsa // expected-error{{'NSArray' is not convertible to '[BridgedClass]'}} {{30-30= as! [BridgedClass]}}
  var _: [OtherClass] = nsa // expected-error{{'NSArray' is not convertible to '[OtherClass]'}} {{28-28= as! [OtherClass]}}
  var _: [BridgedStruct] = nsa // expected-error{{'NSArray' is not convertible to '[BridgedStruct]'}} {{31-31= as! [BridgedStruct]}}
  var _: [NotBridgedStruct] = nsa // expected-error{{use 'as!' to force downcast}}

  var _: [AnyObject] = nsa as [AnyObject]
  var _: [BridgedClass] = nsa as [BridgedClass] // expected-error{{'NSArray' is not convertible to '[BridgedClass]'; did you mean to use 'as!' to force downcast?}} {{31-33=as!}}
  var _: [OtherClass] = nsa as [OtherClass] // expected-error{{'NSArray' is not convertible to '[OtherClass]'; did you mean to use 'as!' to force downcast?}} {{29-31=as!}}
  var _: [BridgedStruct] = nsa as [BridgedStruct] // expected-error{{'NSArray' is not convertible to '[BridgedStruct]'; did you mean to use 'as!' to force downcast?}} {{32-34=as!}}
  var _: [NotBridgedStruct] = nsa as [NotBridgedStruct] // expected-error{{use 'as!' to force downcast}}

  var arr6: Array = nsa as Array
  arr6 = arr1
  arr1 = arr6
}

func dictionaryToNSDictionary() {
  // FIXME: These diagnostics are awful.

  var nsd: NSDictionary

  nsd = [NSObject : AnyObject]() // expected-error {{cannot assign value of type '[NSObject : AnyObject]' to type 'NSDictionary'}}
  nsd = [NSObject : AnyObject]() as NSDictionary
  nsd = [NSObject : BridgedClass]() // expected-error {{cannot assign value of type '[NSObject : BridgedClass]' to type 'NSDictionary'}}
  nsd = [NSObject : BridgedClass]() as NSDictionary
  nsd = [NSObject : OtherClass]() // expected-error {{cannot assign value of type '[NSObject : OtherClass]' to type 'NSDictionary'}}
  nsd = [NSObject : OtherClass]() as NSDictionary
  nsd = [NSObject : BridgedStruct]() // expected-error {{cannot assign value of type '[NSObject : BridgedStruct]' to type 'NSDictionary'}}
  nsd = [NSObject : BridgedStruct]() as NSDictionary
  nsd = [NSObject : NotBridgedStruct]() // expected-error{{cannot assign value of type '[NSObject : NotBridgedStruct]' to type 'NSDictionary'}}
  nsd = [NSObject : NotBridgedStruct]() as NSDictionary

  nsd = [NSObject : BridgedClass?]() // expected-error{{cannot assign value of type '[NSObject : BridgedClass?]' to type 'NSDictionary'}}
  nsd = [NSObject : BridgedClass?]() as NSDictionary
  nsd = [NSObject : BridgedStruct?]()  // expected-error{{cannot assign value of type '[NSObject : BridgedStruct?]' to type 'NSDictionary'}}
  nsd = [NSObject : BridgedStruct?]() as NSDictionary

  nsd = [BridgedClass : AnyObject]() // expected-error {{cannot assign value of type '[BridgedClass : AnyObject]' to type 'NSDictionary'}}
  nsd = [BridgedClass : AnyObject]() as NSDictionary
  nsd = [OtherClass : AnyObject]() // expected-error {{cannot assign value of type '[OtherClass : AnyObject]' to type 'NSDictionary'}}
  nsd = [OtherClass : AnyObject]() as NSDictionary
  nsd = [BridgedStruct : AnyObject]() // expected-error {{cannot assign value of type '[BridgedStruct : AnyObject]' to type 'NSDictionary'}}
  nsd = [BridgedStruct : AnyObject]() as NSDictionary
  nsd = [NotBridgedStruct : AnyObject]()  // expected-error{{cannot assign value of type '[NotBridgedStruct : AnyObject]' to type 'NSDictionary'}}
  nsd = [NotBridgedStruct : AnyObject]() as NSDictionary

  // <rdar://problem/17134986>
  var bcOpt: BridgedClass?
  nsd = [BridgedStruct() : bcOpt]
  bcOpt = nil
  _ = nsd
}

// In this case, we should not implicitly convert Dictionary to NSDictionary.
struct NotEquatable {}
func notEquatableError(_ d: Dictionary<Int, NotEquatable>) -> Bool {
  // FIXME: Another awful diagnostic.
  return d == d // expected-error{{binary operator '==' cannot be applied to two 'Dictionary<Int, NotEquatable>' operands}}
  // expected-note @-1 {{overloads for '==' exist with these partially matching parameter lists: }}
}

// NSString -> String
var nss1 = "Some great text" as NSString
var nss2: NSString = ((nss1 as String) + ", Some more text") as NSString

// <rdar://problem/17943223>
var inferDouble = 1.0/10
let d: Double = 3.14159
inferDouble = d

// rdar://problem/17962491
var inferDouble2 = 1 % 3 / 3.0 // expected-error{{'%' is unavailable: Use truncatingRemainder instead}}
let d2: Double = 3.14159
inferDouble2 = d2

// rdar://problem/18269449
var i1: Int = 1.5 * 3.5 // expected-error {{binary operator '*' cannot be applied to two 'Double' operands}} expected-note {{expected an argument list of type '(Int, Int)'}}

// rdar://problem/18330319
func rdar18330319(_ s: String, d: [String : AnyObject]) {
  _ = d[s] as! String?
}

// rdar://problem/19551164
func rdar19551164a(_ s: String, _ a: [String]) {}
func rdar19551164b(_ s: NSString, _ a: NSArray) {
  rdar19551164a(s, a) // expected-error{{'NSString' is not implicitly convertible to 'String'; did you mean to use 'as' to explicitly convert?}}{{18-18= as String}}
  // expected-error@-1{{'NSArray' is not convertible to '[String]'; did you mean to use 'as!' to force downcast?}}{{21-21= as! [String]}}
}

// rdar://problem/19695671
func takesSet<T: Hashable>(_ p: Set<T>) {}  // expected-note {{in call to function 'takesSet'}}
func takesDictionary<K: Hashable, V>(_ p: Dictionary<K, V>) {} // expected-note {{in call to function 'takesDictionary'}}
func takesArray<T>(_ t: Array<T>) {} // expected-note {{in call to function 'takesArray'}}
func rdar19695671() {
  takesSet(NSSet() as! Set) // expected-error{{generic parameter 'T' could not be inferred}}
  takesDictionary(NSDictionary() as! Dictionary) // expected-error{{generic parameter 'K' could not be inferred}}
  takesArray(NSArray() as! Array) // expected-error{{generic parameter 'T' could not be inferred}}
}


// This failed at one point while fixing rdar://problem/19600325.
func getArrayOfAnyObject(_: AnyObject) -> [AnyObject] { return [] }
func testCallback(_ f: (AnyObject) -> AnyObject?) {}
testCallback { return getArrayOfAnyObject($0) } // expected-error {{cannot convert value of type '[AnyObject]' to closure result type 'AnyObject?'}}

// <rdar://problem/19724719> Type checker thinks "(optionalNSString ?? nonoptionalNSString) as String" is a forced cast
func rdar19724719(_ f: (String) -> (), s1: NSString?, s2: NSString) {
  f((s1 ?? s2) as String)
}

// <rdar://problem/19770981>
func rdar19770981(_ s: String, ns: NSString) {
  func f(_ s: String) {}
  f(ns) // expected-error{{'NSString' is not implicitly convertible to 'String'; did you mean to use 'as' to explicitly convert?}}{{7-7= as String}}
  f(ns as String)
  // 'as' has higher precedence than '>' so no parens are necessary with the fixit:
  s > ns // expected-error{{'NSString' is not implicitly convertible to 'String'; did you mean to use 'as' to explicitly convert?}}{{9-9= as String}}
  _ = s > ns as String
  ns > s // expected-error{{'NSString' is not implicitly convertible to 'String'; did you mean to use 'as' to explicitly convert?}}{{5-5= as String}}
  _ = ns as String > s

  // 'as' has lower precedence than '+' so add parens with the fixit:
  s + ns // expected-error{{'NSString' is not implicitly convertible to 'String'; did you mean to use 'as' to explicitly convert?}}{{7-7=(}}{{9-9= as String)}}
  _ = s + (ns as String)
  ns + s // expected-error{{'NSString' is not implicitly convertible to 'String'; did you mean to use 'as' to explicitly convert?}}{{3-3=(}}{{5-5= as String)}}
  _ = (ns as String) + s
}

// <rdar://problem/19831919> Fixit offers as! conversions that are known to always fail
func rdar19831919() {
  var s1 = 1 + "str"; // expected-error{{binary operator '+' cannot be applied to operands of type 'Int' and 'String'}} expected-note{{overloads for '+' exist with these partially matching parameter lists: (Int, Int), (String, String), (Int, UnsafeMutablePointer<Pointee>), (Int, UnsafePointer<Pointee>)}}
}

// <rdar://problem/19831698> Incorrect 'as' fixits offered for invalid literal expressions
func rdar19831698() {
  var v70 = true + 1 // expected-error{{binary operator '+' cannot be applied to operands of type 'Bool' and 'Int'}} expected-note {{overloads for '+' exist with these partially matching parameter lists: (Int, Int), (UnsafeMutablePointer<Pointee>, Int), (UnsafePointer<Pointee>, Int)}}
  var v71 = true + 1.0 // expected-error{{binary operator '+' cannot be applied to operands of type 'Bool' and 'Double'}}
// expected-note@-1{{overloads for '+'}}
  var v72 = true + true // expected-error{{binary operator '+' cannot be applied to two 'Bool' operands}}
  // expected-note @-1 {{overloads for '+' exist with these partially matching parameter lists:}}
  var v73 = true + [] // expected-error{{binary operator '+' cannot be applied to operands of type 'Bool' and '[Any]'}}
  // expected-note @-1 {{overloads for '+' exist with these partially matching parameter lists:}}
  var v75 = true + "str" // expected-error {{binary operator '+' cannot be applied to operands of type 'Bool' and 'String'}} expected-note {{expected an argument list of type '(String, String)'}}
}

// <rdar://problem/19836341> Incorrect fixit for NSString? to String? conversions
func rdar19836341(_ ns: NSString?, vns: NSString?) {
  var vns = vns
  let _: String? = ns // expected-error{{cannot convert value of type 'NSString?' to specified type 'String?'}}
  var _: String? = ns // expected-error{{cannot convert value of type 'NSString?' to specified type 'String?'}}
  // FIXME: there should be a fixit appending "as String?" to the line; for now
  // it's sufficient that it doesn't suggest appending "as String"

  // Important part about below diagnostic is that from-type is described as
  // 'NSString?' and not '@lvalue NSString?':
  let _: String? = vns // expected-error{{cannot convert value of type 'NSString?' to specified type 'String?'}}
  var _: String? = vns // expected-error{{cannot convert value of type 'NSString?' to specified type 'String?'}}
  
  vns = ns
}

// <rdar://problem/20029786> Swift compiler sometimes suggests changing "as!" to "as?!"
func rdar20029786(_ ns: NSString?) {
  var s: String = ns ?? "str" as String as String // expected-error{{cannot convert value of type 'NSString?' to expected argument type 'String?'}}
  var s2 = ns ?? "str" as String as String // expected-error {{binary operator '??' cannot be applied to operands of type 'NSString?' and 'String'}} expected-note{{}}

  let s3: NSString? = "str" as String? // expected-error {{cannot convert value of type 'String?' to specified type 'NSString?'}}

  var s4: String = ns ?? "str" // expected-error{{'NSString' is not implicitly convertible to 'String'; did you mean to use 'as' to explicitly convert?}}{{20-20=(}}{{31-31=) as String}}
  var s5: String = (ns ?? "str") as String // fixed version
}

// <rdar://problem/19813772> QoI: Using as! instead of as in this case produces really bad diagnostic
func rdar19813772(_ nsma: NSMutableArray) {
  var a1 = nsma as! Array // expected-error{{generic parameter 'Element' could not be inferred in cast to 'Array<_>'}}
  // FIXME: The following diagnostic is misleading and should not happen: expected-warning@-1{{cast from 'NSMutableArray' to unrelated type 'Array<_>' always fails}}
  var a2 = nsma as! Array<AnyObject> // expected-warning{{forced cast from 'NSMutableArray' to 'Array<AnyObject>' always succeeds; did you mean to use 'as'?}} {{17-20=as}}
  var a3 = nsma as Array<AnyObject>
}


// <rdar://problem/20336036> QoI: Add cast-removing fixit for "Forced cast from 'T' to 'T' always succeeds"
func force_cast_fixit(_ a : [NSString]) -> [NSString] {
  return a as! [NSString] // expected-warning {{forced cast of '[NSString]' to same type has no effect}} {{12-27=}}
}

// <rdar://problem/21244068> QoI: IUO prevents specific diagnostic + fixit about non-implicitly converted bridge types
func rdar21244068(_ n: NSString!) -> String {
  return n  // expected-error {{'NSString!' is not implicitly convertible to 'String'; did you mean to use 'as' to explicitly convert?}} {{11-11= as String}}
}

func forceBridgeDiag(_ obj: BridgedClass!) -> BridgedStruct {
  return obj // expected-error{{'BridgedClass!' is not implicitly convertible to 'BridgedStruct'; did you mean to use 'as' to explicitly convert?}}{{13-13= as BridgedStruct}}
}

struct KnownUnbridged {}
class KnownClass {}
protocol KnownClassProtocol: class {}

func forceUniversalBridgeToAnyObject<T, U: KnownClassProtocol>(a: T, b: U, c: Any, d: KnownUnbridged, e: KnownClass, f: KnownClassProtocol, g: AnyObject, h: String) {
  var z: AnyObject
  z = a as AnyObject
  z = b as AnyObject
  z = c as AnyObject
  z = d as AnyObject
  z = e as AnyObject
  z = f as AnyObject
  z = g as AnyObject
  z = h as AnyObject

  z = a // expected-error{{does not conform to 'AnyObject'}}
  z = b
  z = c // expected-error{{does not conform to 'AnyObject'}}
  z = d // expected-error{{does not conform to 'AnyObject'}}
  z = e
  z = f
  z = g
  z = h // expected-error{{does not conform to 'AnyObject'}}

  _ = z
}

func bridgeAnyContainerToAnyObject(x: [Any], y: [NSObject: Any]) {
  var z: AnyObject
  z = x as AnyObject
  z = y as AnyObject

  _ = z
}

func bridgeTupleToAnyObject() {
  let x = (1, "two")
  let y = x as AnyObject
  _ = y
}
