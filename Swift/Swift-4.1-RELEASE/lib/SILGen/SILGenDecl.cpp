//===--- SILGenDecl.cpp - Implements Lowering of ASTs -> SIL for Decls ----===//
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

#include "Initialization.h"
#include "RValue.h"
#include "SILGen.h"
#include "SILGenDynamicCast.h"
#include "Scope.h"
#include "SwitchCaseFullExpr.h"
#include "swift/AST/ASTMangler.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Module.h"
#include "swift/AST/NameLookup.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/Basic/ProfileCounter.h"
#include "swift/SIL/FormalLinkage.h"
#include "swift/SIL/PrettyStackTrace.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/SILDebuggerClient.h"
#include "swift/SIL/SILType.h"
#include "swift/SIL/TypeLowering.h"
#include "llvm/ADT/SmallString.h"
#include <iterator>

using namespace swift;
using namespace Lowering;

void Initialization::_anchor() {}
void SILDebuggerClient::anchor() {}

namespace {
  /// A "null" initialization that indicates that any value being initialized
  /// into this initialization should be discarded. This represents AnyPatterns
  /// (that is, 'var (_)') that bind to values without storing them.
  class BlackHoleInitialization : public Initialization {
  public:
    BlackHoleInitialization() {}

    bool canSplitIntoTupleElements() const override {
      return true;
    }
    
    MutableArrayRef<InitializationPtr>
    splitIntoTupleElements(SILGenFunction &SGF, SILLocation loc,
                           CanType type,
                           SmallVectorImpl<InitializationPtr> &buf) override {
      // "Destructure" an ignored binding into multiple ignored bindings.
      for (auto fieldType : cast<TupleType>(type)->getElementTypes()) {
        (void) fieldType;
        buf.push_back(InitializationPtr(new BlackHoleInitialization()));
      }
      return buf;
    }

    void copyOrInitValueInto(SILGenFunction &SGF, SILLocation loc,
                             ManagedValue value, bool isInit) override {
      /// This just ignores the provided value.
    }

    void finishUninitialized(SILGenFunction &SGF) override {
      // do nothing
    }
  };
} // end anonymous namespace

static void copyOrInitValueIntoHelper(
    SILGenFunction &SGF, SILLocation loc, ManagedValue value, bool isInit,
    ArrayRef<InitializationPtr> subInitializations,
    llvm::function_ref<ManagedValue(ManagedValue, unsigned, SILType)> func) {
  auto sourceType = value.getType().castTo<TupleType>();
  auto sourceSILType = value.getType();
  for (unsigned i = 0, e = sourceType->getNumElements(); i != e; ++i) {
    SILType fieldTy = sourceSILType.getTupleElementType(i);
    ManagedValue elt = func(value, i, fieldTy);
    subInitializations[i]->copyOrInitValueInto(SGF, loc, elt, isInit);
    subInitializations[i]->finishInitialization(SGF);
  }
}

void TupleInitialization::copyOrInitValueInto(SILGenFunction &SGF,
                                              SILLocation loc,
                                              ManagedValue value, bool isInit) {
  // In the object case, we perform a borrow + extract + copy sequence. This is
  // because we do not have a destructure operation.
  if (value.getType().isObject()) {
    value = value.borrow(SGF, loc);
    return copyOrInitValueIntoHelper(
        SGF, loc, value, isInit, SubInitializations,
        [&](ManagedValue aggregate, unsigned i,
            SILType fieldType) -> ManagedValue {
          auto elt = SGF.B.createTupleExtract(loc, aggregate, i, fieldType);
          return SGF.B.createCopyValue(loc, elt);
        });
  }

  // In the address case, we can support takes directly, so forward the cleanup
  // of the aggregate and create takes of the underlying addresses.
  value = ManagedValue::forUnmanaged(value.forward(SGF));
  return copyOrInitValueIntoHelper(
      SGF, loc, value, isInit, SubInitializations,
      [&](ManagedValue aggregate, unsigned i,
          SILType fieldType) -> ManagedValue {
        ManagedValue elt =
            SGF.B.createTupleElementAddr(loc, value, i, fieldType);
        if (!fieldType.isAddressOnly(SGF.F.getModule())) {
          return SGF.B.createLoadTake(loc, elt);
        }

        return SGF.emitManagedRValueWithCleanup(elt.getValue());
      });
}

void TupleInitialization::finishUninitialized(SILGenFunction &SGF) {
  for (auto &subInit : SubInitializations) {
    subInit->finishUninitialized(SGF);
  }
}

namespace {
  class CleanupClosureConstant : public Cleanup {
    SILValue closure;
  public:
    CleanupClosureConstant(SILValue closure) : closure(closure) {}
    void emit(SILGenFunction &SGF, CleanupLocation l) override {
      SGF.B.emitDestroyValueOperation(l, closure);
    }
    void dump(SILGenFunction &) const override {
#ifndef NDEBUG
      llvm::errs() << "CleanupClosureConstant\n"
                   << "State:" << getState() << "\n"
                   << "closure:" << closure << "\n";
#endif
    }
  };
} // end anonymous namespace

SubstitutionList SILGenFunction::getForwardingSubstitutions() {
  return F.getForwardingSubstitutions();
}

void SILGenFunction::visitFuncDecl(FuncDecl *fd) {
  // Generate the local function body.
  SGM.emitFunction(fd);
}

MutableArrayRef<InitializationPtr>
SingleBufferInitialization::
splitIntoTupleElements(SILGenFunction &SGF, SILLocation loc, CanType type,
                       SmallVectorImpl<InitializationPtr> &buf) {
  assert(SplitCleanups.empty() && "getting sub-initializations twice?");
  auto address = getAddressForInPlaceInitialization(SGF, loc);
  return splitSingleBufferIntoTupleElements(SGF, loc, type, address,
                                            buf, SplitCleanups);
}

MutableArrayRef<InitializationPtr>
SingleBufferInitialization::
splitSingleBufferIntoTupleElements(SILGenFunction &SGF, SILLocation loc,
                                   CanType type, SILValue baseAddr,
                                   SmallVectorImpl<InitializationPtr> &buf,
                     TinyPtrVector<CleanupHandle::AsPointer> &splitCleanups) {
  // Destructure the buffer into per-element buffers.
  for (auto i : indices(cast<TupleType>(type)->getElementTypes())) {
    // Project the element.
    SILValue eltAddr = SGF.B.createTupleElementAddr(loc, baseAddr, i);

    // Create an initialization to initialize the element.
    auto &eltTL = SGF.getTypeLowering(eltAddr->getType());
    auto eltInit = SGF.useBufferAsTemporary(eltAddr, eltTL);

    // Remember the element cleanup.
    auto eltCleanup = eltInit->getInitializedCleanup();
    if (eltCleanup.isValid())
      splitCleanups.push_back(eltCleanup);

    buf.emplace_back(eltInit.release());
  }

  return buf;
}

void SingleBufferInitialization::
copyOrInitValueIntoSingleBuffer(SILGenFunction &SGF, SILLocation loc,
                                ManagedValue value, bool isInit,
                                SILValue destAddr) {
  if (!isInit) {
    assert(value.getValue() != destAddr && "copying in place?!");
    value.copyInto(SGF, destAddr, loc);
    return;
  }
  
  // If we didn't evaluate into the initialization buffer, do so now.
  if (value.getValue() != destAddr) {
    value.forwardInto(SGF, loc, destAddr);
  } else {
    // If we did evaluate into the initialization buffer, disable the
    // cleanup.
    value.forwardCleanup(SGF);
  }
}

