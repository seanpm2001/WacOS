// RUN: rm -rf %t
// RUN: mkdir -p %t
//
// RUN: %gyb %s -o %t/main.swift
// RUN: if [ %target-runtime == "objc" ]; then \
// RUN:   %target-clang -fobjc-arc %S/Inputs/SlurpFastEnumeration/SlurpFastEnumeration.m -c -o %t/SlurpFastEnumeration.o; \
// RUN:   %line-directive %t/main.swift -- %target-build-swift %S/Inputs/DictionaryKeyValueTypes.swift %S/Inputs/DictionaryKeyValueTypesObjC.swift %t/main.swift -I %S/Inputs/SlurpFastEnumeration/ -Xlinker %t/SlurpFastEnumeration.o -o %t/Set -Xfrontend -disable-access-control; \
// RUN: else \
// RUN:   %line-directive %t/main.swift -- %target-build-swift %S/Inputs/DictionaryKeyValueTypes.swift %t/main.swift -o %t/Set -Xfrontend -disable-access-control; \
// RUN: fi
//
// RUN: %line-directive %t/main.swift -- %target-run %t/Set
// REQUIRES: executable_test

import StdlibUnittest
import StdlibCollectionUnittest

// For rand32
import SwiftPrivate
#if _runtime(_ObjC)
import Foundation
#else
// for random, srandom
import Glibc
#endif

// For experimental Set operators
import SwiftExperimental

extension Set {
  func _rawIdentifier() -> Int {
    return unsafeBitCast(self, to: Int.self)
  }
}

// Check that the generic parameter is called 'Element'.
protocol TestProtocol1 {}

extension Set where Element : TestProtocol1 {
  var _elementIsTestProtocol1: Bool {
    fatalError("not implemented")
  }
}

extension SetIndex where Element : TestProtocol1 {
  var _elementIsTestProtocol1: Bool {
    fatalError("not implemented")
  }
}

extension SetIterator where Element : TestProtocol1 {
  var _elementIsTestProtocol1: Bool {
    fatalError("not implemented")
  }
}

let hugeNumberArray = Array(0..<500)

var SetTestSuite = TestSuite("Set")

SetTestSuite.setUp {
  resetLeaksOfDictionaryKeysValues()
#if _runtime(_ObjC)
  resetLeaksOfObjCDictionaryKeysValues()
#endif
}

SetTestSuite.tearDown {
  expectNoLeaksOfDictionaryKeysValues()
#if _runtime(_ObjC)
  expectNoLeaksOfObjCDictionaryKeysValues()
#endif
}

func getCOWFastSet(_ members: [Int] = [1010, 2020, 3030]) -> Set<Int> {
  var s = Set<Int>(minimumCapacity: 10)
  for member in members {
    s.insert(member)
  }
  expectTrue(isNativeSet(s))
  return s
}

func getCOWSlowSet(_ members: [Int] = [1010, 2020, 3030]) -> Set<TestKeyTy> {
  var s = Set<TestKeyTy>(minimumCapacity: 10)
  for member in members {
    s.insert(TestKeyTy(member))
  }
  expectTrue(isNativeSet(s))
  return s
}

func helperDeleteThree(_ k1: TestKeyTy, _ k2: TestKeyTy, _ k3: TestKeyTy) {
  var s1 = Set<TestKeyTy>(minimumCapacity: 10)

  s1.insert(k1)
  s1.insert(k2)
  s1.insert(k3)

  expectTrue(s1.contains(k1))
  expectTrue(s1.contains(k2))
  expectTrue(s1.contains(k3))

  s1.remove(k1)
  expectTrue(s1.contains(k2))
  expectTrue(s1.contains(k3))

  s1.remove(k2)
  expectTrue(s1.contains(k3))

  s1.remove(k3)
  expectEqual(0, s1.count)
}

func uniformRandom(_ max: Int) -> Int {
  return Int(rand32(exclusiveUpperBound: UInt32(max)))
}

func pickRandom<T>(_ a: [T]) -> T {
  return a[uniformRandom(a.count)]
}

func equalsUnordered(_ lhs: Set<Int>, _ rhs: Set<Int>) -> Bool {
  return lhs.sorted().elementsEqual(rhs.sorted()) {
    $0 == $1
  }
}

func isNativeSet<T : Hashable>(_ s: Set<T>) -> Bool {
  switch s._variantStorage {
  case .native:
    return true
  case .cocoa:
    return false
  }
}

#if _runtime(_ObjC)
func isNativeNSSet(_ s: NSSet) -> Bool {
  let className: NSString = NSStringFromClass(type(of: s)) as NSString
  return className.range(of: "NativeSetStorage").length > 0
}

func isCocoaNSSet(_ s: NSSet) -> Bool {
  let className: NSString = NSStringFromClass(type(of: s)) as NSString
  return className.range(of: "NSSet").length > 0 ||
    className.range(of: "NSCFSet").length > 0
}

func getBridgedEmptyNSSet() -> NSSet {
  let s = Set<TestObjCKeyTy>()

  let bridged = unsafeBitCast(convertSetToNSSet(s), to: NSSet.self)
  expectTrue(isNativeNSSet(bridged))

  return bridged
}

func isCocoaSet<T : Hashable>(_ s: Set<T>) -> Bool {
  return !isNativeSet(s)
}

/// Get an NSSet of TestObjCKeyTy values
func getAsNSSet(_ members: [Int] = [1010, 2020, 3030]) -> NSSet {
  let nsArray = NSMutableArray()
  for member in members {
    nsArray.add(TestObjCKeyTy(member))
  }
  return NSMutableSet(array: nsArray as [AnyObject])
}

/// Get an NSMutableSet of TestObjCKeyTy values
func getAsNSMutableSet(_ members: [Int] = [1010, 2020, 3030]) -> NSMutableSet {
  let nsArray = NSMutableArray()
  for member in members {
    nsArray.add(TestObjCKeyTy(member))
  }
  return NSMutableSet(array: nsArray as [AnyObject])
}

public func convertSetToNSSet<T>(_ s: Set<T>) -> NSSet {
  return s._bridgeToObjectiveC()
}

public func convertNSSetToSet<T : Hashable>(_ s: NSSet?) -> Set<T> {
  if _slowPath(s == nil) { return [] }
  var result: Set<T>?
  Set._forceBridgeFromObjectiveC(s!, result: &result)
  return result!
}

/// Get a Set<NSObject> (Set<TestObjCKeyTy>) backed by Cocoa storage
func getBridgedVerbatimSet(_ members: [Int] = [1010, 2020, 3030])
  -> Set<NSObject> {
  let nss = getAsNSSet(members)
  let result: Set<NSObject> = convertNSSetToSet(nss)
  expectTrue(isCocoaSet(result))
  return result
}

/// Get a Set<NSObject> (Set<TestObjCKeyTy>) backed by native storage
func getNativeBridgedVerbatimSet(_ members: [Int] = [1010, 2020, 3030]) ->
  Set<NSObject> {
  let result: Set<NSObject> = Set(members.map({ TestObjCKeyTy($0) }))
  expectTrue(isNativeSet(result))
  return result
}

/// Get a Set<NSObject> (Set<TestObjCKeyTy>) backed by Cocoa storage
func getHugeBridgedVerbatimSet() -> Set<NSObject> {
  let nss = getAsNSSet(hugeNumberArray)
  let result: Set<NSObject> = convertNSSetToSet(nss)
  expectTrue(isCocoaSet(result))
  return result
}

/// Get a Set<TestBridgedKeyTy> backed by native storage
func getBridgedNonverbatimSet(_ members: [Int] = [1010, 2020, 3030]) ->
  Set<TestBridgedKeyTy> {
  let nss = getAsNSSet(members)
  let _ = unsafeBitCast(nss, to: Int.self)
  let result: Set<TestBridgedKeyTy> =
    Swift._forceBridgeFromObjectiveC(nss, Set.self)
  expectTrue(isNativeSet(result))
  return result
}

/// Get a larger Set<TestBridgedKeyTy> backed by native storage
func getHugeBridgedNonverbatimSet() -> Set<TestBridgedKeyTy> {
  let nss = getAsNSSet(hugeNumberArray)
  let _ = unsafeBitCast(nss, to: Int.self)
  let result: Set<TestBridgedKeyTy> =
    Swift._forceBridgeFromObjectiveC(nss, Set.self)
  expectTrue(isNativeSet(result))
  return result
}

func getBridgedVerbatimSetAndNSMutableSet() -> (Set<NSObject>, NSMutableSet) {
  let nss = getAsNSMutableSet()
  return (convertNSSetToSet(nss), nss)
}

func getBridgedNonverbatimSetAndNSMutableSet()
    -> (Set<TestBridgedKeyTy>, NSMutableSet) {
  let nss = getAsNSMutableSet()
  return (Swift._forceBridgeFromObjectiveC(nss, Set.self), nss)
}

func getBridgedNSSetOfRefTypesBridgedVerbatim() -> NSSet {
  expectTrue(_isBridgedVerbatimToObjectiveC(TestObjCKeyTy.self))

  var s = Set<TestObjCKeyTy>(minimumCapacity: 32)
  s.insert(TestObjCKeyTy(1010))
  s.insert(TestObjCKeyTy(2020))
  s.insert(TestObjCKeyTy(3030))

  let bridged =
    unsafeBitCast(convertSetToNSSet(s), to: NSSet.self)

  expectTrue(isNativeNSSet(bridged))

  return bridged
}

func getBridgedNSSet_ValueTypesCustomBridged(
  numElements: Int = 3
) -> NSSet {
  expectTrue(!_isBridgedVerbatimToObjectiveC(TestBridgedKeyTy.self))

  var s = Set<TestBridgedKeyTy>()
  for i in 1..<(numElements + 1) {
    s.insert(TestBridgedKeyTy(i * 1000 + i * 10))
  }

  let bridged = convertSetToNSSet(s)
  expectTrue(isNativeNSSet(bridged))

  return bridged
}

func getRoundtripBridgedNSSet() -> NSSet {
  let items = NSMutableArray()
  items.add(TestObjCKeyTy(1010))
  items.add(TestObjCKeyTy(2020))
  items.add(TestObjCKeyTy(3030))

  let nss = NSSet(array: items as [AnyObject])

  let s: Set<NSObject> = convertNSSetToSet(nss)

  let bridgedBack = convertSetToNSSet(s)
  expectTrue(isCocoaNSSet(bridgedBack))
  // FIXME: this should be true.
  //expectTrue(unsafeBitCast(nsd, to: Int.self) == unsafeBitCast(bridgedBack, to: Int.self))

  return bridgedBack
}

func getBridgedNSSet_MemberTypesCustomBridged() -> NSSet {
  expectFalse(_isBridgedVerbatimToObjectiveC(TestBridgedKeyTy.self))

  var s = Set<TestBridgedKeyTy>()
  s.insert(TestBridgedKeyTy(1010))
  s.insert(TestBridgedKeyTy(2020))
  s.insert(TestBridgedKeyTy(3030))

  let bridged = convertSetToNSSet(s)
  expectTrue(isNativeNSSet(bridged))

  return bridged
}
#endif // _runtime(_ObjC)

SetTestSuite.test("AssociatedTypes") {
  typealias Collection = Set<MinimalHashableValue>
  expectCollectionAssociatedTypes(
    collectionType: Collection.self,
    iteratorType: SetIterator<MinimalHashableValue>.self,
    subSequenceType: Slice<Collection>.self,
    indexType: SetIndex<MinimalHashableValue>.self,
    indexDistanceType: Int.self,
    indicesType: DefaultIndices<Collection>.self)
}

SetTestSuite.test("sizeof") {
  var s = Set(["Hello", "world"])
#if arch(i386) || arch(arm)
  expectEqual(4, MemoryLayout.size(ofValue: s))
#else
  expectEqual(8, MemoryLayout.size(ofValue: s))
#endif
}

SetTestSuite.test("COW.Smoke") {
  var s1 = Set<TestKeyTy>(minimumCapacity: 10)
  for i in [1010, 2020, 3030]{ s1.insert(TestKeyTy(i)) }
  var identity1 = s1._rawIdentifier()

  var s2 = s1
  _fixLifetime(s2)
  expectEqual(identity1, s2._rawIdentifier())

  s2.insert(TestKeyTy(4040))
  expectNotEqual(identity1, s2._rawIdentifier())

  s2.insert(TestKeyTy(5050))
  expectEqual(identity1, s1._rawIdentifier())

  // Keep variables alive.
  _fixLifetime(s1)
  _fixLifetime(s2)
}

SetTestSuite.test("COW.Fast.IndexesDontAffectUniquenessCheck") {
  var s = getCOWFastSet()
  var identity1 = s._rawIdentifier()

  var startIndex = s.startIndex
  var endIndex = s.endIndex
  expectNotEqual(startIndex, endIndex)
  expectTrue(startIndex < endIndex)
  expectTrue(startIndex <= endIndex)
  expectFalse(startIndex >= endIndex)
  expectFalse(startIndex > endIndex)

  expectEqual(identity1, s._rawIdentifier())

  s.insert(4040)
  expectEqual(identity1, s._rawIdentifier())

  // Keep indexes alive during the calls above.
  _fixLifetime(startIndex)
  _fixLifetime(endIndex)
}

SetTestSuite.test("COW.Slow.IndexesDontAffectUniquenessCheck") {
  var s = getCOWSlowSet()
  var identity1 = s._rawIdentifier()

  var startIndex = s.startIndex
  var endIndex = s.endIndex
  expectNotEqual(startIndex, endIndex)
  expectTrue(startIndex < endIndex)
  expectTrue(startIndex <= endIndex)
  expectFalse(startIndex >= endIndex)
  expectFalse(startIndex > endIndex)

  expectEqual(identity1, s._rawIdentifier())
  s.insert(TestKeyTy(4040))
  expectEqual(identity1, s._rawIdentifier())

  // Keep indexes alive during the calls above.
  _fixLifetime(startIndex)
  _fixLifetime(endIndex)
}

