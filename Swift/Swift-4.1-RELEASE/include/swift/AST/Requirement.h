//===--- Requirement.h - Swift Requirement ASTs -----------------*- C++ -*-===//
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
// This file defines the Requirement class and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_REQUIREMENT_H
#define SWIFT_AST_REQUIREMENT_H

#include "swift/AST/Type.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/Support/ErrorHandling.h"

namespace swift {

/// Describes the kind of a requirement that occurs within a requirements
/// clause.
enum class RequirementKind : unsigned {
  /// A conformance requirement T : P, where T is a type that depends
  /// on a generic parameter and P is a protocol to which T must conform.
  Conformance,
  /// A superclass requirement T : C, where T is a type that depends
  /// on a generic parameter and C is a concrete class type which T must
  /// equal or be a subclass of.
  Superclass,
  /// A same-type requirement T == U, where T and U are types that shall be
  /// equivalent.
  SameType,
  /// A layout bound T : L, where T is a type that depends on a generic
  /// parameter and L is some layout specification that should bound T.
  Layout,

  // Note: there is code that packs this enum in a 2-bit bitfield.  Audit users
  // when adding enumerators.
};

/// \brief A single requirement placed on the type parameters (or associated
/// types thereof) of a
class Requirement {
  llvm::PointerIntPair<Type, 3, RequirementKind> FirstTypeAndKind;
  /// The second element of the requirement. Its content is dependent
  /// on the requirement kind.
  /// The payload of the following enum should always match the kind!
  /// Any access to the fields of this enum should first check if the
  /// requested access matches the kind of the requirement.
  union {
    Type SecondType;
    LayoutConstraint SecondLayout;
  };

public:
  /// Create a conformance or same-type requirement.
  Requirement(RequirementKind kind, Type first, Type second)
    : FirstTypeAndKind(first, kind), SecondType(second) {
    assert(first);
    assert(second);
  }

  /// Create a layout constraint requirement.
  Requirement(RequirementKind kind, Type first, LayoutConstraint second)
    : FirstTypeAndKind(first, kind), SecondLayout(second) {
    assert(first);
    assert(second);
  }

  /// \brief Determine the kind of requirement.
  RequirementKind getKind() const { return FirstTypeAndKind.getInt(); }

  /// \brief Retrieve the first type.
  Type getFirstType() const {
    return FirstTypeAndKind.getPointer();
  }

  /// \brief Retrieve the second type.
  Type getSecondType() const {
    assert(getKind() != RequirementKind::Layout);
    return SecondType;
  }

  /// \brief Subst the types involved in this requirement.
  ///
  /// The \c args arguments are passed through to Type::subst. This doesn't
  /// touch the superclasses, protocols or layout constraints.
  template <typename... Args>
  Optional<Requirement> subst(Args &&... args) const {
    auto newFirst = getFirstType().subst(std::forward<Args>(args)...);
    if (!newFirst)
      return None;

    switch (getKind()) {
    case RequirementKind::Conformance:
    case RequirementKind::Superclass:
    case RequirementKind::SameType: {
      auto newSecond = getSecondType().subst(std::forward<Args>(args)...);
      if (!newSecond)
        return None;
      return Requirement(getKind(), newFirst, newSecond);
    }
    case RequirementKind::Layout:
      return Requirement(getKind(), newFirst, getLayoutConstraint());
    }

    llvm_unreachable("Unhandled RequirementKind in switch.");
  }

  /// \brief Retrieve the layout constraint.
  LayoutConstraint getLayoutConstraint() const {
    assert(getKind() == RequirementKind::Layout);
    return SecondLayout;
  }

  void dump() const;
  void dump(raw_ostream &out) const;
  void print(raw_ostream &os, const PrintOptions &opts) const;
  void print(ASTPrinter &printer, const PrintOptions &opts) const;
};

} // end namespace swift

#endif
