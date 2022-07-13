// RUN: %target-swift-frontend -assume-parsing-unqualified-ownership-sil -primary-file %s -emit-ir -disable-objc-attr-requires-foundation-module -swift-version 4 | %FileCheck %s

protocol P1 {
	associatedtype AssocP1
}

protocol P2 {
	associatedtype AssocP2: P1

	func getAssocP2() -> AssocP2
}

protocol P3 {
	associatedtype AssocP3: P2 where AssocP3.AssocP2: Q

	func getAssocP3() -> AssocP3
}

protocol Q { }

struct X { }

struct Y: P1, Q {
	typealias AssocP1 = X
}

struct Z: P2 {
	typealias AssocP2 = Y

	func getAssocP2() -> Y { return Y() }
}

// CHECK: @_T035witness_table_indirect_conformances1WVAA2P3AAWP = hidden constant [4 x i8*] [i8* bitcast (%swift.type* ()* @_T035witness_table_indirect_conformances1ZVMa to i8*), i8* bitcast (i8** ()* @_T035witness_table_indirect_conformances1ZVAA2P2AAWa to i8*), i8* bitcast (i8** ()* @_T035witness_table_indirect_conformances1YVAA1QAAWa to i8*), i8* bitcast (void (%T35witness_table_indirect_conformances1ZV*, %T35witness_table_indirect_conformances1WV*, %swift.type*, i8**)* @_T035witness_table_indirect_conformances1WVAA2P3A2aDP08getAssocE00gE0QzyFTW to i8*)]
struct W: P3 {
	typealias AssocP3 = Z

	func getAssocP3() -> Z { return Z() }
}

// CHECK-LABEL: define hidden i8** @_T035witness_table_indirect_conformances1YVAA1QAAWa()
// CHECK-NEXT: entry:
// CHECK-NEXT: ret i8** getelementptr inbounds ([0 x i8*], [0 x i8*]* @_T035witness_table_indirect_conformances1YVAA1QAAWP, i32 0, i32 0)

// CHECK-LABEL: define hidden i8** @_T035witness_table_indirect_conformances1ZVAA2P2AAWa()
// CHECK-NEXT: entry:
// CHECK: ret i8** getelementptr inbounds ([3 x i8*], [3 x i8*]* @_T035witness_table_indirect_conformances1ZVAA2P2AAWP, i32 0, i32 0)

// CHECK-LABEL: define hidden %swift.type* @_T035witness_table_indirect_conformances1ZVMa()
// CHECK-NEXT: entry:
// CHECK-NEXT: ret %swift.type* bitcast {{.*}} @_T035witness_table_indirect_conformances1ZVMf, i32 0, i32 1) to %swift.type*
