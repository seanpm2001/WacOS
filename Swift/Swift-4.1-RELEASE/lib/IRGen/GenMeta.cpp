//===--- GenMeta.cpp - IR generation for metadata constructs --------------===//
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
//  This file implements IR generation for type metadata constructs.
//
//===----------------------------------------------------------------------===//

#include "swift/ABI/MetadataValues.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/CanTypeVisitor.h"
#include "swift/AST/ExistentialLayout.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Attr.h"
#include "swift/AST/IRGenOptions.h"
#include "swift/AST/SubstitutionMap.h"
#include "swift/AST/Types.h"
#include "swift/ClangImporter/ClangModule.h"
#include "swift/IRGen/Linking.h"
#include "swift/Runtime/Metadata.h"
#include "swift/SIL/FormalLinkage.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/TypeLowering.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"

#include "Address.h"
#include "Callee.h"
#include "ClassMetadataVisitor.h"
#include "ConstantBuilder.h"
#include "EnumMetadataVisitor.h"
#include "FixedTypeInfo.h"
#include "GenArchetype.h"
#include "GenClass.h"
#include "GenDecl.h"
#include "GenPoly.h"
#include "GenStruct.h"
#include "GenValueWitness.h"
#include "HeapTypeInfo.h"
#include "IRGenDebugInfo.h"
#include "IRGenMangler.h"
#include "IRGenModule.h"
#include "MetadataLayout.h"
#include "ProtocolInfo.h"
#include "ScalarTypeInfo.h"
#include "StructLayout.h"
#include "StructMetadataVisitor.h"

#include "GenMeta.h"

using namespace swift;
using namespace irgen;

static Address emitAddressOfMetadataSlotAtIndex(IRGenFunction &IGF,
                                                llvm::Value *metadata,
                                                int index,
                                                llvm::Type *objectTy) {
  // Require the metadata to be some type that we recognize as a
  // metadata pointer.
  assert(metadata->getType() == IGF.IGM.TypeMetadataPtrTy);

  return IGF.emitAddressAtOffset(metadata,
                                 Offset(index * IGF.IGM.getPointerSize()),
                                 objectTy, IGF.IGM.getPointerAlignment());
}

/// Emit a load from the given metadata at a constant index.
static llvm::LoadInst *emitLoadFromMetadataAtIndex(IRGenFunction &IGF,
                                                   llvm::Value *metadata,
                                                   int index,
                                                   llvm::Type *objectTy,
                                             const llvm::Twine &suffix = "") {
  Address slot =
    emitAddressOfMetadataSlotAtIndex(IGF, metadata, index, objectTy);

  // Load.
  return IGF.Builder.CreateLoad(slot, metadata->getName() + suffix);
}

static Address createPointerSizedGEP(IRGenFunction &IGF,
                                     Address base,
                                     Size offset) {
  return IGF.Builder.CreateConstArrayGEP(base,
                                         IGF.IGM.getOffsetInWords(offset),
                                         offset);
}

// FIXME: willBeRelativelyAddressed is only needed to work around an ld64 bug
// resolving relative references to coalesceable symbols.
// It should be removed when fixed. rdar://problem/22674524
static llvm::Constant *getMangledTypeName(IRGenModule &IGM, CanType type,
                                      bool willBeRelativelyAddressed = false) {
  IRGenMangler Mangler;
  std::string Name = Mangler.mangleTypeForMetadata(type);
  return IGM.getAddrOfGlobalString(Name, willBeRelativelyAddressed);
}

llvm::Value *irgen::emitObjCMetadataRefForMetadata(IRGenFunction &IGF,
                                                   llvm::Value *classPtr) {
  assert(IGF.IGM.Context.LangOpts.EnableObjCInterop);
  classPtr = IGF.Builder.CreateBitCast(classPtr, IGF.IGM.ObjCClassPtrTy);
  
  // Fetch the metadata for that class.
  auto call = IGF.Builder.CreateCall(IGF.IGM.getGetObjCClassMetadataFn(),
                                     classPtr);
  call->setDoesNotThrow();
  call->setDoesNotAccessMemory();
  return call;
}

/// Emit a reference to the Swift metadata for an Objective-C class.
static llvm::Value *emitObjCMetadataRef(IRGenFunction &IGF,
                                        ClassDecl *theClass) {
  // Derive a pointer to the Objective-C class.
  auto classPtr = emitObjCHeapMetadataRef(IGF, theClass);
  
  return emitObjCMetadataRefForMetadata(IGF, classPtr);
}

namespace {
  /// A structure for collecting generic arguments for emitting a
  /// nominal metadata reference.  The structure produced here is
  /// consumed by swift_getGenericMetadata() and must correspond to
  /// the fill operations that the compiler emits for the bound decl.
  struct GenericArguments {
    /// The values to use to initialize the arguments structure.
    SmallVector<llvm::Value *, 8> Values;
    SmallVector<llvm::Type *, 8> Types;

    static unsigned getNumGenericArguments(IRGenModule &IGM,
                                           NominalTypeDecl *nominal) {
      GenericTypeRequirements requirements(IGM, nominal);
      return requirements.getNumTypeRequirements();
    }

    void collectTypes(IRGenModule &IGM, NominalTypeDecl *nominal) {
      GenericTypeRequirements requirements(IGM, nominal);
      collectTypes(IGM, requirements);
    }

    void collectTypes(IRGenModule &IGM,
                      const GenericTypeRequirements &requirements) {
      for (auto &requirement : requirements.getRequirements()) {
        if (requirement.Protocol) {
          Types.push_back(IGM.WitnessTablePtrTy);
        } else {
          Types.push_back(IGM.TypeMetadataPtrTy);
        }
      }
    }

    void collect(IRGenFunction &IGF, CanType type) {
      auto *decl = type.getNominalOrBoundGenericNominal();
      GenericTypeRequirements requirements(IGF.IGM, decl);

      auto subs =
        type->getContextSubstitutionMap(IGF.IGM.getSwiftModule(), decl);
      requirements.enumerateFulfillments(IGF.IGM, subs,
                                [&](unsigned reqtIndex, CanType type,
                                    Optional<ProtocolConformanceRef> conf) {
        if (conf) {
          Values.push_back(emitWitnessTableRef(IGF, type, *conf));
        } else {
          Values.push_back(IGF.emitTypeMetadataRef(type));
        }
      });

      collectTypes(IGF.IGM, decl);
      assert(Types.size() == Values.size());
    }
  };
} // end anonymous namespace

/// Given an array of polymorphic arguments as might be set up by
/// GenericArguments, bind the polymorphic parameters.
static void emitPolymorphicParametersFromArray(IRGenFunction &IGF,
                                               NominalTypeDecl *typeDecl,
                                               Address array) {
  GenericTypeRequirements requirements(IGF.IGM, typeDecl);

  array = IGF.Builder.CreateElementBitCast(array, IGF.IGM.TypeMetadataPtrTy);

  auto getInContext = [&](CanType type) -> CanType {
    return typeDecl->mapTypeIntoContext(type)
             ->getCanonicalType();
  };

  // Okay, bind everything else from the context.
  requirements.bindFromBuffer(IGF, array, getInContext);
}

static bool isTypeErasedGenericClass(NominalTypeDecl *ntd) {
  // ObjC classes are type erased.
  // TODO: Unless they have magic methods...
  if (auto clas = dyn_cast<ClassDecl>(ntd))
    return clas->hasClangNode() && clas->isGenericContext();
  return false;
}

static bool isTypeErasedGenericClassType(CanType type) {
  if (auto nom = type->getAnyNominal())
    return isTypeErasedGenericClass(nom);
  return false;
}

// Get the type that exists at runtime to represent a compile-time type.
CanType
irgen::getRuntimeReifiedType(IRGenModule &IGM, CanType type) {
  return CanType(type.transform([&](Type t) -> Type {
    if (isTypeErasedGenericClassType(CanType(t))) {
      return t->getAnyNominal()->getDeclaredType()->getCanonicalType();
    }
    return t;
  }));
}

/// Attempts to return a constant heap metadata reference for a
/// class type.  This is generally only valid for specific kinds of
/// ObjC reference, like superclasses or category references.
llvm::Constant *irgen::tryEmitConstantHeapMetadataRef(IRGenModule &IGM,
                                                      CanType type,
                                              bool allowDynamicUninitialized) {
  auto theDecl = type->getClassOrBoundGenericClass();
  assert(theDecl && "emitting constant heap metadata ref for non-class type?");

  // If the class must not require dynamic initialization --- e.g. if it
  // is a super reference --- then respect everything that might impose that.
  if (!allowDynamicUninitialized) {
    if (doesClassMetadataRequireDynamicInitialization(IGM, theDecl))
      return nullptr;

  // Otherwise, just respect genericity.
  } else if (theDecl->isGenericContext() && !isTypeErasedGenericClass(theDecl)){
    return nullptr;
  }

  // For imported classes, use the ObjC class symbol.
  // This incidentally cannot coincide with most of the awkward cases, like
  // having parent metadata.
  if (!hasKnownSwiftMetadata(IGM, theDecl))
    return IGM.getAddrOfObjCClass(theDecl, NotForDefinition);

  return IGM.getAddrOfTypeMetadata(type, false);
}

/// Attempts to return a constant type metadata reference for a
/// nominal type.
ConstantReference
irgen::tryEmitConstantTypeMetadataRef(IRGenModule &IGM, CanType type,
                                      SymbolReferenceKind refKind) {
  if (!isTypeMetadataAccessTrivial(IGM, type))
    return ConstantReference();

  return IGM.getAddrOfTypeMetadata(type, false, refKind);
}

/// Emit a reference to an ObjC class.  In general, the only things
/// you're allowed to do with the address of an ObjC class symbol are
/// (1) send ObjC messages to it (in which case the message will be
/// forwarded to the real class, if one exists) or (2) put it in
/// various data sections where the ObjC runtime will properly arrange
/// things.  Therefore, we must typically force the initialization of
/// a class when emitting a reference to it.
llvm::Value *irgen::emitObjCHeapMetadataRef(IRGenFunction &IGF,
                                            ClassDecl *theClass,
                                            bool allowUninitialized) {
  // If the class is visible only through the Objective-C runtime, form the
  // appropriate runtime call.
  if (theClass->getForeignClassKind() == ClassDecl::ForeignKind::RuntimeOnly) {
    SmallString<64> scratch;
    auto className =
        IGF.IGM.getAddrOfGlobalString(theClass->getObjCRuntimeName(scratch));
    return IGF.Builder.CreateCall(IGF.IGM.getLookUpClassFn(), className);
  }

  assert(!theClass->isForeign());

  Address classRef = IGF.IGM.getAddrOfObjCClassRef(theClass);
  auto classObject = IGF.Builder.CreateLoad(classRef);
  if (allowUninitialized) return classObject;

  // TODO: memoize this the same way that we memoize Swift type metadata?
  return IGF.Builder.CreateCall(IGF.IGM.getGetInitializedObjCClassFn(),
                                classObject);
}

/// Emit a reference to the type metadata for a foreign type.
static llvm::Value *emitForeignTypeMetadataRef(IRGenFunction &IGF,
                                               CanType type) {
  llvm::Value *candidate = IGF.IGM.getAddrOfForeignTypeMetadataCandidate(type);
  auto call = IGF.Builder.CreateCall(IGF.IGM.getGetForeignTypeMetadataFn(),
                                candidate);
  call->addAttribute(llvm::AttributeList::FunctionIndex,
                     llvm::Attribute::NoUnwind);
  call->addAttribute(llvm::AttributeList::FunctionIndex,
                     llvm::Attribute::ReadNone);
  return call;
}

/// Returns a metadata reference for a nominal type.
///
/// This is only valid in a couple of special cases:
/// 1) The nominal type is generic, in which case we emit a call to the
///    generic metadata accessor function, which must be defined separately.
/// 2) The nominal type is a value type with a fixed size from this
///    resilience domain, in which case we can reference the constant
///    metadata directly.
///
/// In any other case, a metadata accessor should be called instead.
static llvm::Value *emitNominalMetadataRef(IRGenFunction &IGF,
                                           NominalTypeDecl *theDecl,
                                           CanType theType) {
  assert(!isa<ProtocolDecl>(theDecl));

  if (!theDecl->isGenericContext()) {
    assert(!IGF.IGM.isResilient(theDecl, ResilienceExpansion::Maximal));
    // TODO: If Obj-C interop is off, we can relax this to allow referencing
    // class metadata too.
    assert(isa<StructDecl>(theDecl) || isa<EnumDecl>(theDecl));
    return IGF.IGM.getAddrOfTypeMetadata(theType, false);
  }

  // We are applying generic parameters to a generic type.
  assert(theType->isSpecialized() &&
         theType->getAnyNominal() == theDecl);

  // Check to see if we've maybe got a local reference already.
  if (auto cache = IGF.tryGetLocalTypeData(theType,
                                           LocalTypeDataKind::forTypeMetadata()))
    return cache;

  // Grab the substitutions.
  GenericArguments genericArgs;
  genericArgs.collect(IGF, theType);
  assert((genericArgs.Values.size() > 0 ||
          theDecl->getGenericSignature()->areAllParamsConcrete())
         && "no generic args?!");

  // Call the generic metadata accessor function.
  llvm::Function *accessor =
      IGF.IGM.getAddrOfGenericTypeMetadataAccessFunction(theDecl,
                                                         genericArgs.Types,
                                                         NotForDefinition);

  auto result = IGF.Builder.CreateCall(accessor, genericArgs.Values);
  result->setDoesNotThrow();
  result->addAttribute(llvm::AttributeList::FunctionIndex,
                       llvm::Attribute::ReadNone);

  IGF.setScopedLocalTypeData(theType, LocalTypeDataKind::forTypeMetadata(),
                             result);
  return result;
}


bool irgen::hasKnownSwiftMetadata(IRGenModule &IGM, CanType type) {
  if (ClassDecl *theClass = type.getClassOrBoundGenericClass()) {
    return hasKnownSwiftMetadata(IGM, theClass);
  }

  if (auto archetype = dyn_cast<ArchetypeType>(type)) {
    if (auto superclass = archetype->getSuperclass()) {
      return hasKnownSwiftMetadata(IGM, superclass->getCanonicalType());
    }
  }

  // Class existentials, etc.
  return false;
}

/// Is the given class known to have Swift-compatible metadata?
bool irgen::hasKnownSwiftMetadata(IRGenModule &IGM, ClassDecl *theClass) {
  // For now, the fact that a declaration was not implemented in Swift
  // is enough to conclusively force us into a slower path.
  // Eventually we might have an attribute here or something based on
  // the deployment target.
  return theClass->hasKnownSwiftImplementation();
}

/// Is it basically trivial to access the given metadata?  If so, we don't
/// need a cache variable in its accessor.
bool irgen::isTypeMetadataAccessTrivial(IRGenModule &IGM, CanType type) {
  assert(!type->hasArchetype());

  // Value type metadata only requires dynamic initialization on first
  // access if it contains a resilient type.
  if (isa<StructType>(type) || isa<EnumType>(type)) {
    auto nominalType = cast<NominalType>(type);
    auto *nominalDecl = nominalType->getDecl();

    // Imported type metadata always requires an accessor.
    if (isa<ClangModuleUnit>(nominalDecl->getModuleScopeContext()))
      return false;

    // Generic type metadata always requires an accessor.
    if (nominalDecl->isGenericContext())
      return false;

    // Resiliently-sized metadata access always requires an accessor.
    return (IGM.getTypeInfoForUnlowered(type).isFixedSize());
  }

  // The empty tuple type has a singleton metadata.
  if (auto tuple = dyn_cast<TupleType>(type))
    return tuple->getNumElements() == 0;

  // The builtin types generally don't require metadata, but some of them
  // have nodes in the runtime anyway.
  if (isa<BuiltinType>(type))
    return true;

  // SIL box types are artificial, but for the purposes of dynamic layout,
  // we use the NativeObject metadata.
  if (isa<SILBoxType>(type))
    return true;

  // DynamicSelfType is actually local.
  if (type->hasDynamicSelfType())
    return true;

  return false;
}

static bool hasRequiredTypeMetadataAccessFunction(NominalTypeDecl *typeDecl) {
  // This needs to be kept in sync with getTypeMetadataStrategy.

  if (isa<ProtocolDecl>(typeDecl))
    return false;

  switch (getDeclLinkage(typeDecl)) {
  case FormalLinkage::PublicUnique:
  case FormalLinkage::HiddenUnique:
  case FormalLinkage::Private:
    return true;

  case FormalLinkage::PublicNonUnique:
  case FormalLinkage::HiddenNonUnique:
    return false;
  }
  llvm_unreachable("bad formal linkage");

}

/// Return the standard access strategy for getting a non-dependent
/// type metadata object.
MetadataAccessStrategy irgen::getTypeMetadataAccessStrategy(CanType type) {
  // We should not be emitting accessors for partially-substituted
  // generic types.
  assert(!type->hasArchetype());

  // Non-generic structs, enums, and classes are special cases.
  //
  // Note that while protocol types don't have a metadata pattern,
  // we still require an accessor since we actually want to get
  // the metadata for the existential type.
  //
  // This needs to kept in sync with hasRequiredTypeMetadataAccessPattern.
  auto nominal = type->getAnyNominal();
  if (nominal && !isa<ProtocolDecl>(nominal)) {
    // Metadata accessors for fully-substituted generic types are
    // emitted with shared linkage.
    if (nominal->isGenericContext() && !nominal->isObjC()) {
      if (type->isSpecialized())
        return MetadataAccessStrategy::NonUniqueAccessor;
      assert(type->hasUnboundGenericType());
    }

    // If the type doesn't guarantee that it has an access function,
    // we might have to use a non-unique accessor.

    // Everything else requires accessors.
    switch (getDeclLinkage(nominal)) {
    case FormalLinkage::PublicUnique:
      return MetadataAccessStrategy::PublicUniqueAccessor;
    case FormalLinkage::HiddenUnique:
      return MetadataAccessStrategy::HiddenUniqueAccessor;
    case FormalLinkage::Private:
      return MetadataAccessStrategy::PrivateAccessor;

    case FormalLinkage::PublicNonUnique:
    case FormalLinkage::HiddenNonUnique:
      return MetadataAccessStrategy::NonUniqueAccessor;
    }
    llvm_unreachable("bad formal linkage");
  }

  // Everything else requires a shared accessor function.
  return MetadataAccessStrategy::NonUniqueAccessor;
}

/// Emit a string encoding the labels in the given tuple type.
static llvm::Constant *getTupleLabelsString(IRGenModule &IGM,
                                            CanTupleType type) {
  bool hasLabels = false;
  llvm::SmallString<128> buffer;
  for (auto &elt : type->getElements()) {
    if (elt.hasName()) {
      hasLabels = true;
      buffer.append(elt.getName().str());
    }

    // Each label is space-terminated.
    buffer += ' ';
  }

  // If there are no labels, use a null pointer.
  if (!hasLabels) {
    return llvm::ConstantPointerNull::get(IGM.Int8PtrTy);
  }

  // Otherwise, create a new string literal.
  // This method implicitly adds a null terminator.
  return IGM.getAddrOfGlobalString(buffer);
}

namespace {
  /// A visitor class for emitting a reference to a metatype object.
  /// This implements a "raw" access, useful for implementing cache
  /// functions or for implementing dependent accesses.
  ///
  /// If the access requires runtime initialization, that initialization
  /// must be dependency-ordered-before any load that carries a dependency
  /// from the resulting metadata pointer.
  class EmitTypeMetadataRef
    : public CanTypeVisitor<EmitTypeMetadataRef, llvm::Value *> {
  private:
    IRGenFunction &IGF;
  public:
    EmitTypeMetadataRef(IRGenFunction &IGF) : IGF(IGF) {}

#define TREAT_AS_OPAQUE(KIND)                          \
    llvm::Value *visit##KIND##Type(KIND##Type *type) { \
      return visitOpaqueType(CanType(type));           \
    }
    TREAT_AS_OPAQUE(BuiltinInteger)
    TREAT_AS_OPAQUE(BuiltinFloat)
    TREAT_AS_OPAQUE(BuiltinVector)
    TREAT_AS_OPAQUE(BuiltinRawPointer)
#undef TREAT_AS_OPAQUE

    llvm::Value *emitDirectMetadataRef(CanType type) {
      return IGF.IGM.getAddrOfTypeMetadata(type,
                                           /*pattern*/ false);
    }

    /// The given type should use opaque type info.  We assume that
    /// the runtime always provides an entry for such a type;  right
    /// now, that mapping is as one of the power-of-two integer types.
    llvm::Value *visitOpaqueType(CanType type) {
      auto &opaqueTI = cast<FixedTypeInfo>(IGF.IGM.getTypeInfoForLowered(type));
      unsigned numBits = opaqueTI.getFixedSize().getValueInBits();
      if (!llvm::isPowerOf2_32(numBits))
        numBits = llvm::NextPowerOf2(numBits);
      auto intTy = BuiltinIntegerType::get(numBits, IGF.IGM.Context);
      return emitDirectMetadataRef(CanType(intTy));
    }

    llvm::Value *visitBuiltinNativeObjectType(CanBuiltinNativeObjectType type) {
      return emitDirectMetadataRef(type);
    }

    llvm::Value *visitBuiltinBridgeObjectType(CanBuiltinBridgeObjectType type) {
      return emitDirectMetadataRef(type);
    }

    llvm::Value *visitBuiltinUnknownObjectType(CanBuiltinUnknownObjectType type) {
      return emitDirectMetadataRef(type);
    }

    llvm::Value *visitBuiltinUnsafeValueBufferType(
                                        CanBuiltinUnsafeValueBufferType type) {
      return emitDirectMetadataRef(type);
    }

    llvm::Value *visitNominalType(CanNominalType type) {
      assert(!type->isExistentialType());
      return emitNominalMetadataRef(IGF, type->getDecl(), type);
    }

    llvm::Value *visitBoundGenericType(CanBoundGenericType type) {
      assert(!type->isExistentialType());
      return emitNominalMetadataRef(IGF, type->getDecl(), type);
    }

    llvm::Value *visitTupleType(CanTupleType type) {
      if (auto cached = tryGetLocal(type))
        return cached;

      // I think the sanest thing to do here is drop labels, but maybe
      // that's not correct.  If so, that's really unfortunate in a
      // lot of ways.

      // Er, varargs bit?  Should that go in?


      switch (type->getNumElements()) {
      case 0: {// Special case the empty tuple, just use the global descriptor.
        llvm::Constant *fullMetadata = IGF.IGM.getEmptyTupleMetadata();
        llvm::Constant *indices[] = {
          llvm::ConstantInt::get(IGF.IGM.Int32Ty, 0),
          llvm::ConstantInt::get(IGF.IGM.Int32Ty, 1)
        };
        return llvm::ConstantExpr::getInBoundsGetElementPtr(
            /*Ty=*/nullptr, fullMetadata, indices);
      }

      case 1:
          // For metadata purposes, we consider a singleton tuple to be
          // isomorphic to its element type.
        return IGF.emitTypeMetadataRef(type.getElementType(0));

      case 2: {
        // Find the metadata pointer for this element.
        auto elt0Metadata = IGF.emitTypeMetadataRef(type.getElementType(0));
        auto elt1Metadata = IGF.emitTypeMetadataRef(type.getElementType(1));

        llvm::Value *args[] = {
          elt0Metadata, elt1Metadata,
          getTupleLabelsString(IGF.IGM, type),
          llvm::ConstantPointerNull::get(IGF.IGM.WitnessTablePtrTy) // proposed
        };

        auto call = IGF.Builder.CreateCall(IGF.IGM.getGetTupleMetadata2Fn(),
                                           args);
        call->setDoesNotThrow();
        return setLocal(CanType(type), call);
      }

      case 3: {
        // Find the metadata pointer for this element.
        auto elt0Metadata = IGF.emitTypeMetadataRef(type.getElementType(0));
        auto elt1Metadata = IGF.emitTypeMetadataRef(type.getElementType(1));
        auto elt2Metadata = IGF.emitTypeMetadataRef(type.getElementType(2));

        llvm::Value *args[] = {
          elt0Metadata, elt1Metadata, elt2Metadata,
          getTupleLabelsString(IGF.IGM, type),
          llvm::ConstantPointerNull::get(IGF.IGM.WitnessTablePtrTy) // proposed
        };

        auto call = IGF.Builder.CreateCall(IGF.IGM.getGetTupleMetadata3Fn(),
                                           args);
        call->setDoesNotThrow();
        return setLocal(CanType(type), call);
      }
      default:
        // TODO: use a caching entrypoint (with all information
        // out-of-line) for non-dependent tuples.

        llvm::Value *pointerToFirst = nullptr; // appease -Wuninitialized

        auto elements = type.getElementTypes();
        auto arrayTy = llvm::ArrayType::get(IGF.IGM.TypeMetadataPtrTy,
                                            elements.size());
        Address buffer = IGF.createAlloca(arrayTy,IGF.IGM.getPointerAlignment(),
                                          "tuple-elements");
        IGF.Builder.CreateLifetimeStart(buffer,
                                    IGF.IGM.getPointerSize() * elements.size());
        for (unsigned i = 0, e = elements.size(); i != e; ++i) {
          // Find the metadata pointer for this element.
          llvm::Value *eltMetadata = IGF.emitTypeMetadataRef(elements[i]);

          // GEP to the appropriate element and store.
          Address eltPtr = IGF.Builder.CreateStructGEP(buffer, i,
                                                     IGF.IGM.getPointerSize());
          IGF.Builder.CreateStore(eltMetadata, eltPtr);

          // Remember the GEP to the first element.
          if (i == 0) pointerToFirst = eltPtr.getAddress();
        }

        llvm::Value *args[] = {
          llvm::ConstantInt::get(IGF.IGM.SizeTy, elements.size()),
          pointerToFirst,
          getTupleLabelsString(IGF.IGM, type),
          llvm::ConstantPointerNull::get(IGF.IGM.WitnessTablePtrTy) // proposed
        };

        auto call = IGF.Builder.CreateCall(IGF.IGM.getGetTupleMetadataFn(),
                                           args);
        call->setDoesNotThrow();

        IGF.Builder.CreateLifetimeEnd(buffer,
                                    IGF.IGM.getPointerSize() * elements.size());

        return setLocal(type, call);
      }
    }

