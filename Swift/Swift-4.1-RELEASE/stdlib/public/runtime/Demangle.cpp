//===----------------------------------------------------------------------===//
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

#include "swift/Runtime/Metadata.h"
#include "swift/Strings.h"
#include "Private.h"

#include <vector>

#if SWIFT_OBJC_INTEROP
#include <objc/runtime.h>
#endif

using namespace swift;

// FIXME: This stuff should be merged with the existing logic in
// include/swift/Reflection/TypeRefBuilder.h as part of the rewrite
// to change stdlib reflection over to using remote mirrors.

Demangle::NodePointer
swift::_swift_buildDemanglingForMetadata(const Metadata *type,
                                         Demangle::Demangler &Dem);

static Demangle::NodePointer
_applyGenericArguments(const Metadata * const *genericArgs,
                       const NominalTypeDescriptor *description,
                       Demangle::NodePointer node, unsigned depth,
                       Demangle::Demangler &Dem) {
  assert(depth > 0);

  auto typeNode = node;
  if (typeNode->getKind() == Node::Kind::Type)
    typeNode = typeNode->getChild(0);

  auto parentNode = typeNode->getChild(0);

  // It might be more accurate to keep this sugar, but the old version
  // of this function dropped it, and I want to keep things compatible.
  if (parentNode->getKind() == Node::Kind::Extension) {
    parentNode = parentNode->getChild(1);
  }

  switch (parentNode->getKind()) {
  case Node::Kind::Class:
  case Node::Kind::Structure:
  case Node::Kind::Enum: {
    // The parent type is a nominal type which may have its own generic
    // arguments.
    auto newParentNode = _applyGenericArguments(genericArgs, description,
                                                parentNode, depth - 1,
                                                Dem);
    if (newParentNode == nullptr)
      return nullptr;

    auto newTypeNode = Dem.createNode(typeNode->getKind());
    newTypeNode->addChild(newParentNode, Dem);
    newTypeNode->addChild(typeNode->getChild(1), Dem);

    typeNode = newTypeNode;
    break;
  }
  default:
    // Parent is a local context or module. Leave it as-is, and just apply
    // generic arguments below.
    break;
  }

  // See if we have any generic arguments at this depth.
  unsigned numArgumentsAtDepth =
      description->GenericParams.getContext(depth - 1).NumPrimaryParams;
  if (numArgumentsAtDepth == 0) {
    // No arguments here, just return the original node (except we may have
    // replaced its parent type above).
    return typeNode;
  }

  // Ok, we have generic arguments. Figure out where the arguments for this
  // depth begin in the generic type metadata.
  unsigned firstArgumentAtDepth = 0;
  for (unsigned i = 0; i < depth - 1; i++) {
    firstArgumentAtDepth +=
        description->GenericParams.getContext(i).NumPrimaryParams;
  }

  // Demangle them.
  auto typeParams = Dem.createNode(Node::Kind::TypeList);
  for (unsigned i = firstArgumentAtDepth,
                e = firstArgumentAtDepth + numArgumentsAtDepth;
                i < e; ++i) {
    auto demangling = _swift_buildDemanglingForMetadata(genericArgs[i], Dem);
    if (demangling == nullptr)
      return nullptr;
    typeParams->addChild(demangling, Dem);
  }

  Node::Kind boundGenericKind;
  switch (typeNode->getKind()) {
  case Node::Kind::Class:
    boundGenericKind = Node::Kind::BoundGenericClass;
    break;
  case Node::Kind::Structure:
    boundGenericKind = Node::Kind::BoundGenericStructure;
    break;
  case Node::Kind::Enum:
    boundGenericKind = Node::Kind::BoundGenericEnum;
    break;
  default:
    return nullptr;
  }

  auto newNode = Dem.createNode(Node::Kind::Type);
  newNode->addChild(typeNode, Dem);

  auto genericNode = Dem.createNode(boundGenericKind);
  genericNode->addChild(newNode, Dem);
  genericNode->addChild(typeParams, Dem);
  return genericNode;
}

// Build a demangled type tree for a nominal type.
static Demangle::NodePointer
_buildDemanglingForNominalType(const Metadata *type, Demangle::Demangler &Dem) {
  using namespace Demangle;

  const NominalTypeDescriptor *description;

  // Demangle the parent type, if any.
  switch (type->getKind()) {
  case MetadataKind::Class: {
    auto classType = static_cast<const ClassMetadata *>(type);
#if SWIFT_OBJC_INTEROP
    // Peek through artificial subclasses.
    while (classType->isTypeMetadata() && classType->isArtificialSubclass())
      classType = classType->SuperClass;
#endif
    description = classType->getDescription();
    break;
  }
  case MetadataKind::Enum:
  case MetadataKind::Optional: {
    auto enumType = static_cast<const EnumMetadata *>(type);
    description = enumType->Description;
    break;
  }
  case MetadataKind::Struct: {
    auto structType = static_cast<const StructMetadata *>(type);
    description = structType->Description;
    break;
  }
  default:
    return nullptr;
  }

  // Demangle the base name.
  auto node = Dem.demangleType(StringRef(description->Name));
  assert(node->getKind() == Node::Kind::Type);

  auto typeBytes = reinterpret_cast<const char *>(type);
  auto genericArgs = reinterpret_cast<const Metadata * const *>(
               typeBytes + sizeof(void*) * description->GenericParams.Offset);

  return _applyGenericArguments(genericArgs, description, node,
                                description->GenericParams.NestingDepth,
                                Dem);
}

