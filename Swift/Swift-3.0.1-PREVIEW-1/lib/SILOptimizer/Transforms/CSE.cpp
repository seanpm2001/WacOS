//===--- CSE.cpp - Simple and fast CSE pass -------------------------------===//
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
// This pass performs a simple dominator tree walk that eliminates trivially
// redundant instructions.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "sil-cse"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/SIL/Dominance.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILOpenedArchetypesTracker.h"
#include "swift/SIL/SILType.h"
#include "swift/SIL/SILValue.h"
#include "swift/SIL/SILVisitor.h"
#include "swift/SIL/DebugUtils.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/SILOptimizer/Analysis/ArraySemantic.h"
#include "swift/SILOptimizer/Analysis/DominanceAnalysis.h"
#include "swift/SILOptimizer/Analysis/SimplifyInstruction.h"
#include "swift/SILOptimizer/Analysis/SideEffectAnalysis.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/RecyclingAllocator.h"

STATISTIC(NumOpenExtRemoved,
          "Number of open_existential_addr instructions removed");

STATISTIC(NumSimplify, "Number of instructions simplified or DCE'd");
STATISTIC(NumCSE,      "Number of instructions CSE'd");

using namespace swift;

//===----------------------------------------------------------------------===//
//                                Simple Value
//===----------------------------------------------------------------------===//

namespace {
/// SimpleValue - Instances of this struct represent available values in the
/// scoped hash table.
struct SimpleValue {
  SILInstruction *Inst;

  SimpleValue(SILInstruction *I) : Inst(I) { }

  bool isSentinel() const {
    return Inst == llvm::DenseMapInfo<SILInstruction *>::getEmptyKey() ||
           Inst == llvm::DenseMapInfo<SILInstruction *>::getTombstoneKey();
  }
};
} // end anonymous namespace

namespace llvm {
template <> struct DenseMapInfo<SimpleValue> {
  static inline SimpleValue getEmptyKey() {
    return DenseMapInfo<SILInstruction *>::getEmptyKey();
  }
  static inline SimpleValue getTombstoneKey() {
    return DenseMapInfo<SILInstruction *>::getTombstoneKey();
  }
  static unsigned getHashValue(SimpleValue Val);
  static bool isEqual(SimpleValue LHS, SimpleValue RHS);
};
} // end namespace llvm

namespace {
class HashVisitor : public SILInstructionVisitor<HashVisitor, llvm::hash_code> {
  using hash_code = llvm::hash_code;

public:
  hash_code visitValueBase(ValueBase *) {
    llvm_unreachable("No hash implemented for the given type");
  }

  hash_code visitBridgeObjectToRefInst(BridgeObjectToRefInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getOperand());
  }

