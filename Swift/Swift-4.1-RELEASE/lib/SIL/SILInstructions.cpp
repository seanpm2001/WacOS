//===--- SILInstructions.cpp - Instructions for SIL code ------------------===//
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

#include "swift/AST/Expr.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/Basic/AssertImplements.h"
#include "swift/Basic/Unicode.h"
#include "swift/Basic/type_traits.h"
#include "swift/SIL/FormalLinkage.h"
#include "swift/SIL/Projection.h"
#include "swift/SIL/SILBuilder.h"
#include "swift/SIL/SILCloner.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILVisitor.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ErrorHandling.h"

using namespace swift;
using namespace Lowering;

/// Allocate an instruction that inherits from llvm::TrailingObjects<>.
template <class Inst, class... TrailingTypes, class... CountTypes>
static void *allocateTrailingInst(SILFunction &F, CountTypes... counts) {
  return F.getModule().allocateInst(
             Inst::template totalSizeToAlloc<TrailingTypes...>(counts...),
             alignof(Inst));
}

// Collect used open archetypes from a given type into the \p openedArchetypes.
// \p openedArchetypes is being used as a set. We don't use a real set type here
// for performance reasons.
static void
collectDependentTypeInfo(CanType Ty,
                         SmallVectorImpl<CanArchetypeType> &openedArchetypes,
                         bool &hasDynamicSelf) {
  if (!Ty)
    return;
  if (Ty->hasDynamicSelfType())
    hasDynamicSelf = true;
  if (!Ty->hasOpenedExistential())
    return;
  Ty.visit([&](CanType t) {
    if (t->isOpenedExistential()) {
      // Add this opened archetype if it was not seen yet.
      // We don't use a set here, because the number of open archetypes
      // is usually very small and using a real set may introduce too
      // much overhead.
      auto archetypeTy = cast<ArchetypeType>(t);
      if (std::find(openedArchetypes.begin(), openedArchetypes.end(),
                    archetypeTy) == openedArchetypes.end())
        openedArchetypes.push_back(archetypeTy);
    }
  });
}

// Takes a set of open archetypes as input and produces a set of
// references to open archetype definitions.
static void buildTypeDependentOperands(
    SmallVectorImpl<CanArchetypeType> &OpenedArchetypes,
    bool hasDynamicSelf,
    SmallVectorImpl<SILValue> &TypeDependentOperands,
    SILOpenedArchetypesState &OpenedArchetypesState, SILFunction &F) {

  for (auto archetype : OpenedArchetypes) {
    auto Def = OpenedArchetypesState.getOpenedArchetypeDef(archetype);
    assert(Def);
    assert(getOpenedArchetypeOf(Def->getType().getSwiftRValueType()) &&
           "Opened archetype operands should be of an opened existential type");
    TypeDependentOperands.push_back(Def);
  }
  if (hasDynamicSelf)
    TypeDependentOperands.push_back(F.getSelfMetadataArgument());
}

// Collects all opened archetypes from a type and a substitutions list and form
// a corresponding list of opened archetype operands.
// We need to know the number of opened archetypes to estimate
// the number of opened archetype operands for the instruction
// being formed, because we need to reserve enough memory
// for these operands.
static void collectTypeDependentOperands(
                      SmallVectorImpl<SILValue> &TypeDependentOperands,
                      SILOpenedArchetypesState &OpenedArchetypesState,
                      SILFunction &F,
                      CanType Ty,
                      SubstitutionList subs = SubstitutionList()) {
  SmallVector<CanArchetypeType, 4> openedArchetypes;
  bool hasDynamicSelf = false;
  collectDependentTypeInfo(Ty, openedArchetypes, hasDynamicSelf);
  for (auto sub : subs) {
    // Substitutions in SIL should really be canonical.
    auto ReplTy = sub.getReplacement()->getCanonicalType();
    collectDependentTypeInfo(ReplTy, openedArchetypes, hasDynamicSelf);
  }
  buildTypeDependentOperands(openedArchetypes, hasDynamicSelf,
                             TypeDependentOperands,
                             OpenedArchetypesState, F);
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
                               ArrayRef<SILValue> TypeDependentOperands,
                               SILFunction &F,
                               SILDebugVariable Var)
    : InstructionBase(Loc, elementType.getAddressType()),
      NumOperands(TypeDependentOperands.size()),
      VarInfo(Var, getTrailingObjects<char>()) {
  TrailingOperandsList::InitOperandsList(getAllOperands().begin(), this,
                                         TypeDependentOperands);
}

AllocStackInst *
AllocStackInst::create(SILDebugLocation Loc,
                       SILType elementType, SILFunction &F,
                       SILOpenedArchetypesState &OpenedArchetypes,
                       SILDebugVariable Var) {
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               elementType.getSwiftRValueType());
  void *Buffer = allocateDebugVarCarryingInst<AllocStackInst>(
      F.getModule(), Var, TypeDependentOperands);
  return ::new (Buffer)
      AllocStackInst(Loc, elementType, TypeDependentOperands, F, Var);
}

/// getDecl - Return the underlying variable declaration associated with this
/// allocation, or null if this is a temporary allocation.
VarDecl *AllocStackInst::getDecl() const {
  return getLoc().getAsASTNode<VarDecl>();
}

AllocRefInstBase::AllocRefInstBase(SILInstructionKind Kind,
                                   SILDebugLocation Loc,
                                   SILType ObjectType,
                                   bool objc, bool canBeOnStack,
                                   ArrayRef<SILType> ElementTypes,
                                   ArrayRef<SILValue> AllOperands)
    : AllocationInst(Kind, Loc, ObjectType),
      StackPromotable(canBeOnStack),
      NumTailTypes(ElementTypes.size()),
      ObjC(objc),
      Operands(this, AllOperands) {
  static_assert(IsTriviallyCopyable<SILType>::value,
                "assuming SILType is trivially copyable");
  assert(!objc || ElementTypes.size() == 0);
  assert(AllOperands.size() >= ElementTypes.size());

  memcpy(getTypeStorage(), ElementTypes.begin(),
         sizeof(SILType) * ElementTypes.size());
}

AllocRefInst *AllocRefInst::create(SILDebugLocation Loc, SILFunction &F,
                                   SILType ObjectType,
                                   bool objc, bool canBeOnStack,
                                   ArrayRef<SILType> ElementTypes,
                                   ArrayRef<SILValue> ElementCountOperands,
                                   SILOpenedArchetypesState &OpenedArchetypes) {
  assert(ElementTypes.size() == ElementCountOperands.size());
  assert(!objc || ElementTypes.size() == 0);
  SmallVector<SILValue, 8> AllOperands(ElementCountOperands.begin(),
                                       ElementCountOperands.end());
  for (SILType ElemType : ElementTypes) {
    collectTypeDependentOperands(AllOperands, OpenedArchetypes, F,
                                 ElemType.getSwiftRValueType());
  }
  collectTypeDependentOperands(AllOperands, OpenedArchetypes, F,
                               ObjectType.getSwiftRValueType());
  void *Buffer = F.getModule().allocateInst(
                      sizeof(AllocRefInst)
                        + decltype(Operands)::getExtraSize(AllOperands.size())
                        + sizeof(SILType) * ElementTypes.size(),
                      alignof(AllocRefInst));
  return ::new (Buffer) AllocRefInst(Loc, F, ObjectType, objc, canBeOnStack,
                                     ElementTypes, AllOperands);
}

AllocRefDynamicInst *
AllocRefDynamicInst::create(SILDebugLocation DebugLoc, SILFunction &F,
                            SILValue metatypeOperand, SILType ty, bool objc,
                            ArrayRef<SILType> ElementTypes,
                            ArrayRef<SILValue> ElementCountOperands,
                            SILOpenedArchetypesState &OpenedArchetypes) {
  SmallVector<SILValue, 8> AllOperands(ElementCountOperands.begin(),
                                       ElementCountOperands.end());
  AllOperands.push_back(metatypeOperand);
  collectTypeDependentOperands(AllOperands, OpenedArchetypes, F,
                               ty.getSwiftRValueType());
  for (SILType ElemType : ElementTypes) {
    collectTypeDependentOperands(AllOperands, OpenedArchetypes, F,
                                 ElemType.getSwiftRValueType());
  }
  void *Buffer = F.getModule().allocateInst(
                      sizeof(AllocRefDynamicInst)
                        + decltype(Operands)::getExtraSize(AllOperands.size())
                        + sizeof(SILType) * ElementTypes.size(),
                      alignof(AllocRefDynamicInst));
  return ::new (Buffer)
      AllocRefDynamicInst(DebugLoc, ty, objc, ElementTypes, AllOperands);
}

AllocBoxInst::AllocBoxInst(SILDebugLocation Loc, CanSILBoxType BoxType,
                           ArrayRef<SILValue> TypeDependentOperands,
                           SILFunction &F, SILDebugVariable Var)
    : InstructionBase(Loc, SILType::getPrimitiveObjectType(BoxType)),
      NumOperands(TypeDependentOperands.size()),
      VarInfo(Var, getTrailingObjects<char>()) {
  TrailingOperandsList::InitOperandsList(getAllOperands().begin(), this,
                                         TypeDependentOperands);
}

