//===--- TypeRef.cpp - TypeRef tests --------------------------------------===//
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

#include "swift/Reflection/TypeRefBuilder.h"
#include "gtest/gtest.h"

using namespace swift;
using namespace reflection;

static const std::string ABC = "ABC";
static const std::string ABCD = "ABCD";
static const std::string XYZ = "XYZ";
static const std::string Empty = "";
static const std::string MyClass = "MyClass";
static const std::string NotMyClass = "NotMyClass";
static const std::string Module = "Module";
static const std::string Shmodule = "Shmodule";
static const std::string Protocol = "Protocol";
static const std::string Shmrotocol = "Shmrotocol";

TEST(TypeRefTest, UniqueBuiltinTypeRef) {
  TypeRefBuilder Builder;

  auto BI1 = Builder.createBuiltinType(ABC);
  auto BI2 = Builder.createBuiltinType(ABC);
  auto BI3 = Builder.createBuiltinType(ABCD);

  EXPECT_EQ(BI1, BI2);
  EXPECT_NE(BI2, BI3);
}

TEST(TypeRefTest, UniqueNominalTypeRef) {
  TypeRefBuilder Builder;

  auto N1 = Builder.createNominalType(ABC, nullptr);
  auto N2 = Builder.createNominalType(ABC, nullptr);
  auto N3 = Builder.createNominalType(ABCD, nullptr);

  EXPECT_EQ(N1, N2);
  EXPECT_NE(N2, N3);

  auto N4 = Builder.createNominalType(ABC, N1);
  auto N5 = Builder.createNominalType(ABC, N1);

  EXPECT_EQ(N4, N5);
}

TEST(TypeRefTest, UniqueBoundGenericTypeRef) {
  TypeRefBuilder Builder;

  auto GTP00 = Builder.createGenericTypeParameterType(0, 0);
  auto GTP01 = Builder.createGenericTypeParameterType(0, 1);

  auto BG1 = Builder.createBoundGenericType(ABC, {}, nullptr);
  auto BG2 = Builder.createBoundGenericType(ABC, {}, nullptr);
  auto BG3 = Builder.createBoundGenericType(ABCD, {}, nullptr);

  EXPECT_EQ(BG1, BG2);
  EXPECT_NE(BG2, BG3);

  std::vector<const TypeRef *> GenericParams { GTP00, GTP01 };

  auto BG4 = Builder.createBoundGenericType(ABC, GenericParams, nullptr);
  auto BG5 = Builder.createBoundGenericType(ABC, GenericParams, nullptr);
  auto BG6 = Builder.createBoundGenericType(ABCD, GenericParams, nullptr);

  EXPECT_EQ(BG4, BG5);
  EXPECT_NE(BG5, BG6);
  EXPECT_NE(BG5, BG1);
}

TEST(TypeRefTest, UniqueTupleTypeRef) {
  TypeRefBuilder Builder;

  auto N1 = Builder.createNominalType(ABC, nullptr);
  auto N2 = Builder.createNominalType(XYZ, nullptr);

  std::vector<const TypeRef *> Void;
  auto Void1 = Builder.createTupleType(Void, "", false);
  auto Void2 = Builder.createTupleType(Void, "", false);

  EXPECT_EQ(Void1, Void2);

  std::vector<const TypeRef *> Elements1 { N1, N2 };
  std::vector<const TypeRef *> Elements2 { N1, N2, N2 };

  auto T1 = Builder.createTupleType(Elements1, "", false);
  auto T2 = Builder.createTupleType(Elements1, "", false);
  auto T3 = Builder.createTupleType(Elements2, "", false);

  EXPECT_EQ(T1, T2);
  EXPECT_NE(T2, T3);
  EXPECT_NE(T1, Void1);

  auto T4 = Builder.createTupleType(Elements1, "", true);
  auto T5 = Builder.createTupleType(Elements1, "", true);
  auto T6 = Builder.createTupleType(Elements1, "", false);

  EXPECT_EQ(T4, T5);
  EXPECT_NE(T5, T6);
}

