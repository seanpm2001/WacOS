//===--- GenReflection.cpp - IR generation for nominal type reflection ----===//
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
//  This file implements IR generation of type metadata for struct/class
//  stored properties and enum cases for use with reflection.
//===----------------------------------------------------------------------===//

#include "swift/AST/Decl.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/IRGenOptions.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/SubstitutionMap.h"
#include "swift/Basic/Platform.h"
#include "swift/IRGen/Linking.h"
#include "swift/Reflection/MetadataSourceBuilder.h"
#include "swift/Reflection/Records.h"
#include "swift/SIL/SILModule.h"

#include "ConstantBuilder.h"
#include "Explosion.h"
#include "Field.h"
#include "GenClass.h"
#include "GenDecl.h"
#include "GenEnum.h"
#include "GenHeap.h"
#include "GenMeta.h"
#include "GenProto.h"
#include "GenType.h"
#include "IRGenDebugInfo.h"
#include "IRGenFunction.h"
#include "IRGenMangler.h"
#include "IRGenModule.h"
#include "LoadableTypeInfo.h"
#include "MetadataRequest.h"

using namespace swift;
using namespace irgen;
using namespace reflection;

class MetadataSourceEncoder
  : public MetadataSourceVisitor<MetadataSourceEncoder> {
  llvm::raw_ostream &OS;
public:
  MetadataSourceEncoder(llvm::raw_ostream &OS) : OS(OS) {}

  void
  visitClosureBindingMetadataSource(const ClosureBindingMetadataSource *CB) {
    OS << 'B';
    OS << CB->getIndex();
  }

  void
  visitReferenceCaptureMetadataSource(const ReferenceCaptureMetadataSource *RC){
    OS << 'R';
    OS << RC->getIndex();
  }

  void
  visitMetadataCaptureMetadataSource(const MetadataCaptureMetadataSource *MC) {
    OS << 'M';
    OS << MC->getIndex();
  }

  void
  visitGenericArgumentMetadataSource(const GenericArgumentMetadataSource *GA) {
    OS << 'G';
    OS << GA->getIndex();
    visit(GA->getSource());
    OS << '_';
  }

  void visitSelfMetadataSource(const SelfMetadataSource *S) {
    OS << 'S';
  }

  void
  visitSelfWitnessTableMetadataSource(const SelfWitnessTableMetadataSource *S) {
    OS << 'W';
  }
};

class PrintMetadataSource
: public MetadataSourceVisitor<PrintMetadataSource, void> {
  llvm::raw_ostream &OS;
  unsigned Indent;

  llvm::raw_ostream &indent(unsigned Amount) {
    for (unsigned i = 0; i < Amount; ++i)
      OS << ' ';
    return OS;
  }

  llvm::raw_ostream &printHeader(std::string Name) {
    indent(Indent) << '(' << Name;
    return OS;
  }

  template<typename T>
  llvm::raw_ostream &printField(std::string name, const T &value) {
    if (!name.empty())
      OS << " " << name << "=" << value;
    else
      OS << " " << value;
    return OS;
  }

  void printRec(const reflection::MetadataSource *MS) {
    OS << "\n";

    Indent += 2;
    visit(MS);
    Indent -= 2;
  }

  void closeForm() {
    OS << ')';
  }

public:
  PrintMetadataSource(llvm::raw_ostream &OS, unsigned Indent)
    : OS(OS), Indent(Indent) {}

  void
  visitClosureBindingMetadataSource(const ClosureBindingMetadataSource *CB) {
    printHeader("closure-binding");
    printField("index", CB->getIndex());
    closeForm();
  }

  void
  visitReferenceCaptureMetadataSource(const ReferenceCaptureMetadataSource *RC){
    printHeader("reference-capture");
    printField("index", RC->getIndex());
    closeForm();
  }

  void
  visitMetadataCaptureMetadataSource(const MetadataCaptureMetadataSource *MC){
    printHeader("metadata-capture");
    printField("index", MC->getIndex());
    closeForm();
  }

  void
  visitGenericArgumentMetadataSource(const GenericArgumentMetadataSource *GA) {
    printHeader("generic-argument");
    printField("index", GA->getIndex());
    printRec(GA->getSource());
    closeForm();
  }

  void
  visitSelfMetadataSource(const SelfMetadataSource *S) {
    printHeader("self");
    closeForm();
  }

  void
  visitSelfWitnessTableMetadataSource(const SelfWitnessTableMetadataSource *S) {
    printHeader("self-witness-table");
    closeForm();
  }
};

Optional<llvm::VersionTuple>
getRuntimeVersionThatSupportsDemanglingType(CanType type) {
  // The Swift 5.5 runtime is the first version able to demangle types
  // related to concurrency.
  bool needsConcurrency = type.findIf([](CanType t) -> bool {
    if (auto fn = dyn_cast<AnyFunctionType>(t)) {
      if (fn->isAsync() || fn->isSendable() || fn->hasGlobalActor())
        return true;

      for (const auto &param : fn->getParams()) {
        if (param.isIsolated())
          return true;
      }

      return false;
    }
    return false;
  });
  if (needsConcurrency) {
    return llvm::VersionTuple(5, 5);
  }

  // Associated types of opaque types weren't mangled in a usable form by the
  // Swift 5.1 runtime, so we needed to add a new mangling in 5.2.
  if (type->hasOpaqueArchetype()) {
    auto hasOpaqueAssocType = type.findIf([](CanType t) -> bool {
      if (auto a = dyn_cast<NestedArchetypeType>(t)) {
        return isa<OpaqueTypeArchetypeType>(a->getRoot());
      }
      return false;
    });
    
    if (hasOpaqueAssocType)
      return llvm::VersionTuple(5, 2);
    // Although opaque types in general were only added in Swift 5.1,
    // declarations that use them are already covered by availability
    // guards, so we don't need to limit availability of mangled names
    // involving them.
  }

  return None;
}

