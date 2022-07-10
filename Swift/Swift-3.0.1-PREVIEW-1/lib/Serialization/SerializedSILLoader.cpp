//===--- SerializedSILLoader.cpp - A loader for SIL sections --------------===//
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

#define DEBUG_TYPE "serialized-sil-loader"
#include "swift/Serialization/SerializedSILLoader.h"
#include "DeserializeSIL.h"
#include "swift/Serialization/ModuleFile.h"
#include "swift/Serialization/SerializedModuleLoader.h"
#include "swift/SIL/SILModule.h"
#include "llvm/Support/Debug.h"

using namespace swift;

SerializedSILLoader::SerializedSILLoader(ASTContext &Ctx,
                                         SILModule *SILMod,
                                         Callback *callback) {

  // Get a list of SerializedModules from ASTContext.
  // FIXME: Iterating over LoadedModules is not a good way to do this.
  for (auto &Entry : Ctx.LoadedModules) {
    for (auto File : Entry.second->getFiles()) {
      if (auto LoadedAST = dyn_cast<SerializedASTFile>(File)) {
        auto Des = new SILDeserializer(&LoadedAST->File, *SILMod, callback);
#ifndef NDEBUG
        SILMod->verify();
#endif
        LoadedSILSections.emplace_back(Des);
      }
    }
  }
}

SerializedSILLoader::~SerializedSILLoader() {}

SILFunction *SerializedSILLoader::lookupSILFunction(SILFunction *Callee) {
  // It is possible that one module has a declaration of a SILFunction, while
  // another has the full definition.
  SILFunction *retVal = nullptr;
  for (auto &Des : LoadedSILSections) {
    if (auto Func = Des->lookupSILFunction(Callee)) {
      DEBUG(llvm::dbgs() << "Deserialized " << Func->getName() << " from "
            << Des->getModuleIdentifier().str() << "\n");
      if (!Func->empty())
        return Func;
      retVal = Func;
    }
  }
  return retVal;
}

SILFunction *SerializedSILLoader::lookupSILFunction(SILDeclRef Decl) {
  llvm::SmallString<32> Name;
  Decl.mangle(Name);
  // It is possible that one module has a declaration of a SILFunction, while
  // another has the full definition.
  SILFunction *retVal = nullptr;
  for (auto &Des : LoadedSILSections) {
    if (auto Func = Des->lookupSILFunction(Name)) {
      DEBUG(llvm::dbgs() << "Deserialized " << Func->getName() << " from "
            << Des->getModuleIdentifier().str() << "\n");
      if (!Func->empty())
        return Func;
      retVal = Func;
    }
  }
  return retVal;
}

SILFunction *SerializedSILLoader::lookupSILFunction(StringRef Name,
                                                    bool declarationOnly,
                                                    SILLinkage Linkage) {
  // It is possible that one module has a declaration of a SILFunction, while
  // another has the full definition.
  SILFunction *retVal = nullptr;
  for (auto &Des : LoadedSILSections) {
    if (auto Func = Des->lookupSILFunction(Name, declarationOnly)) {
      DEBUG(llvm::dbgs() << "Deserialized " << Func->getName() << " from "
            << Des->getModuleIdentifier().str() << "\n");
      if (Linkage != SILLinkage::Private) {
        // This is not the linkage we are looking for.
        if (Func->getLinkage() != Linkage) {
          DEBUG(llvm::dbgs()
                << "Wrong linkage for Function: " << Func->getName() << " : "
                << (int)Func->getLinkage() << "\n");
          Des->invalidateFunction(Func);
          Func->getModule().eraseFunction(Func);
          continue;
        }
      }
      if (!Func->empty() || declarationOnly)
        return Func;
      retVal = Func;
    }
  }
  return retVal;
}

bool SerializedSILLoader::hasSILFunction(StringRef Name, SILLinkage Linkage) {
  // It is possible that one module has a declaration of a SILFunction, while
  // another has the full definition.
  SILFunction *retVal = nullptr;
  for (auto &Des : LoadedSILSections) {
    if (Des->hasSILFunction(Name, Linkage))
      return true;
  }
  return retVal;
}


SILVTable *SerializedSILLoader::lookupVTable(Identifier Name) {
  for (auto &Des : LoadedSILSections) {
    if (auto VT = Des->lookupVTable(Name))
      return VT;
  }
  return nullptr;
}

SILWitnessTable *SerializedSILLoader::lookupWitnessTable(SILWitnessTable *WT) {
  for (auto &Des : LoadedSILSections)
    if (auto wT = Des->lookupWitnessTable(WT))
      return wT;
  return nullptr;
}

SILDefaultWitnessTable *SerializedSILLoader::
lookupDefaultWitnessTable(SILDefaultWitnessTable *WT) {
  for (auto &Des : LoadedSILSections)
    if (auto wT = Des->lookupDefaultWitnessTable(WT))
      return wT;
  return nullptr;
}

void SerializedSILLoader::invalidateCaches() {
  for (auto &Des : LoadedSILSections)
    Des->invalidateFunctionCache();
}

bool SerializedSILLoader::invalidateFunction(SILFunction *F) {
  for (auto &Des : LoadedSILSections)
    if (Des->invalidateFunction(F))
      return true;
  return false;
}

void SerializedSILLoader::getAll() {
  for (auto &Des : LoadedSILSections)
    Des->getAll();
}

// FIXME: Not the best interface. We know exactly which FileUnits may have SIL
// those in the main module.
void SerializedSILLoader::getAllForModule(Identifier Mod,
                                          FileUnit *PrimaryFile) {
  for (auto &Des : LoadedSILSections) {
    if (Des->getModuleIdentifier() == Mod) {
      Des->getAll(PrimaryFile ?
                  Des->getFile() != PrimaryFile : false);
    }
  }
}

void SerializedSILLoader::getAllSILFunctions() {
  for (auto &Des : LoadedSILSections)
    Des->getAllSILFunctions();
}

/// Deserialize all VTables in all SILModules.
void SerializedSILLoader::getAllVTables() {
  for (auto &Des : LoadedSILSections)
    Des->getAllVTables();
}

/// Deserialize all WitnessTables in all SILModules.
void SerializedSILLoader::getAllWitnessTables() {
  for (auto &Des : LoadedSILSections)
    Des->getAllWitnessTables();
}

/// Deserialize all DefaultWitnessTables in all SILModules.
void SerializedSILLoader::getAllDefaultWitnessTables() {
  for (auto &Des : LoadedSILSections)
    Des->getAllDefaultWitnessTables();
}

// Anchor the SerializedSILLoader v-table.
void SerializedSILLoader::Callback::_anchor() {}
