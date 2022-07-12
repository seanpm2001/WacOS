//===--- Verifier.cpp - Verification of Swift SIL Code --------------------===//
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

#define DEBUG_TYPE "sil-verifier"
#include "swift/AST/ASTContext.h"
#include "swift/AST/AnyFunctionRef.h"
#include "swift/AST/Decl.h"
#include "swift/AST/ExistentialLayout.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Module.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/Types.h"
#include "swift/Basic/Range.h"
#include "swift/ClangImporter/ClangModule.h"
#include "swift/SIL/Dominance.h"
#include "swift/SIL/DynamicCasts.h"
#include "swift/SIL/PostOrder.h"
#include "swift/SIL/PrettyStackTrace.h"
#include "swift/SIL/SILDebugScope.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILOpenedArchetypesTracker.h"
#include "swift/SIL/SILVTable.h"
#include "swift/SIL/SILVisitor.h"
#include "swift/SIL/BasicBlockUtils.h"
#include "swift/SIL/TypeLowering.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
using namespace swift;

using Lowering::AbstractionPattern;

// This flag is used only to check that sil-combine can properly
// remove any code after unreachable, thus bringing SIL into
// its canonical form which may get temporarily broken during
// intermediate transformations.
static llvm::cl::opt<bool> SkipUnreachableMustBeLastErrors(
                              "verify-skip-unreachable-must-be-last",
                              llvm::cl::init(false));

// This flag controls the default behaviour when hitting a verification
// failure (abort/exit).
static llvm::cl::opt<bool> AbortOnFailure(
                              "verify-abort-on-failure",
                              llvm::cl::init(true));

// The verifier is basically all assertions, so don't compile it with NDEBUG to
// prevent release builds from triggering spurious unused variable warnings.

//===----------------------------------------------------------------------===//
//                                SILVerifier
//===----------------------------------------------------------------------===//

/// Returns true if A is an opened existential type or is equal to an
/// archetype from F's generic context.
static bool isArchetypeValidInFunction(ArchetypeType *A, const SILFunction *F) {
  if (!A->getOpenedExistentialType().isNull())
    return true;

  // Find the primary archetype.
  A = A->getPrimary();

  // Ok, we have a primary archetype, make sure it is in the nested generic
  // environment of our caller.
  if (auto *genericEnv = F->getGenericEnvironment())
    if (A->getGenericEnvironment() == genericEnv)
      return true;

  return false;
}

namespace {

/// Metaprogramming-friendly base class.
template <class Impl>
class SILVerifierBase : public SILInstructionVisitor<Impl> {
public:
  // visitCLASS calls visitPARENT and checkCLASS.
  // checkCLASS does nothing by default.
#define INST(CLASS, PARENT)                                     \
  void visit##CLASS(CLASS *I) {                                 \
    static_cast<Impl*>(this)->visit##PARENT(I);                 \
    static_cast<Impl*>(this)->check##CLASS(I);                  \
  }                                                             \
  void check##CLASS(CLASS *I) {}
#include "swift/SIL/SILNodes.def"

  void visitSILInstruction(SILInstruction *I) {
    static_cast<Impl*>(this)->checkSILInstruction(I);
  }
  void checkSILInstruction(SILInstruction *I) {}
};
} // end anonymous namespace

namespace {

/// The SIL verifier walks over a SIL function / basic block / instruction,
/// checking and enforcing its invariants.
class SILVerifier : public SILVerifierBase<SILVerifier> {
  ModuleDecl *M;
  const SILFunction &F;
  SILFunctionConventions fnConv;
  Lowering::TypeConverter &TC;
  SILOpenedArchetypesTracker OpenedArchetypes;
  SmallVector<StringRef, 16> DebugVars;
  const SILInstruction *CurInstruction = nullptr;
  DominanceInfo *Dominance = nullptr;
  DeadEndBlocks DEBlocks;
  bool SingleFunction = true;

  SILVerifier(const SILVerifier&) = delete;
  void operator=(const SILVerifier&) = delete;
public:
  bool isSILOwnershipEnabled() const {
    return F.getModule().getOptions().EnableSILOwnership;
  }

