//===--- NameBinding.cpp - Name Binding -----------------------------------===//
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
//
//  This file implements name binding for Swift.
//
//===----------------------------------------------------------------------===//

#include "swift/Subsystems.h"
#include "swift/AST/NameLookup.h"
#include "swift/AST/AST.h"
#include "swift/AST/DiagnosticsSema.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/ModuleLoader.h"
#include "swift/ClangImporter/ClangModule.h"
#include "clang/Basic/Module.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/Path.h"
#include <algorithm>
#include <system_error>
using namespace swift;

//===----------------------------------------------------------------------===//
// NameBinder
//===----------------------------------------------------------------------===//

using ImportedModule = Module::ImportedModule;
using ImportOptions = SourceFile::ImportOptions;

namespace {  
  class NameBinder {    
  public:
    SourceFile &SF;
    ASTContext &Context;

    NameBinder(SourceFile &SF) : SF(SF), Context(SF.getASTContext()) {}

    template<typename ...ArgTypes>
    InFlightDiagnostic diagnose(ArgTypes &&...Args) {
      return Context.Diags.diagnose(std::forward<ArgTypes>(Args)...);
    }
    
    void addImport(
        SmallVectorImpl<std::pair<ImportedModule, ImportOptions>> &imports,
        ImportDecl *ID);

    /// Load a module referenced by an import statement.
    ///
    /// Returns null if no module can be loaded.
    Module *getModule(ArrayRef<std::pair<Identifier,SourceLoc>> ModuleID);
  };
}

Module *
NameBinder::getModule(ArrayRef<std::pair<Identifier, SourceLoc>> modulePath) {
  assert(!modulePath.empty());
  auto moduleID = modulePath[0];
  
  // The Builtin module cannot be explicitly imported unless we're a .sil file
  // or in the REPL.
  if ((SF.Kind == SourceFileKind::SIL || SF.Kind == SourceFileKind::REPL) &&
      moduleID.first == Context.TheBuiltinModule->getName())
    return Context.TheBuiltinModule;

  // If the imported module name is the same as the current module,
  // skip the Swift module loader and use the Clang module loader instead.
  // This allows a Swift module to extend a Clang module of the same name.
  //
  // FIXME: We'd like to only use this in SIL mode, but unfortunately we use it
  // for our fake overlays as well.
  if (moduleID.first == SF.getParentModule()->getName() &&
      modulePath.size() == 1) {
    if (auto importer = Context.getClangModuleLoader())
      return importer->loadModule(moduleID.second, modulePath);
    return nullptr;
  }
  
  return Context.getModule(modulePath);
}

/// Returns true if a decl with the given \p actual kind can legally be
/// imported via the given \p expected kind.
static bool isCompatibleImportKind(ImportKind expected, ImportKind actual) {
  if (expected == actual)
    return true;
  if (expected != ImportKind::Type)
    return false;

  switch (actual) {
  case ImportKind::Module:
    llvm_unreachable("module imports do not bring in decls");
  case ImportKind::Type:
    llvm_unreachable("individual decls cannot have abstract import kind");
  case ImportKind::Struct:
  case ImportKind::Class:
  case ImportKind::Enum:
    return true;
  case ImportKind::Protocol:
  case ImportKind::Var:
  case ImportKind::Func:
    return false;
  }
}

static const char *getImportKindString(ImportKind kind) {
  switch (kind) {
  case ImportKind::Module:
    llvm_unreachable("module imports do not bring in decls");
  case ImportKind::Type:
    return "typealias";
  case ImportKind::Struct:
    return "struct";
  case ImportKind::Class:
    return "class";
  case ImportKind::Enum:
    return "enum";
  case ImportKind::Protocol:
    return "protocol";
  case ImportKind::Var:
    return "var";
  case ImportKind::Func:
    return "func";
  }
}

static bool shouldImportSelfImportClang(const ImportDecl *ID,
                                        const SourceFile &SF) {
  // FIXME: We use '@_exported' for fake overlays in testing.
  if (ID->isExported())
    return true;
  if (SF.Kind == SourceFileKind::SIL)
    return true;
  return false;
}

