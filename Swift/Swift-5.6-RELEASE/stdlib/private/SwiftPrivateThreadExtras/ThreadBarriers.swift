//===--- ThreadBarriers.swift ---------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#if canImport(Darwin)
import Darwin
#elseif canImport(Glibc)
import Glibc
#elseif os(Windows)
import CRT
import WinSDK
#endif

//
// Implement pthread barriers.
//
// (OS X does not implement them.)
//

public var _stdlib_THREAD_BARRIER_SERIAL_THREAD: CInt {
  return 1
}

public struct _stdlib_thread_barrier_t {
#if os(Windows)
  var mutex: UnsafeMutablePointer<SRWLOCK>?
  var cond: UnsafeMutablePointer<CONDITION_VARIABLE>?
#elseif os(Cygwin) || os(FreeBSD) || os(OpenBSD)
  var mutex: UnsafeMutablePointer<pthread_mutex_t?>?
  var cond: UnsafeMutablePointer<pthread_cond_t?>?
#else
  var mutex: UnsafeMutablePointer<pthread_mutex_t>?
  var cond: UnsafeMutablePointer<pthread_cond_t>?
#endif

  /// The number of threads to synchronize.
  var count: CUnsignedInt = 0

  /// The number of threads already waiting on the barrier.
  ///
  /// This shared variable is protected by `mutex`.
  var numThreadsWaiting: CUnsignedInt = 0

  public init() {}
}

public func _stdlib_thread_barrier_init(
  _ barrier: UnsafeMutablePointer<_stdlib_thread_barrier_t>,
  _ count: CUnsignedInt
) -> CInt {
  barrier.pointee = _stdlib_thread_barrier_t()
  if count == 0 {
    errno = EINVAL
    return -1
  }
#if os(Windows)
  barrier.pointee.mutex = UnsafeMutablePointer.allocate(capacity: 1)
  InitializeSRWLock(barrier.pointee.mutex!)

  barrier.pointee.cond = UnsafeMutablePointer.allocate(capacity: 1)
  InitializeConditionVariable(barrier.pointee.cond!)
#else
  barrier.pointee.mutex = UnsafeMutablePointer.allocate(capacity: 1)
  barrier.pointee.cond = UnsafeMutablePointer.allocate(capacity: 1)
  guard _stdlib_thread_barrier_mutex_and_cond_init(barrier) == 0 else {
    barrier.pointee.mutex!.deinitialize(count: 1)
    barrier.pointee.mutex!.deallocate()
    barrier.pointee.cond!.deinitialize(count: 1)
    barrier.pointee.cond!.deallocate()
    return -1
  }
#endif
  barrier.pointee.count = count
  return 0
}

#if !os(Windows)
private func _stdlib_thread_barrier_mutex_and_cond_init(_ barrier: UnsafeMutablePointer<_stdlib_thread_barrier_t>) -> CInt {
  guard pthread_mutex_init(barrier.pointee.mutex!, nil) == 0 else {
    return -1
  }
  guard pthread_cond_init(barrier.pointee.cond!, nil) == 0 else {
    pthread_mutex_destroy(barrier.pointee.mutex!)
    return -1
  }
  return 0
}
#endif

public func _stdlib_thread_barrier_destroy(
  _ barrier: UnsafeMutablePointer<_stdlib_thread_barrier_t>
) {
#if os(Windows)
  // Condition Variables do not need to be explicitly destroyed
  // Mutexes do not need to be explicitly destroyed
#else
  guard pthread_cond_destroy(barrier.pointee.cond!) == 0 &&
    pthread_mutex_destroy(barrier.pointee.mutex!) == 0 else {
    fatalError("_stdlib_thread_barrier_destroy() failed")
  }
#endif
  barrier.pointee.cond!.deinitialize(count: 1)
  barrier.pointee.cond!.deallocate()

  barrier.pointee.mutex!.deinitialize(count: 1)
  barrier.pointee.mutex!.deallocate()

  return
}

public func _stdlib_thread_barrier_wait(
  _ barrier: UnsafeMutablePointer<_stdlib_thread_barrier_t>
) -> CInt {
#if os(Windows)
  AcquireSRWLockExclusive(barrier.pointee.mutex!)
#else
  if pthread_mutex_lock(barrier.pointee.mutex!) != 0 {
    return -1
  }
#endif
  barrier.pointee.numThreadsWaiting += 1
  if barrier.pointee.numThreadsWaiting < barrier.pointee.count {
    // Put the thread to sleep.
#if os(Windows)
    if !SleepConditionVariableSRW(barrier.pointee.cond!, barrier.pointee.mutex!,
                                  INFINITE, 0) {
      return -1
    }
    ReleaseSRWLockExclusive(barrier.pointee.mutex!)
#else
    if pthread_cond_wait(barrier.pointee.cond!, barrier.pointee.mutex!) != 0 {
      return -1
    }
    if pthread_mutex_unlock(barrier.pointee.mutex!) != 0 {
      return -1
    }
#endif
    return 0
  } else {
    // Reset thread count.
    barrier.pointee.numThreadsWaiting = 0

    // Wake up all threads.
#if os(Windows)
    WakeAllConditionVariable(barrier.pointee.cond!)
    ReleaseSRWLockExclusive(barrier.pointee.mutex!)
#else
    if pthread_cond_broadcast(barrier.pointee.cond!) != 0 {
      return -1
    }
    if pthread_mutex_unlock(barrier.pointee.mutex!) != 0 {
      return -1
    }
#endif
    return _stdlib_THREAD_BARRIER_SERIAL_THREAD
  }
}
