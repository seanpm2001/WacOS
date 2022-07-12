//===--- TypeMatcher.h - Recursive Type Matcher -----------------*- C++ -*-===//
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
// This file defines TypeMatcher, which performs a structural match between
// two types, calling out differences for subclasses to process specifically.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_TYPE_MATCHER_H
#define SWIFT_AST_TYPE_MATCHER_H

#include "swift/AST/CanTypeVisitor.h"

namespace swift {

/// Recursively match the structure of two types, calling out those places
/// where the types are not structurally identical and allowing the
/// implementing subclass to take specific actions in response to such
/// mismatches.
///
/// Subclasses must implement the 'mismatch' method, which will be called when
/// a mismatch between the two given types is detected. The most general form
/// of 'mismatch', which all subclasses will likely implement as a catch-all,
/// takes two \c TypeBase pointers:
///
/// \code
/// bool mismatch(TypeBase *firstType, TypeBase *secondType,
///               Type sugaredFirstType);
/// /\endcode
///
/// \c mismatch will be called with the most specific types. Thus, a client that
/// wants to (say) bind type variables in the first type to types in the second
/// type could isolate that case with a 'mismatch' overload:
///
/// \code
/// bool mismatch(TypeVariableType *firstTypeVar, TypeBase *secondType,
///               Type sugaredFirstType);
/// \endcode
///
/// While having other mismatches fall to the original case. The 'mismatch'
/// function should return true to indicate that matching should continue,
/// or false to indicate that matching should exit early.
template<typename ImplClass>
class TypeMatcher {
  class MatchVisitor : public CanTypeVisitor<MatchVisitor, bool, Type, Type> {
    TypeMatcher &Matcher;

    /// Dispatch to the derived class when we've destructured the second type.
    template<typename FirstType, typename SecondType>
    bool mismatch(FirstType *firstType, SecondType *secondType,
                  Type sugaredFirstType) {
      return Matcher.asDerived().mismatch(firstType, secondType,
                                          sugaredFirstType);
    }

    /// Dispatch to the most-derived class when we have not yet destructured the
    /// second type.
    template<typename FirstType>
    bool mismatch(FirstType *firstType, Type secondType,
                  Type sugaredFirstType) {
      switch (secondType->getKind()) {
#define TYPE(CLASS, PARENT)                                         \
      case TypeKind::CLASS:                                         \
        return mismatch(firstType,                                  \
                        cast<CLASS##Type>(secondType.getPointer()), \
                        sugaredFirstType);
#include "swift/AST/TypeNodes.def"
      }

      llvm_unreachable("Unhandled TypeKind in switch.");
    }

    /// Honeypot to catch cases where we should redispatch the second type, but
    /// we have a stray ".getPointer()" in the dispatch call.
    template<typename FirstType>
    bool mismatch(FirstType *firstType, TypeBase *secondType,
                  Type sugaredFirstType) = delete;

  public:
    MatchVisitor(TypeMatcher &matcher) : Matcher(matcher) { }

#define TRIVIAL_CASE(TheType)                                              \
      bool visit##TheType(Can##TheType firstType, Type secondType,         \
                          Type sugaredFirstType) {                         \
        /* If the types match, continue. */                                \
        if (firstType->isEqual(secondType))                                \
          return true;                                                     \
                                                                           \
        /* Otherwise, let the derived class deal with the mismatch. */     \
        return mismatch(firstType.getPointer(), secondType,                \
                        sugaredFirstType);                                 \
      }

    TRIVIAL_CASE(ErrorType)
    TRIVIAL_CASE(BuiltinIntegerType)
    TRIVIAL_CASE(BuiltinFloatType)
    TRIVIAL_CASE(BuiltinRawPointerType)
    TRIVIAL_CASE(BuiltinNativeObjectType)
    TRIVIAL_CASE(BuiltinBridgeObjectType)
    TRIVIAL_CASE(BuiltinUnknownObjectType)
    TRIVIAL_CASE(BuiltinUnsafeValueBufferType)
    TRIVIAL_CASE(BuiltinVectorType)
    TRIVIAL_CASE(SILTokenType)

