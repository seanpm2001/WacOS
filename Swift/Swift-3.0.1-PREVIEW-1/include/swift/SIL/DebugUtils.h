//===--- DebugUtils.h - Utilities for debug-info instructions ---*- C++ -*-===//
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
// This file contains utilities to work with debug-info related instructions:
// debug_value and debug_value_addr.
//
// SIL optimizations should deal with debug-info related instructions when
// looking at the uses of a value.
// When performing an analysis, the usual thing is to just ignore all debug-info
// instructions.
// When transforming the SIL, a pass must decide what to do with debug-info
// instructions. Either delete them (if their value is no longer available),
// keep them (if the transformation has no effect on debug-info values) or
// update them.
//
// To ignore debug-info instructions during an analysis, this file provides
// some utility functions, which can be used instead of the relevant member
// functions in ValueBase and SILValue:
//
// V->use_empty()        ->  onlyHaveDebugUses(V)
// V.hasOneUse()         ->  hasOneNonDebugUse(V)
// V.getUses()           ->  getNonDebugUses(V)
// I->eraseFromParent()  ->  eraseFromParentWithDebugInsts(I)
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_DEBUGUTILS_H
#define SWIFT_SIL_DEBUGUTILS_H

#include "swift/SIL/SILValue.h"

namespace swift {

class SILInstruction;
  
/// Returns true if the instruction \p Inst is an instruction which is only
/// relevant for debug information and has no other impact on program semantics.
inline bool isDebugInst(SILInstruction *Inst) {
  return isa<DebugValueInst>(Inst) || isa<DebugValueAddrInst>(Inst);
}

/// Deletes all of the debug instructions that use \p Inst.
inline void deleteAllDebugUses(ValueBase *Inst) {
  for (auto UI = Inst->use_begin(), E = Inst->use_end(); UI != E;) {
    auto *Inst = UI->getUser();
    UI++;
    if (isDebugInst(Inst))
      Inst->eraseFromParent();
  }
}

/// This iterator filters out any debug (or non-debug) instructions from a range
/// of uses, provided by the underlying ValueBaseUseIterator.
/// If \p nonDebugInsts is true, then the iterator provides a view to all non-
/// debug instructions. Otherwise it provides a view ot all debug-instructions.
template <bool nonDebugInsts> class DebugUseIterator
: public std::iterator<std::forward_iterator_tag, Operand *, ptrdiff_t> {
  
  ValueBaseUseIterator BaseIterator;
  
  // Skip any debug or non-debug instructions (depending on the nonDebugInsts
  // template argument).
  void skipInsts() {
    while (true) {
      if (*BaseIterator == nullptr)
        return;
      
      SILInstruction *User = BaseIterator->getUser();
      if (isDebugInst(User) != nonDebugInsts)
        return;
      
      BaseIterator++;
    }
  }
  
public:
  
  DebugUseIterator(ValueBaseUseIterator BaseIterator) :
      BaseIterator(BaseIterator) {
    skipInsts();
  }
  
  DebugUseIterator() = default;
  
  Operand *operator*() const { return *BaseIterator; }
  Operand *operator->() const { return *BaseIterator; }
  SILInstruction *getUser() const { return BaseIterator.getUser(); }
  
  DebugUseIterator &operator++() {
    BaseIterator++;
    skipInsts();
    return *this;
  }
  
  DebugUseIterator operator++(int unused) {
    DebugUseIterator Copy = *this;
    ++*this;
    return Copy;
  }
  friend bool operator==(DebugUseIterator lhs,
                         DebugUseIterator rhs) {
    return lhs.BaseIterator == rhs.BaseIterator;
  }
  friend bool operator!=(DebugUseIterator lhs,
                         DebugUseIterator rhs) {
    return !(lhs == rhs);
  }
};

/// Iterator for iteration over debug instructions.
using DUIterator = DebugUseIterator<false>;

/// Iterator for iteration over non-debug instructions.
using NonDUIterator = DebugUseIterator<true>;


/// Returns a range of all debug instructions in the uses of a value (e.g.
/// SILValue or SILInstruction).
inline iterator_range<DUIterator> getDebugUses(SILValue V) {
  return make_range(DUIterator(V->use_begin()), DUIterator(V->use_end()));
}

/// Returns a range of all non-debug instructions in the uses of a value (e.g.
/// SILValue or SILInstruction).
inline iterator_range<NonDUIterator> getNonDebugUses(SILValue V) {
  return make_range(NonDUIterator(V->use_begin()), NonDUIterator(V->use_end()));
}

/// Returns true if a value (e.g. SILInstruction) has no uses except debug
/// instructions.
inline bool onlyHaveDebugUses(SILValue V) {
  auto NonDebugUses = getNonDebugUses(V);
  return NonDebugUses.begin() == NonDebugUses.end();
}

/// Returns true if a value (e.g. SILInstruction) has exactly one use which is
/// not a debug instruction.
inline bool hasOneNonDebugUse(SILValue V) {
  auto Range = getNonDebugUses(V);
  auto I = Range.begin(), E = Range.end();
  if (I == E) return false;
  return ++I == E;
}

// Returns the use if the value has only one non debug user.
inline SILInstruction *getSingleNonDebugUser(SILValue V) {
  auto Range = getNonDebugUses(V);
  auto I = Range.begin(), E = Range.end();
  if (I == E) return nullptr;
  if (std::next(I) != E)
    return nullptr;
  return I->getUser();
}

/// Erases the instruction \p I from it's parent block and deletes it, including
/// all debug instructions which use \p I.
/// Precondition: The instruction may only have debug instructions as uses.
/// If the iterator \p InstIter references any deleted debug instruction, it is
/// incremented.
inline void eraseFromParentWithDebugInsts(SILInstruction *I,
                                          SILBasicBlock::iterator &InstIter) {
  while (!I->use_empty()) {
    auto *User = I->use_begin()->getUser();
    assert(isDebugInst(User));
    if (InstIter != SILBasicBlock::iterator() &&
        InstIter != I->getParent()->end() &&
        &*InstIter == User) {
      InstIter++;
    }
    User->eraseFromParent();
  }
  I->eraseFromParent();
}

/// Erases the instruction \p I from it's parent block and deletes it, including
/// all debug instructions which use \p I.
/// Precondition: The instruction may only have debug instructions as uses.
inline void eraseFromParentWithDebugInsts(SILInstruction *I) {
  SILBasicBlock::iterator nullIter;
  eraseFromParentWithDebugInsts(I, nullIter);
}

} // end namespace swift

#endif /* SWIFT_SIL_DEBUGUTILS_H */
