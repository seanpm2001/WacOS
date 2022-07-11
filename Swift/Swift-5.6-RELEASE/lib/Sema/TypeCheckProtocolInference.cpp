//===--- TypeCheckProtocolInference.cpp - Associated Type Inference -------===//
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
// This file implements semantic analysis for protocols, in particular, checking
// whether a given type conforms to a given protocol.
//===----------------------------------------------------------------------===//
#include "TypeCheckProtocol.h"
#include "DerivedConformances.h"
#include "TypeChecker.h"

#include "swift/AST/Decl.h"
#include "swift/AST/GenericSignature.h"
#include "swift/AST/NameLookupRequests.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/SubstitutionMap.h"
#include "swift/AST/TypeMatcher.h"
#include "swift/AST/Types.h"
#include "swift/Basic/Defer.h"
#include "swift/ClangImporter/ClangModule.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/TinyPtrVector.h"

#define DEBUG_TYPE "Associated type inference"
#include "llvm/Support/Debug.h"

STATISTIC(NumSolutionStates, "# of solution states visited");
STATISTIC(NumSolutionStatesFailedCheck,
          "# of solution states that failed constraints check");
STATISTIC(NumConstrainedExtensionChecks,
          "# of constrained extension checks");
STATISTIC(NumConstrainedExtensionChecksFailed,
          "# of constrained extension checks failed");
STATISTIC(NumDuplicateSolutionStates,
          "# of duplicate solution states ");

using namespace swift;

AbstractTypeWitness AbstractTypeWitness::forFixed(AssociatedTypeDecl *assocType,
                                                  Type type) {
  return AbstractTypeWitness(AbstractTypeWitnessKind::Fixed, assocType, type,
                             nullptr);
}

AbstractTypeWitness
AbstractTypeWitness::forDefault(AssociatedTypeDecl *assocType, Type type,
                                AssociatedTypeDecl *defaultedAssocType) {
  return AbstractTypeWitness(AbstractTypeWitnessKind::Default, assocType, type,
                             defaultedAssocType);
}

AbstractTypeWitness
AbstractTypeWitness::forGenericParam(AssociatedTypeDecl *assocType, Type type) {
  return AbstractTypeWitness(AbstractTypeWitnessKind::GenericParam, assocType,
                             type, nullptr);
}

void InferredAssociatedTypesByWitness::dump() const {
  dump(llvm::errs(), 0);
}

void InferredAssociatedTypesByWitness::dump(llvm::raw_ostream &out,
                                            unsigned indent) const {
  out << "\n";
  out.indent(indent) << "(";
  if (Witness) {
    Witness->dumpRef(out);
  }

  for (const auto &inferred : Inferred) {
    out << "\n";
    out.indent(indent + 2);
    out << inferred.first->getName() << " := "
        << inferred.second.getString();
  }

  for (const auto &inferred : NonViable) {
    out << "\n";
    out.indent(indent + 2);
    out << std::get<0>(inferred)->getName() << " := "
        << std::get<1>(inferred).getString();
    auto type = std::get<2>(inferred).getRequirement();
    out << " [failed constraint " << type.getString() << "]";
  }

  out << ")";
}

void InferredTypeWitnessesSolution::dump() const {
  const auto numValueWitnesses = ValueWitnesses.size();
  llvm::errs() << "Type Witnesses:\n";
  for (auto &typeWitness : TypeWitnesses) {
    llvm::errs() << "  " << typeWitness.first->getName() << " := ";
    typeWitness.second.first->print(llvm::errs());
    if (typeWitness.second.second == numValueWitnesses) {
      llvm::errs() << ", abstract";
    } else {
      llvm::errs() << ", inferred from $" << typeWitness.second.second;
    }
    llvm::errs() << '\n';
  }
  llvm::errs() << "Value Witnesses:\n";
  for (unsigned i : indices(ValueWitnesses)) {
    const auto &valueWitness = ValueWitnesses[i];
    llvm::errs() << '$' << i << ":\n  ";
    valueWitness.first->dumpRef(llvm::errs());
    llvm::errs() << " ->\n  ";
    valueWitness.second->dumpRef(llvm::errs());
    llvm::errs() << '\n';
  }
}

namespace {
  void dumpInferredAssociatedTypesByWitnesses(
        const InferredAssociatedTypesByWitnesses &inferred,
        llvm::raw_ostream &out,
        unsigned indent) {
    for (const auto &value : inferred) {
      value.dump(out, indent);
    }
  }

  void dumpInferredAssociatedTypesByWitnesses(
        const InferredAssociatedTypesByWitnesses &inferred) LLVM_ATTRIBUTE_USED;

  void dumpInferredAssociatedTypesByWitnesses(
                          const InferredAssociatedTypesByWitnesses &inferred) {
    dumpInferredAssociatedTypesByWitnesses(inferred, llvm::errs(), 0);
  }

  void dumpInferredAssociatedTypes(const InferredAssociatedTypes &inferred,
                                   llvm::raw_ostream &out,
                                   unsigned indent) {
    for (const auto &value : inferred) {
      out << "\n";
      out.indent(indent) << "(";
      value.first->dumpRef(out);
      dumpInferredAssociatedTypesByWitnesses(value.second, out, indent + 2);
      out << ")";
    }
    out << "\n";
  }

  void dumpInferredAssociatedTypes(
         const InferredAssociatedTypes &inferred) LLVM_ATTRIBUTE_USED;

  void dumpInferredAssociatedTypes(const InferredAssociatedTypes &inferred) {
    dumpInferredAssociatedTypes(inferred, llvm::errs(), 0);
  }
}

AssociatedTypeInference::AssociatedTypeInference(
    ASTContext &ctx, NormalProtocolConformance *conformance)
    : ctx(ctx), conformance(conformance), proto(conformance->getProtocol()),
      dc(conformance->getDeclContext()), adoptee(conformance->getType()) {}

static bool associatedTypesAreSameEquivalenceClass(AssociatedTypeDecl *a,
                                                   AssociatedTypeDecl *b) {
  if (a == b)
    return true;

  // TODO: Do a proper equivalence check here by looking for some relationship
  // between a and b's protocols. In practice today, it's unlikely that
  // two same-named associated types can currently be independent, since we
  // don't have anything like `@implements(P.foo)` to rename witnesses (and
  // we still fall back to name lookup for witnesses in more cases than we
  // should).
  if (a->getName() == b->getName())
    return true;

  return false;
}

