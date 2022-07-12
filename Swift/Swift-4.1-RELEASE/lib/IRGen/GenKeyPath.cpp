//===--- GenKeyPath.cpp - IRGen support for key path objects --------------===//
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
//  This file contains code for emitting key path patterns, which can be used
//  by the standard library to instantiate key path objects.
//
//===----------------------------------------------------------------------===//

#include "Callee.h"
#include "ConstantBuilder.h"
#include "Explosion.h"
#include "GenClass.h"
#include "GenDecl.h"
#include "GenMeta.h"
#include "GenProto.h"
#include "GenStruct.h"
#include "GenericRequirement.h"
#include "IRGenDebugInfo.h"
#include "IRGenFunction.h"
#include "IRGenModule.h"
#include "MetadataLayout.h"
#include "ProtocolInfo.h"
#include "StructLayout.h"
#include "TypeInfo.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/Module.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILLocation.h"
#include "swift/SIL/TypeLowering.h"
#include "swift/ABI/KeyPath.h"
#include "swift/ABI/HeapObject.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/DiagnosticsIRGen.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Types.h"
#include "swift/IRGen/Linking.h"

using namespace swift;
using namespace irgen;

enum KeyPathAccessor {
  Getter,
  Setter,
  Equals,
  Hash,
};

static void
bindPolymorphicArgumentsFromComponentIndices(IRGenFunction &IGF,
                                     const KeyPathPatternComponent &component,
                                     GenericEnvironment *genericEnv,
                                     ArrayRef<GenericRequirement> requirements,
                                     llvm::Value *args,
                                     llvm::Value *size) {
  if (!genericEnv)
    return;
  
  // The generic environment is marshaled into the end of the component
  // argument area inside the instance. Bind the generic information out of
  // the buffer.
  if (!component.getComputedPropertyIndices().empty()) {
    auto genericArgsSize = llvm::ConstantInt::get(IGF.IGM.SizeTy,
      requirements.size() * IGF.IGM.getPointerSize().getValue());

    auto genericArgsOffset = IGF.Builder.CreateSub(size, genericArgsSize);
    args = IGF.Builder.CreateInBoundsGEP(args, genericArgsOffset);
  }
  bindFromGenericRequirementsBuffer(IGF, requirements,
    Address(args, IGF.IGM.getPointerAlignment()),
    [&](CanType t) {
      return genericEnv->mapTypeIntoContext(t)->getCanonicalType();
    });
}

static llvm::Function *
getAccessorForComputedComponent(IRGenModule &IGM,
                                const KeyPathPatternComponent &component,
                                KeyPathAccessor whichAccessor,
                                GenericEnvironment *genericEnv,
                                ArrayRef<GenericRequirement> requirements) {
  SILFunction *accessor;
  switch (whichAccessor) {
  case Getter:
    accessor = component.getComputedPropertyGetter();
    break;
  case Setter:
    accessor = component.getComputedPropertySetter();
    break;
  case Equals:
    accessor = component.getComputedPropertyIndexEquals();
    break;
  case Hash:
    accessor = component.getComputedPropertyIndexHash();
    break;
  }
  
  auto accessorFn = IGM.getAddrOfSILFunction(accessor, NotForDefinition);
  
  // If the accessor is not generic, we can use it as is.
  if (requirements.empty()) {
    return accessorFn;
  }

  auto accessorFnTy = cast<llvm::FunctionType>(
    accessorFn->getType()->getPointerElementType());;
  
  // Otherwise, we need a thunk to unmarshal the generic environment from the
  // argument area. It'd be nice to have a good way to represent this
  // directly in SIL, of course...
  const char *thunkName;
  unsigned numArgsToForward;

  switch (whichAccessor) {
  case Getter:
    thunkName = "keypath_get";
    numArgsToForward = 2;
    break;
  case Setter:
    thunkName = "keypath_set";
    numArgsToForward = 2;
    break;
  case Equals:
    thunkName = "keypath_equals";
    numArgsToForward = 2;
    break;
  case Hash:
    thunkName = "keypath_hash";
    numArgsToForward = 1;
    break;
  }

  SmallVector<llvm::Type *, 4> thunkParams;
  for (unsigned i = 0; i < numArgsToForward; ++i)
    thunkParams.push_back(accessorFnTy->getParamType(i));
  
  switch (whichAccessor) {
  case Getter:
  case Setter:
    thunkParams.push_back(IGM.Int8PtrTy);
    break;
  case Equals:
  case Hash:
    break;
  }
  thunkParams.push_back(IGM.SizeTy);

  auto thunkType = llvm::FunctionType::get(accessorFnTy->getReturnType(),
                                           thunkParams,
                                           /*vararg*/ false);
  
  auto accessorThunk = llvm::Function::Create(thunkType,
    llvm::GlobalValue::PrivateLinkage, thunkName, IGM.getModule());
  accessorThunk->setAttributes(IGM.constructInitialAttributes());
  accessorThunk->setCallingConv(IGM.SwiftCC);

  switch (whichAccessor) {
  case Getter:
    // Original accessor's args should be @in or @out, meaning they won't be
    // captured or aliased.
    accessorThunk->addAttribute(1, llvm::Attribute::NoCapture);
    accessorThunk->addAttribute(1, llvm::Attribute::NoAlias);
    accessorThunk->addAttribute(2, llvm::Attribute::NoCapture);
    accessorThunk->addAttribute(2, llvm::Attribute::NoAlias);
    // Output is sret.
    accessorThunk->addAttribute(1, llvm::Attribute::StructRet);
    break;
  case Setter:
    // Original accessor's args should be @in or @out, meaning they won't be
    // captured or aliased.
    accessorThunk->addAttribute(1, llvm::Attribute::NoCapture);
    accessorThunk->addAttribute(1, llvm::Attribute::NoAlias);
    accessorThunk->addAttribute(2, llvm::Attribute::NoCapture);
    accessorThunk->addAttribute(2, llvm::Attribute::NoAlias);
    break;
  case Equals:
  case Hash:
    break;
  }

  {
    IRGenFunction IGF(IGM, accessorThunk);
    if (IGM.DebugInfo)
      IGM.DebugInfo->emitArtificialFunction(IGF, accessorThunk);
      
    auto params = IGF.collectParameters();
    Explosion forwardedArgs;
    forwardedArgs.add(params.claim(numArgsToForward));
    
    llvm::Value *componentArgsBuf;
    switch (whichAccessor) {
    case Getter:
    case Setter:
      // The component arguments are passed alongside the base being projected.
      componentArgsBuf = params.claimNext();
      // Pass the argument pointer down to the underlying function.
      if (!component.getComputedPropertyIndices().empty()) {
        forwardedArgs.add(componentArgsBuf);
      }
      break;
    case Equals:
    case Hash:
      // We're operating directly on the component argument buffer.
      componentArgsBuf = forwardedArgs.getAll()[0];
      break;
    }
    auto componentArgsBufSize = params.claimNext();
    bindPolymorphicArgumentsFromComponentIndices(IGF, component,
                                                 genericEnv, requirements,
                                                 componentArgsBuf,
                                                 componentArgsBufSize);
    
    // Use the bound generic metadata to form a call to the original generic
    // accessor.
    WitnessMetadata ignoreWitnessMetadata;
    auto forwardingSubs = genericEnv->getGenericSignature()->getSubstitutionMap(
      genericEnv->getForwardingSubstitutions());
    emitPolymorphicArguments(IGF, accessor->getLoweredFunctionType(),
                             forwardingSubs,
                             &ignoreWitnessMetadata,
                             forwardedArgs);
    auto fnPtr = FunctionPointer::forDirect(IGM, accessorFn,
                                          accessor->getLoweredFunctionType());
    auto call = IGF.Builder.CreateCall(fnPtr, forwardedArgs.claimAll());
    
    if (call->getType()->isVoidTy())
      IGF.Builder.CreateRetVoid();
    else
      IGF.Builder.CreateRet(call);
  }
  
  return accessorThunk;
}

