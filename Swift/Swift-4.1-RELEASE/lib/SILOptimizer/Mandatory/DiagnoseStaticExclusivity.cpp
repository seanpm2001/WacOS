//===--- DiagnoseStaticExclusivity.cpp - Find violations of exclusivity ---===//
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
// This file implements a diagnostic pass that finds violations of the
// "Law of Exclusivity" at compile time. The Law of Exclusivity requires
// that the access duration of any access to an address not overlap
// with an access to the same address unless both accesses are reads.
//
// This pass relies on 'begin_access' and 'end_access' SIL instruction
// markers inserted during SILGen to determine when an access to an address
// begins and ends. It models the in-progress accesses with a map from
// storage locations to the counts of read and write-like accesses in progress
// for that location.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "static-exclusivity"
#include "swift/AST/DiagnosticsSIL.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Expr.h"
#include "swift/AST/Stmt.h"
#include "swift/Basic/SourceLoc.h"
#include "swift/Parse/Lexer.h"
#include "swift/SIL/CFG.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/Projection.h"
#include "swift/SILOptimizer/Analysis/AccessSummaryAnalysis.h"
#include "swift/SILOptimizer/Analysis/PostOrderAnalysis.h"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Support/Debug.h"
using namespace swift;

template <typename... T, typename... U>
static InFlightDiagnostic diagnose(ASTContext &Context, SourceLoc loc,
                                   Diag<T...> diag, U &&... args) {
  return Context.Diags.diagnose(loc, diag, std::forward<U>(args)...);
}

namespace {

enum class AccessedStorageKind : unsigned {
  /// The access is to a location represented by a SIL value
  /// (for example, an 'alloc_box' instruction for a local variable).
  /// Two accesses accessing the exact same SILValue are considered to be
  /// accessing the same storage location.
  Value,

  /// The access is to a global variable.
  GlobalVar,

  /// The access is to a stored class property.
  ClassProperty
};

/// Represents the identity of a stored class property as a combination
/// of a base and a single projection. Eventually the goal is to make this
/// more precise and consider, casts, etc.
class ObjectProjection {
public:
  ObjectProjection(SILValue Object, const Projection &Proj)
      : Object(Object), Proj(Proj) {
    assert(Object->getType().isObject());
  }

  SILValue getObject() const { return Object; }
  const Projection &getProjection() const { return Proj; }

  bool operator==(const ObjectProjection &Other) const {
    return Object == Other.Object && Proj == Other.Proj;
  }

  bool operator!=(const ObjectProjection &Other) const {
    return Object != Other.Object || Proj != Other.Proj;
  }

private:
  SILValue Object;
  Projection Proj;
};

/// Represents the identity of a storage location being accessed.
/// This is used to determine when two 'begin_access' instructions
/// definitely access the same underlying location.
///
/// The key invariant that this class must maintain is that if it says
/// two storage locations are the same then they must be the same at run time.
/// It is allowed to err on the other side: it may imprecisely fail to
/// recognize that two storage locations that represent the same run-time
/// location are in fact the same.
class AccessedStorage {

private:
  AccessedStorageKind Kind;

  union {
    SILValue Value;
    SILGlobalVariable *Global;
    ObjectProjection ObjProj;
  };

public:
  AccessedStorage(SILValue V)
      : Kind(AccessedStorageKind::Value), Value(V) { }

  AccessedStorage(SILGlobalVariable *Global)
      : Kind(AccessedStorageKind::GlobalVar), Global(Global) {}

  AccessedStorage(AccessedStorageKind Kind,
                  const ObjectProjection &ObjProj)
      : Kind(Kind), ObjProj(ObjProj) {}

  AccessedStorageKind getKind() const { return Kind; }

  SILValue getValue() const {
    assert(Kind == AccessedStorageKind::Value);
    return Value;
  }

  SILGlobalVariable *getGlobal() const {
    assert(Kind == AccessedStorageKind::GlobalVar);
    return Global;
  }

  const ObjectProjection &getObjectProjection() const {
    assert(Kind == AccessedStorageKind::ClassProperty);
    return ObjProj;
  }

  /// Returns the ValueDecl for the underlying storage, if it can be
  /// determined. Otherwise returns null. For diagnostic purposes.
  const ValueDecl *getStorageDecl() const {
    switch(Kind) {
    case AccessedStorageKind::GlobalVar:
      return getGlobal()->getDecl();
    case AccessedStorageKind::Value:
      if (auto *Box = dyn_cast<AllocBoxInst>(getValue())) {
        return Box->getLoc().getAsASTNode<VarDecl>();
      }
      if (auto *Arg = dyn_cast<SILFunctionArgument>(getValue())) {
        return Arg->getDecl();
      }
      break;
    case AccessedStorageKind::ClassProperty: {
      const ObjectProjection &OP = getObjectProjection();
      const Projection &P = OP.getProjection();
      return P.getVarDecl(OP.getObject()->getType());
    }
    }
    return nullptr;
  }
};

enum class RecordedAccessKind {
  /// The access was for a 'begin_access' instruction in the current function
  /// being checked.
  BeginInstruction,

  /// The access was inside noescape closure that we either
  /// passed to function or called directly. It results from applying the
  /// the summary of the closure to the closure's captures.
  NoescapeClosureCapture
};

/// Records an access to an address and the single subpath of projections
/// that was performed on the address, if such a single subpath exists.
class RecordedAccess {
private:
  RecordedAccessKind RecordKind;
  union {
   BeginAccessInst *Inst;
    struct {
      SILAccessKind ClosureAccessKind;
      SILLocation ClosureAccessLoc;
    };
  };

  const IndexTrieNode *SubPath;
public:
  RecordedAccess(BeginAccessInst *BAI, const IndexTrieNode *SubPath) :
      RecordKind(RecordedAccessKind::BeginInstruction), Inst(BAI),
      SubPath(SubPath) { }

