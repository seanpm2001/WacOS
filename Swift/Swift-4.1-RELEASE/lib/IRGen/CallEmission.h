//===--- CallEmission.h - Utility for emitting calls ------------*- C++ -*-===//
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
//  This file defines the CallEmitter class.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IRGEN_CALLEMISSION_H
#define SWIFT_IRGEN_CALLEMISSION_H

#include "Callee.h"

namespace llvm {
  class CallSite;
}

namespace swift {
namespace irgen {

class Address;
class Explosion;
class LoadableTypeInfo;
struct WitnessMetadata;

/// A plan for emitting a series of calls.
class CallEmission {
public:
  IRGenFunction &IGF;

private:
  /// The builtin/special arguments to pass to the call.
  SmallVector<llvm::Value*, 8> Args;

  /// The function we're going to call.
  Callee CurCallee;

  unsigned LastArgWritten;

  /// Whether we've emitted the call for the current callee yet.  This
  /// is just for debugging purposes --- e.g. the destructor asserts
  /// that it's true --- but is otherwise derivable from
  /// RemainingArgsForCallee, at least between calls.
  bool EmittedCall;

  void setFromCallee();
  void emitToUnmappedMemory(Address addr);
  void emitToUnmappedExplosion(Explosion &out);
  llvm::CallSite emitCallSite();

public:
  CallEmission(IRGenFunction &IGF, Callee &&callee)
      : IGF(IGF), CurCallee(std::move(callee)) {
    setFromCallee();
  }
  CallEmission(const CallEmission &other) = delete;
  CallEmission(CallEmission &&other);
  CallEmission &operator=(const CallEmission &other) = delete;
  ~CallEmission();

  const Callee &getCallee() const { return CurCallee; }

  SubstitutionList getSubstitutions() const {
    return CurCallee.getSubstitutions();
  }

  /// Set the arguments to the function from an explosion.
  void setArgs(Explosion &arg, bool isOutlined,
               WitnessMetadata *witnessMetadata = nullptr);

  void addAttribute(unsigned Index, llvm::Attribute::AttrKind Attr);

  void emitToMemory(Address addr, const LoadableTypeInfo &substResultTI,
                    bool isOutlined);
  void emitToExplosion(Explosion &out, bool isOutlined);
};


}
}

#endif
