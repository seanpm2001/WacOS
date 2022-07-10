//===--- SILInstructions.cpp - Instructions for SIL code ------------------===//
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
// This file defines the high-level SILInstruction classes used for SIL code.
//
//===----------------------------------------------------------------------===//

#include "swift/SIL/SILInstruction.h"
#include "swift/AST/AST.h"
#include "swift/Basic/type_traits.h"
#include "swift/Basic/Unicode.h"
#include "swift/Basic/AssertImplements.h"
#include "swift/SIL/FormalLinkage.h"
#include "swift/SIL/SILBuilder.h"
#include "swift/SIL/SILCloner.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILVisitor.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ErrorHandling.h"

using namespace swift;
using namespace Lowering;

// Collect used open archetypes from a given type into the \p openedArchetypes.
// \p openedArchetypes is being used as a set. We don't use a real set type here
// for performance reasons.
static void
collectOpenedArchetypes(CanType Ty,
                        SmallVectorImpl<CanType> &openedArchetypes) {
  if (!Ty || !Ty->hasOpenedExistential())
    return;
  Ty.visit([&](Type t) {
    if (t->isOpenedExistential()) {
      // Add this opened archetype if it was not seen yet.
      // We don't use a set here, because the number of open archetypes
      // is usually very small and using a real set may introduce too
      // much overhead.
      if (std::find(openedArchetypes.begin(), openedArchetypes.end(),
                    t->getCanonicalType()) == openedArchetypes.end())
        openedArchetypes.push_back(t.getCanonicalTypeOrNull());
    }
  });
}

// Collect used opened archetypes from the list of substitutions.
static void
collectOpenedArchetypes(ArrayRef<Substitution> subs,
                        SmallVectorImpl<CanType> &openedArchetypes) {
  openedArchetypes.clear();
  for (auto sub : subs) {
    auto Ty = sub.getReplacement().getCanonicalTypeOrNull();
    collectOpenedArchetypes(Ty, openedArchetypes);
  }
}

// Takes a set of open archetypes as input and produces a set of
// references to open archetype definitions.
static void collectOpenedArchetypeOperands(
    SmallVectorImpl<CanType> &OpenedArchetypes,
    SmallVectorImpl<SILValue> &OpenedArchetypeOperands,
    SILOpenedArchetypesState &OpenedArchetypesState, SILModule &Module) {

  for (auto archetype : OpenedArchetypes) {
    auto Def = OpenedArchetypesState.getOpenedArchetypeDef(
        Module.Types.getLoweredType(archetype).getSwiftRValueType());
    assert(getOpenedArchetypeOf(Def->getType().getSwiftRValueType()) &&
           "Opened archetype operands should be of an opened existential type");
    OpenedArchetypeOperands.push_back(Def);
  }
}

// Collects all opened archetypes from a substitutions list
// and form a corresponding list of opened archetype operands.
// We need to know the number of opened archetypes to estimate
// the number of opened archetype operands for the instruction
// being formed, because we need to reserve enough memory
// for these operands.
static void
collectOpenedArchetypeOperands(
    ArrayRef<Substitution> subs,
    SmallVectorImpl<SILValue> &openedArchetypeOperands,
    SILOpenedArchetypesState &OpenedArchetypesState,
    SILModule &Module) {
  SmallVector<CanType, 32> openedArchetypes;
  collectOpenedArchetypes(subs, openedArchetypes);
  collectOpenedArchetypeOperands(openedArchetypes, openedArchetypeOperands,
                                 OpenedArchetypesState, Module);
}

// Collects all opened archetypes from a type and form a corresponding
// list of opened archetype operands.
// We need to know the number of opened archetypes to estimate
// the number of opened archetype operands for the instruction
// being formed, because we need to reserve enough memory
// for these operands.
static void collectOpenedArchetypeOperands(CanType Ty,
                            SmallVectorImpl<SILValue> &openedArchetypeOperands,
                            SILOpenedArchetypesState &OpenedArchetypesState,
                            SILModule &Module) {
  SmallVector<CanType, 32> openedArchetypes;
  collectOpenedArchetypes(Ty, openedArchetypes);
  collectOpenedArchetypeOperands(openedArchetypes, openedArchetypeOperands,
                                 OpenedArchetypesState, Module);
}

//===----------------------------------------------------------------------===//
// SILInstruction Subclasses
//===----------------------------------------------------------------------===//

template <typename INST>
static void *allocateDebugVarCarryingInst(SILModule &M, SILDebugVariable Var,
                                          ArrayRef<SILValue> Operands = {}) {
  return M.allocateInst(sizeof(INST) + Var.Name.size() +
                            sizeof(Operand) * Operands.size(),
                        alignof(INST));
}

TailAllocatedDebugVariable::TailAllocatedDebugVariable(SILDebugVariable Var,
                                                       char *buf)
    : ArgNo(Var.ArgNo), NameLength(Var.Name.size()), Constant(Var.Constant) {
  assert((Var.ArgNo < (2<<16)) && "too many arguments");
  assert((NameLength < (2<<15)) && "variable name too long");
  memcpy(buf, Var.Name.data(), NameLength);
}

StringRef TailAllocatedDebugVariable::getName(const char *buf) const {
  return NameLength ? StringRef(buf, NameLength) : StringRef();
}

AllocStackInst::AllocStackInst(SILDebugLocation Loc, SILType elementType,
                               ArrayRef<SILValue> OpenedArchetypeOperands,
                               SILFunction &F,
                               SILDebugVariable Var)
    : AllocationInst(ValueKind::AllocStackInst, Loc,
                     elementType.getAddressType()),
      NumOperands(OpenedArchetypeOperands.size()),
      VarInfo(Var, getTrailingObjects<char>()) {
  TrailingOperandsList::InitOperandsList(getAllOperands().begin(), this,
                                         OpenedArchetypeOperands);
}

AllocStackInst *
AllocStackInst::create(SILDebugLocation Loc,
                       SILType elementType, SILFunction &F,
                       SILOpenedArchetypesState &OpenedArchetypes,
                       SILDebugVariable Var) {
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(elementType.getSwiftRValueType(),
                                 OpenedArchetypeOperands,
                                 OpenedArchetypes, F.getModule());
  void *Buffer = allocateDebugVarCarryingInst<AllocStackInst>(
      F.getModule(), Var, OpenedArchetypeOperands);
  return ::new (Buffer)
      AllocStackInst(Loc, elementType, OpenedArchetypeOperands, F, Var);
}

/// getDecl - Return the underlying variable declaration associated with this
/// allocation, or null if this is a temporary allocation.
VarDecl *AllocStackInst::getDecl() const {
  return getLoc().getAsASTNode<VarDecl>();
}

AllocRefInst::AllocRefInst(SILDebugLocation Loc, SILType elementType,
                           SILFunction &F, bool objc, bool canBeOnStack,
                           ArrayRef<SILValue> OpenedArchetypeOperands)
    : AllocationInst(ValueKind::AllocRefInst, Loc, elementType),
      StackPromotable(canBeOnStack),
      NumOperands(OpenedArchetypeOperands.size()), ObjC(objc) {
  TrailingOperandsList::InitOperandsList(getAllOperands().begin(), this,
                                         OpenedArchetypeOperands);
}

AllocRefInst *AllocRefInst::create(SILDebugLocation Loc, SILType elementType,
                           SILFunction &F, bool objc, bool canBeOnStack,
                           SILOpenedArchetypesState &OpenedArchetypes) {
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(elementType.getSwiftRValueType(),
                                 OpenedArchetypeOperands,
                                 OpenedArchetypes, F.getModule());
  void *Buffer = F.getModule().allocateInst(
      sizeof(AllocRefInst) +
          sizeof(Operand) * (OpenedArchetypeOperands.size()),
      alignof(AllocRefInst));
  return ::new (Buffer) AllocRefInst(Loc, elementType, F, objc, canBeOnStack,
                                     OpenedArchetypeOperands);
}

AllocRefDynamicInst::AllocRefDynamicInst(
    SILDebugLocation DebugLoc, SILValue operand,
    ArrayRef<SILValue> OpenedArchetypeOperands, SILType ty, bool objc)
    : UnaryInstructionWithOpenArchetypesBase(DebugLoc, operand,
                                             OpenedArchetypeOperands, ty),
      ObjC(objc) {
}

AllocRefDynamicInst *
AllocRefDynamicInst::create(SILDebugLocation DebugLoc, SILValue operand,
                            SILType ty, bool objc,
                            SILFunction &F,
                            SILOpenedArchetypesState &OpenedArchetypes) {
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(ty.getSwiftRValueType(),
                                 OpenedArchetypeOperands,
                                 OpenedArchetypes, F.getModule());
  void *Buffer = F.getModule().allocateInst(
      sizeof(AllocRefDynamicInst) +
          sizeof(Operand) * (OpenedArchetypeOperands.size() + 1),
      alignof(AllocRefDynamicInst));
  return ::new (Buffer)
      AllocRefDynamicInst(DebugLoc, operand, OpenedArchetypeOperands, ty, objc);
}