void SingleBufferInitialization::finishInitialization(SILGenFunction &SGF) {
  // Forward all of the split element cleanups, assuming we made any.
  for (CleanupHandle eltCleanup : SplitCleanups)
    SGF.Cleanups.forwardCleanup(eltCleanup);
}

bool KnownAddressInitialization::isInPlaceInitializationOfGlobal() const {
  return isa<GlobalAddrInst>(address);
}

bool TemporaryInitialization::isInPlaceInitializationOfGlobal() const {
  return isa<GlobalAddrInst>(Addr);
}

void TemporaryInitialization::finishInitialization(SILGenFunction &SGF) {
  SingleBufferInitialization::finishInitialization(SGF);
  if (Cleanup.isValid())
    SGF.Cleanups.setCleanupState(Cleanup, CleanupState::Active);
}

namespace {
class EndBorrowCleanup : public Cleanup {
  SILValue original;
  SILValue borrowed;

public:
  EndBorrowCleanup(SILValue original, SILValue borrowed)
      : original(original), borrowed(borrowed) {}

  void emit(SILGenFunction &SGF, CleanupLocation l) override {
    SGF.B.createEndBorrow(l, borrowed, original);
  }

  void dump(SILGenFunction &) const override {
#ifndef NDEBUG
    llvm::errs() << "EndBorrowCleanup "
                 << "State:" << getState() << "\n"
                 << "original:" << original << "\n"
                 << "borrowed:" << borrowed << "\n";
#endif
  }
};
} // end anonymous namespace

namespace {
class ReleaseValueCleanup : public Cleanup {
  SILValue v;
public:
  ReleaseValueCleanup(SILValue v) : v(v) {}

  void emit(SILGenFunction &SGF, CleanupLocation l) override {
    if (v->getType().isAddress())
      SGF.B.createDestroyAddr(l, v);
    else
      SGF.B.emitDestroyValueOperation(l, v);
  }

  void dump(SILGenFunction &) const override {
#ifndef NDEBUG
    llvm::errs() << "ReleaseValueCleanup\n"
                 << "State:" << getState() << "\n"
                 << "Value:" << v << "\n";
#endif
  }
};
} // end anonymous namespace

namespace {
/// Cleanup to destroy an initialized variable.
class DeallocStackCleanup : public Cleanup {
  SILValue Addr;
public:
  DeallocStackCleanup(SILValue addr) : Addr(addr) {}

  void emit(SILGenFunction &SGF, CleanupLocation l) override {
    SGF.B.createDeallocStack(l, Addr);
  }

  void dump(SILGenFunction &) const override {
#ifndef NDEBUG
    llvm::errs() << "DeallocStackCleanup\n"
                 << "State:" << getState() << "\n"
                 << "Addr:" << Addr << "\n";
#endif
  }
};
} // end anonymous namespace

namespace {
/// Cleanup to destroy an initialized 'var' variable.
class DestroyLocalVariable : public Cleanup {
  VarDecl *Var;
public:
  DestroyLocalVariable(VarDecl *var) : Var(var) {}

  void emit(SILGenFunction &SGF, CleanupLocation l) override {
    SGF.destroyLocalVariable(l, Var);
  }

  void dump(SILGenFunction &SGF) const override {
#ifndef NDEBUG
    llvm::errs() << "DestroyLocalVariable\n"
                 << "State:" << getState() << "\n"
                 << "Decl: ";
    Var->print(llvm::errs());
    llvm::errs() << "\n";
    if (isActive()) {
      auto loc = SGF.VarLocs[Var];
      assert((loc.box || loc.value) && "One of box or value should be set");
      if (loc.box) {
        llvm::errs() << "Box: " << loc.box << "\n";
      } else {
        llvm::errs() << "Value: " << loc.value << "\n";
      }
    }
    llvm::errs() << "\n";
#endif
  }
};
} // end anonymous namespace

namespace {
/// Cleanup to destroy an uninitialized local variable.
class DeallocateUninitializedLocalVariable : public Cleanup {
  VarDecl *Var;
public:
  DeallocateUninitializedLocalVariable(VarDecl *var) : Var(var) {}

  void emit(SILGenFunction &SGF, CleanupLocation l) override {
    SGF.deallocateUninitializedLocalVariable(l, Var);
  }

  void dump(SILGenFunction &) const override {
#ifndef NDEBUG
    llvm::errs() << "DeallocateUninitializedLocalVariable\n"
                 << "State:" << getState() << "\n";
    // TODO: Make sure we dump var.
    llvm::errs() << "\n";
#endif
  }
};
} // end anonymous namespace

namespace {
/// An initialization of a local 'var'.
class LocalVariableInitialization : public SingleBufferInitialization {
  /// The local variable decl being initialized.
  VarDecl *decl;
  SILGenFunction &SGF;

  /// The cleanup we pushed to deallocate the local variable before it
  /// gets initialized.
  CleanupHandle DeallocCleanup;

  /// The cleanup we pushed to destroy and deallocate the local variable.
  CleanupHandle ReleaseCleanup;

  bool DidFinish = false;
public:
  /// Sets up an initialization for the allocated box. This pushes a
  /// CleanupUninitializedBox cleanup that will be replaced when
  /// initialization is completed.
  LocalVariableInitialization(VarDecl *decl,
                              Optional<MarkUninitializedInst::Kind> kind,
                              unsigned ArgNo, SILGenFunction &SGF)
      : decl(decl), SGF(SGF) {
    assert(decl->getDeclContext()->isLocalContext() &&
           "can't emit a local var for a non-local var decl");
    assert(decl->hasStorage() && "can't emit storage for a computed variable");
    assert(!SGF.VarLocs.count(decl) && "Already have an entry for this decl?");

    auto boxType = SGF.SGM.Types
      .getContextBoxTypeForCapture(decl,
                     SGF.getLoweredType(decl->getType()).getSwiftRValueType(),
                     SGF.F.getGenericEnvironment(),
                     /*mutable*/ true);

    // The variable may have its lifetime extended by a closure, heap-allocate
    // it using a box.
    SILValue allocBox =
        SGF.B.createAllocBox(decl, boxType, {decl->isLet(), ArgNo});

    // Mark the memory as uninitialized, so DI will track it for us.
    if (kind)
      allocBox = SGF.B.createMarkUninitialized(decl, allocBox, kind.getValue());

    SILValue addr = SGF.B.createProjectBox(decl, allocBox, 0);

    /// Remember that this is the memory location that we're emitting the
    /// decl to.
    SGF.VarLocs[decl] = SILGenFunction::VarLoc::get(addr, allocBox);

    // Push a cleanup to destroy the local variable.  This has to be
    // inactive until the variable is initialized.
    SGF.Cleanups.pushCleanupInState<DestroyLocalVariable>(CleanupState::Dormant,
                                                          decl);
    ReleaseCleanup = SGF.Cleanups.getTopCleanup();

    // Push a cleanup to deallocate the local variable.
    SGF.Cleanups.pushCleanup<DeallocateUninitializedLocalVariable>(decl);
    DeallocCleanup = SGF.Cleanups.getTopCleanup();
  }

  ~LocalVariableInitialization() override {
    assert(DidFinish && "did not call VarInit::finishInitialization!");
  }

  SILValue getAddress() const {
    assert(SGF.VarLocs.count(decl) && "did not emit var?!");
    return SGF.VarLocs[decl].value;    
  }

  SILValue getAddressForInPlaceInitialization(SILGenFunction &SGF,
                                              SILLocation loc) override {
    return getAddress();
  }

  bool isInPlaceInitializationOfGlobal() const override {
    return isa<GlobalAddrInst>(getAddress());
  }

