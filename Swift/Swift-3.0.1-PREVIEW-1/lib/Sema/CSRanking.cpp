//===--- CSRanking.cpp - Constraint System Ranking ------------------------===//
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
// This file implements solution ranking heuristics for the
// constraint-based type checker.
//
//===----------------------------------------------------------------------===//
#include "ConstraintSystem.h"
#include "swift/AST/ArchetypeBuilder.h"
#include "llvm/ADT/Statistic.h"

using namespace swift;
using namespace constraints;

//===----------------------------------------------------------------------===//
// Statistics
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "Constraint solver overall"
STATISTIC(NumDiscardedSolutions, "Number of solutions discarded");

void ConstraintSystem::increaseScore(ScoreKind kind) {
  unsigned index = static_cast<unsigned>(kind);
  ++CurrentScore.Data[index];

  if (TC.getLangOpts().DebugConstraintSolver) {
    auto &log = getASTContext().TypeCheckerDebug->getStream();
    if (solverState)
      log.indent(solverState->depth * 2);
    log << "(increasing score due to ";
    switch (kind) {
    case SK_Unavailable:
      log << "use of an unavailable declaration";
      break;

    case SK_Fix:
      log << "attempting to fix the source";
      break;

    case SK_ForceUnchecked:
      log << "force of an implicitly unwrapped optional";
      break;

    case SK_UserConversion:
      log << "user conversion";
      break;

    case SK_FunctionConversion:
      log << "function conversion";
      break;

    case SK_NonDefaultLiteral:
      log << "non-default literal";
      break;
        
    case SK_CollectionUpcastConversion:
      log << "collection upcast conversion";
      break;
    case SK_CollectionBridgedConversion:
      log << "collection bridged conversion";
      break;
        
    case SK_ValueToOptional:
      log << "value to optional";
      break;

    case SK_ArrayPointerConversion:
      log << "array-to-pointer conversion";
      break;
    case SK_ScalarPointerConversion:
      log << "scalar-to-pointer conversion";
      break;
    case SK_EmptyExistentialConversion:
      log << "empty-existential conversion";
      break;
    }
    log << ")\n";
  }
}

bool ConstraintSystem::worseThanBestSolution() const {
  if (!solverState || !solverState->BestScore ||
      CurrentScore <= *solverState->BestScore)
    return false;

  if (TC.getLangOpts().DebugConstraintSolver) {
    auto &log = getASTContext().TypeCheckerDebug->getStream();
    log.indent(solverState->depth * 2)
      << "(solution is worse than the best solution)\n";
  }

  return true;
}

llvm::raw_ostream &constraints::operator<<(llvm::raw_ostream &out,
                                           const Score &score) {
  for (unsigned i = 0; i != NumScoreKinds; ++i) {
    if (i) out << ' ';
    out << score.Data[i];
  }
  return out;
}

/// \brief Remove the initializers from any tuple types within the
/// given type.
static Type stripInitializers(Type origType) {
  return origType.transform([&](Type type) -> Type {
             if (auto tupleTy = type->getAs<TupleType>()) {
               SmallVector<TupleTypeElt, 4> fields;
               for (const auto &field : tupleTy->getElements()) {
                 fields.push_back(TupleTypeElt(field.getType(),
                                               field.getName(),
                                               field.isVararg()));
                                               
               }
               return TupleType::get(fields, type->getASTContext());
             }
             return type;
           });
}

///\ brief Compare two declarations for equality when they are used.
///
static bool sameDecl(Decl *decl1, Decl *decl2) {
  if (decl1 == decl2)
    return true;

  // All types considered identical.
  // FIXME: This is a hack. What we really want is to have substituted the
  // base type into the declaration reference, so that we can compare the
  // actual types to which two type declarations resolve. If those types are
  // equivalent, then it doesn't matter which declaration is chosen.
  if (isa<TypeDecl>(decl1) && isa<TypeDecl>(decl2))
    return true;
  
  if (decl1->getKind() != decl2->getKind())
    return false;

  return false;
}

/// \brief Compare two overload choices for equality.
static bool sameOverloadChoice(const OverloadChoice &x,
                               const OverloadChoice &y) {
  if (x.getKind() != y.getKind())
    return false;

  switch (x.getKind()) {
  case OverloadChoiceKind::BaseType:
    // FIXME: Compare base types after substitution?
    return true;

  case OverloadChoiceKind::Decl:
  case OverloadChoiceKind::DeclViaDynamic:
  case OverloadChoiceKind::DeclViaBridge:
  case OverloadChoiceKind::DeclViaUnwrappedOptional:
    return sameDecl(x.getDecl(), y.getDecl());

  case OverloadChoiceKind::TypeDecl:
    // FIXME: Compare types after substitution?
    return sameDecl(x.getDecl(), y.getDecl());

  case OverloadChoiceKind::TupleIndex:
    return x.getTupleIndex() == y.getTupleIndex();
  }
}

/// Compare two declarations to determine whether one is a witness of the other.
static Comparison compareWitnessAndRequirement(TypeChecker &tc, DeclContext *dc,
                                               ValueDecl *decl1,
                                               ValueDecl *decl2) {
  // We only have a witness/requirement pair if exactly one of the declarations
  // comes from a protocol.
  auto proto1 = dyn_cast<ProtocolDecl>(decl1->getDeclContext());
  auto proto2 = dyn_cast<ProtocolDecl>(decl2->getDeclContext());
  if ((bool)proto1 == (bool)proto2)
    return Comparison::Unordered;

  // Figure out the protocol, requirement, and potential witness.
  ProtocolDecl *proto;
  ValueDecl *req;
  ValueDecl *potentialWitness;
  if (proto1) {
    proto = proto1;
    req = decl1;
    potentialWitness = decl2;
  } else {
    proto = proto2;
    req = decl2;
    potentialWitness = decl1;
  }

  // Cannot compare type declarations this way.
  // FIXME: Use the same type-substitution approach as lookupMemberType.
  if (isa<TypeDecl>(req))
    return Comparison::Unordered;

  if (!potentialWitness->getDeclContext()->isTypeContext())
    return Comparison::Unordered;

  // Determine whether the type of the witness's context conforms to the
  // protocol.
  auto owningType
    = potentialWitness->getDeclContext()->getDeclaredTypeInContext();
  ProtocolConformance *conformance = nullptr;
  if (!tc.conformsToProtocol(owningType, proto, dc,
                             ConformanceCheckFlags::InExpression, &conformance) ||
      !conformance)
    return Comparison::Unordered;

  // If the witness and the potential witness are not the same, there's no
  // ordering here.
  if (conformance->getWitness(req, &tc).getDecl() != potentialWitness)
    return Comparison::Unordered;

  // We have a requirement/witness match.
  return proto1? Comparison::Worse : Comparison::Better;
}