InferredAssociatedTypesByWitnesses
AssociatedTypeInference::inferTypeWitnessesViaValueWitnesses(
                    ConformanceChecker &checker,
                    const llvm::SetVector<AssociatedTypeDecl *> &allUnresolved,
                    ValueDecl *req) {
  // Conformances constructed by the ClangImporter should have explicit type
  // witnesses already.
  if (isa<ClangModuleUnit>(conformance->getDeclContext()->getModuleScopeContext())) {
    llvm::errs() << "Cannot infer associated types for imported conformance:\n";
    conformance->getType().dump(llvm::errs());
    for (auto assocTypeDecl : allUnresolved)
      assocTypeDecl->dump(llvm::errs());
    abort();
  }

  InferredAssociatedTypesByWitnesses result;

  auto isExtensionUsableForInference = [&](const ExtensionDecl *extension) {
    // The context the conformance being checked is declared on.
    const auto conformanceCtx = checker.Conformance->getDeclContext();
    if (extension == conformanceCtx)
      return true;

    // Invalid case.
    const auto extendedNominal = extension->getExtendedNominal();
    if (extendedNominal == nullptr)
      return true;

    auto *proto = dyn_cast<ProtocolDecl>(extendedNominal);

    // If the extension is bound to the nominal the conformance is
    // declared on, it is viable for inference when its conditional
    // requirements are satisfied by those of the conformance context.
    if (!proto) {
      // Retrieve the generic signature of the extension.
      const auto extensionSig = extension->getGenericSignature();
      return extensionSig
          .requirementsNotSatisfiedBy(
              conformanceCtx->getGenericSignatureOfContext())
          .empty();
    }

    // The condition here is a bit more fickle than
    // `isExtensionApplied`. That check would prematurely reject
    // extensions like `P where AssocType == T` if we're relying on a
    // default implementation inside the extension to infer `AssocType == T`
    // in the first place. Only check conformances on the `Self` type,
    // because those have to be explicitly declared on the type somewhere
    // so won't be affected by whatever answer inference comes up with.
    auto *module = dc->getParentModule();
    auto checkConformance = [&](ProtocolDecl *proto) {
      auto otherConf = module->lookupConformance(conformance->getType(),
                                                 proto);
      return (otherConf && otherConf.getConditionalRequirements().empty());
    };

    // First check the extended protocol itself.
    if (!checkConformance(proto))
      return false;

    // Now check any additional bounds on 'Self' from the where clause.
    auto bounds = getSelfBoundsFromWhereClause(extension);
    for (auto *decl : bounds.decls) {
      if (auto *proto = dyn_cast<ProtocolDecl>(decl)) {
        if (!checkConformance(proto))
          return false;
      }
    }

    return true;
  };

  for (auto witness :
       checker.lookupValueWitnesses(req, /*ignoringNames=*/nullptr)) {
    LLVM_DEBUG(llvm::dbgs() << "Inferring associated types from decl:\n";
               witness->dump(llvm::dbgs()));

    // If the potential witness came from an extension, and our `Self`
    // type can't use it regardless of what associated types we end up
    // inferring, skip the witness.
    if (auto extension = dyn_cast<ExtensionDecl>(witness->getDeclContext()))
      if (!isExtensionUsableForInference(extension))
        continue;

    // Try to resolve the type witness via this value witness.
    auto witnessResult = inferTypeWitnessesViaValueWitness(req, witness);

    // Filter out duplicated inferred types as well as inferred types
    // that don't meet the requirements placed on the associated type.
    llvm::DenseSet<std::pair<AssociatedTypeDecl *, CanType>> known;
    for (unsigned i = 0; i < witnessResult.Inferred.size(); /*nothing*/) {
#define REJECT {\
  witnessResult.Inferred.erase(witnessResult.Inferred.begin() + i); \
  continue; \
}
      auto &result = witnessResult.Inferred[i];

      LLVM_DEBUG(llvm::dbgs() << "Considering whether "
                              << result.first->getName()
                              << " can infer to:\n";
                 result.second->dump(llvm::dbgs()));

      // Filter out errors.
      if (result.second->hasError()) {
        LLVM_DEBUG(llvm::dbgs() << "-- has error type\n");
        REJECT;
      }

      // Filter out duplicates.
      if (!known.insert({result.first, result.second->getCanonicalType()})
                .second) {
        LLVM_DEBUG(llvm::dbgs() << "-- duplicate\n");
        REJECT;
      }

      // Filter out circular possibilities, e.g. that
      // AssocType == S.AssocType or
      // AssocType == Foo<S.AssocType>.
      bool canInferFromOtherAssociatedType = false;
      bool containsTautologicalType =
        result.second.findIf([&](Type t) -> bool {
          auto dmt = t->getAs<DependentMemberType>();
          if (!dmt)
            return false;
          if (!associatedTypesAreSameEquivalenceClass(dmt->getAssocType(),
                                                      result.first))
            return false;

          auto typeInContext =
            conformance->getDeclContext()->mapTypeIntoContext(conformance->getType());

          if (!dmt->getBase()->isEqual(typeInContext))
            return false;

          // If this associated type is same-typed to another associated type
          // on `Self`, then it may still be an interesting candidate if we find
          // an answer for that other type.
          auto witnessContext = witness->getDeclContext();
          if (witnessContext->getExtendedProtocolDecl()
              && witnessContext->getGenericSignatureOfContext()) {
            auto selfTy = witnessContext->getSelfInterfaceType();
            auto selfAssocTy = DependentMemberType::get(selfTy,
                                                        dmt->getAssocType());
            for (auto &reqt : witnessContext->getGenericSignatureOfContext()
                                            .getRequirements()) {
              switch (reqt.getKind()) {
              case RequirementKind::Conformance:
              case RequirementKind::Superclass:
              case RequirementKind::Layout:
                break;

              case RequirementKind::SameType:
                Type other;
                if (reqt.getFirstType()->isEqual(selfAssocTy)) {
                  other = reqt.getSecondType();
                } else if (reqt.getSecondType()->isEqual(selfAssocTy)) {
                  other = reqt.getFirstType();
                } else {
                  break;
                }

                if (auto otherAssoc = other->getAs<DependentMemberType>()) {
                  if (otherAssoc->getBase()->isEqual(selfTy)) {
                    auto otherDMT = DependentMemberType::get(dmt->getBase(),
                                                    otherAssoc->getAssocType());

                    // We may be able to infer one associated type from the
                    // other.
                    result.second = result.second.transform([&](Type t) -> Type{
                      if (t->isEqual(dmt))
                        return otherDMT;
                      return t;
                    });
                    canInferFromOtherAssociatedType = true;
                    LLVM_DEBUG(llvm::dbgs() << "++ we can same-type to:\n";
                               result.second->dump(llvm::dbgs()));
                    return false;
                  }
                }
                break;
              }
            }
          }

          return true;
        });

      if (containsTautologicalType) {
        LLVM_DEBUG(llvm::dbgs() << "-- tautological\n");
        REJECT;
      }

      // Check that the type witness doesn't contradict an
      // explicitly-given type witness. If it does contradict, throw out the
      // witness completely.
      if (!allUnresolved.count(result.first)) {
        auto existingWitness =
          conformance->getTypeWitness(result.first);
        existingWitness = dc->mapTypeIntoContext(existingWitness);

        // If the deduced type contains an irreducible
        // DependentMemberType, that indicates a dependency
        // on another associated type we haven't deduced,
        // so we can't tell whether there's a contradiction
        // yet.
        auto newWitness = result.second->getCanonicalType();
        if (!newWitness->hasTypeParameter() &&
            !newWitness->hasDependentMember() &&
            !existingWitness->isEqual(newWitness)) {
          LLVM_DEBUG(llvm::dbgs() << "** contradicts explicit type witness, "
                                     "rejecting inference from this decl\n");
          goto next_witness;
        }
      }

      // If we same-typed to another unresolved associated type, we won't
      // be able to check conformances yet.
      if (!canInferFromOtherAssociatedType) {
        // Check that the type witness meets the
        // requirements on the associated type.
        if (auto failed =
                checkTypeWitness(result.second, result.first, conformance)) {
          witnessResult.NonViable.push_back(
                          std::make_tuple(result.first,result.second,failed));
          LLVM_DEBUG(llvm::dbgs() << "-- doesn't fulfill requirements\n");
          REJECT;
        }
      }

      LLVM_DEBUG(llvm::dbgs() << "++ seems legit\n");
      ++i;
    }
#undef REJECT

    // If no inferred types remain, skip this witness.
    if (witnessResult.Inferred.empty() && witnessResult.NonViable.empty())
      continue;

    // If there were any non-viable inferred associated types, don't
    // infer anything from this witness.
    if (!witnessResult.NonViable.empty())
      witnessResult.Inferred.clear();

    result.push_back(std::move(witnessResult));
next_witness:;
}

  return result;
}

InferredAssociatedTypes
AssociatedTypeInference::inferTypeWitnessesViaValueWitnesses(
  ConformanceChecker &checker,
  const llvm::SetVector<AssociatedTypeDecl *> &assocTypes)
{
  InferredAssociatedTypes result;
  for (auto member : proto->getMembers()) {
    auto req = dyn_cast<ValueDecl>(member);
    if (!req || !req->isProtocolRequirement())
      continue;

    // Infer type witnesses for associated types.
    if (auto assocType = dyn_cast<AssociatedTypeDecl>(req)) {
      // If this is not one of the associated types we are trying to infer,
      // just continue.
      if (assocTypes.count(assocType) == 0)
        continue;

      auto reqInferred = inferTypeWitnessesViaAssociatedType(checker,
                                                             assocTypes,
                                                             assocType);
      if (!reqInferred.empty())
        result.push_back({req, std::move(reqInferred)});

      continue;
    }

    // Skip operator requirements, because they match globally and
    // therefore tend to cause deduction mismatches.
    // FIXME: If we had some basic sanity checking of Self, we might be able to
    // use these.
    if (auto func = dyn_cast<FuncDecl>(req)) {
      if (func->isOperator() || isa<AccessorDecl>(func))
        continue;
    }

    // Validate the requirement.
    if (req->isInvalid())
      continue;

    // Check whether any of the associated types we care about are
    // referenced in this value requirement.
    {
      const auto referenced = checker.getReferencedAssociatedTypes(req);
      if (llvm::find_if(referenced, [&](AssociatedTypeDecl *const assocType) {
                          return assocTypes.count(assocType);
                        }) == referenced.end())
        continue;
    }

    // Infer associated types from the potential value witnesses for
    // this requirement.
    auto reqInferred =
      inferTypeWitnessesViaValueWitnesses(checker, assocTypes, req);
    if (reqInferred.empty())
      continue;

    result.push_back({req, std::move(reqInferred)});
  }

  return result;
}

/// Map error types back to their original types.
static Type mapErrorTypeToOriginal(Type type) {
  if (auto errorType = type->getAs<ErrorType>()) {
    if (auto originalType = errorType->getOriginalType())
      return originalType.transform(mapErrorTypeToOriginal);
  }

  return type;
}

/// Produce the type when matching a witness.
static Type getWitnessTypeForMatching(NormalProtocolConformance *conformance,
                                      ValueDecl *witness) {
  if (witness->isRecursiveValidation())
    return Type();

  if (witness->isInvalid())
    return Type();

  if (!witness->getDeclContext()->isTypeContext()) {
    // FIXME: Could we infer from 'Self' to make these work?
    return witness->getInterfaceType();
  }

  // Retrieve the set of substitutions to be applied to the witness.
  Type model =
    conformance->getDeclContext()->mapTypeIntoContext(conformance->getType());
  TypeSubstitutionMap substitutions = model->getMemberSubstitutions(witness);
  Type type = witness->getInterfaceType()->getReferenceStorageReferent();

  if (substitutions.empty())
    return type;

  // Strip off the requirements of a generic function type.
  // FIXME: This doesn't actually break recursion when substitution
  // looks for an inferred type witness, but it makes it far less
  // common, because most of the recursion involves the requirements
  // of the generic type.
  if (auto genericFn = type->getAs<GenericFunctionType>()) {
    type = FunctionType::get(genericFn->getParams(),
                             genericFn->getResult(),
                             genericFn->getExtInfo());
  }

  // Remap associated types that reference other protocols into this
  // protocol.
  auto proto = conformance->getProtocol();
  type = type.transformRec([proto](TypeBase *type) -> Optional<Type> {
    if (auto depMemTy = dyn_cast<DependentMemberType>(type)) {
      if (depMemTy->getAssocType() &&
          depMemTy->getAssocType()->getProtocol() != proto) {
        if (auto *assocType = proto->getAssociatedType(depMemTy->getName())) {
          auto origProto = depMemTy->getAssocType()->getProtocol();
          if (proto->inheritsFrom(origProto))
            return Type(DependentMemberType::get(depMemTy->getBase(),
                                                 assocType));
        }
      }
    }

    return None;
  });

  ModuleDecl *module = conformance->getDeclContext()->getParentModule();
  auto resultType = type.subst(QueryTypeSubstitutionMap{substitutions},
                               LookUpConformanceInModule(module));
  if (!resultType->hasError()) return resultType;

  // Map error types with original types *back* to the original, dependent type.
  return resultType.transform(mapErrorTypeToOriginal);
}

