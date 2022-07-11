//===--- SubstitutionMap.cpp - Type substitution map ----------------------===//
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
// This file defines the SubstitutionMap class. A SubstitutionMap packages
// together a set of replacement types and protocol conformances for
// specializing generic types.
//
// SubstitutionMaps either have type parameters or archetypes as keys,
// based on whether they were built from a GenericSignature or a
// GenericEnvironment.
//
// To specialize a type, call Type::subst() with the right SubstitutionMap.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/SubstitutionMap.h"
#include "SubstitutionMapStorage.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Decl.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/LazyResolver.h"
#include "swift/AST/Module.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/TypeCheckRequests.h"
#include "swift/AST/Types.h"
#include "swift/Basic/Defer.h"
#include "llvm/Support/Debug.h"

using namespace swift;

SubstitutionMap::Storage::Storage(
                              GenericSignature genericSig,
                              ArrayRef<Type> replacementTypes,
                              ArrayRef<ProtocolConformanceRef> conformances)
  : genericSig(genericSig),
    numConformanceRequirements(genericSig->getNumConformanceRequirements())
{
  assert(replacementTypes.size() == getNumReplacementTypes());
  assert(conformances.size() == numConformanceRequirements);

  std::copy(replacementTypes.begin(), replacementTypes.end(),
            getReplacementTypes().data());
  std::copy(conformances.begin(), conformances.end(),
            getConformances().data());
  populatedAllReplacements = false;
}

SubstitutionMap::SubstitutionMap(
                                GenericSignature genericSig,
                                ArrayRef<Type> replacementTypes,
                                ArrayRef<ProtocolConformanceRef> conformances)
  : storage(Storage::get(genericSig, replacementTypes, conformances)) {
#ifndef NDEBUG
  if (genericSig->getASTContext().LangOpts.VerifyAllSubstitutionMaps)
    verify();
#endif
}

ArrayRef<Type> SubstitutionMap::getReplacementTypesBuffer() const {
  return storage ? storage->getReplacementTypes() : ArrayRef<Type>();
}

MutableArrayRef<Type> SubstitutionMap::getReplacementTypesBuffer() {
  return storage ? storage->getReplacementTypes() : MutableArrayRef<Type>();
}

MutableArrayRef<ProtocolConformanceRef>
SubstitutionMap::getConformancesBuffer() {
  return storage ? storage->getConformances()
                 : MutableArrayRef<ProtocolConformanceRef>();
}

ArrayRef<ProtocolConformanceRef> SubstitutionMap::getConformances() const {
  return storage ? storage->getConformances()
                 : ArrayRef<ProtocolConformanceRef>();
}

ArrayRef<Type> SubstitutionMap::getReplacementTypes() const {
  if (empty()) return { };

  // Make sure we've filled in all of the replacement types.
  if (!storage->populatedAllReplacements) {
    for (auto gp : getGenericSignature().getGenericParams()) {
      (void)lookupSubstitution(cast<SubstitutableType>(gp->getCanonicalType()));
    }

    storage->populatedAllReplacements = true;
  }

  return getReplacementTypesBuffer();
}

ArrayRef<Type> SubstitutionMap::getInnermostReplacementTypes() const {
  if (empty()) return { };

  return getReplacementTypes().take_back(
      getGenericSignature().getInnermostGenericParams().size());
}

GenericSignature SubstitutionMap::getGenericSignature() const {
  return storage ? storage->getGenericSignature() : nullptr;
}

bool SubstitutionMap::empty() const {
  return getGenericSignature().isNull();
}

bool SubstitutionMap::hasAnySubstitutableParams() const {
  auto genericSig = getGenericSignature();
  if (!genericSig) return false;

  return !genericSig->areAllParamsConcrete();
}

bool SubstitutionMap::hasArchetypes() const {
  for (Type replacementTy : getReplacementTypesBuffer()) {
    if (replacementTy && replacementTy->hasArchetype())
      return true;
  }
  return false;
}

bool SubstitutionMap::hasOpenedExistential() const {
  for (Type replacementTy : getReplacementTypesBuffer()) {
    if (replacementTy && replacementTy->hasOpenedExistential())
      return true;
  }
  return false;
}

bool SubstitutionMap::hasDynamicSelf() const {
  for (Type replacementTy : getReplacementTypesBuffer()) {
    if (replacementTy && replacementTy->hasDynamicSelfType())
      return true;
  }
  return false;
}