AllocBoxInst *AllocBoxInst::create(SILDebugLocation Loc,
                                   CanSILBoxType BoxType,
                                   SILFunction &F,
                                   SILOpenedArchetypesState &OpenedArchetypes,
                                   SILDebugVariable Var) {
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               BoxType);
  void *Buffer = allocateDebugVarCarryingInst<AllocBoxInst>(
      F.getModule(), Var, TypeDependentOperands);
  return ::new (Buffer)
      AllocBoxInst(Loc, BoxType, TypeDependentOperands, F, Var);
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
    ArrayRef<SILValue> TypeDependentOperands, SILFunction *Parent)
    : InstructionBase(Loc, ExistentialType.getObjectType()),
      NumOperands(TypeDependentOperands.size()),
      ConcreteType(ConcreteType), Conformances(Conformances) {
  TrailingOperandsList::InitOperandsList(getAllOperands().begin(), this,
                                         TypeDependentOperands);
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
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, *F,
                               ConcreteType);
  SILModule &Mod = F->getModule();
  void *Buffer =
      Mod.allocateInst(sizeof(AllocExistentialBoxInst) +
                           sizeof(Operand) * (TypeDependentOperands.size()),
                       alignof(AllocExistentialBoxInst));
  for (ProtocolConformanceRef C : Conformances)
    declareWitnessTable(Mod, C);
  return ::new (Buffer) AllocExistentialBoxInst(Loc,
                                                ExistentialType,
                                                ConcreteType,
                                                Conformances,
                                                TypeDependentOperands,
                                                F);
}

AllocValueBufferInst::AllocValueBufferInst(
    SILDebugLocation DebugLoc, SILType valueType, SILValue operand,
    ArrayRef<SILValue> TypeDependentOperands)
    : UnaryInstructionWithTypeDependentOperandsBase(DebugLoc, operand,
                                                    TypeDependentOperands,
                                                 valueType.getAddressType()) {}

AllocValueBufferInst *
AllocValueBufferInst::create(SILDebugLocation DebugLoc, SILType valueType,
                             SILValue operand, SILFunction &F,
                             SILOpenedArchetypesState &OpenedArchetypes) {
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               valueType.getSwiftRValueType());
  void *Buffer = F.getModule().allocateInst(
      sizeof(AllocValueBufferInst) +
          sizeof(Operand) * (TypeDependentOperands.size() + 1),
      alignof(AllocValueBufferInst));
  return ::new (Buffer) AllocValueBufferInst(DebugLoc, valueType, operand,
                                             TypeDependentOperands);
}

BuiltinInst *BuiltinInst::create(SILDebugLocation Loc, Identifier Name,
                                 SILType ReturnType,
                                 SubstitutionList Substitutions,
                                 ArrayRef<SILValue> Args,
                                 SILModule &M) {
  void *Buffer = M.allocateInst(
                              sizeof(BuiltinInst)
                                + decltype(Operands)::getExtraSize(Args.size())
                                + sizeof(Substitution) * Substitutions.size(),
                              alignof(BuiltinInst));
  return ::new (Buffer) BuiltinInst(Loc, Name, ReturnType, Substitutions,
                                    Args);
}

BuiltinInst::BuiltinInst(SILDebugLocation Loc, Identifier Name,
                         SILType ReturnType, SubstitutionList Subs,
                         ArrayRef<SILValue> Args)
    : InstructionBase(Loc, ReturnType), Name(Name),
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
                               SubstitutionList Subs) {
  void *Buffer = F.getModule().allocateInst(
    sizeof(InitBlockStorageHeaderInst) + sizeof(Substitution) * Subs.size(),
    alignof(InitBlockStorageHeaderInst));
  
  return ::new (Buffer) InitBlockStorageHeaderInst(DebugLoc, BlockStorage,
                                                   InvokeFunction, BlockType,
                                                   Subs);
}

ApplyInst::ApplyInst(SILDebugLocation Loc, SILValue Callee,
                     SILType SubstCalleeTy, SILType Result,
                     SubstitutionList Subs,
                     ArrayRef<SILValue> Args, ArrayRef<SILValue> TypeDependentOperands,
                     bool isNonThrowing,
                     const GenericSpecializationInformation *SpecializationInfo)
    : InstructionBase(Loc, Callee, SubstCalleeTy, Subs, Args,
                      TypeDependentOperands, SpecializationInfo, Result) {
  setNonThrowing(isNonThrowing);
  assert(!SubstCalleeTy.castTo<SILFunctionType>()->isCoroutine());
}

ApplyInst *
ApplyInst::create(SILDebugLocation Loc, SILValue Callee, SubstitutionList Subs,
                  ArrayRef<SILValue> Args, bool isNonThrowing,
                  Optional<SILModuleConventions> ModuleConventions,
                  SILFunction &F, SILOpenedArchetypesState &OpenedArchetypes,
                  const GenericSpecializationInformation *SpecializationInfo) {
  SILType SubstCalleeSILTy =
      Callee->getType().substGenericArgs(F.getModule(), Subs);
  auto SubstCalleeTy = SubstCalleeSILTy.getAs<SILFunctionType>();
  SILFunctionConventions Conv(SubstCalleeTy,
                              ModuleConventions.hasValue()
                                  ? ModuleConventions.getValue()
                                  : SILModuleConventions(F.getModule()));
  SILType Result = Conv.getSILResultType();

  SmallVector<SILValue, 32> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               SubstCalleeSILTy.getSwiftRValueType(), Subs);
  void *Buffer =
    allocateTrailingInst<ApplyInst, Operand, Substitution>(
      F, getNumAllOperands(Args, TypeDependentOperands), Subs.size());
  return ::new(Buffer) ApplyInst(Loc, Callee, SubstCalleeSILTy,
                                 Result, Subs, Args,
                                 TypeDependentOperands, isNonThrowing,
                                 SpecializationInfo);
}

BeginApplyInst::BeginApplyInst(SILDebugLocation loc, SILValue callee,
                               SILType substCalleeTy,
                               ArrayRef<SILType> allResultTypes,
                               ArrayRef<ValueOwnershipKind> allResultOwnerships,
                               SubstitutionList subs,
                               ArrayRef<SILValue> args,
                               ArrayRef<SILValue> typeDependentOperands,
                               bool isNonThrowing,
                     const GenericSpecializationInformation *specializationInfo)
    : InstructionBase(loc, callee, substCalleeTy, subs, args,
                      typeDependentOperands, specializationInfo),
      MultipleValueInstructionTrailingObjects(this, allResultTypes,
                                              allResultOwnerships) {
  setNonThrowing(isNonThrowing);
  assert(substCalleeTy.castTo<SILFunctionType>()->isCoroutine());
}

BeginApplyInst *
BeginApplyInst::create(SILDebugLocation loc, SILValue callee,
                       SubstitutionList subs, ArrayRef<SILValue> args,
                       bool isNonThrowing,
                       Optional<SILModuleConventions> moduleConventions,
                       SILFunction &F,
                       SILOpenedArchetypesState &openedArchetypes,
                  const GenericSpecializationInformation *specializationInfo) {
  SILType substCalleeSILType =
      callee->getType().substGenericArgs(F.getModule(), subs);
  auto substCalleeType = substCalleeSILType.castTo<SILFunctionType>();

  SILFunctionConventions conv(substCalleeType,
                              moduleConventions.hasValue()
                                  ? moduleConventions.getValue()
                                  : SILModuleConventions(F.getModule()));

  SmallVector<SILType, 8> resultTypes;
  SmallVector<ValueOwnershipKind, 8> resultOwnerships;

  for (auto &yield : substCalleeType->getYields()) {
    auto yieldType = conv.getSILType(yield);
    auto convention = SILArgumentConvention(yield.getConvention());
    resultTypes.push_back(yieldType);
    resultOwnerships.push_back(
      ValueOwnershipKind(F.getModule(), yieldType, convention));
  }

  resultTypes.push_back(SILType::getSILTokenType(F.getASTContext()));
  resultOwnerships.push_back(ValueOwnershipKind::Trivial);

  SmallVector<SILValue, 32> typeDependentOperands;
  collectTypeDependentOperands(typeDependentOperands, openedArchetypes, F,
                               substCalleeType, subs);
  void *buffer =
    allocateTrailingInst<BeginApplyInst, Operand, Substitution,
                         MultipleValueInstruction*, BeginApplyResult>(
      F, getNumAllOperands(args, typeDependentOperands), subs.size(),
      1, resultTypes.size());
  return ::new(buffer) BeginApplyInst(loc, callee, substCalleeSILType,
                                      resultTypes, resultOwnerships, subs,
                                      args, typeDependentOperands,
                                      isNonThrowing, specializationInfo);
}

bool swift::doesApplyCalleeHaveSemantics(SILValue callee, StringRef semantics) {
  if (auto *FRI = dyn_cast<FunctionRefInst>(callee))
    if (auto *F = FRI->getReferencedFunction())
      return F->hasSemanticsAttr(semantics);
  return false;
}

