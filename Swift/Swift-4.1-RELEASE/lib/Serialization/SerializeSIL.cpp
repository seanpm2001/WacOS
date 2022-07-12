//===--- SerializeSIL.cpp - Read and write SIL ----------------------------===//
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

#define DEBUG_TYPE "sil-serialize"
#include "SILFormat.h"
#include "Serialization.h"
#include "swift/Strings.h"
#include "swift/AST/Module.h"
#include "swift/AST/GenericSignature.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/SIL/CFG.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILUndef.h"
#include "swift/SILOptimizer/Utils/Generics.h"

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/OnDiskHashTable.h"

#include <type_traits>

using namespace swift;
using namespace swift::serialization;
using namespace swift::serialization::sil_block;
using namespace llvm::support;
using llvm::BCBlockRAII;

static unsigned toStableStringEncoding(StringLiteralInst::Encoding encoding) {
  switch (encoding) {
  case StringLiteralInst::Encoding::UTF8: return SIL_UTF8;
  case StringLiteralInst::Encoding::UTF16: return SIL_UTF16;
  case StringLiteralInst::Encoding::ObjCSelector: return SIL_OBJC_SELECTOR;
  }
  llvm_unreachable("bad string encoding");
}

static unsigned
toStableConstStringEncoding(ConstStringLiteralInst::Encoding encoding) {
  switch (encoding) {
  case ConstStringLiteralInst::Encoding::UTF8:
    return SIL_UTF8;
  case ConstStringLiteralInst::Encoding::UTF16:
    return SIL_UTF16;
  }
  llvm_unreachable("bad string encoding");
}

static unsigned toStableSILLinkage(SILLinkage linkage) {
  switch (linkage) {
  case SILLinkage::Public: return SIL_LINKAGE_PUBLIC;
  case SILLinkage::Hidden: return SIL_LINKAGE_HIDDEN;
  case SILLinkage::Shared: return SIL_LINKAGE_SHARED;
  case SILLinkage::Private: return SIL_LINKAGE_PRIVATE;
  case SILLinkage::PublicExternal: return SIL_LINKAGE_PUBLIC_EXTERNAL;
  case SILLinkage::HiddenExternal: return SIL_LINKAGE_HIDDEN_EXTERNAL;
  case SILLinkage::SharedExternal: return SIL_LINKAGE_SHARED_EXTERNAL;
  case SILLinkage::PrivateExternal: return SIL_LINKAGE_PRIVATE_EXTERNAL;
  }
  llvm_unreachable("bad linkage");
}

static unsigned toStableVTableEntryKind(SILVTable::Entry::Kind kind) {
  switch (kind) {
  case SILVTable::Entry::Kind::Normal: return SIL_VTABLE_ENTRY_NORMAL;
  case SILVTable::Entry::Kind::Inherited: return SIL_VTABLE_ENTRY_INHERITED;
  case SILVTable::Entry::Kind::Override: return SIL_VTABLE_ENTRY_OVERRIDE;
  }
  llvm_unreachable("bad vtable entry kind");
}

static unsigned toStableCastConsumptionKind(CastConsumptionKind kind) {
  switch (kind) {
  case CastConsumptionKind::TakeAlways:
    return SIL_CAST_CONSUMPTION_TAKE_ALWAYS;
  case CastConsumptionKind::TakeOnSuccess:
    return SIL_CAST_CONSUMPTION_TAKE_ON_SUCCESS;
  case CastConsumptionKind::CopyOnSuccess:
    return SIL_CAST_CONSUMPTION_COPY_ON_SUCCESS;
  }
  llvm_unreachable("bad cast consumption kind");
}

namespace {
    /// Used to serialize the on-disk func hash table.
  class FuncTableInfo {
  public:
    using key_type = Identifier;
    using key_type_ref = key_type;
    using data_type = DeclID;
    using data_type_ref = const data_type &;
    using hash_value_type = uint32_t;
    using offset_type = unsigned;

    hash_value_type ComputeHash(key_type_ref key) {
      assert(!key.empty());
      return llvm::HashString(key.str());
    }

    std::pair<unsigned, unsigned> EmitKeyDataLength(raw_ostream &out,
                                                    key_type_ref key,
                                                    data_type_ref data) {
      uint32_t keyLength = key.str().size();
      uint32_t dataLength = sizeof(uint32_t);
      endian::Writer<little> writer(out);
      writer.write<uint16_t>(keyLength);
      writer.write<uint16_t>(dataLength);
      return { keyLength, dataLength };
    }

    void EmitKey(raw_ostream &out, key_type_ref key, unsigned len) {
      out << key.str();
    }

    void EmitData(raw_ostream &out, key_type_ref key, data_type_ref data,
                  unsigned len) {
      endian::Writer<little>(out).write<uint32_t>(data);
    }
  };

  class SILSerializer {
    Serializer &S;
    ASTContext &Ctx;

    llvm::BitstreamWriter &Out;

    /// A reusable buffer for emitting records.
    SmallVector<uint64_t, 64> ScratchRecord;

    /// In case we want to encode the relative of InstID vs ValueID.
    uint32_t /*ValueID*/ InstID = 0;

    llvm::DenseMap<const ValueBase*, ValueID> ValueIDs;
    ValueID addValueRef(const ValueBase *Val);

  public:
    using TableData = FuncTableInfo::data_type;
    using Table = llvm::MapVector<FuncTableInfo::key_type, TableData>;
  private:
    /// FuncTable maps function name to an ID.
    Table FuncTable;
    std::vector<BitOffset> Funcs;
    /// The current function ID.
    uint32_t /*DeclID*/ NextFuncID = 1;

    /// Maps class name to a VTable ID.
    Table VTableList;
    /// Holds the list of VTables.
    std::vector<BitOffset> VTableOffset;
    uint32_t /*DeclID*/ NextVTableID = 1;

    /// Maps global variable name to an ID.
    Table GlobalVarList;
    /// Holds the list of SIL global variables.
    std::vector<BitOffset> GlobalVarOffset;
    uint32_t /*DeclID*/ NextGlobalVarID = 1;

    /// Maps witness table identifier to an ID.
    Table WitnessTableList;
    /// Holds the list of WitnessTables.
    std::vector<BitOffset> WitnessTableOffset;
    uint32_t /*DeclID*/ NextWitnessTableID = 1;

    /// Maps default witness table identifier to an ID.
    Table DefaultWitnessTableList;
    /// Holds the list of DefaultWitnessTables.
    std::vector<BitOffset> DefaultWitnessTableOffset;
    uint32_t /*DeclID*/ NextDefaultWitnessTableID = 1;

    /// Give each SILBasicBlock a unique ID.
    llvm::DenseMap<const SILBasicBlock *, unsigned> BasicBlockMap;

    /// Functions that we've emitted a reference to. If the key maps
    /// to true, we want to emit a declaration only.
    llvm::DenseMap<const SILFunction *, bool> FuncsToEmit;

    /// Additional functions we might need to serialize.
    llvm::SmallVector<const SILFunction *, 16> Worklist;

    std::array<unsigned, 256> SILAbbrCodes;
    template <typename Layout>
    void registerSILAbbr() {
      using AbbrArrayTy = decltype(SILAbbrCodes);
      static_assert(Layout::Code <= std::tuple_size<AbbrArrayTy>::value,
                    "layout has invalid record code");
      SILAbbrCodes[Layout::Code] = Layout::emitAbbrev(Out);
      DEBUG(llvm::dbgs() << "SIL abbre code " << SILAbbrCodes[Layout::Code]
                         << " for layout " << Layout::Code << "\n");
    }

    bool ShouldSerializeAll;

    void addMandatorySILFunction(const SILFunction *F,
                                 bool emitDeclarationsForOnoneSupport);
    void addReferencedSILFunction(const SILFunction *F,
                                  bool DeclOnly = false);
    void processSILFunctionWorklist();

    /// Helper function to update ListOfValues for MethodInst. Format:
    /// Attr, SILDeclRef (DeclID, Kind, uncurryLevel), and an operand.
    void handleMethodInst(const MethodInst *MI, SILValue operand,
                          SmallVectorImpl<ValueID> &ListOfValues);

    void writeSILFunction(const SILFunction &F, bool DeclOnly = false);
    void writeSILBasicBlock(const SILBasicBlock &BB);
    void writeSILInstruction(const SILInstruction &SI);
    void writeSILVTable(const SILVTable &vt);
    void writeSILGlobalVar(const SILGlobalVariable &g);
    void writeSILWitnessTable(const SILWitnessTable &wt);
    void writeSILDefaultWitnessTable(const SILDefaultWitnessTable &wt);

    void writeSILBlock(const SILModule *SILMod);
    void writeIndexTables();

    void writeConversionLikeInstruction(const SingleValueInstruction *I);
    void writeOneTypeLayout(SILInstructionKind valueKind, SILType type);
    void writeOneTypeOneOperandLayout(SILInstructionKind valueKind,
                                      unsigned attrs,
                                      SILType type,
                                      SILValue operand);
    void writeOneTypeOneOperandLayout(SILInstructionKind valueKind,
                                      unsigned attrs,
                                      CanType type,
                                      SILValue operand);
    void writeOneOperandLayout(SILInstructionKind valueKind,
                               unsigned attrs,
                               SILValue operand);

    /// Helper function to determine if given the current state of the
    /// deserialization if the function body for F should be deserialized.
    bool shouldEmitFunctionBody(const SILFunction *F, bool isReference = true);

    IdentifierID addSILFunctionRef(SILFunction *F);

  public:
    SILSerializer(Serializer &S, ASTContext &Ctx,
                  llvm::BitstreamWriter &Out, bool serializeAll)
      : S(S), Ctx(Ctx), Out(Out), ShouldSerializeAll(serializeAll) {}

    void writeSILModule(const SILModule *SILMod);
  };
} // end anonymous namespace

void SILSerializer::addMandatorySILFunction(const SILFunction *F,
                                            bool emitDeclarationsForOnoneSupport) {
  // If this function is not fragile, don't do anything.
  if (!emitDeclarationsForOnoneSupport &&
      !shouldEmitFunctionBody(F, /* isReference */ false))
    return;

  auto iter = FuncsToEmit.find(F);
  if (iter != FuncsToEmit.end()) {
    // We've already visited this function. Make sure that we decided
    // to emit its body the first time around.
    assert(iter->second == emitDeclarationsForOnoneSupport
           && "Already emitting declaration");
    return;
  }

  // We haven't seen this function before. Record that we want to
  // emit its body, and add it to the worklist.
  FuncsToEmit[F] = emitDeclarationsForOnoneSupport;

  // Function body should be serialized unless it is a KeepAsPublic function
  // (which is typically a pre-specialization).
  if (!emitDeclarationsForOnoneSupport && !F->isKeepAsPublic())
    Worklist.push_back(F);
}

void SILSerializer::addReferencedSILFunction(const SILFunction *F,
                                             bool DeclOnly) {
  assert(F != nullptr);

  if (FuncsToEmit.count(F) > 0)
    return;

  // We haven't seen this function before. Let's see if we should
  // serialize the body or just the declaration.
  if (shouldEmitFunctionBody(F)) {
    FuncsToEmit[F] = false;
    Worklist.push_back(F);
    return;
  }

  if (F->getLinkage() == SILLinkage::Shared && !DeclOnly) {
    assert(F->isSerialized() == IsSerializable ||
           F->hasForeignBody());

    FuncsToEmit[F] = false;
    Worklist.push_back(F);
    return;
  }

  // Ok, we just need to emit a declaration.
  FuncsToEmit[F] = true;
}

void SILSerializer::processSILFunctionWorklist() {
  while (Worklist.size() > 0) {
    const SILFunction *F = Worklist.back();
    Worklist.pop_back();
    assert(F != nullptr);

    assert(FuncsToEmit.count(F) > 0);
    writeSILFunction(*F, FuncsToEmit[F]);
  }
}

/// We enumerate all values in a SILFunction beforehand to correctly
/// handle forward references of values.
ValueID SILSerializer::addValueRef(const ValueBase *Val) {
  if (!Val || isa<SILUndef>(Val))
    return 0;

  ValueID id = ValueIDs[Val];
  assert(id != 0 && "We should have assigned a value ID to each value.");
  return id;
}