namespace {
  /// Describes the relationship between the context types for two declarations.
  enum class SelfTypeRelationship {
    /// The types are unrelated; ignore the bases entirely.
    Unrelated,
    /// The types are equivalent.
    Equivalent,
    /// The first type is a subclass of the second.
    Subclass,
    /// The second type is a subclass of the first.
    Superclass,
    /// The first type conforms to the second
    ConformsTo,
    /// The second type conforms to the first.
    ConformedToBy
  };
}

/// Determines whether the first type is nominally a superclass of the second
/// type, ignore generic arguments.
static bool isNominallySuperclassOf(TypeChecker &tc, Type type1, Type type2) {
  auto nominal1 = type1->getAnyNominal();
  if (!nominal1)
    return false;

  for (auto super2 = type2; super2; super2 = super2->getSuperclass(&tc)) {
    if (super2->getAnyNominal() == nominal1)
      return true;
  }

  return false;
}

/// Determine the relationship between the self types of the given declaration
/// contexts..
static SelfTypeRelationship computeSelfTypeRelationship(TypeChecker &tc,
                                                        DeclContext *dc,
                                                        DeclContext *dc1,
                                                        DeclContext *dc2){
  // If at least one of the contexts is a non-type context, the two are
  // unrelated.
  if (!dc1->isTypeContext() || !dc2->isTypeContext())
    return SelfTypeRelationship::Unrelated;

  Type type1 = dc1->getDeclaredTypeInContext();
  Type type2 = dc2->getDeclaredTypeInContext();

  // If the types are equal, the answer is simple.
  if (type1->isEqual(type2))
    return SelfTypeRelationship::Equivalent;

  // If both types can have superclasses, which whether one is a superclass
  // of the other. The subclass is the common base type.
  if (type1->mayHaveSuperclass() && type2->mayHaveSuperclass()) {
    if (isNominallySuperclassOf(tc, type1, type2))
      return SelfTypeRelationship::Superclass;

    if (isNominallySuperclassOf(tc, type2, type1))
      return SelfTypeRelationship::Subclass;

    return SelfTypeRelationship::Unrelated;
  }

  // If neither or both are protocol types, consider the bases unrelated.
  bool isProtocol1 = isa<ProtocolDecl>(dc1);
  bool isProtocol2 = isa<ProtocolDecl>(dc2);
  if (isProtocol1 == isProtocol2)
    return SelfTypeRelationship::Unrelated;

  // Just one of the two is a protocol. Check whether the other conforms to
  // that protocol.
  Type protoTy = isProtocol1? type1 : type2;
  Type modelTy = isProtocol1? type2 : type1;
  auto proto = protoTy->castTo<ProtocolType>()->getDecl();

  // If the model type does not conform to the protocol, the bases are
  // unrelated.
  if (!tc.conformsToProtocol(modelTy, proto, dc,
                             ConformanceCheckFlags::InExpression))
    return SelfTypeRelationship::Unrelated;

  return isProtocol1? SelfTypeRelationship::ConformedToBy
                    : SelfTypeRelationship::ConformsTo;
}

// Given a type and a declaration context, return a type with a curried
// 'self' type as input if the declaration context describes a type.
static Type addCurriedSelfType(ASTContext &ctx, Type type, DeclContext *dc) {
  if (!dc->isTypeContext())
    return type;

  auto nominal = dc->getAsNominalTypeOrNominalTypeExtensionContext();
  auto selfTy = nominal->getInterfaceType()->castTo<MetatypeType>()
                  ->getInstanceType();
  if (auto sig = nominal->getGenericSignatureOfContext())
    return GenericFunctionType::get(sig, selfTy, type,
                                    AnyFunctionType::ExtInfo());
  return FunctionType::get(selfTy, type);
}

/// \brief Given two generic function declarations, signal if the first is more
/// "constrained" than the second by comparing the number of constraints
/// applied to each type parameter.
/// Note that this is not a subtype or conversion check - that takes place
/// in isDeclAsSpecializedAs.
static bool isDeclMoreConstrainedThan(ValueDecl *decl1, ValueDecl *decl2) {
  
  if (decl1->getKind() != decl2->getKind() || isa<TypeDecl>(decl1))
    return false;
  
  auto func1 = dyn_cast<FuncDecl>(decl1);
  auto func2 = dyn_cast<FuncDecl>(decl2);
  
  if (func1 && func2) {
    
    auto gp1 = func1->getGenericParams();
    auto gp2 = func2->getGenericParams();
    
    if (gp1 && gp2) {
      auto params1 = gp1->getParams();
      auto params2 = gp2->getParams();
      
      if (params1.size() == params2.size()) {
        for (size_t i = 0; i < params1.size(); i++) {
          auto p1 = params1[i];
          auto p2 = params2[i];
          
          int np1 = static_cast<int>
          (p1->getArchetype()->getConformsTo().size());
          int np2 = static_cast<int>
          (p2->getArchetype()->getConformsTo().size());
          int aDelta = np1 - np2;
          
          if (aDelta)
            return aDelta > 0;
        }
      }
    }
  }
  
  return false;
}

