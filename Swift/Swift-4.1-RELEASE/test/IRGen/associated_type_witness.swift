// RUN: %target-swift-frontend -assume-parsing-unqualified-ownership-sil -primary-file %s -emit-ir > %t.ll
// RUN: %FileCheck %s -check-prefix=GLOBAL < %t.ll
// RUN: %FileCheck %s < %t.ll
// REQUIRES: CPU=x86_64

protocol P {}
protocol Q {}

protocol Assocked {
  associatedtype Assoc : P, Q
}

struct Universal : P, Q {}

//   Witness table access functions for Universal : P and Universal : Q.
// CHECK-LABEL: define hidden i8** @_T023associated_type_witness9UniversalVAA1PAAWa()
// CHECK:         ret i8** getelementptr inbounds ([0 x i8*], [0 x i8*]* @_T023associated_type_witness9UniversalVAA1PAAWP, i32 0, i32 0)
// CHECK-LABEL: define hidden i8** @_T023associated_type_witness9UniversalVAA1QAAWa()
// CHECK:         ret i8** getelementptr inbounds ([0 x i8*], [0 x i8*]* @_T023associated_type_witness9UniversalVAA1QAAWP, i32 0, i32 0)

//   Witness table for WithUniversal : Assocked.
// GLOBAL-LABEL: @_T023associated_type_witness13WithUniversalVAA8AssockedAAWP = hidden constant [3 x i8*] [
// GLOBAL-SAME:    i8* bitcast (%swift.type* ()* @_T023associated_type_witness9UniversalVMa to i8*)
// GLOBAL-SAME:    i8* bitcast (i8** ()* @_T023associated_type_witness9UniversalVAA1PAAWa to i8*)
// GLOBAL-SAME:    i8* bitcast (i8** ()* @_T023associated_type_witness9UniversalVAA1QAAWa to i8*)  
// GLOBAL-SAME:  ]
struct WithUniversal : Assocked {
  typealias Assoc = Universal
}

//   Witness table for GenericWithUniversal : Assocked.
// GLOBAL-LABEL: @_T023associated_type_witness20GenericWithUniversalVyxGAA8AssockedAAWP = hidden constant [3 x i8*] [
// GLOBAL-SAME:    i8* bitcast (%swift.type* ()* @_T023associated_type_witness9UniversalVMa to i8*)
// GLOBAL-SAME:    i8* bitcast (i8** ()* @_T023associated_type_witness9UniversalVAA1PAAWa to i8*)
// GLOBAL-SAME:    i8* bitcast (i8** ()* @_T023associated_type_witness9UniversalVAA1QAAWa to i8*)  
// GLOBAL-SAME:  ]
struct GenericWithUniversal<T> : Assocked {
  typealias Assoc = Universal
}

//   Witness table for Fulfilled : Assocked.
// GLOBAL-LABEL: @_T023associated_type_witness9FulfilledVyxGAA8AssockedAAWP = hidden constant [3 x i8*] [
// GLOBAL-SAME:    i8* bitcast (%swift.type* (%swift.type*, i8**)* @_T023associated_type_witness9FulfilledVyxGAA8AssockedAA5AssocWt to i8*)
// GLOBAL-SAME:    i8* bitcast (i8** (%swift.type*, %swift.type*, i8**)* @_T023associated_type_witness9FulfilledVyxGAA8AssockedAA5Assoc_AA1PPWT to i8*)
// GLOBAL-SAME:    i8* bitcast (i8** (%swift.type*, %swift.type*, i8**)* @_T023associated_type_witness9FulfilledVyxGAA8AssockedAA5Assoc_AA1QPWT to i8*)
// GLOBAL-SAME:  ]
struct Fulfilled<T : P & Q> : Assocked {
  typealias Assoc = T
}

//   Associated type metadata access function for Fulfilled.Assoc.
// CHECK-LABEL:  define internal %swift.type* @_T023associated_type_witness9FulfilledVyxGAA8AssockedAA5AssocWt(%swift.type* %"Fulfilled<T>", i8** %"Fulfilled<T>.Assocked")
// CHECK:         [[T0:%.*]] = bitcast %swift.type* %"Fulfilled<T>" to %swift.type**
// CHECK-NEXT:    [[T1:%.*]] = getelementptr inbounds %swift.type*, %swift.type** [[T0]], i64 2
// CHECK-NEXT:    [[T2:%.*]] = load %swift.type*, %swift.type** [[T1]], align 8, !invariant.load
// CHECK-NEXT:    ret %swift.type* [[T2]]

