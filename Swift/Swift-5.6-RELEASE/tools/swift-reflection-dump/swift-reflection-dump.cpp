//===--- swift-reflection-dump.cpp - Reflection testing application -------===//
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
// This is a host-side tool to dump remote reflection sections in swift
// binaries.
//===----------------------------------------------------------------------===//

#include "swift/ABI/MetadataValues.h"
#include "swift/Basic/LLVMInitialize.h"
#include "swift/Demangling/Demangle.h"
#include "swift/Reflection/ReflectionContext.h"
#include "swift/Reflection/TypeRef.h"
#include "swift/Reflection/TypeRefBuilder.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/RelocationResolver.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#if defined(__APPLE__) && defined(__MACH__)
#include <TargetConditionals.h>
#endif

#include <algorithm>
#include <csignal>
#include <iostream>

using llvm::ArrayRef;
using llvm::dyn_cast;
using llvm::StringRef;
using namespace llvm::object;

using namespace swift;
using namespace swift::reflection;
using namespace swift::remote;
using namespace Demangle;

enum class ActionType { DumpReflectionSections, DumpTypeLowering };

namespace options {
static llvm::cl::opt<ActionType> Action(
    llvm::cl::desc("Mode:"),
    llvm::cl::values(
        clEnumValN(ActionType::DumpReflectionSections,
                   "dump-reflection-sections",
                   "Dump the field reflection section"),
        clEnumValN(
            ActionType::DumpTypeLowering, "dump-type-lowering",
            "Dump the field layout for typeref strings read from stdin")),
    llvm::cl::init(ActionType::DumpReflectionSections));

static llvm::cl::list<std::string>
    BinaryFilename("binary-filename",
                   llvm::cl::desc("Filenames of the binary files"),
                   llvm::cl::OneOrMore);

static llvm::cl::opt<std::string>
    Architecture("arch",
                 llvm::cl::desc("Architecture to inspect in the binary"),
                 llvm::cl::Required);
} // end namespace options

template <typename T> static T unwrap(llvm::Expected<T> value) {
  if (value)
    return std::move(value.get());
  llvm::errs() << "swift-reflection-test error: " << toString(value.takeError())
               << "\n";
  exit(EXIT_FAILURE);
}

using ReadBytesResult = swift::remote::MemoryReader::ReadBytesResult;

// Since ObjectMemoryReader maintains ownership of the ObjectFiles and their
// raw data, we can vend ReadBytesResults with no-op destructors.
static void no_op_destructor(const void*) {}

class Image {
private:
  struct Segment {
    uint64_t Addr;
    StringRef Contents;
  };
  const ObjectFile *O;
  uint64_t HeaderAddress;
  std::vector<Segment> Segments;
  struct DynamicRelocation {
    StringRef Symbol;
    uint64_t Offset;
  };
  llvm::DenseMap<uint64_t, DynamicRelocation> DynamicRelocations;
  