static Type getTypeAtIndex(const ParameterList *params, size_t index) {
  if (params->size() == 0)
    return nullptr;

  if (index < params->size()) {
    auto param = params->get(index);
    if (param->isVariadic())
      return param->getVarargBaseTy();
  
    return param->getType();
  }
  
  /// FIXME: This looks completely wrong for varargs within a parameter list.
  if (params->size() != 0) {
    auto lastParam = params->getArray().back();
    if (lastParam->isVariadic())
      return lastParam->getVarargBaseTy();
  }
  
  return nullptr;
}

/// For two function declarations, determine if a parameter of the second is an
/// empty existential composition ("Any"), and if it would otherwise be compared
/// against a non-existential parameter at the same position of the first decl.
/// This is used to disambiguate function overloads that would otherwise be
/// identical after opening their parameter types.
static bool hasEmptyExistentialParameterMismatch(ValueDecl *decl1,
                                                 ValueDecl *decl2) {
  auto func1 = dyn_cast<FuncDecl>(decl1);
  auto func2 = dyn_cast<FuncDecl>(decl2);
  if (!func1 || !func2) return false;
    
  auto pl1 = func1->getParameterLists();
  auto pl2 = func2->getParameterLists();
  
  auto pc = std::min(pl1.size(), pl2.size());
  
  for (size_t i = 0; i < pc; i++) {
    auto t1 = getTypeAtIndex(pl1[i], i);
    auto t2 = getTypeAtIndex(pl2[i], i);
    if (!t1 || !t2)
      return false;
    
    if (t2->isAnyExistentialType() && !t1->isAnyExistentialType())
      return t2->isEmptyExistentialComposition();
  }
  return false;
}

/// Determine whether one protocol extension is at least as specialized as
/// another.
static bool isProtocolExtensionAsSpecializedAs(TypeChecker &tc,
                                               DeclContext *dc1,
                                               DeclContext *dc2) {
  assert(dc1->getAsProtocolExtensionContext());
  assert(dc2->getAsProtocolExtensionContext());

  // If one of the protocols being extended inherits the other, prefer the
  // more specialized protocol.
  auto proto1 = dc1->getAsProtocolExtensionContext();
  auto proto2 = dc2->getAsProtocolExtensionContext();
  if (proto1 != proto2) {
    if (proto1->inheritsFrom(proto2))
      return true;
    if (proto2->inheritsFrom(proto1))
      return false;
  }


  // If the two generic signatures are identical, neither is as specialized
  // as the other.
  GenericSignature *sig1 = dc1->getGenericSignatureOfContext();
  GenericSignature *sig2 = dc2->getGenericSignatureOfContext();
  if (sig1->getCanonicalSignature() == sig2->getCanonicalSignature())
    return false;

  // Form a constraint system where we've opened up all of the requirements of
  // the second protocol extension.
  ConstraintSystem cs(tc, dc1, None);
  llvm::DenseMap<CanType, TypeVariableType *> replacements;
  cs.openGeneric(dc2, dc2, sig2,
                 /*skipProtocolSelfConstraint=*/false,
                 ConstraintLocatorBuilder(nullptr),
                 replacements);

  // Bind the 'Self' type from the first extension to the type parameter from
  // opening 'Self' of the second extension.
  Type selfType1 = sig1->getGenericParams()[0];
  Type selfType2 = sig2->getGenericParams()[0];
  cs.addConstraint(ConstraintKind::Bind,
                   replacements[selfType2->getCanonicalType()],
                   ArchetypeBuilder::mapTypeIntoContext(dc1, selfType1),
                   nullptr);

  // Solve the system. If the first extension is at least as specialized as the
  // second, we're done.
  return cs.solveSingle().hasValue();
}

