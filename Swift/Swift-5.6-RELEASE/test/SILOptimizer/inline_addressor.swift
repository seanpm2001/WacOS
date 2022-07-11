// RUN: %target-swift-frontend -primary-file %s -parse-as-library -emit-sil -O | %FileCheck %s

var inputval = nonTrivialInit(false)

var totalsum = nonTrivialInit(true)


// Check if the addressor functions for inputval and totalsum are
// 1) hoisted out of the loop (by GlobalOpt) and
// 2) inlined

//CHECK-LABEL: sil {{.*}}testit 
//CHECK: {{^bb0}}
//CHECK: WZ
//CHECK-NOT: {{^bb0}}
//CHECK: {{^bb1}}
//CHECK-NOT: WZ
//CHECK-NOT: totalsum
//CHECK-NOT: inputval
//CHECK: {{^}$}}
func testit(_ x: Int) {
	for _ in 0...10000000 {
		totalsum += inputval
	}
}

@inline(never)
func nonTrivialInit(_ b: Bool) -> Int {
	return b ? 0 : 27
}
