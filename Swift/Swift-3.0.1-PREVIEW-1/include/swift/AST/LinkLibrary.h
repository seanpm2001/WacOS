//===--- LinkLibrary.h - A module-level linker dependency -------*- C++ -*-===//
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

#ifndef SWIFT_AST_LINKLIBRARY_H
#define SWIFT_AST_LINKLIBRARY_H

#include "swift/Basic/LLVM.h"
#include "llvm/ADT/SmallString.h"
#include <string>

namespace swift {

// Must be kept in sync with diag::error_immediate_mode_missing_library.
enum class LibraryKind {
  Library = 0,
  Framework
};

/// Represents a linker dependency for an imported module.
// FIXME: This is basically a slightly more generic version of Clang's
// Module::LinkLibrary.
class LinkLibrary {
private:
  std::string Name;
  unsigned Kind : 1;
  unsigned ForceLoad : 1;

public:
  LinkLibrary(StringRef N, LibraryKind K, bool forceLoad = false)
    : Name(N), Kind(static_cast<unsigned>(K)), ForceLoad(forceLoad) {
    assert(getKind() == K && "not enough bits for the kind");
  }

  LibraryKind getKind() const { return static_cast<LibraryKind>(Kind); }
  StringRef getName() const { return Name; }
  bool shouldForceLoad() const { return ForceLoad; }
};

} // end namespace swift

#endif
