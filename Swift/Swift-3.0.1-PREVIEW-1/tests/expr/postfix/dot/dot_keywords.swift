// RUN: %target-parse-verify-swift

let x: Int = 1
let y: Int = x.self
let int: Int.Type = Int.self


// SE-0071 - Allow (most) keywords in member references
// https://github.com/apple/swift-evolution/blob/master/proposals/0071-member-keywords.md

struct SE0071Struct {
  var `default` : Int
}

func f1(a : SE0071Struct) -> Int {
  return a.default
}

func f2(a : SE0071Struct) -> Int {
  return a.`default`
}

  
enum SE0071Enum {
  case `case`
}

func f2() -> SE0071Enum {
  return .case
}

class SE0071Base {
  func `default`() {}
}

class SE0071Derived : SE0071Base {
  func zonk() {
    super.default()
  }
}
