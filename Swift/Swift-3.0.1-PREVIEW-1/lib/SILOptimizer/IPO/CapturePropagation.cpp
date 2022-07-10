//===--- CapturePropagation.cpp - Propagate closure capture constants -----===//
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

#define DEBUG_TYPE "capture-prop"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/Basic/Demangle.h"
#include "swift/SIL/Mangle.h"
#include "swift/SIL/SILCloner.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SILOptimizer/Analysis/ColdBlockInfo.h"
#include "swift/SILOptimizer/Analysis/DominanceAnalysis.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"

using namespace swift;

STATISTIC(NumCapturesPropagated, "Number of constant captures propagated");

namespace {
/// Propagate constants through closure captures by specializing the partially
/// applied function.
class CapturePropagation : public SILModuleTransform
{
public:
  void run() override;

  StringRef getName() override { return "Captured Constant Propagation"; }

protected:
  bool optimizePartialApply(PartialApplyInst *PAI);
  SILFunction *specializeConstClosure(PartialApplyInst *PAI,
                                      SILFunction *SubstF);
  void rewritePartialApply(PartialApplyInst *PAI, SILFunction *SpecialF);
};
} // namespace

static LiteralInst *getConstant(SILValue V) {
  if (auto I = dyn_cast<ThinToThickFunctionInst>(V))
    return getConstant(I->getOperand());
  return dyn_cast<LiteralInst>(V);
}

static bool isOptimizableConstant(SILValue V) {
  // We do not optimize string literals of length > 32 since we would need to
  // encode them into the symbol name for uniqueness.
  if (auto *SLI = dyn_cast<StringLiteralInst>(V))
    return SLI->getValue().size() <= 32;
  return true;
}

static bool isConstant(SILValue V) {
  V = getConstant(V);
  return V && isOptimizableConstant(V);
}

static std::string getClonedName(PartialApplyInst *PAI, IsFragile_t Fragile,
                                 SILFunction *F) {

  Mangle::Mangler M;
  auto P = SpecializationPass::CapturePropagation;
  FunctionSignatureSpecializationMangler Mangler(P, M, Fragile, F);

  // We know that all arguments are literal insts.
  auto Args = PAI->getArguments();
  for (unsigned i : indices(Args))
    Mangler.setArgumentConstantProp(i, getConstant(Args[i]));
  Mangler.mangle();

  return M.finalize();
}

namespace {
/// Clone the partially applied function, replacing incoming arguments with
/// literal constants.
///
/// The cloned literals will retain the SILLocation from the partial apply's
/// caller, so the cloned function will have a mix of locations from different
/// functions.
class CapturePropagationCloner
  : public SILClonerWithScopes<CapturePropagationCloner> {
  using SuperTy = SILClonerWithScopes<CapturePropagationCloner>;
  friend class SILVisitor<CapturePropagationCloner>;
  friend class SILCloner<CapturePropagationCloner>;

  SILFunction *OrigF;
  bool IsCloningConstant;
public:
  CapturePropagationCloner(SILFunction *OrigF, SILFunction *NewF)
    : SuperTy(*NewF), OrigF(OrigF), IsCloningConstant(false) {}

  void cloneBlocks(OperandValueArrayRef Args);

protected:
  /// Literals cloned from the caller drop their location so the debug line
  /// tables don't senselessly jump around. As a placeholder give them the
  /// location of the newly cloned function.
  SILLocation remapLocation(SILLocation InLoc) {
    if (IsCloningConstant)
      return getBuilder().getFunction().getLocation();
    return InLoc;
  }

  /// Literals cloned from the caller take on the new function's debug scope.
  void postProcess(SILInstruction *Orig, SILInstruction *Cloned) {
    assert(IsCloningConstant == (Orig->getFunction() != OrigF) &&
           "Expect only cloned constants from the caller function.");
    SILClonerWithScopes<CapturePropagationCloner>::postProcess(Orig, Cloned);
  }

  const SILDebugScope *remapScope(const SILDebugScope *DS) {
    if (IsCloningConstant)
      return getBuilder().getFunction().getDebugScope();
    else
      return SILClonerWithScopes<CapturePropagationCloner>::remapScope(DS);
  }

  void cloneConstValue(SILValue Const);
};
} // namespace