  RecordedAccess(SILAccessKind ClosureAccessKind,
                 SILLocation ClosureAccessLoc, const IndexTrieNode *SubPath) :
      RecordKind(RecordedAccessKind::NoescapeClosureCapture),
      ClosureAccessKind(ClosureAccessKind), ClosureAccessLoc(ClosureAccessLoc),
      SubPath(SubPath) { }

  RecordedAccessKind getRecordKind() const {
    return RecordKind;
  }

  BeginAccessInst *getInstruction() const {
    assert(RecordKind == RecordedAccessKind::BeginInstruction);
    return Inst;
  }

  SILAccessKind getAccessKind() const {
    switch (RecordKind) {
      case RecordedAccessKind::BeginInstruction:
        return Inst->getAccessKind();
      case RecordedAccessKind::NoescapeClosureCapture:
        return ClosureAccessKind;
    };
  }

  SILLocation getAccessLoc() const {
    switch (RecordKind) {
      case RecordedAccessKind::BeginInstruction:
        return Inst->getLoc();
      case RecordedAccessKind::NoescapeClosureCapture:
        return ClosureAccessLoc;
    };
  }

  const IndexTrieNode *getSubPath() const {
    return SubPath;
  }
};


/// Records the in-progress accesses to a given sub path.
class SubAccessInfo {
public:
  SubAccessInfo(const IndexTrieNode *P) : Path(P) {}

  const IndexTrieNode *Path;

  /// The number of in-progress 'read' accesses (that is 'begin_access [read]'
  /// instructions that have not yet had the corresponding 'end_access').
  unsigned Reads = 0;

  /// The number of in-progress write-like accesses.
  unsigned NonReads = 0;

  /// The instruction that began the first in-progress access to the storage
  /// location. Used for diagnostic purposes.
  Optional<RecordedAccess> FirstAccess = None;

public:
  /// Increment the count for given access.
  void beginAccess(BeginAccessInst *BAI, const IndexTrieNode *SubPath) {
    if (!FirstAccess) {
      assert(Reads == 0 && NonReads == 0);
      FirstAccess = RecordedAccess(BAI, SubPath);
    }

    if (BAI->getAccessKind() == SILAccessKind::Read)
      Reads++;
    else
      NonReads++;
  }

  /// Decrement the count for given access.
  void endAccess(EndAccessInst *EAI) {
    if (EAI->getBeginAccess()->getAccessKind() == SILAccessKind::Read)
      Reads--;
    else
      NonReads--;

    // If all open accesses are now ended, forget the location of the
    // first access.
    if (Reads == 0 && NonReads == 0)
      FirstAccess = None;
  }

  /// Returns true when there are any accesses to this location in progress.
  bool hasAccessesInProgress() const { return Reads > 0 || NonReads > 0; }

  /// Returns true when there must have already been a conflict diagnosed
  /// for an in-progress access. Used to suppress multiple diagnostics for
  /// the same underlying access violation.
  bool alreadyHadConflict() const {
    return (NonReads > 0 && Reads > 0) || (NonReads > 1);
  }

  // Returns true when beginning an access of the given Kind can
  // result in a conflict with a previous access.
  bool canConflictWithAccessOfKind(SILAccessKind Kind) const {
    if (Kind == SILAccessKind::Read) {
      // A read conflicts with any non-read accesses.
      return NonReads > 0;
    }

    // A non-read access conflicts with any other access.
    return NonReads > 0 || Reads > 0;
  }

  bool conflictsWithAccess(SILAccessKind Kind,
                           const IndexTrieNode *SubPath) const {
    if (!canConflictWithAccessOfKind(Kind))
      return false;

    return pathsConflict(Path, SubPath);
  }

  /// Returns true when the two subpaths access overlapping memory.
  bool pathsConflict(const IndexTrieNode *Path1,
                     const IndexTrieNode *Path2) const {
    return Path1->isPrefixOf(Path2) || Path2->isPrefixOf(Path1);
  }
};

/// Models the in-progress accesses for an address on which access has begun
/// with a begin_access instruction. For a given address, tracks the
/// count and kinds of accesses as well as the subpaths (i.e., projections) that
/// were accessed.
class AccessInfo {
  using SubAccessVector = SmallVector<SubAccessInfo, 4>;

  SubAccessVector SubAccesses;

  /// Returns the SubAccess info for accessing at the given SubPath.
  SubAccessInfo &findOrCreateSubAccessInfo(const IndexTrieNode *SubPath) {
    for (auto &Info : SubAccesses) {
      if (Info.Path == SubPath)
        return Info;
    }

    SubAccesses.emplace_back(SubPath);
    return SubAccesses.back();
  }

  SubAccessVector::const_iterator
  findFirstSubPathWithConflict(SILAccessKind OtherKind,
                               const IndexTrieNode *OtherSubPath) const {
    // Note this iteration requires deterministic ordering for repeatable
    // diagnostics.
    for (auto I = SubAccesses.begin(), E = SubAccesses.end(); I != E; ++I) {
      const SubAccessInfo &Access = *I;
      if (Access.conflictsWithAccess(OtherKind, OtherSubPath))
        return I;
    }

    return SubAccesses.end();
  }

public:
  // Returns the previous access when beginning an access of the given Kind will
  // result in a conflict with a previous access.
  Optional<RecordedAccess>
  conflictsWithAccess(SILAccessKind Kind, const IndexTrieNode *SubPath) const {
    auto I = findFirstSubPathWithConflict(Kind, SubPath);
    if (I == SubAccesses.end())
      return None;

    return I->FirstAccess;
  }

  /// Returns true if any subpath of has already had a conflict.
  bool alreadyHadConflict() const {
    for (const auto &SubAccess : SubAccesses) {
      if (SubAccess.alreadyHadConflict())
        return true;
    }
    return false;
  }

  /// Returns true when there are any accesses to this location in progress.
  bool hasAccessesInProgress() const {
    for (const auto &SubAccess : SubAccesses) {
      if (SubAccess.hasAccessesInProgress())
        return true;
    }
    return false;
  }