PartialApplyInst::PartialApplyInst(
    SILDebugLocation Loc, SILValue Callee, SILType SubstCalleeTy,
    SubstitutionList Subs, ArrayRef<SILValue> Args,
    ArrayRef<SILValue> TypeDependentOperands, SILType ClosureType,
    const GenericSpecializationInformation *SpecializationInfo)
    // FIXME: the callee should have a lowered SIL function type, and
    // PartialApplyInst
    // should derive the type of its result by partially applying the callee's
    // type.
    : InstructionBase(Loc, Callee, SubstCalleeTy, Subs,
                      Args, TypeDependentOperands, SpecializationInfo,
                      ClosureType) {}

PartialApplyInst *PartialApplyInst::create(
    SILDebugLocation Loc, SILValue Callee, ArrayRef<SILValue> Args,
    SubstitutionList Subs, ParameterConvention CalleeConvention, SILFunction &F,
    SILOpenedArchetypesState &OpenedArchetypes,
    const GenericSpecializationInformation *SpecializationInfo) {
  SILType SubstCalleeTy =
      Callee->getType().substGenericArgs(F.getModule(), Subs);
  SILType ClosureType = SILBuilder::getPartialApplyResultType(
      SubstCalleeTy, Args.size(), F.getModule(), {}, CalleeConvention);

  SmallVector<SILValue, 32> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               SubstCalleeTy.getSwiftRValueType(), Subs);
  void *Buffer =
    allocateTrailingInst<PartialApplyInst, Operand, Substitution>(
      F, getNumAllOperands(Args, TypeDependentOperands), Subs.size());
  return ::new(Buffer) PartialApplyInst(Loc, Callee, SubstCalleeTy,
                                        Subs, Args,
                                        TypeDependentOperands, ClosureType,
                                        SpecializationInfo);
}

TryApplyInstBase::TryApplyInstBase(SILInstructionKind kind,
                                   SILDebugLocation loc,
                                   SILBasicBlock *normalBB,
                                   SILBasicBlock *errorBB)
    : TermInst(kind, loc), DestBBs{{this, normalBB}, {this, errorBB}} {}

TryApplyInst::TryApplyInst(
    SILDebugLocation Loc, SILValue callee, SILType substCalleeTy,
    SubstitutionList subs, ArrayRef<SILValue> args,
    ArrayRef<SILValue> TypeDependentOperands, SILBasicBlock *normalBB,
    SILBasicBlock *errorBB,
    const GenericSpecializationInformation *SpecializationInfo)
    : InstructionBase(Loc, callee, substCalleeTy, subs, args,
                      TypeDependentOperands, SpecializationInfo, normalBB,
                      errorBB) {}

TryApplyInst *TryApplyInst::create(
    SILDebugLocation loc, SILValue callee, SubstitutionList subs,
    ArrayRef<SILValue> args, SILBasicBlock *normalBB, SILBasicBlock *errorBB,
    SILFunction &F, SILOpenedArchetypesState &openedArchetypes,
    const GenericSpecializationInformation *specializationInfo) {
  SILType substCalleeTy =
      callee->getType().substGenericArgs(F.getModule(), subs);

  SmallVector<SILValue, 32> typeDependentOperands;
  collectTypeDependentOperands(typeDependentOperands, openedArchetypes, F,
                               substCalleeTy.getSwiftRValueType(), subs);
  void *buffer =
    allocateTrailingInst<TryApplyInst, Operand, Substitution>(
      F, getNumAllOperands(args, typeDependentOperands), subs.size());
  return ::new (buffer) TryApplyInst(loc, callee, substCalleeTy, subs, args,
                                     typeDependentOperands,
                                     normalBB, errorBB, specializationInfo);
}

FunctionRefInst::FunctionRefInst(SILDebugLocation Loc, SILFunction *F)
    : InstructionBase(Loc, F->getLoweredType()),
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
    : InstructionBase(Loc),
      Global(Global) {}

GlobalAddrInst::GlobalAddrInst(SILDebugLocation DebugLoc,
                               SILGlobalVariable *Global)
    : InstructionBase(DebugLoc, Global->getLoweredType().getAddressType(),
                      Global) {}

GlobalValueInst::GlobalValueInst(SILDebugLocation DebugLoc,
                                 SILGlobalVariable *Global)
    : InstructionBase(DebugLoc, Global->getLoweredType().getObjectType(),
                      Global) {}


const IntrinsicInfo &BuiltinInst::getIntrinsicInfo() const {
  return getModule().getIntrinsicInfo(getName());
}

const BuiltinInfo &BuiltinInst::getBuiltinInfo() const {
  return getModule().getBuiltinInfo(getName());
}

static unsigned getWordsForBitWidth(unsigned bits) {
  return ((bits + llvm::APInt::APINT_BITS_PER_WORD - 1)
          / llvm::APInt::APINT_BITS_PER_WORD);
}

template<typename INST>
static void *allocateLiteralInstWithTextSize(SILModule &M, unsigned length) {
  return M.allocateInst(sizeof(INST) + length, alignof(INST));
}

template<typename INST>
static void *allocateLiteralInstWithBitSize(SILModule &M, unsigned bits) {
  unsigned words = getWordsForBitWidth(bits);
  return M.allocateInst(
      sizeof(INST) + sizeof(llvm::APInt::WordType)*words, alignof(INST));
}

IntegerLiteralInst::IntegerLiteralInst(SILDebugLocation Loc, SILType Ty,
                                       const llvm::APInt &Value)
    : InstructionBase(Loc, Ty),
      numBits(Value.getBitWidth()) {
  std::uninitialized_copy_n(Value.getRawData(), Value.getNumWords(),
                            getTrailingObjects<llvm::APInt::WordType>());
}

IntegerLiteralInst *IntegerLiteralInst::create(SILDebugLocation Loc,
                                               SILType Ty, const APInt &Value,
                                               SILModule &M) {
  auto intTy = Ty.castTo<BuiltinIntegerType>();
  assert(intTy->getGreatestWidth() == Value.getBitWidth() &&
         "IntegerLiteralInst APInt value's bit width doesn't match type");
  (void)intTy;

  void *buf = allocateLiteralInstWithBitSize<IntegerLiteralInst>(M,
                                                          Value.getBitWidth());
  return ::new (buf) IntegerLiteralInst(Loc, Ty, Value);
}

IntegerLiteralInst *IntegerLiteralInst::create(SILDebugLocation Loc,
                                               SILType Ty, intmax_t Value,
                                               SILModule &M) {
  auto intTy = Ty.castTo<BuiltinIntegerType>();
  return create(Loc, Ty,
                APInt(intTy->getGreatestWidth(), Value), M);
}

IntegerLiteralInst *IntegerLiteralInst::create(IntegerLiteralExpr *E,
                                               SILDebugLocation Loc,
                                               SILModule &M) {
  return create(
      Loc, SILType::getBuiltinIntegerType(
               E->getType()->castTo<BuiltinIntegerType>()->getGreatestWidth(),
               M.getASTContext()),
      E->getValue(), M);
}

/// getValue - Return the APInt for the underlying integer literal.
APInt IntegerLiteralInst::getValue() const {
  return APInt(numBits, {getTrailingObjects<llvm::APInt::WordType>(),
                         getWordsForBitWidth(numBits)});
}

FloatLiteralInst::FloatLiteralInst(SILDebugLocation Loc, SILType Ty,
                                   const APInt &Bits)
    : InstructionBase(Loc, Ty),
      numBits(Bits.getBitWidth()) {
        std::uninitialized_copy_n(Bits.getRawData(), Bits.getNumWords(),
                                  getTrailingObjects<llvm::APInt::WordType>());
}

FloatLiteralInst *FloatLiteralInst::create(SILDebugLocation Loc, SILType Ty,
                                           const APFloat &Value,
                                           SILModule &M) {
  auto floatTy = Ty.castTo<BuiltinFloatType>();
  assert(&floatTy->getAPFloatSemantics() == &Value.getSemantics() &&
         "FloatLiteralInst value's APFloat semantics do not match type");
  (void)floatTy;

  APInt Bits = Value.bitcastToAPInt();

  void *buf = allocateLiteralInstWithBitSize<FloatLiteralInst>(M,
                                                            Bits.getBitWidth());
  return ::new (buf) FloatLiteralInst(Loc, Ty, Bits);
}

FloatLiteralInst *FloatLiteralInst::create(FloatLiteralExpr *E,
                                           SILDebugLocation Loc,
                                           SILModule &M) {
  return create(Loc,
                // Builtin floating-point types are always valid SIL types.
                SILType::getBuiltinFloatType(
                    E->getType()->castTo<BuiltinFloatType>()->getFPKind(),
                    M.getASTContext()),
                E->getValue(), M);
}

APInt FloatLiteralInst::getBits() const {
  return APInt(numBits, {getTrailingObjects<llvm::APInt::WordType>(),
                         getWordsForBitWidth(numBits)});
}

APFloat FloatLiteralInst::getValue() const {
  return APFloat(getType().castTo<BuiltinFloatType>()->getAPFloatSemantics(),
                 getBits());
}

