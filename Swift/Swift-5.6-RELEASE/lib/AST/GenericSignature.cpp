//===--- GenericSignature.cpp - Generic Signature AST ---------------------===//
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
// This file implements the GenericSignature class.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/GenericSignature.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Decl.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Module.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/TypeCheckRequests.h"
#include "swift/AST/Types.h"
#include "swift/Basic/SourceManager.h"
#include "swift/Basic/STLExtras.h"
#include "GenericSignatureBuilder.h"
#include "GenericSignatureBuilderImpl.h"
#include "RequirementMachine/RequirementMachine.h"
#include <functional>

using namespace swift;

void ConformanceAccessPath::print(raw_ostream &out) const {
  llvm::interleave(
      begin(), end(),
      [&](const Entry &entry) {
        entry.first.print(out);
        out << ": " << entry.second->getName();
      },
      [&] { out << " -> "; });
}

void ConformanceAccessPath::dump() const {
  print(llvm::errs());
  llvm::errs() << "\n";
}

GenericSignatureImpl::GenericSignatureImpl(
    TypeArrayView<GenericTypeParamType> params,
    ArrayRef<Requirement> requirements, bool isKnownCanonical)
    : NumGenericParams(params.size()), NumRequirements(requirements.size()),
      CanonicalSignatureOrASTContext() {
  std::uninitialized_copy(params.begin(), params.end(),
                          getTrailingObjects<Type>());
  std::uninitialized_copy(requirements.begin(), requirements.end(),
                          getTrailingObjects<Requirement>());

#ifndef NDEBUG
  // Make sure generic parameters are in the right order, and
  // none are missing.
  unsigned depth = 0;
  unsigned count = 0;
  for (auto param : params) {
    if (param->getDepth() != depth) {
      assert(param->getDepth() > depth && "Generic parameter depth mismatch");
      depth = param->getDepth();
      count = 0;
    }
    assert(param->getIndex() == count && "Generic parameter index mismatch");
    ++count;
  }
#endif

  if (isKnownCanonical)
    CanonicalSignatureOrASTContext =
        &GenericSignature::getASTContext(params, requirements);
}

TypeArrayView<GenericTypeParamType>
GenericSignatureImpl::getInnermostGenericParams() const {
  const auto params = getGenericParams();

  const unsigned maxDepth = params.back()->getDepth();
  if (params.front()->getDepth() == maxDepth)
    return params;

  // There is a depth change. Count the number of elements
  // to slice off the front.
  unsigned sliceCount = params.size() - 1;
  while (true) {
    if (params[sliceCount - 1]->getDepth() != maxDepth)
      break;
    --sliceCount;
  }

  return params.slice(sliceCount);
}

void GenericSignatureImpl::forEachParam(
    llvm::function_ref<void(GenericTypeParamType *, bool)> callback) const {
  // Figure out which generic parameters are concrete or same-typed to another
  // type parameter.
  auto genericParams = getGenericParams();
  auto genericParamsAreCanonical =
    SmallVector<bool, 4>(genericParams.size(), true);

  for (auto req : getRequirements()) {
    if (req.getKind() != RequirementKind::SameType) continue;

    GenericTypeParamType *gp;
    if (auto secondGP = req.getSecondType()->getAs<GenericTypeParamType>()) {
      // If two generic parameters are same-typed, then the right-hand one
      // is non-canonical.
      assert(req.getFirstType()->is<GenericTypeParamType>());
      gp = secondGP;
    } else {
      // Otherwise, the right-hand side is an associated type or concrete type,
      // and the left-hand one is non-canonical.
      gp = req.getFirstType()->getAs<GenericTypeParamType>();
      if (!gp) continue;

      // If an associated type is same-typed, it doesn't constrain the generic
      // parameter itself. That is, if T == U.Foo, then T is canonical, whereas
      // U.Foo is not.
      if (req.getSecondType()->isTypeParameter()) continue;
    }

    unsigned index = GenericParamKey(gp).findIndexIn(genericParams);
    genericParamsAreCanonical[index] = false;
  }

  // Call the callback with each parameter and the result of the above analysis.
  for (auto index : indices(genericParams))
    callback(genericParams[index], genericParamsAreCanonical[index]);
}

bool GenericSignatureImpl::areAllParamsConcrete() const {
  unsigned numConcreteGenericParams = 0;
  for (const auto &req : getRequirements()) {
    if (req.getKind() != RequirementKind::SameType) continue;
    if (!req.getFirstType()->is<GenericTypeParamType>()) continue;
    if (req.getSecondType()->isTypeParameter()) continue;

    ++numConcreteGenericParams;
  }

  return numConcreteGenericParams == getGenericParams().size();
}

ASTContext &GenericSignature::getASTContext(
                                    TypeArrayView<GenericTypeParamType> params,
                                    ArrayRef<swift::Requirement> requirements) {
  // The params and requirements cannot both be empty.
  if (!params.empty())
    return params.front()->getASTContext();
  else
    return requirements.front().getFirstType()->getASTContext();
}

/// Retrieve the generic parameters.
TypeArrayView<GenericTypeParamType> GenericSignature::getGenericParams() const {
  return isNull()
      ? TypeArrayView<GenericTypeParamType>{}
      : getPointer()->getGenericParams();
}

/// Retrieve the innermost generic parameters.
///
/// Given a generic signature for a nested generic type, produce an
/// array of the generic parameters for the innermost generic type.
TypeArrayView<GenericTypeParamType> GenericSignature::getInnermostGenericParams() const {
  return isNull()
      ? TypeArrayView<GenericTypeParamType>{}
      : getPointer()->getInnermostGenericParams();
}

/// Retrieve the requirements.
ArrayRef<Requirement> GenericSignature::getRequirements() const {
  return isNull()
      ? ArrayRef<Requirement>{}
      : getPointer()->getRequirements();
}

GenericSignatureBuilder *
GenericSignatureImpl::getGenericSignatureBuilder() const {
  // The generic signature builder is associated with the canonical signature.
  if (!isCanonical())
    return getCanonicalSignature()->getGenericSignatureBuilder();

  // generic signature builders are stored on the ASTContext.
  return getASTContext().getOrCreateGenericSignatureBuilder(
                                             CanGenericSignature(this));
}

rewriting::RequirementMachine *
GenericSignatureImpl::getRequirementMachine() const {
  if (Machine)
    return Machine;

  const_cast<GenericSignatureImpl *>(this)->Machine
      = getASTContext().getRewriteContext().getRequirementMachine(
          getCanonicalSignature());
  return Machine;
}

bool GenericSignatureImpl::isEqual(GenericSignature Other) const {
  return getCanonicalSignature() == Other.getCanonicalSignature();
}

bool GenericSignatureImpl::isCanonical() const {
  if (CanonicalSignatureOrASTContext.is<ASTContext *>())
    return true;
  return getCanonicalSignature().getPointer() == this;
}

