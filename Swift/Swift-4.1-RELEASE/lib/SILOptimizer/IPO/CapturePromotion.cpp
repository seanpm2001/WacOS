//===--- CapturePromotion.cpp - Promotes closure captures -----------------===//
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
///
/// \file
///
/// Promotes captures from 'inout' (i.e. by-reference) to by-value
/// ==============================================================
///
/// Swift's closure model is that all local variables are capture by reference.
/// This produces a very simple programming model which is great to use, but
/// relies on the optimizer to promote by-ref captures to by-value (i.e. by-copy)
/// captures for decent performance. Consider this simple example:
///
///   func foo(a : () -> ()) {} // assume this has an unknown body
///
///   func bar() {
///     var x = 42
///
///     foo({ print(x) })
///   }
///
/// Since x is captured by-ref by the closure, x must live on the heap. By
/// looking at bar without any knowledge of foo, we can know that it is safe to
/// promote this to a by-value capture, allowing x to live on the stack under the
/// following conditions:
///
/// 1. If x is not modified in the closure body and is only loaded.
/// 2. If we can prove that all mutations to x occur before the closure is
///    formed.
///
/// Under these conditions if x is loadable then we can even load the given value
/// and pass it as a scalar instead of an address.
///
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "sil-capture-promotion"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/SILOptimizer/Utils/SpecializationMangler.h"
#include "swift/SIL/SILCloner.h"
#include "swift/SIL/TypeSubstCloner.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/AST/GenericEnvironment.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include <tuple>

using namespace swift;

typedef llvm::SmallSet<unsigned, 4> IndicesSet;
typedef llvm::DenseMap<PartialApplyInst*, IndicesSet> PartialApplyIndicesMap;

STATISTIC(NumCapturesPromoted, "Number of captures promoted");

namespace {
/// \brief Transient reference to a block set within ReachabilityInfo.
///
/// This is a bitset that conveniently flattens into a matrix allowing bit-wise
/// operations without masking.
///
/// TODO: If this sticks around, maybe we'll make a BitMatrix ADT.
class ReachingBlockSet {
public:
  enum { BITWORD_SIZE = (unsigned)sizeof(uint64_t) * CHAR_BIT };

  static size_t numBitWords(unsigned NumBlocks) {
    return (NumBlocks + BITWORD_SIZE - 1) / BITWORD_SIZE;
  }

  /// \brief Transient reference to a reaching block matrix.
  struct ReachingBlockMatrix {
    uint64_t *Bits;
    unsigned NumBitWords; // Words per row.

    ReachingBlockMatrix() : Bits(nullptr), NumBitWords(0) {}

    bool empty() const { return !Bits; }
  };

  static ReachingBlockMatrix allocateMatrix(unsigned NumBlocks) {
    ReachingBlockMatrix M;
    M.NumBitWords = numBitWords(NumBlocks);
    M.Bits = new uint64_t[NumBlocks * M.NumBitWords];
    memset(M.Bits, 0, NumBlocks * M.NumBitWords * sizeof(uint64_t));
    return M;
  }
  static void deallocateMatrix(ReachingBlockMatrix &M) {
    delete [] M.Bits;
    M.Bits = nullptr;
    M.NumBitWords = 0;
  }
  static ReachingBlockSet allocateSet(unsigned NumBlocks) {
    ReachingBlockSet S;
    S.NumBitWords = numBitWords(NumBlocks);
    S.Bits = new uint64_t[S.NumBitWords];
    return S;
  }
  static void deallocateSet(ReachingBlockSet &S) {
    delete [] S.Bits;
    S.Bits = nullptr;
    S.NumBitWords = 0;
  }

private:
  uint64_t *Bits;
  unsigned NumBitWords;

public:
  ReachingBlockSet() : Bits(nullptr), NumBitWords(0) {}

  ReachingBlockSet(unsigned BlockID, ReachingBlockMatrix &M)
    : Bits(&M.Bits[BlockID * M.NumBitWords]),
      NumBitWords(M.NumBitWords) {}

  bool test(unsigned ID) const {
    assert(ID / BITWORD_SIZE < NumBitWords && "block ID out-of-bounds");
    unsigned int modulus = ID % BITWORD_SIZE;
    long shifted = 1L << modulus;
    return Bits[ID / BITWORD_SIZE] & shifted;
  }

  void set(unsigned ID) {
    unsigned int modulus = ID % BITWORD_SIZE;
    long shifted = 1L << modulus;
    assert(ID / BITWORD_SIZE < NumBitWords && "block ID out-of-bounds");
    Bits[ID / BITWORD_SIZE] |= shifted;
  }

  ReachingBlockSet &operator|=(const ReachingBlockSet &RHS) {
    for (size_t i = 0, e = NumBitWords; i != e; ++i)
      Bits[i] |= RHS.Bits[i];
    return *this;
  }

  void clear() {
    memset(Bits, 0, NumBitWords * sizeof(uint64_t));
  }

  bool operator==(const ReachingBlockSet &RHS) const {
    assert(NumBitWords == RHS.NumBitWords && "mismatched sets");
    for (size_t i = 0, e = NumBitWords; i != e; ++i) {
      if (Bits[i] != RHS.Bits[i])
        return false;
    }
    return true;
  }

  bool operator!=(const ReachingBlockSet &RHS) const {
    return !(*this == RHS);
  }

  const ReachingBlockSet &operator=(const ReachingBlockSet &RHS) {
    assert(NumBitWords == RHS.NumBitWords && "mismatched sets");
    for (size_t i = 0, e = NumBitWords; i != e; ++i)
      Bits[i] = RHS.Bits[i];
    return *this;
  }
};

/// \brief Store the reachability matrix: ToBlock -> FromBlocks.
class ReachabilityInfo {
  SILFunction *F;
  llvm::DenseMap<SILBasicBlock*, unsigned> BlockMap;
  ReachingBlockSet::ReachingBlockMatrix Matrix;

public:
  ReachabilityInfo(SILFunction *f) : F(f) {}
  ~ReachabilityInfo() { ReachingBlockSet::deallocateMatrix(Matrix); }

  bool isComputed() const { return !Matrix.empty(); }

  bool isReachable(SILBasicBlock *From, SILBasicBlock *To);

private:
  void compute();
};

} // end anonymous namespace


namespace {
/// \brief A SILCloner subclass which clones a closure function while converting
/// one or more captures from 'inout' (by-reference) to by-value.
class ClosureCloner : public SILClonerWithScopes<ClosureCloner> {
public:
  friend class SILInstructionVisitor<ClosureCloner>;
  friend class SILCloner<ClosureCloner>;

  ClosureCloner(SILFunction *Orig, IsSerialized_t Serialized,
                StringRef ClonedName,
                IndicesSet &PromotableIndices);

