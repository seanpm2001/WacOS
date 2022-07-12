//===--- SILOwnershipVerifier.cpp -----------------------------------------===//
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

#define DEBUG_TYPE "sil-ownership-verifier"

#include "UseOwnershipRequirement.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/AnyFunctionRef.h"
#include "swift/AST/Decl.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Module.h"
#include "swift/AST/Types.h"
#include "swift/Basic/Range.h"
#include "swift/Basic/STLExtras.h"
#include "swift/Basic/TransformArrayRef.h"
#include "swift/ClangImporter/ClangModule.h"
#include "swift/SIL/BasicBlockUtils.h"
#include "swift/SIL/Dominance.h"
#include "swift/SIL/DynamicCasts.h"
#include "swift/SIL/OwnershipChecker.h"
#include "swift/SIL/PrettyStackTrace.h"
#include "swift/SIL/Projection.h"
#include "swift/SIL/SILBuiltinVisitor.h"
#include "swift/SIL/SILDebugScope.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILOpenedArchetypesTracker.h"
#include "swift/SIL/SILVTable.h"
#include "swift/SIL/SILVisitor.h"
#include "swift/SIL/TypeLowering.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include <algorithm>

using namespace swift;

// This is an option to put the SILOwnershipVerifier in testing mode. This
// causes the following:
//
// 1. Instead of printing an error message and aborting, the verifier will print
// the message and continue. This allows for FileCheck testing of the verifier.
//
// 2. SILInstruction::verifyOperandOwnership() is disabled. This is used for
// verification in SILBuilder. This causes errors to be printed twice, once when
// we build the IR and a second time when we perform a full verification of the
// IR. For testing purposes, we just want the later.
llvm::cl::opt<bool> IsSILOwnershipVerifierTestingEnabled(
    "sil-ownership-verifier-enable-testing",
    llvm::cl::desc("Put the sil ownership verifier in testing mode. See "
                   "comment in SILOwnershipVerifier.cpp above option for more "
                   "information."));

//===----------------------------------------------------------------------===//
//                              Generalized User
//===----------------------------------------------------------------------===//

namespace {

/// This is a class that models normal users and also cond_br users that are
/// associated with the block in the target block. This is safe to do since in
/// Semantic SIL, cond_br with non-trivial arguments are not allowed to have
/// critical edges.
class GeneralizedUser {
  using InnerTy = llvm::PointerIntPair<SILInstruction *, 1>;
  InnerTy User;

public:
  GeneralizedUser(SILInstruction *I) : User(I) {
    assert(!isa<CondBranchInst>(I));
  }

  GeneralizedUser(CondBranchInst *I) : User(I) {}

  GeneralizedUser(CondBranchInst *I, unsigned SuccessorIndex)
      : User(I, SuccessorIndex) {
    assert(SuccessorIndex == CondBranchInst::TrueIdx ||
           SuccessorIndex == CondBranchInst::FalseIdx);
  }

  GeneralizedUser(const GeneralizedUser &Other) : User(Other.User) {}
  GeneralizedUser &operator=(const GeneralizedUser &Other) {
    User = Other.User;
    return *this;
  }

  operator SILInstruction *() { return User.getPointer(); }
  operator const SILInstruction *() const { return User.getPointer(); }

  SILInstruction *getInst() const { return User.getPointer(); }

  SILBasicBlock *getParent() const;

  bool isCondBranchUser() const {
    return isa<CondBranchInst>(User.getPointer());
  }

  unsigned getCondBranchSuccessorID() const {
    assert(isCondBranchUser());
    return User.getInt();
  }

  SILBasicBlock::iterator getIterator() const {
    return User.getPointer()->getIterator();
  }

  void *getAsOpaqueValue() const {
    return llvm::PointerLikeTypeTraits<InnerTy>::getAsVoidPointer(User);
  }

  static GeneralizedUser getFromOpaqueValue(void *p) {
    InnerTy TmpUser =
        llvm::PointerLikeTypeTraits<InnerTy>::getFromVoidPointer(p);
    if (auto *CBI = dyn_cast<CondBranchInst>(TmpUser.getPointer())) {
      return GeneralizedUser(CBI, TmpUser.getInt());
    }
    return GeneralizedUser(TmpUser.getPointer());
  }

  enum {
    NumLowBitsAvailable =
        llvm::PointerLikeTypeTraits<InnerTy>::NumLowBitsAvailable
  };
};

} // end anonymous namespace

SILBasicBlock *GeneralizedUser::getParent() const {
  if (!isCondBranchUser()) {
    return getInst()->getParent();
  }

  auto *CBI = cast<CondBranchInst>(getInst());
  unsigned Number = getCondBranchSuccessorID();
  if (Number == CondBranchInst::TrueIdx)
    return CBI->getTrueBB();
  return CBI->getFalseBB();
}

namespace llvm {

template <> class PointerLikeTypeTraits<GeneralizedUser> {

public:
  static void *getAsVoidPointer(GeneralizedUser v) {
    return v.getAsOpaqueValue();
  }

  static GeneralizedUser getFromVoidPointer(void *p) {
    return GeneralizedUser::getFromOpaqueValue(p);
  }

  enum { NumLowBitsAvailable = GeneralizedUser::NumLowBitsAvailable };
};

} // namespace llvm

//===----------------------------------------------------------------------===//
//                                  Utility
//===----------------------------------------------------------------------===//

static bool compatibleOwnershipKinds(ValueOwnershipKind K1,
                                     ValueOwnershipKind K2) {
  return K1.merge(K2).hasValue();
}

/// Returns true if \p Kind is trivial or if \p Kind is compatible with \p
/// ComparisonKind.
static bool
trivialOrCompatibleOwnershipKinds(ValueOwnershipKind Kind,
                                  ValueOwnershipKind ComparisonKind) {
  return compatibleOwnershipKinds(Kind, ValueOwnershipKind::Trivial) ||
         compatibleOwnershipKinds(Kind, ComparisonKind);
}

static bool isValueAddressOrTrivial(SILValue V, SILModule &M) {
  return V->getType().isAddress() ||
         V.getOwnershipKind() == ValueOwnershipKind::Trivial ||
         V.getOwnershipKind() == ValueOwnershipKind::Any;
}

static bool isUnsafeGuaranteedBuiltin(SILInstruction *I) {
  auto *BI = dyn_cast<BuiltinInst>(I);
  if (!BI)
    return false;
  auto BuiltinKind = BI->getBuiltinKind();
  return BuiltinKind == BuiltinValueKind::UnsafeGuaranteed;
}

// These operations forward both owned and guaranteed ownership.
static bool isOwnershipForwardingValueKind(SILNodeKind K) {
  switch (K) {
  case SILNodeKind::TupleInst:
  case SILNodeKind::StructInst:
  case SILNodeKind::EnumInst:
  case SILNodeKind::OpenExistentialRefInst:
  case SILNodeKind::UpcastInst:
  case SILNodeKind::UncheckedRefCastInst:
  case SILNodeKind::ConvertFunctionInst:
  case SILNodeKind::RefToBridgeObjectInst:
  case SILNodeKind::BridgeObjectToRefInst:
  case SILNodeKind::UnconditionalCheckedCastInst:
  case SILNodeKind::UncheckedEnumDataInst:
  case SILNodeKind::MarkUninitializedInst:
  case SILNodeKind::SelectEnumInst:
  case SILNodeKind::SwitchEnumInst:
  case SILNodeKind::CheckedCastBranchInst:
  case SILNodeKind::DestructureStructInst:
  case SILNodeKind::DestructureTupleInst:
    return true;
  default:
    return false;
  }
}

// These operations forward guaranteed ownership, but don't necessarily forward
// owned values.
static bool isGuaranteedForwardingValueKind(SILNodeKind K) {
  switch (K) {
  case SILNodeKind::TupleExtractInst:
  case SILNodeKind::StructExtractInst:
  case SILNodeKind::OpenExistentialValueInst:
  case SILNodeKind::OpenExistentialBoxValueInst:
    return true;
  default:
    return isOwnershipForwardingValueKind(K);
  }
}

static bool isGuaranteedForwardingValue(SILValue V) {
  if (auto *SVI = dyn_cast<SingleValueInstruction>(V))
    if (isUnsafeGuaranteedBuiltin(SVI))
      return true;
  return isGuaranteedForwardingValueKind(
      V->getKindOfRepresentativeSILNodeInObject());
}

static bool isGuaranteedForwardingInst(SILInstruction *I) {
  if (isUnsafeGuaranteedBuiltin(I))
    return true;
  return isGuaranteedForwardingValueKind(SILNodeKind(I->getKind()));
}

static bool isOwnershipForwardingInst(SILInstruction *I) {
  if (isUnsafeGuaranteedBuiltin(I))
    return true;
  return isOwnershipForwardingValueKind(SILNodeKind(I->getKind()));
}

//===----------------------------------------------------------------------===//
//                      OwnershipCompatibilityUseChecker
//===----------------------------------------------------------------------===//

namespace {

struct ErrorBehaviorKind {
  enum inner_t {
    Invalid = 0,
    ReturnFalse = 1,
    PrintMessage = 2,
    Assert = 4,
    PrintMessageAndReturnFalse = PrintMessage | ReturnFalse,
    PrintMessageAndAssert = PrintMessage | Assert,
  } Value;

  ErrorBehaviorKind() : Value(Invalid) {}
  ErrorBehaviorKind(inner_t Inner) : Value(Inner) { assert(Value != Invalid); }

  bool shouldAssert() const {
    assert(Value != Invalid);
    return Value & Assert;
  }

  bool shouldPrintMessage() const {
    assert(Value != Invalid);
    return Value & PrintMessage;
  }

  bool shouldReturnFalse() const {
    assert(Value != Invalid);
    return Value & ReturnFalse;
  }
};

struct OwnershipUseCheckerResult {
  bool HasCompatibleOwnership;
  bool ShouldCheckForDataflowViolations;

