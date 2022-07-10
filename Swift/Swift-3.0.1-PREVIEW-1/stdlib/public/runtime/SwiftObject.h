//===--- SwiftObject.h - Native Swift Object root class ---------*- C++ -*-===//
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
// This implements the Objective-C root class that provides basic `id`-
// compatibility and `NSObject` protocol conformance for pure Swift classes.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_RUNTIME_SWIFTOBJECT_H
#define SWIFT_RUNTIME_SWIFTOBJECT_H

#include "swift/Runtime/Config.h"
#include <cstdint>
#include <utility>
#include "swift/Runtime/HeapObject.h"
#if SWIFT_OBJC_INTEROP
#include <objc/NSObject.h>
#endif

namespace swift {

#if SWIFT_OBJC_INTEROP
struct SwiftObject_s {
  void *isa __attribute__((__unavailable__));
  uint32_t strongRefCount __attribute__((__unavailable__));
  uint32_t weakRefCount __attribute__((__unavailable__));
};

static_assert(sizeof(SwiftObject_s) == sizeof(HeapObject),
              "SwiftObject and HeapObject must have the same header");
static_assert(std::is_trivially_constructible<SwiftObject_s>::value,
              "SwiftObject must be trivially constructible");
static_assert(std::is_trivially_destructible<SwiftObject_s>::value,
              "SwiftObject must be trivially destructible");

} // namespace swift

#if __has_attribute(objc_root_class)
__attribute__((__objc_root_class__))
#endif
SWIFT_RUNTIME_EXPORT @interface SwiftObject<NSObject> {
  swift::SwiftObject_s header;
}

- (BOOL)isEqual:(id)object;
- (NSUInteger)hash;

- (Class)superclass;
- (Class)class;
- (instancetype)self;
- (struct _NSZone *)zone;

- (id)performSelector:(SEL)aSelector;
- (id)performSelector:(SEL)aSelector withObject:(id)object;
- (id)performSelector:(SEL)aSelector withObject:(id)object1 withObject:(id)object2;

- (BOOL)isProxy;

+ (BOOL)isSubclassOfClass:(Class)aClass;
- (BOOL)isKindOfClass:(Class)aClass;
- (BOOL)isMemberOfClass:(Class)aClass;
- (BOOL)conformsToProtocol:(Protocol *)aProtocol;

- (BOOL)respondsToSelector:(SEL)aSelector;

- (instancetype)retain;
- (oneway void)release;
- (instancetype)autorelease;
- (NSUInteger)retainCount;

- (NSString *)description;
- (NSString *)debugDescription;
@end

namespace swift {

struct String { void *x, *y, *z; };

/// Helper from the standard library for stringizing an arbitrary object.
extern "C" SWIFT_CC(swift)
void swift_getSummary(String *out, OpaqueValue *value, const Metadata *T);

// Convert a Swift String to an NSString.
NSString *convertStringToNSString(String *swiftString);

#endif
  
}

#endif
