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
#include "swift/AST/GenericSignatureBuilder.h"
#include "swift/AST/Decl.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Module.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/Types.h"
#include "swift/Basic/STLExtras.h"
#include <functional>

using namespace swift;

void ConformanceAccessPath::print(raw_ostream &out) const {
  interleave(begin(), end(),
             [&](const Entry &entry) {
               entry.first.print(out);
               out << ": " << entry.second->getName();
             }, [&] {
               out << " -> ";
             });
}

void ConformanceAccessPath::dump() const {
  print(llvm::errs());
  llvm::errs() << "\n";
}

GenericSignature::GenericSignature(ArrayRef<GenericTypeParamType *> params,
                                   ArrayRef<Requirement> requirements,
                                   bool isKnownCanonical)
  : NumGenericParams(params.size()), NumRequirements(requirements.size()),
    CanonicalSignatureOrASTContext()
{
  auto paramsBuffer = getGenericParamsBuffer();
  for (unsigned i = 0; i < NumGenericParams; ++i) {
    paramsBuffer[i] = params[i];
  }

  auto reqtsBuffer = getRequirementsBuffer();
  for (unsigned i = 0; i < NumRequirements; ++i) {
    reqtsBuffer[i] = requirements[i];
  }

#ifndef NDEBUG
  // Make sure generic parameters are in the right order, and
  // none are missing.
  unsigned depth = 0;
  unsigned count = 0;
  for (auto param : params) {
    if (param->getDepth() != depth) {
      assert(param->getDepth() > depth &&
             "Generic parameter depth mismatch");
      depth = param->getDepth();
      count = 0;
    }
    assert(param->getIndex() == count &&
           "Generic parameter index mismatch");
    count++;
  }
#endif

  if (isKnownCanonical)
    CanonicalSignatureOrASTContext = &getASTContext(params, requirements);
}

ArrayRef<GenericTypeParamType *> 
GenericSignature::getInnermostGenericParams() const {
  auto params = getGenericParams();

  // Find the point at which the depth changes.
  unsigned depth = params.back()->getDepth();
  for (unsigned n = params.size(); n > 0; --n) {
    if (params[n-1]->getDepth() != depth) {
      return params.slice(n);
    }
  }

  // All parameters are at the same depth.
  return params;
}


SmallVector<GenericTypeParamType *, 2>
GenericSignature::getSubstitutableParams() const {
  SmallVector<GenericTypeParamType *, 2> result;

  enumeratePairedRequirements([&](Type depTy, ArrayRef<Requirement>) -> bool {
    if (auto *paramTy = depTy->getAs<GenericTypeParamType>())
      result.push_back(paramTy);
    return false;
  });

  return result;
}

ASTContext &GenericSignature::getASTContext(
                                ArrayRef<swift::GenericTypeParamType *> params,
                                ArrayRef<swift::Requirement> requirements) {
  // The params and requirements cannot both be empty.
  if (!params.empty())
    return params.front()->getASTContext();
  else
    return requirements.front().getFirstType()->getASTContext();
}

GenericSignatureBuilder *GenericSignature::getGenericSignatureBuilder() {
  // The generic signature builder is associated with the canonical signature.
  if (!isCanonical())
    return getCanonicalSignature()->getGenericSignatureBuilder();

  // generic signature builders are stored on the ASTContext.
  return getASTContext().getOrCreateGenericSignatureBuilder(
                                             CanGenericSignature(this));
}

bool GenericSignature::isCanonical() const {
  if (CanonicalSignatureOrASTContext.is<ASTContext*>()) return true;

  return getCanonicalSignature() == this;
}

#ifndef NDEBUG
/// Determine the canonical ordering of requirements.
static unsigned getRequirementKindOrder(RequirementKind kind) {
  switch (kind) {
  case RequirementKind::Conformance: return 2;
  case RequirementKind::Superclass: return 0;
  case RequirementKind::SameType: return 3;
  case RequirementKind::Layout: return 1;
  }
}
#endif

