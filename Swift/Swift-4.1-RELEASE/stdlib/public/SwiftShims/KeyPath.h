//===--- KeyPath.h - ABI constants for key path objects ---------*- C++ -*-===//
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
//  Constants used in the layout of key path objects.
//
//===----------------------------------------------------------------------===//

#ifndef __SWIFT_SHIMS_KEYPATH_H__
#define __SWIFT_SHIMS_KEYPATH_H__

#include "SwiftStdint.h"

#ifdef __cplusplus
namespace swift {
extern "C" {
#endif

// Bitfields for the key path buffer header.

static const __swift_uint32_t _SwiftKeyPathBufferHeader_SizeMask
  = 0x00FFFFFFU;
static const __swift_uint32_t _SwiftKeyPathBufferHeader_TrivialFlag
  = 0x80000000U;
static const __swift_uint32_t _SwiftKeyPathBufferHeader_HasReferencePrefixFlag
  = 0x40000000U;
static const __swift_uint32_t _SwiftKeyPathBufferHeader_ReservedMask
  = 0x3F000000U;
  
// Bitfields for a key path component header.

static const __swift_uint32_t _SwiftKeyPathComponentHeader_PayloadMask
  = 0x1FFFFFFFU;

static const __swift_uint32_t _SwiftKeyPathComponentHeader_DiscriminatorMask
  = 0x60000000U;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_DiscriminatorShift
  = 29;

static const __swift_uint32_t _SwiftKeyPathComponentHeader_StructTag
  = 0;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_ComputedTag
  = 1;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_ClassTag
  = 2;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_OptionalTag
  = 3;
  
static const __swift_uint32_t _SwiftKeyPathComponentHeader_MaximumOffsetPayload
  = 0x1FFFFFFCU;
  
static const __swift_uint32_t _SwiftKeyPathComponentHeader_UnresolvedIndirectOffsetPayload
  = 0x1FFFFFFDU;
  
static const __swift_uint32_t _SwiftKeyPathComponentHeader_UnresolvedFieldOffsetPayload
  = 0x1FFFFFFEU;

static const __swift_uint32_t _SwiftKeyPathComponentHeader_OutOfLineOffsetPayload
  = 0x1FFFFFFFU;

static const __swift_uint32_t _SwiftKeyPathComponentHeader_OptionalChainPayload
  = 0;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_OptionalWrapPayload
  = 1;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_OptionalForcePayload
  = 2;

static const __swift_uint32_t _SwiftKeyPathComponentHeader_EndOfReferencePrefixFlag
  = 0x80000000U;
  
static const __swift_uint32_t _SwiftKeyPathComponentHeader_ComputedMutatingFlag
  = 0x10000000U;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_ComputedSettableFlag
  = 0x08000000U;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_ComputedIDByStoredPropertyFlag
  = 0x04000000U;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_ComputedIDByVTableOffsetFlag
  = 0x02000000U;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_ComputedHasArgumentsFlag
  = 0x01000000U;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_ComputedIDResolutionMask
  = 0x0000000FU;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_ComputedIDResolved
  = 0x00000000U;
static const __swift_uint32_t _SwiftKeyPathComponentHeader_ComputedIDUnresolvedIndirectPointer
  = 0x00000002U;


#ifdef __cplusplus
} // extern "C"
} // namespace swift
#endif

#endif // __SWIFT_SHIMS_KEYPATH_H__
