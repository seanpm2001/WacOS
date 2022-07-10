//===--- TypeRef.cpp - Swift Type References for Reflection ---------------===//
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
// Implements the structures of type references for property and enum
// case reflection.
//
//===----------------------------------------------------------------------===//

#include "swift/Basic/Demangle.h"
#include "swift/Reflection/TypeRef.h"
#include "swift/Reflection/TypeRefBuilder.h"

using namespace swift;
using namespace reflection;

[[noreturn]]
static void unreachable(const char *Message) {
  std::cerr << "fatal error: " << Message << "\n";
  std::abort();
}

class PrintTypeRef : public TypeRefVisitor<PrintTypeRef, void> {
  std::ostream &OS;
  unsigned Indent;

  std::ostream &indent(unsigned Amount) {
    for (unsigned i = 0; i < Amount; ++i)
      OS << ' ';
    return OS;
  }

  std::ostream &printHeader(std::string Name) {
    indent(Indent) << '(' << Name;
    return OS;
  }

  template<typename T>
  std::ostream &printField(std::string name, const T &value) {
    if (!name.empty())
      OS << " " << name << "=" << value;
    else
      OS << " " << value;
    return OS;
  }

  void printRec(const TypeRef *typeRef) {
    OS << "\n";

    Indent += 2;
    visit(typeRef);
    Indent -= 2;
  }

public:
  PrintTypeRef(std::ostream &OS, unsigned Indent)
    : OS(OS), Indent(Indent) {}

  void visitBuiltinTypeRef(const BuiltinTypeRef *B) {
    printHeader("builtin");
    auto demangled = Demangle::demangleTypeAsString(B->getMangledName());
    printField("", demangled);
    OS << ')';
  }

  void visitNominalTypeRef(const NominalTypeRef *N) {
    if (N->isStruct())
      printHeader("struct");
    else if (N->isEnum())
      printHeader("enum");
    else if (N->isClass())
      printHeader("class");
    else
      printHeader("nominal");
    auto demangled = Demangle::demangleTypeAsString(N->getMangledName());
    printField("", demangled);
    if (auto parent = N->getParent())
      printRec(parent);
    OS << ')';
  }

  void visitBoundGenericTypeRef(const BoundGenericTypeRef *BG) {
    if (BG->isStruct())
      printHeader("bound_generic_struct");
    else if (BG->isEnum())
      printHeader("bound_generic_enum");
    else if (BG->isClass())
      printHeader("bound_generic_class");
    else
      printHeader("bound_generic");

    auto demangled = Demangle::demangleTypeAsString(BG->getMangledName());
    printField("", demangled);
    for (auto param : BG->getGenericParams())
      printRec(param);
    if (auto parent = BG->getParent())
      printRec(parent);
    OS << ')';
  }

  void visitTupleTypeRef(const TupleTypeRef *T) {
    printHeader("tuple");
    for (auto element : T->getElements())
      printRec(element);
    OS << ')';
  }

  void visitFunctionTypeRef(const FunctionTypeRef *F) {
    printHeader("function");

    switch (F->getFlags().getConvention()) {
    case FunctionMetadataConvention::Swift:
      break;
    case FunctionMetadataConvention::Block:
      printField("convention", "block");
      break;
    case FunctionMetadataConvention::Thin:
      printField("convention", "thin");
      break;
    case FunctionMetadataConvention::CFunctionPointer:
      printField("convention", "c");
      break;
    }

    for (auto Arg : F->getArguments())
      printRec(Arg);
    printRec(F->getResult());

    OS << ')';
  }

  void visitProtocolTypeRef(const ProtocolTypeRef *P) {
    printHeader("protocol");
    auto demangled = Demangle::demangleTypeAsString(P->getMangledName());
    printField("", demangled);
    OS << ')';
  }

  void visitProtocolCompositionTypeRef(const ProtocolCompositionTypeRef *PC) {
    printHeader("protocol_composition");
    for (auto protocol : PC->getProtocols())
      printRec(protocol);
    OS << ')';
  }

  void visitMetatypeTypeRef(const MetatypeTypeRef *M) {
    printHeader("metatype");
    if (M->wasAbstract())
      printField("", "was_abstract");
    printRec(M->getInstanceType());
    OS << ')';
  }

  void visitExistentialMetatypeTypeRef(const ExistentialMetatypeTypeRef *EM) {
    printHeader("existential_metatype");
    printRec(EM->getInstanceType());
    OS << ')';
  }