AllocBoxInst::AllocBoxInst(SILDebugLocation Loc, SILType ElementType,
                           ArrayRef<SILValue> OpenedArchetypeOperands,
                           SILFunction &F, SILDebugVariable Var)
    : AllocationInst(ValueKind::AllocBoxInst, Loc,
                     SILType::getPrimitiveObjectType(
                       SILBoxType::get(ElementType.getSwiftRValueType()))),
      NumOperands(OpenedArchetypeOperands.size()),
      VarInfo(Var, getTrailingObjects<char>()) {
  TrailingOperandsList::InitOperandsList(getAllOperands().begin(), this,
                                         OpenedArchetypeOperands);
}

AllocBoxInst *AllocBoxInst::create(SILDebugLocation Loc, SILType ElementType,
                                   SILFunction &F,
                                   SILOpenedArchetypesState &OpenedArchetypes,
                                   SILDebugVariable Var) {
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(ElementType.getSwiftRValueType(),
                                 OpenedArchetypeOperands,
                                 OpenedArchetypes, F.getModule());
  void *Buffer = allocateDebugVarCarryingInst<AllocBoxInst>(
      F.getModule(), Var, OpenedArchetypeOperands);
  return ::new (Buffer)
      AllocBoxInst(Loc, ElementType, OpenedArchetypeOperands, F, Var);
}

/// getDecl - Return the underlying variable declaration associated with this
/// allocation, or null if this is a temporary allocation.
VarDecl *AllocBoxInst::getDecl() const {
  return getLoc().getAsASTNode<VarDecl>();
}

DebugValueInst::DebugValueInst(SILDebugLocation DebugLoc, SILValue Operand,
                               SILDebugVariable Var)
    : UnaryInstructionBase(DebugLoc, Operand),
      VarInfo(Var, getTrailingObjects<char>()) {}

DebugValueInst *DebugValueInst::create(SILDebugLocation DebugLoc,
                                       SILValue Operand, SILModule &M,
                                       SILDebugVariable Var) {
  void *buf = allocateDebugVarCarryingInst<DebugValueInst>(M, Var);
  return ::new (buf) DebugValueInst(DebugLoc, Operand, Var);
}

DebugValueAddrInst::DebugValueAddrInst(SILDebugLocation DebugLoc,
                                       SILValue Operand, SILDebugVariable Var)
    : UnaryInstructionBase(DebugLoc, Operand),
      VarInfo(Var, getTrailingObjects<char>()) {}

DebugValueAddrInst *DebugValueAddrInst::create(SILDebugLocation DebugLoc,
                                               SILValue Operand, SILModule &M,
                                               SILDebugVariable Var) {
  void *buf = allocateDebugVarCarryingInst<DebugValueAddrInst>(M, Var);
  return ::new (buf) DebugValueAddrInst(DebugLoc, Operand, Var);
}

VarDecl *DebugValueInst::getDecl() const {
  return getLoc().getAsASTNode<VarDecl>();
}
VarDecl *DebugValueAddrInst::getDecl() const {
  return getLoc().getAsASTNode<VarDecl>();
}

AllocExistentialBoxInst::AllocExistentialBoxInst(
    SILDebugLocation Loc, SILType ExistentialType, CanType ConcreteType,
    ArrayRef<ProtocolConformanceRef> Conformances,
    ArrayRef<SILValue> OpenedArchetypeOperands, SILFunction *Parent)
    : AllocationInst(ValueKind::AllocExistentialBoxInst, Loc,
                     ExistentialType.getObjectType()),
      NumOperands(OpenedArchetypeOperands.size()),
      ConcreteType(ConcreteType), Conformances(Conformances) {
  TrailingOperandsList::InitOperandsList(getAllOperands().begin(), this,
                                         OpenedArchetypeOperands);
}

static void declareWitnessTable(SILModule &Mod,
                                ProtocolConformanceRef conformanceRef) {
  if (conformanceRef.isAbstract()) return;
  auto C = conformanceRef.getConcrete();
  if (!Mod.lookUpWitnessTable(C, false))
    Mod.createWitnessTableDeclaration(C,
        getLinkageForProtocolConformance(C->getRootNormalConformance(),
                                         NotForDefinition));
}

AllocExistentialBoxInst *AllocExistentialBoxInst::create(
    SILDebugLocation Loc, SILType ExistentialType, CanType ConcreteType,
    ArrayRef<ProtocolConformanceRef> Conformances,
    SILFunction *F,
    SILOpenedArchetypesState &OpenedArchetypes) {
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(ConcreteType,
                                 OpenedArchetypeOperands,
                                 OpenedArchetypes, F->getModule());
  SILModule &Mod = F->getModule();
  void *Buffer =
      Mod.allocateInst(sizeof(AllocExistentialBoxInst) +
                           sizeof(Operand) * (OpenedArchetypeOperands.size()),
                       alignof(AllocExistentialBoxInst));
  for (ProtocolConformanceRef C : Conformances)
    declareWitnessTable(Mod, C);
  return ::new (Buffer) AllocExistentialBoxInst(Loc,
                                                ExistentialType,
                                                ConcreteType,
                                                Conformances,
                                                OpenedArchetypeOperands,
                                                F);
}

AllocValueBufferInst::AllocValueBufferInst(
    SILDebugLocation DebugLoc, SILType valueType, SILValue operand,
    ArrayRef<SILValue> OpenedArchetypeOperands)
    : UnaryInstructionWithOpenArchetypesBase(DebugLoc, operand,
                                             OpenedArchetypeOperands,
                                             valueType.getAddressType()) {}

AllocValueBufferInst *
AllocValueBufferInst::create(SILDebugLocation DebugLoc, SILType valueType,
                             SILValue operand, SILFunction &F,
                             SILOpenedArchetypesState &OpenedArchetypes) {
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(valueType.getSwiftRValueType(),
                                 OpenedArchetypeOperands,
                                 OpenedArchetypes, F.getModule());
  void *Buffer = F.getModule().allocateInst(
      sizeof(AllocValueBufferInst) +
          sizeof(Operand) * (OpenedArchetypeOperands.size() + 1),
      alignof(AllocValueBufferInst));
  return ::new (Buffer) AllocValueBufferInst(DebugLoc, valueType, operand,
                                             OpenedArchetypeOperands);
}

BuiltinInst *BuiltinInst::create(SILDebugLocation Loc, Identifier Name,
                                 SILType ReturnType,
                                 ArrayRef<Substitution> Substitutions,
                                 ArrayRef<SILValue> Args,
                                 SILFunction &F) {
  void *Buffer = F.getModule().allocateInst(
                              sizeof(BuiltinInst)
                                + decltype(Operands)::getExtraSize(Args.size())
                                + sizeof(Substitution) * Substitutions.size(),
                              alignof(BuiltinInst));
  return ::new (Buffer) BuiltinInst(Loc, Name, ReturnType, Substitutions,
                                    Args);
}

BuiltinInst::BuiltinInst(SILDebugLocation Loc, Identifier Name,
                         SILType ReturnType, ArrayRef<Substitution> Subs,
                         ArrayRef<SILValue> Args)
    : SILInstruction(ValueKind::BuiltinInst, Loc, ReturnType), Name(Name),
      NumSubstitutions(Subs.size()), Operands(this, Args) {
  static_assert(IsTriviallyCopyable<Substitution>::value,
                "assuming Substitution is trivially copyable");
  memcpy(getSubstitutionsStorage(), Subs.begin(),
         sizeof(Substitution) * Subs.size());
}

InitBlockStorageHeaderInst *
InitBlockStorageHeaderInst::create(SILFunction &F,
                               SILDebugLocation DebugLoc, SILValue BlockStorage,
                               SILValue InvokeFunction, SILType BlockType,
                               ArrayRef<Substitution> Subs) {
  void *Buffer = F.getModule().allocateInst(
    sizeof(InitBlockStorageHeaderInst) + sizeof(Substitution) * Subs.size(),
    alignof(InitBlockStorageHeaderInst));
  
  return ::new (Buffer) InitBlockStorageHeaderInst(DebugLoc, BlockStorage,
                                                   InvokeFunction, BlockType,
                                                   Subs);
}

ApplyInst::ApplyInst(SILDebugLocation Loc, SILValue Callee,
                     SILType SubstCalleeTy, SILType Result,
                     ArrayRef<Substitution> Subs,
                     ArrayRef<SILValue> Args, ArrayRef<SILValue> OpenedArchetypeOperands,
                     bool isNonThrowing)
    : ApplyInstBase(ValueKind::ApplyInst, Loc, Callee, SubstCalleeTy, Subs,
                    Args, OpenedArchetypeOperands, Result) {
  setNonThrowing(isNonThrowing);
}

ApplyInst *ApplyInst::create(SILDebugLocation Loc, SILValue Callee,
                             SILType SubstCalleeTy, SILType Result,
                             ArrayRef<Substitution> Subs,
                             ArrayRef<SILValue> Args, bool isNonThrowing,
                             SILFunction &F,
                             SILOpenedArchetypesState &OpenedArchetypes) {
  SmallVector<SILValue, 32> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(Subs, OpenedArchetypeOperands,
                                 OpenedArchetypes, F.getModule());
  void *Buffer = allocate(F, Subs, OpenedArchetypeOperands, Args);
  return ::new(Buffer) ApplyInst(Loc, Callee, SubstCalleeTy,
                                 Result, Subs, Args,
                                 OpenedArchetypeOperands, isNonThrowing);
}

