// RUN: %target-run-simple-swift(-I %S/Inputs -Xfrontend -enable-cxx-interop -Xfrontend -validate-tbd-against-ir=none)
//
// REQUIRES: executable_test

// TODO: See why -validate-tbd-against-ir=none is needed here (SR-14069)

import StaticVarInitCallsFunction
import StdlibUnittest

var MembersTestSuite = TestSuite("MembersTestSuite")

MembersTestSuite.test("static var init calls function") {
  expectEqual(42, initializeStaticVar())
}

runAllTests()
