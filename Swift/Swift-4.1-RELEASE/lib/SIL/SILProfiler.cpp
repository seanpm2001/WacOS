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
#include "swift/Parse/Lexer.h"
#include "swift/SIL/SILModule.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include "llvm/ProfileData/Coverage/CoverageMappingWriter.h"
#include "llvm/ProfileData/InstrProf.h"

#include <forward_list>

using namespace swift;

static bool isUnmapped(ASTNode N) {
  if (auto *E = N.dyn_cast<Expr *>()) {
    auto *CE = dyn_cast<ClosureExpr>(E);
    return !CE || CE->isImplicit() || !CE->getBody();
  }

  auto *D = N.get<Decl *>();
  if (auto *AFD = dyn_cast<AbstractFunctionDecl>(D)) {
    if (!AFD->getBody())
      return true;

    if (auto *fd = dyn_cast<FuncDecl>(AFD))
      if (fd->isImplicit() && fd->isGetter())
        return false;
  }

  if (isa<ConstructorDecl>(D) || isa<DestructorDecl>(D))
    return false;

  return D->isImplicit() || isa<EnumCaseDecl>(D);
}

/// A simple heuristic to determine whether \p E contains a definition of a
/// closure. This is not complete, but it suffices to cheaply filter away some
/// redundant coverage mappings.
static bool containsClosure(Expr *E) {
  Expr *candidateExpr = E;
  if (auto *ce = dyn_cast<CallExpr>(E))
    candidateExpr = ce->getDirectCallee();
  return dyn_cast_or_null<AbstractClosureExpr>(candidateExpr);
}

/// Walk the non-static initializers in \p PBD.
static void walkPatternForProfiling(PatternBindingDecl *PBD, ASTWalker &Walker,
                                    bool AllowClosures = true) {
  if (PBD && !PBD->isStatic())
    for (auto E : PBD->getPatternList())
      if (E.getInit())
        if (AllowClosures || !containsClosure(E.getInit()))
          E.getInit()->walk(Walker);
}

/// Walk the AST of \c Root and related nodes that are relevant for profiling.
static void walkFunctionForProfiling(AbstractFunctionDecl *Root,
                                     ASTWalker &Walker) {
  Root->walk(Walker);

  // We treat non-closure member initializers as part of the constructor for
  // profiling.
  if (auto *CD = dyn_cast<ConstructorDecl>(Root)) {
    auto *NominalType =
        CD->getDeclContext()->getAsNominalTypeOrNominalTypeExtensionContext();
    for (auto *Member : NominalType->getMembers()) {
      // Find pattern binding declarations that have initializers.
      if (auto *PBD = dyn_cast<PatternBindingDecl>(Member))
        walkPatternForProfiling(PBD, Walker, /*AllowClosures=*/false);
    }
  }
}

/// Walk \p D for profiling.
static void walkForProfiling(ASTNode N, ASTWalker &Walker) {
  if (auto *D = N.dyn_cast<Decl *>()) {
    if (auto *AFD = dyn_cast<AbstractFunctionDecl>(D))
      walkFunctionForProfiling(AFD, Walker);
    else if (auto *PBD = dyn_cast<PatternBindingDecl>(D))
      walkPatternForProfiling(PBD, Walker);
    else if (auto *TLCD = dyn_cast<TopLevelCodeDecl>(D))
      TLCD->walk(Walker);
  } else if (auto *E = N.dyn_cast<Expr *>()) {
    cast<ClosureExpr>(E)->walk(Walker);
  }
}

