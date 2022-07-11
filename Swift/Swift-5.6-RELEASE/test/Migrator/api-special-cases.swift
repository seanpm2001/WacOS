// REQUIRES: objc_interop
// RUN: %empty-directory(%t.mod)
// RUN: %target-swift-frontend -emit-module -o %t.mod/MyAppKit.swiftmodule %S/Inputs/MyAppKit.swift -module-name MyAppKit -parse-as-library
// RUN: %target-swift-frontend -emit-module -o %t.mod/MySwift.swiftmodule %S/Inputs/MySwift.swift -module-name MySwift -parse-as-library
// RUN: %empty-directory(%t) && %target-swift-frontend -c -update-code -primary-file %s -I %t.mod -emit-migrated-file-path %t/api-special-cases.swift.result -emit-remap-file-path %t/api-special-cases.swift.remap -o /dev/null -api-diff-data-file %S/Inputs/SpecialCaseAPI.json
// RUN: diff -u %S/api-special-cases.swift.expected %t/api-special-cases.swift.result

import MyAppKit
import MySwift

func foo(_ Opt: NSOpenGLOption, _ pointer: UnsafeMutablePointer<UnsafeMutablePointer<Int8>>) {
  var Value = 1
  NSOpenGLSetOption(Opt, 1)
  NSOpenGLGetOption(Opt, &Value)
  UIApplicationMain(CommandLine.argc, pointer, "", "")
  UIApplicationMain(
    CommandLine.argc, pointer, "", "")
  UIApplicationMain( CommandLine . 
    argc, pointer, "", "")
  UIApplicationMain(10, pointer, "", "")
}

do {
  _ = abs(1.0)
}
