//===--- ClangImporter.h - Import Clang Modules -----------------*- C++ -*-===//
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
// This file implements support for loading Clang modules into Swift.
//
//===----------------------------------------------------------------------===//
#ifndef SWIFT_CLANG_IMPORTER_H
#define SWIFT_CLANG_IMPORTER_H

#include "swift/AST/ClangModuleLoader.h"

/// The maximum number of SIMD vector elements we currently try to import.
#define SWIFT_MAX_IMPORTED_SIMD_ELEMENTS 4

namespace llvm {
  class Triple;
  template<typename Fn> class function_ref;
}

namespace clang {
  class ASTContext;
  class CodeGenOptions;
  class Decl;
  class EnumConstantDecl;
  class EnumDecl;
  class MacroInfo;
  class Module;
  class NamedDecl;
  class Sema;
  class TargetInfo;
  class VisibleDeclConsumer;
}

namespace swift {

class ASTContext;
class ClangImporterOptions;
class ClangModuleUnit;
class ClangNode;
class Decl;
class DeclContext;
class ImportDecl;
class IRGenOptions;
class LazyResolver;
class ModuleDecl;
class NominalTypeDecl;
class VisibleDeclConsumer;
enum class SelectorSplitKind;

/// \brief Class that imports Clang modules into Swift, mapping directly
/// from Clang ASTs over to Swift ASTs.
class ClangImporter final : public ClangModuleLoader {
  friend class ClangModuleUnit;

public:
  class Implementation;

private:
  Implementation &Impl;

  ClangImporter(ASTContext &ctx, const ClangImporterOptions &clangImporterOpts,
                DependencyTracker *tracker);

public:
  /// \brief Create a new Clang importer that can import a suitable Clang
  /// module into the given ASTContext.
  ///
  /// \param ctx The ASTContext into which the module will be imported.
  /// The ASTContext's SearchPathOptions will be used for the Clang importer.
  ///
  /// \param clangImporterOpts The options to use for the Clang importer.
  ///
  /// \param tracker The object tracking files this compilation depends on.
  ///
  /// \returns a new Clang module importer, or null (with a diagnostic) if
  /// an error occurred.
  static std::unique_ptr<ClangImporter>
  create(ASTContext &ctx, const ClangImporterOptions &clangImporterOpts,
         DependencyTracker *tracker = nullptr);

  ClangImporter(const ClangImporter &) = delete;
  ClangImporter(ClangImporter &&) = delete;
  ClangImporter &operator=(const ClangImporter &) = delete;
  ClangImporter &operator=(ClangImporter &&) = delete;

  ~ClangImporter();

  /// \brief Import a module with the given module path.
  ///
  /// Clang modules will be imported using the Objective-C ARC dialect,
  /// with all warnings disabled.
  ///
  /// \param importLoc The location of the 'import' keyword.
  ///
  /// \param path A sequence of (identifier, location) pairs that denote
  /// the dotted module name to load, e.g., AppKit.NSWindow.
  ///
  /// \returns the module referenced, if it could be loaded. Otherwise,
  /// emits a diagnostic and returns NULL.
  virtual ModuleDecl *loadModule(
                        SourceLoc importLoc,
                        ArrayRef<std::pair<Identifier, SourceLoc>> path)
                      override;

  /// \brief Look for declarations associated with the given name.
  ///
  /// \param name The name we're searching for.
  void lookupValue(DeclName name, VisibleDeclConsumer &consumer);

  /// Look for textually included declarations from the bridging header.
  ///
  /// \param filter returns true if the given clang decl/macro should be
  /// imported and fed to the consumer
  /// \param receiver will be fed decls as they are found and imported.
  ///
  /// \c receiver is not a VisibleDeclConsumer so that it is not limited to
  /// accepting ValueDecls only.
  void lookupBridgingHeaderDecls(llvm::function_ref<bool(ClangNode)> filter,
                                llvm::function_ref<void(Decl*)> receiver) const;

  /// Look for declarations from a particular header. The header may be part of
  /// a clang module or included from the bridging header.
  ///
  /// \param filename path to the header
  /// \param filter returns true if the given clang decl/macro should be
  /// imported and fed to the consumer
  /// \param receiver will be fed decls as they are found and imported.
  ///
  /// \c receiver is not a VisibleDeclConsumer so that it is not limited to
  /// accepting ValueDecls only.
  ///
  /// \returns true if there was a problem, e.g. the file does not exist.
  bool lookupDeclsFromHeader(StringRef filename,
                             llvm::function_ref<bool(ClangNode)> filter,
                             llvm::function_ref<void(Decl*)> receiver) const;

