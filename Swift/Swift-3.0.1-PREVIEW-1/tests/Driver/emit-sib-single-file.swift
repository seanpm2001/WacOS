// RUN: %target-build-swift -Onone -emit-sib %s -o %t.sib
// RUN: %target-build-swift %t.sib -o %t
// RUN: %target-run %t | %FileCheck %s

// RUN: %target-build-swift -Onone -c %t.sib -o %t.o
// RUN: %target-build-swift %t.o -o %t
// RUN: %target-run %t | %FileCheck %s

// RUN: %target-build-swift -Onone -emit-sibgen %s -o %t.sib
// RUN: %target-build-swift %t.sib -o %t
// RUN: %target-run %t | %FileCheck %s

// RUN: %target-build-swift -Onone -c %t.sib -o %t.o
// RUN: %target-build-swift %t.o -o %t
// RUN: %target-run %t | %FileCheck %s
// REQUIRES: executable_test

// CHECK: Hello World
// CHECK: Hello Bob, today is Tuesday.

func greet(_ name: String, _ day: String) -> String {
  return "Hello \(name), today is \(day)."
}

print("Hello World")
print(greet("Bob", "Tuesday"))
