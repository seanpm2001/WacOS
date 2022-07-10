//===--- FoundationHelpers.mm - Cocoa framework helper shims --------------===//
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
// This file contains shims to refer to framework functions required by the
// standard library. The stdlib cannot directly import these modules without
// introducing circular dependencies.
//
//===----------------------------------------------------------------------===//

#import <CoreFoundation/CoreFoundation.h>
#include "../SwiftShims/CoreFoundationShims.h"

using namespace swift;

template <class FromTy> struct DestType;

#define BRIDGE_TYPE(FROM, TO) \
template <> struct DestType<FROM> { using type = TO; }

BRIDGE_TYPE(_swift_shims_CFAllocatorRef, CFAllocatorRef);
BRIDGE_TYPE(_swift_shims_CFStringRef, CFStringRef);
BRIDGE_TYPE(_swift_shims_UniChar *, UniChar *);
BRIDGE_TYPE(_swift_shims_CFStringEncoding, CFStringEncoding);
BRIDGE_TYPE(_swift_shims_CFStringCompareFlags, CFStringCompareFlags);
BRIDGE_TYPE(_swift_shims_CFRange *, CFRange *);
BRIDGE_TYPE(CFComparisonResult, _swift_shims_CFComparisonResult);
BRIDGE_TYPE(CFStringRef, _swift_shims_CFStringRef);

template <class FromTy>
static typename DestType<FromTy>::type cast(FromTy value) {
  return (typename DestType<FromTy>::type) value;
}

static CFRange cast(_swift_shims_CFRange value) {
  return { value.location, value.length };
}

void swift::_swift_stdlib_CFStringGetCharacters(
                                         _swift_shims_CFStringRef theString,
                                         _swift_shims_CFRange range,
                                         _swift_shims_UniChar *buffer) {
  return CFStringGetCharacters(cast(theString), cast(range), cast(buffer));
}

const _swift_shims_UniChar *
swift::_swift_stdlib_CFStringGetCharactersPtr(
                                         _swift_shims_CFStringRef theString) {
  return CFStringGetCharactersPtr(cast(theString));
}

_swift_shims_CFIndex
swift::_swift_stdlib_CFStringGetLength(_swift_shims_CFStringRef theString) {
  return CFStringGetLength(cast(theString));
}

_swift_shims_CFStringRef
swift::_swift_stdlib_CFStringCreateWithSubstring(
                                         _swift_shims_CFAllocatorRef alloc,
                                         _swift_shims_CFStringRef str,
                                         _swift_shims_CFRange range) {
  return cast(CFStringCreateWithSubstring(cast(alloc), cast(str), cast(range)));
}

_swift_shims_UniChar
swift::_swift_stdlib_CFStringGetCharacterAtIndex(_swift_shims_CFStringRef theString,
                                                 _swift_shims_CFIndex idx) {
  return CFStringGetCharacterAtIndex(cast(theString), idx);
}

_swift_shims_CFStringRef
swift::_swift_stdlib_CFStringCreateCopy(_swift_shims_CFAllocatorRef alloc,
                                        _swift_shims_CFStringRef theString) {
  return cast(CFStringCreateCopy(cast(alloc), cast(theString)));
}

const char *
swift::_swift_stdlib_CFStringGetCStringPtr(_swift_shims_CFStringRef theString,
                            _swift_shims_CFStringEncoding encoding) {
  return CFStringGetCStringPtr(cast(theString), cast(encoding));
}

_swift_shims_CFComparisonResult
swift::_swift_stdlib_CFStringCompare(_swift_shims_CFStringRef theString1,
                                     _swift_shims_CFStringRef theString2,
                            _swift_shims_CFStringCompareFlags compareOptions) {
  return cast(CFStringCompare(cast(theString1), cast(theString2),
                              cast(compareOptions)));
}

_swift_shims_Boolean
swift::_swift_stdlib_CFStringFindWithOptions(
                                      _swift_shims_CFStringRef theString,
                                      _swift_shims_CFStringRef stringToFind,
                                      _swift_shims_CFRange rangeToSearch,
                                      _swift_shims_CFStringCompareFlags searchOptions,
                                      _swift_shims_CFRange *result) {
  return CFStringFindWithOptions(cast(theString), cast(stringToFind),
                                 cast(rangeToSearch), cast(searchOptions),
                                 cast(result));
}

_swift_shims_CFStringRef
swift::_swift_stdlib_objcDebugDescription(id __nonnull nsObject) {
  return [nsObject debugDescription];
}