void SILSerializer::writeSILFunction(const SILFunction &F, bool DeclOnly) {
  ValueIDs.clear();
  InstID = 0;

  FuncTable[Ctx.getIdentifier(F.getName())] = NextFuncID++;
  Funcs.push_back(Out.GetCurrentBitNo());
  unsigned abbrCode = SILAbbrCodes[SILFunctionLayout::Code];
  TypeID FnID = S.addTypeRef(F.getLoweredType().getSwiftRValueType());
  DEBUG(llvm::dbgs() << "SILFunction " << F.getName() << " @ BitNo "
                     << Out.GetCurrentBitNo() << " abbrCode " << abbrCode
                     << " FnID " << FnID << "\n");
  DEBUG(llvm::dbgs() << "Serialized SIL:\n"; F.dump());

  SmallVector<IdentifierID, 1> SemanticsIDs;
  for (auto SemanticAttr : F.getSemanticsAttrs()) {
    SemanticsIDs.push_back(S.addDeclBaseNameRef(Ctx.getIdentifier(SemanticAttr)));
  }

  SILLinkage Linkage = F.getLinkage();

  // We serialize shared_external linkage as shared since:
  //
  // 1. shared_external linkage is just a hack to tell the optimizer that a
  // shared function was deserialized.
  //
  // 2. We cannot just serialize a declaration to a shared_external function
  // since shared_external functions still have linkonce_odr linkage at the LLVM
  // level. This means they must be defined not just declared.
  //
  // TODO: When serialization is reworked, this should be removed.
  if (hasSharedVisibility(Linkage))
    Linkage = SILLinkage::Shared;

  // Check if we need to emit a body for this function.
  bool NoBody = DeclOnly || isAvailableExternally(Linkage) ||
                F.isExternalDeclaration();

  // If we don't emit a function body then make sure to mark the declaration
  // as available externally.
  if (NoBody) {
    Linkage = addExternalToLinkage(Linkage);
  }

  // If we have a body, we might have a generic environment.
  GenericEnvironmentID genericEnvID = 0;
  if (!NoBody)
    genericEnvID = S.addGenericEnvironmentRef(F.getGenericEnvironment());

  DeclID clangNodeOwnerID;
  if (F.hasClangNode())
    clangNodeOwnerID = S.addDeclRef(F.getClangNodeOwner());

  unsigned numSpecAttrs = NoBody ? 0 : F.getSpecializeAttrs().size();
  SILFunctionLayout::emitRecord(
      Out, ScratchRecord, abbrCode, toStableSILLinkage(Linkage),
      (unsigned)F.isTransparent(), (unsigned)F.isSerialized(),
      (unsigned)F.isThunk(), (unsigned)F.isGlobalInit(),
      (unsigned)F.getInlineStrategy(), (unsigned)F.getOptimizationMode(),
      (unsigned)F.getEffectsKind(),
      (unsigned)numSpecAttrs, (unsigned)F.hasQualifiedOwnership(), FnID,
      genericEnvID, clangNodeOwnerID, SemanticsIDs);

  if (NoBody)
    return;

  for (auto *SA : F.getSpecializeAttrs()) {
    unsigned specAttrAbbrCode = SILAbbrCodes[SILSpecializeAttrLayout::Code];
    SILSpecializeAttrLayout::emitRecord(Out, ScratchRecord, specAttrAbbrCode,
                                        (unsigned)SA->isExported(),
                                        (unsigned)SA->getSpecializationKind());
    S.writeGenericRequirements(SA->getRequirements(), SILAbbrCodes);
  }

  // Assign a unique ID to each basic block of the SILFunction.
  unsigned BasicID = 0;
  BasicBlockMap.clear();
  // Assign a value ID to each SILInstruction that has value and to each basic
  // block argument.
  unsigned ValueID = 0;
  llvm::ReversePostOrderTraversal<SILFunction *> RPOT(
      const_cast<SILFunction *>(&F));
  for (auto Iter = RPOT.begin(), E = RPOT.end(); Iter != E; ++Iter) {
    auto &BB = **Iter;
    BasicBlockMap.insert(std::make_pair(&BB, BasicID++));

    for (auto I = BB.args_begin(), E = BB.args_end(); I != E; ++I)
      ValueIDs[static_cast<const ValueBase*>(*I)] = ++ValueID;

    for (const SILInstruction &SI : BB)
      for (auto result : SI.getResults())
        ValueIDs[result] = ++ValueID;
  }

  // Write SIL basic blocks in the RPOT order
  // to make sure that instructions defining open archetypes
  // are serialized before instructions using those opened
  // archetypes.
  unsigned SerializedBBNum = 0;
  for (auto Iter = RPOT.begin(), E = RPOT.end(); Iter != E; ++Iter) {
    auto *BB = *Iter;
    writeSILBasicBlock(*BB);
    SerializedBBNum++;
  }
  assert(BasicID == SerializedBBNum && "Wrong number of BBs was serialized");
}

void SILSerializer::writeSILBasicBlock(const SILBasicBlock &BB) {
  SmallVector<DeclID, 4> Args;
  for (auto I = BB.args_begin(), E = BB.args_end(); I != E; ++I) {
    SILArgument *SA = *I;
    DeclID tId = S.addTypeRef(SA->getType().getSwiftRValueType());
    DeclID vId = addValueRef(static_cast<const ValueBase*>(SA));
    Args.push_back(tId);

    // We put these static asserts here to formalize our assumption that both
    // SILValueCategory and ValueOwnershipKind have uint8_t as their underlying
    // pointer values.
    static_assert(
        std::is_same<
            std::underlying_type<decltype(SA->getType().getCategory())>::type,
            uint8_t>::value,
        "Expected an underlying uint8_t type");
    // We put these static asserts here to formalize our assumption that both
    // SILValueCategory and ValueOwnershipKind have uint8_t as their underlying
    // pointer values.
    static_assert(std::is_same<std::underlying_type<decltype(
                                   SA->getOwnershipKind())::innerty>::type,
                               uint8_t>::value,
                  "Expected an underlying uint8_t type");
    unsigned packedMetadata = 0;
    packedMetadata |= unsigned(SA->getType().getCategory());
    packedMetadata |= unsigned(SA->getOwnershipKind()) << 8;
    Args.push_back(packedMetadata);

    Args.push_back(vId);
  }

  unsigned abbrCode = SILAbbrCodes[SILBasicBlockLayout::Code];
  SILBasicBlockLayout::emitRecord(Out, ScratchRecord, abbrCode, Args);

  for (const SILInstruction &SI : BB)
    writeSILInstruction(SI);
}

/// Add SILDeclRef to ListOfValues, so we can reconstruct it at
/// deserialization.
static void handleSILDeclRef(Serializer &S, const SILDeclRef &Ref,
                             SmallVectorImpl<ValueID> &ListOfValues) {
  ListOfValues.push_back(S.addDeclRef(Ref.getDecl()));
  ListOfValues.push_back((unsigned)Ref.kind);
  ListOfValues.push_back((unsigned)Ref.getResilienceExpansion());
  ListOfValues.push_back(Ref.getParameterListCount() - 1);
  ListOfValues.push_back(Ref.isForeign);
}

/// Get an identifier ref for a SILFunction and add it to the list of referenced
/// functions.
IdentifierID SILSerializer::addSILFunctionRef(SILFunction *F) {
  addReferencedSILFunction(F);
  return S.addDeclBaseNameRef(Ctx.getIdentifier(F->getName()));
}

/// Helper function to update ListOfValues for MethodInst. Format:
/// Attr, SILDeclRef (DeclID, Kind, uncurryLevel), and an operand.
void SILSerializer::handleMethodInst(const MethodInst *MI,
                                     SILValue operand,
                                     SmallVectorImpl<ValueID> &ListOfValues) {
  handleSILDeclRef(S, MI->getMember(), ListOfValues);
  ListOfValues.push_back(
      S.addTypeRef(operand->getType().getSwiftRValueType()));
  ListOfValues.push_back((unsigned)operand->getType().getCategory());
  ListOfValues.push_back(addValueRef(operand));
}

void SILSerializer::writeOneTypeLayout(SILInstructionKind valueKind,
                                       SILType type) {
  unsigned abbrCode = SILAbbrCodes[SILOneTypeLayout::Code];
  SILOneTypeLayout::emitRecord(Out, ScratchRecord, abbrCode,
        (unsigned) valueKind,
        S.addTypeRef(type.getSwiftRValueType()),
        (unsigned)type.getCategory());
}

void SILSerializer::writeOneOperandLayout(SILInstructionKind valueKind,
                                          unsigned attrs,
                                          SILValue operand) {

  auto operandType = operand->getType();
  auto operandTypeRef = S.addTypeRef(operandType.getSwiftRValueType());
  auto operandRef = addValueRef(operand);

  SILOneOperandLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneOperandLayout::Code],
        unsigned(valueKind), attrs,
        operandTypeRef, unsigned(operandType.getCategory()),
        operandRef);
}

void SILSerializer::writeOneTypeOneOperandLayout(SILInstructionKind valueKind,
                                                 unsigned attrs,
                                                 SILType type,
                                                 SILValue operand) {
  auto typeRef = S.addTypeRef(type.getSwiftRValueType());
  auto operandType = operand->getType();
  auto operandTypeRef = S.addTypeRef(operandType.getSwiftRValueType());
  auto operandRef = addValueRef(operand);

  SILOneTypeOneOperandLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeOneOperandLayout::Code],
        unsigned(valueKind), attrs,
        typeRef, unsigned(type.getCategory()),
        operandTypeRef, unsigned(operandType.getCategory()),
        operandRef);
}
void SILSerializer::writeOneTypeOneOperandLayout(SILInstructionKind valueKind,
                                                 unsigned attrs,
                                                 CanType type,
                                                 SILValue operand) {
  auto typeRef = S.addTypeRef(type);
  auto operandType = operand->getType();
  auto operandTypeRef = S.addTypeRef(operandType.getSwiftRValueType());
  auto operandRef = addValueRef(operand);

  SILOneTypeOneOperandLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeOneOperandLayout::Code],
        unsigned(valueKind), attrs,
        typeRef, 0,
        operandTypeRef, unsigned(operandType.getCategory()),
        operandRef);
}

/// Write an instruction that looks exactly like a conversion: all
/// important information is encoded in the operand and the result type.
void SILSerializer::writeConversionLikeInstruction(const SingleValueInstruction *I) {
  assert(I->getNumOperands() - I->getTypeDependentOperands().size() == 1);
  writeOneTypeOneOperandLayout(I->getKind(), 0, I->getType(),
                               I->getOperand(0));
}

