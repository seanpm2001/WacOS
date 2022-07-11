// RUN: %empty-directory(%t)
// RUN: %empty-directory(%t/Bar.swiftmodule)
// RUN: echo "// swift-interface-format-version: 1.0" > %t/arm64.swiftinterface
// RUN: echo "// swift-module-flags: -module-name arm64 -target arm64e-apple-macos11.0" >> %t/arm64.swiftinterface

import arm64

// RUN: %target-swift-frontend -scan-dependencies %s -o %t/deps.json -I %t -target arm64-apple-macos11.0
// RUN: %FileCheck %s < %t/deps.json

// CHECK-NOT: arm64e-apple-macos11.0
