//===--- RewriteContext.cpp - Term rewriting allocation arena -------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/AST/Decl.h"
#include "swift/AST/Types.h"
#include "RequirementMachine.h"
#include "RewriteSystem.h"
#include "RewriteContext.h"
#include "RequirementMachine.h"

using namespace swift;
using namespace rewriting;

/// Build a DebugOptions by parsing a comma-separated list of debug flags.
static DebugOptions parseDebugFlags(StringRef debugFlags) {
  DebugOptions result;

  SmallVector<StringRef, 2> debug;
  debugFlags.split(debug, ',');
  for (auto flagStr : debug) {
    auto flag = llvm::StringSwitch<Optional<DebugFlags>>(flagStr)
      .Case("simplify", DebugFlags::Simplify)
      .Case("add", DebugFlags::Add)
      .Case("merge", DebugFlags::Merge)
      .Case("completion", DebugFlags::Completion)
      .Case("concrete-unification", DebugFlags::ConcreteUnification)
      .Case("concretize-nested-types", DebugFlags::ConcretizeNestedTypes)
      .Case("homotopy-reduction", DebugFlags::HomotopyReduction)
      .Case("generating-conformances", DebugFlags::GeneratingConformances)
      .Case("protocol-dependencies", DebugFlags::ProtocolDependencies)
      .Case("minimization", DebugFlags::Minimization)
      .Default(None);
    if (!flag) {
      llvm::errs() << "Unknown debug flag in -debug-requirement-machine "
                   << flagStr << "\n";
      abort();
    }

    result |= *flag;
  }

  return result;
}

RewriteContext::RewriteContext(ASTContext &ctx)
    : Context(ctx),
      Stats(ctx.Stats),
      SymbolHistogram(Symbol::NumKinds),
      TermHistogram(4, /*Start=*/1),
      RuleTrieHistogram(16, /*Start=*/1),
      RuleTrieRootHistogram(16),
      PropertyTrieHistogram(16, /*Start=*/1),
      PropertyTrieRootHistogram(16) {
  auto debugFlags = StringRef(ctx.LangOpts.DebugRequirementMachine);
  if (!debugFlags.empty())
    Debug = parseDebugFlags(debugFlags);
}

const llvm::TinyPtrVector<const ProtocolDecl *> &
RewriteContext::getInheritedProtocols(const ProtocolDecl *proto) {
  auto found = AllInherited.find(proto);
  if (found != AllInherited.end())
    return found->second;

  AllInherited.insert(std::make_pair(proto, TinyPtrVector<const ProtocolDecl *>()));

  llvm::SmallDenseSet<const ProtocolDecl *, 4> visited;
  llvm::TinyPtrVector<const ProtocolDecl *> protos;

  for (auto *inheritedProto : proto->getInheritedProtocols()) {
    if (!visited.insert(inheritedProto).second)
      continue;

    protos.push_back(inheritedProto);
    const auto &allInherited = getInheritedProtocols(inheritedProto);

    for (auto *otherProto : allInherited) {
      if (!visited.insert(otherProto).second)
        continue;

      protos.push_back(otherProto);
    }
  }

  auto &result = AllInherited[proto];
  std::swap(protos, result);
  return result;
}

unsigned RewriteContext::getProtocolSupport(
    const ProtocolDecl *proto) {
  return getInheritedProtocols(proto).size() + 1;
}

unsigned RewriteContext::getProtocolSupport(
    ArrayRef<const ProtocolDecl *> protos) {
  auto found = Support.find(protos);
  if (found != Support.end())
    return found->second;

  unsigned result;
  if (protos.size() == 1) {
    result = getProtocolSupport(protos[0]);
  } else {
    llvm::DenseSet<const ProtocolDecl *> visited;
    for (const auto *proto : protos) {
      visited.insert(proto);
      for (const auto *inheritedProto : getInheritedProtocols(proto))
        visited.insert(inheritedProto);
    }

    result = visited.size();
  }

  Support[protos] = result;
  return result;
}