  void finishUninitialized(SILGenFunction &SGF) override {
    LocalVariableInitialization::finishInitialization(SGF);
  }

  void finishInitialization(SILGenFunction &SGF) override {
    SingleBufferInitialization::finishInitialization(SGF);
    assert(!DidFinish &&
           "called LocalVariableInitialization::finishInitialization twice!");
    SGF.Cleanups.setCleanupState(DeallocCleanup, CleanupState::Dead);
    SGF.Cleanups.setCleanupState(ReleaseCleanup, CleanupState::Active);
    DidFinish = true;
  }
};
} // end anonymous namespace

namespace {
/// Initialize a writeback buffer that receives the value of a 'let'
/// declaration.
class LetValueInitialization : public Initialization {
  /// The VarDecl for the let decl.
  VarDecl *vd;

  /// The address of the buffer used for the binding, if this is an address-only
  /// let.
  SILValue address;

  /// The cleanup we pushed to destroy the local variable.
  CleanupHandle DestroyCleanup;

  /// Cleanups we introduced when splitting.
  TinyPtrVector<CleanupHandle::AsPointer> SplitCleanups;

  bool DidFinish = false;

public:
  LetValueInitialization(VarDecl *vd, SILGenFunction &SGF) : vd(vd) {
    auto &lowering = SGF.getTypeLowering(vd->getType());
    
    // Decide whether we need a temporary stack buffer to evaluate this 'let'.
    // There are three cases we need to handle here: parameters, initialized (or
    // bound) decls, and uninitialized ones.
    bool needsTemporaryBuffer;
    bool isUninitialized = false;

    assert(!isa<ParamDecl>(vd)
           && "should not bind function params on this path");
    if (vd->getParentPatternBinding() && !vd->getParentInitializer()) {
      // This value is uninitialized (and unbound) if it has a pattern binding
      // decl, with no initializer value.
      assert(!vd->hasNonPatternBindingInit() && "Bound values aren't uninit!");
      
      // If this is a let-value without an initializer, then we need a temporary
      // buffer.  DI will make sure it is only assigned to once.
      needsTemporaryBuffer = true;
      isUninitialized = true;
    } else {
      // If this is a let with an initializer or bound value, we only need a
      // buffer if the type is address only.
      needsTemporaryBuffer =
          lowering.isAddressOnly() && SGF.silConv.useLoweredAddresses();
    }
   
    if (needsTemporaryBuffer) {
      address = SGF.emitTemporaryAllocation(vd, lowering.getLoweredType());
      if (isUninitialized)
        address = SGF.B.createMarkUninitializedVar(vd, address);
      DestroyCleanup = SGF.enterDormantTemporaryCleanup(address, lowering);
      SGF.VarLocs[vd] = SILGenFunction::VarLoc::get(address);
    } else if (!lowering.isTrivial()) {
      // Push a cleanup to destroy the let declaration.  This has to be
      // inactive until the variable is initialized: if control flow exits the
      // before the value is bound, we don't want to destroy the value.
      SGF.Cleanups.pushCleanupInState<DestroyLocalVariable>(
                                                    CleanupState::Dormant, vd);
      DestroyCleanup = SGF.Cleanups.getTopCleanup();
    } else {
      DestroyCleanup = CleanupHandle::invalid();
    }
  }

  ~LetValueInitialization() override {
    assert(DidFinish && "did not call LetValueInit::finishInitialization!");
  }

  bool hasAddress() const { return (bool)address; }

  bool canPerformInPlaceInitialization() const override {
    return hasAddress();
  }

  bool isInPlaceInitializationOfGlobal() const override {
    return isa<GlobalAddrInst>(address);
  }
  
  SILValue getAddressForInPlaceInitialization(SILGenFunction &SGF,
                                              SILLocation loc) override {
    // Emit into the buffer that 'let's produce for address-only values if
    // we have it.
    assert(hasAddress());
    return address;
  }

  /// Return true if we can get the addresses of elements with the
  /// 'getSubInitializationsForTuple' method.
  ///
  /// Let-value initializations cannot be broken into constituent pieces if a
  /// scalar value needs to be bound.  If there is an address in play, then we
  /// can initialize the address elements of the tuple though.
  bool canSplitIntoTupleElements() const override {
    return hasAddress();
  }
  
  MutableArrayRef<InitializationPtr>
  splitIntoTupleElements(SILGenFunction &SGF, SILLocation loc, CanType type,
                         SmallVectorImpl<InitializationPtr> &buf) override {
    assert(SplitCleanups.empty());
    auto address = getAddressForInPlaceInitialization(SGF, loc);
    return SingleBufferInitialization
       ::splitSingleBufferIntoTupleElements(SGF, loc, type, address, buf,
                                            SplitCleanups);
  }

  void bindValue(SILValue value, SILGenFunction &SGF) {
    assert(!SGF.VarLocs.count(vd) && "Already emitted this vardecl?");
    // If we're binding an address to this let value, then we can use it as an
    // address later.  This happens when binding an address only parameter to
    // an argument, for example.
    if (value->getType().isAddress())
      address = value;
    SGF.VarLocs[vd] = SILGenFunction::VarLoc::get(value);

    // Emit a debug_value[_addr] instruction to record the start of this value's
    // lifetime.
    SILLocation PrologueLoc(vd);
    PrologueLoc.markAsPrologue();
    if (address)
      SGF.B.createDebugValueAddr(PrologueLoc, value);
    else
      SGF.B.createDebugValue(PrologueLoc, value);
  }
  
  void copyOrInitValueInto(SILGenFunction &SGF, SILLocation loc,
                           ManagedValue value, bool isInit) override {
    // If this let value has an address, we can handle it just like a single
    // buffer value.
    if (hasAddress())
      return SingleBufferInitialization::
        copyOrInitValueIntoSingleBuffer(SGF, loc, value, isInit, address);
    
    // Otherwise, we bind the value.
    if (isInit) {
      // Disable the rvalue expression cleanup, since the let value
      // initialization has a cleanup that lives for the entire scope of the
      // let declaration.
      bindValue(value.forward(SGF), SGF);
    } else {
      // Disable the expression cleanup of the copy, since the let value
      // initialization has a cleanup that lives for the entire scope of the
      // let declaration.
      bindValue(value.copyUnmanaged(SGF, loc).forward(SGF), SGF);
    }
  }

  void finishUninitialized(SILGenFunction &SGF) override {
    LetValueInitialization::finishInitialization(SGF);
  }

  void finishInitialization(SILGenFunction &SGF) override {
    assert(!DidFinish &&
           "called LetValueInit::finishInitialization twice!");
    assert(SGF.VarLocs.count(vd) && "Didn't bind a value to this let!");

    // Deactivate any cleanups we made when splitting the tuple.
    for (auto cleanup : SplitCleanups)
      SGF.Cleanups.forwardCleanup(cleanup);

    // Activate the destroy cleanup.
    if (DestroyCleanup != CleanupHandle::invalid())
      SGF.Cleanups.setCleanupState(DestroyCleanup, CleanupState::Active);

    DidFinish = true;
  }
};
} // end anonymous namespace


namespace {
/// Initialize a variable of reference-storage type.
class ReferenceStorageInitialization : public Initialization {
  InitializationPtr VarInit;
public:
  ReferenceStorageInitialization(InitializationPtr &&subInit)
    : VarInit(std::move(subInit)) {
    assert(VarInit->canPerformInPlaceInitialization());
  }