  void populateCloned();

  SILFunction *getCloned() { return &getBuilder().getFunction(); }

private:
  static SILFunction *initCloned(SILFunction *Orig, IsSerialized_t Serialized,
                                 StringRef ClonedName,
                                 IndicesSet &PromotableIndices);

  SILValue getProjectBoxMappedVal(SILValue Operand);

  void visitDebugValueAddrInst(DebugValueAddrInst *Inst);
  void visitStrongReleaseInst(StrongReleaseInst *Inst);
  void visitDestroyValueInst(DestroyValueInst *Inst);
  void visitStructElementAddrInst(StructElementAddrInst *Inst);
  void visitLoadInst(LoadInst *Inst);
  void visitLoadBorrowInst(LoadBorrowInst *Inst);
  void visitProjectBoxInst(ProjectBoxInst *Inst);
  void visitBeginAccessInst(BeginAccessInst *Inst);
  void visitEndAccessInst(EndAccessInst *Inst);

  SILFunction *Orig;
  IndicesSet &PromotableIndices;
  llvm::DenseMap<SILArgument *, SILValue> BoxArgumentMap;
  llvm::DenseMap<ProjectBoxInst *, SILValue> ProjectBoxArgumentMap;
};
} // end anonymous namespace

/// \brief Compute ReachabilityInfo so that it can answer queries about
/// whether a given basic block in a function is reachable from another basic
/// block in the function.
///
/// FIXME: Computing global reachability requires initializing an N^2
/// bitset. This could be avoided by computing reachability on-the-fly
/// for each alloc_box by walking backward from mutations.
void ReachabilityInfo::compute() {
  assert(!isComputed() && "already computed");

  unsigned N = 0;
  for (auto &BB : *F)
    BlockMap.insert({ &BB, N++ });
  Matrix = ReachingBlockSet::allocateMatrix(N);
  ReachingBlockSet NewSet = ReachingBlockSet::allocateSet(N);

  DEBUG(llvm::dbgs() << "Computing Reachability for " << F->getName()
        << " with " << N << " blocks.\n");

  // Iterate to a fix point, two times for a topological DAG.
  bool Changed;
  do {
    Changed = false;

    // Visit all blocks in a predictable order, hopefully close to topological.
    for (auto &BB : *F) {
      ReachingBlockSet CurSet(BlockMap[&BB], Matrix);
      if (!Changed) {
        // If we have not detected a change yet, then calculate new
        // reachabilities into a new bit vector so we can determine if any
        // change has occurred.
        NewSet = CurSet;
        for (auto PI = BB.pred_begin(), PE = BB.pred_end(); PI != PE; ++PI) {
          unsigned PredID = BlockMap[*PI];
          ReachingBlockSet PredSet(PredID, Matrix);
          NewSet |= PredSet;
          NewSet.set(PredID);
        }
        if (NewSet != CurSet) {
          CurSet = NewSet;
          Changed = true;
        }
      } else {
        // Otherwise, just update the existing reachabilities in-place.
        for (auto PI = BB.pred_begin(), PE = BB.pred_end(); PI != PE; ++PI) {
          unsigned PredID = BlockMap[*PI];
          ReachingBlockSet PredSet(PredID, Matrix);
          CurSet |= PredSet;
          CurSet.set(PredID);
        }
      }
      DEBUG(llvm::dbgs() << "  Block " << BlockMap[&BB] << " reached by ";
            for (unsigned i = 0; i < N; ++i) {
              if (CurSet.test(i))
                llvm::dbgs() << i << " ";
            }
            llvm::dbgs() << "\n");
    }
  } while (Changed);

  ReachingBlockSet::deallocateSet(NewSet);
}

/// \brief Return true if the To basic block is reachable from the From basic
/// block. A block is considered reachable from itself only if its entry can be
/// recursively reached from its own exit.
bool
ReachabilityInfo::isReachable(SILBasicBlock *From, SILBasicBlock *To) {
  if (!isComputed())
    compute();

  auto FI = BlockMap.find(From), TI = BlockMap.find(To);
  assert(FI != BlockMap.end() && TI != BlockMap.end());
  ReachingBlockSet FromSet(TI->second, Matrix);
  return FromSet.test(FI->second);
}

ClosureCloner::ClosureCloner(SILFunction *Orig, IsSerialized_t Serialized,
                             StringRef ClonedName,
                             IndicesSet &PromotableIndices)
  : SILClonerWithScopes<ClosureCloner>(
                           *initCloned(Orig, Serialized, ClonedName, PromotableIndices)),
    Orig(Orig), PromotableIndices(PromotableIndices) {
  assert(Orig->getDebugScope()->Parent != getCloned()->getDebugScope()->Parent);
}

/// Compute the SILParameterInfo list for the new cloned closure.
///
/// Our goal as a result of this transformation is to:
///
/// 1. Let through all arguments not related to a promotable box.
/// 2. Replace container box value arguments for the cloned closure with the
///    transformed address or value argument.
static void
computeNewArgInterfaceTypes(SILFunction *F,
                            IndicesSet &PromotableIndices,
                            SmallVectorImpl<SILParameterInfo> &OutTys) {
  auto fnConv = F->getConventions();
  auto Parameters = fnConv.funcTy->getParameters();

  DEBUG(llvm::dbgs() << "Preparing New Args!\n");

  auto fnTy = F->getLoweredFunctionType();

  auto &Types = F->getModule().Types;
  Lowering::GenericContextScope scope(Types, fnTy->getGenericSignature());

  // For each parameter in the old function...
  for (unsigned Index : indices(Parameters)) {
    auto &param = Parameters[Index];

    // The PromotableIndices index is expressed as the argument index (num
    // indirect result + param index). Add back the num indirect results to get
    // the arg index when working with PromotableIndices.
    unsigned ArgIndex = Index + fnConv.getSILArgIndexOfFirstParam();

    DEBUG(llvm::dbgs() << "Index: " << Index << "; PromotableIndices: "
          << (PromotableIndices.count(ArgIndex)?"yes":"no")
          << " Param: "; param.dump());

    if (!PromotableIndices.count(ArgIndex)) {
      OutTys.push_back(param);
      continue;
    }
    
    // Perform the proper conversions and then add it to the new parameter list
    // for the type.
    assert(!param.isFormalIndirect());
    auto paramTy = param.getSILStorageType();
    auto paramBoxTy = paramTy.castTo<SILBoxType>();
    assert(paramBoxTy->getLayout()->getFields().size() == 1
           && "promoting compound box not implemented yet");
    auto paramBoxedTy = paramBoxTy->getFieldType(F->getModule(), 0);
    auto &paramTL = Types.getTypeLowering(paramBoxedTy);
    ParameterConvention convention;
    if (paramTL.isFormallyPassedIndirectly()) {
      convention = ParameterConvention::Indirect_In;
    } else if (paramTL.isTrivial()) {
      convention = ParameterConvention::Direct_Unowned;
    } else {
      convention = param.isGuaranteed() ? ParameterConvention::Direct_Guaranteed
                                        : ParameterConvention::Direct_Owned;
    }
    OutTys.push_back(SILParameterInfo(paramBoxedTy.getSwiftRValueType(),
                                      convention));
  }
}

