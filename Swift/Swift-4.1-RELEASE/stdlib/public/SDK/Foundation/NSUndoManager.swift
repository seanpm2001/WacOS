//===----------------------------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

@_exported import Foundation // Clang module
import _SwiftFoundationOverlayShims

extension UndoManager {
  @available(*, unavailable, renamed: "registerUndo(withTarget:handler:)")
  public func registerUndoWithTarget<TargetType : AnyObject>(_ target: TargetType, handler: (TargetType) -> Void) {
    fatalError("This API has been renamed")
  }

  @available(OSX 10.11, iOS 9.0, *)
  public func registerUndo<TargetType : AnyObject>(withTarget target: TargetType, handler: @escaping (TargetType) -> Void) {
    __NSUndoManagerRegisterWithTargetHandler( self, target) { internalTarget in
      handler(internalTarget as! TargetType)
    }
  }
}