int RewriteContext::compareProtocols(const ProtocolDecl *lhs,
                                     const ProtocolDecl *rhs) {
  unsigned lhsSupport = getProtocolSupport(lhs);
  unsigned rhsSupport = getProtocolSupport(rhs);

  if (lhsSupport != rhsSupport)
    return rhsSupport - lhsSupport;

  return TypeDecl::compare(lhs, rhs);
}

Term RewriteContext::getTermForType(CanType paramType,
                                    const ProtocolDecl *proto) {
  return Term::get(getMutableTermForType(paramType, proto), *this);
}

/// Map an interface type to a term.
///
/// If \p proto is null, this is a term relative to a generic
/// parameter in a top-level signature. The term is rooted in a generic
/// parameter symbol.
///
/// If \p proto is non-null, this is a term relative to a protocol's
/// 'Self' type. The term is rooted in a protocol symbol for this protocol,
/// or an associated type symbol for some associated type in this protocol.
///
/// Resolved DependentMemberTypes map to associated type symbols.
/// Unresolved DependentMemberTypes map to name symbols.
///
/// Note the behavior of the root term is special if it is an associated
/// type symbol. The protocol of the associated type is always mapped to
/// \p proto if it was provided. This ensures we get the correct behavior
/// if a protocol places a constraint on an associated type inherited from
/// another protocol:
///
/// protocol P {
///   associatedtype Foo
/// }
///
/// protocol Q : P where Foo : R {}
///
/// protocol R {}
///
/// The DependentMemberType in the requirement signature of Q refers to
/// P::Foo.
///
/// However, we want Q's requirement signature to introduce the rewrite rule
///
///   [Q:Foo].[R] => [Q:Foo]
///
/// and not
///
///   [P:Foo].[R] => [P:Foo]
///
/// This is because the rule only applies to Q's logical override of Foo, and
/// not P's Foo.
///
/// To handle this, getMutableTermForType() behaves as follows:
///
/// Self.P::Foo with proto = P         => [P:Foo]
/// Self.P::Foo with proto = Q         => [Q:Foo]
/// τ_0_0.P::Foo with proto == nullptr => τ_0_0.[P:Foo]
///
MutableTerm RewriteContext::getMutableTermForType(CanType paramType,
                                                  const ProtocolDecl *proto) {
  assert(paramType->isTypeParameter());

  // Collect zero or more nested type names in reverse order.
  bool innermostAssocTypeWasResolved = false;

  SmallVector<Symbol, 3> symbols;
  while (auto memberType = dyn_cast<DependentMemberType>(paramType)) {
    paramType = memberType.getBase();

    if (auto *assocType = memberType->getAssocType()) {
      const auto *thisProto = assocType->getProtocol();
      if (proto && isa<GenericTypeParamType>(paramType)) {
        thisProto = proto;
        innermostAssocTypeWasResolved = true;
      }
      symbols.push_back(Symbol::forAssociatedType(thisProto,
                                                  assocType->getName(),
                                                  *this));
    } else {
      symbols.push_back(Symbol::forName(memberType->getName(), *this));
      innermostAssocTypeWasResolved = false;
    }
  }

  // Add the root symbol at the end.
  if (proto) {
    assert(proto->getSelfInterfaceType()->isEqual(paramType));

    // Self.Foo becomes [P].Foo
    // Self.Q::Foo becomes [P:Foo] (not [Q:Foo] or [P].[Q:Foo])
    if (!innermostAssocTypeWasResolved)
      symbols.push_back(Symbol::forProtocol(proto, *this));
  } else {
    symbols.push_back(Symbol::forGenericParam(
        cast<GenericTypeParamType>(paramType), *this));
  }

  std::reverse(symbols.begin(), symbols.end());

  return MutableTerm(symbols);
}

