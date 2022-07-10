//===--- Subsystems.h - Swift Compiler Subsystem Entrypoints ----*- C++ -*-===//
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
//  This file declares the main entrypoints to the various subsystems.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SUBSYSTEMS_H
#define SWIFT_SUBSYSTEMS_H

#include "swift/Basic/LLVM.h"
#include "swift/Basic/OptionSet.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"

#include <memory>

namespace llvm {
  class MemoryBuffer;
  class Module;
  class TargetOptions;
  class TargetMachine;
}

namespace swift {
  class ArchetypeBuilder;
  class ASTContext;
  class CodeCompletionCallbacksFactory;
  class Decl;
  class DeclContext;
  class DelayedParsingCallbacks;
  class DiagnosticConsumer;
  class DiagnosticEngine;
  class FileUnit;
  class GenericParamList;
  class GenericSignature;
  class IRGenOptions;
  class LangOptions;
  class ModuleDecl;
  class Parser;
  class PersistentParserState;
  class SerializationOptions;
  class SILOptions;
  class SILModule;
  class SILParserTUState;
  class SourceFile;
  class SourceManager;
  class Token;
  class TopLevelContext;
  struct TypeLoc;
  
  /// SILParserState - This is a context object used to optionally maintain SIL
  /// parsing context for the parser.
  class SILParserState {
  public:
    SILModule *M;
    SILParserTUState *S;

    explicit SILParserState(SILModule *M);
    ~SILParserState();
  };

  /// @{

  /// \returns true if the declaration should be verified.  This can return
  /// false to decrease the number of declarations we verify in a single
  /// compilation.
  bool shouldVerify(const Decl *D, const ASTContext &Context);

  /// \brief Check that the source file is well-formed, aborting and spewing
  /// errors if not.
  ///
  /// "Well-formed" here means following the invariants of the AST, not that the
  /// code written by the user makes sense.
  void verify(SourceFile &SF);
  void verify(Decl *D);

  /// @}

  /// \brief Parse a single buffer into the given source file.
  ///
  /// If the source file is the main file, stop parsing after the next
  /// stmt-brace-item with side-effects.
  ///
  /// \param SF the file within the module being parsed.
  ///
  /// \param BufferID the buffer to parse from.
  ///
  /// \param[out] Done set to \c true if end of the buffer was reached.
  ///
  /// \param SIL if non-null, we're parsing a SIL file.
  ///
  /// \param PersistentState if non-null the same PersistentState object can
  /// be used to resume parsing or parse delayed function bodies.
  ///
  /// \param DelayedParseCB if non-null enables delayed parsing for function
  /// bodies.
  ///
  /// \return true if the parser found code with side effects.
  bool parseIntoSourceFile(SourceFile &SF, unsigned BufferID, bool *Done,
                           SILParserState *SIL = nullptr,
                           PersistentParserState *PersistentState = nullptr,
                           DelayedParsingCallbacks *DelayedParseCB = nullptr);

  /// \brief Finish the parsing by going over the nodes that were delayed
  /// during the first parsing pass.
  void performDelayedParsing(DeclContext *DC,
                             PersistentParserState &PersistentState,
                             CodeCompletionCallbacksFactory *Factory);

  /// \brief Lex and return a vector of tokens for the given buffer.
  std::vector<Token> tokenize(const LangOptions &LangOpts,
                              const SourceManager &SM, unsigned BufferID,
                              unsigned Offset = 0, unsigned EndOffset = 0,
                              bool KeepComments = true,
                              bool TokenizeInterpolatedString = true,
                              ArrayRef<Token> SplitTokens = ArrayRef<Token>());

  /// Once parsing is complete, this walks the AST to resolve imports, record
  /// operators, and do other top-level validation.
  ///
  /// \param StartElem Where to start for incremental name binding in the main
  ///                  source file.
  void performNameBinding(SourceFile &SF, unsigned StartElem = 0);

  /// Once parsing and name-binding are complete, this optionally transforms the
  /// ASTs to add calls to external logging functions.
  ///
  /// \param HighPerformance True if the playground transform should omit
  /// instrumentation that has a high runtime performance impact.
  void performPlaygroundTransform(SourceFile &SF, bool HighPerformance);
  
  /// Flags used to control type checking.
  enum class TypeCheckingFlags : unsigned {
    /// Whether to delay checking that benefits from having the entire
    /// module parsed, e.g., Objective-C method override checking.
    DelayWholeModuleChecking = 1 << 0,

    /// If set, dumps wall time taken to check each function body to
    /// llvm::errs().
    DebugTimeFunctionBodies = 1 << 1,

    /// Indicates that the type checker is checking code that will be
    /// immediately executed.
    ForImmediateMode = 1 << 2
  };

  /// Once parsing and name-binding are complete, this walks the AST to resolve
  /// types and diagnose problems therein.
  ///
  /// \param StartElem Where to start for incremental type-checking in the main
  /// source file.
  ///
  /// \param WarnLongFunctionBodies If non-zero, warn when a function body takes
  /// longer than this many milliseconds to type-check
  void performTypeChecking(SourceFile &SF, TopLevelContext &TLC,
                           OptionSet<TypeCheckingFlags> Options,
                           unsigned StartElem = 0,
                           unsigned WarnLongFunctionBodies = 0);