  hash_code visitBridgeObjectToWordInst(BridgeObjectToWordInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getOperand());
  }
  
  hash_code visitRefToBridgeObjectInst(RefToBridgeObjectInst *X) {
    OperandValueArrayRef Operands(X->getAllOperands());
    return llvm::hash_combine(
        X->getKind(), X->getType(),
        llvm::hash_combine_range(Operands.begin(), Operands.end()));
  }

  hash_code visitUncheckedTrivialBitCastInst(UncheckedTrivialBitCastInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getOperand());
  }

  hash_code visitUncheckedBitwiseCastInst(UncheckedBitwiseCastInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getOperand());
  }

  hash_code visitUncheckedAddrCastInst(UncheckedAddrCastInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getOperand());
  }

  hash_code visitFunctionRefInst(FunctionRefInst *X) {
    return llvm::hash_combine(X->getKind(), X->getReferencedFunction());
  }

  hash_code visitGlobalAddrInst(GlobalAddrInst *X) {
    return llvm::hash_combine(X->getKind(), X->getReferencedGlobal());
  }

  hash_code visitIntegerLiteralInst(IntegerLiteralInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getValue());
  }

  hash_code visitFloatLiteralInst(FloatLiteralInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getBits());
  }

  hash_code visitRefElementAddrInst(RefElementAddrInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand(), X->getField());
  }

  hash_code visitProjectBoxInst(ProjectBoxInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand());
  }

  hash_code visitRefToRawPointerInst(RefToRawPointerInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand());
  }

  hash_code visitRawPointerToRefInst(RawPointerToRefInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand());
  }

  hash_code visitUnownedToRefInst(UnownedToRefInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand());
  }

  hash_code visitRefToUnownedInst(RefToUnownedInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand());
  }

  hash_code visitUnmanagedToRefInst(UnmanagedToRefInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand());
  }

  hash_code visitRefToUnmanagedInst(RefToUnmanagedInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand());
  }

  hash_code visitUpcastInst(UpcastInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getOperand());
  }

  hash_code visitStringLiteralInst(StringLiteralInst *X) {
    return llvm::hash_combine(X->getKind(), X->getEncoding(), X->getValue());
  }

  hash_code visitStructInst(StructInst *X) {
    // This is safe since we are hashing the operands using the actual pointer
    // values of the values being used by the operand.
    OperandValueArrayRef Operands(X->getAllOperands());
    return llvm::hash_combine(X->getKind(), X->getStructDecl(),
      llvm::hash_combine_range(Operands.begin(), Operands.end()));
  }

  hash_code visitStructExtractInst(StructExtractInst *X) {
    return llvm::hash_combine(X->getKind(), X->getStructDecl(), X->getField(),
                              X->getOperand());
  }

  hash_code visitStructElementAddrInst(StructElementAddrInst *X) {
    return llvm::hash_combine(X->getKind(), X->getStructDecl(), X->getField(),
                              X->getOperand());
  }

  hash_code visitCondFailInst(CondFailInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand());
  }

  hash_code visitClassMethodInst(ClassMethodInst *X) {
    return llvm::hash_combine(X->getKind(),
                              X->getType(),
                              X->getOperand());
  }

  hash_code visitTupleInst(TupleInst *X) {
    OperandValueArrayRef Operands(X->getAllOperands());
    return llvm::hash_combine(X->getKind(), X->getTupleType(),
      llvm::hash_combine_range(Operands.begin(), Operands.end()));
  }

  hash_code visitTupleExtractInst(TupleExtractInst *X) {
    return llvm::hash_combine(X->getKind(), X->getTupleType(), X->getFieldNo(),
                              X->getOperand());
  }

  hash_code visitTupleElementAddrInst(TupleElementAddrInst *X) {
    return llvm::hash_combine(X->getKind(), X->getTupleType(), X->getFieldNo(),
                              X->getOperand());
  }

  hash_code visitMetatypeInst(MetatypeInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType());
  }

  hash_code visitValueMetatypeInst(ValueMetatypeInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getOperand());
  }

  hash_code visitExistentialMetatypeInst(ExistentialMetatypeInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType());
  }

  hash_code visitObjCProtocolInst(ObjCProtocolInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getProtocol());
  }

  hash_code visitIndexRawPointerInst(IndexRawPointerInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getBase(),
                              X->getIndex());
  }

  hash_code visitPointerToAddressInst(PointerToAddressInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getOperand(),
                              X->isStrict());
  }

  hash_code visitAddressToPointerInst(AddressToPointerInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getOperand());
  }

  hash_code visitApplyInst(ApplyInst *X) {
    OperandValueArrayRef Operands(X->getAllOperands());
    return llvm::hash_combine(X->getKind(), X->getCallee(),
                              llvm::hash_combine_range(Operands.begin(),
                                                       Operands.end()),
                              X->hasSubstitutions());
  }

  hash_code visitBuiltinInst(BuiltinInst *X) {
    OperandValueArrayRef Operands(X->getAllOperands());
    return llvm::hash_combine(X->getKind(), X->getName().get(),
                              llvm::hash_combine_range(Operands.begin(),
                                                       Operands.end()),
                              X->hasSubstitutions());
  }
  
  hash_code visitEnumInst(EnumInst *X) {
    // We hash the enum by hashing its kind, element, and operand if it has one.
    if (!X->hasOperand())
      return llvm::hash_combine(X->getKind(), X->getElement());
    return llvm::hash_combine(X->getKind(), X->getElement(), X->getOperand());
  }

  hash_code visitUncheckedEnumDataInst(UncheckedEnumDataInst *X) {
    // We hash the enum by hashing its kind, element, and operand.
    return llvm::hash_combine(X->getKind(), X->getElement(), X->getOperand());
  }

  hash_code visitIndexAddrInst(IndexAddrInst *X) {
    return llvm::hash_combine(X->getKind(), X->getType(), X->getBase(),
                              X->getIndex());
  }

  hash_code visitThickToObjCMetatypeInst(ThickToObjCMetatypeInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand(), X->getType());
  }

  hash_code visitObjCToThickMetatypeInst(ObjCToThickMetatypeInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand(), X->getType());
  }

  hash_code visitObjCMetatypeToObjectInst(ObjCMetatypeToObjectInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand(), X->getType());
  }

  hash_code visitObjCExistentialMetatypeToObjectInst(
      ObjCExistentialMetatypeToObjectInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand(), X->getType());
  }

  hash_code visitUncheckedRefCastInst(UncheckedRefCastInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand(), X->getType());
  }

  hash_code visitSelectEnumInstBase(SelectEnumInstBase *X) {
    auto hash = llvm::hash_combine(X->getKind(),
                                   X->getEnumOperand(),
                                   X->getType(),
                                   X->hasDefault());
    
    for (unsigned i = 0, e = X->getNumCases(); i < e; ++i) {
      hash = llvm::hash_combine(hash, X->getCase(i).first,
                                X->getCase(i).second);
    }
    
    if (X->hasDefault())
      hash = llvm::hash_combine(hash, X->getDefaultResult());
    
    return hash;
  }
  
  hash_code visitSelectEnumInst(SelectEnumInst *X) {
    return visitSelectEnumInstBase(X);
  }

  hash_code visitSelectEnumAddrInst(SelectEnumAddrInst *X) {
    return visitSelectEnumInstBase(X);
  }

  hash_code visitSelectValueInst(SelectValueInst *X) {
    auto hash = llvm::hash_combine(X->getKind(),
                                   X->getOperand(),
                                   X->getType(),
                                   X->hasDefault());

    for (unsigned i = 0, e = X->getNumCases(); i < e; ++i) {
      hash = llvm::hash_combine(hash, X->getCase(i).first,
                                X->getCase(i).second);
    }

    if (X->hasDefault())
      hash = llvm::hash_combine(hash, X->getDefaultResult());

    return hash;
  }

  hash_code visitIsNonnullInst(IsNonnullInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand(), X->getType());
  }

  hash_code visitThinFunctionToPointerInst(ThinFunctionToPointerInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand(), X->getType());
  }

  hash_code visitPointerToThinFunctionInst(PointerToThinFunctionInst *X) {
    return llvm::hash_combine(X->getKind(), X->getOperand(), X->getType());
  }

  hash_code visitWitnessMethodInst(WitnessMethodInst *X) {
    OperandValueArrayRef Operands(X->getAllOperands());
    return llvm::hash_combine(X->getKind(),
                              X->getLookupType().getPointer(),
                              X->getMember().getHashCode(),
                              X->getConformance(),
                              X->getType(),
                              !X->getOpenedArchetypeOperands().empty(),
                              llvm::hash_combine_range(
                              Operands.begin(),
                              Operands.end()));
  }

  hash_code visitMarkDependenceInst(MarkDependenceInst *X) {
    OperandValueArrayRef Operands(X->getAllOperands());
    return llvm::hash_combine(
        X->getKind(), X->getType(),
        llvm::hash_combine_range(Operands.begin(), Operands.end()));
  }

  hash_code visitOpenExistentialRefInst(OpenExistentialRefInst *X) {
    auto ArchetypeTy = cast<ArchetypeType>(X->getType().getSwiftRValueType());
    auto ConformsTo = ArchetypeTy->getConformsTo();
    return llvm::hash_combine(
        X->getKind(), X->getOperand(),
        llvm::hash_combine_range(ConformsTo.begin(), ConformsTo.end()));
  }
};
} // end anonymous namespace

