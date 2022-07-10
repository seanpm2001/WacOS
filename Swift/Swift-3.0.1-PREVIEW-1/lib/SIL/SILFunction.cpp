//===--- SILFunction.cpp - Defines the SILFunction data structure ---------===//
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

#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILBasicBlock.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/CFG.h"
// FIXME: For mapTypeInContext
#include "swift/AST/ArchetypeBuilder.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/GraphWriter.h"

using namespace swift;
using namespace Lowering;

SILSpecializeAttr::SILSpecializeAttr(ArrayRef<Substitution> subs)
  : numSubs(subs.size()) {
  std::copy(subs.begin(), subs.end(), getTrailingObjects<Substitution>());
}

SILSpecializeAttr *SILSpecializeAttr::create(SILModule &M,
                                             ArrayRef<Substitution> subs) {
  unsigned size =
    sizeof(SILSpecializeAttr) + (subs.size() * sizeof(Substitution));
  void *buf = M.allocate(size, alignof(SILSpecializeAttr));
  return ::new (buf) SILSpecializeAttr(subs);
}

SILFunction *SILFunction::create(SILModule &M, SILLinkage linkage,
                                 StringRef name,
                                 CanSILFunctionType loweredType,
                                 GenericParamList *contextGenericParams,
                                 Optional<SILLocation> loc,
                                 IsBare_t isBareSILFunction,
                                 IsTransparent_t isTrans,
                                 IsFragile_t isFragile,
                                 IsThunk_t isThunk,
                                 ClassVisibility_t classVisibility,
                                 Inline_t inlineStrategy, EffectsKind E,
                                 SILFunction *insertBefore,
                                 const SILDebugScope *debugScope,
                                 DeclContext *DC) {
  // Get a StringMapEntry for the function.  As a sop to error cases,
  // allow the name to have an empty string.
  llvm::StringMapEntry<SILFunction*> *entry = nullptr;
  if (!name.empty()) {
    entry = &*M.FunctionTable.insert(std::make_pair(name, nullptr)).first;
    assert(!entry->getValue() && "function already exists");
    name = entry->getKey();
  }

  auto fn = new (M) SILFunction(M, linkage, name,
                                loweredType, contextGenericParams, loc,
                                isBareSILFunction, isTrans, isFragile, isThunk,
                                classVisibility, inlineStrategy, E,
                                insertBefore, debugScope, DC);

  if (entry) entry->setValue(fn);
  return fn;
}

SILFunction::SILFunction(SILModule &Module, SILLinkage Linkage,
                         StringRef Name, CanSILFunctionType LoweredType,
                         GenericParamList *contextGenericParams,
                         Optional<SILLocation> Loc,
                         IsBare_t isBareSILFunction,
                         IsTransparent_t isTrans,
                         IsFragile_t isFragile,
                         IsThunk_t isThunk,
                         ClassVisibility_t classVisibility,
                         Inline_t inlineStrategy, EffectsKind E,
                         SILFunction *InsertBefore,
                         const SILDebugScope *DebugScope,
                         DeclContext *DC)
  : Module(Module),
    Name(Name),
    LoweredType(LoweredType),
    // FIXME: Context params should be independent of the function type.
    ContextGenericParams(contextGenericParams),
    DeclCtx(DC),
    DebugScope(DebugScope),
    Bare(isBareSILFunction),
    Transparent(isTrans),
    Fragile(isFragile),
    Thunk(isThunk),
    ClassVisibility(classVisibility),
    GlobalInitFlag(false),
    InlineStrategy(inlineStrategy),
    Linkage(unsigned(Linkage)),
    KeepAsPublic(false),
    EffectsKindAttr(E) {
  if (InsertBefore)
    Module.functions.insert(SILModule::iterator(InsertBefore), this);
  else
    Module.functions.push_back(this);

  Module.removeFromZombieList(Name);

  // Set our BB list to have this function as its parent. This enables us to
  // splice efficiently basic blocks in between functions.
  BlockList.Parent = this;
}

