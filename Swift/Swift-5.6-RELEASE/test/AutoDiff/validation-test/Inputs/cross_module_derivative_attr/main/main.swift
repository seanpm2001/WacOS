import StdlibUnittest
import _Differentiation

import module1

var Tests = TestSuite("CrossModuleDerivativeAttr")

Tests.test("CrossFile") {
  let grad = gradient(at: 0, of: fCrossFile)
  expectEqual(10, grad)
}

runAllTests()