unsigned llvm::DenseMapInfo<SimpleValue>::getHashValue(SimpleValue Val) {
  return HashVisitor().visit(Val.Inst);
}

bool llvm::DenseMapInfo<SimpleValue>::isEqual(SimpleValue LHS,
                                              SimpleValue RHS) {
  SILInstruction *LHSI = LHS.Inst, *RHSI = RHS.Inst;
  if (LHS.isSentinel() || RHS.isSentinel())
    return LHSI == RHSI;

  if (isa<OpenExistentialRefInst>(LHSI) && isa<OpenExistentialRefInst>(RHSI)) {
    if (LHSI->getNumOperands() != RHSI->getNumOperands())
      return false;

    // Check operands.
    for (unsigned i = 0, e = LHSI->getNumOperands(); i != e; ++i)
      if (LHSI->getOperand(i) !=  RHSI->getOperand(i))
        return false;

    // Consider the types of two open_existential_ref instructions to be equal,
    // if the sets of protocols they conform to are equal.
    auto LHSArchetypeTy =
        cast<ArchetypeType>(LHSI->getType().getSwiftRValueType());
    auto LHSConformsTo = LHSArchetypeTy->getConformsTo();
    auto RHSArchetypeTy =
        cast<ArchetypeType>(RHSI->getType().getSwiftRValueType());
    auto RHSConformsTo = RHSArchetypeTy->getConformsTo();
    return LHSConformsTo == RHSConformsTo;
  }
  return LHSI->getKind() == RHSI->getKind() && LHSI->isIdenticalTo(RHSI);
}

//===----------------------------------------------------------------------===//
//                               CSE Interface
//===----------------------------------------------------------------------===//

namespace swift {

/// CSE - This pass does a simple depth-first walk over the dominator tree,
/// eliminating trivially redundant instructions and using simplifyInstruction
/// to canonicalize things as it goes. It is intended to be fast and catch
/// obvious cases so that SILCombine and other passes are more effective.
class CSE {
public:
  typedef llvm::ScopedHashTableVal<SimpleValue, ValueBase *> SimpleValueHTType;
  typedef llvm::RecyclingAllocator<llvm::BumpPtrAllocator, SimpleValueHTType>
  AllocatorTy;
  typedef llvm::ScopedHashTable<SimpleValue, ValueBase *,
                                llvm::DenseMapInfo<SimpleValue>,
                                AllocatorTy> ScopedHTType;

  /// AvailableValues - This scoped hash table contains the current values of
  /// all of our simple scalar expressions.  As we walk down the domtree, we
  /// look to see if instructions are in this: if so, we replace them with what
  /// we find, otherwise we insert them so that dominated values can succeed in
  /// their lookup.
  ScopedHTType *AvailableValues;

  SideEffectAnalysis *SEA;

  CSE(bool RunsOnHighLevelSil, SideEffectAnalysis *SEA)
      : SEA(SEA), RunsOnHighLevelSil(RunsOnHighLevelSil) {}

  bool processFunction(SILFunction &F, DominanceInfo *DT);
  
  bool canHandle(SILInstruction *Inst);

private:
  
  /// True if CSE is done on high-level SIL, i.e. semantic calls are not inlined
  /// yet. In this case some semantic calls can be CSEd.
  bool RunsOnHighLevelSil;
  
