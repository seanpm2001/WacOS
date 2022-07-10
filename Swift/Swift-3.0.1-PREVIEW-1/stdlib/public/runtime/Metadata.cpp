//===--- Metadata.cpp - Swift Language ABI Metadata Support ---------------===//
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
// Implementations of the metadata ABI functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/MathExtras.h"
#include "swift/Basic/Demangle.h"
#include "swift/Basic/LLVM.h"
#include "swift/Basic/Range.h"
#include "swift/Basic/Lazy.h"
#include "swift/Runtime/HeapObject.h"
#include "swift/Runtime/Metadata.h"
#include "swift/Runtime/Mutex.h"
#include "swift/Strings.h"
#include "MetadataCache.h"
#include <algorithm>
#include <condition_variable>
#include <new>
#include <cctype>
#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
// Avoid defining macro max(), min() which conflict with std::max(), std::min()
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "ErrorObject.h"
#include "ExistentialMetadataImpl.h"
#include "swift/Runtime/Debug.h"
#include "Private.h"

#if defined(__APPLE__)
#include <mach/vm_page_size.h>
#endif

#if SWIFT_OBJC_INTEROP
#include <objc/runtime.h>
#endif

#include <cstdio>

#if defined(__APPLE__) && defined(VM_MEMORY_SWIFT_METADATA)
#define VM_TAG_FOR_SWIFT_METADATA VM_MAKE_TAG(VM_MEMORY_SWIFT_METADATA)
#else
#define VM_TAG_FOR_SWIFT_METADATA (-1)
#endif

using namespace swift;
using namespace metadataimpl;

static uintptr_t swift_pageSize() {
#if defined(__APPLE__)
  return vm_page_size;
#elif defined(_MSC_VER)
  SYSTEM_INFO SystemInfo;
  GetSystemInfo(&SystemInfo);
  return SystemInfo.dwPageSize;
#else
  return sysconf(_SC_PAGESIZE);
#endif
}

// allocate memory up to a nearby page boundary
static void *swift_allocateMetadataRoundingToPage(size_t size) {
  const uintptr_t PageSizeMask = SWIFT_LAZY_CONSTANT(swift_pageSize()) - 1;
  size = (size + PageSizeMask) & ~PageSizeMask;
#if defined(_MSC_VER)
  auto mem = VirtualAlloc(
      nullptr, size, MEM_TOP_DOWN | MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
  auto mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE,
                  VM_TAG_FOR_SWIFT_METADATA, 0);
  if (mem == MAP_FAILED)
    mem = nullptr;
#endif
  return mem;
}

// free memory allocated by swift_allocateMetadataRoundingToPage()
static void swift_freeMetadata(void *addr, size_t size) {
#if defined(_MSC_VER)
  // On success, VirtualFree() returns nonzero, on failure 0 
  int result = VirtualFree(addr, 0, MEM_RELEASE);
  if (result == 0)
    fatalError(/* flags = */ 0, "swift_freePage: VirtualFree() failed");
#else
  // On success, munmap() returns 0, on failure -1
  int result = munmap(addr, size);
  if (result != 0)
    fatalError(/* flags = */ 0, "swift_freePage: munmap() failed");
#endif
}

void *MetadataAllocator::alloc(size_t size) {
  const uintptr_t PageSize = SWIFT_LAZY_CONSTANT(swift_pageSize());
  // If the requested size is a page or larger, map page(s) for it
  // specifically.
  if (LLVM_UNLIKELY(size >= PageSize)) {
    void *mem = swift_allocateMetadataRoundingToPage(size);
    if (!mem)
      crash("unable to allocate memory for metadata cache");
    return mem;
  }

  uintptr_t curValue = NextValue.load(std::memory_order_relaxed);
  while (true) {
    char *next = reinterpret_cast<char*>(curValue);
    char *end = next + size;
  
    // If we wrap over the end of the page, allocate a new page.
    void *allocation = nullptr;
    const uintptr_t PageSizeMask = PageSize - 1;
    if (LLVM_UNLIKELY(((uintptr_t)next & ~PageSizeMask)
                        != (((uintptr_t)end & ~PageSizeMask)))) {
      // Allocate a new page if we haven't already.
      allocation = swift_allocateMetadataRoundingToPage(PageSize);

      if (!allocation)
        crash("unable to allocate memory for metadata cache");

      next = (char*) allocation;
      end = next + size;
    }

    // Swap it into place.
    if (LLVM_LIKELY(std::atomic_compare_exchange_weak_explicit(
            &NextValue, &curValue, reinterpret_cast<uintptr_t>(end),
            std::memory_order_relaxed, std::memory_order_relaxed))) {
      return next;
    }

    // If that didn't succeed, and we allocated, free the allocation.
    // This potentially causes us to perform multiple mmaps under contention,
    // but it keeps the fast path pristine.
    if (allocation) {
      swift_freeMetadata(allocation, PageSize);
    }
  }
}

namespace {
  struct GenericCacheEntry;

  // The cache entries in a generic cache are laid out like this:
  struct GenericCacheEntryHeader : CacheEntry<GenericCacheEntry> {
    const Metadata *Value;
    size_t NumArguments;
  };

  struct GenericCacheEntry
      : CacheEntry<GenericCacheEntry, GenericCacheEntryHeader> {

    static const char *getName() { return "GenericCache"; }

    GenericCacheEntry(unsigned numArguments) {
      NumArguments = numArguments;
    }

    size_t getNumArguments() const { return NumArguments; }

    static GenericCacheEntry *getFromMetadata(GenericMetadata *pattern,
                                              Metadata *metadata) {
      char *bytes = (char*) metadata;
      if (auto classType = dyn_cast<ClassMetadata>(metadata)) {
        assert(classType->isTypeMetadata());
        bytes -= classType->getClassAddressPoint();
      } else {
        bytes -= pattern->AddressPoint;
      }
      bytes -= sizeof(GenericCacheEntry);
      return reinterpret_cast<GenericCacheEntry*>(bytes);
    }
  };
}

using GenericMetadataCache = MetadataCache<GenericCacheEntry>;
using LazyGenericMetadataCache = Lazy<GenericMetadataCache>;

/// Fetch the metadata cache for a generic metadata structure.
static GenericMetadataCache &getCache(GenericMetadata *metadata) {
  // Keep this assert even if you change the representation above.
  static_assert(sizeof(LazyGenericMetadataCache) <=
                sizeof(GenericMetadata::PrivateData),
                "metadata cache is larger than the allowed space");

  auto lazyCache =
    reinterpret_cast<LazyGenericMetadataCache*>(metadata->PrivateData);
  return lazyCache->get();
}

/// Fetch the metadata cache for a generic metadata structure,
/// in a context where it must have already been initialized.
static GenericMetadataCache &unsafeGetInitializedCache(GenericMetadata *metadata) {
  // Keep this assert even if you change the representation above.
  static_assert(sizeof(LazyGenericMetadataCache) <=
                sizeof(GenericMetadata::PrivateData),
                "metadata cache is larger than the allowed space");

  auto lazyCache =
    reinterpret_cast<LazyGenericMetadataCache*>(metadata->PrivateData);
  return lazyCache->unsafeGetAlreadyInitialized();
}

ClassMetadata *
swift::swift_allocateGenericClassMetadata(GenericMetadata *pattern,
                                          const void *arguments,
                                          ClassMetadata *superclass) {
  void * const *argumentsAsArray = reinterpret_cast<void * const *>(arguments);
  size_t numGenericArguments = pattern->NumKeyArguments;

  // Right now, we only worry about there being a difference in prefix matter.
  size_t metadataSize = pattern->MetadataSize;
  size_t prefixSize = pattern->AddressPoint;
  size_t extraPrefixSize = 0;
  if (superclass && superclass->isTypeMetadata()) {
    if (superclass->getClassAddressPoint() > prefixSize) {
      extraPrefixSize = (superclass->getClassAddressPoint() - prefixSize);
      prefixSize += extraPrefixSize;
      metadataSize += extraPrefixSize;
    }
  }
  assert(metadataSize == pattern->MetadataSize + extraPrefixSize);
  assert(prefixSize == pattern->AddressPoint + extraPrefixSize);

  char *bytes = GenericCacheEntry::allocate(
                              unsafeGetInitializedCache(pattern).getAllocator(),
                              argumentsAsArray,
                              numGenericArguments,
                              metadataSize)->getData<char>();

  // Copy any extra prefix bytes in from the superclass.
  if (extraPrefixSize) {
    memcpy(bytes, (const char*) superclass - prefixSize, extraPrefixSize);
    bytes += extraPrefixSize;
  }

  // Copy in the metadata template.
  memcpy(bytes, pattern->getMetadataTemplate(), pattern->MetadataSize);

  // Okay, move to the address point.
  bytes += pattern->AddressPoint;
  ClassMetadata *metadata = reinterpret_cast<ClassMetadata*>(bytes);
  assert(metadata->isTypeMetadata());
  
  // Overwrite the superclass field.
  metadata->SuperClass = superclass;
  // Adjust the relative reference to the nominal type descriptor.
  if (!metadata->isArtificialSubclass()) {
    auto patternBytes =
      reinterpret_cast<const char*>(pattern->getMetadataTemplate()) +
      pattern->AddressPoint;
    metadata->setDescription(
        reinterpret_cast<const ClassMetadata*>(patternBytes)->getDescription());
  }

  // Adjust the class object extents.
  if (extraPrefixSize) {
    metadata->setClassSize(metadata->getClassSize() + extraPrefixSize);
    metadata->setClassAddressPoint(prefixSize);
  }
  assert(metadata->getClassAddressPoint() == prefixSize);

  return metadata;
}

ValueMetadata *
swift::swift_allocateGenericValueMetadata(GenericMetadata *pattern,
                                          const void *arguments) {
  void * const *argumentsAsArray = reinterpret_cast<void * const *>(arguments);
  size_t numGenericArguments = pattern->NumKeyArguments;

  char *bytes =
    GenericCacheEntry::allocate(
                              unsafeGetInitializedCache(pattern).getAllocator(),
                              argumentsAsArray, numGenericArguments,
                              pattern->MetadataSize)->getData<char>();

  // Copy in the metadata template.
  memcpy(bytes, pattern->getMetadataTemplate(), pattern->MetadataSize);

  // Okay, move to the address point.
  bytes += pattern->AddressPoint;
  auto *metadata = reinterpret_cast<ValueMetadata*>(bytes);
  
  // Adjust the relative references to the nominal type descriptor and
  // parent type.
  auto patternBytes =
    reinterpret_cast<const char*>(pattern->getMetadataTemplate()) +
    pattern->AddressPoint;
  auto patternMetadata = reinterpret_cast<const ValueMetadata*>(patternBytes);
  metadata->Description = patternMetadata->Description.get();
  metadata->Parent = patternMetadata->Parent;
  
  return metadata;
}

/// The primary entrypoint.
SWIFT_RT_ENTRY_VISIBILITY
const Metadata *
swift::swift_getGenericMetadata(GenericMetadata *pattern,
                                const void *arguments)
    SWIFT_CC(RegisterPreservingCC_IMPL) {
  auto genericArgs = (const void * const *) arguments;
  size_t numGenericArgs = pattern->NumKeyArguments;

  auto entry = getCache(pattern).findOrAdd(genericArgs, numGenericArgs,
    [&]() -> GenericCacheEntry* {
      // Create new metadata to cache.
      auto metadata = pattern->CreateFunction(pattern, arguments);
      auto entry = GenericCacheEntry::getFromMetadata(pattern, metadata);
      entry->Value = metadata;
      return entry;
    });

  return entry->Value;
}

namespace {
  class ObjCClassCacheEntry : public CacheEntry<ObjCClassCacheEntry> {
    FullMetadata<ObjCClassWrapperMetadata> Metadata;

  public:
    static const char *getName() { return "ObjCClassCache"; }

    ObjCClassCacheEntry(size_t numArguments) {}

    static constexpr size_t getNumArguments() {
      return 1;
    }

    FullMetadata<ObjCClassWrapperMetadata> *getData() {
      return &Metadata;
    }
    const FullMetadata<ObjCClassWrapperMetadata> *getData() const {
      return &Metadata;
    }
  };
}

/// The uniquing structure for ObjC class-wrapper metadata.
static Lazy<MetadataCache<ObjCClassCacheEntry>> ObjCClassWrappers;

const Metadata *
swift::swift_getObjCClassMetadata(const ClassMetadata *theClass) {
  // If the class pointer is valid as metadata, no translation is required.
  if (theClass->isTypeMetadata()) {
    return theClass;
  }

#if SWIFT_OBJC_INTEROP
  // Search the cache.

  const size_t numGenericArgs = 1;
  const void *args[] = { theClass };
  auto &Wrappers = ObjCClassWrappers.get();
  auto entry = Wrappers.findOrAdd(args, numGenericArgs,
    [&]() -> ObjCClassCacheEntry* {
      // Create a new entry for the cache.
      auto entry = ObjCClassCacheEntry::allocate(Wrappers.getAllocator(),
                                                 args, numGenericArgs, 0);

      auto metadata = entry->getData();
      metadata->setKind(MetadataKind::ObjCClassWrapper);
      metadata->ValueWitnesses = &_TWVBO;
      metadata->Class = theClass;

      return entry;
    });

  return entry->getData();
#else
  fatalError(/* flags = */ 0,
             "swift_getObjCClassMetadata: no Objective-C interop");
#endif
}

