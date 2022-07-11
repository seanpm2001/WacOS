//===--- Linking.cpp - Name mangling for IRGen entities -------------------===//
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
//  This file implements name mangling for IRGen entities with linkage.
//
//===----------------------------------------------------------------------===//

#include "swift/IRGen/Linking.h"
#include "IRGenMangler.h"
#include "IRGenModule.h"
#include "swift/AST/ASTMangler.h"
#include "swift/AST/IRGenOptions.h"
#include "swift/ClangImporter/ClangModule.h"
#include "swift/SIL/SILGlobalVariable.h"
#include "swift/SIL/FormalLinkage.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

#include "MetadataRequest.h"

using namespace swift;
using namespace irgen;
using namespace Mangle;

const IRLinkage IRLinkage::InternalLinkOnceODR = {
  llvm::GlobalValue::LinkOnceODRLinkage,
  llvm::GlobalValue::HiddenVisibility,
  llvm::GlobalValue::DefaultStorageClass,
};

const IRLinkage IRLinkage::InternalWeakODR = {
  llvm::GlobalValue::WeakODRLinkage,
  llvm::GlobalValue::HiddenVisibility,
  llvm::GlobalValue::DefaultStorageClass,
};

const IRLinkage IRLinkage::Internal = {
  llvm::GlobalValue::InternalLinkage,
  llvm::GlobalValue::DefaultVisibility,
  llvm::GlobalValue::DefaultStorageClass,
};

const IRLinkage IRLinkage::ExternalCommon = {
  llvm::GlobalValue::CommonLinkage,
  llvm::GlobalValue::DefaultVisibility,
  llvm::GlobalValue::DLLExportStorageClass,
};

const IRLinkage IRLinkage::ExternalImport = {
  llvm::GlobalValue::ExternalLinkage,
  llvm::GlobalValue::DefaultVisibility,
  llvm::GlobalValue::DLLImportStorageClass,
};

const IRLinkage IRLinkage::ExternalWeakImport = {
  llvm::GlobalValue::ExternalWeakLinkage,
  llvm::GlobalValue::DefaultVisibility,
  llvm::GlobalValue::DLLImportStorageClass,
};

const IRLinkage IRLinkage::ExternalExport = {
  llvm::GlobalValue::ExternalLinkage,
  llvm::GlobalValue::DefaultVisibility,
  llvm::GlobalValue::DLLExportStorageClass,
};

bool swift::irgen::useDllStorage(const llvm::Triple &triple) {
  return triple.isOSBinFormatCOFF() && !triple.isOSCygMing();
}

UniversalLinkageInfo::UniversalLinkageInfo(IRGenModule &IGM)
    : UniversalLinkageInfo(IGM.Triple, IGM.IRGen.hasMultipleIGMs(),
                           IGM.IRGen.Opts.ForcePublicLinkage) {}

UniversalLinkageInfo::UniversalLinkageInfo(const llvm::Triple &triple,
                                           bool hasMultipleIGMs,
                                           bool forcePublicDecls)
    : IsELFObject(triple.isOSBinFormatELF()),
      UseDLLStorage(useDllStorage(triple)), HasMultipleIGMs(hasMultipleIGMs),
      ForcePublicDecls(forcePublicDecls) {}

/// Mangle this entity into the given buffer.
void LinkEntity::mangle(SmallVectorImpl<char> &buffer) const {
  llvm::raw_svector_ostream stream(buffer);
  mangle(stream);
}

/// Mangle this entity into the given stream.
void LinkEntity::mangle(raw_ostream &buffer) const {
  std::string Result = mangleAsString();
  buffer.write(Result.data(), Result.size());
}