/// Map an associated type symbol to an associated type declaration.
///
/// Note that the protocol graph is not part of the caching key; each
/// protocol graph is a subgraph of the global inheritance graph, so
/// the specific choice of subgraph does not change the result.
AssociatedTypeDecl *RewriteContext::getAssociatedTypeForSymbol(Symbol symbol) {
  auto found = AssocTypes.find(symbol);
  if (found != AssocTypes.end())
    return found->second;

  assert(symbol.getKind() == Symbol::Kind::AssociatedType);
  auto name = symbol.getName();

  AssociatedTypeDecl *assocType = nullptr;

  // An associated type symbol [P1&P1&...&Pn:A] has one or more protocols
  // P0...Pn and an identifier 'A'.
  //
  // We map it back to a AssociatedTypeDecl as follows:
  //
  // - For each protocol Pn, look for associated types A in Pn itself,
  //   and all protocols that Pn refines.
  //
  // - For each candidate associated type An in protocol Qn where
  //   Pn refines Qn, get the associated type anchor An' defined in
  //   protocol Qn', where Qn refines Qn'.
  //
  // - Out of all the candidiate pairs (Qn', An'), pick the one where
  //   the protocol Qn' is the lowest element according to the linear
  //   order defined by TypeDecl::compare().
  //
  // The associated type An' is then the canonical associated type
  // representative of the associated type symbol [P0&...&Pn:A].
  //
  for (auto *proto : symbol.getProtocols()) {
    auto checkOtherAssocType = [&](AssociatedTypeDecl *otherAssocType) {
      otherAssocType = otherAssocType->getAssociatedTypeAnchor();

      if (otherAssocType->getName() == name &&
          (assocType == nullptr ||
           TypeDecl::compare(otherAssocType->getProtocol(),
                             assocType->getProtocol()) < 0)) {
        assocType = otherAssocType;
      }
    };

    for (auto *otherAssocType : proto->getAssociatedTypeMembers()) {
      checkOtherAssocType(otherAssocType);
    }

    for (auto *inheritedProto : getInheritedProtocols(proto)) {
      for (auto *otherAssocType : inheritedProto->getAssociatedTypeMembers()) {
        checkOtherAssocType(otherAssocType);
      }
    }
  }

  assert(assocType && "Need to look harder");
  AssocTypes[symbol] = assocType;
  return assocType;
}

/// Compute the interface type for a range of symbols, with an optional
/// root type.
///
/// If the root type is specified, we wrap it in a series of
/// DependentMemberTypes. Otherwise, the root is computed from
/// the first symbol of the range.
template<typename Iter>
Type getTypeForSymbolRange(Iter begin, Iter end, Type root,
                           TypeArrayView<GenericTypeParamType> genericParams,
                           const RewriteContext &ctx) {
  Type result = root;

  auto handleRoot = [&](GenericTypeParamType *genericParam) {
    assert(genericParam->isCanonical());

    if (!genericParams.empty()) {
      // Return a sugared GenericTypeParamType if we're given an array of
      // sugared types to substitute.
      unsigned index = GenericParamKey(genericParam).findIndexIn(genericParams);
      result = genericParams[index];
      return;
    }

    // Otherwise, we're going to return a canonical type.
    result = genericParam;
  };

  for (; begin != end; ++begin) {
    auto symbol = *begin;

    if (!result) {
      // A valid term always begins with a generic parameter, protocol or
      // associated type symbol.
      switch (symbol.getKind()) {
      case Symbol::Kind::GenericParam:
        handleRoot(symbol.getGenericParam());
        continue;

      case Symbol::Kind::Protocol:
        handleRoot(GenericTypeParamType::get(/*type sequence*/ false, 0, 0,
                                             ctx.getASTContext()));
        continue;

      case Symbol::Kind::AssociatedType:
        handleRoot(GenericTypeParamType::get(/*type sequence*/ false, 0, 0,
                                             ctx.getASTContext()));

        // An associated type term at the root means we have a dependent
        // member type rooted at Self; handle the associated type below.
        break;

      case Symbol::Kind::Name:
      case Symbol::Kind::Layout:
      case Symbol::Kind::Superclass:
      case Symbol::Kind::ConcreteType:
        llvm_unreachable("Term has invalid root symbol");
      }
    }

    // An unresolved type can appear if we have invalid requirements.
    if (symbol.getKind() == Symbol::Kind::Name) {
      result = DependentMemberType::get(result, symbol.getName());
      continue;
    }

    // We can end up with an unsimplified term like this:
    //
    // X.[P].[P:X]
    //
    // Simplification will rewrite X.[P] to X, so just ignore a protocol symbol
    // in the middle of a term.
    if (symbol.getKind() == Symbol::Kind::Protocol) {
#ifndef NDEBUG
      // Ensure that the domain of the suffix contains P.
      if (begin + 1 < end) {
        auto protos = (begin + 1)->getProtocols();
        assert(std::find(protos.begin(), protos.end(), symbol.getProtocol()));
      }
#endif
      continue;
    }

    // We should have a resolved type at this point.
    auto *assocType =
        const_cast<RewriteContext &>(ctx)
            .getAssociatedTypeForSymbol(symbol);
    result = DependentMemberType::get(result, assocType);
  }

  return result;
}

