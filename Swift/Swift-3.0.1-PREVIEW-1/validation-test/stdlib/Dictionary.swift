// RUN: rm -rf %t
// RUN: mkdir -p %t
//
// RUN: %gyb %s -o %t/main.swift
// RUN: if [ %target-runtime == "objc" ]; then \
// RUN:   %target-clang -fobjc-arc %S/Inputs/SlurpFastEnumeration/SlurpFastEnumeration.m -c -o %t/SlurpFastEnumeration.o; \
// RUN:   %line-directive %t/main.swift -- %target-build-swift %S/Inputs/DictionaryKeyValueTypes.swift %S/Inputs/DictionaryKeyValueTypesObjC.swift %t/main.swift -I %S/Inputs/SlurpFastEnumeration/ -Xlinker %t/SlurpFastEnumeration.o -o %t/Dictionary -Xfrontend -disable-access-control; \
// RUN: else \
// RUN:   %line-directive %t/main.swift -- %target-build-swift %S/Inputs/DictionaryKeyValueTypes.swift %t/main.swift -o %t/Dictionary -Xfrontend -disable-access-control; \
// RUN: fi
//
// RUN: %line-directive %t/main.swift -- %target-run %t/Dictionary
// REQUIRES: executable_test

#if os(OSX) || os(iOS) || os(tvOS) || os(watchOS)
import Darwin
#else
import Glibc
#endif

import StdlibUnittest
import StdlibCollectionUnittest


#if _runtime(_ObjC)
import Foundation
import StdlibUnittestFoundationExtras
#endif

extension Dictionary {
  func _rawIdentifier() -> Int {
    return unsafeBitCast(self, to: Int.self)
  }
}

// Check that the generic parameters are called 'Key' and 'Value'.
protocol TestProtocol1 {}

extension Dictionary where Key : TestProtocol1, Value : TestProtocol1 {
  var _keyValueAreTestProtocol1: Bool {
    fatalError("not implemented")
  }
}

extension DictionaryIndex where Key : TestProtocol1, Value : TestProtocol1 {
  var _keyValueAreTestProtocol1: Bool {
    fatalError("not implemented")
  }
}

extension DictionaryIterator
  where Key : TestProtocol1, Value : TestProtocol1 {

  var _keyValueAreTestProtocol1: Bool {
    fatalError("not implemented")
  }
}

var DictionaryTestSuite = TestSuite("Dictionary")

DictionaryTestSuite.test("AssociatedTypes") {
  typealias Collection = Dictionary<MinimalHashableValue, OpaqueValue<Int>>
  expectCollectionAssociatedTypes(
    collectionType: Collection.self,
    iteratorType: DictionaryIterator<MinimalHashableValue, OpaqueValue<Int>>.self,
    subSequenceType: Slice<Collection>.self,
    indexType: DictionaryIndex<MinimalHashableValue, OpaqueValue<Int>>.self,
    indexDistanceType: Int.self,
    indicesType: DefaultIndices<Collection>.self)
}

DictionaryTestSuite.test("sizeof") {
  var dict = [1: "meow", 2: "meow"]
#if arch(i386) || arch(arm)
  expectEqual(4, MemoryLayout.size(ofValue: dict))
#else
  expectEqual(8, MemoryLayout.size(ofValue: dict))
#endif
}

DictionaryTestSuite.test("valueDestruction") {
  var d1 = Dictionary<Int, TestValueTy>()
  for i in 100...110 {
    d1[i] = TestValueTy(i)
  }

  var d2 = Dictionary<TestKeyTy, TestValueTy>()
  for i in 100...110 {
    d2[TestKeyTy(i)] = TestValueTy(i)
  }
}

DictionaryTestSuite.test("COW.Smoke") {
  var d1 = Dictionary<TestKeyTy, TestValueTy>(minimumCapacity: 10)
  var identity1 = d1._rawIdentifier()

  d1[TestKeyTy(10)] = TestValueTy(1010)
  d1[TestKeyTy(20)] = TestValueTy(1020)
  d1[TestKeyTy(30)] = TestValueTy(1030)

  var d2 = d1
  _fixLifetime(d2)
  assert(identity1 == d2._rawIdentifier())

  d2[TestKeyTy(40)] = TestValueTy(2040)
  assert(identity1 != d2._rawIdentifier())

  d1[TestKeyTy(50)] = TestValueTy(1050)
  assert(identity1 == d1._rawIdentifier())

  // Keep variables alive.
  _fixLifetime(d1)
  _fixLifetime(d2)
}

func getCOWFastDictionary() -> Dictionary<Int, Int> {
  var d = Dictionary<Int, Int>(minimumCapacity: 10)
  d[10] = 1010
  d[20] = 1020
  d[30] = 1030
  return d
}

func getCOWSlowDictionary() -> Dictionary<TestKeyTy, TestValueTy> {
  var d = Dictionary<TestKeyTy, TestValueTy>(minimumCapacity: 10)
  d[TestKeyTy(10)] = TestValueTy(1010)
  d[TestKeyTy(20)] = TestValueTy(1020)
  d[TestKeyTy(30)] = TestValueTy(1030)
  return d
}

func getCOWSlowEquatableDictionary()
    -> Dictionary<TestKeyTy, TestEquatableValueTy> {
  var d = Dictionary<TestKeyTy, TestEquatableValueTy>(minimumCapacity: 10)
  d[TestKeyTy(10)] = TestEquatableValueTy(1010)
  d[TestKeyTy(20)] = TestEquatableValueTy(1020)
  d[TestKeyTy(30)] = TestEquatableValueTy(1030)
  return d
}


DictionaryTestSuite.test("COW.Fast.IndexesDontAffectUniquenessCheck") {
  var d = getCOWFastDictionary()
  var identity1 = d._rawIdentifier()

  var startIndex = d.startIndex
  var endIndex = d.endIndex
  assert(startIndex != endIndex)
  assert(startIndex < endIndex)
  assert(startIndex <= endIndex)
  assert(!(startIndex >= endIndex))
  assert(!(startIndex > endIndex))

  assert(identity1 == d._rawIdentifier())

  d[40] = 2040
  assert(identity1 == d._rawIdentifier())

  // Keep indexes alive during the calls above.
  _fixLifetime(startIndex)
  _fixLifetime(endIndex)
}

DictionaryTestSuite.test("COW.Slow.IndexesDontAffectUniquenessCheck") {
  var d = getCOWSlowDictionary()
  var identity1 = d._rawIdentifier()

  var startIndex = d.startIndex
  var endIndex = d.endIndex
  assert(startIndex != endIndex)
  assert(startIndex < endIndex)
  assert(startIndex <= endIndex)
  assert(!(startIndex >= endIndex))
  assert(!(startIndex > endIndex))
  assert(identity1 == d._rawIdentifier())

  d[TestKeyTy(40)] = TestValueTy(2040)
  assert(identity1 == d._rawIdentifier())

  // Keep indexes alive during the calls above.
  _fixLifetime(startIndex)
  _fixLifetime(endIndex)
}


DictionaryTestSuite.test("COW.Fast.SubscriptWithIndexDoesNotReallocate") {
  var d = getCOWFastDictionary()
  var identity1 = d._rawIdentifier()

  var startIndex = d.startIndex
  let empty = startIndex == d.endIndex
  assert((d.startIndex < d.endIndex) == !empty)
  assert(d.startIndex <= d.endIndex)
  assert((d.startIndex >= d.endIndex) == empty)
  assert(!(d.startIndex > d.endIndex))
  assert(identity1 == d._rawIdentifier())

  assert(d[startIndex].1 != 0)
  assert(identity1 == d._rawIdentifier())
}

DictionaryTestSuite.test("COW.Slow.SubscriptWithIndexDoesNotReallocate") {
  var d = getCOWSlowDictionary()
  var identity1 = d._rawIdentifier()

  var startIndex = d.startIndex
  let empty = startIndex == d.endIndex
  assert((d.startIndex < d.endIndex) == !empty)
  assert(d.startIndex <= d.endIndex)
  assert((d.startIndex >= d.endIndex) == empty)
  assert(!(d.startIndex > d.endIndex))
  assert(identity1 == d._rawIdentifier())

  assert(d[startIndex].1.value != 0)
  assert(identity1 == d._rawIdentifier())
}


DictionaryTestSuite.test("COW.Fast.SubscriptWithKeyDoesNotReallocate") {
  var d = getCOWFastDictionary()
  var identity1 = d._rawIdentifier()

  assert(d[10]! == 1010)
  assert(identity1 == d._rawIdentifier())

  // Insert a new key-value pair.
  d[40] = 2040
  assert(identity1 == d._rawIdentifier())
  assert(d.count == 4)
  assert(d[10]! == 1010)
  assert(d[20]! == 1020)
  assert(d[30]! == 1030)
  assert(d[40]! == 2040)

  // Overwrite a value in existing binding.
  d[10] = 2010
  assert(identity1 == d._rawIdentifier())
  assert(d.count == 4)
  assert(d[10]! == 2010)
  assert(d[20]! == 1020)
  assert(d[30]! == 1030)
  assert(d[40]! == 2040)

  // Delete an existing key.
  d[10] = nil
  assert(identity1 == d._rawIdentifier())
  assert(d.count == 3)
  assert(d[20]! == 1020)
  assert(d[30]! == 1030)
  assert(d[40]! == 2040)

  // Try to delete a key that does not exist.
  d[42] = nil
  assert(identity1 == d._rawIdentifier())
  assert(d.count == 3)
  assert(d[20]! == 1020)
  assert(d[30]! == 1030)
  assert(d[40]! == 2040)

  do {
    var d2: [MinimalHashableValue : OpaqueValue<Int>] = [:]
    MinimalHashableValue.timesEqualEqualWasCalled = 0
    MinimalHashableValue.timesHashValueWasCalled = 0
    expectEmpty(d2[MinimalHashableValue(42)])

    // If the dictionary is empty, we shouldn't be computing the hash value of
    // the provided key.
    expectEqual(0, MinimalHashableValue.timesEqualEqualWasCalled)
    expectEqual(0, MinimalHashableValue.timesHashValueWasCalled)
  }
}

DictionaryTestSuite.test("COW.Slow.SubscriptWithKeyDoesNotReallocate") {
  var d = getCOWSlowDictionary()
  var identity1 = d._rawIdentifier()

  assert(d[TestKeyTy(10)]!.value == 1010)
  assert(identity1 == d._rawIdentifier())

  // Insert a new key-value pair.
  d[TestKeyTy(40)] = TestValueTy(2040)
  assert(identity1 == d._rawIdentifier())
  assert(d.count == 4)
  assert(d[TestKeyTy(10)]!.value == 1010)
  assert(d[TestKeyTy(20)]!.value == 1020)
  assert(d[TestKeyTy(30)]!.value == 1030)
  assert(d[TestKeyTy(40)]!.value == 2040)

  // Overwrite a value in existing binding.
  d[TestKeyTy(10)] = TestValueTy(2010)
  assert(identity1 == d._rawIdentifier())
  assert(d.count == 4)
  assert(d[TestKeyTy(10)]!.value == 2010)
  assert(d[TestKeyTy(20)]!.value == 1020)
  assert(d[TestKeyTy(30)]!.value == 1030)
  assert(d[TestKeyTy(40)]!.value == 2040)

  // Delete an existing key.
  d[TestKeyTy(10)] = nil
  assert(identity1 == d._rawIdentifier())
  assert(d.count == 3)
  assert(d[TestKeyTy(20)]!.value == 1020)
  assert(d[TestKeyTy(30)]!.value == 1030)
  assert(d[TestKeyTy(40)]!.value == 2040)

  // Try to delete a key that does not exist.
  d[TestKeyTy(42)] = nil
  assert(identity1 == d._rawIdentifier())
  assert(d.count == 3)
  assert(d[TestKeyTy(20)]!.value == 1020)
  assert(d[TestKeyTy(30)]!.value == 1030)
  assert(d[TestKeyTy(40)]!.value == 2040)

  do {
    var d2: [MinimalHashableClass : OpaqueValue<Int>] = [:]
    MinimalHashableClass.timesEqualEqualWasCalled = 0
    MinimalHashableClass.timesHashValueWasCalled = 0

    expectEmpty(d2[MinimalHashableClass(42)])

    // If the dictionary is empty, we shouldn't be computing the hash value of
    // the provided key.
    expectEqual(0, MinimalHashableClass.timesEqualEqualWasCalled)
    expectEqual(0, MinimalHashableClass.timesHashValueWasCalled)
  }
}


DictionaryTestSuite.test("COW.Fast.UpdateValueForKeyDoesNotReallocate") {
  do {
    var d1 = getCOWFastDictionary()
    var identity1 = d1._rawIdentifier()

    // Insert a new key-value pair.
    assert(d1.updateValue(2040, forKey: 40) == .none)
    assert(identity1 == d1._rawIdentifier())
    assert(d1[40]! == 2040)

    // Overwrite a value in existing binding.
    assert(d1.updateValue(2010, forKey: 10)! == 1010)
    assert(identity1 == d1._rawIdentifier())
    assert(d1[10]! == 2010)
  }

  do {
    var d1 = getCOWFastDictionary()
    var identity1 = d1._rawIdentifier()

    var d2 = d1
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 == d2._rawIdentifier())

    // Insert a new key-value pair.
    d2.updateValue(2040, forKey: 40)
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 != d2._rawIdentifier())

    assert(d1.count == 3)
    assert(d1[10]! == 1010)
    assert(d1[20]! == 1020)
    assert(d1[30]! == 1030)
    assert(d1[40] == .none)

    assert(d2.count == 4)
    assert(d2[10]! == 1010)
    assert(d2[20]! == 1020)
    assert(d2[30]! == 1030)
    assert(d2[40]! == 2040)

    // Keep variables alive.
    _fixLifetime(d1)
    _fixLifetime(d2)
  }

  do {
    var d1 = getCOWFastDictionary()
    var identity1 = d1._rawIdentifier()

    var d2 = d1
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 == d2._rawIdentifier())

    // Overwrite a value in existing binding.
    d2.updateValue(2010, forKey: 10)
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 != d2._rawIdentifier())

    assert(d1.count == 3)
    assert(d1[10]! == 1010)
    assert(d1[20]! == 1020)
    assert(d1[30]! == 1030)

    assert(d2.count == 3)
    assert(d2[10]! == 2010)
    assert(d2[20]! == 1020)
    assert(d2[30]! == 1030)

    // Keep variables alive.
    _fixLifetime(d1)
    _fixLifetime(d2)
  }
}

DictionaryTestSuite.test("COW.Slow.AddDoesNotReallocate") {
  do {
    var d1 = getCOWSlowDictionary()
    var identity1 = d1._rawIdentifier()

    // Insert a new key-value pair.
    assert(d1.updateValue(TestValueTy(2040), forKey: TestKeyTy(40)) == nil)
    assert(identity1 == d1._rawIdentifier())
    assert(d1.count == 4)
    assert(d1[TestKeyTy(40)]!.value == 2040)

    // Overwrite a value in existing binding.
    assert(d1.updateValue(TestValueTy(2010), forKey: TestKeyTy(10))!.value == 1010)
    assert(identity1 == d1._rawIdentifier())
    assert(d1.count == 4)
    assert(d1[TestKeyTy(10)]!.value == 2010)
  }

  do {
    var d1 = getCOWSlowDictionary()
    var identity1 = d1._rawIdentifier()

    var d2 = d1
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 == d2._rawIdentifier())

    // Insert a new key-value pair.
    d2.updateValue(TestValueTy(2040), forKey: TestKeyTy(40))
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 != d2._rawIdentifier())

    assert(d1.count == 3)
    assert(d1[TestKeyTy(10)]!.value == 1010)
    assert(d1[TestKeyTy(20)]!.value == 1020)
    assert(d1[TestKeyTy(30)]!.value == 1030)
    assert(d1[TestKeyTy(40)] == nil)

    assert(d2.count == 4)
    assert(d2[TestKeyTy(10)]!.value == 1010)
    assert(d2[TestKeyTy(20)]!.value == 1020)
    assert(d2[TestKeyTy(30)]!.value == 1030)
    assert(d2[TestKeyTy(40)]!.value == 2040)

    // Keep variables alive.
    _fixLifetime(d1)
    _fixLifetime(d2)
  }

  do {
    var d1 = getCOWSlowDictionary()
    var identity1 = d1._rawIdentifier()

    var d2 = d1
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 == d2._rawIdentifier())

    // Overwrite a value in existing binding.
    d2.updateValue(TestValueTy(2010), forKey: TestKeyTy(10))
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 != d2._rawIdentifier())

    assert(d1.count == 3)
    assert(d1[TestKeyTy(10)]!.value == 1010)
    assert(d1[TestKeyTy(20)]!.value == 1020)
    assert(d1[TestKeyTy(30)]!.value == 1030)

    assert(d2.count == 3)
    assert(d2[TestKeyTy(10)]!.value == 2010)
    assert(d2[TestKeyTy(20)]!.value == 1020)
    assert(d2[TestKeyTy(30)]!.value == 1030)

    // Keep variables alive.
    _fixLifetime(d1)
    _fixLifetime(d2)
  }
}