  /// Increment the count for given access.
  void beginAccess(BeginAccessInst *BAI, const IndexTrieNode *SubPath) {
    SubAccessInfo &SubAccess = findOrCreateSubAccessInfo(SubPath);
    SubAccess.beginAccess(BAI, SubPath);
  }

  /// Decrement the count for given access.
  void endAccess(EndAccessInst *EAI, const IndexTrieNode *SubPath) {
    SubAccessInfo &SubAccess = findOrCreateSubAccessInfo(SubPath);
    SubAccess.endAccess(EAI);
  }
};

/// Indicates whether a 'begin_access' requires exclusive access
/// or allows shared access. This needs to be kept in sync with
/// diag::exclusivity_access_required, exclusivity_access_required_swift3,
/// and diag::exclusivity_conflicting_access.
enum class ExclusiveOrShared_t : unsigned {
  ExclusiveAccess = 0,
  SharedAccess = 1
};


/// Tracks the in-progress accesses on per-storage-location basis.
using StorageMap = llvm::SmallDenseMap<AccessedStorage, AccessInfo, 4>;

/// Represents two accesses that conflict and their underlying storage.
struct ConflictingAccess {
private:

  /// If true, always diagnose this conflict as a warning. This is useful for
  /// staging in fixes for false negatives without affecting source
  /// compatibility.
  bool AlwaysDiagnoseAsWarning = false;
public:
  /// Create a conflict for two begin_access instructions in the same function.
  ConflictingAccess(const AccessedStorage &Storage, const RecordedAccess &First,
                    const RecordedAccess &Second)
      : Storage(Storage), FirstAccess(First), SecondAccess(Second) {}

  const AccessedStorage Storage;
  const RecordedAccess FirstAccess;
  const RecordedAccess SecondAccess;

  bool getAlwaysDiagnoseAsWarning() const { return AlwaysDiagnoseAsWarning; }
  void setAlwaysDiagnoseAsWarning(bool AlwaysDiagnoseAsWarning) {
    this->AlwaysDiagnoseAsWarning = AlwaysDiagnoseAsWarning;
  }
};

} // end anonymous namespace

namespace llvm {
/// Enable using AccessedStorage as a key in DenseMap.
template <> struct DenseMapInfo<AccessedStorage> {
  static AccessedStorage getEmptyKey() {
    return AccessedStorage(swift::SILValue::getFromOpaqueValue(
        llvm::DenseMapInfo<void *>::getEmptyKey()));
  }

  static AccessedStorage getTombstoneKey() {
    return AccessedStorage(swift::SILValue::getFromOpaqueValue(
        llvm::DenseMapInfo<void *>::getTombstoneKey()));
  }

  static unsigned getHashValue(AccessedStorage Storage) {
    switch (Storage.getKind()) {
    case AccessedStorageKind::Value:
      return DenseMapInfo<swift::SILValue>::getHashValue(Storage.getValue());
    case AccessedStorageKind::GlobalVar:
      return DenseMapInfo<void *>::getHashValue(Storage.getGlobal());
    case AccessedStorageKind::ClassProperty: {
      const ObjectProjection &P = Storage.getObjectProjection();
      return llvm::hash_combine(P.getObject(), P.getProjection());
    }
    }
    llvm_unreachable("Unhandled AccessedStorageKind");
  }

  static bool isEqual(AccessedStorage LHS, AccessedStorage RHS) {
    if (LHS.getKind() != RHS.getKind())
      return false;

    switch (LHS.getKind()) {
    case AccessedStorageKind::Value:
      return LHS.getValue() == RHS.getValue();
    case AccessedStorageKind::GlobalVar:
      return LHS.getGlobal() == RHS.getGlobal();
    case AccessedStorageKind::ClassProperty:
        return LHS.getObjectProjection() == RHS.getObjectProjection();
    }
    llvm_unreachable("Unhandled AccessedStorageKind");
  }
};

} // end namespace llvm

/// Returns whether an access of the given kind requires exclusive or shared
/// access to its storage.
static ExclusiveOrShared_t getRequiredAccess(SILAccessKind Kind) {
  if (Kind == SILAccessKind::Read)
    return ExclusiveOrShared_t::SharedAccess;

  return ExclusiveOrShared_t::ExclusiveAccess;
}

/// Extract the text for the given expression.
static StringRef extractExprText(const Expr *E, SourceManager &SM) {
  const auto CSR = Lexer::getCharSourceRangeFromSourceRange(SM,
      E->getSourceRange());
  return SM.extractText(CSR);
}

/// Returns true when the call expression is a call to swap() in the Standard
/// Library.
/// This is a helper function that is only used in an assertion, which is why
/// it is in the ifndef.
#ifndef NDEBUG
static bool isCallToStandardLibrarySwap(CallExpr *CE, ASTContext &Ctx) {
  if (CE->getCalledValue() == Ctx.getSwap(nullptr))
    return true;

  // Is the call module qualified, i.e. Swift.swap(&a[i], &[j)?
  if (auto *DSBIE = dyn_cast<DotSyntaxBaseIgnoredExpr>(CE->getFn())) {
    if (auto *DRE = dyn_cast<DeclRefExpr>(DSBIE->getRHS())) {
      return DRE->getDecl() == Ctx.getSwap(nullptr);
    }
  }

  return false;
}
#endif

