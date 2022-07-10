//===----------------------------------------------------------------------===//
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

#import <SceneKit/SceneKit.h>

#include "swift/Runtime/Config.h"

SWIFT_CC(swift)
extern "C" NS_RETURNS_RETAINED _Nullable id
SCN_Swift_SCNSceneSource_entryWithIdentifier(
    id NS_RELEASES_ARGUMENT _Nonnull self_,
    NSString *NS_RELEASES_ARGUMENT _Nonnull uid, Class _Nonnull entryClass) {
  SCNSceneSource *sceneSource = self_;
  id Result = [[sceneSource entryWithIdentifier:uid withClass:entryClass] retain];
  [self_ release];
  [uid release];
  return Result;
}