Type RewriteContext::getTypeForTerm(Term term,
                      TypeArrayView<GenericTypeParamType> genericParams) const {
  return getTypeForSymbolRange(term.begin(), term.end(), Type(),
                               genericParams, *this);
}

Type RewriteContext::getTypeForTerm(const MutableTerm &term,
                      TypeArrayView<GenericTypeParamType> genericParams) const {
  return getTypeForSymbolRange(term.begin(), term.end(), Type(),
                               genericParams, *this);
}

Type RewriteContext::getRelativeTypeForTerm(
    const MutableTerm &term, const MutableTerm &prefix) const {
  assert(std::equal(prefix.begin(), prefix.end(), term.begin()));

  auto genericParam =
      CanGenericTypeParamType::get(/*type sequence*/ false, 0, 0, Context);
  return getTypeForSymbolRange(
      term.begin() + prefix.size(), term.end(), genericParam,
      { }, *this);
}

/// Concrete type terms are written in terms of generic parameter types that
/// have a depth of 0, and an index into an array of substitution terms.
///
/// See RewriteSystemBuilder::getConcreteSubstitutionSchema().
static unsigned getGenericParamIndex(Type type) {
  auto *paramTy = type->castTo<GenericTypeParamType>();
  assert(paramTy->getDepth() == 0);
  return paramTy->getIndex();
}

/// Computes the term corresponding to a member type access on a substitution.
///
/// The type witness is a type parameter of the form τ_0_n.X.Y.Z,
/// where 'n' is an index into the substitution array.
///
/// If the nth entry in the array is S, this will produce S.X.Y.Z.
///
/// There is a special behavior if the substitution is a term consisting of a
/// single protocol symbol [P]. If the innermost associated type in
/// \p typeWitness is [Q:Foo], the result will be [P:Foo], not [P].[Q:Foo] or
/// [Q:Foo].
MutableTerm
RewriteContext::getRelativeTermForType(CanType typeWitness,
                                       ArrayRef<Term> substitutions) {
  MutableTerm result;

  // Get the substitution S corresponding to τ_0_n.
  unsigned index = getGenericParamIndex(typeWitness->getRootGenericParam());
  result = MutableTerm(substitutions[index]);

  // If the substitution is a term consisting of a single protocol symbol
  // [P], save P for later.
  const ProtocolDecl *proto = nullptr;
  if (result.size() == 1 &&
      result[0].getKind() == Symbol::Kind::Protocol) {
    proto = result[0].getProtocol();
  }

  // Collect zero or more member type names in reverse order.
  SmallVector<Symbol, 3> symbols;
  while (auto memberType = dyn_cast<DependentMemberType>(typeWitness)) {
    typeWitness = memberType.getBase();

    auto *assocType = memberType->getAssocType();
    assert(assocType != nullptr &&
           "Conformance checking should not produce unresolved member types");

    // If the substitution is a term consisting of a single protocol symbol [P],
    // produce [P:Foo] instead of [P].[Q:Foo] or [Q:Foo].
    const auto *thisProto = assocType->getProtocol();
    if (proto && isa<GenericTypeParamType>(typeWitness)) {
      thisProto = proto;

      assert(result.size() == 1);
      assert(result[0].getKind() == Symbol::Kind::Protocol);
      assert(result[0].getProtocol() == proto);
      result = MutableTerm();
    }

    symbols.push_back(Symbol::forAssociatedType(thisProto,
                                                assocType->getName(), *this));
  }

  // Add the member type names.
  for (auto iter = symbols.rbegin(), end = symbols.rend(); iter != end; ++iter)
    result.add(*iter);

  return result;
}

