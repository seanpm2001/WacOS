//===--- SwiftRemoteMirror.cpp - C wrapper for Reflection API -------------===//
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

#include "swift/Reflection/ReflectionContext.h"
#include "swift/Reflection/TypeLowering.h"
#include "swift/Remote/CMemoryReader.h"
#include "swift/Runtime/Unreachable.h"
#include "swift/SwiftRemoteMirror/SwiftRemoteMirror.h"

using namespace swift;
using namespace swift::reflection;
using namespace swift::remote;

using NativeReflectionContext
  = swift::reflection::ReflectionContext<External<RuntimeTarget<sizeof(uintptr_t)>>>;

uint16_t
swift_reflection_getSupportedMetadataVersion() {
  return SWIFT_REFLECTION_METADATA_VERSION;
}

SwiftReflectionContextRef
swift_reflection_createReflectionContext(void *ReaderContext,
                                         PointerSizeFunction getPointerSize,
                                         SizeSizeFunction getSizeSize,
                                         ReadBytesFunction readBytes,
                                         GetStringLengthFunction getStringLength,
                                         GetSymbolAddressFunction getSymbolAddress) {
  MemoryReaderImpl ReaderImpl {
    ReaderContext,
    getPointerSize,
    getSizeSize,
    readBytes,
    getStringLength,
    getSymbolAddress
  };

  auto Reader = std::make_shared<CMemoryReader>(ReaderImpl);
  auto Context
    = new swift::reflection::ReflectionContext<External<RuntimeTarget<sizeof(uintptr_t)>>>(Reader);
  return reinterpret_cast<SwiftReflectionContextRef>(Context);
}

void swift_reflection_destroyReflectionContext(SwiftReflectionContextRef ContextRef) {
  auto Context = reinterpret_cast<swift::reflection::ReflectionContext<InProcess> *>(ContextRef);
  delete Context;
}

void
swift_reflection_addReflectionInfo(SwiftReflectionContextRef ContextRef,
                                   swift_reflection_info_t Info) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  Context->addReflectionInfo(*reinterpret_cast<ReflectionInfo *>(&Info));
}

int
swift_reflection_readIsaMask(SwiftReflectionContextRef ContextRef,
                             uintptr_t *outIsaMask) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto isaMask = Context->readIsaMask();
  *outIsaMask = isaMask.second;
  return isaMask.first;
}

swift_typeref_t
swift_reflection_typeRefForMetadata(SwiftReflectionContextRef ContextRef,
                                    uintptr_t Metadata) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto TR = Context->readTypeFromMetadata(Metadata);
  return reinterpret_cast<swift_typeref_t>(TR);
}

swift_typeref_t
swift_reflection_typeRefForInstance(SwiftReflectionContextRef ContextRef,
                                    uintptr_t Object) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto MetadataAddress = Context->readMetadataFromInstance(Object);
  if (!MetadataAddress.first)
    return 0;
  auto TR = Context->readTypeFromMetadata(MetadataAddress.second);
  return reinterpret_cast<swift_typeref_t>(TR);
}

swift_typeref_t
swift_reflection_typeRefForMangledTypeName(SwiftReflectionContextRef ContextRef,
                                           const char *MangledTypeName,
                                           uint64_t Length) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto TR = Context->readTypeFromMangledName(MangledTypeName, Length);
  return reinterpret_cast<swift_typeref_t>(TR);
}

swift_typeref_t
swift_reflection_genericArgumentOfTypeRef(swift_typeref_t OpaqueTypeRef,
                                          unsigned Index) {
  auto TR = reinterpret_cast<const TypeRef *>(OpaqueTypeRef);

  if (auto BG = dyn_cast<BoundGenericTypeRef>(TR)) {
    auto &Params = BG->getGenericParams();
    assert(Index < Params.size());
    return reinterpret_cast<swift_typeref_t>(Params[Index]);
  }
  return 0;
}

unsigned
swift_reflection_genericArgumentCountOfTypeRef(swift_typeref_t OpaqueTypeRef) {
  auto TR = reinterpret_cast<const TypeRef *>(OpaqueTypeRef);

  if (auto BG = dyn_cast<BoundGenericTypeRef>(TR)) {
    auto &Params = BG->getGenericParams();
    return Params.size();
  }
  return 0;
}

