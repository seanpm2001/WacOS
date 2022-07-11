//===--- SILProfiler.cpp - Instrumentation based profiling ----------------===//
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

#include "swift/SIL/SILProfiler.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Expr.h"
#include "swift/AST/Module.h"
#include "swift/AST/SourceFile.h"
#include "swift/AST/Stmt.h"
#include "swift/Parse/Lexer.h"
#include "swift/SIL/FormalLinkage.h"
#include "swift/SIL/SILModule.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include "llvm/ProfileData/Coverage/CoverageMappingWriter.h"
#include "llvm/ProfileData/InstrProf.h"

#include <forward_list>

#define DEBUG_TYPE "SILProfiler"

using namespace swift;

/// Check if a closure has a body.
static bool doesClosureHaveBody(AbstractClosureExpr *ACE) {
  if (auto *CE = dyn_cast<ClosureExpr>(ACE))
    return CE->getBody();
  if (auto *autoCE = dyn_cast<AutoClosureExpr>(ACE))
    return autoCE->getBody();
  return false;
}

/// Check whether a root AST node is unmapped, i.e not profiled.
static bool isUnmapped(ASTNode N) {
  // Do not map AST nodes with invalid source locations.
  if (N.getStartLoc().isInvalid() || N.getEndLoc().isInvalid()) {
    LLVM_DEBUG(llvm::dbgs()
               << "Skipping ASTNode: invalid start/end locations\n");
    return true;
  }

  if (auto *E = N.dyn_cast<Expr *>()) {
    if (isa<LiteralExpr>(E)) {
      LLVM_DEBUG(llvm::dbgs() << "Skipping ASTNode: literal expr\n");
      return true;
    }

    if (auto *CE = dyn_cast<AbstractClosureExpr>(E)) {
      // Only map closure expressions with bodies.
      if (!doesClosureHaveBody(CE)) {
        LLVM_DEBUG(llvm::dbgs() << "Skipping ASTNode: closure without body\n");
        return true;
      }

      // Don't map implicit closures, unless they're autoclosures.
      if (!isa<AutoClosureExpr>(CE) && CE->isImplicit()) {
        LLVM_DEBUG(llvm::dbgs() << "Skipping ASTNode: implicit closure expr\n");
        return true;
      }
    }

    // Map all other kinds of expressions.
    return false;
  }

  auto *D = N.get<Decl *>();
  if (auto *AFD = dyn_cast<AbstractFunctionDecl>(D)) {
    // Don't map functions without bodies.
    if (!AFD->hasBody()) {
      LLVM_DEBUG(llvm::dbgs() << "Skipping ASTNode: function without body\n");
      return true;
    }

    // Map implicit getters.
    if (auto *accessor = dyn_cast<AccessorDecl>(AFD))
      if (accessor->isImplicit() && accessor->isGetter())
        return false;
  }

  // Skip any remaining implicit, or otherwise unsupported decls.
  if (D->isImplicit() || isa<EnumCaseDecl>(D)) {
    LLVM_DEBUG(llvm::dbgs() << "Skipping ASTNode: implicit/unsupported decl\n");
    return true;
  }

  return false;
}

namespace swift {
bool doesASTRequireProfiling(SILModule &M, ASTNode N) {
  return M.getOptions().GenerateProfile && !isUnmapped(N);
}
} // namespace swift

/// Get the DeclContext for the decl referenced by \p forDecl.
DeclContext *getProfilerContextForDecl(ASTNode N, SILDeclRef forDecl) {
  if (auto *D = N.dyn_cast<Decl *>())
    if (auto *TLCD = dyn_cast<TopLevelCodeDecl>(D))
      return TLCD;
  assert(!forDecl.isNull() && "Expected a nonnull SILDeclRef");
  if (auto *ACE = forDecl.getAbstractClosureExpr())
    return ACE;
  return forDecl.getDecl()->getDeclContext();
}

static Stmt *getProfilerStmtForCase(CaseStmt *caseStmt) {
  switch (caseStmt->getParentKind()) {
  case CaseParentKind::Switch:
    return caseStmt;
  case CaseParentKind::DoCatch:
    return caseStmt->getBody();
  }
  llvm_unreachable("invalid parent kind");
}

/// Check that the input AST has at least been type-checked.
LLVM_ATTRIBUTE_UNUSED
static bool hasASTBeenTypeChecked(ASTNode N, SILDeclRef forDecl) {
  DeclContext *DC = getProfilerContextForDecl(N, forDecl);
  SourceFile *SF = DC->getParentSourceFile();
  return !SF || SF->ASTStage >= SourceFile::TypeChecked;
}

/// Check whether a mapped AST node requires a new profiler.
static bool canCreateProfilerForAST(ASTNode N, SILDeclRef forDecl) {
  assert(hasASTBeenTypeChecked(N, forDecl) &&
         "Cannot use this AST for profiling");

  if (auto *D = N.dyn_cast<Decl *>()) {
    if (isa<AbstractFunctionDecl>(D))
      return true;

    if (isa<TopLevelCodeDecl>(D))
      return true;
  } else if (N.get<Expr *>()) {
    if (forDecl.isStoredPropertyInitializer() ||
        forDecl.isPropertyWrapperBackingInitializer() ||
        forDecl.getAbstractClosureExpr())
      return true;
  }
  return false;
}

SILProfiler *SILProfiler::create(SILModule &M, ForDefinition_t forDefinition,
                                 ASTNode N, SILDeclRef forDecl) {
  // Avoid generating profiling state for declarations.
  if (!forDefinition)
    return nullptr;

  const auto &Opts = M.getOptions();
  if (!doesASTRequireProfiling(M, N) && Opts.UseProfile.empty())
    return nullptr;

  if (!canCreateProfilerForAST(N, forDecl)) {
    N.dump(llvm::errs());
    llvm_unreachable("Invalid AST node for profiling");
  }

  auto *Buf = M.allocate<SILProfiler>(1);
  auto *SP =
      ::new (Buf) SILProfiler(M, N, forDecl, Opts.EmitProfileCoverageMapping);
  SP->assignRegionCounters();
  return SP;
}