    llvm::Value *visitGenericFunctionType(CanGenericFunctionType type) {
      IGF.unimplemented(SourceLoc(),
                        "metadata ref for generic function type");
      return llvm::UndefValue::get(IGF.IGM.TypeMetadataPtrTy);
    }

    llvm::Value *getFunctionParameterRef(AnyFunctionType::CanParam param) {
      auto type = param.getType();
      if (param.getParameterFlags().isInOut())
        type = type->getInOutObjectType()->getCanonicalType();
      return IGF.emitTypeMetadataRef(type);
    }

    llvm::Value *visitFunctionType(CanFunctionType type) {
      if (auto metatype = tryGetLocal(type))
        return metatype;

      auto result =
          IGF.emitTypeMetadataRef(type->getResult()->getCanonicalType());

      auto params = type.getParams();
      auto numParams = params.size();

      bool hasFlags = false;
      for (auto param : params) {
        if (!param.getParameterFlags().isNone()) {
          hasFlags = true;
          break;
        }
      }

      // Map the convention to a runtime metadata value.
      FunctionMetadataConvention metadataConvention;
      switch (type->getRepresentation()) {
      case FunctionTypeRepresentation::Swift:
        metadataConvention = FunctionMetadataConvention::Swift;
        break;
      case FunctionTypeRepresentation::Thin:
        metadataConvention = FunctionMetadataConvention::Thin;
        break;
      case FunctionTypeRepresentation::Block:
        metadataConvention = FunctionMetadataConvention::Block;
        break;
      case FunctionTypeRepresentation::CFunctionPointer:
        metadataConvention = FunctionMetadataConvention::CFunctionPointer;
        break;
      }

      auto flagsVal = FunctionTypeFlags()
                          .withNumParameters(numParams)
                          .withConvention(metadataConvention)
                          .withThrows(type->throws())
                          .withParameterFlags(hasFlags);

      auto flags = llvm::ConstantInt::get(IGF.IGM.SizeTy,
                                          flagsVal.getIntValue());

      auto collectParameters =
          [&](llvm::function_ref<void(unsigned, llvm::Value *,
                                      ParameterFlags flags)>
                  processor) {
            for (auto index : indices(params)) {
              auto param = params[index];
              auto flags = param.getParameterFlags();

              auto parameterFlags = ParameterFlags()
                                        .withInOut(flags.isInOut())
                                        .withShared(flags.isShared())
                                        .withVariadic(flags.isVariadic());

              processor(index, getFunctionParameterRef(param), parameterFlags);
            }
          };

      auto constructSimpleCall =
          [&](llvm::SmallVectorImpl<llvm::Value *> &arguments)
          -> llvm::Constant * {
        arguments.push_back(flags);

        collectParameters([&](unsigned i, llvm::Value *typeRef,
                              ParameterFlags flags) {
          arguments.push_back(typeRef);
          if (hasFlags)
            arguments.push_back(
                llvm::ConstantInt::get(IGF.IGM.Int32Ty, flags.getIntValue()));
        });

        arguments.push_back(result);

        switch (params.size()) {
        case 1:
          return hasFlags ? IGF.IGM.getGetFunctionMetadata1WithFlagsFn()
                          : IGF.IGM.getGetFunctionMetadata1Fn();

        case 2:
          return hasFlags ? IGF.IGM.getGetFunctionMetadata2WithFlagsFn()
                          : IGF.IGM.getGetFunctionMetadata2Fn();

        case 3:
          return hasFlags ? IGF.IGM.getGetFunctionMetadata3WithFlagsFn()
                          : IGF.IGM.getGetFunctionMetadata3Fn();

        default:
          llvm_unreachable("supports only 1/2/3 parameter functions");
        }
      };

      auto getArrayFor = [&](llvm::Type *elementType, unsigned size,
                             const llvm::Twine &name) -> Address {
        auto arrayTy = llvm::ArrayType::get(elementType, size);
        return IGF.createAlloca(arrayTy, IGF.IGM.getPointerAlignment(), name);
      };

      switch (numParams) {
      case 1:
      case 2:
      case 3: {
        llvm::SmallVector<llvm::Value *, 8> arguments;
        auto *metadataFn = constructSimpleCall(arguments);
        auto *call = IGF.Builder.CreateCall(metadataFn, arguments);
        call->setDoesNotThrow();
        return setLocal(CanType(type), call);
      }

      default:
        auto *const Int32Ptr = IGF.IGM.Int32Ty->getPointerTo();

        llvm::SmallVector<llvm::Value *, 8> arguments;
        arguments.push_back(flags);

        Address parameters;
        if (!params.empty()) {
          parameters = getArrayFor(IGF.IGM.TypeMetadataPtrTy, numParams,
                                   "function-parameters");

          IGF.Builder.CreateLifetimeStart(parameters,
                                          IGF.IGM.getPointerSize() * numParams);

          ConstantInitBuilder paramFlags(IGF.IGM);
          auto flagsArr = paramFlags.beginArray();
          collectParameters([&](unsigned i, llvm::Value *typeRef,
                                ParameterFlags flags) {
            auto argPtr = IGF.Builder.CreateStructGEP(parameters, i,
                                                      IGF.IGM.getPointerSize());
            IGF.Builder.CreateStore(typeRef, argPtr);
            if (hasFlags)
              flagsArr.addInt32(flags.getIntValue());
          });

          auto parametersPtr =
              IGF.Builder.CreateStructGEP(parameters, 0, Size(0));
          arguments.push_back(parametersPtr.getAddress());

          if (hasFlags) {
            auto *flagsVar = flagsArr.finishAndCreateGlobal(
                "parameter-flags", IGF.IGM.getPointerAlignment(),
                /* constant */ true);
            arguments.push_back(IGF.Builder.CreateBitCast(flagsVar, Int32Ptr));
          } else {
            flagsArr.abandon();
            arguments.push_back(llvm::ConstantPointerNull::get(Int32Ptr));
          }
        } else {
          arguments.push_back(llvm::ConstantPointerNull::get(
              IGF.IGM.TypeMetadataPtrTy->getPointerTo()));
          arguments.push_back(llvm::ConstantPointerNull::get(Int32Ptr));
        }

        arguments.push_back(result);

        auto call = IGF.Builder.CreateCall(IGF.IGM.getGetFunctionMetadataFn(),
                                           arguments);
        call->setDoesNotThrow();

        if (parameters.isValid())
          IGF.Builder.CreateLifetimeEnd(parameters,
                                        IGF.IGM.getPointerSize() * numParams);

        return setLocal(type, call);
      }
    }

    llvm::Value *visitAnyMetatypeType(CanAnyMetatypeType type) {
      // FIXME: We shouldn't accept a lowered metatype here, but we need to
      // represent Optional<@objc_metatype T.Type> as an AST type for ABI
      // reasons.
      
      // assert(!type->hasRepresentation()
      //       && "should not be asking for a representation-specific metatype "
      //          "metadata");
      
      if (auto metatype = tryGetLocal(type))
        return metatype;

      auto instMetadata = IGF.emitTypeMetadataRef(type.getInstanceType());
      auto fn = isa<MetatypeType>(type)
                  ? IGF.IGM.getGetMetatypeMetadataFn()
                  : IGF.IGM.getGetExistentialMetatypeMetadataFn();
      auto call = IGF.Builder.CreateCall(fn, instMetadata);
      call->setDoesNotThrow();

      return setLocal(type, call);
    }

    llvm::Value *visitModuleType(CanModuleType type) {
      IGF.unimplemented(SourceLoc(), "metadata ref for module type");
      return llvm::UndefValue::get(IGF.IGM.TypeMetadataPtrTy);
    }

    llvm::Value *visitDynamicSelfType(CanDynamicSelfType type) {
      return IGF.getLocalSelfMetadata();
    }
      
    llvm::Value *emitExistentialTypeMetadata(CanType type) {
      if (auto metatype = tryGetLocal(type))
        return metatype;

      auto layout = type.getExistentialLayout();

      auto protocols = layout.getProtocols();

      // Collect references to the protocol descriptors.
      auto descriptorArrayTy
        = llvm::ArrayType::get(IGF.IGM.ProtocolDescriptorPtrTy,
                               protocols.size());
      Address descriptorArray = IGF.createAlloca(descriptorArrayTy,
                                                 IGF.IGM.getPointerAlignment(),
                                                 "protocols");
      IGF.Builder.CreateLifetimeStart(descriptorArray,
                                   IGF.IGM.getPointerSize() * protocols.size());
      descriptorArray = IGF.Builder.CreateBitCast(descriptorArray,
                               IGF.IGM.ProtocolDescriptorPtrTy->getPointerTo());
      
      unsigned index = 0;
      for (auto *protoTy : protocols) {
        auto *protoDecl = protoTy->getDecl();
        llvm::Value *ref = emitProtocolDescriptorRef(IGF, protoDecl);
        Address slot = IGF.Builder.CreateConstArrayGEP(descriptorArray,
                                               index, IGF.IGM.getPointerSize());
        IGF.Builder.CreateStore(ref, slot);
        ++index;
      }

      // Note: ProtocolClassConstraint::Class is 0, ::Any is 1.
      auto classConstraint =
        llvm::ConstantInt::get(IGF.IGM.Int1Ty,
                               !layout.requiresClass());
      llvm::Value *superclassConstraint =
        llvm::ConstantPointerNull::get(IGF.IGM.TypeMetadataPtrTy);
      if (layout.superclass) {
        superclassConstraint = IGF.emitTypeMetadataRef(
          CanType(layout.superclass));
      }

      auto call = IGF.Builder.CreateCall(IGF.IGM.getGetExistentialMetadataFn(),
                                         {classConstraint,
                                          superclassConstraint,
                                          IGF.IGM.getSize(Size(protocols.size())),
                                          descriptorArray.getAddress()});
      call->setDoesNotThrow();
      IGF.Builder.CreateLifetimeEnd(descriptorArray,
                                   IGF.IGM.getPointerSize() * protocols.size());
      return setLocal(type, call);
    }

    llvm::Value *visitProtocolType(CanProtocolType type) {
      return emitExistentialTypeMetadata(type);
    }
      
    llvm::Value *visitProtocolCompositionType(CanProtocolCompositionType type) {
      return emitExistentialTypeMetadata(type);
    }

    llvm::Value *visitReferenceStorageType(CanReferenceStorageType type) {
      llvm_unreachable("reference storage type should have been converted by "
                       "SILGen");
    }
    llvm::Value *visitSILFunctionType(CanSILFunctionType type) {
      llvm_unreachable("should not be asking for metadata of a lowered SIL "
                       "function type--SILGen should have used the AST type");
    }
    llvm::Value *visitSILTokenType(CanSILTokenType type) {
      llvm_unreachable("should not be asking for metadata of a SILToken type");
    }

    llvm::Value *visitArchetypeType(CanArchetypeType type) {
      return emitArchetypeTypeMetadataRef(IGF, type);
    }

    llvm::Value *visitGenericTypeParamType(CanGenericTypeParamType type) {
      llvm_unreachable("dependent type should have been substituted by Sema or SILGen");
    }

    llvm::Value *visitDependentMemberType(CanDependentMemberType type) {
      llvm_unreachable("dependent type should have been substituted by Sema or SILGen");
    }

    llvm::Value *visitLValueType(CanLValueType type) {
      llvm_unreachable("lvalue type should have been lowered by SILGen");
    }
    llvm::Value *visitInOutType(CanInOutType type) {
      llvm_unreachable("inout type should have been lowered by SILGen");
    }
    llvm::Value *visitErrorType(CanErrorType type) {
      llvm_unreachable("error type should not appear in IRGen");
    }

    llvm::Value *visitSILBlockStorageType(CanSILBlockStorageType type) {
      llvm_unreachable("cannot ask for metadata of block storage");
    }

    llvm::Value *visitSILBoxType(CanSILBoxType type) {
      // The Builtin.NativeObject metadata can stand in for boxes.
      return emitDirectMetadataRef(type->getASTContext().TheNativeObjectType);
    }

    /// Try to find the metatype in local data.
    llvm::Value *tryGetLocal(CanType type) {
      return IGF.tryGetLocalTypeData(type, LocalTypeDataKind::forTypeMetadata());
    }

    /// Set the metatype in local data.
    llvm::Value *setLocal(CanType type, llvm::Instruction *metatype) {
      IGF.setScopedLocalTypeData(type, LocalTypeDataKind::forTypeMetadata(),
                                 metatype);
      return metatype;
    }
  };
} // end anonymous namespace

/// Emit a type metadata reference without using an accessor function.
static llvm::Value *emitDirectTypeMetadataRef(IRGenFunction &IGF,
                                              CanType type) {
  return EmitTypeMetadataRef(IGF).visit(type);
}

static Address emitAddressOfSuperclassRefInClassMetadata(IRGenFunction &IGF,
                                                  llvm::Value *metadata) {
  // The superclass field in a class type is the first field past the isa.
  unsigned index = 1;

  Address addr(metadata, IGF.IGM.getPointerAlignment());
  addr = IGF.Builder.CreateBitCast(addr,
                                   IGF.IGM.TypeMetadataPtrTy->getPointerTo());
  return IGF.Builder.CreateConstArrayGEP(addr, index, IGF.IGM.getPointerSize());
}

static bool isLoadFrom(llvm::Value *value, Address address) {
  if (auto load = dyn_cast<llvm::LoadInst>(value)) {
    return load->getOperand(0) == address.getAddress();
  }
  return false;
}

/// Emit the body of a lazy cache accessor.
///
/// If cacheVariable is null, we perform the direct access every time.
/// This is used for metadata accessors that come about due to resilience,
/// where the direct access is completely trivial.
void irgen::emitLazyCacheAccessFunction(IRGenModule &IGM,
                                        llvm::Function *accessor,
                                        llvm::GlobalVariable *cacheVariable,
         const llvm::function_ref<llvm::Value*(IRGenFunction &IGF)> &getValue) {
  accessor->setDoesNotThrow();

  // This function is logically 'readnone': the caller does not need
  // to reason about any side effects or stores it might perform.
  accessor->setDoesNotAccessMemory();

  IRGenFunction IGF(IGM, accessor);
  if (IGM.DebugInfo)
    IGM.DebugInfo->emitArtificialFunction(IGF, accessor);

  // If there's no cache variable, just perform the direct access.
  if (cacheVariable == nullptr) {
    IGF.Builder.CreateRet(getValue(IGF));
    return;
  }

  // Set up the cache variable.
  llvm::Constant *null =
    llvm::ConstantPointerNull::get(
                        cast<llvm::PointerType>(cacheVariable->getValueType()));

  cacheVariable->setInitializer(null);
  cacheVariable->setAlignment(IGM.getPointerAlignment().getValue());
  Address cache(cacheVariable, IGM.getPointerAlignment());

  // Okay, first thing, check the cache variable.
  //
  // Conceptually, this needs to establish memory ordering with the
  // store we do later in the function: if the metadata value is
  // non-null, we must be able to see any stores performed by the
  // initialization of the metadata.  However, any attempt to read
  // from the metadata will be address-dependent on the loaded
  // metadata pointer, which is sufficient to provide adequate
  // memory ordering guarantees on all the platforms we care about:
  // ARM has special rules about address dependencies, and x86's
  // memory ordering is strong enough to guarantee the visibility
  // even without the address dependency.
  //
  // And we do not need to worry about the compiler because the
  // address dependency naturally forces an order to the memory
  // accesses.
  //
  // Therefore, we can perform a completely naked load here.
  // FIXME: Technically should be "consume", but that introduces barriers in the
  // current LLVM ARM backend.
  auto load = IGF.Builder.CreateLoad(cache);
  // Make this barrier explicit when building for TSan to avoid false positives.
  if (IGM.IRGen.Opts.Sanitizers & SanitizerKind::Thread)
    load->setOrdering(llvm::AtomicOrdering::Acquire);


  // Compare the load result against null.
  auto isNullBB = IGF.createBasicBlock("cacheIsNull");
  auto contBB = IGF.createBasicBlock("cont");
  llvm::Value *comparison = IGF.Builder.CreateICmpEQ(load, null);
  IGF.Builder.CreateCondBr(comparison, isNullBB, contBB);
  auto loadBB = IGF.Builder.GetInsertBlock();

  // If the load yielded null, emit the type metadata.
  IGF.Builder.emitBlock(isNullBB);
  llvm::Value *directResult = getValue(IGF);

  // Store it back to the cache variable.  This needs to be a store-release
  // because it needs to propagate memory visibility to the other threads
  // that can access the cache: the initializing stores might be visible
  // to this thread, but they aren't transitively guaranteed to be visible
  // to other threads unless this is a store-release.
  //
  // However, we can skip this if the value was actually loaded from the
  // cache.  This is a simple, if hacky, peephole that's useful for the
  // code in emitInPlaceTypeMetadataAccessFunctionBody.
  if (!isLoadFrom(directResult, cache)) {
    IGF.Builder.CreateStore(directResult, cache)
      ->setAtomic(llvm::AtomicOrdering::Release);
  }

  IGF.Builder.CreateBr(contBB);
  auto storeBB = IGF.Builder.GetInsertBlock();

  // Emit the continuation block.
  IGF.Builder.emitBlock(contBB);
  auto phi = IGF.Builder.CreatePHI(null->getType(), 2);
  phi->addIncoming(load, loadBB);
  phi->addIncoming(directResult, storeBB);

  IGF.Builder.CreateRet(phi);
}

static llvm::Value *emitGenericMetadataAccessFunction(IRGenFunction &IGF,
                                                      NominalTypeDecl *nominal,
                                                      GenericArguments &genericArgs) {
  CanType declaredType = nominal->getDeclaredType()->getCanonicalType();
  llvm::Value *metadata = IGF.IGM.getAddrOfTypeMetadata(declaredType, true);

  // Collect input arguments to the generic metadata accessor, as laid out
  // by the GenericArguments class.
  for (auto &arg : IGF.CurFn->args())
    genericArgs.Values.push_back(&arg);
  assert(genericArgs.Values.size() == genericArgs.Types.size());
  assert((genericArgs.Values.size() > 0 ||
          nominal->getGenericSignature()->areAllParamsConcrete())
         && "no generic args?!");

  // Slam that information directly into the generic arguments buffer.
  auto argsBufferTy =
    llvm::StructType::get(IGF.IGM.LLVMContext, genericArgs.Types);
  Address argsBuffer = IGF.createAlloca(argsBufferTy,
                                        IGF.IGM.getPointerAlignment(),
                                        "generic.arguments");
  IGF.Builder.CreateLifetimeStart(argsBuffer,
                          IGF.IGM.getPointerSize() * genericArgs.Values.size());
  for (unsigned i = 0, e = genericArgs.Values.size(); i != e; ++i) {
    Address elt = IGF.Builder.CreateStructGEP(argsBuffer, i,
                                              IGF.IGM.getPointerSize() * i);
    IGF.Builder.CreateStore(genericArgs.Values[i], elt);
  }

  // Cast to void*.
  llvm::Value *arguments =
    IGF.Builder.CreateBitCast(argsBuffer.getAddress(), IGF.IGM.Int8PtrTy);

  // Make the call.
  auto result = IGF.Builder.CreateCall(IGF.IGM.getGetGenericMetadataFn(),
                                       {metadata, arguments});
  result->setDoesNotThrow();
  result->addAttribute(llvm::AttributeList::FunctionIndex,
                       llvm::Attribute::ReadOnly);

  IGF.Builder.CreateLifetimeEnd(argsBuffer,
                          IGF.IGM.getPointerSize() * genericArgs.Values.size());

  return result;

}

using InPlaceMetadataInitializer =
  llvm::function_ref<llvm::Value*(IRGenFunction &IGF, llvm::Value *metadata)>;

/// Emit a helper function for swift_once that performs in-place
/// initialization of the given nominal type.
static llvm::Constant *
createInPlaceMetadataInitializationFunction(IRGenModule &IGM, 
                                            CanNominalType type,
                                            llvm::Constant *metadata,
                                            llvm::Constant *cacheVariable,
                                     InPlaceMetadataInitializer &&initialize) {
  // There's an ignored i8* parameter.
  auto fnTy = llvm::FunctionType::get(IGM.VoidTy, {IGM.Int8PtrTy},
                                      /*variadic*/ false);
  llvm::Function *fn = llvm::Function::Create(fnTy,
                                       llvm::GlobalValue::PrivateLinkage,
                                       Twine("initialize_metadata_")
                                           + type->getDecl()->getName().str(),
                                       &IGM.Module);
  fn->setAttributes(IGM.constructInitialAttributes());
  
  // Set up the function.
  IRGenFunction IGF(IGM, fn);
  if (IGM.DebugInfo)
    IGM.DebugInfo->emitArtificialFunction(IGF, fn);

  // Skip instrumentation when building for TSan to avoid false positives.
  // The synchronization for this happens in the Runtime and we do not see it.
  if (IGM.IRGen.Opts.Sanitizers & SanitizerKind::Thread)
    fn->removeFnAttr(llvm::Attribute::SanitizeThread);

  // Emit the initialization.
  llvm::Value *relocatedMetadata = initialize(IGF, metadata);

  // Store back to the cache variable.
  IGF.Builder.CreateStore(relocatedMetadata,
                          Address(cacheVariable, IGM.getPointerAlignment()))
    ->setAtomic(llvm::AtomicOrdering::Release);

  IGF.Builder.CreateRetVoid();
  return fn;
}

/// Emit the function body for the type metadata accessor of a nominal type
/// that might require in-place initialization.
static llvm::Value *
emitInPlaceTypeMetadataAccessFunctionBody(IRGenFunction &IGF,
                                          CanNominalType type,
                                          llvm::Constant *cacheVariable,
                                    InPlaceMetadataInitializer &&initializer) {
  llvm::Constant *metadata = IGF.IGM.getAddrOfTypeMetadata(type, false);

  // We might not have interesting initialization to do.
  assert((cacheVariable == nullptr) ==
            isTypeMetadataAccessTrivial(IGF.IGM, type));
  if (!cacheVariable)
    return metadata;

  // Okay, we have non-trivial initialization to do.
  // Ensure that we don't have multiple threads racing to do this.
  llvm::GlobalVariable *onceGuard =
    new llvm::GlobalVariable(IGF.IGM.Module, IGF.IGM.OnceTy, /*constant*/ false,
                             llvm::GlobalValue::PrivateLinkage,
                             llvm::Constant::getNullValue(IGF.IGM.OnceTy),
                             Twine(IGF.CurFn->getName()) + ".once_token");

  // There's no point in performing the fast-path token check here
  // because we've already checked the cache variable.  We're just using
  // swift_once to guarantee thread safety.
  assert(cacheVariable && "lazy initialization but no cache variable");

  // Create the protected function.  swift_once wants this as an i8*.
  llvm::Value *onceFn =
    createInPlaceMetadataInitializationFunction(IGF.IGM, type, metadata,
                                                cacheVariable,
                                                std::move(initializer));
  onceFn = IGF.Builder.CreateBitCast(onceFn, IGF.IGM.Int8PtrTy);
  auto context = llvm::UndefValue::get(IGF.IGM.Int8PtrTy);

  auto onceCall = IGF.Builder.CreateCall(IGF.IGM.getOnceFn(),
                                         {onceGuard, onceFn, context});
  onceCall->setCallingConv(IGF.IGM.DefaultCC);

  // We can just load the cache now.
  // TODO: this should be consume-ordered when LLVM supports it.
  Address cacheAddr = Address(cacheVariable, IGF.IGM.getPointerAlignment());
  llvm::LoadInst *relocatedMetadata = IGF.Builder.CreateLoad(cacheAddr);
  // Make this barrier explicit when building for TSan to avoid false positives.
  if (IGF.IGM.IRGen.Opts.Sanitizers & SanitizerKind::Thread)
    relocatedMetadata->setOrdering(llvm::AtomicOrdering::Acquire);

  // emitLazyCacheAccessFunction will see that the value was loaded from
  // the guard variable and skip the redundant store back.
  return relocatedMetadata;
}

/// Emit the body of a metadata accessor function for the given type.
///
/// This function is appropriate for ordinary situations where the
/// construction of the metadata value just involves calling idempotent
/// metadata-construction functions.  It is not used for the in-place
/// initialization of non-generic nominal type metadata.
static llvm::Value *emitTypeMetadataAccessFunctionBody(IRGenFunction &IGF,
                                                       CanType type) {
  assert(!type->hasArchetype() &&
         "cannot emit metadata accessor for context-dependent type");

  // We only take this path for non-generic nominal types.
  auto typeDecl = type->getAnyNominal();
  if (!typeDecl)
    return emitDirectTypeMetadataRef(IGF, type);

  if (typeDecl->isGenericContext() &&
      !(isa<ClassDecl>(typeDecl) &&
        isa<ClangModuleUnit>(typeDecl->getModuleScopeContext()))) {
    // This is a metadata accessor for a fully substituted generic type.
    return emitDirectTypeMetadataRef(IGF, type);
  }

  // We should never be emitting a metadata accessor for resilient nominal
  // types outside of their defining module.  We'd only do that anyway for
  // types that don't guarantee the existence of a non-unique access
  // function, and that should never be true of a resilient type with
  // external availability.
  //
  // (The type might still not have a statically-known layout.  It just
  // can't be resilient at the top level: we have to know its immediate
  // members, or we can't even begin to approach the problem of emitting
  // metadata for it.)
  assert(!IGF.IGM.isResilient(typeDecl, ResilienceExpansion::Maximal));

  // Non-native types are just wrapped in various ways.
  if (auto classDecl = dyn_cast<ClassDecl>(typeDecl)) {
    // We emit a completely different pattern for foreign classes.
    if (classDecl->getForeignClassKind() == ClassDecl::ForeignKind::CFType) {
      return emitForeignTypeMetadataRef(IGF, type);
    }

    // Classes that might not have Swift metadata use a different
    // symbol name.
    if (!hasKnownSwiftMetadata(IGF.IGM, classDecl)) {
      return emitObjCMetadataRef(IGF, classDecl);
    }

  // Imported value types require foreign metadata uniquing.
  } else if (isa<ClangModuleUnit>(typeDecl->getModuleScopeContext())) {
    return emitForeignTypeMetadataRef(IGF, type);
  }

  // Okay, everything else is built from a Swift metadata object.
  llvm::Constant *metadata = IGF.IGM.getAddrOfTypeMetadata(type, false);

  // We should not be doing more serious work along this path.
  assert(isTypeMetadataAccessTrivial(IGF.IGM, type));

  return metadata;
}