SILFunction::~SILFunction() {
  // If the function is recursive, a function_ref inst inside of the function
  // will give the function a non-zero ref count triggering the assertion. Thus
  // we drop all instruction references before we erase.
  // We also need to drop all references if instructions are allocated using
  // an allocator that may recycle freed memory.
  dropAllReferences();

  auto &M = getModule();
  for (auto &BB : *this) {
    for (auto I = BB.begin(), E = BB.end(); I != E;) {
      auto Inst = &*I;
      ++I;
      SILInstruction::destroy(Inst);
      // TODO: It is only safe to directly deallocate an
      // instruction if this BB is being removed in scope
      // of destructing a SILFunction.
      M.deallocateInst(Inst);
    }
    BB.InstList.clearAndLeakNodesUnsafely();
  }

  assert(RefCount == 0 &&
         "Function cannot be deleted while function_ref's still exist");
}

void SILFunction::setDeclContext(Decl *D) {
  if (!D)
    return;
  switch (D->getKind()) {
  // These four dual-inherit from DeclContext.
  case DeclKind::Func:        DeclCtx = cast<FuncDecl>(D); break;
  case DeclKind::Constructor: DeclCtx = cast<ConstructorDecl>(D); break;
  case DeclKind::Extension:   DeclCtx = cast<ExtensionDecl>(D);   break;
  case DeclKind::Destructor:  DeclCtx = cast<DestructorDecl>(D);  break;
  default:
    DeclCtx = D->getDeclContext();
  }
  assert(DeclCtx);
}

void SILFunction::setDeclContext(Expr *E) {
  DeclCtx = dyn_cast_or_null<AbstractClosureExpr>(E);
}

bool SILFunction::hasForeignBody() const {
  if (!hasClangNode()) return false;
  return SILDeclRef::isClangGenerated(getClangNode());
}

void SILFunction::numberValues(llvm::DenseMap<const ValueBase*,
                               unsigned> &ValueToNumberMap) const {
  unsigned idx = 0;
  for (auto &BB : *this) {
    for (auto I = BB.bbarg_begin(), E = BB.bbarg_end(); I != E; ++I)
      ValueToNumberMap[*I] = idx++;
    
    for (auto &I : BB)
      ValueToNumberMap[&I] = idx++;
  }
}


ASTContext &SILFunction::getASTContext() const {
  return getModule().getASTContext();
}

bool SILFunction::shouldOptimize() const {
  if (Module.getStage() == SILStage::Raw)
    return true;
  return !hasSemanticsAttr("optimize.sil.never");
}

Type SILFunction::mapTypeIntoContext(Type type) const {
  return ArchetypeBuilder::mapTypeIntoContext(getModule().getSwiftModule(),
                                              getContextGenericParams(),
                                              type);
}

