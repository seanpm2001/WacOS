public func takes_p1<T: P1>(_: T.Type) {}
public protocol P1 {
  func normal()
  func generic<T: P3>(_: T)
}
public protocol P2 {}
public protocol P3 {}

public struct IsP2: P2 {}
public struct IsP3: P3 {}


public struct Single<A> {}
extension Single: P1 where A: P2 {
  public func normal() {}
  public func generic<T: P3>(_: T) {}
}

// witness method for Single.normal

// CHECK-LABEL: define linkonce_odr hidden swiftcc void @"$s42conditional_conformance_basic_conformances6SingleVyxGAA2P1A2A2P2RzlAaEP6normalyyFTW"(%T42conditional_conformance_basic_conformances6SingleV* noalias nocapture swiftself %0, %swift.type* %Self, i8** %SelfWitnessTable)
// CHECK-NEXT:  entry:
// CHECK-NEXT:    [[A_P2_PTR:%.*]] = getelementptr inbounds i8*, i8** %SelfWitnessTable, i32 -1
// CHECK-NEXT:    [[A_P2_i8star:%.*]] = load i8*, i8** [[A_P2_PTR]], align 8
// CHECK-NEXT:    [[A_P2:%.*]] = bitcast i8* [[A_P2_i8star]] to i8**
// CHECK-NEXT:    [[SELF_AS_TYPE_ARRAY:%.*]] = bitcast %swift.type* %Self to %swift.type**
// CHECK-NEXT:    [[A_PTR:%.*]] = getelementptr inbounds %swift.type*, %swift.type** [[SELF_AS_TYPE_ARRAY]], i64 2
// CHECK-NEXT:    [[A:%.*]] = load %swift.type*, %swift.type** [[A_PTR]], align 8
// CHECK-NEXT:    call swiftcc void @"$s42conditional_conformance_basic_conformances6SingleVA2A2P2RzlE6normalyyF"(%swift.type* [[A]], i8** [[A_P2]])
// CHECK-NEXT:    ret void
// CHECK-NEXT:  }

// witness method for Single.generic

// CHECK-LABEL: define linkonce_odr hidden swiftcc void @"$s42conditional_conformance_basic_conformances6SingleVyxGAA2P1A2A2P2RzlAaEP7genericyyqd__AA2P3Rd__lFTW"(%swift.opaque* noalias nocapture %0, %swift.type* %"\CF\84_1_0", i8** %"\CF\84_1_0.P3", %T42conditional_conformance_basic_conformances6SingleV* noalias nocapture swiftself %1, %swift.type* %Self, i8** %SelfWitnessTable)
// CHECK-NEXT:  entry:
// CHECK-NEXT:    [[A_P2_PTR:%.*]] = getelementptr inbounds i8*, i8** %SelfWitnessTable, i32 -1
// CHECK-NEXT:    [[A_P2_i8star:%.*]] = load i8*, i8** [[A_P2_PTR]], align 8
// CHECK-NEXT:    [[A_P2:%.*]] = bitcast i8* [[A_P2_i8star]] to i8**
// CHECK-NEXT:    [[SELF_AS_TYPE_ARRAY:%.*]] = bitcast %swift.type* %Self to %swift.type**
// CHECK-NEXT:    [[A_PTR:%.*]] = getelementptr inbounds %swift.type*, %swift.type** [[SELF_AS_TYPE_ARRAY]], i64 2
// CHECK-NEXT:    [[A:%.*]] = load %swift.type*, %swift.type** [[A_PTR]], align 8
// CHECK-NEXT:    call swiftcc void @"$s42conditional_conformance_basic_conformances6SingleVA2A2P2RzlE7genericyyqd__AA2P3Rd__lF"(%swift.opaque* noalias nocapture %0, %swift.type* [[A]], %swift.type* %"\CF\84_1_0", i8** [[A_P2]], i8** %"\CF\84_1_0.P3")
// CHECK-NEXT:    ret void
// CHECK-NEXT:  }

