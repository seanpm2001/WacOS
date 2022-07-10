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

import SwiftShims

public struct _FDInputStream {
  public let fd: CInt
  public var isClosed: Bool = false
  public var isEOF: Bool = false
  internal var _buffer = [UInt8](repeating: 0, count: 256)
  internal var _bufferUsed: Int = 0

  public init(fd: CInt) {
    self.fd = fd
  }

  public mutating func getline() -> String? {
    if let newlineIndex =
      _buffer[0..<_bufferUsed].index(of: UInt8(UnicodeScalar("\n").value)) {
      let result = String._fromWellFormedCodeUnitSequence(
        UTF8.self, input: _buffer[0..<newlineIndex])
      _buffer.removeSubrange(0...newlineIndex)
      _bufferUsed -= newlineIndex + 1
      return result
    }
    if isEOF && _bufferUsed > 0 {
      let result = String._fromWellFormedCodeUnitSequence(
        UTF8.self, input: _buffer[0..<_bufferUsed])
      _buffer.removeAll()
      _bufferUsed = 0
      return result
    }
    return nil
  }

  public mutating func read() {
    let minFree = 128
    var bufferFree = _buffer.count - _bufferUsed
    if bufferFree < minFree {
      _buffer.reserveCapacity(minFree - bufferFree)
      while bufferFree < minFree {
        _buffer.append(0)
        bufferFree += 1
      }
    }
    let readResult: __swift_ssize_t = _buffer.withUnsafeMutableBufferPointer {
      (_buffer) in
      let fd = self.fd
      let addr = _buffer.baseAddress! + self._bufferUsed
      let size = bufferFree
      return _swift_stdlib_read(fd, addr, size)
    }
    if readResult == 0 {
      isEOF = true
      return
    }
    if readResult < 0 {
      fatalError("read() returned error")
    }
    _bufferUsed += readResult
  }

  public mutating func close() {
    if isClosed {
      return
    }
    let result = _swift_stdlib_close(fd)
    if result < 0 {
      fatalError("close() returned an error")
    }
    isClosed = true
  }
}

public struct _Stderr : TextOutputStream {
  public init() {}

  public mutating func write(_ string: String) {
    for c in string.utf8 {
      _swift_stdlib_putc_stderr(CInt(c))
    }
  }
}

public struct _FDOutputStream : TextOutputStream {
  public let fd: CInt
  public var isClosed: Bool = false

  public init(fd: CInt) {
    self.fd = fd
  }

  public mutating func write(_ string: String) {
    let utf8CStr = string.utf8CString
    utf8CStr.withUnsafeBufferPointer {
      (utf8CStr) -> Void in
      var writtenBytes = 0
      let bufferSize = utf8CStr.count - 1
      while writtenBytes != bufferSize {
        let result = _swift_stdlib_write(
          self.fd, UnsafeRawPointer(utf8CStr.baseAddress! + Int(writtenBytes)),
          bufferSize - writtenBytes)
        if result < 0 {
          fatalError("write() returned an error")
        }
        writtenBytes += result
      }
    }
  }

  public mutating func close() {
    if isClosed {
      return
    }
    let result = _swift_stdlib_close(fd)
    if result < 0 {
      fatalError("close() returned an error")
    }
    isClosed = true
  }
}
