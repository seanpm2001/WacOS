func foo() -> Int{
  var aaa = 1 + 2
  aaa = aaa + 3
  if aaa == 3 { aaa = 4 }
  return aaa
}

protocol P1 {
  func foo()
}

class C1 : P1 {}

enum E1 {
  case e1
  case e2
}

func foo1(_ e : E1) -> Int {
  switch e {
  default:
    return 0
  }
}

fileprivate func foo2() -> Int {
  func bar2() {}
  return foo2()
}


func foo3() -> Int {}

func foo4() -> Int {
  return foo3()
}

class C2 {
  func getSelf1() -> C2 {
    return self
  }
  func getSelf2() -> C2 {
    return self
  }
  func getSelf3() -> C2 {
    return self
  }
  func getSelf4() -> C2 {
    return self
  }
}

func foo5(_ c : C2) -> C2 {
  return c.getSelf1().getSelf2().getSelf3().getSelf4()
}

class C3 {
  func foo1() {}
  func foo2() {
    foo1()
  }
}

func foo6() -> String {
  return "abc"
}

class C4 {
  func foo1() {}
  func foo2() {
    // some comments
    foo1()
  }
}

func foo7() -> String {
  // some comments
  foo6()
}

// RUN: %sourcekitd-test -req=cursor -pos=3:1 -end-pos=5:13 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK1

// CHECK1: ACTIONS BEGIN
// CHECK1-NEXT: source.refactoring.kind.extract.function
// CHECK1-NEXT: Extract Method
// CHECK1-NEXT: ACTIONS END

// RUN: %sourcekitd-test -req=cursor -pos=1:16 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK2

// RUN: %sourcekitd-test -req=cursor -pos=12:8 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK3
// RUN: %sourcekitd-test -req=cursor -pos=21:5 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK4
// RUN: %sourcekitd-test -req=cursor -pos=26:20 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK-GLOBAL
// RUN: %sourcekitd-test -req=cursor -pos=27:11 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK-LOCAL

// RUN: %sourcekitd-test -req=cursor -pos=35:10 -end-pos=35:16 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK-RENAME-EXTRACT
// RUN: %sourcekitd-test -req=cursor -pos=35:10 -end-pos=35:16 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK-RENAME-EXTRACT

// RUN: %sourcekitd-test -req=cursor -pos=54:12 -end-pos=54:22 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK-SELF-RENAME1
// RUN: %sourcekitd-test -req=cursor -pos=54:23 -end-pos=54:33 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK-SELF-RENAME2
// RUN: %sourcekitd-test -req=cursor -pos=54:34 -end-pos=54:44 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK-SELF-RENAME3
// RUN: %sourcekitd-test -req=cursor -pos=54:45 -end-pos=54:55 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK-SELF-RENAME4

// RUN: %sourcekitd-test -req=cursor -pos=60:5 -end-pos=60:11 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK-IMPLICIT-SELF
// RUN: %sourcekitd-test -req=cursor -pos=65:10 -end-pos=65:15 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK-LOCALIZE-STRING

// RUN: %sourcekitd-test -req=cursor -pos=72:5 -end-pos=72:11 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK-RENAME-EXTRACT
// RUN: %sourcekitd-test -req=cursor -pos=78:3 -end-pos=78:9 -cursor-action %s -- %s | %FileCheck %s -check-prefix=CHECK-RENAME-EXTRACT

// CHECK2: ACTIONS BEGIN
// CHECK2-NEXT: source.refactoring.kind.rename.global
// CHECK2-NEXT: Global Rename
// CHECK2-NEXT: symbol from system module cannot be renamed
// CHECK2-NEXT: ACTIONS END

// CHECK3: ACTIONS BEGIN
// CHECK3-NEXT: source.refactoring.kind.rename.global
// CHECK3-NEXT: Global Rename
// CHECK3-NEXT: source.refactoring.kind.fillstub
// CHECK3-NEXT: Add Missing Protocol Requirements
// CHECK3-NEXT: ACTIONS END

// CHECK4: ACTIONS BEGIN
// CHECK4-NEXT: source.refactoring.kind.expand.default
// CHECK4-NEXT: Expand Default

// CHECK-GLOBAL: ACTIONS BEGIN
// CHECK-GLOBAL-NEXT: source.refactoring.kind.rename.global
// CHECK-GLOBAL-NEXT: Global Rename
// CHECK-GLOBAL-NEXT: ACTIONS END

// CHECK-LOCAL: ACTIONS BEGIN
// CHECK-LOCAL-NEXT: source.refactoring.kind.rename.local
// CHECK-LOCAL-NEXT: Local Rename
// CHECK-LOCAL-NEXT: ACTIONS END

// CHECK-RENAME-EXTRACT: Global Rename
// CHECK-RENAME-EXTRACT: Extract Method

// CHECK-SELF-RENAME1: getSelf1()
// CHECK-SELF-RENAME1: Global Rename

// CHECK-SELF-RENAME2: getSelf2()
// CHECK-SELF-RENAME2: Global Rename

// CHECK-SELF-RENAME3: getSelf3()
// CHECK-SELF-RENAME3: Global Rename

// CHECK-SELF-RENAME4: getSelf4()
// CHECK-SELF-RENAME4: Global Rename

// CHECK-IMPLICIT-SELF: Global Rename

// CHECK-LOCALIZE-STRING: source.refactoring.kind.localize.string

// REQUIRES-ANY: OS=macosx, OS=linux-gnu