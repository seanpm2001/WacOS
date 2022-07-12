//===--- SILInstruction.cpp - Instructions for SIL code -------------------===//
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
// This file defines the high-level SILInstruction classes used for SIL code.
//
//===----------------------------------------------------------------------===//

#include "swift/SIL/SILInstruction.h"
#include "swift/Basic/type_traits.h"
#include "swift/Basic/Unicode.h"
#include "swift/SIL/SILBuilder.h"
#include "swift/SIL/SILCloner.h"
#include "swift/SIL/SILDebugScope.h"
#include "swift/SIL/SILVisitor.h"
#include "swift/Basic/AssertImplements.h"
#include "swift/ClangImporter/ClangModule.h"
#include "swift/SIL/SILModule.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ErrorHandling.h"
using namespace swift;
using namespace Lowering;

//===----------------------------------------------------------------------===//
// Instruction-specific properties on SILValue
//===----------------------------------------------------------------------===//

SILLocation SILInstruction::getLoc() const { return Location.getLocation(); }

const SILDebugScope *SILInstruction::getDebugScope() const {
  return Location.getScope();
}

void SILInstruction::setDebugScope(SILBuilder &B, const SILDebugScope *DS) {
  if (getDebugScope() && getDebugScope()->InlinedCallSite)
    assert(DS->InlinedCallSite && "throwing away inlined scope info");

  assert(DS->getParentFunction() == getFunction() &&
         "scope belongs to different function");

  Location = SILDebugLocation(getLoc(), DS);
}

//===----------------------------------------------------------------------===//
// ilist_traits<SILInstruction> Implementation
//===----------------------------------------------------------------------===//

// The trait object is embedded into a basic block.  Use dirty hacks to
// reconstruct the BB from the 'self' pointer of the trait.
SILBasicBlock *llvm::ilist_traits<SILInstruction>::getContainingBlock() {
  size_t Offset(
      size_t(&((SILBasicBlock *)nullptr->*SILBasicBlock::getSublistAccess())));
  iplist<SILInstruction> *Anchor(static_cast<iplist<SILInstruction> *>(this));
  return reinterpret_cast<SILBasicBlock *>(reinterpret_cast<char *>(Anchor) -
                                           Offset);
}


void llvm::ilist_traits<SILInstruction>::addNodeToList(SILInstruction *I) {
  assert(I->ParentBB == nullptr && "Already in a list!");
  I->ParentBB = getContainingBlock();
}

void llvm::ilist_traits<SILInstruction>::removeNodeFromList(SILInstruction *I) {
  // When an instruction is removed from a BB, clear the parent pointer.
  assert(I->ParentBB && "Not in a list!");
  I->ParentBB = nullptr;
}

void llvm::ilist_traits<SILInstruction>::
transferNodesFromList(llvm::ilist_traits<SILInstruction> &L2,
                      instr_iterator first, instr_iterator last) {
  // If transferring instructions within the same basic block, no reason to
  // update their parent pointers.
  SILBasicBlock *ThisParent = getContainingBlock();
  if (ThisParent == L2.getContainingBlock()) return;

  // Update the parent fields in the instructions.
  for (; first != last; ++first)
    first->ParentBB = ThisParent;
}

//===----------------------------------------------------------------------===//
// SILInstruction Implementation
//===----------------------------------------------------------------------===//

// Assert that all subclasses of ValueBase implement classof.
#define NODE(CLASS, PARENT) \
  ASSERT_IMPLEMENTS_STATIC(CLASS, PARENT, classof, bool(const SILNode*));
#include "swift/SIL/SILNodes.def"

SILFunction *SILInstruction::getFunction() {
  return getParent()->getParent();
}
const SILFunction *SILInstruction::getFunction() const {
  return getParent()->getParent();
}

SILModule &SILInstruction::getModule() const {
  return getFunction()->getModule();
}

/// eraseFromParent - This method unlinks 'self' from the containing basic
/// block and deletes it.
///
void SILInstruction::eraseFromParent() {
#ifndef NDEBUG
  for (auto result : getResults()) {
    assert(result->use_empty() && "Uses of SILInstruction remain at deletion.");
  }
#endif
  getParent()->erase(this);
}

void SILInstruction::moveFront(SILBasicBlock *Block) {
  getParent()->remove(this);
  Block->push_front(this);
}

/// Unlink this instruction from its current basic block and insert it into
/// the basic block that Later lives in, right before Later.
void SILInstruction::moveBefore(SILInstruction *Later) {
  if (this == Later)
    return;

  getParent()->remove(this);
  Later->getParent()->insert(Later, this);
}

/// Unlink this instruction from its current basic block and insert it into
/// the basic block that Earlier lives in, right after Earlier.
void SILInstruction::moveAfter(SILInstruction *Earlier) {
  // Since MovePos is an instruction, we know that there is always a valid
  // iterator after it.
  auto Later = std::next(SILBasicBlock::iterator(Earlier));
  moveBefore(&*Later);
}

void SILInstruction::dropAllReferences() {
  MutableArrayRef<Operand> PossiblyDeadOps = getAllOperands();
  for (auto OpI = PossiblyDeadOps.begin(),
            OpE = PossiblyDeadOps.end(); OpI != OpE; ++OpI) {
    OpI->drop();
  }

  // If we have a function ref inst, we need to especially drop its function
  // argument so that it gets a proper ref decrement.
  if (auto *FRI = dyn_cast<FunctionRefInst>(this)) {
    if (!FRI->getReferencedFunction())
      return;
    FRI->dropReferencedFunction();
    return;
  }

  // If we have a KeyPathInst, drop its pattern reference so that we can
  // decrement refcounts on referenced functions.
  if (auto *KPI = dyn_cast<KeyPathInst>(this)) {
    if (!KPI->hasPattern())
      return;
    
    KPI->dropReferencedPattern();
    return;
  }
}

namespace {

class AllResultsAccessor
    : public SILInstructionVisitor<AllResultsAccessor,
                                   SILInstructionResultArray> {
public:
// Make sure we hit a linker error if we ever miss an instruction.
#define INST(ID, NAME) SILInstructionResultArray visit##ID(ID *I);
#define NON_VALUE_INST(ID, NAME, PARENT, MEMBEHAVIOR, MAYRELEASE)              \
  SILInstructionResultArray visit##ID(ID *I) {                                 \
    return SILInstructionResultArray();                                        \
  }
#define SINGLE_VALUE_INST(ID, TEXTUALNAME, PARENT, MEMBEHAVIOR, MAYRELEASE)    \
  SILInstructionResultArray visit##ID(ID *I) {                                 \
    return SILInstructionResultArray(                                          \
        static_cast<SingleValueInstruction *>(I));                             \
  }
