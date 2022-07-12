import APINotesTest

public struct S1 {
  public init(_ : Double) {}
  mutating public func foo1() {}
  mutating public func foo2() {}
  public static func foo3() {}
  public func foo4() {}
  public func foo5(x : Int, y: Int, z: Int) {}
}

public class C0 {
  public func foo4(a : Void?) {}
}

public class C1: C0 {
  public func foo1() {}
  public func foo2(_ : ()->()) {}
  public var CIIns1 : C1?
  public weak var CIIns2 : C1?
  public func foo3(a : ()?) {}
  public init(_ : C1) {}
}

public typealias C3 = C1

public struct NSSomestruct2 {
  public static func foo1(_ a : C3) {}
}

public class C4: NewType {}