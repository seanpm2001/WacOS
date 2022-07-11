//===--- TBDGenVisitor.h - AST Visitor for TBD generation -----------------===//
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
//  This file defines the visitor that finds all symbols in a swift AST.
//
//===----------------------------------------------------------------------===//
#ifndef SWIFT_TBDGEN_TBDGENVISITOR_H
#define SWIFT_TBDGEN_TBDGENVISITOR_H

#include "swift/AST/ASTMangler.h"
#include "swift/AST/ASTVisitor.h"
#include "swift/AST/FileUnit.h"
#include "swift/AST/Module.h"
#include "swift/AST/ParameterList.h"
#include "swift/Basic/LLVM.h"
#include "swift/IRGen/Linking.h"
#include "swift/SIL/FormalLinkage.h"
#include "swift/SIL/SILDeclRef.h"
#include "swift/SIL/SILWitnessTable.h"
#include "swift/SIL/TypeLowering.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Triple.h"
#include "llvm/TextAPI/InterfaceFile.h"

using namespace swift::irgen;
using StringSet = llvm::StringSet<>;

namespace llvm {
class DataLayout;
}

namespace swift {

class TBDGenDescriptor;
struct TBDGenOptions;
class SymbolSource;

namespace tbdgen {

enum class LinkerPlatformId: uint8_t {
#define LD_PLATFORM(Name, Id) Name = Id,
#include "ldPlatformKinds.def"
};

struct InstallNameStore {
  // The default install name to use when no specific install name is specified.
  std::string InstallName;
  // The install name specific to the platform id. This takes precedence over
  // the default install name.
  std::map<uint8_t, std::string> PlatformInstallName;
  StringRef getInstallName(LinkerPlatformId Id) const;
};

/// A set of callbacks for recording APIs.
class APIRecorder {
public:
  virtual ~APIRecorder() {}

  virtual void addSymbol(StringRef name, llvm::MachO::SymbolKind kind,
                         SymbolSource source) {}
  virtual void addObjCInterface(const ClassDecl *decl) {}
  virtual void addObjCMethod(const ClassDecl *cls, SILDeclRef method) {}
};

class SimpleAPIRecorder final : public APIRecorder {
public:
  using SymbolCallbackFn = llvm::function_ref<void(
      StringRef, llvm::MachO::SymbolKind, SymbolSource)>;

  SimpleAPIRecorder(SymbolCallbackFn func) : func(func) {}

  void addSymbol(StringRef symbol, llvm::MachO::SymbolKind kind,
                 SymbolSource source) override {
    func(symbol, kind, source);
  }
private:
  SymbolCallbackFn func;
};

class TBDGenVisitor : public ASTVisitor<TBDGenVisitor> {
#ifndef NDEBUG
  /// Tracks the symbols emitted to ensure we don't emit any duplicates.
  llvm::StringSet<> DuplicateSymbolChecker;
#endif

  Optional<llvm::DataLayout> DataLayout = None;
  const StringRef DataLayoutDescription;

  UniversalLinkageInfo UniversalLinkInfo;
  ModuleDecl *SwiftModule;
  const TBDGenOptions &Opts;
  APIRecorder &recorder;

  using SymbolKind = llvm::MachO::SymbolKind;

  /// A set of original function and derivative configuration pairs for which
  /// derivative symbols have been emitted.
  ///
  /// Used to deduplicate derivative symbol emission for `@differentiable` and
  /// `@derivative` attributes.
  llvm::DenseSet<std::pair<AbstractFunctionDecl *, AutoDiffConfig>>
      AddedDerivatives;

  std::vector<Decl*> DeclStack;
  std::unique_ptr<std::map<std::string, InstallNameStore>>
    previousInstallNameMap;
  std::unique_ptr<std::map<std::string, InstallNameStore>>
    parsePreviousModuleInstallNameMap();
  void addSymbolInternal(StringRef name, llvm::MachO::SymbolKind kind,
                         SymbolSource source);
  void addLinkerDirectiveSymbolsLdHide(StringRef name, llvm::MachO::SymbolKind kind);
  void addLinkerDirectiveSymbolsLdPrevious(StringRef name, llvm::MachO::SymbolKind kind);
  void addSymbol(StringRef name, SymbolSource source,
                 SymbolKind kind = SymbolKind::GlobalSymbol);