  OwnershipUseCheckerResult(bool HasCompatibleOwnership,
                            UseLifetimeConstraint OwnershipRequirement)
      : HasCompatibleOwnership(HasCompatibleOwnership),
        ShouldCheckForDataflowViolations(bool(OwnershipRequirement)) {}
};

class OwnershipCompatibilityUseChecker
    : public SILInstructionVisitor<OwnershipCompatibilityUseChecker,
                                   OwnershipUseCheckerResult> {
public:
private:
  SILModule &Mod;
  const Operand &Op;
  SILValue BaseValue;
  ErrorBehaviorKind ErrorBehavior;

public:
  /// Create a new OwnershipCompatibilityUseChecker.
  ///
  /// In most cases, one should only pass in \p Op and \p BaseValue will be set
  /// to Op.get(). In cases where one is trying to verify subobjects, Op.get()
  /// should be the subobject and Value should be the parent object. An example
  /// of where one would want to do this is in the case of value projections
  /// like struct_extract.
  OwnershipCompatibilityUseChecker(SILModule &M, const Operand &Op,
                                   SILValue BaseValue,
                                   ErrorBehaviorKind ErrorBehavior)
      : Mod(M), Op(Op), BaseValue(BaseValue), ErrorBehavior(ErrorBehavior) {
    assert((BaseValue == Op.get() ||
            BaseValue.getOwnershipKind() == ValueOwnershipKind::Guaranteed) &&
           "Guaranteed values are the only values allowed to have subobject");
    // We only support subobjects on objects.
    assert((BaseValue->getType().isObject() || !isCheckingSubObject()) &&
           "Checking a subobject, but do not have an object base value?!");
  }

  bool isCheckingSubObject() const { return Op.get() != BaseValue; }

  SILValue getValue() const { return Op.get(); }

  ValueOwnershipKind getOwnershipKind() const {
    assert(getValue().getOwnershipKind() == Op.get().getOwnershipKind() &&
           "Expected ownership kind of parent value and operand");
    return getValue().getOwnershipKind();
  }

  unsigned getOperandIndex() const { return Op.getOperandNumber(); }

  SILType getType() const { return Op.get()->getType(); }

  bool compatibleWithOwnership(ValueOwnershipKind Kind) const {
    return compatibleOwnershipKinds(getOwnershipKind(), Kind);
  }

  bool hasExactOwnership(ValueOwnershipKind Kind) const {
    return getOwnershipKind() == Kind;
  }

  bool isAddressOrTrivialType() const {
    if (getType().isAddress())
      return true;
    return getOwnershipKind() == ValueOwnershipKind::Trivial ||
      getOwnershipKind() == ValueOwnershipKind::Any;
  }

  /// Depending on our initialization, either return false or call Func and
  /// throw an error.
  bool handleError(llvm::function_ref<void()> &&MessagePrinterFunc) const {
    if (ErrorBehavior.shouldPrintMessage()) {
      MessagePrinterFunc();
    }

    if (ErrorBehavior.shouldReturnFalse()) {
      return false;
    }

    assert(ErrorBehavior.shouldAssert() && "At this point, we should assert");
    llvm_unreachable("triggering standard assertion failure routine");
  }

  OwnershipUseCheckerResult visitForwardingInst(SILInstruction *I,
                                                ArrayRef<Operand> Ops);
  OwnershipUseCheckerResult visitForwardingInst(SILInstruction *I) {
    return visitForwardingInst(I, I->getAllOperands());
  }

  /// Visit a terminator instance that performs a transform like
  /// operation. E.x.: switch_enum, checked_cast_br. This does not include br or
  /// cond_br.
  OwnershipUseCheckerResult visitTransformingTerminatorInst(TermInst *TI);

  OwnershipUseCheckerResult
  visitEnumArgument(EnumDecl *E, ValueOwnershipKind RequiredConvention);
  OwnershipUseCheckerResult
  visitApplyParameter(ValueOwnershipKind RequiredConvention,
                      UseLifetimeConstraint Requirement);
  OwnershipUseCheckerResult
  visitFullApply(FullApplySite apply);

  /// Check if \p User as compatible ownership with the SILValue that we are
  /// checking.
  ///
  /// \returns true if the user is a use that must be checked for dataflow
  /// violations.
  bool check(SILInstruction *User) {
    auto Result = visit(User);
    if (!Result.HasCompatibleOwnership) {
      return handleError([&]() {
        llvm::errs() << "Function: '" << User->getFunction()->getName() << "'\n"
                     << "Have operand with incompatible ownership?!\n"
                     << "Value: " << *getValue() << "BaseValue: " << *BaseValue
                     << "User: " << *User << "Conv: " << getOwnershipKind()
                     << "\n\n";
      });
    }

    assert((!Result.ShouldCheckForDataflowViolations ||
            !isAddressOrTrivialType()) &&
           "Address or trivial types should never be checked for dataflow "
           "violations");

    return Result.ShouldCheckForDataflowViolations;
  }

  OwnershipUseCheckerResult visitCallee(CanSILFunctionType SubstCalleeType);
  OwnershipUseCheckerResult
  checkTerminatorArgumentMatchesDestBB(SILBasicBlock *DestBB, unsigned OpIndex);

// Create declarations for all instructions, so we get a warning at compile
// time if any instructions do not have an implementation.
#define INST(Id, Parent) \
  OwnershipUseCheckerResult visit##Id(Id *);
#include "swift/SIL/SILNodes.def"
};

} // end anonymous namespace

/// Implementation for instructions without operands. These should never be
/// visited.
#define NO_OPERAND_INST(INST)                                                  \
  OwnershipUseCheckerResult                                                    \
      OwnershipCompatibilityUseChecker::visit##INST##Inst(INST##Inst *I) {     \
    assert(I->getNumOperands() == 0 &&                                         \
           "Expected instruction without operands?!");                         \
    llvm_unreachable("Instruction without operand can not be compatible with " \
                     "any def's OwnershipValueKind");                          \
  }
NO_OPERAND_INST(AllocBox)
NO_OPERAND_INST(AllocExistentialBox)
NO_OPERAND_INST(AllocGlobal)
NO_OPERAND_INST(AllocStack)
NO_OPERAND_INST(FloatLiteral)
NO_OPERAND_INST(FunctionRef)
NO_OPERAND_INST(GlobalAddr)
NO_OPERAND_INST(GlobalValue)
NO_OPERAND_INST(IntegerLiteral)
NO_OPERAND_INST(Metatype)
NO_OPERAND_INST(ObjCProtocol)
NO_OPERAND_INST(RetainValue)
NO_OPERAND_INST(RetainValueAddr)
NO_OPERAND_INST(StringLiteral)
NO_OPERAND_INST(ConstStringLiteral)
NO_OPERAND_INST(StrongRetain)
NO_OPERAND_INST(StrongRetainUnowned)
NO_OPERAND_INST(UnownedRetain)
NO_OPERAND_INST(Unreachable)
NO_OPERAND_INST(Unwind)
#undef NO_OPERAND_INST

/// Instructions whose arguments are always compatible with one convention.
#define CONSTANT_OWNERSHIP_INST(OWNERSHIP, USE_LIFETIME_CONSTRAINT, INST)      \
  OwnershipUseCheckerResult                                                    \
      OwnershipCompatibilityUseChecker::visit##INST##Inst(INST##Inst *I) {     \
    assert(I->getNumOperands() && "Expected to have non-zero operands");       \
    if (ValueOwnershipKind::OWNERSHIP == ValueOwnershipKind::Trivial) {        \
      assert(isAddressOrTrivialType() &&                                       \
             "Trivial ownership requires a trivial type or an address");       \
    }                                                                          \
                                                                               \
    return {compatibleWithOwnership(ValueOwnershipKind::OWNERSHIP),            \
            UseLifetimeConstraint::USE_LIFETIME_CONSTRAINT};                   \
  }
CONSTANT_OWNERSHIP_INST(Guaranteed, MustBeLive, RefElementAddr)
CONSTANT_OWNERSHIP_INST(Guaranteed, MustBeLive, OpenExistentialValue)
CONSTANT_OWNERSHIP_INST(Guaranteed, MustBeLive, OpenExistentialBoxValue)
CONSTANT_OWNERSHIP_INST(Owned, MustBeInvalidated, AutoreleaseValue)
CONSTANT_OWNERSHIP_INST(Owned, MustBeInvalidated, DeallocBox)
CONSTANT_OWNERSHIP_INST(Owned, MustBeInvalidated, DeallocExistentialBox)
CONSTANT_OWNERSHIP_INST(Owned, MustBeInvalidated, DeallocRef)
CONSTANT_OWNERSHIP_INST(Owned, MustBeInvalidated, DestroyValue)
CONSTANT_OWNERSHIP_INST(Owned, MustBeInvalidated, ReleaseValue)
CONSTANT_OWNERSHIP_INST(Owned, MustBeInvalidated, ReleaseValueAddr)
CONSTANT_OWNERSHIP_INST(Owned, MustBeInvalidated, StrongRelease)
CONSTANT_OWNERSHIP_INST(Owned, MustBeInvalidated, StrongUnpin)
CONSTANT_OWNERSHIP_INST(Owned, MustBeInvalidated, UnownedRelease)
CONSTANT_OWNERSHIP_INST(Owned, MustBeInvalidated, InitExistentialRef)
CONSTANT_OWNERSHIP_INST(Owned, MustBeInvalidated, EndLifetime)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, AbortApply)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, AddressToPointer)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, BeginAccess)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, BeginUnpairedAccess)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, BindMemory)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, CheckedCastAddrBranch)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, CondFail)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, CopyAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, DeallocStack)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, DebugValueAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, DeinitExistentialAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, DestroyAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, EndAccess)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, EndApply)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, EndUnpairedAccess)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, IndexAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, IndexRawPointer)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, InitBlockStorageHeader)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, InitEnumDataAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, InitExistentialAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, InitExistentialMetatype)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, InjectEnumAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, IsUnique)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, IsUniqueOrPinned)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, Load)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, LoadBorrow)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, LoadUnowned)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, LoadWeak)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, MarkFunctionEscape)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, MarkUninitializedBehavior)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, ObjCExistentialMetatypeToObject)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, ObjCMetatypeToObject)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, ObjCToThickMetatype)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, OpenExistentialAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, OpenExistentialMetatype)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, PointerToAddress)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, PointerToThinFunction)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, ProjectBlockStorage)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, ProjectValueBuffer)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, RawPointerToRef)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, SelectEnumAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, SelectValue)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, StructElementAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, SwitchEnumAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, SwitchValue)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, TailAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, ThickToObjCMetatype)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, ThinFunctionToPointer)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, ThinToThickFunction)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, TupleElementAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, UncheckedAddrCast)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, UncheckedRefCastAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, UncheckedTakeEnumDataAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, UnconditionalCheckedCastAddr)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, UnmanagedToRef)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, AllocValueBuffer)
CONSTANT_OWNERSHIP_INST(Trivial, MustBeLive, DeallocValueBuffer)
#undef CONSTANT_OWNERSHIP_INST

/// Instructions whose arguments are always compatible with one convention.
#define CONSTANT_OR_TRIVIAL_OWNERSHIP_INST(OWNERSHIP, USE_LIFETIME_CONSTRAINT, \
                                           INST)                               \
  OwnershipUseCheckerResult                                                    \
      OwnershipCompatibilityUseChecker::visit##INST##Inst(INST##Inst *I) {     \
    assert(I->getNumOperands() && "Expected to have non-zero operands");       \
    if (ValueOwnershipKind::OWNERSHIP == ValueOwnershipKind::Trivial) {        \
      assert(isAddressOrTrivialType() &&                                       \
             "Trivial ownership requires a trivial type or an address");       \
    }                                                                          \
                                                                               \
    if (compatibleWithOwnership(ValueOwnershipKind::Trivial)) {                \
      return {true, UseLifetimeConstraint::MustBeLive};                        \
    }                                                                          \
    return {compatibleWithOwnership(ValueOwnershipKind::OWNERSHIP),            \
            UseLifetimeConstraint::USE_LIFETIME_CONSTRAINT};                   \
  }
CONSTANT_OR_TRIVIAL_OWNERSHIP_INST(Owned, MustBeInvalidated,
                                   CheckedCastValueBranch)
CONSTANT_OR_TRIVIAL_OWNERSHIP_INST(Owned, MustBeInvalidated,
                                   UnconditionalCheckedCastValue)
CONSTANT_OR_TRIVIAL_OWNERSHIP_INST(Owned, MustBeInvalidated,
                                   InitExistentialValue)
CONSTANT_OR_TRIVIAL_OWNERSHIP_INST(Owned, MustBeInvalidated,
                                   DeinitExistentialValue)
#undef CONSTANT_OR_TRIVIAL_OWNERSHIP_INST

