// RUN: %target-run-simple-swift
// REQUIRES: executable_test

// REQUIRES: objc_interop

import os
import Foundation
import StdlibUnittest

defer { runAllTests() }

var osAPI = TestSuite("osAPI")

if #available(OSX 10.12, iOS 10.0, watchOS 3.0, tvOS 10.0, *) {
	osAPI.test("log") {
		os_log("test: %d", 42)
		// FIXME: cast to NSString because of <rdar://problem/27651717> CVarArg does not work with String
		os_log("test2: %2", "test" as! NSString)
	}
}
