@objc public class ForwardClass : NSObject {
  override public init() {}
}

@objc public protocol ForwardProto : NSObjectProtocol {
}
@objc public class ForwardProtoAdopter : NSObject, ForwardProto {
  override public init() {}
}

@objc public class PartialBaseClass {
}
@objc public class PartialSubClass : NSObject {
}

public class ProtoConformer : ForwardClassUser {
  @objc public func consumeForwardClass(_ arg: ForwardClass) {}

  @objc public var forward = ForwardClass()
  public init() {}
}

public func testProtocolWrapper(_ conformer: ForwardClassUser) {
   conformer.consumeForwardClass(conformer.forward)
}

public func testStruct(_ p: Point) -> Point {
   var result = p
   result.y += 5
   return result
}

public class Derived : Base {
   public override func safeOverride(_ arg: NSObject) -> ForwardClass {
      return ForwardClass()
   }
}

public func rdar16923405(_ a: AALevel) {}

