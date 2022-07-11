//===--- SILModule.cpp - SILModule implementation -------------------------===//
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

#define DEBUG_TYPE "sil-module"
#include "swift/SIL/SILModule.h"
#include "Linker.h"
#include "swift/AST/ASTMangler.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/ClangImporter/ClangModule.h"
#include "swift/SIL/FormalLinkage.h"
#include "swift/SIL/Notifications.h"
#include "swift/SIL/SILDebugScope.h"
#include "swift/SIL/SILRemarkStreamer.h"
#include "swift/SIL/SILValue.h"
#include "swift/SIL/SILVisitor.h"
#include "swift/Serialization/SerializedSILLoader.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/YAMLTraits.h"
#include <functional>
using namespace swift;
using namespace Lowering;

STATISTIC(NumSlabsAllocated, "number of slabs allocated in SILModule");

class SILModule::SerializationCallback final
    : public DeserializationNotificationHandler {
  void didDeserialize(ModuleDecl *M, SILFunction *fn) override {
    updateLinkage(fn);
  }

  void didDeserialize(ModuleDecl *M, SILGlobalVariable *var) override {
    updateLinkage(var);
    
    // For globals we currently do not support available_externally.
    // In the interpreter it would result in two instances for a single global:
    // one in the imported module and one in the main module.
    var->setDeclaration(true);
  }

  void didDeserialize(ModuleDecl *M, SILVTable *vtable) override {
    // TODO: should vtables get linkage?
    //updateLinkage(vtable);
  }

  void didDeserialize(ModuleDecl *M, SILWitnessTable *wt) override {
    updateLinkage(wt);
  }

  template <class T> void updateLinkage(T *decl) {
    switch (decl->getLinkage()) {
    case SILLinkage::Public:
      decl->setLinkage(SILLinkage::PublicExternal);
      return;
    case SILLinkage::PublicNonABI:
      // PublicNonABI functions receive SharedExternal linkage, so that
      // they have "link once" semantics when deserialized by multiple
      // translation units in the same Swift module.
      decl->setLinkage(SILLinkage::SharedExternal);
      return;
    case SILLinkage::Hidden:
      decl->setLinkage(SILLinkage::HiddenExternal);
      return;
    case SILLinkage::Shared:
      decl->setLinkage(SILLinkage::SharedExternal);
      return;
    case SILLinkage::Private:
      llvm_unreachable("cannot make a private external symbol");
    case SILLinkage::PublicExternal:
    case SILLinkage::HiddenExternal:
    case SILLinkage::SharedExternal:
      return;
    }
  }

  StringRef getName() const override {
    return "SILModule::SerializationCallback";
  }
};

SILModule::SILModule(llvm::PointerUnion<FileUnit *, ModuleDecl *> context,
                     Lowering::TypeConverter &TC, const SILOptions &Options)
    : Stage(SILStage::Raw), indexTrieRoot(new IndexTrieNode()),
      Options(Options), serialized(false),
      regDeserializationNotificationHandlerForNonTransparentFuncOME(false),
      regDeserializationNotificationHandlerForAllFuncOME(false),
      prespecializedFunctionDeclsImported(false), SerializeSILAction(),
      Types(TC) {
  assert(!context.isNull());
  if (auto *file = context.dyn_cast<FileUnit *>()) {
    AssociatedDeclContext = file;
  } else {
    AssociatedDeclContext = context.get<ModuleDecl *>();
  }
  TheSwiftModule = AssociatedDeclContext->getParentModule();

  // We always add the base SILModule serialization callback.
  std::unique_ptr<DeserializationNotificationHandler> callback(
      new SILModule::SerializationCallback());
  deserializationNotificationHandlers.add(std::move(callback));
}

SILModule::~SILModule() {
#ifndef NDEBUG
  checkForLeaks();

  NumSlabsAllocated += numAllocatedSlabs;
  assert(numAllocatedSlabs == freeSlabs.size() && "leaking slabs in SILModule");
#endif

  assert(!hasUnresolvedOpenedArchetypeDefinitions());

  // Decrement ref count for each SILGlobalVariable with static initializers.
  for (SILGlobalVariable &v : silGlobals) {
    v.clear();
  }

  for (auto vt : vtables)
    vt->~SILVTable();

  // Drop everything functions in this module reference.
  //
  // This is necessary since the functions may reference each other.  We don't
  // need to worry about sil_witness_tables since witness tables reference each
  // other via protocol conformances and sil_vtables don't reference each other
  // at all.
  for (SILFunction &F : *this) {
    F.dropAllReferences();
    F.dropDynamicallyReplacedFunction();
    F.clearSpecializeAttrs();
  }

  for (SILFunction &F : *this) {
    F.eraseAllBlocks();
  }
  flushDeletedInsts();
}