CanGenericSignature
CanGenericSignature::getCanonical(TypeArrayView<GenericTypeParamType> params,
                                  ArrayRef<Requirement> requirements) {
  // Canonicalize the parameters and requirements.
  SmallVector<GenericTypeParamType*, 8> canonicalParams;
  canonicalParams.reserve(params.size());
  for (auto param : params) {
    canonicalParams.push_back(cast<GenericTypeParamType>(param->getCanonicalType()));
  }

  SmallVector<Requirement, 8> canonicalRequirements;
  canonicalRequirements.reserve(requirements.size());
  for (auto &reqt : requirements)
    canonicalRequirements.push_back(reqt.getCanonical());

  auto canSig = get(canonicalParams, canonicalRequirements,
                    /*isKnownCanonical=*/true);

  return CanGenericSignature(canSig);
}

CanGenericSignature GenericSignature::getCanonicalSignature() const {
  // If the underlying pointer is null, return `CanGenericSignature()`.
  if (isNull())
    return CanGenericSignature();
  // Otherwise, return the canonical signature of the underlying pointer.
  return getPointer()->getCanonicalSignature();
}

CanGenericSignature GenericSignatureImpl::getCanonicalSignature() const {
  // If we haven't computed the canonical signature yet, do so now.
  if (CanonicalSignatureOrASTContext.isNull()) {
    // Compute the canonical signature.
    auto canSig = CanGenericSignature::getCanonical(getGenericParams(),
                                                    getRequirements());

    // Record either the canonical signature or an indication that
    // this is the canonical signature.
    if (canSig.getPointer() != this)
      CanonicalSignatureOrASTContext = canSig.getPointer();
    else
      CanonicalSignatureOrASTContext = &getGenericParams()[0]->getASTContext();

    // Return the canonical signature.
    return canSig;
  }

  // A stored ASTContext indicates that this is the canonical
  // signature.
  if (CanonicalSignatureOrASTContext.is<ASTContext *>())
    return CanGenericSignature(this);

  // Otherwise, return the stored canonical signature.
  return CanGenericSignature(
      CanonicalSignatureOrASTContext.get<const GenericSignatureImpl *>());
}

GenericEnvironment *GenericSignature::getGenericEnvironment() const {
  if (isNull())
    return nullptr;
  return getPointer()->getGenericEnvironment();
}

GenericEnvironment *GenericSignatureImpl::getGenericEnvironment() const {
  if (GenericEnv == nullptr) {
    const auto impl = const_cast<GenericSignatureImpl *>(this);
    impl->GenericEnv = GenericEnvironment::getIncomplete(this);
  }

  return GenericEnv;
}

GenericSignature::LocalRequirements
GenericSignatureImpl::getLocalRequirements(Type depType) const {
  assert(depType->isTypeParameter() && "Expected a type parameter here");

  auto computeViaGSB = [&]() {
    GenericSignature::LocalRequirements result;

    auto &builder = *getGenericSignatureBuilder();

    auto resolved =
      builder.maybeResolveEquivalenceClass(
                                    depType,
                                    ArchetypeResolutionKind::CompleteWellFormed,
                                    /*wantExactPotentialArchetype=*/false);
    if (!resolved) {
      result.concreteType = ErrorType::get(depType);
      return result;
    }

    if (auto concreteType = resolved.getAsConcreteType()) {
      result.concreteType = concreteType;
      return result;
    }

    auto *equivClass = resolved.getEquivalenceClass(builder);

    auto genericParams = getGenericParams();
    result.anchor = equivClass->getAnchor(builder, genericParams);

    if (equivClass->concreteType) {
      result.concreteType = equivClass->concreteType;
      return result;
    }

    result.superclass = equivClass->superclass;

    for (const auto &conforms : equivClass->conformsTo) {
      auto proto = conforms.first;

      if (!equivClass->isConformanceSatisfiedBySuperclass(proto))
        result.protos.push_back(proto);
    }

    result.layout = equivClass->layout;

    return result;
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->getLocalRequirements(depType, getGenericParams());
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    auto typesEqual = [&](Type lhs, Type rhs, bool canonical) {
      if (!lhs || !rhs)
        return !lhs == !rhs;
      if (lhs->isEqual(rhs))
        return true;

      if (canonical)
        return false;

      if (getCanonicalTypeInContext(lhs) ==
          getCanonicalTypeInContext(rhs))
        return true;

      return false;
    };

    auto compare = [&]() {
      // If the types are concrete, we don't care about the rest.
      if (gsbResult.concreteType || rqmResult.concreteType) {
        if (!typesEqual(gsbResult.concreteType,
                        rqmResult.concreteType,
                        false))
          return false;

        return true;
      }

      if (!typesEqual(gsbResult.anchor,
                      rqmResult.anchor,
                      true))
        return false;

      if (gsbResult.layout != rqmResult.layout)
        return false;

      auto lhsProtos = gsbResult.protos;
      ProtocolType::canonicalizeProtocols(lhsProtos);
      auto rhsProtos = rqmResult.protos;
      ProtocolType::canonicalizeProtocols(rhsProtos);

      if (lhsProtos != rhsProtos)
        return false;

      if (!typesEqual(gsbResult.superclass,
                      rqmResult.superclass,
                      false))
        return false;

      return true;
    };

    auto dumpReqs = [&](const GenericSignature::LocalRequirements &reqs) {
      if (reqs.anchor) {
        llvm::errs() << "- Anchor: " << reqs.anchor << "\n";
        reqs.anchor.dump(llvm::errs());
      }
      if (reqs.concreteType) {
        llvm::errs() << "- Concrete type: " << reqs.concreteType << "\n";
        reqs.concreteType.dump(llvm::errs());
      }
      if (reqs.superclass) {
        llvm::errs() << "- Superclass: " << reqs.superclass << "\n";
        reqs.superclass.dump(llvm::errs());
      }
      if (reqs.layout) {
        llvm::errs() << "- Layout: " << reqs.layout << "\n";
      }
      for (const auto *proto : reqs.protos) {
        llvm::errs() << "- Conforms to: " << proto->getName() << "\n";
      }
    };

    if (!compare()) {
      llvm::errs() << "RequirementMachine::getLocalRequirements() is broken\n";
      llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
      llvm::errs() << "Dependent type: "; depType.dump(llvm::errs());
      llvm::errs() << "GenericSignatureBuilder says:\n";
      dumpReqs(gsbResult);
      llvm::errs() << "\n";
      llvm::errs() << "RequirementMachine says:\n";
      dumpReqs(rqmResult);
      llvm::errs() << "\n";
      getRequirementMachine()->dump(llvm::errs());
      abort();
    }

    return rqmResult;
  }
  }
}

ASTContext &GenericSignatureImpl::getASTContext() const {
  // Canonical signatures store the ASTContext directly.
  if (auto ctx = CanonicalSignatureOrASTContext.dyn_cast<ASTContext *>())
    return *ctx;

  // For everything else, just get it from the generic parameter.
  return GenericSignature::getASTContext(getGenericParams(), getRequirements());
}