#define ACCEPTS_ANY_OWNERSHIP_INST(INST)                                       \
  OwnershipUseCheckerResult                                                    \
      OwnershipCompatibilityUseChecker::visit##INST##Inst(INST##Inst *I) {     \
    return {true, UseLifetimeConstraint::MustBeLive};                          \
  }
ACCEPTS_ANY_OWNERSHIP_INST(BeginBorrow)
ACCEPTS_ANY_OWNERSHIP_INST(CopyValue)
ACCEPTS_ANY_OWNERSHIP_INST(DebugValue)
ACCEPTS_ANY_OWNERSHIP_INST(FixLifetime)
ACCEPTS_ANY_OWNERSHIP_INST(UncheckedBitwiseCast) // Is this right?
ACCEPTS_ANY_OWNERSHIP_INST(WitnessMethod)        // Is this right?
ACCEPTS_ANY_OWNERSHIP_INST(ProjectBox)           // The result is a T*.
ACCEPTS_ANY_OWNERSHIP_INST(DynamicMethodBranch)
ACCEPTS_ANY_OWNERSHIP_INST(UncheckedTrivialBitCast)
ACCEPTS_ANY_OWNERSHIP_INST(ExistentialMetatype)
ACCEPTS_ANY_OWNERSHIP_INST(ValueMetatype)
ACCEPTS_ANY_OWNERSHIP_INST(UncheckedOwnershipConversion)
#undef ACCEPTS_ANY_OWNERSHIP_INST

// Trivial if trivial typed, otherwise must accept owned?
#define ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP_OR_METATYPE(USE_LIFETIME_CONSTRAINT,  \
                                                     INST)                     \
  OwnershipUseCheckerResult                                                    \
      OwnershipCompatibilityUseChecker::visit##INST##Inst(INST##Inst *I) {     \
    assert(I->getNumOperands() && "Expected to have non-zero operands");       \
    if (getType().is<AnyMetatypeType>()) {                                     \
      return {true, UseLifetimeConstraint::MustBeLive};                        \
    }                                                                          \
    bool compatible = hasExactOwnership(ValueOwnershipKind::Any) ||            \
                      !compatibleWithOwnership(ValueOwnershipKind::Trivial);   \
    return {compatible, UseLifetimeConstraint::USE_LIFETIME_CONSTRAINT};       \
  }
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP_OR_METATYPE(MustBeLive, ClassMethod)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP_OR_METATYPE(MustBeLive, ObjCMethod)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP_OR_METATYPE(MustBeLive, ObjCSuperMethod)
#undef ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP_OR_METATYPE

// Trivial if trivial typed, otherwise must accept owned?
#define ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(USE_LIFETIME_CONSTRAINT, INST)        \
  OwnershipUseCheckerResult                                                    \
      OwnershipCompatibilityUseChecker::visit##INST##Inst(INST##Inst *I) {     \
    assert(I->getNumOperands() && "Expected to have non-zero operands");       \
    bool compatible = hasExactOwnership(ValueOwnershipKind::Any) ||            \
                      !compatibleWithOwnership(ValueOwnershipKind::Trivial);   \
    return {compatible, UseLifetimeConstraint::USE_LIFETIME_CONSTRAINT};       \
  }
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, SuperMethod)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, BridgeObjectToWord)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, CopyBlock)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, OpenExistentialBox)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, RefTailAddr)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, RefToRawPointer)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, RefToUnmanaged)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, RefToUnowned)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, SetDeallocating)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, StrongPin)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, UnownedToRef)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, CopyUnownedValue)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, ProjectExistentialBox)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, UnmanagedRetainValue)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, UnmanagedReleaseValue)
ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP(MustBeLive, UnmanagedAutoreleaseValue)
#undef ACCEPTS_ANY_NONTRIVIAL_OWNERSHIP

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitForwardingInst(SILInstruction *I, ArrayRef<Operand> Ops) {
  assert(I->getNumOperands() && "Expected to have non-zero operands");
  assert(isOwnershipForwardingInst(I) &&
         "Expected to have an ownership forwarding inst");

  // Find the first index where we have a trivial value.
  auto Iter = find_if(Ops, [&I](const Operand &Op) -> bool {
    if (I->isTypeDependentOperand(Op))
      return false;
    return Op.get().getOwnershipKind() != ValueOwnershipKind::Trivial;
  });

  // All trivial.
  if (Iter == Ops.end()) {
    return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
            UseLifetimeConstraint::MustBeLive};
  }

  unsigned Index = std::distance(Ops.begin(), Iter);
  ValueOwnershipKind Base = Ops[Index].get().getOwnershipKind();

  for (const Operand &Op : Ops.slice(Index + 1)) {
    if (I->isTypeDependentOperand(Op))
      continue;
    auto OpKind = Op.get().getOwnershipKind();
    if (OpKind.merge(ValueOwnershipKind::Trivial))
      continue;

    auto MergedValue = Base.merge(OpKind.Value);
    if (!MergedValue.hasValue()) {
      return {false, UseLifetimeConstraint::MustBeInvalidated};
    }
    Base = MergedValue.getValue();
  }

  // We only need to treat a forwarded instruction as a lifetime ending use of
  // it is owned.
  auto lifetimeConstraint = hasExactOwnership(ValueOwnershipKind::Owned)
                                ? UseLifetimeConstraint::MustBeInvalidated
                                : UseLifetimeConstraint::MustBeLive;
  return {true, lifetimeConstraint};
}

#define FORWARD_ANY_OWNERSHIP_INST(INST)                                       \
  OwnershipUseCheckerResult                                                    \
      OwnershipCompatibilityUseChecker::visit##INST##Inst(INST##Inst *I) {     \
    return visitForwardingInst(I);                                             \
  }
FORWARD_ANY_OWNERSHIP_INST(Tuple)
FORWARD_ANY_OWNERSHIP_INST(Struct)
FORWARD_ANY_OWNERSHIP_INST(Object)
FORWARD_ANY_OWNERSHIP_INST(Enum)
FORWARD_ANY_OWNERSHIP_INST(OpenExistentialRef)
FORWARD_ANY_OWNERSHIP_INST(Upcast)
FORWARD_ANY_OWNERSHIP_INST(UncheckedRefCast)
FORWARD_ANY_OWNERSHIP_INST(ConvertFunction)
FORWARD_ANY_OWNERSHIP_INST(RefToBridgeObject)
FORWARD_ANY_OWNERSHIP_INST(BridgeObjectToRef)
FORWARD_ANY_OWNERSHIP_INST(UnconditionalCheckedCast)
FORWARD_ANY_OWNERSHIP_INST(MarkUninitialized)
FORWARD_ANY_OWNERSHIP_INST(UncheckedEnumData)
FORWARD_ANY_OWNERSHIP_INST(DestructureStruct)
FORWARD_ANY_OWNERSHIP_INST(DestructureTuple)
#undef FORWARD_ANY_OWNERSHIP_INST

// An instruction that forwards a constant ownership or trivial ownership.
#define FORWARD_CONSTANT_OR_TRIVIAL_OWNERSHIP_INST(                            \
    OWNERSHIP, USE_LIFETIME_CONSTRAINT, INST)                                  \
  OwnershipUseCheckerResult                                                    \
      OwnershipCompatibilityUseChecker::visit##INST##Inst(INST##Inst *I) {     \
    assert(I->getNumOperands() && "Expected to have non-zero operands");       \
    assert(isGuaranteedForwardingInst(I) &&                                    \
           "Expected an ownership forwarding inst");                           \
    if (ValueOwnershipKind::OWNERSHIP != ValueOwnershipKind::Trivial &&        \
        hasExactOwnership(ValueOwnershipKind::Trivial)) {                      \
      assert(isAddressOrTrivialType() &&                                       \
             "Trivial ownership requires a trivial type or an address");       \
      return {true, UseLifetimeConstraint::MustBeLive};                        \
    }                                                                          \
    if (ValueOwnershipKind::OWNERSHIP == ValueOwnershipKind::Trivial) {        \
      assert(isAddressOrTrivialType() &&                                       \
             "Trivial ownership requires a trivial type or an address");       \
    }                                                                          \
                                                                               \
    return {compatibleWithOwnership(ValueOwnershipKind::OWNERSHIP),            \
            UseLifetimeConstraint::USE_LIFETIME_CONSTRAINT};                   \
  }
FORWARD_CONSTANT_OR_TRIVIAL_OWNERSHIP_INST(Guaranteed, MustBeLive, TupleExtract)
FORWARD_CONSTANT_OR_TRIVIAL_OWNERSHIP_INST(Guaranteed, MustBeLive,
                                           StructExtract)