void SILModule::checkForLeaks() const {

  /// Leak checking is not thread safe, because the instruction counters are
  /// global non-atomic variables. Leak checking can only be done in case there
  /// is a single SILModule in a single thread.
  if (!getOptions().checkSILModuleLeaks)
    return;

  int instsInModule = std::distance(scheduledForDeletion.begin(),
                                    scheduledForDeletion.end());
  for (const SILFunction &F : *this) {
    for (const SILBasicBlock &block : F) {
      instsInModule += std::distance(block.begin(), block.end());
    }
  }
  for (const SILFunction &F : zombieFunctions) {
    for (const SILBasicBlock &block : F) {
      instsInModule += std::distance(block.begin(), block.end());
    }
  }
  for (const SILGlobalVariable &global : getSILGlobals()) {
      instsInModule += std::distance(global.StaticInitializerBlock.begin(),
                                     global.StaticInitializerBlock.end());
  }
  
  int numAllocated = SILInstruction::getNumCreatedInstructions() -
                       SILInstruction::getNumDeletedInstructions();
                       
  if (numAllocated != instsInModule) {
    llvm::errs() << "Leaking instructions!\n";
    llvm::errs() << "Alloated instructions: " << numAllocated << '\n';
    llvm::errs() << "Instructions in module: " << instsInModule << '\n';
    llvm_unreachable("leaking instructions");
  }
  
  assert(PlaceholderValue::getNumPlaceholderValuesAlive() == 0 &&
         "leaking placeholders");
}

void SILModule::checkForLeaksAfterDestruction() {
// Disabled in release (non-assert) builds because this check fails in rare
// cases in lldb, causing crashes. rdar://70826934
#ifndef NDEBUG
  int numAllocated = SILInstruction::getNumCreatedInstructions() -
                     SILInstruction::getNumDeletedInstructions();

  if (numAllocated != 0) {
    llvm::errs() << "Leaking " << numAllocated << " instructions!\n";
    llvm_unreachable("leaking instructions");
  }
#endif
}

std::unique_ptr<SILModule> SILModule::createEmptyModule(
    llvm::PointerUnion<FileUnit *, ModuleDecl *> context,
    Lowering::TypeConverter &TC, const SILOptions &Options) {
  return std::unique_ptr<SILModule>(new SILModule(context, TC, Options));
}

ASTContext &SILModule::getASTContext() const {
  return TheSwiftModule->getASTContext();
}

void *SILModule::allocate(unsigned Size, unsigned Align) const {
  if (getASTContext().LangOpts.UseMalloc)
    return AlignedAlloc(Size, Align);

  return BPA.Allocate(Size, Align);
}

FixedSizeSlab *SILModule::allocSlab() {
  if (freeSlabs.empty()) {
    numAllocatedSlabs++;
    return new (*this) FixedSizeSlab();
  }

  FixedSizeSlab *slab = &*freeSlabs.rbegin();
  freeSlabs.remove(*slab);
  return slab;
}

void SILModule::freeSlab(FixedSizeSlab *slab) {
  freeSlabs.push_back(*slab);
  assert(slab->overflowGuard == FixedSizeSlab::magicNumber);
}

void SILModule::freeAllSlabs(SlabList &slabs) {
  freeSlabs.splice(freeSlabs.end(), slabs);
}

void *SILModule::allocateInst(unsigned Size, unsigned Align) const {
  return AlignedAlloc(Size, Align);
}

void SILModule::willDeleteInstruction(SILInstruction *I) {
  // Update openedArchetypeDefs.
  if (auto *svi = dyn_cast<SingleValueInstruction>(I)) {
    if (CanArchetypeType archeTy = svi->getOpenedArchetype()) {
      OpenedArchetypeKey key = {archeTy, svi->getFunction()};
      assert(openedArchetypeDefs.lookup(key) == svi &&
             "archetype def was not registered");
      openedArchetypeDefs.erase(key);
    }
  }
}

