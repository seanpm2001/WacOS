//===--- Leaks.h ------------------------------------------------*- C++ -*-===//
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
// This is a very simple leak detector implementation that detects objects that
// are allocated but not deallocated in a region. It is purposefully behind a
// flag since it is not meant to be used in production yet.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_STDLIB_RUNTIME_LEAKS_H
#define SWIFT_STDLIB_RUNTIME_LEAKS_H

#if SWIFT_RUNTIME_ENABLE_LEAK_CHECKER

#include "../SwiftShims/Visibility.h"

namespace swift {
struct HeapObject;
}

SWIFT_RUNTIME_EXPORT
extern "C" void swift_leaks_startTrackingObjects(const char *)
    __attribute__((__noinline__, __used__));
SWIFT_RUNTIME_EXPORT
extern "C" int swift_leaks_stopTrackingObjects(const char *)
    __attribute__((__noinline__, __used__));
SWIFT_RUNTIME_EXPORT
extern "C" void swift_leaks_startTrackingObject(swift::HeapObject *)
    __attribute__((__noinline__, __used__));
SWIFT_RUNTIME_EXPORT
extern "C" void swift_leaks_stopTrackingObject(swift::HeapObject *)
    __attribute__((__noinline__, __used__));

#define SWIFT_LEAKS_START_TRACKING_OBJECT(obj)                                 \
  swift_leaks_startTrackingObject(obj)
#define SWIFT_LEAKS_STOP_TRACKING_OBJECT(obj)                                  \
  swift_leaks_stopTrackingObject(obj)
#else
#define SWIFT_LEAKS_START_TRACKING_OBJECT(obj)
#define SWIFT_LEAKS_STOP_TRACKING_OBJECT(obj)
#endif

#endif