StringLiteralInst::StringLiteralInst(SILDebugLocation Loc, StringRef Text,
                                     Encoding encoding, SILType Ty)
    : InstructionBase(Loc, Ty), Length(Text.size()),
      TheEncoding(encoding) {
  memcpy(getTrailingObjects<char>(), Text.data(), Text.size());
}

StringLiteralInst *StringLiteralInst::create(SILDebugLocation Loc,
                                             StringRef text, Encoding encoding,
                                             SILModule &M) {
  void *buf
    = allocateLiteralInstWithTextSize<StringLiteralInst>(M, text.size());

  auto Ty = SILType::getRawPointerType(M.getASTContext());
  return ::new (buf) StringLiteralInst(Loc, text, encoding, Ty);
}

uint64_t StringLiteralInst::getCodeUnitCount() {
  if (TheEncoding == Encoding::UTF16)
    return unicode::getUTF16Length(getValue());
  return Length;
}

ConstStringLiteralInst::ConstStringLiteralInst(SILDebugLocation Loc,
                                               StringRef Text,
                                               Encoding encoding, SILType Ty)
    : InstructionBase(Loc, Ty),
      Length(Text.size()), TheEncoding(encoding) {
  memcpy(getTrailingObjects<char>(), Text.data(), Text.size());
}

ConstStringLiteralInst *ConstStringLiteralInst::create(SILDebugLocation Loc,
                                                       StringRef text,
                                                       Encoding encoding,
                                                       SILModule &M) {
  void *buf =
      allocateLiteralInstWithTextSize<ConstStringLiteralInst>(M, text.size());

  auto Ty = SILType::getRawPointerType(M.getASTContext());
  return ::new (buf) ConstStringLiteralInst(Loc, text, encoding, Ty);
}

uint64_t ConstStringLiteralInst::getCodeUnitCount() {
  if (TheEncoding == Encoding::UTF16)
    return unicode::getUTF16Length(getValue());
  return Length;
}

StoreInst::StoreInst(
    SILDebugLocation Loc, SILValue Src, SILValue Dest,
    StoreOwnershipQualifier Qualifier = StoreOwnershipQualifier::Unqualified)
    : InstructionBase(Loc), Operands(this, Src, Dest),
      OwnershipQualifier(Qualifier) {}

StoreBorrowInst::StoreBorrowInst(SILDebugLocation DebugLoc, SILValue Src,
                                 SILValue Dest)
    : InstructionBase(DebugLoc, Dest->getType()),
      Operands(this, Src, Dest) {}

EndBorrowInst::EndBorrowInst(SILDebugLocation DebugLoc, SILValue Src,
                             SILValue Dest)
    : InstructionBase(DebugLoc),
      Operands(this, Src, Dest) {}

EndBorrowArgumentInst::EndBorrowArgumentInst(SILDebugLocation DebugLoc,
                                             SILArgument *Arg)
    : UnaryInstructionBase(DebugLoc, SILValue(Arg)) {}

StringRef swift::getSILAccessKindName(SILAccessKind kind) {
  switch (kind) {
  case SILAccessKind::Init: return "init";
  case SILAccessKind::Read: return "read";
  case SILAccessKind::Modify: return "modify";
  case SILAccessKind::Deinit: return "deinit";
  }
  llvm_unreachable("bad access kind");
}

StringRef swift::getSILAccessEnforcementName(SILAccessEnforcement enforcement) {
  switch (enforcement) {
  case SILAccessEnforcement::Unknown: return "unknown";
  case SILAccessEnforcement::Static: return "static";
  case SILAccessEnforcement::Dynamic: return "dynamic";
  case SILAccessEnforcement::Unsafe: return "unsafe";
  }
  llvm_unreachable("bad access enforcement");
}

AssignInst::AssignInst(SILDebugLocation Loc, SILValue Src, SILValue Dest)
    : InstructionBase(Loc), Operands(this, Src, Dest) {}

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
    : InstructionBase(Loc),
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
    : InstructionBase(Loc), IsTakeOfSrc(isTakeOfSrc),
      IsInitializationOfDest(isInitializationOfDest),
      Operands(this, SrcLValue, DestLValue) {}

BindMemoryInst *
BindMemoryInst::create(SILDebugLocation Loc, SILValue Base, SILValue Index,
                       SILType BoundType, SILFunction &F,
                       SILOpenedArchetypesState &OpenedArchetypes) {
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               BoundType.getSwiftRValueType());
  void *Buffer = F.getModule().allocateInst(
      sizeof(BindMemoryInst) +
          sizeof(Operand) * (TypeDependentOperands.size() + NumFixedOpers),
      alignof(BindMemoryInst));
  return ::new (Buffer)
    BindMemoryInst(Loc, Base, Index, BoundType, TypeDependentOperands);
}

BindMemoryInst::BindMemoryInst(SILDebugLocation Loc, SILValue Base,
                               SILValue Index,
                               SILType BoundType,
                               ArrayRef<SILValue> TypeDependentOperands)
  : InstructionBase(Loc),
    BoundType(BoundType),
    NumOperands(NumFixedOpers + TypeDependentOperands.size()) {
  TrailingOperandsList::InitOperandsList(getAllOperands().begin(), this,
                                         Base, Index, TypeDependentOperands);
}

UncheckedRefCastAddrInst::UncheckedRefCastAddrInst(SILDebugLocation Loc,
                                                   SILValue src,
                                                   CanType srcType,
                                                   SILValue dest,
                                                   CanType targetType)
    : InstructionBase(Loc),
      Operands(this, src, dest), SourceType(srcType), TargetType(targetType) {}

UnconditionalCheckedCastAddrInst::UnconditionalCheckedCastAddrInst(
    SILDebugLocation Loc, SILValue src, CanType srcType, SILValue dest,
    CanType targetType)
    : InstructionBase(Loc),
      Operands(this, src, dest), SourceType(srcType), TargetType(targetType) {}

StructInst *StructInst::create(SILDebugLocation Loc, SILType Ty,
                               ArrayRef<SILValue> Elements, SILModule &M) {
  void *Buffer = M.allocateInst(sizeof(StructInst) +
                            decltype(Operands)::getExtraSize(Elements.size()),
                            alignof(StructInst));
  return ::new(Buffer) StructInst(Loc, Ty, Elements);
}

StructInst::StructInst(SILDebugLocation Loc, SILType Ty,
                       ArrayRef<SILValue> Elems)
    : InstructionBase(Loc, Ty), Operands(this, Elems) {
  assert(!Ty.getStructOrBoundGenericStruct()->hasUnreferenceableStorage());
}

ObjectInst *ObjectInst::create(SILDebugLocation Loc, SILType Ty,
                               ArrayRef<SILValue> Elements,
                               unsigned NumBaseElements, SILModule &M) {
  void *Buffer = M.allocateInst(sizeof(ObjectInst) +
                            decltype(Operands)::getExtraSize(Elements.size()),
                            alignof(ObjectInst));
  return ::new(Buffer) ObjectInst(Loc, Ty, Elements, NumBaseElements);
}

ObjectInst::ObjectInst(SILDebugLocation Loc, SILType Ty,
                       ArrayRef<SILValue> Elems, unsigned NumBaseElements)
    : InstructionBase(Loc, Ty),
      NumBaseElements(NumBaseElements), Operands(this, Elems) {}

TupleInst *TupleInst::create(SILDebugLocation Loc, SILType Ty,
                             ArrayRef<SILValue> Elements, SILModule &M) {
  void *Buffer = M.allocateInst(sizeof(TupleInst) +
                            decltype(Operands)::getExtraSize(Elements.size()),
                            alignof(TupleInst));
  return ::new(Buffer) TupleInst(Loc, Ty, Elements);
}

TupleInst::TupleInst(SILDebugLocation Loc, SILType Ty,
                     ArrayRef<SILValue> Elems)
    : InstructionBase(Loc, Ty), Operands(this, Elems) {}

MetatypeInst::MetatypeInst(SILDebugLocation Loc, SILType Metatype,
                           ArrayRef<SILValue> TypeDependentOperands)
    : InstructionBase(Loc, Metatype),
      NumOperands(TypeDependentOperands.size()) {
  TrailingOperandsList::InitOperandsList(getAllOperands().begin(), this,
                                         TypeDependentOperands);
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
  switch (getKind()) {
#define TERMINATOR(ID, NAME, PARENT, MEMBEHAVIOR, MAYRELEASE) \
  case SILInstructionKind::ID: return cast<ID>(this)->getSuccessors();
#include "swift/SIL/SILNodes.def"
  default: llvm_unreachable("not a terminator");
  }
  llvm_unreachable("bad instruction kind");
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
  case TermKind::CheckedCastValueBranchInst:
  case TermKind::CheckedCastAddrBranchInst:
  case TermKind::UnreachableInst:
  case TermKind::TryApplyInst:
  case TermKind::YieldInst:
    return false;
  case TermKind::ReturnInst:
  case TermKind::ThrowInst:
  case TermKind::UnwindInst:
    return true;
  }

  llvm_unreachable("Unhandled TermKind in switch.");
}

