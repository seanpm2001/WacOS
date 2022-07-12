//===--- ToolChains.cpp - Job invocations (general and per-platform) ------===//
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

#include "ToolChains.h"

#include "swift/Basic/Dwarf.h"
#include "swift/Basic/LLVM.h"
#include "swift/Basic/Platform.h"
#include "swift/Basic/Range.h"
#include "swift/Basic/TaskQueue.h"
#include "swift/Driver/Driver.h"
#include "swift/Driver/Job.h"
#include "swift/Frontend/Frontend.h"
#include "swift/Option/Options.h"
#include "swift/Config.h"
#include "clang/Basic/Version.h"
#include "clang/Driver/Util.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"

using namespace swift;
using namespace swift::driver;
using namespace llvm::opt;

/// The limit for passing a list of files on the command line.
static const size_t TOO_MANY_FILES = 128;

static void addInputsOfType(ArgStringList &Arguments,
                            ArrayRef<const Action *> Inputs,
                            types::ID InputType,
                            const char *PrefixArgument = nullptr) {
  for (auto &Input : Inputs) {
    if (Input->getType() != InputType)
      continue;
    if (PrefixArgument)
      Arguments.push_back(PrefixArgument);
    Arguments.push_back(cast<InputAction>(Input)->getInputArg().getValue());
  }
}

static void addInputsOfType(ArgStringList &Arguments,
                            ArrayRef<const Job *> Jobs,
                            types::ID InputType,
                            const char *PrefixArgument = nullptr) {
  for (const Job *Cmd : Jobs) {
    auto &output = Cmd->getOutput().getAnyOutputForType(InputType);
    if (!output.empty()) {
      if (PrefixArgument)
        Arguments.push_back(PrefixArgument);
      Arguments.push_back(output.c_str());
    }
  }
}

static void addPrimaryInputsOfType(ArgStringList &Arguments,
                                   ArrayRef<const Job *> Jobs,
                                   types::ID InputType,
                                   const char *PrefixArgument = nullptr) {
  for (const Job *Cmd : Jobs) {
    auto &outputInfo = Cmd->getOutput();
    if (outputInfo.getPrimaryOutputType() == InputType) {
      for (const std::string &Output : outputInfo.getPrimaryOutputFilenames()) {
        if (PrefixArgument)
          Arguments.push_back(PrefixArgument);
        Arguments.push_back(Output.c_str());
      }
    }
  }
}

/// Handle arguments common to all invocations of the frontend (compilation,
/// module-merging, LLDB's REPL, etc).
static void addCommonFrontendArgs(const ToolChain &TC,
                                  const OutputInfo &OI,
                                  const CommandOutput &output,
                                  const ArgList &inputArgs,
                                  ArgStringList &arguments) {
  const llvm::Triple &Triple = TC.getTriple();

  // Only pass -target to the REPL or immediate modes if it was explicitly
  // specified on the command line.
  switch (OI.CompilerMode) {
  case OutputInfo::Mode::REPL:
  case OutputInfo::Mode::Immediate:
    if (!inputArgs.hasArg(options::OPT_target))
      break;
    LLVM_FALLTHROUGH;
  case OutputInfo::Mode::StandardCompile:
  case OutputInfo::Mode::SingleCompile:
    arguments.push_back("-target");
    arguments.push_back(inputArgs.MakeArgString(Triple.str()));
    break;
  }

  // Enable address top-byte ignored in the ARM64 backend.
  if (Triple.getArch() == llvm::Triple::aarch64) {
    arguments.push_back("-Xllvm");
    arguments.push_back("-aarch64-use-tbi");
  }

  // Enable or disable ObjC interop appropriately for the platform
  if (Triple.isOSDarwin()) {
    arguments.push_back("-enable-objc-interop");
  } else {
    arguments.push_back("-disable-objc-interop");
  }

  // Handle the CPU and its preferences.
  inputArgs.AddLastArg(arguments, options::OPT_target_cpu);

  if (!OI.SDKPath.empty()) {
    arguments.push_back("-sdk");
    arguments.push_back(inputArgs.MakeArgString(OI.SDKPath));
  }

  inputArgs.AddAllArgs(arguments, options::OPT_I);
  inputArgs.AddAllArgs(arguments, options::OPT_F, options::OPT_Fsystem);

  inputArgs.AddLastArg(arguments, options::OPT_AssertConfig);
  inputArgs.AddLastArg(arguments, options::OPT_autolink_force_load);
  inputArgs.AddLastArg(arguments, options::OPT_color_diagnostics);
  inputArgs.AddLastArg(arguments, options::OPT_fixit_all);
  inputArgs.AddLastArg(arguments,
                       options::OPT_warn_swift3_objc_inference_minimal,
                       options::OPT_warn_swift3_objc_inference_complete);
  inputArgs.AddLastArg(arguments, options::OPT_typo_correction_limit);
  inputArgs.AddLastArg(arguments, options::OPT_enable_app_extension);
  inputArgs.AddLastArg(arguments, options::OPT_enable_testing);
  inputArgs.AddLastArg(arguments, options::OPT_g_Group);
  inputArgs.AddLastArg(arguments, options::OPT_import_underlying_module);
  inputArgs.AddLastArg(arguments, options::OPT_module_cache_path);
  inputArgs.AddLastArg(arguments, options::OPT_module_link_name);
  inputArgs.AddLastArg(arguments, options::OPT_nostdimport);
  inputArgs.AddLastArg(arguments, options::OPT_parse_stdlib);
  inputArgs.AddLastArg(arguments, options::OPT_resource_dir);
  inputArgs.AddLastArg(arguments, options::OPT_solver_memory_threshold);
  inputArgs.AddLastArg(arguments, options::OPT_value_recursion_threshold);
  inputArgs.AddLastArg(arguments, options::OPT_warn_swift3_objc_inference);
  inputArgs.AddLastArg(arguments, options::OPT_Rpass_EQ);
  inputArgs.AddLastArg(arguments, options::OPT_Rpass_missed_EQ);
  inputArgs.AddLastArg(arguments, options::OPT_suppress_warnings);
  inputArgs.AddLastArg(arguments, options::OPT_profile_generate);
  inputArgs.AddLastArg(arguments, options::OPT_profile_use);
  inputArgs.AddLastArg(arguments, options::OPT_profile_coverage_mapping);
  inputArgs.AddLastArg(arguments, options::OPT_warnings_as_errors);
  inputArgs.AddLastArg(arguments, options::OPT_sanitize_EQ);
  inputArgs.AddLastArg(arguments, options::OPT_sanitize_coverage_EQ);
  inputArgs.AddLastArg(arguments, options::OPT_swift_version);
  inputArgs.AddLastArg(arguments, options::OPT_enforce_exclusivity_EQ);
  inputArgs.AddLastArg(arguments, options::OPT_stats_output_dir);
  inputArgs.AddLastArg(arguments, options::OPT_trace_stats_events);
  inputArgs.AddLastArg(arguments,
                       options::OPT_solver_shrink_unsolved_threshold);
  inputArgs.AddLastArg(arguments, options::OPT_O_Group);
  inputArgs.AddLastArg(arguments, options::OPT_RemoveRuntimeAsserts);
  inputArgs.AddLastArg(arguments, options::OPT_AssumeSingleThreaded);

  // Pass on any build config options
  inputArgs.AddAllArgs(arguments, options::OPT_D);

  // Pass through the values passed to -Xfrontend.
  inputArgs.AddAllArgValues(arguments, options::OPT_Xfrontend);

  // Pass through any subsystem flags.
  inputArgs.AddAllArgs(arguments, options::OPT_Xllvm);
  inputArgs.AddAllArgs(arguments, options::OPT_Xcc);

  const std::string &moduleDocOutputPath =
      output.getAdditionalOutputForType(types::TY_SwiftModuleDocFile);
  if (!moduleDocOutputPath.empty()) {
    arguments.push_back("-emit-module-doc-path");
    arguments.push_back(moduleDocOutputPath.c_str());
  }

  if (llvm::sys::Process::StandardErrHasColors())
    arguments.push_back("-color-diagnostics");

  const std::string &SerializedDiagnosticsPath =
    output.getAdditionalOutputForType(types::TY_SerializedDiagnostics);
  if (!SerializedDiagnosticsPath.empty()) {
    arguments.push_back("-serialize-diagnostics-path");
    arguments.push_back(SerializedDiagnosticsPath.c_str());
  }
}