//   Associated type witness table access function for Fulfilled.Assoc : P.
// CHECK-LABEL:  define internal i8** @_T023associated_type_witness9FulfilledVyxGAA8AssockedAA5Assoc_AA1PPWT(%swift.type* %"Fulfilled<T>.Assoc", %swift.type* %"Fulfilled<T>", i8** %"Fulfilled<T>.Assocked")
// CHECK:         [[T0:%.*]] = bitcast %swift.type* %"Fulfilled<T>" to i8***
// CHECK-NEXT:    [[T1:%.*]] = getelementptr inbounds i8**, i8*** [[T0]], i64 3
// CHECK-NEXT:    [[T2:%.*]] = load i8**, i8*** [[T1]], align 8, !invariant.load
// CHECK-NEXT:    ret i8** [[T2]]

//   Associated type witness table access function for Fulfilled.Assoc : Q.
// CHECK-LABEL:  define internal i8** @_T023associated_type_witness9FulfilledVyxGAA8AssockedAA5Assoc_AA1QPWT(%swift.type* %"Fulfilled<T>.Assoc", %swift.type* %"Fulfilled<T>", i8** %"Fulfilled<T>.Assocked")
// CHECK:         [[T0:%.*]] = bitcast %swift.type* %"Fulfilled<T>" to i8***
// CHECK-NEXT:    [[T1:%.*]] = getelementptr inbounds i8**, i8*** [[T0]], i64 4
// CHECK-NEXT:    [[T2:%.*]] = load i8**, i8*** [[T1]], align 8, !invariant.load
// CHECK-NEXT:    ret i8** [[T2]]

struct Pair<T, U> : P, Q {}

//   Generic witness table pattern for Computed : Assocked.
// GLOBAL-LABEL: @_T023associated_type_witness8ComputedVyxq_GAA8AssockedAAWP = hidden constant [3 x i8*] [
// GLOBAL-SAME:    i8* bitcast (%swift.type* (%swift.type*, i8**)* @_T023associated_type_witness8ComputedVyxq_GAA8AssockedAA5AssocWt to i8*)
// GLOBAL-SAME:    i8* bitcast (i8** (%swift.type*, %swift.type*, i8**)* @_T023associated_type_witness8ComputedVyxq_GAA8AssockedAA5Assoc_AA1PPWT to i8*)
// GLOBAL-SAME:    i8* bitcast (i8** (%swift.type*, %swift.type*, i8**)* @_T023associated_type_witness8ComputedVyxq_GAA8AssockedAA5Assoc_AA1QPWT to i8*)
// GLOBAL-SAME:  ]
//   Generic witness table cache for Computed : Assocked.
// GLOBAL-LABEL: @_T023associated_type_witness8ComputedVyxq_GAA8AssockedAAWG = internal constant %swift.generic_witness_table_cache {
// GLOBAL-SAME:    i16 3,
// GLOBAL-SAME:    i16 1,
//    Relative reference to protocol
// GLOBAL-SAME:    i32 trunc (i64 sub (i64 ptrtoint (%swift.protocol* @_T023associated_type_witness8AssockedMp to i64), i64 ptrtoint (i32* getelementptr inbounds (%swift.generic_witness_table_cache, %swift.generic_witness_table_cache* @_T023associated_type_witness8ComputedVyxq_GAA8AssockedAAWG, i32 0, i32 2) to i64)) to i32
//    Relative reference to witness table template
// GLOBAL-SAME:    i32 trunc (i64 sub (i64 ptrtoint ([3 x i8*]* @_T023associated_type_witness8ComputedVyxq_GAA8AssockedAAWP to i64), i64 ptrtoint (i32* getelementptr inbounds (%swift.generic_witness_table_cache, %swift.generic_witness_table_cache* @_T023associated_type_witness8ComputedVyxq_GAA8AssockedAAWG, i32 0, i32 3) to i64)) to i32),
//    No instantiator function
// GLOBAL-SAME:    i32 0,
// GLOBAL-SAME:    i32 trunc (i64 sub (i64 ptrtoint ([16 x i8*]* [[PRIVATE:@.*]] to i64), i64 ptrtoint (i32* getelementptr inbounds (%swift.generic_witness_table_cache, %swift.generic_witness_table_cache* @_T023associated_type_witness8ComputedVyxq_GAA8AssockedAAWG, i32 0, i32 5) to i64)) to i32)
// GLOBAL-SAME:  }
// GLOBAL:       [[PRIVATE]] = internal global [16 x i8*] zeroinitializer

struct Computed<T, U> : Assocked {
  typealias Assoc = Pair<T, U>
}