  /// Once type checking is complete, this walks protocol requirements
  /// to resolve default witnesses.
  void finishTypeChecking(SourceFile &SF);

  /// Now that we have type-checked an entire module, perform any type
  /// checking that requires the full module, e.g., Objective-C method
  /// override checking.
  ///
  /// Note that clients still perform this checking file-by-file to
  /// provide a somewhat defined order in which diagnostics should be
  /// emitted.
  void performWholeModuleTypeChecking(SourceFile &SF);

  /// Incrementally type-check only added external definitions.
  void typeCheckExternalDefinitions(SourceFile &SF);

  /// \brief Recursively validate the specified type.
  ///
  /// This is used when dealing with partial source files (e.g. SIL parsing,
  /// code completion).
  ///
  /// \returns false on success, true on error.
  bool performTypeLocChecking(ASTContext &Ctx, TypeLoc &T,
                              bool isSILType, DeclContext *DC,
                              bool ProduceDiagnostics = true);

  /// Expose TypeChecker's handling of GenericParamList to SIL parsing.
  GenericSignature *handleSILGenericParams(ASTContext &Ctx,
                                           GenericParamList *genericParams,
                                           DeclContext *DC);

  /// Turn the given module into SIL IR.
  ///
  /// The module must contain source files.
  ///
  /// If \p makeModuleFragile is true, all functions and global variables of
  /// the module are marked as fragile. This is used for compiling the stdlib.
  /// if \p wholeModuleCompilation is true, the optimizer assumes that the SIL
  /// of all files in the module is present in the SILModule.
  std::unique_ptr<SILModule>
  performSILGeneration(ModuleDecl *M, SILOptions &options,
                       bool makeModuleFragile = false,
                       bool wholeModuleCompilation = false);

  /// Turn a source file into SIL IR.
  ///
  /// If \p StartElem is provided, the module is assumed to be only part of the
  /// SourceFile, and any optimizations should take that into account.
  /// If \p makeModuleFragile is true, all functions and global variables of
  /// the module are marked as fragile. This is used for compiling the stdlib.
  std::unique_ptr<SILModule>
  performSILGeneration(FileUnit &SF, SILOptions &options,
                       Optional<unsigned> StartElem = None,
                       bool makeModuleFragile = false);

  using ModuleOrSourceFile = PointerUnion<ModuleDecl *, SourceFile *>;

  /// Serializes a module or single source file to the given output file.
  void serialize(ModuleOrSourceFile DC, const SerializationOptions &options,
                 const SILModule *M = nullptr);

  /// Get the CPU and subtarget feature options to use when emitting code.
  std::tuple<llvm::TargetOptions, std::string, std::vector<std::string>>
  getIRTargetOptions(IRGenOptions &Opts, ASTContext &Ctx);

  /// Turn the given Swift module into either LLVM IR or native code
  /// and return the generated LLVM IR module.
  std::unique_ptr<llvm::Module>
  performIRGeneration(IRGenOptions &Opts, ModuleDecl *M, SILModule *SILMod,
                      StringRef ModuleName, llvm::LLVMContext &LLVMContext);

  /// Turn the given Swift module into either LLVM IR or native code
  /// and return the generated LLVM IR module.
  std::unique_ptr<llvm::Module>
  performIRGeneration(IRGenOptions &Opts, SourceFile &SF, SILModule *SILMod,
                      StringRef ModuleName, llvm::LLVMContext &LLVMContext,
                      unsigned StartElem = 0);

  /// Given an already created LLVM module, construct a pass pipeline and run
  /// the Swift LLVM Pipeline upon it. This does not cause the module to be
  /// printed, only to be optimized.
  void performLLVMOptimizations(IRGenOptions &Opts, llvm::Module *Module,
                                llvm::TargetMachine *TargetMachine);

  /// Wrap a serialized module inside a swift AST section in an object file.
  void createSwiftModuleObjectFile(SILModule &SILMod, StringRef Buffer,
                                   StringRef OutputPath);

  /// Turn the given LLVM module into native code and return true on error.
  bool performLLVM(IRGenOptions &Opts, ASTContext &Ctx,
                   llvm::Module *Module);

  /// A convenience wrapper for Parser functionality.
  class ParserUnit {
  public:
    ParserUnit(SourceManager &SM, unsigned BufferID,
               const LangOptions &LangOpts, StringRef ModuleName);
    ParserUnit(SourceManager &SM, unsigned BufferID);
    ParserUnit(SourceManager &SM, unsigned BufferID,
               unsigned Offset, unsigned EndOffset);

    ~ParserUnit();

    Parser &getParser();
    SourceFile &getSourceFile();
    DiagnosticEngine &getDiagnosticEngine();
    const LangOptions &getLangOptions() const;

  private:
    struct Implementation;
    Implementation &Impl;
  };

} // end namespace swift

#endif // SWIFT_SUBSYSTEMS_H