// Produce a fallback mangled type name that uses an open-coded callback
// to form the metadata. This is useful for working around bugs in older
// runtimes, or supporting new type system features when deploying back.
//
// Note that this functionality is limited, because the demangler callback
// mechanism can only produce complete metadata. It can't be used in situations
// where completing the metadata during demangling might cause cyclic
// dependencies.
static std::pair<llvm::Constant *, unsigned>
getTypeRefByFunction(IRGenModule &IGM,
                     CanGenericSignature sig,
                     CanType t) {
  IRGenMangler mangler;
  std::string symbolName =
    mangler.mangleSymbolNameForMangledMetadataAccessorString(
                                                   "get_type_metadata", sig, t);
  auto constant = IGM.getAddrOfStringForMetadataRef(symbolName, /*align*/2,
                                                    /*low bit*/false,
    [&](ConstantInitBuilder &B) {
      llvm::Function *accessor;
      
      // Otherwise, we need to emit a helper function to bind the arguments
      // out of the demangler's argument buffer.
      auto fnTy = llvm::FunctionType::get(IGM.TypeMetadataPtrTy,
                                          {IGM.Int8PtrTy}, /*vararg*/ false);
      accessor =
        llvm::Function::Create(fnTy, llvm::GlobalValue::PrivateLinkage,
                               symbolName, IGM.getModule());
      accessor->setAttributes(IGM.constructInitialAttributes());
      
      SmallVector<GenericRequirement, 4> requirements;
      auto *genericEnv = sig.getGenericEnvironment();
      enumerateGenericSignatureRequirements(sig,
              [&](GenericRequirement reqt) { requirements.push_back(reqt); });

      {
        IRGenFunction IGF(IGM, accessor);
        if (IGM.DebugInfo)
          IGM.DebugInfo->emitArtificialFunction(IGF, accessor);

        auto bindingsBufPtr = IGF.collectParameters().claimNext();

        auto substT = genericEnv
          ? genericEnv->mapTypeIntoContext(t)->getCanonicalType()
          : t;

        bindFromGenericRequirementsBuffer(IGF, requirements,
            Address(bindingsBufPtr, IGM.getPointerAlignment()),
            MetadataState::Complete,
            [&](CanType t) {
              return genericEnv
                ? genericEnv->mapTypeIntoContext(t)->getCanonicalType()
                : t;
            });

        auto ret = IGF.emitTypeMetadataRef(substT);
        IGF.Builder.CreateRet(ret);
      }
      // Form the mangled name with its relative reference.
      auto S = B.beginStruct();
      S.setPacked(true);
      S.add(llvm::ConstantInt::get(IGM.Int8Ty, 255));
      S.add(llvm::ConstantInt::get(IGM.Int8Ty, 9));
      S.addRelativeAddress(accessor);

      // And a null terminator!
      S.addInt(IGM.Int8Ty, 0);

      return S.finishAndCreateFuture();
    });
  return {constant, 6};
}

bool swift::irgen::mangledNameIsUnknownToDeployTarget(IRGenModule &IGM,
                                                      CanType type) {
  if (auto runtimeCompatVersion = getSwiftRuntimeCompatibilityVersionForTarget(
          IGM.Context.LangOpts.Target)) {
    if (auto minimumSupportedRuntimeVersion =
            getRuntimeVersionThatSupportsDemanglingType(type)) {
      if (*runtimeCompatVersion < *minimumSupportedRuntimeVersion) {
        return true;
      }
    }
  }
  return false;
}

static std::pair<llvm::Constant *, unsigned>
getTypeRefImpl(IRGenModule &IGM,
               CanType type,
               CanGenericSignature sig,
               MangledTypeRefRole role) {
  switch (role) {
  case MangledTypeRefRole::DefaultAssociatedTypeWitness:
  case MangledTypeRefRole::Metadata:
    // Note that we're using all of the nominal types referenced by this type,
    // ensuring that we can always reconstruct type metadata from a mangled name
    // in-process.
    IGM.IRGen.noteUseOfTypeMetadata(type);
    
    // If the minimum deployment target's runtime demangler wouldn't understand
    // this mangled name, then fall back to generating a "mangled name" with a
    // symbolic reference with a callback function.
    if (mangledNameIsUnknownToDeployTarget(IGM, type)) {
      return getTypeRefByFunction(IGM, sig, type);
    }

    break;

  case MangledTypeRefRole::Reflection:
    // For reflection records only used for out-of-process reflection, we do not
    // need to force emission of runtime type metadata.
    IGM.IRGen.noteUseOfFieldDescriptors(type);
    break;
  }

  IRGenMangler Mangler;
  auto SymbolicName = Mangler.mangleTypeForReflection(IGM, sig, type);
  return {IGM.getAddrOfStringForTypeRef(SymbolicName, role),
          SymbolicName.runtimeSizeInBytes()};
}

std::pair<llvm::Constant *, unsigned>
IRGenModule::getTypeRef(CanType type, CanGenericSignature sig,
                        MangledTypeRefRole role) {
  type = substOpaqueTypesWithUnderlyingTypes(type);
  return getTypeRefImpl(*this, type, sig, role);
}

std::pair<llvm::Constant *, unsigned>
IRGenModule::getTypeRef(Type type, GenericSignature genericSig,
                        MangledTypeRefRole role) {
  return getTypeRef(type->getCanonicalType(genericSig),
                    genericSig.getCanonicalSignature(), role);
}

std::pair<llvm::Constant *, unsigned>
IRGenModule::getLoweredTypeRef(SILType loweredType,
                               CanGenericSignature genericSig,
                               MangledTypeRefRole role) {
  auto substTy =
    substOpaqueTypesWithUnderlyingTypes(loweredType, genericSig);
  auto type = substTy.getASTType();
  if (substTy.hasArchetype())
    type = type->mapTypeOutOfContext()->getCanonicalType();

  return getTypeRefImpl(*this, type, genericSig, role);
}

/// Emit a mangled string referencing a specific protocol conformance, so that
/// the runtime can fetch its witness table.
///
/// TODO: Currently this uses a stub mangling that just refers to an accessor
/// function. We need to fully develop the mangling with the ability to refer
/// to dependent conformances to be able to use mangled strings.
llvm::Constant *
IRGenModule::emitWitnessTableRefString(CanType type,
                                      ProtocolConformanceRef conformance,
                                      GenericSignature origGenericSig,
                                      bool shouldSetLowBit) {
  std::tie(type, conformance)
    = substOpaqueTypesWithUnderlyingTypes(type, conformance);
  
  auto origType = type;
  auto genericSig = origGenericSig.getCanonicalSignature();

  SmallVector<GenericRequirement, 4> requirements;
  enumerateGenericSignatureRequirements(genericSig,
              [&](GenericRequirement reqt) { requirements.push_back(reqt); });
  auto *genericEnv = genericSig.getGenericEnvironment();

  IRGenMangler mangler;
  std::string symbolName =
    mangler.mangleSymbolNameForMangledConformanceAccessorString(
      "get_witness_table", genericSig, type, conformance);

  return getAddrOfStringForMetadataRef(symbolName, /*alignment=*/2,
      shouldSetLowBit,
      [&](ConstantInitBuilder &B) {
        // Build a stub that loads the necessary bindings from the key path's
        // argument buffer then fetches the metadata.
        auto fnTy = llvm::FunctionType::get(WitnessTablePtrTy,
                                            {Int8PtrTy}, /*vararg*/ false);
        auto accessorThunk =
          llvm::Function::Create(fnTy, llvm::GlobalValue::PrivateLinkage,
                                 symbolName, getModule());
        accessorThunk->setAttributes(constructInitialAttributes());
        
        {
          IRGenFunction IGF(*this, accessorThunk);
          if (DebugInfo)
            DebugInfo->emitArtificialFunction(IGF, accessorThunk);

          if (type->hasTypeParameter()) {
            auto bindingsBufPtr = IGF.collectParameters().claimNext();

            bindFromGenericRequirementsBuffer(IGF, requirements,
                Address(bindingsBufPtr, getPointerAlignment()),
                MetadataState::Complete,
                [&](CanType t) {
                  return genericEnv->mapTypeIntoContext(t)->getCanonicalType();
                });

            type = genericEnv->mapTypeIntoContext(type)->getCanonicalType();
          }
          if (origType->hasTypeParameter()) {
            auto origSig = genericEnv->getGenericSignature();
            conformance = conformance.subst(origType,
              QueryInterfaceTypeSubstitutions(genericEnv),
              LookUpConformanceInSignature(origSig.getPointer()));
          }
          auto ret = emitWitnessTableRef(IGF, type, conformance);
          IGF.Builder.CreateRet(ret);
        }

        // Form the mangled name with its relative reference.
        auto S = B.beginStruct();
        S.setPacked(true);
        S.add(llvm::ConstantInt::get(Int8Ty, 255));
        S.add(llvm::ConstantInt::get(Int8Ty, 9));
        S.addRelativeAddress(accessorThunk);

        // And a null terminator!
        S.addInt(Int8Ty, 0);

        return S.finishAndCreateFuture();
      });
}


