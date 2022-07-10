//===--- ManagedValue.cpp - Value with cleanup ----------------------------===//
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
ManagedValue ManagedValue::copy(SILGenFunction &gen, SILLocation l) {
  if (!cleanup.isValid()) {
    assert(gen.getTypeLowering(getType()).isTrivial());
    return *this;
  }
  
  auto &lowering = gen.getTypeLowering(getType());
  assert(!lowering.isTrivial() && "trivial value has cleanup?");
  
  if (!lowering.isAddressOnly()) {
    lowering.emitRetainValue(gen.B, l, getValue());
    return gen.emitManagedRValueWithCleanup(getValue(), lowering);
  }
  
  SILValue buf = gen.emitTemporaryAllocation(l, getType());
  gen.B.createCopyAddr(l, getValue(), buf, IsNotTake, IsInitialization);
  return gen.emitManagedRValueWithCleanup(buf, lowering);
}

/// Store a copy of this value with independent ownership into the given
/// uninitialized address.
void ManagedValue::copyInto(SILGenFunction &gen, SILValue dest, SILLocation L) {
  auto &lowering = gen.getTypeLowering(getType());
  if (lowering.isAddressOnly()) {
    gen.B.createCopyAddr(L, getValue(), dest,
                         IsNotTake, IsInitialization);
    return;
  }
  lowering.emitRetainValue(gen.B, L, getValue());
  gen.B.createStore(L, getValue(), dest);
}

/// This is the same operation as 'copy', but works on +0 values that don't
/// have cleanups.  It returns a +1 value with one.
ManagedValue ManagedValue::copyUnmanaged(SILGenFunction &gen, SILLocation loc) {
  auto &lowering = gen.getTypeLowering(getType());
  
  if (lowering.isTrivial())
    return *this;
  
  SILValue result;
  if (!lowering.isAddressOnly()) {
    lowering.emitRetainValue(gen.B, loc, getValue());
    result = getValue();
  } else {
    result = gen.emitTemporaryAllocation(loc, getType());
    gen.B.createCopyAddr(loc, getValue(), result, IsNotTake,IsInitialization);
  }
  return gen.emitManagedRValueWithCleanup(result, lowering);
}

/// Disable the cleanup for this value.
void ManagedValue::forwardCleanup(SILGenFunction &gen) const {
  assert(hasCleanup() && "value doesn't have cleanup!");
  gen.Cleanups.forwardCleanup(getCleanup());
}

/// Forward this value, deactivating the cleanup and returning the
/// underlying value.
SILValue ManagedValue::forward(SILGenFunction &gen) const {
  if (hasCleanup())
    forwardCleanup(gen);
  return getValue();
}

void ManagedValue::forwardInto(SILGenFunction &gen, SILLocation loc,
                               SILValue address) {
  if (hasCleanup())
    forwardCleanup(gen);
  auto &addrTL = gen.getTypeLowering(address->getType());
  gen.emitSemanticStore(loc, getValue(), address, addrTL, IsInitialization);
}

void ManagedValue::assignInto(SILGenFunction &gen, SILLocation loc,
                              SILValue address) {
  if (hasCleanup())
    forwardCleanup(gen);
  
  auto &addrTL = gen.getTypeLowering(address->getType());
  gen.emitSemanticStore(loc, getValue(), address, addrTL,
                        IsNotInitialization);
}
