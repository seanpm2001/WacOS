//=== ModuleDependencyCacheSerialization.cpp - serialized format -*- C++ -*-==//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/AST/FileSystem.h"
#include "swift/AST/ModuleDependencies.h"
#include "swift/Basic/PrettyStackTrace.h"
#include "swift/Basic/Version.h"
#include "swift/DependencyScan/SerializedModuleDependencyCacheFormat.h"
#include "llvm/ADT/DenseMap.h"
#include <unordered_map>

using namespace swift;
using namespace dependencies;
using namespace module_dependency_cache_serialization;

// MARK: Deserialization
namespace {

class Deserializer {
  std::vector<std::string> Identifiers;
  std::vector<std::vector<uint64_t>> ArraysOfIdentifierIDs;
  llvm::BitstreamCursor Cursor;
  SmallVector<uint64_t, 64> Scratch;
  StringRef BlobData;

  // These return true if there was an error.
  bool readSignature();
  bool enterGraphBlock();
  bool readMetadata();
  bool readGraph(GlobalModuleDependenciesCache &cache);

  llvm::Optional<std::string> getIdentifier(unsigned n);
  llvm::Optional<std::vector<std::string>> getArray(unsigned n);

public:
  Deserializer(llvm::MemoryBufferRef Data) : Cursor(Data) {}
  bool readInterModuleDependenciesCache(GlobalModuleDependenciesCache &cache);
};

} // end namespace

/// Read in the expected signature: IMDC
bool Deserializer::readSignature() {
  for (unsigned char byte : MODULE_DEPENDENCY_CACHE_FORMAT_SIGNATURE) {
    if (Cursor.AtEndOfStream())
      return true;
    if (auto maybeRead = Cursor.Read(8)) {
      if (maybeRead.get() != byte)
        return true;
    } else {
      return true;
    }
  }
  return false;
}

/// Read in the info block and enter the top-level block which represents the
/// graph
bool Deserializer::enterGraphBlock() {
  // Read the BLOCKINFO_BLOCK, which contains metadata used when dumping
  // the binary data with llvm-bcanalyzer.
  {
    auto next = Cursor.advance();
    if (!next) {
      consumeError(next.takeError());
      return true;
    }

    if (next->Kind != llvm::BitstreamEntry::SubBlock)
      return true;

    if (next->ID != llvm::bitc::BLOCKINFO_BLOCK_ID)
      return true;

    if (!Cursor.ReadBlockInfoBlock())
      return true;
  }

  // Enters our top-level subblock,
  // which contains the actual module dependency information.
  {
    auto next = Cursor.advance();
    if (!next) {
      consumeError(next.takeError());
      return true;
    }

    if (next->Kind != llvm::BitstreamEntry::SubBlock)
      return true;

    if (next->ID != GRAPH_BLOCK_ID)
      return true;

    if (auto err = Cursor.EnterSubBlock(GRAPH_BLOCK_ID)) {
      consumeError(std::move(err));
      return true;
    }
  }
  return false;
}

/// Read in the serialized file's format version, error/exit if not matching
/// current version.
bool Deserializer::readMetadata() {
  using namespace graph_block;

  auto entry = Cursor.advance();
  if (!entry) {
    consumeError(entry.takeError());
    return true;
  }

  if (entry->Kind != llvm::BitstreamEntry::Record)
    return true;

  auto recordID = Cursor.readRecord(entry->ID, Scratch, &BlobData);
  if (!recordID) {
    consumeError(recordID.takeError());
    return true;
  }

  if (*recordID != METADATA)
    return true;

  unsigned majorVersion, minorVersion;

  MetadataLayout::readRecord(Scratch, majorVersion, minorVersion);
  if (majorVersion != MODULE_DEPENDENCY_CACHE_FORMAT_VERSION_MAJOR ||
      minorVersion != MODULE_DEPENDENCY_CACHE_FORMAT_VERSION_MINOR) {
    return true;
  }

  return false;
}