void SILSerializer::writeSILInstruction(const SILInstruction &SI) {
  switch (SI.getKind()) {
  case SILInstructionKind::ObjectInst:
    llvm_unreachable("static initializers of sil_global are not serialized");

  case SILInstructionKind::DebugValueInst:
  case SILInstructionKind::DebugValueAddrInst:
    // Currently we don't serialize debug variable infos, so it doesn't make
    // sense to write the instruction at all.
    // TODO: decide if we want to serialize those instructions.
    return;
      
  case SILInstructionKind::UnwindInst:
  case SILInstructionKind::UnreachableInst: {
    unsigned abbrCode = SILAbbrCodes[SILInstNoOperandLayout::Code];
    SILInstNoOperandLayout::emitRecord(Out, ScratchRecord, abbrCode,
                                       (unsigned)SI.getKind());
    break;
  }
  case SILInstructionKind::AllocExistentialBoxInst:
  case SILInstructionKind::InitExistentialAddrInst:
  case SILInstructionKind::InitExistentialValueInst:
  case SILInstructionKind::InitExistentialMetatypeInst:
  case SILInstructionKind::InitExistentialRefInst: {
    SILValue operand;
    SILType Ty;
    CanType FormalConcreteType;
    ArrayRef<ProtocolConformanceRef> conformances;

    switch (SI.getKind()) {
    default: llvm_unreachable("out of sync with parent");
    case SILInstructionKind::InitExistentialAddrInst: {
      auto &IEI = cast<InitExistentialAddrInst>(SI);
      operand = IEI.getOperand();
      Ty = IEI.getLoweredConcreteType();
      FormalConcreteType = IEI.getFormalConcreteType();
      conformances = IEI.getConformances();
      break;
    }
    case SILInstructionKind::InitExistentialValueInst: {
      auto &IEOI = cast<InitExistentialValueInst>(SI);
      operand = IEOI.getOperand();
      Ty = IEOI.getType();
      FormalConcreteType = IEOI.getFormalConcreteType();
      conformances = IEOI.getConformances();
      break;
    }
    case SILInstructionKind::InitExistentialRefInst: {
      auto &IERI = cast<InitExistentialRefInst>(SI);
      operand = IERI.getOperand();
      Ty = IERI.getType();
      FormalConcreteType = IERI.getFormalConcreteType();
      conformances = IERI.getConformances();
      break;
    }
    case SILInstructionKind::InitExistentialMetatypeInst: {
      auto &IEMI = cast<InitExistentialMetatypeInst>(SI);
      operand = IEMI.getOperand();
      Ty = IEMI.getType();
      conformances = IEMI.getConformances();
      break;
    }
    case SILInstructionKind::AllocExistentialBoxInst: {
      auto &AEBI = cast<AllocExistentialBoxInst>(SI);
      Ty = AEBI.getExistentialType();
      FormalConcreteType = AEBI.getFormalConcreteType();
      conformances = AEBI.getConformances();
      break;
    }
    }

    TypeID operandType = 0;
    SILValueCategory operandCategory = SILValueCategory::Object;
    ValueID operandID = 0;
    if (operand) {
      operandType = S.addTypeRef(operand->getType().getSwiftRValueType());
      operandCategory = operand->getType().getCategory();
      operandID = addValueRef(operand);
    }

    unsigned abbrCode = SILAbbrCodes[SILInitExistentialLayout::Code];
    SILInitExistentialLayout::emitRecord(Out, ScratchRecord, abbrCode,
       (unsigned)SI.getKind(),
       S.addTypeRef(Ty.getSwiftRValueType()),
       (unsigned)Ty.getCategory(),
       operandType,
       (unsigned)operandCategory,
       operandID,
       S.addTypeRef(FormalConcreteType),
       conformances.size());

    for (auto conformance : conformances) {
      S.writeConformance(conformance, SILAbbrCodes);
    }
    break;
  }
  case SILInstructionKind::DeallocValueBufferInst: {
    auto DVBI = cast<DeallocValueBufferInst>(&SI);
    writeOneTypeOneOperandLayout(DVBI->getKind(), 0,
                                 DVBI->getValueType(),
                                 DVBI->getOperand());
    break;
  }
  case SILInstructionKind::DeallocBoxInst: {
    auto DBI = cast<DeallocBoxInst>(&SI);
    writeOneTypeOneOperandLayout(DBI->getKind(), 0,
                                 DBI->getOperand()->getType(),
                                 DBI->getOperand());
    break;
  }
  case SILInstructionKind::DeallocExistentialBoxInst: {
    auto DBI = cast<DeallocExistentialBoxInst>(&SI);
    writeOneTypeOneOperandLayout(DBI->getKind(), 0,
                                 DBI->getConcreteType(),
                                 DBI->getOperand());
    break;
  }
  case SILInstructionKind::ValueMetatypeInst: {
    auto VMI = cast<ValueMetatypeInst>(&SI);
    writeOneTypeOneOperandLayout(VMI->getKind(), 0,
                                 VMI->getType(),
                                 VMI->getOperand());
    break;
  }
  case SILInstructionKind::ExistentialMetatypeInst: {
    auto EMI = cast<ExistentialMetatypeInst>(&SI);
    writeOneTypeOneOperandLayout(EMI->getKind(), 0,
                                 EMI->getType(),
                                 EMI->getOperand());
    break;
  }
  case SILInstructionKind::AllocValueBufferInst: {
    auto AVBI = cast<AllocValueBufferInst>(&SI);
    writeOneTypeOneOperandLayout(AVBI->getKind(), 0,
                                 AVBI->getValueType(),
                                 AVBI->getOperand());
    break;
  }
  case SILInstructionKind::AllocBoxInst: {
    const AllocBoxInst *ABI = cast<AllocBoxInst>(&SI);
    writeOneTypeLayout(ABI->getKind(), ABI->getType());
    break;
  }
  case SILInstructionKind::AllocRefInst:
  case SILInstructionKind::AllocRefDynamicInst: {
    const AllocRefInstBase *ARI = cast<AllocRefInstBase>(&SI);
    unsigned abbrCode = SILAbbrCodes[SILOneTypeValuesLayout::Code];
    SmallVector<ValueID, 4> Args;
    Args.push_back((unsigned)ARI->isObjC() |
                   ((unsigned)ARI->canAllocOnStack() << 1));
    ArrayRef<SILType> TailTypes = ARI->getTailAllocatedTypes();
    ArrayRef<Operand> AllOps = ARI->getAllOperands();
    unsigned NumTailAllocs = TailTypes.size();
    unsigned NumOpsToWrite = NumTailAllocs;
    if (SI.getKind() == SILInstructionKind::AllocRefDynamicInst)
      ++NumOpsToWrite;
    for (unsigned Idx = 0; Idx < NumOpsToWrite; ++Idx) {
      if (Idx < NumTailAllocs) {
        assert(TailTypes[Idx].isObject());
        Args.push_back(S.addTypeRef(TailTypes[Idx].getSwiftRValueType()));
      }
      SILValue OpVal = AllOps[Idx].get();
      Args.push_back(addValueRef(OpVal));
      SILType OpType = OpVal->getType();
      assert(OpType.isObject());
      Args.push_back(S.addTypeRef(OpType.getSwiftRValueType()));
    }
    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord, abbrCode,
                                       (unsigned)SI.getKind(),
                                       S.addTypeRef(
                                         ARI->getType().getSwiftRValueType()),
                                       (unsigned)ARI->getType().getCategory(),
                                       Args);
    break;
  }
  case SILInstructionKind::AllocStackInst: {
    const AllocStackInst *ASI = cast<AllocStackInst>(&SI);
    writeOneTypeLayout(ASI->getKind(), ASI->getElementType());
    break;
  }
  case SILInstructionKind::ProjectValueBufferInst: {
    auto PVBI = cast<ProjectValueBufferInst>(&SI);
    writeOneTypeOneOperandLayout(PVBI->getKind(), 0,
                                 PVBI->getType(),
                                 PVBI->getOperand());
    break;
  }
  case SILInstructionKind::ProjectBoxInst: {
    auto PBI = cast<ProjectBoxInst>(&SI);
    
    // Use SILOneTypeOneOperandLayout with the field index crammed in the TypeID
    auto boxOperand = PBI->getOperand();
    auto boxRef = addValueRef(boxOperand);
    auto boxType = boxOperand->getType();
    auto boxTypeRef = S.addTypeRef(boxType.getSwiftRValueType());
    
    SILOneTypeOneOperandLayout::emitRecord(Out, ScratchRecord,
          SILAbbrCodes[SILOneTypeOneOperandLayout::Code],
          unsigned(PBI->getKind()), 0,
          PBI->getFieldIndex(), 0,
          boxTypeRef, unsigned(boxType.getCategory()),
          boxRef);
    break;
  }
  case SILInstructionKind::ProjectExistentialBoxInst: {
    auto PEBI = cast<ProjectExistentialBoxInst>(&SI);
    writeOneTypeOneOperandLayout(PEBI->getKind(), 0,
                                 PEBI->getType(),
                                 PEBI->getOperand());
    break;
  }
  case SILInstructionKind::BuiltinInst: {
    // Format: number of substitutions, the builtin name, result type, and
    // a list of values for the arguments. Each value in the list
    // is represented with 4 IDs:
    //   ValueID, ValueResultNumber, TypeID, TypeCategory.
    // The record is followed by the substitution list.
    const BuiltinInst *BI = cast<BuiltinInst>(&SI);
    SmallVector<ValueID, 4> Args;
    for (auto Arg : BI->getArguments()) {
      Args.push_back(addValueRef(Arg));
      Args.push_back(S.addTypeRef(Arg->getType().getSwiftRValueType()));
      Args.push_back((unsigned)Arg->getType().getCategory());
    }
    SILInstApplyLayout::emitRecord(Out, ScratchRecord,
                             SILAbbrCodes[SILInstApplyLayout::Code],
                             SIL_BUILTIN,
                             BI->getSubstitutions().size(),
                             S.addTypeRef(BI->getType().getSwiftRValueType()),
                             (unsigned)BI->getType().getCategory(),
                             S.addDeclBaseNameRef(BI->getName()),
                             Args);
    S.writeSubstitutions(BI->getSubstitutions(), SILAbbrCodes);
    break;
  }
  case SILInstructionKind::ApplyInst: {
    // Format: attributes such as transparent and number of substitutions,
    // the callee's substituted and unsubstituted types, a value for
    // the callee and a list of values for the arguments. Each value in the list
    // is represented with 2 IDs: ValueID and ValueResultNumber. The record
    // is followed by the substitution list.
    const ApplyInst *AI = cast<ApplyInst>(&SI);
    SmallVector<ValueID, 4> Args;
    for (auto Arg: AI->getArguments()) {
      Args.push_back(addValueRef(Arg));
    }
    SILInstApplyLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILInstApplyLayout::Code],
        AI->isNonThrowing() ? SIL_NON_THROWING_APPLY : SIL_APPLY,
        AI->getSubstitutions().size(),
        S.addTypeRef(AI->getCallee()->getType().getSwiftRValueType()),
        S.addTypeRef(AI->getSubstCalleeType()),
        addValueRef(AI->getCallee()),
        Args);
    S.writeSubstitutions(AI->getSubstitutions(), SILAbbrCodes);
    break;
  }
  case SILInstructionKind::BeginApplyInst: {
    // Format: attributes such as transparent and number of substitutions,
    // the callee's substituted and unsubstituted types, a value for
    // the callee and a list of values for the arguments. Each value in the list
    // is represented with 2 IDs: ValueID and ValueResultNumber. The record
    // is followed by the substitution list.
    const BeginApplyInst *AI = cast<BeginApplyInst>(&SI);
    SmallVector<ValueID, 4> Args;
    for (auto Arg: AI->getArguments()) {
      Args.push_back(addValueRef(Arg));
    }
    SILInstApplyLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILInstApplyLayout::Code],
        AI->isNonThrowing() ? SIL_NON_THROWING_BEGIN_APPLY : SIL_BEGIN_APPLY,
        AI->getSubstitutions().size(),
        S.addTypeRef(AI->getCallee()->getType().getSwiftRValueType()),
        S.addTypeRef(AI->getSubstCalleeType()),
        addValueRef(AI->getCallee()),
        Args);
    S.writeSubstitutions(AI->getSubstitutions(), SILAbbrCodes);
    break;
  }
  case SILInstructionKind::TryApplyInst: {
    // Format: attributes such as transparent and number of substitutions,
    // the callee's substituted and unsubstituted types, a value for
    // the callee and a list of values for the arguments. Each value in the list
    // is represented with 2 IDs: ValueID and ValueResultNumber. The final two
    // entries in the list are the basic block destinations. The record
    // is followed by the substitution list.
    const TryApplyInst *AI = cast<TryApplyInst>(&SI);
    SmallVector<ValueID, 4> Args;
    for (auto Arg: AI->getArguments()) {
      Args.push_back(addValueRef(Arg));
    }
    Args.push_back(BasicBlockMap[AI->getNormalBB()]);
    Args.push_back(BasicBlockMap[AI->getErrorBB()]);
    SILInstApplyLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILInstApplyLayout::Code], SIL_TRY_APPLY,
        AI->getSubstitutions().size(),
        S.addTypeRef(AI->getCallee()->getType().getSwiftRValueType()),
        S.addTypeRef(AI->getSubstCalleeType()),
        addValueRef(AI->getCallee()),
        Args);
    S.writeSubstitutions(AI->getSubstitutions(), SILAbbrCodes);
    break;
  }
  case SILInstructionKind::PartialApplyInst: {
    const PartialApplyInst *PAI = cast<PartialApplyInst>(&SI);
        SmallVector<ValueID, 4> Args;
    for (auto Arg: PAI->getArguments()) {
      Args.push_back(addValueRef(Arg));
    }
    SILInstApplyLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILInstApplyLayout::Code], SIL_PARTIAL_APPLY,
        PAI->getSubstitutions().size(),
        S.addTypeRef(PAI->getCallee()->getType().getSwiftRValueType()),
        S.addTypeRef(PAI->getType().getSwiftRValueType()),
        addValueRef(PAI->getCallee()),
        Args);
    S.writeSubstitutions(PAI->getSubstitutions(), SILAbbrCodes);
    break;
  }
  case SILInstructionKind::AllocGlobalInst: {
    // Format: Name and type. Use SILOneOperandLayout.
    const AllocGlobalInst *AGI = cast<AllocGlobalInst>(&SI);
    SILOneOperandLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneOperandLayout::Code],
        (unsigned)SI.getKind(), 0, 0, 0,
        S.addDeclBaseNameRef(
            Ctx.getIdentifier(AGI->getReferencedGlobal()->getName())));
    break;
  }
  case SILInstructionKind::GlobalAddrInst:
  case SILInstructionKind::GlobalValueInst: {
    // Format: Name and type. Use SILOneOperandLayout.
    const GlobalAccessInst *GI = cast<GlobalAccessInst>(&SI);
    SILOneOperandLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneOperandLayout::Code],
        (unsigned)SI.getKind(), 0,
        S.addTypeRef(GI->getType().getSwiftRValueType()),
        (unsigned)GI->getType().getCategory(),
        S.addDeclBaseNameRef(
            Ctx.getIdentifier(GI->getReferencedGlobal()->getName())));
    break;
  }
  case SILInstructionKind::BranchInst: {
    // Format: destination basic block ID, a list of arguments. Use
    // SILOneTypeValuesLayout.
    const BranchInst *BrI = cast<BranchInst>(&SI);
    SmallVector<ValueID, 4> ListOfValues;
    for (auto Elt : BrI->getArgs()) {
      ListOfValues.push_back(S.addTypeRef(Elt->getType().getSwiftRValueType()));
      ListOfValues.push_back((unsigned)Elt->getType().getCategory());
      ListOfValues.push_back(addValueRef(Elt));
    }

    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code],
        (unsigned)SI.getKind(),
        BasicBlockMap[BrI->getDestBB()], 0, ListOfValues);
    break;
  }
  case SILInstructionKind::CondBranchInst: {
    // Format: condition, true basic block ID, a list of arguments, false basic
    // block ID, a list of arguments. Use SILOneTypeValuesLayout: the type is
    // for condition, the list has value for condition, true basic block ID,
    // false basic block ID, number of true arguments, and a list of true|false
    // arguments.
    const CondBranchInst *CBI = cast<CondBranchInst>(&SI);
    SmallVector<ValueID, 4> ListOfValues;
    ListOfValues.push_back(addValueRef(CBI->getCondition()));
    ListOfValues.push_back(BasicBlockMap[CBI->getTrueBB()]);
    ListOfValues.push_back(BasicBlockMap[CBI->getFalseBB()]);
    ListOfValues.push_back(CBI->getTrueArgs().size());
    for (auto Elt : CBI->getTrueArgs()) {
      ListOfValues.push_back(S.addTypeRef(Elt->getType().getSwiftRValueType()));
      ListOfValues.push_back((unsigned)Elt->getType().getCategory());
      ListOfValues.push_back(addValueRef(Elt));
    }
    for (auto Elt : CBI->getFalseArgs()) {
      ListOfValues.push_back(S.addTypeRef(Elt->getType().getSwiftRValueType()));
      ListOfValues.push_back((unsigned)Elt->getType().getCategory());
      ListOfValues.push_back(addValueRef(Elt));
    }

    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code],
        (unsigned)SI.getKind(),
        S.addTypeRef(CBI->getCondition()->getType().getSwiftRValueType()),
        (unsigned)CBI->getCondition()->getType().getCategory(),
        ListOfValues);
    break;
  }
  case SILInstructionKind::SwitchEnumInst:
  case SILInstructionKind::SwitchEnumAddrInst: {
    // Format: condition, a list of cases (EnumElementDecl + Basic Block ID),
    // default basic block ID. Use SILOneTypeValuesLayout: the type is
    // for condition, the list has value for condition, hasDefault, default
    // basic block ID, a list of (DeclID, BasicBlock ID).
    const SwitchEnumInstBase *SOI = cast<SwitchEnumInstBase>(&SI);
    SmallVector<ValueID, 4> ListOfValues;
    ListOfValues.push_back(addValueRef(SOI->getOperand()));
    ListOfValues.push_back((unsigned)SOI->hasDefault());
    if (SOI->hasDefault())
      ListOfValues.push_back(BasicBlockMap[SOI->getDefaultBB()]);
    else
      ListOfValues.push_back(0);

    for (unsigned i = 0, e = SOI->getNumCases(); i < e; ++i) {
      EnumElementDecl *elt;
      SILBasicBlock *dest;
      std::tie(elt, dest) = SOI->getCase(i);
      ListOfValues.push_back(S.addDeclRef(elt));
      ListOfValues.push_back(BasicBlockMap[dest]);
    }
    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code],
        (unsigned)SI.getKind(),
        S.addTypeRef(SOI->getOperand()->getType().getSwiftRValueType()),
        (unsigned)SOI->getOperand()->getType().getCategory(),
        ListOfValues);
    break;
  }
  case SILInstructionKind::SelectEnumInst:
  case SILInstructionKind::SelectEnumAddrInst: {
    // Format: condition, a list of cases (EnumElementDecl + Value ID),
    // default value ID. Use SILOneTypeValuesLayout: the type is
    // for condition, the list has value for condition, result type,
    //   hasDefault, default
    // basic block ID, a list of (DeclID, BasicBlock ID).
    const SelectEnumInstBase *SOI = cast<SelectEnumInstBase>(&SI);
    SmallVector<ValueID, 4> ListOfValues;
    ListOfValues.push_back(addValueRef(SOI->getEnumOperand()));
    ListOfValues.push_back(S.addTypeRef(SOI->getType().getSwiftRValueType()));
    ListOfValues.push_back((unsigned)SOI->getType().getCategory());
    ListOfValues.push_back((unsigned)SOI->hasDefault());
    if (SOI->hasDefault()) {
      ListOfValues.push_back(addValueRef(SOI->getDefaultResult()));
    } else {
      ListOfValues.push_back(0);
    }
    for (unsigned i = 0, e = SOI->getNumCases(); i < e; ++i) {
      EnumElementDecl *elt;
      SILValue result;
      std::tie(elt, result) = SOI->getCase(i);
      ListOfValues.push_back(S.addDeclRef(elt));
      ListOfValues.push_back(addValueRef(result));
    }
    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code],
        (unsigned)SI.getKind(),
        S.addTypeRef(SOI->getEnumOperand()->getType().getSwiftRValueType()),
        (unsigned)SOI->getEnumOperand()->getType().getCategory(),
        ListOfValues);
    break;
  }
  case SILInstructionKind::SwitchValueInst: {
    // Format: condition, a list of cases (Value ID + Basic Block ID),
    // default basic block ID. Use SILOneTypeValuesLayout: the type is
    // for condition, the list contains value for condition, hasDefault, default
    // basic block ID, a list of (Value ID, BasicBlock ID).
    const SwitchValueInst *SII = cast<SwitchValueInst>(&SI);
    SmallVector<ValueID, 4> ListOfValues;
    ListOfValues.push_back(addValueRef(SII->getOperand()));
    ListOfValues.push_back((unsigned)SII->hasDefault());
    if (SII->hasDefault())
      ListOfValues.push_back(BasicBlockMap[SII->getDefaultBB()]);
    else
      ListOfValues.push_back(0);

    for (unsigned i = 0, e = SII->getNumCases(); i < e; ++i) {
      SILValue value;
      SILBasicBlock *dest;
      std::tie(value, dest) = SII->getCase(i);
      ListOfValues.push_back(addValueRef(value));
      ListOfValues.push_back(BasicBlockMap[dest]);
    }
    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code],
        (unsigned)SI.getKind(),
        S.addTypeRef(SII->getOperand()->getType().getSwiftRValueType()),
        (unsigned)SII->getOperand()->getType().getCategory(),
        ListOfValues);
    break;
  }
  case SILInstructionKind::SelectValueInst: {
    // Format: condition, a list of cases (Value ID + Value ID),
    // default value ID. Use SILOneTypeValuesLayout: the type is
    // for condition, the list has value for condition, result type,
    // hasDefault, default
    // basic block ID, a list of (Value ID, Value ID).
    const SelectValueInst *SVI = cast<SelectValueInst>(&SI);
    SmallVector<ValueID, 4> ListOfValues;
    ListOfValues.push_back(addValueRef(SVI->getOperand()));
    ListOfValues.push_back(S.addTypeRef(SVI->getType().getSwiftRValueType()));
    ListOfValues.push_back((unsigned)SVI->getType().getCategory());
    ListOfValues.push_back((unsigned)SVI->hasDefault());
    if (SVI->hasDefault()) {
      ListOfValues.push_back(addValueRef(SVI->getDefaultResult()));
    } else {
      ListOfValues.push_back(0);
    }
    for (unsigned i = 0, e = SVI->getNumCases(); i < e; ++i) {
      SILValue casevalue;
      SILValue result;
      std::tie(casevalue, result) = SVI->getCase(i);
      ListOfValues.push_back(addValueRef(casevalue));
      ListOfValues.push_back(addValueRef(result));
    }
    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code],
        (unsigned)SI.getKind(),
        S.addTypeRef(SVI->getOperand()->getType().getSwiftRValueType()),
        (unsigned)SVI->getOperand()->getType().getCategory(),
        ListOfValues);
    break;
  }
  case SILInstructionKind::CondFailInst:
  case SILInstructionKind::RetainValueInst:
  case SILInstructionKind::DestructureStructInst:
  case SILInstructionKind::DestructureTupleInst:
  case SILInstructionKind::RetainValueAddrInst:
  case SILInstructionKind::UnmanagedRetainValueInst:
  case SILInstructionKind::EndBorrowArgumentInst:
  case SILInstructionKind::CopyValueInst:
  case SILInstructionKind::CopyUnownedValueInst:
  case SILInstructionKind::DestroyValueInst:
  case SILInstructionKind::ReleaseValueInst:
  case SILInstructionKind::ReleaseValueAddrInst:
  case SILInstructionKind::UnmanagedReleaseValueInst:
  case SILInstructionKind::AutoreleaseValueInst:
  case SILInstructionKind::UnmanagedAutoreleaseValueInst:
  case SILInstructionKind::SetDeallocatingInst:
  case SILInstructionKind::DeallocStackInst:
  case SILInstructionKind::DeallocRefInst:
  case SILInstructionKind::DeinitExistentialAddrInst:
  case SILInstructionKind::DeinitExistentialValueInst:
  case SILInstructionKind::DestroyAddrInst:
  case SILInstructionKind::LoadInst:
  case SILInstructionKind::LoadBorrowInst:
  case SILInstructionKind::BeginBorrowInst:
  case SILInstructionKind::LoadUnownedInst:
  case SILInstructionKind::LoadWeakInst:
  case SILInstructionKind::MarkUninitializedInst:
  case SILInstructionKind::FixLifetimeInst:
  case SILInstructionKind::EndLifetimeInst:
  case SILInstructionKind::CopyBlockInst:
  case SILInstructionKind::StrongPinInst:
  case SILInstructionKind::StrongReleaseInst:
  case SILInstructionKind::StrongRetainInst:
  case SILInstructionKind::StrongUnpinInst:
  case SILInstructionKind::StrongRetainUnownedInst:
  case SILInstructionKind::UnownedRetainInst:
  case SILInstructionKind::UnownedReleaseInst:
  case SILInstructionKind::IsUniqueInst:
  case SILInstructionKind::IsUniqueOrPinnedInst:
  case SILInstructionKind::AbortApplyInst:
  case SILInstructionKind::EndApplyInst:
  case SILInstructionKind::ReturnInst:
  case SILInstructionKind::UncheckedOwnershipConversionInst:
  case SILInstructionKind::ThrowInst: {
    unsigned Attr = 0;
    if (auto *LI = dyn_cast<LoadInst>(&SI))
      Attr = unsigned(LI->getOwnershipQualifier());
    else if (auto *LWI = dyn_cast<LoadWeakInst>(&SI))
      Attr = LWI->isTake();
    else if (auto *LUI = dyn_cast<LoadUnownedInst>(&SI))
      Attr = LUI->isTake();
    else if (auto *MUI = dyn_cast<MarkUninitializedInst>(&SI))
      Attr = (unsigned)MUI->getKind();
    else if (auto *DRI = dyn_cast<DeallocRefInst>(&SI))
      Attr = (unsigned)DRI->canAllocOnStack();
    else if (auto *RCI = dyn_cast<RefCountingInst>(&SI))
      Attr = RCI->isNonAtomic();
    else if (auto *RCI = dyn_cast<StrongPinInst>(&SI))
      Attr = RCI->isNonAtomic();
    else if (auto *UOCI = dyn_cast<UncheckedOwnershipConversionInst>(&SI)) {
      Attr = unsigned(SILValue(UOCI).getOwnershipKind());
    }
    writeOneOperandLayout(SI.getKind(), Attr, SI.getOperand(0));
    break;
  }
  case SILInstructionKind::YieldInst: {
    auto YI = cast<YieldInst>(&SI);
    SmallVector<ValueID, 4> args;
    for (auto arg: YI->getYieldedValues()) {
      args.push_back(S.addTypeRef(arg->getType().getSwiftRValueType()));
      args.push_back((unsigned)arg->getType().getCategory());
      args.push_back(addValueRef(arg));
    }
    args.push_back(BasicBlockMap[YI->getResumeBB()]);
    args.push_back(BasicBlockMap[YI->getUnwindBB()]);
    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code],
        (unsigned)YI->getKind(), 0, 0, args);
    break;
  }
  case SILInstructionKind::FunctionRefInst: {
    // Use SILOneOperandLayout to specify the function type and the function
    // name (IdentifierID).
    const FunctionRefInst *FRI = cast<FunctionRefInst>(&SI);
    SILFunction *ReferencedFunction = FRI->getReferencedFunction();
    unsigned abbrCode = SILAbbrCodes[SILOneOperandLayout::Code];
    SILOneOperandLayout::emitRecord(Out, ScratchRecord, abbrCode,
        (unsigned)SI.getKind(), 0,
        S.addTypeRef(FRI->getType().getSwiftRValueType()),
        (unsigned)FRI->getType().getCategory(),
        addSILFunctionRef(ReferencedFunction));

    break;
  }
  case SILInstructionKind::DeallocPartialRefInst:
  case SILInstructionKind::MarkDependenceInst:
  case SILInstructionKind::IndexAddrInst:
  case SILInstructionKind::IndexRawPointerInst: {
    SILValue operand, operand2;
    unsigned Attr = 0;
    if (SI.getKind() == SILInstructionKind::DeallocPartialRefInst) {
      const DeallocPartialRefInst *DPRI = cast<DeallocPartialRefInst>(&SI);
      operand = DPRI->getInstance();
      operand2 = DPRI->getMetatype();
    } else if (SI.getKind() == SILInstructionKind::IndexRawPointerInst) {
      const IndexRawPointerInst *IRP = cast<IndexRawPointerInst>(&SI);
      operand = IRP->getBase();
      operand2 = IRP->getIndex();
    } else if (SI.getKind() == SILInstructionKind::MarkDependenceInst) {
      const MarkDependenceInst *MDI = cast<MarkDependenceInst>(&SI);
      operand = MDI->getValue();
      operand2 = MDI->getBase();
    } else {
      const IndexAddrInst *IAI = cast<IndexAddrInst>(&SI);
      operand = IAI->getBase();
      operand2 = IAI->getIndex();
    }
    SILTwoOperandsLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILTwoOperandsLayout::Code],
        (unsigned)SI.getKind(), Attr,
        S.addTypeRef(operand->getType().getSwiftRValueType()),
        (unsigned)operand->getType().getCategory(),
        addValueRef(operand),
        S.addTypeRef(operand2->getType().getSwiftRValueType()),
        (unsigned)operand2->getType().getCategory(),
        addValueRef(operand2));
    break;
  }
  case SILInstructionKind::TailAddrInst: {
    const TailAddrInst *TAI = cast<TailAddrInst>(&SI);
    SILTailAddrLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILTailAddrLayout::Code],
        (unsigned)SI.getKind(),
        S.addTypeRef(TAI->getBase()->getType().getSwiftRValueType()),
        addValueRef(TAI->getBase()),
        S.addTypeRef(TAI->getIndex()->getType().getSwiftRValueType()),
        addValueRef(TAI->getIndex()),
        S.addTypeRef(TAI->getTailType().getSwiftRValueType()));
    break;
  }
  case SILInstructionKind::StringLiteralInst: {
    auto SLI = cast<StringLiteralInst>(&SI);
    StringRef Str = SLI->getValue();
    unsigned abbrCode = SILAbbrCodes[SILOneOperandLayout::Code];
    unsigned encoding = toStableStringEncoding(SLI->getEncoding());
    SILOneOperandLayout::emitRecord(Out, ScratchRecord, abbrCode,
                                    (unsigned)SI.getKind(), encoding, 0, 0,
                                    S.addDeclBaseNameRef(Ctx.getIdentifier(Str)));
    break;
  }
  case SILInstructionKind::ConstStringLiteralInst: {
    auto SLI = cast<ConstStringLiteralInst>(&SI);
    StringRef Str = SLI->getValue();
    unsigned abbrCode = SILAbbrCodes[SILOneOperandLayout::Code];
    unsigned encoding = toStableConstStringEncoding(SLI->getEncoding());
    SILOneOperandLayout::emitRecord(Out, ScratchRecord, abbrCode,
                                    (unsigned)SI.getKind(), encoding, 0, 0,
                                    S.addDeclBaseNameRef(Ctx.getIdentifier(Str)));
    break;
  }
  case SILInstructionKind::FloatLiteralInst:
  case SILInstructionKind::IntegerLiteralInst: {
    // Use SILOneOperandLayout to specify the type and the literal.
    std::string Str;
    SILType Ty;
    switch (SI.getKind()) {
    default: llvm_unreachable("Out of sync with parent switch");
    case SILInstructionKind::IntegerLiteralInst:
      Str = cast<IntegerLiteralInst>(&SI)->getValue().toString(10, true);
      Ty = cast<IntegerLiteralInst>(&SI)->getType();
      break;
    case SILInstructionKind::FloatLiteralInst:
      Str = cast<FloatLiteralInst>(&SI)->getBits().toString(16,
                                                            /*Signed*/false);
      Ty = cast<FloatLiteralInst>(&SI)->getType();
      break;
    }
    unsigned abbrCode = SILAbbrCodes[SILOneOperandLayout::Code];
    SILOneOperandLayout::emitRecord(Out, ScratchRecord, abbrCode,
        (unsigned)SI.getKind(), 0,
        S.addTypeRef(Ty.getSwiftRValueType()),
        (unsigned)Ty.getCategory(),
        S.addDeclBaseNameRef(Ctx.getIdentifier(Str)));
    break;
  }
  case SILInstructionKind::MarkFunctionEscapeInst: {
    // Format: a list of typed values. A typed value is expressed by 4 IDs:
    // TypeID, TypeCategory, ValueID, ValueResultNumber.
    const MarkFunctionEscapeInst *MFE = cast<MarkFunctionEscapeInst>(&SI);
    SmallVector<ValueID, 4> ListOfValues;
    for (auto Elt : MFE->getElements()) {
      ListOfValues.push_back(S.addTypeRef(Elt->getType().getSwiftRValueType()));
      ListOfValues.push_back((unsigned)Elt->getType().getCategory());
      ListOfValues.push_back(addValueRef(Elt));
    }

    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code],
        (unsigned)SI.getKind(), 0, 0, ListOfValues);
    break;
  }
  case SILInstructionKind::MetatypeInst: {
    auto &MI = cast<MetatypeInst>(SI);
    writeOneTypeLayout(MI.getKind(), MI.getType());
    break;
  }
  case SILInstructionKind::ObjCProtocolInst: {
    const ObjCProtocolInst *PI = cast<ObjCProtocolInst>(&SI);
    unsigned abbrCode = SILAbbrCodes[SILOneOperandLayout::Code];
    SILOneOperandLayout::emitRecord(Out, ScratchRecord, abbrCode,
                              (unsigned)SI.getKind(), 0,
                              S.addTypeRef(PI->getType().getSwiftRValueType()),
                              (unsigned)PI->getType().getCategory(),
                              S.addDeclRef(PI->getProtocol()));
    break;
  }
  case SILInstructionKind::OpenExistentialAddrInst: {
    auto &open = cast<OpenExistentialAddrInst>(SI);
    assert(open.getNumOperands() - open.getTypeDependentOperands().size() == 1);
    unsigned attrs = open.getAccessKind() == OpenedExistentialAccess::Immutable
                         ? 0 : 1;
    writeOneTypeOneOperandLayout(open.getKind(), attrs, open.getType(),
                                 open.getOperand());
    break;
  }
  // Conversion instructions (and others of similar form).
  case SILInstructionKind::OpenExistentialRefInst:
  case SILInstructionKind::OpenExistentialMetatypeInst:
  case SILInstructionKind::OpenExistentialBoxInst:
  case SILInstructionKind::OpenExistentialValueInst:
  case SILInstructionKind::OpenExistentialBoxValueInst:
  case SILInstructionKind::UncheckedRefCastInst:
  case SILInstructionKind::UncheckedAddrCastInst:
  case SILInstructionKind::UncheckedTrivialBitCastInst:
  case SILInstructionKind::UncheckedBitwiseCastInst:
  case SILInstructionKind::BridgeObjectToRefInst:
  case SILInstructionKind::BridgeObjectToWordInst:
  case SILInstructionKind::UpcastInst:
  case SILInstructionKind::AddressToPointerInst:
  case SILInstructionKind::RefToRawPointerInst:
  case SILInstructionKind::RawPointerToRefInst:
  case SILInstructionKind::RefToUnownedInst:
  case SILInstructionKind::UnownedToRefInst:
  case SILInstructionKind::RefToUnmanagedInst:
  case SILInstructionKind::UnmanagedToRefInst:
  case SILInstructionKind::ThinToThickFunctionInst:
  case SILInstructionKind::ThickToObjCMetatypeInst:
  case SILInstructionKind::ObjCToThickMetatypeInst:
  case SILInstructionKind::ConvertFunctionInst:
  case SILInstructionKind::ThinFunctionToPointerInst:
  case SILInstructionKind::PointerToThinFunctionInst:
  case SILInstructionKind::ObjCMetatypeToObjectInst:
  case SILInstructionKind::ObjCExistentialMetatypeToObjectInst:
  case SILInstructionKind::ProjectBlockStorageInst: {
    writeConversionLikeInstruction(cast<SingleValueInstruction>(&SI));
    break;
  }
  case SILInstructionKind::PointerToAddressInst: {
    auto &PAI = cast<PointerToAddressInst>(SI);
    assert(PAI.getNumOperands() - PAI.getTypeDependentOperands().size() == 1);
    unsigned attrs = (PAI.isStrict() ? 1 : 0)
                   | (PAI.isInvariant() ? 2 : 0);
    writeOneTypeOneOperandLayout(PAI.getKind(), attrs, PAI.getType(),
                                 PAI.getOperand());
    break;
  }
  case SILInstructionKind::RefToBridgeObjectInst: {
    auto RI = cast<RefToBridgeObjectInst>(&SI);
    SILTwoOperandsLayout::emitRecord(Out, ScratchRecord,
           SILAbbrCodes[SILTwoOperandsLayout::Code], (unsigned)SI.getKind(),
           /*attr*/ 0,
           S.addTypeRef(RI->getConverted()->getType().getSwiftRValueType()),
           (unsigned)RI->getConverted()->getType().getCategory(),
           addValueRef(RI->getConverted()),
           S.addTypeRef(RI->getBitsOperand()->getType().getSwiftRValueType()),
           (unsigned)RI->getBitsOperand()->getType().getCategory(),
           addValueRef(RI->getBitsOperand()));
    break;
  }
  // Checked Conversion instructions.
  case SILInstructionKind::UnconditionalCheckedCastInst: {
    auto CI = cast<UnconditionalCheckedCastInst>(&SI);
    SILInstCastLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILInstCastLayout::Code],
        (unsigned)SI.getKind(), /*attr*/ 0,
        S.addTypeRef(CI->getType().getSwiftRValueType()),
        (unsigned)CI->getType().getCategory(),
        S.addTypeRef(CI->getOperand()->getType().getSwiftRValueType()),
        (unsigned)CI->getOperand()->getType().getCategory(),
        addValueRef(CI->getOperand()));
    break;
  }
  case SILInstructionKind::UnconditionalCheckedCastAddrInst: {
    auto CI = cast<UnconditionalCheckedCastAddrInst>(&SI);
    ValueID listOfValues[] = {
      S.addTypeRef(CI->getSourceType()),
      addValueRef(CI->getSrc()),
      S.addTypeRef(CI->getSrc()->getType().getSwiftRValueType()),
      (unsigned)CI->getSrc()->getType().getCategory(),
      S.addTypeRef(CI->getTargetType()),
      addValueRef(CI->getDest())
    };
    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
             SILAbbrCodes[SILOneTypeValuesLayout::Code], (unsigned)SI.getKind(),
             S.addTypeRef(CI->getDest()->getType().getSwiftRValueType()),
             (unsigned)CI->getDest()->getType().getCategory(),
             llvm::makeArrayRef(listOfValues));
    break;
  }
  case SILInstructionKind::UnconditionalCheckedCastValueInst: {
    auto CI = cast<UnconditionalCheckedCastValueInst>(&SI);
    SILInstCastLayout::emitRecord(
        Out, ScratchRecord, SILAbbrCodes[SILInstCastLayout::Code],
        (unsigned)SI.getKind(),
        /*attr*/ 0,
        S.addTypeRef(CI->getType().getSwiftRValueType()),
        (unsigned)CI->getType().getCategory(),
        S.addTypeRef(CI->getOperand()->getType().getSwiftRValueType()),
        (unsigned)CI->getOperand()->getType().getCategory(),
        addValueRef(CI->getOperand()));
    break;
  }
  case SILInstructionKind::UncheckedRefCastAddrInst: {
    auto CI = cast<UncheckedRefCastAddrInst>(&SI);
    ValueID listOfValues[] = {
      S.addTypeRef(CI->getSourceType()),
      addValueRef(CI->getSrc()),
      S.addTypeRef(CI->getSrc()->getType().getSwiftRValueType()),
      (unsigned)CI->getSrc()->getType().getCategory(),
      S.addTypeRef(CI->getTargetType()),
      addValueRef(CI->getDest())
    };
    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
             SILAbbrCodes[SILOneTypeValuesLayout::Code], (unsigned)SI.getKind(),
             S.addTypeRef(CI->getDest()->getType().getSwiftRValueType()),
             (unsigned)CI->getDest()->getType().getCategory(),
             llvm::makeArrayRef(listOfValues));
    break;
  }

  case SILInstructionKind::EndBorrowInst: {
    unsigned abbrCode = SILAbbrCodes[SILTwoOperandsLayout::Code];
    unsigned Attr = 0;
    auto *EBI = cast<EndBorrowInst>(&SI);
    SILValue BorrowedValue = EBI->getBorrowedValue();
    SILValue OriginalValue = EBI->getOriginalValue();

    SILTwoOperandsLayout::emitRecord(
        Out, ScratchRecord, abbrCode, (unsigned)SI.getKind(), Attr,
        S.addTypeRef(BorrowedValue->getType().getSwiftRValueType()),
        (unsigned)BorrowedValue->getType().getCategory(),
        addValueRef(BorrowedValue),
        S.addTypeRef(OriginalValue->getType().getSwiftRValueType()),
        (unsigned)OriginalValue->getType().getCategory(),
        addValueRef(OriginalValue));
    break;
  }

  case SILInstructionKind::BeginAccessInst: {
    unsigned abbrCode = SILAbbrCodes[SILOneOperandLayout::Code];
    auto *BAI = cast<BeginAccessInst>(&SI);
    unsigned attr =
         unsigned(BAI->getAccessKind())
       + (unsigned(BAI->getEnforcement()) << 2);
    SILValue operand = BAI->getOperand();

    SILOneOperandLayout::emitRecord(
        Out, ScratchRecord, abbrCode, (unsigned)SI.getKind(), attr,
        S.addTypeRef(operand->getType().getSwiftRValueType()),
        (unsigned)operand->getType().getCategory(),
        addValueRef(operand));
    break;
  }

  case SILInstructionKind::EndAccessInst: {
    unsigned abbrCode = SILAbbrCodes[SILOneOperandLayout::Code];
    auto *EAI = cast<EndAccessInst>(&SI);
    unsigned attr = unsigned(EAI->isAborting());
    SILValue operand = EAI->getOperand();

    SILOneOperandLayout::emitRecord(
        Out, ScratchRecord, abbrCode, (unsigned)SI.getKind(), attr,
        S.addTypeRef(operand->getType().getSwiftRValueType()),
        (unsigned)operand->getType().getCategory(),
        addValueRef(operand));
    break;
  }

  case SILInstructionKind::BeginUnpairedAccessInst: {
    unsigned abbrCode = SILAbbrCodes[SILTwoOperandsLayout::Code];
    auto *BAI = cast<BeginUnpairedAccessInst>(&SI);
    unsigned attr =
         unsigned(BAI->getAccessKind())
       + (unsigned(BAI->getEnforcement()) << 2);
    SILValue source = BAI->getSource();
    SILValue buffer = BAI->getBuffer();

    SILTwoOperandsLayout::emitRecord(
        Out, ScratchRecord, abbrCode, (unsigned)SI.getKind(), attr,
        S.addTypeRef(source->getType().getSwiftRValueType()),
        (unsigned)source->getType().getCategory(),
        addValueRef(source),
        S.addTypeRef(buffer->getType().getSwiftRValueType()),
        (unsigned)buffer->getType().getCategory(),
        addValueRef(buffer));
    break;
  }

  case SILInstructionKind::EndUnpairedAccessInst: {
    unsigned abbrCode = SILAbbrCodes[SILOneOperandLayout::Code];
    auto *EAI = cast<EndUnpairedAccessInst>(&SI);
    unsigned attr = unsigned(EAI->isAborting())
                  + (unsigned(EAI->getEnforcement()) << 1);
    SILValue operand = EAI->getOperand();

    SILOneOperandLayout::emitRecord(
        Out, ScratchRecord, abbrCode, (unsigned)SI.getKind(), attr,
        S.addTypeRef(operand->getType().getSwiftRValueType()),
        (unsigned)operand->getType().getCategory(),
        addValueRef(operand));
    break;
  }

  case SILInstructionKind::AssignInst:
  case SILInstructionKind::CopyAddrInst:
  case SILInstructionKind::StoreInst:
  case SILInstructionKind::StoreBorrowInst:
  case SILInstructionKind::StoreUnownedInst:
  case SILInstructionKind::StoreWeakInst: {
    SILValue operand, value;
    unsigned Attr = 0;
    if (SI.getKind() == SILInstructionKind::StoreWeakInst) {
      Attr = cast<StoreWeakInst>(&SI)->isInitializationOfDest();
      operand = cast<StoreWeakInst>(&SI)->getDest();
      value = cast<StoreWeakInst>(&SI)->getSrc();
    } else if (SI.getKind() == SILInstructionKind::StoreUnownedInst) {
      Attr = cast<StoreUnownedInst>(&SI)->isInitializationOfDest();
      operand = cast<StoreUnownedInst>(&SI)->getDest();
      value = cast<StoreUnownedInst>(&SI)->getSrc();
    } else if (SI.getKind() == SILInstructionKind::StoreInst) {
      Attr = unsigned(cast<StoreInst>(&SI)->getOwnershipQualifier());
      operand = cast<StoreInst>(&SI)->getDest();
      value = cast<StoreInst>(&SI)->getSrc();
    } else if (SI.getKind() == SILInstructionKind::AssignInst) {
      operand = cast<AssignInst>(&SI)->getDest();
      value = cast<AssignInst>(&SI)->getSrc();
    } else if (SI.getKind() == SILInstructionKind::CopyAddrInst) {
      const CopyAddrInst *CAI = cast<CopyAddrInst>(&SI);
      Attr = (CAI->isInitializationOfDest() << 1) | CAI->isTakeOfSrc();
      operand = cast<CopyAddrInst>(&SI)->getDest();
      value = cast<CopyAddrInst>(&SI)->getSrc();
    } else if (auto *SBI = dyn_cast<StoreBorrowInst>(&SI)) {
      operand = SBI->getDest();
      value = SBI->getSrc();
    } else {
      llvm_unreachable("switch out of sync");
    }

    unsigned abbrCode = SILAbbrCodes[SILOneValueOneOperandLayout::Code];
    SILOneValueOneOperandLayout::emitRecord(Out, ScratchRecord, abbrCode,
                  (unsigned)SI.getKind(), Attr, addValueRef(value),
                  S.addTypeRef(operand->getType().getSwiftRValueType()),
                  (unsigned)operand->getType().getCategory(),
                  addValueRef(operand));
    break;
  }
  case SILInstructionKind::BindMemoryInst: {
    auto *BI = cast<BindMemoryInst>(&SI);
    SILValue baseOperand = BI->getBase();
    SILValue indexOperand = BI->getIndex();
    SILType boundType = BI->getBoundType();
    SmallVector<ValueID, 6> ListOfValues;
    ListOfValues.push_back(S.addTypeRef(
                             baseOperand->getType().getSwiftRValueType()));
    ListOfValues.push_back((unsigned)baseOperand->getType().getCategory());
    ListOfValues.push_back(addValueRef(baseOperand));
    ListOfValues.push_back(S.addTypeRef(
                             indexOperand->getType().getSwiftRValueType()));
    ListOfValues.push_back((unsigned)indexOperand->getType().getCategory());
    ListOfValues.push_back(addValueRef(indexOperand));

    SILOneTypeValuesLayout::emitRecord(
      Out,
      ScratchRecord,
      SILAbbrCodes[SILOneTypeValuesLayout::Code],
      (unsigned)SI.getKind(),
      S.addTypeRef(boundType.getSwiftRValueType()),
      (unsigned)boundType.getCategory(),
      ListOfValues);
    break;
  }
  case SILInstructionKind::RefElementAddrInst:
  case SILInstructionKind::StructElementAddrInst:
  case SILInstructionKind::StructExtractInst:
  case SILInstructionKind::InitEnumDataAddrInst:
  case SILInstructionKind::UncheckedEnumDataInst:
  case SILInstructionKind::UncheckedTakeEnumDataAddrInst:
  case SILInstructionKind::InjectEnumAddrInst: {
    // Has a typed valueref and a field decl. We use SILOneValueOneOperandLayout
    // where the field decl is streamed as a ValueID.
    SILValue operand;
    Decl *tDecl;
    switch (SI.getKind()) {
    default: llvm_unreachable("Out of sync with parent switch");
    case SILInstructionKind::RefElementAddrInst:
      operand = cast<RefElementAddrInst>(&SI)->getOperand();
      tDecl = cast<RefElementAddrInst>(&SI)->getField();
      break;
    case SILInstructionKind::StructElementAddrInst:
      operand = cast<StructElementAddrInst>(&SI)->getOperand();
      tDecl = cast<StructElementAddrInst>(&SI)->getField();
      break;
    case SILInstructionKind::StructExtractInst:
      operand = cast<StructExtractInst>(&SI)->getOperand();
      tDecl = cast<StructExtractInst>(&SI)->getField();
      break;
    case SILInstructionKind::InitEnumDataAddrInst:
      operand = cast<InitEnumDataAddrInst>(&SI)->getOperand();
      tDecl = cast<InitEnumDataAddrInst>(&SI)->getElement();
      break;
    case SILInstructionKind::UncheckedEnumDataInst:
      operand = cast<UncheckedEnumDataInst>(&SI)->getOperand();
      tDecl = cast<UncheckedEnumDataInst>(&SI)->getElement();
      break;
    case SILInstructionKind::UncheckedTakeEnumDataAddrInst:
      operand = cast<UncheckedTakeEnumDataAddrInst>(&SI)->getOperand();
      tDecl = cast<UncheckedTakeEnumDataAddrInst>(&SI)->getElement();
      break;
    case SILInstructionKind::InjectEnumAddrInst:
      operand = cast<InjectEnumAddrInst>(&SI)->getOperand();
      tDecl = cast<InjectEnumAddrInst>(&SI)->getElement();
      break;
    }
    SILOneValueOneOperandLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneValueOneOperandLayout::Code],
        (unsigned)SI.getKind(), 0, S.addDeclRef(tDecl),
        S.addTypeRef(operand->getType().getSwiftRValueType()),
        (unsigned)operand->getType().getCategory(),
        addValueRef(operand));
    break;
  }
  case SILInstructionKind::RefTailAddrInst: {
    auto *RTAI = cast<RefTailAddrInst>(&SI);
    writeOneTypeOneOperandLayout(RTAI->getKind(), 0,
                                 RTAI->getType(),
                                 RTAI->getOperand());
    break;
  }
  case SILInstructionKind::StructInst: {
    // Format: a type followed by a list of typed values. A typed value is
    // expressed by 4 IDs: TypeID, TypeCategory, ValueID, ValueResultNumber.
    const StructInst *StrI = cast<StructInst>(&SI);
    SmallVector<ValueID, 4> ListOfValues;
    for (auto Elt : StrI->getElements()) {
      ListOfValues.push_back(S.addTypeRef(Elt->getType().getSwiftRValueType()));
      ListOfValues.push_back((unsigned)Elt->getType().getCategory());
      ListOfValues.push_back(addValueRef(Elt));
    }

    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code],
        (unsigned)SI.getKind(),
        S.addTypeRef(StrI->getType().getSwiftRValueType()),
        (unsigned)StrI->getType().getCategory(), ListOfValues);
    break;
  }
  case SILInstructionKind::TupleElementAddrInst:
  case SILInstructionKind::TupleExtractInst: {
    SILValue operand;
    unsigned FieldNo;
    switch (SI.getKind()) {
    default: llvm_unreachable("Out of sync with parent switch");
    case SILInstructionKind::TupleElementAddrInst:
      operand = cast<TupleElementAddrInst>(&SI)->getOperand();
      FieldNo = cast<TupleElementAddrInst>(&SI)->getFieldNo();
      break;
    case SILInstructionKind::TupleExtractInst:
      operand = cast<TupleExtractInst>(&SI)->getOperand();
      FieldNo = cast<TupleExtractInst>(&SI)->getFieldNo();
      break;
    }

    // Use OneTypeOneOperand layout where the field number is stored in TypeID.
    SILOneTypeOneOperandLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeOneOperandLayout::Code],
        (unsigned)SI.getKind(), 0,
        FieldNo, 0,
        S.addTypeRef(operand->getType().getSwiftRValueType()),
        (unsigned)operand->getType().getCategory(),
        addValueRef(operand));
    break;
  }
  case SILInstructionKind::TupleInst: {
    // Format: a type followed by a list of values. A value is expressed by
    // 2 IDs: ValueID, ValueResultNumber.
    const TupleInst *TI = cast<TupleInst>(&SI);
    SmallVector<ValueID, 4> ListOfValues;
    for (auto Elt : TI->getElements()) {
      ListOfValues.push_back(addValueRef(Elt));
    }

    unsigned abbrCode = SILAbbrCodes[SILOneTypeValuesLayout::Code];
    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord, abbrCode,
        (unsigned)SI.getKind(),
        S.addTypeRef(TI->getType().getSwiftRValueType()),
        (unsigned)TI->getType().getCategory(),
        ListOfValues);
    break;
  }
  case SILInstructionKind::EnumInst: {
    // Format: a type, an operand and a decl ID. Use SILTwoOperandsLayout: type,
    // (DeclID + hasOperand), and an operand.
    const EnumInst *UI = cast<EnumInst>(&SI);
    TypeID OperandTy = UI->hasOperand() ?
      S.addTypeRef(UI->getOperand()->getType().getSwiftRValueType()) : TypeID();
    unsigned OperandTyCategory = UI->hasOperand() ?
        (unsigned)UI->getOperand()->getType().getCategory() : 0;
    SILTwoOperandsLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILTwoOperandsLayout::Code], (unsigned)SI.getKind(),
        UI->hasOperand(),
        S.addTypeRef(UI->getType().getSwiftRValueType()),
        (unsigned)UI->getType().getCategory(),
        S.addDeclRef(UI->getElement()),
        OperandTy, OperandTyCategory,
        UI->hasOperand() ? addValueRef(UI->getOperand()) : ValueID());
    break;
  }
  case SILInstructionKind::WitnessMethodInst: {
    // Format: a type, an operand and a SILDeclRef. Use SILOneTypeValuesLayout:
    // type, Attr, SILDeclRef (DeclID, Kind, uncurryLevel, IsObjC), and a type.
    const WitnessMethodInst *WMI = cast<WitnessMethodInst>(&SI);
    CanType Ty = WMI->getLookupType();
    SILType Ty2 = WMI->getType();

    SmallVector<ValueID, 8> ListOfValues;
    handleSILDeclRef(S, WMI->getMember(), ListOfValues);

    // Add an optional operand.
    TypeID OperandTy = TypeID();
    unsigned OperandTyCategory = 0;
    SILValue OptionalOpenedExistential = SILValue();
    auto OperandValueId = addValueRef(OptionalOpenedExistential);

    SILInstWitnessMethodLayout::emitRecord(
        Out, ScratchRecord, SILAbbrCodes[SILInstWitnessMethodLayout::Code],
        S.addTypeRef(Ty), 0, 0,
        S.addTypeRef(Ty2.getSwiftRValueType()), (unsigned)Ty2.getCategory(),
        OperandTy, OperandTyCategory, OperandValueId, ListOfValues);

    S.writeConformance(WMI->getConformance(), SILAbbrCodes);

    break;
  }
  case SILInstructionKind::ClassMethodInst: {
    // Format: a type, an operand and a SILDeclRef. Use SILOneTypeValuesLayout:
    // type, Attr, SILDeclRef (DeclID, Kind, uncurryLevel),
    // and an operand.
    const ClassMethodInst *CMI = cast<ClassMethodInst>(&SI);
    SILType Ty = CMI->getType();
    SmallVector<ValueID, 9> ListOfValues;
    handleMethodInst(CMI, CMI->getOperand(), ListOfValues);

    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code], (unsigned)SI.getKind(),
        S.addTypeRef(Ty.getSwiftRValueType()),
        (unsigned)Ty.getCategory(), ListOfValues);
    break;
  }
  case SILInstructionKind::SuperMethodInst: {
    // Format: a type, an operand and a SILDeclRef. Use SILOneTypeValuesLayout:
    // type, Attr, SILDeclRef (DeclID, Kind, uncurryLevel),
    // and an operand.
    const SuperMethodInst *SMI = cast<SuperMethodInst>(&SI);
    SILType Ty = SMI->getType();
    SmallVector<ValueID, 9> ListOfValues;
    handleMethodInst(SMI, SMI->getOperand(), ListOfValues);

    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code], (unsigned)SI.getKind(),
        S.addTypeRef(Ty.getSwiftRValueType()),
        (unsigned)Ty.getCategory(), ListOfValues);
    break;
  }
  case SILInstructionKind::ObjCMethodInst: {
    // Format: a type, an operand and a SILDeclRef. Use SILOneTypeValuesLayout:
    // type, Attr, SILDeclRef (DeclID, Kind, uncurryLevel),
    // and an operand.
    const ObjCMethodInst *OMI = cast<ObjCMethodInst>(&SI);
    SILType Ty = OMI->getType();
    SmallVector<ValueID, 9> ListOfValues;
    handleMethodInst(OMI, OMI->getOperand(), ListOfValues);

    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code], (unsigned)SI.getKind(),
        S.addTypeRef(Ty.getSwiftRValueType()),
        (unsigned)Ty.getCategory(), ListOfValues);
    break;
  }
  case SILInstructionKind::ObjCSuperMethodInst: {
    // Format: a type, an operand and a SILDeclRef. Use SILOneTypeValuesLayout:
    // type, Attr, SILDeclRef (DeclID, Kind, uncurryLevel),
    // and an operand.
    const ObjCSuperMethodInst *SMI = cast<ObjCSuperMethodInst>(&SI);
    SILType Ty = SMI->getType();
    SmallVector<ValueID, 9> ListOfValues;
    handleMethodInst(SMI, SMI->getOperand(), ListOfValues);

    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code], (unsigned)SI.getKind(),
        S.addTypeRef(Ty.getSwiftRValueType()),
        (unsigned)Ty.getCategory(), ListOfValues);
    break;
  }
  case SILInstructionKind::DynamicMethodBranchInst: {
    // Format: a typed value, a SILDeclRef, a BasicBlock ID for method,
    // a BasicBlock ID for no method. Use SILOneTypeValuesLayout.
    const DynamicMethodBranchInst *DMB = cast<DynamicMethodBranchInst>(&SI);
    SmallVector<ValueID, 8> ListOfValues;
    ListOfValues.push_back(addValueRef(DMB->getOperand()));
    handleSILDeclRef(S, DMB->getMember(), ListOfValues);
    ListOfValues.push_back(BasicBlockMap[DMB->getHasMethodBB()]);
    ListOfValues.push_back(BasicBlockMap[DMB->getNoMethodBB()]);

    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[SILOneTypeValuesLayout::Code], (unsigned)SI.getKind(),
        S.addTypeRef(DMB->getOperand()->getType().getSwiftRValueType()),
        (unsigned)DMB->getOperand()->getType().getCategory(), ListOfValues);
    break;
  }
  case SILInstructionKind::CheckedCastBranchInst: {
    // Format: the cast kind, a typed value, a BasicBlock ID for success,
    // a BasicBlock ID for failure. Uses SILOneTypeValuesLayout.
    const CheckedCastBranchInst *CBI = cast<CheckedCastBranchInst>(&SI);
    SmallVector<ValueID, 8> ListOfValues;
    ListOfValues.push_back(CBI->isExact()),
    ListOfValues.push_back(addValueRef(CBI->getOperand()));
    ListOfValues.push_back(
               S.addTypeRef(CBI->getOperand()->getType().getSwiftRValueType()));
    ListOfValues.push_back((unsigned)CBI->getOperand()->getType().getCategory());
    ListOfValues.push_back(BasicBlockMap[CBI->getSuccessBB()]);
    ListOfValues.push_back(BasicBlockMap[CBI->getFailureBB()]);

    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
             SILAbbrCodes[SILOneTypeValuesLayout::Code], (unsigned)SI.getKind(),
             S.addTypeRef(CBI->getCastType().getSwiftRValueType()),
             (unsigned)CBI->getCastType().getCategory(),
             ListOfValues);
    break;
  }
  case SILInstructionKind::CheckedCastValueBranchInst: {
    // Format: the cast kind, a typed value, a BasicBlock ID for success,
    // a BasicBlock ID for failure. Uses SILOneTypeValuesLayout.
    const CheckedCastValueBranchInst *CBI =
        cast<CheckedCastValueBranchInst>(&SI);
    SmallVector<ValueID, 8> ListOfValues;
    ListOfValues.push_back(addValueRef(CBI->getOperand()));
    ListOfValues.push_back(
        S.addTypeRef(CBI->getOperand()->getType().getSwiftRValueType()));
    ListOfValues.push_back(
        (unsigned)CBI->getOperand()->getType().getCategory());
    ListOfValues.push_back(BasicBlockMap[CBI->getSuccessBB()]);
    ListOfValues.push_back(BasicBlockMap[CBI->getFailureBB()]);

    SILOneTypeValuesLayout::emitRecord(
        Out, ScratchRecord, SILAbbrCodes[SILOneTypeValuesLayout::Code],
        (unsigned)SI.getKind(),
        S.addTypeRef(CBI->getCastType().getSwiftRValueType()),
        (unsigned)CBI->getCastType().getCategory(), ListOfValues);
    break;
  }
  case SILInstructionKind::CheckedCastAddrBranchInst: {
    // Format: the cast kind, two typed values, a BasicBlock ID for
    // success, a BasicBlock ID for failure.  Uses SILOneTypeValuesLayout;
    // the type is the type of the second (dest) operand.
    auto CBI = cast<CheckedCastAddrBranchInst>(&SI);
    ValueID listOfValues[] = {
      toStableCastConsumptionKind(CBI->getConsumptionKind()),
      S.addTypeRef(CBI->getSourceType()),
      addValueRef(CBI->getSrc()),
      S.addTypeRef(CBI->getSrc()->getType().getSwiftRValueType()),
      (unsigned)CBI->getSrc()->getType().getCategory(),
      S.addTypeRef(CBI->getTargetType()),
      addValueRef(CBI->getDest()),
      BasicBlockMap[CBI->getSuccessBB()],
      BasicBlockMap[CBI->getFailureBB()]
    };
    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
             SILAbbrCodes[SILOneTypeValuesLayout::Code], (unsigned)SI.getKind(),
             S.addTypeRef(CBI->getDest()->getType().getSwiftRValueType()),
             (unsigned)CBI->getDest()->getType().getCategory(),
             llvm::makeArrayRef(listOfValues));
    break;
  }
  case SILInstructionKind::InitBlockStorageHeaderInst: {
    auto IBSHI = cast<InitBlockStorageHeaderInst>(&SI);
    SmallVector<ValueID, 6> ListOfValues;
    ListOfValues.push_back(addValueRef(IBSHI->getBlockStorage()));
    ListOfValues.push_back(
         S.addTypeRef(IBSHI->getBlockStorage()->getType().getSwiftRValueType()));
    // Always an address, don't need to save category
    
    ListOfValues.push_back(addValueRef(IBSHI->getInvokeFunction()));
    ListOfValues.push_back(
       S.addTypeRef(IBSHI->getInvokeFunction()->getType().getSwiftRValueType()));
    // Always a value, don't need to save category
    ListOfValues.push_back(IBSHI->getSubstitutions().size());
    
    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
             SILAbbrCodes[SILOneTypeValuesLayout::Code], (unsigned)SI.getKind(),
             S.addTypeRef(IBSHI->getType().getSwiftRValueType()),
             (unsigned)IBSHI->getType().getCategory(),
             ListOfValues);
    S.writeSubstitutions(IBSHI->getSubstitutions(), SILAbbrCodes);

    break;
  }
  case SILInstructionKind::KeyPathInst: {
    auto KPI = cast<KeyPathInst>(&SI);
    SmallVector<ValueID, 6> ListOfValues;

    auto pattern = KPI->getPattern();
    ListOfValues.push_back(S.addTypeRef(pattern->getRootType()));
    ListOfValues.push_back(S.addTypeRef(pattern->getValueType()));
    ListOfValues.push_back(pattern->getComponents().size());
    ListOfValues.push_back(pattern->getNumOperands());
    ListOfValues.push_back(KPI->getSubstitutions().size());
    
    ListOfValues.push_back(
       S.addDeclBaseNameRef(Ctx.getIdentifier(pattern->getObjCString())));

    ArrayRef<Requirement> reqts;
    if (auto sig = pattern->getGenericSignature()) {
      ListOfValues.push_back(sig->getGenericParams().size());
      for (auto param : sig->getGenericParams())
        ListOfValues.push_back(S.addTypeRef(param));
      reqts = sig->getRequirements();
    } else {
      ListOfValues.push_back(0);
    }
    
    SmallVector<ProtocolConformanceRef, 4> hashableConformances;
    
    for (auto &component : pattern->getComponents()) {
      auto handleComponentCommon = [&](KeyPathComponentKindEncoding kind) {
        ListOfValues.push_back((unsigned)kind);
        ListOfValues.push_back(S.addTypeRef(component.getComponentType()));
      };
      auto handleComputedId = [&](KeyPathPatternComponent::ComputedPropertyId id) {
        switch (id.getKind()) {
        case KeyPathPatternComponent::ComputedPropertyId::Property:
          ListOfValues.push_back(
            (unsigned)KeyPathComputedComponentIdKindEncoding::Property);
          ListOfValues.push_back(S.addDeclRef(id.getProperty()));
          break;
        case KeyPathPatternComponent::ComputedPropertyId::Function:
          ListOfValues.push_back(
            (unsigned)KeyPathComputedComponentIdKindEncoding::Function);
          ListOfValues.push_back(addSILFunctionRef(id.getFunction()));
          break;
        case KeyPathPatternComponent::ComputedPropertyId::DeclRef:
          ListOfValues.push_back(
            (unsigned)KeyPathComputedComponentIdKindEncoding::DeclRef);
          handleSILDeclRef(S, id.getDeclRef(), ListOfValues);
          break;
        }
      };
      auto handleComputedIndices
        = [&](const KeyPathPatternComponent &component) {
          auto indices = component.getComputedPropertyIndices();
          ListOfValues.push_back(indices.size());
          for (auto &index : indices) {
            ListOfValues.push_back(index.Operand);
            ListOfValues.push_back(S.addTypeRef(index.FormalType));
            ListOfValues.push_back(
              S.addTypeRef(index.LoweredType.getSwiftRValueType()));
            ListOfValues.push_back((unsigned)index.LoweredType.getCategory());
            hashableConformances.push_back(index.Hashable);
          }
          if (!indices.empty()) {
            ListOfValues.push_back(
              addSILFunctionRef(component.getComputedPropertyIndexEquals()));
            ListOfValues.push_back(
              addSILFunctionRef(component.getComputedPropertyIndexHash()));
          }
        };
    
      switch (component.getKind()) {
      case KeyPathPatternComponent::Kind::StoredProperty:
        handleComponentCommon(KeyPathComponentKindEncoding::StoredProperty);
        ListOfValues.push_back(S.addDeclRef(component.getStoredPropertyDecl()));
        break;
      case KeyPathPatternComponent::Kind::GettableProperty:
        handleComponentCommon(KeyPathComponentKindEncoding::GettableProperty);
        handleComputedId(component.getComputedPropertyId());
        ListOfValues.push_back(
                      addSILFunctionRef(component.getComputedPropertyGetter()));
        handleComputedIndices(component);
        break;
      case KeyPathPatternComponent::Kind::SettableProperty:
        handleComponentCommon(KeyPathComponentKindEncoding::SettableProperty);
        handleComputedId(component.getComputedPropertyId());
        ListOfValues.push_back(
                      addSILFunctionRef(component.getComputedPropertyGetter()));
        ListOfValues.push_back(
                      addSILFunctionRef(component.getComputedPropertySetter()));
        handleComputedIndices(component);
        break;
      case KeyPathPatternComponent::Kind::OptionalChain:
        handleComponentCommon(KeyPathComponentKindEncoding::OptionalChain);
        break;
      case KeyPathPatternComponent::Kind::OptionalForce:
        handleComponentCommon(KeyPathComponentKindEncoding::OptionalForce);
        break;
      case KeyPathPatternComponent::Kind::OptionalWrap:
        handleComponentCommon(KeyPathComponentKindEncoding::OptionalWrap);
        break;
      }
    }
    
    for (auto &operand : KPI->getAllOperands()) {
      auto value = operand.get();
      ListOfValues.push_back(addValueRef(value));
      ListOfValues.push_back(S.addTypeRef(value->getType().getSwiftRValueType()));
      ListOfValues.push_back((unsigned)value->getType().getCategory());
    }
    
    SILOneTypeValuesLayout::emitRecord(Out, ScratchRecord,
         SILAbbrCodes[SILOneTypeValuesLayout::Code], (unsigned)SI.getKind(),
         S.addTypeRef(KPI->getType().getSwiftRValueType()),
         (unsigned)KPI->getType().getCategory(),
         ListOfValues);
    for (auto conformance : hashableConformances) {
      S.writeConformance(conformance, SILAbbrCodes);
    }
    S.writeGenericRequirements(reqts, SILAbbrCodes);
    S.writeSubstitutions(KPI->getSubstitutions(), SILAbbrCodes);

    break;
  }
  case SILInstructionKind::MarkUninitializedBehaviorInst:
    llvm_unreachable("todo");
  }
  // Non-void values get registered in the value table.
  for (auto result : SI.getResults()) {
    addValueRef(result);
    ++InstID;
  }
}

