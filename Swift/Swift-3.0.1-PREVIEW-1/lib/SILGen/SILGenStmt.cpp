//===--- SILGenStmt.cpp - Implements Lowering of ASTs -> SIL for Stmts ----===//
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

#include "SILGen.h"
#include "Scope.h"
#include "Condition.h"
#include "Initialization.h"
#include "LValue.h"
#include "RValue.h"
#include "swift/AST/AST.h"
#include "swift/SIL/SILArgument.h"
#include "swift/AST/DiagnosticsSIL.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace swift;
using namespace Lowering;

template<typename...T, typename...U>
static void diagnose(ASTContext &Context, SourceLoc loc, Diag<T...> diag,
                     U &&...args) {
  Context.Diags.diagnose(loc,
                         diag, std::forward<U>(args)...);
}

SILBasicBlock *SILGenFunction::createBasicBlock(SILBasicBlock *afterBB) {
  // Honor an explicit placement if given.
  if (afterBB) {
    return new (F.getModule()) SILBasicBlock(&F, afterBB);

  // If we don't have a requested placement, but we do have a current
  // insertion point, insert there.
  } else if (B.hasValidInsertionPoint()) {
    return new (F.getModule()) SILBasicBlock(&F, B.getInsertionBB());

  // Otherwise, insert at the end of the current section.
  } else {
    return createBasicBlock(CurFunctionSection);
  }
}

SILBasicBlock *SILGenFunction::createBasicBlock(FunctionSection section) {
  switch (section) {
  case FunctionSection::Ordinary: {
    // The end of the ordinary section is just the end of the function
    // unless postmatter blocks exist.
    SILBasicBlock *afterBB =
        (StartOfPostmatter ? &*std::prev(StartOfPostmatter->getIterator())
                           : nullptr);
    return new (F.getModule()) SILBasicBlock(&F, afterBB);
  }

  case FunctionSection::Postmatter: {
    // The end of the postmatter section is always the end of the function.
    // Register the new block as the start of the postmatter if needed.
    SILBasicBlock *newBB = new (F.getModule()) SILBasicBlock(&F, nullptr);
    if (!StartOfPostmatter) StartOfPostmatter = newBB;
    return newBB;
  }

  }
  llvm_unreachable("bad function section");
}

void SILGenFunction::eraseBasicBlock(SILBasicBlock *block) {
  assert(block->pred_empty() && "erasing block with predecessors");
  assert(block->empty() && "erasing block with content");
  if (block == StartOfPostmatter) {
    StartOfPostmatter = &*std::next(block->getIterator());
  }
  block->eraseFromParent();
}

//===----------------------------------------------------------------------===//
// SILGenFunction emitStmt implementation
//===----------------------------------------------------------------------===//

namespace {
  class StmtEmitter : public Lowering::ASTVisitor<StmtEmitter> {
    SILGenFunction &SGF;
  public:
    StmtEmitter(SILGenFunction &sgf) : SGF(sgf) {}
#define STMT(ID, BASE) void visit##ID##Stmt(ID##Stmt *S);
#include "swift/AST/StmtNodes.def"

    ASTContext &getASTContext() { return SGF.getASTContext(); }

    SILBasicBlock *createBasicBlock() { return SGF.createBasicBlock(); }

    template <class... Args>
    JumpDest createJumpDest(Stmt *cleanupLoc, Args... args) {
      return JumpDest(SGF.createBasicBlock(args...),
                      SGF.getCleanupsDepth(),
                      CleanupLocation(cleanupLoc));
    }
  };
}

void SILGenFunction::emitStmt(Stmt *S) {
  StmtEmitter(*this).visit(S);
}

/// getOrEraseBlock - If there are branches to the specified JumpDest,
/// return the block, otherwise return NULL. The JumpDest must be valid.
static SILBasicBlock *getOrEraseBlock(SILGenFunction &SGF, JumpDest &dest) {
  SILBasicBlock *BB = dest.takeBlock();
  if (BB->pred_empty()) {
    // If the block is unused, we don't need it; just delete it.
    SGF.eraseBasicBlock(BB);
    return nullptr;
  }
  return BB;
}

/// emitOrDeleteBlock - If there are branches to the specified JumpDest,
/// emit it per emitBlock.  If there aren't, then just delete the block - it
/// turns out to have not been needed.
static void emitOrDeleteBlock(SILGenFunction &SGF, JumpDest &dest,
                              SILLocation BranchLoc) {
  // If we ever add a single-use optimization here (to just continue
  // the predecessor instead of branching to a separate block), we'll
  // need to update visitDoCatchStmt so that code like:
  //   try { throw x } catch _ { }
  // doesn't leave us emitting the rest of the function in the
  // postmatter section.
  SILBasicBlock *BB = getOrEraseBlock(SGF, dest);
  if (BB != nullptr)
    SGF.B.emitBlock(BB, BranchLoc);
}

