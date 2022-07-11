@inline(__always) @inlinable public func testAlwaysInline(x: Bool) -> Bool {
  return x
}

@frozen
public struct AlwaysInlineInitStruct {
  @usableFromInline
  var x: Bool

  @inline(__always) @inlinable
  public init(x x2: Bool) {
    self.x = x2
  }
}