#undef CONSTANT_OR_TRIVIAL_OWNERSHIP_INST

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitDeallocPartialRefInst(
    DeallocPartialRefInst *I) {
  if (getValue() == I->getInstance()) {
    return {compatibleWithOwnership(ValueOwnershipKind::Owned),
            UseLifetimeConstraint::MustBeInvalidated};
  }

  return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
          UseLifetimeConstraint::MustBeLive};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitEndBorrowArgumentInst(
    EndBorrowArgumentInst *I) {
  // If we are currently checking an end_borrow_argument as a subobject, then we
  // treat this as just a use.
  if (isCheckingSubObject())
    return {true, UseLifetimeConstraint::MustBeLive};

  // Otherwise, we must be checking an actual argument. Make sure it is guaranteed!
  auto lifetimeConstraint = hasExactOwnership(ValueOwnershipKind::Guaranteed)
                                ? UseLifetimeConstraint::MustBeInvalidated
                                : UseLifetimeConstraint::MustBeLive;
  return {true, lifetimeConstraint};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitSelectEnumInst(SelectEnumInst *I) {
  if (getValue() == I->getEnumOperand()) {
    return {true, UseLifetimeConstraint::MustBeLive};
  }

  return visitForwardingInst(I, I->getAllOperands().drop_front());
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitAllocRefInst(AllocRefInst *I) {
  assert(I->getNumOperands() != 0
         && "If we reach this point, we must have a tail operand");
  return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
          UseLifetimeConstraint::MustBeLive};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitAllocRefDynamicInst(
    AllocRefDynamicInst *I) {
  assert(I->getNumOperands() != 0 &&
         "If we reach this point, we must have a tail operand");
  return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
          UseLifetimeConstraint::MustBeLive};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::checkTerminatorArgumentMatchesDestBB(
    SILBasicBlock *DestBB, unsigned OpIndex) {
  // Grab the ownership kind of the destination block.
  ValueOwnershipKind DestBlockArgOwnershipKind =
      DestBB->getArgument(OpIndex)->getOwnershipKind();

  // Then if we do not have an enum, make sure that the conventions match.
  EnumDecl *E = getType().getEnumOrBoundGenericEnum();
  if (!E) {
    bool matches = compatibleWithOwnership(DestBlockArgOwnershipKind);
    auto lifetimeConstraint = hasExactOwnership(ValueOwnershipKind::Owned)
                                  ? UseLifetimeConstraint::MustBeInvalidated
                                  : UseLifetimeConstraint::MustBeLive;
    return {matches, lifetimeConstraint};
  }

  return visitEnumArgument(E, DestBlockArgOwnershipKind);
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitBranchInst(BranchInst *BI) {
  return checkTerminatorArgumentMatchesDestBB(BI->getDestBB(),
                                              getOperandIndex());
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitCondBranchInst(CondBranchInst *CBI) {
  // If our conditional branch is the condition, it is trivial. Check that the
  // ownership kind is trivial.
  if (CBI->isConditionOperandIndex(getOperandIndex()))
    return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
            UseLifetimeConstraint::MustBeLive};

  // Otherwise, make sure that our operand matches the
  if (CBI->isTrueOperandIndex(getOperandIndex())) {
    unsigned TrueOffset = 1;
    return checkTerminatorArgumentMatchesDestBB(CBI->getTrueBB(),
                                                getOperandIndex() - TrueOffset);
  }

  assert(CBI->isFalseOperandIndex(getOperandIndex()) &&
         "If an operand is not the condition index or a true operand index, it "
         "must be a false operand index");
  unsigned FalseOffset = 1 + CBI->getTrueOperands().size();
  return checkTerminatorArgumentMatchesDestBB(CBI->getFalseBB(),
                                              getOperandIndex() - FalseOffset);
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitSwitchEnumInst(SwitchEnumInst *SEI) {
  return visitTransformingTerminatorInst(SEI);
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitCheckedCastBranchInst(
    CheckedCastBranchInst *SEI) {
  return visitTransformingTerminatorInst(SEI);
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitTransformingTerminatorInst(
    TermInst *TI) {
  // If our operand was trivial, return early.
  if (compatibleWithOwnership(ValueOwnershipKind::Trivial))
    return {true, UseLifetimeConstraint::MustBeLive};

  // Then we need to go through all of our destinations and make sure that if
  // they have a payload, the payload's convention matches our
  // convention.
  //
  // *NOTE* we assume that all of our types line up and are checked by the
  // normal verifier.
  for (auto *Succ : TI->getParent()->getSuccessorBlocks()) {
    // This must be a no-payload case... continue.
    if (Succ->args_size() == 0)
      continue;

    // If we have a trivial value or a value with ownership kind that matches
    // the switch_enum, then continue.
    auto OwnershipKind = Succ->getArgument(0)->getOwnershipKind();
    if (OwnershipKind == ValueOwnershipKind::Trivial ||
        compatibleWithOwnership(OwnershipKind))
      continue;

    // Otherwise, emit an error.
    handleError([&]() {
      llvm::errs()
          << "Function: '" << Succ->getParent()->getName() << "'\n"
          << "Error! Argument ownership kind does not match terminator!\n"
          << "Terminator: " << *TI << "Argument: " << *Succ->getArgument(0)
          << "Expected convention: " << getOwnershipKind() << ".\n"
          << "Actual convention:   " << OwnershipKind << '\n'
          << '\n';
    });
  }

  // Finally, if everything lines up, emit that we match and are a lifetime
  // ending point if we are owned.
  auto lifetimeConstraint = hasExactOwnership(ValueOwnershipKind::Owned)
                                ? UseLifetimeConstraint::MustBeInvalidated
                                : UseLifetimeConstraint::MustBeLive;
  return {true, lifetimeConstraint};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitReturnInst(ReturnInst *RI) {
  SILModule &M = RI->getModule();
  bool IsTrivial = RI->getOperand()->getType().isTrivial(M);
  SILFunctionConventions fnConv = RI->getFunction()->getConventions();
  auto Results = fnConv.getDirectSILResults();
  if (Results.empty() || IsTrivial) {
    return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
            UseLifetimeConstraint::MustBeLive};
  }

  CanGenericSignature Sig = fnConv.funcTy->getGenericSignature();

  // Find the first index where we have a trivial value.
  auto Iter = find_if(Results, [&M, &Sig](const SILResultInfo &Info) -> bool {
    return Info.getOwnershipKind(M, Sig) != ValueOwnershipKind::Trivial;
  });

  // If we have all trivial, then we must be trivial. Why wasn't our original
  // type trivial? This is a hard error since this is a logic error in our code
  // here.
  if (Iter == Results.end())
    llvm_unreachable("Should have already checked a trivial type?!");

  ValueOwnershipKind Base = Iter->getOwnershipKind(M, Sig);

  for (const SILResultInfo &ResultInfo :
       SILFunctionConventions::DirectSILResultRange(std::next(Iter),
                                                    Results.end())) {
    auto RKind = ResultInfo.getOwnershipKind(M, Sig);
    // Ignore trivial types.
    if (RKind.merge(ValueOwnershipKind::Trivial))
      continue;

    auto MergedValue = Base.merge(RKind);
    // If we fail to merge all types in, bail. We can not come up with a proper
    // result type.
    if (!MergedValue.hasValue()) {
      return {false, UseLifetimeConstraint::MustBeLive};
    }
    // In case Base is Any.
    Base = MergedValue.getValue();
  }

  if (auto *E = getType().getEnumOrBoundGenericEnum()) {
    return visitEnumArgument(E, Base);
  }

  return {compatibleWithOwnership(Base),
          UseLifetimeConstraint::MustBeInvalidated};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitEndBorrowInst(EndBorrowInst *I) {
  // We do not consider the original value to be a verified use. But the value
  // does need to be alive.
  if (getOperandIndex() == EndBorrowInst::OriginalValue)
    return {true, UseLifetimeConstraint::MustBeLive};
  // The borrowed value is a verified use though of the begin_borrow.
  return {compatibleWithOwnership(ValueOwnershipKind::Guaranteed),
          UseLifetimeConstraint::MustBeInvalidated};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitThrowInst(ThrowInst *I) {
  return {compatibleWithOwnership(ValueOwnershipKind::Owned),
          UseLifetimeConstraint::MustBeInvalidated};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitStoreUnownedInst(StoreUnownedInst *I) {
  if (getValue() == I->getSrc())
    return {compatibleWithOwnership(ValueOwnershipKind::Owned),
            UseLifetimeConstraint::MustBeInvalidated};
  return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
          UseLifetimeConstraint::MustBeLive};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitStoreWeakInst(StoreWeakInst *I) {
  // A store_weak instruction implies that the value to be stored to be live,
  // but it does not touch the strong reference count of the value.
  if (getValue() == I->getSrc())
    return {true, UseLifetimeConstraint::MustBeLive};
  return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
          UseLifetimeConstraint::MustBeLive};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitStoreBorrowInst(StoreBorrowInst *I) {
  if (getValue() == I->getSrc())
    return {compatibleWithOwnership(ValueOwnershipKind::Guaranteed),
            UseLifetimeConstraint::MustBeLive};
  return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
          UseLifetimeConstraint::MustBeLive};
}

// FIXME: Why not use SILArgumentConvention here?
OwnershipUseCheckerResult OwnershipCompatibilityUseChecker::visitCallee(
    CanSILFunctionType SubstCalleeType) {
  ParameterConvention Conv = SubstCalleeType->getCalleeConvention();
  switch (Conv) {
  case ParameterConvention::Indirect_In:
  case ParameterConvention::Indirect_In_Constant:
    assert(!SILModuleConventions(Mod).isSILIndirect(
        SILParameterInfo(SubstCalleeType, Conv)));
    return {compatibleWithOwnership(ValueOwnershipKind::Owned),
            UseLifetimeConstraint::MustBeInvalidated};
  case ParameterConvention::Indirect_In_Guaranteed:
    assert(!SILModuleConventions(Mod).isSILIndirect(
        SILParameterInfo(SubstCalleeType, Conv)));
    return {compatibleWithOwnership(ValueOwnershipKind::Owned),
            UseLifetimeConstraint::MustBeLive};
  case ParameterConvention::Indirect_Inout:
  case ParameterConvention::Indirect_InoutAliasable:
    llvm_unreachable("Illegal convention for callee");
  case ParameterConvention::Direct_Unowned:
    if (isAddressOrTrivialType())
      return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
              UseLifetimeConstraint::MustBeLive};
    // We accept unowned, owned, and guaranteed in unowned positions.
    return {true, UseLifetimeConstraint::MustBeLive};
  case ParameterConvention::Direct_Owned:
    return {compatibleWithOwnership(ValueOwnershipKind::Owned),
            UseLifetimeConstraint::MustBeInvalidated};
  case ParameterConvention::Direct_Guaranteed:
    return {compatibleWithOwnership(ValueOwnershipKind::Guaranteed),
            UseLifetimeConstraint::MustBeLive};
  }

  llvm_unreachable("Unhandled ParameterConvention in switch.");
}

// Visit an enum value that is passed at argument position, including block
// arguments, apply arguments, and return values.
//
// The operand definition's ownership kind may be known to be "trivial",
// but it is still valid to pass that enum to a argument nontrivial type.
// For example:
//
// %val = enum $Optional<SomeClass>, #Optional.none // trivial ownership
// apply %f(%val) : (@owned Optional<SomeClass>)    // owned argument
OwnershipUseCheckerResult OwnershipCompatibilityUseChecker::visitEnumArgument(
    EnumDecl *E, ValueOwnershipKind RequiredKind) {
  // If this value is already categorized as a trivial ownership kind, it is
  // safe to pass to any argument convention.
  if (compatibleWithOwnership(ValueOwnershipKind::Trivial)) {
    return {true, UseLifetimeConstraint::MustBeLive};
  }

  // The operand has a non-trivial ownership kind. It must match the argument
  // convention.
  auto ownership = getOwnershipKind();
  auto lifetimeConstraint = (ownership == ValueOwnershipKind::Owned)
                                ? UseLifetimeConstraint::MustBeInvalidated
                                : UseLifetimeConstraint::MustBeLive;
  return {compatibleOwnershipKinds(ownership, RequiredKind),
          lifetimeConstraint};
}

// We allow for trivial cases of enums with non-trivial cases to be passed in
// non-trivial argument positions. This fits with modeling of a
// SILFunctionArgument as a phi in a global program graph.
OwnershipUseCheckerResult OwnershipCompatibilityUseChecker::visitApplyParameter(
    ValueOwnershipKind Kind, UseLifetimeConstraint Requirement) {
  // Check if we have an enum. If not, then we just check against the passed in
  // convention.
  EnumDecl *E = getType().getEnumOrBoundGenericEnum();
  if (!E) {
    return {compatibleWithOwnership(Kind), Requirement};
  }
  return visitEnumArgument(E, Kind);
}

// Handle Apply and TryApply.
OwnershipUseCheckerResult OwnershipCompatibilityUseChecker::
visitFullApply(FullApplySite apply) {
  // If we are visiting the callee, handle it specially.
  if (getOperandIndex() == 0)
    return visitCallee(apply.getSubstCalleeType());

  // Indirect return arguments are address types.
  if (isAddressOrTrivialType())
    return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
            UseLifetimeConstraint::MustBeLive};

  unsigned argIndex = apply.getCalleeArgIndex(Op);
  SILParameterInfo paramInfo =
    apply.getSubstCalleeConv().getParamInfoForSILArg(argIndex);

  switch (paramInfo.getConvention()) {
  case ParameterConvention::Indirect_In:
  case ParameterConvention::Direct_Owned:
    return visitApplyParameter(ValueOwnershipKind::Owned,
                               UseLifetimeConstraint::MustBeInvalidated);
  case ParameterConvention::Indirect_In_Constant:
  case ParameterConvention::Direct_Unowned:
    // We accept unowned, owned, and guaranteed in unowned positions.
    return {true, UseLifetimeConstraint::MustBeLive};
  case ParameterConvention::Indirect_In_Guaranteed:
  case ParameterConvention::Direct_Guaranteed:
    return visitApplyParameter(ValueOwnershipKind::Guaranteed,
                               UseLifetimeConstraint::MustBeLive);
  // The following conventions should take address types.
  case ParameterConvention::Indirect_Inout:
  case ParameterConvention::Indirect_InoutAliasable:
    llvm_unreachable("Unexpected non-trivial parameter convention.");
  }
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitBeginApplyInst(BeginApplyInst *I) {
  return visitFullApply(I);
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitApplyInst(ApplyInst *I) {
  return visitFullApply(I);
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitTryApplyInst(TryApplyInst *I) {
  return visitFullApply(I);
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitPartialApplyInst(PartialApplyInst *I) {
  // All non-trivial types should be captured.
  if (isAddressOrTrivialType()) {
    return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
            UseLifetimeConstraint::MustBeLive};
  }
  return {compatibleWithOwnership(ValueOwnershipKind::Owned),
          UseLifetimeConstraint::MustBeInvalidated};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitYieldInst(YieldInst *I) {
  // Indirect return arguments are address types.
  if (isAddressOrTrivialType())
    return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
            UseLifetimeConstraint::MustBeLive};

  auto fnType = I->getFunction()->getLoweredFunctionType();
  auto yieldInfo = fnType->getYields()[getOperandIndex()];
  switch (yieldInfo.getConvention()) {
  case ParameterConvention::Indirect_In:
  case ParameterConvention::Direct_Owned:
    return visitApplyParameter(ValueOwnershipKind::Owned,
                               UseLifetimeConstraint::MustBeInvalidated);
  case ParameterConvention::Indirect_In_Constant:
  case ParameterConvention::Direct_Unowned:
    // We accept unowned, owned, and guaranteed in unowned positions.
    return {true, UseLifetimeConstraint::MustBeLive};
  case ParameterConvention::Indirect_In_Guaranteed:
  case ParameterConvention::Direct_Guaranteed:
    return visitApplyParameter(ValueOwnershipKind::Guaranteed,
                               UseLifetimeConstraint::MustBeLive);
  // The following conventions should take address types.
  case ParameterConvention::Indirect_Inout:
  case ParameterConvention::Indirect_InoutAliasable:
    llvm_unreachable("Unexpected non-trivial parameter convention.");
  }
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitAssignInst(AssignInst *I) {
  if (getValue() == I->getSrc()) {
    if (isAddressOrTrivialType()) {
      return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
              UseLifetimeConstraint::MustBeLive};
    }
    return {compatibleWithOwnership(ValueOwnershipKind::Owned),
            UseLifetimeConstraint::MustBeInvalidated};
  }

  return {true, UseLifetimeConstraint::MustBeLive};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitStoreInst(StoreInst *I) {
  if (getValue() == I->getSrc()) {
    if (isAddressOrTrivialType()) {
      return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
              UseLifetimeConstraint::MustBeLive};
    }
    return {compatibleWithOwnership(ValueOwnershipKind::Owned),
            UseLifetimeConstraint::MustBeInvalidated};
  }
  return {true, UseLifetimeConstraint::MustBeLive};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitMarkDependenceInst(
    MarkDependenceInst *MDI) {
  // We always treat mark dependence as a use that keeps a value alive. We will
  // be introducing a begin_dependence/end_dependence version of this later.
  return {true, UseLifetimeConstraint::MustBeLive};
}

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitKeyPathInst(KeyPathInst *I) {
  // KeyPath moves the value in memory out of address operands, but the
  // ownership checker doesn't reason about that yet.
  if (isAddressOrTrivialType()) {
    return {compatibleWithOwnership(ValueOwnershipKind::Trivial),
            UseLifetimeConstraint::MustBeLive};
  }
  return {compatibleWithOwnership(ValueOwnershipKind::Owned),
          UseLifetimeConstraint::MustBeInvalidated};
}

//===----------------------------------------------------------------------===//
//                            Builtin Use Checker
//===----------------------------------------------------------------------===//

namespace {

class OwnershipCompatibilityBuiltinUseChecker
    : public SILBuiltinVisitor<OwnershipCompatibilityBuiltinUseChecker,
                               OwnershipUseCheckerResult> {

  const OwnershipCompatibilityUseChecker &ParentChecker;

public:
  OwnershipCompatibilityBuiltinUseChecker(
      OwnershipCompatibilityUseChecker &ParentChecker)
      : ParentChecker(ParentChecker) {}

  SILValue getValue() const { return ParentChecker.getValue(); }

  ValueOwnershipKind getOwnershipKind() const {
    return ParentChecker.getOwnershipKind();
  }

  unsigned getOperandIndex() const { return ParentChecker.getOperandIndex(); }

  SILType getType() const { return ParentChecker.getType(); }

  bool compatibleWithOwnership(ValueOwnershipKind Kind) const {
    return ParentChecker.compatibleWithOwnership(Kind);
  }

  bool isAddressOrTrivialType() const {
    return ParentChecker.isAddressOrTrivialType();
  }

  OwnershipUseCheckerResult visitLLVMIntrinsic(BuiltinInst *BI,
                                               llvm::Intrinsic::ID ID) {
    // LLVM intrinsics do not traffic in ownership, so if we have a result, it
    // must be trivial.
    return {true, UseLifetimeConstraint::MustBeLive};
  }

    // BUILTIN_TYPE_CHECKER_OPERATION does not live past the type checker.
#define BUILTIN_TYPE_CHECKER_OPERATION(ID, NAME)

#define BUILTIN(ID, NAME, ATTRS)                                               \
  OwnershipUseCheckerResult visit##ID(BuiltinInst *BI, StringRef Attr);
#include "swift/AST/Builtins.def"

  OwnershipUseCheckerResult check(BuiltinInst *BI) { return visit(BI); }
};

} // end anonymous namespace

OwnershipUseCheckerResult
OwnershipCompatibilityBuiltinUseChecker::visitUnsafeGuaranteed(BuiltinInst *BI,
                                                               StringRef Attr) {
  // We accept owned or guaranteed values here.
  if (compatibleWithOwnership(ValueOwnershipKind::Guaranteed))
    return {true, UseLifetimeConstraint::MustBeLive};
  return {compatibleWithOwnership(ValueOwnershipKind::Owned),
          UseLifetimeConstraint::MustBeInvalidated};
}

// This is correct today since we do not have any builtins which return
// @guaranteed parameters. This means that we can only have a lifetime ending
// use with our builtins if it is owned.
#define CONSTANT_OWNERSHIP_BUILTIN(OWNERSHIP, USE_LIFETIME_CONSTRAINT, ID)     \
  OwnershipUseCheckerResult                                                    \
      OwnershipCompatibilityBuiltinUseChecker::visit##ID(BuiltinInst *BI,      \
                                                         StringRef Attr) {     \
    return {compatibleWithOwnership(ValueOwnershipKind::OWNERSHIP),            \
            UseLifetimeConstraint::USE_LIFETIME_CONSTRAINT};                   \
  }
CONSTANT_OWNERSHIP_BUILTIN(Owned, MustBeLive, ErrorInMain)
CONSTANT_OWNERSHIP_BUILTIN(Owned, MustBeLive, UnexpectedError)
CONSTANT_OWNERSHIP_BUILTIN(Owned, MustBeLive, WillThrow)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, AShr)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Add)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Alignof)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, AllocRaw)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, And)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, AssertConf)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, AssignCopyArrayNoAlias)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, AssignCopyArrayFrontToBack)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, AssignCopyArrayBackToFront)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, AssignTakeArray)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, AssumeNonNegative)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, AtomicLoad)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, AtomicRMW)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, AtomicStore)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, BitCast)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, CanBeObjCClass)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, CmpXChg)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, CondUnreachable)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, CopyArray)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, DeallocRaw)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, DestroyArray)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ExactSDiv)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ExactUDiv)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ExtractElement)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FAdd)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_OEQ)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_OGE)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_OGT)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_OLE)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_OLT)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_ONE)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_ORD)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_UEQ)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_UGE)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_UGT)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_ULE)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_ULT)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_UNE)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FCMP_UNO)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FDiv)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FMul)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FNeg)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FPExt)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FPToSI)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FPToUI)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FPTrunc)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FRem)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, FSub)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Fence)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, GetObjCTypeEncoding)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ICMP_EQ)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ICMP_NE)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ICMP_SGE)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ICMP_SGT)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ICMP_SLE)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ICMP_SLT)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ICMP_UGE)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ICMP_UGT)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ICMP_ULE)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ICMP_ULT)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, InsertElement)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, IntToFPWithOverflow)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, IntToPtr)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, IsOptionalType)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, IsPOD)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, IsSameMetatype)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, LShr)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Mul)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, OnFastPath)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Once)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, OnceWithContext)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Or)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, PtrToInt)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, SAddOver)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, SDiv)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, SExt)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, SExtOrBitCast)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, SIToFP)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, SMulOver)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, SRem)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, SSubOver)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, SToSCheckedTrunc)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, SToUCheckedTrunc)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, SUCheckedConversion)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Shl)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Sizeof)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, StaticReport)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Strideof)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Sub)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, TakeArrayNoAlias)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, TakeArrayBackToFront)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, TakeArrayFrontToBack)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Trunc)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, TruncOrBitCast)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, TSanInoutAccess)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, UAddOver)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, UDiv)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, UIToFP)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, UMulOver)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, URem)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, USCheckedConversion)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, USubOver)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, UToSCheckedTrunc)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, UToUCheckedTrunc)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Unreachable)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, UnsafeGuaranteedEnd)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Xor)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ZExt)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ZExtOrBitCast)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, ZeroInitializer)
CONSTANT_OWNERSHIP_BUILTIN(Trivial, MustBeLive, Swift3ImplicitObjCEntrypoint)
#undef CONSTANT_OWNERSHIP_BUILTIN