  // NodeScope - almost a POD, but needs to call the constructors for the
  // scoped hash tables so that a new scope gets pushed on. These are RAII so
  // that the scope gets popped when the NodeScope is destroyed.
  class NodeScope {
   public:
    NodeScope(ScopedHTType *availableValues) : Scope(*availableValues) {}

   private:
    NodeScope(const NodeScope &) = delete;
    void operator=(const NodeScope &) = delete;

    ScopedHTType::ScopeTy Scope;
  };

  // StackNode - contains all the needed information to create a stack for doing
  // a depth first traversal of the tree. This includes scopes for values and
  // loads as well as the generation. There is a child iterator so that the
  // children do not need to be store separately.
  class StackNode {
   public:
    StackNode(ScopedHTType *availableValues, DominanceInfoNode *n,
              DominanceInfoNode::iterator child,
              DominanceInfoNode::iterator end)
        : Node(n), ChildIter(child), EndIter(end), Scopes(availableValues),
      Processed(false) {}

    // Accessors.
    DominanceInfoNode *node() { return Node; }
    DominanceInfoNode::iterator childIter() { return ChildIter; }
    DominanceInfoNode *nextChild() {
      DominanceInfoNode *child = *ChildIter;
      ++ChildIter;
      return child;
    }
    DominanceInfoNode::iterator end() { return EndIter; }
    bool isProcessed() { return Processed; }
    void process() { Processed = true; }

   private:
    StackNode(const StackNode &) = delete;
    void operator=(const StackNode &) = delete;

    // Members.
    DominanceInfoNode *Node;
    DominanceInfoNode::iterator ChildIter;
    DominanceInfoNode::iterator EndIter;
    NodeScope Scopes;
    bool Processed;
  };

  bool processNode(DominanceInfoNode *Node);
  bool processOpenExistentialRef(SILInstruction *Inst, ValueBase *V,
                                 SILBasicBlock::iterator &I);
};
}  // end anonymous namespace

//===----------------------------------------------------------------------===//
//                             CSE Implementation
//===----------------------------------------------------------------------===//

bool CSE::processFunction(SILFunction &Fm, DominanceInfo *DT) {
  std::vector<StackNode *> nodesToProcess;

  // Tables that the pass uses when walking the domtree.
  ScopedHTType AVTable;
  AvailableValues = &AVTable;

  bool Changed = false;

  // Process the root node.
  nodesToProcess.push_back(new StackNode(AvailableValues, DT->getRootNode(),
                  DT->getRootNode()->begin(),
                  DT->getRootNode()->end()));

  // Process the stack.
  while (!nodesToProcess.empty()) {
    // Grab the first item off the stack. Set the current generation, remove
    // the node from the stack, and process it.
    StackNode *NodeToProcess = nodesToProcess.back();

    // Check if the node needs to be processed.
    if (!NodeToProcess->isProcessed()) {
      // Process the node.
      Changed |= processNode(NodeToProcess->node());
      NodeToProcess->process();

    } else if (NodeToProcess->childIter() != NodeToProcess->end()) {
      // Push the next child onto the stack.
      DominanceInfoNode *child = NodeToProcess->nextChild();
      nodesToProcess.push_back(
          new StackNode(AvailableValues, child, child->begin(), child->end()));
    } else {
      // It has been processed, and there are no more children to process,
      // so delete it and pop it off the stack.
      delete NodeToProcess;
      nodesToProcess.pop_back();
    }
  } // while (!nodes...)

  return Changed;
}

namespace {
  // A very simple cloner for cloning instructions inside
  // the same function. The only interesting thing it does
  // is remapping the archetypes when it is required.
  class InstructionCloner : public SILCloner<InstructionCloner> {
    friend class SILCloner<InstructionCloner>;
    friend class SILVisitor<InstructionCloner>;
    SILInstruction *Result = nullptr;
  public:
    InstructionCloner(SILFunction *F) : SILCloner(*F) {}

    static SILInstruction *doIt(SILInstruction *I) {
      InstructionCloner TC(I->getFunction());
      return TC.clone(I);
    }

    SILInstruction *clone(SILInstruction *I) {
      visit(I);
      return Result;
    }

    void postProcess(SILInstruction *Orig, SILInstruction *Cloned) {
      assert(Orig->getFunction() == &getBuilder().getFunction() &&
             "cloning between functions is not supported");

      Result = Cloned;
      SILCloner<InstructionCloner>::postProcess(Orig, Cloned);
    }
    SILValue remapValue(SILValue Value) {
      return Value;
    }
    SILBasicBlock *remapBasicBlock(SILBasicBlock *BB) { return BB; }
  };
}

