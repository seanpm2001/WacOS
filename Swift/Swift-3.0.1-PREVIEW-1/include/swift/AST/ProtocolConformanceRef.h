//===--- ProtocolConformanceRef.h - AST Protocol Conformance ----*- C++ -*-===//
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
// This file defines the ProtocolConformanceRef type.
//
//===----------------------------------------------------------------------===//
#ifndef SWIFT_AST_PROTOCOLCONFORMANCEREF_H
#define SWIFT_AST_PROTOCOLCONFORMANCEREF_H

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/PointerUnion.h"
#include "swift/AST/TypeAlignments.h"

namespace llvm {
  class raw_ostream;
}

namespace swift {

/// A ProtocolConformanceRef is a handle to a protocol conformance which
/// may be either concrete or abstract.
///
/// A concrete conformance is derived from a specific protocol conformance
/// declaration.
///
/// An abstract conformance is derived from context: the conforming type
/// is either existential or opaque (i.e. an archetype), and while the
/// type-checker promises that the conformance exists, it is not known
/// statically which concrete conformance it refers to.
///
/// ProtocolConformanceRef allows the efficient recovery of the protocol
/// even when the conformance is abstract.
class ProtocolConformanceRef {
  using UnionType = llvm::PointerUnion<ProtocolDecl*, ProtocolConformance*>;
  UnionType Union;

  explicit ProtocolConformanceRef(UnionType value) : Union(value) {
    assert(value && "cannot construct ProtocolConformanceRef with null");
  }
public:
  /// Create an abstract protocol conformance reference.
  explicit ProtocolConformanceRef(ProtocolDecl *proto) : Union(proto) {
    assert(proto != nullptr &&
           "cannot construct ProtocolConformanceRef with null");
  }

  /// Create a concrete protocol conformance reference.
  explicit ProtocolConformanceRef(ProtocolConformance *conf) : Union(conf) {
    assert(conf != nullptr &&
           "cannot construct ProtocolConformanceRef with null");
  }

  /// Create either a concrete or an abstract protocol conformance reference,
  /// depending on whether ProtocolConformance is null.
  explicit ProtocolConformanceRef(ProtocolDecl *protocol,
                                  ProtocolConformance *conf);

  bool isConcrete() const { return Union.is<ProtocolConformance*>(); }
  ProtocolConformance *getConcrete() const {
    return Union.get<ProtocolConformance*>();
  }

  bool isAbstract() const { return Union.is<ProtocolDecl*>(); }
  ProtocolDecl *getAbstract() const {
    return Union.get<ProtocolDecl*>();
  }

  using OpaqueValue = void*;
  OpaqueValue getOpaqueValue() const { return Union.getOpaqueValue(); }
  static ProtocolConformanceRef getFromOpaqueValue(OpaqueValue value) {
    return ProtocolConformanceRef(UnionType::getFromOpaqueValue(value));
  }

  /// Return the protocol requirement.
  ProtocolDecl *getRequirement() const;
  
  /// Get the inherited conformance corresponding to the given protocol.
  /// Returns `this` if `parent` is already the same as the protocol this
  /// conformance represents.
  ProtocolConformanceRef getInherited(ProtocolDecl *parent) const;

  void dump() const;
  void dump(llvm::raw_ostream &out, unsigned indent = 0) const;

  bool operator==(ProtocolConformanceRef other) const {
    return Union == other.Union;
  }
  bool operator!=(ProtocolConformanceRef other) const {
    return Union != other.Union;
  }

  friend llvm::hash_code hash_value(ProtocolConformanceRef conformance) {
    return llvm::hash_value(conformance.Union.getOpaqueValue());
  }
};

} // end namespace swift

#endif // LLVM_SWIFT_AST_PROTOCOLCONFORMANCEREF_H