DictionaryTestSuite.test("COW.Fast.IndexForKeyDoesNotReallocate") {
  var d = getCOWFastDictionary()
  var identity1 = d._rawIdentifier()

  // Find an existing key.
  do {
    var foundIndex1 = d.index(forKey: 10)!
    assert(identity1 == d._rawIdentifier())

    var foundIndex2 = d.index(forKey: 10)!
    assert(foundIndex1 == foundIndex2)

    assert(d[foundIndex1].0 == 10)
    assert(d[foundIndex1].1 == 1010)
    assert(identity1 == d._rawIdentifier())
  }

  // Try to find a key that is not present.
  do {
    var foundIndex1 = d.index(forKey: 1111)
    assert(foundIndex1 == nil)
    assert(identity1 == d._rawIdentifier())
  }

  do {
    var d2: [MinimalHashableValue : OpaqueValue<Int>] = [:]
    MinimalHashableValue.timesEqualEqualWasCalled = 0
    MinimalHashableValue.timesHashValueWasCalled = 0
    expectEmpty(d2.index(forKey: MinimalHashableValue(42)))

    // If the dictionary is empty, we shouldn't be computing the hash value of
    // the provided key.
    expectEqual(0, MinimalHashableValue.timesEqualEqualWasCalled)
    expectEqual(0, MinimalHashableValue.timesHashValueWasCalled)
  }
}

DictionaryTestSuite.test("COW.Slow.IndexForKeyDoesNotReallocate") {
  var d = getCOWSlowDictionary()
  var identity1 = d._rawIdentifier()

  // Find an existing key.
  do {
    var foundIndex1 = d.index(forKey: TestKeyTy(10))!
    assert(identity1 == d._rawIdentifier())

    var foundIndex2 = d.index(forKey: TestKeyTy(10))!
    assert(foundIndex1 == foundIndex2)

    assert(d[foundIndex1].0 == TestKeyTy(10))
    assert(d[foundIndex1].1.value == 1010)
    assert(identity1 == d._rawIdentifier())
  }

  // Try to find a key that is not present.
  do {
    var foundIndex1 = d.index(forKey: TestKeyTy(1111))
    assert(foundIndex1 == nil)
    assert(identity1 == d._rawIdentifier())
  }

  do {
    var d2: [MinimalHashableClass : OpaqueValue<Int>] = [:]
    MinimalHashableClass.timesEqualEqualWasCalled = 0
    MinimalHashableClass.timesHashValueWasCalled = 0
    expectEmpty(d2.index(forKey: MinimalHashableClass(42)))

    // If the dictionary is empty, we shouldn't be computing the hash value of
    // the provided key.
    expectEqual(0, MinimalHashableClass.timesEqualEqualWasCalled)
    expectEqual(0, MinimalHashableClass.timesHashValueWasCalled)
  }
}


DictionaryTestSuite.test("COW.Fast.RemoveAtDoesNotReallocate") {
  do {
    var d = getCOWFastDictionary()
    var identity1 = d._rawIdentifier()

    let foundIndex1 = d.index(forKey: 10)!
    assert(identity1 == d._rawIdentifier())

    assert(d[foundIndex1].0 == 10)
    assert(d[foundIndex1].1 == 1010)

    let removed = d.remove(at: foundIndex1)
    assert(removed.0 == 10)
    assert(removed.1 == 1010)

    assert(identity1 == d._rawIdentifier())
    assert(d.index(forKey: 10) == nil)
  }

  do {
    var d1 = getCOWFastDictionary()
    var identity1 = d1._rawIdentifier()

    var d2 = d1
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 == d2._rawIdentifier())

    var foundIndex1 = d2.index(forKey: 10)!
    assert(d2[foundIndex1].0 == 10)
    assert(d2[foundIndex1].1 == 1010)
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 == d2._rawIdentifier())

    let removed = d2.remove(at: foundIndex1)
    assert(removed.0 == 10)
    assert(removed.1 == 1010)

    assert(identity1 == d1._rawIdentifier())
    assert(identity1 != d2._rawIdentifier())
    assert(d2.index(forKey: 10) == nil)
  }
}

DictionaryTestSuite.test("COW.Slow.RemoveAtDoesNotReallocate") {
  do {
    var d = getCOWSlowDictionary()
    var identity1 = d._rawIdentifier()

    var foundIndex1 = d.index(forKey: TestKeyTy(10))!
    assert(identity1 == d._rawIdentifier())

    assert(d[foundIndex1].0 == TestKeyTy(10))
    assert(d[foundIndex1].1.value == 1010)

    let removed = d.remove(at: foundIndex1)
    assert(removed.0 == TestKeyTy(10))
    assert(removed.1.value == 1010)

    assert(identity1 == d._rawIdentifier())
    assert(d.index(forKey: TestKeyTy(10)) == nil)
  }

  do {
    var d1 = getCOWSlowDictionary()
    var identity1 = d1._rawIdentifier()

    var d2 = d1
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 == d2._rawIdentifier())

    var foundIndex1 = d2.index(forKey: TestKeyTy(10))!
    assert(d2[foundIndex1].0 == TestKeyTy(10))
    assert(d2[foundIndex1].1.value == 1010)

    let removed = d2.remove(at: foundIndex1)
    assert(removed.0 == TestKeyTy(10))
    assert(removed.1.value == 1010)

    assert(identity1 == d1._rawIdentifier())
    assert(identity1 != d2._rawIdentifier())
    assert(d2.index(forKey: TestKeyTy(10)) == nil)
  }
}


DictionaryTestSuite.test("COW.Fast.RemoveValueForKeyDoesNotReallocate") {
  do {
    var d1 = getCOWFastDictionary()
    var identity1 = d1._rawIdentifier()

    var deleted = d1.removeValue(forKey: 0)
    assert(deleted == nil)
    assert(identity1 == d1._rawIdentifier())

    deleted = d1.removeValue(forKey: 10)
    assert(deleted! == 1010)
    assert(identity1 == d1._rawIdentifier())

    // Keep variables alive.
    _fixLifetime(d1)
  }

  do {
    var d1 = getCOWFastDictionary()
    var identity1 = d1._rawIdentifier()

    var d2 = d1
    var deleted = d2.removeValue(forKey: 0)
    assert(deleted == nil)
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 == d2._rawIdentifier())

    deleted = d2.removeValue(forKey: 10)
    assert(deleted! == 1010)
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 != d2._rawIdentifier())

    // Keep variables alive.
    _fixLifetime(d1)
    _fixLifetime(d2)
  }
}

DictionaryTestSuite.test("COW.Slow.RemoveValueForKeyDoesNotReallocate") {
  do {
    var d1 = getCOWSlowDictionary()
    var identity1 = d1._rawIdentifier()

    var deleted = d1.removeValue(forKey: TestKeyTy(0))
    assert(deleted == nil)
    assert(identity1 == d1._rawIdentifier())

    deleted = d1.removeValue(forKey: TestKeyTy(10))
    assert(deleted!.value == 1010)
    assert(identity1 == d1._rawIdentifier())

    // Keep variables alive.
    _fixLifetime(d1)
  }

  do {
    var d1 = getCOWSlowDictionary()
    var identity1 = d1._rawIdentifier()

    var d2 = d1
    var deleted = d2.removeValue(forKey: TestKeyTy(0))
    assert(deleted == nil)
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 == d2._rawIdentifier())

    deleted = d2.removeValue(forKey: TestKeyTy(10))
    assert(deleted!.value == 1010)
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 != d2._rawIdentifier())

    // Keep variables alive.
    _fixLifetime(d1)
    _fixLifetime(d2)
  }
}


DictionaryTestSuite.test("COW.Fast.RemoveAllDoesNotReallocate") {
  do {
    var d = getCOWFastDictionary()
    let originalCapacity = d._variantStorage.asNative.capacity
    assert(d.count == 3)
    assert(d[10]! == 1010)

    d.removeAll()
    // We cannot assert that identity changed, since the new buffer of smaller
    // size can be allocated at the same address as the old one.
    var identity1 = d._rawIdentifier()
    assert(d._variantStorage.asNative.capacity < originalCapacity)
    assert(d.count == 0)
    assert(d[10] == nil)

    d.removeAll()
    assert(identity1 == d._rawIdentifier())
    assert(d.count == 0)
    assert(d[10] == nil)
  }

  do {
    var d = getCOWFastDictionary()
    var identity1 = d._rawIdentifier()
    let originalCapacity = d._variantStorage.asNative.capacity
    assert(d.count == 3)
    assert(d[10]! == 1010)

    d.removeAll(keepingCapacity: true)
    assert(identity1 == d._rawIdentifier())
    assert(d._variantStorage.asNative.capacity == originalCapacity)
    assert(d.count == 0)
    assert(d[10] == nil)

    d.removeAll(keepingCapacity: true)
    assert(identity1 == d._rawIdentifier())
    assert(d._variantStorage.asNative.capacity == originalCapacity)
    assert(d.count == 0)
    assert(d[10] == nil)
  }

  do {
    var d1 = getCOWFastDictionary()
    var identity1 = d1._rawIdentifier()
    assert(d1.count == 3)
    assert(d1[10]! == 1010)

    var d2 = d1
    d2.removeAll()
    var identity2 = d2._rawIdentifier()
    assert(identity1 == d1._rawIdentifier())
    assert(identity2 != identity1)
    assert(d1.count == 3)
    assert(d1[10]! == 1010)
    assert(d2.count == 0)
    assert(d2[10] == nil)

    // Keep variables alive.
    _fixLifetime(d1)
    _fixLifetime(d2)
  }

  do {
    var d1 = getCOWFastDictionary()
    var identity1 = d1._rawIdentifier()
    let originalCapacity = d1._variantStorage.asNative.capacity
    assert(d1.count == 3)
    assert(d1[10] == 1010)

    var d2 = d1
    d2.removeAll(keepingCapacity: true)
    var identity2 = d2._rawIdentifier()
    assert(identity1 == d1._rawIdentifier())
    assert(identity2 != identity1)
    assert(d1.count == 3)
    assert(d1[10]! == 1010)
    assert(d2._variantStorage.asNative.capacity == originalCapacity)
    assert(d2.count == 0)
    assert(d2[10] == nil)

    // Keep variables alive.
    _fixLifetime(d1)
    _fixLifetime(d2)
  }
}

DictionaryTestSuite.test("COW.Slow.RemoveAllDoesNotReallocate") {
  do {
    var d = getCOWSlowDictionary()
    let originalCapacity = d._variantStorage.asNative.capacity
    assert(d.count == 3)
    assert(d[TestKeyTy(10)]!.value == 1010)

    d.removeAll()
    // We cannot assert that identity changed, since the new buffer of smaller
    // size can be allocated at the same address as the old one.
    var identity1 = d._rawIdentifier()
    assert(d._variantStorage.asNative.capacity < originalCapacity)
    assert(d.count == 0)
    assert(d[TestKeyTy(10)] == nil)

    d.removeAll()
    assert(identity1 == d._rawIdentifier())
    assert(d.count == 0)
    assert(d[TestKeyTy(10)] == nil)
  }

  do {
    var d = getCOWSlowDictionary()
    var identity1 = d._rawIdentifier()
    let originalCapacity = d._variantStorage.asNative.capacity
    assert(d.count == 3)
    assert(d[TestKeyTy(10)]!.value == 1010)

    d.removeAll(keepingCapacity: true)
    assert(identity1 == d._rawIdentifier())
    assert(d._variantStorage.asNative.capacity == originalCapacity)
    assert(d.count == 0)
    assert(d[TestKeyTy(10)] == nil)

    d.removeAll(keepingCapacity: true)
    assert(identity1 == d._rawIdentifier())
    assert(d._variantStorage.asNative.capacity == originalCapacity)
    assert(d.count == 0)
    assert(d[TestKeyTy(10)] == nil)
  }

  do {
    var d1 = getCOWSlowDictionary()
    var identity1 = d1._rawIdentifier()
    assert(d1.count == 3)
    assert(d1[TestKeyTy(10)]!.value == 1010)

    var d2 = d1
    d2.removeAll()
    var identity2 = d2._rawIdentifier()
    assert(identity1 == d1._rawIdentifier())
    assert(identity2 != identity1)
    assert(d1.count == 3)
    assert(d1[TestKeyTy(10)]!.value == 1010)
    assert(d2.count == 0)
    assert(d2[TestKeyTy(10)] == nil)

    // Keep variables alive.
    _fixLifetime(d1)
    _fixLifetime(d2)
  }

  do {
    var d1 = getCOWSlowDictionary()
    var identity1 = d1._rawIdentifier()
    let originalCapacity = d1._variantStorage.asNative.capacity
    assert(d1.count == 3)
    assert(d1[TestKeyTy(10)]!.value == 1010)

    var d2 = d1
    d2.removeAll(keepingCapacity: true)
    var identity2 = d2._rawIdentifier()
    assert(identity1 == d1._rawIdentifier())
    assert(identity2 != identity1)
    assert(d1.count == 3)
    assert(d1[TestKeyTy(10)]!.value == 1010)
    assert(d2._variantStorage.asNative.capacity == originalCapacity)
    assert(d2.count == 0)
    assert(d2[TestKeyTy(10)] == nil)

    // Keep variables alive.
    _fixLifetime(d1)
    _fixLifetime(d2)
  }
}


DictionaryTestSuite.test("COW.Fast.CountDoesNotReallocate") {
  var d = getCOWFastDictionary()
  var identity1 = d._rawIdentifier()

  assert(d.count == 3)
  assert(identity1 == d._rawIdentifier())
}

DictionaryTestSuite.test("COW.Slow.CountDoesNotReallocate") {
  var d = getCOWSlowDictionary()
  var identity1 = d._rawIdentifier()

  assert(d.count == 3)
  assert(identity1 == d._rawIdentifier())
}


DictionaryTestSuite.test("COW.Fast.GenerateDoesNotReallocate") {
  var d = getCOWFastDictionary()
  var identity1 = d._rawIdentifier()

  var iter = d.makeIterator()
  var pairs = Array<(Int, Int)>()
  while let (key, value) = iter.next() {
    pairs += [(key, value)]
  }
  assert(equalsUnordered(pairs, [ (10, 1010), (20, 1020), (30, 1030) ]))
  assert(identity1 == d._rawIdentifier())
}

DictionaryTestSuite.test("COW.Slow.GenerateDoesNotReallocate") {
  var d = getCOWSlowDictionary()
  var identity1 = d._rawIdentifier()

  var iter = d.makeIterator()
  var pairs = Array<(Int, Int)>()
  while let (key, value) = iter.next() {
    // FIXME: This doesn't work (<rdar://problem/17751308> Can't +=
    // with array literal of pairs)
    // pairs += [(key.value, value.value)]

    // FIXME: This doesn't work (<rdar://problem/17750582> generics over tuples)
    // pairs.append((key.value, value.value))

    // FIXME: This doesn't work (<rdar://problem/17751359>)
    // pairs.append(key.value, value.value)

    let kv = (key.value, value.value)
    pairs += [kv]
  }
  assert(equalsUnordered(pairs, [ (10, 1010), (20, 1020), (30, 1030) ]))
  assert(identity1 == d._rawIdentifier())
}


DictionaryTestSuite.test("COW.Fast.EqualityTestDoesNotReallocate") {
  var d1 = getCOWFastDictionary()
  var identity1 = d1._rawIdentifier()

  var d2 = getCOWFastDictionary()
  var identity2 = d2._rawIdentifier()

  assert(d1 == d2)
  assert(identity1 == d1._rawIdentifier())
  assert(identity2 == d2._rawIdentifier())

  d2[40] = 2040
  assert(d1 != d2)
  assert(identity1 == d1._rawIdentifier())
  assert(identity2 == d2._rawIdentifier())
}

DictionaryTestSuite.test("COW.Slow.EqualityTestDoesNotReallocate") {
  var d1 = getCOWSlowEquatableDictionary()
  var identity1 = d1._rawIdentifier()

  var d2 = getCOWSlowEquatableDictionary()
  var identity2 = d2._rawIdentifier()

  assert(d1 == d2)
  assert(identity1 == d1._rawIdentifier())
  assert(identity2 == d2._rawIdentifier())

  d2[TestKeyTy(40)] = TestEquatableValueTy(2040)
  assert(d1 != d2)
  assert(identity1 == d1._rawIdentifier())
  assert(identity2 == d2._rawIdentifier())
}


//===---
// Native dictionary tests.
//===---

func helperDeleteThree(_ k1: TestKeyTy, _ k2: TestKeyTy, _ k3: TestKeyTy) {
  var d1 = Dictionary<TestKeyTy, TestValueTy>(minimumCapacity: 10)

  d1[k1] = TestValueTy(1010)
  d1[k2] = TestValueTy(1020)
  d1[k3] = TestValueTy(1030)

  assert(d1[k1]!.value == 1010)
  assert(d1[k2]!.value == 1020)
  assert(d1[k3]!.value == 1030)

  d1[k1] = nil
  assert(d1[k2]!.value == 1020)
  assert(d1[k3]!.value == 1030)

  d1[k2] = nil
  assert(d1[k3]!.value == 1030)

  d1[k3] = nil
  assert(d1.count == 0)
}

DictionaryTestSuite.test("deleteChainCollision") {
  var k1 = TestKeyTy(value: 10, hashValue: 0)
  var k2 = TestKeyTy(value: 20, hashValue: 0)
  var k3 = TestKeyTy(value: 30, hashValue: 0)

  helperDeleteThree(k1, k2, k3)
}

DictionaryTestSuite.test("deleteChainNoCollision") {
  var k1 = TestKeyTy(value: 10, hashValue: 0)
  var k2 = TestKeyTy(value: 20, hashValue: 1)
  var k3 = TestKeyTy(value: 30, hashValue: 2)

  helperDeleteThree(k1, k2, k3)
}