bool swift::doesApplyCalleeHaveSemantics(SILValue callee, StringRef semantics) {
  if (auto *FRI = dyn_cast<FunctionRefInst>(callee))
    if (auto *F = FRI->getReferencedFunction())
      return F->hasSemanticsAttr(semantics);
  return false;
}

void *swift::allocateApplyInst(SILFunction &F, size_t size, size_t alignment) {
  return F.getModule().allocateInst(size, alignment);
}

PartialApplyInst::PartialApplyInst(SILDebugLocation Loc, SILValue Callee,
                                   SILType SubstCalleeTy,
                                   ArrayRef<Substitution> Subs,
                                   ArrayRef<SILValue> Args,
                                   ArrayRef<SILValue> OpenedArchetypeOperands,
                                   SILType ClosureType)
    // FIXME: the callee should have a lowered SIL function type, and
    // PartialApplyInst
    // should derive the type of its result by partially applying the callee's
    // type.
    : ApplyInstBase(ValueKind::PartialApplyInst, Loc, Callee, SubstCalleeTy,
                    Subs, Args, OpenedArchetypeOperands, ClosureType) {}

PartialApplyInst *
PartialApplyInst::create(SILDebugLocation Loc, SILValue Callee,
                         SILType SubstCalleeTy, ArrayRef<Substitution> Subs,
                         ArrayRef<SILValue> Args, SILType ClosureType,
                         SILFunction &F,
                         SILOpenedArchetypesState &OpenedArchetypes) {
  SmallVector<SILValue, 32> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(Subs, OpenedArchetypeOperands,
                                 OpenedArchetypes, F.getModule());
  void *Buffer = allocate(F, Subs, OpenedArchetypeOperands, Args);
  return ::new(Buffer) PartialApplyInst(Loc, Callee, SubstCalleeTy,
                                        Subs, Args,
                                        OpenedArchetypeOperands, ClosureType);
}

TryApplyInstBase::TryApplyInstBase(ValueKind valueKind, SILDebugLocation Loc,
                                   SILBasicBlock *normalBB,
                                   SILBasicBlock *errorBB)
    : TermInst(valueKind, Loc), DestBBs{{this, normalBB}, {this, errorBB}} {}

TryApplyInst::TryApplyInst(SILDebugLocation Loc, SILValue callee,
                           SILType substCalleeTy, ArrayRef<Substitution> subs,
                           ArrayRef<SILValue> args,
                           ArrayRef<SILValue> openedArchetypeOperands,
                           SILBasicBlock *normalBB, SILBasicBlock *errorBB)
    : ApplyInstBase(ValueKind::TryApplyInst, Loc, callee, substCalleeTy, subs,
                    args, openedArchetypeOperands, normalBB, errorBB) {}


TryApplyInst *TryApplyInst::create(SILDebugLocation Loc, SILValue callee,
                                   SILType substCalleeTy,
                                   ArrayRef<Substitution> subs,
                                   ArrayRef<SILValue> args,
                                   SILBasicBlock *normalBB,
                                   SILBasicBlock *errorBB, SILFunction &F,
                                SILOpenedArchetypesState &OpenedArchetypes) {
  SmallVector<SILValue, 32> openedArchetypeOperands;
  collectOpenedArchetypeOperands(subs, openedArchetypeOperands,
                                 OpenedArchetypes, F.getModule());
  void *buffer = allocate(F, subs, openedArchetypeOperands, args);
  return ::new (buffer) TryApplyInst(Loc, callee, substCalleeTy, subs, args,
                                     openedArchetypeOperands,
                                     normalBB, errorBB);
}

FunctionRefInst::FunctionRefInst(SILDebugLocation Loc, SILFunction *F)
    : LiteralInst(ValueKind::FunctionRefInst, Loc, F->getLoweredType()),
      Function(F) {
  F->incrementRefCount();
}

FunctionRefInst::~FunctionRefInst() {
  if (Function)
    Function->decrementRefCount();
}

void FunctionRefInst::dropReferencedFunction() {
  if (Function)
    Function->decrementRefCount();
  Function = nullptr;
}

AllocGlobalInst::AllocGlobalInst(SILDebugLocation Loc,
                                 SILGlobalVariable *Global)
    : SILInstruction(ValueKind::AllocGlobalInst, Loc),
      Global(Global) {}

AllocGlobalInst::AllocGlobalInst(SILDebugLocation Loc)
    : SILInstruction(ValueKind::AllocGlobalInst, Loc) {}

GlobalAddrInst::GlobalAddrInst(SILDebugLocation Loc,
                               SILGlobalVariable *Global)
    : LiteralInst(ValueKind::GlobalAddrInst, Loc,
              Global->getLoweredType().getAddressType()),
      Global(Global) {}

GlobalAddrInst::GlobalAddrInst(SILDebugLocation Loc, SILType Ty)
    : LiteralInst(ValueKind::GlobalAddrInst, Loc, Ty), Global(nullptr) {}

const IntrinsicInfo &BuiltinInst::getIntrinsicInfo() const {
  return getModule().getIntrinsicInfo(getName());
}

const BuiltinInfo &BuiltinInst::getBuiltinInfo() const {
  return getModule().getBuiltinInfo(getName());
}

static unsigned getWordsForBitWidth(unsigned bits) {
  return (bits + llvm::integerPartWidth - 1)/llvm::integerPartWidth;
}

template<typename INST>
static void *allocateLiteralInstWithTextSize(SILFunction &F, unsigned length) {
  return F.getModule().allocateInst(sizeof(INST) + length, alignof(INST));
}

template<typename INST>
static void *allocateLiteralInstWithBitSize(SILFunction &F, unsigned bits) {
  unsigned words = getWordsForBitWidth(bits);
  return F.getModule().allocateInst(
      sizeof(INST) + sizeof(llvm::integerPart)*words, alignof(INST));
}

IntegerLiteralInst::IntegerLiteralInst(SILDebugLocation Loc, SILType Ty,
                                       const llvm::APInt &Value)
    : LiteralInst(ValueKind::IntegerLiteralInst, Loc, Ty),
      numBits(Value.getBitWidth()) {
  std::uninitialized_copy_n(Value.getRawData(), Value.getNumWords(),
                            getTrailingObjects<llvm::integerPart>());
}

IntegerLiteralInst *IntegerLiteralInst::create(SILDebugLocation Loc,
                                               SILType Ty, const APInt &Value,
                                               SILFunction &B) {
  auto intTy = Ty.castTo<BuiltinIntegerType>();
  assert(intTy->getGreatestWidth() == Value.getBitWidth() &&
         "IntegerLiteralInst APInt value's bit width doesn't match type");
  (void)intTy;

  void *buf = allocateLiteralInstWithBitSize<IntegerLiteralInst>(B,
                                                          Value.getBitWidth());
  return ::new (buf) IntegerLiteralInst(Loc, Ty, Value);
}

IntegerLiteralInst *IntegerLiteralInst::create(SILDebugLocation Loc,
                                               SILType Ty, intmax_t Value,
                                               SILFunction &B) {
  auto intTy = Ty.castTo<BuiltinIntegerType>();
  return create(Loc, Ty,
                APInt(intTy->getGreatestWidth(), Value), B);
}

IntegerLiteralInst *IntegerLiteralInst::create(IntegerLiteralExpr *E,
                                               SILDebugLocation Loc,
                                               SILFunction &F) {
  return create(
      Loc, SILType::getBuiltinIntegerType(
               E->getType()->castTo<BuiltinIntegerType>()->getGreatestWidth(),
               F.getASTContext()),
      E->getValue(), F);
}

/// getValue - Return the APInt for the underlying integer literal.
APInt IntegerLiteralInst::getValue() const {
  return APInt(numBits, {getTrailingObjects<llvm::integerPart>(),
                         getWordsForBitWidth(numBits)});
}

FloatLiteralInst::FloatLiteralInst(SILDebugLocation Loc, SILType Ty,
                                   const APInt &Bits)
    : LiteralInst(ValueKind::FloatLiteralInst, Loc, Ty),
      numBits(Bits.getBitWidth()) {
        std::uninitialized_copy_n(Bits.getRawData(), Bits.getNumWords(),
                                  getTrailingObjects<llvm::integerPart>());
}

FloatLiteralInst *FloatLiteralInst::create(SILDebugLocation Loc, SILType Ty,
                                           const APFloat &Value,
                                           SILFunction &B) {
  auto floatTy = Ty.castTo<BuiltinFloatType>();
  assert(&floatTy->getAPFloatSemantics() == &Value.getSemantics() &&
         "FloatLiteralInst value's APFloat semantics do not match type");
  (void)floatTy;

  APInt Bits = Value.bitcastToAPInt();

  void *buf = allocateLiteralInstWithBitSize<FloatLiteralInst>(B,
                                                            Bits.getBitWidth());
  return ::new (buf) FloatLiteralInst(Loc, Ty, Bits);
}

FloatLiteralInst *FloatLiteralInst::create(FloatLiteralExpr *E,
                                           SILDebugLocation Loc,
                                           SILFunction &F) {
  return create(Loc,
                // Builtin floating-point types are always valid SIL types.
                SILType::getBuiltinFloatType(
                    E->getType()->castTo<BuiltinFloatType>()->getFPKind(),
                    F.getASTContext()),
                E->getValue(), F);
}