  void copyOrInitValueInto(SILGenFunction &SGF, SILLocation loc,
                           ManagedValue value, bool isInit) override {
    auto address = VarInit->getAddressForInPlaceInitialization(SGF, loc);
    // If this is not an initialization, copy the value before we translateIt,
    // translation expects a +1 value.
    if (isInit)
      value.forwardInto(SGF, loc, address);
    else
      value.copyInto(SGF, address, loc);
  }

  void finishUninitialized(SILGenFunction &SGF) override {
    ReferenceStorageInitialization::finishInitialization(SGF);
  }
  
  void finishInitialization(SILGenFunction &SGF) override {
    VarInit->finishInitialization(SGF);
  }
};
} // end anonymous namespace

namespace {
/// Abstract base class for refutable pattern initializations.
class RefutablePatternInitialization : public Initialization {
  /// This is the label to jump to if the pattern fails to match.
  JumpDest failureDest;
public:
  RefutablePatternInitialization(JumpDest failureDest)
    : failureDest(failureDest) {
    assert(failureDest.isValid() &&
           "Refutable patterns can only exist in failable conditions");
  }

  JumpDest getFailureDest() const { return failureDest; }

  void copyOrInitValueInto(SILGenFunction &SGF, SILLocation loc,
                           ManagedValue value, bool isInit) override = 0;

  void bindVariable(SILLocation loc, VarDecl *var, ManagedValue value,
                    CanType formalValueType, SILGenFunction &SGF) {
    // Initialize the variable value.
    InitializationPtr init = SGF.emitInitializationForVarDecl(var, var->isLet());
    RValue(SGF, loc, formalValueType, value).forwardInto(SGF, loc, init.get());
  }

};
} // end anonymous namespace

namespace {
class ExprPatternInitialization : public RefutablePatternInitialization {
  ExprPattern *P;
public:
  ExprPatternInitialization(ExprPattern *P, JumpDest patternFailDest)
    : RefutablePatternInitialization(patternFailDest), P(P) {}

  void copyOrInitValueInto(SILGenFunction &SGF, SILLocation loc,
                           ManagedValue value, bool isInit) override;
};
} // end anonymous namespace

void ExprPatternInitialization::
copyOrInitValueInto(SILGenFunction &SGF, SILLocation loc,
                    ManagedValue value, bool isInit) {
  assert(isInit && "Only initialization is supported for refutable patterns");

  FullExpr scope(SGF.Cleanups, CleanupLocation(P));
  bindVariable(P, P->getMatchVar(), value,
               P->getType()->getCanonicalType(), SGF);

  // Emit the match test.
  SILValue testBool;
  {
    FullExpr scope(SGF.Cleanups, CleanupLocation(P->getMatchExpr()));
    testBool = SGF.emitRValueAsSingleValue(P->getMatchExpr()).
       getUnmanagedValue();
  }

  SILBasicBlock *contBB = SGF.B.splitBlockForFallthrough();
  auto falseBB = SGF.Cleanups.emitBlockForCleanups(getFailureDest(), loc);
  SGF.B.createCondBranch(loc, testBool, contBB, falseBB);

  SGF.B.setInsertionPoint(contBB);
}

namespace {
class EnumElementPatternInitialization : public RefutablePatternInitialization {
  EnumElementDecl *ElementDecl;
  InitializationPtr subInitialization;
public:
  EnumElementPatternInitialization(EnumElementDecl *ElementDecl,
                                   InitializationPtr &&subInitialization,
                                   JumpDest patternFailDest)
    : RefutablePatternInitialization(patternFailDest), ElementDecl(ElementDecl),
      subInitialization(std::move(subInitialization)) {}
    
  void copyOrInitValueInto(SILGenFunction &SGF, SILLocation loc,
                           ManagedValue value, bool isInit) override {
    assert(isInit && "Only initialization is supported for refutable patterns");
    emitEnumMatch(value, ElementDecl, subInitialization.get(), getFailureDest(),
                  loc, SGF);
  }

  static void emitEnumMatch(ManagedValue value, EnumElementDecl *ElementDecl,
                            Initialization *subInit, JumpDest FailureDest,
                            SILLocation loc, SILGenFunction &SGF);
  
  void finishInitialization(SILGenFunction &SGF) override {
    if (subInitialization.get())
      subInitialization->finishInitialization(SGF);
  }
};
} // end anonymous namespace

void EnumElementPatternInitialization::emitEnumMatch(
    ManagedValue value, EnumElementDecl *eltDecl, Initialization *subInit,
    JumpDest failureDest, SILLocation loc, SILGenFunction &SGF) {

  // Create all of the blocks early so we can maintain a consistent ordering
  // (and update less tests). Break this at your fingers parallel.
  //
  // *NOTE* This needs to be in reverse order to preserve the textual SIL.
  auto *contBlock = SGF.createBasicBlock();
  auto *someBlock = SGF.createBasicBlock();
  auto *defaultBlock = SGF.createBasicBlock();
  auto *originalBlock = SGF.B.getInsertionBB();

  SwitchEnumBuilder switchBuilder(SGF.B, loc, value);

  // Handle the none case.
  //
  // *NOTE*: Since we are performing an initialization here, it is *VERY*
  // important that we emit the negative case first. The reason why is that
  // currently the initialization has a dormant cleanup in a scope that may be
  // after the failureDest depth. Once we run the positive case, this
  // initialization will be enabled. Thus if we run the negative case /after/
  // the positive case, a cleanup will be emitted for the initialization on the
  // negative path... but the actual initialization happened on the positive
  // path, causing a use (the destroy on the negative path) to be created that
  // does not dominate its definition (in the positive path).
  auto handler = [&SGF, &loc, &failureDest](ManagedValue mv,
                                            SwitchCaseFullExpr &expr) {
    expr.exit();
    SGF.Cleanups.emitBranchAndCleanups(failureDest, loc);
  };

  // If we have a binary enum, do not emit a true default case. This ensures
  // that we do not emit a destroy_value on a .None.
  bool inferredBinaryEnum = false;
  auto *enumDecl = value.getType().getEnumOrBoundGenericEnum();
  if (auto *otherDecl = enumDecl->getOppositeBinaryDecl(eltDecl)) {
    inferredBinaryEnum = true;
    switchBuilder.addCase(otherDecl, defaultBlock, nullptr, handler);
  } else {
    switchBuilder.addDefaultCase(
        defaultBlock, nullptr, handler,
        SwitchEnumBuilder::DefaultDispatchTime::BeforeNormalCases);
  }

  // Always insert the some case at the front of the list. In the default case,
  // this will not matter, but in the case where we have a binary enum, we want
  // to preserve the old ordering of .some/.none. to make it easier to update
  // tests.
  switchBuilder.addCase(
      eltDecl, someBlock, contBlock,
      [&SGF, &loc, &eltDecl, &subInit, &value](ManagedValue mv,
                                               SwitchCaseFullExpr &expr) {
        // If the enum case has no bound value, we're done.
        if (!eltDecl->hasAssociatedValues()) {
          assert(
              subInit == nullptr &&
              "Cannot have a subinit when there is no value to match against");
          expr.exitAndBranch(loc);
          return;
        }

        if (subInit == nullptr) {
          // If there is no subinitialization, then we are done matching.  Don't
          // bother projecting out the any elements value only to ignore it.
          expr.exitAndBranch(loc);
          return;
        }

        // Otherwise, the bound value for the enum case is available.
        SILType eltTy = value.getType().getEnumElementType(eltDecl, SGF.SGM.M);
        auto &eltTL = SGF.getTypeLowering(eltTy);

        if (mv.getType().isAddress()) {
          // If the enum is address-only, take from the enum we have and load it
          // if
          // the element value is loadable.
          assert((eltTL.isTrivial() || mv.hasCleanup()) &&
                 "must be able to consume value");
          mv = SGF.B.createUncheckedTakeEnumDataAddr(loc, mv, eltDecl, eltTy);
          // Load a loadable data value.
          if (eltTL.isLoadable())
            mv = SGF.B.createLoadTake(loc, mv);
        }

        // If the payload is indirect, project it out of the box.
        if (eltDecl->isIndirect() || eltDecl->getParentEnum()->isIndirect()) {
          SILValue boxedValue = SGF.B.createProjectBox(loc, mv.getValue(), 0);
          auto &boxedTL = SGF.getTypeLowering(boxedValue->getType());
          // SEMANTIC ARC TODO: Revisit this when the verifier is enabled.
          if (boxedTL.isLoadable() || !SGF.silConv.useLoweredAddresses())
            boxedValue = boxedTL.emitLoad(SGF.B, loc, boxedValue,
                                          LoadOwnershipQualifier::Take);

          // We must treat the boxed value as +0 since it may be shared. Copy it
          // if nontrivial.
          //
          // TODO: Should be able to hand it off at +0 in some cases.
          mv = ManagedValue::forUnmanaged(boxedValue);
          mv = mv.copyUnmanaged(SGF, loc);
        }

        // Reabstract to the substituted type, if needed.
        CanType substEltTy =
            value.getType()
                .getSwiftRValueType()
                ->getTypeOfMember(SGF.SGM.M.getSwiftModule(), eltDecl,
                                  eltDecl->getArgumentInterfaceType())
                ->getCanonicalType();

        AbstractionPattern origEltTy =
            (eltDecl == SGF.getASTContext().getOptionalSomeDecl()
                 ? AbstractionPattern(substEltTy)
                 : SGF.SGM.M.Types.getAbstractionPattern(eltDecl));

        mv = SGF.emitOrigToSubstValue(loc, mv, origEltTy, substEltTy);

        // Pass the +1 value down into the sub initialization.
        subInit->copyOrInitValueInto(SGF, loc, mv, /*is an init*/ true);
        expr.exitAndBranch(loc);
      });

  std::move(switchBuilder).emit();

  // If we inferred a binary enum, put the asked for case first so we preserve
  // the current code structure. This just ensures that less test updates are
  // needed.
  if (inferredBinaryEnum) {
    if (auto *switchEnum =
            dyn_cast<SwitchEnumInst>(originalBlock->getTerminator())) {
      switchEnum->swapCase(0, 1);
    } else {
      auto *switchEnumAddr =
          cast<SwitchEnumAddrInst>(originalBlock->getTerminator());
      switchEnumAddr->swapCase(0, 1);
    }
  }

  // Reset the insertion point to the end of contBlock.
  SGF.B.setInsertionPoint(contBlock);
}

