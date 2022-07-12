//===--- Types.cpp - Driver input & temporary type information ------------===//
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

#include "swift/Driver/Types.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"

using namespace swift;
using namespace swift::driver;
using namespace swift::driver::types;

struct TypeInfo {
  const char *Name;
  const char *Flags;
  const char *TempSuffix;
};

static const TypeInfo TypeInfos[] = {
#define TYPE(NAME, ID, TEMP_SUFFIX, FLAGS) \
  { NAME, FLAGS, TEMP_SUFFIX },
#include "swift/Driver/Types.def"
};

static const TypeInfo &getInfo(unsigned Id) {
  assert(Id >= 0 && Id < TY_INVALID && "Invalid Type ID.");
  return TypeInfos[Id];
}

StringRef types::getTypeName(ID Id) {
  return getInfo(Id).Name;
}

StringRef types::getTypeTempSuffix(ID Id) {
  return getInfo(Id).TempSuffix;
}

ID types::lookupTypeForExtension(StringRef Ext) {
  if (Ext.empty())
    return TY_INVALID;
  assert(Ext.front() == '.' && "not a file extension");
  return llvm::StringSwitch<types::ID>(Ext.drop_front())
#define TYPE(NAME, ID, SUFFIX, FLAGS) \
           .Case(SUFFIX, TY_##ID)
#include "swift/Driver/Types.def"
           .Default(TY_INVALID);
}

ID types::lookupTypeForName(StringRef Name) {
  return llvm::StringSwitch<types::ID>(Name)
#define TYPE(NAME, ID, SUFFIX, FLAGS) \
           .Case(NAME, TY_##ID)
#include "swift/Driver/Types.def"
           .Default(TY_INVALID);
}

bool types::isTextual(ID Id) {
  switch (Id) {
  case types::TY_Swift:
  case types::TY_SIL:
  case types::TY_Dependencies:
  case types::TY_Assembly:
  case types::TY_RawSIL:
  case types::TY_LLVM_IR:
  case types::TY_ObjCHeader:
  case types::TY_AutolinkFile:
  case types::TY_ImportedModules:
  case types::TY_TBD:
  case types::TY_ModuleTrace:
  case types::TY_OptRecord:
    return true;
  case types::TY_Image:
  case types::TY_Object:
  case types::TY_dSYM:
  case types::TY_PCH:
  case types::TY_SIB:
  case types::TY_RawSIB:
  case types::TY_SwiftModuleFile:
  case types::TY_SwiftModuleDocFile:
  case types::TY_LLVM_BC:
  case types::TY_SerializedDiagnostics:
  case types::TY_ClangModuleFile:
  case types::TY_SwiftDeps:
  case types::TY_Nothing:
  case types::TY_Remapping:
  case types::TY_IndexData:
    return false;
  case types::TY_INVALID:
    llvm_unreachable("Invalid type ID.");
  }

  // Work around MSVC warning: not all control paths return a value
  llvm_unreachable("All switch cases are covered");
}

bool types::isAfterLLVM(ID Id) {
  switch (Id) {
  case types::TY_Assembly:
  case types::TY_LLVM_IR:
  case types::TY_LLVM_BC:
  case types::TY_Object:
    return true;
  case types::TY_Swift:
  case types::TY_PCH:
  case types::TY_ImportedModules:
  case types::TY_TBD:
  case types::TY_SIL:
  case types::TY_Dependencies:
  case types::TY_RawSIL:
  case types::TY_ObjCHeader:
  case types::TY_AutolinkFile:
  case types::TY_Image:
  case types::TY_dSYM:
  case types::TY_SIB:
  case types::TY_RawSIB:
  case types::TY_SwiftModuleFile:
  case types::TY_SwiftModuleDocFile:
  case types::TY_SerializedDiagnostics:
  case types::TY_ClangModuleFile:
  case types::TY_SwiftDeps:
  case types::TY_Nothing:
  case types::TY_Remapping:
  case types::TY_IndexData:
  case types::TY_ModuleTrace:
  case types::TY_OptRecord:
    return false;
  case types::TY_INVALID:
    llvm_unreachable("Invalid type ID.");
  }

  // Work around MSVC warning: not all control paths return a value
  llvm_unreachable("All switch cases are covered");
}

bool types::isPartOfSwiftCompilation(ID Id) {
  switch (Id) {
  case types::TY_Swift:
  case types::TY_SIL:
  case types::TY_RawSIL:
  case types::TY_SIB:
  case types::TY_RawSIB:
    return true;
  case types::TY_Assembly:
  case types::TY_LLVM_IR:
  case types::TY_LLVM_BC:
  case types::TY_Object:
  case types::TY_Dependencies:
  case types::TY_ObjCHeader:
  case types::TY_AutolinkFile:
  case types::TY_PCH:
  case types::TY_ImportedModules:
  case types::TY_TBD:
  case types::TY_Image:
  case types::TY_dSYM:
  case types::TY_SwiftModuleFile:
  case types::TY_SwiftModuleDocFile:
  case types::TY_SerializedDiagnostics:
  case types::TY_ClangModuleFile:
  case types::TY_SwiftDeps:
  case types::TY_Nothing:
  case types::TY_Remapping:
  case types::TY_IndexData:
  case types::TY_ModuleTrace:
  case types::TY_OptRecord:
    return false;
  case types::TY_INVALID:
    llvm_unreachable("Invalid type ID.");
  }

  // Work around MSVC warning: not all control paths return a value
  llvm_unreachable("All switch cases are covered");
}