APInt FloatLiteralInst::getBits() const {
  return APInt(numBits, {getTrailingObjects<llvm::integerPart>(),
                         getWordsForBitWidth(numBits)});
}

APFloat FloatLiteralInst::getValue() const {
  return APFloat(getType().castTo<BuiltinFloatType>()->getAPFloatSemantics(),
                 getBits());
}

StringLiteralInst::StringLiteralInst(SILDebugLocation Loc, StringRef Text,
                                     Encoding encoding, SILType Ty)
    : LiteralInst(ValueKind::StringLiteralInst, Loc, Ty), Length(Text.size()),
      TheEncoding(encoding) {
  memcpy(getTrailingObjects<char>(), Text.data(), Text.size());
}

StringLiteralInst *StringLiteralInst::create(SILDebugLocation Loc,
                                             StringRef text, Encoding encoding,
                                             SILFunction &F) {
  void *buf
    = allocateLiteralInstWithTextSize<StringLiteralInst>(F, text.size());

  auto Ty = SILType::getRawPointerType(F.getModule().getASTContext());
  return ::new (buf) StringLiteralInst(Loc, text, encoding, Ty);
}

uint64_t StringLiteralInst::getCodeUnitCount() {
  if (TheEncoding == Encoding::UTF16)
    return unicode::getUTF16Length(getValue());
  return Length;
}

StoreInst::StoreInst(SILDebugLocation Loc, SILValue Src, SILValue Dest)
    : SILInstruction(ValueKind::StoreInst, Loc), Operands(this, Src, Dest) {}

AssignInst::AssignInst(SILDebugLocation Loc, SILValue Src, SILValue Dest)
    : SILInstruction(ValueKind::AssignInst, Loc), Operands(this, Src, Dest) {}

MarkFunctionEscapeInst *
MarkFunctionEscapeInst::create(SILDebugLocation Loc,
                               ArrayRef<SILValue> Elements, SILFunction &F) {
  void *Buffer = F.getModule().allocateInst(sizeof(MarkFunctionEscapeInst) +
                              decltype(Operands)::getExtraSize(Elements.size()),
                                        alignof(MarkFunctionEscapeInst));
  return ::new(Buffer) MarkFunctionEscapeInst(Loc, Elements);
}

MarkFunctionEscapeInst::MarkFunctionEscapeInst(SILDebugLocation Loc,
                                               ArrayRef<SILValue> Elems)
    : SILInstruction(ValueKind::MarkFunctionEscapeInst, Loc),
      Operands(this, Elems) {}

static SILType getPinResultType(SILType operandType) {
  return SILType::getPrimitiveObjectType(
    OptionalType::get(operandType.getSwiftRValueType())->getCanonicalType());
}

StrongPinInst::StrongPinInst(SILDebugLocation Loc, SILValue operand,
                             Atomicity atomicity)
    : UnaryInstructionBase(Loc, operand, getPinResultType(operand->getType())) {
  setAtomicity(atomicity);
}

CopyAddrInst::CopyAddrInst(SILDebugLocation Loc, SILValue SrcLValue,
                           SILValue DestLValue, IsTake_t isTakeOfSrc,
                           IsInitialization_t isInitializationOfDest)
    : SILInstruction(ValueKind::CopyAddrInst, Loc), IsTakeOfSrc(isTakeOfSrc),
      IsInitializationOfDest(isInitializationOfDest),
      Operands(this, SrcLValue, DestLValue) {}

BindMemoryInst *
BindMemoryInst::create(SILDebugLocation Loc, SILValue Base, SILValue Index,
                       SILType BoundType, SILFunction &F,
                       SILOpenedArchetypesState &OpenedArchetypes) {
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(BoundType.getSwiftRValueType(),
                                 OpenedArchetypeOperands,
                                 OpenedArchetypes, F.getModule());
  void *Buffer = F.getModule().allocateInst(
      sizeof(BindMemoryInst) +
          sizeof(Operand) * (OpenedArchetypeOperands.size() + NumFixedOpers),
      alignof(BindMemoryInst));
  return ::new (Buffer)
    BindMemoryInst(Loc, Base, Index, BoundType, OpenedArchetypeOperands);
}

BindMemoryInst::BindMemoryInst(SILDebugLocation Loc, SILValue Base,
                               SILValue Index,
                               SILType BoundType,
                               ArrayRef<SILValue> OpenedArchetypeOperands)
  : SILInstruction(ValueKind::BindMemoryInst, Loc),
    BoundType(BoundType),
    NumOperands(NumFixedOpers + OpenedArchetypeOperands.size()) {
  TrailingOperandsList::InitOperandsList(getAllOperands().begin(), this,
                                         Base, Index, OpenedArchetypeOperands);
}

UncheckedRefCastAddrInst::UncheckedRefCastAddrInst(SILDebugLocation Loc,
                                                   SILValue src,
                                                   CanType srcType,
                                                   SILValue dest,
                                                   CanType targetType)
    : SILInstruction(ValueKind::UncheckedRefCastAddrInst, Loc),
      Operands(this, src, dest), SourceType(srcType), TargetType(targetType) {}

UnconditionalCheckedCastAddrInst::UnconditionalCheckedCastAddrInst(
    SILDebugLocation Loc, CastConsumptionKind consumption, SILValue src,
    CanType srcType, SILValue dest, CanType targetType)
    : SILInstruction(ValueKind::UnconditionalCheckedCastAddrInst, Loc),
      Operands(this, src, dest), ConsumptionKind(consumption),
      SourceType(srcType), TargetType(targetType) {}

StructInst *StructInst::create(SILDebugLocation Loc, SILType Ty,
                               ArrayRef<SILValue> Elements, SILFunction &F) {
  void *Buffer = F.getModule().allocateInst(sizeof(StructInst) +
                            decltype(Operands)::getExtraSize(Elements.size()),
                            alignof(StructInst));
  return ::new(Buffer) StructInst(Loc, Ty, Elements);
}

StructInst::StructInst(SILDebugLocation Loc, SILType Ty,
                       ArrayRef<SILValue> Elems)
    : SILInstruction(ValueKind::StructInst, Loc, Ty), Operands(this, Elems) {
  assert(!Ty.getStructOrBoundGenericStruct()->hasUnreferenceableStorage());
}

TupleInst *TupleInst::create(SILDebugLocation Loc, SILType Ty,
                             ArrayRef<SILValue> Elements, SILFunction &F) {
  void *Buffer = F.getModule().allocateInst(sizeof(TupleInst) +
                            decltype(Operands)::getExtraSize(Elements.size()),
                            alignof(TupleInst));
  return ::new(Buffer) TupleInst(Loc, Ty, Elements);
}

TupleInst::TupleInst(SILDebugLocation Loc, SILType Ty,
                     ArrayRef<SILValue> Elems)
    : SILInstruction(ValueKind::TupleInst, Loc, Ty), Operands(this, Elems) {}

MetatypeInst::MetatypeInst(SILDebugLocation Loc, SILType Metatype,
                           ArrayRef<SILValue> OpenedArchetypeOperands)
    : SILInstruction(ValueKind::MetatypeInst, Loc, Metatype),
      NumOperands(OpenedArchetypeOperands.size()) {
  TrailingOperandsList::InitOperandsList(getAllOperands().begin(), this,
                                         OpenedArchetypeOperands);
}

bool TupleExtractInst::isTrivialEltOfOneRCIDTuple() const {
  SILModule &Mod = getModule();

  // If we are not trivial, bail.
  if (!getType().isTrivial(Mod))
    return false;

  // If the elt we are extracting is trivial, we cannot have any non trivial
  // fields.
  if (getOperand()->getType().isTrivial(Mod))
    return false;

  // Ok, now we know that our tuple has non-trivial fields. Make sure that our
  // parent tuple has only one non-trivial field.
  bool FoundNonTrivialField = false;
  SILType OpTy = getOperand()->getType();
  unsigned FieldNo = getFieldNo();

  // For each element index of the tuple...
  for (unsigned i = 0, e = getNumTupleElts(); i != e; ++i) {
    // If the element index is the one we are extracting, skip it...
    if (i == FieldNo)
      continue;

    // Otherwise check if we have a non-trivial type. If we don't have one,
    // continue.
    if (OpTy.getTupleElementType(i).isTrivial(Mod))
      continue;

    // Ok, this type is non-trivial. If we have not seen a non-trivial field
    // yet, set the FoundNonTrivialField flag.
    if (!FoundNonTrivialField) {
      FoundNonTrivialField = true;
      continue;
    }

    // If we have seen a field and thus the FoundNonTrivialField flag is set,
    // return false.
    return false;
  }

  // We found only one trivial field.
  assert(FoundNonTrivialField && "Tuple is non-trivial, but does not have a "
                                 "non-trivial element?!");
  return true;
}

