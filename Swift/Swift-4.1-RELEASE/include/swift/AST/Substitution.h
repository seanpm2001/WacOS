//===--- Substitution.h - Swift Generic Substitution ASTs -------*- C++ -*-===//
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
// This file defines the Substitution class.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_SUBSTITUTION_H
#define SWIFT_AST_SUBSTITUTION_H

#include "swift/AST/Type.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"

namespace llvm {
  class raw_ostream;
}

namespace swift {
  class GenericEnvironment;
  class SubstitutionMap;

/// Substitution - A substitution into a generic specialization.
class Substitution {
  Type Replacement;
  ArrayRef<ProtocolConformanceRef> Conformance;

public:
  /// The replacement type.
  Type getReplacement() const { return Replacement; }
  
  /// The protocol conformances for the replacement. These appear in the same
  /// order as Archetype->getConformsTo() for the substituted archetype.
  const ArrayRef<ProtocolConformanceRef> getConformances() const {
    return Conformance;
  }
  
  Substitution() {}
  
  Substitution(Type Replacement, ArrayRef<ProtocolConformanceRef> Conformance);

  /// Checks whether the current substitution is canonical.
  bool isCanonical() const;

  /// Get the canonicalized substitution. If wasCanonical is not nullptr,
  /// store there whether the current substitution was canonical already.
  Substitution getCanonicalSubstitution(bool *wasCanonical = nullptr) const;

  bool operator!=(const Substitution &other) const { return !(*this == other); }
  bool operator==(const Substitution &other) const;
  void print(llvm::raw_ostream &os,
             const PrintOptions &PO = PrintOptions()) const;
  void dump() const;
  void dump(llvm::raw_ostream &os, unsigned indent = 0) const;
};

} // end namespace swift

#endif