llvm::Constant *IRGenModule::getMangledAssociatedConformance(
                                  const NormalProtocolConformance *conformance,
                                  const AssociatedConformance &requirement) {
  // Figure out the name of the symbol to be used for the conformance.
  IRGenMangler mangler;
  auto symbolName =
    mangler.mangleSymbolNameForAssociatedConformanceWitness(
      conformance, requirement.getAssociation(),
      requirement.getAssociatedRequirement());

  // See if we emitted the constant already.
  auto &entry = StringsForTypeRef[symbolName];
  if (entry.second) {
    return entry.second;
  }

  // Get the accessor for this associated conformance.
  llvm::Function *accessor;
  unsigned char kind;
  if (conformance) {
    kind = 7;
    accessor = getAddrOfAssociatedTypeWitnessTableAccessFunction(conformance,
                                                                requirement);
  } else {
    kind = 8;
    accessor = getAddrOfDefaultAssociatedConformanceAccessor(requirement);
  }

  // Form the mangled name with its relative reference.
  ConstantInitBuilder B(*this);
  auto S = B.beginStruct();
  S.setPacked(true);
  S.add(llvm::ConstantInt::get(Int8Ty, 255));
  S.add(llvm::ConstantInt::get(Int8Ty, kind));
  S.addRelativeAddress(accessor);

  // And a null terminator!
  S.addInt(Int8Ty, 0);

  auto finished = S.finishAndCreateFuture();
  auto var = new llvm::GlobalVariable(Module, finished.getType(),
                                      /*constant*/ true,
                                      llvm::GlobalValue::LinkOnceODRLinkage,
                                      nullptr,
                                      symbolName);
  ApplyIRLinkage(IRLinkage::InternalLinkOnceODR).to(var);
  var->setAlignment(llvm::MaybeAlign(2));
  setTrueConstGlobal(var);
  var->setSection(getReflectionTypeRefSectionName());

  finished.installInGlobal(var);

  // Drill down to the i8* at the beginning of the constant.
  auto addr = llvm::ConstantExpr::getBitCast(var, Int8PtrTy);

  // Set the low bit.
  unsigned bit = ProtocolRequirementFlags::AssociatedTypeMangledNameBit;
  auto bitConstant = llvm::ConstantInt::get(IntPtrTy, bit);
  addr = llvm::ConstantExpr::getGetElementPtr(
    addr->getType()->getPointerElementType(), addr, bitConstant);

  // Update the entry.
  entry = {var, addr};

  return addr;
}

class ReflectionMetadataBuilder {
protected:
  IRGenModule &IGM;
  ConstantInitBuilder InitBuilder;
  ConstantStructBuilder B;

  ReflectionMetadataBuilder(IRGenModule &IGM)
    : IGM(IGM), InitBuilder(IGM), B(InitBuilder.beginStruct()) {}

  virtual ~ReflectionMetadataBuilder() {}
  
  // Collect any builtin types referenced from this type.
  void addBuiltinTypeRefs(CanType type) {
    if (IGM.getSwiftModule()->isStdlibModule()) {
      type.visit([&](CanType t) {
        if (isa<BuiltinType>(t))
          IGM.BuiltinTypes.insert(t);
      });
    }
  }

  /// Add a 32-bit relative offset to a mangled typeref string
  /// in the typeref reflection section.
  ///
  /// By default, we use MangledTypeRefRole::Reflection, which does not
  /// force emission of any type metadata referenced from the typeref.
  ///
  /// For reflection records which are demangled to produce type metadata
  /// in-process, pass MangledTypeRefRole::Metadata instead.
  void addTypeRef(Type type, GenericSignature genericSig,
                  MangledTypeRefRole role =
                      MangledTypeRefRole::Reflection) {
    addTypeRef(type->getCanonicalType(genericSig),
               genericSig.getCanonicalSignature(), role);
  }

  /// Add a 32-bit relative offset to a mangled typeref string
  /// in the typeref reflection section.
  ///
  /// By default, we use MangledTypeRefRole::Reflection, which does not
  /// force emission of any type metadata referenced from the typeref.
  ///
  /// For reflection records which are demangled to produce type metadata
  /// in-process, pass MangledTypeRefRole::Metadata instead.
  void addTypeRef(CanType type,
                  CanGenericSignature sig,
                  MangledTypeRefRole role =
                      MangledTypeRefRole::Reflection) {
    B.addRelativeAddress(IGM.getTypeRef(type, sig, role).first);
    addBuiltinTypeRefs(type);
  }

  void
  addLoweredTypeRef(SILType loweredType,
                    CanGenericSignature genericSig,
                    MangledTypeRefRole role = MangledTypeRefRole::Reflection) {
    B.addRelativeAddress(
        IGM.getLoweredTypeRef(loweredType, genericSig, role).first);
    addBuiltinTypeRefs(loweredType.getASTType());
  }

  /// Add a 32-bit relative offset to a mangled nominal type string
  /// in the typeref reflection section.
  ///
  /// See above comment about 'role'.
  void addNominalRef(const NominalTypeDecl *nominal,
                     MangledTypeRefRole role =
                      MangledTypeRefRole::Reflection) {
    if (auto proto = dyn_cast<ProtocolDecl>(nominal)) {
      IRGenMangler mangler;
      SymbolicMangling mangledStr;
      mangledStr.String = mangler.mangleBareProtocol(proto);
      auto mangledName =
        IGM.getAddrOfStringForTypeRef(mangledStr, role);
      B.addRelativeAddress(mangledName);
    } else {
      addTypeRef(nominal->getDeclaredType(), GenericSignature(), role);
    }
  }