bool TupleExtractInst::isEltOnlyNonTrivialElt() const {
  SILModule &Mod = getModule();

  // If the elt we are extracting is trivial, we cannot be a non-trivial
  // field... return false.
  if (getType().isTrivial(Mod))
    return false;

  // Ok, we know that the elt we are extracting is non-trivial. Make sure that
  // we have no other non-trivial elts.
  SILType OpTy = getOperand()->getType();
  unsigned FieldNo = getFieldNo();

  // For each element index of the tuple...
  for (unsigned i = 0, e = getNumTupleElts(); i != e; ++i) {
    // If the element index is the one we are extracting, skip it...
    if (i == FieldNo)
      continue;

    // Otherwise check if we have a non-trivial type. If we don't have one,
    // continue.
    if (OpTy.getTupleElementType(i).isTrivial(Mod))
      continue;

    // If we do have a non-trivial type, return false. We have multiple
    // non-trivial types violating our condition.
    return false;
  }

  // We checked every other elt of the tuple and did not find any
  // non-trivial elt except for ourselves. Return true.
  return true;
}

bool StructExtractInst::isTrivialFieldOfOneRCIDStruct() const {
  SILModule &Mod = getModule();

  // If we are not trivial, bail.
  if (!getType().isTrivial(Mod))
    return false;

  SILType StructTy = getOperand()->getType();

  // If the elt we are extracting is trivial, we cannot have any non trivial
  // fields.
  if (StructTy.isTrivial(Mod))
    return false;

  // Ok, now we know that our tuple has non-trivial fields. Make sure that our
  // parent tuple has only one non-trivial field.
  bool FoundNonTrivialField = false;

  // For each element index of the tuple...
  for (VarDecl *D : getStructDecl()->getStoredProperties()) {
    // If the field is the one we are extracting, skip it...
    if (Field == D)
      continue;

    // Otherwise check if we have a non-trivial type. If we don't have one,
    // continue.
    if (StructTy.getFieldType(D, Mod).isTrivial(Mod))
      continue;

    // Ok, this type is non-trivial. If we have not seen a non-trivial field
    // yet, set the FoundNonTrivialField flag.
    if (!FoundNonTrivialField) {
      FoundNonTrivialField = true;
      continue;
    }

    // If we have seen a field and thus the FoundNonTrivialField flag is set,
    // return false.
    return false;
  }

  // We found only one trivial field.
  assert(FoundNonTrivialField && "Struct is non-trivial, but does not have a "
                                 "non-trivial field?!");
  return true;
}

/// Return true if we are extracting the only non-trivial field of out parent
/// struct. This implies that a ref count operation on the aggregate is
/// equivalent to a ref count operation on this field.
bool StructExtractInst::isFieldOnlyNonTrivialField() const {
  SILModule &Mod = getModule();

  // If the field we are extracting is trivial, we cannot be a non-trivial
  // field... return false.
  if (getType().isTrivial(Mod))
    return false;

  SILType StructTy = getOperand()->getType();

  // Ok, we are visiting a non-trivial field. Then for every stored field...
  for (VarDecl *D : getStructDecl()->getStoredProperties()) {
    // If we are visiting our own field continue.
    if (Field == D)
      continue;

    // Ok, we have a field that is not equal to the field we are
    // extracting. If that field is trivial, we do not care about
    // it... continue.
    if (StructTy.getFieldType(D, Mod).isTrivial(Mod))
      continue;

    // We have found a non trivial member that is not the member we are
    // extracting, fail.
    return false;
  }

  // We checked every other field of the struct and did not find any
  // non-trivial fields except for ourselves. Return true.
  return true;
}

//===----------------------------------------------------------------------===//
// Instructions representing terminators
//===----------------------------------------------------------------------===//


TermInst::SuccessorListTy TermInst::getSuccessors() {
  #define TERMINATOR(TYPE, PARENT, EFFECT, RELEASING) \
    if (auto I = dyn_cast<TYPE>(this)) \
      return I->getSuccessors();
  #include "swift/SIL/SILNodes.def"

  llvm_unreachable("not a terminator?!");
}

bool TermInst::isFunctionExiting() const {
  switch (getTermKind()) {
    case TermKind::BranchInst:
    case TermKind::CondBranchInst:
    case TermKind::SwitchValueInst:
    case TermKind::SwitchEnumInst:
    case TermKind::SwitchEnumAddrInst:
    case TermKind::DynamicMethodBranchInst:
    case TermKind::CheckedCastBranchInst:
    case TermKind::CheckedCastAddrBranchInst:
    case TermKind::UnreachableInst:
    case TermKind::TryApplyInst:
      return false;
    case TermKind::ReturnInst:
    case TermKind::ThrowInst:
      return true;
  }
}

BranchInst::BranchInst(SILDebugLocation Loc, SILBasicBlock *DestBB,
                       ArrayRef<SILValue> Args)
    : TermInst(ValueKind::BranchInst, Loc), DestBB(this, DestBB),
      Operands(this, Args) {}

BranchInst *BranchInst::create(SILDebugLocation Loc, SILBasicBlock *DestBB,
                               SILFunction &F) {
  return create(Loc, DestBB, {}, F);
}

BranchInst *BranchInst::create(SILDebugLocation Loc,
                               SILBasicBlock *DestBB, ArrayRef<SILValue> Args,
                               SILFunction &F) {
  void *Buffer = F.getModule().allocateInst(sizeof(BranchInst) +
                              decltype(Operands)::getExtraSize(Args.size()),
                            alignof(BranchInst));
  return ::new (Buffer) BranchInst(Loc, DestBB, Args);
}

CondBranchInst::CondBranchInst(SILDebugLocation Loc, SILValue Condition,
                               SILBasicBlock *TrueBB, SILBasicBlock *FalseBB,
                               ArrayRef<SILValue> Args, unsigned NumTrue,
                               unsigned NumFalse)
    : TermInst(ValueKind::CondBranchInst, Loc),
      DestBBs{{this, TrueBB}, {this, FalseBB}}, NumTrueArgs(NumTrue),
      NumFalseArgs(NumFalse), Operands(this, Args, Condition) {
  assert(Args.size() == (NumTrueArgs + NumFalseArgs) &&
         "Invalid number of args");
  assert(TrueBB != FalseBB && "Identical destinations");
}

CondBranchInst *CondBranchInst::create(SILDebugLocation Loc,
                                       SILValue Condition,
                                       SILBasicBlock *TrueBB,
                                       SILBasicBlock *FalseBB, SILFunction &F) {
  return create(Loc, Condition, TrueBB, {}, FalseBB, {}, F);
}

CondBranchInst *
CondBranchInst::create(SILDebugLocation Loc, SILValue Condition,
                       SILBasicBlock *TrueBB, ArrayRef<SILValue> TrueArgs,
                       SILBasicBlock *FalseBB, ArrayRef<SILValue> FalseArgs,
                       SILFunction &F) {
  SmallVector<SILValue, 4> Args;
  Args.append(TrueArgs.begin(), TrueArgs.end());
  Args.append(FalseArgs.begin(), FalseArgs.end());

  void *Buffer = F.getModule().allocateInst(sizeof(CondBranchInst) +
                              decltype(Operands)::getExtraSize(Args.size()),
                            alignof(CondBranchInst));
  return ::new (Buffer) CondBranchInst(Loc, Condition, TrueBB, FalseBB, Args,
                                       TrueArgs.size(), FalseArgs.size());
}

OperandValueArrayRef CondBranchInst::getTrueArgs() const {
  return Operands.asValueArray().slice(1, NumTrueArgs);
}

OperandValueArrayRef CondBranchInst::getFalseArgs() const {
  return Operands.asValueArray().slice(1 + NumTrueArgs, NumFalseArgs);
}

SILValue CondBranchInst::getArgForDestBB(const SILBasicBlock *DestBB,
                                         const SILArgument *Arg) const {
  return getArgForDestBB(DestBB, Arg->getIndex());
}

SILValue CondBranchInst::getArgForDestBB(const SILBasicBlock *DestBB,
                                         unsigned ArgIndex) const {
  // If TrueBB and FalseBB equal, we cannot find an arg for this DestBB so
  // return an empty SILValue.
  if (getTrueBB() == getFalseBB()) {
    assert(DestBB == getTrueBB() && "DestBB is not a target of this cond_br");
    return SILValue();
  }

  if (DestBB == getTrueBB())
    return Operands[1 + ArgIndex].get();

  assert(DestBB == getFalseBB()
         && "By process of elimination BB must be false BB");
  return Operands[1 + NumTrueArgs + ArgIndex].get();
}

ArrayRef<Operand> CondBranchInst::getTrueOperands() const {
  if (NumTrueArgs == 0)
    return ArrayRef<Operand>();
  return ArrayRef<Operand>(&Operands[1], NumTrueArgs);
}

MutableArrayRef<Operand> CondBranchInst::getTrueOperands() {
  if (NumTrueArgs == 0)
    return MutableArrayRef<Operand>();
  return MutableArrayRef<Operand>(&Operands[1], NumTrueArgs);
}

ArrayRef<Operand> CondBranchInst::getFalseOperands() const {
  if (NumFalseArgs == 0)
    return ArrayRef<Operand>();
  return ArrayRef<Operand>(&Operands[1+NumTrueArgs], NumFalseArgs);
}

MutableArrayRef<Operand> CondBranchInst::getFalseOperands() {
  if (NumFalseArgs == 0)
    return MutableArrayRef<Operand>();
  return MutableArrayRef<Operand>(&Operands[1+NumTrueArgs], NumFalseArgs);
}