ProtocolConformanceRef
GenericSignatureImpl::lookupConformance(CanType type,
                                        ProtocolDecl *proto) const {
  // FIXME: Actually implement this properly.
  auto *M = proto->getParentModule();

  if (type->isTypeParameter())
    return ProtocolConformanceRef(proto);

  return M->lookupConformance(type, proto, /*allowMissing=*/true);
}

bool GenericSignatureImpl::requiresClass(Type type) const {
  assert(type->isTypeParameter() &&
         "Only type parameters can have superclass requirements");

  auto computeViaGSB = [&]() {
    auto &builder = *getGenericSignatureBuilder();
    auto equivClass =
      builder.resolveEquivalenceClass(
                                    type,
                                    ArchetypeResolutionKind::CompleteWellFormed);
    if (!equivClass) return false;

    // If this type was mapped to a concrete type, then there is no
    // requirement.
    if (equivClass->concreteType) return false;

    // If there is a layout constraint, it might be a class.
    if (equivClass->layout && equivClass->layout->isClass()) return true;

    return false;
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->requiresClass(type);
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    if (gsbResult != rqmResult) {
      llvm::errs() << "RequirementMachine::requiresClass() is broken\n";
      llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
      llvm::errs() << "Dependent type: "; type.dump(llvm::errs());
      llvm::errs() << "GenericSignatureBuilder says: " << gsbResult << "\n";
      llvm::errs() << "RequirementMachine says: " << rqmResult << "\n";
      getRequirementMachine()->dump(llvm::errs());
      abort();
    }

    return rqmResult;
  }
  }
}

/// Determine the superclass bound on the given dependent type.
Type GenericSignatureImpl::getSuperclassBound(Type type) const {
  assert(type->isTypeParameter() &&
         "Only type parameters can have superclass requirements");

  auto computeViaGSB = [&]() -> Type {
    auto &builder = *getGenericSignatureBuilder();
    auto equivClass =
    builder.resolveEquivalenceClass(
                                  type,
                                  ArchetypeResolutionKind::CompleteWellFormed);
    if (!equivClass) return nullptr;

    // If this type was mapped to a concrete type, then there is no
    // requirement.
    if (equivClass->concreteType) return nullptr;

    // Retrieve the superclass bound.
    return equivClass->superclass;
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->getSuperclassBound(type, getGenericParams());
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    auto check = [&]() {
      if (!gsbResult || !rqmResult)
        return !gsbResult == !rqmResult;
      return gsbResult->isEqual(rqmResult);
    };

    if (!check()) {
      llvm::errs() << "RequirementMachine::getSuperclassBound() is broken\n";
      llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
      llvm::errs() << "Dependent type: "; type.dump(llvm::errs());
      llvm::errs() << "GenericSignatureBuilder says: " << gsbResult << "\n";
      if (gsbResult)
        gsbResult.dump(llvm::errs());
      llvm::errs() << "\n";
      llvm::errs() << "RequirementMachine says: " << rqmResult << "\n";
      if (rqmResult)
        rqmResult.dump(llvm::errs());
      llvm::errs() << "\n";
      getRequirementMachine()->dump(llvm::errs());
      abort();
    }

    return rqmResult;
  }
  }
}

/// Determine the set of protocols to which the given type parameter is
/// required to conform.
GenericSignature::RequiredProtocols
GenericSignatureImpl::getRequiredProtocols(Type type) const {
  assert(type->isTypeParameter() && "Expected a type parameter");

  auto computeViaGSB = [&]() -> GenericSignature::RequiredProtocols {
    auto &builder = *getGenericSignatureBuilder();
    auto equivClass =
      builder.resolveEquivalenceClass(
                                    type,
                                    ArchetypeResolutionKind::CompleteWellFormed);
    if (!equivClass) return { };

    // If this type parameter was mapped to a concrete type, then there
    // are no requirements.
    if (equivClass->concreteType) return { };

    // Retrieve the protocols to which this type conforms.
    GenericSignature::RequiredProtocols result;
    for (const auto &conforms : equivClass->conformsTo)
      result.push_back(conforms.first);

    // Canonicalize the resulting set of protocols.
    ProtocolType::canonicalizeProtocols(result);

    return result;
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->getRequiredProtocols(type);
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    if (gsbResult != rqmResult) {
      llvm::errs() << "RequirementMachine::getRequiredProtocols() is broken\n";
      llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
      llvm::errs() << "Dependent type: "; type.dump(llvm::errs());
      llvm::errs() << "GenericSignatureBuilder says: ";
      for (auto *otherProto : gsbResult)
        llvm::errs() << " " << otherProto->getName();
      llvm::errs() << "\n";
      llvm::errs() << "RequirementMachine says: ";
      for (auto *otherProto : rqmResult)
        llvm::errs() << " " << otherProto->getName();
      llvm::errs() << "\n";
      getRequirementMachine()->dump(llvm::errs());
      abort();
    }

    return rqmResult;
  }
  }
}

bool GenericSignatureImpl::requiresProtocol(Type type,
                                            ProtocolDecl *proto) const {
  assert(type->isTypeParameter() && "Expected a type parameter");

  auto computeViaGSB = [&]() {
    auto &builder = *getGenericSignatureBuilder();
    auto equivClass =
      builder.resolveEquivalenceClass(
                                    type,
                                    ArchetypeResolutionKind::CompleteWellFormed);
    if (!equivClass) return false;

    // FIXME: Optionally deal with concrete conformances here
    // or have a separate method do that additionally?
    //
    // If this type parameter was mapped to a concrete type, then there
    // are no requirements.
    if (equivClass->concreteType) return false;

    // Check whether the representative conforms to this protocol.
    return equivClass->conformsTo.count(proto) > 0;
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->requiresProtocol(type, proto);
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    if (gsbResult != rqmResult) {
      llvm::errs() << "RequirementMachine::requiresProtocol() is broken\n";
      llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
      llvm::errs() << "Dependent type: "; type.dump(llvm::errs());
      llvm::errs() << "Protocol: "; proto->dumpRef(llvm::errs());
      llvm::errs() << "\n";
      llvm::errs() << "GenericSignatureBuilder says: " << gsbResult << "\n";
      llvm::errs() << "RequirementMachine says: " << rqmResult << "\n";
      getRequirementMachine()->dump(llvm::errs());
      abort();
    }

    return rqmResult;
  }
  }
}