bool SubstitutionMap::isCanonical() const {
  if (empty()) return true;

  if (!getGenericSignature()->isCanonical()) return false;

  for (Type replacementTy : getReplacementTypesBuffer()) {
    if (replacementTy && !replacementTy->isCanonical())
      return false;
  }

  for (auto conf : getConformances()) {
    if (!conf.isCanonical())
      return false;
  }

  return true;
}

SubstitutionMap SubstitutionMap::getCanonical() const {
  if (empty()) return *this;

  auto canonicalSig = getGenericSignature().getCanonicalSignature();
  SmallVector<Type, 4> replacementTypes;
  for (Type replacementType : getReplacementTypesBuffer()) {
    if (replacementType)
      replacementTypes.push_back(replacementType->getCanonicalType());
    else
      replacementTypes.push_back(nullptr);
  }

  SmallVector<ProtocolConformanceRef, 4> conformances;
  for (auto conf : getConformances()) {
    conformances.push_back(conf.getCanonicalConformanceRef());
  }

  return SubstitutionMap::get(canonicalSig,
                              ArrayRef<Type>(replacementTypes),
                              ArrayRef<ProtocolConformanceRef>(conformances));
}


SubstitutionMap SubstitutionMap::get(GenericSignature genericSig,
                                     SubstitutionMap substitutions) {
  if (!genericSig) {
    assert(!substitutions.hasAnySubstitutableParams() &&
           "Shouldn't have substitutions here");
    return SubstitutionMap();
  }

  return SubstitutionMap::get(genericSig,
           [&](SubstitutableType *type) -> Type {
             return substitutions.lookupSubstitution(
                      CanSubstitutableType(type));
           },
           LookUpConformanceInSubstitutionMap(substitutions));
}

/// Build an interface type substitution map for the given generic signature
/// from a type substitution function and conformance lookup function.
SubstitutionMap SubstitutionMap::get(GenericSignature genericSig,
                                     TypeSubstitutionFn subs,
                                     LookupConformanceFn lookupConformance) {
  if (!genericSig) {
    return SubstitutionMap();
  }

  // Form the replacement types.
  SmallVector<Type, 4> replacementTypes;
  replacementTypes.reserve(genericSig.getGenericParams().size());

  genericSig->forEachParam([&](GenericTypeParamType *gp, bool canonical) {
    // Don't eagerly form replacements for non-canonical generic parameters.
    if (!canonical) {
      replacementTypes.push_back(Type());
      return;
    }

    // Record the replacement.
    Type replacement = Type(gp).subst(subs, lookupConformance);
    replacementTypes.push_back(replacement);
  });

  // Form the stored conformances.
  SmallVector<ProtocolConformanceRef, 4> conformances;
  for (const auto &req : genericSig.getRequirements()) {
    if (req.getKind() != RequirementKind::Conformance) continue;

    CanType depTy = req.getFirstType()->getCanonicalType();
    auto replacement = depTy.subst(subs, lookupConformance);
    auto *proto = req.getProtocolDecl();
    auto conformance = lookupConformance(depTy, replacement, proto);
    conformances.push_back(conformance);
  }

  return SubstitutionMap(genericSig, replacementTypes, conformances);
}

