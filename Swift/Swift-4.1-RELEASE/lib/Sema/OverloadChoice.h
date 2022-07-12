//===--- OverloadChoice.h - A Choice from an Overload Set  ------*- C++ -*-===//
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
// This file provides the \c OverloadChoice class and its related types,
// which is used by the constraint-based type checker to describe the
// selection of a particular overload from a set.
//
//===----------------------------------------------------------------------===//
#ifndef SWIFT_SEMA_OVERLOADCHOICE_H
#define SWIFT_SEMA_OVERLOADCHOICE_H

#include "llvm/ADT/PointerIntPair.h"
#include "llvm/Support/ErrorHandling.h"
#include "swift/AST/Availability.h"
#include "swift/AST/FunctionRefKind.h"
#include "swift/AST/Types.h"

namespace swift {

class ValueDecl;

namespace constraints {
  class ConstraintSystem;

/// \brief The kind of overload choice.
enum class OverloadChoiceKind : int {
  /// \brief The overload choice selects a particular declaration from a
  /// set of declarations.
  Decl,
  /// \brief The overload choice selects a particular declaration that was
  /// found via dynamic lookup and, therefore, might not actually be
  /// available at runtime.
  DeclViaDynamic,
  /// \brief The overload choice equates the member type with the
  /// base type. Used for unresolved member expressions like ".none" that
  /// refer to enum members with unit type.
  BaseType,
  /// \brief The overload choice selects a key path subscripting operation.
  KeyPathApplication,
  /// \brief The overload choice selects a particular declaration that
  /// was found by bridging the base value type to its Objective-C
  /// class type.
  DeclViaBridge,
  /// \brief The overload choice selects a particular declaration that
  /// was found by unwrapping an optional context type.
  DeclViaUnwrappedOptional,
  /// \brief The overload choice indexes into a tuple. Index zero will
  /// have the value of this enumerator, index one will have the value of this
  /// enumerator + 1, and so on. Thus, this enumerator must always be last.
  TupleIndex,
};

/// \brief Describes a particular choice within an overload set.
///
class OverloadChoice {
  enum : unsigned {
    /// Indicates that this is a normal "Decl" kind, or isn't a decl.
    IsDecl = 0x00,
    /// Indicates that this declaration was bridged, turning a
    /// "Decl" kind into "DeclViaBridge" kind.
    IsDeclViaBridge = 0x01,
    /// Indicates that this declaration was resolved by unwrapping an
    /// optional context type, turning a "Decl" kind into
    /// "DeclViaUnwrappedOptional".
    IsDeclViaUnwrappedOptional = 0x02,
    /// Indicates that this declaration was dynamic, turning a
    /// "Decl" kind into "DeclViaDynamic" kind.
    IsDeclViaDynamic = 0x03
  };

  /// \brief The base type to be used when referencing the declaration
  /// along with the two bits above.
  llvm::PointerIntPair<Type, 3, unsigned> BaseAndDeclKind;

  /// We mash together OverloadChoiceKind with tuple indices into a single
  /// integer representation.
  typedef llvm::PointerEmbeddedInt<uint32_t, 29>
    OverloadChoiceKindWithTupleIndex;
  
  /// \brief Either the declaration pointer or the overload choice kind.  The
  /// second case is represented as an OverloadChoiceKind, but has additional
  /// values at the top end that represent the tuple index.
  llvm::PointerUnion<ValueDecl*, OverloadChoiceKindWithTupleIndex> DeclOrKind;

  /// The kind of function reference.
  /// FIXME: This needs two bits. Can we pack them somewhere?
  FunctionRefKind TheFunctionRefKind;

public:
  OverloadChoice()
    : BaseAndDeclKind(nullptr, 0), DeclOrKind(0),
      TheFunctionRefKind(FunctionRefKind::Unapplied) {}

  OverloadChoice(Type base, ValueDecl *value,
                 FunctionRefKind functionRefKind)
    : BaseAndDeclKind(base, 0),
      TheFunctionRefKind(functionRefKind) {
    assert(!base || !base->hasTypeParameter());
    assert((reinterpret_cast<uintptr_t>(value) & (uintptr_t)0x03) == 0 &&
           "Badly aligned decl");
    
    DeclOrKind = value;
  }

  OverloadChoice(Type base, OverloadChoiceKind kind)
      : BaseAndDeclKind(base, 0), DeclOrKind(uint32_t(kind)),
        TheFunctionRefKind(FunctionRefKind::Unapplied) {
    assert(base && "Must have a base type for overload choice");
    assert(!base->hasTypeParameter());
    assert(kind != OverloadChoiceKind::Decl &&
           kind != OverloadChoiceKind::DeclViaDynamic &&
           kind != OverloadChoiceKind::DeclViaBridge &&
           kind != OverloadChoiceKind::DeclViaUnwrappedOptional &&
           "wrong constructor for decl");
  }

