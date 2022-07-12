//===--- CSDiag.h - Constraint-based Type Checking ----*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file provides shared utility methods for ConstraintSystem diagnosis.
//
//===----------------------------------------------------------------------===//


#ifndef SWIFT_SEMA_CSDIAG_H
#define SWIFT_SEMA_CSDIAG_H

namespace swift {
  
  std::string getTypeListString(Type type);
  
  /// Rewrite any type variables & archetypes in the specified type with
  /// UnresolvedType.
  Type replaceTypeParametersWithUnresolved(Type ty);
  Type replaceTypeVariablesWithUnresolved(Type ty);
  
};

#endif /* SWIFT_SEMA_CSDIAG_H */
