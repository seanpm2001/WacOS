//===--- SymbolGraph.cpp - Symbol Graph Data Structure -------------------===//
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

#include "clang/AST/DeclObjC.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Module.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/USRGeneration.h"
#include "swift/Basic/Version.h"
#include "swift/Sema/IDETypeChecking.h"

#include "DeclarationFragmentPrinter.h"
#include "FormatVersion.h"
#include "Symbol.h"
#include "SymbolGraph.h"
#include "SymbolGraphASTWalker.h"

using namespace swift;
using namespace symbolgraphgen;

SymbolGraph::SymbolGraph(SymbolGraphASTWalker &Walker,
                         ModuleDecl &M,
                         Optional<ModuleDecl *> ExtendedModule,
                         markup::MarkupContext &Ctx,
                         Optional<llvm::VersionTuple> ModuleVersion,
                         bool IsForSingleNode)
: Walker(Walker),
  M(M),
  ExtendedModule(ExtendedModule),
  Ctx(Ctx),
  ModuleVersion(ModuleVersion),
  IsForSingleNode(IsForSingleNode) {
    if (auto *DM = M.getDeclaringModuleIfCrossImportOverlay()) {
      DeclaringModule = DM;
      SmallVector<Identifier, 1> Bystanders;
      if (M.getRequiredBystandersIfCrossImportOverlay(DM, Bystanders)) {
        BystanderModules = Bystanders;
      }
    }
  }

// MARK: - Utilities