namespace {

/// Special logic for handling function visitation.
///
/// To avoid creating duplicate mappings, a function decl is only profiled if
/// it hasn't been reached via recursive walk.
///
/// Apply \p Func if the function can be visited.
template <typename F>
bool visitFunctionDecl(ASTWalker &Walker, AbstractFunctionDecl *AFD, F Func) {
  bool continueWalk = Walker.Parent.isNull();
  if (continueWalk)
    Func();
  return continueWalk;
}

/// Whether to skip visitation of an expression. Children of skipped exprs
/// should still be visited.
static bool skipExpr(Expr *E) {
  return !E->getStartLoc().isValid() || !E->getEndLoc().isValid();
}

/// An ASTWalker that maps ASTNodes to profiling counters.
struct MapRegionCounters : public ASTWalker {
  /// The next counter value to assign.
  unsigned NextCounter = 0;

  /// The map of statements to counters.
  llvm::DenseMap<ASTNode, unsigned> &CounterMap;

  MapRegionCounters(llvm::DenseMap<ASTNode, unsigned> &CounterMap)
      : CounterMap(CounterMap) {}

  void mapRegion(ASTNode N) {
    CounterMap[N] = NextCounter;

    LLVM_DEBUG({
      llvm::dbgs() << "Assigned counter #" << NextCounter << " to: ";
      auto *E = N.dyn_cast<Expr *>();
      if (E)
        llvm::dbgs() << Expr::getKindName(E->getKind()) << "\n";
      auto *S = N.dyn_cast<Stmt *>();
      if (S)
        llvm::dbgs() << Stmt::getKindName(S->getKind()) << "\n";
    });

    ++NextCounter;
  }

  bool walkToDeclPre(Decl *D) override {
    if (isUnmapped(D))
      return false;

    if (auto *AFD = dyn_cast<AbstractFunctionDecl>(D)) {
      return visitFunctionDecl(*this, AFD, [&] { mapRegion(AFD->getBody()); });
    } else if (auto *TLCD = dyn_cast<TopLevelCodeDecl>(D)) {
      mapRegion(TLCD->getBody());
    }
    return true;
  }

  std::pair<bool, Stmt *> walkToStmtPre(Stmt *S) override {
    if (auto *IS = dyn_cast<IfStmt>(S)) {
      mapRegion(IS->getThenStmt());
    } else if (auto *US = dyn_cast<GuardStmt>(S)) {
      mapRegion(US->getBody());
    } else if (auto *WS = dyn_cast<WhileStmt>(S)) {
      mapRegion(WS->getBody());
    } else if (auto *RWS = dyn_cast<RepeatWhileStmt>(S)) {
      mapRegion(RWS->getBody());
    } else if (auto *FES = dyn_cast<ForEachStmt>(S)) {
      mapRegion(FES->getBody());
    } else if (auto *SS = dyn_cast<SwitchStmt>(S)) {
      mapRegion(SS);
    } else if (auto *CS = dyn_cast<CaseStmt>(S)) {
      mapRegion(getProfilerStmtForCase(CS));
    }
    return {true, S};
  }

  std::pair<bool, Expr *> walkToExprPre(Expr *E) override {
    if (skipExpr(E))
      return {true, E};

    // Profiling for closures should be handled separately. Do not visit
    // closure expressions twice.
    if (isa<AbstractClosureExpr>(E) && !Parent.isNull())
      return {false, E};

    // If AST visitation begins with an expression, the counter map must be
    // empty. Set up a counter for the root.
    if (Parent.isNull()) {
      assert(CounterMap.empty() && "Mapped a region before visiting the root?");
      mapRegion(E);
    }

    if (auto *IE = dyn_cast<IfExpr>(E)) {
      mapRegion(IE->getThenExpr());
    }

    // rdar://42792053
    // TODO: There's an outstanding issue here with LazyInitializerExpr. A LIE
    // is copied into the body of a property getter after type-checking (before
    // coverage). ASTWalker only visits this expression once via the property's
    // VarDecl, and does not visit it again within the getter. This results in
    // missing coverage. SILGen treats the init expr as part of the getter, but
    // its SILProfiler has no information about the init because the LIE isn't
    // visited here.

    return {true, E};
  }
};

/// A node in an expression tree of counters.
class CounterExpr {
  enum class Kind { Node, Add, Sub, Zero, Ref };
  Kind K;
  ASTNode Node;
  const CounterExpr *LHS;
  const CounterExpr *RHS;

  CounterExpr(Kind K) : K(K) {
    assert((K == Kind::Zero) && "only valid for Zero");
  }

  CounterExpr(Kind K, ASTNode Node) : K(K), Node(Node) {
    assert(K == Kind::Node && "only valid for Node");
  }

  CounterExpr(Kind K, const CounterExpr &LHS) : K(K), LHS(&LHS) {
    assert((K == Kind::Ref) && "only valid for Ref");
  }

  CounterExpr(Kind K, const CounterExpr &LHS, const CounterExpr &RHS)
      : K(K), LHS(&LHS), RHS(&RHS) {
    assert((K == Kind::Add || K == Kind::Sub) && "only valid for operators");
  }

public:
  // Move only.
  CounterExpr(const CounterExpr &) = delete;
  void operator=(const CounterExpr &) = delete;
  CounterExpr(CounterExpr &&Other) = default;
  CounterExpr &operator=(CounterExpr &&RHS) = default;