namespace {
  class FunctionCacheEntry;
  struct FunctionCacheEntryHeader : CacheEntryHeader<FunctionCacheEntry> {
    size_t NumArguments;
  };
  class FunctionCacheEntry
    : public CacheEntry<FunctionCacheEntry, FunctionCacheEntryHeader> {
  public:
    FullMetadata<FunctionTypeMetadata> Metadata;

    static const char *getName() { return "FunctionCache"; }

    FunctionCacheEntry(size_t numArguments) {
      NumArguments = numArguments;
    }

    size_t getNumArguments() const {
      return NumArguments;
    }

    FullMetadata<FunctionTypeMetadata> *getData() {
      return &Metadata;
    }
    const FullMetadata<FunctionTypeMetadata> *getData() const {
      return &Metadata;
    }
  };
}

/// The uniquing structure for function type metadata.
static Lazy<MetadataCache<FunctionCacheEntry>> FunctionTypes;

const FunctionTypeMetadata *
swift::swift_getFunctionTypeMetadata1(FunctionTypeFlags flags,
                                      const void *arg0,
                                      const Metadata *result) {
  assert(flags.getNumArguments() == 1
         && "wrong number of arguments in function metadata flags?!");
  const void *flagsArgsAndResult[] = {
    reinterpret_cast<const void*>(flags.getIntValue()),
    arg0,
    static_cast<const void *>(result)                      
  };                                                       
  return swift_getFunctionTypeMetadata(flagsArgsAndResult);
}                                                          
const FunctionTypeMetadata *                               
swift::swift_getFunctionTypeMetadata2(FunctionTypeFlags flags,
                                      const void *arg0,
                                      const void *arg1,
                                      const Metadata *result) {
  assert(flags.getNumArguments() == 2
         && "wrong number of arguments in function metadata flags?!");
  const void *flagsArgsAndResult[] = {
    reinterpret_cast<const void*>(flags.getIntValue()),
    arg0,
    arg1,                                                  
    static_cast<const void *>(result)                      
  };                                                       
  return swift_getFunctionTypeMetadata(flagsArgsAndResult);
}                                                          
const FunctionTypeMetadata *                               
swift::swift_getFunctionTypeMetadata3(FunctionTypeFlags flags,
                                      const void *arg0,
                                      const void *arg1,
                                      const void *arg2,
                                      const Metadata *result) {
  assert(flags.getNumArguments() == 3
         && "wrong number of arguments in function metadata flags?!");
  const void *flagsArgsAndResult[] = {
    reinterpret_cast<const void*>(flags.getIntValue()),
    arg0,                                                  
    arg1,                                                  
    arg2,                                                  
    static_cast<const void *>(result)                      
  };                                                       
  return swift_getFunctionTypeMetadata(flagsArgsAndResult);
}

const FunctionTypeMetadata *
swift::swift_getFunctionTypeMetadata(const void *flagsArgsAndResult[]) {
  auto flags = FunctionTypeFlags::fromIntValue(size_t(flagsArgsAndResult[0]));

  unsigned numArguments = flags.getNumArguments();

  // Pick a value witness table appropriate to the function convention.
  // All function types of a given convention have the same value semantics,
  // so they share a value witness table.
  const ValueWitnessTable *valueWitnesses;
  switch (flags.getConvention()) {
  case FunctionMetadataConvention::Swift:
    valueWitnesses = &_TWVFT_T_;
    break;
  case FunctionMetadataConvention::Thin:
  case FunctionMetadataConvention::CFunctionPointer:
    valueWitnesses = &_TWVXfT_T_;
    break;
  case FunctionMetadataConvention::Block:
#if SWIFT_OBJC_INTEROP
    // Blocks are ObjC objects, so can share the Builtin.UnknownObject value
    // witnesses.
    valueWitnesses = &_TWVBO;
#else
    assert(false && "objc block without objc interop?");
#endif
    break;
  }

  // Search the cache.

  unsigned numKeyArguments =
  // 1 flags word,
    1 +
  // N argument types (with inout bit set),
    numArguments +
  // and 1 result type
    1;
  auto &Types = FunctionTypes.get();
  
  auto entry = Types.findOrAdd(flagsArgsAndResult, numKeyArguments,
    [&]() -> FunctionCacheEntry* {
      // Create a new entry for the cache.
      auto entry = FunctionCacheEntry::allocate(
        Types.getAllocator(),
        flagsArgsAndResult,
        numKeyArguments,
        numArguments * sizeof(FunctionTypeMetadata::Argument));

      auto metadata = entry->getData();
      metadata->setKind(MetadataKind::Function);
      metadata->ValueWitnesses = valueWitnesses;
      metadata->Flags = flags;
      metadata->ResultType = reinterpret_cast<const Metadata *>(
                                          flagsArgsAndResult[1 + numArguments]);

      for (size_t i = 0; i < numArguments; ++i) {
        auto arg = FunctionTypeMetadata::Argument::getFromOpaqueValue(
          flagsArgsAndResult[i+1]);
        metadata->getArguments()[i] = arg;
      }

      return entry;
    });

  return entry->getData();
}

/*** Tuples ****************************************************************/

namespace {
  class TupleCacheEntry;
  struct TupleCacheEntryHeader : CacheEntryHeader<TupleCacheEntry> {
    size_t NumArguments;
  };
  class TupleCacheEntry
    : public CacheEntry<TupleCacheEntry, TupleCacheEntryHeader> {
  public:
    // NOTE: if you change the layout of this type, you'll also need
    // to update tuple_getValueWitnesses().
    ExtraInhabitantsValueWitnessTable Witnesses;
    FullMetadata<TupleTypeMetadata> Metadata;

    static const char *getName() { return "TupleCache"; }

    TupleCacheEntry(size_t numArguments) {
      NumArguments = numArguments;
    }

    size_t getNumArguments() const {
      return Metadata.NumElements;
    }

    FullMetadata<TupleTypeMetadata> *getData() {
      return &Metadata;
    }
    const FullMetadata<TupleTypeMetadata> *getData() const {
      return &Metadata;
    }
  };
}

/// The uniquing structure for tuple type metadata.
static Lazy<MetadataCache<TupleCacheEntry>> TupleTypes;

/// Given a metatype pointer, produce the value-witness table for it.
/// This is equivalent to metatype->ValueWitnesses but more efficient.
static const ValueWitnessTable *tuple_getValueWitnesses(const Metadata *metatype) {
  return ((const ExtraInhabitantsValueWitnessTable*) asFullMetadata(metatype)) - 1;
}

/// Generic tuple value witness for 'projectBuffer'.
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_projectBuffer(ValueBuffer *buffer,
                                        const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  if (IsInline)
    return reinterpret_cast<OpaqueValue*>(buffer);
  else
    return *reinterpret_cast<OpaqueValue**>(buffer);
}

/// Generic tuple value witness for 'allocateBuffer'
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_allocateBuffer(ValueBuffer *buffer,
                                         const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  if (IsInline)
    return reinterpret_cast<OpaqueValue*>(buffer);

  auto wtable = tuple_getValueWitnesses(metatype);
  auto value = (OpaqueValue*) swift_slowAlloc(wtable->size,
                                              wtable->getAlignmentMask());

  *reinterpret_cast<OpaqueValue**>(buffer) = value;
  return value;
}

/// Generic tuple value witness for 'deallocateBuffer'.
template <bool IsPOD, bool IsInline>
static void tuple_deallocateBuffer(ValueBuffer *buffer,
                                   const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  if (IsInline)
    return;

  auto wtable = tuple_getValueWitnesses(metatype);
  auto value = *reinterpret_cast<OpaqueValue**>(buffer);
  swift_slowDealloc(value, wtable->size, wtable->getAlignmentMask());
}

/// Generic tuple value witness for 'destroy'.
template <bool IsPOD, bool IsInline>
static void tuple_destroy(OpaqueValue *tuple, const Metadata *_metadata) {
  auto &metadata = *(const TupleTypeMetadata*) _metadata;
  assert(IsPOD == tuple_getValueWitnesses(&metadata)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(&metadata)->isValueInline());

  if (IsPOD) return;

  for (size_t i = 0, e = metadata.NumElements; i != e; ++i) {
    auto &eltInfo = metadata.getElements()[i];
    OpaqueValue *elt = eltInfo.findIn(tuple);
    auto eltWitnesses = eltInfo.Type->getValueWitnesses();
    eltWitnesses->destroy(elt, eltInfo.Type);
  }
}

/// Generic tuple value witness for 'destroyArray'.
template <bool IsPOD, bool IsInline>
static void tuple_destroyArray(OpaqueValue *array, size_t n,
                               const Metadata *_metadata) {
  auto &metadata = *(const TupleTypeMetadata*) _metadata;
  assert(IsPOD == tuple_getValueWitnesses(&metadata)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(&metadata)->isValueInline());

  if (IsPOD) return;

  size_t stride = tuple_getValueWitnesses(&metadata)->stride;
  char *bytes = (char*)array;

  while (n--) {
    tuple_destroy<IsPOD, IsInline>((OpaqueValue*)bytes, _metadata);
    bytes += stride;
  }
}

/// Generic tuple value witness for 'destroyBuffer'.
template <bool IsPOD, bool IsInline>
static void tuple_destroyBuffer(ValueBuffer *buffer, const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  auto tuple = tuple_projectBuffer<IsPOD, IsInline>(buffer, metatype);
  tuple_destroy<IsPOD, IsInline>(tuple, metatype);
  tuple_deallocateBuffer<IsPOD, IsInline>(buffer, metatype);
}

// The operation doesn't have to be initializeWithCopy, but they all
// have basically the same type.
typedef value_witness_types::initializeWithCopy *
  ValueWitnessTable::*forEachOperation;

/// Perform an operation for each field of two tuples.
static OpaqueValue *tuple_forEachField(OpaqueValue *destTuple,
                                       OpaqueValue *srcTuple,
                                       const Metadata *_metatype,
                                       forEachOperation member) {
  auto &metatype = *(const TupleTypeMetadata*) _metatype;
  for (size_t i = 0, e = metatype.NumElements; i != e; ++i) {
    auto &eltInfo = metatype.getElement(i);
    auto eltValueWitnesses = eltInfo.Type->getValueWitnesses();

    OpaqueValue *destElt = eltInfo.findIn(destTuple);
    OpaqueValue *srcElt = eltInfo.findIn(srcTuple);
    (eltValueWitnesses->*member)(destElt, srcElt, eltInfo.Type);
  }

  return destTuple;
}

/// Perform a naive memcpy of src into dest.
static OpaqueValue *tuple_memcpy(OpaqueValue *dest,
                                 OpaqueValue *src,
                                 const Metadata *metatype) {
  assert(metatype->getValueWitnesses()->isPOD());
  return (OpaqueValue*)
    memcpy(dest, src, metatype->getValueWitnesses()->getSize());
}
/// Perform a naive memcpy of n tuples from src into dest.
static OpaqueValue *tuple_memcpy_array(OpaqueValue *dest,
                                       OpaqueValue *src,
                                       size_t n,
                                       const Metadata *metatype) {
  assert(metatype->getValueWitnesses()->isPOD());
  return (OpaqueValue*)
    memcpy(dest, src, metatype->getValueWitnesses()->stride * n);
}
/// Perform a naive memmove of n tuples from src into dest.
static OpaqueValue *tuple_memmove_array(OpaqueValue *dest,
                                        OpaqueValue *src,
                                        size_t n,
                                        const Metadata *metatype) {
  assert(metatype->getValueWitnesses()->isPOD());
  return (OpaqueValue*)
    memmove(dest, src, metatype->getValueWitnesses()->stride * n);
}

/// Generic tuple value witness for 'initializeWithCopy'.
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_initializeWithCopy(OpaqueValue *dest,
                                             OpaqueValue *src,
                                             const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  if (IsPOD) return tuple_memcpy(dest, src, metatype);
  return tuple_forEachField(dest, src, metatype,
                            &ValueWitnessTable::initializeWithCopy);
}

/// Generic tuple value witness for 'initializeArrayWithCopy'.
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_initializeArrayWithCopy(OpaqueValue *dest,
                                                  OpaqueValue *src,
                                                  size_t n,
                                                  const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  if (IsPOD) return tuple_memcpy_array(dest, src, n, metatype);

  char *destBytes = (char*)dest;
  char *srcBytes = (char*)src;
  size_t stride = tuple_getValueWitnesses(metatype)->stride;

  while (n--) {
    tuple_initializeWithCopy<IsPOD, IsInline>((OpaqueValue*)destBytes,
                                              (OpaqueValue*)srcBytes,
                                              metatype);
    destBytes += stride; srcBytes += stride;
  }

  return dest;
}

