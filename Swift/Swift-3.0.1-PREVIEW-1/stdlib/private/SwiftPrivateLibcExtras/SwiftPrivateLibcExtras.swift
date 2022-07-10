//===--- SwiftPrivateLibcExtras.swift -------------------------------------===//
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

import SwiftPrivate
#if os(OSX) || os(iOS) || os(watchOS) || os(tvOS)
import Darwin
#elseif os(Linux) || os(FreeBSD) || os(PS4) || os(Android)
import Glibc
#endif

#if !os(Windows) || CYGWIN
public func _stdlib_mkstemps(_ template: inout String, _ suffixlen: CInt) -> CInt {
#if os(Android)
  preconditionFailure("mkstemps doesn't work on Android")
#else
  var utf8CStr = template.utf8CString
  let (fd, fileName) = utf8CStr.withUnsafeMutableBufferPointer {
    (utf8CStr) -> (CInt, String) in
    let fd = mkstemps(utf8CStr.baseAddress!, suffixlen)
    let fileName = String(cString: utf8CStr.baseAddress!)
    return (fd, fileName)
  }
  template = fileName
  return fd
#endif
}
#endif

public var _stdlib_FD_SETSIZE: CInt {
  return 1024
}

public struct _stdlib_fd_set {
  var _data: [UInt]
  static var _wordBits: Int {
    return MemoryLayout<UInt>.size * 8
  }

  public init() {
    _data = [UInt](
      repeating: 0,
      count: Int(_stdlib_FD_SETSIZE) / _stdlib_fd_set._wordBits)
  }

  public func isset(_ fd: CInt) -> Bool {
    let fdInt = Int(fd)
    return (
        _data[fdInt / _stdlib_fd_set._wordBits] &
          UInt(1 << (fdInt % _stdlib_fd_set._wordBits))
      ) != 0
  }

  public mutating func set(_ fd: CInt) {
    let fdInt = Int(fd)
    _data[fdInt / _stdlib_fd_set._wordBits] |=
      UInt(1 << (fdInt % _stdlib_fd_set._wordBits))
  }

  public mutating func clear(_ fd: CInt) {
    let fdInt = Int(fd)
    _data[fdInt / _stdlib_fd_set._wordBits] &=
      ~UInt(1 << (fdInt % _stdlib_fd_set._wordBits))
  }

  public mutating func zero() {
    let count = _data.count
    return _data.withUnsafeMutableBufferPointer {
      (_data) in
      for i in 0..<count {
        _data[i] = 0
      }
      return
    }
  }
}

#if !os(Windows) || CYGWIN
public func _stdlib_select(
  _ readfds: inout _stdlib_fd_set, _ writefds: inout _stdlib_fd_set,
  _ errorfds: inout _stdlib_fd_set, _ timeout: UnsafeMutablePointer<timeval>?
) -> CInt {
  return readfds._data.withUnsafeMutableBufferPointer {
    (readfds) in
    writefds._data.withUnsafeMutableBufferPointer {
      (writefds) in
      errorfds._data.withUnsafeMutableBufferPointer {
        (errorfds) in
        let readAddr = readfds.baseAddress
        let writeAddr = writefds.baseAddress
        let errorAddr = errorfds.baseAddress
        func asFdSetPtr(
          _ p: UnsafeMutablePointer<UInt>?
        ) -> UnsafeMutablePointer<fd_set>? {
          return UnsafeMutableRawPointer(p)?
            .assumingMemoryBound(to: fd_set.self)
        }
        return select(
          _stdlib_FD_SETSIZE,
          asFdSetPtr(readAddr),
          asFdSetPtr(writeAddr),
          asFdSetPtr(errorAddr),
          timeout)
      }
    }
  }
}
#endif

//
// Functions missing in `Darwin` module.
//
public func _WSTATUS(_ status: CInt) -> CInt {
  return status & 0x7f
}

public var _WSTOPPED: CInt {
  return 0x7f
}

public func WIFEXITED(_ status: CInt) -> Bool {
  return _WSTATUS(status) == 0
}

public func WIFSIGNALED(_ status: CInt) -> Bool {
  return _WSTATUS(status) != _WSTOPPED && _WSTATUS(status) != 0
}

public func WEXITSTATUS(_ status: CInt) -> CInt {
  return (status >> 8) & 0xff
}

public func WTERMSIG(_ status: CInt) -> CInt {
  return _WSTATUS(status)
}
