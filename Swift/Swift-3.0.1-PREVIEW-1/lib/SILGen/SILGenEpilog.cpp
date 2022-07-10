//===--- SILGenEpilog.cpp - Function epilogue emission --------------------===//
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

#include "SILGen.h"
#include "SILGenFunction.h"
#include "ASTVisitor.h"
#include "swift/SIL/SILArgument.h"

using namespace swift;
using namespace Lowering;

void SILGenFunction::prepareEpilog(Type resultType, bool isThrowing,
                                   CleanupLocation CleanupL) {
  auto *epilogBB = createBasicBlock();

  // If we have any direct results, receive them via BB arguments.
  // But callers can disable this by passing a null result type.
  if (resultType) {
    NeedsReturn = (F.getLoweredFunctionType()->getNumAllResults() != 0);
    for (auto directResult : F.getLoweredFunctionType()->getDirectResults()) {
      SILType resultType = F.mapTypeIntoContext(directResult.getSILType());
      new (F.getModule()) SILArgument(epilogBB, resultType);
    }
  }

  ReturnDest = JumpDest(epilogBB, getCleanupsDepth(), CleanupL);

  if (isThrowing) {
    prepareRethrowEpilog(CleanupL);
  }
}

void SILGenFunction::prepareRethrowEpilog(CleanupLocation cleanupLoc) {
  auto exnType = SILType::getExceptionType(getASTContext());
  SILBasicBlock *rethrowBB = createBasicBlock(FunctionSection::Postmatter);
  new (F.getModule()) SILArgument(rethrowBB, exnType);
  ThrowDest = JumpDest(rethrowBB, getCleanupsDepth(), cleanupLoc);
}

/// Given a list of direct results, form the direct result value.
///
/// Note that this intentionally loses any tuple sub-structure of the
/// formal result type.
static SILValue buildReturnValue(SILGenFunction &gen, SILLocation loc,
                                 ArrayRef<SILValue> directResults) {
  if (directResults.size() == 1)
    return directResults[0];

  SmallVector<TupleTypeElt, 4> eltTypes;
  for (auto elt : directResults)
    eltTypes.push_back(elt->getType().getSwiftRValueType());
  auto resultType = SILType::getPrimitiveObjectType(
    CanType(TupleType::get(eltTypes, gen.getASTContext())));
  return gen.B.createTuple(loc, resultType, directResults);
}

std::pair<Optional<SILValue>, SILLocation>
SILGenFunction::emitEpilogBB(SILLocation TopLevel) {
  assert(ReturnDest.getBlock() && "no epilog bb prepared?!");
  SILBasicBlock *epilogBB = ReturnDest.getBlock();
  SILLocation ImplicitReturnFromTopLevel =
    ImplicitReturnLocation::getImplicitReturnLoc(TopLevel);
  SmallVector<SILValue, 4> directResults;
  Optional<SILLocation> returnLoc = None;

  // If the current BB isn't terminated, and we require a return, then we
  // are not allowed to fall off the end of the function and can't reach here.
  if (NeedsReturn && B.hasValidInsertionPoint())
    B.createUnreachable(ImplicitReturnFromTopLevel);

  if (epilogBB->pred_empty()) {
    // If the epilog was not branched to at all, kill the BB and
    // just emit the epilog into the current BB.
    while (!epilogBB->empty())
      epilogBB->back().eraseFromParent();
    eraseBasicBlock(epilogBB);

    // If the current bb is terminated then the epilog is just unreachable.
    if (!B.hasValidInsertionPoint())
      return { None, TopLevel };

    // We emit the epilog at the current insertion point.
    returnLoc = ImplicitReturnFromTopLevel;

  } else if (std::next(epilogBB->pred_begin()) == epilogBB->pred_end()
             && !B.hasValidInsertionPoint()) {
    // If the epilog has a single predecessor and there's no current insertion
    // point to fall through from, then we can weld the epilog to that
    // predecessor BB.

    // Steal the branch argument as the return value if present.
    SILBasicBlock *pred = *epilogBB->pred_begin();
    BranchInst *predBranch = cast<BranchInst>(pred->getTerminator());
    assert(predBranch->getArgs().size() == epilogBB->bbarg_size() &&
           "epilog predecessor arguments does not match block params");

    for (auto index : indices(predBranch->getArgs())) {
      SILValue result = predBranch->getArgs()[index];
      directResults.push_back(result);
      epilogBB->getBBArg(index)->replaceAllUsesWith(result);
    }

    // If we are optimizing, we should use the return location from the single,
    // previously processed, return statement if any.
    if (predBranch->getLoc().is<ReturnLocation>()) {
      returnLoc = predBranch->getLoc();
    } else {
      returnLoc = ImplicitReturnFromTopLevel;
    }
    
    // Kill the branch to the now-dead epilog BB.
    pred->erase(predBranch);

    // Move any instructions from the EpilogBB to the end of the 'pred' block.
    pred->spliceAtEnd(epilogBB);

    // Finally we can erase the epilog BB.
    eraseBasicBlock(epilogBB);

    // Emit the epilog into its former predecessor.
    B.setInsertionPoint(pred);
  } else {
    // Move the epilog block to the end of the ordinary section.
    auto endOfOrdinarySection =
      (StartOfPostmatter ? SILFunction::iterator(StartOfPostmatter) : F.end());
    B.moveBlockTo(epilogBB, endOfOrdinarySection);

    // Emit the epilog into the epilog bb. Its arguments are the
    // direct results.
    directResults.append(epilogBB->bbarg_begin(), epilogBB->bbarg_end());

    // If we are falling through from the current block, the return is implicit.
    B.emitBlock(epilogBB, ImplicitReturnFromTopLevel);
  }
  
  // Emit top-level cleanups into the epilog block.
  assert(!Cleanups.hasAnyActiveCleanups(getCleanupsDepth(),
                                        ReturnDest.getDepth()) &&
         "emitting epilog in wrong scope");

  auto cleanupLoc = CleanupLocation::get(TopLevel);
  Cleanups.emitCleanupsForReturn(cleanupLoc);

  // If the return location is known to be that of an already
  // processed return, use it. (This will get triggered when the
  // epilog logic is simplified.)
  //
  // Otherwise make the ret instruction part of the cleanups.
  if (!returnLoc) returnLoc = cleanupLoc;

  // Build the return value.  We don't do this if there are no direct
  // results; this can happen for void functions, but also happens when
  // prepareEpilog was asked to not add result arguments to the epilog
  // block.
  SILValue returnValue;
  if (!directResults.empty()) {
    assert(directResults.size()
             == F.getLoweredFunctionType()->getNumDirectResults());
    returnValue = buildReturnValue(*this, TopLevel, directResults);
  }

  return { returnValue, *returnLoc };
}