  void visitGenericTypeParameterTypeRef(const GenericTypeParameterTypeRef *GTP){
    printHeader("generic_type_parameter");
    printField("depth", GTP->getDepth());
    printField("index", GTP->getIndex());
    OS << ')';
  }

  void visitDependentMemberTypeRef(const DependentMemberTypeRef *DM) {
    printHeader("dependent_member");
    printRec(DM->getProtocol());
    printRec(DM->getBase());
    printField("member", DM->getMember());
    OS << ')';
  }

  void visitForeignClassTypeRef(const ForeignClassTypeRef *F) {
    printHeader("foreign");
    if (!F->getName().empty())
      printField("name", F->getName());
    OS << ')';
  }

  void visitObjCClassTypeRef(const ObjCClassTypeRef *OC) {
    printHeader("objective_c_class");
    if (!OC->getName().empty())
      printField("name", OC->getName());
    OS << ')';
  }

  void visitUnownedStorageTypeRef(const UnownedStorageTypeRef *US) {
    printHeader("unowned_storage");
    printRec(US->getType());
    OS << ')';
  }

  void visitWeakStorageTypeRef(const WeakStorageTypeRef *WS) {
    printHeader("weak_storage");
    printRec(WS->getType());
    OS << ')';
  }

  void visitUnmanagedStorageTypeRef(const UnmanagedStorageTypeRef *US) {
    printHeader("unmanaged_storage");
    printRec(US->getType());
    OS << ')';
  }

  void visitSILBoxTypeRef(const SILBoxTypeRef *SB) {
    printHeader("sil_box");
    printRec(SB->getBoxedType());
    OS << ')';
  }

  void visitOpaqueTypeRef(const OpaqueTypeRef *O) {
    printHeader("opaque");
    OS << ')';
  }
};

struct TypeRefIsConcrete
  : public TypeRefVisitor<TypeRefIsConcrete, bool> {
  const GenericArgumentMap &Subs;

  TypeRefIsConcrete(const GenericArgumentMap &Subs) : Subs(Subs) {}

  bool visitBuiltinTypeRef(const BuiltinTypeRef *B) {
    return true;
  }

  bool visitNominalTypeRef(const NominalTypeRef *N) {
    if (N->getParent())
      return visit(N->getParent());
    return true;
  }

  bool visitBoundGenericTypeRef(const BoundGenericTypeRef *BG) {
    if (BG->getParent())
      if (!visit(BG->getParent()))
        return false;
    for (auto Param : BG->getGenericParams())
      if (!visit(Param))
        return false;
    return true;
  }

  bool visitTupleTypeRef(const TupleTypeRef *T) {
    for (auto Element : T->getElements()) {
      if (!visit(Element))
        return false;
    }
    return true;
  }

  bool visitFunctionTypeRef(const FunctionTypeRef *F) {
    std::vector<TypeRef *> SubstitutedArguments;
    for (auto Argument : F->getArguments())
      if (!visit(Argument))
        return false;
    return visit(F->getResult());
  }

  bool visitProtocolTypeRef(const ProtocolTypeRef *P) {
    return true;
  }

  bool
  visitProtocolCompositionTypeRef(const ProtocolCompositionTypeRef *PC) {
    for (auto Protocol : PC->getProtocols())
      if (!visit(Protocol))
        return false;
    return true;
  }

  bool visitMetatypeTypeRef(const MetatypeTypeRef *M) {
    return visit(M->getInstanceType());
  }

  bool
  visitExistentialMetatypeTypeRef(const ExistentialMetatypeTypeRef *EM) {
    return visit(EM->getInstanceType());
  }

  bool
  visitGenericTypeParameterTypeRef(const GenericTypeParameterTypeRef *GTP) {
    return Subs.find({GTP->getDepth(), GTP->getIndex()}) != Subs.end();
  }

  bool
  visitDependentMemberTypeRef(const DependentMemberTypeRef *DM) {
    return visit(DM->getBase());
  }

  bool visitForeignClassTypeRef(const ForeignClassTypeRef *F) {
    return true;
  }

  bool visitObjCClassTypeRef(const ObjCClassTypeRef *OC) {
    return true;
  }
  
  bool visitOpaqueTypeRef(const OpaqueTypeRef *O) {
    return true;
  }

  bool visitUnownedStorageTypeRef(const UnownedStorageTypeRef *US) {
    return visit(US->getType());
  }

  bool visitWeakStorageTypeRef(const WeakStorageTypeRef *WS) {
    return visit(WS->getType());
  }

  bool visitUnmanagedStorageTypeRef(const UnmanagedStorageTypeRef *US) {
    return visit(US->getType());
  }

  bool visitSILBoxTypeRef(const SILBoxTypeRef *SB) {
    return visit(SB->getBoxedType());
  }
};