swift_layout_kind_t getTypeInfoKind(const TypeInfo &TI) {
  switch (TI.getKind()) {
  case TypeInfoKind::Builtin: {
    auto &BuiltinTI = cast<BuiltinTypeInfo>(TI);
    if (BuiltinTI.getMangledTypeName() == "Bp")
      return SWIFT_RAW_POINTER;
    return SWIFT_BUILTIN;
  }
  case TypeInfoKind::Record: {
    auto &RecordTI = cast<RecordTypeInfo>(TI);
    switch (RecordTI.getRecordKind()) {
    case RecordKind::Invalid:
      return SWIFT_UNKNOWN;
    case RecordKind::Tuple:
      return SWIFT_TUPLE;
    case RecordKind::Struct:
      return SWIFT_STRUCT;
    case RecordKind::NoPayloadEnum:
      return SWIFT_NO_PAYLOAD_ENUM;
    case RecordKind::SinglePayloadEnum:
      return SWIFT_SINGLE_PAYLOAD_ENUM;
    case RecordKind::MultiPayloadEnum:
      return SWIFT_MULTI_PAYLOAD_ENUM;
    case RecordKind::ThickFunction:
      return SWIFT_THICK_FUNCTION;
    case RecordKind::OpaqueExistential:
      return SWIFT_OPAQUE_EXISTENTIAL;
    case RecordKind::ClassExistential:
      return SWIFT_CLASS_EXISTENTIAL;
    case RecordKind::ErrorExistential:
      return SWIFT_ERROR_EXISTENTIAL;
    case RecordKind::ExistentialMetatype:
      return SWIFT_EXISTENTIAL_METATYPE;
    case RecordKind::ClassInstance:
      return SWIFT_CLASS_INSTANCE;
    case RecordKind::ClosureContext:
      return SWIFT_CLOSURE_CONTEXT;
    }
  }
  case TypeInfoKind::Reference: {
    auto &ReferenceTI = cast<ReferenceTypeInfo>(TI);
    switch (ReferenceTI.getReferenceKind()) {
    case ReferenceKind::Strong:
      return SWIFT_STRONG_REFERENCE;
    case ReferenceKind::Unowned:
      return SWIFT_UNOWNED_REFERENCE;
    case ReferenceKind::Weak:
      return SWIFT_WEAK_REFERENCE;
    case ReferenceKind::Unmanaged:
      return SWIFT_UNMANAGED_REFERENCE;
    }
  }
  }

  swift_runtime_unreachable("Unhandled TypeInfoKind in switch");
}

static swift_typeinfo_t convertTypeInfo(const TypeInfo *TI) {
  if (TI == nullptr) {
    return {
      SWIFT_UNKNOWN,
      0,
      0,
      0,
      0
    };
  }

  unsigned NumFields = 0;
  if (auto *RecordTI = dyn_cast<RecordTypeInfo>(TI))
    NumFields = RecordTI->getNumFields();

  return {
    getTypeInfoKind(*TI),
    TI->getSize(),
    TI->getAlignment(),
    TI->getStride(),
    NumFields
  };
}

static swift_childinfo_t convertChild(const TypeInfo *TI, unsigned Index) {
  auto *RecordTI = cast<RecordTypeInfo>(TI);
  auto &FieldInfo = RecordTI->getFields()[Index];

  return {
    FieldInfo.Name.c_str(),
    FieldInfo.Offset,
    getTypeInfoKind(FieldInfo.TI),
    reinterpret_cast<swift_typeref_t>(FieldInfo.TR),
  };
}

swift_typeinfo_t
swift_reflection_infoForTypeRef(SwiftReflectionContextRef ContextRef,
                                swift_typeref_t OpaqueTypeRef) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto TR = reinterpret_cast<const TypeRef *>(OpaqueTypeRef);
  auto TI = Context->getTypeInfo(TR);
  return convertTypeInfo(TI);
}

