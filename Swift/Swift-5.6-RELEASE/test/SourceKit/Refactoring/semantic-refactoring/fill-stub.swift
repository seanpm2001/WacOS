protocol P {
  func foo()
}

class C1 : P {}

// RUN: %empty-directory(%t.result)
// RUN: %sourcekitd-test -req=refactoring.fillstub -pos=5:8 %s -- %s > %t.result/fill-stub.swift.expected
// RUN: %diff -u %S/fill-stub.swift.expected %t.result/fill-stub.swift.expected

// REQUIRES: OS=macosx || OS=linux-gnu
