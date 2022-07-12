//===--- Cleanup.h - Declarations for SIL Cleanup Generation ----*- C++ -*-===//
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
//
// This file defines the Cleanup and CleanupManager classes.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SILGEN_CLEANUP_H
#define SWIFT_SILGEN_CLEANUP_H

#include "swift/Basic/DiverseStack.h"
#include "swift/SIL/SILLocation.h"
#include "llvm/ADT/SmallVector.h"

namespace swift {

class SILBasicBlock;
class SILFunction;
class SILValue;

namespace Lowering {

class RValue;
class JumpDest;
class SILGenFunction;
class SILGenBuilder;
class ManagedValue;
class SharedBorrowFormalAccess;
class FormalEvaluationScope;

/// The valid states that a cleanup can be in.
enum class CleanupState {
  /// The cleanup is inactive but may be activated later.
  Dormant,
  
  /// The cleanup is inactive and will not be activated later.
  Dead,

  // Only active states after this point

  /// The cleanup is currently active.
  Active,
  
  /// The cleanup is currently active.  When it's forwarded, it should
  /// be placed in a dormant state, not a dead state.
  PersistentlyActive
};

llvm::raw_ostream &operator<<(raw_ostream &os, CleanupState state);

class LLVM_LIBRARY_VISIBILITY Cleanup {
  friend class CleanupManager;

  unsigned allocatedSize;

protected:
  CleanupState state;

  Cleanup() {}
  virtual ~Cleanup() {}

public:
  /// Return the allocated size of this object.  This is required by
  /// DiverseStack for iteration.
  size_t allocated_size() const { return allocatedSize; }
  
  CleanupState getState() const { return state; }
  virtual void setState(SILGenFunction &SGF, CleanupState newState) {
    state = newState;
  }
  bool isActive() const { return state >= CleanupState::Active; }
  bool isDead() const { return state == CleanupState::Dead; }

  virtual void emit(SILGenFunction &SGF, CleanupLocation loc) = 0;
  virtual void dump(SILGenFunction &SGF) const = 0;
};

/// A cleanup depth is generally used to denote the set of cleanups
/// between the given cleanup (and not including it) and the top of
/// the stack.
///
/// Cleanup depths can be the stack's stable_end(), but generally
/// cannot be invalid.
typedef DiverseStackImpl<Cleanup>::stable_iterator CleanupsDepth;

/// A cleanup handle is a stable pointer to a single cleanup.
///
/// Cleanup handles can be invalid() (if no cleanup was required), but
/// generally cannot be the stack's stable_end().
typedef DiverseStackImpl<Cleanup>::stable_iterator CleanupHandle;

class LLVM_LIBRARY_VISIBILITY CleanupManager {
  friend class Scope;

  SILGenFunction &SGF;

  /// Stack - Currently active cleanups in this scope tree.
  DiverseStack<Cleanup, 128> stack;

  /// The shallowest depth held by an active Scope object.
  ///
  /// Generally, the rule is that a CleanupHandle is invalidated as
  /// soon as the underlying cleanup is marked dead, meaning that
  /// further uses of that handle are free to misbehave, and therefore
  /// that we're free to actually pop the cleanup.  But doing so might
  /// break any outstanding CleanupsDepths we've vended, of which we
  /// only really care about those held by the Scope RAII objects.  So
  /// we can only reap the cleanup stack up to the innermost depth
  /// that we've handed out as a Scope.
  CleanupsDepth innermostScope;

  void popTopDeadCleanups(CleanupsDepth end);
  void emitCleanups(CleanupsDepth depth, CleanupLocation l,
                    bool popCleanups=true);
  void endScope(CleanupsDepth depth, CleanupLocation l);

  Cleanup &initCleanup(Cleanup &cleanup, size_t allocSize, CleanupState state);
  void setCleanupState(Cleanup &cleanup, CleanupState state);

  friend class CleanupStateRestorationScope;
  friend class SharedBorrowFormalEvaluation;
  friend class FormalEvaluationScope;

public:
  CleanupManager(SILGenFunction &SGF)
      : SGF(SGF), innermostScope(stack.stable_end()) {}

  /// Return a stable reference to the last cleanup pushed.
  CleanupsDepth getCleanupsDepth() const { return stack.stable_begin(); }

