// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) %s -emit-ir -o - | %FileCheck %s

// REQUIRES: OS=macosx

import cvars

// Check that the mangling is correct.
// CHECK: @PI = external global float, align 4

func getPI() -> Float {
  return PI
}
