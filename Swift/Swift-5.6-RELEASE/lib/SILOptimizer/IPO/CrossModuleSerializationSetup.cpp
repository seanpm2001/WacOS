//===--- UsePrespecialized.cpp - use pre-specialized functions ------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
/// An optimization which marks functions and types as inlinable or usable
/// from inline. This lets such functions be serialized (later in the pipeline),
/// which makes them available for other modules.
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "cross-module-serialization-setup"
#include "swift/AST/Module.h"
#include "swift/SIL/ApplySite.h"
#include "swift/SIL/SILCloner.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILModule.h"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/SILOptimizer/Utils/InstOptUtils.h"
#include "swift/SILOptimizer/Utils/SILInliner.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

using namespace swift;

/// Functions up to this (abstract) size are serialized, even if they are not
/// generic.
static llvm::cl::opt<int> CMOFunctionSizeLimit("cmo-function-size-limit",
                                               llvm::cl::init(20));

namespace {

/// Scans a whole module and marks functions and types as inlinable or usable
/// from inline.
class CrossModuleSerializationSetup {
  friend class InstructionVisitor;

  // The worklist of function which should be serialized.
  llvm::SmallVector<SILFunction *, 16> workList;
  llvm::SmallPtrSet<SILFunction *, 16> functionsHandled;

  llvm::DenseMap<SILType, bool> typesChecked;
  llvm::SmallPtrSet<TypeBase *, 16> typesHandled;

  SILModule &M;
  
  void addToWorklistIfNotHandled(SILFunction *F) {
    if (functionsHandled.count(F) == 0) {
      workList.push_back(F);
      functionsHandled.insert(F);
    }
  }

  bool canUseFromInline(SILFunction *F, bool lookIntoThunks);

  bool canSerialize(SILFunction *F, bool lookIntoThunks);

  bool canSerialize(SILInstruction *inst, bool lookIntoThunks);

  bool canSerialize(SILGlobalVariable *global);

  bool canSerialize(SILType type);

  void setUpForSerialization(SILFunction *F);

  void setUpForSerialization(SILGlobalVariable *global);

  void prepareInstructionForSerialization(SILInstruction *inst);

  void handleReferencedFunction(SILFunction *F);

  void handleReferencedMethod(SILDeclRef method);

  void makeTypeUsableFromInline(CanType type);

  void makeSubstUsableFromInline(const SubstitutionMap &substs);

public:
  CrossModuleSerializationSetup(SILModule &M) : M(M) { }

  void scanModule();
};

/// Visitor for making used types of an intruction inlinable.
///
/// We use the SILCloner for visiting types, though it sucks that we allocate
/// instructions just to delete them immediately. But it's better than to
/// reimplement the logic.
/// TODO: separate the type visiting logic in SILCloner from the instruction
/// creation.
class InstructionVisitor : public SILCloner<InstructionVisitor> {
  friend class SILCloner<InstructionVisitor>;
  friend class SILInstructionVisitor<InstructionVisitor>;

private:
  CrossModuleSerializationSetup &CMS;
  SILInstruction *result = nullptr;

public:
  InstructionVisitor(SILInstruction *I, CrossModuleSerializationSetup &CMS) :
    SILCloner(*I->getFunction()), CMS(CMS) {
    Builder.setInsertionPoint(I);
  }

  SILType remapType(SILType Ty) {
    CMS.makeTypeUsableFromInline(Ty.getASTType());
    return Ty;
  }

  CanType remapASTType(CanType Ty) {
    CMS.makeTypeUsableFromInline(Ty);
    return Ty;
  }

  SubstitutionMap remapSubstitutionMap(SubstitutionMap Subs) {
    CMS.makeSubstUsableFromInline(Subs);
    return Subs;
  }

  void postProcess(SILInstruction *Orig, SILInstruction *Cloned) {
    result = Cloned;
    SILCloner<InstructionVisitor>::postProcess(Orig, Cloned);
  }

  SILValue getMappedValue(SILValue Value) { return Value; }

  SILBasicBlock *remapBasicBlock(SILBasicBlock *BB) { return BB; }

