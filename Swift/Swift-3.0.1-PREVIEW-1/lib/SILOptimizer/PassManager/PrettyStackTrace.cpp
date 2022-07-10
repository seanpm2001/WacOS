//===--- PrettyStackTrace.cpp ---------------------------------------------===//
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

#include "swift/SILOptimizer/PassManager/PrettyStackTrace.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "llvm/Support/raw_ostream.h"

using namespace swift;

void PrettyStackTraceSILFunctionTransform::print(llvm::raw_ostream &out) const {
  out << "While running pass #" << PassNumber
      << " SILFunctionTransform \"" << SFT->getName()
      << "\" on SILFunction ";
  if (!SFT->getFunction()) {
    out << " <<null>>";
    return;
  }
  out << "\"";
  SFT->getFunction()->printName(out);
  out << "\".\n";
}

void PrettyStackTraceSILModuleTransform::print(llvm::raw_ostream &out) const {
  out << "While running pass #" << PassNumber
      << " SILModuleTransform \"" << SMT->getName() << "\".\n";
}