    bool visitTupleType(CanTupleType firstTuple, Type secondType,
                        Type sugaredFirstType) {
      if (auto secondTuple = secondType->getAs<TupleType>()) {
        auto sugaredFirstTuple = sugaredFirstType->getAs<TupleType>();
        if (firstTuple->getNumElements() != secondTuple->getNumElements())
          return mismatch(firstTuple.getPointer(), secondTuple,
                          sugaredFirstType);

        for (unsigned i = 0, n = firstTuple->getNumElements(); i != n; ++i) {
          const auto &firstElt = firstTuple->getElements()[i];
          const auto &secondElt = secondTuple->getElements()[i];

          if (firstElt.getName() != secondElt.getName() ||
              firstElt.isVararg() != secondElt.isVararg())
            return mismatch(firstTuple.getPointer(), secondTuple,
                            sugaredFirstType);

          // Recurse on the tuple elements.
          if (!this->visit(firstTuple.getElementType(i), secondElt.getType(),
                           sugaredFirstTuple->getElementType(i)))
            return false;
        }

        return true;
      }

      // Tuple/non-tuple mismatch.
      return mismatch(firstTuple.getPointer(), secondType, sugaredFirstType);
    }

    template<typename FirstReferenceStorageType>
    bool handleReferenceStorageType(
           CanTypeWrapper<FirstReferenceStorageType> firstStorage,
           Type secondType, Type sugaredFirstType) {
      if (auto secondStorage = secondType->getAs<FirstReferenceStorageType>()) {
        return this->visit(
                 firstStorage.getReferentType(),
                 secondStorage->getReferentType(),
                 sugaredFirstType->getAs<FirstReferenceStorageType>()
                   ->getReferentType());
      }

      return mismatch(firstStorage.getPointer(), secondType, sugaredFirstType);
    }

    bool visitUnownedStorageType(CanUnownedStorageType firstStorage,
                                 Type secondType, Type sugaredFirstType) {
      return handleReferenceStorageType(firstStorage, secondType,
                                        sugaredFirstType);
    }

    bool visitUnmanagedStorageType(CanUnmanagedStorageType firstStorage,
                                   Type secondType, Type sugaredFirstType) {
      return handleReferenceStorageType(firstStorage, secondType,
                                        sugaredFirstType);
    }

    bool visitWeakStorageType(CanWeakStorageType firstStorage,
                              Type secondType, Type sugaredFirstType) {
      return handleReferenceStorageType(firstStorage, secondType,
                                        sugaredFirstType);
    }

    template<typename FirstNominalType>
    bool handleNominalType(CanTypeWrapper<FirstNominalType> firstNominal,
                           Type secondType, Type sugaredFirstType) {
      if (auto secondNominal = secondType->getAs<FirstNominalType>()) {
        if (firstNominal->getDecl() != secondNominal->getDecl())
          return mismatch(firstNominal.getPointer(), secondNominal,
                          sugaredFirstType);

        if (firstNominal.getParent())
          return this->visit(firstNominal.getParent(),
                             secondNominal->getParent(),
                             sugaredFirstType->castTo<FirstNominalType>()
                               ->getParent());

        return true;
      }

      return mismatch(firstNominal.getPointer(), secondType, sugaredFirstType);
    }

    bool visitEnumType(CanEnumType firstEnum, Type secondType,
                       Type sugaredFirstType) {
      return handleNominalType(firstEnum, secondType, sugaredFirstType);
    }

    bool visitStructType(CanStructType firstStruct, Type secondType,
                         Type sugaredFirstType) {
      return handleNominalType(firstStruct, secondType, sugaredFirstType);
    }

    bool visitClassType(CanClassType firstClass, Type secondType,
                        Type sugaredFirstType) {
      return handleNominalType(firstClass, secondType, sugaredFirstType);
    }