#define MULTIPLE_VALUE_INST(ID, TEXTUALNAME, PARENT, MEMBEHAVIOR, MAYRELEASE)  \
  SILInstructionResultArray visit##ID(ID *I) {                                 \
    return SILInstructionResultArray(I->getAllResults());                      \
  }
#include "swift/SIL/SILNodes.def"
};

} // end anonymous namespace

SILInstructionResultArray SILInstruction::getResultsImpl() const {
  return AllResultsAccessor().visit(const_cast<SILInstruction *>(this));
}

// Initialize the static members of SILInstruction.
int SILInstruction::NumCreatedInstructions = 0;
int SILInstruction::NumDeletedInstructions = 0;

/// Map a SILInstruction name to its SILInstructionKind.
SILInstructionKind swift::getSILInstructionKind(StringRef name) {
#define FULL_INST(ID, NAME, PARENT, MEMBEHAVIOR, MAYRELEASE)  \
  if (name == #NAME)                                          \
    return SILInstructionKind::ID;
#include "swift/SIL/SILNodes.def"

#ifdef NDEBUG
  llvm::errs() << "Unknown SIL instruction name\n";
  abort();
#endif
  llvm_unreachable("Unknown SIL insruction name");
}

/// Map SILInstructionKind to a corresponding SILInstruction name.
StringRef swift::getSILInstructionName(SILInstructionKind kind) {
  switch (kind) {
#define FULL_INST(ID, NAME, PARENT, MEMBEHAVIOR, MAYRELEASE)  \
  case SILInstructionKind::ID:                                \
    return #NAME;
#include "swift/SIL/SILNodes.def"
  }
  llvm_unreachable("bad kind");
}

void SILInstruction::replaceAllUsesOfAllResultsWithUndef() {
  for (auto result : getResults()) {
    result->replaceAllUsesWithUndef();
  }
}

void SILInstruction::replaceAllUsesPairwiseWith(SILInstruction *other) {
  auto results = getResults();

  // If we don't have any results, fast-path out without asking the other
  // instruction for its results.
  if (results.empty()) {
    assert(other->getResults().empty());
    return;
  }

  // Replace values with the corresponding values of the other instruction.
  auto otherResults = other->getResults();
  assert(results.size() == otherResults.size());
  for (auto i : indices(results)) {
    results[i]->replaceAllUsesWith(otherResults[i]);
  }
}

void SILInstruction::replaceAllUsesPairwiseWith(
    const llvm::SmallVectorImpl<SILValue> &NewValues) {
  auto Results = getResults();

  // If we don't have any results, fast-path out without asking the other
  // instruction for its results.
  if (Results.empty()) {
    assert(NewValues.empty());
    return;
  }

  // Replace values with the corresponding values of the list. Make sure they
  // are all the same type.
  assert(Results.size() == NewValues.size());
  for (unsigned i : indices(Results)) {
    assert(Results[i]->getType() == NewValues[i]->getType() &&
           "Can only replace results with new values of the same type");
    Results[i]->replaceAllUsesWith(NewValues[i]);
  }
}

namespace {
class InstructionDestroyer
    : public SILInstructionVisitor<InstructionDestroyer> {
public:
#define INST(CLASS, PARENT) \
  void visit##CLASS(CLASS *I) { I->~CLASS(); }
#include "swift/SIL/SILNodes.def"
  };
} // end anonymous namespace

void SILInstruction::destroy(SILInstruction *I) {
  InstructionDestroyer().visit(I);
}

namespace {
  /// Given a pair of instructions that are already known to have the same kind,
  /// type, and operands check any special state in the two instructions that
  /// could disrupt equality.
  class InstructionIdentityComparer :
    public SILInstructionVisitor<InstructionIdentityComparer, bool> {
  public:

    InstructionIdentityComparer(const SILInstruction *L) : LHS(L) { }

    /// Make sure we only process instructions we know how to process.
    bool visitSILInstruction(const SILInstruction *RHS) {
      return false;
    }

    bool visitInjectEnumAddrInst(const InjectEnumAddrInst *RHS) {
        auto *X = cast<InjectEnumAddrInst>(LHS);
        return X->getElement() == RHS->getElement();
    }

    bool visitDestroyAddrInst(const DestroyAddrInst *RHS) {
      return true;
    }

    bool visitReleaseValueInst(const ReleaseValueInst *RHS) {
      return true;
    }

    bool visitReleaseValueAddrInst(const ReleaseValueAddrInst *RHS) {
      return true;
    }

    bool visitRetainValueInst(const RetainValueInst *RHS) {
      return true;
    }

    bool visitRetainValueAddrInst(const RetainValueAddrInst *RHS) {
      return true;
    }

    bool visitDeallocStackInst(const DeallocStackInst *RHS) {
      return true;
    }

    bool visitAllocStackInst(const AllocStackInst *RHS) {
      return true;
    }

    bool visitDeallocBoxInst(const DeallocBoxInst *RHS) {
      return true;
    }

    bool visitAllocBoxInst(const AllocBoxInst *RHS) {
      return true;
    }

    bool visitDeallocRefInst(const DeallocRefInst *RHS) {
      return true;
    }

    bool visitDeallocPartialRefInst(const DeallocPartialRefInst *RHS) {
      return true;
    }

    bool visitAllocRefInst(const AllocRefInst *RHS) {
      auto *LHSInst = cast<AllocRefInst>(LHS);
      auto LHSTypes = LHSInst->getTailAllocatedTypes();
      auto RHSTypes = RHS->getTailAllocatedTypes();
      unsigned NumTypes = LHSTypes.size();
      assert(NumTypes == RHSTypes.size());
      for (unsigned Idx = 0; Idx < NumTypes; ++Idx) {
        if (LHSTypes[Idx] != RHSTypes[Idx])
          return false;
      }
      return true;
    }

    bool visitAllocRefDynamicInst(const AllocRefDynamicInst *RHS) {
      return true;
    }

    bool visitProjectValueBufferInst(const ProjectValueBufferInst *RHS) {
      auto *X = cast<ProjectValueBufferInst>(LHS);
      return X->getValueType() == RHS->getValueType();
    }

    bool visitProjectBoxInst(const ProjectBoxInst *RHS) {
      return true;
    }

    bool visitProjectExistentialBoxInst(const ProjectExistentialBoxInst *RHS) {
      return true;
    }

    bool visitBeginAccessInst(const BeginAccessInst *right) {
      auto left = cast<BeginAccessInst>(LHS);
      return left->getAccessKind() == right->getAccessKind()
          && left->getEnforcement() == right->getEnforcement();
    }

    bool visitEndAccessInst(const EndAccessInst *right) {
      auto left = cast<EndAccessInst>(LHS);
      return left->isAborting() == right->isAborting();
    }

    bool visitBeginUnpairedAccessInst(const BeginUnpairedAccessInst *right) {
      auto left = cast<BeginUnpairedAccessInst>(LHS);
      return left->getAccessKind() == right->getAccessKind()
          && left->getEnforcement() == right->getEnforcement();
    }

    bool visitEndUnpairedAccessInst(const EndUnpairedAccessInst *right) {
      auto left = cast<EndUnpairedAccessInst>(LHS);
      return left->getEnforcement() == right->getEnforcement()
          && left->isAborting() == right->isAborting();
    }

    bool visitStrongReleaseInst(const StrongReleaseInst *RHS) {
      return true;
    }

    bool visitStrongRetainInst(const StrongRetainInst *RHS) {
      return true;
    }

    bool visitStrongRetainUnownedInst(const StrongRetainUnownedInst *RHS) {
      return true;
    }

    bool visitLoadInst(const LoadInst *RHS) {
      auto LHSQualifier = cast<LoadInst>(LHS)->getOwnershipQualifier();
      return LHSQualifier == RHS->getOwnershipQualifier();
    }

    bool visitLoadBorrowInst(const LoadBorrowInst *RHS) { return true; }

    bool visitEndBorrowInst(const EndBorrowInst *RHS) { return true; }
    bool visitBeginBorrowInst(const BeginBorrowInst *BBI) { return true; }

    bool visitStoreBorrowInst(const StoreBorrowInst *RHS) {
      auto *X = cast<StoreBorrowInst>(LHS);
      return X->getSrc() == RHS->getSrc() && X->getDest() == RHS->getDest();
    }

    bool visitStoreInst(const StoreInst *RHS) {
      auto *X = cast<StoreInst>(LHS);
      return X->getSrc() == RHS->getSrc() && X->getDest() == RHS->getDest() &&
        X->getOwnershipQualifier() == RHS->getOwnershipQualifier();
    }

    bool visitBindMemoryInst(const BindMemoryInst *RHS) {
      auto *X = cast<BindMemoryInst>(LHS);
      return X->getBoundType() == RHS->getBoundType();
    }

    bool visitFunctionRefInst(const FunctionRefInst *RHS) {
      auto *X = cast<FunctionRefInst>(LHS);
      return X->getReferencedFunction() == RHS->getReferencedFunction();
    }

    bool visitAllocGlobalInst(const AllocGlobalInst *RHS) {
      auto *X = cast<AllocGlobalInst>(LHS);
      return X->getReferencedGlobal() == RHS->getReferencedGlobal();
    }

    bool visitGlobalAddrInst(const GlobalAddrInst *RHS) {
      auto *X = cast<GlobalAddrInst>(LHS);
      return X->getReferencedGlobal() == RHS->getReferencedGlobal();
    }

    bool visitIntegerLiteralInst(const IntegerLiteralInst *RHS) {
      APInt X = cast<IntegerLiteralInst>(LHS)->getValue();
      APInt Y = RHS->getValue();
      return X.getBitWidth() == Y.getBitWidth() &&
        X == Y;
    }

    bool visitFloatLiteralInst(const FloatLiteralInst *RHS) {
      // Avoid floating point comparison issues by doing a bitwise comparison.
      APInt X = cast<FloatLiteralInst>(LHS)->getBits();
      APInt Y = RHS->getBits();
      return X.getBitWidth() == Y.getBitWidth() &&
        X == Y;
    }

    bool visitStringLiteralInst(const StringLiteralInst *RHS) {
      auto LHS_ = cast<StringLiteralInst>(LHS);
      return LHS_->getEncoding() == RHS->getEncoding()
        && LHS_->getValue().equals(RHS->getValue());
    }

    bool visitConstStringLiteralInst(const ConstStringLiteralInst *RHS) {
      auto LHS_ = cast<ConstStringLiteralInst>(LHS);
      return LHS_->getEncoding() == RHS->getEncoding() &&
             LHS_->getValue().equals(RHS->getValue());
    }

    bool visitStructInst(const StructInst *RHS) {
      // We have already checked the operands. Make sure that the StructDecls
      // match up.
      StructDecl *S1 = cast<StructInst>(LHS)->getStructDecl();
      return S1 == RHS->getStructDecl();
    }

    bool visitStructExtractInst(const StructExtractInst *RHS) {
      // We have already checked that the operands of our struct_extracts
      // match. Thus we need to check the field/struct decl which are not
      // operands.
      auto *X = cast<StructExtractInst>(LHS);
      if (X->getStructDecl() != RHS->getStructDecl())
        return false;
      if (X->getField() != RHS->getField())
        return false;
      return true;
    }

    bool visitRefElementAddrInst(RefElementAddrInst *RHS) {
      auto *X = cast<RefElementAddrInst>(LHS);
      if (X->getField() != RHS->getField())
        return false;
      if (X->getOperand() != RHS->getOperand())
        return false;
      return true;
    }

    bool visitRefTailAddrInst(RefTailAddrInst *RHS) {
      auto *X = cast<RefTailAddrInst>(LHS);
      return X->getTailType() == RHS->getTailType();
    }

    bool visitStructElementAddrInst(const StructElementAddrInst *RHS) {
      // We have already checked that the operands of our struct_element_addrs
      // match. Thus we only need to check the field/struct decl which are not
      // operands.
      auto *X = cast<StructElementAddrInst>(LHS);
      if (X->getStructDecl() != RHS->getStructDecl())
        return false;
      if (X->getField() != RHS->getField())
        return false;
      return true;
    }

    bool visitTupleInst(const TupleInst *RHS) {
      // We have already checked the operands. Make sure that the tuple types
      // match up.
      TupleType *TT1 = cast<TupleInst>(LHS)->getTupleType();
      return TT1 == RHS->getTupleType();
    }

    bool visitTupleExtractInst(const TupleExtractInst *RHS) {
      // We have already checked that the operands match. Thus we only need to
      // check the field no and tuple type which are not represented as operands.
      auto *X = cast<TupleExtractInst>(LHS);
      if (X->getTupleType() != RHS->getTupleType())
        return false;
      if (X->getFieldNo() != RHS->getFieldNo())
        return false;
      return true;
    }

    bool visitTupleElementAddrInst(const TupleElementAddrInst *RHS) {
      // We have already checked that the operands match. Thus we only need to
      // check the field no and tuple type which are not represented as operands.
      auto *X = cast<TupleElementAddrInst>(LHS);
      if (X->getTupleType() != RHS->getTupleType())
        return false;
      if (X->getFieldNo() != RHS->getFieldNo())
        return false;
      return true;
    }

    bool visitMetatypeInst(const MetatypeInst *RHS) {
      // We have already compared the operands/types, so we should have equality
      // at this point.
      return true;
    }

    bool visitValueMetatypeInst(const ValueMetatypeInst *RHS) {
      // We have already compared the operands/types, so we should have equality
      // at this point.
      return true;
    }

    bool visitExistentialMetatypeInst(const ExistentialMetatypeInst *RHS) {
      // We have already compared the operands/types, so we should have equality
      // at this point.
      return true;
    }

    bool visitIndexRawPointerInst(IndexRawPointerInst *RHS) {
      // We have already compared the operands/types, so we should have equality
      // at this point.
      return true;
    }

    bool visitIndexAddrInst(IndexAddrInst *RHS) {
      // We have already compared the operands/types, so we should have equality
      // at this point.
      return true;
    }

    bool visitTailAddrInst(TailAddrInst *RHS) {
      auto *X = cast<TailAddrInst>(LHS);
      return X->getTailType() == RHS->getTailType();
    }

    bool visitCondFailInst(CondFailInst *RHS) {
      // We have already compared the operands/types, so we should have equality
      // at this point.
      return true;
    }

    bool visitApplyInst(ApplyInst *RHS) {
      auto *X = cast<ApplyInst>(LHS);
      return X->getSubstitutions() == RHS->getSubstitutions();
    }

    bool visitBuiltinInst(BuiltinInst *RHS) {
      auto *X = cast<BuiltinInst>(LHS);
      if (X->getName() != RHS->getName())
        return false;
      return X->getSubstitutions() == RHS->getSubstitutions();
    }

    bool visitEnumInst(EnumInst *RHS) {
      // We already checked operands and types. Only thing we need to check is
      // that the element is the same.
      auto *X = cast<EnumInst>(LHS);
      return X->getElement() == RHS->getElement();
    }

    bool visitUncheckedEnumDataInst(UncheckedEnumDataInst *RHS) {
      // We already checked operands and types. Only thing we need to check is
      // that the element is the same.
      auto *X = cast<UncheckedEnumDataInst>(LHS);
      return X->getElement() == RHS->getElement();
    }

    bool visitSelectEnumInstBase(const SelectEnumInstBase *RHS) {
      // Check that the instructions match cases in the same order.
      auto *X = cast<SelectEnumInstBase>(LHS);

      if (X->getNumCases() != RHS->getNumCases())
        return false;
      if (X->hasDefault() != RHS->hasDefault())
        return false;

      for (unsigned i = 0, e = X->getNumCases(); i < e; ++i) {
        if (X->getCase(i).first != RHS->getCase(i).first)
          return false;
      }

      return true;
    }

    bool visitSelectEnumInst(const SelectEnumInst *RHS) {
      return visitSelectEnumInstBase(RHS);
    }
    bool visitSelectEnumAddrInst(const SelectEnumAddrInst *RHS) {
      return visitSelectEnumInstBase(RHS);
    }

    bool visitSelectValueInst(const SelectValueInst *RHS) {
      // Check that the instructions match cases in the same order.
      auto *X = cast<SelectValueInst>(LHS);

      if (X->getNumCases() != RHS->getNumCases())
        return false;
      if (X->hasDefault() != RHS->hasDefault())
        return false;

      for (unsigned i = 0, e = X->getNumCases(); i < e; ++i) {
        if (X->getCase(i).first != RHS->getCase(i).first)
          return false;
        if (X->getCase(i).second != RHS->getCase(i).second)
          return false;
      }

      return true;
    }

    // Conversion instructions.
    // All of these just return true as they have already had their
    // operands and types checked
    bool visitUncheckedRefCastInst(UncheckedRefCastInst *RHS) {
      return true;
    }

    bool visitUncheckedAddrCastInst(UncheckedAddrCastInst *RHS) {
      return true;
    }

    bool visitUncheckedTrivialBitCastInst(UncheckedTrivialBitCastInst *RHS) {
      return true;
    }

    bool visitUncheckedBitwiseCastInst(UncheckedBitwiseCastInst *RHS) {
      return true;
    }

    bool visitUpcastInst(UpcastInst *RHS) {
      return true;
    }

    bool visitAddressToPointerInst(AddressToPointerInst *RHS) {
      return true;
    }

    bool visitPointerToAddressInst(PointerToAddressInst *RHS) {
      return cast<PointerToAddressInst>(LHS)->isStrict() == RHS->isStrict();
    }

    bool visitRefToRawPointerInst(RefToRawPointerInst *RHS) {
      return true;
    }

    bool visitRawPointerToRefInst(RawPointerToRefInst *RHS) {
      return true;
    }

    bool visitRefToUnownedInst(RefToUnownedInst *RHS) {
      return true;
    }

    bool visitUnownedToRefInst(UnownedToRefInst *RHS) {
      return true;
    }

    bool visitRefToUnmanagedInst(RefToUnmanagedInst *RHS) {
      return true;
    }

    bool visitUnmanagedToRefInst(UnmanagedToRefInst *RHS) {
      return true;
    }

    bool visitThinToThickFunctionInst(ThinToThickFunctionInst *RHS) {
      return true;
    }

    bool visitThickToObjCMetatypeInst(ThickToObjCMetatypeInst *RHS) {
      return true;
    }

    bool visitObjCToThickMetatypeInst(ObjCToThickMetatypeInst *RHS) {
      return true;
    }

    bool visitConvertFunctionInst(ConvertFunctionInst *RHS) {
      return true;
    }

    bool visitObjCMetatypeToObjectInst(ObjCMetatypeToObjectInst *RHS) {
      return true;
    }

    bool visitObjCExistentialMetatypeToObjectInst(ObjCExistentialMetatypeToObjectInst *RHS) {
      return true;
    }

    bool visitProjectBlockStorageInst(ProjectBlockStorageInst *RHS) {
      return true;
    }

    bool visitBridgeObjectToRefInst(BridgeObjectToRefInst *X) {
      return true;
    }

    bool visitBridgeObjectToWordInst(BridgeObjectToWordInst *X) {
      return true;
    }
      
    bool visitRefToBridgeObjectInst(RefToBridgeObjectInst *X) {
      return true;
    }
    bool visitThinFunctionToPointerInst(ThinFunctionToPointerInst *X) {
      return true;
    }
    bool visitPointerToThinFunctionInst(PointerToThinFunctionInst *X) {
      return true;
    }

    bool visitObjCProtocolInst(ObjCProtocolInst *RHS) {
      auto *X = cast<ObjCProtocolInst>(LHS);
      return X->getProtocol() == RHS->getProtocol();
    }

    bool visitClassMethodInst(ClassMethodInst *RHS) {
      auto *X = cast<ClassMethodInst>(LHS);
      return X->getMember()  == RHS->getMember() &&
             X->getOperand() == RHS->getOperand() &&
             X->getType()    == RHS->getType();
    }

    bool visitSuperMethodInst(SuperMethodInst *RHS) {
      auto *X = cast<SuperMethodInst>(LHS);
      return X->getMember()  == RHS->getMember() &&
             X->getOperand() == RHS->getOperand() &&
             X->getType()    == RHS->getType();
    }

    bool visitObjCMethodInst(ObjCMethodInst *RHS) {
      auto *X = cast<ObjCMethodInst>(LHS);
      return X->getMember()  == RHS->getMember() &&
             X->getOperand() == RHS->getOperand() &&
             X->getType()    == RHS->getType();
    }

    bool visitObjCSuperMethodInst(ObjCSuperMethodInst *RHS) {
      auto *X = cast<ObjCSuperMethodInst>(LHS);
      return X->getMember()  == RHS->getMember() &&
             X->getOperand() == RHS->getOperand() &&
             X->getType()    == RHS->getType();
    }

    bool visitWitnessMethodInst(const WitnessMethodInst *RHS) {
      auto *X = cast<WitnessMethodInst>(LHS);
      if (X->getMember() != RHS->getMember())
        return false;
      if (X->getLookupType() != RHS->getLookupType())
        return false;
      if (X->getConformance() != RHS->getConformance())
        return false;
      return true;
    }

    bool visitMarkDependenceInst(const MarkDependenceInst *RHS) {
       return true;
    }

    bool visitOpenExistentialRefInst(const OpenExistentialRefInst *RHS) {
      return true;
    }

  private:
    const SILInstruction *LHS;
  };
} // end anonymous namespace

bool SILInstruction::hasIdenticalState(const SILInstruction *RHS) const {
  SILInstruction *UnconstRHS = const_cast<SILInstruction *>(RHS);
  return InstructionIdentityComparer(this).visit(UnconstRHS);
}

namespace {
  class AllOperandsAccessor : public SILInstructionVisitor<AllOperandsAccessor,
                                                           ArrayRef<Operand> > {
  public:
#define INST(CLASS, PARENT)                                                    \
  ArrayRef<Operand> visit##CLASS(const CLASS *I) {                             \
    ASSERT_IMPLEMENTS(CLASS, SILInstruction, getAllOperands,                   \
                      ArrayRef<Operand>() const);                              \
    return I->getAllOperands();                                                \
  }
#include "swift/SIL/SILNodes.def"
  };

  class AllOperandsMutableAccessor
    : public SILInstructionVisitor<AllOperandsMutableAccessor,
                                   MutableArrayRef<Operand> > {
  public:
#define INST(CLASS, PARENT)                                                    \
  MutableArrayRef<Operand> visit##CLASS(CLASS *I) {                            \
    ASSERT_IMPLEMENTS(CLASS, SILInstruction, getAllOperands,                   \
                      MutableArrayRef<Operand>());                             \
    return I->getAllOperands();                                                \
  }
#include "swift/SIL/SILNodes.def"
  };