/// Generic tuple value witness for 'initializeWithTake'.
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_initializeWithTake(OpaqueValue *dest,
                                             OpaqueValue *src,
                                             const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  if (IsPOD) return tuple_memcpy(dest, src, metatype);
  return tuple_forEachField(dest, src, metatype,
                            &ValueWitnessTable::initializeWithTake);
}

/// Generic tuple value witness for 'initializeArrayWithTakeFrontToBack'.
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_initializeArrayWithTakeFrontToBack(
                                             OpaqueValue *dest,
                                             OpaqueValue *src,
                                             size_t n,
                                             const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  if (IsPOD) return tuple_memmove_array(dest, src, n, metatype);

  char *destBytes = (char*)dest;
  char *srcBytes = (char*)src;
  size_t stride = tuple_getValueWitnesses(metatype)->stride;

  while (n--) {
    tuple_initializeWithTake<IsPOD, IsInline>((OpaqueValue*)destBytes,
                                              (OpaqueValue*)srcBytes,
                                              metatype);
    destBytes += stride; srcBytes += stride;
  }

  return dest;
}

/// Generic tuple value witness for 'initializeArrayWithTakeBackToFront'.
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_initializeArrayWithTakeBackToFront(
                                             OpaqueValue *dest,
                                             OpaqueValue *src,
                                             size_t n,
                                             const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  if (IsPOD) return tuple_memmove_array(dest, src, n, metatype);

  size_t stride = tuple_getValueWitnesses(metatype)->stride;
  char *destBytes = (char*)dest + n * stride;
  char *srcBytes = (char*)src + n * stride;

  while (n--) {
    destBytes -= stride; srcBytes -= stride;
    tuple_initializeWithTake<IsPOD, IsInline>((OpaqueValue*)destBytes,
                                              (OpaqueValue*)srcBytes,
                                              metatype);
  }

  return dest;
}

/// Generic tuple value witness for 'assignWithCopy'.
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_assignWithCopy(OpaqueValue *dest,
                                         OpaqueValue *src,
                                         const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  if (IsPOD) return tuple_memcpy(dest, src, metatype);
  return tuple_forEachField(dest, src, metatype,
                            &ValueWitnessTable::assignWithCopy);
}

/// Generic tuple value witness for 'assignWithTake'.
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_assignWithTake(OpaqueValue *dest,
                                         OpaqueValue *src,
                                         const Metadata *metatype) {
  if (IsPOD) return tuple_memcpy(dest, src, metatype);
  return tuple_forEachField(dest, src, metatype,
                            &ValueWitnessTable::assignWithTake);
}

/// Generic tuple value witness for 'initializeBufferWithCopy'.
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_initializeBufferWithCopy(ValueBuffer *dest,
                                                   OpaqueValue *src,
                                                   const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  return tuple_initializeWithCopy<IsPOD, IsInline>(
                        tuple_allocateBuffer<IsPOD, IsInline>(dest, metatype),
                        src,
                        metatype);
}

/// Generic tuple value witness for 'initializeBufferWithTake'.
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_initializeBufferWithTake(ValueBuffer *dest,
                                                   OpaqueValue *src,
                                                   const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  return tuple_initializeWithTake<IsPOD, IsInline>(
                        tuple_allocateBuffer<IsPOD, IsInline>(dest, metatype),
                        src,
                        metatype);
}

/// Generic tuple value witness for 'initializeBufferWithCopyOfBuffer'.
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_initializeBufferWithCopyOfBuffer(ValueBuffer *dest,
                                                           ValueBuffer *src,
                                                     const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  return tuple_initializeBufferWithCopy<IsPOD, IsInline>(
                            dest,
                            tuple_projectBuffer<IsPOD, IsInline>(src, metatype),
                            metatype);
}

/// Generic tuple value witness for 'initializeBufferWithTakeOfBuffer'.
template <bool IsPOD, bool IsInline>
static OpaqueValue *tuple_initializeBufferWithTakeOfBuffer(ValueBuffer *dest,
                                                           ValueBuffer *src,
                                                     const Metadata *metatype) {
  assert(IsPOD == tuple_getValueWitnesses(metatype)->isPOD());
  assert(IsInline == tuple_getValueWitnesses(metatype)->isValueInline());

  if (IsInline) {
    return tuple_initializeWithTake<IsPOD, IsInline>(
                      tuple_projectBuffer<IsPOD, IsInline>(dest, metatype),
                      tuple_projectBuffer<IsPOD, IsInline>(src, metatype),
                      metatype);
  } else {
    dest->PrivateData[0] = src->PrivateData[0];
    return (OpaqueValue*) dest->PrivateData[0];
  }
}

static void tuple_storeExtraInhabitant(OpaqueValue *tuple,
                                       int index,
                                       const Metadata *_metatype) {
  auto &metatype = *(const TupleTypeMetadata*) _metatype;
  auto &eltInfo = metatype.getElement(0);

  assert(eltInfo.Offset == 0);
  OpaqueValue *elt = tuple;

  eltInfo.Type->vw_storeExtraInhabitant(elt, index);
}

static int tuple_getExtraInhabitantIndex(const OpaqueValue *tuple,
                                         const Metadata *_metatype) {
  auto &metatype = *(const TupleTypeMetadata*) _metatype;
  auto &eltInfo = metatype.getElement(0);

  assert(eltInfo.Offset == 0);
  const OpaqueValue *elt = tuple;

  return eltInfo.Type->vw_getExtraInhabitantIndex(elt);
}

/// Various standard witness table for tuples.
static const ValueWitnessTable tuple_witnesses_pod_inline = {
#define TUPLE_WITNESS(NAME) &tuple_##NAME<true, true>,
  FOR_ALL_FUNCTION_VALUE_WITNESSES(TUPLE_WITNESS)
#undef TUPLE_WITNESS
  0,
  ValueWitnessFlags(),
  0
};
static const ValueWitnessTable tuple_witnesses_nonpod_inline = {
#define TUPLE_WITNESS(NAME) &tuple_##NAME<false, true>,
  FOR_ALL_FUNCTION_VALUE_WITNESSES(TUPLE_WITNESS)
#undef TUPLE_WITNESS
  0,
  ValueWitnessFlags(),
  0
};
static const ValueWitnessTable tuple_witnesses_pod_noninline = {
#define TUPLE_WITNESS(NAME) &tuple_##NAME<true, false>,
  FOR_ALL_FUNCTION_VALUE_WITNESSES(TUPLE_WITNESS)
#undef TUPLE_WITNESS
  0,
  ValueWitnessFlags(),
  0
};
static const ValueWitnessTable tuple_witnesses_nonpod_noninline = {
#define TUPLE_WITNESS(NAME) &tuple_##NAME<false, false>,
  FOR_ALL_FUNCTION_VALUE_WITNESSES(TUPLE_WITNESS)
#undef TUPLE_WITNESS
  0,
  ValueWitnessFlags(),
  0
};

namespace {
struct BasicLayout {
  size_t size;
  ValueWitnessFlags flags;
  size_t stride;

  static constexpr BasicLayout initialForValueType() {
    return {0, ValueWitnessFlags().withAlignment(1).withPOD(true), 0};
  }

  static constexpr BasicLayout initialForHeapObject() {
    return {sizeof(HeapObject),
            ValueWitnessFlags().withAlignment(alignof(HeapObject)),
            sizeof(HeapObject)};
  }
};

static size_t roundUpToAlignMask(size_t size, size_t alignMask) {
  return (size + alignMask) & ~alignMask;
}

/// Perform basic sequential layout given a vector of metadata pointers,
/// calling a functor with the offset of each field, and returning the
/// final layout characteristics of the type.
/// FUNCTOR should have signature:
///   void (size_t index, const Metadata *type, size_t offset)
template<typename FUNCTOR, typename LAYOUT>
void performBasicLayout(BasicLayout &layout,
                        const LAYOUT * const *elements,
                        size_t numElements,
                        FUNCTOR &&f) {
  size_t size = layout.size;
  size_t alignMask = layout.flags.getAlignmentMask();
  bool isPOD = layout.flags.isPOD();
  bool isBitwiseTakable = layout.flags.isBitwiseTakable();
  for (unsigned i = 0; i != numElements; ++i) {
    auto elt = elements[i];

    // Lay out this element.
    const TypeLayout *eltLayout = elt->getTypeLayout();
    size = roundUpToAlignMask(size, eltLayout->flags.getAlignmentMask());

    // Report this record to the functor.
    f(i, elt, size);

    // Update the size and alignment of the aggregate..
    size += eltLayout->size;
    alignMask = std::max(alignMask, eltLayout->flags.getAlignmentMask());
    if (!eltLayout->flags.isPOD()) isPOD = false;
    if (!eltLayout->flags.isBitwiseTakable()) isBitwiseTakable = false;
  }
  bool isInline = ValueWitnessTable::isValueInline(size, alignMask + 1);

  layout.size = size;
  layout.flags = ValueWitnessFlags().withAlignmentMask(alignMask)
                                    .withPOD(isPOD)
                                    .withBitwiseTakable(isBitwiseTakable)
                                    .withInlineStorage(isInline);
  layout.stride = roundUpToAlignMask(size, alignMask);
}
} // end anonymous namespace

const TupleTypeMetadata *
swift::swift_getTupleTypeMetadata(size_t numElements,
                                  const Metadata * const *elements,
                                  const char *labels,
                                  const ValueWitnessTable *proposedWitnesses) {
  // Bypass the cache for the empty tuple. We might reasonably get called
  // by generic code, like a demangler that produces type objects.
  if (numElements == 0) return &_TMT_;

  // Search the cache.

  // FIXME: include labels when uniquing!
  auto genericArgs = (const void * const *) elements;
  auto &Types = TupleTypes.get();
  auto entry = Types.findOrAdd(genericArgs, numElements,
    [&]() -> TupleCacheEntry* {
      // Create a new entry for the cache.

      typedef TupleTypeMetadata::Element Element;

      // Allocate the tuple cache entry, which includes space for both the
      // metadata and a value-witness table.
      auto entry = TupleCacheEntry::allocate(Types.getAllocator(),
                                             genericArgs, numElements,
                                             numElements * sizeof(Element));

      auto witnesses = &entry->Witnesses;

      auto metadata = entry->getData();
      metadata->setKind(MetadataKind::Tuple);
      metadata->ValueWitnesses = witnesses;
      metadata->NumElements = numElements;
      metadata->Labels = labels;

      // Perform basic layout on the tuple.
      auto layout = BasicLayout::initialForValueType();
      performBasicLayout(layout, elements, numElements,
        [&](size_t i, const Metadata *elt, size_t offset) {
          metadata->getElement(i).Type = elt;
          metadata->getElement(i).Offset = offset;
        });

      witnesses->size = layout.size;
      witnesses->flags = layout.flags;
      witnesses->stride = layout.stride;

      // Copy the function witnesses in, either from the proposed
      // witnesses or from the standard table.
      if (!proposedWitnesses) {
        // For a tuple with a single element, just use the witnesses for
        // the element type.
        if (numElements == 1) {
          proposedWitnesses = elements[0]->getValueWitnesses();

          // Otherwise, use generic witnesses (when we can't pattern-match
          // into something better).
        } else if (layout.flags.isInlineStorage()
                   && layout.flags.isPOD()) {
          if (layout.size == 8 && layout.flags.getAlignmentMask() == 7)
            proposedWitnesses = &_TWVBi64_;
          else if (layout.size == 4 && layout.flags.getAlignmentMask() == 3)
            proposedWitnesses = &_TWVBi32_;
          else if (layout.size == 2 && layout.flags.getAlignmentMask() == 1)
            proposedWitnesses = &_TWVBi16_;
          else if (layout.size == 1)
            proposedWitnesses = &_TWVBi8_;
          else
            proposedWitnesses = &tuple_witnesses_pod_inline;
        } else if (layout.flags.isInlineStorage()
                   && !layout.flags.isPOD()) {
          proposedWitnesses = &tuple_witnesses_nonpod_inline;
        } else if (!layout.flags.isInlineStorage()
                   && layout.flags.isPOD()) {
          proposedWitnesses = &tuple_witnesses_pod_noninline;
        } else {
          assert(!layout.flags.isInlineStorage()
                 && !layout.flags.isPOD());
          proposedWitnesses = &tuple_witnesses_nonpod_noninline;
        }
      }
#define ASSIGN_TUPLE_WITNESS(NAME) \
      witnesses->NAME = proposedWitnesses->NAME;
      FOR_ALL_FUNCTION_VALUE_WITNESSES(ASSIGN_TUPLE_WITNESS)
#undef ASSIGN_TUPLE_WITNESS

      // We have extra inhabitants if the first element does.
      // FIXME: generalize this.
      if (auto firstEltEIVWT = dyn_cast<ExtraInhabitantsValueWitnessTable>(
                                 elements[0]->getValueWitnesses())) {
        witnesses->flags = witnesses->flags.withExtraInhabitants(true);
        witnesses->extraInhabitantFlags = firstEltEIVWT->extraInhabitantFlags;
        witnesses->storeExtraInhabitant = tuple_storeExtraInhabitant;
        witnesses->getExtraInhabitantIndex = tuple_getExtraInhabitantIndex;
      }

      return entry;
    });

  return entry->getData();
}