  /// \brief Load extensions to the given nominal type.
  ///
  /// \param nominal The nominal type whose extensions should be loaded.
  ///
  /// \param previousGeneration The previous generation number. The AST already
  /// contains extensions loaded from any generation up to and including this
  /// one.
  virtual void loadExtensions(NominalTypeDecl *nominal,
                              unsigned previousGeneration) override;

  virtual void loadObjCMethods(
                 ClassDecl *classDecl,
                 ObjCSelector selector,
                 bool isInstanceMethod,
                 unsigned previousGeneration,
                 llvm::TinyPtrVector<AbstractFunctionDecl *> &methods) override;

  /// Adds a new search path to the Clang CompilerInstance, as if specified with
  /// -I or -F.
  ///
  /// \returns true if there was an error adding the search path.
  bool addSearchPath(StringRef newSearchPath, bool isFramework) override;

  /// Imports an Objective-C header file into the shared imported header module.
  ///
  /// \param header A header name or full path, to be used in a \#import
  /// directive.
  /// \param adapter The module that depends on the contents of this header.
  /// \param expectedSize The size of the header when the module was compiled
  ///        against it.
  /// \param expectedModTime The mtime of the header when the module was
  ///        compiled against it.
  /// \param cachedContents A buffer to use if the header has been modified
  ///        since the module was compiled.
  /// \param diagLoc A location to attach any diagnostics to if import fails.
  ///
  /// \returns true if there was an error importing the header.
  ///
  /// \sa getImportedHeaderModule
  bool importHeader(StringRef header, ModuleDecl *adapter, off_t expectedSize,
                    time_t expectedModTime, StringRef cachedContents,
                    SourceLoc diagLoc);

  /// Imports an Objective-C header file into the shared imported header module.
  ///
  /// \param header A header name or full path, to be used in a \#import
  ///        directive.
  /// \param adapter The module that depends on the contents of this header.
  /// \param diagLoc A location to attach any diagnostics to if import fails.
  /// \param trackParsedSymbols If true, tracks decls and macros that were
  ///        parsed from the bridging header.
  ///
  /// \returns true if there was an error importing the header.
  ///
  /// \sa getImportedHeaderModule
  bool importBridgingHeader(StringRef header, ModuleDecl *adapter,
                            SourceLoc diagLoc = {},
                            bool trackParsedSymbols = false);

  /// Returns the module that contains imports and declarations from all loaded
  /// Objective-C header files.
  ///
  /// \sa importHeader
  ModuleDecl *getImportedHeaderModule() const override;

  std::string getBridgingHeaderContents(StringRef headerPath, off_t &fileSize,
                                        time_t &fileModTime);

  const clang::Module *getClangOwningModule(ClangNode Node) const;
  bool hasTypedef(const clang::Decl *typeDecl) const;

  void verifyAllModules() override;

  void setTypeResolver(LazyResolver &resolver);
  void clearTypeResolver();

  clang::TargetInfo &getTargetInfo() const;
  clang::ASTContext &getClangASTContext() const override;
  clang::Preprocessor &getClangPreprocessor() const override;
  clang::Sema &getClangSema() const override;
  clang::CodeGenOptions &getClangCodeGenOpts() const;

  std::string getClangModuleHash() const;

  /// If we already imported a given decl, return the corresponding Swift decl.
  /// Otherwise, return nullptr.
  Decl *importDeclCached(const clang::NamedDecl *ClangDecl);

  // Returns true if it is expected that the macro is ignored.
  bool shouldIgnoreMacro(StringRef Name, const clang::MacroInfo *Macro);

  /// Returns the name of the given enum element as it would be imported into
  /// Swift.
  ///
  /// The return value may be an empty identifier, in which case the enum would
  /// not be imported.
  ///
  /// This is mostly an implementation detail of the importer, but is also
  /// used by the debugger.
  Identifier getEnumConstantName(const clang::EnumConstantDecl *enumConstant);

  /// Writes the mangled name of \p clangDecl to \p os.
  void getMangledName(raw_ostream &os, const clang::NamedDecl *clangDecl) const;

  using ClangModuleLoader::addDependency;
  
  // Print statistics from the Clang AST reader.
  void printStatistics() const override;

  /// Dump Swift lookup tables.
  void dumpSwiftLookupTables();
  
  /// Given the path of a Clang module, collect the names of all its submodules.
  /// Calling this function does not load the module.
  void collectSubModuleNames(
      ArrayRef<std::pair<Identifier, SourceLoc>> path,
      std::vector<std::string> &names);

  /// Given a Clang module, decide whether this module is imported already.
  static bool isModuleImported(const clang::Module *M);
};

ImportDecl *createImportDecl(ASTContext &Ctx, DeclContext *DC, ClangNode ClangN,
                             ArrayRef<clang::Module *> Exported);
}

#endif