#define IMPLEMENTS_METHOD(DerivedClass, BaseClass, MemberName, ExpectedType)  \
  (!::std::is_same<BaseClass, GET_IMPLEMENTING_CLASS(DerivedClass, MemberName,\
                                                     ExpectedType)>::value)

  class TypeDependentOperandsAccessor
      : public SILInstructionVisitor<TypeDependentOperandsAccessor,
                                     ArrayRef<Operand>> {
  public:
#define INST(CLASS, PARENT)                                                    \
  ArrayRef<Operand> visit##CLASS(const CLASS *I) {                             \
    if (!IMPLEMENTS_METHOD(CLASS, SILInstruction, getTypeDependentOperands,    \
                           ArrayRef<Operand>() const))                         \
      return {};                                                               \
    return I->getTypeDependentOperands();                                      \
  }
#include "swift/SIL/SILNodes.def"
  };

  class TypeDependentOperandsMutableAccessor
    : public SILInstructionVisitor<TypeDependentOperandsMutableAccessor,
                                   MutableArrayRef<Operand> > {
  public:
#define INST(CLASS, PARENT)                                                    \
  MutableArrayRef<Operand> visit##CLASS(CLASS *I) {                            \
    if (!IMPLEMENTS_METHOD(CLASS, SILInstruction, getTypeDependentOperands,    \
                           MutableArrayRef<Operand>()))                        \
      return {};                                                               \
    return I->getTypeDependentOperands();                                      \
  }