  // A function signature for a lambda wrapping an IRGenModule::getAddrOf*
  // method.
  using GetAddrOfEntityFn = llvm::Constant* (IRGenModule &, ConstantInit);

  llvm::GlobalVariable *emit(
                        Optional<llvm::function_ref<GetAddrOfEntityFn>> getAddr,
                        const char *section) {
    layout();

    llvm::GlobalVariable *var;

    // Some reflection records have a mangled symbol name, for uniquing
    // imported type metadata.
    if (getAddr) {
      auto init = B.finishAndCreateFuture();

      var = cast<llvm::GlobalVariable>((*getAddr)(IGM, init));
      var->setConstant(true);
    // Others, such as capture descriptors, do not have a name.
    } else {
      var = B.finishAndCreateGlobal("\x01l__swift5_reflection_descriptor",
                                    Alignment(4), /*isConstant*/ true,
                                    llvm::GlobalValue::PrivateLinkage);
    }

    var->setSection(section);

    IGM.addUsedGlobal(var);

    disableAddressSanitizer(IGM, var);

    return var;
  }

  // Helpers to guide the C++ type system into converting lambda arguments
  // to Optional<function_ref>
  llvm::GlobalVariable *emit(llvm::function_ref<GetAddrOfEntityFn> getAddr,
                             const char *section) {
    return emit(Optional<llvm::function_ref<GetAddrOfEntityFn>>(getAddr),
                section);
  }
  llvm::GlobalVariable *emit(NoneType none,
                             const char *section) {
    return emit(Optional<llvm::function_ref<GetAddrOfEntityFn>>(),
                section);
  }

  virtual void layout() = 0;
};

class AssociatedTypeMetadataBuilder : public ReflectionMetadataBuilder {
  static const uint32_t AssociatedTypeRecordSize = 8;

  const ProtocolConformance *Conformance;
  ArrayRef<std::pair<StringRef, CanType>> AssociatedTypes;

  void layout() override {
    PrettyStackTraceConformance DebugStack("emitting associated type metadata",
                                           Conformance);

    auto *DC = Conformance->getDeclContext();
    addNominalRef(DC->getSelfNominalTypeDecl());
    addNominalRef(Conformance->getProtocol());

    B.addInt32(AssociatedTypes.size());
    B.addInt32(AssociatedTypeRecordSize);

    auto genericSig = DC->getGenericSignatureOfContext().getCanonicalSignature();
    for (auto AssocTy : AssociatedTypes) {
      auto NameGlobal = IGM.getAddrOfFieldName(AssocTy.first);
      B.addRelativeAddress(NameGlobal);
      addTypeRef(AssocTy.second, genericSig);
    }
  }

public:
  AssociatedTypeMetadataBuilder(IRGenModule &IGM,
                        const ProtocolConformance *Conformance,
                        ArrayRef<std::pair<StringRef, CanType>> AssociatedTypes)
    : ReflectionMetadataBuilder(IGM), Conformance(Conformance),
      AssociatedTypes(AssociatedTypes) {}

  llvm::GlobalVariable *emit() {
    auto section = IGM.getAssociatedTypeMetadataSectionName();
    llvm::GlobalVariable *var = ReflectionMetadataBuilder::emit(
        [&](IRGenModule &IGM, ConstantInit init) -> llvm::Constant * {
          return IGM.getAddrOfReflectionAssociatedTypeDescriptor(Conformance,
                                                                 init);
        },
        section);

    if (IGM.IRGen.Opts.ConditionalRuntimeRecords) {
      // Allow dead-stripping `var` (the reflection record) when the protocol
      // or type (from the conformance) is not referenced.
      IGM.appendLLVMUsedConditionalEntry(var, Conformance);
    }

    return var;
  }
};

class FieldTypeMetadataBuilder : public ReflectionMetadataBuilder {
public:
  static const uint32_t FieldRecordSize = 12;
  
private:
  const NominalTypeDecl *NTD;

  void addField(reflection::FieldRecordFlags flags,
                Type type, StringRef name) {
    B.addInt32(flags.getRawValue());

    if (!type) {
      B.addInt32(0);
    } else {
      auto genericSig = NTD->getGenericSignature();

      // The standard library's Mirror demangles metadata from field
      // descriptors, so use MangledTypeRefRole::Metadata to ensure
      // runtime metadata is available.
      addTypeRef(type, genericSig, MangledTypeRefRole::Metadata);
    }

    if (IGM.IRGen.Opts.EnableReflectionNames) {
      auto fieldName = IGM.getAddrOfFieldName(name);
      B.addRelativeAddress(fieldName);
    } else {
      B.addInt32(0);
    }
  }

  void addField(Field field) {
    reflection::FieldRecordFlags flags;
    bool isLet = false;

    switch (field.getKind()) {
    case Field::Var: {
      auto var = field.getVarDecl();
      isLet = var->isLet();
      break;
    }
    case Field::MissingMember:
      llvm_unreachable("emitting reflection for type with missing member");
    case Field::DefaultActorStorage:
      flags.setIsArtificial();
      break;
    }
    flags.setIsVar(!isLet);

    addField(flags, field.getInterfaceType(IGM), field.getName());
  }

  void layoutRecord() {
    auto kind = FieldDescriptorKind::Struct;

    if (auto CD = dyn_cast<ClassDecl>(NTD)) {
      auto type = CD->getDeclaredType()->getCanonicalType();
      auto RC = type->getReferenceCounting();
      if (RC == ReferenceCounting::ObjC)
        kind = FieldDescriptorKind::ObjCClass;
      else
        kind = FieldDescriptorKind::Class;
    }

    B.addInt16(uint16_t(kind));
    B.addInt16(FieldRecordSize);

    B.addInt32(getNumFields(NTD));
    forEachField(IGM, NTD, [&](Field field) {
      addField(field);
    });
  }

  void addField(const EnumDecl *enumDecl, const EnumElementDecl *decl,
                bool hasPayload) {
    reflection::FieldRecordFlags flags;
    if (hasPayload && (decl->isIndirect() || enumDecl->isIndirect()))
      flags.setIsIndirectCase();

    addField(flags, decl->getArgumentInterfaceType(),
             decl->getBaseIdentifier().str());
  }