static llvm::Constant *
getLayoutFunctionForComputedComponent(IRGenModule &IGM,
                                    const KeyPathPatternComponent &component,
                                    GenericEnvironment *genericEnv,
                                    ArrayRef<GenericRequirement> requirements) {
  // Generate a function that returns the expected size and alignment necessary
  // to store captured generic context and subscript index arguments.
  auto retTy = llvm::StructType::get(IGM.getLLVMContext(),
                                     {IGM.SizeTy, IGM.SizeTy});
  auto fnTy = llvm::FunctionType::get(
    retTy, { IGM.Int8PtrTy }, /*vararg*/ false);
    
  auto layoutFn = llvm::Function::Create(fnTy,
    llvm::GlobalValue::PrivateLinkage, "keypath_get_arg_layout", IGM.getModule());
    
  {
    IRGenFunction IGF(IGM, layoutFn);
    if (IGM.DebugInfo)
      IGM.DebugInfo->emitArtificialFunction(IGF, layoutFn);
    // Unmarshal the generic environment from the argument buffer.
    auto parameters = IGF.collectParameters();
    auto args = parameters.claimNext();
    
    if (genericEnv) {
      bindFromGenericRequirementsBuffer(IGF, requirements,
        Address(args, IGF.IGM.getPointerAlignment()),
        [&](CanType t) {
          return genericEnv->mapTypeIntoContext(t)->getCanonicalType();
        });
    }
    
    // Run through the captured index types to determine the size and alignment
    // needed. Start with pointer alignment for the generic environment.
    llvm::Value *size = llvm::ConstantInt::get(IGM.SizeTy, 0);
    llvm::Value *alignMask = llvm::ConstantInt::get(IGM.SizeTy, 0);

    for (auto &index : component.getComputedPropertyIndices()) {
      auto ty = genericEnv
        ? genericEnv->mapTypeIntoContext(IGM.getSILModule(), index.LoweredType)
        : index.LoweredType;
      auto &ti = IGM.getTypeInfo(ty);
      auto indexSize = ti.getSize(IGF, ty);
      auto indexAlign = ti.getAlignmentMask(IGF, ty);
      
      auto notIndexAlign = IGF.Builder.CreateNot(indexAlign);
      
      size = IGF.Builder.CreateAdd(size, indexAlign);
      size = IGF.Builder.CreateAnd(size, notIndexAlign);
      size = IGF.Builder.CreateAdd(size, indexSize);
      
      alignMask = IGF.Builder.CreateOr(alignMask, indexAlign);
    }
    
    // If there's generic environment to capture, then it's stored as a block
    // of pointer-aligned words after the captured values.
    
    auto genericsSize = llvm::ConstantInt::get(IGM.SizeTy,
      IGM.getPointerSize().getValue() * requirements.size());
    auto genericsAlign = llvm::ConstantInt::get(IGM.SizeTy,
      IGM.getPointerAlignment().getValue() - 1);
    auto notGenericsAlign = llvm::ConstantExpr::getNot(genericsAlign);
    size = IGF.Builder.CreateAdd(size, genericsAlign);
    size = IGF.Builder.CreateAnd(size, notGenericsAlign);
    size = IGF.Builder.CreateAdd(size, genericsSize);
    alignMask = IGF.Builder.CreateOr(alignMask, genericsAlign);

    llvm::Value *retValue = IGF.Builder.CreateInsertValue(
      llvm::UndefValue::get(retTy), size, 0);
    retValue = IGF.Builder.CreateInsertValue(
      retValue, alignMask, 1);
      
    IGF.Builder.CreateRet(retValue);
  }
  
  return layoutFn;
}