/// Update SIL basic block's arguments types which refer to opened
/// archetypes. Replace such types by performing type substitutions
/// according to the provided type substitution map.
static void updateBasicBlockArgTypes(SILBasicBlock *BB,
                                     TypeSubstitutionMap &TypeSubstMap) {
  // Check types of all BB arguments.
  for (auto &Arg : BB->getBBArgs()) {
    if (!Arg->getType().getSwiftRValueType()->hasOpenedExistential())
      continue;
    // Type of this BB argument uses an opened existential.
    // Try to apply substitutions to it and if it produces a different type,
    // use this type as new type of the BB argument.
    auto NewArgType = Arg->getType().subst(
        BB->getModule(), BB->getModule().getSwiftModule(), TypeSubstMap);
    if (NewArgType == Arg->getType())
      continue;
    // Replace the type of this BB argument. The type of a BBArg
    // can only be changed using replaceBBArg, if the BBArg has no uses.
    // So, make it look as if it has no uses.

    // First collect all uses, before changing the type.
    SmallVector<Operand *, 4> OriginalArgUses;
    for (auto *ArgUse : Arg->getUses()) {
      OriginalArgUses.push_back(ArgUse);
    }
    // Then replace all uses by an undef.
    Arg->replaceAllUsesWith(SILUndef::get(Arg->getType(), BB->getModule()));
    // Replace the type of the BB argument.
    BB->replaceBBArg(Arg->getIndex(), NewArgType, Arg->getDecl());
    // Restore all uses to refer to the BB argument with updated type.
    for (auto ArgUse : OriginalArgUses) {
      ArgUse->set(Arg);
    }
  }
}

/// Handle CSE of open_existential_ref instructions.
/// Returns true if uses of open_existential_ref can
/// be replaced by a dominating instruction.
/// \Inst is the open_existential_ref instruction
/// \V is the dominating open_existential_ref instruction
/// \I is the iterator referring to the current instruction.
bool CSE::processOpenExistentialRef(SILInstruction *Inst, ValueBase *V,
                                    SILBasicBlock::iterator &I) {
  assert(isa<OpenExistentialRefInst>(Inst));
  llvm::SmallSetVector<SILInstruction *, 16> Candidates;
  auto OldOpenedArchetype = getOpenedArchetypeOf(Inst);
  auto NewOpenedArchetype = getOpenedArchetypeOf(dyn_cast<SILInstruction>(V));
  TypeSubstitutionMap TypeSubstMap;
  TypeSubstMap[OldOpenedArchetype.getPointer()] = NewOpenedArchetype;
  // Collect all candidates that may contain opened archetypes
  // that need to be replaced.
  for (auto Use : Inst->getUses()) {
    auto User = Use->getUser();
    if (User->mayHaveOpenedArchetypeOperands()) {
      if (canHandle(User)) {
        auto It = AvailableValues->begin(User);
        if (It != AvailableValues->end()) {
          return false;
        }
      }
      Candidates.insert(User);
    }
    if (!isa<TermInst>(User))
      continue;
    // The current use of the opened archetype is a terminator instruction.
    // Check if any of the successor BBs uses this opened archetype in the
    // types of its basic block arguments. If this is the case, replace
    // those uses by the new opened archetype.
    auto Successors = User->getParent()->getSuccessorBlocks();
    for (auto Successor : Successors) {
      if (Successor->bbarg_empty())
        continue;
      // If a BB has any arguments, update their types if necessary.
      updateBasicBlockArgTypes(Successor, TypeSubstMap);
    }
  }
  // Now process candidates.
  // TODO: Move it to CSE instance to avoid recreating it every time?
  SILOpenedArchetypesTracker OpenedArchetypesTracker(*Inst->getFunction());
  // Register the new archetype to be used.
  OpenedArchetypesTracker.registerOpenedArchetypes(dyn_cast<SILInstruction>(V));
  // Use a cloner. It makes copying the instruction and remapping of
  // opened archetypes trivial.
  InstructionCloner Cloner(I->getFunction());
  Cloner.registerOpenedExistentialRemapping(
      OldOpenedArchetype->castTo<ArchetypeType>(), NewOpenedArchetype);
  auto &Builder = Cloner.getBuilder();
  Builder.setOpenedArchetypesTracker(&OpenedArchetypesTracker);

  // Now clone each candidate and replace the opened archetype
  // by a dominating one.
  for (auto Candidate : Candidates) {
    Builder.getOpenedArchetypes().addOpenedArchetypeOperands(
        Candidate->getOpenedArchetypeOperands());
    Builder.setInsertionPoint(Candidate);
    auto NewI = Cloner.clone(Candidate);
    Candidate->replaceAllUsesWith(NewI);
    if (I == Candidate->getIterator())
      I = NewI->getIterator();
    eraseFromParentWithDebugInsts(Candidate, I);
  }
  return true;
}