// Builtins that should be lowered to SIL instructions so we should never see
// them.
#define BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(ID)                 \
  OwnershipUseCheckerResult                                                    \
      OwnershipCompatibilityBuiltinUseChecker::visit##ID(BuiltinInst *BI,      \
                                                         StringRef Attr) {     \
    llvm_unreachable("Builtin should have been lowered to SIL instruction?!"); \
  }
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(Retain)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(Release)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(Autorelease)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(TryPin)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(Unpin)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(Load)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(LoadRaw)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(LoadInvariant)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(Take)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(Destroy)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(Assign)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(Init)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(CastToNativeObject)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(UnsafeCastToNativeObject)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(CastFromNativeObject)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(CastToBridgeObject)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(
    CastReferenceFromBridgeObject)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(
    CastBitPatternFromBridgeObject)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(BridgeToRawPointer)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(BridgeFromRawPointer)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(CastReference)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(ReinterpretCast)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(AddressOf)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(GepRaw)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(Gep)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(GetTailAddr)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(CondFail)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(FixLifetime)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(IsUnique)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(IsUniqueOrPinned)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(IsUnique_native)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(IsUniqueOrPinned_native)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(BindMemory)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(AllocWithTailElems)
BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS(ProjectTailElems)
#undef BUILTINS_THAT_SHOULD_HAVE_BEEN_LOWERED_TO_SILINSTS

OwnershipUseCheckerResult
OwnershipCompatibilityUseChecker::visitBuiltinInst(BuiltinInst *BI) {
  return OwnershipCompatibilityBuiltinUseChecker(*this).check(BI);
}