public func single_generic<T: P2>(_: T.Type) {
  takes_p1(Single<T>.self)
}
// CHECK-LABEL: define{{( dllexport)?}}{{( protected)?}} swiftcc void @"$s42conditional_conformance_basic_conformances14single_genericyyxmAA2P2RzlF"(%swift.type* %0, %swift.type* %T, i8** %T.P2)
// CHECK-NEXT:  entry:
// CHECK:         %conditional.requirement.buffer = alloca [1 x i8**], align 8
// CHECK:         [[T0:%.*]] = call swiftcc %swift.metadata_response @"$s42conditional_conformance_basic_conformances6SingleVMa"(i64 0, %swift.type* %T)
// CHECK-NEXT:    [[Single_TYPE:%.*]] = extractvalue %swift.metadata_response [[T0]], 0
// CHECK-NEXT:    [[CONDITIONAL_REQUIREMENTS:%.*]] = getelementptr inbounds [1 x i8**], [1 x i8**]* %conditional.requirement.buffer, i32 0, i32 0
// CHECK-NEXT:    [[T_P2_PTR:%.*]] = getelementptr inbounds i8**, i8*** [[CONDITIONAL_REQUIREMENTS]], i32 0
// CHECK-NEXT:    store i8** %T.P2, i8*** [[T_P2_PTR]], align 8
// CHECK-NEXT:    [[Single_P1:%.*]] = call i8** @swift_getWitnessTable
// CHECK-NEXT:    call swiftcc void @"$s42conditional_conformance_basic_conformances8takes_p1yyxmAA2P1RzlF"(%swift.type* [[Single_TYPE]], %swift.type* [[Single_TYPE]], i8** [[Single_P1]])
// CHECK-NEXT:    ret void
// CHECK-NEXT:  }


public func single_concrete() {
  takes_p1(Single<IsP2>.self)
}
// CHECK-LABEL: define{{( dllexport)?}}{{( protected)?}} swiftcc void @"$s42conditional_conformance_basic_conformances15single_concreteyyF"()
// CHECK-NEXT:  entry:
// CHECK-NEXT:    [[Single_TYPE:%.*]] = call {{.*}} @__swift_instantiateConcreteTypeFromMangledName({{.*}} @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGMD")
// CHECK-NEXT:    [[Single_P1:%.*]] = call i8** @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGACyxGAA2P1A2A0G0RzlWl"()
// CHECK-NEXT:    call swiftcc void @"$s42conditional_conformance_basic_conformances8takes_p1yyxmAA2P1RzlF"(%swift.type* [[Single_TYPE]], %swift.type* [[Single_TYPE]], i8** [[Single_P1]])
// CHECK-NEXT:    ret void
// CHECK-NEXT:  }

// CHECK-PRESPECIALIZED: define{{( dllexport)?}}{{( protected)?}} swiftcc void @"$s42conditional_conformance_basic_conformances15single_concreteyyF"()
// CHECK-PRESPECIALIZED-NEXT:  entry:
// CHECK-PRESPECIALIZED-NEXT:    [[Single_P1:%.*]] = call i8** @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGACyxGAA2P1A2A0G0RzlWl"()
// CHECK-PRESPECIALIZED-NEXT:    call swiftcc void @"$s42conditional_conformance_basic_conformances8takes_p1yyxmAA2P1RzlF"(
// CHECK-PRESPECIALIZED-SAME:      %swift.type* getelementptr inbounds (
// CHECK-PRESPECIALIZED-SAME:        %swift.full_type, 
// CHECK-PRESPECIALIZED-SAME:        %swift.full_type* bitcast (
// CHECK-PRESPECIALIZED-SAME:          <{ 
// CHECK-PRESPECIALIZED-SAME:            i8**, 
// CHECK-PRESPECIALIZED-SAME:            [[INT]], 
// CHECK-PRESPECIALIZED-SAME:            %swift.type_descriptor*, 
// CHECK-PRESPECIALIZED-SAME:            %swift.type*, 
// CHECK-PRESPECIALIZED-SAME:            i64 
// CHECK-PRESPECIALIZED-SAME:          }>* @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGMf" 
// CHECK-PRESPECIALIZED-SAME:          to %swift.full_type*
// CHECK-PRESPECIALIZED-SAME:        ), 
// CHECK-PRESPECIALIZED-SAME:        i32 0, 
// CHECK-PRESPECIALIZED-SAME:        i32 1
// CHECK-PRESPECIALIZED-SAME:      ), 
// CHECK-PRESPECIALIZED-SAME:      %swift.type* getelementptr inbounds (
// CHECK-PRESPECIALIZED-SAME:        %swift.full_type, 
// CHECK-PRESPECIALIZED-SAME:        %swift.full_type* bitcast (
// CHECK-PRESPECIALIZED-SAME:          <{ 
// CHECK-PRESPECIALIZED-SAME:            i8**, 
// CHECK-PRESPECIALIZED-SAME:            [[INT]], 
// CHECK-PRESPECIALIZED-SAME:            %swift.type_descriptor*, 
// CHECK-PRESPECIALIZED-SAME:            %swift.type*, 
// CHECK-PRESPECIALIZED-SAME:            i64 
// CHECK-PRESPECIALIZED-SAME:          }>* @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGMf" 
// CHECK-PRESPECIALIZED-SAME:          to %swift.full_type*
// CHECK-PRESPECIALIZED-SAME:        ), 
// CHECK-PRESPECIALIZED-SAME:        i32 0, 
// CHECK-PRESPECIALIZED-SAME:        i32 1
// CHECK-PRESPECIALIZED-SAME:      ), 
// CHECK-PRESPECIALIZED-SAME:      i8** [[Single_P1]]
// CHECK-PRESPECIALIZED-SAME:    )
// CHECK-PRESPECIALIZED-NEXT:    ret void
// CHECK-PRESPECIALIZED-NEXT:  }


