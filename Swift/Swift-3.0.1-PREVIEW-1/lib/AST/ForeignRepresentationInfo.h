//===--- ForeignRepresentationInfo.h - Used in bridging queries -*- C++ -*-===//
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

#ifndef SWIFT_FOREIGNREPRESENTATIONINFO_H
#define SWIFT_FOREIGNREPRESENTATIONINFO_H

#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/Type.h"
#include "swift/Basic/LLVM.h"
#include "llvm/ADT/PointerEmbeddedInt.h"
#include "llvm/ADT/PointerIntPair.h"

namespace swift {

class ForeignRepresentationInfo {
  using PayloadTy =
    llvm::PointerEmbeddedInt<uintptr_t, sizeof(uintptr_t) * CHAR_BIT - 3>;

  /// The low three bits store a ForeignRepresentableKind.
  ///
  /// When the ForeignRepresentableKind == None, the upper bits are
  /// the generation count at which this negative result was last checked.
  /// When it's Bridged, it's the conformance that describes the bridging.
  /// When it's Trivial, it's simply a flag stating whether Optional is
  /// supported.
  llvm::PointerIntPair<PayloadTy, 3, ForeignRepresentableKind> Storage;

public:
  /// Retrieve a cache entry for a non-foreign-representable type.
  static ForeignRepresentationInfo forNone(unsigned generation = 0) {
    ForeignRepresentationInfo result;
    result.Storage = { generation, ForeignRepresentableKind::None };
    return result;
  }

  // Retrieve a cache entry for a trivially representable type.
  static ForeignRepresentationInfo forTrivial() {
    ForeignRepresentationInfo result;
    result.Storage = { 0, ForeignRepresentableKind::Trivial };
    return result;
  }

  // Retrieve a cache entry for a trivially representable type that can also
  // be optional.
  static ForeignRepresentationInfo forTrivialWithOptional() {
    ForeignRepresentationInfo result;
    result.Storage = { 1, ForeignRepresentableKind::Trivial };
    return result;
  }

  // Retrieve a cache entry for a bridged representable type.
  static ForeignRepresentationInfo
  forBridged(ProtocolConformance *conformance) {
    using PayloadTraits = llvm::PointerLikeTypeTraits<PayloadTy>;
    ForeignRepresentationInfo result;
    result.Storage = {PayloadTraits::getFromVoidPointer(conformance),
                      ForeignRepresentableKind::Bridged};
    assert(result.getConformance() == conformance && "insufficiently aligned");
    return result;
  }

  // Retrieve a cache entry for a trivially representable type that can also
  // be optional.
  static ForeignRepresentationInfo forBridgedError() {
    ForeignRepresentationInfo result;
    result.Storage = { 0, ForeignRepresentableKind::BridgedError };
    return result;
  }

  /// Retrieve the foreign representable kind.
  ForeignRepresentableKind getKind() const {
    return Storage.getInt();
  }

  /// Retrieve the generation for a non-representable type.
  unsigned getGeneration() const {
    assert(getKind() == ForeignRepresentableKind::None);
    return Storage.getPointer();
  }

  /// Retrieve the protocol conformance that makes it representable.
  ProtocolConformance *getConformance() const {
    switch (getKind()) {
    case ForeignRepresentableKind::None:
      llvm_unreachable("this type is not representable");

    case ForeignRepresentableKind::Trivial:
    case ForeignRepresentableKind::BridgedError:
      return nullptr;

    case ForeignRepresentableKind::Bridged: {
      using PayloadTraits = llvm::PointerLikeTypeTraits<PayloadTy>;
      auto payload = PayloadTraits::getAsVoidPointer(Storage.getPointer());
      return static_cast<ProtocolConformance *>(payload);
    }

    case ForeignRepresentableKind::Object:
    case ForeignRepresentableKind::StaticBridged:
      llvm_unreachable("unexpected kind in ForeignRepresentableCacheEntry");
    }
  }

  /// Returns true if the optional version of this type is also representable.
  bool isRepresentableAsOptional() const {
    switch (getKind()) {
    case ForeignRepresentableKind::None:
      llvm_unreachable("this type is not representable");

    case ForeignRepresentableKind::Trivial:
      return Storage.getPointer() != 0;

    case ForeignRepresentableKind::Bridged: {
      auto KPK_ObjectiveCBridgeable = KnownProtocolKind::ObjectiveCBridgeable;
      ProtocolDecl *proto = getConformance()->getProtocol();
      assert(proto->isSpecificProtocol(KPK_ObjectiveCBridgeable) &&
             "unknown protocol; does it support optional?");
      (void)proto;
      (void)KPK_ObjectiveCBridgeable;

      return true;
    }

    case ForeignRepresentableKind::BridgedError:
      return true;

    case ForeignRepresentableKind::Object:
    case ForeignRepresentableKind::StaticBridged:
      llvm_unreachable("unexpected kind in ForeignRepresentableCacheEntry");
    }
  }
};

} // end namespace swift

#endif // SWIFT_FOREIGNREPRESENTATIONINFO_H