Condition SILGenFunction::emitCondition(Expr *E,
                                        bool hasFalseCode, bool invertValue,
                                        ArrayRef<SILType> contArgs) {
  assert(B.hasValidInsertionPoint() &&
         "emitting condition at unreachable point");

  // Sema forces conditions to have Builtin.i1 type, which guarantees this.
  SILValue V;
  {
    FullExpr Scope(Cleanups, CleanupLocation(E));
    V = emitRValue(E).forwardAsSingleValue(*this, E);
  }
  assert(V->getType().castTo<BuiltinIntegerType>()->isFixedWidth(1));

  return emitCondition(V, E, hasFalseCode, invertValue, contArgs);
}



Condition SILGenFunction::emitCondition(SILValue V, SILLocation Loc,
                                        bool hasFalseCode, bool invertValue,
                                        ArrayRef<SILType> contArgs) {
  assert(B.hasValidInsertionPoint() &&
         "emitting condition at unreachable point");

  SILBasicBlock *ContBB = createBasicBlock();

  for (SILType argTy : contArgs) {
    new (F.getModule()) SILArgument(ContBB, argTy);
  }
  
  SILBasicBlock *FalseBB, *FalseDestBB;
  if (hasFalseCode) {
    FalseBB = FalseDestBB = createBasicBlock();
  } else {
    FalseBB = nullptr;
    FalseDestBB = ContBB;
  }

  SILBasicBlock *TrueBB = createBasicBlock();

  if (invertValue)
    B.createCondBranch(Loc, V, FalseDestBB, TrueBB);
  else
    B.createCondBranch(Loc, V, TrueBB, FalseDestBB);
  
  return Condition(TrueBB, FalseBB, ContBB, Loc);
}

void StmtEmitter::visitBraceStmt(BraceStmt *S) {
  // Enter a new scope.
  LexicalScope BraceScope(SGF.Cleanups, SGF, CleanupLocation(S));
  // Keep in sync with DiagnosticsSIL.def.
  const unsigned ReturnStmtType   = 0;
  const unsigned BreakStmtType    = 1;
  const unsigned ContinueStmtType = 2;
  const unsigned ThrowStmtType    = 3;
  const unsigned UnknownStmtType  = 4;
  unsigned StmtType = UnknownStmtType;
  
  for (auto &ESD : S->getElements()) {
    
    if (auto S = ESD.dyn_cast<Stmt*>())
      if (isa<IfConfigStmt>(S))
        continue;
    
    // If we ever reach an unreachable point, stop emitting statements and issue
    // an unreachable code diagnostic.
    if (!SGF.B.hasValidInsertionPoint()) {
      // If this is an implicit statement or expression, just skip over it,
      // don't emit a diagnostic here.
      if (Stmt *S = ESD.dyn_cast<Stmt*>()) {
        if (S->isImplicit()) continue;
      } else if (Expr *E = ESD.dyn_cast<Expr*>()) {
        if (E->isImplicit()) continue;
      }
      
      if (StmtType != UnknownStmtType) {
        diagnose(getASTContext(), ESD.getStartLoc(),
                 diag::unreachable_code_after_stmt, StmtType);
      } else {
        diagnose(getASTContext(), ESD.getStartLoc(),
                 diag::unreachable_code);
      }
      return;
    }

    // Process children.
    if (Stmt *S = ESD.dyn_cast<Stmt*>()) {
      visit(S);
      if (isa<ReturnStmt>(S))
        StmtType = ReturnStmtType;
      if (isa<BreakStmt>(S))
        StmtType = BreakStmtType;
      if (isa<ContinueStmt>(S))
        StmtType = ContinueStmtType;
      if (isa<ThrowStmt>(S))
        StmtType = ThrowStmtType;
    } else if (Expr *E = ESD.dyn_cast<Expr*>()) {
      SGF.emitIgnoredExpr(E);
    } else {
      SGF.visit(ESD.get<Decl*>());
    }
  }
}

namespace {
  class StoreResultInitialization : public Initialization {
    SILValue &Storage;
    SmallVectorImpl<CleanupHandle> &Cleanups;
  public:
    StoreResultInitialization(SILValue &storage,
                              SmallVectorImpl<CleanupHandle> &cleanups)
      : Storage(storage), Cleanups(cleanups) {}

