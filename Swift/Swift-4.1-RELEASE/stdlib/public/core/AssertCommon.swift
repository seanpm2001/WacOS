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

import SwiftShims

// Implementation Note: this file intentionally uses very LOW-LEVEL
// CONSTRUCTS, so that assert and fatal may be used liberally in
// building library abstractions without fear of infinite recursion.
//
// FIXME: We could go farther with this simplification, e.g. avoiding
// UnsafeMutablePointer

@_inlineable // FIXME(sil-serialize-all)
@_transparent
public // @testable
func _isDebugAssertConfiguration() -> Bool {
  // The values for the assert_configuration call are:
  // 0: Debug
  // 1: Release
  // 2: Fast
  return Int32(Builtin.assert_configuration()) == 0
}

@_inlineable // FIXME(sil-serialize-all)
@_versioned
@_transparent
internal func _isReleaseAssertConfiguration() -> Bool {
  // The values for the assert_configuration call are:
  // 0: Debug
  // 1: Release
  // 2: Fast
  return Int32(Builtin.assert_configuration()) == 1
}

@_inlineable // FIXME(sil-serialize-all)
@_transparent
public // @testable
func _isFastAssertConfiguration() -> Bool {
  // The values for the assert_configuration call are:
  // 0: Debug
  // 1: Release
  // 2: Fast
  return Int32(Builtin.assert_configuration()) == 2
}

@_inlineable // FIXME(sil-serialize-all)
@_transparent
public // @testable
func _isStdlibInternalChecksEnabled() -> Bool {
#if INTERNAL_CHECKS_ENABLED
  return true
#else
  return false
#endif
}

@_inlineable // FIXME(sil-serialize-all)
@_versioned
@_transparent
internal
func _fatalErrorFlags() -> UInt32 {
  // The current flags are:
  // (1 << 0): Report backtrace on fatal error
#if os(iOS) || os(tvOS) || os(watchOS)
  return 0
#else
  return _isDebugAssertConfiguration() ? 1 : 0
#endif
}

/// This function should be used only in the implementation of user-level
/// assertions.
///
/// This function should not be inlined because it is cold and inlining just
/// bloats code.
@_versioned // FIXME(sil-serialize-all)
@inline(never)
internal func _assertionFailure(
  _ prefix: StaticString, _ message: StaticString,
  file: StaticString, line: UInt,
  flags: UInt32
) -> Never {
  prefix.withUTF8Buffer {
    (prefix) -> Void in
    message.withUTF8Buffer {
      (message) -> Void in
      file.withUTF8Buffer {
        (file) -> Void in
        _swift_stdlib_reportFatalErrorInFile(
          prefix.baseAddress!, CInt(prefix.count),
          message.baseAddress!, CInt(message.count),
          file.baseAddress!, CInt(file.count), UInt32(line),
          flags)
        Builtin.int_trap()
      }
    }
  }
  Builtin.int_trap()
}

/// This function should be used only in the implementation of user-level
/// assertions.
///
/// This function should not be inlined because it is cold and inlining just
/// bloats code.
@_versioned // FIXME(sil-serialize-all)
@inline(never)
internal func _assertionFailure(
  _ prefix: StaticString, _ message: String,
  file: StaticString, line: UInt,
  flags: UInt32
) -> Never {
  prefix.withUTF8Buffer {
    (prefix) -> Void in
    message._withUnsafeBufferPointerToUTF8 {
      (messageUTF8) -> Void in
      file.withUTF8Buffer {
        (file) -> Void in
        _swift_stdlib_reportFatalErrorInFile(
          prefix.baseAddress!, CInt(prefix.count),
          messageUTF8.baseAddress!, CInt(messageUTF8.count),
          file.baseAddress!, CInt(file.count), UInt32(line),
          flags)
      }
    }
  }

  Builtin.int_trap()
}

/// This function should be used only in the implementation of stdlib
/// assertions.
///
/// This function should not be inlined because it is cold and it inlining just
/// bloats code.
@_versioned // FIXME(sil-serialize-all)
@inline(never)
@_semantics("arc.programtermination_point")
internal func _fatalErrorMessage(
  _ prefix: StaticString, _ message: StaticString,
  file: StaticString, line: UInt,
  flags: UInt32
) -> Never {
#if INTERNAL_CHECKS_ENABLED
  prefix.withUTF8Buffer {
    (prefix) in
    message.withUTF8Buffer {
      (message) in
      file.withUTF8Buffer {
        (file) in
        _swift_stdlib_reportFatalErrorInFile(
          prefix.baseAddress!, CInt(prefix.count),
          message.baseAddress!, CInt(message.count),
          file.baseAddress!, CInt(file.count), UInt32(line),
          flags)
      }
    }
  }
#else
  prefix.withUTF8Buffer {
    (prefix) in
    message.withUTF8Buffer {
      (message) in
      _swift_stdlib_reportFatalError(
        prefix.baseAddress!, CInt(prefix.count),
        message.baseAddress!, CInt(message.count),
        flags)
    }
  }
#endif

  Builtin.int_trap()
}

/// Prints a fatal error message when an unimplemented initializer gets
/// called by the Objective-C runtime.
@_inlineable // FIXME(sil-serialize-all)
@_transparent
public // COMPILER_INTRINSIC
func _unimplementedInitializer(className: StaticString,
                               initName: StaticString = #function,
                               file: StaticString = #file,
                               line: UInt = #line,
                               column: UInt = #column
) -> Never {
  // This function is marked @_transparent so that it is inlined into the caller
  // (the initializer stub), and, depending on the build configuration,
  // redundant parameter values (#file etc.) are eliminated, and don't leak
  // information about the user's source.

  if _isDebugAssertConfiguration() {
    className.withUTF8Buffer {
      (className) in
      initName.withUTF8Buffer {
        (initName) in
        file.withUTF8Buffer {
          (file) in
          _swift_stdlib_reportUnimplementedInitializerInFile(
            className.baseAddress!, CInt(className.count),
            initName.baseAddress!, CInt(initName.count),
            file.baseAddress!, CInt(file.count),
            UInt32(line), UInt32(column),
            /*flags:*/ 0)
        }
      }
    }
  } else {
    className.withUTF8Buffer {
      (className) in
      initName.withUTF8Buffer {
        (initName) in
        _swift_stdlib_reportUnimplementedInitializer(
          className.baseAddress!, CInt(className.count),
          initName.baseAddress!, CInt(initName.count),
          /*flags:*/ 0)
      }
    }
  }

  Builtin.int_trap()
}

// FIXME(ABI)#21 (Type Checker): rename to something descriptive.
@_inlineable // FIXME(sil-serialize-all)
public // COMPILER_INTRINSIC
func _undefined<T>(
  _ message: @autoclosure () -> String = String(),
  file: StaticString = #file, line: UInt = #line
) -> T {
  _assertionFailure("Fatal error", message(), file: file, line: line, flags: 0)
}