static std::string getSpecializedName(SILFunction *F,
                                      IsSerialized_t Serialized,
                                      IndicesSet &PromotableIndices) {
  auto P = Demangle::SpecializationPass::CapturePromotion;
  Mangle::FunctionSignatureSpecializationMangler Mangler(P, Serialized, F);
  auto fnConv = F->getConventions();

  for (unsigned argIdx = 0, endIdx = fnConv.getNumSILArguments();
       argIdx < endIdx; ++argIdx) {
    if (!PromotableIndices.count(argIdx))
      continue;
    Mangler.setArgumentBoxToValue(argIdx);
  }
  return Mangler.mangle();
}

/// \brief Create the function corresponding to the clone of the original
/// closure with the signature modified to reflect promotable captures (which
/// are given by PromotableIndices, such that each entry in the set is the
/// index of the box containing the variable in the closure's argument list, and
/// the address of the box's contents is the argument immediately following each
/// box argument); does not actually clone the body of the function
///
/// *NOTE* PromotableIndices only contains the container value of the box, not
/// the address value.
SILFunction*
ClosureCloner::initCloned(SILFunction *Orig, IsSerialized_t Serialized,
                          StringRef ClonedName,
                          IndicesSet &PromotableIndices) {
  SILModule &M = Orig->getModule();

  // Compute the arguments for our new function.
  SmallVector<SILParameterInfo, 4> ClonedInterfaceArgTys;
  computeNewArgInterfaceTypes(Orig, PromotableIndices, ClonedInterfaceArgTys);

  SILFunctionType *OrigFTI = Orig->getLoweredFunctionType();

  // Create the thin function type for the cloned closure.
  auto ClonedTy = SILFunctionType::get(
      OrigFTI->getGenericSignature(), OrigFTI->getExtInfo(),
      OrigFTI->getCoroutineKind(), OrigFTI->getCalleeConvention(),
      ClonedInterfaceArgTys, OrigFTI->getYields(),
      OrigFTI->getResults(), OrigFTI->getOptionalErrorResult(),
      M.getASTContext(), OrigFTI->getWitnessMethodConformanceOrNone());

  assert((Orig->isTransparent() || Orig->isBare() || Orig->getLocation())
         && "SILFunction missing location");
  assert((Orig->isTransparent() || Orig->isBare() || Orig->getDebugScope())
         && "SILFunction missing DebugScope");
  assert(!Orig->isGlobalInit() && "Global initializer cannot be cloned");

  auto *Fn = M.createFunction(
      Orig->getLinkage(), ClonedName, ClonedTy, Orig->getGenericEnvironment(),
      Orig->getLocation(), Orig->isBare(), IsNotTransparent, Serialized,
      Orig->getEntryCount(), Orig->isThunk(), Orig->getClassSubclassScope(),
      Orig->getInlineStrategy(), Orig->getEffectsKind(), Orig,
      Orig->getDebugScope());
  for (auto &Attr : Orig->getSemanticsAttrs())
    Fn->addSemanticsAttr(Attr);
  if (!Orig->hasQualifiedOwnership()) {
    Fn->setUnqualifiedOwnership();
  }
  return Fn;
}

/// \brief Populate the body of the cloned closure, modifying instructions as
/// necessary to take into consideration the promoted capture(s)
void
ClosureCloner::populateCloned() {
  SILFunction *Cloned = getCloned();

  // Create arguments for the entry block
  SILBasicBlock *OrigEntryBB = &*Orig->begin();
  SILBasicBlock *ClonedEntryBB = Cloned->createBasicBlock();
  getBuilder().setInsertionPoint(ClonedEntryBB);

  unsigned ArgNo = 0;
  auto I = OrigEntryBB->args_begin(), E = OrigEntryBB->args_end();
  for (; I != E; ++ArgNo, ++I) {
    if (!PromotableIndices.count(ArgNo)) {
      // Otherwise, create a new argument which copies the original argument
      SILValue MappedValue = ClonedEntryBB->createFunctionArgument(
          (*I)->getType(), (*I)->getDecl());
      ValueMap.insert(std::make_pair(*I, MappedValue));
      continue;
    }

    // Handle the case of a promoted capture argument.
    auto BoxTy = (*I)->getType().castTo<SILBoxType>();
    assert(BoxTy->getLayout()->getFields().size() == 1 &&
           "promoting compound box not implemented");
    auto BoxedTy = BoxTy->getFieldType(Cloned->getModule(), 0).getObjectType();
    SILValue MappedValue =
        ClonedEntryBB->createFunctionArgument(BoxedTy, (*I)->getDecl());

    // If SIL ownership is enabled, we need to perform a borrow here if we have
    // a non-trivial value. We know that our value is not written to and it does
    // not escape. The use of a borrow enforces this.
    if (Cloned->hasQualifiedOwnership() &&
        MappedValue.getOwnershipKind() != ValueOwnershipKind::Trivial) {
      SILLocation Loc(const_cast<ValueDecl *>((*I)->getDecl()));
      MappedValue = getBuilder().createBeginBorrow(Loc, MappedValue);
    }
    BoxArgumentMap.insert(std::make_pair(*I, MappedValue));

    // Track the projections of the box.
    for (auto *Use : (*I)->getUses()) {
      if (auto Proj = dyn_cast<ProjectBoxInst>(Use->getUser())) {
        ProjectBoxArgumentMap.insert(std::make_pair(Proj, MappedValue));
      }
    }
  }

  BBMap.insert(std::make_pair(OrigEntryBB, ClonedEntryBB));
  // Recursively visit original BBs in depth-first preorder, starting with the
  // entry block, cloning all instructions other than terminators.
  visitSILBasicBlock(OrigEntryBB);

  // Now iterate over the BBs and fix up the terminators.
  for (auto BI = BBMap.begin(), BE = BBMap.end(); BI != BE; ++BI) {
    getBuilder().setInsertionPoint(BI->second);
    visit(BI->first->getTerminator());
  }
}