/// \brief Determine whether the first declaration is as "specialized" as
/// the second declaration.
///
/// "Specialized" is essentially a form of subtyping, defined below.
static bool isDeclAsSpecializedAs(TypeChecker &tc, DeclContext *dc,
                                  ValueDecl *decl1, ValueDecl *decl2) {

  if (tc.getLangOpts().DebugConstraintSolver) {
    auto &log = tc.Context.TypeCheckerDebug->getStream();
    log << "Comparing declarations\n";
    decl1->print(log); 
    log << "\nand\n";
    decl2->print(log);
    log << "\n";
  }

  if (!tc.specializedOverloadComparisonCache.count({decl1, decl2})) {

    auto compareSpecializations = [&] () -> bool {
      // If the kinds are different, there's nothing we can do.
      // FIXME: This is wrong for type declarations, which we're skipping
      // entirely.
      if (decl1->getKind() != decl2->getKind() || isa<TypeDecl>(decl1))
        return false;

      // A non-generic declaration is more specialized than a generic declaration.
      if (auto func1 = dyn_cast<AbstractFunctionDecl>(decl1)) {
        auto func2 = cast<AbstractFunctionDecl>(decl2);
        if (static_cast<bool>(func1->getGenericParams()) !=
              static_cast<bool>(func2->getGenericParams()))
          return func2->getGenericParams();
      }

      // A witness is always more specialized than the requirement it satisfies.
      switch (compareWitnessAndRequirement(tc, dc, decl1, decl2)) {
      case Comparison::Unordered:
        break;

      case Comparison::Better:
        return true;

      case Comparison::Worse:
        return false;
      }

      // Members of protocol extensions have special overloading rules.
      ProtocolDecl *inProtocolExtension1 = decl1->getDeclContext()
                                             ->getAsProtocolExtensionContext();
      ProtocolDecl *inProtocolExtension2 = decl2->getDeclContext()
                                             ->getAsProtocolExtensionContext();
      if (inProtocolExtension1 && inProtocolExtension2) {
        // Both members are in protocol extensions.
        // Determine whether the 'Self' type from the first protocol extension
        // satisfies all of the requirements of the second protocol extension.
        DeclContext *dc1 = decl1->getDeclContext();
        DeclContext *dc2 = decl2->getDeclContext();
        bool better1 = isProtocolExtensionAsSpecializedAs(tc, dc1, dc2);
        bool better2 = isProtocolExtensionAsSpecializedAs(tc, dc2, dc1);
        if (better1 != better2) {
          return better1;
        }
      } else if (inProtocolExtension1 || inProtocolExtension2) {
        // One member is in a protocol extension, the other is in a concrete type.
        // Prefer the member in the concrete type.
        return inProtocolExtension2;
      }

      Type type1 = decl1->getInterfaceType();
      Type type2 = decl2->getInterfaceType();

      /// What part of the type should we check?
      enum {
        CheckAll,
        CheckInput,
      } checkKind;
      if (isa<AbstractFunctionDecl>(decl1) || isa<EnumElementDecl>(decl1)) {
        // Nothing to do: these have the curried 'self' already.
        if (auto elt = dyn_cast<EnumElementDecl>(decl1)) {
          checkKind = elt->hasArgumentType() ? CheckInput : CheckAll;
        } else {
          checkKind = CheckInput;
        }
      } else {
        // Add a curried 'self' type.
        assert(!type1->is<GenericFunctionType>() && "Odd generic function type?");
        assert(!type2->is<GenericFunctionType>() && "Odd generic function type?");
        type1 = addCurriedSelfType(tc.Context, type1, decl1->getDeclContext());
        type2 = addCurriedSelfType(tc.Context, type2, decl2->getDeclContext());

        // For a subscript declaration, only look at the input type (i.e., the
        // indices).
        if (isa<SubscriptDecl>(decl1))
          checkKind = CheckInput;
        else
          checkKind = CheckAll;
      }

      // Construct a constraint system to compare the two declarations.
      ConstraintSystem cs(tc, dc, ConstraintSystemOptions());

      auto locator = cs.getConstraintLocator(nullptr);
      // FIXME: Locator when anchored on a declaration.
      // Get the type of a reference to the second declaration.
      llvm::DenseMap<CanType, TypeVariableType *> unused;
      Type openedType2;
      if (auto *funcType = type2->getAs<AnyFunctionType>()) {
        openedType2 = cs.openFunctionType(
            funcType, /*numArgumentLabelsToRemove=*/0, locator,
            /*replacements=*/unused,
            decl2->getInnermostDeclContext(),
            decl2->getDeclContext(),
            /*skipProtocolSelfConstraint=*/false);
      } else {
        openedType2 = cs.openType(type2, locator, unused);
      }

      // Get the type of a reference to the first declaration, swapping in
      // archetypes for the dependent types.
      llvm::DenseMap<CanType, TypeVariableType *> replacements;
      auto dc1 = decl1->getInnermostDeclContext();
      Type openedType1;
      if (auto *funcType = type1->getAs<AnyFunctionType>()) {
        openedType1 = cs.openFunctionType(
            funcType, /*numArgumentLabelsToRemove=*/0, locator,
            replacements,
            dc1,
            decl1->getDeclContext(),
            /*skipProtocolSelfConstraint=*/false);
      } else {
        openedType1 = cs.openType(type1, locator, replacements);
      }

      for (const auto &replacement : replacements) {
        if (auto mapped = 
                  ArchetypeBuilder::mapTypeIntoContext(dc1,
                                                       replacement.first)) {
          cs.addConstraint(ConstraintKind::Bind, replacement.second, mapped,
                           locator);
        }
      }

      // Extract the self types from the declarations, if they have them.
      Type selfTy1;
      Type selfTy2;
      if (decl1->getDeclContext()->isTypeContext()) {
        auto funcTy1 = openedType1->castTo<FunctionType>();
        selfTy1 = funcTy1->getInput()->getRValueInstanceType();
        openedType1 = funcTy1->getResult();
      }
      if (decl2->getDeclContext()->isTypeContext()) {
        auto funcTy2 = openedType2->castTo<FunctionType>();
        selfTy2 = funcTy2->getInput()->getRValueInstanceType();
        openedType2 = funcTy2->getResult();
      }
      
      // Determine the relationship between the 'self' types and add the
      // appropriate constraints. The constraints themselves never fail, but
      // they help deduce type variables that were opened.
      switch (computeSelfTypeRelationship(tc, dc, decl1->getDeclContext(),
                                          decl2->getDeclContext())) {
      case SelfTypeRelationship::Unrelated:
        // Skip the self types parameter entirely.
        break;

      case SelfTypeRelationship::Equivalent:
        cs.addConstraint(ConstraintKind::Equal, selfTy1, selfTy2, locator);
        break;

      case SelfTypeRelationship::Subclass:
        cs.addConstraint(ConstraintKind::Subtype, selfTy1, selfTy2, locator);
        break;

      case SelfTypeRelationship::Superclass:
        cs.addConstraint(ConstraintKind::Subtype, selfTy2, selfTy1, locator);
        break;

      case SelfTypeRelationship::ConformsTo:
        cs.addConstraint(ConstraintKind::ConformsTo, selfTy1,
                         cast<ProtocolDecl>(decl2->getDeclContext())
                           ->getDeclaredType(),
                         locator);
        break;

      case SelfTypeRelationship::ConformedToBy:
        cs.addConstraint(ConstraintKind::ConformsTo, selfTy2,
                         cast<ProtocolDecl>(decl1->getDeclContext())
                           ->getDeclaredType(),
                         locator);
        break;
      }

      bool fewerEffectiveParameters = false;
      switch (checkKind) {
      case CheckAll:
        // Check whether the first type is a subtype of the second.
        cs.addConstraint(ConstraintKind::Subtype,
                         openedType1,
                         openedType2,
                         locator);
        break;

      case CheckInput: {
        // Check whether the first function type's input is a subtype of the
        // second type's inputs, i.e., can we forward the arguments?
        auto funcTy1 = openedType1->castTo<FunctionType>();
        auto funcTy2 = openedType2->castTo<FunctionType>();
        SmallVector<CallArgParam, 4> params1 =
          decomposeParamType(funcTy1->getInput(), decl1,
                             decl1->getDeclContext()->isTypeContext());
        SmallVector<CallArgParam, 4> params2 =
          decomposeParamType(funcTy2->getInput(), decl2,
                             decl2->getDeclContext()->isTypeContext());

        unsigned numParams1 = params1.size();
        unsigned numParams2 = params2.size();
        if (numParams1 > numParams2) return false;

        for (unsigned i = 0; i != numParams2; ++i) {
          // If there is no corresponding argument in the first
          // parameter list...
          if (i >= numParams1) {
            // We need either a default argument or a variadic
            // argument for the first declaration to be more
            // specialized.
            if (!params2[i].HasDefaultArgument && !params2[i].Variadic)
              return false;

            fewerEffectiveParameters = true;
            continue;
          }

          // Labels must match.
          if (params1[i].Label != params2[i].Label) return false;

          // If one parameter is variadic and the other is not...
          if (params1[i].Variadic != params2[i].Variadic) {
            // If the first parameter is the variadic one, it's not
            // more specialized.
            if (params1[i].Variadic) return false;

            fewerEffectiveParameters = true;
          }

          // Check whether the first parameter is a subtype of the second.
          cs.addConstraint(ConstraintKind::Subtype,
                           params1[i].Ty, params2[i].Ty, locator);
        }

        break;
      }
      }

      // Solve the system.
      auto solution = cs.solveSingle(FreeTypeVariableBinding::Allow);

      // Ban value-to-optional conversions.
      if (solution && solution->getFixedScore().Data[SK_ValueToOptional] == 0)
        return true;

      // If the first function has fewer effective parameters than the
      // second, it is more specialized.
      if (fewerEffectiveParameters) return true;

      return false;
    };

    tc.specializedOverloadComparisonCache[{decl1, decl2}] = 
        compareSpecializations();
  } else if (tc.getLangOpts().DebugConstraintSolver) {
    auto &log = tc.Context.TypeCheckerDebug->getStream();
    log << "Found cached comparison: " 
        << tc.specializedOverloadComparisonCache[{decl1, decl2}] << "\n";
  }

  return tc.specializedOverloadComparisonCache[{decl1, decl2}];
}

