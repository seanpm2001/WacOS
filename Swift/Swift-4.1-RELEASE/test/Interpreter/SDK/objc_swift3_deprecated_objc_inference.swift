// RUN: rm -rf %t  &&  mkdir -p %t
// RUN: %target-build-swift -swift-version 4 -Xfrontend -enable-swift3-objc-inference %s -o %t/a.out
// RUN: %target-run %t/a.out 2>&1 | %FileCheck %s -check-prefix=CHECK_WARNINGS
// RUN: env SWIFT_DEBUG_IMPLICIT_OBJC_ENTRYPOINT=0 SIMCTL_CHILD_SWIFT_DEBUG_IMPLICIT_OBJC_ENTRYPOINT=0 %target-run %t/a.out 2>&1 | %FileCheck %s -check-prefix=CHECK_NOTHING

// RUN: env SWIFT_DEBUG_IMPLICIT_OBJC_ENTRYPOINT=1 SIMCTL_CHILD_SWIFT_DEBUG_IMPLICIT_OBJC_ENTRYPOINT=2 %target-run %t/a.out > %t/level1.log 2>&1
// RUN: %FileCheck %s -check-prefix=CHECK_WARNINGS < %t/level1.log

// RUN: env SWIFT_DEBUG_IMPLICIT_OBJC_ENTRYPOINT=2 SIMCTL_CHILD_SWIFT_DEBUG_IMPLICIT_OBJC_ENTRYPOINT=2 %target-run %t/a.out > %t/level2.log 2>&1
// RUN: %FileCheck %s -check-prefix=CHECK_WARNINGS < %t/level2.log

// RUN: SWIFT_DEBUG_IMPLICIT_OBJC_ENTRYPOINT=3 SIMCTL_CHILD_SWIFT_DEBUG_IMPLICIT_OBJC_ENTRYPOINT=3 %target-run %t/a.out -expect-crash > %t/level3.log 2>&1
// RUN: %FileCheck %s -check-prefix=CHECK_CRASH < %t/level3.log

// REQUIRES: executable_test
// REQUIRES: objc_interop
// REQUIRES: OS=macosx

import StdlibUnittest
import Foundation
import Darwin

var DeprecatedObjCInferenceTestSuite = TestSuite("DeprecatedObjCInferenceTestSuite")

class MyClass : NSObject {
  func foo() { }
  class func bar() { }
}

let x = MyClass()
let fooSel = "foo"
let barSel = "bar"

var shouldCrash = CommandLine.arguments.contains("-expect-crash")

DeprecatedObjCInferenceTestSuite.test("messagingObjCInference") {
	// Note whether we're expecting to crash.
  if shouldCrash { expectCrashLater() }
	
	// CHECK_NOTHING: ---Begin 
	// CHECK_WARNINGS: ---Begin 
	// CHECK_CRASH: ---Begin 
	fputs("---Begin\n", stderr)

	// CHECK_WARNINGS: .swift:26:3: implicit Objective-C entrypoint -[a.MyClass foo]
	// CHECK_CRASH: .swift:26:3: implicit Objective-C entrypoint -[a.MyClass foo]
	x.perform(Selector(fooSel))
	// CHECK_WARNINGS-NOT: .swift:26:3: implicit Objective-C entrypoint -[a.MyClass foo]
	x.perform(Selector(fooSel))

	// CHECK_WARNINGS: .swift:27:3: implicit Objective-C entrypoint +[a.MyClass bar]
	type(of: x).perform(Selector(barSel))
	// CHECK_WARNINGS-NOT: .swift:27:3: implicit Objective-C entrypoint +[a.MyClass bar]
	type(of: x).perform(Selector(barSel))

	// CHECK_NOTHING-NEXT: ---End 
	// CHECK_WARNINGS: ---End 
	// CHECK_CRASH-NOT: ---End
	fputs("---End\n", stderr)
}

runAllTests()

