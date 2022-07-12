//===--- TestOptions.cpp --------------------------------------------------===//
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

#include "TestOptions.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/raw_ostream.h"

using namespace sourcekitd_test;
using llvm::StringRef;

namespace {

// Create enum with OPT_xxx values for each option in Options.td.
enum Opt {
  OPT_INVALID = 0,
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELP, META, VALUES)                                             \
  OPT_##ID,
#include "Options.inc"
  LastOption
#undef OPTION
};

// Create prefix string literals used in Options.td.
#define PREFIX(NAME, VALUE) const char *const NAME[] = VALUE;
#include "Options.inc"
#undef PREFIX

// Create table mapping all options defined in Options.td.
static const llvm::opt::OptTable::Info InfoTable[] = {
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELPTEXT, METAVAR, VALUES)                                      \
  {PREFIX,      NAME,      HELPTEXT,                                           \
   METAVAR,     OPT_##ID,  llvm::opt::Option::KIND##Class,                     \
   PARAM,       FLAGS,     OPT_##GROUP,                                        \
   OPT_##ALIAS, ALIASARGS, VALUES},
#include "Options.inc"
#undef OPTION
};

// Create OptTable class for parsing actual command line arguments
class TestOptTable : public llvm::opt::OptTable {
public:
  TestOptTable() : OptTable(InfoTable, llvm::array_lengthof(InfoTable)){}
};

} // end anonymous namespace

static std::pair<unsigned, unsigned> parseLineCol(StringRef LineCol) {
  unsigned Line, Col;
  size_t ColonIdx = LineCol.find(':');
  if (ColonIdx == StringRef::npos) {
    llvm::errs() << "wrong pos format, it should be '<line>:<column>'\n";
    exit(1);
  }
  if (LineCol.substr(0, ColonIdx).getAsInteger(10, Line)) {
    llvm::errs() << "wrong pos format, it should be '<line>:<column>'\n";
    exit(1);
  }
  if (LineCol.substr(ColonIdx+1).getAsInteger(10, Col)) {
    llvm::errs() << "wrong pos format, it should be '<line>:<column>'\n";
    exit(1);
  }

  if (Line == 0 || Col == 0) {
    llvm::errs() << "wrong pos format, line/col should start from 1\n";
    exit(1);
  }

  return { Line, Col };
}