DictionaryTestSuite.test("deleteChainCollision2") {
  var k1_0 = TestKeyTy(value: 10, hashValue: 0)
  var k2_0 = TestKeyTy(value: 20, hashValue: 0)
  var k3_2 = TestKeyTy(value: 30, hashValue: 2)
  var k4_0 = TestKeyTy(value: 40, hashValue: 0)
  var k5_2 = TestKeyTy(value: 50, hashValue: 2)
  var k6_0 = TestKeyTy(value: 60, hashValue: 0)

  var d = Dictionary<TestKeyTy, TestValueTy>(minimumCapacity: 10)

  d[k1_0] = TestValueTy(1010) // in bucket 0
  d[k2_0] = TestValueTy(1020) // in bucket 1
  d[k3_2] = TestValueTy(1030) // in bucket 2
  d[k4_0] = TestValueTy(1040) // in bucket 3
  d[k5_2] = TestValueTy(1050) // in bucket 4
  d[k6_0] = TestValueTy(1060) // in bucket 5

  d[k3_2] = nil

  assert(d[k1_0]!.value == 1010)
  assert(d[k2_0]!.value == 1020)
  assert(d[k3_2] == nil)
  assert(d[k4_0]!.value == 1040)
  assert(d[k5_2]!.value == 1050)
  assert(d[k6_0]!.value == 1060)
}

func uniformRandom(_ max: Int) -> Int {
#if os(Linux)
  // SR-685: Can't use arc4random on Linux
  return Int(random() % (max + 1))
#else
  return Int(arc4random_uniform(UInt32(max)))
#endif
}

func pickRandom<T>(_ a: [T]) -> T {
  return a[uniformRandom(a.count)]
}

func product<C1 : Collection, C2 : Collection>(
  _ c1: C1, _ c2: C2
) -> [(C1.Iterator.Element, C2.Iterator.Element)] {
  var result: [(C1.Iterator.Element, C2.Iterator.Element)] = []
  for e1 in c1 {
    for e2 in c2 {
      result.append((e1, e2))
    }
  }
  return result
}

DictionaryTestSuite.test("deleteChainCollisionRandomized")
  .forEach(in: product(1...8, 0...5)) {
  (collisionChains, chainOverlap) in

  func check(_ d: Dictionary<TestKeyTy, TestValueTy>) {
    var keys = Array(d.keys)
    for i in 0..<keys.count {
      for j in 0..<i {
        assert(keys[i] != keys[j])
      }
    }

    for k in keys {
      assert(d[k] != nil)
    }
  }

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

  var d = Dictionary<TestKeyTy, TestValueTy>(minimumCapacity: 30)
  for i in 1..<300 {
    let key = getKey(uniformRandom(collisionChains * chainLength))
    if uniformRandom(chainLength * 2) == 0 {
      d[key] = nil
    } else {
      d[key] = TestValueTy(key.value * 10)
    }
    check(d)
  }
}

DictionaryTestSuite.test("init(dictionaryLiteral:)") {
  do {
    var empty = Dictionary<Int, Int>()
    assert(empty.count == 0)
    assert(empty[1111] == nil)
  }
  do {
    var d = Dictionary(dictionaryLiteral: (10, 1010))
    assert(d.count == 1)
    assert(d[10]! == 1010)
    assert(d[1111] == nil)
  }
  do {
    var d = Dictionary(dictionaryLiteral: 
        (10, 1010), (20, 1020))
    assert(d.count == 2)
    assert(d[10]! == 1010)
    assert(d[20]! == 1020)
    assert(d[1111] == nil)
  }
  do {
    var d = Dictionary(dictionaryLiteral: 
        (10, 1010), (20, 1020), (30, 1030))
    assert(d.count == 3)
    assert(d[10]! == 1010)
    assert(d[20]! == 1020)
    assert(d[30]! == 1030)
    assert(d[1111] == nil)
  }
  do {
    var d = Dictionary(dictionaryLiteral: 
        (10, 1010), (20, 1020), (30, 1030), (40, 1040))
    assert(d.count == 4)
    assert(d[10]! == 1010)
    assert(d[20]! == 1020)
    assert(d[30]! == 1030)
    assert(d[40]! == 1040)
    assert(d[1111] == nil)
  }
  do {
    var d: Dictionary<Int, Int> = [ 10: 1010, 20: 1020, 30: 1030 ]
    assert(d.count == 3)
    assert(d[10]! == 1010)
    assert(d[20]! == 1020)
    assert(d[30]! == 1030)
  }
}

#if _runtime(_ObjC)
//===---
// NSDictionary -> Dictionary bridging tests.
//===---

func getAsNSDictionary(_ d: Dictionary<Int, Int>) -> NSDictionary {
  let keys = Array(d.keys.map { TestObjCKeyTy($0) })
  let values = Array(d.values.map { TestObjCValueTy($0) })

  // Return an `NSMutableDictionary` to make sure that it has a unique
  // pointer identity.
  return NSMutableDictionary(objects: values, forKeys: keys)
}

func getAsEquatableNSDictionary(_ d: Dictionary<Int, Int>) -> NSDictionary {
  let keys = Array(d.keys.map { TestObjCKeyTy($0) })
  let values = Array(d.values.map { TestObjCEquatableValueTy($0) })

  // Return an `NSMutableDictionary` to make sure that it has a unique
  // pointer identity.
  return NSMutableDictionary(objects: values, forKeys: keys)
}

func getAsNSMutableDictionary(_ d: Dictionary<Int, Int>) -> NSMutableDictionary {
  let keys = Array(d.keys.map { TestObjCKeyTy($0) })
  let values = Array(d.values.map { TestObjCValueTy($0) })

  return NSMutableDictionary(objects: values, forKeys: keys)
}

func getBridgedVerbatimDictionary() -> Dictionary<NSObject, AnyObject> {
  let nsd = getAsNSDictionary([10: 1010, 20: 1020, 30: 1030])
  return convertNSDictionaryToDictionary(nsd)
}

func getBridgedVerbatimDictionary(_ d: Dictionary<Int, Int>) -> Dictionary<NSObject, AnyObject> {
  let nsd = getAsNSDictionary(d)
  return convertNSDictionaryToDictionary(nsd)
}

func getBridgedVerbatimDictionaryAndNSMutableDictionary()
    -> (Dictionary<NSObject, AnyObject>, NSMutableDictionary) {
  let nsd = getAsNSMutableDictionary([10: 1010, 20: 1020, 30: 1030])
  return (convertNSDictionaryToDictionary(nsd), nsd)
}

func getBridgedNonverbatimDictionary() -> Dictionary<TestBridgedKeyTy, TestBridgedValueTy> {
  let nsd = getAsNSDictionary([10: 1010, 20: 1020, 30: 1030 ])
  return Swift._forceBridgeFromObjectiveC(nsd, Dictionary.self)
}

func getBridgedNonverbatimDictionary(_ d: Dictionary<Int, Int>) -> Dictionary<TestBridgedKeyTy, TestBridgedValueTy> {
  let nsd = getAsNSDictionary(d)
  return Swift._forceBridgeFromObjectiveC(nsd, Dictionary.self)
}

func getBridgedNonverbatimDictionaryAndNSMutableDictionary()
    -> (Dictionary<TestBridgedKeyTy, TestBridgedValueTy>, NSMutableDictionary) {
  let nsd = getAsNSMutableDictionary([10: 1010, 20: 1020, 30: 1030])
  return (Swift._forceBridgeFromObjectiveC(nsd, Dictionary.self), nsd)
}

func getBridgedVerbatimEquatableDictionary(_ d: Dictionary<Int, Int>) -> Dictionary<NSObject, TestObjCEquatableValueTy> {
  let nsd = getAsEquatableNSDictionary(d)
  return convertNSDictionaryToDictionary(nsd)
}

func getBridgedNonverbatimEquatableDictionary(_ d: Dictionary<Int, Int>) -> Dictionary<TestBridgedKeyTy, TestBridgedEquatableValueTy> {
  let nsd = getAsEquatableNSDictionary(d)
  return Swift._forceBridgeFromObjectiveC(nsd, Dictionary.self)
}

func getHugeBridgedVerbatimDictionaryHelper() -> NSDictionary {
  let keys = (1...32).map { TestObjCKeyTy($0) }
  let values = (1...32).map { TestObjCValueTy(1000 + $0) }

  return NSMutableDictionary(objects: values, forKeys: keys)
}

func getHugeBridgedVerbatimDictionary() -> Dictionary<NSObject, AnyObject> {
  let nsd = getHugeBridgedVerbatimDictionaryHelper()
  return convertNSDictionaryToDictionary(nsd)
}

func getHugeBridgedNonverbatimDictionary() -> Dictionary<TestBridgedKeyTy, TestBridgedValueTy> {
  let nsd = getHugeBridgedVerbatimDictionaryHelper()
  return Swift._forceBridgeFromObjectiveC(nsd, Dictionary.self)
}

/// A mock dictionary that stores its keys and values in parallel arrays, which
/// allows it to return inner pointers to the keys array in fast enumeration.
@objc
class ParallelArrayDictionary : NSDictionary {
  struct Keys {
    var key0: AnyObject = TestObjCKeyTy(10)
    var key1: AnyObject = TestObjCKeyTy(20)
    var key2: AnyObject = TestObjCKeyTy(30)
    var key3: AnyObject = TestObjCKeyTy(40)
  }
  var keys = [ Keys() ]
  var value: AnyObject = TestObjCValueTy(1111)

  override init() {
    super.init()
  }

  override init(
    objects: UnsafePointer<AnyObject>,
    forKeys keys: UnsafePointer<NSCopying>,
    count: Int) {
    super.init(objects: objects, forKeys: keys, count: count)
  }

  required init(coder aDecoder: NSCoder) {
    fatalError("init(coder:) not implemented by ParallelArrayDictionary")
  }

  @objc(copyWithZone:)
  override func copy(with zone: NSZone?) -> Any {
    // Ensure that copying this dictionary does not produce a CoreFoundation
    // object.
    return self
  }

  override func countByEnumerating(
    with state: UnsafeMutablePointer<NSFastEnumerationState>,
    objects: AutoreleasingUnsafeMutablePointer<AnyObject?>,
    count: Int
  ) -> Int {
    var theState = state.pointee
    if theState.state == 0 {
      theState.state = 1
      theState.itemsPtr = AutoreleasingUnsafeMutablePointer(keys._baseAddressIfContiguous)
      theState.mutationsPtr = _fastEnumerationStorageMutationsPtr
      state.pointee = theState
      return 4
    }
    return 0
  }

  override func object(forKey aKey: Any) -> Any? {
    return value
  }

  override var count: Int {
    return 4
  }
}

func getParallelArrayBridgedVerbatimDictionary() -> Dictionary<NSObject, AnyObject> {
  let nsd: NSDictionary = ParallelArrayDictionary()
  return convertNSDictionaryToDictionary(nsd)
}

func getParallelArrayBridgedNonverbatimDictionary() -> Dictionary<TestBridgedKeyTy, TestBridgedValueTy> {
  let nsd: NSDictionary = ParallelArrayDictionary()
  return Swift._forceBridgeFromObjectiveC(nsd, Dictionary.self)
}

@objc
class CustomImmutableNSDictionary : NSDictionary {
  init(_privateInit: ()) {
    super.init()
  }

  override init() {
    expectUnreachable()
    super.init()
  }

  override init(
    objects: UnsafePointer<AnyObject>,
    forKeys keys: UnsafePointer<NSCopying>,
    count: Int) {
    expectUnreachable()
    super.init(objects: objects, forKeys: keys, count: count)
  }

  required init(coder aDecoder: NSCoder) {
    fatalError("init(coder:) not implemented by CustomImmutableNSDictionary")
  }

  @objc(copyWithZone:)
  override func copy(with zone: NSZone?) -> Any {
    CustomImmutableNSDictionary.timesCopyWithZoneWasCalled += 1
    return self
  }

  override func object(forKey aKey: Any) -> Any? {
    CustomImmutableNSDictionary.timesObjectForKeyWasCalled += 1
    return getAsNSDictionary([10: 1010, 20: 1020, 30: 1030]).object(forKey: aKey)
  }

  override func keyEnumerator() -> NSEnumerator {
    CustomImmutableNSDictionary.timesKeyEnumeratorWasCalled += 1
    return getAsNSDictionary([10: 1010, 20: 1020, 30: 1030]).keyEnumerator()
  }

  override var count: Int {
    CustomImmutableNSDictionary.timesCountWasCalled += 1
    return 3
  }

  static var timesCopyWithZoneWasCalled = 0
  static var timesObjectForKeyWasCalled = 0
  static var timesKeyEnumeratorWasCalled = 0
  static var timesCountWasCalled = 0
}

DictionaryTestSuite.test("BridgedFromObjC.Verbatim.DictionaryIsCopied") {
  var (d, nsd) = getBridgedVerbatimDictionaryAndNSMutableDictionary()
  var identity1 = d._rawIdentifier()
  assert(isCocoaDictionary(d))

  // Find an existing key.
  do {
    var kv = d[d.index(forKey: TestObjCKeyTy(10))!]
    assert(kv.0 == TestObjCKeyTy(10))
    assert((kv.1 as! TestObjCValueTy).value == 1010)
  }

  // Delete the key from the NSMutableDictionary.
  assert(nsd[TestObjCKeyTy(10)] != nil)
  nsd.removeObject(forKey: TestObjCKeyTy(10))
  assert(nsd[TestObjCKeyTy(10)] == nil)

  // Find an existing key, again.
  do {
    var kv = d[d.index(forKey: TestObjCKeyTy(10))!]
    assert(kv.0 == TestObjCKeyTy(10))
    assert((kv.1 as! TestObjCValueTy).value == 1010)
  }
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.DictionaryIsCopied") {
  var (d, nsd) = getBridgedNonverbatimDictionaryAndNSMutableDictionary()
  var identity1 = d._rawIdentifier()
  assert(isNativeDictionary(d))

  // Find an existing key.
  do {
    var kv = d[d.index(forKey: TestBridgedKeyTy(10))!]
    assert(kv.0 == TestBridgedKeyTy(10))
    assert(kv.1.value == 1010)
  }

  // Delete the key from the NSMutableDictionary.
  assert(nsd[TestBridgedKeyTy(10) as NSCopying] != nil)
  nsd.removeObject(forKey: TestBridgedKeyTy(10) as NSCopying)
  assert(nsd[TestBridgedKeyTy(10) as NSCopying] == nil)

  // Find an existing key, again.
  do {
    var kv = d[d.index(forKey: TestBridgedKeyTy(10))!]
    assert(kv.0 == TestBridgedKeyTy(10))
    assert(kv.1.value == 1010)
  }
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.NSDictionaryIsRetained") {
  var nsd: NSDictionary = autoreleasepool {
    NSDictionary(dictionary:
      getAsNSDictionary([10: 1010, 20: 1020, 30: 1030]))
  }

  var d: [NSObject : AnyObject] = convertNSDictionaryToDictionary(nsd)

  var bridgedBack: NSDictionary = convertDictionaryToNSDictionary(d)

  expectEqual(
    unsafeBitCast(nsd, to: Int.self),
    unsafeBitCast(bridgedBack, to: Int.self))

  _fixLifetime(nsd)
  _fixLifetime(d)
  _fixLifetime(bridgedBack)
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.NSDictionaryIsCopied") {
  var nsd: NSDictionary = autoreleasepool {
    NSDictionary(dictionary:
      getAsNSDictionary([10: 1010, 20: 1020, 30: 1030]))
  }

  var d: [TestBridgedKeyTy : TestBridgedValueTy] =
    convertNSDictionaryToDictionary(nsd)

  var bridgedBack: NSDictionary = convertDictionaryToNSDictionary(d)

  expectNotEqual(
    unsafeBitCast(nsd, to: Int.self),
    unsafeBitCast(bridgedBack, to: Int.self))

  _fixLifetime(nsd)
  _fixLifetime(d)
  _fixLifetime(bridgedBack)
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.ImmutableDictionaryIsRetained") {
  var nsd: NSDictionary = CustomImmutableNSDictionary(_privateInit: ())

  CustomImmutableNSDictionary.timesCopyWithZoneWasCalled = 0
  CustomImmutableNSDictionary.timesObjectForKeyWasCalled = 0
  CustomImmutableNSDictionary.timesKeyEnumeratorWasCalled = 0
  CustomImmutableNSDictionary.timesCountWasCalled = 0
  var d: [NSObject : AnyObject] = convertNSDictionaryToDictionary(nsd)
  expectEqual(1, CustomImmutableNSDictionary.timesCopyWithZoneWasCalled)
  expectEqual(0, CustomImmutableNSDictionary.timesObjectForKeyWasCalled)
  expectEqual(0, CustomImmutableNSDictionary.timesKeyEnumeratorWasCalled)
  expectEqual(0, CustomImmutableNSDictionary.timesCountWasCalled)

  var bridgedBack: NSDictionary = convertDictionaryToNSDictionary(d)
  expectEqual(
    unsafeBitCast(nsd, to: Int.self),
    unsafeBitCast(bridgedBack, to: Int.self))

  _fixLifetime(nsd)
  _fixLifetime(d)
  _fixLifetime(bridgedBack)
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.ImmutableDictionaryIsCopied") {
  var nsd: NSDictionary = CustomImmutableNSDictionary(_privateInit: ())

  CustomImmutableNSDictionary.timesCopyWithZoneWasCalled = 0
  CustomImmutableNSDictionary.timesObjectForKeyWasCalled = 0
  CustomImmutableNSDictionary.timesKeyEnumeratorWasCalled = 0
  CustomImmutableNSDictionary.timesCountWasCalled = 0
  TestBridgedValueTy.bridgeOperations = 0
  var d: [TestBridgedKeyTy : TestBridgedValueTy] =
    convertNSDictionaryToDictionary(nsd)
  expectEqual(0, CustomImmutableNSDictionary.timesCopyWithZoneWasCalled)
  expectEqual(3, CustomImmutableNSDictionary.timesObjectForKeyWasCalled)
  expectEqual(1, CustomImmutableNSDictionary.timesKeyEnumeratorWasCalled)
  expectNotEqual(0, CustomImmutableNSDictionary.timesCountWasCalled)
  expectEqual(3, TestBridgedValueTy.bridgeOperations)

  var bridgedBack: NSDictionary = convertDictionaryToNSDictionary(d)
  expectNotEqual(
    unsafeBitCast(nsd, to: Int.self),
    unsafeBitCast(bridgedBack, to: Int.self))

  _fixLifetime(nsd)
  _fixLifetime(d)
  _fixLifetime(bridgedBack)
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.IndexForKey") {
  var d = getBridgedVerbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isCocoaDictionary(d))

  // Find an existing key.
  do {
    var kv = d[d.index(forKey: TestObjCKeyTy(10))!]
    assert(kv.0 == TestObjCKeyTy(10))
    assert((kv.1 as! TestObjCValueTy).value == 1010)

    kv = d[d.index(forKey: TestObjCKeyTy(20))!]
    assert(kv.0 == TestObjCKeyTy(20))
    assert((kv.1 as! TestObjCValueTy).value == 1020)

    kv = d[d.index(forKey: TestObjCKeyTy(30))!]
    assert(kv.0 == TestObjCKeyTy(30))
    assert((kv.1 as! TestObjCValueTy).value == 1030)
  }

  // Try to find a key that does not exist.
  assert(d.index(forKey: TestObjCKeyTy(40)) == nil)
  assert(identity1 == d._rawIdentifier())
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.IndexForKey") {
  var d = getBridgedNonverbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isNativeDictionary(d))

  // Find an existing key.
  do {
    var kv = d[d.index(forKey: TestBridgedKeyTy(10))!]
    assert(kv.0 == TestBridgedKeyTy(10))
    assert(kv.1.value == 1010)

    kv = d[d.index(forKey: TestBridgedKeyTy(20))!]
    assert(kv.0 == TestBridgedKeyTy(20))
    assert(kv.1.value == 1020)

    kv = d[d.index(forKey: TestBridgedKeyTy(30))!]
    assert(kv.0 == TestBridgedKeyTy(30))
    assert(kv.1.value == 1030)
  }

  // Try to find a key that does not exist.
  assert(d.index(forKey: TestBridgedKeyTy(40)) == nil)
  assert(identity1 == d._rawIdentifier())
}