/// Do a syntactic pattern match to determine whether the call is a call
/// to swap(&base[index1], &base[index2]), which can
/// be replaced with a call to MutableCollection.swapAt(_:_:) on base.
///
/// Returns true if the call can be replaced. Returns the call expression,
/// the base expression, and the two indices as out expressions.
///
/// This method takes an array of all the ApplyInsts for calls to swap()
/// in the function to avoid needing to construct a parent map over the AST
/// to find the CallExpr for the inout accesses.
static bool
canReplaceWithCallToCollectionSwapAt(const BeginAccessInst *Access1,
                                     const BeginAccessInst *Access2,
                                     ArrayRef<ApplyInst *> CallsToSwap,
                                     ASTContext &Ctx,
                                     CallExpr *&FoundCall,
                                     Expr *&Base,
                                     Expr *&Index1,
                                     Expr *&Index2) {
  if (CallsToSwap.empty())
    return false;

  // Inout arguments must be modifications.
  if (Access1->getAccessKind() != SILAccessKind::Modify ||
      Access2->getAccessKind() != SILAccessKind::Modify) {
    return false;
  }

  SILLocation Loc1 = Access1->getLoc();
  SILLocation Loc2 = Access2->getLoc();
  if (Loc1.isNull() || Loc2.isNull())
    return false;

  auto *InOut1 = Loc1.getAsASTNode<InOutExpr>();
  auto *InOut2 = Loc2.getAsASTNode<InOutExpr>();
  if (!InOut1 || !InOut2)
    return false;

  FoundCall = nullptr;
  // Look through all the calls to swap() recorded in the function to find
  // which one we're diagnosing.
  for (ApplyInst *AI : CallsToSwap) {
    SILLocation CallLoc = AI->getLoc();
    if (CallLoc.isNull())
      continue;

    auto *CE = CallLoc.getAsASTNode<CallExpr>();
    if (!CE)
      continue;

    assert(isCallToStandardLibrarySwap(CE, Ctx));
    // swap() takes two arguments.
    auto *ArgTuple = cast<TupleExpr>(CE->getArg());
    const Expr *Arg1 = ArgTuple->getElement(0);
    const Expr *Arg2 = ArgTuple->getElement(1);
    if ((Arg1 == InOut1 && Arg2 == InOut2)) {
        FoundCall = CE;
      break;
    }
  }
  if (!FoundCall)
    return false;

  // We found a call to swap(&e1, &e2). Now check to see whether it
  // matches the form swap(&someCollection[index1], &someCollection[index2]).
  auto *SE1 = dyn_cast<SubscriptExpr>(InOut1->getSubExpr());
  if (!SE1)
    return false;
  auto *SE2 = dyn_cast<SubscriptExpr>(InOut2->getSubExpr());
  if (!SE2)
    return false;

  // Do the two subscripts refer to the same subscript declaration?
  auto *Decl1 = cast<SubscriptDecl>(SE1->getDecl().getDecl());
  auto *Decl2 = cast<SubscriptDecl>(SE2->getDecl().getDecl());
  if (Decl1 != Decl2)
    return false;

  ProtocolDecl *MutableCollectionDecl = Ctx.getMutableCollectionDecl();

  // Is the subcript either (1) on MutableCollection itself or (2) a
  // a witness for a subscript on MutableCollection?
  bool IsSubscriptOnMutableCollection = false;
  ProtocolDecl *ProtocolForDecl =
      Decl1->getDeclContext()->getAsProtocolOrProtocolExtensionContext();
  if (ProtocolForDecl) {
    IsSubscriptOnMutableCollection = (ProtocolForDecl == MutableCollectionDecl);
  } else {
    for (ValueDecl *Req : Decl1->getSatisfiedProtocolRequirements()) {
      DeclContext *ReqDC = Req->getDeclContext();
      ProtocolDecl *ReqProto = ReqDC->getAsProtocolOrProtocolExtensionContext();
      assert(ReqProto && "Protocol requirement not in a protocol?");

      if (ReqProto == MutableCollectionDecl) {
        IsSubscriptOnMutableCollection = true;
        break;
      }
    }
  }

  if (!IsSubscriptOnMutableCollection)
    return false;

  // We're swapping two subscripts on mutable collections -- but are they
  // the same collection? Approximate this by checking for textual
  // equality on the base expressions. This is just an approximation,
  // but is fine for a best-effort Fix-It.
  SourceManager &SM = Ctx.SourceMgr;
  StringRef Base1Text = extractExprText(SE1->getBase(), SM);
  StringRef Base2Text = extractExprText(SE2->getBase(), SM);

  if (Base1Text != Base2Text)
    return false;

  auto *Index1Paren = dyn_cast<ParenExpr>(SE1->getIndex());
  if (!Index1Paren)
    return false;

  auto *Index2Paren = dyn_cast<ParenExpr>(SE2->getIndex());
  if (!Index2Paren)
    return false;

  Base = SE1->getBase();
  Index1 = Index1Paren->getSubExpr();
  Index2 = Index2Paren->getSubExpr();
  return true;
}

/// Suggest replacing with call with a call to swapAt().
static void addSwapAtFixit(InFlightDiagnostic &Diag, CallExpr *&FoundCall,
                           Expr *Base, Expr *&Index1, Expr *&Index2,
                           SourceManager &SM) {
  StringRef BaseText = extractExprText(Base, SM);
  StringRef Index1Text = extractExprText(Index1, SM);
  StringRef Index2Text = extractExprText(Index2, SM);
  SmallString<64> FixItText;
  {
    llvm::raw_svector_ostream Out(FixItText);
    Out << BaseText << ".swapAt(" << Index1Text << ", " << Index2Text << ")";
  }

  Diag.fixItReplace(FoundCall->getSourceRange(), FixItText);
}

/// Returns a string representation of the BaseName and the SubPath
/// suitable for use in diagnostic text. Only supports the Projections
/// that stored-property relaxation supports: struct stored properties
/// and tuple elements.
static std::string getPathDescription(DeclName BaseName, SILType BaseType,
                                      const IndexTrieNode *SubPath,
                                      SILModule &M) {
  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);

  os << "'" << BaseName;
  os << AccessSummaryAnalysis::getSubPathDescription(BaseType, SubPath, M);
  os << "'";

  return os.str();
}