    SILValue getAddressOrNull() const override { return SILValue(); }
    void copyOrInitValueInto(SILGenFunction &gen, SILLocation loc,
                             ManagedValue value, bool isInit) override {
      Storage = value.getValue();
      auto cleanup = value.getCleanup();
      if (cleanup.isValid()) Cleanups.push_back(cleanup);
    }
  };
}

static InitializationPtr
prepareIndirectResultInit(SILGenFunction &gen, CanType resultType,
                          ArrayRef<SILResultInfo> &allResults,
                          MutableArrayRef<SILValue> &directResults,
                          ArrayRef<SILArgument*> &indirectResultAddrs,
                          SmallVectorImpl<CleanupHandle> &cleanups) {
  // Recursively decompose tuple types.
  if (auto resultTupleType = dyn_cast<TupleType>(resultType)) {
    auto tupleInit = new TupleInitialization();
    tupleInit->SubInitializations.reserve(resultTupleType->getNumElements());

    for (auto resultEltType : resultTupleType.getElementTypes()) {
      auto eltInit = prepareIndirectResultInit(gen, resultEltType, allResults,
                                               directResults,
                                               indirectResultAddrs, cleanups);
      tupleInit->SubInitializations.push_back(std::move(eltInit));
    }

    return InitializationPtr(tupleInit);
  }

  // Okay, pull the next result off the list of results.
  auto result = allResults[0];
  allResults = allResults.slice(1);

  // If it's indirect, we should be emitting into an argument.
  if (result.isIndirect()) {
    // Pull off the next indirect result argument.
    SILValue addr = indirectResultAddrs.front();
    indirectResultAddrs = indirectResultAddrs.slice(1);

    // Create an initialization which will initialize it.
    auto &resultTL = gen.getTypeLowering(addr->getType());
    auto temporary = gen.useBufferAsTemporary(addr, resultTL);

    // Remember the cleanup that will be activated.
    auto cleanup = temporary->getInitializedCleanup();
    if (cleanup.isValid())
      cleanups.push_back(cleanup);

    return InitializationPtr(temporary.release());
  }

  // Otherwise, make an Initialization that stores the value in the
  // next element of the directResults array.
  auto init = new StoreResultInitialization(directResults[0], cleanups);
  directResults = directResults.slice(1);
  return InitializationPtr(init);
}

/// Prepare an Initialization that will initialize the result of the
/// current function.
///
/// \param directResultsBuffer - will be filled with the direct
///   components of the result
/// \param cleanups - will be filled (after initialization completes)
///   with all the active cleanups managing the result values
static std::unique_ptr<Initialization>
prepareIndirectResultInit(SILGenFunction &gen, CanType formalResultType,
                          SmallVectorImpl<SILValue> &directResultsBuffer,
                          SmallVectorImpl<CleanupHandle> &cleanups) {
  auto fnType = gen.F.getLoweredFunctionType();

  // Make space in the direct-results array for all the entries we need.
  directResultsBuffer.append(fnType->getNumDirectResults(), SILValue());

  ArrayRef<SILResultInfo> allResults = fnType->getAllResults();
  MutableArrayRef<SILValue> directResults = directResultsBuffer;
  ArrayRef<SILArgument*> indirectResultAddrs = gen.F.getIndirectResults();

  auto init = prepareIndirectResultInit(gen, formalResultType, allResults,
                                        directResults, indirectResultAddrs,
                                        cleanups);

  assert(allResults.empty());
  assert(directResults.empty());
  assert(indirectResultAddrs.empty());

  return init;
}

void SILGenFunction::emitReturnExpr(SILLocation branchLoc,
                                    Expr *ret) {
  SmallVector<SILValue, 4> directResults;

  if (F.getLoweredFunctionType()->hasIndirectResults()) {
    // Indirect return of an address-only value.
    FullExpr scope(Cleanups, CleanupLocation(ret));

    // Build an initialization which recursively destructures the tuple.
    SmallVector<CleanupHandle, 4> resultCleanups;
    InitializationPtr resultInit =
      prepareIndirectResultInit(*this, ret->getType()->getCanonicalType(),
                                directResults, resultCleanups);

    // Emit the result expression into the initialization.
    emitExprInto(ret, resultInit.get());

    // Deactivate all the cleanups for the result values.
    for (auto cleanup : resultCleanups) {
      Cleanups.forwardCleanup(cleanup);
    }
  } else {
    // SILValue return.
    FullExpr scope(Cleanups, CleanupLocation(ret));
    emitRValue(ret).forwardAll(*this, directResults);
  }
  Cleanups.emitBranchAndCleanups(ReturnDest, branchLoc, directResults);
}