using MetadataAccessGenerator =
  llvm::function_ref<llvm::Value*(IRGenFunction &IGF, llvm::Constant *cache)>;

/// Get or create an accessor function to the given non-dependent type.
static llvm::Function *getTypeMetadataAccessFunction(IRGenModule &IGM,
                                        CanType type,
                                        ForDefinition_t shouldDefine,
                                        MetadataAccessGenerator &&generator) {
  assert(!type->hasArchetype());
  // Type should be bound unless it's type erased.
  assert(isTypeErasedGenericClassType(type)
           ? !isa<BoundGenericType>(type)
           : !isa<UnboundGenericType>(type));

  llvm::Function *accessor =
    IGM.getAddrOfTypeMetadataAccessFunction(type, shouldDefine);

  // If we're not supposed to define the accessor, or if we already
  // have defined it, just return the pointer.
  if (!shouldDefine || !accessor->empty())
    return accessor;

  // Okay, define the accessor.
  llvm::GlobalVariable *cacheVariable = nullptr;

  // If our preferred access method is to go via an accessor, it means
  // there is some non-trivial computation that needs to be cached.
  if (!isTypeMetadataAccessTrivial(IGM, type)) {
    cacheVariable = cast<llvm::GlobalVariable>(
        IGM.getAddrOfTypeMetadataLazyCacheVariable(type, ForDefinition));

    if (IGM.getOptions().optimizeForSize())
      accessor->addFnAttr(llvm::Attribute::NoInline);
  }

  emitLazyCacheAccessFunction(IGM, accessor, cacheVariable,
                              [&](IRGenFunction &IGF) -> llvm::Value* {
    return generator(IGF, cacheVariable);
  });

  return accessor;
}

/// Get or create an accessor function to the given non-dependent type.
static llvm::Function *getTypeMetadataAccessFunction(IRGenModule &IGM,
                                                 CanType type,
                                                 ForDefinition_t shouldDefine) {
  return getTypeMetadataAccessFunction(IGM, type, shouldDefine,
                                       [&](IRGenFunction &IGF,
                                           llvm::Constant *cacheVariable) {
    // We should not be called with ForDefinition for nominal types
    // that require in-place initialization.
    return emitTypeMetadataAccessFunctionBody(IGF, type);
  });
}

/// Get or create an accessor function to the given generic type.
static llvm::Function *getGenericTypeMetadataAccessFunction(IRGenModule &IGM,
                                                 NominalTypeDecl *nominal,
                                                 ForDefinition_t shouldDefine) {
  assert(nominal->isGenericContext());
  assert(!isTypeErasedGenericClass(nominal));

  GenericArguments genericArgs;
  genericArgs.collectTypes(IGM, nominal);

  llvm::Function *accessor =
    IGM.getAddrOfGenericTypeMetadataAccessFunction(
        nominal, genericArgs.Types, shouldDefine);

  // If we're not supposed to define the accessor, or if we already
  // have defined it, just return the pointer.
  if (!shouldDefine || !accessor->empty())
    return accessor;

  if (IGM.getOptions().optimizeForSize())
    accessor->addFnAttr(llvm::Attribute::NoInline);

  emitLazyCacheAccessFunction(IGM, accessor, /*cacheVariable=*/nullptr,
                              [&](IRGenFunction &IGF) -> llvm::Value* {
    return emitGenericMetadataAccessFunction(IGF, nominal, genericArgs);
  });

  return accessor;
}

/// Return the type metadata access function for the given type, if it
/// is guaranteed to exist.
static llvm::Constant *
getRequiredTypeMetadataAccessFunction(IRGenModule &IGM,
                                      NominalTypeDecl *theDecl,
                                      ForDefinition_t shouldDefine) {
  if (!hasRequiredTypeMetadataAccessFunction(theDecl))
    return nullptr;

  if (theDecl->isGenericContext()) {
    return getGenericTypeMetadataAccessFunction(IGM, theDecl, shouldDefine);
  }

  CanType declaredType = theDecl->getDeclaredType()->getCanonicalType();
  return getTypeMetadataAccessFunction(IGM, declaredType, shouldDefine);
}

/// Force a public metadata access function into existence if necessary
/// for the given type.
template <class BuilderTy>
static void maybeEmitNominalTypeMetadataAccessFunction(NominalTypeDecl *theDecl,
                                                       BuilderTy &builder) {
  if (!hasRequiredTypeMetadataAccessFunction(theDecl))
    return;

  builder.createMetadataAccessFunction();
}

/// Emit a call to the type metadata accessor for the given function.
static llvm::Value *emitCallToTypeMetadataAccessFunction(IRGenFunction &IGF,
                                                         CanType type,
                                                 ForDefinition_t shouldDefine) {
  // If we already cached the metadata, use it.
  if (auto local =
        IGF.tryGetLocalTypeData(type, LocalTypeDataKind::forTypeMetadata()))
    return local;
  
  llvm::Constant *accessor =
    getTypeMetadataAccessFunction(IGF.IGM, type, shouldDefine);
  llvm::CallInst *call = IGF.Builder.CreateCall(accessor, {});
  call->setCallingConv(IGF.IGM.DefaultCC);
  call->setDoesNotAccessMemory();
  call->setDoesNotThrow();
  
  // Save the metadata for future lookups.
  IGF.setScopedLocalTypeData(type, LocalTypeDataKind::forTypeMetadata(), call);
  
  return call;
}

/// Produce the type metadata pointer for the given type.
llvm::Value *IRGenFunction::emitTypeMetadataRef(CanType type) {
  type = getRuntimeReifiedType(IGM, type);

  if (type->hasArchetype() ||
      isTypeMetadataAccessTrivial(IGM, type)) {
    return emitDirectTypeMetadataRef(*this, type);
  }

  switch (getTypeMetadataAccessStrategy(type)) {
  case MetadataAccessStrategy::PublicUniqueAccessor:
  case MetadataAccessStrategy::HiddenUniqueAccessor:
  case MetadataAccessStrategy::PrivateAccessor:
    return emitCallToTypeMetadataAccessFunction(*this, type, NotForDefinition);
  case MetadataAccessStrategy::NonUniqueAccessor:
    return emitCallToTypeMetadataAccessFunction(*this, type, ForDefinition);
  }
  llvm_unreachable("bad type metadata access strategy");
}

/// Return the address of a function that will return type metadata 
/// for the given non-dependent type.
llvm::Function *irgen::getOrCreateTypeMetadataAccessFunction(IRGenModule &IGM,
                                                             CanType type) {
  type = getRuntimeReifiedType(IGM, type);

  assert(!type->hasArchetype() &&
         "cannot create global function to return dependent type metadata");

  switch (getTypeMetadataAccessStrategy(type)) {
  case MetadataAccessStrategy::PublicUniqueAccessor:
  case MetadataAccessStrategy::HiddenUniqueAccessor:
  case MetadataAccessStrategy::PrivateAccessor:
    return getTypeMetadataAccessFunction(IGM, type, NotForDefinition);
  case MetadataAccessStrategy::NonUniqueAccessor:
    return getTypeMetadataAccessFunction(IGM, type, ForDefinition);
  }
  llvm_unreachable("bad type metadata access strategy");
}

namespace {
  /// A visitor class for emitting a reference to a metatype object.
  /// This implements a "raw" access, useful for implementing cache
  /// functions or for implementing dependent accesses.
  class EmitTypeMetadataRefForLayout
    : public CanTypeVisitor<EmitTypeMetadataRefForLayout, llvm::Value *> {
  private:
    IRGenFunction &IGF;
  public:
    EmitTypeMetadataRefForLayout(IRGenFunction &IGF) : IGF(IGF) {}

    llvm::Value *emitDirectMetadataRef(CanType type) {
      return IGF.IGM.getAddrOfTypeMetadata(type, /*pattern*/ false);
    }

    /// For most types, we can just emit the usual metadata.
    llvm::Value *visitType(CanType t) {
      return IGF.emitTypeMetadataRef(t);
    }

    llvm::Value *visitBoundGenericEnumType(CanBoundGenericEnumType type) {
      // Optionals have a lowered payload type, so we recurse here.
      if (auto objectTy = CanType(type).getAnyOptionalObjectType()) {
        auto payloadMetadata = visit(objectTy);
        llvm::Value *args[] = { payloadMetadata };
        llvm::Type *types[] = { IGF.IGM.TypeMetadataPtrTy };

        // Call the generic metadata accessor function.
        llvm::Function *accessor =
            IGF.IGM.getAddrOfGenericTypeMetadataAccessFunction(
                type->getDecl(), types, NotForDefinition);

        auto result = IGF.Builder.CreateCall(accessor, args);
        result->setDoesNotThrow();
        result->addAttribute(llvm::AttributeList::FunctionIndex,
                             llvm::Attribute::ReadNone);

        return result;
      }

      // Otherwise, generic arguments are not lowered.
      return visitType(type);
    }

    llvm::Value *visitTupleType(CanTupleType type) {
      if (auto cached = tryGetLocal(type))
        return cached;

      switch (type->getNumElements()) {
      case 0: {// Special case the empty tuple, just use the global descriptor.
        llvm::Constant *fullMetadata = IGF.IGM.getEmptyTupleMetadata();
        llvm::Constant *indices[] = {
          llvm::ConstantInt::get(IGF.IGM.Int32Ty, 0),
          llvm::ConstantInt::get(IGF.IGM.Int32Ty, 1)
        };
        return llvm::ConstantExpr::getInBoundsGetElementPtr(
            /*Ty=*/nullptr, fullMetadata, indices);
      }

      case 1:
          // For layout purposes, we consider a singleton tuple to be
          // isomorphic to its element type.
        return visit(type.getElementType(0));

      case 2: {
        // Find the layout metadata pointers for these elements.
        auto elt0Metadata = visit(type.getElementType(0));
        auto elt1Metadata = visit(type.getElementType(1));

        llvm::Value *args[] = {
          elt0Metadata, elt1Metadata,
          // labels don't matter for layout
          llvm::ConstantPointerNull::get(IGF.IGM.Int8PtrTy),
          llvm::ConstantPointerNull::get(IGF.IGM.WitnessTablePtrTy) // proposed
        };

        auto call = IGF.Builder.CreateCall(IGF.IGM.getGetTupleMetadata2Fn(),
                                           args);
        call->setDoesNotThrow();
        return setLocal(CanType(type), call);
      }

      case 3: {
        // Find the layout metadata pointers for these elements.
        auto elt0Metadata = visit(type.getElementType(0));
        auto elt1Metadata = visit(type.getElementType(1));
        auto elt2Metadata = visit(type.getElementType(2));

        llvm::Value *args[] = {
          elt0Metadata, elt1Metadata, elt2Metadata,
          // labels don't matter for layout
          llvm::ConstantPointerNull::get(IGF.IGM.Int8PtrTy),
          llvm::ConstantPointerNull::get(IGF.IGM.WitnessTablePtrTy) // proposed
        };

        auto call = IGF.Builder.CreateCall(IGF.IGM.getGetTupleMetadata3Fn(),
                                           args);
        call->setDoesNotThrow();
        return setLocal(CanType(type), call);
      }
      default:
        // TODO: use a caching entrypoint (with all information
        // out-of-line) for non-dependent tuples.

        llvm::Value *pointerToFirst = nullptr; // appease -Wuninitialized

        auto elements = type.getElementTypes();
        auto arrayTy = llvm::ArrayType::get(IGF.IGM.TypeMetadataPtrTy,
                                            elements.size());
        Address buffer = IGF.createAlloca(arrayTy,IGF.IGM.getPointerAlignment(),
                                          "tuple-elements");
        IGF.Builder.CreateLifetimeStart(buffer,
                                    IGF.IGM.getPointerSize() * elements.size());
        for (unsigned i = 0, e = elements.size(); i != e; ++i) {
          // Find the metadata pointer for this element.
          llvm::Value *eltMetadata = visit(elements[i]);

          // GEP to the appropriate element and store.
          Address eltPtr = IGF.Builder.CreateStructGEP(buffer, i,
                                                     IGF.IGM.getPointerSize());
          IGF.Builder.CreateStore(eltMetadata, eltPtr);

          // Remember the GEP to the first element.
          if (i == 0) pointerToFirst = eltPtr.getAddress();
        }

        llvm::Value *args[] = {
          llvm::ConstantInt::get(IGF.IGM.SizeTy, elements.size()),
          pointerToFirst,
          // labels don't matter for layout
          llvm::ConstantPointerNull::get(IGF.IGM.Int8PtrTy),
          llvm::ConstantPointerNull::get(IGF.IGM.WitnessTablePtrTy) // proposed
        };

        auto call = IGF.Builder.CreateCall(IGF.IGM.getGetTupleMetadataFn(),
                                           args);
        call->setDoesNotThrow();

        IGF.Builder.CreateLifetimeEnd(buffer,
                                    IGF.IGM.getPointerSize() * elements.size());

        return setLocal(type, call);
      }
    }

    llvm::Value *visitAnyFunctionType(CanAnyFunctionType type) {
      llvm_unreachable("not a SIL type");
    }
      
    llvm::Value *visitSILFunctionType(CanSILFunctionType type) {
      // All function types have the same layout regardless of arguments or
      // abstraction level. Use the metadata for () -> () for thick functions,
      // or Builtin.UnknownObject for block functions.
      auto &C = type->getASTContext();
      switch (type->getRepresentation()) {
      case SILFunctionType::Representation::Thin:
      case SILFunctionType::Representation::Method:
      case SILFunctionType::Representation::WitnessMethod:
      case SILFunctionType::Representation::ObjCMethod:
      case SILFunctionType::Representation::CFunctionPointer:
      case SILFunctionType::Representation::Closure:
        // A thin function looks like a plain pointer.
        // FIXME: Except for extra inhabitants?
        return emitDirectMetadataRef(C.TheRawPointerType);
      case SILFunctionType::Representation::Thick:
        // All function types look like () -> ().
        // FIXME: It'd be nice not to have to call through the runtime here.
        return IGF.emitTypeMetadataRef(
                 CanFunctionType::get(AnyFunctionType::CanParamArrayRef(),
                                      C.TheEmptyTupleType,
                                      AnyFunctionType::ExtInfo()));
      case SILFunctionType::Representation::Block:
        // All block types look like Builtin.UnknownObject.
        return emitDirectMetadataRef(C.TheUnknownObjectType);
      }

      llvm_unreachable("Not a valid SILFunctionType.");
    }

    llvm::Value *visitAnyMetatypeType(CanAnyMetatypeType type) {
      
      assert(type->hasRepresentation()
             && "not a lowered metatype");

      switch (type->getRepresentation()) {
      case MetatypeRepresentation::Thin: {
        // Thin metatypes are empty, so they look like the empty tuple type.
        llvm::Constant *fullMetadata = IGF.IGM.getEmptyTupleMetadata();
        llvm::Constant *indices[] = {
          llvm::ConstantInt::get(IGF.IGM.Int32Ty, 0),
          llvm::ConstantInt::get(IGF.IGM.Int32Ty, 1)
        };
        return llvm::ConstantExpr::getInBoundsGetElementPtr(
            /*Ty=*/nullptr, fullMetadata, indices);
      }
      case MetatypeRepresentation::Thick:
      case MetatypeRepresentation::ObjC:
        // Thick and ObjC metatypes look like pointers with extra inhabitants.
        // Get the metatype metadata from the runtime.
        // FIXME: It'd be nice not to need a runtime call here.
        return IGF.emitTypeMetadataRef(type);
      }

      llvm_unreachable("Not a valid MetatypeRepresentation.");
    }

    /// Try to find the metatype in local data.
    llvm::Value *tryGetLocal(CanType type) {
      return IGF.tryGetLocalTypeDataForLayout(
                                          IGF.IGM.getLoweredType(type),
                                          LocalTypeDataKind::forTypeMetadata());
    }

    /// Set the metatype in local data.
    llvm::Value *setLocal(CanType type, llvm::Instruction *metatype) {
      IGF.setScopedLocalTypeDataForLayout(IGF.IGM.getLoweredType(type),
                                          LocalTypeDataKind::forTypeMetadata(),
                                          metatype);
      return metatype;
    }
  };
} // end anonymous namespace

llvm::Value *IRGenFunction::emitTypeMetadataRefForLayout(SILType type) {
  return EmitTypeMetadataRefForLayout(*this).visit(type.getSwiftRValueType());
}

namespace {

  /// A visitor class for emitting a reference to a type layout struct.
  /// There are a few ways we can emit it:
  ///
  /// - If the type is fixed-layout and we have visibility of its value
  ///   witness table (or one close enough), we can project the layout struct
  ///   from it.
  /// - If the type is fixed layout, we can emit our own copy of the layout
  ///   struct.
  /// - If the type is dynamic-layout, we have to instantiate its metadata
  ///   and project out its metadata. (FIXME: This leads to deadlocks in
  ///   recursive cases, though we can avoid many deadlocks because most
  ///   valid recursive types bottom out in fixed-sized types like classes
  ///   or pointers.)
  class EmitTypeLayoutRef
    : public CanTypeVisitor<EmitTypeLayoutRef, llvm::Value *> {
  private:
    IRGenFunction &IGF;
  public:
    EmitTypeLayoutRef(IRGenFunction &IGF) : IGF(IGF) {}

    llvm::Value *emitFromValueWitnessTablePointer(llvm::Value *vwtable) {
      llvm::Value *indexConstant = llvm::ConstantInt::get(IGF.IGM.Int32Ty,
                               (unsigned)ValueWitness::First_TypeLayoutWitness);
      return IGF.Builder.CreateInBoundsGEP(IGF.IGM.Int8PtrTy, vwtable,
                                           indexConstant);
    }

    /// Emit the type layout by projecting it from a value witness table to
    /// which we have linkage.
    llvm::Value *emitFromValueWitnessTable(CanType t) {
      auto *vwtable = IGF.IGM.getAddrOfValueWitnessTable(t);
      return emitFromValueWitnessTablePointer(vwtable);
    }

    /// Emit the type layout by projecting it from dynamic type metadata.
    llvm::Value *emitFromTypeMetadata(CanType t) {
      auto *vwtable = IGF.emitValueWitnessTableRef(IGF.IGM.getLoweredType(t));
      return emitFromValueWitnessTablePointer(vwtable);
    }

    bool hasVisibleValueWitnessTable(CanType t) const {
      // Some builtin and structural types have value witnesses exported from
      // the runtime.
      auto &C = IGF.IGM.Context;
      if (t == C.TheEmptyTupleType
          || t == C.TheNativeObjectType
          || t == C.TheUnknownObjectType
          || t == C.TheBridgeObjectType
          || t == C.TheRawPointerType)
        return true;
      if (auto intTy = dyn_cast<BuiltinIntegerType>(t)) {
        auto width = intTy->getWidth();
        if (width.isPointerWidth())
          return true;
        if (width.isFixedWidth()) {
          switch (width.getFixedWidth()) {
          case 8:
          case 16:
          case 32:
          case 64:
          case 128:
          case 256:
            return true;
          default:
            return false;
          }
        }
        return false;
      }

      // TODO: If a nominal type is in the same source file as we're currently
      // emitting, we would be able to see its value witness table.
      return false;
    }

    /// Fallback default implementation.
    llvm::Value *visitType(CanType t) {
      auto silTy = IGF.IGM.getLoweredType(t);
      auto &ti = IGF.getTypeInfo(silTy);

      // If the type is in the same source file, or has a common value
      // witness table exported from the runtime, we can project from the
      // value witness table instead of emitting a new record.
      if (hasVisibleValueWitnessTable(t))
        return emitFromValueWitnessTable(t);

      // If the type is a singleton aggregate, the field's layout is equivalent
      // to the aggregate's.
      if (SILType singletonFieldTy = getSingletonAggregateFieldType(IGF.IGM,
                                             silTy, ResilienceExpansion::Maximal))
        return visit(singletonFieldTy.getSwiftRValueType());

      // If the type is fixed-layout, emit a copy of its layout.
      if (auto fixed = dyn_cast<FixedTypeInfo>(&ti))
        return IGF.IGM.emitFixedTypeLayout(t, *fixed);

      return emitFromTypeMetadata(t);
    }
      
    llvm::Value *visitAnyFunctionType(CanAnyFunctionType type) {
      llvm_unreachable("not a SIL type");
    }
      
    llvm::Value *visitSILFunctionType(CanSILFunctionType type) {
      // All function types have the same layout regardless of arguments or
      // abstraction level. Use the value witness table for
      // @convention(blah) () -> () from the runtime.
      auto &C = type->getASTContext();
      switch (type->getRepresentation()) {
      case SILFunctionType::Representation::Thin:
      case SILFunctionType::Representation::Method:
      case SILFunctionType::Representation::WitnessMethod:
      case SILFunctionType::Representation::ObjCMethod:
      case SILFunctionType::Representation::CFunctionPointer:
      case SILFunctionType::Representation::Closure:
        // A thin function looks like a plain pointer.
        // FIXME: Except for extra inhabitants?
        return emitFromValueWitnessTable(C.TheRawPointerType);
      case SILFunctionType::Representation::Thick:
        // All function types look like () -> ().
        return emitFromValueWitnessTable(
                 CanFunctionType::get(AnyFunctionType::CanParamArrayRef(),
                                      C.TheEmptyTupleType,
                                      AnyFunctionType::ExtInfo()));
      case SILFunctionType::Representation::Block:
        // All block types look like Builtin.UnknownObject.
        return emitFromValueWitnessTable(C.TheUnknownObjectType);
      }

      llvm_unreachable("Not a valid SILFunctionType.");
    }

    llvm::Value *visitAnyMetatypeType(CanAnyMetatypeType type) {
      
      assert(type->hasRepresentation()
             && "not a lowered metatype");

      switch (type->getRepresentation()) {
      case MetatypeRepresentation::Thin: {
        // Thin metatypes are empty, so they look like the empty tuple type.
        return emitFromValueWitnessTable(IGF.IGM.Context.TheEmptyTupleType);
      }
      case MetatypeRepresentation::Thick:
        if (isa<ExistentialMetatypeType>(type)) {
          return emitFromTypeMetadata(type);
        }
        // Otherwise, this is a metatype that looks like a pointer.
      case MetatypeRepresentation::ObjC:
        // Thick metatypes look like pointers with spare bits.
        return emitFromValueWitnessTable(
                     CanMetatypeType::get(IGF.IGM.Context.TheNativeObjectType));
      }

      llvm_unreachable("Not a valid MetatypeRepresentation.");
    }

    llvm::Value *visitAnyClassType(ClassDecl *classDecl) {
      // All class types have the same layout.
      auto type = classDecl->getDeclaredType()->getCanonicalType();
      switch (getReferenceCountingForType(IGF.IGM, type)) {
      case ReferenceCounting::Native:
        return emitFromValueWitnessTable(IGF.IGM.Context.TheNativeObjectType);

      case ReferenceCounting::ObjC:
      case ReferenceCounting::Block:
      case ReferenceCounting::Unknown:
        return emitFromValueWitnessTable(IGF.IGM.Context.TheUnknownObjectType);

      case ReferenceCounting::Bridge:
      case ReferenceCounting::Error:
        llvm_unreachable("classes shouldn't have this kind of refcounting");
      }

      llvm_unreachable("Not a valid ReferenceCounting.");
    }

    llvm::Value *visitClassType(CanClassType type) {
      return visitAnyClassType(type->getClassOrBoundGenericClass());
    }

    llvm::Value *visitBoundGenericClassType(CanBoundGenericClassType type) {
      return visitAnyClassType(type->getClassOrBoundGenericClass());
    }

    llvm::Value *visitReferenceStorageType(CanReferenceStorageType type) {
      // Other reference storage types all have the same layout for their
      // storage qualification and the reference counting of their underlying
      // object.

      auto &C = IGF.IGM.Context;
      CanType referent;
      switch (type->getOwnership()) {
      case Ownership::Strong:
        llvm_unreachable("shouldn't be a ReferenceStorageType");
      case Ownership::Weak:
        referent = type.getReferentType().getAnyOptionalObjectType();
        break;
      case Ownership::Unmanaged:
      case Ownership::Unowned:
        referent = type.getReferentType();
        break;
      }

      // Reference storage types with witness tables need open-coded layouts.
      // TODO: Maybe we could provide prefabs for 1 witness table.
      if (referent.isExistentialType()) {
        auto layout = referent.getExistentialLayout();
        for (auto *protoTy : layout.getProtocols()) {
          auto *protoDecl = protoTy->getDecl();
          if (IGF.getSILTypes().protocolRequiresWitnessTable(protoDecl))
            return visitType(type);
        }
      }

      // Unmanaged references are plain pointers with extra inhabitants,
      // which look like thick metatypes.
      //
      // FIXME: This sounds wrong, an Objective-C tagged pointer could be
      // stored in an unmanaged reference for instance.
      if (type->getOwnership() == Ownership::Unmanaged) {
        auto metatype = CanMetatypeType::get(C.TheNativeObjectType);
        return emitFromValueWitnessTable(metatype);
      }

      CanType valueWitnessReferent;
      switch (getReferenceCountingForType(IGF.IGM, referent)) {
      case ReferenceCounting::Unknown:
      case ReferenceCounting::Block:
      case ReferenceCounting::ObjC:
        valueWitnessReferent = C.TheUnknownObjectType;
        break;

      case ReferenceCounting::Native:
        valueWitnessReferent = C.TheNativeObjectType;
        break;

      case ReferenceCounting::Bridge:
        valueWitnessReferent = C.TheBridgeObjectType;
        break;

      case ReferenceCounting::Error:
        llvm_unreachable("shouldn't be possible");
      }

      // Get the reference storage type of the builtin object whose value
      // witness we can borrow.
      if (type->getOwnership() == Ownership::Weak)
        valueWitnessReferent = OptionalType::get(valueWitnessReferent)
          ->getCanonicalType();

      auto valueWitnessType = CanReferenceStorageType::get(valueWitnessReferent,
                                                       type->getOwnership());
      return emitFromValueWitnessTable(valueWitnessType);
    }
  };

} // end anonymous namespace