YieldInst::YieldInst(SILDebugLocation loc, ArrayRef<SILValue> yieldedValues,
                     SILBasicBlock *normalBB, SILBasicBlock *unwindBB)
  : InstructionBase(loc),
    DestBBs{{this, normalBB}, {this, unwindBB}},
    Operands(this, yieldedValues) {}

YieldInst *YieldInst::create(SILDebugLocation loc,
                             ArrayRef<SILValue> yieldedValues,
                             SILBasicBlock *normalBB, SILBasicBlock *unwindBB,
                             SILFunction &F) {
  void *buffer = F.getModule().allocateInst(sizeof(YieldInst) +
                        decltype(Operands)::getExtraSize(yieldedValues.size()),
                                            alignof(YieldInst));
  return ::new (buffer) YieldInst(loc, yieldedValues, normalBB, unwindBB);
}

BranchInst::BranchInst(SILDebugLocation Loc, SILBasicBlock *DestBB,
                       ArrayRef<SILValue> Args)
    : InstructionBase(Loc), DestBB(this, DestBB),
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
                               unsigned NumFalse, ProfileCounter TrueBBCount,
                               ProfileCounter FalseBBCount)
    : InstructionBase(Loc), DestBBs{{this, TrueBB, TrueBBCount},
                                    {this, FalseBB, FalseBBCount}},
      NumTrueArgs(NumTrue), NumFalseArgs(NumFalse),
      Operands(this, Args, Condition) {
  assert(Args.size() == (NumTrueArgs + NumFalseArgs) &&
         "Invalid number of args");
  assert(TrueBB != FalseBB && "Identical destinations");
}

CondBranchInst *CondBranchInst::create(SILDebugLocation Loc, SILValue Condition,
                                       SILBasicBlock *TrueBB,
                                       SILBasicBlock *FalseBB,
                                       ProfileCounter TrueBBCount,
                                       ProfileCounter FalseBBCount,
                                       SILFunction &F) {
  return create(Loc, Condition, TrueBB, {}, FalseBB, {}, TrueBBCount,
                FalseBBCount, F);
}

CondBranchInst *
CondBranchInst::create(SILDebugLocation Loc, SILValue Condition,
                       SILBasicBlock *TrueBB, ArrayRef<SILValue> TrueArgs,
                       SILBasicBlock *FalseBB, ArrayRef<SILValue> FalseArgs,
                       ProfileCounter TrueBBCount, ProfileCounter FalseBBCount,
                       SILFunction &F) {
  SmallVector<SILValue, 4> Args;
  Args.append(TrueArgs.begin(), TrueArgs.end());
  Args.append(FalseArgs.begin(), FalseArgs.end());

  void *Buffer = F.getModule().allocateInst(sizeof(CondBranchInst) +
                              decltype(Operands)::getExtraSize(Args.size()),
                            alignof(CondBranchInst));
  return ::new (Buffer) CondBranchInst(Loc, Condition, TrueBB, FalseBB, Args,
                                       TrueArgs.size(), FalseArgs.size(),
                                       TrueBBCount, FalseBBCount);
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
    : InstructionBase(Loc), NumCases(Cases.size()),
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
    : InstructionBase(Loc, Type,
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
    SILInstructionKind Kind, SILDebugLocation Loc, SILType Ty, SILValue Operand,
    SILValue DefaultValue,
    ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues,
    Optional<ArrayRef<ProfileCounter>> CaseCounts, ProfileCounter DefaultCount)
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
    ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues, SILFunction &F,
    Optional<ArrayRef<ProfileCounter>> CaseCounts,
    ProfileCounter DefaultCount) {
  // Allocate enough room for the instruction with tail-allocated
  // EnumElementDecl and operand arrays. There are `CaseBBs.size()` decls
  // and `CaseBBs.size() + (DefaultBB ? 1 : 0)` values.
  unsigned numCases = CaseValues.size();

  void *buf = F.getModule().allocateInst(
      sizeof(SELECT_ENUM_INST) + sizeof(EnumElementDecl *) * numCases +
          sizeof(ProfileCounter) + TailAllocatedOperandList<1>::getExtraSize(
                                       numCases + (bool)DefaultValue),
      alignof(SELECT_ENUM_INST));
  return ::new (buf) SELECT_ENUM_INST(Loc, Operand, Ty, DefaultValue,
                                      CaseValues, CaseCounts, DefaultCount);
}

SelectEnumInst *SelectEnumInst::create(
    SILDebugLocation Loc, SILValue Operand, SILType Type, SILValue DefaultValue,
    ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues, SILFunction &F,
    Optional<ArrayRef<ProfileCounter>> CaseCounts,
    ProfileCounter DefaultCount) {
  return createSelectEnum<SelectEnumInst>(Loc, Operand, Type, DefaultValue,
                                          CaseValues, F, CaseCounts,
                                          DefaultCount);
}

SelectEnumAddrInst *SelectEnumAddrInst::create(
    SILDebugLocation Loc, SILValue Operand, SILType Type, SILValue DefaultValue,
    ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues, SILFunction &F,
    Optional<ArrayRef<ProfileCounter>> CaseCounts,
    ProfileCounter DefaultCount) {
  return createSelectEnum<SelectEnumAddrInst>(Loc, Operand, Type, DefaultValue,
                                              CaseValues, F, CaseCounts,
                                              DefaultCount);
}

SwitchEnumInstBase::SwitchEnumInstBase(
    SILInstructionKind Kind, SILDebugLocation Loc, SILValue Operand,
    SILBasicBlock *DefaultBB,
    ArrayRef<std::pair<EnumElementDecl *, SILBasicBlock *>> CaseBBs,
    Optional<ArrayRef<ProfileCounter>> CaseCounts, ProfileCounter DefaultCount)
    : TermInst(Kind, Loc), Operands(this, Operand), NumCases(CaseBBs.size()),
      HasDefault(bool(DefaultBB)) {
  // Initialize the case and successor arrays.
  auto *cases = getCaseBuf();
  auto *succs = getSuccessorBuf();
  for (unsigned i = 0, size = CaseBBs.size(); i < size; ++i) {
    cases[i] = CaseBBs[i].first;
    if (CaseCounts) {
      ::new (succs + i)
          SILSuccessor(this, CaseBBs[i].second, CaseCounts.getValue()[i]);
    } else {
      ::new (succs + i) SILSuccessor(this, CaseBBs[i].second);
    }
  }

  if (HasDefault) {
    ::new (succs + NumCases) SILSuccessor(this, DefaultBB, DefaultCount);
  }
}

void SwitchEnumInstBase::swapCase(unsigned i, unsigned j) {
  assert(i < getNumCases() && "First index is out of bounds?!");
  assert(j < getNumCases() && "Second index is out of bounds?!");

  auto *succs = getSuccessorBuf();

  // First grab our destination blocks.
  SILBasicBlock *iBlock = succs[i].getBB();
  SILBasicBlock *jBlock = succs[j].getBB();

  // Then destroy the sil successors and reinitialize them with the new things
  // that they are pointing at.
  succs[i].~SILSuccessor();
  ::new (succs + i) SILSuccessor(this, jBlock);
  succs[j].~SILSuccessor();
  ::new (succs + j) SILSuccessor(this, iBlock);

  // Now swap our cases.
  auto *cases = getCaseBuf();
  std::swap(cases[i], cases[j]);
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
} // end anonymous namespace

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
    SILFunction &F, Optional<ArrayRef<ProfileCounter>> CaseCounts,
    ProfileCounter DefaultCount) {
  // Allocate enough room for the instruction with tail-allocated
  // EnumElementDecl and SILSuccessor arrays. There are `CaseBBs.size()` decls
  // and `CaseBBs.size() + (DefaultBB ? 1 : 0)` successors.
  unsigned numCases = CaseBBs.size();
  unsigned numSuccessors = numCases + (DefaultBB ? 1 : 0);

  void *buf = F.getModule().allocateInst(
      sizeof(SWITCH_ENUM_INST) + sizeof(EnumElementDecl *) * numCases +
          sizeof(SILSuccessor) * numSuccessors,
      alignof(SWITCH_ENUM_INST));
  return ::new (buf) SWITCH_ENUM_INST(Loc, Operand, DefaultBB, CaseBBs,
                                      CaseCounts, DefaultCount);
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
    SILFunction &F, Optional<ArrayRef<ProfileCounter>> CaseCounts,
    ProfileCounter DefaultCount) {
  return createSwitchEnum<SwitchEnumInst>(Loc, Operand, DefaultBB, CaseBBs, F,
                                          CaseCounts, DefaultCount);
}

SwitchEnumAddrInst *SwitchEnumAddrInst::create(
    SILDebugLocation Loc, SILValue Operand, SILBasicBlock *DefaultBB,
    ArrayRef<std::pair<EnumElementDecl *, SILBasicBlock *>> CaseBBs,
    SILFunction &F, Optional<ArrayRef<ProfileCounter>> CaseCounts,
    ProfileCounter DefaultCount) {
  return createSwitchEnum<SwitchEnumAddrInst>(Loc, Operand, DefaultBB, CaseBBs,
                                              F, CaseCounts, DefaultCount);
}

