//===--- ModuleLoader.h - Module Loader Interface ---------------*- C++ -*-===//
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
// This file implements an abstract interface for loading modules.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_MODULE_LOADER_H
#define SWIFT_AST_MODULE_LOADER_H

#include "swift/AST/Identifier.h"
#include "swift/Basic/LLVM.h"
#include "swift/Basic/SourceLoc.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/TinyPtrVector.h"

namespace swift {

class AbstractFunctionDecl;
class ClassDecl;
class ModuleDecl;
class NominalTypeDecl;

enum class KnownProtocolKind : uint8_t;

/// Records dependencies on files outside of the current module.
class DependencyTracker {
  llvm::SetVector<std::string, std::vector<std::string>,
                  llvm::SmallSet<std::string, 16>> paths;

public:
  /// Adds a file as a dependency.
  ///
  /// The contents of \p file are taken literally, and should be appropriate
  /// for appearing in a list of dependencies suitable for tooling like Make.
  /// No path canonicalization is done.
  void addDependency(StringRef file) {
    paths.insert(file);
  }

  /// Fetches the list of dependencies.
  ArrayRef<std::string> getDependencies() const {
    if (paths.empty())
      return None;
    assert((&paths[0]) + (paths.size() - 1) == &paths.back() &&
           "elements not stored contiguously");
    return llvm::makeArrayRef(&paths[0], paths.size());
  }
};

/// \brief Abstract interface that loads named modules into the AST.
class ModuleLoader {
  DependencyTracker * const dependencyTracker;
  virtual void anchor();

protected:
  ModuleLoader(DependencyTracker *tracker) : dependencyTracker(tracker) {}

  void addDependency(StringRef file) {
    if (dependencyTracker)
      dependencyTracker->addDependency(file);
  }

public:
  virtual ~ModuleLoader() = default;

  /// \brief Import a module with the given module path.
  ///
  /// \param importLoc The location of the 'import' keyword.
  ///
  /// \param path A sequence of (identifier, location) pairs that denote
  /// the dotted module name to load, e.g., AppKit.NSWindow.
  ///
  /// \returns the module referenced, if it could be loaded. Otherwise,
  /// emits a diagnostic and returns NULL.
  virtual
  ModuleDecl *loadModule(SourceLoc importLoc,
                         ArrayRef<std::pair<Identifier, SourceLoc>> path) = 0;

  /// \brief Load extensions to the given nominal type.
  ///
  /// \param nominal The nominal type whose extensions should be loaded.
  ///
  /// \param previousGeneration The previous generation number. The AST already
  /// contains extensions loaded from any generation up to and including this
  /// one.
  virtual void loadExtensions(NominalTypeDecl *nominal,
                              unsigned previousGeneration) { }

  /// \brief Load the methods within the given class that produce
  /// Objective-C class or instance methods with the given selector.
  ///
  /// \param classDecl The class in which we are searching for @objc methods.
  /// The search only considers this class and its extensions; not any
  /// superclasses.
  ///
  /// \param selector The selector to search for.
  ///
  /// \param isInstanceMethod Whether we are looking for an instance method
  /// (vs. a class method).
  ///
  /// \param previousGeneration The previous generation with which this
  /// callback was invoked. The list of methods will already contain all of
  /// the results from generations up and including \c previousGeneration.
  ///
  /// \param methods The list of @objc methods in this class that have this
  /// selector and are instance/class methods as requested. This list will be
  /// extended with any methods found in subsequent generations.
  virtual void loadObjCMethods(
                 ClassDecl *classDecl,
                 ObjCSelector selector,
                 bool isInstanceMethod,
                 unsigned previousGeneration,
                 llvm::TinyPtrVector<AbstractFunctionDecl *> &methods) = 0;

  /// \brief Verify all modules loaded by this loader.
  virtual void verifyAllModules() { }
};

} // namespace swift

#endif
