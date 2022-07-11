//===--- DerivedConformanceActor.cpp - Derived Actor Conformance ----------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//  This file implements implicit derivation of the Actor protocol.
//
//===----------------------------------------------------------------------===//

#include "CodeSynthesis.h"
#include "DerivedConformances.h"
#include "TypeChecker.h"
#include "TypeCheckConcurrency.h"
#include "TypeCheckDistributed.h"
#include "swift/AST/NameLookupRequests.h"
#include "swift/AST/ParameterList.h"

using namespace swift;

bool DerivedConformance::canDeriveDistributedActor(
    NominalTypeDecl *nominal, DeclContext *dc) {
  auto classDecl = dyn_cast<ClassDecl>(nominal);
  return classDecl && classDecl->isDistributedActor() && dc == nominal;
}

/******************************************************************************/
/******************************* RESOLVE FUNCTION *****************************/
/******************************************************************************/

/// Synthesizes the
///
/// \verbatim
/// static resolve(_ address: ActorAddress,
///                using transport: ActorTransport) throws -> Self {
///   <filled in by SILGenDistributed>
/// }
/// \endverbatim
///
/// factory function in the AST, with an empty body. Its body is
/// expected to be filled-in during SILGen.
// TODO(distributed): move this synthesis to DerivedConformance style
static FuncDecl *deriveDistributedActor_resolve(DerivedConformance &derived) {
  auto decl = dyn_cast<ClassDecl>(derived.Nominal);
  assert(decl->isDistributedActor());
  auto &C = decl->getASTContext();

  auto mkParam = [&](Identifier argName, Identifier paramName, Type ty) -> ParamDecl* {
    auto *param = new (C) ParamDecl(SourceLoc(),
                                    SourceLoc(), argName,
                                    SourceLoc(), paramName, decl);
    param->setImplicit();
    param->setSpecifier(ParamSpecifier::Default);
    param->setInterfaceType(ty);
    return param;
  };

  auto addressType = getDistributedActorIdentityType(decl);
  auto transportType = getDistributedActorTransportType(decl);

  // (_ identity: Identity, using transport: ActorTransport)
  auto *params = ParameterList::create(
      C,
      /*LParenLoc=*/SourceLoc(),
      /*params=*/{  mkParam(Identifier(), C.Id_identity, addressType),
                  mkParam(C.Id_using, C.Id_transport, transportType)
      },
      /*RParenLoc=*/SourceLoc()
  );

  // Func name: resolve(_:using:)
  DeclName name(C, C.Id_resolve, params);

  // Expected type: (Self) -> (Identity, ActorTransport) throws -> (Self)
  auto *factoryDecl =
      FuncDecl::createImplicit(C, StaticSpellingKind::KeywordStatic,
                               name, SourceLoc(),
                               /*async=*/false,
                               /*throws=*/true,
                               /*genericParams=*/nullptr,
                               params,
                               /*returnType*/decl->getDeclaredInterfaceType(),
                               decl);

  factoryDecl->setDistributedActorFactory(); // TODO(distributed): should we mark this specifically as the resolve factory?
  factoryDecl->copyFormalAccessFrom(decl, /*sourceIsParentContext=*/true);

  derived.addMembersToConformanceContext({factoryDecl});
  return factoryDecl;
}

/******************************************************************************/
/******************************* PROPERTIES ***********************************/
/******************************************************************************/

static ValueDecl *deriveDistributedActor_id(DerivedConformance &derived) {
  assert(derived.Nominal->isDistributedActor());
  auto &C = derived.Context;

  // ```
  // nonisolated
  // let id: Identity
  // ```
  auto propertyType = getDistributedActorIdentityType(derived.Nominal);

  VarDecl *propDecl;
  PatternBindingDecl *pbDecl;
  std::tie(propDecl, pbDecl) = derived.declareDerivedProperty(
      C.Id_id,
      propertyType, propertyType,
      /*isStatic=*/false, /*isFinal=*/true);

  propDecl->setIntroducer(VarDecl::Introducer::Let);

  // mark as nonisolated, allowing access to it from everywhere
  propDecl->getAttrs().add(
      new (C) NonisolatedAttr(/*IsImplicit=*/true));

  derived.addMembersToConformanceContext({ propDecl, pbDecl });
  return propDecl;
}

