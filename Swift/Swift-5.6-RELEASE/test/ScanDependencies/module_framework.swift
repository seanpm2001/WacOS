// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -scan-dependencies %s -o %t/deps.json -emit-dependencies -emit-dependencies-path %t/deps.d -swift-version 4 -Xcc -Xclang
// Check the contents of the JSON output
// RUN: %FileCheck %s < %t/deps.json

// Ensure that round-trip serialization does not affect result
// RUN: %target-swift-frontend -scan-dependencies -test-dependency-scan-cache-serialization %s -o %t/deps.json -emit-dependencies -emit-dependencies-path %t/deps.d -swift-version 4 -Xcc -Xclang
// RUN: %FileCheck %s < %t/deps.json

// REQUIRES: OS=macosx

import CryptoKit

// CHECK: "mainModuleName": "deps"
// CHECK: directDependencies
// CHECK-NEXT: {
// CHECK-NEXT: "swift": "CryptoKit"
// CHECK-NEXT: }
// CHECK-NEXT: {
// CHECK-NEXT: "swift": "Swift"
// CHECK-NEXT: }
// CHECK-NEXT: {
// CHECK-NEXT: "swift": "SwiftOnoneSupport"
// CHECK-NEXT: },
// CHECK-NEXT: {
// CHECK-NEXT: "swift": "_Concurrency"
// CHECK-NEXT: }
// CHECK-NEXT: ],

// CHECK: "isFramework": true