  static CounterExpr Leaf(ASTNode Node) {
    return CounterExpr(Kind::Node, Node);
  }
  static CounterExpr Add(const CounterExpr &LHS, const CounterExpr &RHS) {
    return CounterExpr(Kind::Add, LHS, RHS);
  }
  static CounterExpr Sub(const CounterExpr &LHS, const CounterExpr &RHS) {
    return CounterExpr(Kind::Sub, LHS, RHS);
  }
  static CounterExpr Zero() { return CounterExpr(Kind::Zero); }
  static CounterExpr Ref(const CounterExpr &LHS) {
    return CounterExpr(Kind::Ref, LHS);
  }

  /// Return the referenced node, or null if this is not a Ref type.
  const CounterExpr *getReferencedNode() const {
    return K == Kind::Ref ? LHS : nullptr;
  }

  /// Returns true if this is a Zero node.
  bool isZero() const { return K == Kind::Zero; }

  /// Expand this node into an llvm::coverage::Counter.
  ///
  /// Updates \c Builder with any expressions that are needed to represent this
  /// counter.
  llvm::coverage::Counter
  expand(llvm::coverage::CounterExpressionBuilder &Builder,
         llvm::DenseMap<ASTNode, unsigned> &Counters) const {
    switch (K) {
    case Kind::Zero:
      return llvm::coverage::Counter::getZero();
    case Kind::Node:
      return llvm::coverage::Counter::getCounter(Counters[Node]);
    case Kind::Add:
      return Builder.add(LHS->expand(Builder, Counters),
                         RHS->expand(Builder, Counters));
    case Kind::Sub:
      return Builder.subtract(LHS->expand(Builder, Counters),
                              RHS->expand(Builder, Counters));
    case Kind::Ref:
      return LHS->expand(Builder, Counters);
    }

    llvm_unreachable("Unhandled Kind in switch.");
  }

  void print(raw_ostream &OS) const {
    switch (K) {
    case Kind::Zero:
      OS << "zero";
      return;
    case Kind::Node:
      OS << "node(" << Node.getOpaqueValue() << ")";
      return;
    case Kind::Add:
    case Kind::Sub:
      LHS->print(OS);
      OS << ' ' << ((K == Kind::Add) ? '+' : '-') << ' ';
      RHS->print(OS);
      return;
    case Kind::Ref:
      OS << "ref(";
      LHS->print(OS);
      OS << ")";
      return;
    }
    llvm_unreachable("Unhandled Kind in switch.");
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() const { print(llvm::errs()); }
#endif
};

/// A region of source code that can be mapped to a counter.
class SourceMappingRegion {
  ASTNode Node;

  CounterExpr *Count;

  /// The region's starting location.
  Optional<SourceLoc> StartLoc;

  /// The region's ending location.
  Optional<SourceLoc> EndLoc;

public:
  SourceMappingRegion(ASTNode Node, CounterExpr &Count,
                      Optional<SourceLoc> StartLoc, Optional<SourceLoc> EndLoc)
      : Node(Node), Count(&Count), StartLoc(StartLoc), EndLoc(EndLoc) {
    assert((!StartLoc || StartLoc->isValid()) &&
           "Expected start location to be valid");
    assert((!EndLoc || EndLoc->isValid()) &&
           "Expected end location to be valid");
  }

  SourceMappingRegion(SourceMappingRegion &&Region) = default;
  SourceMappingRegion &operator=(SourceMappingRegion &&RHS) = default;

  ASTNode getNode() const { return Node; }

  CounterExpr &getCounter() const { return *Count; }

  bool hasStartLoc() const { return StartLoc.hasValue(); }

  void setStartLoc(SourceLoc Loc) {
    assert(Loc.isValid());
    StartLoc = Loc;
  }

  const SourceLoc &getStartLoc() const {
    assert(StartLoc && "Region has no start location");
    return *StartLoc;
  }

  bool hasEndLoc() const { return EndLoc.hasValue(); }

  void setEndLoc(SourceLoc Loc) {
    assert(Loc.isValid());
    EndLoc = Loc;
  }

  const SourceLoc &getEndLoc() const {
    assert(EndLoc && "Region has no end location");
    return *EndLoc;
  }

  void print(llvm::raw_ostream &OS, const SourceManager &SM) const {
    OS << "[";
    if (hasStartLoc())
      getStartLoc().print(OS, SM);
    else
      OS << "?";
    OS << ", ";
    if (hasEndLoc())
      getEndLoc().print(OS, SM);
    else
      OS << "?";
    OS << "]";
  }
};

/// An ASTWalker that maps ASTNodes to profiling counters.
struct PGOMapping : public ASTWalker {
  /// The next counter value to assign.
  unsigned NextCounter;

  /// The map of statements to counters.
  llvm::DenseMap<ASTNode, ProfileCounter> &LoadedCounterMap;
  llvm::Expected<llvm::InstrProfRecord> &LoadedCounts;
  llvm::DenseMap<ASTNode, ASTNode> &CondToParentMap;
  llvm::DenseMap<ASTNode, unsigned> CounterMap;

  PGOMapping(llvm::DenseMap<ASTNode, ProfileCounter> &LoadedCounterMap,
             llvm::Expected<llvm::InstrProfRecord> &LoadedCounts,
             llvm::DenseMap<ASTNode, ASTNode> &RegionCondToParentMap)
      : NextCounter(0), LoadedCounterMap(LoadedCounterMap),
        LoadedCounts(LoadedCounts), CondToParentMap(RegionCondToParentMap) {}

  unsigned getParentCounter() const {
    if (Parent.isNull())
      return 0;
    else if (Parent.getKind() == ASTWalker::ParentKind::Decl) {
      auto it = CounterMap.find(Parent.getAsDecl());
      return (it != CounterMap.end()) ? it->getSecond() : 0;
    } else if (Parent.getKind() == ASTWalker::ParentKind::Stmt) {
      auto it = CounterMap.find(Parent.getAsStmt());
      return (it != CounterMap.end()) ? it->getSecond() : 0;
    } else if (Parent.getKind() == ASTWalker::ParentKind::Expr) {
      auto it = CounterMap.find(Parent.getAsExpr());
      return (it != CounterMap.end()) ? it->getSecond() : 0;
    }
    return 0;
  }