/// Determine whether the given dependent type is equal to a concrete type.
bool GenericSignatureImpl::isConcreteType(Type type) const {
  assert(type->isTypeParameter() && "Expected a type parameter");

  auto computeViaGSB = [&]() {
    auto &builder = *getGenericSignatureBuilder();
    auto equivClass =
      builder.resolveEquivalenceClass(
                                    type,
                                    ArchetypeResolutionKind::CompleteWellFormed);
    if (!equivClass) return false;

    return bool(equivClass->concreteType);
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->isConcreteType(type);
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    if (gsbResult != rqmResult) {
      llvm::errs() << "RequirementMachine::isConcreteType() is broken\n";
      llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
      llvm::errs() << "Dependent type: "; type.dump(llvm::errs());
      llvm::errs() << "\n";
      llvm::errs() << "GenericSignatureBuilder says: " << gsbResult << "\n";
      llvm::errs() << "RequirementMachine says: " << rqmResult << "\n";
      getRequirementMachine()->dump(llvm::errs());
      abort();
    }

    return rqmResult;
  }
  }
}

/// Return the concrete type that the given type parameter is constrained to,
/// or the null Type if it is not the subject of a concrete same-type
/// constraint.
Type GenericSignatureImpl::getConcreteType(Type type) const {
  assert(type->isTypeParameter() && "Expected a type parameter");

  auto computeViaGSB = [&]() -> Type {
    auto &builder = *getGenericSignatureBuilder();
    auto equivClass =
    builder.resolveEquivalenceClass(
                                  type,
                                  ArchetypeResolutionKind::CompleteWellFormed);
    if (!equivClass) return nullptr;

    return equivClass->concreteType;
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->getConcreteType(type, getGenericParams());
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    auto check = [&]() {
      if (!gsbResult || !rqmResult)
        return !gsbResult == !rqmResult;
      if (gsbResult->isEqual(rqmResult))
        return true;

      return (getCanonicalTypeInContext(gsbResult)
              == getCanonicalTypeInContext(rqmResult));
    };

    if (!check()) {
      llvm::errs() << "RequirementMachine::getConcreteType() is broken\n";
      llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
      llvm::errs() << "Dependent type: "; type.dump(llvm::errs());
      llvm::errs() << "GenericSignatureBuilder says: " << gsbResult << "\n";
      if (gsbResult)
        gsbResult.dump(llvm::errs());
      llvm::errs() << "\n";
      llvm::errs() << "RequirementMachine says: " << rqmResult << "\n";
      if (rqmResult)
        rqmResult.dump(llvm::errs());
      llvm::errs() << "\n";
      getRequirementMachine()->dump(llvm::errs());
      abort();
    }

    return rqmResult;
  }
  }
}

LayoutConstraint GenericSignatureImpl::getLayoutConstraint(Type type) const {
  assert(type->isTypeParameter() &&
         "Only type parameters can have layout constraints");

  auto computeViaGSB = [&]() {
    auto &builder = *getGenericSignatureBuilder();
    auto equivClass =
      builder.resolveEquivalenceClass(
                                    type,
                                    ArchetypeResolutionKind::CompleteWellFormed);
    if (!equivClass) return LayoutConstraint();

    return equivClass->layout;
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->getLayoutConstraint(type);
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    if (gsbResult != rqmResult) {
      llvm::errs() << "RequirementMachine::getLayoutConstraint() is broken\n";
      llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
      llvm::errs() << "Dependent type: "; type.dump(llvm::errs());
      llvm::errs() << "\n";
      llvm::errs() << "GenericSignatureBuilder says: " << gsbResult << "\n";
      llvm::errs() << "RequirementMachine says: " << rqmResult << "\n";
      getRequirementMachine()->dump(llvm::errs());
      abort();
    }

    return rqmResult;
  }
  }
}

bool GenericSignatureImpl::areSameTypeParameterInContext(Type type1,
                                                         Type type2) const {
  assert(type1->isTypeParameter());
  assert(type2->isTypeParameter());

  if (type1.getPointer() == type2.getPointer())
    return true;

  auto computeViaGSB = [&]() {
    return areSameTypeParameterInContext(type1, type2,
                                         *getGenericSignatureBuilder());
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->areSameTypeParameterInContext(type1, type2);
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    if (gsbResult != rqmResult) {
      auto firstConcreteType = getConcreteType(type1);
      auto secondConcreteType = getConcreteType(type2);
      if (!firstConcreteType->isEqual(secondConcreteType)) {
        llvm::errs() << "RequirementMachine::areSameTypeParameterInContext() is broken\n";
        llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
        llvm::errs() << "First dependent type: "; type1.dump(llvm::errs());
        llvm::errs() << "Second dependent type: "; type2.dump(llvm::errs());
        llvm::errs() << "\n";
        llvm::errs() << "GenericSignatureBuilder says: " << gsbResult << "\n";
        llvm::errs() << "RequirementMachine says: " << rqmResult << "\n";
        getRequirementMachine()->dump(llvm::errs());
        abort();
      }
    }

    return rqmResult;
  }
  }
}

bool GenericSignatureImpl::areSameTypeParameterInContext(Type type1,
                                                         Type type2,
                                                         GenericSignatureBuilder &builder) const {
  assert(type1->isTypeParameter());
  assert(type2->isTypeParameter());

  if (type1.getPointer() == type2.getPointer())
    return true;

  auto equivClass1 =
    builder.resolveEquivalenceClass(
                             type1,
                             ArchetypeResolutionKind::CompleteWellFormed);
  assert(equivClass1 && "not a valid dependent type of this signature?");

  auto equivClass2 =
    builder.resolveEquivalenceClass(
                             type2,
                             ArchetypeResolutionKind::CompleteWellFormed);
  assert(equivClass2 && "not a valid dependent type of this signature?");

  return equivClass1 == equivClass2;
}

bool GenericSignatureImpl::isRequirementSatisfied(
    Requirement requirement, bool allowMissing) const {
  if (requirement.getFirstType()->hasTypeParameter()) {
    auto *genericEnv = getGenericEnvironment();

    auto substituted = requirement.subst(
        [&](SubstitutableType *type) -> Type {
          if (auto *paramType = type->getAs<GenericTypeParamType>())
            return genericEnv->mapTypeIntoContext(paramType);

          return type;
        },
        LookUpConformanceInSignature(this));

    if (!substituted)
      return false;

    requirement = *substituted;
  }

  // FIXME: Need to check conditional requirements here.
  ArrayRef<Requirement> conditionalRequirements;

  return requirement.isSatisfied(conditionalRequirements, allowMissing);
}

SmallVector<Requirement, 4>
GenericSignature::requirementsNotSatisfiedBy(GenericSignature otherSig) const {
  // The null generic signature has no requirements, therefore all requirements
  // are satisfied by any signature.
  if (isNull()) {
    return {};
  }
  return getPointer()->requirementsNotSatisfiedBy(otherSig);
}