#include "swift/SIL/SILNodes.def"
  };
} // end anonymous namespace

ArrayRef<Operand> SILInstruction::getAllOperands() const {
  return AllOperandsAccessor().visit(const_cast<SILInstruction *>(this));
}

MutableArrayRef<Operand> SILInstruction::getAllOperands() {
  return AllOperandsMutableAccessor().visit(this);
}

ArrayRef<Operand> SILInstruction::getTypeDependentOperands() const {
  return TypeDependentOperandsAccessor().visit(
      const_cast<SILInstruction *>(this));
}

MutableArrayRef<Operand> SILInstruction::getTypeDependentOperands() {
  return TypeDependentOperandsMutableAccessor().visit(this);
}

/// getOperandNumber - Return which operand this is in the operand list of the
/// using instruction.
unsigned Operand::getOperandNumber() const {
  return this - &cast<SILInstruction>(getUser())->getAllOperands()[0];
}

SILInstruction::MemoryBehavior SILInstruction::getMemoryBehavior() const {

  if (auto *BI = dyn_cast<BuiltinInst>(this)) {
    // Handle Swift builtin functions.
    const BuiltinInfo &BInfo = BI->getBuiltinInfo();
    if (BInfo.ID != BuiltinValueKind::None)
      return BInfo.isReadNone() ? MemoryBehavior::None
                                : MemoryBehavior::MayHaveSideEffects;

    // Handle LLVM intrinsic functions.
    const IntrinsicInfo &IInfo = BI->getIntrinsicInfo();
    if (IInfo.ID != llvm::Intrinsic::not_intrinsic) {
      // Read-only.
      if (IInfo.hasAttribute(llvm::Attribute::ReadOnly) &&
          IInfo.hasAttribute(llvm::Attribute::NoUnwind))
        return MemoryBehavior::MayRead;
      // Read-none?
      return IInfo.hasAttribute(llvm::Attribute::ReadNone) &&
                     IInfo.hasAttribute(llvm::Attribute::NoUnwind)
                 ? MemoryBehavior::None
                 : MemoryBehavior::MayHaveSideEffects;
    }
  }

  // Handle full apply sites that have a resolvable callee function with an
  // effects attribute.
  if (isa<FullApplySite>(this)) {
    FullApplySite Site(const_cast<SILInstruction *>(this));
    if (auto *F = Site.getCalleeFunction()) {
      return F->getEffectsKind() == EffectsKind::ReadNone
                 ? MemoryBehavior::None
                 : MemoryBehavior::MayHaveSideEffects;
    }
  }

  switch (getKind()) {
#define FULL_INST(CLASS, TEXTUALNAME, PARENT, MEMBEHAVIOR, RELEASINGBEHAVIOR)  \
  case SILInstructionKind::CLASS:                                              \
    return MemoryBehavior::MEMBEHAVIOR;
#include "swift/SIL/SILNodes.def"
  }
  llvm_unreachable("We've just exhausted the switch.");
}