  OverloadChoice(Type base, unsigned index)
      : BaseAndDeclKind(base, 0),
        DeclOrKind(uint32_t(OverloadChoiceKind::TupleIndex)+index),
        TheFunctionRefKind(FunctionRefKind::Unapplied) {
    assert(base->getRValueType()->is<TupleType>() && "Must have tuple type");
  }

  bool isInvalid() const {
    return BaseAndDeclKind.getPointer().isNull() &&
           BaseAndDeclKind.getInt() == 0 &&
           DeclOrKind.isNull() &&
           TheFunctionRefKind == FunctionRefKind::Unapplied;
  }

  /// Retrieve an overload choice for a declaration that was found via
  /// dynamic lookup.
  static OverloadChoice getDeclViaDynamic(Type base, ValueDecl *value,
                                          FunctionRefKind functionRefKind) {
    OverloadChoice result;
    result.BaseAndDeclKind.setPointer(base);
    result.BaseAndDeclKind.setInt(IsDeclViaDynamic);
    result.DeclOrKind = value;
    result.TheFunctionRefKind = functionRefKind;
    return result;
  }

  /// Retrieve an overload choice for a declaration that was found via
  /// bridging to an Objective-C class.
  static OverloadChoice getDeclViaBridge(Type base, ValueDecl *value,
                                         FunctionRefKind functionRefKind) {
    OverloadChoice result;
    result.BaseAndDeclKind.setPointer(base);
    result.BaseAndDeclKind.setInt(IsDeclViaBridge);
    result.DeclOrKind = value;
    result.TheFunctionRefKind = functionRefKind;
    return result;
  }

  /// Retrieve an overload choice for a declaration that was found
  /// by unwrapping an optional context type.
  static OverloadChoice
  getDeclViaUnwrappedOptional(Type base, ValueDecl *value,
                              FunctionRefKind functionRefKind) {
    OverloadChoice result;
    result.BaseAndDeclKind.setPointer(base);
    result.BaseAndDeclKind.setInt(IsDeclViaUnwrappedOptional);
    result.DeclOrKind = value;
    result.TheFunctionRefKind = functionRefKind;
    return result;
  }

  /// \brief Retrieve the base type used to refer to the declaration.
  Type getBaseType() const {
    return BaseAndDeclKind.getPointer();
  }
  
  /// \brief Determines the kind of overload choice this is.
  OverloadChoiceKind getKind() const {
    if (DeclOrKind.is<ValueDecl*>()) {
      switch (BaseAndDeclKind.getInt()) {
      case IsDeclViaBridge: return OverloadChoiceKind::DeclViaBridge;
      case IsDeclViaDynamic: return OverloadChoiceKind::DeclViaDynamic;
      case IsDeclViaUnwrappedOptional:
        return OverloadChoiceKind::DeclViaUnwrappedOptional;
      default: return OverloadChoiceKind::Decl;
      }
    }

    uint32_t kind = DeclOrKind.get<OverloadChoiceKindWithTupleIndex>();
    if (kind >= (uint32_t)OverloadChoiceKind::TupleIndex)
      return OverloadChoiceKind::TupleIndex;

    return (OverloadChoiceKind)kind;
  }

  /// Determine whether this choice is for a declaration.
  bool isDecl() const {
    return DeclOrKind.is<ValueDecl*>();
  }

  /// \brief Retrieve the declaration that corresponds to this overload choice.
  ValueDecl *getDecl() const {
    return DeclOrKind.get<ValueDecl*>();
  }
  
  /// Get the name of the overload choice.
  DeclName getName() const;

  /// \brief Retrieve the tuple index that corresponds to this overload
  /// choice.
  unsigned getTupleIndex() const {
    assert(getKind() == OverloadChoiceKind::TupleIndex);
    uint32_t kind = DeclOrKind.get<OverloadChoiceKindWithTupleIndex>();
    return kind-(uint32_t)OverloadChoiceKind::TupleIndex;
  }

  /// \brief Retrieves an opaque choice that ignores the base type.
  void *getOpaqueChoiceSimple() const {
    return DeclOrKind.getOpaqueValue();
  }

  FunctionRefKind getFunctionRefKind() const {
    assert(isDecl() && "only makes sense for declaration choices");
    return TheFunctionRefKind;
  }
};

} // end namespace constraints
} // end namespace swift

#endif // LLVM_SWIFT_SEMA_OVERLOADCHOICE_H