namespace {
class IsPatternInitialization : public RefutablePatternInitialization {
  IsPattern *pattern;
  InitializationPtr subInitialization;
public:
  IsPatternInitialization(IsPattern *pattern,
                          InitializationPtr &&subInitialization,
                          JumpDest patternFailDest)
  : RefutablePatternInitialization(patternFailDest), pattern(pattern),
    subInitialization(std::move(subInitialization)) {}
    
  void copyOrInitValueInto(SILGenFunction &SGF, SILLocation loc,
                           ManagedValue value, bool isInit) override;
  
  void finishInitialization(SILGenFunction &SGF) override {
    if (subInitialization.get())
      subInitialization->finishInitialization(SGF);
  }
};
} // end anonymous namespace

void IsPatternInitialization::
copyOrInitValueInto(SILGenFunction &SGF, SILLocation loc,
                    ManagedValue value, bool isInit) {
  assert(isInit && "Only initialization is supported for refutable patterns");
  
  // Try to perform the cast to the destination type, producing an optional that
  // indicates whether we succeeded.
  auto destType = OptionalType::get(pattern->getCastTypeLoc().getType());

  value =
      emitConditionalCheckedCast(SGF, loc, value, pattern->getType(), destType,
                                 pattern->getCastKind(), SGFContext(),
                                 ProfileCounter(), ProfileCounter())
          .getAsSingleValue(SGF, loc);

  // Now that we have our result as an optional, we can use an enum projection
  // to do all the work.
  EnumElementPatternInitialization::
  emitEnumMatch(value, SGF.getASTContext().getOptionalSomeDecl(),
                subInitialization.get(), getFailureDest(), loc, SGF);
}

namespace {
class BoolPatternInitialization : public RefutablePatternInitialization {
  BoolPattern *pattern;
public:
  BoolPatternInitialization(BoolPattern *pattern,
                            JumpDest patternFailDest)
    : RefutablePatternInitialization(patternFailDest), pattern(pattern) {}

  void copyOrInitValueInto(SILGenFunction &SGF, SILLocation loc,
                           ManagedValue value, bool isInit) override;
};
} // end anonymous namespace

void BoolPatternInitialization::
copyOrInitValueInto(SILGenFunction &SGF, SILLocation loc,
                    ManagedValue value, bool isInit) {
  assert(isInit && "Only initialization is supported for refutable patterns");

  // Extract the i1 from the Bool struct.
  StructDecl *BoolStruct = cast<StructDecl>(SGF.getASTContext().getBoolDecl());
  auto Members = BoolStruct->lookupDirect(SGF.getASTContext().Id_value_);
  assert(Members.size() == 1 &&
         "Bool should have only one property with name '_value'");
  auto Member = dyn_cast<VarDecl>(Members[0]);
  assert(Member &&"Bool should have a property with name '_value' of type Int1");
  auto *i1Val = SGF.B.createStructExtract(loc, value.forward(SGF), Member);

  // Branch on the boolean based on whether we're testing for true or false.
  SILBasicBlock *trueBB = SGF.B.splitBlockForFallthrough();
  auto contBB = trueBB;
  auto falseBB = SGF.Cleanups.emitBlockForCleanups(getFailureDest(), loc);

  if (!pattern->getValue())
    std::swap(trueBB, falseBB);
  SGF.B.createCondBranch(loc, i1Val, trueBB, falseBB);
  SGF.B.setInsertionPoint(contBB);
}