/// If this operand originates from a mapped ProjectBox, return the mapped
/// value. Otherwise return an invalid value.
SILValue ClosureCloner::getProjectBoxMappedVal(SILValue Operand) {
  if (auto *Access = dyn_cast<BeginAccessInst>(Operand))
    Operand = Access->getSource();

  if (auto *Project = dyn_cast<ProjectBoxInst>(Operand)) {
    auto I = ProjectBoxArgumentMap.find(Project);
    if (I != ProjectBoxArgumentMap.end())
      return I->second;
  }
  return SILValue();
}

/// Handle a debug_value_addr instruction during cloning of a closure;
/// if its operand is the promoted address argument then lower it to a
/// debug_value, otherwise it is handled normally.
void ClosureCloner::visitDebugValueAddrInst(DebugValueAddrInst *Inst) {
  if (SILValue Val = getProjectBoxMappedVal(Inst->getOperand())) {
    getBuilder().setCurrentDebugScope(getOpScope(Inst->getDebugScope()));
    getBuilder().createDebugValue(Inst->getLoc(), Val, Inst->getVarInfo());
    return;
  }
  SILCloner<ClosureCloner>::visitDebugValueAddrInst(Inst);
}

/// \brief Handle a strong_release instruction during cloning of a closure; if
/// it is a strong release of a promoted box argument, then it is replaced with
/// a ReleaseValue of the new object type argument, otherwise it is handled
/// normally.
void
ClosureCloner::visitStrongReleaseInst(StrongReleaseInst *Inst) {
  assert(
      !Inst->getFunction()->hasQualifiedOwnership() &&
      "Should not see strong release in a function with qualified ownership");
  SILValue Operand = Inst->getOperand();
  if (auto *A = dyn_cast<SILArgument>(Operand)) {
    auto I = BoxArgumentMap.find(A);
    if (I != BoxArgumentMap.end()) {
      // Releases of the box arguments get replaced with ReleaseValue of the new
      // object type argument.
      SILFunction &F = getBuilder().getFunction();
      auto &typeLowering = F.getModule().getTypeLowering(I->second->getType());
      SILBuilderWithPostProcess<ClosureCloner, 1> B(this, Inst);
      typeLowering.emitDestroyValue(B, Inst->getLoc(), I->second);
      return;
    }
  }

  SILCloner<ClosureCloner>::visitStrongReleaseInst(Inst);
}

/// \brief Handle a destroy_value instruction during cloning of a closure; if
/// it is a strong release of a promoted box argument, then it is replaced with
/// a destroy_value of the new object type argument, otherwise it is handled
/// normally.
void ClosureCloner::visitDestroyValueInst(DestroyValueInst *Inst) {
  SILValue Operand = Inst->getOperand();
  if (auto *A = dyn_cast<SILArgument>(Operand)) {
    auto I = BoxArgumentMap.find(A);
    if (I != BoxArgumentMap.end()) {
      // Releases of the box arguments get replaced with an end_borrow,
      // destroy_value of the new object type argument.
      SILFunction &F = getBuilder().getFunction();
      auto &typeLowering = F.getModule().getTypeLowering(I->second->getType());
      SILBuilderWithPostProcess<ClosureCloner, 1> B(this, Inst);

      SILValue Value = I->second;

      // If ownership is enabled, then we must emit a begin_borrow for any
      // non-trivial value.
      if (F.hasQualifiedOwnership() &&
          Value.getOwnershipKind() != ValueOwnershipKind::Trivial) {
        auto *BBI = cast<BeginBorrowInst>(Value);
        Value = BBI->getOperand();
        B.createEndBorrow(Inst->getLoc(), BBI, Value);
      }

      typeLowering.emitDestroyValue(B, Inst->getLoc(), Value);
      return;
    }
  }

  SILCloner<ClosureCloner>::visitDestroyValueInst(Inst);
}

/// Handle a struct_element_addr instruction during cloning of a closure.
///
/// If its operand is the promoted address argument then ignore it, otherwise it
/// is handled normally.
void
ClosureCloner::visitStructElementAddrInst(StructElementAddrInst *Inst) {
  if (getProjectBoxMappedVal(Inst->getOperand()))
    return;

  SILCloner<ClosureCloner>::visitStructElementAddrInst(Inst);
}

/// project_box of captured boxes can be eliminated.
void
ClosureCloner::visitProjectBoxInst(ProjectBoxInst *I) {
  if (auto Arg = dyn_cast<SILArgument>(I->getOperand()))
    if (BoxArgumentMap.count(Arg))
      return;
  
  SILCloner<ClosureCloner>::visitProjectBoxInst(I);
}

/// If its operand is the promoted address argument then ignore it, otherwise it
/// is handled normally.
void ClosureCloner::visitBeginAccessInst(BeginAccessInst *Inst) {
  if (getProjectBoxMappedVal(Inst->getSource()))
    return;

  SILCloner<ClosureCloner>::visitBeginAccessInst(Inst);
}

/// If its operand is the promoted address argument then ignore it, otherwise it
/// is handled normally.
void ClosureCloner::visitEndAccessInst(EndAccessInst *Inst) {
  if (getProjectBoxMappedVal(Inst->getBeginAccess()))
    return;

  SILCloner<ClosureCloner>::visitEndAccessInst(Inst);
}

/// \brief Handle a load_borrow instruction during cloning of a closure.
///
/// The two relevant cases are a direct load from a promoted address argument or
/// a load of a struct_element_addr of a promoted address argument.
void ClosureCloner::visitLoadBorrowInst(LoadBorrowInst *LI) {
  assert(LI->getFunction()->hasQualifiedOwnership() &&
         "We should only see a load borrow in ownership qualified SIL");
  if (SILValue Val = getProjectBoxMappedVal(LI->getOperand())) {
    // Loads of the address argument get eliminated completely; the uses of
    // the loads get mapped to uses of the new object type argument.
    //
    // We assume that the value is already guaranteed.
    assert(Val.getOwnershipKind().isTrivialOr(ValueOwnershipKind::Guaranteed) &&
           "Expected argument value to be guaranteed");
    ValueMap.insert(std::make_pair(LI, Val));
    return;
  }

  SILCloner<ClosureCloner>::visitLoadBorrowInst(LI);
  return;
}

