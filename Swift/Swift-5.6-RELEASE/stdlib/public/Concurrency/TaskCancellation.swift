//===----------------------------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

import Swift
@_implementationOnly import _SwiftConcurrencyShims

// ==== Task Cancellation ------------------------------------------------------

/// Execute an operation with a cancellation handler that's immediately
/// invoked if the current task is canceled.
///
/// This differs from the operation cooperatively checking for cancellation
/// and reacting to it in that the cancellation handler is _always_ and
/// _immediately_ invoked when the task is canceled. For example, even if the
/// operation is running code that never checks for cancellation, a cancellation
/// handler still runs and provides a chance to run some cleanup code.
///
/// Doesn't check for cancellation, and always executes the passed `operation`.
///
/// This function returns immediately and never suspends.
@available(SwiftStdlib 5.1, *)
public func withTaskCancellationHandler<T>(
  operation: () async throws -> T,
  onCancel handler: @Sendable () -> Void
) async rethrows -> T {
  // unconditionally add the cancellation record to the task.
  // if the task was already cancelled, it will be executed right away.
  let record = _taskAddCancellationHandler(handler: handler)
  defer { _taskRemoveCancellationHandler(record: record) }

  return try await operation()
}

@available(SwiftStdlib 5.1, *)
extension Task {
  /// A Boolean value that indicates whether the task should stop executing.
  ///
  /// After the value of this property becomes `true`, it remains `true` indefinitely.
  /// There is no way to uncancel a task.
  ///
  /// - SeeAlso: `checkCancellation()`
  @_transparent public var isCancelled: Bool {
    _taskIsCancelled(_task)
  }
}

@available(SwiftStdlib 5.1, *)
extension Task where Success == Never, Failure == Never {
  /// A Boolean value that indicates whether the task should stop executing.
  ///
  /// After the value of this property becomes `true`, it remains `true` indefinitely.
  /// There is no way to uncancel a task.
  ///
  /// - SeeAlso: `checkCancellation()`
  public static var isCancelled: Bool {
     withUnsafeCurrentTask { task in
       task?.isCancelled ?? false
     }
  }
}

@available(SwiftStdlib 5.1, *)
extension Task where Success == Never, Failure == Never {
  /// Throws an error if the task was canceled.
  ///
  /// The error is always an instance of `CancellationError`.
  ///
  /// - SeeAlso: `isCancelled()`
  public static func checkCancellation() throws {
    if Task<Never, Never>.isCancelled {
      throw _Concurrency.CancellationError()
    }
  }
}

/// An error that indicates a task was canceled.
///
/// This error is also thrown automatically by `Task.checkCancellation()`,
/// if the current task has been canceled.
@available(SwiftStdlib 5.1, *)
public struct CancellationError: Error {
  // no extra information, cancellation is intended to be light-weight
  public init() {}
}

@available(SwiftStdlib 5.1, *)
@_silgen_name("swift_task_addCancellationHandler")
func _taskAddCancellationHandler(handler: () -> Void) -> UnsafeRawPointer /*CancellationNotificationStatusRecord*/

@available(SwiftStdlib 5.1, *)
@_silgen_name("swift_task_removeCancellationHandler")
func _taskRemoveCancellationHandler(
  record: UnsafeRawPointer /*CancellationNotificationStatusRecord*/
)