SetTestSuite.test("COW.Fast.SubscriptWithIndexDoesNotReallocate") {
  var s = getCOWFastSet()
  var identity1 = s._rawIdentifier()

  var startIndex = s.startIndex
  let empty = startIndex == s.endIndex
  expectNotEqual(empty, (s.startIndex < s.endIndex))
  expectTrue(s.startIndex <= s.endIndex)
  expectEqual(empty, (s.startIndex >= s.endIndex))
  expectFalse(s.startIndex > s.endIndex)
  expectEqual(identity1, s._rawIdentifier())

  expectNotEqual(0, s[startIndex])
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("COW.Slow.SubscriptWithIndexDoesNotReallocate") {
  var s = getCOWSlowSet()
  var identity1 = s._rawIdentifier()

  var startIndex = s.startIndex
  let empty = startIndex == s.endIndex
  expectNotEqual(empty, (s.startIndex < s.endIndex))
  expectTrue(s.startIndex <= s.endIndex)
  expectEqual(empty, (s.startIndex >= s.endIndex))
  expectFalse(s.startIndex > s.endIndex)
  expectEqual(identity1, s._rawIdentifier())

  expectNotEqual(TestKeyTy(0), s[startIndex])
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("COW.Fast.ContainsDoesNotReallocate") {
  var s = getCOWFastSet()
  var identity1 = s._rawIdentifier()

  expectTrue(s.contains(1010))
  expectEqual(identity1, s._rawIdentifier())

  do {
    var s2: Set<MinimalHashableValue> = []
    MinimalHashableValue.timesEqualEqualWasCalled = 0
    MinimalHashableValue.timesHashValueWasCalled = 0
    expectFalse(s2.contains(MinimalHashableValue(42)))

    // If the set is empty, we shouldn't be computing the hash value of the
    // provided key.
    expectEqual(0, MinimalHashableValue.timesEqualEqualWasCalled)
    expectEqual(0, MinimalHashableValue.timesHashValueWasCalled)
  }
}

SetTestSuite.test("COW.Slow.ContainsDoesNotReallocate") {
  var s = getCOWSlowSet()
  var identity1 = s._rawIdentifier()

  expectTrue(s.contains(TestKeyTy(1010)))
  expectEqual(identity1, s._rawIdentifier())

  // Insert a new key-value pair.
  s.insert(TestKeyTy(4040))
  expectEqual(identity1, s._rawIdentifier())
  expectEqual(4, s.count)
  expectTrue(s.contains(TestKeyTy(1010)))
  expectTrue(s.contains(TestKeyTy(2020)))
  expectTrue(s.contains(TestKeyTy(3030)))
  expectTrue(s.contains(TestKeyTy(4040)))

  // Delete an existing key.
  s.remove(TestKeyTy(1010))
  expectEqual(identity1, s._rawIdentifier())
  expectEqual(3, s.count)
  expectTrue(s.contains(TestKeyTy(2020)))
  expectTrue(s.contains(TestKeyTy(3030)))
  expectTrue(s.contains(TestKeyTy(4040)))

  // Try to delete a key that does not exist.
  s.remove(TestKeyTy(777))
  expectEqual(identity1, s._rawIdentifier())
  expectEqual(3, s.count)
  expectTrue(s.contains(TestKeyTy(2020)))
  expectTrue(s.contains(TestKeyTy(3030)))
  expectTrue(s.contains(TestKeyTy(4040)))

  do {
    var s2: Set<MinimalHashableClass> = []
    MinimalHashableClass.timesEqualEqualWasCalled = 0
    MinimalHashableClass.timesHashValueWasCalled = 0
    expectFalse(s2.contains(MinimalHashableClass(42)))

    // If the set is empty, we shouldn't be computing the hash value of the
    // provided key.
    expectEqual(0, MinimalHashableClass.timesEqualEqualWasCalled)
    expectEqual(0, MinimalHashableClass.timesHashValueWasCalled)
  }
}

SetTestSuite.test("COW.Fast.InsertDoesNotReallocate") {
  var s1 = getCOWFastSet()

  let identity1 = s1._rawIdentifier()
  let count1 = s1.count

  // Inserting a redundant element should not create new storage
  s1.insert(2020)
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(count1, s1.count)

  s1.insert(4040)
  s1.insert(5050)
  s1.insert(6060)
  expectEqual(count1 + 3, s1.count)
  expectEqual(identity1, s1._rawIdentifier())
}

SetTestSuite.test("COW.Slow.InsertDoesNotReallocate") {
  do {
    var s1 = getCOWSlowSet()

    let identity1 = s1._rawIdentifier()
    let count1 = s1.count

    // Inserting a redundant element should not create new storage
    s1.insert(TestKeyTy(2020))
    expectEqual(identity1, s1._rawIdentifier())
    expectEqual(count1, s1.count)

    s1.insert(TestKeyTy(4040))
    s1.insert(TestKeyTy(5050))
    s1.insert(TestKeyTy(6060))
    expectEqual(count1 + 3, s1.count)
    expectEqual(identity1, s1._rawIdentifier())
  }

  do {
    var s1 = getCOWSlowSet()
    var identity1 = s1._rawIdentifier()

    var s2 = s1
    expectEqual(identity1, s1._rawIdentifier())
    expectEqual(identity1, s2._rawIdentifier())

    s2.insert(TestKeyTy(2040))
    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity1, s2._rawIdentifier())

    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestKeyTy(1010)))
    expectTrue(s1.contains(TestKeyTy(2020)))
    expectTrue(s1.contains(TestKeyTy(3030)))
    expectFalse(s1.contains(TestKeyTy(2040)))

    expectEqual(4, s2.count)
    expectTrue(s2.contains(TestKeyTy(1010)))
    expectTrue(s2.contains(TestKeyTy(2020)))
    expectTrue(s2.contains(TestKeyTy(3030)))
    expectTrue(s2.contains(TestKeyTy(2040)))

    // Keep variables alive.
    _fixLifetime(s1)
    _fixLifetime(s2)
  }

  do {
    var s1 = getCOWSlowSet()
    var identity1 = s1._rawIdentifier()

    var s2 = s1
    expectEqual(identity1, s1._rawIdentifier())
    expectEqual(identity1, s2._rawIdentifier())

    // Replace a redundant element.
    s2.update(with: TestKeyTy(2020))
    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity1, s2._rawIdentifier())

    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestKeyTy(1010)))
    expectTrue(s1.contains(TestKeyTy(2020)))
    expectTrue(s1.contains(TestKeyTy(3030)))

    expectEqual(3, s2.count)
    expectTrue(s2.contains(TestKeyTy(1010)))
    expectTrue(s2.contains(TestKeyTy(2020)))
    expectTrue(s2.contains(TestKeyTy(3030)))

    // Keep variables alive.
    _fixLifetime(s1)
    _fixLifetime(s2)
  }
}

SetTestSuite.test("COW.Fast.IndexForMemberDoesNotReallocate") {
  var s = getCOWFastSet()
  var identity1 = s._rawIdentifier()

  // Find an existing key.
  do {
    var foundIndex1 = s.index(of: 1010)!
    expectEqual(identity1, s._rawIdentifier())

    var foundIndex2 = s.index(of: 1010)!
    expectEqual(foundIndex1, foundIndex2)

    expectEqual(1010, s[foundIndex1])
    expectEqual(identity1, s._rawIdentifier())
  }

  // Try to find a key that is not present.
  do {
    var foundIndex1 = s.index(of: 1111)
    expectEmpty(foundIndex1)
    expectEqual(identity1, s._rawIdentifier())
  }

  do {
    var s2: Set<MinimalHashableValue> = []
    MinimalHashableValue.timesEqualEqualWasCalled = 0
    MinimalHashableValue.timesHashValueWasCalled = 0
    expectEmpty(s2.index(of: MinimalHashableValue(42)))

    // If the set is empty, we shouldn't be computing the hash value of the
    // provided key.
    expectEqual(0, MinimalHashableValue.timesEqualEqualWasCalled)
    expectEqual(0, MinimalHashableValue.timesHashValueWasCalled)
  }
}

SetTestSuite.test("COW.Slow.IndexForMemberDoesNotReallocate") {
  var s = getCOWSlowSet()
  var identity1 = s._rawIdentifier()

  // Find an existing key.
  do {
    var foundIndex1 = s.index(of: TestKeyTy(1010))!
    expectEqual(identity1, s._rawIdentifier())

    var foundIndex2 = s.index(of: TestKeyTy(1010))!
    expectEqual(foundIndex1, foundIndex2)

    expectEqual(TestKeyTy(1010), s[foundIndex1])
    expectEqual(identity1, s._rawIdentifier())
  }

  // Try to find a key that is not present.
  do {
    var foundIndex1 = s.index(of: TestKeyTy(1111))
    expectEmpty(foundIndex1)
    expectEqual(identity1, s._rawIdentifier())
  }

  do {
    var s2: Set<MinimalHashableClass> = []
    MinimalHashableClass.timesEqualEqualWasCalled = 0
    MinimalHashableClass.timesHashValueWasCalled = 0
    expectEmpty(s2.index(of: MinimalHashableClass(42)))

    // If the set is empty, we shouldn't be computing the hash value of the
    // provided key.
    expectEqual(0, MinimalHashableClass.timesEqualEqualWasCalled)
    expectEqual(0, MinimalHashableClass.timesHashValueWasCalled)
  }
}

SetTestSuite.test("COW.Fast.RemoveAtDoesNotReallocate") {
  do {
    var s = getCOWFastSet()
    var identity1 = s._rawIdentifier()

    let foundIndex1 = s.index(of: 1010)!
    expectEqual(identity1, s._rawIdentifier())

    expectEqual(1010, s[foundIndex1])

    let removed = s.remove(at: foundIndex1)
    expectEqual(1010, removed)

    expectEqual(identity1, s._rawIdentifier())
    expectEmpty(s.index(of: 1010))
  }

  do {
    var s1 = getCOWFastSet()
    var identity1 = s1._rawIdentifier()

    var s2 = s1
    expectEqual(identity1, s1._rawIdentifier())
    expectEqual(identity1, s2._rawIdentifier())

    var foundIndex1 = s2.index(of: 1010)!
    expectEqual(1010, s2[foundIndex1])
    expectEqual(identity1, s1._rawIdentifier())
    expectEqual(identity1, s2._rawIdentifier())

    let removed = s2.remove(at: foundIndex1)
    expectEqual(1010, removed)

    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity1, s2._rawIdentifier())
    expectEmpty(s2.index(of: 1010))
  }
}

SetTestSuite.test("COW.Slow.RemoveAtDoesNotReallocate") {
  do {
    var s = getCOWSlowSet()
    var identity1 = s._rawIdentifier()

    let foundIndex1 = s.index(of: TestKeyTy(1010))!
    expectEqual(identity1, s._rawIdentifier())

    expectEqual(TestKeyTy(1010), s[foundIndex1])

    let removed = s.remove(at: foundIndex1)
    expectEqual(TestKeyTy(1010), removed)

    expectEqual(identity1, s._rawIdentifier())
    expectEmpty(s.index(of: TestKeyTy(1010)))
  }

  do {
    var s1 = getCOWSlowSet()
    var identity1 = s1._rawIdentifier()

    var s2 = s1
    expectEqual(identity1, s1._rawIdentifier())
    expectEqual(identity1, s2._rawIdentifier())

    var foundIndex1 = s2.index(of: TestKeyTy(1010))!
    expectEqual(TestKeyTy(1010), s2[foundIndex1])
    expectEqual(identity1, s1._rawIdentifier())
    expectEqual(identity1, s2._rawIdentifier())

    let removed = s2.remove(at: foundIndex1)
    expectEqual(TestKeyTy(1010), removed)

    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity1, s2._rawIdentifier())
    expectEmpty(s2.index(of: TestKeyTy(1010)))
  }
}

SetTestSuite.test("COW.Fast.RemoveDoesNotReallocate") {
  do {
    var s1 = getCOWFastSet()
    var identity1 = s1._rawIdentifier()

    var deleted = s1.remove(0)
    expectEmpty(deleted)
    expectEqual(identity1, s1._rawIdentifier())

    deleted = s1.remove(1010)
    expectOptionalEqual(1010, deleted)
    expectEqual(identity1, s1._rawIdentifier())

    // Keep variables alive.
    _fixLifetime(s1)
  }

  do {
    var s1 = getCOWFastSet()
    var identity1 = s1._rawIdentifier()

    var s2 = s1
    var deleted = s2.remove(0)
    expectEmpty(deleted)
    expectEqual(identity1, s1._rawIdentifier())
    expectEqual(identity1, s2._rawIdentifier())

    deleted = s2.remove(1010)
    expectOptionalEqual(1010, deleted)
    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity1, s2._rawIdentifier())

    // Keep variables alive.
    _fixLifetime(s1)
    _fixLifetime(s2)
  }
}

SetTestSuite.test("COW.Slow.RemoveDoesNotReallocate") {
  do {
    var s1 = getCOWSlowSet()
    var identity1 = s1._rawIdentifier()

    var deleted = s1.remove(TestKeyTy(0))
    expectEmpty(deleted)
    expectEqual(identity1, s1._rawIdentifier())

    deleted = s1.remove(TestKeyTy(1010))
    expectOptionalEqual(TestKeyTy(1010), deleted)
    expectEqual(identity1, s1._rawIdentifier())

    // Keep variables alive.
    _fixLifetime(s1)
  }

  do {
    var s1 = getCOWSlowSet()
    var identity1 = s1._rawIdentifier()

    var s2 = s1
    var deleted = s2.remove(TestKeyTy(0))
    expectEmpty(deleted)
    expectEqual(identity1, s1._rawIdentifier())
    expectEqual(identity1, s2._rawIdentifier())

    deleted = s2.remove(TestKeyTy(1010))
    expectOptionalEqual(TestKeyTy(1010), deleted)
    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity1, s2._rawIdentifier())

    // Keep variables alive.
    _fixLifetime(s1)
    _fixLifetime(s2)
  }
}

SetTestSuite.test("COW.Fast.UnionInPlaceSmallSetDoesNotReallocate") {
  var s1 = getCOWFastSet()
  let s2 = Set([4040, 5050, 6060])
  let s3 = Set([1010, 2020, 3030, 4040, 5050, 6060])

  let identity1 = s1._rawIdentifier()

  // Adding the empty set should obviously not allocate
  s1.formUnion(Set<Int>())
  expectEqual(identity1, s1._rawIdentifier())

  // adding a small set shouldn't cause a reallocation
  s1.formUnion(s2)
  expectEqual(s1, s3)
  expectEqual(identity1, s1._rawIdentifier())
}

SetTestSuite.test("COW.Fast.RemoveAllDoesNotReallocate") {
  do {
    var s = getCOWFastSet()
    let originalCapacity = s._variantStorage.asNative.capacity
    expectEqual(3, s.count)
    expectTrue(s.contains(1010))

    s.removeAll()
    // We cannot expectTrue that identity changed, since the new buffer of
    // smaller size can be allocated at the same address as the old one.
    var identity1 = s._rawIdentifier()
    expectTrue(s._variantStorage.asNative.capacity < originalCapacity)
    expectEqual(0, s.count)
    expectFalse(s.contains(1010))

    s.removeAll()
    expectEqual(identity1, s._rawIdentifier())
    expectEqual(0, s.count)
    expectFalse(s.contains(1010))
  }

  do {
    var s = getCOWFastSet()
    var identity1 = s._rawIdentifier()
    let originalCapacity = s._variantStorage.asNative.capacity
    expectEqual(3, s.count)
    expectTrue(s.contains(1010))

    s.removeAll(keepingCapacity: true)
    expectEqual(identity1, s._rawIdentifier())
    expectEqual(originalCapacity, s._variantStorage.asNative.capacity)
    expectEqual(0, s.count)
    expectFalse(s.contains(1010))

    s.removeAll(keepingCapacity: true)
    expectEqual(identity1, s._rawIdentifier())
    expectEqual(originalCapacity, s._variantStorage.asNative.capacity)
    expectEqual(0, s.count)
    expectFalse(s.contains(1010))
  }

  do {
    var s1 = getCOWFastSet()
    var identity1 = s1._rawIdentifier()
    expectEqual(3, s1.count)
    expectTrue(s1.contains(1010))

    var s2 = s1
    s2.removeAll()
    var identity2 = s2._rawIdentifier()
    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity1, identity2)
    expectEqual(3, s1.count)
    expectTrue(s1.contains(1010))
    expectEqual(0, s2.count)
    expectFalse(s2.contains(1010))

    // Keep variables alive.
    _fixLifetime(s1)
    _fixLifetime(s2)
  }

  do {
    var s1 = getCOWFastSet()
    var identity1 = s1._rawIdentifier()
    let originalCapacity = s1._variantStorage.asNative.capacity
    expectEqual(3, s1.count)
    expectTrue(s1.contains(1010))

    var s2 = s1
    s2.removeAll(keepingCapacity: true)
    var identity2 = s2._rawIdentifier()
    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity1, identity2)
    expectEqual(3, s1.count)
    expectTrue(s1.contains(1010))
    expectEqual(originalCapacity, s2._variantStorage.asNative.capacity)
    expectEqual(0, s2.count)
    expectFalse(s2.contains(1010))

    // Keep variables alive.
    _fixLifetime(s1)
    _fixLifetime(s2)
  }
}