CanGenericSignature GenericSignature::getCanonical(
                                        ArrayRef<GenericTypeParamType *> params,
                                        ArrayRef<Requirement> requirements,
                                        bool skipValidation) {
  // Canonicalize the parameters and requirements.
  SmallVector<GenericTypeParamType*, 8> canonicalParams;
  canonicalParams.reserve(params.size());
  for (auto param : params) {
    canonicalParams.push_back(cast<GenericTypeParamType>(param->getCanonicalType()));
  }

  SmallVector<Requirement, 8> canonicalRequirements;
  canonicalRequirements.reserve(requirements.size());
  for (auto &reqt : requirements) {
    if (reqt.getKind() != RequirementKind::Layout) {
      auto secondTy = reqt.getSecondType();
      canonicalRequirements.push_back(
          Requirement(reqt.getKind(), reqt.getFirstType()->getCanonicalType(),
                      secondTy ? secondTy->getCanonicalType() : CanType()));
    } else
      canonicalRequirements.push_back(
          Requirement(reqt.getKind(), reqt.getFirstType()->getCanonicalType(),
                      reqt.getLayoutConstraint()));
  }

  (void)skipValidation;
  auto canSig = get(canonicalParams, canonicalRequirements,
                    /*isKnownCanonical=*/true);

#ifndef NDEBUG
  if (skipValidation)
    return CanGenericSignature(canSig);

  PrettyStackTraceGenericSignature debugStack("canonicalizing", canSig);

  // Check that the signature is canonical.
  for (unsigned idx : indices(canonicalRequirements)) {
    debugStack.setRequirement(idx);

    const auto &reqt = canonicalRequirements[idx];

    // Left-hand side must be canonical in its context.
    // Check canonicalization of requirement itself.
    switch (reqt.getKind()) {
    case RequirementKind::Superclass:
      assert(canSig->isCanonicalTypeInContext(reqt.getFirstType()) &&
             "Left-hand side is not canonical");
      assert(canSig->isCanonicalTypeInContext(reqt.getSecondType()) &&
             "Superclass type isn't canonical in its own context");
      break;

    case RequirementKind::Layout:
      assert(canSig->isCanonicalTypeInContext(reqt.getFirstType()) &&
             "Left-hand side is not canonical");
      break;

    case RequirementKind::SameType:
      assert(reqt.getFirstType()->isTypeParameter() &&
             "Left-hand side must be a type parameter");
      if (reqt.getSecondType()->isTypeParameter()) {
        assert(compareDependentTypes(reqt.getFirstType(), reqt.getSecondType())
                 < 0 &&
               "Out-of-order type parameters in same-type constraint");
      } else {
        assert(canSig->isCanonicalTypeInContext(reqt.getSecondType()) &&
               "Concrete same-type isn't canonical in its own context");
      }
      break;

    case RequirementKind::Conformance:
      assert(reqt.getFirstType()->isTypeParameter() &&
             "Left-hand side must be a type parameter");
      assert(isa<ProtocolType>(reqt.getSecondType().getPointer()) &&
             "Right-hand side of conformance isn't a protocol type");
      break;
    }

    // From here on, we're only interested in requirements beyond the first.
    if (idx == 0) continue;

    // Make sure that the left-hand sides are in nondecreasing order.
    const auto &prevReqt = canonicalRequirements[idx-1];
    int compareLHS =
      compareDependentTypes(prevReqt.getFirstType(), reqt.getFirstType());
    assert(compareLHS <= 0 && "Out-of-order left-hand sides");

    // If we have two same-type requirements where the left-hand sides differ
    // but fall into the same equivalence class, we can check the form.
    if (compareLHS < 0 && reqt.getKind() == RequirementKind::SameType &&
        prevReqt.getKind() == RequirementKind::SameType &&
        canSig->areSameTypeParameterInContext(prevReqt.getFirstType(),
                                              reqt.getFirstType())) {
      // If it's a it's a type parameter, make sure the equivalence class is
      // wired together sanely.
      if (prevReqt.getSecondType()->isTypeParameter()) {
        assert(prevReqt.getSecondType()->isEqual(reqt.getFirstType()) &&
               "same-type constraints within an equiv. class are out-of-order");
      } else {
        // Otherwise, the concrete types must match up.
        assert(prevReqt.getSecondType()->isEqual(reqt.getSecondType()) &&
               "inconsistent concrete same-type constraints in equiv. class");
      }
    }

    // From here on, we only care about cases where the previous and current
    // requirements have the same left-hand side.
    if (compareLHS != 0) continue;

    // Check ordering of requirement kinds.
    assert((getRequirementKindOrder(prevReqt.getKind()) <=
            getRequirementKindOrder(reqt.getKind())) &&
           "Requirements for a given kind are out-of-order");

    // From here on, we only care about the same requirement kind.
    if (prevReqt.getKind() != reqt.getKind()) continue;

    assert(reqt.getKind() == RequirementKind::Conformance &&
           "Only conformance requirements can have multiples");

    auto prevProto =
      prevReqt.getSecondType()->castTo<ProtocolType>()->getDecl();
    auto proto = reqt.getSecondType()->castTo<ProtocolType>()->getDecl();
    assert(ProtocolType::compareProtocols(&prevProto, &proto) < 0 &&
           "Out-of-order conformance requirements");
  }
#endif

  return CanGenericSignature(canSig);
}

