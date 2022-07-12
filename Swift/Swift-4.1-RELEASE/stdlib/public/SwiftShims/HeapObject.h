//===--- HeapObject.h -------------------------------------------*- C++ -*-===//
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
#ifndef SWIFT_STDLIB_SHIMS_HEAPOBJECT_H
#define SWIFT_STDLIB_SHIMS_HEAPOBJECT_H

#include "RefCount.h"
#include "System.h"

#define SWIFT_ABI_HEAP_OBJECT_HEADER_SIZE_64 16
#define SWIFT_ABI_HEAP_OBJECT_HEADER_SIZE_32 8

#ifdef __cplusplus
#include <type_traits>
#include "swift/Basic/type_traits.h"

namespace swift {

struct InProcess;

template <typename Target> struct TargetHeapMetadata;
using HeapMetadata = TargetHeapMetadata<InProcess>;
#else
typedef struct HeapMetadata HeapMetadata;
#endif

// The members of the HeapObject header that are not shared by a
// standard Objective-C instance
#define SWIFT_HEAPOBJECT_NON_OBJC_MEMBERS       \
  InlineRefCounts refCounts

/// The Swift heap-object header.
/// This must match RefCountedStructTy in IRGen.
struct HeapObject {
  /// This is always a valid pointer to a metadata object.
  HeapMetadata const *metadata;

  SWIFT_HEAPOBJECT_NON_OBJC_MEMBERS;

#ifdef __cplusplus
  HeapObject() = default;

  // Initialize a HeapObject header as appropriate for a newly-allocated object.
  constexpr HeapObject(HeapMetadata const *newMetadata) 
    : metadata(newMetadata)
    , refCounts(InlineRefCounts::Initialized)
  { }
#endif // __cplusplus
};

#ifdef __cplusplus
extern "C" {
#endif

SWIFT_RUNTIME_STDLIB_INTERFACE
void _swift_instantiateInertHeapObject(void *address,
                                       const HeapMetadata *metadata);

#ifdef __cplusplus
} // extern "C"
#endif

#ifdef __cplusplus
static_assert(swift::IsTriviallyConstructible<HeapObject>::value,
              "HeapObject must be trivially initializable");
static_assert(std::is_trivially_destructible<HeapObject>::value,
              "HeapObject must be trivially destructible");

static_assert(sizeof(HeapObject) == 2*sizeof(void*),
              "HeapObject must be two pointers long");

static_assert(alignof(HeapObject) == alignof(void*),
              "HeapObject must be pointer-aligned");

} // end namespace swift
#endif // __cplusplus

/// Global bit masks

// TODO(<rdar://problem/34837179>): Convert each macro below to static consts
// when static consts are visible to SIL.

// The extra inhabitants and spare bits of heap object pointers.
// These must align with the values in IRGen's SwiftTargetInfo.cpp.
#if defined(__x86_64__)

#ifdef __APPLE__
#define _swift_abi_LeastValidPointerValue                                      \
  (__swift_uintptr_t) SWIFT_ABI_DARWIN_X86_64_LEAST_VALID_POINTER
#else
#define _swift_abi_LeastValidPointerValue                                      \
  (__swift_uintptr_t) SWIFT_ABI_DEFAULT_LEAST_VALID_POINTER
#endif
#define _swift_abi_SwiftSpareBitsMask                                          \
  (__swift_uintptr_t) SWIFT_ABI_X86_64_SWIFT_SPARE_BITS_MASK
#define _swift_abi_ObjCReservedBitsMask                                        \
  (__swift_uintptr_t) SWIFT_ABI_X86_64_OBJC_RESERVED_BITS_MASK
#define _swift_abi_ObjCReservedLowBits                                         \
  (unsigned) SWIFT_ABI_X86_64_OBJC_NUM_RESERVED_LOW_BITS

#elif defined(__arm64__)

#ifdef __APPLE__
#define _swift_abi_LeastValidPointerValue                                      \
  (__swift_uintptr_t) SWIFT_ABI_DARWIN_ARM64_LEAST_VALID_POINTER
#else
#define _swift_abi_LeastValidPointerValue                                      \
  (__swift_uintptr_t) SWIFT_ABI_DEFAULT_LEAST_VALID_POINTER