void SILModule::scheduleForDeletion(SILInstruction *I) {
  I->dropAllReferences();
  scheduledForDeletion.push_back(I);
  I->ParentBB = nullptr;
}

void SILModule::flushDeletedInsts() {
  while (!scheduledForDeletion.empty()) {
    SILInstruction *inst = &*scheduledForDeletion.begin();
    scheduledForDeletion.erase(inst);
    AlignedFree(inst);
  }
}

SILWitnessTable *
SILModule::lookUpWitnessTable(ProtocolConformanceRef C,
                              bool deserializeLazily) {
  // If we have an abstract conformance passed in (a legal value), just return
  // nullptr.
  if (!C.isConcrete())
    return nullptr;

  return lookUpWitnessTable(C.getConcrete());
}

SILWitnessTable *
SILModule::lookUpWitnessTable(const ProtocolConformance *C,
                              bool deserializeLazily) {
  assert(C && "null conformance passed to lookUpWitnessTable");

  SILWitnessTable *wtable;

  auto rootC = C->getRootConformance();
  // Attempt to lookup the witness table from the table.
  auto found = WitnessTableMap.find(rootC);
  if (found == WitnessTableMap.end()) {
#ifndef NDEBUG
    // Make sure that all witness tables are in the witness table lookup
    // cache.
    //
    // This code should not be hit normally since we add witness tables to the
    // lookup cache when we create them. We don't just assert here since there
    // is the potential for a conformance without a witness table to be passed
    // to this function.
    for (SILWitnessTable &WT : witnessTables)
      assert(WT.getConformance() != rootC &&
             "Found witness table that is not"
             " in the witness table lookup cache.");
#endif

    // If we don't have a witness table and we're not going to try
    // deserializing it, do not create a declaration.
    if (!deserializeLazily)
      return nullptr;

    auto linkage = getLinkageForProtocolConformance(rootC, NotForDefinition);
    wtable = SILWitnessTable::create(*this, linkage,
                                 const_cast<RootProtocolConformance *>(rootC));
  } else {
    wtable = found->second;
    assert(wtable != nullptr && "Should never map a conformance to a null witness"
                            " table.");

    // If we have a definition, return it.
    if (wtable->isDefinition())
      return wtable;
  }

  // If the module is at or past the Lowered stage, then we can't do any
  // further deserialization, since pre-IRGen SIL lowering changes the types
  // of definitions to make them incompatible with canonical serialized SIL.
  switch (getStage()) {
  case SILStage::Canonical:
  case SILStage::Raw:
    break;

  case SILStage::Lowered:
    return wtable;
  }

  // Otherwise try to deserialize it. If we succeed return the deserialized
  // function.
  //
  // *NOTE* In practice, wtable will be deserializedTable, but I do not want to rely
  // on that behavior for now.
  if (deserializeLazily)
    if (auto deserialized = getSILLoader()->lookupWitnessTable(wtable))
      return deserialized;

  // If we fail, just return the declaration.
  return wtable;
}

SILDefaultWitnessTable *
SILModule::lookUpDefaultWitnessTable(const ProtocolDecl *Protocol,
                                     bool deserializeLazily) {
  // Note: we only ever look up default witness tables in the translation unit
  // that is currently being compiled, since they SILGen generates them when it
  // visits the protocol declaration, and IRGen emits them when emitting the
  // protocol descriptor metadata for the protocol.

  auto found = DefaultWitnessTableMap.find(Protocol);
  if (found == DefaultWitnessTableMap.end()) {
    if (deserializeLazily) {
      SILLinkage linkage =
        getSILLinkage(getDeclLinkage(Protocol), ForDefinition);
      SILDefaultWitnessTable *wtable =
        SILDefaultWitnessTable::create(*this, linkage, Protocol);
      wtable = getSILLoader()->lookupDefaultWitnessTable(wtable);
      if (wtable)
        DefaultWitnessTableMap[Protocol] = wtable;
      return wtable;
    }

    return nullptr;
  }

  return found->second;
}

SILDefaultWitnessTable *
SILModule::createDefaultWitnessTableDeclaration(const ProtocolDecl *Protocol,
                                                SILLinkage Linkage) {
  return SILDefaultWitnessTable::create(*this, Linkage, Protocol);
}

