//===--- IRGenMangler.h - mangling of IRGen symbols -------------*- C++ -*-===//
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

#ifndef SWIFT_IRGEN_IRGENMANGLER_H
#define SWIFT_IRGEN_IRGENMANGLER_H

#include "IRGenModule.h"
#include "swift/AST/ASTMangler.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/IRGen/ValueWitness.h"

namespace swift {
namespace irgen {

/// The mangler for all kind of symbols produced in IRGen.
class IRGenMangler : public Mangle::ASTMangler {
public:
  IRGenMangler() { }

  std::string mangleValueWitness(Type type, ValueWitness witness);

  std::string mangleValueWitnessTable(Type type) {
    return mangleTypeSymbol(type, "WV");
  }

  std::string mangleTypeMetadataAccessFunction(Type type) {
    return mangleTypeSymbol(type, "Ma");
  }

  std::string mangleTypeMetadataLazyCacheVariable(Type type) {
    return mangleTypeSymbol(type, "ML");
  }

  std::string mangleTypeFullMetadataFull(Type type) {
    return mangleTypeSymbol(type, "Mf");
  }

  std::string mangleTypeMetadataFull(Type type, bool isPattern) {
    return mangleTypeSymbol(type, isPattern ? "MP" : "N");
  }

  std::string mangleClassMetaClass(const ClassDecl *Decl) {
    return mangleNominalTypeSymbol(Decl, "Mm");
  }

  std::string mangleNominalTypeDescriptor(const NominalTypeDecl *Decl) {
    return mangleNominalTypeSymbol(Decl, "Mn");
  }

  std::string mangleProtocolDescriptor(const ProtocolDecl *Decl) {
    beginMangling();
    appendProtocolName(Decl);
    appendOperator("Mp");
    return finalize();
  }

  std::string mangleFieldOffsetFull(const ValueDecl *Decl, bool isIndirect) {
    beginMangling();
    appendEntity(Decl);
    appendOperator("Wv", isIndirect ? "i" : "d");
    return finalize();
  }

  std::string mangleDirectProtocolWitnessTable(const ProtocolConformance *C) {
    return mangleConformanceSymbol(Type(), C, "WP");
  }

  std::string mangleGenericProtocolWitnessTableCache(
                                                const ProtocolConformance *C) {
    return mangleConformanceSymbol(Type(), C, "WG");
  }

  std::string mangleGenericProtocolWitnessTableInstantiationFunction(
                                                const ProtocolConformance *C) {
    return mangleConformanceSymbol(Type(), C, "WI");
  }

  std::string mangleProtocolWitnessTableAccessFunction(
                                                const ProtocolConformance *C) {
    return mangleConformanceSymbol(Type(), C, "Wa");
  }

  std::string mangleProtocolWitnessTableLazyAccessFunction(Type type,
                                                const ProtocolConformance *C) {
    return mangleConformanceSymbol(type, C, "Wl");
  }

  std::string mangleProtocolWitnessTableLazyCacheVariable(Type type,
                                                const ProtocolConformance *C) {
    return mangleConformanceSymbol(type, C, "WL");
  }

  std::string mangleAssociatedTypeMetadataAccessFunction(
                                      const ProtocolConformance *Conformance,
                                      StringRef AssocTyName) {
    beginMangling();
    appendProtocolConformance(Conformance);
    appendIdentifier(AssocTyName);
    appendOperator("Wt");
    return finalize();
  }

  std::string mangleAssociatedTypeWitnessTableAccessFunction(
                                      const ProtocolConformance *Conformance,
                                      CanType AssociatedType,
                                      const ProtocolDecl *Proto) {
    beginMangling();
    appendProtocolConformance(Conformance);
    bool isFirstAssociatedTypeIdentifier = true;
    appendAssociatedTypePath(AssociatedType, isFirstAssociatedTypeIdentifier);
    appendAnyGenericType(Proto);
    appendOperator("WT");
    return finalize();
  }

  void appendAssociatedTypePath(CanType associatedType, bool &isFirst) {
    if (auto memberType = dyn_cast<DependentMemberType>(associatedType)) {
      appendAssociatedTypePath(memberType.getBase(), isFirst);
      appendIdentifier(memberType->getName().str());
      appendListSeparator(isFirst);
    } else {
      assert(isa<GenericTypeParamType>(associatedType));
    }
  }

  std::string mangleReflectionBuiltinDescriptor(Type type) {
    return mangleTypeSymbol(type, "MB");
  }

  std::string mangleReflectionFieldDescriptor(Type type) {
    return mangleTypeSymbol(type, "MF");
  }