TEST(TypeRefTest, UniqueFunctionTypeRef) {

  TypeRefBuilder Builder;

  std::vector<const TypeRef *> Void;
  auto VoidResult = Builder.createTupleType(Void, "", false);
  std::vector<bool> VoidInout;
  auto Arg1 = Builder.createNominalType(ABC, nullptr);
  auto Arg2 = Builder.createNominalType(XYZ, nullptr);

  std::vector<const TypeRef *> Arguments1 { Arg1, Arg2 };
  std::vector<bool> Inout1 { false, false };
  auto Result = Builder.createTupleType({Arg1, Arg2}, "", false);

  std::vector<const TypeRef *> Arguments2 { Arg1, Arg1 };
  std::vector<bool> Inout2 { false, false };

  auto F1 = Builder.createFunctionType(Arguments1, Inout1, Result,
                                       FunctionTypeFlags());
  auto F2 = Builder.createFunctionType(Arguments1, Inout1, Result,
                                       FunctionTypeFlags());
  auto F3 = Builder.createFunctionType(Arguments2, Inout1, Result,
                                       FunctionTypeFlags());

  EXPECT_EQ(F1, F2);
  EXPECT_NE(F2, F3);

  auto F4 = Builder.createFunctionType(Arguments1, Inout1, Result,
                                       FunctionTypeFlags().withThrows(true));
  auto F5 = Builder.createFunctionType(Arguments1, Inout1, Result,
                                       FunctionTypeFlags().withThrows(true));

  EXPECT_EQ(F4, F5);
  EXPECT_NE(F4, F1);

  auto VoidVoid1 = Builder.createFunctionType(Void, VoidInout, VoidResult,
                                              FunctionTypeFlags());
  auto VoidVoid2 = Builder.createFunctionType(Void, VoidInout, VoidResult,
                                              FunctionTypeFlags());

  EXPECT_EQ(VoidVoid1, VoidVoid2);
  EXPECT_NE(VoidVoid1, F1);
}

TEST(TypeRefTest, UniqueProtocolTypeRef) {
  TypeRefBuilder Builder;

  auto P1 = Builder.createProtocolType(ABC, Module, Protocol);
  auto P2 = Builder.createProtocolType(ABC, Module, Protocol);
  auto P3 = Builder.createProtocolType(ABCD, Module, Shmrotocol);
  auto P4 = Builder.createProtocolType(XYZ, Shmodule, Protocol);

  EXPECT_EQ(P1, P2);
  EXPECT_NE(P2, P3);
  EXPECT_NE(P2, P3);
  EXPECT_NE(P3, P4);

  auto PC1 = Builder.createProtocolCompositionType({P1, P2});
  auto PC2 = Builder.createProtocolCompositionType({P1, P2});
  auto PC3 = Builder.createProtocolCompositionType({P1, P2, P2});
  auto Any = Builder.createProtocolCompositionType({});

  EXPECT_EQ(PC1, PC2);
  EXPECT_NE(PC2, PC3);
  EXPECT_NE(PC1, Any);
}

TEST(TypeRefTest, UniqueMetatypeTypeRef) {
  TypeRefBuilder Builder;

  auto N1 = Builder.createNominalType(ABC, nullptr);
  auto M1 = Builder.createMetatypeType(N1, false);
  auto M2 = Builder.createMetatypeType(N1, false);
  auto MM3 = Builder.createMetatypeType(M1, false);
  auto M4 = Builder.createMetatypeType(N1, true);

  EXPECT_EQ(M1, M2);
  EXPECT_NE(M2, MM3);
  EXPECT_NE(M1, M4);
}

TEST(TypeRefTest, UniqueExistentialMetatypeTypeRef) {
  TypeRefBuilder Builder;

  auto N1 = Builder.createNominalType(ABC, nullptr);
  auto M1 = Builder.createExistentialMetatypeType(N1);
  auto M2 = Builder.createExistentialMetatypeType(N1);
  auto MM3 = Builder.createExistentialMetatypeType(M1);

  EXPECT_EQ(M1, M2);
  EXPECT_NE(M2, MM3);
}

TEST(TypeRefTest, UniqueGenericTypeParameterTypeRef) {
  TypeRefBuilder Builder;

  auto GTP00 = Builder.createGenericTypeParameterType(0, 0);
  auto GTP00_2 = Builder.createGenericTypeParameterType(0, 0);
  auto GTP01 = Builder.createGenericTypeParameterType(0, 1);
  auto GTP10 = Builder.createGenericTypeParameterType(1, 0);

  EXPECT_EQ(GTP00, GTP00_2);
  EXPECT_NE(GTP00, GTP01);
  EXPECT_NE(GTP01, GTP10);
}