namespace {
template<typename SubstFn>
struct SubstDependentSILType
  : CanTypeVisitor<SubstDependentSILType<SubstFn>, CanType>
{
  SILModule &M;
  SubstFn Subst;
  
  SubstDependentSILType(SILModule &M, SubstFn Subst)
    : M(M), Subst(std::move(Subst))
  {}
  
  using super = CanTypeVisitor<SubstDependentSILType<SubstFn>, CanType>;
  using super::visit;
  
  CanType visitDependentMemberType(CanDependentMemberType t) {
    // If a dependent member type appears in lowered position, we need to lower
    // its context substitution against the associated type's abstraction
    // pattern.
    CanType astTy = Subst(t);
    AbstractionPattern origTy(t->getAssocType()->getArchetype());
    
    return M.Types.getLoweredType(origTy, astTy)
      .getSwiftRValueType();
  }
  
  CanType visitTupleType(CanTupleType t) {
    // Dependent members can appear in lowered position inside tuples.
    
    SmallVector<TupleTypeElt, 4> elements;
    
    for (auto &elt : t->getElements())
      elements.push_back(elt.getWithType(visit(CanType(elt.getType()))));
    
    return TupleType::get(elements, t->getASTContext())
      ->getCanonicalType();
  }
  
  CanType visitSILFunctionType(CanSILFunctionType t) {
    // Dependent members can appear in lowered position inside SIL functions.
    
    SmallVector<SILParameterInfo, 4> params;
    for (auto &param : t->getParameters())
      params.push_back(param.map([&](CanType pt) -> CanType {
        return visit(pt);
      }));

    SmallVector<SILResultInfo, 4> results;
    for (auto &result : t->getAllResults())
      results.push_back(result.map([&](CanType pt) -> CanType {
        return visit(pt);
      }));
    
    Optional<SILResultInfo> errorResult;
    if (t->hasErrorResult()) {
      errorResult = t->getErrorResult().map([&](CanType elt) -> CanType {
          return visit(elt);
      });
    }
    
    return SILFunctionType::get(t->getGenericSignature(),
                                t->getExtInfo(),
                                t->getCalleeConvention(),
                                params, results, errorResult,
                                t->getASTContext());
  }
  
  CanType visitType(CanType t) {
    // Other types get substituted into context normally.
    return Subst(t);
  }
};

template<typename SubstFn>
SILType doSubstDependentSILType(SILModule &M,
                                SubstFn Subst,
                                SILType t) {
  CanType result = SubstDependentSILType<SubstFn>(M, std::move(Subst))
    .visit(t.getSwiftRValueType());
  return SILType::getPrimitiveType(result, t.getCategory());
}
  
} // end anonymous namespace

SILType SILFunction::mapTypeIntoContext(SILType type) const {
  return doSubstDependentSILType(getModule(),
    [&](CanType t) { return mapTypeIntoContext(t)->getCanonicalType(); },
    type);
}

SILType ArchetypeBuilder::substDependentType(SILModule &M, SILType type) {
  return doSubstDependentSILType(M,
    [&](CanType t) { return substDependentType(t)->getCanonicalType(); },
    type);
}

Type SILFunction::mapTypeOutOfContext(Type type) const {
  return ArchetypeBuilder::mapTypeOutOfContext(getModule().getSwiftModule(),
                                               getContextGenericParams(),
                                               type);
}

bool SILFunction::isNoReturnFunction() const {
  return SILType::getPrimitiveObjectType(getLoweredFunctionType())
      .isNoReturnFunction();
}

SILBasicBlock *SILFunction::createBasicBlock() {
  return new (getModule()) SILBasicBlock(this);
}

//===----------------------------------------------------------------------===//
//                          View CFG Implementation
//===----------------------------------------------------------------------===//

#ifndef NDEBUG

static llvm::cl::opt<unsigned>
MaxColumns("view-cfg-max-columns", llvm::cl::init(80),
           llvm::cl::desc("Maximum width of a printed node"));

namespace {
enum class LongLineBehavior { None, Truncate, Wrap };
} // end anonymous namespace
static llvm::cl::opt<LongLineBehavior>
LLBehavior("view-cfg-long-line-behavior",
           llvm::cl::init(LongLineBehavior::Truncate),
           llvm::cl::desc("Behavior when line width is greater than the "
                          "value provided my -view-cfg-max-columns "
                          "option"),
           llvm::cl::values(
               clEnumValN(LongLineBehavior::None, "none", "Print everything"),
               clEnumValN(LongLineBehavior::Truncate, "truncate",
                          "Truncate long lines"),
               clEnumValN(LongLineBehavior::Wrap, "wrap", "Wrap long lines"),
               clEnumValEnd));

static llvm::cl::opt<bool>
RemoveUseListComments("view-cfg-remove-use-list-comments",
                      llvm::cl::init(false),
                      llvm::cl::desc("Should use list comments be removed"));