void SILModule::deleteWitnessTable(SILWitnessTable *Wt) {
  auto Conf = Wt->getConformance();
  assert(lookUpWitnessTable(Conf, false) == Wt);
  getSILLoader()->invalidateWitnessTable(Wt);
  WitnessTableMap.erase(Conf);
  witnessTables.erase(Wt);
}

const IntrinsicInfo &SILModule::getIntrinsicInfo(Identifier ID) {
  unsigned OldSize = IntrinsicIDCache.size();
  IntrinsicInfo &Info = IntrinsicIDCache[ID];

  // If the element was is in the cache, return it.
  if (OldSize == IntrinsicIDCache.size())
    return Info;

  // Otherwise, lookup the ID and Type and store them in the map.
  StringRef NameRef = getBuiltinBaseName(getASTContext(), ID.str(), Info.Types);
  Info.ID = getLLVMIntrinsicID(NameRef);

  return Info;
}

const BuiltinInfo &SILModule::getBuiltinInfo(Identifier ID) {
  unsigned OldSize = BuiltinIDCache.size();
  BuiltinInfo &Info = BuiltinIDCache[ID];

  // If the element was is in the cache, return it.
  if (OldSize == BuiltinIDCache.size())
    return Info;

  // Otherwise, lookup the ID and Type and store them in the map.
  // Find the matching ID.
  StringRef OperationName =
    getBuiltinBaseName(getASTContext(), ID.str(), Info.Types);

  // Several operation names have suffixes and don't match the name from
  // Builtins.def, so handle those first.
  if (OperationName.startswith("fence_"))
    Info.ID = BuiltinValueKind::Fence;
  else if (OperationName.startswith("ifdef_"))
    Info.ID = BuiltinValueKind::Ifdef;
  else if (OperationName.startswith("cmpxchg_"))
    Info.ID = BuiltinValueKind::CmpXChg;
  else if (OperationName.startswith("atomicrmw_"))
    Info.ID = BuiltinValueKind::AtomicRMW;
  else if (OperationName.startswith("atomicload_"))
    Info.ID = BuiltinValueKind::AtomicLoad;
  else if (OperationName.startswith("atomicstore_"))
    Info.ID = BuiltinValueKind::AtomicStore;
  else if (OperationName.startswith("allocWithTailElems_"))
    Info.ID = BuiltinValueKind::AllocWithTailElems;
  else if (OperationName.startswith("applyDerivative_"))
    Info.ID = BuiltinValueKind::ApplyDerivative;
  else if (OperationName.startswith("applyTranspose_"))
    Info.ID = BuiltinValueKind::ApplyTranspose;
  else
    Info.ID = llvm::StringSwitch<BuiltinValueKind>(OperationName)
#define BUILTIN(id, name, attrs) .Case(name, BuiltinValueKind::id)
#include "swift/AST/Builtins.def"
      .Default(BuiltinValueKind::None);

  return Info;
}

SILFunction *SILModule::lookUpFunction(SILDeclRef fnRef) {
  auto name = fnRef.mangle();
  return lookUpFunction(name);
}

bool SILModule::loadFunction(SILFunction *F) {
  SILFunction *NewF =
    getSILLoader()->lookupSILFunction(F, /*onlyUpdateLinkage*/ false);
  if (!NewF)
    return false;

  assert(F == NewF);
  return true;
}

void SILModule::updateFunctionLinkage(SILFunction *F) {
  getSILLoader()->lookupSILFunction(F, /*onlyUpdateLinkage*/ true);
}

bool SILModule::linkFunction(SILFunction *F, SILModule::LinkingMode Mode) {
  return SILLinkerVisitor(*this, Mode).processFunction(F);
}