/// Mangle this entity as a std::string.
std::string LinkEntity::mangleAsString() const {
  IRGenMangler mangler;
  switch (getKind()) {
  case Kind::DispatchThunk: {
    auto *func = cast<FuncDecl>(getDecl());
    return mangler.mangleDispatchThunk(func);
  }

  case Kind::DispatchThunkDerivative: {
    auto *func = cast<AbstractFunctionDecl>(getDecl());
    auto *derivativeId = getAutoDiffDerivativeFunctionIdentifier();
    return mangler.mangleDerivativeDispatchThunk(func, derivativeId);
  }

  case Kind::DispatchThunkInitializer: {
    auto *ctor = cast<ConstructorDecl>(getDecl());
    return mangler.mangleConstructorDispatchThunk(ctor,
                                                  /*isAllocating=*/false);
  }

  case Kind::DispatchThunkAllocator: {
    auto *ctor = cast<ConstructorDecl>(getDecl());
    return mangler.mangleConstructorDispatchThunk(ctor,
                                                  /*isAllocating=*/true);
  }

  case Kind::MethodDescriptor: {
    auto *func = cast<FuncDecl>(getDecl());
    return mangler.mangleMethodDescriptor(func);
  }

  case Kind::MethodDescriptorDerivative: {
    auto *func = cast<AbstractFunctionDecl>(getDecl());
    auto *derivativeId = getAutoDiffDerivativeFunctionIdentifier();
    return mangler.mangleDerivativeMethodDescriptor(func, derivativeId);
  }

  case Kind::MethodDescriptorInitializer: {
    auto *ctor = cast<ConstructorDecl>(getDecl());
    return mangler.mangleConstructorMethodDescriptor(ctor,
                                                     /*isAllocating=*/false);
  }

  case Kind::MethodDescriptorAllocator: {
    auto *ctor = cast<ConstructorDecl>(getDecl());
    return mangler.mangleConstructorMethodDescriptor(ctor,
                                                     /*isAllocating=*/true);
  }

  case Kind::MethodLookupFunction: {
    auto *classDecl = cast<ClassDecl>(getDecl());
    return mangler.mangleMethodLookupFunction(classDecl);
  }

  case Kind::ValueWitness:
    return mangler.mangleValueWitness(getType(), getValueWitness());

  case Kind::ValueWitnessTable:
    return mangler.mangleValueWitnessTable(getType());

  case Kind::TypeMetadataAccessFunction:
    return mangler.mangleTypeMetadataAccessFunction(getType());

  case Kind::CanonicalSpecializedGenericTypeMetadataAccessFunction:
    return mangler.mangleCanonicalSpecializedGenericTypeMetadataAccessFunction(
        getType());

  case Kind::TypeMetadataLazyCacheVariable:
    return mangler.mangleTypeMetadataLazyCacheVariable(getType());

  case Kind::TypeMetadataDemanglingCacheVariable:
    return mangler.mangleTypeMetadataDemanglingCacheVariable(getType());

  case Kind::TypeMetadataInstantiationCache:
    return mangler.mangleTypeMetadataInstantiationCache(
                                            cast<NominalTypeDecl>(getDecl()));

  case Kind::TypeMetadataInstantiationFunction:
    return mangler.mangleTypeMetadataInstantiationFunction(
                                            cast<NominalTypeDecl>(getDecl()));

  case Kind::TypeMetadataSingletonInitializationCache:
    return mangler.mangleTypeMetadataSingletonInitializationCache(
                                            cast<NominalTypeDecl>(getDecl()));

  case Kind::TypeMetadataCompletionFunction:
    return mangler.mangleTypeMetadataCompletionFunction(
                                            cast<NominalTypeDecl>(getDecl()));

  case Kind::TypeMetadata:
    switch (getMetadataAddress()) {
      case TypeMetadataAddress::FullMetadata:
        return mangler.mangleTypeFullMetadataFull(getType());
      case TypeMetadataAddress::AddressPoint:
        return mangler.mangleTypeMetadataFull(getType());
    }
    llvm_unreachable("invalid metadata address");

  case Kind::NoncanonicalSpecializedGenericTypeMetadata:
    return mangler.mangleNoncanonicalTypeMetadata(getType());

  case Kind::CanonicalPrespecializedGenericTypeCachingOnceToken:
    return mangler.mangleCanonicalPrespecializedGenericTypeCachingOnceToken(
        cast<NominalTypeDecl>(getDecl()));

  case Kind::NoncanonicalSpecializedGenericTypeMetadataCacheVariable:
    return mangler.mangleNoncanonicalSpecializedGenericTypeMetadataCache(getType());

  case Kind::TypeMetadataPattern:
    return mangler.mangleTypeMetadataPattern(
                                        cast<NominalTypeDecl>(getDecl()));

  case Kind::SwiftMetaclassStub:
    return mangler.mangleClassMetaClass(cast<ClassDecl>(getDecl()));

  case Kind::CanonicalSpecializedGenericSwiftMetaclassStub:
    return mangler.mangleSpecializedGenericClassMetaClass(getType());

  case Kind::ObjCMetadataUpdateFunction:
    return mangler.mangleObjCMetadataUpdateFunction(cast<ClassDecl>(getDecl()));

  case Kind::ObjCResilientClassStub:
    switch (getMetadataAddress()) {
    case TypeMetadataAddress::FullMetadata:
      return mangler.mangleFullObjCResilientClassStub(cast<ClassDecl>(getDecl()));
    case TypeMetadataAddress::AddressPoint:
      return mangler.mangleObjCResilientClassStub(cast<ClassDecl>(getDecl()));
    }
    llvm_unreachable("invalid metadata address");

  case Kind::ClassMetadataBaseOffset:               // class metadata base offset
    return mangler.mangleClassMetadataBaseOffset(cast<ClassDecl>(getDecl()));

  case Kind::NominalTypeDescriptor:
    return mangler.mangleNominalTypeDescriptor(
                                        cast<NominalTypeDecl>(getDecl()));

  case Kind::NominalTypeDescriptorRecord:
    return mangler.mangleNominalTypeDescriptorRecord(
                                        cast<NominalTypeDecl>(getDecl()));

  case Kind::OpaqueTypeDescriptor:
    return mangler.mangleOpaqueTypeDescriptor(cast<OpaqueTypeDecl>(getDecl()));

  case Kind::OpaqueTypeDescriptorRecord:
    return mangler.mangleOpaqueTypeDescriptorRecord(
        cast<OpaqueTypeDecl>(getDecl()));

  case Kind::OpaqueTypeDescriptorAccessor:
    return mangler.mangleOpaqueTypeDescriptorAccessor(
        cast<OpaqueTypeDecl>(getDecl()));

  case Kind::OpaqueTypeDescriptorAccessorImpl:
    return mangler.mangleOpaqueTypeDescriptorAccessorImpl(
        cast<OpaqueTypeDecl>(getDecl()));

  case Kind::OpaqueTypeDescriptorAccessorKey:
    return mangler.mangleOpaqueTypeDescriptorAccessorKey(
        cast<OpaqueTypeDecl>(getDecl()));

  case Kind::OpaqueTypeDescriptorAccessorVar:
    return mangler.mangleOpaqueTypeDescriptorAccessorVar(
        cast<OpaqueTypeDecl>(getDecl()));

  case Kind::PropertyDescriptor:
    return mangler.manglePropertyDescriptor(
                                        cast<AbstractStorageDecl>(getDecl()));

  case Kind::ModuleDescriptor:
    return mangler.mangleModuleDescriptor(cast<ModuleDecl>(getDecl()));
  
  case Kind::ExtensionDescriptor:
    return mangler.mangleExtensionDescriptor(getExtension());

  case Kind::AnonymousDescriptor:
    return mangler.mangleAnonymousDescriptor(getAnonymousDeclContext());

  case Kind::ProtocolDescriptor:
    return mangler.mangleProtocolDescriptor(cast<ProtocolDecl>(getDecl()));

  case Kind::ProtocolDescriptorRecord:
    return mangler.mangleProtocolDescriptorRecord(cast<ProtocolDecl>(getDecl()));

  case Kind::ProtocolRequirementsBaseDescriptor:
    return mangler.mangleProtocolRequirementsBaseDescriptor(
                                                 cast<ProtocolDecl>(getDecl()));

  case Kind::AssociatedTypeDescriptor:
    return mangler.mangleAssociatedTypeDescriptor(
                                          cast<AssociatedTypeDecl>(getDecl()));

  case Kind::AssociatedConformanceDescriptor: {
    auto assocConformance = getAssociatedConformance();
    return mangler.mangleAssociatedConformanceDescriptor(
             cast<ProtocolDecl>(getDecl()),
             assocConformance.first,
             assocConformance.second);
  }

  case Kind::BaseConformanceDescriptor: {
    auto assocConformance = getAssociatedConformance();
    return mangler.mangleBaseConformanceDescriptor(
             cast<ProtocolDecl>(getDecl()),
             assocConformance.second);
  }

  case Kind::DefaultAssociatedConformanceAccessor: {
    auto assocConformance = getAssociatedConformance();
    return mangler.mangleDefaultAssociatedConformanceAccessor(
             cast<ProtocolDecl>(getDecl()),
             assocConformance.first,
             assocConformance.second);
  }

  case Kind::ProtocolConformanceDescriptor:
    return mangler.mangleProtocolConformanceDescriptor(
                                                  getRootProtocolConformance());

  case Kind::ProtocolConformanceDescriptorRecord:
    return mangler.mangleProtocolConformanceDescriptorRecord(
                                                  getRootProtocolConformance());

  case Kind::EnumCase:
    return mangler.mangleEnumCase(getDecl());

  case Kind::FieldOffset:
    return mangler.mangleFieldOffset(getDecl());

  case Kind::ProtocolWitnessTable:
    return mangler.mangleWitnessTable(getRootProtocolConformance());

  case Kind::GenericProtocolWitnessTableInstantiationFunction:
    return mangler.mangleGenericProtocolWitnessTableInstantiationFunction(
                                                    getProtocolConformance());

  case Kind::ProtocolWitnessTablePattern:
    return mangler.mangleProtocolWitnessTablePattern(getProtocolConformance());

  case Kind::ProtocolWitnessTableLazyAccessFunction:
    return mangler.mangleProtocolWitnessTableLazyAccessFunction(getType(),
                                                    getProtocolConformance());

  case Kind::ProtocolWitnessTableLazyCacheVariable:
    return mangler.mangleProtocolWitnessTableLazyCacheVariable(getType(),
                                                    getProtocolConformance());

  case Kind::AssociatedTypeWitnessTableAccessFunction: {
    auto assocConf = getAssociatedConformance();
    if (isa<GenericTypeParamType>(assocConf.first)) {
      return mangler.mangleBaseWitnessTableAccessFunction(
                  getProtocolConformance(), assocConf.second);
    }
    
    return mangler.mangleAssociatedTypeWitnessTableAccessFunction(
                getProtocolConformance(), assocConf.first, assocConf.second);
  }

  case Kind::CoroutineContinuationPrototype:
    return mangler.mangleCoroutineContinuationPrototype(
                                            cast<SILFunctionType>(getType()));

    // An Objective-C class reference reference. The symbol is private, so
    // the mangling is unimportant; it should just be readable in LLVM IR.
  case Kind::ObjCClassRef: {
    llvm::SmallString<64> tempBuffer;
    StringRef name = cast<ClassDecl>(getDecl())->getObjCRuntimeName(tempBuffer);
    std::string Result("OBJC_CLASS_REF_$_");
    Result.append(name.data(), name.size());
    return Result;
  }

    // An Objective-C class reference;  not a swift mangling.
  case Kind::ObjCClass: {
    llvm::SmallString<64> TempBuffer;
    StringRef Name = cast<ClassDecl>(getDecl())->getObjCRuntimeName(TempBuffer);
    std::string Result("OBJC_CLASS_$_");
    Result.append(Name.data(), Name.size());
    return Result;
  }

    // An Objective-C metaclass reference;  not a swift mangling.
  case Kind::ObjCMetaclass: {
    llvm::SmallString<64> TempBuffer;
    StringRef Name = cast<ClassDecl>(getDecl())->getObjCRuntimeName(TempBuffer);
    std::string Result("OBJC_METACLASS_$_");
    Result.append(Name.data(), Name.size());
    return Result;
  }

  case Kind::SILFunction: {
    std::string Result(getSILFunction()->getName());
    if (isDynamicallyReplaceable()) {
      Result.append("TI");
    }
    return Result;
  }
  case Kind::DynamicallyReplaceableFunctionImpl: {
    assert(isa<AbstractFunctionDecl>(getDecl()));
    std::string Result;
    if (auto *Constructor = dyn_cast<ConstructorDecl>(getDecl())) {
      Result = mangler.mangleConstructorEntity(Constructor, isAllocator());
    } else  {
      Result = mangler.mangleEntity(getDecl());
    }
    Result.append("TI");
    return Result;
  }

  case Kind::DynamicallyReplaceableFunctionVariable: {
    std::string Result(getSILFunction()->getName());
    Result.append("TX");
    return Result;
  }

  case Kind::DynamicallyReplaceableFunctionKey: {
    std::string Result(getSILFunction()->getName());
    Result.append("Tx");
    return Result;
  }


  case Kind::DynamicallyReplaceableFunctionVariableAST: {
    assert(isa<AbstractFunctionDecl>(getDecl()));
    std::string Result;
    if (auto *Constructor = dyn_cast<ConstructorDecl>(getDecl())) {
      Result =
          mangler.mangleConstructorEntity(Constructor, isAllocator());
    } else  {
      Result = mangler.mangleEntity(getDecl());
    }
    Result.append("TX");
    return Result;
  }

  case Kind::DynamicallyReplaceableFunctionKeyAST: {
    assert(isa<AbstractFunctionDecl>(getDecl()));
    std::string Result;
    if (auto *Constructor = dyn_cast<ConstructorDecl>(getDecl())) {
      Result =
          mangler.mangleConstructorEntity(Constructor, isAllocator());
    } else  {
      Result = mangler.mangleEntity(getDecl());
    }
    Result.append("Tx");
    return Result;
  }

  case Kind::SILGlobalVariable:
    return getSILGlobalVariable()->getName().str();

  case Kind::ReflectionBuiltinDescriptor:
    return mangler.mangleReflectionBuiltinDescriptor(getType());
  case Kind::ReflectionFieldDescriptor:
    return mangler.mangleReflectionFieldDescriptor(getType());
  case Kind::ReflectionAssociatedTypeDescriptor:
    return mangler.mangleReflectionAssociatedTypeDescriptor(
                                                    getProtocolConformance());
  case Kind::DifferentiabilityWitness:
    return mangler.mangleSILDifferentiabilityWitness(
        getSILDifferentiabilityWitness()->getOriginalFunction()->getName(),
        getSILDifferentiabilityWitness()->getKind(),
        getSILDifferentiabilityWitness()->getConfig());

  case Kind::AsyncFunctionPointer:
  case Kind::DispatchThunkAsyncFunctionPointer:
  case Kind::DispatchThunkInitializerAsyncFunctionPointer:
  case Kind::DispatchThunkAllocatorAsyncFunctionPointer:
  case Kind::PartialApplyForwarderAsyncFunctionPointer: {
    std::string Result(getUnderlyingEntityForAsyncFunctionPointer()
        .mangleAsString());
    Result.append("Tu");
    return Result;
  }
  case Kind::DistributedThunkAsyncFunctionPointer: {
    std::string Result = getSILDeclRef().mangle();
    Result.append("TE");
    Result.append("Tu");
    return Result;
  }
  case Kind::KnownAsyncFunctionPointer: {
    std::string Result(static_cast<char *>(Pointer));
    Result.append("Tu");
    return Result;
  }

  case Kind::AsyncFunctionPointerAST: {
    std::string Result = getSILDeclRef().mangle();
    Result.append("Tu");
    return Result;
  }
  case Kind::PartialApplyForwarder:
    std::string Result;
    Result = std::string(static_cast<llvm::Function *>(Pointer)->getName());
    return Result;
  }
  llvm_unreachable("bad entity kind!");
}

