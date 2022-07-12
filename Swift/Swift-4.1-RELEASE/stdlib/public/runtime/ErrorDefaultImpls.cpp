//===--- ErrorDefaultImpls.cpp - Error default implementations ------------===//
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
//
// This implements helpers for the default implementations of Error protocol
// members.
//
//===----------------------------------------------------------------------===//

#include "swift/Runtime/Config.h"
#include "swift/Runtime/Metadata.h"
using namespace swift;

// @_silgen_name("_swift_stdlib_getDefaultErrorCode")
// func _getDefaultErrorCode<T : Error>(_ x: T) -> Int
SWIFT_CC(swift) SWIFT_RUNTIME_STDLIB_SPI
intptr_t _swift_stdlib_getDefaultErrorCode(OpaqueValue *error,
                                           const Metadata *T,
                                           const WitnessTable *Error) {
  intptr_t result;

  switch (T->getKind()) {
    case MetadataKind::Enum: 
      // Enum tags use negative values for payload cases, so adjust code to be
      // in the range [0, num-cases).
      result = T->vw_getEnumTag(error) +
        T->getNominalTypeDescriptor()->Enum.getNumPayloadCases();
      break;

    case MetadataKind::Class:
    case MetadataKind::ObjCClassWrapper:
    case MetadataKind::ForeignClass:
    case MetadataKind::Function:
    case MetadataKind::Struct:
    case MetadataKind::Optional:
    case MetadataKind::Opaque:
    case MetadataKind::Tuple:
    case MetadataKind::Existential:
    case MetadataKind::Metatype:
    case MetadataKind::ExistentialMetatype:
    case MetadataKind::HeapLocalVariable:
    case MetadataKind::HeapGenericLocalVariable:
    case MetadataKind::ErrorObject:
      result = 1;
      break;
  }

  // Destroy the value.
  T->vw_destroy(error);
  return result;
}