  ProfileCounter subtract(ProfileCounter L, ProfileCounter R) {
    if (!L.hasValue() || !R.hasValue()) {
      return L;
    }
    uint64_t LV = L.getValue();
    uint64_t RV = R.getValue();
    assert(LV >= RV && "Invalid counter subtraction");
    return LV - RV;
  }

  /// Load the execution count corresponding to \p Node from a profile, if one
  /// is available.
  ProfileCounter loadExecutionCount(ASTNode Node) {
    if (!Node)
      return ProfileCounter();

    auto CounterIt = CounterMap.find(Node);
    assert(CounterIt != CounterMap.end() &&
           "region does not have an associated counter");

    unsigned CounterIndexForFunc = CounterIt->second;
    return LoadedCounts->Counts[CounterIndexForFunc];
  }

  bool walkToDeclPre(Decl *D) override {
    if (isUnmapped(D))
      return false;
    if (auto *AFD = dyn_cast<AbstractFunctionDecl>(D)) {
      return visitFunctionDecl(*this, AFD, [&] {
        auto node = AFD->getBody();
        CounterMap[node] = NextCounter++;
        auto count = loadExecutionCount(node);
        LoadedCounterMap[node] = count;
      });
    }
    if (auto *TLCD = dyn_cast<TopLevelCodeDecl>(D)) {
      auto node = TLCD->getBody();
      CounterMap[node] = NextCounter++;
      auto count = loadExecutionCount(node);
      LoadedCounterMap[node] = count;
    }
    return true;
  }

  std::pair<bool, Stmt *> walkToStmtPre(Stmt *S) override {
    unsigned parent = getParentCounter();
    if (auto *IS = dyn_cast<IfStmt>(S)) {
      auto thenStmt = IS->getThenStmt();
      CounterMap[thenStmt] = NextCounter++;
      auto thenCount = loadExecutionCount(thenStmt);
      LoadedCounterMap[thenStmt] = thenCount;
      if (auto elseStmt = IS->getElseStmt()) {
        CounterMap[elseStmt] = parent;
        auto count = loadExecutionCount(elseStmt);
        if (!parent) {
          auto thenVal = thenCount.getValue();
          for (auto pCount = NextCounter - 1; pCount > 0; --pCount) {
            auto cCount = LoadedCounts->Counts[pCount];
            if (cCount > thenVal) {
              count = cCount;
              break;
            }
          }
        }
        LoadedCounterMap[elseStmt] = subtract(count, thenCount);
        auto Cond = IS->getCond();
        for (const auto &elt : Cond) {
          if (elt.getKind() ==
              StmtConditionElement::ConditionKind::CK_PatternBinding) {
            CondToParentMap[elt.getInitializer()] = IS;
          }
        }
      }
    } else if (auto *US = dyn_cast<GuardStmt>(S)) {
      auto guardBody = US->getBody();
      CounterMap[guardBody] = NextCounter++;
      auto guardCount = loadExecutionCount(guardBody);
      LoadedCounterMap[guardBody] = guardCount;
      CounterMap[US] = parent;
      auto count = loadExecutionCount(US);
      LoadedCounterMap[US] = subtract(count, guardCount);
    } else if (auto *WS = dyn_cast<WhileStmt>(S)) {
      auto whileBody = WS->getBody();
      CounterMap[whileBody] = NextCounter++;
      auto whileCount = loadExecutionCount(whileBody);
      LoadedCounterMap[whileBody] = whileCount;
      CounterMap[WS] = parent;
      auto count = loadExecutionCount(WS);
      LoadedCounterMap[WS] = count;
    } else if (auto *RWS = dyn_cast<RepeatWhileStmt>(S)) {
      auto rwsBody = RWS->getBody();
      CounterMap[rwsBody] = NextCounter++;
      auto rwsBodyCount = loadExecutionCount(rwsBody);
      LoadedCounterMap[rwsBody] = rwsBodyCount;
      CounterMap[RWS] = parent;
      auto count = loadExecutionCount(RWS);
      LoadedCounterMap[RWS] = count;
    } else if (auto *FES = dyn_cast<ForEachStmt>(S)) {
      auto fesBody = FES->getBody();
      CounterMap[fesBody] = NextCounter++;
      auto fesCount = loadExecutionCount(fesBody);
      LoadedCounterMap[fesBody] = fesCount;
      CounterMap[FES] = parent;
      auto count = loadExecutionCount(FES);
      LoadedCounterMap[FES] = count;
    } else if (auto *SS = dyn_cast<SwitchStmt>(S)) {
      CounterMap[SS] = NextCounter++;
      auto ssCount = loadExecutionCount(SS);
      LoadedCounterMap[SS] = ssCount;
    } else if (auto *CS = dyn_cast<CaseStmt>(S)) {
      auto stmt = getProfilerStmtForCase(CS);
      CounterMap[stmt] = NextCounter++;
      auto csCount = loadExecutionCount(stmt);
      LoadedCounterMap[stmt] = csCount;
    }
    return {true, S};
  }