llvm::Value *IRGenFunction::emitTypeLayoutRef(SILType type) {
  return EmitTypeLayoutRef(*this).visit(type.getSwiftRValueType());
}

void IRGenModule::setTrueConstGlobal(llvm::GlobalVariable *var) {
  switch (TargetInfo.OutputObjectFormat) {
  case llvm::Triple::UnknownObjectFormat:
    llvm_unreachable("unknown object format");
  case llvm::Triple::MachO:
    var->setSection("__TEXT,__const");
    break;
  case llvm::Triple::ELF:
    var->setSection(".rodata");
    break;
  case llvm::Triple::COFF:
    var->setSection(".rdata");
    break;
  case llvm::Triple::Wasm:
    llvm_unreachable("web assembly object format is not supported.");
    break;
  }
}

/// Produce the heap metadata pointer for the given class type.  For
/// Swift-defined types, this is equivalent to the metatype for the
/// class, but for Objective-C-defined types, this is the class
/// object.
llvm::Value *irgen::emitClassHeapMetadataRef(IRGenFunction &IGF, CanType type,
                                             MetadataValueType desiredType,
                                             bool allowUninitialized) {
  assert(type->mayHaveSuperclass());

  // Archetypes may or may not be ObjC classes and need unwrapping to get at
  // the class object.
  if (auto archetype = dyn_cast<ArchetypeType>(type)) {
    // Look up the Swift metadata from context.
    llvm::Value *archetypeMeta = IGF.emitTypeMetadataRef(type);
    // Get the class pointer.
    auto classPtr = emitClassHeapMetadataRefForMetatype(IGF, archetypeMeta,
                                                        archetype);
    if (desiredType == MetadataValueType::ObjCClass)
      classPtr = IGF.Builder.CreateBitCast(classPtr, IGF.IGM.ObjCClassPtrTy);
    return classPtr;
  }
  
  if (ClassDecl *theClass = type->getClassOrBoundGenericClass()) {
    if (!hasKnownSwiftMetadata(IGF.IGM, theClass)) {
      llvm::Value *result =
        emitObjCHeapMetadataRef(IGF, theClass, allowUninitialized);
      if (desiredType == MetadataValueType::TypeMetadata)
        result = IGF.Builder.CreateBitCast(result, IGF.IGM.TypeMetadataPtrTy);
      return result;
    }
  }

  llvm::Value *result = IGF.emitTypeMetadataRef(type);
  if (desiredType == MetadataValueType::ObjCClass)
    result = IGF.Builder.CreateBitCast(result, IGF.IGM.ObjCClassPtrTy);
  return result;
}

/// Emit a metatype value for a known type.
void irgen::emitMetatypeRef(IRGenFunction &IGF, CanMetatypeType type,
                            Explosion &explosion) {
  switch (type->getRepresentation()) {
  case MetatypeRepresentation::Thin:
    // Thin types have a trivial representation.
    break;

  case MetatypeRepresentation::Thick:
    explosion.add(IGF.emitTypeMetadataRef(type.getInstanceType()));
    break;

  case MetatypeRepresentation::ObjC:
    explosion.add(emitClassHeapMetadataRef(IGF, type.getInstanceType(),
                                           MetadataValueType::ObjCClass));
    break;
  }
}

/*****************************************************************************/
/** Nominal Type Descriptor Emission *****************************************/
/*****************************************************************************/

template <class Flags>
static Flags getMethodDescriptorFlags(ValueDecl *fn) {
  if (isa<ConstructorDecl>(fn))
    return Flags(Flags::Kind::Init); // 'init' is considered static

  auto kind = [&] {
    switch (cast<FuncDecl>(fn)->getAccessorKind()) {
    case AccessorKind::NotAccessor:
      return Flags::Kind::Method;
    case AccessorKind::IsGetter:
      return Flags::Kind::Getter;
    case AccessorKind::IsSetter:
      return Flags::Kind::Setter;
    case AccessorKind::IsMaterializeForSet:
      return Flags::Kind::MaterializeForSet;
    case AccessorKind::IsWillSet:
    case AccessorKind::IsDidSet:
    case AccessorKind::IsAddressor:
    case AccessorKind::IsMutableAddressor:
      llvm_unreachable("these accessors never appear in protocols or v-tables");
    }
    llvm_unreachable("bad kind");
  }();
  return Flags(kind).withIsInstance(!fn->isStatic());
}

namespace {  
  template<class Impl>
  class NominalTypeDescriptorBuilderBase {
  protected:
    Impl &asImpl() { return *static_cast<Impl*>(this); }
    IRGenModule &IGM;
  private:
    ConstantInitBuilder InitBuilder;
  protected:
    ConstantStructBuilder B;

    NominalTypeDescriptorBuilderBase(IRGenModule &IGM)
      : IGM(IGM), InitBuilder(IGM), B(InitBuilder.beginStruct()) {
      B.setPacked(true);
    }

  public:    
    void layout() {
      asImpl().addName();
      asImpl().addKindDependentFields();
      asImpl().addKind();
      asImpl().addAccessFunction();
      asImpl().addGenericParams();
    }

    CanType getAbstractType() {
      return asImpl().getTarget()->getDeclaredType()->getCanonicalType();
    }

    void addName() {
      B.addRelativeAddress(getMangledTypeName(IGM, getAbstractType(),
                                 /*willBeRelativelyAddressed*/ true));
    }
    
    void addKind() {
      auto kind = asImpl().getKind();
      B.addInt32(kind);
    }

    void addAccessFunction() {
      NominalTypeDecl *typeDecl = asImpl().getTarget();
      llvm::Constant *accessFn =
        getRequiredTypeMetadataAccessFunction(IGM, typeDecl, NotForDefinition);
      B.addRelativeAddressOrNull(accessFn);
    }
    
    void addGenericParams() {
      NominalTypeDecl *ntd = asImpl().getTarget();

      // uint32_t GenericParameterVectorOffset;
      B.addInt32(asImpl().getGenericParamsOffset() / IGM.getPointerSize());

      // The archetype order here needs to be consistent with
      // MetadataVisitor::addGenericFields.
      
      GenericTypeRequirements requirements(IGM, ntd);
      
      // uint32_t NumGenericRequirements;
      B.addInt32(requirements.getStorageSizeInWords());

      // uint32_t NumPrimaryGenericParameters;
      B.addInt32(requirements.getNumTypeRequirements());

      // GenericParameterDescriptorFlags Flags;
      GenericParameterDescriptorFlags flags;
      if (auto *cd = dyn_cast<ClassDecl>(ntd)) {
        auto &layout = IGM.getMetadataLayout(cd);
        if (layout.getVTableSize() > 0)
          flags = flags.withHasVTable(true);
      }

      // Calculate the number of generic parameters at each nesting depth.
      unsigned totalGenericParams = 0;
      SmallVector<unsigned, 2> numPrimaryParams;
      for (auto *outer = ntd; outer != nullptr;
           outer = outer->getDeclContext()
               ->getAsNominalTypeOrNominalTypeExtensionContext()) {
        unsigned genericParamsAtDepth = 0;
        if (auto *genericParams = outer->getGenericParams()) {
          for (auto *paramDecl : *genericParams) {
            auto contextTy = ntd->mapTypeIntoContext(
                paramDecl->getDeclaredInterfaceType());
            // Skip parameters which have been made concrete, because they do
            // not appear in type metadata.
            //
            // FIXME: We should emit information about same-type constraints
            // as well as conformance constraints, so that clients can
            // reconstruct the full generic signature of the type, including
            // fully-concrete parameters.
            if (contextTy->is<ArchetypeType>()) {
              totalGenericParams++;
              genericParamsAtDepth++;
            }
          }
        }
        numPrimaryParams.push_back(genericParamsAtDepth);
      }

      // This assertion will fail once we have generic types nested
      // inside generic functions or other local generic contexts.
      assert(totalGenericParams == requirements.getNumTypeRequirements());

      // Emit the nesting depth.
      B.addInt16(numPrimaryParams.size());

      // Emit the flags.
      B.addInt16(flags.getIntValue());

      // Emit the number of generic parameters at each nesting depth.
      std::reverse(numPrimaryParams.begin(), numPrimaryParams.end());
      for (auto count : numPrimaryParams)
        B.addInt32(count);

      // TODO: provide reflective descriptions of the type and
      // conformance requirements stored here.
    }

    llvm::Constant *emit() {
      asImpl().layout();

      auto addr = IGM.getAddrOfNominalTypeDescriptor(asImpl().getTarget(),
                                                     B.finishAndCreateFuture());
      auto var = cast<llvm::GlobalVariable>(addr);

      var->setConstant(true);
      IGM.setTrueConstGlobal(var);

      return var;
    }
    
    // Derived class must provide:
    //   NominalTypeDecl *getTarget();
    //   unsigned getKind();
    //   unsigned getGenericParamsOffset();
    //   void addKindDependentFields();
  };
  
  /// Build a doubly-null-terminated list of field names.
  template<typename ValueDeclRange>
  unsigned getFieldNameString(const ValueDeclRange &fields,
                              llvm::SmallVectorImpl<char> &out) {
    unsigned numFields = 0;

    {
      llvm::raw_svector_ostream os(out);
      
      for (ValueDecl *prop : fields) {
        os << prop->getBaseName() << '\0';
        ++numFields;
      }
      // The final null terminator is provided by getAddrOfGlobalString.
    }
    return numFields;
  }
  
  /// Build the field type vector accessor for a nominal type. This is a
  /// function that lazily instantiates the type metadata for all of the
  /// types of the stored properties of an instance of a nominal type.
  static llvm::Function *
  getFieldTypeAccessorFn(IRGenModule &IGM,
                         NominalTypeDecl *type,
                         ArrayRef<FieldTypeInfo> fieldTypes) {
    // The accessor function has the following signature:
    // const Metadata * const *(*GetFieldTypes)(const Metadata *T);
    auto metadataArrayPtrTy = IGM.TypeMetadataPtrTy->getPointerTo();
    auto fnTy = llvm::FunctionType::get(metadataArrayPtrTy,
                                        IGM.TypeMetadataPtrTy,
                                        /*vararg*/ false);
    auto fn = llvm::Function::Create(fnTy, llvm::GlobalValue::PrivateLinkage,
                                     llvm::Twine("get_field_types_")
                                       + type->getName().str(),
                                     IGM.getModule());
    fn->setAttributes(IGM.constructInitialAttributes());

    // Emit the body of the field type accessor later. We need to access
    // the type metadata for the fields, which could lead to infinite recursion
    // in recursive types if we build the field type accessor during metadata
    // generation.
    IGM.addLazyFieldTypeAccessor(type, fieldTypes, fn);
    
    return fn;
  }
  
  /// Build a field type accessor for stored properties.
  static llvm::Function *
  getFieldTypeAccessorFn(IRGenModule &IGM,
                         NominalTypeDecl *type,
                         NominalTypeDecl::StoredPropertyRange storedProperties){
    SmallVector<FieldTypeInfo, 4> types;
    for (VarDecl *prop : storedProperties) {
      auto propertyType = type->mapTypeIntoContext(prop->getInterfaceType())
                              ->getCanonicalType();
      types.push_back(FieldTypeInfo(propertyType,
                                    /*indirect*/ false,
                                    propertyType->is<WeakStorageType>()));
    }
    return getFieldTypeAccessorFn(IGM, type, types);
  }
  
  /// Build a case type accessor for enum payloads.
  static llvm::Function *
  getFieldTypeAccessorFn(IRGenModule &IGM,
                         NominalTypeDecl *type,
                         ArrayRef<EnumImplStrategy::Element> enumElements) {
    SmallVector<FieldTypeInfo, 4> types;

    // This is a terrible special case, but otherwise the archetypes
    // aren't mapped correctly because the EnumImplStrategy ends up
    // using the lowered cases, i.e. the cases for Optional<>.
    if (type->classifyAsOptionalType() == OTK_ImplicitlyUnwrappedOptional) {
      assert(enumElements.size() == 1);
      auto decl = IGM.Context.getImplicitlyUnwrappedOptionalSomeDecl();
      auto caseType = decl->getParentEnum()->mapTypeIntoContext(
        decl->getArgumentInterfaceType())
          ->getCanonicalType();
      types.push_back(FieldTypeInfo(caseType, false, false));
      return getFieldTypeAccessorFn(IGM, type, types);
    }

    for (auto &elt : enumElements) {
      auto caseType = elt.decl->getParentEnum()->mapTypeIntoContext(
        elt.decl->getArgumentInterfaceType())
          ->getCanonicalType();
      bool isIndirect = elt.decl->isIndirect()
        || elt.decl->getParentEnum()->isIndirect();
      types.push_back(FieldTypeInfo(caseType, isIndirect, /*weak*/ false));
    }
    return getFieldTypeAccessorFn(IGM, type, types);
  }
  
  class StructNominalTypeDescriptorBuilder
    : public NominalTypeDescriptorBuilderBase<StructNominalTypeDescriptorBuilder>
  {
    using super
      = NominalTypeDescriptorBuilderBase<StructNominalTypeDescriptorBuilder>;
    
    // Offsets of key fields in the metadata records.
    Size FieldVectorOffset, GenericParamsOffset;
    
    StructDecl *Target;
    
  public:
    StructNominalTypeDescriptorBuilder(IRGenModule &IGM,
                                       StructDecl *s)
      : super(IGM), Target(s)
    {
      auto &layout = IGM.getMetadataLayout(Target);
      FieldVectorOffset = layout.getFieldOffsetVectorOffset().getStatic();
      GenericParamsOffset = layout.getStaticGenericRequirementsOffset();
    }
    
    StructDecl *getTarget() { return Target; }
    
    unsigned getKind() {
      return unsigned(NominalTypeKind::Struct);
    }
    
    Size getGenericParamsOffset() {
      return GenericParamsOffset;
    }
    
    void addKindDependentFields() {
      // Build the field name list.
      llvm::SmallString<64> fieldNames;
      unsigned numFields = getFieldNameString(Target->getStoredProperties(),
                                              fieldNames);
      
      B.addInt32(numFields);
      B.addInt32(FieldVectorOffset / IGM.getPointerSize());
      B.addRelativeAddress(IGM.getAddrOfGlobalString(fieldNames,
                                           /*willBeRelativelyAddressed*/ true));
      
      // Build the field type accessor function.
      llvm::Function *fieldTypeVectorAccessor
        = getFieldTypeAccessorFn(IGM, Target,
                                   Target->getStoredProperties());
      
      B.addRelativeAddress(fieldTypeVectorAccessor);
    }
  };
  
  class ClassNominalTypeDescriptorBuilder
    : public NominalTypeDescriptorBuilderBase<ClassNominalTypeDescriptorBuilder>,
      public SILVTableVisitor<ClassNominalTypeDescriptorBuilder>
  {
    using super
      = NominalTypeDescriptorBuilderBase<ClassNominalTypeDescriptorBuilder>;
    
    // Offsets of key fields in the metadata records.
    Size FieldVectorOffset, GenericParamsOffset;

    SILVTable *VTable;

    Size VTableOffset;
    unsigned VTableSize;

    ClassDecl *Target;
    
  public:
    ClassNominalTypeDescriptorBuilder(IRGenModule &IGM,
                                       ClassDecl *c)
      : super(IGM),
        SILVTableVisitor<ClassNominalTypeDescriptorBuilder>(IGM.getSILTypes()),
        Target(c)
    {
      auto &layout = IGM.getMetadataLayout(Target);
      FieldVectorOffset = layout.getStaticFieldOffsetVectorOffset();
      GenericParamsOffset = layout.getStaticGenericRequirementsOffset();

      VTable = IGM.getSILModule().lookUpVTable(Target);

      VTableOffset = layout.getStaticVTableOffset();
      VTableSize = layout.getVTableSize();
    }
    
    ClassDecl *getTarget() { return Target; }
    
    unsigned getKind() {
      return unsigned(NominalTypeKind::Class);
    }
    
    Size getGenericParamsOffset() {
      return GenericParamsOffset;
    }
    
    void addKindDependentFields() {
      // Build the field name list.
      llvm::SmallString<64> fieldNames;
      unsigned numFields = getFieldNameString(Target->getStoredProperties(),
                                              fieldNames);
      
      B.addInt32(numFields);
      B.addInt32(FieldVectorOffset / IGM.getPointerSize());
      B.addRelativeAddress(IGM.getAddrOfGlobalString(fieldNames,
                                           /*willBeRelativelyAddressed*/ true));
      
      // Build the field type accessor function.
      llvm::Function *fieldTypeVectorAccessor
        = getFieldTypeAccessorFn(IGM, Target,
                                 Target->getStoredProperties());
      
      B.addRelativeAddress(fieldTypeVectorAccessor);
    }

    void addVTableDescriptor() {
      assert(VTableSize != 0);
      B.addInt32(VTableOffset / IGM.getPointerSize());
      B.addInt32(VTableSize);

      addVTableEntries(Target);

      // TODO: Emit reflection metadata for virtual methods
    }

    void addMethod(SILDeclRef fn) {
      assert(VTable && "no vtable?!");

      auto descriptor = B.beginStruct(IGM.MethodDescriptorStructTy);

      // Classify the method.
      using Flags = MethodDescriptorFlags;
      auto flags = getMethodDescriptorFlags<Flags>(fn.getDecl());

      // Remember if the declaration was dynamic.
      if (fn.getDecl()->isDynamic())
        flags = flags.withIsDynamic(true);

      // TODO: final? open?

      auto *dc = fn.getDecl()->getDeclContext();
      assert(!isa<ExtensionDecl>(dc));

      if (fn.getDecl()->getDeclContext() == Target) {
        if (auto entry = VTable->getEntry(IGM.getSILModule(), fn)) {
          assert(entry->TheKind == SILVTable::Entry::Kind::Normal);
          auto *implFn = IGM.getAddrOfSILFunction(entry->Implementation,
                                                  NotForDefinition);
          descriptor.addRelativeAddress(implFn);
        } else {
          // The method is removed by dead method elimination.
          // It should be never called. We add a pointer to an error function.
          descriptor.addRelativeAddressOrNull(nullptr);
        }
      }

      descriptor.addInt(IGM.Int32Ty, flags.getIntValue());

      descriptor.finishAndAddTo(B);
    }

    void addMethodOverride(SILDeclRef baseRef, SILDeclRef declRef) {}

    void addPlaceholder(MissingMemberDecl *MMD) {
      llvm_unreachable("cannot generate metadata with placeholders in it");
    }

    void layout() {
      super::layout();
      if (VTableSize != 0)
        addVTableDescriptor();
    }
  };
  
  class EnumNominalTypeDescriptorBuilder
    : public NominalTypeDescriptorBuilderBase<EnumNominalTypeDescriptorBuilder>
  {
    using super
      = NominalTypeDescriptorBuilderBase<EnumNominalTypeDescriptorBuilder>;
    
    // Offsets of key fields in the metadata records.
    Size GenericParamsOffset;
    Size PayloadSizeOffset;
    
    EnumDecl *Target;
    
  public:
    EnumNominalTypeDescriptorBuilder(IRGenModule &IGM, EnumDecl *c)
      : super(IGM), Target(c)
    {
      auto &layout = IGM.getMetadataLayout(Target);
      GenericParamsOffset = layout.getStaticGenericRequirementsOffset();
      if (layout.hasPayloadSizeOffset())
        PayloadSizeOffset = layout.getPayloadSizeOffset().getStatic();
    }
    
    EnumDecl *getTarget() { return Target; }
    
    unsigned getKind() {
      return unsigned(NominalTypeKind::Enum);
    }
    
    Size getGenericParamsOffset() {
      return GenericParamsOffset;
    }
    
    void addKindDependentFields() {
      auto &strategy = getEnumImplStrategy(IGM,
                        Target->getDeclaredTypeInContext()->getCanonicalType());
      
      
      // # payload cases in the low 24 bits, payload size offset in the high 8.
      unsigned numPayloads = strategy.getElementsWithPayload().size();
      assert(numPayloads < (1<<24) && "too many payload elements for runtime");
      assert(PayloadSizeOffset % IGM.getPointerAlignment() == Size(0)
             && "payload size not word-aligned");
      unsigned PayloadSizeOffsetInWords
        = PayloadSizeOffset / IGM.getPointerSize();
      assert(PayloadSizeOffsetInWords < 0x100 &&
             "payload size offset too far from address point for runtime");
      B.addInt32(numPayloads | (PayloadSizeOffsetInWords << 24));
      // # empty cases
      B.addInt32(strategy.getElementsWithNoPayload().size());

      B.addRelativeAddressOrNull(strategy.emitCaseNames());

      // Build the case type accessor.
      llvm::Function *caseTypeVectorAccessor
        = getFieldTypeAccessorFn(IGM, Target,
                                 strategy.getElementsWithPayload());
      
      B.addRelativeAddress(caseTypeVectorAccessor);
    }
  };

} // end anonymous namespace

void
IRGenModule::addLazyFieldTypeAccessor(NominalTypeDecl *type,
                                      ArrayRef<FieldTypeInfo> fieldTypes,
                                      llvm::Function *fn) {
  IRGen.addLazyFieldTypeAccessor(type, fieldTypes, fn, this);
}

void
irgen::emitFieldTypeAccessor(IRGenModule &IGM,
                             NominalTypeDecl *type,
                             llvm::Function *fn,
                             ArrayRef<FieldTypeInfo> fieldTypes)
{
  IRGenFunction IGF(IGM, fn);
  if (IGM.DebugInfo)
    IGM.DebugInfo->emitArtificialFunction(IGF, fn);

  auto metadataArrayPtrTy = IGM.TypeMetadataPtrTy->getPointerTo();

  CanType formalType = type->getDeclaredTypeInContext()->getCanonicalType();
  llvm::Value *metadata = IGF.collectParameters().claimNext();
  setTypeMetadataName(IGM, metadata, formalType);
  
  // Get the address at which the field type vector reference should be
  // cached.
  llvm::Value *vectorPtr;
  auto nullVector = llvm::ConstantPointerNull::get(metadataArrayPtrTy);
  
  // If the type is not generic, we can use a global variable to cache the
  // address of the field type vector for the single instance.
  if (!type->isGenericContext()) {
    vectorPtr = new llvm::GlobalVariable(*IGM.getModule(),
                                         metadataArrayPtrTy,
                                         /*constant*/ false,
                                         llvm::GlobalValue::PrivateLinkage,
                                         nullVector,
                                         llvm::Twine("field_type_vector_")
                                           + type->getName().str());
  // For a generic type, use a slot we saved in the generic metadata pattern
  // immediately after the metadata object itself, which should be
  // instantiated with every generic metadata instance.
  } else {
    auto size = IGM.getMetadataLayout(type).getSize();
    Size offset = size.getOffsetToEnd();
    vectorPtr = IGF.Builder.CreateBitCast(metadata,
                                          metadataArrayPtrTy->getPointerTo());
    vectorPtr = IGF.Builder.CreateConstInBoundsGEP1_32(
        /*Ty=*/nullptr, vectorPtr, IGM.getOffsetInWords(offset));
  }
  
  // First, see if the field type vector has already been populated. This
  // load can be nonatomic; if we race to build the field offset vector, we
  // will detect so when we try to commit our pointer and simply discard the
  // redundant work.
  llvm::Value *initialVector
    = IGF.Builder.CreateLoad(vectorPtr, IGM.getPointerAlignment());
  
  auto entryBB = IGF.Builder.GetInsertBlock();
  auto buildBB = IGF.createBasicBlock("build_field_types");
  auto raceLostBB = IGF.createBasicBlock("race_lost");
  auto doneBB = IGF.createBasicBlock("done");
  
  llvm::Value *isNull
    = IGF.Builder.CreateICmpEQ(initialVector, nullVector);
  IGF.Builder.CreateCondBr(isNull, buildBB, doneBB);
  
  // Build the field type vector if we didn't already.
  IGF.Builder.emitBlock(buildBB);
  
  // Bind the metadata instance to our local type data so we
  // use it to provide metadata for generic parameters in field types.
  IGF.bindLocalTypeDataFromTypeMetadata(formalType, IsExact, metadata);
  
  // Allocate storage for the field vector.
  unsigned allocSize = fieldTypes.size() * IGM.getPointerSize().getValue();
  auto allocSizeVal = llvm::ConstantInt::get(IGM.IntPtrTy, allocSize);
  auto allocAlignMaskVal =
    IGM.getSize(IGM.getPointerAlignment().asSize() - Size(1));
  llvm::Value *builtVectorAlloc
    = IGF.emitAllocRawCall(allocSizeVal, allocAlignMaskVal);
  
  llvm::Value *builtVector
    = IGF.Builder.CreateBitCast(builtVectorAlloc, metadataArrayPtrTy);
  
  // Emit type metadata for the fields into the vector.
  for (unsigned i : indices(fieldTypes)) {
    auto fieldTy = fieldTypes[i].getType();
    auto slot = IGF.Builder.CreateInBoundsGEP(builtVector,
                      llvm::ConstantInt::get(IGM.Int32Ty, i));
    
    // Strip reference storage qualifiers like unowned and weak.
    // FIXME: Some clients probably care about them.
    if (auto refStorTy = dyn_cast<ReferenceStorageType>(fieldTy))
      fieldTy = refStorTy.getReferentType();
    
    auto metadata = IGF.emitTypeMetadataRef(fieldTy);

    auto fieldTypeInfo = fieldTypes[i];

    // Mix in flag bits.
    if (fieldTypeInfo.hasFlags()) {
      auto flags = FieldType()
        .withIndirect(fieldTypeInfo.isIndirect())
        .withWeak(fieldTypeInfo.isWeak());
      auto metadataBits = IGF.Builder.CreatePtrToInt(metadata, IGF.IGM.SizeTy);
      metadataBits = IGF.Builder.CreateOr(metadataBits,
                   llvm::ConstantInt::get(IGF.IGM.SizeTy, flags.getIntValue()));
      metadata = IGF.Builder.CreateIntToPtr(metadataBits, metadata->getType());
    }

    IGF.Builder.CreateStore(metadata, slot, IGM.getPointerAlignment());
  }
  
  // Atomically compare-exchange a pointer to our vector into the slot.
  auto vectorIntPtr = IGF.Builder.CreateBitCast(vectorPtr,
                                                IGM.IntPtrTy->getPointerTo());
  auto builtVectorInt = IGF.Builder.CreatePtrToInt(builtVector,
                                                   IGM.IntPtrTy);
  auto zero = llvm::ConstantInt::get(IGM.IntPtrTy, 0);
  
  llvm::Value *raceVectorInt = IGF.Builder.CreateAtomicCmpXchg(vectorIntPtr,
                               zero, builtVectorInt,
                               llvm::AtomicOrdering::SequentiallyConsistent,
                               llvm::AtomicOrdering::SequentiallyConsistent);

  // We might have added internal control flow above.
  buildBB = IGF.Builder.GetInsertBlock();

  // The pointer in the slot should still have been null.
  auto didStore = IGF.Builder.CreateExtractValue(raceVectorInt, 1);
  raceVectorInt = IGF.Builder.CreateExtractValue(raceVectorInt, 0);
  IGF.Builder.CreateCondBr(didStore, doneBB, raceLostBB);
  
  // If the cmpxchg failed, someone beat us to landing their field type
  // vector. Deallocate ours and return the winner.
  IGF.Builder.emitBlock(raceLostBB);
  IGF.emitDeallocRawCall(builtVectorAlloc, allocSizeVal, allocAlignMaskVal);
  auto raceVector = IGF.Builder.CreateIntToPtr(raceVectorInt,
                                               metadataArrayPtrTy);
  IGF.Builder.CreateBr(doneBB);
  
  // Return the result.
  IGF.Builder.emitBlock(doneBB);
  auto phi = IGF.Builder.CreatePHI(metadataArrayPtrTy, 3);
  phi->addIncoming(initialVector, entryBB);
  phi->addIncoming(builtVector, buildBB);
  phi->addIncoming(raceVector, raceLostBB);
  
  IGF.Builder.CreateRet(phi);
}