// Lazy witness table accessor for the concrete Single<IsP2> : P1.

// CHECK-LABEL: define linkonce_odr hidden i8** @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGACyxGAA2P1A2A0G0RzlWl"()
// CHECK-NEXT:  entry:
// CHECK-NEXT:    %conditional.requirement.buffer = alloca [1 x i8**], align 8
// CHECK-NEXT:    [[CACHE:%.*]] = load i8**, i8*** @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGACyxGAA2P1A2A0G0RzlWL", align 8
// CHECK-NEXT:    [[IS_NULL:%.*]] = icmp eq i8** [[CACHE]], null
// CHECK-NEXT:    br i1 [[IS_NULL]], label %cacheIsNull, label %cont

// CHECK:       cacheIsNull:
// CHECK-STABLE-ABI-FALSE-NEXT:    [[T0:%.*]] = call swiftcc %swift.metadata_response @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGMa"(i64 255)
// CHECK-STABLE-ABI-FALSE-NEXT:    [[Single_TYPE:%.*]] = extractvalue %swift.metadata_response [[T0]], 0
// CHECK-STABLE-ABI-FALSE-NEXT:    extractvalue %swift.metadata_response [[T0]], 1
// CHECK-STABLE-ABI-TRUE-NEXT:     [[T0:%.*]] = call %swift.type* @__swift_instantiateConcreteTypeFromMangledNameAbstract({ i32, i32 }* @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGMD")

// CHECK-NEXT:    [[CONDITIONAL_REQUIREMENTS:%.*]] = getelementptr inbounds [1 x i8**], [1 x i8**]* %conditional.requirement.buffer, i32 0, i32 0
// CHECK-NEXT:    [[A_P2_PTR:%.*]] = getelementptr inbounds i8**, i8*** [[CONDITIONAL_REQUIREMENTS]], i32 0
// CHECK-NEXT:    store i8** getelementptr inbounds ([1 x i8*], [1 x i8*]* @"$s42conditional_conformance_basic_conformances4IsP2VAA0F0AAWP", i32 0, i32 0), i8*** [[A_P2_PTR]], align 8

// CHECK-NEXT:    [[Single_P1:%.*]] = call i8** @swift_getWitnessTable
// CHECK-NEXT:    store atomic i8** [[Single_P1]], i8*** @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGACyxGAA2P1A2A0G0RzlWL" release, align 8
// CHECK-NEXT:    br label %cont

// CHECK:       cont:
// CHECK-NEXT:    [[T0:%.*]] = phi i8** [ [[CACHE]], %entry ], [ [[Single_P1]], %cacheIsNull ]
// CHECK-NEXT:    ret i8** [[T0]]
// CHECK-NEXT:  }