void StmtEmitter::visitReturnStmt(ReturnStmt *S) {
  SGF.CurrentSILLoc = S;
  SILLocation Loc = S->isImplicit() ?
                      (SILLocation)ImplicitReturnLocation(S) :
                      (SILLocation)ReturnLocation(S);

  SILValue ArgV;
  if (!S->hasResult())
    // Void return.
    SGF.Cleanups.emitBranchAndCleanups(SGF.ReturnDest, Loc);
  else
    SGF.emitReturnExpr(Loc, S->getResult());
}

void StmtEmitter::visitThrowStmt(ThrowStmt *S) {
  ManagedValue exn = SGF.emitRValueAsSingleValue(S->getSubExpr());
  SGF.emitThrow(S, exn, /* emit a call to willThrow */ true);
}


namespace {
  // This is a little cleanup that ensures that there are no jumps out of a
  // defer body.  The cleanup is only active and installed when emitting the
  // body of a defer, and it is disabled at the end.  If it ever needs to be
  // emitted, it crashes the compiler because Sema missed something.
  class DeferEscapeCheckerCleanup : public Cleanup {
    SourceLoc deferLoc;
  public:
    DeferEscapeCheckerCleanup(SourceLoc deferLoc) : deferLoc(deferLoc) {}
    void emit(SILGenFunction &SGF, CleanupLocation l) override {
      assert(false && "Sema didn't catch exit out of a defer?");
    }
  };
}


namespace {
  class DeferCleanup : public Cleanup {
    SourceLoc deferLoc;
    Expr *call;
  public:
    DeferCleanup(SourceLoc deferLoc, Expr *call)
      : deferLoc(deferLoc), call(call) {}
    void emit(SILGenFunction &SGF, CleanupLocation l) override {
      SGF.Cleanups.pushCleanup<DeferEscapeCheckerCleanup>(deferLoc);
      auto TheCleanup = SGF.Cleanups.getTopCleanup();

      SGF.emitIgnoredExpr(call);
      
      if (SGF.B.hasValidInsertionPoint())
        SGF.Cleanups.setCleanupState(TheCleanup, CleanupState::Dead);
    }
  };
}


void StmtEmitter::visitDeferStmt(DeferStmt *S) {
  // Emit the closure for the defer, along with its binding.
  SGF.visitFuncDecl(S->getTempDecl());

  // Register a cleanup to invoke the closure on any exit paths.
  SGF.Cleanups.pushCleanup<DeferCleanup>(S->getDeferLoc(), S->getCallExpr());
}




void StmtEmitter::visitIfStmt(IfStmt *S) {
  Scope condBufferScope(SGF.Cleanups, S);
  
  // Create a continuation block.  We need it if there is a labeled break out
  // of the if statement or if there is an if/then/else.
  JumpDest contDest = createJumpDest(S->getThenStmt());
  auto contBB = contDest.getBlock();

  // Set the destinations for any 'break' and 'continue' statements inside the
  // body.  Note that "continue" is not valid out of a labeled 'if'.
  SGF.BreakContinueDestStack.push_back(
                               { S, contDest, JumpDest(CleanupLocation(S)) });

  // Set up the block for the false case.  If there is an 'else' block, we make
  // a new one, otherwise it is our continue block.
  JumpDest falseDest = contDest;
  if (S->getElseStmt())
    falseDest = createJumpDest(S);
  
  // Emit the condition, along with the "then" part of the if properly guarded
  // by the condition and a jump to ContBB.  If the condition fails, jump to
  // the CondFalseBB.
  {
    // Enter a scope for any bound pattern variables.
    LexicalScope trueScope(SGF.Cleanups, SGF, S);

    SGF.emitStmtCondition(S->getCond(), falseDest, S);
    
    // In the success path, emit the 'then' part if the if.
    SGF.emitProfilerIncrement(S->getThenStmt());
    SGF.emitStmt(S->getThenStmt());
  
    // Finish the "true part" by cleaning up any temporaries and jumping to the
    // continuation block.
    if (SGF.B.hasValidInsertionPoint()) {
      RegularLocation L(S->getThenStmt());
      L.pointToEnd();
      SGF.Cleanups.emitBranchAndCleanups(contDest, L);
    }
  }
  
  // If there is 'else' logic, then emit it.
  if (S->getElseStmt()) {
    SGF.B.emitBlock(falseDest.getBlock());
    visit(S->getElseStmt());
    if (SGF.B.hasValidInsertionPoint()) {
      RegularLocation L(S->getElseStmt());
      L.pointToEnd();
      SGF.B.createBranch(L, contBB);
    }
  }

  // If the continuation block was used, emit it now, otherwise remove it.
  if (contBB->pred_empty()) {
    SGF.eraseBasicBlock(contBB);
  } else {
    RegularLocation L(S->getThenStmt());
    L.pointToEnd();
    SGF.B.emitBlock(contBB, L);
  }
  SGF.BreakContinueDestStack.pop_back();
}

