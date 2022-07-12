func foo() -> Int {
  var a = 3
  a = a + 1
  return 1
}

// RUN: %empty-directory(%t.result)
// RUN: %sourcekitd-test -req=refactoring.extract.expr.repeated -pos=3:11 -end-pos 3:12 -name new_name %s -- %s > %t.result/extract-repeated-expression.swift.expected
// RUN: %diff -u %S/extract-repeated-expression.swift.expected %t.result/extract-repeated-expression.swift.expected

// REQUIRES: OS=macosx || OS=linux-gnu
