//===--- ClassHierarchyAnalysis.h - Analysis of Class Hierarchy -*- C++ -*-===//
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

#ifndef SWIFT_SILOPTIMIZER_ANALYSIS_CLASSHIERARCHY_H
#define SWIFT_SILOPTIMIZER_ANALYSIS_CLASSHIERARCHY_H

#include "swift/SILOptimizer/Analysis/Analysis.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/SILValue.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Debug.h"

namespace swift {

class SILModule;
class ClassDecl;
class ClassHierarchyAnalysis : public SILAnalysis {
public:
  typedef SmallVector<ClassDecl *, 8> ClassList;
  typedef SmallPtrSet<ClassDecl *, 32> ClassSet;
  typedef SmallVector<NominalTypeDecl *, 8> NominalTypeList;
  typedef llvm::DenseMap<ProtocolDecl *, NominalTypeList>
      ProtocolImplementations;

  ClassHierarchyAnalysis(SILModule *Mod)
      : SILAnalysis(AnalysisKind::ClassHierarchy), M(Mod) {
      init(); 
    }

  ~ClassHierarchyAnalysis();

  static bool classof(const SILAnalysis *S) {
    return S->getKind() == AnalysisKind::ClassHierarchy;
  }

  virtual void invalidate(SILAnalysis::InvalidationKind K) {
    // Nothing can invalidate the ClassHierarchyAnalysis!
  }


  /// Returns a list of the known direct subclasses of a class \p C in
  /// the current module.
  const ClassList &getDirectSubClasses(ClassDecl *C) {
    return DirectSubclassesCache[C];
  }

  /// Returns a list of the known indirect subclasses of a class \p C in
  /// the current module.
  const ClassList &getIndirectSubClasses(ClassDecl *C) {
    if (!IndirectSubclassesCache.count(C)) {
      // Lazy initialization
      auto &K = IndirectSubclassesCache[C];
      getIndirectSubClasses(C, K);
    }
    return IndirectSubclassesCache[C];
  }

  const NominalTypeList &getProtocolImplementations(ProtocolDecl *P) {
    return ProtocolImplementationsCache[P];
  }

  /// Returns true if the class is inherited by another class in this module.
  bool hasKnownDirectSubclasses(ClassDecl *C) {
    return DirectSubclassesCache.count(C);
  }

  /// Returns true if the class is indirectly inherited by another class
  /// in this module.
  bool hasKnownIndirectSubclasses(ClassDecl *C) {
    return IndirectSubclassesCache.count(C) &&
           IndirectSubclassesCache[C].size() > 0;
  }

  /// Returns true if the protocol is implemented by any class in this module.
  bool hasKnownImplementations(ProtocolDecl *C) {
    return ProtocolImplementationsCache.count(C);
  }

  virtual void invalidate(SILFunction *F, SILAnalysis::InvalidationKind K) {
    invalidate(K);
  }

private:
  /// Compute inheritance properties.
  void init();
  void getIndirectSubClasses(ClassDecl *Base,
                             ClassList &IndirectSubs);
  /// The module
  SILModule *M;

  /// A cache that maps a class to all of its known direct subclasses.
  llvm::DenseMap<ClassDecl*, ClassList> DirectSubclassesCache;

  /// A cache that maps a class to all of its known indirect subclasses.
  llvm::DenseMap<ClassDecl*, ClassList> IndirectSubclassesCache;

  /// A cache that maps a protocol to all of its known implementations.
  ProtocolImplementations ProtocolImplementationsCache;
};

}
#endif
