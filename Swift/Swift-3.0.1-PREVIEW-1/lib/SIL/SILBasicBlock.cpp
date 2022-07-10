//===--- SILBasicBlock.cpp - Basic blocks for high-level SIL code ---------===//
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
// This file defines the high-level BasicBlocks used for Swift SIL code.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "swift/SIL/SILBasicBlock.h"
#include "swift/SIL/SILBuilder.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/SILDebugScope.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILModule.h"
using namespace swift;

//===----------------------------------------------------------------------===//
// SILBasicBlock Implementation
//===----------------------------------------------------------------------===//

SILBasicBlock::SILBasicBlock(SILFunction *parent, SILBasicBlock *afterBB)
  : Parent(parent), PredList(0) {
  if (afterBB) {
    parent->getBlocks().insertAfter(afterBB->getIterator(), this);
  } else {
    parent->getBlocks().push_back(this);
  }
}
SILBasicBlock::~SILBasicBlock() {
  // Invalidate all of the basic block arguments.
  for (auto *Arg : BBArgList) {
    getModule().notifyDeleteHandlers(Arg);
  }

  dropAllReferences();

  // Notify the delete handlers that the instructions in this block are
  // being deleted.
  auto &M = getModule();
  for (auto I = begin(), E = end(); I != E;) {
    auto Inst = &*I;
    ++I;
    M.notifyDeleteHandlers(Inst);
    erase(Inst);
  }

  // iplist's destructor is going to destroy the InstList.
  InstList.clearAndLeakNodesUnsafely();
}

int SILBasicBlock::getDebugID() {
  if (!getParent())
    return -1;
  int idx = 0;
  for (const SILBasicBlock &B : *getParent()) {
    if (&B == this)
      return idx;
    idx++;
  }
  llvm_unreachable("block not in function's block list");
}

SILModule &SILBasicBlock::getModule() const {
  return getParent()->getModule();
}

void SILBasicBlock::insert(iterator InsertPt, SILInstruction *I) {
  InstList.insert(InsertPt, I);
}

void SILBasicBlock::push_back(SILInstruction *I) {
  InstList.push_back(I);
}

void SILBasicBlock::push_front(SILInstruction *I) {
  InstList.push_front(I);
}

void SILBasicBlock::remove(SILInstruction *I) {
  InstList.remove(I);
}

void SILBasicBlock::erase(SILInstruction *I) {
  // Notify the delete handlers that this instruction is going away.
  getModule().notifyDeleteHandlers(&*I);
  auto *F = getParent();
  InstList.erase(I);
  F->getModule().deallocateInst(I);
}

/// This method unlinks 'self' from the containing SILFunction and deletes it.
void SILBasicBlock::eraseFromParent() {
  getParent()->getBlocks().erase(this);
}

/// Replace the ith BB argument with a new one with type Ty (and optional
/// ValueDecl D).
SILArgument *SILBasicBlock::replaceBBArg(unsigned i, SILType Ty,
                                         const ValueDecl *D) {
  SILModule &M = getParent()->getModule();


  assert(BBArgList[i]->use_empty() && "Expected no uses of the old BB arg!");

  // Notify the delete handlers that this argument is being deleted.
  M.notifyDeleteHandlers(BBArgList[i]);

  auto *NewArg = new (M) SILArgument(Ty, D);
  NewArg->setParent(this);

  // TODO: When we switch to malloc/free allocation we'll be leaking memory
  // here.
  BBArgList[i] = NewArg;

  return NewArg;
}

SILArgument *SILBasicBlock::createBBArg(SILType Ty, const ValueDecl *D) {
  return new (getModule()) SILArgument(this, Ty, D);
}

SILArgument *SILBasicBlock::insertBBArg(bbarg_iterator Iter, SILType Ty,
                                        const ValueDecl *D) {
  return new (getModule()) SILArgument(this, Iter, Ty, D);
}

void SILBasicBlock::eraseBBArg(int Index) {
  assert(getBBArg(Index)->use_empty() &&
         "Erasing block argument that has uses!");
  // Notify the delete handlers that this BB argument is going away.
  getModule().notifyDeleteHandlers(getBBArg(Index));
  BBArgList.erase(BBArgList.begin() + Index);
}