    bool visitProtocolType(CanProtocolType firstProtocol, Type secondType,
                           Type sugaredFirstType) {
      return handleNominalType(firstProtocol, secondType, sugaredFirstType);
    }

    template<typename FirstMetatypeType>
    bool handleAnyMetatypeType(CanTypeWrapper<FirstMetatypeType> firstMeta,
                               Type secondType, Type sugaredFirstType) {
      if (auto secondMeta = secondType->getAs<FirstMetatypeType>()) {
        if (firstMeta->getKind() != secondMeta->getKind())
          return mismatch(firstMeta.getPointer(), secondMeta, sugaredFirstType);

        return this->visit(firstMeta.getInstanceType(),
                           secondMeta->getInstanceType(),
                           sugaredFirstType->castTo<FirstMetatypeType>()
                             ->getInstanceType());
      }

      return mismatch(firstMeta.getPointer(), secondType, sugaredFirstType);
    }

    bool visitMetatypeType(CanMetatypeType firstMeta, Type secondType,
                           Type sugaredFirstType) {
      return handleAnyMetatypeType(firstMeta, secondType, sugaredFirstType);
    }

    bool visitExistentialMetatypeType(CanExistentialMetatypeType firstMeta,
                                      Type secondType, Type sugaredFirstType) {
      return handleAnyMetatypeType(firstMeta, secondType, sugaredFirstType);
    }

    TRIVIAL_CASE(ModuleType)
    TRIVIAL_CASE(DynamicSelfType)
    TRIVIAL_CASE(ArchetypeType)
    TRIVIAL_CASE(GenericTypeParamType)
    TRIVIAL_CASE(DependentMemberType)

    /// FIXME: Split this out into cases?
    bool visitAnyFunctionType(CanAnyFunctionType firstFunc, Type secondType,
                              Type sugaredFirstType) {
      if (auto secondFunc = secondType->getAs<AnyFunctionType>()) {
        // FIXME: Compare throws()? Both existing subclasses would prefer
        // to mismatch on (!firstFunc->throws() && secondFunc->throws()), but
        // embedding that non-commutativity in this general matcher is icky.
        if (firstFunc->isNoEscape() != secondFunc->isNoEscape())
          return mismatch(firstFunc.getPointer(), secondFunc, sugaredFirstType);

        auto sugaredFirstFunc = sugaredFirstType->castTo<AnyFunctionType>();
        if (firstFunc->getParams().size() != secondFunc->getParams().size())
          return mismatch(firstFunc.getInput().getPointer(), secondFunc->getInput(),
                          sugaredFirstFunc->getInput());

        for (unsigned i = 0, n = firstFunc->getParams().size(); i != n; ++i) {
          const auto &firstElt = firstFunc->getParams()[i];
          const auto &secondElt = secondFunc->getParams()[i];

          if (firstElt.getLabel() != secondElt.getLabel() ||
              firstElt.isVariadic() != secondElt.isVariadic() ||
              firstElt.isInOut() != secondElt.isInOut())
            return mismatch(firstFunc.getInput().getPointer(),
                            secondFunc->getInput(),
                            sugaredFirstFunc->getInput());

          // Recurse on parameter components.
          if (!this->visit(firstElt.getType()->getCanonicalType(),
                           secondElt.getType(),
                           sugaredFirstFunc->getParams()[i].getType()))
            return false;
        }

        return this->visit(firstFunc.getInput(), secondFunc->getInput(),
                           sugaredFirstFunc->getInput()) &&
               this->visit(firstFunc.getResult(), secondFunc->getResult(),
                           sugaredFirstFunc->getResult());
      }

      return mismatch(firstFunc.getPointer(), secondType, sugaredFirstType);
    }

    TRIVIAL_CASE(SILFunctionType)
    TRIVIAL_CASE(SILBlockStorageType)
    TRIVIAL_CASE(SILBoxType)
    TRIVIAL_CASE(ProtocolCompositionType)

