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

/// Print a string as is to stdout.
public // COMPILER_INTRINSIC
func _replPrintLiteralString(_ text: String) {
  print(text, terminator: "")
}

/// Print the debug representation of `value`, followed by a newline.
@inline(never)
@_semantics("stdlib_binary_only")
public // COMPILER_INTRINSIC
func _replDebugPrintln<T>(_ value: T) {
  debugPrint(value)
}