SILFunction *SILModule::findFunction(StringRef Name, SILLinkage Linkage) {
  assert((Linkage == SILLinkage::Public ||
          Linkage == SILLinkage::SharedExternal ||
          Linkage == SILLinkage::PublicExternal) &&
         "Only a lookup of public functions is supported currently");

  SILFunction *F = nullptr;

  // First, check if there is a function with a required name in the
  // current module.
  SILFunction *CurF = lookUpFunction(Name);

  // Nothing to do if the current module has a required function
  // with a proper linkage already.
  if (CurF && CurF->getLinkage() == Linkage) {
    F = CurF;
  } else {
    assert((!CurF || CurF->getLinkage() != Linkage) &&
           "hasFunction should be only called for functions that are not "
           "contained in the SILModule yet or do not have a required linkage");
  }

  if (!F) {
    if (CurF) {
      // Perform this lookup only if a function with a given
      // name is present in the current module.
      // This is done to reduce the amount of IO from the
      // swift module file.
      if (!getSILLoader()->hasSILFunction(Name, Linkage))
        return nullptr;
      // The function in the current module will be changed.
      F = CurF;
    }

    // If function with a given name wasn't seen anywhere yet
    // or if it is known to exist, perform a lookup.
    if (!F) {
      // Try to load the function from other modules.
      F = getSILLoader()->lookupSILFunction(Name, /*declarationOnly*/ true,
                                            Linkage);
      // Bail if nothing was found and we are not sure if
      // this function exists elsewhere.
      if (!F)
        return nullptr;
      assert(F && "SILFunction should be present in one of the modules");
      assert(F->getLinkage() == Linkage && "SILFunction has a wrong linkage");
    }
  }

  // If a function exists already and it is a non-optimizing
  // compilation, simply convert it into an external declaration,
  // so that a compiled version from the shared library is used.
  if (F->isDefinition() &&
      // Don't eliminate bodies of _alwaysEmitIntoClient functions
      // (PublicNonABI linkage is de-serialized as SharedExternal)
      F->getLinkage() != SILLinkage::SharedExternal &&
      !F->getModule().getOptions().shouldOptimize()) {
    F->convertToDeclaration();
  }
  if (F->isExternalDeclaration())
    F->setSerialized(IsSerialized_t::IsNotSerialized);
  F->setLinkage(Linkage);
  return F;
}

bool SILModule::hasFunction(StringRef Name) {
  if (lookUpFunction(Name))
    return true;
  return getSILLoader()->hasSILFunction(Name);
}

void SILModule::invalidateSILLoaderCaches() {
  getSILLoader()->invalidateAllCaches();
}

SILFunction *SILModule::removeFromZombieList(StringRef Name) {
  if (auto *Zombie = ZombieFunctionTable.lookup(Name)) {
    ZombieFunctionTable.erase(Name);
    zombieFunctions.remove(Zombie);

    // The owner of the function's Name is the ZombieFunctionTable key, which is
    // freed by erase().
    // Make sure nobody accesses the name string after it is freed.
    Zombie->Name = StringRef();
    return Zombie;
  }
  return nullptr;
}

/// Erase a function from the module.
void SILModule::eraseFunction(SILFunction *F) {
  assert(!F->isZombie() && "zombie function is in list of alive functions");

  llvm::StringMapEntry<SILFunction*> *entry =
      &*ZombieFunctionTable.insert(std::make_pair(F->getName(), nullptr)).first;
  assert(!entry->getValue() && "Zombie function already exists");
  StringRef zombieName = entry->getKey();

  // The owner of the function's Name is the FunctionTable key. As we remove
  // the function from the table we need to use the allocated name string from
  // the ZombieFunctionTable.
  FunctionTable.erase(F->getName());
  F->Name = zombieName;

  // The function is dead, but we need it later (at IRGen) for debug info
  // or vtable stub generation. So we move it into the zombie list.
  getFunctionList().remove(F);
  zombieFunctions.push_back(F);
  entry->setValue(F);
  F->setZombie();

  // This opens dead-function-removal opportunities for called functions.
  // (References are not needed anymore.)
  F->clear();
  F->dropDynamicallyReplacedFunction();
  // Drop references for any _specialize(target:) functions.
  F->clearSpecializeAttrs();
}

void SILModule::invalidateFunctionInSILCache(SILFunction *F) {
  getSILLoader()->invalidateFunction(F);
}

/// Erase a global SIL variable from the module.
void SILModule::eraseGlobalVariable(SILGlobalVariable *gv) {
  getSILLoader()->invalidateGlobalVariable(gv);
  GlobalVariableMap.erase(gv->getName());
  getSILGlobalList().erase(gv);
}