void CondBranchInst::swapSuccessors() {
  // Swap our destinations.
  SILBasicBlock *First = DestBBs[0].getBB();
  DestBBs[0] = DestBBs[1].getBB();
  DestBBs[1] = First;

  // If we don't have any arguments return.
  if (!NumTrueArgs && !NumFalseArgs)
    return;

  // Otherwise swap our true and false arguments.
  MutableArrayRef<Operand> Ops = getAllOperands();
  llvm::SmallVector<SILValue, 4> TrueOps;
  for (SILValue V : getTrueArgs())
    TrueOps.push_back(V);

  auto FalseArgs = getFalseArgs();
  for (unsigned i = 0, e = NumFalseArgs; i < e; ++i) {
    Ops[1+i].set(FalseArgs[i]);
  }

  for (unsigned i = 0, e = NumTrueArgs; i < e; ++i) {
    Ops[1+i+NumFalseArgs].set(TrueOps[i]);
  }

  // Finally swap the number of arguments that we have.
  std::swap(NumTrueArgs, NumFalseArgs);
}

SwitchValueInst::SwitchValueInst(SILDebugLocation Loc, SILValue Operand,
                                 SILBasicBlock *DefaultBB,
                                 ArrayRef<SILValue> Cases,
                                 ArrayRef<SILBasicBlock *> BBs)
    : TermInst(ValueKind::SwitchValueInst, Loc), NumCases(Cases.size()),
      HasDefault(bool(DefaultBB)), Operands(this, Cases, Operand) {

  // Initialize the successor array.
  auto *succs = getSuccessorBuf();
  unsigned OperandBitWidth = 0;

  if (auto OperandTy = Operand->getType().getAs<BuiltinIntegerType>()) {
    OperandBitWidth = OperandTy->getGreatestWidth();
  }

  for (unsigned i = 0, size = Cases.size(); i < size; ++i) {
    // If we have undef, just add the case and continue.
    if (isa<SILUndef>(Cases[i])) {
      ::new (succs + i) SILSuccessor(this, BBs[i]);
      continue;
    }

    if (OperandBitWidth) {
      auto *IL = dyn_cast<IntegerLiteralInst>(Cases[i]);
      assert(IL && "switch_value case value should be of an integer type");
      assert(IL->getValue().getBitWidth() == OperandBitWidth &&
             "switch_value case value is not same bit width as operand");
      (void)IL;
    } else {
      auto *FR = dyn_cast<FunctionRefInst>(Cases[i]);
      if (!FR) {
        if (auto *CF = dyn_cast<ConvertFunctionInst>(Cases[i])) {
          FR = dyn_cast<FunctionRefInst>(CF->getOperand());
        }
      }
      assert(FR && "switch_value case value should be a function reference");
    }
    ::new (succs + i) SILSuccessor(this, BBs[i]);
  }

  if (HasDefault)
    ::new (succs + NumCases) SILSuccessor(this, DefaultBB);
}

SwitchValueInst::~SwitchValueInst() {
  // Destroy the successor records to keep the CFG up to date.
  auto *succs = getSuccessorBuf();
  for (unsigned i = 0, end = NumCases + HasDefault; i < end; ++i) {
    succs[i].~SILSuccessor();
  }
}

SwitchValueInst *SwitchValueInst::create(
    SILDebugLocation Loc, SILValue Operand, SILBasicBlock *DefaultBB,
    ArrayRef<std::pair<SILValue, SILBasicBlock *>> CaseBBs, SILFunction &F) {
  // Allocate enough room for the instruction with tail-allocated data for all
  // the case values and the SILSuccessor arrays. There are `CaseBBs.size()`
  // SILValues and `CaseBBs.size() + (DefaultBB ? 1 : 0)` successors.
  SmallVector<SILValue, 8> Cases;
  SmallVector<SILBasicBlock *, 8> BBs;
  unsigned numCases = CaseBBs.size();
  unsigned numSuccessors = numCases + (DefaultBB ? 1 : 0);
  for (auto pair: CaseBBs) {
    Cases.push_back(pair.first);
    BBs.push_back(pair.second);
  }
  size_t bufSize = sizeof(SwitchValueInst) +
                   decltype(Operands)::getExtraSize(Cases.size()) +
                   sizeof(SILSuccessor) * numSuccessors;
  void *buf = F.getModule().allocateInst(bufSize, alignof(SwitchValueInst));
  return ::new (buf) SwitchValueInst(Loc, Operand, DefaultBB, Cases, BBs);
}

SelectValueInst::SelectValueInst(SILDebugLocation Loc, SILValue Operand,
                                 SILType Type, SILValue DefaultResult,
                                 ArrayRef<SILValue> CaseValuesAndResults)
    : SelectInstBase(ValueKind::SelectValueInst, Loc, Type,
                     CaseValuesAndResults.size() / 2, bool(DefaultResult),
                     CaseValuesAndResults, Operand) {

  unsigned OperandBitWidth = 0;

  if (auto OperandTy = Operand->getType().getAs<BuiltinIntegerType>()) {
    OperandBitWidth = OperandTy->getGreatestWidth();
  }
}

SelectValueInst::~SelectValueInst() {
}

SelectValueInst *
SelectValueInst::create(SILDebugLocation Loc, SILValue Operand, SILType Type,
                        SILValue DefaultResult,
                        ArrayRef<std::pair<SILValue, SILValue>> CaseValues,
                        SILFunction &F) {
  // Allocate enough room for the instruction with tail-allocated data for all
  // the case values and the SILSuccessor arrays. There are `CaseBBs.size()`
  // SILValues and `CaseBBs.size() + (DefaultBB ? 1 : 0)` successors.
  SmallVector<SILValue, 8> CaseValuesAndResults;
  for (auto pair : CaseValues) {
    CaseValuesAndResults.push_back(pair.first);
    CaseValuesAndResults.push_back(pair.second);
  }

  if ((bool)DefaultResult)
    CaseValuesAndResults.push_back(DefaultResult);

  size_t bufSize = sizeof(SelectValueInst) + decltype(Operands)::getExtraSize(
                                               CaseValuesAndResults.size());
  void *buf = F.getModule().allocateInst(bufSize, alignof(SelectValueInst));
  return ::new (buf)
      SelectValueInst(Loc, Operand, Type, DefaultResult, CaseValuesAndResults);
}

static SmallVector<SILValue, 4>
getCaseOperands(ArrayRef<std::pair<EnumElementDecl*, SILValue>> CaseValues,
                SILValue DefaultValue) {
  SmallVector<SILValue, 4> result;

  for (auto &pair : CaseValues)
    result.push_back(pair.second);
  if (DefaultValue)
    result.push_back(DefaultValue);

  return result;
}

SelectEnumInstBase::SelectEnumInstBase(
    ValueKind Kind, SILDebugLocation Loc, SILValue Operand, SILType Ty,
    SILValue DefaultValue,
    ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues)
    : SelectInstBase(Kind, Loc, Ty, CaseValues.size(), bool(DefaultValue),
                     getCaseOperands(CaseValues, DefaultValue), Operand) {
  // Initialize the case and successor arrays.
  auto *cases = getCaseBuf();
  for (unsigned i = 0, size = CaseValues.size(); i < size; ++i) {
    cases[i] = CaseValues[i].first;
  }
}

template <typename SELECT_ENUM_INST>
SELECT_ENUM_INST *SelectEnumInstBase::createSelectEnum(
    SILDebugLocation Loc, SILValue Operand, SILType Ty, SILValue DefaultValue,
    ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues,
    SILFunction &F) {
  // Allocate enough room for the instruction with tail-allocated
  // EnumElementDecl and operand arrays. There are `CaseBBs.size()` decls
  // and `CaseBBs.size() + (DefaultBB ? 1 : 0)` values.
  unsigned numCases = CaseValues.size();

  void *buf = F.getModule().allocateInst(
    sizeof(SELECT_ENUM_INST) + sizeof(EnumElementDecl*) * numCases
     + TailAllocatedOperandList<1>::getExtraSize(numCases + (bool)DefaultValue),
    alignof(SELECT_ENUM_INST));
  return ::new (buf) SELECT_ENUM_INST(Loc,Operand,Ty,DefaultValue,CaseValues);
}

SelectEnumInst *SelectEnumInst::create(
    SILDebugLocation Loc, SILValue Operand, SILType Type,
    SILValue DefaultValue,
    ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues,
    SILFunction &F) {
  return createSelectEnum<SelectEnumInst>(Loc, Operand, Type, DefaultValue,
                                          CaseValues, F);
}

SelectEnumAddrInst *SelectEnumAddrInst::create(
    SILDebugLocation Loc, SILValue Operand, SILType Type,
    SILValue DefaultValue,
    ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues,
    SILFunction &F) {
  return createSelectEnum<SelectEnumAddrInst>(Loc, Operand, Type, DefaultValue,
                                              CaseValues, F);
}