CanGenericSignature
GenericSignature::getCanonicalSignature() const {
  // If we haven't computed the canonical signature yet, do so now.
  if (CanonicalSignatureOrASTContext.isNull()) {
    // Compute the canonical signature.
    CanGenericSignature canSig = getCanonical(getGenericParams(),
                                              getRequirements());

    // Record either the canonical signature or an indication that
    // this is the canonical signature.
    if (canSig != this)
      CanonicalSignatureOrASTContext = canSig;
    else
      CanonicalSignatureOrASTContext = &getGenericParams()[0]->getASTContext();

    // Return the canonical signature.
    return canSig;
  }

  // A stored ASTContext indicates that this is the canonical
  // signature.
  if (CanonicalSignatureOrASTContext.is<ASTContext*>())
    // TODO: CanGenericSignature should be const-correct.
    return CanGenericSignature(const_cast<GenericSignature*>(this));
  
  // Otherwise, return the stored canonical signature.
  return CanGenericSignature(
           CanonicalSignatureOrASTContext.get<GenericSignature*>());
}

GenericEnvironment *GenericSignature::createGenericEnvironment() {
  auto *builder = getGenericSignatureBuilder();
  return GenericEnvironment::getIncomplete(this, builder);
}


ASTContext &GenericSignature::getASTContext() const {
  // Canonical signatures store the ASTContext directly.
  if (auto ctx = CanonicalSignatureOrASTContext.dyn_cast<ASTContext *>())
    return *ctx;

  // For everything else, just get it from the generic parameter.
  return getASTContext(getGenericParams(), getRequirements());
}

Optional<ProtocolConformanceRef>
GenericSignature::lookupConformance(CanType type, ProtocolDecl *proto) const {
  // FIXME: Actually implement this properly.
  auto *M = proto->getParentModule();

  if (type->isTypeParameter())
    return ProtocolConformanceRef(proto);

  return M->lookupConformance(type, proto);
}

bool GenericSignature::enumeratePairedRequirements(
               llvm::function_ref<bool(Type, ArrayRef<Requirement>)> fn) const {
  // We'll be walking through the list of requirements.
  ArrayRef<Requirement> reqs = getRequirements();
  unsigned curReqIdx = 0, numReqs = reqs.size();

  // ... and walking through the list of generic parameters.
  ArrayRef<GenericTypeParamType *> genericParams = getGenericParams();
  unsigned curGenericParamIdx = 0, numGenericParams = genericParams.size();

  // Figure out which generic parameters are complete.
  SmallVector<bool, 4> genericParamsAreConcrete(genericParams.size(), false);
  for (auto req : reqs) {
    if (req.getKind() != RequirementKind::SameType) continue;
    if (req.getSecondType()->isTypeParameter()) continue;

    auto gp = req.getFirstType()->getAs<GenericTypeParamType>();
    if (!gp) continue;

    unsigned index = GenericParamKey(gp).findIndexIn(genericParams);
    genericParamsAreConcrete[index] = true;
  }

  /// Local function to 'catch up' to the next dependent type we're going to
  /// visit, calling the function for each of the generic parameters in the
  /// generic parameter list prior to this parameter.
  auto enumerateGenericParamsUpToDependentType = [&](CanType depTy) -> bool {
    // Figure out where we should stop when enumerating generic parameters.
    unsigned stopDepth, stopIndex;
    if (auto gp = dyn_cast_or_null<GenericTypeParamType>(depTy)) {
      stopDepth = gp->getDepth();
      stopIndex = gp->getIndex();
    } else {
      stopDepth = genericParams.back()->getDepth() + 1;
      stopIndex = 0;
    }

    // Enumerate generic parameters up to the stopping point, calling the
    // callback function for each one
    while (curGenericParamIdx != numGenericParams) {
      auto curGenericParam = genericParams[curGenericParamIdx];

      // If the current generic parameter is before our stopping point, call
      // the function.
      if (curGenericParam->getDepth() < stopDepth ||
          (curGenericParam->getDepth() == stopDepth &&
           curGenericParam->getIndex() < stopIndex)) {
        if (!genericParamsAreConcrete[curGenericParamIdx] &&
            fn(curGenericParam, { }))
          return true;

        ++curGenericParamIdx;
        continue;
      }

      // If the current generic parameter is at our stopping point, we're
      // done.
      if (curGenericParam->getDepth() == stopDepth &&
          curGenericParam->getIndex() == stopIndex) {
        ++curGenericParamIdx;
        return false;
      }

      // Otherwise, there's nothing to do.
      break;
    }

    return false;
  };

  // Walk over all of the requirements.
  while (curReqIdx != numReqs) {
    // "Catch up" by enumerating generic parameters up to this dependent type.
    CanType depTy = reqs[curReqIdx].getFirstType()->getCanonicalType();
    if (enumerateGenericParamsUpToDependentType(depTy)) return true;

    // Utility to skip over non-conformance constraints that apply to this
    // type.
    auto skipNonConformanceConstraints = [&] {
      while (curReqIdx != numReqs &&
             reqs[curReqIdx].getKind() != RequirementKind::Conformance &&
             reqs[curReqIdx].getFirstType()->getCanonicalType() == depTy) {
        ++curReqIdx;
      }
    };

    // First, skip past any non-conformance constraints on this type.
    skipNonConformanceConstraints();

    // Collect all of the conformance constraints for this dependent type.
    unsigned startIdx = curReqIdx;
    unsigned endIdx = curReqIdx;
    while (curReqIdx != numReqs &&
           reqs[curReqIdx].getKind() == RequirementKind::Conformance &&
           reqs[curReqIdx].getFirstType()->getCanonicalType() == depTy) {
      ++curReqIdx;
      endIdx = curReqIdx;
    }

    // Skip any trailing non-conformance constraints.
    skipNonConformanceConstraints();

    // If there were any conformance constraints, or we have a generic
    // parameter we can't skip, invoke the callback.
    if ((startIdx != endIdx ||
         (isa<GenericTypeParamType>(depTy) &&
          !genericParamsAreConcrete[
            GenericParamKey(cast<GenericTypeParamType>(depTy))
              .findIndexIn(genericParams)])) &&
        fn(depTy, reqs.slice(startIdx, endIdx-startIdx)))
      return true;
  }

  // Catch up on any remaining generic parameters.
  return enumerateGenericParamsUpToDependentType(CanType());
}