/// Remove the 'self' type from the given type, if it's a method type.
static Type removeSelfParam(ValueDecl *value, Type type) {
  if (value->hasCurriedSelf()) {
    return type->castTo<AnyFunctionType>()->getResult();
  }

  return type;
}

InferredAssociatedTypesByWitnesses
AssociatedTypeInference::inferTypeWitnessesViaAssociatedType(
                   ConformanceChecker &checker,
                   const llvm::SetVector<AssociatedTypeDecl *> &allUnresolved,
                   AssociatedTypeDecl *assocType) {
  // Form the default name _Default_Foo.
  DeclNameRef defaultName;
  {
    SmallString<32> defaultNameStr;
    {
      llvm::raw_svector_ostream out(defaultNameStr);
      out << "_Default_";
      out << assocType->getName().str();
    }

    defaultName = DeclNameRef(getASTContext().getIdentifier(defaultNameStr));
  }

  NLOptions subOptions = (NL_QualifiedDefault |
                          NL_OnlyTypes |
                          NL_ProtocolMembers);

  // Look for types with the given default name that have appropriate
  // @_implements attributes.
  SmallVector<ValueDecl *, 4> lookupResults;
  dc->lookupQualified(adoptee->getAnyNominal(), defaultName,
                      subOptions, lookupResults);

  InferredAssociatedTypesByWitnesses result;

  for (auto decl : lookupResults) {
    // We want type declarations.
    auto typeDecl = dyn_cast<TypeDecl>(decl);
    if (!typeDecl || isa<AssociatedTypeDecl>(typeDecl))
      continue;

    // We only find these within a protocol extension.
    auto defaultProto = typeDecl->getDeclContext()->getSelfProtocolDecl();
    if (!defaultProto)
      continue;

    // Determine the witness type.
    Type witnessType = getWitnessTypeForMatching(conformance, typeDecl);
    if (!witnessType) continue;

    if (auto witnessMetaType = witnessType->getAs<AnyMetatypeType>())
      witnessType = witnessMetaType->getInstanceType();
    else
      continue;

    // Add this result.
    InferredAssociatedTypesByWitness inferred;
    inferred.Witness = typeDecl;
    inferred.Inferred.push_back({assocType, witnessType});
    result.push_back(std::move(inferred));
  }

  return result;
}

Type swift::adjustInferredAssociatedType(Type type, bool &noescapeToEscaping) {
  // If we have an optional type, adjust its wrapped type.
  if (auto optionalObjectType = type->getOptionalObjectType()) {
    auto newOptionalObjectType =
      adjustInferredAssociatedType(optionalObjectType, noescapeToEscaping);
    if (newOptionalObjectType.getPointer() == optionalObjectType.getPointer())
      return type;

    return OptionalType::get(newOptionalObjectType);
  }

  // If we have a noescape function type, make it escaping.
  if (auto funcType = type->getAs<FunctionType>()) {
    if (funcType->isNoEscape()) {
      noescapeToEscaping = true;
      return FunctionType::get(funcType->getParams(), funcType->getResult(),
                               funcType->getExtInfo().withNoEscape(false));
    }
  }
  return type;
}

/// Attempt to resolve a type witness via a specific value witness.
InferredAssociatedTypesByWitness
AssociatedTypeInference::inferTypeWitnessesViaValueWitness(ValueDecl *req,
                                                           ValueDecl *witness) {
  InferredAssociatedTypesByWitness inferred;
  inferred.Witness = witness;

  // Compute the requirement and witness types we'll use for matching.
  Type fullWitnessType = getWitnessTypeForMatching(conformance, witness);
  if (!fullWitnessType) {
    return inferred;
  }

  auto setup = [&]() -> std::tuple<Optional<RequirementMatch>, Type, Type> {
    fullWitnessType = removeSelfParam(witness, fullWitnessType);
    return std::make_tuple(
        None,
        removeSelfParam(req, req->getInterfaceType()),
        fullWitnessType);
  };

  /// Visits a requirement type to match it to a potential witness for
  /// the purpose of deducing associated types.
  ///
  /// The visitor argument is the witness type. If there are any
  /// obvious conflicts between the structure of the two types,
  /// returns true. The conflict checking is fairly conservative, only
  /// considering rough structure.
  class MatchVisitor : public TypeMatcher<MatchVisitor> {
    NormalProtocolConformance *Conformance;
    InferredAssociatedTypesByWitness &Inferred;

  public:
    MatchVisitor(NormalProtocolConformance *conformance,
                 InferredAssociatedTypesByWitness &inferred)
      : Conformance(conformance), Inferred(inferred) { }

    /// Structural mismatches imply that the witness cannot match.
    bool mismatch(TypeBase *firstType, TypeBase *secondType,
                  Type sugaredFirstType) {
      // If either type hit an error, don't stop yet.
      if (firstType->hasError() || secondType->hasError())
        return true;

      // FIXME: Check whether one of the types is dependent?
      return false;
    }

    /// Deduce associated types from dependent member types in the witness.
    bool mismatch(DependentMemberType *firstDepMember,
                  TypeBase *secondType, Type sugaredFirstType) {
      // If the second type is an error, don't look at it further.
      if (secondType->hasError())
        return true;

      // Adjust the type to a type that can be written explicitly.
      bool noescapeToEscaping = false;
      Type inferredType =
        adjustInferredAssociatedType(secondType, noescapeToEscaping);
      if (!inferredType->isMaterializable())
        return true;

      // If the type contains a type parameter, there is nothing we can infer
      // from it.
      // FIXME: This is a weird state introduced by associated type inference
      // that should not exist.
      if (inferredType->hasTypeParameter())
        return true;

      auto proto = Conformance->getProtocol();
      if (auto assocType = getReferencedAssocTypeOfProtocol(firstDepMember,
                                                            proto)) {
        Inferred.Inferred.push_back({assocType, inferredType});
      }

      // Always allow mismatches here.
      return true;
    }

    /// FIXME: Recheck the type of Self against the second type?
    bool mismatch(GenericTypeParamType *selfParamType,
                  TypeBase *secondType, Type sugaredFirstType) {
      return true;
    }
  };

  // Match a requirement and witness type.
  MatchVisitor matchVisitor(conformance, inferred);
  auto matchTypes = [&](Type reqType, Type witnessType)
                      -> Optional<RequirementMatch> {
    if (!matchVisitor.match(reqType, witnessType)) {
      return RequirementMatch(witness, MatchKind::TypeConflict,
                              fullWitnessType);
    }

    return None;
  };

  // Finalization of the checking is pretty trivial; just bundle up a
  // result we can look at.
  auto finalize = [&](bool anyRenaming, ArrayRef<OptionalAdjustment>)
                    -> RequirementMatch {
    return RequirementMatch(witness,
                            anyRenaming ? MatchKind::RenamedMatch
                                        : MatchKind::ExactMatch,
                            fullWitnessType);

  };

  // Match the witness. If we don't succeed, throw away the inference
  // information.
  // FIXME: A renamed match might be useful to retain for the failure case.
  if (!matchWitness(dc, req, witness, setup, matchTypes, finalize)
          .isWellFormed()) {
    inferred.Inferred.clear();
  }

  return inferred;
}

AssociatedTypeDecl *AssociatedTypeInference::findDefaultedAssociatedType(
                                             AssociatedTypeDecl *assocType) {
  // If this associated type has a default, we're done.
  if (assocType->hasDefaultDefinitionType())
    return assocType;

  // Look at overridden associated types.
  SmallPtrSet<CanType, 4> canonicalTypes;
  SmallVector<AssociatedTypeDecl *, 2> results;
  for (auto overridden : assocType->getOverriddenDecls()) {
    auto overriddenDefault = findDefaultedAssociatedType(overridden);
    if (!overriddenDefault) continue;

    Type overriddenType =
      overriddenDefault->getDefaultDefinitionType();
    assert(overriddenType);
    if (!overriddenType) continue;

    CanType key = overriddenType->getCanonicalType();
    if (canonicalTypes.insert(key).second)
      results.push_back(overriddenDefault);
  }

  // If there was a single result, return it.
  // FIXME: We could find *all* of the non-covered, defaulted associated types.
  return results.size() == 1 ? results.front() : nullptr;
}

