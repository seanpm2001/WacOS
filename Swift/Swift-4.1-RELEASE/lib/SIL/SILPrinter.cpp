//===--- SILPrinter.cpp - Pretty-printing of SIL Code ---------------------===//
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
/// This file defines the logic to pretty-print SIL, Instructions, etc.
///
//===----------------------------------------------------------------------===//

#include "swift/Strings.h"
#include "swift/Demangling/Demangle.h"
#include "swift/Basic/QuotedString.h"
#include "swift/SIL/SILPrintContext.h"
#include "swift/SIL/CFG.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILCoverageMap.h"
#include "swift/SIL/SILDebugScope.h"
#include "swift/SIL/SILDeclRef.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILVisitor.h"
#include "swift/SIL/SILVTable.h"
#include "swift/AST/Decl.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Module.h"
#include "swift/AST/PrintOptions.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/Types.h"
#include "swift/Basic/STLExtras.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/FileSystem.h"


using namespace swift;
using ID = SILPrintContext::ID;

llvm::cl::opt<bool>
SILPrintNoColor("sil-print-no-color", llvm::cl::init(""),
                llvm::cl::desc("Don't use color when printing SIL"));

llvm::cl::opt<bool>
SILFullDemangle("sil-full-demangle", llvm::cl::init(false),
                llvm::cl::desc("Fully demangle symbol names in SIL output"));

llvm::cl::opt<bool>
SILPrintDebugInfo("sil-print-debuginfo", llvm::cl::init(false),
                llvm::cl::desc("Include debug info in SIL output"));

llvm::cl::opt<bool> SILPrintGenericSpecializationInfo(
    "sil-print-generic-specialization-info", llvm::cl::init(false),
    llvm::cl::desc("Include generic specialization"
                   "information info in SIL output"));

static std::string demangleSymbol(StringRef Name) {
  if (SILFullDemangle)
    return Demangle::demangleSymbolAsString(Name);
  return Demangle::demangleSymbolAsString(Name,
                    Demangle::DemangleOptions::SimplifiedUIDemangleOptions());
}

enum SILColorKind {
  SC_Type,
};

namespace {
/// RAII based coloring of SIL output.
class SILColor {
  raw_ostream &OS;
  enum raw_ostream::Colors Color;
public:
#define DEF_COL(NAME, RAW) case NAME: Color = raw_ostream::RAW; break;

  explicit SILColor(raw_ostream &OS, SILColorKind K) : OS(OS) {
    if (!OS.has_colors() || SILPrintNoColor)
      return;
    switch (K) {
      DEF_COL(SC_Type, YELLOW)
    }
    OS.resetColor();
    OS.changeColor(Color);
  }

  explicit SILColor(raw_ostream &OS, ID::ID_Kind K) : OS(OS) {
    if (!OS.has_colors() || SILPrintNoColor)
      return;
    switch (K) {
      DEF_COL(ID::SILUndef, RED)
      DEF_COL(ID::SILBasicBlock, GREEN)
      DEF_COL(ID::SSAValue, MAGENTA)
    }
    OS.resetColor();
    OS.changeColor(Color);
  }
  
  ~SILColor() {
    if (!OS.has_colors() || SILPrintNoColor)
      return;
    // FIXME: instead of resetColor(), we can look into
    // capturing the current active color and restoring it.
    OS.resetColor();
  }
#undef DEF_COL
};
} // end anonymous namespace

void SILPrintContext::ID::print(raw_ostream &OS) {
  SILColor C(OS, Kind);
  switch (Kind) {
  case ID::SILUndef:
    OS << "undef";
    return;
  case ID::SILBasicBlock: OS << "bb"; break;
  case ID::SSAValue: OS << '%'; break;
  }
  OS << Number;
}

namespace swift {
raw_ostream &operator<<(raw_ostream &OS, SILPrintContext::ID i) {
  i.print(OS);
  return OS;
}
} // namespace swift

/// IDAndType - Used when a client wants to print something like "%0 : $Int".
struct SILValuePrinterInfo {
  ID ValueID;
  SILType Type;
  Optional<ValueOwnershipKind> OwnershipKind;

  SILValuePrinterInfo(ID ValueID) : ValueID(ValueID), Type(), OwnershipKind() {}
  SILValuePrinterInfo(ID ValueID, SILType Type)
      : ValueID(ValueID), Type(Type), OwnershipKind() {}
  SILValuePrinterInfo(ID ValueID, SILType Type,
                      ValueOwnershipKind OwnershipKind)
      : ValueID(ValueID), Type(Type), OwnershipKind(OwnershipKind) {}
};

/// Return the fully qualified dotted path for DeclContext.
static void printFullContext(const DeclContext *Context, raw_ostream &Buffer) {
  if (!Context)
    return;
  switch (Context->getContextKind()) {
  case DeclContextKind::Module:
    if (Context == cast<ModuleDecl>(Context)->getASTContext().TheBuiltinModule)
      Buffer << cast<ModuleDecl>(Context)->getName() << ".";
    return;

  case DeclContextKind::FileUnit:
    // Ignore the file; just print the module.
    printFullContext(Context->getParent(), Buffer);
    return;

  case DeclContextKind::Initializer:
    // FIXME
    Buffer << "<initializer>";
    return;

  case DeclContextKind::AbstractClosureExpr:
    // FIXME
    Buffer << "<anonymous function>";
    return;

  case DeclContextKind::SerializedLocal:
    Buffer << "<serialized local context>";
    return;

  case DeclContextKind::GenericTypeDecl: {
    auto *generic = cast<GenericTypeDecl>(Context);
    printFullContext(generic->getDeclContext(), Buffer);
    Buffer << generic->getName() << ".";
    return;
  }

  case DeclContextKind::ExtensionDecl: {
    Type Ty = cast<ExtensionDecl>(Context)->getExtendedType();
    TypeBase *Base = Ty->getCanonicalType().getPointer();
    const NominalTypeDecl *ExtNominal = nullptr;
    switch (Base->getKind()) {
      default:
        llvm_unreachable("unhandled context kind in SILPrint!");
      case TypeKind::Protocol:
        ExtNominal = cast<ProtocolType>(Base)->getDecl();
        break;
      case TypeKind::Enum:
        ExtNominal = cast<EnumType>(Base)->getDecl();
        break;
      case TypeKind::Struct:
        ExtNominal = cast<StructType>(Base)->getDecl();
        break;
      case TypeKind::Class:
        ExtNominal = cast<ClassType>(Base)->getDecl();
        break;
      case TypeKind::BoundGenericEnum:
        ExtNominal = cast<BoundGenericEnumType>(Base)->getDecl();
        break;
      case TypeKind::BoundGenericStruct:
        ExtNominal = cast<BoundGenericStructType>(Base)->getDecl();
        break;
      case TypeKind::BoundGenericClass:
        ExtNominal = cast<BoundGenericClassType>(Base)->getDecl();
        break;
    }
    printFullContext(ExtNominal->getDeclContext(), Buffer);
    Buffer << ExtNominal->getName() << ".";
    return;
  }
  
  case DeclContextKind::TopLevelCodeDecl:
    // FIXME
    Buffer << "<top level code>";
    return;
  case DeclContextKind::AbstractFunctionDecl:
    // FIXME
    Buffer << "<abstract function>";
    return;
  case DeclContextKind::SubscriptDecl:
    // FIXME
    Buffer << "<subscript>";
    return;
  }
  llvm_unreachable("bad decl context");
}