DictionaryTestSuite.test("BridgedFromObjC.Verbatim.SubscriptWithIndex") {
  var d = getBridgedVerbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isCocoaDictionary(d))

  var startIndex = d.startIndex
  var endIndex = d.endIndex
  assert(startIndex != endIndex)
  assert(startIndex < endIndex)
  assert(startIndex <= endIndex)
  assert(!(startIndex >= endIndex))
  assert(!(startIndex > endIndex))
  assert(identity1 == d._rawIdentifier())

  var pairs = Array<(Int, Int)>()
  for i in d.indices {
    var (key, value) = d[i]
    let kv = ((key as! TestObjCKeyTy).value, (value as! TestObjCValueTy).value)
    pairs += [kv]
  }
  assert(equalsUnordered(pairs, [ (10, 1010), (20, 1020), (30, 1030) ]))
  assert(identity1 == d._rawIdentifier())

  // Keep indexes alive during the calls above.
  _fixLifetime(startIndex)
  _fixLifetime(endIndex)
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.SubscriptWithIndex") {
  var d = getBridgedNonverbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isNativeDictionary(d))

  var startIndex = d.startIndex
  var endIndex = d.endIndex
  assert(startIndex != endIndex)
  assert(startIndex < endIndex)
  assert(startIndex <= endIndex)
  assert(!(startIndex >= endIndex))
  assert(!(startIndex > endIndex))
  assert(identity1 == d._rawIdentifier())

  var pairs = Array<(Int, Int)>()
  for i in d.indices {
    var (key, value) = d[i]
    let kv = (key.value, value.value)
    pairs += [kv]
  }
  assert(equalsUnordered(pairs, [ (10, 1010), (20, 1020), (30, 1030) ]))
  assert(identity1 == d._rawIdentifier())

  // Keep indexes alive during the calls above.
  _fixLifetime(startIndex)
  _fixLifetime(endIndex)
}

DictionaryTestSuite.test("BridgedFromObjC.Verbatim.SubscriptWithIndex_Empty") {
  var d = getBridgedVerbatimDictionary([:])
  var identity1 = d._rawIdentifier()
  assert(isCocoaDictionary(d))

  var startIndex = d.startIndex
  var endIndex = d.endIndex
  assert(startIndex == endIndex)
  assert(!(startIndex < endIndex))
  assert(startIndex <= endIndex)
  assert(startIndex >= endIndex)
  assert(!(startIndex > endIndex))
  assert(identity1 == d._rawIdentifier())

  // Keep indexes alive during the calls above.
  _fixLifetime(startIndex)
  _fixLifetime(endIndex)
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.SubscriptWithIndex_Empty") {
  var d = getBridgedNonverbatimDictionary([:])
  var identity1 = d._rawIdentifier()
  assert(isNativeDictionary(d))

  var startIndex = d.startIndex
  var endIndex = d.endIndex
  assert(startIndex == endIndex)
  assert(!(startIndex < endIndex))
  assert(startIndex <= endIndex)
  assert(startIndex >= endIndex)
  assert(!(startIndex > endIndex))
  assert(identity1 == d._rawIdentifier())

  // Keep indexes alive during the calls above.
  _fixLifetime(startIndex)
  _fixLifetime(endIndex)
}

DictionaryTestSuite.test("BridgedFromObjC.Verbatim.SubscriptWithKey") {
  var d = getBridgedVerbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isCocoaDictionary(d))

  // Read existing key-value pairs.
  var v = d[TestObjCKeyTy(10)] as! TestObjCValueTy
  assert(v.value == 1010)

  v = d[TestObjCKeyTy(20)] as! TestObjCValueTy
  assert(v.value == 1020)

  v = d[TestObjCKeyTy(30)] as! TestObjCValueTy
  assert(v.value == 1030)

  assert(identity1 == d._rawIdentifier())

  // Insert a new key-value pair.
  d[TestObjCKeyTy(40)] = TestObjCValueTy(2040)
  var identity2 = d._rawIdentifier()
  assert(identity1 != identity2)
  assert(isNativeDictionary(d))
  assert(d.count == 4)

  v = d[TestObjCKeyTy(10)] as! TestObjCValueTy
  assert(v.value == 1010)

  v = d[TestObjCKeyTy(20)] as! TestObjCValueTy
  assert(v.value == 1020)

  v = d[TestObjCKeyTy(30)] as! TestObjCValueTy
  assert(v.value == 1030)

  v = d[TestObjCKeyTy(40)] as! TestObjCValueTy
  assert(v.value == 2040)

  // Overwrite value in existing binding.
  d[TestObjCKeyTy(10)] = TestObjCValueTy(2010)
  assert(identity2 == d._rawIdentifier())
  assert(isNativeDictionary(d))
  assert(d.count == 4)

  v = d[TestObjCKeyTy(10)] as! TestObjCValueTy
  assert(v.value == 2010)

  v = d[TestObjCKeyTy(20)] as! TestObjCValueTy
  assert(v.value == 1020)

  v = d[TestObjCKeyTy(30)] as! TestObjCValueTy
  assert(v.value == 1030)

  v = d[TestObjCKeyTy(40)] as! TestObjCValueTy
  assert(v.value == 2040)
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.SubscriptWithKey") {
  var d = getBridgedNonverbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isNativeDictionary(d))

  // Read existing key-value pairs.
  var v = d[TestBridgedKeyTy(10)]
  assert(v!.value == 1010)

  v = d[TestBridgedKeyTy(20)]
  assert(v!.value == 1020)

  v = d[TestBridgedKeyTy(30)]
  assert(v!.value == 1030)

  assert(identity1 == d._rawIdentifier())

  // Insert a new key-value pair.
  d[TestBridgedKeyTy(40)] = TestBridgedValueTy(2040)
  var identity2 = d._rawIdentifier()
  assert(identity1 != identity2)
  assert(isNativeDictionary(d))
  assert(d.count == 4)

  v = d[TestBridgedKeyTy(10)]
  assert(v!.value == 1010)

  v = d[TestBridgedKeyTy(20)]
  assert(v!.value == 1020)

  v = d[TestBridgedKeyTy(30)]
  assert(v!.value == 1030)

  v = d[TestBridgedKeyTy(40)]
  assert(v!.value == 2040)

  // Overwrite value in existing binding.
  d[TestBridgedKeyTy(10)] = TestBridgedValueTy(2010)
  assert(identity2 == d._rawIdentifier())
  assert(isNativeDictionary(d))
  assert(d.count == 4)

  v = d[TestBridgedKeyTy(10)]
  assert(v!.value == 2010)

  v = d[TestBridgedKeyTy(20)]
  assert(v!.value == 1020)

  v = d[TestBridgedKeyTy(30)]
  assert(v!.value == 1030)

  v = d[TestBridgedKeyTy(40)]
  assert(v!.value == 2040)
}