/*****************************************************************************/
/** Metadata Emission ********************************************************/
/*****************************************************************************/

namespace {
  /// An adapter class which turns a metadata layout class into a
  /// generic metadata layout class.
  template <class Impl, class Base>
  class GenericMetadataBuilderBase : public Base {
    typedef Base super;

    /// The number of generic witnesses in the type we're emitting.
    /// This is not really something we need to track.
    unsigned NumGenericWitnesses = 0;

    struct FillOp {
      CanType Type;
      Optional<ProtocolConformanceRef> Conformance;
      Size ToOffset;
      bool IsRelative;
    };

    SmallVector<FillOp, 8> FillOps;

    enum { TemplateHeaderFieldCount = 5 };
    enum { NumPrivateDataWords = swift::NumGenericMetadataPrivateDataWords };

  protected:
    Size TemplateHeaderSize;

    /// The offset of the address point in the type we're emitting.
    Size AddressPoint = Size::invalid();
    
    IRGenModule &IGM = super::IGM;
    using super::asImpl;
    using super::Target;
    using super::B;
    
    /// Set to true if the metadata record for the generic type has fields
    /// outside of the generic parameter vector.
    bool HasDependentMetadata = false;
    
    /// Set to true if the value witness table for the generic type is dependent
    /// on its generic parameters. If true, the value witness will be
    /// tail-emplaced inside the metadata pattern and initialized by the fill
    /// function. Implies HasDependentMetadata.
    bool HasDependentVWT = false;
    
    /// The offset of the tail-allocated dependent VWT, if any.
    Size DependentVWTPoint = Size::invalid();

    template <class... T>
    GenericMetadataBuilderBase(IRGenModule &IGM, T &&...args)
      : super(IGM, std::forward<T>(args)...) {}

    /// Emit the create function for the template.
    llvm::Function *emitCreateFunction() {
      // Metadata *(*CreateFunction)(GenericMetadata*, const void * const *)
      llvm::Type *argTys[] = {IGM.TypeMetadataPatternPtrTy, IGM.Int8PtrPtrTy};
      auto ty = llvm::FunctionType::get(IGM.TypeMetadataPtrTy,
                                        argTys, /*isVarArg*/ false);
      llvm::Function *f = llvm::Function::Create(ty,
                                           llvm::GlobalValue::PrivateLinkage,
                                           llvm::Twine("create_generic_metadata_")
                                               + Target->getName().str(),
                                           &IGM.Module);
      f->setAttributes(IGM.constructInitialAttributes());
      
      IRGenFunction IGF(IGM, f);

      // Skip instrumentation when building for TSan to avoid false positives.
      // The synchronization for this happens in the Runtime and we do not see it.
      if (IGM.IRGen.Opts.Sanitizers & SanitizerKind::Thread)
        f->removeFnAttr(llvm::Attribute::SanitizeThread);

      if (IGM.DebugInfo)
        IGM.DebugInfo->emitArtificialFunction(IGF, f);

      Explosion params = IGF.collectParameters();
      llvm::Value *metadataPattern = params.claimNext();
      llvm::Value *args = params.claimNext();

      // Bind the generic arguments.
      if (Target->isGenericContext()) {
        Address argsArray(args, IGM.getPointerAlignment());
        emitPolymorphicParametersFromArray(IGF, Target, argsArray);
      }

      // Allocate the metadata.
      llvm::Value *metadataValue =
        asImpl().emitAllocateMetadata(IGF, metadataPattern, args);
      
      // Execute the fill ops. Cast the parameters to word pointers because the
      // fill indexes are word-indexed.
      Address metadataWords(IGF.Builder.CreateBitCast(metadataValue, IGM.Int8PtrPtrTy),
                            IGM.getPointerAlignment());
      
      for (auto &fillOp : FillOps) {
        llvm::Value *value;
        if (fillOp.Conformance) {
          value = emitWitnessTableRef(IGF, fillOp.Type, *fillOp.Conformance);
        } else {
          value = IGF.emitTypeMetadataRef(fillOp.Type);
        }

        auto dest = createPointerSizedGEP(IGF, metadataWords,
                                          fillOp.ToOffset - AddressPoint);

        // A far relative indirectable pointer.
        if (fillOp.IsRelative) {
          dest = IGF.Builder.CreateElementBitCast(dest,
                                                  IGM.FarRelativeAddressTy);
          IGF.emitStoreOfRelativeIndirectablePointer(value, dest,
                                                     /*isFar*/ true);

        // A direct pointer.
        } else {
          value = IGF.Builder.CreateBitCast(value, IGM.Int8PtrTy);
          IGF.Builder.CreateStore(value, dest);
        }
      }
      
      // Initialize the instantiated dependent value witness table, if we have
      // one.
      llvm::Value *vwtableValue = nullptr;
      if (HasDependentVWT) {
        assert(!AddressPoint.isInvalid() && "did not set valid address point!");
        assert(!DependentVWTPoint.isInvalid() && "did not set dependent VWT point!");
        
        // Fill in the pointer from the metadata to the VWT. The VWT pointer
        // always immediately precedes the address point.
        auto vwtAddr = createPointerSizedGEP(IGF, metadataWords,
                                             DependentVWTPoint - AddressPoint);
        vwtableValue = IGF.Builder.CreateBitCast(vwtAddr.getAddress(),
                                                 IGF.IGM.WitnessTablePtrTy);

        auto vwtAddrVal = IGF.Builder.CreateBitCast(vwtableValue, IGM.Int8PtrTy);
        auto vwtRefAddr = createPointerSizedGEP(IGF, metadataWords,
                                                Size(0) - IGM.getPointerSize());
        IGF.Builder.CreateStore(vwtAddrVal, vwtRefAddr);
        
        HasDependentMetadata = true;
      }

      if (HasDependentMetadata) {
        asImpl().emitInitializeMetadata(IGF, metadataValue, vwtableValue);
      }
      
      // The metadata is now complete.
      IGF.Builder.CreateRet(metadataValue);
      
      return f;
    }

    void addFillOp(CanType type, Optional<ProtocolConformanceRef> conf,
                   bool isRelative) {
      FillOps.push_back({type, conf, getNextOffsetFromTemplateHeader(),
                         isRelative });
    }
    
  public:
    void createMetadataAccessFunction() {
      (void) getGenericTypeMetadataAccessFunction(IGM, Target, ForDefinition);
    }

    void layout() {
      TemplateHeaderSize =
        ((NumPrivateDataWords + 1) * IGM.getPointerSize()) + Size(8);

      auto privateDataInit = getPrivateDataInit();

      // Leave room for the header.
      auto createFunctionField = B.addPlaceholderWithSize(IGM.Int8PtrTy);
      auto sizeField = B.addPlaceholderWithSize(IGM.Int32Ty);
      auto numArgumentsField = B.addPlaceholderWithSize(IGM.Int16Ty);
      auto addressPointField = B.addPlaceholderWithSize(IGM.Int16Ty);
      auto privateDataField =
        B.addPlaceholderWithSize(privateDataInit->getType());

      // Lay out the template data.
      super::layout();
      
      // Save a slot for the field type vector address to be instantiated into.
      asImpl().addFieldTypeVectorReferenceSlot();
      
      // If we have a dependent value witness table, emit its template.
      if (HasDependentVWT) {
        // Note the dependent VWT offset.
        DependentVWTPoint = getNextOffsetFromTemplateHeader();
        asImpl().addDependentValueWitnessTablePattern();
      }
      
      asImpl().addDependentData();

      // Fill in the header:

      //   Metadata *(*CreateFunction)(GenericMetadata *, const void*);
      B.fillPlaceholder(createFunctionField, emitCreateFunction());
      
      //   uint32_t MetadataSize;
      // We compute this assuming that every entry in the metadata table
      // is a pointer in size.
      Size size = getNextOffsetFromTemplateHeader();
      B.fillPlaceholderWithInt(sizeField, IGM.Int32Ty, size.getValue());
      
      //   uint16_t NumArguments;
      // TODO: ultimately, this should be the number of actual template
      // arguments, not the number of witness tables required.
      unsigned numGenericArguments =
        GenericArguments::getNumGenericArguments(IGM, Target);
      B.fillPlaceholderWithInt(numArgumentsField,
                               IGM.Int16Ty, numGenericArguments);

      //   uint16_t AddressPoint;
      assert(!AddressPoint.isInvalid() && "address point not noted!");
      B.fillPlaceholderWithInt(addressPointField,
                               IGM.Int16Ty, AddressPoint.getValue());

      //   void *PrivateData[NumPrivateDataWords];
      B.fillPlaceholder(privateDataField, privateDataInit);
    }

    /// Write down the index of the address point.
    void noteAddressPoint() {
      AddressPoint = getNextOffsetFromTemplateHeader();
      super::noteAddressPoint();
    }

    /// Ignore the preallocated header.
    Size getNextOffsetFromTemplateHeader() const {
      // Note that the header fields are all pointer-sized.
      return B.getNextOffsetFromGlobal() - TemplateHeaderSize;
    }

    /// Ignore the destructor and value witness table.
    Size getNextOffsetFromAddressPoint() const {
      return getNextOffsetFromTemplateHeader() - AddressPoint;
    }

    template <class... T>
    void addGenericArgument(CanType type, T &&...args) {
      NumGenericWitnesses++;
      addFillOp(type, None, /*relative*/ false);
      super::addGenericArgument(type, std::forward<T>(args)...);
    }

    template <class... T>
    void addGenericWitnessTable(CanType type, ProtocolConformanceRef conf,
                                T &&...args) {
      NumGenericWitnesses++;
      addFillOp(type, conf, /*relative*/ false);
      super::addGenericWitnessTable(type, conf, std::forward<T>(args)...);
    }
    
    void addFieldTypeVectorReferenceSlot() {
      B.addNullPointer(IGM.TypeMetadataPtrTy->getPointerTo());
    }
    
    // Can be overridden by subclassers to emit other dependent metadata.
    void addDependentData() {}
    
  private:
    static llvm::Constant *makeArray(llvm::Type *eltTy,
                                     ArrayRef<llvm::Constant*> elts) {
      auto arrayTy = llvm::ArrayType::get(eltTy, elts.size());
      return llvm::ConstantArray::get(arrayTy, elts);
    }

    /// Produce the initializer for the private-data field of the
    /// template header.
    llvm::Constant *getPrivateDataInit() {
      auto null = llvm::ConstantPointerNull::get(IGM.Int8PtrTy);
      
      llvm::Constant *privateData[NumPrivateDataWords];
      
      for (auto &element : privateData)
        element = null;

      return makeArray(IGM.Int8PtrTy, privateData);
    }
  };
} // end anonymous namespace

llvm::Value *
irgen::emitInitializeFieldOffsetVector(IRGenFunction &IGF,
                                       SILType T,
                                       llvm::Value *metadata,
                                       llvm::Value *vwtable) {
  auto *target = T.getNominalOrBoundGenericNominal();
  llvm::Value *fieldVector
    = emitAddressOfFieldOffsetVector(IGF, metadata, target)
      .getAddress();
  
  // Collect the stored properties of the type.
  llvm::SmallVector<VarDecl*, 4> storedProperties;
  for (auto prop : target->getStoredProperties()) {
    storedProperties.push_back(prop);
  }

  // Fill out an array with the field type metadata records.
  Address fields = IGF.createAlloca(
                   llvm::ArrayType::get(IGF.IGM.Int8PtrPtrTy,
                                        storedProperties.size()),
                   IGF.IGM.getPointerAlignment(), "classFields");
  IGF.Builder.CreateLifetimeStart(fields,
                  IGF.IGM.getPointerSize() * storedProperties.size());
  fields = IGF.Builder.CreateStructGEP(fields, 0, Size(0));

  unsigned index = 0;
  for (auto prop : storedProperties) {
    auto propTy = T.getFieldType(prop, IGF.getSILModule());
    llvm::Value *metadata = IGF.emitTypeLayoutRef(propTy);
    Address field = IGF.Builder.CreateConstArrayGEP(fields, index,
                                                    IGF.IGM.getPointerSize());
    IGF.Builder.CreateStore(metadata, field);
    ++index;
  }

  // Ask the runtime to lay out the class.  This can relocate it if it
  // wasn't allocated with swift_allocateGenericClassMetadata.
  auto numFields = IGF.IGM.getSize(Size(storedProperties.size()));

  if (isa<ClassDecl>(target)) {
    assert(vwtable == nullptr);
    metadata = IGF.Builder.CreateCall(IGF.IGM.getInitClassMetadataUniversalFn(),
                                      {metadata, numFields,
                                       fields.getAddress(), fieldVector});
  } else {
    assert(isa<StructDecl>(target));
    IGF.Builder.CreateCall(IGF.IGM.getInitStructMetadataUniversalFn(),
                           {numFields, fields.getAddress(),
                            fieldVector, vwtable});
  }

  IGF.Builder.CreateLifetimeEnd(fields,
                  IGF.IGM.getPointerSize() * storedProperties.size());

  return metadata;
}

// Classes

namespace {
  /// An adapter for laying out class metadata.
  template <class Impl>
  class ClassMetadataBuilderBase : public ClassMetadataVisitor<Impl> {
    using super = ClassMetadataVisitor<Impl>;

  protected:
    using super::IGM;
    using super::Target;
    using super::asImpl;

    ConstantStructBuilder &B;
    const StructLayout &Layout;
    const ClassLayout &FieldLayout;
    SILVTable *VTable;

    struct MethodOverride {
      Size Offset;
      SILFunction *Method;
    };

    SmallVector<MethodOverride, 4> Overrides;

    ClassMetadataBuilderBase(IRGenModule &IGM, ClassDecl *theClass,
                             ConstantStructBuilder &builder,
                             const StructLayout &layout,
                             const ClassLayout &fieldLayout)
      : super(IGM, theClass), B(builder),
        Layout(layout), FieldLayout(fieldLayout) {
      VTable = IGM.getSILModule().lookUpVTable(Target);
    }

  public:
    /// The 'metadata flags' field in a class is actually a pointer to
    /// the metaclass object for the class.
    ///
    /// NONAPPLE: This is only really required for ObjC interop; maybe
    /// suppress this for classes that don't need to be exposed to
    /// ObjC, e.g. for non-Apple platforms?
    void addMetadataFlags() {
      static_assert(unsigned(MetadataKind::Class) == 0,
                    "class metadata kind is non-zero?");

      if (IGM.ObjCInterop) {
        // Get the metaclass pointer as an intptr_t.
        auto metaclass = IGM.getAddrOfMetaclassObject(Target,
                                                      NotForDefinition);
        auto flags =
          llvm::ConstantExpr::getPtrToInt(metaclass, IGM.MetadataKindTy);
        B.add(flags);
      } else {
        // On non-objc platforms just fill it with a null, there
        // is no Objective-C metaclass.
        // FIXME: Remove this to save metadata space.
        // rdar://problem/18801263
        B.addInt(IGM.MetadataKindTy, unsigned(MetadataKind::Class));
      }
    }

    /// The runtime provides a value witness table for Builtin.NativeObject.
    void addValueWitnessTable() {
      ClassDecl *cls = Target;
      
      auto type = (cls->checkObjCAncestry() != ObjCClassKind::NonObjC
                   ? IGM.Context.TheUnknownObjectType
                   : IGM.Context.TheNativeObjectType);
      auto wtable = IGM.getAddrOfValueWitnessTable(type);
      B.add(wtable);
    }

    void addDestructorFunction() {
      auto expansion = ResilienceExpansion::Minimal;
      auto dtorRef = SILDeclRef(Target->getDestructor(),
                                SILDeclRef::Kind::Deallocator,
                                expansion);
      SILFunction *dtorFunc = IGM.getSILModule().lookUpFunction(dtorRef);
      if (dtorFunc) {
        B.add(IGM.getAddrOfSILFunction(dtorFunc, NotForDefinition));
      } else {
        // In case the optimizer removed the function. See comment in
        // addMethod().
        B.addNullPointer(IGM.FunctionPtrTy);
      }
    }

    void addNominalTypeDescriptor() {
      auto descriptor = ClassNominalTypeDescriptorBuilder(IGM, Target).emit();
      B.add(descriptor);
    }

    void addIVarDestroyer() {
      auto dtorFunc = IGM.getAddrOfIVarInitDestroy(Target,
                                                   /*isDestroyer=*/ true,
                                                   /*isForeign=*/ false,
                                                   NotForDefinition);
      if (dtorFunc) {
        B.add(*dtorFunc);
      } else {
        B.addNullPointer(IGM.FunctionPtrTy);
      }
    }

    bool addReferenceToHeapMetadata(CanType type, bool allowUninitialized) {
      if (llvm::Constant *metadata
            = tryEmitConstantHeapMetadataRef(IGM, type, allowUninitialized)) {
        B.add(metadata);
        return true;
      } else {
        // Leave a null pointer placeholder to be filled at runtime
        B.addNullPointer(IGM.TypeMetadataPtrTy);
        return false;
      }
    }

    void addClassFlags() {
      // Always set a flag saying that this is a Swift 1.0 class.
      ClassFlags flags = ClassFlags::IsSwift1;

      // Set a flag if the class uses Swift 1.0 refcounting.
      auto type = Target->getDeclaredType()->getCanonicalType();
      if (getReferenceCountingForType(IGM, type)
            == ReferenceCounting::Native) {
        flags |= ClassFlags::UsesSwift1Refcounting;
      }

      DeclAttributes attrs = Target->getAttrs();
      if (auto objc = attrs.getAttribute<ObjCAttr>()) {
        if (objc->getName())
          flags |= ClassFlags::HasCustomObjCName;
      }
      if (attrs.hasAttribute<ObjCRuntimeNameAttr>())
        flags |= ClassFlags::HasCustomObjCName;

      B.addInt32((uint32_t) flags);
    }

    void addInstanceAddressPoint() {
      // Right now, we never allocate fields before the address point.
      B.addInt32(0);
    }

    void addInstanceSize() {
      if (llvm::Constant *size
            = tryEmitClassConstantFragileInstanceSize(IGM, Target)) {
        // We only support a maximum 32-bit instance size.
        if (IGM.SizeTy != IGM.Int32Ty)
          size = llvm::ConstantExpr::getTrunc(size, IGM.Int32Ty);
        B.add(size);
      } else {
        // Leave a zero placeholder to be filled at runtime
        B.addInt32(0);
      }
    }
    
    void addInstanceAlignMask() {
      if (llvm::Constant *align
            = tryEmitClassConstantFragileInstanceAlignMask(IGM, Target)) {
        if (IGM.SizeTy != IGM.Int16Ty)
          align = llvm::ConstantExpr::getTrunc(align, IGM.Int16Ty);
        B.add(align);
      } else {
        // Leave a zero placeholder to be filled at runtime
        B.addInt16(0);
      }
    }

    void addRuntimeReservedBits() {
      B.addInt16(0);
    }

    void addClassSize() {
      auto size = IGM.getMetadataLayout(Target).getSize();
      B.addInt32(size.FullSize.getValue());
    }

    void addClassAddressPoint() {
      auto size = IGM.getMetadataLayout(Target).getSize();
      B.addInt32(size.AddressPoint.getValue());
    }
    
    void addClassCacheData() {
      // We initially fill in these fields with addresses taken from
      // the ObjC runtime.
      // FIXME: Remove null data altogether rdar://problem/18801263
      B.add(IGM.getObjCEmptyCachePtr());
      B.add(IGM.getObjCEmptyVTablePtr());
    }

    void addClassDataPointer() {
      if (!IGM.ObjCInterop) {
        // with no Objective-C runtime, just give an empty pointer with the
        // swift bit set.
        B.addInt(IGM.IntPtrTy, 1);
        return;
      }
      // Derive the RO-data.
      llvm::Constant *data = emitClassPrivateData(IGM, Target);

      // We always set the low bit to indicate this is a Swift class.
      data = llvm::ConstantExpr::getPtrToInt(data, IGM.IntPtrTy);
      data = llvm::ConstantExpr::getAdd(data,
                                    llvm::ConstantInt::get(IGM.IntPtrTy, 1));

      B.add(data);
    }

    void addFieldOffset(VarDecl *var) {
      assert(var->hasStorage());
      
      unsigned fieldIndex = FieldLayout.getFieldIndex(var);
      llvm::Constant *fieldOffsetOrZero;
      auto &element = Layout.getElement(fieldIndex);

      if (element.getKind() == ElementLayout::Kind::Fixed) {
        // Use a fixed offset if we have one.
        fieldOffsetOrZero = IGM.getSize(element.getByteOffset());
      } else {
        // Otherwise, leave a placeholder for the runtime to populate at runtime.
        fieldOffsetOrZero = IGM.getSize(Size(0));
      }
      B.add(fieldOffsetOrZero);

      if (var->getDeclContext() == Target) {
        auto access = FieldLayout.AllFieldAccesses[fieldIndex];
        switch (access) {
        case FieldAccess::ConstantDirect:
        case FieldAccess::NonConstantDirect: {
          // Emit a global variable storing the constant field offset.
          // If the superclass was imported from Objective-C, the offset
          // does not include the superclass size; we rely on the
          // Objective-C runtime sliding it down.
          //
          // TODO: Don't emit the symbol if field has a fixed offset and size
          // in all resilience domains
          auto offsetAddr = IGM.getAddrOfFieldOffset(var, /*indirect*/ false,
                                                     ForDefinition);
          auto offsetVar = cast<llvm::GlobalVariable>(offsetAddr.getAddress());
          offsetVar->setInitializer(fieldOffsetOrZero);

          // If we know the offset won't change, make it a constant.
          offsetVar->setConstant(access == FieldAccess::ConstantDirect);

          break;
        }

        case FieldAccess::ConstantIndirect:
          // No global variable is needed.
          break;

        case FieldAccess::NonConstantIndirect:
          // Emit a global variable storing an offset into the field offset
          // vector within the class metadata. This access pattern is used
          // when the field offset depends on generic parameters. As above,
          // the Objective-C runtime will slide the field offsets within the
          // class metadata to adjust for the superclass size.
          //
          // TODO: This isn't plumbed through all the way yet.
          auto offsetAddr = IGM.getAddrOfFieldOffset(var, /*indirect*/ true,
                                                     ForDefinition);
          auto offsetVar = cast<llvm::GlobalVariable>(offsetAddr.getAddress());
          offsetVar->setConstant(false);
          auto offset = getClassFieldOffsetOffset(IGM, Target, var).getValue();
          auto offsetVal = llvm::ConstantInt::get(IGM.IntPtrTy, offset);
          offsetVar->setInitializer(offsetVal);

          break;
        }
      }
    }
    
    void addFieldOffsetPlaceholders(MissingMemberDecl *placeholder) {
      for (unsigned i = 0,
                    e = placeholder->getNumberOfFieldOffsetVectorEntries();
           i < e; ++i) {
        // Emit placeholder values for some number of stored properties we
        // know exist but aren't able to reference directly.
        B.addInt(IGM.SizeTy, 0);
      }
    }

