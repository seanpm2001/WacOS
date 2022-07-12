//===--- ProtocolConformance.cpp - AST Protocol Conformance ---------------===//
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
// This file implements the protocol conformance data structures.
//
//===----------------------------------------------------------------------===//

#include "ConformanceLookupTable.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Decl.h"
#include "swift/AST/LazyResolver.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Module.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/Substitution.h"
#include "swift/AST/Types.h"
#include "swift/AST/TypeWalker.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/SaveAndRestore.h"

#define DEBUG_TYPE "AST"

STATISTIC(NumConformanceLookupTables, "# of conformance lookup tables built");

using namespace swift;

Witness::Witness(ValueDecl *decl, SubstitutionList substitutions,
                 GenericEnvironment *syntheticEnv,
                 SubstitutionList reqToSynthesizedEnvSubs) {
  auto &ctx = decl->getASTContext();

  auto declRef = ConcreteDeclRef(ctx, decl, substitutions);
  auto storedMem = ctx.Allocate(sizeof(StoredWitness), alignof(StoredWitness));
  auto stored = new (storedMem)
      StoredWitness{declRef, syntheticEnv,
                    ctx.AllocateCopy(reqToSynthesizedEnvSubs)};
  ctx.addDestructorCleanup(*stored);

  storage = stored;
}

void Witness::dump() const { dump(llvm::errs()); }

void Witness::dump(llvm::raw_ostream &out) const {
  // FIXME: Implement!
}

ProtocolConformanceRef::ProtocolConformanceRef(ProtocolDecl *protocol,
                                               ProtocolConformance *conf) {
  assert(protocol != nullptr &&
         "cannot construct ProtocolConformanceRef with null protocol");
  if (conf) {
    assert(protocol == conf->getProtocol() && "protocol conformance mismatch");
    Union = conf;
  } else {
    Union = protocol;
  }
}

ProtocolDecl *ProtocolConformanceRef::getRequirement() const {
  if (isConcrete()) {
    return getConcrete()->getProtocol();
  } else {
    return getAbstract();
  }
}

ProtocolConformanceRef
ProtocolConformanceRef::subst(Type origType,
                              TypeSubstitutionFn subs,
                              LookupConformanceFn conformances) const {
  auto substType = origType.subst(subs, conformances,
                                  SubstFlags::UseErrorType);

  // If we have a concrete conformance, we need to substitute the
  // conformance to apply to the new type.
  if (isConcrete()) {
    auto concrete = getConcrete();
    if (auto classDecl = concrete->getType()->getClassOrBoundGenericClass()) {
      // If this is a class, we need to traffic in the actual type that
      // implements the protocol, not 'Self' and not any subclasses (with their
      // inherited conformances).
      substType =
          substType->eraseDynamicSelfType()->getSuperclassForDecl(classDecl);
    }
    return ProtocolConformanceRef(
      getConcrete()->subst(substType, subs, conformances));
  }

  // Opened existentials trivially conform and do not need to go through
  // substitution map lookup.
  if (substType->isOpenedExistential())
    return *this;

  // If the substituted type is an existential, we have a self-conforming
  // existential being substituted in place of itself. There's no
  // conformance information in this case, so just return.
  if (substType->isObjCExistentialType())
    return *this;

  auto *proto = getRequirement();

  // Check the conformance map.
  if (auto result = conformances(origType->getCanonicalType(),
                                 substType,
                                 proto->getDeclaredType())) {
    return *result;
  }

  llvm_unreachable("Invalid conformance substitution");
}

Type
ProtocolConformanceRef::getTypeWitnessByName(Type type,
                                             ProtocolConformanceRef conformance,
                                             Identifier name,
                                             LazyResolver *resolver) {
  // For an archetype, retrieve the nested type with the appropriate
  // name. There are no conformance tables.
  if (auto archetype = type->getAs<ArchetypeType>()) {
    return archetype->getNestedType(name);
  }

  // Find the named requirement.
  AssociatedTypeDecl *assocType = nullptr;
  auto members = conformance.getRequirement()->lookupDirect(name);
  for (auto member : members) {
    assocType = dyn_cast<AssociatedTypeDecl>(member);
    if (assocType)
      break;
  }

  // FIXME: Shouldn't this be a hard error?
  if (!assocType)
    return nullptr;

  if (conformance.isAbstract())
    return DependentMemberType::get(type, assocType);

  auto concrete = conformance.getConcrete();
  if (!concrete->hasTypeWitness(assocType, resolver)) {
    return nullptr;
  }
  return concrete->getTypeWitness(assocType, resolver);
}

void *ProtocolConformance::operator new(size_t bytes, ASTContext &context,
                                        AllocationArena arena,
                                        unsigned alignment) {
  return context.Allocate(bytes, alignment, arena);

}