SetTestSuite.test("COW.Slow.RemoveAllDoesNotReallocate") {
  do {
    var s = getCOWSlowSet()
    let originalCapacity = s._variantStorage.asNative.capacity
    expectEqual(3, s.count)
    expectTrue(s.contains(TestKeyTy(1010)))

    s.removeAll()
    // We cannot expectTrue that identity changed, since the new buffer of
    // smaller size can be allocated at the same address as the old one.
    var identity1 = s._rawIdentifier()
    expectTrue(s._variantStorage.asNative.capacity < originalCapacity)
    expectEqual(0, s.count)
    expectFalse(s.contains(TestKeyTy(1010)))

    s.removeAll()
    expectEqual(identity1, s._rawIdentifier())
    expectEqual(0, s.count)
    expectFalse(s.contains(TestKeyTy(1010)))
  }

  do {
    var s = getCOWSlowSet()
    var identity1 = s._rawIdentifier()
    let originalCapacity = s._variantStorage.asNative.capacity
    expectEqual(3, s.count)
    expectTrue(s.contains(TestKeyTy(1010)))

    s.removeAll(keepingCapacity: true)
    expectEqual(identity1, s._rawIdentifier())
    expectEqual(originalCapacity, s._variantStorage.asNative.capacity)
    expectEqual(0, s.count)
    expectFalse(s.contains(TestKeyTy(1010)))

    s.removeAll(keepingCapacity: true)
    expectEqual(identity1, s._rawIdentifier())
    expectEqual(originalCapacity, s._variantStorage.asNative.capacity)
    expectEqual(0, s.count)
    expectFalse(s.contains(TestKeyTy(1010)))
  }

  do {
    var s1 = getCOWSlowSet()
    var identity1 = s1._rawIdentifier()
    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestKeyTy(1010)))

    var s2 = s1
    s2.removeAll()
    var identity2 = s2._rawIdentifier()
    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity1, identity2)
    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestKeyTy(1010)))
    expectEqual(0, s2.count)
    expectFalse(s2.contains(TestKeyTy(1010)))

    // Keep variables alive.
    _fixLifetime(s1)
    _fixLifetime(s2)
  }

  do {
    var s1 = getCOWSlowSet()
    var identity1 = s1._rawIdentifier()
    let originalCapacity = s1._variantStorage.asNative.capacity
    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestKeyTy(1010)))

    var s2 = s1
    s2.removeAll(keepingCapacity: true)
    var identity2 = s2._rawIdentifier()
    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity1, identity2)
    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestKeyTy(1010)))
    expectEqual(originalCapacity, s2._variantStorage.asNative.capacity)
    expectEqual(0, s2.count)
    expectFalse(s2.contains(TestKeyTy(1010)))

    // Keep variables alive.
    _fixLifetime(s1)
    _fixLifetime(s2)
  }
}