SmallVector<Requirement, 4> GenericSignatureImpl::requirementsNotSatisfiedBy(
                                            GenericSignature otherSig) const {
  SmallVector<Requirement, 4> result;

  // If the signatures match by pointer, all requirements are satisfied.
  if (otherSig.getPointer() == this) return result;

  // If there is no other signature, no requirements are satisfied.
  if (!otherSig) {
    const auto reqs = getRequirements();
    result.append(reqs.begin(), reqs.end());
    return result;
  }

  // If the canonical signatures are equal, all requirements are satisfied.
  if (getCanonicalSignature() == otherSig->getCanonicalSignature())
    return result;

  // Find the requirements that aren't satisfied.
  for (const auto &req : getRequirements()) {
    if (!otherSig->isRequirementSatisfied(req))
      result.push_back(req);
  }

  return result;
}

bool GenericSignatureImpl::isCanonicalTypeInContext(Type type) const {
  // If the type isn't independently canonical, it's certainly not canonical
  // in this context.
  if (!type->isCanonical())
    return false;

  // All the contextual canonicality rules apply to type parameters, so if the
  // type doesn't involve any type parameters, it's already canonical.
  if (!type->hasTypeParameter())
    return true;

  auto computeViaGSB = [&]() {
    auto &builder = *getGenericSignatureBuilder();
    return isCanonicalTypeInContext(type, builder);
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->isCanonicalTypeInContext(type);
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    if (gsbResult != rqmResult) {
      llvm::errs() << "RequirementMachine::isCanonicalTypeInContext() is broken\n";
      llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
      llvm::errs() << "Dependent type: "; type.dump(llvm::errs());
      llvm::errs() << "\n";
      llvm::errs() << "GenericSignatureBuilder says: " << gsbResult << "\n";
      llvm::errs() << "RequirementMachine says: " << rqmResult << "\n";
      getRequirementMachine()->dump(llvm::errs());
      abort();
    }

    return rqmResult;
  }
  }
}

bool GenericSignatureImpl::isCanonicalTypeInContext(
    Type type, GenericSignatureBuilder &builder) const {
  // If the type isn't independently canonical, it's certainly not canonical
  // in this context.
  if (!type->isCanonical())
    return false;

  // All the contextual canonicality rules apply to type parameters, so if the
  // type doesn't involve any type parameters, it's already canonical.
  if (!type->hasTypeParameter())
    return true;

  // Look for non-canonical type parameters.
  return !type.findIf([&](Type component) -> bool {
    if (!component->isTypeParameter()) return false;

    auto equivClass =
      builder.resolveEquivalenceClass(
                               Type(component),
                               ArchetypeResolutionKind::CompleteWellFormed);
    if (!equivClass) return false;

    return (equivClass->concreteType ||
            !component->isEqual(equivClass->getAnchor(builder,
                                                      getGenericParams())));
  });
}

CanType GenericSignature::getCanonicalTypeInContext(Type type) const {
  // The null generic signature has no requirements so cannot influence the
  // structure of the can type computed here.
  if (isNull()) {
    return type->getCanonicalType();
  }
  return getPointer()->getCanonicalTypeInContext(type);
}

CanType GenericSignatureImpl::getCanonicalTypeInContext(Type type) const {
  type = type->getCanonicalType();

  // All the contextual canonicality rules apply to type parameters, so if the
  // type doesn't involve any type parameters, it's already canonical.
  if (!type->hasTypeParameter())
    return CanType(type);

  auto computeViaGSB = [&]() {
    auto &builder = *getGenericSignatureBuilder();
    return builder.getCanonicalTypeInContext(type, { })->getCanonicalType();
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->getCanonicalTypeInContext(type, { })->getCanonicalType();
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    if (gsbResult != rqmResult) {
      llvm::errs() << "RequirementMachine::getCanonicalTypeInContext() is broken\n";
      llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
      llvm::errs() << "Dependent type: "; type.dump(llvm::errs());
      llvm::errs() << "GenericSignatureBuilder says: " << gsbResult << "\n";
      gsbResult.dump(llvm::errs());
      llvm::errs() << "RequirementMachine says: " << rqmResult << "\n";
      rqmResult.dump(llvm::errs());
      llvm::errs() << "\n";
      getRequirementMachine()->dump(llvm::errs());
      abort();
    }

    return rqmResult;
  }
  }
}

ArrayRef<CanTypeWrapper<GenericTypeParamType>>
CanGenericSignature::getGenericParams() const {
  auto params = getPointer()->getGenericParams().getOriginalArray();
  auto base = static_cast<const CanTypeWrapper<GenericTypeParamType>*>(
                                                              params.data());
  return {base, params.size()};
}

ConformanceAccessPath
GenericSignatureImpl::getConformanceAccessPath(Type type,
                                               ProtocolDecl *protocol) const {
  auto computeViaGSB = [&]() {
    return getGenericSignatureBuilder()->getConformanceAccessPath(
        type, protocol, this);
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->getConformanceAccessPath(type, protocol);
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    auto compare = [&]() {
      if (gsbResult.size() != rqmResult.size())
        return false;

      auto *begin1 = gsbResult.begin();
      auto *end1 = gsbResult.end();
      auto *begin2 = rqmResult.begin();
      auto *end2 = rqmResult.end();

      while (begin1 < end1) {
        assert(begin2 < end2);

        if (!begin1->first->isEqual(begin2->first))
          return false;
        if (begin1->second != begin2->second)
          return false;

        ++begin1;
        ++begin2;
      }

      return true;
    };

    if (!compare()) {
      llvm::errs() << "RequirementMachine::getConformanceAccessPath() is broken\n";
      llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
      llvm::errs() << "Dependent type: "; type.dump(llvm::errs());
      llvm::errs() << "Protocol: "; protocol->dumpRef(llvm::errs());
      llvm::errs() << "\n";
      llvm::errs() << "GenericSignatureBuilder says: ";
      gsbResult.print(llvm::errs());
      llvm::errs() << "\n";
      llvm::errs() << "RequirementMachine says: ";
      rqmResult.print(llvm::errs());
      llvm::errs() << "\n\n";
      getRequirementMachine()->dump(llvm::errs());
      abort();
    }

    return rqmResult;
  }
  }
}

TypeDecl *
GenericSignatureImpl::lookupNestedType(Type type, Identifier name) const {
  assert(type->isTypeParameter());

  auto computeViaGSB = [&]() -> TypeDecl * {
    auto *builder = getGenericSignatureBuilder();
    auto equivClass =
      builder->resolveEquivalenceClass(
                                  type,
                                  ArchetypeResolutionKind::CompleteWellFormed);
    if (!equivClass)
      return nullptr;

    return equivClass->lookupNestedType(*builder, name);
  };

  auto computeViaRQM = [&]() {
    auto *machine = getRequirementMachine();
    return machine->lookupNestedType(type, name);
  };

  auto &ctx = getASTContext();
  switch (ctx.LangOpts.EnableRequirementMachine) {
  case RequirementMachineMode::Disabled:
    return computeViaGSB();

  case RequirementMachineMode::Enabled:
    return computeViaRQM();

  case RequirementMachineMode::Verify: {
    auto rqmResult = computeViaRQM();
    auto gsbResult = computeViaGSB();

    if (gsbResult != rqmResult) {
      llvm::errs() << "RequirementMachine::lookupNestedType() is broken\n";
      llvm::errs() << "Generic signature: " << GenericSignature(this) << "\n";
      llvm::errs() << "Dependent type: "; type.dump(llvm::errs());
      llvm::errs() << "GenericSignatureBuilder says: ";
      if (gsbResult)
        gsbResult->dumpRef(llvm::errs());
      else
        llvm::errs() << "<nullptr>";
      llvm::errs() << "\n";
      llvm::errs() << "RequirementMachine says: ";
      if (rqmResult)
        rqmResult->dumpRef(llvm::errs());
      else
        llvm::errs() << "<nullptr>";
      llvm::errs() << "\n";
      getRequirementMachine()->dump(llvm::errs());
      abort();
    }

    return rqmResult;
  }
  }
}