ToolChain::InvocationInfo
ToolChain::constructInvocation(const CompileJobAction &job,
                               const JobContext &context) const {
  InvocationInfo II{SWIFT_EXECUTABLE_NAME};
  ArgStringList &Arguments = II.Arguments;

  Arguments.push_back("-frontend");

  // Determine the frontend mode option.
  const char *FrontendModeOption = nullptr;
  switch (context.OI.CompilerMode) {
  case OutputInfo::Mode::StandardCompile:
  case OutputInfo::Mode::SingleCompile: {
    switch (context.Output.getPrimaryOutputType()) {
    case types::TY_Object:
      FrontendModeOption = "-c";
      break;
    case types::TY_PCH:
      FrontendModeOption = "-emit-pch";
      break;
    case types::TY_RawSIL:
      FrontendModeOption = "-emit-silgen";
      break;
    case types::TY_SIL:
      FrontendModeOption = "-emit-sil";
      break;
    case types::TY_RawSIB:
      FrontendModeOption = "-emit-sibgen";
      break;
    case types::TY_SIB:
      FrontendModeOption = "-emit-sib";
      break;
    case types::TY_LLVM_IR:
      FrontendModeOption = "-emit-ir";
      break;
    case types::TY_LLVM_BC:
      FrontendModeOption = "-emit-bc";
      break;
    case types::TY_Assembly:
      FrontendModeOption = "-S";
      break;
    case types::TY_SwiftModuleFile:
      // Since this is our primary output, we need to specify the option here.
      FrontendModeOption = "-emit-module";
      break;
    case types::TY_ImportedModules:
      FrontendModeOption = "-emit-imported-modules";
      break;
    case types::TY_IndexData:
      FrontendModeOption = "-typecheck";
      break;
    case types::TY_Remapping:
      FrontendModeOption = "-update-code";
      break;
    case types::TY_Nothing:
      // We were told to output nothing, so get the last mode option and use that.
      if (const Arg *A = context.Args.getLastArg(options::OPT_modes_Group))
        FrontendModeOption = A->getSpelling().data();
      else
        llvm_unreachable("We were told to perform a standard compile, "
                         "but no mode option was passed to the driver.");
      break;
    case types::TY_Swift:
    case types::TY_dSYM:
    case types::TY_AutolinkFile:
    case types::TY_Dependencies:
    case types::TY_SwiftModuleDocFile:
    case types::TY_ClangModuleFile:
    case types::TY_SerializedDiagnostics:
    case types::TY_ObjCHeader:
    case types::TY_Image:
    case types::TY_SwiftDeps:
    case types::TY_ModuleTrace:
    case types::TY_TBD:
    case types::TY_OptRecord:
      llvm_unreachable("Output type can never be primary output.");
    case types::TY_INVALID:
      llvm_unreachable("Invalid type ID");
    }
    break;
  }
  case OutputInfo::Mode::Immediate:
  case OutputInfo::Mode::REPL:
    llvm_unreachable("REPL and immediate modes handled elsewhere");
  }

  assert(FrontendModeOption != nullptr && "No frontend mode option specified!");
  
  Arguments.push_back(FrontendModeOption);

  // Add input arguments.
  switch (context.OI.CompilerMode) {
  case OutputInfo::Mode::StandardCompile: {
    assert(context.InputActions.size() == 1 &&
           "The Swift frontend expects exactly one input (the primary file)!");

    auto *IA = cast<InputAction>(context.InputActions[0]);
    const Arg &PrimaryInputArg = IA->getInputArg();

    if (context.Args.hasArg(options::OPT_driver_use_filelists) ||
        context.getTopLevelInputFiles().size() > TOO_MANY_FILES) {
      Arguments.push_back("-filelist");
      Arguments.push_back(context.getAllSourcesPath());
      Arguments.push_back("-primary-file");
      PrimaryInputArg.render(context.Args, Arguments);
    } else {
      bool FoundPrimaryInput = false;
      for (auto inputPair : context.getTopLevelInputFiles()) {
        if (!types::isPartOfSwiftCompilation(inputPair.first))
          continue;

        // See if this input should be passed with -primary-file.
        if (!FoundPrimaryInput &&
            PrimaryInputArg.getIndex() == inputPair.second->getIndex()) {
          Arguments.push_back("-primary-file");
          FoundPrimaryInput = true;
        }
        Arguments.push_back(inputPair.second->getValue());

        // Forward migrator flags.
        if (auto DataPath = context.Args.getLastArg(options::
                                                    OPT_api_diff_data_file)) {
          Arguments.push_back("-api-diff-data-file");
          Arguments.push_back(DataPath->getValue());
        }
        if (context.Args.hasArg(options::OPT_dump_usr)) {
          Arguments.push_back("-dump-usr");
        }
      }
    }
    break;
  }
  case OutputInfo::Mode::SingleCompile: {
    if (context.Output.getPrimaryOutputType() == types::TY_IndexData) {
      if (Arg *A = context.Args.getLastArg(options::OPT_index_file_path)) {
        Arguments.push_back("-primary-file");
        Arguments.push_back(A->getValue());
      }
    }
    if (context.Args.hasArg(options::OPT_driver_use_filelists) ||
        context.InputActions.size() > TOO_MANY_FILES) {
      Arguments.push_back("-filelist");
      Arguments.push_back(context.getAllSourcesPath());
    } else {
      for (const Action *A : context.InputActions) {
        cast<InputAction>(A)->getInputArg().render(context.Args, Arguments);
      }
    }
    break;
  }

  case OutputInfo::Mode::Immediate:
  case OutputInfo::Mode::REPL:
    llvm_unreachable("REPL and immediate modes handled elsewhere");
  }

  if (context.Args.hasArg(options::OPT_parse_stdlib))
    Arguments.push_back("-disable-objc-attr-requires-foundation-module");

  addCommonFrontendArgs(*this, context.OI, context.Output, context.Args,
                        Arguments);

  // Pass along an -import-objc-header arg, replacing the argument with the name
  // of any input PCH to the current action if one is present.
  if (context.Args.hasArgNoClaim(options::OPT_import_objc_header)) {
    bool ForwardAsIs = true;
    bool bridgingPCHIsEnabled =
        context.Args.hasFlag(options::OPT_enable_bridging_pch,
                             options::OPT_disable_bridging_pch,
                             true);
    bool usePersistentPCH = bridgingPCHIsEnabled &&
        context.Args.hasArg(options::OPT_pch_output_dir);
    if (!usePersistentPCH) {
      for (auto *IJ : context.Inputs) {
        if (!IJ->getOutput().getAnyOutputForType(types::TY_PCH).empty()) {
          Arguments.push_back("-import-objc-header");
          addInputsOfType(Arguments, context.Inputs, types::TY_PCH);
          ForwardAsIs = false;
          break;
        }
      }
    }
    if (ForwardAsIs) {
      context.Args.AddLastArg(Arguments, options::OPT_import_objc_header);
    }
    if (usePersistentPCH) {
      context.Args.AddLastArg(Arguments, options::OPT_pch_output_dir);
      if (context.OI.CompilerMode == OutputInfo::Mode::StandardCompile) {
        // In the 'multiple invocations for each file' mode we don't need to
        // validate the PCH every time, it has been validated with the initial
        // -emit-pch invocation.
        Arguments.push_back("-pch-disable-validation");
      }
    }
  }

  if (context.Args.hasArg(options::OPT_parse_as_library) ||
      context.Args.hasArg(options::OPT_emit_library))
    Arguments.push_back("-parse-as-library");

  context.Args.AddLastArg(Arguments, options::OPT_parse_sil);

  Arguments.push_back("-module-name");
  Arguments.push_back(context.Args.MakeArgString(context.OI.ModuleName));

  const std::string &ModuleOutputPath =
    context.Output.getAdditionalOutputForType(types::ID::TY_SwiftModuleFile);
  if (!ModuleOutputPath.empty()) {
    Arguments.push_back("-emit-module-path");
    Arguments.push_back(ModuleOutputPath.c_str());
  }

  const std::string &ObjCHeaderOutputPath =
    context.Output.getAdditionalOutputForType(types::ID::TY_ObjCHeader);
  if (!ObjCHeaderOutputPath.empty()) {
    assert(context.OI.CompilerMode == OutputInfo::Mode::SingleCompile &&
           "The Swift tool should only emit an Obj-C header in single compile"
           "mode!");

    Arguments.push_back("-emit-objc-header-path");
    Arguments.push_back(ObjCHeaderOutputPath.c_str());
  }

  const std::string &DependenciesPath =
    context.Output.getAdditionalOutputForType(types::TY_Dependencies);
  if (!DependenciesPath.empty()) {
    Arguments.push_back("-emit-dependencies-path");
    Arguments.push_back(DependenciesPath.c_str());
  }

  const std::string &ReferenceDependenciesPath =
    context.Output.getAdditionalOutputForType(types::TY_SwiftDeps);
  if (!ReferenceDependenciesPath.empty()) {
    Arguments.push_back("-emit-reference-dependencies-path");
    Arguments.push_back(ReferenceDependenciesPath.c_str());
  }

  const std::string &LoadedModuleTracePath =
      context.Output.getAdditionalOutputForType(types::TY_ModuleTrace);
  if (!LoadedModuleTracePath.empty()) {
    Arguments.push_back("-emit-loaded-module-trace-path");
    Arguments.push_back(LoadedModuleTracePath.c_str());
  }

  const std::string &TBDPath =
      context.Output.getAdditionalOutputForType(types::TY_TBD);
  if (!TBDPath.empty()) {
    Arguments.push_back("-emit-tbd-path");
    Arguments.push_back(TBDPath.c_str());
  }

  const std::string &OptRecordPath =
      context.Output.getAdditionalOutputForType(types::TY_OptRecord);
  if (!OptRecordPath.empty()) {
    Arguments.push_back("-save-optimization-record-path");
    Arguments.push_back(OptRecordPath.c_str());
  }

  if (context.Args.hasArg(options::OPT_migrate_keep_objc_visibility)) {
    Arguments.push_back("-migrate-keep-objc-visibility");
  }

  const std::string &FixitsPath =
    context.Output.getAdditionalOutputForType(types::TY_Remapping);
  if (!FixitsPath.empty()) {
    Arguments.push_back("-emit-remap-file-path");
    Arguments.push_back(FixitsPath.c_str());
  }

  if (context.OI.numThreads > 0) {
    Arguments.push_back("-num-threads");
    Arguments.push_back(
        context.Args.MakeArgString(Twine(context.OI.numThreads)));
  }

  // Add the output file argument if necessary.
  if (context.Output.getPrimaryOutputType() != types::TY_Nothing) {
    if (context.Args.hasArg(options::OPT_driver_use_filelists) ||
        context.Output.getPrimaryOutputFilenames().size() > TOO_MANY_FILES) {
      Arguments.push_back("-output-filelist");
      Arguments.push_back(context.getTemporaryFilePath("outputs", ""));
      II.FilelistInfo = {Arguments.back(),
                         context.Output.getPrimaryOutputType(),
                         FilelistInfo::Output};
    } else {
      for (auto &FileName : context.Output.getPrimaryOutputFilenames()) {
        Arguments.push_back("-o");
        Arguments.push_back(FileName.c_str());
      }
    }
  }

  if (context.Args.hasArg(options::OPT_embed_bitcode_marker))
    Arguments.push_back("-embed-bitcode-marker");

  if (context.Args.hasArg(options::OPT_index_store_path)) {
    context.Args.AddLastArg(Arguments, options::OPT_index_store_path);
    Arguments.push_back("-index-system-modules");
  }

  return II;
}