SILDeclRef LinkEntity::getSILDeclRef() const {
  assert(getKind() == Kind::DistributedThunkAsyncFunctionPointer ||
         getKind() == Kind::AsyncFunctionPointerAST);

  return SILDeclRef(const_cast<ValueDecl *>(getDecl()),
             static_cast<SILDeclRef::Kind>(
                 reinterpret_cast<uintptr_t>(SecondaryPointer)));
}

SILLinkage LinkEntity::getLinkage(ForDefinition_t forDefinition) const {
  // For when `this` is a protocol conformance of some kind.
  auto getLinkageAsConformance = [&] {
    return getLinkageForProtocolConformance(
        getProtocolConformance()->getRootConformance(), forDefinition);
  };

  switch (getKind()) {
  case Kind::DispatchThunk:
  case Kind::DispatchThunkDerivative:
  case Kind::DispatchThunkInitializer:
  case Kind::DispatchThunkAllocator:
  case Kind::MethodDescriptor:
  case Kind::MethodDescriptorDerivative:
  case Kind::MethodDescriptorInitializer:
  case Kind::MethodDescriptorAllocator: {
    auto *decl = getDecl();

    // Protocol requirements don't have their own access control
    if (auto *proto = dyn_cast<ProtocolDecl>(decl->getDeclContext()))
      decl = proto;

    return getSILLinkage(getDeclLinkage(decl), forDefinition);
  }

  // Most type metadata depend on the formal linkage of their type.
  case Kind::ValueWitnessTable: {
    auto type = getType();

    // Builtin types, (), () -> () and so on are in the runtime.
    if (!type.getAnyNominal())
      return getSILLinkage(FormalLinkage::PublicUnique, forDefinition);

    // Imported types.
    if (isAccessorLazilyGenerated(getTypeMetadataAccessStrategy(type)))
      return SILLinkage::Shared;

    // Everything else is only referenced inside its module.
    return SILLinkage::Private;
  }

  case Kind::ObjCMetadataUpdateFunction:
  case Kind::TypeMetadataInstantiationCache:
  case Kind::TypeMetadataInstantiationFunction:
  case Kind::TypeMetadataSingletonInitializationCache:
  case Kind::TypeMetadataCompletionFunction:
  case Kind::TypeMetadataPattern:
    return SILLinkage::Private;

  case Kind::TypeMetadataLazyCacheVariable: {
    auto type = getType();

    // Imported types, non-primitive structural types.
    if (isAccessorLazilyGenerated(getTypeMetadataAccessStrategy(type)))
      return SILLinkage::Shared;

    // Everything else is only referenced inside its module.
    return SILLinkage::Private;
  }
      
  case Kind::TypeMetadataDemanglingCacheVariable:
    return SILLinkage::Shared;

  case Kind::TypeMetadata: {
    auto *nominal = getType().getAnyNominal();
    switch (getMetadataAddress()) {
    case TypeMetadataAddress::FullMetadata:
      // For imported types, the full metadata object is a candidate
      // for uniquing.
      if (getDeclLinkage(nominal) == FormalLinkage::PublicNonUnique)
        return SILLinkage::Shared;

      // Prespecialization of the same generic metadata may be requested 
      // multiple times within the same module, so it needs to be uniqued.
      if (nominal->isGenericContext())
        return SILLinkage::Shared;

      // The full metadata object is private to the containing module.
      return SILLinkage::Private;
    case TypeMetadataAddress::AddressPoint: {
      return getSILLinkage(nominal
                           ? getDeclLinkage(nominal)
                           : FormalLinkage::PublicUnique,
                           forDefinition);
    }
    }
    llvm_unreachable("bad kind");
  }

  case Kind::NoncanonicalSpecializedGenericTypeMetadata:
  case Kind::NoncanonicalSpecializedGenericTypeMetadataCacheVariable:
    // Prespecialization of the same non-canonical generic metadata may be
    // requested multiple times, so it needs to be uniqued.
    return SILLinkage::Shared;

  // ...but we don't actually expose individual value witnesses (right now).
  case Kind::ValueWitness: {
    auto *nominal = getType().getAnyNominal();
    if (getDeclLinkage(nominal) == FormalLinkage::PublicNonUnique)
      return SILLinkage::Shared;
    assert(forDefinition);
    return SILLinkage::Private;
  }

  case Kind::TypeMetadataAccessFunction:
    switch (getTypeMetadataAccessStrategy(getType())) {
    case MetadataAccessStrategy::PublicUniqueAccessor:
      return getSILLinkage(FormalLinkage::PublicUnique, forDefinition);
    case MetadataAccessStrategy::HiddenUniqueAccessor:
      return getSILLinkage(FormalLinkage::HiddenUnique, forDefinition);
    case MetadataAccessStrategy::PrivateAccessor:
      return getSILLinkage(FormalLinkage::Private, forDefinition);
    case MetadataAccessStrategy::ForeignAccessor:
    case MetadataAccessStrategy::NonUniqueAccessor:
      return SILLinkage::Shared;
    }
    llvm_unreachable("bad metadata access kind");

  case Kind::CanonicalSpecializedGenericTypeMetadataAccessFunction:
    return SILLinkage::Shared;

  case Kind::ObjCClassRef:
    return SILLinkage::Private;

  // Continuation prototypes need to be external or else LLVM will fret.
  case Kind::CoroutineContinuationPrototype:
    return SILLinkage::PublicExternal;

  case Kind::ObjCResilientClassStub: {
    switch (getMetadataAddress()) {
    case TypeMetadataAddress::FullMetadata:
      // The full class stub object is private to the containing module,
      // excpet for foreign types.
      return SILLinkage::Private;
    case TypeMetadataAddress::AddressPoint: {
      auto *classDecl = cast<ClassDecl>(getDecl());
      return getSILLinkage(getDeclLinkage(classDecl),
                           forDefinition);
    }
    }
    llvm_unreachable("invalid metadata address");
  }

  case Kind::EnumCase: {
    auto *elementDecl = cast<EnumElementDecl>(getDecl());
    return getSILLinkage(getDeclLinkage(elementDecl), forDefinition);
  }

  case Kind::FieldOffset: {
    auto *varDecl = cast<VarDecl>(getDecl());

    auto linkage = getDeclLinkage(varDecl);

    // Classes with resilient storage don't expose field offset symbols.
    if (cast<ClassDecl>(varDecl->getDeclContext())->isResilient()) {
      assert(linkage != FormalLinkage::PublicNonUnique &&
            "Cannot have a resilient class with non-unique linkage");

      if (linkage == FormalLinkage::PublicUnique)
        linkage = FormalLinkage::HiddenUnique;
    }

    return getSILLinkage(linkage, forDefinition);
  }

  case Kind::PropertyDescriptor: {
    // Return the linkage of the getter, which may be more permissive than the
    // property itself (for instance, with a private/internal property whose
    // accessor is @inlinable or @usableFromInline)
    auto getterDecl = cast<AbstractStorageDecl>(getDecl())
      ->getOpaqueAccessor(AccessorKind::Get);
    return getSILLinkage(getDeclLinkage(getterDecl), forDefinition);
  }

  case Kind::AssociatedConformanceDescriptor:
  case Kind::BaseConformanceDescriptor:
  case Kind::ObjCClass:
  case Kind::ObjCMetaclass:
  case Kind::SwiftMetaclassStub:
  case Kind::NominalTypeDescriptor:
  case Kind::NominalTypeDescriptorRecord:
  case Kind::ClassMetadataBaseOffset:
  case Kind::ProtocolDescriptor:
  case Kind::ProtocolDescriptorRecord:
  case Kind::ProtocolRequirementsBaseDescriptor:
  case Kind::MethodLookupFunction:
  case Kind::OpaqueTypeDescriptor:
  case Kind::OpaqueTypeDescriptorRecord:
  case Kind::OpaqueTypeDescriptorAccessor:
  case Kind::OpaqueTypeDescriptorAccessorImpl:
  case Kind::OpaqueTypeDescriptorAccessorKey:
  case Kind::OpaqueTypeDescriptorAccessorVar:
    return getSILLinkage(getDeclLinkage(getDecl()), forDefinition);

  case Kind::CanonicalSpecializedGenericSwiftMetaclassStub:
    // Prespecialization of the same generic class' metaclass may be requested
    // multiple times within the same module, so it needs to be uniqued.
    return SILLinkage::Shared;

  case Kind::AssociatedTypeDescriptor:
    return getSILLinkage(getDeclLinkage(getAssociatedType()->getProtocol()),
                         forDefinition);

  case Kind::ProtocolWitnessTable:
  case Kind::ProtocolConformanceDescriptor:
  case Kind::ProtocolConformanceDescriptorRecord:
    return getLinkageForProtocolConformance(getRootProtocolConformance(),
                                            forDefinition);

  case Kind::ProtocolWitnessTablePattern:
    if (getLinkageAsConformance() == SILLinkage::Shared)
      return SILLinkage::Shared;
    return SILLinkage::Private;

  case Kind::ProtocolWitnessTableLazyAccessFunction:
  case Kind::ProtocolWitnessTableLazyCacheVariable: {
    auto *nominal = getType().getAnyNominal();
    assert(nominal);
    if (getDeclLinkage(nominal) == FormalLinkage::Private ||
        getLinkageAsConformance() == SILLinkage::Private) {
      return SILLinkage::Private;
    } else {
      return SILLinkage::Shared;
    }
  }

  case Kind::AssociatedTypeWitnessTableAccessFunction:
  case Kind::DefaultAssociatedConformanceAccessor:
  case Kind::GenericProtocolWitnessTableInstantiationFunction:
  case Kind::CanonicalPrespecializedGenericTypeCachingOnceToken:
    return SILLinkage::Private;

  case Kind::DynamicallyReplaceableFunctionKey:
    return getSILFunction()->getLinkage();

  case Kind::SILFunction:
    return getSILFunction()->getEffectiveSymbolLinkage();

  case Kind::AsyncFunctionPointerAST:
  case Kind::DistributedThunkAsyncFunctionPointer:
    return getSILLinkage(getDeclLinkage(getDecl()), forDefinition);

  case Kind::DynamicallyReplaceableFunctionImpl:
  case Kind::DynamicallyReplaceableFunctionKeyAST:
    return getSILLinkage(getDeclLinkage(getDecl()), forDefinition);


  case Kind::DynamicallyReplaceableFunctionVariable:
    return getSILFunction()->getEffectiveSymbolLinkage();
  case Kind::DynamicallyReplaceableFunctionVariableAST:
    return getSILLinkage(getDeclLinkage(getDecl()), forDefinition);

  case Kind::SILGlobalVariable:
    return getSILGlobalVariable()->getLinkage();

  case Kind::ReflectionBuiltinDescriptor:
  case Kind::ReflectionFieldDescriptor: {
    // Reflection descriptors for imported types have shared linkage,
    // since we may emit them in other TUs in the same module.
    if (auto *nominal = getType().getAnyNominal())
      if (getDeclLinkage(nominal) == FormalLinkage::PublicNonUnique)
        return SILLinkage::Shared;
    return SILLinkage::Private;
  }
  case Kind::ReflectionAssociatedTypeDescriptor:
    if (getLinkageAsConformance() == SILLinkage::Shared)
      return SILLinkage::Shared;
    return SILLinkage::Private;

  case Kind::ModuleDescriptor:
  case Kind::ExtensionDescriptor:
  case Kind::AnonymousDescriptor:
    return SILLinkage::Shared;
  case Kind::DifferentiabilityWitness:
    return getSILDifferentiabilityWitness()->getLinkage();

  case Kind::AsyncFunctionPointer:
  case Kind::DispatchThunkAsyncFunctionPointer:
  case Kind::DispatchThunkInitializerAsyncFunctionPointer:
  case Kind::DispatchThunkAllocatorAsyncFunctionPointer:
  case Kind::PartialApplyForwarderAsyncFunctionPointer:
    return getUnderlyingEntityForAsyncFunctionPointer()
        .getLinkage(forDefinition);
  case Kind::KnownAsyncFunctionPointer:
    return SILLinkage::PublicExternal;
  case Kind::PartialApplyForwarder:
    return SILLinkage::Private;
  }
  llvm_unreachable("bad link entity kind");
}

