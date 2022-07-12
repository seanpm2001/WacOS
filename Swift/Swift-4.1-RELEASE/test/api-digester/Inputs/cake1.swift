import APINotesTest

public struct S1 {
  public init(_ : Int) {}
  public func foo1() {}
  mutating public func foo2() {}
  public func foo3() {}
  public func foo4() -> Void {}
  public func foo5(x : Int, y: Int) {}
}

public class C1 {
  public class func foo1() {}
  public func foo2(_ : Int) {}
  public weak var CIIns1 : C1?
  public var CIIns2 : C1?
  public func foo3(a : Void?) {}
  public func foo4(a : Void?) {}
}

public class C3 {}

public struct Somestruct2 {
  public init(_ : C1) {}
  public static func foo1(_ a : C3) {}
}

public class C4: OldType {
  public func foo() {}
}