    void addMethod(SILDeclRef fn) {
      // Find the vtable entry.
      assert(VTable && "no vtable?!");
      auto entry = VTable->getEntry(IGM.getSILModule(), fn);

      // If the class is resilient or generic, the runtime will construct the
      // vtable for us. All we need to do is fix up overrides of superclass
      // methods.
      if (doesClassMetadataRequireDynamicInitialization(IGM, Target)) {
        if (entry && entry->TheKind == SILVTable::Entry::Kind::Override) {
          // Record the override so that we can fill it in later.
          Overrides.push_back({asImpl().getNextOffsetFromAddressPoint(),
                               entry->Implementation});
        }

        B.addNullPointer(IGM.FunctionPtrTy);
        return;
      }

      // The class is fragile. Emit a direct reference to the vtable entry.
      if (entry) {
        B.add(IGM.getAddrOfSILFunction(entry->Implementation, NotForDefinition));
        return;
      }

      // The method is removed by dead method elimination.
      // It should be never called. We add a pointer to an error function.
      B.addBitCast(IGM.getDeletedMethodErrorFn(), IGM.FunctionPtrTy);
    }

    void addPlaceholder(MissingMemberDecl *m) {
      assert(m->getNumberOfVTableEntries() == 0
             && "cannot generate metadata with placeholders in it");
    }

    void addMethodOverride(SILDeclRef baseRef, SILDeclRef declRef) {}

    void addGenericArgument(CanType argTy, ClassDecl *forClass) {
      B.addNullPointer(IGM.TypeMetadataPtrTy);
    }

    void addGenericWitnessTable(CanType argTy, ProtocolConformanceRef conf,
                                ClassDecl *forClass) {
      B.addNullPointer(IGM.WitnessTablePtrTy);
    }

  protected:
    bool isFinishInitializationIdempotent() {
      if (!Layout.isFixedLayout())
        return false;

      if (doesClassMetadataRequireDynamicInitialization(IGM, Target))
        return false;

      return true;
    }

    llvm::Value *emitFinishIdempotentInitialization(IRGenFunction &IGF,
                                                    llvm::Value *metadata) {
      if (IGF.IGM.ObjCInterop) {
        metadata =
          IGF.Builder.CreateBitCast(metadata, IGF.IGM.ObjCClassPtrTy);
        metadata =
          IGF.Builder.CreateCall(IGF.IGM.getGetInitializedObjCClassFn(),
                                 metadata);
        metadata =
           IGF.Builder.CreateBitCast(metadata, IGF.IGM.TypeMetadataPtrTy);
      }
      return metadata;
    }

    llvm::Value *emitFinishInitializationOfClassMetadata(IRGenFunction &IGF,
                                                      llvm::Value *metadata) {
      // We assume that we've already filled in the class's generic arguments.
      // We need to:
      //   - relocate the metadata to accommodate the superclass,
      //     if something in our hierarchy is resilient to us;
      //   - fill out the subclass's field offset vector, if its layout
      //     wasn't fixed;
      //   - copy field offsets and generic arguments from higher in the
      //     class hierarchy, if 
      //   - copy the superclass data, if there are generic arguments
      //     or field offset vectors there that weren't filled in;
      //   - populate the field offset vector, if layout isn't fixed, and
      //   - register the class with the ObjC runtime, if ObjC interop is
      //     enabled.
      //
      // emitInitializeFieldOffsetVector will do everything in the full case.
      if (doesClassMetadataRequireDynamicInitialization(IGF.IGM, Target)) {
        auto classTy = Target->getDeclaredTypeInContext()->getCanonicalType();
        auto loweredClassTy = IGF.IGM.getLoweredType(classTy);
        metadata = emitInitializeFieldOffsetVector(IGF, loweredClassTy,
                                                   metadata,
                                                   /*vwtable=*/nullptr);

      // TODO: do something intermediate when e.g. all we needed to do was
      // set parent metadata pointers.

      // Otherwise, all we need to do is register with the ObjC runtime.
      } else {
        metadata = emitFinishIdempotentInitialization(IGF, metadata);
        assert(Overrides.empty());
      }

      emitInitializeMethodOverrides(IGF, metadata);

      // Realizing the class with the ObjC runtime will copy back to the
      // field offset globals for us; but if ObjC interop is disabled, we
      // have to do that ourselves, assuming we didn't just emit them all
      // correctly in the first place.
      if (!Layout.isFixedLayout() && !IGF.IGM.ObjCInterop)
        emitInitializeFieldOffsets(IGF, metadata);

      return metadata;
    }

    // Update vtable entries for method overrides. The runtime copies in
    // the vtable from the superclass for us; we have to install method
    // overrides ourselves.
    void emitInitializeMethodOverrides(IRGenFunction &IGF,
                                       llvm::Value *metadata) {
      if (Overrides.empty())
        return;

      Address metadataWords(
          IGF.Builder.CreateBitCast(metadata, IGM.Int8PtrPtrTy),
          IGM.getPointerAlignment());

      for (auto Override : Overrides) {
        auto *implFn = IGM.getAddrOfSILFunction(Override.Method,
                                                NotForDefinition);

        auto dest = createPointerSizedGEP(IGF, metadataWords,
                                          Override.Offset);
        auto *value = IGF.Builder.CreateBitCast(implFn, IGM.Int8PtrTy);
        IGF.Builder.CreateStore(value, dest);
      }
    }

    // The Objective-C runtime will copy field offsets from the field offset
    // vector into field offset globals for us, if present. If there's no
    // Objective-C runtime, we have to do this ourselves.
    void emitInitializeFieldOffsets(IRGenFunction &IGF,
                                    llvm::Value *metadata) {
      unsigned index = FieldLayout.InheritedStoredProperties.size();

      for (auto prop : Target->getStoredProperties()) {
        auto access = FieldLayout.AllFieldAccesses[index];
        if (access == FieldAccess::NonConstantDirect) {
          Address offsetA = IGF.IGM.getAddrOfFieldOffset(prop,
                                                         /*indirect*/ false,
                                                         ForDefinition);

          // We can't use emitClassFieldOffset() here because that creates
          // an invariant load, which could be hoisted above the point
          // where the metadata becomes fully initialized
          auto slot =
            emitAddressOfClassFieldOffset(IGF, metadata, Target, prop);
          auto offsetVal = IGF.emitInvariantLoad(slot);
          IGF.Builder.CreateStore(offsetVal, offsetA);
        }

        index++;
      }
    }
  };

  class ClassMetadataBuilder :
    public ClassMetadataBuilderBase<ClassMetadataBuilder> {

    using super = ClassMetadataBuilderBase<ClassMetadataBuilder>;

    bool HasUnfilledSuperclass = false;

    Size AddressPoint;

  public:
    ClassMetadataBuilder(IRGenModule &IGM, ClassDecl *theClass,
                         ConstantStructBuilder &builder,
                         const StructLayout &layout,
                         const ClassLayout &fieldLayout)
      : ClassMetadataBuilderBase(IGM, theClass, builder, layout, fieldLayout) {
    }

    void noteAddressPoint() {
      super::noteAddressPoint();
      AddressPoint = B.getNextOffsetFromGlobal();
    }

    Size getNextOffsetFromAddressPoint() const {
      return B.getNextOffsetFromGlobal() - AddressPoint;
    }

    void addSuperClass() {
      // If this is a root class, use SwiftObject as our formal parent.
      if (!Target->hasSuperclass()) {
        // This is only required for ObjC interoperation.
        if (!IGM.ObjCInterop) {
          B.addNullPointer(IGM.TypeMetadataPtrTy);
          return;
        }

        // We have to do getAddrOfObjCClass ourselves here because
        // the ObjC runtime base needs to be ObjC-mangled but isn't
        // actually imported from a clang module.
        B.add(IGM.getAddrOfObjCClass(
                               IGM.getObjCRuntimeBaseForSwiftRootClass(Target),
                               NotForDefinition));
        return;
      }

      Type superclassTy = Target->mapTypeIntoContext(Target->getSuperclass());

      if (!addReferenceToHeapMetadata(superclassTy->getCanonicalType(),
                                      /*allowUninit*/ false)) {
        HasUnfilledSuperclass = true;
      }
      
      // If the superclass came from another module, we may have dropped
      // stored properties due to the Swift language version availability of
      // their types. In these cases we can't precisely lay out the ivars in
      // the class object at compile time so we need to do runtime layout.
      if (classHasIncompleteLayout(IGM,
                                 superclassTy->getClassOrBoundGenericClass())) {
        HasUnfilledSuperclass = true;
      }
    }

    bool canBeConstant() {
      // TODO: the metadata global can actually be constant in a very
      // special case: it's not a pattern, ObjC interoperation isn't
      // required, there are no class fields, and there is nothing that
      // needs to be runtime-adjusted.
      return false;
    }

    void createMetadataAccessFunction() {
      assert(!Target->isGenericContext());
      auto type =cast<ClassType>(Target->getDeclaredType()->getCanonicalType());

      (void) getTypeMetadataAccessFunction(IGM, type, ForDefinition,
      [&](IRGenFunction &IGF, llvm::Constant *cacheVar) -> llvm::Value* {
        // There's an interesting special case where we can do the
        // initialization idempotently and thus avoid the need for a lock.
        if (!HasUnfilledSuperclass &&
            isFinishInitializationIdempotent()) {
          auto type = Target->getDeclaredType()->getCanonicalType();
          auto metadata =
            IGF.IGM.getAddrOfTypeMetadata(type, /*pattern*/ false);
          return emitFinishIdempotentInitialization(IGF, metadata);
        }

        // Otherwise, use the generic path.
        return emitInPlaceTypeMetadataAccessFunctionBody(IGF, type, cacheVar,
          [&](IRGenFunction &IGF, llvm::Value *metadata) {
            return emitInPlaceMetadataInitialization(IGF, type, metadata);
          });
      });
    }

  private:
    llvm::Value *emitInPlaceMetadataInitialization(IRGenFunction &IGF,
                                                   CanClassType type,
                                                   llvm::Value *metadata) {
      // Many of the things done by generic instantiation are unnecessary here:
      //   initializing the metaclass pointer
      //   initializing the ro-data pointer

      // Initialize the superclass if we didn't do so as a constant.
      if (HasUnfilledSuperclass) {
        auto superclass = type->getSuperclass()->getCanonicalType();
        llvm::Value *superclassMetadata =
          emitClassHeapMetadataRef(IGF, superclass,
                                   MetadataValueType::TypeMetadata,
                                   /*allowUninit*/ false);
        Address superField =
          emitAddressOfSuperclassRefInClassMetadata(IGF, metadata);
        superField = IGF.Builder.CreateElementBitCast(superField,
                                                     IGF.IGM.TypeMetadataPtrTy);
        IGF.Builder.CreateStore(superclassMetadata, superField);
      }

      metadata = emitFinishInitializationOfClassMetadata(IGF, metadata);

      return metadata;
    }
  };
  
  /// A builder for metadata templates.
  class GenericClassMetadataBuilder :
    public GenericMetadataBuilderBase<GenericClassMetadataBuilder,
                      ClassMetadataBuilderBase<GenericClassMetadataBuilder>>
  {
    typedef GenericMetadataBuilderBase super;

    Size MetaclassPtrOffset = Size::invalid();
    Size ClassRODataPtrOffset = Size::invalid();
    Size MetaclassRODataPtrOffset = Size::invalid();
    Size DependentMetaclassPoint = Size::invalid();
    Size DependentClassRODataPoint = Size::invalid();
    Size DependentMetaclassRODataPoint = Size::invalid();
  public:
    GenericClassMetadataBuilder(IRGenModule &IGM, ClassDecl *theClass,
                                ConstantStructBuilder &B,
                                const StructLayout &layout,
                                const ClassLayout &fieldLayout)
      : super(IGM, theClass, B, layout, fieldLayout)
    {
      // We need special initialization of metadata objects to trick the ObjC
      // runtime into initializing them.
      HasDependentMetadata = true;
    }

    void addSuperClass() {
      // Filled in by the runtime.
      B.addNullPointer(IGM.TypeMetadataPtrTy);
    }

    llvm::Value *emitAllocateMetadata(IRGenFunction &IGF,
                                      llvm::Value *metadataPattern,
                                      llvm::Value *arguments) {
      llvm::Value *superMetadata;
      if (Target->hasSuperclass()) {
        Type superclass = Target->getSuperclass();
        superclass = Target->mapTypeIntoContext(superclass);
        superMetadata =
          emitClassHeapMetadataRef(IGF, superclass->getCanonicalType(),
                                   MetadataValueType::ObjCClass);
      } else if (IGM.ObjCInterop) {
        superMetadata = emitObjCHeapMetadataRef(IGF,
                               IGM.getObjCRuntimeBaseForSwiftRootClass(Target));
      } else {
        superMetadata
          = llvm::ConstantPointerNull::get(IGF.IGM.ObjCClassPtrTy);
      }

      return IGF.Builder.CreateCall(IGM.getAllocateGenericClassMetadataFn(),
                                    {metadataPattern, arguments, superMetadata});
    }
    
    void addMetadataFlags() {
      // The metaclass pointer will be instantiated here.
      MetaclassPtrOffset = getNextOffsetFromTemplateHeader();
      B.addInt(IGM.MetadataKindTy, 0);
    }
    
    void addClassDataPointer() {
      // The rodata pointer will be instantiated here.
      // Make sure we at least set the 'is Swift class' bit, though.
      ClassRODataPtrOffset = getNextOffsetFromTemplateHeader();
      B.addInt(IGM.MetadataKindTy, 1);
    }
    
    void addDependentData() {
      if (!IGM.ObjCInterop) {
        // Every piece of data in the dependent data appears to be related to
        // Objective-C information. If we're not doing Objective-C interop, we
        // can just skip adding it to the class.
        return;
      }
      // Emit space for the dependent metaclass.
      DependentMetaclassPoint = getNextOffsetFromTemplateHeader();
      // isa
      ClassDecl *rootClass = getRootClassForMetaclass(IGM, Target);
      auto isa = IGM.getAddrOfMetaclassObject(rootClass, NotForDefinition);
      B.add(isa);
      // super, which is dependent if the superclass is generic
      B.addNullPointer(IGM.ObjCClassPtrTy);
      // cache
      B.add(IGM.getObjCEmptyCachePtr());
      // vtable
      B.add(IGM.getObjCEmptyVTablePtr());
      // rodata, which is always dependent
      MetaclassRODataPtrOffset = getNextOffsetFromTemplateHeader();
      B.addInt(IGM.IntPtrTy, 0);

      std::tie(DependentClassRODataPoint, DependentMetaclassRODataPoint)
        = emitClassPrivateDataFields(IGM, B, Target);
      DependentClassRODataPoint -= TemplateHeaderSize;
      DependentMetaclassRODataPoint -= TemplateHeaderSize;
    }
                            
    void addDependentValueWitnessTablePattern() {
      llvm_unreachable("classes should never have dependent vwtables");
    }

    void noteStartOfFieldOffsets(ClassDecl *whichClass) {
      HasDependentMetadata = true;
    }
    
    void noteEndOfFieldOffsets(ClassDecl *whichClass) {}
    
    // Suppress GenericMetadataBuilderBase's default behavior of introducing
    // fill ops for generic arguments unless they belong directly to the target
    // class and not its ancestors.

    void addGenericArgument(CanType type, ClassDecl *forClass) {
      if (forClass == Target) {
        // Introduce the fill op.
        GenericMetadataBuilderBase::addGenericArgument(type, forClass);
      } else {
        // Lay out the field, but don't fill it in, we will copy it from
        // the superclass.
        HasDependentMetadata = true;
        ClassMetadataBuilderBase::addGenericArgument(type, forClass);
      }
    }
    
    void addGenericWitnessTable(CanType type, ProtocolConformanceRef conf,
                                ClassDecl *forClass) {
      if (forClass == Target) {
        // Introduce the fill op.
        GenericMetadataBuilderBase::addGenericWitnessTable(type, conf,forClass);
      } else {
        // Lay out the field, but don't provide the fill op, which we'll get
        // from the superclass.
        HasDependentMetadata = true;
        ClassMetadataBuilderBase::addGenericWitnessTable(type, conf, forClass);
      }
    }

    void emitInitializeMetadata(IRGenFunction &IGF,
                                llvm::Value *metadata,
                                llvm::Value *vwtable) {
      assert(!HasDependentVWT && "class should never have dependent VWT");

      // Fill in the metaclass pointer.
      Address metadataPtr(IGF.Builder.CreateBitCast(metadata, IGF.IGM.Int8PtrPtrTy),
                          IGF.IGM.getPointerAlignment());
      
      llvm::Value *metaclass;
      if (IGF.IGM.ObjCInterop) {
        assert(!DependentMetaclassPoint.isInvalid());
        assert(!MetaclassPtrOffset.isInvalid());

        Address metaclassPtrSlot = createPointerSizedGEP(IGF, metadataPtr,
                                            MetaclassPtrOffset - AddressPoint);
        metaclassPtrSlot = IGF.Builder.CreateBitCast(metaclassPtrSlot,
                                        IGF.IGM.ObjCClassPtrTy->getPointerTo());
        Address metaclassRawPtr = createPointerSizedGEP(IGF, metadataPtr,
                                        DependentMetaclassPoint - AddressPoint);
        metaclass = IGF.Builder.CreateBitCast(metaclassRawPtr,
                                              IGF.IGM.ObjCClassPtrTy)
          .getAddress();
        IGF.Builder.CreateStore(metaclass, metaclassPtrSlot);
      } else {
        // FIXME: Remove altogether rather than injecting a NULL value.
        // rdar://problem/18801263
        assert(!MetaclassPtrOffset.isInvalid());
        Address metaclassPtrSlot = createPointerSizedGEP(IGF, metadataPtr,
                                            MetaclassPtrOffset - AddressPoint);
        metaclassPtrSlot = IGF.Builder.CreateBitCast(metaclassPtrSlot,
                                        IGF.IGM.ObjCClassPtrTy->getPointerTo());
        IGF.Builder.CreateStore(
          llvm::ConstantPointerNull::get(IGF.IGM.ObjCClassPtrTy), 
          metaclassPtrSlot);
      }
      
      // Fill in the rodata reference in the class.
      Address classRODataPtr;
      if (IGF.IGM.ObjCInterop) {
        assert(!DependentClassRODataPoint.isInvalid());
        assert(!ClassRODataPtrOffset.isInvalid());
        Address rodataPtrSlot = createPointerSizedGEP(IGF, metadataPtr,
                                           ClassRODataPtrOffset - AddressPoint);
        rodataPtrSlot = IGF.Builder.CreateBitCast(rodataPtrSlot,
                                              IGF.IGM.IntPtrTy->getPointerTo());
        
        classRODataPtr = createPointerSizedGEP(IGF, metadataPtr,
                                      DependentClassRODataPoint - AddressPoint);
        // Set the low bit of the value to indicate "compiled by Swift".
        llvm::Value *rodata = IGF.Builder.CreatePtrToInt(
                                classRODataPtr.getAddress(), IGF.IGM.IntPtrTy);
        rodata = IGF.Builder.CreateOr(rodata, 1);
        IGF.Builder.CreateStore(rodata, rodataPtrSlot);
      } else {
        // NOTE: Unlike other bits of the metadata that should later be removed,
        // this one is important because things check this value's flags to
        // determine what kind of object it is. That said, if those checks
        // are determined to be removable, we can remove this as well per
        // rdar://problem/18801263
        assert(!ClassRODataPtrOffset.isInvalid());
        Address rodataPtrSlot = createPointerSizedGEP(IGF, metadataPtr,
                                           ClassRODataPtrOffset - AddressPoint);
        rodataPtrSlot = IGF.Builder.CreateBitCast(rodataPtrSlot,
                                              IGF.IGM.IntPtrTy->getPointerTo());
        
        IGF.Builder.CreateStore(llvm::ConstantInt::get(IGF.IGM.IntPtrTy, 1), 
                                                                rodataPtrSlot);
      }

      // Fill in the rodata reference in the metaclass.
      Address metaclassRODataPtr;
      if (IGF.IGM.ObjCInterop) {
        assert(!DependentMetaclassRODataPoint.isInvalid());
        assert(!MetaclassRODataPtrOffset.isInvalid());
        Address rodataPtrSlot = createPointerSizedGEP(IGF, metadataPtr,
                                      MetaclassRODataPtrOffset - AddressPoint);
        rodataPtrSlot = IGF.Builder.CreateBitCast(rodataPtrSlot,
                                              IGF.IGM.IntPtrTy->getPointerTo());
        
        metaclassRODataPtr = createPointerSizedGEP(IGF, metadataPtr,
                                 DependentMetaclassRODataPoint - AddressPoint);
        llvm::Value *rodata = IGF.Builder.CreatePtrToInt(
                            metaclassRODataPtr.getAddress(), IGF.IGM.IntPtrTy);
        IGF.Builder.CreateStore(rodata, rodataPtrSlot);
      }

      // We can assume that this never relocates the metadata because
      // it should have been allocated properly for the class.
      (void) emitFinishInitializationOfClassMetadata(IGF, metadata);
    }
  };
} // end anonymous namespace

/// Emit the ObjC-compatible class symbol for a class.
/// Since LLVM and many system linkers do not have a notion of relative symbol
/// references, we emit the symbol as a global asm block.
static void emitObjCClassSymbol(IRGenModule &IGM,
                                ClassDecl *classDecl,
                                llvm::GlobalValue *metadata) {
  llvm::SmallString<32> classSymbol;
  LinkEntity::forObjCClass(classDecl).mangle(classSymbol);
  
  // Create the alias.
  auto *metadataTy = cast<llvm::PointerType>(metadata->getType());

  // Create the alias.
  auto *alias = llvm::GlobalAlias::create(metadataTy->getElementType(),
                                          metadataTy->getAddressSpace(),
                                          metadata->getLinkage(),
                                          classSymbol.str(), metadata,
                                          IGM.getModule());
  alias->setVisibility(metadata->getVisibility());

  if (IGM.useDllStorage())
    alias->setDLLStorageClass(metadata->getDLLStorageClass());
}

/// Emit the type metadata or metadata template for a class.
void irgen::emitClassMetadata(IRGenModule &IGM, ClassDecl *classDecl,
                              const StructLayout &layout,
                              const ClassLayout &fieldLayout) {
  assert(!classDecl->isForeign());

  // Set up a dummy global to stand in for the metadata object while we produce
  // relative references.
  ConstantInitBuilder builder(IGM);
  auto init = builder.beginStruct();
  init.setPacked(true);

  bool isPattern;
  bool canBeConstant;
  if (classDecl->isGenericContext()) {
    GenericClassMetadataBuilder builder(IGM, classDecl, init,
                                        layout, fieldLayout);
    builder.layout();
    isPattern = true;
    canBeConstant = false;

    maybeEmitNominalTypeMetadataAccessFunction(classDecl, builder);
  } else {
    ClassMetadataBuilder builder(IGM, classDecl, init, layout, fieldLayout);
    builder.layout();
    isPattern = false;
    canBeConstant = builder.canBeConstant();

    maybeEmitNominalTypeMetadataAccessFunction(classDecl, builder);
  }

  CanType declaredType = classDecl->getDeclaredType()->getCanonicalType();

  // For now, all type metadata is directly stored.
  bool isIndirect = false;

  StringRef section{};
  if (classDecl->isObjC() &&
      IGM.TargetInfo.OutputObjectFormat == llvm::Triple::MachO)
    section = "__DATA,__objc_data, regular";

  auto var = IGM.defineTypeMetadata(declaredType, isIndirect, isPattern,
                                    canBeConstant,
                                    init.finishAndCreateFuture(),
                                    section);

  // Add classes that don't require dynamic initialization to the
  // ObjC class list.
  if (IGM.ObjCInterop && !isPattern && !isIndirect &&
      !doesClassMetadataRequireDynamicInitialization(IGM, classDecl)) {
    // Emit the ObjC class symbol to make the class visible to ObjC.
    if (classDecl->isObjC()) {
      emitObjCClassSymbol(IGM, classDecl, var);
    }

    IGM.addObjCClass(var,
              classDecl->getAttrs().hasAttribute<ObjCNonLazyRealizationAttr>());
  }
}

llvm::Value *IRGenFunction::emitInvariantLoad(Address address,
                                              const llvm::Twine &name) {
  auto load = Builder.CreateLoad(address, name);
  setInvariantLoad(load);
  return load;
}

void IRGenFunction::setInvariantLoad(llvm::LoadInst *load) {
  load->setMetadata(IGM.InvariantMetadataID, IGM.InvariantNode);
}

void IRGenFunction::setDereferenceableLoad(llvm::LoadInst *load,
                                           unsigned size) {
  auto sizeConstant = llvm::ConstantInt::get(IGM.Int64Ty, size);
  auto sizeNode = llvm::MDNode::get(IGM.LLVMContext,
                                  llvm::ConstantAsMetadata::get(sizeConstant));
  load->setMetadata(IGM.DereferenceableID, sizeNode);
}

/// Emit a load from the given metadata at a constant index.
///
/// The load is marked invariant. This function should not be called
/// on metadata objects that are in the process of being initialized.
static llvm::LoadInst *
emitInvariantLoadFromMetadataAtIndex(IRGenFunction &IGF,
                                     llvm::Value *metadata,
                                     int index,
                                     llvm::Type *objectTy,
                               const Twine &suffix = Twine::createNull()) {
  auto result = emitLoadFromMetadataAtIndex(IGF, metadata, index, objectTy,
                                            suffix);
  IGF.setInvariantLoad(result);
  return result;
}

/// Given an AST type, load its value witness table.
llvm::Value *
IRGenFunction::emitValueWitnessTableRef(CanType type) {
  // See if we have a cached projection we can use.
  if (auto cached = tryGetLocalTypeData(type,
                                  LocalTypeDataKind::forValueWitnessTable())) {
    return cached;
  }
  
  auto metadata = emitTypeMetadataRef(type);
  auto vwtable = emitValueWitnessTableRefForMetadata(metadata);
  setScopedLocalTypeData(type, LocalTypeDataKind::forValueWitnessTable(),
                         vwtable);
  return vwtable;
}