static llvm::Constant *
getWitnessTableForComputedComponent(IRGenModule &IGM,
                                    const KeyPathPatternComponent &component,
                                    GenericEnvironment *genericEnv,
                                    ArrayRef<GenericRequirement> requirements) {
  // If the only thing we're capturing is generic environment, then we can
  // use a prefab witness table from the runtime.
  if (component.getComputedPropertyIndices().empty()) {
    if (auto existing =
          IGM.Module.getNamedGlobal("swift_keyPathGenericWitnessTable"))
      return existing;
    
    auto linkInfo = LinkInfo::get(IGM, "swift_keyPathGenericWitnessTable",
                                  SILLinkage::PublicExternal,
                                  NotForDefinition,
                                  /*weak imported*/ false);
    
    return createVariable(IGM, linkInfo,
                          IGM.Int8PtrTy, IGM.getPointerAlignment());
  }
  
  // Are the index values trivial?
  bool isTrivial = true;
  for (auto &component : component.getComputedPropertyIndices()) {
    auto ty = genericEnv
      ? genericEnv->mapTypeIntoContext(IGM.getSILModule(), component.LoweredType)
      : component.LoweredType;
    auto &ti = IGM.getTypeInfo(ty);
    isTrivial &= ti.isPOD(ResilienceExpansion::Minimal);
  }
  
  llvm::Constant *destroy;
  llvm::Constant *copy;
  if (isTrivial) {
    // We can use prefab witnesses for handling trivial copying and destruction.
    // A null destructor witness signals that the payload is trivial.
    destroy = llvm::ConstantPointerNull::get(IGM.Int8PtrTy);
    copy = IGM.getCopyKeyPathTrivialIndicesFn();
  } else {
    // Generate a destructor for this set of indices.
    {
      auto destroyType = llvm::FunctionType::get(IGM.VoidTy,
                                                 {IGM.Int8PtrTy, IGM.SizeTy},
                                                 /*vararg*/ false);
      auto destroyFn = llvm::Function::Create(destroyType,
        llvm::GlobalValue::PrivateLinkage, "keypath_destroy", IGM.getModule());
      destroy = destroyFn;
      
      IRGenFunction IGF(IGM, destroyFn);
      if (IGM.DebugInfo)
        IGM.DebugInfo->emitArtificialFunction(IGF, destroyFn);
    
      auto params = IGF.collectParameters();
      auto componentArgsBuf = params.claimNext();
      auto componentArgsBufSize = params.claimNext();
      bindPolymorphicArgumentsFromComponentIndices(IGF, component,
                                                   genericEnv, requirements,
                                                   componentArgsBuf,
                                                   componentArgsBufSize);
      
      llvm::Value *offset = nullptr;
      for (auto &component : component.getComputedPropertyIndices()) {
        auto ty = genericEnv
          ? genericEnv->mapTypeIntoContext(IGM.getSILModule(),
                                           component.LoweredType)
          : component.LoweredType;
        auto &ti = IGM.getTypeInfo(ty);
        if (offset) {
          auto align = ti.getAlignmentMask(IGF, ty);
          auto notAlign = IGF.Builder.CreateNot(align);
          offset = IGF.Builder.CreateAdd(offset, align);
          offset = IGF.Builder.CreateAnd(offset, notAlign);
        } else {
          offset = llvm::ConstantInt::get(IGM.SizeTy, 0);
        }
        auto elt = IGF.Builder.CreateInBoundsGEP(componentArgsBuf, offset);
        auto eltAddr = ti.getAddressForPointer(
          IGF.Builder.CreateBitCast(elt, ti.getStorageType()->getPointerTo()));
        ti.destroy(IGF, eltAddr, ty,
                   true /*witness table: need it to be fast*/);
        auto size = ti.getSize(IGF, ty);
        offset = IGF.Builder.CreateAdd(offset, size);
      }
      IGF.Builder.CreateRetVoid();
    }
    // Generate a copier for this set of indices.
    {
      auto copyType = llvm::FunctionType::get(IGM.VoidTy,
                                              {IGM.Int8PtrTy, IGM.Int8PtrTy,
                                               IGM.SizeTy},
                                              /*vararg*/ false);
      auto copyFn = llvm::Function::Create(copyType,
        llvm::GlobalValue::PrivateLinkage, "keypath_copy", IGM.getModule());
      copy = copyFn;
      
      IRGenFunction IGF(IGM, copyFn);
      if (IGM.DebugInfo)
        IGM.DebugInfo->emitArtificialFunction(IGF, copyFn);
    
      auto params = IGF.collectParameters();
      auto sourceArgsBuf = params.claimNext();
      auto destArgsBuf = params.claimNext();
      auto componentArgsBufSize = params.claimNext();
      bindPolymorphicArgumentsFromComponentIndices(IGF, component,
                                                   genericEnv, requirements,
                                                   sourceArgsBuf,
                                                   componentArgsBufSize);
      
      // Copy over the index values.
      llvm::Value *offset = nullptr;
      for (auto &component : component.getComputedPropertyIndices()) {
        auto ty = genericEnv
          ? genericEnv->mapTypeIntoContext(IGM.getSILModule(),
                                           component.LoweredType)
          : component.LoweredType;
        auto &ti = IGM.getTypeInfo(ty);
        if (offset) {
          auto align = ti.getAlignmentMask(IGF, ty);
          auto notAlign = IGF.Builder.CreateNot(align);
          offset = IGF.Builder.CreateAdd(offset, align);
          offset = IGF.Builder.CreateAnd(offset, notAlign);
        } else {
          offset = llvm::ConstantInt::get(IGM.SizeTy, 0);
        }
        auto sourceElt = IGF.Builder.CreateInBoundsGEP(sourceArgsBuf, offset);
        auto destElt = IGF.Builder.CreateInBoundsGEP(destArgsBuf, offset);
        auto sourceEltAddr = ti.getAddressForPointer(
          IGF.Builder.CreateBitCast(sourceElt,
                                    ti.getStorageType()->getPointerTo()));
        auto destEltAddr = ti.getAddressForPointer(
          IGF.Builder.CreateBitCast(destElt,
                                    ti.getStorageType()->getPointerTo()));

        ti.initializeWithCopy(IGF, destEltAddr, sourceEltAddr, ty, false);
        auto size = ti.getSize(IGF, ty);
        offset = IGF.Builder.CreateAdd(offset, size);
      }
      
      // Copy over the generic environment.
      if (genericEnv) {
        auto envAlignMask = llvm::ConstantInt::get(IGM.SizeTy,
          IGM.getPointerAlignment().getMaskValue());
        auto notAlignMask = IGF.Builder.CreateNot(envAlignMask);
        offset = IGF.Builder.CreateAdd(offset, envAlignMask);
        offset = IGF.Builder.CreateAnd(offset, notAlignMask);
        
        auto sourceEnv = IGF.Builder.CreateInBoundsGEP(sourceArgsBuf, offset);
        auto destEnv = IGF.Builder.CreateInBoundsGEP(destArgsBuf, offset);
        
        IGF.Builder.CreateMemCpy(destEnv, sourceEnv,
          IGM.getPointerSize().getValue() * requirements.size(),
          IGM.getPointerAlignment().getValue());
      }
      
      IGF.Builder.CreateRetVoid();
    }
  }
  
  auto equals = getAccessorForComputedComponent(IGM, component, Equals,
                                                genericEnv, requirements);
  auto hash = getAccessorForComputedComponent(IGM, component, Hash,
                                              genericEnv, requirements);
  
  auto witnesses = llvm::ConstantStruct::getAnon({destroy, copy, equals, hash});
  return new llvm::GlobalVariable(IGM.Module, witnesses->getType(),
                                  /*constant*/ true,
                                  llvm::GlobalValue::PrivateLinkage,
                                  witnesses,
                                  "keypath_witnesses");
}