SetTestSuite.test("COW.Fast.FirstDoesNotReallocate") {
  var s = getCOWFastSet()
  var identity1 = s._rawIdentifier()

  expectNotEmpty(s.first)
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("COW.Fast.CountDoesNotReallocate") {
  var s = getCOWFastSet()
  var identity1 = s._rawIdentifier()

  expectEqual(3, s.count)
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("COW.Slow.FirstDoesNotReallocate") {
  var s = getCOWSlowSet()
  var identity1 = s._rawIdentifier()

  expectNotEmpty(s.first)
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("COW.Slow.CountDoesNotReallocate") {
  var s = getCOWSlowSet()
  var identity1 = s._rawIdentifier()

  expectEqual(3, s.count)
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("COW.Fast.GenerateDoesNotReallocate") {
  var s = getCOWFastSet()
  var identity1 = s._rawIdentifier()

  var iter = s.makeIterator()
  var items: [Int] = []
  while let value = iter.next() {
    items += [value]
  }
  expectTrue(equalsUnordered(items, [ 1010, 2020, 3030 ]))
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("COW.Slow.GenerateDoesNotReallocate") {
  var s = getCOWSlowSet()
  var identity1 = s._rawIdentifier()

  var iter = s.makeIterator()
  var items: [Int] = []
  while let value = iter.next() {
    items.append(value.value)
  }
  expectTrue(equalsUnordered(items, [ 1010, 2020, 3030 ]))
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("COW.Fast.EqualityTestDoesNotReallocate") {
  var s1 = getCOWFastSet()
  var identity1 = s1._rawIdentifier()

  var s2 = getCOWFastSet()
  var identity2 = s2._rawIdentifier()

  expectEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(identity2, s2._rawIdentifier())
}

SetTestSuite.test("COW.Slow.EqualityTestDoesNotReallocate") {
  var s1 = getCOWFastSet()
  var identity1 = s1._rawIdentifier()

  var s2 = getCOWFastSet()
  var identity2 = s2._rawIdentifier()

  expectEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(identity2, s2._rawIdentifier())
}

//===---
// Native set tests.
//===---

SetTestSuite.test("deleteChainCollision") {
  var k1 = TestKeyTy(value: 1010, hashValue: 0)
  var k2 = TestKeyTy(value: 2020, hashValue: 0)
  var k3 = TestKeyTy(value: 3030, hashValue: 0)

  helperDeleteThree(k1, k2, k3)
}

SetTestSuite.test("deleteChainNoCollision") {
  var k1 = TestKeyTy(value: 1010, hashValue: 0)
  var k2 = TestKeyTy(value: 2020, hashValue: 1)
  var k3 = TestKeyTy(value: 3030, hashValue: 2)

  helperDeleteThree(k1, k2, k3)
}

SetTestSuite.test("deleteChainCollision2") {
  var k1_0 = TestKeyTy(value: 1010, hashValue: 0)
  var k2_0 = TestKeyTy(value: 2020, hashValue: 0)
  var k3_2 = TestKeyTy(value: 3030, hashValue: 2)
  var k4_0 = TestKeyTy(value: 4040, hashValue: 0)
  var k5_2 = TestKeyTy(value: 5050, hashValue: 2)
  var k6_0 = TestKeyTy(value: 6060, hashValue: 0)

  var s = Set<TestKeyTy>(minimumCapacity: 10)

  s.insert(k1_0) // in bucket 0
  s.insert(k2_0) // in bucket 1
  s.insert(k3_2) // in bucket 2
  s.insert(k4_0) // in bucket 3
  s.insert(k5_2) // in bucket 4
  s.insert(k6_0) // in bucket 5

  s.remove(k3_2)

  expectTrue(s.contains(k1_0))
  expectTrue(s.contains(k2_0))
  expectFalse(s.contains(k3_2))
  expectTrue(s.contains(k4_0))
  expectTrue(s.contains(k5_2))
  expectTrue(s.contains(k6_0))
}

SetTestSuite.test("deleteChainCollisionRandomized") {
  let timeNow = CUnsignedInt(time(nil))
  print("time is \(timeNow)")
  srandom(timeNow)

  func check(_ s: Set<TestKeyTy>) {
    var keys = Array(s)
    for i in 0..<keys.count {
      for j in 0..<i {
        expectNotEqual(keys[i], keys[j])
      }
    }

    for k in keys {
      expectTrue(s.contains(k))
    }
  }

  var collisionChainsChoices = Array(1...8)
  var chainOverlapChoices = Array(0...5)

  var collisionChains = pickRandom(collisionChainsChoices)
  var chainOverlap = pickRandom(chainOverlapChoices)
  print("chose parameters: collisionChains=\(collisionChains) chainLength=\(chainOverlap)")

  let chainLength = 7

  var knownKeys: [TestKeyTy] = []
  func getKey(_ value: Int) -> TestKeyTy {
    for k in knownKeys {
      if k.value == value {
        return k
      }
    }
    let hashValue = uniformRandom(chainLength - chainOverlap) * collisionChains
    let k = TestKeyTy(value: value, hashValue: hashValue)
    knownKeys += [k]
    return k
  }

  var s = Set<TestKeyTy>(minimumCapacity: 30)
  for i in 1..<300 {
    let key = getKey(uniformRandom(collisionChains * chainLength))
    if uniformRandom(chainLength * 2) == 0 {
      s.remove(key)
    } else {
      s.insert(TestKeyTy(key.value))
    }
    check(s)
  }
}

#if _runtime(_ObjC)
@objc
class CustomImmutableNSSet : NSSet {
  init(_privateInit: ()) {
    super.init()
  }

  override init() {
    expectUnreachable()
    super.init()
  }

  override init(objects: UnsafePointer<AnyObject>, count: Int) {
    expectUnreachable()
    super.init(objects: objects, count: count)
  }

  required init(coder aDecoder: NSCoder) {
    fatalError("init(coder:) not implemented by CustomImmutableNSSet")
  }

  @objc(copyWithZone:)
  override func copy(with zone: NSZone?) -> Any {
    CustomImmutableNSSet.timesCopyWithZoneWasCalled += 1
    return self
  }

  override func member(_ object: Any) -> Any? {
    CustomImmutableNSSet.timesMemberWasCalled += 1
    return getAsNSSet([ 1010, 1020, 1030 ]).member(object)
  }

  override func objectEnumerator() -> NSEnumerator {
    CustomImmutableNSSet.timesObjectEnumeratorWasCalled += 1
    return getAsNSSet([ 1010, 1020, 1030 ]).objectEnumerator()
  }

  override var count: Int {
    CustomImmutableNSSet.timesCountWasCalled += 1
    return 3
  }

  static var timesCopyWithZoneWasCalled = 0
  static var timesMemberWasCalled = 0
  static var timesObjectEnumeratorWasCalled = 0
  static var timesCountWasCalled = 0
}


SetTestSuite.test("BridgedFromObjC.Verbatim.SetIsCopied") {
  var (s, nss) = getBridgedVerbatimSetAndNSMutableSet()
  expectTrue(isCocoaSet(s))

  expectTrue(s.contains(TestObjCKeyTy(1010)))
  expectNotEmpty(nss.member(TestObjCKeyTy(1010)))

  nss.remove(TestObjCKeyTy(1010))
  expectEmpty(nss.member(TestObjCKeyTy(1010)))

  expectTrue(s.contains(TestObjCKeyTy(1010)))
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.SetIsCopied") {
  var (s, nss) = getBridgedNonverbatimSetAndNSMutableSet()
  expectTrue(isNativeSet(s))

  expectTrue(s.contains(TestBridgedKeyTy(1010)))
  expectNotEmpty(nss.member(TestBridgedKeyTy(1010) as AnyObject))

  nss.remove(TestBridgedKeyTy(1010) as AnyObject)
  expectEmpty(nss.member(TestBridgedKeyTy(1010) as AnyObject))

  expectTrue(s.contains(TestBridgedKeyTy(1010)))
}


SetTestSuite.test("BridgedFromObjC.Verbatim.NSSetIsRetained") {
  var nss: NSSet = NSSet(set: getAsNSSet([ 1010, 1020, 1030 ]))

  var s: Set<NSObject> = convertNSSetToSet(nss)

  var bridgedBack: NSSet = convertSetToNSSet(s)

  expectEqual(
    unsafeBitCast(nss, to: Int.self),
    unsafeBitCast(bridgedBack, to: Int.self))

  _fixLifetime(nss)
  _fixLifetime(s)
  _fixLifetime(bridgedBack)
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.NSSetIsCopied") {
  var nss: NSSet = NSSet(set: getAsNSSet([ 1010, 1020, 1030 ]))

  var s: Set<TestBridgedKeyTy> = convertNSSetToSet(nss)

  var bridgedBack: NSSet = convertSetToNSSet(s)

  expectNotEqual(
    unsafeBitCast(nss, to: Int.self),
    unsafeBitCast(bridgedBack, to: Int.self))

  _fixLifetime(nss)
  _fixLifetime(s)
  _fixLifetime(bridgedBack)
}


SetTestSuite.test("BridgedFromObjC.Verbatim.ImmutableSetIsRetained") {
  var nss: NSSet = CustomImmutableNSSet(_privateInit: ())

  CustomImmutableNSSet.timesCopyWithZoneWasCalled = 0
  CustomImmutableNSSet.timesMemberWasCalled = 0
  CustomImmutableNSSet.timesObjectEnumeratorWasCalled = 0
  CustomImmutableNSSet.timesCountWasCalled = 0
  var s: Set<NSObject> = convertNSSetToSet(nss)
  expectEqual(1, CustomImmutableNSSet.timesCopyWithZoneWasCalled)
  expectEqual(0, CustomImmutableNSSet.timesMemberWasCalled)
  expectEqual(0, CustomImmutableNSSet.timesObjectEnumeratorWasCalled)
  expectEqual(0, CustomImmutableNSSet.timesCountWasCalled)

  var bridgedBack: NSSet = convertSetToNSSet(s)
  expectEqual(
    unsafeBitCast(nss, to: Int.self),
    unsafeBitCast(bridgedBack, to: Int.self))

  _fixLifetime(nss)
  _fixLifetime(s)
  _fixLifetime(bridgedBack)
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.ImmutableSetIsCopied") {
  var nss: NSSet = CustomImmutableNSSet(_privateInit: ())

  CustomImmutableNSSet.timesCopyWithZoneWasCalled = 0
  CustomImmutableNSSet.timesMemberWasCalled = 0
  CustomImmutableNSSet.timesObjectEnumeratorWasCalled = 0
  CustomImmutableNSSet.timesCountWasCalled = 0
  var s: Set<TestBridgedKeyTy> = convertNSSetToSet(nss)
  expectEqual(0, CustomImmutableNSSet.timesCopyWithZoneWasCalled)
  expectEqual(0, CustomImmutableNSSet.timesMemberWasCalled)
  expectEqual(1, CustomImmutableNSSet.timesObjectEnumeratorWasCalled)
  expectNotEqual(0, CustomImmutableNSSet.timesCountWasCalled)

  var bridgedBack: NSSet = convertSetToNSSet(s)
  expectNotEqual(
    unsafeBitCast(nss, to: Int.self),
    unsafeBitCast(bridgedBack, to: Int.self))

  _fixLifetime(nss)
  _fixLifetime(s)
  _fixLifetime(bridgedBack)
}


SetTestSuite.test("BridgedFromObjC.Verbatim.IndexForMember") {
  var s = getBridgedVerbatimSet()
  var identity1 = s._rawIdentifier()
  expectTrue(isCocoaSet(s))

  // Find an existing key.
  var member = s[s.index(of: TestObjCKeyTy(1010))!]
  expectEqual(TestObjCKeyTy(1010), member)

  member = s[s.index(of: TestObjCKeyTy(2020))!]
  expectEqual(TestObjCKeyTy(2020), member)

  member = s[s.index(of: TestObjCKeyTy(3030))!]
  expectEqual(TestObjCKeyTy(3030), member)

  // Try to find a key that does not exist.
  expectEmpty(s.index(of: TestObjCKeyTy(4040)))
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.IndexForMember") {
  var s = getBridgedNonverbatimSet()
  var identity1 = s._rawIdentifier()

  do {
    var member = s[s.index(of: TestBridgedKeyTy(1010))!]
    expectEqual(TestBridgedKeyTy(1010), member)

    member = s[s.index(of: TestBridgedKeyTy(2020))!]
    expectEqual(TestBridgedKeyTy(2020), member)

    member = s[s.index(of: TestBridgedKeyTy(3030))!]
    expectEqual(TestBridgedKeyTy(3030), member)
  }

  expectEmpty(s.index(of: TestBridgedKeyTy(4040)))
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Verbatim.Insert") {
  do {
    var s = getBridgedVerbatimSet()
    var identity1 = s._rawIdentifier()
    expectTrue(isCocoaSet(s))

    expectFalse(s.contains(TestObjCKeyTy(2040)))
    s.insert(TestObjCKeyTy(2040))

    var identity2 = s._rawIdentifier()
    expectNotEqual(identity1, identity2)
    expectTrue(isNativeSet(s))
    expectEqual(4, s.count)

    expectTrue(s.contains(TestObjCKeyTy(1010)))
    expectTrue(s.contains(TestObjCKeyTy(2020)))
    expectTrue(s.contains(TestObjCKeyTy(3030)))
    expectTrue(s.contains(TestObjCKeyTy(2040)))
  }
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.Insert") {
  do {
    var s = getBridgedNonverbatimSet()
    var identity1 = s._rawIdentifier()
    expectTrue(isNativeSet(s))

    expectFalse(s.contains(TestBridgedKeyTy(2040)))
    s.insert(TestObjCKeyTy(2040) as TestBridgedKeyTy)

    var identity2 = s._rawIdentifier()
    expectNotEqual(identity1, identity2)
    expectTrue(isNativeSet(s))
    expectEqual(4, s.count)

    expectTrue(s.contains(TestBridgedKeyTy(1010)))
    expectTrue(s.contains(TestBridgedKeyTy(2020)))
    expectTrue(s.contains(TestBridgedKeyTy(3030)))
    expectTrue(s.contains(TestBridgedKeyTy(2040)))
  }
}

SetTestSuite.test("BridgedFromObjC.Verbatim.SubscriptWithIndex") {
  var s = getBridgedVerbatimSet()
  var identity1 = s._rawIdentifier()
  expectTrue(isCocoaSet(s))

  var startIndex = s.startIndex
  var endIndex = s.endIndex
  expectNotEqual(startIndex, endIndex)
  expectTrue(startIndex < endIndex)
  expectTrue(startIndex <= endIndex)
  expectFalse(startIndex >= endIndex)
  expectFalse(startIndex > endIndex)
  expectEqual(identity1, s._rawIdentifier())

  var members = [Int]()
  for i in s.indices {
    var foundMember: AnyObject = s[i]
    let member = foundMember as! TestObjCKeyTy
    members.append(member.value)
  }
  expectTrue(equalsUnordered(members, [1010, 2020, 3030]))
  expectEqual(identity1, s._rawIdentifier())

  // Keep indexes alive during the calls above.
  _fixLifetime(startIndex)
  _fixLifetime(endIndex)
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.SubscriptWithIndex") {
  var s = getBridgedNonverbatimSet()
  var identity1 = s._rawIdentifier()
  expectTrue(isNativeSet(s))

  var startIndex = s.startIndex
  var endIndex = s.endIndex
  expectNotEqual(startIndex, endIndex)
  expectTrue(startIndex < endIndex)
  expectTrue(startIndex <= endIndex)
  expectFalse(startIndex >= endIndex)
  expectFalse(startIndex > endIndex)
  expectEqual(identity1, s._rawIdentifier())

  var members = [Int]()
  for i in s.indices {
    var foundMember: AnyObject = s[i] as NSObject
    let member = foundMember as! TestObjCKeyTy
    members.append(member.value)
  }
  expectTrue(equalsUnordered(members, [1010, 2020, 3030]))
  expectEqual(identity1, s._rawIdentifier())

  // Keep indexes alive during the calls above.
  _fixLifetime(startIndex)
  _fixLifetime(endIndex)
}

SetTestSuite.test("BridgedFromObjC.Verbatim.SubscriptWithIndex_Empty") {
  var s = getBridgedVerbatimSet([])
  var identity1 = s._rawIdentifier()
  expectTrue(isCocoaSet(s))

  var startIndex = s.startIndex
  var endIndex = s.endIndex
  expectEqual(startIndex, endIndex)
  expectFalse(startIndex < endIndex)
  expectTrue(startIndex <= endIndex)
  expectTrue(startIndex >= endIndex)
  expectFalse(startIndex > endIndex)
  expectEqual(identity1, s._rawIdentifier())

  // Keep indexes alive during the calls above.
  _fixLifetime(startIndex)
  _fixLifetime(endIndex)
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.SubscriptWithIndex_Empty") {
  var s = getBridgedNonverbatimSet([])
  var identity1 = s._rawIdentifier()
  expectTrue(isNativeSet(s))

  var startIndex = s.startIndex
  var endIndex = s.endIndex
  expectEqual(startIndex, endIndex)
  expectFalse(startIndex < endIndex)
  expectTrue(startIndex <= endIndex)
  expectTrue(startIndex >= endIndex)
  expectFalse(startIndex > endIndex)
  expectEqual(identity1, s._rawIdentifier())

  // Keep indexes alive during the calls above.
  _fixLifetime(startIndex)
  _fixLifetime(endIndex)
}

SetTestSuite.test("BridgedFromObjC.Verbatim.Contains") {
  var s = getBridgedVerbatimSet()
  var identity1 = s._rawIdentifier()
  expectTrue(isCocoaSet(s))

  expectTrue(s.contains(TestObjCKeyTy(1010)))
  expectTrue(s.contains(TestObjCKeyTy(2020)))
  expectTrue(s.contains(TestObjCKeyTy(3030)))

  expectEqual(identity1, s._rawIdentifier())

  // Inserting an item should now create storage unique from the bridged set.
  s.insert(TestObjCKeyTy(4040))
  var identity2 = s._rawIdentifier()
  expectNotEqual(identity1, identity2)
  expectTrue(isNativeSet(s))
  expectEqual(4, s.count)

  // Verify items are still present.
  expectTrue(s.contains(TestObjCKeyTy(1010)))
  expectTrue(s.contains(TestObjCKeyTy(2020)))
  expectTrue(s.contains(TestObjCKeyTy(3030)))
  expectTrue(s.contains(TestObjCKeyTy(4040)))

  // Insert a redundant item to the set.
  // A copy should *not* occur here.
  s.insert(TestObjCKeyTy(1010))
  expectEqual(identity2, s._rawIdentifier())
  expectTrue(isNativeSet(s))
  expectEqual(4, s.count)

  // Again, verify items are still present.
  expectTrue(s.contains(TestObjCKeyTy(1010)))
  expectTrue(s.contains(TestObjCKeyTy(2020)))
  expectTrue(s.contains(TestObjCKeyTy(3030)))
  expectTrue(s.contains(TestObjCKeyTy(4040)))
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.Contains") {
  var s = getBridgedNonverbatimSet()
  var identity1 = s._rawIdentifier()
  expectTrue(isNativeSet(s))

  expectTrue(s.contains(TestBridgedKeyTy(1010)))
  expectTrue(s.contains(TestBridgedKeyTy(2020)))
  expectTrue(s.contains(TestBridgedKeyTy(3030)))

  expectEqual(identity1, s._rawIdentifier())

  // Inserting an item should now create storage unique from the bridged set.
  s.insert(TestBridgedKeyTy(4040))
  var identity2 = s._rawIdentifier()
  expectNotEqual(identity1, identity2)
  expectTrue(isNativeSet(s))
  expectEqual(4, s.count)

  // Verify items are still present.
  expectTrue(s.contains(TestBridgedKeyTy(1010)))
  expectTrue(s.contains(TestBridgedKeyTy(2020)))
  expectTrue(s.contains(TestBridgedKeyTy(3030)))
  expectTrue(s.contains(TestBridgedKeyTy(4040)))

  // Insert a redundant item to the set.
  // A copy should *not* occur here.
  s.insert(TestBridgedKeyTy(1010))
  expectEqual(identity2, s._rawIdentifier())
  expectTrue(isNativeSet(s))
  expectEqual(4, s.count)

  // Again, verify items are still present.
  expectTrue(s.contains(TestBridgedKeyTy(1010)))
  expectTrue(s.contains(TestBridgedKeyTy(2020)))
  expectTrue(s.contains(TestBridgedKeyTy(3030)))
  expectTrue(s.contains(TestBridgedKeyTy(4040)))
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.SubscriptWithMember") {
  var s = getBridgedNonverbatimSet()
  var identity1 = s._rawIdentifier()
  expectTrue(isNativeSet(s))

  expectTrue(s.contains(TestBridgedKeyTy(1010)))
  expectTrue(s.contains(TestBridgedKeyTy(2020)))
  expectTrue(s.contains(TestBridgedKeyTy(3030)))

  expectEqual(identity1, s._rawIdentifier())

  // Insert a new member.
  // This should trigger a copy.
  s.insert(TestBridgedKeyTy(4040))
  var identity2 = s._rawIdentifier()

  expectTrue(isNativeSet(s))
  expectEqual(4, s.count)

  // Verify all items in place.
  expectTrue(s.contains(TestBridgedKeyTy(1010)))
  expectTrue(s.contains(TestBridgedKeyTy(2020)))
  expectTrue(s.contains(TestBridgedKeyTy(3030)))
  expectTrue(s.contains(TestBridgedKeyTy(4040)))

  // Insert a redundant member.
  // This should *not* trigger a copy.
  s.insert(TestBridgedKeyTy(1010))
  expectEqual(identity2, s._rawIdentifier())
  expectTrue(isNativeSet(s))
  expectEqual(4, s.count)

  // Again, verify all items in place.
  expectTrue(s.contains(TestBridgedKeyTy(1010)))
  expectTrue(s.contains(TestBridgedKeyTy(2020)))
  expectTrue(s.contains(TestBridgedKeyTy(3030)))
  expectTrue(s.contains(TestBridgedKeyTy(4040)))
}

SetTestSuite.test("BridgedFromObjC.Verbatim.RemoveAt") {
  var s = getBridgedVerbatimSet()
  let identity1 = s._rawIdentifier()
  expectTrue(isCocoaSet(s))

  let foundIndex1 = s.index(of: TestObjCKeyTy(1010))!
  expectEqual(TestObjCKeyTy(1010), s[foundIndex1])
  expectEqual(identity1, s._rawIdentifier())

  let removedElement = s.remove(at: foundIndex1)
  expectNotEqual(identity1, s._rawIdentifier())
  expectTrue(isNativeSet(s))
  expectEqual(2, s.count)
  expectEqual(TestObjCKeyTy(1010), removedElement)
  expectEmpty(s.index(of: TestObjCKeyTy(1010)))
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.RemoveAt") {
  var s = getBridgedNonverbatimSet()
  let identity1 = s._rawIdentifier()
  expectTrue(isNativeSet(s))

  let foundIndex1 = s.index(of: TestBridgedKeyTy(1010))!
  expectEqual(1010, s[foundIndex1].value)
  expectEqual(identity1, s._rawIdentifier())

  let removedElement = s.remove(at: foundIndex1)
  expectEqual(identity1, s._rawIdentifier())
  expectTrue(isNativeSet(s))
  expectEqual(1010, removedElement.value)
  expectEqual(2, s.count)
  expectEmpty(s.index(of: TestBridgedKeyTy(1010)))
}

SetTestSuite.test("BridgedFromObjC.Verbatim.Remove") {
  do {
    var s = getBridgedVerbatimSet()
    var identity1 = s._rawIdentifier()
    expectTrue(isCocoaSet(s))

    var deleted: AnyObject? = s.remove(TestObjCKeyTy(0))
    expectEmpty(deleted)
    expectEqual(identity1, s._rawIdentifier())
    expectTrue(isCocoaSet(s))

    deleted = s.remove(TestObjCKeyTy(1010))
    expectEqual(1010, deleted!.value)
    var identity2 = s._rawIdentifier()
    expectNotEqual(identity1, identity2)
    expectTrue(isNativeSet(s))
    expectEqual(2, s.count)

    expectFalse(s.contains(TestObjCKeyTy(1010)))
    expectTrue(s.contains(TestObjCKeyTy(2020)))
    expectTrue(s.contains(TestObjCKeyTy(3030)))
    expectEqual(identity2, s._rawIdentifier())
  }

  do {
    var s1 = getBridgedVerbatimSet()
    var identity1 = s1._rawIdentifier()

    var s2 = s1
    expectTrue(isCocoaSet(s1))
    expectTrue(isCocoaSet(s2))

    var deleted: AnyObject? = s2.remove(TestObjCKeyTy(0))
    expectEmpty(deleted)
    expectEqual(identity1, s1._rawIdentifier())
    expectEqual(identity1, s2._rawIdentifier())
    expectTrue(isCocoaSet(s1))
    expectTrue(isCocoaSet(s2))

    deleted = s2.remove(TestObjCKeyTy(1010))
    expectEqual(1010, deleted!.value)
    var identity2 = s2._rawIdentifier()
    expectNotEqual(identity1, identity2)
    expectTrue(isCocoaSet(s1))
    expectTrue(isNativeSet(s2))
    expectEqual(2, s2.count)

    expectTrue(s1.contains(TestObjCKeyTy(1010)))
    expectTrue(s1.contains(TestObjCKeyTy(2020)))
    expectTrue(s1.contains(TestObjCKeyTy(3030)))
    expectEqual(identity1, s1._rawIdentifier())

    expectFalse(s2.contains(TestObjCKeyTy(1010)))
    expectTrue(s2.contains(TestObjCKeyTy(2020)))
    expectTrue(s2.contains(TestObjCKeyTy(3030)))

    expectEqual(identity2, s2._rawIdentifier())
  }
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.Remove") {
  do {
    var s = getBridgedNonverbatimSet()
    var identity1 = s._rawIdentifier()
    expectTrue(isNativeSet(s))

    // Trying to remove something not in the set should
    // leave it completely unchanged.
    var deleted = s.remove(TestBridgedKeyTy(0))
    expectEmpty(deleted)
    expectEqual(identity1, s._rawIdentifier())
    expectTrue(isNativeSet(s))

    // Now remove an item that is in the set. This should
    // not create a new set altogether, however.
    deleted = s.remove(TestBridgedKeyTy(1010))
    expectEqual(1010, deleted!.value)
    var identity2 = s._rawIdentifier()
    expectEqual(identity1, identity2)
    expectTrue(isNativeSet(s))
    expectEqual(2, s.count)

    // Double-check - the removed member should not be found.
    expectFalse(s.contains(TestBridgedKeyTy(1010)))

    // ... but others not removed should be found.
    expectTrue(s.contains(TestBridgedKeyTy(2020)))
    expectTrue(s.contains(TestBridgedKeyTy(3030)))

    // Triple-check - we should still be working with the same object.
    expectEqual(identity2, s._rawIdentifier())
  }

  do {
    var s1 = getBridgedNonverbatimSet()
    let identity1 = s1._rawIdentifier()

    var s2 = s1
    expectTrue(isNativeSet(s1))
    expectTrue(isNativeSet(s2))

    var deleted = s2.remove(TestBridgedKeyTy(0))
    expectEmpty(deleted)
    expectEqual(identity1, s1._rawIdentifier())
    expectEqual(identity1, s2._rawIdentifier())
    expectTrue(isNativeSet(s1))
    expectTrue(isNativeSet(s2))

    deleted = s2.remove(TestBridgedKeyTy(1010))
    expectEqual(1010, deleted!.value)
    let identity2 = s2._rawIdentifier()
    expectNotEqual(identity1, identity2)
    expectTrue(isNativeSet(s1))
    expectTrue(isNativeSet(s2))
    expectEqual(2, s2.count)

    expectTrue(s1.contains(TestBridgedKeyTy(1010)))
    expectTrue(s1.contains(TestBridgedKeyTy(2020)))
    expectTrue(s1.contains(TestBridgedKeyTy(3030)))
    expectEqual(identity1, s1._rawIdentifier())

    expectFalse(s2.contains(TestBridgedKeyTy(1010)))
    expectTrue(s2.contains(TestBridgedKeyTy(2020)))
    expectTrue(s2.contains(TestBridgedKeyTy(3030)))
    expectEqual(identity2, s2._rawIdentifier())
  }
}

SetTestSuite.test("BridgedFromObjC.Verbatim.RemoveAll") {
  do {
    var s = getBridgedVerbatimSet([])
    var identity1 = s._rawIdentifier()
    expectTrue(isCocoaSet(s))
    expectEqual(0, s.count)

    s.removeAll()
    expectEqual(identity1, s._rawIdentifier())
    expectEqual(0, s.count)
  }

  do {
    var s = getBridgedVerbatimSet()
    var identity1 = s._rawIdentifier()
    expectTrue(isCocoaSet(s))
    expectEqual(3, s.count)
    expectTrue(s.contains(TestBridgedKeyTy(1010) as NSObject))

    s.removeAll()
    expectEqual(0, s.count)
    expectFalse(s.contains(TestBridgedKeyTy(1010) as NSObject))
  }

  do {
    var s1 = getBridgedVerbatimSet()
    var identity1 = s1._rawIdentifier()
    expectTrue(isCocoaSet(s1))
    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestBridgedKeyTy(1010) as NSObject))

    var s2 = s1
    s2.removeAll()
    var identity2 = s2._rawIdentifier()
    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity2, identity1)
    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestBridgedKeyTy(1010) as NSObject))
    expectEqual(0, s2.count)
    expectFalse(s2.contains(TestBridgedKeyTy(1010) as NSObject))
  }

  do {
    var s1 = getBridgedVerbatimSet()
    var identity1 = s1._rawIdentifier()
    expectTrue(isCocoaSet(s1))
    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestBridgedKeyTy(1010) as NSObject))

    var s2 = s1
    s2.removeAll(keepingCapacity: true)
    var identity2 = s2._rawIdentifier()
    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity2, identity1)
    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestBridgedKeyTy(1010) as NSObject))
    expectEqual(0 , s2.count)
    expectFalse(s2.contains(TestBridgedKeyTy(1010) as NSObject))
  }
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.RemoveAll") {
  do {
    var s = getBridgedNonverbatimSet([])
    var identity1 = s._rawIdentifier()
    expectTrue(isNativeSet(s))
    expectEqual(0, s.count)

    s.removeAll()
    expectEqual(identity1, s._rawIdentifier())
    expectEqual(0, s.count)
  }

  do {
    var s = getBridgedNonverbatimSet()
    var identity1 = s._rawIdentifier()
    expectTrue(isNativeSet(s))
    expectEqual(3, s.count)
    expectTrue(s.contains(TestBridgedKeyTy(1010)))

    s.removeAll()
    expectEqual(0, s.count)
    expectFalse(s.contains(TestBridgedKeyTy(1010)))
  }

  do {
    var s1 = getBridgedNonverbatimSet()
    var identity1 = s1._rawIdentifier()
    expectTrue(isNativeSet(s1))
    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestBridgedKeyTy(1010)))

    var s2 = s1
    s2.removeAll()
    var identity2 = s2._rawIdentifier()
    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity2, identity1)
    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestBridgedKeyTy(1010)))
    expectEqual(0, s2.count)
    expectFalse(s2.contains(TestBridgedKeyTy(1010)))
  }

  do {
    var s1 = getBridgedNonverbatimSet()
    var identity1 = s1._rawIdentifier()
    expectTrue(isNativeSet(s1))
    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestBridgedKeyTy(1010)))

    var s2 = s1
    s2.removeAll(keepingCapacity: true)
    var identity2 = s2._rawIdentifier()
    expectEqual(identity1, s1._rawIdentifier())
    expectNotEqual(identity2, identity1)
    expectEqual(3, s1.count)
    expectTrue(s1.contains(TestObjCKeyTy(1010) as TestBridgedKeyTy))
    expectEqual(0, s2.count)
    expectFalse(s2.contains(TestObjCKeyTy(1010) as TestBridgedKeyTy))
  }
}