template <typename InstTy, typename CaseValueTy>
inline CaseValueTy getCaseValueForBB(const InstTy *Inst,
                                     const SILBasicBlock *BB) {
  for (unsigned i = 0, e = Inst->getNumCases(); i != e; ++i) {
    auto P = Inst->getCase(i);
    if (P.second != BB)
      continue;
    return P.first;
  }
  llvm_unreachable("Error! should never pass in BB that is not a successor");
}

namespace llvm {
template <>
struct DOTGraphTraits<SILFunction *> : public DefaultDOTGraphTraits {

  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(const SILFunction *F) {
    return "CFG for '" + F->getName().str() + "' function";
  }

  static std::string getSimpleNodeLabel(const SILBasicBlock *Node,
                                        const SILFunction *F) {
    std::string OutStr;
    raw_string_ostream OSS(OutStr);
    const_cast<SILBasicBlock *>(Node)->printAsOperand(OSS, false);
    return OSS.str();
  }

  static std::string getCompleteNodeLabel(const SILBasicBlock *Node,
                                          const SILFunction *F) {
    std::string Str;
    raw_string_ostream OS(Str);

    OS << *Node;
    std::string OutStr = OS.str();
    if (OutStr[0] == '\n')
      OutStr.erase(OutStr.begin());

    // Process string output to make it nicer...
    unsigned ColNum = 0;
    unsigned LastSpace = 0;
    for (unsigned i = 0; i != OutStr.length(); ++i) {
      if (OutStr[i] == '\n') { // Left justify
        OutStr[i] = '\\';
        OutStr.insert(OutStr.begin() + i + 1, 'l');
        ColNum = 0;
        LastSpace = 0;
      } else if (RemoveUseListComments && OutStr[i] == '/' &&
                 i != (OutStr.size() - 1) && OutStr[i + 1] == '/') {
        unsigned Idx = OutStr.find('\n', i + 1); // Find end of line
        OutStr.erase(OutStr.begin() + i, OutStr.begin() + Idx);
        --i;

      } else if (ColNum == MaxColumns) { // Handle long lines.

        if (LLBehavior == LongLineBehavior::Wrap) {
          if (!LastSpace)
            LastSpace = i;
          OutStr.insert(LastSpace, "\\l...");
          ColNum = i - LastSpace;
          LastSpace = 0;
          i += 3; // The loop will advance 'i' again.
        } else if (LLBehavior == LongLineBehavior::Truncate) {
          unsigned Idx = OutStr.find('\n', i + 1); // Find end of line
          OutStr.erase(OutStr.begin() + i, OutStr.begin() + Idx);
          --i;
        }

        // Else keep trying to find a space.
      } else
        ++ColNum;
      if (OutStr[i] == ' ')
        LastSpace = i;
    }
    return OutStr;
  }

  std::string getNodeLabel(const SILBasicBlock *Node,
                           const SILFunction *Graph) {
    if (isSimple())
      return getSimpleNodeLabel(Node, Graph);
    else
      return getCompleteNodeLabel(Node, Graph);
  }