bool LinkEntity::isContextDescriptor() const {
  switch (getKind()) {
  case Kind::ModuleDescriptor:
  case Kind::ExtensionDescriptor:
  case Kind::AnonymousDescriptor:
  case Kind::NominalTypeDescriptor:
  case Kind::ProtocolDescriptor:
  case Kind::OpaqueTypeDescriptor:
    return true;
  case Kind::AsyncFunctionPointer:
  case Kind::AsyncFunctionPointerAST:
  case Kind::DistributedThunkAsyncFunctionPointer:
  case Kind::PropertyDescriptor:
  case Kind::DispatchThunk:
  case Kind::DispatchThunkDerivative:
  case Kind::DispatchThunkInitializer:
  case Kind::DispatchThunkAllocator:
  case Kind::DispatchThunkAsyncFunctionPointer:
  case Kind::DispatchThunkInitializerAsyncFunctionPointer:
  case Kind::DispatchThunkAllocatorAsyncFunctionPointer:
  case Kind::PartialApplyForwarderAsyncFunctionPointer:
  case Kind::MethodDescriptor:
  case Kind::MethodDescriptorDerivative:
  case Kind::MethodDescriptorInitializer:
  case Kind::MethodDescriptorAllocator:
  case Kind::MethodLookupFunction:
  case Kind::EnumCase:
  case Kind::FieldOffset:
  case Kind::ObjCClass:
  case Kind::ObjCClassRef:
  case Kind::ObjCMetaclass:
  case Kind::ObjCMetadataUpdateFunction:
  case Kind::ObjCResilientClassStub:
  case Kind::SwiftMetaclassStub:
  case Kind::ClassMetadataBaseOffset:
  case Kind::TypeMetadataPattern:
  case Kind::TypeMetadataInstantiationCache:
  case Kind::TypeMetadataInstantiationFunction:
  case Kind::TypeMetadataSingletonInitializationCache:
  case Kind::TypeMetadataCompletionFunction:
  case Kind::NominalTypeDescriptorRecord:
  case Kind::OpaqueTypeDescriptorRecord:
  case Kind::ProtocolDescriptorRecord:
  case Kind::ProtocolRequirementsBaseDescriptor:
  case Kind::AssociatedTypeDescriptor:
  case Kind::AssociatedConformanceDescriptor:
  case Kind::BaseConformanceDescriptor:
  case Kind::DefaultAssociatedConformanceAccessor:
  case Kind::SILFunction:
  case Kind::SILGlobalVariable:
  case Kind::ProtocolWitnessTable:
  case Kind::ProtocolWitnessTablePattern:
  case Kind::GenericProtocolWitnessTableInstantiationFunction:
  case Kind::AssociatedTypeWitnessTableAccessFunction:
  case Kind::ReflectionAssociatedTypeDescriptor:
  case Kind::ProtocolConformanceDescriptor:
  case Kind::ProtocolConformanceDescriptorRecord:
  case Kind::ProtocolWitnessTableLazyAccessFunction:
  case Kind::ProtocolWitnessTableLazyCacheVariable:
  case Kind::ValueWitness:
  case Kind::ValueWitnessTable:
  case Kind::TypeMetadata:
  case Kind::TypeMetadataAccessFunction:
  case Kind::CanonicalSpecializedGenericTypeMetadataAccessFunction:
  case Kind::TypeMetadataLazyCacheVariable:
  case Kind::TypeMetadataDemanglingCacheVariable:
  case Kind::ReflectionBuiltinDescriptor:
  case Kind::ReflectionFieldDescriptor:
  case Kind::CoroutineContinuationPrototype:
  case Kind::DynamicallyReplaceableFunctionVariableAST:
  case Kind::DynamicallyReplaceableFunctionKeyAST:
  case Kind::DynamicallyReplaceableFunctionImpl:
  case Kind::DynamicallyReplaceableFunctionKey:
  case Kind::DynamicallyReplaceableFunctionVariable:
  case Kind::OpaqueTypeDescriptorAccessor:
  case Kind::OpaqueTypeDescriptorAccessorImpl:
  case Kind::OpaqueTypeDescriptorAccessorKey:
  case Kind::OpaqueTypeDescriptorAccessorVar:
  case Kind::DifferentiabilityWitness:
  case Kind::CanonicalSpecializedGenericSwiftMetaclassStub:
  case Kind::NoncanonicalSpecializedGenericTypeMetadata:
  case Kind::NoncanonicalSpecializedGenericTypeMetadataCacheVariable:
  case Kind::CanonicalPrespecializedGenericTypeCachingOnceToken:
  case Kind::PartialApplyForwarder:
  case Kind::KnownAsyncFunctionPointer:
    return false;
  }
  llvm_unreachable("invalid descriptor");
}