DictionaryTestSuite.test("BridgedFromObjC.Verbatim.UpdateValueForKey") {
  // Insert a new key-value pair.
  do {
    var d = getBridgedVerbatimDictionary()
    var identity1 = d._rawIdentifier()
    assert(isCocoaDictionary(d))

    var oldValue: AnyObject? =
        d.updateValue(TestObjCValueTy(2040), forKey: TestObjCKeyTy(40))
    assert(oldValue == nil)
    var identity2 = d._rawIdentifier()
    assert(identity1 != identity2)
    assert(isNativeDictionary(d))
    assert(d.count == 4)

    assert((d[TestObjCKeyTy(10)] as! TestObjCValueTy).value == 1010)
    assert((d[TestObjCKeyTy(20)] as! TestObjCValueTy).value == 1020)
    assert((d[TestObjCKeyTy(30)] as! TestObjCValueTy).value == 1030)
    assert((d[TestObjCKeyTy(40)] as! TestObjCValueTy).value == 2040)
  }

  // Overwrite a value in existing binding.
  do {
    var d = getBridgedVerbatimDictionary()
    var identity1 = d._rawIdentifier()
    assert(isCocoaDictionary(d))

    var oldValue: AnyObject? =
        d.updateValue(TestObjCValueTy(2010), forKey: TestObjCKeyTy(10))
    assert((oldValue as! TestObjCValueTy).value == 1010)

    var identity2 = d._rawIdentifier()
    assert(identity1 != identity2)
    assert(isNativeDictionary(d))
    assert(d.count == 3)

    assert((d[TestObjCKeyTy(10)] as! TestObjCValueTy).value == 2010)
    assert((d[TestObjCKeyTy(20)] as! TestObjCValueTy).value == 1020)
    assert((d[TestObjCKeyTy(30)] as! TestObjCValueTy).value == 1030)
  }
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.UpdateValueForKey") {
  // Insert a new key-value pair.
  do {
    var d = getBridgedNonverbatimDictionary()
    var identity1 = d._rawIdentifier()
    assert(isNativeDictionary(d))

    var oldValue =
        d.updateValue(TestBridgedValueTy(2040), forKey: TestBridgedKeyTy(40))
    assert(oldValue == nil)
    var identity2 = d._rawIdentifier()
    assert(identity1 != identity2)
    assert(isNativeDictionary(d))
    assert(d.count == 4)

    assert(d[TestBridgedKeyTy(10)]!.value == 1010)
    assert(d[TestBridgedKeyTy(20)]!.value == 1020)
    assert(d[TestBridgedKeyTy(30)]!.value == 1030)
    assert(d[TestBridgedKeyTy(40)]!.value == 2040)
  }

  // Overwrite a value in existing binding.
  do {
    var d = getBridgedNonverbatimDictionary()
    var identity1 = d._rawIdentifier()
    assert(isNativeDictionary(d))

    var oldValue =
        d.updateValue(TestBridgedValueTy(2010), forKey: TestBridgedKeyTy(10))!
    assert(oldValue.value == 1010)

    var identity2 = d._rawIdentifier()
    assert(identity1 == identity2)
    assert(isNativeDictionary(d))
    assert(d.count == 3)

    assert(d[TestBridgedKeyTy(10)]!.value == 2010)
    assert(d[TestBridgedKeyTy(20)]!.value == 1020)
    assert(d[TestBridgedKeyTy(30)]!.value == 1030)
  }
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.RemoveAt") {
  var d = getBridgedVerbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isCocoaDictionary(d))

  let foundIndex1 = d.index(forKey: TestObjCKeyTy(10))!
  assert(d[foundIndex1].0 == TestObjCKeyTy(10))
  assert((d[foundIndex1].1 as! TestObjCValueTy).value == 1010)
  assert(identity1 == d._rawIdentifier())

  let removedElement = d.remove(at: foundIndex1)
  assert(identity1 != d._rawIdentifier())
  assert(isNativeDictionary(d))
  assert(removedElement.0 == TestObjCKeyTy(10))
  assert((removedElement.1 as! TestObjCValueTy).value == 1010)
  assert(d.count == 2)
  assert(d.index(forKey: TestObjCKeyTy(10)) == nil)
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.RemoveAt") {
  var d = getBridgedNonverbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isNativeDictionary(d))

  let foundIndex1 = d.index(forKey: TestBridgedKeyTy(10))!
  assert(d[foundIndex1].0 == TestBridgedKeyTy(10))
  assert(d[foundIndex1].1.value == 1010)
  assert(identity1 == d._rawIdentifier())

  let removedElement = d.remove(at: foundIndex1)
  assert(identity1 == d._rawIdentifier())
  assert(isNativeDictionary(d))
  assert(removedElement.0 == TestObjCKeyTy(10) as TestBridgedKeyTy)
  assert(removedElement.1.value == 1010)
  assert(d.count == 2)
  assert(d.index(forKey: TestBridgedKeyTy(10)) == nil)
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.RemoveValueForKey") {
  do {
    var d = getBridgedVerbatimDictionary()
    var identity1 = d._rawIdentifier()
    assert(isCocoaDictionary(d))

    var deleted: AnyObject? = d.removeValue(forKey: TestObjCKeyTy(0))
    assert(deleted == nil)
    assert(identity1 == d._rawIdentifier())
    assert(isCocoaDictionary(d))

    deleted = d.removeValue(forKey: TestObjCKeyTy(10))
    assert((deleted as! TestObjCValueTy).value == 1010)
    var identity2 = d._rawIdentifier()
    assert(identity1 != identity2)
    assert(isNativeDictionary(d))
    assert(d.count == 2)

    assert(d[TestObjCKeyTy(10)] == nil)
    assert((d[TestObjCKeyTy(20)] as! TestObjCValueTy).value == 1020)
    assert((d[TestObjCKeyTy(30)] as! TestObjCValueTy).value == 1030)
    assert(identity2 == d._rawIdentifier())
  }

  do {
    var d1 = getBridgedVerbatimDictionary()
    var identity1 = d1._rawIdentifier()

    var d2 = d1
    assert(isCocoaDictionary(d1))
    assert(isCocoaDictionary(d2))

    var deleted: AnyObject? = d2.removeValue(forKey: TestObjCKeyTy(0))
    assert(deleted == nil)
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 == d2._rawIdentifier())
    assert(isCocoaDictionary(d1))
    assert(isCocoaDictionary(d2))

    deleted = d2.removeValue(forKey: TestObjCKeyTy(10))
    assert((deleted as! TestObjCValueTy).value == 1010)
    var identity2 = d2._rawIdentifier()
    assert(identity1 != identity2)
    assert(isCocoaDictionary(d1))
    assert(isNativeDictionary(d2))
    assert(d2.count == 2)

    assert((d1[TestObjCKeyTy(10)] as! TestObjCValueTy).value == 1010)
    assert((d1[TestObjCKeyTy(20)] as! TestObjCValueTy).value == 1020)
    assert((d1[TestObjCKeyTy(30)] as! TestObjCValueTy).value == 1030)
    assert(identity1 == d1._rawIdentifier())

    assert(d2[TestObjCKeyTy(10)] == nil)
    assert((d2[TestObjCKeyTy(20)] as! TestObjCValueTy).value == 1020)
    assert((d2[TestObjCKeyTy(30)] as! TestObjCValueTy).value == 1030)
    assert(identity2 == d2._rawIdentifier())
  }
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.RemoveValueForKey") {
  do {
    var d = getBridgedNonverbatimDictionary()
    var identity1 = d._rawIdentifier()
    assert(isNativeDictionary(d))

    var deleted = d.removeValue(forKey: TestBridgedKeyTy(0))
    assert(deleted == nil)
    assert(identity1 == d._rawIdentifier())
    assert(isNativeDictionary(d))

    deleted = d.removeValue(forKey: TestBridgedKeyTy(10))
    assert(deleted!.value == 1010)
    var identity2 = d._rawIdentifier()
    assert(identity1 == identity2)
    assert(isNativeDictionary(d))
    assert(d.count == 2)

    assert(d[TestBridgedKeyTy(10)] == nil)
    assert(d[TestBridgedKeyTy(20)]!.value == 1020)
    assert(d[TestBridgedKeyTy(30)]!.value == 1030)
    assert(identity2 == d._rawIdentifier())
  }

  do {
    var d1 = getBridgedNonverbatimDictionary()
    var identity1 = d1._rawIdentifier()

    var d2 = d1
    assert(isNativeDictionary(d1))
    assert(isNativeDictionary(d2))

    var deleted = d2.removeValue(forKey: TestBridgedKeyTy(0))
    assert(deleted == nil)
    assert(identity1 == d1._rawIdentifier())
    assert(identity1 == d2._rawIdentifier())
    assert(isNativeDictionary(d1))
    assert(isNativeDictionary(d2))

    deleted = d2.removeValue(forKey: TestBridgedKeyTy(10))
    assert(deleted!.value == 1010)
    var identity2 = d2._rawIdentifier()
    assert(identity1 != identity2)
    assert(isNativeDictionary(d1))
    assert(isNativeDictionary(d2))
    assert(d2.count == 2)

    assert(d1[TestBridgedKeyTy(10)]!.value == 1010)
    assert(d1[TestBridgedKeyTy(20)]!.value == 1020)
    assert(d1[TestBridgedKeyTy(30)]!.value == 1030)
    assert(identity1 == d1._rawIdentifier())

    assert(d2[TestBridgedKeyTy(10)] == nil)
    assert(d2[TestBridgedKeyTy(20)]!.value == 1020)
    assert(d2[TestBridgedKeyTy(30)]!.value == 1030)
    assert(identity2 == d2._rawIdentifier())
  }
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.RemoveAll") {
  do {
    var d = getBridgedVerbatimDictionary([:])
    var identity1 = d._rawIdentifier()
    assert(isCocoaDictionary(d))
    assert(d.count == 0)

    d.removeAll()
    assert(identity1 == d._rawIdentifier())
    assert(d.count == 0)
  }

  do {
    var d = getBridgedVerbatimDictionary()
    var identity1 = d._rawIdentifier()
    assert(isCocoaDictionary(d))
    let originalCapacity = d.count
    assert(d.count == 3)
    assert((d[TestObjCKeyTy(10)] as! TestObjCValueTy).value == 1010)

    d.removeAll()
    assert(identity1 != d._rawIdentifier())
    assert(d._variantStorage.asNative.capacity < originalCapacity)
    assert(d.count == 0)
    assert(d[TestObjCKeyTy(10)] == nil)
  }

  do {
    var d = getBridgedVerbatimDictionary()
    var identity1 = d._rawIdentifier()
    assert(isCocoaDictionary(d))
    let originalCapacity = d.count
    assert(d.count == 3)
    assert((d[TestObjCKeyTy(10)] as! TestObjCValueTy).value == 1010)

    d.removeAll(keepingCapacity: true)
    assert(identity1 != d._rawIdentifier())
    assert(d._variantStorage.asNative.capacity >= originalCapacity)
    assert(d.count == 0)
    assert(d[TestObjCKeyTy(10)] == nil)
  }

  do {
    var d1 = getBridgedVerbatimDictionary()
    var identity1 = d1._rawIdentifier()
    assert(isCocoaDictionary(d1))
    let originalCapacity = d1.count
    assert(d1.count == 3)
    assert((d1[TestObjCKeyTy(10)] as! TestObjCValueTy).value == 1010)

    var d2 = d1
    d2.removeAll()
    var identity2 = d2._rawIdentifier()
    assert(identity1 == d1._rawIdentifier())
    assert(identity2 != identity1)
    assert(d1.count == 3)
    assert((d1[TestObjCKeyTy(10)] as! TestObjCValueTy).value == 1010)
    assert(d2._variantStorage.asNative.capacity < originalCapacity)
    assert(d2.count == 0)
    assert(d2[TestObjCKeyTy(10)] == nil)
  }

  do {
    var d1 = getBridgedVerbatimDictionary()
    var identity1 = d1._rawIdentifier()
    assert(isCocoaDictionary(d1))
    let originalCapacity = d1.count
    assert(d1.count == 3)
    assert((d1[TestObjCKeyTy(10)] as! TestObjCValueTy).value == 1010)

    var d2 = d1
    d2.removeAll(keepingCapacity: true)
    var identity2 = d2._rawIdentifier()
    assert(identity1 == d1._rawIdentifier())
    assert(identity2 != identity1)
    assert(d1.count == 3)
    assert((d1[TestObjCKeyTy(10)] as! TestObjCValueTy).value == 1010)
    assert(d2._variantStorage.asNative.capacity >= originalCapacity)
    assert(d2.count == 0)
    assert(d2[TestObjCKeyTy(10)] == nil)
  }
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.RemoveAll") {
  do {
    var d = getBridgedNonverbatimDictionary([:])
    var identity1 = d._rawIdentifier()
    assert(isNativeDictionary(d))
    assert(d.count == 0)

    d.removeAll()
    assert(identity1 == d._rawIdentifier())
    assert(d.count == 0)
  }

  do {
    var d = getBridgedNonverbatimDictionary()
    var identity1 = d._rawIdentifier()
    assert(isNativeDictionary(d))
    let originalCapacity = d.count
    assert(d.count == 3)
    assert(d[TestBridgedKeyTy(10)]!.value == 1010)

    d.removeAll()
    assert(identity1 != d._rawIdentifier())
    assert(d._variantStorage.asNative.capacity < originalCapacity)
    assert(d.count == 0)
    assert(d[TestBridgedKeyTy(10)] == nil)
  }

  do {
    var d = getBridgedNonverbatimDictionary()
    var identity1 = d._rawIdentifier()
    assert(isNativeDictionary(d))
    let originalCapacity = d.count
    assert(d.count == 3)
    assert(d[TestBridgedKeyTy(10)]!.value == 1010)

    d.removeAll(keepingCapacity: true)
    assert(identity1 == d._rawIdentifier())
    assert(d._variantStorage.asNative.capacity >= originalCapacity)
    assert(d.count == 0)
    assert(d[TestBridgedKeyTy(10)] == nil)
  }

  do {
    var d1 = getBridgedNonverbatimDictionary()
    var identity1 = d1._rawIdentifier()
    assert(isNativeDictionary(d1))
    let originalCapacity = d1.count
    assert(d1.count == 3)
    assert(d1[TestBridgedKeyTy(10)]!.value == 1010)

    var d2 = d1
    d2.removeAll()
    var identity2 = d2._rawIdentifier()
    assert(identity1 == d1._rawIdentifier())
    assert(identity2 != identity1)
    assert(d1.count == 3)
    assert(d1[TestBridgedKeyTy(10)]!.value == 1010)
    assert(d2._variantStorage.asNative.capacity < originalCapacity)
    assert(d2.count == 0)
    assert(d2[TestBridgedKeyTy(10)] == nil)
  }

  do {
    var d1 = getBridgedNonverbatimDictionary()
    var identity1 = d1._rawIdentifier()
    assert(isNativeDictionary(d1))
    let originalCapacity = d1.count
    assert(d1.count == 3)
    assert(d1[TestBridgedKeyTy(10)]!.value == 1010)

    var d2 = d1
    d2.removeAll(keepingCapacity: true)
    var identity2 = d2._rawIdentifier()
    assert(identity1 == d1._rawIdentifier())
    assert(identity2 != identity1)
    assert(d1.count == 3)
    assert(d1[TestBridgedKeyTy(10)]!.value == 1010)
    assert(d2._variantStorage.asNative.capacity >= originalCapacity)
    assert(d2.count == 0)
    assert(d2[TestBridgedKeyTy(10)] == nil)
  }
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.Count") {
  var d = getBridgedVerbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isCocoaDictionary(d))

  assert(d.count == 3)
  assert(identity1 == d._rawIdentifier())
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.Count") {
  var d = getBridgedNonverbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isNativeDictionary(d))

  assert(d.count == 3)
  assert(identity1 == d._rawIdentifier())
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.Generate") {
  var d = getBridgedVerbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isCocoaDictionary(d))

  var iter = d.makeIterator()
  var pairs = Array<(Int, Int)>()
  while let (key, value) = iter.next() {
    let kv = ((key as! TestObjCKeyTy).value, (value as! TestObjCValueTy).value)
    pairs.append(kv)
  }
  assert(equalsUnordered(pairs, [ (10, 1010), (20, 1020), (30, 1030) ]))
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(identity1 == d._rawIdentifier())
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.Generate") {
  var d = getBridgedNonverbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isNativeDictionary(d))

  var iter = d.makeIterator()
  var pairs = Array<(Int, Int)>()
  while let (key, value) = iter.next() {
    let kv = (key.value, value.value)
    pairs.append(kv)
  }
  assert(equalsUnordered(pairs, [ (10, 1010), (20, 1020), (30, 1030) ]))
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(identity1 == d._rawIdentifier())
}

DictionaryTestSuite.test("BridgedFromObjC.Verbatim.Generate_Empty") {
  var d = getBridgedVerbatimDictionary([:])
  var identity1 = d._rawIdentifier()
  assert(isCocoaDictionary(d))

  var iter = d.makeIterator()
  // Cannot write code below because of
  // <rdar://problem/16811736> Optional tuples are broken as optionals regarding == comparison
  // assert(iter.next() == .none)
  assert(iter.next() == nil)
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(identity1 == d._rawIdentifier())
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.Generate_Empty") {
  var d = getBridgedNonverbatimDictionary([:])
  var identity1 = d._rawIdentifier()
  assert(isNativeDictionary(d))

  var iter = d.makeIterator()
  // Cannot write code below because of
  // <rdar://problem/16811736> Optional tuples are broken as optionals regarding == comparison
  // assert(iter.next() == .none)
  assert(iter.next() == nil)
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(identity1 == d._rawIdentifier())
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.Generate_Huge") {
  var d = getHugeBridgedVerbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isCocoaDictionary(d))

  var iter = d.makeIterator()
  var pairs = Array<(Int, Int)>()
  while let (key, value) = iter.next() {
    let kv = ((key as! TestObjCKeyTy).value, (value as! TestObjCValueTy).value)
    pairs.append(kv)
  }
  var expectedPairs = Array<(Int, Int)>()
  for i in 1...32 {
    expectedPairs += [(i, 1000 + i)]
  }
  assert(equalsUnordered(pairs, expectedPairs))
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(identity1 == d._rawIdentifier())
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.Generate_Huge") {
  var d = getHugeBridgedNonverbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isNativeDictionary(d))

  var iter = d.makeIterator()
  var pairs = Array<(Int, Int)>()
  while let (key, value) = iter.next() {
    let kv = (key.value, value.value)
    pairs.append(kv)
  }
  var expectedPairs = Array<(Int, Int)>()
  for i in 1...32 {
    expectedPairs += [(i, 1000 + i)]
  }
  assert(equalsUnordered(pairs, expectedPairs))
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(identity1 == d._rawIdentifier())
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.Generate_ParallelArray") {
autoreleasepoolIfUnoptimizedReturnAutoreleased {
  // Add an autorelease pool because ParallelArrayDictionary autoreleases
  // values in objectForKey.

  var d = getParallelArrayBridgedVerbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isCocoaDictionary(d))

  var iter = d.makeIterator()
  var pairs = Array<(Int, Int)>()
  while let (key, value) = iter.next() {
    let kv = ((key as! TestObjCKeyTy).value, (value as! TestObjCValueTy).value)
    pairs.append(kv)
  }
  var expectedPairs = [ (10, 1111), (20, 1111), (30, 1111), (40, 1111) ]
  assert(equalsUnordered(pairs, expectedPairs))
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(identity1 == d._rawIdentifier())
}
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.Generate_ParallelArray") {
autoreleasepoolIfUnoptimizedReturnAutoreleased {
  // Add an autorelease pool because ParallelArrayDictionary autoreleases
  // values in objectForKey.

  var d = getParallelArrayBridgedNonverbatimDictionary()
  var identity1 = d._rawIdentifier()
  assert(isNativeDictionary(d))

  var iter = d.makeIterator()
  var pairs = Array<(Int, Int)>()
  while let (key, value) = iter.next() {
    let kv = (key.value, value.value)
    pairs.append(kv)
  }
  var expectedPairs = [ (10, 1111), (20, 1111), (30, 1111), (40, 1111) ]
  assert(equalsUnordered(pairs, expectedPairs))
  // The following is not required by the IteratorProtocol protocol, but
  // it is a nice QoI.
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(iter.next() == nil)
  assert(identity1 == d._rawIdentifier())
}
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.EqualityTest_Empty") {
  var d1 = getBridgedVerbatimEquatableDictionary([:])
  var identity1 = d1._rawIdentifier()
  assert(isCocoaDictionary(d1))

  var d2 = getBridgedVerbatimEquatableDictionary([:])
  var identity2 = d2._rawIdentifier()
  assert(isCocoaDictionary(d2))

  // We can't check that `identity1 != identity2` because Foundation might be
  // returning the same singleton NSDictionary for empty dictionaries.

  assert(d1 == d2)
  assert(identity1 == d1._rawIdentifier())
  assert(identity2 == d2._rawIdentifier())

  d2[TestObjCKeyTy(10)] = TestObjCEquatableValueTy(2010)
  assert(isNativeDictionary(d2))
  assert(identity2 != d2._rawIdentifier())
  identity2 = d2._rawIdentifier()

  assert(d1 != d2)
  assert(identity1 == d1._rawIdentifier())
  assert(identity2 == d2._rawIdentifier())
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.EqualityTest_Empty") {
  var d1 = getBridgedNonverbatimEquatableDictionary([:])
  var identity1 = d1._rawIdentifier()
  assert(isNativeDictionary(d1))

  var d2 = getBridgedNonverbatimEquatableDictionary([:])
  var identity2 = d2._rawIdentifier()
  assert(isNativeDictionary(d2))
  assert(identity1 != identity2)

  assert(d1 == d2)
  assert(identity1 == d1._rawIdentifier())
  assert(identity2 == d2._rawIdentifier())

  d2[TestBridgedKeyTy(10)] = TestBridgedEquatableValueTy(2010)
  assert(isNativeDictionary(d2))
  assert(identity2 == d2._rawIdentifier())

  assert(d1 != d2)
  assert(identity1 == d1._rawIdentifier())
  assert(identity2 == d2._rawIdentifier())
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.EqualityTest_Small") {
  func helper(_ nd1: Dictionary<Int, Int>, _ nd2: Dictionary<Int, Int>, _ expectedEq: Bool) {
    let d1 = getBridgedVerbatimEquatableDictionary(nd1)
    let identity1 = d1._rawIdentifier()
    assert(isCocoaDictionary(d1))

    var d2 = getBridgedVerbatimEquatableDictionary(nd2)
    var identity2 = d2._rawIdentifier()
    assert(isCocoaDictionary(d2))

    do {
      let eq1 = (d1 == d2)
      assert(eq1 == expectedEq)

      let eq2 = (d2 == d1)
      assert(eq2 == expectedEq)

      let neq1 = (d1 != d2)
      assert(neq1 != expectedEq)

      let neq2 = (d2 != d1)
      assert(neq2 != expectedEq)
    }
    assert(identity1 == d1._rawIdentifier())
    assert(identity2 == d2._rawIdentifier())

    d2[TestObjCKeyTy(1111)] = TestObjCEquatableValueTy(1111)
    d2[TestObjCKeyTy(1111)] = nil
    assert(isNativeDictionary(d2))
    assert(identity2 != d2._rawIdentifier())
    identity2 = d2._rawIdentifier()

    do {
      let eq1 = (d1 == d2)
      assert(eq1 == expectedEq)

      let eq2 = (d2 == d1)
      assert(eq2 == expectedEq)

      let neq1 = (d1 != d2)
      assert(neq1 != expectedEq)

      let neq2 = (d2 != d1)
      assert(neq2 != expectedEq)
    }
    assert(identity1 == d1._rawIdentifier())
    assert(identity2 == d2._rawIdentifier())
  }

  helper([:], [:], true)

  helper([10: 1010],
         [10: 1010],
         true)

  helper([10: 1010, 20: 1020],
         [10: 1010, 20: 1020],
         true)

  helper([10: 1010, 20: 1020, 30: 1030],
         [10: 1010, 20: 1020, 30: 1030],
         true)

  helper([10: 1010, 20: 1020, 30: 1030],
         [10: 1010, 20: 1020, 1111: 1030],
         false)

  helper([10: 1010, 20: 1020, 30: 1030],
         [10: 1010, 20: 1020, 30: 1111],
         false)

  helper([10: 1010, 20: 1020, 30: 1030],
         [10: 1010, 20: 1020],
         false)

  helper([10: 1010, 20: 1020, 30: 1030],
         [10: 1010],
         false)

  helper([10: 1010, 20: 1020, 30: 1030],
         [:],
         false)

  helper([10: 1010, 20: 1020, 30: 1030],
         [10: 1010, 20: 1020, 30: 1030, 40: 1040],
         false)
}


DictionaryTestSuite.test("BridgedFromObjC.Verbatim.ArrayOfDictionaries") {
  var nsa = NSMutableArray()
  for i in 0..<3 {
    nsa.add(
        getAsNSDictionary([10: 1010 + i, 20: 1020 + i, 30: 1030 + i]))
  }

  var a = nsa as [AnyObject] as! [Dictionary<NSObject, AnyObject>]
  for i in 0..<3 {
    var d = a[i]
    var iter = d.makeIterator()
    var pairs = Array<(Int, Int)>()
    while let (key, value) = iter.next() {
      let kv = ((key as! TestObjCKeyTy).value, (value as! TestObjCValueTy).value)
      pairs.append(kv)
    }
    var expectedPairs = [ (10, 1010 + i), (20, 1020 + i), (30, 1030 + i) ]
    assert(equalsUnordered(pairs, expectedPairs))
  }
}

