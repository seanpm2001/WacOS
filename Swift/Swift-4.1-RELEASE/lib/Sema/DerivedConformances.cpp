//===--- DerivedConformances.cpp - Derived conformance utilities ----------===//
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

#include "TypeChecker.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Stmt.h"
#include "swift/AST/Expr.h"
#include "swift/AST/Pattern.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/Types.h"
#include "swift/ClangImporter/ClangModule.h"
#include "DerivedConformances.h"

using namespace swift;
using namespace DerivedConformance;

bool DerivedConformance::derivesProtocolConformance(TypeChecker &tc,
                                                    NominalTypeDecl *nominal,
                                                    ProtocolDecl *protocol) {
  // Only known protocols can be derived.
  auto knownProtocol = protocol->getKnownProtocolKind();
  if (!knownProtocol)
    return false;

  if (auto *enumDecl = dyn_cast<EnumDecl>(nominal)) {
    switch (*knownProtocol) {
        // The presence of a raw type is an explicit declaration that
        // the compiler should derive a RawRepresentable conformance.
      case KnownProtocolKind::RawRepresentable:
        return enumDecl->hasRawType();

        // Enums without associated values can implicitly derive Equatable and
        // Hashable conformance.
      case KnownProtocolKind::Equatable:
        return canDeriveEquatable(tc, enumDecl, protocol);
      case KnownProtocolKind::Hashable:
        return canDeriveHashable(tc, enumDecl, protocol);

        // @objc enums can explicitly derive their _BridgedNSError conformance.
      case KnownProtocolKind::BridgedNSError:
        return enumDecl->isObjC() && enumDecl->hasCases()
            && enumDecl->hasOnlyCasesWithoutAssociatedValues();

        // Enums without associated values and enums with a raw type of String
        // or Int can explicitly derive CodingKey conformance.
      case KnownProtocolKind::CodingKey: {
        Type rawType = enumDecl->getRawType();
        if (rawType) {
          auto parentDC = enumDecl->getDeclContext();
          ASTContext &C = parentDC->getASTContext();

          auto nominal = rawType->getAnyNominal();
          return nominal == C.getStringDecl() || nominal == C.getIntDecl();
        }

        // hasOnlyCasesWithoutAssociatedValues will return true for empty enums;
        // empty enumas are allowed to conform as well.
        return enumDecl->hasOnlyCasesWithoutAssociatedValues();
      }

      default:
        return false;
    }
  } else if (isa<StructDecl>(nominal) || isa<ClassDecl>(nominal)) {
    // Structs and classes can explicitly derive Encodable and Decodable
    // conformance (explicitly meaning we can synthesize an implementation if
    // a type conforms manually).
    if (*knownProtocol == KnownProtocolKind::Encodable ||
        *knownProtocol == KnownProtocolKind::Decodable) {
      // FIXME: This is not actually correct. We cannot promise to always
      // provide a witness here for all structs and classes. Unfortunately,
      // figuring out whether this is actually possible requires much more
      // context -- a TypeChecker and the parent decl context at least -- and is
      // tightly coupled to the logic within DerivedConformance.
      // This unfortunately means that we expect a witness even if one will not
      // be produced, which requires DerivedConformance::deriveCodable to output
      // its own diagnostics.
      return true;
    }

    // Structs can explicitly derive Equatable and Hashable conformance.
    if (auto structDecl = dyn_cast<StructDecl>(nominal)) {
      switch (*knownProtocol) {
        case KnownProtocolKind::Equatable:
          return canDeriveEquatable(tc, structDecl, protocol);
        case KnownProtocolKind::Hashable:
          return canDeriveHashable(tc, structDecl, protocol);
        default:
          return false;
      }
    }
  }
  return false;
}