void StmtEmitter::visitGuardStmt(GuardStmt *S) {
  // Create a block for the body and emit code into it before processing any of
  // the patterns, because none of the bound variables will be in scope in the
  // 'body' context.
  JumpDest bodyBB =
    JumpDest(createBasicBlock(), SGF.getCleanupsDepth(), CleanupLocation(S));

  {
    // Move the insertion point to the 'body' block temporarily and emit it.
    // Note that we don't push break/continue locations since they aren't valid
    // in this statement.
    SavedInsertionPoint savedIP(SGF, bodyBB.getBlock());
    SGF.emitProfilerIncrement(S->getBody());
    SGF.emitStmt(S->getBody());

    // The body block must end in a noreturn call, return, break etc.  It
    // isn't valid to fall off into the normal flow.  To model this, we emit
    // an unreachable instruction and then have SIL diagnostic check this.
    if (SGF.B.hasValidInsertionPoint())
      SGF.B.createUnreachable(S);
  }

  // Emit the condition bindings, branching to the bodyBB if they fail.  Since
  // we didn't push a scope, the bound variables are live after this statement.
  SGF.emitStmtCondition(S->getCond(), bodyBB, S);
}


void StmtEmitter::visitIfConfigStmt(IfConfigStmt *S) {
  // Active members are attached to the enclosing declaration, so there's no
  // need to walk anything within.
}

void StmtEmitter::visitWhileStmt(WhileStmt *S) {
  LexicalScope condBufferScope(SGF.Cleanups, SGF, S);
  
  // Create a new basic block and jump into it.
  JumpDest loopDest = createJumpDest(S->getBody());
  SGF.B.emitBlock(loopDest.getBlock(), S);
  
  // Create a break target (at this level in the cleanup stack) in case it is
  // needed.
  JumpDest breakDest = createJumpDest(S->getBody());

  // Set the destinations for any 'break' and 'continue' statements inside the
  // body.
  SGF.BreakContinueDestStack.push_back({S, breakDest, loopDest});
  
  // Evaluate the condition, the body, and a branch back to LoopBB when the
  // condition is true.  On failure, jump to BreakBB.
  {
    // Enter a scope for any bound pattern variables.
    Scope conditionScope(SGF.Cleanups, S);
    
    SGF.emitStmtCondition(S->getCond(), breakDest, S);
    
    // In the success path, emit the body of the while.
    SGF.emitProfilerIncrement(S->getBody());
    SGF.emitStmt(S->getBody());
    
    // Finish the "true part" by cleaning up any temporaries and jumping to the
    // continuation block.
    if (SGF.B.hasValidInsertionPoint()) {
      RegularLocation L(S->getBody());
      L.pointToEnd();
      SGF.Cleanups.emitBranchAndCleanups(loopDest, L);
    }
  }

  SGF.BreakContinueDestStack.pop_back();

  // Handle break block.  If it was used, we link it up with the cleanup chain,
  // otherwise we just remove it.
  SILBasicBlock *breakBB = breakDest.getBlock();
  if (breakBB->pred_empty()) {
    SGF.eraseBasicBlock(breakBB);
  } else {
    SGF.B.emitBlock(breakBB);
  }
}

void StmtEmitter::visitDoStmt(DoStmt *S) {
  // We don't need to do anything fancy if we don't have a label.
  // Otherwise, assume we might break or continue.
  bool hasLabel = (bool) S->getLabelInfo();

  JumpDest endDest = JumpDest::invalid();
  if (hasLabel) {
    // Create the end dest first so that the loop dest comes in-between.
    endDest = createJumpDest(S->getBody());

    // Create a new basic block and jump into it.
    JumpDest loopDest = createJumpDest(S->getBody());
    SGF.B.emitBlock(loopDest.getBlock(), S);

    // Set the destinations for 'break' and 'continue'.
    SGF.BreakContinueDestStack.push_back({S, endDest, loopDest});
  }

  // Emit the body.
  visit(S->getBody());

  if (hasLabel) {
    SGF.BreakContinueDestStack.pop_back();
    emitOrDeleteBlock(SGF, endDest, CleanupLocation(S));
  }
}