Type AssociatedTypeInference::computeFixedTypeWitness(
                                            AssociatedTypeDecl *assocType) {
  Type resultType;

  // Look at all of the inherited protocols to determine whether they
  // require a fixed type for this associated type.
  for (auto conformedProto : adoptee->getAnyNominal()->getAllProtocols()) {
    if (conformedProto != assocType->getProtocol() &&
        !conformedProto->inheritsFrom(assocType->getProtocol()))
      continue;

    auto sig = conformedProto->getGenericSignature();

    // FIXME: The RequirementMachine will assert on re-entrant construction.
    // We should find a more principled way of breaking this cycle.
    if (ctx.isRecursivelyConstructingRequirementMachine(sig.getCanonicalSignature()) ||
        conformedProto->isComputingRequirementSignature())
      continue;

    auto selfTy = conformedProto->getSelfInterfaceType();
    if (!sig->requiresProtocol(selfTy, assocType->getProtocol()))
      continue;

    auto structuralTy = DependentMemberType::get(selfTy, assocType->getName());
    const auto ty = sig.getCanonicalTypeInContext(structuralTy);

    // A dependent member type with an identical base and name indicates that
    // the protocol does not same-type constrain it in any way; move on to
    // the next protocol.
    if (auto *const memberTy = ty->getAs<DependentMemberType>()) {
      if (memberTy->getBase()->isEqual(selfTy) &&
          memberTy->getName() == assocType->getName())
        continue;
    }

    if (!resultType) {
      resultType = ty;
      continue;
    }

    // FIXME: Bailing out on ambiguity.
    if (!resultType->isEqual(ty))
      return Type();
  }

  return resultType;
}

Optional<AbstractTypeWitness>
AssociatedTypeInference::computeDefaultTypeWitness(
    AssociatedTypeDecl *assocType) {
  // Go find a default definition.
  auto *const defaultedAssocType = findDefaultedAssociatedType(assocType);
  if (!defaultedAssocType)
    return None;

  const Type defaultType = defaultedAssocType->getDefaultDefinitionType();
  // FIXME: Circularity
  if (!defaultType)
    return None;

  if (defaultType->hasError())
    return None;

  return AbstractTypeWitness::forDefault(assocType, defaultType,
                                         defaultedAssocType);
}

std::pair<Type, TypeDecl *>
AssociatedTypeInference::computeDerivedTypeWitness(
                                              AssociatedTypeDecl *assocType) {
  if (adoptee->hasError())
    return std::make_pair(Type(), nullptr);

  // Can we derive conformances for this protocol and adoptee?
  NominalTypeDecl *derivingTypeDecl = adoptee->getAnyNominal();
  if (!DerivedConformance::derivesProtocolConformance(dc, derivingTypeDecl,
                                                      proto))
    return std::make_pair(Type(), nullptr);

  // Try to derive the type witness.
  auto result = TypeChecker::deriveTypeWitness(dc, derivingTypeDecl, assocType);
  if (!result.first)
    return std::make_pair(Type(), nullptr);

  // Make sure that the derived type satisfies requirements.
  if (checkTypeWitness(result.first, assocType, conformance)) {
    /// FIXME: Diagnose based on this.
    failedDerivedAssocType = assocType;
    failedDerivedWitness = result.first;
    return std::make_pair(Type(), nullptr);
  }

  return result;
}

Optional<AbstractTypeWitness>
AssociatedTypeInference::computeAbstractTypeWitness(
    AssociatedTypeDecl *assocType) {
  // We don't have a type witness for this associated type, so go
  // looking for more options.
  if (Type concreteType = computeFixedTypeWitness(assocType))
    return AbstractTypeWitness::forFixed(assocType, concreteType);

  // If we can form a default type, do so.
  if (const auto &typeWitness = computeDefaultTypeWitness(assocType))
    return typeWitness;

  // If there is a generic parameter of the named type, use that.
  if (auto genericSig = dc->getGenericSignatureOfContext()) {
    for (auto gp : genericSig.getInnermostGenericParams()) {
      if (gp->getName() == assocType->getName())
        return AbstractTypeWitness::forGenericParam(assocType, gp);
    }
  }

  return None;
}

Type AssociatedTypeInference::substCurrentTypeWitnesses(Type type) {
  // Local function that folds dependent member types with non-dependent
  // bases into actual member references.
  std::function<Type(Type)> foldDependentMemberTypes;
  llvm::DenseSet<AssociatedTypeDecl *> recursionCheck;
  foldDependentMemberTypes = [&](Type type) -> Type {
    if (auto depMemTy = type->getAs<DependentMemberType>()) {
      auto baseTy = depMemTy->getBase().transform(foldDependentMemberTypes);
      if (baseTy.isNull() || baseTy->hasTypeParameter())
        return nullptr;

      auto assocType = depMemTy->getAssocType();
      if (!assocType)
        return nullptr;

      if (!recursionCheck.insert(assocType).second)
        return nullptr;

      SWIFT_DEFER { recursionCheck.erase(assocType); };

      auto *module = dc->getParentModule();

      // Try to substitute into the base type.
      Type result = depMemTy->substBaseType(module, baseTy);
      if (!result->hasError())
        return result;

      // If that failed, check whether it's because of the conformance we're
      // evaluating.
      auto localConformance
        = module->lookupConformance(baseTy, assocType->getProtocol());
      if (localConformance.isInvalid() || localConformance.isAbstract() ||
          (localConformance.getConcrete()->getRootConformance() !=
           conformance)) {
        return nullptr;
      }

      // Find the tentative type witness for this associated type.
      auto known = typeWitnesses.begin(assocType);
      if (known == typeWitnesses.end())
        return nullptr;

      return known->first.transform(foldDependentMemberTypes);
    }

    // The presence of a generic type parameter indicates that we
    // cannot use this type binding.
    if (type->is<GenericTypeParamType>()) {
      return nullptr;
    }

    return type;
  };

  return type.transform(foldDependentMemberTypes);
}

/// "Sanitize" requirements for conformance checking, removing any requirements
/// that unnecessarily refer to associated types of other protocols.
static void sanitizeProtocolRequirements(
                                     ProtocolDecl *proto,
                                     ArrayRef<Requirement> requirements,
                                     SmallVectorImpl<Requirement> &sanitized) {
  std::function<Type(Type)> sanitizeType;
  sanitizeType = [&](Type outerType) {
    return outerType.transformRec([&] (TypeBase *type) -> Optional<Type> {
      if (auto depMemTy = dyn_cast<DependentMemberType>(type)) {
        if (!depMemTy->getAssocType() ||
            depMemTy->getAssocType()->getProtocol() != proto) {

          if (auto *assocType = proto->getAssociatedType(depMemTy->getName())) {
            Type sanitizedBase = sanitizeType(depMemTy->getBase());
            if (!sanitizedBase)
              return Type();
            return Type(DependentMemberType::get(sanitizedBase,
                                                  assocType));
          }

          if (depMemTy->getBase()->is<GenericTypeParamType>())
            return Type();
        }
      }

      return None;
    });
  };

  for (const auto &req : requirements) {
    switch (req.getKind()) {
    case RequirementKind::Conformance:
    case RequirementKind::SameType:
    case RequirementKind::Superclass: {
      Type firstType = sanitizeType(req.getFirstType());
      Type secondType = sanitizeType(req.getSecondType());
      if (firstType && secondType) {
        sanitized.push_back({req.getKind(), firstType, secondType});
      }
      break;
    }

    case RequirementKind::Layout: {
      Type firstType = sanitizeType(req.getFirstType());
      if (firstType) {
        sanitized.push_back({req.getKind(), firstType,
                             req.getLayoutConstraint()});
      }
      break;
    }
    }
  }
}

SubstOptions
AssociatedTypeInference::getSubstOptionsWithCurrentTypeWitnesses() {
  SubstOptions options(None);
  AssociatedTypeInference *self = this;
  options.getTentativeTypeWitness =
    [self](const NormalProtocolConformance *conformance,
           AssociatedTypeDecl *assocType) -> TypeBase * {
      auto thisProto = self->conformance->getProtocol();
      if (conformance == self->conformance) {
        // Okay: we have the associated type we need.
      } else if (conformance->getType()->isEqual(
                   self->conformance->getType()) &&
                 thisProto->inheritsFrom(conformance->getProtocol())) {
        // Find an associated type with the same name in the given
        // protocol.
        auto *foundAssocType = thisProto->getAssociatedType(
            assocType->getName());
        if (!foundAssocType) return nullptr;
        assocType = foundAssocType;
      } else {
        return nullptr;
      }

      Type type = self->typeWitnesses.begin(assocType)->first;

      // FIXME: Get rid of this hack.
      if (auto *aliasTy = dyn_cast<TypeAliasType>(type.getPointer()))
        type = aliasTy->getSinglyDesugaredType();

      return type->hasArchetype() ? type->mapTypeOutOfContext().getPointer()
                                  : type.getPointer();
    };
  return options;
}

bool AssociatedTypeInference::checkCurrentTypeWitnesses(
       const SmallVectorImpl<std::pair<ValueDecl *, ValueDecl *>>
         &valueWitnesses) {
  // Check any same-type requirements in the protocol's requirement signature.
  SubstOptions options = getSubstOptionsWithCurrentTypeWitnesses();

  auto typeInContext = dc->mapTypeIntoContext(adoptee);

  auto substitutions =
    SubstitutionMap::getProtocolSubstitutions(
                                    proto, typeInContext,
                                    ProtocolConformanceRef(conformance));

  SmallVector<Requirement, 4> sanitizedRequirements;
  sanitizeProtocolRequirements(proto, proto->getRequirementSignature(),
                               sanitizedRequirements);
  auto result =
    TypeChecker::checkGenericArguments(dc->getParentModule(), SourceLoc(),
                                       SourceLoc(), typeInContext,
                                       { proto->getSelfInterfaceType() },
                                       sanitizedRequirements,
                                       QuerySubstitutionMap{substitutions},
                                       options);
  switch (result) {
  case RequirementCheckResult::Failure:
    ++NumSolutionStatesFailedCheck;
    return true;

  case RequirementCheckResult::Success:
  case RequirementCheckResult::SubstitutionFailure:
    break;
  }

  // Check for extra requirements in the constrained extensions that supply
  // defaults.
  SmallPtrSet<ExtensionDecl *, 4> checkedExtensions;
  for (const auto &valueWitness : valueWitnesses) {
    // We only perform this additional checking for default associated types.
    if (!isa<TypeDecl>(valueWitness.first)) continue;

    auto witness = valueWitness.second;
    if (!witness) continue;

    auto ext = dyn_cast<ExtensionDecl>(witness->getDeclContext());
    if (!ext) continue;

    if (!ext->isConstrainedExtension()) continue;
    if (!checkedExtensions.insert(ext).second) continue;

    ++NumConstrainedExtensionChecks;
    if (checkConstrainedExtension(ext)) {
      ++NumConstrainedExtensionChecksFailed;
      return true;
    }
  }

  return false;
}