/// Read in the top-level block's graph structure by first reading in
/// all of the file's identifiers and arrays of identifiers, followed by
/// consuming individual module info records and registering them into the
/// cache.
bool Deserializer::readGraph(GlobalModuleDependenciesCache &cache) {
  using namespace graph_block;

  bool hasCurrentModule = false;
  std::string currentModuleName;
  unsigned currentTripleID;
  llvm::Optional<std::vector<std::string>> currentModuleDependencies;

  auto getTriple = [&]() {
    assert(currentTripleID &&
           "Expected target triple ID for a MODULE_DETAILS_NODE record");
    auto triple = getIdentifier(currentTripleID);
    if (!triple.hasValue())
      llvm::report_fatal_error("Unexpected MODULE_DETAILS_NODE record");
    return triple.getValue();
  };

  while (!Cursor.AtEndOfStream()) {
    auto entry = cantFail(Cursor.advance(), "Advance bitstream cursor");

    if (entry.Kind == llvm::BitstreamEntry::EndBlock) {
      Cursor.ReadBlockEnd();
      assert(Cursor.GetCurrentBitNo() % CHAR_BIT == 0);
      break;
    }

    if (entry.Kind != llvm::BitstreamEntry::Record)
      llvm::report_fatal_error("Bad bitstream entry kind");

    Scratch.clear();
    unsigned recordID =
        cantFail(Cursor.readRecord(entry.ID, Scratch, &BlobData),
                 "Read bitstream record");

    switch (recordID) {
    case METADATA: {
      // METADATA must appear at the beginning and is read by readMetadata().
      llvm::report_fatal_error("Unexpected METADATA record");
      break;
    }

    case IDENTIFIER_NODE: {
      // IDENTIFIER_NODE must come before MODULE_NODEs.
      if (hasCurrentModule)
        llvm::report_fatal_error("Unexpected IDENTIFIER_NODE record");
      IdentifierNodeLayout::readRecord(Scratch);
      Identifiers.push_back(BlobData.str());
      break;
    }

    case IDENTIFIER_ARRAY_NODE: {
      // IDENTIFIER_ARRAY_NODE must come before MODULE_NODEs.
      if (hasCurrentModule)
        llvm::report_fatal_error("Unexpected IDENTIFIER_NODE record");
      ArrayRef<uint64_t> identifierIDs;
      IdentifierArrayLayout::readRecord(Scratch, identifierIDs);
      ArraysOfIdentifierIDs.push_back(identifierIDs.vec());
      break;
    }

    case MODULE_NODE: {
      hasCurrentModule = true;
      unsigned moduleNameID, tripleID, moduleDependenciesArrayID;
      ModuleInfoLayout::readRecord(Scratch, moduleNameID, tripleID,
                                   moduleDependenciesArrayID);
      auto moduleName = getIdentifier(moduleNameID);
      if (!moduleName)
        llvm::report_fatal_error("Bad module name");
      currentModuleName = *moduleName;
      currentTripleID = tripleID;
      currentModuleDependencies = getArray(moduleDependenciesArrayID);
      if (!currentModuleDependencies)
        llvm::report_fatal_error("Bad direct dependencies");
      break;
    }

    case SWIFT_INTERFACE_MODULE_DETAILS_NODE: {
      if (!hasCurrentModule)
        llvm::report_fatal_error(
            "Unexpected SWIFT_TEXTUAL_MODULE_DETAILS_NODE record");
      cache.configureForTriple(getTriple());
      unsigned interfaceFileID, compiledModuleCandidatesArrayID,
          buildCommandLineArrayID, extraPCMArgsArrayID, contextHashID,
          isFramework, bridgingHeaderFileID, sourceFilesArrayID,
          bridgingSourceFilesArrayID, bridgingModuleDependenciesArrayID;
      SwiftInterfaceModuleDetailsLayout::readRecord(
          Scratch, interfaceFileID, compiledModuleCandidatesArrayID,
          buildCommandLineArrayID, extraPCMArgsArrayID, contextHashID,
          isFramework, bridgingHeaderFileID, sourceFilesArrayID,
          bridgingSourceFilesArrayID, bridgingModuleDependenciesArrayID);

      Optional<std::string> optionalSwiftInterfaceFile;
      if (interfaceFileID != 0) {
        auto swiftInterfaceFile = getIdentifier(interfaceFileID);
        if (!swiftInterfaceFile)
          llvm::report_fatal_error("Bad swift interface file path");
        optionalSwiftInterfaceFile = *swiftInterfaceFile;
      }
      auto compiledModuleCandidates = getArray(compiledModuleCandidatesArrayID);
      if (!compiledModuleCandidates)
        llvm::report_fatal_error("Bad compiled module candidates");
      auto commandLine = getArray(buildCommandLineArrayID);
      if (!commandLine)
        llvm::report_fatal_error("Bad command line");
      auto extraPCMArgs = getArray(extraPCMArgsArrayID);
      if (!extraPCMArgs)
        llvm::report_fatal_error("Bad PCM Args set");
      auto contextHash = getIdentifier(contextHashID);
      if (!contextHash)
        llvm::report_fatal_error("Bad context hash");

      // forSwiftInterface API demands references here.
      std::vector<StringRef> buildCommandRefs;
      for (auto &arg : *commandLine)
        buildCommandRefs.push_back(arg);
      std::vector<StringRef> extraPCMRefs;
      for (auto &arg : *extraPCMArgs)
        extraPCMRefs.push_back(arg);

      // Form the dependencies storage object
      auto moduleDep = ModuleDependencies::forSwiftInterfaceModule(
          optionalSwiftInterfaceFile.getValue(), *compiledModuleCandidates,
          buildCommandRefs, extraPCMRefs, *contextHash, isFramework);

      // Add dependencies of this module
      for (const auto &moduleName : *currentModuleDependencies)
        moduleDep.addModuleDependency(moduleName);

      // Add bridging header file path
      if (bridgingHeaderFileID != 0) {
        auto bridgingHeaderFile = getIdentifier(bridgingHeaderFileID);
        if (!bridgingHeaderFile)
          llvm::report_fatal_error("Bad bridging header path");

        moduleDep.addBridgingHeader(*bridgingHeaderFile);
      }

      // Add bridging source files
      auto bridgingSourceFiles = getArray(bridgingSourceFilesArrayID);
      if (!bridgingSourceFiles)
        llvm::report_fatal_error("Bad bridging source files");
      for (const auto &file : *bridgingSourceFiles)
        moduleDep.addBridgingSourceFile(file);

      // Add source files
      auto sourceFiles = getArray(sourceFilesArrayID);
      if (!sourceFiles)
        llvm::report_fatal_error("Bad bridging source files");
      for (const auto &file : *sourceFiles)
        moduleDep.addSourceFile(file);

      // Add bridging module dependencies
      auto bridgingModuleDeps = getArray(bridgingModuleDependenciesArrayID);
      if (!bridgingModuleDeps)
        llvm::report_fatal_error("Bad bridging module dependencies");
      llvm::StringSet<> alreadyAdded;
      for (const auto &mod : *bridgingModuleDeps)
        moduleDep.addBridgingModuleDependency(mod, alreadyAdded);

      cache.recordDependencies(currentModuleName, std::move(moduleDep));
      hasCurrentModule = false;
      break;
    }

    case SWIFT_SOURCE_MODULE_DETAILS_NODE: {
      if (!hasCurrentModule)
        llvm::report_fatal_error(
            "Unexpected SWIFT_SOURCE_MODULE_DETAILS_NODE record");
      // Expected triple ID is 0
      if (currentTripleID)
        llvm::report_fatal_error(
            "Unexpected target triple on MODULE_NODE corresponding to a "
            "SWIFT_SOURCE_MODULE_DETAILS_NODE record");
      unsigned extraPCMArgsArrayID, bridgingHeaderFileID, sourceFilesArrayID,
          bridgingSourceFilesArrayID, bridgingModuleDependenciesArrayID;
      SwiftSourceModuleDetailsLayout::readRecord(
          Scratch, extraPCMArgsArrayID, bridgingHeaderFileID,
          sourceFilesArrayID, bridgingSourceFilesArrayID,
          bridgingModuleDependenciesArrayID);

      auto extraPCMArgs = getArray(extraPCMArgsArrayID);
      if (!extraPCMArgs)
        llvm::report_fatal_error("Bad PCM Args set");
      std::vector<StringRef> extraPCMRefs;
      for (auto &arg : *extraPCMArgs)
        extraPCMRefs.push_back(arg);

      // Form the dependencies storage object
      auto moduleDep = ModuleDependencies::forSwiftSourceModule(extraPCMRefs);

      // Add dependencies of this module
      for (const auto &moduleName : *currentModuleDependencies)
        moduleDep.addModuleDependency(moduleName);

      // Add bridging header file path
      if (bridgingHeaderFileID != 0) {
        auto bridgingHeaderFile = getIdentifier(bridgingHeaderFileID);
        if (!bridgingHeaderFile)
          llvm::report_fatal_error("Bad bridging header path");

        moduleDep.addBridgingHeader(*bridgingHeaderFile);
      }

      // Add bridging source files
      auto bridgingSourceFiles = getArray(bridgingSourceFilesArrayID);
      if (!bridgingSourceFiles)
        llvm::report_fatal_error("Bad bridging source files");
      for (const auto &file : *bridgingSourceFiles)
        moduleDep.addBridgingSourceFile(file);

      // Add source files
      auto sourceFiles = getArray(sourceFilesArrayID);
      if (!sourceFiles)
        llvm::report_fatal_error("Bad bridging source files");
      for (const auto &file : *sourceFiles)
        moduleDep.addSourceFile(file);

      // Add bridging module dependencies
      auto bridgingModuleDeps = getArray(bridgingModuleDependenciesArrayID);
      if (!bridgingModuleDeps)
        llvm::report_fatal_error("Bad bridging module dependencies");
      llvm::StringSet<> alreadyAdded;
      for (const auto &mod : *bridgingModuleDeps)
        moduleDep.addBridgingModuleDependency(mod, alreadyAdded);

      cache.recordDependencies(currentModuleName, std::move(moduleDep));
      hasCurrentModule = false;
      break;
    }

    case SWIFT_BINARY_MODULE_DETAILS_NODE: {
      if (!hasCurrentModule)
        llvm::report_fatal_error(
            "Unexpected SWIFT_BINARY_MODULE_DETAILS_NODE record");
      cache.configureForTriple(getTriple());
      unsigned compiledModulePathID, moduleDocPathID, moduleSourceInfoPathID,
          isFramework;
      SwiftBinaryModuleDetailsLayout::readRecord(
          Scratch, compiledModulePathID, moduleDocPathID,
          moduleSourceInfoPathID, isFramework);

      auto compiledModulePath = getIdentifier(compiledModulePathID);
      if (!compiledModulePath)
        llvm::report_fatal_error("Bad compiled module path");
      auto moduleDocPath = getIdentifier(moduleDocPathID);
      if (!moduleDocPath)
        llvm::report_fatal_error("Bad module doc path");
      auto moduleSourceInfoPath = getIdentifier(moduleSourceInfoPathID);
      if (!moduleSourceInfoPath)
        llvm::report_fatal_error("Bad module source info path");

      // Form the dependencies storage object
      auto moduleDep = ModuleDependencies::forSwiftBinaryModule(
          *compiledModulePath, *moduleDocPath, *moduleSourceInfoPath,
          isFramework);
      // Add dependencies of this module
      for (const auto &moduleName : *currentModuleDependencies)
        moduleDep.addModuleDependency(moduleName);

      cache.recordDependencies(currentModuleName, std::move(moduleDep));
      hasCurrentModule = false;
      break;
    }

    case SWIFT_PLACEHOLDER_MODULE_DETAILS_NODE: {
      if (!hasCurrentModule)
        llvm::report_fatal_error(
            "Unexpected SWIFT_PLACEHOLDER_MODULE_DETAILS_NODE record");
      cache.configureForTriple(getTriple());
      unsigned compiledModulePathID, moduleDocPathID, moduleSourceInfoPathID;
      SwiftPlaceholderModuleDetailsLayout::readRecord(
          Scratch, compiledModulePathID, moduleDocPathID,
          moduleSourceInfoPathID);

      auto compiledModulePath = getIdentifier(compiledModulePathID);
      if (!compiledModulePath)
        llvm::report_fatal_error("Bad compiled module path");
      auto moduleDocPath = getIdentifier(moduleDocPathID);
      if (!moduleDocPath)
        llvm::report_fatal_error("Bad module doc path");
      auto moduleSourceInfoPath = getIdentifier(moduleSourceInfoPathID);
      if (!moduleSourceInfoPath)
        llvm::report_fatal_error("Bad module source info path");

      // Form the dependencies storage object
      auto moduleDep = ModuleDependencies::forPlaceholderSwiftModuleStub(
          *compiledModulePath, *moduleDocPath, *moduleSourceInfoPath);
      // Add dependencies of this module
      for (const auto &moduleName : *currentModuleDependencies)
        moduleDep.addModuleDependency(moduleName);

      cache.recordDependencies(currentModuleName, std::move(moduleDep));
      hasCurrentModule = false;
      break;
    }

    case CLANG_MODULE_DETAILS_NODE: {
      if (!hasCurrentModule)
        llvm::report_fatal_error("Unexpected CLANG_MODULE_DETAILS_NODE record");
      cache.configureForTriple(getTriple());
      unsigned moduleMapPathID, contextHashID, commandLineArrayID,
               fileDependenciesArrayID, capturedPCMArgsArrayID;
      ClangModuleDetailsLayout::readRecord(Scratch, moduleMapPathID,
                                           contextHashID, commandLineArrayID,
                                           fileDependenciesArrayID,
                                           capturedPCMArgsArrayID);
      auto moduleMapPath = getIdentifier(moduleMapPathID);
      if (!moduleMapPath)
        llvm::report_fatal_error("Bad module map path");
      auto contextHash = getIdentifier(contextHashID);
      if (!contextHash)
        llvm::report_fatal_error("Bad context hash");
      auto commandLineArgs = getArray(commandLineArrayID);
      if (!commandLineArgs)
        llvm::report_fatal_error("Bad command line");
      auto fileDependencies = getArray(fileDependenciesArrayID);
      if (!fileDependencies)
        llvm::report_fatal_error("Bad file dependencies");
      auto capturedPCMArgs = getArray(capturedPCMArgsArrayID);
      if (!capturedPCMArgs)
        llvm::report_fatal_error("Bad captured PCM Args");

      // Form the dependencies storage object
      auto moduleDep = ModuleDependencies::forClangModule(
          *moduleMapPath, *contextHash, *commandLineArgs, *fileDependencies,
          *capturedPCMArgs);

      // Add dependencies of this module
      for (const auto &moduleName : *currentModuleDependencies)
        moduleDep.addModuleDependency(moduleName);

      cache.recordDependencies(currentModuleName, std::move(moduleDep));
      hasCurrentModule = false;
      break;
    }

    default: {
      llvm::report_fatal_error("Unknown record ID");
    }
    }
  }

  return false;
}

