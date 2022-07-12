// RUN: not %swift -emit-sil -target %target-triple %s -emit-fixits-path %t.remap -I %S/Inputs -diagnostics-editor-mode
// RUN: c-arcmt-test %t.remap | arcmt-test -verify-transformed-files %s.result

enum E1 : Int {
  case e1
  case e2
  case e3
  case e4
}

func foo1(_ e : E1) -> Int {
  switch(e) {
  case .e1:
    return 1
  }
}

func foo2(_ i : Int) -> Int {
  switch i {
  case 1:
    return 1
  }
}

func foo3(_ c : Character) -> Character {
  switch c {
  case "a":
    return "a"
  }
}

enum E2 {
  case e1(a: Int, s: Int)
  case e2(a: Int)
  case e3(a: Int)
  case e4(_: Int)
  case e5(_: Int, _: Int)
  case e6(a : Int, _: Int)
  case e7
  case e8(a : Int, Int, Int)
  case e9(Int, Int, Int)
}

func foo4(_ e : E2) -> Int {
  switch e {
  case .e2:
    return 1
  }
}

func foo5(_ e : E1) -> Int {
  switch e {
  case _ where e.rawValue > 0:
    return 1
  }
}

func foo6(_ e : E2) -> Int {
  switch e {
  case let .e1(x, y):
    return x + y
  }
}

func foo7(_ e : E2) -> Int {
  switch e {
  case .e2(1): return 0
  case .e1: return 0
  case .e3: return 0
  }
}