  std::string mangleReflectionAssociatedTypeDescriptor(
                                                 const ProtocolConformance *C) {
    return mangleConformanceSymbol(Type(), C, "MA");
  }

  std::string mangleReflectionSuperclassDescriptor(const ClassDecl *Decl) {
    return mangleNominalTypeSymbol(Decl, "MC");
  }

  std::string mangleOutlinedCopyFunction(const GenericTypeDecl *Decl) {
    beginMangling();
    appendAnyGenericType(Decl);
    appendOperator("Wy");
    return finalize();
  }
  std::string mangleOutlinedConsumeFunction(const GenericTypeDecl *Decl) {
    beginMangling();
    appendAnyGenericType(Decl);
    appendOperator("We");
    return finalize();
  }

  std::string mangleOutlinedRetainFunction(const Type t) {
    beginMangling();
    appendType(t);
    appendOperator("Wr");
    return finalize();
  }
  std::string mangleOutlinedReleaseFunction(const Type t) {
    beginMangling();
    appendType(t);
    appendOperator("Ws");
    return finalize();
  }

  std::string mangleOutlinedInitializeWithTakeFunction(const CanType t,
                                                       IRGenModule *mod) {
    beginMangling();
    if (!t->hasArchetype()) {
      appendType(t);
      appendOperator("Wb", Index(1));
    } else {
      appendModule(mod->getSwiftModule());
      appendOperator("y");
      appendOperator("t");
      appendOperator("Wb", Index(mod->getCanTypeID(t)));
    }
    return finalize();
  }
  std::string mangleOutlinedInitializeWithCopyFunction(const CanType t,
                                                       IRGenModule *mod) {
    beginMangling();
    if (!t->hasArchetype()) {
      appendType(t);
      appendOperator("Wc", Index(1));
    } else {
      appendModule(mod->getSwiftModule());
      appendOperator("y");
      appendOperator("t");
      appendOperator("Wc", Index(mod->getCanTypeID(t)));
    }
    return finalize();
  }
  std::string mangleOutlinedAssignWithTakeFunction(const CanType t,
                                                   IRGenModule *mod) {
    beginMangling();
    if (!t->hasArchetype()) {
      appendType(t);
      appendOperator("Wd", Index(1));
    } else {
      appendModule(mod->getSwiftModule());
      appendOperator("y");
      appendOperator("t");
      appendOperator("Wd", Index(mod->getCanTypeID(t)));
    }
    return finalize();
  }
  std::string mangleOutlinedAssignWithCopyFunction(const CanType t,
                                                   IRGenModule *mod) {
    beginMangling();
    if (!t->hasArchetype()) {
      appendType(t);
      appendOperator("Wf", Index(1));
    } else {
      appendModule(mod->getSwiftModule());
      appendOperator("y");
      appendOperator("t");
      appendOperator("Wf", Index(mod->getCanTypeID(t)));
    }
    return finalize();
  }
  std::string mangleOutlinedDestroyFunction(const CanType t, IRGenModule *mod) {
    beginMangling();
    if (!t->hasArchetype()) {
      appendType(t);
      appendOperator("Wh", Index(1));
    } else {
      appendModule(mod->getSwiftModule());
      appendOperator("y");
      appendOperator("t");
      appendOperator("Wh", Index(mod->getCanTypeID(t)));
    }
    return finalize();
  }

  std::string manglePartialApplyForwarder(StringRef FuncName);

  std::string mangleTypeForMetadata(Type type) {
    return mangleTypeWithoutPrefix(type);
  }

  std::string mangleForProtocolDescriptor(ProtocolType *Proto) {
    beginMangling();
    appendType(Proto->getCanonicalType());
    appendOperator("D");
    return finalize();
  }

  std::string mangleTypeForReflection(Type Ty, ModuleDecl *Module,
                                      bool isSingleFieldOfBox);

  std::string mangleTypeForLLVMTypeName(CanType Ty);

  std::string mangleProtocolForLLVMTypeName(ProtocolCompositionType *type);

protected:

  std::string mangleTypeSymbol(Type type, const char *Op) {
    beginMangling();
    appendType(type);
    appendOperator(Op);
    return finalize();
  }

  std::string mangleNominalTypeSymbol(const NominalTypeDecl *Decl,
                                      const char *Op) {
    beginMangling();
    appendAnyGenericType(Decl);
    appendOperator(Op);
    return finalize();
  }

  std::string mangleConformanceSymbol(Type type,
                                      const ProtocolConformance *Conformance,
                                      const char *Op) {
    beginMangling();
    if (type)
      appendType(type);
    appendProtocolConformance(Conformance);
    appendOperator(Op);
    return finalize();
  }
};

} // end namespace irgen
} // end namespace swift

#endif