  std::pair<bool, Expr *> walkToExprPre(Expr *E) override {
    if (skipExpr(E))
      return {true, E};

    // Profiling for closures should be handled separately. Do not visit
    // closure expressions twice.
    if (isa<AbstractClosureExpr>(E) && !Parent.isNull())
      return {false, E};

    unsigned parent = getParentCounter();

    if (Parent.isNull()) {
      CounterMap[E] = NextCounter++;
      auto eCount = loadExecutionCount(E);
      LoadedCounterMap[E] = eCount;
    }

    if (auto *IE = dyn_cast<IfExpr>(E)) {
      auto thenExpr = IE->getThenExpr();
      CounterMap[thenExpr] = NextCounter++;
      auto thenCount = loadExecutionCount(thenExpr);
      LoadedCounterMap[thenExpr] = thenCount;
      auto elseExpr = IE->getElseExpr();
      assert(elseExpr && "An if-expr must have an else subexpression");
      CounterMap[elseExpr] = parent;
      auto count = loadExecutionCount(elseExpr);
      if (!parent) {
        auto thenVal = thenCount.getValue();
        for (auto pCount = NextCounter - 1; pCount > 0; --pCount) {
          auto cCount = LoadedCounts->Counts[pCount];
          if (cCount > thenVal) {
            count = cCount;
            break;
          }
        }
      }
      LoadedCounterMap[elseExpr] = subtract(count, thenCount);
    }
    return {true, E};
  }
};

struct CoverageMapping : public ASTWalker {
private:
  const SourceManager &SM;

  /// Storage for counter expressions.
  std::forward_list<CounterExpr> Exprs;

  /// The map of statements to counter expressions.
  llvm::DenseMap<ASTNode, CounterExpr *> CounterMap;

  /// The source mapping regions for this function.
  std::vector<SourceMappingRegion> SourceRegions;

  /// A stack of currently live regions.
  std::vector<SourceMappingRegion> RegionStack;

  /// A stack of active repeat-while loops.
  std::vector<RepeatWhileStmt *> RepeatWhileStack;

  /// A stack of active do-catch statements.
  std::vector<DoCatchStmt *> DoCatchStack;

  CounterExpr *ExitCounter = nullptr;

  Stmt *ImplicitTopLevelBody = nullptr;

  /// Return true if \c Node has an associated counter.
  bool hasCounter(ASTNode Node) { return CounterMap.count(Node); }

  /// Return the region counter for \c Node.
  ///
  /// This should only be called on statements that have a dedicated counter.
  CounterExpr &getCounter(ASTNode Node) {
    assert(CounterMap.count(Node) && "No counter found");
    return *CounterMap[Node];
  }

  /// Create a counter expression.
  CounterExpr &createCounter(CounterExpr &&Expr) {
    Exprs.push_front(std::move(Expr));
    return Exprs.front();
  }

  /// Create a counter expression for \c Node and add it to the map.
  CounterExpr &assignCounter(ASTNode Node, CounterExpr &&Expr) {
    assert(Node && "Assigning counter expression to non-existent AST node");
    CounterExpr &Result = createCounter(std::move(Expr));
    CounterMap[Node] = &Result;
    return Result;
  }

  /// Create a counter expression referencing \c Node's own counter.
  CounterExpr &assignCounter(ASTNode Node) {
    return assignCounter(Node, CounterExpr::Leaf(Node));
  }

  /// Add \c Expr to \c Node's counter.
  void addToCounter(ASTNode Node, CounterExpr &Expr) {
    CounterExpr &Counter = getCounter(Node);
    if (const CounterExpr *ReferencedCounter = Counter.getReferencedNode())
      Counter = CounterExpr::Add(*ReferencedCounter, Expr);
    else if (Counter.isZero())
      Counter = CounterExpr::Ref(Expr);
    else
      Counter = CounterExpr::Add(createCounter(std::move(Counter)), Expr);
  }

  /// Subtract \c Expr from \c Node's counter.
  void subtractFromCounter(ASTNode Node, CounterExpr &Expr) {
    CounterExpr &Counter = getCounter(Node);
    assert(!Counter.isZero() && "Cannot create a negative counter");
    if (const CounterExpr *ReferencedCounter = Counter.getReferencedNode())
      Counter = CounterExpr::Sub(*ReferencedCounter, Expr);
    else
      Counter = CounterExpr::Sub(createCounter(std::move(Counter)), Expr);
  }

  /// Return the current region's counter.
  CounterExpr &getCurrentCounter() { return getRegion().getCounter(); }

  /// Get the counter from the end of the most recent scope.
  CounterExpr &getExitCounter() {
    assert(ExitCounter && "no exit counter available");
    return *ExitCounter;
  }

  /// Set the exit count so we can leave the scope related to \c Node
  ///
  /// Returns the delta of the count on entering \c Node and exiting, or null if
  /// there was no change.
  CounterExpr *setExitCount(ASTNode Node) {
    ExitCounter = &getCurrentCounter();
    if (hasCounter(Node) && ExitCounter != &getCounter(Node))
      return &createCounter(CounterExpr::Sub(getCounter(Node), *ExitCounter));
    return nullptr;
  }

  /// Adjust the count for control flow when exiting a scope.
  void adjustForNonLocalExits(ASTNode Scope, CounterExpr *ControlFlowAdjust) {
    if (Parent.getAsDecl())
      return;

    CounterExpr *JumpsToLabel = nullptr;
    Stmt *ParentStmt = Parent.getAsStmt();
    if (ParentStmt) {
      if (isa<DoStmt>(ParentStmt) || isa<DoCatchStmt>(ParentStmt))
        return;
      auto caseStmt = dyn_cast_or_null<CaseStmt>(ParentStmt);
      if (caseStmt && caseStmt->getParentKind() == CaseParentKind::DoCatch)
        return;
      if (auto *LS = dyn_cast<LabeledStmt>(ParentStmt))
        JumpsToLabel = &getCounter(LS);
    }

    if (!ControlFlowAdjust && !JumpsToLabel)
      return;

    CounterExpr *Count = &getCurrentCounter();
    // Add the counts from jumps directly to the label (such as breaks)
    if (JumpsToLabel)
      Count = &createCounter(CounterExpr::Add(*Count, *JumpsToLabel));
    // Now apply any adjustments for control flow.
    if (ControlFlowAdjust)
      Count = &createCounter(CounterExpr::Sub(*Count, *ControlFlowAdjust));

    RegionStack.emplace_back(ASTNode(), *Count, getEndLoc(Scope), None);
  }