DynamicMethodBranchInst::DynamicMethodBranchInst(SILDebugLocation Loc,
                                                 SILValue Operand,
                                                 SILDeclRef Member,
                                                 SILBasicBlock *HasMethodBB,
                                                 SILBasicBlock *NoMethodBB)
  : InstructionBase(Loc),
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

WitnessMethodInst *
WitnessMethodInst::create(SILDebugLocation Loc, CanType LookupType,
                          ProtocolConformanceRef Conformance, SILDeclRef Member,
                          SILType Ty, SILFunction *F,
                          SILOpenedArchetypesState &OpenedArchetypes) {
  assert(cast<ProtocolDecl>(Member.getDecl()->getDeclContext())
         == Conformance.getRequirement());

  SILModule &Mod = F->getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, *F,
                               LookupType);
  void *Buffer =
      Mod.allocateInst(sizeof(WitnessMethodInst) +
                           sizeof(Operand) * TypeDependentOperands.size(),
                       alignof(WitnessMethodInst));

  declareWitnessTable(Mod, Conformance);
  return ::new (Buffer) WitnessMethodInst(Loc, LookupType, Conformance, Member,
                                          Ty, TypeDependentOperands);
}

ObjCMethodInst *
ObjCMethodInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                       SILDeclRef Member, SILType Ty, SILFunction *F,
                       SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F->getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, *F,
                               Ty.getSwiftRValueType());

  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(ObjCMethodInst));
  return ::new (Buffer) ObjCMethodInst(DebugLoc, Operand,
                                       TypeDependentOperands,
                                       Member, Ty);
}

InitExistentialAddrInst *InitExistentialAddrInst::create(
    SILDebugLocation Loc, SILValue Existential, CanType ConcreteType,
    SILType ConcreteLoweredType, ArrayRef<ProtocolConformanceRef> Conformances,
    SILFunction *F, SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F->getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, *F,
                               ConcreteType);
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size,
                                  alignof(InitExistentialAddrInst));
  for (ProtocolConformanceRef C : Conformances)
    declareWitnessTable(Mod, C);
  return ::new (Buffer) InitExistentialAddrInst(Loc, Existential,
                                                TypeDependentOperands,
                                                ConcreteType,
                                                ConcreteLoweredType,
                                                Conformances);
}

InitExistentialValueInst *InitExistentialValueInst::create(
    SILDebugLocation Loc, SILType ExistentialType, CanType ConcreteType,
    SILValue Instance, ArrayRef<ProtocolConformanceRef> Conformances,
    SILFunction *F, SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F->getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, *F,
                               ConcreteType);
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());

  void *Buffer = Mod.allocateInst(size, alignof(InitExistentialRefInst));
  for (ProtocolConformanceRef C : Conformances)
    declareWitnessTable(Mod, C);

  return ::new (Buffer)
      InitExistentialValueInst(Loc, ExistentialType, ConcreteType, Instance,
                                TypeDependentOperands, Conformances);
}

InitExistentialRefInst *
InitExistentialRefInst::create(SILDebugLocation Loc, SILType ExistentialType,
                               CanType ConcreteType, SILValue Instance,
                               ArrayRef<ProtocolConformanceRef> Conformances,
                               SILFunction *F,
                               SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F->getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, *F,
                               ConcreteType);
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());

  void *Buffer = Mod.allocateInst(size,
                                  alignof(InitExistentialRefInst));
  for (ProtocolConformanceRef C : Conformances)
    declareWitnessTable(Mod, C);

  return ::new (Buffer) InitExistentialRefInst(Loc, ExistentialType,
                                               ConcreteType,
                                               Instance,
                                               TypeDependentOperands,
                                               Conformances);
}

InitExistentialMetatypeInst::InitExistentialMetatypeInst(
    SILDebugLocation Loc, SILType existentialMetatypeType, SILValue metatype,
    ArrayRef<SILValue> TypeDependentOperands,
    ArrayRef<ProtocolConformanceRef> conformances)
    : UnaryInstructionWithTypeDependentOperandsBase(Loc, metatype,
                                                    TypeDependentOperands,
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
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, *F,
                               existentialMetatypeType.getSwiftRValueType());

  unsigned size = totalSizeToAlloc<swift::Operand, ProtocolConformanceRef>(
      1 + TypeDependentOperands.size(), conformances.size());

  void *buffer = M.allocateInst(size, alignof(InitExistentialMetatypeInst));
  for (ProtocolConformanceRef conformance : conformances)
    declareWitnessTable(M, conformance);

  return ::new (buffer) InitExistentialMetatypeInst(
      Loc, existentialMetatypeType, metatype,
      TypeDependentOperands, conformances);
}

ArrayRef<ProtocolConformanceRef>
InitExistentialMetatypeInst::getConformances() const {
  return {getTrailingObjects<ProtocolConformanceRef>(), NumConformances};
}

MarkUninitializedBehaviorInst *
MarkUninitializedBehaviorInst::create(SILModule &M,
                                      SILDebugLocation DebugLoc,
                                      SILValue InitStorage,
                                      SubstitutionList InitStorageSubs,
                                      SILValue Storage,
                                      SILValue Setter,
                                      SubstitutionList SetterSubs,
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
                                        SubstitutionList InitStorageSubs,
                                        SILValue Storage,
                                        SILValue Setter,
                                        SubstitutionList SetterSubs,
                                        SILValue Self,
                                        SILType Ty)
  : InstructionBase(DebugLoc, Ty),
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

OpenedExistentialAccess swift::getOpenedExistentialAccessFor(AccessKind access) {
  switch (access) {
  case AccessKind::Read:
    return OpenedExistentialAccess::Immutable;
  case AccessKind::ReadWrite:
  case AccessKind::Write:
    return OpenedExistentialAccess::Mutable;
  }
  llvm_unreachable("Uncovered covered switch?");
}

OpenExistentialAddrInst::OpenExistentialAddrInst(
    SILDebugLocation DebugLoc, SILValue Operand, SILType SelfTy,
    OpenedExistentialAccess AccessKind)
    : UnaryInstructionBase(DebugLoc, Operand, SelfTy), ForAccess(AccessKind) {}

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

OpenExistentialBoxValueInst::OpenExistentialBoxValueInst(
    SILDebugLocation DebugLoc, SILValue operand, SILType ty)
    : UnaryInstructionBase(DebugLoc, operand, ty) {
}

OpenExistentialValueInst::OpenExistentialValueInst(SILDebugLocation DebugLoc,
                                                     SILValue Operand,
                                                     SILType SelfTy)
    : UnaryInstructionBase(DebugLoc, Operand, SelfTy) {}

UncheckedRefCastInst *
UncheckedRefCastInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                             SILType Ty, SILFunction &F,
                             SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               Ty.getSwiftRValueType());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(UncheckedRefCastInst));
  return ::new (Buffer) UncheckedRefCastInst(DebugLoc, Operand,
                                             TypeDependentOperands, Ty);
}

UncheckedAddrCastInst *
UncheckedAddrCastInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                              SILType Ty, SILFunction &F,
                              SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               Ty.getSwiftRValueType());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(UncheckedAddrCastInst));
  return ::new (Buffer) UncheckedAddrCastInst(DebugLoc, Operand,
                                              TypeDependentOperands, Ty);
}

UncheckedTrivialBitCastInst *
UncheckedTrivialBitCastInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                              SILType Ty, SILFunction &F,
                              SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               Ty.getSwiftRValueType());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(UncheckedTrivialBitCastInst));
  return ::new (Buffer) UncheckedTrivialBitCastInst(DebugLoc, Operand,
                                                    TypeDependentOperands,
                                                    Ty);
}

UncheckedBitwiseCastInst *
UncheckedBitwiseCastInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                                 SILType Ty, SILFunction &F,
                                 SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               Ty.getSwiftRValueType());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(UncheckedBitwiseCastInst));
  return ::new (Buffer) UncheckedBitwiseCastInst(DebugLoc, Operand,
                                                 TypeDependentOperands, Ty);
}

UnconditionalCheckedCastInst *UnconditionalCheckedCastInst::create(
    SILDebugLocation DebugLoc, SILValue Operand, SILType DestTy, SILFunction &F,
    SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               DestTy.getSwiftRValueType());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(UnconditionalCheckedCastInst));
  return ::new (Buffer) UnconditionalCheckedCastInst(DebugLoc, Operand,
                                                     TypeDependentOperands, DestTy);
}

UnconditionalCheckedCastValueInst *UnconditionalCheckedCastValueInst::create(
    SILDebugLocation DebugLoc, SILValue Operand, SILType DestTy, SILFunction &F,
    SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               DestTy.getSwiftRValueType());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer =
      Mod.allocateInst(size, alignof(UnconditionalCheckedCastValueInst));
  return ::new (Buffer) UnconditionalCheckedCastValueInst(
      DebugLoc, Operand, TypeDependentOperands, DestTy);
}