//===----------------------------------------------------------------------===//
//                         SILValueOwnershipChecker
//===----------------------------------------------------------------------===//

namespace {

// TODO: This class uses a bunch of global state like variables. It should be
// refactored into a large state object that is used by functions.
class SILValueOwnershipChecker {
  /// The result of performing the check.
  llvm::Optional<bool> Result;

  /// The module that we are in.
  SILModule &Mod;

  /// A cache of dead-end basic blocks that we use to determine if we can
  /// ignore "leaks".
  DeadEndBlocks &DEBlocks;

  /// The value whose ownership we will check.
  SILValue Value;

  /// The action that the checker should perform on detecting an error.
  ErrorBehaviorKind ErrorBehavior;

  /// The worklist that we will use for our iterative reachability query.
  llvm::SmallVector<SILBasicBlock *, 32> Worklist;

  /// The set of blocks with lifetime ending uses.
  llvm::SmallPtrSet<SILBasicBlock *, 8> BlocksWithLifetimeEndingUses;

  /// The set of blocks with non-lifetime ending uses and the associated
  /// non-lifetime ending use SILInstruction.
  llvm::SmallDenseMap<SILBasicBlock *, GeneralizedUser, 8>
      BlocksWithNonLifetimeEndingUses;

  /// The blocks that we have already visited.
  llvm::SmallPtrSet<SILBasicBlock *, 32> &VisitedBlocks;

  /// A list of successor blocks that we must visit by the time the algorithm
  /// terminates.
  llvm::SmallPtrSet<SILBasicBlock *, 8> SuccessorBlocksThatMustBeVisited;

  /// The list of lifetime ending users that we found. Only valid if check is
  /// successful.
  llvm::SmallVector<GeneralizedUser, 16> LifetimeEndingUsers;

  /// The list of non lifetime ending users that we found. Only valid if check
  /// is successful.
  llvm::SmallVector<GeneralizedUser, 16> RegularUsers;

public:
  SILValueOwnershipChecker(
      SILModule &M, DeadEndBlocks &DEBlocks, SILValue V,
      ErrorBehaviorKind ErrorBehavior,
      llvm::SmallPtrSet<SILBasicBlock *, 32> &VisitedBlocks)
      : Result(), Mod(M), DEBlocks(DEBlocks), Value(V),
        ErrorBehavior(ErrorBehavior),
        VisitedBlocks(VisitedBlocks) {
    assert(Value && "Can not initialize a checker with an empty SILValue");
  }

  ~SILValueOwnershipChecker() = default;
  SILValueOwnershipChecker(SILValueOwnershipChecker &) = delete;
  SILValueOwnershipChecker(SILValueOwnershipChecker &&) = delete;

  bool check() {
    if (Result.hasValue())
      return Result.getValue();

    DEBUG(llvm::dbgs() << "Verifying ownership of: " << *Value);
    Result = checkUses() && checkDataflow();

    return Result.getValue();
  }

  using user_array_transform = std::function<SILInstruction *(GeneralizedUser)>;
  using user_array = TransformArrayRef<user_array_transform>;

  /// A function that returns a range of lifetime ending users found for the
  /// given value.
  user_array getLifetimeEndingUsers() const {
    assert(Result.hasValue() && "Can not call until check() is called");
    assert(Result.getValue() && "Can not call if check() returned false");

    user_array_transform Transform(
        [](GeneralizedUser User) -> SILInstruction * {
          return User.getInst();
        });
    return user_array(ArrayRef<GeneralizedUser>(LifetimeEndingUsers),
                      Transform);
  }

  /// A function that returns a range of regular (i.e. "non lifetime ending")
  /// users found for the given value.
  user_array getRegularUsers() const {
    assert(Result.hasValue() && "Can not call until check() is called");
    assert(Result.getValue() && "Can not call if check() returned false");

    user_array_transform Transform(
        [](GeneralizedUser User) -> SILInstruction * {
          return User.getInst();
        });
    return user_array(ArrayRef<GeneralizedUser>(RegularUsers), Transform);
  }

private:
  bool checkUses();
  bool checkDataflow();
  void checkDataflowEndConditions();
  void
  gatherUsers(llvm::SmallVectorImpl<GeneralizedUser> &LifetimeEndingUsers,
              llvm::SmallVectorImpl<GeneralizedUser> &NonLifetimeEndingUsers);
  void uniqueNonLifetimeEndingUsers(
      ArrayRef<GeneralizedUser> NonLifetimeEndingUsers);

  /// Returns true if the given block is in the BlocksWithLifetimeEndingUses
  /// set. This is a helper to extract out large logging messages so that the
  /// main logic is easy to read.
  bool doesBlockDoubleConsume(
      SILBasicBlock *UserBlock,
      llvm::Optional<GeneralizedUser> LifetimeEndingUser = None,
      bool ShouldInsert = false);

  /// Returns true if the given block contains a non-lifetime ending use that is
  /// strictly later in the block than a lifetime ending use. If all
  /// non-lifetime ending uses are before the lifetime ending use, the block is
  /// removed from the BlocksWithNonLifetimeEndingUses map to show that the uses
  /// were found to properly be post-dominated by a lifetime ending use.
  bool doesBlockContainUseAfterFree(GeneralizedUser LifetimeEndingUser,
                                    SILBasicBlock *UserBlock);

  bool checkValueWithoutLifetimeEndingUses();

  bool checkFunctionArgWithoutLifetimeEndingUses(SILFunctionArgument *Arg);

  bool isGuaranteedFunctionArgWithLifetimeEndingUses(
      SILFunctionArgument *Arg,
      const llvm::SmallVectorImpl<GeneralizedUser> &LifetimeEndingUsers) const;
  bool isSubobjectProjectionWithLifetimeEndingUses(
      SILValue Value,
      const llvm::SmallVectorImpl<GeneralizedUser> &LifetimeEndingUsers) const;

  /// Depending on our initialization, either return false or call Func and
  /// throw an error.
  bool handleError(llvm::function_ref<void()> &&MessagePrinterFunc) const {
    if (ErrorBehavior.shouldPrintMessage()) {
      MessagePrinterFunc();
    }

    if (ErrorBehavior.shouldReturnFalse()) {
      return false;
    }

    assert(ErrorBehavior.shouldAssert() && "At this point, we should assert");
    llvm_unreachable("triggering standard assertion failure routine");
  }
};

} // end anonymous namespace

bool SILValueOwnershipChecker::doesBlockContainUseAfterFree(
    GeneralizedUser LifetimeEndingUser, SILBasicBlock *UserBlock) {
  auto Iter = BlocksWithNonLifetimeEndingUses.find(UserBlock);
  if (Iter == BlocksWithNonLifetimeEndingUses.end())
    return false;

  GeneralizedUser NonLifetimeEndingUser = Iter->second;

  // Make sure that the non-lifetime ending use is before the lifetime ending
  // use. Otherwise, we have a use after free.

  // First check if our lifetime ending user is a cond_br. In such a case, we
  // always consider the non-lifetime ending use to be a use after free.
  if (LifetimeEndingUser.isCondBranchUser()) {
    return !handleError([&]() {
      llvm::errs() << "Function: '" << Value->getFunction()->getName() << "'\n"
                   << "Found use after free?!\n"
                   << "Value: " << *Value
                   << "Consuming User: " << *LifetimeEndingUser
                   << "Non Consuming User: " << *Iter->second << "Block: bb"
                   << UserBlock->getDebugID() << "\n\n";
    });
  }

  // Ok. At this point, we know that our lifetime ending user is not a cond
  // branch user. Check if our non-lifetime ending use is. In such a case, we
  // know that our non lifetime ending user is properly post-dominated so we can
  // erase the non lifetime ending use and continue.
  if (NonLifetimeEndingUser.isCondBranchUser()) {
    BlocksWithNonLifetimeEndingUses.erase(Iter);
    return false;
  }

  // Otherwise, we know that both of our users are non-cond branch users and
  // thus must be instructions in the given block. Make sure that the non
  // lifetime ending user is strictly before the lifetime ending user.
  if (std::find_if(LifetimeEndingUser.getIterator(), UserBlock->end(),
                   [&NonLifetimeEndingUser](const SILInstruction &I) -> bool {
                     return NonLifetimeEndingUser == &I;
                   }) != UserBlock->end()) {
    return !handleError([&] {
      llvm::errs() << "Function: '" << Value->getFunction()->getName() << "'\n"
                   << "Found use after free?!\n"
                   << "Value: " << *Value
                   << "Consuming User: " << *LifetimeEndingUser
                   << "Non Consuming User: " << *Iter->second << "Block: bb"
                   << UserBlock->getDebugID() << "\n\n";
    });
  }

  // Erase the use since we know that it is properly joint post-dominated.
  BlocksWithNonLifetimeEndingUses.erase(Iter);
  return false;
}

bool SILValueOwnershipChecker::doesBlockDoubleConsume(
    SILBasicBlock *UserBlock,
    llvm::Optional<GeneralizedUser> LifetimeEndingUser, bool ShouldInsert) {
  if ((ShouldInsert && BlocksWithLifetimeEndingUses.insert(UserBlock).second) ||
      !BlocksWithLifetimeEndingUses.count(UserBlock))
    return false;

  return !handleError([&] {
    llvm::errs() << "Function: '" << Value->getFunction()->getName() << "'\n"
                 << "Found over consume?!\n"
                 << "Value: " << *Value;
    if (LifetimeEndingUser.hasValue())
      llvm::errs() << "User: " << *LifetimeEndingUser.getValue();
    llvm::errs() << "Block: bb" << UserBlock->getDebugID() << "\n\n";
  });
}

