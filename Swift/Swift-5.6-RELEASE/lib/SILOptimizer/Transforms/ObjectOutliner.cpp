//===--- ObjectOutliner.cpp - Outline heap objects -----------------------===//
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

#define DEBUG_TYPE "objectoutliner"
#include "swift/AST/ASTMangler.h"
#include "swift/AST/SemanticAttrs.h"
#include "swift/SIL/DebugUtils.h"
#include "swift/SIL/SILBuilder.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/SILOptimizer/Utils/BasicBlockOptUtils.h"
#include "swift/SILOptimizer/Utils/SILOptFunctionBuilder.h"
#include "llvm/Support/Debug.h"
using namespace swift;

namespace {

class ObjectOutliner {
  SILOptFunctionBuilder &FunctionBuilder;
  NominalTypeDecl *ArrayDecl = nullptr;
  int GlobIdx = 0;

  // Instructions to be deleted.
  llvm::SmallVector<SILInstruction *, 4> ToRemove;

  bool isCOWType(SILType type) {
    return type.getNominalOrBoundGenericNominal() == ArrayDecl;
  }

  bool isValidUseOfObject(SILInstruction *Val, EndCOWMutationInst *toIgnore);

  ApplyInst *findFindStringCall(SILValue V);

  bool getObjectInitVals(SILValue Val,
                         llvm::DenseMap<VarDecl *, StoreInst *> &MemberStores,
                         llvm::SmallVectorImpl<StoreInst *> &TailStores,
                         unsigned NumTailTupleElements,
                         EndCOWMutationInst *toIgnore);
  bool handleTailAddr(int TailIdx, SILInstruction *I, unsigned NumTailTupleElements,
                      llvm::SmallVectorImpl<StoreInst *> &TailStores,
                      EndCOWMutationInst *toIgnore);

  bool optimizeObjectAllocation(AllocRefInst *ARI);
  void replaceFindStringCall(ApplyInst *FindStringCall);

public:
  ObjectOutliner(SILOptFunctionBuilder &FunctionBuilder,
                 NominalTypeDecl *ArrayDecl)
      : FunctionBuilder(FunctionBuilder), ArrayDecl(ArrayDecl) { }