SubstitutionMap
GenericSignature::getSubstitutionMap(SubstitutionList subs) const {
  SubstitutionMap result(const_cast<GenericSignature *>(this));

  enumeratePairedRequirements(
    [&](Type depTy, ArrayRef<Requirement> reqts) -> bool {
      auto sub = subs.front();
      subs = subs.slice(1);

      auto canTy = depTy->getCanonicalType();
      if (auto paramTy = dyn_cast<GenericTypeParamType>(canTy))
        result.addSubstitution(paramTy,
                               sub.getReplacement());

      auto conformances = sub.getConformances();
      assert(reqts.size() == conformances.size());

      for (unsigned i = 0, e = conformances.size(); i < e; i++) {
        assert(reqts[i].getSecondType()->getAnyNominal() ==
               conformances[i].getRequirement());
        result.addConformance(canTy, conformances[i]);
      }

      return false;
    });

  assert(subs.empty() && "did not use all substitutions?!");
  result.verify();
  return result;
}

SubstitutionMap
GenericSignature::
getSubstitutionMap(TypeSubstitutionFn subs,
                   LookupConformanceFn lookupConformance) const {
  SubstitutionMap subMap(const_cast<GenericSignature *>(this));

  // Enumerate all of the requirements that require substitution.
  enumeratePairedRequirements([&](Type depTy, ArrayRef<Requirement> reqs) {
    auto canTy = depTy->getCanonicalType();

    // Compute the replacement type.
    Type currentReplacement = depTy.subst(subs, lookupConformance,
                                          SubstFlags::UseErrorType);
    if (auto paramTy = dyn_cast<GenericTypeParamType>(canTy))
      if (!currentReplacement->hasError())
        subMap.addSubstitution(paramTy, currentReplacement);

    // Collect the conformances.
    for (auto req: reqs) {
      assert(req.getKind() == RequirementKind::Conformance);
      auto protoType = req.getSecondType()->castTo<ProtocolType>();
      if (auto conformance = lookupConformance(canTy,
                                               currentReplacement,
                                               protoType)) {
        subMap.addConformance(canTy, *conformance);
      }
    }

    return false;
  });

  subMap.verify();
  return subMap;
}

void GenericSignature::
getSubstitutions(const SubstitutionMap &subMap,
                 SmallVectorImpl<Substitution> &result) const {

  // Enumerate all of the requirements that require substitution.
  enumeratePairedRequirements([&](Type depTy, ArrayRef<Requirement> reqs) {
    auto &ctx = getASTContext();

    // Compute the replacement type.
    Type currentReplacement = depTy.subst(subMap);
    if (!currentReplacement)
      currentReplacement = ErrorType::get(depTy);

    // Collect the conformances.
    SmallVector<ProtocolConformanceRef, 4> currentConformances;
    for (auto req: reqs) {
      assert(req.getKind() == RequirementKind::Conformance);
      auto protoDecl = req.getSecondType()->castTo<ProtocolType>()->getDecl();
      if (auto conformance = subMap.lookupConformance(depTy->getCanonicalType(),
                                                      protoDecl)) {
        currentConformances.push_back(*conformance);
      } else {
        if (!currentReplacement->hasError())
          currentReplacement = ErrorType::get(currentReplacement);
        currentConformances.push_back(ProtocolConformanceRef(protoDecl));
      }
    }

    // Add it to the final substitution list.
    result.push_back({
      currentReplacement,
      ctx.AllocateCopy(currentConformances)
    });

    return false;
  });
}

