//===--- MetadataCache.h - Implements the metadata cache --------*- C++ -*-===//
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
#ifndef SWIFT_RUNTIME_METADATACACHE_H
#define SWIFT_RUNTIME_METADATACACHE_H

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "swift/Runtime/Concurrent.h"
#include "swift/Runtime/Metadata.h"
#include "swift/Runtime/Mutex.h"
#include <condition_variable>
#include <thread>

#ifndef SWIFT_DEBUG_RUNTIME
#define SWIFT_DEBUG_RUNTIME 0
#endif

namespace swift {

/// A bump pointer for metadata allocations. Since metadata is (currently)
/// never released, it does not support deallocation. This allocator by itself
/// is not thread-safe; in concurrent uses, allocations must be guarded by
/// a lock, such as the per-metadata-cache lock used to guard metadata
/// instantiations. All allocations are pointer-aligned.
class MetadataAllocator {
  /// Address of the next available space. The allocator grabs a page at a time,
  /// so the need for a new page can be determined by page alignment.
  ///
  /// Initializing to -1 instead of nullptr ensures that the first allocation
  /// triggers a page allocation since it will always span a "page" boundary.
  std::atomic<uintptr_t> NextValue;
  
public:
  constexpr MetadataAllocator() : NextValue(~(uintptr_t)0) {}

  // Don't copy or move, please.
  MetadataAllocator(const MetadataAllocator &) = delete;
  MetadataAllocator(MetadataAllocator &&) = delete;
  MetadataAllocator &operator=(const MetadataAllocator &) = delete;
  MetadataAllocator &operator=(MetadataAllocator &&) = delete;
  
  void *alloc(size_t size);
};

// A wrapper around a pointer to a metadata cache entry that provides
// DenseMap semantics that compare values in the key vector for the metadata
// instance.
//
// This is stored as a pointer to the arguments buffer, so that we can save
// an offset while looking for the matching argument given a key.
class KeyDataRef {
  const void * const *Args;
  unsigned Length;

  KeyDataRef(const void * const *args, unsigned length)
    : Args(args), Length(length) {}

public:
  template <class Entry>
  static KeyDataRef forEntry(const Entry *e, unsigned numArguments) {
    return KeyDataRef(e->getArgumentsBuffer(), numArguments);
  }

  static KeyDataRef forArguments(const void * const *args,
                                 unsigned numArguments) {
    return KeyDataRef(args, numArguments);
  }

  template <class Entry>
  const Entry *getEntry() const {
    return Entry::fromArgumentsBuffer(Args, Length);
  }

  bool operator==(KeyDataRef rhs) const {
    // Compare the sizes.
    unsigned asize = size(), bsize = rhs.size();
    if (asize != bsize) return false;

    // Compare the content.
    auto abegin = begin(), bbegin = rhs.begin();
    for (unsigned i = 0; i < asize; ++i)
      if (abegin[i] != bbegin[i]) return false;
    return true;
  }

  int compare(KeyDataRef rhs) const {
    // Compare the sizes.
    unsigned asize = size(), bsize = rhs.size();
    if (asize != bsize) {
      return (asize < bsize ? -1 : 1);
    }

    // Compare the content.
    auto abegin = begin(), bbegin = rhs.begin();
    for (unsigned i = 0; i < asize; ++i) {
      if (abegin[i] != bbegin[i])
        return (uintptr_t(abegin[i]) < uintptr_t(bbegin[i]) ? -1 : 1);
    }

    return 0;
  }

  size_t hash() {
    size_t H = 0x56ba80d1 * Length ;
    for (unsigned i = 0; i < Length; i++) {
      H = (H >> 10) | (H << ((sizeof(size_t) * 8) - 10));
      H ^= ((size_t)Args[i]) ^ ((size_t)Args[i] >> 19);
    }
    H *= 0x27d4eb2d;
    return (H >> 10) | (H << ((sizeof(size_t) * 8) - 10));
  }