SILProfiler *SILProfiler::create(SILModule &M, ASTNode N) {
  if (auto *D = N.dyn_cast<Decl *>()) {
    assert(isa<AbstractFunctionDecl>(D) ||
           isa<TopLevelCodeDecl>(D) && "Cannot create profiler");
  } else if (auto *E = N.dyn_cast<Expr *>()) {
    assert(isa<AbstractClosureExpr>(E) && "Cannot create profiler");
  } else {
    llvm_unreachable("Invalid AST node for profiling");
  }

  const auto &Opts = M.getOptions();
  if ((!Opts.GenerateProfile && Opts.UseProfile.empty()) || isUnmapped(N))
    return nullptr;

  auto *Buf = M.allocate<SILProfiler>(1);
  auto *SP = ::new (Buf) SILProfiler(M, N, Opts.EmitProfileCoverageMapping);
  SP->assignRegionCounters();
  return SP;
}

namespace {

/// An ASTWalker that maps ASTNodes to profiling counters.
struct MapRegionCounters : public ASTWalker {
  /// The next counter value to assign.
  unsigned NextCounter;

  /// The map of statements to counters.
  llvm::DenseMap<ASTNode, unsigned> &CounterMap;

  MapRegionCounters(llvm::DenseMap<ASTNode, unsigned> &CounterMap)
      : NextCounter(0), CounterMap(CounterMap) {}

  bool walkToDeclPre(Decl *D) override {
    if (isUnmapped(D))
      return false;
    if (auto *AFD = dyn_cast<AbstractFunctionDecl>(D))
      CounterMap[AFD->getBody()] = NextCounter++;
    if (auto *TLCD = dyn_cast<TopLevelCodeDecl>(D))
      CounterMap[TLCD->getBody()] = NextCounter++;
    return true;
  }

  std::pair<bool, Stmt *> walkToStmtPre(Stmt *S) override {
    if (auto *IS = dyn_cast<IfStmt>(S)) {
      CounterMap[IS->getThenStmt()] = NextCounter++;
    } else if (auto *US = dyn_cast<GuardStmt>(S)) {
      CounterMap[US->getBody()] = NextCounter++;
    } else if (auto *WS = dyn_cast<WhileStmt>(S)) {
      CounterMap[WS->getBody()] = NextCounter++;
    } else if (auto *RWS = dyn_cast<RepeatWhileStmt>(S)) {
      CounterMap[RWS->getBody()] = NextCounter++;
    } else if (auto *FES = dyn_cast<ForEachStmt>(S)) {
      CounterMap[FES->getBody()] = NextCounter++;
      walkPatternForProfiling(FES->getIterator(), *this);
    } else if (auto *SS = dyn_cast<SwitchStmt>(S)) {
      CounterMap[SS] = NextCounter++;
    } else if (auto *CS = dyn_cast<CaseStmt>(S)) {
      CounterMap[CS] = NextCounter++;
    } else if (auto *DCS = dyn_cast<DoCatchStmt>(S)) {
      CounterMap[DCS] = NextCounter++;
    } else if (auto *CS = dyn_cast<CatchStmt>(S)) {
      CounterMap[CS->getBody()] = NextCounter++;
    }
    return {true, S};
  }

  std::pair<bool, Expr *> walkToExprPre(Expr *E) override {
    if (auto *IE = dyn_cast<IfExpr>(E)) {
      CounterMap[IE->getThenExpr()] = NextCounter++;
    } else if (isa<AutoClosureExpr>(E) || isa<ClosureExpr>(E)) {
      CounterMap[E] = NextCounter++;
    }
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
};

/// \brief A region of source code that can be mapped to a counter.
class SourceMappingRegion {
  ASTNode Node;

  CounterExpr *Count;

  /// \brief The region's starting location.
  Optional<SourceLoc> StartLoc;

  /// \brief The region's ending location.
  Optional<SourceLoc> EndLoc;

public:
  SourceMappingRegion(ASTNode Node, CounterExpr &Count,
                      Optional<SourceLoc> StartLoc, Optional<SourceLoc> EndLoc)
      : Node(Node), Count(&Count), StartLoc(StartLoc), EndLoc(EndLoc) {}