SetTestSuite.test("BridgedFromObjC.Verbatim.Count") {
  var s = getBridgedVerbatimSet()
  var identity1 = s._rawIdentifier()
  expectTrue(isCocoaSet(s))

  expectEqual(3, s.count)
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.Count") {
  var s = getBridgedNonverbatimSet()
  var identity1 = s._rawIdentifier()
  expectTrue(isNativeSet(s))

  expectEqual(3, s.count)
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Verbatim.Generate") {
  var s = getBridgedVerbatimSet()
  var identity1 = s._rawIdentifier()
  expectTrue(isCocoaSet(s))

  var iter = s.makeIterator()
  var members: [Int] = []
  while let member = iter.next() {
    members.append((member as! TestObjCKeyTy).value)
  }
  expectTrue(equalsUnordered(members, [1010, 2020, 3030]))
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  expectEmpty(iter.next())
  expectEmpty(iter.next())
  expectEmpty(iter.next())
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.Generate") {
  var s = getBridgedNonverbatimSet()
  var identity1 = s._rawIdentifier()
  expectTrue(isNativeSet(s))

  var iter = s.makeIterator()
  var members: [Int] = []
  while let member = iter.next() {
    members.append(member.value)
  }
  expectTrue(equalsUnordered(members, [1010, 2020, 3030]))
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  expectEmpty(iter.next())
  expectEmpty(iter.next())
  expectEmpty(iter.next())
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Verbatim.Generate_Empty") {
  var s = getBridgedVerbatimSet([])
  var identity1 = s._rawIdentifier()
  expectTrue(isCocoaSet(s))

  var iter = s.makeIterator()
  expectEmpty(iter.next())
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  expectEmpty(iter.next())
  expectEmpty(iter.next())
  expectEmpty(iter.next())
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.Generate_Empty") {
  var s = getBridgedNonverbatimSet([])
  var identity1 = s._rawIdentifier()
  expectTrue(isNativeSet(s))

  var iter = s.makeIterator()
  expectEmpty(iter.next())
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  expectEmpty(iter.next())
  expectEmpty(iter.next())
  expectEmpty(iter.next())
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Verbatim.Generate_Huge") {
  var s = getHugeBridgedVerbatimSet()
  var identity1 = s._rawIdentifier()
  expectTrue(isCocoaSet(s))

  var iter = s.makeIterator()
  var members = [Int]()
  while let member = iter.next() {
    members.append((member as! TestObjCKeyTy).value)
  }
  expectTrue(equalsUnordered(members, hugeNumberArray))
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  expectEmpty(iter.next())
  expectEmpty(iter.next())
  expectEmpty(iter.next())
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.Generate_Huge") {
  var s = getHugeBridgedNonverbatimSet()
  var identity1 = s._rawIdentifier()
  expectTrue(isNativeSet(s))

  var iter = s.makeIterator()
  var members = [Int]()
  while let member = iter.next() as AnyObject? {
    members.append((member as! TestBridgedKeyTy).value)
  }
  expectTrue(equalsUnordered(members, hugeNumberArray))
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  expectEmpty(iter.next())
  expectEmpty(iter.next())
  expectEmpty(iter.next())
  expectEqual(identity1, s._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Verbatim.EqualityTest_Empty") {
  var s1 = getBridgedVerbatimSet([])
  var identity1 = s1._rawIdentifier()
  expectTrue(isCocoaSet(s1))

  var s2 = getBridgedVerbatimSet([])
  var identity2 = s2._rawIdentifier()
  expectTrue(isCocoaSet(s2))
  expectEqual(identity1, identity2)

  expectEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(identity2, s2._rawIdentifier())

  s2.insert(TestObjCKeyTy(4040))
  expectTrue(isNativeSet(s2))
  expectNotEqual(identity2, s2._rawIdentifier())
  identity2 = s2._rawIdentifier()

  expectNotEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(identity2, s2._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.EqualityTest_Empty") {
  var s1 = getBridgedNonverbatimSet([])
  var identity1 = s1._rawIdentifier()
  expectTrue(isNativeSet(s1))

  var s2 = getBridgedNonverbatimSet([])
  var identity2 = s2._rawIdentifier()
  expectTrue(isNativeSet(s2))
  expectNotEqual(identity1, identity2)

  expectEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(identity2, s2._rawIdentifier())

  s2.insert(TestObjCKeyTy(4040) as TestBridgedKeyTy)
  expectTrue(isNativeSet(s2))
  expectEqual(identity2, s2._rawIdentifier())

  expectNotEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(identity2, s2._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Verbatim.EqualityTest_Small") {
  var s1 = getBridgedVerbatimSet()
  let identity1 = s1._rawIdentifier()
  expectTrue(isCocoaSet(s1))

  var s2 = getBridgedVerbatimSet()
  let identity2 = s2._rawIdentifier()
  expectTrue(isCocoaSet(s2))
  expectNotEqual(identity1, identity2)

  expectEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(identity2, s2._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.EqualityTest_Small") {
  var s1 = getBridgedNonverbatimSet()
  let identity1 = s1._rawIdentifier()
  expectTrue(isNativeSet(s1))

  var s2 = getBridgedNonverbatimSet()
  let identity2 = s2._rawIdentifier()
  expectTrue(isNativeSet(s2))
  expectNotEqual(identity1, identity2)

  expectEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(identity2, s2._rawIdentifier())
}

SetTestSuite.test("BridgedFromObjC.Verbatim.ArrayOfSets") {
  var nsa = NSMutableArray()
  for i in 0..<3 {
    nsa.add(
        getAsNSSet([1 + i,  2 + i, 3 + i]))
  }

  var a = nsa as [AnyObject] as! [Set<NSObject>]
  for i in 0..<3 {
    var s = a[i]
    var iter = s.makeIterator()
    var items: [Int] = []
    while let value = iter.next() {
      let v = (value as! TestObjCKeyTy).value
      items.append(v)
    }
    var expectedItems = [1 + i, 2 + i, 3 + i]
    expectTrue(equalsUnordered(items, expectedItems))
  }
}

SetTestSuite.test("BridgedFromObjC.Nonverbatim.ArrayOfSets") {
  var nsa = NSMutableArray()
  for i in 0..<3 {
    nsa.add(
        getAsNSSet([1 + i, 2 + i, 3 + i]))
  }

  var a = nsa as [AnyObject] as! [Set<TestBridgedKeyTy>]
  for i in 0..<3 {
    var d = a[i]
    var iter = d.makeIterator()
    var items: [Int] = []
    while let value = iter.next() {
      items.append(value.value)
    }
    var expectedItems = [1 + i, 2 + i, 3 + i]
    expectTrue(equalsUnordered(items, expectedItems))
  }
}


//===---
// Dictionary -> NSDictionary bridging tests.
//
// Value is bridged verbatim.
//===---

SetTestSuite.test("BridgedToObjC.Verbatim.Count") {
  let s = getBridgedNSSetOfRefTypesBridgedVerbatim()

  expectEqual(3, s.count)
}

SetTestSuite.test("BridgedToObjC.Verbatim.Contains") {
  let s = getBridgedNSSetOfRefTypesBridgedVerbatim()

  var v: AnyObject? = s.member(TestObjCKeyTy(1010)).map { $0 as AnyObject }
  expectEqual(1010, (v as! TestObjCKeyTy).value)
  let idValue10 = unsafeBitCast(v, to: UInt.self)

  v = s.member(TestObjCKeyTy(2020)).map { $0 as AnyObject }
  expectEqual(2020, (v as! TestObjCKeyTy).value)
  let idValue20 = unsafeBitCast(v, to: UInt.self)

  v = s.member(TestObjCKeyTy(3030)).map { $0 as AnyObject }
  expectEqual(3030, (v as! TestObjCKeyTy).value)
  let idValue30 = unsafeBitCast(v, to: UInt.self)

  expectEmpty(s.member(TestObjCKeyTy(4040)))

  // NSSet can store mixed key types.  Swift's Set is typed, but when bridged
  // to NSSet, it should behave like one, and allow queries for mismatched key
  // types.
  expectEmpty(s.member(TestObjCInvalidKeyTy()))

  for i in 0..<3 {
    expectEqual(idValue10,
      unsafeBitCast(s.member(TestObjCKeyTy(1010)).map { $0 as AnyObject }, to: UInt.self))

    expectEqual(idValue20,
      unsafeBitCast(s.member(TestObjCKeyTy(2020)).map { $0 as AnyObject }, to: UInt.self))

    expectEqual(idValue30,
      unsafeBitCast(s.member(TestObjCKeyTy(3030)).map { $0 as AnyObject }, to: UInt.self))
  }

  expectAutoreleasedKeysAndValues(unopt: (3, 0))
}

SetTestSuite.test("BridgingRoundtrip") {
  let s = getRoundtripBridgedNSSet()
  let enumerator = s.objectEnumerator()

  var items: [Int] = []
  while let value = enumerator.nextObject() {
    let v = (value as! TestObjCKeyTy).value
    items.append(v)
  }
  expectTrue(equalsUnordered([1010, 2020, 3030], items))
}

SetTestSuite.test("BridgedToObjC.Verbatim.ObjectEnumerator.FastEnumeration.UseFromSwift") {
  let s = getBridgedNSSetOfRefTypesBridgedVerbatim()

  checkSetFastEnumerationFromSwift(
    [ 1010, 2020, 3030 ],
    s, { s.objectEnumerator() },
    { ($0 as! TestObjCKeyTy).value })

  expectAutoreleasedKeysAndValues(unopt: (3, 0))
}

SetTestSuite.test("BridgedToObjC.Verbatim.ObjectEnumerator.FastEnumeration.UseFromObjC") {
  let s = getBridgedNSSetOfRefTypesBridgedVerbatim()

  checkSetFastEnumerationFromObjC(
    [ 1010, 2020, 3030 ],
    s, { s.objectEnumerator() },
    { ($0 as! TestObjCKeyTy).value })

  expectAutoreleasedKeysAndValues(unopt: (3, 0))
}

SetTestSuite.test("BridgedToObjC.Verbatim.ObjectEnumerator.FastEnumeration_Empty") {
  let s = getBridgedEmptyNSSet()

  checkSetFastEnumerationFromSwift(
    [], s, { s.objectEnumerator() },
    { ($0 as! TestObjCKeyTy).value })

  checkSetFastEnumerationFromObjC(
    [], s, { s.objectEnumerator() },
    { ($0 as! TestObjCKeyTy).value })
}

SetTestSuite.test("BridgedToObjC.Custom.ObjectEnumerator.FastEnumeration.UseFromObjC") {
  let s = getBridgedNSSet_MemberTypesCustomBridged()

  checkSetFastEnumerationFromObjC(
    [ 1010, 2020, 3030 ],
    s, { s.objectEnumerator() },
    { ($0 as! TestObjCKeyTy).value })

  expectAutoreleasedKeysAndValues(unopt: (3, 0))
}

SetTestSuite.test("BridgedToObjC.Custom.ObjectEnumerator.FastEnumeration.UseFromSwift") {
  let s = getBridgedNSSet_MemberTypesCustomBridged()

  checkSetFastEnumerationFromSwift(
    [ 1010, 2020, 3030 ],
    s, { s.objectEnumerator() },
    { ($0 as! TestObjCKeyTy).value })

  expectAutoreleasedKeysAndValues(unopt: (3, 0))
}

SetTestSuite.test("BridgedToObjC.Verbatim.FastEnumeration.UseFromSwift") {
  let s = getBridgedNSSetOfRefTypesBridgedVerbatim()

  checkSetFastEnumerationFromSwift(
    [ 1010, 2020, 3030 ],
    s, { s },
    { ($0 as! TestObjCKeyTy).value })
}

SetTestSuite.test("BridgedToObjC.Verbatim.FastEnumeration.UseFromObjC") {
  let s = getBridgedNSSetOfRefTypesBridgedVerbatim()

  checkSetFastEnumerationFromObjC(
    [ 1010, 2020, 3030 ],
    s, { s },
    { ($0 as! TestObjCKeyTy).value })
}

SetTestSuite.test("BridgedToObjC.Verbatim.FastEnumeration_Empty") {
  let s = getBridgedEmptyNSSet()

  checkSetFastEnumerationFromSwift(
    [], s, { s },
    { ($0 as! TestObjCKeyTy).value })

  checkSetFastEnumerationFromObjC(
    [], s, { s },
    { ($0 as! TestObjCKeyTy).value })
}

SetTestSuite.test("BridgedToObjC.Custom.FastEnumeration.UseFromSwift") {
  let s = getBridgedNSSet_ValueTypesCustomBridged()

  checkSetFastEnumerationFromSwift(
    [ 1010, 2020, 3030 ],
    s, { s },
    { ($0 as! TestObjCKeyTy).value })
}

SetTestSuite.test("BridgedToObjC.Custom.FastEnumeration.UseFromObjC") {
  let s = getBridgedNSSet_ValueTypesCustomBridged()

  checkSetFastEnumerationFromObjC(
    [ 1010, 2020, 3030 ],
    s, { s },
    { ($0 as! TestObjCKeyTy).value })
}

SetTestSuite.test("BridgedToObjC.Custom.FastEnumeration_Empty") {
  let s = getBridgedNSSet_ValueTypesCustomBridged(
    numElements: 0)

  checkSetFastEnumerationFromSwift(
    [], s, { s },
    { ($0 as! TestObjCKeyTy).value })

  checkSetFastEnumerationFromObjC(
    [], s, { s },
    { ($0 as! TestObjCKeyTy).value })
}

SetTestSuite.test("BridgedToObjC.Count") {
  let s = getBridgedNSSetOfRefTypesBridgedVerbatim()
  expectEqual(3, s.count)
}

SetTestSuite.test("BridgedToObjC.ObjectEnumerator.NextObject") {
  let s = getBridgedNSSetOfRefTypesBridgedVerbatim()
  let enumerator = s.objectEnumerator()

  var members = [Int]()
  while let nextObject = enumerator.nextObject() {
    members.append((nextObject as! TestObjCKeyTy).value)
  }
  expectTrue(equalsUnordered([1010, 2020, 3030], members))

  expectEmpty(enumerator.nextObject())
  expectEmpty(enumerator.nextObject())
  expectEmpty(enumerator.nextObject())

  expectAutoreleasedKeysAndValues(unopt: (3, 0))
}

SetTestSuite.test("BridgedToObjC.ObjectEnumerator.NextObject.Empty") {
  let s = getBridgedEmptyNSSet()
  let enumerator = s.objectEnumerator()

  expectEmpty(enumerator.nextObject())
  expectEmpty(enumerator.nextObject())
  expectEmpty(enumerator.nextObject())
}

//
// Set -> NSSet Bridging
//

SetTestSuite.test("BridgedToObjC.MemberTypesCustomBridged") {
  let s = getBridgedNSSet_MemberTypesCustomBridged()
  let enumerator = s.objectEnumerator()

  var members = [Int]()
  while let nextObject = enumerator.nextObject() {
    members.append((nextObject as! TestObjCKeyTy).value)
  }
  expectTrue(equalsUnordered([1010, 2020, 3030], members))

  expectAutoreleasedKeysAndValues(unopt: (3, 0))
}

//
// NSSet -> Set -> NSSet Round trip bridging
//

SetTestSuite.test("BridgingRoundTrip") {
  let s = getRoundtripBridgedNSSet()
  let enumerator = s.objectEnumerator()

  var members = [Int]()
  while let nextObject = enumerator.nextObject() {
    members.append((nextObject as! TestObjCKeyTy).value)
  }
  expectTrue(equalsUnordered([1010, 2020, 3030] ,members))
}

//
// NSSet -> Set implicit conversion
//

SetTestSuite.test("NSSetToSetConversion") {
  let nsArray = NSMutableArray()
  for i in [1010, 2020, 3030] {
    nsArray.add(TestObjCKeyTy(i))
  }

  let nss = NSSet(array: nsArray as [AnyObject])

  let s = nss as Set

  var members = [Int]()
  for member: AnyObject in s {
    members.append((member as! TestObjCKeyTy).value)
  }
  expectTrue(equalsUnordered(members, [1010, 2020, 3030]))
}

SetTestSuite.test("SetToNSSetConversion") {
  var s = Set<TestObjCKeyTy>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
    s.insert(TestObjCKeyTy(i))
  }
  let nss: NSSet = s as NSSet

  expectTrue(equalsUnordered(Array(s).map { $0.value }, [1010, 2020, 3030]))
}

//
// Set Casts
//

SetTestSuite.test("SetUpcastEntryPoint") {
  var s = Set<TestObjCKeyTy>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
      s.insert(TestObjCKeyTy(i))
  }

  var sAsAnyObject: Set<NSObject> = _setUpCast(s)

  expectEqual(3, sAsAnyObject.count)
  expectTrue(sAsAnyObject.contains(TestObjCKeyTy(1010)))
  expectTrue(sAsAnyObject.contains(TestObjCKeyTy(2020)))
  expectTrue(sAsAnyObject.contains(TestObjCKeyTy(3030)))
}

SetTestSuite.test("SetUpcast") {
  var s = Set<TestObjCKeyTy>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
      s.insert(TestObjCKeyTy(i))
  }

  var sAsAnyObject: Set<NSObject> = s

  expectEqual(3, sAsAnyObject.count)
  expectTrue(sAsAnyObject.contains(TestObjCKeyTy(1010)))
  expectTrue(sAsAnyObject.contains(TestObjCKeyTy(2020)))
  expectTrue(sAsAnyObject.contains(TestObjCKeyTy(3030)))
}

SetTestSuite.test("SetUpcastBridgedEntryPoint") {
  var s = Set<TestBridgedKeyTy>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
      s.insert(TestBridgedKeyTy(i))
  }

  do {
    var s: Set<NSObject> = _setBridgeToObjectiveC(s)

    expectTrue(s.contains(TestBridgedKeyTy(1010) as NSObject))
    expectTrue(s.contains(TestBridgedKeyTy(2020) as NSObject))
    expectTrue(s.contains(TestBridgedKeyTy(3030) as NSObject))
  }

  do {
    var s: Set<TestObjCKeyTy> = _setBridgeToObjectiveC(s)

    expectEqual(3, s.count)
    expectTrue(s.contains(TestBridgedKeyTy(1010) as TestObjCKeyTy))
    expectTrue(s.contains(TestBridgedKeyTy(2020) as TestObjCKeyTy))
    expectTrue(s.contains(TestBridgedKeyTy(3030) as TestObjCKeyTy))
  }
}

