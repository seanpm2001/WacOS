// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend %clang-importer-sdk %S/Inputs/inlinable_bitfields_other.swift -emit-module -emit-module-path %t/inlinable_bitfields_other.swiftmodule
// RUN: %target-swift-frontend %clang-importer-sdk -I %t %s -emit-ir -disable-llvm-optzns -O | %FileCheck %s -DINT=i%target-ptrsize

import inlinable_bitfields_other

public func g(_ m: MM) -> UInt32 {
  return f(m)
}

// Just make sure this is a definition and not a declaration...

// CHECK: define internal{{( zeroext)?}} i32 @"$So5ModRMV$rm$getter"