swift_childinfo_t
swift_reflection_childOfTypeRef(SwiftReflectionContextRef ContextRef,
                                swift_typeref_t OpaqueTypeRef,
                                unsigned Index) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto TR = reinterpret_cast<const TypeRef *>(OpaqueTypeRef);
  auto *TI = Context->getTypeInfo(TR);
  return convertChild(TI, Index);
}

swift_typeinfo_t
swift_reflection_infoForMetadata(SwiftReflectionContextRef ContextRef,
                                 uintptr_t Metadata) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto *TI = Context->getMetadataTypeInfo(Metadata);
  return convertTypeInfo(TI);
}

swift_childinfo_t
swift_reflection_childOfMetadata(SwiftReflectionContextRef ContextRef,
                                 uintptr_t Metadata,
                                 unsigned Index) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto *TI = Context->getMetadataTypeInfo(Metadata);
  return convertChild(TI, Index);
}

swift_typeinfo_t
swift_reflection_infoForInstance(SwiftReflectionContextRef ContextRef,
                                 uintptr_t Object) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto *TI = Context->getInstanceTypeInfo(Object);
  return convertTypeInfo(TI);
}

swift_childinfo_t
swift_reflection_childOfInstance(SwiftReflectionContextRef ContextRef,
                                 uintptr_t Object,
                                 unsigned Index) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto *TI = Context->getInstanceTypeInfo(Object);
  return convertChild(TI, Index);
}

int swift_reflection_projectExistential(SwiftReflectionContextRef ContextRef,
                                        swift_addr_t ExistentialAddress,
                                        swift_typeref_t ExistentialTypeRef,
                                        swift_typeref_t *InstanceTypeRef,
                                        swift_addr_t *StartOfInstanceData) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto ExistentialTR = reinterpret_cast<const TypeRef *>(ExistentialTypeRef);
  auto RemoteExistentialAddress = RemoteAddress(ExistentialAddress);
  const TypeRef *InstanceTR = nullptr;
  RemoteAddress RemoteStartOfInstanceData(nullptr);
  auto Success = Context->projectExistential(RemoteExistentialAddress,
                                             ExistentialTR,
                                             &InstanceTR,
                                             &RemoteStartOfInstanceData);

  if (Success) {
    *InstanceTypeRef = reinterpret_cast<swift_typeref_t>(InstanceTR);
    *StartOfInstanceData = RemoteStartOfInstanceData.getAddressData();
  }

  return Success;
}

void swift_reflection_dumpTypeRef(swift_typeref_t OpaqueTypeRef) {
  auto TR = reinterpret_cast<const TypeRef *>(OpaqueTypeRef);
  if (TR == nullptr) {
    std::cout << "<null type reference>\n";
  } else {
    TR->dump(std::cout);
  }
}

void swift_reflection_dumpInfoForTypeRef(SwiftReflectionContextRef ContextRef,
                                         swift_typeref_t OpaqueTypeRef) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto TR = reinterpret_cast<const TypeRef *>(OpaqueTypeRef);
  auto TI = Context->getTypeInfo(TR);
  if (TI == nullptr) {
    std::cout << "<null type info>\n";
  } else {
    TI->dump(std::cout);
  }
}

void swift_reflection_dumpInfoForMetadata(SwiftReflectionContextRef ContextRef,
                                          uintptr_t Metadata) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto TI = Context->getMetadataTypeInfo(Metadata);
  if (TI == nullptr) {
    std::cout << "<null type info>\n";
  } else {
    TI->dump(std::cout);
  }
}

void swift_reflection_dumpInfoForInstance(SwiftReflectionContextRef ContextRef,
                                          uintptr_t Object) {
  auto Context = reinterpret_cast<NativeReflectionContext *>(ContextRef);
  auto TI = Context->getInstanceTypeInfo(Object);
  if (TI == nullptr) {
    std::cout << "<null type info>\n";
  } else {
    TI->dump(std::cout);
  }
}

size_t swift_reflection_demangle(const char *MangledName, size_t Length,
                                 char *OutDemangledName, size_t MaxLength) {
  if (MangledName == nullptr || Length == 0)
    return 0;

  std::string Mangled(MangledName, Length);
  auto Demangled = Demangle::demangleTypeAsString(Mangled);
  strncpy(OutDemangledName, Demangled.c_str(), MaxLength);
  return Demangled.size();
}
