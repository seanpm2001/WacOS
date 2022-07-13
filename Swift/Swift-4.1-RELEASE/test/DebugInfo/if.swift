// RUN: %target-swift-frontend -g -emit-ir %s | %FileCheck %s

func yieldValue() -> Int64? { return 23 }

// Verify that variables bound in the while statements are in distinct scopes.
if let val = yieldValue() {
// CHECK: !DILocalVariable(name: "val", scope: ![[SCOPE1:[0-9]+]]
// CHECK: ![[SCOPE1]] = distinct !DILexicalBlock(scope: ![[MAIN:[0-9]+]]
}
if let val = yieldValue() {
// CHECK: !DILocalVariable(name: "val", scope: ![[SCOPE2:[0-9]+]]
// CHECK: ![[SCOPE2]] = distinct !DILexicalBlock(scope: ![[MAIN]]
}