bool GenericSignature::requiresClass(Type type) {
  if (!type->isTypeParameter()) return false;

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

  // If there is a superclass bound, then obviously it must be a class.
  // FIXME: We shouldn't need this?
  if (equivClass->superclass) return true;

  // If any of the protocols are class-bound, then it must be a class.
  // FIXME: We shouldn't need this?
  for (const auto &conforms : equivClass->conformsTo) {
    if (conforms.first->requiresClass()) return true;
  }

  return false;
}

/// Determine the superclass bound on the given dependent type.
Type GenericSignature::getSuperclassBound(Type type) {
  if (!type->isTypeParameter()) return nullptr;

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
}

/// Determine the set of protocols to which the given dependent type
/// must conform.
SmallVector<ProtocolDecl *, 2>
GenericSignature::getConformsTo(Type type) {
  if (!type->isTypeParameter()) return { };

  auto &builder = *getGenericSignatureBuilder();
  auto equivClass =
    builder.resolveEquivalenceClass(
                                  type,
                                  ArchetypeResolutionKind::CompleteWellFormed);
  if (!equivClass) return { };

  // If this type was mapped to a concrete type, then there are no
  // requirements.
  if (equivClass->concreteType) return { };

  // Retrieve the protocols to which this type conforms.
  SmallVector<ProtocolDecl *, 2> result;
  for (const auto &conforms : equivClass->conformsTo)
    result.push_back(conforms.first);

  // Canonicalize the resulting set of protocols.
  ProtocolType::canonicalizeProtocols(result);

  return result;
}

bool GenericSignature::conformsToProtocol(Type type, ProtocolDecl *proto) {
  // FIXME: Deal with concrete conformances here?
  if (!type->isTypeParameter()) return false;

  auto &builder = *getGenericSignatureBuilder();
  auto equivClass =
    builder.resolveEquivalenceClass(
                                  type,
                                  ArchetypeResolutionKind::CompleteWellFormed);
  if (!equivClass) return false;

  // FIXME: Deal with concrete conformances here?
  if (equivClass->concreteType) return false;

  // Check whether the representative conforms to this protocol.
  return equivClass->conformsTo.count(proto) > 0;
}

/// Determine whether the given dependent type is equal to a concrete type.
bool GenericSignature::isConcreteType(Type type) {
  return bool(getConcreteType(type));
}

/// Return the concrete type that the given dependent type is constrained to,
/// or the null Type if it is not the subject of a concrete same-type
/// constraint.
Type GenericSignature::getConcreteType(Type type) {
  if (!type->isTypeParameter()) return Type();

  auto &builder = *getGenericSignatureBuilder();
  auto equivClass =
    builder.resolveEquivalenceClass(
                                  type,
                                  ArchetypeResolutionKind::CompleteWellFormed);
  if (!equivClass) return Type();

  return equivClass->concreteType;
}

LayoutConstraint GenericSignature::getLayoutConstraint(Type type) {
  if (!type->isTypeParameter()) return LayoutConstraint();

  auto &builder = *getGenericSignatureBuilder();
  auto equivClass =
    builder.resolveEquivalenceClass(
                                  type,
                                  ArchetypeResolutionKind::CompleteWellFormed);
  if (!equivClass) return LayoutConstraint();

  return equivClass->layout;
}