ToolChain::InvocationInfo
ToolChain::constructInvocation(const InterpretJobAction &job,
                               const JobContext &context) const {
  assert(context.OI.CompilerMode == OutputInfo::Mode::Immediate);
  ArgStringList Arguments;

  Arguments.push_back("-frontend");
  Arguments.push_back("-interpret");

  assert(context.Inputs.empty() &&
         "The Swift frontend does not expect to be fed any input Jobs!");

  for (const Action *A : context.InputActions) {
    cast<InputAction>(A)->getInputArg().render(context.Args, Arguments);
  }

  if (context.Args.hasArg(options::OPT_parse_stdlib))
    Arguments.push_back("-disable-objc-attr-requires-foundation-module");

  addCommonFrontendArgs(*this, context.OI, context.Output, context.Args,
                        Arguments);
  context.Args.AddLastArg(Arguments, options::OPT_import_objc_header);

  context.Args.AddLastArg(Arguments, options::OPT_parse_sil);

  Arguments.push_back("-module-name");
  Arguments.push_back(context.Args.MakeArgString(context.OI.ModuleName));

  context.Args.AddAllArgs(Arguments, options::OPT_l, options::OPT_framework);

  // The immediate arguments must be last.
  context.Args.AddLastArg(Arguments, options::OPT__DASH_DASH);

  return {SWIFT_EXECUTABLE_NAME, Arguments};
}

ToolChain::InvocationInfo
ToolChain::constructInvocation(const BackendJobAction &job,
                               const JobContext &context) const {
  assert(context.Args.hasArg(options::OPT_embed_bitcode));
  ArgStringList Arguments;

  Arguments.push_back("-frontend");

  // Determine the frontend mode option.
  const char *FrontendModeOption = nullptr;
  switch (context.OI.CompilerMode) {
  case OutputInfo::Mode::StandardCompile:
  case OutputInfo::Mode::SingleCompile: {
    switch (context.Output.getPrimaryOutputType()) {
    case types::TY_Object:
      FrontendModeOption = "-c";
      break;
    case types::TY_LLVM_IR:
      FrontendModeOption = "-emit-ir";
      break;
    case types::TY_LLVM_BC:
      FrontendModeOption = "-emit-bc";
      break;
    case types::TY_Assembly:
      FrontendModeOption = "-S";
      break;
    case types::TY_Nothing:
      // We were told to output nothing, so get the last mode option and use that.
      if (const Arg *A = context.Args.getLastArg(options::OPT_modes_Group))
        FrontendModeOption = A->getSpelling().data();
      else
        llvm_unreachable("We were told to perform a standard compile, "
                         "but no mode option was passed to the driver.");
      break;

    case types::TY_ImportedModules:
    case types::TY_TBD:
    case types::TY_SwiftModuleFile:
    case types::TY_RawSIL:
    case types::TY_RawSIB:
    case types::TY_SIL:
    case types::TY_SIB:
    case types::TY_PCH:
    case types::TY_IndexData:
      llvm_unreachable("Cannot be output from backend job");
    case types::TY_Swift:
    case types::TY_dSYM:
    case types::TY_AutolinkFile:
    case types::TY_Dependencies:
    case types::TY_SwiftModuleDocFile:
    case types::TY_ClangModuleFile:
    case types::TY_SerializedDiagnostics:
    case types::TY_ObjCHeader:
    case types::TY_Image:
    case types::TY_SwiftDeps:
    case types::TY_Remapping:
    case types::TY_ModuleTrace:
    case types::TY_OptRecord:
      llvm_unreachable("Output type can never be primary output.");
    case types::TY_INVALID:
      llvm_unreachable("Invalid type ID");
    }
    break;
  }
  case OutputInfo::Mode::Immediate:
  case OutputInfo::Mode::REPL:
    llvm_unreachable("invalid mode for backend job");
  }

  assert(FrontendModeOption != nullptr && "No frontend mode option specified!");
  
  Arguments.push_back(FrontendModeOption);

  // Add input arguments.
  switch (context.OI.CompilerMode) {
  case OutputInfo::Mode::StandardCompile: {
    assert(context.Inputs.size() == 1 && "The backend expects one input!");
    Arguments.push_back("-primary-file");
    const Job *Cmd = context.Inputs.front();
    Arguments.push_back(
      Cmd->getOutput().getPrimaryOutputFilename().c_str());
    break;
  }
  case OutputInfo::Mode::SingleCompile: {
    assert(context.Inputs.size() == 1 && "The backend expects one input!");
    Arguments.push_back("-primary-file");
    const Job *Cmd = context.Inputs.front();
    
    // In multi-threaded compilation, the backend job must select the correct
    // output file of the compilation job.
    auto OutNames = Cmd->getOutput().getPrimaryOutputFilenames();
    Arguments.push_back(OutNames[job.getInputIndex()].c_str());
    break;
  }
  case OutputInfo::Mode::Immediate:
  case OutputInfo::Mode::REPL:
    llvm_unreachable("invalid mode for backend job");
  }

  // Add flags implied by -embed-bitcode.
  Arguments.push_back("-embed-bitcode");

  // -embed-bitcode only supports a restricted set of flags.
  Arguments.push_back("-target");
  Arguments.push_back(context.Args.MakeArgString(getTriple().str()));

  // Enable address top-byte ignored in the ARM64 backend.
  if (getTriple().getArch() == llvm::Triple::aarch64) {
    Arguments.push_back("-Xllvm");
    Arguments.push_back("-aarch64-use-tbi");
  }

  // Handle the CPU and its preferences.
  context.Args.AddLastArg(Arguments, options::OPT_target_cpu);

  // Enable optimizations, but disable all LLVM-IR-level transformations.
  context.Args.AddLastArg(Arguments, options::OPT_O_Group);
  Arguments.push_back("-disable-llvm-optzns");

  context.Args.AddLastArg(Arguments, options::OPT_parse_stdlib);

  Arguments.push_back("-module-name");
  Arguments.push_back(context.Args.MakeArgString(context.OI.ModuleName));

  // Add the output file argument if necessary.
  if (context.Output.getPrimaryOutputType() != types::TY_Nothing) {
    for (auto &FileName : context.Output.getPrimaryOutputFilenames()) {
      Arguments.push_back("-o");
      Arguments.push_back(FileName.c_str());
    }
  }

  return {SWIFT_EXECUTABLE_NAME, Arguments};
}