/// Reverses the transformation performed by
/// RewriteSystemBuilder::getConcreteSubstitutionSchema().
Type RewriteContext::getTypeFromSubstitutionSchema(
    Type schema, ArrayRef<Term> substitutions,
    TypeArrayView<GenericTypeParamType> genericParams,
    const MutableTerm &prefix) const {
  assert(!schema->isTypeParameter() && "Must have a concrete type here");

  if (!schema->hasTypeParameter())
    return schema;

  return schema.transformRec([&](Type t) -> Optional<Type> {
    if (t->is<GenericTypeParamType>()) {
      auto index = getGenericParamIndex(t);
      auto substitution = substitutions[index];

      // Prepend the prefix of the lookup key to the substitution.
      if (prefix.empty()) {
        // Skip creation of a new MutableTerm in the case where the
        // prefix is empty.
        return getTypeForTerm(substitution, genericParams);
      } else {
        // Otherwise build a new term by appending the substitution
        // to the prefix.
        MutableTerm result(prefix);
        result.append(substitution);
        return getTypeForTerm(result, genericParams);
      }
    }

    assert(!t->isTypeParameter());
    return None;
  });
}

RequirementMachine *RewriteContext::getRequirementMachine(
    CanGenericSignature sig) {
  auto &machine = Machines[sig];
  if (machine) {
    if (!machine->isComplete()) {
      llvm::errs() << "Re-entrant construction of requirement "
                   << "machine for " << sig << "\n";
      abort();
    }

    return machine;
  }

  // Store this requirement machine before adding the signature,
  // to catch re-entrant construction via initWithGenericSignature()
  // below.
  auto *newMachine = new rewriting::RequirementMachine(*this);
  machine = newMachine;

  // This might re-entrantly invalidate 'machine', which is a reference
  // into Protos.
  newMachine->initWithGenericSignature(sig);
  return newMachine;
}

bool RewriteContext::isRecursivelyConstructingRequirementMachine(
    CanGenericSignature sig) {
  auto found = Machines.find(sig);
  if (found == Machines.end())
    return false;

  return !found->second->isComplete();
}

