//===--- ASTDemangler.h - Swift AST symbol demangling -----------*- C++ -*-===//
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
// Defines a builder concept for the TypeDecoder and MetadataReader which builds
// AST Types, and a utility function wrapper which takes a mangled string and
// feeds it through the TypeDecoder instance.
//
// The RemoteAST library defines a MetadataReader instance that uses this
// concept, together with some additional utilities.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_ASTDEMANGLER_H
#define SWIFT_AST_ASTDEMANGLER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "swift/AST/Types.h"
#include "swift/Demangling/Demangler.h"
#include "swift/Demangling/NamespaceMacros.h"
#include "swift/Demangling/TypeDecoder.h"

namespace swift {
 
class TypeDecl;

namespace Demangle {
SWIFT_BEGIN_INLINE_NAMESPACE

Type getTypeForMangling(ASTContext &ctx,
                        llvm::StringRef mangling);

TypeDecl *getTypeDeclForMangling(ASTContext &ctx,
                                 llvm::StringRef mangling);

TypeDecl *getTypeDeclForUSR(ASTContext &ctx,
                            llvm::StringRef usr);

/// An implementation of MetadataReader's BuilderType concept that
/// just finds and builds things in the AST.
class ASTBuilder {
  ASTContext &Ctx;
  Demangle::NodeFactory Factory;

  /// The notional context in which we're writing and type-checking code.
  /// Created lazily.
  DeclContext *NotionalDC = nullptr;

public:
  using BuiltType = swift::Type;
  using BuiltTypeDecl = swift::GenericTypeDecl *; // nominal or type alias
  using BuiltProtocolDecl = swift::ProtocolDecl *;
  explicit ASTBuilder(ASTContext &ctx) : Ctx(ctx) {}

  ASTContext &getASTContext() { return Ctx; }
  DeclContext *getNotionalDC();

  Demangle::NodeFactory &getNodeFactory() { return Factory; }

  Type decodeMangledType(NodePointer node, bool forRequirement = true);
  Type createBuiltinType(StringRef builtinName, StringRef mangledName);

  TypeDecl *createTypeDecl(NodePointer node);

  GenericTypeDecl *createTypeDecl(StringRef mangledName, bool &typeAlias);
  
  GenericTypeDecl *createTypeDecl(NodePointer node,
                                  bool &typeAlias);

  ProtocolDecl *createProtocolDecl(NodePointer node);

  Type createNominalType(GenericTypeDecl *decl);

  Type createNominalType(GenericTypeDecl *decl, Type parent);

  Type createTypeAliasType(GenericTypeDecl *decl, Type parent);

  Type createBoundGenericType(GenericTypeDecl *decl, ArrayRef<Type> args);
  
  Type resolveOpaqueType(NodePointer opaqueDescriptor,
                         ArrayRef<ArrayRef<Type>> args,
                         unsigned ordinal);

  Type createBoundGenericType(GenericTypeDecl *decl, ArrayRef<Type> args,
                              Type parent);

  Type createTupleType(ArrayRef<Type> eltTypes, StringRef labels);

  Type createFunctionType(
      ArrayRef<Demangle::FunctionParam<Type>> params,
      Type output, FunctionTypeFlags flags,
      FunctionMetadataDifferentiabilityKind diffKind, Type globalActor);

  Type createImplFunctionType(
    Demangle::ImplParameterConvention calleeConvention,
    ArrayRef<Demangle::ImplFunctionParam<Type>> params,
    ArrayRef<Demangle::ImplFunctionResult<Type>> results,
    Optional<Demangle::ImplFunctionResult<Type>> errorResult,
    ImplFunctionTypeFlags flags);

  Type createProtocolCompositionType(ArrayRef<ProtocolDecl *> protocols,
                                     Type superclass,
                                     bool isClassBound,
                                     bool forRequirement = true);

  Type createExistentialMetatypeType(Type instance,
                     Optional<Demangle::ImplMetatypeRepresentation> repr=None);

  Type createMetatypeType(Type instance,
                     Optional<Demangle::ImplMetatypeRepresentation> repr=None);

  Type createGenericTypeParameterType(unsigned depth, unsigned index);

  Type createDependentMemberType(StringRef member, Type base);

  Type createDependentMemberType(StringRef member, Type base,
                                 ProtocolDecl *protocol);

#define REF_STORAGE(Name, ...) \
  Type create##Name##StorageType(Type base);
#include "swift/AST/ReferenceStorage.def"

  Type createSILBoxType(Type base);
  using BuiltSILBoxField = llvm::PointerIntPair<Type, 1>;
  using BuiltSubstitution = std::pair<Type, Type>;
  using BuiltRequirement = swift::Requirement;
  using BuiltLayoutConstraint = swift::LayoutConstraint;
  Type createSILBoxTypeWithLayout(ArrayRef<BuiltSILBoxField> Fields,
                                  ArrayRef<BuiltSubstitution> Substitutions,
                                  ArrayRef<BuiltRequirement> Requirements);

  bool isExistential(Type type) {
    return type->isExistentialType();
  }


  Type createObjCClassType(StringRef name);

  Type createBoundGenericObjCClassType(StringRef name, ArrayRef<Type> args);

  ProtocolDecl *createObjCProtocolDecl(StringRef name);

  Type createDynamicSelfType(Type selfType);

  Type createForeignClassType(StringRef mangledName);

  Type getUnnamedForeignClassType();

  Type getOpaqueType();

  Type createOptionalType(Type base);

  Type createArrayType(Type base);

  Type createDictionaryType(Type key, Type value);

  Type createParenType(Type base);

  LayoutConstraint getLayoutConstraint(LayoutConstraintKind kind);
  LayoutConstraint getLayoutConstraintWithSizeAlign(LayoutConstraintKind kind,
                                                    unsigned size,
                                                    unsigned alignment);

private:
  bool validateParentType(TypeDecl *decl, Type parent);
  CanGenericSignature demangleGenericSignature(
      NominalTypeDecl *nominalDecl,
      NodePointer node);
  DeclContext *findDeclContext(NodePointer node);
  ModuleDecl *findModule(NodePointer node);
  Demangle::NodePointer findModuleNode(NodePointer node);

  enum class ForeignModuleKind {
    Imported,
    SynthesizedByImporter
  };

  Optional<ForeignModuleKind>
  getForeignModuleKind(NodePointer node);

  GenericTypeDecl *findTypeDecl(DeclContext *dc,
                                Identifier name,
                                Identifier privateDiscriminator,
                                Demangle::Node::Kind kind);
  GenericTypeDecl *findForeignTypeDecl(StringRef name,
                                       StringRef relatedEntityKind,
                                       ForeignModuleKind lookupKind,
                                       Demangle::Node::Kind kind);

  static GenericTypeDecl *getAcceptableTypeDeclCandidate(ValueDecl *decl,
                                              Demangle::Node::Kind kind);
};

SWIFT_END_INLINE_NAMESPACE
}  // namespace Demangle

}  // namespace swift

#endif  // SWIFT_AST_ASTDEMANGLER_H
