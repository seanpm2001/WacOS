//===--- Bincompat.h - Binary compatibility checks. -------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// Checks for enabling binary compatibility workarounds.
//
//===----------------------------------------------------------------------===//

namespace swift {

namespace runtime {

namespace bincompat {

/// Whether protocol conformance iteration should be reversed, to prefer
/// conformances from images that are later in the list over earlier ones.
/// Default is false starting with Swift 5.4.
bool workaroundProtocolConformanceReverseIteration();

/// Whether we should crash when we encounter a non-nullable Obj-C
/// reference with a null value as the source of a cast.
/// Default is true starting with Swift 5.4.
bool unexpectedObjCNullWhileCastingIsFatal();

/// Whether we should use the legacy semantics for casting nil optionals
/// to nested optionals
bool useLegacyOptionalNilInjection();

} // namespace bincompat

} // namespace runtime

} // namespace swift
