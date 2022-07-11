//===--- ASTContext.cpp - ASTContext Implementation -----------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//  This file implements the ASTContext class.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/ASTContext.h"
#include "ClangTypeConverter.h"
#include "ForeignRepresentationInfo.h"
#include "GenericSignatureBuilder.h"
#include "SubstitutionMapStorage.h"
#include "swift/AST/ClangModuleLoader.h"
#include "swift/AST/ConcreteDeclRef.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/DiagnosticsSema.h"
#include "swift/AST/ExistentialLayout.h"
#include "swift/AST/FileUnit.h"
#include "swift/AST/ForeignAsyncConvention.h"
#include "swift/AST/ForeignErrorConvention.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/GenericSignature.h"
#include "swift/AST/ImportCache.h"
#include "swift/AST/IndexSubset.h"
#include "swift/AST/KnownProtocols.h"
#include "swift/AST/LazyResolver.h"
#include "swift/AST/ModuleDependencies.h"
#include "swift/AST/ModuleLoader.h"
#include "swift/AST/NameLookup.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/PropertyWrappers.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/RawComment.h"
#include "swift/AST/SearchPathOptions.h"
#include "swift/AST/SILLayout.h"
#include "swift/AST/SemanticAttrs.h"
#include "swift/AST/SourceFile.h"
#include "swift/AST/SubstitutionMap.h"
#include "swift/AST/TypeCheckRequests.h"
#include "swift/Basic/Compiler.h"
#include "swift/Basic/SourceManager.h"
#include "swift/Basic/Statistic.h"
#include "swift/Basic/StringExtras.h"
#include "swift/Strings.h"
#include "swift/Subsystems.h"
#include "swift/SymbolGraphGen/SymbolGraphOptions.h"
#include "swift/Syntax/References.h"
#include "swift/Syntax/SyntaxArena.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <memory>

#include "RequirementMachine/RewriteContext.h"

using namespace swift;

#define DEBUG_TYPE "ASTContext"
STATISTIC(NumRegisteredGenericSignatureBuilders,
          "# of generic signature builders successfully registered");
STATISTIC(NumRegisteredGenericSignatureBuildersAlready,
          "# of generic signature builders already registered");
STATISTIC(NumCollapsedSpecializedProtocolConformances,
          "# of specialized protocol conformances collapsed");

/// Define this to 1 to enable expensive assertions of the
/// GenericSignatureBuilder.
#define SWIFT_GSB_EXPENSIVE_ASSERTIONS 0

void ModuleLoader::anchor() {}
void ClangModuleLoader::anchor() {}

llvm::StringRef swift::getProtocolName(KnownProtocolKind kind) {
  switch (kind) {
#define PROTOCOL_WITH_NAME(Id, Name) \
  case KnownProtocolKind::Id: \
    return Name;
#include "swift/AST/KnownProtocols.def"
  }
  llvm_unreachable("bad KnownProtocolKind");
}

namespace {
  enum class SearchPathKind : uint8_t {
    Import = 1 << 0,
    Framework = 1 << 1
  };
} // end anonymous namespace

using AssociativityCacheType =
  llvm::DenseMap<std::pair<PrecedenceGroupDecl *, PrecedenceGroupDecl *>,
                 Associativity>;

struct OverrideSignatureKey {
  GenericSignature baseMethodSig;
  GenericSignature derivedMethodSig;
  Decl *subclassDecl;

  OverrideSignatureKey(GenericSignature baseMethodSignature,
                       GenericSignature derivedMethodSignature,
                       Decl *subclassDecl)
    : baseMethodSig(baseMethodSignature),
      derivedMethodSig(derivedMethodSignature),
      subclassDecl(subclassDecl) {}
};

namespace llvm {
template <> struct DenseMapInfo<OverrideSignatureKey> {
  using Type = swift::Type;
  using GenericSignature = swift::GenericSignature;

  static bool isEqual(const OverrideSignatureKey lhs,
                      const OverrideSignatureKey rhs) {
    return lhs.baseMethodSig.getPointer() == rhs.baseMethodSig.getPointer() &&
           lhs.derivedMethodSig.getPointer() == rhs.derivedMethodSig.getPointer() &&
           lhs.subclassDecl == rhs.subclassDecl;
  }

  static inline OverrideSignatureKey getEmptyKey() {
    return OverrideSignatureKey(DenseMapInfo<GenericSignature>::getEmptyKey(),
                                DenseMapInfo<GenericSignature>::getEmptyKey(),
                                DenseMapInfo<Decl *>::getEmptyKey());
  }

  static inline OverrideSignatureKey getTombstoneKey() {
    return OverrideSignatureKey(
        DenseMapInfo<GenericSignature>::getTombstoneKey(),
        DenseMapInfo<GenericSignature>::getTombstoneKey(),
        DenseMapInfo<Decl *>::getTombstoneKey());
  }

  static unsigned getHashValue(const OverrideSignatureKey &Val) {
    return hash_combine(
        DenseMapInfo<GenericSignature>::getHashValue(Val.baseMethodSig),
        DenseMapInfo<GenericSignature>::getHashValue(Val.derivedMethodSig),
        DenseMapInfo<Decl *>::getHashValue(Val.subclassDecl));
  }
};
} // namespace llvm

struct ASTContext::Implementation {
  Implementation();
  ~Implementation();

  llvm::BumpPtrAllocator Allocator; // used in later initializations

  /// The set of cleanups to be called when the ASTContext is destroyed.
  std::vector<std::function<void(void)>> Cleanups;

  /// The set of top-level modules we have loaded.
  /// This map is used for iteration, therefore it's a MapVector and not a
  /// DenseMap.
  llvm::MapVector<Identifier, ModuleDecl *> LoadedModules;

  /// The set of top-level modules we have loaded, indexed by ABI name.
  llvm::MapVector<Identifier, ModuleDecl *> LoadedModulesByABIName;

  // FIXME: This is a StringMap rather than a StringSet because StringSet
  // doesn't allow passing in a pre-existing allocator.
  llvm::StringMap<Identifier::Aligner, llvm::BumpPtrAllocator&>
  IdentifierTable;

  /// The declaration of Swift.AssignmentPrecedence.
  PrecedenceGroupDecl *AssignmentPrecedence = nullptr;

  /// The declaration of Swift.CastingPrecedence.
  PrecedenceGroupDecl *CastingPrecedence = nullptr;

  /// The declaration of Swift.FunctionArrowPrecedence.
  PrecedenceGroupDecl *FunctionArrowPrecedence = nullptr;

  /// The declaration of Swift.TernaryPrecedence.
  PrecedenceGroupDecl *TernaryPrecedence = nullptr;

  /// The declaration of Swift.DefaultPrecedence.
  PrecedenceGroupDecl *DefaultPrecedence = nullptr;

  /// The AnyObject type.
  CanType AnyObjectType;

#define KNOWN_STDLIB_TYPE_DECL(NAME, DECL_CLASS, NUM_GENERIC_PARAMS) \
  /** The declaration of Swift.NAME. */ \
  DECL_CLASS *NAME##Decl = nullptr;
#include "swift/AST/KnownStdlibTypes.def"

#define KNOWN_SDK_TYPE_DECL(MODULE, NAME, DECL_CLASS, NUM_GENERIC_PARAMS) \
  /** The declaration of MODULE.NAME. */ \
  DECL_CLASS *NAME##Decl = nullptr;
#include "swift/AST/KnownSDKTypes.def"

  /// The declaration of '+' function for two RangeReplaceableCollection.
  FuncDecl *PlusFunctionOnRangeReplaceableCollection = nullptr;

  /// The declaration of '+' function for two String.
  FuncDecl *PlusFunctionOnString = nullptr;

  /// The declaration of 'Sequence.makeIterator()'.
  FuncDecl *MakeIterator = nullptr;

  /// The declaration of 'AsyncSequence.makeAsyncIterator()'.
  FuncDecl *MakeAsyncIterator = nullptr;

  /// The declaration of Swift.Optional<T>.Some.
  EnumElementDecl *OptionalSomeDecl = nullptr;

  /// The declaration of Swift.Optional<T>.None.
  EnumElementDecl *OptionalNoneDecl = nullptr;

  /// The declaration of Swift.Void.
  TypeAliasDecl *VoidDecl = nullptr;

  /// The declaration of Swift.UnsafeMutableRawPointer.memory.
  VarDecl *UnsafeMutableRawPointerMemoryDecl = nullptr;

  /// The declaration of Swift.UnsafeRawPointer.memory.
  VarDecl *UnsafeRawPointerMemoryDecl = nullptr;

  /// The declaration of Swift.UnsafeMutablePointer<T>.memory.
  VarDecl *UnsafeMutablePointerMemoryDecl = nullptr;
  
  /// The declaration of Swift.UnsafePointer<T>.memory.
  VarDecl *UnsafePointerMemoryDecl = nullptr;
  
  /// The declaration of Swift.AutoreleasingUnsafeMutablePointer<T>.memory.
  VarDecl *AutoreleasingUnsafeMutablePointerMemoryDecl = nullptr;

  /// The declaration of _Concurrency.DefaultActor.
  ClassDecl *DefaultActorDecl = nullptr;

  /// The declaration of _Concurrency.NSObjectDefaultActor.
  ClassDecl *NSObjectDefaultActorDecl = nullptr;

  // Declare cached declarations for each of the known declarations.
#define FUNC_DECL(Name, Id) FuncDecl *Get##Name = nullptr;
#include "swift/AST/KnownDecls.def"

  // Declare cached declarations for each of the known declarations.
#define KNOWN_SDK_FUNC_DECL(Module, Name, Id) FuncDecl *Get##Name = nullptr;
#include "swift/AST/KnownSDKDecls.def"
  
  /// func <Int, Int) -> Bool
  FuncDecl *LessThanIntDecl = nullptr;
  
  /// func ==(Int, Int) -> Bool
  FuncDecl *EqualIntDecl = nullptr;

  /// func _hashValue<H: Hashable>(for: H) -> Int
  FuncDecl *HashValueForDecl = nullptr;

  /// func append(Element) -> void
  FuncDecl *ArrayAppendElementDecl = nullptr;

  /// init(Builtin.RawPointer, Builtin.Word, Builtin.Int1)
  ConstructorDecl *MakeUTF8StringDecl = nullptr;

  /// func reserveCapacityForAppend(newElementsCount: Int)
  FuncDecl *ArrayReserveCapacityDecl = nullptr;

  /// func _stdlib_isOSVersionAtLeast(Builtin.Word,Builtin.Word, Builtin.word)
  //    -> Builtin.Int1
  FuncDecl *IsOSVersionAtLeastDecl = nullptr;
  
  /// The set of known protocols, lazily populated as needed.
  ProtocolDecl *KnownProtocols[NumKnownProtocols] = { };

  /// The module interface checker owned by the ASTContext.
  std::unique_ptr<ModuleInterfaceChecker> InterfaceChecker;

  /// The various module loaders that import external modules into this
  /// ASTContext.
  SmallVector<std::unique_ptr<swift::ModuleLoader>, 4> ModuleLoaders;

  /// Singleton used to cache the import graph.
  swift::namelookup::ImportCache TheImportCache;

  /// The module loader used to load Clang modules.
  ClangModuleLoader *TheClangModuleLoader = nullptr;

  /// The module loader used to load Clang modules from DWARF.
  ClangModuleLoader *TheDWARFModuleLoader = nullptr;

  /// Map from Swift declarations to deserialized resolved locations, ie.
  /// actual \c SourceLocs that require opening their external buffer.
  llvm::DenseMap<const Decl *, ExternalSourceLocs *> ExternalSourceLocs;

  /// Map from Swift declarations to raw comments.
  llvm::DenseMap<const Decl *, std::pair<RawComment, bool>> RawComments;

  /// Map from Swift declarations to brief comments.
  llvm::DenseMap<const Decl *, StringRef> BriefComments;

  /// Map from declarations to foreign error conventions.
  /// This applies to both actual imported functions and to @objc functions.
  llvm::DenseMap<const AbstractFunctionDecl *,
                 ForeignErrorConvention> ForeignErrorConventions;

  /// Map from declarations to foreign async conventions.
  llvm::DenseMap<const AbstractFunctionDecl *,
                 ForeignAsyncConvention> ForeignAsyncConventions;

  /// Cache of previously looked-up precedence queries.
  AssociativityCacheType AssociativityCache;

  /// Map from normal protocol conformances to diagnostics that have
  /// been delayed until the conformance is fully checked.
  llvm::DenseMap<NormalProtocolConformance *,
                 std::vector<ASTContext::DelayedConformanceDiag>>
    DelayedConformanceDiags;

  /// Map from normal protocol conformances to missing witnesses that have
  /// been delayed until the conformance is fully checked, so that we can
  /// issue a fixit that fills the entire protocol stub.
  llvm::DenseMap<
      NormalProtocolConformance *, std::unique_ptr<MissingWitnessesBase>>
    DelayedMissingWitnesses;

  /// Stores information about lazy deserialization of various declarations.
  llvm::DenseMap<const DeclContext *, LazyContextData *> LazyContexts;

  /// The single-parameter generic signature with no constraints, <T>.
  CanGenericSignature SingleGenericParameterSignature;

  /// The existential signature <T : P> for each P.
  llvm::DenseMap<CanType, CanGenericSignature> ExistentialSignatures;

  /// Overridden declarations.
  llvm::DenseMap<const ValueDecl *, ArrayRef<ValueDecl *>> Overrides;

  /// Default witnesses.
  llvm::DenseMap<std::pair<const ProtocolDecl *, ValueDecl *>, Witness>
    DefaultWitnesses;

  /// Default type witnesses for protocols.
  llvm::DenseMap<std::pair<const ProtocolDecl *, AssociatedTypeDecl *>, Type>
    DefaultTypeWitnesses;

  /// Default associated conformance witnesses for protocols.
  llvm::DenseMap<std::tuple<const ProtocolDecl *, CanType, ProtocolDecl *>,
                 ProtocolConformanceRef>
    DefaultAssociatedConformanceWitnesses;

  /// Caches of default types for DefaultTypeRequest.
  /// Used to be instance variables in the TypeChecker.
  /// There is a logically separate cache for each SourceFile and
  /// KnownProtocolKind.
  llvm::DenseMap<SourceFile *, std::array<Type, NumKnownProtocols>>
      DefaultTypeRequestCaches;

  /// Mapping from property declarations to the backing variable types.
  llvm::DenseMap<const VarDecl *, Type> PropertyWrapperBackingVarTypes;

  /// A mapping from the backing storage of a property that has a wrapper
  /// to the original property with the wrapper.
  llvm::DenseMap<const VarDecl *, VarDecl *> OriginalWrappedProperties;

  /// The builtin initializer witness for a literal. Used when building
  /// LiteralExprs in fully-checked AST.
  llvm::DenseMap<const NominalTypeDecl *, ConcreteDeclRef> BuiltinInitWitness;

  /// Mapping from the function decl to its original body's source range. This
  /// is populated if the body is reparsed from other source buffers.
  llvm::DenseMap<const AbstractFunctionDecl *, SourceRange> OriginalBodySourceRanges;

  /// Structure that captures data that is segregated into different
  /// arenas.
  struct Arena {
    static_assert(alignof(TypeBase) >= 8, "TypeBase not 8-byte aligned?");
    static_assert(alignof(TypeBase) > static_cast<unsigned>(
               MetatypeRepresentation::Last_MetatypeRepresentation) + 1,
               "Use std::pair for MetatypeTypes and ExistentialMetatypeTypes.");

    llvm::DenseMap<Type, ErrorType *> ErrorTypesWithOriginal;
    llvm::FoldingSet<TypeAliasType> TypeAliasTypes;
    llvm::FoldingSet<TupleType> TupleTypes;
    llvm::DenseMap<llvm::PointerIntPair<TypeBase*, 3, unsigned>,
                   MetatypeType*> MetatypeTypes;
    llvm::DenseMap<llvm::PointerIntPair<TypeBase*, 3, unsigned>,
                   ExistentialMetatypeType*> ExistentialMetatypeTypes;
    llvm::DenseMap<Type, ArraySliceType*> ArraySliceTypes;
    llvm::DenseMap<Type, VariadicSequenceType*> VariadicSequenceTypes;
    llvm::DenseMap<std::pair<Type, Type>, DictionaryType *> DictionaryTypes;
    llvm::DenseMap<Type, OptionalType*> OptionalTypes;
    llvm::DenseMap<Type, ParenType*> SimpleParenTypes; // Most are simple
    llvm::DenseMap<std::pair<Type, unsigned>, ParenType*> ParenTypes;
    llvm::DenseMap<uintptr_t, ReferenceStorageType*> ReferenceStorageTypes;
    llvm::DenseMap<Type, LValueType*> LValueTypes;
    llvm::DenseMap<Type, InOutType*> InOutTypes;
    llvm::DenseMap<std::pair<Type, void*>, DependentMemberType *>
      DependentMemberTypes;
    llvm::DenseMap<Type, DynamicSelfType *> DynamicSelfTypes;
    llvm::DenseMap<std::pair<EnumDecl*, Type>, EnumType*> EnumTypes;
    llvm::DenseMap<std::pair<StructDecl*, Type>, StructType*> StructTypes;
    llvm::DenseMap<std::pair<ClassDecl*, Type>, ClassType*> ClassTypes;
    llvm::DenseMap<std::pair<ProtocolDecl*, Type>, ProtocolType*> ProtocolTypes;
    llvm::DenseMap<Type, ExistentialType *> ExistentialTypes;
    llvm::FoldingSet<UnboundGenericType> UnboundGenericTypes;
    llvm::FoldingSet<BoundGenericType> BoundGenericTypes;
    llvm::FoldingSet<ProtocolCompositionType> ProtocolCompositionTypes;
    llvm::FoldingSet<LayoutConstraintInfo> LayoutConstraints;
    llvm::FoldingSet<OpaqueTypeArchetypeType> OpaqueArchetypes;

    /// The set of function types.
    llvm::FoldingSet<FunctionType> FunctionTypes;

    /// The set of normal protocol conformances.
    llvm::FoldingSet<NormalProtocolConformance> NormalConformances;

    // The set of self protocol conformances.
    llvm::DenseMap<ProtocolDecl*, SelfProtocolConformance*> SelfConformances;

    /// The set of specialized protocol conformances.
    llvm::FoldingSet<SpecializedProtocolConformance> SpecializedConformances;

    /// The set of inherited protocol conformances.
    llvm::FoldingSet<InheritedProtocolConformance> InheritedConformances;

    /// The set of builtin protocol conformances.
    llvm::DenseMap<std::pair<Type, ProtocolDecl *>,
                   BuiltinProtocolConformance *> BuiltinConformances;

    /// The set of substitution maps (uniqued by their storage).
    llvm::FoldingSet<SubstitutionMap::Storage> SubstitutionMaps;

    ~Arena() {
      for (auto &conformance : SpecializedConformances)
        conformance.~SpecializedProtocolConformance();
      // Work around MSVC warning: local variable is initialized but
      // not referenced.
#if SWIFT_COMPILER_IS_MSVC
#pragma warning (disable: 4189)
#endif
      for (auto &conformance : InheritedConformances)
        conformance.~InheritedProtocolConformance();
#if SWIFT_COMPILER_IS_MSVC
#pragma warning (default: 4189)
#endif

      // Call the normal conformance destructors last since they could be
      // referenced by the other conformance types.
      for (auto &conformance : NormalConformances)
        conformance.~NormalProtocolConformance();
    }

    size_t getTotalMemory() const;
  };

  llvm::DenseMap<ModuleDecl*, ModuleType*> ModuleTypes;
  llvm::DenseMap<std::pair<unsigned, unsigned>, GenericTypeParamType *>
    GenericParamTypes;
  llvm::FoldingSet<GenericFunctionType> GenericFunctionTypes;
  llvm::FoldingSet<SILFunctionType> SILFunctionTypes;
  llvm::DenseMap<CanType, SILBlockStorageType *> SILBlockStorageTypes;
  llvm::FoldingSet<SILBoxType> SILBoxTypes;
  llvm::DenseMap<BuiltinIntegerWidth, BuiltinIntegerType*> IntegerTypes;
  llvm::FoldingSet<BuiltinVectorType> BuiltinVectorTypes;
  llvm::FoldingSet<DeclName::CompoundDeclName> CompoundNames;
  llvm::DenseMap<UUID, OpenedArchetypeType *> OpenedExistentialArchetypes;
  llvm::FoldingSet<IndexSubset> IndexSubsets;
  llvm::FoldingSet<AutoDiffDerivativeFunctionIdentifier>
      AutoDiffDerivativeFunctionIdentifiers;

  llvm::FoldingSet<GenericSignatureImpl> GenericSignatures;

  /// Stored generic signature builders for canonical generic signatures.
  llvm::DenseMap<GenericSignature, std::unique_ptr<GenericSignatureBuilder>>
    GenericSignatureBuilders;

  /// A cache of information about whether particular nominal types
  /// are representable in a foreign language.
  llvm::DenseMap<NominalTypeDecl *, ForeignRepresentationInfo>
    ForeignRepresentableCache;

  llvm::StringMap<OptionSet<SearchPathKind>> SearchPathsSet;

  /// The permanent arena.
  Arena Permanent;

  /// Temporary arena used for a constraint solver.
  struct ConstraintSolverArena : public Arena {
    /// The allocator used for all allocations within this arena.
    llvm::BumpPtrAllocator &Allocator;

    ConstraintSolverArena(llvm::BumpPtrAllocator &allocator)
      : Allocator(allocator) { }

    ConstraintSolverArena(const ConstraintSolverArena &) = delete;
    ConstraintSolverArena(ConstraintSolverArena &&) = delete;
    ConstraintSolverArena &operator=(const ConstraintSolverArena &) = delete;
    ConstraintSolverArena &operator=(ConstraintSolverArena &&) = delete;
  };

  /// The current constraint solver arena, if any.
  std::unique_ptr<ConstraintSolverArena> CurrentConstraintSolverArena;

  Arena &getArena(AllocationArena arena) {
    switch (arena) {
    case AllocationArena::Permanent:
      return Permanent;

    case AllocationArena::ConstraintSolver:
      assert(CurrentConstraintSolverArena && "No constraint solver active?");
      return *CurrentConstraintSolverArena;
    }
    llvm_unreachable("bad AllocationArena");
  }
  
  llvm::FoldingSet<SILLayout> SILLayouts;

  RC<syntax::SyntaxArena> TheSyntaxArena;

  llvm::DenseMap<OverrideSignatureKey, GenericSignature> overrideSigCache;

  Optional<ClangTypeConverter> Converter;

  /// The IRGen specific SIL transforms that have been registered.
  SILTransformCtors IRGenSILPasses;

#if !SWIFT_BUILD_ONLY_SYNTAXPARSERLIB
  /// The scratch context used to allocate intrinsic data on behalf of \c swift::IntrinsicInfo
  std::unique_ptr<llvm::LLVMContext> IntrinsicScratchContext;
#endif

  /// Memory allocation arena for the term rewriting system.
  std::unique_ptr<rewriting::RewriteContext> TheRewriteContext;
};

ASTContext::Implementation::Implementation()
    : IdentifierTable(Allocator),
      TheSyntaxArena(new syntax::SyntaxArena())
#if !SWIFT_BUILD_ONLY_SYNTAXPARSERLIB
      , IntrinsicScratchContext(new llvm::LLVMContext())
#endif
      {}
ASTContext::Implementation::~Implementation() {
  for (auto &cleanup : Cleanups)
    cleanup();
}

ConstraintCheckerArenaRAII::
ConstraintCheckerArenaRAII(ASTContext &self, llvm::BumpPtrAllocator &allocator)
  : Self(self), Data(self.getImpl().CurrentConstraintSolverArena.release())
{
  Self.getImpl().CurrentConstraintSolverArena.reset(
    new ASTContext::Implementation::ConstraintSolverArena(allocator));
}

ConstraintCheckerArenaRAII::~ConstraintCheckerArenaRAII() {
  Self.getImpl().CurrentConstraintSolverArena.reset(
    (ASTContext::Implementation::ConstraintSolverArena *)Data);
}

static ModuleDecl *createBuiltinModule(ASTContext &ctx) {
  auto M = ModuleDecl::create(ctx.getIdentifier(BUILTIN_NAME), ctx);
  M->addFile(*new (ctx) BuiltinUnit(*M));
  M->setHasResolvedImports();
  return M;
}

inline ASTContext::Implementation &ASTContext::getImpl() const {
  auto pointer = reinterpret_cast<char*>(const_cast<ASTContext*>(this));
  auto offset = llvm::alignAddr((void *)sizeof(*this),
                                llvm::Align(alignof(Implementation)));
  return *reinterpret_cast<Implementation*>(pointer + offset);
}

void ASTContext::operator delete(void *Data) throw() {
  AlignedFree(Data);
}

ASTContext *ASTContext::get(LangOptions &langOpts,
                            TypeCheckerOptions &typeckOpts, SILOptions &silOpts,
                            SearchPathOptions &SearchPathOpts,
                            ClangImporterOptions &ClangImporterOpts,
                            symbolgraphgen::SymbolGraphOptions &SymbolGraphOpts,
                            SourceManager &SourceMgr, DiagnosticEngine &Diags) {
  // If more than two data structures are concatentated, then the aggregate
  // size math needs to become more complicated due to per-struct alignment
  // constraints.
  auto align = std::max(alignof(ASTContext), alignof(Implementation));
  auto size = llvm::alignTo(sizeof(ASTContext) + sizeof(Implementation), align);
  auto mem = AlignedAlloc(size, align);
  auto impl = reinterpret_cast<void*>((char*)mem + sizeof(ASTContext));
  impl = reinterpret_cast<void *>(
      llvm::alignAddr(impl, llvm::Align(alignof(Implementation))));
  new (impl) Implementation();
  return new (mem)
      ASTContext(langOpts, typeckOpts, silOpts, SearchPathOpts,
                 ClangImporterOpts, SymbolGraphOpts, SourceMgr, Diags);
}