const OpaqueTypeRef *
OpaqueTypeRef::Singleton = new OpaqueTypeRef();

const OpaqueTypeRef *OpaqueTypeRef::get() {
  return Singleton;
}

void TypeRef::dump() const {
  dump(std::cerr);
}

void TypeRef::dump(std::ostream &OS, unsigned Indent) const {
  PrintTypeRef(OS, Indent).visit(this);
  OS << std::endl;
}

bool TypeRef::isConcrete() const {
  GenericArgumentMap Subs;
  return TypeRefIsConcrete(Subs).visit(this);
}

bool TypeRef::isConcreteAfterSubstitutions(
    const GenericArgumentMap &Subs) const {
  return TypeRefIsConcrete(Subs).visit(this);
}

unsigned NominalTypeTrait::getDepth() const {
  if (auto P = Parent) {
    switch (P->getKind()) {
    case TypeRefKind::Nominal:
      return 1 + cast<NominalTypeRef>(P)->getDepth();
    case TypeRefKind::BoundGeneric:
      return 1 + cast<BoundGenericTypeRef>(P)->getDepth();
    default:
      unreachable("Asked for depth on non-nominal typeref");
    }
  }

  return 0;
}

GenericArgumentMap TypeRef::getSubstMap() const {
  GenericArgumentMap Substitutions;
  switch (getKind()) {
    case TypeRefKind::Nominal: {
      auto Nom = cast<NominalTypeRef>(this);
      if (auto Parent = Nom->getParent())
        return Parent->getSubstMap();
      return GenericArgumentMap();
    }
    case TypeRefKind::BoundGeneric: {
      auto BG = cast<BoundGenericTypeRef>(this);
      auto Depth = BG->getDepth();
      unsigned Index = 0;
      for (auto Param : BG->getGenericParams())
        Substitutions.insert({{Depth, Index++}, Param});
      if (auto Parent = BG->getParent()) {
        auto ParentSubs = Parent->getSubstMap();
        Substitutions.insert(ParentSubs.begin(), ParentSubs.end());
      }
      break;
    }
    default:
      break;
  }
  return Substitutions;
}

namespace {
bool isStruct(Demangle::NodePointer Node) {
  switch (Node->getKind()) {
    case Demangle::Node::Kind::Type:
      return isStruct(Node->getChild(0));
    case Demangle::Node::Kind::Structure:
    case Demangle::Node::Kind::BoundGenericStructure:
      return true;
    default:
      return false;
  }
}
bool isEnum(Demangle::NodePointer Node) {
  switch (Node->getKind()) {
    case Demangle::Node::Kind::Type:
      return isEnum(Node->getChild(0));
    case Demangle::Node::Kind::Enum:
    case Demangle::Node::Kind::BoundGenericEnum:
      return true;
    default:
      return false;
  }
}
bool isClass(Demangle::NodePointer Node) {
  switch (Node->getKind()) {
    case Demangle::Node::Kind::Type:
      return isClass(Node->getChild(0));
    case Demangle::Node::Kind::Class:
    case Demangle::Node::Kind::BoundGenericClass:
      return true;
    default:
      return false;
  }
}
}

bool NominalTypeTrait::isStruct() const {
  auto Demangled = Demangle::demangleTypeAsNode(MangledName);
  return ::isStruct(Demangled);
}


bool NominalTypeTrait::isEnum() const {
  auto Demangled = Demangle::demangleTypeAsNode(MangledName);
  return ::isEnum(Demangled);
}


bool NominalTypeTrait::isClass() const {
  auto Demangled = Demangle::demangleTypeAsNode(MangledName);
  return ::isClass(Demangled);
}