  void scanMachO(const MachOObjectFile *O) {
    using namespace llvm::MachO;

    HeaderAddress = UINT64_MAX;
    
    // Collect the segment preferred vm mappings.
    for (const auto &Load : O->load_commands()) {
      if (Load.C.cmd == LC_SEGMENT_64) {
        auto Seg = O->getSegment64LoadCommand(Load);
        if (Seg.filesize == 0)
          continue;
        
        auto contents = O->getData().slice(Seg.fileoff,
                                           Seg.fileoff + Seg.filesize);
        
        if (contents.empty() || contents.size() != Seg.filesize)
          continue;
        
        Segments.push_back({Seg.vmaddr, contents});
        HeaderAddress = std::min(HeaderAddress, Seg.vmaddr);
      } else if (Load.C.cmd == LC_SEGMENT) {
        auto Seg = O->getSegmentLoadCommand(Load);
        if (Seg.filesize == 0)
          continue;
        
        auto contents = O->getData().slice(Seg.fileoff,
                                           Seg.fileoff + Seg.filesize);
        
        if (contents.empty() || contents.size() != Seg.filesize)
          continue;
        
        Segments.push_back({Seg.vmaddr, contents});
        HeaderAddress = std::min(HeaderAddress, (uint64_t)Seg.vmaddr);
      }
    }
    
    // Walk through the bindings list to collect all the external references
    // in the image.
    llvm::Error error = llvm::Error::success();
    auto OO = const_cast<MachOObjectFile*>(O);

    for (auto bind : OO->bindTable(error)) {
      if (error) {
        llvm::consumeError(std::move(error));
        break;
      }
      
      // The offset from the symbol is stored at the target address.
      uint64_t Offset;
      auto OffsetContent = getContentsAtAddress(bind.address(),
                                                O->getBytesInAddress());
      if (OffsetContent.empty())
        continue;
      
      if (O->getBytesInAddress() == 8) {
        memcpy(&Offset, OffsetContent.data(), sizeof(Offset));
      } else if (O->getBytesInAddress() == 4) {
        uint32_t OffsetValue;
        memcpy(&OffsetValue, OffsetContent.data(), sizeof(OffsetValue));
        Offset = OffsetValue;
      } else {
        assert(false && "unexpected word size?!");
      }
      
      DynamicRelocations.insert({bind.address(), {bind.symbolName(), Offset}});
    }
    if (error) {
      llvm::consumeError(std::move(error));
    }
  }
  
  template<typename ELFT>
  void scanELFType(const ELFObjectFile<ELFT> *O) {
    using namespace llvm::ELF;

    HeaderAddress = UINT64_MAX;

    auto phdrs = O->getELFFile().program_headers();
    if (!phdrs) {
      llvm::consumeError(phdrs.takeError());
    }

    for (auto &ph : *phdrs) {
      if (ph.p_filesz == 0)
        continue;
      
      auto contents = O->getData().slice(ph.p_offset,
                                         ph.p_offset + ph.p_filesz);
      if (contents.empty() || contents.size() != ph.p_filesz)
        continue;
      
      Segments.push_back({ph.p_vaddr, contents});
      HeaderAddress = std::min(HeaderAddress, (uint64_t)ph.p_vaddr);
    }
        
    // Collect the dynamic relocations.
    auto resolver = getRelocationResolver(*O);
    auto resolverSupports = resolver.first;
    auto resolve = resolver.second;
    
    if (!resolverSupports || !resolve)
      return;
    
    auto machine = O->getELFFile().getHeader().e_machine;
    auto relativeRelocType = getELFRelativeRelocationType(machine);
    
    for (auto &S : static_cast<const ELFObjectFileBase*>(O)
                                             ->dynamic_relocation_sections()) {
      bool isRela = O->getSection(S.getRawDataRefImpl())->sh_type
        == llvm::ELF::SHT_RELA;

      for (const RelocationRef &R : S.relocations()) {
        // `getRelocationResolver` doesn't handle RELATIVE relocations, so we
        // have to do that ourselves.
        if (isRela && R.getType() == relativeRelocType) {
          auto rela = O->getRela(R.getRawDataRefImpl());
          DynamicRelocations.insert({R.getOffset(),
            {{}, HeaderAddress + rela->r_addend}});
          continue;
        }
        
        if (!resolverSupports(R.getType()))
          continue;
        auto symbol = R.getSymbol();
        auto name = symbol->getName();
        if (!name) {
          llvm::consumeError(name.takeError());
          continue;
        }
        uint64_t offset = resolve(R.getType(), R.getOffset(), 0, 0, 0);
        DynamicRelocations.insert({R.getOffset(), {*name, offset}});
      }
    }
  }
  
