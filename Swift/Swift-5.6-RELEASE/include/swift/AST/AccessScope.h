//===--- AccessScope.h - Swift Access Scope ---------------------*- C++ -*-===//
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

#ifndef SWIFT_ACCESSSCOPE_H
#define SWIFT_ACCESSSCOPE_H

#include "swift/AST/AttrKind.h"
#include "swift/AST/DeclContext.h"
#include "swift/Basic/Debug.h"
#include "llvm/ADT/PointerIntPair.h"

namespace swift {

/// The wrapper around the outermost DeclContext from which
/// a particular declaration can be accessed.
class AccessScope {
  /// The declaration context (if not public) along with a bit saying
  /// whether this scope is private, SPI or not.
  /// If the declaration context is set, the bit means that the scope is
  /// private or not. If the declaration context is null, the bit means that
  /// this scope is SPI or not.
  llvm::PointerIntPair<const DeclContext *, 1, bool> Value;

public:
  AccessScope(const DeclContext *DC, bool isPrivate = false);

  static AccessScope getPublic() { return AccessScope(nullptr, false); }

  /// Check if private access is allowed. This is a lexical scope check in Swift
  /// 3 mode. In Swift 4 mode, declarations and extensions of the same type will
  /// also allow access.
  static bool allowsPrivateAccess(const DeclContext *useDC, const DeclContext *sourceDC);

  /// Returns nullptr if access scope is public.
  const DeclContext *getDeclContext() const { return Value.getPointer(); }

  bool operator==(AccessScope RHS) const { return Value == RHS.Value; }
  bool operator!=(AccessScope RHS) const { return !(*this == RHS); }
  bool hasEqualDeclContextWith(AccessScope RHS) const {
    return getDeclContext() == RHS.getDeclContext();
  }

  bool isPublic() const { return !Value.getPointer(); }
  bool isPrivate() const { return Value.getPointer() && Value.getInt(); }
  bool isFileScope() const;
  bool isInternal() const;

  /// Returns true if this is a child scope of the specified other access scope.
  ///
  /// \see DeclContext::isChildContextOf
  bool isChildOf(AccessScope AS) const {
    if (!isPublic() && !AS.isPublic())
      return allowsPrivateAccess(getDeclContext(), AS.getDeclContext());
    if (isPublic() && AS.isPublic())
      return false;
    return AS.isPublic();
  }

  /// Returns the associated access level for diagnostic purposes.
  AccessLevel accessLevelForDiagnostics() const;

  /// Returns the minimum access level required to access
  /// associated DeclContext for diagnostic purposes.
  AccessLevel requiredAccessForDiagnostics() const {
    return isFileScope()
      ? AccessLevel::FilePrivate
      : accessLevelForDiagnostics();
  }

  /// Returns the narrowest access scope if this and the specified access scope
  /// have common intersection, or None if scopes don't intersect.
  const Optional<AccessScope> intersectWith(AccessScope accessScope) const {
    if (hasEqualDeclContextWith(accessScope)) {
      if (isPrivate())
        return *this;
      return accessScope;
    }
    if (isChildOf(accessScope))
      return *this;
    if (accessScope.isChildOf(*this))
      return accessScope;

    return None;
  }

  SWIFT_DEBUG_DUMP;
};

} // end namespace swift

#endif