  const void * const *begin() const { return Args; }
  const void * const *end() const { return Args + Length; }
  unsigned size() const { return Length; }
};

template <class Impl>
struct CacheEntryHeader {
  /// LLDB walks this list.
  /// FIXME: when LLDB stops walking this list, there will stop being
  /// any reason to store argument data in cache entries, and a *ton*
  /// of weird stuff here will go away.
  const Impl *Next;
};

/// A CRTP class for defining entries in a metadata cache.
template <class Impl, class Header = CacheEntryHeader<Impl> >
class alignas(void*) CacheEntry : public Header {

  CacheEntry(const CacheEntry &other) = delete;
  void operator=(const CacheEntry &other) = delete;

  Impl *asImpl() { return static_cast<Impl*>(this); }
  const Impl *asImpl() const { return static_cast<const Impl*>(this); }

protected:
  CacheEntry() = default;

public:
  static Impl *allocate(MetadataAllocator &allocator,
                        const void * const *arguments,
                        size_t numArguments, size_t payloadSize) {
    void *buffer = allocator.alloc(sizeof(Impl)  +
                                   numArguments * sizeof(void*) +
                                   payloadSize);
    void *resultPtr = (char*)buffer + numArguments * sizeof(void*);
    auto result = new (resultPtr) Impl(numArguments);

    // Copy the arguments into the right place for the key.
    memcpy(buffer, arguments,
           numArguments * sizeof(void*));

    return result;
  }

  void **getArgumentsBuffer() {
    return reinterpret_cast<void**>(this) - asImpl()->getNumArguments();
  }

  void * const *getArgumentsBuffer() const {
    return reinterpret_cast<void * const *>(this)
      - asImpl()->getNumArguments();
  }

  template <class T> T *getData() {
    return reinterpret_cast<T *>(asImpl() + 1);
  }

  template <class T> const T *getData() const {
    return const_cast<CacheEntry*>(this)->getData<T>();
  }

