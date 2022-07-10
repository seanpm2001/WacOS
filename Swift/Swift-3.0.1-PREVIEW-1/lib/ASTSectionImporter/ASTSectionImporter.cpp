//===--- ASTSectionImporter.cpp - Import AST Section Modules --------------===//
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
// This file implements support for loading modules serialized into a
// Mach-O AST section into Swift.
//
//===----------------------------------------------------------------------===//

#include "swift/ASTSectionImporter/ASTSectionImporter.h"
#include "swift/Basic/Dwarf.h"
#include "swift/Serialization/SerializedModuleLoader.h"
#include "swift/Serialization/Validation.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace swift;

bool swift::parseASTSection(SerializedModuleLoader *SML, StringRef buf,
                            SmallVectorImpl<std::string> &foundModules) {
  if (!serialization::isSerializedAST(buf))
    return false;

  // An AST section consists of one or more AST modules, optionally with
  // headers. Iterate over all AST modules.
  while (!buf.empty()) {
    auto info = serialization::validateSerializedAST(buf);

    assert(info.name.size() < (2 << 10) && "name failed sanity check");

    if (info.status == serialization::Status::Valid) {
      assert(info.bytes != 0);
      if (!info.name.empty()) {
        StringRef moduleData = buf.substr(0, info.bytes);
        std::unique_ptr<llvm::MemoryBuffer> bitstream(
          llvm::MemoryBuffer::getMemBuffer(moduleData, info.name, false));

        // Register the memory buffer.
        SML->registerMemoryBuffer(info.name, std::move(bitstream));
        foundModules.push_back(info.name);
      }
    } else {
      llvm::dbgs() << "Unable to load module";
      if (!info.name.empty())
        llvm::dbgs() << '\'' << info.name << '\'';
      llvm::dbgs() << ".\n";
    }

    if (info.bytes == 0)
      return false;

    if (info.bytes > buf.size()) {
      llvm::dbgs() << "AST section too small.\n";
      return false;
    }

    buf = buf.substr(info.bytes);
  }

  return true;
}