TEST(TypeRefTest, UniqueDependentMemberTypeRef) {
  TypeRefBuilder Builder;

  auto N1 = Builder.createNominalType(ABC, nullptr);
  auto N2 = Builder.createNominalType(XYZ, nullptr);
  auto P1 = Builder.createProtocolType(ABC, Module, Protocol);
  auto P2 = Builder.createProtocolType(ABCD, Shmodule, Protocol);

  auto DM1 = Builder.createDependentMemberType("Index", N1, P1);
  auto DM2 = Builder.createDependentMemberType("Index", N1, P1);
  auto DM3 = Builder.createDependentMemberType("Element", N1, P1);
  auto DM4 = Builder.createDependentMemberType("Index", N2, P1);
  auto DM5 = Builder.createDependentMemberType("Index", N2, P2);

  EXPECT_EQ(DM1, DM2);
  EXPECT_NE(DM2, DM3);
  EXPECT_NE(DM2, DM4);
  EXPECT_NE(DM4, DM5);
}

TEST(TypeRefTest, UniqueForeignClassTypeRef) {
  TypeRefBuilder Builder;

  auto UN1 = Builder.getUnnamedForeignClassType();
  auto UN2 = Builder.getUnnamedForeignClassType();
  auto FC1 = Builder.createForeignClassType(ABC);
  auto FC2 = Builder.createForeignClassType(ABC);
  auto FC3 = Builder.createForeignClassType(ABCD);

  EXPECT_EQ(UN1, UN2);
  EXPECT_EQ(FC1, FC2);
  EXPECT_NE(FC2, FC3);
}

TEST(TypeRefTest, UniqueObjCClassTypeRef) {
  TypeRefBuilder Builder;

  auto UN1 = Builder.getUnnamedObjCClassType();
  auto UN2 = Builder.getUnnamedObjCClassType();
  auto FC1 = Builder.createObjCClassType(ABC);
  auto FC2 = Builder.createObjCClassType(ABC);
  auto FC3 = Builder.createObjCClassType(ABCD);

  EXPECT_EQ(UN1, UN2);
  EXPECT_EQ(FC1, FC2);
  EXPECT_NE(FC2, FC3);
}

TEST(TypeRefTest, UniqueOpaqueTypeRef) {
  TypeRefBuilder Builder;

  auto Op = OpaqueTypeRef::get();
  auto Op1 = Builder.getOpaqueType();
  auto Op2 = Builder.getOpaqueType();

  EXPECT_EQ(Op, Op1);
  EXPECT_EQ(Op1, Op2);
}

TEST(TypeRefTest, UniqueUnownedStorageType) {
  TypeRefBuilder Builder;

  auto N1 = Builder.createNominalType(MyClass, nullptr);
  auto N2 = Builder.createNominalType(NotMyClass, nullptr);
  auto RS1 = Builder.createUnownedStorageType(N1);
  auto RS2 = Builder.createUnownedStorageType(N1);
  auto RS3 = Builder.createUnownedStorageType(N2);

  EXPECT_EQ(RS1, RS2);
  EXPECT_NE(RS2, RS3);
}

TEST(TypeRefTest, UniqueWeakStorageType) {
  TypeRefBuilder Builder;

  auto N1 = Builder.createNominalType(MyClass, nullptr);
  auto N2 = Builder.createNominalType(NotMyClass, nullptr);
  auto RS1 = Builder.createWeakStorageType(N1);
  auto RS2 = Builder.createWeakStorageType(N1);
  auto RS3 = Builder.createWeakStorageType(N2);

  EXPECT_EQ(RS1, RS2);
  EXPECT_NE(RS2, RS3);
}

TEST(TypeRefTest, UniqueUnmanagedStorageType) {
  TypeRefBuilder Builder;

  auto N1 = Builder.createNominalType(MyClass, nullptr);
  auto N2 = Builder.createNominalType(NotMyClass, nullptr);
  auto RS1 = Builder.createUnmanagedStorageType(N1);
  auto RS2 = Builder.createUnmanagedStorageType(N1);
  auto RS3 = Builder.createUnmanagedStorageType(N2);

  EXPECT_EQ(RS1, RS2);
  EXPECT_NE(RS2, RS3);
}

