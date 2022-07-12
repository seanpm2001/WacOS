//===--- OwnedString.h - String storage -------------------------*- C++ -*-===//
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
//  This file defines the 'OwnedString' storage wrapper, which can hold its own
//  unique copy of a string, or merely hold a reference to some point in a
//  source buffer, which is assumed to live at least as long as a value of this
//  type.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_BASIC_OWNEDSTRING_H
#define SWIFT_BASIC_OWNEDSTRING_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"

using llvm::StringRef;

namespace swift {

enum class StringOwnership {
  /// An OwnedString holds a weak reference to the underlying string storage
  /// and will never attempt to free it.
  Unowned,

  /// An OwnedString has its own copy of the underlying string storage and
  /// will free the storage upon its destruction.
  Copied,
};

/// Holds a string - either statically allocated or dynamically allocated
/// and owned by this type.
class OwnedString {
  const char *Data;
  size_t Length;
  StringOwnership Ownership = StringOwnership::Unowned;

  void release() {
    if (Ownership == StringOwnership::Copied)
      free(const_cast<char *>(Data));
  }

  void initialize(const char* Data, size_t Length, StringOwnership Ownership) {
    this->Length = Length;
    this->Ownership = Ownership;
    assert(Length >= 0 && "expected length to be non-negative");
    if (Ownership == StringOwnership::Copied && Data) {
      char *substring = static_cast<char *>(malloc(Length + 1));
      assert(substring && "expected successful malloc of copy");

      memcpy(substring, Data, Length);
      substring[Length] = '\0';

      this->Data = substring;
    }
    else
      this->Data = Data;
  }
  OwnedString(const char* Data, size_t Length, StringOwnership Ownership) {
    initialize(Data, Length, Ownership);
  }
public:
  OwnedString(): OwnedString(nullptr, 0, StringOwnership::Unowned) {}

  OwnedString(const char *Data, size_t Length):
    OwnedString(Data, Length, StringOwnership::Unowned) {}

  OwnedString(StringRef Str) : OwnedString(Str.data(), Str.size()) {}

  OwnedString(const char *Data) : OwnedString(StringRef(Data)) {}

  OwnedString(const OwnedString &Other):
    OwnedString(Other.Data, Other.Length, Other.Ownership) {}

  OwnedString(OwnedString &&Other): Data(Other.Data), Length(Other.Length),
      Ownership(Other.Ownership) {
    Other.Data = nullptr;
    Other.Ownership = StringOwnership::Unowned;
  }

  OwnedString& operator=(const OwnedString &Other) {
    if (&Other != this) {
      release();
      initialize(Other.Data, Other.Length, Other.Ownership);
    }
    return *this;
  }

  OwnedString& operator=(OwnedString &&Other) {
    if (&Other != this) {
      release();
      this->Data = Other.Data;
      this->Length = Other.Length;
      this->Ownership = Other.Ownership;
      Other.Ownership = StringOwnership::Unowned;
      Other.Data = nullptr;
    }
    return *this;
  }

  OwnedString copy() {
    return OwnedString(Data, Length, StringOwnership::Copied);
  }

  /// Returns the length of the string in bytes.
  size_t size() const {
    return Length;
  }

  /// Returns true if the length is 0.
  bool empty() const {
    return Length == 0;
  }

  /// Returns a StringRef to the underlying data. No copy is made and no
  /// ownership changes take place.
  StringRef str() const {
    return StringRef { Data, Length };
  }

  bool operator==(const OwnedString &Right) const {
    return str() == Right.str();
  }

  ~OwnedString() {
    release();
  }
};

} // end namespace swift

#endif // SWIFT_BASIC_OWNEDSTRING_H