Type SubstitutionMap::lookupSubstitution(CanSubstitutableType type) const {
  if (empty())
    return Type();

  // If we have an archetype, map out of the context so we can compute a
  // conformance access path.
  if (auto archetype = dyn_cast<ArchetypeType>(type)) {
    if (!isa<PrimaryArchetypeType>(archetype) &&
        !isa<SequenceArchetypeType>(archetype))
      return Type();

    type = cast<GenericTypeParamType>(
      archetype->getInterfaceType()->getCanonicalType());
  }

  // Find the index of the replacement type based on the generic parameter we
  // have.
  auto genericParam = cast<GenericTypeParamType>(type);
  auto mutableThis = const_cast<SubstitutionMap *>(this);
  auto replacementTypes = mutableThis->getReplacementTypesBuffer();
  auto genericSig = getGenericSignature();
  assert(genericSig);
  auto genericParams = genericSig.getGenericParams();
  auto replacementIndex =
    GenericParamKey(genericParam).findIndexIn(genericParams);

  // If this generic parameter isn't represented, we don't have a replacement
  // type for it.
  if (replacementIndex == genericParams.size())
    return Type();

  // If we already have a replacement type, return it.
  Type &replacementType = replacementTypes[replacementIndex];
  if (replacementType)
    return replacementType;

  // The generic parameter may have been made concrete by the generic signature,
  // substitute into the concrete type.
  if (auto concreteType = genericSig->getConcreteType(genericParam)) {
    // Set the replacement type to an error, to block infinite recursion.
    replacementType = ErrorType::get(concreteType);

    // Substitute into the replacement type.
    replacementType = concreteType.subst(*this);

    // If the generic signature is canonical, canonicalize the replacement type.
    if (getGenericSignature()->isCanonical())
      replacementType = replacementType->getCanonicalType();

    return replacementType;
  }

  // The generic parameter may not be canonical. Retrieve the canonical
  // type, which will be dependent.
  CanType canonicalType = genericSig.getCanonicalTypeInContext(genericParam);

  // If nothing changed, we don't have a replacement.
  if (canonicalType == type) return Type();

  // If we're left with a substitutable type, substitute into that.
  // First, set the replacement type to an error, to block infinite recursion.
  replacementType = ErrorType::get(type);

  replacementType = lookupSubstitution(cast<SubstitutableType>(canonicalType));

  // If the generic signature is canonical, canonicalize the replacement type.
  if (getGenericSignature()->isCanonical())
    replacementType = replacementType->getCanonicalType();

  return replacementType;
}

ProtocolConformanceRef
SubstitutionMap::lookupConformance(CanType type, ProtocolDecl *proto) const {
  if (empty())
    return ProtocolConformanceRef::forInvalid();

  // If we have an archetype, map out of the context so we can compute a
  // conformance access path.
  if (auto archetype = dyn_cast<ArchetypeType>(type)) {
    if (!isa<OpaqueTypeArchetypeType>(archetype->getRoot())) {
      type = archetype->getInterfaceType()->getCanonicalType();
    }
  }

  // Error path: if we don't have a type parameter, there is no conformance.
  // FIXME: Query concrete conformances in the generic signature?
  if (!type->isTypeParameter())
    return ProtocolConformanceRef::forInvalid();

  auto genericSig = getGenericSignature();

  auto getSignatureConformance =
      [&](Type type, ProtocolDecl *proto) -> Optional<ProtocolConformanceRef> {
    unsigned index = 0;
    for (auto reqt : genericSig.getRequirements()) {
      if (reqt.getKind() == RequirementKind::Conformance) {
        if (reqt.getFirstType()->isEqual(type) &&
            reqt.getProtocolDecl() == proto)
          return getConformances()[index];

        ++index;
      }
    }

    return None;
  };

  // Fast path -- check if the generic signature directly states the
  // conformance.
  if (auto directConformance = getSignatureConformance(type, proto))
    return *directConformance;

  // Check whether the superclass conforms.
  if (auto superclass = genericSig->getSuperclassBound(type)) {
    LookUpConformanceInSignature lookup(getGenericSignature().getPointer());
    auto substType = type.subst(*this);
    if (auto conformance = lookup(type->getCanonicalType(), substType, proto)){
      return conformance;
    }
  }

  // If the type doesn't conform to this protocol, the result isn't formed
  // from these requirements.
  if (!genericSig->requiresProtocol(type, proto)) {
    Type substType = type.subst(*this);
    return ProtocolConformanceRef::forMissingOrInvalid(substType, proto);
  }

  auto accessPath =
    genericSig->getConformanceAccessPath(type, proto);

  // Fall through because we cannot yet evaluate an access path.
  ProtocolConformanceRef conformance;
  for (const auto &step : accessPath) {
    // For the first step, grab the initial conformance.
    if (conformance.isInvalid()) {
      if (auto initialConformance = getSignatureConformance(
            step.first, step.second)) {
        conformance = *initialConformance;
        continue;
      }

      // We couldn't find the initial conformance, fail.
      return ProtocolConformanceRef::forInvalid();
    }

    // If we've hit an abstract conformance, everything from here on out is
    // abstract.
    // FIXME: This may not always be true, but it holds for now.
    if (conformance.isAbstract()) {
      // FIXME: Rip this out once we can get a concrete conformance from
      // an archetype.
      auto substType = type.subst(*this);
      if (substType->hasError())
        return ProtocolConformanceRef(proto);

      if ((!substType->is<ArchetypeType>() ||
           substType->castTo<ArchetypeType>()->getSuperclass()) &&
          !substType->isTypeParameter() &&
          !substType->isExistentialType()) {
        auto *M = proto->getParentModule();
        return M->lookupConformance(substType, proto);
      }

      return ProtocolConformanceRef(proto);
    }

    // For the second step, we're looking into the requirement signature for
    // this protocol.
    auto concrete = conformance.getConcrete();
    auto normal = concrete->getRootNormalConformance();

    // If we haven't set the signature conformances yet, force the issue now.
    if (normal->getSignatureConformances().empty()) {
      // If we're in the process of checking the type witnesses, fail
      // gracefully.
      // FIXME: Seems like we should be able to get at the intermediate state
      // to use that.
      if (normal->getState() == ProtocolConformanceState::CheckingTypeWitnesses)
        return ProtocolConformanceRef::forInvalid();
    }

    // Get the associated conformance.
    conformance = concrete->getAssociatedConformance(step.first, step.second);
    if (conformance.isInvalid())
      return conformance;
  }

  return conformance;
}