/// Visitor class to set the WasAbstract flag of any MetatypeTypeRefs
/// contained in the given type.
class ThickenMetatype
  : public TypeRefVisitor<ThickenMetatype, const TypeRef *> {
  TypeRefBuilder &Builder;
public:
  using TypeRefVisitor<ThickenMetatype, const TypeRef *>::visit;

  ThickenMetatype(TypeRefBuilder &Builder) : Builder(Builder) {}

  const TypeRef *visitBuiltinTypeRef(const BuiltinTypeRef *B) {
    return B;
  }

  const TypeRef *visitNominalTypeRef(const NominalTypeRef *N) {
    return N;
  }

  const TypeRef *visitBoundGenericTypeRef(const BoundGenericTypeRef *BG) {
    std::vector<const TypeRef *> GenericParams;
    for (auto Param : BG->getGenericParams())
      GenericParams.push_back(visit(Param));
    return BoundGenericTypeRef::create(Builder, BG->getMangledName(),
                                       GenericParams);
  }

  const TypeRef *visitTupleTypeRef(const TupleTypeRef *T) {
    std::vector<const TypeRef *> Elements;
    for (auto Element : T->getElements())
      Elements.push_back(visit(Element));
    return TupleTypeRef::create(Builder, Elements);
  }

  const TypeRef *visitFunctionTypeRef(const FunctionTypeRef *F) {
    std::vector<const TypeRef *> SubstitutedArguments;
    for (auto Argument : F->getArguments())
      SubstitutedArguments.push_back(visit(Argument));

    auto SubstitutedResult = visit(F->getResult());

    return FunctionTypeRef::create(Builder, SubstitutedArguments,
                                   SubstitutedResult, F->getFlags());
  }

  const TypeRef *visitProtocolTypeRef(const ProtocolTypeRef *P) {
    return P;
  }

  const TypeRef *
  visitProtocolCompositionTypeRef(const ProtocolCompositionTypeRef *PC) {
    return PC;
  }

  const TypeRef *visitMetatypeTypeRef(const MetatypeTypeRef *M) {
    return MetatypeTypeRef::create(Builder, visit(M->getInstanceType()),
                                   /*WasAbstract=*/true);
  }

  const TypeRef *
  visitExistentialMetatypeTypeRef(const ExistentialMetatypeTypeRef *EM) {
    return EM;
  }

  const TypeRef *
  visitGenericTypeParameterTypeRef(const GenericTypeParameterTypeRef *GTP) {
    return GTP;
  }

  const TypeRef *visitDependentMemberTypeRef(const DependentMemberTypeRef *DM) {
    return DM;
  }

  const TypeRef *visitForeignClassTypeRef(const ForeignClassTypeRef *F) {
    return F;
  }

  const TypeRef *visitObjCClassTypeRef(const ObjCClassTypeRef *OC) {
    return OC;
  }

  const TypeRef *visitUnownedStorageTypeRef(const UnownedStorageTypeRef *US) {
    return US;
  }

  const TypeRef *visitWeakStorageTypeRef(const WeakStorageTypeRef *WS) {
    return WS;
  }

  const TypeRef *
  visitUnmanagedStorageTypeRef(const UnmanagedStorageTypeRef *US) {
    return US;
  }

  const TypeRef *visitSILBoxTypeRef(const SILBoxTypeRef *SB) {
    return SILBoxTypeRef::create(Builder, visit(SB->getBoxedType()));
  }

  const TypeRef *visitOpaqueTypeRef(const OpaqueTypeRef *O) {
    return O;
  }
};

static const TypeRef *
thickenMetatypes(TypeRefBuilder &Builder, const TypeRef *TR) {
  return ThickenMetatype(Builder).visit(TR);
}

