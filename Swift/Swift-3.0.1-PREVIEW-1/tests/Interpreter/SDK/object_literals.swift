// RUN: rm -rf %t && mkdir -p %t/Test.app/Contents/MacOS
// RUN: cp -r %S/Inputs/object_literals-Resources %t/Test.app/Contents/Resources
// RUN: %target-build-swift %s -o %t/Test.app/Contents/MacOS/main
// RUN: %target-run %t/Test.app/Contents/MacOS/main

// REQUIRES: executable_test
// REQUIRES: OS=macosx

import AppKit
import StdlibUnittest

var LiteralsTestSuite = TestSuite("ObjectLiterals")

LiteralsTestSuite.test("file") {
  // This is what requires the proper bundle folder structure.
  let resource = #fileLiteral(resourceName: "testData.plist")
  let contents = NSDictionary(contentsOf: resource) as! [String: NSObject]?
  _ = expectNotEmpty(contents)
  expectEqual(["test": true as NSObject], contents!)
}

LiteralsTestSuite.test("image") {
  let image = #imageLiteral(resourceName: NSImageNameComputer)
  expectTrue(image.isValid)
}

LiteralsTestSuite.test("color") {
  let color = #colorLiteral(red: 1.0, green: 0.0, blue: 0.0, alpha: 1.0)
  expectEqual(NSColor(srgbRed: 1.0, green: 0.0, blue: 0.0, alpha: 1.0), color)
}

runAllTests()