  SourceMappingRegion(SourceMappingRegion &&Region) = default;
  SourceMappingRegion &operator=(SourceMappingRegion &&RHS) = default;

  ASTNode getNode() const { return Node; }

  CounterExpr &getCounter() const { return *Count; }

  bool hasStartLoc() const { return StartLoc.hasValue(); }

  void setStartLoc(SourceLoc Loc) { StartLoc = Loc; }

  const SourceLoc &getStartLoc() const {
    assert(StartLoc && "Region has no start location");
    return *StartLoc;
  }

  bool hasEndLoc() const { return EndLoc.hasValue(); }

  void setEndLoc(SourceLoc Loc) { EndLoc = Loc; }

  const SourceLoc &getEndLoc() const {
    assert(EndLoc && "Region has no end location");
    return *EndLoc;
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
      auto node = AFD->getBody();
      CounterMap[node] = NextCounter++;
      auto count = loadExecutionCount(node);
      LoadedCounterMap[node] = count;
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
      walkPatternForProfiling(FES->getIterator(), *this);
    } else if (auto *SS = dyn_cast<SwitchStmt>(S)) {
      CounterMap[SS] = NextCounter++;
      auto ssCount = loadExecutionCount(SS);
      LoadedCounterMap[SS] = ssCount;
    } else if (auto *CS = dyn_cast<CaseStmt>(S)) {
      CounterMap[CS] = NextCounter++;
      auto csCount = loadExecutionCount(CS);
      LoadedCounterMap[CS] = csCount;
    } else if (auto *DCS = dyn_cast<DoCatchStmt>(S)) {
      CounterMap[DCS] = NextCounter++;
      auto dcsCount = loadExecutionCount(DCS);
      LoadedCounterMap[DCS] = dcsCount;
    } else if (auto *CS = dyn_cast<CatchStmt>(S)) {
      auto csBody = CS->getBody();
      CounterMap[csBody] = NextCounter++;
      auto csBodyCount = loadExecutionCount(csBody);
      LoadedCounterMap[csBody] = csBodyCount;
    }
    return {true, S};
  }

  std::pair<bool, Expr *> walkToExprPre(Expr *E) override {
    unsigned parent = getParentCounter();
    if (auto *IE = dyn_cast<IfExpr>(E)) {
      auto thenExpr = IE->getThenExpr();
      CounterMap[thenExpr] = NextCounter++;
      auto thenCount = loadExecutionCount(thenExpr);
      LoadedCounterMap[thenExpr] = thenCount;
      if (auto elseExpr = IE->getElseExpr()) {
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
    } else if (isa<AutoClosureExpr>(E) || isa<ClosureExpr>(E)) {
      CounterMap[E] = NextCounter++;
      auto eCount = loadExecutionCount(E);
      LoadedCounterMap[E] = eCount;
    }
    return {true, E};
  }
};

struct CoverageMapping : public ASTWalker {
private:
  const SourceManager &SM;

  /// \brief Storage for counter expressions.
  std::forward_list<CounterExpr> Exprs;

  /// \brief The map of statements to counter expressions.
  llvm::DenseMap<ASTNode, CounterExpr *> CounterMap;

  /// \brief The source mapping regions for this function.
  std::vector<SourceMappingRegion> SourceRegions;

  /// \brief A stack of currently live regions.
  std::vector<SourceMappingRegion> RegionStack;

  /// \brief A stack of active repeat-while loops.
  std::vector<RepeatWhileStmt *> RepeatWhileStack;

  CounterExpr *ExitCounter;

  /// \brief Return true if \c Node has an associated counter.
  bool hasCounter(ASTNode Node) { return CounterMap.count(Node); }

  /// \brief Return the region counter for \c Node.
  ///
  /// This should only be called on statements that have a dedicated counter.
  CounterExpr &getCounter(ASTNode Node) {
    assert(CounterMap.count(Node) && "No counter found");
    return *CounterMap[Node];
  }

