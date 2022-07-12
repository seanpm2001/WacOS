// RUN: %empty-directory(%t)

// RUN: %target-build-swift-dylib(%t/%target-library-name(resil)) -enable-library-evolution %S/Inputs/resilient_generic_struct_v1.swift -emit-module -emit-module-path %t/resil.swiftmodule -module-name resil
// RUN: %target-codesign %t/%target-library-name(resil)

// RUN: %target-build-swift %s -L %t -I %t -lresil -o %t/main %target-rpath(%t)
// RUN: %target-codesign %t/main

// RUN: %target-build-swift-dylib(%t/%target-library-name(resil)) -enable-library-evolution %S/Inputs/resilient_generic_struct_v2.swift -emit-module -emit-module-path %t/resil.swiftmodule -module-name resil
// RUN: %target-codesign %t/%target-library-name(resil)

// RUN: %target-run %t/main %t/%target-library-name(resil)

// REQUIRES: executable_test

import StdlibUnittest

// We build this code against a version of 'resil' where
// ResilientGenericStruct<T> doesn't store a T, then switch the
// dynamic library to a new version where it does, introducing
// an unresolvable dynamic cycle.
//
// It would also be sufficient to demonstrate this crash if the
// compiler *actually* didn't know about the internal implementation
// details of 'resil' when building this file, but since it currently
// still does, it'll report a cycle immediately if we don't pull
// this switcharoo.
import resil

var DynamicMetadataCycleTests =
  TestSuite("Unresolvable dynamic metadata cycle tests")

enum test0_Node {
    case link(ResilientGenericStruct<test0_Node>)
}


DynamicMetadataCycleTests.test("cycle through enum")
  .crashOutputMatches("runtime error: unresolvable type metadata dependency cycle detected")
  .crashOutputMatches("  Request for transitive completion of main.test0_Node")
  .crashOutputMatches("  depends on layout of resil.ResilientGenericStruct<main.test0_Node")
  .crashOutputMatches("  depends on layout of main.test0_Node")
  .code {
    expectCrashLater()
    _blackHole(test0_Node.self)
  }

runAllTests()
