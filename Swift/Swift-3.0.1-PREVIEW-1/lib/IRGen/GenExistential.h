//===--- GenExistential.h - IR generation for existentials ------*- C++ -*-===//
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
//  This file provides the private interface to the existential emission code.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IRGEN_GENEXISTENTIAL_H
#define SWIFT_IRGEN_GENEXISTENTIAL_H

#include "Address.h"
#include "swift/Basic/LLVM.h"
#include "swift/AST/Types.h"

namespace llvm {
  class Value;
}

namespace swift {
  class ProtocolConformanceRef;
  class SILType;

namespace irgen {
  class Address;
  class Explosion;
  class IRGenFunction;

  /// Emit the metadata and witness table initialization for an allocated
  /// opaque existential container.
  Address emitOpaqueExistentialContainerInit(IRGenFunction &IGF,
                                   Address dest,
                                   SILType destType,
                                   CanType formalSrcType,
                                   SILType loweredSrcType,
                                 ArrayRef<ProtocolConformanceRef> conformances);

  /// Emit an existential metatype container from a metatype value
  /// as an explosion.
  void emitExistentialMetatypeContainer(IRGenFunction &IGF,
                                        Explosion &out,
                                        SILType outType,
                                        llvm::Value *metatype,
                                        SILType metatypeType,
                                 ArrayRef<ProtocolConformanceRef> conformances);
  
  
  /// Emit a class existential container from a class instance value
  /// as an explosion.
  void emitClassExistentialContainer(IRGenFunction &IGF,
                                 Explosion &out,
                                 SILType outType,
                                 llvm::Value *instance,
                                 CanType instanceFormalType,
                                 SILType instanceLoweredType,
                                 ArrayRef<ProtocolConformanceRef> conformances);

  /// Allocate a boxed existential container with uninitialized space to hold a
  /// value of a given type.
  OwnedAddress emitBoxedExistentialContainerAllocation(IRGenFunction &IGF,
                                  SILType destType,
                                  CanType formalSrcType,
                                 ArrayRef<ProtocolConformanceRef> conformances);
  
  /// "Deinitialize" an existential container whose contained value is allocated
  /// but uninitialized, by deallocating the buffer owned by the container if any.
  void emitOpaqueExistentialContainerDeinit(IRGenFunction &IGF,
                                            Address container,
                                            SILType type);
  
  /// Deallocate a boxed existential container with uninitialized space to hold
  /// a value of a given type.
  void emitBoxedExistentialContainerDeallocation(IRGenFunction &IGF,
                                                 Explosion &container,
                                                 SILType containerType,
                                                 CanType valueType);
  
  /// Emit a projection from an existential container address to the address
  /// of its concrete value buffer.
  ///
  /// \param openedArchetype If non-null, the archetype that will capture the
  /// metadata and witness tables produced by projecting the archetype.
  Address emitOpaqueExistentialProjection(IRGenFunction &IGF,
                                          Address base,
                                          SILType baseTy,
                                          CanArchetypeType openedArchetype);
  
  /// Extract the instance pointer from a class existential value.
  ///
  /// \param openedArchetype If non-null, the archetype that will capture the
  /// metadata and witness tables produced by projecting the archetype.
  llvm::Value *emitClassExistentialProjection(IRGenFunction &IGF,
                                              Explosion &base,
                                              SILType baseTy,
                                              CanArchetypeType openedArchetype);

  /// Extract the metatype pointer from an existential metatype value.
  ///
  /// \param openedTy If non-null, a metatype of the archetype that
  ///   will capture the metadata and witness tables
  llvm::Value *emitExistentialMetatypeProjection(IRGenFunction &IGF,
                                                 Explosion &base,
                                                 SILType baseTy,
                                                 CanType openedTy);

  /// Project the address of the value inside a boxed existential container.
  ContainedAddress emitBoxedExistentialProjection(IRGenFunction &IGF,
                                                  Explosion &base,
                                                  SILType baseTy,
                                                  CanType projectedType);

  /// Project the address of the value inside a boxed existential container,
  /// and open an archetype to its contained type.
  Address emitOpenExistentialBox(IRGenFunction &IGF,
                                 Explosion &base,
                                 SILType baseTy,
                                 CanArchetypeType openedArchetype);

  /// Emit the existential metatype of an opaque existential value.
  void emitMetatypeOfOpaqueExistential(IRGenFunction &IGF, Address addr,
                                       SILType type, Explosion &out);
  
  /// Emit the existential metatype of a class existential value.
  void emitMetatypeOfClassExistential(IRGenFunction &IGF,
                                      Explosion &value, SILType metatypeType,
                                      SILType existentialType, Explosion &out);

  /// Emit the existential metatype of a boxed existential value.
  void emitMetatypeOfBoxedExistential(IRGenFunction &IGF, Explosion &value,
                                      SILType type, Explosion &out);

  /// Emit the existential metatype of a metatype.
  void emitMetatypeOfMetatype(IRGenFunction &IGF, Explosion &value,
                              SILType existentialType, Explosion &out);

  std::pair<Address, llvm::Value*>
  emitIndirectExistentialProjectionWithMetadata(IRGenFunction &IGF,
                                                Address base,
                                                SILType baseTy,
                                                CanType openedArchetype);
  
} // end namespace irgen
} // end namespace swift

#endif