class TypeRefSubstitution
  : public TypeRefVisitor<TypeRefSubstitution, const TypeRef *> {
  TypeRefBuilder &Builder;
  GenericArgumentMap Substitutions;
public:
  using TypeRefVisitor<TypeRefSubstitution, const TypeRef *>::visit;

  TypeRefSubstitution(TypeRefBuilder &Builder, GenericArgumentMap Substitutions)
    : Builder(Builder), Substitutions(Substitutions) {}

  const TypeRef *visitBuiltinTypeRef(const BuiltinTypeRef *B) {
    return B;
  }

  const TypeRef *visitNominalTypeRef(const NominalTypeRef *N) {
    if (N->getParent())
      return NominalTypeRef::create(Builder, N->getMangledName(),
                                    visit(N->getParent()));
    return N;
  }

  const TypeRef *visitBoundGenericTypeRef(const BoundGenericTypeRef *BG) {
    auto *Parent = BG->getParent();
    if (Parent != nullptr)
      Parent = visit(Parent);
    std::vector<const TypeRef *> GenericParams;
    for (auto Param : BG->getGenericParams())
      GenericParams.push_back(visit(Param));
    return BoundGenericTypeRef::create(Builder, BG->getMangledName(),
                                       GenericParams, Parent);
  }

  const TypeRef *visitTupleTypeRef(const TupleTypeRef *T) {
    std::vector<const TypeRef *> Elements;
    for (auto Element : T->getElements())
      Elements.push_back(visit(Element));
    return TupleTypeRef::create(Builder, Elements);
  }

  const TypeRef *visitFunctionTypeRef(const FunctionTypeRef *F) {
    std::vector<const TypeRef *> SubstitutedArguments;
    for (auto Argument : F->getArguments())
      SubstitutedArguments.push_back(visit(Argument));

    auto SubstitutedResult = visit(F->getResult());

    return FunctionTypeRef::create(Builder, SubstitutedArguments,
                                   SubstitutedResult, F->getFlags());
  }

  const TypeRef *visitProtocolTypeRef(const ProtocolTypeRef *P) {
    // Protocol compositions do not contain type parameters.
    assert(P->isConcrete());
    return P;
  }

  const TypeRef *
  visitProtocolCompositionTypeRef(const ProtocolCompositionTypeRef *PC) {
    return PC;
  }

  const TypeRef *visitMetatypeTypeRef(const MetatypeTypeRef *M) {
    // If the metatype's instance type does not contain any type parameters,
    // substitution does not alter anything, and the empty representation
    // can still be used.
    if (M->isConcrete())
      return M;

    // When substituting a concrete type into a type parameter inside
    // of a metatype's instance type, (eg; T.Type, T := C), we must
    // represent the metatype at runtime as a value, even if the
    // metatype naturally has an empty representation.
    return MetatypeTypeRef::create(Builder, visit(M->getInstanceType()),
                                   /*WasAbstract=*/true);
  }

  const TypeRef *
  visitExistentialMetatypeTypeRef(const ExistentialMetatypeTypeRef *EM) {
    // Existential metatypes do not contain type parameters.
    assert(EM->getInstanceType()->isConcrete());
    return EM;
  }

  const TypeRef *
  visitGenericTypeParameterTypeRef(const GenericTypeParameterTypeRef *GTP) {
    auto found = Substitutions.find({GTP->getDepth(), GTP->getIndex()});
    assert(found != Substitutions.end());
    assert(found->second->isConcrete());

    // When substituting a concrete type containing a metatype into a
    // type parameter, (eg: T, T := C.Type), we must also represent
    // the metatype as a value.
    return thickenMetatypes(Builder, found->second);
  }

  const TypeRef *visitDependentMemberTypeRef(const DependentMemberTypeRef *DM) {
    // Substitute type parameters in the base type to get a fully concrete
    // type.
    auto SubstBase = visit(DM->getBase());

    const TypeRef *TypeWitness = nullptr;

    // Get the original type of the witness from the conformance.
    switch (SubstBase->getKind()) {
    case TypeRefKind::Nominal: {
      auto Nominal = cast<NominalTypeRef>(SubstBase);
      TypeWitness = Builder.getDependentMemberTypeRef(Nominal->getMangledName(), DM);
      break;
    }
    case TypeRefKind::BoundGeneric: {
      auto BG = cast<BoundGenericTypeRef>(SubstBase);
      TypeWitness = Builder.getDependentMemberTypeRef(BG->getMangledName(), DM);
      break;
    }
    default:
      unreachable("Unknown base type");
    }

    assert(TypeWitness);

    // Apply base type substitutions to get the fully-substituted nested type.
    auto *Subst = TypeWitness->subst(Builder, SubstBase->getSubstMap());

    // Same as above.
    return thickenMetatypes(Builder, Subst);
  }

  const TypeRef *visitForeignClassTypeRef(const ForeignClassTypeRef *F) {
    return F;
  }

  const TypeRef *visitObjCClassTypeRef(const ObjCClassTypeRef *OC) {
    return OC;
  }

  const TypeRef *visitUnownedStorageTypeRef(const UnownedStorageTypeRef *US) {
    return UnownedStorageTypeRef::create(Builder, visit(US->getType()));
  }

  const TypeRef *visitWeakStorageTypeRef(const WeakStorageTypeRef *WS) {
    return WeakStorageTypeRef::create(Builder, visit(WS->getType()));
  }

  const TypeRef *
  visitUnmanagedStorageTypeRef(const UnmanagedStorageTypeRef *US) {
    return UnmanagedStorageTypeRef::create(Builder, visit(US->getType()));
  }

  const TypeRef *visitSILBoxTypeRef(const SILBoxTypeRef *SB) {
    return SILBoxTypeRef::create(Builder, visit(SB->getBoxedType()));
  }

  const TypeRef *visitOpaqueTypeRef(const OpaqueTypeRef *O) {
    return O;
  }
};

