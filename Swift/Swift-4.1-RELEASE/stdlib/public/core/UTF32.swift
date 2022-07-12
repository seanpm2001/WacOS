//===--- UTF32.swift ------------------------------------------------------===//
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
extension Unicode {
  @_fixed_layout // FIXME(sil-serialize-all)
  public enum UTF32 {
  case _swift3Codec
  }
}

extension Unicode.UTF32 : Unicode.Encoding {
  public typealias CodeUnit = UInt32
  public typealias EncodedScalar = CollectionOfOne<UInt32>

  @_inlineable // FIXME(sil-serialize-all)
  public static var encodedReplacementCharacter : EncodedScalar {
    return EncodedScalar(0xFFFD)
  }

  @_inlineable // FIXME(sil-serialize-all)
  @inline(__always)
  public static func _isScalar(_ x: CodeUnit) -> Bool  {
    return true
  }

  @_inlineable // FIXME(sil-serialize-all)
  @inline(__always)
  public static func decode(_ source: EncodedScalar) -> Unicode.Scalar {
    return Unicode.Scalar(_unchecked: source.first!)
  }

  @_inlineable // FIXME(sil-serialize-all)
  @inline(__always)
  public static func encode(
    _ source: Unicode.Scalar
  ) -> EncodedScalar? {
    return EncodedScalar(source.value)
  }
  
  @_fixed_layout // FIXME(sil-serialize-all)
  public struct Parser {
    @_inlineable // FIXME(sil-serialize-all)
    public init() { }
  }
  
  public typealias ForwardParser = Parser
  public typealias ReverseParser = Parser
}

extension UTF32.Parser : Unicode.Parser {
  public typealias Encoding = Unicode.UTF32

  /// Parses a single Unicode scalar value from `input`.
  @_inlineable // FIXME(sil-serialize-all)
  public mutating func parseScalar<I : IteratorProtocol>(
    from input: inout I
  ) -> Unicode.ParseResult<Encoding.EncodedScalar>
  where I.Element == Encoding.CodeUnit {
    let n = input.next()
    if _fastPath(n != nil), let x = n {
      // Check code unit is valid: not surrogate-reserved and within range.
      guard _fastPath((x &>> 11) != 0b1101_1 && x <= 0x10ffff)
      else { return .error(length: 1) }
      
      // x is a valid scalar.
      return .valid(UTF32.EncodedScalar(x))
    }
    return .emptyInput
  }
}