SubstitutionMap SubstitutionMap::mapReplacementTypesOutOfContext() const {
  return subst(MapTypeOutOfContext(), MakeAbstractConformanceForGenericType());
}

SubstitutionMap SubstitutionMap::subst(SubstitutionMap subMap,
                                       SubstOptions options) const {
  return subst(QuerySubstitutionMap{subMap},
               LookUpConformanceInSubstitutionMap(subMap),
               options);
}

SubstitutionMap SubstitutionMap::subst(TypeSubstitutionFn subs,
                                       LookupConformanceFn conformances,
                                       SubstOptions options) const {
  if (empty()) return SubstitutionMap();

  SmallVector<Type, 4> newSubs;
  for (Type type : getReplacementTypesBuffer()) {
    if (!type) {
      // Non-canonical parameter.
      newSubs.push_back(Type());
      continue;
    }
    newSubs.push_back(type.subst(subs, conformances, options));
  }

  SmallVector<ProtocolConformanceRef, 4> newConformances;
  auto oldConformances = getConformances();

  auto genericSig = getGenericSignature();
  for (const auto &req : genericSig.getRequirements()) {
    if (req.getKind() != RequirementKind::Conformance) continue;

    auto conformance = oldConformances[0];

    // Fast path for concrete case -- we don't need to compute substType
    // at all.
    if (conformance.isConcrete() &&
        !options.contains(SubstFlags::SubstituteOpaqueArchetypes)) {
      newConformances.push_back(
        ProtocolConformanceRef(
          conformance.getConcrete()->subst(subs, conformances, options)));
    } else {
      auto origType = req.getFirstType();
      auto substType = origType.subst(*this, options);

      newConformances.push_back(
        conformance.subst(substType, subs, conformances, options));
    }
    
    oldConformances = oldConformances.slice(1);
  }

  assert(oldConformances.empty());
  return SubstitutionMap(genericSig, newSubs, newConformances);
}

SubstitutionMap
SubstitutionMap::getProtocolSubstitutions(ProtocolDecl *protocol,
                                          Type selfType,
                                          ProtocolConformanceRef conformance) {
  return get(protocol->getGenericSignature(),
             llvm::makeArrayRef<Type>(selfType),
             llvm::makeArrayRef<ProtocolConformanceRef>(conformance));
}

SubstitutionMap
SubstitutionMap::getOverrideSubstitutions(
                                      const ValueDecl *baseDecl,
                                      const ValueDecl *derivedDecl,
                                      Optional<SubstitutionMap> derivedSubs) {
  // For overrides within a protocol hierarchy, substitute the Self type.
  if (auto baseProto = baseDecl->getDeclContext()->getSelfProtocolDecl()) {
    if (auto derivedProtoSelf =
          derivedDecl->getDeclContext()->getSelfInterfaceType()) {
      return SubstitutionMap::getProtocolSubstitutions(
                                             baseProto,
                                             derivedProtoSelf,
                                             ProtocolConformanceRef(baseProto));
    }

    return SubstitutionMap();
  }

  auto *baseClass = baseDecl->getDeclContext()->getSelfClassDecl();
  auto *derivedClass = derivedDecl->getDeclContext()->getSelfClassDecl();

  auto baseSig = baseDecl->getInnermostDeclContext()
      ->getGenericSignatureOfContext();
  auto derivedSig = derivedDecl->getInnermostDeclContext()
      ->getGenericSignatureOfContext();

  return getOverrideSubstitutions(baseClass, derivedClass,
                                  baseSig, derivedSig,
                                  derivedSubs);
}