bool GenericSignature::areSameTypeParameterInContext(Type type1, Type type2) {
  assert(type1->isTypeParameter());
  assert(type2->isTypeParameter());

  if (type1.getPointer() == type2.getPointer())
    return true;

  auto &builder = *getGenericSignatureBuilder();
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

bool GenericSignature::isRequirementSatisfied(Requirement requirement) {
  auto GSB = getGenericSignatureBuilder();

  auto firstType = requirement.getFirstType();
  auto canFirstType = getCanonicalTypeInContext(firstType);

  switch (requirement.getKind()) {
  case RequirementKind::Conformance: {
    auto protocolType = requirement.getSecondType()->castTo<ProtocolType>();
    auto protocol = protocolType->getDecl();

    if (canFirstType->isTypeParameter())
      return conformsToProtocol(canFirstType, protocol);
    else
      return (bool)GSB->lookupConformance(/*dependentType=*/CanType(),
                                          canFirstType, protocolType);
  }

  case RequirementKind::SameType: {
    auto canSecondType = getCanonicalTypeInContext(requirement.getSecondType());
    return canFirstType->isEqual(canSecondType);
  }

  case RequirementKind::Superclass: {
    auto requiredSuperclass =
        getCanonicalTypeInContext(requirement.getSecondType());

    // The requirement could be in terms of type parameters like a user-written
    // requirement, but it could also be in terms of concrete types if it has
    // been substituted/otherwise 'resolved', so we need to handle both.
    auto baseType = canFirstType;
    if (canFirstType->isTypeParameter()) {
      auto directSuperclass = getSuperclassBound(baseType);
      if (!directSuperclass)
        return false;

      baseType = getCanonicalTypeInContext(directSuperclass);
    }

    return requiredSuperclass->isExactSuperclassOf(baseType);
  }

  case RequirementKind::Layout: {
    auto requiredLayout = requirement.getLayoutConstraint();

    if (canFirstType->isTypeParameter()) {
      if (auto layout = getLayoutConstraint(canFirstType))
        return static_cast<bool>(layout.merge(requiredLayout));

      return false;
    }

    // The requirement is on a concrete type, so it's either globally correct
    // or globally incorrect, independent of this generic context. The latter
    // case should be diagnosed elsewhere, so let's assume it's correct.
    return true;
  }
  }
}

SmallVector<Requirement, 4> GenericSignature::requirementsNotSatisfiedBy(
                                                 GenericSignature *otherSig) {
  SmallVector<Requirement, 4> result;

  // If the signatures are the same, all requirements are satisfied.
  if (otherSig == this) return result;

  // If there is no other signature, no requirements are satisfied.
  if (!otherSig){
    result.insert(result.end(),
                  getRequirements().begin(), getRequirements().end());
    return result;
  }

  // Find the requirements that aren't satisfied.
  for (const auto &req : getRequirements()) {
    if (!otherSig->isRequirementSatisfied(req))
      result.push_back(req);
  }

  return result;
}

bool GenericSignature::isCanonicalTypeInContext(Type type) {
  // If the type isn't independently canonical, it's certainly not canonical
  // in this context.
  if (!type->isCanonical())
    return false;

  // All the contextual canonicality rules apply to type parameters, so if the
  // type doesn't involve any type parameters, it's already canonical.
  if (!type->hasTypeParameter())
    return true;

  auto &builder = *getGenericSignatureBuilder();
  return isCanonicalTypeInContext(type, builder);
}

bool GenericSignature::isCanonicalTypeInContext(Type type,
                                                GenericSignatureBuilder &builder) {
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

CanType GenericSignature::getCanonicalTypeInContext(Type type,
                                                    GenericSignatureBuilder &builder) {
  type = type->getCanonicalType();

  // All the contextual canonicality rules apply to type parameters, so if the
  // type doesn't involve any type parameters, it's already canonical.
  if (!type->hasTypeParameter())
    return CanType(type);

  // Replace non-canonical type parameters.
  type = type.transformRec([&](TypeBase *component) -> Optional<Type> {
    if (!isa<GenericTypeParamType>(component) &&
        !isa<DependentMemberType>(component))
      return None;

    // Find the equivalence class for this dependent member type.
    auto equivClass =
      builder.resolveEquivalenceClass(
                               Type(component),
                               ArchetypeResolutionKind::CompleteWellFormed);
    if (!equivClass) return None;

    if (equivClass->concreteType) {
      return getCanonicalTypeInContext(equivClass->concreteType, builder);
    }

    return equivClass->getAnchor(builder, getGenericParams());
  });
  
  auto result = type->getCanonicalType();

  assert(isCanonicalTypeInContext(result, builder));
  return result;
}

CanType GenericSignature::getCanonicalTypeInContext(Type type) {
  type = type->getCanonicalType();

  // All the contextual canonicality rules apply to type parameters, so if the
  // type doesn't involve any type parameters, it's already canonical.
  if (!type->hasTypeParameter())
    return CanType(type);

  auto &builder = *getGenericSignatureBuilder();
  return getCanonicalTypeInContext(type, builder);
}

GenericEnvironment *CanGenericSignature::getGenericEnvironment() const {
  // generic signature builders are stored on the ASTContext.
  auto &ctx = getGenericParams()[0]->getASTContext();
  return ctx.getOrCreateCanonicalGenericEnvironment(
           ctx.getOrCreateGenericSignatureBuilder(*this),
           *this);
}

/// Remove all of the associated type declarations from the given type
/// parameter, producing \c DependentMemberTypes with names alone.
static Type eraseAssociatedTypes(Type type) {
  if (auto depMemTy = type->getAs<DependentMemberType>())
    return DependentMemberType::get(eraseAssociatedTypes(depMemTy->getBase()),
                                    depMemTy->getName());

  return type;
}

namespace {
  typedef GenericSignatureBuilder::RequirementSource RequirementSource;

  template<typename T>
  using GSBConstraint = GenericSignatureBuilder::Constraint<T>;
} // end anonymous namespace

/// Determine whether there is a conformance of the given
/// subject type to the given protocol within the given set of explicit
/// requirements.
static bool hasConformanceInSignature(ArrayRef<Requirement> requirements,
                                      Type subjectType,
                                      ProtocolDecl *proto) {
  // Make sure this requirement exists in the requirement signature.
  for (const auto& req: requirements) {
    if (req.getKind() == RequirementKind::Conformance &&
        req.getFirstType()->isEqual(subjectType) &&
        req.getSecondType()->castTo<ProtocolType>()->getDecl()
          == proto) {
      return true;
    }
  }

  return false;
}

/// Check whether the given requirement source has any non-canonical protocol
/// requirements in it.
static bool hasNonCanonicalSelfProtocolRequirement(
                                          const RequirementSource *source,
                                          ProtocolDecl *conformingProto) {
  for (; source; source = source->parent) {
    // Only look at protocol requirements.
    if (!source->isProtocolRequirement())
      continue;

    // If we don't already have a requirement signature for this protocol,
    // build one now.
    auto inProto = source->getProtocolDecl();
    if (!inProto->isRequirementSignatureComputed()) {
      inProto->computeRequirementSignature();
      assert(inProto->isRequirementSignatureComputed() &&
             "couldn't compute requirement signature?");
    }

    // Check whether the given requirement is in the requirement signature.
    if (!source->usesRequirementSignature &&
        !hasConformanceInSignature(inProto->getRequirementSignature(),
                                   source->getStoredType(), conformingProto))
      return true;

    // Update the conforming protocol for the rest of the search.
    conformingProto = inProto;
  }

  return false;
}

/// Retrieve the best requirement source from the list
static const RequirementSource *
getBestRequirementSource(ArrayRef<GSBConstraint<ProtocolDecl *>> constraints) {
  const RequirementSource *bestSource = nullptr;
  bool bestIsNonCanonical = false;

  auto isBetter = [&](const RequirementSource *source, bool isNonCanonical) {
    if (!bestSource) return true;

    if (bestIsNonCanonical != isNonCanonical)
      return bestIsNonCanonical;

    return bestSource->compare(source) > 0;
  };

  for (const auto &constraint : constraints) {
    auto source = constraint.source;

    // If there is a non-canonical protocol requirement next to the root,
    // skip this requirement source.
    bool isNonCanonical =
      hasNonCanonicalSelfProtocolRequirement(source, constraint.value);

    if (isBetter(source, isNonCanonical)) {
      bestSource = source;
      bestIsNonCanonical = isNonCanonical;
      continue;
    }
  }

  return bestSource;
}

ConformanceAccessPath GenericSignature::getConformanceAccessPath(
                                                       Type type,
                                                       ProtocolDecl *protocol) {
  assert(type->isTypeParameter() && "not a type parameter");

  // Resolve this type to a potential archetype.
  auto &builder = *getGenericSignatureBuilder();
  auto equivClass =
    builder.resolveEquivalenceClass(
                                  type,
                                  ArchetypeResolutionKind::CompleteWellFormed);

  // Dig out the conformance of this type to the given protocol, because we
  // want its requirement source.
  auto conforms = equivClass->conformsTo.find(protocol);
  assert(conforms != equivClass->conformsTo.end());

  // Follow the requirement source to form the conformance access path.
  typedef GenericSignatureBuilder::RequirementSource RequirementSource;
  ConformanceAccessPath path;

  // Local function to construct the conformance access path from the
  // requirement.
  std::function<void(ArrayRef<Requirement>, const RequirementSource *,
                     ProtocolDecl *, Type, ProtocolDecl *)> buildPath;
  buildPath = [&](ArrayRef<Requirement> reqs, const RequirementSource *source,
                  ProtocolDecl *conformingProto, Type rootType,
                  ProtocolDecl *requirementSignatureProto) {
    // Each protocol requirement is a step along the path.
    if (source->isProtocolRequirement()) {
      // If we're expanding for a protocol that had no requirement signature
      // and have hit the penultimate step, this is the last step
      // that would occur in the requirement signature.
      Optional<GenericSignatureBuilder> replacementBuilder;
      if (!source->parent->parent && requirementSignatureProto) {
        // If we have a requirement signature now, we're done.
        if (source->usesRequirementSignature) {
          Type subjectType = source->getStoredType()->getCanonicalType();
          path.path.push_back({subjectType, conformingProto});
          return;
        }

        // The generic signature builder we're using for this protocol
        // wasn't built from its own requirement signature, so we can't
        // trust it. Make sure we have a requirement signature, then build
        // a new generic signature builder.
        // FIXME: It would be better if we could replace the canonical generic
        // signature builder with the rebuilt one.
        if (!requirementSignatureProto->isRequirementSignatureComputed())
          requirementSignatureProto->computeRequirementSignature();
        assert(requirementSignatureProto->isRequirementSignatureComputed());

        replacementBuilder.emplace(getASTContext());
        replacementBuilder->addGenericSignature(
                            requirementSignatureProto->getGenericSignature());
        replacementBuilder->processDelayedRequirements();
      }

      // Follow the rest of the path to derive the conformance into which
      // this particular protocol requirement step would look.
      auto inProtocol = source->getProtocolDecl();
      buildPath(reqs, source->parent, inProtocol, rootType,
                requirementSignatureProto);
      assert(path.path.back().second == inProtocol &&
             "path produces incorrect conformance");

      // If this step was computed via the requirement signature, add it
      // directly.
      if (source->usesRequirementSignature) {
        // Add this step along the path, which involves looking for the
        // conformance we want (\c conformingProto) within the protocol
        // described by this source.

        // Canonicalize the subject type within the protocol's generic
        // signature.
        Type subjectType = source->getStoredType();
        subjectType = inProtocol->getGenericSignature()
          ->getCanonicalTypeInContext(subjectType);

        assert(hasConformanceInSignature(inProtocol->getRequirementSignature(),
                                         subjectType, conformingProto) &&
               "missing explicit conformance in requirement signature");

        // Record this step.
        path.path.push_back({subjectType, conformingProto});
        return;
      }

      // Get the generic signature builder for the protocol.
      // Get a generic signature for the protocol's signature.
      auto inProtoSig = inProtocol->getGenericSignature();
      auto &inProtoSigBuilder =
          replacementBuilder ? *replacementBuilder
                             : *inProtoSig->getGenericSignatureBuilder();

      // Retrieve the stored type, but erase all of the specific associated
      // type declarations; we don't want any details of the enclosing context
      // to sneak in here.
      Type storedType = eraseAssociatedTypes(source->getStoredType());

      // Dig out the potential archetype for this stored type.
      auto equivClass =
        inProtoSigBuilder.resolveEquivalenceClass(
                                 storedType,
                                 ArchetypeResolutionKind::CompleteWellFormed);

      // Find the conformance of this potential archetype to the protocol in
      // question.
      auto conforms = equivClass->conformsTo.find(conformingProto);
      assert(conforms != equivClass->conformsTo.end());

      // Compute the root type, canonicalizing it w.r.t. the protocol context.
      auto conformsSource = getBestRequirementSource(conforms->second);
      assert(conformsSource != source || !requirementSignatureProto);
      Type localRootType = conformsSource->getRootType();
      localRootType = inProtoSig->getCanonicalTypeInContext(localRootType);

      // Build the path according to the requirement signature.
      buildPath(inProtocol->getRequirementSignature(), conformsSource,
                conformingProto, localRootType, inProtocol);

      // We're done.
      return;
    }

    // If we have a superclass or concrete requirement, the conformance
    // we need is stored in it.
    if (source->kind == RequirementSource::Superclass ||
        source->kind == RequirementSource::Concrete) {
      auto conformance = source->getProtocolConformance();
      assert(conformance.getRequirement() == conformingProto);
      path.path.push_back({source->getAffectedType(), conformingProto});
      return;
    }

    // If we still have a parent, keep going.
    if (source->parent) {
      buildPath(reqs, source->parent, conformingProto, rootType,
                requirementSignatureProto);
      return;
    }

    // We are at an explicit or inferred requirement.
    assert(source->kind == RequirementSource::Explicit ||
           source->kind == RequirementSource::Inferred);

    // Skip trivial path elements. These occur when querying a requirement
    // signature.
    if (!path.path.empty() && conformingProto == path.path.back().second &&
        rootType->isEqual(conformingProto->getSelfInterfaceType()))
      return;

    assert(hasConformanceInSignature(reqs, rootType, conformingProto) &&
           "missing explicit conformance in signature");

    // Add the root of the path, which starts at this explicit requirement.
    path.path.push_back({rootType, conformingProto});
  };

  // Canonicalize the root type.
  auto source = getBestRequirementSource(conforms->second);
  Type rootType = source->getRootType()->getCanonicalType(this);

  // Build the path.
  buildPath(getRequirements(), source, protocol, rootType, nullptr);

  // Return the path; we're done!
  return path;
}

unsigned GenericParamKey::findIndexIn(
                  llvm::ArrayRef<GenericTypeParamType *> genericParams) const {
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