  void layoutEnum() {
    auto enumDecl = cast<EnumDecl>(NTD);
    auto &strategy = irgen::getEnumImplStrategy(
        IGM, enumDecl->getDeclaredTypeInContext()
                     ->getCanonicalType());

    auto kind = FieldDescriptorKind::Enum;

    if (strategy.getElementsWithPayload().size() > 1 &&
        !strategy.needsPayloadSizeInMetadata()) {
      kind = FieldDescriptorKind::MultiPayloadEnum;
    }

    B.addInt16(uint16_t(kind));
    B.addInt16(FieldRecordSize);
    B.addInt32(strategy.getElementsWithPayload().size()
               + strategy.getElementsWithNoPayload().size());

    for (auto enumCase : strategy.getElementsWithPayload()) {
      addField(enumDecl, enumCase.decl, /*has payload*/ true);
    }

    for (auto enumCase : strategy.getElementsWithNoPayload()) {
      addField(enumDecl, enumCase.decl, /*has payload*/ false);
    }
  }

  void layoutProtocol() {
    auto PD = cast<ProtocolDecl>(NTD);
    FieldDescriptorKind Kind;
    if (PD->isObjC())
      Kind = FieldDescriptorKind::ObjCProtocol;
    else if (PD->requiresClass())
      Kind = FieldDescriptorKind::ClassProtocol;
    else
      Kind = FieldDescriptorKind::Protocol;
    B.addInt16(uint16_t(Kind));
    B.addInt16(FieldRecordSize);
    B.addInt32(0);
  }

  void layout() override {
    if (NTD->hasClangNode()) {
      auto *enumDecl = dyn_cast<EnumDecl>(NTD);
      // Structs and namespace-like enums are ok.
      assert(isa<StructDecl>(NTD) || (enumDecl && !enumDecl->hasCases()));
    }

    PrettyStackTraceDecl DebugStack("emitting field type metadata", NTD);
    addNominalRef(NTD);

    auto *CD = dyn_cast<ClassDecl>(NTD);
    auto *PD = dyn_cast<ProtocolDecl>(NTD);
    if (CD && CD->getSuperclass()) {
      addTypeRef(CD->getSuperclass(),
                 CD->getGenericSignature());
    } else if (PD && PD->getDeclaredInterfaceType()->getSuperclass()) {
      addTypeRef(PD->getDeclaredInterfaceType()->getSuperclass(),
                 PD->getGenericSignature());
    } else {
      B.addInt32(0);
    }

    switch (NTD->getKind()) {
    case DeclKind::Class:
    case DeclKind::Struct:
      layoutRecord();
      break;

    case DeclKind::Enum:
      layoutEnum();
      break;

    case DeclKind::Protocol:
      layoutProtocol();
      break;

    default:
      llvm_unreachable("Not a nominal type");
      break;
    }
  }

public:
  FieldTypeMetadataBuilder(IRGenModule &IGM,
                           const NominalTypeDecl * NTD)
    : ReflectionMetadataBuilder(IGM), NTD(NTD) {}

  llvm::GlobalVariable *emit() {
    auto section = IGM.getFieldTypeMetadataSectionName();
    llvm::GlobalVariable *var = ReflectionMetadataBuilder::emit(
        [&](IRGenModule &IGM, ConstantInit definition) -> llvm::Constant * {
          return IGM.getAddrOfReflectionFieldDescriptor(
              NTD->getDeclaredType()->getCanonicalType(), definition);
        },
        section);

    if (IGM.IRGen.Opts.ConditionalRuntimeRecords) {
      // Allow dead-stripping `var` (the reflection record) when the type
      // (NTD) is not referenced.
      auto ref = IGM.getTypeEntityReference(const_cast<NominalTypeDecl *>(NTD));
      IGM.appendLLVMUsedConditionalEntry(var, ref.getValue());
    }

    return var;
  }
};

static bool
deploymentTargetHasRemoteMirrorZeroSizedTypeDescriptorBug(IRGenModule &IGM) {
  auto target = IGM.Context.LangOpts.Target;
  
  if (target.isMacOSX() && target.isMacOSXVersionLT(10, 15, 4)) {
    return true;
  }
  if (target.isiOS() && target.isOSVersionLT(13, 4)) { // includes tvOS
    return true;
  }
  if (target.isWatchOS() && target.isOSVersionLT(6, 2)) {
    return true;
  }
  
  return false;
}

/// Metadata builder that emits a fixed-layout empty type as an empty struct, as
/// a workaround for a RemoteMirror crash in older OSes.
class EmptyStructMetadataBuilder : public ReflectionMetadataBuilder {
  const NominalTypeDecl *NTD;
  
  void layout() override {
    addNominalRef(NTD);
    B.addInt32(0);
    B.addInt16(uint16_t(FieldDescriptorKind::Struct));
    B.addInt16(FieldTypeMetadataBuilder::FieldRecordSize);
    B.addInt32(0);
  }
  
public:
  EmptyStructMetadataBuilder(IRGenModule &IGM,
                             const NominalTypeDecl *NTD)
    : ReflectionMetadataBuilder(IGM), NTD(NTD) {
      assert(IGM.getTypeInfoForUnlowered(
                           NTD->getDeclaredTypeInContext()->getCanonicalType())
                .isKnownEmpty(ResilienceExpansion::Maximal)
             && "should only be used for known empty types");
  }

  llvm::GlobalVariable *emit() {
    auto section = IGM.getFieldTypeMetadataSectionName();
    return ReflectionMetadataBuilder::emit(
      [&](IRGenModule &IGM, ConstantInit definition) -> llvm::Constant* {
        return IGM.getAddrOfReflectionFieldDescriptor(
          NTD->getDeclaredType()->getCanonicalType(), definition);
      },
      section);
  }
};

class FixedTypeMetadataBuilder : public ReflectionMetadataBuilder {
  ModuleDecl *module;
  CanType type;
  const FixedTypeInfo *ti;

public:
  FixedTypeMetadataBuilder(IRGenModule &IGM,
                           CanType builtinType)
    : ReflectionMetadataBuilder(IGM) {
    module = builtinType->getASTContext().TheBuiltinModule;
    type = builtinType;
    ti = &cast<FixedTypeInfo>(IGM.getTypeInfoForUnlowered(builtinType));
  }

  FixedTypeMetadataBuilder(IRGenModule &IGM,
                           const NominalTypeDecl *nominalDecl)
    : ReflectionMetadataBuilder(IGM) {
    module = nominalDecl->getParentModule();
    type = nominalDecl->getDeclaredType()->getCanonicalType();
    ti = &cast<FixedTypeInfo>(IGM.getTypeInfoForUnlowered(
        nominalDecl->getDeclaredTypeInContext()->getCanonicalType()));
  }
  