  /// \brief Create a counter expression.
  CounterExpr &createCounter(CounterExpr &&Expr) {
    Exprs.push_front(std::move(Expr));
    return Exprs.front();
  }

  /// \brief Create a counter expression for \c Node and add it to the map.
  CounterExpr &assignCounter(ASTNode Node, CounterExpr &&Expr) {
    assert(Node && "Assigning counter expression to non-existent AST node");
    CounterExpr &Result = createCounter(std::move(Expr));
    CounterMap[Node] = &Result;
    return Result;
  }

  /// \brief Create a counter expression referencing \c Node's own counter.
  CounterExpr &assignCounter(ASTNode Node) {
    return assignCounter(Node, CounterExpr::Leaf(Node));
  }

  /// \brief Add \c Expr to \c Node's counter.
  void addToCounter(ASTNode Node, CounterExpr &Expr) {
    CounterExpr &Counter = getCounter(Node);
    if (const CounterExpr *ReferencedCounter = Counter.getReferencedNode())
      Counter = CounterExpr::Add(*ReferencedCounter, Expr);
    else if (Counter.isZero())
      Counter = CounterExpr::Ref(Expr);
    else
      Counter = CounterExpr::Add(createCounter(std::move(Counter)), Expr);
  }

  /// \brief Subtract \c Expr from \c Node's counter.
  void subtractFromCounter(ASTNode Node, CounterExpr &Expr) {
    CounterExpr &Counter = getCounter(Node);
    assert(!Counter.isZero() && "Cannot create a negative counter");
    if (const CounterExpr *ReferencedCounter = Counter.getReferencedNode())
      Counter = CounterExpr::Sub(*ReferencedCounter, Expr);
    else
      Counter = CounterExpr::Sub(createCounter(std::move(Counter)), Expr);
  }

  /// \brief Return the current region's counter.
  CounterExpr &getCurrentCounter() { return getRegion().getCounter(); }

  /// \brief Get the counter from the end of the most recent scope.
  CounterExpr &getExitCounter() {
    assert(ExitCounter && "no exit counter available");
    return *ExitCounter;
  }

  /// \brief Set the exit count so we can leave the scope related to \c Node
  ///
  /// Returns the delta of the count on entering \c Node and exiting, or null if
  /// there was no change.
  CounterExpr *setExitCount(ASTNode Node) {
    ExitCounter = &getCurrentCounter();
    if (hasCounter(Node) && ExitCounter != &getCounter(Node))
      return &createCounter(CounterExpr::Sub(getCounter(Node), *ExitCounter));
    return nullptr;
  }

