// This test specifically exercises the interpreter's top-level error handler.
// RUN: not --crash %target-jit-run %s 2>&1 | %FileCheck %s
// REQUIRES: swift_interpreter

// rdar://20809122
// CHECK: Error raised at top level: return_from_main.MyError.Foo
enum MyError : Error { case Foo }
throw MyError.Foo