bool Deserializer::readInterModuleDependenciesCache(
    GlobalModuleDependenciesCache &cache) {
  using namespace graph_block;

  if (readSignature())
    return true;

  if (enterGraphBlock())
    return true;

  if (readMetadata())
    return true;

  if (readGraph(cache))
    return true;

  return false;
}

llvm::Optional<std::string> Deserializer::getIdentifier(unsigned n) {
  if (n == 0)
    return std::string();

  --n;
  if (n >= Identifiers.size())
    return None;

  return Identifiers[n];
}

llvm::Optional<std::vector<std::string>> Deserializer::getArray(unsigned n) {
  if (n == 0)
    return std::vector<std::string>();

  --n;
  if (n >= ArraysOfIdentifierIDs.size())
    return None;

  auto &identifierIDs = ArraysOfIdentifierIDs[n];

  auto IDtoStringMap = [this](unsigned id) {
    auto identifier = getIdentifier(id);
    if (!identifier)
      llvm::report_fatal_error("Bad identifier array element");
    return *identifier;
  };
  std::vector<std::string> result;
  result.reserve(identifierIDs.size());
  std::transform(identifierIDs.begin(), identifierIDs.end(),
                 std::back_inserter(result), IDtoStringMap);
  return result;
}

bool swift::dependencies::module_dependency_cache_serialization::
    readInterModuleDependenciesCache(llvm::MemoryBuffer &buffer,
                                     GlobalModuleDependenciesCache &cache) {
  Deserializer deserializer(buffer.getMemBufferRef());
  return deserializer.readInterModuleDependenciesCache(cache);
}