bool AssociatedTypeInference::checkConstrainedExtension(ExtensionDecl *ext) {
  auto typeInContext = dc->mapTypeIntoContext(adoptee);
  auto subs = typeInContext->getContextSubstitutions(ext);

  SubstOptions options = getSubstOptionsWithCurrentTypeWitnesses();
  switch (TypeChecker::checkGenericArguments(
                       dc->getParentModule(), SourceLoc(), SourceLoc(), adoptee,
                       ext->getGenericSignature().getGenericParams(),
                       ext->getGenericSignature().getRequirements(),
                       QueryTypeSubstitutionMap{subs},
                       options)) {
  case RequirementCheckResult::Success:
  case RequirementCheckResult::SubstitutionFailure:
    return false;

  case RequirementCheckResult::Failure:
    return true;
  }
  llvm_unreachable("unhandled result");
}

AssociatedTypeDecl *AssociatedTypeInference::completeSolution(
    ArrayRef<AssociatedTypeDecl *> unresolvedAssocTypes, unsigned reqDepth) {
  // Examine the solution for errors and attempt to compute abstract type
  // witnesses for associated types that are still lacking an entry.
  llvm::SmallVector<AbstractTypeWitness, 2> abstractTypeWitnesses;
  for (auto *const assocType : unresolvedAssocTypes) {
    const auto typeWitness = typeWitnesses.begin(assocType);
    if (typeWitness != typeWitnesses.end()) {
      // The solution contains an error.
      if (typeWitness->first->hasError()) {
        return assocType;
      }

      continue;
    }

    // Try to compute the type without the aid of a specific potential witness.
    if (const auto &typeWitness = computeAbstractTypeWitness(assocType)) {
      // Record the type witness immediately to make it available
      // for substitutions into other tentative type witnesses.
      typeWitnesses.insert(assocType, {typeWitness->getType(), reqDepth});

      abstractTypeWitnesses.push_back(std::move(typeWitness.getValue()));
      continue;
    }

    // The solution is incomplete.
    return assocType;
  }

  // Check each abstract type witness we computed against the generic
  // requirements on the corresponding associated type.
  const auto substOptions = getSubstOptionsWithCurrentTypeWitnesses();
  for (const auto &witness : abstractTypeWitnesses) {
    Type type = witness.getType();
    if (type->hasTypeParameter()) {
      if (witness.getKind() == AbstractTypeWitnessKind::GenericParam) {
        type = type = dc->mapTypeIntoContext(type);
      } else {
        // Replace type parameters with other known or tentative type witnesses.
        type = type.subst(
            [&](SubstitutableType *type) {
              if (type->isEqual(proto->getSelfInterfaceType()))
                return adoptee;

              return Type();
            },
            LookUpConformanceInModule(dc->getParentModule()), substOptions);

        // If the substitution produced an error, we're done.
        if (type->hasError())
          return witness.getAssocType();

        // FIXME: If mapping into context yields an error, or we still have a
        // type parameter despite not having a generic environment, then a type
        // parameter was sent to a tentative type witness that itself is a type
        // parameter, and the solution is cyclic, e.g. { A := B.A, B := A.B },
        // or beyond the current algorithm, e.g.
        // protocol P {
        //   associatedtype A = B
        //   associatedtype B = C
        //   associatedtype C = Int
        // }
        // struct Conformer: P {}
        if (dc->getGenericEnvironmentOfContext()) {
          type = dc->mapTypeIntoContext(type);

          if (type->hasError())
            return witness.getAssocType();
        } else if (type->hasTypeParameter()) {
          return witness.getAssocType();
        }
      }
    }

    if (const auto &failed = checkTypeWitness(type, witness.getAssocType(),
                                              conformance, substOptions)) {
      // We failed to satisfy a requirement. If this is a default type
      // witness failure and we haven't seen one already, write it down.
      if (witness.getKind() == AbstractTypeWitnessKind::Default &&
          !failedDefaultedAssocType && !failed.isError()) {
        failedDefaultedAssocType = witness.getDefaultedAssocType();
        failedDefaultedWitness = type;
        failedDefaultedResult = std::move(failed);
      }

      return witness.getAssocType();
    }

    // Update the solution entry.
    typeWitnesses.insert(witness.getAssocType(), {type, reqDepth});
  }

  return nullptr;
}

void AssociatedTypeInference::findSolutions(
                   ArrayRef<AssociatedTypeDecl *> unresolvedAssocTypes,
                   SmallVectorImpl<InferredTypeWitnessesSolution> &solutions) {
  SmallVector<InferredTypeWitnessesSolution, 4> nonViableSolutions;
  SmallVector<std::pair<ValueDecl *, ValueDecl *>, 4> valueWitnesses;
  findSolutionsRec(unresolvedAssocTypes, solutions, nonViableSolutions,
                   valueWitnesses, 0, 0, 0);
}

void AssociatedTypeInference::findSolutionsRec(
          ArrayRef<AssociatedTypeDecl *> unresolvedAssocTypes,
          SmallVectorImpl<InferredTypeWitnessesSolution> &solutions,
          SmallVectorImpl<InferredTypeWitnessesSolution> &nonViableSolutions,
          SmallVector<std::pair<ValueDecl *, ValueDecl *>, 4> &valueWitnesses,
          unsigned numTypeWitnesses,
          unsigned numValueWitnessesInProtocolExtensions,
          unsigned reqDepth) {
  using TypeWitnessesScope = decltype(typeWitnesses)::ScopeTy;

  // If we hit the last requirement, record and check this solution.
  if (reqDepth == inferred.size()) {
    // Introduce a hash table scope; we may add type witnesses here.
    TypeWitnessesScope typeWitnessesScope(typeWitnesses);

    // Validate and complete the solution.
    if (auto *const assocType =
            completeSolution(unresolvedAssocTypes, reqDepth)) {
      // The solution is decisively incomplete; record the associated type
      // we failed on and bail out.
      if (!missingTypeWitness)
        missingTypeWitness = assocType;

      return;
    }

    ++NumSolutionStates;

    // Fold the dependent member types within this type.
    for (auto assocType : proto->getAssociatedTypeMembers()) {
      if (conformance->hasTypeWitness(assocType))
        continue;

      // If the type binding does not have a type parameter, there's nothing
      // to do.
      auto known = typeWitnesses.begin(assocType);
      assert(known != typeWitnesses.end());
      if (!known->first->hasTypeParameter() &&
          !known->first->hasDependentMember())
        continue;

      Type replaced = substCurrentTypeWitnesses(known->first);
      if (replaced.isNull())
        return;

      known->first = replaced;
    }

    // Check whether our current solution matches the given solution.
    auto matchesSolution =
        [&](const InferredTypeWitnessesSolution &solution) {
      for (const auto &existingTypeWitness : solution.TypeWitnesses) {
        auto typeWitness = typeWitnesses.begin(existingTypeWitness.first);
        if (!typeWitness->first->isEqual(existingTypeWitness.second.first))
          return false;
      }

      return true;
    };

    // If we've seen this solution already, bail out; there's no point in
    // checking further.
    if (llvm::any_of(solutions, matchesSolution) ||
        llvm::any_of(nonViableSolutions, matchesSolution)) {
      ++NumDuplicateSolutionStates;
      return;
    }

    /// Check the current set of type witnesses.
    bool invalid = checkCurrentTypeWitnesses(valueWitnesses);

    auto &solutionList = invalid ? nonViableSolutions : solutions;
    solutionList.push_back(InferredTypeWitnessesSolution());
    auto &solution = solutionList.back();

    // Copy the type witnesses.
    for (auto assocType : unresolvedAssocTypes) {
      auto typeWitness = typeWitnesses.begin(assocType);
      solution.TypeWitnesses.insert({assocType, *typeWitness});
    }

    // Copy the value witnesses.
    solution.ValueWitnesses = valueWitnesses;
    solution.NumValueWitnessesInProtocolExtensions
      = numValueWitnessesInProtocolExtensions;

    // We're done recording the solution.
    return;
  }

  // Iterate over the potential witnesses for this requirement,
  // looking for solutions involving each one.
  const auto &inferredReq = inferred[reqDepth];
  for (const auto &witnessReq : inferredReq.second) {
    llvm::SaveAndRestore<unsigned> savedNumTypeWitnesses(numTypeWitnesses);

    // If we inferred a type witness via a default, try both with and without
    // the default.
    if (isa<TypeDecl>(inferredReq.first)) {
      // Recurse without considering this type.
      valueWitnesses.push_back({inferredReq.first, nullptr});
      findSolutionsRec(unresolvedAssocTypes, solutions, nonViableSolutions,
                       valueWitnesses, numTypeWitnesses,
                       numValueWitnessesInProtocolExtensions, reqDepth + 1);
      valueWitnesses.pop_back();

      ++numTypeWitnesses;
      for (const auto &typeWitness : witnessReq.Inferred) {
        auto known = typeWitnesses.begin(typeWitness.first);
        if (known != typeWitnesses.end()) continue;

        // Enter a new scope for the type witnesses hash table.
        TypeWitnessesScope typeWitnessesScope(typeWitnesses);
        typeWitnesses.insert(typeWitness.first, {typeWitness.second, reqDepth});

        valueWitnesses.push_back({inferredReq.first, witnessReq.Witness});
        findSolutionsRec(unresolvedAssocTypes, solutions, nonViableSolutions,
                         valueWitnesses, numTypeWitnesses,
                         numValueWitnessesInProtocolExtensions, reqDepth + 1);
        valueWitnesses.pop_back();
      }

      continue;
    }

    // Enter a new scope for the type witnesses hash table.
    TypeWitnessesScope typeWitnessesScope(typeWitnesses);

    // Record this value witness, popping it when we exit the current scope.
    valueWitnesses.push_back({inferredReq.first, witnessReq.Witness});
    if (!isa<TypeDecl>(inferredReq.first) &&
        witnessReq.Witness->getDeclContext()->getExtendedProtocolDecl())
      ++numValueWitnessesInProtocolExtensions;
    SWIFT_DEFER {
      if (!isa<TypeDecl>(inferredReq.first) &&
          witnessReq.Witness->getDeclContext()->getExtendedProtocolDecl())
        --numValueWitnessesInProtocolExtensions;

      valueWitnesses.pop_back();
    };

    // Introduce each of the type witnesses into the hash table.
    bool failed = false;
    for (const auto &typeWitness : witnessReq.Inferred) {
      // If we've seen a type witness for this associated type that
      // conflicts, there is no solution.
      auto known = typeWitnesses.begin(typeWitness.first);
      if (known != typeWitnesses.end()) {
        // Don't overwrite a defaulted associated type witness.
        if (isa<TypeDecl>(valueWitnesses[known->second].second))
          continue;

        // If witnesses for two different requirements inferred the same
        // type, we're okay.
        if (known->first->isEqual(typeWitness.second))
          continue;

        // If one has a type parameter remaining but the other does not,
        // drop the one with the type parameter.
        if ((known->first->hasTypeParameter() ||
             known->first->hasDependentMember())
            != (typeWitness.second->hasTypeParameter() ||
                typeWitness.second->hasDependentMember())) {
          if (typeWitness.second->hasTypeParameter() ||
              typeWitness.second->hasDependentMember())
            continue;

          known->first = typeWitness.second;
          continue;
        }

        if (!typeWitnessConflict ||
            numTypeWitnesses > numTypeWitnessesBeforeConflict) {
          typeWitnessConflict = {typeWitness.first,
                                 typeWitness.second,
                                 inferredReq.first,
                                 witnessReq.Witness,
                                 known->first,
                                 valueWitnesses[known->second].first,
                                 valueWitnesses[known->second].second};
          numTypeWitnessesBeforeConflict = numTypeWitnesses;
        }

        failed = true;
        break;
      }

      // Record the type witness.
      ++numTypeWitnesses;
      typeWitnesses.insert(typeWitness.first, {typeWitness.second, reqDepth});
    }

    if (failed)
      continue;

    // Recurse
    findSolutionsRec(unresolvedAssocTypes, solutions, nonViableSolutions,
                     valueWitnesses, numTypeWitnesses,
                     numValueWitnessesInProtocolExtensions, reqDepth + 1);
  }
}