/// \brief Handle a load instruction during cloning of a closure.
///
/// The two relevant cases are a direct load from a promoted address argument or
/// a load of a struct_element_addr of a promoted address argument.
void ClosureCloner::visitLoadInst(LoadInst *LI) {
  if (SILValue Val = getProjectBoxMappedVal(LI->getOperand())) {
    // Loads of the address argument get eliminated completely; the uses of
    // the loads get mapped to uses of the new object type argument.
    //
    // If we are compiling with SIL ownership, we need to take different
    // behaviors depending on the type of load. Specifically, if we have a
    // load [copy], then we need to add a copy_value here. If we have a take
    // or trivial, we just propagate the value through.
    if (LI->getFunction()->hasQualifiedOwnership()
        && LI->getOwnershipQualifier() == LoadOwnershipQualifier::Copy) {
      Val = getBuilder().createCopyValue(LI->getLoc(), Val);
    }
    ValueMap.insert(std::make_pair(LI, Val));
    return;
  }

  auto *SEAI = dyn_cast<StructElementAddrInst>(LI->getOperand());
  if (!SEAI) {
    SILCloner<ClosureCloner>::visitLoadInst(LI);
    return;
  }

  if (SILValue Val = getProjectBoxMappedVal(SEAI->getOperand())) {
    // Loads of a struct_element_addr of an argument get replaced with a
    // struct_extract of the new passed in value. The value should be borrowed
    // already, so we can just extract the value.
    assert(!getBuilder().getFunction().hasQualifiedOwnership() ||
           Val.getOwnershipKind().isTrivialOr(ValueOwnershipKind::Guaranteed));
    Val = getBuilder().emitStructExtract(LI->getLoc(), Val, SEAI->getField(),
                                         LI->getType());

    // If we were performing a load [copy], then we need to a perform a copy
    // here since when cloning, we do not eliminate the destroy on the copied
    // value.
    if (LI->getFunction()->hasQualifiedOwnership()
        && LI->getOwnershipQualifier() == LoadOwnershipQualifier::Copy) {
      Val = getBuilder().createCopyValue(LI->getLoc(), Val);
    }
    ValueMap.insert(std::make_pair(LI, Val));
    return;
  }
  SILCloner<ClosureCloner>::visitLoadInst(LI);
}

static SILArgument *getBoxFromIndex(SILFunction *F, unsigned Index) {
  assert(F->isDefinition() && "Expected definition not external declaration!");
  auto &Entry = F->front();
  return Entry.getArgument(Index);
}

static bool isNonMutatingLoad(SILInstruction *I) {
  auto *LI = dyn_cast<LoadInst>(I);
  if (!LI)
    return false;
  return LI->getOwnershipQualifier() != LoadOwnershipQualifier::Take;
}

/// \brief Given a partial_apply instruction and the argument index into its
/// callee's argument list of a box argument (which is followed by an argument
/// for the address of the box's contents), return true if the closure is known
/// not to mutate the captured variable.
static bool
isNonMutatingCapture(SILArgument *BoxArg) {
  SmallVector<ProjectBoxInst*, 2> Projections;

  // Conservatively do not allow any use of the box argument other than a
  // strong_release or projection, since this is the pattern expected from
  // SILGen.
  for (auto *O : BoxArg->getUses()) {
    if (isa<StrongReleaseInst>(O->getUser()) ||
        isa<DestroyValueInst>(O->getUser()))
      continue;
    
    if (auto Projection = dyn_cast<ProjectBoxInst>(O->getUser())) {
      Projections.push_back(Projection);
      continue;
    }
    
    return false;
  }

  // Only allow loads of projections, either directly or via
  // struct_element_addr instructions.
  //
  // TODO: This seems overly limited.  Why not projections of tuples and other
  // stuff?  Also, why not recursive struct elements?  This should be a helper
  // function that mirrors isNonEscapingUse.
  auto isAddrUseMutating = [](SILInstruction *AddrInst) {
    if (auto *SEAI = dyn_cast<StructElementAddrInst>(AddrInst)) {
      return all_of(SEAI->getUses(),
                    [](Operand *Op) -> bool {
                      return isNonMutatingLoad(Op->getUser());
                    });
    }

    return isNonMutatingLoad(AddrInst) || isa<DebugValueAddrInst>(AddrInst)
           || isa<MarkFunctionEscapeInst>(AddrInst)
           || isa<EndAccessInst>(AddrInst);
  };

  for (auto *Projection : Projections) {
    for (auto *UseOper : Projection->getUses()) {
      if (auto *Access = dyn_cast<BeginAccessInst>(UseOper->getUser())) {
        for (auto *AccessUseOper : Access->getUses()) {
          if (!isAddrUseMutating(AccessUseOper->getUser()))
            return false;
        }
        continue;
      }

      if (!isAddrUseMutating(UseOper->getUser()))
        return false;
    }
  }

  return true;
}

namespace {

class NonEscapingUserVisitor
    : public SILInstructionVisitor<NonEscapingUserVisitor, bool> {
  llvm::SmallVector<Operand *, 32> Worklist;
  llvm::SmallVectorImpl<SILInstruction *> &Mutations;
  NullablePtr<Operand> CurrentOp;

public:
  NonEscapingUserVisitor(Operand *Op,
                         llvm::SmallVectorImpl<SILInstruction *> &Mutations)
      : Worklist(), Mutations(Mutations), CurrentOp() {
    Worklist.push_back(Op);
  }

  NonEscapingUserVisitor(const NonEscapingUserVisitor &) = delete;
  NonEscapingUserVisitor &operator=(const NonEscapingUserVisitor &) = delete;
  NonEscapingUserVisitor(NonEscapingUserVisitor &&) = delete;
  NonEscapingUserVisitor &operator=(NonEscapingUserVisitor &&) = delete;

  bool compute() {
    while (!Worklist.empty()) {
      CurrentOp = Worklist.pop_back_val();
      SILInstruction *User = CurrentOp.get()->getUser();

      // Ignore type dependent operands.
      if (User->isTypeDependentOperand(*(CurrentOp.get())))
        continue;

      // Then visit the specific user. This routine returns true if the value
      // does not escape. In such a case, continue.
      if (visit(User)) {
        continue;
      }

      return false;
    }

    return true;
  }

  /// Visit a random value base.
  ///
  /// These are considered to be escapes.
  bool visitSILInstruction(SILInstruction *I) {
    DEBUG(llvm::dbgs() << "    FAIL! Have unknown escaping user: " << *I);
    return false;
  }

#define ALWAYS_NON_ESCAPING_INST(INST)                                         \
  bool visit##INST##Inst(INST##Inst *V) { return true; }
  // Marking the boxed value as escaping is OK. It's just a DI annotation.
  ALWAYS_NON_ESCAPING_INST(MarkFunctionEscape)
  // These remaining instructions are ok and don't count as mutations.
  ALWAYS_NON_ESCAPING_INST(StrongRetain)
  ALWAYS_NON_ESCAPING_INST(Load)
  ALWAYS_NON_ESCAPING_INST(StrongRelease)
  ALWAYS_NON_ESCAPING_INST(DestroyValue)
#undef ALWAYS_NON_ESCAPING_INST

  bool visitDeallocBoxInst(DeallocBoxInst *DBI) {
    Mutations.push_back(DBI);
    return true;
  }

  bool visitEndAccessInst(EndAccessInst *EAI) { return true; }

  bool visitApplyInst(ApplyInst *AI) {
    auto argIndex = CurrentOp.get()->getOperandNumber() - 1;
    SILFunctionConventions substConv(AI->getSubstCalleeType(), AI->getModule());
    auto convention = substConv.getSILArgumentConvention(argIndex);
    if (!convention.isIndirectConvention()) {
      DEBUG(llvm::dbgs() << "    FAIL! Found non indirect apply user: " << *AI);
      return false;
    }
    Mutations.push_back(AI);
    return true;
  }

  /// Add the Operands of a transitive use instruction to the worklist.
  void addUserOperandsToWorklist(SingleValueInstruction *I) {
    for (auto *User : I->getUses()) {
      Worklist.push_back(User);
    }
  }

  /// This is separate from the normal copy value handling since we are matching
  /// the old behavior of non-top-level uses not being able to have partial
  /// apply and project box uses.
  struct detail {
  enum IsMutating_t {
    IsNotMutating = 0,
    IsMutating = 1,
  };
  };
#define RECURSIVE_INST_VISITOR(MUTATING, INST)    \
  bool visit##INST##Inst(INST##Inst *I) {         \
    if (bool(detail::MUTATING)) {                 \
      Mutations.push_back(I);                     \
    }                                             \
    addUserOperandsToWorklist(I);                 \
    return true;                                  \
  }
  // *NOTE* It is important that we do not have copy_value here. The reason why
  // is that we only want to handle copy_value directly of the alloc_box without
  // going through any other instructions. This protects our optimization later
  // on.
  //
  // Additionally, copy_value is not a valid use of any of the instructions that
  // we allow through.
  //
  // TODO: Can we ever hit copy_values here? If we do, we may be missing
  // opportunities.
  RECURSIVE_INST_VISITOR(IsNotMutating, StructElementAddr)
  RECURSIVE_INST_VISITOR(IsNotMutating, TupleElementAddr)
  RECURSIVE_INST_VISITOR(IsNotMutating, InitEnumDataAddr)
  RECURSIVE_INST_VISITOR(IsNotMutating, OpenExistentialAddr)
  // begin_access may signify a modification, but is considered nonmutating
  // because we will peek though it's uses to find the actual mutation.
  RECURSIVE_INST_VISITOR(IsNotMutating, BeginAccess)
  RECURSIVE_INST_VISITOR(IsMutating   , UncheckedTakeEnumDataAddr)
#undef RECURSIVE_INST_VISITOR