//   Associated type metadata access function for Computed.Assoc.
// CHECK-LABEL:  define internal %swift.type* @_T023associated_type_witness8ComputedVyxq_GAA8AssockedAA5AssocWt(%swift.type* %"Computed<T, U>", i8** %"Computed<T, U>.Assocked")
// CHECK:         entry:
// CHECK:          [[T0:%.*]] = getelementptr inbounds i8*, i8** %"Computed<T, U>.Assocked", i32 -1
// CHECK-NEXT:     [[CACHE:%.*]] = bitcast i8** [[T0]] to %swift.type**
// CHECK-NEXT:     [[CACHE_RESULT:%.*]] = load %swift.type*, %swift.type** [[CACHE]], align 8
// CHECK-NEXT:     [[T1:%.*]] = icmp eq %swift.type* [[CACHE_RESULT]], null
// CHECK-NEXT:     br i1 [[T1]], label %fetch, label %cont
// CHECK:        cont:
// CHECK-NEXT:     [[T0:%.*]] = phi %swift.type* [ [[CACHE_RESULT]], %entry ], [ [[FETCH_RESULT:%.*]], %fetch ]
// CHECK-NEXT:     ret %swift.type* [[T0]]
// CHECK:        fetch:
// CHECK-NEXT:    [[T0:%.*]] = bitcast %swift.type* %"Computed<T, U>" to %swift.type**
// CHECK-NEXT:    [[T1:%.*]] = getelementptr inbounds %swift.type*, %swift.type** [[T0]], i64 2
// CHECK-NEXT:    [[T:%.*]] = load %swift.type*, %swift.type** [[T1]], align 8, !invariant.load
// CHECK:         [[T0:%.*]] = bitcast %swift.type* %"Computed<T, U>" to %swift.type**
// CHECK-NEXT:    [[T1:%.*]] = getelementptr inbounds %swift.type*, %swift.type** [[T0]], i64 3
// CHECK-NEXT:    [[U:%.*]] = load %swift.type*, %swift.type** [[T1]], align 8, !invariant.load
// CHECK-NEXT:    [[FETCH_RESULT]] = call %swift.type* @_T023associated_type_witness4PairVMa(%swift.type* [[T]], %swift.type* [[U]])
// CHECK-NEXT:    store atomic %swift.type* [[FETCH_RESULT]], %swift.type** [[CACHE]] release, align 8
// CHECK-NEXT:    br label %cont

//   Witness table accessor function for Computed : Assocked.
// CHECK-LABEL: define hidden i8** @_T023associated_type_witness15GenericComputedVyxGAA22DerivedFromSimpleAssocAAWa(%swift.type*, i8***, i64)
// CHECK:         entry:
// CHECK-NEXT:     %conditional.tables = alloca %swift.witness_table_slice, align 8
// CHECK-NEXT:     [[TABLES:%.*]] = getelementptr inbounds %swift.witness_table_slice, %swift.witness_table_slice* %conditional.tables, i32 0, i32 0
// CHECK-NEXT:     store i8*** %1, i8**** [[TABLES]], align 8
// CHECK-NEXT:     [[COUNT:%.*]] = getelementptr inbounds %swift.witness_table_slice, %swift.witness_table_slice* %conditional.tables, i32 0, i32 1
// CHECK-NEXT:     store i64 %2, i64* [[COUNT]], align 8
// CHECK-NEXT:     [[INSTANTIATION_ARGS:%.*]] = bitcast %swift.witness_table_slice* %conditional.tables to i8**
// CHECK-NEXT:     [[WTABLE:%.*]] = call i8** @swift_rt_swift_getGenericWitnessTable(%swift.generic_witness_table_cache* @_T023associated_type_witness15GenericComputedVyxGAA22DerivedFromSimpleAssocAAWG, %swift.type* %0, i8** [[INSTANTIATION_ARGS]])
// CHECK-NEXT:     ret i8** [[WTABLE]]


struct PBox<T: P> {}
protocol HasSimpleAssoc {
  associatedtype Assoc
}
protocol DerivedFromSimpleAssoc : HasSimpleAssoc {}