static Comparison compareDeclsForInference(DeclContext *DC, ValueDecl *decl1,
                                           ValueDecl *decl2) {
  // TypeChecker::compareDeclarations assumes that it's comparing two decls that
  // apply equally well to a call site. We haven't yet inferred the
  // associated types for a type, so the ranking algorithm used by
  // compareDeclarations to score protocol extensions is inappropriate,
  // since we may have potential witnesses from extensions with mutually
  // exclusive associated type constraints, and compareDeclarations will
  // consider these unordered since neither extension's generic signature
  // is a superset of the other.

  // If one of the declarations is null, it implies that we're working with
  // a skipped associated type default. Prefer that default to something
  // that came from a protocol extension.
  if (!decl1 || !decl2) {
    if (!decl1 &&
        decl2 && decl2->getDeclContext()->getExtendedProtocolDecl())
      return Comparison::Worse;

    if (!decl2 &&
        decl1 && decl1->getDeclContext()->getExtendedProtocolDecl())
      return Comparison::Better;

    return Comparison::Unordered;
  }


  // If the witnesses come from the same decl context, score normally.
  auto dc1 = decl1->getDeclContext();
  auto dc2 = decl2->getDeclContext();

  if (dc1 == dc2)
    return TypeChecker::compareDeclarations(DC, decl1, decl2);

  auto isProtocolExt1 = (bool)dc1->getExtendedProtocolDecl();
  auto isProtocolExt2 = (bool)dc2->getExtendedProtocolDecl();

  // If one witness comes from a protocol extension, favor the one
  // from a concrete context.
  if (isProtocolExt1 != isProtocolExt2) {
    return isProtocolExt1 ? Comparison::Worse : Comparison::Better;
  }

  // If both witnesses came from concrete contexts, score normally.
  // Associated type inference shouldn't impact the result.
  // FIXME: It could, if someone constrained to ConcreteType.AssocType...
  if (!isProtocolExt1)
    return TypeChecker::compareDeclarations(DC, decl1, decl2);

  // Compare protocol extensions by which protocols they require Self to
  // conform to. If one extension requires a superset of the other's
  // constraints, it wins.
  auto sig1 = dc1->getGenericSignatureOfContext();
  auto sig2 = dc2->getGenericSignatureOfContext();

  // FIXME: Extensions sometimes have null generic signatures while
  // checking the standard library...
  if (!sig1 || !sig2)
    return TypeChecker::compareDeclarations(DC, decl1, decl2);

  auto selfParam = GenericTypeParamType::get(/*type sequence*/ false,
                                             /*depth*/ 0, /*index*/ 0,
                                             decl1->getASTContext());

  // Collect the protocols required by extension 1.
  Type class1;
  SmallPtrSet<ProtocolDecl*, 4> protos1;

  std::function<void (ProtocolDecl*)> insertProtocol;
  insertProtocol = [&](ProtocolDecl *p) {
    if (!protos1.insert(p).second)
      return;

    for (auto parent : p->getInheritedProtocols())
      insertProtocol(parent);
  };

  for (auto &reqt : sig1.getRequirements()) {
    if (!reqt.getFirstType()->isEqual(selfParam))
      continue;
    switch (reqt.getKind()) {
    case RequirementKind::Conformance: {
      insertProtocol(reqt.getProtocolDecl());
      break;
    }
    case RequirementKind::Superclass:
      class1 = reqt.getSecondType();
      break;

    case RequirementKind::SameType:
    case RequirementKind::Layout:
      break;
    }
  }

  // Compare with the protocols required by extension 2.
  Type class2;
  SmallPtrSet<ProtocolDecl*, 4> protos2;
  bool protos2AreSubsetOf1 = true;
  std::function<void (ProtocolDecl*)> removeProtocol;
  removeProtocol = [&](ProtocolDecl *p) {
    if (!protos2.insert(p).second)
      return;

    protos2AreSubsetOf1 &= protos1.erase(p);
    for (auto parent : p->getInheritedProtocols())
      removeProtocol(parent);
  };

  for (auto &reqt : sig2.getRequirements()) {
    if (!reqt.getFirstType()->isEqual(selfParam))
      continue;
    switch (reqt.getKind()) {
    case RequirementKind::Conformance: {
      removeProtocol(reqt.getProtocolDecl());
      break;
    }
    case RequirementKind::Superclass:
      class2 = reqt.getSecondType();
      break;

    case RequirementKind::SameType:
    case RequirementKind::Layout:
      break;
    }
  }

  auto isClassConstraintAsStrict = [&](Type t1, Type t2) -> bool {
    if (!t1)
      return !t2;

    if (!t2)
      return true;

    return t2->isExactSuperclassOf(t1);
  };

  bool protos1AreSubsetOf2 = protos1.empty();
  // If the second extension requires strictly more protocols than the
  // first, it's better.
  if (protos1AreSubsetOf2 > protos2AreSubsetOf1
      && isClassConstraintAsStrict(class2, class1)) {
    return Comparison::Worse;
  // If the first extension requires strictly more protocols than the
  // second, it's better.
  } else if (protos2AreSubsetOf1 > protos1AreSubsetOf2
             && isClassConstraintAsStrict(class1, class2)) {
    return Comparison::Better;
  }

  // If they require the same set of protocols, or non-overlapping
  // sets, judge them normally.
  return TypeChecker::compareDeclarations(DC, decl1, decl2);
}

bool AssociatedTypeInference::isBetterSolution(
                      const InferredTypeWitnessesSolution &first,
                      const InferredTypeWitnessesSolution &second) {
  assert(first.ValueWitnesses.size() == second.ValueWitnesses.size());
  bool firstBetter = false;
  bool secondBetter = false;
  for (unsigned i = 0, n = first.ValueWitnesses.size(); i != n; ++i) {
    assert(first.ValueWitnesses[i].first == second.ValueWitnesses[i].first);
    auto firstWitness = first.ValueWitnesses[i].second;
    auto secondWitness = second.ValueWitnesses[i].second;
    if (firstWitness == secondWitness)
      continue;

    switch (compareDeclsForInference(dc, firstWitness, secondWitness)) {
    case Comparison::Better:
      if (secondBetter)
        return false;

      firstBetter = true;
      break;

    case Comparison::Worse:
      if (firstBetter)
        return false;

      secondBetter = true;
      break;

    case Comparison::Unordered:
      break;
    }
  }

  return firstBetter;
}