  static std::string getEdgeSourceLabel(const SILBasicBlock *Node,
                                        SILBasicBlock::const_succ_iterator I) {
    SILBasicBlock *Succ = I->getBB();
    const TermInst *Term = Node->getTerminator();

    // Label source of conditional branches with "T" or "F"
    if (auto *CBI = dyn_cast<CondBranchInst>(Term))
      return (Succ == CBI->getTrueBB()) ? "T" : "F";

    // Label source of switch edges with the associated value.
    if (auto *SI = dyn_cast<SwitchValueInst>(Term)) {
      if (SI->hasDefault() && SI->getDefaultBB() == Succ)
        return "def";

      std::string Str;
      raw_string_ostream OS(Str);

      SILValue I = getCaseValueForBB<SwitchValueInst, SILValue>(SI, Succ);
      OS << I; // TODO: or should we output the literal value of I?
      return OS.str();
    }

    if (auto *SEIB = dyn_cast<SwitchEnumInst>(Term)) {
      std::string Str;
      raw_string_ostream OS(Str);

      EnumElementDecl *E =
          getCaseValueForBB<SwitchEnumInst, EnumElementDecl *>(SEIB, Succ);
      OS << E->getFullName();
      return OS.str();
    }

    if (auto *SEIB = dyn_cast<SwitchEnumAddrInst>(Term)) {
      std::string Str;
      raw_string_ostream OS(Str);

      EnumElementDecl *E =
          getCaseValueForBB<SwitchEnumAddrInst, EnumElementDecl *>(SEIB, Succ);
      OS << E->getFullName();
      return OS.str();
    }

    if (auto *DMBI = dyn_cast<DynamicMethodBranchInst>(Term))
      return (Succ == DMBI->getHasMethodBB()) ? "T" : "F";

    if (auto *CCBI = dyn_cast<CheckedCastBranchInst>(Term))
      return (Succ == CCBI->getSuccessBB()) ? "T" : "F";

    if (auto *CCBI = dyn_cast<CheckedCastAddrBranchInst>(Term))
      return (Succ == CCBI->getSuccessBB()) ? "T" : "F";

    return "";
  }
};
} // end llvm namespace
#endif

#ifndef NDEBUG
static llvm::cl::opt<std::string>
TargetFunction("view-cfg-only-for-function", llvm::cl::init(""),
               llvm::cl::desc("Only print out the cfg for this function"));
#endif


void SILFunction::viewCFG() const {
/// When asserts are disabled, this should be a NoOp.
#ifndef NDEBUG
    // If we have a target function, only print that function out.
    if (!TargetFunction.empty() && !(getName().str() == TargetFunction))
      return;

  ViewGraph(const_cast<SILFunction *>(this), "cfg" + getName().str());
#endif
}

/// Returns true if this function has either a self metadata argument or
/// object from which Self metadata may be obtained.
bool SILFunction::hasSelfMetadataParam() const {
  auto paramTypes = getLoweredFunctionType()->getParameterSILTypes();
  if (paramTypes.empty())
    return false;

  auto silTy = paramTypes.back();
  if (!silTy.isClassOrClassMetatype())
    return false;

  auto metaTy = dyn_cast<MetatypeType>(silTy.getSwiftRValueType());
  (void)metaTy;
  assert(!metaTy || metaTy->getRepresentation() != MetatypeRepresentation::Thin
         && "Class metatypes are never thin.");
  return true;
}

bool SILFunction::hasName(const char *Name) const {
  return getName() == Name;
}

/// Returns true if this function can be referenced from a fragile function
/// body.
bool SILFunction::hasValidLinkageForFragileRef() const {
  // Fragile functions can reference 'static inline' functions imported
  // from C.
  if (hasForeignBody())
    return true;

  // If we can inline it, we can reference it.
  if (hasValidLinkageForFragileInline())
    return true;

  // Otherwise, only public functions can be referenced.
  return hasPublicVisibility(getLinkage());
}

/// Helper method which returns true if the linkage of the SILFunction
/// indicates that the objects definition might be required outside the
/// current SILModule.
bool
SILFunction::isPossiblyUsedExternally() const {
  return swift::isPossiblyUsedExternally(getLinkage(),
                                         getModule().isWholeModule());
}

bool SILFunction::isExternallyUsedSymbol() const {
  return swift::isPossiblyUsedExternally(getEffectiveSymbolLinkage(),
                                         getModule().isWholeModule());
}

void SILFunction::convertToDeclaration() {
  assert(isDefinition() && "Can only convert definitions to declarations");
  dropAllReferences();
  getBlocks().clear();
}

ArrayRef<Substitution> SILFunction::getForwardingSubstitutions() {
  auto *params = getContextGenericParams();
  if (!params)
    return {};
  return params->getForwardingSubstitutions(getASTContext());
}