  void scanELF(const ELFObjectFileBase *O) {
    if (auto le32 = dyn_cast<ELFObjectFile<ELF32LE>>(O)) {
      scanELFType(le32);
    } else if (auto be32 = dyn_cast<ELFObjectFile<ELF32BE>>(O)) {
      scanELFType(be32);
    } else if (auto le64 = dyn_cast<ELFObjectFile<ELF64LE>>(O)) {
      scanELFType(le64);
    } else if (auto be64 = dyn_cast<ELFObjectFile<ELF64BE>>(O)) {
      scanELFType(be64);
    } else {
      return;
    }
    
    // FIXME: ReflectionContext tries to read bits of the ELF structure that
    // aren't normally mapped by a phdr. Until that's fixed,
    // allow access to the whole file 1:1 in address space that isn't otherwise
    // mapped.
    Segments.push_back({HeaderAddress, O->getData()});
  }
  
  void scanCOFF(const COFFObjectFile *O) {
    HeaderAddress = O->getImageBase();
    
    for (auto SectionRef : O->sections()) {
      auto Section = O->getCOFFSection(SectionRef);
      
      if (Section->SizeOfRawData == 0)
        continue;
      
      auto SectionBase = O->getImageBase() + Section->VirtualAddress;
      auto SectionContent =
        O->getData().slice(Section->PointerToRawData,
                           Section->PointerToRawData + Section->SizeOfRawData);
      if (SectionContent.empty()
          || SectionContent.size() != Section->SizeOfRawData)
        continue;
      
      Segments.push_back({SectionBase, SectionContent});
    }
    
    // FIXME: We need to map the header at least, but how much of it does
    // Windows typically map?
    Segments.push_back({HeaderAddress, O->getData()});
  }

  bool isMachOWithPtrAuth() const {
    auto macho = dyn_cast<MachOObjectFile>(O);
    if (!macho)
      return false;

    auto &header = macho->getHeader();

    return header.cputype == llvm::MachO::CPU_TYPE_ARM64
      && header.cpusubtype == llvm::MachO::CPU_SUBTYPE_ARM64E;
  }

public:
  explicit Image(const ObjectFile *O) : O(O) {
    // Unfortunately llvm doesn't provide a uniform interface for iterating
    // loadable segments or dynamic relocations in executable images yet.
    if (auto macho = dyn_cast<MachOObjectFile>(O)) {
      scanMachO(macho);
    } else if (auto elf = dyn_cast<ELFObjectFileBase>(O)) {
      scanELF(elf);
    } else if (auto coff = dyn_cast<COFFObjectFile>(O)) {
      scanCOFF(coff);
    } else {
      fputs("unsupported image format\n", stderr);
      abort();
    }
  }

  const ObjectFile *getObjectFile() const { return O; }
  
  unsigned getBytesInAddress() const {
    return O->getBytesInAddress();
  }
    
  uint64_t getStartAddress() const {
    return HeaderAddress;
  }
  
  uint64_t getEndAddress() const {
    uint64_t max = 0;
    for (auto &Segment : Segments) {
      max = std::max(max, Segment.Addr + Segment.Contents.size());
    }
    return max;
  }

  StringRef getContentsAtAddress(uint64_t Addr, uint64_t Size) const {
    for (auto &Segment : Segments) {
      auto addrInSegment = Segment.Addr <= Addr
        && Addr + Size <= Segment.Addr + Segment.Contents.size();
      
      if (!addrInSegment)
        continue;

      auto offset = Addr - Segment.Addr;
      auto result = Segment.Contents.drop_front(offset);
      return result;
    }
    return {};
  }
  
  RemoteAbsolutePointer
  resolvePointer(uint64_t Addr, uint64_t pointerValue) const {
    auto found = DynamicRelocations.find(Addr);
    RemoteAbsolutePointer result;
    if (found == DynamicRelocations.end())
      // In Mach-O images with ptrauth, the pointer value has an offset from
      // the base address in the low 32 bits, and ptrauth discriminator info
      // in the top 32 bits.
      if (isMachOWithPtrAuth()) {
        result = RemoteAbsolutePointer("",
                                HeaderAddress + (pointerValue & 0xffffffffull));
      } else {
        result = RemoteAbsolutePointer("", pointerValue);
      }
    else
      result = RemoteAbsolutePointer(found->second.Symbol,
                                     found->second.Offset);
    return result;
  }
};