ToolChain::InvocationInfo
ToolChain::constructInvocation(const MergeModuleJobAction &job,
                               const JobContext &context) const {
  InvocationInfo II{SWIFT_EXECUTABLE_NAME};
  ArgStringList &Arguments = II.Arguments;

  Arguments.push_back("-frontend");

  Arguments.push_back("-merge-modules");
  Arguments.push_back("-emit-module");

  if (context.Args.hasArg(options::OPT_driver_use_filelists) ||
      context.Inputs.size() > TOO_MANY_FILES) {
    Arguments.push_back("-filelist");
    Arguments.push_back(context.getTemporaryFilePath("inputs", ""));
    II.FilelistInfo = {Arguments.back(), types::TY_SwiftModuleFile,
                       FilelistInfo::Input};

    addInputsOfType(Arguments, context.InputActions, types::TY_SwiftModuleFile);
  } else {
    size_t origLen = Arguments.size();
    (void)origLen;
    addInputsOfType(Arguments, context.Inputs, types::TY_SwiftModuleFile);
    addInputsOfType(Arguments, context.InputActions, types::TY_SwiftModuleFile);
    assert(Arguments.size() - origLen >=
           context.Inputs.size() + context.InputActions.size());
    assert((Arguments.size() - origLen == context.Inputs.size() ||
            !context.InputActions.empty()) &&
           "every input to MergeModule must generate a swiftmodule");
  }

  // Tell all files to parse as library, which is necessary to load them as
  // serialized ASTs.
  Arguments.push_back("-parse-as-library");

  // Merge serialized SIL from partial modules.
  Arguments.push_back("-sil-merge-partial-modules");

  // Disable SIL optimization passes; we've already optimized the code in each
  // partial mode.
  Arguments.push_back("-disable-diagnostic-passes");
  Arguments.push_back("-disable-sil-perf-optzns");

  addCommonFrontendArgs(*this, context.OI, context.Output, context.Args,
                        Arguments);
  context.Args.AddLastArg(Arguments, options::OPT_import_objc_header);

  Arguments.push_back("-module-name");
  Arguments.push_back(context.Args.MakeArgString(context.OI.ModuleName));

  assert(context.Output.getPrimaryOutputType() == types::TY_SwiftModuleFile &&
         "The MergeModule tool only produces swiftmodule files!");

  const std::string &ObjCHeaderOutputPath =
    context.Output.getAdditionalOutputForType(types::TY_ObjCHeader);
  if (!ObjCHeaderOutputPath.empty()) {
    Arguments.push_back("-emit-objc-header-path");
    Arguments.push_back(ObjCHeaderOutputPath.c_str());
  }

  Arguments.push_back("-o");
  Arguments.push_back(
      context.Args.MakeArgString(context.Output.getPrimaryOutputFilename()));

  return II;
}

ToolChain::InvocationInfo
ToolChain::constructInvocation(const ModuleWrapJobAction &job,
                               const JobContext &context) const {
  ArgStringList Arguments;

  Arguments.push_back("-modulewrap");

  addInputsOfType(Arguments, context.Inputs, types::TY_SwiftModuleFile);
  addInputsOfType(Arguments, context.InputActions, types::TY_SwiftModuleFile);
  assert(Arguments.size() == 2 &&
         "ModuleWrap expects exactly one merged swiftmodule as input");

  assert(context.Output.getPrimaryOutputType() == types::TY_Object &&
         "The -modulewrap mode only produces object files");

  Arguments.push_back("-target");
  Arguments.push_back(context.Args.MakeArgString(getTriple().str()));
    
  Arguments.push_back("-o");
  Arguments.push_back(
      context.Args.MakeArgString(context.Output.getPrimaryOutputFilename()));

  return {SWIFT_EXECUTABLE_NAME, Arguments};
}

ToolChain::InvocationInfo
ToolChain::constructInvocation(const REPLJobAction &job,
                               const JobContext &context) const {
  assert(context.Inputs.empty());
  assert(context.InputActions.empty());

  bool useLLDB;

  switch (job.getRequestedMode()) {
  case REPLJobAction::Mode::Integrated:
    useLLDB = false;
    break;
  case REPLJobAction::Mode::RequireLLDB:
    useLLDB = true;
    break;
  case REPLJobAction::Mode::PreferLLDB:
    useLLDB = !findProgramRelativeToSwift("lldb").empty();
    break;
  }

  ArgStringList FrontendArgs;
  addCommonFrontendArgs(*this, context.OI, context.Output, context.Args,
                        FrontendArgs);
  context.Args.AddLastArg(FrontendArgs, options::OPT_import_objc_header);
  context.Args.AddAllArgs(FrontendArgs, options::OPT_l, options::OPT_framework,
                          options::OPT_L);

  if (!useLLDB) {
    FrontendArgs.insert(FrontendArgs.begin(), {"-frontend", "-repl"});
    FrontendArgs.push_back("-module-name");
    FrontendArgs.push_back(context.Args.MakeArgString(context.OI.ModuleName));
    return {SWIFT_EXECUTABLE_NAME, FrontendArgs};
  }

  // Squash important frontend options into a single argument for LLDB.
  std::string SingleArg = "--repl=";
  {
    llvm::raw_string_ostream os(SingleArg);
    Job::printArguments(os, FrontendArgs);
  }

  ArgStringList Arguments;
  Arguments.push_back(context.Args.MakeArgString(std::move(SingleArg)));

  return {"lldb", Arguments};
}


ToolChain::InvocationInfo
ToolChain::constructInvocation(const GenerateDSYMJobAction &job,
                               const JobContext &context) const {
  assert(context.Inputs.size() == 1);
  assert(context.InputActions.empty());
  assert(context.Output.getPrimaryOutputType() == types::TY_dSYM);

  ArgStringList Arguments;

  StringRef inputPath =
      context.Inputs.front()->getOutput().getPrimaryOutputFilename();
  Arguments.push_back(context.Args.MakeArgString(inputPath));

  Arguments.push_back("-o");
  Arguments.push_back(
      context.Args.MakeArgString(context.Output.getPrimaryOutputFilename()));

  return {"dsymutil", Arguments};
}

ToolChain::InvocationInfo
ToolChain::constructInvocation(const VerifyDebugInfoJobAction &job,
                               const JobContext &context) const {
  assert(context.Inputs.size() == 1);
  assert(context.InputActions.empty());

  // This mirrors the clang driver's --verify-debug-info option.
  ArgStringList Arguments;
  Arguments.push_back("--verify");
  Arguments.push_back("--debug-info");
  Arguments.push_back("--eh-frame");
  Arguments.push_back("--quiet");

  StringRef inputPath =
      context.Inputs.front()->getOutput().getPrimaryOutputFilename();
  Arguments.push_back(context.Args.MakeArgString(inputPath));

  return {"dwarfdump", Arguments};
}

ToolChain::InvocationInfo
ToolChain::constructInvocation(const GeneratePCHJobAction &job,
                               const JobContext &context) const {
  assert(context.Inputs.empty());
  assert(context.InputActions.size() == 1);
  assert((!job.isPersistentPCH() &&
            context.Output.getPrimaryOutputType() == types::TY_PCH) ||
         (job.isPersistentPCH() &&
            context.Output.getPrimaryOutputType() == types::TY_Nothing));

  ArgStringList Arguments;

  Arguments.push_back("-frontend");

  addCommonFrontendArgs(*this, context.OI, context.Output, context.Args,
                        Arguments);

  addInputsOfType(Arguments, context.InputActions, types::TY_ObjCHeader);
  context.Args.AddLastArg(Arguments, options::OPT_index_store_path);

  if (job.isPersistentPCH()) {
    Arguments.push_back("-emit-pch");
    Arguments.push_back("-pch-output-dir");
    Arguments.push_back(
      context.Args.MakeArgString(job.getPersistentPCHDir()));
  } else {
    Arguments.push_back("-emit-pch");
    Arguments.push_back("-o");
    Arguments.push_back(
      context.Args.MakeArgString(context.Output.getPrimaryOutputFilename()));
  }

  return {SWIFT_EXECUTABLE_NAME, Arguments};
}

ToolChain::InvocationInfo
ToolChain::constructInvocation(const AutolinkExtractJobAction &job,
                               const JobContext &context) const {
  llvm_unreachable("autolink extraction not implemented for this toolchain");
}

