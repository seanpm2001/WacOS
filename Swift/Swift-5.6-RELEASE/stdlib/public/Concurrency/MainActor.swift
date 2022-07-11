//===----------------------------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

import Swift

/// A singleton actor whose executor is equivalent to the main
/// dispatch queue.
@available(SwiftStdlib 5.1, *)
@globalActor public final actor MainActor: GlobalActor {
  public static let shared = MainActor()

  @inlinable
  public nonisolated var unownedExecutor: UnownedSerialExecutor {
    #if compiler(>=5.5) && $BuiltinBuildMainExecutor
    return UnownedSerialExecutor(Builtin.buildMainActorExecutorRef())
    #else
    fatalError("Swift compiler is incompatible with this SDK version")
    #endif
  }

  @inlinable
  public static var sharedUnownedExecutor: UnownedSerialExecutor {
    #if compiler(>=5.5) && $BuiltinBuildMainExecutor
    return UnownedSerialExecutor(Builtin.buildMainActorExecutorRef())
    #else
    fatalError("Swift compiler is incompatible with this SDK version")
    #endif
  }

  @inlinable
  public nonisolated func enqueue(_ job: UnownedJob) {
    _enqueueOnMain(job)
  }
}

@available(SwiftStdlib 5.1, *)
extension MainActor {
  /// Execute the given body closure on the main actor.
  ///
  /// Historical ABI entry point, superceded by the Sendable version that is
  /// also inlined to back-deploy a semantic fix where this operation would
  /// not hop back at the end.
  @usableFromInline
  static func run<T>(
    resultType: T.Type = T.self,
    body: @MainActor @Sendable () throws -> T
  ) async rethrows -> T {
    return try await body()
  }

  /// Execute the given body closure on the main actor.
  @_alwaysEmitIntoClient
  public static func run<T: Sendable>(
    resultType: T.Type = T.self,
    body: @MainActor @Sendable () throws -> T
  ) async rethrows -> T {
    return try await body()
  }
}