void NameBinder::addImport(
    SmallVectorImpl<std::pair<ImportedModule, ImportOptions>> &imports,
    ImportDecl *ID) {
  if (ID->getModulePath().front().first == SF.getParentModule()->getName() &&
      ID->getModulePath().size() == 1 && !shouldImportSelfImportClang(ID, SF)) {
    // If the imported module name is the same as the current module,
    // produce a diagnostic.
    StringRef filename = llvm::sys::path::filename(SF.getFilename());
    if (filename.empty())
      Context.Diags.diagnose(ID, diag::sema_import_current_module,
                             ID->getModulePath().front().first);
    else
      Context.Diags.diagnose(ID, diag::sema_import_current_module_with_file,
                             filename, ID->getModulePath().front().first);
    ID->setModule(SF.getParentModule());
    return;
  }

  Module *M = getModule(ID->getModulePath());
  if (!M) {
    SmallString<64> modulePathStr;
    interleave(ID->getModulePath(),
               [&](ImportDecl::AccessPathElement elem) {
                 modulePathStr += elem.first.str();
               },
               [&] { modulePathStr += "."; });

    auto diagKind = diag::sema_no_import;
    if (SF.Kind == SourceFileKind::REPL || Context.LangOpts.DebuggerSupport)
      diagKind = diag::sema_no_import_repl;
    diagnose(ID->getLoc(), diagKind, modulePathStr);

    if (Context.SearchPathOpts.SDKPath.empty() &&
        llvm::Triple(llvm::sys::getProcessTriple()).isMacOSX()) {
      diagnose(SourceLoc(), diag::sema_no_import_no_sdk);
      diagnose(SourceLoc(), diag::sema_no_import_no_sdk_xcrun);
    }
    return;
  }

  ID->setModule(M);

  Module *topLevelModule;
  if (ID->getModulePath().size() == 1) {
    topLevelModule = M;
  } else {
    // If we imported a submodule, import the top-level module as well.
    Identifier topLevelName = ID->getModulePath().front().first;
    topLevelModule = Context.getLoadedModule(topLevelName);
    assert(topLevelModule && "top-level module missing");
  }

  auto *testableAttr = ID->getAttrs().getAttribute<TestableAttr>();
  if (testableAttr && !topLevelModule->isTestingEnabled() &&
      Context.LangOpts.EnableTestableAttrRequiresTestableModule) {
    diagnose(ID->getModulePath().front().second, diag::module_not_testable,
             topLevelModule->getName());
    testableAttr->setInvalid();
  }

  ImportOptions options;
  if (ID->isExported())
    options |= SourceFile::ImportFlags::Exported;
  if (testableAttr)
    options |= SourceFile::ImportFlags::Testable;
  imports.push_back({ { ID->getDeclPath(), M }, options });

  if (topLevelModule != M)
    imports.push_back({ { ID->getDeclPath(), topLevelModule }, options });

  if (ID->getImportKind() != ImportKind::Module) {
    // If we're importing a specific decl, validate the import kind.
    using namespace namelookup;
    auto declPath = ID->getDeclPath();

    // FIXME: Doesn't handle scoped testable imports correctly.
    assert(declPath.size() == 1 && "can't handle sub-decl imports");
    SmallVector<ValueDecl *, 8> decls;
    lookupInModule(topLevelModule, declPath, declPath.front().first, decls,
                   NLKind::QualifiedLookup, ResolutionKind::Overloadable,
                   /*resolver*/nullptr, &SF);

    if (decls.empty()) {
      diagnose(ID, diag::no_decl_in_module)
        .highlight(SourceRange(declPath.front().second,
                               declPath.back().second));
      return;
    }

    ID->setDecls(Context.AllocateCopy(decls));

    Optional<ImportKind> actualKind = ImportDecl::findBestImportKind(decls);
    if (!actualKind.hasValue()) {
      // FIXME: print entire module name?
      diagnose(ID, diag::ambiguous_decl_in_module,
               declPath.front().first, M->getName());
      for (auto next : decls)
        diagnose(next, diag::found_candidate);

    } else if (!isCompatibleImportKind(ID->getImportKind(), *actualKind)) {
      diagnose(ID, diag::imported_decl_is_wrong_kind,
               declPath.front().first,
               getImportKindString(ID->getImportKind()),
               static_cast<unsigned>(*actualKind))
        .fixItReplace(SourceRange(ID->getKindLoc()),
                      getImportKindString(*actualKind));

      if (decls.size() == 1)
        diagnose(decls.front(), diag::decl_declared_here,
                 decls.front()->getFullName());
    }
  }
}