// TYPEBYNAME-LABEL: define linkonce_odr hidden i8** @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGACyxGAA2P1A2A0G0RzlWl"()
// TYPEBYNAME-NEXT:  entry:
// TYPEBYNAME-NEXT:    %conditional.requirement.buffer = alloca [1 x i8**], align 8
// TYPEBYNAME-NEXT:    [[CACHE:%.*]] = load i8**, i8*** @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGACyxGAA2P1A2A0G0RzlWL", align 8
// TYPEBYNAME-NEXT:    [[IS_NULL:%.*]] = icmp eq i8** [[CACHE]], null
// TYPEBYNAME-NEXT:    br i1 [[IS_NULL]], label %cacheIsNull, label %cont

// TYPEBYNAME:       cacheIsNull:
// TYPEBYNAME-NEXT:    [[T0:%.*]] = call %swift.type* @__swift_instantiateConcreteTypeFromMangledNameAbstract({ i32, i32 }* @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGMD")
// TYPEBYNAME-NEXT:    [[CONDITIONAL_REQUIREMENTS:%.*]] = getelementptr inbounds [1 x i8**], [1 x i8**]* %conditional.requirement.buffer, i32 0, i32 0
// TYPEBYNAME-NEXT:    [[A_P2_PTR:%.*]] = getelementptr inbounds i8**, i8*** [[CONDITIONAL_REQUIREMENTS]], i32 0
// TYPEBYNAME-NEXT:    store i8** getelementptr inbounds ([1 x i8*], [1 x i8*]* @"$s42conditional_conformance_basic_conformances4IsP2VAA0F0AAWP", i32 0, i32 0), i8*** [[A_P2_PTR]], align 8

// TYPEBYNAME-NEXT:    [[Single_P1:%.*]] = call i8** @swift_getWitnessTable
// TYPEBYNAME-NEXT:    store atomic i8** [[Single_P1]], i8*** @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGACyxGAA2P1A2A0G0RzlWL" release, align 8
// TYPEBYNAME-NEXT:    br label %cont

// TYPEBYNAME:       cont:
// TYPEBYNAME-NEXT:    [[T0:%.*]] = phi i8** [ [[CACHE]], %entry ], [ [[Single_P1]], %cacheIsNull ]
// TYPEBYNAME-NEXT:    ret i8** [[T0]]
// TYPEBYNAME-NEXT:  }

// TYPEBYNAME_PRESPECIALIZED-LABEL: define linkonce_odr hidden i8** @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGACyxGAA2P1A2A0G0RzlWl"()
// TYPEBYNAME_PRESPECIALIZED-NEXT:  entry:
// TYPEBYNAME_PRESPECIALIZED-NEXT:    %conditional.requirement.buffer = alloca [1 x i8**], align 8
// TYPEBYNAME_PRESPECIALIZED-NEXT:    [[CACHE:%.*]] = load i8**, i8*** @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGACyxGAA2P1A2A0G0RzlWL", align 8
// TYPEBYNAME_PRESPECIALIZED-NEXT:    [[IS_NULL:%.*]] = icmp eq i8** [[CACHE]], null
// TYPEBYNAME_PRESPECIALIZED-NEXT:    br i1 [[IS_NULL]], label %cacheIsNull, label %cont

// TYPEBYNAME_PRESPECIALIZED:       cacheIsNull:
// TYPEBYNAME_PRESPECIALIZED-NEXT:    [[CONDITIONAL_REQUIREMENTS:%.*]] = getelementptr inbounds [1 x i8**], [1 x i8**]* %conditional.requirement.buffer, i32 0, i32 0
// TYPEBYNAME_PRESPECIALIZED-NEXT:    [[A_P2_PTR:%.*]] = getelementptr inbounds i8**, i8*** [[CONDITIONAL_REQUIREMENTS]], i32 0
// TYPEBYNAME_PRESPECIALIZED-NEXT:    store i8** getelementptr inbounds ([1 x i8*], [1 x i8*]* @"$s42conditional_conformance_basic_conformances4IsP2VAA0F0AAWP", i32 0, i32 0), i8*** [[A_P2_PTR]], align 8