bool CSE::processNode(DominanceInfoNode *Node) {
  SILBasicBlock *BB = Node->getBlock();
  bool Changed = false;

  // See if any instructions in the block can be eliminated.  If so, do it.  If
  // not, add them to AvailableValues.
  for (SILBasicBlock::iterator I = BB->begin(), E = BB->end(); I != E;) {
    SILInstruction *Inst = &*I;
    ++I;

    DEBUG(llvm::dbgs() << "SILCSE VISITING: " << *Inst << "\n");

    // Dead instructions should just be removed.
    if (isInstructionTriviallyDead(Inst)) {
      DEBUG(llvm::dbgs() << "SILCSE DCE: " << *Inst << '\n');
      eraseFromParentWithDebugInsts(Inst, I);
      Changed = true;
      ++NumSimplify;
      continue;
    }

    // If the instruction can be simplified (e.g. X+0 = X) then replace it with
    // its simpler value.
    if (SILValue V = simplifyInstruction(Inst)) {
      DEBUG(llvm::dbgs() << "SILCSE SIMPLIFY: " << *Inst << "  to: " << *V
            << '\n');
      Inst->replaceAllUsesWith(V);
      Inst->eraseFromParent();
      Changed = true;
      ++NumSimplify;
      continue;
    }

    // If this is not a simple instruction that we can value number, skip it.
    if (!canHandle(Inst))
      continue;

    // If an instruction can be handled here, then it must also be handled
    // in isIdenticalTo, otherwise looking up a key in the map with fail to
    // match itself.
    assert(Inst->isIdenticalTo(Inst) &&
           "Inst must match itself for map to work");

    // Now that we know we have an instruction we understand see if the
    // instruction has an available value.  If so, use it.
    if (ValueBase *V = AvailableValues->lookup(Inst)) {
      DEBUG(llvm::dbgs() << "SILCSE CSE: " << *Inst << "  to: " << *V << '\n');
      // Instructions producing a new opened archetype need a special handling,
      // because replacing these instructions may require a replacement
      // of the opened archetype type operands in some of the uses.
      if (!isa<OpenExistentialRefInst>(Inst) ||
          processOpenExistentialRef(Inst, V, I)) {
        Inst->replaceAllUsesWith(V);
        Inst->eraseFromParent();
        Changed = true;
        ++NumCSE;
        continue;
      }
    }

    // Otherwise, just remember that this value is available.
    AvailableValues->insert(Inst, Inst);
    DEBUG(llvm::dbgs() << "SILCSE Adding to value table: " << *Inst << " -> "
                       << *Inst << "\n");
  }

  return Changed;
}

bool CSE::canHandle(SILInstruction *Inst) {
  if (auto *AI = dyn_cast<ApplyInst>(Inst)) {
    if (!AI->mayReadOrWriteMemory())
      return true;
    
    if (RunsOnHighLevelSil) {
      ArraySemanticsCall SemCall(AI);
      switch (SemCall.getKind()) {
        case ArrayCallKind::kGetCount:
        case ArrayCallKind::kGetCapacity:
        case ArrayCallKind::kCheckIndex:
        case ArrayCallKind::kCheckSubscript:
          if (SemCall.hasGuaranteedSelf()) {
            return true;
          }
          return false;
        default:
          return false;
      }
    }
    
    // We can CSE function calls which do not read or write memory and don't
    // have any other side effects.
    SideEffectAnalysis::FunctionEffects Effects;
    SEA->getEffects(Effects, AI);

    // Note that the function also may not contain any retains. And there are
    // functions which are read-none and have a retain, e.g. functions which
    // _convert_ a global_addr to a reference and retain it.
    auto MB = Effects.getMemBehavior(RetainObserveKind::ObserveRetains);
    if (MB == SILInstruction::MemoryBehavior::None)
      return true;
    
    return false;
  }
  if (auto *BI = dyn_cast<BuiltinInst>(Inst)) {
    // Although the onFastPath builtin has no side-effects we don't want to
    // (re-)move it.
    if (BI->getBuiltinInfo().ID == BuiltinValueKind::OnFastPath)
      return false;
    return !BI->mayReadOrWriteMemory();
  }
  if (auto *CMI = dyn_cast<ClassMethodInst>(Inst)) {
    return !CMI->isVolatile();
  }
  if (auto *WMI = dyn_cast<WitnessMethodInst>(Inst)) {
    return !WMI->isVolatile();
  }
  if (auto *EMI = dyn_cast<ExistentialMetatypeInst>(Inst)) {
    return !EMI->getOperand()->getType().isAddress();
  }
  switch (Inst->getKind()) {
    case ValueKind::FunctionRefInst:
    case ValueKind::GlobalAddrInst:
    case ValueKind::IntegerLiteralInst:
    case ValueKind::FloatLiteralInst:
    case ValueKind::StringLiteralInst:
    case ValueKind::StructInst:
    case ValueKind::StructExtractInst:
    case ValueKind::StructElementAddrInst:
    case ValueKind::TupleInst:
    case ValueKind::TupleExtractInst:
    case ValueKind::TupleElementAddrInst:
    case ValueKind::MetatypeInst:
    case ValueKind::ValueMetatypeInst:
    case ValueKind::ObjCProtocolInst:
    case ValueKind::RefElementAddrInst:
    case ValueKind::ProjectBoxInst:
    case ValueKind::IndexRawPointerInst:
    case ValueKind::IndexAddrInst:
    case ValueKind::PointerToAddressInst:
    case ValueKind::AddressToPointerInst:
    case ValueKind::CondFailInst:
    case ValueKind::EnumInst:
    case ValueKind::UncheckedEnumDataInst:
    case ValueKind::IsNonnullInst:
    case ValueKind::UncheckedTrivialBitCastInst:
    case ValueKind::UncheckedBitwiseCastInst:
    case ValueKind::RefToRawPointerInst:
    case ValueKind::RawPointerToRefInst:
    case ValueKind::RefToUnownedInst:
    case ValueKind::UnownedToRefInst:
    case ValueKind::RefToUnmanagedInst:
    case ValueKind::UnmanagedToRefInst:
    case ValueKind::UpcastInst:
    case ValueKind::ThickToObjCMetatypeInst:
    case ValueKind::ObjCToThickMetatypeInst:
    case ValueKind::UncheckedRefCastInst:
    case ValueKind::UncheckedAddrCastInst:
    case ValueKind::ObjCMetatypeToObjectInst:
    case ValueKind::ObjCExistentialMetatypeToObjectInst:
    case ValueKind::SelectEnumInst:
    case ValueKind::SelectValueInst:
    case ValueKind::RefToBridgeObjectInst:
    case ValueKind::BridgeObjectToRefInst:
    case ValueKind::BridgeObjectToWordInst:
    case ValueKind::ThinFunctionToPointerInst:
    case ValueKind::PointerToThinFunctionInst:
    case ValueKind::MarkDependenceInst:
    case ValueKind::OpenExistentialRefInst:
      return true;
    default:
      return false;
  }
}