SwitchEnumInstBase::SwitchEnumInstBase(
    ValueKind Kind, SILDebugLocation Loc, SILValue Operand,
    SILBasicBlock *DefaultBB,
    ArrayRef<std::pair<EnumElementDecl *, SILBasicBlock *>> CaseBBs)
    : TermInst(Kind, Loc), Operands(this, Operand), NumCases(CaseBBs.size()),
      HasDefault(bool(DefaultBB)) {
  // Initialize the case and successor arrays.
  auto *cases = getCaseBuf();
  auto *succs = getSuccessorBuf();
  for (unsigned i = 0, size = CaseBBs.size(); i < size; ++i) {
    cases[i] = CaseBBs[i].first;
    ::new (succs + i) SILSuccessor(this, CaseBBs[i].second);
  }

  if (HasDefault)
    ::new (succs + NumCases) SILSuccessor(this, DefaultBB);
}

namespace {
  template <class Inst> EnumElementDecl *
  getUniqueCaseForDefaultValue(Inst *inst, SILValue enumValue) {
    assert(inst->hasDefault() && "doesn't have a default");
    SILType enumType = enumValue->getType();

    EnumDecl *decl = enumType.getEnumOrBoundGenericEnum();
    assert(decl && "switch_enum operand is not an enum");

    // FIXME: Get expansion from SILFunction
    if (!decl->hasFixedLayout(inst->getModule().getSwiftModule(),
                              ResilienceExpansion::Maximal))
      return nullptr;

    llvm::SmallPtrSet<EnumElementDecl *, 4> unswitchedElts;
    for (auto elt : decl->getAllElements())
      unswitchedElts.insert(elt);

    for (unsigned i = 0, e = inst->getNumCases(); i != e; ++i) {
      auto Entry = inst->getCase(i);
      unswitchedElts.erase(Entry.first);
    }

    if (unswitchedElts.size() == 1)
      return *unswitchedElts.begin();

    return nullptr;
  }
}

NullablePtr<EnumElementDecl> SelectEnumInstBase::getUniqueCaseForDefault() {
  return getUniqueCaseForDefaultValue(this, getEnumOperand());
}

NullablePtr<EnumElementDecl> SelectEnumInstBase::getSingleTrueElement() const {
  auto SEIType = getType().getAs<BuiltinIntegerType>();
  if (!SEIType)
    return nullptr;
  if (SEIType->getWidth() != BuiltinIntegerWidth::fixed(1))
    return nullptr;

  // Try to find a single literal "true" case.
  Optional<EnumElementDecl*> TrueElement;
  for (unsigned i = 0, e = getNumCases(); i < e; ++i) {
    auto casePair = getCase(i);
    if (auto intLit = dyn_cast<IntegerLiteralInst>(casePair.second)) {
      if (intLit->getValue() == APInt(1, 1)) {
        if (!TrueElement)
          TrueElement = casePair.first;
        else
          // Use Optional(nullptr) to represent more than one.
          TrueElement = Optional<EnumElementDecl*>(nullptr);
      }
    }
  }

  if (!TrueElement || !*TrueElement)
    return nullptr;
  return *TrueElement;
}

SwitchEnumInstBase::~SwitchEnumInstBase() {
  // Destroy the successor records to keep the CFG up to date.
  auto *succs = getSuccessorBuf();
  for (unsigned i = 0, end = NumCases + HasDefault; i < end; ++i) {
    succs[i].~SILSuccessor();
  }
}

template <typename SWITCH_ENUM_INST>
SWITCH_ENUM_INST *SwitchEnumInstBase::createSwitchEnum(
    SILDebugLocation Loc, SILValue Operand, SILBasicBlock *DefaultBB,
    ArrayRef<std::pair<EnumElementDecl *, SILBasicBlock *>> CaseBBs,
    SILFunction &F) {
  // Allocate enough room for the instruction with tail-allocated
  // EnumElementDecl and SILSuccessor arrays. There are `CaseBBs.size()` decls
  // and `CaseBBs.size() + (DefaultBB ? 1 : 0)` successors.
  unsigned numCases = CaseBBs.size();
  unsigned numSuccessors = numCases + (DefaultBB ? 1 : 0);

  void *buf = F.getModule().allocateInst(sizeof(SWITCH_ENUM_INST)
                                       + sizeof(EnumElementDecl*) * numCases
                                       + sizeof(SILSuccessor) * numSuccessors,
                                     alignof(SWITCH_ENUM_INST));
  return ::new (buf) SWITCH_ENUM_INST(Loc, Operand, DefaultBB, CaseBBs);
}

NullablePtr<EnumElementDecl> SwitchEnumInstBase::getUniqueCaseForDefault() {
  return getUniqueCaseForDefaultValue(this, getOperand());
}

NullablePtr<EnumElementDecl>
SwitchEnumInstBase::getUniqueCaseForDestination(SILBasicBlock *BB) {
  SILValue value = getOperand();
  SILType enumType = value->getType();
  EnumDecl *decl = enumType.getEnumOrBoundGenericEnum();
  assert(decl && "switch_enum operand is not an enum");
  (void)decl;

  EnumElementDecl *D = nullptr;
  for (unsigned i = 0, e = getNumCases(); i != e; ++i) {
    auto Entry = getCase(i);
    if (Entry.second == BB) {
      if (D != nullptr)
        return nullptr;
      D = Entry.first;
    }
  }
  if (!D && hasDefault() && getDefaultBB() == BB) {
    return getUniqueCaseForDefault();
  }
  return D;
}

SwitchEnumInst *SwitchEnumInst::create(
    SILDebugLocation Loc, SILValue Operand, SILBasicBlock *DefaultBB,
    ArrayRef<std::pair<EnumElementDecl *, SILBasicBlock *>> CaseBBs,
    SILFunction &F) {
  return
    createSwitchEnum<SwitchEnumInst>(Loc, Operand, DefaultBB, CaseBBs, F);
}

SwitchEnumAddrInst *SwitchEnumAddrInst::create(
    SILDebugLocation Loc, SILValue Operand, SILBasicBlock *DefaultBB,
    ArrayRef<std::pair<EnumElementDecl *, SILBasicBlock *>> CaseBBs,
    SILFunction &F) {
  return createSwitchEnum<SwitchEnumAddrInst>
    (Loc, Operand, DefaultBB, CaseBBs, F);
}

DynamicMethodBranchInst::DynamicMethodBranchInst(SILDebugLocation Loc,
                                                 SILValue Operand,
                                                 SILDeclRef Member,
                                                 SILBasicBlock *HasMethodBB,
                                                 SILBasicBlock *NoMethodBB)
  : TermInst(ValueKind::DynamicMethodBranchInst, Loc),
    Member(Member),
    DestBBs{{this, HasMethodBB}, {this, NoMethodBB}},
    Operands(this, Operand)
{
}

DynamicMethodBranchInst *
DynamicMethodBranchInst::create(SILDebugLocation Loc, SILValue Operand,
                                SILDeclRef Member, SILBasicBlock *HasMethodBB,
                                SILBasicBlock *NoMethodBB, SILFunction &F) {
  void *Buffer = F.getModule().allocateInst(sizeof(DynamicMethodBranchInst),
                                            alignof(DynamicMethodBranchInst));
  return ::new (Buffer)
      DynamicMethodBranchInst(Loc, Operand, Member, HasMethodBB, NoMethodBB);
}

/// Create a witness method, creating a witness table declaration if we don't
/// have a witness table for it. Later on if someone wants the real definition,
/// lookUpWitnessTable will deserialize it for us if we can.
///
/// This is following the same model of how we deal with SILFunctions in
/// function_ref. There we always just create a declaration and then later
/// deserialize the actual function definition if we need to.
WitnessMethodInst *
WitnessMethodInst::create(SILDebugLocation Loc, CanType LookupType,
                          ProtocolConformanceRef Conformance, SILDeclRef Member,
                          SILType Ty, SILFunction *F,
                          SILOpenedArchetypesState &OpenedArchetypes,
                          bool Volatile) {
  SILModule &Mod = F->getModule();
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(LookupType, OpenedArchetypeOperands,
                                 OpenedArchetypes, F->getModule());
  void *Buffer =
      Mod.allocateInst(sizeof(WitnessMethodInst) +
                           sizeof(Operand) * OpenedArchetypeOperands.size(),
                       alignof(WitnessMethodInst));

  declareWitnessTable(Mod, Conformance);
  return ::new (Buffer) WitnessMethodInst(Loc, LookupType, Conformance, Member,
                                          Ty, OpenedArchetypeOperands, Volatile);
}

InitExistentialAddrInst *InitExistentialAddrInst::create(
    SILDebugLocation Loc, SILValue Existential, CanType ConcreteType,
    SILType ConcreteLoweredType, ArrayRef<ProtocolConformanceRef> Conformances,
    SILFunction *F, SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F->getModule();
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(ConcreteType, OpenedArchetypeOperands,
                                 OpenedArchetypes, F->getModule());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + OpenedArchetypeOperands.size());
  void *Buffer = Mod.allocateInst(size,
                                  alignof(InitExistentialAddrInst));
  for (ProtocolConformanceRef C : Conformances)
    declareWitnessTable(Mod, C);
  return ::new (Buffer) InitExistentialAddrInst(Loc, Existential,
                                                OpenedArchetypeOperands,
                                                ConcreteType,
                                                ConcreteLoweredType,
                                                Conformances);
}

