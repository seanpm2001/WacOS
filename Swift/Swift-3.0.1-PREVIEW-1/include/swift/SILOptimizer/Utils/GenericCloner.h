//===--- GenericCloner.h - Specializes generic functions  -------*- C++ -*-===//
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
// This contains the definition of a cloner class for creating specialized
// versions of generic functions by substituting concrete types.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_GENERICCLONER_H
#define SWIFT_SIL_GENERICCLONER_H

#include "swift/AST/Type.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/TypeSubstCloner.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "swift/SILOptimizer/Utils/Generics.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <functional>

namespace swift {

class GenericCloner : public TypeSubstCloner<GenericCloner> {
  IsFragile_t Fragile;
  const ReabstractionInfo &ReInfo;
  CloneCollector::CallbackType Callback;

public:
  friend class SILCloner<GenericCloner>;

  GenericCloner(SILFunction *F,
                IsFragile_t Fragile,
                const ReabstractionInfo &ReInfo,
                TypeSubstitutionMap &ContextSubs,
                ArrayRef<Substitution> ParamSubs,
                StringRef NewName,
                CloneCollector::CallbackType Callback)
  : TypeSubstCloner(*initCloned(F, Fragile, ReInfo, NewName), *F, ContextSubs,
                    ParamSubs), ReInfo(ReInfo), Callback(Callback) {
    assert(F->getDebugScope()->Parent != getCloned()->getDebugScope()->Parent);
  }
  /// Clone and remap the types in \p F according to the substitution
  /// list in \p Subs. Parameters are re-abstracted (changed from indirect to
  /// direct) according to \p ReInfo.
  static SILFunction *
  cloneFunction(SILFunction *F,
                IsFragile_t Fragile,
                const ReabstractionInfo &ReInfo,
                TypeSubstitutionMap &ContextSubs,
                ArrayRef<Substitution> ParamSubs,
                StringRef NewName,
                CloneCollector::CallbackType Callback =nullptr) {
    // Clone and specialize the function.
    GenericCloner SC(F, Fragile, ReInfo, ContextSubs, ParamSubs,
                     NewName, Callback);
    SC.populateCloned();
    SC.cleanUp(SC.getCloned());
    return SC.getCloned();
  }

protected:
  // FIXME: We intentionally call SILClonerWithScopes here to ensure
  //        the debug scopes are set correctly for cloned
  //        functions. TypeSubstCloner, SILClonerWithScopes, and
  //        SILCloner desperately need refactoring and/or combining so
  //        that the obviously right things are happening for cloning
  //        vs. inlining.
  void postProcess(SILInstruction *Orig, SILInstruction *Cloned) {
    // Call client-supplied callback function.
    if (Callback)
      Callback(Orig, Cloned);

    SILClonerWithScopes<GenericCloner>::postProcess(Orig, Cloned);
  }

private:
  static SILFunction *initCloned(SILFunction *Orig,
                                 IsFragile_t Fragile,
                                 const ReabstractionInfo &ReInfo,
                                 StringRef NewName);
  /// Clone the body of the function into the empty function that was created
  /// by initCloned.
  void populateCloned();
  SILFunction *getCloned() { return &getBuilder().getFunction(); }
};

} // end namespace swift

#endif