using ApplyWitnessPair = std::pair<ApplyInst *, WitnessMethodInst *>;

/// Returns the Apply and WitnessMethod instructions that use the
/// open_existential_addr instructions, or null if at least one of the
/// instructions is missing.
static ApplyWitnessPair getOpenExistentialUsers(OpenExistentialAddrInst *OE) {
  ApplyInst *AI = nullptr;
  WitnessMethodInst *WMI = nullptr;
  ApplyWitnessPair Empty = std::make_pair(nullptr, nullptr);

  for (auto *UI : getNonDebugUses(OE)) {
    auto *User = UI->getUser();
    if (!isa<WitnessMethodInst>(User) &&
        User->isOpenedArchetypeOperand(UI->getOperandNumber()))
      continue;
    // Check that we have a single Apply user.
    if (auto *AA = dyn_cast<ApplyInst>(User)) {
      if (AI)
        return Empty;

      AI = AA;
      continue;
    }

    // Check that we have a single WMI user.
    if (auto *W = dyn_cast<WitnessMethodInst>(User)) {
      if (WMI)
        return Empty;

      WMI = W;
      continue;
    }

    // Unknown instruction.
    return Empty;
  }

  // Both instructions need to exist.
  if (!WMI || !AI)
    return Empty;

  // Make sure that the WMI and AI match.
  if (AI->getCallee() != WMI)
    return Empty;

  // We have exactly the pattern that we expected.
  return std::make_pair(AI, WMI);
}

/// Try to CSE the users of \p From to the users of \p To.
/// The original users of \p To are passed in ToApplyWitnessUsers.
/// Returns true on success.
static bool tryToCSEOpenExtCall(OpenExistentialAddrInst *From,
                                OpenExistentialAddrInst *To,
                                ApplyWitnessPair ToApplyWitnessUsers,
                                DominanceInfo *DA) {
  assert(From != To && "Can't replace instruction with itself");

  ApplyInst *FromAI = nullptr;
  ApplyInst *ToAI = nullptr;
  WitnessMethodInst *FromWMI = nullptr;
  WitnessMethodInst *ToWMI = nullptr;
  std::tie(FromAI, FromWMI) = getOpenExistentialUsers(From);
  std::tie(ToAI, ToWMI) = ToApplyWitnessUsers;

  // Make sure that the OEA instruction has exactly two expected users.
  if (!FromAI || !ToAI || !FromWMI || !ToWMI)
    return false;

  // Make sure we are calling the same method.
  if (FromWMI->getMember() != ToWMI->getMember())
    return false;

  // We are going to reuse the TO-WMI, so make sure it dominates the call site.
  if (!DA->properlyDominates(ToWMI, FromWMI))
    return false;

  SILBuilder Builder(FromAI);
  // Make archetypes used by the ToAI available to the builder.
  SILOpenedArchetypesTracker OpenedArchetypesTracker(*FromAI->getFunction());
  OpenedArchetypesTracker.registerUsedOpenedArchetypes(ToAI);
  Builder.setOpenedArchetypesTracker(&OpenedArchetypesTracker);

  assert(FromAI->getArguments().size() == ToAI->getArguments().size() &&
         "Invalid number of arguments");

  // Don't handle any apply instructions that involve substitutions.
  if (ToAI->getSubstitutions().size() != 1) return false;

  // Prepare the Apply args.
  SmallVector<SILValue, 8> Args;
  for (auto Op : FromAI->getArguments()) {
      Args.push_back(Op == From ? To : Op);
  }

  auto FnTy = ToAI->getSubstCalleeSILType();
  auto ResTy = FnTy.castTo<SILFunctionType>()->getSILResult();

  ApplyInst *NAI = Builder.createApply(ToAI->getLoc(), ToWMI, FnTy, ResTy,
                                       ToAI->getSubstitutions(), Args,
                                       ToAI->isNonThrowing());
  FromAI->replaceAllUsesWith(NAI);
  FromAI->eraseFromParent();
  NumOpenExtRemoved++;
  return true;
}

