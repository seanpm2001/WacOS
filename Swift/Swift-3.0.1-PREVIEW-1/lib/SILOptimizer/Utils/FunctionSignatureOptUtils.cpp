//===--- FunctionSignatureOptUtils.cpp ------------------------------------===//
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

#include "swift/SIL/SILBasicBlock.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILValue.h"
#include "swift/SIL/DebugUtils.h"
#include "swift/SILOptimizer/Utils/FunctionSignatureOptUtils.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "swift/SIL/Mangle.h"

using namespace swift;

bool swift::hasNonTrivialNonDebugUse(SILArgument *Arg) {
  llvm::SmallVector<SILInstruction *, 8> Worklist;
  llvm::SmallPtrSet<SILInstruction *, 8> SeenInsts;

  for (Operand *I : getNonDebugUses(SILValue(Arg)))
    Worklist.push_back(I->getUser());

  while (!Worklist.empty()) {
    SILInstruction *U = Worklist.pop_back_val();
    if (!SeenInsts.insert(U).second)
      continue;

    // If U is a terminator inst, return false.
    if (isa<TermInst>(U))
      return true;

    // If U has side effects...
    if (U->mayHaveSideEffects()) 
      return true;

    // Otherwise add all non-debug uses of I to the worklist.
    for (Operand *I : getNonDebugUses(SILValue(U)))
      Worklist.push_back(I->getUser());
  }
  return false;
}

static bool isSpecializableRepresentation(SILFunctionTypeRepresentation Rep) {
  switch (Rep) {
  case SILFunctionTypeRepresentation::Method:
  case SILFunctionTypeRepresentation::Thin:
  case SILFunctionTypeRepresentation::Thick:
  case SILFunctionTypeRepresentation::CFunctionPointer:
    return true;
  case SILFunctionTypeRepresentation::WitnessMethod:
  case SILFunctionTypeRepresentation::ObjCMethod:
  case SILFunctionTypeRepresentation::Block:
    return false;
  }
}

/// Returns true if F is a function which the pass know show to specialize
/// function signatures for.
bool swift::canSpecializeFunction(SILFunction *F) {
  // Do not specialize the signature of SILFunctions that are external
  // declarations since there is no body to optimize.
  if (F->isExternalDeclaration())
    return false;

  // For now ignore functions with indirect results.
  if (F->getLoweredFunctionType()->hasIndirectResults())
    return false;

  // Do not specialize functions that are available externally. If an external
  // function was able to be specialized, it would have been specialized in its
  // own module. We will inline the original function as a thunk. The thunk will
  // call the specialized function.
  if (F->isAvailableExternally())
    return false;

  // Do not specialize the signature of always inline functions. We
  // will just inline them and specialize each one of the individual
  // functions that these sorts of functions are inlined into.
  if (F->getInlineStrategy() == Inline_t::AlwaysInline)
    return false;

  // For now ignore generic functions to keep things simple...
  if (F->getLoweredFunctionType()->isPolymorphic())
    return false;

  // Make sure F has a linkage that we can optimize.
  if (!isSpecializableRepresentation(F->getRepresentation()))
    return false;

  return true;
}