const TupleTypeMetadata *
swift::swift_getTupleTypeMetadata2(const Metadata *elt0, const Metadata *elt1,
                                   const char *labels,
                                   const ValueWitnessTable *proposedWitnesses) {
  const Metadata *elts[] = { elt0, elt1 };
  return swift_getTupleTypeMetadata(2, elts, labels, proposedWitnesses);
}

const TupleTypeMetadata *
swift::swift_getTupleTypeMetadata3(const Metadata *elt0, const Metadata *elt1,
                                   const Metadata *elt2,
                                   const char *labels,
                                   const ValueWitnessTable *proposedWitnesses) {
  const Metadata *elts[] = { elt0, elt1, elt2 };
  return swift_getTupleTypeMetadata(3, elts, labels, proposedWitnesses);
}

/*** Common value witnesses ************************************************/

// Value witness methods for an arbitrary trivial type.
// The buffer operations assume that the value is stored indirectly, because
// installCommonValueWitnesses will install the direct equivalents instead.

namespace {
  template<typename T>
  struct pointer_function_cast_impl;
  
  template<typename OutRet, typename...OutArgs>
  struct pointer_function_cast_impl<OutRet * (OutArgs *...)> {
    template<typename InRet, typename...InArgs>
    static constexpr auto perform(InRet * (*function)(InArgs *...))
      -> OutRet * (*)(OutArgs *...)
    {
      static_assert(sizeof...(InArgs) == sizeof...(OutArgs),
                    "cast changed number of arguments");
      return (OutRet *(*)(OutArgs *...))function;
    }
  };

  template<typename...OutArgs>
  struct pointer_function_cast_impl<void (OutArgs *...)> {
    template<typename...InArgs>
    static constexpr auto perform(void (*function)(InArgs *...))
      -> void (*)(OutArgs *...)
    {
      static_assert(sizeof...(InArgs) == sizeof...(OutArgs),
                    "cast changed number of arguments");
      return (void (*)(OutArgs *...))function;
    }
  };
}

/// Cast a function that takes all pointer arguments and returns to a
/// function type that takes different pointer arguments and returns.
/// In any reasonable calling convention the input and output function types
/// should be ABI-compatible.
template<typename Out, typename In>
static constexpr Out *pointer_function_cast(In *function) {
  return pointer_function_cast_impl<Out>::perform(function);
}

static void pod_indirect_deallocateBuffer(ValueBuffer *buffer,
                                          const Metadata *self) {
  auto value = *reinterpret_cast<OpaqueValue**>(buffer);
  auto wtable = self->getValueWitnesses();
  swift_slowDealloc(value, wtable->size, wtable->getAlignmentMask());
}
#define pod_indirect_destroyBuffer \
  pointer_function_cast<value_witness_types::destroyBuffer>(pod_indirect_deallocateBuffer)

static OpaqueValue *pod_indirect_initializeBufferWithCopyOfBuffer(
                    ValueBuffer *dest, ValueBuffer *src, const Metadata *self) {
  auto wtable = self->getValueWitnesses();
  auto destBuf = (OpaqueValue*)swift_slowAlloc(wtable->size,
                                               wtable->getAlignmentMask());
  *reinterpret_cast<OpaqueValue**>(dest) = destBuf;
  OpaqueValue *srcBuf = *reinterpret_cast<OpaqueValue**>(src);
  memcpy(destBuf, srcBuf, wtable->size);
  return destBuf;
}

static OpaqueValue *pod_indirect_initializeBufferWithTakeOfBuffer(
                    ValueBuffer *dest, ValueBuffer *src, const Metadata *self) {
  memcpy(dest, src, sizeof(ValueBuffer));
  return *reinterpret_cast<OpaqueValue**>(dest);
}

static OpaqueValue *pod_indirect_projectBuffer(ValueBuffer *buffer,
                                               const Metadata *self) {
  return *reinterpret_cast<OpaqueValue**>(buffer);
}

static OpaqueValue *pod_indirect_allocateBuffer(ValueBuffer *buffer,
                                                const Metadata *self) {
  auto wtable = self->getValueWitnesses();
  auto destBuf = (OpaqueValue*)swift_slowAlloc(wtable->size,
                                               wtable->getAlignmentMask());
  *reinterpret_cast<OpaqueValue**>(buffer) = destBuf;
  return destBuf;
}

static void pod_noop(void *object, const Metadata *self) {
}
#define pod_direct_destroy \
  pointer_function_cast<value_witness_types::destroy>(pod_noop)
#define pod_indirect_destroy pod_direct_destroy
#define pod_direct_destroyBuffer \
  pointer_function_cast<value_witness_types::destroyBuffer>(pod_noop)
#define pod_direct_deallocateBuffer \
  pointer_function_cast<value_witness_types::deallocateBuffer>(pod_noop)

static void *pod_noop_return(void *object, const Metadata *self) {
  return object;
}
#define pod_direct_projectBuffer \
  pointer_function_cast<value_witness_types::projectBuffer>(pod_noop_return)
#define pod_direct_allocateBuffer \
  pointer_function_cast<value_witness_types::allocateBuffer>(pod_noop_return)

static OpaqueValue *pod_indirect_initializeBufferWithCopy(ValueBuffer *dest,
                                                          OpaqueValue *src,
                                                          const Metadata *self){
  auto wtable = self->getValueWitnesses();
  auto destBuf = (OpaqueValue*)swift_slowAlloc(wtable->size,
                                               wtable->getAlignmentMask());
  *reinterpret_cast<OpaqueValue**>(dest) = destBuf;
  memcpy(destBuf, src, wtable->size);
  return destBuf;
}
#define pod_indirect_initializeBufferWithTake pod_indirect_initializeBufferWithCopy

static OpaqueValue *pod_direct_initializeWithCopy(OpaqueValue *dest,
                                                  OpaqueValue *src,
                                                  const Metadata *self) {
  memcpy(dest, src, self->getValueWitnesses()->size);
  return dest;
}
#define pod_indirect_initializeWithCopy pod_direct_initializeWithCopy
#define pod_direct_initializeBufferWithCopyOfBuffer \
  pointer_function_cast<value_witness_types::initializeBufferWithCopyOfBuffer> \
    (pod_direct_initializeWithCopy)
#define pod_direct_initializeBufferWithTakeOfBuffer \
  pointer_function_cast<value_witness_types::initializeBufferWithTakeOfBuffer> \
    (pod_direct_initializeWithCopy)
#define pod_direct_initializeBufferWithCopy \
  pointer_function_cast<value_witness_types::initializeBufferWithCopy> \
    (pod_direct_initializeWithCopy)
#define pod_direct_initializeBufferWithTake \
  pointer_function_cast<value_witness_types::initializeBufferWithTake> \
    (pod_direct_initializeWithCopy)
#define pod_direct_assignWithCopy pod_direct_initializeWithCopy
#define pod_indirect_assignWithCopy pod_direct_initializeWithCopy
#define pod_direct_initializeWithTake pod_direct_initializeWithCopy
#define pod_indirect_initializeWithTake pod_direct_initializeWithCopy
#define pod_direct_assignWithTake pod_direct_initializeWithCopy
#define pod_indirect_assignWithTake pod_direct_initializeWithCopy

static void pod_direct_destroyArray(OpaqueValue *, size_t, const Metadata *) {
  // noop
}
#define pod_indirect_destroyArray pod_direct_destroyArray

static OpaqueValue *pod_direct_initializeArrayWithCopy(OpaqueValue *dest,
                                                       OpaqueValue *src,
                                                       size_t n,
                                                       const Metadata *self) {
  auto totalSize = self->getValueWitnesses()->stride * n;
  memcpy(dest, src, totalSize);
  return dest;
}
#define pod_indirect_initializeArrayWithCopy pod_direct_initializeArrayWithCopy

static OpaqueValue *pod_direct_initializeArrayWithTakeFrontToBack(
                                                        OpaqueValue *dest,
                                                        OpaqueValue *src,
                                                        size_t n,
                                                        const Metadata *self) {
  auto totalSize = self->getValueWitnesses()->stride * n;
  memmove(dest, src, totalSize);
  return dest;
}
#define pod_direct_initializeArrayWithTakeBackToFront \
  pod_direct_initializeArrayWithTakeFrontToBack
#define pod_indirect_initializeArrayWithTakeFrontToBack \
  pod_direct_initializeArrayWithTakeFrontToBack
#define pod_indirect_initializeArrayWithTakeBackToFront \
  pod_direct_initializeArrayWithTakeFrontToBack

static constexpr uint64_t sizeWithAlignmentMask(uint64_t size,
                                                uint64_t alignmentMask) {
  return (size << 16) | alignmentMask;
}

void swift::installCommonValueWitnesses(ValueWitnessTable *vwtable) {
  auto flags = vwtable->flags;
  if (flags.isPOD()) {
    // Use POD value witnesses.
    // If the value has a common size and alignment, use specialized value
    // witnesses we already have lying around for the builtin types.
    const ValueWitnessTable *commonVWT;
    switch (sizeWithAlignmentMask(vwtable->size, vwtable->getAlignmentMask())) {
    default:
      // For uncommon layouts, use value witnesses that work with an arbitrary
      // size and alignment.
      if (flags.isInlineStorage()) {
  #define INSTALL_POD_DIRECT_WITNESS(NAME) vwtable->NAME = pod_direct_##NAME;
        FOR_ALL_FUNCTION_VALUE_WITNESSES(INSTALL_POD_DIRECT_WITNESS)
  #undef INSTALL_POD_DIRECT_WITNESS
      } else {
  #define INSTALL_POD_INDIRECT_WITNESS(NAME) vwtable->NAME = pod_indirect_##NAME;
        FOR_ALL_FUNCTION_VALUE_WITNESSES(INSTALL_POD_INDIRECT_WITNESS)
  #undef INSTALL_POD_INDIRECT_WITNESS
      }
      return;
      
    case sizeWithAlignmentMask(1, 0):
      commonVWT = &_TWVBi8_;
      break;
    case sizeWithAlignmentMask(2, 1):
      commonVWT = &_TWVBi16_;
      break;
    case sizeWithAlignmentMask(4, 3):
      commonVWT = &_TWVBi32_;
      break;
    case sizeWithAlignmentMask(8, 7):
      commonVWT = &_TWVBi64_;
      break;
    case sizeWithAlignmentMask(16, 15):
      commonVWT = &_TWVBi128_;
      break;
    case sizeWithAlignmentMask(32, 31):
      commonVWT = &_TWVBi256_;
      break;
    }
    
  #define INSTALL_POD_COMMON_WITNESS(NAME) vwtable->NAME = commonVWT->NAME;
    FOR_ALL_FUNCTION_VALUE_WITNESSES(INSTALL_POD_COMMON_WITNESS)
  #undef INSTALL_POD_COMMON_WITNESS
    
    return;
  }
  
  if (flags.isBitwiseTakable()) {
    // Use POD value witnesses for operations that do an initializeWithTake.
    if (flags.isInlineStorage()) {
      vwtable->initializeWithTake = pod_direct_initializeWithTake;
      vwtable->initializeBufferWithTakeOfBuffer
        = pod_direct_initializeBufferWithTakeOfBuffer;
      vwtable->initializeArrayWithTakeFrontToBack
        = pod_direct_initializeArrayWithTakeFrontToBack;
      vwtable->initializeArrayWithTakeBackToFront
        = pod_direct_initializeArrayWithTakeBackToFront;
    } else {
      vwtable->initializeWithTake = pod_indirect_initializeWithTake;
      vwtable->initializeBufferWithTakeOfBuffer
        = pod_indirect_initializeBufferWithTakeOfBuffer;
      vwtable->initializeArrayWithTakeFrontToBack
        = pod_indirect_initializeArrayWithTakeFrontToBack;
      vwtable->initializeArrayWithTakeBackToFront
        = pod_indirect_initializeArrayWithTakeBackToFront;
    }
    return;
  }

  if (!flags.isInlineStorage()) {
    // For values stored out-of-line, initializeBufferWithTakeOfBuffer is
    // always a memcpy.
    vwtable->initializeBufferWithTakeOfBuffer
      = pod_indirect_initializeBufferWithTakeOfBuffer;
    return;
  }
}

/*** Structs ***************************************************************/