  void layout() override {
    if (type->isAnyObject()) {
      // AnyObject isn't actually a builtin type; we're emitting it as the old
      // Builtin.UnknownObject type for ABI compatibility.
      B.addRelativeAddress(
          IGM.getAddrOfStringForTypeRef("BO", MangledTypeRefRole::Reflection));
    } else {
      addTypeRef(type, CanGenericSignature());
    }

    B.addInt32(ti->getFixedSize().getValue());

    auto alignment = ti->getFixedAlignment().getValue();
    unsigned bitwiseTakable =
      (ti->isBitwiseTakable(ResilienceExpansion::Minimal) == IsBitwiseTakable
       ? 1 : 0);
    B.addInt32(alignment | (bitwiseTakable << 16));

    B.addInt32(ti->getFixedStride().getValue());
    B.addInt32(ti->getFixedExtraInhabitantCount(IGM));
  }

  llvm::GlobalVariable *emit() {
    auto section = IGM.getBuiltinTypeMetadataSectionName();
    return ReflectionMetadataBuilder::emit(
      [&](IRGenModule &IGM, ConstantInit definition) -> llvm::Constant * {
        return IGM.getAddrOfReflectionBuiltinDescriptor(type, definition);
      },
      section);
  }
};

void IRGenModule::emitBuiltinTypeMetadataRecord(CanType builtinType) {
  FixedTypeMetadataBuilder builder(*this, builtinType);
  builder.emit();
}

/// Builds a constant LLVM struct describing the layout of a fixed-size
/// SIL @box. These look like closure contexts, but without any necessary
/// bindings or metadata sources, and only a single captured value.
class BoxDescriptorBuilder : public ReflectionMetadataBuilder {
  SILType BoxedType;
  CanGenericSignature genericSig;
public:
  BoxDescriptorBuilder(IRGenModule &IGM, SILType BoxedType,
                       CanGenericSignature genericSig)
      : ReflectionMetadataBuilder(IGM), BoxedType(BoxedType),
        genericSig(genericSig) {}

  void layout() override {
    B.addInt32(1);
    B.addInt32(0); // Number of sources
    B.addInt32(0); // Number of generic bindings

    addLoweredTypeRef(BoxedType, genericSig);
  }

  llvm::GlobalVariable *emit() {
    auto section = IGM.getCaptureDescriptorMetadataSectionName();
    return ReflectionMetadataBuilder::emit(None, section);
  }
};

/// Builds a constant LLVM struct describing the layout of a heap closure,
/// the types of its captures, and the sources of metadata if any of the
/// captures are generic.
///
/// For now capture descriptors are only used by out-of-process reflection.
///
/// If the standard library's Mirror type ever gains the ability to reflect
/// closure contexts, we should use MangledTypeRefRole::Metadata below.
class CaptureDescriptorBuilder : public ReflectionMetadataBuilder {
  swift::reflection::MetadataSourceBuilder SourceBuilder;
  CanSILFunctionType OrigCalleeType;
  CanSILFunctionType SubstCalleeType;
  SubstitutionMap Subs;
  const HeapLayout &Layout;

public:
  CaptureDescriptorBuilder(IRGenModule &IGM,
                           CanSILFunctionType OrigCalleeType,
                           CanSILFunctionType SubstCalleeType,
                           SubstitutionMap Subs,
                           const HeapLayout &Layout)
    : ReflectionMetadataBuilder(IGM),
      // TODO: Preserve substitutions, since they may affect representation in
      // the box
      OrigCalleeType(OrigCalleeType->getUnsubstitutedType(IGM.getSILModule())),
      SubstCalleeType(SubstCalleeType->getUnsubstitutedType(IGM.getSILModule())),
      Subs(Subs),
      Layout(Layout) {}

  using MetadataSourceMap
    = std::vector<std::pair<CanType, const reflection::MetadataSource*>>;

  void addMetadataSource(const reflection::MetadataSource *Source) {
    if (Source == nullptr) {
      B.addInt32(0);
    } else {
      SmallString<16> EncodeBuffer;
      llvm::raw_svector_ostream OS(EncodeBuffer);
      MetadataSourceEncoder Encoder(OS);
      Encoder.visit(Source);

      auto EncodedSource =
        IGM.getAddrOfStringForTypeRef(OS.str(), MangledTypeRefRole::Reflection);
      B.addRelativeAddress(EncodedSource);
    }
  }

  /// Give up if we captured an opened existential type. Eventually we
  /// should figure out how to represent this.
  static bool hasOpenedExistential(CanSILFunctionType OrigCalleeType,
                                   const HeapLayout &Layout) {
    if (!OrigCalleeType->isPolymorphic() ||
        OrigCalleeType->isPseudogeneric())
      return false;

    auto &Bindings = Layout.getBindings();
    for (unsigned i = 0; i < Bindings.size(); ++i) {
      // Skip protocol requirements (FIXME: for now?)
      if (Bindings[i].Protocol != nullptr)
        continue;

      if (Bindings[i].TypeParameter->hasOpenedExistential())
        return true;
    }

    auto ElementTypes =
        Layout.getElementTypes().slice(Layout.getIndexAfterBindings());
    for (auto ElementType : ElementTypes) {
      auto SwiftType = ElementType.getASTType();
      if (SwiftType->hasOpenedExistential())
        return true;
    }

    return false;
  }

  /// Slice off the NecessaryBindings struct at the beginning, if it's there.
  /// We'll keep track of how many things are in the bindings struct with its
  /// own count in the capture descriptor.
  ArrayRef<SILType> getElementTypes() {
    return Layout.getElementTypes().slice(Layout.getIndexAfterBindings());
  }

  /// Build a map from generic parameter -> source of its metadata at runtime.
  ///
  /// If the callee that we are partially applying to create a box/closure
  /// isn't generic, then the map is empty.
  MetadataSourceMap getMetadataSourceMap() {
    MetadataSourceMap SourceMap;

    // Generic parameters of pseudogeneric functions do not have
    // runtime metadata.
    if (!OrigCalleeType->isPolymorphic() ||
        OrigCalleeType->isPseudogeneric())
      return SourceMap;

    // Any generic parameters that are not fulfilled are passed in via the
    // bindings. Structural types are decomposed, so emit the contents of
    // the bindings structure directly.
    auto &Bindings = Layout.getBindings();
    for (unsigned i = 0; i < Bindings.size(); ++i) {
      // Skip protocol requirements (FIXME: for now?)
      if (Bindings[i].Protocol != nullptr)
        continue;

      auto Source = SourceBuilder.createClosureBinding(i);
      auto BindingType = Bindings[i].TypeParameter;
      auto InterfaceType = BindingType->mapTypeOutOfContext();
      SourceMap.push_back({InterfaceType->getCanonicalType(), Source});
    }

    // Check if any requirements were fulfilled by metadata stored inside a
    // captured value.

    enumerateGenericParamFulfillments(IGM, OrigCalleeType,
        [&](CanType GenericParam,
            const irgen::MetadataSource &Source,
            const MetadataPath &Path) {

      const reflection::MetadataSource *Root;
      switch (Source.getKind()) {
      case irgen::MetadataSource::Kind::SelfMetadata:
      case irgen::MetadataSource::Kind::SelfWitnessTable:
        // Handled as part of bindings
        return;

      case irgen::MetadataSource::Kind::GenericLValueMetadata:
        // FIXME?
        return;

      case irgen::MetadataSource::Kind::ClassPointer:
        Root = SourceBuilder.createReferenceCapture(Source.getParamIndex());
        break;

      case irgen::MetadataSource::Kind::Metadata:
        Root = SourceBuilder.createMetadataCapture(Source.getParamIndex());
        break;

      case irgen::MetadataSource::Kind::ErasedTypeMetadata:
        // Fixed in the function body
        break;
      }

      // The metadata might be reached via a non-trivial path (eg,
      // dereferencing an isa pointer or a generic argument). Record
      // the path. We assume captured values map 1-1 with function
      // parameters.
      auto Src = Path.getMetadataSource(SourceBuilder, Root);

      auto SubstType = GenericParam.subst(Subs);
      auto InterfaceType = SubstType->mapTypeOutOfContext();
      SourceMap.push_back({InterfaceType->getCanonicalType(), Src});
    });

    return SourceMap;
  }