// TYPEBYNAME_PRESPECIALIZED-NEXT:    [[Single_P1:%.*]] = call i8** @swift_getWitnessTable
// TYPEBYNAME_PRESPECIALIZED-NEXT:    store atomic i8** [[Single_P1]], i8*** @"$s42conditional_conformance_basic_conformances6SingleVyAA4IsP2VGACyxGAA2P1A2A0G0RzlWL" release, align 8
// TYPEBYNAME_PRESPECIALIZED-NEXT:    br label %cont

// TYPEBYNAME_PRESPECIALIZED:       cont:
// TYPEBYNAME_PRESPECIALIZED-NEXT:    [[T0:%.*]] = phi i8** [ [[CACHE]], %entry ], [ [[Single_P1]], %cacheIsNull ]
// TYPEBYNAME_PRESPECIALIZED-NEXT:    ret i8** [[T0]]
// TYPEBYNAME_PRESPECIALIZED-NEXT:  }


public struct Double<B, C> {}
extension Double: P1 where B: P2, C: P3 {
  public func normal() {}
  public func generic<T: P3>(_: T) {}
}

// witness method for Double.normal

// CHECK-LABEL: define linkonce_odr hidden swiftcc void @"$s42conditional_conformance_basic_conformances6DoubleVyxq_GAA2P1A2A2P2RzAA2P3R_rlAaEP6normalyyFTW"(%T42conditional_conformance_basic_conformances6DoubleV* noalias nocapture swiftself %0, %swift.type* %Self, i8** %SelfWitnessTable)
// CHECK-NEXT:  entry:
// CHECK-NEXT:    [[B_P2_PTR:%.*]] = getelementptr inbounds i8*, i8** %SelfWitnessTable, i32 -1
// CHECK-NEXT:    [[B_P2_i8star:%.*]] = load i8*, i8** [[B_P2_PTR]], align 8
// CHECK-NEXT:    [[B_P2:%.*]] = bitcast i8* [[B_P2_i8star]] to i8**

// CHECK-NEXT:    [[C_P3_PTR:%.*]] = getelementptr inbounds i8*, i8** %SelfWitnessTable, i32 -2
// CHECK-NEXT:    [[C_P3_i8star:%.*]] = load i8*, i8** [[C_P3_PTR]], align 8
// CHECK-NEXT:    [[C_P3:%.*]] = bitcast i8* [[C_P3_i8star]] to i8**

// CHECK-NEXT:    [[SELF_AS_TYPE_ARRAY:%.*]] = bitcast %swift.type* %Self to %swift.type**
// CHECK-NEXT:    [[B_PTR:%.*]] = getelementptr inbounds %swift.type*, %swift.type** [[SELF_AS_TYPE_ARRAY]], i64 2
// CHECK-NEXT:    [[B:%.*]] = load %swift.type*, %swift.type** [[B_PTR]], align 8

// CHECK-NEXT:    [[SELF_AS_TYPE_ARRAY_2:%.*]] = bitcast %swift.type* %Self to %swift.type**
// CHECK-NEXT:    [[C_PTR:%.*]] = getelementptr inbounds %swift.type*, %swift.type** [[SELF_AS_TYPE_ARRAY_2]], i64 3
// CHECK-NEXT:    [[C:%.*]] = load %swift.type*, %swift.type** [[C_PTR]], align 8

// CHECK-NEXT:    call swiftcc void @"$s42conditional_conformance_basic_conformances6DoubleVA2A2P2RzAA2P3R_rlE6normalyyF"(%swift.type* [[B]], %swift.type* [[C]], i8** [[B_P2]], i8** [[C_P3]])
// CHECK-NEXT:    ret void
// CHECK-NEXT:  }

// witness method for Double.generic

// CHECK-LABEL: define linkonce_odr hidden swiftcc void @"$s42conditional_conformance_basic_conformances6DoubleVyxq_GAA2P1A2A2P2RzAA2P3R_rlAaEP7genericyyqd__AaGRd__lFTW"(%swift.opaque* noalias nocapture %0, %swift.type* %"\CF\84_1_0", i8** %"\CF\84_1_0.P3", %T42conditional_conformance_basic_conformances6DoubleV* noalias nocapture swiftself %1, %swift.type* %Self, i8** %SelfWitnessTable)
// CHECK-NEXT:  entry:

// CHECK-NEXT:    [[B_P2_PTR:%.*]] = getelementptr inbounds i8*, i8** %SelfWitnessTable, i32 -1
// CHECK-NEXT:    [[B_P2_i8star:%.*]] = load i8*, i8** [[B_P2_PTR]], align 8
// CHECK-NEXT:    [[B_P2:%.*]] = bitcast i8* [[B_P2_i8star]] to i8**

// CHECK-NEXT:    [[C_P3_PTR:%.*]] = getelementptr inbounds i8*, i8** %SelfWitnessTable, i32 -2
// CHECK-NEXT:    [[C_P3_i8star:%.*]] = load i8*, i8** [[C_P3_PTR]], align 8
// CHECK-NEXT:    [[C_P3:%.*]] = bitcast i8* [[C_P3_i8star]] to i8**

// CHECK-NEXT:    [[SELF_AS_TYPE_ARRAY:%.*]] = bitcast %swift.type* %Self to %swift.type**
// CHECK-NEXT:    [[B_PTR:%.*]] = getelementptr inbounds %swift.type*, %swift.type** [[SELF_AS_TYPE_ARRAY]], i64 2
// CHECK-NEXT:    [[B:%.*]] = load %swift.type*, %swift.type** [[B_PTR]], align 8

// CHECK-NEXT:    [[SELF_AS_TYPE_ARRAY_2:%.*]] = bitcast %swift.type* %Self to %swift.type**
// CHECK-NEXT:    [[C_PTR:%.*]] = getelementptr inbounds %swift.type*, %swift.type** [[SELF_AS_TYPE_ARRAY_2]], i64 3
// CHECK-NEXT:    [[C:%.*]] = load %swift.type*, %swift.type** [[C_PTR]], align 8

// CHECK-NEXT:    call swiftcc void @"$s42conditional_conformance_basic_conformances6DoubleVA2A2P2RzAA2P3R_rlE7genericyyqd__AaERd__lF"(%swift.opaque* noalias nocapture %0, %swift.type* [[B]], %swift.type* [[C]], %swift.type* %"\CF\84_1_0", i8** [[B_P2]], i8** [[C_P3]], i8** %"\CF\84_1_0.P3")
// CHECK-NEXT:    ret void
// CHECK-NEXT:  }


public func double_generic_generic<U: P2, V: P3>(_: U.Type, _: V.Type) {
  takes_p1(Double<U, V>.self)
}
// CHECK-LABEL: define{{( dllexport)?}}{{( protected)?}} swiftcc void @"$s42conditional_conformance_basic_conformances015double_generic_F0yyxm_q_mtAA2P2RzAA2P3R_r0_lF"(%swift.type* %0, %swift.type* %1, %swift.type* %U, %swift.type* %V, i8** %U.P2, i8** %V.P3)
// CHECK-NEXT:  entry:
// CHECK:          %conditional.requirement.buffer = alloca [2 x i8**], align 8
// CHECK:         [[T0:%.*]] = call swiftcc %swift.metadata_response @"$s42conditional_conformance_basic_conformances6DoubleVMa"(i64 0, %swift.type* %U, %swift.type* %V)
// CHECK-NEXT:    [[Double_TYPE:%.*]] = extractvalue %swift.metadata_response [[T0]], 0

// CHECK-NEXT:    [[CONDITIONAL_REQUIREMENTS:%.*]] = getelementptr inbounds [2 x i8**], [2 x i8**]* %conditional.requirement.buffer, i32 0, i32 0
// CHECK-NEXT:    [[B_P2_PTR:%.*]] = getelementptr inbounds i8**, i8*** [[CONDITIONAL_REQUIREMENTS]], i32 0
// CHECK-NEXT:    store i8** %U.P2, i8*** [[B_P2_PTR]], align 8
// CHECK-NEXT:    [[C_P3_PTR:%.*]] = getelementptr inbounds i8**, i8*** [[CONDITIONAL_REQUIREMENTS]], i32 1
// CHECK-NEXT:    store i8** %V.P3, i8*** [[C_P3_PTR]], align 8