unsigned GenericParamKey::findIndexIn(
                      TypeArrayView<GenericTypeParamType> genericParams) const {
  // For depth 0, we have random access. We perform the extra checking so that
  // we can return
  if (Depth == 0 && Index < genericParams.size() &&
      genericParams[Index] == *this)
    return Index;

  // At other depths, perform a binary search.
  unsigned result =
      std::lower_bound(genericParams.begin(), genericParams.end(), *this,
                       Ordering())
        - genericParams.begin();
  if (result < genericParams.size() && genericParams[result] == *this)
    return result;

  // We didn't find the parameter we were looking for.
  return genericParams.size();
}

SubstitutionMap GenericSignatureImpl::getIdentitySubstitutionMap() const {
  return SubstitutionMap::get(const_cast<GenericSignatureImpl *>(this),
                              [](SubstitutableType *t) -> Type {
                                return Type(cast<GenericTypeParamType>(t));
                              },
                              MakeAbstractConformanceForGenericType());
}

GenericTypeParamType *GenericSignatureImpl::getSugaredType(
    GenericTypeParamType *type) const {
  unsigned ordinal = getGenericParamOrdinal(type);
  return getGenericParams()[ordinal];
}

Type GenericSignatureImpl::getSugaredType(Type type) const {
  if (!type->hasTypeParameter())
    return type;

  return type.transform([this](Type Ty) -> Type {
    if (auto GP = dyn_cast<GenericTypeParamType>(Ty.getPointer())) {
      return Type(getSugaredType(GP));
    }
    return Ty;
  });
}

unsigned GenericSignatureImpl::getGenericParamOrdinal(
    GenericTypeParamType *param) const {
  return GenericParamKey(param).findIndexIn(getGenericParams());
}

void GenericSignature::Profile(llvm::FoldingSetNodeID &id) const {
  return GenericSignature::Profile(id, getPointer()->getGenericParams(),
                                     getPointer()->getRequirements());
}

void GenericSignature::Profile(llvm::FoldingSetNodeID &ID,
                               TypeArrayView<GenericTypeParamType> genericParams,
                               ArrayRef<Requirement> requirements) {
  return GenericSignatureImpl::Profile(ID, genericParams, requirements);
}

void swift::simple_display(raw_ostream &out, GenericSignature sig) {
  if (sig)
    sig->print(out);
  else
    out << "NULL";
}

bool Requirement::isCanonical() const {
  if (getFirstType() && !getFirstType()->isCanonical())
    return false;

  switch (getKind()) {
  case RequirementKind::Conformance:
  case RequirementKind::SameType:
  case RequirementKind::Superclass:
    if (getSecondType() && !getSecondType()->isCanonical())
      return false;
    break;

  case RequirementKind::Layout:
    break;
  }

  return true;
}

/// Get the canonical form of this requirement.
Requirement Requirement::getCanonical() const {
  Type firstType = getFirstType();
  if (firstType)
    firstType = firstType->getCanonicalType();

  switch (getKind()) {
  case RequirementKind::Conformance:
  case RequirementKind::SameType:
  case RequirementKind::Superclass: {
    Type secondType = getSecondType();
    if (secondType)
      secondType = secondType->getCanonicalType();
    return Requirement(getKind(), firstType, secondType);
  }

  case RequirementKind::Layout:
    return Requirement(getKind(), firstType, getLayoutConstraint());
  }
  llvm_unreachable("Unhandled RequirementKind in switch");
}

ProtocolDecl *Requirement::getProtocolDecl() const {
  assert(getKind() == RequirementKind::Conformance);
  return getSecondType()->castTo<ProtocolType>()->getDecl();
}

bool
Requirement::isSatisfied(ArrayRef<Requirement> &conditionalRequirements,
                         bool allowMissing) const {
  switch (getKind()) {
  case RequirementKind::Conformance: {
    auto *proto = getProtocolDecl();
    auto *module = proto->getParentModule();
    auto conformance = module->lookupConformance(
        getFirstType(), proto, allowMissing);
    if (!conformance)
      return false;

    conditionalRequirements = conformance.getConditionalRequirements();
    return true;
  }

  case RequirementKind::Layout: {
    if (auto *archetypeType = getFirstType()->getAs<ArchetypeType>()) {
      auto layout = archetypeType->getLayoutConstraint();
      return (layout && layout.merge(getLayoutConstraint()));
    }

    if (getLayoutConstraint()->isClass())
      return getFirstType()->satisfiesClassConstraint();

    // TODO: Statically check other layout constraints, once they can
    // be spelled in Swift.
    return true;
  }

  case RequirementKind::Superclass:
    return getSecondType()->isExactSuperclassOf(getFirstType());

  case RequirementKind::SameType:
    return getFirstType()->isEqual(getSecondType());
  }

  llvm_unreachable("Bad requirement kind");
}

bool Requirement::canBeSatisfied() const {
  switch (getKind()) {
  case RequirementKind::Conformance:
    return getFirstType()->is<ArchetypeType>();

  case RequirementKind::Layout: {
    if (auto *archetypeType = getFirstType()->getAs<ArchetypeType>()) {
      auto layout = archetypeType->getLayoutConstraint();
      return (!layout || layout.merge(getLayoutConstraint()));
    }

    return false;
  }

  case RequirementKind::Superclass:
    return (getFirstType()->isBindableTo(getSecondType()) ||
            getSecondType()->isBindableTo(getFirstType()));

  case RequirementKind::SameType:
    return (getFirstType()->isBindableTo(getSecondType()) ||
            getSecondType()->isBindableTo(getFirstType()));
  }

  llvm_unreachable("Bad requirement kind");
}

/// Determine the canonical ordering of requirements.
static unsigned getRequirementKindOrder(RequirementKind kind) {
  switch (kind) {
  case RequirementKind::Conformance: return 2;
  case RequirementKind::Superclass: return 0;
  case RequirementKind::SameType: return 3;
  case RequirementKind::Layout: return 1;
  }
  llvm_unreachable("unhandled kind");
}