  /// Return a stable reference to the last cleanup pushed.
  CleanupHandle getTopCleanup() const {
    assert(!stack.empty());
    return stack.stable_begin();
  }

  /// \brief Emit a branch to the given jump destination,
  /// threading out through any cleanups we need to run. This does not pop the
  /// cleanup stack.
  ///
  /// \param dest       The destination scope and block.
  /// \param branchLoc  The location of the branch instruction.
  /// \param args       Arguments to pass to the destination block.
  void emitBranchAndCleanups(JumpDest dest, SILLocation branchLoc,
                             ArrayRef<SILValue> args = {});

  /// emitCleanupsForReturn - Emit the top-level cleanups needed prior to a
  /// return from the function.
  void emitCleanupsForReturn(CleanupLocation loc);

  /// Emit a new block that jumps to the specified location and runs necessary
  /// cleanups based on its level.  If there are no cleanups to run, this just
  /// returns the dest block.
  SILBasicBlock *emitBlockForCleanups(JumpDest dest, SILLocation branchLoc,
                                      ArrayRef<SILValue> args = {});

  /// pushCleanup - Push a new cleanup.
  template<class T, class... A>
  T &pushCleanupInState(CleanupState state,
                        A &&... args) {
    assert(state != CleanupState::Dead);

#ifndef NDEBUG
    CleanupsDepth oldTop = stack.stable_begin();
#endif

    T &cleanup = stack.push<T, A...>(::std::forward<A>(args)...);
    T &result = static_cast<T&>(initCleanup(cleanup, sizeof(T), state));
    
#ifndef NDEBUG
    auto newTop = stack.begin();
    ++newTop;
    assert(newTop == stack.find(oldTop));
#endif
    return result;
  }
  template<class T, class... A>
  T &pushCleanup(A &&... args) {
    return pushCleanupInState<T, A...>(CleanupState::Active,
                                       ::std::forward<A>(args)...);
  }

  /// Transition the given active cleanup to the corresponding
  /// inactive state: Active becomes Dead and PersistentlyActive
  /// becomes Dormant.
  void forwardCleanup(CleanupHandle depth);

  /// Set the state of the cleanup at the given depth.
  /// The transition must be non-trivial and legal.
  void setCleanupState(CleanupHandle depth, CleanupState state);
  
  /// True if there are any active cleanups in the scope between the two
  /// cleanup handles.
  bool hasAnyActiveCleanups(CleanupsDepth from, CleanupsDepth to);

  /// True if there are any active cleanups in the scope between the specified
  /// cleanup handle and the current top of stack.
  bool hasAnyActiveCleanups(CleanupsDepth from);

  /// Dump the output of each cleanup on this stack.
  void dump() const;

  /// Dump the given cleanup handle if it is on the current stack.
  void dump(CleanupHandle handle) const;

  /// Verify that the given cleanup handle is valid.
  void checkIterator(CleanupHandle handle) const;
};

/// An RAII object that allows the state of a cleanup to be
/// temporarily modified.
class CleanupStateRestorationScope {
  CleanupManager &cleanups;
  SmallVector<std::pair<CleanupHandle, CleanupState>, 4> savedStates;

  CleanupStateRestorationScope(const CleanupStateRestorationScope &) = delete;
  CleanupStateRestorationScope &
    operator=(const CleanupStateRestorationScope &) = delete;
public:
  CleanupStateRestorationScope(CleanupManager &cleanups) : cleanups(cleanups) {}

  /// Set the state of the given cleanup and remember what we set it to.
  void pushCleanupState(CleanupHandle handle, CleanupState newState);

  /// Just remember whatever the current state of the given cleanup is.
  void pushCurrentCleanupState(CleanupHandle handle);

  void pop() &&;

  ~CleanupStateRestorationScope() { popImpl(); }

private:
  void popImpl();
};

class CleanupCloner {
  SILGenFunction &SGF;
  bool hasCleanup;
  bool isLValue;

public:
  CleanupCloner(SILGenFunction &SGF, const ManagedValue &mv);
  CleanupCloner(SILGenBuilder &builder, const ManagedValue &mv);
  CleanupCloner(SILGenFunction &SGF, const RValue &rv);
  CleanupCloner(SILGenBuilder &builder, const RValue &rv);

  ManagedValue clone(SILValue value) const;
};

} // end namespace Lowering
} // end namespace swift

#endif