/// Given a type metadata pointer, load its value witness table.
llvm::Value *
IRGenFunction::emitValueWitnessTableRefForMetadata(llvm::Value *metadata) {
  auto witness = emitInvariantLoadFromMetadataAtIndex(*this, metadata, -1,
                                                      IGM.WitnessTablePtrTy,
                                                      ".valueWitnesses");
  // A value witness table is dereferenceable to the number of value witness
  // pointers.
  
  // TODO: If we know the type statically has extra inhabitants, we know
  // there are more witnesses.
  auto numValueWitnesses
    = unsigned(ValueWitness::Last_RequiredValueWitness) + 1;
  setDereferenceableLoad(witness,
                         IGM.getPointerSize().getValue() * numValueWitnesses);
  return witness;
}

/// Given a lowered SIL type, load a value witness table that represents its
/// layout.
llvm::Value *
IRGenFunction::emitValueWitnessTableRef(SILType type,
                                        llvm::Value **metadataSlot) {
  // See if we have a cached projection we can use.
  if (auto cached = tryGetLocalTypeDataForLayout(type,
                                  LocalTypeDataKind::forValueWitnessTable())) {
    if (metadataSlot)
      *metadataSlot = emitTypeMetadataRefForLayout(type);
    return cached;
  }
  
  auto metadata = emitTypeMetadataRefForLayout(type);
  if (metadataSlot) *metadataSlot = metadata;
  auto vwtable = emitValueWitnessTableRefForMetadata(metadata);
  setScopedLocalTypeDataForLayout(type,
                                  LocalTypeDataKind::forValueWitnessTable(),
                                  vwtable);
  return vwtable;
}

/// Given a reference to class metadata of the given type,
/// load the fragile instance size and alignment of the class.
std::pair<llvm::Value *, llvm::Value *>
irgen::emitClassFragileInstanceSizeAndAlignMask(IRGenFunction &IGF,
                                                ClassDecl *theClass,
                                                llvm::Value *metadata) {
  // FIXME: The below checks should capture this property already, but
  // resilient class metadata layout is not fully implemented yet.
  auto superClass = theClass;
  do {
    if (superClass->getParentModule() != IGF.IGM.getSwiftModule()) {
      return emitClassResilientInstanceSizeAndAlignMask(IGF, theClass,
                                                        metadata);
    }
  } while ((superClass = superClass->getSuperclassDecl()));

  // If the class has fragile fixed layout, return the constant size and
  // alignment.
  if (llvm::Constant *size
        = tryEmitClassConstantFragileInstanceSize(IGF.IGM, theClass)) {
    llvm::Constant *alignMask
      = tryEmitClassConstantFragileInstanceAlignMask(IGF.IGM, theClass);
    assert(alignMask && "static size without static align");
    return {size, alignMask};
  }
 
  // Otherwise, load it from the metadata.
  return emitClassResilientInstanceSizeAndAlignMask(IGF, theClass, metadata);
}

std::pair<llvm::Value *, llvm::Value *>
irgen::emitClassResilientInstanceSizeAndAlignMask(IRGenFunction &IGF,
                                                  ClassDecl *theClass,
                                                  llvm::Value *metadata) {
  auto &layout = IGF.IGM.getMetadataLayout(theClass);

  Address metadataAsBytes(IGF.Builder.CreateBitCast(metadata, IGF.IGM.Int8PtrTy),
                          IGF.IGM.getPointerAlignment());

  Address slot = IGF.Builder.CreateConstByteArrayGEP(
      metadataAsBytes,
      layout.getInstanceSizeOffset());
  slot = IGF.Builder.CreateBitCast(slot, IGF.IGM.Int32Ty->getPointerTo());
  llvm::Value *size = IGF.Builder.CreateLoad(slot);
  if (IGF.IGM.SizeTy != IGF.IGM.Int32Ty)
    size = IGF.Builder.CreateZExt(size, IGF.IGM.SizeTy);

  slot = IGF.Builder.CreateConstByteArrayGEP(
      metadataAsBytes,
      layout.getInstanceAlignMaskOffset());
  slot = IGF.Builder.CreateBitCast(slot, IGF.IGM.Int16Ty->getPointerTo());
  llvm::Value *alignMask = IGF.Builder.CreateLoad(slot);
  alignMask = IGF.Builder.CreateZExt(alignMask, IGF.IGM.SizeTy);

  return {size, alignMask};
}

/// Given a non-tagged object pointer, load a pointer to its class object.
llvm::Value *irgen::emitLoadOfObjCHeapMetadataRef(IRGenFunction &IGF,
                                                  llvm::Value *object) {
  if (IGF.IGM.TargetInfo.hasISAMasking()) {
    object = IGF.Builder.CreateBitCast(object,
                                       IGF.IGM.IntPtrTy->getPointerTo());
    llvm::Value *metadata =
      IGF.Builder.CreateLoad(Address(object, IGF.IGM.getPointerAlignment()));
    llvm::Value *mask = IGF.Builder.CreateLoad(IGF.IGM.getAddrOfObjCISAMask());
    metadata = IGF.Builder.CreateAnd(metadata, mask);
    metadata = IGF.Builder.CreateIntToPtr(metadata, IGF.IGM.TypeMetadataPtrTy);
    return metadata;
  } else if (IGF.IGM.TargetInfo.hasOpaqueISAs()) {
    return emitHeapMetadataRefForUnknownHeapObject(IGF, object);
  } else {
    object = IGF.Builder.CreateBitCast(object,
                                  IGF.IGM.TypeMetadataPtrTy->getPointerTo());
    llvm::Value *metadata =
      IGF.Builder.CreateLoad(Address(object, IGF.IGM.getPointerAlignment()));
    return metadata;
  }
}

/// Given a pointer to a heap object (i.e. definitely not a tagged
/// pointer), load its heap metadata pointer.
static llvm::Value *emitLoadOfHeapMetadataRef(IRGenFunction &IGF,
                                              llvm::Value *object,
                                              IsaEncoding isaEncoding,
                                              bool suppressCast) {
  switch (isaEncoding) {
  case IsaEncoding::Pointer: {
    // Drill into the object pointer.  Rather than bitcasting, we make
    // an effort to do something that should explode if we get something
    // mistyped.
    llvm::StructType *structTy =
      cast<llvm::StructType>(
        cast<llvm::PointerType>(object->getType())->getElementType());

    llvm::Value *slot;

    // We need a bitcast if we're dealing with an opaque class.
    if (structTy->isOpaque()) {
      auto metadataPtrPtrTy = IGF.IGM.TypeMetadataPtrTy->getPointerTo();
      slot = IGF.Builder.CreateBitCast(object, metadataPtrPtrTy);

    // Otherwise, make a GEP.
    } else {
      auto zero = llvm::ConstantInt::get(IGF.IGM.Int32Ty, 0);

      SmallVector<llvm::Value*, 4> indexes;
      indexes.push_back(zero);
      do {
        indexes.push_back(zero);

        // Keep drilling down to the first element type.
        auto eltTy = structTy->getElementType(0);
        assert(isa<llvm::StructType>(eltTy) || eltTy == IGF.IGM.TypeMetadataPtrTy);
        structTy = dyn_cast<llvm::StructType>(eltTy);
      } while (structTy != nullptr);

      slot = IGF.Builder.CreateInBoundsGEP(object, indexes);

      if (!suppressCast) {
        slot = IGF.Builder.CreateBitCast(slot,
                                    IGF.IGM.TypeMetadataPtrTy->getPointerTo());
      }
    }

    auto metadata = IGF.Builder.CreateLoad(Address(slot,
                                               IGF.IGM.getPointerAlignment()));
    if (IGF.IGM.EnableValueNames && object->hasName())
      metadata->setName(llvm::Twine(object->getName()) + ".metadata");
    return metadata;
  }
      
  case IsaEncoding::ObjC: {
    // Feed the object pointer to object_getClass.
    llvm::Value *objcClass = emitLoadOfObjCHeapMetadataRef(IGF, object);
    objcClass = IGF.Builder.CreateBitCast(objcClass, IGF.IGM.TypeMetadataPtrTy);
    return objcClass;
  }
  }

  llvm_unreachable("Not a valid IsaEncoding.");
}

/// Given an object of class type, produce the heap metadata reference
/// as an %objc_class*.
llvm::Value *irgen::emitHeapMetadataRefForHeapObject(IRGenFunction &IGF,
                                                     llvm::Value *object,
                                                     CanType objectType,
                                                     bool suppressCast) {
  ClassDecl *theClass = objectType.getClassOrBoundGenericClass();
  if (theClass && isKnownNotTaggedPointer(IGF.IGM, theClass))
    return emitLoadOfHeapMetadataRef(IGF, object,
                                     getIsaEncodingForType(IGF.IGM, objectType),
                                     suppressCast);

  // OK, ask the runtime for the class pointer of this potentially-ObjC object.
  return emitHeapMetadataRefForUnknownHeapObject(IGF, object);
}

llvm::Value *irgen::emitHeapMetadataRefForHeapObject(IRGenFunction &IGF,
                                                     llvm::Value *object,
                                                     SILType objectType,
                                                     bool suppressCast) {
  return emitHeapMetadataRefForHeapObject(IGF, object,
                                          objectType.getSwiftRValueType(),
                                          suppressCast);
}

/// Given an opaque class instance pointer, produce the type metadata reference
/// as a %type*.
llvm::Value *irgen::emitDynamicTypeOfOpaqueHeapObject(IRGenFunction &IGF,
                                                      llvm::Value *object) {
  object = IGF.Builder.CreateBitCast(object, IGF.IGM.ObjCPtrTy);
  auto metadata = IGF.Builder.CreateCall(IGF.IGM.getGetObjectTypeFn(),
                                         object,
                                         object->getName() + ".Type");
  metadata->setDoesNotThrow();
  metadata->setOnlyReadsMemory();
  return metadata;
}

llvm::Value *irgen::
emitHeapMetadataRefForUnknownHeapObject(IRGenFunction &IGF,
                                        llvm::Value *object) {
  object = IGF.Builder.CreateBitCast(object, IGF.IGM.ObjCPtrTy);
  auto metadata = IGF.Builder.CreateCall(IGF.IGM.getGetObjectClassFn(),
                                         object,
                                         object->getName() + ".Type");
  metadata->setCallingConv(llvm::CallingConv::C);
  metadata->setDoesNotThrow();
  metadata->addAttribute(llvm::AttributeList::FunctionIndex,
                         llvm::Attribute::ReadOnly);
  return metadata;
}

/// Given an object of class type, produce the type metadata reference
/// as a %type*.
llvm::Value *irgen::emitDynamicTypeOfHeapObject(IRGenFunction &IGF,
                                                llvm::Value *object,
                                                SILType objectType,
                                                bool suppressCast) {
  // If it is known to have swift metadata, just load.
  if (hasKnownSwiftMetadata(IGF.IGM, objectType.getSwiftRValueType())) {
    return emitLoadOfHeapMetadataRef(IGF, object,
                getIsaEncodingForType(IGF.IGM, objectType.getSwiftRValueType()),
                suppressCast);
  }

  // Okay, ask the runtime for the type metadata of this
  // potentially-ObjC object.
  return emitDynamicTypeOfOpaqueHeapObject(IGF, object);
}

/// Given a class metatype, produce the necessary heap metadata
/// reference.  This is generally the metatype pointer, but may
/// instead be a reference type.
llvm::Value *irgen::emitClassHeapMetadataRefForMetatype(IRGenFunction &IGF,
                                                        llvm::Value *metatype,
                                                        CanType type) {
  // If the type is known to have Swift metadata, this is trivial.
  if (hasKnownSwiftMetadata(IGF.IGM, type))
    return metatype;

  // Otherwise, we may have to unwrap an ObjC class wrapper.
  assert(IGF.IGM.Context.LangOpts.EnableObjCInterop);
  metatype = IGF.Builder.CreateBitCast(metatype, IGF.IGM.TypeMetadataPtrTy);
  
  // Fetch the metadata for that class.
  auto call = IGF.Builder.CreateCall(IGF.IGM.getGetObjCClassFromMetadataFn(),
                                     metatype);
  call->setDoesNotThrow();
  call->setDoesNotAccessMemory();
  return call;
}

/// Load the correct virtual function for the given class method.
FunctionPointer irgen::emitVirtualMethodValue(IRGenFunction &IGF,
                                              llvm::Value *base,
                                              SILType baseType,
                                              SILDeclRef method,
                                              CanSILFunctionType methodType,
                                              bool useSuperVTable) {
  AbstractFunctionDecl *methodDecl
    = cast<AbstractFunctionDecl>(method.getDecl());

  // Find the vtable entry for this method.
  SILDeclRef overridden = IGF.IGM.getSILTypes().getOverriddenVTableEntry(method);

  // Find the metadata.
  llvm::Value *metadata;
  if (useSuperVTable) {
    auto instanceTy = baseType;
    if (auto metaTy = dyn_cast<MetatypeType>(baseType.getSwiftRValueType()))
      instanceTy = SILType::getPrimitiveObjectType(metaTy.getInstanceType());

    if (IGF.IGM.isResilient(instanceTy.getClassOrBoundGenericClass(),
                            ResilienceExpansion::Maximal)) {
      // The derived type that is making the super call is resilient,
      // for example we may be in an extension of a class outside of our
      // resilience domain. So, we need to load the superclass metadata
      // dynamically.

      metadata = emitClassHeapMetadataRef(IGF, instanceTy.getSwiftRValueType(),
                                          MetadataValueType::TypeMetadata);
      auto superField = emitAddressOfSuperclassRefInClassMetadata(IGF, metadata);
      metadata = IGF.Builder.CreateLoad(superField);
    } else {
      // Otherwise, we can directly load the statically known superclass's
      // metadata.
      auto superTy = instanceTy.getSuperclass();
      metadata = emitClassHeapMetadataRef(IGF, superTy.getSwiftRValueType(),
                                          MetadataValueType::TypeMetadata);
    }
  } else {
    if ((isa<FuncDecl>(methodDecl) && cast<FuncDecl>(methodDecl)->isStatic()) ||
        (isa<ConstructorDecl>(methodDecl) &&
         method.kind == SILDeclRef::Kind::Allocator)) {
      metadata = base;
    } else {
      metadata = emitHeapMetadataRefForHeapObject(IGF, base, baseType,
                                                  /*suppress cast*/ true);
    }
  }

  // Use the type of the method we were type-checked against, not the
  // type of the overridden method.
  auto sig = IGF.IGM.getSignature(methodType);

  auto declaringClass = cast<ClassDecl>(overridden.getDecl()->getDeclContext());

  auto methodInfo =
    IGF.IGM.getMetadataLayout(declaringClass).getMethodInfo(IGF, overridden);
  auto offset = methodInfo.getOffset();

  auto slot = IGF.emitAddressAtOffset(metadata, offset,
                                      sig.getType()->getPointerTo(),
                                      IGF.IGM.getPointerAlignment());
  auto fnPtr = IGF.emitInvariantLoad(slot);

  return FunctionPointer(fnPtr, sig);
}

//===----------------------------------------------------------------------===//
// Value types (structs and enums)
//===----------------------------------------------------------------------===//

static llvm::Value *
emitInPlaceValueTypeMetadataInitialization(IRGenFunction &IGF,
                                           CanNominalType type,
                                           llvm::Value *metadata) {
  // All the value types are basically similar.
  assert(isa<StructType>(type) || isa<EnumType>(type));

  // Set up the value witness table if it's dependent.
  SILType loweredType = IGF.IGM.getLoweredType(AbstractionPattern(type), type);
  auto &ti = IGF.IGM.getTypeInfo(loweredType);
  if (!ti.isFixedSize()) {
    // We assume that that value witness table will already have been written
    // into the metadata; just load it.
    llvm::Value *vwtable = IGF.emitValueWitnessTableRefForMetadata(metadata);

    // Initialize the metadata.
    ti.initializeMetadata(IGF, metadata, vwtable, loweredType.getAddressType());
  }

  return metadata;
}

/// Create an access function for the type metadata of the given
/// non-generic nominal type.
static void createInPlaceValueTypeMetadataAccessFunction(IRGenModule &IGM,
                                                      NominalTypeDecl *typeDecl) {
  assert(!typeDecl->isGenericContext());
  auto type =
    cast<NominalType>(typeDecl->getDeclaredType()->getCanonicalType());

  (void) getTypeMetadataAccessFunction(IGM, type, ForDefinition,
                                       [&](IRGenFunction &IGF,
                                           llvm::Constant *cacheVariable) {
    return emitInPlaceTypeMetadataAccessFunctionBody(IGF, type, cacheVariable,
      [&](IRGenFunction &IGF, llvm::Value *metadata) {
        return emitInPlaceValueTypeMetadataInitialization(IGF, type, metadata);
      });
  });
}

//===----------------------------------------------------------------------===//
// Structs
//===----------------------------------------------------------------------===//

namespace {
  /// An adapter for laying out struct metadata.
  template <class Impl>
  class StructMetadataBuilderBase : public StructMetadataVisitor<Impl> {
    using super = StructMetadataVisitor<Impl>;

  protected:
    ConstantStructBuilder &B;
    using super::IGM;
    using super::Target;
    using super::asImpl;

    StructMetadataBuilderBase(IRGenModule &IGM, StructDecl *theStruct,
                              ConstantStructBuilder &B)
      : super(IGM, theStruct), B(B) {
    }

  public:
    void addMetadataFlags() {
      B.addInt(IGM.MetadataKindTy, unsigned(MetadataKind::Struct));
    }

    void addNominalTypeDescriptor() {
      auto *descriptor = StructNominalTypeDescriptorBuilder(IGM, Target).emit();
      B.add(descriptor);
    }

    void addFieldOffset(VarDecl *var) {
      assert(var->hasStorage() &&
             "storing field offset for computed property?!");
      SILType structType =
        IGM.getLoweredType(Target->getDeclaredTypeInContext());

      llvm::Constant *offset =
        emitPhysicalStructMemberFixedOffset(IGM, structType, var);
      // If we have a fixed offset, add it. Otherwise, leave zero as a
      // placeholder.
      if (offset) {
        B.add(offset);
      } else {
        asImpl().flagUnfilledFieldOffset();
        B.addInt(IGM.IntPtrTy, 0);
      }
    }

    void addGenericArgument(CanType type) {
      B.addNullPointer(IGM.TypeMetadataPtrTy);
    }

    void addGenericWitnessTable(CanType type, ProtocolConformanceRef conf) {
      B.addNullPointer(IGM.WitnessTablePtrTy);
    }
  };

  class StructMetadataBuilder :
    public StructMetadataBuilderBase<StructMetadataBuilder> {

    bool HasUnfilledFieldOffset = false;
  public:
    StructMetadataBuilder(IRGenModule &IGM, StructDecl *theStruct,
                          ConstantStructBuilder &B)
      : StructMetadataBuilderBase(IGM, theStruct, B) {}

    void flagUnfilledFieldOffset() {
      HasUnfilledFieldOffset = true;
    }

    bool canBeConstant() {
      return !HasUnfilledFieldOffset;
    }

    void addValueWitnessTable() {
      auto type = this->Target->getDeclaredType()->getCanonicalType();
      B.add(emitValueWitnessTable(IGM, type));
    }

    void createMetadataAccessFunction() {
      createInPlaceValueTypeMetadataAccessFunction(IGM, Target);
    }
  };
  
  /// Emit a value witness table for a fixed-layout generic type, or a null
  /// placeholder if the value witness table is dependent on generic parameters.
  /// Returns nullptr if the value witness table is dependent.
  static llvm::Constant *
  getValueWitnessTableForGenericValueType(IRGenModule &IGM,
                                          NominalTypeDecl *decl,
                                          bool &dependent) {
    CanType unboundType
      = decl->getDeclaredType()->getCanonicalType();
    
    dependent = hasDependentValueWitnessTable(IGM, unboundType);    
    if (dependent)
      return llvm::ConstantPointerNull::get(IGM.Int8PtrTy);
    else
      return emitValueWitnessTable(IGM, unboundType);
  }
  
  /// A builder for metadata templates.
  class GenericStructMetadataBuilder :
    public GenericMetadataBuilderBase<GenericStructMetadataBuilder,
                      StructMetadataBuilderBase<GenericStructMetadataBuilder>> {

    typedef GenericMetadataBuilderBase super;
                        
  public:
    GenericStructMetadataBuilder(IRGenModule &IGM, StructDecl *theStruct,
                                 ConstantStructBuilder &B)
      : super(IGM, theStruct, B) {}

    llvm::Value *emitAllocateMetadata(IRGenFunction &IGF,
                                      llvm::Value *metadataPattern,
                                      llvm::Value *arguments) {
      return IGF.Builder.CreateCall(IGM.getAllocateGenericValueMetadataFn(),
                                    {metadataPattern, arguments});
    }

    void flagUnfilledFieldOffset() {
      // We just assume this might happen.
    }
    
    void addValueWitnessTable() {
      B.add(getValueWitnessTableForGenericValueType(IGM, Target,
                                                    HasDependentVWT));
    }
                        
    void addDependentValueWitnessTablePattern() {
      emitDependentValueWitnessTablePattern(IGM, B,
                        Target->getDeclaredType()->getCanonicalType());
    }
                        
    void emitInitializeMetadata(IRGenFunction &IGF,
                                llvm::Value *metadata,
                                llvm::Value *vwtable) {
      // Nominal types are always preserved through SIL lowering.
      auto structTy = Target->getDeclaredTypeInContext()->getCanonicalType();
      IGM.getTypeInfoForUnlowered(structTy)
        .initializeMetadata(IGF, metadata, vwtable,
                            IGF.IGM.getLoweredType(structTy));
    }
  };
} // end anonymous namespace

/// Emit the type metadata or metadata template for a struct.
void irgen::emitStructMetadata(IRGenModule &IGM, StructDecl *structDecl) {
  // TODO: structs nested within generic types
  ConstantInitBuilder initBuilder(IGM);
  auto init = initBuilder.beginStruct();
  init.setPacked(true);

  bool isPattern;
  bool canBeConstant;
  if (structDecl->isGenericContext()) {
    GenericStructMetadataBuilder builder(IGM, structDecl, init);
    builder.layout();
    isPattern = true;
    canBeConstant = false;

    maybeEmitNominalTypeMetadataAccessFunction(structDecl, builder);
  } else {
    StructMetadataBuilder builder(IGM, structDecl, init);
    builder.layout();
    isPattern = false;
    canBeConstant = builder.canBeConstant();

    maybeEmitNominalTypeMetadataAccessFunction(structDecl, builder);
  }

  CanType declaredType = structDecl->getDeclaredType()->getCanonicalType();

  // For now, all type metadata is directly stored.
  bool isIndirect = false;

  IGM.defineTypeMetadata(declaredType, isIndirect, isPattern,
                         canBeConstant, init.finishAndCreateFuture());
}

// Enums

namespace {

template<class Impl>
class EnumMetadataBuilderBase : public EnumMetadataVisitor<Impl> {
  using super = EnumMetadataVisitor<Impl>;

protected:
  ConstantStructBuilder &B;
  using super::IGM;
  using super::Target;

public:
  EnumMetadataBuilderBase(IRGenModule &IGM, EnumDecl *theEnum,
                          ConstantStructBuilder &B)
  : super(IGM, theEnum), B(B) {
  }
  
  void addMetadataFlags() {
    auto kind = Target->classifyAsOptionalType()
                  ? MetadataKind::Optional
                  : MetadataKind::Enum;
    B.addInt(IGM.MetadataKindTy, unsigned(kind));
  }

  void addNominalTypeDescriptor() {
    auto descriptor = EnumNominalTypeDescriptorBuilder(IGM, Target).emit();
    B.add(descriptor);
  }

  void addGenericArgument(CanType type) {
    B.addNullPointer(IGM.TypeMetadataPtrTy);
  }
  
  void addGenericWitnessTable(CanType type, ProtocolConformanceRef conf) {
    B.addNullPointer(IGM.WitnessTablePtrTy);
  }
};
  
class EnumMetadataBuilder
  : public EnumMetadataBuilderBase<EnumMetadataBuilder> {
  bool HasUnfilledPayloadSize = false;

public:
  EnumMetadataBuilder(IRGenModule &IGM, EnumDecl *theEnum,
                      ConstantStructBuilder &B)
    : EnumMetadataBuilderBase(IGM, theEnum, B) {}
  
  void addValueWitnessTable() {
    auto type = Target->getDeclaredType()->getCanonicalType();
    B.add(emitValueWitnessTable(IGM, type));
  }
  
  void addPayloadSize() {
    auto enumTy = Target->getDeclaredTypeInContext()->getCanonicalType();
    auto &enumTI = IGM.getTypeInfoForUnlowered(enumTy);
    if (!enumTI.isFixedSize(ResilienceExpansion::Maximal)) {
      B.addInt(IGM.IntPtrTy, 0);
      HasUnfilledPayloadSize = true;
      return;
    }

    assert(!enumTI.isFixedSize(ResilienceExpansion::Minimal) &&
           "non-generic, non-resilient enums don't need payload size in metadata");
    auto &strategy = getEnumImplStrategy(IGM, enumTy);
    B.addInt(IGM.IntPtrTy, strategy.getPayloadSizeForMetadata());
  }

  bool canBeConstant() {
    return !HasUnfilledPayloadSize;
  }

  void createMetadataAccessFunction() {
    createInPlaceValueTypeMetadataAccessFunction(IGM, Target);
  }
};
  
class GenericEnumMetadataBuilder
  : public GenericMetadataBuilderBase<GenericEnumMetadataBuilder,
                        EnumMetadataBuilderBase<GenericEnumMetadataBuilder>>
{
public:
  GenericEnumMetadataBuilder(IRGenModule &IGM, EnumDecl *theEnum,
                             ConstantStructBuilder &B)
    : GenericMetadataBuilderBase(IGM, theEnum, B) {}

  llvm::Value *emitAllocateMetadata(IRGenFunction &IGF,
                                    llvm::Value *metadataPattern,
                                    llvm::Value *arguments) {
    return IGF.Builder.CreateCall(IGM.getAllocateGenericValueMetadataFn(),
                                  {metadataPattern, arguments});
  }

  void addValueWitnessTable() {
    B.add(getValueWitnessTableForGenericValueType(IGM, Target,
                                                  HasDependentVWT));
  }
  
  void addDependentValueWitnessTablePattern() {
    emitDependentValueWitnessTablePattern(IGM, B,
                        Target->getDeclaredType()->getCanonicalType());
  }
  
  void addPayloadSize() {
    // In all cases where a payload size is demanded in the metadata, it's
    // runtime-dependent, so fill in a zero here.
    auto enumTy = Target->getDeclaredTypeInContext()->getCanonicalType();
    auto &enumTI = IGM.getTypeInfoForUnlowered(enumTy);
    (void) enumTI;
    assert(!enumTI.isFixedSize(ResilienceExpansion::Minimal) &&
           "non-generic, non-resilient enums don't need payload size in metadata");
    B.addInt(IGM.IntPtrTy, 0);
  }
  
  void emitInitializeMetadata(IRGenFunction &IGF,
                              llvm::Value *metadata,
                              llvm::Value *vwtable) {
    // Nominal types are always preserved through SIL lowering.
    auto enumTy = Target->getDeclaredTypeInContext()->getCanonicalType();
    IGM.getTypeInfoForUnlowered(enumTy)
      .initializeMetadata(IGF, metadata, vwtable,
                          IGF.IGM.getLoweredType(enumTy));
  }
};
  
} // end anonymous namespace