ToolChain::InvocationInfo
ToolChain::constructInvocation(const LinkJobAction &job,
                               const JobContext &context) const {
  llvm_unreachable("linking not implemented for this toolchain");
}

std::string
toolchains::Darwin::findProgramRelativeToSwiftImpl(StringRef name) const {
  StringRef swiftPath = getDriver().getSwiftProgramPath();
  StringRef swiftBinDir = llvm::sys::path::parent_path(swiftPath);

  // See if we're in an Xcode toolchain.
  bool hasToolchain = false;
  llvm::SmallString<128> path{swiftBinDir};
  llvm::sys::path::remove_filename(path); // bin
  llvm::sys::path::remove_filename(path); // usr
  if (llvm::sys::path::extension(path) == ".xctoolchain") {
    hasToolchain = true;
    llvm::sys::path::remove_filename(path); // *.xctoolchain
    llvm::sys::path::remove_filename(path); // Toolchains
    llvm::sys::path::append(path, "usr", "bin");
  }

  StringRef paths[] = { swiftBinDir, path };
  auto pathsRef = llvm::makeArrayRef(paths);
  if (!hasToolchain)
    pathsRef = pathsRef.drop_back();

  auto result = llvm::sys::findProgramByName(name, pathsRef);
  if (result)
    return result.get();
  return {};
}

static void addVersionString(const ArgList &inputArgs, ArgStringList &arguments,
                             unsigned major, unsigned minor, unsigned micro) {
  llvm::SmallString<8> buf;
  llvm::raw_svector_ostream os{buf};
  os << major << '.' << minor << '.' << micro;
  arguments.push_back(inputArgs.MakeArgString(os.str()));
}

/// Runs <code>xcrun -f clang</code> in order to find the location of Clang for
/// the currently active Xcode.
///
/// We get the "currently active" part by passing through the DEVELOPER_DIR
/// environment variable (along with the rest of the environment).
static bool findXcodeClangPath(llvm::SmallVectorImpl<char> &path) {
  assert(path.empty());

  auto xcrunPath = llvm::sys::findProgramByName("xcrun");
  if (!xcrunPath.getError()) {
    const char *args[] = {"-f", "clang", nullptr};
    sys::TaskQueue queue;
    queue.addTask(xcrunPath->c_str(), args, /*Env=*/llvm::None,
                  /*Context=*/nullptr,
                  /*SeparateErrors=*/true);
    queue.execute(nullptr, [&path](sys::ProcessId PID, int returnCode,
                                   StringRef output, StringRef errors,
                                   void *unused) -> sys::TaskFinishedResponse {
      if (returnCode == 0) {
        output = output.rtrim();
        path.append(output.begin(), output.end());
      }
      return sys::TaskFinishedResponse::ContinueExecution;
    });
  }

  return !path.empty();
}

static void addPathEnvironmentVariableIfNeeded(Job::EnvironmentVector &env,
                                               const char *name,
                                               const char *separator,
                                               options::ID optionID,
                                               const ArgList &args,
                                               StringRef extraEntry = "") {
  auto linkPathOptions = args.filtered(optionID);
  if (linkPathOptions.begin() == linkPathOptions.end() && extraEntry.empty())
    return;

  std::string newPaths;
  interleave(linkPathOptions,
             [&](const Arg *arg) { newPaths.append(arg->getValue()); },
             [&] { newPaths.append(separator); });
  if (!extraEntry.empty()) {
    if (!newPaths.empty())
      newPaths.append(separator);
    newPaths.append(extraEntry.data(), extraEntry.size());
  }
  if (auto currentPaths = llvm::sys::Process::GetEnv(name)) {
    newPaths.append(separator);
    newPaths.append(currentPaths.getValue());
  }
  env.emplace_back(name, args.MakeArgString(newPaths));
}

/// Get the runtime library link path, which is platform-specific and found
/// relative to the compiler.
static void getRuntimeLibraryPath(SmallVectorImpl<char> &runtimeLibPath,
                                  const llvm::opt::ArgList &args,
                                  const ToolChain &TC) {
  // FIXME: Duplicated from CompilerInvocation, but in theory the runtime
  // library link path and the standard library module import path don't
  // need to be the same.
  if (const Arg *A = args.getLastArg(options::OPT_resource_dir)) {
    StringRef value = A->getValue();
    runtimeLibPath.append(value.begin(), value.end());
  } else {
    auto programPath = TC.getDriver().getSwiftProgramPath();
    runtimeLibPath.append(programPath.begin(), programPath.end());
    llvm::sys::path::remove_filename(runtimeLibPath); // remove /swift
    llvm::sys::path::remove_filename(runtimeLibPath); // remove /bin
    llvm::sys::path::append(runtimeLibPath, "lib", "swift");
  }
  llvm::sys::path::append(runtimeLibPath,
                          getPlatformNameForTriple(TC.getTriple()));
}

static void getClangLibraryPathOnDarwin(SmallVectorImpl<char> &libPath,
                                        const ArgList &args,
                                        const ToolChain &TC) {
  getRuntimeLibraryPath(libPath, args, TC);
  // Remove platform name.
  llvm::sys::path::remove_filename(libPath);
  llvm::sys::path::append(libPath, "clang", "lib", "darwin");
}

static void getClangLibraryPathOnLinux(SmallVectorImpl<char> &libPath,
                                        const ArgList &args,
                                        const ToolChain &TC) {
  getRuntimeLibraryPath(libPath, args, TC);
  // Remove platform name.
  llvm::sys::path::remove_filename(libPath);
  llvm::sys::path::append(libPath, "clang", "lib", "linux");
}

/// Get the runtime library link path for static linking,
/// which is platform-specific and found relative to the compiler.
static void getRuntimeStaticLibraryPath(SmallVectorImpl<char> &runtimeLibPath,
                                  const llvm::opt::ArgList &args,
                                  const ToolChain &TC) {
  // FIXME: Duplicated from CompilerInvocation, but in theory the runtime
  // library link path and the standard library module import path don't
  // need to be the same.
  if (const Arg *A = args.getLastArg(options::OPT_resource_dir)) {
    StringRef value = A->getValue();
    runtimeLibPath.append(value.begin(), value.end());
  } else {
    auto programPath = TC.getDriver().getSwiftProgramPath();
    runtimeLibPath.append(programPath.begin(), programPath.end());
    llvm::sys::path::remove_filename(runtimeLibPath); // remove /swift
    llvm::sys::path::remove_filename(runtimeLibPath); // remove /bin
    llvm::sys::path::append(runtimeLibPath, "lib", "swift_static");
  }
  llvm::sys::path::append(runtimeLibPath,
                          getPlatformNameForTriple(TC.getTriple()));
}

ToolChain::InvocationInfo
toolchains::Darwin::constructInvocation(const InterpretJobAction &job,
                                        const JobContext &context) const {
  InvocationInfo II = ToolChain::constructInvocation(job, context);

  SmallString<128> runtimeLibraryPath;
  getRuntimeLibraryPath(runtimeLibraryPath, context.Args, *this);

  addPathEnvironmentVariableIfNeeded(II.ExtraEnvironment, "DYLD_LIBRARY_PATH",
                                     ":", options::OPT_L, context.Args,
                                     runtimeLibraryPath);
  addPathEnvironmentVariableIfNeeded(II.ExtraEnvironment, "DYLD_FRAMEWORK_PATH",
                                     ":", options::OPT_F, context.Args);
  // FIXME: Add options::OPT_Fsystem paths to DYLD_FRAMEWORK_PATH as well.
  return II;
}

static StringRef
getDarwinLibraryNameSuffixForTriple(const llvm::Triple &triple) {
  switch (getDarwinPlatformKind(triple)) {
  case DarwinPlatformKind::MacOS:
    return "osx";
  case DarwinPlatformKind::IPhoneOS:
    return "ios";
  case DarwinPlatformKind::IPhoneOSSimulator:
    return "iossim";
  case DarwinPlatformKind::TvOS:
    return "tvos";
  case DarwinPlatformKind::TvOSSimulator:
    return "tvossim";
  case DarwinPlatformKind::WatchOS:
    return "watchos";
  case DarwinPlatformKind::WatchOSSimulator:
    return "watchossim";
  }
  llvm_unreachable("Unsupported Darwin platform");
}

static std::string
getSanitizerRuntimeLibNameForDarwin(StringRef Sanitizer,
                                    const llvm::Triple &Triple,
                                    bool shared = true) {
  return (Twine("libclang_rt.")
      + Sanitizer + "_"
      + getDarwinLibraryNameSuffixForTriple(Triple)
      + (shared ? "_dynamic.dylib" : ".a")).str();
}