ASTContext::ASTContext(LangOptions &langOpts, TypeCheckerOptions &typeckOpts,
                       SILOptions &silOpts, SearchPathOptions &SearchPathOpts,
                       ClangImporterOptions &ClangImporterOpts,
                       symbolgraphgen::SymbolGraphOptions &SymbolGraphOpts,
                       SourceManager &SourceMgr, DiagnosticEngine &Diags)
    : LangOpts(langOpts), TypeCheckerOpts(typeckOpts), SILOpts(silOpts),
      SearchPathOpts(SearchPathOpts), ClangImporterOpts(ClangImporterOpts),
      SymbolGraphOpts(SymbolGraphOpts), SourceMgr(SourceMgr), Diags(Diags),
      evaluator(Diags, langOpts), TheBuiltinModule(createBuiltinModule(*this)),
      StdlibModuleName(getIdentifier(STDLIB_NAME)),
      SwiftShimsModuleName(getIdentifier(SWIFT_SHIMS_NAME)),
      TheErrorType(new (*this, AllocationArena::Permanent) ErrorType(
          *this, Type(), RecursiveTypeProperties::HasError)),
      TheUnresolvedType(new (*this, AllocationArena::Permanent)
                            UnresolvedType(*this)),
      TheEmptyTupleType(TupleType::get(ArrayRef<TupleTypeElt>(), *this)),
      TheAnyType(ProtocolCompositionType::get(*this, ArrayRef<Type>(),
                                              /*HasExplicitAnyObject=*/false)),
#define SINGLETON_TYPE(SHORT_ID, ID) \
    The##SHORT_ID##Type(new (*this, AllocationArena::Permanent) \
                          ID##Type(*this)),
#include "swift/AST/TypeNodes.def"
    TheIEEE32Type(new (*this, AllocationArena::Permanent)
                    BuiltinFloatType(BuiltinFloatType::IEEE32,*this)),
    TheIEEE64Type(new (*this, AllocationArena::Permanent)
                    BuiltinFloatType(BuiltinFloatType::IEEE64,*this)),
    TheIEEE16Type(new (*this, AllocationArena::Permanent)
                    BuiltinFloatType(BuiltinFloatType::IEEE16,*this)),
    TheIEEE80Type(new (*this, AllocationArena::Permanent)
                    BuiltinFloatType(BuiltinFloatType::IEEE80,*this)),
    TheIEEE128Type(new (*this, AllocationArena::Permanent)
                    BuiltinFloatType(BuiltinFloatType::IEEE128, *this)),
    ThePPC128Type(new (*this, AllocationArena::Permanent)
                    BuiltinFloatType(BuiltinFloatType::PPC128, *this)) {

  // Initialize all of the known identifiers.
#define IDENTIFIER_WITH_NAME(Name, IdStr) Id_##Name = getIdentifier(IdStr);
#include "swift/AST/KnownIdentifiers.def"

  // Record the initial set of search paths.
  for (StringRef path : SearchPathOpts.ImportSearchPaths)
    getImpl().SearchPathsSet[path] |= SearchPathKind::Import;
  for (const auto &framepath : SearchPathOpts.FrameworkSearchPaths)
    getImpl().SearchPathsSet[framepath.Path] |= SearchPathKind::Framework;

  // Register any request-evaluator functions available at the AST layer.
  registerAccessRequestFunctions(evaluator);
  registerNameLookupRequestFunctions(evaluator);
}

ASTContext::~ASTContext() {
  getImpl().~Implementation();
}

llvm::BumpPtrAllocator &ASTContext::getAllocator(AllocationArena arena) const {
  switch (arena) {
  case AllocationArena::Permanent:
    return getImpl().Allocator;

  case AllocationArena::ConstraintSolver:
    assert(getImpl().CurrentConstraintSolverArena != nullptr);
    return getImpl().CurrentConstraintSolverArena->Allocator;
  }
  llvm_unreachable("bad AllocationArena");
}

void *detail::allocateInASTContext(size_t bytes, const ASTContext &ctx,
                                   AllocationArena arena, unsigned alignment) {
  return ctx.Allocate(bytes, alignment, arena);
}

ImportPath::Raw
swift::detail::ImportPathBuilder_copyToImpl(ASTContext &ctx,
                                            ImportPath::Raw raw) {
  return ctx.AllocateCopy(raw);
}

Identifier
swift::detail::ImportPathBuilder_getIdentifierImpl(ASTContext &ctx,
                                                   StringRef string) {
  return ctx.getIdentifier(string);
}

/// Set a new stats reporter.
void ASTContext::setStatsReporter(UnifiedStatsReporter *stats) {
  if (stats) {
    stats->getFrontendCounters().NumASTBytesAllocated =
        getAllocator().getBytesAllocated();
  }
  evaluator.setStatsReporter(stats);
  Stats = stats;
}

RC<syntax::SyntaxArena> ASTContext::getSyntaxArena() const {
  return getImpl().TheSyntaxArena;
}

/// getIdentifier - Return the uniqued and AST-Context-owned version of the
/// specified string.
Identifier ASTContext::getIdentifier(StringRef Str) const {
  // Make sure null pointers stay null.
  if (Str.data() == nullptr)
    return Identifier(nullptr);

  auto pair = std::make_pair(Str, Identifier::Aligner());
  auto I = getImpl().IdentifierTable.insert(pair).first;
  return Identifier(I->getKeyData());
}

void ASTContext::lookupInModule(
    ModuleDecl *M,
    StringRef name,
    SmallVectorImpl<ValueDecl *> &results) const {
  if (!M)
    return;

  // Find all of the declarations with this name in the Swift module.
  auto identifier = getIdentifier(name);
  M->lookupValue(identifier, NLKind::UnqualifiedLookup, results);
}

void ASTContext::lookupInSwiftModule(
                   StringRef name,
                   SmallVectorImpl<ValueDecl *> &results) const {
  lookupInModule(getStdlibModule(), name, results);
}

FuncDecl *ASTContext::getPlusFunctionOnRangeReplaceableCollection() const {
  if (getImpl().PlusFunctionOnRangeReplaceableCollection) {
    return getImpl().PlusFunctionOnRangeReplaceableCollection;
  }
  // Find all of the declarations with this name in the Swift module.
  SmallVector<ValueDecl *, 1> Results;
  lookupInSwiftModule("+", Results);
  for (auto Result : Results) {
    if (auto *FD = dyn_cast<FuncDecl>(Result)) {
      if (!FD->getOperatorDecl())
        continue;
      for (auto Req: FD->getGenericRequirements()) {
        if (Req.getKind() == RequirementKind::Conformance &&
              Req.getProtocolDecl() ==
              getProtocol(KnownProtocolKind::RangeReplaceableCollection)) {
          getImpl().PlusFunctionOnRangeReplaceableCollection = FD;
        }
      }
    }
  }
  return getImpl().PlusFunctionOnRangeReplaceableCollection;
}

FuncDecl *ASTContext::getPlusFunctionOnString() const {
  if (getImpl().PlusFunctionOnString) {
    return getImpl().PlusFunctionOnString;
  }
  // Find all of the declarations with this name in the Swift module.
  SmallVector<ValueDecl *, 1> Results;
  lookupInSwiftModule("+", Results);
  for (auto Result : Results) {
    if (auto *FD = dyn_cast<FuncDecl>(Result)) {
      if (!FD->getOperatorDecl())
        continue;
      auto ResultType = FD->getResultInterfaceType();
      if (!ResultType->isString())
        continue;
      auto ParamList = FD->getParameters();
      if (ParamList->size() != 2)
        continue;
      if (ParamList->get(0)->getInterfaceType()->isString() &&
          ParamList->get(1)->getInterfaceType()->isString()) {
        getImpl().PlusFunctionOnString = FD;
        break;
      }
    }
  }
  return getImpl().PlusFunctionOnString;
}

FuncDecl *ASTContext::getSequenceMakeIterator() const {
  if (getImpl().MakeIterator) {
    return getImpl().MakeIterator;
  }

  auto proto = getProtocol(KnownProtocolKind::Sequence);
  if (!proto)
    return nullptr;

  for (auto result : proto->lookupDirect(Id_makeIterator)) {
    if (result->getDeclContext() != proto)
      continue;

    if (auto func = dyn_cast<FuncDecl>(result)) {
      if (func->getParameters()->size() != 0)
        continue;

      getImpl().MakeIterator = func;
      return func;
    }
  }

  return nullptr;
}

FuncDecl *ASTContext::getAsyncSequenceMakeAsyncIterator() const {
  if (getImpl().MakeAsyncIterator) {
    return getImpl().MakeAsyncIterator;
  }

  auto proto = getProtocol(KnownProtocolKind::AsyncSequence);
  if (!proto)
    return nullptr;

  for (auto result : proto->lookupDirect(Id_makeAsyncIterator)) {
    if (result->getDeclContext() != proto)
      continue;

    if (auto func = dyn_cast<FuncDecl>(result)) {
      if (func->getParameters()->size() != 0)
        continue;

      getImpl().MakeAsyncIterator = func;
      return func;
    }
  }

  return nullptr;
}