  /// Get the interface types of all of the captured values, mapped out of the
  /// context of the callee we're partially applying.
  std::vector<SILType> getCaptureTypes() {
    std::vector<SILType> CaptureTypes;

    for (auto ElementType : getElementTypes()) {
      auto SwiftType = ElementType.getASTType();

      // Erase pseudogeneric captures down to AnyObject.
      if (OrigCalleeType->isPseudogeneric()) {
        SwiftType = SwiftType.transform([&](Type t) -> Type {
          if (auto *archetype = t->getAs<ArchetypeType>()) {
            assert(archetype->requiresClass() && "don't know what to do");
            return IGM.Context.getAnyObjectType();
          }
          return t;
        })->getCanonicalType();
      }
      
      // TODO: We should preserve substitutions in SILFunctionType captures
      // once the runtime MetadataReader can understand them, since they can
      // affect representation.
      //
      // For now, eliminate substitutions from the capture representation.
      SwiftType =
        SwiftType->replaceSubstitutedSILFunctionTypesWithUnsubstituted(IGM.getSILModule())
                 ->getCanonicalType();

      CaptureTypes.push_back(SILType::getPrimitiveObjectType(SwiftType));
    }

    return CaptureTypes;
  }

  void layout() override {
    auto CaptureTypes = getCaptureTypes();
    auto MetadataSources = getMetadataSourceMap();

    B.addInt32(CaptureTypes.size());
    B.addInt32(MetadataSources.size());
    B.addInt32(Layout.getBindings().size());

    auto sig =
      OrigCalleeType->getInvocationGenericSignature().getCanonicalSignature();

    // Now add typerefs of all of the captures.
    for (auto CaptureType : CaptureTypes) {
      addLoweredTypeRef(CaptureType, sig);
    }

    // Add the pairs that make up the generic param -> metadata source map
    // to the struct.
    for (auto GenericAndSource : MetadataSources) {
      auto GenericParam = GenericAndSource.first->getCanonicalType();
      auto Source = GenericAndSource.second;

      addTypeRef(GenericParam, sig);
      addMetadataSource(Source);
    }
  }

  llvm::GlobalVariable *emit() {
    auto section = IGM.getCaptureDescriptorMetadataSectionName();
    return ReflectionMetadataBuilder::emit(None, section);
  }
};

static std::string getReflectionSectionName(IRGenModule &IGM,
                                            StringRef LongName,
                                            StringRef FourCC) {
  SmallString<50> SectionName;
  llvm::raw_svector_ostream OS(SectionName);
  switch (IGM.TargetInfo.OutputObjectFormat) {
  case llvm::Triple::GOFF:
  case llvm::Triple::UnknownObjectFormat:
    llvm_unreachable("unknown object format");
  case llvm::Triple::XCOFF:
  case llvm::Triple::COFF:
    assert(FourCC.size() <= 4 &&
           "COFF section name length must be <= 8 characters");
    OS << ".sw5" << FourCC << "$B";
    break;
  case llvm::Triple::ELF:
  case llvm::Triple::Wasm:
    OS << "swift5_" << LongName;
    break;
  case llvm::Triple::MachO:
    assert(LongName.size() <= 7 &&
           "Mach-O section name length must be <= 16 characters");
    OS << "__TEXT,__swift5_" << LongName << ", regular, no_dead_strip";
    break;
  }
  return std::string(OS.str());
}

const char *IRGenModule::getFieldTypeMetadataSectionName() {
  if (FieldTypeSection.empty())
    FieldTypeSection = getReflectionSectionName(*this, "fieldmd", "flmd");
  return FieldTypeSection.c_str();
}

const char *IRGenModule::getBuiltinTypeMetadataSectionName() {
  if (BuiltinTypeSection.empty())
    BuiltinTypeSection = getReflectionSectionName(*this, "builtin", "bltn");
  return BuiltinTypeSection.c_str();
}

const char *IRGenModule::getAssociatedTypeMetadataSectionName() {
  if (AssociatedTypeSection.empty())
    AssociatedTypeSection = getReflectionSectionName(*this, "assocty", "asty");
  return AssociatedTypeSection.c_str();
}

const char *IRGenModule::getCaptureDescriptorMetadataSectionName() {
  if (CaptureDescriptorSection.empty())
    CaptureDescriptorSection = getReflectionSectionName(*this, "capture", "cptr");
  return CaptureDescriptorSection.c_str();
}

const char *IRGenModule::getReflectionStringsSectionName() {
  if (ReflectionStringsSection.empty())
    ReflectionStringsSection = getReflectionSectionName(*this, "reflstr", "rfst");
  return ReflectionStringsSection.c_str();
}

const char *IRGenModule::getReflectionTypeRefSectionName() {
  if (ReflectionTypeRefSection.empty())
    ReflectionTypeRefSection = getReflectionSectionName(*this, "typeref", "tyrf");
  return ReflectionTypeRefSection.c_str();
}

llvm::Constant *IRGenModule::getAddrOfFieldName(StringRef Name) {
  auto &entry = FieldNames[Name];
  if (entry.second)
    return entry.second;

  entry = createStringConstant(Name, /*willBeRelativelyAddressed*/ true,
                               getReflectionStringsSectionName());
  disableAddressSanitizer(*this, entry.first);
  return entry.second;
}