void StmtEmitter::visitDoCatchStmt(DoCatchStmt *S) {
  Type formalExnType =
    S->getCatches().front()->getErrorPattern()->getType();
  auto &exnTL = SGF.getTypeLowering(formalExnType);

  // Create the throw destination at the end of the function.
  JumpDest throwDest = createJumpDest(S->getBody(),
                                      FunctionSection::Postmatter);
  SILArgument *exnArg =
    throwDest.getBlock()->createBBArg(exnTL.getLoweredType());

  // We always need a continuation block because we might fall out of
  // a catch block.  But we don't need a loop block unless the 'do'
  // statement is labeled.
  JumpDest endDest = createJumpDest(S->getBody());

  // We don't need to do anything too fancy about emission if we don't
  // have a label.  Otherwise, assume we might break or continue.
  bool hasLabel = (bool) S->getLabelInfo();
  if (hasLabel) {
    // Create a new basic block and jump into it.
    JumpDest loopDest = createJumpDest(S->getBody());
    SGF.B.emitBlock(loopDest.getBlock(), S);

    // Set the destinations for 'break' and 'continue'.
    SGF.BreakContinueDestStack.push_back({S, endDest, loopDest});
  }

  // Emit the body.
  {
    // Push the new throw destination.
    llvm::SaveAndRestore<JumpDest> savedThrowDest(SGF.ThrowDest, throwDest);

    visit(S->getBody());
    // We emit the counter for exiting the do-block here, as we may not have a
    // valid insertion point when falling out.
    SGF.emitProfilerIncrement(S);
  }

  // Emit the catch clauses, but only if the body of the function
  // actually throws. This is a consequence of the fact that a
  // DoCatchStmt with a non-throwing body will type check even in
  // a non-throwing lexical context. In this case, our local throwDest
  // has no predecessors, and SGF.ThrowDest may not be valid either.
  if (auto *BB = getOrEraseBlock(SGF, throwDest)) {
    // Move the insertion point to the throw destination.
    SavedInsertionPoint savedIP(SGF, BB, FunctionSection::Postmatter);

    // The exception cleanup should be getting forwarded around
    // correctly anyway, but push a scope to ensure it gets popped.
    Scope exnScope(SGF.Cleanups, CleanupLocation(S));

    // Take ownership of the exception.
    ManagedValue exn = SGF.emitManagedRValueWithCleanup(exnArg, exnTL);

    // Emit all the catch clauses, branching to the end destination if
    // we fall out of one.
    SGF.emitCatchDispatch(S, exn, S->getCatches(), endDest);
  }

  if (hasLabel) {
    SGF.BreakContinueDestStack.pop_back();
  }

  // Handle falling out of the do-block.
  //
  // It's important for good code layout that the insertion point be
  // left in the original function section after this.  So if
  // emitOrDeleteBlock ever learns to just continue in the
  // predecessor, we'll need to suppress that here.
  emitOrDeleteBlock(SGF, endDest, CleanupLocation(S->getBody()));
}

void StmtEmitter::visitCatchStmt(CatchStmt *S) {
  llvm_unreachable("catch statement outside of context?");
}

void StmtEmitter::visitRepeatWhileStmt(RepeatWhileStmt *S) {
  // Create a new basic block and jump into it.
  SILBasicBlock *loopBB = createBasicBlock();
  SGF.B.emitBlock(loopBB, S);
  
  // Set the destinations for 'break' and 'continue'
  JumpDest endDest = createJumpDest(S->getBody());
  JumpDest condDest = createJumpDest(S->getBody());
  SGF.BreakContinueDestStack.push_back({ S, endDest, condDest });

  // Emit the body, which is always evaluated the first time around.
  SGF.emitProfilerIncrement(S->getBody());
  visit(S->getBody());

  // Let's not differ from C99 6.8.5.2: "The evaluation of the controlling
  // expression takes place after each execution of the loop body."
  emitOrDeleteBlock(SGF, condDest, S);

  if (SGF.B.hasValidInsertionPoint()) {
    // Evaluate the condition with the false edge leading directly
    // to the continuation block.
    Condition Cond = SGF.emitCondition(S->getCond(), /*hasFalseCode*/ false);
    
    Cond.enterTrue(SGF);
    if (SGF.B.hasValidInsertionPoint()) {
      SGF.B.createBranch(S->getCond(), loopBB);
    }
    
    Cond.exitTrue(SGF);
    // Complete the conditional execution.
    Cond.complete(SGF);
  }
  
  emitOrDeleteBlock(SGF, endDest, S);
  SGF.BreakContinueDestStack.pop_back();
}