SILVTable *SILModule::lookUpVTable(const ClassDecl *C,
                                   bool deserializeLazily) {
  if (!C)
    return nullptr;

  // First try to look up R from the lookup table.
  auto R = VTableMap.find(C);
  if (R != VTableMap.end())
    return R->second;

  if (!deserializeLazily)
    return nullptr;

  // If that fails, try to deserialize it. If that fails, return nullptr.
  SILVTable *Vtbl = getSILLoader()->lookupVTable(C);
  if (!Vtbl)
    return nullptr;

  // If we succeeded, map C -> VTbl in the table and return VTbl.
  VTableMap[C] = Vtbl;
  return Vtbl;
}

SerializedSILLoader *SILModule::getSILLoader() {
  // If the SILLoader is null, create it.
  if (!SILLoader)
    SILLoader = SerializedSILLoader::create(
        getASTContext(), this, &deserializationNotificationHandlers);
  // Return the SerializedSILLoader.
  return SILLoader.get();
}

/// Given a conformance \p C and a protocol requirement \p Requirement,
/// search the witness table for the conformance and return the witness thunk
/// for the requirement.
std::pair<SILFunction *, SILWitnessTable *>
SILModule::lookUpFunctionInWitnessTable(ProtocolConformanceRef C,
                                        SILDeclRef Requirement) {
  // Look up the witness table associated with our protocol conformance from the
  // SILModule.
  auto Ret = lookUpWitnessTable(C);

  // If no witness table was found, bail.
  if (!Ret) {
    LLVM_DEBUG(llvm::dbgs() << "        Failed speculative lookup of "
               "witness for: ";
               C.dump(llvm::dbgs()); Requirement.dump());
    return std::make_pair(nullptr, nullptr);
  }

  // Okay, we found the correct witness table. Now look for the method.
  for (auto &Entry : Ret->getEntries()) {
    // Look at method entries only.
    if (Entry.getKind() != SILWitnessTable::WitnessKind::Method)
      continue;

    SILWitnessTable::MethodWitness MethodEntry = Entry.getMethodWitness();
    // Check if this is the member we were looking for.
    if (MethodEntry.Requirement != Requirement)
      continue;

    return std::make_pair(MethodEntry.Witness, Ret);
  }

  return std::make_pair(nullptr, nullptr);
}

/// Given a protocol \p Protocol and a requirement \p Requirement,
/// search the protocol's default witness table and return the default
/// witness thunk for the requirement.
std::pair<SILFunction *, SILDefaultWitnessTable *>
SILModule::lookUpFunctionInDefaultWitnessTable(const ProtocolDecl *Protocol,
                                               SILDeclRef Requirement,
                                               bool deserializeLazily) {
  // Look up the default witness table associated with our protocol from the
  // SILModule.
  auto Ret = lookUpDefaultWitnessTable(Protocol, deserializeLazily);

  // If no default witness table was found, bail.
  //
  // FIXME: Could be an assert if we fix non-single-frontend mode to link
  // together serialized SIL emitted by each translation unit.
  if (!Ret) {
    LLVM_DEBUG(llvm::dbgs() << "        Failed speculative lookup of default "
               "witness for " << Protocol->getName() << " ";
               Requirement.dump());
    return std::make_pair(nullptr, nullptr);
  }

  // Okay, we found the correct default witness table. Now look for the method.
  for (auto &Entry : Ret->getEntries()) {
    // Ignore dummy entries emitted for non-method requirements, as well as
    // requirements without default implementations.
    if (!Entry.isValid() || Entry.getKind() != SILWitnessTable::Method)
      continue;

    // Check if this is the member we were looking for.
    if (Entry.getMethodWitness().Requirement != Requirement)
      continue;

    return std::make_pair(Entry.getMethodWitness().Witness, Ret);
  }

  // This requirement doesn't have a default implementation.
  return std::make_pair(nullptr, nullptr);
}

SILFunction *
SILModule::
lookUpFunctionInVTable(ClassDecl *Class, SILDeclRef Member) {
  // Try to lookup a VTable for Class from the module...
  auto *Vtbl = lookUpVTable(Class);

  // Bail, if the lookup of VTable fails.
  if (!Vtbl) {
    return nullptr;
  }

  // Ok, we have a VTable. Try to lookup the SILFunction implementation from
  // the VTable.
  if (auto E = Vtbl->getEntry(*this, Member))
    return E->getImplementation();

  return nullptr;
}