  /// Push a region covering \c Node onto the stack.
  void pushRegion(ASTNode Node) {
    RegionStack.emplace_back(Node, getCounter(Node), Node.getStartLoc(),
                             getEndLoc(Node));
    LLVM_DEBUG({
      llvm::dbgs() << "Pushed region: ";
      RegionStack.back().print(llvm::dbgs(), SM);
      llvm::dbgs() << "\n";
    });
  }

  /// Replace the current region's count by pushing an incomplete region.
  void replaceCount(CounterExpr &&Expr, Optional<SourceLoc> Start = None) {
    CounterExpr &Counter = createCounter(std::move(Expr));
    RegionStack.emplace_back(ASTNode(), Counter, Start, None);
  }

  /// Get the location for the end of the last token in \c Node.
  SourceLoc getEndLoc(ASTNode Node) {
    return Lexer::getLocForEndOfToken(SM, Node.getEndLoc());
  }

  /// Pop regions from the stack into the function's list of regions.
  ///
  /// Adds all regions from \c ParentNode to the top of the stack to the
  /// function's \c SourceRegions.
  void popRegions(ASTNode ParentNode) {
    auto I = RegionStack.begin(), E = RegionStack.end();
    while (I != E &&
           I->getNode().getOpaqueValue() != ParentNode.getOpaqueValue())
      ++I;
    assert(I != E && "parent not in stack");
    auto ParentIt = I;
    SourceLoc EndLoc = ParentIt->getEndLoc();

    unsigned FirstPoppedIndex = SourceRegions.size();
    (void)FirstPoppedIndex;
    SourceRegions.push_back(std::move(*I++));
    for (; I != E; ++I) {
      if (!I->hasStartLoc())
        continue;
      if (!I->hasEndLoc())
        I->setEndLoc(EndLoc);
      SourceRegions.push_back(std::move(*I));
    }

    LLVM_DEBUG({
      for (unsigned Idx = FirstPoppedIndex; Idx < SourceRegions.size(); ++Idx) {
        llvm::dbgs() << "Popped region: ";
        SourceRegions[Idx].print(llvm::dbgs(), SM);
        llvm::dbgs() << "\n";
      }
    });

    RegionStack.erase(ParentIt, E);
  }

  /// Return the currently active region.
  SourceMappingRegion &getRegion() {
    assert(!RegionStack.empty() && "statement has no region");
    return RegionStack.back();
  }

  /// Ensure that \c S is included in the current region.
  void extendRegion(ASTNode S) {
    SourceMappingRegion &Region = getRegion();
    SourceLoc StartLoc = S.getStartLoc();
    if (!Region.hasStartLoc())
      Region.setStartLoc(StartLoc);
  }

  /// Mark \c S as a terminator, starting a zero region.
  void terminateRegion(ASTNode S) {
    SourceMappingRegion &Region = getRegion();
    if (!Region.hasEndLoc())
      Region.setEndLoc(getEndLoc(S));
    replaceCount(CounterExpr::Zero());
  }

  Expr *getConditionNode(StmtCondition SC) {
    assert(!SC.empty() && "Empty condition");
    return SC.front().getBooleanOrNull();
  }

public:
  CoverageMapping(const SourceManager &SM) : SM(SM) {}

  /// Generate the coverage counter mapping regions from collected
  /// source regions.
  SILCoverageMap *emitSourceRegions(
      SILModule &M, StringRef Name, StringRef PGOFuncName, uint64_t Hash,
      llvm::DenseMap<ASTNode, unsigned> &CounterIndices, StringRef Filename) {
    if (SourceRegions.empty())
      return nullptr;

    llvm::coverage::CounterExpressionBuilder Builder;
    std::vector<SILCoverageMap::MappedRegion> Regions;
    for (const auto &Region : SourceRegions) {
      assert(Region.hasStartLoc() && "invalid region");
      assert(Region.hasEndLoc() && "incomplete region");

      auto Start = SM.getLineAndColumnInBuffer(Region.getStartLoc());
      auto End = SM.getLineAndColumnInBuffer(Region.getEndLoc());
      assert(Start.first <= End.first && "region start and end out of order");

      Regions.emplace_back(Start.first, Start.second, End.first, End.second,
                           Region.getCounter().expand(Builder, CounterIndices));
    }
    return SILCoverageMap::create(M, Filename, Name, PGOFuncName, Hash, Regions,
                                  Builder.getExpressions());
  }

  bool walkToDeclPre(Decl *D) override {
    if (isUnmapped(D))
      return false;

    if (auto *AFD = dyn_cast<AbstractFunctionDecl>(D)) {
      return visitFunctionDecl(*this, AFD, [&] {
        assignCounter(AFD->getBody());
      });
    } else if (auto *TLCD = dyn_cast<TopLevelCodeDecl>(D)) {
      assignCounter(TLCD->getBody());
      ImplicitTopLevelBody = TLCD->getBody();
    }
    return true;
  }

  bool walkToDeclPost(Decl *D) override {
    if (isa<TopLevelCodeDecl>(D))
      ImplicitTopLevelBody = nullptr;
    return true;
  }

