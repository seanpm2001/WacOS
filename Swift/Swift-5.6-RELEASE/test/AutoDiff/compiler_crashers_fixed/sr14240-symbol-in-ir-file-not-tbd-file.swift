// RUN: %target-run-simple-swift

// REQUIRES: executable_test

// SR-14240: Error: symbol 'powTJfSSpSr' (powTJfSSpSr) is in generated IR file,
// but not in TBD file.

import _Differentiation

#if canImport(Darwin)
  import Darwin
#elseif canImport(Glibc)
  import Glibc
#elseif os(Windows)
  import CRT
#else
#error("Unsupported platform")
#endif

@inlinable
@derivative(of: pow)
func powVJP(
  _ base: Double, _ exponent: Double
) -> (value: Double, pullback: (Double) -> (Double, Double)) {
  fatalError()
}