/// Try to CSE the users of the protocol that's passed in argument \p Arg.
/// \returns True if some instructions were modified.
static bool CSExistentialInstructions(SILArgument *Arg, DominanceInfo *DA) {
  ParameterConvention Conv = Arg->getKnownParameterInfo().getConvention();
  // We can assume that the address of Proto does not alias because the
  // calling convention is In or In-guaranteed.
  bool MayAlias = Conv != ParameterConvention::Indirect_In_Guaranteed &&
                  Conv != ParameterConvention::Indirect_In;
  if (MayAlias)
    return false;

  // Now check that the only uses of the protocol are witness_method,
  // open_existential_addr and destroy_addr. Also, collect all of the 'opens'.
  llvm::SmallVector<OpenExistentialAddrInst*, 8> Opens;
  for (auto *UI : getNonDebugUses(Arg)) {
    auto *User = UI->getUser();
    if (auto *Open = dyn_cast<OpenExistentialAddrInst>(User)) {
      Opens.push_back(Open);
      continue;
    }

    if (isa<WitnessMethodInst>(User) || isa<DestroyAddrInst>(User))
      continue;

    // Bail out if we found an instruction that we can't handle.
    return false;
  }

  // Find the best dominating 'open' for each open existential.
  llvm::SmallVector<OpenExistentialAddrInst*, 8> TopDominator(Opens);

  bool Changed = false;

  // Try to CSE the users of the current open_existential_addr instruction with
  // one of the other open_existential_addr that dominate it.
  int NumOpenInstr = Opens.size();
  for (int i = 0; i < NumOpenInstr; i++) {
    // Try to find a better dominating 'open' for the i-th instruction.
    OpenExistentialAddrInst *SomeOpen = TopDominator[i];
    for (int j = 0; j < NumOpenInstr; j++) {

      if (i == j || TopDominator[i] == TopDominator[j])
        continue;

      OpenExistentialAddrInst *DominatingOpen = TopDominator[j];

      if (DominatingOpen->getOperand() != SomeOpen->getOperand())
        continue;

      if (DA->properlyDominates(DominatingOpen, SomeOpen)) {
        // We found an open instruction that DominatingOpen dominates:
        TopDominator[i] = TopDominator[j];
      }
    }
  }


  // Inspect all of the open_existential_addr instructions and record the
  // apply-witness users. We need to save the original Apply-Witness users
  // because we'll be adding new users and we need to make sure that we can
  // find the original users.
  llvm::SmallVector<ApplyWitnessPair, 8> OriginalAW;
  for (int i=0; i < NumOpenInstr; i++) {
    OriginalAW.push_back(getOpenExistentialUsers(TopDominator[i]));
  }

  // Perform the CSE for the open_existential_addr instruction and their
  // dominating instruction.
  for (int i=0; i < NumOpenInstr; i++) {
    if (Opens[i] != TopDominator[i])
      Changed |= tryToCSEOpenExtCall(Opens[i], TopDominator[i],
                                     OriginalAW[i], DA);
  }

  return Changed;
}

/// Detect multiple calls to existential members and try to CSE the instructions
/// that perform the method lookup (the open_existential_addr and
/// witness_method):
///
/// open_existential_addr %0 : $*Pingable to $*@opened("1E467EB8-...")
/// witness_method $@opened("1E467EB8-...") Pingable, #Pingable.ping!1, %2
/// apply %3<@opened("1E467EB8-...") Pingable>(%2)
///
/// \returns True if some instructions were modified.
static bool CSEExistentialCalls(SILFunction *Func, DominanceInfo *DA) {
  bool Changed = false;
  for (auto *Arg : Func->getArgumentsWithoutIndirectResults()) {
    if (Arg->getType().isExistentialType())
      Changed |= CSExistentialInstructions(Arg, DA);
  }

  return Changed;
}

namespace {
class SILCSE : public SILFunctionTransform {
  
  /// True if CSE is done on high-level SIL, i.e. semantic calls are not inlined
  /// yet. In this case some semantic calls can be CSEd.
  /// We only CSE semantic calls on high-level SIL because we can be sure that
  /// e.g. an Array as SILValue is really immutable (including its content).
  bool RunsOnHighLevelSil;
  
  void run() override {
    DEBUG(llvm::dbgs() << "***** CSE on function: " << getFunction()->getName()
          << " *****\n");

    DominanceAnalysis* DA = getAnalysis<DominanceAnalysis>();

    auto *SEA = PM->getAnalysis<SideEffectAnalysis>();

    CSE C(RunsOnHighLevelSil, SEA);
    bool Changed = false;

    // Perform the traditional CSE.
    Changed |= C.processFunction(*getFunction(), DA->get(getFunction()));

    // Perform CSE of existential and witness_method instructions.
    Changed |= CSEExistentialCalls(getFunction(),
                                          DA->get(getFunction()));
    if (Changed) {
      invalidateAnalysis(SILAnalysis::InvalidationKind::CallsAndInstructions);
    }
  }

  StringRef getName() override {
    return RunsOnHighLevelSil ? "High-level CSE" : "CSE";
  }
  
public:
  SILCSE(bool RunsOnHighLevelSil) : RunsOnHighLevelSil(RunsOnHighLevelSil) {}
};
} // end anonymous namespace

SILTransform *swift::createCSE() {
  return new SILCSE(false);
}

SILTransform *swift::createHighLevelCSE() {
  return new SILCSE(true);
}
