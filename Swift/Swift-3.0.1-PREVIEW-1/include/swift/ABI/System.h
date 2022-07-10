//===--- System.h - Swift ABI system-specific constants ---------*- C++ -*-===//
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
//
// Here are some fun facts about the target platforms we support!
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_ABI_SYSTEM_H
#define SWIFT_ABI_SYSTEM_H

// In general, these macros are expected to expand to host-independent
// integer constant expressions.  This allows the same data to feed
// both the compiler and runtime implementation.

/******************************* Default Rules ********************************/

/// The least valid pointer value for an actual pointer (as opposed to
/// Objective-C pointers, which may be tagged pointers and are covered
/// separately).  Values up to this are "extra inhabitants" of the
/// pointer representation, and payloaded enum types can take
/// advantage of that as they see fit.
///
/// By default, we assume that there's at least an unmapped page at
/// the bottom of the address space.  4K is a reasonably likely page
/// size.
///
/// The minimum possible value for this macro is 1; we always assume
/// that the null representation is available.
#define SWIFT_ABI_DEFAULT_LEAST_VALID_POINTER 4096

/// The bitmask of spare bits in a function pointer.
#define SWIFT_ABI_DEFAULT_FUNCTION_SPARE_BITS_MASK 0

/// The bitmask of spare bits in a Swift heap object pointer.  A Swift
/// heap object allocation will never set any of these bits.
#define SWIFT_ABI_DEFAULT_SWIFT_SPARE_BITS_MASK 0

/// The bitmask of reserved bits in an Objective-C object pointer.
/// By default we assume the ObjC runtime doesn't use tagged pointers.
#define SWIFT_ABI_DEFAULT_OBJC_RESERVED_BITS_MASK 0

/// The number of low bits in an Objective-C object pointer that
/// are reserved by the Objective-C runtime.
#define SWIFT_ABI_DEFAULT_OBJC_NUM_RESERVED_LOW_BITS 0

/// The ObjC runtime will not use pointer values for which
/// ``pointer & SWIFT_ABI_XXX_OBJC_RESERVED_BITS_MASK == 0 && 
/// pointer & SWIFT_ABI_XXX_SWIFT_SPARE_BITS_MASK != 0``.

/*********************************** i386 *************************************/

// Heap objects are pointer-aligned, so the low two bits are unused.
#define SWIFT_ABI_I386_SWIFT_SPARE_BITS_MASK 0x00000003U

/*********************************** arm **************************************/

// Heap objects are pointer-aligned, so the low two bits are unused.
#define SWIFT_ABI_ARM_SWIFT_SPARE_BITS_MASK 0x00000003U

/*********************************** x86-64 ***********************************/

/// Darwin reserves the low 4GB of address space.
#define SWIFT_ABI_DARWIN_X86_64_LEAST_VALID_POINTER (4ULL*1024*1024*1024)

// Only the bottom 56 bits are used, and heap objects are eight-byte-aligned.
#define SWIFT_ABI_X86_64_SWIFT_SPARE_BITS_MASK 0xFF00000000000007ULL

// Objective-C reserves the high and low bits for tagged pointers.
// Systems exist which use either bit.
#define SWIFT_ABI_X86_64_OBJC_RESERVED_BITS_MASK 0x8000000000000001ULL
#define SWIFT_ABI_X86_64_OBJC_NUM_RESERVED_LOW_BITS 1

/*********************************** arm64 ************************************/

/// Darwin reserves the low 4GB of address space.
#define SWIFT_ABI_DARWIN_ARM64_LEAST_VALID_POINTER (4ULL*1024*1024*1024)

// TBI guarantees the top byte of pointers is unused.
// Heap objects are eight-byte aligned.
#define SWIFT_ABI_ARM64_SWIFT_SPARE_BITS_MASK 0xFF00000000000007ULL

// Objective-C reserves just the high bit for tagged pointers.
#define SWIFT_ABI_ARM64_OBJC_RESERVED_BITS_MASK 0x8000000000000000ULL
#define SWIFT_ABI_ARM64_OBJC_NUM_RESERVED_LOW_BITS 0

/*********************************** powerpc64 ********************************/

// Heap objects are pointer-aligned, so the low three bits are unused.
#define SWIFT_ABI_POWERPC64_SWIFT_SPARE_BITS_MASK 0x0000000000000007ULL

/*********************************** s390x ************************************/

// Top byte of pointers is unused, and heap objects are eight-byte aligned.
#define SWIFT_ABI_S390X_SWIFT_SPARE_BITS_MASK 0x0000000000000007ULL

#endif /* SWIFT_ABI_SYSTEM_H */
