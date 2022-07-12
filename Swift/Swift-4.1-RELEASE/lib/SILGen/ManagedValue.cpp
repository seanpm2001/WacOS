//===--- ManagedValue.cpp - Value with cleanup ----------------------------===//
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
// A storage structure for holding a destructured rvalue with an optional
// cleanup(s).
// Ownership of the rvalue can be "forwarded" to disable the associated
// cleanup(s).
//
//===----------------------------------------------------------------------===//

#include "ManagedValue.h"
#include "SILGenFunction.h"
using namespace swift;
using namespace Lowering;

/// Emit a copy of this value with independent ownership.
ManagedValue ManagedValue::copy(SILGenFunction &SGF, SILLocation loc) const {
  auto &lowering = SGF.getTypeLowering(getType());
  if (lowering.isTrivial())
    return *this;

  if (getType().isObject()) {
    return SGF.B.createCopyValue(loc, *this, lowering);
  }

  SILValue buf = SGF.emitTemporaryAllocation(loc, getType());
  SGF.B.createCopyAddr(loc, getValue(), buf, IsNotTake, IsInitialization);
  return SGF.emitManagedRValueWithCleanup(buf, lowering);
}

/// Emit a copy of this value with independent ownership.
ManagedValue ManagedValue::formalAccessCopy(SILGenFunction &SGF,
                                            SILLocation loc) {
  assert(SGF.InFormalEvaluationScope && "Can only perform a formal access copy in a "
                                 "formal evaluation scope");
  auto &lowering = SGF.getTypeLowering(getType());
  if (lowering.isTrivial())
    return *this;

  if (getType().isObject()) {
    return SGF.B.createFormalAccessCopyValue(loc, *this);
  }

  SILValue buf = SGF.emitTemporaryAllocation(loc, getType());
  return SGF.B.createFormalAccessCopyAddr(loc, *this, buf, IsNotTake,
                                          IsInitialization);
}

/// Store a copy of this value with independent ownership into the given
/// uninitialized address.
void ManagedValue::copyInto(SILGenFunction &SGF, SILValue dest,
                            SILLocation loc) {
  auto &lowering = SGF.getTypeLowering(getType());
  if (lowering.isAddressOnly() && SGF.silConv.useLoweredAddresses()) {
    SGF.B.createCopyAddr(loc, getValue(), dest, IsNotTake, IsInitialization);
    return;
  }

  SILValue copy = lowering.emitCopyValue(SGF.B, loc, getValue());
  lowering.emitStoreOfCopy(SGF.B, loc, copy, dest, IsInitialization);
}

/// This is the same operation as 'copy', but works on +0 values that don't
/// have cleanups.  It returns a +1 value with one.
ManagedValue ManagedValue::copyUnmanaged(SILGenFunction &SGF, SILLocation loc) {
  if (getType().isObject()) {
    return SGF.B.createCopyValue(loc, *this);
  }

  SILValue result = SGF.emitTemporaryAllocation(loc, getType());
  SGF.B.createCopyAddr(loc, getValue(), result, IsNotTake, IsInitialization);
  return SGF.emitManagedRValueWithCleanup(result);
}

/// This is the same operation as 'copy', but works on +0 values that don't
/// have cleanups.  It returns a +1 value with one.
ManagedValue ManagedValue::formalAccessCopyUnmanaged(SILGenFunction &SGF,
                                                     SILLocation loc) {
  if (getType().isObject()) {
    return SGF.B.createFormalAccessCopyValue(loc, *this);
  }

  SILValue result = SGF.emitTemporaryAllocation(loc, getType());
  return SGF.B.createFormalAccessCopyAddr(loc, *this, result, IsNotTake,
                                          IsInitialization);
}

/// Disable the cleanup for this value.
void ManagedValue::forwardCleanup(SILGenFunction &SGF) const {
  assert(hasCleanup() && "value doesn't have cleanup!");
  SGF.Cleanups.forwardCleanup(getCleanup());
}