DictionaryTestSuite.test("BridgedFromObjC.Nonverbatim.ArrayOfDictionaries") {
  var nsa = NSMutableArray()
  for i in 0..<3 {
    nsa.add(
        getAsNSDictionary([10: 1010 + i, 20: 1020 + i, 30: 1030 + i]))
  }

  var a = nsa as [AnyObject] as! [Dictionary<TestBridgedKeyTy, TestBridgedValueTy>]
  for i in 0..<3 {
    var d = a[i]
    var iter = d.makeIterator()
    var pairs = Array<(Int, Int)>()
    while let (key, value) = iter.next() {
      let kv = (key.value, value.value)
      pairs.append(kv)
    }
    var expectedPairs = [ (10, 1010 + i), (20, 1020 + i), (30, 1030 + i) ]
    assert(equalsUnordered(pairs, expectedPairs))
  }
}


//===---
// Dictionary -> NSDictionary bridging tests.
//
// Key and Value are bridged verbatim.
//===---

DictionaryTestSuite.test("BridgedToObjC.Verbatim.Count") {
  let d = getBridgedNSDictionaryOfRefTypesBridgedVerbatim()

  assert(d.count == 3)
}

DictionaryTestSuite.test("BridgedToObjC.Verbatim.ObjectForKey") {
  let d = getBridgedNSDictionaryOfRefTypesBridgedVerbatim()

  var v: AnyObject? = d.object(forKey: TestObjCKeyTy(10)).map { $0 as AnyObject }
  expectEqual(1010, (v as! TestObjCValueTy).value)
  let idValue10 = unsafeBitCast(v, to: UInt.self)

  v = d.object(forKey: TestObjCKeyTy(20)).map { $0 as AnyObject }
  expectEqual(1020, (v as! TestObjCValueTy).value)
  let idValue20 = unsafeBitCast(v, to: UInt.self)

  v = d.object(forKey: TestObjCKeyTy(30)).map { $0 as AnyObject }
  expectEqual(1030, (v as! TestObjCValueTy).value)
  let idValue30 = unsafeBitCast(v, to: UInt.self)

  expectEmpty(d.object(forKey: TestObjCKeyTy(40)))

  // NSDictionary can store mixed key types.  Swift's Dictionary is typed, but
  // when bridged to NSDictionary, it should behave like one, and allow queries
  // for mismatched key types.
  expectEmpty(d.object(forKey: TestObjCInvalidKeyTy()))

  for i in 0..<3 {
    expectEqual(idValue10, unsafeBitCast(
      d.object(forKey: TestObjCKeyTy(10)).map { $0 as AnyObject }, to: UInt.self))

    expectEqual(idValue20, unsafeBitCast(
      d.object(forKey: TestObjCKeyTy(20)).map { $0 as AnyObject }, to: UInt.self))

    expectEqual(idValue30, unsafeBitCast(
      d.object(forKey: TestObjCKeyTy(30)).map { $0 as AnyObject }, to: UInt.self))
  }

  expectAutoreleasedKeysAndValues(unopt: (0, 3))
}

DictionaryTestSuite.test("BridgedToObjC.Verbatim.KeyEnumerator.NextObject") {
  let d = getBridgedNSDictionaryOfRefTypesBridgedVerbatim()

  var capturedIdentityPairs = Array<(UInt, UInt)>()

  for i in 0..<3 {
    let enumerator = d.keyEnumerator()

    var dataPairs = Array<(Int, Int)>()
    var identityPairs = Array<(UInt, UInt)>()
    while let key = enumerator.nextObject() {
      let keyObj = key as AnyObject
      let value: AnyObject = d.object(forKey: keyObj)! as AnyObject

      let dataPair =
        ((keyObj as! TestObjCKeyTy).value, (value as! TestObjCValueTy).value)
      dataPairs.append(dataPair)

      let identityPair =
        (unsafeBitCast(keyObj, to: UInt.self),
         unsafeBitCast(value, to: UInt.self))
      identityPairs.append(identityPair)
    }
    expectTrue(
      equalsUnordered(dataPairs, [ (10, 1010), (20, 1020), (30, 1030) ]))

    if capturedIdentityPairs.isEmpty {
      capturedIdentityPairs = identityPairs
    } else {
      expectTrue(equalsUnordered(capturedIdentityPairs, identityPairs))
    }

    assert(enumerator.nextObject() == nil)
    assert(enumerator.nextObject() == nil)
    assert(enumerator.nextObject() == nil)
  }

  expectAutoreleasedKeysAndValues(unopt: (3, 3))
}

DictionaryTestSuite.test("BridgedToObjC.Verbatim.KeyEnumerator.NextObject_Empty") {
  let d = getBridgedEmptyNSDictionary()
  let enumerator = d.keyEnumerator()

  assert(enumerator.nextObject() == nil)
  assert(enumerator.nextObject() == nil)
  assert(enumerator.nextObject() == nil)
}