Comparison TypeChecker::compareDeclarations(DeclContext *dc,
                                            ValueDecl *decl1,
                                            ValueDecl *decl2){
  bool decl1Better = isDeclAsSpecializedAs(*this, dc, decl1, decl2);
  bool decl2Better = isDeclAsSpecializedAs(*this, dc, decl2, decl1);

  if (decl1Better == decl2Better)
    return Comparison::Unordered;

  return decl1Better? Comparison::Better : Comparison::Worse;
}

SolutionCompareResult
ConstraintSystem::compareSolutions(ConstraintSystem &cs,
                                   ArrayRef<Solution> solutions,
                                   const SolutionDiff &diff,
                                   unsigned idx1, unsigned idx2) {

  if (cs.TC.getLangOpts().DebugConstraintSolver) {
    auto &log = cs.getASTContext().TypeCheckerDebug->getStream();
    log.indent(cs.solverState->depth * 2)
      << "comparing solutions " << idx1 << " and " << idx2 <<"\n";
  }

  // Whether the solutions are identical.
  bool identical = true;

  // Compare the fixed scores by themselves.
  if (solutions[idx1].getFixedScore() != solutions[idx2].getFixedScore()) {
    return solutions[idx1].getFixedScore() < solutions[idx2].getFixedScore()
             ? SolutionCompareResult::Better
             : SolutionCompareResult::Worse;
  }
  
  // Compute relative score.
  unsigned score1 = 0;
  unsigned score2 = 0;
  
  auto foundRefinement1 = false;
  auto foundRefinement2 = false;

  bool isStdlibOptionalMPlusOperator1 = false;
  bool isStdlibOptionalMPlusOperator2 = false;

  // Compare overload sets.
  for (auto &overload : diff.overloads) {
    auto choice1 = overload.choices[idx1];
    auto choice2 = overload.choices[idx2];

    // If the systems made the same choice, there's nothing interesting here.
    if (sameOverloadChoice(choice1, choice2))
      continue;

    auto decl1 = choice1.getDecl();
    auto dc1 = decl1->getDeclContext();
    auto decl2 = choice2.getDecl();
    auto dc2 = decl2->getDeclContext();

    // The two systems are not identical. If the decls in question are distinct
    // protocol members, let the checks below determine if the two choices are
    // 'identical' or not. This allows us to structurally unify disparate
    // protocol members during overload resolution.
    // FIXME: Along with the FIXME below, this is a hack to work around
    // problems with restating requirements in protocols.
    identical = false;
    bool decl1InSubprotocol = false;
    bool decl2InSubprotocol = false;
    if (dc1->getContextKind() == DeclContextKind::GenericTypeDecl &&
        dc1->getContextKind() == dc2->getContextKind()) {
      auto pd1 = dyn_cast<ProtocolDecl>(dc1);
      auto pd2 = dyn_cast<ProtocolDecl>(dc2);

      // FIXME: This hack tells us to prefer members of subprotocols over
      // those of the protocols they inherit, if all else fails.
      // If we were properly handling overrides of protocol members when
      // requirements get restated, it would not be necessary.
      if (pd1 && pd2 && pd1 != pd2) {
        identical = true;
        decl1InSubprotocol = pd1->inheritsFrom(pd2);
        decl2InSubprotocol = pd2->inheritsFrom(pd1);
      }
    }
    
    // If the kinds of overload choice don't match...
    if (choice1.getKind() != choice2.getKind()) {
      identical = false;
      
      // A declaration found directly beats any declaration found via dynamic
      // lookup, bridging, or optional unwrapping.
      if (choice1.getKind() == OverloadChoiceKind::Decl &&
          (choice2.getKind() == OverloadChoiceKind::DeclViaDynamic || 
           choice2.getKind() == OverloadChoiceKind::DeclViaBridge ||
           choice2.getKind() == OverloadChoiceKind::DeclViaUnwrappedOptional)) {
        ++score1;
        continue;
      }

      if ((choice1.getKind() == OverloadChoiceKind::DeclViaDynamic ||
           choice1.getKind() == OverloadChoiceKind::DeclViaBridge ||
           choice1.getKind() == OverloadChoiceKind::DeclViaUnwrappedOptional) &&
          choice2.getKind() == OverloadChoiceKind::Decl) {
        ++score2;
        continue;
      }

      continue;
    }

    // The kinds of overload choice match, but the contents don't.
    auto &tc = cs.getTypeChecker();
    switch (choice1.getKind()) {
    case OverloadChoiceKind::TupleIndex:
    case OverloadChoiceKind::TypeDecl:
      continue;

    case OverloadChoiceKind::BaseType:
      llvm_unreachable("Never considered different");

    case OverloadChoiceKind::DeclViaDynamic:
    case OverloadChoiceKind::Decl:
    case OverloadChoiceKind::DeclViaBridge:
    case OverloadChoiceKind::DeclViaUnwrappedOptional:
      break;
    }
    
    // Determine whether one declaration is more specialized than the other.
    bool firstAsSpecializedAs = false;
    bool secondAsSpecializedAs = false;
    if (isDeclAsSpecializedAs(tc, cs.DC, decl1, decl2)) {
      ++score1;
      firstAsSpecializedAs = true;
    }
    if (isDeclAsSpecializedAs(tc, cs.DC, decl2, decl1)) {
      ++score2;
      secondAsSpecializedAs = true;
    }

    // If each is as specialized as the other, and both are constructors,
    // check the constructor kind.
    if (firstAsSpecializedAs && secondAsSpecializedAs) {
      if (auto ctor1 = dyn_cast<ConstructorDecl>(decl1)) {
        if (auto ctor2 = dyn_cast<ConstructorDecl>(decl2)) {
          if (ctor1->getInitKind() != ctor2->getInitKind()) {
            if (ctor1->getInitKind() < ctor2->getInitKind())
              ++score1;
            else
              ++score2;
          } else if (ctor1->getInitKind() ==
                     CtorInitializerKind::Convenience) {
            
            // If both are convenience initializers, and the instance type of
            // one is a subtype of the other's, favor the subtype constructor.
            auto resType1 = ctor1->getResultType();
            auto resType2 = ctor2->getResultType();
            
            if (!resType1->isEqual(resType2)) {
              if (tc.isSubtypeOf(resType1, resType2, cs.DC)) {
                ++score1;
              } else if (tc.isSubtypeOf(resType2, resType1, cs.DC)) {
                ++score2;
              }
            }
          }
        }
      }
    }

    // If both declarations come from Clang, and one is a type and the other
    // is a function, prefer the function.
    if (decl1->hasClangNode() &&
        decl2->hasClangNode() &&
        ((isa<TypeDecl>(decl1) &&
          isa<AbstractFunctionDecl>(decl2)) ||
         (isa<AbstractFunctionDecl>(decl1) &&
          isa<TypeDecl>(decl2)))) {
      if (isa<TypeDecl>(decl1))
        ++score2;
      else
        ++score1;
    }

    // A class member is always better than a curried instance member.
    // If the members agree on instance-ness, a property is better than a
    // method (because a method is usually immediately invoked).
    if (!decl1->isInstanceMember() && decl2->isInstanceMember())
      ++score1;
    else if (!decl2->isInstanceMember() && decl1->isInstanceMember())
      ++score2;
    else if (isa<VarDecl>(decl1) && isa<FuncDecl>(decl2))
      ++score1;
    else if (isa<VarDecl>(decl2) && isa<FuncDecl>(decl1))
      ++score2;
    
    // If we haven't found a refinement, record whether one overload is in
    // any way more constrained than another. We'll only utilize this
    // information in the case of a potential ambiguity.
    if (!(foundRefinement1 && foundRefinement2)) {
      if (isDeclMoreConstrainedThan(decl1, decl2)) {
        foundRefinement1 = true;
      }
      
      if (isDeclMoreConstrainedThan(decl2, decl1)) {
        foundRefinement2 = true;
      }
    }
     
    // If we still haven't found a refinement, check if there's a parameter-
    // wise comparison between an empty existential collection and a non-
    // existential type.
    if (!(foundRefinement1 && foundRefinement2)) {
      if (hasEmptyExistentialParameterMismatch(decl1, decl2)) {
        foundRefinement1 = true;
      }
      
      if (hasEmptyExistentialParameterMismatch(decl2, decl1)) {
        foundRefinement2 = true;
      }
    }

    // FIXME: The rest of the hack for restating requirements.
    if (!(foundRefinement1 && foundRefinement2)) {
      if (identical && decl1InSubprotocol != decl2InSubprotocol) {
        foundRefinement1 = decl1InSubprotocol;
        foundRefinement2 = decl2InSubprotocol;
      }
    }

    // FIXME: Lousy hack for ?? to prefer the catamorphism (flattening)
    // over the mplus (non-flattening) overload if all else is equal.
    if (decl1->getName().str() == "??") {
      assert(decl2->getName().str() == "??");

      auto check = [](const ValueDecl *VD) -> bool {
        if (!VD->getModuleContext()->isStdlibModule())
          return false;
        auto fnTy = VD->getType()->castTo<AnyFunctionType>();
        if (!fnTy->getResult()->getAnyOptionalObjectType())
          return false;

        // Check that the standard library hasn't added another overload of
        // the ?? operator.
        auto inputTupleTy = fnTy->getInput()->castTo<TupleType>();
        auto inputTypes = inputTupleTy->getElementTypes();
        assert(inputTypes.size() == 2);
        assert(inputTypes[0]->getAnyOptionalObjectType());
        auto autoclosure = inputTypes[1]->castTo<AnyFunctionType>();
        assert(autoclosure->isAutoClosure());
        auto secondParamTy = autoclosure->getResult();
        assert(secondParamTy->getAnyOptionalObjectType());
        (void)secondParamTy;

        return true;
      };

      isStdlibOptionalMPlusOperator1 = check(decl1);
      isStdlibOptionalMPlusOperator2 = check(decl2);
    }
  }

  // Compare the type variable bindings.
  auto &tc = cs.getTypeChecker();
  for (auto &binding : diff.typeBindings) {
    // If the type variable isn't one for which we should be looking at the
    // bindings, don't.
    if (!binding.typeVar->getImpl().prefersSubtypeBinding())
      continue;

    auto type1 = binding.bindings[idx1];
    auto type2 = binding.bindings[idx2];

    // Strip any initializers from tuples in the type; they aren't
    // to be compared.
    type1 = stripInitializers(type1);
    type2 = stripInitializers(type2);

    // If the types are equivalent, there's nothing more to do.
    if (type1->isEqual(type2))
      continue;
    
    // If either of the types still contains type variables, we can't
    // compare them.
    // FIXME: This is really unfortunate. More type variable sharing
    // (when it's sane) would help us do much better here.
    if (type1->hasTypeVariable() || type2->hasTypeVariable()) {
      identical = false;
      continue;
    }

    // If one type is an implicitly unwrapped optional of the other,
    // prefer the non-optional.    
    bool type1Better = false;
    bool type2Better = false;
    if (auto type1Obj = type1->getImplicitlyUnwrappedOptionalObjectType()) {
      if (type1Obj->isEqual(type2))
        type2Better = true;
    }
    if (auto type2Obj = type2->getImplicitlyUnwrappedOptionalObjectType()) {
      if (type2Obj->isEqual(type1))
        type1Better = true;
    }

    if (type1Better || type2Better) {
      if (type1Better)
        ++score1;
      if (type2Better)
        ++score2;
      continue;
    }

    // If one type is a subtype of the other, but not vice-versa,
    // we prefer the system with the more-constrained type.
    // FIXME: Collapse this check into the second check.
    type1Better = tc.isSubtypeOf(type1, type2, cs.DC);
    type2Better = tc.isSubtypeOf(type2, type1, cs.DC);
    if (type1Better || type2Better) {
      if (type1Better)
        ++score1;
      if (type2Better)
        ++score2;

      // Prefer the unlabeled form of a type.
      auto unlabeled1 = type1->getUnlabeledType(cs.getASTContext());
      auto unlabeled2 = type2->getUnlabeledType(cs.getASTContext());
      if (unlabeled1->isEqual(unlabeled2)) {
        if (type1->isEqual(unlabeled1)) {
          ++score1;
          continue;
        }
        if (type2->isEqual(unlabeled2)) {
          ++score2;
          continue;
        }
      }

      identical = false;
      continue;
    }

    // The systems are not considered equivalent.
    identical = false;

    // If one type is convertible to of the other, but not vice-versa.
    type1Better = tc.isConvertibleTo(type1, type2, cs.DC);
    type2Better = tc.isConvertibleTo(type2, type1, cs.DC);
    if (type1Better || type2Better) {
      if (type1Better)
        ++score1;
      if (type2Better)
        ++score2;
      continue;
    }

    // A concrete type is better than an archetype.
    // FIXME: Total hack.
    if (type1->is<ArchetypeType>() != type2->is<ArchetypeType>()) {
      if (type1->is<ArchetypeType>())
        ++score2;
      else
        ++score1;
      continue;
    }
    
    // FIXME:
    // This terrible hack is in place to support equality comparisons of non-
    // equatable option types to 'nil'. Until we have a way to constrain a type
    // variable on "!Equatable", if all other aspects of the overload choices
    // are equal, favor the overload that does not require an implicit literal
    // argument conversion to 'nil'.
    // Post-1.0, we'll need to remove this hack in favor of richer constraint
    // declarations.
    if (!(score1 || score2)) {
      if (auto nominalType2 = type2->getNominalOrBoundGenericNominal()) {
        if ((nominalType2->getName() ==
             cs.TC.Context.Id_OptionalNilComparisonType)) {
          ++score1;
        }
      } else if (auto nominalType1 = type1->getNominalOrBoundGenericNominal()) {
        if ((nominalType1->getName() ==
             cs.TC.Context.Id_OptionalNilComparisonType)) {
          ++score2;
        }
      }
    }
  }
  
  // All other things considered equal, if any overload choice is more
  // more constrained than the other, increment the score.
  if (score1 == score2) {
    if (foundRefinement1) {
      ++score1;
    }
    if (foundRefinement2) {
      ++score2;
    }
  }

  // FIXME: All other things being equal, prefer the catamorphism (flattening)
  // overload of ?? over the mplus (non-flattening) overload.
  if (score1 == score2) {
    // This is correct: we want to /disprefer/ the mplus.
    score2 += isStdlibOptionalMPlusOperator1;
    score1 += isStdlibOptionalMPlusOperator2;
  }

  // FIXME: There are type variables and overloads not common to both solutions
  // that haven't been considered. They make the systems different, but don't
  // affect ranking. We need to handle this.

  // If the scores are different, we have a winner.
  if (score1 != score2) {
    return score1 > score2? SolutionCompareResult::Better
                          : SolutionCompareResult::Worse;
  }

  // Neither system wins; report whether they were identical or not.
  return identical? SolutionCompareResult::Identical
                  : SolutionCompareResult::Incomparable;
}