SetTestSuite.test("SetUpcastBridged") {
  var s = Set<TestBridgedKeyTy>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
      s.insert(TestBridgedKeyTy(i))
  }

  do {
    var s = s as Set<NSObject>

    expectEqual(3, s.count)
    expectTrue(s.contains(TestBridgedKeyTy(1010) as NSObject))
    expectTrue(s.contains(TestBridgedKeyTy(2020) as NSObject))
    expectTrue(s.contains(TestBridgedKeyTy(3030) as NSObject))
  }

  do {
    var s = s as Set<TestObjCKeyTy>

    expectEqual(3, s.count)
    expectTrue(s.contains(TestBridgedKeyTy(1010) as TestObjCKeyTy))
    expectTrue(s.contains(TestBridgedKeyTy(2020) as TestObjCKeyTy))
    expectTrue(s.contains(TestBridgedKeyTy(3030) as TestObjCKeyTy))
  }
}

//
// Set downcasts
//

SetTestSuite.test("SetDowncastEntryPoint") {
  var s = Set<NSObject>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
      s.insert(TestObjCKeyTy(i))
  }

  // Successful downcast.
  let sCC: Set<TestObjCKeyTy> = _setDownCast(s)
  expectEqual(3, sCC.count)
  expectTrue(sCC.contains(TestObjCKeyTy(1010)))
  expectTrue(sCC.contains(TestObjCKeyTy(2020)))
  expectTrue(sCC.contains(TestObjCKeyTy(3030)))

  expectAutoreleasedKeysAndValues(unopt: (3, 0))
}

SetTestSuite.test("SetDowncast") {
  var s = Set<NSObject>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
      s.insert(TestObjCKeyTy(i))
  }

  // Successful downcast.
  let sCC = s as! Set<TestObjCKeyTy>
  expectEqual(3, sCC.count)
  expectTrue(sCC.contains(TestObjCKeyTy(1010)))
  expectTrue(sCC.contains(TestObjCKeyTy(2020)))
  expectTrue(sCC.contains(TestObjCKeyTy(3030)))

  expectAutoreleasedKeysAndValues(unopt: (3, 0))
}

SetTestSuite.test("SetDowncastConditionalEntryPoint") {
  var s = Set<NSObject>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
      s.insert(TestObjCKeyTy(i))
  }

  // Successful downcast.
  if let sCC  = _setDownCastConditional(s) as Set<TestObjCKeyTy>? {
    expectEqual(3, sCC.count)
    expectTrue(sCC.contains(TestObjCKeyTy(1010)))
    expectTrue(sCC.contains(TestObjCKeyTy(2020)))
    expectTrue(sCC.contains(TestObjCKeyTy(3030)))
  } else {
    expectTrue(false)
  }

  // Unsuccessful downcast
  s.insert("Hello, world" as NSString)
  if let sCC = _setDownCastConditional(s) as Set<TestObjCKeyTy>? {
    expectTrue(false)
  }
}

SetTestSuite.test("SetDowncastConditional") {
  var s = Set<NSObject>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
      s.insert(TestObjCKeyTy(i))
  }

  // Successful downcast.
  if let sCC = s as? Set<TestObjCKeyTy> {
    expectEqual(3, sCC.count)
    expectTrue(sCC.contains(TestObjCKeyTy(1010)))
    expectTrue(sCC.contains(TestObjCKeyTy(2020)))
    expectTrue(sCC.contains(TestObjCKeyTy(3030)))
  } else {
    expectTrue(false)
  }

  // Unsuccessful downcast
  s.insert("Hello, world, I'm your wild girl. I'm your ch-ch-ch-ch-ch-ch cherry bomb" as NSString)
  if let sCC = s as? Set<TestObjCKeyTy> {
    expectTrue(false)
  }
}

SetTestSuite.test("SetBridgeFromObjectiveCEntryPoint") {
  var s = Set<NSObject>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
      s.insert(TestObjCKeyTy(i))
  }

  // Successful downcast.
  let sCV: Set<TestBridgedKeyTy> = _setBridgeFromObjectiveC(s)
  do {
    expectEqual(3, sCV.count)
    expectTrue(sCV.contains(TestBridgedKeyTy(1010)))
    expectTrue(sCV.contains(TestBridgedKeyTy(2020)))
    expectTrue(sCV.contains(TestBridgedKeyTy(3030)))
  }
}

SetTestSuite.test("SetBridgeFromObjectiveC") {
  var s = Set<NSObject>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
      s.insert(TestObjCKeyTy(i))
  }

  // Successful downcast.
  let sCV = s as! Set<TestObjCKeyTy>
  do {
    expectEqual(3, sCV.count)
    expectTrue(sCV.contains(TestObjCKeyTy(1010)))
    expectTrue(sCV.contains(TestObjCKeyTy(2020)))
    expectTrue(sCV.contains(TestObjCKeyTy(3030)))
  }

  // Successful downcast.
  let sVC = s as! Set<TestBridgedKeyTy>
  do {
    expectEqual(3, sVC.count)
    expectTrue(sVC.contains(TestBridgedKeyTy(1010)))
    expectTrue(sVC.contains(TestBridgedKeyTy(2020)))
    expectTrue(sVC.contains(TestBridgedKeyTy(3030)))
  }

  expectAutoreleasedKeysAndValues(unopt: (3, 0))
}

SetTestSuite.test("SetBridgeFromObjectiveCConditionalEntryPoint") {
  var s = Set<NSObject>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
      s.insert(TestObjCKeyTy(i))
  }

  // Successful downcast.
  if let sVC = _setBridgeFromObjectiveCConditional(s) as Set<TestBridgedKeyTy>? {
    expectEqual(3, sVC.count)
    expectTrue(sVC.contains(TestBridgedKeyTy(1010)))
    expectTrue(sVC.contains(TestBridgedKeyTy(2020)))
    expectTrue(sVC.contains(TestBridgedKeyTy(3030)))
  } else {
    expectTrue(false)
  }

  // Unsuccessful downcasts
  s.insert("Hello, world, I'm your wild girl. I'm your ch-ch-ch-ch-ch-ch cherry bomb" as NSString)
  if let sVC = _setBridgeFromObjectiveCConditional(s) as Set<TestBridgedKeyTy>? {
    expectTrue(false)
  }
}

SetTestSuite.test("SetBridgeFromObjectiveCConditional") {
  var s = Set<NSObject>(minimumCapacity: 32)
  for i in [1010, 2020, 3030] {
      s.insert(TestObjCKeyTy(i))
  }

  // Successful downcast.
  if let sCV = s as? Set<TestObjCKeyTy> {
    expectEqual(3, sCV.count)
    expectTrue(sCV.contains(TestObjCKeyTy(1010)))
    expectTrue(sCV.contains(TestObjCKeyTy(2020)))
    expectTrue(sCV.contains(TestObjCKeyTy(3030)))
  } else {
    expectTrue(false)
  }

  // Successful downcast.
  if let sVC = s as? Set<TestBridgedKeyTy> {
    expectEqual(3, sVC.count)
    expectTrue(sVC.contains(TestBridgedKeyTy(1010)))
    expectTrue(sVC.contains(TestBridgedKeyTy(2020)))
    expectTrue(sVC.contains(TestBridgedKeyTy(3030)))
  } else {
    expectTrue(false)
  }

  // Unsuccessful downcasts
  s.insert("Hello, world, I'm your wild girl. I'm your ch-ch-ch-ch-ch-ch cherry bomb" as NSString)
  if let sCm = s as? Set<TestObjCKeyTy> {
    expectTrue(false)
  }
  if let sVC = s as? Set<TestBridgedKeyTy> {
    expectTrue(false)
  }
  if let sVm = s as? Set<TestBridgedKeyTy> {
    expectTrue(false)
  }
}
#endif // _runtime(_ObjC)

// Public API

SetTestSuite.test("init(Sequence:)") {
  let s1 = Set([1010, 2020, 3030])
  var s2 = Set<Int>()
  s2.insert(1010)
  s2.insert(2020)
  s2.insert(3030)
  expectEqual(s1, s2)

  // Test the uniquing capabilities of a set
  let s3 = Set([
    1010, 1010, 1010, 1010, 1010, 1010,
    1010, 1010, 1010, 1010, 1010, 1010,
    2020, 2020, 2020, 3030, 3030, 3030
  ])
  expectEqual(s1, s3)
}

SetTestSuite.test("init(arrayLiteral:)") {
  let s1: Set<Int> = [1010, 2020, 3030, 1010, 2020, 3030]
  let s2 = Set([1010, 2020, 3030])
  var s3 = Set<Int>()
  s3.insert(1010)
  s3.insert(2020)
  s3.insert(3030)
  expectEqual(s1, s2)
  expectEqual(s2, s3)
}

SetTestSuite.test("isSubsetOf.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  expectTrue(Set<Int>().isSubset(of: s1))
  expectFalse(s1.isSubset(of: Set<Int>()))
  expectTrue(s1.isSubset(of: s1))
  expectTrue(s2.isSubset(of: s1))
}

SetTestSuite.test("⊆.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  expectTrue(Set<Int>().isSubset(of: s1))
  expectFalse(s1 ⊆ Set<Int>())
  expectTrue(s1 ⊆ s1)
  expectTrue(s2 ⊆ s1)
}

SetTestSuite.test("⊈.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  expectFalse(Set<Int>() ⊈ s1)
  expectTrue(s1 ⊈ Set<Int>())
  expectFalse(s1 ⊈ s1)
  expectFalse(s2 ⊈ s1)
}

SetTestSuite.test("isSubsetOf.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  expectTrue(Set<Int>().isSubset(of: s1))
  expectFalse(s1.isSubset(of: Set<Int>()))
  expectTrue(s1.isSubset(of: s1))
}

SetTestSuite.test("⊆.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  expectTrue(Set<Int>().isSubset(of: s1))
  expectFalse(s1 ⊆ Set<Int>())
  expectTrue(s1 ⊆ s1)
}

SetTestSuite.test("⊈.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  expectFalse(Set<Int>() ⊈ s1)
  expectTrue(s1 ⊈ Set<Int>())
  expectFalse(s1 ⊈ s1)
}

SetTestSuite.test("isStrictSubsetOf.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  expectTrue(Set<Int>().isStrictSubset(of: s1))
  expectFalse(s1.isStrictSubset(of: Set<Int>()))
  expectFalse(s1.isStrictSubset(of: s1))
}

SetTestSuite.test("⊂.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  expectTrue(Set<Int>() ⊂ s1)
  expectFalse(s1 ⊂ Set<Int>())
  expectFalse(s1 ⊂ s1)
}

SetTestSuite.test("⊄.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  expectFalse(Set<Int>() ⊄ s1)
  expectTrue(s1 ⊄ Set<Int>())
  expectTrue(s1 ⊄ s1)
}

SetTestSuite.test("isStrictSubsetOf.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  expectTrue(Set<Int>().isStrictSubset(of: s1))
  expectFalse(s1.isStrictSubset(of: Set<Int>()))
  expectFalse(s1.isStrictSubset(of: s1))
}

SetTestSuite.test("⊂.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  expectTrue(Set<Int>() ⊂ s1)
  expectFalse(s1 ⊂ Set<Int>())
  expectFalse(s1 ⊂ s1)
}

SetTestSuite.test("⊄.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  expectFalse(Set<Int>() ⊄ s1)
  expectTrue(s1 ⊄ Set<Int>())
  expectTrue(s1 ⊄ s1)
}

SetTestSuite.test("isSupersetOf.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  expectTrue(s1.isSuperset(of: Set<Int>()))
  expectFalse(Set<Int>().isSuperset(of: s1))
  expectTrue(s1.isSuperset(of: s1))
  expectTrue(s1.isSuperset(of: s2))
  expectFalse(Set<Int>().isSuperset(of: s1))
}