SILInstruction::ReleasingBehavior SILInstruction::getReleasingBehavior() const {
  switch (getKind()) {
#define FULL_INST(CLASS, TEXTUALNAME, PARENT, MEMBEHAVIOR, RELEASINGBEHAVIOR)  \
  case SILInstructionKind::CLASS:                                              \
    return ReleasingBehavior::RELEASINGBEHAVIOR;
#include "swift/SIL/SILNodes.def"
  }
  llvm_unreachable("We've just exhausted the switch.");
}

bool SILInstruction::mayHaveSideEffects() const {
  // If this instruction traps then it must have side effects.
  if (mayTrap())
    return true;

  MemoryBehavior B = getMemoryBehavior();
  return B == MemoryBehavior::MayWrite ||
    B == MemoryBehavior::MayReadWrite ||
    B == MemoryBehavior::MayHaveSideEffects;
}

bool SILInstruction::mayRelease() const {
  if (getReleasingBehavior() ==
      SILInstruction::ReleasingBehavior::DoesNotRelease)
    return false;

  switch (getKind()) {
  default:
    llvm_unreachable("Unhandled releasing instruction!");

  case SILInstructionKind::ApplyInst:
  case SILInstructionKind::TryApplyInst:
  case SILInstructionKind::DestroyAddrInst:
  case SILInstructionKind::StrongReleaseInst:
  case SILInstructionKind::UnownedReleaseInst:
  case SILInstructionKind::ReleaseValueInst:
  case SILInstructionKind::ReleaseValueAddrInst:
    return true;

  case SILInstructionKind::DestroyValueInst:
    assert(!SILModuleConventions(getModule()).useLoweredAddresses());
    return true;

  case SILInstructionKind::UnconditionalCheckedCastAddrInst:
  case SILInstructionKind::UnconditionalCheckedCastValueInst:
    return true;

  case SILInstructionKind::CheckedCastAddrBranchInst: {
    // Failing casts with take_always can release.
    auto *Cast = cast<CheckedCastAddrBranchInst>(this);
    return Cast->getConsumptionKind() == CastConsumptionKind::TakeAlways;
  }

  case SILInstructionKind::CopyAddrInst: {
    auto *CopyAddr = cast<CopyAddrInst>(this);
    // copy_addr without initialization can cause a release.
    return CopyAddr->isInitializationOfDest() ==
           IsInitialization_t::IsNotInitialization;
  }

  case SILInstructionKind::BuiltinInst: {
    auto *BI = cast<BuiltinInst>(this);
    // Builtins without side effects also do not release.
    if (!BI->mayHaveSideEffects())
      return false;
    // If this is a builtin which might have side effect, but its side
    // effects do not cause reference counts to be decremented, return false.
    if (auto Kind = BI->getBuiltinKind()) {
      switch (Kind.getValue()) {
        case BuiltinValueKind::CopyArray:
          return false;
        default:
          break;
      }
    }
    if (auto ID = BI->getIntrinsicID()) {
      switch (ID.getValue()) {
        case llvm::Intrinsic::memcpy:
        case llvm::Intrinsic::memmove:
        case llvm::Intrinsic::memset:
          return false;
        default:
          break;
      }
    }
    return true;
  }
  }
}