  bool visitCopyAddrInst(CopyAddrInst *CAI) {
    if (CurrentOp.get()->getOperandNumber() == 1 || CAI->isTakeOfSrc())
      Mutations.push_back(CAI);
    return true;
  }

  bool visitStoreInst(StoreInst *SI) {
    if (CurrentOp.get()->getOperandNumber() != 1) {
      DEBUG(llvm::dbgs() << "    FAIL! Found store of pointer: " << *SI);
      return false;
    }
    Mutations.push_back(SI);
    return true;
  }

  bool visitAssignInst(AssignInst *AI) {
    if (CurrentOp.get()->getOperandNumber() != 1) {
      DEBUG(llvm::dbgs() << "    FAIL! Found store of pointer: " << *AI);
      return false;
    }
    Mutations.push_back(AI);
    return true;
  }
};

} // end anonymous namespace

namespace {

struct EscapeMutationScanningState {
  /// The list of mutations that we found while checking for escapes.
  llvm::SmallVector<SILInstruction *, 8> Mutations;

  /// A flag that we use to ensure that we only ever see 1 project_box on an
  /// alloc_box.
  bool SawProjectBoxInst;

  /// The global partial_apply -> index map.
  llvm::DenseMap<PartialApplyInst *, unsigned> &IM;
};

} // end anonymous namespace

/// \brief Given a use of an alloc_box instruction, return true if the use
/// definitely does not allow the box to escape; also, if the use is an
/// instruction which possibly mutates the contents of the box, then add it to
/// the Mutations vector.
static bool isNonEscapingUse(Operand *InitialOp,
                             EscapeMutationScanningState &State) {
  return NonEscapingUserVisitor(InitialOp, State.Mutations).compute();
}

bool isPartialApplyNonEscapingUser(Operand *CurrentOp, PartialApplyInst *PAI,
                                   EscapeMutationScanningState &State) {
  DEBUG(llvm::dbgs() << "    Found partial: " << *PAI);

  unsigned OpNo = CurrentOp->getOperandNumber();
  assert(OpNo != 0 && "Alloc box used as callee of partial apply?");

  // If we've already seen this partial apply, then it means the same alloc
  // box is being captured twice by the same closure, which is odd and
  // unexpected: bail instead of trying to handle this case.
  if (State.IM.count(PAI)) {
    DEBUG(llvm::dbgs() << "        FAIL! Already seen.\n");
    return false;
  }

  SILModule &M = PAI->getModule();
  auto closureType = PAI->getType().castTo<SILFunctionType>();
  SILFunctionConventions closureConv(closureType, M);

  // Calculate the index into the closure's argument list of the captured
  // box pointer (the captured address is always the immediately following
  // index so is not stored separately);
  unsigned Index = OpNo - 1 + closureConv.getNumSILArguments();

  auto *Fn = PAI->getReferencedFunction();
  if (!Fn || !Fn->isDefinition()) {
    DEBUG(llvm::dbgs() << "        FAIL! Not a direct function definition "
                          "reference.\n");
    return false;
  }

  SILArgument *BoxArg = getBoxFromIndex(Fn, Index);

  // For now, return false is the address argument is an address-only type,
  // since we currently handle loadable types only.
  // TODO: handle address-only types
  auto BoxTy = BoxArg->getType().castTo<SILBoxType>();
  assert(BoxTy->getLayout()->getFields().size() == 1 &&
         "promoting compound box not implemented yet");
  if (BoxTy->getFieldType(M, 0).isAddressOnly(M)) {
    DEBUG(llvm::dbgs() << "        FAIL! Box is an address only argument!\n");
    return false;
  }

  // Verify that this closure is known not to mutate the captured value; if
  // it does, then conservatively refuse to promote any captures of this
  // value.
  if (!isNonMutatingCapture(BoxArg)) {
    DEBUG(llvm::dbgs() << "        FAIL: Have a mutating capture!\n");
    return false;
  }

  // Record the index and continue.
  DEBUG(llvm::dbgs()
        << "        Partial apply does not escape, may be optimizable!\n");
  DEBUG(llvm::dbgs() << "        Index: " << Index << "\n");
  State.IM.insert(std::make_pair(PAI, Index));
  return true;
}

