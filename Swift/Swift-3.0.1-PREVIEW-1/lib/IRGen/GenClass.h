//===--- GenClass.h - Swift IR generation for classes -----------*- C++ -*-===//
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
//
//  This file provides the private interface to the class-emission code.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IRGEN_GENCLASS_H
#define SWIFT_IRGEN_GENCLASS_H

#include "llvm/ADT/SmallVector.h"

namespace llvm {
  class Constant;
  class Value;
  class Function;
}

namespace swift {
  class CanType;
  class ClassDecl;
  class ExtensionDecl;
  class ProtocolDecl;
  class SILType;
  class Type;
  class VarDecl;

namespace irgen {
  class HeapLayout;
  class IRGenFunction;
  class IRGenModule;
  class MemberAccessStrategy;
  class OwnedAddress;
  class Size;
  
  enum class ReferenceCounting : unsigned char;
  enum class IsaEncoding : unsigned char;
  enum class ClassDeallocationKind : unsigned char;
  
  OwnedAddress projectPhysicalClassMemberAddress(IRGenFunction &IGF,
                                                 llvm::Value *base,
                                                 SILType baseType,
                                                 SILType fieldType,
                                                 VarDecl *field);

  /// Return a strategy for accessing the given stored class property.
  ///
  /// This API is used by RemoteAST.
  MemberAccessStrategy
  getPhysicalClassMemberAccessStrategy(IRGenModule &IGM,
                                       SILType baseType, VarDecl *field);


  std::tuple<llvm::Constant * /*classData*/,
             llvm::Constant * /*metaclassData*/,
             Size>
  emitClassPrivateDataFields(IRGenModule &IGM, ClassDecl *cls);
  
  llvm::Constant *emitClassPrivateData(IRGenModule &IGM, ClassDecl *theClass);
  void emitGenericClassPrivateDataTemplate(IRGenModule &IGM,
                                      ClassDecl *theClass,
                                      llvm::SmallVectorImpl<llvm::Constant*> &fields,
                                      Size &metaclassOffset,
                                      Size &classRODataOffset,
                                      Size &metaclassRODataOffset,
                                      Size &totalSize);
  llvm::Constant *emitCategoryData(IRGenModule &IGM, ExtensionDecl *ext);
  llvm::Constant *emitObjCProtocolData(IRGenModule &IGM, ProtocolDecl *ext);

  /// Emit an allocation of a class.
  /// The \p StackAllocSize is an in- and out-parameter. The passed value
  /// specifies the maximum object size for stack allocation. A negative value
  /// means that no stack allocation is possible.
  /// The returned \p StackAllocSize value is the actual size if the object is
  /// allocated on the stack or -1, if the object is allocated on the heap.
  llvm::Value *emitClassAllocation(IRGenFunction &IGF, SILType selfType,
                                   bool objc, int &StackAllocSize);

  /// Emit an allocation of a class using a metadata value.
  llvm::Value *emitClassAllocationDynamic(IRGenFunction &IGF, 
                                          llvm::Value *metadata,
                                          SILType selfType,
                                          bool objc);

  /// Emit class deallocation.
  void emitClassDeallocation(IRGenFunction &IGF, SILType selfType,
                             llvm::Value *selfValue);

  /// Emit class deallocation.
  void emitPartialClassDeallocation(IRGenFunction &IGF,
                                    SILType selfType,
                                    llvm::Value *selfValue,
                                    llvm::Value *metadataValue);

  /// Emit the constant fragile instance size of the class, or null if the class
  /// does not have fixed layout. For resilient classes this does not
  /// correspond to the runtime alignment of instances of the class.
  llvm::Constant *tryEmitClassConstantFragileInstanceSize(IRGenModule &IGM,
                                                   ClassDecl *theClass);
  /// Emit the constant fragile instance alignment mask of the class, or null if
  /// the class does not have fixed layout. For resilient classes this does not
  /// correspond to the runtime alignment of instances of the class.
  llvm::Constant *tryEmitClassConstantFragileInstanceAlignMask(IRGenModule &IGM,
                                                        ClassDecl *theClass);
  
  /// What reference counting mechanism does a class use?
  ReferenceCounting getReferenceCountingForClass(IRGenModule &IGM,
                                                 ClassDecl *theClass);
  
  /// What isa-encoding mechanism does a type use?
  IsaEncoding getIsaEncodingForType(IRGenModule &IGM, CanType type);
  
  ClassDecl *getRootClassForMetaclass(IRGenModule &IGM, ClassDecl *theClass);

  /// Does the class metadata for the given class require dynamic
  /// initialization beyond what can be achieved automatically by
  /// the runtime?
  bool doesClassMetadataRequireDynamicInitialization(IRGenModule &IGM,
                                                     ClassDecl *theClass);
} // end namespace irgen
} // end namespace swift

#endif