bool swift::dependencies::module_dependency_cache_serialization::
    readInterModuleDependenciesCache(StringRef path,
                                     GlobalModuleDependenciesCache &cache) {
  PrettyStackTraceStringAction stackTrace(
      "loading inter-module dependency graph", path);
  auto buffer = llvm::MemoryBuffer::getFile(path);
  if (!buffer)
    return true;

  return readInterModuleDependenciesCache(*buffer.get(), cache);
}

// MARK: Serialization

/// Kinds of arrays that we track being serialized. Used to query serialized
/// array ID for a given module.
enum ModuleIdentifierArrayKind : uint8_t {
  Empty = 0,
  DirectDependencies,
  CompiledModuleCandidates,
  BuildCommandLine,
  ExtraPCMArgs,
  SourceFiles,
  BridgingSourceFiles,
  BridgingModuleDependencies,
  NonPathCommandLine,
  FileDependencies,
  CapturedPCMArgs,
  LastArrayKind
};

using ModuleIdentifierArrayKey =
    std::pair<ModuleDependencyID, ModuleIdentifierArrayKind>;

// DenseMap Infos for hashing of ModuleIdentifierArrayKind
template <>
struct llvm::DenseMapInfo<ModuleIdentifierArrayKind> {
  using UnderlyingType = std::underlying_type<ModuleIdentifierArrayKind>::type;
  using UnerlyingInfo = DenseMapInfo<UnderlyingType>;

  static inline ModuleIdentifierArrayKind getEmptyKey() {
    return ModuleIdentifierArrayKind::Empty;
  }
  static inline ModuleIdentifierArrayKind getTombstoneKey() {
    return ModuleIdentifierArrayKind::LastArrayKind;
  }
  static unsigned getHashValue(const ModuleIdentifierArrayKind &arrKind) {
    auto underlyingValue = static_cast<UnderlyingType>(arrKind);
    return UnerlyingInfo::getHashValue(underlyingValue);
  }
  static bool isEqual(const ModuleIdentifierArrayKind &LHS,
                      const ModuleIdentifierArrayKind &RHS) {
    return LHS == RHS;
  }
};

