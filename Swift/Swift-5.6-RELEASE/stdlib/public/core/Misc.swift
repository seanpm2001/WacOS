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
// Extern C functions
//===----------------------------------------------------------------------===//

// FIXME: Once we have an FFI interface, make these have proper function bodies

/// Returns if `x` is a power of 2.
@_transparent
public // @testable
func _isPowerOf2(_ x: UInt) -> Bool {
  if x == 0 {
    return false
  }
  // Note: use unchecked subtraction because we have checked that `x` is not
  // zero.
  return x & (x &- 1) == 0
}

/// Returns if `x` is a power of 2.
@_transparent
public // @testable
func _isPowerOf2(_ x: Int) -> Bool {
  if x <= 0 {
    return false
  }
  // Note: use unchecked subtraction because we have checked that `x` is not
  // `Int.min`.
  return x & (x &- 1) == 0
}

#if _runtime(_ObjC)
@_transparent
public func _autorelease(_ x: AnyObject) {
  Builtin.retain(x)
  Builtin.autorelease(x)
}
#endif

// FIXME(ABI)#51 : this API should allow controlling different kinds of
// qualification separately: qualification with module names and qualification
// with type names that we are nested in.
// But we can place it behind #if _runtime(_Native) and remove it from ABI on
// Apple platforms, deferring discussions mentioned above.
@_silgen_name("swift_getTypeName")
public func _getTypeName(_ type: Any.Type, qualified: Bool)
  -> (UnsafePointer<UInt8>, Int)

/// Returns the demangled qualified name of a metatype.
@_semantics("typeName")
public // @testable
func _typeName(_ type: Any.Type, qualified: Bool = true) -> String {
  let (stringPtr, count) = _getTypeName(type, qualified: qualified)
  return String._fromUTF8Repairing(
    UnsafeBufferPointer(start: stringPtr, count: count)).0
}

@available(SwiftStdlib 5.3, *)
@_silgen_name("swift_getMangledTypeName")
public func _getMangledTypeName(_ type: Any.Type)
  -> (UnsafePointer<UInt8>, Int)

/// Returns the mangled name for a given type.
@available(SwiftStdlib 5.3, *)
public // SPI
func _mangledTypeName(_ type: Any.Type) -> String? {
  let (stringPtr, count) = _getMangledTypeName(type)
  guard count > 0 else {
    return nil
  }

  let (result, repairsMade) = String._fromUTF8Repairing(
      UnsafeBufferPointer(start: stringPtr, count: count))

  _precondition(!repairsMade, "repairs made to _mangledTypeName, this is not expected since names should be valid UTF-8")

  return result
}

/// Lookup a class given a name. Until the demangled encoding of type
/// names is stabilized, this is limited to top-level class names (Foo.bar).
public // SPI(Foundation)
func _typeByName(_ name: String) -> Any.Type? {
  let nameUTF8 = Array(name.utf8)
  return nameUTF8.withUnsafeBufferPointer { (nameUTF8) in
    return  _getTypeByMangledNameUntrusted(nameUTF8.baseAddress!,
                                  UInt(nameUTF8.endIndex))
  }
}

@_silgen_name("swift_stdlib_getTypeByMangledNameUntrusted")
internal func _getTypeByMangledNameUntrusted(
  _ name: UnsafePointer<UInt8>,
  _ nameLength: UInt)
  -> Any.Type?

@_silgen_name("swift_getTypeByMangledNameInEnvironment")
public func _getTypeByMangledNameInEnvironment(
  _ name: UnsafePointer<UInt8>,
  _ nameLength: UInt,
  genericEnvironment: UnsafeRawPointer?,
  genericArguments: UnsafeRawPointer?)
  -> Any.Type?

@_silgen_name("swift_getTypeByMangledNameInContext")
public func _getTypeByMangledNameInContext(
  _ name: UnsafePointer<UInt8>,
  _ nameLength: UInt,
  genericContext: UnsafeRawPointer?,
  genericArguments: UnsafeRawPointer?)
  -> Any.Type?

/// Prevents performance diagnostics in the passed closure.
@_alwaysEmitIntoClient
@_semantics("no_performance_analysis")
public func _unsafePerformance<T>(_ c: () -> T) -> T {
  return c()
}
