//===--- GenControl.cpp - IR Generation for Control Flow ------------------===//
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
//  This file implements general IR generation for control flow.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Function.h"
#include "IRGenFunction.h"
#include "IRGenModule.h"

using namespace swift;
using namespace irgen;

/// Insert the given basic block after the IP block and move the
/// insertion point to it.  Only valid if the IP is valid.
void IRBuilder::emitBlock(llvm::BasicBlock *BB) {
  assert(ClearedIP == nullptr);
  llvm::BasicBlock *CurBB = GetInsertBlock();
  assert(CurBB && "current insertion point is invalid");
  CurBB->getParent()->getBasicBlockList().insertAfter(CurBB->getIterator(), BB);
  IRBuilderBase::SetInsertPoint(BB);
}

/// Create a new basic block with the given name.  The block is not
/// automatically inserted into the function.
llvm::BasicBlock *
IRGenFunction::createBasicBlock(const llvm::Twine &Name) {
  return llvm::BasicBlock::Create(IGM.getLLVMContext(), Name);
}

