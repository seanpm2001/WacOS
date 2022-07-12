//===--- OwnershipChecker.h -------------------------------------*- C++ -*-===//
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

#ifndef SWIFT_SIL_OWNERSHIPCHECKER_H
#define SWIFT_SIL_OWNERSHIPCHECKER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace swift {

class SILBasicBlock;
class SILInstruction;
class SILModule;
class SILValue;
class DeadEndBlocks;

/// This class is a higher level interface to the ownership checker meant for
/// use with SILPasses. It uses the actual checker as an internal PImpl detail
/// so types/etc do not leak.
struct OwnershipChecker {
  /// The module that we are in.
  SILModule &Mod;

  /// A cache of dead-end basic blocks that we use to determine if we can
  /// ignore "leaks".
  DeadEndBlocks &DEBlocks;

  /// The list of regular users from the last run of the checker.
  llvm::SmallVector<SILInstruction *, 16> RegularUsers;

  /// The list of regular users from the last run of the checker.
  llvm::SmallVector<SILInstruction *, 16> LifetimeEndingUsers;

  /// The live blocks for the SILValue we processed. This can be used to
  /// determine if a block is in the "live" region of our SILInstruction.
  llvm::SmallPtrSet<SILBasicBlock *, 32> LiveBlocks;

  bool checkValue(SILValue Value);
};

} // end swift namespace

#endif