SILDifferentiabilityWitness *
SILModule::lookUpDifferentiabilityWitness(StringRef name) {
  auto it = DifferentiabilityWitnessMap.find(name);
  if (it != DifferentiabilityWitnessMap.end())
    return it->second;
  return nullptr;
}

SILDifferentiabilityWitness *
SILModule::lookUpDifferentiabilityWitness(SILDifferentiabilityWitnessKey key) {
  Mangle::ASTMangler mangler;
  return lookUpDifferentiabilityWitness(
      mangler.mangleSILDifferentiabilityWitness(
          key.originalFunctionName, key.kind, key.config));
}

/// Look up the differentiability witness corresponding to the given indices.
llvm::ArrayRef<SILDifferentiabilityWitness *>
SILModule::lookUpDifferentiabilityWitnessesForFunction(StringRef name) {
  return DifferentiabilityWitnessesByFunction[name];
}

bool SILModule::loadDifferentiabilityWitness(SILDifferentiabilityWitness *dw) {
  auto *newDW = getSILLoader()->lookupDifferentiabilityWitness(dw->getKey());
  if (!newDW)
    return false;
  assert(dw == newDW);
  return true;
}

void SILModule::registerDeserializationNotificationHandler(
    std::unique_ptr<DeserializationNotificationHandler> &&handler) {
  deserializationNotificationHandlers.add(std::move(handler));
}

SILValue SILModule::getOpenedArchetypeDef(CanArchetypeType archetype,
                                          SILFunction *inFunction) {
  SILValue &def = openedArchetypeDefs[{archetype, inFunction}];
  if (!def) {
    numUnresolvedOpenedArchetypes++;
    def = ::new PlaceholderValue(SILType::getPrimitiveAddressType(archetype));
  }

  return def;
}

bool SILModule::hasUnresolvedOpenedArchetypeDefinitions() {
  return numUnresolvedOpenedArchetypes != 0;
}

void SILModule::notifyAddedInstruction(SILInstruction *inst) {
  if (auto *svi = dyn_cast<SingleValueInstruction>(inst)) {
    if (CanArchetypeType archeTy = svi->getOpenedArchetype()) {
      SILValue &val = openedArchetypeDefs[{archeTy, inst->getFunction()}];
      if (val) {
        if (!isa<PlaceholderValue>(val)) {
          // Print a useful error message (and not just abort with an assert).
          llvm::errs() << "re-definition of opened archetype in function "
                       << svi->getFunction()->getName() << ":\n";
          svi->print(llvm::errs());
          llvm::errs() << "previously defined in function "
                       << val->getFunction()->getName() << ":\n";
          val->print(llvm::errs());
          abort();
        }
        // The opened archetype was unresolved so far. Replace the placeholder
        // by inst.
        auto *placeholder = cast<PlaceholderValue>(val);
        placeholder->replaceAllUsesWith(svi);
        ::delete placeholder;
        numUnresolvedOpenedArchetypes--;
      }
      val = svi;
    }
  }
}

void SILModule::notifyMovedInstruction(SILInstruction *inst,
                                       SILFunction *fromFunction) {
  if (auto *svi = dyn_cast<SingleValueInstruction>(inst)) {
    if (CanArchetypeType archeTy = svi->getOpenedArchetype()) {
      OpenedArchetypeKey key = {archeTy, fromFunction};
      assert(openedArchetypeDefs.lookup(key) == svi &&
             "archetype def was not registered");
      openedArchetypeDefs.erase(key);
      openedArchetypeDefs[{archeTy, svi->getFunction()}] = svi;
    }
  }
}

// TODO: We should have an "isNoReturn" bit on Swift's BuiltinInfo, but for
// now, let's recognize noreturn intrinsics and builtins specially here.
bool SILModule::isNoReturnBuiltinOrIntrinsic(Identifier Name) {
  const auto &IntrinsicInfo = getIntrinsicInfo(Name);
  if (IntrinsicInfo.ID != llvm::Intrinsic::not_intrinsic) {
    return IntrinsicInfo
              .getOrCreateAttributes(getASTContext())
              .hasFnAttribute(llvm::Attribute::NoReturn);
  }
  const auto &BuiltinInfo = getBuiltinInfo(Name);
  switch (BuiltinInfo.ID) {
  default:
    return false;
  case BuiltinValueKind::Unreachable:
  case BuiltinValueKind::CondUnreachable:
  case BuiltinValueKind::UnexpectedError:
  case BuiltinValueKind::ErrorInMain:
    return true;
  }
}