/// Forward this value, deactivating the cleanup and returning the
/// underlying value.
SILValue ManagedValue::forward(SILGenFunction &SGF) const {
  if (hasCleanup())
    forwardCleanup(SGF);
  return getValue();
}

void ManagedValue::forwardInto(SILGenFunction &SGF, SILLocation loc,
                               SILValue address) {
  if (!hasCleanup() && getOwnershipKind() != ValueOwnershipKind::Trivial)
    return copyUnmanaged(SGF, loc).forwardInto(SGF, loc, address);

  if (hasCleanup())
    forwardCleanup(SGF);

  auto &addrTL = SGF.getTypeLowering(address->getType());
  SGF.emitSemanticStore(loc, getValue(), address,
                        addrTL, IsInitialization);
}

void ManagedValue::assignInto(SILGenFunction &SGF, SILLocation loc,
                              SILValue address) {
  if (hasCleanup())
    forwardCleanup(SGF);
  
  auto &addrTL = SGF.getTypeLowering(address->getType());
  SGF.emitSemanticStore(loc, getValue(), address, addrTL,
                        IsNotInitialization);
}

void ManagedValue::forwardInto(SILGenFunction &SGF, SILLocation loc,
                               Initialization *dest) {
  dest->copyOrInitValueInto(SGF, loc, *this, /*isInit*/ true);
  dest->finishInitialization(SGF);
}

ManagedValue ManagedValue::borrow(SILGenFunction &SGF, SILLocation loc) const {
  assert(getValue() && "cannot borrow an invalid or in-context value");
  if (isLValue())
    return *this;
  if (getType().isAddress())
    return ManagedValue::forUnmanaged(getValue());
  return SGF.emitManagedBeginBorrow(loc, getValue());
}

ManagedValue ManagedValue::formalAccessBorrow(SILGenFunction &SGF,
                                              SILLocation loc) const {
  assert(getValue() && "cannot borrow an invalid or in-context value");
  if (isLValue())
    return *this;
  if (getType().isAddress())
    return ManagedValue::forUnmanaged(getValue());
  return SGF.emitFormalEvaluationManagedBeginBorrow(loc, getValue());
}

ManagedValue ManagedValue::materialize(SILGenFunction &SGF,
                                       SILLocation loc) const {
  auto temporary = SGF.emitTemporaryAllocation(loc, getType());
  bool hadCleanup = hasCleanup();

  // The temporary memory is +0 if the value was.
  if (hadCleanup) {
    SGF.B.emitStoreValueOperation(loc, forward(SGF), temporary,
                                  StoreOwnershipQualifier::Init);

    // SEMANTIC SIL TODO: This should really be called a temporary LValue.
    return ManagedValue::forOwnedAddressRValue(temporary,
                                          SGF.enterDestroyCleanup(temporary));
  } else {
    auto object = SGF.emitManagedBeginBorrow(loc, getValue());
    SGF.emitManagedStoreBorrow(loc, object.getValue(), temporary);
    return ManagedValue::forBorrowedAddressRValue(temporary);
  }
}

void ManagedValue::print(raw_ostream &os) const {
  if (SILValue v = getValue()) {
    v->print(os);
  }
}

void ManagedValue::dump() const {
  dump(llvm::errs());
}

void ManagedValue::dump(raw_ostream &os, unsigned indent) const {
  os.indent(indent);
  if (isInContext()) {
    os << "InContext\n";
    return;
  }
  if (isLValue()) os << "[lvalue] ";
  if (hasCleanup()) os << "[cleanup] ";
  if (SILValue v = getValue()) {
    v->print(os);
  } else {
    os << "<null>\n";
  }
}

ManagedValue ManagedValue::ensurePlusOne(SILGenFunction &SGF,
                                         SILLocation loc) const {
  // guaranteed-normal-args-todo: We only copy here when guaranteed normal args
  // are explicitly enabled. Otherwise, this always just returns self.
  if (SGF.getOptions().EnableGuaranteedNormalArguments && !hasCleanup()) {
    return copy(SGF, loc);
  }
  return *this;
}