const TypeRef *
TypeRef::subst(TypeRefBuilder &Builder, const GenericArgumentMap &Subs) const {
  const TypeRef *Result = TypeRefSubstitution(Builder, Subs).visit(this);
  assert(Result->isConcrete());
  return Result;
}

bool TypeRef::deriveSubstitutions(GenericArgumentMap &Subs,
                                  const TypeRef *OrigTR,
                                  const TypeRef *SubstTR) {

  // Walk into parent types of concrete nominal types.
  if (auto *O = dyn_cast<NominalTypeRef>(OrigTR)) {
    if (auto *S = dyn_cast<NominalTypeRef>(SubstTR)) {
      if (!!O->getParent() != !!S->getParent() ||
          O->getMangledName() != S->getMangledName())
        return false;

      if (O->getParent() &&
          !deriveSubstitutions(Subs,
                               O->getParent(),
                               S->getParent()))
        return false;

      return true;
    }
  }

  // Decompose arguments of bound generic types in parallel.
  if (auto *O = dyn_cast<BoundGenericTypeRef>(OrigTR)) {
    if (auto *S = dyn_cast<BoundGenericTypeRef>(SubstTR)) {
      if (!!O->getParent() != !!S->getParent() ||
          O->getMangledName() != S->getMangledName() ||
          O->getGenericParams().size() != S->getGenericParams().size())
        return false;

      if (O->getParent() &&
          !deriveSubstitutions(Subs,
                               O->getParent(),
                               S->getParent()))
        return false;

      for (unsigned i = 0, e = O->getGenericParams().size(); i < e; i++) {
        if (!deriveSubstitutions(Subs,
                                 O->getGenericParams()[i],
                                 S->getGenericParams()[i]))
          return false;
      }

      return true;
    }
  }

  // Decompose tuple element types in parallel.
  if (auto *O = dyn_cast<TupleTypeRef>(OrigTR)) {
    if (auto *S = dyn_cast<TupleTypeRef>(SubstTR)) {
      if (O->getElements().size() != S->getElements().size())
        return false;

      for (unsigned i = 0, e = O->getElements().size(); i < e; i++) {
        if (!deriveSubstitutions(Subs,
                                 O->getElements()[i],
                                 S->getElements()[i]))
          return false;
      }

      return true;
    }
  }

  // Decompose argument and result types in parallel.
  if (auto *O = dyn_cast<FunctionTypeRef>(OrigTR)) {
    if (auto *S = dyn_cast<FunctionTypeRef>(SubstTR)) {

      if (O->getArguments().size() != S->getArguments().size())
        return false;

      for (unsigned i = 0, e = O->getArguments().size(); i < e; i++) {
        if (!deriveSubstitutions(Subs,
                                 O->getArguments()[i],
                                 S->getArguments()[i]))
          return false;
      }

      if (!deriveSubstitutions(Subs,
                               O->getResult(),
                               S->getResult()))
        return false;

      return true;
    }
  }

  // Walk down into the instance type.
  if (auto *O = dyn_cast<MetatypeTypeRef>(OrigTR)) {
    if (auto *S = dyn_cast<MetatypeTypeRef>(SubstTR)) {

      if (!deriveSubstitutions(Subs,
                               O->getInstanceType(),
                               S->getInstanceType()))
        return false;

      return true;
    }
  }

  // Walk down into the referent storage type.
  if (auto *O = dyn_cast<ReferenceStorageTypeRef>(OrigTR)) {
    if (auto *S = dyn_cast<ReferenceStorageTypeRef>(SubstTR)) {

      if (O->getKind() != S->getKind())
        return false;

      if (!deriveSubstitutions(Subs,
                               O->getType(),
                               S->getType()))
        return false;

      return true;
    }
  }

  if (isa<DependentMemberTypeRef>(OrigTR)) {
    // FIXME: Do some validation here?
    return true;
  }

  // If the original type is a generic type parameter, just make
  // sure the substituted type matches anything we've already
  // seen.
  if (auto *O = dyn_cast<GenericTypeParameterTypeRef>(OrigTR)) {
    DepthAndIndex key = {O->getDepth(), O->getIndex()};
    auto found = Subs.find(key);
    if (found == Subs.end()) {
      Subs[key] = SubstTR;
      return true;
    }

    return (found->second == SubstTR);
  }

  // Anything else must be concrete and the two types must match
  // exactly.
  return (OrigTR == SubstTR);
}