// CHECK-NEXT:    [[Double_P1:%.*]] = call i8** @swift_getWitnessTable
// CHECK-NEXT:    call swiftcc void @"$s42conditional_conformance_basic_conformances8takes_p1yyxmAA2P1RzlF"(%swift.type* [[Double_TYPE]], %swift.type* [[Double_TYPE]], i8** [[Double_P1]])
// CHECK-NEXT:    ret void
// CHECK-NEXT:  }

public func double_generic_concrete<X: P2>(_: X.Type) {
  takes_p1(Double<X, IsP3>.self)
}
// CHECK-LABEL: define{{( dllexport)?}}{{( protected)?}} swiftcc void @"$s42conditional_conformance_basic_conformances23double_generic_concreteyyxmAA2P2RzlF"(%swift.type* %0, %swift.type* %X, i8** %X.P2)
// CHECK-NEXT:  entry:
// CHECK:         %conditional.requirement.buffer = alloca [2 x i8**], align 8
// CHECK:         [[T0:%.*]] = call swiftcc %swift.metadata_response @"$s42conditional_conformance_basic_conformances6DoubleVMa"(i64 0, %swift.type* %X, %swift.type* bitcast (i64* getelementptr inbounds (<{ i8**, i64, <{ {{.*}} }>* }>, <{ {{.*}} }>* @"$s42conditional_conformance_basic_conformances4IsP3VMf", i32 0, i32 1) to %swift.type*))
// CHECK-NEXT:    [[Double_TYPE:%.*]] = extractvalue %swift.metadata_response [[T0]], 0

// CHECK-NEXT:    [[CONDITIONAL_REQUIREMENTS:%.*]] = getelementptr inbounds [2 x i8**], [2 x i8**]* %conditional.requirement.buffer, i32 0, i32 0
// CHECK-NEXT:    [[B_P2_PTR:%.*]] = getelementptr inbounds i8**, i8*** [[CONDITIONAL_REQUIREMENTS]], i32 0
// CHECK-NEXT:    store i8** %X.P2, i8*** [[B_P2_PTR]], align 8
// CHECK-NEXT:    [[C_P3_PTR:%.*]] = getelementptr inbounds i8**, i8*** [[CONDITIONAL_REQUIREMENTS]], i32 1
// CHECK-NEXT:    store i8** getelementptr inbounds ([1 x i8*], [1 x i8*]* @"$s42conditional_conformance_basic_conformances4IsP3VAA0F0AAWP", i32 0, i32 0), i8*** [[C_P3_PTR]], align 8

// CHECK-NEXT:    [[Double_P1:%.*]] = call i8** @swift_getWitnessTable
// CHECK-NEXT:    call swiftcc void @"$s42conditional_conformance_basic_conformances8takes_p1yyxmAA2P1RzlF"(%swift.type* [[Double_TYPE]], %swift.type* [[Double_TYPE]], i8** [[Double_P1]])
// CHECK-NEXT:    ret void
// CHECK-NEXT:  }

public func double_concrete_concrete() {
  takes_p1(Double<IsP2, IsP3>.self)
}
// CHECK-LABEL: define{{( dllexport)?}}{{( protected)?}} swiftcc void @"$s42conditional_conformance_basic_conformances016double_concrete_F0yyF"()
// CHECK-NEXT:  entry:
// CHECK-NEXT:    [[Double_TYPE:%.*]] = call {{.*}} @__swift_instantiateConcreteTypeFromMangledName({{.*}} @"$s42conditional_conformance_basic_conformances6DoubleVyAA4IsP2VAA0F2P3VGMD")
// CHECK-NEXT:    [[Double_P1:%.*]] = call i8** @"$s42conditional_conformance_basic_conformances6DoubleVyAA4IsP2VAA0F2P3VGACyxq_GAA2P1A2A0G0RzAA0H0R_rlWl"()
// CHECK-NEXT:    call swiftcc void @"$s42conditional_conformance_basic_conformances8takes_p1yyxmAA2P1RzlF"(%swift.type* [[Double_TYPE]], %swift.type* [[Double_TYPE]], i8** [[Double_P1]])
// CHECK-NEXT:    ret void
// CHECK-NEXT:  }

