//===--- GenArchetype.h - Swift IR generation for archetypes ----*- C++ -*-===//
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
//  This file provides the private interface to the archetype emission code.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IRGEN_GENARCHETYPE_H
#define SWIFT_IRGEN_GENARCHETYPE_H

#include "swift/AST/Types.h"
#include "llvm/ADT/STLExtras.h"

namespace llvm {
  class Value;
}

namespace swift {
  class ProtocolDecl;
  class SILType;

namespace irgen {
  class Address;
  class IRGenFunction;

  using GetTypeParameterInContextFn =
    llvm::function_ref<CanType(CanType type)>;

  void bindArchetypeAccessPaths(IRGenFunction &IGF,
                                GenericSignature *generics,
                                GetTypeParameterInContextFn getInContext);

  /// Emit a type metadata reference for an archetype.
  llvm::Value *emitArchetypeTypeMetadataRef(IRGenFunction &IGF,
                                            CanArchetypeType archetype);

  /// Emit a witness table reference.
  llvm::Value *emitArchetypeWitnessTableRef(IRGenFunction &IGF,
                                            CanArchetypeType archetype,
                                            ProtocolDecl *protocol);

  /// Emit a metadata reference for an associated type of an archetype.
  llvm::Value *emitAssociatedTypeMetadataRef(IRGenFunction &IGF,
                                             CanArchetypeType origin,
                                             AssociatedTypeDecl *associate);

  /// Emit a witness table reference for a specific conformance of an
  /// associated type of an archetype.
  llvm::Value *emitAssociatedTypeWitnessTableRef(IRGenFunction &IGF,
                                                 CanArchetypeType origin,
                                                 AssociatedTypeDecl *associate,
                                                 llvm::Value *associateMetadata,
                                               ProtocolDecl *associateProtocol);

  /// Emit a dynamic metatype lookup for the given archetype.
  llvm::Value *emitDynamicTypeOfOpaqueArchetype(IRGenFunction &IGF,
                                                Address archetypeAddr,
                                                SILType archetypeType);
  
  
} // end namespace irgen
} // end namespace swift

#endif