bool SILInstruction::mayReleaseOrReadRefCount() const {
  switch (getKind()) {
  case SILInstructionKind::IsUniqueInst:
  case SILInstructionKind::IsUniqueOrPinnedInst:
    return true;
  default:
    return mayRelease();
  }
}

namespace {
  class TrivialCloner : public SILCloner<TrivialCloner> {
    friend class SILCloner<TrivialCloner>;
    friend class SILInstructionVisitor<TrivialCloner>;
    SILInstruction *Result = nullptr;
    TrivialCloner(SILFunction *F) : SILCloner(*F) {}
  public:

    static SILInstruction *doIt(SILInstruction *I) {
      TrivialCloner TC(I->getFunction());
      TC.visit(I);
      return TC.Result;
    }

    void postProcess(SILInstruction *Orig, SILInstruction *Cloned) {
      assert(Orig->getFunction() == &getBuilder().getFunction() &&
             "cloning between functions is not supported");

      Result = Cloned;
      SILCloner<TrivialCloner>::postProcess(Orig, Cloned);
    }
    SILValue remapValue(SILValue Value) {
      return Value;
    }
    SILBasicBlock *remapBasicBlock(SILBasicBlock *BB) { return BB; }
  };
} // end anonymous namespace

bool SILInstruction::isAllocatingStack() const {
  if (isa<AllocStackInst>(this))
    return true;

  if (auto *ARI = dyn_cast<AllocRefInst>(this)) {
    if (ARI->canAllocOnStack())
      return true;
  }
  return false;
}

bool SILInstruction::isDeallocatingStack() const {
  if (isa<DeallocStackInst>(this))
    return true;

  if (auto *DRI = dyn_cast<DeallocRefInst>(this)) {
    if (DRI->canAllocOnStack())
      return true;
  }
  return false;
}


/// Create a new copy of this instruction, which retains all of the operands
/// and other information of this one.  If an insertion point is specified,
/// then the new instruction is inserted before the specified point, otherwise
/// the new instruction is returned without a parent.
SILInstruction *SILInstruction::clone(SILInstruction *InsertPt) {
  SILInstruction *NewInst = TrivialCloner::doIt(this);

  if (NewInst && InsertPt)
    InsertPt->getParent()->insert(InsertPt, NewInst);
  return NewInst;
}