void StmtEmitter::visitForStmt(ForStmt *S) {
  llvm_unreachable("c-style for loop is always a semantic error");
}

void StmtEmitter::visitForEachStmt(ForEachStmt *S) {
  // Emit the 'iterator' variable that we'll be using for iteration.
  LexicalScope OuterForScope(SGF.Cleanups, SGF, CleanupLocation(S));
  SGF.visitPatternBindingDecl(S->getIterator());
  
  // If we ever reach an unreachable point, stop emitting statements.
  // This will need revision if we ever add goto.
  if (!SGF.B.hasValidInsertionPoint()) return;
  
  // If generator's optional result is address-only, create a stack allocation
  // to hold the results.  This will be initialized on every entry into the loop
  // header and consumed by the loop body. On loop exit, the terminating value
  // will be in the buffer.
  auto optTy = S->getIteratorNext()->getType()->getCanonicalType();
  auto &optTL = SGF.getTypeLowering(optTy);
  SILValue nextBufOrValue;

  if (optTL.isAddressOnly())
    nextBufOrValue = SGF.emitTemporaryAllocation(S, optTL.getLoweredType());
  
  // Create a new basic block and jump into it.
  JumpDest loopDest = createJumpDest(S->getBody());
  SGF.B.emitBlock(loopDest.getBlock(), S);
  
  // Set the destinations for 'break' and 'continue'.
  JumpDest endDest = createJumpDest(S->getBody());
  SGF.BreakContinueDestStack.push_back({ S, endDest, loopDest });

  // Advance the generator.  Use a scope to ensure that any temporary stack
  // allocations in the subexpression are immediately released.
  if (optTL.isAddressOnly()) {
    Scope InnerForScope(SGF.Cleanups, CleanupLocation(S->getIteratorNext()));
    auto nextInit = SGF.useBufferAsTemporary(nextBufOrValue, optTL);
    SGF.emitExprInto(S->getIteratorNext(), nextInit.get());
    nextInit->getManagedAddress().forward(SGF);
  } else {
    Scope InnerForScope(SGF.Cleanups, CleanupLocation(S->getIteratorNext()));
    nextBufOrValue =
      SGF.emitRValueAsSingleValue(S->getIteratorNext()).forward(SGF);
  }
  
  // Continue if the value is present.
  Condition Cond = SGF.emitCondition(
         SGF.emitDoesOptionalHaveValue(S, nextBufOrValue), S,
         /*hasFalseCode=*/false, /*invertValue=*/false);

  if (Cond.hasTrue()) {
    Cond.enterTrue(SGF);
    SGF.emitProfilerIncrement(S->getBody());
    
    // Emit the loop body.
    // The declared variable(s) for the current element are destroyed
    // at the end of each loop iteration.
    {
      Scope InnerForScope(SGF.Cleanups, CleanupLocation(S->getBody()));
      // Emit the initialization for the pattern.  If any of the bound patterns
      // fail (because this is a 'for case' pattern with a refutable pattern,
      // the code should jump to the continue block.
      InitializationPtr initLoopVars
        = SGF.emitPatternBindingInitialization(S->getPattern(), loopDest);
      ManagedValue val;

      // If we had a loadable "next" generator value, we know it is present.
      // Get the value out of the optional, and wrap it up with a cleanup so
      // that any exits out of this scope properly clean it up.
      if (optTL.isLoadable()) {
        val = SGF.emitManagedRValueWithCleanup(nextBufOrValue);
      } else {
        val = SGF.emitManagedBufferWithCleanup(nextBufOrValue);
      }
      val = SGF.emitUncheckedGetOptionalValueFrom(S, val, optTL,
                                            SGFContext(initLoopVars.get()));
      if (!val.isInContext())
        RValue(SGF, S, optTy.getAnyOptionalObjectType(), val)
          .forwardInto(SGF, S, initLoopVars.get());

      // Now that the pattern has been initialized, check any where condition.
      // If it fails, loop around as if 'continue' happened.
      if (auto *Where = S->getWhere()) {
        auto cond = SGF.emitCondition(Where, /*hasFalse*/false, /*invert*/true);
        // If self is null, branch to the epilog.
        cond.enterTrue(SGF);
        SGF.Cleanups.emitBranchAndCleanups(loopDest, Where, { });
        cond.exitTrue(SGF);
        cond.complete(SGF);
      }

      visit(S->getBody());
    }
    
    // Loop back to the header.
    if (SGF.B.hasValidInsertionPoint()) {
      // Associate the loop body's closing brace with this branch.
      RegularLocation L(S->getBody());
      L.pointToEnd();
      SGF.B.createBranch(L, loopDest.getBlock());
    }
    Cond.exitTrue(SGF);
  }
  
  // Complete the conditional execution.
  Cond.complete(SGF);
  
  emitOrDeleteBlock(SGF, endDest, S);
  SGF.BreakContinueDestStack.pop_back();
  
  // We do not need to destroy the value in the 'nextBuf' slot here, because
  // either the 'for' loop finished naturally and the buffer contains '.None',
  // or we exited by 'break' and the value in the buffer was consumed.
}

