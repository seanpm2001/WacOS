//===--- Timer.h - Shared timers for compilation phases ---------*- C++ -*-===//
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

#ifndef SWIFT_BASIC_TIMER_H
#define SWIFT_BASIC_TIMER_H

#include "swift/Basic/LLVM.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/Timer.h"

namespace swift {
  /// A convenience class for declaring a timer that's part of the Swift
  /// compilation timers group.
  class SharedTimer {
    enum class State {
      Initial,
      Skipped,
      Enabled
    };
    static State CompilationTimersEnabled;

    Optional<llvm::NamedRegionTimer> Timer;

  public:
    explicit SharedTimer(StringRef name) {
      if (CompilationTimersEnabled == State::Enabled)
        Timer.emplace(name, StringRef("Swift compilation"));
      else
        CompilationTimersEnabled = State::Skipped;
    }

    /// Must be called before any SharedTimers have been created.
    static void enableCompilationTimers() {
      assert(CompilationTimersEnabled != State::Skipped &&
             "a timer has already been created");
      CompilationTimersEnabled = State::Enabled;
    }
  };
} // end namespace swift

#endif // SWIFT_BASIC_TIMER_H