llvm::Type *LinkEntity::getDefaultDeclarationType(IRGenModule &IGM) const {
  switch (getKind()) {
  case Kind::ModuleDescriptor:
  case Kind::ExtensionDescriptor:
  case Kind::AnonymousDescriptor:
  case Kind::NominalTypeDescriptor:
  case Kind::NominalTypeDescriptorRecord:
  case Kind::PropertyDescriptor:
    return IGM.TypeContextDescriptorTy;
  case Kind::OpaqueTypeDescriptor:
  case Kind::OpaqueTypeDescriptorRecord:
    return IGM.OpaqueTypeDescriptorTy;
  case Kind::ProtocolDescriptor:
    return IGM.ProtocolDescriptorStructTy;
  case Kind::AssociatedTypeDescriptor:
  case Kind::AssociatedConformanceDescriptor:
  case Kind::BaseConformanceDescriptor:
  case Kind::ProtocolDescriptorRecord:
  case Kind::ProtocolRequirementsBaseDescriptor:
    return IGM.ProtocolRequirementStructTy;
  case Kind::ProtocolConformanceDescriptor:
  case Kind::ProtocolConformanceDescriptorRecord:
    return IGM.ProtocolConformanceDescriptorTy;
  case Kind::ObjCClassRef:
    return IGM.ObjCClassPtrTy;
  case Kind::ObjCClass:
  case Kind::ObjCMetaclass:
  case Kind::SwiftMetaclassStub:
  case Kind::CanonicalSpecializedGenericSwiftMetaclassStub:
    return IGM.ObjCClassStructTy;
  case Kind::TypeMetadataLazyCacheVariable:
  case Kind::NoncanonicalSpecializedGenericTypeMetadataCacheVariable:
    return IGM.TypeMetadataPtrTy;
  case Kind::TypeMetadataDemanglingCacheVariable:
    return llvm::StructType::get(IGM.Int32Ty, IGM.Int32Ty);
  case Kind::TypeMetadataSingletonInitializationCache:
    // TODO: put a cache variable on IGM
    return llvm::StructType::get(IGM.getLLVMContext(),
                                 {IGM.TypeMetadataPtrTy, IGM.Int8PtrTy});
  case Kind::TypeMetadata:
  case Kind::NoncanonicalSpecializedGenericTypeMetadata:
    switch (getMetadataAddress()) {
    case TypeMetadataAddress::FullMetadata:
      if (getType().getClassOrBoundGenericClass())
        return IGM.FullHeapMetadataStructTy;
      else
        return IGM.FullTypeMetadataStructTy;
    case TypeMetadataAddress::AddressPoint:
      return IGM.TypeMetadataStructTy;
    }
    llvm_unreachable("invalid metadata address");
    
  case Kind::TypeMetadataPattern:
    // TODO: Use a real type?
    return IGM.Int8Ty;
    
  case Kind::ClassMetadataBaseOffset:
    // TODO: put a cache variable on IGM
    return llvm::StructType::get(IGM.getLLVMContext(), {
      IGM.SizeTy,  // Immediate members offset
      IGM.Int32Ty, // Negative size in words
      IGM.Int32Ty  // Positive size in words
    });
    
  case Kind::TypeMetadataInstantiationCache:
    // TODO: put a cache variable on IGM
    return llvm::ArrayType::get(IGM.Int8PtrTy,
                                NumGenericMetadataPrivateDataWords);
  case Kind::ReflectionBuiltinDescriptor:
  case Kind::ReflectionFieldDescriptor:
  case Kind::ReflectionAssociatedTypeDescriptor:
    return IGM.FieldDescriptorTy;
  case Kind::ValueWitnessTable: // TODO: use ValueWitnessTableTy
  case Kind::ProtocolWitnessTable:
  case Kind::ProtocolWitnessTablePattern:
    return IGM.WitnessTableTy;
  case Kind::FieldOffset:
    return IGM.SizeTy;
  case Kind::EnumCase:
    return IGM.Int32Ty;
  case Kind::ProtocolWitnessTableLazyCacheVariable:
    return IGM.WitnessTablePtrTy;
  case Kind::SILFunction:
    return IGM.FunctionPtrTy->getPointerTo();
  case Kind::MethodDescriptor:
  case Kind::MethodDescriptorInitializer:
  case Kind::MethodDescriptorAllocator:
  case Kind::MethodDescriptorDerivative:
    return IGM.MethodDescriptorStructTy;
  case Kind::DynamicallyReplaceableFunctionKey:
  case Kind::OpaqueTypeDescriptorAccessorKey:
    return IGM.DynamicReplacementKeyTy;
  case Kind::DynamicallyReplaceableFunctionVariable:
  case Kind::OpaqueTypeDescriptorAccessorVar:
    return IGM.DynamicReplacementLinkEntryTy;
  case Kind::ObjCMetadataUpdateFunction:
    return IGM.ObjCUpdateCallbackTy;
  case Kind::ObjCResilientClassStub:
    switch (getMetadataAddress()) {
    case TypeMetadataAddress::FullMetadata:
      return IGM.ObjCFullResilientClassStubTy;
    case TypeMetadataAddress::AddressPoint:
      return IGM.ObjCResilientClassStubTy;
    }
    llvm_unreachable("invalid metadata address");
  case Kind::DifferentiabilityWitness:
    return IGM.DifferentiabilityWitnessTy;
  case Kind::CanonicalPrespecializedGenericTypeCachingOnceToken:
    return IGM.OnceTy;
  case Kind::AsyncFunctionPointer:
  case Kind::DispatchThunkAsyncFunctionPointer:
  case Kind::DispatchThunkInitializerAsyncFunctionPointer:
  case Kind::DispatchThunkAllocatorAsyncFunctionPointer:
  case Kind::DistributedThunkAsyncFunctionPointer:
  case Kind::PartialApplyForwarderAsyncFunctionPointer:
  case Kind::AsyncFunctionPointerAST:
  case Kind::KnownAsyncFunctionPointer:
    return IGM.AsyncFunctionPointerTy;
  case Kind::PartialApplyForwarder:
    return IGM.FunctionPtrTy;
  default:
    llvm_unreachable("declaration LLVM type not specified");
  }
}