/// Emits a diagnostic if beginning an access with the given in-progress
/// accesses violates the law of exclusivity. Returns true when a
/// diagnostic was emitted.
static void diagnoseExclusivityViolation(const ConflictingAccess &Violation,
                                         ArrayRef<ApplyInst *> CallsToSwap,
                                         ASTContext &Ctx) {

  const AccessedStorage &Storage = Violation.Storage;
  const RecordedAccess &FirstAccess = Violation.FirstAccess;
  const RecordedAccess &SecondAccess = Violation.SecondAccess;

  DEBUG(llvm::dbgs() << "Conflict on " << *FirstAccess.getInstruction()
                     << "\n  vs " << *SecondAccess.getInstruction()
                     << "\n  in function "
                     << *FirstAccess.getInstruction()->getFunction());

  // Can't have a conflict if both accesses are reads.
  assert(!(FirstAccess.getAccessKind() == SILAccessKind::Read &&
           SecondAccess.getAccessKind() == SILAccessKind::Read));

  ExclusiveOrShared_t FirstRequires =
      getRequiredAccess(FirstAccess.getAccessKind());

  // Diagnose on the first access that requires exclusivity.
  bool FirstIsMain = (FirstRequires == ExclusiveOrShared_t::ExclusiveAccess);
  const RecordedAccess &MainAccess = (FirstIsMain ? FirstAccess : SecondAccess);
  const RecordedAccess &NoteAccess = (FirstIsMain ? SecondAccess : FirstAccess);

  SourceRange RangeForMain = MainAccess.getAccessLoc().getSourceRange();
  unsigned AccessKindForMain =
      static_cast<unsigned>(MainAccess.getAccessKind());

  // For now, all exclusivity violations are warning in Swift 3 mode.
  // Also treat some violations as warnings to allow them to be staged in.
  bool DiagnoseAsWarning = Violation.getAlwaysDiagnoseAsWarning() ||
      Ctx.LangOpts.isSwiftVersion3();

  if (const ValueDecl *VD = Storage.getStorageDecl()) {
    // We have a declaration, so mention the identifier in the diagnostic.
    auto DiagnosticID = (DiagnoseAsWarning ?
                         diag::exclusivity_access_required_warn :
                         diag::exclusivity_access_required);
    SILType BaseType = FirstAccess.getInstruction()->getType().getAddressType();
    SILModule &M = FirstAccess.getInstruction()->getModule();
    std::string PathDescription = getPathDescription(
        VD->getBaseName(), BaseType, MainAccess.getSubPath(), M);

    // Determine whether we can safely suggest replacing the violation with
    // a call to MutableCollection.swapAt().
    bool SuggestSwapAt = false;
    CallExpr *CallToReplace = nullptr;
    Expr *Base = nullptr;
    Expr *SwapIndex1 = nullptr;
    Expr *SwapIndex2 = nullptr;
    if (SecondAccess.getRecordKind() == RecordedAccessKind::BeginInstruction) {
        SuggestSwapAt = canReplaceWithCallToCollectionSwapAt(
            FirstAccess.getInstruction(), SecondAccess.getInstruction(),
            CallsToSwap, Ctx, CallToReplace, Base, SwapIndex1, SwapIndex2);
    }

    auto D =
        diagnose(Ctx, MainAccess.getAccessLoc().getSourceLoc(), DiagnosticID,
                 PathDescription, AccessKindForMain, SuggestSwapAt);
    D.highlight(RangeForMain);
    if (SuggestSwapAt)
      addSwapAtFixit(D, CallToReplace, Base, SwapIndex1, SwapIndex2,
                     Ctx.SourceMgr);
  } else {
    auto DiagnosticID = (DiagnoseAsWarning ?
                         diag::exclusivity_access_required_unknown_decl_warn :
                         diag::exclusivity_access_required_unknown_decl);
    diagnose(Ctx, MainAccess.getAccessLoc().getSourceLoc(), DiagnosticID,
             AccessKindForMain)
        .highlight(RangeForMain);
  }
  diagnose(Ctx, NoteAccess.getAccessLoc().getSourceLoc(),
           diag::exclusivity_conflicting_access)
      .highlight(NoteAccess.getAccessLoc().getSourceRange());
}

/// Make a best effort to find the underlying object for the purpose
/// of identifying the base of a 'ref_element_addr'.
static SILValue findUnderlyingObject(SILValue Value) {
  assert(Value->getType().isObject());
  SILValue Iter = Value;

  while (true) {
    // For now just look through begin_borrow instructions; we can likely
    // make this more precise in the future.
    if (auto *BBI = dyn_cast<BeginBorrowInst>(Iter)) {
      Iter = BBI->getOperand();
      continue;
    }
    break;
  }

  assert(Iter->getType().isObject());
  return Iter;
}

/// Look through a value to find the underlying storage accessed.
static AccessedStorage findAccessedStorage(SILValue Source) {
  SILValue Iter = Source;
  while (true) {
    // Base case for globals: make sure ultimate source is recognized.
    if (auto *GAI = dyn_cast<GlobalAddrInst>(Iter)) {
      return AccessedStorage(GAI->getReferencedGlobal());
    }

    // Base case for class objects.
    if (auto *REA = dyn_cast<RefElementAddrInst>(Iter)) {
      // Do a best-effort to find the identity of the object being projected
      // from. It is OK to be unsound here (i.e. miss when two ref_element_addrs
      // actually refer the same address) because these will be dynamically
      // checked.
      SILValue Object = findUnderlyingObject(REA->getOperand());
      const ObjectProjection &OP = ObjectProjection(Object,
                                                    Projection(REA));
      return AccessedStorage(AccessedStorageKind::ClassProperty, OP);
    }

    switch (Iter->getKind()) {
    // Inductive cases: look through operand to find ultimate source.
    case ValueKind::ProjectBoxInst:
    case ValueKind::CopyValueInst:
    case ValueKind::MarkUninitializedInst:
    case ValueKind::UncheckedAddrCastInst:
    // Inlined access to subobjects.
    case ValueKind::StructElementAddrInst:
    case ValueKind::TupleElementAddrInst:
    case ValueKind::UncheckedTakeEnumDataAddrInst:
    case ValueKind::RefTailAddrInst:
    case ValueKind::TailAddrInst:
    case ValueKind::IndexAddrInst:
      Iter = cast<SingleValueInstruction>(Iter)->getOperand(0);
      continue;

    // Base address producers.
    case ValueKind::AllocBoxInst:
      // An AllocBox is a fully identified memory location.
    case ValueKind::AllocStackInst:
      // An AllocStack is a fully identified memory location, which may occur
      // after inlining code already subjected to stack promotion.
    case ValueKind::BeginAccessInst:
      // The current access is nested within another access.
      // View the outer access as a separate location because nested accesses do
      // not conflict with each other.
    case ValueKind::SILFunctionArgument:
      // A function argument is effectively a nested access, enforced
      // independently in the caller and callee.
    case ValueKind::PointerToAddressInst:
      // An addressor provides access to a global or class property via a
      // RawPointer. Calling the addressor casts that raw pointer to an address.
      return AccessedStorage(Iter);

    // Unsupported address producers.
    // Initialization is always local.
    case ValueKind::InitEnumDataAddrInst:
    case ValueKind::InitExistentialAddrInst:
    // Accessing an existential value requires a cast.
    case ValueKind::OpenExistentialAddrInst:
    default:
      DEBUG(llvm::dbgs() << "Bad memory access source: " << Iter);
      llvm_unreachable("Unexpected access source.");
    }
  }
}