// Build a demangled type tree for a type.
Demangle::NodePointer
swift::_swift_buildDemanglingForMetadata(const Metadata *type,
                                         Demangle::Demangler &Dem) {
  using namespace Demangle;

  switch (type->getKind()) {
  case MetadataKind::Class:
  case MetadataKind::Enum:
  case MetadataKind::Optional:
  case MetadataKind::Struct:
    return _buildDemanglingForNominalType(type, Dem);
  case MetadataKind::ObjCClassWrapper: {
#if SWIFT_OBJC_INTEROP
    auto objcWrapper = static_cast<const ObjCClassWrapperMetadata *>(type);
    const char *className = class_getName(objcWrapper->getObjCClassObject());
    
    // ObjC classes mangle as being in the magic "__ObjC" module.
    auto module = Dem.createNode(Node::Kind::Module, "__ObjC");
    
    auto node = Dem.createNode(Node::Kind::Class);
    node->addChild(module, Dem);
    node->addChild(Dem.createNode(Node::Kind::Identifier,
                                       llvm::StringRef(className)), Dem);
    
    return node;
#else
    assert(false && "no ObjC interop");
    return nullptr;
#endif
  }
  case MetadataKind::ForeignClass: {
    auto foreign = static_cast<const ForeignClassMetadata *>(type);
    return Dem.demangleType(foreign->getName());
  }
  case MetadataKind::Existential: {
    auto exis = static_cast<const ExistentialTypeMetadata *>(type);
    
    std::vector<const ProtocolDescriptor *> protocols;
    protocols.reserve(exis->Protocols.NumProtocols);
    for (unsigned i = 0, e = exis->Protocols.NumProtocols; i < e; ++i)
      protocols.push_back(exis->Protocols[i]);

    auto type_list = Dem.createNode(Node::Kind::TypeList);
    auto proto_list = Dem.createNode(Node::Kind::ProtocolList);
    proto_list->addChild(type_list, Dem);

    // The protocol descriptors should be pre-sorted since the compiler will
    // only ever make a swift_getExistentialTypeMetadata invocation using
    // its canonical ordering of protocols.

    for (auto *protocol : protocols) {
      // The protocol name is mangled as a type symbol, with the _Tt prefix.
      StringRef ProtoName(protocol->Name);
      NodePointer protocolNode = Dem.demangleSymbol(ProtoName);

      // ObjC protocol names aren't mangled.
      if (!protocolNode) {
        auto module = Dem.createNode(Node::Kind::Module,
                                          MANGLING_MODULE_OBJC);
        auto node = Dem.createNode(Node::Kind::Protocol);
        node->addChild(module, Dem);
        node->addChild(Dem.createNode(Node::Kind::Identifier,
                                        llvm::StringRef(protocol->Name)), Dem);
        auto typeNode = Dem.createNode(Node::Kind::Type);
        typeNode->addChild(node, Dem);
        type_list->addChild(typeNode, Dem);
        continue;
      }

      // FIXME: We have to dig through a ridiculous number of nodes to get
      // to the Protocol node here.
      protocolNode = protocolNode->getChild(0); // Global -> TypeMangling
      protocolNode = protocolNode->getChild(0); // TypeMangling -> Type
      protocolNode = protocolNode->getChild(0); // Type -> ProtocolList
      protocolNode = protocolNode->getChild(0); // ProtocolList -> TypeList
      protocolNode = protocolNode->getChild(0); // TypeList -> Type
      
      assert(protocolNode->getKind() == Node::Kind::Type);
      assert(protocolNode->getChild(0)->getKind() == Node::Kind::Protocol);
      type_list->addChild(protocolNode, Dem);
    }

    if (auto superclass = exis->getSuperclassConstraint()) {
      // If there is a superclass constraint, we mangle it specially.
      auto result = Dem.createNode(Node::Kind::ProtocolListWithClass);
      auto superclassNode = _swift_buildDemanglingForMetadata(superclass, Dem);

      result->addChild(proto_list, Dem);
      result->addChild(superclassNode, Dem);
      return result;
    }

    if (exis->isClassBounded()) {
      // Check if the class constraint is implied by any of our
      // protocols.
      bool requiresClassImplicit = false;

      for (auto *protocol : protocols) {
        if (protocol->Flags.getClassConstraint()
            == ProtocolClassConstraint::Class)
          requiresClassImplicit = true;
      }

      // If it was implied, we don't do anything special.
      if (requiresClassImplicit)
        return proto_list;

      // If the existential type has an explicit AnyObject constraint,
      // we must mangle it as such.
      auto result = Dem.createNode(Node::Kind::ProtocolListWithAnyObject);
      result->addChild(proto_list, Dem);
      return result;
    }

    // Just a simple composition of protocols.
    return proto_list;
  }
  case MetadataKind::ExistentialMetatype: {
    auto metatype = static_cast<const ExistentialMetatypeMetadata *>(type);
    auto instance = _swift_buildDemanglingForMetadata(metatype->InstanceType,
                                                      Dem);
    auto node = Dem.createNode(Node::Kind::ExistentialMetatype);
    node->addChild(instance, Dem);
    return node;
  }
  case MetadataKind::Function: {
    auto func = static_cast<const FunctionTypeMetadata *>(type);

    Node::Kind kind;
    switch (func->getConvention()) {
    case FunctionMetadataConvention::Swift:
      kind = Node::Kind::FunctionType;
      break;
    case FunctionMetadataConvention::Block:
      kind = Node::Kind::ObjCBlock;
      break;
    case FunctionMetadataConvention::CFunctionPointer:
      kind = Node::Kind::CFunctionPointer;
      break;
    case FunctionMetadataConvention::Thin:
      kind = Node::Kind::ThinFunctionType;
      break;
    }
    
    std::vector<NodePointer> inputs;
    for (unsigned i = 0, e = func->getNumParameters(); i < e; ++i) {
      auto param = func->getParameter(i);
      auto flags = func->getParameterFlags(i);
      auto input = _swift_buildDemanglingForMetadata(param, Dem);

      if (flags.isInOut()) {
        NodePointer inout = Dem.createNode(Node::Kind::InOut);
        inout->addChild(input, Dem);
        input = inout;
      } else if (flags.isShared()) {
        NodePointer shared = Dem.createNode(Node::Kind::Shared);
        shared->addChild(input, Dem);
        input = shared;
      }
      inputs.push_back(input);
    }

    NodePointer totalInput = nullptr;
    switch (inputs.size()) {
    case 1:
      totalInput = inputs.front();
      break;

    // This covers both none and multiple parameters.
    default:
      auto tuple = Dem.createNode(Node::Kind::Tuple);
      for (auto &input : inputs)
        tuple->addChild(input, Dem);
      totalInput = tuple;
      break;
    }

    NodePointer args = Dem.createNode(Node::Kind::ArgumentTuple);
    args->addChild(totalInput, Dem);
    
    NodePointer resultTy = _swift_buildDemanglingForMetadata(func->ResultType,
                                                             Dem);
    NodePointer result = Dem.createNode(Node::Kind::ReturnType);
    result->addChild(resultTy, Dem);
    
    auto funcNode = Dem.createNode(kind);
    if (func->throws())
      funcNode->addChild(Dem.createNode(Node::Kind::ThrowsAnnotation), Dem);
    funcNode->addChild(args, Dem);
    funcNode->addChild(result, Dem);
    return funcNode;
  }
  case MetadataKind::Metatype: {
    auto metatype = static_cast<const MetatypeMetadata *>(type);
    auto instance = _swift_buildDemanglingForMetadata(metatype->InstanceType,
                                                      Dem);
    auto typeNode = Dem.createNode(Node::Kind::Type);
    typeNode->addChild(instance, Dem);
    auto node = Dem.createNode(Node::Kind::Metatype);
    node->addChild(typeNode, Dem);
    return node;
  }
  case MetadataKind::Tuple: {
    auto tuple = static_cast<const TupleTypeMetadata *>(type);
    const char *labels = tuple->Labels;
    auto tupleNode = Dem.createNode(Node::Kind::Tuple);
    for (unsigned i = 0, e = tuple->NumElements; i < e; ++i) {
      auto elt = Dem.createNode(Node::Kind::TupleElement);

      // Add a label child if applicable:
      if (labels) {
        // Look for the next space in the labels string.
        if (const char *space = strchr(labels, ' ')) {
          // If there is one, and the label isn't empty, add a label child.
          if (labels != space) {
            auto eltName =
              Dem.createNode(Node::Kind::TupleElementName,
                                  llvm::StringRef(labels, space - labels));
            elt->addChild(eltName, Dem);
          }

          // Skip past the space.
          labels = space + 1;
        }
      }

      // Add the element type child.
      auto eltType =
        _swift_buildDemanglingForMetadata(tuple->getElement(i).Type, Dem);
      elt->addChild(eltType, Dem);

      // Add the completed element to the tuple.
      tupleNode->addChild(elt, Dem);
    }
    return tupleNode;
  }
  case MetadataKind::Opaque:
    // FIXME: Some opaque types do have manglings, but we don't have enough info
    // to figure them out.
  case MetadataKind::HeapLocalVariable:
  case MetadataKind::HeapGenericLocalVariable:
  case MetadataKind::ErrorObject:
    break;
  }
  // Not a type.
  return nullptr;
}