/// \brief Splits a basic block into two at the specified instruction.
///
/// Note that all the instructions BEFORE the specified iterator
/// stay as part of the original basic block. The old basic block is left
/// without a terminator.
SILBasicBlock *SILBasicBlock::splitBasicBlock(iterator I) {
  SILBasicBlock *New = new (Parent->getModule()) SILBasicBlock(Parent);
  SILFunction::iterator Where = std::next(SILFunction::iterator(this));
  SILFunction::iterator First = SILFunction::iterator(New);
  if (Where != First)
    Parent->getBlocks().splice(Where, Parent->getBlocks(), First);
  // Move all of the specified instructions from the original basic block into
  // the new basic block.
  New->InstList.splice(New->end(), InstList, I, end());
  return New;
}

/// \brief Move the basic block to after the specified basic block in the IR.
void SILBasicBlock::moveAfter(SILBasicBlock *After) {
  assert(getParent() && getParent() == After->getParent() &&
         "Blocks must be in the same function");
  auto InsertPt = std::next(SILFunction::iterator(After));
  auto &BlkList = getParent()->getBlocks();
  BlkList.splice(InsertPt, BlkList, this);
}

void
llvm::ilist_traits<swift::SILBasicBlock>::
transferNodesFromList(llvm::ilist_traits<SILBasicBlock> &SrcTraits,
                      llvm::ilist_iterator<SILBasicBlock> First,
                      llvm::ilist_iterator<SILBasicBlock> Last) {
  assert(&Parent->getModule() == &SrcTraits.Parent->getModule() &&
         "Module mismatch!");

  // If we are asked to splice into the same function, don't update parent
  // pointers.
  if (Parent == SrcTraits.Parent)
    return;

  ScopeCloner ScopeCloner(*Parent);
  SILBuilder B(*Parent);

  // If splicing blocks not in the same function, update the parent pointers.
  for (; First != Last; ++First) {
    First->Parent = Parent;
    for (auto &II : *First)
      II.setDebugScope(B,
                       ScopeCloner.getOrCreateClonedScope(II.getDebugScope()));
  }
}

/// ScopeCloner expects NewFn to be a clone of the original
/// function, with all debug scopes and locations still pointing to
/// the original function.
ScopeCloner::ScopeCloner(SILFunction &NewFn) : NewFn(NewFn) {
  // Some clients of SILCloner copy over the original function's
  // debug scope. Create a new one here.
  // FIXME: Audit all call sites and make them create the function
  // debug scope.
  auto *SILFn = NewFn.getDebugScope()->Parent.get<SILFunction *>();
  if (SILFn != &NewFn) {
    SILFn->setInlined();
    NewFn.setDebugScope(getOrCreateClonedScope(NewFn.getDebugScope()));
  }
}

const SILDebugScope *
ScopeCloner::getOrCreateClonedScope(const SILDebugScope *OrigScope) {
  if (!OrigScope)
    return nullptr;

  auto it = ClonedScopeCache.find(OrigScope);
  if (it != ClonedScopeCache.end())
    return it->second;

  auto ClonedScope = new (NewFn.getModule()) SILDebugScope(*OrigScope);
  if (OrigScope->InlinedCallSite) {
    // For inlined functions, we need to rewrite the inlined call site.
    ClonedScope->InlinedCallSite =
        getOrCreateClonedScope(OrigScope->InlinedCallSite);
  } else {
    if (auto *ParentScope = OrigScope->Parent.dyn_cast<const SILDebugScope *>())
      ClonedScope->Parent = getOrCreateClonedScope(ParentScope);
    else
      ClonedScope->Parent = &NewFn;
  }
  // Create an inline scope for the cloned instruction.
  assert(ClonedScopeCache.find(OrigScope) == ClonedScopeCache.end());
  ClonedScopeCache.insert({OrigScope, ClonedScope});
  return ClonedScope;
}

bool SILBasicBlock::isEntry() const {
  return this == &*getParent()->begin();
}