/// Initialize the value witness table and struct field offset vector for a
/// struct, using the "Universal" layout strategy.
void swift::swift_initStructMetadata_UniversalStrategy(size_t numFields,
                                     const TypeLayout * const *fieldTypes,
                                     size_t *fieldOffsets,
                                     ValueWitnessTable *vwtable) {
  auto layout = BasicLayout::initialForValueType();
  performBasicLayout(layout, fieldTypes, numFields,
    [&](size_t i, const TypeLayout *fieldType, size_t offset) {
      assignUnlessEqual(fieldOffsets[i], offset);
    });

  vwtable->size = layout.size;
  vwtable->flags = layout.flags;
  vwtable->stride = layout.stride;
  
  // Substitute in better value witnesses if we have them.
  installCommonValueWitnesses(vwtable);

  // We have extra inhabitants if the first element does.
  // FIXME: generalize this.
  if (fieldTypes[0]->flags.hasExtraInhabitants()) {
    vwtable->flags = vwtable->flags.withExtraInhabitants(true);
    auto xiVWT = cast<ExtraInhabitantsValueWitnessTable>(vwtable);
    xiVWT->extraInhabitantFlags = fieldTypes[0]->getExtraInhabitantFlags();

    // The compiler should already have initialized these.
    assert(xiVWT->storeExtraInhabitant);
    assert(xiVWT->getExtraInhabitantIndex);
  }
}

/*** Classes ***************************************************************/

namespace {
  /// The structure of ObjC class ivars as emitted by compilers.
  struct ClassIvarEntry {
    size_t *Offset;
    const char *Name;
    const char *Type;
    uint32_t Log2Alignment;
    uint32_t Size;
  };

  /// The structure of ObjC class ivar lists as emitted by compilers.
  struct ClassIvarList {
    uint32_t EntrySize;
    uint32_t Count;

    ClassIvarEntry *getIvars() {
      return reinterpret_cast<ClassIvarEntry*>(this+1);
    }
    const ClassIvarEntry *getIvars() const {
      return reinterpret_cast<const ClassIvarEntry*>(this+1);
    }
  };

  /// The structure of ObjC class rodata as emitted by compilers.
  struct ClassROData {
    uint32_t Flags;
    uint32_t InstanceStart;
    uint32_t InstanceSize;
#ifdef __LP64__
    uint32_t Reserved;
#endif
    const uint8_t *IvarLayout;
    const char *Name;
    const void *MethodList;
    const void *ProtocolList;
    ClassIvarList *IvarList;
    const uint8_t *WeakIvarLayout;
    const void *PropertyList;
  };
}

#if SWIFT_OBJC_INTEROP
static uint32_t getLog2AlignmentFromMask(size_t alignMask) {
  assert(((alignMask + 1) & alignMask) == 0 &&
         "not an alignment mask!");

  uint32_t log2 = 0;
  while ((1 << log2) != (alignMask + 1))
    log2++;
  return log2;
}

static inline ClassROData *getROData(ClassMetadata *theClass) {
  return (ClassROData*) (theClass->Data & ~uintptr_t(1));
}

static void _swift_initGenericClassObjCName(ClassMetadata *theClass) {
  // Use the remangler to generate a mangled name from the type metadata.
  auto demangling = _swift_buildDemanglingForMetadata(theClass);

  // Remangle that into a new type mangling string.
  auto typeNode
    = Demangle::NodeFactory::create(Demangle::Node::Kind::TypeMangling);
  typeNode->addChild(demangling);
  auto globalNode
    = Demangle::NodeFactory::create(Demangle::Node::Kind::Global);
  globalNode->addChild(typeNode);
  
  auto string = Demangle::mangleNode(globalNode);
  
  auto fullNameBuf = (char*)swift_slowAlloc(string.size() + 1, 0);
  memcpy(fullNameBuf, string.c_str(), string.size() + 1);

  auto theMetaclass = (ClassMetadata *)object_getClass((id)theClass);

  getROData(theClass)->Name = fullNameBuf;
  getROData(theMetaclass)->Name = fullNameBuf;
}
#endif

/// Initialize the invariant superclass components of a class metadata,
/// such as the generic type arguments, field offsets, and so on.
///
/// This may also relocate the metadata object if it wasn't allocated
/// with enough space.
static ClassMetadata *_swift_initializeSuperclass(ClassMetadata *theClass,
                                                  bool copyFieldOffsetVectors) {
#if SWIFT_OBJC_INTEROP
  // If the class is generic, we need to give it a name for Objective-C.
  if (theClass->getDescription()->GenericParams.isGeneric())
    _swift_initGenericClassObjCName(theClass);
#endif

  const ClassMetadata *theSuperclass = theClass->SuperClass;
  if (theSuperclass == nullptr)
    return theClass;

  // Relocate the metadata if necessary.
  //
  // For now, we assume that relocation is only required when the parent
  // class has prefix matter we didn't know about.  This isn't consistent
  // with general class resilience, however.
  if (theSuperclass->isTypeMetadata()) {
    auto superAP = theSuperclass->getClassAddressPoint();
    auto oldClassAP = theClass->getClassAddressPoint();
    if (superAP > oldClassAP) {
      size_t extraPrefixSize = superAP - oldClassAP;
      size_t oldClassSize = theClass->getClassSize();

      // Allocate a new metadata object.
      auto rawNewClass = (char*) malloc(extraPrefixSize + oldClassSize);
      auto rawOldClass = (const char*) theClass;
      auto rawSuperclass = (const char*) theSuperclass;

      // Copy the extra prefix from the superclass.
      memcpy((void**) (rawNewClass),
             (void* const *) (rawSuperclass - superAP),
             extraPrefixSize);
      // Copy the rest of the data from the derived class.
      memcpy((void**) (rawNewClass + extraPrefixSize),
             (void* const *) (rawOldClass - oldClassAP),
             oldClassSize);

      // Update the class extents on the new metadata object.
      theClass = reinterpret_cast<ClassMetadata*>(rawNewClass + oldClassAP);
      theClass->setClassAddressPoint(superAP);
      theClass->setClassSize(extraPrefixSize + oldClassSize);

      // The previous metadata should be global data, so we have no real
      // choice but to drop it on the floor.
    }
  }

  // If any ancestor classes have generic parameters or field offset
  // vectors, inherit them.
  auto ancestor = theSuperclass;
  auto *classWords = reinterpret_cast<uintptr_t *>(theClass);
  auto *superWords = reinterpret_cast<const uintptr_t *>(theSuperclass);
  while (ancestor && ancestor->isTypeMetadata()) {
    auto &description = ancestor->getDescription();
    auto &genericParams = description->GenericParams;

    // Copy the parent type.
    if (genericParams.Flags.hasParent()) {
      memcpy(classWords + genericParams.Offset - 1,
             superWords + genericParams.Offset - 1,
             sizeof(uintptr_t));
    }

    // Copy the generic requirements.
    if (genericParams.hasGenericRequirements()) {
      unsigned numParamWords = genericParams.NumGenericRequirements;
      memcpy(classWords + genericParams.Offset,
             superWords + genericParams.Offset,
             numParamWords * sizeof(uintptr_t));
    }

    // Copy the field offsets.
    if (copyFieldOffsetVectors &&
        description->Class.hasFieldOffsetVector()) {
      unsigned fieldOffsetVector = description->Class.FieldOffsetVectorOffset;
      memcpy(classWords + fieldOffsetVector,
             superWords + fieldOffsetVector,
             description->Class.NumFields * sizeof(uintptr_t));
    }
    ancestor = ancestor->SuperClass;
  }

#if SWIFT_OBJC_INTEROP
  // Set up the superclass of the metaclass, which is the metaclass of the
  // superclass.
  auto theMetaclass = (ClassMetadata *)object_getClass((id)theClass);
  auto theSuperMetaclass
    = (const ClassMetadata *)object_getClass((id)theSuperclass);
  theMetaclass->SuperClass = theSuperMetaclass;
#endif

  return theClass;
}

#if SWIFT_OBJC_INTEROP
static MetadataAllocator &getResilientMetadataAllocator() {
  // This should be constant-initialized, but this is safe.
  static MetadataAllocator allocator;
  return allocator;
}
#endif

/// Initialize the field offset vector for a dependent-layout class, using the
/// "Universal" layout strategy.
ClassMetadata *
swift::swift_initClassMetadata_UniversalStrategy(ClassMetadata *self,
                                                 size_t numFields,
                                           const ClassFieldLayout *fieldLayouts,
                                                 size_t *fieldOffsets) {
  self = _swift_initializeSuperclass(self, /*copyFieldOffsetVectors=*/true);

  // Start layout by appending to a standard heap object header.
  size_t size, alignMask;

#if SWIFT_OBJC_INTEROP
  ClassROData *rodata = getROData(self);
#endif

  // If we have a superclass, start from its size and alignment instead.
  if (classHasSuperclass(self)) {
    const ClassMetadata *super = self->SuperClass;

    // This is straightforward if the superclass is Swift.
#if SWIFT_OBJC_INTEROP
    if (super->isTypeMetadata()) {
#endif
      size = super->getInstanceSize();
      alignMask = super->getInstanceAlignMask();

#if SWIFT_OBJC_INTEROP
    // If it's Objective-C, start layout from our static notion of
    // where the superclass starts.  Objective-C expects us to have
    // generated a correct ivar layout, which it will simply slide if
    // it needs to.
    } else {
      size = rodata->InstanceStart;
      alignMask = 0xF; // malloc alignment guarantee
    }
#endif

  // If we don't have a formal superclass, start with the basic heap header.
  } else {
    auto heapLayout = BasicLayout::initialForHeapObject();
    size = heapLayout.size;
    alignMask = heapLayout.flags.getAlignmentMask();
  }

#if SWIFT_OBJC_INTEROP
  // In ObjC interop mode, we have up to two places we need each correct
  // ivar offset to end up:
  //
  // - the global ivar offset in the RO-data; this should only exist
  //   if the class layout (up to this ivar) is not actually dependent
  //
  // - the field offset vector (fieldOffsets)
  //
  // When we ask the ObjC runtime to lay out this class, we need the
  // RO-data to point to the field offset vector, even if the layout
  // is not dependent.  The RO-data is not shared between
  // instantiations, but the global ivar offset is (by definition).
  // If the compiler didn't have the correct static size for the
  // superclass (i.e. if rodata->InstanceStart is wrong), a previous
  // instantiation might have already slid the global offset to the
  // correct place; we need the ObjC runtime to see a pre-slid value,
  // and it's not safe to briefly unslide it and let the runtime slide
  // it back because there might already be concurrent code relying on
  // the global ivar offset.
  //
  // So we need to the remember the addresses of the global ivar offsets.
  // We use this lazily-filled SmallVector to do so.
  const unsigned NumInlineGlobalIvarOffsets = 8;
  size_t *_inlineGlobalIvarOffsets[NumInlineGlobalIvarOffsets];
  size_t **_globalIvarOffsets = nullptr;
  auto getGlobalIvarOffsets = [&]() -> size_t** {
    if (!_globalIvarOffsets) {
      if (numFields <= NumInlineGlobalIvarOffsets) {
        _globalIvarOffsets = _inlineGlobalIvarOffsets;
      } else {
        _globalIvarOffsets = new size_t*[numFields];
      }

      // Make sure all the entries start out null.
      memset(_globalIvarOffsets, 0, sizeof(size_t*) * numFields);
    }
    return _globalIvarOffsets;
  };

  // Ensure that Objective-C does layout starting from the right
  // offset.  This needs to exactly match the superclass rodata's
  // InstanceSize in cases where the compiler decided that we didn't
  // really have a resilient ObjC superclass, because the compiler
  // might hardcode offsets in that case, so we can't slide ivars.
  // Fortunately, the cases where that happens are exactly the
  // situations where our entire superclass hierarchy is defined
  // in Swift.  (But note that ObjC might think we have a superclass
  // even if Swift doesn't, because of SwiftObject.)
  rodata->InstanceStart = size;

  auto genericPattern = self->getDescription()->getGenericMetadataPattern();
  auto &allocator =
    genericPattern ? unsafeGetInitializedCache(genericPattern).getAllocator()
                   : getResilientMetadataAllocator();

  // Always clone the ivar descriptors.
  if (numFields) {
    const ClassIvarList *dependentIvars = rodata->IvarList;
    assert(dependentIvars->Count == numFields);
    assert(dependentIvars->EntrySize == sizeof(ClassIvarEntry));

    auto ivarListSize = sizeof(ClassIvarList) +
                        numFields * sizeof(ClassIvarEntry);
    auto ivars = (ClassIvarList*) allocator.alloc(ivarListSize);
    memcpy(ivars, dependentIvars, ivarListSize);
    rodata->IvarList = ivars;

    for (unsigned i = 0; i != numFields; ++i) {
      ClassIvarEntry &ivar = ivars->getIvars()[i];

      // Remember the global ivar offset if present.
      if (ivar.Offset) {
        getGlobalIvarOffsets()[i] = ivar.Offset;
      }

      // Change the ivar offset to point to the respective entry of
      // the field-offset vector, as discussed above.
      ivar.Offset = &fieldOffsets[i];

      // If the ivar's size doesn't match the field layout we
      // computed, overwrite it and give it better type information.
      if (ivar.Size != fieldLayouts[i].Size) {
        ivar.Size = fieldLayouts[i].Size;
        ivar.Type = nullptr;
        ivar.Log2Alignment =
          getLog2AlignmentFromMask(fieldLayouts[i].AlignMask);
      }
    }
  }
#endif

  // Okay, now do layout.
  for (unsigned i = 0; i != numFields; ++i) {
    // Skip empty fields.
    if (fieldOffsets[i] == 0 && fieldLayouts[i].Size == 0)
      continue;
    auto offset = roundUpToAlignMask(size, fieldLayouts[i].AlignMask);
    fieldOffsets[i] = offset;
    size = offset + fieldLayouts[i].Size;
    alignMask = std::max(alignMask, fieldLayouts[i].AlignMask);
  }

  // Save the final size and alignment into the metadata record.
  assert(self->isTypeMetadata());
  self->setInstanceSize(size);
  self->setInstanceAlignMask(alignMask);

#if SWIFT_OBJC_INTEROP
  // Save the size into the Objective-C metadata as well.
  rodata->InstanceSize = size;

  // Register this class with the runtime.  This will also cause the
  // runtime to lay us out.
  swift_instantiateObjCClass(self);

  // If we saved any global ivar offsets, make sure we write back to them.
  if (_globalIvarOffsets) {
    for (unsigned i = 0; i != numFields; ++i) {
      if (!_globalIvarOffsets[i]) continue;

      // To avoid dirtying memory, only write to the global ivar
      // offset if it's actually wrong.
      if (*_globalIvarOffsets[i] != fieldOffsets[i])
        *_globalIvarOffsets[i] = fieldOffsets[i];
    }

    // Free the out-of-line if we allocated one.
    if (_globalIvarOffsets != _inlineGlobalIvarOffsets) {
      delete [] _globalIvarOffsets;
    }
  }
#endif

  return self;
}