SetTestSuite.test("⊇.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  expectTrue(s1 ⊇ Set<Int>())
  expectFalse(Set<Int>() ⊇ s1)
  expectTrue(s1 ⊇ s1)
  expectTrue(s1 ⊇ s2)
  expectFalse(Set<Int>() ⊇ s1)
}

SetTestSuite.test("⊉.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  expectFalse(s1 ⊉ Set<Int>())
  expectTrue(Set<Int>() ⊉ s1)
  expectFalse(s1 ⊉ s1)
  expectFalse(s1 ⊉ s2)
  expectTrue(Set<Int>() ⊉ s1)
}

SetTestSuite.test("isSupersetOf.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  expectTrue(s1.isSuperset(of: Set<Int>()))
  expectFalse(Set<Int>().isSuperset(of: s1))
  expectTrue(s1.isSuperset(of: s1))
  expectTrue(s1.isSuperset(of: s2))
  expectFalse(Set<Int>().isSuperset(of: s1))
}

SetTestSuite.test("⊇.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  expectTrue(s1 ⊇ Set<Int>())
  expectFalse(Set<Int>() ⊇ s1)
  expectTrue(s1 ⊇ s1)
  expectTrue(s1 ⊇ s2)
  expectFalse(Set<Int>() ⊇ s1)
}

SetTestSuite.test("⊉.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  expectFalse(s1 ⊉ Set<Int>())
  expectTrue(Set<Int>() ⊉ s1)
  expectFalse(s1 ⊉ s1)
  expectFalse(s1 ⊉ s2)
  expectTrue(Set<Int>() ⊉ s1)
}

SetTestSuite.test("strictSuperset.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  expectTrue(s1.isStrictSuperset(of: Set<Int>()))
  expectFalse(Set<Int>().isStrictSuperset(of: s1))
  expectFalse(s1.isStrictSuperset(of: s1))
  expectTrue(s1.isStrictSuperset(of: s2))
}

SetTestSuite.test("⊃.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  expectTrue(s1 ⊃ Set<Int>())
  expectFalse(Set<Int>() ⊃ s1)
  expectFalse(s1 ⊃ s1)
  expectTrue(s1 ⊃ s2)
}

SetTestSuite.test("⊅.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  expectFalse(s1 ⊅ Set<Int>())
  expectTrue(Set<Int>() ⊅ s1)
  expectTrue(s1 ⊅ s1)
  expectFalse(s1 ⊅ s2)
}

SetTestSuite.test("strictSuperset.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  expectTrue(s1.isStrictSuperset(of: Set<Int>()))
  expectFalse(Set<Int>().isStrictSuperset(of: s1))
  expectFalse(s1.isStrictSuperset(of: s1))
  expectTrue(s1.isStrictSuperset(of: s2))
}

SetTestSuite.test("⊃.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  expectTrue(s1 ⊃ Set<Int>())
  expectFalse(Set<Int>() ⊃ s1)
  expectFalse(s1 ⊃ s1)
  expectTrue(s1 ⊃ s2)
}

SetTestSuite.test("⊅.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  expectFalse(s1 ⊅ Set<Int>())
  expectTrue(Set<Int>() ⊅ s1)
  expectTrue(s1 ⊅ s1)
  expectFalse(s1 ⊅ s2)
}

SetTestSuite.test("Equatable.Native.Native") {
  let s1 = getCOWFastSet()
  let s2 = getCOWFastSet([1010, 2020, 3030, 4040, 5050, 6060])

  checkEquatable(true, s1, s1)
  checkEquatable(false, s1, Set<Int>())
  checkEquatable(true, Set<Int>(), Set<Int>())
  checkEquatable(false, s1, s2)
}

#if _runtime(_ObjC)
SetTestSuite.test("Equatable.Native.BridgedVerbatim") {
  let s1 = getNativeBridgedVerbatimSet()
  let bvs1 = getBridgedVerbatimSet()
  let bvs2 = getBridgedVerbatimSet([1010, 2020, 3030, 4040, 5050, 6060])
  let bvsEmpty = getBridgedVerbatimSet([])

  checkEquatable(true, s1, bvs1)
  checkEquatable(false, s1, bvs2)
  checkEquatable(false, s1, bvsEmpty)
}

SetTestSuite.test("Equatable.BridgedVerbatim.BridgedVerbatim") {
  let bvs1 = getBridgedVerbatimSet()
  let bvs2 = getBridgedVerbatimSet([1010, 2020, 3030, 4040, 5050, 6060])
  let bvsEmpty = getBridgedVerbatimSet([])

  checkEquatable(true, bvs1, bvs1)
  checkEquatable(false, bvs1, bvs2)
  checkEquatable(false, bvs1, bvsEmpty)
}

SetTestSuite.test("Equatable.BridgedNonverbatim.BridgedNonverbatim") {
  let bnvs1 = getBridgedNonverbatimSet()
  let bnvs2 = getBridgedNonverbatimSet([1010, 2020, 3030, 4040, 5050, 6060])
  let bnvsEmpty = getBridgedNonverbatimSet([])

  checkEquatable(true, bnvs1, bnvs1)
  checkEquatable(false, bnvs1, bnvs2)
  checkEquatable(false, bnvs1, bnvsEmpty)
  checkEquatable(false, bnvs2, bnvsEmpty)
}
#endif // _runtime(_ObjC)

SetTestSuite.test("isDisjointWith.Set.Set") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  let s3 = Set([7070, 8080, 9090])
  expectTrue(s1.isDisjoint(with: s3))
  expectFalse(s1.isDisjoint(with: s2))
  expectTrue(Set<Int>().isDisjoint(with: s1))
  expectTrue(Set<Int>().isDisjoint(with: Set<Int>()))
}

SetTestSuite.test("isDisjointWith.Set.Sequence") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = AnySequence([1010, 2020, 3030])
  let s3 = AnySequence([7070, 8080, 9090])
  expectTrue(s1.isDisjoint(with: s3))
  expectFalse(s1.isDisjoint(with: s2))
  expectTrue(Set<Int>().isDisjoint(with: s1))
  expectTrue(Set<Int>().isDisjoint(with: Set<Int>()))
}

SetTestSuite.test("insert") {
  // These are anagrams - they should amount to the same sets.
  var s1 = Set<TestKeyTy>([1010, 2020, 3030])

  let fortyForty: TestKeyTy = 4040
  do {
    // Inserting an element that isn't present
    let (inserted, currentMember) = s1.insert(fortyForty)
    expectTrue(inserted)
    expectTrue(currentMember === fortyForty)
  }
  
  do {
    // Inserting an element that is already present
    let (inserted, currentMember) = s1.insert(4040)
    expectFalse(inserted)
    expectTrue(currentMember === fortyForty)
  }
  
  expectEqual(4, s1.count)
  expectTrue(s1.contains(1010))
  expectTrue(s1.contains(2020))
  expectTrue(s1.contains(3030))
  expectTrue(s1.contains(4040))
}

SetTestSuite.test("replace") {
  // These are anagrams - they should amount to the same sets.
  var s1 = Set<TestKeyTy>([1010, 2020, 3030])

  let fortyForty: TestKeyTy = 4040
  do {
    // Replacing an element that isn't present
    let oldMember = s1.update(with: fortyForty)
    expectEmpty(oldMember)
  }
  
  do {
    // Replacing an element that is already present
    let oldMember = s1.update(with: 4040)
    expectTrue(oldMember === fortyForty)
  }
  
  expectEqual(4, s1.count)
  expectTrue(s1.contains(1010))
  expectTrue(s1.contains(2020))
  expectTrue(s1.contains(3030))
  expectTrue(s1.contains(4040))
}

SetTestSuite.test("union") {
  let s1 = Set([1010, 2020, 3030])
  let s2 = Set([4040, 5050, 6060])
  let s3 = Set([1010, 2020, 3030, 4040, 5050, 6060])

  let identity1 = s1._rawIdentifier()

  let s4 = s1.union(s2)
  expectEqual(s4, s3)

  // s1 should be unchanged
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(Set([1010, 2020, 3030]), s1)

  // s4 should be a fresh set
  expectNotEqual(identity1, s4._rawIdentifier())
  expectEqual(s4, s3)

  let s5 = s1.union(s1)
  expectEqual(s5, s1)
  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(s1, s1.union(Set<Int>()))
  expectEqual(s1, Set<Int>().union(s1))
}

SetTestSuite.test("∪") {
  let s1 = Set([1010, 2020, 3030])
  let s2 = Set([4040, 5050, 6060])
  let s3 = Set([1010, 2020, 3030, 4040, 5050, 6060])

  let identity1 = s1._rawIdentifier()

  let s4 = s1 ∪ s2
  expectEqual(s4, s3)

  // s1 should be unchanged
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(Set([1010, 2020, 3030]), s1)

  // s4 should be a fresh set
  expectNotEqual(identity1, s4._rawIdentifier())
  expectEqual(s4, s3)

  let s5 = s1 ∪ s1
  expectEqual(s5, s1)
  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(s1, s1 ∪ Set<Int>())
  expectEqual(s1, Set<Int>() ∪ s1)
}

SetTestSuite.test("formUnion") {
  // These are anagrams - they should amount to the same sets.
  var s1 = Set("the morse code".characters)
  let s2 = Set("here come dots".characters)
  let s3 = Set("and then dashes".characters)

  let identity1 = s1._rawIdentifier()

  s1.formUnion("".characters)
  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(s1, s2)
  s1.formUnion(s2)
  expectEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())

  s1.formUnion(s3)
  expectNotEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())
}

SetTestSuite.test("∪=") {
  // These are anagrams - they should amount to the same sets.
  var s1 = Set("the morse code".characters)
  let s2 = Set("here come dots".characters)
  let s3 = Set("and then dashes".characters)

  let identity1 = s1._rawIdentifier()

  s1 ∪= "".characters
  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(s1, s2)
  s1 ∪= s2
  expectEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())

  s1 ∪= s3
  expectNotEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())
}

SetTestSuite.test("subtract") {
  let s1 = Set([1010, 2020, 3030])
  let s2 = Set([4040, 5050, 6060])
  let s3 = Set([1010, 2020, 3030, 4040, 5050, 6060])

  let identity1 = s1._rawIdentifier()

  // Subtracting a disjoint set should not create a
  // unique reference
  let s4 = s1.subtracting(s2)
  expectEqual(s1, s4)
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(identity1, s4._rawIdentifier())

  // Subtracting a superset will leave the set empty
  let s5 = s1.subtracting(s3)
  expectTrue(s5.isEmpty)
  expectEqual(identity1, s1._rawIdentifier())
  expectNotEqual(identity1, s5._rawIdentifier())

  // Subtracting the empty set does nothing
  expectEqual(s1, s1.subtracting(Set<Int>()))
  expectEqual(Set<Int>(), Set<Int>().subtracting(s1))
  expectEqual(identity1, s1._rawIdentifier())
}

SetTestSuite.test("∖") {
  let s1 = Set([1010, 2020, 3030])
  let s2 = Set([4040, 5050, 6060])
  let s3 = Set([1010, 2020, 3030, 4040, 5050, 6060])

  let identity1 = s1._rawIdentifier()

  // Subtracting a disjoint set should not create a
  // unique reference
  let s4 = s1 ∖ s2
  expectEqual(s1, s4)
  expectEqual(identity1, s1._rawIdentifier())
  expectEqual(identity1, s4._rawIdentifier())

  // Subtracting a superset will leave the set empty
  let s5 = s1 ∖ s3
  expectTrue(s5.isEmpty)
  expectEqual(identity1, s1._rawIdentifier())
  expectNotEqual(identity1, s5._rawIdentifier())

  // Subtracting the empty set does nothing
  expectEqual(s1, s1 ∖ Set<Int>())
  expectEqual(Set<Int>(), Set<Int>() ∖ s1)
  expectEqual(identity1, s1._rawIdentifier())
}

SetTestSuite.test("subtract") {
  var s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  let s3 = Set([4040, 5050, 6060])

  let identity1 = s1._rawIdentifier()

  s1.subtract(Set<Int>())
  expectEqual(identity1, s1._rawIdentifier())

  s1.subtract(s3)
  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())
}

SetTestSuite.test("∖=") {
  var s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010, 2020, 3030])
  let s3 = Set([4040, 5050, 6060])

  let identity1 = s1._rawIdentifier()

  s1 ∖= Set<Int>()
  expectEqual(identity1, s1._rawIdentifier())

  s1 ∖= s3
  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(s1, s2)
  expectEqual(identity1, s1._rawIdentifier())
}

SetTestSuite.test("intersect") {
  let s1 = Set([1010, 2020, 3030])
  let s2 = Set([4040, 5050, 6060])
  var s3 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  var s4 = Set([1010, 2020, 3030])

  let identity1 = s1._rawIdentifier()
  expectEqual(Set([1010, 2020, 3030]),
    Set([1010, 2020, 3030]).intersection(Set([1010, 2020, 3030])) as Set<Int>)
  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(s1, s1.intersection(s3))
  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(Set<Int>(), Set<Int>().intersection(Set<Int>()))
  expectEqual(Set<Int>(), s1.intersection(Set<Int>()))
  expectEqual(Set<Int>(), Set<Int>().intersection(s1))
}

SetTestSuite.test("∩") {
  let s1 = Set([1010, 2020, 3030])
  let s2 = Set([4040, 5050, 6060])
  var s3 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  var s4 = Set([1010, 2020, 3030])

  let identity1 = s1._rawIdentifier()
  expectEqual(Set([1010, 2020, 3030]),
    Set([1010, 2020, 3030]) ∩ Set([1010, 2020, 3030]) as Set<Int>)
  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(s1, s1 ∩ s3)
  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(Set<Int>(), Set<Int>() ∩ Set<Int>())
  expectEqual(Set<Int>(), s1 ∩ Set<Int>())
  expectEqual(Set<Int>(), Set<Int>() ∩ s1)
}

SetTestSuite.test("formIntersection") {
  var s1 = Set([1010, 2020, 3030])
  let s2 = Set([4040, 5050, 6060])
  var s3 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  var s4 = Set([1010, 2020, 3030])

  let identity1 = s1._rawIdentifier()
  s1.formIntersection(s4)
  expectEqual(s1, s4)
  expectEqual(identity1, s1._rawIdentifier())

  s4.formIntersection(s2)
  expectEqual(Set<Int>(), s4)

  let identity2 = s3._rawIdentifier()
  s3.formIntersection(s2)
  expectEqual(s3, s2)
  expectTrue(s1.isDisjoint(with: s3))
  expectNotEqual(identity1, s3._rawIdentifier())

  var s5 = Set<Int>()
  s5.formIntersection(s5)
  expectEqual(s5, Set<Int>())
  s5.formIntersection(s1)
  expectEqual(s5, Set<Int>())
}

SetTestSuite.test("∩=") {
  var s1 = Set([1010, 2020, 3030])
  let s2 = Set([4040, 5050, 6060])
  var s3 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  var s4 = Set([1010, 2020, 3030])

  let identity1 = s1._rawIdentifier()
  s1 ∩= s4
  expectEqual(s1, s4)
  expectEqual(identity1, s1._rawIdentifier())

  s4 ∩= s2
  expectEqual(Set<Int>(), s4)

  let identity2 = s3._rawIdentifier()
  s3 ∩= s2
  expectEqual(s3, s2)
  expectTrue(s1.isDisjoint(with: s3))
  expectNotEqual(identity1, s3._rawIdentifier())

  var s5 = Set<Int>()
  s5 ∩= s5
  expectEqual(s5, Set<Int>())
  s5 ∩= s1
  expectEqual(s5, Set<Int>())
}