  void _require(bool condition, const Twine &complaint,
                const std::function<void()> &extraContext = nullptr) {
    if (condition) return;

    llvm::dbgs() << "SIL verification failed: " << complaint << "\n";

    if (extraContext) extraContext();

    if (CurInstruction) {
      llvm::dbgs() << "Verifying instruction:\n";
      CurInstruction->printInContext(llvm::dbgs());
      llvm::dbgs() << "In function:\n";
      F.print(llvm::dbgs());
    } else {
      llvm::dbgs() << "In function:\n";
      F.print(llvm::dbgs());
    }

    // We abort by default because we want to always crash in
    // the debugger.
    if (AbortOnFailure)
      abort();
    else
      exit(1);
  }
#define require(condition, complaint) \
  _require(bool(condition), complaint ": " #condition)

  template <class T> typename CanTypeWrapperTraits<T>::type
  _requireObjectType(SILType type, const Twine &valueDescription,
                     const char *typeName) {
    _require(type.isObject(), valueDescription + " must be an object");
    auto result = type.getAs<T>();
    _require(bool(result), valueDescription + " must have type " + typeName);
    return result;
  }
  template <class T> typename CanTypeWrapperTraits<T>::type
  _requireObjectType(SILValue value, const Twine &valueDescription,
                     const char *typeName) {
    return _requireObjectType<T>(value->getType(), valueDescription, typeName);
  }
#define requireObjectType(type, value, valueDescription) \
  _requireObjectType<type>(value, valueDescription, #type)

  template <class T> typename CanTypeWrapperTraits<T>::type
  _requireAddressType(SILType type, const Twine &valueDescription,
                      const char *typeName) {
    _require(type.isAddress(), valueDescription + " must be an address");
    auto result = type.getAs<T>();
    _require(bool(result), valueDescription + " must have type " + typeName);
    return result;
  }
  template <class T> typename CanTypeWrapperTraits<T>::type
  _requireAddressType(SILValue value, const Twine &valueDescription,
                     const char *typeName) {
    return _requireAddressType<T>(value->getType(), valueDescription, typeName);
  }
#define requireAddressType(type, value, valueDescription) \
  _requireAddressType<type>(value, valueDescription, #type)

  template <class T>
  typename CanTypeWrapperTraits<T>::type
  _forbidObjectType(SILType type, const Twine &valueDescription,
                    const char *typeName) {
    _require(type.isObject(), valueDescription + " must be an object");
    auto result = type.getAs<T>();
    _require(!bool(result),
             valueDescription + " must not have type " + typeName);
    return result;
  }
  template <class T>
  typename CanTypeWrapperTraits<T>::type
  _forbidObjectType(SILValue value, const Twine &valueDescription,
                    const char *typeName) {
    return _forbidObjectType<T>(value->getType(), valueDescription, typeName);
  }
#define forbidObjectType(type, value, valueDescription)                        \
  _forbidObjectType<type>(value, valueDescription, #type)

  // Require that the operand is a non-optional, non-unowned reference-counted
  // type.
  void requireReferenceValue(SILValue value, const Twine &valueDescription) {
    require(value->getType().isObject(), valueDescription +" must be an object");
    require(value->getType().isReferenceCounted(F.getModule()),
            valueDescription + " must have reference semantics");
    forbidObjectType(UnownedStorageType, value, valueDescription);
  }

  // Require that the operand is a reference-counted type, or an Optional
  // thereof.
  void requireReferenceOrOptionalReferenceValue(SILValue value,
                                                const Twine &valueDescription) {
    require(value->getType().isObject(), valueDescription +" must be an object");
    
    auto objectTy = value->getType().unwrapAnyOptionalType();
    
    require(objectTy.isReferenceCounted(F.getModule()),
            valueDescription + " must have reference semantics");
  }
  
  // Require that the operand is a type that supports reference storage
  // modifiers.
  void requireReferenceStorageCapableValue(SILValue value,
                                           const Twine &valueDescription) {
    requireReferenceOrOptionalReferenceValue(value, valueDescription);
    require(!value->getType().is<SILFunctionType>(),
            valueDescription + " cannot apply to a function type");
  }
  
  /// Assert that two types are equal.
  void requireSameType(SILType type1, SILType type2, const Twine &complaint) {
    _require(type1 == type2, complaint, [&] {
      llvm::dbgs() << "  " << type1 << "\n  " << type2 << '\n';
    });
  }
  
  /// Require two function types to be ABI-compatible.
  void requireABICompatibleFunctionTypes(CanSILFunctionType type1,
                                         CanSILFunctionType type2,
                                         const Twine &what) {
    auto complain = [=](const char *msg) -> std::function<void()> {
      return [=]{
        llvm::dbgs() << "  " << msg << '\n'
                     << "  " << type1 << "\n  " << type2 << '\n';
      };
    };
    auto complainBy = [=](std::function<void()> msg) -> std::function<void()> {
      return [=]{
        msg();
        llvm::dbgs() << '\n';
        llvm::dbgs() << "  " << type1 << "\n  " << type2 << '\n';
      };
    };

    // If we didn't have a failure, return.
    auto Result = type1->isABICompatibleWith(type2);
    if (Result.isCompatible())
      return;

    if (!Result.hasPayload()) {
      _require(false, what, complain(Result.getMessage().data()));
    } else {
      _require(false, what, complainBy([=] {
                 llvm::dbgs() << " " << Result.getMessage().data()
                              << ".\nParameter: " << Result.getPayload();
               }));
    }
  }

  void requireSameFunctionComponents(CanSILFunctionType type1,
                                     CanSILFunctionType type2,
                                     const Twine &what) {
    require(type1->getNumResults() == type2->getNumResults(),
            "results of " + what + " do not match in count");
    for (auto i : indices(type1->getResults())) {
      require(type1->getResults()[i] == type2->getResults()[i],
              "result " + Twine(i) + " of " + what + " do not match");
    }
    require(type1->getParameters().size() ==
            type2->getParameters().size(),
            "inputs of " + what + " do not match in count");
    for (auto i : indices(type1->getParameters())) {
      require(type1->getParameters()[i] ==
              type2->getParameters()[i],
              "input " + Twine(i) + " of " + what + " do not match");
    }
  }

  SILVerifier(const SILFunction &F, bool SingleFunction = true)
      : M(F.getModule().getSwiftModule()), F(F),
        fnConv(F.getLoweredFunctionType(), F.getModule()),
        TC(F.getModule().Types), OpenedArchetypes(&F), Dominance(nullptr),
        DEBlocks(&F), SingleFunction(SingleFunction) {
    if (F.isExternalDeclaration())
      return;
      
    // Check to make sure that all blocks are well formed.  If not, the
    // SILVerifier object will explode trying to compute dominance info.
    for (auto &BB : F) {
      require(!BB.empty(), "Basic blocks cannot be empty");
      require(isa<TermInst>(BB.back()),
              "Basic blocks must end with a terminator instruction");
    }

    Dominance = new DominanceInfo(const_cast<SILFunction *>(&F));

    auto *DebugScope = F.getDebugScope();
    require(DebugScope, "All SIL functions must have a debug scope");
    require(DebugScope->Parent.get<SILFunction *>() == &F,
            "Scope of SIL function points to different function");
  }

  ~SILVerifier() {
    if (Dominance)
      delete Dominance;
  }

  void visitSILArgument(SILArgument *arg) {
    checkLegalType(arg->getFunction(), arg, nullptr);
    checkValueBaseOwnership(arg);
  }

  void visitSILInstruction(SILInstruction *I) {
    CurInstruction = I;
    OpenedArchetypes.registerOpenedArchetypes(I);
    checkSILInstruction(I);

    // Check the SILLLocation attached to the instruction.
    checkInstructionsSILLocation(I);

    // Check ownership.
    SILFunction *F = I->getFunction();
    assert(F && "Expected value base with parent function");

    for (auto result : I->getResults()) {
      checkLegalType(F, result, I);
      checkValueBaseOwnership(result);
    }
  }

  void checkValueBaseOwnership(ValueBase *V) {
    // If ownership is not enabled, bail.
    if (!isSILOwnershipEnabled())
      return;

    SILFunction *F = V->getFunction();
    assert(F && "Expected value base with parent function");
    // If we do not have qualified ownership, then do not verify value base
    // ownership.
    if (!F->hasQualifiedOwnership())
      return;
    SILValue(V).verifyOwnership(F->getModule(), &DEBlocks);
  }

  void checkSILInstruction(SILInstruction *I) {
    const SILBasicBlock *BB = I->getParent();
    require(BB, "Instruction with null parent");

    // Check that non-terminators look ok.
    if (!isa<TermInst>(I)) {
      require(!BB->empty(), "Can't be in a parent block if it is empty");
      if (!I->isStaticInitializerInst()) {
        require(&*BB->rbegin() != I,
                "Non-terminators cannot be the last in a block");
      }
    } else {
      // Skip the check for UnreachableInst, if explicitly asked to do so.
      if (!isa<UnreachableInst>(I) || !SkipUnreachableMustBeLastErrors)
        require(&*BB->rbegin() == I,
                "Terminator must be the last in block");
    }

    // Verify that all of our uses are in this function.
    for (auto result : I->getResults()) {
      for (Operand *use : result->getUses()) {
        auto user = use->getUser();
        require(user, "instruction user is null?");
        require(isa<SILInstruction>(user),
                "instruction used by non-instruction");
        auto userI = cast<SILInstruction>(user);
        require(userI->getParent(),
                "instruction used by unparented instruction");
        if (I->isStaticInitializerInst()) {
          require(userI->getParent() == BB,
                "instruction used by instruction not in same static initializer");
        } else {
          require(userI->getFunction() == &F,
                  "instruction used by instruction in different function");
        }

        auto operands = userI->getAllOperands();
        require(operands.begin() <= use && use <= operands.end(),
                "use doesn't actually belong to instruction it claims to");
      }
    }

    // Verify some basis structural stuff about an instruction's operands.
    for (auto &operand : I->getAllOperands()) {
      require(operand.get(), "instruction has null operand");

      if (auto *valueI = operand.get()->getDefiningInstruction()) {
        require(valueI->getParent(),
                "instruction uses value of unparented instruction");
        if (I->isStaticInitializerInst()) {
          require(valueI->getParent() == BB,
              "instruction uses value which is not in same static initializer");
        } else {
          require(valueI->getFunction() == &F,
                  "instruction uses value of instruction from another function");
          require(Dominance->properlyDominates(valueI, I),
                  "instruction isn't dominated by its operand");
        }
      }
      
      if (auto *valueBBA = dyn_cast<SILArgument>(operand.get())) {
        require(!I->isStaticInitializerInst(),
                "static initializer inst cannot refer to SILArgument");
        require(valueBBA->getParent(),
                "instruction uses value of unparented instruction");
        require(valueBBA->getFunction() == &F,
                "bb argument value from another function");
        require(Dominance->dominates(valueBBA->getParent(), I->getParent()),
                "instruction isn't dominated by its bb argument operand");
      }

      require(operand.getUser() == I,
              "instruction's operand's owner isn't the instruction");
      require(isInValueUses(&operand), "operand value isn't used by operand");

      if (I->isTypeDependentOperand(operand)) {
        require(isa<SILInstruction>(I),
               "opened archetype operand should refer to a SILInstruction");
      }

      // Make sure that if operand is generic that its primary archetypes match
      // the function context.
      checkLegalType(I->getFunction(), operand.get(), I);
    }

    // TODO: There should be a use of an opened archetype inside the instruction for
    // each opened archetype operand of the instruction.
  }

  void checkInstructionsSILLocation(SILInstruction *I) {
    // Check the debug scope.
    auto *DS = I->getDebugScope();
    if (DS && !maybeScopeless(*I))
      require(DS, "instruction has a location, but no scope");

    require(!DS || DS->getParentFunction() == I->getFunction(),
            "debug scope of instruction belongs to a different function");

    // Check the location kind.
    SILLocation L = I->getLoc();
    SILLocation::LocationKind LocKind = L.getKind();
    SILInstructionKind InstKind = I->getKind();

    // Check that there is at most one debug variable defined
    // for each argument slot. This catches SIL transformations
    // that accidentally remove inline information (stored in the SILDebugScope)
    // from debug-variable-carrying instructions.
    if (!DS->InlinedCallSite) {
      SILDebugVariable VarInfo;
      if (auto *DI = dyn_cast<AllocStackInst>(I)) {
        VarInfo = DI->getVarInfo();
      } else if (auto *DI = dyn_cast<AllocBoxInst>(I)) {
        VarInfo = DI->getVarInfo();
      } else if (auto *DI = dyn_cast<DebugValueInst>(I)) {
        VarInfo = DI->getVarInfo();
      } else if (auto *DI = dyn_cast<DebugValueAddrInst>(I)) {
        VarInfo = DI->getVarInfo();
      }

      if (unsigned ArgNo = VarInfo.ArgNo) {
        // It is a function argument.
        if (ArgNo < DebugVars.size() && !DebugVars[ArgNo].empty()) {
          require(DebugVars[ArgNo] == VarInfo.Name,
                  "Scope contains conflicting debug variables for one function "
                  "argument");
        } else {
          // Reserve enough space.
          while (DebugVars.size() <= ArgNo) {
            DebugVars.push_back(StringRef());
          }
        }
        DebugVars[ArgNo] = VarInfo.Name;
      }
    }

    // Regular locations are allowed on all instructions.
    if (LocKind == SILLocation::RegularKind)
      return;

#if 0
    // FIXME: This check was tautological before the removal of
    // AutoreleaseReturnInst, and it turns out that we're violating it.
    // Fix incoming.
    if (LocKind == SILLocation::CleanupKind ||
        LocKind == SILLocation::InlinedKind)
      require(InstKind != SILInstructionKind::ReturnInst ||
              InstKind != SILInstructionKind::AutoreleaseReturnInst,
        "cleanup and inlined locations are not allowed on return instructions");
#endif

    if (LocKind == SILLocation::ReturnKind ||
        LocKind == SILLocation::ImplicitReturnKind)
      require(InstKind == SILInstructionKind::BranchInst ||
              InstKind == SILInstructionKind::ReturnInst ||
              InstKind == SILInstructionKind::UnreachableInst,
        "return locations are only allowed on branch and return instructions");

    if (LocKind == SILLocation::ArtificialUnreachableKind)
      require(InstKind == SILInstructionKind::UnreachableInst,
        "artificial locations are only allowed on Unreachable instructions");
  }

  /// Check that the types of this value producer are all legal in the function
  /// context in which it exists.
  void checkLegalType(SILFunction *F, ValueBase *value, SILInstruction *I) {
    SILType type = value->getType();
    if (type.is<SILTokenType>()) {
      require(isLegalSILTokenProducer(value),
              "SIL tokens can only be produced as the results of specific "
              "instructions");
      return;
    }

    checkLegalType(F, type, I);
  }

  static bool isLegalSILTokenProducer(SILValue value) {
    if (auto beginApply = dyn_cast<BeginApplyResult>(value))
      return beginApply->isTokenResult();

    // Add more token cases here as they arise.

    return false;
  }

  /// Check that the given type is a legal SIL value type.
  void checkLegalType(SILFunction *F, SILType type, SILInstruction *I) {
    checkLegalSILType(F, type.getSwiftRValueType(), I);
  }

  /// Check that the given type is a legal SIL value type.
  void checkLegalSILType(SILFunction *F, CanType rvalueType, SILInstruction *I) {
    // These types should have been removed by lowering.
    require(!isa<LValueType>(rvalueType),
            "l-value types are not legal in SIL");
    require(!isa<AnyFunctionType>(rvalueType),
            "AST function types are not legal in SIL");

    // Tuples should have had their element lowered.
    if (auto tuple = dyn_cast<TupleType>(rvalueType)) {
      for (auto eltTy : tuple.getElementTypes()) {
        checkLegalSILType(F, eltTy, I);
      }
      return;
    }

    // Optionals should have had their objects lowered.
    OptionalTypeKind optKind;
    if (auto objectType = rvalueType.getAnyOptionalObjectType(optKind)) {
      require(optKind == OTK_Optional,
              "ImplicitlyUnwrappedOptional is not legal in SIL values");
      return checkLegalSILType(F, objectType, I);
    }

    // Metatypes should have explicit representations.
    if (auto metatype = dyn_cast<AnyMetatypeType>(rvalueType)) {
      require(metatype->hasRepresentation(),
              "metatypes in SIL must have a representation");;
      // fallthrough for archetype check
    }

    rvalueType.visit([&](CanType t) {
      auto A = dyn_cast<ArchetypeType>(t);
      if (!A)
        return;
      require(isArchetypeValidInFunction(A, F),
              "Operand is of an ArchetypeType that does not exist in the "
              "Caller's generic param list.");
      if (auto OpenedA = getOpenedArchetypeOf(A)) {
        auto Def = OpenedArchetypes.getOpenedArchetypeDef(OpenedA);
        require (Def, "Opened archetype should be registered in SILFunction");
        require(I == nullptr || Def == I ||
                Dominance->properlyDominates(cast<SILInstruction>(Def), I),
                "Use of an opened archetype should be dominated by a "
                "definition of this opened archetype");
      }
    });
  }

  /// Check that this operand appears in the use-chain of the value it uses.
  static bool isInValueUses(const Operand *operand) {
    for (auto use : operand->get()->getUses())
      if (use == operand)
        return true;
    return false;
  }

  /// \return True if all of the users of the AllocStack instruction \p ASI are
  /// inside the same basic block.
  static bool isSingleBlockUsage(AllocStackInst *ASI, DominanceInfo *Dominance){
    SILBasicBlock *BB = ASI->getParent();
    for (auto UI = ASI->use_begin(), E = ASI->use_end(); UI != E; ++UI)
      if (UI->getUser()->getParent() != BB &&
          Dominance->isReachableFromEntry(UI->getUser()->getParent()))
        return false;

    return true;
  }

  void checkAllocStackInst(AllocStackInst *AI) {
    require(AI->getType().isAddress(),
            "result of alloc_stack must be an address type");

    verifyOpenedArchetype(AI, AI->getElementType().getSwiftRValueType());

    // There used to be a check if all uses of ASI are inside the alloc-dealloc
    // range. But apparently it can be the case that ASI has uses after the
    // dealloc_stack. This can come up if the source contains a
    // withUnsafePointer where the pointer escapes.
    // It's illegal code but the compiler should not crash on it.
  }

  void checkAllocRefBase(AllocRefInstBase *ARI) {
    requireReferenceValue(ARI, "Result of alloc_ref");
    verifyOpenedArchetype(ARI, ARI->getType().getSwiftRValueType());
    auto Types = ARI->getTailAllocatedTypes();
    auto Counts = ARI->getTailAllocatedCounts();
    unsigned NumTypes = Types.size();
    require(NumTypes == Counts.size(), "Mismatching types and counts");
    require(NumTypes == 0 || !ARI->isObjC(),
            "Can't tail allocate with ObjC class");
    for (unsigned Idx = 0; Idx < NumTypes; ++Idx) {
      verifyOpenedArchetype(ARI, Types[Idx].getSwiftRValueType());
      require(Counts[Idx].get()->getType().is<BuiltinIntegerType>(),
              "count needs integer type");
    }
  }

  void checkAllocRefInst(AllocRefInst *AI) {
    require(AI->isObjC() || AI->getType().getClassOrBoundGenericClass(),
            "alloc_ref must allocate class");
    checkAllocRefBase(AI);
  }

  void checkAllocRefDynamicInst(AllocRefDynamicInst *ARDI) {
    SILValue Metadata = ARDI->getMetatypeOperand();
    require(Metadata->getType().is<AnyMetatypeType>(),
            "operand of alloc_ref_dynamic must be of metatype type");
    auto metaTy = Metadata->getType().castTo<AnyMetatypeType>();
    require(metaTy->hasRepresentation(),
            "operand of alloc_ref_dynamic must have a metatype representation");
    if (ARDI->isObjC()) {
      require(metaTy->getRepresentation() == MetatypeRepresentation::ObjC,
              "alloc_ref_dynamic @objc requires operand of ObjC metatype");
    } else {
      require(metaTy->getRepresentation() == MetatypeRepresentation::Thick,
              "alloc_ref_dynamic requires operand of thick metatype");
    }
    checkAllocRefBase(ARDI);
  }

  /// Check the substitutions passed to an apply or partial_apply.
  CanSILFunctionType checkApplySubstitutions(SubstitutionList subs,
                                             SILType calleeTy) {
    auto fnTy = requireObjectType(SILFunctionType, calleeTy, "callee operand");

    // If there are substitutions, verify them and apply them to the callee.
    if (subs.empty()) {
      require(!fnTy->isPolymorphic(),
              "callee of apply without substitutions must not be polymorphic");
      return fnTy;
    }
    require(fnTy->isPolymorphic(),
            "callee of apply with substitutions must be polymorphic");

    // Each archetype occurring in the substitutions list should belong to the
    // current function.
    for (auto sub : subs) {
      sub.getReplacement()->getCanonicalType().visit([&](CanType t) {
        auto A = dyn_cast<ArchetypeType>(t);
        if (!A)
          return;
        require(isArchetypeValidInFunction(A, &F),
                "Replacement type of a substitution contains an ArchetypeType "
                "that does not exist in the Caller's generic param list.");
      });
    }

    // Apply the substitutions.
    return fnTy->substGenericArgs(F.getModule(), subs);
  }

  /// Check that for each opened archetype or dynamic self type in substitutions
  /// or the calle type, there is a type dependent operand.
  void checkApplyTypeDependentArguments(ApplySite AS) {
    SILInstruction *AI = AS.getInstruction();

    llvm::DenseSet<ArchetypeType *> FoundOpenedArchetypes;
    unsigned hasDynamicSelf = 0;

    // Function to collect opened archetypes in FoundOpenedArchetypes and set
    // hasDynamicSelf.
    auto HandleType = [&](CanType Ty) {
      if (Ty->isOpenedExistential()) {
        auto A = cast<ArchetypeType>(Ty);
        require(isArchetypeValidInFunction(A, AI->getFunction()),
                "Archetype to be substituted must be valid in function.");
        // Collect all opened archetypes used in the substitutions list.
        FoundOpenedArchetypes.insert(A);
        // Also check that they are properly tracked inside the current
        // function.
        auto Def = OpenedArchetypes.getOpenedArchetypeDef(A);
        require(Def, "Opened archetype should be registered in SILFunction");
        require(Def == AI ||
                Dominance->properlyDominates(cast<SILInstruction>(Def), AI),
                "Use of an opened archetype should be dominated by a "
                "definition of this opened archetype");
      }
      if (Ty->hasDynamicSelfType()) {
        hasDynamicSelf = 1;
      }
    };

    // Search for opened archetypes and dynamic self.
    for (auto &Sub : AS.getSubstitutions()) {
      Sub.getReplacement()->getCanonicalType().visit(HandleType);
    }
    AS.getSubstCalleeType().visit(HandleType);

    require(FoundOpenedArchetypes.size() + hasDynamicSelf ==
                AI->getTypeDependentOperands().size(),
            "Number of opened archetypes and dynamic self in the substitutions "
            "list should match the number of type dependent operands");

    for (auto &Op : AI->getTypeDependentOperands()) {
      auto V = Op.get();
      if (isa<SILArgument>(V)) {
        require(hasDynamicSelf,
                "dynamic self operand without dynamic self type");
        require(AI->getFunction()->hasSelfMetadataParam(),
                "self metadata operand in function without self metadata param");
        require((ValueBase *)V == AI->getFunction()->getSelfMetadataArgument(),
                "wrong self metadata operand");
      } else {
        require(isa<SingleValueInstruction>(V),
                "opened archetype operand should refer to a SIL instruction");
        auto Archetype = getOpenedArchetypeOf(cast<SingleValueInstruction>(V));
        require(Archetype,
                "opened archetype operand should define an opened archetype");
        require(FoundOpenedArchetypes.count(Archetype),
                "opened archetype operand does not correspond to any opened "
                "archetype from the substitutions list");
      }
    }
  }

  void checkFullApplySite(FullApplySite site) {
    checkApplyTypeDependentArguments(site);

    // Then make sure that we have a type that can be substituted for the
    // callee.
    auto substTy = checkApplySubstitutions(site.getSubstitutions(),
                                           site.getCallee()->getType());
    require(site.getOrigCalleeType()->getRepresentation() ==
            site.getSubstCalleeType()->getRepresentation(),
            "calling convention difference between types");

    require(!site.getSubstCalleeType()->isPolymorphic(),
            "substituted callee type should not be generic");

    requireSameType(SILType::getPrimitiveObjectType(substTy),
                    SILType::getPrimitiveObjectType(site.getSubstCalleeType()),
            "substituted callee type does not match substitutions");

    // Check that the arguments and result match.
    SILFunctionConventions substConv(substTy, F.getModule());
    //require(site.getArguments().size() == substTy->getNumSILArguments(),
    require(site.getNumArguments() == substConv.getNumSILArguments(),
            "apply doesn't have right number of arguments for function");
    for (size_t i = 0, size = site.getNumArguments(); i < size; ++i) {
      requireSameType(site.getArguments()[i]->getType(),
                      substConv.getSILArgumentType(i),
                      "operand of 'apply' doesn't match function input type");
    }
  }

  void checkApplyInst(ApplyInst *AI) {
    checkFullApplySite(AI);

    SILFunctionConventions calleeConv(AI->getSubstCalleeType(), F.getModule());
    require(AI->getType() == calleeConv.getSILResultType(),
            "type of apply instruction doesn't match function result type");
    if (AI->isNonThrowing()) {
      require(calleeConv.funcTy->hasErrorResult(),
              "nothrow flag used for callee without error result");
    } else {
      require(!calleeConv.funcTy->hasErrorResult(),
              "apply instruction cannot call function with error result");
    }

    require(!calleeConv.funcTy->isCoroutine(),
            "cannot call coroutine with normal apply");

    // Check that if the apply is of a noreturn callee, make sure that an
    // unreachable is the next instruction.
    if (AI->getModule().getStage() == SILStage::Raw ||
        !AI->isCalleeNoReturn())
      return;
    require(isa<UnreachableInst>(std::next(SILBasicBlock::iterator(AI))),
            "No return apply without an unreachable as a next instruction.");
  }

  void checkTryApplyInst(TryApplyInst *AI) {
    checkFullApplySite(AI);

    SILFunctionConventions calleeConv(AI->getSubstCalleeType(), F.getModule());

    require(!calleeConv.funcTy->isCoroutine(),
            "cannot call coroutine with normal apply");

    auto normalBB = AI->getNormalBB();
    require(normalBB->args_size() == 1,
            "normal destination of try_apply must take one argument");
    requireSameType((*normalBB->args_begin())->getType(),
                    calleeConv.getSILResultType(),
                    "normal destination of try_apply must take argument "
                    "of normal result type");

    auto errorBB = AI->getErrorBB();
    require(calleeConv.funcTy->hasErrorResult(),
            "try_apply must call function with error result");
    require(errorBB->args_size() == 1,
            "error destination of try_apply must take one argument");
    requireSameType((*errorBB->args_begin())->getType(),
                    calleeConv.getSILErrorType(),
                    "error destination of try_apply must take argument "
                    "of error result type");
  }

  void checkBeginApplyInst(BeginApplyInst *AI) {
    checkFullApplySite(AI);

    SILFunctionConventions calleeConv(AI->getSubstCalleeType(), F.getModule());
    auto yieldResults = AI->getYieldedValues();
    auto yields = calleeConv.getYields();
    require(yields.size() == yieldResults.size(),
            "length mismatch in callee yields vs. begin_apply results");
    for (auto i : indices(yields)) {
      require(yieldResults[i]->getType() == calleeConv.getSILType(yields[i]),
              "callee yield type does not match begin_apply result type");
    }

    if (AI->isNonThrowing()) {
      require(calleeConv.funcTy->hasErrorResult(),
              "nothrow flag used for callee without error result");
    } else {
      require(!calleeConv.funcTy->hasErrorResult(),
              "begin_apply instruction cannot call function with error result");
    }

    require(calleeConv.funcTy->getCoroutineKind() == SILCoroutineKind::YieldOnce,
            "must call yield_once coroutine with begin_apply");
  }

  void checkAbortApplyInst(AbortApplyInst *AI) {
    require(isa<BeginApplyResult>(AI->getOperand()) &&
            cast<BeginApplyResult>(AI->getOperand())->isTokenResult(),
            "operand of abort_apply must be a begin_apply");
  }

  void checkEndApplyInst(EndApplyInst *AI) {
    require(isa<BeginApplyResult>(AI->getOperand()) &&
            cast<BeginApplyResult>(AI->getOperand())->isTokenResult(),
            "operand of end_apply must be a begin_apply");
  }

  void verifyLLVMIntrinsic(BuiltinInst *BI, llvm::Intrinsic::ID ID) {
    // Certain llvm intrinsic require constant values as their operands.
    // Consequently, these must not be phi nodes (aka. basic block arguments).
    switch (ID) {
    default:
      break;
    case llvm::Intrinsic::ctlz: // llvm.ctlz
    case llvm::Intrinsic::cttz: // llvm.cttz
      break;
    case llvm::Intrinsic::memcpy:
    case llvm::Intrinsic::memmove:
    case llvm::Intrinsic::memset:
      require(!isa<SILArgument>(BI->getArguments()[3]),
              "alignment argument of memory intrinsics must be an integer "
              "literal");
      require(!isa<SILArgument>(BI->getArguments()[4]),
              "isvolatile argument of memory intrinsics must be an integer "
              "literal");
      break;
    case llvm::Intrinsic::lifetime_start:
    case llvm::Intrinsic::lifetime_end:
    case llvm::Intrinsic::invariant_start:
      require(!isa<SILArgument>(BI->getArguments()[0]),
              "size argument of memory use markers must be an integer literal");
      break;
    case llvm::Intrinsic::invariant_end:
      require(!isa<SILArgument>(BI->getArguments()[1]),
              "llvm.invariant.end parameter #2 must be an integer literal");
      break;
    }
  }

  void checkPartialApplyInst(PartialApplyInst *PAI) {
    auto resultInfo = requireObjectType(SILFunctionType, PAI,
                                        "result of partial_apply");
    verifySILFunctionType(resultInfo);
    require(resultInfo->getExtInfo().hasContext(),
            "result of closure cannot have a thin function type");

    checkApplyTypeDependentArguments(PAI);

    auto substTy = checkApplySubstitutions(PAI->getSubstitutions(),
                                        PAI->getCallee()->getType());

    require(!PAI->getSubstCalleeType()->isPolymorphic(),
            "substituted callee type should not be generic");

    requireSameType(SILType::getPrimitiveObjectType(substTy),
                    SILType::getPrimitiveObjectType(PAI->getSubstCalleeType()),
            "substituted callee type does not match substitutions");

    // The arguments must match the suffix of the original function's input
    // types.
    require(PAI->getArguments().size() +
              resultInfo->getParameters().size()
              == substTy->getParameters().size(),
            "result of partial_apply should take as many inputs as were not "
            "applied by the instruction");

    SILFunctionConventions substConv(substTy, F.getModule());
    unsigned appliedArgStartIdx =
        substConv.getNumSILArguments() - PAI->getNumArguments();
    for (unsigned i = 0, size = PAI->getArguments().size(); i < size; ++i) {
      require(PAI->getArguments()[i]->getType()
                  == substConv.getSILArgumentType(appliedArgStartIdx + i),
              "applied argument types do not match suffix of function type's "
              "inputs");
    }

    // The arguments to the result function type must match the prefix of the
    // original function's input types.
    for (unsigned i = 0, size = resultInfo->getParameters().size();
         i < size; ++i) {
      require(resultInfo->getParameters()[i] ==
              substTy->getParameters()[i],
              "inputs to result function type do not match unapplied inputs "
              "of original function");
    }

    require(resultInfo->getNumResults() == substTy->getNumResults(),
            "applied results do not agree in count with function type");
    for (unsigned i = 0, size = resultInfo->getNumResults(); i < size; ++i) {
      auto originalResult = resultInfo->getResults()[i];
      auto expectedResult = substTy->getResults()[i];

      // The "returns inner pointer" convention doesn't survive through a
      // partial application, since the thunk takes responsibility for
      // lifetime-extending 'self'.
      if (expectedResult.getConvention()
            == ResultConvention::UnownedInnerPointer) {
        expectedResult = SILResultInfo(expectedResult.getType(),
                                       ResultConvention::Unowned);
        require(originalResult == expectedResult,
                "result type of result function type for partially applied "
                "@unowned_inner_pointer function should have @unowned"
                "convention");

      // The "autoreleased" convention doesn't survive through a
      // partial application, since the thunk takes responsibility for
      // retaining the return value.
      } else if (expectedResult.getConvention()
            == ResultConvention::Autoreleased) {
        expectedResult = SILResultInfo(expectedResult.getType(),
                                       ResultConvention::Owned);
        require(originalResult == expectedResult,
                "result type of result function type for partially applied "
                "@autoreleased function should have @owned convention");

      } else {
        require(originalResult == expectedResult,
                "result type of result function type does not match original "
                "function");
      }
    }
    
    // TODO: Impose additional constraints when partial_apply when the
    // -disable-sil-partial-apply flag is enabled. We want to reduce
    // partial_apply to being only a means of associating a closure invocation
    // function with its context.
    //
    // When we reach that point, we should be able to more deeply redesign
    // PartialApplyInst to simplify the representation to carry a single
    // argument.
    if (PAI->getModule().getOptions().DisableSILPartialApply) {
      // Should only be one context argument.
      require(PAI->getArguments().size() == 1,
              "partial_apply should have a single context argument");
      
      // Callee should already have the thin convention, and result should be
      // thick.
      require(resultInfo->getRepresentation() ==
                SILFunctionTypeRepresentation::Thick,
              "partial_apply result should have thick convention");
      require(PAI->getCallee()->getType().castTo<SILFunctionType>()
                ->getRepresentation() ==
              SILFunctionTypeRepresentation::Thin,
              "partial_apply callee should have thin convention");
      
      // TODO: Check that generic signature matches box's generic signature,
      // once we have boxes with generic signatures.
      require(!PAI->getCalleeFunction()->getGenericEnvironment(),
              "partial_apply context must capture generic environment for "
              "callee");
      
      // Result's callee convention should match context argument's convention.
      require(substTy->getParameters().back().getConvention()
                == resultInfo->getCalleeConvention(),
              "partial_apply context argument must have the same convention "
              "as the resulting function's callee convention");
      
      auto isSwiftRefcounted = [](SILType t) -> bool {
        if (t.is<SILBoxType>())
          return true;
        if (t.getSwiftRValueType() == t.getASTContext().TheNativeObjectType)
          return true;
        if (auto clas = t.getClassOrBoundGenericClass())
          // Must be a class defined in Swift.
          return clas->hasKnownSwiftImplementation();
        return false;
      };
      
      // The context argument must be a swift-refcounted box or class.
      require(isSwiftRefcounted(PAI->getArguments().front()->getType()),
              "partial_apply context argument must be swift-refcounted");
    }
  }

  void checkBuiltinInst(BuiltinInst *BI) {
    // Check for special constraints on llvm intrinsics.
    if (BI->getIntrinsicInfo().ID != llvm::Intrinsic::not_intrinsic)
      verifyLLVMIntrinsic(BI, BI->getIntrinsicInfo().ID);
  }
  
  void checkFunctionRefInst(FunctionRefInst *FRI) {
    auto fnType = requireObjectType(SILFunctionType, FRI,
                                    "result of function_ref");
    require(!fnType->getExtInfo().hasContext(),
            "function_ref should have a context-free function result");

    // Note: in SingleFunction mode, we relax some of these checks because
    // we may not have linked everything yet.

    SILFunction *RefF = FRI->getReferencedFunction();

    // A direct reference to a shared_external declaration is an error; we
    // should have deserialized a body.
    if (RefF->isExternalDeclaration()) {
      require(SingleFunction ||
              !hasSharedVisibility(RefF->getLinkage()) ||
              RefF->hasForeignBody(),
              "external declarations of SILFunctions with shared visibility is "
              "not allowed");
    }

    // A direct reference to a non-public or shared but not fragile function
    // from a fragile function is an error.
    //
    // Exception: When compiling OnoneSupport anything can reference anything,
    // because the bodies of functions are never SIL serialized, but
    // specializations are exposed as public symbols in the produced object
    // files. For the same reason, KeepAsPublic functions (i.e. specializations)
    // can refer to anything or can be referenced from any other function.
    if (!F.getModule().isOptimizedOnoneSupportModule() &&
        !(F.isKeepAsPublic() || RefF->isKeepAsPublic())) {
      if (F.isSerialized()) {
        require((SingleFunction && RefF->isExternalDeclaration()) ||
                    RefF->hasValidLinkageForFragileRef(),
                "function_ref inside fragile function cannot "
                "reference a private or hidden symbol");
      }
    }
    verifySILFunctionType(fnType);
  }

  void checkAllocGlobalInst(AllocGlobalInst *AGI) {
    if (F.isSerialized()) {
      SILGlobalVariable *RefG = AGI->getReferencedGlobal();
      require(RefG->isSerialized()
                || hasPublicVisibility(RefG->getLinkage()),
              "alloc_global inside fragile function cannot "
              "reference a private or hidden symbol");
    }
  }

  void checkGlobalAccessInst(GlobalAccessInst *GAI) {
    SILGlobalVariable *RefG = GAI->getReferencedGlobal();
    require(GAI->getType().getObjectType() == RefG->getLoweredType(),
            "global_addr/value must be the type of the variable it references");
    if (F.isSerialized()) {
      require(RefG->isSerialized()
              || hasPublicVisibility(RefG->getLinkage()),
              "global_addr/value inside fragile function cannot "
              "reference a private or hidden symbol");
    }
  }

  void checkGlobalAddrInst(GlobalAddrInst *GAI) {
    require(GAI->getType().isAddress(),
            "global_addr must have an address result type");
    require(!GAI->getReferencedGlobal()->isInitializedObject(),
            "global_addr cannot refer to a statically initialized object");
    checkGlobalAccessInst(GAI);
  }

  void checkGlobalValueInst(GlobalValueInst *GVI) {
    require(GVI->getType().isObject(),
            "global_value must have an address result type");
    checkGlobalAccessInst(GVI);
  }

  void checkObjectInst(ObjectInst *) {
    require(false, "object instruction is only allowed in a static initializer");
  }

  void checkIntegerLiteralInst(IntegerLiteralInst *ILI) {
    require(ILI->getType().is<BuiltinIntegerType>(),
            "invalid integer literal type");
  }

  void checkLoadInst(LoadInst *LI) {
    require(LI->getType().isObject(), "Result of load must be an object");
    require(!fnConv.useLoweredAddresses()
                || LI->getType().isLoadable(LI->getModule()),
            "Load must have a loadable type");
    require(LI->getOperand()->getType().isAddress(),
            "Load operand must be an address");
    require(LI->getOperand()->getType().getObjectType() == LI->getType(),
            "Load operand type and result type mismatch");

    // Ownership semantic checks.
    switch (LI->getOwnershipQualifier()) {
    case LoadOwnershipQualifier::Unqualified:
      // We should not see loads with unqualified ownership when SILOwnership is
      // enabled.
      require(!F.hasQualifiedOwnership(),
              "Load with unqualified ownership in a qualified function");
      break;
    case LoadOwnershipQualifier::Copy:
    case LoadOwnershipQualifier::Take:
      require(F.hasQualifiedOwnership(),
              "Load with qualified ownership in an unqualified function");
      // TODO: Could probably make this a bit stricter.
      require(!LI->getType().isTrivial(LI->getModule()),
              "load [copy] or load [take] can only be applied to non-trivial "
              "types");
      break;
    case LoadOwnershipQualifier::Trivial:
      require(F.hasQualifiedOwnership(),
              "Load with qualified ownership in an unqualified function");
      require(LI->getType().isTrivial(LI->getModule()),
              "A load with trivial ownership must load a trivial type");
      break;
    }
  }

  void checkLoadBorrowInst(LoadBorrowInst *LBI) {
    require(
        F.hasQualifiedOwnership(),
        "Inst with qualified ownership in a function that is not qualified");
    require(LBI->getType().isObject(), "Result of load must be an object");
    require(!fnConv.useLoweredAddresses()
            || LBI->getType().isLoadable(LBI->getModule()),
            "Load must have a loadable type");
    require(LBI->getOperand()->getType().isAddress(),
            "Load operand must be an address");
    require(LBI->getOperand()->getType().getObjectType() == LBI->getType(),
            "Load operand type and result type mismatch");
  }

  void checkEndBorrowInst(EndBorrowInst *EBI) {
    require(
        F.hasQualifiedOwnership(),
        "Inst with qualified ownership in a function that is not qualified");
    // We allow for end_borrow to express relationships in between addresses and
    // values, but we require that the types are the same ignoring value
    // category.
    require(EBI->getBorrowedValue()->getType().getObjectType() ==
                EBI->getOriginalValue()->getType().getObjectType(),
            "end_borrow can only relate the same types ignoring value "
            "category");
  }

  void checkBeginAccessInst(BeginAccessInst *BAI) {
    auto op = BAI->getOperand();
    requireSameType(BAI->getType(), op->getType(),
                    "result must be same type as operand");
    require(BAI->getType().isAddress(),
            "begin_access operand must have address type");

    if (BAI->getModule().getStage() != SILStage::Raw) {
      require(BAI->getEnforcement() != SILAccessEnforcement::Unknown,
              "access must have known enforcement outside raw stage");
    }

    switch (BAI->getAccessKind()) {
    case SILAccessKind::Init:
    case SILAccessKind::Deinit:
      require(BAI->getEnforcement() == SILAccessEnforcement::Static,
              "init/deinit accesses cannot use non-static enforcement");
      break;

    case SILAccessKind::Read:
    case SILAccessKind::Modify:
      break;
    }
  }

  void checkEndAccessInst(EndAccessInst *EAI) {
    auto BAI = dyn_cast<BeginAccessInst>(EAI->getOperand());
    require(BAI != nullptr,
            "operand of end_access must be a begin_access");

    if (EAI->isAborting()) {
      require(BAI->getAccessKind() == SILAccessKind::Init ||
              BAI->getAccessKind() == SILAccessKind::Deinit,
              "aborting access must apply to init or deinit");
    }
  }

  void checkBeginUnpairedAccessInst(BeginUnpairedAccessInst *I) {
    require(I->getEnforcement() != SILAccessEnforcement::Unknown,
            "unpaired access can never use unknown enforcement");
    require(I->getSource()->getType().isAddress(),
            "address operand must have address type");
    requireAddressType(BuiltinUnsafeValueBufferType, I->getBuffer(),
                       "scratch buffer operand");
  }

  void checkEndUnpairedAccessInst(EndUnpairedAccessInst *I) {
    require(I->getEnforcement() != SILAccessEnforcement::Unknown,
            "unpaired access can never use unknown enforcement");
    requireAddressType(BuiltinUnsafeValueBufferType, I->getBuffer(),
                       "scratch buffer operand");
  }

  void checkStoreInst(StoreInst *SI) {
    require(SI->getSrc()->getType().isObject(),
            "Can't store from an address source");
    require(!fnConv.useLoweredAddresses()
                || SI->getSrc()->getType().isLoadable(SI->getModule()),
            "Can't store a non loadable type");
    require(SI->getDest()->getType().isAddress(),
            "Must store to an address dest");
    require(SI->getDest()->getType().getObjectType() == SI->getSrc()->getType(),
            "Store operand type and dest type mismatch");

    // Perform ownership checks.
    switch (SI->getOwnershipQualifier()) {
    case StoreOwnershipQualifier::Unqualified:
      // We should not see loads with unqualified ownership when SILOwnership is
      // enabled.
      require(!F.hasQualifiedOwnership(),
              "Qualified store in function with unqualified ownership?!");
      break;
    case StoreOwnershipQualifier::Init:
    case StoreOwnershipQualifier::Assign:
      require(
          F.hasQualifiedOwnership(),
          "Inst with qualified ownership in a function that is not qualified");
      // TODO: Could probably make this a bit stricter.
      require(!SI->getSrc()->getType().isTrivial(SI->getModule()),
              "store [init] or store [assign] can only be applied to "
              "non-trivial types");
      break;
    case StoreOwnershipQualifier::Trivial: {
      require(
          F.hasQualifiedOwnership(),
          "Inst with qualified ownership in a function that is not qualified");
      SILValue Src = SI->getSrc();
      require(Src->getType().isTrivial(SI->getModule()) ||
              Src.getOwnershipKind() == ValueOwnershipKind::Trivial,
              "A store with trivial ownership must store a type with trivial "
              "ownership");
      break;
    }
    }
  }

  void checkAssignInst(AssignInst *AI) {
    SILValue Src = AI->getSrc(), Dest = AI->getDest();
    require(AI->getModule().getStage() == SILStage::Raw,
            "assign instruction can only exist in raw SIL");
    require(Src->getType().isObject(), "Can't assign from an address source");
    require(Dest->getType().isAddress(), "Must store to an address dest");
    require(Dest->getType().getObjectType() == Src->getType(),
            "Store operand type and dest type mismatch");
  }

  void checkLoadUnownedInst(LoadUnownedInst *LUI) {
    require(LUI->getType().isObject(), "Result of load must be an object");
    auto PointerType = LUI->getOperand()->getType();
    auto PointerRVType = PointerType.getSwiftRValueType();
    require(PointerType.isAddress() &&
            PointerRVType->is<UnownedStorageType>(),
            "load_unowned operand must be an unowned address");
    require(PointerRVType->getReferenceStorageReferent()->getCanonicalType() ==
            LUI->getType().getSwiftRValueType(),
            "Load operand type and result type mismatch");
  }

  void checkStoreUnownedInst(StoreUnownedInst *SUI) {
    require(SUI->getSrc()->getType().isObject(),
            "Can't store from an address source");
    auto PointerType = SUI->getDest()->getType();
    auto PointerRVType = PointerType.getSwiftRValueType();
    require(PointerType.isAddress() &&
            PointerRVType->is<UnownedStorageType>(),
            "store_unowned address operand must be an unowned address");
    require(PointerRVType->getReferenceStorageReferent()->getCanonicalType() ==
            SUI->getSrc()->getType().getSwiftRValueType(),
            "Store operand type and dest type mismatch");
  }

  void checkLoadWeakInst(LoadWeakInst *LWI) {
    require(LWI->getType().isObject(), "Result of load must be an object");
    require(LWI->getType().getAnyOptionalObjectType(),
            "Result of weak load must be an optional");
    auto PointerType = LWI->getOperand()->getType();
    auto PointerRVType = PointerType.getSwiftRValueType();
    require(PointerType.isAddress() &&
            PointerRVType->is<WeakStorageType>(),
            "load_weak operand must be a weak address");
    require(PointerRVType->getReferenceStorageReferent()->getCanonicalType() ==
            LWI->getType().getSwiftRValueType(),
            "Load operand type and result type mismatch");
  }

  void checkStoreWeakInst(StoreWeakInst *SWI) {
    require(SWI->getSrc()->getType().isObject(),
            "Can't store from an address source");
    require(SWI->getSrc()->getType().getAnyOptionalObjectType(),
            "store_weak must be of an optional value");
    auto PointerType = SWI->getDest()->getType();
    auto PointerRVType = PointerType.getSwiftRValueType();
    require(PointerType.isAddress() &&
            PointerRVType->is<WeakStorageType>(),
            "store_weak address operand must be a weak address");
    require(PointerRVType->getReferenceStorageReferent()->getCanonicalType() ==
            SWI->getSrc()->getType().getSwiftRValueType(),
            "Store operand type and dest type mismatch");
  }

  void checkMarkUninitializedInst(MarkUninitializedInst *MU) {
    SILValue Src = MU->getOperand();
    require(MU->getModule().getStage() == SILStage::Raw,
            "mark_uninitialized instruction can only exist in raw SIL");
    require(Src->getType().isAddress() ||
                Src->getType()
                    .getSwiftRValueType()
                    ->getClassOrBoundGenericClass() ||
                Src->getType().getAs<SILBoxType>(),
            "mark_uninitialized must be an address, class, or box type");
    require(Src->getType() == MU->getType(),"operand and result type mismatch");
#if 0
    // This will be turned back on in a couple of commits.
    require(isa<AllocationInst>(Src) || isa<SILArgument>(Src),
            "Mark Uninitialized should always be on the storage location");
#endif
  }
  
  void checkMarkUninitializedBehaviorInst(MarkUninitializedBehaviorInst *MU) {
    require(MU->getModule().getStage() == SILStage::Raw,
            "mark_uninitialized instruction can only exist in raw SIL");
    auto InitStorage = MU->getInitStorageFunc();
    auto InitStorageTy = InitStorage->getType().getAs<SILFunctionType>();
    require(InitStorageTy,
            "mark_uninitialized initializer must be a function");
    auto SubstInitStorageTy = InitStorageTy->substGenericArgs(F.getModule(),
                                             MU->getInitStorageSubstitutions());
    // FIXME: Destructured value or results?
    require(SubstInitStorageTy->getResults().size() == 1,
            "mark_uninitialized initializer must have one result");
    auto StorageTy = SILType::getPrimitiveAddressType(
                              SubstInitStorageTy->getSingleResult().getType());
    requireSameType(StorageTy, MU->getStorage()->getType(),
                    "storage must be address of initializer's result type");
    
    auto Setter = MU->getSetterFunc();
    auto SetterTy = Setter->getType().getAs<SILFunctionType>();
    require(SetterTy,
            "mark_uninitialized setter must be a function");
    auto SubstSetterTy = SetterTy->substGenericArgs(F.getModule(),
                                               MU->getSetterSubstitutions());
    require(SubstSetterTy->getParameters().size() == 2,
            "mark_uninitialized setter must have a value and self param");
    requireSameType(fnConv.getSILType(SubstSetterTy->getSelfParameter()),
                    MU->getSelf()->getType(),
                    "self type must match setter's self parameter type");

    auto ValueTy = SubstInitStorageTy->getParameters()[0].getType();
    requireSameType(SILType::getPrimitiveAddressType(ValueTy),
                    SILType::getPrimitiveAddressType(
                      SubstSetterTy->getParameters()[0].getType()),
                    "value parameter type must match between initializer "
                    "and setter");
    
    auto ValueAddrTy = SILType::getPrimitiveAddressType(ValueTy);
    requireSameType(ValueAddrTy, MU->getType(),
                    "result of mark_uninitialized_behavior should be address "
                    "of value parameter to setter and initializer");
  }
  
  void checkMarkFunctionEscapeInst(MarkFunctionEscapeInst *MFE) {
    require(MFE->getModule().getStage() == SILStage::Raw,
            "mark_function_escape instruction can only exist in raw SIL");
    for (auto Elt : MFE->getElements())
      require(Elt->getType().isAddress(), "MFE must refer to variable addrs");
  }

  void checkCopyAddrInst(CopyAddrInst *SI) {
    require(SI->getSrc()->getType().isAddress(),
            "Src value should be lvalue");
    require(SI->getDest()->getType().isAddress(),
            "Dest address should be lvalue");
    require(SI->getDest()->getType() == SI->getSrc()->getType(),
            "Store operand type and dest type mismatch");
  }

  void checkRetainValueInst(RetainValueInst *I) {
    require(I->getOperand()->getType().isObject(),
            "Source value should be an object value");
    require(!F.hasQualifiedOwnership(),
            "retain_value is only in functions with unqualified ownership");
  }

  void checkRetainValueAddrInst(RetainValueAddrInst *I) {
    require(I->getOperand()->getType().isAddress(),
            "Source value should be an address value");
    require(!F.hasQualifiedOwnership(),
            "retain_value is only in functions with unqualified ownership");
  }

  void checkCopyValueInst(CopyValueInst *I) {
    require(I->getOperand()->getType().isObject(),
            "Source value should be an object value");
    require(!fnConv.useLoweredAddresses() || F.hasQualifiedOwnership(),
            "copy_value is only valid in functions with qualified "
            "ownership");
  }

  void checkCopyUnownedValueInst(CopyUnownedValueInst *I) {
    auto unownedType = requireObjectType(UnownedStorageType, I->getOperand(),
                                         "Operand of unowned_retain");
    require(unownedType->isLoadable(ResilienceExpansion::Maximal),
            "unowned_retain requires unowned type to be loadable");
    require(F.hasQualifiedOwnership(),
            "copy_unowned_value is only valid in functions with qualified "
            "ownership");
  }

  void checkDestroyValueInst(DestroyValueInst *I) {
    require(I->getOperand()->getType().isObject(),
            "Source value should be an object value");
    require(!fnConv.useLoweredAddresses() || F.hasQualifiedOwnership(),
            "destroy_value is only valid in functions with qualified "
            "ownership");
  }

  void checkReleaseValueInst(ReleaseValueInst *I) {
    require(I->getOperand()->getType().isObject(),
            "Source value should be an object value");
    require(!F.hasQualifiedOwnership(),
            "release_value is only in functions with unqualified ownership");
  }

  void checkReleaseValueAddrInst(ReleaseValueAddrInst *I) {
    require(I->getOperand()->getType().isAddress(),
            "Source value should be an address value");
    require(!F.hasQualifiedOwnership(),
            "release_value is only in functions with unqualified ownership");
  }

  void checkAutoreleaseValueInst(AutoreleaseValueInst *I) {
    require(I->getOperand()->getType().isObject(),
            "Source value should be an object value");
    // TODO: This instruction could in principle be generalized.
    require(I->getOperand()->getType().hasRetainablePointerRepresentation(),
            "Source value must be a reference type or optional thereof");
  }
  
  void checkSetDeallocatingInst(SetDeallocatingInst *I) {
    require(I->getOperand()->getType().isObject(),
            "Source value should be an object value");
    require(I->getOperand()->getType().hasRetainablePointerRepresentation(),
            "Source value must be a reference type");
  }
  
  void checkCopyBlockInst(CopyBlockInst *I) {
    require(I->getOperand()->getType().isBlockPointerCompatible(),
            "operand of copy_block should be a block");
    require(I->getOperand()->getType() == I->getType(),
            "result of copy_block should be same type as operand");
  }

  void checkAllocValueBufferInst(AllocValueBufferInst *I) {
    require(I->getOperand()->getType().isAddress(),
            "Operand value should be an address");
    require(I->getOperand()->getType().is<BuiltinUnsafeValueBufferType>(),
            "Operand value should be a Builtin.UnsafeValueBuffer");
    verifyOpenedArchetype(I, I->getValueType().getSwiftRValueType());
  }

  void checkProjectValueBufferInst(ProjectValueBufferInst *I) {
    require(I->getOperand()->getType().isAddress(),
            "Operand value should be an address");
    require(I->getOperand()->getType().is<BuiltinUnsafeValueBufferType>(),
            "Operand value should be a Builtin.UnsafeValueBuffer");
  }

  void checkProjectBoxInst(ProjectBoxInst *I) {
    require(I->getOperand()->getType().isObject(),
            "project_box operand should be a value");
    auto boxTy = I->getOperand()->getType().getAs<SILBoxType>();
    require(boxTy, "project_box operand should be a @box type");
    require(I->getType() == boxTy->getFieldType(F.getModule(),
                                                I->getFieldIndex()),
            "project_box result should be address of boxed type");

    // If we have a mark_uninitialized as a user, the mark_uninitialized must be
    // our only user. This is a requirement that is asserted by allocbox to
    // stack. This check just embeds the requirement into the IR.
    require(I->hasOneUse() ||
            none_of(I->getUses(),
                    [](Operand *Op) -> bool {
                      return isa<MarkUninitializedInst>(Op->getUser());
                    }),
            "project_box with more than one user when a user is a "
            "mark_uninitialized");
  }

  void checkProjectExistentialBoxInst(ProjectExistentialBoxInst *PEBI) {
    SILType operandType = PEBI->getOperand()->getType();
    require(operandType.isObject(),
            "project_existential_box operand must not be address");

    require(operandType.canUseExistentialRepresentation(F.getModule(),
                                              ExistentialRepresentation::Boxed),
            "project_existential_box operand must be boxed existential");

    require(PEBI->getType().isAddress(),
            "project_existential_box result must be an address");

    if (auto *AEBI = dyn_cast<AllocExistentialBoxInst>(PEBI->getOperand())) {
      // The lowered type must be the properly-abstracted form of the AST type.
      SILType exType = AEBI->getExistentialType();
      auto archetype = ArchetypeType::getOpened(exType.getSwiftRValueType());

      auto loweredTy = F.getModule().Types.getLoweredType(
                                      Lowering::AbstractionPattern(archetype),
                                      AEBI->getFormalConcreteType())
                                        .getAddressType();

      requireSameType(loweredTy, PEBI->getType(),
              "project_existential_box result should be the lowered "
              "concrete type of its alloc_existential_box");
    }
  }
  
  void checkDeallocValueBufferInst(DeallocValueBufferInst *I) {
    require(I->getOperand()->getType().isAddress(),
            "Operand value should be an address");
    require(I->getOperand()->getType().is<BuiltinUnsafeValueBufferType>(),
            "Operand value should be a Builtin.UnsafeValueBuffer");
  }
  
  void checkStructInst(StructInst *SI) {
    auto *structDecl = SI->getType().getStructOrBoundGenericStruct();
    require(structDecl, "StructInst must return a struct");
    require(!structDecl->hasUnreferenceableStorage(),
            "Cannot build a struct with unreferenceable storage from elements "
            "using StructInst");
    require(SI->getType().isObject(),
            "StructInst must produce an object");

    SILType structTy = SI->getType();
    auto opi = SI->getElements().begin(), opEnd = SI->getElements().end();
    for (VarDecl *field : structDecl->getStoredProperties()) {
      require(opi != opEnd,
              "number of struct operands does not match number of stored "
              "member variables of struct");

      SILType loweredType = structTy.getFieldType(field, F.getModule());
      if (SI->getModule().getStage() != SILStage::Lowered) {
        require((*opi)->getType() == loweredType,
                "struct operand type does not match field type");
      }
      ++opi;
    }
  }

  void checkEnumInst(EnumInst *UI) {
    EnumDecl *ud = UI->getType().getEnumOrBoundGenericEnum();
    require(ud, "EnumInst must return an enum");
    require(UI->getElement()->getParentEnum() == ud,
            "EnumInst case must be a case of the result enum type");
    require(UI->getType().isObject(),
            "EnumInst must produce an object");
    require(UI->hasOperand() == UI->getElement()->hasAssociatedValues(),
            "EnumInst must take an argument iff the element does");

    if (UI->getElement()->hasAssociatedValues()) {
      require(UI->getOperand()->getType().isObject(),
              "EnumInst operand must be an object");
      SILType caseTy = UI->getType().getEnumElementType(UI->getElement(),
                                                        F.getModule());
      require(caseTy == UI->getOperand()->getType(),
              "EnumInst operand type does not match type of case");
    }
  }

  void checkInitEnumDataAddrInst(InitEnumDataAddrInst *UI) {
    EnumDecl *ud = UI->getOperand()->getType().getEnumOrBoundGenericEnum();
    require(ud, "InitEnumDataAddrInst must take an enum operand");
    require(UI->getElement()->getParentEnum() == ud,
            "InitEnumDataAddrInst case must be a case of the enum operand type");
    require(UI->getElement()->hasAssociatedValues(),
            "InitEnumDataAddrInst case must have a data type");
    require(UI->getOperand()->getType().isAddress(),
            "InitEnumDataAddrInst must take an address operand");
    require(UI->getType().isAddress(),
            "InitEnumDataAddrInst must produce an address");

    SILType caseTy =
      UI->getOperand()->getType().getEnumElementType(UI->getElement(),
                                                    F.getModule());
    requireSameType(caseTy, UI->getType(),
            "InitEnumDataAddrInst result does not match type of enum case");
  }

  void checkUncheckedEnumDataInst(UncheckedEnumDataInst *UI) {
    EnumDecl *ud = UI->getOperand()->getType().getEnumOrBoundGenericEnum();
    require(ud, "UncheckedEnumData must take an enum operand");
    require(UI->getElement()->getParentEnum() == ud,
            "UncheckedEnumData case must be a case of the enum operand type");
    require(UI->getElement()->getArgumentInterfaceType(),
            "UncheckedEnumData case must have a data type");
    require(UI->getOperand()->getType().isObject(),
            "UncheckedEnumData must take an address operand");
    require(UI->getType().isObject(),
            "UncheckedEnumData must produce an address");

    SILType caseTy =
      UI->getOperand()->getType().getEnumElementType(UI->getElement(),
                                                    F.getModule());
    require(caseTy == UI->getType(),
            "UncheckedEnumData result does not match type of enum case");
  }

  void checkUncheckedTakeEnumDataAddrInst(UncheckedTakeEnumDataAddrInst *UI) {
    EnumDecl *ud = UI->getOperand()->getType().getEnumOrBoundGenericEnum();
    require(ud, "UncheckedTakeEnumDataAddrInst must take an enum operand");
    require(UI->getElement()->getParentEnum() == ud,
            "UncheckedTakeEnumDataAddrInst case must be a case of the enum operand type");
    require(UI->getElement()->getArgumentInterfaceType(),
            "UncheckedTakeEnumDataAddrInst case must have a data type");
    require(UI->getOperand()->getType().isAddress(),
            "UncheckedTakeEnumDataAddrInst must take an address operand");
    require(UI->getType().isAddress(),
            "UncheckedTakeEnumDataAddrInst must produce an address");

    SILType caseTy =
      UI->getOperand()->getType().getEnumElementType(UI->getElement(),
                                                    F.getModule());
    require(caseTy == UI->getType(),
            "UncheckedTakeEnumDataAddrInst result does not match type of enum case");
  }

  void checkInjectEnumAddrInst(InjectEnumAddrInst *IUAI) {
    require(IUAI->getOperand()->getType().is<EnumType>()
              || IUAI->getOperand()->getType().is<BoundGenericEnumType>(),
            "InjectEnumAddrInst must take an enum operand");
    require(IUAI->getElement()->getParentEnum()
              == IUAI->getOperand()->getType().getEnumOrBoundGenericEnum(),
            "InjectEnumAddrInst case must be a case of the enum operand type");
    require(IUAI->getOperand()->getType().isAddress(),
            "InjectEnumAddrInst must take an address operand");
  }

  void checkTupleInst(TupleInst *TI) {
    CanTupleType ResTy = requireObjectType(TupleType, TI, "Result of tuple");

    require(TI->getElements().size() == ResTy->getNumElements(),
            "Tuple field count mismatch!");

    if (TI->getModule().getStage() != SILStage::Lowered) {
      for (size_t i = 0, size = TI->getElements().size(); i < size; ++i) {
        require(TI->getElement(i)->getType().getSwiftRValueType() ==
                    ResTy.getElementType(i),
                "Tuple element arguments do not match tuple type!");
      }
    }
  }

  // Is a SIL type a potential lowering of a formal type?
  bool isLoweringOf(SILType loweredType, CanType formalType) {
    return loweredType.isLoweringOf(F.getModule(), formalType);
  }
  
  void checkMetatypeInst(MetatypeInst *MI) {
    require(MI->getType().is<MetatypeType>(),
            "metatype instruction must be of metatype type");
    auto MetaTy = MI->getType().castTo<MetatypeType>();
    require(MetaTy->hasRepresentation(),
            "metatype instruction must have a metatype representation");
    verifyOpenedArchetype(MI, MetaTy.getInstanceType());
  }
  void checkValueMetatypeInst(ValueMetatypeInst *MI) {
    require(MI->getType().is<MetatypeType>(),
            "value_metatype instruction must be of metatype type");
    require(MI->getType().castTo<MetatypeType>()->hasRepresentation(),
            "value_metatype instruction must have a metatype representation");
    auto formalInstanceTy
      = MI->getType().castTo<MetatypeType>().getInstanceType();
    require(isLoweringOf(MI->getOperand()->getType(), formalInstanceTy),
            "value_metatype result must be formal metatype of "
            "lowered operand type");
  }
  void checkExistentialMetatypeInst(ExistentialMetatypeInst *MI) {
    require(MI->getType().is<ExistentialMetatypeType>(),
            "existential_metatype instruction must be of metatype type");
    require(MI->getType().castTo<ExistentialMetatypeType>()->hasRepresentation(),
            "value_metatype instruction must have a metatype representation");
    require(MI->getOperand()->getType().isAnyExistentialType(),
            "existential_metatype operand must be of protocol type");
    auto formalInstanceTy
      = MI->getType().castTo<ExistentialMetatypeType>().getInstanceType();
    require(isLoweringOf(MI->getOperand()->getType(), formalInstanceTy),
            "existential_metatype result must be formal metatype of "
            "lowered operand type");
  }

  void checkStrongRetainInst(StrongRetainInst *RI) {
    requireReferenceValue(RI->getOperand(), "Operand of strong_retain");
    require(!F.hasQualifiedOwnership(),
            "strong_retain is only in functions with unqualified ownership");
  }
  void checkStrongReleaseInst(StrongReleaseInst *RI) {
    requireReferenceValue(RI->getOperand(), "Operand of release");
    require(!F.hasQualifiedOwnership(),
            "strong_release is only in functions with unqualified ownership");
  }
  void checkStrongRetainUnownedInst(StrongRetainUnownedInst *RI) {
    auto unownedType = requireObjectType(UnownedStorageType, RI->getOperand(),
                                         "Operand of strong_retain_unowned");
    require(unownedType->isLoadable(ResilienceExpansion::Maximal),
            "strong_retain_unowned requires unowned type to be loadable");
    require(!F.hasQualifiedOwnership(), "strong_retain_unowned is only in "
                                        "functions with unqualified "
                                        "ownership");
  }
  void checkUnownedRetainInst(UnownedRetainInst *RI) {
    auto unownedType = requireObjectType(UnownedStorageType, RI->getOperand(),
                                          "Operand of unowned_retain");
    require(unownedType->isLoadable(ResilienceExpansion::Maximal),
            "unowned_retain requires unowned type to be loadable");
    require(!F.hasQualifiedOwnership(),
            "unowned_retain is only in functions with unqualified ownership");
  }
  void checkUnownedReleaseInst(UnownedReleaseInst *RI) {
    auto unownedType = requireObjectType(UnownedStorageType, RI->getOperand(),
                                         "Operand of unowned_release");
    require(unownedType->isLoadable(ResilienceExpansion::Maximal),
            "unowned_release requires unowned type to be loadable");
    require(!F.hasQualifiedOwnership(),
            "unowned_release is only in functions with unqualified ownership");
  }
  void checkDeallocStackInst(DeallocStackInst *DI) {
    require(isa<SILUndef>(DI->getOperand()) ||
                isa<AllocStackInst>(DI->getOperand()),
            "Operand of dealloc_stack must be an alloc_stack");
  }
  void checkDeallocRefInst(DeallocRefInst *DI) {
    require(DI->getOperand()->getType().isObject(),
            "Operand of dealloc_ref must be object");
    require(DI->getOperand()->getType().getClassOrBoundGenericClass(),
            "Operand of dealloc_ref must be of class type");
  }
  void checkDeallocPartialRefInst(DeallocPartialRefInst *DPRI) {
    require(DPRI->getInstance()->getType().isObject(),
            "First operand of dealloc_partial_ref must be object");
    auto class1 = DPRI->getInstance()->getType().getClassOrBoundGenericClass();
    require(class1,
            "First operand of dealloc_partial_ref must be of class type");
    require(DPRI->getMetatype()->getType().is<MetatypeType>(),
            "Second operand of dealloc_partial_ref must be a metatype");
    auto class2 = DPRI->getMetatype()->getType().castTo<MetatypeType>()
        ->getInstanceType()->getClassOrBoundGenericClass();
    require(class2,
            "Second operand of dealloc_partial_ref must be a class metatype");
    while (class1 != class2) {
      class1 = class1->getSuperclass()->getClassOrBoundGenericClass();
      require(class1, "First operand not superclass of second instance type");
    }
  }

  void checkAllocBoxInst(AllocBoxInst *AI) {
    auto boxTy = AI->getType().getAs<SILBoxType>();
    require(boxTy, "alloc_box must have a @box type");

    require(AI->getType().isObject(),
            "result of alloc_box must be an object");
    for (unsigned field : indices(AI->getBoxType()->getLayout()->getFields())) {
      verifyOpenedArchetype(AI,
                   AI->getBoxType()->getFieldLoweredType(F.getModule(), field));
    }

    // An alloc_box with a mark_uninitialized user can not have any other users.
    require(none_of(AI->getUses(),
                    [](Operand *Op) -> bool {
                      return isa<MarkUninitializedInst>(Op->getUser());
                    }) ||
                AI->hasOneUse(),
            "An alloc_box with a mark_uninitialized user can not have any "
            "other users.");
  }

  void checkDeallocBoxInst(DeallocBoxInst *DI) {
    auto boxTy = DI->getOperand()->getType().getAs<SILBoxType>();
    require(boxTy, "operand must be a @box type");
    require(DI->getOperand()->getType().isObject(),
            "operand must be an object");
  }

  void checkDestroyAddrInst(DestroyAddrInst *DI) {
    require(DI->getOperand()->getType().isAddress(),
            "Operand of destroy_addr must be address");
  }

  void checkBindMemoryInst(BindMemoryInst *BI) {
    require(BI->getBoundType(), "BI must have a bound type");
    require(BI->getBase()->getType().is<BuiltinRawPointerType>(),
            "bind_memory base be a RawPointer");
    require(BI->getIndex()->getType()
            == SILType::getBuiltinWordType(F.getASTContext()),
            "bind_memory index must be a Word");
  }

  void checkIndexAddrInst(IndexAddrInst *IAI) {
    require(IAI->getType().isAddress(), "index_addr must produce an address");
    require(IAI->getType() == IAI->getBase()->getType(),
            "index_addr must produce an address of the same type as its base");
    require(IAI->getIndex()->getType().is<BuiltinIntegerType>(),
            "index_addr index must be of a builtin integer type");
  }

  void checkTailAddrInst(TailAddrInst *IAI) {
    require(IAI->getType().isAddress(), "tail_addr must produce an address");
    require(IAI->getIndex()->getType().is<BuiltinIntegerType>(),
            "tail_addr index must be of a builtin integer type");
  }

  void checkIndexRawPointerInst(IndexRawPointerInst *IAI) {
    require(IAI->getType().is<BuiltinRawPointerType>(),
            "index_raw_pointer must produce a RawPointer");
    require(IAI->getBase()->getType().is<BuiltinRawPointerType>(),
            "index_raw_pointer base must be a RawPointer");
    require(IAI->getIndex()->getType().is<BuiltinIntegerType>(),
            "index_raw_pointer index must be of a builtin integer type");
  }

  void checkTupleExtractInst(TupleExtractInst *EI) {
    CanTupleType operandTy = requireObjectType(TupleType, EI->getOperand(),
                                               "Operand of tuple_extract");
    require(EI->getType().isObject(),
            "result of tuple_extract must be object");

    require(EI->getFieldNo() < operandTy->getNumElements(),
            "invalid field index for tuple_extract instruction");
    if (EI->getModule().getStage() != SILStage::Lowered) {
      require(EI->getType().getSwiftRValueType() ==
                  operandTy.getElementType(EI->getFieldNo()),
              "type of tuple_extract does not match type of element");
    }
  }

  void checkStructExtractInst(StructExtractInst *EI) {
    SILType operandTy = EI->getOperand()->getType();
    require(operandTy.isObject(),
            "cannot struct_extract from address");
    require(EI->getType().isObject(),
            "result of struct_extract cannot be address");
    StructDecl *sd = operandTy.getStructOrBoundGenericStruct();
    require(sd, "must struct_extract from struct");
    require(!EI->getField()->isStatic(),
            "cannot get address of static property with struct_element_addr");
    require(EI->getField()->hasStorage(),
            "cannot load computed property with struct_extract");

    require(EI->getField()->getDeclContext() == sd,
            "struct_extract field is not a member of the struct");

    if (EI->getModule().getStage() != SILStage::Lowered) {
      SILType loweredFieldTy =
          operandTy.getFieldType(EI->getField(), F.getModule());
      require(loweredFieldTy == EI->getType(),
              "result of struct_extract does not match type of field");
    }
  }

  void checkTupleElementAddrInst(TupleElementAddrInst *EI) {
    SILType operandTy = EI->getOperand()->getType();
    require(operandTy.isAddress(),
            "must derive element_addr from address");
    require(EI->getType().isAddress(),
            "result of tuple_element_addr must be address");
    require(operandTy.is<TupleType>(),
            "must derive tuple_element_addr from tuple");

    ArrayRef<TupleTypeElt> fields = operandTy.castTo<TupleType>()->getElements();
    require(EI->getFieldNo() < fields.size(),
            "invalid field index for element_addr instruction");
    if (EI->getModule().getStage() != SILStage::Lowered) {
      require(EI->getType().getSwiftRValueType() ==
                  CanType(fields[EI->getFieldNo()].getType()),
              "type of tuple_element_addr does not match type of element");
    }
  }

  void checkStructElementAddrInst(StructElementAddrInst *EI) {
    SILType operandTy = EI->getOperand()->getType();
    require(operandTy.isAddress(),
            "must derive struct_element_addr from address");
    StructDecl *sd = operandTy.getStructOrBoundGenericStruct();
    require(sd, "struct_element_addr operand must be struct address");
    require(EI->getType().isAddress(),
            "result of struct_element_addr must be address");
    require(!EI->getField()->isStatic(),
            "cannot get address of static property with struct_element_addr");
    require(EI->getField()->hasStorage(),
            "cannot get address of computed property with struct_element_addr");

    require(EI->getField()->getDeclContext() == sd,
            "struct_element_addr field is not a member of the struct");

    if (EI->getModule().getStage() != SILStage::Lowered) {
      SILType loweredFieldTy =
          operandTy.getFieldType(EI->getField(), F.getModule());
      require(loweredFieldTy == EI->getType(),
              "result of struct_element_addr does not match type of field");
    }
  }

  void checkRefElementAddrInst(RefElementAddrInst *EI) {
    requireReferenceValue(EI->getOperand(), "Operand of ref_element_addr");
    require(EI->getType().isAddress(),
            "result of ref_element_addr must be lvalue");
    require(!EI->getField()->isStatic(),
            "cannot get address of static property with struct_element_addr");
    require(EI->getField()->hasStorage(),
            "cannot get address of computed property with ref_element_addr");
    SILType operandTy = EI->getOperand()->getType();
    ClassDecl *cd = operandTy.getClassOrBoundGenericClass();
    require(cd, "ref_element_addr operand must be a class instance");

    require(EI->getField()->getDeclContext() == cd,
            "ref_element_addr field must be a member of the class");

    if (EI->getModule().getStage() != SILStage::Lowered) {
      SILType loweredFieldTy =
          operandTy.getFieldType(EI->getField(), F.getModule());
      require(loweredFieldTy == EI->getType(),
              "result of ref_element_addr does not match type of field");
    }
    EI->getFieldNo();  // Make sure we can access the field without crashing.
  }

  void checkRefTailAddrInst(RefTailAddrInst *RTAI) {
    requireReferenceValue(RTAI->getOperand(), "Operand of ref_tail_addr");
    require(RTAI->getType().isAddress(),
            "result of ref_tail_addr must be lvalue");
    SILType operandTy = RTAI->getOperand()->getType();
    ClassDecl *cd = operandTy.getClassOrBoundGenericClass();
    require(cd, "ref_tail_addr operand must be a class instance");
  }
  
  SILType getMethodSelfType(CanSILFunctionType ft) {
    return fnConv.getSILType(ft->getParameters().back());
  }

  void checkWitnessMethodInst(WitnessMethodInst *AMI) {
    auto methodType = requireObjectType(SILFunctionType, AMI,
                                        "result of witness_method");

    auto *protocol
      = dyn_cast<ProtocolDecl>(AMI->getMember().getDecl()->getDeclContext());
    require(protocol,
            "witness_method method must be a protocol method");

    require(methodType->getRepresentation()
              == F.getModule().Types.getProtocolWitnessRepresentation(protocol),
            "result of witness_method must have correct representation for protocol");

    require(methodType->isPolymorphic(),
            "result of witness_method must be polymorphic");

    auto genericSig = methodType->getGenericSignature();

    auto selfGenericParam = genericSig->getGenericParams()[0];
    require(selfGenericParam->getDepth() == 0
            && selfGenericParam->getIndex() == 0,
            "method should be polymorphic on Self parameter at depth 0 index 0");
    Optional<Requirement> selfRequirement;
    for (auto req : genericSig->getRequirements()) {
      if (req.getKind() != RequirementKind::SameType) {
        selfRequirement = req;
        break;
      }
    }

    require(selfRequirement &&
            selfRequirement->getKind() == RequirementKind::Conformance,
            "first non-same-typerequirement should be conformance requirement");
    auto conformsTo = genericSig->getConformsTo(selfGenericParam);
    require(conformsTo.size() == 1,
            "requirement Self parameter must conform to exactly one protocol");
    require(conformsTo[0] == protocol,
            "requirement Self parameter should be constrained by protocol");

    auto lookupType = AMI->getLookupType();
    if (getOpenedArchetypeOf(lookupType)) {
      require(AMI->getTypeDependentOperands().size() == 1,
              "Must have a type dependent operand for the opened archetype");
      verifyOpenedArchetype(AMI, lookupType);
    } else {
      require(AMI->getTypeDependentOperands().empty(),
              "Should not have an operand for the opened existential");
    }
    if (isa<ArchetypeType>(lookupType) || lookupType->isAnyExistentialType()) {
      require(AMI->getConformance().isAbstract(),
              "archetype or existential lookup should have abstract conformance");
    } else {
      require(AMI->getConformance().isConcrete(),
              "concrete type lookup requires concrete conformance");
      auto conformance = AMI->getConformance().getConcrete();
      require(conformance->getType()->isEqual(AMI->getLookupType()),
              "concrete type lookup requires conformance that matches type");
      require(AMI->getModule().lookUpWitnessTable(conformance, false),
              "Could not find witness table for conformance");
    }
  }

  // Get the expected type of a dynamic method reference.
  SILType getDynamicMethodType(SILType selfType, SILDeclRef method) {
    auto &C = F.getASTContext();

    // The type of the dynamic method must match the usual type of the method,
    // but with the more opaque Self type.
    auto constantInfo = F.getModule().Types.getConstantInfo(method);
    auto methodTy = constantInfo.SILFnType;

    assert(!methodTy->isCoroutine());

    // Map interface types to archetypes.
    if (auto *env = constantInfo.GenericEnv) {
      auto subs = env->getForwardingSubstitutions();
      methodTy = methodTy->substGenericArgs(F.getModule(), subs);
    }
    assert(!methodTy->isPolymorphic());

    // Replace Self parameter with type of 'self' at the call site.
    auto params = methodTy->getParameters();
    SmallVector<SILParameterInfo, 4>
      dynParams(params.begin(), params.end() - 1);
    dynParams.push_back(SILParameterInfo(selfType.getSwiftRValueType(),
                                         params.back().getConvention()));

    auto results = methodTy->getResults();
    SmallVector<SILResultInfo, 4> dynResults(results.begin(), results.end());    

    // If the method returns Self, substitute AnyObject for the result type.
    if (auto fnDecl = dyn_cast<FuncDecl>(method.getDecl())) {
      if (fnDecl->hasDynamicSelf()) {
        auto anyObjectTy = C.getAnyObjectType();
        for (auto &dynResult : dynResults) {
          auto newResultTy
            = dynResult.getType()->replaceCovariantResultType(anyObjectTy, 0);
          dynResult = SILResultInfo(newResultTy->getCanonicalType(),
                                    dynResult.getConvention());
        }
      }
    }

    auto fnTy = SILFunctionType::get(nullptr,
                                     methodTy->getExtInfo(),
                                     methodTy->getCoroutineKind(),
                                     methodTy->getCalleeConvention(),
                                     dynParams,
                                     methodTy->getYields(),
                                     dynResults,
                                     methodTy->getOptionalErrorResult(),
                                     F.getASTContext());
    return SILType::getPrimitiveObjectType(fnTy);
  }
  
  void checkClassMethodInst(ClassMethodInst *CMI) {
    auto member = CMI->getMember();
    auto overrideTy = TC.getConstantOverrideType(member);
    if (CMI->getModule().getStage() != SILStage::Lowered) {
      requireSameType(
          CMI->getType(), SILType::getPrimitiveObjectType(overrideTy),
          "result type of class_method must match abstracted type of method");
    }
    auto methodType = requireObjectType(SILFunctionType, CMI,
                                        "result of class_method");
    require(!methodType->getExtInfo().hasContext(),
            "result method must be of a context-free function type");
    SILType operandType = CMI->getOperand()->getType();
    require(operandType.isClassOrClassMetatype(),
            "operand must be of a class type");
    require(getMethodSelfType(methodType).isClassOrClassMetatype(),
            "result must be a method of a class");
    
    require(!member.isForeign,
            "foreign method cannot be dispatched natively");
    require(!isa<ExtensionDecl>(member.getDecl()->getDeclContext()),
            "extension method cannot be dispatched natively");
  }

  void checkSuperMethodInst(SuperMethodInst *CMI) {
    auto member = CMI->getMember();
    auto overrideTy = TC.getConstantOverrideType(member);
    if (CMI->getModule().getStage() != SILStage::Lowered) {
      requireSameType(
          CMI->getType(), SILType::getPrimitiveObjectType(overrideTy),
          "result type of super_method must match abstracted type of method");
    }
    auto methodType = requireObjectType(SILFunctionType, CMI,
                                        "result of super_method");
    require(!methodType->getExtInfo().hasContext(),
            "result method must be of a context-free function type");
    SILType operandType = CMI->getOperand()->getType();
    require(operandType.isClassOrClassMetatype(),
            "operand must be of a class type");
    require(getMethodSelfType(methodType).isClassOrClassMetatype(),
            "result must be a method of a class");

    require(!member.isForeign,
            "foreign method cannot be dispatched natively");
    require(!isa<ExtensionDecl>(member.getDecl()->getDeclContext()),
            "extension method cannot be dispatched natively");

    auto decl = CMI->getMember().getDecl();
    auto methodClass = decl->getDeclContext()->getDeclaredInterfaceType();

    require(methodClass->getClassOrBoundGenericClass(),
            "super_method must look up a class method");
  }

  void checkObjCMethodInst(ObjCMethodInst *OMI) {
    auto member = OMI->getMember();
    require(member.isForeign,
            "native method cannot be dispatched via objc");

    auto methodType = requireObjectType(SILFunctionType, OMI,
                                        "result of objc_method");
    require(!methodType->getExtInfo().hasContext(),
            "result method must be of a context-free function type");
    require(methodType->getRepresentation()
            == SILFunctionTypeRepresentation::ObjCMethod,
            "wrong function type representation");

    auto operandType = OMI->getOperand()->getType();
    auto operandInstanceType = operandType.getSwiftRValueType();
    if (auto metatypeType = dyn_cast<MetatypeType>(operandInstanceType))
      operandInstanceType = metatypeType.getInstanceType();

    if (operandInstanceType.getClassOrBoundGenericClass()) {
      auto overrideTy = TC.getConstantOverrideType(member);
      requireSameType(
          OMI->getType(), SILType::getPrimitiveObjectType(overrideTy),
          "result type of objc_method must match abstracted type of method");
    } else {
      require(isa<ArchetypeType>(operandInstanceType) ||
              operandInstanceType->isObjCExistentialType(),
              "operand type must be an archetype or self-conforming existential");
      verifyOpenedArchetype(OMI, OMI->getType().getSwiftRValueType());
    }

    // TODO: We should enforce that ObjC methods are dispatched on ObjC
    // metatypes, but IRGen appears not to care right now.
#if 0
    if (auto metaTy = operandType.getAs<AnyMetatypeType>()) {
      bool objcMetatype
        = metaTy->getRepresentation() == MetatypeRepresentation::ObjC;
      require(objcMetatype,
              "objc class methods must be invoked on objc metatypes");
    }
#endif
  }

  void checkObjCSuperMethodInst(ObjCSuperMethodInst *OMI) {
    auto member = OMI->getMember();
    auto overrideTy = TC.getConstantOverrideType(member);
    if (OMI->getModule().getStage() != SILStage::Lowered) {
      requireSameType(
          OMI->getType(), SILType::getPrimitiveObjectType(overrideTy),
          "result type of super_method must match abstracted type of method");
    }
    auto methodType = requireObjectType(SILFunctionType, OMI,
                                        "result of objc_super_method");
    require(!methodType->getExtInfo().hasContext(),
            "result method must be of a context-free function type");
    SILType operandType = OMI->getOperand()->getType();
    require(operandType.isClassOrClassMetatype(),
            "operand must be of a class type");
    require(getMethodSelfType(methodType).isClassOrClassMetatype(),
            "result must be a method of a class");

    require(member.isForeign,
            "native method cannot be dispatched via objc");

    auto decl = member.getDecl();
    auto methodClass = decl->getDeclContext()->getDeclaredInterfaceType();

    require(methodClass->getClassOrBoundGenericClass(),
            "objc_super_method must look up a class method");
  }

  void checkOpenExistentialAddrInst(OpenExistentialAddrInst *OEI) {
    SILType operandType = OEI->getOperand()->getType();
    require(operandType.isAddress(),
            "open_existential_addr must be applied to address");
    require(operandType.canUseExistentialRepresentation(F.getModule(),
                                        ExistentialRepresentation::Opaque),
           "open_existential_addr must be applied to opaque existential");

    require(OEI->getType().isAddress(),
            "open_existential_addr result must be an address");

    auto archetype = getOpenedArchetypeOf(OEI->getType().getSwiftRValueType());
    require(archetype,
        "open_existential_addr result must be an opened existential archetype");
    require(OpenedArchetypes.getOpenedArchetypeDef(archetype) == OEI,
            "Archetype opened by open_existential_addr should be registered in "
            "SILFunction");

    // Check all the uses. Consuming or mutating uses must have mutable access
    // to the opened value.
    auto allowedAccessKind = OEI->getAccessKind();
    if (allowedAccessKind == OpenedExistentialAccess::Mutable)
      return;

    auto isConsumingOrMutatingApplyUse = [](Operand *use) -> bool {
      ApplySite apply(use->getUser());
      assert(apply && "Not an apply instruction kind");
      auto conv = apply.getArgumentConvention(use->getOperandNumber() - 1);
      switch (conv) {
      case SILArgumentConvention::Indirect_In_Guaranteed:
        return false;

      case SILArgumentConvention::Indirect_Out:
      case SILArgumentConvention::Indirect_In:
      case SILArgumentConvention::Indirect_In_Constant:
      case SILArgumentConvention::Indirect_Inout:
      case SILArgumentConvention::Indirect_InoutAliasable:
        return true;

      case SILArgumentConvention::Direct_Unowned:
      case SILArgumentConvention::Direct_Guaranteed:
      case SILArgumentConvention::Direct_Owned:
      case SILArgumentConvention::Direct_Deallocating:
        assert(conv.isIndirectConvention() && "Expect an indirect convention");
        return true; // return something "conservative".
      }
      llvm_unreachable("covered switch isn't covered?!");
    };

    // A "copy_addr %src [take] to *" is consuming on "%src".
    // A "copy_addr * to * %dst" is mutating on "%dst".
    auto isConsumingOrMutatingCopyAddrUse = [](Operand *use) -> bool {
      auto *copyAddr = cast<CopyAddrInst>(use->getUser());
      if (copyAddr->getDest() == use->get())
        return true;
      if (copyAddr->getSrc() == use->get() && copyAddr->isTakeOfSrc() == IsTake)
        return true;
      return false;
    };

    auto isMutatingOrConsuming = [=](OpenExistentialAddrInst *OEI) -> bool {
      for (auto *use : OEI->getUses()) {
        auto *inst = use->getUser();
        if (inst->isTypeDependentOperand(*use))
          continue;
        switch (inst->getKind()) {
        case SILInstructionKind::MarkDependenceInst:
          break;
        case SILInstructionKind::ApplyInst:
        case SILInstructionKind::TryApplyInst:
        case SILInstructionKind::PartialApplyInst:
          // Non-Mutating set pattern that allows a inout (that can't really
          // write back.
          if (auto *AI = dyn_cast<ApplyInst>(inst)) {
            if (isa<PointerToThinFunctionInst>(AI->getCallee())) {
              break;
            }
          }
          if (isConsumingOrMutatingApplyUse(use))
            return true;
          else
            break;
        case SILInstructionKind::CopyAddrInst:
          if (isConsumingOrMutatingCopyAddrUse(use))
            return true;
          else
            break;
        case SILInstructionKind::DestroyAddrInst:
          return true;
        case SILInstructionKind::UncheckedAddrCastInst: {
          // Don't be too conservative here, we have a new case:
          // sil-combine producing a new code pattern for devirtualizer
          // open_existential_addr immutable_access -> witness_method
          // witness_method gets transformed into unchecked_addr_cast
          // we are "OK" If one of the new users is an non-consuming apply
          // we are also "OK" if we have a single non-consuming user
          auto isCastToNonConsuming = [=](UncheckedAddrCastInst *I) -> bool {
            for (auto *use : I->getUses()) {
              auto *inst = use->getUser();
              switch (inst->getKind()) {
              case SILInstructionKind::ApplyInst:
              case SILInstructionKind::TryApplyInst:
              case SILInstructionKind::PartialApplyInst:
                if (!isConsumingOrMutatingApplyUse(use))
                  return true;
                break;
              case SILInstructionKind::StructElementAddrInst:
              case SILInstructionKind::LoadInst:
              case SILInstructionKind::DebugValueAddrInst:
                if (I->hasOneUse())
                  return true;
                break;
              default:
                break;
              }
            }
            return false;
          };
          if (isCastToNonConsuming(dyn_cast<UncheckedAddrCastInst>(inst))) {
            break;
          }
          return true;
        }
        case SILInstructionKind::CheckedCastAddrBranchInst:
          if (cast<CheckedCastAddrBranchInst>(inst)->getConsumptionKind() !=
              CastConsumptionKind::CopyOnSuccess)
            return true;
          break;
        case SILInstructionKind::LoadInst:
          // A 'non-taking' value load is harmless.
          return cast<LoadInst>(inst)->getOwnershipQualifier() ==
                 LoadOwnershipQualifier::Take;
          break;
        case SILInstructionKind::DebugValueAddrInst:
          // Harmless use.
          break;
        default:
          llvm_unreachable("Unhandled unexpected instruction");
          break;
        }
      }
      return false;
    };
    require(allowedAccessKind == OpenedExistentialAccess::Mutable
            || !isMutatingOrConsuming(OEI),
            "open_existential_addr uses that consumes or mutates but is not "
            "opened for mutation");
  }

  void checkOpenExistentialRefInst(OpenExistentialRefInst *OEI) {
    SILType operandType = OEI->getOperand()->getType();
    require(operandType.isObject(),
            "open_existential_ref operand must not be address");

    require(operandType.canUseExistentialRepresentation(F.getModule(),
                                              ExistentialRepresentation::Class),
            "open_existential_ref operand must be class existential");

    CanType resultInstanceTy = OEI->getType().getSwiftRValueType();

    require(OEI->getType().isObject(),
            "open_existential_ref result must be an address");

    auto archetype = getOpenedArchetypeOf(resultInstanceTy);
    require(archetype,
        "open_existential_ref result must be an opened existential archetype");
    require(OpenedArchetypes.getOpenedArchetypeDef(archetype) == OEI,
            "Archetype opened by open_existential_ref should be registered in "
            "SILFunction");
  }

  void checkOpenExistentialBoxInst(OpenExistentialBoxInst *OEI) {
    SILType operandType = OEI->getOperand()->getType();
    require(operandType.isObject(),
            "open_existential_box operand must not be address");

    require(operandType.canUseExistentialRepresentation(F.getModule(),
                                              ExistentialRepresentation::Boxed),
            "open_existential_box operand must be boxed existential");

    CanType resultInstanceTy = OEI->getType().getSwiftRValueType();

    require(OEI->getType().isAddress(),
            "open_existential_box result must be an address");

    auto archetype = getOpenedArchetypeOf(resultInstanceTy);
    require(archetype,
        "open_existential_box result must be an opened existential archetype");
    require(OpenedArchetypes.getOpenedArchetypeDef(archetype) == OEI,
            "Archetype opened by open_existential_box should be registered in "
            "SILFunction");
  }

  void checkOpenExistentialBoxValueInst(OpenExistentialBoxValueInst *OEI) {
    SILType operandType = OEI->getOperand()->getType();
    require(operandType.isObject(),
            "open_existential_box operand must not be address");

    require(operandType.canUseExistentialRepresentation(F.getModule(),
                                              ExistentialRepresentation::Boxed),
            "open_existential_box operand must be boxed existential");

    CanType resultInstanceTy = OEI->getType().getSwiftRValueType();

    require(!OEI->getType().isAddress(),
            "open_existential_box_value result must not be an address");

    auto archetype = getOpenedArchetypeOf(resultInstanceTy);
    require(archetype,
        "open_existential_box_value result not an opened existential archetype");
    require(OpenedArchetypes.getOpenedArchetypeDef(archetype) == OEI,
            "Archetype opened by open_existential_box_value should be "
            "registered in SILFunction");
  }

  void checkOpenExistentialMetatypeInst(OpenExistentialMetatypeInst *I) {
    SILType operandType = I->getOperand()->getType();
    require(operandType.isObject(),
            "open_existential_metatype operand must not be address");
    require(operandType.is<ExistentialMetatypeType>(),
            "open_existential_metatype operand must be existential metatype");
    require(operandType.castTo<ExistentialMetatypeType>()->hasRepresentation(),
            "open_existential_metatype operand must have a representation");

    SILType resultType = I->getType();
    require(resultType.isObject(),
            "open_existential_metatype result must not be address");
    require(resultType.is<MetatypeType>(),
            "open_existential_metatype result must be metatype");
    require(resultType.castTo<MetatypeType>()->hasRepresentation(),
            "open_existential_metatype result must have a representation");
    require(operandType.castTo<ExistentialMetatypeType>()->getRepresentation()
              == resultType.castTo<MetatypeType>()->getRepresentation(),
            "open_existential_metatype result must match representation of "
            "operand");

    CanType operandInstTy =
      operandType.castTo<ExistentialMetatypeType>().getInstanceType();
    CanType resultInstTy = 
      resultType.castTo<MetatypeType>().getInstanceType();

    while (auto operandMetatype =
             dyn_cast<ExistentialMetatypeType>(operandInstTy)) {
      require(isa<MetatypeType>(resultInstTy),
              "metatype depth mismatch in open_existential_metatype result");
      operandInstTy = operandMetatype.getInstanceType();
      resultInstTy = cast<MetatypeType>(resultInstTy).getInstanceType();
    }

    require(operandInstTy.isExistentialType(),
            "ill-formed existential metatype in open_existential_metatype "
            "operand");
    auto archetype = getOpenedArchetypeOf(resultInstTy);
    require(archetype, "open_existential_metatype result must be an opened "
                       "existential metatype");
    require(
        OpenedArchetypes.getOpenedArchetypeDef(archetype) == I,
        "Archetype opened by open_existential_metatype should be registered in "
        "SILFunction");
  }

  void checkOpenExistentialValueInst(OpenExistentialValueInst *OEI) {
    SILType operandType = OEI->getOperand()->getType();
    require(!operandType.isAddress(),
            "open_existential_value must not be applied to address");
    require(operandType.canUseExistentialRepresentation(
                F.getModule(), ExistentialRepresentation::Opaque),
            "open_existential_value must be applied to opaque existential");

    require(!OEI->getType().isAddress(),
            "open_existential_value result must not be an address");

    auto archetype = getOpenedArchetypeOf(OEI->getType().getSwiftRValueType());
    require(archetype, "open_existential_value result must be an opened "
                       "existential archetype");
    require(OpenedArchetypes.getOpenedArchetypeDef(archetype) == OEI,
            "Archetype opened by open_existential should be registered in "
            "SILFunction");
  }

  void checkAllocExistentialBoxInst(AllocExistentialBoxInst *AEBI) {
    SILType exType = AEBI->getExistentialType();
    require(exType.isObject(),
            "alloc_existential_box #0 result should be a value");
    require(exType.canUseExistentialRepresentation(F.getModule(),
                                             ExistentialRepresentation::Boxed,
                                             AEBI->getFormalConcreteType()),
            "alloc_existential_box must be used with a boxed existential "
            "type");
    
    checkExistentialProtocolConformances(exType,
                                         AEBI->getFormalConcreteType(),
                                         AEBI->getConformances());
    verifyOpenedArchetype(AEBI, AEBI->getFormalConcreteType());
  }

  void checkInitExistentialAddrInst(InitExistentialAddrInst *AEI) {
    SILType exType = AEI->getOperand()->getType();
    require(exType.isAddress(),
            "init_existential_addr must be applied to an address");
    require(exType.canUseExistentialRepresentation(F.getModule(),
                                       ExistentialRepresentation::Opaque,
                                       AEI->getFormalConcreteType()),
            "init_existential_addr must be used with an opaque "
            "existential type");
    
    // The lowered type must be the properly-abstracted form of the AST type.
    auto archetype = ArchetypeType::getOpened(exType.getSwiftRValueType());
    
    auto loweredTy = F.getModule().Types.getLoweredType(
                                Lowering::AbstractionPattern(archetype),
                                AEI->getFormalConcreteType())
                      .getAddressType();
    
    requireSameType(loweredTy, AEI->getLoweredConcreteType(),
                    "init_existential_addr result type must be the lowered "
                    "concrete type at the right abstraction level");

    require(isLoweringOf(AEI->getLoweredConcreteType(),
                         AEI->getFormalConcreteType()),
            "init_existential_addr payload must be a lowering of the formal "
            "concrete type");
    
    checkExistentialProtocolConformances(exType,
                                         AEI->getFormalConcreteType(),
                                         AEI->getConformances());
    verifyOpenedArchetype(AEI, AEI->getFormalConcreteType());
  }

  void checkInitExistentialValueInst(InitExistentialValueInst *IEI) {
    SILType concreteType = IEI->getOperand()->getType();
    require(!concreteType.isAddress(),
            "init_existential_value must not be used on addresses");
    require(!IEI->getType().isAddress(),
            "init_existential_value result must not be an address");
    // The operand must be at the right abstraction level for the existential.
    SILType exType = IEI->getType();
    auto archetype = ArchetypeType::getOpened(exType.getSwiftRValueType());
    auto loweredTy = F.getModule().Types.getLoweredType(
        Lowering::AbstractionPattern(archetype), IEI->getFormalConcreteType());
    requireSameType(
        concreteType, loweredTy,
        "init_existential_value operand must be lowered to the right "
        "abstraction level for the existential");

    require(isLoweringOf(IEI->getOperand()->getType(),
                         IEI->getFormalConcreteType()),
            "init_existential_value operand must be a lowering of the formal "
            "concrete type");

    checkExistentialProtocolConformances(exType,
                                         IEI->getFormalConcreteType(),
                                         IEI->getConformances());
    verifyOpenedArchetype(IEI, IEI->getFormalConcreteType());
  }

  void checkInitExistentialRefInst(InitExistentialRefInst *IEI) {
    SILType concreteType = IEI->getOperand()->getType();
    require(concreteType.getSwiftRValueType()->isBridgeableObjectType(),
            "init_existential_ref operand must be a class instance");
    require(IEI->getType().canUseExistentialRepresentation(F.getModule(),
                                     ExistentialRepresentation::Class,
                                     IEI->getFormalConcreteType()),
            "init_existential_ref must be used with a class existential type");
    require(IEI->getType().isObject(),
            "init_existential_ref result must not be an address");
    
    // The operand must be at the right abstraction level for the existential.
    SILType exType = IEI->getType();
    auto archetype = ArchetypeType::getOpened(exType.getSwiftRValueType());
    auto loweredTy = F.getModule().Types.getLoweredType(
                                       Lowering::AbstractionPattern(archetype),
                                       IEI->getFormalConcreteType());
    requireSameType(concreteType, loweredTy,
                    "init_existential_ref operand must be lowered to the right "
                    "abstraction level for the existential");
    
    require(isLoweringOf(IEI->getOperand()->getType(),
                         IEI->getFormalConcreteType()),
            "init_existential_ref operand must be a lowering of the formal "
            "concrete type");
    
    checkExistentialProtocolConformances(exType,
                                         IEI->getFormalConcreteType(),
                                         IEI->getConformances());
    verifyOpenedArchetype(IEI, IEI->getFormalConcreteType());
  }

  void checkDeinitExistentialAddrInst(DeinitExistentialAddrInst *DEI) {
    SILType exType = DEI->getOperand()->getType();
    require(exType.isAddress(),
            "deinit_existential_addr must be applied to an address");
    require(exType.canUseExistentialRepresentation(
                F.getModule(), ExistentialRepresentation::Opaque),
            "deinit_existential_addr must be applied to an opaque existential");
  }

  void checkDeinitExistentialValueInst(DeinitExistentialValueInst *DEI) {
    SILType exType = DEI->getOperand()->getType();
    require(!exType.isAddress(),
            "deinit_existential_value must not be applied to an address");
    require(
        exType.canUseExistentialRepresentation(
            F.getModule(), ExistentialRepresentation::Opaque),
        "deinit_existential_value must be applied to an opaque existential");
  }
  
  void checkDeallocExistentialBoxInst(DeallocExistentialBoxInst *DEBI) {
    SILType exType = DEBI->getOperand()->getType();
    require(exType.isObject(),
            "dealloc_existential_box must be applied to a value");
    require(exType.canUseExistentialRepresentation(F.getModule(),
                                       ExistentialRepresentation::Boxed),
            "dealloc_existential_box must be applied to a boxed "
            "existential");
  }

  void checkInitExistentialMetatypeInst(InitExistentialMetatypeInst *I) {
    SILType operandType = I->getOperand()->getType();
    require(operandType.isObject(),
            "init_existential_metatype operand must not be an address");
    require(operandType.is<MetatypeType>(),
            "init_existential_metatype operand must be a metatype");
    require(operandType.castTo<MetatypeType>()->hasRepresentation(),
            "init_existential_metatype operand must have a representation");

    SILType resultType = I->getType();
    require(resultType.is<ExistentialMetatypeType>(),
            "init_existential_metatype result must be an existential metatype");
    auto MetaTy = resultType.castTo<ExistentialMetatypeType>();
    require(resultType.isObject(),
            "init_existential_metatype result must not be an address");
    require(MetaTy->hasRepresentation(),
            "init_existential_metatype result must have a representation");
    require(MetaTy->getRepresentation()
              == operandType.castTo<MetatypeType>()->getRepresentation(),
            "init_existential_metatype result must match representation of "
            "operand");

    while (resultType.is<ExistentialMetatypeType>()) {
      resultType = resultType.getMetatypeInstanceType(F.getModule());
      operandType = operandType.getMetatypeInstanceType(F.getModule());
    }

    checkExistentialProtocolConformances(resultType,
                                         operandType.getSwiftRValueType(),
                                         I->getConformances());
    verifyOpenedArchetype(I, MetaTy.getInstanceType());
  }

  void checkExistentialProtocolConformances(SILType resultType,
                                            CanType concreteType,
                                ArrayRef<ProtocolConformanceRef> conformances) {
    auto layout = resultType.getSwiftRValueType().getExistentialLayout();
    auto protocols = layout.getProtocols();

    require(conformances.size() == protocols.size(),
            "init_existential instruction must have the "
            "right number of conformances");

    if (layout.requiresClass()) {
      require(concreteType->mayHaveSuperclass() ||
              (concreteType.isExistentialType() &&
               concreteType.getExistentialLayout().requiresClass()),
              "init_existential of class existential with non-class type");
    }

    if (layout.superclass) {
      require(layout.superclass->isExactSuperclassOf(concreteType),
              "init_existential of subclass existential with wrong type");
    }

    for (auto i : indices(conformances)) {
      require(conformances[i].getRequirement() == protocols[i]->getDecl(),
              "init_existential instruction must have conformances in "
              "proper order");

      if (conformances[i].isConcrete()) {
        auto conformance = conformances[i].getConcrete();
        require(F.getModule().lookUpWitnessTable(conformance, false),
                "Could not find witness table for conformance.");

      }
    }
  }

  void verifyCheckedCast(bool isExact, SILType fromTy, SILType toTy,
                         bool isOpaque = false) {
    // Verify common invariants.
    require(fromTy.isObject() && toTy.isObject(),
            "value checked cast src and dest must be objects");

    auto fromCanTy = fromTy.getSwiftRValueType();
    auto toCanTy = toTy.getSwiftRValueType();

    require(isOpaque || canUseScalarCheckedCastInstructions(F.getModule(),
                                                            fromCanTy, toCanTy),
            "invalid value checked cast src or dest types");

    // Peel off metatypes. If two types are checked-cast-able, so are their
    // metatypes.
    unsigned MetatyLevel = 0;
    while (isa<AnyMetatypeType>(fromCanTy) && isa<AnyMetatypeType>(toCanTy)) {
      auto fromMetaty = cast<AnyMetatypeType>(fromCanTy);
      auto toMetaty = cast<AnyMetatypeType>(toCanTy);
      
      // Check representations only for the top-level metatypes as only
      // those are SIL-lowered.
      if (!MetatyLevel) {
        // The representations must match.
        require(fromMetaty->getRepresentation() == toMetaty->getRepresentation(),
                "metatype checked cast cannot change metatype representation");

        // We can't handle the 'thin' case yet, but it shouldn't really even be
        // interesting.
        require(fromMetaty->getRepresentation() != MetatypeRepresentation::Thin,
                "metatype checked cast cannot check thin metatypes");
      }

      fromCanTy = fromMetaty.getInstanceType();
      toCanTy = toMetaty.getInstanceType();
      MetatyLevel++;
    }

    if (isExact) {
      require(fromCanTy.getClassOrBoundGenericClass(),
              "downcast operand must be a class type");
      require(toCanTy.getClassOrBoundGenericClass(),
              "downcast must convert to a class type");
      require(SILType::getPrimitiveObjectType(fromCanTy).
              isBindableToSuperclassOf(SILType::getPrimitiveObjectType(toCanTy)),
              "downcast must convert to a subclass");
    }
  }

  void checkUnconditionalCheckedCastInst(UnconditionalCheckedCastInst *CI) {
    verifyCheckedCast(/*exact*/ false,
                      CI->getOperand()->getType(),
                      CI->getType());
    verifyOpenedArchetype(CI, CI->getType().getSwiftRValueType());
  }

  void checkUnconditionalCheckedCastValueInst(
      UnconditionalCheckedCastValueInst *CI) {
    verifyCheckedCast(/*exact*/ false, CI->getOperand()->getType(),
                      CI->getType(), true);
    verifyOpenedArchetype(CI, CI->getType().getSwiftRValueType());
  }

  /// Verify if a given type is or contains an opened archetype or dynamic self.
  /// If this is the case, verify that the provided instruction has a type
  /// dependent operand for it.
  void verifyOpenedArchetype(SILInstruction *I, CanType Ty) {
    if (!Ty)
      return;
    // Check the type and all of its contained types.
    Ty.visit([&](CanType t) {
      SILValue Def;
      if (t->isOpenedExistential()) {
        auto archetypeTy = cast<ArchetypeType>(t);
        Def = OpenedArchetypes.getOpenedArchetypeDef(archetypeTy);
        require(Def, "Opened archetype should be registered in SILFunction");
      } else if (t->hasDynamicSelfType()) {
        require(I->getFunction()->hasSelfParam() ||
                I->getFunction()->hasSelfMetadataParam(),
              "Function containing dynamic self type must have self parameter");
        if (I->getFunction()->hasSelfMetadataParam())
          Def = I->getFunction()->getArguments().back();
        else
          Def = I->getFunction()->getSelfArgument();
      } else {
        return;
      }
      for (auto &TypeDefOp : I->getTypeDependentOperands()) {
        if (TypeDefOp.get() == Def)
          return;
      }
      require(false, "Instruction should contain a type dependent operand for "
                     "every used open archetype or dynamic self");
    });
  }

  void checkCheckedCastBranchInst(CheckedCastBranchInst *CBI) {
    verifyCheckedCast(CBI->isExact(),
                      CBI->getOperand()->getType(),
                      CBI->getCastType());
    verifyOpenedArchetype(CBI, CBI->getCastType().getSwiftRValueType());

    require(CBI->getSuccessBB()->args_size() == 1,
            "success dest of checked_cast_br must take one argument");
    require(CBI->getSuccessBB()->args_begin()[0]->getType() ==
                CBI->getCastType(),
            "success dest block argument of checked_cast_br must match type of "
            "cast");
    require(F.hasQualifiedOwnership() || CBI->getFailureBB()->args_empty(),
            "failure dest of checked_cast_br in unqualified ownership sil must "
            "take no arguments");
#if 0
    require(F.hasUnqualifiedOwnership() ||
                CBI->getFailureBB()->args_size() == 1,
            "failure dest of checked_cast_br must take one argument in "
            "ownership qualified sil");
    require(F.hasUnqualifiedOwnership() ||
                CBI->getFailureBB()->args_begin()[0]->getType() ==
                    CBI->getOperand()->getType(),
            "failure dest block argument must match type of original type in "
            "ownership qualified sil");
#endif
  }

  void checkCheckedCastValueBranchInst(CheckedCastValueBranchInst *CBI) {
    verifyCheckedCast(false, CBI->getOperand()->getType(), CBI->getCastType(),
                      true);
    verifyOpenedArchetype(CBI, CBI->getCastType().getSwiftRValueType());

    require(CBI->getSuccessBB()->args_size() == 1,
            "success dest of checked_cast_value_br must take one argument");
    require(CBI->getSuccessBB()->args_begin()[0]->getType() ==
                CBI->getCastType(),
            "success dest block argument of checked_cast_value_br must match "
            "type of cast");
    require(F.hasQualifiedOwnership() || CBI->getFailureBB()->args_empty(),
            "failure dest of checked_cast_value_br in unqualified ownership "
            "sil must take no arguments");
  }

  void checkCheckedCastAddrBranchInst(CheckedCastAddrBranchInst *CCABI) {
    require(CCABI->getSrc()->getType().isAddress(),
            "checked_cast_addr_br src must be an address");
    require(CCABI->getDest()->getType().isAddress(),
            "checked_cast_addr_br dest must be an address");

    require(
        CCABI->getSuccessBB()->args_size() == 0,
        "success dest block of checked_cast_addr_br must not take an argument");
    require(
        CCABI->getFailureBB()->args_size() == 0,
        "failure dest block of checked_cast_addr_br must not take an argument");
  }

  void checkThinToThickFunctionInst(ThinToThickFunctionInst *TTFI) {
    auto opFTy = requireObjectType(SILFunctionType, TTFI->getOperand(),
                                   "thin_to_thick_function operand");
    auto resFTy = requireObjectType(SILFunctionType, TTFI,
                                    "thin_to_thick_function result");
    require(opFTy->isPolymorphic() == resFTy->isPolymorphic(),
            "thin_to_thick_function operand and result type must differ only "
            " in thinness");
    requireSameFunctionComponents(opFTy, resFTy,
                                  "thin_to_thick_function operand and result");

    require(opFTy->getRepresentation() == SILFunctionType::Representation::Thin,
            "operand of thin_to_thick_function must be thin");
    require(resFTy->getRepresentation() == SILFunctionType::Representation::Thick,
            "result of thin_to_thick_function must be thick");

    auto adjustedOperandExtInfo = opFTy->getExtInfo().withRepresentation(
                                           SILFunctionType::Representation::Thick);
    require(adjustedOperandExtInfo == resFTy->getExtInfo(),
            "operand and result of thin_to_think_function must agree in particulars");
  }

  void checkThickToObjCMetatypeInst(ThickToObjCMetatypeInst *TTOCI) {
    auto opTy = requireObjectType(AnyMetatypeType, TTOCI->getOperand(),
                                  "thick_to_objc_metatype operand");
    auto resTy = requireObjectType(AnyMetatypeType, TTOCI,
                                   "thick_to_objc_metatype result");

    require(TTOCI->getOperand()->getType().is<MetatypeType>() ==
            TTOCI->getType().is<MetatypeType>(),
            "thick_to_objc_metatype cannot change metatype kinds");
    require(opTy->getRepresentation() == MetatypeRepresentation::Thick,
            "operand of thick_to_objc_metatype must be thick");
    require(resTy->getRepresentation() == MetatypeRepresentation::ObjC,
            "operand of thick_to_objc_metatype must be ObjC");

    require(opTy->getInstanceType()->isEqual(resTy->getInstanceType()),
            "thick_to_objc_metatype instance types do not match");
  }

  void checkObjCToThickMetatypeInst(ObjCToThickMetatypeInst *OCTTI) {
    auto opTy = requireObjectType(AnyMetatypeType, OCTTI->getOperand(),
                                  "objc_to_thick_metatype operand");
    auto resTy = requireObjectType(AnyMetatypeType, OCTTI,
                                   "objc_to_thick_metatype result");

    require(OCTTI->getOperand()->getType().is<MetatypeType>() ==
            OCTTI->getType().is<MetatypeType>(),
            "objc_to_thick_metatype cannot change metatype kinds");
    require(opTy->getRepresentation() == MetatypeRepresentation::ObjC,
            "operand of objc_to_thick_metatype must be ObjC");
    require(resTy->getRepresentation() == MetatypeRepresentation::Thick,
            "operand of objc_to_thick_metatype must be thick");

    require(opTy->getInstanceType()->isEqual(resTy->getInstanceType()),
            "objc_to_thick_metatype instance types do not match");
  }

  void checkRefToUnownedInst(RefToUnownedInst *I) {
    requireReferenceStorageCapableValue(I->getOperand(),
                                        "Operand of ref_to_unowned");
    auto operandType = I->getOperand()->getType().getSwiftRValueType();
    auto resultType = requireObjectType(UnownedStorageType, I,
                                        "Result of ref_to_unowned");
    require(resultType->isLoadable(ResilienceExpansion::Maximal),
            "ref_to_unowned requires unowned type to be loadable");
    require(resultType.getReferentType() == operandType,
            "Result of ref_to_unowned does not have the "
            "operand's type as its referent type");
  }

  void checkUnownedToRefInst(UnownedToRefInst *I) {
    auto operandType = requireObjectType(UnownedStorageType,
                                         I->getOperand(),
                                         "Operand of unowned_to_ref");
    require(operandType->isLoadable(ResilienceExpansion::Maximal),
            "unowned_to_ref requires unowned type to be loadable");
    requireReferenceStorageCapableValue(I, "Result of unowned_to_ref");
    auto resultType = I->getType().getSwiftRValueType();
    require(operandType.getReferentType() == resultType,
            "Operand of unowned_to_ref does not have the "
            "operand's type as its referent type");
  }

  void checkRefToUnmanagedInst(RefToUnmanagedInst *I) {
    requireReferenceStorageCapableValue(I->getOperand(),
                                        "Operand of ref_to_unmanaged");
    auto operandType = I->getOperand()->getType().getSwiftRValueType();
    auto resultType = requireObjectType(UnmanagedStorageType, I,
                                        "Result of ref_to_unmanaged");
    require(resultType.getReferentType() == operandType,
            "Result of ref_to_unmanaged does not have the "
            "operand's type as its referent type");
  }

  void checkUnmanagedToRefInst(UnmanagedToRefInst *I) {
    auto operandType = requireObjectType(UnmanagedStorageType,
                                         I->getOperand(),
                                         "Operand of unmanaged_to_ref");
    requireReferenceStorageCapableValue(I, "Result of unmanaged_to_ref");
    auto resultType = I->getType().getSwiftRValueType();
    require(operandType.getReferentType() == resultType,
            "Operand of unmanaged_to_ref does not have the "
            "operand's type as its referent type");
  }

  void checkUpcastInst(UpcastInst *UI) {
    require(UI->getType() != UI->getOperand()->getType(),
            "can't upcast to same type");
    if (UI->getType().is<MetatypeType>()) {
      CanType instTy(UI->getType().castTo<MetatypeType>()->getInstanceType());
      require(UI->getOperand()->getType().is<MetatypeType>(),
              "upcast operand must be a class or class metatype instance");
      CanType opInstTy(UI->getOperand()->getType().castTo<MetatypeType>()
                         ->getInstanceType());
      auto instClass = instTy->getClassOrBoundGenericClass();
      require(instClass,
              "upcast must convert a class metatype to a class metatype");
      
      if (instClass->usesObjCGenericsModel()) {
        require(instClass->getDeclaredTypeInContext()
                  ->isBindableToSuperclassOf(opInstTy),
                "upcast must cast to a superclass or an existential metatype");
      } else {
        require(instTy->isExactSuperclassOf(opInstTy),
                "upcast must cast to a superclass or an existential metatype");
      }
      return;
    }

    require(UI->getType().getCategory() ==
            UI->getOperand()->getType().getCategory(),
            "Upcast can only upcast in between types of the same "
            "SILValueCategory. This prevents address types from being cast to "
            "object types or vis-a-versa");

    auto ToTy = UI->getType();
    auto FromTy = UI->getOperand()->getType();

    // Upcast from Optional<B> to Optional<A> is legal as long as B is a
    // subclass of A.
    if (ToTy.getSwiftRValueType().getAnyOptionalObjectType() &&
        FromTy.getSwiftRValueType().getAnyOptionalObjectType()) {
      ToTy = SILType::getPrimitiveObjectType(
          ToTy.getSwiftRValueType().getAnyOptionalObjectType());
      FromTy = SILType::getPrimitiveObjectType(
          FromTy.getSwiftRValueType().getAnyOptionalObjectType());
    }

    auto ToClass = ToTy.getClassOrBoundGenericClass();
    require(ToClass,
            "upcast must convert a class instance to a class type");
      if (ToClass->usesObjCGenericsModel()) {
        require(ToClass->getDeclaredTypeInContext()
                  ->isBindableToSuperclassOf(FromTy.getSwiftRValueType()),
                "upcast must cast to a superclass or an existential metatype");
      } else {
        require(ToTy.isExactSuperclassOf(FromTy),
                "upcast must cast to a superclass or an existential metatype");
      }
  }

  void checkAddressToPointerInst(AddressToPointerInst *AI) {
    require(AI->getOperand()->getType().isAddress(),
            "address-to-pointer operand must be an address");
    require(AI->getType().getSwiftRValueType()->isEqual(
                              AI->getType().getASTContext().TheRawPointerType),
            "address-to-pointer result type must be RawPointer");
  }
  
  void checkUncheckedRefCastInst(UncheckedRefCastInst *AI) {
    verifyOpenedArchetype(AI, AI->getType().getSwiftRValueType());
    require(AI->getOperand()->getType().isObject(),
            "unchecked_ref_cast operand must be a value");
    require(AI->getType().isObject(),
            "unchecked_ref_cast result must be an object");
    require(SILType::canRefCast(AI->getOperand()->getType(), AI->getType(),
                                AI->getModule()),
            "unchecked_ref_cast requires a heap object reference type");
  }

  void checkUncheckedRefCastAddrInst(UncheckedRefCastAddrInst *AI) {
    auto srcTy = AI->getSrc()->getType();
    auto destTy = AI->getDest()->getType();
    require(srcTy.isAddress(),
            "unchecked_ref_cast_addr operand must be an address");
    require(destTy.isAddress(),
            "unchecked_ref_cast_addr result must be an address");
    // The static src/dest types cannot be checked here even if they are
    // loadable. unchecked_ref_cast_addr may accept nonreference static types
    // (as a result of specialization). These cases will never be promoted to
    // value bitcast, thus will cause the subsequent runtime cast to fail.
  }
  
  void checkUncheckedAddrCastInst(UncheckedAddrCastInst *AI) {
    verifyOpenedArchetype(AI, AI->getType().getSwiftRValueType());

    require(AI->getOperand()->getType().isAddress(),
            "unchecked_addr_cast operand must be an address");
    require(AI->getType().isAddress(),
            "unchecked_addr_cast result must be an address");
  }
  
  void checkUncheckedTrivialBitCastInst(UncheckedTrivialBitCastInst *BI) {
    verifyOpenedArchetype(BI, BI->getType().getSwiftRValueType());
    require(BI->getOperand()->getType().isObject(),
            "unchecked_trivial_bit_cast must operate on a value");
    require(BI->getType().isObject(),
            "unchecked_trivial_bit_cast must produce a value");
    require(BI->getType().isTrivial(F.getModule()),
            "unchecked_trivial_bit_cast must produce a value of trivial type");
  }

  void checkUncheckedBitwiseCastInst(UncheckedBitwiseCastInst *BI) {
    verifyOpenedArchetype(BI, BI->getType().getSwiftRValueType());
    require(BI->getOperand()->getType().isObject(),
            "unchecked_bitwise_cast must operate on a value");
    require(BI->getType().isObject(),
            "unchecked_bitwise_cast must produce a value");
  }

  void checkRefToRawPointerInst(RefToRawPointerInst *AI) {
    require(AI->getOperand()->getType().getSwiftRValueType()
              ->isAnyClassReferenceType(),
            "ref-to-raw-pointer operand must be a class reference or"
            " NativeObject");
    require(AI->getType().getSwiftRValueType()->isEqual(
                            AI->getType().getASTContext().TheRawPointerType),
            "ref-to-raw-pointer result must be RawPointer");
  }

  void checkRawPointerToRefInst(RawPointerToRefInst *AI) {
    verifyOpenedArchetype(AI, AI->getType().getSwiftRValueType());
    require(AI->getType()
              .getSwiftRValueType()->isBridgeableObjectType()
            || AI->getType().getSwiftRValueType()->isEqual(
                             AI->getType().getASTContext().TheNativeObjectType),
        "raw-pointer-to-ref result must be a class reference or NativeObject");
    require(AI->getOperand()->getType().getSwiftRValueType()->isEqual(
                            AI->getType().getASTContext().TheRawPointerType),
            "raw-pointer-to-ref operand must be NativeObject");
  }
  
  void checkRefToBridgeObjectInst(RefToBridgeObjectInst *RI) {
    require(RI->getConverted()->getType().isObject(),
            "ref_to_bridge_object must convert from a value");
    require(RI->getConverted()->getType().getSwiftRValueType()
              ->isBridgeableObjectType(),
            "ref_to_bridge_object must convert from a heap object ref");
    require(RI->getBitsOperand()->getType()
              == SILType::getBuiltinWordType(F.getASTContext()),
            "ref_to_bridge_object must take a Builtin.Word bits operand");
    require(RI->getType() == SILType::getBridgeObjectType(F.getASTContext()),
            "ref_to_bridge_object must produce a BridgeObject");
  }
  
  void checkBridgeObjectToRefInst(BridgeObjectToRefInst *RI) {
    verifyOpenedArchetype(RI, RI->getType().getSwiftRValueType());
    require(RI->getConverted()->getType()
               == SILType::getBridgeObjectType(F.getASTContext()),
            "bridge_object_to_ref must take a BridgeObject");
    require(RI->getType().isObject(),
            "bridge_object_to_ref must produce a value");
    require(RI->getType().getSwiftRValueType()->isBridgeableObjectType(),
            "bridge_object_to_ref must produce a heap object reference");
  }
  void checkBridgeObjectToWordInst(BridgeObjectToWordInst *RI) {
    require(RI->getConverted()->getType()
               == SILType::getBridgeObjectType(F.getASTContext()),
            "bridge_object_to_word must take a BridgeObject");
    require(RI->getType().isObject(),
            "bridge_object_to_word must produce a value");
    require(RI->getType() == SILType::getBuiltinWordType(F.getASTContext()),
            "bridge_object_to_word must produce a Word");
  }

  void checkConvertFunctionInst(ConvertFunctionInst *ICI) {
    auto opTI = requireObjectType(SILFunctionType, ICI->getOperand(),
                                  "convert_function operand");
    auto resTI = requireObjectType(SILFunctionType, ICI,
                                   "convert_function result");

    // convert_function is required to be an ABI-compatible conversion.
    requireABICompatibleFunctionTypes(
        opTI, resTI, "convert_function cannot change function ABI");
  }

  void checkThinFunctionToPointerInst(ThinFunctionToPointerInst *CI) {
    auto opTI = requireObjectType(SILFunctionType, CI->getOperand(),
                                  "thin_function_to_pointer operand");
    requireObjectType(BuiltinRawPointerType, CI,
                      "thin_function_to_pointer result");

    auto rep = opTI->getRepresentation();
    require(rep == SILFunctionTypeRepresentation::Thin ||
            rep == SILFunctionTypeRepresentation::Method ||
            rep == SILFunctionTypeRepresentation::WitnessMethod,
            "thin_function_to_pointer only works on thin, method or "
            "witness_method functions");
  }

  void checkPointerToThinFunctionInst(PointerToThinFunctionInst *CI) {
    auto resultTI = requireObjectType(SILFunctionType, CI,
                                      "pointer_to_thin_function result");
    requireObjectType(BuiltinRawPointerType, CI->getOperand(),
                      "pointer_to_thin_function operand");

    auto rep = resultTI->getRepresentation();
    require(rep == SILFunctionTypeRepresentation::Thin ||
            rep == SILFunctionTypeRepresentation::Method ||
            rep == SILFunctionTypeRepresentation::WitnessMethod,
            "pointer_to_thin_function only works on thin, method or "
            "witness_method functions");
  }

  void checkCondFailInst(CondFailInst *CFI) {
    require(CFI->getOperand()->getType()
              == SILType::getBuiltinIntegerType(1, F.getASTContext()),
            "cond_fail operand must be a Builtin.Int1");
  }

  void checkReturnInst(ReturnInst *RI) {
    DEBUG(RI->print(llvm::dbgs()));

    SILType functionResultType =
        F.mapTypeIntoContext(fnConv.getSILResultType());
    SILType instResultType = RI->getOperand()->getType();
    DEBUG(llvm::dbgs() << "function return type: ";
          functionResultType.dump();
          llvm::dbgs() << "return inst type: ";
          instResultType.dump(););
    require(functionResultType == instResultType,
            "return value type does not match return type of function");
  }

  void checkThrowInst(ThrowInst *TI) {
    DEBUG(TI->print(llvm::dbgs()));

    CanSILFunctionType fnType = F.getLoweredFunctionType();
    require(fnType->hasErrorResult(),
            "throw in function that doesn't have an error result");

    SILType functionResultType = F.mapTypeIntoContext(fnConv.getSILErrorType());
    SILType instResultType = TI->getOperand()->getType();
    DEBUG(llvm::dbgs() << "function error result type: ";
          functionResultType.dump();
          llvm::dbgs() << "throw operand type: ";
          instResultType.dump(););
    require(functionResultType == instResultType,
            "throw operand type does not match error result type of function");
  }
  
  void checkUnwindInst(UnwindInst *UI) {
    require(F.getLoweredFunctionType()->isCoroutine(),
            "unwind in non-coroutine function");
  }

  void checkYieldInst(YieldInst *YI) {
    CanSILFunctionType fnType = F.getLoweredFunctionType();
    require(fnType->isCoroutine(),
            "yield in non-coroutine function");

    auto yieldedValues = YI->getYieldedValues();
    auto yieldInfos = fnType->getYields();
    require(yieldedValues.size() == yieldInfos.size(),
            "wrong number of yielded values for function");
    for (auto i : indices(yieldedValues)) {
      SILType yieldType =
        F.mapTypeIntoContext(fnConv.getSILType(yieldInfos[i]));
      require(yieldedValues[i]->getType() == yieldType,
              "yielded value does not match yield type of coroutine");
    }

    // We require the resume and unwind destinations to be unique in order
    // to prevent either edge from becoming critical.
    require(YI->getResumeBB()->getSinglePredecessorBlock(),
            "resume dest of 'yield' must be uniquely used");
    require(YI->getUnwindBB()->getSinglePredecessorBlock(),
            "unwind dest of 'yield' must be uniquely used");
  }

  void checkSelectEnumCases(SelectEnumInstBase *I) {
    EnumDecl *eDecl = I->getEnumOperand()->getType().getEnumOrBoundGenericEnum();
    require(eDecl, "select_enum operand must be an enum");

    // Find the set of enum elements for the type so we can verify
    // exhaustiveness.
    // FIXME: We also need to consider if the enum is resilient, in which case
    // we're never guaranteed to be exhaustive.
    llvm::DenseSet<EnumElementDecl*> unswitchedElts;
    eDecl->getAllElements(unswitchedElts);

    // Verify the set of enum cases we dispatch on.
    for (unsigned i = 0, e = I->getNumCases(); i < e; ++i) {
      EnumElementDecl *elt;
      SILValue result;
      std::tie(elt, result) = I->getCase(i);

      require(elt->getDeclContext() == eDecl,
              "select_enum dispatches on enum element that is not part of "
              "its type");
      require(unswitchedElts.count(elt),
              "select_enum dispatches on same enum element more than once");
      unswitchedElts.erase(elt);

      // The result value must match the type of the instruction.
      requireSameType(result->getType(), I->getType(),
                    "select_enum case operand must match type of instruction");
    }

    // If the select is non-exhaustive, we require a default.
    require(unswitchedElts.empty() || I->hasDefault(),
            "nonexhaustive select_enum must have a default destination");
    if (I->hasDefault()) {
      requireSameType(I->getDefaultResult()->getType(),
                  I->getType(),
                  "select_enum default operand must match type of instruction");
    }
  }

  void checkSelectEnumInst(SelectEnumInst *SEI) {
    require(SEI->getEnumOperand()->getType().isObject(),
            "select_enum operand must be an object");
    
    checkSelectEnumCases(SEI);
  }
  void checkSelectEnumAddrInst(SelectEnumAddrInst *SEI) {
    require(SEI->getEnumOperand()->getType().isAddress(),
            "select_enum_addr operand must be an address");
    
    checkSelectEnumCases(SEI);
  }

  void checkSwitchValueInst(SwitchValueInst *SVI) {
    // TODO: Type should be either integer or function
    auto Ty = SVI->getOperand()->getType();
    require(Ty.is<BuiltinIntegerType>() || Ty.is<SILFunctionType>(),
            "switch_value operand should be either of an integer "
            "or function type");

    auto ult = [](const SILValue &a, const SILValue &b) { 
      return a == b || a < b; 
    };

    std::set<SILValue, decltype(ult)> cases(ult);

    for (unsigned i = 0, e = SVI->getNumCases(); i < e; ++i) {
      SILValue value;
      SILBasicBlock *dest;
      std::tie(value, dest) = SVI->getCase(i);

      require(value->getType() == Ty,
             "switch_value case value should have the same type as its operand");

      require(!cases.count(value),
              "multiple switch_value cases for same value");
      cases.insert(value);

      require(dest->args_empty(),
              "switch_value case destination cannot take arguments");
    }

    if (SVI->hasDefault())
      require(SVI->getDefaultBB()->args_empty(),
              "switch_value default destination cannot take arguments");
  }

  void checkSelectValueCases(SelectValueInst *I) {
    struct APIntCmp {
      bool operator()(const APInt &a, const APInt &b) const {
        return a.ult(b);
      };
    };

    llvm::SmallSet<APInt, 16, APIntCmp> seenCaseValues;

    // Verify the set of cases we dispatch on.
    for (unsigned i = 0, e = I->getNumCases(); i < e; ++i) {
      SILValue casevalue;
      SILValue result;
      std::tie(casevalue, result) = I->getCase(i);
      
      if (!isa<SILUndef>(casevalue)) {
        auto  *il = dyn_cast<IntegerLiteralInst>(casevalue);
        require(il,
                "select_value case operands should refer to integer literals");
        APInt elt = il->getValue();

        require(!seenCaseValues.count(elt),
                "select_value dispatches on same case value more than once");

        seenCaseValues.insert(elt);
      }

      requireSameType(I->getOperand()->getType(), casevalue->getType(),
                      "select_value case value must match type of operand");

      // The result value must match the type of the instruction.
      requireSameType(result->getType(), I->getType(),
                    "select_value case result must match type of instruction");
    }

    require(I->hasDefault(),
            "select_value should always have a default");
    requireSameType(I->getDefaultResult()->getType(),
                  I->getType(),
                  "select_value default operand must match type of instruction");
  }

  void checkSelectValueInst(SelectValueInst *SVI) {
    require(SVI->getOperand()->getType().isObject(),
            "select_value operand must be an object");

    checkSelectValueCases(SVI);
  }

  void checkSwitchEnumInst(SwitchEnumInst *SOI) {
    require(SOI->getOperand()->getType().isObject(),
            "switch_enum operand must be an object");

    SILType uTy = SOI->getOperand()->getType();
    EnumDecl *uDecl = uTy.getEnumOrBoundGenericEnum();
    require(uDecl, "switch_enum operand is not an enum");

    // Find the set of enum elements for the type so we can verify
    // exhaustiveness.
    // FIXME: We also need to consider if the enum is resilient, in which case
    // we're never guaranteed to be exhaustive.
    llvm::DenseSet<EnumElementDecl*> unswitchedElts;
    uDecl->getAllElements(unswitchedElts);

    // Verify the set of enum cases we dispatch on.
    for (unsigned i = 0, e = SOI->getNumCases(); i < e; ++i) {
      EnumElementDecl *elt;
      SILBasicBlock *dest;
      std::tie(elt, dest) = SOI->getCase(i);

      require(elt->getDeclContext() == uDecl,
              "switch_enum dispatches on enum element that is not part of "
              "its type");
      require(unswitchedElts.count(elt),
              "switch_enum dispatches on same enum element more than once");
      unswitchedElts.erase(elt);

      // The destination BB can take the argument payload, if any, as a BB
      // arguments, or it can ignore it and take no arguments.
      if (elt->hasAssociatedValues()) {
        if (isSILOwnershipEnabled() && F.hasQualifiedOwnership()) {
          require(dest->getArguments().size() == 1,
                  "switch_enum destination for case w/ args must take 1 "
                  "argument");
        } else {
          require(dest->getArguments().size() == 0 ||
                      dest->getArguments().size() == 1,
                  "switch_enum destination for case w/ args must take 0 or 1 "
                  "arguments");
        }

        if (dest->getArguments().size() == 1) {
          SILType eltArgTy = uTy.getEnumElementType(elt, F.getModule());
          SILType bbArgTy = dest->getArguments()[0]->getType();
          require(eltArgTy == bbArgTy,
                  "switch_enum destination bbarg must match case arg type");
          require(!dest->getArguments()[0]->getType().isAddress(),
                  "switch_enum destination bbarg type must not be an address");
        }

      } else {
        require(dest->getArguments().size() == 0,
                "switch_enum destination for no-argument case must take no "
                "arguments");
      }
    }

    // If the switch is non-exhaustive, we require a default.
    require(unswitchedElts.empty() || SOI->hasDefault(),
            "nonexhaustive switch_enum must have a default destination");
    if (SOI->hasDefault()) {
      // When SIL ownership is enabled, we require all default branches to take
      // an @owned original version of the enum.
      //
      // When SIL ownership is disabled, we no longer support this.
      if (isSILOwnershipEnabled() && F.hasQualifiedOwnership()) {
        require(SOI->getDefaultBB()->getNumArguments() == 1,
                "Switch enum default block should have one argument");
        require(SOI->getDefaultBB()->getArgument(0)->getType() ==
                    SOI->getOperand()->getType(),
                "Switch enum default block should have one argument that is "
                "the same as the input type");
      } else if (!F.hasQualifiedOwnership()) {
        require(SOI->getDefaultBB()->args_empty(),
                "switch_enum default destination must take no arguments");
      }
    }
  }

  void checkSwitchEnumAddrInst(SwitchEnumAddrInst *SOI) {
    require(SOI->getOperand()->getType().isAddress(),
            "switch_enum_addr operand must be an address");

    SILType uTy = SOI->getOperand()->getType();
    EnumDecl *uDecl = uTy.getEnumOrBoundGenericEnum();
    require(uDecl, "switch_enum_addr operand must be an enum");

    // Find the set of enum elements for the type so we can verify
    // exhaustiveness.
    // FIXME: We also need to consider if the enum is resilient, in which case
    // we're never guaranteed to be exhaustive.
    llvm::DenseSet<EnumElementDecl*> unswitchedElts;
    uDecl->getAllElements(unswitchedElts);

    // Verify the set of enum cases we dispatch on.
    for (unsigned i = 0, e = SOI->getNumCases(); i < e; ++i) {
      EnumElementDecl *elt;
      SILBasicBlock *dest;
      std::tie(elt, dest) = SOI->getCase(i);

      require(elt->getDeclContext() == uDecl,
              "switch_enum_addr dispatches on enum element that "
              "is not part of its type");
      require(unswitchedElts.count(elt),
              "switch_enum_addr dispatches on same enum element "
              "more than once");
      unswitchedElts.erase(elt);

      // The destination BB must not have BB arguments.
      require(dest->getArguments().size() == 0,
              "switch_enum_addr destination must take no BB args");
    }

    // If the switch is non-exhaustive, we require a default.
    require(unswitchedElts.empty() || SOI->hasDefault(),
            "nonexhaustive switch_enum_addr must have a default "
            "destination");
    if (SOI->hasDefault())
      require(SOI->getDefaultBB()->args_empty(),
              "switch_enum_addr default destination must take "
              "no arguments");
  }

  bool verifyBranchArgs(SILValue branchArg, SILArgument *bbArg) {
    // NOTE: IRGen currently does not support the following method_inst
    // variants as branch arguments.
    // Once this is supported, the check can be removed.
    require(!(isa<MethodInst>(branchArg) &&
              cast<MethodInst>(branchArg)->getMember().isForeign),
        "branch argument cannot be a witness_method or an objc method_inst");
    return branchArg->getType() == bbArg->getType();
  }

  void checkBranchInst(BranchInst *BI) {
    require(BI->getArgs().size() == BI->getDestBB()->args_size(),
            "branch has wrong number of arguments for dest bb");
    require(std::equal(BI->getArgs().begin(), BI->getArgs().end(),
                       BI->getDestBB()->args_begin(),
                       [&](SILValue branchArg, SILArgument *bbArg) {
                         return verifyBranchArgs(branchArg, bbArg);
                       }),
            "branch argument types do not match arguments for dest bb");
  }

  void checkCondBranchInst(CondBranchInst *CBI) {
    // It is important that cond_br keeps an i1 type. ARC Sequence Opts assumes
    // that cond_br does not use reference counted values or decrement reference
    // counted values under the assumption that the instruction that computes
    // the i1 is the use/decrement that ARC cares about and that after that
    // instruction is evaluated, the scalar i1 has a different identity and the
    // object can be deallocated.
    require(CBI->getCondition()->getType() ==
             SILType::getBuiltinIntegerType(1,
                                 CBI->getCondition()->getType().getASTContext()),
            "condition of conditional branch must have Int1 type");

    require(CBI->getTrueArgs().size() == CBI->getTrueBB()->args_size(),
            "true branch has wrong number of arguments for dest bb");
    require(CBI->getTrueBB() != CBI->getFalseBB(),
            "identical destinations");
    require(std::equal(CBI->getTrueArgs().begin(), CBI->getTrueArgs().end(),
                       CBI->getTrueBB()->args_begin(),
                       [&](SILValue branchArg, SILArgument *bbArg) {
                         return verifyBranchArgs(branchArg, bbArg);
                       }),
            "true branch argument types do not match arguments for dest bb");

    require(CBI->getFalseArgs().size() == CBI->getFalseBB()->args_size(),
            "false branch has wrong number of arguments for dest bb");
    require(std::equal(CBI->getFalseArgs().begin(), CBI->getFalseArgs().end(),
                       CBI->getFalseBB()->args_begin(),
                       [&](SILValue branchArg, SILArgument *bbArg) {
                         return verifyBranchArgs(branchArg, bbArg);
                       }),
            "false branch argument types do not match arguments for dest bb");
  }

  void checkDynamicMethodBranchInst(DynamicMethodBranchInst *DMBI) {
    SILType operandType = DMBI->getOperand()->getType();

    require(DMBI->getMember().getDecl()->isObjC(), "method must be @objc");
    if (!DMBI->getMember().getDecl()->isInstanceMember()) {
      require(operandType.getSwiftRValueType()->is<MetatypeType>(),
              "operand must have metatype type");
      require(operandType.getSwiftRValueType()->castTo<MetatypeType>()
                ->getInstanceType()->mayHaveSuperclass(),
              "operand must have metatype of class or class-bound type");
    }

    // Check that the branch argument is of the expected dynamic method type.
    require(DMBI->getHasMethodBB()->args_size() == 1,
            "true bb for dynamic_method_br must take an argument");

    auto bbArgTy = DMBI->getHasMethodBB()->args_begin()[0]->getType();
    require(getDynamicMethodType(operandType, DMBI->getMember())
              .getSwiftRValueType()
              ->isBindableTo(bbArgTy.getSwiftRValueType()),
            "bb argument for dynamic_method_br must be of the method's type");
  }

  void checkProjectBlockStorageInst(ProjectBlockStorageInst *PBSI) {
    require(PBSI->getOperand()->getType().isAddress(),
            "operand must be an address");
    auto storageTy = PBSI->getOperand()->getType().getAs<SILBlockStorageType>();
    require(storageTy, "operand must be a @block_storage type");
    
    require(PBSI->getType().isAddress(),
            "result must be an address");
    auto captureTy = PBSI->getType().getSwiftRValueType();
    require(storageTy->getCaptureType() == captureTy,
            "result must be the capture type of the @block_storage type");
  }
  
  void checkInitBlockStorageHeaderInst(InitBlockStorageHeaderInst *IBSHI) {
    require(IBSHI->getBlockStorage()->getType().isAddress(),
            "block storage operand must be an address");
    auto storageTy
      = IBSHI->getBlockStorage()->getType().getAs<SILBlockStorageType>();
    require(storageTy, "block storage operand must be a @block_storage type");
    
    require(IBSHI->getInvokeFunction()->getType().isObject(),
            "invoke function operand must be a value");
    auto invokeTy
      = IBSHI->getInvokeFunction()->getType().getAs<SILFunctionType>();
    require(invokeTy, "invoke function operand must be a function");
    require(invokeTy->getRepresentation()
              == SILFunctionType::Representation::CFunctionPointer,
            "invoke function operand must be a c function");
    require(invokeTy->getParameters().size() >= 1,
            "invoke function must take at least one parameter");
    require(!invokeTy->getGenericSignature() ||
            invokeTy->getExtInfo().isPseudogeneric(),
            "invoke function must not take reified generic parameters");
    
    invokeTy = checkApplySubstitutions(IBSHI->getSubstitutions(),
                                    SILType::getPrimitiveObjectType(invokeTy));
    
    auto storageParam = invokeTy->getParameters()[0];
    require(storageParam.getConvention() ==
            ParameterConvention::Indirect_InoutAliasable,
            "invoke function must take block storage as @inout_aliasable "
            "parameter");
    require(storageParam.getType() == storageTy,
            "invoke function must take block storage type as first parameter");
    
    require(IBSHI->getType().isObject(), "result must be a value");
    auto blockTy = IBSHI->getType().getAs<SILFunctionType>();
    require(blockTy, "result must be a function");
    require(blockTy->getRepresentation() == SILFunctionType::Representation::Block,
            "result must be a cdecl block function");
    require(blockTy->getResults() == invokeTy->getResults(),
            "result must have same results as invoke function");

    require(blockTy->getParameters().size() + 1
              == invokeTy->getParameters().size(),
          "result must match all parameters of invoke function but the first");
    auto blockParams = blockTy->getParameters();
    auto invokeBlockParams = invokeTy->getParameters().slice(1);
    for (unsigned i : indices(blockParams)) {
      require(blockParams[i] == invokeBlockParams[i],
          "result must match all parameters of invoke function but the first");
    }
  }
  
  void checkObjCProtocolInst(ObjCProtocolInst *OPI) {
    require(OPI->getProtocol()->isObjC(),
            "objc_protocol must be applied to an @objc protocol");
    auto classTy = OPI->getType();
    require(classTy.isObject(), "objc_protocol must produce a value");
    auto classDecl = classTy.getClassOrBoundGenericClass();
    require(classDecl, "objc_protocol must produce a class instance");
    require(classDecl->getName() == F.getASTContext().Id_Protocol,
            "objc_protocol must produce an instance of ObjectiveC.Protocol class");
    require(classDecl->getModuleContext()->getName() == F.getASTContext().Id_ObjectiveC,
            "objc_protocol must produce an instance of ObjectiveC.Protocol class");
  }
  
  void checkObjCMetatypeToObjectInst(ObjCMetatypeToObjectInst *OMOI) {
    require(OMOI->getOperand()->getType().isObject(),
            "objc_metatype_to_object must take a value");
    auto fromMetaTy = OMOI->getOperand()->getType().getAs<MetatypeType>();
    require(fromMetaTy, "objc_metatype_to_object must take an @objc metatype value");
    require(fromMetaTy->getRepresentation() == MetatypeRepresentation::ObjC,
            "objc_metatype_to_object must take an @objc metatype value");
    require(OMOI->getType().isObject(),
            "objc_metatype_to_object must produce a value");
    require(OMOI->getType().getSwiftRValueType()->isAnyObject(),
            "objc_metatype_to_object must produce an AnyObject value");
  }

  void checkObjCExistentialMetatypeToObjectInst(
                                    ObjCExistentialMetatypeToObjectInst *OMOI) {
    require(OMOI->getOperand()->getType().isObject(),
            "objc_metatype_to_object must take a value");
    auto fromMetaTy = OMOI->getOperand()->getType()
      .getAs<ExistentialMetatypeType>();
    require(fromMetaTy, "objc_metatype_to_object must take an @objc existential metatype value");
    require(fromMetaTy->getRepresentation() == MetatypeRepresentation::ObjC,
            "objc_metatype_to_object must take an @objc existential metatype value");
    require(OMOI->getType().isObject(),
            "objc_metatype_to_object must produce a value");
    require(OMOI->getType().getSwiftRValueType()->isAnyObject(),
            "objc_metatype_to_object must produce an AnyObject value");
  }
  
  void checkKeyPathInst(KeyPathInst *KPI) {
    auto kpTy = KPI->getType();
    
    require(kpTy.isObject(), "keypath result must be an object type");
    
    auto kpBGT = kpTy.getAs<BoundGenericType>();
    require(kpBGT, "keypath result must be a generic type");
    auto &C = F.getASTContext();
    require(kpBGT->getDecl() == C.getKeyPathDecl()
            || kpBGT->getDecl() == C.getWritableKeyPathDecl()
            || kpBGT->getDecl() == C.getReferenceWritableKeyPathDecl(),
            "keypath result must be a key path type");
    
    auto baseTy = CanType(kpBGT->getGenericArgs()[0]);
    auto pattern = KPI->getPattern();
    SubstitutionMap patternSubs;
    if (pattern->getGenericSignature())
      patternSubs = pattern->getGenericSignature()
                           ->getSubstitutionMap(KPI->getSubstitutions());
    require(baseTy == pattern->getRootType().subst(patternSubs)->getCanonicalType(),
            "keypath root type should match root type of keypath pattern");

    auto leafTy = CanType(kpBGT->getGenericArgs()[1]);
    require(leafTy == pattern->getValueType().subst(patternSubs)->getCanonicalType(),
            "keypath value type should match value type of keypath pattern");
    
    {
      Lowering::GenericContextScope scope(F.getModule().Types,
                                          pattern->getGenericSignature());
      
      for (auto &component : pattern->getComponents()) {
        auto loweredBaseTy =
          F.getModule().Types.getLoweredType(AbstractionPattern::getOpaque(),
                                             baseTy);
        auto componentTy = component.getComponentType().subst(patternSubs)
          ->getCanonicalType();
        auto loweredComponentTy =
          F.getModule().Types.getLoweredType(AbstractionPattern::getOpaque(),
                                             componentTy);
        
        switch (auto kind = component.getKind()) {
        case KeyPathPatternComponent::Kind::StoredProperty: {
          auto property = component.getStoredPropertyDecl();
          require(property->getDeclContext()
                   == baseTy->getAnyNominal(),
                  "property decl should be a member of the component base type");
          switch (property->getStorageKind()) {
          case AbstractStorageDecl::Stored:
          case AbstractStorageDecl::StoredWithObservers:
          case AbstractStorageDecl::StoredWithTrivialAccessors:
            break;
          case AbstractStorageDecl::Addressed:
          case AbstractStorageDecl::AddressedWithObservers:
          case AbstractStorageDecl::AddressedWithTrivialAccessors:
          case AbstractStorageDecl::Computed:
          case AbstractStorageDecl::ComputedWithMutableAddress:
          case AbstractStorageDecl::InheritedWithObservers:
            require(false, "property must be stored");
          }
          auto propertyTy = loweredBaseTy.getFieldType(property, F.getModule());
          require(propertyTy.getObjectType()
                    == loweredComponentTy.getObjectType(),
                  "component type should match the maximal abstraction of the "
                  "formal type");
          break;
        }
          
        case KeyPathPatternComponent::Kind::GettableProperty:
        case KeyPathPatternComponent::Kind::SettableProperty: {
          bool hasIndices = !component.getComputedPropertyIndices().empty();
        
          // Getter should be <Sig...> @convention(thin) (@in Base) -> @out Result
          {
            auto getter = component.getComputedPropertyGetter();
            auto substGetterType = getter->getLoweredFunctionType()
              ->substGenericArgs(F.getModule(), KPI->getSubstitutions());
            require(substGetterType->getRepresentation() ==
                      SILFunctionTypeRepresentation::Thin,
                    "getter should be a thin function");
            
            require(substGetterType->getNumParameters() == 1 + hasIndices,
                    "getter should have one parameter");
            auto baseParam = substGetterType->getParameters()[0];
            require(baseParam.getConvention() ==
                      ParameterConvention::Indirect_In,
                    "getter base parameter should be @in");
            require(baseParam.getType() == loweredBaseTy.getSwiftRValueType(),
                    "getter base parameter should match base of component");
            
            if (hasIndices) {
              auto indicesParam = substGetterType->getParameters()[1];
              require(indicesParam.getConvention()
                        == ParameterConvention::Direct_Unowned,
                      "indices pointer should be trivial");
              require(indicesParam.getType()->getAnyNominal()
                        == C.getUnsafeRawPointerDecl(),
                      "indices pointer should be an UnsafeRawPointer");
            }

            require(substGetterType->getNumResults() == 1,
                    "getter should have one result");
            auto result = substGetterType->getResults()[0];
            require(result.getConvention() == ResultConvention::Indirect,
                    "getter result should be @out");
            require(result.getType() == loweredComponentTy.getSwiftRValueType(),
                    "getter result should match the maximal abstraction of the "
                    "formal component type");
          }
          
          if (kind == KeyPathPatternComponent::Kind::SettableProperty) {
            // Setter should be
            // <Sig...> @convention(thin) (@in Result, @in Base) -> ()
            
            auto setter = component.getComputedPropertySetter();
            auto substSetterType = setter->getLoweredFunctionType()
              ->substGenericArgs(F.getModule(), KPI->getSubstitutions());
            
            require(substSetterType->getRepresentation() ==
                      SILFunctionTypeRepresentation::Thin,
                    "setter should be a thin function");
            
            require(substSetterType->getNumParameters() == 2 + hasIndices,
                    "setter should have two parameters");

            auto newValueParam = substSetterType->getParameters()[0];
            require(newValueParam.getConvention() ==
                      ParameterConvention::Indirect_In,
                    "setter value parameter should be @in");

            auto baseParam = substSetterType->getParameters()[1];
            require(baseParam.getConvention() ==
                      ParameterConvention::Indirect_In
                    || baseParam.getConvention() ==
                        ParameterConvention::Indirect_Inout,
                    "setter base parameter should be @in or @inout");
            
            if (hasIndices) {
              auto indicesParam = substSetterType->getParameters()[2];
              require(indicesParam.getConvention()
                        == ParameterConvention::Direct_Unowned,
                      "indices pointer should be trivial");
              require(indicesParam.getType()->getAnyNominal()
                        == C.getUnsafeRawPointerDecl(),
                      "indices pointer should be an UnsafeRawPointer");
            }

            require(newValueParam.getType() ==
                      loweredComponentTy.getSwiftRValueType(),
                    "setter value should match the maximal abstraction of the "
                    "formal component type");
            
            require(substSetterType->getNumResults() == 0,
                    "setter should have no results");
          }
          
          for (auto &index : component.getComputedPropertyIndices()) {
            auto opIndex = index.Operand;
            auto contextType =
              index.LoweredType.subst(F.getModule(), patternSubs);
            requireSameType(contextType,
                            KPI->getAllOperands()[opIndex].get()->getType(),
                            "operand must match type required by pattern");
            require(isLoweringOf(index.LoweredType, index.FormalType),
                    "pattern index formal type doesn't match lowered type");
          }
          
          if (!component.getComputedPropertyIndices().empty()) {
            // Equals should be
            // <Sig...> @convention(thin) (RawPointer, RawPointer) -> Bool
            {
              auto equals = component.getComputedPropertyIndexEquals();
              require(equals, "key path pattern with indexes must have equals "
                              "operator");
              
              auto substEqualsType = equals->getLoweredFunctionType()
                ->substGenericArgs(F.getModule(), KPI->getSubstitutions());
              
              require(substEqualsType->getParameters().size() == 2,
                      "must have two arguments");
              for (unsigned i = 0; i < 2; ++i) {
                auto param = substEqualsType->getParameters()[i];
                require(param.getConvention()
                          == ParameterConvention::Direct_Unowned,
                        "indices pointer should be trivial");
                require(param.getType()->getAnyNominal()
                          == C.getUnsafeRawPointerDecl(),
                        "indices pointer should be an UnsafeRawPointer");
              }
              
              require(substEqualsType->getResults().size() == 1,
                      "must have one result");
              
              require(substEqualsType->getResults()[0].getConvention()
                        == ResultConvention::Unowned,
                      "result should be unowned");
              require(substEqualsType->getResults()[0].getType()->getAnyNominal()
                        == C.getBoolDecl(),
                      "result should be Bool");
            }
            {
              // Hash should be
              // <Sig...> @convention(thin) (RawPointer) -> Int
              auto hash = component.getComputedPropertyIndexHash();
              require(hash, "key path pattern with indexes must have hash "
                            "operator");
              
              auto substHashType = hash->getLoweredFunctionType()
                ->substGenericArgs(F.getModule(), KPI->getSubstitutions());
              
              require(substHashType->getParameters().size() == 1,
                      "must have two arguments");
              auto param = substHashType->getParameters()[0];
              require(param.getConvention()
                        == ParameterConvention::Direct_Unowned,
                      "indices pointer should be trivial");
              require(param.getType()->getAnyNominal()
                        == C.getUnsafeRawPointerDecl(),
                      "indices pointer should be an UnsafeRawPointer");
              
              require(substHashType->getResults().size() == 1,
                      "must have one result");
              
              require(substHashType->getResults()[0].getConvention()
                        == ResultConvention::Unowned,
                      "result should be unowned");
              require(substHashType->getResults()[0].getType()->getAnyNominal()
                        == C.getIntDecl(),
                      "result should be Int");
            }
          } else {
            require(!component.getComputedPropertyIndexEquals()
                    && !component.getComputedPropertyIndexHash(),
                    "component without indexes must not have equals/hash");
          }

          break;
        }
        case KeyPathPatternComponent::Kind::OptionalChain: {
          require(baseTy->getAnyOptionalObjectType()->isEqual(componentTy),
                  "chaining component should unwrap optional");
          require(leafTy->getAnyOptionalObjectType(),
                  "key path with chaining component should have optional "
                  "result");
          break;
        }
        case KeyPathPatternComponent::Kind::OptionalForce: {
          require(baseTy->getAnyOptionalObjectType()->isEqual(componentTy),
                  "forcing component should unwrap optional");
          break;
        }
        case KeyPathPatternComponent::Kind::OptionalWrap: {
          require(componentTy->getAnyOptionalObjectType()->isEqual(baseTy),
                  "wrapping component should wrap optional");
          break;
        }
        }
        
        baseTy = componentTy;
      }
    }
    require(CanType(baseTy) == CanType(leafTy),
            "final component should match leaf value type of key path type");
  }

  // This verifies that the entry block of a SIL function doesn't have
  // any predecessors and also verifies the entry point arguments.
  void verifyEntryBlock(SILBasicBlock *entry) {
    require(entry->pred_empty(), "entry block cannot have predecessors");

    DEBUG(llvm::dbgs() << "Argument types for entry point BB:\n";
          for (auto *arg
               : make_range(entry->args_begin(), entry->args_end()))
              arg->getType()
                  .dump();
          llvm::dbgs() << "Input types for SIL function type ";
          F.getLoweredFunctionType()->print(llvm::dbgs());
          llvm::dbgs() << ":\n";
          for (auto paramTy
               : fnConv.getParameterSILTypes()) { paramTy.dump(); });

    require(entry->args_size() == (fnConv.getNumIndirectSILResults()
                                   + fnConv.getNumParameters()),
            "entry point has wrong number of arguments");

    bool matched = true;
    auto argI = entry->args_begin();
    SILModule &M = F.getModule();

    auto check = [&](const char *what, SILType ty) {
      auto mappedTy = F.mapTypeIntoContext(ty);
      SILArgument *bbarg = *argI;
      ++argI;
      if (bbarg->getType() != mappedTy) {
        llvm::errs() << what << " type mismatch!\n";
        llvm::errs() << "  argument: "; bbarg->dump();
        llvm::errs() << "  expected: "; mappedTy.dump();
        matched = false;
      }

      // If we do not have qualified ownership, do not check ownership.
      if (!F.hasQualifiedOwnership()) {
        return;
      }

      auto ownershipkind = ValueOwnershipKind(
          M, mappedTy, fnConv.getSILArgumentConvention(bbarg->getIndex()));

      if (bbarg->getOwnershipKind() != ownershipkind) {
        llvm::errs() << what << " ownership kind mismatch!\n";
        llvm::errs() << "  argument: " << bbarg->getOwnershipKind() << '\n';
        llvm::errs() << "  expected: " << ownershipkind << '\n';
        matched = false;
      }
    };

    for (auto result : fnConv.getIndirectSILResults()) {
      assert(fnConv.isSILIndirect(result));
      check("result", fnConv.getSILType(result));
    }
    for (auto param : F.getLoweredFunctionType()->getParameters()) {
      check("parameter", fnConv.getSILType(param));
    }

    require(matched, "entry point argument types do not match function type");

    // TBAA requirement for all address arguments.
    require(std::equal(entry->args_begin() + fnConv.getNumIndirectSILResults(),
                       entry->args_end(),
                       fnConv.funcTy->getParameters().begin(),
                       [&](SILArgument *bbarg, SILParameterInfo paramInfo) {
                         if (!bbarg->getType().isAddress())
                           return true;
                         switch (paramInfo.getConvention()) {
                         default:
                           return false;
                         case ParameterConvention::Indirect_In:
                         case ParameterConvention::Indirect_In_Constant:
                         case ParameterConvention::Indirect_Inout:
                         case ParameterConvention::Indirect_InoutAliasable:
                         case ParameterConvention::Indirect_In_Guaranteed:
                           return true;
                         }
                       }),
            "entry point address argument must have an indirect calling "
            "convention");
  }

  void verifyEpilogBlocks(SILFunction *F) {
    bool FoundReturnBlock = false;
    bool FoundThrowBlock = false;
    for (auto &BB : *F) {
      if (isa<ReturnInst>(BB.getTerminator())) {
        require(!FoundReturnBlock,
                "more than one return block in function");
        FoundReturnBlock = true;
      }
      if (isa<ThrowInst>(BB.getTerminator())) {
        require(!FoundThrowBlock,
                "more than one throw block in function");
        FoundThrowBlock = true;
      }
    }
  }

  bool
  isUnreachableAlongAllPathsStartingAt(SILBasicBlock *StartBlock,
                                       SmallPtrSet<SILBasicBlock *, 16> &Visited) {
    if (isa<UnreachableInst>(StartBlock->getTerminator()))
      return true;
    else if (isa<ReturnInst>(StartBlock->getTerminator()))
      return false;
    else if (isa<ThrowInst>(StartBlock->getTerminator()))
      return false;

    // Recursively check all successors.
    for (auto *SuccBB : StartBlock->getSuccessorBlocks())
      if (!Visited.insert(SuccBB).second)
        if (!isUnreachableAlongAllPathsStartingAt(SuccBB, Visited))
          return false;

    return true;
  }

  void verifySILFunctionType(CanSILFunctionType FTy) {
    // Make sure that if FTy's calling convention implies that it must have a
    // self parameter.
    require(!FTy->hasSelfParam() || !FTy->getParameters().empty(),
            "Functions with a calling convention with self parameter must "
            "have at least one argument for self.");
  }

  /// Verify the various control-flow-sensitive rules of SIL:
  ///
  /// - stack allocations and deallocations must obey a stack discipline
  /// - accesses must be uniquely ended
  /// - flow-sensitive states must be equivalent on all paths into a block
  void verifyFlowSensitiveRules(SILFunction *F) {
    enum CFGState {
      /// No special rules are in play.
      Normal,
      /// We've followed the resume edge of a yield in a yield_once coroutine.
      YieldOnceResume,
      /// We've followed the unwind edge of a yield.
      YieldUnwind
    };
    struct BBState {
      std::vector<SingleValueInstruction*> Stack;

      /// Contents: BeginAccessInst*, BeginApplyInst*.
      std::set<SILInstruction*> ActiveOps;

      CFGState CFG = Normal;
    };

    // Do a breath-first search through the basic blocks.
    // Note that we intentionally don't verify these properties in blocks
    // that can't be reached from the entry block.
    llvm::DenseMap<SILBasicBlock*, BBState> visitedBBs;
    SmallVector<SILBasicBlock*, 16> Worklist;
    visitedBBs.try_emplace(&*F->begin());
    Worklist.push_back(&*F->begin());
    while (!Worklist.empty()) {
      SILBasicBlock *BB = Worklist.pop_back_val();
      BBState state = visitedBBs[BB];
      for (SILInstruction &i : *BB) {
        CurInstruction = &i;

        if (i.isAllocatingStack()) {
          state.Stack.push_back(cast<SingleValueInstruction>(&i));

        } else if (i.isDeallocatingStack()) {
          SILValue op = i.getOperand(0);
          require(!state.Stack.empty(),
                  "stack dealloc with empty stack");
          require(op == state.Stack.back(),
                  "stack dealloc does not match most recent stack alloc");
          state.Stack.pop_back();

        } else if (isa<BeginAccessInst>(i) || isa<BeginApplyInst>(i)) {
          bool notAlreadyPresent = state.ActiveOps.insert(&i).second;
          require(notAlreadyPresent,
                  "operation was not ended before re-beginning it");

        } else if (isa<EndAccessInst>(i) || isa<AbortApplyInst>(i) ||
                   isa<EndApplyInst>(i)) {
          if (auto beginOp = i.getOperand(0)->getDefiningInstruction()) {
            bool present = state.ActiveOps.erase(beginOp);
            require(present, "operation has already been ended");
          }

        } else if (auto term = dyn_cast<TermInst>(&i)) {
          if (term->isFunctionExiting()) {
            require(state.Stack.empty(),
                    "return with stack allocs that haven't been deallocated");
            require(state.ActiveOps.empty(),
                    "return with operations still active");

            if (isa<UnwindInst>(term)) {
              require(state.CFG == YieldUnwind,
                      "encountered 'unwind' when not on unwind path");
            } else {
              require(state.CFG != YieldUnwind,
                      "encountered 'return' or 'throw' when on unwind path");
              if (isa<ReturnInst>(term) &&
                  F->getLoweredFunctionType()->getCoroutineKind() ==
                    SILCoroutineKind::YieldOnce &&
                  F->getModule().getStage() != SILStage::Raw) {
                require(state.CFG == YieldOnceResume,
                        "encountered 'return' before yielding a value in "
                        "yield_once coroutine");
              }
            }
          }

          if (isa<YieldInst>(term)) {
            require(state.CFG != YieldOnceResume,
                    "encountered multiple 'yield's along single path");
            require(state.CFG == Normal,
                    "encountered 'yield' on abnormal CFG path");
          }

          auto successors = term->getSuccessors();
          for (auto i : indices(successors)) {
            SILBasicBlock *succBB = successors[i].getBB();

            // Optimistically try to set our current state as the state
            // of the successor.  We can use a move on the final successor;
            // note that if the insertion fails, the move won't actually
            // happen, which is important because we'll still need it
            // to compare against the already-recorded state for the block.
            auto insertResult =
              i + 1 == successors.size()
                ? visitedBBs.try_emplace(succBB, std::move(state))
                : visitedBBs.try_emplace(succBB, state);

            // If the insertion was successful, add the successor to the
            // worklist and continue.
            if (insertResult.second) {
              Worklist.push_back(succBB);

              // If we're following a 'yield', update the CFG state:
              if (isa<YieldInst>(term)) {
                // Enforce that the unwind logic is segregated in all stages.
                if (i == 1) {
                  insertResult.first->second.CFG = YieldUnwind;

                // We check the yield_once rule in the mandatory analyses,
                // so we can't assert it yet in the raw stage.
                } else if (F->getLoweredFunctionType()->getCoroutineKind()
                             == SILCoroutineKind::YieldOnce && 
                           F->getModule().getStage() != SILStage::Raw) {
                  insertResult.first->second.CFG = YieldOnceResume;
                }
              }

              continue;
            }

            // This rule is checked elsewhere, but we'd want to assert it
            // here anyway.
            require(!isa<YieldInst>(term),
                    "successor of 'yield' should not be encountered twice");

            // Check that the stack height is consistent coming from all entry
            // points into this BB. We only care about consistency if there is
            // a possible return from this function along the path starting at
            // this successor bb.  (FIXME: Why? Infinite loops should still
            // preserve consistency...)
            auto isUnreachable = [&] {
              SmallPtrSet<SILBasicBlock *, 16> visited;
              return isUnreachableAlongAllPathsStartingAt(succBB, visited);
            };
            
            const auto &foundState = insertResult.first->second;
            require(state.Stack == foundState.Stack || isUnreachable(),
                    "inconsistent stack heights entering basic block");
            require(state.ActiveOps == foundState.ActiveOps || isUnreachable(),
                    "inconsistent active-operations sets entering basic block");
            require(state.CFG == foundState.CFG,
                    "inconsistent coroutine states entering basic block");
          }
        }
      }
    }
  }

  void verifyBranches(SILFunction *F) {
    // Verify that there is no non_condbr critical edge.
    auto isCriticalEdgePred = [](const TermInst *T, unsigned EdgeIdx) {
      assert(T->getSuccessors().size() > EdgeIdx && "Not enough successors");

      // A critical edge has more than one outgoing edges from the source
      // block.
      auto SrcSuccs = T->getSuccessors();
      if (SrcSuccs.size() <= 1)
        return false;

      // And its destination block has more than one predecessor.
      SILBasicBlock *DestBB = SrcSuccs[EdgeIdx];
      assert(!DestBB->pred_empty() && "There should be a predecessor");
      if (DestBB->getSinglePredecessorBlock())
        return false;

      return true;
    };

    SILModule &M = F->getModule();
    for (auto &BB : *F) {
      TermInst *TI = BB.getTerminator();

      // Check for non-cond_br critical edges in canonical SIL.
      if (!isa<CondBranchInst>(TI) && M.getStage() == SILStage::Canonical) {
        for (unsigned Idx = 0, e = BB.getSuccessors().size(); Idx != e; ++Idx) {
          require(!isCriticalEdgePred(TI, Idx),
                  "non cond_br critical edges not allowed");
        }
        continue;
      }

      // In ownership qualified SIL, ban critical edges from CondBranchInst that
      // have non-trivial arguments.
      if (!F->hasQualifiedOwnership())
        continue;

      auto *CBI = dyn_cast<CondBranchInst>(TI);
      if (!CBI)
        continue;

      if (isCriticalEdgePred(CBI, CondBranchInst::TrueIdx)) {
        require(
            llvm::all_of(CBI->getTrueArgs(),
                         [](SILValue V) -> bool {
                           return V.getOwnershipKind() ==
                                  ValueOwnershipKind::Trivial;
                         }),
            "cond_br with critical edges must not have a non-trivial value");
      }
      if (isCriticalEdgePred(CBI, CondBranchInst::FalseIdx)) {
        require(
            llvm::all_of(CBI->getFalseArgs(),
                         [](SILValue V) -> bool {
                           return V.getOwnershipKind() ==
                                  ValueOwnershipKind::Trivial;
                         }),
            "cond_br with critical edges must not have a non-trivial value");
      }
    }
  }

  void verifyOpenedArchetypes(SILFunction *F) {
    require(OpenedArchetypes.getFunction() == F,
           "Wrong SILFunction provided to verifyOpenedArchetypes");
    // Check that definitions of all opened archetypes from
    // OpenedArchetypesDefs are existing instructions
    // belonging to the function F.
    for (auto KV: OpenedArchetypes.getOpenedArchetypeDefs()) {
      require(getOpenedArchetypeOf(CanType(KV.first)),
              "Only opened archetypes should be registered in SILFunction");
      auto Def = cast<SILInstruction>(KV.second);
      require(Def->getFunction() == F, 
              "Definition of every registered opened archetype should be an"
              " existing instruction in a current SILFunction");
    }
  }

  void visitSILBasicBlock(SILBasicBlock *BB) {
    // Make sure that each of the successors/predecessors of this basic block
    // have this basic block in its predecessor/successor list.
    for (const auto *SuccBB : BB->getSuccessorBlocks()) {
      bool FoundSelfInSuccessor = false;
      if (SuccBB->isPredecessorBlock(BB)) {
        FoundSelfInSuccessor = true;
        break;
      }
      require(FoundSelfInSuccessor, "Must be a predecessor of each successor.");
    }

    for (const SILBasicBlock *PredBB : BB->getPredecessorBlocks()) {
      bool FoundSelfInPredecessor = false;
      if (PredBB->isSuccessorBlock(BB)) {
        FoundSelfInPredecessor = true;
        break;
      }
      require(FoundSelfInPredecessor, "Must be a successor of each predecessor.");
    }
    
    SILInstructionVisitor::visitSILBasicBlock(BB);
  }

  void visitBasicBlockArguments(SILBasicBlock *BB) {
    for (auto argI = BB->args_begin(), argEnd = BB->args_end(); argI != argEnd;
         ++argI)
      visitSILArgument(*argI);
  }

  void visitSILBasicBlocks(SILFunction *F) {
    // Visit all basic blocks in the RPOT order.
    // This ensures that any open_existential instructions, which
    // open archetypes, are seen before the uses of these archetypes.
    llvm::ReversePostOrderTraversal<SILFunction *> RPOT(F);
    llvm::DenseSet<SILBasicBlock *> VisitedBBs;
    for (auto Iter = RPOT.begin(), E = RPOT.end(); Iter != E; ++Iter) {
      auto *BB = *Iter;
      VisitedBBs.insert(BB);
      visitSILBasicBlock(BB);
    }

    // Visit all basic blocks that were not visited during the RPOT traversal,
    // e.g. unreachable basic blocks.
    for (auto &BB : *F) {
      if (VisitedBBs.count(&BB))
        continue;
      visitSILBasicBlock(&BB);
    }
  }

  void visitSILFunction(SILFunction *F) {
    PrettyStackTraceSILFunction stackTrace("verifying", F);

    CanSILFunctionType FTy = F->getLoweredFunctionType();
    verifySILFunctionType(FTy);

    if (F->isExternalDeclaration()) {
      if (F->hasForeignBody())
        return;

      assert(F->isAvailableExternally() &&
             "external declaration of internal SILFunction not allowed");
      // If F is an external declaration, there is nothing further to do,
      // return.
      return;
    }

    assert(!F->hasForeignBody());

    // Make sure that our SILFunction only has context generic params if our
    // SILFunctionType is non-polymorphic.
    if (F->getGenericEnvironment()) {
      require(FTy->isPolymorphic(),
              "non-generic function definitions cannot have a "
              "generic environment");
    } else {
      require(!FTy->isPolymorphic(),
              "generic function definition must have a generic environment");
    }

    // Otherwise, verify the body of the function.
    verifyEntryBlock(&*F->getBlocks().begin());
    verifyEpilogBlocks(F);
    verifyFlowSensitiveRules(F);
    verifyBranches(F);

    visitSILBasicBlocks(F);

    // Verify archetypes after all basic blocks are visited,
    // because we build the map of archetypes as we visit the
    // instructions.
    verifyOpenedArchetypes(F);
  }

  void verify() {
    visitSILFunction(const_cast<SILFunction*>(&F));
  }
};
} // end anonymous namespace

#undef require
#undef requireObjectType

//===----------------------------------------------------------------------===//
//                     Out of Line Verifier Run Functions
//===----------------------------------------------------------------------===//

/// verify - Run the SIL verifier to make sure that the SILFunction follows
/// invariants.
void SILFunction::verify(bool SingleFunction) const {
#ifdef NDEBUG
  if (!getModule().getOptions().VerifyAll)
    return;
#endif
  // Please put all checks in visitSILFunction in SILVerifier, not here. This
  // ensures that the pretty stack trace in the verifier is included with the
  // back trace when the verifier crashes.
  SILVerifier(*this, SingleFunction).verify();
}

/// Verify that a vtable follows invariants.
void SILVTable::verify(const SILModule &M) const {
#ifdef NDEBUG
  if (!M.getOptions().VerifyAll)
    return;
#endif
  for (auto &entry : getEntries()) {
    // All vtable entries must be decls in a class context.
    assert(entry.Method.hasDecl() && "vtable entry is not a decl");
    auto baseInfo = M.Types.getConstantInfo(entry.Method);
    ValueDecl *decl = entry.Method.getDecl();

    assert((!isa<FuncDecl>(decl)
            || !cast<FuncDecl>(decl)->isObservingAccessor())
           && "observing accessors shouldn't have vtable entries");

    // For ivar destroyers, the decl is the class itself.
    ClassDecl *theClass;
    if (entry.Method.kind == SILDeclRef::Kind::IVarDestroyer)
      theClass = dyn_cast<ClassDecl>(decl);
    else
      theClass = dyn_cast<ClassDecl>(decl->getDeclContext());

    assert(theClass && "vtable entry must refer to a class member");

    // The class context must be the vtable's class, or a superclass thereof.
    auto c = getClass();
    do {
      if (c == theClass)
        break;
      if (auto ty = c->getSuperclass())
        c = ty->getClassOrBoundGenericClass();
      else
        c = nullptr;
    } while (c);
    assert(c && "vtable entry must refer to a member of the vtable's class");

    // All function vtable entries must be at their natural uncurry level.
    assert(!entry.Method.isCurried && "vtable entry must not be curried");

    // Foreign entry points shouldn't appear in vtables.
    assert(!entry.Method.isForeign && "vtable entry must not be foreign");
    
    // The vtable entry must be ABI-compatible with the overridden vtable slot.
    SmallString<32> baseName;
    {
      llvm::raw_svector_ostream os(baseName);
      entry.Method.print(os);
    }

    if (M.getStage() != SILStage::Lowered) {
      SILVerifier(*entry.Implementation)
          .requireABICompatibleFunctionTypes(
              baseInfo.getSILType().castTo<SILFunctionType>(),
              entry.Implementation->getLoweredFunctionType(),
              "vtable entry for " + baseName + " must be ABI-compatible");
    }
  }
}

/// Verify that a witness table follows invariants.
void SILWitnessTable::verify(const SILModule &M) const {
#ifdef NDEBUG
  if (!M.getOptions().VerifyAll)
    return;
#endif
  if (isDeclaration())
    assert(getEntries().size() == 0 &&
           "A witness table declaration should not have any entries.");

  auto *protocol = getConformance()->getProtocol();

  for (const Entry &E : getEntries())
    if (E.getKind() == SILWitnessTable::WitnessKind::Method) {
      SILFunction *F = E.getMethodWitness().Witness;
      if (F) {
        // If a SILWitnessTable is going to be serialized, it must only
        // reference public or serializable functions.
        if (isSerialized()) {
          assert((!isLessVisibleThan(F->getLinkage(), getLinkage()) ||
                  (F->isSerialized() &&
                   hasSharedVisibility(F->getLinkage()))) &&
                 "Fragile witness tables should not reference "
                 "less visible functions.");
        }

        assert(F->getLoweredFunctionType()->getRepresentation() ==
               SILFunctionTypeRepresentation::WitnessMethod &&
               "Witnesses must have witness_method representation.");
        auto *witnessSelfProtocol = F->getLoweredFunctionType()
            ->getDefaultWitnessMethodProtocol();
        assert((witnessSelfProtocol == nullptr ||
                witnessSelfProtocol == protocol) &&
               "Witnesses must either have a concrete Self, or an "
               "an abstract Self that is constrained to their "
               "protocol.");
      }
    }
}

/// Verify that a default witness table follows invariants.
void SILDefaultWitnessTable::verify(const SILModule &M) const {
#ifndef NDEBUG
  for (const Entry &E : getEntries()) {
    if (!E.isValid())
      continue;

    SILFunction *F = E.getWitness();
    // FIXME
    #if 0
    assert(!isLessVisibleThan(F->getLinkage(), getLinkage()) &&
           "Default witness tables should not reference "
           "less visible functions.");
    #endif
    assert(F->getLoweredFunctionType()->getRepresentation() ==
           SILFunctionTypeRepresentation::WitnessMethod &&
           "Default witnesses must have witness_method representation.");
    auto *witnessSelfProtocol = F->getLoweredFunctionType()
        ->getDefaultWitnessMethodProtocol();
    assert(witnessSelfProtocol == getProtocol() &&
           "Default witnesses must have an abstract Self parameter "
           "constrained to their protocol.");
  }
#endif
}

/// Verify that a global variable follows invariants.
void SILGlobalVariable::verify() const {
#ifdef NDEBUG
  if (!getModule().getOptions().VerifyAll)
    return;
#endif
  assert(getLoweredType().isObject()
         && "global variable cannot have address type");

  // Verify the static initializer.
  for (const SILInstruction &I : StaticInitializerBlock) {
    assert(isValidStaticInitializerInst(&I, getModule()) &&
           "illegal static initializer");
    auto init = cast<SingleValueInstruction>(&I);
    if (init == &StaticInitializerBlock.back()) {
      assert(init->use_empty() && "Init value must not have another use");
    } else {
      assert(!init->use_empty() && "dead instruction in static initializer");
      assert(!isa<ObjectInst>(init) &&
             "object instruction is only allowed for final initial value");
    }
    assert(I.getParent() == &StaticInitializerBlock);
  }
}

/// Verify the module.
void SILModule::verify() const {
#ifdef NDEBUG
  if (!getOptions().VerifyAll)
    return;
#endif
  // Uniquing set to catch symbol name collisions.
  llvm::StringSet<> symbolNames;

  // When merging partial modules, we only link functions from the current
  // module, without enabling "LinkAll" mode or running the SILLinker pass;
  // in this case, we need to relax some of the checks.
  bool SingleFunction = false;
  if (getOptions().MergePartialModules)
    SingleFunction = true;

  // Check all functions.
  for (const SILFunction &f : *this) {
    if (!symbolNames.insert(f.getName()).second) {
      llvm::errs() << "Symbol redefined: " << f.getName() << "!\n";
      assert(false && "triggering standard assertion failure routine");
    }
    f.verify(SingleFunction);
  }

  // Check all globals.
  for (const SILGlobalVariable &g : getSILGlobals()) {
    if (!symbolNames.insert(g.getName()).second) {
      llvm::errs() << "Symbol redefined: " << g.getName() << "!\n";
      assert(false && "triggering standard assertion failure routine");
    }
    g.verify();
  }

  // Check all vtables and the vtable cache.
  llvm::DenseSet<ClassDecl*> vtableClasses;
  unsigned EntriesSZ = 0;
  for (const SILVTable &vt : getVTables()) {
    if (!vtableClasses.insert(vt.getClass()).second) {
      llvm::errs() << "Vtable redefined: " << vt.getClass()->getName() << "!\n";
      assert(false && "triggering standard assertion failure routine");
    }
    vt.verify(*this);
    // Check if there is a cache entry for each vtable entry
    for (auto entry : vt.getEntries()) {
      if (VTableEntryCache.find({&vt, entry.Method}) == VTableEntryCache.end()) {
        llvm::errs() << "Vtable entry for function: "
                     << entry.Implementation->getName() << "not in cache!\n";
        assert(false && "triggering standard assertion failure routine");
      }
      EntriesSZ++;
    }
  }
  assert(EntriesSZ == VTableEntryCache.size() &&
         "Cache size is not equal to true number of VTable entries");

  // Check all witness tables.
  DEBUG(llvm::dbgs() << "*** Checking witness tables for duplicates ***\n");
  llvm::DenseSet<NormalProtocolConformance*> wtableConformances;
  for (const SILWitnessTable &wt : getWitnessTables()) {
    DEBUG(llvm::dbgs() << "Witness Table:\n"; wt.dump());
    auto conformance = wt.getConformance();
    if (!wtableConformances.insert(conformance).second) {
      llvm::errs() << "Witness table redefined: ";
      conformance->printName(llvm::errs());
      assert(false && "triggering standard assertion failure routine");
    }
    wt.verify(*this);
  }

  // Check all default witness tables.
  DEBUG(llvm::dbgs() << "*** Checking default witness tables for duplicates ***\n");
  llvm::DenseSet<const ProtocolDecl *> defaultWitnessTables;
  for (const SILDefaultWitnessTable &wt : getDefaultWitnessTables()) {
    DEBUG(llvm::dbgs() << "Default Witness Table:\n"; wt.dump());
    if (!defaultWitnessTables.insert(wt.getProtocol()).second) {
      llvm::errs() << "Default witness table redefined: ";
      wt.dump();
      assert(false && "triggering standard assertion failure routine");
    }
    wt.verify(*this);
  }
}

/// Determine whether an instruction may not have a SILDebugScope.
bool swift::maybeScopeless(SILInstruction &I) {
  if (I.getFunction()->isBare())
    return true;
  return !isa<DebugValueInst>(I) && !isa<DebugValueAddrInst>(I);
}
