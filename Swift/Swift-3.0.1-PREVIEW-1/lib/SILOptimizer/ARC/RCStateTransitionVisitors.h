//===--- RCStateTransitionVisitors.h ----------------------------*- C++ -*-===//
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
// This file contains RCStateTransitionVisitors for performing ARC dataflow. It
// is necessary to prevent a circular dependency in between RefCountState.h and
// RCStateTransition.h
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SILOPTIMIZER_PASSMANAGER_ARC_RCSTATETRANSITIONVISITORS_H
#define SWIFT_SILOPTIMIZER_PASSMANAGER_ARC_RCSTATETRANSITIONVISITORS_H

#include "ARCBBState.h"
#include "ARCRegionState.h"
#include "RCStateTransition.h"
#include "swift/Basic/ImmutablePointerSet.h"
#include "swift/Basic/BlotMapVector.h"

//===----------------------------------------------------------------------===//
//                          RCStateTransitionVisitor
//===----------------------------------------------------------------------===//

namespace swift {

/// Define a visitor for visiting instructions according to their
/// RCStateTransitionKind.
template <typename ImplTy, typename ResultTy>
class RCStateTransitionKindVisitor {
  ImplTy &asImpl() { return *reinterpret_cast<ImplTy *>(this); }

public:
#define KIND(K) ResultTy visit ## K(ValueBase *) { return ResultTy(); }
#include "RCStateTransition.def"

  ResultTy visit(ValueBase *V) {
    switch (getRCStateTransitionKind(V)) {
#define KIND(K)                                 \
  case RCStateTransitionKind::K:                \
    return asImpl().visit ## K(V);
#include "RCStateTransition.def"
    }
    llvm_unreachable("Covered switch isn't covered?!");
  }
};

} // end swift namespace

//===----------------------------------------------------------------------===//
//                      RCStateTransitionDataflowResult
//===----------------------------------------------------------------------===//

namespace swift {

enum class RCStateTransitionDataflowResultKind {
  /// Can this dataflow result have no further effects on any state. This means
  /// we can just early out and break early.
  NoEffects,

  /// Must we check for effects.
  CheckForEffects,
};

struct RCStateTransitionDataflowResult {
  using DataflowResultKind = RCStateTransitionDataflowResultKind;

  DataflowResultKind Kind;
  SILValue RCIdentity;
  bool NestingDetected = false;

  RCStateTransitionDataflowResult()
      : Kind(DataflowResultKind::CheckForEffects), RCIdentity() {}
  RCStateTransitionDataflowResult(DataflowResultKind Kind)
      : Kind(Kind), RCIdentity() {}
  RCStateTransitionDataflowResult(SILValue RCIdentity,
                                  bool NestingDetected = false)
      : Kind(DataflowResultKind::CheckForEffects), RCIdentity(RCIdentity),
        NestingDetected(NestingDetected) {}
  RCStateTransitionDataflowResult(const RCStateTransitionDataflowResult &) =
      default;
  ~RCStateTransitionDataflowResult() = default;
};

} // end swift namespace

namespace llvm {
raw_ostream &operator<<(raw_ostream &os,
                        swift::RCStateTransitionDataflowResult Kind);
} // end llvm namespace

//===----------------------------------------------------------------------===//
//                       BottomUpdataflowRCStateVisitor
//===----------------------------------------------------------------------===//

namespace swift {

/// A visitor for performing the bottom up dataflow depending on the
/// RCState. Enables behavior to be cleanly customized depending on the
/// RCStateTransition associated with an instruction.
template <class ARCState>
class BottomUpDataflowRCStateVisitor
    : public RCStateTransitionKindVisitor<BottomUpDataflowRCStateVisitor<ARCState>,
                                          RCStateTransitionDataflowResult> {
  /// A local typedef to make things cleaner.
  using DataflowResult = RCStateTransitionDataflowResult;
  using IncToDecStateMapTy =
      BlotMapVector<SILInstruction *, BottomUpRefCountState>;

public:
  RCIdentityFunctionInfo *RCFI;
  ARCState &DataflowState;
  bool FreezeOwnedArgEpilogueReleases;
  ConsumedArgToEpilogueReleaseMatcher &EpilogueReleaseMatcher;
  BlotMapVector<SILInstruction *, BottomUpRefCountState> &IncToDecStateMap;
  ImmutablePointerSetFactory<SILInstruction> &SetFactory;

public:
  BottomUpDataflowRCStateVisitor(
      RCIdentityFunctionInfo *RCFI, ARCState &DataflowState,
      bool FreezeOwnedArgEpilogueReleases,
      ConsumedArgToEpilogueReleaseMatcher &ERM,
      IncToDecStateMapTy &IncToDecStateMap,
      ImmutablePointerSetFactory<SILInstruction> &SetFactory);
  DataflowResult visitAutoreleasePoolCall(ValueBase *V);
  DataflowResult visitStrongDecrement(ValueBase *V);
  DataflowResult visitStrongIncrement(ValueBase *V);
};

} // end swift namespace

//===----------------------------------------------------------------------===//
//                       TopDownDataflowRCStateVisitor
//===----------------------------------------------------------------------===//

namespace swift {

/// A visitor for performing the bottom up dataflow depending on the
/// RCState. Enables behavior to be cleanly customized depending on the
/// RCStateTransition associated with an instruction.
template <class ARCState>
class TopDownDataflowRCStateVisitor
    : public RCStateTransitionKindVisitor<TopDownDataflowRCStateVisitor<ARCState>,
                                          RCStateTransitionDataflowResult> {
  /// A local typedef to make things cleaner.
  using DataflowResult = RCStateTransitionDataflowResult;
  using DecToIncStateMapTy =
      BlotMapVector<SILInstruction *, TopDownRefCountState>;

  RCIdentityFunctionInfo *RCFI;
  ARCState &DataflowState;
  DecToIncStateMapTy &DecToIncStateMap;
  ImmutablePointerSetFactory<SILInstruction> &SetFactory;

public:
  TopDownDataflowRCStateVisitor(
      RCIdentityFunctionInfo *RCFI, ARCState &State,
      DecToIncStateMapTy &DecToIncStateMap,
      ImmutablePointerSetFactory<SILInstruction> &SetFactory);
  DataflowResult visitAutoreleasePoolCall(ValueBase *V);
  DataflowResult visitStrongDecrement(ValueBase *V);
  DataflowResult visitStrongIncrement(ValueBase *V);
  DataflowResult visitStrongEntrance(ValueBase *V);

private:
  DataflowResult visitStrongEntranceApply(ApplyInst *AI);
  DataflowResult visitStrongEntranceArgument(SILArgument *Arg);
  DataflowResult visitStrongEntranceAllocRef(AllocRefInst *ARI);
  DataflowResult visitStrongEntranceAllocRefDynamic(AllocRefDynamicInst *ARI);
  DataflowResult visitStrongAllocBox(AllocBoxInst *ABI);
};

} // end swift namespace

//===----------------------------------------------------------------------===//
//                       Forward Template Declarations
//===----------------------------------------------------------------------===//

namespace swift {

extern template class BottomUpDataflowRCStateVisitor<
    ARCSequenceDataflowEvaluator::ARCBBState>;
extern template class BottomUpDataflowRCStateVisitor<ARCRegionState>;

extern template class TopDownDataflowRCStateVisitor<
    ARCSequenceDataflowEvaluator::ARCBBState>;
extern template class TopDownDataflowRCStateVisitor<ARCRegionState>;

} // end swift namespace

#endif