Alignment LinkEntity::getAlignment(IRGenModule &IGM) const {
  switch (getKind()) {
  case Kind::ModuleDescriptor:
  case Kind::ExtensionDescriptor:
  case Kind::AnonymousDescriptor:
  case Kind::NominalTypeDescriptor:
  case Kind::NominalTypeDescriptorRecord:
  case Kind::ProtocolDescriptor:
  case Kind::ProtocolDescriptorRecord:
  case Kind::AssociatedTypeDescriptor:
  case Kind::AssociatedConformanceDescriptor:
  case Kind::BaseConformanceDescriptor:
  case Kind::ProtocolConformanceDescriptor:
  case Kind::ProtocolConformanceDescriptorRecord:
  case Kind::ProtocolRequirementsBaseDescriptor:
  case Kind::ReflectionBuiltinDescriptor:
  case Kind::ReflectionFieldDescriptor:
  case Kind::ReflectionAssociatedTypeDescriptor:
  case Kind::PropertyDescriptor:
  case Kind::EnumCase:
  case Kind::MethodDescriptor:
  case Kind::MethodDescriptorInitializer:
  case Kind::MethodDescriptorAllocator:
  case Kind::OpaqueTypeDescriptor:
  case Kind::OpaqueTypeDescriptorRecord:
    return Alignment(4);
  case Kind::AsyncFunctionPointer:
  case Kind::DispatchThunkAsyncFunctionPointer:
  case Kind::DispatchThunkInitializerAsyncFunctionPointer:
  case Kind::DispatchThunkAllocatorAsyncFunctionPointer:
  case Kind::PartialApplyForwarderAsyncFunctionPointer:
  case Kind::KnownAsyncFunctionPointer:
  case Kind::ObjCClassRef:
  case Kind::ObjCClass:
  case Kind::TypeMetadataLazyCacheVariable:
  case Kind::TypeMetadataSingletonInitializationCache:
  case Kind::TypeMetadata:
  case Kind::TypeMetadataPattern:
  case Kind::ClassMetadataBaseOffset:
  case Kind::TypeMetadataInstantiationCache:
  case Kind::ValueWitnessTable:
  case Kind::FieldOffset:
  case Kind::ProtocolWitnessTableLazyCacheVariable:
  case Kind::ProtocolWitnessTable:
  case Kind::ProtocolWitnessTablePattern:
  case Kind::ObjCMetaclass:
  case Kind::SwiftMetaclassStub:
  case Kind::CanonicalSpecializedGenericSwiftMetaclassStub:
  case Kind::DynamicallyReplaceableFunctionVariable:
  case Kind::DynamicallyReplaceableFunctionKey:
  case Kind::OpaqueTypeDescriptorAccessorKey:
  case Kind::OpaqueTypeDescriptorAccessorVar:
  case Kind::ObjCResilientClassStub:
  case Kind::DifferentiabilityWitness:
  case Kind::NoncanonicalSpecializedGenericTypeMetadata:
  case Kind::NoncanonicalSpecializedGenericTypeMetadataCacheVariable:
  case Kind::PartialApplyForwarder:
    return IGM.getPointerAlignment();
  case Kind::CanonicalPrespecializedGenericTypeCachingOnceToken:
  case Kind::TypeMetadataDemanglingCacheVariable:
    return Alignment(8);
  case Kind::SILFunction:
    return Alignment(1);
  default:
    llvm_unreachable("alignment not specified");
  }
}