  static void visitInst(SILInstruction *I, CrossModuleSerializationSetup &CMS) {
    InstructionVisitor visitor(I, CMS);
    visitor.visit(I);
    visitor.result->eraseFromParent();
  }
};

/// Make a nominal type, including it's context, usable from inline.
static void makeDeclUsableFromInline(ValueDecl *decl, SILModule &M) {
  if (decl->getEffectiveAccess() >= AccessLevel::Public)
    return;

  if (decl->getFormalAccess() < AccessLevel::Public &&
      !decl->isUsableFromInline()) {
    // Mark the nominal type as "usableFromInline".
    // TODO: find a way to do this without modifying the AST. The AST should be
    // immutable at this point.
    auto &ctx = decl->getASTContext();
    auto *attr = new (ctx) UsableFromInlineAttr(/*implicit=*/true);
    decl->getAttrs().add(attr);
  }
  if (auto *nominalCtx = dyn_cast<NominalTypeDecl>(decl->getDeclContext())) {
    makeDeclUsableFromInline(nominalCtx, M);
  } else if (auto *extCtx = dyn_cast<ExtensionDecl>(decl->getDeclContext())) {
    if (auto *extendedNominal = extCtx->getExtendedNominal()) {
      makeDeclUsableFromInline(extendedNominal, M);
    }
  } else if (decl->getDeclContext()->isLocalContext()) {
    // TODO
  }
}

/// Ensure that the \p type is usable from serialized functions.
void CrossModuleSerializationSetup::makeTypeUsableFromInline(CanType type) {
  if (!typesHandled.insert(type.getPointer()).second)
    return;

  if (NominalTypeDecl *NT = type->getNominalOrBoundGenericNominal()) {
    makeDeclUsableFromInline(NT, M);
  }

  // Also make all sub-types usable from inline.
  type.visit([this](Type rawSubType) {
    CanType subType = rawSubType->getCanonicalType();
    if (typesHandled.insert(subType.getPointer()).second) {
      if (NominalTypeDecl *subNT = subType->getNominalOrBoundGenericNominal()) {
        makeDeclUsableFromInline(subNT, M);
      }
    }
  });
}

/// Ensure that all replacement types of \p substs are usable from serialized
/// functions.
void CrossModuleSerializationSetup::
makeSubstUsableFromInline(const SubstitutionMap &substs) {
  for (Type replType : substs.getReplacementTypes()) {
    makeTypeUsableFromInline(replType->getCanonicalType());
  }
  for (ProtocolConformanceRef pref : substs.getConformances()) {
    if (pref.isConcrete()) {
      ProtocolConformance *concrete = pref.getConcrete();
      makeDeclUsableFromInline(concrete->getProtocol(), M);
    }
  }
}
static llvm::cl::opt<bool> SerializeEverything(
    "sil-cross-module-serialize-all", llvm::cl::init(false),
    llvm::cl::desc(
        "Serialize everything when performing cross module optimization in "
        "order to investigate performance differences caused by different "
        "@inlinable, @usableFromInline choices."),
    llvm::cl::Hidden);

/// Decide whether to serialize a function.
static bool shouldSerialize(SILFunction *F) {
  // Check if we already handled this function before.
  if (F->isSerialized() == IsSerialized)
    return false;

  if (F->hasSemanticsAttr("optimize.no.crossmodule"))
    return false;

  if (SerializeEverything)
    return true;

  // The basic heursitic: serialize all generic functions, because it makes a
  // huge difference if generic functions can be specialized or not.
  if (F->getLoweredFunctionType()->isPolymorphic())
    return true;

  // Also serialize "small" non-generic functions.
  int size = 0;
  for (SILBasicBlock &block : *F) {
    for (SILInstruction &inst : block) {
      size += (int)instructionInlineCost(inst);
      if (size >= CMOFunctionSizeLimit)
        return false;
    }
  }

  return true;
}

static void makeFunctionUsableFromInline(SILFunction *F) {
  if (!isAvailableExternally(F->getLinkage()))
    F->setLinkage(SILLinkage::Public);
}

/// Prepare \p inst for serialization and in case it's a function_ref, put the
/// referenced function onto the worklist.
void CrossModuleSerializationSetup::
prepareInstructionForSerialization(SILInstruction *inst) {
  // Put callees onto the worklist if they should be serialized as well.
  if (auto *FRI = dyn_cast<FunctionRefBaseInst>(inst)) {
    SILFunction *callee = FRI->getReferencedFunctionOrNull();
    assert(callee);
    handleReferencedFunction(callee);
    return;
  }
  if (auto *GAI = dyn_cast<GlobalAddrInst>(inst)) {
    SILGlobalVariable *gl = GAI->getReferencedGlobal();
    if (canSerialize(gl)) {
      setUpForSerialization(gl);
    }
    gl->setLinkage(SILLinkage::Public);
    return;
  }
  if (auto *MI = dyn_cast<MethodInst>(inst)) {
    handleReferencedMethod(MI->getMember());
    return;
  }
  if (auto *KPI = dyn_cast<KeyPathInst>(inst)) {
    KPI->getPattern()->visitReferencedFunctionsAndMethods(
        [this](SILFunction *func) { handleReferencedFunction(func); },
        [this](SILDeclRef method) { handleReferencedMethod(method); });
    return;
  }
  if (auto *REAI = dyn_cast<RefElementAddrInst>(inst)) {
    makeDeclUsableFromInline(REAI->getField(), M);
  }
}

void CrossModuleSerializationSetup::handleReferencedFunction(SILFunction *func) {
  if (!func->isDefinition() || func->isAvailableExternally())
    return;
  if (func->isSerialized() == IsSerialized)
    return;

  if (func->getLinkage() == SILLinkage::Shared) {
    assert(func->isThunk() != IsNotThunk &&
      "only thunks are accepted to have shared linkage");
    assert(canSerialize(func, /*lookIntoThunks*/ false) &&
      "we should already have checked that the thunk is serializable");
    
    // We cannot make shared functions "usableFromInline", i.e. make them Public
    // because this could result in duplicate-symbol errors. Instead we make
    // them "@alwaysEmitIntoClient"
    setUpForSerialization(func);
    return;
  }
  if (shouldSerialize(func)) {
    addToWorklistIfNotHandled(func);
    return;
  }
  makeFunctionUsableFromInline(func);
  return;
}

void CrossModuleSerializationSetup::handleReferencedMethod(SILDeclRef method) {
  if (method.isForeign)
    return;
  // Prevent the method from dead-method elimination.
  auto *methodDecl = cast<AbstractFunctionDecl>(method.getDecl());
  M.addExternallyVisibleDecl(getBaseMethod(methodDecl));
}

/// Check if the function \p F can be serialized.
///
/// If \p lookIntoThunks is true, function_ref instructions of shared
/// thunks are also accepted.
bool CrossModuleSerializationSetup::canSerialize(SILFunction *F,
                                                 bool lookIntoThunks) {
  // First step: check if serializing F is even possible.
  for (SILBasicBlock &block : *F) {
    for (SILInstruction &inst : block) {
      if (!canSerialize(&inst, lookIntoThunks))
        return false;
    }
  }
  return true;
}

bool CrossModuleSerializationSetup::canSerialize(SILInstruction *inst,
                                                 bool lookIntoThunks) {

  for (SILValue result : inst->getResults()) {
    if (!canSerialize(result->getType()))
      return false;
  }

  if (auto *FRI = dyn_cast<FunctionRefBaseInst>(inst)) {
    SILFunction *callee = FRI->getReferencedFunctionOrNull();
    return canUseFromInline(callee, lookIntoThunks);
  }
  if (auto *KPI = dyn_cast<KeyPathInst>(inst)) {
    bool canUse = true;
    KPI->getPattern()->visitReferencedFunctionsAndMethods(
        [&](SILFunction *func) {
          if (!canUseFromInline(func, lookIntoThunks))
            canUse = false;
        },
        [&](SILDeclRef method) {
          if (method.isForeign)
            canUse = false;
        });
    return canUse;
  }
  if (auto *MI = dyn_cast<MethodInst>(inst)) {
    return !MI->getMember().isForeign;
  }
  
  return true;
}

bool CrossModuleSerializationSetup::canSerialize(SILGlobalVariable *global) {
  for (const SILInstruction &initInst : *global) {
    if (!canSerialize(const_cast<SILInstruction *>(&initInst),
                      /*lookIntoThunks*/ true))
      return false;
  }
  return true;
}

bool CrossModuleSerializationSetup::canSerialize(SILType type) {
  auto iter = typesChecked.find(type);
  if (iter != typesChecked.end())
    return iter->getSecond();

  ModuleDecl *mod = M.getSwiftModule();
  bool success = !type.getASTType().findIf(
    [mod](Type rawSubType) {
      CanType subType = rawSubType->getCanonicalType();
      if (NominalTypeDecl *subNT = subType->getNominalOrBoundGenericNominal()) {
      
        // Exclude types which are defined in an @_implementationOnly imported
        // module. Such modules are not transitively available.
        if (!mod->canBeUsedForCrossModuleOptimization(subNT)) {
          return true;
        }
      }
      return false;
    });
  typesChecked[type] = success;
  return success;
}

/// Returns true if the function \p func can be used from a serialized function.
///
/// If \p lookIntoThunks is true, serializable shared thunks are also accepted.
bool CrossModuleSerializationSetup::canUseFromInline(SILFunction *func,
                                                     bool lookIntoThunks) {
  if (!func)
    return false;

  if (DeclContext *funcCtxt = func->getDeclContext()) {
    if (!M.getSwiftModule()->canBeUsedForCrossModuleOptimization(funcCtxt))
      return false;
  }

  switch (func->getLinkage()) {
  case SILLinkage::PublicNonABI:
    return func->isSerialized() != IsNotSerialized;
  case SILLinkage::Shared:
    if (func->isThunk() != IsNotThunk && lookIntoThunks &&
        // Don't recursively lookIntoThunks to avoid infinite loops.
        canSerialize(func, /*lookIntoThunks*/ false)) {
      return true;
    }
    return false;
  case SILLinkage::Public:
  case SILLinkage::Hidden:
  case SILLinkage::Private:
  case SILLinkage::PublicExternal:
  case SILLinkage::SharedExternal:
  case SILLinkage::HiddenExternal:
    break;
  }
  return true;
}

/// Setup the function \p param F for serialization and put callees onto the
/// worklist for further processing.
///
/// Returns false in case this is not possible for some reason.
void CrossModuleSerializationSetup::setUpForSerialization(SILFunction *F) {
  assert(F->isSerialized() != IsSerialized);

  // Second step: go through all instructions and prepare them for
  // for serialization.
  for (SILBasicBlock &block : *F) {
    for (SILInstruction &inst : block) {
      // Make all types of the instruction usable from inline.
      InstructionVisitor::visitInst(&inst, *this);

      prepareInstructionForSerialization(&inst);
    }
  }
  F->setSerialized(IsSerialized);

  if (F->getLoweredFunctionType()->isPolymorphic() ||
      F->getLinkage() != SILLinkage::Public) {
    // As a code size optimization, make serialized functions
    // @alwaysEmitIntoClient.
    // Also, for shared thunks it's required to make them @alwaysEmitIntoClient.
    // SILLinkage::Public would not work for shared functions, because it could
    // result in duplicate-symbol linker errors.
    F->setLinkage(SILLinkage::PublicNonABI);
  } else {
    F->setLinkage(SILLinkage::Public);
  }
}

void CrossModuleSerializationSetup::
setUpForSerialization(SILGlobalVariable *global) {
  for (const SILInstruction &initInst : *global) {
    prepareInstructionForSerialization(const_cast<SILInstruction *>(&initInst));
  }
  global->setSerialized(IsSerialized);
}

/// Select functions in the module which should be serialized.
void CrossModuleSerializationSetup::scanModule() {

  // Start with public functions.
  for (SILFunction &F : M) {
    if (F.getLinkage() == SILLinkage::Public)
      addToWorklistIfNotHandled(&F);
  }

  // Continue with called functions.
  while (!workList.empty()) {
    SILFunction *F = workList.pop_back_val();
    // Decide whether we want to serialize the function.
    if (shouldSerialize(F)) {
      // Try to serialize.
      if (canSerialize(F, /*lookIntoThunks*/ true)) {
        setUpForSerialization(F);
      } else {
        // If for some reason the function cannot be serialized, we mark it as
        // usable-from-inline.
        makeFunctionUsableFromInline(F);
      }
    }
  }
}

class CrossModuleSerializationSetupPass: public SILModuleTransform {
  void run() override {

    auto &M = *getModule();
    if (M.getSwiftModule()->isResilient())
      return;
    if (!M.isWholeModule())
      return;
    if (!M.getOptions().CrossModuleOptimization)
      return;

    CrossModuleSerializationSetup CMSS(M);
    CMSS.scanModule();
  }
};

} // end anonymous namespace

SILTransform *swift::createCrossModuleSerializationSetup() {
  return new CrossModuleSerializationSetupPass();
}