  std::pair<bool, Stmt *> walkToStmtPre(Stmt *S) override {
    if (S->isImplicit() && S != ImplicitTopLevelBody)
      return {true, S};

    if (!RegionStack.empty())
      extendRegion(S);

    if (auto *BS = dyn_cast<BraceStmt>(S)) {
      if (hasCounter(BS))
        pushRegion(BS);

    } else if (auto *IS = dyn_cast<IfStmt>(S)) {
      if (auto *Cond = getConditionNode(IS->getCond()))
        assignCounter(Cond, CounterExpr::Ref(getCurrentCounter()));
      assignCounter(IS, CounterExpr::Zero());
      CounterExpr &ThenCounter = assignCounter(IS->getThenStmt());
      if (IS->getElseStmt())
        assignCounter(IS->getElseStmt(),
                      CounterExpr::Sub(getCurrentCounter(), ThenCounter));
    } else if (auto *GS = dyn_cast<GuardStmt>(S)) {
      assignCounter(GS, CounterExpr::Zero());
      assignCounter(GS->getBody());

    } else if (auto *WS = dyn_cast<WhileStmt>(S)) {
      assignCounter(WS, CounterExpr::Zero());
      if (auto *E = getConditionNode(WS->getCond()))
        assignCounter(E, CounterExpr::Ref(getCurrentCounter()));
      assignCounter(WS->getBody());

    } else if (auto *RWS = dyn_cast<RepeatWhileStmt>(S)) {
      assignCounter(RWS, CounterExpr::Zero());
      CounterExpr &BodyCounter = assignCounter(RWS->getBody());
      assignCounter(RWS->getCond(), CounterExpr::Ref(BodyCounter));
      RepeatWhileStack.push_back(RWS);

    } else if (auto *FES = dyn_cast<ForEachStmt>(S)) {
      assignCounter(FES, CounterExpr::Zero());
      assignCounter(FES->getBody());

    } else if (auto *SS = dyn_cast<SwitchStmt>(S)) {
      assignCounter(SS);
      // Assign counters for cases so they're available for fallthrough.
      for (CaseStmt *Case : SS->getCases())
        assignCounter(Case);

    } else if (auto caseStmt = dyn_cast<CaseStmt>(S)) {
      if (caseStmt->getParentKind() == CaseParentKind::Switch)
        pushRegion(S);
    } else if (auto *DS = dyn_cast<DoStmt>(S)) {
      assignCounter(DS->getBody(), CounterExpr::Ref(getCurrentCounter()));
      assignCounter(DS);

    } else if (auto *DCS = dyn_cast<DoCatchStmt>(S)) {
      // The do-catch body is visited the same number of times as its parent.
      assignCounter(DCS->getBody(), CounterExpr::Ref(getCurrentCounter()));

      for (CaseStmt *Catch : DCS->getCatches())
        assignCounter(Catch->getBody());

      // Initialize the exit count of the do-catch to the entry count, then
      // subtract off non-local exits as they are visited.
      assignCounter(DCS, CounterExpr::Ref(getCurrentCounter()));
      DoCatchStack.push_back(DCS);
    }
    return {true, S};
  }

  Stmt *walkToStmtPost(Stmt *S) override {
    if (S->isImplicit() && S != ImplicitTopLevelBody)
      return S;

    if (isa<BraceStmt>(S)) {
      if (hasCounter(S)) {
        CounterExpr *Adjust = setExitCount(S);
        popRegions(S);
        adjustForNonLocalExits(S, Adjust);
      }

    } else if (auto *WS = dyn_cast<WhileStmt>(S)) {
      // Update the condition with the backedge count.
      if (auto *E = getConditionNode(WS->getCond()))
        addToCounter(E, getExitCounter());

    } else if (auto *RWS = dyn_cast<RepeatWhileStmt>(S)) {
      assert(RepeatWhileStack.back() == RWS && "Malformed repeat-while stack");
      (void)RWS;
      RepeatWhileStack.pop_back();

    } else if (auto *CS = dyn_cast<ContinueStmt>(S)) {
      // Continues create extra backedges, add them to the appropriate counters.
      if (!isa<RepeatWhileStmt>(CS->getTarget()))
        addToCounter(CS->getTarget(), getCurrentCounter());
      if (auto *WS = dyn_cast<WhileStmt>(CS->getTarget())) {
        if (auto *E = getConditionNode(WS->getCond()))
          addToCounter(E, getCurrentCounter());
      }
      terminateRegion(S);

    } else if (auto *BS = dyn_cast<BreakStmt>(S)) {
      // When we break from a loop, we need to adjust the exit count.
      Stmt *BreakTarget = BS->getTarget();
      if (auto *RWS = dyn_cast<RepeatWhileStmt>(BreakTarget)) {
        subtractFromCounter(RWS->getCond(), getCurrentCounter());
      } else if (!isa<SwitchStmt>(BreakTarget)) {
        addToCounter(BS->getTarget(), getCurrentCounter());
      }

      // The break also affects the exit counts of active do-catch statements.
      for (auto *DCS : DoCatchStack)
        subtractFromCounter(DCS, getCurrentCounter());

      terminateRegion(S);

    } else if (auto *FS = dyn_cast<FallthroughStmt>(S)) {
      addToCounter(FS->getFallthroughDest(), getCurrentCounter());
      terminateRegion(S);

    } else if (isa<SwitchStmt>(S)) {
      replaceCount(CounterExpr::Ref(getCounter(S)), getEndLoc(S));

    } else if (auto caseStmt = dyn_cast<CaseStmt>(S)) {
      if (caseStmt->getParentKind() == CaseParentKind::Switch)
        popRegions(S);

    } else if (auto *DCS = dyn_cast<DoCatchStmt>(S)) {
      assert(DoCatchStack.back() == DCS && "Malformed do-catch stack");
      DoCatchStack.pop_back();
      replaceCount(CounterExpr::Ref(getCounter(S)), getEndLoc(S));

    } else if (isa<ReturnStmt>(S) || isa<FailStmt>(S) || isa<ThrowStmt>(S)) {
      // When we return, adjust loop condition counts and do-catch exit counts
      // to reflect the early exit.
      if (isa<ReturnStmt>(S) || isa<FailStmt>(S)) {
        for (auto *RWS : RepeatWhileStack)
          subtractFromCounter(RWS->getCond(), getCurrentCounter());
        for (auto *DCS : DoCatchStack)
          subtractFromCounter(DCS, getCurrentCounter());
      }

      terminateRegion(S);
    }
    return S;
  }