bool SILModule::
shouldSerializeEntitiesAssociatedWithDeclContext(const DeclContext *DC) const {
  // Serialize entities associated with this module's associated context.
  if (DC->isChildContextOf(getAssociatedContext())) {
    return true;
  }
  
  // Serialize entities associated with clang modules, since other entities
  // may depend on them, and someone who deserializes those entities may not
  // have their own copy.
  if (isa<ClangModuleUnit>(DC->getModuleScopeContext())) {
    return true;
  }
  
  return false;
}

/// Returns true if it is the optimized OnoneSupport module.
bool SILModule::isOptimizedOnoneSupportModule() const {
  return getOptions().shouldOptimize() &&
         getSwiftModule()->isOnoneSupportModule();
}

void SILModule::setSerializeSILAction(SILModule::ActionCallback Action) {
  assert(!SerializeSILAction && "Serialization action can be set only once");
  SerializeSILAction = Action;
}

SILModule::ActionCallback SILModule::getSerializeSILAction() const {
  return SerializeSILAction;
}

void SILModule::serialize() {
  assert(SerializeSILAction && "Serialization action should be set");
  assert(!isSerialized() && "The module was serialized already");
  SerializeSILAction();
  setSerialized();
}

void SILModule::installSILRemarkStreamer() {
  assert(!silRemarkStreamer && "SIL Remark Streamer is already installed!");
  silRemarkStreamer = SILRemarkStreamer::create(*this);
}

bool SILModule::isStdlibModule() const {
  return TheSwiftModule->isStdlibModule();
}
void SILModule::performOnceForPrespecializedImportedExtensions(
    llvm::function_ref<void(AbstractFunctionDecl *)> action) {
  if (prespecializedFunctionDeclsImported)
    return;

  SmallVector<ModuleDecl *, 8> importedModules;
  // Add the Swift module.
  if (!isStdlibModule()) {
    auto *SwiftStdlib = getASTContext().getStdlibModule(true);
    if (SwiftStdlib)
      importedModules.push_back(SwiftStdlib);
  }

  // Add explicitly imported modules.
  SmallVector<Decl *, 32> topLevelDecls;
  getSwiftModule()->getTopLevelDecls(topLevelDecls);
  for (const Decl *D : topLevelDecls) {
    if (auto importDecl = dyn_cast<ImportDecl>(D)) {
      if (!importDecl->getModule() ||
          importDecl->getModule()->isNonSwiftModule())
        continue;
      importedModules.push_back(importDecl->getModule());
    }
  }

  for (auto *module : importedModules) {
    SmallVector<Decl *, 16> prespecializations;
    module->getExportedPrespecializations(prespecializations);
    for (auto *p : prespecializations) {
      if (auto *vd = dyn_cast<AbstractFunctionDecl>(p)) {
        action(vd);
      }
    }
  }
  prespecializedFunctionDeclsImported = true;
}

SILProperty *SILProperty::create(SILModule &M,
                                 bool Serialized,
                                 AbstractStorageDecl *Decl,
                                 Optional<KeyPathPatternComponent> Component) {
  auto prop = new (M) SILProperty(Serialized, Decl, Component);
  M.properties.push_back(prop);
  return prop;
}

// Definition from SILLinkage.h.
SILLinkage swift::getDeclSILLinkage(const ValueDecl *decl) {
  AccessLevel access = decl->getEffectiveAccess();
  SILLinkage linkage;
  switch (access) {
  case AccessLevel::Private:
  case AccessLevel::FilePrivate:
    linkage = SILLinkage::Private;
    break;
  case AccessLevel::Internal:
    linkage = SILLinkage::Hidden;
    break;
  case AccessLevel::Public:
  case AccessLevel::Open:
    linkage = SILLinkage::Public;
    break;
  }
  return linkage;
}

void swift::simple_display(llvm::raw_ostream &out, const SILModule *M) {
  if (!M) {
    out << "(null)";
    return;
  }
  out << "SIL for ";
  simple_display(out, M->getSwiftModule());
}

SourceLoc swift::extractNearestSourceLoc(const SILModule *M) {
  if (!M)
    return SourceLoc();
  return extractNearestSourceLoc(M->getSwiftModule());
}