ValueDecl *DerivedConformance::getDerivableRequirement(TypeChecker &tc,
                                                       NominalTypeDecl *nominal,
                                                       ValueDecl *requirement) {
  // Note: whenever you update this function, also update
  // TypeChecker::deriveProtocolRequirement.
  ASTContext &ctx = nominal->getASTContext();
  auto name = requirement->getFullName();

  // Local function that retrieves the requirement with the same name as
  // the provided requirement, but within the given known protocol.
  auto getRequirement = [&](KnownProtocolKind kind) -> ValueDecl * {
    // Dig out the protocol.
    auto proto = ctx.getProtocol(kind);
    if (!proto) return nullptr;

    // Check whether this nominal type derives conformances to the protocol.
    if (!derivesProtocolConformance(tc, nominal, proto)) return nullptr;

    // Retrieve the requirement.
    auto results = proto->lookupDirect(name);
    return results.empty() ? nullptr : results.front();
  };

  // Properties.
  if (isa<VarDecl>(requirement)) {
    // RawRepresentable.rawValue
    if (name.isSimpleName(ctx.Id_rawValue))
      return getRequirement(KnownProtocolKind::RawRepresentable);

    // Hashable.hashValue
    if (name.isSimpleName(ctx.Id_hashValue))
      return getRequirement(KnownProtocolKind::Hashable);

    // _BridgedNSError._nsErrorDomain
    if (name.isSimpleName(ctx.Id_nsErrorDomain))
      return getRequirement(KnownProtocolKind::BridgedNSError);

    // CodingKey.stringValue
    if (name.isSimpleName(ctx.Id_stringValue))
      return getRequirement(KnownProtocolKind::CodingKey);

    // CodingKey.intValue
    if (name.isSimpleName(ctx.Id_intValue))
      return getRequirement(KnownProtocolKind::CodingKey);

    return nullptr;
  }

  // Functions.
  if (auto func = dyn_cast<FuncDecl>(requirement)) {
    if (func->isOperator() && name.getBaseName() == "==")
      return getRequirement(KnownProtocolKind::Equatable);

    // Encodable.encode(to: Encoder)
    if (name.isCompoundName() && name.getBaseName() == ctx.Id_encode) {
      auto argumentNames = name.getArgumentNames();
      if (argumentNames.size() == 1 && argumentNames[0] == ctx.Id_to)
        return getRequirement(KnownProtocolKind::Encodable);
    }

    return nullptr;
  }

  // Initializers.
  if (auto ctor = dyn_cast<ConstructorDecl>(requirement)) {
    auto argumentNames = name.getArgumentNames();
    if (argumentNames.size() == 1) {
      if (argumentNames[0] == ctx.Id_rawValue)
        return getRequirement(KnownProtocolKind::RawRepresentable);

      // CodingKey.init?(stringValue:), CodingKey.init?(intValue:)
      if (ctor->getFailability() == OTK_Optional &&
          (argumentNames[0] == ctx.Id_stringValue ||
           argumentNames[0] == ctx.Id_intValue))
        return getRequirement(KnownProtocolKind::CodingKey);

      // Decodable.init(from: Decoder)
      if (argumentNames[0] == ctx.Id_from)
        return getRequirement(KnownProtocolKind::Decodable);
    }

    return nullptr;
  }

  // Associated types.
  if (isa<AssociatedTypeDecl>(requirement)) {
    // RawRepresentable.RawValue
    if (name.isSimpleName(ctx.Id_RawValue))
      return getRequirement(KnownProtocolKind::RawRepresentable);

    return nullptr;
  }

  return nullptr;
}

DeclRefExpr *
DerivedConformance::createSelfDeclRef(AbstractFunctionDecl *fn) {
  ASTContext &C = fn->getASTContext();

  auto selfDecl = fn->getImplicitSelfDecl();
  return new (C) DeclRefExpr(selfDecl, DeclNameLoc(), /*implicit*/true);
}

