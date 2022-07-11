// RUN: %target-run-stdlib-swift
// REQUIRES: executable_test

import Swift
import SwiftPrivate
import StdlibUnittest


var HashingTestSuite = TestSuite("Hashing")

HashingTestSuite.test("_mixUInt32/GoldenValues") {
  expectEqual(0x11b882c9, _mixUInt32(0x0))
  expectEqual(0x60d0aafb, _mixUInt32(0x1))
  expectEqual(0x636847b5, _mixUInt32(0xffff))
  expectEqual(0x203f5350, _mixUInt32(0xffff_ffff))

  expectEqual(0xb8747ef6, _mixUInt32(0xa62301f9))
  expectEqual(0xef4eeeb2, _mixUInt32(0xfe1b46c6))
  expectEqual(0xd44c9cf1, _mixUInt32(0xe4daf7ca))
  expectEqual(0xfc1eb1de, _mixUInt32(0x33ff6f5c))
  expectEqual(0x5605f0c0, _mixUInt32(0x13c2a2b8))
  expectEqual(0xd9c48026, _mixUInt32(0xf3ad1745))
  expectEqual(0x471ab8d0, _mixUInt32(0x656eff5a))
  expectEqual(0xfe265934, _mixUInt32(0xfd2268c9))
}

HashingTestSuite.test("_mixInt32/GoldenValues") {
  expectEqual(Int32(bitPattern: 0x11b882c9), _mixInt32(0x0))
}

HashingTestSuite.test("_mixUInt64/GoldenValues") {
  expectEqual(0xb2b2_4f68_8dc4_164d, _mixUInt64(0x0))
  expectEqual(0x792e_33eb_0685_57de, _mixUInt64(0x1))
  expectEqual(0x9ec4_3423_1b42_3dab, _mixUInt64(0xffff))
  expectEqual(0x4cec_e9c9_01fa_9a84, _mixUInt64(0xffff_ffff))
  expectEqual(0xcba5_b650_bed5_b87c, _mixUInt64(0xffff_ffff_ffff))
  expectEqual(0xe583_5646_3fb8_ac99, _mixUInt64(0xffff_ffff_ffff_ffff))

  expectEqual(0xf5d0079f828d43a5, _mixUInt64(0x94ce7d9319f8d233))
  expectEqual(0x61900a6be9db9c3f, _mixUInt64(0x2728821e8c5b1f7))
  expectEqual(0xf2fd34b1b7d4b46e, _mixUInt64(0xe7f67ec98c64f482))
  expectEqual(0x216199ed628c821, _mixUInt64(0xd7c277b5438873ac))
  expectEqual(0xb1b486ff5f2e0e53, _mixUInt64(0x8399f1d563c42f82))
  expectEqual(0x61acc92bd91c030, _mixUInt64(0x488cefd48a2c4bfd))
  expectEqual(0xa7a52d6e4a8e3ddf, _mixUInt64(0x270a15116c351f95))
  expectEqual(0x98ceedc363c4e56a, _mixUInt64(0xe5fb9b5f6c426a84))
}

HashingTestSuite.test("_mixUInt64/GoldenValues") {
  expectEqual(Int64(bitPattern: 0xb2b2_4f68_8dc4_164d), _mixInt64(0x0))
}

HashingTestSuite.test("_mixUInt/GoldenValues") {
#if arch(i386) || arch(arm)
  expectEqual(0x11b8_82c9, _mixUInt(0x0))
#elseif arch(x86_64) || arch(arm64) || arch(powerpc64) || arch(powerpc64le) || arch(s390x)
  expectEqual(0xb2b2_4f68_8dc4_164d, _mixUInt(0x0))
#else
  fatalError("unimplemented")
#endif
}

HashingTestSuite.test("_mixInt/GoldenValues") {
#if arch(i386) || arch(arm)
  expectEqual(Int(bitPattern: 0x11b8_82c9), _mixInt(0x0))
#elseif arch(x86_64) || arch(arm64) || arch(powerpc64) || arch(powerpc64le) || arch(s390x)
  expectEqual(Int(bitPattern: 0xb2b2_4f68_8dc4_164d), _mixInt(0x0))
#else
  fatalError("unimplemented")
#endif
}

HashingTestSuite.test("_squeezeHashValue/Int") {
  // Check that the function can return values that cover the whole range.
  func checkRange(_ r: Range<Int>) {
    var results = [Int : Void]()
    for _ in 0..<(14 * (r.upperBound - r.lowerBound)) {
      let v = _squeezeHashValue(randInt(), r)
      expectTrue(r ~= v)
      if results[v] == nil {
        results[v] = Void()
      }
    }
    expectEqual(r.upperBound - r.lowerBound, results.count)
  }
  checkRange(Int.min..<(Int.min+10))
  checkRange(0..<4)
  checkRange(0..<8)
  checkRange(-5..<5)
  checkRange((Int.max-10)..<(Int.max-1))

  // Check that we can handle ranges that span more than `Int.max`.
#if arch(i386) || arch(arm)
  expectEqual(-0x6e477d37, _squeezeHashValue(0, Int.min..<(Int.max - 1)))
  expectEqual(0x38a3ea26, _squeezeHashValue(2, Int.min..<(Int.max - 1)))
#elseif arch(x86_64) || arch(arm64) || arch(powerpc64) || arch(powerpc64le) || arch(s390x)
  expectEqual(0x32b24f688dc4164d, _squeezeHashValue(0, Int.min..<(Int.max - 1)))
  expectEqual(-0x6d1cc14f97aa822, _squeezeHashValue(1, Int.min..<(Int.max - 1)))
#else
  fatalError("unimplemented")
#endif
}

HashingTestSuite.test("_squeezeHashValue/UInt") {
  // Check that the function can return values that cover the whole range.
  func checkRange(_ r: Range<UInt>) {
    var results = [UInt : Void]()
    let cardinality = r.upperBound - r.lowerBound
    for _ in 0..<(10*cardinality) {
      let v = _squeezeHashValue(randInt(), r)
      expectTrue(r ~= v)
      if results[v] == nil {
        results[v] = Void()
      }
    }
    expectEqual(Int(cardinality), results.count)
  }
  checkRange(0..<4)
  checkRange(0..<8)
  checkRange(0..<10)
  checkRange(10..<20)
  checkRange((UInt.max-10)..<(UInt.max-1))
}

HashingTestSuite.test("String/hashValue/topBitsSet") {
#if _runtime(_ObjC)
#if arch(x86_64) || arch(arm64)
  // Make sure that we don't accidentally throw away bits by storing the result
  // of NSString.hash into an int in the runtime.

  // This is the bit pattern that we xor to NSString's hash value.
  let hashOffset = UInt(bitPattern: 0x429b_1266_0000_0000)
  let hash = "efghijkl".hashValue
  // When we are not equal to the top bit of the xor'ed hashOffset pattern
  // there where some bits set.
  let topHashBits = UInt(bitPattern: hash) & 0xffff_ffff_0000_0000
  expectTrue(hash > 0)
  expectTrue(topHashBits != hashOffset)
#endif
#endif
}

HashingTestSuite.test("overridePerExecutionHashSeed/overflow") {
  // Test that we don't use checked arithmetic on the seed.
  _HashingDetail.fixedSeedOverride = UInt64.max
  expectEqual(0x4344_dc3a_239c_3e81, _mixUInt64(0xffff_ffff_ffff_ffff))
  _HashingDetail.fixedSeedOverride = 0
}

runAllTests()

