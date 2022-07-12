//===--- TBD.cpp -- generates and validates TBD files ---------------------===//
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

#include "TBD.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Decl.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/DiagnosticsFrontend.h"
#include "swift/AST/Module.h"
#include "swift/Basic/LLVM.h"
#include "swift/Demangling/Demangle.h"
#include "swift/Frontend/FrontendOptions.h"
#include "swift/TBDGen/TBDGen.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/FileSystem.h"
#include <vector>

using namespace swift;

static std::vector<StringRef> sortSymbols(llvm::StringSet<> &symbols) {
  std::vector<StringRef> sorted;
  for (auto &symbol : symbols)
    sorted.push_back(symbol.getKey());
  std::sort(sorted.begin(), sorted.end());
  return sorted;
}

bool swift::writeTBD(ModuleDecl *M, bool hasMultipleIRGenThreads,
                     StringRef OutputFilename,
                     StringRef installName) {
  std::error_code EC;
  llvm::raw_fd_ostream OS(OutputFilename, EC, llvm::sys::fs::F_None);
  if (EC) {
    M->getASTContext().Diags.diagnose(SourceLoc(), diag::error_opening_output,
                                      OutputFilename, EC.message());
    return true;
  }

  writeTBDFile(M, OS, hasMultipleIRGenThreads, installName);

  return false;
}

bool swift::inputFileKindCanHaveTBDValidated(InputFileKind kind) {
  // Only things that involve an AST can have a TBD file computed, at the
  // moment.
  switch (kind) {
  case InputFileKind::IFK_Swift:
  case InputFileKind::IFK_Swift_Library:
    return true;
  case InputFileKind::IFK_None:
  case InputFileKind::IFK_Swift_REPL:
  case InputFileKind::IFK_SIL:
  case InputFileKind::IFK_LLVM_IR:
    return false;
  }
}

static bool validateSymbolSet(DiagnosticEngine &diags,
                              llvm::StringSet<> symbols, llvm::Module &IRModule,
                              bool diagnoseExtraSymbolsInTBD) {
  auto error = false;

  // Diff the two sets of symbols, flagging anything outside their intersection.

  // Delay the emission of errors for things in the IR but not TBD, so we can
  // sort them to get a stable order.
  std::vector<StringRef> irNotTBD;

  for (auto &nameValue : IRModule.getValueSymbolTable()) {
    auto name = nameValue.getKey();
    auto value = nameValue.getValue();
    if (auto GV = dyn_cast<llvm::GlobalValue>(value)) {
      // Is this a symbol that should be listed?
      auto externallyVisible =
          GV->hasExternalLinkage() && !GV->hasHiddenVisibility();
      if (!GV->isDeclaration() && externallyVisible) {
        // Is it listed?
        if (!symbols.erase(name))
          irNotTBD.push_back(name);
      }
    } else {
      assert(symbols.find(name) == symbols.end() &&
             "non-global value in value symbol table");
    }
  }

  std::sort(irNotTBD.begin(), irNotTBD.end());
  for (auto &name : irNotTBD) {
    diags.diagnose(SourceLoc(), diag::symbol_in_ir_not_in_tbd, name,
                   Demangle::demangleSymbolAsString(name));
    error = true;
  }

  if (diagnoseExtraSymbolsInTBD) {
    // Look for any extra symbols.
    for (auto &name : sortSymbols(symbols)) {
      diags.diagnose(SourceLoc(), diag::symbol_in_tbd_not_in_ir, name,
                     Demangle::demangleSymbolAsString(name));
      error = true;
    }
  }

  return error;
}

bool swift::validateTBD(ModuleDecl *M, llvm::Module &IRModule,
                        bool hasMultipleIRGenThreads,
                        bool diagnoseExtraSymbolsInTBD) {
  llvm::StringSet<> symbols;
  enumeratePublicSymbols(M, symbols, hasMultipleIRGenThreads);

  return validateSymbolSet(M->getASTContext().Diags, symbols, IRModule,
                           diagnoseExtraSymbolsInTBD);
}

bool swift::validateTBD(FileUnit *file, llvm::Module &IRModule,
                        bool hasMultipleIRGenThreads,
                        bool diagnoseExtraSymbolsInTBD) {
  llvm::StringSet<> symbols;
  enumeratePublicSymbols(file, symbols, hasMultipleIRGenThreads);

  return validateSymbolSet(file->getParentModule()->getASTContext().Diags,
                           symbols, IRModule, diagnoseExtraSymbolsInTBD);
}