/// Returns true if the instruction can be duplicated without any special
/// additional handling. It is important to know this information when
/// you perform such optimizations like e.g. jump-threading.
bool SILInstruction::isTriviallyDuplicatable() const {
  if (isa<ThrowInst>(this))
    return false;

  if (isa<AllocStackInst>(this) || isa<DeallocStackInst>(this)) {
    return false;
  }
  if (auto *ARI = dyn_cast<AllocRefInst>(this)) {
    if (ARI->canAllocOnStack())
      return false;
  }

  if (isa<OpenExistentialAddrInst>(this) || isa<OpenExistentialRefInst>(this) ||
      isa<OpenExistentialMetatypeInst>(this) ||
      isa<OpenExistentialValueInst>(this) || isa<OpenExistentialBoxInst>(this) ||
      isa<OpenExistentialBoxValueInst>(this)) {
    // Don't know how to duplicate these properly yet. Inst.clone() per
    // instruction does not work. Because the follow-up instructions need to
    // reuse the same archetype uuid which would only work if we used a
    // cloner.
    return false;
  }

  if (auto *MI = dyn_cast<MethodInst>(this)) {
    // We can't build SSA for method values that lower to objc methods.
    if (MI->getMember().isForeign)
      return false;
  }

  return true;
}

bool SILInstruction::mayTrap() const {
  switch(getKind()) {
  case SILInstructionKind::CondFailInst:
  case SILInstructionKind::UnconditionalCheckedCastInst:
  case SILInstructionKind::UnconditionalCheckedCastAddrInst:
    return true;
  default:
    return false;
  }
}

//===----------------------------------------------------------------------===//
//                                 Utilities
//===----------------------------------------------------------------------===//

llvm::raw_ostream &swift::operator<<(llvm::raw_ostream &OS,
                                     SILInstruction::MemoryBehavior B) {
  switch (B) {
    case SILInstruction::MemoryBehavior::None:
      return OS << "None";
    case SILInstruction::MemoryBehavior::MayRead:
      return OS << "MayRead";
    case SILInstruction::MemoryBehavior::MayWrite:
      return OS << "MayWrite";
    case SILInstruction::MemoryBehavior::MayReadWrite:
      return OS << "MayReadWrite";
    case SILInstruction::MemoryBehavior::MayHaveSideEffects:
      return OS << "MayHaveSideEffects";
  }

  llvm_unreachable("Unhandled MemoryBehavior in switch.");
}

llvm::raw_ostream &swift::operator<<(llvm::raw_ostream &OS,
                                     SILInstruction::ReleasingBehavior B) {
  switch (B) {
  case SILInstruction::ReleasingBehavior::DoesNotRelease:
    return OS << "DoesNotRelease";
  case SILInstruction::ReleasingBehavior::MayRelease:
    return OS << "MayRelease";
  }

  llvm_unreachable("Unhandled ReleasingBehavior in switch.");
}

//===----------------------------------------------------------------------===//
//                         SILInstructionResultArray
//===----------------------------------------------------------------------===//

SILInstructionResultArray::SILInstructionResultArray(
    const SingleValueInstruction *SVI)
    : Pointer(), Size(1) {
  // Make sure that even though we are munging things, we are able to get back
  // the original value, types, and operands.
  SILValue originalValue(SVI);
  SILType originalType = SVI->getType();
  (void)originalValue;
  (void)originalType;

  // *PLEASE READ BEFORE CHANGING*
  //
  // Since SingleValueInstruction is both a ValueBase and a SILInstruction, but
  // SILInstruction is the first parent, we need to ensure that our ValueBase *
  // pointer is properly offset. by first static casting to ValueBase and then
  // going back to a uint8_t *.
  auto *Value = static_cast<const ValueBase *>(SVI);
  assert(uintptr_t(Value) != uintptr_t(SVI) &&
         "Expected value to be offset from SVI since it is not the first "
         "multi-inheritence parent");
  Pointer = reinterpret_cast<const uint8_t *>(Value);

#ifndef NDEBUG
  assert(originalValue == (*this)[0] &&
         "Wrong value returned for single result");
  assert(originalType == (*this)[0]->getType());

  auto ValueRange = getValues();
  assert(1 == std::distance(ValueRange.begin(), ValueRange.end()));
  assert(originalValue == *ValueRange.begin());

  auto TypedRange = getTypes();
  assert(1 == std::distance(TypedRange.begin(), TypedRange.end()));
  assert(originalType == *TypedRange.begin());

  SILInstructionResultArray Copy = *this;
  assert(Copy.hasSameTypes(*this));
  assert(Copy == *this);
#endif
}

SILInstructionResultArray::SILInstructionResultArray(
    ArrayRef<MultipleValueInstructionResult> MVResults)
    : Pointer(nullptr), Size(MVResults.size()) {
  // We are assuming here that MultipleValueInstructionResult when static_cast
  // is not offset.
  if (Size)
    Pointer = reinterpret_cast<const uint8_t *>(&MVResults[0]);

#ifndef NDEBUG
  // Verify our invariants.
  assert(size() == MVResults.size());
  auto ValueRange = getValues();
  auto VRangeBegin = ValueRange.begin();
  auto VRangeIter = VRangeBegin;
  auto VRangeEnd = ValueRange.end();
  assert(MVResults.size() == unsigned(std::distance(VRangeBegin, VRangeEnd)));

  auto TypedRange = getTypes();
  auto TRangeBegin = TypedRange.begin();
  auto TRangeIter = TRangeBegin;
  auto TRangeEnd = TypedRange.end();
  assert(MVResults.size() == unsigned(std::distance(VRangeBegin, VRangeEnd)));
  for (unsigned i : indices(MVResults)) {
    assert(SILValue(&MVResults[i]) == (*this)[i]);
    assert(SILValue(&MVResults[i])->getType() == (*this)[i]->getType());
    assert(SILValue(&MVResults[i]) == (*VRangeIter));
    assert(SILValue(&MVResults[i])->getType() == (*VRangeIter)->getType());
    assert(SILValue(&MVResults[i])->getType() == *TRangeIter);
    ++VRangeIter;
    ++TRangeIter;
  }

  SILInstructionResultArray Copy = *this;
  assert(Copy.hasSameTypes(*this));
  assert(Copy == *this);
#endif
}

SILValue SILInstructionResultArray::operator[](size_t Index) const {
  assert(Index < Size && "Index out of bounds");
  // *NOTE* In the case where we have a single instruction, Index will always
  // necessarily be 0 implying that it is safe for us to just multiple Index by
  // sizeof(MultipleValueInstructionResult).
  size_t Offset = sizeof(MultipleValueInstructionResult) * Index;
  return SILValue(reinterpret_cast<const ValueBase *>(&Pointer[Offset]));
}