llvm::Constant *
IRGenModule::getAddrOfBoxDescriptor(SILType BoxedType,
                                    CanGenericSignature genericSig) {
  if (!IRGen.Opts.EnableReflectionMetadata)
    return llvm::Constant::getNullValue(CaptureDescriptorPtrTy);

  BoxDescriptorBuilder builder(*this, BoxedType, genericSig);
  auto var = builder.emit();

  return llvm::ConstantExpr::getBitCast(var, CaptureDescriptorPtrTy);
}

llvm::Constant *
IRGenModule::getAddrOfCaptureDescriptor(SILFunction &Caller,
                                        CanSILFunctionType OrigCalleeType,
                                        CanSILFunctionType SubstCalleeType,
                                        SubstitutionMap Subs,
                                        const HeapLayout &Layout) {
  if (!IRGen.Opts.EnableReflectionMetadata)
    return llvm::Constant::getNullValue(CaptureDescriptorPtrTy);

  if (CaptureDescriptorBuilder::hasOpenedExistential(OrigCalleeType, Layout))
    return llvm::Constant::getNullValue(CaptureDescriptorPtrTy);

  CaptureDescriptorBuilder builder(*this,
                                   OrigCalleeType, SubstCalleeType, Subs,
                                   Layout);
  auto var = builder.emit();
  return llvm::ConstantExpr::getBitCast(var, CaptureDescriptorPtrTy);
}

void IRGenModule::
emitAssociatedTypeMetadataRecord(const RootProtocolConformance *conformance) {
  auto normalConf = dyn_cast<NormalProtocolConformance>(conformance);
  if (!normalConf)
    return;

  if (!IRGen.Opts.EnableReflectionMetadata)
    return;

  SmallVector<std::pair<StringRef, CanType>, 2> AssociatedTypes;

  auto collectTypeWitness = [&](const AssociatedTypeDecl *AssocTy,
                                Type Replacement,
                                const TypeDecl *TD) -> bool {
    AssociatedTypes.push_back({
      AssocTy->getNameStr(),
      Replacement->getCanonicalType()
    });
    return false;
  };

  normalConf->forEachTypeWitness(collectTypeWitness);

  // If there are no associated types, don't bother emitting any
  // metadata.
  if (AssociatedTypes.empty())
    return;

  AssociatedTypeMetadataBuilder builder(*this, normalConf, AssociatedTypes);
  builder.emit();
}

void IRGenModule::emitBuiltinReflectionMetadata() {
  if (getSwiftModule()->isStdlibModule()) {
    BuiltinTypes.insert(Context.TheNativeObjectType);
    BuiltinTypes.insert(Context.getAnyObjectType());
    BuiltinTypes.insert(Context.TheBridgeObjectType);
    BuiltinTypes.insert(Context.TheRawPointerType);
    BuiltinTypes.insert(Context.TheUnsafeValueBufferType);

    // This would not be necessary if RawPointer had the same set of
    // extra inhabitants as these. But maybe it's best not to codify
    // that in the ABI anyway.
    CanType thinFunction = CanFunctionType::get(
      {}, Context.TheEmptyTupleType,
      AnyFunctionType::ExtInfo().withRepresentation(
          FunctionTypeRepresentation::Thin));
    BuiltinTypes.insert(thinFunction);

    CanType anyMetatype = CanExistentialMetatypeType::get(
      Context.TheAnyType);
    BuiltinTypes.insert(anyMetatype);
  }

  for (auto builtinType : BuiltinTypes)
    emitBuiltinTypeMetadataRecord(builtinType);
}

void IRGenerator::emitBuiltinReflectionMetadata() {
  for (auto &m : *this) {
    m.second->emitBuiltinReflectionMetadata();
  }
}

void IRGenModule::emitFieldDescriptor(const NominalTypeDecl *D) {
  if (!IRGen.Opts.EnableReflectionMetadata)
    return;

  auto T = D->getDeclaredTypeInContext()->getCanonicalType();

  bool needsOpaqueDescriptor = false;
  bool needsFieldDescriptor = true;

  if (auto *ED = dyn_cast<EnumDecl>(D)) {
    auto &strategy = getEnumImplStrategy(*this, T);

    // @objc enums never have generic parameters or payloads,
    // and lower as their raw type.
    if (!strategy.isReflectable()) {
      needsOpaqueDescriptor = true;
      needsFieldDescriptor = false;
    }

    // If this is a fixed-size multi-payload enum, we have to emit a descriptor
    // with the size and alignment of the type, because the reflection library
    // cannot derive this information at runtime.
    if (strategy.getElementsWithPayload().size() > 1 &&
        !strategy.needsPayloadSizeInMetadata()) {
      needsOpaqueDescriptor = true;
    }
  }

  if (auto *SD = dyn_cast<StructDecl>(D)) {
    if (SD->hasClangNode())
      needsOpaqueDescriptor = true;
  }

  // If the type has custom @_alignment, emit a fixed record with the
  // alignment since remote mirrors will need to treat the type as opaque.
  //
  // Note that we go on to also emit a field descriptor in this case,
  // since in-process reflection only cares about the types of the fields
  // and does not independently re-derive the layout.
  if (D->getAttrs().hasAttribute<AlignmentAttr>()) {
    auto &TI = getTypeInfoForUnlowered(T);
    if (isa<FixedTypeInfo>(TI)) {
      needsOpaqueDescriptor = true;
    }
  }

  if (needsOpaqueDescriptor) {
    // Work around an issue in the RemoteMirror library that ships in
    // macOS 10.15/iOS 13 and earlier that causes it to crash on a
    // BuiltinTypeDescriptor with zero size. If the type has zero size, emit it
    // as an empty struct instead, which will have the same impact on the
    // encoded type layout.
    auto &TI = getTypeInfoForUnlowered(T);
    if (deploymentTargetHasRemoteMirrorZeroSizedTypeDescriptorBug(*this)
        && TI.isKnownEmpty(ResilienceExpansion::Maximal)) {
      EmptyStructMetadataBuilder builder(*this, D);
      builder.emit();
      return;
    }
    
    FixedTypeMetadataBuilder builder(*this, D);
    builder.emit();
  }

  if (needsFieldDescriptor) {
    FieldTypeMetadataBuilder builder(*this, D);
    builder.emit();
  }
}

void IRGenModule::emitReflectionMetadataVersion() {
  auto Init =
    llvm::ConstantInt::get(Int16Ty, SWIFT_REFLECTION_METADATA_VERSION);
  auto Version = new llvm::GlobalVariable(Module, Int16Ty, /*constant*/ true,
                                          llvm::GlobalValue::LinkOnceODRLinkage,
                                          Init,
                                          "__swift_reflection_version");
  ApplyIRLinkage(IRLinkage::InternalLinkOnceODR).to(Version);
  addUsedGlobal(Version);
}

void IRGenerator::emitReflectionMetadataVersion() {
  for (auto &m : *this) {
    m.second->emitReflectionMetadataVersion();
  }
}