/// Depending on the RecordKind, we write the SILFunction table, the global
/// variable table, the table for SILVTable, or the table for SILWitnessTable.
static void writeIndexTable(const sil_index_block::ListLayout &List,
                            sil_index_block::RecordKind kind,
                            const SILSerializer::Table &table) {
  assert((kind == sil_index_block::SIL_FUNC_NAMES ||
          kind == sil_index_block::SIL_VTABLE_NAMES ||
          kind == sil_index_block::SIL_GLOBALVAR_NAMES ||
          kind == sil_index_block::SIL_WITNESS_TABLE_NAMES ||
          kind == sil_index_block::SIL_DEFAULT_WITNESS_TABLE_NAMES) &&
         "SIL function table, global, vtable and (default) witness table "
         "are supported");
  llvm::SmallString<4096> hashTableBlob;
  uint32_t tableOffset;
  {
    llvm::OnDiskChainedHashTableGenerator<FuncTableInfo> generator;
    for (auto &entry : table)
      generator.insert(entry.first, entry.second);

    llvm::raw_svector_ostream blobStream(hashTableBlob);
    // Make sure that no bucket is at offset 0.
    endian::Writer<little>(blobStream).write<uint32_t>(0);
    tableOffset = generator.Emit(blobStream);
  }
  SmallVector<uint64_t, 8> scratch;
  List.emit(scratch, kind, tableOffset, hashTableBlob);
}