SubstitutionMap
SubstitutionMap::getOverrideSubstitutions(const ClassDecl *baseClass,
                                          const ClassDecl *derivedClass,
                                          GenericSignature baseSig,
                                          GenericSignature derivedSig,
                                          Optional<SubstitutionMap> derivedSubs) {
  if (baseSig.isNull())
    return SubstitutionMap();

  auto *M = baseClass->getParentModule();

  unsigned baseDepth = 0;
  SubstitutionMap baseSubMap;
  if (auto baseClassSig = baseClass->getGenericSignature()) {
    baseDepth = baseClassSig.getGenericParams().back()->getDepth() + 1;

    auto derivedClassTy = derivedClass->getDeclaredInterfaceType();
    if (derivedSubs)
      derivedClassTy = derivedClassTy.subst(*derivedSubs);
    auto baseClassTy = derivedClassTy->getSuperclassForDecl(baseClass);
    if (baseClassTy->is<ErrorType>())
      return SubstitutionMap();

    baseSubMap = baseClassTy->getContextSubstitutionMap(M, baseClass);
  }

  unsigned origDepth = 0;
  if (auto derivedClassSig = derivedClass->getGenericSignature())
    origDepth = derivedClassSig.getGenericParams().back()->getDepth() + 1;

  SubstitutionMap origSubMap;
  if (derivedSubs)
    origSubMap = *derivedSubs;
  else if (derivedSig)
    origSubMap = derivedSig->getIdentitySubstitutionMap();

  return combineSubstitutionMaps(baseSubMap, origSubMap,
                                 CombineSubstitutionMaps::AtDepth,
                                 baseDepth, origDepth,
                                 baseSig);
}

SubstitutionMap
SubstitutionMap::combineSubstitutionMaps(SubstitutionMap firstSubMap,
                                         SubstitutionMap secondSubMap,
                                         CombineSubstitutionMaps how,
                                         unsigned firstDepthOrIndex,
                                         unsigned secondDepthOrIndex,
                                         GenericSignature genericSig) {
  auto &ctx = genericSig->getASTContext();

  auto replaceGenericParameter = [&](Type type) -> Type {
    if (auto gp = type->getAs<GenericTypeParamType>()) {
      if (how == CombineSubstitutionMaps::AtDepth) {
        if (gp->getDepth() < firstDepthOrIndex)
          return Type();
        return GenericTypeParamType::get(gp->isTypeSequence(),
                                         gp->getDepth() + secondDepthOrIndex -
                                             firstDepthOrIndex,
                                         gp->getIndex(), ctx);
      }

      assert(how == CombineSubstitutionMaps::AtIndex);
      if (gp->getIndex() < firstDepthOrIndex)
        return Type();
      return GenericTypeParamType::get(
          gp->isTypeSequence(), gp->getDepth(),
          gp->getIndex() + secondDepthOrIndex - firstDepthOrIndex, ctx);
    }

    return type;
  };

  return get(
    genericSig,
    [&](SubstitutableType *type) {
      if (auto replacement = replaceGenericParameter(type))
        return Type(replacement).subst(secondSubMap);
      return Type(type).subst(firstSubMap);
    },
    [&](CanType type, Type substType, ProtocolDecl *proto) {
      if (auto replacement = type.transform(replaceGenericParameter))
        return secondSubMap.lookupConformance(replacement->getCanonicalType(),
                                              proto);
      if (auto conformance = firstSubMap.lookupConformance(type, proto))
        return conformance;

      // We might not have enough information in the substitution maps alone.
      //
      // Eg,
      //
      // class Base<T1> {
      //   func foo<U1>(_: U1) where T1 : P {}
      // }
      //
      // class Derived<T2> : Base<Foo<T2>> {
      //   override func foo<U2>(_: U2) where T2 : Q {}
      // }
      //
      // Suppose we're devirtualizing a call to Base.foo() on a value whose
      // type is known to be Derived<Bar>. We start with substitutions written
      // in terms of Base.foo()'s generic signature:
      //
      // <T1, U1 where T1 : P>
      // T1 := Foo<Bar>
      // T1 : P := Foo<Bar> : P
      //
      // We want to build substitutions in terms of Derived.foo()'s
      // generic signature:
      //
      // <T2, U2 where T2 : Q>
      // T2 := Bar
      // T2 : Q := Bar : Q
      //
      // The conformance Bar : Q is difficult to recover in the general case.
      //
      // Some combination of storing substitution maps in BoundGenericTypes
      // as well as for method overrides would solve this, but for now, just
      // punt to module lookup.
      return proto->getParentModule()->lookupConformance(substType, proto);
    });
}

