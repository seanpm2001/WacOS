//===--- Type.swift - Value type ------------------------------------------===//
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

public struct Type {
  var bridged: BridgedType
  
  public var isAddress: Bool { SILType_isAddress(bridged) != 0 }
  public var isObject: Bool { !isAddress }
  
  public func isTrivial(in function: Function) -> Bool {
    return SILType_isTrivial(bridged, function.bridged) != 0
  }
}
