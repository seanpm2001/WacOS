//===--- ImageInspectionELF.h -----------------------------------*- C++ -*-===//
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
///
/// \file
///
/// ELF specific image inspection routines.
///
//===----------------------------------------------------------------------===//

#ifndef SWIFT_RUNTIME_IMAGEINSPECTIONELF_H
#define SWIFT_RUNTIME_IMAGEINSPECTIONELF_H

#if defined(__ELF__)

#include "../SwiftShims/Visibility.h"
#include <cstdint>
#include <cstddef>

namespace swift {
struct SectionInfo {
  uint64_t size;
  const char *data;
};

static constexpr const uintptr_t CurrentSectionMetadataVersion = 1;

struct MetadataSections {
  uintptr_t version;
  uintptr_t reserved;

  mutable const MetadataSections *next;
  mutable const MetadataSections *prev;

  struct Range {
    uintptr_t start;
    size_t length;
  };

  Range swift2_protocol_conformances;
  Range swift2_type_metadata;
  Range swift3_typeref;
  Range swift3_reflstr;
  Range swift3_fieldmd;
  Range swift3_assocty;
};
} // namespace swift

// Called by injected constructors when a dynamic library is loaded.
SWIFT_RUNTIME_EXPORT
void swift_addNewDSOImage(const void *addr);

#endif // defined(__ELF__)

#endif // SWIFT_RUNTIME_IMAGE_INSPECTION_ELF_H