/// Returns true when the apply calls the Standard Library swap().
/// Used for fix-its to suggest replacing with Collection.swapAt()
/// on exclusivity violations.
static bool isCallToStandardLibrarySwap(ApplyInst *AI, ASTContext &Ctx) {
  SILFunction *SF = AI->getReferencedFunction();
  if (!SF)
    return false;

  if (!SF->hasLocation())
    return false;

  auto *FD = SF->getLocation().getAsASTNode<FuncDecl>();
  if (!FD)
    return false;

  return FD == Ctx.getSwap(nullptr);
}

/// If making an access of the given kind at the given subpath would
/// would conflict, returns the first recorded access it would conflict
/// with. Otherwise, returns None.
static Optional<RecordedAccess>
shouldReportAccess(const AccessInfo &Info,swift::SILAccessKind Kind,
                   const IndexTrieNode *SubPath) {
  if (Info.alreadyHadConflict())
    return None;

  return Info.conflictsWithAccess(Kind, SubPath);
}

/// For each projection that the summarized function accesses on its
/// capture, check whether the access conflicts with already-in-progress
/// access. Returns the most general summarized conflict -- so if there are
/// two conflicts in the called function and one is for an access to an
/// aggregate and another is for an access to a projection from the aggregate,
/// this will return the conflict for the aggregate. This approach guarantees
/// determinism and makes it more  likely that we'll diagnose the most helpful
/// conflict.
static Optional<ConflictingAccess>
findConflictingArgumentAccess(const AccessSummaryAnalysis::ArgumentSummary &AS,
                              const AccessedStorage &AccessedStorage,
                              const AccessInfo &InProgressInfo) {
  Optional<RecordedAccess> BestInProgressAccess;
  Optional<RecordedAccess> BestArgAccess;

  for (const auto &MapPair : AS.getSubAccesses()) {
    const IndexTrieNode *SubPath = MapPair.getFirst();
    const auto &SubAccess = MapPair.getSecond();
    SILAccessKind Kind = SubAccess.getAccessKind();
    auto InProgressAccess = shouldReportAccess(InProgressInfo, Kind, SubPath);
    if (!InProgressAccess)
      continue;

    if (!BestArgAccess ||
        AccessSummaryAnalysis::compareSubPaths(SubPath,
                                               BestArgAccess->getSubPath())) {
        SILLocation AccessLoc = SubAccess.getAccessLoc();

        BestArgAccess = RecordedAccess(Kind, AccessLoc, SubPath);
        BestInProgressAccess = InProgressAccess;
    }
  }

  if (!BestArgAccess)
    return None;

  return ConflictingAccess(AccessedStorage, *BestInProgressAccess,
                           *BestArgAccess);
}

/// Use the summary analysis to check whether a call to the given
/// function would conflict with any in progress accesses. The starting
/// index indicates what index into the the callee's parameters the
/// arguments array starts at -- this is useful for partial_apply functions,
/// which pass only a suffix of the callee's arguments at the apply site.
static void checkForViolationWithCall(
    const StorageMap &Accesses, SILFunction *Callee, unsigned StartingAtIndex,
    OperandValueArrayRef Arguments, AccessSummaryAnalysis *ASA,
    bool DiagnoseAsWarning,
    llvm::SmallVectorImpl<ConflictingAccess> &ConflictingAccesses) {
  const AccessSummaryAnalysis::FunctionSummary &FS =
      ASA->getOrCreateSummary(Callee);

  // For each argument in the suffix of the callee arguments being passed
  // at this call site, determine whether the arguments will be accessed
  // in a way that conflicts with any currently in progress accesses.
  // If so, diagnose.
  for (unsigned ArgumentIndex : indices(Arguments)) {
    unsigned CalleeIndex = StartingAtIndex + ArgumentIndex;

    const AccessSummaryAnalysis::ArgumentSummary &AS =
        FS.getAccessForArgument(CalleeIndex);

    const auto &SubAccesses = AS.getSubAccesses();

    // Is the capture accessed in the callee?
    if (SubAccesses.size() == 0)
      continue;

    SILValue Argument = Arguments[ArgumentIndex];
    assert(Argument->getType().isAddress());

    const AccessedStorage &Storage = findAccessedStorage(Argument);
    auto AccessIt = Accesses.find(Storage);

    // Are there any accesses in progress at the time of the call?
    if (AccessIt == Accesses.end())
      continue;

    const AccessInfo &Info = AccessIt->getSecond();
    if (auto Conflict = findConflictingArgumentAccess(AS, Storage, Info)) {
      Conflict->setAlwaysDiagnoseAsWarning(DiagnoseAsWarning);
      ConflictingAccesses.push_back(*Conflict);
    }
  }
}