void irgen::emitEnumMetadata(IRGenModule &IGM, EnumDecl *theEnum) {
  // TODO: enums nested inside generic types
  ConstantInitBuilder initBuilder(IGM);
  auto init = initBuilder.beginStruct();
  init.setPacked(true);
  
  bool isPattern;
  bool canBeConstant;
  if (theEnum->isGenericContext()) {
    GenericEnumMetadataBuilder builder(IGM, theEnum, init);
    builder.layout();
    isPattern = true;
    canBeConstant = false;

    maybeEmitNominalTypeMetadataAccessFunction(theEnum, builder);
  } else {
    EnumMetadataBuilder builder(IGM, theEnum, init);
    builder.layout();
    isPattern = false;
    canBeConstant = builder.canBeConstant();

    maybeEmitNominalTypeMetadataAccessFunction(theEnum, builder);
  }

  CanType declaredType = theEnum->getDeclaredType()->getCanonicalType();

  // For now, all type metadata is directly stored.
  bool isIndirect = false;
  
  IGM.defineTypeMetadata(declaredType, isIndirect, isPattern,
                         canBeConstant, init.finishAndCreateFuture());
}

llvm::Value *IRGenFunction::emitObjCSelectorRefLoad(StringRef selector) {
  llvm::Constant *loadSelRef = IGM.getAddrOfObjCSelectorRef(selector);
  llvm::Value *loadSel =
    Builder.CreateLoad(Address(loadSelRef, IGM.getPointerAlignment()));

  // When generating JIT'd code, we need to call sel_registerName() to force
  // the runtime to unique the selector. For non-JIT'd code, the linker will
  // do it for us.
  if (IGM.IRGen.Opts.UseJIT) {
    loadSel = Builder.CreateCall(IGM.getObjCSelRegisterNameFn(), loadSel);
  }

  return loadSel;
}

//===----------------------------------------------------------------------===//
// Foreign types
//===----------------------------------------------------------------------===//

namespace {
  /// A CRTP layout class for foreign class metadata.
  template <class Impl>
  class ForeignClassMetadataVisitor
         : public NominalMetadataVisitor<Impl> {
    using super = NominalMetadataVisitor<Impl>;
  protected:
    ClassDecl *Target;
    using super::asImpl;
  public:
    ForeignClassMetadataVisitor(IRGenModule &IGM, ClassDecl *target)
      : super(IGM), Target(target) {}

    void layout() {
      super::layout();
      asImpl().addSuperClass();
      asImpl().addReservedWord();
      asImpl().addReservedWord();
      asImpl().addReservedWord();
    }

    bool requiresInitializationFunction() {
      // TODO: superclasses?
      return false;
    }
           
    CanType getTargetType() const {
      return Target->getDeclaredType()->getCanonicalType();
    }
  };
  
  /// An adapter that turns a metadata layout class into a foreign metadata
  /// layout class. Foreign metadata has an additional header that
  template<typename Impl, typename Base>
  class ForeignMetadataBuilderBase : public Base {
    typedef Base super;
    
  protected:
    using super::IGM;
    using super::asImpl;
    using super::B;

    template <class... T>
    ForeignMetadataBuilderBase(T &&...args) : super(std::forward<T>(args)...) {}

    Size AddressPoint = Size::invalid();

  public:
    void layout() {
      if (asImpl().requiresInitializationFunction())
        asImpl().addInitializationFunction();
      asImpl().addForeignName();
      asImpl().addUniquePointer();
      asImpl().addForeignFlags();
      super::layout();
    }
    
    void addForeignFlags() {
      int64_t flags = 0;
      if (asImpl().requiresInitializationFunction()) flags |= 1;
      B.addInt(IGM.IntPtrTy, flags);
    }

    void addForeignName() {
      CanType targetType = asImpl().getTargetType();
      B.add(getMangledTypeName(IGM, targetType));
    }

    void addUniquePointer() {
      B.addNullPointer(IGM.TypeMetadataPtrTy);
    }

    void addInitializationFunction() {
      auto type = cast<NominalType>(asImpl().getTargetType());

      auto fnTy = llvm::FunctionType::get(IGM.VoidTy, {IGM.TypeMetadataPtrTy},
                                          /*variadic*/ false);
      llvm::Function *fn = llvm::Function::Create(fnTy,
                                           llvm::GlobalValue::PrivateLinkage,
                                           Twine("initialize_metadata_")
                                             + type->getDecl()->getName().str(),
                                           &IGM.Module);
      fn->setAttributes(IGM.constructInitialAttributes());
      
      // Set up the function.
      IRGenFunction IGF(IGM, fn);
      if (IGM.DebugInfo)
        IGM.DebugInfo->emitArtificialFunction(IGF, fn);

      // Emit the initialization.
      llvm::Value *metadata = IGF.collectParameters().claimNext();
      asImpl().emitInitialization(IGF, metadata);

      IGF.Builder.CreateRetVoid();

      B.add(fn);
    }

    void noteAddressPoint() {
      AddressPoint = B.getNextOffsetFromGlobal();
    }

    Size getOffsetOfAddressPoint() const { return AddressPoint; }
  };

  class ForeignClassMetadataBuilder;
  class ForeignClassMetadataBuilderBase :
      public ForeignClassMetadataVisitor<ForeignClassMetadataBuilder> {
  protected:
    ConstantStructBuilder &B;

    ForeignClassMetadataBuilderBase(IRGenModule &IGM, ClassDecl *target,
                                    ConstantStructBuilder &B)
      : ForeignClassMetadataVisitor(IGM, target), B(B) {}
  };

  /// A builder for ForeignClassMetadata.
  class ForeignClassMetadataBuilder :
      public ForeignMetadataBuilderBase<ForeignClassMetadataBuilder,
                                        ForeignClassMetadataBuilderBase> {
  public:
    ForeignClassMetadataBuilder(IRGenModule &IGM, ClassDecl *target,
                                ConstantStructBuilder &B)
      : ForeignMetadataBuilderBase(IGM, target, B) {}

    void emitInitialization(IRGenFunction &IGF, llvm::Value *metadata) {
      // TODO: superclasses?
      llvm_unreachable("no supported forms of initialization");
    }

    // Visitor methods.

    void addValueWitnessTable() {
      // Without Objective-C interop, foreign classes must still use
      // Swift native reference counting.
      auto type = (IGM.ObjCInterop
                   ? IGM.Context.TheUnknownObjectType
                   : IGM.Context.TheNativeObjectType);
      auto wtable = IGM.getAddrOfValueWitnessTable(type);
      B.add(wtable);
    }

    void addMetadataFlags() {
      B.addInt(IGM.MetadataKindTy, (unsigned) MetadataKind::ForeignClass);
    }

    void addSuperClass() {
      // TODO: superclasses
      B.addNullPointer(IGM.TypeMetadataPtrTy);
    }

    void addReservedWord() {
      B.addNullPointer(IGM.Int8PtrTy);
    }
  };
  
  /// A builder for ForeignStructMetadata.
  class ForeignStructMetadataBuilder :
    public ForeignMetadataBuilderBase<ForeignStructMetadataBuilder,
                      StructMetadataBuilderBase<ForeignStructMetadataBuilder>>
  {
  public:
    ForeignStructMetadataBuilder(IRGenModule &IGM, StructDecl *target,
                                 ConstantStructBuilder &builder)
        : ForeignMetadataBuilderBase(IGM, target, builder) {}
    
    CanType getTargetType() const {
      return Target->getDeclaredType()->getCanonicalType();
    }

    bool requiresInitializationFunction() const {
      return false;
    }
    void emitInitialization(IRGenFunction &IGF, llvm::Value *metadata) {}

    void addValueWitnessTable() {
      auto type = this->Target->getDeclaredType()->getCanonicalType();
      B.add(emitValueWitnessTable(IGM, type));
    }

    void flagUnfilledFieldOffset() {
      llvm_unreachable("foreign type with non-fixed layout?");
    }
  };
  
  /// A builder for ForeignEnumMetadata.
  class ForeignEnumMetadataBuilder :
    public ForeignMetadataBuilderBase<ForeignEnumMetadataBuilder,
                      EnumMetadataBuilderBase<ForeignEnumMetadataBuilder>>
  {
  public:
    ForeignEnumMetadataBuilder(IRGenModule &IGM, EnumDecl *target,
                               ConstantStructBuilder &builder)
      : ForeignMetadataBuilderBase(IGM, target, builder) {}
    
    CanType getTargetType() const {
      return Target->getDeclaredType()->getCanonicalType();
    }

    bool requiresInitializationFunction() const {
      return false;
    }
    void emitInitialization(IRGenFunction &IGF, llvm::Value *metadata) {}

    void addValueWitnessTable() {
      auto type = this->Target->getDeclaredType()->getCanonicalType();
      B.add(emitValueWitnessTable(IGM, type));
    }
    
    void addPayloadSize() const {
      llvm_unreachable("nongeneric enums shouldn't need payload size in metadata");
    }
  };
} // end anonymous namespace

llvm::Constant *
IRGenModule::getAddrOfForeignTypeMetadataCandidate(CanType type) {
  // What we save in GlobalVars is actually the offsetted value.
  auto entity = LinkEntity::forForeignTypeMetadataCandidate(type);
  if (auto entry = GlobalVars[entity])
    return entry;

  // Create a temporary base for relative references.
  ConstantInitBuilder builder(*this);
  auto init = builder.beginStruct();
  init.setPacked(true);
  
  // Compute the constant initializer and the offset of the type
  // metadata candidate within it.
  Size addressPoint;
  if (auto classType = dyn_cast<ClassType>(type)) {
    assert(!classType.getParent());
    auto classDecl = classType->getDecl();
    assert(classDecl->isForeign());

    ForeignClassMetadataBuilder builder(*this, classDecl, init);
    builder.layout();
    addressPoint = builder.getOffsetOfAddressPoint();
  } else if (auto structType = dyn_cast<StructType>(type)) {
    auto structDecl = structType->getDecl();
    assert(isa<ClangModuleUnit>(structDecl->getModuleScopeContext()));
    
    ForeignStructMetadataBuilder builder(*this, structDecl, init);
    builder.layout();
    addressPoint = builder.getOffsetOfAddressPoint();
  } else if (auto enumType = dyn_cast<EnumType>(type)) {
    auto enumDecl = enumType->getDecl();
    assert(enumDecl->hasClangNode());
    
    ForeignEnumMetadataBuilder builder(*this, enumDecl, init);
    builder.layout();
    addressPoint = builder.getOffsetOfAddressPoint();
  } else {
    llvm_unreachable("foreign metadata for unexpected type?!");
  }

  auto definition = init.finishAndCreateFuture();
  
  // Create the global variable.
  LinkInfo link = LinkInfo::get(*this, entity, ForDefinition);
  auto var =
      createVariable(*this, link, definition.getType(), getPointerAlignment());
  definition.installInGlobal(var);
  
  // Apply the offset.
  llvm::Constant *result = var;
  result = llvm::ConstantExpr::getBitCast(result, Int8PtrTy);
  result = llvm::ConstantExpr::getInBoundsGetElementPtr(
      Int8Ty, result, getSize(addressPoint));
  result = llvm::ConstantExpr::getBitCast(result, TypeMetadataPtrTy);

  // Only remember the offset.
  GlobalVars[entity] = result;

  if (NominalTypeDecl *Nominal = type->getAnyNominal()) {
    addLazyConformances(Nominal);
  }

  return result;
}

// Protocols

/// Get the runtime identifier for a special protocol, if any.
SpecialProtocol irgen::getSpecialProtocolID(ProtocolDecl *P) {
  auto known = P->getKnownProtocolKind();
  if (!known)
    return SpecialProtocol::None;
  switch (*known) {
  case KnownProtocolKind::Error:
    return SpecialProtocol::Error;
    
  // The other known protocols aren't special at runtime.
  case KnownProtocolKind::Sequence:
  case KnownProtocolKind::IteratorProtocol:
  case KnownProtocolKind::RawRepresentable:
  case KnownProtocolKind::Equatable:
  case KnownProtocolKind::Hashable:
  case KnownProtocolKind::Comparable:
  case KnownProtocolKind::ObjectiveCBridgeable:
  case KnownProtocolKind::DestructorSafeContainer:
  case KnownProtocolKind::SwiftNewtypeWrapper:
  case KnownProtocolKind::ExpressibleByArrayLiteral:
  case KnownProtocolKind::ExpressibleByBooleanLiteral:
  case KnownProtocolKind::ExpressibleByDictionaryLiteral:
  case KnownProtocolKind::ExpressibleByExtendedGraphemeClusterLiteral:
  case KnownProtocolKind::ExpressibleByFloatLiteral:
  case KnownProtocolKind::ExpressibleByIntegerLiteral:
  case KnownProtocolKind::ExpressibleByStringInterpolation:
  case KnownProtocolKind::ExpressibleByStringLiteral:
  case KnownProtocolKind::ExpressibleByNilLiteral:
  case KnownProtocolKind::ExpressibleByUnicodeScalarLiteral:
  case KnownProtocolKind::ExpressibleByColorLiteral:
  case KnownProtocolKind::ExpressibleByImageLiteral:
  case KnownProtocolKind::ExpressibleByFileReferenceLiteral:
  case KnownProtocolKind::ExpressibleByBuiltinBooleanLiteral:
  case KnownProtocolKind::ExpressibleByBuiltinUTF16ExtendedGraphemeClusterLiteral:
  case KnownProtocolKind::ExpressibleByBuiltinExtendedGraphemeClusterLiteral:
  case KnownProtocolKind::ExpressibleByBuiltinFloatLiteral:
  case KnownProtocolKind::ExpressibleByBuiltinIntegerLiteral:
  case KnownProtocolKind::ExpressibleByBuiltinStringLiteral:
  case KnownProtocolKind::ExpressibleByBuiltinUTF16StringLiteral:
  case KnownProtocolKind::ExpressibleByBuiltinUnicodeScalarLiteral:
  case KnownProtocolKind::OptionSet:
  case KnownProtocolKind::BridgedNSError:
  case KnownProtocolKind::BridgedStoredNSError:
  case KnownProtocolKind::CFObject:
  case KnownProtocolKind::ErrorCodeProtocol:
  case KnownProtocolKind::ExpressibleByBuiltinConstStringLiteral:
  case KnownProtocolKind::ExpressibleByBuiltinConstUTF16StringLiteral:
  case KnownProtocolKind::CodingKey:
  case KnownProtocolKind::Encodable:
  case KnownProtocolKind::Decodable:
    return SpecialProtocol::None;
  }

  llvm_unreachable("Not a valid KnownProtocolKind.");
}

namespace {
  class ProtocolDescriptorBuilder {
    IRGenModule &IGM;
    ConstantStructBuilder &B;
    ProtocolDecl *Protocol;
    SILDefaultWitnessTable *DefaultWitnesses;

  public:
    ProtocolDescriptorBuilder(IRGenModule &IGM, ProtocolDecl *protocol,
                              ConstantStructBuilder &B,
                              SILDefaultWitnessTable *defaultWitnesses)
      : IGM(IGM), B(B), Protocol(protocol),
        DefaultWitnesses(defaultWitnesses) {}

    void layout() {
      addObjCCompatibilityIsa();
      addName();
      addInherited();
      addObjCCompatibilityTables();
      addSize();
      addFlags();
      addRequirements();

      B.suggestType(IGM.ProtocolDescriptorStructTy);
    }

    void addObjCCompatibilityIsa() {
      // The ObjC runtime will drop a reference to its magic Protocol class
      // here.
      B.addNullPointer(IGM.Int8PtrTy);
    }
    
    void addName() {
      // Include the _Tt prefix. Since Swift protocol descriptors are laid
      // out to look like ObjC Protocol* objects, the name has to clearly be
      // a Swift mangled name.

      IRGenMangler mangler;
      std::string Name =
        mangler.mangleForProtocolDescriptor(Protocol->getDeclaredType());

      auto global = IGM.getAddrOfGlobalString(Name);
      B.add(global);
    }
    
    void addInherited() {
      // If there are no inherited protocols, produce null.
      auto inherited = Protocol->getInheritedProtocols();
      if (inherited.empty()) {
        B.addNullPointer(IGM.Int8PtrTy);
        return;
      }
      
      // Otherwise, collect references to all of the inherited protocol
      // descriptors.
      SmallVector<llvm::Constant*, 4> inheritedDescriptors;
      inheritedDescriptors.push_back(IGM.getSize(Size(inherited.size())));
      
      for (ProtocolDecl *p : inherited) {
        auto descriptor = IGM.getAddrOfProtocolDescriptor(p);
        inheritedDescriptors.push_back(descriptor);
      }
      
      auto inheritedInit = llvm::ConstantStruct::getAnon(inheritedDescriptors);
      auto inheritedVar = new llvm::GlobalVariable(IGM.Module,
                                           inheritedInit->getType(),
                                           /*isConstant*/ true,
                                           llvm::GlobalValue::PrivateLinkage,
                                           inheritedInit);
      
      B.addBitCast(inheritedVar, IGM.Int8PtrTy);
    }
    
    void addObjCCompatibilityTables() {
      // Required instance methods
      B.addNullPointer(IGM.Int8PtrTy);
      // Required class methods
      B.addNullPointer(IGM.Int8PtrTy);
      // Optional instance methods
      B.addNullPointer(IGM.Int8PtrTy);
      // Optional class methods
      B.addNullPointer(IGM.Int8PtrTy);
      // Properties
      B.addNullPointer(IGM.Int8PtrTy);
    }
    
    void addSize() {
      // The number of fields so far in words, plus 4 bytes for size and
      // 4 bytes for flags.
      B.addInt32(B.getNextOffsetFromGlobal().getValue() + 4 + 4);
    }
    
    void addFlags() {
      auto flags = ProtocolDescriptorFlags()
        .withSwift(true)
        .withClassConstraint(Protocol->requiresClass()
                               ? ProtocolClassConstraint::Class
                               : ProtocolClassConstraint::Any)
        .withDispatchStrategy(
                Lowering::TypeConverter::getProtocolDispatchStrategy(Protocol))
        .withSpecialProtocol(getSpecialProtocolID(Protocol));

      if (DefaultWitnesses)
        flags = flags.withResilient(true);

      B.addInt32(flags.getIntValue());
    }

    void addRequirements() {
      auto &pi = IGM.getProtocolInfo(Protocol);

      B.addInt16(DefaultWitnesses
                   ? DefaultWitnesses->getMinimumWitnessTableSize()
                   : pi.getNumWitnesses());
      B.addInt16(pi.getNumWitnesses());

      // If there are no entries, just add a null reference and return.
      if (pi.getNumWitnesses() == 0) {
        B.addInt(IGM.RelativeAddressTy, 0);
        return;
      }

#ifndef NDEBUG
      unsigned numDefaultWitnesses = 0;
#endif

      ConstantInitBuilder reqtBuilder(IGM);
      auto reqtsArray = reqtBuilder.beginArray(IGM.ProtocolRequirementStructTy);
      for (auto &entry : pi.getWitnessEntries()) {
        auto reqt = reqtsArray.beginStruct(IGM.ProtocolRequirementStructTy);

        auto info = getRequirementInfo(entry);

        // Flags.
        reqt.addInt32(info.Flags.getIntValue());

        // Default implementation.
        reqt.addRelativeAddressOrNull(info.DefaultImpl);
#ifndef NDEBUG
        assert((info.DefaultImpl || numDefaultWitnesses == 0) &&
               "adding mandatory witness after defaulted witness");
        if (info.DefaultImpl) numDefaultWitnesses++;
#endif

        reqt.finishAndAddTo(reqtsArray);
      }

#ifndef NDEBUG
      if (DefaultWitnesses) {
        assert(numDefaultWitnesses
                 == DefaultWitnesses->getDefaultWitnessTableSize() &&
               "didn't use all the default witnesses!");
      } else {
        assert(numDefaultWitnesses == 0);
      }
#endif

      auto global =
        reqtsArray.finishAndCreateGlobal("", Alignment(4), /*constant*/ true,
                                         llvm::GlobalVariable::InternalLinkage);
      global->setUnnamedAddr(llvm::GlobalVariable::UnnamedAddr::Global);
      B.addRelativeOffset(IGM.Int32Ty, global);
    }

    struct RequirementInfo {
      ProtocolRequirementFlags Flags;
      llvm::Constant *DefaultImpl;
    };

    /// Build the information which will go into a ProtocolRequirement entry.
    RequirementInfo getRequirementInfo(const WitnessTableEntry &entry) {
      using Flags = ProtocolRequirementFlags;
      if (entry.isBase()) {
        assert(entry.isOutOfLineBase());
        auto flags = Flags(Flags::Kind::BaseProtocol);
        return { flags, nullptr };
      }

      if (entry.isAssociatedType()) {
        auto flags = Flags(Flags::Kind::AssociatedTypeAccessFunction);
        return { flags, nullptr };
      }

      if (entry.isAssociatedConformance()) {
        auto flags = Flags(Flags::Kind::AssociatedConformanceAccessFunction);
        return { flags, nullptr };
      }

      assert(entry.isFunction());
      auto func = entry.getFunction();

      // Classify the function.
      auto flags = getMethodDescriptorFlags<Flags>(func);

      // Look for a default witness.
      llvm::Constant *defaultImpl = findDefaultWitness(func);

      return { flags, defaultImpl };
    }

    llvm::Constant *findDefaultWitness(AbstractFunctionDecl *func) {
      if (!DefaultWitnesses) return nullptr;

      for (auto &entry : DefaultWitnesses->getResilientDefaultEntries()) {
        if (entry.getRequirement().getDecl() != func)
          continue;
        return IGM.getAddrOfSILFunction(entry.getWitness(), NotForDefinition);
      }

      return nullptr;
    }
  };
} // end anonymous namespace

/// Emit global structures associated with the given protocol. This comprises
/// the protocol descriptor, and for ObjC interop, references to the descriptor
/// that the ObjC runtime uses for uniquing.
void IRGenModule::emitProtocolDecl(ProtocolDecl *protocol) {
  // Emit remote reflection metadata for the protocol.
  emitFieldMetadataRecord(protocol);

  // If the protocol is Objective-C-compatible, go through the path that
  // produces an ObjC-compatible protocol_t.
  if (protocol->isObjC()) {
    // In JIT mode, we need to create protocol descriptors using the ObjC
    // runtime in JITted code.
    if (IRGen.Opts.UseJIT)
      return;
    
    // Native ObjC protocols are emitted on-demand in ObjC and uniqued by the
    // runtime; we don't need to try to emit a unique descriptor symbol for them.
    if (protocol->hasClangNode())
      return;
    
    getObjCProtocolGlobalVars(protocol);
    return;
  }

  SILDefaultWitnessTable *defaultWitnesses = nullptr;
  if (!protocol->hasFixedLayout())
    defaultWitnesses = getSILModule().lookUpDefaultWitnessTable(protocol);

  ConstantInitBuilder initBuilder(*this);
  auto init = initBuilder.beginStruct();
  ProtocolDescriptorBuilder builder(*this, protocol, init, defaultWitnesses);
  builder.layout();

  auto var = cast<llvm::GlobalVariable>(
          getAddrOfProtocolDescriptor(protocol, init.finishAndCreateFuture()));
  var->setConstant(true);
}

/// \brief Load a reference to the protocol descriptor for the given protocol.
///
/// For Swift protocols, this is a constant reference to the protocol descriptor
/// symbol.
/// For ObjC protocols, descriptors are uniqued at runtime by the ObjC runtime.
/// We need to load the unique reference from a global variable fixed up at
/// startup.
llvm::Value *irgen::emitProtocolDescriptorRef(IRGenFunction &IGF,
                                              ProtocolDecl *protocol) {
  if (!protocol->isObjC())
    return IGF.IGM.getAddrOfProtocolDescriptor(protocol);
  
  auto refVar = IGF.IGM.getAddrOfObjCProtocolRef(protocol, NotForDefinition);
  llvm::Value *val
    = IGF.Builder.CreateLoad(refVar, IGF.IGM.getPointerAlignment());
  val = IGF.Builder.CreateBitCast(val,
                          IGF.IGM.ProtocolDescriptorStructTy->getPointerTo());
  return val;
}

//===----------------------------------------------------------------------===//
// Other metadata.
//===----------------------------------------------------------------------===//

llvm::Value *irgen::emitMetatypeInstanceType(IRGenFunction &IGF,
                                             llvm::Value *metatypeMetadata) {
  // The instance type field of MetatypeMetadata is immediately after
  // the isa field.
  return emitInvariantLoadFromMetadataAtIndex(IGF, metatypeMetadata, 1,
                                     IGF.IGM.TypeMetadataPtrTy);
}