static bool isProjectBoxNonEscapingUse(ProjectBoxInst *PBI,
                                       EscapeMutationScanningState &State) {
  DEBUG(llvm::dbgs() << "    Found project box: " << *PBI);

  for (Operand *AddrOp : PBI->getUses()) {
    if (!isNonEscapingUse(AddrOp, State)) {
      DEBUG(llvm::dbgs() << "    FAIL! Has escaping user of addr:"
                         << *AddrOp->getUser());
      return false;
    }
  }

  return true;
}

static bool scanUsesForEscapesAndMutations(Operand *Op,
                                           EscapeMutationScanningState &State) {
  SILInstruction *User = Op->getUser();

  if (auto *PAI = dyn_cast<PartialApplyInst>(User)) {
    return isPartialApplyNonEscapingUser(Op, PAI, State);
  }

  if (auto *PBI = dyn_cast<ProjectBoxInst>(User)) {
    // It is assumed in later code that we will only have 1 project_box. This
    // can be seen since there is no code for reasoning about multiple
    // boxes. Just put in the restriction so we are consistent.
    if (State.SawProjectBoxInst)
      return false;
    State.SawProjectBoxInst = true;
    return isProjectBoxNonEscapingUse(PBI, State);
  }

  // Given a top level copy value use or mark_uninitialized, check all of its
  // user operands as if they were apart of the use list of the base operand.
  //
  // This is a separate code path from the non escaping user visitor check since
  // we want to be more conservative around non-top level copies (i.e. a copy
  // derived from a projection like instruction). In fact such a thing may not
  // even make any sense!
  if (isa<CopyValueInst>(User) || isa<MarkUninitializedInst>(User)) {
    return all_of(cast<SingleValueInstruction>(User)->getUses(),
                  [&State](Operand *UserOp) -> bool {
      return scanUsesForEscapesAndMutations(UserOp, State);
    });
  }

  // Verify that this use does not otherwise allow the alloc_box to
  // escape.
  return isNonEscapingUse(Op, State);
}

/// \brief Examine an alloc_box instruction, returning true if at least one
/// capture of the boxed variable is promotable.  If so, then the pair of the
/// partial_apply instruction and the index of the box argument in the closure's
/// argument list is added to IM.
static bool
examineAllocBoxInst(AllocBoxInst *ABI, ReachabilityInfo &RI,
                    llvm::DenseMap<PartialApplyInst *, unsigned> &IM) {
  DEBUG(llvm::dbgs() << "Visiting alloc box: " << *ABI);
  EscapeMutationScanningState State{{}, false, IM};

  // Scan the box for interesting uses.
  if (any_of(ABI->getUses(), [&State](Operand *Op) {
        return !scanUsesForEscapesAndMutations(Op, State);
      })) {
    DEBUG(llvm::dbgs()
          << "Found an escaping use! Can not optimize this alloc box?!\n");
    return false;
  }

  DEBUG(llvm::dbgs() << "We can optimize this alloc box!\n");

  // Helper lambda function to determine if instruction b is strictly after
  // instruction a, assuming both are in the same basic block.
  auto isAfter = [](SILInstruction *a, SILInstruction *b) {
    auto fIter = b->getParent()->begin();
    auto bIter = b->getIterator();
    auto aIter = a->getIterator();
    while (bIter != fIter) {
      --bIter;
      if (aIter == bIter)
        return true;
    }
    return false;
  };

  DEBUG(llvm::dbgs()
        << "Checking for any mutations that invalidate captures...\n");
  // Loop over all mutations to possibly invalidate captures.
  for (auto *I : State.Mutations) {
    auto Iter = IM.begin();
    while (Iter != IM.end()) {
      auto *PAI = Iter->first;
      // The mutation invalidates a capture if it occurs in a block reachable
      // from the block the partial_apply is in, or if it is in the same
      // block is after the partial_apply.
      if (RI.isReachable(PAI->getParent(), I->getParent()) ||
          (PAI->getParent() == I->getParent() && isAfter(PAI, I))) {
        DEBUG(llvm::dbgs() << "    Invalidating: " << *PAI);
        DEBUG(llvm::dbgs() << "    Because of user: " << *I);
        auto Prev = Iter++;
        IM.erase(Prev);
        continue;
      }
      ++Iter;
    }
    // If there are no valid captures left, then stop.
    if (IM.empty()) {
      DEBUG(llvm::dbgs() << "    Ran out of valid captures... bailing!\n");
      return false;
    }
  }

  DEBUG(llvm::dbgs() << "    We can optimize this box!\n");
  return true;
}

static SILFunction *
constructClonedFunction(PartialApplyInst *PAI, FunctionRefInst *FRI,
                        IndicesSet &PromotableIndices) {
  SILFunction *F = PAI->getFunction();

  // Create the Cloned Name for the function.
  SILFunction *Orig = FRI->getReferencedFunction();

  IsSerialized_t Serialized = IsNotSerialized;
  if (F->isSerialized() && Orig->isSerialized())
    Serialized = IsSerializable;

  auto ClonedName = getSpecializedName(Orig, Serialized, PromotableIndices);

  // If we already have such a cloned function in the module then just use it.
  if (auto *PrevF = F->getModule().lookUpFunction(ClonedName)) {
    assert(PrevF->isSerialized() == Serialized);
    return PrevF;
  }

  // Otherwise, create a new clone.
  ClosureCloner cloner(Orig, Serialized, ClonedName, PromotableIndices);
  cloner.populateCloned();
  return cloner.getCloned();
}

/// For an alloc_box or iterated copy_value alloc_box, get or create the
/// project_box for the copy or original alloc_box.
///
/// There are two possible case here:
///
/// 1. It could be an alloc box.
/// 2. It could be an iterated copy_value from an alloc_box.
///
/// Some important constraints from our initial safety condition checks:
///
/// 1. We only see a project_box paired with an alloc_box. e.x.:
///
///       (project_box (alloc_box)).
///
/// 2. We only see a mark_uninitialized when paired with an (alloc_box,
///    project_box). e.x.:
///
///       (mark_uninitialized (project_box (alloc_box)))
///
/// The asserts are to make sure that if the initial safety condition check
/// is changed, this code is changed as well.
static SILValue getOrCreateProjectBoxHelper(SILValue PartialOperand) {
  // If we have a copy_value, just create a project_box on the copy and return.
  if (auto *CVI = dyn_cast<CopyValueInst>(PartialOperand)) {
    SILBuilder B(std::next(CVI->getIterator()));
    return B.createProjectBox(CVI->getLoc(), CVI, 0);
  }

  // Otherwise, handle the alloc_box case. If we have a mark_uninitialized on
  // the box, we create the project value through that.
  SingleValueInstruction *Box = cast<AllocBoxInst>(PartialOperand);
  if (auto *Op = Box->getSingleUse()) {
    if (auto *MUI = dyn_cast<MarkUninitializedInst>(Op->getUser())) {
      Box = MUI;
    }
  }

  // Just return a project_box.
  SILBuilder B(std::next(Box->getIterator()));
  return B.createProjectBox(Box->getLoc(), Box, 0);
}