//   Generic witness table pattern for GenericComputed : DerivedFromSimpleAssoc.
// GLOBAL-LABEL: @_T023associated_type_witness15GenericComputedVyxGAA22DerivedFromSimpleAssocAAWP = hidden constant [1 x i8*] zeroinitializer
//   Generic witness table cache for GenericComputed : DerivedFromSimpleAssoc.
// GLOBAL-LABEL: @_T023associated_type_witness15GenericComputedVyxGAA22DerivedFromSimpleAssocAAWG = internal constant %swift.generic_witness_table_cache {
// GLOBAL-SAME:    i16 1,
// GLOBAL-SAME:    i16 0,
//   Relative reference to protocol
// GLOBAL-SAME:    i32 trunc (i64 sub (i64 ptrtoint (%swift.protocol* @_T023associated_type_witness22DerivedFromSimpleAssocMp to i64),
//   Relative reference to witness table template
// GLOBAL-SAME:    i32 trunc (i64 sub (i64 ptrtoint ([1 x i8*]* @_T023associated_type_witness15GenericComputedVyxGAA22DerivedFromSimpleAssocAAWP to i64)
//   Relative reference to instantiator function
// GLOBAL-SAME:    i32 trunc (i64 sub (i64 ptrtoint (void (i8**, %swift.type*, i8**)* @_T023associated_type_witness15GenericComputedVyxGAA22DerivedFromSimpleAssocAAWI to i64), i64 ptrtoint (i32* getelementptr inbounds (%swift.generic_witness_table_cache, %swift.generic_witness_table_cache* @_T023associated_type_witness15GenericComputedVyxGAA22DerivedFromSimpleAssocAAWG, i32 0, i32 4) to i64)) to i32)
// GLOBAL-SAME:    i32 trunc (i64 sub (i64 ptrtoint ([16 x i8*]* @1 to i64), i64 ptrtoint (i32* getelementptr inbounds (%swift.generic_witness_table_cache, %swift.generic_witness_table_cache* @_T023associated_type_witness15GenericComputedVyxGAA22DerivedFromSimpleAssocAAWG, i32 0, i32 5) to i64)) to i32)
// GLOBAL-SAME:  }
struct GenericComputed<T: P> : DerivedFromSimpleAssoc {
  typealias Assoc = PBox<T>
}

//   Instantiation function for GenericComputed : DerivedFromSimpleAssoc.
// CHECK-LABEL: define internal void @_T023associated_type_witness15GenericComputedVyxGAA22DerivedFromSimpleAssocAAWI(i8**, %swift.type*, i8**)
// CHECK:         [[T0:%.*]] = call i8** @_T023associated_type_witness15GenericComputedVyxGAA14HasSimpleAssocAAWa(%swift.type* %1, i8*** undef, i64 0)
// CHECK-NEXT:    [[T1:%.*]] = bitcast i8** [[T0]] to i8*
// CHECK-NEXT:    [[T2:%.*]] = getelementptr inbounds i8*, i8** %0, i32 0
// CHECK-NEXT:    store i8* [[T1]], i8** [[T2]], align 8
// CHECK-NEXT:    ret void

//   Witness table accessor function for GenericComputed : HasSimpleAssoc..
// CHECK-LABEL: define hidden i8** @_T023associated_type_witness15GenericComputedVyxGAA14HasSimpleAssocAAWa(%swift.type*, i8***, i64)
// CHECK-NEXT:   entry:
// CHECK-NEXT:    %conditional.tables = alloca %swift.witness_table_slice, align 8
// CHECK-NEXT:    [[TABLES:%.*]] = getelementptr inbounds %swift.witness_table_slice, %swift.witness_table_slice* %conditional.tables, i32 0, i32 0
// CHECK-NEXT:    store i8*** %1, i8**** [[TABLES]], align 8
// CHECK-NEXT:    [[COUNT:%.*]] = getelementptr inbounds %swift.witness_table_slice, %swift.witness_table_slice* %conditional.tables, i32 0, i32 1
// CHECK-NEXT:    store i64 %2, i64* [[COUNT]], align 8
// CHECK-NEXT:    [[INSTANTIATION_ARGS:%.*]] = bitcast %swift.witness_table_slice* %conditional.tables to i8**
// CHECK-NEXT:    [[WTABLE:%.*]] = call i8** @swift_rt_swift_getGenericWitnessTable(%swift.generic_witness_table_cache* @_T023associated_type_witness15GenericComputedVyxGAA14HasSimpleAssocAAWG, %swift.type* %0, i8** [[INSTANTIATION_ARGS]])
// CHECK-NEXT:    ret i8** [[WTABLE]]


protocol HasAssocked {
  associatedtype Contents : Assocked
}
struct FulfilledFromAssociatedType<T : HasAssocked> : HasSimpleAssoc {
  typealias Assoc = PBox<T.Contents.Assoc>
}

struct UsesVoid : HasSimpleAssoc {
  typealias Assoc = ()
}