static std::string
getSanitizerRuntimeLibNameForLinux(StringRef Sanitizer, const llvm::Triple &Triple) {
  return (Twine("libclang_rt.") + Sanitizer + "-" +
                       Triple.getArchName() + ".a").str();
}

bool toolchains::Darwin::sanitizerRuntimeLibExists(
    const ArgList &args, StringRef sanitizer) const {
  SmallString<128> sanitizerLibPath;
  getClangLibraryPathOnDarwin(sanitizerLibPath, args, *this);
  llvm::sys::path::append(sanitizerLibPath,
      getSanitizerRuntimeLibNameForDarwin(sanitizer, this->getTriple()));
  return llvm::sys::fs::exists(sanitizerLibPath.str());
}

bool toolchains::GenericUnix::sanitizerRuntimeLibExists(
    const ArgList &args, StringRef sanitizer) const {
  SmallString<128> sanitizerLibPath;
  getClangLibraryPathOnLinux(sanitizerLibPath, args, *this);
  llvm::sys::path::append(sanitizerLibPath,
      getSanitizerRuntimeLibNameForLinux(sanitizer, this->getTriple()));
  return llvm::sys::fs::exists(sanitizerLibPath.str());
}


static void
addLinkRuntimeLibForDarwin(const ArgList &Args, ArgStringList &Arguments,
                           StringRef DarwinLibName, bool AddRPath,
                           const ToolChain &TC) {
  SmallString<128> ClangLibraryPath;
  getClangLibraryPathOnDarwin(ClangLibraryPath, Args, TC);

  SmallString<128> P(ClangLibraryPath);
  llvm::sys::path::append(P, DarwinLibName);
  Arguments.push_back(Args.MakeArgString(P));

  // Adding the rpaths might negatively interact when other rpaths are involved,
  // so we should make sure we add the rpaths last, after all user-specified
  // rpaths. This is currently true from this place, but we need to be
  // careful if this function is ever called before user's rpaths are emitted.
  if (AddRPath) {
    assert(DarwinLibName.endswith(".dylib") && "must be a dynamic library");

    // Add @executable_path to rpath to support having the dylib copied with
    // the executable.
    Arguments.push_back("-rpath");
    Arguments.push_back("@executable_path");

    // Add the path to the resource dir to rpath to support using the dylib
    // from the default location without copying.
    Arguments.push_back("-rpath");
    Arguments.push_back(Args.MakeArgString(ClangLibraryPath));
  }
}

static void
addLinkRuntimeLibForLinux(const ArgList &Args, ArgStringList &Arguments,
                           StringRef LinuxLibName,
                           const ToolChain &TC) {
  SmallString<128> Dir;
  getRuntimeLibraryPath(Dir, Args, TC);
  // Remove platform name.
  llvm::sys::path::remove_filename(Dir);
  llvm::sys::path::append(Dir, "clang", "lib", "linux");
  SmallString<128> P(Dir);
  llvm::sys::path::append(P, LinuxLibName);
  Arguments.push_back(Args.MakeArgString(P));
}

static void
addLinkSanitizerLibArgsForDarwin(const ArgList &Args,
                                 ArgStringList &Arguments,
                                 StringRef Sanitizer,
                                 const ToolChain &TC,
                                 bool shared = true
                                 ) {
  // Sanitizer runtime libraries requires C++.
  Arguments.push_back("-lc++");
  // Add explicit dependency on -lc++abi, as -lc++ doesn't re-export
  // all RTTI-related symbols that are used.
  Arguments.push_back("-lc++abi");

  addLinkRuntimeLibForDarwin(Args, Arguments,
      getSanitizerRuntimeLibNameForDarwin(Sanitizer, TC.getTriple(), shared),
      /*AddRPath=*/ shared, TC);
}

static void
addLinkSanitizerLibArgsForLinux(const ArgList &Args,
                                 ArgStringList &Arguments,
                                 StringRef Sanitizer, const ToolChain &TC) {
  addLinkRuntimeLibForLinux(Args, Arguments,
      getSanitizerRuntimeLibNameForLinux(Sanitizer, TC.getTriple()), TC);

  // Code taken from
  // https://github.com/apple/swift-clang/blob/ab3cbe7/lib/Driver/Tools.cpp#L3264-L3276
  // There's no libpthread or librt on RTEMS.
  if (TC.getTriple().getOS() != llvm::Triple::RTEMS) {
    Arguments.push_back("-lpthread");
    Arguments.push_back("-lrt");
  }
  Arguments.push_back("-lm");

  // There's no libdl on FreeBSD or RTEMS.
  if (TC.getTriple().getOS() != llvm::Triple::FreeBSD &&
      TC.getTriple().getOS() != llvm::Triple::RTEMS)
    Arguments.push_back("-ldl");
}