void SILSerializer::writeIndexTables() {
  BCBlockRAII restoreBlock(Out, SIL_INDEX_BLOCK_ID, 4);

  sil_index_block::ListLayout List(Out);
  sil_index_block::OffsetLayout Offset(Out);
  if (!FuncTable.empty()) {
    writeIndexTable(List, sil_index_block::SIL_FUNC_NAMES, FuncTable);
    Offset.emit(ScratchRecord, sil_index_block::SIL_FUNC_OFFSETS, Funcs);
  }

  if (!VTableList.empty()) {
    writeIndexTable(List, sil_index_block::SIL_VTABLE_NAMES, VTableList);
    Offset.emit(ScratchRecord, sil_index_block::SIL_VTABLE_OFFSETS,
                VTableOffset);
  }

  if (!GlobalVarList.empty()) {
    writeIndexTable(List, sil_index_block::SIL_GLOBALVAR_NAMES, GlobalVarList);
    Offset.emit(ScratchRecord, sil_index_block::SIL_GLOBALVAR_OFFSETS,
                GlobalVarOffset);
  }

  if (!WitnessTableList.empty()) {
    writeIndexTable(List, sil_index_block::SIL_WITNESS_TABLE_NAMES,
                    WitnessTableList);
    Offset.emit(ScratchRecord, sil_index_block::SIL_WITNESS_TABLE_OFFSETS,
                WitnessTableOffset);
  }

  if (!DefaultWitnessTableList.empty()) {
    writeIndexTable(List, sil_index_block::SIL_DEFAULT_WITNESS_TABLE_NAMES,
                    DefaultWitnessTableList);
    Offset.emit(ScratchRecord,
                sil_index_block::SIL_DEFAULT_WITNESS_TABLE_OFFSETS,
                DefaultWitnessTableOffset);
  }
}