/// \brief Fetch the type metadata associated with the formal dynamic
/// type of the given (possibly Objective-C) object.  The formal
/// dynamic type ignores dynamic subclasses such as those introduced
/// by KVO.
///
/// The object pointer may be a tagged pointer, but cannot be null.
const Metadata *swift::swift_getObjectType(HeapObject *object) {
  auto classAsMetadata = _swift_getClass(object);
  if (classAsMetadata->isTypeMetadata()) return classAsMetadata;

  return swift_getObjCClassMetadata(classAsMetadata);
}

/*** Metatypes *************************************************************/

namespace {
  class MetatypeCacheEntry : public CacheEntry<MetatypeCacheEntry> {
    FullMetadata<MetatypeMetadata> Metadata;

  public:
    static const char *getName() { return "MetatypeCache"; }

    MetatypeCacheEntry(size_t numArguments) {}

    static constexpr size_t getNumArguments() {
      return 1;
    }

    FullMetadata<MetatypeMetadata> *getData() {
      return &Metadata;
    }
    const FullMetadata<MetatypeMetadata> *getData() const {
      return &Metadata;
    }
  };
}

/// The uniquing structure for metatype type metadata.
static Lazy<MetadataCache<MetatypeCacheEntry>> MetatypeTypes;

/// \brief Find the appropriate value witness table for the given type.
static const ValueWitnessTable *
getMetatypeValueWitnesses(const Metadata *instanceType) {
  // When metatypes are accessed opaquely, they always have a "thick"
  // representation.
  return &getUnmanagedPointerPointerValueWitnesses();
}

/// \brief Fetch a uniqued metadata for a metatype type.
SWIFT_RUNTIME_EXPORT
extern "C" const MetatypeMetadata *
swift::swift_getMetatypeMetadata(const Metadata *instanceMetadata) {
  // Search the cache.
  const size_t numGenericArgs = 1;
  const void *args[] = { instanceMetadata };
  auto &Types = MetatypeTypes.get();
  auto entry = Types.findOrAdd(args, numGenericArgs,
    [&]() -> MetatypeCacheEntry* {
      // Create a new entry for the cache.
      auto entry = MetatypeCacheEntry::allocate(Types.getAllocator(),
                                                args, numGenericArgs, 0);

      auto metadata = entry->getData();
      metadata->setKind(MetadataKind::Metatype);
      metadata->ValueWitnesses = getMetatypeValueWitnesses(instanceMetadata);
      metadata->InstanceType = instanceMetadata;

      return entry;
    });

  return entry->getData();
}

/*** Existential Metatypes *************************************************/

namespace {
  class ExistentialMetatypeCacheEntry :
      public CacheEntry<ExistentialMetatypeCacheEntry> {
    FullMetadata<ExistentialMetatypeMetadata> Metadata;

  public:
    static const char *getName() { return "ExistentialMetatypeCache"; }

    ExistentialMetatypeCacheEntry(size_t numArguments) {}

    static constexpr size_t getNumArguments() {
      return 1;
    }

    FullMetadata<ExistentialMetatypeMetadata> *getData() {
      return &Metadata;
    }
    const FullMetadata<ExistentialMetatypeMetadata> *getData() const {
      return &Metadata;
    }
  };
}

struct ExistentialMetatypeState {
  MetadataCache<ExistentialMetatypeCacheEntry> Types;
  llvm::DenseMap<unsigned, const ExtraInhabitantsValueWitnessTable*>
    ValueWitnessTables;
};

/// The uniquing structure for existential metatype type metadata.
static Lazy<ExistentialMetatypeState> ExistentialMetatypes;

static const ExtraInhabitantsValueWitnessTable
ExistentialMetatypeValueWitnesses_1 =
  ValueWitnessTableForBox<ExistentialMetatypeBox<1>>::table;
static const ExtraInhabitantsValueWitnessTable
ExistentialMetatypeValueWitnesses_2 =
  ValueWitnessTableForBox<ExistentialMetatypeBox<2>>::table;

/// Instantiate a value witness table for an existential metatype
/// container with the given number of witness table pointers.
static const ExtraInhabitantsValueWitnessTable *
getExistentialMetatypeValueWitnesses(ExistentialMetatypeState &EM,
                                     unsigned numWitnessTables) {
  if (numWitnessTables == 0)
    return &getUnmanagedPointerPointerValueWitnesses();
  if (numWitnessTables == 1)
    return &ExistentialMetatypeValueWitnesses_1;
  if (numWitnessTables == 2)
    return &ExistentialMetatypeValueWitnesses_2;

  static_assert(3 * sizeof(void*) >= sizeof(ValueBuffer),
                "not handling all possible inline-storage class existentials!");

  auto found = EM.ValueWitnessTables.find(numWitnessTables);
  if (found != EM.ValueWitnessTables.end())
    return found->second;

  using Box = NonFixedExistentialMetatypeBox;
  using Witnesses = NonFixedValueWitnesses<Box, /*known allocated*/ true>;

  auto *vwt = new ExtraInhabitantsValueWitnessTable;
#define STORE_VAR_EXISTENTIAL_METATYPE_WITNESS(WITNESS) \
  vwt->WITNESS = Witnesses::WITNESS;
  FOR_ALL_FUNCTION_VALUE_WITNESSES(STORE_VAR_EXISTENTIAL_METATYPE_WITNESS)
  STORE_VAR_EXISTENTIAL_METATYPE_WITNESS(storeExtraInhabitant)
  STORE_VAR_EXISTENTIAL_METATYPE_WITNESS(getExtraInhabitantIndex)
#undef STORE_VAR_EXISTENTIAL_METATYPE_WITNESS

  vwt->size = Box::Container::getSize(numWitnessTables);
  vwt->flags = ValueWitnessFlags()
    .withAlignment(Box::Container::getAlignment(numWitnessTables))
    .withPOD(true)
    .withBitwiseTakable(true)
    .withInlineStorage(false)
    .withExtraInhabitants(true);
  vwt->stride = Box::Container::getStride(numWitnessTables);
  vwt->extraInhabitantFlags = ExtraInhabitantFlags()
    .withNumExtraInhabitants(Witnesses::numExtraInhabitants);

  EM.ValueWitnessTables.insert({numWitnessTables, vwt});

  return vwt;
}

/// \brief Fetch a uniqued metadata for a metatype type.
SWIFT_RUNTIME_EXPORT
extern "C" const ExistentialMetatypeMetadata *
swift::swift_getExistentialMetatypeMetadata(const Metadata *instanceMetadata) {
  // Search the cache.
  const size_t numGenericArgs = 1;
  const void *args[] = { instanceMetadata };
  auto &EM = ExistentialMetatypes.get();
  auto entry = EM.Types.findOrAdd(args, numGenericArgs,
    [&]() -> ExistentialMetatypeCacheEntry* {
      // Create a new entry for the cache.
      auto entry =
        ExistentialMetatypeCacheEntry::allocate(EM.Types.getAllocator(),
                                                args, numGenericArgs, 0);

      ExistentialTypeFlags flags;
      if (instanceMetadata->getKind() == MetadataKind::Existential) {
        flags = static_cast<const ExistentialTypeMetadata*>(instanceMetadata)->Flags;
      } else {
        assert(instanceMetadata->getKind()==MetadataKind::ExistentialMetatype);
        flags = static_cast<const ExistentialMetatypeMetadata*>(instanceMetadata)->Flags;
      }

      auto metadata = entry->getData();
      metadata->setKind(MetadataKind::ExistentialMetatype);
      metadata->ValueWitnesses =
        getExistentialMetatypeValueWitnesses(EM, flags.getNumWitnessTables());
      metadata->InstanceType = instanceMetadata;
      metadata->Flags = flags;

      return entry;
    });

  return entry->getData();
}

/*** Existential types ********************************************************/

namespace {
  class ExistentialCacheEntry : public CacheEntry<ExistentialCacheEntry> {
  public:
    FullMetadata<ExistentialTypeMetadata> Metadata;

    static const char *getName() { return "ExistentialCache"; }

    ExistentialCacheEntry(size_t numArguments) {
      Metadata.Protocols.NumProtocols = numArguments;
    }

    size_t getNumArguments() const {
      return Metadata.Protocols.NumProtocols;
    }

    FullMetadata<ExistentialTypeMetadata> *getData() {
      return &Metadata;
    }
    const FullMetadata<ExistentialTypeMetadata> *getData() const {
      return &Metadata;
    }
  };
}

struct ExistentialTypeState {
  MetadataCache<ExistentialCacheEntry> Types;
  llvm::DenseMap<unsigned, const ValueWitnessTable*> OpaqueValueWitnessTables;
  llvm::DenseMap<unsigned, const ExtraInhabitantsValueWitnessTable*>
    ClassValueWitnessTables;
};

/// The uniquing structure for existential type metadata.
static Lazy<ExistentialTypeState> Existentials;

static const ValueWitnessTable OpaqueExistentialValueWitnesses_0 =
  ValueWitnessTableForBox<OpaqueExistentialBox<0>>::table;
static const ValueWitnessTable OpaqueExistentialValueWitnesses_1 =
  ValueWitnessTableForBox<OpaqueExistentialBox<1>>::table;

/// Instantiate a value witness table for an opaque existential container with
/// the given number of witness table pointers.
static const ValueWitnessTable *
getOpaqueExistentialValueWitnesses(ExistentialTypeState &E,
                                   unsigned numWitnessTables) {
  // We pre-allocate a couple of important cases.
  if (numWitnessTables == 0)
    return &OpaqueExistentialValueWitnesses_0;
  if (numWitnessTables == 1)
    return &OpaqueExistentialValueWitnesses_1;

  // FIXME: make thread-safe

  auto found = E.OpaqueValueWitnessTables.find(numWitnessTables);
  if (found != E.OpaqueValueWitnessTables.end())
    return found->second;

  using Box = NonFixedOpaqueExistentialBox;
  using Witnesses = NonFixedValueWitnesses<Box, /*known allocated*/ true>;
  static_assert(!Witnesses::hasExtraInhabitants, "no extra inhabitants");

  auto *vwt = new ValueWitnessTable;
#define STORE_VAR_OPAQUE_EXISTENTIAL_WITNESS(WITNESS) \
  vwt->WITNESS = Witnesses::WITNESS;
  FOR_ALL_FUNCTION_VALUE_WITNESSES(STORE_VAR_OPAQUE_EXISTENTIAL_WITNESS)
#undef STORE_VAR_OPAQUE_EXISTENTIAL_WITNESS

  vwt->size = Box::Container::getSize(numWitnessTables);
  vwt->flags = ValueWitnessFlags()
    .withAlignment(Box::Container::getAlignment(numWitnessTables))
    .withPOD(false)
    .withBitwiseTakable(false)
    .withInlineStorage(false)
    .withExtraInhabitants(false);
  vwt->stride = Box::Container::getStride(numWitnessTables);

  E.OpaqueValueWitnessTables.insert({numWitnessTables, vwt});

  return vwt;
}