#endif
#define _swift_abi_SwiftSpareBitsMask                                          \
  (__swift_uintptr_t) SWIFT_ABI_ARM64_SWIFT_SPARE_BITS_MASK
#define _swift_abi_ObjCReservedBitsMask                                        \
  (__swift_uintptr_t) SWIFT_ABI_ARM64_OBJC_RESERVED_BITS_MASK
#define _swift_abi_ObjCReservedLowBits                                         \
  (unsigned) SWIFT_ABI_ARM64_OBJC_NUM_RESERVED_LOW_BITS

#elif defined(__powerpc64__)

#define _swift_abi_LeastValidPointerValue                                      \
  (__swift_uintptr_t) SWIFT_ABI_DEFAULT_LEAST_VALID_POINTER
#define _swift_abi_SwiftSpareBitsMask                                          \
  (__swift_uintptr_t) SWIFT_ABI_POWERPC64_SWIFT_SPARE_BITS_MASK
#define _swift_abi_ObjCReservedBitsMask                                        \
  (__swift_uintptr_t) SWIFT_ABI_DEFAULT_OBJC_RESERVED_BITS_MASK
#define _swift_abi_ObjCReservedLowBits                                         \
  (unsigned) SWIFT_ABI_DEFAULT_OBJC_NUM_RESERVED_LOW_BITS

#elif defined(__s390x__)

#define _swift_abi_LeastValidPointerValue                                      \
  (__swift_uintptr_t) SWIFT_ABI_DEFAULT_LEAST_VALID_POINTER
#define _swift_abi_SwiftSpareBitsMask                                          \
  (__swift_uintptr_t) SWIFT_ABI_S390X_SWIFT_SPARE_BITS_MASK
#define _swift_abi_ObjCReservedBitsMask                                        \
  (__swift_uintptr_t) SWIFT_ABI_DEFAULT_OBJC_RESERVED_BITS_MASK
#define _swift_abi_ObjCReservedLowBits                                         \
  (unsigned) SWIFT_ABI_DEFAULT_OBJC_NUM_RESERVED_LOW_BITS

#else

#define _swift_abi_LeastValidPointerValue                                      \
  (__swift_uintptr_t) SWIFT_ABI_DEFAULT_LEAST_VALID_POINTER

#if __i386__
#define _swift_abi_SwiftSpareBitsMask                                          \
  (__swift_uintptr_t) SWIFT_ABI_I386_SWIFT_SPARE_BITS_MASK
#elif __arm__
#define _swift_abi_SwiftSpareBitsMask                                          \
  (__swift_uintptr_t) SWIFT_ABI_ARM_SWIFT_SPARE_BITS_MASK
#else
#define _swift_abi_SwiftSpareBitsMask                                          \
  (__swift_uintptr_t) SWIFT_ABI_DEFAULT_SWIFT_SPARE_BITS_MASK
#endif

#define _swift_abi_ObjCReservedBitsMask                                        \
  (__swift_uintptr_t) SWIFT_ABI_DEFAULT_OBJC_RESERVED_BITS_MASK
#define _swift_abi_ObjCReservedLowBits                                         \
  (unsigned) SWIFT_ABI_DEFAULT_OBJC_NUM_RESERVED_LOW_BITS
#endif

/// Corresponding namespaced decls
#ifdef __cplusplus
namespace heap_object_abi {
static const __swift_uintptr_t LeastValidPointerValue =
    _swift_abi_LeastValidPointerValue;
static const __swift_uintptr_t SwiftSpareBitsMask =
    _swift_abi_SwiftSpareBitsMask;
static const __swift_uintptr_t ObjCReservedBitsMask =
    _swift_abi_ObjCReservedBitsMask;
static const unsigned ObjCReservedLowBits = _swift_abi_ObjCReservedLowBits;
} // heap_object_abi
#endif // __cplusplus

/// BridgeObject masks

#define _swift_BridgeObject_TaggedPointerBits _swift_abi_ObjCReservedBitsMask


#endif // SWIFT_STDLIB_SHIMS_HEAPOBJECT_H