/// Linear order on requirements in a generic signature.
int Requirement::compare(const Requirement &other) const {
  int compareLHS =
    compareDependentTypes(getFirstType(), other.getFirstType());

  if (compareLHS != 0)
    return compareLHS;

  int compareKind = (getRequirementKindOrder(getKind()) -
                     getRequirementKindOrder(other.getKind()));

  if (compareKind != 0)
    return compareKind;

  // We should only have multiple conformance requirements.
  assert(getKind() == RequirementKind::Conformance);

  int compareProtos =
    TypeDecl::compare(getProtocolDecl(), other.getProtocolDecl());
  assert(compareProtos != 0 && "Duplicate conformance requirements");

  return compareProtos;
}

/// Compare two associated types.
int swift::compareAssociatedTypes(AssociatedTypeDecl *assocType1,
                                  AssociatedTypeDecl *assocType2) {
  // - by name.
  if (int result = assocType1->getName().str().compare(
                                              assocType2->getName().str()))
    return result;

  // Prefer an associated type with no overrides (i.e., an anchor) to one
  // that has overrides.
  bool hasOverridden1 = !assocType1->getOverriddenDecls().empty();
  bool hasOverridden2 = !assocType2->getOverriddenDecls().empty();
  if (hasOverridden1 != hasOverridden2)
    return hasOverridden1 ? +1 : -1;

  // - by protocol, so t_n_m.`P.T` < t_n_m.`Q.T` (given P < Q)
  auto proto1 = assocType1->getProtocol();
  auto proto2 = assocType2->getProtocol();
  if (int compareProtocols = TypeDecl::compare(proto1, proto2))
    return compareProtocols;

  // Error case: if we have two associated types with the same name in the
  // same protocol, just tie-break based on source location.
  if (assocType1 != assocType2) {
    auto &ctx = assocType1->getASTContext();
    return ctx.SourceMgr.isBeforeInBuffer(assocType1->getLoc(),
                                          assocType2->getLoc()) ? -1 : +1;
  }

  return 0;
}

/// Canonical ordering for type parameters.
int swift::compareDependentTypes(Type type1, Type type2) {
  // Fast-path check for equality.
  if (type1->isEqual(type2)) return 0;

  // Ordering is as follows:
  // - Generic params
  auto gp1 = type1->getAs<GenericTypeParamType>();
  auto gp2 = type2->getAs<GenericTypeParamType>();
  if (gp1 && gp2)
    return GenericParamKey(gp1) < GenericParamKey(gp2) ? -1 : +1;

  // A generic parameter is always ordered before a nested type.
  if (static_cast<bool>(gp1) != static_cast<bool>(gp2))
    return gp1 ? -1 : +1;

  // - Dependent members
  auto depMemTy1 = type1->castTo<DependentMemberType>();
  auto depMemTy2 = type2->castTo<DependentMemberType>();

  // - by base, so t_0_n.`P.T` < t_1_m.`P.T`
  if (int compareBases =
        compareDependentTypes(depMemTy1->getBase(), depMemTy2->getBase()))
    return compareBases;

  // - by name, so t_n_m.`P.T` < t_n_m.`P.U`
  if (int compareNames = depMemTy1->getName().str().compare(
                                                  depMemTy2->getName().str()))
    return compareNames;

  auto *assocType1 = depMemTy1->getAssocType();
  auto *assocType2 = depMemTy2->getAssocType();
  if (int result = compareAssociatedTypes(assocType1, assocType2))
    return result;

  return 0;
}

#pragma mark Generic signature verification

void GenericSignature::verify() const {
  auto canSig = getCanonicalSignature();

  PrettyStackTraceGenericSignature debugStack("checking", canSig);

  auto canonicalRequirements = canSig.getRequirements();

  // We collect conformance requirements to check that they're minimal.
  llvm::SmallDenseMap<CanType, SmallVector<ProtocolDecl *, 2>, 2> conformances;

  // Check that the requirements satisfy certain invariants.
  for (unsigned idx : indices(canonicalRequirements)) {
    debugStack.setRequirement(idx);

    const auto &reqt = canonicalRequirements[idx];

    // Left-hand side must be a canonical type parameter.
    if (reqt.getKind() != RequirementKind::SameType) {
      if (!reqt.getFirstType()->isTypeParameter()) {
        llvm::errs() << "Left-hand side must be a type parameter: ";
        reqt.dump(llvm::errs());
        llvm::errs() << "\n";
        abort();
      }

      if (!canSig->isCanonicalTypeInContext(reqt.getFirstType())) {
        llvm::errs() << "Left-hand side is not canonical: ";
        reqt.dump(llvm::errs());
        llvm::errs() << "\n";
        abort();
      }
    }

    // Check canonicalization of requirement itself.
    switch (reqt.getKind()) {
    case RequirementKind::Superclass:
      if (!canSig->isCanonicalTypeInContext(reqt.getSecondType())) {
        llvm::errs() << "Right-hand side is not canonical: ";
        reqt.dump(llvm::errs());
        llvm::errs() << "\n";
        abort();
      }
      break;

    case RequirementKind::Layout:
      break;

    case RequirementKind::SameType: {
      auto isCanonicalAnchor = [&](Type type) {
        if (auto *dmt = type->getAs<DependentMemberType>())
          return canSig->isCanonicalTypeInContext(dmt->getBase());
        return type->is<GenericTypeParamType>();
      };

      auto firstType = reqt.getFirstType();
      auto secondType = reqt.getSecondType();
      if (!isCanonicalAnchor(firstType)) {
        llvm::errs() << "Left hand side does not have a canonical parent: ";
        reqt.dump(llvm::errs());
        llvm::errs() << "\n";
        abort();
      }

      if (reqt.getSecondType()->isTypeParameter()) {
        if (!isCanonicalAnchor(secondType)) {
          llvm::errs() << "Right hand side does not have a canonical parent: ";
          reqt.dump(llvm::errs());
          llvm::errs() << "\n";
          abort();
        }
        if (compareDependentTypes(firstType, secondType) >= 0) {
          llvm::errs() << "Out-of-order type parameters: ";
          reqt.dump(llvm::errs());
          llvm::errs() << "\n";
          abort();
        }
      } else {
        if (!canSig->isCanonicalTypeInContext(secondType)) {
          llvm::errs() << "Right hand side is not canonical: ";
          reqt.dump(llvm::errs());
          llvm::errs() << "\n";
          abort();
        }
      }
      break;
    }

    case RequirementKind::Conformance:
      // Collect all conformance requirements on each type parameter.
      conformances[CanType(reqt.getFirstType())].push_back(
          reqt.getProtocolDecl());
      break;
    }

    // From here on, we're only interested in requirements beyond the first.
    if (idx == 0) continue;

    // Make sure that the left-hand sides are in nondecreasing order.
    const auto &prevReqt = canonicalRequirements[idx-1];
    int compareLHS =
      compareDependentTypes(prevReqt.getFirstType(), reqt.getFirstType());
    if (compareLHS > 0) {
      llvm::errs() << "Out-of-order left-hand side: ";
      reqt.dump(llvm::errs());
      llvm::errs() << "\n";
      abort();
    }

    // If we have two same-type requirements where the left-hand sides differ
    // but fall into the same equivalence class, we can check the form.
    if (compareLHS < 0 && reqt.getKind() == RequirementKind::SameType &&
        prevReqt.getKind() == RequirementKind::SameType &&
        canSig->areSameTypeParameterInContext(prevReqt.getFirstType(),
                                              reqt.getFirstType())) {
      // If it's a it's a type parameter, make sure the equivalence class is
      // wired together sanely.
      if (prevReqt.getSecondType()->isTypeParameter()) {
        if (!prevReqt.getSecondType()->isEqual(reqt.getFirstType())) {
          llvm::errs() << "Same-type requirement within an equiv. class "
                       << "is out-of-order: ";
          reqt.dump(llvm::errs());
          llvm::errs() << "\n";
          abort();
        }
      } else {
        // Otherwise, the concrete types must match up.
        if (!prevReqt.getSecondType()->isEqual(reqt.getSecondType())) {
          llvm::errs() << "Inconsistent concrete requirement in equiv. class: ";
          reqt.dump(llvm::errs());
          llvm::errs() << "\n";
          abort();
        }
      }
    }

    // If we have a concrete same-type requirement, we shouldn't have any
    // other requirements on the same type.
    if (reqt.getKind() == RequirementKind::SameType &&
        !reqt.getSecondType()->isTypeParameter()) {
      if (compareLHS >= 0) {
        llvm::errs() << "Concrete subject type should not have "
                     << "any other requirements: ";
        reqt.dump(llvm::errs());
        llvm::errs() << "\n";
        abort();
      }
    }

    if (prevReqt.compare(reqt) >= 0) {
      llvm::errs() << "Out-of-order requirement: ";
      reqt.dump(llvm::errs());
      llvm::errs() << "\n";
      abort();
    }
  }

  // Make sure we don't have redundant protocol conformance requirements.
  for (auto pair : conformances) {
    const auto &protos = pair.second;
    auto canonicalProtos = protos;

    // canonicalizeProtocols() will sort them and filter out any protocols that
    // are refined by other protocols in the list. It should be a no-op at this
    // point.
    ProtocolType::canonicalizeProtocols(canonicalProtos);

    if (protos.size() != canonicalProtos.size()) {
      llvm::errs() << "Redundant conformance requirements in signature\n";
      abort();
    }
    if (!std::equal(protos.begin(), protos.end(), canonicalProtos.begin())) {
      llvm::errs() << "Out-of-order conformance requirements\n";
      abort();
    }
  }
}

