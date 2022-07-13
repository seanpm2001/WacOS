// RUN: %empty-directory(%t)

// RUN: %target-build-swift -emit-executable %s -g -o %t/ASTSection -emit-module
// RUN: %lldb-moduleimport-test %t/ASTSection | %FileCheck %s

// REQUIRES: executable_test

// The test ASTSection_linker.swift builds this code with separate
// compile and link steps.
// The test ASTSection_ObjC.swift builds this code with -DOBJC.

// A type that should be serialized.
class Foo {
init() { }
func bar() -> Int64 { return 42 }
}

// Some toplevel code that should not be serialized.
var foo: Foo = Foo()
Double(foo.bar())

#if OBJC
func objCUser(_ obj: ObjCClass) {}
#endif

// CHECK: Loaded module ASTSection from
// CHECK: - Target: {{.+}}-{{.+}}-{{.+}}
// CHECK: Importing ASTSection... ok!

// LINETABLE-CHECK-NOT: ASTSection