namespace {

/// InitializationForPattern - A visitor for traversing a pattern, generating
/// SIL code to allocate the declared variables, and generating an
/// Initialization representing the needed initializations.
///
/// It is important that any Initialization created for a pattern that might
/// not have an immediate initializer implement finishUninitialized.  Note
/// that this only applies to irrefutable patterns.
struct InitializationForPattern
  : public PatternVisitor<InitializationForPattern, InitializationPtr>
{
  SILGenFunction &SGF;

  /// This is the place that should be jumped to if the pattern fails to match.
  /// This is invalid for irrefutable pattern initializations.
  JumpDest patternFailDest;

  InitializationForPattern(SILGenFunction &SGF, JumpDest patternFailDest)
    : SGF(SGF), patternFailDest(patternFailDest) {}

  // Paren, Typed, and Var patterns are noops, just look through them.
  InitializationPtr visitParenPattern(ParenPattern *P) {
    return visit(P->getSubPattern());
  }
  InitializationPtr visitTypedPattern(TypedPattern *P) {
    return visit(P->getSubPattern());
  }
  InitializationPtr visitVarPattern(VarPattern *P) {
    return visit(P->getSubPattern());
  }

  // AnyPatterns (i.e, _) don't require any storage. Any value bound here will
  // just be dropped.
  InitializationPtr visitAnyPattern(AnyPattern *P) {
    return InitializationPtr(new BlackHoleInitialization());
  }

  // Bind to a named pattern by creating a memory location and initializing it
  // with the initial value.
  InitializationPtr visitNamedPattern(NamedPattern *P) {
    if (!P->getDecl()->hasName()) {
      // Unnamed parameters don't require any storage. Any value bound here will
      // just be dropped.
      return InitializationPtr(new BlackHoleInitialization());
    }

    return SGF.emitInitializationForVarDecl(P->getDecl(), P->getDecl()->isLet());
  }

  // Bind a tuple pattern by aggregating the component variables into a
  // TupleInitialization.
  InitializationPtr visitTuplePattern(TuplePattern *P) {
    TupleInitialization *init = new TupleInitialization();
    for (auto &elt : P->getElements())
      init->SubInitializations.push_back(visit(elt.getPattern()));
    return InitializationPtr(init);
  }

  InitializationPtr visitEnumElementPattern(EnumElementPattern *P) {
    InitializationPtr subInit;
    if (auto *subP = P->getSubPattern())
      subInit = visit(subP);
    auto *res = new EnumElementPatternInitialization(P->getElementDecl(),
                                                     std::move(subInit),
                                                     patternFailDest);
    return InitializationPtr(res);
  }
  InitializationPtr visitOptionalSomePattern(OptionalSomePattern *P) {
    InitializationPtr subInit = visit(P->getSubPattern());
    auto *res = new EnumElementPatternInitialization(P->getElementDecl(),
                                                     std::move(subInit),
                                                     patternFailDest);
    return InitializationPtr(res);
  }
  InitializationPtr visitIsPattern(IsPattern *P) {
    InitializationPtr subInit;
    if (auto *subP = P->getSubPattern())
      subInit = visit(subP);
    return InitializationPtr(new IsPatternInitialization(P, std::move(subInit),
                                                         patternFailDest));
  }
  InitializationPtr visitBoolPattern(BoolPattern *P) {
    return InitializationPtr(new BoolPatternInitialization(P, patternFailDest));
  }
  InitializationPtr visitExprPattern(ExprPattern *P) {
    return InitializationPtr(new ExprPatternInitialization(P, patternFailDest));
  }
};

} // end anonymous namespace

InitializationPtr
SILGenFunction::emitInitializationForVarDecl(VarDecl *vd, bool forceImmutable) {
  // If this is a computed variable, we don't need to do anything here.
  // We'll generate the getter and setter when we see their FuncDecls.
  if (!vd->hasStorage())
    return InitializationPtr(new BlackHoleInitialization());

  if (vd->isDebuggerVar()) {
    DebuggerClient *DebugClient = SGM.SwiftModule->getDebugClient();
    assert(DebugClient && "Debugger variables with no debugger client");
    SILDebuggerClient *SILDebugClient = DebugClient->getAsSILDebuggerClient();
    assert(SILDebugClient && "Debugger client doesn't support SIL");
    SILValue SV = SILDebugClient->emitLValueForVariable(vd, B);

    VarLocs[vd] = SILGenFunction::VarLoc::get(SV);
    return InitializationPtr(new KnownAddressInitialization(SV));
  }

  CanType varType = vd->getType()->getCanonicalType();

  assert(!isa<InOutType>(varType) && "local variables should never be inout");

  // If this is a 'let' initialization for a non-global, set up a
  // let binding, which stores the initialization value into VarLocs directly.
  if (forceImmutable && vd->getDeclContext()->isLocalContext() &&
      !isa<ReferenceStorageType>(varType))
    return InitializationPtr(new LetValueInitialization(vd, *this));

  // If the variable has no initial value, emit a mark_uninitialized instruction
  // so that DI tracks and enforces validity of it.
  bool isUninitialized =
    vd->getParentPatternBinding() && !vd->getParentInitializer();
  
  // If this is a global variable, initialize it without allocations or
  // cleanups.
  InitializationPtr Result;
  if (!vd->getDeclContext()->isLocalContext()) {
    auto *silG = SGM.getSILGlobalVariable(vd, NotForDefinition);
    B.createAllocGlobal(vd, silG);
    SILValue addr = B.createGlobalAddr(vd, silG);
    if (isUninitialized)
      addr = B.createMarkUninitializedVar(vd, addr);

    VarLocs[vd] = SILGenFunction::VarLoc::get(addr);
    Result = InitializationPtr(new KnownAddressInitialization(addr));
  } else {
    Optional<MarkUninitializedInst::Kind> uninitKind;
    if (isUninitialized) {
      uninitKind = MarkUninitializedInst::Kind::Var;
    }
    Result = emitLocalVariableWithCleanup(vd, uninitKind);
  }

  // If we're initializing a weak or unowned variable, this requires a change in
  // type.
  if (isa<ReferenceStorageType>(varType))
    Result = InitializationPtr(new
                           ReferenceStorageInitialization(std::move(Result)));
  return Result;
}

void SILGenFunction::emitPatternBinding(PatternBindingDecl *PBD,
                                        unsigned pbdEntry) {
  auto &entry = PBD->getPatternList()[pbdEntry];
  auto initialization = emitPatternBindingInitialization(entry.getPattern(),
                                                         JumpDest::invalid());

  // If an initial value expression was specified by the decl, emit it into
  // the initialization. Otherwise, mark it uninitialized for DI to resolve.
  if (auto *Init = entry.getInit()) {
    FullExpr Scope(Cleanups, CleanupLocation(Init));
    emitExprInto(Init, initialization.get(), SILLocation(PBD));
  } else {
    initialization->finishUninitialized(*this);
  }
}

void SILGenFunction::visitPatternBindingDecl(PatternBindingDecl *PBD) {

  // Allocate the variables and build up an Initialization over their
  // allocated storage.
  for (unsigned i : indices(PBD->getPatternList())) {
    emitPatternBinding(PBD, i);
  }
}

void SILGenFunction::visitVarDecl(VarDecl *D) {
  // We handle emitting the variable storage when we see the pattern binding.
  // Here we just emit the behavior witness table, if any.
  
  if (D->hasBehavior())
    SGM.emitPropertyBehavior(D);
}

/// Emit a check that returns 1 if the running OS version is in
/// the specified version range and 0 otherwise. The returned SILValue
/// (which has type Builtin.Int1) represents the result of this check.
SILValue SILGenFunction::emitOSVersionRangeCheck(SILLocation loc,
                                                 const VersionRange &range) {
  // Emit constants for the checked version range.
  clang::VersionTuple Vers = range.getLowerEndpoint();
  unsigned major = Vers.getMajor();
  unsigned minor =
      (Vers.getMinor().hasValue() ? Vers.getMinor().getValue() : 0);
  unsigned subminor =
      (Vers.getSubminor().hasValue() ? Vers.getSubminor().getValue() : 0);

  SILType wordType = SILType::getBuiltinWordType(getASTContext());

  SILValue majorValue = B.createIntegerLiteral(loc, wordType, major);
  SILValue minorValue = B.createIntegerLiteral(loc, wordType, minor);
  SILValue subminorValue = B.createIntegerLiteral(loc, wordType, subminor);

  // Emit call to _stdlib_isOSVersionAtLeast(major, minor, patch)
  FuncDecl *versionQueryDecl =
      getASTContext().getIsOSVersionAtLeastDecl(nullptr);
  assert(versionQueryDecl);

  auto silDeclRef = SILDeclRef(versionQueryDecl);
  SILValue availabilityGTEFn = emitGlobalFunctionRef(
      loc, silDeclRef, getConstantInfo(silDeclRef));

  SILValue args[] = {majorValue, minorValue, subminorValue};
  return B.createApply(loc, availabilityGTEFn, args, false);
}