void SILSerializer::writeSILGlobalVar(const SILGlobalVariable &g) {
  GlobalVarList[Ctx.getIdentifier(g.getName())] = NextGlobalVarID++;
  GlobalVarOffset.push_back(Out.GetCurrentBitNo());
  TypeID TyID = S.addTypeRef(g.getLoweredType().getSwiftRValueType());
  DeclID dID = S.addDeclRef(g.getDecl());
  SILGlobalVarLayout::emitRecord(Out, ScratchRecord,
                                 SILAbbrCodes[SILGlobalVarLayout::Code],
                                 toStableSILLinkage(g.getLinkage()),
                                 g.isSerialized() ? 1 : 0,
                                 (unsigned)!g.isDefinition(),
                                 (unsigned)g.isLet(),
                                 TyID, dID);
}

void SILSerializer::writeSILVTable(const SILVTable &vt) {
  // Do not emit vtables for non-public classes unless everything has to be
  // serialized.
  if (!ShouldSerializeAll &&
      vt.getClass()->getEffectiveAccess() < swift::AccessLevel::Public)
    return;
  VTableList[vt.getClass()->getName()] = NextVTableID++;
  VTableOffset.push_back(Out.GetCurrentBitNo());
  VTableLayout::emitRecord(Out, ScratchRecord, SILAbbrCodes[VTableLayout::Code],
                           S.addDeclRef(vt.getClass()),
                           vt.isSerialized() == IsSerialized ? 1 : 0);

  for (auto &entry : vt.getEntries()) {
    SmallVector<ValueID, 4> ListOfValues;
    // Do not emit entries which are not public or serialized, unless everything
    // has to be serialized.
    if (!ShouldSerializeAll && entry.Implementation &&
        !entry.Implementation->isPossiblyUsedExternally() &&
        !entry.Implementation->isSerialized())
      continue;
    handleSILDeclRef(S, entry.Method, ListOfValues);
    addReferencedSILFunction(entry.Implementation, true);
    // Each entry is a pair of SILDeclRef and SILFunction.
    VTableEntryLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[VTableEntryLayout::Code],
        // SILFunction name
        S.addDeclBaseNameRef(Ctx.getIdentifier(entry.Implementation->getName())),
        toStableVTableEntryKind(entry.TheKind),
        toStableSILLinkage(entry.Linkage),
        ListOfValues);
  }
}

