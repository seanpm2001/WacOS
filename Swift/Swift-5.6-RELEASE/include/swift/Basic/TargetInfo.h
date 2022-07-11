//===--- TargetInfo.h - Target Info Output ---------------------*- C++ -*-===//
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
// This file provides a high-level API for emitting target info
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_TARGETINFO_H
#define SWIFT_TARGETINFO_H

#include "swift/Basic/LLVM.h"

namespace llvm {
class Triple;
class VersionTuple;
}

namespace swift {
class CompilerInvocation;

namespace targetinfo {
void printTargetInfo(const CompilerInvocation &invocation,
                     llvm::raw_ostream &out);

void printTripleInfo(const llvm::Triple &triple,
                     llvm::Optional<llvm::VersionTuple> runtimeVersion,
                     llvm::raw_ostream &out);
} // namespace targetinfo
} // namespace swift

#endif