SILLocation SILGenFunction::
emitEpilog(SILLocation TopLevel, bool UsesCustomEpilog) {
  Optional<SILValue> maybeReturnValue;
  SILLocation returnLoc(TopLevel);
  std::tie(maybeReturnValue, returnLoc) = emitEpilogBB(TopLevel);

  SILBasicBlock *ResultBB = nullptr;
  
  if (!maybeReturnValue) {
    // Nothing to do.
  } else if (UsesCustomEpilog) {
    // If the epilog is reachable, and the caller provided an epilog, just
    // remember the block so the caller can continue it.
    ResultBB = B.getInsertionBB();
    assert(ResultBB && "Didn't have an epilog block?");
    B.clearInsertionPoint();
  } else {
    // Otherwise, if the epilog block is reachable, return the return value.
    SILValue returnValue = *maybeReturnValue;

    // Return () if no return value was given.
    if (!returnValue)
      returnValue = emitEmptyTuple(CleanupLocation::get(TopLevel));

    B.createReturn(returnLoc, returnValue);
  }
  
  emitRethrowEpilog(TopLevel);
  
  if (ResultBB)
    B.setInsertionPoint(ResultBB);
  
  return returnLoc;
}

void SILGenFunction::emitRethrowEpilog(SILLocation topLevel) {
  assert(!B.hasValidInsertionPoint());

  // If we don't have a rethrow destination, we're done.
  if (!ThrowDest.isValid())
    return;

  // If the rethrow destination isn't used, we're done.
  SILBasicBlock *rethrowBB = ThrowDest.getBlock();
  if (rethrowBB->pred_empty()) {
    ThrowDest = JumpDest::invalid();
    eraseBasicBlock(rethrowBB);
    return;
  }

  SILLocation throwLoc = topLevel;
  SILValue exn = rethrowBB->bbarg_begin()[0];
  bool reposition = true;

  // If the rethrow destination has a single branch predecessor,
  // consider emitting the rethrow into it.
  SILBasicBlock *predBB = *rethrowBB->pred_begin();
  if (std::next(rethrowBB->pred_begin()) == rethrowBB->pred_end()) {
    if (auto branch = dyn_cast<BranchInst>(predBB->getTerminator())) {
      assert(branch->getArgs().size() == 1);

      // Save the location and operand information from the branch,
      // then destroy it.
      throwLoc = branch->getLoc();
      exn = branch->getArgs()[0];
      predBB->erase(branch);

      // Erase the rethrow block.
      eraseBasicBlock(rethrowBB);
      rethrowBB = predBB;
      reposition = false;
    }
  }

  // Reposition the rethrow block to the end of the postmatter section
  // unless we're emitting into a single predecessor.
  if (reposition) {
    B.moveBlockTo(rethrowBB, F.end());
  }

  B.setInsertionPoint(rethrowBB);
  Cleanups.emitCleanupsForReturn(ThrowDest.getCleanupLocation());

  B.createThrow(throwLoc, exn);

  ThrowDest = JumpDest::invalid();
}
