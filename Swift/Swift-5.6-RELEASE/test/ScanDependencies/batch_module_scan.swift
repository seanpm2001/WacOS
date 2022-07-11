// RUN: %empty-directory(%t)
// RUN: %empty-directory(%t/inputs)
// RUN: %empty-directory(%t/outputs)
// RUN: mkdir -p %t/clang-module-cache

// RUN: echo "[{" > %/t/inputs/input.json
// RUN: echo "\"swiftModuleName\": \"F\"," >> %/t/inputs/input.json
// RUN: echo "\"arguments\": \"-target x86_64-apple-macosx10.9\"," >> %/t/inputs/input.json
// RUN: echo "\"output\": \"%/t/outputs/F.swiftmodule.json\"" >> %/t/inputs/input.json
// RUN: echo "}," >> %/t/inputs/input.json
// RUN: echo "{" >> %/t/inputs/input.json
// RUN: echo "\"clangModuleName\": \"F\"," >> %/t/inputs/input.json
// RUN: echo "\"arguments\": \"-target x86_64-apple-macosx10.9\"," >> %/t/inputs/input.json
// RUN: echo "\"output\": \"%/t/outputs/F.pcm.json\"" >> %/t/inputs/input.json
// RUN: echo "}]" >> %/t/inputs/input.json

// RUN: %target-swift-frontend -scan-dependencies -module-cache-path %t/clang-module-cache %s -o %t/deps.json -I %S/Inputs/CHeaders -I %S/Inputs/Swift -emit-dependencies -emit-dependencies-path %t/deps.d -import-objc-header %S/Inputs/CHeaders/Bridging.h -swift-version 4 -batch-scan-input-file %/t/inputs/input.json

// Check the contents of the JSON output
// RUN: %FileCheck %s -check-prefix=CHECK-PCM < %t/outputs/F.pcm.json
// RUN: %FileCheck %s -check-prefix=CHECK-SWIFT < %t/outputs/F.swiftmodule.json

// CHECK-PCM: 		{
// CHECK-PCM-NEXT:  "mainModuleName": "F",
// CHECK-PCM-NEXT:  "modules": [
// CHECK-PCM-NEXT:    {
// CHECK-PCM-NEXT:      "clang": "F"
// CHECK-PCM-NEXT:    },
// CHECK-PCM-NEXT:    {
// CHECK-PCM-NEXT:      "modulePath": "F.pcm",
// CHECK-PCM: "-I

// CHECK-SWIFT: {
// CHECK-SWIFT-NEXT:  "mainModuleName": "F",
// CHECK-SWIFT-NEXT:  "modules": [
// CHECK-SWIFT-NEXT:    {
// CHECK-SWIFT-NEXT:      "swift": "F"
// CHECK-SWIFT-NEXT:    },
// CHECK-SWIFT-NEXT:    {
// CHECK-SWIFT-NEXT:      "modulePath": "F.swiftmodule",