/// Given a block used as a noescape function argument, attempt to find
/// the Swift closure that invoking the block will call.
static SILValue findClosureStoredIntoBlock(SILValue V) {
  auto FnType = V->getType().castTo<SILFunctionType>();
  assert(FnType->getRepresentation() == SILFunctionTypeRepresentation::Block);

  // Given a no escape block argument to a function,
  // pattern match to find the noescape closure that invoking the block
  // will call:
  //     %noescape_closure = ...
  //     %storage = alloc_stack
  //     %storage_address = project_block_storage %storage
  //     store %noescape_closure to [init] %storage_address
  //     %block = init_block_storage_header %storage invoke %thunk
  //     %arg = copy_block %block

  InitBlockStorageHeaderInst *IBSHI = nullptr;

  // Look through block copies to find the initialization of block storage.
  while (true) {
    if (auto *CBI = dyn_cast<CopyBlockInst>(V)) {
      V = CBI->getOperand();
      continue;
    }

    IBSHI = dyn_cast<InitBlockStorageHeaderInst>(V);
    break;
  }

  if (!IBSHI)
    return nullptr;

  SILValue BlockStorage = IBSHI->getBlockStorage();
  auto *PBSI = BlockStorage->getSingleUserOfType<ProjectBlockStorageInst>();
  assert(PBSI && "Couldn't find block storage projection");

  auto *SI = PBSI->getSingleUserOfType<StoreInst>();
  assert(SI && "Couldn't find single store of function into block storage");

  return SI->getSrc();
}

/// Look through a value passed as a function argument to determine whether
/// it is a partial_apply.
static PartialApplyInst *lookThroughForPartialApply(SILValue V) {
  auto ArgumentFnType = V->getType().castTo<SILFunctionType>();
  assert(ArgumentFnType->isNoEscape());

  if (ArgumentFnType->getRepresentation() ==
        SILFunctionTypeRepresentation::Block) {
    V = findClosureStoredIntoBlock(V);

    if (!V)
      return nullptr;
  }

  while (true) {
    if (auto CFI = dyn_cast<ConvertFunctionInst>(V)) {
      V = CFI->getOperand();
      continue;
    }

    if (auto *PAI = dyn_cast<PartialApplyInst>(V))
      return PAI;

    return nullptr;
  }
}

/// Checks whether any of the arguments to the apply are closures and diagnoses
/// if any of the @inout_aliasable captures passed to those closures have
/// in-progress accesses that would conflict with any access the summary
/// says the closure would perform.
static void checkForViolationsInNoEscapeClosureArguments(
    const StorageMap &Accesses, ApplySite AS, AccessSummaryAnalysis *ASA,
    llvm::SmallVectorImpl<ConflictingAccess> &ConflictingAccesses,
    bool DiagnoseAsWarning) {

  // Check for violation with closures passed as arguments
  for (SILValue Argument : AS.getArguments()) {
    auto ArgumentFnType = Argument->getType().getAs<SILFunctionType>();
    if (!ArgumentFnType)
      continue;

    if (!ArgumentFnType->isNoEscape())
      continue;

    auto *PAI = lookThroughForPartialApply(Argument);
    if (!PAI)
      continue;

    SILFunction *Callee = PAI->getCalleeFunction();
    if (!Callee)
      continue;

    if (Callee->isThunk() == IsReabstractionThunk) {
      // For source compatibility reasons, treat conflicts found by
      // looking through reabstraction thunks as warnings. A future compiler
      // will upgrade these to errors;
      bool WarnOnThunkConflict = true;
      // Recursively check any arguments to the partial apply that are
      // themselves noescape closures. This detects violations when a noescape
      // closure is captured by a reabstraction thunk which is itself then passed
      // to an apply.
      checkForViolationsInNoEscapeClosureArguments(Accesses, PAI, ASA,
                                                   ConflictingAccesses,
                                                   WarnOnThunkConflict);
      continue;
    }
    // The callee is not a reabstraction thunk, so check its captures directly.

    if (Callee->empty())
      continue;


    // For source compatibility reasons, treat conflicts found by
    // looking through noescape blocks as warnings. A future compiler
    // will upgrade these to errors.
    bool ArgumentIsBlock = (ArgumentFnType->getRepresentation() ==
                            SILFunctionTypeRepresentation::Block);

    // Check the closure's captures, which are a suffix of the closure's
    // parameters.
    unsigned StartIndex =
        Callee->getArguments().size() - PAI->getNumArguments();
    checkForViolationWithCall(Accesses, Callee, StartIndex,
                              PAI->getArguments(), ASA,
                              DiagnoseAsWarning || ArgumentIsBlock,
                              ConflictingAccesses);
  }
}

/// Given a full apply site, diagnose if the apply either calls a closure
/// directly that conflicts with an in-progress access or takes a noescape
/// argument that, when called, would conflict with an in-progress access.
static void checkForViolationsInNoEscapeClosures(
    const StorageMap &Accesses, FullApplySite FAS, AccessSummaryAnalysis *ASA,
    llvm::SmallVectorImpl<ConflictingAccess> &ConflictingAccesses) {
  // Check to make sure that calling a closure immediately will not result in
  // a conflict. This diagnoses in cases where there is a conflict between an
  // argument passed inout to the closure and an access inside the closure to a
  // captured variable:
  //
  //  var i = 7
  //  ({ (p: inout Int) in i = 8})(&i) // Overlapping access to 'i'
  //
  SILFunction *Callee = FAS.getCalleeFunction();
  if (Callee && !Callee->empty()) {
    // Check for violation with directly called closure
    checkForViolationWithCall(Accesses, Callee, 0, FAS.getArguments(), ASA,
                              /*DiagnoseAsWarning=*/false, ConflictingAccesses);
  }

  // Check to make sure that any arguments to the apply are not themselves
  // noescape closures that -- when called -- might conflict with an in-progress
  // access. For example, this will diagnose on the following:
  //
  // var i = 7
  // takesInoutAndClosure(&i) { i = 8 } // Overlapping access to 'i'
  //
  checkForViolationsInNoEscapeClosureArguments(Accesses, FAS, ASA,
                                               ConflictingAccesses,
                                               /*DiagnoseAsWarning=*/false);
}