FuncDecl *DerivedConformance::declareDerivedPropertyGetter(TypeChecker &tc,
                                                 Decl *parentDecl,
                                                 NominalTypeDecl *typeDecl,
                                                 Type propertyInterfaceType,
                                                 Type propertyContextType,
                                                 bool isStatic,
                                                 bool isFinal) {
  auto &C = tc.Context;
  auto parentDC = cast<DeclContext>(parentDecl);
  auto selfDecl = ParamDecl::createSelf(SourceLoc(), parentDC, isStatic);
  ParameterList *params[] = {
    ParameterList::createWithoutLoc(selfDecl),
    ParameterList::createEmpty(C)
  };
  
  FuncDecl *getterDecl =
    FuncDecl::create(C, /*StaticLoc=*/SourceLoc(), StaticSpellingKind::None,
                     /*FuncLoc=*/SourceLoc(), DeclName(), /*NameLoc=*/SourceLoc(),
                     /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                     /*AccessorKeywordLoc=*/SourceLoc(),
                     nullptr, params,
                     TypeLoc::withoutLoc(propertyInterfaceType), parentDC);
  getterDecl->setImplicit();
  getterDecl->setStatic(isStatic);

  // If this is supposed to be a final method, mark it as such.
  assert(isFinal || !parentDC->getAsClassOrClassExtensionContext());
  if (isFinal && parentDC->getAsClassOrClassExtensionContext() &&
      !getterDecl->isFinal())
    getterDecl->getAttrs().add(new (C) FinalAttr(/*IsImplicit=*/true));

  // Compute the interface type of the getter.
  Type interfaceType = FunctionType::get(TupleType::getEmpty(C),
                                         propertyInterfaceType);
  auto selfParam = computeSelfParam(getterDecl);
  if (auto sig = parentDC->getGenericSignatureOfContext()) {
    getterDecl->setGenericEnvironment(
        parentDC->getGenericEnvironmentOfContext());
    interfaceType = GenericFunctionType::get(sig, {selfParam},
                                             interfaceType,
                                             FunctionType::ExtInfo());
  } else
    interfaceType = FunctionType::get({selfParam}, interfaceType,
                                      FunctionType::ExtInfo());
  getterDecl->setInterfaceType(interfaceType);
  getterDecl->copyFormalAccessAndVersionedAttrFrom(typeDecl);

  // If the enum was not imported, the derived conformance is either from the
  // enum itself or an extension, in which case we will emit the declaration
  // normally.
  if (isa<ClangModuleUnit>(parentDC->getModuleScopeContext()))
    tc.Context.addExternalDecl(getterDecl);

  return getterDecl;
}

std::pair<VarDecl *, PatternBindingDecl *>
DerivedConformance::declareDerivedReadOnlyProperty(TypeChecker &tc,
                                                   Decl *parentDecl,
                                                   NominalTypeDecl *typeDecl,
                                                   Identifier name,
                                                   Type propertyInterfaceType,
                                                   Type propertyContextType,
                                                   FuncDecl *getterDecl,
                                                   bool isStatic,
                                                   bool isFinal) {
  auto &C = tc.Context;
  auto parentDC = cast<DeclContext>(parentDecl);

  VarDecl *propDecl = new (C) VarDecl(/*IsStatic*/isStatic, VarDecl::Specifier::Var,
                                      /*IsCaptureList*/false, SourceLoc(), name,
                                      propertyContextType, parentDC);
  propDecl->setImplicit();
  propDecl->makeComputed(SourceLoc(), getterDecl, nullptr, nullptr,
                         SourceLoc());
  propDecl->copyFormalAccessAndVersionedAttrFrom(typeDecl);
  propDecl->setInterfaceType(propertyInterfaceType);

  // If this is supposed to be a final property, mark it as such.
  assert(isFinal || !parentDC->getAsClassOrClassExtensionContext());
  if (isFinal && parentDC->getAsClassOrClassExtensionContext() &&
      !propDecl->isFinal())
    propDecl->getAttrs().add(new (C) FinalAttr(/*IsImplicit=*/true));

  Pattern *propPat = new (C) NamedPattern(propDecl, /*implicit*/ true);
  propPat->setType(propertyContextType);
  propPat = new (C) TypedPattern(propPat,
                                 TypeLoc::withoutLoc(propertyContextType),
                                 /*implicit*/ true);

  auto pbDecl = PatternBindingDecl::create(C, SourceLoc(),
                                           StaticSpellingKind::None,
                                           SourceLoc(), propPat, nullptr,
                                           parentDC);
  pbDecl->setImplicit();

  return {propDecl, pbDecl};
}