Optional<unsigned>
ConstraintSystem::findBestSolution(SmallVectorImpl<Solution> &viable,
                                   bool minimize) {
  if (viable.empty())
    return None;
  if (viable.size() == 1)
    return 0;

  SolutionDiff diff(viable);

  // Find a potential best.
  SmallVector<bool, 16> losers(viable.size(), false);
  unsigned bestIdx = 0;
  for (unsigned i = 1, n = viable.size(); i != n; ++i) {
    switch (compareSolutions(*this, viable, diff, i, bestIdx)) {
    case SolutionCompareResult::Identical:
      // FIXME: Might want to warn about this in debug builds, so we can
      // find a way to eliminate the redundancy in the search space.
    case SolutionCompareResult::Incomparable:
      break;

    case SolutionCompareResult::Worse:
      losers[i] = true;
      break;

    case SolutionCompareResult::Better:
      losers[bestIdx] = true;
      bestIdx = i;
      break;
    }
  }

  // Make sure that our current best is better than all of the solved systems.
  bool ambiguous = false;
  for (unsigned i = 0, n = viable.size(); i != n && !ambiguous; ++i) {
    if (i == bestIdx)
      continue;

    switch (compareSolutions(*this, viable, diff, bestIdx, i)) {
    case SolutionCompareResult::Identical:
      // FIXME: Might want to warn about this in debug builds, so we can
      // find a way to eliminate the redundancy in the search space.
      break;

    case SolutionCompareResult::Better:
      losers[i] = true;
      break;

    case SolutionCompareResult::Worse:
      losers[bestIdx] = true;
      SWIFT_FALLTHROUGH;

    case SolutionCompareResult::Incomparable:
      // If we're not supposed to minimize the result set, just return eagerly.
      if (!minimize)
        return None;

      ambiguous = true;
      break;
    }
  }

  // If the result was not ambiguous, we're done.
  if (!ambiguous) {
    NumDiscardedSolutions += viable.size() - 1;
    return bestIdx;
  }

  // The comparison was ambiguous. Identify any solutions that are worse than
  // any other solution.
  for (unsigned i = 0, n = viable.size(); i != n; ++i) {
    // If the first solution has already lost once, don't bother looking
    // further.
    if (losers[i])
      continue;

    for (unsigned j = i + 1; j != n; ++j) {
      // If the second solution has already lost once, don't bother looking
      // further.
      if (losers[j])
        continue;

      switch (compareSolutions(*this, viable, diff, i, j)) {
      case SolutionCompareResult::Identical:
        // FIXME: Dub one of these the loser arbitrarily?
        break;

      case SolutionCompareResult::Better:
        losers[j] = true;
        break;

      case SolutionCompareResult::Worse:
        losers[i] = true;
        break;

      case SolutionCompareResult::Incomparable:
        break;
      }
    }
  }

  // Remove any solution that is worse than some other solution.
  unsigned outIndex = 0;
  for (unsigned i = 0, n = viable.size(); i != n; ++i) {
    // Skip over the losing solutions.
    if (losers[i])
      continue;

    // If we have skipped any solutions, move this solution into the next
    // open position.
    if (outIndex < i)
      viable[outIndex] = std::move(viable[i]);

    ++outIndex;
  }
  viable.erase(viable.begin() + outIndex, viable.end());
  NumDiscardedSolutions += viable.size() - outIndex;

  return None;
}