bool SILInstructionResultArray::hasSameTypes(
    const SILInstructionResultArray &rhs) {
  auto &lhs = *this;
  if (lhs.size() != rhs.size())
    return false;
  for (unsigned i : indices(lhs)) {
    if (lhs[i]->getType() != rhs[i]->getType())
      return false;
  }
  return true;
}

bool SILInstructionResultArray::
operator==(const SILInstructionResultArray &other) {
  if (size() != other.size())
    return false;
  for (auto i : indices(*this))
    if ((*this)[i] != other[i])
      return false;
  return true;
}

SILInstructionResultArray::type_range
SILInstructionResultArray::getTypes() const {
  std::function<SILType(SILValue)> F = [](SILValue V) -> SILType {
    return V->getType();
  };
  return {llvm::map_iterator(begin(), F), llvm::map_iterator(end(), F)};
}

SILInstructionResultArray::iterator SILInstructionResultArray::begin() const {
  return iterator(*this, getStartOffset());
}

SILInstructionResultArray::iterator SILInstructionResultArray::end() const {
  return iterator(*this, getEndOffset());
}

SILInstructionResultArray::reverse_iterator
SILInstructionResultArray::rbegin() const {
  return llvm::make_reverse_iterator(end());
}

SILInstructionResultArray::reverse_iterator
SILInstructionResultArray::rend() const {
  return llvm::make_reverse_iterator(begin());
}

SILInstructionResultArray::range SILInstructionResultArray::getValues() const {
  return {begin(), end()};
}

SILInstructionResultArray::reverse_range
SILInstructionResultArray::getReversedValues() const {
  return {rbegin(), rend()};
}

const ValueBase *SILInstructionResultArray::front() const {
  assert(size() && "Can not access front of an empty result array");
  return *begin();
}

const ValueBase *SILInstructionResultArray::back() const {
  assert(size() && "Can not access back of an empty result array");
  if (std::next(begin()) == end()) {
    return *begin();
  }
  return *std::prev(end());
}

//===----------------------------------------------------------------------===//
//                         Multiple Value Instruction
//===----------------------------------------------------------------------===//

Optional<unsigned>
MultipleValueInstruction::getIndexOfResult(SILValue Target) const {
  // First make sure we actually have one of our instruction results.
  auto *MVIR = dyn_cast<MultipleValueInstructionResult>(Target);
  if (!MVIR || MVIR->getParent() != this)
    return None;
  return MVIR->getIndex();
}

MultipleValueInstructionResult::MultipleValueInstructionResult(
    ValueKind valueKind, unsigned index, SILType type,
    ValueOwnershipKind ownershipKind)
    : ValueBase(valueKind, type, IsRepresentative::No) {
  setOwnershipKind(ownershipKind);
  setIndex(index);
}

void MultipleValueInstructionResult::setOwnershipKind(
    ValueOwnershipKind NewKind) {
  // Get the original data, merge in our new lower values and then reset the
  // value in our data.
  //
  // *NOTE* This is a two phase set so if this code ever needs to be used in a
  // concurrent context, a this will need to be updated.
  uint64_t OriginalData = getSubclassData();
  uint64_t NewData =
      (OriginalData & ~ValueOwnershipKind::Mask) | uint64_t(NewKind);
  setSubclassData(NewData);
}

void MultipleValueInstructionResult::setIndex(unsigned NewIndex) {
  // We only take the last 3 bytes for simplicity. If more bits are needed at
  // some point, we can take 5 bits we are burning here and combine them with
  // 6 bits from SILType, Operand to get more storage. But to do so now would
  // be premature optimization since we could potentially use those bits for
  // flags. 500k fields is probably enough.
  assert(NewIndex < (1 << NumIndexBits) && "Unrepresentable index");
  uint64_t OriginalData = getSubclassData();
  uint64_t NewData = (OriginalData & ~(IndexMask << IndexBitOffset)) |
                     (NewIndex << IndexBitOffset);
  setSubclassData(NewData);
}

ValueOwnershipKind MultipleValueInstructionResult::getOwnershipKind() const {
  uint64_t Data = getSubclassData() & ValueOwnershipKind::Mask;
  return ValueOwnershipKind(Data);
}

MultipleValueInstruction *MultipleValueInstructionResult::getParent() {
  char *Ptr = reinterpret_cast<char *>(
      const_cast<MultipleValueInstructionResult *>(this));

  // We know that we are in a trailing objects array with an extra prefix
  // element that contains the pointer to our parent SILNode. So grab the
  // address of the beginning of the array.
  Ptr -= getIndex() * sizeof(MultipleValueInstructionResult);

  // We may have some bytes of padding depending on our platform. Move past
  // those bytes if we need to.
  static_assert(alignof(MultipleValueInstructionResult) >=
                    alignof(MultipleValueInstruction *),
                "We assume this relationship in between the alignments");
  Ptr -= alignof(MultipleValueInstructionResult) -
         alignof(MultipleValueInstruction *);

  // Then subtract the size of MultipleValueInstruction.
  Ptr -= sizeof(MultipleValueInstruction *);

  // Now that we have the correct address of our parent instruction, grab it and
  // return it avoiding type punning.
  uintptr_t value;
  memcpy(&value, Ptr, sizeof(value));
  return reinterpret_cast<MultipleValueInstruction *>(value);
}

#ifndef NDEBUG

//---
// Static verification of multiple value properties.
//

// Make sure that all subclasses of MultipleValueInstruction implement
// getAllResults()
#define MULTIPLE_VALUE_INST(ID, TEXTUALNAME, PARENT, MEMBEHAVIOR, MAYRELEASE)  \
  static_assert(IMPLEMENTS_METHOD(ID, PARENT, getAllResults,                   \
                                  SILInstructionResultArray() const),          \
                #ID " does not implement SILInstructionResultArray "           \
                    "getAllResults() const?!");

// Check that all subclasses of MultipleValueInstructionResult are the same size
// as MultipleValueInstructionResult.
//
// If this changes, we just need to expand the size fo SILInstructionResultArray
// to contain a stride. But we assume this now so we should enforce it.
#define MULTIPLE_VALUE_INST_RESULT(ID, PARENT)                                 \
  static_assert(                                                               \
      sizeof(ID) == sizeof(PARENT) && alignof(ID) == alignof(PARENT),          \
      "Expected all multiple value inst result to be the same size?!");
#include "swift/SIL/SILNodes.def"

#endif