void SILValueOwnershipChecker::gatherUsers(
    llvm::SmallVectorImpl<GeneralizedUser> &LifetimeEndingUsers,
    llvm::SmallVectorImpl<GeneralizedUser> &NonLifetimeEndingUsers) {

  // See if Value is guaranteed. If we are guaranteed and not forwarding, then
  // we need to look through subobject uses for more uses. Otherwise, if we are
  // forwarding, we do not create any lifetime ending users/non lifetime ending
  // users since we verify against our base.
  auto OwnershipKind = Value.getOwnershipKind();
  bool IsGuaranteed = OwnershipKind == ValueOwnershipKind::Guaranteed;

  if (IsGuaranteed && isGuaranteedForwardingValue(Value))
    return;

  // Then gather up our initial list of users.
  llvm::SmallVector<Operand *, 8> Users;
  std::copy(Value->use_begin(), Value->use_end(), std::back_inserter(Users));

  auto addCondBranchToList = [](llvm::SmallVectorImpl<GeneralizedUser> &List,
                                CondBranchInst *CBI, unsigned OperandIndex) {
    if (CBI->isConditionOperandIndex(OperandIndex)) {
      List.emplace_back(CBI);
      return;
    }

    bool isTrueOperand = CBI->isTrueOperandIndex(OperandIndex);
    List.emplace_back(CBI, isTrueOperand ? CondBranchInst::TrueIdx
                                         : CondBranchInst::FalseIdx);
  };

  while (!Users.empty()) {
    Operand *Op = Users.pop_back_val();
    SILInstruction *User = Op->getUser();

    // If this op is a type dependent operand, skip it. It is not interesting
    // from an ownership perspective.
    if (User->isTypeDependentOperand(*Op))
      continue;

    if (OwnershipCompatibilityUseChecker(Mod, *Op, Value, ErrorBehavior)
            .check(User)) {
      DEBUG(llvm::dbgs() << "        Lifetime Ending User: " << *User);
      if (auto *CBI = dyn_cast<CondBranchInst>(User)) {
        addCondBranchToList(LifetimeEndingUsers, CBI, Op->getOperandNumber());
      } else {
        LifetimeEndingUsers.emplace_back(User);
      }
    } else {
      DEBUG(llvm::dbgs() << "        Regular User: " << *User);
      if (auto *CBI = dyn_cast<CondBranchInst>(User)) {
        addCondBranchToList(NonLifetimeEndingUsers, CBI,
                            Op->getOperandNumber());
      } else {
        NonLifetimeEndingUsers.emplace_back(User);
      }
    }

    // If our base value is not guaranteed or our intermediate value is not an
    // ownership forwarding inst, continue. We do not want to visit any
    // subobjects recursively.
    if (!IsGuaranteed || !isGuaranteedForwardingInst(User)) {
      continue;
    }

    // At this point, we know that we must have a forwarded subobject. Since the
    // base type is guaranteed, we know that the subobject is either guaranteed
    // or trivial. We now split into two cases, if the user is a terminator or
    // not. If we do not have a terminator, then just add the uses of all of
    // User's results to the worklist.
    if (User->getResults().size()) {
      for (SILValue result : User->getResults()) {
        if (result.getOwnershipKind() == ValueOwnershipKind::Trivial) {
          continue;
        }

        // Now, we /must/ have a guaranteed subobject, so let's assert that the
        // user is actually guaranteed and add the subobject's users to our
        // worklist.
        assert(result.getOwnershipKind() == ValueOwnershipKind::Guaranteed &&
               "Our value is guaranteed and this is a forwarding instruction. "
               "Should have guaranteed ownership as well.");
        copy(result->getUses(), std::back_inserter(Users));
      }

      continue;
    }

    assert(User->getResults().empty());

    auto *TI = dyn_cast<TermInst>(User);
    if (!TI) {
      continue;
    }

    // Otherwise if we have a terminator, add any as uses any
    // end_borrow_argument to ensure that the subscope is completely enclsed
    // within the super scope. all of the arguments to the work list. We require
    // all of our arguments to be either trivial or guaranteed.
    for (auto &Succ : TI->getSuccessors()) {
      auto *BB = Succ.getBB();

      // If we do not have any arguments, then continue.
      if (BB->args_empty())
        continue;

      // Otherwise, make sure that all arguments are trivial or guaranteed. If
      // we fail, emit an error.
      //
      // TODO: We could ignore this error and emit a more specific error on the
      // actual terminator.
      for (auto *BBArg : BB->getArguments()) {
        // *NOTE* We do not emit an error here since we want to allow for more
        // specific errors to be found during use_verification.
        //
        // TODO: Add a flag that associates the terminator instruction with
        // needing to be verified. If it isn't verified appropriately, assert
        // when the verifier is destroyed.
        if (!trivialOrCompatibleOwnershipKinds(BBArg->getOwnershipKind(),
                                               OwnershipKind)) {
          // This is where the error would go.
          continue;
        }

        // If we have a trivial value, just continue.
        if (BBArg->getOwnershipKind() == ValueOwnershipKind::Trivial)
          continue;

        // Otherwise, 
        std::copy(BBArg->use_begin(), BBArg->use_end(), std::back_inserter(Users));
      }
    }
  }
}

// Unique our non lifetime ending user list by only selecting the last user in
// each block.
void SILValueOwnershipChecker::uniqueNonLifetimeEndingUsers(
    ArrayRef<GeneralizedUser> NonLifetimeEndingUsers) {
  for (GeneralizedUser User : NonLifetimeEndingUsers) {
    auto *UserBlock = User.getParent();
    // First try to associate User with User->getParent().
    auto Result =
        BlocksWithNonLifetimeEndingUses.insert(std::make_pair(UserBlock, User));

    // If the insertion succeeds, then we know that there is no more work to
    // be done, so process the next use.
    if (Result.second)
      continue;

    // If the insertion fails, then we have at least two non-lifetime ending
    // uses in the same block. Since we are performing a liveness type of
    // dataflow, we only need the last non-lifetime ending use to show that all
    // lifetime ending uses post dominate both.
    //
    // We begin by checking if the first use is a cond_br use from the previous
    // block. In such a case, we always use the already stored value and
    // continue.
    if (User.isCondBranchUser()) {
      continue;
    }

    // Then, we check if Use is after Result.first->second in the use list. If
    // Use is not later, then we wish to keep the already mapped value, not use,
    // so continue.
    if (std::find_if(Result.first->second.getIterator(), UserBlock->end(),
                     [&User](const SILInstruction &I) -> bool {
                       return User == &I;
                     }) == UserBlock->end()) {
      continue;
    }

    // At this point, we know that User is later in the Block than
    // Result.first->second, so store Use instead.
    Result.first->second = User;
  }
}

bool SILValueOwnershipChecker::checkFunctionArgWithoutLifetimeEndingUses(
    SILFunctionArgument *Arg) {
  switch (Arg->getOwnershipKind()) {
  case ValueOwnershipKind::Guaranteed:
  case ValueOwnershipKind::Unowned:
  case ValueOwnershipKind::Trivial:
    return true;
  case ValueOwnershipKind::Any:
    llvm_unreachable(
        "Function arguments should never have ValueOwnershipKind::Any");
  case ValueOwnershipKind::Owned:
    break;
  }

  if (DEBlocks.isDeadEnd(Arg->getParent()))
    return true;

  return !handleError([&] {
    llvm::errs() << "Function: '" << Arg->getFunction()->getName() << "'\n"
                 << "    Owned function parameter without life ending uses!\n"
                 << "Value: " << *Arg << '\n';
  });
}

bool SILValueOwnershipChecker::checkValueWithoutLifetimeEndingUses() {
  DEBUG(llvm::dbgs() << "    No lifetime ending users?! Bailing early.\n");
  if (auto *Arg = dyn_cast<SILFunctionArgument>(Value)) {
    if (checkFunctionArgWithoutLifetimeEndingUses(Arg)) {
      return true;
    }
  }

  // Check if we are a guaranteed subobject. In such a case, we should never
  // have lifetime ending uses, since our lifetime is guaranteed by our
  // operand, so there is nothing further to do. So just return true.
  if (isGuaranteedForwardingValue(Value) &&
      Value.getOwnershipKind() == ValueOwnershipKind::Guaranteed)
    return true;

  // If we have an unowned value, then again there is nothing left to do.
  if (Value.getOwnershipKind() == ValueOwnershipKind::Unowned)
    return true;

  if (auto *ParentBlock = Value->getParentBlock()) {
    if (DEBlocks.isDeadEnd(ParentBlock)) {
      DEBUG(llvm::dbgs() << "    Ignoring transitively unreachable value "
                         << "without users!\n"
                         << "    Function: '" << Value->getFunction()->getName()
                         << "'\n"
                         << "    Value: " << *Value << '\n');
      return true;
    }
  }

  if (!isValueAddressOrTrivial(Value, Mod)) {
    return !handleError([&] {
      llvm::errs() << "Function: '" << Value->getFunction()->getName() << "'\n"
                   << "Non trivial values, non address values, and non "
                      "guaranteed function args must have at least one "
                      "lifetime ending use?!\n"
                   << "Value: " << *Value << '\n';
    });
  }

  return true;
}

bool SILValueOwnershipChecker::isGuaranteedFunctionArgWithLifetimeEndingUses(
    SILFunctionArgument *Arg,
    const llvm::SmallVectorImpl<GeneralizedUser> &LifetimeEndingUsers) const {
  if (Arg->getOwnershipKind() != ValueOwnershipKind::Guaranteed)
    return true;

  return handleError([&] {
    llvm::errs() << "    Function: '" << Arg->getFunction()->getName() << "'\n"
                 << "    Guaranteed function parameter with life ending uses!\n"
                 << "    Value: " << *Arg;
    for (const auto &U : LifetimeEndingUsers) {
      llvm::errs() << "    Lifetime Ending User: " << *U;
    }
    llvm::errs() << '\n';
  });
}

bool SILValueOwnershipChecker::isSubobjectProjectionWithLifetimeEndingUses(
    SILValue Value,
    const llvm::SmallVectorImpl<GeneralizedUser> &LifetimeEndingUsers) const {
  return handleError([&] {
    llvm::errs() << "    Function: '" << Value->getFunction()->getName()
                 << "'\n"
                 << "    Subobject projection with life ending uses!\n"
                 << "    Value: " << *Value;
    for (const auto &U : LifetimeEndingUsers) {
      llvm::errs() << "    Lifetime Ending User: " << *U;
    }
    llvm::errs() << '\n';
  });
}

bool SILValueOwnershipChecker::checkUses() {
  DEBUG(llvm::dbgs() << "    Gathering and classifying uses!\n");

  // First go through V and gather up its uses. While we do this we:
  //
  // 1. Verify that none of the uses are in the same block. This would be an
  // overconsume so in this case we assert.
  // 2. Verify that the uses are compatible with our ownership convention.
  gatherUsers(LifetimeEndingUsers, RegularUsers);

  // We can only have no lifetime ending uses if we have:
  //
  // 1. A trivial typed value.
  // 2. An address type value.
  // 3. A guaranteed function argument.
  //
  // In the first two cases, it is easy to see that there is nothing further to
  // do but return false.
  //
  // In the case of a function argument, one must think about the issues a bit
  // more. Specifically, we should have /no/ lifetime ending uses of a
  // guaranteed function argument, since a guaranteed function argument should
  // outlive the current function always.
  if (LifetimeEndingUsers.empty() && checkValueWithoutLifetimeEndingUses()) {
    return false;
  }

  DEBUG(llvm::dbgs() << "    Found lifetime ending users! Performing initial "
                        "checks\n");

  // See if we have a guaranteed function address. Guaranteed function addresses
  // should never have any lifetime ending uses.
  if (auto *Arg = dyn_cast<SILFunctionArgument>(Value)) {
    if (!isGuaranteedFunctionArgWithLifetimeEndingUses(Arg,
                                                       LifetimeEndingUsers)) {
      return false;
    }
  }

  // Check if we are an instruction that forwards forwards guaranteed
  // ownership. In such a case, we are a subobject projection. We should not
  // have any lifetime ending uses.
  if (isGuaranteedForwardingValue(Value) &&
      Value.getOwnershipKind() == ValueOwnershipKind::Guaranteed) {
    if (!isSubobjectProjectionWithLifetimeEndingUses(Value,
                                                     LifetimeEndingUsers)) {
      return false;
    }
  }

  // Then add our non lifetime ending users and their blocks to the
  // BlocksWithNonLifetimeEndingUses map. While we do this, if we have multiple
  // uses in the same block, we only accept the last use since from a liveness
  // perspective that is all we care about.
  uniqueNonLifetimeEndingUsers(RegularUsers);

  // Finally, we go through each one of our lifetime ending users performing the
  // following operation:
  //
  // 1. Verifying that no two lifetime ending users are in the same block. This
  // is accomplished by adding the user blocks to the
  // BlocksWithLifetimeEndingUses list. This avoids double consumes.
  //
  // 2. Verifying that no predecessor is a block with a lifetime ending use. The
  // reason why this is necessary is because we wish to not add elements to the
  // worklist twice. Thus we want to check if we have already visited a
  // predecessor.
  llvm::SmallVector<std::pair<GeneralizedUser, SILBasicBlock *>, 32>
      PredsToAddToWorklist;
  for (GeneralizedUser User : LifetimeEndingUsers) {
    SILBasicBlock *UserBlock = User.getParent();
    // If the block does over consume, we either assert or return false. We only
    // return false when debugging.
    if (doesBlockDoubleConsume(UserBlock, User, true)) {
      return handleError([] {});
    }

    // Then check if the given block has a use after free.
    if (doesBlockContainUseAfterFree(User, UserBlock)) {
      return handleError([] {});
    }

    // If this user is in the same block as the value, do not visit
    // predecessors. We must be extra tolerant here since we allow for
    // unreachable code.
    if (UserBlock == Value->getParentBlock())
      continue;

    // Then for each predecessor of this block...
    for (auto *Pred : UserBlock->getPredecessorBlocks()) {
      // If this block is not a block that we have already put on the list, add
      // it to the worklist.
      PredsToAddToWorklist.push_back({User, Pred});
    }
  }

  for (const auto &I : LifetimeEndingUsers) {
    // Finally add the user block to the visited list so we do not try to add it
    // to our must visit successor list.
    VisitedBlocks.insert(I.getParent());
  }

  // Make sure not to add predecessors to our worklist if we only have 1
  // lifetime ending user and it is in the same block as our def.
  if (LifetimeEndingUsers.size() == 1 &&
      LifetimeEndingUsers[0].getParent() == Value->getParentBlock()) {
    return true;
  }

  // Now that we have marked all of our producing blocks, we go through our
  // PredsToAddToWorklist list and add our preds, making sure that none of these
  // preds are in BlocksWithLifetimeEndingUses.
  for (auto Pair : PredsToAddToWorklist) {
    GeneralizedUser User = Pair.first;
    SILBasicBlock *PredBlock = Pair.second;

    // Make sure that the predecessor is not in our
    // BlocksWithLifetimeEndingUses list.
    if (doesBlockDoubleConsume(PredBlock, User)) {
      return handleError([] {});
    }

    if (!VisitedBlocks.insert(PredBlock).second)
      continue;
    Worklist.push_back(PredBlock);
  }

  return true;
}

