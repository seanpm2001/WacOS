// RUN: %target-build-swift -Xfrontend -disable-access-control -module-name a %s -o %t.out -O
// RUN: %target-run %t.out
// REQUIRES: executable_test

import SwiftPrivate
import StdlibUnittest


var HashingTestSuite = TestSuite("Hashing")

func avalancheTest(
  _ bits: Int,
  _ hashUnderTest: @escaping (UInt64) -> UInt64,
  _ pValue: Double
) {
  let testsInBatch = 100000
  let testData = randArray64(testsInBatch)
  let testDataHashed = Array(testData.lazy.map { hashUnderTest($0) })

  for inputBit in 0..<bits {
    // Using an array here makes the test too slow.
    var bitFlips = UnsafeMutablePointer<Int>.allocate(capacity: bits)
    for i in 0..<bits {
      bitFlips[i] = 0
    }
    for i in testData.indices {
      let inputA = testData[i]
      let outputA = testDataHashed[i]
      let inputB = inputA ^ (1 << UInt64(inputBit))
      let outputB = hashUnderTest(inputB)
      var delta = outputA ^ outputB
      for outputBit in 0..<bits {
        if delta & 1 == 1 {
          bitFlips[outputBit] += 1
        }
        delta = delta >> 1
      }
    }
    for outputBit in 0..<bits {
      expectTrue(
        chiSquaredUniform2(testsInBatch, bitFlips[outputBit], pValue),
        "inputBit: \(inputBit), outputBit: \(outputBit)")
    }
    bitFlips.deallocate(capacity: bits)
  }
}

// White-box testing: assume that the other N-bit to N-bit mixing functions
// just dispatch to these.  (Avalanche test is relatively expensive.)
HashingTestSuite.test("_mixUInt64/avalanche") {
  avalancheTest(64, _mixUInt64, 0.02)
}

HashingTestSuite.test("_mixUInt32/avalanche") {
  avalancheTest(32, { UInt64(_mixUInt32(UInt32($0 & 0xffff_ffff))) }, 0.02)
}

runAllTests()