void SILSerializer::writeSILWitnessTable(const SILWitnessTable &wt) {
  WitnessTableList[wt.getIdentifier()] = NextWitnessTableID++;
  WitnessTableOffset.push_back(Out.GetCurrentBitNo());

  WitnessTableLayout::emitRecord(
    Out, ScratchRecord,
    SILAbbrCodes[WitnessTableLayout::Code],
    toStableSILLinkage(wt.getLinkage()),
    unsigned(wt.isDeclaration()),
    wt.isSerialized() == IsSerialized ? 1 : 0);

  S.writeConformance(wt.getConformance(), SILAbbrCodes);

  // If we have a declaration, do not attempt to serialize entries.
  if (wt.isDeclaration())
    return;

  for (auto &entry : wt.getEntries()) {
    if (entry.getKind() == SILWitnessTable::BaseProtocol) {
      auto &baseWitness = entry.getBaseProtocolWitness();

      WitnessBaseEntryLayout::emitRecord(Out, ScratchRecord,
          SILAbbrCodes[WitnessBaseEntryLayout::Code],
          S.addDeclRef(baseWitness.Requirement));

      S.writeConformance(baseWitness.Witness, SILAbbrCodes);
      continue;
    }
    if (entry.getKind() == SILWitnessTable::AssociatedTypeProtocol) {
      auto &assoc = entry.getAssociatedTypeProtocolWitness();

      WitnessAssocProtocolLayout::emitRecord(
        Out, ScratchRecord,
        SILAbbrCodes[WitnessAssocProtocolLayout::Code],
        S.addTypeRef(assoc.Requirement),
        S.addDeclRef(assoc.Protocol));
          
      S.writeConformance(assoc.Witness, SILAbbrCodes);
      continue;
    }
    if (entry.getKind() == SILWitnessTable::AssociatedType) {
      auto &assoc = entry.getAssociatedTypeWitness();
      WitnessAssocEntryLayout::emitRecord(Out, ScratchRecord,
          SILAbbrCodes[WitnessAssocEntryLayout::Code],
          S.addDeclRef(assoc.Requirement),
          S.addTypeRef(assoc.Witness));
      continue;
    }
    auto &methodWitness = entry.getMethodWitness();
    SmallVector<ValueID, 4> ListOfValues;
    handleSILDeclRef(S, methodWitness.Requirement, ListOfValues);
    IdentifierID witnessID = 0;
    if (SILFunction *witness = methodWitness.Witness) {
      addReferencedSILFunction(witness, true);
      witnessID = S.addDeclBaseNameRef(Ctx.getIdentifier(witness->getName()));
    }
    WitnessMethodEntryLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[WitnessMethodEntryLayout::Code],
        // SILFunction name
        witnessID,
        ListOfValues);
  }

  for (auto conditional : wt.getConditionalConformances()) {
    WitnessConditionalConformanceLayout::emitRecord(
        Out, ScratchRecord,
        SILAbbrCodes[WitnessConditionalConformanceLayout::Code],
        S.addTypeRef(conditional.Requirement));

    S.writeConformance(conditional.Conformance, SILAbbrCodes);
    continue;
  }
}