CheckedCastBranchInst *CheckedCastBranchInst::create(
    SILDebugLocation DebugLoc, bool IsExact, SILValue Operand, SILType DestTy,
    SILBasicBlock *SuccessBB, SILBasicBlock *FailureBB, SILFunction &F,
    SILOpenedArchetypesState &OpenedArchetypes, ProfileCounter Target1Count,
    ProfileCounter Target2Count) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               DestTy.getSwiftRValueType());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(CheckedCastBranchInst));
  return ::new (Buffer) CheckedCastBranchInst(
      DebugLoc, IsExact, Operand, TypeDependentOperands, DestTy, SuccessBB,
      FailureBB, Target1Count, Target2Count);
}

CheckedCastValueBranchInst *
CheckedCastValueBranchInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                                   SILType DestTy, SILBasicBlock *SuccessBB,
                                   SILBasicBlock *FailureBB, SILFunction &F,
                                   SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               DestTy.getSwiftRValueType());
  unsigned size =
      totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(CheckedCastValueBranchInst));
  return ::new (Buffer) CheckedCastValueBranchInst(
      DebugLoc, Operand, TypeDependentOperands, DestTy, SuccessBB, FailureBB);
}

MetatypeInst *MetatypeInst::create(SILDebugLocation Loc, SILType Ty,
                                   SILFunction *F,
                                   SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F->getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, *F,
                               Ty.castTo<MetatypeType>().getInstanceType());
  void *Buffer =
      Mod.allocateInst(sizeof(MetatypeInst) +
                           sizeof(Operand) * TypeDependentOperands.size(),
                       alignof(MetatypeInst));

  return ::new (Buffer) MetatypeInst(Loc, Ty, TypeDependentOperands);
}

UpcastInst *UpcastInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                               SILType Ty, SILFunction &F,
                               SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               Ty.getSwiftRValueType());
  unsigned size =
    totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(UpcastInst));
  return ::new (Buffer) UpcastInst(DebugLoc, Operand,
                                   TypeDependentOperands, Ty);
}

ThinToThickFunctionInst *
ThinToThickFunctionInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                                SILType Ty, SILFunction &F,
                                SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               Ty.getSwiftRValueType());
  unsigned size =
    totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(ThinToThickFunctionInst));
  return ::new (Buffer) ThinToThickFunctionInst(DebugLoc, Operand,
                                                TypeDependentOperands, Ty);
}

PointerToThinFunctionInst *
PointerToThinFunctionInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                                  SILType Ty, SILFunction &F,
                                  SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               Ty.getSwiftRValueType());
  unsigned size =
    totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(PointerToThinFunctionInst));
  return ::new (Buffer) PointerToThinFunctionInst(DebugLoc, Operand,
                                                  TypeDependentOperands, Ty);
}

ConvertFunctionInst *
ConvertFunctionInst::create(SILDebugLocation DebugLoc, SILValue Operand,
                            SILType Ty, SILFunction &F,
                            SILOpenedArchetypesState &OpenedArchetypes) {
  SILModule &Mod = F.getModule();
  SmallVector<SILValue, 8> TypeDependentOperands;
  collectTypeDependentOperands(TypeDependentOperands, OpenedArchetypes, F,
                               Ty.getSwiftRValueType());
  unsigned size =
    totalSizeToAlloc<swift::Operand>(1 + TypeDependentOperands.size());
  void *Buffer = Mod.allocateInst(size, alignof(ConvertFunctionInst));
  auto *CFI = ::new (Buffer)
      ConvertFunctionInst(DebugLoc, Operand, TypeDependentOperands, Ty);
  // Make sure we are not performing ABI-incompatible conversions.
  CanSILFunctionType opTI =
      CFI->getOperand()->getType().castTo<SILFunctionType>();
  (void)opTI;
  CanSILFunctionType resTI =
      CFI->getOperand()->getType().castTo<SILFunctionType>();
  (void)resTI;
  assert(opTI->isABICompatibleWith(resTI).isCompatible() &&
         "Can not convert in between ABI incompatible function types");
  return CFI;
}

bool KeyPathPatternComponent::isComputedSettablePropertyMutating() const {
  switch (getKind()) {
  case Kind::StoredProperty:
  case Kind::GettableProperty:
  case Kind::OptionalChain:
  case Kind::OptionalWrap:
  case Kind::OptionalForce:
    llvm_unreachable("not a settable computed property");
  case Kind::SettableProperty: {
    auto setter = getComputedPropertySetter();
    return setter->getLoweredFunctionType()->getParameters()[1].getConvention()
       == ParameterConvention::Indirect_Inout;
  }
  }
}

static void
forEachRefcountableReference(const KeyPathPatternComponent &component,
                         llvm::function_ref<void (SILFunction*)> forFunction) {
  switch (component.getKind()) {
  case KeyPathPatternComponent::Kind::StoredProperty:
  case KeyPathPatternComponent::Kind::OptionalChain:
  case KeyPathPatternComponent::Kind::OptionalWrap:
  case KeyPathPatternComponent::Kind::OptionalForce:
    return;
  case KeyPathPatternComponent::Kind::SettableProperty:
    forFunction(component.getComputedPropertySetter());
    LLVM_FALLTHROUGH;
  case KeyPathPatternComponent::Kind::GettableProperty:
    forFunction(component.getComputedPropertyGetter());
    
    switch (component.getComputedPropertyId().getKind()) {
    case KeyPathPatternComponent::ComputedPropertyId::DeclRef:
      // Mark the vtable entry as used somehow?
      break;
    case KeyPathPatternComponent::ComputedPropertyId::Function:
      forFunction(component.getComputedPropertyId().getFunction());
      break;
    case KeyPathPatternComponent::ComputedPropertyId::Property:
      break;
    }
    
    if (auto equals = component.getComputedPropertyIndexEquals())
      forFunction(equals);
    if (auto hash = component.getComputedPropertyIndexHash())
      forFunction(hash);
    return;
  }
}

void KeyPathPatternComponent::incrementRefCounts() const {
  forEachRefcountableReference(*this,
    [&](SILFunction *f) { f->incrementRefCount(); });
}
void KeyPathPatternComponent::decrementRefCounts() const {
  forEachRefcountableReference(*this,
                               [&](SILFunction *f) { f->decrementRefCount(); });
}

KeyPathPattern *
KeyPathPattern::get(SILModule &M, CanGenericSignature signature,
                    CanType rootType, CanType valueType,
                    ArrayRef<KeyPathPatternComponent> components,
                    StringRef objcString) {
  llvm::FoldingSetNodeID id;
  Profile(id, signature, rootType, valueType, components, objcString);
  
  void *insertPos;
  auto existing = M.KeyPathPatterns.FindNodeOrInsertPos(id, insertPos);
  if (existing)
    return existing;
  
  // Determine the number of operands.
  int maxOperandNo = -1;
  for (auto component : components) {
    switch (component.getKind()) {
    case KeyPathPatternComponent::Kind::StoredProperty:
    case KeyPathPatternComponent::Kind::OptionalChain:
    case KeyPathPatternComponent::Kind::OptionalWrap:
    case KeyPathPatternComponent::Kind::OptionalForce:
      break;
      
    case KeyPathPatternComponent::Kind::GettableProperty:
    case KeyPathPatternComponent::Kind::SettableProperty:
      for (auto &index : component.getComputedPropertyIndices()) {
        maxOperandNo = std::max(maxOperandNo, (int)index.Operand);
      }
    }
  }
  
  auto newPattern = KeyPathPattern::create(M, signature, rootType, valueType,
                                           components, objcString,
                                           maxOperandNo + 1);
  M.KeyPathPatterns.InsertNode(newPattern, insertPos);
  return newPattern;
}

KeyPathPattern *
KeyPathPattern::create(SILModule &M, CanGenericSignature signature,
                       CanType rootType, CanType valueType,
                       ArrayRef<KeyPathPatternComponent> components,
                       StringRef objcString,
                       unsigned numOperands) {
  auto totalSize = totalSizeToAlloc<KeyPathPatternComponent>(components.size());
  void *mem = M.allocate(totalSize, alignof(KeyPathPatternComponent));
  return ::new (mem) KeyPathPattern(signature, rootType, valueType,
                                    components, objcString, numOperands);
}

KeyPathPattern::KeyPathPattern(CanGenericSignature signature,
                               CanType rootType, CanType valueType,
                               ArrayRef<KeyPathPatternComponent> components,
                               StringRef objcString,
                               unsigned numOperands)
  : NumOperands(numOperands), NumComponents(components.size()),
    Signature(signature), RootType(rootType), ValueType(valueType),
    ObjCString(objcString)
{
  auto *componentsBuf = getTrailingObjects<KeyPathPatternComponent>();
  std::uninitialized_copy(components.begin(), components.end(),
                          componentsBuf);
}

ArrayRef<KeyPathPatternComponent>
KeyPathPattern::getComponents() const {
  return {getTrailingObjects<KeyPathPatternComponent>(), NumComponents};
}