/// MemoryReader that reads from the on-disk representation of an executable
/// or dynamic library image.
///
/// This reader uses a remote addressing scheme where the most significant
/// 16 bits of the address value serve as an index into the array of loaded images,
/// and the low 48 bits correspond to the preferred virtual address mapping of
/// the image.
class ObjectMemoryReader : public MemoryReader {
  struct ImageEntry {
    Image TheImage;
    uint64_t Slide;
  };
  std::vector<ImageEntry> Images;
  
  std::pair<const Image *, uint64_t>
  decodeImageIndexAndAddress(uint64_t Addr) const {
    for (auto &Image : Images) {
      if (Image.TheImage.getStartAddress() + Image.Slide <= Addr
          && Addr < Image.TheImage.getEndAddress() + Image.Slide) {
        return {&Image.TheImage, Addr - Image.Slide};
      }
    }
    return {nullptr, 0};
  }
  
  uint64_t
  encodeImageIndexAndAddress(const Image *image, uint64_t imageAddr) const {
    auto entry = (const ImageEntry*)image;
    return imageAddr + entry->Slide;
  }

  StringRef getContentsAtAddress(uint64_t Addr, uint64_t Size) {
    const Image *image;
    uint64_t imageAddr;
    std::tie(image, imageAddr) = decodeImageIndexAndAddress(Addr);

    if (!image)
      return StringRef();
    
    return image->getContentsAtAddress(imageAddr, Size);
  }
  
public:
  explicit ObjectMemoryReader(
      const std::vector<const ObjectFile *> &ObjectFiles) {
    if (ObjectFiles.empty()) {
      fputs("no object files provided\n", stderr);
      abort();
    }
    unsigned WordSize = 0;
    for (const ObjectFile *O : ObjectFiles) {
      // All the object files we look at should share a word size.
      if (!WordSize) {
        WordSize = O->getBytesInAddress();
      } else if (WordSize != O->getBytesInAddress()) {
        fputs("object files must all be for the same architecture\n", stderr);
        abort();
      }
      Images.push_back({Image(O), 0});
    }
    
    // If there is more than one image loaded, try to fit them into one address
    // space.
    if (Images.size() > 1) {
      uint64_t NextAddrSpace = 0;
      for (auto &Image : Images) {
        Image.Slide = NextAddrSpace - Image.TheImage.getStartAddress();
        NextAddrSpace +=
          Image.TheImage.getEndAddress() - Image.TheImage.getStartAddress();
        NextAddrSpace = (NextAddrSpace + 16383) & ~16383;
      }
      
      if (WordSize < 8 && NextAddrSpace > 0xFFFFFFFFu) {
        fputs("object files did not fit in address space", stderr);
        abort();
      }
    }
  }

  ArrayRef<ImageEntry> getImages() const { return Images; }