// Tests that a concrete ABC<Int, Int> is the exact same typeref
// (with the same pointer) as an ABC<T, U> that got substituted
// with T : Int and U : Int.
TEST(TypeRefTest, UniqueAfterSubstitution) {
  TypeRefBuilder Builder;

  std::string MangledIntName("Si");
  auto NominalInt = Builder.createNominalType(MangledIntName,
                                              /*parent*/ nullptr);
  std::vector<const TypeRef *> ConcreteArgs { NominalInt, NominalInt };

  std::string MangledName("ABC");

  auto ConcreteBG = Builder.createBoundGenericType(MangledName,
                                                   ConcreteArgs,
                                                   /*parent*/ nullptr);

  auto GTP00 = Builder.createGenericTypeParameterType(0, 0);
  auto GTP01 = Builder.createGenericTypeParameterType(0, 1);
  std::vector<const TypeRef *> GenericParams { GTP00, GTP01 };

  auto Unbound = Builder.createBoundGenericType(MangledName, GenericParams,
                                                /*parent*/ nullptr);

  GenericArgumentMap Subs;
  Subs[{0,0}] = NominalInt;
  Subs[{0,1}] = NominalInt;

  auto SubstitutedBG = Unbound->subst(Builder, Subs);

  EXPECT_EQ(ConcreteBG, SubstitutedBG);
}

// Make sure subst() and isConcrete() walk into parent types
TEST(TypeRefTest, NestedTypes) {
  TypeRefBuilder Builder;

  auto GTP00 = Builder.createGenericTypeParameterType(0, 0);

  std::string ParentName("parent");
  std::vector<const TypeRef *> ParentArgs { GTP00 };
  auto Parent = Builder.createBoundGenericType(ParentName, ParentArgs,
                                               /*parent*/ nullptr);

  std::string ChildName("child");
  auto Child = Builder.createNominalType(ChildName, Parent);

  EXPECT_FALSE(Child->isConcrete());

  std::string SubstName("subst");
  auto SubstArg = Builder.createNominalType(SubstName, /*parent*/ nullptr);

  std::vector<const TypeRef *> SubstParentArgs { SubstArg };
  auto SubstParent = Builder.createBoundGenericType(ParentName,
                                                    SubstParentArgs,
                                                    /*parent*/ nullptr);
  auto SubstChild = Builder.createNominalType(ChildName, SubstParent);

  GenericArgumentMap Subs;
  Subs[{0,0}] = SubstArg;

  EXPECT_TRUE(Child->isConcreteAfterSubstitutions(Subs));
  EXPECT_EQ(SubstChild, Child->subst(Builder, Subs));
}

TEST(TypeRefTest, DeriveSubstitutions) {
  TypeRefBuilder Builder;

  auto GTP00 = Builder.createGenericTypeParameterType(0, 0);
  auto GTP01 = Builder.createGenericTypeParameterType(0, 1);

  std::string NominalName("nominal");
  std::vector<const TypeRef *> NominalArgs { GTP00 };
  auto Nominal = Builder.createBoundGenericType(NominalName, NominalArgs,
                                               /*parent*/ nullptr);

  auto Result = Builder.createTupleType({GTP00, GTP01}, "", false);
  auto Func = Builder.createFunctionType({Nominal}, {}, Result,
                                         FunctionTypeFlags());

  std::string SubstOneName("subst1");
  auto SubstOne = Builder.createNominalType(SubstOneName, /*parent*/ nullptr);

  std::string SubstTwoName("subst2");
  auto SubstTwo = Builder.createNominalType(SubstTwoName, /*parent*/ nullptr);

  GenericArgumentMap Subs;
  Subs[{0,0}] = SubstOne;
  Subs[{0,1}] = SubstTwo;

  auto Subst = Func->subst(Builder, Subs);

  GenericArgumentMap DerivedSubs;
  EXPECT_TRUE(TypeRef::deriveSubstitutions(DerivedSubs, Func, Subst));

  auto ResultOne = DerivedSubs[{0,0}];
  auto ResultTwo = DerivedSubs[{0,1}];
  EXPECT_EQ(SubstOne, ResultOne);
  EXPECT_EQ(SubstTwo, ResultTwo);
}