SolutionDiff::SolutionDiff(ArrayRef<Solution> solutions) {
  if (solutions.size() <= 1)
    return;

  // Populate the type bindings with the first solution.
  llvm::DenseMap<TypeVariableType *, SmallVector<Type, 2>> typeBindings;
  for (auto binding : solutions[0].typeBindings) {
    typeBindings[binding.first].push_back(binding.second);
  }

  // Populate the overload choices with the first solution.
  llvm::DenseMap<ConstraintLocator *, SmallVector<OverloadChoice, 2>>
    overloadChoices;
  for (auto choice : solutions[0].overloadChoices) {
    overloadChoices[choice.first].push_back(choice.second.choice);
  }

  // Find the type variables and overload locators common to all of the
  // solutions.
  for (auto &solution : solutions.slice(1)) {
    // For each type variable bound in all of the previous solutions, check
    // whether we have a binding for this type variable in this solution.
    SmallVector<TypeVariableType *, 4> removeTypeBindings;
    for (auto &binding : typeBindings) {
      auto known = solution.typeBindings.find(binding.first);
      if (known == solution.typeBindings.end()) {
        removeTypeBindings.push_back(binding.first);
        continue;
      }

      // Add this solution's binding to the results.
      binding.second.push_back(known->second);
    }

    // Remove those type variables for which this solution did not have a
    // binding.
    for (auto typeVar : removeTypeBindings) {
      typeBindings.erase(typeVar);
    }
    removeTypeBindings.clear();

    // For each overload locator for which we have an overload choice in
    // all of the previous solutions. Check whether we have an overload choice
    // in this solution.
    SmallVector<ConstraintLocator *, 4> removeOverloadChoices;
    for (auto &overloadChoice : overloadChoices) {
      auto known = solution.overloadChoices.find(overloadChoice.first);
      if (known == solution.overloadChoices.end()) {
        removeOverloadChoices.push_back(overloadChoice.first);
        continue;
      }

      // Add this solution's overload choice to the results.
      overloadChoice.second.push_back(known->second.choice);
    }

    // Remove those overload locators for which this solution did not have
    // an overload choice.
    for (auto overloadChoice : removeOverloadChoices) {
      overloadChoices.erase(overloadChoice);
    }
  }

  // Look through the type variables that have bindings in all of the
  // solutions, and add those that have differences to the diff.
  for (auto &binding : typeBindings) {
    Type singleType;
    for (auto type : binding.second) {
      if (!singleType)
        singleType = type;
      else if (!singleType->isEqual(type)) {
        // We have a difference. Add this binding to the diff.
        this->typeBindings.push_back(
          SolutionDiff::TypeBindingDiff{
            binding.first,
            std::move(binding.second)
          });

        break;
      }
    }
  }

  // Look through the overload locators that have overload choices in all of
  // the solutions, and add those that have differences to the diff.
  for (auto &overloadChoice : overloadChoices) {
    OverloadChoice singleChoice = overloadChoice.second[0];
    for (auto choice : overloadChoice.second) {
      if (!sameOverloadChoice(singleChoice, choice)) {
        // We have a difference. Add this set of overload choices to the diff.
        this->overloads.push_back(
          SolutionDiff::OverloadDiff{
            overloadChoice.first,
            overloadChoice.second
          });
        
      }
    }
  }
}