bool AssociatedTypeInference::findBestSolution(
                   SmallVectorImpl<InferredTypeWitnessesSolution> &solutions) {
  if (solutions.empty()) return true;
  if (solutions.size() == 1) return false;

  // Find the smallest number of value witnesses found in protocol extensions.
  // FIXME: This is a silly heuristic that should go away.
  unsigned bestNumValueWitnessesInProtocolExtensions
    = solutions.front().NumValueWitnessesInProtocolExtensions;
  for (unsigned i = 1, n = solutions.size(); i != n; ++i) {
    bestNumValueWitnessesInProtocolExtensions
      = std::min(bestNumValueWitnessesInProtocolExtensions,
                 solutions[i].NumValueWitnessesInProtocolExtensions);
  }

  // Erase any solutions with more value witnesses in protocol
  // extensions than the best.
  solutions.erase(
    std::remove_if(solutions.begin(), solutions.end(),
                   [&](const InferredTypeWitnessesSolution &solution) {
                     return solution.NumValueWitnessesInProtocolExtensions >
                              bestNumValueWitnessesInProtocolExtensions;
                   }),
    solutions.end());

  // If we're down to one solution, success!
  if (solutions.size() == 1) return false;

  // Find a solution that's at least as good as the solutions that follow it.
  unsigned bestIdx = 0;
  for (unsigned i = 1, n = solutions.size(); i != n; ++i) {
    if (isBetterSolution(solutions[i], solutions[bestIdx]))
      bestIdx = i;
  }

  // Make sure that solution is better than any of the other solutions.
  bool ambiguous = false;
  for (unsigned i = 1, n = solutions.size(); i != n; ++i) {
    if (i != bestIdx && !isBetterSolution(solutions[bestIdx], solutions[i])) {
      ambiguous = true;
      break;
    }
  }

  // If the result was ambiguous, fail.
  if (ambiguous) {
    assert(solutions.size() != 1 && "should have succeeded somewhere above?");
    return true;

  }
  // Keep the best solution, erasing all others.
  if (bestIdx != 0)
    solutions[0] = std::move(solutions[bestIdx]);
  solutions.erase(solutions.begin() + 1, solutions.end());
  return false;
}

namespace {
  /// A failed type witness binding.
  struct FailedTypeWitness {
    /// The value requirement that triggered inference.
    ValueDecl *Requirement;

    /// The corresponding value witness from which the type witness
    /// was inferred.
    ValueDecl *Witness;

    /// The actual type witness that was inferred.
    Type TypeWitness;

    /// The failed type witness result.
    CheckTypeWitnessResult Result;
  };
} // end anonymous namespace

bool AssociatedTypeInference::diagnoseNoSolutions(
                         ArrayRef<AssociatedTypeDecl *> unresolvedAssocTypes,
                         ConformanceChecker &checker) {
  // If a defaulted type witness failed, diagnose it.
  if (failedDefaultedAssocType) {
    auto failedDefaultedAssocType = this->failedDefaultedAssocType;
    auto failedDefaultedWitness = this->failedDefaultedWitness;
    auto failedDefaultedResult = this->failedDefaultedResult;

    checker.diagnoseOrDefer(failedDefaultedAssocType, true,
      [failedDefaultedAssocType, failedDefaultedWitness,
       failedDefaultedResult](NormalProtocolConformance *conformance) {
        auto proto = conformance->getProtocol();
        auto &diags = proto->getASTContext().Diags;
        diags.diagnose(failedDefaultedAssocType,
                       diag::default_associated_type_req_fail,
                       failedDefaultedWitness,
                       failedDefaultedAssocType->getName(),
                       proto->getDeclaredInterfaceType(),
                       failedDefaultedResult.getRequirement(),
                       failedDefaultedResult.isConformanceRequirement());
      });

    return true;
  }

  // Form a mapping from associated type declarations to failed type
  // witnesses.
  llvm::DenseMap<AssociatedTypeDecl *, SmallVector<FailedTypeWitness, 2>>
    failedTypeWitnesses;
  for (const auto &inferredReq : inferred) {
    for (const auto &inferredWitness : inferredReq.second) {
      for (const auto &nonViable : inferredWitness.NonViable) {
        failedTypeWitnesses[std::get<0>(nonViable)]
          .push_back({inferredReq.first, inferredWitness.Witness,
                      std::get<1>(nonViable), std::get<2>(nonViable)});
      }
    }
  }

  // Local function to attempt to diagnose potential type witnesses
  // that failed requirements.
  auto tryDiagnoseTypeWitness = [&](AssociatedTypeDecl *assocType) -> bool {
    auto known = failedTypeWitnesses.find(assocType);
    if (known == failedTypeWitnesses.end())
      return false;

    auto failedSet = std::move(known->second);
    checker.diagnoseOrDefer(assocType, true,
      [assocType, failedSet](NormalProtocolConformance *conformance) {
        auto proto = conformance->getProtocol();
        auto &diags = proto->getASTContext().Diags;
        diags.diagnose(assocType, diag::bad_associated_type_deduction,
                       assocType->getName(), proto->getName());
        for (const auto &failed : failedSet) {
          if (failed.Result.isError())
            continue;

          if ((!failed.TypeWitness->getAnyNominal() ||
               failed.TypeWitness->isExistentialType()) &&
              failed.Result.isConformanceRequirement()) {
            Type resultType;
            SourceRange typeRange;
            if (auto *var = dyn_cast<VarDecl>(failed.Witness)) {
              resultType = var->getValueInterfaceType();
              typeRange = var->getTypeSourceRangeForDiagnostics();
            } else if (auto *func = dyn_cast<FuncDecl>(failed.Witness)) {
              resultType = func->getResultInterfaceType();
              typeRange = func->getResultTypeSourceRange();
            } else if (auto *subscript = dyn_cast<SubscriptDecl>(failed.Witness)) {
              resultType = subscript->getElementInterfaceType();
              typeRange = subscript->getElementTypeSourceRange();
            }

            // If the type witness was inferred from an existential
            // result type, suggest an opaque result type instead,
            // which can conform to protocols.
            if (failed.TypeWitness->isExistentialType() &&
                resultType && resultType->isEqual(failed.TypeWitness) &&
                typeRange.isValid()) {
              diags.diagnose(typeRange.Start,
                             diag::suggest_opaque_type_witness,
                             assocType->getName(), failed.TypeWitness,
                             failed.Result.getRequirement())
                .highlight(typeRange)
                .fixItInsert(typeRange.Start, "some ");
              continue;
            }

            diags.diagnose(failed.Witness,
                           diag::associated_type_witness_conform_impossible,
                           assocType->getName(), failed.TypeWitness,
                           failed.Result.getRequirement());
            continue;
          }
          if (!failed.TypeWitness->getClassOrBoundGenericClass() &&
              failed.Result.isSuperclassRequirement()) {
            diags.diagnose(failed.Witness,
                           diag::associated_type_witness_inherit_impossible,
                           assocType->getName(), failed.TypeWitness,
                           failed.Result.getRequirement());
            continue;
          }

          diags.diagnose(failed.Witness,
                         diag::associated_type_deduction_witness_failed,
                         assocType->getName(),
                         failed.TypeWitness,
                         failed.Result.getRequirement(),
                         failed.Result.isConformanceRequirement());
        }
      });

    return true;
  };

  // Try to diagnose the first missing type witness we encountered.
  if (missingTypeWitness && tryDiagnoseTypeWitness(missingTypeWitness))
    return true;

  // Failing that, try to diagnose any type witness that failed a
  // requirement.
  for (auto assocType : unresolvedAssocTypes) {
    if (tryDiagnoseTypeWitness(assocType))
      return true;
  }

  // If we saw a conflict, complain about it.
  if (typeWitnessConflict) {
    auto typeWitnessConflict = this->typeWitnessConflict;

    checker.diagnoseOrDefer(typeWitnessConflict->AssocType, true,
      [typeWitnessConflict](NormalProtocolConformance *conformance) {
        auto &diags = conformance->getDeclContext()->getASTContext().Diags;
        diags.diagnose(typeWitnessConflict->AssocType,
                       diag::ambiguous_associated_type_deduction,
                       typeWitnessConflict->AssocType->getName(),
                       typeWitnessConflict->FirstType,
                       typeWitnessConflict->SecondType);

        diags.diagnose(typeWitnessConflict->FirstWitness,
                       diag::associated_type_deduction_witness,
                       typeWitnessConflict->FirstRequirement->getName(),
                       typeWitnessConflict->FirstType);
        diags.diagnose(typeWitnessConflict->SecondWitness,
                       diag::associated_type_deduction_witness,
                       typeWitnessConflict->SecondRequirement->getName(),
                       typeWitnessConflict->SecondType);
      });

    return true;
  }

  return false;
}

