func foo() -> Int {
  var a = 3
  a = a + 1
  return 1
}

// RUN: %empty-directory(%t.result)
// RUN: %sourcekitd-test -req=refactoring.extract.function -pos=2:1 -end-pos 4:11 %s -- %s > %t.result/extract-func-default.swift.expected
// RUN: %diff -u %S/extract-func-default.swift.expected %t.result/extract-func-default.swift.expected

// FIXME: Fails on linux with assertion: "!GlibcModuleMapPath.empty()"" failed
// REQUIRES: OS=macosx