ToolChain::InvocationInfo
toolchains::Darwin::constructInvocation(const LinkJobAction &job,
                                        const JobContext &context) const {
  assert(context.Output.getPrimaryOutputType() == types::TY_Image &&
         "Invalid linker output type.");

  if (context.Args.hasFlag(options::OPT_static_executable,
                           options::OPT_no_static_executable,
                           false)) {
    llvm::report_fatal_error("-static-executable is not supported on Darwin");
  }

  const Driver &D = getDriver();
  const llvm::Triple &Triple = getTriple();

  // Configure the toolchain.
  // By default, use the system `ld` to link.
  const char *LD = "ld";
  if (const Arg *A = context.Args.getLastArg(options::OPT_tools_directory)) {
    StringRef toolchainPath(A->getValue());

    // If there is a 'ld' in the toolchain folder, use that instead.
    if (auto toolchainLD = llvm::sys::findProgramByName("ld", {toolchainPath})) {
      LD = context.Args.MakeArgString(toolchainLD.get());
    }
  }

  InvocationInfo II = {LD};
  ArgStringList &Arguments = II.Arguments;

  if (context.Args.hasArg(options::OPT_driver_use_filelists) ||
      context.Inputs.size() > TOO_MANY_FILES) {
    Arguments.push_back("-filelist");
    Arguments.push_back(context.getTemporaryFilePath("inputs", "LinkFileList"));
    II.FilelistInfo = {Arguments.back(), types::TY_Object, FilelistInfo::Input};
  } else {
    addPrimaryInputsOfType(Arguments, context.Inputs, types::TY_Object);
  }

  addInputsOfType(Arguments, context.InputActions, types::TY_Object);

  if (context.OI.CompilerMode == OutputInfo::Mode::SingleCompile)
    addInputsOfType(Arguments, context.Inputs, types::TY_SwiftModuleFile,
                    "-add_ast_path");
  else
    addPrimaryInputsOfType(Arguments, context.Inputs,
                           types::TY_SwiftModuleFile, "-add_ast_path");

  // Add all .swiftmodule file inputs as arguments, preceded by the
  // "-add_ast_path" linker option.
  addInputsOfType(Arguments, context.InputActions, types::TY_SwiftModuleFile,
                  "-add_ast_path");

  switch (job.getKind()) {
  case LinkKind::None:
    llvm_unreachable("invalid link kind");
  case LinkKind::Executable:
    // The default for ld; no extra flags necessary.
    break;
  case LinkKind::DynamicLibrary:
    Arguments.push_back("-dylib");
    break;
  }

  assert(Triple.isOSDarwin());

  // FIXME: If we used Clang as a linker instead of going straight to ld,
  // we wouldn't have to replicate Clang's logic here.
  bool wantsObjCRuntime = false;
  if (Triple.isiOS())
    wantsObjCRuntime = Triple.isOSVersionLT(9);
  else if (Triple.isMacOSX())
    wantsObjCRuntime = Triple.isMacOSXVersionLT(10, 11);

  if (context.Args.hasFlag(options::OPT_link_objc_runtime,
                           options::OPT_no_link_objc_runtime,
                           /*Default=*/wantsObjCRuntime)) {
    llvm::SmallString<128> ARCLiteLib(D.getSwiftProgramPath());
    llvm::sys::path::remove_filename(ARCLiteLib); // 'swift'
    llvm::sys::path::remove_filename(ARCLiteLib); // 'bin'
    llvm::sys::path::append(ARCLiteLib, "lib", "arc");

    if (!llvm::sys::fs::is_directory(ARCLiteLib)) {
      // If we don't have a 'lib/arc/' directory, find the "arclite" library
      // relative to the Clang in the active Xcode.
      ARCLiteLib.clear();
      if (findXcodeClangPath(ARCLiteLib)) {
        llvm::sys::path::remove_filename(ARCLiteLib); // 'clang'
        llvm::sys::path::remove_filename(ARCLiteLib); // 'bin'
        llvm::sys::path::append(ARCLiteLib, "lib", "arc");
      }
    }

    if (!ARCLiteLib.empty()) {
      llvm::sys::path::append(ARCLiteLib, "libarclite_");
      ARCLiteLib += getPlatformNameForTriple(Triple);
      ARCLiteLib += ".a";

      Arguments.push_back("-force_load");
      Arguments.push_back(context.Args.MakeArgString(ARCLiteLib));

      // Arclite depends on CoreFoundation.
      Arguments.push_back("-framework");
      Arguments.push_back("CoreFoundation");
    } else {
      // FIXME: We should probably diagnose this, but this is not a place where
      // we can emit diagnostics. Silently ignore it for now.
    }
  }

  context.Args.AddAllArgValues(Arguments, options::OPT_Xlinker);
  context.Args.AddAllArgs(Arguments, options::OPT_linker_option_Group);
  for (const Arg *arg : context.Args.filtered(options::OPT_F,
                                              options::OPT_Fsystem)) {
    Arguments.push_back("-F");
    Arguments.push_back(arg->getValue());
  }

  if (context.Args.hasArg(options::OPT_enable_app_extension)) {
    // Keep this string fixed in case the option used by the
    // compiler itself changes.
    Arguments.push_back("-application_extension");
  }

  // Linking sanitizers will add rpaths, which might negatively interact when
  // other rpaths are involved, so we should make sure we add the rpaths after
  // all user-specified rpaths.
  if (context.OI.SelectedSanitizers & SanitizerKind::Address)
    addLinkSanitizerLibArgsForDarwin(context.Args, Arguments, "asan", *this);

  if (context.OI.SelectedSanitizers & SanitizerKind::Thread)
    addLinkSanitizerLibArgsForDarwin(context.Args, Arguments, "tsan", *this);

  // Only link in libFuzzer for executables.
  if (job.getKind() == LinkKind::Executable &&
      (context.OI.SelectedSanitizers & SanitizerKind::Fuzzer))
    addLinkSanitizerLibArgsForDarwin(
        context.Args, Arguments, "fuzzer", *this, /*shared=*/false);

  if (context.Args.hasArg(options::OPT_embed_bitcode,
                          options::OPT_embed_bitcode_marker)) {
    Arguments.push_back("-bitcode_bundle");
  }

  if (!context.OI.SDKPath.empty()) {
    Arguments.push_back("-syslibroot");
    Arguments.push_back(context.Args.MakeArgString(context.OI.SDKPath));
  }

  Arguments.push_back("-lobjc");
  Arguments.push_back("-lSystem");

  Arguments.push_back("-arch");
  Arguments.push_back(context.Args.MakeArgString(getTriple().getArchName()));

  // Add the runtime library link path, which is platform-specific and found
  // relative to the compiler.
  SmallString<128> RuntimeLibPath;
  getRuntimeLibraryPath(RuntimeLibPath, context.Args, *this);

  // Link the standard library.
  Arguments.push_back("-L");
  if (context.Args.hasFlag(options::OPT_static_stdlib,
                            options::OPT_no_static_stdlib,
                            false)) {
    SmallString<128> StaticRuntimeLibPath;
    getRuntimeStaticLibraryPath(StaticRuntimeLibPath, context.Args, *this);
    Arguments.push_back(context.Args.MakeArgString(StaticRuntimeLibPath));
    Arguments.push_back("-lc++");
    Arguments.push_back("-framework");
    Arguments.push_back("Foundation");
    Arguments.push_back("-force_load_swift_libs");
  } else {
    Arguments.push_back(context.Args.MakeArgString(RuntimeLibPath));
    // FIXME: We probably shouldn't be adding an rpath here unless we know ahead
    // of time the standard library won't be copied. SR-1967
    Arguments.push_back("-rpath");
    Arguments.push_back(context.Args.MakeArgString(RuntimeLibPath));
  }

  if (context.Args.hasArg(options::OPT_profile_generate)) {
    SmallString<128> LibProfile(RuntimeLibPath);
    llvm::sys::path::remove_filename(LibProfile); // remove platform name
    llvm::sys::path::append(LibProfile, "clang", "lib", "darwin");

    StringRef RT;
    if (Triple.isiOS()) {
      if (Triple.isTvOS())
        RT = "tvos";
      else
        RT = "ios";
    } else if (Triple.isWatchOS()) {
      RT = "watchos";
    } else {
      assert(Triple.isMacOSX());
      RT = "osx";
    }

    StringRef Sim;
    if (tripleIsAnySimulator(Triple)) {
      Sim = "sim";
    }

    llvm::sys::path::append(LibProfile,
                            "libclang_rt.profile_" + RT + Sim + ".a");

    // FIXME: Continue accepting the old path for simulator libraries for now.
    if (!Sim.empty() && !llvm::sys::fs::exists(LibProfile)) {
      llvm::sys::path::remove_filename(LibProfile);
      llvm::sys::path::append(LibProfile,
                              "libclang_rt.profile_" + RT + ".a");
    }

    Arguments.push_back(context.Args.MakeArgString(LibProfile));
  }

  // FIXME: Properly handle deployment targets.
  assert(Triple.isiOS() || Triple.isWatchOS() || Triple.isMacOSX());
  if (Triple.isiOS()) {
    bool isiOSSimulator = tripleIsiOSSimulator(Triple);
    if (Triple.isTvOS()) {
      if (isiOSSimulator)
        Arguments.push_back("-tvos_simulator_version_min");
      else
        Arguments.push_back("-tvos_version_min");
    } else {
      if (isiOSSimulator)
        Arguments.push_back("-ios_simulator_version_min");
      else
        Arguments.push_back("-iphoneos_version_min");
    }
    unsigned major, minor, micro;
    Triple.getiOSVersion(major, minor, micro);
    addVersionString(context.Args, Arguments, major, minor, micro);
  } else if (Triple.isWatchOS()) {
    if (tripleIsWatchSimulator(Triple))
      Arguments.push_back("-watchos_simulator_version_min");
    else
      Arguments.push_back("-watchos_version_min");
    unsigned major, minor, micro;
    Triple.getOSVersion(major, minor, micro);
    addVersionString(context.Args, Arguments, major, minor, micro);
  } else {
    Arguments.push_back("-macosx_version_min");
    unsigned major, minor, micro;
    Triple.getMacOSXVersion(major, minor, micro);
    addVersionString(context.Args, Arguments, major, minor, micro);
  }

  Arguments.push_back("-no_objc_category_merging");

  // This should be the last option, for convenience in checking output.
  Arguments.push_back("-o");
  Arguments.push_back(context.Output.getPrimaryOutputFilename().c_str());

  return II;
}

ToolChain::InvocationInfo
toolchains::GenericUnix::constructInvocation(const InterpretJobAction &job,
                                             const JobContext &context) const {
  InvocationInfo II = ToolChain::constructInvocation(job, context);

  SmallString<128> runtimeLibraryPath;
  getRuntimeLibraryPath(runtimeLibraryPath, context.Args, *this);

  addPathEnvironmentVariableIfNeeded(II.ExtraEnvironment, "LD_LIBRARY_PATH",
                                     ":", options::OPT_L, context.Args,
                                     runtimeLibraryPath);
  return II;
}


ToolChain::InvocationInfo
toolchains::GenericUnix::constructInvocation(const AutolinkExtractJobAction &job,
                                             const JobContext &context) const {
  assert(context.Output.getPrimaryOutputType() == types::TY_AutolinkFile);

  ArgStringList Arguments;
  addPrimaryInputsOfType(Arguments, context.Inputs, types::TY_Object);
  addInputsOfType(Arguments, context.InputActions, types::TY_Object);

  Arguments.push_back("-o");
  Arguments.push_back(
      context.Args.MakeArgString(context.Output.getPrimaryOutputFilename()));

  return {"swift-autolink-extract", Arguments};
}

std::string toolchains::GenericUnix::getDefaultLinker() const {
  switch(getTriple().getArch()) {
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    // BFD linker has issues wrt relocation of the protocol conformance
    // section on these targets, it also generates COPY relocations for
    // final executables, as such, unless specified, we default to gold
    // linker.
    return "gold";
  case llvm::Triple::x86_64:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
  case llvm::Triple::systemz:
    // BFD linker has issues wrt relocations against protected symbols.
    return "gold";
  default:
    // Otherwise, use the default BFD linker.
    return "";
  }
}

std::string toolchains::GenericUnix::getTargetForLinker() const {
  return getTriple().str();
}

bool toolchains::GenericUnix::shouldProvideRPathToLinker() const {
  return true;
}