static ValueDecl *deriveDistributedActor_actorTransport(
    DerivedConformance &derived) {
  assert(derived.Nominal->isDistributedActor());
  auto &C = derived.Context;

  // ```
  // nonisolated
  // let actorTransport: Transport
  // ```
  // (no need for @actorIndependent because it is an immutable let)
  auto propertyType = getDistributedActorTransportType(derived.Nominal);

  VarDecl *propDecl;
  PatternBindingDecl *pbDecl;
  std::tie(propDecl, pbDecl) = derived.declareDerivedProperty(
      C.Id_actorTransport,
      propertyType, propertyType,
      /*isStatic=*/false, /*isFinal=*/true);

  propDecl->setIntroducer(VarDecl::Introducer::Let);

  // mark as nonisolated, allowing access to it from everywhere
  propDecl->getAttrs().add(
      new (C) NonisolatedAttr(/*IsImplicit=*/true));

  derived.addMembersToConformanceContext({ propDecl, pbDecl });
  return propDecl;
}

static Type deriveDistributedActor_Transport(
    DerivedConformance &derived) {
  assert(derived.Nominal->isDistributedActor());
  auto &C = derived.Context;

  // Look for a type DefaultActorTransport within the parent context.
  auto defaultTransportLookup = TypeChecker::lookupUnqualified(
      derived.getConformanceContext()->getModuleScopeContext(),
      DeclNameRef(C.Id_DefaultActorTransport),
      derived.ConformanceDecl->getLoc());
  TypeDecl *defaultTransportTypeDecl = nullptr;
  for (const auto &found : defaultTransportLookup) {
    if (auto foundType = dyn_cast_or_null<TypeDecl>(found.getValueDecl())) {
      if (defaultTransportTypeDecl) {
        // Note: ambiguity, for now just fail.
        return nullptr;
      }

      defaultTransportTypeDecl = foundType;
      continue;
    }
  }

  // There is no default, so fail to synthesize.
  if (!defaultTransportTypeDecl)
    return nullptr;

  // Return the default transport type.
  return defaultTransportTypeDecl->getDeclaredInterfaceType();
}
// ==== ------------------------------------------------------------------------

ValueDecl *DerivedConformance::deriveDistributedActor(ValueDecl *requirement) {
  if (auto var = dyn_cast<VarDecl>(requirement)) {
    if (var->getName() == Context.Id_id)
      return deriveDistributedActor_id(*this);

    if (var->getName() == Context.Id_actorTransport)
      return deriveDistributedActor_actorTransport(*this);
  }

  if (auto func = dyn_cast<FuncDecl>(requirement)) {
    // just a simple name check is enough here,
    // if we are invoked here we know for sure it is for the "right" function
    if (func->getName().getBaseName() == Context.Id_resolve)
      return deriveDistributedActor_resolve(*this);
  }

  return nullptr;
}

std::pair<Type, TypeDecl *> DerivedConformance::deriveDistributedActor(
    AssociatedTypeDecl *assocType) {
  if (!canDeriveDistributedActor(Nominal, cast<DeclContext>(ConformanceDecl)))
    return std::make_pair(Type(), nullptr);

  if (assocType->getName() == Context.Id_Transport) {
    return std::make_pair(deriveDistributedActor_Transport(*this), nullptr);
  }

  Context.Diags.diagnose(assocType->getLoc(),
                         diag::broken_distributed_actor_requirement);
  return std::make_pair(Type(), nullptr);
}