namespace std {
template <>
struct hash<ModuleDependencyID> {
  using UnderlyingKindType = std::underlying_type<ModuleDependenciesKind>::type;
  std::size_t operator()(const ModuleDependencyID &id) const {
    auto underlyingKindValue = static_cast<UnderlyingKindType>(id.second);

    return (hash<string>()(id.first) ^
            (hash<UnderlyingKindType>()(underlyingKindValue)));
  }
};
} // namespace std

namespace {

class Serializer {
  llvm::StringMap<unsigned, llvm::BumpPtrAllocator> IdentifierIDs;
  std::unordered_map<ModuleDependencyID,
                     llvm::DenseMap<ModuleIdentifierArrayKind, unsigned>>
      ArrayIDs;
  unsigned LastIdentifierID = 0;
  unsigned LastArrayID = 0;
  std::vector<StringRef> Identifiers;
  std::vector<std::vector<unsigned>> ArraysOfIdentifiers;

  llvm::BitstreamWriter &Out;

  /// A reusable buffer for emitting records.
  SmallVector<uint64_t, 64> ScratchRecord;
  std::array<unsigned, 256> AbbrCodes;

  // Returns the identifier ID of the added identifier, either
  // new or previously-hashed
  unsigned addIdentifier(const std::string &str);
  unsigned getIdentifier(const std::string &str) const;

  // Returns the array ID of the added array
  void addArray(ModuleDependencyID moduleID,
                ModuleIdentifierArrayKind arrayKind,
                const std::vector<std::string> &vec);
  unsigned getArray(ModuleDependencyID moduleID,
                    ModuleIdentifierArrayKind arrayKind) const;

  template <typename Layout>
  void registerRecordAbbr() {
    using AbbrArrayTy = decltype(AbbrCodes);
    static_assert(Layout::Code <= std::tuple_size<AbbrArrayTy>::value,
                  "layout has invalid record code");
    AbbrCodes[Layout::Code] = Layout::emitAbbrev(Out);
  }

  void collectStringsAndArrays(const GlobalModuleDependenciesCache &cache);

  void emitBlockID(unsigned ID, StringRef name,
                   SmallVectorImpl<unsigned char> &nameBuffer);

  void emitRecordID(unsigned ID, StringRef name,
                    SmallVectorImpl<unsigned char> &nameBuffer);

  void writeSignature();
  void writeBlockInfoBlock();

  void writeMetadata();
  void writeIdentifiers();
  void writeArraysOfIdentifiers();

  void writeModuleInfo(ModuleDependencyID moduleID,
                       Optional<std::string> triple,
                       const ModuleDependencies &dependencyInfo);

public:
  Serializer(llvm::BitstreamWriter &ExistingOut) : Out(ExistingOut) {}

public:
  void
  writeInterModuleDependenciesCache(const GlobalModuleDependenciesCache &cache);
};

} // end namespace

/// Record the name of a block.
void Serializer::emitBlockID(unsigned ID, StringRef name,
                             SmallVectorImpl<unsigned char> &nameBuffer) {
  SmallVector<unsigned, 1> idBuffer;
  idBuffer.push_back(ID);
  Out.EmitRecord(llvm::bitc::BLOCKINFO_CODE_SETBID, idBuffer);

  // Emit the block name if present.
  if (name.empty())
    return;
  nameBuffer.resize(name.size());
  memcpy(nameBuffer.data(), name.data(), name.size());
  Out.EmitRecord(llvm::bitc::BLOCKINFO_CODE_BLOCKNAME, nameBuffer);
}

/// Record the name of a record.
void Serializer::emitRecordID(unsigned ID, StringRef name,
                              SmallVectorImpl<unsigned char> &nameBuffer) {
  assert(ID < 256 && "can't fit record ID in next to name");
  nameBuffer.resize(name.size() + 1);
  nameBuffer[0] = ID;
  memcpy(nameBuffer.data() + 1, name.data(), name.size());
  Out.EmitRecord(llvm::bitc::BLOCKINFO_CODE_SETRECORDNAME, nameBuffer);
}

void Serializer::writeBlockInfoBlock() {
  llvm::BCBlockRAII restoreBlock(Out, llvm::bitc::BLOCKINFO_BLOCK_ID, 2);

  SmallVector<unsigned char, 64> nameBuffer;
#define BLOCK(X) emitBlockID(X##_ID, #X, nameBuffer)
#define BLOCK_RECORD(K, X) emitRecordID(K::X, #X, nameBuffer)

  BLOCK(GRAPH_BLOCK);
  BLOCK_RECORD(graph_block, METADATA);
  BLOCK_RECORD(graph_block, IDENTIFIER_NODE);
  BLOCK_RECORD(graph_block, IDENTIFIER_ARRAY_NODE);

  BLOCK_RECORD(graph_block, MODULE_NODE);
  BLOCK_RECORD(graph_block, SWIFT_INTERFACE_MODULE_DETAILS_NODE);
  BLOCK_RECORD(graph_block, SWIFT_SOURCE_MODULE_DETAILS_NODE);
  BLOCK_RECORD(graph_block, SWIFT_BINARY_MODULE_DETAILS_NODE);
  BLOCK_RECORD(graph_block, SWIFT_PLACEHOLDER_MODULE_DETAILS_NODE);
  BLOCK_RECORD(graph_block, CLANG_MODULE_DETAILS_NODE);
}

void Serializer::writeSignature() {
  for (auto c : MODULE_DEPENDENCY_CACHE_FORMAT_SIGNATURE)
    Out.Emit((unsigned)c, 8);
}

void Serializer::writeMetadata() {
  using namespace graph_block;

  MetadataLayout::emitRecord(Out, ScratchRecord,
                             AbbrCodes[MetadataLayout::Code],
                             MODULE_DEPENDENCY_CACHE_FORMAT_VERSION_MAJOR,
                             MODULE_DEPENDENCY_CACHE_FORMAT_VERSION_MINOR,
                             version::getSwiftFullVersion());
}