  bool queryDataLayout(DataLayoutQueryType type, void *inBuffer,
                       void *outBuffer) override {
    auto wordSize = Images.front().TheImage.getBytesInAddress();
    // TODO: The following should be set based on inspecting the image.
    // This code sets it to match the platform this code was compiled for.
#if defined(__APPLE__) && __APPLE__
    auto applePlatform = true;
#else
    auto applePlatform = false;
#endif
#if defined(__APPLE__) && __APPLE__ && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_IOS) && TARGET_OS_WATCH) || (defined(TARGET_OS_TV) && TARGET_OS_TV) || defined(__arm64__))
    auto iosDerivedPlatform = true;
#else
    auto iosDerivedPlatform = false;
#endif

    switch (type) {
    case DLQ_GetPointerSize: {
      auto result = static_cast<uint8_t *>(outBuffer);
      *result = wordSize;
      return true;
    }
    case DLQ_GetSizeSize: {
      auto result = static_cast<uint8_t *>(outBuffer);
      *result = wordSize;
      return true;
    }
    case DLQ_GetPtrAuthMask: {
      // We don't try to sign pointers at all in our view of the object
      // mapping.
      if (wordSize == 4) {
        auto result = static_cast<uint32_t *>(outBuffer);
        *result = (uint32_t)~0ull;
        return true;
      } else if (wordSize == 8) {
        auto result = static_cast<uint64_t *>(outBuffer);
        *result = (uint64_t)~0ull;
        return true;
      }
      return false;
    }
    case DLQ_GetObjCReservedLowBits: {
      auto result = static_cast<uint8_t *>(outBuffer);
      if (applePlatform && !iosDerivedPlatform && wordSize == 8) {
        // Obj-C reserves low bit on 64-bit macOS only.
        // Other Apple platforms don't reserve this bit (even when
        // running on x86_64-based simulators).
        *result = 1;
      } else {
        *result = 0;
      }
      return true;
    }
    case DLQ_GetLeastValidPointerValue: {
      auto result = static_cast<uint64_t *>(outBuffer);
      if (applePlatform && wordSize == 8) {
        // Swift reserves the first 4GiB on 64-bit Apple platforms
        *result = 0x100000000;
      } else {
        // Swift reserves the first 4KiB everywhere else
        *result = 0x1000;
      }
      return true;
    }
    }

    return false;
  }
  
  RemoteAddress getImageStartAddress(unsigned i) const {
    assert(i < Images.size());
    
    return RemoteAddress(
           encodeImageIndexAndAddress(&Images[i].TheImage,
                                      Images[i].TheImage.getStartAddress()));
  }

  // TODO: We could consult the dynamic symbol tables of the images to
  // implement this.
  RemoteAddress getSymbolAddress(const std::string &name) override {
    return RemoteAddress(nullptr);
  }

  ReadBytesResult readBytes(RemoteAddress Addr, uint64_t Size) override {
    auto addrValue = Addr.getAddressData();
    auto resultBuffer = getContentsAtAddress(addrValue, Size);
    return ReadBytesResult(resultBuffer.data(), no_op_destructor);
  }

  bool readString(RemoteAddress Addr, std::string &Dest) override {
    auto addrValue = Addr.getAddressData();
    auto resultBuffer = getContentsAtAddress(addrValue, 1);
    if (resultBuffer.empty())
      return false;
    
    // Make sure there's a null terminator somewhere in the contents.
    unsigned i = 0;
    for (unsigned e = resultBuffer.size(); i < e; ++i) {
      if (resultBuffer[i] == 0)
        goto found_terminator;
    }
    return false;
    
  found_terminator:
    Dest.append(resultBuffer.begin(), resultBuffer.begin() + i);
    return true;
  }
  
  RemoteAbsolutePointer resolvePointer(RemoteAddress Addr,
                                       uint64_t pointerValue) override {
    auto addrValue = Addr.getAddressData();
    const Image *image;
    uint64_t imageAddr;
    std::tie(image, imageAddr) =
      decodeImageIndexAndAddress(addrValue);
    
    if (!image)
      return RemoteAbsolutePointer();
    
    auto resolved = image->resolvePointer(imageAddr, pointerValue);
    
    if (resolved && resolved.isResolved()) {
      // Mix in the image index again to produce a remote address pointing into
      // the same image.
      return RemoteAbsolutePointer("", encodeImageIndexAndAddress(image,
                               resolved.getResolvedAddress().getAddressData()));
    }
    // If the pointer is relative to an unresolved relocation, leave it as is.
    return resolved;
  }
};

using ReflectionContextOwner
  = std::unique_ptr<void, void (*)(void*)>;

struct ReflectionContextHolder {
  ReflectionContextOwner Owner;
  TypeRefBuilder &Builder;
  ObjectMemoryReader &Reader;
};

template <typename Runtime>
static ReflectionContextHolder makeReflectionContextForMetadataReader(
    std::shared_ptr<ObjectMemoryReader> reader) {
  using ReflectionContext = ReflectionContext<Runtime>;
  auto context = new ReflectionContext(reader);
  auto &builder = context->getBuilder();
  for (unsigned i = 0, e = reader->getImages().size(); i < e; ++i) {
    context->addImage(reader->getImageStartAddress(i));
  }
  return {ReflectionContextOwner(
              context, [](void *x) { delete (ReflectionContext *)x; }),
          builder, *reader};
}