#define CONFORMANCE_SUBCLASS_DISPATCH(Method, Args)                          \
switch (getKind()) {                                                         \
  case ProtocolConformanceKind::Normal:                                      \
    static_assert(&ProtocolConformance::Method !=                            \
                    &NormalProtocolConformance::Method,                      \
                  "Must override NormalProtocolConformance::" #Method);      \
    return cast<NormalProtocolConformance>(this)->Method Args;               \
  case ProtocolConformanceKind::Specialized:                                 \
    static_assert(&ProtocolConformance::Method !=                            \
                    &SpecializedProtocolConformance::Method,                 \
                  "Must override SpecializedProtocolConformance::" #Method); \
    return cast<SpecializedProtocolConformance>(this)->Method Args;          \
  case ProtocolConformanceKind::Inherited:                                   \
    static_assert(&ProtocolConformance::Method !=                            \
                    &InheritedProtocolConformance::Method,                   \
                  "Must override InheritedProtocolConformance::" #Method);   \
    return cast<InheritedProtocolConformance>(this)->Method Args;            \
}                                                                            \
llvm_unreachable("bad ProtocolConformanceKind");

/// Get the protocol being conformed to.
ProtocolDecl *ProtocolConformance::getProtocol() const {
  CONFORMANCE_SUBCLASS_DISPATCH(getProtocol, ())
}

DeclContext *ProtocolConformance::getDeclContext() const {
  CONFORMANCE_SUBCLASS_DISPATCH(getDeclContext, ())
}

/// Retrieve the state of this conformance.
ProtocolConformanceState ProtocolConformance::getState() const {
  CONFORMANCE_SUBCLASS_DISPATCH(getState, ())
}

bool
ProtocolConformance::hasTypeWitness(AssociatedTypeDecl *assocType,
                                    LazyResolver *resolver) const {
  CONFORMANCE_SUBCLASS_DISPATCH(hasTypeWitness, (assocType, resolver));
}

std::pair<Type, TypeDecl *>
ProtocolConformance::getTypeWitnessAndDecl(AssociatedTypeDecl *assocType,
                                           LazyResolver *resolver,
                                           SubstOptions options) const {
  CONFORMANCE_SUBCLASS_DISPATCH(getTypeWitnessAndDecl,
                                (assocType, resolver, options))
}

Type ProtocolConformance::getTypeWitness(AssociatedTypeDecl *assocType,
                                         LazyResolver *resolver,
                                         SubstOptions options) const {
  return getTypeWitnessAndDecl(assocType, resolver, options).first;
}

ConcreteDeclRef
ProtocolConformance::getWitnessDeclRef(ValueDecl *requirement,
                                       LazyResolver *resolver) const {
  CONFORMANCE_SUBCLASS_DISPATCH(getWitnessDeclRef, (requirement, resolver))
}

ValueDecl *ProtocolConformance::getWitnessDecl(ValueDecl *requirement,
                                               LazyResolver *resolver) const {
  switch (getKind()) {
  case ProtocolConformanceKind::Normal:
    return cast<NormalProtocolConformance>(this)->getWitness(requirement,
                                                             resolver)
      .getDecl();

  case ProtocolConformanceKind::Inherited:
    return cast<InheritedProtocolConformance>(this)
      ->getInheritedConformance()->getWitnessDecl(requirement, resolver);

  case ProtocolConformanceKind::Specialized:
    return cast<SpecializedProtocolConformance>(this)
      ->getGenericConformance()->getWitnessDecl(requirement, resolver);
  }
}

/// Determine whether the witness for the given requirement
/// is either the default definition or was otherwise deduced.
bool ProtocolConformance::
usesDefaultDefinition(AssociatedTypeDecl *requirement) const {
  CONFORMANCE_SUBCLASS_DISPATCH(usesDefaultDefinition, (requirement))
}

GenericEnvironment *ProtocolConformance::getGenericEnvironment() const {
  switch (getKind()) {
  case ProtocolConformanceKind::Inherited:
  case ProtocolConformanceKind::Normal:
    // If we have a normal or inherited protocol conformance, look for its
    // generic parameters.
    return getDeclContext()->getGenericEnvironmentOfContext();

  case ProtocolConformanceKind::Specialized:
    // If we have a specialized protocol conformance, since we do not support
    // currently partial specialization, we know that it cannot have any open
    // type variables.
    //
    // FIXME: We could return a meaningful GenericEnvironment here
    return nullptr;
  }

  llvm_unreachable("Unhandled ProtocolConformanceKind in switch.");
}

GenericSignature *ProtocolConformance::getGenericSignature() const {
  switch (getKind()) {
  case ProtocolConformanceKind::Inherited:
  case ProtocolConformanceKind::Normal:
    // If we have a normal or inherited protocol conformance, look for its
    // generic signature.
    return getDeclContext()->getGenericSignatureOfContext();

  case ProtocolConformanceKind::Specialized:
    // If we have a specialized protocol conformance, since we do not support
    // currently partial specialization, we know that it cannot have any open
    // type variables.
    return nullptr;
  }

  llvm_unreachable("Unhandled ProtocolConformanceKind in switch.");
}

SubstitutionMap ProtocolConformance::getSubstitutions(ModuleDecl *M) const {
  // Walk down to the base NormalProtocolConformance.
  SubstitutionMap subMap;
  const ProtocolConformance *parent = this;
  while (!isa<NormalProtocolConformance>(parent)) {
    switch (parent->getKind()) {
    case ProtocolConformanceKind::Normal:
      llvm_unreachable("should have exited the loop?!");
    case ProtocolConformanceKind::Inherited:
      parent =
          cast<InheritedProtocolConformance>(parent)->getInheritedConformance();
      break;
    case ProtocolConformanceKind::Specialized: {
      auto SC = cast<SpecializedProtocolConformance>(parent);
      parent = SC->getGenericConformance();
      assert(subMap.empty() && "multiple conformance specializations?!");
      subMap = SC->getSubstitutionMap();
      break;
    }
    }
  }

  // Found something; we're done!
  if (!subMap.empty())
    return subMap;

  // If the normal conformance is for a generic type, and we didn't hit a
  // specialized conformance, collect the substitutions from the generic type.
  // FIXME: The AST should do this for us.
  const NormalProtocolConformance *normalC =
      cast<NormalProtocolConformance>(parent);

  if (!normalC->getType()->isSpecialized())
    return SubstitutionMap();

  auto *DC = normalC->getDeclContext();
  return normalC->getType()->getContextSubstitutionMap(M, DC);
}

bool ProtocolConformance::isBehaviorConformance() const {
  return getRootNormalConformance()->isBehaviorConformance();
}

AbstractStorageDecl *ProtocolConformance::getBehaviorDecl() const {
  return getRootNormalConformance()->getBehaviorDecl();
}

ArrayRef<Requirement> ProtocolConformance::getConditionalRequirements() const {
  CONFORMANCE_SUBCLASS_DISPATCH(getConditionalRequirements, ());
}

ArrayRef<Requirement>
ProtocolConformanceRef::getConditionalRequirements() const {
  if (isConcrete())
    return getConcrete()->getConditionalRequirements();
  else
    // An abstract conformance is never conditional: any conditionality in the
    // concrete types that will eventually pass through this at runtime is
    // completely pre-checked and packaged up.
    return {};
}

void NormalProtocolConformance::differenceAndStoreConditionalRequirements() {
  assert(ConditionalRequirements.size() == 0 &&
         "should not recompute conditional requirements");
  auto &ctxt = getProtocol()->getASTContext();
  auto DC = getDeclContext();
  // Only conformances in extensions can be conditional
  if (!isa<ExtensionDecl>(DC))
    return;

  auto typeSig = DC->getAsNominalTypeOrNominalTypeExtensionContext()
                     ->getGenericSignature();
  auto extensionSig = DC->getGenericSignatureOfContext();

  // If the type is generic, the extension should be too, and vice versa.
  assert((bool)typeSig == (bool)extensionSig &&
         "unexpected generic-ness mismatch on conformance");
  if (!typeSig)
    return;

  auto canExtensionSig = extensionSig->getCanonicalSignature();
  auto canTypeSig = typeSig->getCanonicalSignature();
  if (canTypeSig == canExtensionSig)
    return;

  // The extension signature should be a superset of the type signature, meaning
  // every thing in the type signature either is included too or is implied by
  // something else. The most important bit is having the same type
  // parameters. (NB. if/when Swift gets parameterized extensions, this needs to
  // change.)
  assert(canTypeSig.getGenericParams() == canExtensionSig.getGenericParams());

  // Find the requirements in the extension that aren't proved by the original
  // type, these are the ones that make the conformance conditional.
  ConditionalRequirements =
      ctxt.AllocateCopy(extensionSig->requirementsNotSatisfiedBy(typeSig));
}

void NormalProtocolConformance::setSignatureConformances(
                               ArrayRef<ProtocolConformanceRef> conformances) {
  if (conformances.empty()) {
    SignatureConformances = { };
    return;
  }

  auto &ctx = getProtocol()->getASTContext();
  SignatureConformances = ctx.AllocateCopy(conformances);

#if !NDEBUG
  unsigned idx = 0;
  for (const auto &req : getProtocol()->getRequirementSignature()) {
    if (req.getKind() == RequirementKind::Conformance) {
      assert(!conformances[idx].isConcrete() ||
             !conformances[idx].getConcrete()->getType()->hasArchetype() &&
             "Should have interface types here");
      assert(idx < conformances.size());
      assert(conformances[idx].getRequirement() ==
               req.getSecondType()->castTo<ProtocolType>()->getDecl());
      ++idx;
    }
  }
  assert(idx == conformances.size() && "Too many conformances");
#endif
}

std::function<void(ProtocolConformanceRef)>
NormalProtocolConformance::populateSignatureConformances() {
  assert(SignatureConformances.empty());

  class Writer {
    NormalProtocolConformance *self;
    ArrayRef<Requirement> requirementSignature;
    MutableArrayRef<ProtocolConformanceRef> buffer;
    mutable bool owning = true;

    /// Skip any non-conformance requirements in the requirement signature.
    void skipNonConformanceRequirements() {
      while (!requirementSignature.empty() &&
             requirementSignature.front().getKind()
               != RequirementKind::Conformance)
        requirementSignature = requirementSignature.drop_front();
    }

  public:
    Writer(NormalProtocolConformance *self) : self(self) {
      requirementSignature = self->getProtocol()->getRequirementSignature();

      // Determine the number of conformance requirements we need.
      unsigned numConformanceRequirements = 0;
      for (const auto &req : requirementSignature) {
        if (req.getKind() == RequirementKind::Conformance)
          ++numConformanceRequirements;
      }

      // Allocate the buffer of conformance requirements.
      auto &ctx = self->getProtocol()->getASTContext();
      buffer = ctx.AllocateUninitialized<ProtocolConformanceRef>(numConformanceRequirements);

      // Skip over any non-conformance requirements in the requirement
      // signature.
      skipNonConformanceRequirements();
    };

    Writer(Writer &&other)
      : self(other.self),
        requirementSignature(other.requirementSignature),
        buffer(other.buffer)
    {
      other.owning = false;
    }

    Writer(const Writer &other)
      : self(other.self),
        requirementSignature(other.requirementSignature),
        buffer(other.buffer) {
      other.owning = false;
    }

    void operator()(ProtocolConformanceRef conformance){
      // Make sure we have the right conformance.
      assert(!requirementSignature.empty() && "Too many conformances?");
      assert(conformance.getRequirement() ==
               requirementSignature.front().getSecondType()->castTo<ProtocolType>()->getDecl());
      assert((!conformance.isConcrete() ||
              !conformance.getConcrete()->getType()->hasArchetype()) &&
             "signature conformances must use interface types");
      // Add this conformance to the known signature conformances.
      requirementSignature = requirementSignature.drop_front();
      new (&buffer[self->SignatureConformances.size()])
        ProtocolConformanceRef(conformance);
      self->SignatureConformances =
        buffer.slice(0, self->SignatureConformances.size() + 1);

      // Skip over any non-conformance requirements.
      skipNonConformanceRequirements();
    }
  };

  return Writer(this);
}

void NormalProtocolConformance::resolveLazyInfo() const {
  assert(Loader);

  auto *loader = Loader;
  auto *mutableThis = const_cast<NormalProtocolConformance *>(this);
  mutableThis->Loader = nullptr;
  loader->finishNormalConformance(mutableThis, LoaderContextData);
}

void NormalProtocolConformance::setLazyLoader(LazyConformanceLoader *loader,
                                              uint64_t contextData) {
  assert(!Loader && "already has a loader");
  Loader = loader;
  LoaderContextData = contextData;
}

namespace {
  class PrettyStackTraceRequirement : public llvm::PrettyStackTraceEntry {
    const char *Action;
    const ProtocolConformance *Conformance;
    ValueDecl *Requirement;
  public:
    PrettyStackTraceRequirement(const char *action,
                                const ProtocolConformance *conformance,
                                ValueDecl *requirement)
      : Action(action), Conformance(conformance), Requirement(requirement) { }

    void print(llvm::raw_ostream &out) const override {
      out << "While " << Action << " requirement ";
      Requirement->dumpRef(out);
      out << " in conformance ";
      Conformance->printName(out);
      out << "\n";
    }
  };
} // end anonymous namespace

bool NormalProtocolConformance::hasTypeWitness(AssociatedTypeDecl *assocType,
                                               LazyResolver *resolver) const {
  if (Loader)
    resolveLazyInfo();

  auto found = TypeWitnesses.find(assocType);
  if (found != TypeWitnesses.end()) {
    return !found->getSecond().first.isNull();
  }
  if (resolver) {
    PrettyStackTraceRequirement trace("resolving", this, assocType);
    resolver->resolveTypeWitness(this, assocType);
    if (TypeWitnesses.find(assocType) != TypeWitnesses.end()) {
      return true;
    }
  }
  return false;
}

using TypeWitnessAndDecl = std::pair<Type, TypeDecl *>;
TypeWitnessAndDecl
NormalProtocolConformance::getTypeWitnessAndDecl(AssociatedTypeDecl *assocType,
                                                 LazyResolver *resolver,
                                                 SubstOptions options) const {
  if (Loader)
    resolveLazyInfo();

  // Check whether we already have a type witness.
  auto known = TypeWitnesses.find(assocType);
  if (known != TypeWitnesses.end())
    return known->second;

  // If there is a tentative-type-witness function, use it.
  if (options.getTentativeTypeWitness) {
   if (Type witnessType =
         Type(options.getTentativeTypeWitness(this, assocType)))
     return { witnessType, nullptr };
  }

  // If this conformance is in a state where it is inferring type witnesses but
  // we didn't find anything, fail.
  if (getState() == ProtocolConformanceState::CheckingTypeWitnesses) {
    return { Type(), nullptr };
  }

  // Otherwise, resolve the type witness.
  PrettyStackTraceRequirement trace("resolving", this, assocType);
  assert(resolver && "Unable to resolve type witness");

  // Block recursive resolution of this type witness.
  TypeWitnesses[assocType] = { Type(), nullptr };
  resolver->resolveTypeWitness(this, assocType);

  known = TypeWitnesses.find(assocType);
  assert(known != TypeWitnesses.end() && "Didn't resolve witness?");
  return known->second;
}

void NormalProtocolConformance::setTypeWitness(AssociatedTypeDecl *assocType,
                                               Type type,
                                               TypeDecl *typeDecl) const {
  assert(getProtocol() == cast<ProtocolDecl>(assocType->getDeclContext()) &&
         "associated type in wrong protocol");
  assert((TypeWitnesses.count(assocType) == 0 ||
          TypeWitnesses[assocType].first.isNull()) &&
         "Type witness already known");
  assert((!isComplete() || isInvalid()) && "Conformance already complete?");
  assert(!type->hasArchetype() && "type witnesses must be interface types");
  TypeWitnesses[assocType] = std::make_pair(type, typeDecl);
}

Type ProtocolConformance::getAssociatedType(Type assocType,
                                            LazyResolver *resolver) const {
  assert(assocType->isTypeParameter() &&
         "associated type must be a type parameter");

  ProtocolConformanceRef ref(const_cast<ProtocolConformance*>(this));
  return ref.getAssociatedType(getType(), assocType, resolver);
}

Type ProtocolConformanceRef::getAssociatedType(Type conformingType,
                                               Type assocType,
                                               LazyResolver *resolver) const {
  assert(!isConcrete() || getConcrete()->getType()->isEqual(conformingType));

  auto type = assocType->getCanonicalType();
  auto proto = getRequirement();

  // Fast path for generic parameters.
  if (isa<GenericTypeParamType>(type)) {
    assert(type->isEqual(proto->getSelfInterfaceType()) &&
           "type parameter in protocol was not Self");
    return conformingType;
  }

  // Fast path for dependent member types on 'Self' of our associated types.
  auto memberType = cast<DependentMemberType>(type);
  if (memberType.getBase()->isEqual(proto->getProtocolSelfType()) &&
      memberType->getAssocType()->getProtocol() == proto &&
      isConcrete())
    return getConcrete()->getTypeWitness(memberType->getAssocType(), resolver);

  // General case: consult the substitution map.
  auto substMap =
    SubstitutionMap::getProtocolSubstitutions(proto, conformingType, *this);
  return type.subst(substMap);
}

ProtocolConformanceRef
ProtocolConformanceRef::getAssociatedConformance(Type conformingType,
                                                 Type assocType,
                                                 ProtocolDecl *protocol,
                                                 LazyResolver *resolver) const {
  // If this is a concrete conformance, look up the associated conformance.
  if (isConcrete()) {
    auto conformance = getConcrete();
    assert(conformance->getType()->isEqual(conformingType));
    return conformance->getAssociatedConformance(assocType, protocol, resolver);
  }

  // Otherwise, apply the substitution {self -> conformingType}
  // to the abstract conformance requirement laid upon the dependent type
  // by the protocol.
  auto subMap =
    SubstitutionMap::getProtocolSubstitutions(getRequirement(),
                                              conformingType, *this);
  auto abstractConf = ProtocolConformanceRef(protocol);
  return abstractConf.subst(assocType,
                            QuerySubstitutionMap{subMap},
                            LookUpConformanceInSubstitutionMap(subMap));
}

ProtocolConformanceRef
ProtocolConformance::getAssociatedConformance(Type assocType,
                                               ProtocolDecl *protocol,
                                               LazyResolver *resolver) const {
  CONFORMANCE_SUBCLASS_DISPATCH(getAssociatedConformance,
                                (assocType, protocol, resolver))
}

ProtocolConformanceRef
NormalProtocolConformance::getAssociatedConformance(Type assocType,
                                                    ProtocolDecl *protocol,
                                                LazyResolver *resolver) const {
  assert(assocType->isTypeParameter() &&
         "associated type must be a type parameter");
  assert(!getSignatureConformances().empty() &&
         "signature conformances not yet computed");

  unsigned conformanceIndex = 0;
  for (const auto &reqt : getProtocol()->getRequirementSignature()) {
    if (reqt.getKind() == RequirementKind::Conformance) {
      // Is this the conformance we're looking for?
      if (reqt.getFirstType()->isEqual(assocType) &&
          reqt.getSecondType()->castTo<ProtocolType>()->getDecl() == protocol)
        return getSignatureConformances()[conformanceIndex];

      ++conformanceIndex;
    }
  }

  llvm_unreachable(
    "requested conformance was not a direct requirement of the protocol");
}

/// Retrieve the value witness corresponding to the given requirement.
Witness NormalProtocolConformance::getWitness(ValueDecl *requirement,
                                              LazyResolver *resolver) const {
  assert(!isa<AssociatedTypeDecl>(requirement) && "Request type witness");
  assert(requirement->isProtocolRequirement() && "Not a requirement");

  if (Loader)
    resolveLazyInfo();

  auto known = Mapping.find(requirement);
  if (known == Mapping.end()) {
    assert(resolver && "Unable to resolve witness without resolver");
    resolver->resolveWitness(this, requirement);
    known = Mapping.find(requirement);
  }
  if (known != Mapping.end()) {
    return known->second;
  } else {
    assert((!isComplete() || isInvalid()) &&
           "Resolver did not resolve requirement");
    return Witness();
  }
}

ConcreteDeclRef
NormalProtocolConformance::getWitnessDeclRef(ValueDecl *requirement,
                                             LazyResolver *resolver) const {
  if (auto witness = getWitness(requirement, resolver))
    return witness.getDeclRef();
  return ConcreteDeclRef();
}

void NormalProtocolConformance::setWitness(ValueDecl *requirement,
                                           Witness witness) const {
  assert(!isa<AssociatedTypeDecl>(requirement) && "Request type witness");
  assert(getProtocol() == cast<ProtocolDecl>(requirement->getDeclContext()) &&
         "requirement in wrong protocol");
  assert(Mapping.count(requirement) == 0 && "Witness already known");
  assert((!isComplete() || isInvalid() ||
          requirement->getAttrs().hasAttribute<OptionalAttr>() ||
          requirement->getAttrs().isUnavailable(
                                        requirement->getASTContext())) &&
         "Conformance already complete?");
  Mapping[requirement] = witness;
}

SpecializedProtocolConformance::SpecializedProtocolConformance(
    Type conformingType,
    ProtocolConformance *genericConformance,
    SubstitutionList substitutions)
  : ProtocolConformance(ProtocolConformanceKind::Specialized, conformingType),
    GenericConformance(genericConformance),
    GenericSubstitutions(substitutions)
{
  assert(genericConformance->getKind() != ProtocolConformanceKind::Specialized);

  if (!GenericConformance->getConditionalRequirements().empty()) {
    // Substitute the conditional requirements so that they're phrased in
    // terms of the specialized types, not the conformance-declaring decl's
    // types.
    auto nominal = GenericConformance->getType()->getAnyNominal();
    auto module = nominal->getModuleContext();
    auto subMap = getType()->getContextSubstitutionMap(module, nominal);

    SmallVector<Requirement, 4> newReqs;
    for (auto oldReq : GenericConformance->getConditionalRequirements()) {
      if (auto newReq = oldReq.subst(QuerySubstitutionMap{subMap},
                                     LookUpConformanceInModule(module)))
        newReqs.push_back(*newReq);
    }
    auto &ctxt = getProtocol()->getASTContext();
    ConditionalRequirements = ctxt.AllocateCopy(newReqs);
  }
}

SubstitutionMap SpecializedProtocolConformance::getSubstitutionMap() const {
  auto *genericSig = GenericConformance->getGenericSignature();
  if (genericSig)
    return genericSig->getSubstitutionMap(GenericSubstitutions);

  return SubstitutionMap();
}

bool SpecializedProtocolConformance::hasTypeWitness(
                      AssociatedTypeDecl *assocType, 
                      LazyResolver *resolver) const {
  return TypeWitnesses.find(assocType) != TypeWitnesses.end() ||
         GenericConformance->hasTypeWitness(assocType, resolver);
}

std::pair<Type, TypeDecl *>
SpecializedProtocolConformance::getTypeWitnessAndDecl(
                      AssociatedTypeDecl *assocType, 
                      LazyResolver *resolver,
                      SubstOptions options) const {
  // If we've already created this type witness, return it.
  auto known = TypeWitnesses.find(assocType);
  if (known != TypeWitnesses.end()) {
    return known->second;
  }

  // Otherwise, perform substitutions to create this witness now.

  // Local function to determine whether we will end up referring to a
  // tentative witness that may not be chosen.
  auto normal = GenericConformance->getRootNormalConformance();
  auto isTentativeWitness = [&] {
    if (normal->getState() != ProtocolConformanceState::CheckingTypeWitnesses)
      return false;

    return !normal->hasTypeWitness(assocType, nullptr);
  };

  auto genericWitnessAndDecl
    = GenericConformance->getTypeWitnessAndDecl(assocType, resolver, options);

  auto genericWitness = genericWitnessAndDecl.first;
  if (!genericWitness)
    return { Type(), nullptr };

  auto *typeDecl = genericWitnessAndDecl.second;

  // Form the substitution.
  auto substitutionMap = getSubstitutionMap();
  if (substitutionMap.empty())
    return {Type(), nullptr};

  // Apply the substitution we computed above
  auto specializedType = genericWitness.subst(substitutionMap, options);
  if (!specializedType) {
    if (isTentativeWitness())
      return { Type(), nullptr };

    specializedType = ErrorType::get(genericWitness);
  }

  // If we aren't in a case where we used the tentative type witness
  // information, cache the result.
  auto specializedWitnessAndDecl = std::make_pair(specializedType, typeDecl);
  if (!isTentativeWitness() && !specializedType->hasError())
    TypeWitnesses[assocType] = specializedWitnessAndDecl;

  return specializedWitnessAndDecl;
}

ProtocolConformanceRef
SpecializedProtocolConformance::getAssociatedConformance(Type assocType,
                                                ProtocolDecl *protocol,
                                                LazyResolver *resolver) const {
  ProtocolConformanceRef conformance =
    GenericConformance->getAssociatedConformance(assocType, protocol, resolver);

  auto subMap = getSubstitutionMap();

  Type origType =
    (conformance.isConcrete()
       ? conformance.getConcrete()->getType()
       : GenericConformance->getAssociatedType(assocType, resolver));

  return conformance.subst(origType,
                           QuerySubstitutionMap{subMap},
                           LookUpConformanceInSubstitutionMap(subMap));
}

ConcreteDeclRef
SpecializedProtocolConformance::getWitnessDeclRef(ValueDecl *requirement,
                                                  LazyResolver *resolver) const {
  auto baseWitness = GenericConformance->getWitnessDeclRef(requirement, resolver);
  if (!baseWitness || !baseWitness.isSpecialized())
    return baseWitness;

  auto specializationMap = getSubstitutionMap();

  auto witnessDecl = baseWitness.getDecl();
  auto witnessSig =
    witnessDecl->getInnermostDeclContext()->getGenericSignatureOfContext();
  auto witnessMap =
    witnessSig->getSubstitutionMap(baseWitness.getSubstitutions());

  auto combinedMap = witnessMap.subst(specializationMap);

  SmallVector<Substitution, 4> substSubs;
  witnessSig->getSubstitutions(combinedMap, substSubs);

  // Fast path if the substitutions didn't change.
  if (SubstitutionList(substSubs) == baseWitness.getSubstitutions())
    return baseWitness;

  return ConcreteDeclRef(witnessDecl->getASTContext(), witnessDecl, substSubs);
}

ProtocolConformanceRef
InheritedProtocolConformance::getAssociatedConformance(Type assocType,
                         ProtocolDecl *protocol,
                         LazyResolver *resolver) const {
  auto underlying =
    InheritedConformance->getAssociatedConformance(assocType, protocol,
                                                   resolver);


  // If the conformance is for Self, return an inherited conformance.
  if (underlying.isConcrete() &&
      assocType->isEqual(getProtocol()->getSelfInterfaceType())) {
    auto subclassType = getType();
    ASTContext &ctx = subclassType->getASTContext();
    return ProtocolConformanceRef(
             ctx.getInheritedConformance(subclassType,
                                         underlying.getConcrete()));
  }

  return underlying;
}

ConcreteDeclRef
InheritedProtocolConformance::getWitnessDeclRef(ValueDecl *requirement,
                                                LazyResolver *resolver) const {
  // FIXME: substitutions?
  return InheritedConformance->getWitnessDeclRef(requirement, resolver);
}

const NormalProtocolConformance *
ProtocolConformance::getRootNormalConformance() const {
  const ProtocolConformance *C = this;
  while (!isa<NormalProtocolConformance>(C)) {
    switch (C->getKind()) {
    case ProtocolConformanceKind::Normal:
      llvm_unreachable("should have broken out of loop");
    case ProtocolConformanceKind::Inherited:
      C = cast<InheritedProtocolConformance>(C)
          ->getInheritedConformance();
      break;
    case ProtocolConformanceKind::Specialized:
      C = cast<SpecializedProtocolConformance>(C)
        ->getGenericConformance();
      break;
    }
  }
  return cast<NormalProtocolConformance>(C);
}

bool ProtocolConformance::witnessTableAccessorRequiresArguments() const {
  return getRootNormalConformance()->getDeclContext()->isGenericContext();
}

bool ProtocolConformance::isVisibleFrom(const DeclContext *dc) const {
  // FIXME: Implement me!
  return true;
}

ProtocolConformance *
ProtocolConformance::subst(Type substType,
                           TypeSubstitutionFn subs,
                           LookupConformanceFn conformances) const {
  // ModuleDecl::lookupConformance() strips off dynamic Self, so
  // we should do the same here.
  if (auto selfType = substType->getAs<DynamicSelfType>())
    substType = selfType->getSelfType();

  if (getType()->isEqual(substType))
    return const_cast<ProtocolConformance *>(this);
  
  switch (getKind()) {
  case ProtocolConformanceKind::Normal: {
    if (substType->isSpecialized()) {
      assert(getType()->isSpecialized()
             && "substitution mapped non-specialized to specialized?!");
      assert(getType()->getNominalOrBoundGenericNominal()
               == substType->getNominalOrBoundGenericNominal()
             && "substitution mapped to different nominal?!");

      SubstitutionMap subMap;
      if (auto *genericSig = getGenericSignature())
        subMap = genericSig->getSubstitutionMap(subs, conformances);

      return substType->getASTContext()
        .getSpecializedConformance(substType,
                                   const_cast<ProtocolConformance *>(this),
                                   subMap);
    }

    assert(substType->isEqual(getType())
           && "substitution changed non-specialized type?!");
    return const_cast<ProtocolConformance *>(this);
  }
  case ProtocolConformanceKind::Inherited: {
    // Substitute the base.
    auto inheritedConformance
      = cast<InheritedProtocolConformance>(this)->getInheritedConformance();
    ProtocolConformance *newBase;
    if (inheritedConformance->getType()->isSpecialized()) {
      // Follow the substituted type up the superclass chain until we reach
      // the underlying class type.
      auto targetClass =
        inheritedConformance->getType()->getClassOrBoundGenericClass();
      auto superclassType = substType->getSuperclassForDecl(targetClass);

      // Substitute into the superclass.
      newBase = inheritedConformance->subst(superclassType, subs, conformances);
    } else {
      newBase = inheritedConformance;
    }

    return substType->getASTContext()
      .getInheritedConformance(substType, newBase);
  }
  case ProtocolConformanceKind::Specialized: {
    // Substitute the substitutions in the specialized conformance.
    auto spec = cast<SpecializedProtocolConformance>(this);
    auto genericConformance = spec->getGenericConformance();
    auto subMap = spec->getSubstitutionMap();

    return substType->getASTContext()
      .getSpecializedConformance(substType, genericConformance,
                                 subMap.subst(subs, conformances));
  }
  }
  llvm_unreachable("bad ProtocolConformanceKind");
}

ProtocolConformance *
ProtocolConformance::getInheritedConformance(ProtocolDecl *protocol) const {
  auto result =
    getAssociatedConformance(getProtocol()->getSelfInterfaceType(), protocol);
  return result.isConcrete() ? result.getConcrete() : nullptr;
}

#pragma mark Protocol conformance lookup
void NominalTypeDecl::prepareConformanceTable() const {
  if (ConformanceTable)
    return;

  auto mutableThis = const_cast<NominalTypeDecl *>(this);
  ASTContext &ctx = getASTContext();
  auto resolver = ctx.getLazyResolver();
  ConformanceTable = new (ctx) ConformanceLookupTable(ctx, resolver);
  ++NumConformanceLookupTables;

  // If this type declaration was not parsed from source code or introduced
  // via the Clang importer, don't add any synthesized conformances.
  auto *file = cast<FileUnit>(getModuleScopeContext());
  if (file->getKind() != FileUnitKind::Source &&
      file->getKind() != FileUnitKind::ClangModule) {
    return;
  }

  SmallPtrSet<ProtocolDecl *, 2> protocols;

  auto addSynthesized = [&](KnownProtocolKind kind) {
    if (auto *proto = getASTContext().getProtocol(kind)) {
      if (protocols.count(proto) == 0) {
        ConformanceTable->addSynthesizedConformance(mutableThis, proto);
        protocols.insert(proto);
      }
    }
  };

  // Add protocols for any synthesized protocol attributes.
  for (auto attr : getAttrs().getAttributes<SynthesizedProtocolAttr>()) {
    addSynthesized(attr->getProtocolKind());
  }

  // Add any implicit conformances.
  if (auto theEnum = dyn_cast<EnumDecl>(mutableThis)) {
    if (theEnum->hasCases() && theEnum->hasOnlyCasesWithoutAssociatedValues()) {
      // Simple enumerations conform to Equatable.
      addSynthesized(KnownProtocolKind::Equatable);

      // Simple enumerations conform to Hashable.
      addSynthesized(KnownProtocolKind::Hashable);
    }

    // Enumerations with a raw type conform to RawRepresentable.
    if (resolver)
      resolver->resolveRawType(theEnum);
    if (theEnum->hasRawType()) {
      addSynthesized(KnownProtocolKind::RawRepresentable);
    }
  }
}

bool NominalTypeDecl::lookupConformance(
       ModuleDecl *module, ProtocolDecl *protocol,
       SmallVectorImpl<ProtocolConformance *> &conformances) const {
  prepareConformanceTable();
  return ConformanceTable->lookupConformance(
           module,
           const_cast<NominalTypeDecl *>(this),
           protocol,
           getASTContext().getLazyResolver(),
           conformances);
}

SmallVector<ProtocolDecl *, 2> NominalTypeDecl::getAllProtocols() const {
  prepareConformanceTable();
  SmallVector<ProtocolDecl *, 2> result;
  ConformanceTable->getAllProtocols(const_cast<NominalTypeDecl *>(this),
                                    getASTContext().getLazyResolver(),
                                    result);
  return result;
}

SmallVector<ProtocolConformance *, 2> NominalTypeDecl::getAllConformances(
                                        bool sorted) const
{
  prepareConformanceTable();
  SmallVector<ProtocolConformance *, 2> result;
  ConformanceTable->getAllConformances(const_cast<NominalTypeDecl *>(this),
                                       getASTContext().getLazyResolver(),
                                       sorted,
                                       result);
  return result;
}

void NominalTypeDecl::getImplicitProtocols(
       SmallVectorImpl<ProtocolDecl *> &protocols) {
  prepareConformanceTable();
  ConformanceTable->getImplicitProtocols(this, protocols);
}

void NominalTypeDecl::registerProtocolConformance(
       ProtocolConformance *conformance) {
  prepareConformanceTable();
  ConformanceTable->registerProtocolConformance(conformance);
}

ArrayRef<ValueDecl *>
NominalTypeDecl::getSatisfiedProtocolRequirementsForMember(
                                             const ValueDecl *member,
                                             bool sorted) const {
  assert(member->getDeclContext()->getAsNominalTypeOrNominalTypeExtensionContext()
           == this);
  assert(!isa<ProtocolDecl>(this));
  prepareConformanceTable();
  return ConformanceTable->getSatisfiedProtocolRequirementsForMember(member,
                                           const_cast<NominalTypeDecl *>(this),
                                           getASTContext().getLazyResolver(),
                                           sorted);
}

SmallVector<ProtocolDecl *, 2>
DeclContext::getLocalProtocols(
  ConformanceLookupKind lookupKind,
  SmallVectorImpl<ConformanceDiagnostic> *diagnostics,
  bool sorted) const
{
  SmallVector<ProtocolDecl *, 2> result;

  // Dig out the nominal type.
  NominalTypeDecl *nominal = getAsNominalTypeOrNominalTypeExtensionContext();
  if (!nominal)
    return result;

  // Update to record all potential conformances.
  nominal->prepareConformanceTable();
  nominal->ConformanceTable->lookupConformances(
    nominal,
    const_cast<DeclContext *>(this),
    getASTContext().getLazyResolver(),
    lookupKind,
    &result,
    nullptr,
    diagnostics);

  // Sort if required.
  if (sorted) {
    llvm::array_pod_sort(result.begin(), result.end(),
                         &ProtocolType::compareProtocols);
  }

  return result;
}

SmallVector<ProtocolConformance *, 2>
DeclContext::getLocalConformances(
  ConformanceLookupKind lookupKind,
  SmallVectorImpl<ConformanceDiagnostic> *diagnostics,
  bool sorted) const
{
  SmallVector<ProtocolConformance *, 2> result;

  // Dig out the nominal type.
  NominalTypeDecl *nominal = getAsNominalTypeOrNominalTypeExtensionContext();
  if (!nominal)
    return result;

  // Protocols don't have conformances.
  if (isa<ProtocolDecl>(nominal))
    return { };

  // Update to record all potential conformances.
  nominal->prepareConformanceTable();
  nominal->ConformanceTable->lookupConformances(
    nominal,
    const_cast<DeclContext *>(this),
    nominal->getASTContext().getLazyResolver(),
    lookupKind,
    nullptr,
    &result,
    diagnostics);

  // If requested, sort the results.
  if (sorted) {
    llvm::array_pod_sort(result.begin(), result.end(),
                         &ConformanceLookupTable::compareProtocolConformances);
  }

  return result;
}

/// Check of all types used by the conformance are canonical.
bool ProtocolConformance::isCanonical() const {
  // Normal conformances are always canonical by construction.
  if (getKind() == ProtocolConformanceKind::Normal)
    return true;

  if (!getType()->isCanonical())
    return false;

  switch (getKind()) {
  case ProtocolConformanceKind::Normal: {
    return true;
  }
  case ProtocolConformanceKind::Inherited: {
    // Substitute the base.
    auto inheritedConformance
      = cast<InheritedProtocolConformance>(this);
    return inheritedConformance->getInheritedConformance()->isCanonical();
  }
  case ProtocolConformanceKind::Specialized: {
    // Substitute the substitutions in the specialized conformance.
    auto spec = cast<SpecializedProtocolConformance>(this);
    auto genericConformance = spec->getGenericConformance();
    if (!genericConformance->isCanonical())
      return false;
    auto specSubs = spec->getGenericSubstitutions();
    for (const auto &sub : specSubs) {
      if (!sub.isCanonical())
        return false;
    }
    return true;
  }
  }
  llvm_unreachable("bad ProtocolConformanceKind");
}

Substitution Substitution::getCanonicalSubstitution(bool *wasCanonical) const {
  bool createdNewCanonicalConformances = false;
  bool createdCanReplacement = false;
  SmallVector<ProtocolConformanceRef, 4> newCanConformances;

  CanType canReplacement = getReplacement()->getCanonicalType();

  if (!getReplacement()->isCanonical()) {
    createdCanReplacement = true;
  }

  for (auto conf : getConformances()) {
    if (conf.isCanonical()) {
      newCanConformances.push_back(conf);
      continue;
    }
    newCanConformances.push_back(conf.getCanonicalConformanceRef());
    createdNewCanonicalConformances = true;
  }

  ArrayRef<ProtocolConformanceRef> canConformances = getConformances();
  if (createdNewCanonicalConformances) {
    auto &C = canReplacement->getASTContext();
    canConformances = C.AllocateCopy(newCanConformances);
  }

  if (createdCanReplacement || createdNewCanonicalConformances) {
    if (wasCanonical)
      *wasCanonical = false;
    return Substitution(canReplacement, canConformances);
  }
  if (wasCanonical)
    *wasCanonical = true;
  return *this;
}

SubstitutionList
swift::getCanonicalSubstitutionList(SubstitutionList subs,
                                    SmallVectorImpl<Substitution> &canSubs) {
  bool subListWasCanonical = true;
  for (auto &sub : subs) {
    bool subWasCanonical = false;
    auto canSub = sub.getCanonicalSubstitution(&subWasCanonical);
    if (!subWasCanonical)
      subListWasCanonical = false;
    canSubs.push_back(canSub);
  }

  if (subListWasCanonical) {
    canSubs.clear();
    return subs;
  }

  subs = canSubs;
  return subs;
}

/// Check of all types used by the conformance are canonical.
ProtocolConformance *ProtocolConformance::getCanonicalConformance() {
  if (isCanonical())
    return this;

  switch (getKind()) {
  case ProtocolConformanceKind::Normal: {
    // Normal conformances are always canonical by construction.
    return this;
  }

  case ProtocolConformanceKind::Inherited: {
    auto &Ctx = getType()->getASTContext();
    auto inheritedConformance = cast<InheritedProtocolConformance>(this);
    return Ctx.getInheritedConformance(
        getType()->getCanonicalType(),
        inheritedConformance->getInheritedConformance()
            ->getCanonicalConformance());
  }

  case ProtocolConformanceKind::Specialized: {
    auto &Ctx = getType()->getASTContext();
    // Substitute the substitutions in the specialized conformance.
    auto spec = cast<SpecializedProtocolConformance>(this);
    auto genericConformance = spec->getGenericConformance();
    auto specSubs = spec->getGenericSubstitutions();
    SmallVector<Substitution, 4> newSpecSubs;
    auto canSpecSubs = getCanonicalSubstitutionList(specSubs, newSpecSubs);
    return Ctx.getSpecializedConformance(
        getType()->getCanonicalType(),
        genericConformance->getCanonicalConformance(),
        newSpecSubs.empty() ? canSpecSubs : Ctx.AllocateCopy(canSpecSubs));
  }
  }
  llvm_unreachable("bad ProtocolConformanceKind");
}

/// Check of all types used by the conformance are canonical.
bool ProtocolConformanceRef::isCanonical() const {
  if (isAbstract())
    return true;
  return getConcrete()->isCanonical();
}

ProtocolConformanceRef
ProtocolConformanceRef::getCanonicalConformanceRef() const {
  if (isAbstract())
    return *this;
  return ProtocolConformanceRef(getConcrete()->getCanonicalConformance());
}