#define KNOWN_STDLIB_TYPE_DECL(NAME, DECL_CLASS, NUM_GENERIC_PARAMS) \
DECL_CLASS *ASTContext::get##NAME##Decl() const { \
  if (getImpl().NAME##Decl) \
    return getImpl().NAME##Decl; \
  SmallVector<ValueDecl *, 1> results; \
  lookupInSwiftModule(#NAME, results); \
  for (auto result : results) { \
    if (auto type = dyn_cast<DECL_CLASS>(result)) { \
      auto params = type->getGenericParams(); \
      if (NUM_GENERIC_PARAMS == (params == nullptr ? 0 : params->size())) { \
        getImpl().NAME##Decl = type; \
        return type; \
      } \
    } \
  } \
  return nullptr; \
} \
\
Type ASTContext::get##NAME##Type() const { \
  if (!get##NAME##Decl()) \
    return Type(); \
  return get##NAME##Decl()->getDeclaredInterfaceType(); \
}
#include "swift/AST/KnownStdlibTypes.def"

CanType ASTContext::getErrorExistentialType() const {
  if (auto exn = getErrorDecl()) {
    return exn->getExistentialType()->getCanonicalType();
  } else {
    // Use Builtin.NativeObject just as a stand-in.
    return TheNativeObjectType;
  }
}

ProtocolDecl *ASTContext::getErrorDecl() const {
  return getProtocol(KnownProtocolKind::Error);
}

EnumElementDecl *ASTContext::getOptionalSomeDecl() const {
  if (!getImpl().OptionalSomeDecl)
    getImpl().OptionalSomeDecl = getOptionalDecl()->getUniqueElement(/*hasVal*/true);
  return getImpl().OptionalSomeDecl;
}

EnumElementDecl *ASTContext::getOptionalNoneDecl() const {
  if (!getImpl().OptionalNoneDecl)
    getImpl().OptionalNoneDecl =getOptionalDecl()->getUniqueElement(/*hasVal*/false);
  return getImpl().OptionalNoneDecl;
}

TypeAliasDecl *ASTContext::getVoidDecl() const {
  if (getImpl().VoidDecl) {
    return getImpl().VoidDecl;
  }

  SmallVector<ValueDecl *, 1> results;
  lookupInSwiftModule("Void", results);
  for (auto result : results) {
    if (auto typealias = dyn_cast<TypeAliasDecl>(result)) {
      getImpl().VoidDecl = typealias;
      return typealias;
    }
  }

  return nullptr;
}

Type ASTContext::getVoidType() const {
  auto decl = getVoidDecl();
  if (!decl)
    return Type();
  return decl->getDeclaredInterfaceType();
}

static VarDecl *getPointeeProperty(VarDecl *&cache,
                           NominalTypeDecl *(ASTContext::*getNominal)() const,
                                  const ASTContext &ctx) {
  if (cache) return cache;

  // There must be a generic type with one argument.
  NominalTypeDecl *nominal = (ctx.*getNominal)();
  if (!nominal) return nullptr;
  auto sig = nominal->getGenericSignature();
  if (sig.getGenericParams().size() != 1) return nullptr;

  // There must be a property named "pointee".
  auto identifier = ctx.getIdentifier("pointee");
  auto results = nominal->lookupDirect(identifier);
  if (results.size() != 1) return nullptr;

  // The property must have type T.
  auto *property = dyn_cast<VarDecl>(results[0]);
  if (!property) return nullptr;
  if (!property->getInterfaceType()->isEqual(sig.getGenericParams()[0]))
    return nullptr;

  cache = property;
  return property;
}

VarDecl *
ASTContext::getPointerPointeePropertyDecl(PointerTypeKind ptrKind) const {
  switch (ptrKind) {
  case PTK_UnsafeMutableRawPointer:
    return getPointeeProperty(getImpl().UnsafeMutableRawPointerMemoryDecl,
                             &ASTContext::getUnsafeMutableRawPointerDecl,
                             *this);
  case PTK_UnsafeRawPointer:
    return getPointeeProperty(getImpl().UnsafeRawPointerMemoryDecl,
                             &ASTContext::getUnsafeRawPointerDecl,
                             *this);
  case PTK_UnsafeMutablePointer:
    return getPointeeProperty(getImpl().UnsafeMutablePointerMemoryDecl,
                             &ASTContext::getUnsafeMutablePointerDecl,
                             *this);
  case PTK_UnsafePointer:
    return getPointeeProperty(getImpl().UnsafePointerMemoryDecl,
                             &ASTContext::getUnsafePointerDecl,
                             *this);
  case PTK_AutoreleasingUnsafeMutablePointer:
    return getPointeeProperty(getImpl().AutoreleasingUnsafeMutablePointerMemoryDecl,
                         &ASTContext::getAutoreleasingUnsafeMutablePointerDecl,
                             *this);
  }
  llvm_unreachable("bad pointer kind");
}

CanType ASTContext::getAnyObjectType() const {
  if (getImpl().AnyObjectType) {
    return getImpl().AnyObjectType;
  }

  getImpl().AnyObjectType = CanType(
    ProtocolCompositionType::get(
      *this, {}, /*HasExplicitAnyObject=*/true));
  return getImpl().AnyObjectType;
}

#define KNOWN_SDK_TYPE_DECL(MODULE, NAME, DECLTYPE, GENERIC_ARGS) \
DECLTYPE *ASTContext::get##NAME##Decl() const { \
  if (!getImpl().NAME##Decl) { \
    if (ModuleDecl *M = getLoadedModule(Id_##MODULE)) { \
      /* Note: lookupQualified() will search both the Swift overlay \
       * and the Clang module it imports. */ \
      SmallVector<ValueDecl *, 1> decls; \
      M->lookupQualified(M, DeclNameRef(getIdentifier(#NAME)), NL_OnlyTypes, \
                         decls); \
      if (decls.size() == 1 && isa<DECLTYPE>(decls[0])) { \
        auto decl = cast<DECLTYPE>(decls[0]); \
        if (isa<ProtocolDecl>(decl) \
            || (bool)decl->getGenericParams() == (bool)GENERIC_ARGS) { \
          getImpl().NAME##Decl = decl; \
        } \
      } \
    } \
  } \
  \
  return getImpl().NAME##Decl; \
} \
\
Type ASTContext::get##NAME##Type() const { \
  auto *decl = get##NAME##Decl(); \
  if (!decl) \
    return Type(); \
  return decl->getDeclaredInterfaceType(); \
}

#include "swift/AST/KnownSDKTypes.def"

ProtocolDecl *ASTContext::getProtocol(KnownProtocolKind kind) const {
  // Check whether we've already looked for and cached this protocol.
  unsigned index = (unsigned)kind;
  assert(index < NumKnownProtocols && "Number of known protocols is wrong");
  if (getImpl().KnownProtocols[index])
    return getImpl().KnownProtocols[index];

  // Find all of the declarations with this name in the appropriate module.
  SmallVector<ValueDecl *, 1> results;

  const ModuleDecl *M;
  switch (kind) {
  case KnownProtocolKind::BridgedNSError:
  case KnownProtocolKind::BridgedStoredNSError:
  case KnownProtocolKind::ErrorCodeProtocol:
    M = getLoadedModule(Id_Foundation);
    break;
  case KnownProtocolKind::CFObject:
    M = getLoadedModule(Id_CoreFoundation);
    break;
  case KnownProtocolKind::Differentiable:
    M = getLoadedModule(Id_Differentiation);
    break;
  case KnownProtocolKind::Actor:
  case KnownProtocolKind::GlobalActor:
  case KnownProtocolKind::AsyncSequence:
  case KnownProtocolKind::AsyncIteratorProtocol:
  case KnownProtocolKind::SerialExecutor:
    M = getLoadedModule(Id_Concurrency);
    break;
  case KnownProtocolKind::DistributedActor:
  case KnownProtocolKind::ActorTransport:
  case KnownProtocolKind::ActorIdentity:
    M = getLoadedModule(Id_Distributed);
    break;
  default:
    M = getStdlibModule();
    break;
  }

  if (!M)
    return nullptr;
  M->lookupValue(getIdentifier(getProtocolName(kind)),
                 NLKind::UnqualifiedLookup, results);

  for (auto result : results) {
    if (auto protocol = dyn_cast<ProtocolDecl>(result)) {
      getImpl().KnownProtocols[index] = protocol;
      return protocol;
    }
  }

  return nullptr;
}

/// Find the implementation for the given "intrinsic" library function,
/// in the passed in module.
static FuncDecl *findLibraryIntrinsic(const ASTContext &ctx,
                                      ModuleDecl *M,
                                      StringRef name) {
  SmallVector<ValueDecl *, 1> results;
  ctx.lookupInModule(M, name, results);
  if (results.size() == 1)
    return dyn_cast_or_null<FuncDecl>(results.front());
  return nullptr;
}

/// Find the implementation for the given "intrinsic" library function.
static FuncDecl *findLibraryIntrinsic(const ASTContext &ctx,
                                      StringRef name) {
  return findLibraryIntrinsic(ctx, ctx.getStdlibModule(), name);
}

/// Returns the type of an intrinsic function if it is not generic, otherwise
/// returns nullptr.
static FunctionType *
getIntrinsicCandidateType(FuncDecl *fn, bool allowTypeMembers) {
  auto type = fn->getInterfaceType();
  if (allowTypeMembers && fn->getDeclContext()->isTypeContext()) {
    auto fnType = type->getAs<FunctionType>();
    if (!fnType) return nullptr;

    type = fnType->getResult();
  }
  return type->getAs<FunctionType>();
}

/// Check whether the given type is Builtin.Int1.
static bool isBuiltinInt1Type(Type type) {
  if (auto intType = type->getAs<BuiltinIntegerType>())
    return intType->isFixedWidth() && intType->getFixedWidth() == 1;
  return false;
}

/// Check whether the given type is Builtin.Word.
static bool isBuiltinWordType(Type type) {
  if (auto intType = type->getAs<BuiltinIntegerType>())
    return intType->getWidth().isPointerWidth();
  return false;
}

/// Looks up all implementations of an operator (globally and declared in types)
/// and passes potential matches to the given callback. The search stops when
/// the predicate returns true (in which case the matching function declaration
/// is returned); otherwise, nullptr is returned if there are no matches.
/// \p C The AST context.
/// \p oper The name of the operator.
/// \p contextType If the operator is declared on a type, then only operators
///     defined on this type should be considered.
/// \p pred A callback predicate that takes as its argument the type of a
///     candidate function declaration and returns true if the function matches
///     the desired criteria.
/// \return The matching function declaration, or nullptr if there was no match.
static FuncDecl *
lookupOperatorFunc(const ASTContext &ctx, StringRef oper, Type contextType,
                   llvm::function_ref<bool(FunctionType *)> pred) {
  SmallVector<ValueDecl *, 32> candidates;
  ctx.lookupInSwiftModule(oper, candidates);

  for (auto candidate : candidates) {
    // All operator declarations should be functions, but make sure.
    auto *fnDecl = dyn_cast<FuncDecl>(candidate);
    if (!fnDecl)
      continue;

    if (fnDecl->getDeclContext()->isTypeContext()) {
      auto contextTy = fnDecl->getDeclContext()->getDeclaredInterfaceType();
      if (!contextTy->isEqual(contextType)) continue;
    }

    auto *funcTy = getIntrinsicCandidateType(fnDecl, /*allowTypeMembers=*/true);
    if (!funcTy)
      continue;

    if (pred(funcTy))
      return fnDecl;
  }

  return nullptr;
}

ConcreteDeclRef ASTContext::getBoolBuiltinInitDecl() const {
  auto fn = [&](ASTContext &ctx) {
    return DeclName(ctx, DeclBaseName::createConstructor(),
                    { Id_builtinBooleanLiteral });
  };
  auto builtinProtocolKind =
    KnownProtocolKind::ExpressibleByBuiltinBooleanLiteral;
  return getBuiltinInitDecl(getBoolDecl(), builtinProtocolKind, fn);
}

ConcreteDeclRef
ASTContext::getIntBuiltinInitDecl(NominalTypeDecl *intDecl) const {
  auto fn = [&](ASTContext &ctx) {
    return DeclName(ctx, DeclBaseName::createConstructor(),
                    { Id_builtinIntegerLiteral });
  };
  auto builtinProtocolKind =
    KnownProtocolKind::ExpressibleByBuiltinIntegerLiteral;
  return getBuiltinInitDecl(intDecl, builtinProtocolKind, fn);
}

ConcreteDeclRef
ASTContext::getFloatBuiltinInitDecl(NominalTypeDecl *floatDecl) const {
  auto fn = [&](ASTContext &ctx) {
    return DeclName(ctx, DeclBaseName::createConstructor(),
                    { Id_builtinFloatLiteral });
  };

  auto builtinProtocolKind =
    KnownProtocolKind::ExpressibleByBuiltinFloatLiteral;
  return getBuiltinInitDecl(floatDecl, builtinProtocolKind, fn);
}

ConcreteDeclRef
ASTContext::getStringBuiltinInitDecl(NominalTypeDecl *stringDecl) const {
  auto fn = [&](ASTContext &ctx) {
    return DeclName(ctx, DeclBaseName::createConstructor(),
                    { Id_builtinStringLiteral,
                      getIdentifier("utf8CodeUnitCount"),
                      getIdentifier("isASCII") });
  };

  auto builtinProtocolKind =
    KnownProtocolKind::ExpressibleByBuiltinStringLiteral;
  return getBuiltinInitDecl(stringDecl, builtinProtocolKind, fn);
}

ConcreteDeclRef
ASTContext::getBuiltinInitDecl(NominalTypeDecl *decl,
                               KnownProtocolKind builtinProtocolKind,
               llvm::function_ref<DeclName (ASTContext &ctx)> initName) const {
  auto &witness = getImpl().BuiltinInitWitness[decl];
  if (witness)
    return witness;

  auto type = decl->getDeclaredInterfaceType();
  auto builtinProtocol = getProtocol(builtinProtocolKind);
  auto builtinConformance = getStdlibModule()->lookupConformance(
      type, builtinProtocol);
  if (builtinConformance.isInvalid()) {
    assert(false && "Missing required conformance");
    witness = ConcreteDeclRef();
    return witness;
  }

  auto *ctx = const_cast<ASTContext *>(this);
  witness = builtinConformance.getWitnessByName(type, initName(*ctx));
  if (!witness) {
    assert(false && "Missing required witness");
    witness = ConcreteDeclRef();
    return witness;
  }

  return witness;
}

static 
FuncDecl *getBinaryComparisonOperatorIntDecl(const ASTContext &C, StringRef op, FuncDecl *&cached) {
  if (cached)
    return cached;

  if (!C.getIntDecl() || !C.getBoolDecl())
    return nullptr;

  auto isIntParam = [&](AnyFunctionType::Param param) {
    return (!param.isVariadic() && !param.isInOut() &&
            param.getPlainType()->isInt());
  };

  auto decl = lookupOperatorFunc(C, op, C.getIntType(),
                                 [=](FunctionType *type) {
    // Check for the signature: (Int, Int) -> Bool
    if (type->getParams().size() != 2) return false;
    if (!isIntParam(type->getParams()[0]) ||
        !isIntParam(type->getParams()[1])) return false;
    return type->getResult()->isBool();
  });
  cached = decl;
  return decl;
}
FuncDecl *ASTContext::getLessThanIntDecl() const {
  return getBinaryComparisonOperatorIntDecl(*this, "<", getImpl().LessThanIntDecl);
}
FuncDecl *ASTContext::getEqualIntDecl() const {
  return getBinaryComparisonOperatorIntDecl(*this, "==", getImpl().EqualIntDecl);
}

FuncDecl *ASTContext::getHashValueForDecl() const {
  if (getImpl().HashValueForDecl)
    return getImpl().HashValueForDecl;

  SmallVector<ValueDecl *, 1> results;
  lookupInSwiftModule("_hashValue", results);
  for (auto result : results) {
    auto *fd = dyn_cast<FuncDecl>(result);
    if (!fd)
      continue;
    auto paramList = fd->getParameters();
    if (paramList->size() != 1)
      continue;
    auto paramDecl = paramList->get(0);
    if (paramDecl->getArgumentName() != Id_for)
      continue;
    auto genericParams = fd->getGenericParams();
    if (!genericParams || genericParams->size() != 1)
      continue;
    getImpl().HashValueForDecl = fd;
    return fd;
  }
  return nullptr;
}

FuncDecl *ASTContext::getArrayAppendElementDecl() const {
  if (getImpl().ArrayAppendElementDecl)
    return getImpl().ArrayAppendElementDecl;

  auto AppendFunctions = getArrayDecl()->lookupDirect(getIdentifier("append"));

  for (auto CandidateFn : AppendFunctions) {
    auto FnDecl = dyn_cast<FuncDecl>(CandidateFn);
    auto Attrs = FnDecl->getAttrs();
    for (auto *A : Attrs.getAttributes<SemanticsAttr, false>()) {
      if (A->Value != "array.append_element")
        continue;

      auto SelfDecl = FnDecl->getImplicitSelfDecl();
      if (!SelfDecl->isInOut())
        return nullptr;

      auto SelfInOutTy = SelfDecl->getInterfaceType();

      if (!SelfInOutTy->isArray())
        return nullptr;

      auto ParamList = FnDecl->getParameters();
      if (ParamList->size() != 1)
        return nullptr;

      GenericTypeParamType *ElementType = ParamList->get(0)->
                             getInterfaceType()->getAs<GenericTypeParamType>();
      if (!ElementType)
        return nullptr;
      if (ElementType->getName() != getIdentifier("Element"))
        return nullptr;

      if (!FnDecl->getResultInterfaceType()->isVoid())
        return nullptr;

      getImpl().ArrayAppendElementDecl = FnDecl;
      return FnDecl;
    }
  }
  return nullptr;
}

FuncDecl *ASTContext::getArrayReserveCapacityDecl() const {
  if (getImpl().ArrayReserveCapacityDecl)
    return getImpl().ArrayReserveCapacityDecl;

  auto ReserveFunctions = getArrayDecl()->lookupDirect(
                                   getIdentifier("reserveCapacityForAppend"));

  for (auto CandidateFn : ReserveFunctions) {
    auto FnDecl = dyn_cast<FuncDecl>(CandidateFn);
    auto Attrs = FnDecl->getAttrs();
    for (auto *A : Attrs.getAttributes<SemanticsAttr, false>()) {
      if (A->Value != "array.reserve_capacity_for_append")
        continue;

      auto SelfDecl = FnDecl->getImplicitSelfDecl();
      if (!SelfDecl->isInOut())
        return nullptr;

      auto SelfInOutTy = SelfDecl->getInterfaceType();

      if (!SelfInOutTy->isArray())
        return nullptr;

      auto ParamList = FnDecl->getParameters();
      if (ParamList->size() != 1)
        return nullptr;
      StructType *IntType =
        ParamList->get(0)->getInterfaceType()->getAs<StructType>();
      if (!IntType)
        return nullptr;

      StructDecl *IntDecl = IntType->getDecl();
      auto StoredProperties = IntDecl->getStoredProperties();
      if (StoredProperties.size() != 1)
        return nullptr;
      VarDecl *field = StoredProperties[0];
      if (field->hasClangNode())
        return nullptr;
      if (!field->getInterfaceType()->is<BuiltinIntegerType>())
        return nullptr;

      if (!FnDecl->getResultInterfaceType()->isVoid())
        return nullptr;

      getImpl().ArrayReserveCapacityDecl = FnDecl;
      return FnDecl;
    }
  }
  return nullptr;
}

ConstructorDecl *ASTContext::getMakeUTF8StringDecl() const {
  if (getImpl().MakeUTF8StringDecl)
    return getImpl().MakeUTF8StringDecl;

  auto initializers =
    getStringDecl()->lookupDirect(DeclBaseName::createConstructor());

  for (Decl *initializer : initializers) {
    auto *constructor = cast<ConstructorDecl>(initializer);
    auto Attrs = constructor->getAttrs();
    for (auto *A : Attrs.getAttributes<SemanticsAttr, false>()) {
      if (A->Value != semantics::STRING_MAKE_UTF8)
        continue;
      auto ParamList = constructor->getParameters();
      if (ParamList->size() != 3)
        continue;
      ParamDecl *param = constructor->getParameters()->get(0);
      if (param->getArgumentName().str() != "_builtinStringLiteral")
        continue;

      getImpl().MakeUTF8StringDecl = constructor;
      return constructor;
    }
  }
  return nullptr;
}

FuncDecl *ASTContext::getIsOSVersionAtLeastDecl() const {
  if (getImpl().IsOSVersionAtLeastDecl)
    return getImpl().IsOSVersionAtLeastDecl;

  // Look for the function.
  auto decl =
      findLibraryIntrinsic(*this, "_stdlib_isOSVersionAtLeast");
  if (!decl)
    return nullptr;

  auto *fnType = getIntrinsicCandidateType(decl, /*allowTypeMembers=*/false);
  if (!fnType)
    return nullptr;

  // Input must be (Builtin.Word, Builtin.Word, Builtin.Word)
  auto intrinsicsParams = fnType->getParams();
  if (intrinsicsParams.size() != 3)
    return nullptr;

  if (llvm::any_of(intrinsicsParams, [](AnyFunctionType::Param param) {
    return (param.isVariadic() || param.isInOut() ||
            !isBuiltinWordType(param.getPlainType()));
  })) {
    return nullptr;
  }

  // Output must be Builtin.Int1
  if (!isBuiltinInt1Type(fnType->getResult()))
    return nullptr;

  getImpl().IsOSVersionAtLeastDecl = decl;
  return decl;
}

static bool isHigherPrecedenceThan(PrecedenceGroupDecl *a,
                                   PrecedenceGroupDecl *b) {
  assert(a != b && "exact match should already have been filtered");

  SmallVector<PrecedenceGroupDecl*, 4> stack;

  // Compute the transitive set of precedence groups that are
  // explicitly lower than 'b', including 'b' itself.  This is expected
  // to be very small, since it's only legal in downstream modules.
  SmallPtrSet<PrecedenceGroupDecl*, 4> targets;
  targets.insert(b);
  stack.push_back(b);
  do {
    auto cur = stack.pop_back_val();
    for (auto &rel : cur->getLowerThan()) {
      auto group = rel.Group;

      // If we ever see 'a', we're done.
      if (group == a) return true;

      // Protect against invalid ASTs where the group isn't actually set.
      if (!group) continue;

      // If we've already inserted this, don't add it to the queue.
      if (!targets.insert(group).second) continue;

      stack.push_back(group);
    }
  } while (!stack.empty());

  // Walk down the higherThan relationships from 'a' and look for
  // anything in the set we just built.
  stack.push_back(a);
  do {
    auto cur = stack.pop_back_val();
    assert(!targets.count(cur));

    for (auto &rel : cur->getHigherThan()) {
      auto group = rel.Group;

      if (!group) continue;

      // If we ever see a group that's in the targets set, we're done.
      if (targets.count(group)) return true;

      stack.push_back(group);
    }
  } while (!stack.empty());

  return false;
}

static Associativity computeAssociativity(AssociativityCacheType &cache,
                                          PrecedenceGroupDecl *left,
                                          PrecedenceGroupDecl *right) {
  auto it = cache.find({left, right});
  if (it != cache.end()) return it->second;

  auto result = Associativity::None;
  if (isHigherPrecedenceThan(left, right))
    result = Associativity::Left;
  else if (isHigherPrecedenceThan(right, left))
    result = Associativity::Right;
  cache.insert({{left, right}, result});
  return result;
}

Associativity
ASTContext::associateInfixOperators(PrecedenceGroupDecl *left,
                                    PrecedenceGroupDecl *right) const {
  // If the operators are in the same precedence group, use the group's
  // associativity.
  if (left == right) {
    return left->getAssociativity();
  }

  // This relationship is antisymmetric, so we can canonicalize to avoid
  // computing it twice.  Arbitrarily, if the pointer value of 'left'
  // is greater than the pointer value of 'right', we flip them and
  // then flip the result.

  if (uintptr_t(left) < uintptr_t(right)) {
    return computeAssociativity(getImpl().AssociativityCache, left, right);
  }

  switch (computeAssociativity(getImpl().AssociativityCache, right, left)) {
  case Associativity::Left: return Associativity::Right;
  case Associativity::Right: return Associativity::Left;
  case Associativity::None: return Associativity::None;
  }
  llvm_unreachable("bad associativity");
}

// Find library intrinsic function.
static FuncDecl *findLibraryFunction(const ASTContext &ctx, FuncDecl *&cache,
                                     StringRef name) {
  if (cache) return cache;

  // Look for a generic function.
  cache = findLibraryIntrinsic(ctx, name);
  return cache;
}

// Find library intrinsic function in passed in module
static FuncDecl *findLibraryFunction(const ASTContext &ctx,
                                     ModuleDecl *M, FuncDecl *&cache,
                                     StringRef name) {
  if (cache) return cache;

  // Look for a generic function.
  cache = findLibraryIntrinsic(ctx, M, name);
  return cache;
}

#define FUNC_DECL(Name, Id)                                    \
FuncDecl *ASTContext::get##Name() const {                      \
  return findLibraryFunction(*this, getImpl().Get##Name, Id);  \
}
#include "swift/AST/KnownDecls.def"

#define KNOWN_SDK_FUNC_DECL(Module, Name, Id)                                \
FuncDecl *ASTContext::get##Name() const {                                    \
  if (ModuleDecl *M = getLoadedModule(Id_##Module)) {                        \
    return findLibraryFunction(*this, M, getImpl().Get##Name, Id);           \
  } else {                                                                   \
    return findLibraryFunction(*this, getImpl().Get##Name, Id);              \
  }                                                                          \
}
#include "swift/AST/KnownSDKDecls.def"

bool ASTContext::hasOptionalIntrinsics() const {
  return getOptionalDecl() &&
         getOptionalSomeDecl() &&
         getOptionalNoneDecl() &&
         getDiagnoseUnexpectedNilOptional();
}

bool ASTContext::hasPointerArgumentIntrinsics() const {
  return getUnsafeMutableRawPointerDecl()
    && getUnsafeRawPointerDecl()
    && getUnsafeMutablePointerDecl()
    && getUnsafePointerDecl()
    && (!LangOpts.EnableObjCInterop || getAutoreleasingUnsafeMutablePointerDecl())
    && getUnsafeBufferPointerDecl()
    && getUnsafeMutableBufferPointerDecl()
    && getUnsafeRawBufferPointerDecl()
    && getUnsafeMutableRawBufferPointerDecl()
    && getConvertPointerToPointerArgument()
    && getConvertMutableArrayToPointerArgument()
    && getConvertConstArrayToPointerArgument()
    && getConvertConstStringToUTF8PointerArgument()
    && getConvertInOutToPointerArgument();
}

bool ASTContext::hasArrayLiteralIntrinsics() const {
  return getArrayDecl()
    && getAllocateUninitializedArray()
    && getDeallocateUninitializedArray();
}

void ASTContext::addCleanup(std::function<void(void)> cleanup) {
  getImpl().Cleanups.push_back(std::move(cleanup));
}

bool ASTContext::hadError() const {
  return Diags.hadAnyError();
}

/// Retrieve the arena from which we should allocate storage for a type.
static AllocationArena getArena(RecursiveTypeProperties properties) {
  bool hasTypeVariable = properties.hasTypeVariable();
  return hasTypeVariable ? AllocationArena::ConstraintSolver
                         : AllocationArena::Permanent;
}

void ASTContext::addSearchPath(StringRef searchPath, bool isFramework,
                               bool isSystem) {
  OptionSet<SearchPathKind> &loaded = getImpl().SearchPathsSet[searchPath];
  auto kind = isFramework ? SearchPathKind::Framework : SearchPathKind::Import;
  if (loaded.contains(kind))
    return;
  loaded |= kind;

  if (isFramework)
    SearchPathOpts.FrameworkSearchPaths.push_back({searchPath, isSystem});
  else
    SearchPathOpts.ImportSearchPaths.push_back(searchPath.str());

  if (auto *clangLoader = getClangModuleLoader())
    clangLoader->addSearchPath(searchPath, isFramework, isSystem);
}

void ASTContext::addModuleLoader(std::unique_ptr<ModuleLoader> loader,
                                 bool IsClang, bool IsDwarf, bool IsInterface) {
  if (IsClang && !IsDwarf && !getImpl().TheClangModuleLoader)
    getImpl().TheClangModuleLoader =
        static_cast<ClangModuleLoader *>(loader.get());
  if (IsClang && IsDwarf && !getImpl().TheDWARFModuleLoader)
    getImpl().TheDWARFModuleLoader =
        static_cast<ClangModuleLoader *>(loader.get());
  getImpl().ModuleLoaders.push_back(std::move(loader));
}

void ASTContext::addModuleInterfaceChecker(
    std::unique_ptr<ModuleInterfaceChecker> checker) {
  assert(!getImpl().InterfaceChecker && "Checker has been set already");
  getImpl().InterfaceChecker = std::move(checker);
}

void ASTContext::setModuleAliases(const llvm::StringMap<StringRef> &aliasMap) {
  // This setter should be called only once after ASTContext has been initialized
  assert(ModuleAliasMap.empty());
  
  for (auto k: aliasMap.keys()) {
    auto v = aliasMap.lookup(k);
    if (!v.empty()) {
      auto key = getIdentifier(k);
      auto val = getIdentifier(v);
      // key is a module alias, val is its corresponding real name
      ModuleAliasMap[key] = std::make_pair(val, true);
      // add an entry with an alias as key for an easier lookup later
      ModuleAliasMap[val] = std::make_pair(key, false);
    }
  }
}

Identifier ASTContext::getRealModuleName(Identifier key, ModuleAliasLookupOption option) const {
  auto found = ModuleAliasMap.find(key);
  if (found == ModuleAliasMap.end())
    return key; // No module aliasing was used, so just return the given key

  // Found an entry
  auto value = found->second;

  // With the alwaysRealName option, look up the real name by treating
  // the given key as an alias; if the key's not an alias, return the key
  // itself since that's the real name.
  if (option == ModuleAliasLookupOption::alwaysRealName) {
     return value.second ? value.first : key;
  }

  // With realNameFromAlias or aliasFromRealName option, only return the value
  // if the given key matches the description (whether it's an alias or real name)
  // by looking up the value.second (true if keyed by an alias). If not matched,
  // return an empty Identifier.
  if ((option == ModuleAliasLookupOption::realNameFromAlias && !value.second) ||
      (option == ModuleAliasLookupOption::aliasFromRealName && value.second))
      return Identifier();

  // Otherwise return the value found (whether the key is an alias or real name)
  return value.first;
}

Optional<ModuleDependencies> ASTContext::getModuleDependencies(
    StringRef moduleName, bool isUnderlyingClangModule,
    ModuleDependenciesCache &cache, InterfaceSubContextDelegate &delegate,
    bool cacheOnly) {
  // Retrieve the dependencies for this module.
  if (cacheOnly) {
    auto searchPathSet = getAllModuleSearchPathsSet();
    // Check whether we've cached this result.
    if (!isUnderlyingClangModule) {
      if (auto found = cache.findDependencies(
              moduleName,
              {ModuleDependenciesKind::SwiftSource, searchPathSet}))
        return found;
      if (auto found = cache.findDependencies(
              moduleName,
              {ModuleDependenciesKind::SwiftInterface, searchPathSet}))
        return found;
      if (auto found = cache.findDependencies(
              moduleName, {ModuleDependenciesKind::SwiftBinary, searchPathSet}))
        return found;
      if (auto found = cache.findDependencies(
              moduleName,
              {ModuleDependenciesKind::SwiftPlaceholder, searchPathSet}))
        return found;
    }
    if (auto found = cache.findDependencies(
            moduleName, {ModuleDependenciesKind::Clang, searchPathSet}))
      return found;
  } else {
    for (auto &loader : getImpl().ModuleLoaders) {
      if (isUnderlyingClangModule &&
          loader.get() != getImpl().TheClangModuleLoader)
        continue;

      if (auto dependencies =
              loader->getModuleDependencies(moduleName, cache, delegate))
        return dependencies;
    }
  }

  return None;
}

Optional<ModuleDependencies>
ASTContext::getSwiftModuleDependencies(StringRef moduleName,
                                       ModuleDependenciesCache &cache,
                                       InterfaceSubContextDelegate &delegate) {
  for (auto &loader : getImpl().ModuleLoaders) {
    if (loader.get() == getImpl().TheClangModuleLoader)
      continue;

    if (auto dependencies = loader->getModuleDependencies(moduleName, cache,
                                                          delegate))
      return dependencies;
  }
  return None;
}

namespace {
  static StringRef
  pathStringFromFrameworkSearchPath(const SearchPathOptions::FrameworkSearchPath &next) {
    return next.Path;
  }
}

std::vector<std::string> ASTContext::getDarwinImplicitFrameworkSearchPaths()
const {
  assert(LangOpts.Target.isOSDarwin());
  SmallString<128> systemFrameworksScratch;
  systemFrameworksScratch = SearchPathOpts.SDKPath;
  llvm::sys::path::append(systemFrameworksScratch, "System", "Library", "Frameworks");

  SmallString<128> frameworksScratch;
  frameworksScratch = SearchPathOpts.SDKPath;
  llvm::sys::path::append(frameworksScratch, "Library", "Frameworks");
  return {systemFrameworksScratch.str().str(), frameworksScratch.str().str()};
}

llvm::StringSet<> ASTContext::getAllModuleSearchPathsSet()
const {
  llvm::StringSet<> result;
  result.insert(SearchPathOpts.ImportSearchPaths.begin(),
                SearchPathOpts.ImportSearchPaths.end());

  // Framework paths are "special", they contain more than path strings,
  // but path strings are all we care about here.
  using FrameworkPathView = ArrayRefView<SearchPathOptions::FrameworkSearchPath,
                                         StringRef,
                                         pathStringFromFrameworkSearchPath>;
  FrameworkPathView frameworkPathsOnly{SearchPathOpts.FrameworkSearchPaths};
  result.insert(frameworkPathsOnly.begin(), frameworkPathsOnly.end());

  if (LangOpts.Target.isOSDarwin()) {
    auto implicitFrameworkSearchPaths = getDarwinImplicitFrameworkSearchPaths();
    result.insert(implicitFrameworkSearchPaths.begin(),
                  implicitFrameworkSearchPaths.end());
  }
  result.insert(SearchPathOpts.RuntimeLibraryImportPaths.begin(),
                SearchPathOpts.RuntimeLibraryImportPaths.end());

  // ClangImporter special-cases the path for SwiftShims, so do the same here
  // If there are no shims in the resource dir, add a search path in the SDK.
  SmallString<128> shimsPath(SearchPathOpts.RuntimeResourcePath);
  llvm::sys::path::append(shimsPath, "shims");
  if (!llvm::sys::fs::exists(shimsPath)) {
    shimsPath = SearchPathOpts.SDKPath;
    llvm::sys::path::append(shimsPath, "usr", "lib", "swift", "shims");
  }
  result.insert(shimsPath.str());

  // Clang system modules are found in the SDK root
  SmallString<128> clangSysRootPath(SearchPathOpts.SDKPath);
  llvm::sys::path::append(clangSysRootPath, "usr", "include");
  result.insert(clangSysRootPath.str());
  return result;
}

void ASTContext::loadExtensions(NominalTypeDecl *nominal,
                                unsigned previousGeneration) {
  PrettyStackTraceDecl stackTrace("loading extensions for", nominal);
  for (auto &loader : getImpl().ModuleLoaders) {
    loader->loadExtensions(nominal, previousGeneration);
  }
}

void ASTContext::loadObjCMethods(
    ClassDecl *classDecl, ObjCSelector selector, bool isInstanceMethod,
    unsigned previousGeneration,
    llvm::TinyPtrVector<AbstractFunctionDecl *> &methods, bool swiftOnly) {
  PrettyStackTraceSelector stackTraceSelector("looking for", selector);
  PrettyStackTraceDecl stackTraceDecl("...in", classDecl);
  for (auto &loader : getImpl().ModuleLoaders) {
    // Ignore the Clang importer if we've been asked for Swift-only results.
    if (swiftOnly && loader.get() == getClangModuleLoader())
      continue;

    loader->loadObjCMethods(classDecl, selector, isInstanceMethod,
                            previousGeneration, methods);
  }
}

void ASTContext::loadDerivativeFunctionConfigurations(
    AbstractFunctionDecl *originalAFD, unsigned previousGeneration,
    llvm::SetVector<AutoDiffConfig> &results) {
  PrettyStackTraceDecl stackTrace(
      "loading derivative function configurations for", originalAFD);
  for (auto &loader : getImpl().ModuleLoaders) {
    loader->loadDerivativeFunctionConfigurations(originalAFD,
                                                 previousGeneration, results);
  }
}

void ASTContext::verifyAllLoadedModules() const {
#ifndef NDEBUG
  FrontendStatsTracer tracer(Stats, "verify-all-loaded-modules");
  for (auto &loader : getImpl().ModuleLoaders)
    loader->verifyAllModules();
#endif
}

swift::namelookup::ImportCache &ASTContext::getImportCache() const {
  return getImpl().TheImportCache;
}

ClangModuleLoader *ASTContext::getClangModuleLoader() const {
  return getImpl().TheClangModuleLoader;
}

ClangModuleLoader *ASTContext::getDWARFModuleLoader() const {
  return getImpl().TheDWARFModuleLoader;
}

ModuleInterfaceChecker *ASTContext::getModuleInterfaceChecker() const {
  auto *result = getImpl().InterfaceChecker.get();
  assert(result);
  return result;
}

ModuleDecl *ASTContext::getLoadedModule(
    ImportPath::Module ModulePath) const {
  assert(!ModulePath.empty());

  // TODO: Swift submodules.
  if (ModulePath.size() == 1) {
    return getLoadedModule(ModulePath[0].Item);
  }
  return nullptr;
}

iterator_range<llvm::MapVector<Identifier, ModuleDecl *>::const_iterator>
ASTContext::getLoadedModules() const {
  return {getImpl().LoadedModules.begin(), getImpl().LoadedModules.end()};
}

ModuleDecl *ASTContext::getLoadedModule(Identifier ModuleName) const {
  // Look up a loaded module using an actual module name (physical name
  // on disk). If the -module-alias option is used, the module name that
  // appears in source code will be different from the real module name
  // on disk, otherwise the same.
  //
  // For example, if '-module-alias Foo=Bar' is passed in to the frontend,
  // and a source file has 'import Foo', a module called Bar (real name)
  // will be loaded and returned.
  auto realName = getRealModuleName(ModuleName);
  return getImpl().LoadedModules.lookup(realName);
}

void ASTContext::addLoadedModule(ModuleDecl *M) {
  assert(M);
  // Add a loaded module using an actual module name (physical name
  // on disk), in case -module-alias is used (otherwise same).
  //
  // For example, if '-module-alias Foo=Bar' is passed in to the frontend,
  // and a source file has 'import Foo', a module called Bar (real name)
  // will be loaded and added to the map.
  getImpl().LoadedModules[M->getRealName()] = M;
}

void ASTContext::registerGenericSignatureBuilder(
                                       GenericSignature sig,
                                       GenericSignatureBuilder &&builder) {
  if (LangOpts.EnableRequirementMachine == RequirementMachineMode::Enabled)
    return;

  auto canSig = sig.getCanonicalSignature();
  auto &genericSignatureBuilders = getImpl().GenericSignatureBuilders;
  auto known = genericSignatureBuilders.find(canSig);
  if (known != genericSignatureBuilders.end()) {
    ++NumRegisteredGenericSignatureBuildersAlready;
    return;
  }

  ++NumRegisteredGenericSignatureBuilders;
  genericSignatureBuilders[canSig] =
    std::make_unique<GenericSignatureBuilder>(std::move(builder));
}

GenericSignatureBuilder *ASTContext::getOrCreateGenericSignatureBuilder(
                                                      CanGenericSignature sig) {
  // We should only create GenericSignatureBuilders if the requirement machine
  // mode is ::Disabled or ::Verify.
  assert(LangOpts.EnableRequirementMachine != RequirementMachineMode::Enabled &&
         "Shouldn't create GenericSignatureBuilder when RequirementMachine "
         "is enabled");

  // Check whether we already have a generic signature builder for this
  // signature and module.
  auto &genericSignatureBuilders = getImpl().GenericSignatureBuilders;
  auto known = genericSignatureBuilders.find(sig);
  if (known != genericSignatureBuilders.end())
    return known->second.get();

  // Create a new generic signature builder with the given signature.
  auto builder = new GenericSignatureBuilder(*this);

  // Store this generic signature builder (no generic environment yet).
  genericSignatureBuilders[sig] =
    std::unique_ptr<GenericSignatureBuilder>(builder);

  builder->addGenericSignature(sig);

#if SWIFT_GSB_EXPENSIVE_ASSERTIONS
  auto builderSig =
    builder->computeGenericSignature(/*allowConcreteGenericParams=*/true);
  if (builderSig.getCanonicalSignature() != sig) {
    llvm::errs() << "ERROR: generic signature builder is not idempotent.\n";
    llvm::errs() << "Original generic signature   : ";
    sig->print(llvm::errs());
    llvm::errs() << "\nReprocessed generic signature: ";
    auto reprocessedSig = builderSig.getCanonicalSignature();

    reprocessedSig->print(llvm::errs());
    llvm::errs() << "\n";

    if (sig.getGenericParams().size() ==
          reprocessedSig.getGenericParams().size() &&
        sig.getRequirements().size() ==
          reprocessedSig.getRequirements().size()) {
      for (unsigned i : indices(sig.getRequirements())) {
        auto sigReq = sig.getRequirements()[i];
        auto reprocessedReq = reprocessedSig.getRequirements()[i];
        if (sigReq.getKind() != reprocessedReq.getKind()) {
          llvm::errs() << "Requirement mismatch:\n";
          llvm::errs() << "  Original: ";
          sigReq.print(llvm::errs(), PrintOptions());
          llvm::errs() << "\n  Reprocessed: ";
          reprocessedReq.print(llvm::errs(), PrintOptions());
          llvm::errs() << "\n";
          break;
        }

        if (!sigReq.getFirstType()->isEqual(reprocessedReq.getFirstType())) {
          llvm::errs() << "First type mismatch, original is:\n";
          sigReq.getFirstType().dump(llvm::errs());
          llvm::errs() << "Reprocessed:\n";
          reprocessedReq.getFirstType().dump(llvm::errs());
          llvm::errs() << "\n";
          break;
        }

        if (sigReq.getKind() == RequirementKind::SameType &&
            !sigReq.getSecondType()->isEqual(reprocessedReq.getSecondType())) {
          llvm::errs() << "Second type mismatch, original is:\n";
          sigReq.getSecondType().dump(llvm::errs());
          llvm::errs() << "Reprocessed:\n";
          reprocessedReq.getSecondType().dump(llvm::errs());
          llvm::errs() << "\n";
          break;
        }
      }
    }

    llvm_unreachable("idempotency problem with a generic signature");
  }
#else
  // FIXME: This should be handled lazily in the future, and therefore not
  // required.
  builder->processDelayedRequirements();
#endif

  return builder;
}

rewriting::RewriteContext &
ASTContext::getRewriteContext() {
  auto &rewriteCtx = getImpl().TheRewriteContext;
  if (!rewriteCtx)
    rewriteCtx.reset(new rewriting::RewriteContext(*this));

  return *rewriteCtx;
}

bool ASTContext::isRecursivelyConstructingRequirementMachine(
      CanGenericSignature sig) {
  return getRewriteContext().isRecursivelyConstructingRequirementMachine(sig);
}

Optional<llvm::TinyPtrVector<ValueDecl *>>
OverriddenDeclsRequest::getCachedResult() const {
  auto decl = std::get<0>(getStorage());
  if (!decl->LazySemanticInfo.hasOverriddenComputed)
    return None;

  // If there are no overridden declarations (the common case), return.
  llvm::TinyPtrVector<ValueDecl *> overridden;
  if (!decl->LazySemanticInfo.hasOverridden) return overridden;

  // Retrieve the set of overrides from the ASTContext.
  ASTContext &ctx = decl->getASTContext();
  auto known = ctx.getImpl().Overrides.find(decl);
  assert(known != ctx.getImpl().Overrides.end());
  overridden.insert(overridden.end(),
                    known->second.begin(), known->second.end());
  return overridden;
}

void OverriddenDeclsRequest::cacheResult(
                                llvm::TinyPtrVector<ValueDecl *> value) const {
  auto decl = std::get<0>(getStorage());
  decl->LazySemanticInfo.hasOverriddenComputed = true;
  decl->LazySemanticInfo.hasOverridden = !value.empty();

  if (value.empty())
    return;

  // Sanity-check the declarations we were given.
  for (auto overriddenDecl : value) {
    assert(overriddenDecl->getKind() == decl->getKind() &&
           "Overridden decl kind mismatch");
    if (auto func = dyn_cast<AbstractFunctionDecl>(overriddenDecl))
      func->setIsOverridden();
  }

  // Record the overrides in the context.
  auto &ctx = decl->getASTContext();
  auto overriddenCopy =
    ctx.AllocateCopy(value.operator ArrayRef<ValueDecl *>());
  (void)ctx.getImpl().Overrides.insert({decl, overriddenCopy});
}

/// Returns the default witness for a requirement, or nullptr if there is
/// no default.
Witness ProtocolDecl::getDefaultWitness(ValueDecl *requirement) const {
  loadAllMembers();

  ASTContext &ctx = getASTContext();
  auto found = ctx.getImpl().DefaultWitnesses.find({this, requirement});
  if (found == ctx.getImpl().DefaultWitnesses.end())
    return Witness();
  return found->second;
}

/// Record the default witness for a requirement.
void ProtocolDecl::setDefaultWitness(ValueDecl *requirement, Witness witness) {
  assert(witness);
  ASTContext &ctx = getASTContext();
  auto pair = ctx.getImpl().DefaultWitnesses.insert(
                std::make_pair(std::make_pair(this, requirement), witness));
  assert(pair.second && "Already have a default witness!");
  (void) pair;
}

/// Returns the default type witness for an associated type, or a null
/// type if there is no default.
Type ProtocolDecl::getDefaultTypeWitness(AssociatedTypeDecl *assocType) const {
  auto &ctx = getASTContext();
  auto found = ctx.getImpl().DefaultTypeWitnesses.find({this, assocType});
  if (found == ctx.getImpl().DefaultTypeWitnesses.end())
    return Type();

  return found->second;
}

/// Set the default type witness for an associated type.
void ProtocolDecl::setDefaultTypeWitness(AssociatedTypeDecl *assocType,
                                         Type witness) {
  assert(witness);
  assert(!witness->hasArchetype() && "Only record interface types");
  ASTContext &ctx = getASTContext();
  auto pair = ctx.getImpl().DefaultTypeWitnesses.insert(
                std::make_pair(std::make_pair(this, assocType), witness));
  assert(pair.second && "Already have a default witness");
  (void)pair;
}

ProtocolConformanceRef ProtocolDecl::getDefaultAssociatedConformanceWitness(
    CanType association, ProtocolDecl *requirement) const {
  auto &ctx = getASTContext();
  auto found =
    ctx.getImpl().DefaultAssociatedConformanceWitnesses.find(
      std::make_tuple(this, association, requirement));
  if (found == ctx.getImpl().DefaultAssociatedConformanceWitnesses.end())
    return ProtocolConformanceRef::forInvalid();

  return found->second;
}

void ProtocolDecl::setDefaultAssociatedConformanceWitness(
                                          CanType association,
                                          ProtocolDecl *requirement,
                                          ProtocolConformanceRef conformance) {
  auto &ctx = getASTContext();
  auto pair = ctx.getImpl().DefaultAssociatedConformanceWitnesses.insert(
                std::make_pair(std::make_tuple(this, association, requirement),
                               conformance));
  assert(pair.second && "Already have a default associated conformance");
  (void)pair;
}

void ASTContext::getVisibleTopLevelModuleNames(
    SmallVectorImpl<Identifier> &names) const {
  names.clear();
  for (auto &importer : getImpl().ModuleLoaders)
    importer->collectVisibleTopLevelModuleNames(names);

  // Sort and unique.
  std::sort(names.begin(), names.end(), [](Identifier LHS, Identifier RHS) {
    return LHS.str().compare_insensitive(RHS.str()) < 0;
  });
  names.erase(std::unique(names.begin(), names.end()), names.end());
}

bool ASTContext::shouldPerformTypoCorrection() {
  NumTypoCorrections += 1;
  return NumTypoCorrections <= LangOpts.TypoCorrectionLimit;
}

bool ASTContext::canImportModuleImpl(ImportPath::Element ModuleName,
                                     llvm::VersionTuple version,
                                     bool underlyingVersion,
                                     bool updateFailingList) const {
  // If we've failed loading this module before, don't look for it again.
  if (FailedModuleImportNames.count(ModuleName.Item))
    return false;
  // If no specific version, the module is importable if it has already been imported.
  if (version.empty()) {
    // If this module has already been successfully imported, it is importable.
    if (getLoadedModule(ImportPath::Module::Builder(ModuleName).get()) != nullptr)
      return true;
  }
  // Otherwise, ask the module loaders.
  for (auto &importer : getImpl().ModuleLoaders) {
    if (importer->canImportModule(ModuleName, version, underlyingVersion)) {
      return true;
    }
  }
  if (updateFailingList && version.empty()) {
    FailedModuleImportNames.insert(ModuleName.Item);
  }
  return false;
}

bool ASTContext::canImportModule(ImportPath::Element ModuleName,
                                 llvm::VersionTuple version,
                                 bool underlyingVersion) {
  return canImportModuleImpl(ModuleName, version, underlyingVersion, true);
}

bool ASTContext::canImportModule(ImportPath::Element ModuleName,
                                 llvm::VersionTuple version,
                                 bool underlyingVersion) const {
  return canImportModuleImpl(ModuleName, version, underlyingVersion, false);
}

ModuleDecl *
ASTContext::getModule(ImportPath::Module ModulePath) {
  assert(!ModulePath.empty());

  if (auto *M = getLoadedModule(ModulePath))
    return M;

  auto moduleID = ModulePath[0];
  for (auto &importer : getImpl().ModuleLoaders) {
    if (ModuleDecl *M = importer->loadModule(moduleID.Loc, ModulePath)) {
      if (LangOpts.EnableModuleLoadingRemarks) {
        Diags.diagnose(ModulePath.getSourceRange().Start,
                       diag::module_loaded,
                       M->getModuleFilename());
      }
      return M;
    }
  }

  return nullptr;
}

ModuleDecl *ASTContext::getOverlayModule(const FileUnit *FU) {
  assert(FU && FU->getKind() == FileUnitKind::ClangModule &&
         "Overlays can only be retrieved for clang modules!");
  ImportPath::Module::Builder builder(FU->getParentModule()->getName());
  auto ModPath = builder.get();
  if (auto *Existing = getLoadedModule(ModPath)) {
    if (!Existing->isNonSwiftModule())
      return Existing;
  }

  for (auto &importer : getImpl().ModuleLoaders) {
    if (importer.get() == getClangModuleLoader())
      continue;
    if (ModuleDecl *M = importer->loadModule(SourceLoc(), ModPath)) {
      return M;
    }
  }

  return nullptr;
}

ModuleDecl *ASTContext::getModuleByName(StringRef ModuleName) {
  ImportPath::Module::Builder builder(*this, ModuleName, /*separator=*/'.');
  return getModule(builder.get());
}

ModuleDecl *ASTContext::getModuleByIdentifier(Identifier ModuleID) {
  ImportPath::Module::Builder builder(ModuleID);
  return getModule(builder.get());
}

ModuleDecl *ASTContext::getStdlibModule(bool loadIfAbsent) {
  if (TheStdlibModule)
    return TheStdlibModule;

  if (loadIfAbsent) {
    auto mutableThis = const_cast<ASTContext*>(this);
    TheStdlibModule = mutableThis->getModuleByIdentifier(StdlibModuleName);
  } else {
    TheStdlibModule = getLoadedModule(StdlibModuleName);
  }
  return TheStdlibModule;
}

Optional<ExternalSourceLocs *>
ASTContext::getExternalSourceLocs(const Decl *D) {
  auto Known = getImpl().ExternalSourceLocs.find(D);
  if (Known == getImpl().ExternalSourceLocs.end())
    return None;

  return Known->second;
}

void ASTContext::setExternalSourceLocs(const Decl *D,
                                       ExternalSourceLocs *Locs) {
  getImpl().ExternalSourceLocs[D] = Locs;
}

Optional<std::pair<RawComment, bool>> ASTContext::getRawComment(const Decl *D) {
  auto Known = getImpl().RawComments.find(D);
  if (Known == getImpl().RawComments.end())
    return None;

  return Known->second;
}

void ASTContext::setRawComment(const Decl *D, RawComment RC, bool FromSerialized) {
  getImpl().RawComments[D] = std::make_pair(RC, FromSerialized);
}

Optional<StringRef> ASTContext::getBriefComment(const Decl *D) {
  auto Known = getImpl().BriefComments.find(D);
  if (Known == getImpl().BriefComments.end())
    return None;

  return Known->second;
}

void ASTContext::setBriefComment(const Decl *D, StringRef Comment) {
  getImpl().BriefComments[D] = Comment;
}

NormalProtocolConformance *
ASTContext::getConformance(Type conformingType,
                           ProtocolDecl *protocol,
                           SourceLoc loc,
                           DeclContext *dc,
                           ProtocolConformanceState state,
                           bool isUnchecked) {
  assert(dc->isTypeContext());

  llvm::FoldingSetNodeID id;
  NormalProtocolConformance::Profile(id, protocol, dc);

  // Did we already record the normal conformance?
  void *insertPos;
  auto &normalConformances =
    getImpl().getArena(AllocationArena::Permanent).NormalConformances;
  if (auto result = normalConformances.FindNodeOrInsertPos(id, insertPos))
    return result;

  // Build a new normal protocol conformance.
  auto result
    = new (*this, AllocationArena::Permanent)
        NormalProtocolConformance(
          conformingType, protocol, loc, dc, state,isUnchecked);
  normalConformances.InsertNode(result, insertPos);

  return result;
}

/// Produce a self-conformance for the given protocol.
SelfProtocolConformance *
ASTContext::getSelfConformance(ProtocolDecl *protocol) {
  auto &selfConformances =
    getImpl().getArena(AllocationArena::Permanent).SelfConformances;
  auto &entry = selfConformances[protocol];
  if (!entry) {
    entry = new (*this, AllocationArena::Permanent)
      SelfProtocolConformance(protocol->getExistentialType());
  }
  return entry;
}

/// Produce the builtin conformance for some non-nominal to some protocol.
BuiltinProtocolConformance *
ASTContext::getBuiltinConformance(
    Type type, ProtocolDecl *protocol,
    GenericSignature genericSig,
    ArrayRef<Requirement> conditionalRequirements,
    BuiltinConformanceKind kind
) {
  auto key = std::make_pair(type, protocol);
  AllocationArena arena = getArena(type->getRecursiveProperties());
  auto &builtinConformances = getImpl().getArena(arena).BuiltinConformances;

  auto &entry = builtinConformances[key];
  if (!entry) {
    auto size = BuiltinProtocolConformance::
        totalSizeToAlloc<Requirement>(conditionalRequirements.size());
    auto mem = this->Allocate(size, alignof(BuiltinProtocolConformance), arena);
    entry = new (mem) BuiltinProtocolConformance(
        type, protocol, genericSig, conditionalRequirements, kind);
  }
  return entry;
}

/// If one of the ancestor conformances already has a matching type, use
/// that instead.
static ProtocolConformance *collapseSpecializedConformance(
                                             Type type,
                                             ProtocolConformance *conformance,
                                             SubstitutionMap substitutions) {
  while (true) {
    switch (conformance->getKind()) {
    case ProtocolConformanceKind::Specialized:
      conformance = cast<SpecializedProtocolConformance>(conformance)
                      ->getGenericConformance();
      break;

    case ProtocolConformanceKind::Normal:
    case ProtocolConformanceKind::Inherited:
    case ProtocolConformanceKind::Self:
    case ProtocolConformanceKind::Builtin:
      // If the conformance matches, return it.
      if (conformance->getType()->isEqual(type)) {
        for (auto subConformance : substitutions.getConformances())
          if (!subConformance.isAbstract())
            return nullptr;

        return conformance;
      }

      return nullptr;
    }
  }
}

ProtocolConformance *
ASTContext::getSpecializedConformance(Type type,
                                      ProtocolConformance *generic,
                                      SubstitutionMap substitutions) {
  // If we are performing a substitution that would get us back to the
  // a prior conformance (e.g., mapping into and then out of a conformance),
  // return the existing conformance.
  if (auto existing = collapseSpecializedConformance(type, generic,
                                                     substitutions)) {
    ++NumCollapsedSpecializedProtocolConformances;
    return existing;
  }

  llvm::FoldingSetNodeID id;
  SpecializedProtocolConformance::Profile(id, type, generic, substitutions);

  // Figure out which arena this conformance should go into.
  AllocationArena arena = getArena(type->getRecursiveProperties());

  // Did we already record the specialized conformance?
  void *insertPos;
  auto &specializedConformances = getImpl().getArena(arena).SpecializedConformances;
  if (auto result = specializedConformances.FindNodeOrInsertPos(id, insertPos))
    return result;

  // Build a new specialized conformance.
  auto result
    = new (*this, arena) SpecializedProtocolConformance(type, generic,
                                                        substitutions);
  auto node = specializedConformances.FindNodeOrInsertPos(id, insertPos);
  (void)node;
  assert(!node);
  specializedConformances.InsertNode(result, insertPos);
  return result;
}

InheritedProtocolConformance *
ASTContext::getInheritedConformance(Type type, ProtocolConformance *inherited) {
  llvm::FoldingSetNodeID id;
  InheritedProtocolConformance::Profile(id, type, inherited);

  // Figure out which arena this conformance should go into.
  AllocationArena arena = getArena(type->getRecursiveProperties());

  // Did we already record the normal protocol conformance?
  void *insertPos;
  auto &inheritedConformances = getImpl().getArena(arena).InheritedConformances;
  if (auto result
        = inheritedConformances.FindNodeOrInsertPos(id, insertPos))
    return result;

  // Build a new normal protocol conformance.
  auto result = new (*this, arena) InheritedProtocolConformance(type, inherited);
  inheritedConformances.InsertNode(result, insertPos);
  return result;
}

LazyContextData *ASTContext::getOrCreateLazyContextData(
                                                const DeclContext *dc,
                                                LazyMemberLoader *lazyLoader) {
  LazyContextData *&entry = getImpl().LazyContexts[dc];
  if (entry) {
    // Make sure we didn't provide an incompatible lazy loader.
    assert(!lazyLoader || lazyLoader == entry->loader);
    return entry;
  }

  // Create new lazy context data with the given loader.
  assert(lazyLoader && "Queried lazy data for non-lazy iterable context");
  if (isa<ProtocolDecl>(dc))
    entry = Allocate<LazyProtocolData>();
  else {
    assert(isa<NominalTypeDecl>(dc) || isa<ExtensionDecl>(dc));
    entry = Allocate<LazyIterableDeclContextData>();
  }

  entry->loader = lazyLoader;
  return entry;
}

LazyIterableDeclContextData *ASTContext::getOrCreateLazyIterableContextData(
                                            const IterableDeclContext *idc,
                                            LazyMemberLoader *lazyLoader) {
  if (auto ext = dyn_cast<ExtensionDecl>(idc)) {
    return (LazyIterableDeclContextData *)getOrCreateLazyContextData(
                                                              ext, lazyLoader);
  }

  auto nominal = cast<NominalTypeDecl>(idc);
  return (LazyIterableDeclContextData *)getOrCreateLazyContextData(nominal,
                                                                   lazyLoader);
}

bool ASTContext::hasDelayedConformanceErrors(
                          NormalProtocolConformance const* conformance) const {

  auto hasDelayedErrors = [](std::vector<DelayedConformanceDiag> const& diags) {
    return std::any_of(diags.begin(), diags.end(),
                    [](ASTContext::DelayedConformanceDiag const& diag) {
                      return diag.IsError;
                    });
  };

  if (conformance) {
    auto entry = getImpl().DelayedConformanceDiags.find(conformance);
    if (entry != getImpl().DelayedConformanceDiags.end())
      return hasDelayedErrors(entry->second);

    return false; // unknown conformance, so no delayed delayed diags either.
  }
  
  // check all conformances for any delayed errors
  for (const auto &entry : getImpl().DelayedConformanceDiags) {
    auto const& diagnostics = entry.getSecond();
    if (hasDelayedErrors(diagnostics))
      return true;
  }

  return false;
}

MissingWitnessesBase::~MissingWitnessesBase() { }

void ASTContext::addDelayedConformanceDiag(
       NormalProtocolConformance *conformance,
       DelayedConformanceDiag fn) {
  getImpl().DelayedConformanceDiags[conformance].push_back(std::move(fn));
}

void ASTContext::addDelayedMissingWitnesses(
    NormalProtocolConformance *conformance,
    std::unique_ptr<MissingWitnessesBase> missingWitnesses) {
  getImpl().DelayedMissingWitnesses[conformance] = std::move(missingWitnesses);
}

std::unique_ptr<MissingWitnessesBase>
ASTContext::takeDelayedMissingWitnesses(
    NormalProtocolConformance *conformance) {
  std::unique_ptr<MissingWitnessesBase> result;
  auto known = getImpl().DelayedMissingWitnesses.find(conformance);
  if (known != getImpl().DelayedMissingWitnesses.end()) {
    result = std::move(known->second);
    getImpl().DelayedMissingWitnesses.erase(known);
  }
  return result;
}

std::vector<ASTContext::DelayedConformanceDiag>
ASTContext::takeDelayedConformanceDiags(NormalProtocolConformance const* cnfrm){
  std::vector<ASTContext::DelayedConformanceDiag> result;
  auto known = getImpl().DelayedConformanceDiags.find(cnfrm);
  if (known != getImpl().DelayedConformanceDiags.end()) {
    result = std::move(known->second);
    getImpl().DelayedConformanceDiags.erase(known);
  }
  return result;
}

size_t ASTContext::getTotalMemory() const {
  size_t Size = sizeof(*this) +
    // LoadedModules ?
    llvm::capacity_in_bytes(CanonicalGenericTypeParamTypeNames) +
    // RemappedTypes ?
    sizeof(getImpl()) +
    getImpl().Allocator.getTotalMemory() +
    getImpl().Cleanups.capacity() +
    llvm::capacity_in_bytes(getImpl().ModuleLoaders) +
    llvm::capacity_in_bytes(getImpl().RawComments) +
    llvm::capacity_in_bytes(getImpl().BriefComments) +
    llvm::capacity_in_bytes(getImpl().ModuleTypes) +
    llvm::capacity_in_bytes(getImpl().GenericParamTypes) +
    // getImpl().GenericFunctionTypes ?
    // getImpl().SILFunctionTypes ?
    llvm::capacity_in_bytes(getImpl().SILBlockStorageTypes) +
    llvm::capacity_in_bytes(getImpl().IntegerTypes) +
    // getImpl().ProtocolCompositionTypes ?
    // getImpl().BuiltinVectorTypes ?
    // getImpl().GenericSignatures ?
    // getImpl().CompoundNames ?
    getImpl().OpenedExistentialArchetypes.getMemorySize() +
    getImpl().Permanent.getTotalMemory();

    Size += getSolverMemory();

    return Size;
}

size_t ASTContext::getSolverMemory() const {
  size_t Size = 0;
  
  if (getImpl().CurrentConstraintSolverArena) {
    Size += getImpl().CurrentConstraintSolverArena->getTotalMemory();
    Size += getImpl().CurrentConstraintSolverArena->Allocator.getBytesAllocated();
  }
  
  return Size;
}

size_t ASTContext::Implementation::Arena::getTotalMemory() const {
  return sizeof(*this) +
    // TupleTypes ?
    llvm::capacity_in_bytes(MetatypeTypes) +
    llvm::capacity_in_bytes(ExistentialMetatypeTypes) +
    llvm::capacity_in_bytes(ArraySliceTypes) +
    llvm::capacity_in_bytes(DictionaryTypes) +
    llvm::capacity_in_bytes(OptionalTypes) +
    llvm::capacity_in_bytes(VariadicSequenceTypes) +
    llvm::capacity_in_bytes(SimpleParenTypes) +
    llvm::capacity_in_bytes(ParenTypes) +
    llvm::capacity_in_bytes(ReferenceStorageTypes) +
    llvm::capacity_in_bytes(LValueTypes) +
    llvm::capacity_in_bytes(InOutTypes) +
    llvm::capacity_in_bytes(DependentMemberTypes) +
    llvm::capacity_in_bytes(EnumTypes) +
    llvm::capacity_in_bytes(StructTypes) +
    llvm::capacity_in_bytes(ClassTypes) +
    llvm::capacity_in_bytes(ProtocolTypes) +
    llvm::capacity_in_bytes(DynamicSelfTypes);
    // FunctionTypes ?
    // UnboundGenericTypes ?
    // BoundGenericTypes ?
    // NormalConformances ?
    // SpecializedConformances ?
    // InheritedConformances ?
    // BuiltinConformances ?
}

void AbstractFunctionDecl::setForeignErrorConvention(
                                         const ForeignErrorConvention &conv) {
  assert(hasThrows() && "setting error convention on non-throwing decl");
  auto &conventionsMap = getASTContext().getImpl().ForeignErrorConventions;
  assert(!conventionsMap.count(this) && "error convention already set");
  conventionsMap.insert({this, conv});
}

Optional<ForeignErrorConvention>
AbstractFunctionDecl::getForeignErrorConvention() const {
  if (!hasThrows())
    return None;
  auto &conventionsMap = getASTContext().getImpl().ForeignErrorConventions;
  auto it = conventionsMap.find(this);
  if (it == conventionsMap.end()) return None;
  return it->second;
}

void AbstractFunctionDecl::setForeignAsyncConvention(
                                         const ForeignAsyncConvention &conv) {
  assert(hasAsync() && "setting error convention on non-throwing decl");
  auto &conventionsMap = getASTContext().getImpl().ForeignAsyncConventions;
  assert(!conventionsMap.count(this) && "error convention already set");
  conventionsMap.insert({this, conv});
}

Optional<ForeignAsyncConvention>
AbstractFunctionDecl::getForeignAsyncConvention() const {
  if (!hasAsync())
    return None;
  auto &conventionsMap = getASTContext().getImpl().ForeignAsyncConventions;
  auto it = conventionsMap.find(this);
  if (it == conventionsMap.end()) return None;
  return it->second;
}

Optional<KnownFoundationEntity> swift::getKnownFoundationEntity(StringRef name){
  return llvm::StringSwitch<Optional<KnownFoundationEntity>>(name)
#define FOUNDATION_ENTITY(Name) .Case(#Name, KnownFoundationEntity::Name)
#include "swift/AST/KnownFoundationEntities.def"
    .Default(None);
}

StringRef ASTContext::getSwiftName(KnownFoundationEntity kind) {
  StringRef objcName;
  switch (kind) {
#define FOUNDATION_ENTITY(Name) case KnownFoundationEntity::Name:  \
    objcName = #Name;                                             \
    break;
#include "swift/AST/KnownFoundationEntities.def"
  }

  return objcName;
}

//===----------------------------------------------------------------------===//
// Type manipulation routines.
//===----------------------------------------------------------------------===//

TypeAliasType::TypeAliasType(TypeAliasDecl *typealias, Type parent,
                             SubstitutionMap substitutions,
                             Type underlying,
                             RecursiveTypeProperties properties)
    : SugarType(TypeKind::TypeAlias, underlying, properties),
      typealias(typealias) {
  // Record the parent (or absence of a parent).
  if (parent) {
    Bits.TypeAliasType.HasParent = true;
    *getTrailingObjects<Type>() = parent;
  } else {
    Bits.TypeAliasType.HasParent = false;
  }

  // Record the substitutions.
  if (substitutions) {
    Bits.TypeAliasType.HasSubstitutionMap = true;
    *getTrailingObjects<SubstitutionMap>() = substitutions;
  } else {
    Bits.TypeAliasType.HasSubstitutionMap = false;
  }
}

TypeAliasType *TypeAliasType::get(TypeAliasDecl *typealias, Type parent,
                                  SubstitutionMap substitutions,
                                  Type underlying) {
  // Compute the recursive properties.
  //
  auto properties = underlying->getRecursiveProperties();
  if (parent)
    properties |= parent->getRecursiveProperties();

  for (auto substGP : substitutions.getReplacementTypes())
    properties |= substGP->getRecursiveProperties();

  // Figure out which arena this type will go into.
  auto &ctx = underlying->getASTContext();
  auto arena = getArena(properties);

  // Profile the type.
  llvm::FoldingSetNodeID id;
  TypeAliasType::Profile(id, typealias, parent, substitutions, underlying);

  // Did we already record this type?
  void *insertPos;
  auto &types = ctx.getImpl().getArena(arena).TypeAliasTypes;
  if (auto result = types.FindNodeOrInsertPos(id, insertPos))
    return result;

  // Build a new type.
  auto genericSig = substitutions.getGenericSignature();
  auto size = totalSizeToAlloc<Type, SubstitutionMap>(parent ? 1 : 0,
                                                      genericSig ? 1 : 0);
  auto mem = ctx.Allocate(size, alignof(TypeAliasType), arena);
  auto result = new (mem) TypeAliasType(typealias, parent, substitutions,
                                        underlying, properties);
  types.InsertNode(result, insertPos);
  return result;
}

void TypeAliasType::Profile(llvm::FoldingSetNodeID &id) const {
  Profile(id, getDecl(), getParent(), getSubstitutionMap(),
          Type(getSinglyDesugaredType()));
}

void TypeAliasType::Profile(
                           llvm::FoldingSetNodeID &id,
                           TypeAliasDecl *typealias,
                           Type parent, SubstitutionMap substitutions,
                           Type underlying) {
  id.AddPointer(typealias);
  id.AddPointer(parent.getPointer());
  substitutions.profile(id);
  id.AddPointer(underlying.getPointer());
}

// Simple accessors.
Type ErrorType::get(const ASTContext &C) { return C.TheErrorType; }

Type ErrorType::get(Type originalType) {
  assert(originalType);

  auto originalProperties = originalType->getRecursiveProperties();
  auto arena = getArena(originalProperties);

  auto &ctx = originalType->getASTContext();
  auto &entry = ctx.getImpl().getArena(arena).ErrorTypesWithOriginal[originalType];
  if (entry) return entry;

  void *mem = ctx.Allocate(sizeof(ErrorType) + sizeof(Type),
                           alignof(ErrorType), arena);
  RecursiveTypeProperties properties = RecursiveTypeProperties::HasError;
  if (originalProperties.hasTypeVariable())
    properties |= RecursiveTypeProperties::HasTypeVariable;
  return entry = new (mem) ErrorType(ctx, originalType, properties);
}

Type PlaceholderType::get(ASTContext &ctx, Originator originator) {
  assert(originator);
  return new (ctx, AllocationArena::Permanent)
      PlaceholderType(ctx, originator, RecursiveTypeProperties::HasPlaceholder);
}

BuiltinIntegerType *BuiltinIntegerType::get(BuiltinIntegerWidth BitWidth,
                                            const ASTContext &C) {
  assert(!BitWidth.isArbitraryWidth());
  BuiltinIntegerType *&Result = C.getImpl().IntegerTypes[BitWidth];
  if (Result == nullptr)
    Result = new (C, AllocationArena::Permanent) BuiltinIntegerType(BitWidth,C);
  return Result;
}

BuiltinVectorType *BuiltinVectorType::get(const ASTContext &context,
                                          Type elementType,
                                          unsigned numElements) {
  llvm::FoldingSetNodeID id;
  BuiltinVectorType::Profile(id, elementType, numElements);

  void *insertPos;
  if (BuiltinVectorType *vecType
        = context.getImpl().BuiltinVectorTypes.FindNodeOrInsertPos(id, insertPos))
    return vecType;

  assert(elementType->isCanonical() && "Non-canonical builtin vector?");
  BuiltinVectorType *vecTy
    = new (context, AllocationArena::Permanent)
       BuiltinVectorType(context, elementType, numElements);
  context.getImpl().BuiltinVectorTypes.InsertNode(vecTy, insertPos);
  return vecTy;
}

ParenType *ParenType::get(const ASTContext &C, Type underlying,
                          ParameterTypeFlags fl) {
  if (fl.isInOut())
    assert(!underlying->is<InOutType>() && "caller did not pass a base type");
  if (underlying->is<InOutType>())
    assert(fl.isInOut() && "caller did not set flags correctly");
  
  auto properties = underlying->getRecursiveProperties();
  auto arena = getArena(properties);
  auto flags = fl.toRaw();
  ParenType *&Result = flags == 0
      ? C.getImpl().getArena(arena).SimpleParenTypes[underlying]
      : C.getImpl().getArena(arena).ParenTypes[{underlying, flags}];
  if (Result == nullptr) {
    Result = new (C, arena) ParenType(underlying,
                                      properties, fl);
  }
  return Result;
}

CanTupleType TupleType::getEmpty(const ASTContext &C) {
  return cast<TupleType>(CanType(C.TheEmptyTupleType));
}

void TupleType::Profile(llvm::FoldingSetNodeID &ID,
                        ArrayRef<TupleTypeElt> Fields) {
  ID.AddInteger(Fields.size());
  for (const TupleTypeElt &Elt : Fields) {
    ID.AddPointer(Elt.Name.get());
    ID.AddPointer(Elt.getType().getPointer());
    ID.AddInteger(Elt.Flags.toRaw());
  }
}

/// getTupleType - Return the uniqued tuple type with the specified elements.
Type TupleType::get(ArrayRef<TupleTypeElt> Fields, const ASTContext &C) {
  if (Fields.size() == 1 && !Fields[0].isVararg() && !Fields[0].hasName())
    return ParenType::get(C, Fields[0].getRawType(),
                          Fields[0].getParameterFlags());

  RecursiveTypeProperties properties;
  bool hasElementWithOwnership = false;
  for (const TupleTypeElt &Elt : Fields) {
    auto eltTy = Elt.getType();
    if (!eltTy) continue;
    
    properties |= eltTy->getRecursiveProperties();
    // Recur into paren types and canonicalized paren types.  'inout' in nested
    // non-paren tuples are malformed and will be diagnosed later.
    if (auto *TTy = Elt.getType()->getAs<TupleType>()) {
      if (TTy->getNumElements() == 1)
        hasElementWithOwnership |= TTy->hasElementWithOwnership();
    } else if (auto *Pty = dyn_cast<ParenType>(Elt.getType().getPointer())) {
      hasElementWithOwnership |= (Pty->getParameterFlags().getValueOwnership() !=
                                  ValueOwnership::Default);
    } else {
      hasElementWithOwnership |= (Elt.getParameterFlags().getValueOwnership() !=
                                  ValueOwnership::Default);
    }
  }

  auto arena = getArena(properties);

  void *InsertPos = nullptr;
  // Check to see if we've already seen this tuple before.
  llvm::FoldingSetNodeID ID;
  TupleType::Profile(ID, Fields);

  if (TupleType *TT
        = C.getImpl().getArena(arena).TupleTypes.FindNodeOrInsertPos(ID,InsertPos))
    return TT;

  bool IsCanonical = true;   // All canonical elts means this is canonical.
  for (const TupleTypeElt &Elt : Fields) {
    if (Elt.getType().isNull() || !Elt.getType()->isCanonical()) {
      IsCanonical = false;
      break;
    }
  }

  size_t bytes = totalSizeToAlloc<TupleTypeElt>(Fields.size());
  // TupleType will copy the fields list into ASTContext owned memory.
  void *mem = C.Allocate(bytes, alignof(TupleType), arena);
  auto New = new (mem) TupleType(Fields, IsCanonical ? &C : nullptr, properties,
                                 hasElementWithOwnership);
  C.getImpl().getArena(arena).TupleTypes.InsertNode(New, InsertPos);
  return New;
}

TupleTypeElt::TupleTypeElt(Type ty, Identifier name,
                           ParameterTypeFlags fl)
  : Name(name), ElementType(ty), Flags(fl) {
  if (fl.isInOut())
    assert(!ty->is<InOutType>() && "caller did not pass a base type");
  if (ty->is<InOutType>())
    assert(fl.isInOut() && "caller did not set flags correctly");
}

Type TupleTypeElt::getType() const {
  if (Flags.isInOut()) return InOutType::get(ElementType);
  return ElementType;
}

Type AnyFunctionType::Param::getOldType() const {
  if (Flags.isInOut()) return InOutType::get(Ty);
  return Ty;
}

AnyFunctionType::Param swift::computeSelfParam(AbstractFunctionDecl *AFD,
                                               bool isInitializingCtor,
                                               bool wantDynamicSelf) {
  auto *dc = AFD->getDeclContext();
  auto &Ctx = dc->getASTContext();
  
  // Determine the type of the container.
  auto containerTy = dc->getDeclaredInterfaceType();
  if (!containerTy || containerTy->hasError())
    return AnyFunctionType::Param(ErrorType::get(Ctx));

  // Determine the type of 'self' inside the container.
  auto selfTy = dc->getSelfInterfaceType();
  if (!selfTy || selfTy->hasError())
    return AnyFunctionType::Param(ErrorType::get(Ctx));

  bool isStatic = false;
  SelfAccessKind selfAccess = SelfAccessKind::NonMutating;
  bool isDynamicSelf = false;

  if (auto *FD = dyn_cast<FuncDecl>(AFD)) {
    isStatic = FD->isStatic();
    selfAccess = FD->getSelfAccessKind();

    // `self`s type for subscripts and properties
    if (auto *AD = dyn_cast<AccessorDecl>(AFD)) {
      if (wantDynamicSelf && AD->getStorage()
          ->getValueInterfaceType()->hasDynamicSelfType())
        isDynamicSelf = true;
    }
    // Methods returning 'Self' have a dynamic 'self'.
    //
    // FIXME: All methods of non-final classes should have this.
    else if (wantDynamicSelf && FD->hasDynamicSelfResult())
      isDynamicSelf = true;

  } else if (auto *CD = dyn_cast<ConstructorDecl>(AFD)) {
    if (isInitializingCtor) {
      // initializing constructors of value types always have an implicitly
      // inout self.
      if (!containerTy->hasReferenceSemantics())
        selfAccess = SelfAccessKind::Mutating;

      // FIXME(distributed): pending swift-evolution, allow `self =` in class
      //  inits in general.
      //  See also: https://github.com/apple/swift/pull/19151 general impl
      if (Ctx.LangOpts.EnableExperimentalDistributed) {
        auto ext = dyn_cast<ExtensionDecl>(AFD->getDeclContext());
        auto distProto =
            Ctx.getProtocol(KnownProtocolKind::DistributedActor);
        if (distProto && ext && ext->getExtendedNominal() &&
            ext->getExtendedNominal()->getInterfaceType()
                ->isEqual(distProto->getInterfaceType())) {
          auto name = CD->getName();
          auto params = name.getArgumentNames();
          if (params.size() == 1 && params[0] == Ctx.Id_from) {
            // FIXME(distributed): this is a workaround to allow init(from:) to
            //  be implemented in AST by allowing the self to be mutable in the
            //  decoding initializer. This should become a general Swift
            //  feature, allowing this in all classes:
            //  https://forums.swift.org/t/allow-self-x-in-class-convenience-initializers/15924
            selfAccess = SelfAccessKind::Mutating;
          }
        }
      }
    } else {
      // allocating constructors have metatype 'self'.
      isStatic = true;
    }

    // Convenience initializers have a dynamic 'self' in '-swift-version 5'.
    if (Ctx.isSwiftVersionAtLeast(5)) {
      if (wantDynamicSelf && CD->isConvenienceInit())
        if (auto *classDecl = selfTy->getClassOrBoundGenericClass())
          if (!classDecl->isSemanticallyFinal())
            isDynamicSelf = true;
    }
  } else if (isa<DestructorDecl>(AFD)) {
    // Destructors only correctly appear on classes today. (If move-only types
    // have destructors, they probably would want to consume self.)
    // Note that we can't assert(containerTy->hasReferenceSemantics()) here
    // since incorrect or incomplete code could have deinit decls in invalid
    // contexts, and we need to recover gracefully in those cases.
  }

  if (isDynamicSelf)
    selfTy = DynamicSelfType::get(selfTy, Ctx);

  // 'static' functions have 'self' of type metatype<T>.
  if (isStatic)
    return AnyFunctionType::Param(MetatypeType::get(selfTy, Ctx));

  // `self` is isolated if typechecker says the function is isolated to it.
  bool isIsolated =
      evaluateOrDefault(Ctx.evaluator, HasIsolatedSelfRequest{AFD}, false);

  auto flags = ParameterTypeFlags().withIsolated(isIsolated);
  switch (selfAccess) {
  case SelfAccessKind::Consuming:
    flags = flags.withOwned(true);
    break;
  case SelfAccessKind::Mutating:
    flags = flags.withInOut(true);
    break;
  case SelfAccessKind::NonMutating:
    // The default flagless state.
    break;
  }

  return AnyFunctionType::Param(selfTy, Identifier(), flags);
}

void UnboundGenericType::Profile(llvm::FoldingSetNodeID &ID,
                                 GenericTypeDecl *TheDecl, Type Parent) {
  ID.AddPointer(TheDecl);
  ID.AddPointer(Parent.getPointer());
}

UnboundGenericType *UnboundGenericType::
get(GenericTypeDecl *TheDecl, Type Parent, const ASTContext &C) {
  llvm::FoldingSetNodeID ID;
  UnboundGenericType::Profile(ID, TheDecl, Parent);
  void *InsertPos = nullptr;
  RecursiveTypeProperties properties;
  if (Parent) properties |= Parent->getRecursiveProperties();
  auto arena = getArena(properties);

  if (auto unbound = C.getImpl().getArena(arena).UnboundGenericTypes
                        .FindNodeOrInsertPos(ID, InsertPos))
    return unbound;

  auto result = new (C, arena) UnboundGenericType(TheDecl, Parent, C,
                                                  properties);
  C.getImpl().getArena(arena).UnboundGenericTypes.InsertNode(result, InsertPos);
  return result;
}

void BoundGenericType::Profile(llvm::FoldingSetNodeID &ID,
                               NominalTypeDecl *TheDecl, Type Parent,
                               ArrayRef<Type> GenericArgs) {
  ID.AddPointer(TheDecl);
  ID.AddPointer(Parent.getPointer());
  ID.AddInteger(GenericArgs.size());
  for (Type Arg : GenericArgs) {
    ID.AddPointer(Arg.getPointer());
  }
}

BoundGenericType::BoundGenericType(TypeKind theKind,
                                   NominalTypeDecl *theDecl,
                                   Type parent,
                                   ArrayRef<Type> genericArgs,
                                   const ASTContext *context,
                                   RecursiveTypeProperties properties)
    : NominalOrBoundGenericNominalType(theDecl, parent, theKind, context,
                                       properties) {
  Bits.BoundGenericType.GenericArgCount = genericArgs.size();
  // Subtypes are required to provide storage for the generic arguments
  std::uninitialized_copy(genericArgs.begin(), genericArgs.end(),
                          getTrailingObjectsPointer());
}

BoundGenericType *BoundGenericType::get(NominalTypeDecl *TheDecl,
                                        Type Parent,
                                        ArrayRef<Type> GenericArgs) {
  assert(TheDecl->getGenericParams() && "must be a generic type decl");
  assert((!Parent || Parent->is<NominalType>() ||
          Parent->is<BoundGenericType>() ||
          Parent->is<UnboundGenericType>()) &&
         "parent must be a nominal type");

  ASTContext &C = TheDecl->getDeclContext()->getASTContext();
  llvm::FoldingSetNodeID ID;
  BoundGenericType::Profile(ID, TheDecl, Parent, GenericArgs);
  RecursiveTypeProperties properties;
  if (Parent) properties |= Parent->getRecursiveProperties();
  for (Type Arg : GenericArgs) {
    properties |= Arg->getRecursiveProperties();
  }

  auto arena = getArena(properties);

  void *InsertPos = nullptr;
  if (BoundGenericType *BGT =
        C.getImpl().getArena(arena).BoundGenericTypes.FindNodeOrInsertPos(ID,
                                                                     InsertPos))
    return BGT;

  bool IsCanonical = !Parent || Parent->isCanonical();
  if (IsCanonical) {
    for (Type Arg : GenericArgs) {
      if (!Arg->isCanonical()) {
        IsCanonical = false;
        break;
      }
    }
  }

  BoundGenericType *newType;
  if (auto theClass = dyn_cast<ClassDecl>(TheDecl)) {
    auto sz = BoundGenericClassType::totalSizeToAlloc<Type>(GenericArgs.size());
    auto mem = C.Allocate(sz, alignof(BoundGenericClassType), arena);
    newType = new (mem) BoundGenericClassType(
        theClass, Parent, GenericArgs, IsCanonical ? &C : nullptr, properties);
  } else if (auto theStruct = dyn_cast<StructDecl>(TheDecl)) {
    auto sz = BoundGenericStructType::totalSizeToAlloc<Type>(GenericArgs.size());
    auto mem = C.Allocate(sz, alignof(BoundGenericStructType), arena);
    newType = new (mem) BoundGenericStructType(
        theStruct, Parent, GenericArgs, IsCanonical ? &C : nullptr, properties);
  } else if (auto theEnum = dyn_cast<EnumDecl>(TheDecl)) {
    auto sz = BoundGenericEnumType::totalSizeToAlloc<Type>(GenericArgs.size());
    auto mem = C.Allocate(sz, alignof(BoundGenericEnumType), arena);
    newType = new (mem) BoundGenericEnumType(
        theEnum, Parent, GenericArgs, IsCanonical ? &C : nullptr, properties);
  } else {
    llvm_unreachable("Unhandled NominalTypeDecl");
  }
  C.getImpl().getArena(arena).BoundGenericTypes.InsertNode(newType, InsertPos);

  return newType;
}

NominalType *NominalType::get(NominalTypeDecl *D, Type Parent, const ASTContext &C) {
  assert((isa<ProtocolDecl>(D) || !D->getGenericParams()) &&
         "must be a non-generic type decl");
  assert((!Parent || Parent->is<NominalType>() ||
          Parent->is<BoundGenericType>() ||
          Parent->is<UnboundGenericType>()) &&
         "parent must be a nominal type");

  switch (D->getKind()) {
  case DeclKind::Enum:
    return EnumType::get(cast<EnumDecl>(D), Parent, C);
  case DeclKind::Struct:
    return StructType::get(cast<StructDecl>(D), Parent, C);
  case DeclKind::Class:
    return ClassType::get(cast<ClassDecl>(D), Parent, C);
  case DeclKind::Protocol: {
    return ProtocolType::get(cast<ProtocolDecl>(D), Parent, C);
  }

  default:
    llvm_unreachable("Not a nominal declaration!");
  }
}

EnumType::EnumType(EnumDecl *TheDecl, Type Parent, const ASTContext &C,
                     RecursiveTypeProperties properties)
  : NominalType(TypeKind::Enum, &C, TheDecl, Parent, properties) { }

EnumType *EnumType::get(EnumDecl *D, Type Parent, const ASTContext &C) {
  RecursiveTypeProperties properties;
  if (Parent) properties |= Parent->getRecursiveProperties();
  auto arena = getArena(properties);

  auto *&known = C.getImpl().getArena(arena).EnumTypes[{D, Parent}];
  if (!known) {
    known = new (C, arena) EnumType(D, Parent, C, properties);
  }
  return known;
}

StructType::StructType(StructDecl *TheDecl, Type Parent, const ASTContext &C,
                       RecursiveTypeProperties properties)
  : NominalType(TypeKind::Struct, &C, TheDecl, Parent, properties) { }

StructType *StructType::get(StructDecl *D, Type Parent, const ASTContext &C) {
  RecursiveTypeProperties properties;
  if (Parent) properties |= Parent->getRecursiveProperties();
  auto arena = getArena(properties);

  auto *&known = C.getImpl().getArena(arena).StructTypes[{D, Parent}];
  if (!known) {
    known = new (C, arena) StructType(D, Parent, C, properties);
  }
  return known;
}

ClassType::ClassType(ClassDecl *TheDecl, Type Parent, const ASTContext &C,
                     RecursiveTypeProperties properties)
  : NominalType(TypeKind::Class, &C, TheDecl, Parent, properties) { }

ClassType *ClassType::get(ClassDecl *D, Type Parent, const ASTContext &C) {
  RecursiveTypeProperties properties;
  if (Parent) properties |= Parent->getRecursiveProperties();
  auto arena = getArena(properties);

  auto *&known = C.getImpl().getArena(arena).ClassTypes[{D, Parent}];
  if (!known) {
    known = new (C, arena) ClassType(D, Parent, C, properties);
  }
  return known;
}

ProtocolCompositionType *
ProtocolCompositionType::build(const ASTContext &C, ArrayRef<Type> Members,
                               bool HasExplicitAnyObject) {
  // Check to see if we've already seen this protocol composition before.
  void *InsertPos = nullptr;
  llvm::FoldingSetNodeID ID;
  ProtocolCompositionType::Profile(ID, Members, HasExplicitAnyObject);

  bool isCanonical = true;
  RecursiveTypeProperties properties;
  for (Type t : Members) {
    if (!t->isCanonical())
      isCanonical = false;
    properties |= t->getRecursiveProperties();
  }

  // Create a new protocol composition type.
  auto arena = getArena(properties);

  if (auto compTy
      = C.getImpl().getArena(arena).ProtocolCompositionTypes
          .FindNodeOrInsertPos(ID, InsertPos))
    return compTy;

  // Use trailing objects for member type storage
  auto size = totalSizeToAlloc<Type>(Members.size());
  auto mem = C.Allocate(size, alignof(ProtocolCompositionType), arena);
  auto compTy = new (mem) ProtocolCompositionType(isCanonical ? &C : nullptr,
                                                  Members,
                                                  HasExplicitAnyObject,
                                                  properties);
  C.getImpl().getArena(arena).ProtocolCompositionTypes.InsertNode(compTy, InsertPos);
  return compTy;
}

ReferenceStorageType *ReferenceStorageType::get(Type T,
                                                ReferenceOwnership ownership,
                                                const ASTContext &C) {
  assert(!T->hasTypeVariable()); // not meaningful in type-checker
  assert(!T->hasPlaceholder());
  switch (optionalityOf(ownership)) {
  case ReferenceOwnershipOptionality::Disallowed:
    assert(!T->getOptionalObjectType() && "optional type is disallowed");
    break;
  case ReferenceOwnershipOptionality::Allowed:
    break;
  case ReferenceOwnershipOptionality::Required:
    assert(T->getOptionalObjectType() && "optional type is required");
    break;
  }

  auto properties = T->getRecursiveProperties();
  auto arena = getArena(properties);

  auto key = uintptr_t(T.getPointer()) | unsigned(ownership);
  auto &entry = C.getImpl().getArena(arena).ReferenceStorageTypes[key];
  if (entry) return entry;

  switch (ownership) {
  case ReferenceOwnership::Strong:
    llvm_unreachable("strong ownership does not use ReferenceStorageType");
#define REF_STORAGE(Name, ...) \
  case ReferenceOwnership::Name: \
    return entry = new (C, arena) \
      Name##StorageType(T, T->isCanonical() ? &C : nullptr, properties);
#include "swift/AST/ReferenceStorage.def"
  }
  llvm_unreachable("bad ownership");
}

AnyMetatypeType::AnyMetatypeType(TypeKind kind, const ASTContext *C,
                                 RecursiveTypeProperties properties,
                                 Type instanceType,
                                 Optional<MetatypeRepresentation> repr)
    : TypeBase(kind, C, properties), InstanceType(instanceType) {
  if (repr) {
    Bits.AnyMetatypeType.Representation = static_cast<char>(*repr) + 1;
  } else {
    Bits.AnyMetatypeType.Representation = 0;
  }
}

MetatypeType *MetatypeType::get(Type T, Optional<MetatypeRepresentation> Repr,
                                const ASTContext &Ctx) {
  auto properties = T->getRecursiveProperties();
  auto arena = getArena(properties);

  unsigned reprKey;
  if (Repr.hasValue())
    reprKey = static_cast<unsigned>(*Repr) + 1;
  else
    reprKey = 0;

  auto pair = llvm::PointerIntPair<TypeBase*, 3, unsigned>(T.getPointer(),
                                                           reprKey);

  MetatypeType *&Entry = Ctx.getImpl().getArena(arena).MetatypeTypes[pair];
  if (Entry) return Entry;

  return Entry = new (Ctx, arena) MetatypeType(
             T, T->isCanonical() ? &Ctx : nullptr, properties, Repr);
}

MetatypeType::MetatypeType(Type T, const ASTContext *C,
                           RecursiveTypeProperties properties,
                           Optional<MetatypeRepresentation> repr)
  : AnyMetatypeType(TypeKind::Metatype, C, properties, T, repr) {
}

ExistentialMetatypeType *
ExistentialMetatypeType::get(Type T, Optional<MetatypeRepresentation> repr,
                             const ASTContext &ctx) {
  // If we're creating an existential metatype from an
  // existential type, wrap the constraint type direcly.
  if (auto existential = T->getAs<ExistentialType>())
    T = existential->getConstraintType();

  auto properties = T->getRecursiveProperties();
  auto arena = getArena(properties);

  unsigned reprKey;
  if (repr.hasValue())
    reprKey = static_cast<unsigned>(*repr) + 1;
  else
    reprKey = 0;

  auto pair = llvm::PointerIntPair<TypeBase*, 3, unsigned>(T.getPointer(),
                                                           reprKey);

  auto &entry = ctx.getImpl().getArena(arena).ExistentialMetatypeTypes[pair];
  if (entry) return entry;

  return entry = new (ctx, arena) ExistentialMetatypeType(
             T, T->isCanonical() ? &ctx : nullptr, properties, repr);
}

ExistentialMetatypeType::ExistentialMetatypeType(Type T,
                                                 const ASTContext *C,
                                       RecursiveTypeProperties properties,
                                       Optional<MetatypeRepresentation> repr)
  : AnyMetatypeType(TypeKind::ExistentialMetatype, C, properties, T, repr) {
  if (repr) {
    assert(*repr != MetatypeRepresentation::Thin &&
           "creating a thin existential metatype?");
    assert(getASTContext().LangOpts.EnableObjCInterop ||
           *repr != MetatypeRepresentation::ObjC);
  }
}

Type ExistentialMetatypeType::getExistentialInstanceType() {
  return ExistentialType::get(getInstanceType());
}

ModuleType *ModuleType::get(ModuleDecl *M) {
  ASTContext &C = M->getASTContext();

  ModuleType *&Entry = C.getImpl().ModuleTypes[M];
  if (Entry) return Entry;

  return Entry = new (C, AllocationArena::Permanent) ModuleType(M, C);
}

DynamicSelfType *DynamicSelfType::get(Type selfType, const ASTContext &ctx) {
  assert(selfType->isMaterializable()
         && "non-materializable dynamic self?");
  
  auto properties = selfType->getRecursiveProperties();
  auto arena = getArena(properties);

  auto &dynamicSelfTypes = ctx.getImpl().getArena(arena).DynamicSelfTypes;
  auto known = dynamicSelfTypes.find(selfType);
  if (known != dynamicSelfTypes.end())
    return known->second;

  auto result = new (ctx, arena) DynamicSelfType(selfType, ctx, properties);
  dynamicSelfTypes.insert({selfType, result});
  return result;
}

static RecursiveTypeProperties
getFunctionRecursiveProperties(ArrayRef<AnyFunctionType::Param> params,
                               Type result, Type globalActor) {
  RecursiveTypeProperties properties;
  for (auto param : params)
    properties |= param.getPlainType()->getRecursiveProperties();
  properties |= result->getRecursiveProperties();
  if (globalActor)
    properties |= globalActor->getRecursiveProperties();
  properties &= ~RecursiveTypeProperties::IsLValue;
  return properties;
}

static bool
isAnyFunctionTypeCanonical(ArrayRef<AnyFunctionType::Param> params,
                        Type result) {
  for (auto param : params) {
    if (!param.getPlainType()->isCanonical())
      return false;
    if (!param.getInternalLabel().empty()) {
      // Canonical types don't have internal labels
      return false;
    }
  }

  return result->isCanonical();
}

// For now, generic function types cannot be dependent (in fact,
// they erase dependence) or contain type variables, and they're
// always materializable.
static RecursiveTypeProperties
getGenericFunctionRecursiveProperties(ArrayRef<AnyFunctionType::Param> params,
                                      Type result) {
  static_assert(RecursiveTypeProperties::BitWidth == 12,
                "revisit this if you add new recursive type properties");
  RecursiveTypeProperties properties;

  for (auto param : params) {
    if (param.getPlainType()->getRecursiveProperties().hasError())
      properties |= RecursiveTypeProperties::HasError;
  }

  if (result->getRecursiveProperties().hasDynamicSelf())
    properties |= RecursiveTypeProperties::HasDynamicSelf;
  if (result->getRecursiveProperties().hasError())
    properties |= RecursiveTypeProperties::HasError;

  return properties;
}

static bool
isGenericFunctionTypeCanonical(GenericSignature sig,
                               ArrayRef<AnyFunctionType::Param> params,
                               Type result) {
  if (!sig->isCanonical())
    return false;

  for (auto param : params) {
    if (!sig->isCanonicalTypeInContext(param.getPlainType()))
      return false;
    if (!param.getInternalLabel().empty()) {
      // Canonical types don't have internal labels
      return false;
    }
  }

  return sig->isCanonicalTypeInContext(result);
}

AnyFunctionType *AnyFunctionType::withExtInfo(ExtInfo info) const {
  if (isa<FunctionType>(this))
    return FunctionType::get(getParams(), getResult(), info);

  auto *genFnTy = cast<GenericFunctionType>(this);
  return GenericFunctionType::get(genFnTy->getGenericSignature(),
                                  getParams(), getResult(), info);
}

Type AnyFunctionType::Param::getParameterType(bool forCanonical,
                                              ASTContext *ctx) const {
  Type type = getPlainType();
  if (isVariadic()) {
    if (!ctx) ctx = &type->getASTContext();
    auto arrayDecl = ctx->getArrayDecl();
    if (!arrayDecl)
      type = ErrorType::get(*ctx);
    else if (forCanonical)
      type = BoundGenericType::get(arrayDecl, Type(), {type});
    else
      type = VariadicSequenceType::get(type);
  }
  return type;
}

Type AnyFunctionType::composeTuple(ASTContext &ctx, ArrayRef<Param> params,
                                   bool wantParamFlags) {
  SmallVector<TupleTypeElt, 4> elements;
  for (const auto &param : params) {
    auto flags = wantParamFlags ? param.getParameterFlags()
                                : ParameterTypeFlags();
    elements.emplace_back(param.getParameterType(), param.getLabel(), flags);
  }
  return TupleType::get(elements, ctx);
}

bool AnyFunctionType::equalParams(ArrayRef<AnyFunctionType::Param> a,
                                  ArrayRef<AnyFunctionType::Param> b) {
  if (a.size() != b.size())
    return false;

  for (unsigned i = 0, n = a.size(); i != n; ++i) {
    if (a[i] != b[i])
      return false;
  }

  return true;
}

bool AnyFunctionType::equalParams(CanParamArrayRef a, CanParamArrayRef b) {
  if (a.size() != b.size())
    return false;

  for (unsigned i = 0, n = a.size(); i != n; ++i) {
    if (a[i] != b[i])
      return false;
  }

  return true;
}

void AnyFunctionType::relabelParams(MutableArrayRef<Param> params,
                                    ArgumentList *argList) {
  assert(params.size() == argList->size());
  for (auto i : indices(params)) {
    auto &param = params[i];
    param = AnyFunctionType::Param(param.getPlainType(), argList->getLabel(i),
                                   param.getParameterFlags(),
                                   param.getInternalLabel());
  }
}

/// Profile \p params into \p ID. In contrast to \c == on \c Param, the profile
/// *does* take the internal label into account and *does not* canonicalize
/// the param's type.
static void profileParams(llvm::FoldingSetNodeID &ID,
                          ArrayRef<AnyFunctionType::Param> params) {
  ID.AddInteger(params.size());
  for (auto param : params) {
    ID.AddPointer(param.getLabel().get());
    ID.AddPointer(param.getInternalLabel().get());
    ID.AddPointer(param.getPlainType().getPointer());
    ID.AddInteger(param.getParameterFlags().toRaw());
  }
}

void FunctionType::Profile(llvm::FoldingSetNodeID &ID,
                           ArrayRef<AnyFunctionType::Param> params, Type result,
                           Optional<ExtInfo> info) {
  profileParams(ID, params);
  ID.AddPointer(result.getPointer());
  if (info.hasValue()) {
    auto infoKey = info.getValue().getFuncAttrKey();
    ID.AddInteger(std::get<0>(infoKey));
    ID.AddPointer(std::get<1>(infoKey));
    ID.AddPointer(std::get<2>(infoKey));
  }
}

FunctionType *FunctionType::get(ArrayRef<AnyFunctionType::Param> params,
                                Type result, Optional<ExtInfo> info) {
  Type globalActor;
  if (info.hasValue())
    globalActor = info->getGlobalActor();

  auto properties = getFunctionRecursiveProperties(params, result, globalActor);
  auto arena = getArena(properties);

  llvm::FoldingSetNodeID id;
  FunctionType::Profile(id, params, result, info);

  const ASTContext &ctx = result->getASTContext();

  // Do we already have this generic function type?
  void *insertPos;
  if (auto funcTy =
        ctx.getImpl().getArena(arena).FunctionTypes.FindNodeOrInsertPos(id, insertPos)) {
    return funcTy;
  }

  ClangTypeInfo clangTypeInfo;
  if (info.hasValue())
    clangTypeInfo = info.getValue().getClangTypeInfo();

  bool hasClangInfo =
      info.hasValue() && !info.getValue().getClangTypeInfo().empty();

  size_t allocSize = totalSizeToAlloc<
      AnyFunctionType::Param, ClangTypeInfo, Type
    >(params.size(), hasClangInfo ? 1 : 0, globalActor ? 1 : 0);
  void *mem = ctx.Allocate(allocSize, alignof(FunctionType), arena);

  bool isCanonical = isAnyFunctionTypeCanonical(params, result);
  if (!clangTypeInfo.empty()) {
    if (ctx.LangOpts.UseClangFunctionTypes)
      isCanonical &= clangTypeInfo.getType()->isCanonicalUnqualified();
    else
      isCanonical = false;
  }

  if (globalActor && !globalActor->isCanonical())
    isCanonical = false;

  auto funcTy = new (mem) FunctionType(params, result, info,
                                       isCanonical ? &ctx : nullptr,
                                       properties);
  ctx.getImpl().getArena(arena).FunctionTypes.InsertNode(funcTy, insertPos);
  return funcTy;
}

// If the input and result types are canonical, then so is the result.
FunctionType::FunctionType(ArrayRef<AnyFunctionType::Param> params, Type output,
                           Optional<ExtInfo> info, const ASTContext *ctx,
                           RecursiveTypeProperties properties)
    : AnyFunctionType(TypeKind::Function, ctx, output, properties,
                      params.size(), info) {
  std::uninitialized_copy(params.begin(), params.end(),
                          getTrailingObjects<AnyFunctionType::Param>());
  if (info.hasValue()) {
    auto clangTypeInfo = info.getValue().getClangTypeInfo();
    if (!clangTypeInfo.empty())
      *getTrailingObjects<ClangTypeInfo>() = clangTypeInfo;
    if (Type globalActor = info->getGlobalActor())
      *getTrailingObjects<Type>() = globalActor;
  }
}

void GenericFunctionType::Profile(llvm::FoldingSetNodeID &ID,
                                  GenericSignature sig,
                                  ArrayRef<AnyFunctionType::Param> params,
                                  Type result, Optional<ExtInfo> info) {
  ID.AddPointer(sig.getPointer());
  profileParams(ID, params);
  ID.AddPointer(result.getPointer());
  if (info.hasValue()) {
    auto infoKey = info.getValue().getFuncAttrKey();
    ID.AddInteger(std::get<0>(infoKey));
    ID.AddPointer(std::get<1>(infoKey));
    ID.AddPointer(std::get<2>(infoKey));
  }
}

GenericFunctionType *GenericFunctionType::get(GenericSignature sig,
                                              ArrayRef<Param> params,
                                              Type result,
                                              Optional<ExtInfo> info) {
  assert(sig && "no generic signature for generic function type?!");
  assert(!result->hasTypeVariable());

  llvm::FoldingSetNodeID id;
  GenericFunctionType::Profile(id, sig, params, result, info);

  const ASTContext &ctx = result->getASTContext();

  // Do we already have this generic function type?
  void *insertPos;
  if (auto result
        = ctx.getImpl().GenericFunctionTypes.FindNodeOrInsertPos(id, insertPos)) {
    return result;
  }

  // We have to construct this generic function type. Determine whether
  // it's canonical.  Unfortunately, isCanonicalTypeInContext can cause
  // new GenericFunctionTypes to be created and thus invalidate our insertion
  // point.
  bool isCanonical = isGenericFunctionTypeCanonical(sig, params, result);

  assert((!info.hasValue() || info.getValue().getClangTypeInfo().empty()) &&
         "Generic functions do not have Clang types at the moment.");

  if (auto funcTy
        = ctx.getImpl().GenericFunctionTypes.FindNodeOrInsertPos(id, insertPos)) {
    return funcTy;
  }

  Type globalActor;
  if (info.hasValue())
    globalActor = info->getGlobalActor();

  if (globalActor && !sig->isCanonicalTypeInContext(globalActor))
    isCanonical = false;

  size_t allocSize = totalSizeToAlloc<AnyFunctionType::Param, Type>(
      params.size(), globalActor ? 1 : 0);
  void *mem = ctx.Allocate(allocSize, alignof(GenericFunctionType));

  auto properties = getGenericFunctionRecursiveProperties(params, result);
  auto funcTy = new (mem) GenericFunctionType(sig, params, result, info,
                                              isCanonical ? &ctx : nullptr,
                                              properties);

  ctx.getImpl().GenericFunctionTypes.InsertNode(funcTy, insertPos);
  return funcTy;
}

GenericFunctionType::GenericFunctionType(
                       GenericSignature sig,
                       ArrayRef<AnyFunctionType::Param> params,
                       Type result,
                       Optional<ExtInfo> info,
                       const ASTContext *ctx,
                       RecursiveTypeProperties properties)
  : AnyFunctionType(TypeKind::GenericFunction, ctx, result,
                    properties, params.size(), info), Signature(sig) {
  std::uninitialized_copy(params.begin(), params.end(),
                          getTrailingObjects<AnyFunctionType::Param>());
  if (info) {
    if (Type globalActor = info->getGlobalActor())
      *getTrailingObjects<Type>() = globalActor;
  }
}

GenericTypeParamType *GenericTypeParamType::get(bool isTypeSequence,
                                                unsigned depth, unsigned index,
                                                const ASTContext &ctx) {
  const auto depthKey = depth | ((isTypeSequence ? 1 : 0) << 30);
  auto known = ctx.getImpl().GenericParamTypes.find({depthKey, index});
  if (known != ctx.getImpl().GenericParamTypes.end())
    return known->second;

  auto result = new (ctx, AllocationArena::Permanent)
      GenericTypeParamType(isTypeSequence, depth, index, ctx);
  ctx.getImpl().GenericParamTypes[{depthKey, index}] = result;
  return result;
}

TypeArrayView<GenericTypeParamType>
GenericFunctionType::getGenericParams() const {
  return Signature.getGenericParams();
}

/// Retrieve the requirements of this polymorphic function type.
ArrayRef<Requirement> GenericFunctionType::getRequirements() const {
  return Signature.getRequirements();
}

void SILFunctionType::Profile(
    llvm::FoldingSetNodeID &id,
    GenericSignature genericParams,
    ExtInfo info,
    SILCoroutineKind coroutineKind,
    ParameterConvention calleeConvention,
    ArrayRef<SILParameterInfo> params,
    ArrayRef<SILYieldInfo> yields,
    ArrayRef<SILResultInfo> results,
    Optional<SILResultInfo> errorResult,
    ProtocolConformanceRef conformance,
    SubstitutionMap patternSubs,
    SubstitutionMap invocationSubs) {
  id.AddPointer(genericParams.getPointer());
  auto infoKey = info.getFuncAttrKey();
  id.AddInteger(infoKey.first);
  id.AddPointer(infoKey.second);
  id.AddInteger(unsigned(coroutineKind));
  id.AddInteger(unsigned(calleeConvention));
  id.AddInteger(params.size());
  for (auto param : params)
    param.profile(id);
  id.AddInteger(yields.size());
  for (auto yield : yields)
    yield.profile(id);
  id.AddInteger(results.size());
  for (auto result : results)
    result.profile(id);

  // Just allow the profile length to implicitly distinguish the
  // presence of an error result.
  if (errorResult) errorResult->profile(id);
  patternSubs.profile(id);
  invocationSubs.profile(id);
  id.AddBoolean((bool)conformance);
  if (conformance)
    id.AddPointer(conformance.getRequirement());
}

SILFunctionType::SILFunctionType(
    GenericSignature genericSig,
    ExtInfo ext,
    SILCoroutineKind coroutineKind,
    ParameterConvention calleeConvention,
    ArrayRef<SILParameterInfo> params,
    ArrayRef<SILYieldInfo> yields,
    ArrayRef<SILResultInfo> normalResults,
    Optional<SILResultInfo> errorResult,
    SubstitutionMap patternSubs,
    SubstitutionMap invocationSubs,
    const ASTContext &ctx,
    RecursiveTypeProperties properties,
    ProtocolConformanceRef witnessMethodConformance)
    : TypeBase(TypeKind::SILFunction, &ctx, properties),
      InvocationGenericSig(CanGenericSignature(genericSig)),
      WitnessMethodConformance(witnessMethodConformance) {

  Bits.SILFunctionType.HasErrorResult = errorResult.hasValue();
  Bits.SILFunctionType.ExtInfoBits = ext.getBits();
  Bits.SILFunctionType.HasClangTypeInfo = false;
  Bits.SILFunctionType.HasPatternSubs = (bool) patternSubs;
  Bits.SILFunctionType.HasInvocationSubs = (bool) invocationSubs;
  // The use of both assert() and static_assert() below is intentional.
  assert(Bits.SILFunctionType.ExtInfoBits == ext.getBits() &&
         "Bits were dropped!");
  static_assert(SILExtInfoBuilder::NumMaskBits == NumSILExtInfoBits,
                "ExtInfo and SILFunctionTypeBitfields must agree on bit size");
  Bits.SILFunctionType.HasClangTypeInfo = !ext.getClangTypeInfo().empty();
  Bits.SILFunctionType.CoroutineKind = unsigned(coroutineKind);
  NumParameters = params.size();
  if (coroutineKind == SILCoroutineKind::None) {
    assert(yields.empty());
    NumAnyResults = normalResults.size();
    NumAnyIndirectFormalResults =
      std::count_if(normalResults.begin(), normalResults.end(),
                    [](const SILResultInfo &resultInfo) {
                      return resultInfo.isFormalIndirect();
                    });
    memcpy(getMutableResults().data(), normalResults.data(),
           normalResults.size() * sizeof(SILResultInfo));
  } else {
    assert(normalResults.empty());    
    NumAnyResults = yields.size();
    NumAnyIndirectFormalResults = 0; // unused
    memcpy(getMutableYields().data(), yields.data(),
           yields.size() * sizeof(SILYieldInfo));
  }

  assert(!isIndirectFormalParameter(calleeConvention));
  Bits.SILFunctionType.CalleeConvention = unsigned(calleeConvention);

  memcpy(getMutableParameters().data(), params.data(),
         params.size() * sizeof(SILParameterInfo));
  if (errorResult)
    getMutableErrorResult() = *errorResult;

  if (patternSubs)
    getMutablePatternSubs() = patternSubs;
  if (invocationSubs)
    getMutableInvocationSubs() = invocationSubs;

  if (hasResultCache()) {
    getMutableFormalResultsCache() = CanType();
    getMutableAllResultsCache() = CanType();
  }
  if (!ext.getClangTypeInfo().empty())
    *getTrailingObjects<ClangTypeInfo>() = ext.getClangTypeInfo();

#ifndef NDEBUG
  if (ext.getRepresentation() == Representation::WitnessMethod)
    assert(!WitnessMethodConformance.isInvalid() &&
           "witness_method SIL function without a conformance");
  else
    assert(WitnessMethodConformance.isInvalid() &&
           "non-witness_method SIL function with a conformance");

  // Make sure the type follows invariants.
  assert((!invocationSubs || genericSig)
         && "can only have substitutions with a generic signature");
        
  if (invocationSubs) {
    assert(invocationSubs.getGenericSignature().getCanonicalSignature() ==
               genericSig.getCanonicalSignature() &&
           "substitutions must match generic signature");
  }
        
  if (genericSig) {
    assert(!genericSig->areAllParamsConcrete() &&
           "If all generic parameters are concrete, SILFunctionType should "
           "not have a generic signature at all");

    for (auto gparam : genericSig.getGenericParams()) {
      (void)gparam;
      assert(gparam->isCanonical() && "generic signature is not canonicalized");
    }
  }

  if (genericSig || patternSubs) {
    for (auto param : getParameters()) {
      (void)param;
      assert(!param.getInterfaceType()->hasError()
             && "interface type of parameter should not contain error types");
      assert(!param.getInterfaceType()->hasArchetype()
             && "interface type of parameter should not contain context archetypes");
    }
    for (auto result : getResults()) {
      (void)result;
      assert(!result.getInterfaceType()->hasError()
             && "interface type of result should not contain error types");
      assert(!result.getInterfaceType()->hasArchetype()
             && "interface type of result should not contain context archetypes");
    }
    for (auto yield : getYields()) {
      (void)yield;
      assert(!yield.getInterfaceType()->hasError()
             && "interface type of yield should not contain error types");
      assert(!yield.getInterfaceType()->hasArchetype()
             && "interface type of yield should not contain context archetypes");
    }
    if (hasErrorResult()) {
      assert(!getErrorResult().getInterfaceType()->hasError()
             && "interface type of result should not contain error types");
      assert(!getErrorResult().getInterfaceType()->hasArchetype()
             && "interface type of result should not contain context archetypes");
    }

    if (genericSig && patternSubs) {
      assert(!patternSubs.hasArchetypes()
             && "pattern substitutions should not contain context archetypes");
    }
  }
  for (auto result : getResults()) {
    (void)result;
    if (auto *FnType = result.getInterfaceType()->getAs<SILFunctionType>()) {
      assert(!FnType->isNoEscape() &&
             "Cannot return an @noescape function type");
    }
  }

  // Check that `@noDerivative` parameters and results only exist in
  // `@differentiable` function types.
  if (!ext.isDifferentiable()) {
    for (auto param : getParameters()) {
      assert(param.getDifferentiability() ==
                 SILParameterDifferentiability::DifferentiableOrNotApplicable &&
             "non-`@differentiable` function type should not have "
             "`@noDerivative` parameter");
    }
    for (auto result : getResults()) {
      assert(result.getDifferentiability() ==
                 SILResultDifferentiability::DifferentiableOrNotApplicable &&
             "non-`@differentiable` function type should not have "
             "`@noDerivative` result");
    }
  }
#endif
}

CanSILBlockStorageType SILBlockStorageType::get(CanType captureType) {
  ASTContext &ctx = captureType->getASTContext();
  auto found = ctx.getImpl().SILBlockStorageTypes.find(captureType);
  if (found != ctx.getImpl().SILBlockStorageTypes.end())
    return CanSILBlockStorageType(found->second);
  
  void *mem = ctx.Allocate(sizeof(SILBlockStorageType),
                           alignof(SILBlockStorageType));
  
  SILBlockStorageType *storageTy = new (mem) SILBlockStorageType(captureType);
  ctx.getImpl().SILBlockStorageTypes.insert({captureType, storageTy});
  return CanSILBlockStorageType(storageTy);
}

CanSILFunctionType SILFunctionType::get(
    GenericSignature genericSig,
    ExtInfo ext, SILCoroutineKind coroutineKind,
    ParameterConvention callee,
    ArrayRef<SILParameterInfo> params,
    ArrayRef<SILYieldInfo> yields,
    ArrayRef<SILResultInfo> normalResults,
    Optional<SILResultInfo> errorResult,
    SubstitutionMap patternSubs,
    SubstitutionMap invocationSubs,
    const ASTContext &ctx,
    ProtocolConformanceRef witnessMethodConformance) {
  assert(coroutineKind == SILCoroutineKind::None || normalResults.empty());
  assert(coroutineKind != SILCoroutineKind::None || yields.empty());
  assert(!ext.isPseudogeneric() || genericSig);
  
  patternSubs = patternSubs.getCanonical();
  invocationSubs = invocationSubs.getCanonical();

  // [FIXME: Clang-type-plumbing]
  if (ctx.LangOpts.UseClangFunctionTypes) {
    if (auto error = ext.checkClangType()) {
      error.getValue().dump();
      llvm_unreachable("Unexpected Clang type in SILExtInfo.");
    }
  } else if (!ext.getClangTypeInfo().empty()) {
    // Unlike AnyFunctionType, SILFunctionType is always canonical. Hence,
    // conditionalizing canonical type computation based on
    // UseClangFunctionTypes like AnyFunctionType is not feasible. It is simpler
    // to drop the Clang type altogether.
    ext = ext.intoBuilder().withClangFunctionType(nullptr).build();
  }

  
  llvm::FoldingSetNodeID id;
  SILFunctionType::Profile(id, genericSig, ext, coroutineKind, callee, params,
                           yields, normalResults, errorResult,
                           witnessMethodConformance,
                           patternSubs, invocationSubs);

  // Do we already have this generic function type?
  void *insertPos;
  if (auto result
        = ctx.getImpl().SILFunctionTypes.FindNodeOrInsertPos(id, insertPos))
    return CanSILFunctionType(result);

  // All SILFunctionTypes are canonical.

  // See [NOTE: SILFunctionType-layout]
  bool hasResultCache = normalResults.size() > 1;
  size_t bytes = totalSizeToAlloc<SILParameterInfo, SILResultInfo, SILYieldInfo,
                                  SubstitutionMap, CanType, ClangTypeInfo>(
      params.size(), normalResults.size() + (errorResult ? 1 : 0),
      yields.size(), (patternSubs ? 1 : 0) + (invocationSubs ? 1 : 0),
      hasResultCache ? 2 : 0, ext.getClangTypeInfo().empty() ? 0 : 1);

  void *mem = ctx.Allocate(bytes, alignof(SILFunctionType));

  RecursiveTypeProperties properties;
  static_assert(RecursiveTypeProperties::BitWidth == 12,
                "revisit this if you add new recursive type properties");
  for (auto &param : params)
    properties |= param.getInterfaceType()->getRecursiveProperties();
  for (auto &yield : yields)
    properties |= yield.getInterfaceType()->getRecursiveProperties();
  for (auto &result : normalResults)
    properties |= result.getInterfaceType()->getRecursiveProperties();
  if (errorResult)
    properties |= errorResult->getInterfaceType()->getRecursiveProperties();
  
  // FIXME: If we ever have first-class polymorphic values, we'll need to
  // revisit this.
  if (genericSig || patternSubs) {
    properties.removeHasTypeParameter();
    properties.removeHasDependentMember();
  }

  auto outerSubs = genericSig ? invocationSubs : patternSubs;
  for (auto replacement : outerSubs.getReplacementTypes()) {
    properties |= replacement->getRecursiveProperties();
  }

  auto fnType =
      new (mem) SILFunctionType(genericSig, ext, coroutineKind, callee,
                                params, yields, normalResults, errorResult,
                                patternSubs, invocationSubs,
                                ctx, properties, witnessMethodConformance);
  assert(fnType->hasResultCache() == hasResultCache);

  ctx.getImpl().SILFunctionTypes.InsertNode(fnType, insertPos);
  return CanSILFunctionType(fnType);
}

ArraySliceType *ArraySliceType::get(Type base) {
  auto properties = base->getRecursiveProperties();
  auto arena = getArena(properties);

  const ASTContext &C = base->getASTContext();

  ArraySliceType *&entry = C.getImpl().getArena(arena).ArraySliceTypes[base];
  if (entry) return entry;

  return entry = new (C, arena) ArraySliceType(C, base, properties);
}

VariadicSequenceType *VariadicSequenceType::get(Type base) {
  auto properties = base->getRecursiveProperties();
  auto arena = getArena(properties);

  const ASTContext &C = base->getASTContext();

  VariadicSequenceType *&entry = C.getImpl().getArena(arena).VariadicSequenceTypes[base];
  if (entry) return entry;

  return entry = new (C, arena) VariadicSequenceType(C, base, properties);
}

DictionaryType *DictionaryType::get(Type keyType, Type valueType) {
  auto properties = keyType->getRecursiveProperties() 
                  | valueType->getRecursiveProperties();
  auto arena = getArena(properties);

  const ASTContext &C = keyType->getASTContext();

  DictionaryType *&entry
    = C.getImpl().getArena(arena).DictionaryTypes[{keyType, valueType}];
  if (entry) return entry;

  return entry = new (C, arena) DictionaryType(C, keyType, valueType, 
                                               properties);
}

OptionalType *OptionalType::get(Type base) {
  auto properties = base->getRecursiveProperties();
  auto arena = getArena(properties);

  const ASTContext &C = base->getASTContext();

  OptionalType *&entry = C.getImpl().getArena(arena).OptionalTypes[base];
  if (entry) return entry;

  return entry = new (C, arena) OptionalType(C, base, properties);
}

ProtocolType *ProtocolType::get(ProtocolDecl *D, Type Parent,
                                const ASTContext &C) {
  RecursiveTypeProperties properties;
  if (Parent) properties |= Parent->getRecursiveProperties();
  auto arena = getArena(properties);

  auto *&known = C.getImpl().getArena(arena).ProtocolTypes[{D, Parent}];
  if (!known) {
    known = new (C, arena) ProtocolType(D, Parent, C, properties);
  }
  return known;
}

ProtocolType::ProtocolType(ProtocolDecl *TheDecl, Type Parent,
                           const ASTContext &Ctx,
                           RecursiveTypeProperties properties)
  : NominalType(TypeKind::Protocol, &Ctx, TheDecl, Parent, properties) { }

Type ExistentialType::get(Type constraint) {
  auto &C = constraint->getASTContext();
  if (!C.LangOpts.EnableExplicitExistentialTypes)
    return constraint;

  // FIXME: Any and AnyObject don't yet use ExistentialType.
  if (constraint->isAny() || constraint->isAnyObject())
    return constraint;

  // ExistentialMetatypeType is already an existential type.
  if (constraint->is<ExistentialMetatypeType>())
    return constraint;

  auto properties = constraint->getRecursiveProperties();
  auto arena = getArena(properties);

  auto &entry = C.getImpl().getArena(arena).ExistentialTypes[constraint];
  if (entry)
    return entry;

  const ASTContext *canonicalContext = constraint->isCanonical() ? &C : nullptr;
  return entry = new (C, arena) ExistentialType(constraint,
                                                canonicalContext,
                                                properties);
}

LValueType *LValueType::get(Type objectTy) {
  assert(!objectTy->is<LValueType>() && !objectTy->is<InOutType>() &&
         "cannot have 'inout' or @lvalue wrapped inside an @lvalue");

  auto properties = objectTy->getRecursiveProperties()
                    | RecursiveTypeProperties::IsLValue;
  auto arena = getArena(properties);

  auto &C = objectTy->getASTContext();
  auto &entry = C.getImpl().getArena(arena).LValueTypes[objectTy];
  if (entry)
    return entry;

  const ASTContext *canonicalContext = objectTy->isCanonical() ? &C : nullptr;
  return entry = new (C, arena) LValueType(objectTy, canonicalContext,
                                           properties);
}

InOutType *InOutType::get(Type objectTy) {
  assert(!objectTy->is<LValueType>() && !objectTy->is<InOutType>() &&
         "cannot have 'inout' or @lvalue wrapped inside an 'inout'");

  auto properties = objectTy->getRecursiveProperties();

  properties &= ~RecursiveTypeProperties::IsLValue;
  auto arena = getArena(properties);

  auto &C = objectTy->getASTContext();
  auto &entry = C.getImpl().getArena(arena).InOutTypes[objectTy];
  if (entry)
    return entry;

  const ASTContext *canonicalContext = objectTy->isCanonical() ? &C : nullptr;
  return entry = new (C, arena) InOutType(objectTy, canonicalContext,
                                          properties);
}

DependentMemberType *DependentMemberType::get(Type base, Identifier name) {
  auto properties = base->getRecursiveProperties();
  properties |= RecursiveTypeProperties::HasDependentMember;
  auto arena = getArena(properties);

  llvm::PointerUnion<Identifier, AssociatedTypeDecl *> stored(name);
  const ASTContext &ctx = base->getASTContext();
  auto *&known = ctx.getImpl().getArena(arena).DependentMemberTypes[
                                            {base, stored.getOpaqueValue()}];
  if (!known) {
    const ASTContext *canonicalCtx = base->isCanonical() ? &ctx : nullptr;
    known = new (ctx, arena) DependentMemberType(base, name, canonicalCtx,
                                                 properties);
  }
  return known;
}

DependentMemberType *DependentMemberType::get(Type base,
                                              AssociatedTypeDecl *assocType) {
  assert(assocType && "Missing associated type");
  auto properties = base->getRecursiveProperties();
  properties |= RecursiveTypeProperties::HasDependentMember;
  auto arena = getArena(properties);

  llvm::PointerUnion<Identifier, AssociatedTypeDecl *> stored(assocType);
  const ASTContext &ctx = base->getASTContext();
  auto *&known = ctx.getImpl().getArena(arena).DependentMemberTypes[
                                            {base, stored.getOpaqueValue()}];
  if (!known) {
    const ASTContext *canonicalCtx = base->isCanonical() ? &ctx : nullptr;
    known = new (ctx, arena) DependentMemberType(base, assocType, canonicalCtx,
                                                 properties);
  }
  return known;
}

OpaqueTypeArchetypeType *
OpaqueTypeArchetypeType::get(OpaqueTypeDecl *Decl, unsigned ordinal,
                             SubstitutionMap Substitutions) {
  // TODO [OPAQUE SUPPORT]: multiple opaque types
  assert(ordinal == 0 && "we only support one 'some' type per composite type");
  auto opaqueParamType = Decl->getUnderlyingInterfaceType();

  // TODO: We could attempt to preserve type sugar in the substitution map.
  // Currently archetypes are assumed to be always canonical in many places,
  // though, so doing so would require fixing those places.
  Substitutions = Substitutions.getCanonical();

  llvm::FoldingSetNodeID id;
  Profile(id, Decl, Substitutions);
  
  auto &ctx = Decl->getASTContext();

  // An opaque type isn't contextually dependent like other archetypes, so
  // by itself, it doesn't impose the "Has Archetype" recursive property,
  // but the substituted types might. A disjoint "Has Opaque Archetype" tracks
  // the presence of opaque archetypes.
  RecursiveTypeProperties properties =
    RecursiveTypeProperties::HasOpaqueArchetype;
  for (auto type : Substitutions.getReplacementTypes()) {
    properties |= type->getRecursiveProperties();
  }
  
  auto arena = getArena(properties);
  
  llvm::FoldingSet<OpaqueTypeArchetypeType> &set
    = ctx.getImpl().getArena(arena).OpaqueArchetypes;
  
  {
    void *insertPos; // Discarded because the work below may invalidate the
                     // insertion point inside the folding set
    if (auto existing = set.FindNodeOrInsertPos(id, insertPos)) {
      return existing;
    }
  }
  
  // Create a new opaque archetype.
  // It lives in an environment in which the interface generic arguments of the
  // decl have all been same-type-bound to the arguments from our substitution
  // map.
  SmallVector<Requirement, 2> newRequirements;

  // TODO: The proper thing to do to build the environment in which the opaque
  // type's archetype exists would be to take the generic signature of the
  // decl, feed it into a GenericSignatureBuilder, then add same-type
  // constraints into the builder to bind the outer generic parameters
  // to their substituted types provided by \c Substitutions. However,
  // this is problematic for interface types. In a situation like this:
  //
  // __opaque_type Foo<t_0_0: P>: Q // internal signature <t_0_0: P, t_1_0: Q>
  //
  // func bar<t_0_0, t_0_1, t_0_2: P>() -> Foo<t_0_2>
  //
  // we'd want to feed the GSB constraints to form:
  //
  // <t_0_0: P, t_1_0: Q where t_0_0 == t_0_2>
  //
  // even though t_0_2 isn't *in* the generic signature being built; it
  // represents a type
  // bound elsewhere from some other generic context. If we knew the generic
  // environment `t_0_2` came from, then maybe we could map it into that context,
  // but currently we have no way to know that with certainty.
  //
  // Because opaque types are currently limited so that they only have immediate
  // protocol constraints, and therefore don't interact with the outer generic
  // parameters at all, we can get away without adding these constraints for now.
  // Adding where clauses would break this hack.
#if DO_IT_CORRECTLY
  // Same-type-constrain the arguments in the outer signature to their
  // replacements in the substitution map.
  if (auto outerSig = Decl->getGenericSignature()) {
    for (auto outerParam : outerSig.getGenericParams()) {
      auto boundType = Type(outerParam).subst(Substitutions);
      newRequirements.push_back(
          Requirement(RequirementKind::SameType, Type(outerParam), boundType));
    }
  }
#else
  // Assert that there are no same type constraints on the opaque type or its
  // associated types.
  //
  // This should not be possible until we add where clause support, with the
  // exception of generic base class constraints (handled below).
  (void)newRequirements;
# ifndef NDEBUG
  for (auto req :
                Decl->getOpaqueInterfaceGenericSignature().getRequirements()) {
    auto reqBase = req.getFirstType()->getRootGenericParam();
    if (reqBase->isEqual(opaqueParamType)) {
      assert(req.getKind() != RequirementKind::SameType
             && "supporting where clauses on opaque types requires correctly "
                "setting up the generic environment for "
                "OpaqueTypeArchetypeTypes; see comment above");
    }
  }
# endif
#endif
  auto signature = buildGenericSignature(
        ctx,
        Decl->getOpaqueInterfaceGenericSignature(),
        /*genericParams=*/{ },
        std::move(newRequirements));

  auto reqs = signature->getLocalRequirements(opaqueParamType);
  auto superclass = reqs.superclass;
  #if !DO_IT_CORRECTLY
    // Ad-hoc substitute the generic parameters of the superclass.
    // If we correctly applied the substitutions to the generic signature
    // constraints above, this would be unnecessary.
    if (superclass && superclass->hasTypeParameter()) {
      superclass = superclass.subst(Substitutions);
    }
  #endif

  auto mem = ctx.Allocate(
    OpaqueTypeArchetypeType::totalSizeToAlloc<ProtocolDecl *, Type, LayoutConstraint>(
      reqs.protos.size(), superclass ? 1 : 0, reqs.layout ? 1 : 0),
      alignof(OpaqueTypeArchetypeType),
      arena);

  auto newOpaque = ::new (mem)
      OpaqueTypeArchetypeType(Decl, Substitutions, properties, opaqueParamType,
                              reqs.protos, superclass, reqs.layout);

  // Create a generic environment and bind the opaque archetype to the
  // opaque interface type from the decl's signature.
  auto *env = GenericEnvironment::getIncomplete(signature);
  env->addMapping(GenericParamKey(opaqueParamType), newOpaque);
  newOpaque->Environment = env;

  // Look up the insertion point in the folding set again in case something
  // invalidated it above.
  {
    void *insertPos;
    auto existing = set.FindNodeOrInsertPos(id, insertPos);
    (void)existing;
    assert(!existing && "race to create opaque archetype?!");
    set.InsertNode(newOpaque, insertPos);
  }

  return newOpaque;
}

CanOpenedArchetypeType OpenedArchetypeType::get(CanType existential,
                                                Optional<UUID> knownID) {
  // FIXME: Opened archetypes can't be transformed because the
  // the identity of the archetype has to be preserved. This
  // means that simplifying an opened archetype in the constraint
  // system to replace type variables with fixed types is not
  // yet supported. For now, assert that an opened archetype never
  // contains type variables to catch cases where type variables
  // would be applied to the type-checked AST.
  assert(!existential->hasTypeVariable() &&
         "opened existentials containing type variables cannot be simplified");

  auto &ctx = existential->getASTContext();
  auto &openedExistentialArchetypes = ctx.getImpl().OpenedExistentialArchetypes;
  // If we know the ID already...
  if (knownID) {
    // ... and we already have an archetype for that ID, return it.
    auto found = openedExistentialArchetypes.find(*knownID);
    
    if (found != openedExistentialArchetypes.end()) {
      auto result = found->second;
      assert(result->getOpenedExistentialType()->isEqual(existential) &&
             "Retrieved the wrong opened existential type?");
      return CanOpenedArchetypeType(result);
    }
  } else {
    // Create a new ID.
    knownID = UUID::fromTime();
  }

  auto layout = existential->getExistentialLayout();

  SmallVector<ProtocolDecl *, 2> protos;
  for (auto proto : layout.getProtocols())
    protos.push_back(proto->getDecl());

  auto layoutConstraint = layout.getLayoutConstraint();
  if (!layoutConstraint && layout.requiresClass()) {
    layoutConstraint = LayoutConstraint::getLayoutConstraint(
        LayoutConstraintKind::Class);
  }

  auto layoutSuperclass = layout.getSuperclass();

  auto arena = AllocationArena::Permanent;
  void *mem = ctx.Allocate(
    OpenedArchetypeType::totalSizeToAlloc<ProtocolDecl *, Type, LayoutConstraint>(
      protos.size(),
      layoutSuperclass ? 1 : 0,
      layoutConstraint ? 1 : 0),
      alignof(OpenedArchetypeType), arena);

  auto result =
      ::new (mem) OpenedArchetypeType(ctx, existential,
                                protos, layoutSuperclass,
                                layoutConstraint, *knownID);
  result->InterfaceType =
      GenericTypeParamType::get(/*type sequence*/ false,
                                /*depth*/ 0, /*index*/ 0, ctx);

  openedExistentialArchetypes[*knownID] = result;
  return CanOpenedArchetypeType(result);
}

GenericEnvironment *OpenedArchetypeType::getGenericEnvironment() const {
  if (Environment)
    return Environment;
  
  auto thisType = Type(const_cast<OpenedArchetypeType*>(this));
  auto &ctx = thisType->getASTContext();
  // Create a generic environment to represent the opened type.
  auto signature = ctx.getOpenedArchetypeSignature(Opened);
  auto *env = GenericEnvironment::getIncomplete(signature);
  env->addMapping(signature.getGenericParams().front().getPointer(), thisType);
  Environment = env;
  
  return env;
}

CanType OpenedArchetypeType::getAny(CanType existential) {
  if (auto metatypeTy = existential->getAs<ExistentialMetatypeType>()) {
    auto instanceTy =
        metatypeTy->getExistentialInstanceType()->getCanonicalType();
    return CanMetatypeType::get(OpenedArchetypeType::getAny(instanceTy));
  }
  assert(existential->isExistentialType());
  return OpenedArchetypeType::get(existential);
}

void SubstitutionMap::Storage::Profile(
                               llvm::FoldingSetNodeID &id,
                               GenericSignature genericSig,
                               ArrayRef<Type> replacementTypes,
                               ArrayRef<ProtocolConformanceRef> conformances) {
  id.AddPointer(genericSig.getPointer());
  if (!genericSig) return;

  // Profile those replacement types that corresponding to canonical generic
  // parameters within the generic signature.
  id.AddInteger(replacementTypes.size());

  unsigned i = 0;
  genericSig->forEachParam([&](GenericTypeParamType *gp, bool canonical) {
    if (canonical)
      id.AddPointer(replacementTypes[i].getPointer());
    else
      id.AddPointer(nullptr);
    ++i;
  });

  // Conformances.
  id.AddInteger(conformances.size());
  for (auto conformance : conformances)
    id.AddPointer(conformance.getOpaqueValue());
}

SubstitutionMap::Storage *SubstitutionMap::Storage::get(
                            GenericSignature genericSig,
                            ArrayRef<Type> replacementTypes,
                            ArrayRef<ProtocolConformanceRef> conformances) {
  // If there is no generic signature, we need no storage.
  if (!genericSig) {
    assert(replacementTypes.empty());
    assert(conformances.empty());
    return nullptr;
  }

  // Figure out which arena this should go in.
  RecursiveTypeProperties properties;
  for (auto type : replacementTypes) {
    if (type)
      properties |= type->getRecursiveProperties();
  }

  // Profile the substitution map.
  llvm::FoldingSetNodeID id;
  SubstitutionMap::Storage::Profile(id, genericSig, replacementTypes,
                                    conformances);

  auto arena = getArena(properties);

  // Did we already record this substitution map?
  auto &ctx = genericSig->getASTContext();
  void *insertPos;
  auto &substitutionMaps = ctx.getImpl().getArena(arena).SubstitutionMaps;
  if (auto result = substitutionMaps.FindNodeOrInsertPos(id, insertPos))
    return result;

  // Allocate the appropriate amount of storage for the signature and its
  // replacement types and conformances.
  auto size = Storage::totalSizeToAlloc<Type, ProtocolConformanceRef>(
                                                      replacementTypes.size(),
                                                      conformances.size());
  auto mem = ctx.Allocate(size, alignof(Storage), arena);

  auto result = new (mem) Storage(genericSig, replacementTypes, conformances);
  substitutionMaps.InsertNode(result, insertPos);
  return result;
}

void GenericSignatureImpl::Profile(llvm::FoldingSetNodeID &ID,
                              TypeArrayView<GenericTypeParamType> genericParams,
                              ArrayRef<Requirement> requirements) {
  for (auto p : genericParams)
    ID.AddPointer(p);

  for (auto &reqt : requirements) {
    ID.AddPointer(reqt.getFirstType().getPointer());
    if (reqt.getKind() != RequirementKind::Layout)
      ID.AddPointer(reqt.getSecondType().getPointer());
    else
      ID.AddPointer(reqt.getLayoutConstraint().getPointer());
    ID.AddInteger(unsigned(reqt.getKind()));
  }
}

GenericSignature 
GenericSignature::get(ArrayRef<GenericTypeParamType *> params,
                      ArrayRef<Requirement> requirements,
                      bool isKnownCanonical) {
  SmallVector<Type, 4> paramTypes;
  for (auto param : params)
    paramTypes.push_back(param);
  auto paramsView = TypeArrayView<GenericTypeParamType>(paramTypes);
  return get(paramsView, requirements, isKnownCanonical);
}

GenericSignature
GenericSignature::get(TypeArrayView<GenericTypeParamType> params,
                      ArrayRef<Requirement> requirements,
                      bool isKnownCanonical) {
  assert(!params.empty());

#ifndef NDEBUG
  for (auto req : requirements) {
    assert(req.getFirstType()->isTypeParameter());
    assert(!req.getFirstType()->hasTypeVariable());
    assert(req.getKind() == RequirementKind::Layout ||
           !req.getSecondType()->hasTypeVariable());
  }
#endif

  // Check for an existing generic signature.
  llvm::FoldingSetNodeID ID;
  GenericSignatureImpl::Profile(ID, params, requirements);

  auto &ctx = getASTContext(params, requirements);
  void *insertPos;
  auto &sigs = ctx.getImpl().GenericSignatures;
  if (auto *sig = sigs.FindNodeOrInsertPos(ID, insertPos)) {
    if (isKnownCanonical)
      sig->CanonicalSignatureOrASTContext = &ctx;

    return sig;
  }

  // Allocate and construct the new signature.
  size_t bytes =
      GenericSignatureImpl::template totalSizeToAlloc<Type, Requirement>(
          params.size(), requirements.size());
  void *mem = ctx.Allocate(bytes, alignof(GenericSignatureImpl));
  auto *newSig =
      new (mem) GenericSignatureImpl(params, requirements, isKnownCanonical);
  ctx.getImpl().GenericSignatures.InsertNode(newSig, insertPos);
  return newSig;
}

GenericEnvironment *GenericEnvironment::getIncomplete(
                                             GenericSignature signature) {
  auto &ctx = signature->getASTContext();

  // Allocate and construct the new environment.
  unsigned numGenericParams = signature.getGenericParams().size();
  size_t bytes = totalSizeToAlloc<Type>(numGenericParams);
  void *mem = ctx.Allocate(bytes, alignof(GenericEnvironment));
  return new (mem) GenericEnvironment(signature);
}

void DeclName::CompoundDeclName::Profile(llvm::FoldingSetNodeID &id,
                                         DeclBaseName baseName,
                                         ArrayRef<Identifier> argumentNames) {
  id.AddPointer(baseName.getAsOpaquePointer());
  id.AddInteger(argumentNames.size());
  for (auto arg : argumentNames)
    id.AddPointer(arg.get());
}

void DeclName::initialize(ASTContext &C, DeclBaseName baseName,
                          ArrayRef<Identifier> argumentNames) {
  llvm::FoldingSetNodeID id;
  CompoundDeclName::Profile(id, baseName, argumentNames);

  void *insert = nullptr;
  if (CompoundDeclName *compoundName
        = C.getImpl().CompoundNames.FindNodeOrInsertPos(id, insert)) {
    BaseNameOrCompound = compoundName;
    return;
  }

  size_t size =
      CompoundDeclName::totalSizeToAlloc<Identifier>(argumentNames.size());
  auto buf = C.Allocate(size, alignof(CompoundDeclName));
  auto compoundName = new (buf) CompoundDeclName(baseName,argumentNames.size());
  std::uninitialized_copy(argumentNames.begin(), argumentNames.end(),
                          compoundName->getArgumentNames().begin());
  BaseNameOrCompound = compoundName;
  C.getImpl().CompoundNames.InsertNode(compoundName, insert);
}

/// Build a compound value name given a base name and a set of argument names
/// extracted from a parameter list.
DeclName::DeclName(ASTContext &C, DeclBaseName baseName,
                   ParameterList *paramList) {
  SmallVector<Identifier, 4> names;
  
  for (auto P : *paramList)
    names.push_back(P->getArgumentName());
  initialize(C, baseName, names);
}

/// Find the implementation of the named type in the given module.
static NominalTypeDecl *findUnderlyingTypeInModule(ASTContext &ctx, 
                                                   Identifier name,
                                                   ModuleDecl *module) {
  // Find all of the declarations with this name in the Swift module.
  SmallVector<ValueDecl *, 1> results;
  module->lookupValue(name, NLKind::UnqualifiedLookup, results);
  for (auto result : results) {
    if (auto nominal = dyn_cast<NominalTypeDecl>(result))
      return nominal;

    // Look through typealiases.
    if (auto typealias = dyn_cast<TypeAliasDecl>(result)) {
      return typealias->getDeclaredInterfaceType()->getAnyNominal();
    }
  }

  return nullptr;
}

bool ForeignRepresentationInfo::isRepresentableAsOptional() const {
  switch (getKind()) {
  case ForeignRepresentableKind::None:
    llvm_unreachable("this type is not representable");

  case ForeignRepresentableKind::Trivial:
    return Storage.getPointer() != 0;

  case ForeignRepresentableKind::Bridged: {
    auto KPK_ObjectiveCBridgeable = KnownProtocolKind::ObjectiveCBridgeable;
    ProtocolDecl *proto = getConformance()->getProtocol();
    assert(proto->isSpecificProtocol(KPK_ObjectiveCBridgeable) &&
           "unknown protocol; does it support optional?");
    (void)proto;
    (void)KPK_ObjectiveCBridgeable;

    return true;
  }

  case ForeignRepresentableKind::BridgedError:
    return true;

  case ForeignRepresentableKind::Object:
  case ForeignRepresentableKind::StaticBridged:
    llvm_unreachable("unexpected kind in ForeignRepresentableCacheEntry");
  }

  llvm_unreachable("Unhandled ForeignRepresentableKind in switch.");
}

ForeignRepresentationInfo
ASTContext::getForeignRepresentationInfo(NominalTypeDecl *nominal,
                                         ForeignLanguage language,
                                         const DeclContext *dc) {
  // Local function to add a type with the given name and module as
  // trivially-representable.
  auto addTrivial = [&](Identifier name, ModuleDecl *module,
                        bool allowOptional = false) {
    if (auto type = findUnderlyingTypeInModule(*this, name, module)) {
      auto info = ForeignRepresentationInfo::forTrivial();
      if (allowOptional)
        info = ForeignRepresentationInfo::forTrivialWithOptional();
      getImpl().ForeignRepresentableCache.insert({type, info});
    }
  };

  if (getImpl().ForeignRepresentableCache.empty()) {
    // Pre-populate the foreign-representable cache with known types.
    if (auto stdlib = getStdlibModule()) {
      addTrivial(getIdentifier("OpaquePointer"), stdlib, true);

      // Builtin types
      // FIXME: Layering violation to use the ClangImporter's define.
#define MAP_BUILTIN_TYPE(CLANG_BUILTIN_KIND, SWIFT_TYPE_NAME) \
      addTrivial(getIdentifier(#SWIFT_TYPE_NAME), stdlib);
#include "swift/ClangImporter/BuiltinMappedTypes.def"

      // Even though we may never import types directly as Int or UInt
      // (e.g. on 64-bit Windows, where CLong maps to Int32 and
      // CLongLong to Int64), it's always possible to convert an Int
      // or UInt to a C type.
      addTrivial(getIdentifier("Int"), stdlib);
      addTrivial(getIdentifier("UInt"), stdlib);
    }

    if (auto darwin = getLoadedModule(Id_Darwin)) {
      // Note: DarwinBoolean is odd because it's bridged to Bool in APIs,
      // but can also be trivially bridged.
      addTrivial(getIdentifier("DarwinBoolean"), darwin);
    }

    if (auto winsdk = getLoadedModule(Id_WinSDK)) {
      // NOTE: WindowsBool is odd because it is bridged to Bool in APIs, but can
      // also be trivially bridged.
      addTrivial(getIdentifier("WindowsBool"), winsdk);
    }

    if (auto objectiveC = getLoadedModule(Id_ObjectiveC)) {
      addTrivial(Id_Selector, objectiveC, true);

      // Note: ObjCBool is odd because it's bridged to Bool in APIs,
      // but can also be trivially bridged.
      addTrivial(getIdentifier("ObjCBool"), objectiveC);

      addTrivial(getSwiftId(KnownFoundationEntity::NSZone), objectiveC, true);
    }

    if (auto coreGraphics = getLoadedModule(getIdentifier("CoreGraphics"))) {
      addTrivial(Id_CGFloat, coreGraphics);
    }

    // Pull SIMD types of size 2...4 from the SIMD module, if it exists.
    // FIXME: Layering violation to use the ClangImporter's define.
    const unsigned SWIFT_MAX_IMPORTED_SIMD_ELEMENTS = 4;
    if (auto simd = getLoadedModule(Id_simd)) {
#define MAP_SIMD_TYPE(BASENAME, _, __)                                  \
      {                                                                 \
        char name[] = #BASENAME "0";                                    \
        for (unsigned i = 2; i <= SWIFT_MAX_IMPORTED_SIMD_ELEMENTS; ++i) { \
          *(std::end(name) - 2) = '0' + i;                              \
          addTrivial(getIdentifier(name), simd);                        \
        }                                                               \
      }
#include "swift/ClangImporter/SIMDMappedTypes.def"      
    }
  }

  // Determine whether we know anything about this nominal type
  // yet. If we've never seen this nominal type before, or if we have
  // an out-of-date negative cached value, we'll have to go looking.
  auto known = getImpl().ForeignRepresentableCache.find(nominal);
  bool wasNotFoundInCache = known == getImpl().ForeignRepresentableCache.end();

  // For the REPL. We might have initialized the cache above before CoreGraphics
  // was loaded.
  //   let s = "" // Here we initialize the ForeignRepresentableCache.
  //   import Foundation
  //   let pt = CGPoint(x: 1.0, y: 2.0) // Here we query for CGFloat.
  // Add CGFloat as trivial if we encounter it later.
  // If the type was not found check if it would be found after having recently
  // loaded the module.
  // Similar for types for other non stdlib modules.
  auto conditionallyAddTrivial = [&](NominalTypeDecl *nominalDecl,
                                     Identifier typeName, Identifier moduleName,
                                     bool allowOptional = false) {
    if (nominal->getName() == typeName && wasNotFoundInCache) {
      if (auto module = getLoadedModule(moduleName)) {
        addTrivial(typeName, module, allowOptional);
        known = getImpl().ForeignRepresentableCache.find(nominal);
        wasNotFoundInCache = known == getImpl().ForeignRepresentableCache.end();
      }
    }
  };
  conditionallyAddTrivial(nominal, getIdentifier("DarwinBoolean") , Id_Darwin);
  conditionallyAddTrivial(nominal, getIdentifier("WindowsBool"), Id_WinSDK);
  conditionallyAddTrivial(nominal, Id_Selector, Id_ObjectiveC, true);
  conditionallyAddTrivial(nominal, getIdentifier("ObjCBool"), Id_ObjectiveC);
  conditionallyAddTrivial(nominal, getSwiftId(KnownFoundationEntity::NSZone), Id_ObjectiveC, true);
  conditionallyAddTrivial(nominal, Id_CGFloat, getIdentifier("CoreGraphics"));
  const unsigned SWIFT_MAX_IMPORTED_SIMD_ELEMENTS = 4;
#define MAP_SIMD_TYPE(BASENAME, _, __)                                         \
  {                                                                            \
    char name[] = #BASENAME "0";                                               \
    for (unsigned i = 2; i <= SWIFT_MAX_IMPORTED_SIMD_ELEMENTS; ++i) {         \
      *(std::end(name) - 2) = '0' + i;                                         \
      conditionallyAddTrivial(nominal, getIdentifier(name), Id_simd);          \
    }                                                                          \
  }
#include "swift/ClangImporter/SIMDMappedTypes.def"

  if (wasNotFoundInCache ||
      (known->second.getKind() == ForeignRepresentableKind::None &&
       known->second.getGeneration() < CurrentGeneration)) {
    Optional<ForeignRepresentationInfo> result;

    // Look for a conformance to _ObjectiveCBridgeable (other than Optional's--
    // we don't want to allow exposing APIs with double-optional types like
    // NSObject??, even though Optional is bridged to its underlying type).
    //
    // FIXME: We're implicitly depending on the fact that lookupConformance
    // is global, ignoring the module we provide for it.
    if (nominal != dc->getASTContext().getOptionalDecl()) {
      if (auto objcBridgeable
            = getProtocol(KnownProtocolKind::ObjectiveCBridgeable)) {
        auto conformance = dc->getParentModule()->lookupConformance(
            nominal->getDeclaredInterfaceType(), objcBridgeable);
        if (conformance) {
          result =
              ForeignRepresentationInfo::forBridged(conformance.getConcrete());
        }
      }
    }

    // Error is bridged to NSError, when it's available.
    if (nominal == getErrorDecl() && getNSErrorDecl())
      result = ForeignRepresentationInfo::forBridgedError();

    // If we didn't find anything, mark the result as "None".
    if (!result)
      result = ForeignRepresentationInfo::forNone(CurrentGeneration);
    
    // Cache the result.
    known = getImpl().ForeignRepresentableCache.insert({ nominal, *result }).first;
  }

  // Map a cache entry to a result for this specific 
  auto entry = known->second;
  if (entry.getKind() == ForeignRepresentableKind::None)
    return entry;

  // Extract the protocol conformance.
  auto conformance = entry.getConformance();

  // If the conformance is not visible, fail.
  if (conformance && !conformance->isVisibleFrom(dc))
    return ForeignRepresentationInfo::forNone();

  // Language-specific filtering.
  switch (language) {
  case ForeignLanguage::C:
    // Ignore _ObjectiveCBridgeable conformances in C.
    if (conformance &&
        conformance->getProtocol()->isSpecificProtocol(
          KnownProtocolKind::ObjectiveCBridgeable))
      return ForeignRepresentationInfo::forNone();

    // Ignore error bridging in C.
    if (entry.getKind() == ForeignRepresentableKind::BridgedError)
      return ForeignRepresentationInfo::forNone();

    LLVM_FALLTHROUGH;

  case ForeignLanguage::ObjectiveC:
    return entry;
  }

  llvm_unreachable("Unhandled ForeignLanguage in switch.");
}

bool ASTContext::isTypeBridgedInExternalModule(
     NominalTypeDecl *nominal) const {
  return (nominal == getBoolDecl() ||
          nominal == getIntDecl() ||
          nominal == getInt64Decl() ||
          nominal == getInt32Decl() ||
          nominal == getInt16Decl() ||
          nominal == getInt8Decl() ||
          nominal == getUIntDecl() ||
          nominal == getUInt64Decl() ||
          nominal == getUInt32Decl() ||
          nominal == getUInt16Decl() ||
          nominal == getUInt8Decl() ||
          nominal == getFloatDecl() ||
          nominal == getDoubleDecl() ||
          nominal == getArrayDecl() ||
          nominal == getCollectionDifferenceDecl() ||
          (nominal->getDeclContext()->getAsDecl() ==
            getCollectionDifferenceDecl() &&
            nominal->getBaseName() == Id_Change) ||
          nominal == getDictionaryDecl() ||
          nominal == getSetDecl() ||
          nominal == getStringDecl() ||
          nominal == getSubstringDecl() ||
          nominal == getErrorDecl() ||
          nominal == getAnyHashableDecl() ||
          // Foundation's overlay depends on the CoreGraphics overlay, but
          // CoreGraphics value types bridge to Foundation objects such as
          // NSValue and NSNumber, so to avoid circular dependencies, the
          // bridging implementations of CG types appear in the Foundation
          // module.
          nominal->getParentModule()->getName() == Id_CoreGraphics ||
          nominal->getParentModule()->getName() == Id_CoreFoundation ||
          // CoreMedia is a dependency of AVFoundation, but the bridged
          // NSValue implementations for CMTime, CMTimeRange, and
          // CMTimeMapping are provided by AVFoundation, and AVFoundation
          // gets upset if you don't use the NSValue subclasses its factory
          // methods instantiate.
          nominal->getParentModule()->getName() == Id_CoreMedia);
}

bool ASTContext::isObjCClassWithMultipleSwiftBridgedTypes(Type t) {
  auto clas = t->getClassOrBoundGenericClass();
  if (!clas)
    return false;
  
  if (clas == getNSErrorDecl())
    return true;
  if (clas == getNSNumberDecl())
    return true;
  if (clas == getNSValueDecl())
    return true;
  
  return false;
}

Type ASTContext::getBridgedToObjC(const DeclContext *dc, Type type,
                                  Type *bridgedValueType) const {
  if (type->isBridgeableObjectType()) {
    if (bridgedValueType) *bridgedValueType = type;

    return type;
  }

  if (auto metaTy = type->getAs<MetatypeType>())
    if (metaTy->getInstanceType()->mayHaveSuperclass())
      return type;

  if (auto existentialMetaTy = type->getAs<ExistentialMetatypeType>())
    if (existentialMetaTy->getInstanceType()->isObjCExistentialType())
      return type;

  // Check whether the type is an existential that contains
  // Error. If so, it's bridged to NSError.
  if (type->isExistentialWithError()) {
    if (auto nsErrorTy = getNSErrorType()) {
      // The corresponding value type is Error.
      if (bridgedValueType)
        *bridgedValueType = getErrorExistentialType();

      return nsErrorTy;
    }
  }

  // Try to find a conformance that will enable bridging.
  auto findConformance =
      [&](KnownProtocolKind known) -> ProtocolConformanceRef {
    // Don't ascribe any behavior to Optional other than what we explicitly
    // give it. We don't want things like AnyObject?? to work.
    if (type->isOptional())
      return ProtocolConformanceRef::forInvalid();

    // Find the protocol.
    auto proto = getProtocol(known);
    if (!proto)
      return ProtocolConformanceRef::forInvalid();

    return dc->getParentModule()->lookupConformance(type, proto);
  };

  // Do we conform to _ObjectiveCBridgeable?
  if (auto conformance =
          findConformance(KnownProtocolKind::ObjectiveCBridgeable)) {
    // The corresponding value type is... the type.
    if (bridgedValueType)
      *bridgedValueType = type;

    // Find the Objective-C class type we bridge to.
    return conformance.getTypeWitnessByName(type, Id_ObjectiveCType);
  }

  // Do we conform to Error?
  if (findConformance(KnownProtocolKind::Error)) {
    // The corresponding value type is Error.
    if (bridgedValueType)
      *bridgedValueType = getErrorExistentialType();

    // Bridge to NSError.
    if (auto nsErrorTy = getNSErrorType())
      return nsErrorTy;
  }

  // No special bridging to Objective-C, but this can become an 'Any'.
  return Type();
}

ClangTypeConverter &ASTContext::getClangTypeConverter() {
  auto &impl = getImpl();
  if (!impl.Converter) {
    auto *cml = getClangModuleLoader();
    impl.Converter.emplace(*this, cml->getClangASTContext(), LangOpts.Target);
  }
  return impl.Converter.getValue();
}

const clang::Type *
ASTContext::getClangFunctionType(ArrayRef<AnyFunctionType::Param> params,
                                 Type resultTy,
                                 FunctionTypeRepresentation trueRep) {
  return getClangTypeConverter().getFunctionType(params, resultTy, trueRep);
}

const clang::Type *
ASTContext::getCanonicalClangFunctionType(
    ArrayRef<SILParameterInfo> params,
    Optional<SILResultInfo> result,
    SILFunctionType::Representation trueRep) {
  auto *ty = getClangTypeConverter().getFunctionType(params, result, trueRep);
  return ty ? ty->getCanonicalTypeInternal().getTypePtr() : nullptr;
}

std::unique_ptr<TemplateInstantiationError>
ASTContext::getClangTemplateArguments(
    const clang::TemplateParameterList *templateParams,
    ArrayRef<Type> genericArgs,
    SmallVectorImpl<clang::TemplateArgument> &templateArgs) {
  auto &impl = getImpl();
  if (!impl.Converter) {
    auto *cml = getClangModuleLoader();
    impl.Converter.emplace(*this, cml->getClangASTContext(), LangOpts.Target);
  }

  return impl.Converter->getClangTemplateArguments(templateParams, genericArgs,
                                                   templateArgs);
}

const Decl *
ASTContext::getSwiftDeclForExportedClangDecl(const clang::Decl *decl) {
  auto &impl = getImpl();

  // If we haven't exported anything yet, this must not be how we found
  // this declaration.
  if (!impl.Converter) return nullptr;

  return impl.Converter->getSwiftDeclForExportedClangDecl(decl);
}

const clang::Type *
ASTContext::getClangTypeForIRGen(Type ty) {
  return getClangTypeConverter().convert(ty).getTypePtrOrNull();
}

CanGenericSignature ASTContext::getSingleGenericParameterSignature() const {
  if (auto theSig = getImpl().SingleGenericParameterSignature)
    return theSig;

  auto param = GenericTypeParamType::get(/*type sequence*/ false,
                                         /*depth*/ 0, /*index*/ 0, *this);
  auto sig = GenericSignature::get(param, { });
  auto canonicalSig = CanGenericSignature(sig);
  getImpl().SingleGenericParameterSignature = canonicalSig;
  return canonicalSig;
}

// Return the signature for an opened existential. The opened archetype may have
// a different set of conformances from the corresponding existential. The
// opened archetype conformances are dictated by the ABI for generic arguments,
// while the existential value conformances are dictated by their layout (see
// Type::getExistentialLayout()). In particular, the opened archetype signature
// does not have requirements for conformances inherited from superclass
// constraints while existential values do.
CanGenericSignature ASTContext::getOpenedArchetypeSignature(Type type) {
  assert(type->isExistentialType());
  if (auto existential = type->getAs<ExistentialType>())
    type = existential->getConstraintType();

  const CanType constraint = type->getCanonicalType();

  // The opened archetype signature for a protocol type is identical
  // to the protocol's own canonical generic signature.
  if (const auto protoTy = dyn_cast<ProtocolType>(constraint)) {
    return protoTy->getDecl()->getGenericSignature().getCanonicalSignature();
  }

  auto found = getImpl().ExistentialSignatures.find(constraint);
  if (found != getImpl().ExistentialSignatures.end())
    return found->second;

  auto genericParam =
      GenericTypeParamType::get(/*type sequence*/ false,
                                /*depth*/ 0, /*index*/ 0, *this);
  Requirement requirement(RequirementKind::Conformance, genericParam,
                          constraint);
  auto genericSig = buildGenericSignature(*this,
                                          GenericSignature(),
                                          {genericParam},
                                          {requirement});

  CanGenericSignature canGenericSig(genericSig);

  auto result = getImpl().ExistentialSignatures.insert(
    std::make_pair(constraint, canGenericSig));
  assert(result.second);
  (void) result;

  return canGenericSig;
}

GenericSignature 
ASTContext::getOverrideGenericSignature(const ValueDecl *base,
                                        const ValueDecl *derived) {
  assert(isa<AbstractFunctionDecl>(base) || isa<SubscriptDecl>(base));
  assert(isa<AbstractFunctionDecl>(derived) || isa<SubscriptDecl>(derived));

  const auto baseClass = base->getDeclContext()->getSelfClassDecl();
  const auto derivedClass = derived->getDeclContext()->getSelfClassDecl();

  assert(baseClass != nullptr);
  assert(derivedClass != nullptr);

  const auto baseGenericSig =
      base->getAsGenericContext()->getGenericSignature();
  const auto derivedGenericSig =
      derived->getAsGenericContext()->getGenericSignature();

  if (base == derived)
    return derivedGenericSig;

  const auto derivedSuperclass = derivedClass->getSuperclass();
  if (derivedSuperclass.isNull())
    return nullptr;

  if (derivedGenericSig.isNull())
    return nullptr;

  if (baseGenericSig.isNull())
    return derivedGenericSig;

  auto key = OverrideSignatureKey(baseGenericSig,
                                  derivedGenericSig,
                                  derivedClass);

  if (getImpl().overrideSigCache.find(key) !=
      getImpl().overrideSigCache.end()) {
    return getImpl().overrideSigCache.lookup(key);
  }

  const auto derivedClassSig = derivedClass->getGenericSignature();

  unsigned derivedDepth = 0;
  unsigned baseDepth = 0;
  if (derivedClassSig)
    derivedDepth = derivedClassSig.getGenericParams().back()->getDepth() + 1;
  if (const auto baseClassSig = baseClass->getGenericSignature())
    baseDepth = baseClassSig.getGenericParams().back()->getDepth() + 1;

  SmallVector<GenericTypeParamType *, 2> addedGenericParams;
  if (const auto *gpList = derived->getAsGenericContext()->getGenericParams()) {
    for (auto gp : *gpList) {
      addedGenericParams.push_back(
          gp->getDeclaredInterfaceType()->castTo<GenericTypeParamType>());
    }
  }

  const auto subMap = derivedSuperclass->getContextSubstitutionMap(
      derivedClass->getModuleContext(), baseClass);

  auto substFn = [&](SubstitutableType *type) -> Type {
    auto *gp = cast<GenericTypeParamType>(type);

    if (gp->getDepth() < baseDepth) {
      return Type(gp).subst(subMap);
    }

    return CanGenericTypeParamType::get(
        gp->isTypeSequence(), gp->getDepth() - baseDepth + derivedDepth,
        gp->getIndex(), *this);
  };

  auto lookupConformanceFn =
      [&](CanType depTy, Type substTy,
          ProtocolDecl *proto) -> ProtocolConformanceRef {
    if (auto conf = subMap.lookupConformance(depTy, proto))
      return conf;

    return ProtocolConformanceRef(proto);
  };

  SmallVector<Requirement, 2> addedRequirements;
  for (auto reqt : baseGenericSig.getRequirements()) {
    if (auto substReqt = reqt.subst(substFn, lookupConformanceFn)) {
      addedRequirements.push_back(*substReqt);
    }
  }

  auto genericSig = buildGenericSignature(*this, derivedClassSig,
                                          std::move(addedGenericParams),
                                          std::move(addedRequirements));
  getImpl().overrideSigCache.insert(std::make_pair(key, genericSig));
  return genericSig;
}

bool ASTContext::overrideGenericSignatureReqsSatisfied(
    const ValueDecl *base, const ValueDecl *derived,
    const OverrideGenericSignatureReqCheck direction) {
  auto *baseCtx = base->getAsGenericContext();
  auto *derivedCtx = derived->getAsGenericContext();

  if (baseCtx->isGeneric() != derivedCtx->isGeneric())
    return false;

  if (baseCtx->isGeneric() &&
      (baseCtx->getGenericParams()->size() !=
       derivedCtx->getGenericParams()->size()))
    return false;

  auto sig = getOverrideGenericSignature(base, derived);
  if (!sig)
    return true;

  auto derivedSig = derivedCtx->getGenericSignature();

  switch (direction) {
  case OverrideGenericSignatureReqCheck::BaseReqSatisfiedByDerived:
    return sig.requirementsNotSatisfiedBy(derivedSig).empty();
  case OverrideGenericSignatureReqCheck::DerivedReqSatisfiedByBase:
    return derivedSig.requirementsNotSatisfiedBy(sig).empty();
  }
  llvm_unreachable("Unhandled OverrideGenericSignatureReqCheck in switch");
}

void ASTContext::registerIRGenSILTransforms(SILTransformCtors ctors) {
  assert(getImpl().IRGenSILPasses.empty() && "Already registered");
  getImpl().IRGenSILPasses = ctors;
}

ASTContext::SILTransformCtors ASTContext::getIRGenSILTransforms() const {
  auto passes = getImpl().IRGenSILPasses;
  assert(!passes.empty() && "Didn't register the necessary passes");
  return passes;
}

std::string ASTContext::getEntryPointFunctionName() const {
  return LangOpts.entryPointFunctionName;
}

SILLayout *SILLayout::get(ASTContext &C,
                          CanGenericSignature Generics,
                          ArrayRef<SILField> Fields) {
  // Profile the layout parameters.
  llvm::FoldingSetNodeID id;
  Profile(id, Generics, Fields);
  
  // Return an existing layout if there is one.
  void *insertPos;
  auto &Layouts = C.getImpl().SILLayouts;
  
  if (auto existing = Layouts.FindNodeOrInsertPos(id, insertPos))
    return existing;
  
  // Allocate a new layout.
  void *memory = C.Allocate(totalSizeToAlloc<SILField>(Fields.size()),
                            alignof(SILLayout));
  
  auto newLayout = ::new (memory) SILLayout(Generics, Fields);
  Layouts.InsertNode(newLayout, insertPos);
  return newLayout;
}

CanSILBoxType SILBoxType::get(ASTContext &C,
                              SILLayout *Layout,
                              SubstitutionMap Substitutions) {
  // Canonicalize substitutions.
  Substitutions = Substitutions.getCanonical();

  // Return an existing layout if there is one.
  void *insertPos;
  auto &SILBoxTypes = C.getImpl().SILBoxTypes;
  llvm::FoldingSetNodeID id;
  Profile(id, Layout, Substitutions);
  if (auto existing = SILBoxTypes.FindNodeOrInsertPos(id, insertPos))
    return CanSILBoxType(existing);

  auto newBox = new (C, AllocationArena::Permanent) SILBoxType(C, Layout,
                                                               Substitutions);
  SILBoxTypes.InsertNode(newBox, insertPos);
  return CanSILBoxType(newBox);
}

/// TODO: Transitional factory to present the single-type SILBoxType::get
/// interface.
CanSILBoxType SILBoxType::get(CanType boxedType) {
  auto &ctx = boxedType->getASTContext();
  auto singleGenericParamSignature = ctx.getSingleGenericParameterSignature();
  auto genericParam = singleGenericParamSignature.getGenericParams()[0];
  auto layout = SILLayout::get(ctx, singleGenericParamSignature,
                               SILField(CanType(genericParam),
                                        /*mutable*/ true));

  auto subMap =
    SubstitutionMap::get(
      singleGenericParamSignature,
      [&](SubstitutableType *type) -> Type {
        if (type->isEqual(genericParam)) return boxedType;

        return nullptr;
      },
      MakeAbstractConformanceForGenericType());
  return get(boxedType->getASTContext(), layout, subMap);
}

LayoutConstraint
LayoutConstraint::getLayoutConstraint(LayoutConstraintKind Kind,
                                      ASTContext &C) {
  return getLayoutConstraint(Kind, 0, 0, C);
}

LayoutConstraint LayoutConstraint::getLayoutConstraint(LayoutConstraintKind Kind,
                                                      unsigned SizeInBits,
                                                      unsigned Alignment,
                                                      ASTContext &C) {
  if (!LayoutConstraintInfo::isKnownSizeTrivial(Kind)) {
    assert(SizeInBits == 0);
    assert(Alignment == 0);
    return getLayoutConstraint(Kind);
  }

  // Check to see if we've already seen this tuple before.
  llvm::FoldingSetNodeID ID;
  LayoutConstraintInfo::Profile(ID, Kind, SizeInBits, Alignment);

  void *InsertPos = nullptr;
  if (LayoutConstraintInfo *Layout =
          C.getImpl().getArena(AllocationArena::Permanent)
              .LayoutConstraints.FindNodeOrInsertPos(ID, InsertPos))
    return LayoutConstraint(Layout);

  LayoutConstraintInfo *New =
      LayoutConstraintInfo::isTrivial(Kind)
          ? new (C, AllocationArena::Permanent)
                LayoutConstraintInfo(Kind, SizeInBits, Alignment)
          : new (C, AllocationArena::Permanent) LayoutConstraintInfo(Kind);
  C.getImpl().getArena(AllocationArena::Permanent)
      .LayoutConstraints.InsertNode(New, InsertPos);
  return LayoutConstraint(New);
}

Type &ASTContext::getDefaultTypeRequestCache(SourceFile *SF,
                                             KnownProtocolKind kind) {
  return getImpl().DefaultTypeRequestCaches[SF][size_t(kind)];
}

Type ASTContext::getSideCachedPropertyWrapperBackingPropertyType(
    VarDecl *var) const {
  return getImpl().PropertyWrapperBackingVarTypes[var];
}

void ASTContext::setSideCachedPropertyWrapperBackingPropertyType(
    VarDecl *var, Type type) {
  assert(!getImpl().PropertyWrapperBackingVarTypes[var] ||
         getImpl().PropertyWrapperBackingVarTypes[var]->isEqual(type));
  getImpl().PropertyWrapperBackingVarTypes[var] = type;
}

VarDecl *VarDecl::getOriginalWrappedProperty(
    Optional<PropertyWrapperSynthesizedPropertyKind> kind) const {
  if (!Bits.VarDecl.IsPropertyWrapperBackingProperty)
    return nullptr;

  ASTContext &ctx = getASTContext();
  assert(ctx.getImpl().OriginalWrappedProperties.count(this) > 0);
  auto original = ctx.getImpl().OriginalWrappedProperties[this];
  if (!kind)
    return original;

  auto wrapperInfo = original->getPropertyWrapperAuxiliaryVariables();
  switch (*kind) {
  case PropertyWrapperSynthesizedPropertyKind::Backing:
    return this == wrapperInfo.backingVar ? original : nullptr;

  case PropertyWrapperSynthesizedPropertyKind::Projection:
    return this == wrapperInfo.projectionVar ? original : nullptr;
  }
  llvm_unreachable("covered switch");
}

void VarDecl::setOriginalWrappedProperty(VarDecl *originalProperty) {
  Bits.VarDecl.IsPropertyWrapperBackingProperty = true;
  ASTContext &ctx = getASTContext();
  assert(ctx.getImpl().OriginalWrappedProperties.count(this) == 0);
  ctx.getImpl().OriginalWrappedProperties[this] = originalProperty;
}

#ifndef NDEBUG
static bool isSourceLocInOrignalBuffer(const Decl *D, SourceLoc Loc) {
  assert(Loc.isValid());
  auto bufferID = D->getDeclContext()->getParentSourceFile()->getBufferID();
  assert(bufferID.hasValue() && "Source buffer ID must be set");
  auto &SM = D->getASTContext().SourceMgr;
  return SM.getRangeForBuffer(*bufferID).contains(Loc);
}
#endif

void AbstractFunctionDecl::keepOriginalBodySourceRange() {
  auto &impl = getASTContext().getImpl();
  auto result =
      impl.OriginalBodySourceRanges.insert({this, getBodySourceRange()});
  assert((!result.second ||
          isSourceLocInOrignalBuffer(this, result.first->getSecond().Start)) &&
         "This function must be called before setting new body range");
  (void)result;
}

SourceRange AbstractFunctionDecl::getOriginalBodySourceRange() const {
  auto &impl = getASTContext().getImpl();
  auto found = impl.OriginalBodySourceRanges.find(this);
  if (found != impl.OriginalBodySourceRanges.end()) {
    return found->getSecond();
  } else {
    return getBodySourceRange();
  }
}

IndexSubset *
IndexSubset::get(ASTContext &ctx, const SmallBitVector &indices) {
  auto &foldingSet = ctx.getImpl().IndexSubsets;
  llvm::FoldingSetNodeID id;
  unsigned capacity = indices.size();
  id.AddInteger(capacity);
  for (unsigned index : indices.set_bits())
    id.AddInteger(index);
  void *insertPos = nullptr;
  auto *existing = foldingSet.FindNodeOrInsertPos(id, insertPos);
  if (existing)
    return existing;
  auto sizeToAlloc = sizeof(IndexSubset) +
      getNumBytesNeededForCapacity(capacity);
  auto *buf = reinterpret_cast<IndexSubset *>(
      ctx.Allocate(sizeToAlloc, alignof(IndexSubset)));
  auto *newNode = new (buf) IndexSubset(indices);
  foldingSet.InsertNode(newNode, insertPos);
  return newNode;
}

AutoDiffDerivativeFunctionIdentifier *AutoDiffDerivativeFunctionIdentifier::get(
    AutoDiffDerivativeFunctionKind kind, IndexSubset *parameterIndices,
    GenericSignature derivativeGenericSignature, ASTContext &C) {
  assert(parameterIndices);
  auto &foldingSet = C.getImpl().AutoDiffDerivativeFunctionIdentifiers;
  llvm::FoldingSetNodeID id;
  id.AddInteger((unsigned)kind);
  id.AddPointer(parameterIndices);
  auto derivativeCanGenSig = derivativeGenericSignature.getCanonicalSignature();
  id.AddPointer(derivativeCanGenSig.getPointer());

  void *insertPos;
  auto *existing = foldingSet.FindNodeOrInsertPos(id, insertPos);
  if (existing)
    return existing;

  void *mem = C.Allocate(sizeof(AutoDiffDerivativeFunctionIdentifier),
                         alignof(AutoDiffDerivativeFunctionIdentifier));
  auto *newNode = ::new (mem) AutoDiffDerivativeFunctionIdentifier(
      kind, parameterIndices, derivativeGenericSignature);
  foldingSet.InsertNode(newNode, insertPos);

  return newNode;
}

llvm::LLVMContext &ASTContext::getIntrinsicScratchContext() const {
#if !SWIFT_BUILD_ONLY_SYNTAXPARSERLIB
  return *getImpl().IntrinsicScratchContext.get();
#endif
}

bool ASTContext::isASCIIString(StringRef s) const {
  for (unsigned char c : s) {
    if (c > 127) {
      return false;
    }
  }
  return true;
}
