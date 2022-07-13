// -*- swift -*-

//===----------------------------------------------------------------------===//
// Automatically Generated From validation-test/stdlib/Slice/Inputs/Template.swift.gyb
// Do Not Edit Directly!
//===----------------------------------------------------------------------===//

// RUN: %target-run-simple-swift
// REQUIRES: executable_test

// FIXME: the test is too slow when the standard library is not optimized.
// REQUIRES: optimized_stdlib

import StdlibUnittest
import StdlibCollectionUnittest

var SliceTests = TestSuite("Collection")

let prefix: [Int] = [-9999, -9998, -9997]
let suffix: [Int] = []

func makeCollection(elements: [OpaqueValue<Int>])
  -> RangeReplaceableRandomAccessSlice<MinimalRangeReplaceableRandomAccessCollection<OpaqueValue<Int>>> {
  var baseElements = prefix.map(OpaqueValue.init)
  baseElements.append(contentsOf: elements)
  baseElements.append(contentsOf: suffix.map(OpaqueValue.init))
  let base = MinimalRangeReplaceableRandomAccessCollection(elements: baseElements)
  let startIndex = base.index(
    base.startIndex,
    offsetBy: numericCast(prefix.count))
  let endIndex = base.index(
    base.startIndex,
    offsetBy: numericCast(prefix.count + elements.count))
  return RangeReplaceableRandomAccessSlice(
    base: base,
    bounds: startIndex..<endIndex)
}

func makeCollectionOfEquatable(elements: [MinimalEquatableValue])
  -> RangeReplaceableRandomAccessSlice<MinimalRangeReplaceableRandomAccessCollection<MinimalEquatableValue>> {
  var baseElements = prefix.map(MinimalEquatableValue.init)
  baseElements.append(contentsOf: elements)
  baseElements.append(contentsOf: suffix.map(MinimalEquatableValue.init))
  let base = MinimalRangeReplaceableRandomAccessCollection(elements: baseElements)
  let startIndex = base.index(
    base.startIndex,
    offsetBy: numericCast(prefix.count))
  let endIndex = base.index(
    base.startIndex,
    offsetBy: numericCast(prefix.count + elements.count))
  return RangeReplaceableRandomAccessSlice(
    base: base,
    bounds: startIndex..<endIndex)
}

func makeCollectionOfComparable(elements: [MinimalComparableValue])
  -> RangeReplaceableRandomAccessSlice<MinimalRangeReplaceableRandomAccessCollection<MinimalComparableValue>> {
  var baseElements = prefix.map(MinimalComparableValue.init)
  baseElements.append(contentsOf: elements)
  baseElements.append(contentsOf: suffix.map(MinimalComparableValue.init))
  let base = MinimalRangeReplaceableRandomAccessCollection(elements: baseElements)
  let startIndex = base.index(
    base.startIndex,
    offsetBy: numericCast(prefix.count))
  let endIndex = base.index(
    base.startIndex,
    offsetBy: numericCast(prefix.count + elements.count))
  return RangeReplaceableRandomAccessSlice(
    base: base,
    bounds: startIndex..<endIndex)
}

var resiliencyChecks = CollectionMisuseResiliencyChecks.all
resiliencyChecks.creatingOutOfBoundsIndicesBehavior = .trap
resiliencyChecks.subscriptOnOutOfBoundsIndicesBehavior = .trap
resiliencyChecks.subscriptRangeOnOutOfBoundsRangesBehavior = .trap

SliceTests.addRangeReplaceableRandomAccessSliceTests(
  "RangeReplaceableRandomAccessSlice_Of_MinimalRangeReplaceableRandomAccessCollection_WithPrefix.swift.",
  makeCollection: makeCollection,
  wrapValue: identity,
  extractValue: identity,
  makeCollectionOfEquatable: makeCollectionOfEquatable,
  wrapValueIntoEquatable: identityEq,
  extractValueFromEquatable: identityEq,
  resiliencyChecks: resiliencyChecks,
  outOfBoundsIndexOffset: 6
)

runAllTests()