//===----------------------------------------------------------------------===//
// performNameBinding
//===----------------------------------------------------------------------===//

template<typename OP_DECL>
static void insertOperatorDecl(NameBinder &Binder,
                               SourceFile::OperatorMap<OP_DECL*> &Operators,
                               OP_DECL *OpDecl) {
  auto previousDecl = Operators.find(OpDecl->getName());
  if (previousDecl != Operators.end()) {
    Binder.diagnose(OpDecl->getLoc(), diag::operator_redeclared);
    Binder.diagnose(previousDecl->second.getPointer(),
                    diag::previous_operator_decl);
    return;
  }

  // FIXME: The second argument indicates whether the given operator is visible
  // outside the current file.
  Operators[OpDecl->getName()] = { OpDecl, true };
}

static void insertPrecedenceGroupDecl(NameBinder &binder, SourceFile &SF,
                                      PrecedenceGroupDecl *group) {
  auto previousDecl = SF.PrecedenceGroups.find(group->getName());
  if (previousDecl != SF.PrecedenceGroups.end()) {
    binder.diagnose(group->getLoc(), diag::precedence_group_redeclared);
    binder.diagnose(previousDecl->second.getPointer(),
                    diag::previous_precedence_group_decl);
    return;
  }

  // FIXME: The second argument indicates whether the given precedence
  // group is visible outside the current file.
  SF.PrecedenceGroups[group->getName()] = { group, true };  
}

/// performNameBinding - Once parsing is complete, this walks the AST to
/// resolve names and do other top-level validation.
///
/// At this parsing has been performed, but we still have UnresolvedDeclRefExpr
/// nodes for unresolved value names, and we may have unresolved type names as
/// well.  This handles import directives and forward references.
void swift::performNameBinding(SourceFile &SF, unsigned StartElem) {
  // Make sure we skip adding the standard library imports if the
  // source file is empty.
  if (SF.ASTStage == SourceFile::NameBound || SF.Decls.empty()) {
    SF.ASTStage = SourceFile::NameBound;
    return;
  }

  // Reset the name lookup cache so we find new decls.
  // FIXME: This is inefficient.
  SF.clearLookupCache();

  NameBinder Binder(SF);

  SmallVector<std::pair<ImportedModule, ImportOptions>, 8> ImportedModules;

  // Do a prepass over the declarations to find and load the imported modules
  // and map operator decls.
  for (auto D : llvm::makeArrayRef(SF.Decls).slice(StartElem)) {
    if (ImportDecl *ID = dyn_cast<ImportDecl>(D)) {
      Binder.addImport(ImportedModules, ID);
    } else if (auto *OD = dyn_cast<PrefixOperatorDecl>(D)) {
      insertOperatorDecl(Binder, SF.PrefixOperators, OD);
    } else if (auto *OD = dyn_cast<PostfixOperatorDecl>(D)) {
      insertOperatorDecl(Binder, SF.PostfixOperators, OD);
    } else if (auto *OD = dyn_cast<InfixOperatorDecl>(D)) {
      insertOperatorDecl(Binder, SF.InfixOperators, OD);
    } else if (auto *PGD = dyn_cast<PrecedenceGroupDecl>(D)) {
      insertPrecedenceGroupDecl(Binder, SF, PGD);
    }
  }

  SF.addImports(ImportedModules);

  // FIXME: This algorithm has quadratic memory usage.  (In practice,
  // import statements after the first "chunk" should be rare, though.)
  // FIXME: Can we make this more efficient?

  SF.ASTStage = SourceFile::NameBound;
  verify(SF);
}

