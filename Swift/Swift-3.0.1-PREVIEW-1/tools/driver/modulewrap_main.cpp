//===--- modulewrap_main.cpp - module wrapping utility --------------------===//
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
// Wraps .swiftmodule files inside an object file container so they
// can be passed to the linker directly. Mostly useful for platforms
// where the debug info typically stays in the executable.
// (ie. ELF-based platforms).
//
//===----------------------------------------------------------------------===//

#include "swift/AST/DiagnosticsFrontend.h"
#include "swift/Frontend/Frontend.h"
#include "swift/Frontend/PrintingDiagnosticConsumer.h"
#include "swift/Option/Options.h"
#include "swift/Serialization/ModuleFormat.h"
#include "swift/Subsystems.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Bitcode/BitstreamReader.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"

using namespace llvm::opt;
using namespace swift;

class ModuleWrapInvocation {
private:
  std::string MainExecutablePath;
  std::string OutputFilename = "-";
  llvm::Triple TargetTriple;
  std::vector<std::string> InputFilenames;

public:
  void setMainExecutablePath(const std::string &Path) {
    MainExecutablePath = Path;
  }

  const std::string &getOutputFilename() { return OutputFilename; }

  const std::vector<std::string> &getInputFilenames() { return InputFilenames; }
  llvm::Triple &getTargetTriple() { return TargetTriple; }

  int parseArgs(llvm::ArrayRef<const char *> Args, DiagnosticEngine &Diags) {
    using namespace options;

    // Parse frontend command line options using Swift's option table.
    std::unique_ptr<llvm::opt::OptTable> Table = createSwiftOptTable();
    unsigned MissingIndex;
    unsigned MissingCount;
    llvm::opt::InputArgList ParsedArgs =
      Table->ParseArgs(Args, MissingIndex, MissingCount,
                       ModuleWrapOption);
    if (MissingCount) {
      Diags.diagnose(SourceLoc(), diag::error_missing_arg_value,
                     ParsedArgs.getArgString(MissingIndex), MissingCount);
      return 1;
    }

    if (const Arg *A = ParsedArgs.getLastArg(options::OPT_target))
      TargetTriple = llvm::Triple(llvm::Triple::normalize(A->getValue()));
    else
      TargetTriple = llvm::Triple(llvm::sys::getDefaultTargetTriple());

    if (ParsedArgs.hasArg(OPT_UNKNOWN)) {
      for (const Arg *A : make_range(ParsedArgs.filtered_begin(OPT_UNKNOWN),
                                     ParsedArgs.filtered_end())) {
        Diags.diagnose(SourceLoc(), diag::error_unknown_arg,
                       A->getAsString(ParsedArgs));
      }
      return true;
    }

    if (ParsedArgs.getLastArg(OPT_help)) {
      std::string ExecutableName = llvm::sys::path::stem(MainExecutablePath);
      Table->PrintHelp(llvm::outs(), ExecutableName.c_str(),
                       "Swift Module Wrapper", options::ModuleWrapOption, 0);
      return 1;
    }

    for (const Arg *A : make_range(ParsedArgs.filtered_begin(OPT_INPUT),
                                   ParsedArgs.filtered_end())) {
      InputFilenames.push_back(A->getValue());
    }

    if (InputFilenames.empty()) {
      Diags.diagnose(SourceLoc(), diag::error_mode_requires_an_input_file);
      return 1;
    }

    if (const Arg *A = ParsedArgs.getLastArg(OPT_o)) {
      OutputFilename = A->getValue();
    }

    return 0;
  }
};

int modulewrap_main(ArrayRef<const char *> Args, const char *Argv0,
                    void *MainAddr) {
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  CompilerInstance Instance;
  PrintingDiagnosticConsumer PDC;
  Instance.addDiagnosticConsumer(&PDC);

  ModuleWrapInvocation Invocation;
  std::string MainExecutablePath =
      llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
  Invocation.setMainExecutablePath(MainExecutablePath);

  // Parse arguments.
  if (Invocation.parseArgs(Args, Instance.getDiags()) != 0) {
    return 1;
  }

  if (Invocation.getInputFilenames().size() != 1) {
    Instance.getDiags().diagnose(SourceLoc(),
                                 diag::error_mode_requires_one_input_file);
    return 1;
  }

  StringRef Filename = Invocation.getInputFilenames()[0];
  auto ErrOrBuf = llvm::MemoryBuffer::getFile(Filename);
  if (!ErrOrBuf) {
    Instance.getDiags().diagnose(
        SourceLoc(), diag::error_no_such_file_or_directory, Filename);
    return 1;
  }

  // Superficially verify that the input is a swift module file.
  llvm::BitstreamReader Reader((unsigned char *)(*ErrOrBuf)->getBufferStart(),
                               (unsigned char *)(*ErrOrBuf)->getBufferEnd());
  llvm::BitstreamCursor Cursor(Reader);
  for (unsigned char Byte : serialization::MODULE_SIGNATURE)
    if (Cursor.AtEndOfStream() || Cursor.Read(8) != Byte) {
      Instance.getDiags().diagnose(SourceLoc(), diag::error_parse_input_file,
                                   Filename, "signature mismatch");
      return 1;
    }

  // Wrap the bitstream in a module object file. To use the ClangImporter to
  // create the module loader, we need to properly set the runtime library path.
  SearchPathOptions SearchPathOpts;
  // FIXME: This logic has been duplicated from
  //        CompilerInvocation::setMainExecutablePath. ModuleWrapInvocation
  //        should share its implementation.
  SmallString<128> RuntimeResourcePath(MainExecutablePath);
  llvm::sys::path::remove_filename(RuntimeResourcePath); // Remove /swift
  llvm::sys::path::remove_filename(RuntimeResourcePath); // Remove /bin
  llvm::sys::path::append(RuntimeResourcePath, "lib", "swift");
  SearchPathOpts.RuntimeResourcePath = RuntimeResourcePath.str();

  SourceManager SrcMgr;
  LangOptions LangOpts;
  LangOpts.Target = Invocation.getTargetTriple();
  ASTContext ASTCtx(LangOpts, SearchPathOpts, SrcMgr, Instance.getDiags());
  ClangImporterOptions ClangImporterOpts;
  ASTCtx.addModuleLoader(ClangImporter::create(ASTCtx, ClangImporterOpts),
                         true);
  Module *M = Module::create(ASTCtx.getIdentifier("swiftmodule"), ASTCtx);
  SILOptions SILOpts;
  std::unique_ptr<SILModule> SM = SILModule::createEmptyModule(M, SILOpts);
  createSwiftModuleObjectFile(*SM, (*ErrOrBuf)->getBuffer(),
                              Invocation.getOutputFilename());
  return 0;
}