DictionaryTestSuite.test("BridgedToObjC.Verbatim.KeyEnumerator.FastEnumeration.UseFromSwift") {
  let d = getBridgedNSDictionaryOfRefTypesBridgedVerbatim()

  checkDictionaryFastEnumerationFromSwift(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    d, { d.keyEnumerator() },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  expectAutoreleasedKeysAndValues(unopt: (3, 3))
}

DictionaryTestSuite.test("BridgedToObjC.Verbatim.KeyEnumerator.FastEnumeration.UseFromObjC") {
  let d = getBridgedNSDictionaryOfRefTypesBridgedVerbatim()

  checkDictionaryFastEnumerationFromObjC(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    d, { d.keyEnumerator() },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  expectAutoreleasedKeysAndValues(unopt: (3, 3))
}

DictionaryTestSuite.test("BridgedToObjC.Verbatim.KeyEnumerator.FastEnumeration_Empty") {
  let d = getBridgedEmptyNSDictionary()

  checkDictionaryFastEnumerationFromSwift(
    [], d, { d.keyEnumerator() },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  checkDictionaryFastEnumerationFromObjC(
    [], d, { d.keyEnumerator() },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })
}

DictionaryTestSuite.test("BridgedToObjC.Verbatim.FastEnumeration.UseFromSwift") {
  let d = getBridgedNSDictionaryOfRefTypesBridgedVerbatim()

  checkDictionaryFastEnumerationFromSwift(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    d, { d },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  expectAutoreleasedKeysAndValues(unopt: (0, 3))
}

DictionaryTestSuite.test("BridgedToObjC.Verbatim.FastEnumeration.UseFromObjC") {
  let d = getBridgedNSDictionaryOfRefTypesBridgedVerbatim()

  checkDictionaryFastEnumerationFromObjC(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    d, { d },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  expectAutoreleasedKeysAndValues(unopt: (0, 3))
}

DictionaryTestSuite.test("BridgedToObjC.Verbatim.FastEnumeration_Empty") {
  let d = getBridgedEmptyNSDictionary()

  checkDictionaryFastEnumerationFromSwift(
    [], d, { d },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  checkDictionaryFastEnumerationFromObjC(
    [], d, { d },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })
}

//===---
// Dictionary -> NSDictionary bridging tests.
//
// Key type and value type are bridged non-verbatim.
//===---

DictionaryTestSuite.test("BridgedToObjC.KeyValue_ValueTypesCustomBridged") {
  let d = getBridgedNSDictionaryOfKeyValue_ValueTypesCustomBridged()
  let enumerator = d.keyEnumerator()

  var pairs = Array<(Int, Int)>()
  while let key = enumerator.nextObject() {
    let value: AnyObject = d.object(forKey: key)! as AnyObject
    let kv = ((key as! TestObjCKeyTy).value, (value as! TestObjCValueTy).value)
    pairs.append(kv)
  }
  assert(equalsUnordered(pairs, [ (10, 1010), (20, 1020), (30, 1030) ]))

  expectAutoreleasedKeysAndValues(unopt: (3, 3))
}

DictionaryTestSuite.test("BridgedToObjC.Custom.KeyEnumerator.FastEnumeration.UseFromSwift") {
  let d = getBridgedNSDictionaryOfKeyValue_ValueTypesCustomBridged()

  checkDictionaryFastEnumerationFromSwift(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    d, { d.keyEnumerator() },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  expectAutoreleasedKeysAndValues(unopt: (3, 3))
}

DictionaryTestSuite.test("BridgedToObjC.Custom.KeyEnumerator.FastEnumeration.UseFromSwift.Partial") {
  let d = getBridgedNSDictionaryOfKeyValue_ValueTypesCustomBridged(
    numElements: 9)

  checkDictionaryEnumeratorPartialFastEnumerationFromSwift(
    [ (10, 1010), (20, 1020), (30, 1030), (40, 1040), (50, 1050),
      (60, 1060), (70, 1070), (80, 1080), (90, 1090) ],
    d, maxFastEnumerationItems: 5,
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  expectAutoreleasedKeysAndValues(unopt: (9, 9))
}

DictionaryTestSuite.test("BridgedToObjC.Custom.KeyEnumerator.FastEnumeration.UseFromObjC") {
  let d = getBridgedNSDictionaryOfKeyValue_ValueTypesCustomBridged()

  checkDictionaryFastEnumerationFromObjC(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    d, { d.keyEnumerator() },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  expectAutoreleasedKeysAndValues(unopt: (3, 3))
}

DictionaryTestSuite.test("BridgedToObjC.Custom.FastEnumeration.UseFromSwift") {
  let d = getBridgedNSDictionaryOfKeyValue_ValueTypesCustomBridged()

  checkDictionaryFastEnumerationFromSwift(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    d, { d },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  expectAutoreleasedKeysAndValues(unopt: (0, 3))
}

DictionaryTestSuite.test("BridgedToObjC.Custom.FastEnumeration.UseFromObjC") {
  let d = getBridgedNSDictionaryOfKeyValue_ValueTypesCustomBridged()

  checkDictionaryFastEnumerationFromObjC(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    d, { d },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  expectAutoreleasedKeysAndValues(unopt: (0, 3))
}

DictionaryTestSuite.test("BridgedToObjC.Custom.FastEnumeration_Empty") {
  let d = getBridgedNSDictionaryOfKeyValue_ValueTypesCustomBridged(
    numElements: 0)

  checkDictionaryFastEnumerationFromSwift(
    [], d, { d },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  checkDictionaryFastEnumerationFromObjC(
    [], d, { d },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })
}

func getBridgedNSDictionaryOfKey_ValueTypeCustomBridged() -> NSDictionary {
  assert(!_isBridgedVerbatimToObjectiveC(TestBridgedKeyTy.self))
  assert(_isBridgedVerbatimToObjectiveC(TestObjCValueTy.self))

  var d = Dictionary<TestBridgedKeyTy, TestObjCValueTy>()
  d[TestBridgedKeyTy(10)] = TestObjCValueTy(1010)
  d[TestBridgedKeyTy(20)] = TestObjCValueTy(1020)
  d[TestBridgedKeyTy(30)] = TestObjCValueTy(1030)

  let bridged = convertDictionaryToNSDictionary(d)
  assert(isNativeNSDictionary(bridged))

  return bridged
}

DictionaryTestSuite.test("BridgedToObjC.Key_ValueTypeCustomBridged") {
  let d = getBridgedNSDictionaryOfKey_ValueTypeCustomBridged()
  let enumerator = d.keyEnumerator()

  var pairs = Array<(Int, Int)>()
  while let key = enumerator.nextObject() {
    let value: AnyObject = d.object(forKey: key)! as AnyObject
    let kv = ((key as! TestObjCKeyTy).value, (value as! TestObjCValueTy).value)
    pairs.append(kv)
  }
  assert(equalsUnordered(pairs, [ (10, 1010), (20, 1020), (30, 1030) ]))

  expectAutoreleasedKeysAndValues(unopt: (3, 3))
}

func getBridgedNSDictionaryOfValue_ValueTypeCustomBridged() -> NSDictionary {
  assert(_isBridgedVerbatimToObjectiveC(TestObjCKeyTy.self))
  assert(!_isBridgedVerbatimToObjectiveC(TestBridgedValueTy.self))

  var d = Dictionary<TestObjCKeyTy, TestBridgedValueTy>()
  d[TestObjCKeyTy(10)] = TestBridgedValueTy(1010)
  d[TestObjCKeyTy(20)] = TestBridgedValueTy(1020)
  d[TestObjCKeyTy(30)] = TestBridgedValueTy(1030)

  let bridged = convertDictionaryToNSDictionary(d)
  assert(isNativeNSDictionary(bridged))

  return bridged
}

DictionaryTestSuite.test("BridgedToObjC.Value_ValueTypeCustomBridged") {
  let d = getBridgedNSDictionaryOfValue_ValueTypeCustomBridged()
  let enumerator = d.keyEnumerator()

  var pairs = Array<(Int, Int)>()
  while let key = enumerator.nextObject() {
    let value: AnyObject = d.object(forKey: key)! as AnyObject
    let kv = ((key as! TestObjCKeyTy).value, (value as! TestObjCValueTy).value)
    pairs.append(kv)
  }
  assert(equalsUnordered(pairs, [ (10, 1010), (20, 1020), (30, 1030) ]))

  expectAutoreleasedKeysAndValues(unopt: (3, 3))
}


//===---
// NSDictionary -> Dictionary -> NSDictionary bridging tests.
//===---

func getRoundtripBridgedNSDictionary() -> NSDictionary {
  let keys = [ 10, 20, 30 ].map { TestObjCKeyTy($0) }
  let values = [ 1010, 1020, 1030 ].map { TestObjCValueTy($0) }

  let nsd = NSDictionary(objects: values, forKeys: keys)

  let d: Dictionary<NSObject, AnyObject> = convertNSDictionaryToDictionary(nsd)

  let bridgedBack = convertDictionaryToNSDictionary(d)
  assert(isCocoaNSDictionary(bridgedBack))
  // FIXME: this should be true.
  //assert(unsafeBitCast(nsd, Int.self) == unsafeBitCast(bridgedBack, Int.self))

  return bridgedBack
}

DictionaryTestSuite.test("BridgingRoundtrip") {
  let d = getRoundtripBridgedNSDictionary()
  let enumerator = d.keyEnumerator()

  var pairs = Array<(key: Int, value: Int)>()
  while let key = enumerator.nextObject() {
    let value: AnyObject = d.object(forKey: key)! as AnyObject
    let kv = ((key as! TestObjCKeyTy).value, (value as! TestObjCValueTy).value)
    pairs.append(kv)
  }
  expectEqualsUnordered([ (10, 1010), (20, 1020), (30, 1030) ], pairs)
}

//===---
// NSDictionary -> Dictionary implicit conversion.
//===---

DictionaryTestSuite.test("NSDictionaryToDictionaryConversion") {
  let keys = [ 10, 20, 30 ].map { TestObjCKeyTy($0) }
  let values = [ 1010, 1020, 1030 ].map { TestObjCValueTy($0) }

  let nsd = NSDictionary(objects: values, forKeys: keys)

  let d: Dictionary = nsd as Dictionary

  var pairs = Array<(Int, Int)>()
  for (key, value) in d {
    let kv = ((key as! TestObjCKeyTy).value, (value as! TestObjCValueTy).value)
    pairs.append(kv)
  }
  assert(equalsUnordered(pairs, [ (10, 1010), (20, 1020), (30, 1030) ]))
}

DictionaryTestSuite.test("DictionaryToNSDictionaryConversion") {
  var d = Dictionary<TestObjCKeyTy, TestObjCValueTy>(minimumCapacity: 32)
  d[TestObjCKeyTy(10)] = TestObjCValueTy(1010)
  d[TestObjCKeyTy(20)] = TestObjCValueTy(1020)
  d[TestObjCKeyTy(30)] = TestObjCValueTy(1030)
  let nsd: NSDictionary = d as NSDictionary

  checkDictionaryFastEnumerationFromSwift(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    d as NSDictionary, { d as NSDictionary },
    { ($0 as! TestObjCKeyTy).value },
    { ($0 as! TestObjCValueTy).value })

  expectAutoreleasedKeysAndValues(unopt: (0, 3))
}

//===---
// Dictionary upcasts
//===---

DictionaryTestSuite.test("DictionaryUpcastEntryPoint") {
  var d = Dictionary<TestObjCKeyTy, TestObjCValueTy>(minimumCapacity: 32)
  d[TestObjCKeyTy(10)] = TestObjCValueTy(1010)
  d[TestObjCKeyTy(20)] = TestObjCValueTy(1020)
  d[TestObjCKeyTy(30)] = TestObjCValueTy(1030)

  var dAsAnyObject: Dictionary<NSObject, AnyObject> = _dictionaryUpCast(d)

  assert(dAsAnyObject.count == 3)
  var v: AnyObject? = dAsAnyObject[TestObjCKeyTy(10)]
  assert((v! as! TestObjCValueTy).value == 1010)

  v = dAsAnyObject[TestObjCKeyTy(20)]
  assert((v! as! TestObjCValueTy).value == 1020)

  v = dAsAnyObject[TestObjCKeyTy(30)]
  assert((v! as! TestObjCValueTy).value == 1030)
}

DictionaryTestSuite.test("DictionaryUpcast") {
  var d = Dictionary<TestObjCKeyTy, TestObjCValueTy>(minimumCapacity: 32)
  d[TestObjCKeyTy(10)] = TestObjCValueTy(1010)
  d[TestObjCKeyTy(20)] = TestObjCValueTy(1020)
  d[TestObjCKeyTy(30)] = TestObjCValueTy(1030)

  var dAsAnyObject: Dictionary<NSObject, AnyObject> = d

  assert(dAsAnyObject.count == 3)
  var v: AnyObject? = dAsAnyObject[TestObjCKeyTy(10)]
  assert((v! as! TestObjCValueTy).value == 1010)

  v = dAsAnyObject[TestObjCKeyTy(20)]
  assert((v! as! TestObjCValueTy).value == 1020)

  v = dAsAnyObject[TestObjCKeyTy(30)]
  assert((v! as! TestObjCValueTy).value == 1030)
}

DictionaryTestSuite.test("DictionaryUpcastBridgedEntryPoint") {
  var d = Dictionary<TestBridgedKeyTy, TestBridgedValueTy>(minimumCapacity: 32)
  d[TestBridgedKeyTy(10)] = TestBridgedValueTy(1010)
  d[TestBridgedKeyTy(20)] = TestBridgedValueTy(1020)
  d[TestBridgedKeyTy(30)] = TestBridgedValueTy(1030)

  do {
    var dOO: Dictionary<NSObject, AnyObject> = _dictionaryBridgeToObjectiveC(d)

    assert(dOO.count == 3)
    var v: AnyObject? = dOO[TestObjCKeyTy(10)]
    assert((v! as! TestBridgedValueTy).value == 1010)

    v = dOO[TestObjCKeyTy(20)]
    assert((v! as! TestBridgedValueTy).value == 1020)

    v = dOO[TestObjCKeyTy(30)]
    assert((v! as! TestBridgedValueTy).value == 1030)
  }

  do {
    var dOV: Dictionary<NSObject, TestBridgedValueTy>
      = _dictionaryBridgeToObjectiveC(d)

    assert(dOV.count == 3)
    var v = dOV[TestObjCKeyTy(10)]
    assert(v!.value == 1010)

    v = dOV[TestObjCKeyTy(20)]
    assert(v!.value == 1020)

    v = dOV[TestObjCKeyTy(30)]
    assert(v!.value == 1030)
  }

  do {
    var dVO: Dictionary<TestBridgedKeyTy, AnyObject>
      = _dictionaryBridgeToObjectiveC(d)

    assert(dVO.count == 3)
    var v: AnyObject? = dVO[TestBridgedKeyTy(10)]
    assert((v! as! TestBridgedValueTy).value == 1010)

    v = dVO[TestBridgedKeyTy(20)]
    assert((v! as! TestBridgedValueTy).value == 1020)

    v = dVO[TestBridgedKeyTy(30)]
    assert((v! as! TestBridgedValueTy).value == 1030)
  }
}

DictionaryTestSuite.test("DictionaryUpcastBridged") {
  var d = Dictionary<TestBridgedKeyTy, TestBridgedValueTy>(minimumCapacity: 32)
  d[TestBridgedKeyTy(10)] = TestBridgedValueTy(1010)
  d[TestBridgedKeyTy(20)] = TestBridgedValueTy(1020)
  d[TestBridgedKeyTy(30)] = TestBridgedValueTy(1030)

  do {
    var dOO = d as Dictionary<NSObject, AnyObject>

    assert(dOO.count == 3)
    var v: AnyObject? = dOO[TestObjCKeyTy(10)]
    assert((v! as! TestBridgedValueTy).value == 1010)

    v = dOO[TestObjCKeyTy(20)]
    assert((v! as! TestBridgedValueTy).value == 1020)

    v = dOO[TestObjCKeyTy(30)]
    assert((v! as! TestBridgedValueTy).value == 1030)
  }

  do {
    var dOV = d as Dictionary<NSObject, TestBridgedValueTy>

    assert(dOV.count == 3)
    var v = dOV[TestObjCKeyTy(10)]
    assert(v!.value == 1010)

    v = dOV[TestObjCKeyTy(20)]
    assert(v!.value == 1020)

    v = dOV[TestObjCKeyTy(30)]
    assert(v!.value == 1030)
  }

  do {
    var dVO = d as Dictionary<TestBridgedKeyTy, AnyObject>

    assert(dVO.count == 3)
    var v: AnyObject? = dVO[TestBridgedKeyTy(10)]
    assert((v! as! TestBridgedValueTy).value == 1010)

    v = dVO[TestBridgedKeyTy(20)]
    assert((v! as! TestBridgedValueTy).value == 1020)

    v = dVO[TestBridgedKeyTy(30)]
    assert((v! as! TestBridgedValueTy).value == 1030)
  }
}

//===---
// Dictionary downcasts
//===---

DictionaryTestSuite.test("DictionaryDowncastEntryPoint") {
  var d = Dictionary<NSObject, AnyObject>(minimumCapacity: 32)
  d[TestObjCKeyTy(10)] = TestObjCValueTy(1010)
  d[TestObjCKeyTy(20)] = TestObjCValueTy(1020)
  d[TestObjCKeyTy(30)] = TestObjCValueTy(1030)

  // Successful downcast.
  let dCC: Dictionary<TestObjCKeyTy, TestObjCValueTy> = _dictionaryDownCast(d)
  assert(dCC.count == 3)
  var v = dCC[TestObjCKeyTy(10)]
  assert(v!.value == 1010)

  v = dCC[TestObjCKeyTy(20)]
  assert(v!.value == 1020)

  v = dCC[TestObjCKeyTy(30)]
  assert(v!.value == 1030)

  expectAutoreleasedKeysAndValues(unopt: (0, 3))
}

DictionaryTestSuite.test("DictionaryDowncast") {
  var d = Dictionary<NSObject, AnyObject>(minimumCapacity: 32)
  d[TestObjCKeyTy(10)] = TestObjCValueTy(1010)
  d[TestObjCKeyTy(20)] = TestObjCValueTy(1020)
  d[TestObjCKeyTy(30)] = TestObjCValueTy(1030)

  // Successful downcast.
  let dCC = d as! Dictionary<TestObjCKeyTy, TestObjCValueTy>
  assert(dCC.count == 3)
  var v = dCC[TestObjCKeyTy(10)]
  assert(v!.value == 1010)

  v = dCC[TestObjCKeyTy(20)]
  assert(v!.value == 1020)

  v = dCC[TestObjCKeyTy(30)]
  assert(v!.value == 1030)

  expectAutoreleasedKeysAndValues(unopt: (0, 3))
}

DictionaryTestSuite.test("DictionaryDowncastConditionalEntryPoint") {
  var d = Dictionary<NSObject, AnyObject>(minimumCapacity: 32)
  d[TestObjCKeyTy(10)] = TestObjCValueTy(1010)
  d[TestObjCKeyTy(20)] = TestObjCValueTy(1020)
  d[TestObjCKeyTy(30)] = TestObjCValueTy(1030)

  // Successful downcast.
  if let dCC
       = _dictionaryDownCastConditional(d) as Dictionary<TestObjCKeyTy, TestObjCValueTy>? {
    assert(dCC.count == 3)
    var v = dCC[TestObjCKeyTy(10)]
    assert(v!.value == 1010)

    v = dCC[TestObjCKeyTy(20)]
    assert(v!.value == 1020)

    v = dCC[TestObjCKeyTy(30)]
    assert(v!.value == 1030)
  } else {
    assert(false)
  }

  // Unsuccessful downcast
  d["hello" as NSString] = 17 as NSNumber
  if let dCC
       = _dictionaryDownCastConditional(d) as Dictionary<TestObjCKeyTy, TestObjCValueTy>? {
    assert(false)
  }
}

DictionaryTestSuite.test("DictionaryDowncastConditional") {
  var d = Dictionary<NSObject, AnyObject>(minimumCapacity: 32)
  d[TestObjCKeyTy(10)] = TestObjCValueTy(1010)
  d[TestObjCKeyTy(20)] = TestObjCValueTy(1020)
  d[TestObjCKeyTy(30)] = TestObjCValueTy(1030)

  // Successful downcast.
  if let dCC = d as? Dictionary<TestObjCKeyTy, TestObjCValueTy> {
    assert(dCC.count == 3)
    var v = dCC[TestObjCKeyTy(10)]
    assert(v!.value == 1010)

    v = dCC[TestObjCKeyTy(20)]
    assert(v!.value == 1020)

    v = dCC[TestObjCKeyTy(30)]
    assert(v!.value == 1030)
  } else {
    assert(false)
  }

  // Unsuccessful downcast
  d["hello" as NSString] = 17 as NSNumber
  if let dCC = d as? Dictionary<TestObjCKeyTy, TestObjCValueTy> {
    assert(false)
  }
}

DictionaryTestSuite.test("DictionaryBridgeFromObjectiveCEntryPoint") {
  var d = Dictionary<NSObject, AnyObject>(minimumCapacity: 32)
  d[TestObjCKeyTy(10)] = TestObjCValueTy(1010)
  d[TestObjCKeyTy(20)] = TestObjCValueTy(1020)
  d[TestObjCKeyTy(30)] = TestObjCValueTy(1030)

  // Successful downcast.
  let dCV: Dictionary<TestObjCKeyTy, TestBridgedValueTy>
    = _dictionaryBridgeFromObjectiveC(d)
  do {
    assert(dCV.count == 3)
    var v = dCV[TestObjCKeyTy(10)]
    assert(v!.value == 1010)

    v = dCV[TestObjCKeyTy(20)]
    assert(v!.value == 1020)

    v = dCV[TestObjCKeyTy(30)]
    assert(v!.value == 1030)
  }

  // Successful downcast.
  let dVC: Dictionary<TestBridgedKeyTy, TestObjCValueTy>
    = _dictionaryBridgeFromObjectiveC(d)
  do {
    assert(dVC.count == 3)
    var v = dVC[TestBridgedKeyTy(10)]
    assert(v!.value == 1010)

    v = dVC[TestBridgedKeyTy(20)]
    assert(v!.value == 1020)

    v = dVC[TestBridgedKeyTy(30)]
    assert(v!.value == 1030)
  }

  // Successful downcast.
  let dVV: Dictionary<TestBridgedKeyTy, TestBridgedValueTy>
        = _dictionaryBridgeFromObjectiveC(d)
  do {
    assert(dVV.count == 3)
    var v = dVV[TestBridgedKeyTy(10)]
    assert(v!.value == 1010)

    v = dVV[TestBridgedKeyTy(20)]
    assert(v!.value == 1020)

    v = dVV[TestBridgedKeyTy(30)]
    assert(v!.value == 1030)
  }
}

DictionaryTestSuite.test("DictionaryBridgeFromObjectiveC") {
  var d = Dictionary<NSObject, AnyObject>(minimumCapacity: 32)
  d[TestObjCKeyTy(10)] = TestObjCValueTy(1010)
  d[TestObjCKeyTy(20)] = TestObjCValueTy(1020)
  d[TestObjCKeyTy(30)] = TestObjCValueTy(1030)

  // Successful downcast.
  let dCV = d as! Dictionary<TestObjCKeyTy, TestBridgedValueTy>
  do {
    assert(dCV.count == 3)
    var v = dCV[TestObjCKeyTy(10)]
    assert(v!.value == 1010)

    v = dCV[TestObjCKeyTy(20)]
    assert(v!.value == 1020)

    v = dCV[TestObjCKeyTy(30)]
    assert(v!.value == 1030)
  }

  // Successful downcast.
  let dVC = d as! Dictionary<TestBridgedKeyTy, TestObjCValueTy>
  do {
    assert(dVC.count == 3)
    var v = dVC[TestBridgedKeyTy(10)]
    assert(v!.value == 1010)

    v = dVC[TestBridgedKeyTy(20)]
    assert(v!.value == 1020)

    v = dVC[TestBridgedKeyTy(30)]
    assert(v!.value == 1030)
  }

  // Successful downcast.
  let dVV = d as! Dictionary<TestBridgedKeyTy, TestBridgedValueTy>
  do {
    assert(dVV.count == 3)
    var v = dVV[TestBridgedKeyTy(10)]
    assert(v!.value == 1010)

    v = dVV[TestBridgedKeyTy(20)]
    assert(v!.value == 1020)

    v = dVV[TestBridgedKeyTy(30)]
    assert(v!.value == 1030)
  }
}

DictionaryTestSuite.test("DictionaryBridgeFromObjectiveCConditionalEntryPoint") {
  var d = Dictionary<NSObject, AnyObject>(minimumCapacity: 32)
  d[TestObjCKeyTy(10)] = TestObjCValueTy(1010)
  d[TestObjCKeyTy(20)] = TestObjCValueTy(1020)
  d[TestObjCKeyTy(30)] = TestObjCValueTy(1030)

  // Successful downcast.
  if let dCV
       = _dictionaryBridgeFromObjectiveCConditional(d) as
         Dictionary<TestObjCKeyTy, TestBridgedValueTy>? {
    assert(dCV.count == 3)
    var v = dCV[TestObjCKeyTy(10)]
    assert(v!.value == 1010)

    v = dCV[TestObjCKeyTy(20)]
    assert(v!.value == 1020)

    v = dCV[TestObjCKeyTy(30)]
    assert(v!.value == 1030)
  } else {
    assert(false)
  }

  // Successful downcast.
  if let dVC
       = _dictionaryBridgeFromObjectiveCConditional(d) as Dictionary<TestBridgedKeyTy, TestObjCValueTy>? {
    assert(dVC.count == 3)
    var v = dVC[TestBridgedKeyTy(10)]
    assert(v!.value == 1010)

    v = dVC[TestBridgedKeyTy(20)]
    assert(v!.value == 1020)

    v = dVC[TestBridgedKeyTy(30)]
    assert(v!.value == 1030)
  } else {
    assert(false)
  }

  // Successful downcast.
  if let dVV
       = _dictionaryBridgeFromObjectiveCConditional(d) as Dictionary<TestBridgedKeyTy, TestBridgedValueTy>? {
    assert(dVV.count == 3)
    var v = dVV[TestBridgedKeyTy(10)]
    assert(v!.value == 1010)

    v = dVV[TestBridgedKeyTy(20)]
    assert(v!.value == 1020)

    v = dVV[TestBridgedKeyTy(30)]
    assert(v!.value == 1030)
  } else {
    assert(false)
  }

  // Unsuccessful downcasts
  d["hello" as NSString] = 17 as NSNumber
  if let dCV
       = _dictionaryBridgeFromObjectiveCConditional(d) as Dictionary<TestObjCKeyTy, TestBridgedValueTy>?{
    assert(false)
  }
  if let dVC
       = _dictionaryBridgeFromObjectiveCConditional(d) as Dictionary<TestBridgedKeyTy, TestObjCValueTy>?{
    assert(false)
  }
  if let dVV
       = _dictionaryBridgeFromObjectiveCConditional(d) as Dictionary<TestBridgedKeyTy, TestBridgedValueTy>?{
    assert(false)
  }
}

DictionaryTestSuite.test("DictionaryBridgeFromObjectiveCConditional") {
  var d = Dictionary<NSObject, AnyObject>(minimumCapacity: 32)
  d[TestObjCKeyTy(10)] = TestObjCValueTy(1010)
  d[TestObjCKeyTy(20)] = TestObjCValueTy(1020)
  d[TestObjCKeyTy(30)] = TestObjCValueTy(1030)

  // Successful downcast.
  if let dCV = d as? Dictionary<TestObjCKeyTy, TestBridgedValueTy> {
    assert(dCV.count == 3)
    var v = dCV[TestObjCKeyTy(10)]
    assert(v!.value == 1010)

    v = dCV[TestObjCKeyTy(20)]
    assert(v!.value == 1020)

    v = dCV[TestObjCKeyTy(30)]
    assert(v!.value == 1030)
  } else {
    assert(false)
  }

  // Successful downcast.
  if let dVC = d as? Dictionary<TestBridgedKeyTy, TestObjCValueTy> {
    assert(dVC.count == 3)
    var v = dVC[TestBridgedKeyTy(10)]
    assert(v!.value == 1010)

    v = dVC[TestBridgedKeyTy(20)]
    assert(v!.value == 1020)

    v = dVC[TestBridgedKeyTy(30)]
    assert(v!.value == 1030)
  } else {
    assert(false)
  }

  // Successful downcast.
  if let dVV = d as? Dictionary<TestBridgedKeyTy, TestBridgedValueTy> {
    assert(dVV.count == 3)
    var v = dVV[TestBridgedKeyTy(10)]
    assert(v!.value == 1010)

    v = dVV[TestBridgedKeyTy(20)]
    assert(v!.value == 1020)

    v = dVV[TestBridgedKeyTy(30)]
    assert(v!.value == 1030)
  } else {
    assert(false)
  }

  // Unsuccessful downcasts
  d["hello" as NSString] = 17 as NSNumber
  if let dCV = d as? Dictionary<TestObjCKeyTy, TestBridgedValueTy> {
    assert(false)
  }
  if let dVC = d as? Dictionary<TestBridgedKeyTy, TestObjCValueTy> {
    assert(false)
  }
  if let dVV = d as? Dictionary<TestBridgedKeyTy, TestBridgedValueTy> {
    assert(false)
  }
}
#endif // _runtime(_ObjC)

//===---
// Tests for APIs implemented strictly based on public interface.  We only need
// to test them once, not for every storage type.
//===---

func getDerivedAPIsDictionary() -> Dictionary<Int, Int> {
  var d = Dictionary<Int, Int>(minimumCapacity: 10)
  d[10] = 1010
  d[20] = 1020
  d[30] = 1030
  return d
}

var DictionaryDerivedAPIs = TestSuite("DictionaryDerivedAPIs")

DictionaryDerivedAPIs.test("isEmpty") {
  do {
    var empty = Dictionary<Int, Int>()
    expectTrue(empty.isEmpty)
  }
  do {
    var d = getDerivedAPIsDictionary()
    expectFalse(d.isEmpty)
  }
}

#if _runtime(_ObjC)
@objc
class MockDictionaryWithCustomCount : NSDictionary {
  init(count: Int) {
    self._count = count
    super.init()
  }

  override init() {
    expectUnreachable()
    super.init()
  }

  override init(
    objects: UnsafePointer<AnyObject>,
    forKeys keys: UnsafePointer<NSCopying>,
    count: Int) {
    expectUnreachable()
    super.init(objects: objects, forKeys: keys, count: count)
  }

  required init(coder aDecoder: NSCoder) {
    fatalError("init(coder:) not implemented by MockDictionaryWithCustomCount")
  }

  @objc(copyWithZone:)
  override func copy(with zone: NSZone?) -> Any {
    // Ensure that copying this dictionary produces an object of the same
    // dynamic type.
    return self
  }

  override func object(forKey aKey: Any) -> Any? {
    expectUnreachable()
    return NSObject()
  }

  override var count: Int {
    MockDictionaryWithCustomCount.timesCountWasCalled += 1
    return _count
  }

  var _count: Int = 0

  static var timesCountWasCalled = 0
}

func getMockDictionaryWithCustomCount(count: Int)
  -> Dictionary<NSObject, AnyObject> {

  return MockDictionaryWithCustomCount(count: count) as Dictionary
}

func callGenericIsEmpty<C : Collection>(_ collection: C) -> Bool {
  return collection.isEmpty
}

DictionaryDerivedAPIs.test("isEmpty/ImplementationIsCustomized") {
  do {
    var d = getMockDictionaryWithCustomCount(count: 0)
    MockDictionaryWithCustomCount.timesCountWasCalled = 0
    expectTrue(d.isEmpty)
    expectEqual(1, MockDictionaryWithCustomCount.timesCountWasCalled)
  }
  do {
    var d = getMockDictionaryWithCustomCount(count: 0)
    MockDictionaryWithCustomCount.timesCountWasCalled = 0
    expectTrue(callGenericIsEmpty(d))
    expectEqual(1, MockDictionaryWithCustomCount.timesCountWasCalled)
  }

  do {
    var d = getMockDictionaryWithCustomCount(count: 4)
    MockDictionaryWithCustomCount.timesCountWasCalled = 0
    expectFalse(d.isEmpty)
    expectEqual(1, MockDictionaryWithCustomCount.timesCountWasCalled)
  }
  do {
    var d = getMockDictionaryWithCustomCount(count: 4)
    MockDictionaryWithCustomCount.timesCountWasCalled = 0
    expectFalse(callGenericIsEmpty(d))
    expectEqual(1, MockDictionaryWithCustomCount.timesCountWasCalled)
  }
}
#endif // _runtime(_ObjC)

DictionaryDerivedAPIs.test("keys") {
  do {
    var empty = Dictionary<Int, Int>()
    var keys = Array(empty.keys)
    expectTrue(equalsUnordered(keys, []))
  }
  do {
    var d = getDerivedAPIsDictionary()
    var keys = Array(d.keys)
    expectTrue(equalsUnordered(keys, [ 10, 20, 30 ]))
  }
}

DictionaryDerivedAPIs.test("values") {
  do {
    var empty = Dictionary<Int, Int>()
    var values = Array(empty.values)
    expectTrue(equalsUnordered(values, []))
  }
  do {
    var d = getDerivedAPIsDictionary()

    var values = Array(d.values)
    expectTrue(equalsUnordered(values, [ 1010, 1020, 1030 ]))

    d[11] = 1010
    values = Array(d.values)
    expectTrue(equalsUnordered(values, [ 1010, 1010, 1020, 1030 ]))
  }
}

#if _runtime(_ObjC)
var ObjCThunks = TestSuite("ObjCThunks")

class ObjCThunksHelper : NSObject {
  dynamic func acceptArrayBridgedVerbatim(_ array: [TestObjCValueTy]) {
    expectEqual(10, array[0].value)
    expectEqual(20, array[1].value)
    expectEqual(30, array[2].value)
  }

  dynamic func acceptArrayBridgedNonverbatim(_ array: [TestBridgedValueTy]) {
    // Cannot check elements because doing so would bridge them.
    expectEqual(3, array.count)
  }

  dynamic func returnArrayBridgedVerbatim() -> [TestObjCValueTy] {
    return [ TestObjCValueTy(10), TestObjCValueTy(20),
        TestObjCValueTy(30) ]
  }

  dynamic func returnArrayBridgedNonverbatim() -> [TestBridgedValueTy] {
    return [ TestBridgedValueTy(10), TestBridgedValueTy(20),
        TestBridgedValueTy(30) ]
  }

  dynamic func acceptDictionaryBridgedVerbatim(
      _ d: [TestObjCKeyTy : TestObjCValueTy]) {
    expectEqual(3, d.count)
    expectEqual(1010, d[TestObjCKeyTy(10)]!.value)
    expectEqual(1020, d[TestObjCKeyTy(20)]!.value)
    expectEqual(1030, d[TestObjCKeyTy(30)]!.value)
  }

  dynamic func acceptDictionaryBridgedNonverbatim(
      _ d: [TestBridgedKeyTy : TestBridgedValueTy]) {
    expectEqual(3, d.count)
    // Cannot check elements because doing so would bridge them.
  }

  dynamic func returnDictionaryBridgedVerbatim() ->
      [TestObjCKeyTy : TestObjCValueTy] {
    return [
        TestObjCKeyTy(10): TestObjCValueTy(1010),
        TestObjCKeyTy(20): TestObjCValueTy(1020),
        TestObjCKeyTy(30): TestObjCValueTy(1030),
    ]
  }

  dynamic func returnDictionaryBridgedNonverbatim() ->
      [TestBridgedKeyTy : TestBridgedValueTy] {
    return [
        TestBridgedKeyTy(10): TestBridgedValueTy(1010),
        TestBridgedKeyTy(20): TestBridgedValueTy(1020),
        TestBridgedKeyTy(30): TestBridgedValueTy(1030),
    ]
  }
}

ObjCThunks.test("Array/Accept") {
  var helper = ObjCThunksHelper()

  do {
    helper.acceptArrayBridgedVerbatim(
        [ TestObjCValueTy(10), TestObjCValueTy(20), TestObjCValueTy(30) ])
  }
  do {
    TestBridgedValueTy.bridgeOperations = 0
    helper.acceptArrayBridgedNonverbatim(
        [ TestBridgedValueTy(10), TestBridgedValueTy(20),
          TestBridgedValueTy(30) ])
    expectEqual(0, TestBridgedValueTy.bridgeOperations)
  }
}

ObjCThunks.test("Array/Return") {
  var helper = ObjCThunksHelper()

  do {
    let a = helper.returnArrayBridgedVerbatim()
    expectEqual(10, a[0].value)
    expectEqual(20, a[1].value)
    expectEqual(30, a[2].value)
  }
  do {
    TestBridgedValueTy.bridgeOperations = 0
    let a = helper.returnArrayBridgedNonverbatim()
    expectEqual(0, TestBridgedValueTy.bridgeOperations)

    TestBridgedValueTy.bridgeOperations = 0
    expectEqual(10, a[0].value)
    expectEqual(20, a[1].value)
    expectEqual(30, a[2].value)
    expectEqual(0, TestBridgedValueTy.bridgeOperations)
  }
}

ObjCThunks.test("Dictionary/Accept") {
  var helper = ObjCThunksHelper()

  do {
    helper.acceptDictionaryBridgedVerbatim(
        [ TestObjCKeyTy(10): TestObjCValueTy(1010),
          TestObjCKeyTy(20): TestObjCValueTy(1020),
          TestObjCKeyTy(30): TestObjCValueTy(1030) ])
  }
  do {
    TestBridgedKeyTy.bridgeOperations = 0
    TestBridgedValueTy.bridgeOperations = 0
    helper.acceptDictionaryBridgedNonverbatim(
        [ TestBridgedKeyTy(10): TestBridgedValueTy(1010),
          TestBridgedKeyTy(20): TestBridgedValueTy(1020),
          TestBridgedKeyTy(30): TestBridgedValueTy(1030) ])
    expectEqual(0, TestBridgedKeyTy.bridgeOperations)
    expectEqual(0, TestBridgedValueTy.bridgeOperations)
  }
}

ObjCThunks.test("Dictionary/Return") {
  var helper = ObjCThunksHelper()

  do {
    let d = helper.returnDictionaryBridgedVerbatim()
    expectEqual(3, d.count)
    expectEqual(1010, d[TestObjCKeyTy(10)]!.value)
    expectEqual(1020, d[TestObjCKeyTy(20)]!.value)
    expectEqual(1030, d[TestObjCKeyTy(30)]!.value)
  }
  do {
    TestBridgedKeyTy.bridgeOperations = 0
    TestBridgedValueTy.bridgeOperations = 0
    let d = helper.returnDictionaryBridgedNonverbatim()
    expectEqual(0, TestBridgedKeyTy.bridgeOperations)
    expectEqual(0, TestBridgedValueTy.bridgeOperations)

    TestBridgedKeyTy.bridgeOperations = 0
    TestBridgedValueTy.bridgeOperations = 0
    expectEqual(3, d.count)
    expectEqual(1010, d[TestBridgedKeyTy(10)]!.value)
    expectEqual(1020, d[TestBridgedKeyTy(20)]!.value)
    expectEqual(1030, d[TestBridgedKeyTy(30)]!.value)
    expectEqual(0, TestBridgedKeyTy.bridgeOperations)
    expectEqual(0, TestBridgedValueTy.bridgeOperations)
  }
}
#endif // _runtime(_ObjC)

//===---
// Check that iterators traverse a snapshot of the collection.
//===---

DictionaryTestSuite.test("mutationDoesNotAffectIterator/subscript/store") {
  var dict = getDerivedAPIsDictionary()
  var iter = dict.makeIterator()
  dict[10] = 1011

  expectEqualsUnordered(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    Array(IteratorSequence(iter)))
}

DictionaryTestSuite.test("mutationDoesNotAffectIterator/removeValueForKey,1") {
  var dict = getDerivedAPIsDictionary()
  var iter = dict.makeIterator()
  expectOptionalEqual(1010, dict.removeValue(forKey: 10))

  expectEqualsUnordered(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    Array(IteratorSequence(iter)))
}

DictionaryTestSuite.test("mutationDoesNotAffectIterator/removeValueForKey,all") {
  var dict = getDerivedAPIsDictionary()
  var iter = dict.makeIterator()
  expectOptionalEqual(1010, dict.removeValue(forKey: 10))
  expectOptionalEqual(1020, dict.removeValue(forKey: 20))
  expectOptionalEqual(1030, dict.removeValue(forKey: 30))

  expectEqualsUnordered(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    Array(IteratorSequence(iter)))
}

DictionaryTestSuite.test(
  "mutationDoesNotAffectIterator/removeAll,keepingCapacity=false") {
  var dict = getDerivedAPIsDictionary()
  var iter = dict.makeIterator()
  dict.removeAll(keepingCapacity: false)

  expectEqualsUnordered(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    Array(IteratorSequence(iter)))
}

DictionaryTestSuite.test(
  "mutationDoesNotAffectIterator/removeAll,keepingCapacity=true") {
  var dict = getDerivedAPIsDictionary()
  var iter = dict.makeIterator()
  dict.removeAll(keepingCapacity: true)

  expectEqualsUnordered(
    [ (10, 1010), (20, 1020), (30, 1030) ],
    Array(IteratorSequence(iter)))
}

//===---
// Misc tests.
//===---

DictionaryTestSuite.test("misc") {
  do {
    // Dictionary literal
    var dict = ["Hello": 1, "World": 2]

    // Insertion
    dict["Swift"] = 3

    // Access
    expectOptionalEqual(1, dict["Hello"])
    expectOptionalEqual(2, dict["World"])
    expectOptionalEqual(3, dict["Swift"])
    expectEmpty(dict["Universe"])

    // Overwriting existing value
    dict["Hello"] = 0
    expectOptionalEqual(0, dict["Hello"])
    expectOptionalEqual(2, dict["World"])
    expectOptionalEqual(3, dict["Swift"])
    expectEmpty(dict["Universe"])
  }

  do {
    // Dictionaries with other types
    var d = [ 1.2: 1, 2.6: 2 ]
    d[3.3] = 3
    expectOptionalEqual(1, d[1.2])
    expectOptionalEqual(2, d[2.6])
    expectOptionalEqual(3, d[3.3])
  }

  do {
    var d = Dictionary<String, Int>(minimumCapacity: 13)
    d["one"] = 1
    d["two"] = 2
    d["three"] = 3
    d["four"] = 4
    d["five"] = 5
    expectOptionalEqual(1, d["one"])
    expectOptionalEqual(2, d["two"])
    expectOptionalEqual(3, d["three"])
    expectOptionalEqual(4, d["four"])
    expectOptionalEqual(5, d["five"])

    // Iterate over (key, value) tuples as a silly copy
    var d3 = Dictionary<String,Int>(minimumCapacity: 13)

    for (k, v) in d {
      d3[k] = v
    }
    expectOptionalEqual(1, d3["one"])
    expectOptionalEqual(2, d3["two"])
    expectOptionalEqual(3, d3["three"])
    expectOptionalEqual(4, d3["four"])
    expectOptionalEqual(5, d3["five"])

    expectEqual(3, d.values[d.keys.index(of: "three")!])
    expectEqual(4, d.values[d.keys.index(of: "four")!])

    expectEqual(3, d3.values[d.keys.index(of: "three")!])
    expectEqual(4, d3.values[d.keys.index(of: "four")!])
  }
}

#if _runtime(_ObjC)
DictionaryTestSuite.test("dropsBridgedCache") {
  // rdar://problem/18544533
  // Previously this code would segfault due to a double free in the Dictionary
  // implementation.
  // This test will only fail in address sanitizer.
  var dict = [0:10]
  do {
    var bridged: NSDictionary = dict as NSDictionary
    expectEqual(10, bridged[0 as NSNumber] as! Int)
  }

  dict[0] = 11
  do {
    var bridged: NSDictionary = dict as NSDictionary
    expectEqual(11, bridged[0 as NSNumber] as! Int)
  }
}

DictionaryTestSuite.test("getObjects:andKeys:") {
  let d = ([1: "one", 2: "two"] as Dictionary<Int, String>) as NSDictionary
  var keys = UnsafeMutableBufferPointer(
    start: UnsafeMutablePointer<NSNumber>.allocate(capacity: 2), count: 2)
  var values = UnsafeMutableBufferPointer(
    start: UnsafeMutablePointer<NSString>.allocate(capacity: 2), count: 2)
  var kp = AutoreleasingUnsafeMutablePointer<AnyObject?>(keys.baseAddress!)
  var vp = AutoreleasingUnsafeMutablePointer<AnyObject?>(values.baseAddress!)
  var null: AutoreleasingUnsafeMutablePointer<AnyObject?>? = nil

  d.available_getObjects(null, andKeys: null) // don't segfault

  d.available_getObjects(null, andKeys: kp)
  expectEqual([2, 1] as [NSNumber], Array(keys))

  d.available_getObjects(vp, andKeys: null)
  expectEqual(["two", "one"] as [NSString], Array(values))

  d.available_getObjects(vp, andKeys: kp)
  expectEqual([2, 1] as [NSNumber], Array(keys))
  expectEqual(["two", "one"] as [NSString], Array(values))
}
#endif

DictionaryTestSuite.test("popFirst") {
  // Empty
  do {
    var d = [Int: Int]()
    let popped = d.popFirst()
    expectEmpty(popped)
  }

  do {
    var popped = [(Int, Int)]()
    var d: [Int: Int] = [
      1010: 1010,
      2020: 2020,
      3030: 3030,
    ]
    let expected = Array(d.map{($0.0, $0.1)})
    while let element = d.popFirst() {
      popped.append(element)
    }
    expectEqualSequence(expected, Array(popped)) {
      (lhs: (Int, Int), rhs: (Int, Int)) -> Bool in
      lhs.0 == rhs.0 && lhs.1 == rhs.1
    }
    expectTrue(d.isEmpty)
  }
}

DictionaryTestSuite.test("removeAt") {
  // Test removing from the startIndex, the middle, and the end of a dictionary.
  for i in 1...3 {
    var d: [Int: Int] = [
      10: 1010,
      20: 2020,
      30: 3030,
    ]
    let removed = d.remove(at: d.index(forKey: i*10)!)
    expectEqual(i*10, removed.0)
    expectEqual(i*1010, removed.1)
    expectEqual(2, d.count)
    expectEmpty(d.index(forKey: i))
    let origKeys: [Int] = [10, 20, 30]
    expectEqual(origKeys.filter { $0 != (i*10) }, d.keys.sorted())
  }
}

DictionaryTestSuite.setUp {
  resetLeaksOfDictionaryKeysValues()
#if _runtime(_ObjC)
  resetLeaksOfObjCDictionaryKeysValues()
#endif
}

DictionaryTestSuite.tearDown {
  expectNoLeaksOfDictionaryKeysValues()
#if _runtime(_ObjC)
  expectNoLeaksOfObjCDictionaryKeysValues()
#endif
}

runAllTests()