/// Emit the boolean test and/or pattern bindings indicated by the specified
/// stmt condition.  If the condition fails, control flow is transferred to the
/// specified JumpDest.  The insertion point is left in the block where the
/// condition has matched and any bound variables are in scope.
///
void SILGenFunction::emitStmtCondition(StmtCondition Cond, JumpDest FailDest,
                                       SILLocation loc,
                                       ProfileCounter NumTrueTaken,
                                       ProfileCounter NumFalseTaken) {

  assert(B.hasValidInsertionPoint() &&
         "emitting condition at unreachable point");
  
  for (const auto &elt : Cond) {
    SILLocation booleanTestLoc = loc;
    SILValue booleanTestValue;

    switch (elt.getKind()) {
    case StmtConditionElement::CK_PatternBinding: {
      InitializationPtr initialization =
      InitializationForPattern(*this, FailDest).visit(elt.getPattern());

      // Emit the initial value into the initialization.
      FullExpr Scope(Cleanups, CleanupLocation(elt.getInitializer()));
      emitExprInto(elt.getInitializer(), initialization.get());
      // Pattern bindings handle their own tests, we don't need a boolean test.
      continue;
    }

    case StmtConditionElement::CK_Boolean: { // Handle boolean conditions.
      auto *expr = elt.getBoolean();
      // Evaluate the condition as an i1 value (guaranteed by Sema).
      FullExpr Scope(Cleanups, CleanupLocation(expr));
      booleanTestValue = emitRValue(expr).forwardAsSingleValue(*this, expr);
      booleanTestLoc = expr;
      break;
    }
    case StmtConditionElement::CK_Availability:
      // Check the running OS version to determine whether it is in the range
      // specified by elt.
      VersionRange OSVersion = elt.getAvailability()->getAvailableRange();
      assert(!OSVersion.isEmpty());

      if (OSVersion.isAll()) {
        // If there's no check for the current platform, this condition is
        // trivially true.
        SILType i1 = SILType::getBuiltinIntegerType(1, getASTContext());
        booleanTestValue = B.createIntegerLiteral(loc, i1, true);
      } else {
        booleanTestValue = emitOSVersionRangeCheck(loc, OSVersion);
      }
      break;
    }

    // Now that we have a boolean test as a Builtin.i1, emit the branch.
    assert(booleanTestValue->getType().
           castTo<BuiltinIntegerType>()->isFixedWidth(1) &&
           "Sema forces conditions to have Builtin.i1 type");
    
    // Just branch on the condition.  On failure, we unwind any active cleanups,
    // on success we fall through to a new block.
    SILBasicBlock *ContBB = createBasicBlock();
    auto FailBB = Cleanups.emitBlockForCleanups(FailDest, loc);
    B.createCondBranch(booleanTestLoc, booleanTestValue, ContBB, FailBB,
                       NumTrueTaken, NumFalseTaken);

    // Finally, emit the continue block and keep emitting the rest of the
    // condition.
    B.emitBlock(ContBB);
  }
}

InitializationPtr
SILGenFunction::emitPatternBindingInitialization(Pattern *P,
                                                 JumpDest failureDest) {
  return InitializationForPattern(*this, failureDest).visit(P);
}

/// Enter a cleanup to deallocate the given location.
CleanupHandle SILGenFunction::enterDeallocStackCleanup(SILValue temp) {
  assert(temp->getType().isAddress() &&  "dealloc must have an address type");
  Cleanups.pushCleanup<DeallocStackCleanup>(temp);
  return Cleanups.getTopCleanup();
}

CleanupHandle SILGenFunction::enterDestroyCleanup(SILValue valueOrAddr) {
  Cleanups.pushCleanup<ReleaseValueCleanup>(valueOrAddr);
  return Cleanups.getTopCleanup();
}

namespace {
  /// A cleanup that deinitializes an opaque existential container
  /// before a value has been stored into it, or after its value was taken.
  class DeinitExistentialCleanup: public Cleanup {
    SILValue existentialAddr;
    CanType concreteFormalType;
    ExistentialRepresentation repr;
  public:
    DeinitExistentialCleanup(SILValue existentialAddr,
                             CanType concreteFormalType,
                             ExistentialRepresentation repr)
      : existentialAddr(existentialAddr),
        concreteFormalType(concreteFormalType),
        repr(repr) {}
    
    void emit(SILGenFunction &SGF, CleanupLocation l) override {
      switch (repr) {
      case ExistentialRepresentation::None:
      case ExistentialRepresentation::Class:
      case ExistentialRepresentation::Metatype:
        llvm_unreachable("cannot cleanup existential");
      case ExistentialRepresentation::Opaque:
        if (SGF.silConv.useLoweredAddresses()) {
          SGF.B.createDeinitExistentialAddr(l, existentialAddr);
        } else {
          SGF.B.createDeinitExistentialValue(l, existentialAddr);
        }
        break;
      case ExistentialRepresentation::Boxed:
        SGF.B.createDeallocExistentialBox(l, concreteFormalType,
                                          existentialAddr);
        break;
      }
    }

    void dump(SILGenFunction &) const override {
#ifndef NDEBUG
      llvm::errs() << "DeinitExistentialCleanup\n"
                   << "State:" << getState() << "\n"
                   << "Value:" << existentialAddr << "\n";
#endif
    }
  };
} // end anonymous namespace

/// Enter a cleanup to emit a DeinitExistentialAddr or DeinitExistentialBox
/// of the specified value.
CleanupHandle SILGenFunction::enterDeinitExistentialCleanup(
                                               SILValue valueOrAddr,
                                               CanType concreteFormalType,
                                               ExistentialRepresentation repr) {
  Cleanups.pushCleanup<DeinitExistentialCleanup>(valueOrAddr,
                                                 concreteFormalType,
                                                 repr);
  return Cleanups.getTopCleanup();
}

void SILGenModule::emitExternalWitnessTable(ProtocolConformance *c) {
  auto root = c->getRootNormalConformance();
  // Emit the witness table right now if we used it.
  if (usedConformances.count(root)) {
    getWitnessTable(c);
    return;
  }
  // Otherwise, remember it for later.
  delayedConformances.insert({root, {lastEmittedConformance}});
  lastEmittedConformance = root;
}

void SILGenModule::emitExternalDefinition(Decl *d) {
  switch (d->getKind()) {
  case DeclKind::Func: {
    emitFunction(cast<FuncDecl>(d));
    break;
  }
  case DeclKind::Constructor: {
    auto C = cast<ConstructorDecl>(d);
    // For factories, we don't need to emit a special thunk; the normal
    // foreign-to-native thunk is sufficient.
    if (C->isFactoryInit())
      break;

    emitConstructor(C);
    break;
  }
  case DeclKind::Enum:
  case DeclKind::Struct:
  case DeclKind::Class: {
    // Emit witness tables.
    auto nom = cast<NominalTypeDecl>(d);
    for (auto c : nom->getLocalConformances(ConformanceLookupKind::All,
                                            nullptr, /*sorted=*/true)) {
      auto *proto = c->getProtocol();
      if (Lowering::TypeConverter::protocolRequiresWitnessTable(proto) &&
          isa<NormalProtocolConformance>(c) &&
          c->isComplete())
        emitExternalWitnessTable(c);
    }
    break;
  }

  case DeclKind::Protocol:
    // Nothing to do in SILGen for other external types.
    break;

  case DeclKind::Var:
    // Imported static vars are handled solely in IRGen.
    break;

  case DeclKind::IfConfig:
  case DeclKind::Extension:
  case DeclKind::PatternBinding:
  case DeclKind::EnumCase:
  case DeclKind::EnumElement:
  case DeclKind::TopLevelCode:
  case DeclKind::TypeAlias:
  case DeclKind::AssociatedType:
  case DeclKind::GenericTypeParam:
  case DeclKind::Param:
  case DeclKind::Import:
  case DeclKind::Subscript:
  case DeclKind::Destructor:
  case DeclKind::InfixOperator:
  case DeclKind::PrefixOperator:
  case DeclKind::PostfixOperator:
  case DeclKind::PrecedenceGroup:
  case DeclKind::Module:
  case DeclKind::MissingMember:
    llvm_unreachable("Not a valid external definition for SILGen");
  }
}