  /// \brief Adjust the count for control flow when exiting a scope.
  void adjustForNonLocalExits(ASTNode Scope, CounterExpr *ControlFlowAdjust) {
    if (Parent.getAsDecl())
      return;

    CounterExpr *JumpsToLabel = nullptr;
    Stmt *ParentStmt = Parent.getAsStmt();
    if (ParentStmt) {
      if (isa<DoStmt>(ParentStmt) || isa<DoCatchStmt>(ParentStmt) ||
          isa<CatchStmt>(ParentStmt))
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

  /// \brief Push a region covering \c Node onto the stack.
  void pushRegion(ASTNode Node) {
    RegionStack.emplace_back(Node, getCounter(Node), Node.getStartLoc(),
                             getEndLoc(Node));
  }

  /// \brief Replace the current region's count by pushing an incomplete region.
  void replaceCount(CounterExpr &&Expr, Optional<SourceLoc> Start = None) {
    CounterExpr &Counter = createCounter(std::move(Expr));
    RegionStack.emplace_back(ASTNode(), Counter, Start, None);
  }

  /// \brief Get the location for the end of the last token in \c Node.
  SourceLoc getEndLoc(ASTNode Node) {
    return Lexer::getLocForEndOfToken(SM, Node.getEndLoc());
  }

  /// \brief Pop regions from the stack into the function's list of regions.
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

    SourceRegions.push_back(std::move(*I++));
    for (; I != E; ++I) {
      if (!I->hasStartLoc())
        continue;
      if (!I->hasEndLoc())
        I->setEndLoc(EndLoc);
      SourceRegions.push_back(std::move(*I));
    }

    RegionStack.erase(ParentIt, E);
  }

  /// \brief Return the currently active region.
  SourceMappingRegion &getRegion() {
    assert(!RegionStack.empty() && "statement has no region");
    return RegionStack.back();
  }

  /// \brief Ensure that \c S is included in the current region.
  void extendRegion(ASTNode S) {
    SourceMappingRegion &Region = getRegion();
    SourceLoc StartLoc = S.getStartLoc();
    if (!Region.hasStartLoc())
      Region.setStartLoc(StartLoc);
  }

  /// \brief Mark \c S as a terminator, starting a zero region.
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

  /// \brief Generate the coverage counter mapping regions from collected
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

      auto Start = SM.getLineAndColumn(Region.getStartLoc());
      auto End = SM.getLineAndColumn(Region.getEndLoc());
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
    if (auto *AFD = dyn_cast<AbstractFunctionDecl>(D))
      assignCounter(AFD->getBody());
    else if (auto *TLCD = dyn_cast<TopLevelCodeDecl>(D))
      assignCounter(TLCD->getBody());
    return true;
  }

  std::pair<bool, Stmt *> walkToStmtPre(Stmt *S) override {
    if (S->isImplicit())
      return {true, S};

    if (!RegionStack.empty())
      extendRegion(S);

    if (auto *BS = dyn_cast<BraceStmt>(S)) {
      if (hasCounter(BS))
        pushRegion(BS);

    } else if (auto *IS = dyn_cast<IfStmt>(S)) {
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
      walkPatternForProfiling(FES->getIterator(), *this);

    } else if (auto *SS = dyn_cast<SwitchStmt>(S)) {
      assignCounter(SS);
      // Assign counters for cases so they're available for fallthrough.
      for (CaseStmt *Case : SS->getCases())
        assignCounter(Case);

    } else if (isa<CaseStmt>(S)) {
      pushRegion(S);

    } else if (auto *DS = dyn_cast<DoStmt>(S)) {
      assignCounter(DS->getBody(), CounterExpr::Ref(getCurrentCounter()));
      assignCounter(DS);

    } else if (auto *DCS = dyn_cast<DoCatchStmt>(S)) {
      assignCounter(DCS->getBody(), CounterExpr::Ref(getCurrentCounter()));
      assignCounter(DCS);

    } else if (auto *CS = dyn_cast<CatchStmt>(S)) {
      assignCounter(CS->getBody());
    }
    return {true, S};
  }

  Stmt *walkToStmtPost(Stmt *S) override {
    if (S->isImplicit())
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
      if (auto *RWS = dyn_cast<RepeatWhileStmt>(BS->getTarget())) {
        subtractFromCounter(RWS->getCond(), getCurrentCounter());
      } else if (!isa<SwitchStmt>(BS->getTarget())) {
        addToCounter(BS->getTarget(), getCurrentCounter());
      }
      terminateRegion(S);

    } else if (auto *FS = dyn_cast<FallthroughStmt>(S)) {
      addToCounter(FS->getFallthroughDest(), getCurrentCounter());
      terminateRegion(S);

    } else if (isa<SwitchStmt>(S)) {
      replaceCount(CounterExpr::Ref(getCounter(S)), getEndLoc(S));

    } else if (isa<CaseStmt>(S)) {
      popRegions(S);

    } else if (isa<DoCatchStmt>(S)) {
      replaceCount(CounterExpr::Ref(getCounter(S)), getEndLoc(S));

    } else if (isa<ReturnStmt>(S) || isa<FailStmt>(S) || isa<ThrowStmt>(S)) {
      // When we return, we may need to adjust some loop condition counts.
      for (auto *RWS : RepeatWhileStack)
        subtractFromCounter(RWS->getCond(), getCurrentCounter());

      terminateRegion(S);
    }
    return S;
  }

