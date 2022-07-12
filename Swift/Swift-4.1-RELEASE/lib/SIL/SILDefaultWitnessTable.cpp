//===--- SILDefaultWitnessTable.cpp ---------------------------------------===//
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
// This file defines the SILDefaultWitnessTable class, which is used to provide
// default implementations of protocol requirements for resilient protocols,
// allowing IRGen to generate the appropriate metadata so that the runtime can
// insert those requirements to witness tables that were emitted prior to the
// requirement being added.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/ASTMangler.h"
#include "swift/SIL/SILDefaultWitnessTable.h"
#include "swift/SIL/SILModule.h"
#include "llvm/ADT/SmallString.h"

using namespace swift;

void SILDefaultWitnessTable::addDefaultWitnessTable() {
  // Make sure we have not seen this witness table yet.
  assert(Mod.DefaultWitnessTableMap.find(Protocol) ==
         Mod.DefaultWitnessTableMap.end() && "Attempting to create duplicate "
         "default witness table.");
  Mod.DefaultWitnessTableMap[Protocol] = this;
  Mod.defaultWitnessTables.push_back(this);
}

SILDefaultWitnessTable *
SILDefaultWitnessTable::create(SILModule &M, SILLinkage Linkage,
                               const ProtocolDecl *Protocol,
                               ArrayRef<SILDefaultWitnessTable::Entry> entries){
  // Allocate the witness table and initialize it.
  auto *buf = M.allocate<SILDefaultWitnessTable>(1);
  SILDefaultWitnessTable *wt =
      ::new (buf) SILDefaultWitnessTable(M, Linkage, Protocol, entries);

  wt->addDefaultWitnessTable();

  // Return the resulting default witness table.
  return wt;
}

SILDefaultWitnessTable *
SILDefaultWitnessTable::create(SILModule &M, SILLinkage Linkage,
                               const ProtocolDecl *Protocol) {
  // Allocate the witness table and initialize it.
  auto *buf = M.allocate<SILDefaultWitnessTable>(1);
  SILDefaultWitnessTable *wt =
      ::new (buf) SILDefaultWitnessTable(M, Linkage, Protocol);

  wt->addDefaultWitnessTable();

  // Return the resulting default witness table.
  return wt;
}

SILDefaultWitnessTable::
SILDefaultWitnessTable(SILModule &M,
                       SILLinkage Linkage,
                       const ProtocolDecl *Protocol,
                       ArrayRef<Entry> entries)
  : Mod(M), Linkage(Linkage), Protocol(Protocol), Entries(),
    IsDeclaration(true) {

  convertToDefinition(entries);
}

SILDefaultWitnessTable::SILDefaultWitnessTable(SILModule &M,
                                               SILLinkage Linkage,
                                               const ProtocolDecl *Protocol)
  : Mod(M), Linkage(Linkage), Protocol(Protocol), Entries(),
    IsDeclaration(true) {}

void SILDefaultWitnessTable::
convertToDefinition(ArrayRef<Entry> entries) {
  assert(IsDeclaration);
  IsDeclaration = false;

  Entries = Mod.allocateCopy(entries);

  // Bump the reference count of witness functions referenced by this table.
  for (auto entry : getEntries()) {
    if (entry.isValid()) {
      entry.getWitness()->incrementRefCount();
    }
  }
}

Identifier SILDefaultWitnessTable::getIdentifier() const {
  Mangle::ASTMangler Mangler;
  std::string name = Mangler.mangleTypeAsUSR(getProtocol()->getDeclaredType());
  return Mod.getASTContext().getIdentifier(name);
}

unsigned SILDefaultWitnessTable::getMinimumWitnessTableSize() const {
  unsigned defaultEntries = 0;
  unsigned minimumEntries = 0;

  // Count the number of entries up to and including the last null entry.
  // This is the number of witnesses that all conforming types must
  // provide.
  //
  // Any witnesses after the last null entry all have defaults, and can
  // be omitted from conformances; these are the resilient defaults.
  //
  // FIXME: Really this should look at availability instead.
  for (auto entry : Entries) {
    if (entry.isValid()) {
      defaultEntries++;
    } else {
      minimumEntries++;
      minimumEntries += defaultEntries;
      defaultEntries = 0;
    }
  }

  return minimumEntries;
}

SILDefaultWitnessTable::~SILDefaultWitnessTable() {
  // Drop the reference count of witness functions referenced by this table.
  for (auto entry : getEntries()) {
    if (entry.isValid()) {
      entry.getWitness()->decrementRefCount();
    }
  }
}