void SILSerializer::
writeSILDefaultWitnessTable(const SILDefaultWitnessTable &wt) {
  if (wt.isDeclaration())
    return;

  DefaultWitnessTableList[wt.getIdentifier()] = NextDefaultWitnessTableID++;
  DefaultWitnessTableOffset.push_back(Out.GetCurrentBitNo());

  DefaultWitnessTableLayout::emitRecord(
    Out, ScratchRecord,
    SILAbbrCodes[DefaultWitnessTableLayout::Code],
    S.addDeclRef(wt.getProtocol()),
    toStableSILLinkage(wt.getLinkage()));

  for (auto &entry : wt.getEntries()) {
    if (!entry.isValid()) {
      DefaultWitnessTableNoEntryLayout::emitRecord(Out, ScratchRecord,
          SILAbbrCodes[DefaultWitnessTableNoEntryLayout::Code]);
      continue;
    }

    SmallVector<ValueID, 4> ListOfValues;
    handleSILDeclRef(S, entry.getRequirement(), ListOfValues);
    SILFunction *witness = entry.getWitness();
    addReferencedSILFunction(witness, true);
    IdentifierID witnessID = S.addDeclBaseNameRef(
        Ctx.getIdentifier(witness->getName()));
    DefaultWitnessTableEntryLayout::emitRecord(Out, ScratchRecord,
        SILAbbrCodes[DefaultWitnessTableEntryLayout::Code],
        // SILFunction name
        witnessID,
        ListOfValues);
  }
}

/// Helper function for whether to emit a function body.
bool SILSerializer::shouldEmitFunctionBody(const SILFunction *F,
                                           bool isReference) {

  // If F is a declaration, it has no body to emit...
  // The declaration will be serialized anyways if it is referenced anywhere.
  if (F->isExternalDeclaration())
    return false;

  // Never serialize any function definitions available externally, unless
  // it is a referenced shared function (see the explanation in
  // SILSerializer::writeSILFunction).
  // TODO: Special handling for resilient mode.
  if (F->isAvailableExternally() &&
      !(isReference && hasSharedVisibility(F->getLinkage())))
    return false;

  // If we are asked to serialize everything, go ahead and do it.
  if (ShouldSerializeAll)
    return true;

  // If F is serialized, we should always emit its body.
  if (F->isSerialized() == IsSerialized)
    return true;

  return false;
}

void SILSerializer::writeSILBlock(const SILModule *SILMod) {
  BCBlockRAII subBlock(Out, SIL_BLOCK_ID, 6);

  registerSILAbbr<SILFunctionLayout>();
  registerSILAbbr<SILBasicBlockLayout>();
  registerSILAbbr<SILOneValueOneOperandLayout>();
  registerSILAbbr<SILOneTypeLayout>();
  registerSILAbbr<SILOneOperandLayout>();
  registerSILAbbr<SILOneTypeOneOperandLayout>();
  registerSILAbbr<SILInitExistentialLayout>();
  registerSILAbbr<SILOneTypeValuesLayout>();
  registerSILAbbr<SILTwoOperandsLayout>();
  registerSILAbbr<SILTailAddrLayout>();
  registerSILAbbr<SILInstApplyLayout>();
  registerSILAbbr<SILInstNoOperandLayout>();

  registerSILAbbr<VTableLayout>();
  registerSILAbbr<VTableEntryLayout>();
  registerSILAbbr<SILGlobalVarLayout>();
  registerSILAbbr<WitnessTableLayout>();
  registerSILAbbr<WitnessMethodEntryLayout>();
  registerSILAbbr<WitnessBaseEntryLayout>();
  registerSILAbbr<WitnessAssocProtocolLayout>();
  registerSILAbbr<WitnessAssocEntryLayout>();
  registerSILAbbr<WitnessConditionalConformanceLayout>();
  registerSILAbbr<DefaultWitnessTableLayout>();
  registerSILAbbr<DefaultWitnessTableEntryLayout>();
  registerSILAbbr<DefaultWitnessTableNoEntryLayout>();

  registerSILAbbr<SILInstCastLayout>();
  registerSILAbbr<SILInstWitnessMethodLayout>();
  registerSILAbbr<SILSpecializeAttrLayout>();

  // Register the abbreviation codes so these layouts can exist in both
  // decl blocks and sil blocks.
  // We have to make sure BOUND_GENERIC_SUBSTITUTION does not overlap with
  // SIL-specific records.
  registerSILAbbr<decls_block::BoundGenericSubstitutionLayout>();
  registerSILAbbr<decls_block::AbstractProtocolConformanceLayout>();
  registerSILAbbr<decls_block::NormalProtocolConformanceLayout>();
  registerSILAbbr<decls_block::SpecializedProtocolConformanceLayout>();
  registerSILAbbr<decls_block::InheritedProtocolConformanceLayout>();
  registerSILAbbr<decls_block::NormalProtocolConformanceIdLayout>();
  registerSILAbbr<decls_block::ProtocolConformanceXrefLayout>();
  registerSILAbbr<decls_block::GenericRequirementLayout>();
  registerSILAbbr<decls_block::LayoutRequirementLayout>();

  for (const SILGlobalVariable &g : SILMod->getSILGlobals())
    writeSILGlobalVar(g);

  // Write out VTables first because it may require serializations of
  // non-transparent SILFunctions (body is not needed).
  // Go through all SILVTables in SILMod and write them if we should
  // serialize everything.
  // FIXME: Resilience: could write out vtable for fragile classes.
  const DeclContext *assocDC = SILMod->getAssociatedContext();
  assert(assocDC && "cannot serialize SIL without an associated DeclContext");
  for (const SILVTable &vt : SILMod->getVTables()) {
    if ((ShouldSerializeAll || vt.isSerialized()) &&
        vt.getClass()->isChildContextOf(assocDC))
      writeSILVTable(vt);
  }

  // Write out fragile WitnessTables.
  for (const SILWitnessTable &wt : SILMod->getWitnessTables()) {
    if ((ShouldSerializeAll || wt.isSerialized()) &&
        wt.getConformance()->getDeclContext()->isChildContextOf(assocDC))
      writeSILWitnessTable(wt);
  }

  // Write out DefaultWitnessTables.
  for (const SILDefaultWitnessTable &wt : SILMod->getDefaultWitnessTables()) {
    // FIXME: Don't need to serialize private and internal default witness
    // tables.
    if (wt.getProtocol()->getDeclContext()->isChildContextOf(assocDC))
      writeSILDefaultWitnessTable(wt);
  }

  // Emit only declarations if it is a module with pre-specializations.
  // And only do it in optimized builds.
  bool emitDeclarationsForOnoneSupport =
      SILMod->isOptimizedOnoneSupportModule();

  // Go through all the SILFunctions in SILMod and write out any
  // mandatory function bodies.
  for (const SILFunction &F : *SILMod) {
    if (emitDeclarationsForOnoneSupport) {
      // Only declarations of hardcoded pre-specializations with
      // public linkage need to be serialized as they will be used
      // by the UsePrespecializations pass during -Onone compilation to
      // check for availability of concrete pre-specializations.
      if (!hasPublicVisibility(F.getLinkage()) ||
          !isKnownPrespecialization(F.getName()))
        continue;
    }

    addMandatorySILFunction(&F, emitDeclarationsForOnoneSupport);
    processSILFunctionWorklist();
  }

  // Now write function declarations for every function we've
  // emitted a reference to without emitting a function body for.
  for (const SILFunction &F : *SILMod) {
    auto iter = FuncsToEmit.find(&F);
    if (iter != FuncsToEmit.end() && iter->second) {
      assert((emitDeclarationsForOnoneSupport ||
              !shouldEmitFunctionBody(&F)) &&
             "Should have emitted function body earlier");
      writeSILFunction(F, true);
    }
  }

  assert(Worklist.empty() && "Did not emit everything in worklist");
}

void SILSerializer::writeSILModule(const SILModule *SILMod) {
  writeSILBlock(SILMod);
  writeIndexTables();
}

void Serializer::writeSIL(const SILModule *SILMod, bool serializeAllSIL) {
  if (!SILMod)
    return;

  SILSerializer SILSer(*this, M->getASTContext(), Out, serializeAllSIL);
  SILSer.writeSILModule(SILMod);
}