SetTestSuite.test("symmetricDifference") {

  // Overlap with 4040, 5050, 6060
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([4040, 5050, 6060, 7070, 8080, 9090])
  let result = Set([1010, 2020, 3030, 7070, 8080, 9090])
  let universe = Set([1010, 2020, 3030, 4040, 5050, 6060,
                       7070, 8080, 9090])

  let identity1 = s1._rawIdentifier()

  let s3 = s1.symmetricDifference(s2)

  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(s3, result)

  expectEqual(s1.symmetricDifference(s2),
    s1.union(s2).intersection(universe.subtracting(s1.intersection(s2))))

  expectEqual(s1.symmetricDifference(s2),
    s1.intersection(universe.subtracting(s2)).union(universe.subtracting(s1).intersection(s2)))

  expectTrue(s1.symmetricDifference(s1).isEmpty)
}

SetTestSuite.test("⨁") {

  // Overlap with 4040, 5050, 6060
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([4040, 5050, 6060, 7070, 8080, 9090])
  let result = Set([1010, 2020, 3030, 7070, 8080, 9090])
  let universe = Set([1010, 2020, 3030, 4040, 5050, 6060,
                       7070, 8080, 9090])

  let identity1 = s1._rawIdentifier()

  let s3 = s1 ⨁ s2

  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(s3, result)

  expectEqual(s1 ⨁ s2,
    (s1 ∪ s2) ∩ (universe ∖ (s1 ∩ s2)))

  expectEqual(s1 ⨁ s2,
    s1 ∩ (universe ∖ s2) ∪ (universe ∖ s1) ∩ s2)

  expectTrue((s1 ⨁ s1).isEmpty)
}

SetTestSuite.test("formSymmetricDifference") {
  // Overlap with 4040, 5050, 6060
  var s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010])
  let result = Set([2020, 3030, 4040, 5050, 6060])

  // s1 ⨁ s2 == result
  let identity1 = s1._rawIdentifier()
  s1.formSymmetricDifference(s2)

  // Removing just one element shouldn't cause an identity change
  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(s1, result)

  // A ⨁ A == {}
  s1.formSymmetricDifference(s1)
  expectTrue(s1.isEmpty)

  // Removing all elements should cause an identity change
  expectNotEqual(identity1, s1._rawIdentifier())
}

SetTestSuite.test("⨁=") {
  // Overlap with 4040, 5050, 6060
  var s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s2 = Set([1010])
  let result = Set([2020, 3030, 4040, 5050, 6060])

  let identity1 = s1._rawIdentifier()
  s1 ⨁= s2

  // Removing just one element shouldn't cause an identity change
  expectEqual(identity1, s1._rawIdentifier())

  expectEqual(s1, result)

  s1 ⨁= s1
  expectTrue(s1.isEmpty)

  // Removing all elements should cause an identity change
  expectNotEqual(identity1, s1._rawIdentifier())
}

SetTestSuite.test("removeFirst") {
  var s1 = Set([1010, 2020, 3030])
  let s2 = s1
  let empty = Set<Int>()

  let a1 = s1.removeFirst()

  expectFalse(s1.contains(a1))
  expectTrue(s2.contains(a1))
  expectNotEqual(s1._rawIdentifier(), s2._rawIdentifier())
  expectTrue(s1.isSubset(of: s2))
  expectEmpty(empty.first)
}

SetTestSuite.test("remove(member)") {
  let s1 : Set<TestKeyTy> = [1010, 2020, 3030]
  var s2 = Set<TestKeyTy>(minimumCapacity: 10)
  for i in [1010, 2020, 3030] {
    s2.insert(TestKeyTy(i))
  }
  let identity1 = s2._rawIdentifier()
  
  // remove something that's not there.
  let fortyForty = s2.remove(4040)
  expectEqual(s2, s1)
  expectEmpty(fortyForty)
  expectEqual(identity1, s2._rawIdentifier())

  // Remove things that are there.
  let thirtyThirty = s2.remove(3030)
  expectEqual(3030, thirtyThirty)
  expectEqual(identity1, s2._rawIdentifier())

  s2.remove(2020)
  expectEqual(identity1, s2._rawIdentifier())

  s2.remove(1010)
  expectEqual(identity1, s2._rawIdentifier())
  expectEqual(Set(), s2)
  expectTrue(s2.isEmpty)
}

SetTestSuite.test("contains") {
  let s1 = Set([1010, 2020, 3030])
  expectTrue(s1.contains(1010))
  expectFalse(s1.contains(999))
}

SetTestSuite.test("∈") {
  let s1 = Set([1010, 2020, 3030])
  expectTrue(1010  ∈ s1)
  expectFalse(999 ∈ s1)
}

SetTestSuite.test("∉") {
  let s1 = Set([1010, 2020, 3030])
  expectFalse(1010 ∉ s1)
  expectTrue(999 ∉ s1)
}

SetTestSuite.test("memberAtIndex") {
  let s1 = Set([1010, 2020, 3030])

  let foundIndex = s1.index(of: 1010)!
  expectEqual(1010, s1[foundIndex])
}

SetTestSuite.test("first") {
  let s1 = Set([1010, 2020, 3030])
  let emptySet = Set<Int>()

  expectTrue(s1.contains(s1.first!))
  expectEmpty(emptySet.first)
}

SetTestSuite.test("isEmpty") {
  let s1 = Set([1010, 2020, 3030])
  expectFalse(s1.isEmpty)

  let emptySet = Set<Int>()
  expectTrue(emptySet.isEmpty)
}

#if _runtime(_ObjC)
@objc
class MockSetWithCustomCount : NSSet {
  init(count: Int) {
    self._count = count
    super.init()
  }

  override init() {
    expectUnreachable()
    super.init()
  }

  override init(objects: UnsafePointer<AnyObject>, count: Int) {
    expectUnreachable()
    super.init(objects: objects, count: count)
  }

  required init(coder aDecoder: NSCoder) {
    fatalError("init(coder:) not implemented by MockSetWithCustomCount")
  }

  @objc(copyWithZone:)
  override func copy(with zone: NSZone?) -> Any {
    // Ensure that copying this set produces an object of the same
    // dynamic type.
    return self
  }

  override func member(_ object: Any) -> Any? {
    expectUnreachable()
    return object
  }

  override func objectEnumerator() -> NSEnumerator {
    expectUnreachable()
    return getAsNSSet([1010, 1020, 1030]).objectEnumerator()
  }

  override var count: Int {
    MockSetWithCustomCount.timesCountWasCalled += 1
    return _count
  }

  var _count: Int = 0

  static var timesCountWasCalled = 0
}

func getMockSetWithCustomCount(count: Int)
  -> Set<NSObject> {

  return MockSetWithCustomCount(count: count) as Set
}

func callGenericIsEmpty<C : Collection>(_ collection: C) -> Bool {
  return collection.isEmpty
}

SetTestSuite.test("isEmpty/ImplementationIsCustomized") {
  do {
    var d = getMockSetWithCustomCount(count: 0)
    MockSetWithCustomCount.timesCountWasCalled = 0
    expectTrue(d.isEmpty)
    expectEqual(1, MockSetWithCustomCount.timesCountWasCalled)
  }
  do {
    var d = getMockSetWithCustomCount(count: 0)
    MockSetWithCustomCount.timesCountWasCalled = 0
    expectTrue(callGenericIsEmpty(d))
    expectEqual(1, MockSetWithCustomCount.timesCountWasCalled)
  }

  do {
    var d = getMockSetWithCustomCount(count: 4)
    MockSetWithCustomCount.timesCountWasCalled = 0
    expectFalse(d.isEmpty)
    expectEqual(1, MockSetWithCustomCount.timesCountWasCalled)
  }
  do {
    var d = getMockSetWithCustomCount(count: 4)
    MockSetWithCustomCount.timesCountWasCalled = 0
    expectFalse(callGenericIsEmpty(d))
    expectEqual(1, MockSetWithCustomCount.timesCountWasCalled)
  }
}
#endif // _runtime(_ObjC)

SetTestSuite.test("count") {
  let s1 = Set([1010, 2020, 3030])
  var s2 = Set([4040, 5050, 6060])
  expectEqual(0, Set<Int>().count)
  expectEqual(3, s1.count)
}

SetTestSuite.test("contains") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  expectTrue(s1.contains(1010))
  expectFalse(s1.contains(999))
  expectFalse(Set<Int>().contains(1010))
}

SetTestSuite.test("_customContainsEquatableElement") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  expectTrue(s1._customContainsEquatableElement(1010)!)
  expectFalse(s1._customContainsEquatableElement(999)!)
  expectFalse(Set<Int>()._customContainsEquatableElement(1010)!)
}

SetTestSuite.test("index(of:)") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let foundIndex1 = s1.index(of: 1010)!
  expectEqual(1010, s1[foundIndex1])

  expectEmpty(s1.index(of: 999))
}

SetTestSuite.test("popFirst") {
  // Empty
  do {
    var s = Set<Int>()
    let popped = s.popFirst()
    expectEmpty(popped)
    expectTrue(s.isEmpty)
  }

  do {
    var popped = [Int]()
    var s = Set([1010, 2020, 3030])
    let expected = Array(s)
    while let element = s.popFirst() {
      popped.append(element)
    }
    expectEqualSequence(expected, Array(popped))
    expectTrue(s.isEmpty)
  }
}

SetTestSuite.test("removeAt") {
  // Test removing from the startIndex, the middle, and the end of a set.
  for i in 1...3 {
    var s = Set<Int>([1010, 2020, 3030])
    let removed = s.remove(at: s.index(of: i*1010)!)
    expectEqual(i*1010, removed)
    expectEqual(2, s.count)
    expectEmpty(s.index(of: i*1010))
    let origKeys: [Int] = [1010, 2020, 3030]
    expectEqual(origKeys.filter { $0 != (i*1010) }, [Int](s).sorted())
  }
}

SetTestSuite.test("_customIndexOfEquatableElement") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let foundIndex1 = s1._customIndexOfEquatableElement(1010)!!
  expectEqual(1010, s1[foundIndex1])

  expectEmpty(s1._customIndexOfEquatableElement(999)!)
}

SetTestSuite.test("commutative") {
  let s1 = Set([1010, 2020, 3030])
  let s2 = Set([2020, 3030])
  expectTrue(equalsUnordered(s1.intersection(s2), s2.intersection(s1)))
  expectTrue(equalsUnordered(s1.union(s2), s2.union(s1)))
}

SetTestSuite.test("associative") {
  let s1 = Set([1010, 2020, 3030])
  let s2 = Set([2020, 3030])
  let s3 = Set([1010, 2020, 3030])
  let s4 = Set([2020, 3030])
  let s5 = Set([7070, 8080, 9090])

  expectTrue(equalsUnordered(s1.intersection(s2).intersection(s3),
    s1.intersection(s2.intersection(s3))))
  expectTrue(equalsUnordered(s3.union(s4).union(s5), s3.union(s4.union(s5))))
}

SetTestSuite.test("distributive") {
  let s1 = Set([1010])
  let s2 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  let s3 = Set([2020, 3030])
  expectTrue(equalsUnordered(s1.union(s2.intersection(s3)),
    s1.union(s2).intersection(s1.union(s3))))

  let s4 = Set([2020, 3030])
  expectTrue(equalsUnordered(s4.intersection(s1.union(s3)),
    s4.intersection(s1).union(s4.intersection(s3))))
}

SetTestSuite.test("idempotent") {
  let s1 = Set([1010, 2020, 3030, 4040, 5050, 6060])
  expectTrue(equalsUnordered(s1, s1.intersection(s1)))
  expectTrue(equalsUnordered(s1, s1.union(s1)))
}

SetTestSuite.test("absorption") {
  let s1 = Set([1010, 2020, 3030])
  let s2 = Set([4040, 5050, 6060])
  let s3 = Set([2020, 3030])
  expectTrue(equalsUnordered(s1, s1.union(s1.intersection(s2))))
  expectTrue(equalsUnordered(s1, s1.intersection(s1.union(s3))))
}

SetTestSuite.test("misc") {
  // Set with other types
  do {
    var s = Set([1.1, 2.2, 3.3])
    s.insert(4.4)
    expectTrue(s.contains(1.1))
    expectTrue(s.contains(2.2))
    expectTrue(s.contains(3.3))
  }

  do {
    var s = Set(["Hello", "world"])
    expectTrue(s.contains("Hello"))
    expectTrue(s.contains("world"))
  }
}

SetTestSuite.test("Hashable") {
  let s1 = Set([1010])
  let s2 = Set([2020])
  checkHashable(s1 == s2, s1, s2)

  // Explicit types help the type checker quite a bit.
  let ss1 = Set([Set([1010] as [Int]), Set([2020] as [Int]), Set([3030] as [Int])])
  let ss11 = Set([Set([2020] as [Int]), Set([3030] as [Int]), Set([2020] as [Int])])
  let ss2 = Set([Set([9090] as [Int])])
  checkHashable(ss1 == ss11, ss1, ss11)
  checkHashable(ss1 == ss2, ss1, ss2)
}

SetTestSuite.test("Operator.Precedence") {
  let s1 = Set([1010, 2020, 3030])
  let s2 = Set([3030, 4040, 5050])
  let s3 = Set([6060, 7070, 8080])
  let s4 = Set([8080, 9090, 100100])

  // intersection higher precedence than union
  expectEqual(s1 ∪ (s2 ∩ s3) ∪ s4, s1 ∪ s2 ∩ s3 ∪ s4 as Set<Int>)

  // intersection higher precedence than complement
  expectEqual(s1 ∖ (s2 ∩ s3) ∖ s4, s1 ∖ s2 ∩ s3 ∖ s4 as Set<Int>)

  // intersection higher precedence than exclusive-or
  expectEqual(s1 ⨁ (s2 ∩ s3) ⨁ s4, s1 ⨁ s2 ∩ s3 ⨁ s4 as Set<Int>)

  // union/complement/exclusive-or same precedence
  expectEqual((((s1 ∪ s3) ∖ s2) ⨁ s4), s1 ∪ s3 ∖ s2 ⨁ s4 as Set<Int>)

  // ∪= should happen last.
  var s5 = Set([1010, 2020, 3030])
  s5 ∪= Set([4040, 5050, 6060]) ∪ [7070]
  expectEqual(Set([1010, 2020, 3030, 4040, 5050, 6060, 7070]), s5)

  // ∩= should happen last.
  var s6 = Set([1010, 2020, 3030])
  s6 ∩= Set([1010, 2020, 3030]) ∩ [3030]
  expectEqual(Set([3030]), s6)

  // ⨁= should happen last.
  var s7 = Set([1010, 2020, 3030])
  s7 ⨁= Set([1010, 2020, 3030]) ⨁ [1010, 3030]
  expectEqual(Set([1010, 3030]), s7)

  // ∖= should happen last.
  var s8 = Set([1010, 2020, 3030])
  s8 ∖= Set([2020, 3030]) ∖ [3030]
  expectEqual(Set([1010, 3030]), s8)
}

//===---
// Check that iterators traverse a snapshot of the collection.
//===---

SetTestSuite.test("mutationDoesNotAffectIterator/remove,1") {
  var set = Set([1010, 1020, 1030])
  var iter = set.makeIterator()
  expectOptionalEqual(1010, set.remove(1010))

  expectEqualsUnordered([1010, 1020, 1030], Array(IteratorSequence(iter)))
}

SetTestSuite.test("mutationDoesNotAffectIterator/remove,all") {
  var set = Set([1010, 1020, 1030])
  var iter = set.makeIterator()
  expectOptionalEqual(1010, set.remove(1010))
  expectOptionalEqual(1020, set.remove(1020))
  expectOptionalEqual(1030, set.remove(1030))

  expectEqualsUnordered([1010, 1020, 1030], Array(IteratorSequence(iter)))
}

SetTestSuite.test("mutationDoesNotAffectIterator/removeAll,keepingCapacity=false") {
  var set = Set([1010, 1020, 1030])
  var iter = set.makeIterator()
  set.removeAll(keepingCapacity: false)

  expectEqualsUnordered([1010, 1020, 1030], Array(IteratorSequence(iter)))
}

SetTestSuite.test("mutationDoesNotAffectIterator/removeAll,keepingCapacity=true") {
  var set = Set([1010, 1020, 1030])
  var iter = set.makeIterator()
  set.removeAll(keepingCapacity: true)

  expectEqualsUnordered([1010, 1020, 1030], Array(IteratorSequence(iter)))
}

runAllTests()