static void printValueDecl(ValueDecl *Decl, raw_ostream &OS) {
  printFullContext(Decl->getDeclContext(), OS);
  assert(Decl->hasName());

  if (Decl->isOperator()) {
    OS << '"' << Decl->getBaseName() << '"';
  } else {
    bool shouldEscape = !Decl->getBaseName().isSpecial() &&
        llvm::StringSwitch<bool>(Decl->getBaseName().userFacingName())
            // FIXME: Represent "init" by a special name and remove this case
            .Case("init", false)
#define KEYWORD(kw) \
            .Case(#kw, true)
#include "swift/Syntax/TokenKinds.def"
            .Default(false);

    if (shouldEscape) {
      OS << '`' << Decl->getBaseName().userFacingName() << '`';
    } else {
      OS << Decl->getBaseName().userFacingName();
    }
  }
}

/// SILDeclRef uses sigil "#" and prints the fully qualified dotted path.
void SILDeclRef::print(raw_ostream &OS) const {
  OS << "#";
  if (isNull()) {
    OS << "<null>";
    return;
  }

  bool isDot = true;
  if (!hasDecl()) {
    OS << "<anonymous function>";
  } else if (kind == SILDeclRef::Kind::Func) {
    auto *FD = cast<FuncDecl>(getDecl());
    switch (FD->getAccessorKind()) {
    case AccessorKind::IsWillSet:
      printValueDecl(FD->getAccessorStorageDecl(), OS);
      OS << "!willSet";
      break;
    case AccessorKind::IsDidSet:
      printValueDecl(FD->getAccessorStorageDecl(), OS);
      OS << "!didSet";
      break;
    case AccessorKind::NotAccessor:
      printValueDecl(FD, OS);
      isDot = false;
      break;
    case AccessorKind::IsGetter:
      printValueDecl(FD->getAccessorStorageDecl(), OS);
      OS << "!getter";
      break;
    case AccessorKind::IsSetter:
      printValueDecl(FD->getAccessorStorageDecl(), OS);
      OS << "!setter";
      break;
    case AccessorKind::IsMaterializeForSet:
      printValueDecl(FD->getAccessorStorageDecl(), OS);
      OS << "!materializeForSet";
      break;
    case AccessorKind::IsAddressor:
      printValueDecl(FD->getAccessorStorageDecl(), OS);
      OS << "!addressor";
      break;
    case AccessorKind::IsMutableAddressor:
      printValueDecl(FD->getAccessorStorageDecl(), OS);
      OS << "!mutableAddressor";
      break;
    }
  } else {
    printValueDecl(getDecl(), OS);
  }
  switch (kind) {
  case SILDeclRef::Kind::Func:
    break;
  case SILDeclRef::Kind::Allocator:
    OS << "!allocator";
    break;
  case SILDeclRef::Kind::Initializer:
    OS << "!initializer";
    break;
  case SILDeclRef::Kind::EnumElement:
    OS << "!enumelt";
    break;
  case SILDeclRef::Kind::Destroyer:
    OS << "!destroyer";
    break;
  case SILDeclRef::Kind::Deallocator:
    OS << "!deallocator";
    break;
  case SILDeclRef::Kind::IVarInitializer:
    OS << "!ivarinitializer";
    break;
  case SILDeclRef::Kind::IVarDestroyer:
    OS << "!ivardestroyer";
    break;
  case SILDeclRef::Kind::GlobalAccessor:
    OS << "!globalaccessor";
    break;
  case SILDeclRef::Kind::GlobalGetter:
    OS << "!globalgetter";
    break;
  case SILDeclRef::Kind::DefaultArgGenerator:
    OS << "!defaultarg" << "." << defaultArgIndex;
    break;
  case SILDeclRef::Kind::StoredPropertyInitializer:
    OS << "!propertyinit";
    break;
  }

  auto uncurryLevel = getParameterListCount() - 1;
  if (uncurryLevel != 0)
    OS << (isDot ? '.' : '!')  << uncurryLevel;

  if (isForeign)
    OS << ((isDot || uncurryLevel != 0) ? '.' : '!')  << "foreign";

  if (isDirectReference)
    OS << ((isDot || uncurryLevel != 0) ? '.' : '!')  << "direct";
}

void SILDeclRef::dump() const {
  print(llvm::errs());
  llvm::errs() << '\n';
}

/// Pretty-print the generic specialization information.
static void printGenericSpecializationInfo(
    raw_ostream &OS, StringRef Kind, StringRef Name,
    const GenericSpecializationInformation *SpecializationInfo,
    SubstitutionList Subs = SubstitutionList()) {
  if (!SpecializationInfo)
    return;

  auto PrintSubstitutions = [&](SubstitutionList Subs) {
    OS << '<';
    interleave(Subs,
               [&](const Substitution &s) { OS << s.getReplacement(); },
               [&] { OS << ", "; });
    OS << '>';
  };

  OS << "// Generic specialization information for " << Kind << " " << Name;
  if (!Subs.empty()) {
    OS << " ";
    PrintSubstitutions(Subs);
  }

  OS << ":\n";

  while (SpecializationInfo) {
    OS << "// Caller: " << SpecializationInfo->getCaller()->getName() << '\n';
    OS << "// Parent: " << SpecializationInfo->getParent()->getName() << '\n';
    OS << "// Substitutions: ";
    PrintSubstitutions(SpecializationInfo->getSubstitutions());
    OS << '\n';
    OS << "//\n";
    if (!SpecializationInfo->getCaller()->isSpecialization())
      return;
    SpecializationInfo =
      SpecializationInfo->getCaller()->getSpecializationInfo();
  }
}

static void print(raw_ostream &OS, SILValueCategory category) {
  switch (category) {
  case SILValueCategory::Object: return;
  case SILValueCategory::Address: OS << '*'; return;
  }
  llvm_unreachable("bad value category!");
}

static StringRef getCastConsumptionKindName(CastConsumptionKind kind) {
  switch (kind) {
  case CastConsumptionKind::TakeAlways: return "take_always";
  case CastConsumptionKind::TakeOnSuccess: return "take_on_success";
  case CastConsumptionKind::CopyOnSuccess: return "copy_on_success";
  }
  llvm_unreachable("bad cast consumption kind");
}

static void printSILTypeColorAndSigil(raw_ostream &OS, SILType t) {
  SILColor C(OS, SC_Type);
  OS << '$';
  
  // Potentially add a leading sigil for the value category.
  ::print(OS, t.getCategory());
}
      
void SILType::print(raw_ostream &OS) const {
  printSILTypeColorAndSigil(OS, *this);
  
  // Print other types as their Swift representation.
  PrintOptions SubPrinter = PrintOptions::printSIL();
  getSwiftRValueType().print(OS, SubPrinter);
}

void SILType::dump() const {
  print(llvm::errs());
  llvm::errs() << '\n';
}

namespace {
  
/// SILPrinter class - This holds the internal implementation details of
/// printing SIL structures.
class SILPrinter : public SILInstructionVisitor<SILPrinter> {
  SILPrintContext &Ctx;
  struct {
    llvm::formatted_raw_ostream OS;
    PrintOptions ASTOptions;
  } PrintState;
  unsigned LastBufferID;

  // Printers for the underlying stream.
#define SIMPLE_PRINTER(TYPE) \
  SILPrinter &operator<<(TYPE value) { \
    PrintState.OS << value;            \
    return *this;                      \
  }
  SIMPLE_PRINTER(char)
  SIMPLE_PRINTER(unsigned)
  SIMPLE_PRINTER(uint64_t)
  SIMPLE_PRINTER(StringRef)
  SIMPLE_PRINTER(Identifier)
  SIMPLE_PRINTER(ID)
  SIMPLE_PRINTER(QuotedString)
  SIMPLE_PRINTER(SILDeclRef)
  SIMPLE_PRINTER(APInt)
  SIMPLE_PRINTER(ValueOwnershipKind)
#undef SIMPLE_PRINTER

  SILPrinter &operator<<(SILValuePrinterInfo i) {
    SILColor C(PrintState.OS, SC_Type);
    *this << i.ValueID;
    if (!i.Type)
      return *this;
    *this << " : ";
    if (i.OwnershipKind) {
      *this << "@" << i.OwnershipKind.getValue() << " ";
    }
    return *this << i.Type;
  }

  SILPrinter &operator<<(Type t) {
    // Print the type using our print options.
    t.print(PrintState.OS, PrintState.ASTOptions);
    return *this;
  }
  
  SILPrinter &operator<<(SILType t) {
    printSILTypeColorAndSigil(PrintState.OS, t);
    t.getSwiftRValueType().print(PrintState.OS, PrintState.ASTOptions);
    return *this;
  }
  
public:
  SILPrinter(
      SILPrintContext &PrintCtx,
      llvm::DenseMap<CanType, Identifier> *AlternativeTypeNames = nullptr)
      : Ctx(PrintCtx),
        PrintState{{PrintCtx.OS()}, PrintOptions::printSIL()},
        LastBufferID(0) {
    PrintState.ASTOptions.AlternativeTypeNames = AlternativeTypeNames;
    PrintState.ASTOptions.PrintForSIL = true;
  }

  SILValuePrinterInfo getIDAndType(SILValue V) {
    return {Ctx.getID(V), V->getType()};
  }
  SILValuePrinterInfo getIDAndTypeAndOwnership(SILValue V) {
    return {Ctx.getID(V), V->getType(), V.getOwnershipKind()};
  }

  //===--------------------------------------------------------------------===//
  // Big entrypoints.
  void print(const SILFunction *F) {
    // If we are asked to emit sorted SIL, print out our BBs in RPOT order.
    if (Ctx.sortSIL()) {
      std::vector<SILBasicBlock *> RPOT;
      auto *UnsafeF = const_cast<SILFunction *>(F);
      std::copy(po_begin(UnsafeF), po_end(UnsafeF),
                std::back_inserter(RPOT));
      std::reverse(RPOT.begin(), RPOT.end());
      Ctx.initBlockIDs(RPOT);
      interleave(RPOT,
                 [&](SILBasicBlock *B) { print(B); },
                 [&] { *this << '\n'; });
      return;
    }

    interleave(*F,
               [&](const SILBasicBlock &B) { print(&B); },
               [&] { *this << '\n'; });
  }

  void printBlockArgumentUses(const SILBasicBlock *BB) {
    if (BB->args_empty())
      return;

    for (SILValue V : BB->getArguments()) {
      if (V->use_empty())
        continue;
      *this << "// " << Ctx.getID(V);
      PrintState.OS.PadToColumn(50);
      *this << "// user";
      if (std::next(V->use_begin()) != V->use_end())
        *this << 's';
      *this << ": ";

      llvm::SmallVector<ID, 32> UserIDs;
      for (auto *Op : V->getUses())
        UserIDs.push_back(Ctx.getID(Op->getUser()));

      // Display the user ids sorted to give a stable use order in the
      // printer's output if we are asked to do so. This makes diffing large
      // sections of SIL significantly easier at the expense of not showing
      // the _TRUE_ order of the users in the use list.
      if (Ctx.sortSIL()) {
        std::sort(UserIDs.begin(), UserIDs.end());
      }

      interleave(UserIDs.begin(), UserIDs.end(),
                 [&] (ID id) { *this << id; },
                 [&] { *this << ", "; });
      *this << '\n';
    }
  }

  void printBlockArguments(const SILBasicBlock *BB) {
    if (BB->args_empty())
      return;
    *this << '(';
    ArrayRef<SILArgument *> Args = BB->getArguments();

    // If SIL ownership is enabled and the given function has not had ownership
    // stripped out, print out ownership of SILArguments.
    if (BB->getModule().getOptions().EnableSILOwnership &&
        BB->getParent()->hasQualifiedOwnership()) {
      *this << getIDAndTypeAndOwnership(Args[0]);
      for (SILArgument *Arg : Args.drop_front()) {
        *this << ", " << getIDAndTypeAndOwnership(Arg);
      }
      *this << ')';
      return;
    }

    // Otherwise, fall back to the old behavior
    *this << getIDAndType(Args[0]);
    for (SILArgument *Arg : Args.drop_front()) {
      *this << ", " << getIDAndType(Arg);
    }
    *this << ')';
  }

  void print(const SILBasicBlock *BB) {
    // Output uses for BB arguments. These are put into place as comments before
    // the block header.
    printBlockArgumentUses(BB);

    // Then print the name of our block, the arguments, and the block colon.
    *this << Ctx.getID(BB);
    printBlockArguments(BB);
    *this << ":";

    if (!BB->pred_empty()) {
      PrintState.OS.PadToColumn(50);
      
      *this << "// Preds:";

      llvm::SmallVector<ID, 32> PredIDs;
      for (auto *BBI : BB->getPredecessorBlocks())
        PredIDs.push_back(Ctx.getID(BBI));

      // Display the pred ids sorted to give a stable use order in the printer's
      // output if we are asked to do so. This makes diffing large sections of
      // SIL significantly easier at the expense of not showing the _TRUE_ order
      // of the users in the use list.
      if (Ctx.sortSIL()) {
        std::sort(PredIDs.begin(), PredIDs.end());
      }

      for (auto Id : PredIDs)
        *this << ' ' << Id;
    }
    *this << '\n';

    for (const SILInstruction &I : *BB) {
      Ctx.printInstructionCallBack(&I);
      if (SILPrintGenericSpecializationInfo) {
        if (auto AI = ApplySite::isa(const_cast<SILInstruction *>(&I)))
          if (AI.getSpecializationInfo() && AI.getCalleeFunction())
            printGenericSpecializationInfo(
                PrintState.OS, "call-site", AI.getCalleeFunction()->getName(),
                AI.getSpecializationInfo(), AI.getSubstitutions());
      }
      print(&I);
    }
  }

  //===--------------------------------------------------------------------===//
  // SILInstruction Printing Logic

  bool printTypeDependentOperands(const SILInstruction *I) {
    ArrayRef<Operand> TypeDepOps = I->getTypeDependentOperands();
    if (TypeDepOps.empty())
      return false;

    PrintState.OS.PadToColumn(50);
    *this << "// type-defs: ";
    interleave(TypeDepOps,
               [&](const Operand &op) { *this << Ctx.getID(op.get()); },
               [&] { *this << ", "; });
    return true;
  }

  /// Print out the users of the SILValue \p V. Return true if we printed out
  /// either an id or a use list. Return false otherwise.
  bool printUsersOfSILNode(const SILNode *node, bool printedSlashes) {
    llvm::SmallVector<SILValue, 8> values;
    if (auto *value = dyn_cast<ValueBase>(node)) {
      values.push_back(value);
    } else if (auto *inst = dyn_cast<SILInstruction>(node)) {
      assert(!isa<SingleValueInstruction>(inst) && "SingleValueInstruction was "
                                                   "handled by the previous "
                                                   "value base check.");
      copy(inst->getResults(), std::back_inserter(values));
    }

    // If the set of values is empty, we need to print the ID of
    // the instruction.  Otherwise, if none of the values has a use,
    // we don't need to do anything.
    if (!values.empty()) {
      bool hasUse = false;
      for (auto value : values) {
        if (!value->use_empty()) hasUse = true;
      }
      if (!hasUse)
        return printedSlashes;
    }

    if (printedSlashes) {
      *this << "; ";
    } else {
      PrintState.OS.PadToColumn(50);
      *this << "// ";
    }
    if (values.empty()) {
      *this << "id: " << Ctx.getID(node);
      return true;
    }

    llvm::SmallVector<ID, 32> UserIDs;
    for (auto value : values)
      for (auto *Op : value->getUses())
        UserIDs.push_back(Ctx.getID(Op->getUser()));

    *this << "user";
    if (UserIDs.size() != 1)
      *this << 's';
    *this << ": ";

    // If we are asked to, display the user ids sorted to give a stable use
    // order in the printer's output. This makes diffing large sections of SIL
    // significantly easier.
    if (Ctx.sortSIL()) {
      std::sort(UserIDs.begin(), UserIDs.end());
    }

    interleave(UserIDs.begin(), UserIDs.end(), [&](ID id) { *this << id; },
               [&] { *this << ", "; });
    return true;
  }

  void printDebugLocRef(SILLocation Loc, const SourceManager &SM,
                        bool PrintComma = true) {
    auto DL = Loc.decodeDebugLoc(SM);
    if (!DL.Filename.empty()) {
      if (PrintComma)
        *this << ", ";
      *this << "loc " << QuotedString(DL.Filename) << ':' << DL.Line << ':'
            << DL.Column;
    }
  }

  void printDebugScope(const SILDebugScope *DS, const SourceManager &SM) {
    if (!DS)
      return;

    if (!Ctx.hasScopeID(DS)) {
      printDebugScope(DS->Parent.dyn_cast<const SILDebugScope *>(), SM);
      printDebugScope(DS->InlinedCallSite, SM);
      unsigned ID = Ctx.assignScopeID(DS);
      *this << "sil_scope " << ID << " { ";
      printDebugLocRef(DS->Loc, SM, false);
      *this << " parent ";
      if (auto *F = DS->Parent.dyn_cast<SILFunction *>())
        *this << "@" << F->getName() << " : $" << F->getLoweredFunctionType();
      else {
        auto *PS = DS->Parent.get<const SILDebugScope *>();
        *this << Ctx.getScopeID(PS);
      }
      if (auto *CS = DS->InlinedCallSite)
        *this << " inlined_at " << Ctx.getScopeID(CS);
      *this << " }\n";
    }
  }

  void printDebugScopeRef(const SILDebugScope *DS, const SourceManager &SM,
                          bool PrintComma = true) {
    if (DS) {
      if (PrintComma)
        *this << ", ";
      *this << "scope " << Ctx.getScopeID(DS);
    }
  }

  void printSILLocation(SILLocation L, SILModule &M, const SILDebugScope *DS,
                        bool printedSlashes) {
    if (!L.isNull()) {
      if (!printedSlashes) {
        PrintState.OS.PadToColumn(50);
        *this << "//";
      }
      *this << " ";

      // To minimize output, only print the line and column number for
      // everything but the first instruction.
      L.getSourceLoc().printLineAndColumn(PrintState.OS,
                                          M.getASTContext().SourceMgr);

      // Print the type of location.
      switch (L.getKind()) {
      case SILLocation::NoneKind:
        assert(L.isAutoGenerated() && "This kind shouldn't be printed.");
        break;
      case SILLocation::RegularKind:
        break;
      case SILLocation::ReturnKind:
        *this << ":return";
        break;
      case SILLocation::ImplicitReturnKind:
        *this << ":imp_return";
        break;
      case SILLocation::InlinedKind:
        *this << ":inlined";
        break;
      case SILLocation::MandatoryInlinedKind:
        *this << ":minlined";
        break;
      case SILLocation::CleanupKind:
        *this << ":cleanup";
        break;
      case SILLocation::ArtificialUnreachableKind:
        *this << ":art_unreach";
        break;
      }
      if (L.isSILFile())
        *this << ":sil";
      if (L.isAutoGenerated())
        *this << ":auto_gen";
      if (L.isInPrologue())
        *this << ":in_prologue";
    }
    if (L.isNull()) {
      if (!printedSlashes) {
        PrintState.OS.PadToColumn(50);
        *this << "//";
      }
      if (L.isInTopLevel())
        *this << " top_level";
      else if (L.isAutoGenerated())
        *this << " auto_gen";
      else
        *this << " no_loc";
      if (L.isInPrologue())
        *this << ":in_prologue";
    }

    if (!DS)
      return;

    // Print inlined-at location, if any.
    const SILDebugScope *CS = DS;
    while ((CS = CS->InlinedCallSite)) {
      *this << ": ";
      if (auto *InlinedF = CS->getInlinedFunction())
        *this << demangleSymbol(InlinedF->getName());
      else
        *this << '?';
      *this << " perf_inlined_at ";
      auto CallSite = CS->Loc;
      if (!CallSite.isNull() && CallSite.isASTNode())
        CallSite.getSourceLoc().print(
            PrintState.OS, M.getASTContext().SourceMgr, LastBufferID);
      else
        *this << "?";
    }
  }

  void printInstOpCode(const SILInstruction *I) {
    *this << getSILInstructionName(I->getKind()) << " ";
  }

  void print(const SILInstruction *I) {
    if (auto *FRI = dyn_cast<FunctionRefInst>(I))
      *this << "  // function_ref "
            << demangleSymbol(FRI->getReferencedFunction()->getName())
            << "\n";

    *this << "  ";

    // Print results.
    auto results = I->getResults();
    if (results.size() == 1 &&
        I->isStaticInitializerInst() &&
        I == &I->getParent()->back()) {
      *this << "%initval = ";
    } else if (results.size() == 1) {
      ID Name = Ctx.getID(results[0]);
      *this << Name << " = ";
    } else if (results.size() > 1) {
      *this << '(';
      bool first = true;
      for (auto result : results) {
        if (first) {
          first = false;
        } else {
          *this << ", ";
        }
        ID Name = Ctx.getID(result);
        *this << Name;
      }
      *this << ") = ";
    }

    // Print the opcode.
    printInstOpCode(I);

    // Use the visitor to print the rest of the instruction.
    visit(const_cast<SILInstruction*>(I));

    // Maybe print debugging information.
    bool printedSlashes = false;
    if (Ctx.printDebugInfo() && !I->isStaticInitializerInst()) {
      auto &SM = I->getModule().getASTContext().SourceMgr;
      printDebugLocRef(I->getLoc(), SM);
      printDebugScopeRef(I->getDebugScope(), SM);
    }
    printedSlashes = printTypeDependentOperands(I);

    // Print users, or id for valueless instructions.
    printedSlashes = printUsersOfSILNode(I, printedSlashes);

    // Print SIL location.
    if (Ctx.printVerbose()) {
      printSILLocation(I->getLoc(), I->getModule(), I->getDebugScope(),
                       printedSlashes);
    }
    
    *this << '\n';
  }

  void print(const SILNode *node) {
    switch (node->getKind()) {
#define INST(ID, PARENT) \
    case SILNodeKind::ID:
#include "swift/SIL/SILNodes.def"
      print(cast<SILInstruction>(node));
      return;

#define ARGUMENT(ID, PARENT) \
    case SILNodeKind::ID:
#include "swift/SIL/SILNodes.def"
      printSILArgument(cast<SILArgument>(node));
      return;

    case SILNodeKind::SILUndef:
      printSILUndef(cast<SILUndef>(node));
      return;

#define MULTIPLE_VALUE_INST_RESULT(ID, PARENT) \
    case SILNodeKind::ID:
#include "swift/SIL/SILNodes.def"
      printSILMultipleValueInstructionResult(
          cast<MultipleValueInstructionResult>(node));
      return;
    }
    llvm_unreachable("bad kind");
  }

  void printSILArgument(const SILArgument *arg) {
    // This should really only happen during debugging.
    *this << Ctx.getID(arg) << " = argument of "
          << Ctx.getID(arg->getParent()) << " : " << arg->getType();

    // Print users.
    (void) printUsersOfSILNode(arg, false);

    *this << '\n';
  }

  void printSILUndef(const SILUndef *undef) {
    // This should really only happen during debugging.
    *this << "undef<" << undef->getType() << ">\n";
  }

  void printSILMultipleValueInstructionResult(
      const MultipleValueInstructionResult *result) {
    // This should really only happen during debugging.
    if (result->getParent()->getNumResults() == 1) {
      *this << "**" << Ctx.getID(result) << "** = ";
    } else {
      *this << '(';
      interleave(result->getParent()->getResults(),
                 [&](SILValue value) {
                   if (value == SILValue(result)) {
                     *this << "**" << Ctx.getID(result) << "**";
                     return;
                   }
                   *this << Ctx.getID(value);
                 },
                 [&] { *this << ", "; });
      *this << ')';
    }

    *this << " = ";
    printInstOpCode(result->getParent());
    auto *nonConstParent =
        const_cast<MultipleValueInstruction *>(result->getParent());
    visit(static_cast<SILInstruction *>(nonConstParent));

    // Print users.
    (void)printUsersOfSILNode(result, false);

    *this << '\n';
  }

  void printInContext(const SILNode *node) {
    auto sortByID = [&](const SILNode *a, const SILNode *b) {
      return Ctx.getID(a).Number < Ctx.getID(b).Number;
    };

    if (auto *I = dyn_cast<SILInstruction>(node)) {
      auto operands = map<SmallVector<SILValue,4>>(I->getAllOperands(),
                                                   [](Operand const &o) {
                                                     return o.get();
                                                   });
      std::sort(operands.begin(), operands.end(), sortByID);
      for (auto &operand : operands) {
        *this << "   ";
        print(operand);
      }
    }
    
    *this << "-> ";
    print(node);

    if (auto V = dyn_cast<ValueBase>(node)) {    
      auto users = map<SmallVector<const SILInstruction*,4>>(V->getUses(),
                                                       [](Operand *o) {
                                                         return o->getUser();
                                                       });
      std::sort(users.begin(), users.end(), sortByID);
      for (auto &user : users) {
        *this << "   ";
        print(user);
      }
    }
  }

  void printDebugVar(SILDebugVariable Var) {
    if (Var.Name.empty())
      return;
    if (Var.Constant)
      *this << ", let";
    else
      *this << ", var";
    *this << ", name \"" << Var.Name << '"';
    if (Var.ArgNo)
      *this << ", argno " << Var.ArgNo;
  }

  void visitAllocStackInst(AllocStackInst *AVI) {
    *this << AVI->getElementType();
    printDebugVar(AVI->getVarInfo());
  }

  void printAllocRefInstBase(AllocRefInstBase *ARI) {
    if (ARI->isObjC())
      *this << "[objc] ";
    if (ARI->canAllocOnStack())
      *this << "[stack] ";
    auto Types = ARI->getTailAllocatedTypes();
    auto Counts = ARI->getTailAllocatedCounts();
    for (unsigned Idx = 0, NumTypes = Types.size(); Idx < NumTypes; ++Idx) {
      *this << "[tail_elems " << Types[Idx] << " * "
            << getIDAndType(Counts[Idx].get()) << "] ";
    }
  }

  void visitAllocRefInst(AllocRefInst *ARI) {
    printAllocRefInstBase(ARI);
    *this << ARI->getType();
  }

  void visitAllocRefDynamicInst(AllocRefDynamicInst *ARDI) {
    printAllocRefInstBase(ARDI);
    *this << getIDAndType(ARDI->getMetatypeOperand());
    *this << ", " << ARDI->getType();
  }

  void visitAllocValueBufferInst(AllocValueBufferInst *AVBI) {
    *this << AVBI->getValueType() << " in " << getIDAndType(AVBI->getOperand());
  }

  void visitAllocBoxInst(AllocBoxInst *ABI) {
    *this << ABI->getType();
    printDebugVar(ABI->getVarInfo());
  }

  void printSubstitutions(SubstitutionList Subs) {
    if (Subs.empty())
      return;
    
    *this << '<';
    interleave(Subs,
               [&](const Substitution &s) { *this << s.getReplacement(); },
               [&] { *this << ", "; });
    *this << '>';
  }

  template <class Inst>
  void visitApplyInstBase(Inst *AI) {
    *this << Ctx.getID(AI->getCallee());
    printSubstitutions(AI->getSubstitutions());
    *this << '(';
    interleave(AI->getArguments(),
               [&](const SILValue &arg) { *this << Ctx.getID(arg); },
               [&] { *this << ", "; });
    *this << ") : " << AI->getCallee()->getType();
  }

  void visitApplyInst(ApplyInst *AI) {
    if (AI->isNonThrowing())
      *this << "[nothrow] ";
    visitApplyInstBase(AI);
  }

  void visitBeginApplyInst(BeginApplyInst *AI) {
    if (AI->isNonThrowing())
      *this << "[nothrow] ";
    visitApplyInstBase(AI);
  }

  void visitTryApplyInst(TryApplyInst *AI) {
    visitApplyInstBase(AI);
    *this << ", normal " << Ctx.getID(AI->getNormalBB());
    *this << ", error " << Ctx.getID(AI->getErrorBB());
  }

  void visitPartialApplyInst(PartialApplyInst *CI) {
    switch (CI->getFunctionType()->getCalleeConvention()) {
    case ParameterConvention::Direct_Owned:
      // Default; do nothing.
      break;
    case ParameterConvention::Direct_Guaranteed:
      *this << "[callee_guaranteed] ";
      break;
    
    // Should not apply to callees.
    case ParameterConvention::Direct_Unowned:
    case ParameterConvention::Indirect_In:
    case ParameterConvention::Indirect_In_Constant:
    case ParameterConvention::Indirect_Inout:
    case ParameterConvention::Indirect_In_Guaranteed:
    case ParameterConvention::Indirect_InoutAliasable:
      llvm_unreachable("unexpected callee convention!");
    }
    visitApplyInstBase(CI);
  }

  void visitAbortApplyInst(AbortApplyInst *AI) {
    *this << Ctx.getID(AI->getOperand());
  }

  void visitEndApplyInst(EndApplyInst *AI) {
    *this << Ctx.getID(AI->getOperand());
  }

  void visitFunctionRefInst(FunctionRefInst *FRI) {
    FRI->getReferencedFunction()->printName(PrintState.OS);
    *this << " : " << FRI->getType();
  }
  
  void visitBuiltinInst(BuiltinInst *BI) {
    *this << QuotedString(BI->getName().str());
    printSubstitutions(BI->getSubstitutions());
    *this << "(";
    
    interleave(BI->getArguments(), [&](SILValue v) {
      *this << getIDAndType(v);
    }, [&]{
      *this << ", ";
    });
    
    *this << ") : ";
    *this << BI->getType();
  }
  
  void visitAllocGlobalInst(AllocGlobalInst *AGI) {
    if (AGI->getReferencedGlobal()) {
      AGI->getReferencedGlobal()->printName(PrintState.OS);
    } else {
      *this << "<<placeholder>>";
    }
  }
  
  void visitGlobalAddrInst(GlobalAddrInst *GAI) {
    if (GAI->getReferencedGlobal()) {
      GAI->getReferencedGlobal()->printName(PrintState.OS);
    } else {
      *this << "<<placeholder>>";
    }
    *this << " : " << GAI->getType();
  }

  void visitGlobalValueInst(GlobalValueInst *GVI) {
    GVI->getReferencedGlobal()->printName(PrintState.OS);
    *this << " : " << GVI->getType();
  }

  void visitIntegerLiteralInst(IntegerLiteralInst *ILI) {
    const auto &lit = ILI->getValue();
    *this << ILI->getType() << ", " << lit;
  }
  void visitFloatLiteralInst(FloatLiteralInst *FLI) {
    *this << FLI->getType() << ", 0x";
    APInt bits = FLI->getBits();
    *this << bits.toString(16, /*Signed*/ false);
    llvm::SmallString<12> decimal;
    FLI->getValue().toString(decimal);
    *this << " // " << decimal;
  }
  static StringRef getStringEncodingName(StringLiteralInst::Encoding kind) {
    switch (kind) {
    case StringLiteralInst::Encoding::UTF8: return "utf8 ";
    case StringLiteralInst::Encoding::UTF16: return "utf16 ";
    case StringLiteralInst::Encoding::ObjCSelector: return "objc_selector ";
    }
    llvm_unreachable("bad string literal encoding");
  }

  void visitStringLiteralInst(StringLiteralInst *SLI) {
    *this << getStringEncodingName(SLI->getEncoding())
          << QuotedString(SLI->getValue());
  }

  static StringRef
  getStringEncodingName(ConstStringLiteralInst::Encoding kind) {
    switch (kind) {
    case ConstStringLiteralInst::Encoding::UTF8:
      return "utf8 ";
    case ConstStringLiteralInst::Encoding::UTF16:
      return "utf16 ";
    }
    llvm_unreachable("bad string literal encoding");
  }

  void visitConstStringLiteralInst(ConstStringLiteralInst *SLI) {
    *this << getStringEncodingName(SLI->getEncoding())
          << QuotedString(SLI->getValue());
  }

  void printLoadOwnershipQualifier(LoadOwnershipQualifier Qualifier) {
    switch (Qualifier) {
    case LoadOwnershipQualifier::Unqualified:
      return;
    case LoadOwnershipQualifier::Take:
      *this << "[take] ";
      return;
    case LoadOwnershipQualifier::Copy:
      *this << "[copy] ";
      return;
    case LoadOwnershipQualifier::Trivial:
      *this << "[trivial] ";
      return;
    }
  }

  void visitLoadInst(LoadInst *LI) {
    printLoadOwnershipQualifier(LI->getOwnershipQualifier());
    *this << getIDAndType(LI->getOperand());
  }

  void visitLoadBorrowInst(LoadBorrowInst *LBI) {
    *this << getIDAndType(LBI->getOperand());
  }

  void visitBeginBorrowInst(BeginBorrowInst *LBI) {
    *this << getIDAndType(LBI->getOperand());
  }

  void printStoreOwnershipQualifier(StoreOwnershipQualifier Qualifier) {
    switch (Qualifier) {
    case StoreOwnershipQualifier::Unqualified:
      return;
    case StoreOwnershipQualifier::Init:
      *this << "[init] ";
      return;
    case StoreOwnershipQualifier::Assign:
      *this << "[assign] ";
      return;
    case StoreOwnershipQualifier::Trivial:
      *this << "[trivial] ";
      return;
    }
  }

  void visitStoreInst(StoreInst *SI) {
    *this << Ctx.getID(SI->getSrc()) << " to ";
    printStoreOwnershipQualifier(SI->getOwnershipQualifier());
    *this << getIDAndType(SI->getDest());
  }

  void visitStoreBorrowInst(StoreBorrowInst *SI) {
    *this << Ctx.getID(SI->getSrc()) << " to ";
    *this << getIDAndType(SI->getDest());
  }

  void visitEndBorrowInst(EndBorrowInst *EBI) {
    *this << Ctx.getID(EBI->getBorrowedValue()) << " from "
          << Ctx.getID(EBI->getOriginalValue()) << " : "
          << EBI->getBorrowedValue()->getType() << ", "
          << EBI->getOriginalValue()->getType();
  }

  void visitEndBorrowArgumentInst(EndBorrowArgumentInst *EBAI) {
    *this << getIDAndType(EBAI->getOperand());
  }

  void visitAssignInst(AssignInst *AI) {
    *this << Ctx.getID(AI->getSrc()) << " to " << getIDAndType(AI->getDest());
  }

  void visitMarkUninitializedInst(MarkUninitializedInst *MU) {
    switch (MU->getKind()) {
    case MarkUninitializedInst::Var: *this << "[var] "; break;
    case MarkUninitializedInst::RootSelf:  *this << "[rootself] "; break;
    case MarkUninitializedInst::CrossModuleRootSelf:
      *this << "[crossmodulerootself] ";
      break;
    case MarkUninitializedInst::DerivedSelf:  *this << "[derivedself] "; break;
    case MarkUninitializedInst::DerivedSelfOnly:
      *this << "[derivedselfonly] ";
      break;
    case MarkUninitializedInst::DelegatingSelf: *this << "[delegatingself] ";break;
    }
    
    *this << getIDAndType(MU->getOperand());
  }
  void visitMarkUninitializedBehaviorInst(MarkUninitializedBehaviorInst *MU) {
    *this << Ctx.getID(MU->getInitStorageFunc());
    printSubstitutions(MU->getInitStorageSubstitutions());
    *this << '(' << Ctx.getID(MU->getStorage())
          << ") : " << MU->getInitStorageFunc()->getType() << ", "
          << Ctx.getID(MU->getSetterFunc());
    printSubstitutions(MU->getSetterSubstitutions());
    *this << '(' << Ctx.getID(MU->getSelf())
          << ") : " << MU->getSetterFunc()->getType();
  }
  void visitMarkFunctionEscapeInst(MarkFunctionEscapeInst *MFE) {
    interleave(MFE->getElements(),
               [&](SILValue Var) { *this << getIDAndType(Var); },
               [&] { *this << ", "; });
  }

  void visitDebugValueInst(DebugValueInst *DVI) {
    *this << getIDAndType(DVI->getOperand());
    printDebugVar(DVI->getVarInfo());
  }

  void visitDebugValueAddrInst(DebugValueAddrInst *DVAI) {
    *this << getIDAndType(DVAI->getOperand());
    printDebugVar(DVAI->getVarInfo());
  }

  void visitLoadUnownedInst(LoadUnownedInst *LI) {
    if (LI->isTake())
      *this << "[take] ";
    *this << getIDAndType(LI->getOperand());
  }
  void visitStoreUnownedInst(StoreUnownedInst *SI) {
    *this << Ctx.getID(SI->getSrc()) << " to ";
    if (SI->isInitializationOfDest())
      *this << "[initialization] ";
    *this << getIDAndType(SI->getDest());
  }

  void visitLoadWeakInst(LoadWeakInst *LI) {
    if (LI->isTake())
      *this << "[take] ";
    *this << getIDAndType(LI->getOperand());
  }
  void visitStoreWeakInst(StoreWeakInst *SI) {
    *this << Ctx.getID(SI->getSrc()) << " to ";
    if (SI->isInitializationOfDest())
      *this << "[initialization] ";
    *this << getIDAndType(SI->getDest());
  }

  void visitCopyAddrInst(CopyAddrInst *CI) {
    if (CI->isTakeOfSrc())
      *this << "[take] ";
    *this << Ctx.getID(CI->getSrc()) << " to ";
    if (CI->isInitializationOfDest())
      *this << "[initialization] ";
    *this << getIDAndType(CI->getDest());
  }

  void visitBindMemoryInst(BindMemoryInst *BI) {
    *this << getIDAndType(BI->getBase()) << ", ";
    *this << getIDAndType(BI->getIndex()) << " to ";
    *this << BI->getBoundType();
  }
  
  void visitUnconditionalCheckedCastInst(UnconditionalCheckedCastInst *CI) {
    *this << getIDAndType(CI->getOperand()) << " to " << CI->getType();
  }
  
  void visitCheckedCastBranchInst(CheckedCastBranchInst *CI) {
    if (CI->isExact())
      *this << "[exact] ";
    *this << getIDAndType(CI->getOperand()) << " to " << CI->getCastType()
          << ", " << Ctx.getID(CI->getSuccessBB()) << ", "
          << Ctx.getID(CI->getFailureBB());
    if (CI->getTrueBBCount())
      *this << " !true_count(" << CI->getTrueBBCount().getValue() << ")";
    if (CI->getFalseBBCount())
      *this << " !false_count(" << CI->getFalseBBCount().getValue() << ")";
  }

  void visitCheckedCastValueBranchInst(CheckedCastValueBranchInst *CI) {
    *this << getIDAndType(CI->getOperand()) << " to " << CI->getCastType()
          << ", " << Ctx.getID(CI->getSuccessBB()) << ", "
          << Ctx.getID(CI->getFailureBB());
  }

  void visitUnconditionalCheckedCastAddrInst(UnconditionalCheckedCastAddrInst *CI) {
    *this << CI->getSourceType() << " in " << getIDAndType(CI->getSrc())
          << " to " << CI->getTargetType() << " in "
          << getIDAndType(CI->getDest());
  }

  void visitUnconditionalCheckedCastValueInst(
      UnconditionalCheckedCastValueInst *CI) {
    *this << getIDAndType(CI->getOperand()) << " to " << CI->getType();
  }

  void visitCheckedCastAddrBranchInst(CheckedCastAddrBranchInst *CI) {
    *this << getCastConsumptionKindName(CI->getConsumptionKind()) << ' '
          << CI->getSourceType() << " in " << getIDAndType(CI->getSrc())
          << " to " << CI->getTargetType() << " in "
          << getIDAndType(CI->getDest()) << ", "
          << Ctx.getID(CI->getSuccessBB()) << ", "
          << Ctx.getID(CI->getFailureBB());
    if (CI->getTrueBBCount())
      *this << " !true_count(" << CI->getTrueBBCount().getValue() << ")";
    if (CI->getFalseBBCount())
      *this << " !false_count(" << CI->getFalseBBCount().getValue() << ")";
  }

  void printUncheckedConversionInst(ConversionInst *CI, SILValue operand) {
    *this << getIDAndType(operand) << " to " << CI->getType();
  }

  void visitUncheckedOwnershipConversionInst(
      UncheckedOwnershipConversionInst *UOCI) {
    *this << getIDAndType(UOCI->getOperand()) << ", "
          << "@" << UOCI->getOperand().getOwnershipKind() << " to "
          << "@" << UOCI->getConversionOwnershipKind();
  }

  void visitConvertFunctionInst(ConvertFunctionInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitThinFunctionToPointerInst(ThinFunctionToPointerInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitPointerToThinFunctionInst(PointerToThinFunctionInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitUpcastInst(UpcastInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitAddressToPointerInst(AddressToPointerInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitPointerToAddressInst(PointerToAddressInst *CI) {
    *this << getIDAndType(CI->getOperand()) << " to ";
    if (CI->isStrict())
      *this << "[strict] ";
    if (CI->isInvariant())
      *this << "[invariant] ";
    *this << CI->getType();
  }
  void visitUncheckedRefCastInst(UncheckedRefCastInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitUncheckedRefCastAddrInst(UncheckedRefCastAddrInst *CI) {
    *this << ' ' << CI->getSourceType() << " in " << getIDAndType(CI->getSrc())
          << " to " << CI->getTargetType() << " in "
          << getIDAndType(CI->getDest());
  }
  void visitUncheckedAddrCastInst(UncheckedAddrCastInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitUncheckedTrivialBitCastInst(UncheckedTrivialBitCastInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitUncheckedBitwiseCastInst(UncheckedBitwiseCastInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitRefToRawPointerInst(RefToRawPointerInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitRawPointerToRefInst(RawPointerToRefInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitRefToUnownedInst(RefToUnownedInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitUnownedToRefInst(UnownedToRefInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitRefToUnmanagedInst(RefToUnmanagedInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitUnmanagedToRefInst(UnmanagedToRefInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitThinToThickFunctionInst(ThinToThickFunctionInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitThickToObjCMetatypeInst(ThickToObjCMetatypeInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitObjCToThickMetatypeInst(ObjCToThickMetatypeInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitObjCMetatypeToObjectInst(ObjCMetatypeToObjectInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitObjCExistentialMetatypeToObjectInst(
                                      ObjCExistentialMetatypeToObjectInst *CI) {
    printUncheckedConversionInst(CI, CI->getOperand());
  }
  void visitObjCProtocolInst(ObjCProtocolInst *CI) {
    *this << "#" << CI->getProtocol()->getName() << " : " << CI->getType();
  }
  
  void visitRefToBridgeObjectInst(RefToBridgeObjectInst *I) {
    *this << getIDAndType(I->getConverted()) << ", "
          << getIDAndType(I->getBitsOperand());
  }
  
  void visitBridgeObjectToRefInst(BridgeObjectToRefInst *I) {
    printUncheckedConversionInst(I, I->getOperand());
  }
  void visitBridgeObjectToWordInst(BridgeObjectToWordInst *I) {
    printUncheckedConversionInst(I, I->getOperand());
  }

  void visitCopyValueInst(CopyValueInst *I) {
    *this << getIDAndType(I->getOperand());
  }

  void visitCopyUnownedValueInst(CopyUnownedValueInst *I) {
    *this << getIDAndType(I->getOperand());
  }

  void visitDestroyValueInst(DestroyValueInst *I) {
    *this << getIDAndType(I->getOperand());
  }

  void visitStructInst(StructInst *SI) {
    *this << SI->getType() << " (";
    interleave(SI->getElements(),
               [&](const SILValue &V) { *this << getIDAndType(V); },
               [&] { *this << ", "; });
    *this << ')';
  }

  void visitObjectInst(ObjectInst *OI) {
    *this << OI->getType() << " (";
    interleave(OI->getBaseElements(),
               [&](const SILValue &V) { *this << getIDAndType(V); },
               [&] { *this << ", "; });
    if (OI->getTailElements().size() > 0) {
      *this << ", [tail_elems] ";
      interleave(OI->getTailElements(),
                 [&](const SILValue &V) { *this << getIDAndType(V); },
                 [&] { *this << ", "; });
    }
    *this << ')';
  }

  void visitTupleInst(TupleInst *TI) {
    
    // Check to see if the type of the tuple can be inferred accurately from the
    // elements.
    bool SimpleType = true;
    for (auto &Elt : TI->getType().castTo<TupleType>()->getElements()) {
      if (Elt.hasName() || Elt.isVararg()) {
        SimpleType = false;
        break;
      }
    }
    
    // If the type is simple, just print the tuple elements.
    if (SimpleType) {
      *this << '(';
      interleave(TI->getElements(),
                 [&](const SILValue &V){ *this << getIDAndType(V); },
                 [&] { *this << ", "; });
      *this << ')';
    } else {
      // Otherwise, print the type, then each value.
      *this << TI->getType() << " (";
      interleave(TI->getElements(),
                 [&](const SILValue &V) { *this << Ctx.getID(V); },
                 [&] { *this << ", "; });
      *this << ')';
    }
  }
  
  void visitEnumInst(EnumInst *UI) {
    *this << UI->getType() << ", "
          << SILDeclRef(UI->getElement(), SILDeclRef::Kind::EnumElement);
    if (UI->hasOperand()) {
      *this << ", " << getIDAndType(UI->getOperand());
    }
  }

  void visitInitEnumDataAddrInst(InitEnumDataAddrInst *UDAI) {
    *this << getIDAndType(UDAI->getOperand()) << ", "
          << SILDeclRef(UDAI->getElement(), SILDeclRef::Kind::EnumElement);
  }
  
  void visitUncheckedEnumDataInst(UncheckedEnumDataInst *UDAI) {
    *this << getIDAndType(UDAI->getOperand()) << ", "
          << SILDeclRef(UDAI->getElement(), SILDeclRef::Kind::EnumElement);
  }
  
  void visitUncheckedTakeEnumDataAddrInst(UncheckedTakeEnumDataAddrInst *UDAI) {
    *this << getIDAndType(UDAI->getOperand()) << ", "
          << SILDeclRef(UDAI->getElement(), SILDeclRef::Kind::EnumElement);
  }
  
  void visitInjectEnumAddrInst(InjectEnumAddrInst *IUAI) {
    *this << getIDAndType(IUAI->getOperand()) << ", "
          << SILDeclRef(IUAI->getElement(), SILDeclRef::Kind::EnumElement);
  }
  
  void visitTupleExtractInst(TupleExtractInst *EI) {
    *this << getIDAndType(EI->getOperand()) << ", " << EI->getFieldNo();
  }

  void visitTupleElementAddrInst(TupleElementAddrInst *EI) {
    *this << getIDAndType(EI->getOperand()) << ", " << EI->getFieldNo();
  }
  void visitStructExtractInst(StructExtractInst *EI) {
    *this << getIDAndType(EI->getOperand()) << ", #";
    printFullContext(EI->getField()->getDeclContext(), PrintState.OS);
    *this << EI->getField()->getName().get();
  }
  void visitStructElementAddrInst(StructElementAddrInst *EI) {
    *this << getIDAndType(EI->getOperand()) << ", #";
    printFullContext(EI->getField()->getDeclContext(), PrintState.OS);
    *this << EI->getField()->getName().get();
  }
  void visitRefElementAddrInst(RefElementAddrInst *EI) {
    *this << getIDAndType(EI->getOperand()) << ", #";
    printFullContext(EI->getField()->getDeclContext(), PrintState.OS);
    *this << EI->getField()->getName().get();
  }

  void visitRefTailAddrInst(RefTailAddrInst *RTAI) {
    *this << getIDAndType(RTAI->getOperand()) << ", " << RTAI->getTailType();
  }

  void visitDestructureStructInst(DestructureStructInst *DSI) {
    *this << getIDAndType(DSI->getOperand());
  }

  void visitDestructureTupleInst(DestructureTupleInst *DTI) {
    *this << getIDAndType(DTI->getOperand());
  }

  void printMethodInst(MethodInst *I, SILValue Operand) {
    *this << getIDAndType(Operand) << ", " << I->getMember();
  }
  
  void visitClassMethodInst(ClassMethodInst *AMI) {
    printMethodInst(AMI, AMI->getOperand());
    *this << " : " << AMI->getMember().getDecl()->getInterfaceType();
    *this << ", ";
    *this << AMI->getType();
  }
  void visitSuperMethodInst(SuperMethodInst *AMI) {
    printMethodInst(AMI, AMI->getOperand());
    *this << " : " << AMI->getMember().getDecl()->getInterfaceType();
    *this << ", ";
    *this << AMI->getType();
  }
  void visitObjCMethodInst(ObjCMethodInst *AMI) {
    printMethodInst(AMI, AMI->getOperand());
    *this << " : " << AMI->getMember().getDecl()->getInterfaceType();
    *this << ", ";
    *this << AMI->getType();
  }
  void visitObjCSuperMethodInst(ObjCSuperMethodInst *AMI) {
    printMethodInst(AMI, AMI->getOperand());
    *this << " : " << AMI->getMember().getDecl()->getInterfaceType();
    *this << ", ";
    *this << AMI->getType();
  }
  void visitWitnessMethodInst(WitnessMethodInst *WMI) {
    PrintOptions QualifiedSILTypeOptions =
        PrintOptions::printQualifiedSILType();
    QualifiedSILTypeOptions.CurrentModule = WMI->getModule().getSwiftModule();
    *this << "$" << WMI->getLookupType() << ", " << WMI->getMember() << " : ";
    WMI->getMember().getDecl()->getInterfaceType().print(
        PrintState.OS, QualifiedSILTypeOptions);
    if (!WMI->getTypeDependentOperands().empty()) {
      *this << ", ";
      *this << getIDAndType(WMI->getTypeDependentOperands()[0].get());
    }
    *this << " : " << WMI->getType();
  }
  void visitOpenExistentialAddrInst(OpenExistentialAddrInst *OI) {
    if (OI->getAccessKind() == OpenedExistentialAccess::Immutable)
      *this << "immutable_access ";
    else
      *this << "mutable_access ";
    *this << getIDAndType(OI->getOperand()) << " to " << OI->getType();
  }
  void visitOpenExistentialRefInst(OpenExistentialRefInst *OI) {
    *this << getIDAndType(OI->getOperand()) << " to " << OI->getType();
  }
  void visitOpenExistentialMetatypeInst(OpenExistentialMetatypeInst *OI) {
    *this << getIDAndType(OI->getOperand()) << " to " << OI->getType();
  }
  void visitOpenExistentialBoxInst(OpenExistentialBoxInst *OI) {
    *this << getIDAndType(OI->getOperand()) << " to " << OI->getType();
  }
  void visitOpenExistentialBoxValueInst(OpenExistentialBoxValueInst *OI) {
    *this << getIDAndType(OI->getOperand()) << " to " << OI->getType();
  }
  void visitOpenExistentialValueInst(OpenExistentialValueInst *OI) {
    *this << getIDAndType(OI->getOperand()) << " to " << OI->getType();
  }
  void visitInitExistentialAddrInst(InitExistentialAddrInst *AEI) {
    *this << getIDAndType(AEI->getOperand()) << ", $"
          << AEI->getFormalConcreteType();
  }
  void visitInitExistentialValueInst(InitExistentialValueInst *AEI) {
    *this << getIDAndType(AEI->getOperand()) << ", $"
          << AEI->getFormalConcreteType() << ", " << AEI->getType();
  }
  void visitInitExistentialRefInst(InitExistentialRefInst *AEI) {
    *this << getIDAndType(AEI->getOperand()) << " : $"
          << AEI->getFormalConcreteType() << ", " << AEI->getType();
  }
  void visitInitExistentialMetatypeInst(InitExistentialMetatypeInst *AEI) {
    *this << getIDAndType(AEI->getOperand()) << ", " << AEI->getType();
  }
  void visitAllocExistentialBoxInst(AllocExistentialBoxInst *AEBI) {
    *this << AEBI->getExistentialType() << ", $"
          << AEBI->getFormalConcreteType();
  }
  void visitDeinitExistentialAddrInst(DeinitExistentialAddrInst *DEI) {
    *this << getIDAndType(DEI->getOperand());
  }
  void visitDeinitExistentialValueInst(DeinitExistentialValueInst *DEI) {
    *this << getIDAndType(DEI->getOperand());
  }
  void visitDeallocExistentialBoxInst(DeallocExistentialBoxInst *DEI) {
    *this << getIDAndType(DEI->getOperand()) << ", $" << DEI->getConcreteType();
  }
  void visitProjectBlockStorageInst(ProjectBlockStorageInst *PBSI) {
    *this << getIDAndType(PBSI->getOperand());
  }
  void visitInitBlockStorageHeaderInst(InitBlockStorageHeaderInst *IBSHI) {
    *this << getIDAndType(IBSHI->getBlockStorage()) << ", invoke "
          << Ctx.getID(IBSHI->getInvokeFunction());
    printSubstitutions(IBSHI->getSubstitutions());
    *this << " : " << IBSHI->getInvokeFunction()->getType()
          << ", type " << IBSHI->getType();
  }
  void visitValueMetatypeInst(ValueMetatypeInst *MI) {
    *this << MI->getType() << ", " << getIDAndType(MI->getOperand());
  }
  void visitExistentialMetatypeInst(ExistentialMetatypeInst *MI) {
    *this << MI->getType() << ", " << getIDAndType(MI->getOperand());
  }
  void visitMetatypeInst(MetatypeInst *MI) { *this << MI->getType(); }

  void visitFixLifetimeInst(FixLifetimeInst *RI) {
    *this << getIDAndType(RI->getOperand());
  }

  void visitEndLifetimeInst(EndLifetimeInst *ELI) {
    *this << getIDAndType(ELI->getOperand());
  }

  void visitMarkDependenceInst(MarkDependenceInst *MDI) {
    *this << getIDAndType(MDI->getValue()) << " on "
          << getIDAndType(MDI->getBase());
  }
  void visitCopyBlockInst(CopyBlockInst *RI) {
    *this << getIDAndType(RI->getOperand());
  }
  void visitRefCountingInst(RefCountingInst *I) {
    if (I->isNonAtomic())
      *this << "[nonatomic] ";
    *this << getIDAndType(I->getOperand(0));
  }
  void visitStrongPinInst(StrongPinInst *I) {
    if (I->isNonAtomic())
      *this << "[nonatomic] ";
    *this << getIDAndType(I->getOperand());
  }
  void visitIsUniqueInst(IsUniqueInst *CUI) {
    *this << getIDAndType(CUI->getOperand());
  }
  void visitIsUniqueOrPinnedInst(IsUniqueOrPinnedInst *CUI) {
    *this << getIDAndType(CUI->getOperand());
  }
  void visitDeallocStackInst(DeallocStackInst *DI) {
    *this << getIDAndType(DI->getOperand());
  }
  void visitDeallocRefInst(DeallocRefInst *DI) {
    if (DI->canAllocOnStack())
      *this << "[stack] ";
    *this << getIDAndType(DI->getOperand());
  }
  void visitDeallocPartialRefInst(DeallocPartialRefInst *DPI) {
    *this << getIDAndType(DPI->getInstance());
    *this << ", ";
    *this << getIDAndType(DPI->getMetatype());
  }
  void visitDeallocValueBufferInst(DeallocValueBufferInst *DVBI) {
    *this << DVBI->getValueType() << " in " << getIDAndType(DVBI->getOperand());
  }
  void visitDeallocBoxInst(DeallocBoxInst *DI) {
    *this << getIDAndType(DI->getOperand());
  }
  void visitDestroyAddrInst(DestroyAddrInst *DI) {
    *this << getIDAndType(DI->getOperand());
  }
  void visitProjectValueBufferInst(ProjectValueBufferInst *PVBI) {
    *this << PVBI->getValueType() << " in " << getIDAndType(PVBI->getOperand());
  }
  void visitProjectBoxInst(ProjectBoxInst *PBI) {
    *this << getIDAndType(PBI->getOperand()) << ", " << PBI->getFieldIndex();
  }
  void visitProjectExistentialBoxInst(ProjectExistentialBoxInst *PEBI) {
    *this << PEBI->getType().getObjectType()
          << " in " << getIDAndType(PEBI->getOperand());
  }
  void visitBeginAccessInst(BeginAccessInst *BAI) {
    *this << '[' << getSILAccessKindName(BAI->getAccessKind()) << "] ["
          << getSILAccessEnforcementName(BAI->getEnforcement())
          << "] " << getIDAndType(BAI->getOperand());
  }
  void visitEndAccessInst(EndAccessInst *EAI) {
    *this << (EAI->isAborting() ? "[abort] " : "")
          << getIDAndType(EAI->getOperand());
  }
  void visitBeginUnpairedAccessInst(BeginUnpairedAccessInst *BAI) {
    *this << '[' << getSILAccessKindName(BAI->getAccessKind()) << "] ["
          << getSILAccessEnforcementName(BAI->getEnforcement())
          << "] " << getIDAndType(BAI->getSource()) << ", "
          << getIDAndType(BAI->getBuffer());
  }
  void visitEndUnpairedAccessInst(EndUnpairedAccessInst *EAI) {
    *this << (EAI->isAborting() ? "[abort] " : "")
          << '[' << getSILAccessEnforcementName(EAI->getEnforcement()) << "] "
          << getIDAndType(EAI->getOperand());
  }

  void visitCondFailInst(CondFailInst *FI) {
    *this << getIDAndType(FI->getOperand());
  }
  
  void visitIndexAddrInst(IndexAddrInst *IAI) {
    *this << getIDAndType(IAI->getBase()) << ", "
          << getIDAndType(IAI->getIndex());
  }

  void visitTailAddrInst(TailAddrInst *TAI) {
    *this << getIDAndType(TAI->getBase()) << ", "
          << getIDAndType(TAI->getIndex()) << ", " << TAI->getTailType();
  }

  void visitIndexRawPointerInst(IndexRawPointerInst *IAI) {
    *this << getIDAndType(IAI->getBase()) << ", "
          << getIDAndType(IAI->getIndex());
  }

  void visitUnreachableInst(UnreachableInst *UI) {}

  void visitReturnInst(ReturnInst *RI) {
    *this << getIDAndType(RI->getOperand());
  }
  
  void visitThrowInst(ThrowInst *TI) {
    *this << getIDAndType(TI->getOperand());
  }

  void visitUnwindInst(UnwindInst *UI) {
    // no operands
  }

  void visitYieldInst(YieldInst *YI) {
    auto values = YI->getYieldedValues();
    if (values.size() != 1) *this << '(';
    interleave(values,
               [&](SILValue value) { *this << getIDAndType(value); },
               [&] { *this << ", "; });
    if (values.size() != 1) *this << ')';
    *this << ", resume " << Ctx.getID(YI->getResumeBB())
          << ", unwind " << Ctx.getID(YI->getUnwindBB());
  }

  void visitSwitchValueInst(SwitchValueInst *SII) {
    *this << getIDAndType(SII->getOperand());
    for (unsigned i = 0, e = SII->getNumCases(); i < e; ++i) {
      SILValue value;
      SILBasicBlock *dest;
      std::tie(value, dest) = SII->getCase(i);
      *this << ", case " << Ctx.getID(value) << ": " << Ctx.getID(dest);
    }
    if (SII->hasDefault())
      *this << ", default " << Ctx.getID(SII->getDefaultBB());
  }
  
  void printSwitchEnumInst(SwitchEnumInstBase *SOI) {
    *this << getIDAndType(SOI->getOperand());
    for (unsigned i = 0, e = SOI->getNumCases(); i < e; ++i) {
      EnumElementDecl *elt;
      SILBasicBlock *dest;
      std::tie(elt, dest) = SOI->getCase(i);
      *this << ", case " << SILDeclRef(elt, SILDeclRef::Kind::EnumElement)
            << ": " << Ctx.getID(dest);
      if (SOI->getCaseCount(i)) {
        *this << " !case_count(" << SOI->getCaseCount(i).getValue() << ")";
      }
    }
    if (SOI->hasDefault()) {
      *this << ", default " << Ctx.getID(SOI->getDefaultBB());
      if (SOI->getDefaultCount()) {
        *this << " !default_count(" << SOI->getDefaultCount().getValue() << ")";
      }
    }
  }
  
  void visitSwitchEnumInst(SwitchEnumInst *SOI) {
    printSwitchEnumInst(SOI);
  }
  void visitSwitchEnumAddrInst(SwitchEnumAddrInst *SOI) {
    printSwitchEnumInst(SOI);
  }
  
  void printSelectEnumInst(SelectEnumInstBase *SEI) {
    *this << getIDAndType(SEI->getEnumOperand());

    for (unsigned i = 0, e = SEI->getNumCases(); i < e; ++i) {
      EnumElementDecl *elt;
      SILValue result;
      std::tie(elt, result) = SEI->getCase(i);
      *this << ", case " << SILDeclRef(elt, SILDeclRef::Kind::EnumElement)
            << ": " << Ctx.getID(result);
    }
    if (SEI->hasDefault())
      *this << ", default " << Ctx.getID(SEI->getDefaultResult());

    *this << " : " << SEI->getType();
  }

  void visitSelectEnumInst(SelectEnumInst *SEI) {
    printSelectEnumInst(SEI);
  }
  void visitSelectEnumAddrInst(SelectEnumAddrInst *SEI) {
    printSelectEnumInst(SEI);
  }

  void visitSelectValueInst(SelectValueInst *SVI) {
    *this << getIDAndType(SVI->getOperand());

    for (unsigned i = 0, e = SVI->getNumCases(); i < e; ++i) {
      SILValue casevalue;
      SILValue result;
      std::tie(casevalue, result) = SVI->getCase(i);
      *this << ", case " << Ctx.getID(casevalue) << ": " << Ctx.getID(result);
    }
    if (SVI->hasDefault())
      *this << ", default " << Ctx.getID(SVI->getDefaultResult());

    *this << " : " << SVI->getType();
  }
  
  void visitDynamicMethodBranchInst(DynamicMethodBranchInst *DMBI) {
    *this << getIDAndType(DMBI->getOperand()) << ", " << DMBI->getMember()
          << ", " << Ctx.getID(DMBI->getHasMethodBB()) << ", "
          << Ctx.getID(DMBI->getNoMethodBB());
  }

  void printBranchArgs(OperandValueArrayRef args) {
    if (args.empty()) return;

    *this << '(';
    interleave(args,
               [&](SILValue v) { *this << getIDAndType(v); },
               [&] { *this << ", "; });
    *this << ')';
  }
  
  void visitBranchInst(BranchInst *UBI) {
    *this << Ctx.getID(UBI->getDestBB());
    printBranchArgs(UBI->getArgs());
  }

  void visitCondBranchInst(CondBranchInst *CBI) {
    *this << Ctx.getID(CBI->getCondition()) << ", "
          << Ctx.getID(CBI->getTrueBB());
    printBranchArgs(CBI->getTrueArgs());
    *this << ", " << Ctx.getID(CBI->getFalseBB());
    printBranchArgs(CBI->getFalseArgs());
    if (CBI->getTrueBBCount())
      *this << " !true_count(" << CBI->getTrueBBCount().getValue() << ")";
    if (CBI->getFalseBBCount())
      *this << " !false_count(" << CBI->getFalseBBCount().getValue() << ")";
  }
  
  void visitKeyPathInst(KeyPathInst *KPI) {
    *this << KPI->getType() << ", ";
    
    auto pattern = KPI->getPattern();
    
    if (pattern->getGenericSignature()) {
      pattern->getGenericSignature()->print(PrintState.OS);
      *this << ' ';
    }
    
    *this << "(";
    
    if (!pattern->getObjCString().empty())
      *this << "objc \"" << pattern->getObjCString() << "\"; ";
    
    *this << "root $" << KPI->getPattern()->getRootType();
    
    for (auto &component : pattern->getComponents()) {
      *this << "; ";
      
      switch (auto kind = component.getKind()) {
      case KeyPathPatternComponent::Kind::StoredProperty: {
        auto prop = component.getStoredPropertyDecl();
        *this << "stored_property #";
        printValueDecl(prop, PrintState.OS);
        *this << " : $" << component.getComponentType();
        break;
      }
      case KeyPathPatternComponent::Kind::GettableProperty:
      case KeyPathPatternComponent::Kind::SettableProperty: {
        *this << (kind == KeyPathPatternComponent::Kind::GettableProperty
                    ? "gettable_property $" : "settable_property $")
              << component.getComponentType() << ", "
              << " id ";
        auto id = component.getComputedPropertyId();
        switch (id.getKind()) {
        case KeyPathPatternComponent::ComputedPropertyId::DeclRef: {
          auto declRef = id.getDeclRef();
          *this << declRef << " : "
                << declRef.getDecl()->getInterfaceType();
          break;
        }
        case KeyPathPatternComponent::ComputedPropertyId::Function: {
          id.getFunction()->printName(PrintState.OS);
          *this << " : " << id.getFunction()->getLoweredType();
          break;
        }
        case KeyPathPatternComponent::ComputedPropertyId::Property: {
          *this << "##";
          printValueDecl(id.getProperty(), PrintState.OS);
          break;
        }
        }
        *this << ", getter ";
        component.getComputedPropertyGetter()->printName(PrintState.OS);
        *this << " : "
              << component.getComputedPropertyGetter()->getLoweredType();
        if (kind == KeyPathPatternComponent::Kind::SettableProperty) {
          *this << ", setter ";
          component.getComputedPropertySetter()->printName(PrintState.OS);
          *this << " : "
                << component.getComputedPropertySetter()->getLoweredType();
        }
        
        if (!component.getComputedPropertyIndices().empty()) {
          *this << ", indices [";
          interleave(component.getComputedPropertyIndices(),
            [&](const KeyPathPatternComponent::Index &i) {
              *this << "%$" << i.Operand << " : $"
                    << i.FormalType << " : "
                    << i.LoweredType;
            }, [&]{
              *this << ", ";
            });
          *this << "], indices_equals ";
          component.getComputedPropertyIndexEquals()->printName(PrintState.OS);
          *this << " : "
                << component.getComputedPropertyIndexEquals()->getLoweredType();
          *this << ", indices_hash ";
          component.getComputedPropertyIndexHash()->printName(PrintState.OS);
          *this << " : "
                << component.getComputedPropertyIndexHash()->getLoweredType();
        }
        break;
      }
      case KeyPathPatternComponent::Kind::OptionalWrap:
      case KeyPathPatternComponent::Kind::OptionalChain:
      case KeyPathPatternComponent::Kind::OptionalForce: {
        switch (kind) {
        case KeyPathPatternComponent::Kind::OptionalWrap:
          *this << "optional_wrap : $";
          break;
        case KeyPathPatternComponent::Kind::OptionalChain:
          *this << "optional_chain : $";
          break;
        case KeyPathPatternComponent::Kind::OptionalForce:
          *this << "optional_force : $";
          break;
        default:
          llvm_unreachable("out of sync");
        }
        *this << component.getComponentType();
        break;
      }
      }
    }
    
    *this << ')';
    if (!KPI->getSubstitutions().empty()) {
      *this << ' ';
      printSubstitutions(KPI->getSubstitutions());
    }
    if (!KPI->getAllOperands().empty()) {
      *this << " (";
      
      interleave(KPI->getAllOperands(),
        [&](const Operand &operand) {
          *this << Ctx.getID(operand.get());
        }, [&]{
          *this << ", ";
        });
      
      *this << ")";
    }
  }
};
} // end anonymous namespace

static void printBlockID(raw_ostream &OS, SILBasicBlock *bb) {
  SILPrintContext Ctx(OS);
  OS << Ctx.getID(bb);
}

void SILBasicBlock::printAsOperand(raw_ostream &OS, bool PrintType) {
  printBlockID(OS, this);
}

//===----------------------------------------------------------------------===//
// Printing for SILInstruction, SILBasicBlock, SILFunction, and SILModule
//===----------------------------------------------------------------------===//

void SILNode::dump() const {
  print(llvm::errs());
}

void SILNode::print(raw_ostream &OS) const {
  SILPrintContext Ctx(OS);
  SILPrinter(Ctx).print(this);
}

void SILInstruction::dump() const {
  print(llvm::errs());
}

void SingleValueInstruction::dump() const {
  SILInstruction::dump();
}

void SILInstruction::print(raw_ostream &OS) const {
  SILPrintContext Ctx(OS);
  SILPrinter(Ctx).print(this);
}

/// Pretty-print the SILBasicBlock to errs.
void SILBasicBlock::dump() const {
  print(llvm::errs());
}

/// Pretty-print the SILBasicBlock to the designated stream.
void SILBasicBlock::print(raw_ostream &OS) const {
  SILPrintContext Ctx(OS);
  SILPrinter(Ctx).print(this);
}

void SILBasicBlock::print(raw_ostream &OS, SILPrintContext &Ctx) const {
  SILPrinter(Ctx).print(this);
}

/// Pretty-print the SILFunction to errs.
void SILFunction::dump(bool Verbose) const {
  SILPrintContext Ctx(llvm::errs(), Verbose);
  print(Ctx);
}

// This is out of line so the debugger can find it.
void SILFunction::dump() const {
  dump(false);
}

void SILFunction::dump(const char *FileName) const {
  std::error_code EC;
  llvm::raw_fd_ostream os(FileName, EC, llvm::sys::fs::OpenFlags::F_None);
  print(os);
}

static StringRef getLinkageString(SILLinkage linkage) {
  switch (linkage) {
  case SILLinkage::Public: return "public ";
  case SILLinkage::Hidden: return "hidden ";
  case SILLinkage::Shared: return "shared ";
  case SILLinkage::Private: return "private ";
  case SILLinkage::PublicExternal: return "public_external ";
  case SILLinkage::HiddenExternal: return "hidden_external ";
  case SILLinkage::SharedExternal: return "shared_external ";
  case SILLinkage::PrivateExternal: return "private_external ";
  }
  llvm_unreachable("bad linkage");
}

static void printLinkage(llvm::raw_ostream &OS, SILLinkage linkage,
                         bool isDefinition) {
  if ((isDefinition && linkage == SILLinkage::DefaultForDefinition) ||
      (!isDefinition && linkage == SILLinkage::DefaultForDeclaration))
    return;

  OS << getLinkageString(linkage);
}

/// Pretty-print the SILFunction to the designated stream.
void SILFunction::print(SILPrintContext &PrintCtx) const {
  llvm::raw_ostream &OS = PrintCtx.OS();
  if (PrintCtx.printDebugInfo()) {
    auto &SM = getModule().getASTContext().SourceMgr;
    for (auto &BB : *this)
      for (auto &I : BB) {
        SILPrinter P(PrintCtx);
        P.printDebugScope(I.getDebugScope(), SM);
      }
    OS << "\n";
  }

  if (SILPrintGenericSpecializationInfo) {
    if (isSpecialization()) {
      printGenericSpecializationInfo(OS, "function", getName(),
                                     getSpecializationInfo());
    }
  }

  OS << "// " << demangleSymbol(getName()) << '\n';
  OS << "sil ";
  printLinkage(OS, getLinkage(), isDefinition());

  if (isTransparent())
    OS << "[transparent] ";

  switch (isSerialized()) {
  case IsNotSerialized: break;
  case IsSerializable: OS << "[serializable] "; break;
  case IsSerialized: OS << "[serialized] "; break;
  }

  switch (isThunk()) {
  case IsNotThunk: break;
  case IsThunk: OS << "[thunk] "; break;
  case IsReabstractionThunk: OS << "[reabstraction_thunk] "; break;
  }

  if (isGlobalInit())
    OS << "[global_init] ";
  
  switch (getInlineStrategy()) {
    case NoInline: OS << "[noinline] "; break;
    case AlwaysInline: OS << "[always_inline] "; break;
    case InlineDefault: break;
  }

  switch (getOptimizationMode()) {
    case OptimizationMode::NoOptimization: OS << "[Onone] "; break;
    case OptimizationMode::ForSpeed: OS << "[Ospeed] "; break;
    case OptimizationMode::ForSize: OS << "[Osize] "; break;
    default: break;
  }

  if (getEffectsKind() == EffectsKind::ReadOnly)
    OS << "[readonly] ";
  else if (getEffectsKind() == EffectsKind::ReadNone)
      OS << "[readnone] ";
  if (getEffectsKind() == EffectsKind::ReadWrite)
    OS << "[readwrite] ";

  for (auto &Attr : getSemanticsAttrs())
    OS << "[_semantics \"" << Attr << "\"] ";

  for (auto *Attr : getSpecializeAttrs()) {
    OS << "[_specialize "; Attr->print(OS); OS << "] ";
  }

  // TODO: Handle clang node owners which don't have a name.
  if (hasClangNode() && getClangNodeOwner()->hasName()) {
    OS << "[clang ";
    printValueDecl(getClangNodeOwner(), OS);
    OS << "] ";
  }

  printName(OS);
  OS << " : $";
  
  // Print the type by substituting our context parameter names for the dependent
  // parameters. In SIL, we may end up with multiple generic parameters that
  // have the same name from different contexts, for instance, a generic
  // protocol requirement with a generic method parameter <T>, which is
  // witnessed by a generic type that has a generic type parameter also named
  // <T>, so we may need to introduce disambiguating aliases.
  llvm::DenseMap<CanType, Identifier> Aliases;
  llvm::DenseSet<Identifier> UsedNames;
  
  auto sig = getLoweredFunctionType()->getGenericSignature();
  auto *env = getGenericEnvironment();
  if (sig && env) {
    llvm::SmallString<16> disambiguatedNameBuf;
    unsigned disambiguatedNameCounter = 1;
    for (auto *paramTy : sig->getGenericParams()) {
      auto sugaredTy = env->getSugaredType(paramTy);
      Identifier name = sugaredTy->getName();
      while (!UsedNames.insert(name).second) {
        disambiguatedNameBuf.clear();
        {
          llvm::raw_svector_ostream names(disambiguatedNameBuf);
          names << sugaredTy->getName() << disambiguatedNameCounter++;
        }
        name = getASTContext().getIdentifier(disambiguatedNameBuf);
      }
      if (name != sugaredTy->getName()) {
        Aliases[paramTy->getCanonicalType()] = name;

        // Also for the archetype
        auto archetypeTy = env->mapTypeIntoContext(paramTy)
            ->getAs<ArchetypeType>();
        if (archetypeTy)
          Aliases[archetypeTy->getCanonicalType()] = name;
      }
    }
  }

  {
    PrintOptions withGenericEnvironment = PrintOptions::printSIL();
    withGenericEnvironment.GenericEnv = env;
    withGenericEnvironment.AlternativeTypeNames =
      Aliases.empty() ? nullptr : &Aliases;
    LoweredType->print(OS, withGenericEnvironment);
  }
  
  if (!isExternalDeclaration()) {
    if (auto eCount = getEntryCount()) {
      OS << " !function_entry_count(" << eCount.getValue() << ")";
    }
    OS << " {\n";

    SILPrinter(PrintCtx, (Aliases.empty() ? nullptr : &Aliases))
        .print(this);
    OS << "} // end sil function '" << getName() << '\'';
  }

  OS << "\n\n";
}
      
/// Pretty-print the SILFunction's name using SIL syntax,
/// '@function_mangled_name'.
void SILFunction::printName(raw_ostream &OS) const {
  OS << "@" << Name;  
}

/// Pretty-print a global variable to the designated stream.
void SILGlobalVariable::print(llvm::raw_ostream &OS, bool Verbose) const {
  OS << "// " << demangleSymbol(getName()) << '\n';
  
  OS << "sil_global ";
  printLinkage(OS, getLinkage(), isDefinition());

  if (isSerialized())
    OS << "[serialized] ";
  
  if (isLet())
    OS << "[let] ";

  printName(OS);
  OS << " : " << LoweredType;

  if (!StaticInitializerBlock.empty()) {
    OS << " = {\n";
    {
      SILPrintContext Ctx(OS);
      SILPrinter Printer(Ctx);
      for (const SILInstruction &I : StaticInitializerBlock) {
        Printer.print(&I);
      }
    }
    OS << "}\n";
  }

  OS << "\n\n";
}

void SILGlobalVariable::dump(bool Verbose) const {
  print(llvm::errs(), Verbose);
}
void SILGlobalVariable::dump() const {
   dump(false);
}

void SILGlobalVariable::printName(raw_ostream &OS) const {
  OS << "@" << Name;
}
      
/// Pretty-print the SILModule to errs.
void SILModule::dump(bool Verbose) const {
  SILPrintContext Ctx(llvm::errs(), Verbose);
  print(Ctx);
}

void SILModule::dump(const char *FileName, bool Verbose,
                     bool PrintASTDecls) const {
  std::error_code EC;
  llvm::raw_fd_ostream os(FileName, EC, llvm::sys::fs::OpenFlags::F_None);
  SILPrintContext Ctx(os, Verbose);
  print(Ctx, getSwiftModule(), PrintASTDecls);
}

static void printSILGlobals(SILPrintContext &Ctx,
                            const SILModule::GlobalListType &Globals) {
  if (!Ctx.sortSIL()) {
    for (const SILGlobalVariable &g : Globals)
      g.print(Ctx.OS(), Ctx.printVerbose());
    return;
  }

  std::vector<const SILGlobalVariable *> globals;
  globals.reserve(Globals.size());
  for (const SILGlobalVariable &g : Globals)
    globals.push_back(&g);
  std::sort(globals.begin(), globals.end(),
    [] (const SILGlobalVariable *g1, const SILGlobalVariable *g2) -> bool {
      return g1->getName().compare(g2->getName()) == -1;
    }
  );
  for (const SILGlobalVariable *g : globals)
    g->print(Ctx.OS(), Ctx.printVerbose());
}

static void printSILFunctions(SILPrintContext &Ctx,
                              const SILModule::FunctionListType &Functions) {
  if (!Ctx.sortSIL()) {
    for (const SILFunction &f : Functions)
      f.print(Ctx);
    return;
  }

  std::vector<const SILFunction *> functions;
  functions.reserve(Functions.size());
  for (const SILFunction &f : Functions)
    functions.push_back(&f);
  std::sort(functions.begin(), functions.end(),
    [] (const SILFunction *f1, const SILFunction *f2) -> bool {
      return f1->getName().compare(f2->getName()) == -1;
    }
  );
  for (const SILFunction *f : functions)
    f->print(Ctx);
}

static void printSILVTables(SILPrintContext &Ctx,
                            const SILModule::VTableListType &VTables) {
  if (!Ctx.sortSIL()) {
    for (const SILVTable &vt : VTables)
      vt.print(Ctx.OS(), Ctx.printVerbose());
    return;
  }

  std::vector<const SILVTable *> vtables;
  vtables.reserve(VTables.size());
  for (const SILVTable &vt : VTables)
    vtables.push_back(&vt);
  std::sort(vtables.begin(), vtables.end(),
    [] (const SILVTable *v1, const SILVTable *v2) -> bool {
      StringRef Name1 = v1->getClass()->getName().str();
      StringRef Name2 = v2->getClass()->getName().str();
      return Name1.compare(Name2) == -1;
    }
  );
  for (const SILVTable *vt : vtables)
    vt->print(Ctx.OS(), Ctx.printVerbose());
}

static void
printSILWitnessTables(SILPrintContext &Ctx,
                      const SILModule::WitnessTableListType &WTables) {
  if (!Ctx.sortSIL()) {
    for (const SILWitnessTable &wt : WTables)
      wt.print(Ctx.OS(), Ctx.printVerbose());
    return;
  }

  std::vector<const SILWitnessTable *> witnesstables;
  witnesstables.reserve(WTables.size());
  for (const SILWitnessTable &wt : WTables)
    witnesstables.push_back(&wt);
  std::sort(witnesstables.begin(), witnesstables.end(),
    [] (const SILWitnessTable *w1, const SILWitnessTable *w2) -> bool {
      return w1->getName().compare(w2->getName()) == -1;
    }
  );
  for (const SILWitnessTable *wt : witnesstables)
    wt->print(Ctx.OS(), Ctx.printVerbose());
}

static void
printSILDefaultWitnessTables(SILPrintContext &Ctx,
                        const SILModule::DefaultWitnessTableListType &WTables) {
  if (!Ctx.sortSIL()) {
    for (const SILDefaultWitnessTable &wt : WTables)
      wt.print(Ctx.OS(), Ctx.printVerbose());
    return;
  }

  std::vector<const SILDefaultWitnessTable *> witnesstables;
  witnesstables.reserve(WTables.size());
  for (const SILDefaultWitnessTable &wt : WTables)
    witnesstables.push_back(&wt);
  std::sort(witnesstables.begin(), witnesstables.end(),
    [] (const SILDefaultWitnessTable *w1,
        const SILDefaultWitnessTable *w2) -> bool {
      return w1->getProtocol()->getName()
          .compare(w2->getProtocol()->getName()) == -1;
    }
  );
  for (const SILDefaultWitnessTable *wt : witnesstables)
    wt->print(Ctx.OS(), Ctx.printVerbose());
}

static void
printSILCoverageMaps(SILPrintContext &Ctx,
                     const SILModule::CoverageMapListType &CoverageMaps) {
  if (!Ctx.sortSIL()) {
    for (const SILCoverageMap &M : CoverageMaps)
      M.print(Ctx);
    return;
  }

  std::vector<const SILCoverageMap *> Maps;
  Maps.reserve(CoverageMaps.size());
  for (const SILCoverageMap &M : CoverageMaps)
    Maps.push_back(&M);
  std::sort(Maps.begin(), Maps.end(),
            [](const SILCoverageMap *LHS, const SILCoverageMap *RHS) -> bool {
              return LHS->getName().compare(RHS->getName()) == -1;
            });
  for (const SILCoverageMap *M : Maps)
    M->print(Ctx);
}

/// Pretty-print the SILModule to the designated stream.
void SILModule::print(SILPrintContext &PrintCtx, ModuleDecl *M,
                      bool PrintASTDecls) const {
  llvm::raw_ostream &OS = PrintCtx.OS();
  OS << "sil_stage ";
  switch (Stage) {
  case SILStage::Raw:
    OS << "raw";
    break;
  case SILStage::Canonical:
    OS << "canonical";
    break;
  case SILStage::Lowered:
    OS << "lowered";
    break;
  }
  
  OS << "\n\nimport Builtin\nimport " << STDLIB_NAME
     << "\nimport SwiftShims" << "\n\n";

  // Print the declarations and types from the origin module, unless we're not
  // in whole-module mode.
  if (M && AssociatedDeclContext == M && PrintASTDecls) {
    PrintOptions Options = PrintOptions::printSIL();
    Options.TypeDefinitions = true;
    Options.VarInitializers = true;
    // FIXME: ExplodePatternBindingDecls is incompatible with VarInitializers!
    Options.ExplodePatternBindingDecls = true;
    Options.SkipImplicit = false;
    Options.PrintGetSetOnRWProperties = true;
    Options.PrintInSILBody = false;
    Options.PrintDefaultParameterPlaceholder = false;

    SmallVector<Decl *, 32> topLevelDecls;
    M->getTopLevelDecls(topLevelDecls);
    for (const Decl *D : topLevelDecls) {
      if ((isa<ValueDecl>(D) || isa<OperatorDecl>(D) ||
           isa<ExtensionDecl>(D)) &&
          !D->isImplicit()) {
        if (auto *FD = dyn_cast<FuncDecl>(D))
          if (FD->isAccessor())
            continue;
        D->print(OS, Options);
        OS << "\n\n";
      }
    }
  }

  printSILGlobals(PrintCtx, getSILGlobalList());
  printSILFunctions(PrintCtx, getFunctionList());
  printSILVTables(PrintCtx, getVTableList());
  printSILWitnessTables(PrintCtx, getWitnessTableList());
  printSILDefaultWitnessTables(PrintCtx, getDefaultWitnessTableList());
  printSILCoverageMaps(PrintCtx, getCoverageMapList());
  
  OS << "\n\n";
}

void SILNode::dumpInContext() const {
  printInContext(llvm::errs());
}
void SILNode::printInContext(llvm::raw_ostream &OS) const {
  SILPrintContext Ctx(OS);
  SILPrinter(Ctx).printInContext(this);
}

void SILInstruction::dumpInContext() const {
  printInContext(llvm::errs());
}
void SILInstruction::printInContext(llvm::raw_ostream &OS) const {
  SILPrintContext Ctx(OS);
  SILPrinter(Ctx).printInContext(this);
}

void SILVTable::print(llvm::raw_ostream &OS, bool Verbose) const {
  OS << "sil_vtable ";
  if (isSerialized())
    OS << "[serialized] ";
  OS << getClass()->getName() << " {\n";

  PrintOptions QualifiedSILTypeOptions = PrintOptions::printQualifiedSILType();
  for (auto &entry : getEntries()) {
    OS << "  ";
    entry.Method.print(OS);
    OS << ": ";

    bool HasSingleImplementation = false;
    switch (entry.Method.kind) {
    default:
      break;
    case SILDeclRef::Kind::IVarDestroyer:
    case SILDeclRef::Kind::Destroyer:
    case SILDeclRef::Kind::Deallocator:
      HasSingleImplementation = true;
    }
    // No need to emit the signature for methods that may have only
    // single implementation, e.g. for destructors.
    if (!HasSingleImplementation) {
      QualifiedSILTypeOptions.CurrentModule =
          entry.Method.getDecl()->getDeclContext()->getParentModule();
      entry.Method.getDecl()->getInterfaceType().print(OS,
                                                       QualifiedSILTypeOptions);
      OS << " : ";
    }
    if (entry.Linkage !=
        stripExternalFromLinkage(entry.Implementation->getLinkage())) {
      OS << getLinkageString(entry.Linkage);
    }
    OS << entry.Implementation->getName();
    switch (entry.TheKind) {
    case SILVTable::Entry::Kind::Normal:
      break;
    case SILVTable::Entry::Kind::Inherited:
      OS << " [inherited]";
      break;
    case SILVTable::Entry::Kind::Override:
      OS << " [override]";
      break;
    }
    OS << "\t// " << demangleSymbol(entry.Implementation->getName());
    OS << "\n";
  }
  OS << "}\n\n";
}

void SILVTable::dump() const {
  print(llvm::errs());
}

/// Returns true if anything was printed.
static bool printAssociatedTypePath(llvm::raw_ostream &OS, CanType path) {
  if (auto memberType = dyn_cast<DependentMemberType>(path)) {
    if (printAssociatedTypePath(OS, memberType.getBase()))
      OS << '.';
    OS << memberType->getName().str();
    return true;
  } else {
    assert(isa<GenericTypeParamType>(path));
    return false;
  }
}

void SILWitnessTable::print(llvm::raw_ostream &OS, bool Verbose) const {
  PrintOptions Options = PrintOptions::printSIL();
  PrintOptions QualifiedSILTypeOptions = PrintOptions::printQualifiedSILType();
  OS << "sil_witness_table ";
  printLinkage(OS, getLinkage(), /*isDefinition*/ isDefinition());
  if (isSerialized())
    OS << "[serialized] ";

  getConformance()->printName(OS, Options);
  Options.GenericEnv =
    getConformance()->getDeclContext()->getGenericEnvironmentOfContext();

  if (isDeclaration()) {
    OS << "\n\n";
    return;
  }

  OS << " {\n";
  
  for (auto &witness : getEntries()) {
    OS << "  ";
    switch (witness.getKind()) {
    case Invalid:
      llvm_unreachable("invalid witness?!");
    case Method: {
      // method #declref: @function
      auto &methodWitness = witness.getMethodWitness();
      OS << "method ";
      methodWitness.Requirement.print(OS);
      OS << ": ";
      QualifiedSILTypeOptions.CurrentModule =
          methodWitness.Requirement.getDecl()
              ->getDeclContext()
              ->getParentModule();
      methodWitness.Requirement.getDecl()->getInterfaceType().print(
          OS, QualifiedSILTypeOptions);
      OS << " : ";
      if (methodWitness.Witness) {
        methodWitness.Witness->printName(OS);
        OS << "\t// "
           << demangleSymbol(methodWitness.Witness->getName());
      } else {
        OS << "nil";
      }
      break;
    }
    case AssociatedType: {
      // associated_type AssociatedTypeName: ConformingType
      auto &assocWitness = witness.getAssociatedTypeWitness();
      OS << "associated_type ";
      OS << assocWitness.Requirement->getName() << ": ";
      assocWitness.Witness->print(OS, Options);
      break;
    }
    case AssociatedTypeProtocol: {
      // associated_type_protocol (AssociatedTypeName: Protocol): <conformance>
      auto &assocProtoWitness = witness.getAssociatedTypeProtocolWitness();
      OS << "associated_type_protocol (";
      (void) printAssociatedTypePath(OS, assocProtoWitness.Requirement);
      OS << ": " << assocProtoWitness.Protocol->getName() << "): ";
      if (assocProtoWitness.Witness.isConcrete())
        assocProtoWitness.Witness.getConcrete()->printName(OS, Options);
      else
        OS << "dependent";
      break;
    }
    case BaseProtocol: {
      // base_protocol Protocol: <conformance>
      auto &baseProtoWitness = witness.getBaseProtocolWitness();
      OS << "base_protocol "
         << baseProtoWitness.Requirement->getName() << ": ";
      baseProtoWitness.Witness->printName(OS, Options);
      break;
    }
    case MissingOptional: {
      // optional requirement 'declref': <<not present>>
      OS << "optional requirement '"
         << witness.getMissingOptionalWitness().Witness->getBaseName()
         << "': <<not present>>";
      break;
    }
    }
    OS << '\n';
  }

  for (auto conditionalConformance : getConditionalConformances()) {
    // conditional_conformance (TypeName: Protocol):
    // <conformance>
    OS << "  conditional_conformance (";
    conditionalConformance.Requirement.print(OS, Options);
    OS << ": " << conditionalConformance.Conformance.getRequirement()->getName()
       << "): ";
    if (conditionalConformance.Conformance.isConcrete())
      conditionalConformance.Conformance.getConcrete()->printName(OS, Options);
    else
      OS << "dependent";

    OS << '\n';
  }

  OS << "}\n\n";
}

void SILWitnessTable::dump() const {
  print(llvm::errs());
}

void SILDefaultWitnessTable::print(llvm::raw_ostream &OS, bool Verbose) const {
  // sil_default_witness_table [<Linkage>] <Protocol> <MinSize>
  PrintOptions QualifiedSILTypeOptions = PrintOptions::printQualifiedSILType();
  OS << "sil_default_witness_table ";
  printLinkage(OS, getLinkage(), ForDefinition);
  OS << getProtocol()->getName() << " {\n";
  
  for (auto &witness : getEntries()) {
    if (!witness.isValid()) {
      OS << "  no_default\n";
      continue;
    }

    // method #declref: @function
    OS << "  method ";
    witness.getRequirement().print(OS);
    OS << ": ";
    QualifiedSILTypeOptions.CurrentModule =
        witness.getRequirement().getDecl()->getDeclContext()->getParentModule();
    witness.getRequirement().getDecl()->getInterfaceType().print(
        OS, QualifiedSILTypeOptions);
    OS << " : ";
    witness.getWitness()->printName(OS);
    OS << "\t// "
       << Demangle::demangleSymbolAsString(witness.getWitness()->getName());
    OS << '\n';
  }
  
  OS << "}\n\n";
}

void SILDefaultWitnessTable::dump() const {
  print(llvm::errs());
}

void SILCoverageMap::print(SILPrintContext &PrintCtx) const {
  llvm::raw_ostream &OS = PrintCtx.OS();
  OS << "sil_coverage_map " << QuotedString(getFile()) << " " << getName()
     << " " << QuotedString(getPGOFuncName()) << " " << getHash() << " {\t// "
     << demangleSymbol(getName()) << "\n";
  if (PrintCtx.sortSIL())
    std::sort(MappedRegions.begin(), MappedRegions.end(),
              [](const MappedRegion &LHS, const MappedRegion &RHS) {
      return std::tie(LHS.StartLine, LHS.StartCol, LHS.EndLine, LHS.EndCol) <
             std::tie(RHS.StartLine, RHS.StartCol, RHS.EndLine, RHS.EndCol);
    });
  for (auto &MR : getMappedRegions()) {
    OS << "  " << MR.StartLine << ":" << MR.StartCol << " -> " << MR.EndLine
       << ":" << MR.EndCol << " : ";
    printCounter(OS, MR.Counter);
    OS << "\n";
  }
  OS << "}\n\n";
}

void SILCoverageMap::dump() const {
  print(llvm::errs());
}

#ifndef NDEBUG
void SILDebugScope::dump(SourceManager &SM, llvm::raw_ostream &OS,
                         unsigned Indent) const {
  OS << "{\n";
  OS.indent(Indent);
  if (Loc.isASTNode())
    Loc.getSourceLoc().print(OS, SM);
  OS << "\n";
  OS.indent(Indent + 2);
  OS << " parent: ";
  if (auto *P = Parent.dyn_cast<const SILDebugScope *>()) {
    P->dump(SM, OS, Indent + 2);
    OS.indent(Indent + 2);
  }
  else if (auto *F = Parent.dyn_cast<SILFunction *>())
    OS << "@" << F->getName();
  else
    OS << "nullptr";

  OS << "\n";
  OS.indent(Indent + 2);
  if (auto *CS = InlinedCallSite) {
    OS << "inlinedCallSite: ";
    CS->dump(SM, OS, Indent + 2);
    OS.indent(Indent + 2);
  }
  OS << "}\n";
}
#endif

void SILSpecializeAttr::print(llvm::raw_ostream &OS) const {
  SILPrintContext Ctx(OS);
  // Print other types as their Swift representation.
  PrintOptions SubPrinter = PrintOptions::printSIL();
  auto exported = isExported() ? "true" : "false";
  auto kind = isPartialSpecialization() ? "partial" : "full";

  OS << "exported: " << exported << ", ";
  OS << "kind: " << kind << ", ";

  if (!getRequirements().empty()) {
    OS << "where ";
    SILFunction *F = getFunction();
    assert(F);
    auto GenericEnv = F->getGenericEnvironment();
    assert(GenericEnv);
    interleave(getRequirements(),
               [&](Requirement req) {
                 // Use GenericEnvironment to produce user-friendly
                 // names instead of something like t_0_0.
                 auto FirstTy = GenericEnv->getSugaredType(req.getFirstType());
                 if (req.getKind() != RequirementKind::Layout) {
                   auto SecondTy =
                       GenericEnv->getSugaredType(req.getSecondType());
                   Requirement ReqWithDecls(req.getKind(), FirstTy, SecondTy);
                   ReqWithDecls.print(OS, SubPrinter);
                 } else {
                   Requirement ReqWithDecls(req.getKind(), FirstTy,
                                            req.getLayoutConstraint());
                   ReqWithDecls.print(OS, SubPrinter);
                 }
               },
               [&] { OS << ", "; });
  }
}

//===----------------------------------------------------------------------===//
// SILPrintContext members
//===----------------------------------------------------------------------===//

SILPrintContext::SILPrintContext(llvm::raw_ostream &OS, bool Verbose,
                bool SortedSIL) :
  OutStream(OS), Verbose(Verbose), SortedSIL(SortedSIL),
  DebugInfo(SILPrintDebugInfo) { }

SILPrintContext::SILPrintContext(llvm::raw_ostream &OS, bool Verbose,
                                 bool SortedSIL, bool DebugInfo) :
  OutStream(OS), Verbose(Verbose), SortedSIL(SortedSIL),
  DebugInfo(DebugInfo) { }

void SILPrintContext::setContext(const void *FunctionOrBlock) {
  if (FunctionOrBlock != ContextFunctionOrBlock) {
    BlocksToIDMap.clear();
    ValueToIDMap.clear();
    ContextFunctionOrBlock = FunctionOrBlock;
  }
}

SILPrintContext::~SILPrintContext() {
}

void SILPrintContext::printInstructionCallBack(const SILInstruction *I) {
}

void SILPrintContext::initBlockIDs(ArrayRef<const SILBasicBlock *> Blocks) {
  if (Blocks.empty())
    return;

  setContext(Blocks[0]->getParent());

  // Initialize IDs so our IDs are in RPOT as well. This is a hack.
  for (unsigned Index : indices(Blocks))
    BlocksToIDMap[Blocks[Index]] = Index;
}

ID SILPrintContext::getID(const SILBasicBlock *Block) {
  setContext(Block->getParent());

  // Lazily initialize the Blocks-to-IDs mapping.
  // If we are asked to emit sorted SIL, print out our BBs in RPOT order.
  if (BlocksToIDMap.empty()) {
    if (sortSIL()) {
      std::vector<SILBasicBlock *> RPOT;
      auto *UnsafeF = const_cast<SILFunction *>(Block->getParent());
      std::copy(po_begin(UnsafeF), po_end(UnsafeF), std::back_inserter(RPOT));
      std::reverse(RPOT.begin(), RPOT.end());
      // Initialize IDs so our IDs are in RPOT as well. This is a hack.
      for (unsigned Index : indices(RPOT))
        BlocksToIDMap[RPOT[Index]] = Index;
    } else {
      unsigned idx = 0;
      for (const SILBasicBlock &B : *Block->getParent())
        BlocksToIDMap[&B] = idx++;
    }
  }
  ID R = {ID::SILBasicBlock, BlocksToIDMap[Block]};
  return R;
}

ID SILPrintContext::getID(const SILNode *node) {
  if (isa<SILUndef>(node))
    return {ID::SILUndef, 0};
  
  SILBasicBlock *BB = node->getParentBlock();
  if (SILFunction *F = BB->getParent()) {
    setContext(F);
    // Lazily initialize the instruction -> ID mapping.
    if (ValueToIDMap.empty())
      F->numberValues(ValueToIDMap);
    ID R = {ID::SSAValue, ValueToIDMap[node]};
    return R;
  }

  setContext(BB);

  // Check if we have initialized our ValueToIDMap yet. If we have, just use
  // that.
  if (!ValueToIDMap.empty()) {
    ID R = {ID::SSAValue, ValueToIDMap[node]};
    return R;
  }

  // Otherwise, initialize the instruction -> ID mapping cache.
  unsigned idx = 0;
  for (auto &I : *BB) {
    // Give the instruction itself the next ID.
    ValueToIDMap[&I] = idx;

    // If there are no results, make sure we don't reuse that ID.
    auto results = I.getResults();
    if (results.empty()) {
      idx++;
      continue;
    }

    // Otherwise, assign all of the results an index.  Note that
    // we'll assign the same ID to both the instruction and the
    // first result.
    for (auto result : results) {
      ValueToIDMap[result] = idx++;
    }
  }

  ID R = {ID::SSAValue, ValueToIDMap[node]};
  return R;
}