bool SILValueOwnershipChecker::checkDataflow() {
  DEBUG(llvm::dbgs() << "    Beginning to check dataflow constraints\n");
  // Until the worklist is empty...
  while (!Worklist.empty()) {
    // Grab the next block to visit.
    SILBasicBlock *BB = Worklist.pop_back_val();
    DEBUG(llvm::dbgs() << "    Visiting Block: bb" << BB->getDebugID() << '\n');

    // Since the block is on our worklist, we know already that it is not a
    // block with lifetime ending uses, due to the invariants of our loop.

    // First remove BB from the SuccessorBlocksThatMustBeVisited list. This
    // ensures that when the algorithm terminates, we know that BB was not the
    // beginning of a non-covered path to the exit.
    SuccessorBlocksThatMustBeVisited.erase(BB);

    // Then remove BB from BlocksWithNonLifetimeEndingUses so we know that
    // this block was properly joint post-dominated by our lifetime ending
    // users.
    BlocksWithNonLifetimeEndingUses.erase(BB);

    // Ok, now we know that we do not have an overconsume. If this block does
    // not end in a no return function, we need to update our state for our
    // successors to make sure by the end of the traversal we visit them.
    //
    // We must consider such no-return blocks since we may be running during
    // SILGen before NoReturn folding has run.
    for (SILBasicBlock *SuccBlock : BB->getSuccessorBlocks()) {
      // If we already visited the successor, there is nothing to do since we
      // already visited the successor.
      if (VisitedBlocks.count(SuccBlock))
        continue;

      // Then check if the successor is a transitively unreachable block. In
      // such a case, we ignore it since we are going to leak along that path.
      if (DEBlocks.isDeadEnd(SuccBlock))
        continue;

      // Otherwise, add the successor to our SuccessorBlocksThatMustBeVisited
      // set to ensure that we assert if we do not visit it by the end of the
      // algorithm.
      SuccessorBlocksThatMustBeVisited.insert(SuccBlock);
    }

    // If we are at the dominating block of our walk, continue. There is nothing
    // further to do since we do not want to visit the predecessors of our
    // dominating block. On the other hand, we do want to add its successors to
    // the SuccessorBlocksThatMustBeVisited set.
    if (BB == Value->getParentBlock())
      continue;

    // Then for each predecessor of this block:
    //
    // 1. If we have visited the predecessor already, that it is not a block
    // with lifetime ending uses. If it is a block with uses, then we have a
    // double release... so assert. If not, we continue.
    //
    // 2. We add the predecessor to the worklist if we have not visited it yet.
    for (auto *PredBlock : BB->getPredecessorBlocks()) {
      if (doesBlockDoubleConsume(PredBlock)) {
        return handleError([] {});
      }

      if (VisitedBlocks.count(PredBlock)) {
        continue;
      }

      VisitedBlocks.insert(PredBlock);
      Worklist.push_back(PredBlock);
    }
  }

  // Make sure that we visited all successor blocks that we needed to visit to
  // make sure we didn't leak.
  if (!SuccessorBlocksThatMustBeVisited.empty()) {
    return handleError([&] {
      llvm::errs()
          << "Function: '" << Value->getFunction()->getName() << "'\n"
          << "Error! Found a leak due to a consuming post-dominance failure!\n"
          << "    Value: " << *Value << "    Post Dominating Failure Blocks:\n";
      for (auto *BB : SuccessorBlocksThatMustBeVisited) {
        llvm::errs() << "        bb" << BB->getDebugID();
      }
      llvm::errs() << '\n';
    });
  }

  // Make sure that we do not have any lifetime ending uses left to visit that
  // are not transitively unreachable blocks. If we do, then these non lifetime
  // ending uses must be outside of our "alive" blocks implying a use-after
  // free.
  if (!BlocksWithNonLifetimeEndingUses.empty()) {
    for (auto &Pair : BlocksWithNonLifetimeEndingUses) {
      if (DEBlocks.isDeadEnd(Pair.first)) {
        continue;
      }

      return handleError([&] {
        llvm::errs() << "Function: '" << Value->getFunction()->getName()
                     << "'\n"
                     << "Found use after free due to unvisited non lifetime "
                        "ending uses?!\n"
                     << "Value: " << *Value << "    Remaining Users:\n";
        for (auto &Pair : BlocksWithNonLifetimeEndingUses) {
          llvm::errs() << "User:" << *Pair.second << "Block: bb"
                       << Pair.first->getDebugID() << "\n";
        }
        llvm::errs() << "\n";
      });
    }
  }

  return true;
}

//===----------------------------------------------------------------------===//
//                           Top Level Entrypoints
//===----------------------------------------------------------------------===//

void SILInstruction::verifyOperandOwnership() const {
#ifndef NDEBUG
  if (isStaticInitializerInst())
    return;

  // If SILOwnership is not enabled, do not perform verification.
  if (!getModule().getOptions().EnableSILOwnership)
    return;

  // If the given function has unqualified ownership or we have been asked by
  // the user not to verify this function, there is nothing to verify.
  if (!getFunction()->hasQualifiedOwnership() ||
      !getFunction()->shouldVerifyOwnership())
    return;

  // If we are testing the verifier, bail so we only print errors once when
  // performing a full verification, instead of additionally in the SILBuilder.
  if (IsSILOwnershipVerifierTestingEnabled)
    return;

  // If this is a terminator instruction, do not verify in SILBuilder. This is
  // because when building a new function, one must create the destination block
  // first which is an unnatural pattern and pretty brittle.
  if (isa<TermInst>(this))
    return;

  ErrorBehaviorKind ErrorBehavior;
  if (IsSILOwnershipVerifierTestingEnabled) {
    ErrorBehavior = ErrorBehaviorKind::PrintMessageAndReturnFalse;
  } else {
    ErrorBehavior = ErrorBehaviorKind::PrintMessageAndAssert;
  }
  auto *Self = const_cast<SILInstruction *>(this);
  for (const Operand &Op : getAllOperands()) {
    if (isTypeDependentOperand(Op))
      continue;
    // Skip any SILUndef that we see.
    if (isa<SILUndef>(Op.get()))
      continue;
    OwnershipCompatibilityUseChecker(getModule(), Op, Op.get(), ErrorBehavior)
        .check(Self);
  }
#endif
}

void SILValue::verifyOwnership(SILModule &Mod, DeadEndBlocks *DEBlocks) const {
#ifndef NDEBUG
  // If we are SILUndef, just bail. SILUndef can pair with anything. Any uses of
  // the SILUndef will make sure that the matching checks out.
  if (isa<SILUndef>(*this))
    return;

  // Since we do not have SILUndef, we now know that getFunction() should return
  // a real function. Assert in case this assumption is no longer true.
  SILFunction *F = (*this)->getFunction();
  assert(F && "Instructions and arguments should have a function");

  // If the given function has unqualified ownership or we have been asked by
  // the user not to verify this function, there is nothing to verify.
  if (!F->hasQualifiedOwnership() || !F->shouldVerifyOwnership())
    return;

  ErrorBehaviorKind ErrorBehavior;
  if (IsSILOwnershipVerifierTestingEnabled) {
    ErrorBehavior = ErrorBehaviorKind::PrintMessageAndReturnFalse;
  } else {
    ErrorBehavior = ErrorBehaviorKind::PrintMessageAndAssert;
  }
  llvm::SmallPtrSet<SILBasicBlock *, 32> LiveBlocks;
  if (DEBlocks) {
    SILValueOwnershipChecker(Mod, *DEBlocks, *this, ErrorBehavior, LiveBlocks)
        .check();
  } else {
    DeadEndBlocks DEBlocks((*this)->getFunction());
    SILValueOwnershipChecker(Mod, DEBlocks, *this, ErrorBehavior, LiveBlocks)
        .check();
  }
#endif
}

bool OwnershipChecker::checkValue(SILValue Value) {
  RegularUsers.clear();
  LifetimeEndingUsers.clear();
  LiveBlocks.clear();

  // If we are SILUndef, just bail. SILUndef can pair with anything. Any uses of
  // the SILUndef will make sure that the matching checks out.
  if (isa<SILUndef>(Value))
    return false;

  // Since we do not have SILUndef, we now know that getFunction() should return
  // a real function. Assert in case this assumption is no longer true.
  SILFunction *F = Value->getFunction();
  assert(F && "Instructions and arguments should have a function");

  // If the given function has unqualified ownership, there is nothing further
  // to verify.
  if (!F->hasQualifiedOwnership())
    return false;

  ErrorBehaviorKind ErrorBehavior(ErrorBehaviorKind::ReturnFalse);
  SILValueOwnershipChecker Checker(Mod, DEBlocks, Value, ErrorBehavior,
                                   LiveBlocks);
  if (!Checker.check()) {
    return false;
  }

  // TODO: Make this more efficient.
  copy(Checker.getRegularUsers(), std::back_inserter(RegularUsers));
  copy(Checker.getLifetimeEndingUsers(),
       std::back_inserter(LifetimeEndingUsers));
  return true;
}
