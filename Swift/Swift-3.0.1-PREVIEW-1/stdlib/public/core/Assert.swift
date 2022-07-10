//===----------------------------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

/// Performs a traditional C-style assert with an optional message.
///
/// Use this function for internal sanity checks that are active during testing
/// but do not impact performance of shipping code. To check for invalid usage
/// in Release builds, see `precondition(_:_:file:line:)`.
///
/// * In playgrounds and `-Onone` builds (the default for Xcode's Debug
///   configuration): If `condition` evaluates to `false`, stop program
///   execution in a debuggable state after printing `message`.
///
/// * In `-O` builds (the default for Xcode's Release configuration),
///   `condition` is not evaluated, and there are no effects.
///
/// * In `-Ounchecked` builds, `condition` is not evaluated, but the optimizer
///   may assume that it *always* evaluates to `true`. Failure to satisfy that
///   assumption is a serious programming error.
///
/// - Parameters:
///   - condition: The condition to test. `condition` is only evaluated in
///     playgrounds and `-Onone` builds.
///   - message: A string to print if `condition` is evaluated to `false`. The
///     default is an empty string.
///   - file: The file name to print with `message` if the assertion fails. The
///     default is the file where `assert(_:_:file:line:)` is called.
///   - line: The line number to print along with `message` if the assertion
///     fails. The default is the line number where `assert(_:_:file:line:)`
///     is called.
@_transparent
public func assert(
  _ condition: @autoclosure () -> Bool,
  _ message: @autoclosure () -> String = String(),
  file: StaticString = #file, line: UInt = #line
) {
  // Only assert in debug mode.
  if _isDebugAssertConfiguration() {
    if !_branchHint(condition(), expected: true) {
      _assertionFailed("assertion failed", message(), file, line,
        flags: _fatalErrorFlags())
    }
  }
}

/// Checks a necessary condition for making forward progress.
///
/// Use this function to detect conditions that must prevent the program from
/// proceeding, even in shipping code.
///
/// * In playgrounds and `-Onone` builds (the default for Xcode's Debug
///   configuration): If `condition` evaluates to `false`, stop program
///   execution in a debuggable state after printing `message`.
///
/// * In `-O` builds (the default for Xcode's Release configuration): If
///   `condition` evaluates to `false`, stop program execution.
///
/// * In `-Ounchecked` builds, `condition` is not evaluated, but the optimizer
///   may assume that it *always* evaluates to `true`. Failure to satisfy that
///   assumption is a serious programming error.
///
/// - Parameters:
///   - condition: The condition to test. `condition` is not evaluated in
///     `-Ounchecked` builds.
///   - message: A string to print if `condition` is evaluated to `false` in a
///     playground or `-Onone` build. The default is an empty string.
///   - file: The file name to print with `message` if the precondition fails.
///     The default is the file where `precondition(_:_:file:line:)` is
///     called.
///   - line: The line number to print along with `message` if the assertion
///     fails. The default is the line number where
///     `precondition(_:_:file:line:)` is called.
@_transparent
public func precondition(
  _ condition: @autoclosure () -> Bool,
  _ message: @autoclosure () -> String = String(),
  file: StaticString = #file, line: UInt = #line
) {
  // Only check in debug and release mode.  In release mode just trap.
  if _isDebugAssertConfiguration() {
    if !_branchHint(condition(), expected: true) {
      _assertionFailed("precondition failed", message(), file, line,
        flags: _fatalErrorFlags())
    }
  } else if _isReleaseAssertConfiguration() {
    let error = !condition()
    Builtin.condfail(error._value)
  }
}

/// Indicates that an internal sanity check failed.
///
/// Use this function to stop the program, without impacting the performance of
/// shipping code, when control flow is not expected to reach the call---for
/// example, in the `default` case of a `switch` where you have knowledge that
/// one of the other cases must be satisfied. To protect code from invalid
/// usage in Release builds, see `preconditionFailure(_:file:line:)`.
///
/// * In playgrounds and -Onone builds (the default for Xcode's Debug
///   configuration), stop program execution in a debuggable state after
///   printing `message`.
///
/// * In -O builds, has no effect.
///
/// * In -Ounchecked builds, the optimizer may assume that this function is
///   never called. Failure to satisfy that assumption is a serious
///   programming error.
///
/// - Parameters:
///   - message: A string to print in a playground or `-Onone` build. The
///     default is an empty string.
///   - file: The file name to print with `message`. The default is the file
///     where `assertionFailure(_:file:line:)` is called.
///   - line: The line number to print along with `message`. The default is the
///     line number where `assertionFailure(_:file:line:)` is called.
@inline(__always)
public func assertionFailure(
  _ message: @autoclosure () -> String = String(),
  file: StaticString = #file, line: UInt = #line
) {
  if _isDebugAssertConfiguration() {
    _assertionFailed("fatal error", message(), file, line,
      flags: _fatalErrorFlags())
  }
  else if _isFastAssertConfiguration() {
    _conditionallyUnreachable()
  }
}