  static const Impl *fromArgumentsBuffer(const void * const *argsBuffer,
                                         unsigned numArguments) {
    return reinterpret_cast<const Impl *>(argsBuffer + numArguments);
  }
};

/// The implementation of a metadata cache.  Note that all-zero must
/// be a valid state for the cache.
template <class ValueTy> class MetadataCache {
  /// A key value as provided to the concurrent map.
  struct Key {
    size_t Hash;
    KeyDataRef KeyData;

    Key(KeyDataRef data) : Hash(data.hash()), KeyData(data) {}
  };

  /// The layout of an entry in the concurrent map.
  class Entry {
    size_t Hash;
    unsigned KeyLength;

    /// Does this entry have a value, or is it currently undergoing
    /// initialization?
    ///
    /// This (and the following field) is ever modified under the lock,
    /// but it can be read from any thread, including while the lock
    /// is held.
    std::atomic<bool> HasValue;
    union {
      ValueTy *Value;
      std::thread::id InitializingThread;
    };

    const void **getKeyDataBuffer() {
      return reinterpret_cast<const void **>(this + 1);
    }
    const void * const *getKeyDataBuffer() const {
      return reinterpret_cast<const void * const *>(this + 1);
    }
  public:
    Entry(const Key &key)
      : Hash(key.Hash), KeyLength(key.KeyData.size()), HasValue(false) {
      InitializingThread = std::this_thread::get_id();
      memcpy(getKeyDataBuffer(), key.KeyData.begin(),
             KeyLength * sizeof(void*));
    }

    bool isBeingInitializedByCurrentThread() const {
      return InitializingThread == std::this_thread::get_id();
    }

    KeyDataRef getKeyData() const {
      return KeyDataRef::forArguments(getKeyDataBuffer(), KeyLength);
    }

    long getKeyIntValueForDump() const {
      return Hash;
    }

    static size_t getExtraAllocationSize(const Key &key) {
      return key.KeyData.size() * sizeof(void*);
    }

    int compareWithKey(const Key &key) const {
      // Order by hash first, then by the actual key data.
      if (key.Hash != Hash) {
        return (key.Hash < Hash ? -1 : 1);
      } else {
        return key.KeyData.compare(getKeyData());
      }
    }

    ValueTy *getValue() const {
      if (HasValue.load(std::memory_order_acquire)) {
        return Value;
      }
      return nullptr;
    }

    void setValue(ValueTy *value) {
      Value = value;
      HasValue.store(true, std::memory_order_release);
    }
  };

  /// The concurrent map.
  ConcurrentMap<Entry> Map;

  static_assert(sizeof(Map) == 2 * sizeof(void*),
                "offset of Head is not at proper offset");

  /// The head of a linked list connecting all the metadata cache entries.
  /// TODO: Remove this when LLDB is able to understand the final data
  /// structure for the metadata cache.
  const ValueTy *Head;

  struct ConcurrencyControl {
    Mutex Lock;
    ConditionVariable Queue;
  };
  std::unique_ptr<ConcurrencyControl> Concurrency;

  /// Allocator for entries of this cache.
  MetadataAllocator Allocator;
  
public:
  MetadataCache() : Concurrency(new ConcurrencyControl()) {}
  ~MetadataCache() {}

  /// Caches are not copyable.
  MetadataCache(const MetadataCache &other) = delete;
  MetadataCache &operator=(const MetadataCache &other) = delete;

  /// Get the allocator for metadata in this cache.
  /// The allocator can only be safely used while the cache is locked during
  /// an addMetadataEntry call.
  MetadataAllocator &getAllocator() { return Allocator; }

  /// Look up a cached metadata entry. If a cache match exists, return it.
  /// Otherwise, call entryBuilder() and add that to the cache.
  const ValueTy *findOrAdd(const void * const *arguments, size_t numArguments,
                           llvm::function_ref<ValueTy *()> builder) {

#if SWIFT_DEBUG_RUNTIME
    printf("%s(%p): looking for entry with %zu arguments:\n",
           Entry::getName(), this, numArguments);
    for (size_t i = 0; i < numArguments; i++) {
      printf("%s(%p):     %p\n", ValueTy::getName(), this, arguments[i]);
    }
#endif

    Key key(KeyDataRef::forArguments(arguments, numArguments));

#if SWIFT_DEBUG_RUNTIME
    printf("%s(%p): generated hash %llx\n",
           ValueTy::getName(), this, key.Hash);
#endif

    // Ensure the existence of a map entry.
    auto insertResult = Map.getOrInsert(key);
    Entry *entry = insertResult.first;

    // If we didn't insert the entry, then we just need to get the
    // initialized value from the entry.
    if (!insertResult.second) {

      // If the entry is already initialized, great.
      auto value = entry->getValue();
      if (value) {
        return value;
      }

      // Otherwise, we have to grab the lock and wait for the value to
      // appear there.  Note that we have to check again immediately
      // after acquiring the lock to prevent a race.
      auto concurrency = Concurrency.get();
      concurrency->Lock.withLockOrWait(concurrency->Queue, [&, this] {
        if ((value = entry->getValue())) {
          return true; // found a value, done waiting
        }

        // As a QoI safe-guard against the simplest form of cyclic
        // dependency, check whether this thread is the one responsible
        // for initializing the metadata.
        if (entry->isBeingInitializedByCurrentThread()) {
          fprintf(stderr,
                  "%s(%p): cyclic metadata dependency detected, aborting\n",
                  ValueTy::getName(), (void*) this);
          abort();
        }

        return false; // don't have a value, continue waiting
      });

      return value;
    }

    // Otherwise, we created the entry and are responsible for
    // creating the metadata.
    auto value = builder();

    // Update the linked list.
    value->Next = Head;
    Head = value;

#if SWIFT_DEBUG_RUNTIME
        printf("%s(%p): created %p\n",
               ValueTy::getName(), (void*) this, value);
#endif

    // Acquire the lock, set the value, and notify any waiters.
    auto concurrency = Concurrency.get();
    concurrency->Lock.withLockThenNotifyAll(
        concurrency->Queue, [&entry, &value] { entry->setValue(value); });

    return value;
  }
};

} // namespace swift

#endif // SWIFT_RUNTIME_METADATACACHE_H