bool AssociatedTypeInference::diagnoseAmbiguousSolutions(
                  ArrayRef<AssociatedTypeDecl *> unresolvedAssocTypes,
                  ConformanceChecker &checker,
                  SmallVectorImpl<InferredTypeWitnessesSolution> &solutions) {
  for (auto assocType : unresolvedAssocTypes) {
    // Find two types that conflict.
    auto &firstSolution = solutions.front();

    // Local function to retrieve the value witness for the current associated
    // type within the given solution.
    auto getValueWitness = [&](InferredTypeWitnessesSolution &solution) {
      unsigned witnessIdx = solution.TypeWitnesses[assocType].second;
      if (witnessIdx < solution.ValueWitnesses.size())
        return solution.ValueWitnesses[witnessIdx];

      return std::pair<ValueDecl *, ValueDecl *>(nullptr, nullptr);
    };

    Type firstType = firstSolution.TypeWitnesses[assocType].first;

    // Extract the value witness used to deduce this associated type, if any.
    auto firstMatch = getValueWitness(firstSolution);

    Type secondType;
    std::pair<ValueDecl *, ValueDecl *> secondMatch;
    for (auto &solution : solutions) {
      Type typeWitness = solution.TypeWitnesses[assocType].first;
      if (!typeWitness->isEqual(firstType)) {
        secondType = typeWitness;
        secondMatch = getValueWitness(solution);
        break;
      }
    }

    if (!secondType)
      continue;

    // We found an ambiguity. diagnose it.
    checker.diagnoseOrDefer(assocType, true,
      [assocType, firstType, firstMatch, secondType, secondMatch](
        NormalProtocolConformance *conformance) {
        auto &diags = assocType->getASTContext().Diags;
        diags.diagnose(assocType, diag::ambiguous_associated_type_deduction,
                       assocType->getName(), firstType, secondType);

        auto diagnoseWitness = [&](std::pair<ValueDecl *, ValueDecl *> match,
                                   Type type){
          // If we have a requirement/witness pair, diagnose it.
          if (match.first && match.second) {
            diags.diagnose(match.second,
                           diag::associated_type_deduction_witness,
                           match.first->getName(), type);

            return;
          }

          // Otherwise, we have a default.
          auto defaultDiag =
            diags.diagnose(assocType, diag::associated_type_deduction_default,
                           type);
          if (auto defaultTypeRepr = assocType->getDefaultDefinitionTypeRepr())
            defaultDiag.highlight(defaultTypeRepr->getSourceRange());
        };

        diagnoseWitness(firstMatch, firstType);
        diagnoseWitness(secondMatch, secondType);
      });

    return true;
  }

  return false;
}

auto AssociatedTypeInference::solve(ConformanceChecker &checker)
    -> Optional<InferredTypeWitnesses> {
  // Track when we are checking type witnesses.
  ProtocolConformanceState initialState = conformance->getState();
  conformance->setState(ProtocolConformanceState::CheckingTypeWitnesses);
  SWIFT_DEFER { conformance->setState(initialState); };

  // Try to resolve type witnesses via name lookup.
  llvm::SetVector<AssociatedTypeDecl *> unresolvedAssocTypes;
  for (auto assocType : proto->getAssociatedTypeMembers()) {
    // If we already have a type witness, do nothing.
    if (conformance->hasTypeWitness(assocType))
      continue;

    // Try to resolve this type witness via name lookup, which is the
    // most direct mechanism, overriding all others.
    switch (checker.resolveTypeWitnessViaLookup(assocType)) {
    case ResolveWitnessResult::Success:
      // Success. Move on to the next.
      continue;

    case ResolveWitnessResult::ExplicitFailed:
      continue;

    case ResolveWitnessResult::Missing:
      // We did not find the witness via name lookup. Try to derive
      // it below.
      break;
    }

    // Finally, try to derive the witness if we know how.
    auto derivedType = computeDerivedTypeWitness(assocType);
    if (derivedType.first) {
      checker.recordTypeWitness(assocType,
                                derivedType.first->mapTypeOutOfContext(),
                                derivedType.second);
      continue;
    }

    // We failed to derive the witness. We're going to go on to try
    // to infer it from potential value witnesses next.
    unresolvedAssocTypes.insert(assocType);
  }

  // Result variable to use for returns so that we get NRVO.
  Optional<InferredTypeWitnesses> result = InferredTypeWitnesses();

  // If we resolved everything, we're done.
  if (unresolvedAssocTypes.empty())
    return result;

  // Infer potential type witnesses from value witnesses.
  inferred = inferTypeWitnessesViaValueWitnesses(checker,
                                                 unresolvedAssocTypes);
  LLVM_DEBUG(llvm::dbgs() << "Candidates for inference:\n";
             dumpInferredAssociatedTypes(inferred));

  // Compute the set of solutions.
  SmallVector<InferredTypeWitnessesSolution, 4> solutions;
  findSolutions(unresolvedAssocTypes.getArrayRef(), solutions);

  // Go make sure that type declarations that would act as witnesses
  // did not get injected while we were performing checks above. This
  // can happen when two associated types in different protocols have
  // the same name, and validating a declaration (above) triggers the
  // type-witness generation for that second protocol, introducing a
  // new type declaration.
  // FIXME: This is ridiculous.
  for (auto assocType : unresolvedAssocTypes) {
    switch (checker.resolveTypeWitnessViaLookup(assocType)) {
    case ResolveWitnessResult::Success:
    case ResolveWitnessResult::ExplicitFailed:
      // A declaration that can become a witness has shown up. Go
      // perform the resolution again now that we have more
      // information.
      return solve(checker);

    case ResolveWitnessResult::Missing:
      // The type witness is still missing. Keep going.
      break;
    }
  }

  // Find the best solution.
  if (!findBestSolution(solutions)) {
    assert(solutions.size() == 1 && "Not a unique best solution?");
    // Form the resulting solution.
    auto &typeWitnesses = solutions.front().TypeWitnesses;
    for (auto assocType : unresolvedAssocTypes) {
      assert(typeWitnesses.count(assocType) == 1 && "missing witness");
      auto replacement = typeWitnesses[assocType].first;
      // FIXME: We can end up here with dependent types that were not folded
      // away for some reason.
      if (replacement->hasDependentMember())
        return None;

      if (replacement->hasArchetype()) {
        replacement = replacement->mapTypeOutOfContext();
      }

      result->push_back({assocType, replacement});
    }

    return result;
  }

  // Diagnose the complete lack of solutions.
  if (solutions.empty() &&
      diagnoseNoSolutions(unresolvedAssocTypes.getArrayRef(), checker))
    return None;

  // Diagnose ambiguous solutions.
  if (!solutions.empty() &&
      diagnoseAmbiguousSolutions(unresolvedAssocTypes.getArrayRef(), checker,
                                 solutions))
    return None;

  // Save the missing type witnesses for later diagnosis.
  for (auto assocType : unresolvedAssocTypes) {
    checker.GlobalMissingWitnesses.insert({assocType, {}});
  }

  return None;
}

void ConformanceChecker::resolveTypeWitnesses() {
  // Attempt to infer associated type witnesses.
  AssociatedTypeInference inference(getASTContext(), Conformance);
  if (auto inferred = inference.solve(*this)) {
    for (const auto &inferredWitness : *inferred) {
      recordTypeWitness(inferredWitness.first, inferredWitness.second,
                        /*typeDecl=*/nullptr);
    }

    return;
  }

  // Conformance failed. Record errors for each of the witnesses.
  Conformance->setInvalid();

  // We're going to produce an error below. Mark each unresolved
  // associated type witness as erroneous.
  for (auto assocType : Proto->getAssociatedTypeMembers()) {
    // If we already have a type witness, do nothing.
    if (Conformance->hasTypeWitness(assocType))
      continue;

    recordTypeWitness(assocType, ErrorType::get(getASTContext()), nullptr);
  }
}

void ConformanceChecker::resolveSingleTypeWitness(
       AssociatedTypeDecl *assocType) {
  // Ensure we diagnose if the witness is missing.
  SWIFT_DEFER {
    diagnoseMissingWitnesses(MissingWitnessDiagnosisKind::ErrorFixIt);
  };
  switch (resolveTypeWitnessViaLookup(assocType)) {
  case ResolveWitnessResult::Success:
  case ResolveWitnessResult::ExplicitFailed:
    // We resolved this type witness one way or another.
    return;

  case ResolveWitnessResult::Missing:
    // The type witness is still missing. Resolve all of the type witnesses.
    resolveTypeWitnesses();
    return;
  }
}

void ConformanceChecker::resolveSingleWitness(ValueDecl *requirement) {
  assert(!isa<AssociatedTypeDecl>(requirement) && "Not a value witness");
  assert(!Conformance->hasWitness(requirement) && "Already resolved");

  // Note that we're resolving this witness.
  assert(ResolvingWitnesses.count(requirement) == 0 && "Currently resolving");
  ResolvingWitnesses.insert(requirement);
  SWIFT_DEFER { ResolvingWitnesses.erase(requirement); };

  // Make sure we've validated the requirement.
  if (requirement->isInvalid()) {
    Conformance->setInvalid();
    return;
  }

  if (!requirement->isProtocolRequirement())
    return;

  // Resolve all associated types before trying to resolve this witness.
  resolveTypeWitnesses();

  // If any of the type witnesses was erroneous, don't bother to check
  // this value witness: it will fail.
  for (auto assocType : getReferencedAssociatedTypes(requirement)) {
    if (Conformance->getTypeWitness(assocType)->hasError()) {
      Conformance->setInvalid();
      return;
    }
  }

  // Try to resolve the witness.
  switch (resolveWitnessTryingAllStrategies(requirement)) {
  case ResolveWitnessResult::Success:
    return;

  case ResolveWitnessResult::ExplicitFailed:
    Conformance->setInvalid();
    recordInvalidWitness(requirement);
    return;

  case ResolveWitnessResult::Missing:
    llvm_unreachable("Should have failed");
  }
}
