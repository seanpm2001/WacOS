//===--- Address.h - Address Representation ---------------------*- C++ -*-===//
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
// A structure for holding the address of an object.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IRGEN_ADDRESS_H
#define SWIFT_IRGEN_ADDRESS_H

#include "IRGen.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Instruction.h"
#include "llvm/ADT/ilist.h"
#include "llvm/IR/DerivedTypes.h"

namespace swift {
namespace irgen {

/// The address of an object in memory.
class Address {
  llvm::Value *Addr;
  Alignment Align;

public:
  Address() : Addr(nullptr) {}
  Address(llvm::Value *addr, Alignment align) : Addr(addr), Align(align) {
    assert(addr != nullptr && "building an invalid address");
  }
  Address(llvm::ilist_iterator<llvm::Argument> addr, Alignment align)
      : Address(&*addr, align) {}
  Address(llvm::ilist_iterator<llvm::Instruction> addr, Alignment align)
      : Address(&*addr, align) {}

  llvm::Value *operator->() const {
    assert(isValid());
    return getAddress();
  }

  bool isValid() const { return Addr != nullptr; }

  llvm::Value *getAddress() const { return Addr; }

  Alignment getAlignment() const {
    return Align;
  }
  
  llvm::PointerType *getType() const {
    return cast<llvm::PointerType>(Addr->getType());
  }
};

/// An address in memory together with the (possibly null) heap
/// allocation which owns it.
class OwnedAddress {
  Address Addr;
  llvm::Value *Owner;

public:
  OwnedAddress() : Owner(nullptr) {}
  OwnedAddress(Address address, llvm::Value *owner)
    : Addr(address), Owner(owner) {}

  llvm::Value *getAddressPointer() const { return Addr.getAddress(); }
  Alignment getAlignment() const { return Addr.getAlignment(); }
  Address getAddress() const { return Addr; }
  llvm::Value *getOwner() const { return Owner; }

  Address getUnownedAddress() const {
    assert(getOwner() == nullptr);
    return getAddress();
  }

  operator Address() const { return getAddress(); }

  bool isValid() const { return Addr.isValid(); }
};

/// An address in memory together with the local allocation which owns it.
class ContainedAddress {
  /// The address of an object of type T.
  Address Addr;

  /// The container of the address.
  Address Container;

public:
  ContainedAddress() {}
  ContainedAddress(Address container, Address address)
    : Addr(address), Container(container) {}

  llvm::Value *getAddressPointer() const { return Addr.getAddress(); }
  Alignment getAlignment() const { return Addr.getAlignment(); }
  Address getAddress() const { return Addr; }
  Address getContainer() const { return Container; }

  bool isValid() const { return Addr.isValid(); }
};

} // end namespace irgen
} // end namespace swift

#endif