static void checkStaticExclusivity(SILFunction &Fn, PostOrderFunctionInfo *PO,
                                   AccessSummaryAnalysis *ASA) {
  // The implementation relies on the following SIL invariants:
  //    - All incoming edges to a block must have the same in-progress
  //      accesses. This enables the analysis to not perform a data flow merge
  //      on incoming edges.
  //    - Further, for a given address each of the in-progress
  //      accesses must have begun in the same order on all edges. This ensures
  //      consistent diagnostics across changes to the exploration of the CFG.
  //    - On return from a function there are no in-progress accesses. This
  //      enables a sanity check for lean analysis state at function exit.
  //    - Each end_access instruction corresponds to exactly one begin access
  //      instruction. (This is encoded in the EndAccessInst itself)
  //    - begin_access arguments cannot be basic block arguments.
  //      This enables the analysis to look back to find the *single* storage
  //      storage location accessed.

  if (Fn.empty())
    return;

  // Collects calls the Standard Library swap() for Fix-Its.
  llvm::SmallVector<ApplyInst *, 8> CallsToSwap;

  // Stores the accesses that have been found to conflict. Used to defer
  // emitting diagnostics until we can determine whether they should
  // be suppressed.
  llvm::SmallVector<ConflictingAccess, 4> ConflictingAccesses;

  // For each basic block, track the stack of current accesses on
  // exit from that block.
  llvm::SmallDenseMap<SILBasicBlock *, Optional<StorageMap>, 32>
      BlockOutAccesses;

  BlockOutAccesses[Fn.getEntryBlock()] = StorageMap();

  for (auto *BB : PO->getReversePostOrder()) {
    Optional<StorageMap> &BBState = BlockOutAccesses[BB];

    // Because we use a reverse post-order traversal, unless this is the entry
    // at least one of its predecessors must have been reached. Use the out
    // state for that predecessor as our in state. The SIL verifier guarantees
    // that all incoming edges must have the same current accesses.
    for (auto *Pred : BB->getPredecessorBlocks()) {
      auto it = BlockOutAccesses.find(Pred);
      if (it == BlockOutAccesses.end())
        continue;

      const Optional<StorageMap> &PredAccesses = it->getSecond();
      if (PredAccesses) {
        BBState = PredAccesses;
        break;
      }
    }

    // The in-progress accesses for the current program point, represented
    // as map from storage locations to the accesses in progress for the
    // location.
    StorageMap &Accesses = *BBState;

    for (auto &I : *BB) {
      // Apply transfer functions. Beginning an access
      // increments the read or write count for the storage location;
      // Ending onr decrements the count.
      if (auto *BAI = dyn_cast<BeginAccessInst>(&I)) {
        SILAccessKind Kind = BAI->getAccessKind();
        const AccessedStorage &Storage = findAccessedStorage(BAI->getSource());
        AccessInfo &Info = Accesses[Storage];
        const IndexTrieNode *SubPath = ASA->findSubPathAccessed(BAI);
        if (auto Conflict = shouldReportAccess(Info, Kind, SubPath)) {
          ConflictingAccesses.emplace_back(Storage, *Conflict,
                                           RecordedAccess(BAI, SubPath));
        }

        Info.beginAccess(BAI, SubPath);
        continue;
      }

      if (auto *EAI = dyn_cast<EndAccessInst>(&I)) {
        auto It = Accesses.find(findAccessedStorage(EAI->getSource()));
        AccessInfo &Info = It->getSecond();

        BeginAccessInst *BAI = EAI->getBeginAccess();
        const IndexTrieNode *SubPath = ASA->findSubPathAccessed(BAI);
        Info.endAccess(EAI, SubPath);

        // If the storage location has no more in-progress accesses, remove
        // it to keep the StorageMap lean.
        if (!Info.hasAccessesInProgress())
          Accesses.erase(It);
        continue;
      }

      if (auto *AI = dyn_cast<ApplyInst>(&I)) {
        // Record calls to swap() for potential Fix-Its.
        if (isCallToStandardLibrarySwap(AI, Fn.getASTContext()))
          CallsToSwap.push_back(AI);
        else
          checkForViolationsInNoEscapeClosures(Accesses, AI, ASA,
                                               ConflictingAccesses);
        continue;
      }

      if (auto *TAI = dyn_cast<TryApplyInst>(&I)) {
        checkForViolationsInNoEscapeClosures(Accesses, TAI, ASA,
                                             ConflictingAccesses);
        continue;
      }
      // Sanity check to make sure entries are properly removed.
      assert((!isa<ReturnInst>(&I) || Accesses.size() == 0) &&
             "Entries were not properly removed?!");
    }
  }

  // Now that we've collected violations and suppressed calls, emit
  // diagnostics.
  for (auto &Violation : ConflictingAccesses) {
    diagnoseExclusivityViolation(Violation, CallsToSwap, Fn.getASTContext());
  }
}

namespace {

class DiagnoseStaticExclusivity : public SILFunctionTransform {
public:
  DiagnoseStaticExclusivity() {}

private:
  void run() override {
    SILFunction *Fn = getFunction();
    // This is a staging flag. Eventually the ability to turn off static
    // enforcement will be removed.
    if (!Fn->getModule().getOptions().EnforceExclusivityStatic)
      return;

    PostOrderFunctionInfo *PO = getAnalysis<PostOrderAnalysis>()->get(Fn);
    auto *ASA = getAnalysis<AccessSummaryAnalysis>();
    checkStaticExclusivity(*Fn, PO, ASA);
  }
};

} // end anonymous namespace

SILTransform *swift::createDiagnoseStaticExclusivity() {
  return new DiagnoseStaticExclusivity();
}