  std::pair<bool, Expr *> walkToExprPre(Expr *E) override {
    if (!RegionStack.empty())
      extendRegion(E);

    if (isa<AutoClosureExpr>(E)) {
      // Autoclosures look strange if there isn't a region, since it looks like
      // control flow starts partway through an expression. For now we skip
      // these so we don't get odd behavior in default arguments and the like,
      // but in the future we should consider creating appropriate regions for
      // those expressions.
      if (!RegionStack.empty())
        assignCounter(E);
    } else if (isa<ClosureExpr>(E)) {
      assignCounter(E);
    } else if (auto *IE = dyn_cast<IfExpr>(E)) {
      CounterExpr &ThenCounter = assignCounter(IE->getThenExpr());
      if (RegionStack.empty())
        assignCounter(IE->getElseExpr());
      else
        assignCounter(IE->getElseExpr(),
                      CounterExpr::Sub(getCurrentCounter(), ThenCounter));
    }

    if (hasCounter(E))
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
  case FormalLinkage::HiddenNonUnique:
  case FormalLinkage::Private:
    return llvm::GlobalValue::PrivateLinkage;
  }

  llvm_unreachable("Unhandled FormalLinkage in switch.");
}

static StringRef getCurrentFileName(ASTNode Root) {
  DeclContext *Ctx = Root.getAsDeclContext();
  if (auto *ParentFile = Ctx->getParentSourceFile())
    return ParentFile->getFilename();
  return {};
}

void SILProfiler::assignRegionCounters() {
  const auto &SM = M.getASTContext().SourceMgr;

  CurrentFileName = getCurrentFileName(Root);

  MapRegionCounters Mapper(RegionCounterMap);

  std::string CurrentFuncName;
  FormalLinkage CurrentFuncLinkage;
  if (auto *D = Root.dyn_cast<Decl *>()) {
    if (auto *AFD = dyn_cast<AbstractFunctionDecl>(D)) {
      CurrentFuncName = SILDeclRef(AFD).mangle();
      CurrentFuncLinkage = getDeclLinkage(AFD);
    } else if (auto *TLCD = dyn_cast<TopLevelCodeDecl>(D)) {
      llvm::raw_string_ostream OS{CurrentFuncName};
      OS << "__tlcd_";
      TLCD->getStartLoc().printLineAndColumn(OS, SM);
      CurrentFuncLinkage = FormalLinkage::HiddenUnique;
    } else {
      llvm_unreachable("Unsupported decl");
    }
  } else {
    auto *E = Root.get<Expr *>();
    if (auto *CE = dyn_cast<ClosureExpr>(E)) {
      CurrentFuncName = SILDeclRef(CE).mangle();
      CurrentFuncLinkage = FormalLinkage::HiddenUnique;
    } else {
      llvm_unreachable("Unsupported expr");
    }
  }

  PGOFuncName = llvm::getPGOFuncName(
      CurrentFuncName, getEquivalentPGOLinkage(CurrentFuncLinkage),
      CurrentFileName);

  walkForProfiling(Root, Mapper);

  NumRegionCounters = Mapper.NextCounter;
  // TODO: Mapper needs to calculate a function hash as it goes.
  PGOFuncHash = 0x0;

  if (EmitCoverageMapping) {
    CoverageMapping Coverage(SM);
    walkForProfiling(Root, Coverage);
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
    walkForProfiling(Root, pgoMapper);
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

void SILProfiler::recordCounterUpdate() {
  // If a counter update is recorded, the profile symbol table is guaranteed
  // to have name data needed by the coverage mapping.
  if (CovMap)
    CovMap->setSymtabEntryGuaranteed();
}
