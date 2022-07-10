//===--- OutputFileMap.cpp - Driver output file map -----------------------===//
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

#include "swift/Driver/OutputFileMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <system_error>

using namespace swift;
using namespace swift::driver;

std::unique_ptr<OutputFileMap> OutputFileMap::loadFromPath(StringRef Path) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> FileBufOrErr =
    llvm::MemoryBuffer::getFile(Path);
  if (!FileBufOrErr)
    return nullptr;
  return loadFromBuffer(std::move(FileBufOrErr.get()));
}

std::unique_ptr<OutputFileMap> OutputFileMap::loadFromBuffer(StringRef Data) {
  std::unique_ptr<llvm::MemoryBuffer> Buffer{
    llvm::MemoryBuffer::getMemBuffer(Data)
  };
  return loadFromBuffer(std::move(Buffer));
}

std::unique_ptr<OutputFileMap>
OutputFileMap::loadFromBuffer(std::unique_ptr<llvm::MemoryBuffer> Buffer) {
  std::unique_ptr<OutputFileMap> OFM(new OutputFileMap());

  if (OFM->parse(std::move(Buffer)))
    return nullptr;

  return OFM;
}

const TypeToPathMap *OutputFileMap::getOutputMapForInput(StringRef Input) const{
  auto iter = InputToOutputsMap.find(Input);
  if (iter == InputToOutputsMap.end())
    return nullptr;
  else
    return &iter->second;
}

const TypeToPathMap *OutputFileMap::getOutputMapForSingleOutput() const {
  return getOutputMapForInput(StringRef());
}

void OutputFileMap::dump(llvm::raw_ostream &os, bool Sort) const {
  typedef std::pair<types::ID, std::string> TypePathPair;

  auto printOutputPair = [&os] (StringRef InputPath,
                                const TypePathPair &OutputPair) -> void {
    os << InputPath << " -> " << types::getTypeName(OutputPair.first) << ": \""
       << OutputPair.second << "\"\n";
  };

  if (Sort) {
    typedef std::pair<StringRef, TypeToPathMap> PathMapPair;
    std::vector<PathMapPair> Maps;
    for (auto &InputPair : InputToOutputsMap) {
      Maps.emplace_back(InputPair.first(), InputPair.second);
    }
    std::sort(Maps.begin(), Maps.end(), [] (const PathMapPair &LHS,
                                            const PathMapPair &RHS) -> bool {
      return LHS.first < RHS.first;
    });
    for (auto &InputPair : Maps) {
      const TypeToPathMap &Map = InputPair.second;
      std::vector<TypePathPair> Pairs;
      Pairs.insert(Pairs.end(), Map.begin(), Map.end());
      std::sort(Pairs.begin(), Pairs.end());
      for (auto &OutputPair : Pairs) {
        printOutputPair(InputPair.first, OutputPair);
      }
    }
  } else {
    for (auto &InputPair : InputToOutputsMap) {
      const TypeToPathMap &Map = InputPair.second;
      for (const TypePathPair &OutputPair : Map) {
        printOutputPair(InputPair.first(), OutputPair);
      }
    }
  }
}

bool OutputFileMap::parse(std::unique_ptr<llvm::MemoryBuffer> Buffer) {
  llvm::SourceMgr SM;
  llvm::yaml::Stream YAMLStream(Buffer->getMemBufferRef(), SM);
  auto I = YAMLStream.begin();
  if (I == YAMLStream.end())
    return true;

  auto Root = I->getRoot();
  if (!Root)
    return true;

  llvm::yaml::MappingNode *Map = dyn_cast<llvm::yaml::MappingNode>(Root);
  if (!Map)
    return true;

  for (auto Pair : *Map) {
    llvm::yaml::Node *Key = Pair.getKey();
    llvm::yaml::Node *Value = Pair.getValue();

    if (!Key)
      return true;

    if (!Value)
      return true;

    llvm::yaml::ScalarNode *InputPath = dyn_cast<llvm::yaml::ScalarNode>(Key);
    if (!InputPath)
      return true;

    llvm::yaml::MappingNode *OutputMapNode =
      dyn_cast<llvm::yaml::MappingNode>(Value);
    if (!OutputMapNode)
      return true;

    TypeToPathMap OutputMap;

    for (auto OutputPair : *OutputMapNode) {
      llvm::yaml::Node *Key = OutputPair.getKey();
      llvm::yaml::Node *Value = OutputPair.getValue();

      llvm::yaml::ScalarNode *KindNode = dyn_cast<llvm::yaml::ScalarNode>(Key);
      if (!KindNode)
        return true;

      llvm::yaml::ScalarNode *Path = dyn_cast<llvm::yaml::ScalarNode>(Value);
      if (!Path)
        return true;

      llvm::SmallString<16> KindStorage;
      types::ID Kind =
        types::lookupTypeForName(KindNode->getValue(KindStorage));

      // Ignore unknown types, so that an older swiftc can be used with a newer
      // build system.
      if (Kind == types::TY_INVALID)
        continue;

      llvm::SmallString<128> PathStorage;
      OutputMap.insert(
        std::pair<types::ID, std::string>(Kind, Path->getValue(PathStorage)));
    }

    llvm::SmallString<128> InputStorage;
    InputToOutputsMap[InputPath->getValue(InputStorage)] = std::move(OutputMap);
  }

  return false;
}