PrintOptions SymbolGraph::getDeclarationFragmentsPrintOptions() const {
  PrintOptions Opts;
  Opts.FunctionDefinitions = false;
  Opts.ArgAndParamPrinting =
    PrintOptions::ArgAndParamPrintingMode::MatchSource;
  Opts.PrintGetSetOnRWProperties = true;
  Opts.PrintPropertyAccessors = true;
  Opts.PrintSubscriptAccessors = true;
  Opts.SkipUnderscoredKeywords = true;
  Opts.SkipAttributes = false;
  Opts.PrintOverrideKeyword = true;
  Opts.PrintImplicitAttrs = false;
  Opts.PrintFunctionRepresentationAttrs =
    PrintOptions::FunctionRepresentationMode::None;
  Opts.PrintUserInaccessibleAttrs = false;
  Opts.SkipPrivateStdlibDecls = true;
  Opts.SkipUnderscoredStdlibProtocols = true;
  Opts.PrintGenericRequirements = true;
  Opts.PrintInherited = false;
  Opts.ExplodeEnumCaseDecls = true;

  Opts.ExclusiveAttrList.clear();

  llvm::StringMap<AnyAttrKind> ExcludeAttrs;

#define DECL_ATTR(SPELLING, CLASS, OPTIONS, CODE) \
  if (StringRef(#SPELLING).startswith("_")) \
    ExcludeAttrs.insert(std::make_pair("DAK_" #CLASS, DAK_##CLASS));
#define TYPE_ATTR(X) ExcludeAttrs.insert(std::make_pair("TAK_" #X, TAK_##X));
#include "swift/AST/Attr.def"

  // Allow the following type attributes:
  ExcludeAttrs.erase("TAK_autoclosure");
  ExcludeAttrs.erase("TAK_convention");
  ExcludeAttrs.erase("TAK_noescape");
  ExcludeAttrs.erase("TAK_escaping");
  ExcludeAttrs.erase("TAK_inout");

  // Don't allow the following decl attributes:
  // These can be large and are already included elsewhere in
  // symbol graphs.
  ExcludeAttrs.insert(std::make_pair("DAK_Available", DAK_Available));
  ExcludeAttrs.insert(std::make_pair("DAK_Inline", DAK_Inline));
  ExcludeAttrs.insert(std::make_pair("DAK_Inlinable", DAK_Inlinable));
  ExcludeAttrs.insert(std::make_pair("DAK_Prefix", DAK_Prefix));
  ExcludeAttrs.insert(std::make_pair("DAK_Postfix", DAK_Postfix));
  ExcludeAttrs.insert(std::make_pair("DAK_Infix", DAK_Infix));

  // In "emit modules separately" jobs, access modifiers show up as attributes,
  // but we don't want them to be printed in declarations
  ExcludeAttrs.insert(std::make_pair("DAK_AccessControl", DAK_AccessControl));

  for (const auto &Entry : ExcludeAttrs) {
    Opts.ExcludeAttrList.push_back(Entry.getValue());
  }

  return Opts;
}

PrintOptions
SymbolGraph::getSubHeadingDeclarationFragmentsPrintOptions() const {
  auto Options = getDeclarationFragmentsPrintOptions();
  Options.ArgAndParamPrinting =
    PrintOptions::ArgAndParamPrintingMode::ArgumentOnly;

  //--------------------------------------------------------------------------//
  // Although we want these in the full declaration presentation,
  // particularly for protocol requirements,
  // we don't want to show these in subheadings.
  Options.PrintGetSetOnRWProperties = false;
  Options.PrintPropertyAccessors = false;
  Options.PrintSubscriptAccessors = false;
  //--------------------------------------------------------------------------//

  Options.SkipAttributes = true;
  Options.VarInitializers = false;
  Options.PrintDefaultArgumentValue = false;
  Options.PrintEmptyArgumentNames = false;
  Options.PrintOverrideKeyword = false;
  Options.PrintGenericRequirements = false;

  #define DECL_ATTR(SPELLING, CLASS, OPTIONS, CODE) \
    Options.ExcludeAttrList.push_back(DAK_##CLASS);
  #define TYPE_ATTR(X) \
    Options.ExcludeAttrList.push_back(TAK_##X);
  #include "swift/AST/Attr.def"

  // Don't include these attributes in subheadings.
  Options.ExcludeAttrList.push_back(DAK_Final);
  Options.ExcludeAttrList.push_back(DAK_Mutating);
  Options.ExcludeAttrList.push_back(DAK_NonMutating);
  Options.ExcludeAttrList.push_back(TAK_escaping);

  return Options;
}

bool
SymbolGraph::isRequirementOrDefaultImplementation(const ValueDecl *VD) const {
  const auto *DC = VD->getDeclContext();

  if (isa<ProtocolDecl>(DC) && VD->isProtocolRequirement()) {
    return true;
  }

  // At this point, VD is either a default implementation of a requirement
  // or a freestanding implementation from a protocol extension without
  // a corresponding requirement.

  auto *Proto = dyn_cast_or_null<ProtocolDecl>(DC->getSelfNominalTypeDecl());
  if (!Proto) {
    return false;
  }

  /// Try to find a member of the owning protocol with the same name
  /// that is a requirement.
  auto FoundRequirementMemberNamed = [](DeclName Name,
                                        ProtocolDecl *Proto) -> bool {
    for (const auto *Member : Proto->lookupDirect(Name)) {
      if (isa<ProtocolDecl>(Member->getDeclContext()) &&
          Member->isProtocolRequirement()) {
        return true;
      }
    }
    return false;
  };

  if (FoundRequirementMemberNamed(VD->getName(), Proto)) {
    return true;
  }
  for (auto *Inherited : Proto->getInheritedProtocols()) {
    if (FoundRequirementMemberNamed(VD->getName(), Inherited)) {
      return true;
    }
  }

  // Couldn't find any requirement members of a protocol by this name.
  // It's not a requirement or default implementation of a requirement.
  return false;
}

// MARK: - Symbols (Nodes)

void SymbolGraph::recordNode(Symbol S) {
  Nodes.insert(S);

  // Record all of the possible relationships (edges) originating
  // with this declaration.
  recordMemberRelationship(S);
  recordConformanceSynthesizedMemberRelationships(S);
  recordConformanceRelationships(S);
  recordInheritanceRelationships(S);
  recordDefaultImplementationRelationships(S);
  recordOverrideRelationship(S);
  recordRequirementRelationships(S);
  recordOptionalRequirementRelationships(S);
}

// MARK: - Relationships (Edges)

void SymbolGraph::recordEdge(Symbol Source,
                             Symbol Target,
                             RelationshipKind Kind,
                             const ExtensionDecl *ConformanceExtension) {
  if (isImplicitlyPrivate(Target.getSymbolDecl())) {
    // Don't record relationships to privately named things because
    // we'll never be able to look up the target anyway.
    return;
  }
  Edges.insert({this, Kind, Source, Target, ConformanceExtension});
}

void SymbolGraph::recordMemberRelationship(Symbol S) {
  const auto *DC = S.getSymbolDecl()->getDeclContext();
  switch (DC->getContextKind()) {
    case DeclContextKind::GenericTypeDecl:
    case DeclContextKind::ExtensionDecl:
    case swift::DeclContextKind::EnumElementDecl:
      /*
       If this symbol is itself a protocol requirement, or
       is a default implementation of a protocol requirement,
       don't record a memberOf relationship.

       This is to allow distinguishing between requirements,
       default implementations of requirements, and just other
       things added to protocols in extensions not related to their
       requirements.
       */
      if (isRequirementOrDefaultImplementation(S.getSymbolDecl())) {
        return;
      }
      if (DC->getSelfNominalTypeDecl() == nullptr) {
        // If we couldn't look up the type the member is declared on (e.g.
        // because the member is declared in an extension whose extended type
        // doesn't exist), don't record a memberOf relationship.
        return;
      }
      return recordEdge(S,
                        Symbol(this, DC->getSelfNominalTypeDecl(), nullptr),
                        RelationshipKind::MemberOf());
    case swift::DeclContextKind::AbstractClosureExpr:
    case swift::DeclContextKind::Initializer:
    case swift::DeclContextKind::TopLevelCodeDecl:
    case swift::DeclContextKind::SubscriptDecl:
    case swift::DeclContextKind::AbstractFunctionDecl:
    case swift::DeclContextKind::SerializedLocal:
    case swift::DeclContextKind::Module:
    case swift::DeclContextKind::FileUnit:
      break;
  }
}

bool SymbolGraph::synthesizedMemberIsBestCandidate(const ValueDecl *VD,
    const NominalTypeDecl *Owner) const {
  const auto *FD = dyn_cast<FuncDecl>(VD);
  if (!FD) {
    return true;
  }
  auto *DC = const_cast<DeclContext*>(Owner->getDeclContext());

  ResolvedMemberResult Result =
    resolveValueMember(*DC, Owner->getSelfTypeInContext(),
                       FD->getEffectiveFullName());

  const auto ViableCandidates =
    Result.getMemberDecls(InterestedMemberKind::All);

  if (ViableCandidates.size() < 2) {
    return true;
  }

  return !(Result.hasBestOverload() && Result.getBestOverload() != VD);
}

void SymbolGraph::recordConformanceSynthesizedMemberRelationships(Symbol S) {
  if (!Walker.Options.EmitSynthesizedMembers) {
    return;
  }
  const auto VD = S.getSymbolDecl();
  const NominalTypeDecl *OwningNominal = nullptr;
  if (const auto *ThisNominal = dyn_cast<NominalTypeDecl>(VD)) {
    OwningNominal = ThisNominal;
  } else if (const auto *Extension = dyn_cast<ExtensionDecl>(VD)) {
    if (const auto *ExtendedNominal = Extension->getExtendedNominal()) {
      if (!ExtendedNominal->getModuleContext()->getNameStr()
          .equals(M.getNameStr())) {
        OwningNominal = ExtendedNominal;
      } else {
        return;
      }
    } else {
      return;
    }
  } else {
    return;
  }

  SynthesizedExtensionAnalyzer ExtensionAnalyzer(
      const_cast<NominalTypeDecl *>(OwningNominal),
      PrintOptions::printModuleInterface(
          OwningNominal->getASTContext().TypeCheckerOpts.PrintFullConvention));
  auto MergeGroupKind = SynthesizedExtensionAnalyzer::MergeGroupKind::All;
  ExtensionAnalyzer.forEachExtensionMergeGroup(MergeGroupKind,
      [&](ArrayRef<ExtensionInfo> ExtensionInfos){
    for (const auto &Info : ExtensionInfos) {
      if (!Info.IsSynthesized) {
        continue;
      }

      // We are only interested in synthesized members that come from an
      // extension that we defined in our module.
      if (Info.EnablingExt && Info.EnablingExt->getModuleContext() != &M) {
        continue;
      }

      for (const auto ExtensionMember : Info.Ext->getMembers()) {
        if (const auto SynthMember = dyn_cast<ValueDecl>(ExtensionMember)) {
          if (SynthMember->isObjC()) {
            continue;
          }

          const auto StdlibModule = OwningNominal->getASTContext()
              .getStdlibModule(/*loadIfAbsent=*/true);

          // There can be synthesized members on effectively private protocols
          // or things that conform to them. We don't want to include those.
          if (isImplicitlyPrivate(SynthMember,
              /*IgnoreContext =*/
              SynthMember->getModuleContext() == StdlibModule)) {
            continue;
          }

          if (!synthesizedMemberIsBestCandidate(SynthMember, OwningNominal)) {
            continue;
          }

          auto ExtendedSG = Walker.getModuleSymbolGraph(OwningNominal);

          Symbol Source(this, SynthMember, OwningNominal);
          Symbol Target(this, OwningNominal, nullptr);

          ExtendedSG->Nodes.insert(Source);

          ExtendedSG->recordEdge(Source, Target, RelationshipKind::MemberOf());
         }
      }
    }
  });
}

void
SymbolGraph::recordInheritanceRelationships(Symbol S) {
  const auto VD = S.getSymbolDecl();
  if (const auto *NTD = dyn_cast<NominalTypeDecl>(VD)) {
    for (const auto &InheritanceLoc : NTD->getInherited()) {
      auto Ty = InheritanceLoc.getType();
      if (!Ty) {
        continue;
      }
      auto *InheritedTypeDecl =
          dyn_cast_or_null<ClassDecl>(Ty->getAnyNominal());
      if (!InheritedTypeDecl) {
        continue;
      }

      recordEdge(Symbol(this, VD, nullptr),
                 Symbol(this, InheritedTypeDecl, nullptr),
                 RelationshipKind::InheritsFrom());
    }
  }
}

void SymbolGraph::recordDefaultImplementationRelationships(Symbol S) {
  const auto *VD = S.getSymbolDecl();

  /// Claim a protocol `P`'s members as default implementation targets
  /// for `VD`.
  auto HandleProtocol = [=](const ProtocolDecl *P) {
    for (const auto *Member : P->getMembers()) {
      if (const auto *MemberVD = dyn_cast<ValueDecl>(Member)) {
        if (MemberVD->getName().compare(VD->getName()) == 0) {
          recordEdge(Symbol(this, VD, nullptr),
                     Symbol(this, MemberVD, nullptr),
                     RelationshipKind::DefaultImplementationOf());

          // If P is from a different module, and it's being added to a type
          // from the current module, add a `memberOf` relation to the extended
          // protocol.
          if (MemberVD->getModuleContext()->getNameStr() != M.getNameStr() && VD->getDeclContext()) {
            if (auto *ExP = VD->getDeclContext()->getSelfNominalTypeDecl()) {
              recordEdge(Symbol(this, VD, nullptr),
                         Symbol(this, ExP, nullptr),
                         RelationshipKind::MemberOf());
            }
          }
        }
      }
    }
  };

  if (const auto *Extension = dyn_cast<ExtensionDecl>(VD->getDeclContext())) {
    if (const auto *ExtendedProtocol = Extension->getExtendedProtocolDecl()) {
      HandleProtocol(ExtendedProtocol);
      for (const auto *Inherited : ExtendedProtocol->getInheritedProtocols()) {
        HandleProtocol(Inherited);
      }
    }
  }
}

void
SymbolGraph::recordRequirementRelationships(Symbol S) {
  const auto VD = S.getSymbolDecl();
  if (const auto *Protocol = dyn_cast<ProtocolDecl>(VD->getDeclContext())) {
    if (VD->isProtocolRequirement()) {
      recordEdge(Symbol(this, VD, nullptr),
                 Symbol(this, Protocol, nullptr),
                 RelationshipKind::RequirementOf());
    }
  }
}

void SymbolGraph::recordOptionalRequirementRelationships(Symbol S) {
  const auto VD = S.getSymbolDecl();
  if (const auto *Protocol = dyn_cast<ProtocolDecl>(VD->getDeclContext())) {
    if (VD->isProtocolRequirement() &&
        VD->getAttrs().hasAttribute<OptionalAttr>()) {
      recordEdge(Symbol(this, VD, nullptr),
                 Symbol(this, Protocol, nullptr),
                 RelationshipKind::OptionalRequirementOf());
    }
  }
}

void
SymbolGraph::recordConformanceRelationships(Symbol S) {
  const auto VD = S.getSymbolDecl();
  if (const auto *NTD = dyn_cast<NominalTypeDecl>(VD)) {
    for (const auto *Conformance : NTD->getAllConformances()) {
      recordEdge(Symbol(this, VD, nullptr),
        Symbol(this, Conformance->getProtocol(), nullptr),
        RelationshipKind::ConformsTo(),
        dyn_cast_or_null<ExtensionDecl>(Conformance->getDeclContext()));
    }
  }
}

void SymbolGraph::recordOverrideRelationship(Symbol S) {
  const auto VD = S.getSymbolDecl();
  if (const auto *Override = VD->getOverriddenDecl()) {
    recordEdge(Symbol(this, VD, nullptr),
               Symbol(this, Override, nullptr),
               RelationshipKind::Overrides());
  }
}

// MARK: - Serialization

void SymbolGraph::serialize(llvm::json::OStream &OS) {
  OS.object([&](){
    OS.attributeObject("metadata", [&](){
      {
        AttributeRAII FV("formatVersion", OS);
        llvm::VersionTuple FormatVersion(SWIFT_SYMBOLGRAPH_FORMAT_MAJOR,
                                         SWIFT_SYMBOLGRAPH_FORMAT_MINOR,
                                         SWIFT_SYMBOLGRAPH_FORMAT_PATCH);
        symbolgraphgen::serialize(FormatVersion, OS);
      } // end formatVersion:

      auto VersionString = version::getSwiftFullVersion();
      StringRef VersionStringRef(VersionString.c_str(), VersionString.size());
      OS.attribute("generator", VersionStringRef);
    }); // end metadata:

    OS.attributeObject("module", [&](){
      if (DeclaringModule) {
        // A cross-import overlay can be considered part of its declaring module
        OS.attribute("name", (*DeclaringModule)->getNameStr());
        std::vector<StringRef> B;
        for (auto BModule : BystanderModules) {
          B.push_back(BModule.str());
        }
        OS.attribute("bystanders", B);
      } else {
        OS.attribute("name", M.getNameStr());
      }
      AttributeRAII Platform("platform", OS);

      symbolgraphgen::serialize(M, OS, Walker.Options.Target);
    });

    if (ModuleVersion) {
      AttributeRAII MV("moduleVersion", OS);
      symbolgraphgen::serialize(*ModuleVersion, OS);
    }

    OS.attributeArray("symbols", [&](){
      for (const auto &S: Nodes) {
        S.serialize(OS);
      }
    });

    OS.attributeArray("relationships", [&](){
      for (const auto &Relationship : Edges) {
        Relationship.serialize(OS);
      }
    });
  });
}

void
SymbolGraph::serializeDeclarationFragments(StringRef Key,
                                           const Symbol &S,
                                           llvm::json::OStream &OS) {
  DeclarationFragmentPrinter Printer(this, OS, Key);
  auto Options = getDeclarationFragmentsPrintOptions();
  if (S.getBaseType()) {
    Options.setBaseType(S.getBaseType());
    Options.PrintAsMember = true;
  }
  S.getSymbolDecl()->print(Printer, Options);
}

void
SymbolGraph::serializeNavigatorDeclarationFragments(StringRef Key,
                                                    const Symbol &S,
                                                    llvm::json::OStream &OS) {
  if (const auto *TD = dyn_cast<GenericTypeDecl>(S.getSymbolDecl())) {
    DeclarationFragmentPrinter Printer(this, OS, Key);
    Printer.printAbridgedType(TD, /*PrintKeyword=*/false);
  }
}

void
SymbolGraph::serializeSubheadingDeclarationFragments(StringRef Key,
                                                     const Symbol &S,
                                                     llvm::json::OStream &OS) {
  DeclarationFragmentPrinter Printer(this, OS, Key);

  if (const auto *TD = dyn_cast<GenericTypeDecl>(S.getSymbolDecl())) {
    Printer.printAbridgedType(TD, /*PrintKeyword=*/true);
  } else {
    auto Options = getSubHeadingDeclarationFragmentsPrintOptions();
    if (S.getBaseType()) {
      Options.setBaseType(S.getBaseType());
      Options.PrintAsMember = true;
    }
    S.getSymbolDecl()->print(Printer, Options);
  }
}

void
SymbolGraph::serializeDeclarationFragments(StringRef Key, Type T,
                                           Type BaseType,
                                           llvm::json::OStream &OS) {
  DeclarationFragmentPrinter Printer(this, OS, Key);
  auto Options = getDeclarationFragmentsPrintOptions();
  if (BaseType) {
    Options.setBaseType(BaseType);
    Options.PrintAsMember = true;
  }
  T->print(Printer, Options);
}

bool SymbolGraph::isImplicitlyPrivate(const Decl *D,
                                      bool IgnoreContext) const {
  // Don't record unconditionally private declarations
  if (D->isPrivateStdlibDecl(/*treatNonBuiltinProtocolsAsPublic=*/false)) {
    return true;
  }

  // Don't record effectively internal declarations if specified
  if (D->hasUnderscoredNaming()) {
    AccessLevel symLevel = AccessLevel::Public;
    if (const ValueDecl *VD = dyn_cast<ValueDecl>(D)) {
      symLevel = VD->getFormalAccess();
    }

    // Underscored symbols should be treated as `internal`, unless they're already
    // marked that way - in that case, treat them as `private`
    AccessLevel effectiveLevel;
    if (symLevel > AccessLevel::Internal) {
      effectiveLevel = AccessLevel::Internal;
    } else {
      effectiveLevel = AccessLevel::Private;
    }

    if (Walker.Options.MinimumAccessLevel > effectiveLevel)
      return true;
  }

  // Don't include declarations with the @_spi attribute unless the
  // access control filter is internal or below.
  if (D->isSPI() && !Walker.Options.IncludeSPISymbols) {
    return Walker.Options.MinimumAccessLevel > AccessLevel::Internal;
  }

  if (const auto *Extension = dyn_cast<ExtensionDecl>(D)) {
    if (const auto *Nominal = Extension->getExtendedNominal()) {
      return isImplicitlyPrivate(Nominal, IgnoreContext);
    }
  }

  if (const auto *VD = dyn_cast<ValueDecl>(D)) {
    // Symbols must meet the minimum access level to be included in the graph.
    if (VD->getFormalAccess() < Walker.Options.MinimumAccessLevel) {
      return true;
    }

    // Special cases below.

    auto BaseName = VD->getBaseName().userFacingName();

    // ${MODULE}Version{Number,String} in ${Module}.h
    SmallString<32> VersionNameIdentPrefix { M.getName().str() };
    VersionNameIdentPrefix.append("Version");
    if (BaseName.startswith(VersionNameIdentPrefix.str())) {
      return true;
    }

    // Automatically mapped SIMD types
    auto IsGlobalSIMDType = llvm::StringSwitch<bool>(BaseName)
  #define MAP_SIMD_TYPE(C_TYPE, _, __) \
  .Case("swift_" #C_TYPE "2", true) \
  .Case("swift_" #C_TYPE "3", true) \
  .Case("swift_" #C_TYPE "4", true)
  #include "swift/ClangImporter/SIMDMappedTypes.def"
    .Case("SWIFT_TYPEDEFS", true)
    .Case("char16_t", true)
    .Case("char32_t", true)
    .Default(false);

    if (IsGlobalSIMDType) {
      return true;
    }

    if (IgnoreContext) {
      return false;
    }
  }

  // Check up the parent chain. Anything inside a privately named
  // thing is also private. We could be looking at the `B` of `_A.B`.
  if (const auto *DC = D->getDeclContext()) {
    if (const auto *Parent = DC->getAsDecl()) {
      return isImplicitlyPrivate(Parent, IgnoreContext);
    }
  }
  return false;
}

/// Returns `true` if the symbol should be included as a node in the graph.
bool SymbolGraph::canIncludeDeclAsNode(const Decl *D) const {
  // If this decl isn't in this module, don't record it,
  // as it will appear elsewhere in its module's symbol graph.
  if (D->getModuleContext()->getName() != M.getName()) {
    return false;
  }

  if (!isa<ValueDecl>(D)) {
    return false;
  }
  return !isImplicitlyPrivate(cast<ValueDecl>(D));
}