  void addSymbol(SILDeclRef declRef);
  void addAsyncFunctionPointerSymbol(SILDeclRef declRef);

  void addSymbol(LinkEntity entity);

  void addConformances(const IterableDeclContext *IDC);

  void addDispatchThunk(SILDeclRef declRef);

  void addMethodDescriptor(SILDeclRef declRef);

  void addProtocolRequirementsBaseDescriptor(ProtocolDecl *proto);
  void addAssociatedTypeDescriptor(AssociatedTypeDecl *assocType);
  void addAssociatedConformanceDescriptor(AssociatedConformance conformance);
  void addBaseConformanceDescriptor(BaseConformance conformance);

  /// Adds the symbol for the linear map function of the given kind associated
  /// with the given original function and derivative function configuration.
  void addAutoDiffLinearMapFunction(AbstractFunctionDecl *original,
                                    const AutoDiffConfig &config,
                                    AutoDiffLinearMapKind kind);

  /// Adds the symbol for the autodiff function of the given kind associated
  /// with the given original function, parameter indices, and derivative
  /// generic signature.
  void
  addAutoDiffDerivativeFunction(AbstractFunctionDecl *original,
                                IndexSubset *parameterIndices,
                                GenericSignature derivativeGenericSignature,
                                AutoDiffDerivativeFunctionKind kind);

  /// Adds the symbol for the differentiability witness associated with the
  /// given original function, AST parameter indices, result indices, and
  /// derivative generic signature.
  void addDifferentiabilityWitness(AbstractFunctionDecl *original,
                                   DifferentiabilityKind kind,
                                   IndexSubset *astParameterIndices,
                                   IndexSubset *resultIndices,
                                   GenericSignature derivativeGenericSignature);

  /// Adds symbols associated with the given original function and
  /// derivative function configuration.
  void addDerivativeConfiguration(DifferentiabilityKind diffKind,
                                  AbstractFunctionDecl *original,
                                  const AutoDiffConfig &config);

public:
  TBDGenVisitor(const llvm::Triple &target, const StringRef dataLayoutString,
                ModuleDecl *swiftModule, const TBDGenOptions &opts,
                APIRecorder &recorder)
      : DataLayoutDescription(dataLayoutString),
        UniversalLinkInfo(target, opts.HasMultipleIGMs, /*forcePublic*/ false),
        SwiftModule(swiftModule), Opts(opts), recorder(recorder),
        previousInstallNameMap(parsePreviousModuleInstallNameMap()) {}

  /// Create a new visitor using the target and layout information from a
  /// TBDGenDescriptor.
  TBDGenVisitor(const TBDGenDescriptor &desc, APIRecorder &recorder);

  ~TBDGenVisitor() { assert(DeclStack.empty()); }

  /// Add the main symbol.
  void addMainIfNecessary(FileUnit *file);

  /// Adds the global symbols associated with the first file.
  void addFirstFileSymbols();

  void visitDefaultArguments(ValueDecl *VD, ParameterList *PL);

  void visitAbstractFunctionDecl(AbstractFunctionDecl *AFD);

  void visitAccessorDecl(AccessorDecl *AD);

  void visitNominalTypeDecl(NominalTypeDecl *NTD);

  void visitClassDecl(ClassDecl *CD);

  void visitConstructorDecl(ConstructorDecl *CD);

  void visitDestructorDecl(DestructorDecl *DD);

  void visitExtensionDecl(ExtensionDecl *ED);
  
  void visitFuncDecl(FuncDecl *FD);

  void visitProtocolDecl(ProtocolDecl *PD);

  void visitAbstractStorageDecl(AbstractStorageDecl *ASD);

  void visitVarDecl(VarDecl *VD);

  void visitEnumDecl(EnumDecl *ED);

  void visitEnumElementDecl(EnumElementDecl *EED);

  void visitDecl(Decl *D) {}

  void visit(Decl *D);

  /// Visit the symbols in a given file unit.
  void visitFile(FileUnit *file);

  /// Visit the files specified by a given TBDGenDescriptor.
  void visit(const TBDGenDescriptor &desc);
};
} // end namespace tbdgen
} // end namespace swift

#endif