    bool visitLValueType(CanLValueType firstLValue, Type secondType,
                         Type sugaredFirstType) {
      if (auto secondLValue = secondType->getAs<LValueType>()) {
        return this->visit(firstLValue.getObjectType(),
                           secondLValue->getObjectType(),
                           sugaredFirstType->castTo<LValueType>()
                            ->getObjectType());
      }

      return mismatch(firstLValue.getPointer(), secondType, sugaredFirstType);
    }

    bool visitInOutType(CanInOutType firstInOut, Type secondType,
                        Type sugaredFirstType) {
      if (auto secondInOut = secondType->getAs<InOutType>()) {
        return this->visit(firstInOut.getObjectType(),
                           secondInOut->getObjectType(),
                           sugaredFirstType->castTo<InOutType>()
                             ->getObjectType());
      }

      return mismatch(firstInOut.getPointer(), secondType, sugaredFirstType);
    }

    bool visitUnboundBoundGenericType(CanUnboundGenericType firstUBGT,
                                      Type secondType, Type sugaredFirstType) {
      if (auto secondUBGT = secondType->getAs<UnboundGenericType>()) {
        if (firstUBGT->getDecl() != secondUBGT->getDecl())
          return mismatch(firstUBGT.getPointer(), secondUBGT, sugaredFirstType);

        if (firstUBGT.getParent())
          return this->visit(firstUBGT.getParent(), secondUBGT->getParent(),
                             sugaredFirstType->castTo<UnboundGenericType>()
                               ->getParent());

        return true;
      }

      return mismatch(firstUBGT.getPointer(), secondType, sugaredFirstType);
    }

    template<typename FirstBoundGenericType>
    bool handleBoundGenericType(CanTypeWrapper<FirstBoundGenericType> firstBGT,
                                Type secondType, Type sugaredFirstType) {
      if (auto secondBGT = secondType->getAs<FirstBoundGenericType>()) {
        if (firstBGT->getDecl() != secondBGT->getDecl())
          return mismatch(firstBGT.getPointer(), secondBGT, sugaredFirstType);

        auto sugaredFirstBGT
          = sugaredFirstType->castTo<FirstBoundGenericType>();
        if (firstBGT->getParent() &&
            !this->visit(firstBGT.getParent(), secondBGT->getParent(),
                         sugaredFirstBGT->getParent()))
          return false;

        for (unsigned i = 0, n = firstBGT->getGenericArgs().size();
             i != n; ++i) {
          if (!this->visit(firstBGT.getGenericArgs()[i],
                           secondBGT->getGenericArgs()[i],
                           sugaredFirstBGT->getGenericArgs()[i]))
            return false;
        }

        return true;
      }

      return mismatch(firstBGT.getPointer(), secondType, sugaredFirstType);
    }

    bool visitBoundGenericClassType(CanBoundGenericClassType firstBGT,
                                    Type secondType, Type sugaredFirstType) {
      return handleBoundGenericType(firstBGT, secondType, sugaredFirstType);
    }

    bool visitBoundGenericEnumType(CanBoundGenericEnumType firstBGT,
                                   Type secondType, Type sugaredFirstType) {
      return handleBoundGenericType(firstBGT, secondType, sugaredFirstType);
    }

    bool visitBoundGenericStructType(CanBoundGenericStructType firstBGT,
                                     Type secondType, Type sugaredFirstType) {
      return handleBoundGenericType(firstBGT, secondType, sugaredFirstType);
    }

    TRIVIAL_CASE(TypeVariableType)

#undef TRIVIAL_CASE
  };

  ImplClass &asDerived() { return static_cast<ImplClass &>(*this); }

  const ImplClass &asDerived() const {
    return static_cast<const ImplClass &>(*this);
  }

public:
  bool match(Type first, Type second) {
    return MatchVisitor(*this).visit(first->getCanonicalType(), second,
                                     first);
  }
};

} // end namespace swift

#endif // SWIFT_AST_TYPE_MATCHER_H