void SubstitutionMap::verify() const {
#ifndef NDEBUG
  if (empty())
    return;

  unsigned conformanceIndex = 0;

  for (const auto &req : getGenericSignature().getRequirements()) {
    if (req.getKind() != RequirementKind::Conformance)
      continue;

    SWIFT_DEFER { ++conformanceIndex; };
    auto substType = req.getFirstType().subst(*this);
    if (substType->isTypeParameter() ||
        substType->is<ArchetypeType>() ||
        substType->isTypeVariableOrMember() ||
        substType->is<UnresolvedType>() ||
        substType->hasError())
      continue;

    auto conformance = getConformances()[conformanceIndex];

    if (conformance.isInvalid())
      continue;

    // All of the conformances should be concrete.
    if (!conformance.isConcrete()) {
      llvm::dbgs() << "Concrete type cannot have abstract conformance:\n";
      substType->dump(llvm::dbgs());
      llvm::dbgs() << "SubstitutionMap:\n";
      dump(llvm::dbgs());
      llvm::dbgs() << "\n";
      llvm::dbgs() << "Requirement:\n";
      req.dump(llvm::dbgs());
      llvm::dbgs() << "\n";
    }
    assert(conformance.isConcrete() && "Conformance should be concrete");
    
    if (substType->is<UnboundGenericType>())
      continue;
    
    auto conformanceTy = conformance.getConcrete()->getType();
    if (conformanceTy->hasTypeParameter()
        && !substType->hasTypeParameter()) {
      conformanceTy = conformance.getConcrete()->getDeclContext()
        ->mapTypeIntoContext(conformanceTy);
    }
    
    if (!substType->isEqual(conformanceTy)) {
      llvm::dbgs() << "Conformance must match concrete replacement type:\n";
      substType->dump(llvm::dbgs());
      llvm::dbgs() << "Conformance type:\n";
      conformance.getConcrete()->getType()->dump(llvm::dbgs());
      llvm::dbgs() << "Conformance:\n";
      conformance.dump(llvm::dbgs());
      llvm::dbgs() << "\n";
      llvm::dbgs() << "SubstitutionMap:\n";
      dump(llvm::dbgs());
      llvm::dbgs() << "\n";
      llvm::dbgs() << "Requirement:\n";
      req.dump(llvm::dbgs());
      llvm::dbgs() << "\n";
    }
    assert(substType->isEqual(conformanceTy)
           && "conformance should match corresponding type");

    if (substType->isExistentialType()) {
      assert(isa<SelfProtocolConformance>(conformance.getConcrete()) &&
              "Existential type cannot have normal conformance");
    }
  }
#endif
}

void SubstitutionMap::profile(llvm::FoldingSetNodeID &id) const {
  id.AddPointer(storage);
}

bool SubstitutionMap::isIdentity() const {
  if (empty())
    return true;

  GenericSignature sig = getGenericSignature();
  bool hasNonIdentityReplacement = false;
  auto replacements = getReplacementTypesBuffer();

  sig->forEachParam([&](GenericTypeParamType *paramTy, bool isCanonical) {
    if (isCanonical) {
      if (!paramTy->isEqual(replacements[0]))
        hasNonIdentityReplacement = true;
    }

    replacements = replacements.slice(1);
  });

  assert(replacements.empty());

  return !hasNonIdentityReplacement;
}

SubstitutionMap SubstitutionMap::mapIntoTypeExpansionContext(
    TypeExpansionContext context) const {
  ReplaceOpaqueTypesWithUnderlyingTypes replacer(
      context.getContext(), context.getResilienceExpansion(),
      context.isWholeModuleContext());
  return this->subst(replacer, replacer, SubstFlags::SubstituteOpaqueArchetypes);
}