void Serializer::writeIdentifiers() {
  using namespace graph_block;
  for (auto str : Identifiers) {
    IdentifierNodeLayout::emitRecord(
        Out, ScratchRecord, AbbrCodes[IdentifierNodeLayout::Code], str);
  }
}

void Serializer::writeArraysOfIdentifiers() {
  using namespace graph_block;
  for (auto vec : ArraysOfIdentifiers) {
    IdentifierArrayLayout::emitRecord(
        Out, ScratchRecord, AbbrCodes[IdentifierArrayLayout::Code], vec);
  }
}

void Serializer::writeModuleInfo(ModuleDependencyID moduleID,
                                 Optional<std::string> triple,
                                 const ModuleDependencies &dependencyInfo) {
  using namespace graph_block;
  auto tripleStrID = triple.hasValue() ? getIdentifier(triple.getValue()) : 0;

  ModuleInfoLayout::emitRecord(
      Out, ScratchRecord, AbbrCodes[ModuleInfoLayout::Code],
      getIdentifier(moduleID.first), tripleStrID,
      getArray(moduleID, ModuleIdentifierArrayKind::DirectDependencies));

  switch (dependencyInfo.getKind()) {
  case swift::ModuleDependenciesKind::SwiftInterface: {
    assert(triple.hasValue() && "Expected triple for serializing MODULE_NODE");
    auto swiftTextDeps = dependencyInfo.getAsSwiftInterfaceModule();
    assert(swiftTextDeps);
    unsigned swiftInterfaceFileId =
        getIdentifier(swiftTextDeps->swiftInterfaceFile);
    unsigned bridgingHeaderFileId =
        swiftTextDeps->textualModuleDetails.bridgingHeaderFile
            ? getIdentifier(swiftTextDeps->textualModuleDetails
                                .bridgingHeaderFile.getValue())
            : 0;
    SwiftInterfaceModuleDetailsLayout::emitRecord(
        Out, ScratchRecord, AbbrCodes[SwiftInterfaceModuleDetailsLayout::Code],
        swiftInterfaceFileId,
        getArray(moduleID, ModuleIdentifierArrayKind::CompiledModuleCandidates),
        getArray(moduleID, ModuleIdentifierArrayKind::BuildCommandLine),
        getArray(moduleID, ModuleIdentifierArrayKind::ExtraPCMArgs),
        getIdentifier(swiftTextDeps->contextHash), swiftTextDeps->isFramework,
        bridgingHeaderFileId,
        getArray(moduleID, ModuleIdentifierArrayKind::SourceFiles),
        getArray(moduleID, ModuleIdentifierArrayKind::BridgingSourceFiles),
        getArray(moduleID,
                 ModuleIdentifierArrayKind::BridgingModuleDependencies));
    break;
  }
  case swift::ModuleDependenciesKind::SwiftSource: {
    assert(!triple.hasValue() &&
           "Did not expecte triple for serializing MODULE_NODE");
    auto swiftSourceDeps = dependencyInfo.getAsSwiftSourceModule();
    assert(swiftSourceDeps);
    unsigned bridgingHeaderFileId =
        swiftSourceDeps->textualModuleDetails.bridgingHeaderFile
            ? getIdentifier(swiftSourceDeps->textualModuleDetails
                                .bridgingHeaderFile.getValue())
            : 0;
    SwiftSourceModuleDetailsLayout::emitRecord(
        Out, ScratchRecord, AbbrCodes[SwiftSourceModuleDetailsLayout::Code],
        getArray(moduleID, ModuleIdentifierArrayKind::ExtraPCMArgs),
        bridgingHeaderFileId,
        getArray(moduleID, ModuleIdentifierArrayKind::SourceFiles),
        getArray(moduleID, ModuleIdentifierArrayKind::BridgingSourceFiles),
        getArray(moduleID,
                 ModuleIdentifierArrayKind::BridgingModuleDependencies));
    break;
  }
  case swift::ModuleDependenciesKind::SwiftBinary: {
    assert(triple.hasValue() && "Expected triple for serializing MODULE_NODE");
    auto swiftBinDeps = dependencyInfo.getAsSwiftBinaryModule();
    assert(swiftBinDeps);
    SwiftBinaryModuleDetailsLayout::emitRecord(
        Out, ScratchRecord, AbbrCodes[SwiftBinaryModuleDetailsLayout::Code],
        getIdentifier(swiftBinDeps->compiledModulePath),
        getIdentifier(swiftBinDeps->moduleDocPath),
        getIdentifier(swiftBinDeps->sourceInfoPath), swiftBinDeps->isFramework);

    break;
  }
  case swift::ModuleDependenciesKind::SwiftPlaceholder: {
    assert(triple.hasValue() && "Expected triple for serializing MODULE_NODE");
    auto swiftPHDeps = dependencyInfo.getAsPlaceholderDependencyModule();
    assert(swiftPHDeps);
    SwiftPlaceholderModuleDetailsLayout::emitRecord(
        Out, ScratchRecord,
        AbbrCodes[SwiftPlaceholderModuleDetailsLayout::Code],
        getIdentifier(swiftPHDeps->compiledModulePath),
        getIdentifier(swiftPHDeps->moduleDocPath),
        getIdentifier(swiftPHDeps->sourceInfoPath));
    break;
  }
  case swift::ModuleDependenciesKind::Clang: {
    assert(triple.hasValue() && "Expected triple for serializing MODULE_NODE");
    auto clangDeps = dependencyInfo.getAsClangModule();
    assert(clangDeps);
    ClangModuleDetailsLayout::emitRecord(
        Out, ScratchRecord, AbbrCodes[ClangModuleDetailsLayout::Code],
        getIdentifier(clangDeps->moduleMapFile),
        getIdentifier(clangDeps->contextHash),
        getArray(moduleID, ModuleIdentifierArrayKind::NonPathCommandLine),
        getArray(moduleID, ModuleIdentifierArrayKind::FileDependencies),
        getArray(moduleID, ModuleIdentifierArrayKind::CapturedPCMArgs));

    break;
  }
  default:
    llvm_unreachable("Unhandled dependency kind.");
  }
}

