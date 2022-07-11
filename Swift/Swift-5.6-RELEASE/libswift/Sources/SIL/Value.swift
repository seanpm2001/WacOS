//===--- Value.swift - the Value protocol ---------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

import SILBridging

public protocol Value : AnyObject, CustomStringConvertible {
  var uses: UseList { get }
  var type: Type { get }
  var definingInstruction: Instruction? { get }
}

extension Value {
  public var description: String {
    var s = SILNode_debugDescription(bridgedNode)
    return String(cString: s.c_str())
  }

  public var uses: UseList {
    return UseList(SILValue_firstUse(bridged))
  }

  public var type: Type {
    return Type(bridged: SILValue_getType(bridged))
  }

  public var hashable: HashableValue { ObjectIdentifier(self) }

  public var bridged: BridgedValue {
    BridgedValue(obj: SwiftObject(self as AnyObject))
  }
  var bridgedNode: BridgedNode {
    BridgedNode(obj: SwiftObject(self as AnyObject))
  }
}

public typealias HashableValue = ObjectIdentifier

public func ==(_ lhs: Value, _ rhs: Value) -> Bool {
  return lhs === rhs
}

extension BridgedValue {
  func getAs<T: AnyObject>(_ valueType: T.Type) -> T { obj.getAs(T.self) }
}

final class Undef : Value {
  public var definingInstruction: Instruction? { nil }
}

final class PlaceholderValue : Value {
  public var definingInstruction: Instruction? { nil }
}