/// Indicates that a precondition was violated.
///
/// Use this function to stop the program when control flow can only reach the
/// call if your API was improperly used. This function's effects vary
/// depending on the build flag used:
///
/// * In playgrounds and `-Onone` builds (the default for Xcode's Debug
///   configuration), stops program execution in a debuggable state after
///   printing `message`.
///
/// * In `-O` builds (the default for Xcode's Release configuration), stops
///   program execution.
///
/// * In `-Ounchecked` builds, the optimizer may assume that this function is
///   never called. Failure to satisfy that assumption is a serious
///   programming error.
///
/// - Parameters:
///   - message: A string to print in a playground or `-Onone` build. The
///     default is an empty string.
///   - file: The file name to print with `message`. The default is the file
///     where `preconditionFailure(_:file:line:)` is called.
///   - line: The line number to print along with `message`. The default is the
///     line number where `preconditionFailure(_:file:line:)` is called.
@_transparent
public func preconditionFailure(
  _ message: @autoclosure () -> String = String(),
  file: StaticString = #file, line: UInt = #line
) -> Never {
  // Only check in debug and release mode.  In release mode just trap.
  if _isDebugAssertConfiguration() {
    _assertionFailed("fatal error", message(), file, line,
      flags: _fatalErrorFlags())
  } else if _isReleaseAssertConfiguration() {
    Builtin.int_trap()
  }
  _conditionallyUnreachable()
}

/// Unconditionally prints a given message and stops execution.
///
/// - Parameters:
///   - message: The string to print. The default is an empty string.
///   - file: The file name to print with `message`. The default is the file
///     where `fatalError(_:file:line:)` is called.
///   - line: The line number to print along with `message`. The default is the
///     line number where `fatalError(_:file:line:)` is called.
@_transparent
public func fatalError(
  _ message: @autoclosure () -> String = String(),
  file: StaticString = #file, line: UInt = #line
) -> Never {
  _assertionFailed("fatal error", message(), file, line,
    flags: _fatalErrorFlags())
}

/// Library precondition checks.
///
/// Library precondition checks are enabled in debug mode and release mode. When
/// building in fast mode they are disabled.  In release mode they don't print
/// an error message but just trap. In debug mode they print an error message
/// and abort.
@_transparent
public func _precondition(
  _ condition: @autoclosure () -> Bool, _ message: StaticString = StaticString(),
  file: StaticString = #file, line: UInt = #line
) {
  // Only check in debug and release mode. In release mode just trap.
  if _isDebugAssertConfiguration() {
    if !_branchHint(condition(), expected: true) {
      _fatalErrorMessage("fatal error", message, file, line,
        flags: _fatalErrorFlags())
    }
  } else if _isReleaseAssertConfiguration() {
    let error = !condition()
    Builtin.condfail(error._value)
  }
}

@_transparent
public func _preconditionFailure(
  _ message: StaticString = StaticString(),
  file: StaticString = #file, line: UInt = #line
) -> Never {
  _precondition(false, message, file: file, line: line)
  _conditionallyUnreachable()
}

/// If `error` is true, prints an error message in debug mode, traps in release
/// mode, and returns an undefined error otherwise.
/// Otherwise returns `result`.
@_transparent
public func _overflowChecked<T>(
  _ args: (T, Bool),
  file: StaticString = #file, line: UInt = #line
) -> T {
  let (result, error) = args
  if _isDebugAssertConfiguration() {
    if _branchHint(error, expected: false) {
      _fatalErrorMessage("fatal error", "Overflow/underflow", file, line,
        flags: _fatalErrorFlags())
    }
  } else {
    Builtin.condfail(error._value)
  }
  return result
}


/// Debug library precondition checks.
///
/// Debug library precondition checks are only on in debug mode. In release and
/// in fast mode they are disabled. In debug mode they print an error message
/// and abort.
/// They are meant to be used when the check is not comprehensively checking for
/// all possible errors.
@_transparent
public func _debugPrecondition(
  _ condition: @autoclosure () -> Bool, _ message: StaticString = StaticString(),
  file: StaticString = #file, line: UInt = #line
) {
  // Only check in debug mode.
  if _isDebugAssertConfiguration() {
    if !_branchHint(condition(), expected: true) {
      _fatalErrorMessage("fatal error", message, file, line,
        flags: _fatalErrorFlags())
    }
  }
}

@_transparent
public func _debugPreconditionFailure(
  _ message: StaticString = StaticString(),
  file: StaticString = #file, line: UInt = #line
) -> Never {
  if _isDebugAssertConfiguration() {
    _precondition(false, message, file: file, line: line)
  }
  _conditionallyUnreachable()
}

/// Internal checks.
///
/// Internal checks are to be used for checking correctness conditions in the
/// standard library. They are only enable when the standard library is built
/// with the build configuration INTERNAL_CHECKS_ENABLED enabled. Otherwise, the
/// call to this function is a noop.
@_transparent
public func _sanityCheck(
  _ condition: @autoclosure () -> Bool, _ message: StaticString = StaticString(),
  file: StaticString = #file, line: UInt = #line
) {
#if INTERNAL_CHECKS_ENABLED
  if !_branchHint(condition(), expected: true) {
    _fatalErrorMessage("fatal error", message, file, line,
      flags: _fatalErrorFlags())
  }
#endif
}

@_transparent
public func _sanityCheckFailure(
  _ message: StaticString = StaticString(),
  file: StaticString = #file, line: UInt = #line
) -> Never {
  _sanityCheck(false, message, file: file, line: line)
  _conditionallyUnreachable()
}