static const ExtraInhabitantsValueWitnessTable ClassExistentialValueWitnesses_1 =
  ValueWitnessTableForBox<ClassExistentialBox<1>>::table;
static const ExtraInhabitantsValueWitnessTable ClassExistentialValueWitnesses_2 =
  ValueWitnessTableForBox<ClassExistentialBox<2>>::table;

/// Instantiate a value witness table for a class-constrained existential
/// container with the given number of witness table pointers.
static const ExtraInhabitantsValueWitnessTable *
getClassExistentialValueWitnesses(ExistentialTypeState &E,
                                  unsigned numWitnessTables) {
  if (numWitnessTables == 0) {
#if SWIFT_OBJC_INTEROP
    return &_TWVBO;
#else
    return &_TWVBo;
#endif
  }
  if (numWitnessTables == 1)
    return &ClassExistentialValueWitnesses_1;
  if (numWitnessTables == 2)
    return &ClassExistentialValueWitnesses_2;

  static_assert(3 * sizeof(void*) >= sizeof(ValueBuffer),
                "not handling all possible inline-storage class existentials!");

  auto found = E.ClassValueWitnessTables.find(numWitnessTables);
  if (found != E.ClassValueWitnessTables.end())
    return found->second;

  using Box = NonFixedClassExistentialBox;
  using Witnesses = NonFixedValueWitnesses<Box, /*known allocated*/ true>;

  auto *vwt = new ExtraInhabitantsValueWitnessTable;
#define STORE_VAR_CLASS_EXISTENTIAL_WITNESS(WITNESS) \
  vwt->WITNESS = Witnesses::WITNESS;
  FOR_ALL_FUNCTION_VALUE_WITNESSES(STORE_VAR_CLASS_EXISTENTIAL_WITNESS)
  STORE_VAR_CLASS_EXISTENTIAL_WITNESS(storeExtraInhabitant)
  STORE_VAR_CLASS_EXISTENTIAL_WITNESS(getExtraInhabitantIndex)
#undef STORE_VAR_CLASS_EXISTENTIAL_WITNESS

  vwt->size = Box::Container::getSize(numWitnessTables);
  vwt->flags = ValueWitnessFlags()
    .withAlignment(Box::Container::getAlignment(numWitnessTables))
    .withPOD(false)
    .withBitwiseTakable(true)
    .withInlineStorage(false)
    .withExtraInhabitants(true);
  vwt->stride = Box::Container::getStride(numWitnessTables);
  vwt->extraInhabitantFlags = ExtraInhabitantFlags()
    .withNumExtraInhabitants(Witnesses::numExtraInhabitants);

  E.ClassValueWitnessTables.insert({numWitnessTables, vwt});

  return vwt;
}

/// Get the value witness table for an existential type, first trying to use a
/// shared specialized table for common cases.
static const ValueWitnessTable *
getExistentialValueWitnesses(ExistentialTypeState &E,
                             ProtocolClassConstraint classConstraint,
                             unsigned numWitnessTables,
                             SpecialProtocol special) {
  // Use special representation for special protocols.
  switch (special) {
  case SpecialProtocol::Error:
#if SWIFT_OBJC_INTEROP
    // Error always has a single-ObjC-refcounted representation.
    return &_TWVBO;
#else
    // Without ObjC interop, Error is native-refcounted.
    return &_TWVBo;
#endif
      
  // Other existentials use standard representation.
  case SpecialProtocol::AnyObject:
  case SpecialProtocol::None:
    break;
  }
  
  switch (classConstraint) {
  case ProtocolClassConstraint::Class:
    return getClassExistentialValueWitnesses(E, numWitnessTables);
  case ProtocolClassConstraint::Any:
    return getOpaqueExistentialValueWitnesses(E, numWitnessTables);
  }
}

template<> ExistentialTypeRepresentation
ExistentialTypeMetadata::getRepresentation() const {
  // Some existentials use special containers.
  switch (Flags.getSpecialProtocol()) {
  case SpecialProtocol::Error:
    return ExistentialTypeRepresentation::Error;
  case SpecialProtocol::AnyObject:
  case SpecialProtocol::None:
    break;
  }
  // The layout of standard containers depends on whether the existential is
  // class-constrained.
  if (isClassBounded())
    return ExistentialTypeRepresentation::Class;
  return ExistentialTypeRepresentation::Opaque;
}

template<> bool
ExistentialTypeMetadata::mayTakeValue(const OpaqueValue *container) const {
  switch (getRepresentation()) {
  // Owning a reference to a class existential is equivalent to owning a
  // reference to the contained class instance.
  case ExistentialTypeRepresentation::Class:
    return true;
  // Opaque existential containers uniquely own their contained value.
  case ExistentialTypeRepresentation::Opaque:
    return true;
    
  // References to boxed existential containers may be shared.
  case ExistentialTypeRepresentation::Error: {
    // We can only take the value if the box is a bridged NSError, in which case
    // owning a reference to the box is owning a reference to the NSError.
    // TODO: Or if the box is uniquely referenced. We don't have intimate
    // enough knowledge of CF refcounting to check for that dynamically yet.
    const SwiftError *errorBox
      = *reinterpret_cast<const SwiftError * const *>(container);
    return errorBox->isPureNSError();
  }
  }
}

template<> void
ExistentialTypeMetadata::deinitExistentialContainer(OpaqueValue *container)
const {
  switch (getRepresentation()) {
  case ExistentialTypeRepresentation::Class:
    // Nothing to clean up after taking the class reference.
    break;
  
  case ExistentialTypeRepresentation::Opaque: {
    // Containing the value may require a side allocation, which we need
    // to clean up.
    auto opaque = reinterpret_cast<OpaqueExistentialContainer *>(container);
    opaque->Type->vw_deallocateBuffer(&opaque->Buffer);
    break;
  }
  
  case ExistentialTypeRepresentation::Error:
    // TODO: If we were able to claim the value from a uniquely-owned
    // existential box, we would want to deallocError here.
    break;
  }
}

template<> const OpaqueValue *
ExistentialTypeMetadata::projectValue(const OpaqueValue *container) const {
  switch (getRepresentation()) {
  case ExistentialTypeRepresentation::Class: {
    auto classContainer =
      reinterpret_cast<const ClassExistentialContainer*>(container);
    return reinterpret_cast<const OpaqueValue *>(&classContainer->Value);
  }
  case ExistentialTypeRepresentation::Opaque: {
    auto opaqueContainer =
      reinterpret_cast<const OpaqueExistentialContainer*>(container);
    return opaqueContainer->Type->vw_projectBuffer(
                         const_cast<ValueBuffer*>(&opaqueContainer->Buffer));
  }
  case ExistentialTypeRepresentation::Error: {
    const SwiftError *errorBox
      = *reinterpret_cast<const SwiftError * const *>(container);
    // If the error is a bridged NSError, then the "box" is in fact itself
    // the value.
    if (errorBox->isPureNSError())
      return container;
    return errorBox->getValue();
  }
  }
}

template<> const Metadata *
ExistentialTypeMetadata::getDynamicType(const OpaqueValue *container) const {
  switch (getRepresentation()) {
  case ExistentialTypeRepresentation::Class: {
    auto classContainer =
      reinterpret_cast<const ClassExistentialContainer*>(container);
    void *obj = classContainer->Value;
    return swift_getObjectType(reinterpret_cast<HeapObject*>(obj));
  }
  case ExistentialTypeRepresentation::Opaque: {
    auto opaqueContainer =
      reinterpret_cast<const OpaqueExistentialContainer*>(container);
    return opaqueContainer->Type;
  }
  case ExistentialTypeRepresentation::Error: {
    const SwiftError *errorBox
      = *reinterpret_cast<const SwiftError * const *>(container);
    return errorBox->getType();
  }
  }
}

template<> const WitnessTable *
ExistentialTypeMetadata::getWitnessTable(const OpaqueValue *container,
                                         unsigned i) const {
  assert(i < Flags.getNumWitnessTables());

  // The layout of the container depends on whether it's class-constrained
  // or a special protocol.
  const WitnessTable * const *witnessTables;
  
  switch (getRepresentation()) {
  case ExistentialTypeRepresentation::Class: {
    auto classContainer =
      reinterpret_cast<const ClassExistentialContainer*>(container);
    witnessTables = classContainer->getWitnessTables();
    break;
  }
  case ExistentialTypeRepresentation::Opaque: {
    auto opaqueContainer =
      reinterpret_cast<const OpaqueExistentialContainer*>(container);
    witnessTables = opaqueContainer->getWitnessTables();
    break;
  }
  case ExistentialTypeRepresentation::Error: {
    // Only one witness table we should be able to return, which is the
    // Error.
    assert(i == 0 && "only one witness table in an Error box");
    const SwiftError *errorBox
      = *reinterpret_cast<const SwiftError * const *>(container);
    return errorBox->getErrorConformance();
  }
  }

  // The return type here describes extra structure for the protocol
  // witness table for some reason.  We should probably have a nominal
  // type for these, just for type safety reasons.
  return witnessTables[i];
}

/// \brief Fetch a uniqued metadata for an existential type. The array
/// referenced by \c protocols will be sorted in-place.
SWIFT_RT_ENTRY_VISIBILITY
const ExistentialTypeMetadata *
swift::swift_getExistentialTypeMetadata(size_t numProtocols,
                                        const ProtocolDescriptor **protocols)
    SWIFT_CC(RegisterPreservingCC_IMPL) {
  // Sort the protocol set.
  std::sort(protocols, protocols + numProtocols);

  // Calculate the class constraint and number of witness tables for the
  // protocol set.
  unsigned numWitnessTables = 0;
  ProtocolClassConstraint classConstraint = ProtocolClassConstraint::Any;
  for (auto p : make_range(protocols, protocols + numProtocols)) {
    if (p->Flags.needsWitnessTable()) {
      ++numWitnessTables;
    }
    if (p->Flags.getClassConstraint() == ProtocolClassConstraint::Class)
      classConstraint = ProtocolClassConstraint::Class;
  }

  // Search the cache.

  auto protocolArgs = reinterpret_cast<const void * const *>(protocols);

  auto &E = Existentials.get();
  auto entry = E.Types.findOrAdd(protocolArgs, numProtocols,
    [&]() -> ExistentialCacheEntry* {
      // Create a new entry for the cache.
      auto entry = ExistentialCacheEntry::allocate(E.Types.getAllocator(),
                             protocolArgs, numProtocols,
                             sizeof(const ProtocolDescriptor *) * numProtocols);
      auto metadata = entry->getData();
      
      // Get the special protocol kind for an uncomposed protocol existential.
      // Protocol compositions are currently never special.
      auto special = SpecialProtocol::None;
      if (numProtocols == 1)
        special = protocols[0]->Flags.getSpecialProtocol();
      
      metadata->setKind(MetadataKind::Existential);
      metadata->ValueWitnesses = getExistentialValueWitnesses(E,
                                                              classConstraint,
                                                              numWitnessTables,
                                                              special);
      metadata->Flags = ExistentialTypeFlags()
        .withNumWitnessTables(numWitnessTables)
        .withClassConstraint(classConstraint)
        .withSpecialProtocol(special);
      metadata->Protocols.NumProtocols = numProtocols;
      for (size_t i = 0; i < numProtocols; ++i)
        metadata->Protocols[i] = protocols[i];

      return entry;
    });
  return entry->getData();
}

/// \brief Perform a copy-assignment from one existential container to another.
/// Both containers must be of the same existential type representable with no
/// witness tables.
OpaqueValue *swift::swift_assignExistentialWithCopy0(OpaqueValue *dest,
                                                     const OpaqueValue *src,
                                                     const Metadata *type) {
  using Witnesses = ValueWitnesses<OpaqueExistentialBox<0>>;
  return Witnesses::assignWithCopy(dest, const_cast<OpaqueValue*>(src), type);
}

/// \brief Perform a copy-assignment from one existential container to another.
/// Both containers must be of the same existential type representable with one
/// witness table.
OpaqueValue *swift::swift_assignExistentialWithCopy1(OpaqueValue *dest,
                                                     const OpaqueValue *src,
                                                     const Metadata *type) {
  using Witnesses = ValueWitnesses<OpaqueExistentialBox<1>>;
  return Witnesses::assignWithCopy(dest, const_cast<OpaqueValue*>(src), type);
}

/// \brief Perform a copy-assignment from one existential container to another.
/// Both containers must be of the same existential type representable with the
/// same number of witness tables.
OpaqueValue *swift::swift_assignExistentialWithCopy(OpaqueValue *dest,
                                                    const OpaqueValue *src,
                                                    const Metadata *type) {
  assert(!type->getValueWitnesses()->isValueInline());
  using Witnesses = NonFixedValueWitnesses<NonFixedOpaqueExistentialBox,
                                           /*known allocated*/ true>;
  return Witnesses::assignWithCopy(dest, const_cast<OpaqueValue*>(src), type);
}

