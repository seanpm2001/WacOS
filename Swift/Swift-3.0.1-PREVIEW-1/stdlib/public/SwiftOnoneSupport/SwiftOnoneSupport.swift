//===----------------------------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
// Pre-specialization of some popular generic classes and functions.
//===----------------------------------------------------------------------===//
import Swift

struct _Prespecialize {
  // Create specializations for the arrays of most
  // popular builtin integer and floating point types.
  static internal func _specializeArrays() {
    func _createArrayUser<Element : Comparable>(_ sampleValue: Element) {
      // Initializers.
      let _: [Element] = [sampleValue]
      var a = [Element](repeating: sampleValue, count: 1)

      // Read array element
      let _ =  a[0]

      // Set array elements
      for j in 1..<a.count {
        a[0] = a[j]
        a[j-1] = a[j]
      }

      for i1 in 0..<a.count {
        for i2 in 0..<a.count {
          a[i1] = a[i2]
        }
      }

      a[0] = sampleValue

      // Get count and capacity
      let _ = a.count + a.capacity

      // Iterate over array
      for e in a {
        print(e)
        print("Value: \(e)")
      }

      print(a)

      // Reserve capacity
      a.removeAll()
      a.reserveCapacity(100)

      // Sort array
      let _ = a.sorted { (a: Element, b: Element) in a < b }
      a.sort { (a: Element, b: Element) in a < b }

      // force specialization of append.
      a.append(a[0])

      // force specialization of print<Element>
      print(sampleValue)
      print("Element:\(sampleValue)")
    }

    func _createArrayUserWithoutSorting<Element>(_ sampleValue: Element) {
      // Initializers.
      let _: [Element] = [sampleValue]
      var a = [Element](repeating: sampleValue, count: 1)

      // Read array element
      let _ =  a[0]

      // Set array elements
      for j in 0..<a.count {
        a[0] = a[j]
      }

      for i1 in 0..<a.count {
        for i2 in 0..<a.count {
          a[i1] = a[i2]
        }
      }

      a[0] = sampleValue

      // Get length and capacity
      let _ = a.count + a.capacity

      // Iterate over array
      for e in a {
        print(e)
        print("Value: \(e)")
      }

      print(a)

      // Reserve capacity
      a.removeAll()
      a.reserveCapacity(100)


      // force specialization of append.
      a.append(a[0])

      // force specialization of print<Element>
      print(sampleValue)
      print("Element:\(sampleValue)")
    }

    // Force pre-specialization of arrays with elements of different
    // integer types.
    _createArrayUser(1 as Int)
    _createArrayUser(1 as Int8)
    _createArrayUser(1 as Int16)
    _createArrayUser(1 as Int32)
    _createArrayUser(1 as Int64)
    _createArrayUser(1 as UInt)
    _createArrayUser(1 as UInt8)
    _createArrayUser(1 as UInt16)
    _createArrayUser(1 as UInt32)
    _createArrayUser(1 as UInt64)

    // Force pre-specialization of arrays with elements of different
    // floating point types.
    _createArrayUser(1.5 as Float)
    _createArrayUser(1.5 as Double)

    // Force pre-specialization of string arrays
    _createArrayUser("a" as String)

    // Force pre-specialization of arrays with elements of different
    // character and unicode scalar types.
    _createArrayUser("a" as Character)
    _createArrayUser("a" as UnicodeScalar)
    _createArrayUserWithoutSorting("a".utf8)
    _createArrayUserWithoutSorting("a".utf16)
    _createArrayUserWithoutSorting("a".unicodeScalars)
    _createArrayUserWithoutSorting("a".characters)
  }

  // Force pre-specialization of Range<Int>
  @discardableResult
  static internal func _specializeRanges() -> Int {
    let a = [Int](repeating: 1, count: 10)
    var count = 0
    // Specialize Range for integers
    for i in 0..<a.count {
      count += a[i]
    }
    // Specialize Range for integers
    for j in 0...a.count - 1{
      count += a[j]
    }
    return count
  }
}

// Mark with optimize.sil.never to make sure its not get
// rid of by dead function elimination. 
@_semantics("optimize.sil.never")
internal func _swift_forcePrespecializations() {
  _Prespecialize._specializeArrays()
  _Prespecialize._specializeRanges()
}