bool TestOptions::parseArgs(llvm::ArrayRef<const char *> Args) {
  if (Args.empty())
    return false;

  // Parse command line options using Options.td
  TestOptTable Table;
  unsigned MissingIndex;
  unsigned MissingCount;
  llvm::opt::InputArgList ParsedArgs =
      Table.ParseArgs(Args, MissingIndex, MissingCount);
  if (MissingCount) {
    llvm::errs() << "error: missing argument value for '"
        << ParsedArgs.getArgString(MissingIndex) << "', expected "
        << MissingCount << " argument(s)\n";
    return true;
  }

  for (auto InputArg : ParsedArgs) {
    switch (InputArg->getOption().getID()) {
    case OPT_req:
      Request = llvm::StringSwitch<SourceKitRequest>(InputArg->getValue())
        .Case("version", SourceKitRequest::ProtocolVersion)
        .Case("demangle", SourceKitRequest::DemangleNames)
        .Case("mangle", SourceKitRequest::MangleSimpleClasses)
        .Case("index", SourceKitRequest::Index)
        .Case("complete", SourceKitRequest::CodeComplete)
        .Case("complete.open", SourceKitRequest::CodeCompleteOpen)
        .Case("complete.close", SourceKitRequest::CodeCompleteClose)
        .Case("complete.update", SourceKitRequest::CodeCompleteUpdate)
        .Case("complete.cache.ondisk", SourceKitRequest::CodeCompleteCacheOnDisk)
        .Case("complete.setpopularapi", SourceKitRequest::CodeCompleteSetPopularAPI)
        .Case("cursor", SourceKitRequest::CursorInfo)
        .Case("related-idents", SourceKitRequest::RelatedIdents)
        .Case("syntax-map", SourceKitRequest::SyntaxMap)
        .Case("structure", SourceKitRequest::Structure)
        .Case("format", SourceKitRequest::Format)
        .Case("expand-placeholder", SourceKitRequest::ExpandPlaceholder)
        .Case("doc-info", SourceKitRequest::DocInfo)
        .Case("sema", SourceKitRequest::SemanticInfo)
        .Case("interface-gen", SourceKitRequest::InterfaceGen)
        .Case("interface-gen-open", SourceKitRequest::InterfaceGenOpen)
        .Case("find-usr", SourceKitRequest::FindUSR)
        .Case("find-interface", SourceKitRequest::FindInterfaceDoc)
        .Case("open", SourceKitRequest::Open)
        .Case("close", SourceKitRequest::Close)
        .Case("edit", SourceKitRequest::Edit)
        .Case("print-annotations", SourceKitRequest::PrintAnnotations)
        .Case("print-diags", SourceKitRequest::PrintDiags)
        .Case("extract-comment", SourceKitRequest::ExtractComment)
        .Case("module-groups", SourceKitRequest::ModuleGroups)
        .Case("range", SourceKitRequest::RangeInfo)
        .Case("syntactic-rename", SourceKitRequest::SyntacticRename)
        .Case("find-rename-ranges", SourceKitRequest::FindRenameRanges)
        .Case("find-local-rename-ranges", SourceKitRequest::FindLocalRenameRanges)
        .Case("translate", SourceKitRequest::NameTranslation)
        .Case("local-rename", SourceKitRequest::LocalRename)
        .Case("extract-expr", SourceKitRequest::ExtractExpr)
        .Case("extract-repeated", SourceKitRequest::ExtractRepeatedExpr)
        .Case("extract-func", SourceKitRequest::ExtractFunction)
        .Case("fill-stub", SourceKitRequest::FillProtocolStub)
        .Case("expand-default", SourceKitRequest::ExpandDefault)
        .Case("localize-string", SourceKitRequest::LocalizeString)
        .Case("markup-xml", SourceKitRequest::MarkupToXML)
        .Case("stats", SourceKitRequest::Statistics)
        .Default(SourceKitRequest::None);

      if (Request == SourceKitRequest::None) {
        llvm::errs() << "error: invalid request, expected one of "
            << "version/demangle/mangle/index/complete/complete.open/complete.cursor/"
               "complete.update/complete.cache.ondisk/complete.cache.setpopularapi/"
               "cursor/related-idents/syntax-map/structure/format/expand-placeholder/"
               "doc-info/sema/interface-gen/interface-gen-openfind-usr/find-interface/"
               "open/close/edit/print-annotations/print-diags/extract-comment/module-groups/"
               "range/syntactic-rename/find-rename-ranges/translate/markup-xml/stats\n";
        return true;
      }
      break;

    case OPT_help: {
      printHelp(false);
      return true;
    }

    case OPT_offset:
      if (StringRef(InputArg->getValue()).getAsInteger(10, Offset)) {
        llvm::errs() << "error: expected integer for 'offset'\n";
        return true;
      }
      break;

    case OPT_length:
      if (StringRef(InputArg->getValue()).getAsInteger(10, Length)) {
        llvm::errs() << "error: expected integer for 'length'\n";
        return true;
      }
      break;

    case OPT_pos: {
      auto linecol = parseLineCol(InputArg->getValue());
      Line = linecol.first;
      Col = linecol.second;
      break;
    }

    case OPT_end_pos: {
      auto linecol = parseLineCol(InputArg->getValue());
      EndLine = linecol.first;
      EndCol = linecol.second;
      break;
    }

    case OPT_using_swift_args: {
      UsingSwiftArgs = true;
      break;
    }

      case OPT_swift_version: {
        unsigned ver;
        if (StringRef(InputArg->getValue()).getAsInteger(10, ver)) {
          llvm::errs() << "error: expected integer for 'swift-version'\n";
          return true;
        }
        SwiftVersion = ver;
        break;
      }

    case OPT_line:
      if (StringRef(InputArg->getValue()).getAsInteger(10, Line)) {
        llvm::errs() << "error: expected integer for 'line'\n";
        return true;
      }
      Col = 1;
      break;

    case OPT_replace:
      ReplaceText = InputArg->getValue();
      break;

    case OPT_module:
      ModuleName = InputArg->getValue();
      break;

    case OPT_group_name:
      ModuleGroupName = InputArg->getValue();
      break;

    case OPT_interested_usr:
      InterestedUSR = InputArg->getValue();
      break;

    case OPT_header:
      HeaderPath = InputArg->getValue();
      break;

    case OPT_text_input:
      TextInputFile = InputArg->getValue();
      break;

    case OPT_usr:
      USR = InputArg->getValue();
      break;

    case OPT_pass_as_sourcetext:
      PassAsSourceText = true;
      break;

    case OPT_cache_path:
      CachePath = InputArg->getValue();
      break;

    case OPT_req_opts:
      for (auto item : InputArg->getValues())
        RequestOptions.push_back(item);
      break;

    case OPT_check_interface_is_ascii:
      CheckInterfaceIsASCII = true;
      break;

    case OPT_dont_print_request:
      PrintRequest = false;
      break;

    case OPT_print_response_as_json:
      PrintResponseAsJSON = true;
      break;

    case OPT_print_raw_response:
      PrintRawResponse = true;
      break;

    case OPT_INPUT:
      SourceFile = InputArg->getValue();
      SourceText = llvm::None;
      Inputs.push_back(InputArg->getValue());
      break;

    case OPT_rename_spec:
      RenameSpecPath = InputArg->getValue();
      break;

    case OPT_json_request_path:
      JsonRequestPath = InputArg->getValue();
      break;

    case OPT_simplified_demangling:
      SimplifiedDemangling = true;
      break;

    case OPT_synthesized_extension:
      SynthesizedExtensions = true;
      break;

    case OPT_async:
      isAsyncRequest = true;
      break;

    case OPT_cursor_action:
      CollectActionables = true;
      break;

    case OPT_swift_name:
      SwiftName = InputArg->getValue();
      break;

    case OPT_objc_name:
      ObjCName = InputArg->getValue();
      break;

    case OPT_objc_selector:
      ObjCSelector = InputArg->getValue();
      break;

    case OPT_name:
      Name = InputArg->getValue();
      break;

    case OPT_cancel_on_subsequent_request:
      unsigned Cancel;
      if (StringRef(InputArg->getValue()).getAsInteger(10, Cancel)) {
        llvm::errs() << "error: expected integer for 'cancel-on-subsequent-request'\n";
        return true;
      }
      CancelOnSubsequentRequest = Cancel;
      break;

    case OPT_UNKNOWN:
      llvm::errs() << "error: unknown argument: "
                   << InputArg->getAsString(ParsedArgs) << '\n'
                   << "Use -h or -help for assistance" << '\n';
      return true;
    }
  }

  if (Request == SourceKitRequest::InterfaceGenOpen && isAsyncRequest) {
    llvm::errs()
        << "error: cannot use -async with interface-gen-open request\n";
    return true;
  }

  return false;
}

void TestOptions::printHelp(bool ShowHidden) const {

  // Based off of swift/lib/Driver/Driver.cpp, at Driver::printHelp
  //FIXME: should we use IncludedFlagsBitmask and ExcludedFlagsBitmask?
  // Maybe not for modes such as Interactive, Batch, AutolinkExtract, etc,
  // as in Driver.cpp. But could be useful for extra info, like HelpHidden.

  TestOptTable Table;

  Table.PrintHelp(llvm::outs(), "sourcekitd-test", "SourceKit Testing Tool",
                      ShowHidden);
}