ToolChain::InvocationInfo
toolchains::GenericUnix::constructInvocation(const LinkJobAction &job,
                                             const JobContext &context) const {
  assert(context.Output.getPrimaryOutputType() == types::TY_Image &&
         "Invalid linker output type.");

  ArgStringList Arguments;

  switch (job.getKind()) {
  case LinkKind::None:
    llvm_unreachable("invalid link kind");
  case LinkKind::Executable:
    // Default case, nothing extra needed.
    break;
  case LinkKind::DynamicLibrary:
    Arguments.push_back("-shared");
    break;
  }

  // Select the linker to use.
  std::string Linker;
  if (const Arg *A = context.Args.getLastArg(options::OPT_use_ld)) {
    Linker = A->getValue();
  } else {
    Linker = getDefaultLinker();
  }
  if (!Linker.empty()) {
#if defined(__HAIKU__)
    // For now, passing -fuse-ld on Haiku doesn't work as swiftc doesn't recognise
    // it. Passing -use-ld= as the argument works fine.
    Arguments.push_back(context.Args.MakeArgString("-use-ld=" + Linker));
#else
    Arguments.push_back(context.Args.MakeArgString("-fuse-ld=" + Linker));
#endif
  }

  // Configure the toolchain.
  // By default, use the system clang++ to link.
  const char * Clang = "clang++";
  if (const Arg *A = context.Args.getLastArg(options::OPT_tools_directory)) {
    StringRef toolchainPath(A->getValue());

    // If there is a clang in the toolchain folder, use that instead.
    if (auto toolchainClang = llvm::sys::findProgramByName("clang++", {toolchainPath})) {
      Clang = context.Args.MakeArgString(toolchainClang.get());
    }

    // Look for binutils in the toolchain folder.
    Arguments.push_back("-B");
    Arguments.push_back(context.Args.MakeArgString(A->getValue()));
  }

  if (getTriple().getOS() == llvm::Triple::Linux &&
      job.getKind() == LinkKind::Executable) {
    Arguments.push_back("-pie");
  }

  std::string Target = getTargetForLinker();
  if (!Target.empty()) {
    Arguments.push_back("-target");
    Arguments.push_back(context.Args.MakeArgString(Target));
  }

  bool staticExecutable = false;
  bool staticStdlib = false;

  if (context.Args.hasFlag(options::OPT_static_executable,
                           options::OPT_no_static_executable,
                           false)) {
    staticExecutable = true;
  } else if (context.Args.hasFlag(options::OPT_static_stdlib,
                                options::OPT_no_static_stdlib,
                                false)) {
    staticStdlib = true;
  }

  SmallString<128> SharedRuntimeLibPath;
  getRuntimeLibraryPath(SharedRuntimeLibPath, context.Args, *this);

  SmallString<128> StaticRuntimeLibPath;
  getRuntimeStaticLibraryPath(StaticRuntimeLibPath, context.Args, *this);

  // Add the runtime library link path, which is platform-specific and found
  // relative to the compiler.
  if (!(staticExecutable || staticStdlib) && shouldProvideRPathToLinker()) {
    // FIXME: We probably shouldn't be adding an rpath here unless we know
    //        ahead of time the standard library won't be copied.
    Arguments.push_back("-Xlinker");
    Arguments.push_back("-rpath");
    Arguments.push_back("-Xlinker");
    Arguments.push_back(context.Args.MakeArgString(SharedRuntimeLibPath));
  }

  SmallString<128> swiftrtPath = SharedRuntimeLibPath;
  llvm::sys::path::append(swiftrtPath,
                          swift::getMajorArchitectureName(getTriple()));
  llvm::sys::path::append(swiftrtPath, "swiftrt.o");
  Arguments.push_back(context.Args.MakeArgString(swiftrtPath));

  addPrimaryInputsOfType(Arguments, context.Inputs, types::TY_Object);
  addInputsOfType(Arguments, context.InputActions, types::TY_Object);

  for (const Arg *arg : context.Args.filtered(options::OPT_F,
                                              options::OPT_Fsystem)) {
    if (arg->getOption().matches(options::OPT_Fsystem))
      Arguments.push_back("-iframework");
    else
      Arguments.push_back(context.Args.MakeArgString(arg->getSpelling()));
    Arguments.push_back(arg->getValue());
  }

  if (!context.OI.SDKPath.empty()) {
    Arguments.push_back("--sysroot");
    Arguments.push_back(context.Args.MakeArgString(context.OI.SDKPath));
  }

  // Add any autolinking scripts to the arguments
  for (const Job *Cmd : context.Inputs) {
    auto &OutputInfo = Cmd->getOutput();
    if (OutputInfo.getPrimaryOutputType() == types::TY_AutolinkFile)
      Arguments.push_back(context.Args.MakeArgString(
        Twine("@") + OutputInfo.getPrimaryOutputFilename()));
  }

  // Link the standard library.
  Arguments.push_back("-L");

  if (staticExecutable) {
    Arguments.push_back(context.Args.MakeArgString(StaticRuntimeLibPath));

    SmallString<128> linkFilePath = StaticRuntimeLibPath;
    llvm::sys::path::append(linkFilePath, "static-executable-args.lnk");
    auto linkFile = linkFilePath.str();

    if (llvm::sys::fs::is_regular_file(linkFile)) {
      Arguments.push_back(context.Args.MakeArgString(Twine("@") + linkFile));
    } else {
      llvm::report_fatal_error("-static-executable not supported on this platform");
    }
  }
  else if (staticStdlib) {
    Arguments.push_back(context.Args.MakeArgString(StaticRuntimeLibPath));

    SmallString<128> linkFilePath = StaticRuntimeLibPath;
    llvm::sys::path::append(linkFilePath, "static-stdlib-args.lnk");
    auto linkFile = linkFilePath.str();
    if (llvm::sys::fs::is_regular_file(linkFile)) {
      Arguments.push_back(context.Args.MakeArgString(Twine("@") + linkFile));
    } else {
      llvm::report_fatal_error(linkFile + " not found");
    }
  }
  else {
    Arguments.push_back(context.Args.MakeArgString(SharedRuntimeLibPath));
    Arguments.push_back("-lswiftCore");
  }
  
  // Explicitly pass the target to the linker
  Arguments.push_back(context.Args.MakeArgString("--target=" + getTriple().str()));

  if (getTriple().getOS() == llvm::Triple::Linux) {
    //Make sure we only add SanitizerLibs for executables
    if (job.getKind() == LinkKind::Executable) {
      if (context.OI.SelectedSanitizers & SanitizerKind::Address)
        addLinkSanitizerLibArgsForLinux(context.Args, Arguments, "asan", *this);

      if (context.OI.SelectedSanitizers & SanitizerKind::Thread)
        addLinkSanitizerLibArgsForLinux(context.Args, Arguments, "tsan", *this);

      if (context.OI.SelectedSanitizers & SanitizerKind::Fuzzer)
        addLinkRuntimeLibForLinux(context.Args, Arguments,
            getSanitizerRuntimeLibNameForLinux(
                "fuzzer", this->getTriple()), *this);
    }
  }

  if (context.Args.hasArg(options::OPT_profile_generate)) {
    SmallString<128> LibProfile(SharedRuntimeLibPath);
    llvm::sys::path::remove_filename(LibProfile); // remove platform name
    llvm::sys::path::append(LibProfile, "clang", "lib");

    llvm::sys::path::append(LibProfile, getTriple().getOSName(),
                            Twine("libclang_rt.profile-") +
                              getTriple().getArchName() +
                              ".a");
    Arguments.push_back(context.Args.MakeArgString(LibProfile));
    Arguments.push_back(context.Args.MakeArgString(
        Twine("-u", llvm::getInstrProfRuntimeHookVarName())));
  }

  context.Args.AddAllArgs(Arguments, options::OPT_Xlinker);
  context.Args.AddAllArgs(Arguments, options::OPT_linker_option_Group);

  // This should be the last option, for convenience in checking output.
  Arguments.push_back("-o");
  Arguments.push_back(context.Output.getPrimaryOutputFilename().c_str());

  return {Clang, Arguments};
}

std::string
toolchains::Android::getTargetForLinker() const {
  // Explicitly set the linker target to "androideabi", as opposed to the
  // llvm::Triple representation of "armv7-none-linux-android".
  // This is the only ABI we currently support for Android.
  assert(
    getTriple().getArch() == llvm::Triple::arm &&
    getTriple().getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v7 &&
    "Only armv7 targets are supported for Android");
  return "armv7-none-linux-androideabi";
}

bool toolchains::Android::shouldProvideRPathToLinker() const {
  return false;
}

std::string toolchains::Cygwin::getDefaultLinker() const {
  // Cygwin uses the default BFD linker, even on ARM.
  return "";
}

std::string toolchains::Cygwin::getTargetForLinker() const {
  return "";
}