/// Information about each index operand for a key path pattern that is used
/// to lay out and consume the argument packet.
struct KeyPathIndexOperand {
  SILType LoweredType;
  const KeyPathPatternComponent *LastUser;
};

static llvm::Constant *
getInitializerForComputedComponent(IRGenModule &IGM,
           const KeyPathPatternComponent &component,
           ArrayRef<KeyPathIndexOperand> operands,
           GenericEnvironment *genericEnv,
           ArrayRef<GenericRequirement> requirements) {
  auto fnTy = llvm::FunctionType::get(IGM.VoidTy,
    { /*src*/ IGM.Int8PtrTy,
      /*dest*/ IGM.Int8PtrTy }, /*vararg*/ false);
      
  auto initFn = llvm::Function::Create(fnTy,
    llvm::GlobalValue::PrivateLinkage, "keypath_arg_init", IGM.getModule());
    
  {
    IRGenFunction IGF(IGM, initFn);
    if (IGM.DebugInfo)
      IGM.DebugInfo->emitArtificialFunction(IGF, initFn);

    auto params = IGF.collectParameters();
    // Pointer to the argument packet passed into swift_getKeyPath
    auto src = params.claimNext();
    // Pointer to the destination component's argument buffer
    auto dest = params.claimNext();
    
    SmallVector<Address, 4> srcAddresses;
    int lastOperandNeeded = -1;
    for (auto &index : component.getComputedPropertyIndices()) {
      lastOperandNeeded = std::max(lastOperandNeeded, (int)index.Operand);
    }
    
    llvm::Value *offset;
    
    if (genericEnv) {
      // We'll copy over the generic environment after we copy in the indexes.
      offset = llvm::ConstantInt::get(IGM.SizeTy,
        IGM.getPointerSize().getValue() * requirements.size());

      // Bind the generic environment from the argument buffer.
      bindFromGenericRequirementsBuffer(IGF, requirements,
        Address(src, IGF.IGM.getPointerAlignment()),
        [&](CanType t) {
          return genericEnv->mapTypeIntoContext(t)->getCanonicalType();
        });

    } else {
      offset = llvm::ConstantInt::get(IGM.SizeTy, 0);
    }
    
    // Figure out the offsets of the operands in the source buffer.
    for (int i = 0; i <= lastOperandNeeded; ++i) {
      auto ty = genericEnv
        ? genericEnv->mapTypeIntoContext(IGM.getSILModule(),
                                         operands[i].LoweredType)
        : operands[i].LoweredType;
      
      auto &ti = IGM.getTypeInfo(ty);

      if (i != 0 || genericEnv) {
        auto alignMask = ti.getAlignmentMask(IGF, ty);
        auto notAlignMask = IGF.Builder.CreateNot(alignMask);
        offset = IGF.Builder.CreateAdd(offset, alignMask);
        offset = IGF.Builder.CreateAnd(offset, notAlignMask);
      }
      
      auto ptr = IGF.Builder.CreateInBoundsGEP(src, offset);
      auto addr = ti.getAddressForPointer(IGF.Builder.CreateBitCast(
        ptr, ti.getStorageType()->getPointerTo()));
      srcAddresses.push_back(addr);
      
      auto size = ti.getSize(IGF, ty);
      offset = IGF.Builder.CreateAdd(offset, size);
    }
    
    offset = llvm::ConstantInt::get(IGM.SizeTy, 0);
    
    // Transfer the operands we want into the destination buffer.
    for (unsigned i : indices(component.getComputedPropertyIndices())) {
      auto &index = component.getComputedPropertyIndices()[i];
      
      auto ty = genericEnv
        ? genericEnv->mapTypeIntoContext(IGM.getSILModule(),
                                         index.LoweredType)
        : index.LoweredType;
      
      auto &ti = IGM.getTypeInfo(ty);
      
      if (i != 0) {
        auto alignMask = ti.getAlignmentMask(IGF, ty);
        auto notAlignMask = IGF.Builder.CreateNot(alignMask);
        offset = IGF.Builder.CreateAdd(offset, alignMask);
        offset = IGF.Builder.CreateAnd(offset, notAlignMask);
      }
      
      auto ptr = IGF.Builder.CreateInBoundsGEP(dest, offset);
      auto destAddr = ti.getAddressForPointer(IGF.Builder.CreateBitCast(
        ptr, ti.getStorageType()->getPointerTo()));
      
      // The last component using an operand can move the value out of the
      // buffer.
      if (&component == operands[index.Operand].LastUser) {
        ti.initializeWithTake(IGF, destAddr, srcAddresses[index.Operand], ty,
                              false);
      } else {
        ti.initializeWithCopy(IGF, destAddr, srcAddresses[index.Operand], ty,
                              false);
      }
      auto size = ti.getSize(IGF, ty);
      offset = IGF.Builder.CreateAdd(offset, size);
    }
    
    // Transfer the generic environment.
    if (genericEnv) {
      auto destGenericEnv = dest;
      if (!component.getComputedPropertyIndices().empty()) {
        auto genericEnvAlignMask = llvm::ConstantInt::get(IGM.SizeTy,
          IGM.getPointerAlignment().getMaskValue());
        auto notGenericEnvAlignMask = IGF.Builder.CreateNot(genericEnvAlignMask);
        offset = IGF.Builder.CreateAdd(offset, genericEnvAlignMask);
        offset = IGF.Builder.CreateAnd(offset, notGenericEnvAlignMask);
        destGenericEnv = IGF.Builder.CreateInBoundsGEP(dest, offset);
      }
      
      IGF.Builder.CreateMemCpy(destGenericEnv, src,
                           IGM.getPointerSize().getValue() * requirements.size(),
                           IGM.getPointerAlignment().getValue());
    }
    IGF.Builder.CreateRetVoid();
  }
  return initFn;
}