unsigned Serializer::addIdentifier(const std::string &str) {
  if (str.empty())
    return 0;

  decltype(IdentifierIDs)::iterator iter;
  bool isNew;
  std::tie(iter, isNew) = IdentifierIDs.insert({str, LastIdentifierID + 1});

  if (!isNew)
    return iter->getValue();

  ++LastIdentifierID;
  // Note that we use the string data stored in the StringMap.
  Identifiers.push_back(iter->getKey());
  return iter->getValue();
}

unsigned Serializer::getIdentifier(const std::string &str) const {
  if (str.empty())
    return 0;

  auto iter = IdentifierIDs.find(str);
  assert(iter != IdentifierIDs.end());
  assert(iter->second != 0);
  return iter->second;
}

void Serializer::addArray(ModuleDependencyID moduleID,
                          ModuleIdentifierArrayKind arrayKind,
                          const std::vector<std::string> &vec) {
  if (ArrayIDs.find(moduleID) != ArrayIDs.end()) {
    // Already have arrays for this module
    llvm::DenseMap<ModuleIdentifierArrayKind, unsigned>::iterator iter;
    bool isNew;
    std::tie(iter, isNew) =
        ArrayIDs[moduleID].insert({arrayKind, LastArrayID + 1});
    if (!isNew)
      return;
  } else {
    // Do not yet have any arrays for this module
    ArrayIDs[moduleID] = llvm::DenseMap<ModuleIdentifierArrayKind, unsigned>();
    ArrayIDs[moduleID].insert({arrayKind, LastArrayID + 1});
  }

  ++LastArrayID;

  // Add in the individual identifiers in the array
  std::vector<unsigned> identifierIDs;
  identifierIDs.reserve(vec.size());
  for (const auto &id : vec) {
    identifierIDs.push_back(addIdentifier(id));
  }

  ArraysOfIdentifiers.push_back(identifierIDs);
  return;
}

unsigned Serializer::getArray(ModuleDependencyID moduleID,
                              ModuleIdentifierArrayKind arrayKind) const {
  auto iter = ArrayIDs.find(moduleID);
  assert(iter != ArrayIDs.end());
  auto &innerMap = iter->second;
  auto arrayIter = innerMap.find(arrayKind);
  assert(arrayIter != innerMap.end());
  return arrayIter->second;
}

void Serializer::collectStringsAndArrays(
    const GlobalModuleDependenciesCache &cache) {
  for (auto &moduleID : cache.getAllSourceModules()) {
    assert(moduleID.second == ModuleDependenciesKind::SwiftSource &&
           "Expected source-based dependency");
    auto optionalDependencyInfo =
        cache.findSourceModuleDependency(moduleID.first);
    assert(optionalDependencyInfo.hasValue() && "Expected dependency info.");
    auto dependencyInfo = optionalDependencyInfo.getValue();
    // Add the module's name
    addIdentifier(moduleID.first);
    // Add the module's dependencies
    addArray(moduleID, ModuleIdentifierArrayKind::DirectDependencies,
             dependencyInfo.getModuleDependencies());
    auto swiftSourceDeps = dependencyInfo.getAsSwiftSourceModule();
    assert(swiftSourceDeps);
    addArray(moduleID, ModuleIdentifierArrayKind::ExtraPCMArgs,
             swiftSourceDeps->textualModuleDetails.extraPCMArgs);
    if (swiftSourceDeps->textualModuleDetails.bridgingHeaderFile.hasValue())
      addIdentifier(
          swiftSourceDeps->textualModuleDetails.bridgingHeaderFile.getValue());
    addArray(moduleID, ModuleIdentifierArrayKind::SourceFiles,
             swiftSourceDeps->sourceFiles);
    addArray(moduleID, ModuleIdentifierArrayKind::BridgingSourceFiles,
             swiftSourceDeps->textualModuleDetails.bridgingSourceFiles);
    addArray(moduleID, ModuleIdentifierArrayKind::BridgingModuleDependencies,
             swiftSourceDeps->textualModuleDetails.bridgingModuleDependencies);
  }

  for (auto &triple : cache.getAllTriples()) {
    addIdentifier(triple);
    for (auto &moduleID : cache.getAllNonSourceModules(triple)) {
      auto dependencyInfos = cache.findAllDependenciesIrrespectiveOfSearchPaths(
          moduleID.first, moduleID.second);
      assert(dependencyInfos.hasValue() && "Expected dependency info.");
      for (auto &dependencyInfo : *dependencyInfos) {
        // Add the module's name
        addIdentifier(moduleID.first);
        // Add the module's dependencies
        addArray(moduleID, ModuleIdentifierArrayKind::DirectDependencies,
                 dependencyInfo.getModuleDependencies());

        // Add the dependency-kind-specific data
        switch (dependencyInfo.getKind()) {
        case swift::ModuleDependenciesKind::SwiftInterface: {
          auto swiftTextDeps = dependencyInfo.getAsSwiftInterfaceModule();
          assert(swiftTextDeps);
          addIdentifier(swiftTextDeps->swiftInterfaceFile);
          addArray(moduleID,
                   ModuleIdentifierArrayKind::CompiledModuleCandidates,
                   swiftTextDeps->compiledModuleCandidates);
          addArray(moduleID, ModuleIdentifierArrayKind::BuildCommandLine,
                   swiftTextDeps->buildCommandLine);
          addArray(moduleID, ModuleIdentifierArrayKind::ExtraPCMArgs,
                   swiftTextDeps->textualModuleDetails.extraPCMArgs);
          addIdentifier(swiftTextDeps->contextHash);
          if (swiftTextDeps->textualModuleDetails.bridgingHeaderFile.hasValue())
            addIdentifier(swiftTextDeps->textualModuleDetails.bridgingHeaderFile
                              .getValue());
          addArray(moduleID, ModuleIdentifierArrayKind::SourceFiles,
                   std::vector<std::string>());
          addArray(moduleID, ModuleIdentifierArrayKind::BridgingSourceFiles,
                   swiftTextDeps->textualModuleDetails.bridgingSourceFiles);
          addArray(
              moduleID, ModuleIdentifierArrayKind::BridgingModuleDependencies,
              swiftTextDeps->textualModuleDetails.bridgingModuleDependencies);
          break;
        }
        case swift::ModuleDependenciesKind::SwiftBinary: {
          auto swiftBinDeps = dependencyInfo.getAsSwiftBinaryModule();
          assert(swiftBinDeps);
          addIdentifier(swiftBinDeps->compiledModulePath);
          addIdentifier(swiftBinDeps->moduleDocPath);
          addIdentifier(swiftBinDeps->sourceInfoPath);
          break;
        }
        case swift::ModuleDependenciesKind::SwiftPlaceholder: {
          auto swiftPHDeps = dependencyInfo.getAsPlaceholderDependencyModule();
          assert(swiftPHDeps);
          addIdentifier(swiftPHDeps->compiledModulePath);
          addIdentifier(swiftPHDeps->moduleDocPath);
          addIdentifier(swiftPHDeps->sourceInfoPath);
          break;
        }
        case swift::ModuleDependenciesKind::Clang: {
          auto clangDeps = dependencyInfo.getAsClangModule();
          assert(clangDeps);
          addIdentifier(clangDeps->moduleMapFile);
          addIdentifier(clangDeps->contextHash);
          addArray(moduleID, ModuleIdentifierArrayKind::NonPathCommandLine,
                   clangDeps->nonPathCommandLine);
          addArray(moduleID, ModuleIdentifierArrayKind::FileDependencies,
                   clangDeps->fileDependencies);
          addArray(moduleID, ModuleIdentifierArrayKind::CapturedPCMArgs,
                   clangDeps->capturedPCMArgs);
          break;
        }
        default:
          llvm_unreachable("Unhandled dependency kind.");
        }
      }
    }
  }
}