// Lazy witness table accessor for the concrete Double<IsP2, IsP3> : P1.

// CHECK-LABEL: define linkonce_odr hidden i8** @"$s42conditional_conformance_basic_conformances6DoubleVyAA4IsP2VAA0F2P3VGACyxq_GAA2P1A2A0G0RzAA0H0R_rlWl"()
// CHECK-NEXT:  entry:
// CHECK-NEXT:    %conditional.requirement.buffer = alloca [2 x i8**], align 8
// CHECK-NEXT:    [[CACHE:%.*]] = load i8**, i8*** @"$s42conditional_conformance_basic_conformances6DoubleVyAA4IsP2VAA0F2P3VGACyxq_GAA2P1A2A0G0RzAA0H0R_rlWL", align 8
// CHECK-NEXT:    [[IS_NULL:%.*]] = icmp eq i8** [[CACHE]], null
// CHECK-NEXT:    br i1 [[IS_NULL]], label %cacheIsNull, label %cont

// CHECK:       cacheIsNull:
// CHECK-STABLE-ABI-FALSE-NEXT:    [[T0:%.*]] = call swiftcc %swift.metadata_response @"$s42conditional_conformance_basic_conformances6DoubleVyAA4IsP2VAA0F2P3VGMa"(i64 255)
// CHECK-STABLE-ABI-FALSE-NEXT:    [[Double_TYPE:%.*]] = extractvalue %swift.metadata_response [[T0]], 0
// CHECK-STABLE-ABI-FALSE-NEXT:    extractvalue %swift.metadata_response [[T0]], 1
// CHECK-STABLE-ABI-TRUE-NEXT:     [[T0:%.*]] = call %swift.type* @__swift_instantiateConcreteTypeFromMangledNameAbstract({ i32, i32 }* @"$s42conditional_conformance_basic_conformances6DoubleVyAA4IsP2VAA0F2P3VGMD")

// CHECK-NEXT:    [[CONDITIONAL_REQUIREMENTS:%.*]] = getelementptr inbounds [2 x i8**], [2 x i8**]* %conditional.requirement.buffer, i32 0, i32 0
// CHECK-NEXT:    [[B_P2_PTR:%.*]] = getelementptr inbounds i8**, i8*** [[CONDITIONAL_REQUIREMENTS]], i32 0
// CHECK-NEXT:    store i8** getelementptr inbounds ([1 x i8*], [1 x i8*]* @"$s42conditional_conformance_basic_conformances4IsP2VAA0F0AAWP", i32 0, i32 0), i8*** [[B_P2_PTR]], align 8
// CHECK-NEXT:    [[C_P3_PTR:%.*]] = getelementptr inbounds i8**, i8*** [[CONDITIONAL_REQUIREMENTS]], i32 1
// CHECK-NEXT:    store i8** getelementptr inbounds ([1 x i8*], [1 x i8*]* @"$s42conditional_conformance_basic_conformances4IsP3VAA0F0AAWP", i32 0, i32 0), i8*** [[C_P3_PTR]], align 8

// CHECK-NEXT:    [[Double_P1:%.*]] = call i8** @swift_getWitnessTable
// CHECK-NEXT:    store atomic i8** [[Double_P1]], i8*** @"$s42conditional_conformance_basic_conformances6DoubleVyAA4IsP2VAA0F2P3VGACyxq_GAA2P1A2A0G0RzAA0H0R_rlWL" release, align 8
// CHECK-NEXT:    br label %cont

// CHECK:       cont:
// CHECK-NEXT:    [[T0:%.*]] = phi i8** [ [[CACHE]], %entry ], [ [[Double_P1]], %cacheIsNull ]
// CHECK-NEXT:    ret i8** [[T0]]
// CHECK-NEXT:  }


func dynamicCastToP1(_ value: Any) -> P1? {
  return value as? P1
}

protocol P4 {}
typealias P4Typealias = P4
protocol P5 {}

struct SR7101<T> {}
extension SR7101 : P5 where T == P4Typealias {}