static ReflectionContextHolder makeReflectionContextForObjectFiles(
    const std::vector<const ObjectFile *> &objectFiles) {
  auto Reader = std::make_shared<ObjectMemoryReader>(objectFiles);

  uint8_t pointerSize;
  Reader->queryDataLayout(DataLayoutQueryType::DLQ_GetPointerSize,
                          nullptr, &pointerSize);
  
  switch (pointerSize) {
  case 4:
    return makeReflectionContextForMetadataReader<External<RuntimeTarget<4>>>
                                                            (std::move(Reader));
  case 8:
    return makeReflectionContextForMetadataReader<External<RuntimeTarget<8>>>
                                                            (std::move(Reader));
  default:
    fputs("unsupported word size in object file\n", stderr);
    abort();
  }
}

static int doDumpReflectionSections(ArrayRef<std::string> BinaryFilenames,
                                    StringRef Arch, ActionType Action,
                                    FILE *file) {
  // Note: binaryOrError and objectOrError own the memory for our ObjectFile;
  // once they go out of scope, we can no longer do anything.
  std::vector<OwningBinary<Binary>> BinaryOwners;
  std::vector<std::unique_ptr<ObjectFile>> ObjectOwners;
  std::vector<const ObjectFile *> ObjectFiles;

  for (const std::string &BinaryFilename : BinaryFilenames) {
    auto BinaryOwner = unwrap(createBinary(BinaryFilename));
    Binary *BinaryFile = BinaryOwner.getBinary();

    // The object file we are doing lookups in -- either the binary itself, or
    // a particular slice of a universal binary.
    std::unique_ptr<ObjectFile> ObjectOwner;
    const ObjectFile *O = dyn_cast<ObjectFile>(BinaryFile);
    if (!O) {
      auto Universal = cast<MachOUniversalBinary>(BinaryFile);
      ObjectOwner = unwrap(Universal->getMachOObjectForArch(Arch));
      O = ObjectOwner.get();
    }

    // Retain the objects that own section memory
    BinaryOwners.push_back(std::move(BinaryOwner));
    ObjectOwners.push_back(std::move(ObjectOwner));
    ObjectFiles.push_back(O);
  }
  
  auto context = makeReflectionContextForObjectFiles(ObjectFiles);
  auto &builder = context.Builder;

  switch (Action) {
  case ActionType::DumpReflectionSections:
    // Dump everything
    builder.dumpAllSections(file);
    break;
  case ActionType::DumpTypeLowering: {
    for (std::string Line; std::getline(std::cin, Line);) {
      if (Line.empty())
        continue;

      if (StringRef(Line).startswith("//"))
        continue;

      Demangle::Demangler Dem;
      auto Demangled = Dem.demangleType(Line);
      auto Result = swift::Demangle::decodeMangledType(builder, Demangled);
      if (Result.isError()) {
        auto *error = Result.getError();
        char *str = error->copyErrorString();
        fprintf(file, "Invalid typeref:%s - %s\n", Line.c_str(), str);
        error->freeErrorString(str);
        continue;
      }
      auto TypeRef = Result.getType();

      TypeRef->dump(file);
      auto *TypeInfo = builder.getTypeConverter().getTypeInfo(TypeRef, nullptr);
      if (TypeInfo == nullptr) {
        fprintf(file, "Invalid lowering\n");
        continue;
      }
      TypeInfo->dump(file);
    }
    break;
  }
  }

  return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
  PROGRAM_START(argc, argv);
  llvm::cl::ParseCommandLineOptions(argc, argv, "Swift Reflection Dump\n");
  return doDumpReflectionSections(options::BinaryFilename,
                                  options::Architecture, options::Action,
                                  stdout);
}