bool LinkEntity::isWeakImported(ModuleDecl *module) const {
  switch (getKind()) {
  case Kind::SILGlobalVariable:
    if (getSILGlobalVariable()->getDecl()) {
      return getSILGlobalVariable()->getDecl()->isWeakImported(module);
    }
    return false;
  case Kind::DynamicallyReplaceableFunctionKey:
  case Kind::DynamicallyReplaceableFunctionVariable:
  case Kind::SILFunction: {
    return getSILFunction()->isWeakImported();
  }

  case Kind::AssociatedConformanceDescriptor:
  case Kind::DefaultAssociatedConformanceAccessor: {
    // Associated conformance descriptors use the protocol as
    // their declaration, but are weak linked if the associated
    // type stored in extra storage area is weak linked.
    auto assocConformance = getAssociatedConformance();
    auto *depMemTy = assocConformance.first->castTo<DependentMemberType>();
    return depMemTy->getAssocType()->isWeakImported(module);
  }

  case Kind::BaseConformanceDescriptor:
    return cast<ProtocolDecl>(getDecl())->isWeakImported(module);

  case Kind::TypeMetadata:
  case Kind::TypeMetadataAccessFunction: {
    if (auto *nominalDecl = getType()->getAnyNominal())
      return nominalDecl->isWeakImported(module);
    return false;
  }

  case Kind::AsyncFunctionPointerAST:
  case Kind::DistributedThunkAsyncFunctionPointer:
  case Kind::DispatchThunk:
  case Kind::DispatchThunkDerivative:
  case Kind::DispatchThunkInitializer:
  case Kind::DispatchThunkAllocator:
  case Kind::MethodDescriptor:
  case Kind::MethodDescriptorDerivative:
  case Kind::MethodDescriptorInitializer:
  case Kind::MethodDescriptorAllocator:
  case Kind::MethodLookupFunction:
  case Kind::EnumCase:
  case Kind::FieldOffset:
  case Kind::ObjCClass:
  case Kind::ObjCClassRef:
  case Kind::ObjCMetaclass:
  case Kind::SwiftMetaclassStub:
  case Kind::ClassMetadataBaseOffset:
  case Kind::PropertyDescriptor:
  case Kind::NominalTypeDescriptor:
  case Kind::NominalTypeDescriptorRecord:
  case Kind::ModuleDescriptor:
  case Kind::ProtocolDescriptor:
  case Kind::ProtocolDescriptorRecord:
  case Kind::ProtocolRequirementsBaseDescriptor:
  case Kind::AssociatedTypeDescriptor:
  case Kind::DynamicallyReplaceableFunctionKeyAST:
  case Kind::DynamicallyReplaceableFunctionVariableAST:
  case Kind::DynamicallyReplaceableFunctionImpl:
  case Kind::OpaqueTypeDescriptor:
  case Kind::OpaqueTypeDescriptorRecord:
  case Kind::OpaqueTypeDescriptorAccessor:
  case Kind::OpaqueTypeDescriptorAccessorImpl:
  case Kind::OpaqueTypeDescriptorAccessorKey:
  case Kind::OpaqueTypeDescriptorAccessorVar:
    return getDecl()->isWeakImported(module);

  case Kind::CanonicalSpecializedGenericSwiftMetaclassStub:
    return getType()->getClassOrBoundGenericClass()->isWeakImported(module);

  case Kind::ProtocolWitnessTable:
  case Kind::ProtocolConformanceDescriptor:
  case Kind::ProtocolConformanceDescriptorRecord:
    return getProtocolConformance()->getRootConformance()
                                   ->isWeakImported(module);

  case Kind::CanonicalSpecializedGenericTypeMetadataAccessFunction:
  case Kind::NoncanonicalSpecializedGenericTypeMetadata:
  case Kind::NoncanonicalSpecializedGenericTypeMetadataCacheVariable:
  case Kind::CanonicalPrespecializedGenericTypeCachingOnceToken:
    return false;

  // TODO: Revisit some of the below, for weak conformances.
  case Kind::ObjCMetadataUpdateFunction:
  case Kind::ObjCResilientClassStub:
  case Kind::PartialApplyForwarder:
  case Kind::TypeMetadataPattern:
  case Kind::TypeMetadataInstantiationCache:
  case Kind::TypeMetadataInstantiationFunction:
  case Kind::TypeMetadataSingletonInitializationCache:
  case Kind::TypeMetadataCompletionFunction:
  case Kind::ExtensionDescriptor:
  case Kind::AnonymousDescriptor:
  case Kind::ProtocolWitnessTablePattern:
  case Kind::GenericProtocolWitnessTableInstantiationFunction:
  case Kind::AssociatedTypeWitnessTableAccessFunction:
  case Kind::ReflectionAssociatedTypeDescriptor:
  case Kind::ProtocolWitnessTableLazyAccessFunction:
  case Kind::ProtocolWitnessTableLazyCacheVariable:
  case Kind::ValueWitness:
  case Kind::ValueWitnessTable:
  case Kind::TypeMetadataLazyCacheVariable:
  case Kind::TypeMetadataDemanglingCacheVariable:
  case Kind::ReflectionBuiltinDescriptor:
  case Kind::ReflectionFieldDescriptor:
  case Kind::CoroutineContinuationPrototype:
  case Kind::DifferentiabilityWitness:
    return false;

  case Kind::AsyncFunctionPointer:
  case Kind::DispatchThunkAsyncFunctionPointer:
  case Kind::DispatchThunkInitializerAsyncFunctionPointer:
  case Kind::DispatchThunkAllocatorAsyncFunctionPointer:
  case Kind::PartialApplyForwarderAsyncFunctionPointer:
    return getUnderlyingEntityForAsyncFunctionPointer()
        .isWeakImported(module);
  case Kind::KnownAsyncFunctionPointer:
    auto &context = module->getASTContext();
    auto deploymentAvailability =
        AvailabilityContext::forDeploymentTarget(context);
    return !deploymentAvailability.isContainedIn(
        context.getConcurrencyAvailability());
  }

  llvm_unreachable("Bad link entity kind");
}