void StmtEmitter::visitBreakStmt(BreakStmt *S) {
  assert(S->getTarget() && "Sema didn't fill in break target?");
  SGF.emitBreakOutOf(S, S->getTarget());
}

void SILGenFunction::emitBreakOutOf(SILLocation loc, Stmt *target) {
  CurrentSILLoc = loc;
  
  // Find the target JumpDest based on the target that sema filled into the
  // stmt.
  for (auto &elt : BreakContinueDestStack) {
    if (target == elt.Target) {
      Cleanups.emitBranchAndCleanups(elt.BreakDest, loc);
      return;
    }
  }
  llvm_unreachable("Break has available target block.");
}

void StmtEmitter::visitContinueStmt(ContinueStmt *S) {
  assert(S->getTarget() && "Sema didn't fill in continue target?");

  SGF.CurrentSILLoc = S;
  
  // Find the target JumpDest based on the target that sema filled into the
  // stmt.
  for (auto &elt : SGF.BreakContinueDestStack) {
    if (S->getTarget() == elt.Target) {
      SGF.Cleanups.emitBranchAndCleanups(elt.ContinueDest, S);
      return;
    }
  }
  llvm_unreachable("Continue has available target block.");
}

void StmtEmitter::visitSwitchStmt(SwitchStmt *S) {
  // Implemented in SILGenPattern.cpp.
  SGF.emitSwitchStmt(S);
}

void StmtEmitter::visitCaseStmt(CaseStmt *S) {
  llvm_unreachable("cases should be lowered as part of switch stmt");
}

void StmtEmitter::visitFallthroughStmt(FallthroughStmt *S) {
  // Implemented in SILGenPattern.cpp.
  SGF.emitSwitchFallthrough(S);
}

void StmtEmitter::visitFailStmt(FailStmt *S) {
  // Jump to the failure block.
  assert(SGF.FailDest.isValid() && "too big to fail");
  SGF.Cleanups.emitBranchAndCleanups(SGF.FailDest, S);
}

/// Return a basic block suitable to be the destination block of a
/// try_apply instruction.  The block is implicitly emitted and filled in.
SILBasicBlock *
SILGenFunction::getTryApplyErrorDest(SILLocation loc,
                                     SILResultInfo exnResult,
                                     bool suppressErrorPath) {
  assert(exnResult.getConvention() == ResultConvention::Owned);

  // For now, don't try to re-use destination blocks for multiple
  // failure sites.
  SILBasicBlock *destBB = createBasicBlock(FunctionSection::Postmatter);
  SILValue exn = destBB->createBBArg(exnResult.getSILType());

  assert(B.hasValidInsertionPoint() && B.insertingAtEndOfBlock());
  SavedInsertionPoint savedIP(*this, destBB, FunctionSection::Postmatter);

  // If we're suppressing error paths, just wrap it up as unreachable
  // and return.
  if (suppressErrorPath) {
    B.createUnreachable(loc);
    return destBB;
  }

  // We don't want to exit here with a dead cleanup on the stack,
  // so push the scope first.
  FullExpr scope(Cleanups, CleanupLocation::get(loc));
  emitThrow(loc, emitManagedRValueWithCleanup(exn));

  return destBB;
}

void SILGenFunction::emitThrow(SILLocation loc, ManagedValue exnMV,
                               bool emitWillThrow) {
  assert(ThrowDest.isValid() &&
         "calling emitThrow with invalid throw destination!");

  // Claim the exception value.  If we need to handle throwing
  // cleanups, the correct thing to do here is to recreate the
  // exception's cleanup when emitting each cleanup we branch through.
  // But for now we aren't bothering.
  SILValue exn = exnMV.forward(*this);

  if (emitWillThrow) {
    // Generate a call to the 'swift_willThrow' runtime function to allow the
    // debugger to catch the throw event.
    B.createBuiltin(loc, SGM.getASTContext().getIdentifier("willThrow"),
                    SGM.Types.getEmptyTupleType(), {}, {exn});
  }

  // Branch to the cleanup destination.
  Cleanups.emitBranchAndCleanups(ThrowDest, loc, exn);
}