  bool run(SILFunction *F);
};

bool ObjectOutliner::run(SILFunction *F) {
  bool hasChanged = false;

  for (auto &BB : *F) {
    auto Iter = BB.begin();

    while (Iter != BB.end()) {
      SILInstruction *I = &*Iter;
      ++Iter;
      if (auto *ARI = dyn_cast<AllocRefInst>(I)) {
        unsigned GarbageSize = ToRemove.size();

        // Try to replace the alloc_ref with a static object.
        if (optimizeObjectAllocation(ARI)) {
          hasChanged = true;
        } else {
          // No transformation was made. Restore the original state of the garbage list.
          assert(GarbageSize <= ToRemove.size());
          ToRemove.resize(GarbageSize);
        }
      }
    }
    // Delaying the deallocation of instructions avoids problems with iterator invalidation in the
    // instruction loop above.
    for (auto *I : ToRemove)
      I->eraseFromParent();
    ToRemove.clear();
  }
  return hasChanged;
}

/// Get all stored properties of a class, including it's super classes.
static void getFields(ClassDecl *Cl, SmallVectorImpl<VarDecl *> &Fields) {
  if (ClassDecl *SuperCl = Cl->getSuperclassDecl()) {
    getFields(SuperCl, Fields);
  }
  for (VarDecl *Field : Cl->getStoredProperties()) {
    Fields.push_back(Field);
  }
}

/// Check if \p V is a valid instruction for a static initializer, including
/// all its operands.
static bool isValidInitVal(SILValue V) {
  if (auto I = dyn_cast<SingleValueInstruction>(V)) {
    if (!SILGlobalVariable::isValidStaticInitializerInst(I, I->getModule()))
      return false;

    for (Operand &Op : I->getAllOperands()) {
      if (!isValidInitVal(Op.get()))
        return false;
    }
    return true;
  }
  return false;
}

/// Check if a use of an object may prevent outlining the object.
bool ObjectOutliner::isValidUseOfObject(SILInstruction *I,
                                        EndCOWMutationInst *toIgnore) {
  if (I == toIgnore)
    return true;

  switch (I->getKind()) {
  case SILInstructionKind::DebugValueInst:
  case SILInstructionKind::LoadInst:
  case SILInstructionKind::DeallocRefInst:
  case SILInstructionKind::StrongRetainInst:
  case SILInstructionKind::StrongReleaseInst:
  case SILInstructionKind::FixLifetimeInst:
  case SILInstructionKind::SetDeallocatingInst:
    return true;

  case SILInstructionKind::StructElementAddrInst:
  case SILInstructionKind::AddressToPointerInst:
  case SILInstructionKind::StructInst:
  case SILInstructionKind::TupleInst:
  case SILInstructionKind::TupleExtractInst:
  case SILInstructionKind::EnumInst:
  case SILInstructionKind::StructExtractInst:
  case SILInstructionKind::UncheckedRefCastInst:
  case SILInstructionKind::UpcastInst: {
    auto SVI = cast<SingleValueInstruction>(I);
    for (Operand *Use : getNonDebugUses(SVI)) {
      if (!isValidUseOfObject(Use->getUser(), toIgnore))
        return false;
    }
    return true;
  }

  case SILInstructionKind::BuiltinInst: {
    // Handle the case for comparing addresses. This occurs when the Array
    // comparison function is inlined.
    auto *BI = cast<BuiltinInst>(I);
    BuiltinValueKind K = BI->getBuiltinInfo().ID;
    if (K == BuiltinValueKind::ICMP_EQ || K == BuiltinValueKind::ICMP_NE)
      return true;
    if (K == BuiltinValueKind::DestroyArray) {
      // We must not try to delete the tail allocated values. Although this would be a no-op
      // (because we only handle trivial types), it would be semantically wrong to apply this
      // builtin on the outlined object.
      ToRemove.push_back(BI);
      return true;
    }
    return false;
  }

  default:
    return false;
  }
}

/// Finds a call to findStringSwitchCase in the uses of \p V.
ApplyInst *ObjectOutliner::findFindStringCall(SILValue V) {
  for (Operand *use : V->getUses()) {
    SILInstruction *user = use->getUser();
    switch (user->getKind()) {
    case SILInstructionKind::ApplyInst:
      // There should only be a single call to findStringSwitchCase. But even
      // if there are multiple calls, it's not problem - we'll just optimize the
      // last one we find.
      if (cast<ApplyInst>(user)->hasSemantics(semantics::FIND_STRING_SWITCH_CASE))
        return cast<ApplyInst>(user);
      break;

    case SILInstructionKind::StructInst:
    case SILInstructionKind::TupleInst:
    case SILInstructionKind::UncheckedRefCastInst:
    case SILInstructionKind::UpcastInst: {
      if (ApplyInst *foundCall =
           findFindStringCall(cast<SingleValueInstruction>(user))) {
        return foundCall;
      }
      break;
    }

    default:
      break;
    }
  }
  return nullptr;
}

/// Handle the address of a tail element.
bool ObjectOutliner::handleTailAddr(int TailIdx, SILInstruction *TailAddr,
                                    unsigned NumTailTupleElements,
                                    llvm::SmallVectorImpl<StoreInst *> &TailStores,
                                    EndCOWMutationInst *toIgnore) {
  if (NumTailTupleElements > 0) {
    if (auto *TEA = dyn_cast<TupleElementAddrInst>(TailAddr)) {
      unsigned TupleIdx = TEA->getFieldIndex();
      assert(TupleIdx < NumTailTupleElements);
      for (Operand *Use : TEA->getUses()) {
        if (!handleTailAddr(TailIdx * NumTailTupleElements + TupleIdx, Use->getUser(), 0,
                            TailStores, toIgnore))
          return false;
      }
      return true;
    }
  } else {
    if (TailIdx >= 0 && TailIdx < (int)TailStores.size()) {
      if (auto *SI = dyn_cast<StoreInst>(TailAddr)) {
        if (!isValidInitVal(SI->getSrc()) || TailStores[TailIdx])
          return false;
        TailStores[TailIdx] = SI;
        return true;
      }
    }
  }
  return isValidUseOfObject(TailAddr, toIgnore);
}

/// Get the init values for an object's stored properties and its tail elements.
bool ObjectOutliner::getObjectInitVals(SILValue Val,
                        llvm::DenseMap<VarDecl *, StoreInst *> &MemberStores,
                        llvm::SmallVectorImpl<StoreInst *> &TailStores,
                        unsigned NumTailTupleElements,
                        EndCOWMutationInst *toIgnore) {
  for (Operand *Use : Val->getUses()) {
    SILInstruction *User = Use->getUser();
    if (auto *UC = dyn_cast<UpcastInst>(User)) {
      // Upcast is transparent.
      if (!getObjectInitVals(UC, MemberStores, TailStores, NumTailTupleElements,
                             toIgnore))
        return false;
    } else if (auto *REA = dyn_cast<RefElementAddrInst>(User)) {
      // The address of a stored property.
      for (Operand *ElemAddrUse : REA->getUses()) {
        SILInstruction *ElemAddrUser = ElemAddrUse->getUser();
        if (auto *SI = dyn_cast<StoreInst>(ElemAddrUser)) {
          if (!isValidInitVal(SI->getSrc()) || MemberStores[REA->getField()])
            return false;
          MemberStores[REA->getField()] = SI;
        } else if (!isValidUseOfObject(ElemAddrUser, toIgnore)) {
          return false;
        }
      }
    } else if (auto *RTA = dyn_cast<RefTailAddrInst>(User)) {
      // The address of a tail element.
      for (Operand *TailUse : RTA->getUses()) {
        SILInstruction *TailUser = TailUse->getUser();
        if (auto *IA = dyn_cast<IndexAddrInst>(TailUser)) {
          // An index_addr yields the address of any tail element. Only if the
          // second operand (the index) is an integer literal we can figure out
          // which tail element is refereneced.
          int TailIdx = -1;
          if (auto *Index = dyn_cast<IntegerLiteralInst>(IA->getIndex()))
            TailIdx = Index->getValue().getZExtValue();

          for (Operand *IAUse : IA->getUses()) {
            if (!handleTailAddr(TailIdx, IAUse->getUser(), NumTailTupleElements,
                                TailStores, toIgnore))
              return false;
          }
        // Without an index_addr it's the first tail element.
        } else if (!handleTailAddr(/*TailIdx*/0, TailUser, NumTailTupleElements,
                                   TailStores, toIgnore)) {
          return false;
        }
      }
    } else if (!isValidUseOfObject(User, toIgnore)) {
      return false;
    }
  }
  return true;
}

class GlobalVariableMangler : public Mangle::ASTMangler {
public:
  std::string mangleOutlinedVariable(SILFunction *F, int &uniqueIdx) {
    std::string GlobName;
    do {
      beginManglingWithoutPrefix();
      appendOperator(F->getName());
      appendOperator("Tv", Index(uniqueIdx++));
      GlobName = finalize();
    } while (F->getModule().lookUpGlobalVariable(GlobName));

    return GlobName;
  }
};

static EndCOWMutationInst *getEndCOWMutation(SILValue object) {
  for (Operand *use : object->getUses()) {
    SILInstruction *user = use->getUser();
    if (auto *upCast = dyn_cast<UpcastInst>(user)) {
      // Look through upcast instructions.
      if (EndCOWMutationInst *ecm = getEndCOWMutation(upCast))
        return ecm;
    } else if (auto *ecm = dyn_cast<EndCOWMutationInst>(use->getUser())) {
      return ecm;
    }
  }
  return nullptr;
}

/// Try to convert an object allocation into a statically initialized object.
///
/// In general this works for any class, but in practice it will only kick in
/// for copy-on-write buffers, like array buffer objects.
/// The use cases are array literals in a function.
/// For example:
///     func getarray() -> [Int] {
///       return [1, 2, 3]
///     }
bool ObjectOutliner::optimizeObjectAllocation(AllocRefInst *ARI) {
  if (ARI->isObjC())
    return false;

  // Find the end_cow_mutation. Only for such COW buffer objects we do the
  // transformation.
  EndCOWMutationInst *endCOW = getEndCOWMutation(ARI);
  if (!endCOW || endCOW->doKeepUnique())
    return false;

  // Check how many tail allocated elements are on the object.
  ArrayRef<Operand> TailCounts = ARI->getTailAllocatedCounts();
  SILType TailType;
  unsigned NumTailElems = 0;

  // We only support a single tail allocated arrays.
  // Stdlib's tail allocated arrays don't have any side-effects in the
  // constructor if the element type is trivial.
  // TODO: also exclude custom tail allocated arrays which might have
  // side-effects in the destructor.
  if (TailCounts.size() != 1)
      return false;

  // The number of tail allocated elements must be constant.
  if (auto *ILI = dyn_cast<IntegerLiteralInst>(TailCounts[0].get())) {
    if (ILI->getValue().getActiveBits() > 20)
      return false;
    NumTailElems = ILI->getValue().getZExtValue();
    TailType = ARI->getTailAllocatedTypes()[0];
  } else {
    return false;
  }

  SILType Ty = ARI->getType();
  ClassDecl *Cl = Ty.getClassOrBoundGenericClass();
  if (!Cl)
    return false;
  llvm::SmallVector<VarDecl *, 16> Fields;
  getFields(Cl, Fields);

  llvm::DenseMap<VarDecl *, StoreInst *> MemberStores;

  // A store for each element of the tail allocated array. In case of a tuple, there is a store
  // for each tuple element. For example, a 3 element array of 2-element tuples
  //     [ (i0, i1), (i2, i3), (i4, i5) ]
  // results in following store instructions, collected in TailStores:
  //     [ store i0, store i1, store i2, store i3, store i4, store i5 ]
  llvm::SmallVector<StoreInst *, 16> TailStores;

  unsigned NumStores = NumTailElems;
  unsigned NumTailTupleElems = 0;
  if (auto Tuple = TailType.getAs<TupleType>()) {
    NumTailTupleElems = Tuple->getNumElements();
    if (NumTailTupleElems == 0)
      return false;
    NumStores *= NumTailTupleElems;
  }

  TailStores.resize(NumStores);

  // Get the initialization stores of the object's properties and tail
  // allocated elements. Also check if there are any "bad" uses of the object.
  if (!getObjectInitVals(ARI, MemberStores, TailStores, NumTailTupleElems,
                         endCOW)) {
    return false;
  }

  // Is there a store for all the class properties?
  if (MemberStores.size() != Fields.size())
    return false;

  // Is there a store for all tail allocated elements?
  for (auto V : TailStores) {
    if (!V)
      return false;
  }

  LLVM_DEBUG(llvm::dbgs() << "Outline global variable in "
                          << ARI->getFunction()->getName() << '\n');

  SILModule *Module = &ARI->getFunction()->getModule();
  // FIXME: Expansion
  assert(!Cl->isResilient(Module->getSwiftModule(),
                          ResilienceExpansion::Minimal) &&
    "constructor call of resilient class should prevent static allocation");

  // Create a name for the outlined global variable.
  GlobalVariableMangler Mangler;
  std::string GlobName =
    Mangler.mangleOutlinedVariable(ARI->getFunction(), GlobIdx);

  SILGlobalVariable *Glob =
    SILGlobalVariable::create(*Module, SILLinkage::Private, IsNotSerialized,
                              GlobName, ARI->getType());

  // Schedule all init values for cloning into the initializer of Glob.
  StaticInitCloner Cloner(Glob);
  for (VarDecl *Field : Fields) {
    StoreInst *MemberStore = MemberStores[Field];
    Cloner.add(cast<SingleValueInstruction>(MemberStore->getSrc()));
  }
  for (StoreInst *TailStore : TailStores) {
    Cloner.add(cast<SingleValueInstruction>(TailStore->getSrc()));
  }

  // Create the class property initializers
  llvm::SmallVector<SILValue, 16> ObjectArgs;
  for (VarDecl *Field : Fields) {
    StoreInst *MemberStore = MemberStores[Field];
    assert(MemberStore);
    ObjectArgs.push_back(Cloner.clone(
                           cast<SingleValueInstruction>(MemberStore->getSrc())));
    ToRemove.push_back(MemberStore);
  }
  unsigned NumBaseElements = ObjectArgs.size();

  // Create the initializers for the tail elements.
  if (NumTailTupleElems == 0) {
    // The non-tuple element case.
    for (StoreInst *TailStore : TailStores) {
      ObjectArgs.push_back(Cloner.clone(
                             cast<SingleValueInstruction>(TailStore->getSrc())));
      ToRemove.push_back(TailStore);
    }
  } else {
    // The elements are tuples: combine NumTailTupleElems elements from TailStores to a single tuple
    // instruction.
    for (unsigned EIdx = 0; EIdx < NumTailElems; EIdx++) {
      SmallVector<SILValue, 8> TupleElems;
      for (unsigned TIdx = 0; TIdx < NumTailTupleElems; TIdx++) {
        StoreInst *TailStore = TailStores[EIdx * NumTailTupleElems + TIdx];
        SILValue V = Cloner.clone(cast<SingleValueInstruction>(TailStore->getSrc()));
        TupleElems.push_back(V);
        ToRemove.push_back(TailStore);
      }
      auto *TI = Cloner.getBuilder().createTuple(ARI->getLoc(), TailType, TupleElems);
      ObjectArgs.push_back(TI);
    }
  }

  // Create the initializer for the object itself.
  SILBuilder StaticInitBuilder(Glob);
  StaticInitBuilder.createObject(ArtificialUnreachableLocation(),
                                 ARI->getType(), ObjectArgs, NumBaseElements);

  // Replace the alloc_ref by global_value + strong_retain instructions.
  SILBuilder B(ARI);
  GlobalValueInst *GVI = B.createGlobalValue(ARI->getLoc(), Glob);
  B.createStrongRetain(ARI->getLoc(), GVI, B.getDefaultAtomicity());

  ApplyInst *FindStringCall = findFindStringCall(endCOW);
  endCOW->replaceAllUsesWith(endCOW->getOperand());
  ToRemove.push_back(endCOW);

  llvm::SmallVector<Operand *, 8> Worklist(ARI->use_begin(), ARI->use_end());
  while (!Worklist.empty()) {
    auto *Use = Worklist.pop_back_val();
    SILInstruction *User = Use->getUser();
    switch (User->getKind()) {
      case SILInstructionKind::SetDeallocatingInst:
        // set_deallocating is a replacement for a strong_release. Therefore
        // we have to insert a strong_release to balance the strong_retain which
        // we inserted after the global_value instruction.
        B.setInsertionPoint(User);
        B.createStrongRelease(User->getLoc(), GVI, B.getDefaultAtomicity());
        LLVM_FALLTHROUGH;
      case SILInstructionKind::DeallocRefInst:
        ToRemove.push_back(User);
        break;
      default:
        Use->set(GVI);
    }
  }
  if (FindStringCall && NumTailElems > 16) {
    assert(&*std::next(ARI->getIterator()) != FindStringCall &&
           "FindStringCall must not be the next instruction after ARI because "
           "deleting it would invalidate the instruction iterator");
    replaceFindStringCall(FindStringCall);
  }

  ToRemove.push_back(ARI);
  return true;
}

/// Replaces a call to _findStringSwitchCase with a call to
/// _findStringSwitchCaseWithCache which builds a cache (e.g. a Dictionary) and
/// stores it into a global variable. Then subsequent calls to this function can
/// do a fast lookup using the cache.
void ObjectOutliner::replaceFindStringCall(ApplyInst *FindStringCall) {
  // Find the replacement function in the swift stdlib.
  SmallVector<ValueDecl *, 1> results;
  auto &F = *FindStringCall->getFunction();
  SILModule *Module = &F.getModule();
  Module->getASTContext().lookupInSwiftModule("_findStringSwitchCaseWithCache",
                                              results);
  if (results.size() != 1)
    return;

  auto *FD = dyn_cast<FuncDecl>(results.front());
  if (!FD)
    return;

  SILDeclRef declRef(FD, SILDeclRef::Kind::Func);
  SILFunction *replacementFunc = FunctionBuilder.getOrCreateFunction(
      FindStringCall->getLoc(), declRef, NotForDefinition);

  SILFunctionType *FTy = replacementFunc->getLoweredFunctionType();
  if (FTy->getNumParameters() != 3)
    return;

  SILType cacheType =
      FTy->getParameters()[2]
          .getSILStorageType(*Module, FTy, TypeExpansionContext::minimal())
          .getObjectType();
  NominalTypeDecl *cacheDecl = cacheType.getNominalOrBoundGenericNominal();
  if (!cacheDecl)
    return;


  // FIXME: Expansion
  assert(!cacheDecl->isResilient(Module->getSwiftModule(),
                                 ResilienceExpansion::Minimal));

  SILType wordTy =
      cacheType.getFieldType(cacheDecl->getStoredProperties().front(), *Module,
                             F.getTypeExpansionContext());

  GlobalVariableMangler Mangler;
  std::string GlobName =
    Mangler.mangleOutlinedVariable(FindStringCall->getFunction(), GlobIdx);

  // Create an "opaque" global variable which is passed as inout to
  // _findStringSwitchCaseWithCache and into which the function stores the
  // "cache".
  SILGlobalVariable *CacheVar =
    SILGlobalVariable::create(*Module, SILLinkage::Private, IsNotSerialized,
                              GlobName, cacheType);

  SILLocation Loc = FindStringCall->getLoc();
  SILBuilder StaticInitBuilder(CacheVar);
  auto *Zero = StaticInitBuilder.createIntegerLiteral(Loc, wordTy, 0);
  StaticInitBuilder.createStruct(ArtificialUnreachableLocation(), cacheType,
                                 {Zero, Zero});

  SILBuilder B(FindStringCall);
  GlobalAddrInst *CacheAddr = B.createGlobalAddr(FindStringCall->getLoc(),
                                                 CacheVar);
  FunctionRefInst *FRI = B.createFunctionRef(FindStringCall->getLoc(),
                                             replacementFunc);
  ApplyInst *NewCall = B.createApply(FindStringCall->getLoc(), FRI,
                                     FindStringCall->getSubstitutionMap(),
                                     { FindStringCall->getArgument(0),
                                       FindStringCall->getArgument(1),
                                       CacheAddr },
                                     FindStringCall->getApplyOptions());

  FindStringCall->replaceAllUsesWith(NewCall);
  FindStringCall->eraseFromParent();
}

class ObjectOutlinerPass : public SILFunctionTransform {
  void run() override {
    SILFunction *F = getFunction();
    SILOptFunctionBuilder FuncBuilder(*this);
    ObjectOutliner Outliner(FuncBuilder,
                            F->getModule().getASTContext().getArrayDecl());
    if (Outliner.run(F)) {
      invalidateAnalysis(SILAnalysis::InvalidationKind::Instructions);
    }
  }
};

} // end anonymous namespace

SILTransform *swift::createObjectOutliner() {
  return new ObjectOutlinerPass();
}
