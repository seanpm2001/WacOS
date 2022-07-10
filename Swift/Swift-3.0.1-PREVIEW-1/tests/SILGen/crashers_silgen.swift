// RUN: %target-swift-frontend -emit-silgen -o /dev/null %s

// <rdar://22000564> Crash on Subscript taking a tuple argument list
class r22000564 {
  subscript (position: (Int, Int)) -> Int {
    get { return 32 }
    set {}
  }
  subscript(native native: Int) -> Int {
    get { return native }
    set {}
  }
  subscript (position position: (Int, Int)) -> Int {
    get { return 32 }
    set {}
  }
}