/*** Foreign types *********************************************************/

namespace {
  /// A string whose data is globally-allocated.
  struct GlobalString {
    StringRef Data;
    /*implicit*/ GlobalString(StringRef data) : Data(data) {}
  };
}

template <>
struct llvm::DenseMapInfo<GlobalString> {
  static GlobalString getEmptyKey() {
    return StringRef((const char*) 0, 0);
  }
  static GlobalString getTombstoneKey() {
    return StringRef((const char*) 1, 0);
  }
  static unsigned getHashValue(const GlobalString &val) {
    // llvm::hash_value(StringRef) is, unfortunately, defined out of
    // line in a library we otherwise would not need to link against.
    return llvm::hash_combine_range(val.Data.begin(), val.Data.end());
  }
  static bool isEqual(const GlobalString &lhs, const GlobalString &rhs) {
    return lhs.Data == rhs.Data;
  }
};

// We use a DenseMap over what are essentially StringRefs instead of a
// StringMap because we don't need to actually copy the string.
namespace {
struct ForeignTypeState {
  Mutex Lock;
  ConditionVariable InitializationWaiters;
  llvm::DenseMap<GlobalString, const ForeignTypeMetadata *> Types;
};
}

static Lazy<ForeignTypeState> ForeignTypes;

const ForeignTypeMetadata *
swift::swift_getForeignTypeMetadata(ForeignTypeMetadata *nonUnique) {
  // Fast path: check the invasive cache.
  if (auto unique = nonUnique->getCachedUniqueMetadata()) {
    return unique;
  }

  // Okay, check the global map.
  auto &foreignTypes = ForeignTypes.get();
  GlobalString key(nonUnique->getName());
  bool hasInit = nonUnique->hasInitializationFunction();

  const ForeignTypeMetadata *uniqueMetadata;
  bool inserted;

  // A helper function to find the current entry for the key using the
  // saved iterator if it's still valid.  This should only be called
  // while the lock is held.
  decltype(foreignTypes.Types.begin()) savedIterator;
  size_t savedSize;
  auto getCurrentEntry = [&]() -> const ForeignTypeMetadata *& {
    // The iterator may have been invalidated if the size of the map
    // has changed since the last lookup.
    if (foreignTypes.Types.size() != savedSize) {
      savedSize = foreignTypes.Types.size();
      savedIterator = foreignTypes.Types.find(key);
      assert(savedIterator != foreignTypes.Types.end() &&
             "entries cannot be removed from foreign types metadata map");
    }
    return savedIterator->second;
  };

  {
    ScopedLock guard(foreignTypes.Lock);

    // Try to create an entry in the map.  The initial value of the entry
    // is our copy of the metadata unless it has an initialization function,
    // in which case we have to insert null as a placeholder to tell others
    // to wait while we call the initializer.
    auto valueToInsert = (hasInit ? nullptr : nonUnique);
    auto insertResult = foreignTypes.Types.insert({key, valueToInsert});
    inserted = insertResult.second;
    savedIterator = insertResult.first;
    savedSize = foreignTypes.Types.size();
    uniqueMetadata = savedIterator->second;

    // If we created the entry, then the unique metadata is our copy.
    if (inserted) {
      uniqueMetadata = nonUnique;

    // If we didn't create the entry, but it's null, then we have to wait
    // until it becomes non-null.
    } else {
      while (uniqueMetadata == nullptr) {
        foreignTypes.Lock.wait(foreignTypes.InitializationWaiters);
        uniqueMetadata = getCurrentEntry();
      }
    }
  }

  // If we inserted the entry and there's an initialization function,
  // call it.  This has to be done with the lock dropped.
  if (inserted && hasInit) {
    nonUnique->getInitializationFunction()(nonUnique);

    // Update the cache entry:

    //   - Reacquire the lock.
    ScopedLock guard(foreignTypes.Lock);

    //   - Change the entry.
    auto &entry = getCurrentEntry();
    assert(entry == nullptr);
    entry = nonUnique;

    //   - Notify waiters.
    foreignTypes.InitializationWaiters.notifyAll();
  }

  // Remember the unique result in the invasive cache.  We don't want
  // to do this until after the initialization completes; otherwise,
  // it will be possible for code to fast-path through this function
  // too soon.
  nonUnique->setCachedUniqueMetadata(uniqueMetadata);

  return uniqueMetadata;
}

/*** Other metadata routines ***********************************************/

template<> const GenericMetadata *
Metadata::getGenericPattern() const {
  auto &ntd = getNominalTypeDescriptor();
  if (!ntd)
    return nullptr;
  return ntd->getGenericMetadataPattern();
}

template<> const ClassMetadata *
Metadata::getClassObject() const {
  switch (getKind()) {
  case MetadataKind::Class: {
    // Native Swift class metadata is also the class object.
    return static_cast<const ClassMetadata *>(this);
  }
  case MetadataKind::ObjCClassWrapper: {
    // Objective-C class objects are referenced by their Swift metadata wrapper.
    auto wrapper = static_cast<const ObjCClassWrapperMetadata *>(this);
    return wrapper->Class;
  }
  // Other kinds of types don't have class objects.
  case MetadataKind::Struct:
  case MetadataKind::Enum:
  case MetadataKind::Optional:
  case MetadataKind::ForeignClass:
  case MetadataKind::Opaque:
  case MetadataKind::Tuple:
  case MetadataKind::Function:
  case MetadataKind::Existential:
  case MetadataKind::ExistentialMetatype:
  case MetadataKind::Metatype:
  case MetadataKind::HeapLocalVariable:
  case MetadataKind::HeapGenericLocalVariable:
  case MetadataKind::ErrorObject:
    return nullptr;
  }
}

#ifndef NDEBUG
SWIFT_RUNTIME_EXPORT
extern "C"
void _swift_debug_verifyTypeLayoutAttribute(Metadata *type,
                                            const void *runtimeValue,
                                            const void *staticValue,
                                            size_t size,
                                            const char *description) {
  auto presentValue = [&](const void *value) {
    if (size < sizeof(long long)) {
      long long intValue = 0;
      memcpy(&intValue, value, size);
      fprintf(stderr, "%lld (%#llx)\n                  ", intValue, intValue);
    }
    auto bytes = reinterpret_cast<const uint8_t *>(value);
    for (unsigned i = 0; i < size; ++i) {
      fprintf(stderr, "%02x ", bytes[i]);
    }
    fprintf(stderr, "\n");
  };
  
  if (memcmp(runtimeValue, staticValue, size) != 0) {
    auto typeName = nameForMetadata(type);
    fprintf(stderr, "*** Type verification of %s %s failed ***\n",
            typeName.c_str(), description);
    
    fprintf(stderr, "  runtime value:  ");
    presentValue(runtimeValue);
    fprintf(stderr, "  compiler value: ");
    presentValue(staticValue);
  }
}
#endif

/*** Protocol witness tables *************************************************/

namespace {
  class WitnessTableCacheEntry : public CacheEntry<WitnessTableCacheEntry> {
  public:
    static const char *getName() { return "WitnessTableCache"; }

    WitnessTableCacheEntry(size_t numArguments) {
      assert(numArguments == getNumArguments());
    }

    static constexpr size_t getNumArguments() {
      return 1;
    }

    /// Advance the address point to the end of the private storage area.
    WitnessTable *get(GenericWitnessTable *genericTable) const {
      return reinterpret_cast<WitnessTable *>(
          const_cast<void **>(getData<void *>()) +
          genericTable->WitnessTablePrivateSizeInWords);
    }
  };
}

using GenericWitnessTableCache = MetadataCache<WitnessTableCacheEntry>;
using LazyGenericWitnessTableCache = Lazy<GenericWitnessTableCache>;

/// Fetch the cache for a generic witness-table structure.
static GenericWitnessTableCache &getCache(GenericWitnessTable *gen) {
  // Keep this assert even if you change the representation above.
  static_assert(sizeof(LazyGenericWitnessTableCache) <=
                sizeof(GenericWitnessTable::PrivateData),
                "metadata cache is larger than the allowed space");

  auto lazyCache =
    reinterpret_cast<LazyGenericWitnessTableCache*>(gen->PrivateData);
  return lazyCache->get();
}

/// If there's no initializer, no private storage, and all requirements
/// are present, we don't have to instantiate anything; just return the
/// witness table template.
///
/// Most of the time IRGen should be able to determine this statically;
/// the one case is with resilient conformances, where the resilient
/// protocol has not yet changed in a way that's incompatible with the
/// conformance.
static bool doesNotRequireInstantiation(GenericWitnessTable *genericTable) {
  if (genericTable->Instantiator.isNull() &&
      genericTable->WitnessTablePrivateSizeInWords == 0 &&
      (genericTable->Protocol.isNull() ||
       genericTable->WitnessTableSizeInWords -
       genericTable->Protocol->MinimumWitnessTableSizeInWords ==
       genericTable->Protocol->DefaultWitnessTableSizeInWords)) {
    return true;
  }

  return false;
}

/// Instantiate a brand new witness table for a resilient or generic
/// protocol conformance.
static WitnessTableCacheEntry *
allocateWitnessTable(GenericWitnessTable *genericTable,
                     MetadataAllocator &allocator,
                     const void *args[],
                     size_t numGenericArgs) {

  // Number of bytes for any private storage used by the conformance itself.
  size_t privateSize = genericTable->WitnessTablePrivateSizeInWords * sizeof(void *);

  size_t minWitnessTableSize, expectedWitnessTableSize;
  size_t actualWitnessTableSize = genericTable->WitnessTableSizeInWords * sizeof(void *);

  auto protocol = genericTable->Protocol.get();

  if (protocol != nullptr && protocol->Flags.isResilient()) {
    // The protocol and conforming type are in different resilience domains.
    // Allocate the witness table with the correct size, and fill in default
    // requirements at the end as needed.
    minWitnessTableSize = (protocol->MinimumWitnessTableSizeInWords *
                           sizeof(void *));
    expectedWitnessTableSize = ((protocol->MinimumWitnessTableSizeInWords +
                                 protocol->DefaultWitnessTableSizeInWords) *
                                sizeof(void *));
    assert(actualWitnessTableSize >= minWitnessTableSize &&
           actualWitnessTableSize <= expectedWitnessTableSize);
  } else {
    // The protocol and conforming type are in the same resilience domain.
    // Trust that the witness table template already has the correct size.
    minWitnessTableSize = expectedWitnessTableSize = actualWitnessTableSize;
  }

  // Create a new entry for the cache.
  auto entry = WitnessTableCacheEntry::allocate(
      allocator, args, numGenericArgs,
      privateSize + expectedWitnessTableSize);

  char *fullTable = entry->getData<char>();

  // Zero out the private storage area.
  memset(fullTable, 0, privateSize);

  // Advance the address point; the private storage area is accessed via
  // negative offsets.
  auto *table = entry->get(genericTable);

  // Fill in the provided part of the requirements from the pattern.
  memcpy(table, (void * const *) &*genericTable->Pattern,
         actualWitnessTableSize);

  // If this is a resilient conformance, copy in the rest.
  if (protocol != nullptr && protocol->Flags.isResilient()) {
    memcpy((char *) table + actualWitnessTableSize,
           (char *) protocol->getDefaultWitnesses() +
              (actualWitnessTableSize - minWitnessTableSize),
           expectedWitnessTableSize - actualWitnessTableSize);
  }

  return entry;
}

SWIFT_RT_ENTRY_VISIBILITY
extern "C" const WitnessTable *
swift::swift_getGenericWitnessTable(GenericWitnessTable *genericTable,
                                    const Metadata *type,
                                    void * const *instantiationArgs)
    SWIFT_CC(RegisterPreservingCC_IMPL) {
  if (doesNotRequireInstantiation(genericTable)) {
    return genericTable->Pattern;
  }

  // If type is not nullptr, the witness table depends on the substituted
  // conforming type, so use that are the key.
  constexpr const size_t numGenericArgs = 1;
  const void *args[] = { type };

  auto &cache = getCache(genericTable);
  auto entry = cache.findOrAdd(args, numGenericArgs,
    [&]() -> WitnessTableCacheEntry* {
      // Allocate the witness table and fill it in.
      auto entry = allocateWitnessTable(genericTable,
                                        cache.getAllocator(),
                                        args, numGenericArgs);

      // Call the instantiation function to initialize
      // dependent associated type metadata.
      if (!genericTable->Instantiator.isNull()) {
        genericTable->Instantiator(entry->get(genericTable),
                                   type, instantiationArgs);
      }

      return entry;
    });

  return entry->get(genericTable);
}

uint64_t swift::RelativeDirectPointerNullPtr = 0;