llvm::Constant *
IRGenModule::getAddrOfKeyPathPattern(KeyPathPattern *pattern,
                                     SILLocation diagLoc) {
  // See if we already emitted this.
  auto found = KeyPathPatterns.find(pattern);
  if (found != KeyPathPatterns.end())
    return found->second;
  
  // Gather type arguments from the root and leaf types of the key path.
  auto rootTy = pattern->getRootType();
  auto valueTy = pattern->getValueType();

  // Check for parameterization, whether by subscript indexes or by the generic
  // environment. If there isn't any, we can instantiate the pattern in-place.
  bool isInstantiableInPlace = pattern->getNumOperands() == 0
    && !pattern->getGenericSignature();

  // Collect the required parameters for the keypath's generic environment.
  SmallVector<GenericRequirement, 4> requirements;
  
  GenericEnvironment *genericEnv = nullptr;
  if (auto sig = pattern->getGenericSignature()) {
    genericEnv = sig->createGenericEnvironment();
    enumerateGenericSignatureRequirements(pattern->getGenericSignature(),
      [&](GenericRequirement reqt) { requirements.push_back(reqt); });
  }

  /// Generate a metadata accessor that produces metadata for the given type
  /// using arguments from the generic context of the key path.
  auto emitMetadataGenerator = [&](CanType type) -> llvm::Function * {
    // TODO: Use the standard metadata accessor when there are no arguments
    // and the metadata accessor is defined.
    
    // Build a stub that loads the necessary bindings from the key path's
    // argument buffer then fetches the metadata.
    auto fnTy = llvm::FunctionType::get(TypeMetadataPtrTy,
                                        {Int8PtrTy}, /*vararg*/ false);
    auto accessorThunk = llvm::Function::Create(fnTy,
                                              llvm::GlobalValue::PrivateLinkage,
                                              "keypath_get_type", getModule());
    accessorThunk->setAttributes(constructInitialAttributes());
    {
      IRGenFunction IGF(*this, accessorThunk);
      if (DebugInfo)
        DebugInfo->emitArtificialFunction(IGF, accessorThunk);

      if (type->hasTypeParameter()) {
        auto bindingsBufPtr = IGF.collectParameters().claimNext();

        bindFromGenericRequirementsBuffer(IGF, requirements,
          Address(bindingsBufPtr, getPointerAlignment()),
          [&](CanType t) {
            return genericEnv->mapTypeIntoContext(t)->getCanonicalType();
          });
      
        type = genericEnv->mapTypeIntoContext(type)->getCanonicalType();
      }
      auto ret = IGF.emitTypeMetadataRef(type);
      IGF.Builder.CreateRet(ret);
    }
    return accessorThunk;
  };
  
  // Start building the key path pattern.
  ConstantInitBuilder builder(*this);
  ConstantStructBuilder fields = builder.beginStruct();
  fields.setPacked(true);
  // Add a zero-initialized header we can use for lazy initialization.
  fields.add(llvm::ConstantInt::get(SizeTy, 0));

#ifndef NDEBUG
  auto startOfObject = fields.getNextOffsetFromGlobal();
#endif

  // Store references to metadata generator functions to generate the metadata
  // for the root and leaf. These sit in the "isa" and object header parts of
  // the final object.
  fields.add(emitMetadataGenerator(rootTy));
  fields.add(emitMetadataGenerator(valueTy));
  
#ifndef NDEBUG
  auto endOfObjectHeader = fields.getNextOffsetFromGlobal();
  unsigned expectedObjectHeaderSize;
  if (SizeTy == Int64Ty)
    expectedObjectHeaderSize = SWIFT_ABI_HEAP_OBJECT_HEADER_SIZE_64;
  else if (SizeTy == Int32Ty)
    expectedObjectHeaderSize = SWIFT_ABI_HEAP_OBJECT_HEADER_SIZE_32;
  else
    llvm_unreachable("unexpected pointer size");
  assert((endOfObjectHeader - startOfObject).getValue()
            == expectedObjectHeaderSize
       && "key path pattern header size doesn't match heap object header size");
#endif
  
  // Add a pointer to the ObjC KVC compatibility string, if there is one, or
  // null otherwise.
  llvm::Constant *objcString;
  if (!pattern->getObjCString().empty()) {
    objcString = getAddrOfGlobalString(pattern->getObjCString());
  } else {
    objcString = llvm::ConstantPointerNull::get(Int8PtrTy);
  }
  fields.add(objcString);
  
  // Leave a placeholder for the buffer header, since we need to know the full
  // buffer size to fill it in.
  auto headerPlaceholder = fields.addPlaceholderWithSize(Int32Ty);
  fields.addAlignmentPadding(getPointerAlignment());
  
  auto startOfKeyPathBuffer = fields.getNextOffsetFromGlobal();
  
  // Build out the components.
  auto baseTy = rootTy;
  
  auto assertPointerAlignment = [&]{
    assert(fields.getNextOffsetFromGlobal() % getPointerAlignment() == Size(0)
           && "must be pointer-aligned here");
  };
  
  // Collect the order and types of any captured index operands, which will
  // determine the layout of the buffer that gets passed to the initializer
  // for each component.
  SmallVector<KeyPathIndexOperand, 4> operands;
  operands.resize(pattern->getNumOperands());
  for (auto &component : pattern->getComponents()) {
    switch (component.getKind()) {
    case KeyPathPatternComponent::Kind::GettableProperty:
    case KeyPathPatternComponent::Kind::SettableProperty:
      for (auto &index : component.getComputedPropertyIndices()) {
        operands[index.Operand].LoweredType = index.LoweredType;
        operands[index.Operand].LastUser = &component;
      }
      break;
    case KeyPathPatternComponent::Kind::StoredProperty:
    case KeyPathPatternComponent::Kind::OptionalChain:
    case KeyPathPatternComponent::Kind::OptionalForce:
    case KeyPathPatternComponent::Kind::OptionalWrap:
      break;
    }
  }
  
  for (unsigned i : indices(pattern->getComponents())) {
    assertPointerAlignment();
    SILType loweredBaseTy;
    Lowering::GenericContextScope scope(getSILTypes(),
                                        pattern->getGenericSignature());
    loweredBaseTy = getLoweredType(AbstractionPattern::getOpaque(),
                                   baseTy->getWithoutSpecifierType());
    auto &component = pattern->getComponents()[i];
    switch (auto kind = component.getKind()) {
    case KeyPathPatternComponent::Kind::StoredProperty: {
      auto property = cast<VarDecl>(component.getStoredPropertyDecl());
      
      auto addFixedOffset = [&](bool isStruct, llvm::Constant *offset) {
        if (auto offsetInt = dyn_cast_or_null<llvm::ConstantInt>(offset)) {
          auto offsetValue = offsetInt->getValue().getZExtValue();
          if (KeyPathComponentHeader::offsetCanBeInline(offsetValue)) {
            auto header = isStruct
              ? KeyPathComponentHeader::forStructComponentWithInlineOffset(offsetValue)
              : KeyPathComponentHeader::forClassComponentWithInlineOffset(offsetValue);
            fields.addInt32(header.getData());
            return;
          }
        }
        auto header = isStruct
          ? KeyPathComponentHeader::forStructComponentWithOutOfLineOffset()
          : KeyPathComponentHeader::forClassComponentWithOutOfLineOffset();
        fields.addInt32(header.getData());
        fields.add(llvm::ConstantExpr::getTruncOrBitCast(offset, Int32Ty));
      };
      
      // For a struct stored property, we may know the fixed offset of the field,
      // or we may need to fetch it out of the type's metadata at instantiation
      // time.
      if (auto theStruct = loweredBaseTy.getStructOrBoundGenericStruct()) {
        if (auto offset = emitPhysicalStructMemberFixedOffset(*this,
                                                              loweredBaseTy,
                                                              property)) {
          // We have a known constant fixed offset.
          addFixedOffset(/*struct*/ true, offset);
          break;
        }

        // If the offset isn't fixed, try instead to get the field offset out
        // of the type metadata at instantiation time.
        auto &metadataLayout = getMetadataLayout(theStruct);
        auto fieldOffset = metadataLayout.getStaticFieldOffset(property);

        auto header = KeyPathComponentHeader::forStructComponentWithUnresolvedFieldOffset();
        fields.addInt32(header.getData());
        fields.addInt32(fieldOffset.getValue());
        break;
      }
      
      // For a class, we may know the fixed offset of a field at compile time,
      // or we may need to fetch it at instantiation time. Depending on the
      // ObjC-ness and resilience of the class hierarchy, there might be a few
      // different ways we need to go about this.
      if (loweredBaseTy.getClassOrBoundGenericClass()) {
        switch (getClassFieldAccess(*this, loweredBaseTy, property)) {
        case FieldAccess::ConstantDirect: {
          // Known constant fixed offset.
          auto offset = tryEmitConstantClassFragilePhysicalMemberOffset(*this,
                                                                  loweredBaseTy,
                                                                  property);
          assert(offset && "no constant offset for ConstantDirect field?!");
          addFixedOffset(/*struct*/ false, offset);
          break;
        }
        case FieldAccess::NonConstantDirect: {
          // A constant offset that's determined at class realization time.
          // We have to load the offset from a global ivar.
          auto header =
            KeyPathComponentHeader::forClassComponentWithUnresolvedIndirectOffset();
          fields.addInt32(header.getData());
          fields.addAlignmentPadding(getPointerAlignment());
          auto offsetVar = getAddrOfFieldOffset(property, /*indirect*/ false,
                                                NotForDefinition);
          fields.add(cast<llvm::Constant>(offsetVar.getAddress()));
          break;
        }
        case FieldAccess::ConstantIndirect: {
          // An offset that depends on the instance's generic parameterization,
          // but whose field offset is at a known vtable offset.
          auto header =
            KeyPathComponentHeader::forClassComponentWithUnresolvedFieldOffset();
          fields.addInt32(header.getData());
          auto fieldOffset =
            getClassFieldOffsetOffset(*this, loweredBaseTy.getClassOrBoundGenericClass(),
                                      property);
          fields.addInt32(fieldOffset.getValue());
          break;
        }
        case FieldAccess::NonConstantIndirect:
          // An offset that depends on the instance's generic parameterization,
          // whose vtable offset is also unknown.
          // TODO: This doesn't happen until class resilience is enabled.
          llvm_unreachable("not implemented");
        }
        break;
      }
      llvm_unreachable("not struct or class");
    }
    case KeyPathPatternComponent::Kind::GettableProperty:
    case KeyPathPatternComponent::Kind::SettableProperty: {
      // Encode the settability.
      bool settable = kind == KeyPathPatternComponent::Kind::SettableProperty;
      KeyPathComponentHeader::ComputedPropertyKind componentKind;
      if (settable) {
        componentKind = component.isComputedSettablePropertyMutating()
          ? KeyPathComponentHeader::SettableMutating
          : KeyPathComponentHeader::SettableNonmutating;
      } else {
        componentKind = KeyPathComponentHeader::GetOnly;
      }
      
      // Lower the id reference.
      auto id = component.getComputedPropertyId();
      KeyPathComponentHeader::ComputedPropertyIDKind idKind;
      llvm::Constant *idValue;
      bool idResolved;
      switch (id.getKind()) {
      case KeyPathPatternComponent::ComputedPropertyId::Function:
        idKind = KeyPathComponentHeader::Pointer;
        idValue = getAddrOfSILFunction(id.getFunction(), NotForDefinition);
        idResolved = true;
        break;
      case KeyPathPatternComponent::ComputedPropertyId::DeclRef: {
        auto declRef = id.getDeclRef();
      
        // Foreign method refs identify using a selector
        // reference, which is doubly-indirected and filled in with a unique
        // pointer by dyld.
        if (declRef.isForeign) {
          assert(ObjCInterop && "foreign keypath component w/o objc interop?!");
          idKind = KeyPathComponentHeader::Pointer;
          idValue = getAddrOfObjCSelectorRef(declRef);
          idResolved = false;
        } else {
          idKind = KeyPathComponentHeader::VTableOffset;
          auto dc = declRef.getDecl()->getDeclContext();
          if (isa<ClassDecl>(dc)) {
            auto overridden = getSILTypes().getOverriddenVTableEntry(declRef);
            auto declaringClass =
              cast<ClassDecl>(overridden.getDecl()->getDeclContext());
            auto &metadataLayout = getMetadataLayout(declaringClass);
            auto offset = metadataLayout.getStaticMethodOffset(overridden);
            idValue = llvm::ConstantInt::get(SizeTy, offset.getValue());
            idResolved = true;
          } else if (auto methodProto = dyn_cast<ProtocolDecl>(dc)) {
            auto &protoInfo = getProtocolInfo(methodProto);
            auto index = protoInfo.getFunctionIndex(
                                 cast<AbstractFunctionDecl>(declRef.getDecl()));
            idValue = llvm::ConstantInt::get(SizeTy, -index.getValue());
            idResolved = true;
          } else {
            llvm_unreachable("neither a class nor protocol dynamic method?");
          }
        }
        break;
      }
      case KeyPathPatternComponent::ComputedPropertyId::Property:
        // Use the index of the stored property within the aggregate to key
        // the property.
        auto property = id.getProperty();
        idKind = KeyPathComponentHeader::StoredPropertyIndex;
        if (baseTy->getStructOrBoundGenericStruct()) {
          idResolved = true;
          Optional<unsigned> structIdx =  getPhysicalStructFieldIndex(*this,
                            SILType::getPrimitiveAddressType(baseTy), property);
          assert(structIdx.hasValue() && "empty property");
          idValue = llvm::ConstantInt::get(SizeTy, structIdx.getValue());
        } else if (baseTy->getClassOrBoundGenericClass()) {
          // TODO: This field index would require runtime resolution with Swift
          // native class resilience. We never directly access ObjC-imported
          // ivars so we can disregard ObjC ivar resilience for this computation
          // and start counting at the Swift native root.
          switch (getClassFieldAccess(*this, loweredBaseTy, property)) {
          case FieldAccess::ConstantDirect:
          case FieldAccess::ConstantIndirect:
          case FieldAccess::NonConstantDirect:
            idResolved = true;
            idValue = llvm::ConstantInt::get(SizeTy,
              getClassFieldIndex(*this,
                           SILType::getPrimitiveAddressType(baseTy), property));
            break;
          case FieldAccess::NonConstantIndirect:
            llvm_unreachable("not implemented");
          }
          
        } else {
          llvm_unreachable("neither struct nor class");
        }
        break;
      }
      
      auto header = KeyPathComponentHeader::forComputedProperty(componentKind,
                                    idKind, !isInstantiableInPlace, idResolved);
      
      fields.addInt32(header.getData());
      fields.addAlignmentPadding(getPointerAlignment());
      fields.add(idValue);
      
      if (isInstantiableInPlace) {
        // No generic arguments or indexes, so we can invoke the
        // getter/setter as is.
        fields.add(getAddrOfSILFunction(component.getComputedPropertyGetter(),
                                        NotForDefinition));
        if (settable)
          fields.add(getAddrOfSILFunction(component.getComputedPropertySetter(),
                                          NotForDefinition));
      } else {
        // If there's generic context or subscript indexes, embed as
        // arguments in the component. Thunk the SIL-level accessors to give the
        // runtime implementation a polymorphically-callable interface.
        
        // Push the accessors, possibly thunked to marshal generic environment.
        fields.add(getAccessorForComputedComponent(*this, component, Getter,
                                                     genericEnv, requirements));
        if (settable)
          fields.add(getAccessorForComputedComponent(*this, component, Setter,
                                                     genericEnv, requirements));
                                                     
        fields.add(getLayoutFunctionForComputedComponent(*this, component,
                                                     genericEnv, requirements));
                                                     
        // Set up a "witness table" for the component that handles copying,
        // destroying, equating, and hashing the captured contents of the
        // component.
        // If there are only generic parameters, we can use a prefab witness
        // table from the runtime.
        // TODO: For subscripts we'd generate functions that dispatch out to
        // the copy/destroy/equals/hash functionality of the subscript indexes.
        fields.add(getWitnessTableForComputedComponent(*this, component,
                                                     genericEnv, requirements));
        
        // Add an initializer function that copies generic arguments out of the
        // pattern argument buffer into the instantiated object.
        fields.add(getInitializerForComputedComponent(*this, component, operands,
                                                     genericEnv, requirements));
      }
      break;
    }
    case KeyPathPatternComponent::Kind::OptionalChain:
      fields.addInt32(KeyPathComponentHeader::forOptionalChain().getData());
      break;
    case KeyPathPatternComponent::Kind::OptionalForce:
      fields.addInt32(KeyPathComponentHeader::forOptionalForce().getData());
      break;
    case KeyPathPatternComponent::Kind::OptionalWrap:
      fields.addInt32(KeyPathComponentHeader::forOptionalWrap().getData());
      break;
    }
    
    // For all but the last component, we pack in the type of the component.
    if (i + 1 != pattern->getComponents().size()) {
      fields.addAlignmentPadding(getPointerAlignment());
      fields.add(emitMetadataGenerator(component.getComponentType()));
    }
    baseTy = component.getComponentType();
  }
  
  // Save the total size of the buffer.
  Size componentSize = fields.getNextOffsetFromGlobal()
    - startOfKeyPathBuffer;
  
  // We now have enough info to build the header.
  KeyPathBufferHeader header(componentSize.getValue(), isInstantiableInPlace,
                             /*reference prefix*/ false);
  // Add the header, followed by the components.
  fields.fillPlaceholder(headerPlaceholder,
                         llvm::ConstantInt::get(Int32Ty, header.getData()));
  
  // Create the global variable.
  // TODO: The pattern could be immutable if
  // it isn't instantiable in place, and if we made the type metadata accessor
  // references private, it could go in true-const memory.
  auto patternVar = fields.finishAndCreateGlobal("keypath",
                                          getPointerAlignment(),
                                          /*constant*/ false,
                                          llvm::GlobalVariable::PrivateLinkage);
  KeyPathPatterns.insert({pattern, patternVar});
  return patternVar;
}