void KeyPathPattern::Profile(llvm::FoldingSetNodeID &ID,
                             CanGenericSignature signature,
                             CanType rootType,
                             CanType valueType,
                             ArrayRef<KeyPathPatternComponent> components,
                             StringRef objcString) {
  ID.AddPointer(signature.getPointer());
  ID.AddPointer(rootType.getPointer());
  ID.AddPointer(valueType.getPointer());
  ID.AddString(objcString);
  
  for (auto &component : components) {
    ID.AddInteger((unsigned)component.getKind());
    switch (component.getKind()) {
    case KeyPathPatternComponent::Kind::OptionalForce:
    case KeyPathPatternComponent::Kind::OptionalWrap:
    case KeyPathPatternComponent::Kind::OptionalChain:
      break;
      
    case KeyPathPatternComponent::Kind::StoredProperty:
      ID.AddPointer(component.getStoredPropertyDecl());
      break;
      
    case KeyPathPatternComponent::Kind::SettableProperty:
      ID.AddPointer(component.getComputedPropertySetter());
      LLVM_FALLTHROUGH;
    case KeyPathPatternComponent::Kind::GettableProperty:
      ID.AddPointer(component.getComputedPropertyGetter());
      auto id = component.getComputedPropertyId();
      ID.AddInteger(id.getKind());
      switch (id.getKind()) {
      case KeyPathPatternComponent::ComputedPropertyId::DeclRef: {
        auto declRef = id.getDeclRef();
        ID.AddPointer(declRef.loc.getOpaqueValue());
        ID.AddInteger((unsigned)declRef.kind);
        ID.AddInteger(declRef.isCurried);
        ID.AddBoolean(declRef.Expansion);
        ID.AddBoolean(declRef.isCurried);
        ID.AddBoolean(declRef.isForeign);
        ID.AddBoolean(declRef.isDirectReference);
        ID.AddBoolean(declRef.defaultArgIndex);
        break;
      }
      case KeyPathPatternComponent::ComputedPropertyId::Function: {
        ID.AddPointer(id.getFunction());
        break;
      }
      case KeyPathPatternComponent::ComputedPropertyId::Property: {
        ID.AddPointer(id.getProperty());
        break;
      }
      }
      for (auto &index : component.getComputedPropertyIndices()) {
        ID.AddInteger(index.Operand);
        ID.AddPointer(index.FormalType.getPointer());
        ID.AddPointer(index.LoweredType.getOpaqueValue());
        ID.AddPointer(index.Hashable.getOpaqueValue());
      }
      break;
    }
  }
}

KeyPathInst *
KeyPathInst::create(SILDebugLocation Loc,
                    KeyPathPattern *Pattern,
                    SubstitutionList Subs,
                    ArrayRef<SILValue> Args,
                    SILType Ty,
                    SILFunction &F) {
  assert(Args.size() == Pattern->getNumOperands()
         && "number of key path args doesn't match pattern");

  auto totalSize = totalSizeToAlloc<Substitution, Operand>
    (Subs.size(), Args.size());
  void *mem = F.getModule().allocateInst(totalSize, alignof(Substitution));
  return ::new (mem) KeyPathInst(Loc, Pattern, Subs, Args, Ty);
}

KeyPathInst::KeyPathInst(SILDebugLocation Loc,
                         KeyPathPattern *Pattern,
                         SubstitutionList Subs,
                         ArrayRef<SILValue> Args,
                         SILType Ty)
  : InstructionBase(Loc, Ty),
    Pattern(Pattern), NumSubstitutions(Subs.size()),
    NumOperands(Pattern->getNumOperands())
{
  auto *subsBuf = getTrailingObjects<Substitution>();
  std::uninitialized_copy(Subs.begin(), Subs.end(), subsBuf);
  
  auto *operandsBuf = getTrailingObjects<Operand>();
  for (unsigned i = 0; i < Args.size(); ++i) {
    ::new ((void*)&operandsBuf[i]) Operand(this, Args[i]);
  }
  
  // Increment the use of any functions referenced from the keypath pattern.
  for (auto component : Pattern->getComponents()) {
    component.incrementRefCounts();
  }
}

MutableArrayRef<Substitution>
KeyPathInst::getSubstitutions() {
  return {getTrailingObjects<Substitution>(), NumSubstitutions};
}

MutableArrayRef<Operand>
KeyPathInst::getAllOperands() {
  return {getTrailingObjects<Operand>(), NumOperands};
}

KeyPathInst::~KeyPathInst() {
  if (!Pattern)
    return;

  // Decrement the use of any functions referenced from the keypath pattern.
  for (auto component : Pattern->getComponents()) {
    component.decrementRefCounts();
  }
  // Destroy operands.
  for (auto &operand : getAllOperands())
    operand.~Operand();
}

KeyPathPattern *KeyPathInst::getPattern() const {
  assert(Pattern && "pattern was reset!");
  return Pattern;
}

void KeyPathInst::dropReferencedPattern() {
  for (auto component : Pattern->getComponents()) {
    component.decrementRefCounts();
  }
  Pattern = nullptr;
}

GenericSpecializationInformation::GenericSpecializationInformation(
    SILFunction *Caller, SILFunction *Parent, SubstitutionList Subs)
    : Caller(Caller), Parent(Parent), Subs(Subs) {}

const GenericSpecializationInformation *
GenericSpecializationInformation::create(SILFunction *Caller,
                                         SILFunction *Parent,
                                         SubstitutionList Subs) {
  auto &M = Parent->getModule();
  void *Buf = M.allocate(sizeof(GenericSpecializationInformation),
                           alignof(GenericSpecializationInformation));
  auto NewSubs = M.allocateCopy(Subs);
  return new (Buf) GenericSpecializationInformation(Caller, Parent, NewSubs);
}

const GenericSpecializationInformation *
GenericSpecializationInformation::create(SILInstruction *Inst, SILBuilder &B) {
  auto Apply = ApplySite::isa(Inst);
  // Preserve history only for apply instructions for now.
  // NOTE: We may want to preserve history for all instructions in the future,
  // because it may allow us to track their origins.
  assert(Apply);
  auto *F = Inst->getFunction();
  auto &BuilderF = B.getFunction();

  // If cloning inside the same function, don't change the specialization info.
  if (F == &BuilderF) {
    return Apply.getSpecializationInfo();
  }

  // The following lines are used in case of inlining.

  // If a call-site has a history already, simply preserve it.
  if (Apply.getSpecializationInfo())
    return Apply.getSpecializationInfo();

  // If a call-site has no history, use the history of a containing function.
  if (F->isSpecialization())
    return F->getSpecializationInfo();

  return nullptr;
}

static void computeAggregateFirstLevelSubtypeInfo(
    SILModule &M, SILValue Operand, llvm::SmallVectorImpl<SILType> &Types,
    llvm::SmallVectorImpl<ValueOwnershipKind> &OwnershipKinds) {
  SILType OpType = Operand->getType();

  // TODO: Create an iterator for accessing first level projections to eliminate
  // this SmallVector.
  llvm::SmallVector<Projection, 8> Projections;
  Projection::getFirstLevelProjections(OpType, M, Projections);

  auto OpOwnershipKind = Operand.getOwnershipKind();
  for (auto &P : Projections) {
    SILType ProjType = P.getType(OpType, M);
    Types.emplace_back(ProjType);
    OwnershipKinds.emplace_back(
        OpOwnershipKind.getProjectedOwnershipKind(M, ProjType));
  }
}

DestructureStructInst *DestructureStructInst::create(SILModule &M,
                                                     SILDebugLocation Loc,
                                                     SILValue Operand) {
  assert(Operand->getType().getStructOrBoundGenericStruct() &&
         "Expected a struct typed operand?!");

  llvm::SmallVector<SILType, 8> Types;
  llvm::SmallVector<ValueOwnershipKind, 8> OwnershipKinds;
  computeAggregateFirstLevelSubtypeInfo(M, Operand, Types, OwnershipKinds);
  assert(Types.size() == OwnershipKinds.size() &&
         "Expected same number of Types and OwnerKinds");

  unsigned NumElts = Types.size();
  unsigned Size =
      totalSizeToAlloc<MultipleValueInstruction *, DestructureStructResult>(
          1, NumElts);

  void *Buffer = M.allocateInst(Size, alignof(DestructureStructInst));

  return ::new (Buffer)
      DestructureStructInst(M, Loc, Operand, Types, OwnershipKinds);
}

DestructureTupleInst *DestructureTupleInst::create(SILModule &M,
                                                   SILDebugLocation Loc,
                                                   SILValue Operand) {
  assert(Operand->getType().getSwiftRValueType()->is<TupleType>() &&
         "Expected a tuple typed operand?!");

  llvm::SmallVector<SILType, 8> Types;
  llvm::SmallVector<ValueOwnershipKind, 8> OwnershipKinds;
  computeAggregateFirstLevelSubtypeInfo(M, Operand, Types, OwnershipKinds);
  assert(Types.size() == OwnershipKinds.size() &&
         "Expected same number of Types and OwnerKinds");

  // We add 1 since we store an offset to our
  unsigned NumElts = Types.size();
  unsigned Size =
      totalSizeToAlloc<MultipleValueInstruction *, DestructureTupleResult>(
          1, NumElts);

  void *Buffer = M.allocateInst(Size, alignof(DestructureTupleInst));

  return ::new (Buffer)
      DestructureTupleInst(M, Loc, Operand, Types, OwnershipKinds);
}