DeclContext *LinkEntity::getDeclContextForEmission() const {
  switch (getKind()) {
  case Kind::AsyncFunctionPointerAST:
  case Kind::DistributedThunkAsyncFunctionPointer:
  case Kind::DispatchThunk:
  case Kind::DispatchThunkDerivative:
  case Kind::DispatchThunkInitializer:
  case Kind::DispatchThunkAllocator:
  case Kind::MethodDescriptor:
  case Kind::MethodDescriptorDerivative:
  case Kind::MethodDescriptorInitializer:
  case Kind::MethodDescriptorAllocator:
  case Kind::MethodLookupFunction:
  case Kind::EnumCase:
  case Kind::FieldOffset:
  case Kind::ObjCClass:
  case Kind::ObjCMetaclass:
  case Kind::SwiftMetaclassStub:
  case Kind::ObjCMetadataUpdateFunction:
  case Kind::ObjCResilientClassStub:
  case Kind::ClassMetadataBaseOffset:
  case Kind::PropertyDescriptor:
  case Kind::NominalTypeDescriptor:
  case Kind::NominalTypeDescriptorRecord:
  case Kind::TypeMetadataPattern:
  case Kind::TypeMetadataInstantiationCache:
  case Kind::TypeMetadataInstantiationFunction:
  case Kind::TypeMetadataSingletonInitializationCache:
  case Kind::TypeMetadataCompletionFunction:
  case Kind::ProtocolDescriptor:
  case Kind::ProtocolDescriptorRecord:
  case Kind::ProtocolRequirementsBaseDescriptor:
  case Kind::AssociatedTypeDescriptor:
  case Kind::AssociatedConformanceDescriptor:
  case Kind::DefaultAssociatedConformanceAccessor:
  case Kind::BaseConformanceDescriptor:
  case Kind::DynamicallyReplaceableFunctionVariableAST:
  case Kind::DynamicallyReplaceableFunctionKeyAST:
  case Kind::DynamicallyReplaceableFunctionImpl:
  case Kind::OpaqueTypeDescriptor:
  case Kind::OpaqueTypeDescriptorRecord:
  case Kind::OpaqueTypeDescriptorAccessor:
  case Kind::OpaqueTypeDescriptorAccessorImpl:
  case Kind::OpaqueTypeDescriptorAccessorKey:
  case Kind::OpaqueTypeDescriptorAccessorVar:
  case Kind::CanonicalPrespecializedGenericTypeCachingOnceToken:
    return getDecl()->getDeclContext();

  case Kind::CanonicalSpecializedGenericSwiftMetaclassStub:
    return getType()->getClassOrBoundGenericClass()->getDeclContext();

  case Kind::SILFunction:
  case Kind::DynamicallyReplaceableFunctionVariable:
  case Kind::DynamicallyReplaceableFunctionKey:
    return getSILFunction()->getDeclContext();
  
  case Kind::SILGlobalVariable:
    if (auto decl = getSILGlobalVariable()->getDecl())
      return decl->getDeclContext();

    return nullptr;
    
  case Kind::ProtocolWitnessTable:
  case Kind::ProtocolConformanceDescriptor:
  case Kind::ProtocolConformanceDescriptorRecord:
    return getRootProtocolConformance()->getDeclContext();

  case Kind::ProtocolWitnessTablePattern:
  case Kind::GenericProtocolWitnessTableInstantiationFunction:
  case Kind::AssociatedTypeWitnessTableAccessFunction:
  case Kind::ReflectionAssociatedTypeDescriptor:
  case Kind::ProtocolWitnessTableLazyCacheVariable:
  case Kind::ProtocolWitnessTableLazyAccessFunction:
    return getProtocolConformance()->getDeclContext();

  case Kind::TypeMetadata: {
    auto ty = getType();
    // Only fully concrete nominal type metadata gets emitted eagerly.
    auto nom = ty->getAnyNominal();
    if (nom)
      return nom->getDeclContext();
    
    return nullptr;
  }

  // Always shared linkage
  case Kind::ModuleDescriptor:
  case Kind::ExtensionDescriptor:
  case Kind::AnonymousDescriptor:
  case Kind::ObjCClassRef:
  case Kind::TypeMetadataAccessFunction:
  case Kind::CanonicalSpecializedGenericTypeMetadataAccessFunction:
  case Kind::TypeMetadataLazyCacheVariable:
  case Kind::TypeMetadataDemanglingCacheVariable:
  case Kind::NoncanonicalSpecializedGenericTypeMetadata:
  case Kind::NoncanonicalSpecializedGenericTypeMetadataCacheVariable:
    assert(isAlwaysSharedLinkage() && "kind should always be shared linkage");
    return nullptr;

  // TODO
  case Kind::CoroutineContinuationPrototype:
  case Kind::ReflectionFieldDescriptor:
  case Kind::ReflectionBuiltinDescriptor:
  case Kind::ValueWitness:
  case Kind::ValueWitnessTable:
  case Kind::DifferentiabilityWitness:
  case Kind::PartialApplyForwarder:
  case Kind::KnownAsyncFunctionPointer:
    return nullptr;

  case Kind::AsyncFunctionPointer:
  case Kind::DispatchThunkAsyncFunctionPointer:
  case Kind::DispatchThunkInitializerAsyncFunctionPointer:
  case Kind::DispatchThunkAllocatorAsyncFunctionPointer:
  case Kind::PartialApplyForwarderAsyncFunctionPointer:
    return getUnderlyingEntityForAsyncFunctionPointer()
        .getDeclContextForEmission();
  }
  llvm_unreachable("invalid decl kind");
}

bool LinkEntity::isAlwaysSharedLinkage() const {
  switch (getKind()) {
  case Kind::ModuleDescriptor:
  case Kind::ExtensionDescriptor:
  case Kind::AnonymousDescriptor:
  case Kind::ObjCClassRef:
  case Kind::TypeMetadataAccessFunction:
  case Kind::CanonicalSpecializedGenericTypeMetadataAccessFunction:
  case Kind::TypeMetadataLazyCacheVariable:
  case Kind::TypeMetadataDemanglingCacheVariable:
  case Kind::NoncanonicalSpecializedGenericTypeMetadata:
  case Kind::NoncanonicalSpecializedGenericTypeMetadataCacheVariable:
    return true;

  default:
    return false;
  }
}
