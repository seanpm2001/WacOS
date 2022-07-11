//===-- TypeCheckDistributed.h - Distributed actor typechecking -*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file provides type checking support for Swift's distributed actor model.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SEMA_TYPECHECKDISTRIBUTED_H
#define SWIFT_SEMA_TYPECHECKDISTRIBUTED_H

#include "swift/AST/ConcreteDeclRef.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/Type.h"

namespace swift {

class ClassDecl;
class ConstructorDecl;
class Decl;
class DeclContext;
class FuncDecl;
class NominalTypeDecl;

/******************************************************************************/
/********************* Distributed Actor Type Checking ************************/
/******************************************************************************/

// Diagnose an error if the _Distributed module is not loaded.
bool ensureDistributedModuleLoaded(Decl *decl);

/// Check for illegal property declarations (e.g. re-declaring transport or id)
void checkDistributedActorProperties(const ClassDecl *decl);

/// The local and resolve distributed actor constructors have special rules to check.
void checkDistributedActorConstructor(const ClassDecl *decl, ConstructorDecl *ctor);

bool checkDistributedFunction(FuncDecl *decl, bool diagnose);

/// Determine the distributed actor transport type for the given actor.
Type getDistributedActorTransportType(NominalTypeDecl *actor);

/// Determine the distributed actor identity type for the given actor.
Type getDistributedActorIdentityType(NominalTypeDecl *actor);

/// Diagnose a distributed func declaration in a not-distributed actor protocol.
void diagnoseDistributedFunctionInNonDistributedActorProtocol(
  const ProtocolDecl *proto, InFlightDiagnostic &diag);

/// Emit a FixIt suggesting to add Codable to the nominal type.
void addCodableFixIt(const NominalTypeDecl *nominal, InFlightDiagnostic &diag);

}


#endif /* SWIFT_SEMA_TYPECHECKDISTRIBUTED_H */
