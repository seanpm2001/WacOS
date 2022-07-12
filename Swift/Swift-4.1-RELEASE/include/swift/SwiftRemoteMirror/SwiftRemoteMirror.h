//===--- SwiftRemoteMirror.h - Public remote reflection interf. -*- C++ -*-===//
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
///
/// \file
/// This header declares functions in the libswiftReflection library,
/// which provides mechanisms for reflecting heap information in a
/// remote Swift process.
///
//===----------------------------------------------------------------------===//

#ifndef SWIFT_REFLECTION_SWIFT_REFLECTION_H
#define SWIFT_REFLECTION_SWIFT_REFLECTION_H

#include "MemoryReaderInterface.h"
#include "SwiftRemoteMirrorTypes.h"

#include <stdlib.h>

/// Major version changes when there are ABI or source incompatible changes.
#define SWIFT_REFLECTION_VERSION_MAJOR 3

/// Minor version changes when new APIs are added in ABI- and source-compatible
/// way.
#define SWIFT_REFLECTION_VERSION_MINOR 0

#ifdef __cplusplus
extern "C" {
#endif

/// Get the metadata version supported by the Remote Mirror library.
uint16_t
swift_reflection_getSupportedMetadataVersion();

/// \returns An opaque reflection context.
SwiftReflectionContextRef
swift_reflection_createReflectionContext(
    void *ReaderContext,
    PointerSizeFunction getPointerSize,
    SizeSizeFunction getSizeSize,
    ReadBytesFunction readBytes,
    GetStringLengthFunction getStringLength,
    GetSymbolAddressFunction getSymbolAddress);

/// Destroys an opaque reflection context.
void
swift_reflection_destroyReflectionContext(SwiftReflectionContextRef Context);

/// Add reflection sections for a loaded Swift image.
void
swift_reflection_addReflectionInfo(SwiftReflectionContextRef ContextRef,
                                   swift_reflection_info_t Info);


/// Returns a boolean indicating if the isa mask was successfully
/// read, in which case it is stored in the isaMask out parameter.
int
swift_reflection_readIsaMask(SwiftReflectionContextRef ContextRef,
                             uintptr_t *outIsaMask);

/// Returns an opaque type reference for a metadata pointer, or
/// NULL if one can't be constructed.
///
/// This function loses information; in particular, passing the
/// result to swift_reflection_infoForTypeRef() will not give
/// the same result as calling swift_reflection_infoForMetadata().
swift_typeref_t
swift_reflection_typeRefForMetadata(SwiftReflectionContextRef ContextRef,
                                    uintptr_t Metadata);

/// Returns an opaque type reference for a class or closure context
/// instance pointer, or NULL if one can't be constructed.
///
/// This function loses information; in particular, passing the
/// result to swift_reflection_infoForTypeRef() will not give
/// the same result as calling swift_reflection_infoForInstance().
swift_typeref_t
swift_reflection_typeRefForInstance(SwiftReflectionContextRef ContextRef,
                                    uintptr_t Object);

/// Returns a typeref from a mangled type name string.
swift_typeref_t
swift_reflection_typeRefForMangledTypeName(SwiftReflectionContextRef ContextRef,
                                           const char *MangledName,
                                           uint64_t Length);

/// Returns a structure describing the layout of a value of a typeref.
/// For classes, this returns the reference value itself.
swift_typeinfo_t
swift_reflection_infoForTypeRef(SwiftReflectionContextRef ContextRef,
                                swift_typeref_t OpaqueTypeRef);

/// Returns the information about a child field of a value by index.
swift_childinfo_t
swift_reflection_childOfTypeRef(SwiftReflectionContextRef ContextRef,
                                swift_typeref_t OpaqueTypeRef,
                                unsigned Index);

/// Returns a structure describing the layout of a class instance
/// from the isa pointer of a class.
swift_typeinfo_t
swift_reflection_infoForMetadata(SwiftReflectionContextRef ContextRef,
                                 uintptr_t Metadata);

/// Returns the information about a child field of a class instance
/// by index.
swift_childinfo_t
swift_reflection_childOfMetadata(SwiftReflectionContextRef ContextRef,
                                 uintptr_t Metadata,
                                 unsigned Index);

/// Returns a structure describing the layout of a class or closure
/// context instance, from a pointer to the object itself.
swift_typeinfo_t
swift_reflection_infoForInstance(SwiftReflectionContextRef ContextRef,
                                 uintptr_t Object);

/// Returns the information about a child field of a class or closure
/// context instance.
swift_childinfo_t
swift_reflection_childOfInstance(SwiftReflectionContextRef ContextRef,
                                 uintptr_t Object,
                                 unsigned Index);

/// Returns the number of generic arguments of a typeref.
unsigned
swift_reflection_genericArgumentCountOfTypeRef(swift_typeref_t OpaqueTypeRef);

/// Returns a fully instantiated typeref for a generic argument by index.
swift_typeref_t
swift_reflection_genericArgumentOfTypeRef(swift_typeref_t OpaqueTypeRef,
                                          unsigned Index);

/// Projects the type inside of an existential container.
///
/// Takes the address of the start of an existential container and the typeref
/// for the existential, and sets two out parameters:
///
/// - InstanceTypeRef: A type reference for the type inside of the existential
///   container.
/// - StartOfInstanceData: The address to the start of the inner type's instance
///   data, which may or may not be inside the container directly.
///   If the type is a reference type, the pointer to the instance is the first
///   word in the container.
///
///   If the existential contains a value type that can fit in the first three
///   spare words of the container, *StartOfInstanceData == InstanceAddress.
///   If it's a value type that can't fit in three words, the data will be in
///   its own heap box, so *StartOfInstanceData will be the address of that box.
///
/// Returns true if InstanceTypeRef and StartOfInstanceData contain valid
/// valid values.
int swift_reflection_projectExistential(SwiftReflectionContextRef ContextRef,
                                        swift_addr_t ExistentialAddress,
                                        swift_typeref_t ExistentialTypeRef,
                                        swift_typeref_t *OutInstanceTypeRef,
                                        swift_addr_t *OutStartOfInstanceData);

/// Dump a brief description of the typeref as a tree to stderr.
void swift_reflection_dumpTypeRef(swift_typeref_t OpaqueTypeRef);

/// Dump information about the layout for a typeref.
void swift_reflection_dumpInfoForTypeRef(SwiftReflectionContextRef ContextRef,
                                         swift_typeref_t OpaqueTypeRef);

/// Dump information about the layout of a class instance from its isa pointer.
void swift_reflection_dumpInfoForMetadata(SwiftReflectionContextRef ContextRef,
                                          uintptr_t Metadata);

/// Dump information about the layout of a class or closure context instance.
void swift_reflection_dumpInfoForInstance(SwiftReflectionContextRef ContextRef,
                                          uintptr_t Object);

/// Demangle a type name.
///
/// Copies at most `MaxLength` bytes from the demangled name string into
/// `OutDemangledName`.
///
/// Returns the length of the demangled string this function tried to copy
/// into `OutDemangledName`.
size_t swift_reflection_demangle(const char *MangledName,
                                 size_t Length,
                                 char *OutDemangledName,
                                 size_t MaxLength);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SWIFT_REFLECTION_SWIFT_REFLECTION_H

