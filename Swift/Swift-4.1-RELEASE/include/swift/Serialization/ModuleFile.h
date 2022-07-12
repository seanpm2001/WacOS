//===--- ModuleFile.h - Info about a loaded serialized module ---*- C++ -*-===//
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

#ifndef SWIFT_SERIALIZATION_MODULEFILE_H
#define SWIFT_SERIALIZATION_MODULEFILE_H

#include "swift/AST/Identifier.h"
#include "swift/AST/LazyResolver.h"
#include "swift/AST/LinkLibrary.h"
#include "swift/AST/Module.h"
#include "swift/AST/RawComment.h"
#include "swift/AST/TypeLoc.h"
#include "swift/Serialization/ModuleFormat.h"
#include "swift/Serialization/Validation.h"
#include "swift/Basic/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Bitcode/BitstreamReader.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {
  class BitstreamCursor;
  class BitstreamReader;
  class MemoryBuffer;
  template <typename Info> class OnDiskIterableChainedHashTable;
}

namespace swift {
class Decl;
class FileUnit;
class ModuleDecl;
class Pattern;
class ProtocolConformance;

/// A serialized module, along with the tools to access it.
class ModuleFile
  : public LazyMemberLoader,
    public LazyConformanceLoader {
  friend class SerializedASTFile;
  friend class SILDeserializer;
  using Status = serialization::Status;

  /// A reference back to the AST representation of the file.
  FileUnit *FileContext = nullptr;

  /// The module shadowed by this module, if any.
  ModuleDecl *ShadowedModule = nullptr;

  /// The module file data.
  std::unique_ptr<llvm::MemoryBuffer> ModuleInputBuffer;
  std::unique_ptr<llvm::MemoryBuffer> ModuleDocInputBuffer;

  /// The cursor used to lazily load things from the file.
  llvm::BitstreamCursor DeclTypeCursor;

  llvm::BitstreamCursor SILCursor;
  llvm::BitstreamCursor SILIndexCursor;
  llvm::BitstreamCursor DeclMemberTablesCursor;

  /// The name of the module.
  StringRef Name;
  friend StringRef getNameOfModule(const ModuleFile *);

  /// The target the module was built for.
  StringRef TargetTriple;

  /// The Swift compatibility version in use when this module was built.
  version::Version CompatibilityVersion;

  /// The data blob containing all of the module's identifiers.
  StringRef IdentifierData;

  /// A callback to be invoked every time a type was deserialized.
  std::function<void(Type)> DeserializedTypeCallback;

  /// The number of entities that are currently being deserialized.
  unsigned NumCurrentDeserializingEntities = 0;

  /// RAII class to be used when deserializing an entity.
  class DeserializingEntityRAII {
    ModuleFile &MF;

  public:
    DeserializingEntityRAII(ModuleFile &mf)
        : MF(mf.getModuleFileForDelayedActions()) {
      ++MF.NumCurrentDeserializingEntities;
    }
    ~DeserializingEntityRAII() {
      assert(MF.NumCurrentDeserializingEntities > 0 &&
             "Imbalanced currently-deserializing count?");
      if (MF.NumCurrentDeserializingEntities == 1) {
        MF.finishPendingActions();
      }

      --MF.NumCurrentDeserializingEntities;
    }
  };
  friend class DeserializingEntityRAII;

  /// Picks a specific ModuleFile instance to serve as the "delayer" for the
  /// entire module.
  ///
  /// This is usually \c this, but when there are partial swiftmodules all
  /// loaded for the same module it may be different.
  ModuleFile &getModuleFileForDelayedActions();

  /// Finish any pending actions that were waiting for the topmost entity to
  /// be deserialized.
  void finishPendingActions();

public:
  /// Represents another module that has been imported as a dependency.
  class Dependency {
  public:
    ModuleDecl::ImportedModule Import = {};
    const StringRef RawPath;

  private:
    unsigned IsExported : 1;
    const unsigned IsHeader : 1;
    const unsigned IsScoped : 1;

    Dependency(StringRef path, bool isHeader, bool exported, bool isScoped)
      : RawPath(path), IsExported(exported), IsHeader(isHeader),
        IsScoped(isScoped) {}

  public:
    Dependency(StringRef path, bool exported, bool isScoped)
      : Dependency(path, false, exported, isScoped) {}

    static Dependency forHeader(StringRef headerPath, bool exported) {
      return Dependency(headerPath, true, exported, false);
    }

    bool isLoaded() const {
      return Import.second != nullptr;
    }

    bool isExported() const { return IsExported; }
    bool isHeader() const { return IsHeader; }
    bool isScoped() const { return IsScoped; }

    void forceExported() { IsExported = true; }

    std::string getPrettyPrintedPath() const;
  };

private:
  /// All modules this module depends on.
  SmallVector<Dependency, 8> Dependencies;

  struct SearchPath {
    StringRef Path;
    bool IsFramework;
    bool IsSystem;
  };
  /// Search paths this module may provide.
  ///
  /// This is not intended for use by frameworks, but may show up in debug
  /// modules.
  std::vector<SearchPath> SearchPaths;

  /// Info for the (lone) imported header for this module.
  struct {
    off_t fileSize;
    time_t fileModTime;
    StringRef contents;
  } importedHeaderInfo = {};

  /// All of this module's link-time dependencies.
  SmallVector<LinkLibrary, 8> LinkLibraries;

public:
  template <typename T>
  class Serialized {
  private:
    using RawBitOffset = uint64_t;

    using ImplTy = PointerUnion<T, serialization::BitOffset>;
    ImplTy Value;

  public:
    /*implicit*/ Serialized(serialization::BitOffset offset) : Value(offset) {}

    bool isComplete() const {
      return Value.template is<T>();
    }

    T get() const {
      return Value.template get<T>();
    }

    /*implicit*/ operator T() const {
      return get();
    }

    /*implicit*/ operator serialization::BitOffset() const {
      return Value.template get<serialization::BitOffset>();
    }

    /*implicit*/ operator RawBitOffset() const {
      return Value.template get<serialization::BitOffset>();
    }

    template <typename Derived>
    Serialized &operator=(Derived deserialized) {
      assert(!isComplete() || ImplTy(deserialized) == Value);
      Value = deserialized;
      return *this;
    }

    void unsafeOverwrite(T t) {
      Value = t;
    }
  };

  /// A class for holding a value that can be partially deserialized.
  ///
  /// This class assumes that "T()" is not a valid deserialized value.
  template <typename T>
  class PartiallySerialized {
  private:
    using RawBitOffset = decltype(DeclTypeCursor.GetCurrentBitNo());

    /// The deserialized value.
    T Value;

    /// The offset.
    serialization::BitOffset Offset;

    unsigned IsFullyDeserialized : 1;

  public:
    /*implicit*/ PartiallySerialized(serialization::BitOffset offset)
      : Value(), Offset(offset), IsFullyDeserialized(0) {}

    /*implicit*/ PartiallySerialized(RawBitOffset offset)
      : Value(), Offset(static_cast<unsigned>(offset)), IsFullyDeserialized(0) {
      assert(Offset == offset && "offset is too large");
    }

    bool isDeserialized() const {
      return Value != T();
    }

    bool isFullyDeserialized() const {
      return isDeserialized() && IsFullyDeserialized;
    }

    serialization::BitOffset getOffset() const {
      assert(!isFullyDeserialized());
      return Offset;
    }

    T get() const {
      assert(isDeserialized());
      return Value;
    }

    void reset() {
      IsFullyDeserialized = 0;
      Value = T();
    }

    void set(T value, bool isFullyDeserialized) {
      assert(!isDeserialized() || Value == value);
      Value = value;
      IsFullyDeserialized = isFullyDeserialized;
    }
  };

private:
  /// Decls referenced by this module.
  std::vector<Serialized<Decl*>> Decls;

  /// DeclContexts referenced by this module.
  std::vector<Serialized<DeclContext*>> DeclContexts;

  /// Local DeclContexts referenced by this module.
  std::vector<Serialized<DeclContext*>> LocalDeclContexts;

  /// Normal protocol conformances referenced by this module.
  std::vector<Serialized<NormalProtocolConformance *>> NormalConformances;

  /// SILLayouts referenced by this module.
  std::vector<Serialized<SILLayout *>> SILLayouts;

  /// Types referenced by this module.
  std::vector<Serialized<Type>> Types;

  /// Generic signatures referenced by this module.
  std::vector<Serialized<GenericSignature *>> GenericSignatures;

  /// Generic environments referenced by this module.
  std::vector<Serialized<GenericEnvironment *>> GenericEnvironments;

  /// Represents an identifier that may or may not have been deserialized yet.
  ///
  /// If \c Offset is non-zero, the identifier has not been loaded yet.
  class SerializedIdentifier {
  public:
    Identifier Ident;
    serialization::BitOffset Offset;

    template <typename IntTy>
    /*implicit*/ SerializedIdentifier(IntTy rawOffset)
      : Offset(static_cast<unsigned>(rawOffset)) {
      assert(Offset == rawOffset && "not enough bits");
    }
  };

  /// Identifiers referenced by this module.
  std::vector<SerializedIdentifier> Identifiers;

  class DeclTableInfo;
  using SerializedDeclTable =
      llvm::OnDiskIterableChainedHashTable<DeclTableInfo>;

  class ExtensionTableInfo;
  using SerializedExtensionTable =
      llvm::OnDiskIterableChainedHashTable<ExtensionTableInfo>;

  class LocalDeclTableInfo;
  using SerializedLocalDeclTable =
      llvm::OnDiskIterableChainedHashTable<LocalDeclTableInfo>;

  class NestedTypeDeclsTableInfo;
  using SerializedNestedTypeDeclsTable =
      llvm::OnDiskIterableChainedHashTable<NestedTypeDeclsTableInfo>;

  class DeclMemberNamesTableInfo;
  using SerializedDeclMemberNamesTable =
      llvm::OnDiskIterableChainedHashTable<DeclMemberNamesTableInfo>;

  class DeclMembersTableInfo;
  using SerializedDeclMembersTable =
      llvm::OnDiskIterableChainedHashTable<DeclMembersTableInfo>;

  std::unique_ptr<SerializedDeclTable> TopLevelDecls;
  std::unique_ptr<SerializedDeclTable> OperatorDecls;
  std::unique_ptr<SerializedDeclTable> PrecedenceGroupDecls;
  std::unique_ptr<SerializedDeclTable> ClassMembersForDynamicLookup;
  std::unique_ptr<SerializedDeclTable> OperatorMethodDecls;
  std::unique_ptr<SerializedExtensionTable> ExtensionDecls;
  std::unique_ptr<SerializedLocalDeclTable> LocalTypeDecls;
  std::unique_ptr<SerializedNestedTypeDeclsTable> NestedTypeDecls;
  std::unique_ptr<SerializedDeclMemberNamesTable> DeclMemberNames;

  llvm::DenseMap<uint32_t,
           std::unique_ptr<SerializedDeclMembersTable>> DeclMembersTables;

  class ObjCMethodTableInfo;
  using SerializedObjCMethodTable =
    llvm::OnDiskIterableChainedHashTable<ObjCMethodTableInfo>;

  std::unique_ptr<SerializedObjCMethodTable> ObjCMethods;

  llvm::DenseMap<const ValueDecl *, Identifier> PrivateDiscriminatorsByValue;

  TinyPtrVector<Decl *> ImportDecls;

  using DeclIDVector = SmallVector<serialization::DeclID, 4>;

  DeclIDVector EagerDeserializationDecls;

  class DeclCommentTableInfo;
  using SerializedDeclCommentTable =
      llvm::OnDiskIterableChainedHashTable<DeclCommentTableInfo>;

  using GroupNameTable = llvm::DenseMap<unsigned, StringRef>;

  std::unique_ptr<GroupNameTable> GroupNamesMap;
  std::unique_ptr<SerializedDeclCommentTable> DeclCommentTable;

  struct ModuleBits {
    /// The decl ID of the main class in this module file, if it has one.
    unsigned EntryPointDeclID : 31;

    /// Whether or not this module file comes from a context that had a main
    /// entry point.
    unsigned HasEntryPoint : 1;

    /// Whether this module file comes from a framework.
    unsigned IsFramework : 1;

    /// THIS SETTING IS OBSOLETE BUT IS STILL USED BY OLDER MODULES.
    ///
    /// Whether this module has a shadowed module that's part of its public
    /// interface.
    unsigned HasUnderlyingModule : 1;

    /// Whether or not ImportDecls is valid.
    unsigned ComputedImportDecls : 1;

    /// Whether this module file can be used, and what's wrong if not.
    unsigned Status : 4;

    // Explicitly pad out to the next word boundary.
    unsigned : 0;
  } Bits = {};
  static_assert(sizeof(ModuleBits) <= 8, "The bit set should be small");

  void setStatus(Status status) {
    Bits.Status = static_cast<unsigned>(status);
    assert(status == getStatus() && "not enough bits for status");
  }

  void setEntryPointClassID(serialization::DeclID DID) {
    Bits.HasEntryPoint = true;
    Bits.EntryPointDeclID = DID;
    assert(Bits.EntryPointDeclID == DID && "not enough bits for DeclID");
  }

  /// Creates a new AST node to represent a deserialized decl.
  template <typename T, typename ...Args>
  T *createDecl(Args &&... args);

  /// Constructs a new module and validates it.
  ModuleFile(std::unique_ptr<llvm::MemoryBuffer> moduleInputBuffer,
             std::unique_ptr<llvm::MemoryBuffer> moduleDocInputBuffer,
             bool isFramework, serialization::ValidationInfo &info,
             serialization::ExtendedValidationInfo *extInfo);

public:
  /// Change the status of the current module. Default argument marks the module
  /// as being malformed.
  Status error(Status issue = Status::Malformed) {
    assert(issue != Status::Valid);
    if (FileContext && issue == Status::Malformed) {
      // This would normally be an assertion but it's more useful to print the
      // PrettyStackTrace here even in no-asserts builds. Malformed modules are
      // generally unrecoverable.
      fatal(llvm::make_error<llvm::StringError>(
          "(see \"While...\" info below)", llvm::inconvertibleErrorCode()));
    }
    setStatus(issue);
    return getStatus();
  }

  /// Emits one last diagnostic, logs the error, and then aborts for the stack
  /// trace.
  LLVM_ATTRIBUTE_NORETURN void fatal(llvm::Error error);

  ASTContext &getContext() const {
    assert(FileContext && "no associated context yet");
    return FileContext->getParentModule()->getASTContext();
  }

  ModuleDecl *getAssociatedModule() const {
    assert(FileContext && "no associated context yet");
    return FileContext->getParentModule();
  }

  FileUnit *getFile() const {
    assert(FileContext && "no associated context yet");
    return FileContext;
  }

private:
  /// Read an on-disk decl hash table stored in index_block::DeclListLayout
  /// format.
  std::unique_ptr<SerializedDeclTable>
  readDeclTable(ArrayRef<uint64_t> fields, StringRef blobData);

  /// Read an on-disk local decl hash table stored in
  /// index_block::DeclListLayout format.
  std::unique_ptr<SerializedLocalDeclTable>
  readLocalDeclTable(ArrayRef<uint64_t> fields, StringRef blobData);

  /// Read an on-disk Objective-C method table stored in
  /// index_block::ObjCMethodTableLayout format.
  std::unique_ptr<ModuleFile::SerializedObjCMethodTable>
  readObjCMethodTable(ArrayRef<uint64_t> fields, StringRef blobData);

  /// Read an on-disk local decl hash table stored in
  /// index_block::ExtensionTableLayout format.
  std::unique_ptr<SerializedExtensionTable>
  readExtensionTable(ArrayRef<uint64_t> fields, StringRef blobData);

  /// Read an on-disk local decl hash table stored in
  /// index_block::NestedTypeDeclsLayout format.
  std::unique_ptr<SerializedNestedTypeDeclsTable>
  readNestedTypeDeclsTable(ArrayRef<uint64_t> fields, StringRef blobData);

  /// Read an on-disk local decl-name hash table stored in
  /// index_block::DeclMemberNamesLayout format.
  std::unique_ptr<SerializedDeclMemberNamesTable>
  readDeclMemberNamesTable(ArrayRef<uint64_t> fields, StringRef blobData);

  /// Read an on-disk local decl-members hash table stored in
  /// index_block::DeclMembersLayout format.
  std::unique_ptr<SerializedDeclMembersTable>
  readDeclMembersTable(ArrayRef<uint64_t> fields, StringRef blobData);

  /// Main logic of getDeclChecked.
  llvm::Expected<Decl *>
  getDeclCheckedImpl(serialization::DeclID DID,
                     Optional<DeclContext *> ForcedContext = None);

  /// Reads the index block, which contains global tables.
  ///
  /// Returns false if there was an error.
  bool readIndexBlock(llvm::BitstreamCursor &cursor);

  /// Read an on-disk decl hash table stored in
  /// \c comment_block::DeclCommentListLayout format.
  std::unique_ptr<SerializedDeclCommentTable>
  readDeclCommentTable(ArrayRef<uint64_t> fields, StringRef blobData);

  std::unique_ptr<GroupNameTable>
  readGroupTable(ArrayRef<uint64_t> fields, StringRef blobData);

  /// Reads the comment block, which contains USR to comment mappings.
  ///
  /// Returns false if there was an error.
  bool readCommentBlock(llvm::BitstreamCursor &cursor);

  /// Recursively reads a pattern from \c DeclTypeCursor.
  llvm::Expected<Pattern *> readPattern(DeclContext *owningDC);

  ParameterList *readParameterList();
  
  /// Reads a generic param list from \c DeclTypeCursor.
  ///
  /// If the record at the cursor is not a generic param list, returns null
  /// without moving the cursor.
  GenericParamList *maybeReadGenericParams(DeclContext *DC,
                                     GenericParamList *outerParams = nullptr);

  /// Reads a set of requirements from \c DeclTypeCursor.
  void readGenericRequirements(SmallVectorImpl<Requirement> &requirements,
                               llvm::BitstreamCursor &Cursor);

  /// Set up a (potentially lazy) generic environment for the given type,
  /// function or extension.
  void configureGenericEnvironment(
                   GenericContext *genericDecl,
                   serialization::GenericEnvironmentID envID);

  /// Populates the protocol's default witness table.
  ///
  /// Returns true if there is an error.
  ///
  /// Note: this destroys the cursor's position in the stream. Furthermore,
  /// because it reads from the cursor, it is not possible to reset the cursor
  /// after reading. Nothing should ever follow a DEFAULT_WITNESS_TABLE record.
  bool readDefaultWitnessTable(ProtocolDecl *proto);

  /// Resolves a cross-reference, starting from the given module.
  ///
  /// Note: this destroys the cursor's position in the stream. Furthermore,
  /// because it reads from the cursor, it is not possible to reset the cursor
  /// after reading. Nothing should ever follow an XREF record except
  /// XREF_PATH_PIECE records.
  llvm::Expected<Decl *> resolveCrossReference(ModuleDecl *M, uint32_t pathLen);

  /// Populates TopLevelIDs for name lookup.
  void buildTopLevelDeclMap();

  /// Sets the accessors for \p storage based on \p rawStorageKind.
  void configureStorage(AbstractStorageDecl *storage,
                        unsigned rawStorageKind,
                        serialization::DeclID getter,
                        serialization::DeclID setter,
                        serialization::DeclID materializeForSet,
                        serialization::DeclID addressor,
                        serialization::DeclID mutableAddressor,
                        serialization::DeclID willSet,
                        serialization::DeclID didSet);

public:
  /// Loads a module from the given memory buffer.
  ///
  /// \param moduleInputBuffer A memory buffer containing the serialized module
  /// data. The created ModuleFile takes ownership of the buffer, even if
  /// there's an error in loading.
  /// \param moduleDocInputBuffer An optional memory buffer containing
  /// documentation data for the module. The created ModuleFile takes ownership
  /// of the buffer, even if there's an error in loading.
  /// \param isFramework If true, this is treated as a framework module for
  /// linking purposes.
  /// \param[out] theModule The loaded module.
  /// \param[out] extInfo Optionally, extra info serialized about the module.
  /// \returns Whether the module was successfully loaded, or what went wrong
  ///          if it was not.
  static serialization::ValidationInfo
  load(std::unique_ptr<llvm::MemoryBuffer> moduleInputBuffer,
       std::unique_ptr<llvm::MemoryBuffer> moduleDocInputBuffer,
       bool isFramework, std::unique_ptr<ModuleFile> &theModule,
       serialization::ExtendedValidationInfo *extInfo = nullptr) {
    serialization::ValidationInfo info;
    theModule.reset(new ModuleFile(std::move(moduleInputBuffer),
                                   std::move(moduleDocInputBuffer),
                                   isFramework, info, extInfo));
    return info;
  }

  // Out of line to avoid instantiation OnDiskChainedHashTable here.
  ~ModuleFile();

  /// Associates this module file with an AST module.
  ///
  /// Returns any error that occurred during association, including validation
  /// that the module file is compatible with the module it's being loaded as.
  Status associateWithFileContext(FileUnit *file, SourceLoc diagLoc);

  /// Checks whether this module can be used.
  Status getStatus() const {
    return static_cast<Status>(Bits.Status);
  }

  /// Returns the list of modules this module depends on.
  ArrayRef<Dependency> getDependencies() const {
    return Dependencies;
  }

  /// The module shadowed by this module, if any.
  ModuleDecl *getShadowedModule() const { return ShadowedModule; }

  /// Searches the module's top-level decls for the given identifier.
  void lookupValue(DeclName name, SmallVectorImpl<ValueDecl*> &results);

  /// Searches the module's local type decls for the given mangled name.
  TypeDecl *lookupLocalType(StringRef MangledName);

  /// Searches the module's nested type decls table for the given member of
  /// the given type.
  TypeDecl *lookupNestedType(Identifier name, const NominalTypeDecl *parent);

  /// Searches the module's operators for one with the given name and fixity.
  ///
  /// If none is found, returns null.
  OperatorDecl *lookupOperator(Identifier name, DeclKind fixity);

  /// Searches the module's precedence groups for one with the given
  /// name and fixity.
  ///
  /// If none is found, returns null.
  PrecedenceGroupDecl *lookupPrecedenceGroup(Identifier name);

  /// Adds any imported modules to the given vector.
  void getImportedModules(SmallVectorImpl<ModuleDecl::ImportedModule> &results,
                          ModuleDecl::ImportFilter filter);

  void getImportDecls(SmallVectorImpl<Decl *> &Results);

  /// Reports all visible top-level members in this module.
  void lookupVisibleDecls(ModuleDecl::AccessPathTy accessPath,
                          VisibleDeclConsumer &consumer,
                          NLKind lookupKind);

  /// Loads extensions for the given decl.
  ///
  /// Note that this may cause other decls to load as well.
  void loadExtensions(NominalTypeDecl *nominal);

  /// \brief Load the methods within the given class that produce
  /// Objective-C class or instance methods with the given selector.
  ///
  /// \param classDecl The class in which we are searching for @objc methods.
  /// The search only considers this class and its extensions; not any
  /// superclasses.
  ///
  /// \param selector The selector to search for.
  ///
  /// \param isInstanceMethod Whether we are looking for an instance method
  /// (vs. a class method).
  ///
  /// \param methods The list of @objc methods in this class that have this
  /// selector and are instance/class methods as requested.
  void loadObjCMethods(ClassDecl *classDecl,
                       ObjCSelector selector,
                       bool isInstanceMethod,
                       llvm::TinyPtrVector<AbstractFunctionDecl *> &methods);

  /// Reports all class members in the module to the given consumer.
  ///
  /// This is intended for use with id-style lookup and code completion.
  void lookupClassMembers(ModuleDecl::AccessPathTy accessPath,
                          VisibleDeclConsumer &consumer);

  /// Adds class members in the module with the given name to the given vector.
  ///
  /// This is intended for use with id-style lookup.
  void lookupClassMember(ModuleDecl::AccessPathTy accessPath,
                         DeclName name,
                         SmallVectorImpl<ValueDecl*> &results);

  /// Find all Objective-C methods with the given selector.
  void lookupObjCMethods(
         ObjCSelector selector,
         SmallVectorImpl<AbstractFunctionDecl *> &results);

  /// Reports all link-time dependencies.
  void collectLinkLibraries(ModuleDecl::LinkLibraryCallback callback) const;

  /// Adds all top-level decls to the given vector.
  void getTopLevelDecls(SmallVectorImpl<Decl*> &Results);

  /// Adds all local type decls to the given vector.
  void getLocalTypeDecls(SmallVectorImpl<TypeDecl*> &Results);

  /// Adds all top-level decls to the given vector.
  ///
  /// This includes all decls that should be displayed to clients of the module.
  /// This can differ from \c getTopLevelDecls, e.g. it returns decls from a
  /// shadowed clang module.
  void getDisplayDecls(SmallVectorImpl<Decl*> &results);

  StringRef getModuleFilename() const {
    // FIXME: This seems fragile, maybe store the filename separately ?
    return ModuleInputBuffer->getBufferIdentifier();
  }

  /// AST-verify imported decls.
  ///
  /// Has no effect in NDEBUG builds.
  void verify() const;

  virtual void loadAllMembers(Decl *D,
                              uint64_t contextData) override;

  virtual
  Optional<TinyPtrVector<ValueDecl *>>
  loadNamedMembers(const IterableDeclContext *IDC, DeclBaseName N,
                   uint64_t contextData) override;

  virtual void
  loadAllConformances(const Decl *D, uint64_t contextData,
                    SmallVectorImpl<ProtocolConformance*> &Conforms) override;

  virtual TypeLoc loadAssociatedTypeDefault(const AssociatedTypeDecl *ATD,
                                            uint64_t contextData) override;

  virtual void finishNormalConformance(NormalProtocolConformance *conformance,
                                       uint64_t contextData) override;

  GenericEnvironment *loadGenericEnvironment(const DeclContext *decl,
                                             uint64_t contextData) override;

  Optional<StringRef> getGroupNameById(unsigned Id) const;
  Optional<StringRef> getSourceFileNameById(unsigned Id) const;
  Optional<StringRef> getGroupNameForDecl(const Decl *D) const;
  Optional<StringRef> getSourceFileNameForDecl(const Decl *D) const;
  Optional<unsigned> getSourceOrderForDecl(const Decl *D) const;
  void collectAllGroups(std::vector<StringRef> &Names) const;
  Optional<CommentInfo> getCommentForDecl(const Decl *D) const;
  Optional<CommentInfo> getCommentForDeclByUSR(StringRef USR) const;
  Optional<StringRef> getGroupNameByUSR(StringRef USR) const;

  Identifier getDiscriminatorForPrivateValue(const ValueDecl *D);

  // MARK: Deserialization interface

  llvm::BitstreamCursor getSILCursor() const {
    return SILCursor;
  }
  llvm::BitstreamCursor getSILIndexCursor() const {
    return SILIndexCursor;
  }

  /// Returns the type with the given ID, deserializing it if needed.
  ///
  /// \sa getTypeChecked
  Type getType(serialization::TypeID TID);

  /// Returns the type with the given ID, deserializing it if needed.
  llvm::Expected<Type> getTypeChecked(serialization::TypeID TID);

  /// Returns the base name with the given ID, deserializing it if needed.
  DeclBaseName getDeclBaseName(serialization::IdentifierID IID);

  /// Convenience method to retrieve the identifier backing the name with
  /// given ID. Asserts that the name with this ID is not special.
  Identifier getIdentifier(serialization::IdentifierID IID);

  /// Returns the decl with the given ID, deserializing it if needed.
  ///
  /// \param DID The ID for the decl within this module.
  /// \param ForcedContext Optional override for the decl context of certain
  ///                      kinds of decls, used to avoid re-entrant
  ///                      deserialization.
  ///
  /// \sa getDeclChecked
  Decl *getDecl(serialization::DeclID DID,
                Optional<DeclContext *> ForcedContext = None);

  /// Returns the decl with the given ID, deserializing it if needed.
  ///
  /// \param DID The ID for the decl within this module.
  /// \param ForcedContext Optional override for the decl context of certain
  ///                      kinds of decls, used to avoid re-entrant
  ///                      deserialization.
  llvm::Expected<Decl *>
  getDeclChecked(serialization::DeclID DID,
                 Optional<DeclContext *> ForcedContext = None);

  /// Returns the decl context with the given ID, deserializing it if needed.
  DeclContext *getDeclContext(serialization::DeclContextID DID);

  /// Returns the local decl context with the given ID, deserializing it if needed.
  DeclContext *getLocalDeclContext(serialization::DeclContextID DID);

  /// Returns the appropriate module for the given ID.
  ModuleDecl *getModule(serialization::ModuleID MID);

  /// Returns the appropriate module for the given name.
  ///
  /// If the name matches the name of the current module, a shadowed module
  /// is loaded instead.
  ModuleDecl *getModule(ArrayRef<Identifier> name);

  /// Returns the generic signature for the given ID.
  GenericSignature *getGenericSignature(serialization::GenericSignatureID ID);

  /// Returns the generic signature or environment for the given ID,
  /// deserializing it if needed.
  ///
  /// \param wantEnvironment If true, always return the full generic
  /// environment. Otherwise, only return the generic environment if it's
  /// already been constructed, and the signature in other cases.
  llvm::PointerUnion<GenericSignature *, GenericEnvironment *>
  getGenericSignatureOrEnvironment(serialization::GenericEnvironmentID ID,
                                   bool wantEnvironment = false);

  /// Returns the generic environment for the given ID, deserializing it if
  /// needed.
  GenericEnvironment *getGenericEnvironment(
                                        serialization::GenericEnvironmentID ID);

  /// Reads a substitution record from \c DeclTypeCursor.
  ///
  /// If the record at the cursor is not a substitution, returns None.
  Optional<Substitution> maybeReadSubstitution(llvm::BitstreamCursor &Cursor,
                                               GenericEnvironment *genericEnv =
                                                nullptr);

  /// Recursively reads a protocol conformance from the given cursor.
  ProtocolConformanceRef readConformance(llvm::BitstreamCursor &Cursor,
                                         GenericEnvironment *genericEnv =
                                           nullptr);
  
  /// Read a SILLayout from the given cursor.
  SILLayout *readSILLayout(llvm::BitstreamCursor &Cursor);

  /// Read the given normal conformance from the current module file.
  NormalProtocolConformance *
  readNormalConformance(serialization::NormalConformanceID id);

  /// Reads a foreign error conformance from \c DeclTypeCursor, if present.
  Optional<ForeignErrorConvention> maybeReadForeignErrorConvention();
};

} // end namespace swift

#endif