/// Create a LocalVariableInitialization for the uninitialized var.
InitializationPtr SILGenFunction::emitLocalVariableWithCleanup(
    VarDecl *vd, Optional<MarkUninitializedInst::Kind> kind, unsigned ArgNo) {
  return InitializationPtr(
      new LocalVariableInitialization(vd, kind, ArgNo, *this));
}

/// Create an Initialization for an uninitialized temporary.
std::unique_ptr<TemporaryInitialization>
SILGenFunction::emitTemporary(SILLocation loc, const TypeLowering &tempTL) {
  SILValue addr = emitTemporaryAllocation(loc, tempTL.getLoweredType());
  return useBufferAsTemporary(addr, tempTL);
}

std::unique_ptr<TemporaryInitialization>
SILGenFunction::emitFormalAccessTemporary(SILLocation loc,
                                          const TypeLowering &tempTL) {
  SILValue addr = emitTemporaryAllocation(loc, tempTL.getLoweredType());
  CleanupHandle cleanup =
      enterDormantFormalAccessTemporaryCleanup(addr, loc, tempTL);
  return std::unique_ptr<TemporaryInitialization>(
      new TemporaryInitialization(addr, cleanup));
}

/// Create an Initialization for an uninitialized buffer.
std::unique_ptr<TemporaryInitialization>
SILGenFunction::useBufferAsTemporary(SILValue addr,
                                     const TypeLowering &tempTL) {
  CleanupHandle cleanup = enterDormantTemporaryCleanup(addr, tempTL);
  return std::unique_ptr<TemporaryInitialization>(
                                    new TemporaryInitialization(addr, cleanup));
}

CleanupHandle
SILGenFunction::enterDormantTemporaryCleanup(SILValue addr,
                                             const TypeLowering &tempTL) {
  if (tempTL.isTrivial())
    return CleanupHandle::invalid();

  Cleanups.pushCleanupInState<ReleaseValueCleanup>(CleanupState::Dormant, addr);
  return Cleanups.getCleanupsDepth();
}

namespace {

struct FormalAccessReleaseValueCleanup : Cleanup {
  FormalEvaluationContext::stable_iterator Depth;

  FormalAccessReleaseValueCleanup() : Depth() {}

  void setState(SILGenFunction &SGF, CleanupState newState) override {
    if (newState == CleanupState::Dead) {
      getEvaluation(SGF).setFinished();
    }

    state = newState;
  }

  void emit(SILGenFunction &SGF, CleanupLocation l) override {
    getEvaluation(SGF).finish(SGF);
  }

  void dump(SILGenFunction &SGF) const override {
#ifndef NDEBUG
    llvm::errs() << "FormalAccessReleaseValueCleanup "
                 << "State:" << getState() << "\n"
                 << "Value:" << getValue(SGF) << "\n";
#endif
  }

  OwnedFormalAccess &getEvaluation(SILGenFunction &SGF) const {
    auto &evaluation = *SGF.FormalEvalContext.find(Depth);
    assert(evaluation.getKind() == FormalAccess::Owned);
    return static_cast<OwnedFormalAccess &>(evaluation);
  }

  SILValue getValue(SILGenFunction &SGF) const {
    return getEvaluation(SGF).getValue();
  }
};

} // end anonymous namespace

ManagedValue
SILGenFunction::emitFormalAccessManagedBufferWithCleanup(SILLocation loc,
                                                         SILValue addr) {
  assert(InFormalEvaluationScope && "Must be in formal evaluation scope");
  auto &lowering = getTypeLowering(addr->getType());
  if (lowering.isTrivial())
    return ManagedValue::forUnmanaged(addr);

  auto &cleanup = Cleanups.pushCleanup<FormalAccessReleaseValueCleanup>();
  CleanupHandle handle = Cleanups.getTopCleanup();
  FormalEvalContext.push<OwnedFormalAccess>(loc, handle, addr);
  cleanup.Depth = FormalEvalContext.stable_begin();
  return ManagedValue(addr, handle);
}

ManagedValue
SILGenFunction::emitFormalAccessManagedRValueWithCleanup(SILLocation loc,
                                                         SILValue value) {
  assert(InFormalEvaluationScope && "Must be in formal evaluation scope");
  auto &lowering = getTypeLowering(value->getType());
  if (lowering.isTrivial())
    return ManagedValue::forUnmanaged(value);

  auto &cleanup = Cleanups.pushCleanup<FormalAccessReleaseValueCleanup>();
  CleanupHandle handle = Cleanups.getTopCleanup();
  FormalEvalContext.push<OwnedFormalAccess>(loc, handle, value);
  cleanup.Depth = FormalEvalContext.stable_begin();
  return ManagedValue(value, handle);
}

CleanupHandle SILGenFunction::enterDormantFormalAccessTemporaryCleanup(
    SILValue addr, SILLocation loc, const TypeLowering &tempTL) {
  assert(InFormalEvaluationScope && "Must be in formal evaluation scope");
  if (tempTL.isTrivial())
    return CleanupHandle::invalid();

  auto &cleanup = Cleanups.pushCleanup<FormalAccessReleaseValueCleanup>();
  CleanupHandle handle = Cleanups.getTopCleanup();
  Cleanups.setCleanupState(handle, CleanupState::Dormant);
  FormalEvalContext.push<OwnedFormalAccess>(loc, handle, addr);
  cleanup.Depth = FormalEvalContext.stable_begin();
  return handle;
}

void SILGenFunction::destroyLocalVariable(SILLocation silLoc, VarDecl *vd) {
  assert(vd->getDeclContext()->isLocalContext() &&
         "can't emit a local var for a non-local var decl");
  assert(vd->hasStorage() && "can't emit storage for a computed variable");

  assert(VarLocs.count(vd) && "var decl wasn't emitted?!");

  auto loc = VarLocs[vd];

  // For a heap variable, the box is responsible for the value. We just need
  // to give up our retain count on it.
  if (loc.box) {
    B.emitDestroyValueOperation(silLoc, loc.box);
    return;
  }

  // For 'let' bindings, we emit a release_value or destroy_addr, depending on
  // whether we have an address or not.
  SILValue Val = loc.value;
  if (!Val->getType().isAddress())
    B.emitDestroyValueOperation(silLoc, Val);
  else
    B.createDestroyAddr(silLoc, Val);
}

void SILGenFunction::deallocateUninitializedLocalVariable(SILLocation silLoc,
                                                          VarDecl *vd) {
  assert(vd->getDeclContext()->isLocalContext() &&
         "can't emit a local var for a non-local var decl");
  assert(vd->hasStorage() && "can't emit storage for a computed variable");

  assert(VarLocs.count(vd) && "var decl wasn't emitted?!");

  auto loc = VarLocs[vd];

  // Ignore let values captured without a memory location.
  if (!loc.value->getType().isAddress()) return;

  assert(loc.box && "captured var should have been given a box");
  B.createDeallocBox(silLoc, loc.box);
}