void Serializer::writeInterModuleDependenciesCache(
    const GlobalModuleDependenciesCache &cache) {
  // Write the header
  writeSignature();
  writeBlockInfoBlock();

  // Enter the main graph block
  unsigned blockID = GRAPH_BLOCK_ID;
  llvm::BCBlockRAII restoreBlock(Out, blockID, 8);

  using namespace graph_block;

  registerRecordAbbr<MetadataLayout>();
  registerRecordAbbr<IdentifierNodeLayout>();
  registerRecordAbbr<IdentifierArrayLayout>();
  registerRecordAbbr<ModuleInfoLayout>();
  registerRecordAbbr<SwiftSourceModuleDetailsLayout>();
  registerRecordAbbr<SwiftInterfaceModuleDetailsLayout>();
  registerRecordAbbr<SwiftBinaryModuleDetailsLayout>();
  registerRecordAbbr<SwiftPlaceholderModuleDetailsLayout>();
  registerRecordAbbr<ClangModuleDetailsLayout>();

  // Make a pass to collect all unique strings and arrays
  // of strings
  collectStringsAndArrays(cache);

  // Write the version information
  writeMetadata();

  // Write the strings
  writeIdentifiers();

  // Write the arrays
  writeArraysOfIdentifiers();

  // Write the core graph
  // First, write the source modules we've encountered
  for (auto &moduleID : cache.getAllSourceModules()) {
    auto dependencyInfo = cache.findSourceModuleDependency(moduleID.first);
    assert(dependencyInfo.hasValue() && "Expected dependency info.");
    writeModuleInfo(moduleID, llvm::Optional<std::string>(),
                    dependencyInfo.getValue());
  }

  // Write all non-source modules, for each of the target triples this scanner
  // has been used with
  for (auto &triple : cache.getAllTriples()) {
    for (auto &moduleID : cache.getAllNonSourceModules(triple)) {
      auto dependencyInfos = cache.findAllDependenciesIrrespectiveOfSearchPaths(
          moduleID.first, moduleID.second);
      assert(dependencyInfos.hasValue() && "Expected dependency info.");
      for (auto &dependencyInfo : *dependencyInfos) {
        writeModuleInfo(moduleID, triple, dependencyInfo);
      }
    }
  }

  return;
}

void swift::dependencies::module_dependency_cache_serialization::
    writeInterModuleDependenciesCache(
        llvm::BitstreamWriter &Out,
        const GlobalModuleDependenciesCache &cache) {
  Serializer serializer{Out};
  serializer.writeInterModuleDependenciesCache(cache);
}

bool swift::dependencies::module_dependency_cache_serialization::
    writeInterModuleDependenciesCache(
        DiagnosticEngine &diags, StringRef path,
        const GlobalModuleDependenciesCache &cache) {
  PrettyStackTraceStringAction stackTrace(
      "saving inter-module dependency graph", path);
  return withOutputFile(diags, path, [&](llvm::raw_ostream &out) {
    SmallVector<char, 0> Buffer;
    llvm::BitstreamWriter Writer{Buffer};
    writeInterModuleDependenciesCache(Writer, cache);
    out.write(Buffer.data(), Buffer.size());
    out.flush();
    return false;
  });
}