/// \brief Given a partial_apply instruction and a set of promotable indices,
/// clone the closure with the promoted captures and replace the partial_apply
/// with a partial_apply of the new closure, fixing up reference counting as
/// necessary. Also, if the closure is cloned, the cloned function is added to
/// the worklist.
static SILFunction *
processPartialApplyInst(PartialApplyInst *PAI, IndicesSet &PromotableIndices,
                        SmallVectorImpl<SILFunction*> &Worklist) {
  SILModule &M = PAI->getModule();

  auto *FRI = dyn_cast<FunctionRefInst>(PAI->getCallee());

  // Clone the closure with the given promoted captures.
  SILFunction *ClonedFn = constructClonedFunction(PAI, FRI, PromotableIndices);
  Worklist.push_back(ClonedFn);

  // Initialize a SILBuilder and create a function_ref referencing the cloned
  // closure.
  SILBuilderWithScope B(PAI);
  B.addOpenedArchetypeOperands(PAI);
  SILValue FnVal = B.createFunctionRef(PAI->getLoc(), ClonedFn);

  // Populate the argument list for a new partial_apply instruction, taking into
  // consideration any captures.
  auto CalleeFunctionTy = PAI->getCallee()->getType().castTo<SILFunctionType>();
  auto SubstCalleeFunctionTy = CalleeFunctionTy;
  if (PAI->hasSubstitutions())
    SubstCalleeFunctionTy =
        CalleeFunctionTy->substGenericArgs(M, PAI->getSubstitutions());
  SILFunctionConventions calleeConv(SubstCalleeFunctionTy, M);
  auto CalleePInfo = SubstCalleeFunctionTy->getParameters();
  SILFunctionConventions paConv(PAI->getType().castTo<SILFunctionType>(), M);
  unsigned FirstIndex = paConv.getNumSILArguments();
  unsigned OpNo = 1;
  unsigned OpCount = PAI->getNumOperands() - PAI->getNumTypeDependentOperands();
  SmallVector<SILValue, 16> Args;
  auto NumIndirectResults = calleeConv.getNumIndirectSILResults();
  for (; OpNo != OpCount; ++OpNo) {
    unsigned Index = OpNo - 1 + FirstIndex;
    if (!PromotableIndices.count(Index)) {
      Args.push_back(PAI->getOperand(OpNo));
      continue;
    }

    // First the grab the box and projected_box for the box value.
    //
    // *NOTE* Box may be a copy_value.
    SILValue Box = PAI->getOperand(OpNo);
    SILValue Addr = getOrCreateProjectBoxHelper(Box);

    auto &typeLowering = M.getTypeLowering(Addr->getType());
    Args.push_back(
        typeLowering.emitLoadOfCopy(B, PAI->getLoc(), Addr, IsNotTake));

    // Cleanup the captured argument.
    //
    // *NOTE* If we initially had a box, then this is on the actual
    // alloc_box. Otherwise, it is on the specific iterated copy_value that we
    // started with.
    SILParameterInfo CPInfo = CalleePInfo[Index - NumIndirectResults];
    assert(calleeConv.getSILType(CPInfo) == Box->getType() &&
           "SILType of parameter info does not match type of parameter");
    releasePartialApplyCapturedArg(B, PAI->getLoc(), Box, CPInfo);
    ++NumCapturesPromoted;
  }

  // Create a new partial apply with the new arguments.
  auto *NewPAI = B.createPartialApply(
      PAI->getLoc(), FnVal, PAI->getSubstitutions(), Args,
      PAI->getType().getAs<SILFunctionType>()->getCalleeConvention());
  PAI->replaceAllUsesWith(NewPAI);
  PAI->eraseFromParent();
  if (FRI->use_empty()) {
    FRI->eraseFromParent();
    // TODO: If this is the last use of the closure, and if it has internal
    // linkage, we should remove it from the SILModule now.
  }
  return ClonedFn;
}

static void
constructMapFromPartialApplyToPromotableIndices(SILFunction *F,
                                                PartialApplyIndicesMap &Map) {
  ReachabilityInfo RS(F);

  // This is a map from each partial apply to a single index which is a
  // promotable box variable for the alloc_box currently being considered.
  llvm::DenseMap<PartialApplyInst*, unsigned> IndexMap;

  // Consider all alloc_box instructions in the function.
  for (auto &BB : *F) {
    for (auto &I : BB) {
      if (auto *ABI = dyn_cast<AllocBoxInst>(&I)) {
        IndexMap.clear();
        if (examineAllocBoxInst(ABI, RS, IndexMap)) {
          // If we are able to promote at least one capture of the alloc_box,
          // then add the promotable index to the main map.
          for (auto &IndexPair : IndexMap)
            Map[IndexPair.first].insert(IndexPair.second);
        }
        DEBUG(llvm::dbgs() << "\n");
      }
    }
  }
}

namespace {

class CapturePromotionPass : public SILModuleTransform {
  /// The entry point to the transformation.
  void run() override {
    SmallVector<SILFunction*, 128> Worklist;
    for (auto &F : *getModule()) {
      processFunction(&F, Worklist);
    }

    while (!Worklist.empty()) {
      processFunction(Worklist.pop_back_val(), Worklist);
    }
  }

  void processFunction(SILFunction *F, SmallVectorImpl<SILFunction*> &Worklist);

};

} // end anonymous namespace

void CapturePromotionPass::processFunction(SILFunction *F,
                                      SmallVectorImpl<SILFunction*> &Worklist) {
  DEBUG(llvm::dbgs() << "******** Performing Capture Promotion on: "
                     << F->getName() << "********\n");
  // This is a map from each partial apply to a set of indices of promotable
  // box variables.
  PartialApplyIndicesMap IndicesMap;
  constructMapFromPartialApplyToPromotableIndices(F, IndicesMap);

  // Do the actual promotions; all promotions on a single partial_apply are
  // handled together.
  for (auto &IndicesPair : IndicesMap) {
    PartialApplyInst *PAI = IndicesPair.first;
    SILFunction *ClonedFn = processPartialApplyInst(PAI, IndicesPair.second,
                                                    Worklist);
    notifyAddFunction(ClonedFn);
  }
  invalidateAnalysis(F, SILAnalysis::InvalidationKind::Everything);
}

SILTransform *swift::createCapturePromotion() {
  return new CapturePromotionPass();
}