/// Implement Tarjan's algorithm to compute strongly-connected components in
/// the protocol dependency graph.
void RewriteContext::getRequirementMachineRec(
    const ProtocolDecl *proto,
    SmallVectorImpl<const ProtocolDecl *> &stack) {
  assert(Protos.count(proto) == 0);

  // Initialize the next component index and push the entry
  // on the stack
  {
    auto &entry = Protos[proto];
    entry.Index = NextComponentIndex;
    entry.LowLink = NextComponentIndex;
    entry.OnStack = 1;
  }

  NextComponentIndex++;
  stack.push_back(proto);

  // Look at each successor.
  for (auto *depProto : proto->getProtocolDependencies()) {
    auto found = Protos.find(depProto);
    if (found == Protos.end()) {
      // Successor has not yet been visited. Recurse.
      getRequirementMachineRec(depProto, stack);

      auto &entry = Protos[proto];
      assert(Protos.count(depProto) != 0);
      entry.LowLink = std::min(entry.LowLink, Protos[depProto].LowLink);
    } else if (found->second.OnStack) {
      // Successor is on the stack and hence in the current SCC.
      auto &entry = Protos[proto];
      entry.LowLink = std::min(entry.LowLink, found->second.Index);
    }
  }

  auto &entry = Protos[proto];

  // If this a root node, pop the stack and generate an SCC.
  if (entry.LowLink == entry.Index) {
    unsigned id = Components.size();
    SmallVector<const ProtocolDecl *, 3> protos;

    const ProtocolDecl *depProto = nullptr;
    do {
      depProto = stack.back();
      stack.pop_back();

      assert(Protos.count(depProto) != 0);
      Protos[depProto].OnStack = false;
      Protos[depProto].ComponentID = id;

      protos.push_back(depProto);
    } while (depProto != proto);

    if (Debug.contains(DebugFlags::ProtocolDependencies)) {
      llvm::dbgs() << "Connected component: [";
      bool first = true;
      for (auto *depProto : protos) {
        if (!first) {
          llvm::dbgs() << ", ";
        } else {
          first = false;
        }
        llvm::dbgs() << depProto->getName();
      }
      llvm::dbgs() << "]\n";
    }

    Components[id] = {Context.AllocateCopy(protos), nullptr};
  }
}

/// Lazily construct a requirement machine for the given protocol's strongly
/// connected component (SCC) in the protocol dependency graph.
RequirementMachine *RewriteContext::getRequirementMachine(
    const ProtocolDecl *proto) {
  auto found = Protos.find(proto);
  if (found == Protos.end()) {
    SmallVector<const ProtocolDecl *, 3> stack;
    getRequirementMachineRec(proto, stack);
    assert(stack.empty());

    found = Protos.find(proto);
    assert(found != Protos.end());
  }

  assert(Components.count(found->second.ComponentID) != 0);
  auto &component = Components[found->second.ComponentID];

  auto *&machine = component.Machine;

  if (machine) {
    // If this component has a machine already, make sure it is ready
    // for use.
    if (!machine->isComplete()) {
      llvm::errs() << "Re-entrant construction of requirement "
                   << "machine for:";
      for (auto *proto : component.Protos)
        llvm::errs() << " " << proto->getName();
      abort();
    }

    return machine;
  }

  // Construct a requirement machine from the structural requirements of
  // the given set of protocols.
  auto *newMachine = new RequirementMachine(*this);
  machine = newMachine;

  // This might re-entrantly invalidate 'machine', which is a reference
  // into Protos.
  newMachine->initWithProtocols(component.Protos);
  return newMachine;
}

/// We print stats in the destructor, which should get executed at the end of
/// a compilation job.
RewriteContext::~RewriteContext() {
  if (Context.LangOpts.AnalyzeRequirementMachine) {
    llvm::dbgs() << "--- Requirement Machine Statistics ---\n";
    llvm::dbgs() << "\n* Symbol kind:\n";
    SymbolHistogram.dump(llvm::dbgs(), Symbol::Kinds);
    llvm::dbgs() << "\n* Term length:\n";
    TermHistogram.dump(llvm::dbgs());
    llvm::dbgs() << "\n* Rule trie fanout:\n";
    RuleTrieHistogram.dump(llvm::dbgs());
    llvm::dbgs() << "\n* Rule trie root fanout:\n";
    RuleTrieRootHistogram.dump(llvm::dbgs());
    llvm::dbgs() << "\n* Property trie fanout:\n";
    PropertyTrieHistogram.dump(llvm::dbgs());
    llvm::dbgs() << "\n* Property trie root fanout:\n";
    PropertyTrieRootHistogram.dump(llvm::dbgs());
  }

  for (const auto &pair : Machines)
    delete pair.second;

  Machines.clear();

  for (const auto &pair : Components)
    delete pair.second.Machine;

  Components.clear();
}