void swift::validateGenericSignature(ASTContext &context,
                                     GenericSignature sig) {
  llvm::errs() << "Validating generic signature: ";
  sig->print(llvm::errs());
  llvm::errs() << "\n";

  // Try building a new signature having the same requirements.
  SmallVector<GenericTypeParamType *, 2> genericParams;
  for (auto *genericParam :  sig.getGenericParams())
    genericParams.push_back(genericParam);

  SmallVector<Requirement, 2> requirements;
  for (auto requirement : sig.getRequirements())
    requirements.push_back(requirement);

  {
    PrettyStackTraceGenericSignature debugStack("verifying", sig);

    auto newSigWithError = evaluateOrDefault(
        context.evaluator,
        AbstractGenericSignatureRequest{
            nullptr,
            genericParams,
            requirements},
        GenericSignatureWithError());

    // If there were any errors, the signature was invalid.
    if (newSigWithError.getInt()) {
      context.Diags.diagnose(SourceLoc(), diag::generic_signature_not_valid,
                             sig->getAsString());
    }

    auto newSig = newSigWithError.getPointer();

    // The new signature should be equal.
    if (!newSig->isEqual(sig)) {
      context.Diags.diagnose(SourceLoc(), diag::generic_signature_not_equal,
                             sig->getAsString(), newSig->getAsString());
    }
  }

  // Try removing each requirement in turn.
  for (unsigned victimIndex : indices(requirements)) {
    PrettyStackTraceGenericSignature debugStack("verifying", sig, victimIndex);

    // Add the requirements *except* the victim.
    SmallVector<Requirement, 2> newRequirements;
    for (unsigned i : indices(requirements)) {
      if (i != victimIndex)
        newRequirements.push_back(requirements[i]);
    }

    auto newSigWithError = evaluateOrDefault(
        context.evaluator,
        AbstractGenericSignatureRequest{
          nullptr,
          genericParams,
          newRequirements},
        GenericSignatureWithError());

    // If there were any errors, we formed an invalid signature, so
    // just continue.
    if (newSigWithError.getInt())
      continue;

    auto newSig = newSigWithError.getPointer();

    // If the new signature once again contains the removed requirement, it's
    // not redundant.
    if (newSig->isEqual(sig))
      continue;

    // If the removed requirement is satisfied by the new generic signature,
    // it is redundant. Complain.
    if (newSig->isRequirementSatisfied(requirements[victimIndex])) {
      SmallString<32> reqString;
      {
        llvm::raw_svector_ostream out(reqString);
        requirements[victimIndex].print(out, PrintOptions());
      }
      context.Diags.diagnose(SourceLoc(), diag::generic_signature_not_minimal,
                             reqString, sig->getAsString());
    }
  }
}

void swift::validateGenericSignaturesInModule(ModuleDecl *module) {
  LoadedFile *loadedFile = nullptr;
  for (auto fileUnit : module->getFiles()) {
    loadedFile = dyn_cast<LoadedFile>(fileUnit);
    if (loadedFile) break;
  }

  if (!loadedFile) return;

  // Check all of the (canonical) generic signatures.
  SmallVector<GenericSignature, 8> allGenericSignatures;
  SmallPtrSet<CanGenericSignature, 4> knownGenericSignatures;
  (void)loadedFile->getAllGenericSignatures(allGenericSignatures);
  ASTContext &context = module->getASTContext();
  for (auto genericSig : allGenericSignatures) {
    // Check whether this is the first time we've checked this (canonical)
    // signature.
    auto canGenericSig = genericSig.getCanonicalSignature();
    if (!knownGenericSignatures.insert(canGenericSig).second) continue;

    validateGenericSignature(context, canGenericSig);
  }
}

GenericSignature
swift::buildGenericSignature(ASTContext &ctx,
                             GenericSignature baseSignature,
                             SmallVector<GenericTypeParamType *, 2> addedParameters,
                             SmallVector<Requirement, 2> addedRequirements) {
  return evaluateOrDefault(
      ctx.evaluator,
      AbstractGenericSignatureRequest{
        baseSignature.getPointer(),
        addedParameters,
        addedRequirements},
      GenericSignatureWithError()).getPointer();
}