InitExistentialRefInst *
InitExistentialRefInst::create(SILDebugLocation Loc, SILType ExistentialType,
                               CanType ConcreteType, SILValue Instance,
                               ArrayRef<ProtocolConformanceRef> Conformances,
                               SILFunction *F,
                               SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F->getModule();
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(ConcreteType, OpenedArchetypeOperands,
                                 OpenedArchetypes, F->getModule());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + OpenedArchetypeOperands.size());

  void *Buffer = Mod.allocateInst(size,
                                  alignof(InitExistentialRefInst));
  for (ProtocolConformanceRef C : Conformances)
    declareWitnessTable(Mod, C);

  return ::new (Buffer) InitExistentialRefInst(Loc, ExistentialType,
                                               ConcreteType,
                                               Instance,
                                               OpenedArchetypeOperands,
                                               Conformances);
}

InitExistentialMetatypeInst::InitExistentialMetatypeInst(
    SILDebugLocation Loc, SILType existentialMetatypeType, SILValue metatype,
    ArrayRef<SILValue> OpenedArchetypeOperands,
    ArrayRef<ProtocolConformanceRef> conformances)
    : UnaryInstructionWithOpenArchetypesBase(Loc, metatype,
                                             OpenedArchetypeOperands,
                                             existentialMetatypeType),
      NumConformances(conformances.size()) {
  std::uninitialized_copy(conformances.begin(), conformances.end(),
                          getTrailingObjects<ProtocolConformanceRef>());
}

InitExistentialMetatypeInst *InitExistentialMetatypeInst::create(
    SILDebugLocation Loc, SILType existentialMetatypeType, SILValue metatype,
    ArrayRef<ProtocolConformanceRef> conformances, SILFunction *F,
    SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &M = F->getModule();
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(existentialMetatypeType.getSwiftRValueType(),
                                 OpenedArchetypeOperands,
                                 OpenedArchetypes, M);

  unsigned size = totalSizeToAlloc<swift::Operand, ProtocolConformanceRef>(
      1 + OpenedArchetypeOperands.size(), conformances.size());

  void *buffer = M.allocateInst(size, alignof(InitExistentialMetatypeInst));
  for (ProtocolConformanceRef conformance : conformances)
    declareWitnessTable(M, conformance);

  return ::new (buffer) InitExistentialMetatypeInst(
      Loc, existentialMetatypeType, metatype,
      OpenedArchetypeOperands, conformances);
}

ArrayRef<ProtocolConformanceRef>
InitExistentialMetatypeInst::getConformances() const {
  return {getTrailingObjects<ProtocolConformanceRef>(), NumConformances};
}

MarkUninitializedBehaviorInst *
MarkUninitializedBehaviorInst::create(SILModule &M,
                                      SILDebugLocation DebugLoc,
                                      SILValue InitStorage,
                                      ArrayRef<Substitution> InitStorageSubs,
                                      SILValue Storage,
                                      SILValue Setter,
                                      ArrayRef<Substitution> SetterSubs,
                                      SILValue Self,
                                      SILType Ty) {
  auto totalSubs = InitStorageSubs.size() + SetterSubs.size();
  auto mem = M.allocateInst(sizeof(MarkUninitializedBehaviorInst)
                              + additionalSizeToAlloc<Substitution>(totalSubs),
                            alignof(MarkUninitializedBehaviorInst));
  return ::new (mem) MarkUninitializedBehaviorInst(DebugLoc,
                                                   InitStorage, InitStorageSubs,
                                                   Storage,
                                                   Setter, SetterSubs,
                                                   Self,
                                                   Ty);
}

MarkUninitializedBehaviorInst::MarkUninitializedBehaviorInst(
                                        SILDebugLocation DebugLoc,
                                        SILValue InitStorage,
                                        ArrayRef<Substitution> InitStorageSubs,
                                        SILValue Storage,
                                        SILValue Setter,
                                        ArrayRef<Substitution> SetterSubs,
                                        SILValue Self,
                                        SILType Ty)
  : SILInstruction(ValueKind::MarkUninitializedBehaviorInst, DebugLoc, Ty),
    Operands(this, InitStorage, Storage, Setter, Self),
    NumInitStorageSubstitutions(InitStorageSubs.size()),
    NumSetterSubstitutions(SetterSubs.size())
{
  auto *trailing = getTrailingObjects<Substitution>();
  for (unsigned i = 0; i < InitStorageSubs.size(); ++i) {
    ::new ((void*)trailing++) Substitution(InitStorageSubs[i]);
  }
  for (unsigned i = 0; i < SetterSubs.size(); ++i) {
    ::new ((void*)trailing++) Substitution(SetterSubs[i]);
  }
}

OpenExistentialAddrInst::OpenExistentialAddrInst(
    SILDebugLocation DebugLoc, SILValue Operand, SILType SelfTy)
    : UnaryInstructionBase(DebugLoc, Operand, SelfTy) {
}

OpenExistentialRefInst::OpenExistentialRefInst(
    SILDebugLocation DebugLoc, SILValue Operand, SILType Ty)
    : UnaryInstructionBase(DebugLoc, Operand, Ty) {
}

OpenExistentialMetatypeInst::OpenExistentialMetatypeInst(
    SILDebugLocation DebugLoc, SILValue operand, SILType ty)
    : UnaryInstructionBase(DebugLoc, operand, ty) {
}

OpenExistentialBoxInst::OpenExistentialBoxInst(
    SILDebugLocation DebugLoc, SILValue operand, SILType ty)
    : UnaryInstructionBase(DebugLoc, operand, ty) {
}

UncheckedRefCastInst *
UncheckedRefCastInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                             SILType Ty, SILFunction &F,
                             SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(Ty.getSwiftRValueType(),
                                 OpenedArchetypeOperands, OpenedArchetypes,
                                 F.getModule());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + OpenedArchetypeOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(UncheckedRefCastInst));
  return ::new (Buffer) UncheckedRefCastInst(DebugLoc, Operand,
                                             OpenedArchetypeOperands, Ty);
}

UncheckedAddrCastInst *
UncheckedAddrCastInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                              SILType Ty, SILFunction &F,
                              SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(Ty.getSwiftRValueType(),
                                 OpenedArchetypeOperands, OpenedArchetypes,
                                 F.getModule());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + OpenedArchetypeOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(UncheckedAddrCastInst));
  return ::new (Buffer) UncheckedAddrCastInst(DebugLoc, Operand,
                                              OpenedArchetypeOperands, Ty);
}

UncheckedTrivialBitCastInst *
UncheckedTrivialBitCastInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                              SILType Ty, SILFunction &F,
                              SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(Ty.getSwiftRValueType(),
                                 OpenedArchetypeOperands, OpenedArchetypes,
                                 F.getModule());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + OpenedArchetypeOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(UncheckedTrivialBitCastInst));
  return ::new (Buffer) UncheckedTrivialBitCastInst(DebugLoc, Operand,
                                                    OpenedArchetypeOperands,
                                                    Ty);
}

UncheckedBitwiseCastInst *
UncheckedBitwiseCastInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                                 SILType Ty, SILFunction &F,
                                 SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(Ty.getSwiftRValueType(),
                                 OpenedArchetypeOperands, OpenedArchetypes,
                                 F.getModule());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + OpenedArchetypeOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(UncheckedBitwiseCastInst));
  return ::new (Buffer) UncheckedBitwiseCastInst(DebugLoc, Operand,
                                                 OpenedArchetypeOperands, Ty);
}

UnconditionalCheckedCastInst *UnconditionalCheckedCastInst::create(
    SILDebugLocation DebugLoc, SILValue Operand, SILType DestTy, SILFunction &F,
    SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(DestTy.getSwiftRValueType(),
                                 OpenedArchetypeOperands, OpenedArchetypes,
                                 F.getModule());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + OpenedArchetypeOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(UnconditionalCheckedCastInst));
  return ::new (Buffer) UnconditionalCheckedCastInst(DebugLoc, Operand,
                                                     OpenedArchetypeOperands, DestTy);
}

CheckedCastBranchInst *CheckedCastBranchInst::create(
    SILDebugLocation DebugLoc, bool IsExact, SILValue Operand, SILType DestTy,
    SILBasicBlock *SuccessBB, SILBasicBlock *FailureBB, SILFunction &F,
    SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(DestTy.getSwiftRValueType(),
                                 OpenedArchetypeOperands, OpenedArchetypes,
                                 F.getModule());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + OpenedArchetypeOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(CheckedCastBranchInst));
  return ::new (Buffer) CheckedCastBranchInst(DebugLoc, IsExact, Operand,
                                              OpenedArchetypeOperands, DestTy,
                                              SuccessBB, FailureBB);
}

MetatypeInst *MetatypeInst::create(SILDebugLocation Loc, SILType Ty,
                                   SILFunction *F,
                                   SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F->getModule();
  SmallVector<SILValue, 8> OpenedArchetypeOperands;
  collectOpenedArchetypeOperands(getOpenedArchetypeOf(Ty.getSwiftRValueType()),
                                 OpenedArchetypeOperands, OpenedArchetypes,
                                 F->getModule());
  void *Buffer =
      Mod.allocateInst(sizeof(MetatypeInst) +
                           sizeof(Operand) * OpenedArchetypeOperands.size(),
                       alignof(MetatypeInst));

  return ::new (Buffer) MetatypeInst(Loc, Ty, OpenedArchetypeOperands);
}
