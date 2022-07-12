//===--- TypeCheckAvailability.h - Availability Diagnostics -----*- C++ -*-===//
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

#ifndef SWIFT_SEMA_TYPE_CHECK_AVAILABILITY_H
#define SWIFT_SEMA_TYPE_CHECK_AVAILABILITY_H

#include "swift/AST/AttrKind.h"
#include "swift/AST/Identifier.h"
#include "swift/Basic/LLVM.h"
#include "swift/Basic/SourceLoc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"

namespace swift {
  class DeclContext;
  class Expr;
  class TypeChecker;
  class ValueDecl;

/// Diagnose uses of unavailable declarations.
void diagAvailability(TypeChecker &TC, const Expr *E,
                      DeclContext *DC);

/// Run the Availability-diagnostics algorithm otherwise used in an expr
/// context, but for non-expr contexts such as TypeDecls referenced from
/// TypeReprs.
bool diagnoseDeclAvailability(const ValueDecl *Decl,
                              TypeChecker &TC,
                              DeclContext *DC,
                              SourceRange R,
                              bool AllowPotentiallyUnavailableProtocol,
                              bool SignalOnPotentialUnavailability);

} // namespace swift

#endif // SWIFT_SEMA_TYPE_CHECK_AVAILABILITY_H