/// Clone a constant value. Recursively walk the operand chain through cast
/// instructions to ensure that all dependents are cloned. Note that the
/// original value may not belong to the same function as the one being cloned
/// by cloneBlocks() (they may be from the partial apply caller).
void CapturePropagationCloner::cloneConstValue(SILValue Val) {
  assert(IsCloningConstant && "incorrect mode");

  auto Inst = dyn_cast<SILInstruction>(Val);
  if (!Inst)
    return;

  auto II = InstructionMap.find(Inst);
  if (II != InstructionMap.end())
    return;

  if (Inst->getNumOperands() > 0) {
    // Only handle single operands for simple recursion without a worklist.
    assert(Inst->getNumOperands() == 1 && "expected single-operand cast");
    cloneConstValue(Inst->getOperand(0));
  }
  visit(Inst);
}

/// Clone the original partially applied function into the new specialized
/// function, replacing some arguments with literals.
void CapturePropagationCloner::cloneBlocks(
  OperandValueArrayRef PartialApplyArgs) {

  SILFunction &CloneF = getBuilder().getFunction();
  SILModule &M = CloneF.getModule();

  // Create the entry basic block with the function arguments.
  SILBasicBlock *OrigEntryBB = &*OrigF->begin();
  SILBasicBlock *ClonedEntryBB = new (M) SILBasicBlock(&CloneF);
  CanSILFunctionType CloneFTy = CloneF.getLoweredFunctionType();

  // Only clone the arguments that remain in the new function type. The trailing
  // arguments are now propagated through the partial apply.
  assert(!IsCloningConstant && "incorrect mode");
  unsigned ParamIdx = 0;
  for (unsigned NewParamEnd = CloneFTy->getNumSILArguments();
       ParamIdx != NewParamEnd; ++ParamIdx) {

    SILArgument *Arg = OrigEntryBB->getBBArg(ParamIdx);

    SILValue MappedValue = new (M)
        SILArgument(ClonedEntryBB, remapType(Arg->getType()), Arg->getDecl());
    ValueMap.insert(std::make_pair(Arg, MappedValue));
  }
  assert(OrigEntryBB->bbarg_size() - ParamIdx == PartialApplyArgs.size()
         && "unexpected number of partial apply arguments");

  // Replace the rest of the old arguments with constants.
  BBMap.insert(std::make_pair(OrigEntryBB, ClonedEntryBB));
  getBuilder().setInsertionPoint(ClonedEntryBB);
  IsCloningConstant = true;
  for (SILValue PartialApplyArg : PartialApplyArgs) {
    assert(isConstant(PartialApplyArg) &&
           "expected a constant arg to partial apply");

    cloneConstValue(PartialApplyArg);

    // The PartialApplyArg from the caller is now mapped to its cloned
    // instruction.  Also map the original argument to the cloned instruction.
    SILArgument *InArg = OrigEntryBB->getBBArg(ParamIdx);
    ValueMap.insert(std::make_pair(InArg, remapValue(PartialApplyArg)));
    ++ParamIdx;
  }
  IsCloningConstant = false;
  // Recursively visit original BBs in depth-first preorder, starting with the
  // entry block, cloning all instructions other than terminators.
  visitSILBasicBlock(OrigEntryBB);

  // Now iterate over the BBs and fix up the terminators.
  for (auto BI = BBMap.begin(), BE = BBMap.end(); BI != BE; ++BI) {
    getBuilder().setInsertionPoint(BI->second);
    visit(BI->first->getTerminator());
  }
}

/// Given a partial_apply instruction, create a specialized callee by removing
/// all constant arguments and adding constant literals to the specialized
/// function body.
SILFunction *CapturePropagation::specializeConstClosure(PartialApplyInst *PAI,
                                                        SILFunction *OrigF) {
  IsFragile_t Fragile = IsNotFragile;
  if (PAI->getFunction()->isFragile() && OrigF->isFragile())
    Fragile = IsFragile;

  std::string Name = getClonedName(PAI, Fragile, OrigF);

  // See if we already have a version of this function in the module. If so,
  // just return it.
  if (auto *NewF = OrigF->getModule().lookUpFunction(Name)) {
    assert(NewF->isFragile() == Fragile);
    DEBUG(llvm::dbgs()
              << "  Found an already specialized version of the callee: ";
          NewF->printName(llvm::dbgs()); llvm::dbgs() << "\n");
    return NewF;
  }

  // The new partial_apply will no longer take any arguments--they are all
  // expressed as literals. So its callee signature will be the same as its
  // return signature.
  CanSILFunctionType NewFTy =
    Lowering::adjustFunctionType(PAI->getType().castTo<SILFunctionType>(),
                                 SILFunctionType::Representation::Thin);
  SILFunction *NewF = getModule()->createFunction(
      SILLinkage::Shared, Name, NewFTy,
      /*contextGenericParams*/ nullptr, OrigF->getLocation(), OrigF->isBare(),
      OrigF->isTransparent(), Fragile, OrigF->isThunk(),
      OrigF->getClassVisibility(), OrigF->getInlineStrategy(),
      OrigF->getEffectsKind(),
      /*InsertBefore*/ OrigF, OrigF->getDebugScope(), OrigF->getDeclContext());
  NewF->setDeclCtx(OrigF->getDeclContext());
  DEBUG(llvm::dbgs() << "  Specialize callee as ";
        NewF->printName(llvm::dbgs()); llvm::dbgs() << " " << NewFTy << "\n");

  CapturePropagationCloner cloner(OrigF, NewF);
  cloner.cloneBlocks(PAI->getArguments());
  assert(OrigF->getDebugScope()->Parent != NewF->getDebugScope()->Parent);
  return NewF;
}