  std::pair<bool, Expr *> walkToExprPre(Expr *E) override {
    if (skipExpr(E))
      return {true, E};

    // Profiling for closures should be handled separately. Do not visit
    // closure expressions twice.
    if (isa<AbstractClosureExpr>(E) && !Parent.isNull())
      return {false, E};

    if (!RegionStack.empty())
      extendRegion(E);

    // If AST visitation begins with an expression, the region stack must be
    // empty. Set up a region for the root.
    if (Parent.isNull()) {
      assert(RegionStack.empty() &&
             "Mapped a region before visiting the root?");
      assignCounter(E);
      pushRegion(E);
    }

    // If there isn't an active region, we may be visiting a default
    // initializer for a function argument.
    if (!RegionStack.empty()) {
      if (auto *IE = dyn_cast<IfExpr>(E)) {
        CounterExpr &ThenCounter = assignCounter(IE->getThenExpr());
        assignCounter(IE->getElseExpr(),
                      CounterExpr::Sub(getCurrentCounter(), ThenCounter));
      }
    }

    if (hasCounter(E) && !Parent.isNull())
      pushRegion(E);
    return {true, E};
  }

  Expr *walkToExprPost(Expr *E) override {
    if (hasCounter(E))
      popRegions(E);

    return E;
  }
};

} // end anonymous namespace

static llvm::GlobalValue::LinkageTypes
getEquivalentPGOLinkage(FormalLinkage Linkage) {
  switch (Linkage) {
  case FormalLinkage::PublicUnique:
  case FormalLinkage::PublicNonUnique:
    return llvm::GlobalValue::ExternalLinkage;

  case FormalLinkage::HiddenUnique:
  case FormalLinkage::Private:
    return llvm::GlobalValue::PrivateLinkage;
  }

  llvm_unreachable("Unhandled FormalLinkage in switch.");
}

static StringRef getCurrentFileName(ASTNode N, SILDeclRef forDecl) {
  DeclContext *Ctx = getProfilerContextForDecl(N, forDecl);
  if (auto *ParentFile = Ctx->getParentSourceFile())
    return ParentFile->getFilename();
  return {};
}

void SILProfiler::assignRegionCounters() {
  const auto &SM = M.getASTContext().SourceMgr;

  CurrentFileName = getCurrentFileName(Root, forDecl);

  MapRegionCounters Mapper(RegionCounterMap);

  std::string CurrentFuncName;
  FormalLinkage CurrentFuncLinkage;
  if (auto *D = Root.dyn_cast<Decl *>()) {
    if (auto *AFD = dyn_cast<AbstractFunctionDecl>(D)) {
      CurrentFuncName = forDecl.mangle();
      CurrentFuncLinkage = getDeclLinkage(AFD);
    } else {
      auto *TLCD = cast<TopLevelCodeDecl>(D);
      llvm::raw_string_ostream OS{CurrentFuncName};
      OS << "__tlcd_";
      TLCD->getStartLoc().printLineAndColumn(OS, SM);
      CurrentFuncLinkage = FormalLinkage::HiddenUnique;
    }
  } else {
    CurrentFuncName = forDecl.mangle();
    CurrentFuncLinkage = FormalLinkage::HiddenUnique;
  }

  PGOFuncName = llvm::getPGOFuncName(
      CurrentFuncName, getEquivalentPGOLinkage(CurrentFuncLinkage),
      CurrentFileName);

  assert((!CurrentFuncName.empty() && !PGOFuncName.empty()) &&
         "Expected covered region to be named");

  LLVM_DEBUG(llvm::dbgs() << "Assigning counters to: " << CurrentFuncName
                          << "\n");
  Root.walk(Mapper);

  NumRegionCounters = Mapper.NextCounter;
  // TODO: Mapper needs to calculate a function hash as it goes.
  PGOFuncHash = 0x0;

  if (EmitCoverageMapping) {
    CoverageMapping Coverage(SM);
    Root.walk(Coverage);
    CovMap =
        Coverage.emitSourceRegions(M, CurrentFuncName, PGOFuncName, PGOFuncHash,
                                   RegionCounterMap, CurrentFileName);
  }

  if (llvm::IndexedInstrProfReader *IPR = M.getPGOReader()) {
    auto LoadedCounts = IPR->getInstrProfRecord(PGOFuncName, PGOFuncHash);
    if (auto E = LoadedCounts.takeError()) {
      llvm::handleAllErrors(std::move(E), [](const llvm::InstrProfError &Err) {
        Err.log(llvm::dbgs());
        return;
      });
      llvm::dbgs() << PGOFuncName << "\n";
      return;
    }
    PGOMapping pgoMapper(RegionLoadedCounterMap, LoadedCounts,
                         RegionCondToParentMap);
    Root.walk(pgoMapper);
  }
}

ProfileCounter SILProfiler::getExecutionCount(ASTNode Node) {
  if (!Node || !M.getPGOReader() || !hasRegionCounters()) {
    return ProfileCounter();
  }
  auto it = RegionLoadedCounterMap.find(Node);
  if (it == RegionLoadedCounterMap.end()) {
    return ProfileCounter();
  }
  return it->getSecond();
}

Optional<ASTNode> SILProfiler::getPGOParent(ASTNode Node) {
  if (!Node || !M.getPGOReader() || !hasRegionCounters()) {
    return None;
  }
  auto it = RegionCondToParentMap.find(Node);
  if (it == RegionCondToParentMap.end()) {
    return None;
  }
  return it->getSecond();
}