void CapturePropagation::rewritePartialApply(PartialApplyInst *OrigPAI,
                                             SILFunction *SpecialF) {
  SILBuilderWithScope Builder(OrigPAI);
  auto FuncRef = Builder.createFunctionRef(OrigPAI->getLoc(), SpecialF);
  auto NewPAI = Builder.createPartialApply(OrigPAI->getLoc(),
                                           FuncRef,
                                           SpecialF->getLoweredType(),
                                           ArrayRef<Substitution>(),
                                           ArrayRef<SILValue>(),
                                           OrigPAI->getType());
  OrigPAI->replaceAllUsesWith(NewPAI);
  recursivelyDeleteTriviallyDeadInstructions(OrigPAI, true);
  DEBUG(llvm::dbgs() << "  Rewrote caller:\n" << *NewPAI);
}

/// For now, we conservative only specialize if doing so can eliminate dynamic
/// dispatch.
///
/// TODO: Check for other profitable constant propagation, like builtin compare.
static bool isProfitable(SILFunction *Callee) {
  SILBasicBlock *EntryBB = &*Callee->begin();
  for (auto *Arg : EntryBB->getBBArgs()) {
    for (auto *Operand : Arg->getUses()) {
      if (auto *AI = dyn_cast<ApplyInst>(Operand->getUser())) {
        if (AI->getCallee() == Operand->get())
          return true;
      }
    }
  }
  return false;
}

bool CapturePropagation::optimizePartialApply(PartialApplyInst *PAI) {
  // Check if the partial_apply has generic substitutions.
  // FIXME: We could handle generic thunks if it's worthwhile.
  if (PAI->hasSubstitutions())
    return false;

  auto *FRI = dyn_cast<FunctionRefInst>(PAI->getCallee());
  if (!FRI)
    return false;

  assert(!FRI->getFunctionType()->isPolymorphic() &&
         "cannot specialize generic partial apply");

  for (auto Arg : PAI->getArguments()) {
    if (!isConstant(Arg))
      return false;
  }
  SILFunction *SubstF = FRI->getReferencedFunction();
  if (SubstF->isExternalDeclaration() || !isProfitable(SubstF))
    return false;

  DEBUG(llvm::dbgs() << "Specializing closure for constant arguments:\n"
        << "  " << SubstF->getName() << "\n" << *PAI);
  ++NumCapturesPropagated;
  SILFunction *NewF = specializeConstClosure(PAI, SubstF);
  rewritePartialApply(PAI, NewF);
  return true;
}

void CapturePropagation::run() {
  DominanceAnalysis *DA = PM->getAnalysis<DominanceAnalysis>();
  bool HasChanged = false;
  for (auto &F : *getModule()) {

    // Don't optimize functions that are marked with the opt.never attribute.
    if (!F.shouldOptimize())
      continue;

    // Cache cold blocks per function.
    ColdBlockInfo ColdBlocks(DA);
    for (auto &BB : F) {
      if (ColdBlocks.isCold(&BB))
        continue;

      auto I = BB.begin();
      while (I != BB.end()) {
        SILInstruction *Inst = &*I;
        ++I;
        if (PartialApplyInst *PAI = dyn_cast<PartialApplyInst>(Inst))
          HasChanged |= optimizePartialApply(PAI);
      }
    }
  }

  if (HasChanged) {
    invalidateAnalysis(SILAnalysis::InvalidationKind::Everything);
  }
}

SILTransform *swift::createCapturePropagation() {
  return new CapturePropagation();
}
