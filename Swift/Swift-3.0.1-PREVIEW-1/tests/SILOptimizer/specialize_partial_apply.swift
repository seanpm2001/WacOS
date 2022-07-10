// First check the SIL.
// RUN: %target-swift-frontend -O -Xllvm -sil-disable-pass="Function Signature Optimization" -module-name=test -emit-sil -primary-file %s | %FileCheck %s

// Also do an end-to-end test to check all components, including IRGen.
// RUN: rm -rf %t && mkdir -p %t 
// RUN: %target-build-swift -O -Xllvm -sil-disable-pass="Function Signature Optimization" -module-name=test %s -o %t/a.out
// RUN: %target-run %t/a.out | %FileCheck %s -check-prefix=CHECK-OUTPUT
// REQUIRES: executable_test

struct MyError : Error {
	let _code : Int
	init(_ xx: Int) {
		_code = xx
	}
}

// We need a reabstraction thunk to convert from direct args/result to indirect
// args/result, which is expected in the returned closure.

// CHECK-LABEL: sil shared [noinline] @_TTSg5Si___TF4test16generic_get_funcurFTxSb_Fxx : $@convention(thin) (Int, Bool) -> @owned @callee_owned (@in Int) -> @out Int {
// CHECK: [[F:%[0-9]+]] = function_ref @_TTSr5Si___TFF4test16generic_get_funcurFTxSb_FxxL_7genericurfxx : $@convention(thin) (@in Int, Bool, @owned @box Int) -> @out Int
// CHECK: [[PA:%[0-9]+]] = partial_apply [[F]](%1, %{{[0-9]+}}) : $@convention(thin) (@in Int, Bool, @owned @box Int) -> @out Int
// CHECK: return [[PA]] : $@callee_owned (@in Int) -> @out Int
@inline(never)
func generic_get_func<T>(_ t1: T, _ b: Bool) -> (T) -> T {

	@inline(never)
	func generic(_ t2: T) -> T {
		return b ? t1 : t2
	}

	return generic
}

// CHECK-LABEL: sil hidden [noinline] @_TF4test7testit1FSbFSiSi : $@convention(thin) (Bool) -> @owned @callee_owned (Int) -> Int {
// CHECK: [[F:%[0-9]+]] = function_ref @_TTSg5Si___TF4test16generic_get_funcurFTxSb_Fxx : $@convention(thin) (Int, Bool) -> @owned @callee_owned (@in Int) -> @out Int
// CHECK: [[CL:%[0-9]+]] = apply [[F]](%{{[0-9]+}}, %0) : $@convention(thin) (Int, Bool) -> @owned @callee_owned (@in Int) -> @out Int
// CHECK: [[TH:%[0-9]+]] = function_ref @_TTRXFo_iSi_iSi_XFo_dSi_dSi_ : $@convention(thin) (Int, @owned @callee_owned (@in Int) -> @out Int) -> Int
// CHECK: [[RET:%[0-9]+]] = partial_apply [[TH]]([[CL]]) : $@convention(thin) (Int, @owned @callee_owned (@in Int) -> @out Int) -> Int
// CHECK: return [[RET]] : $@callee_owned (Int) -> Int
@inline(never)
func testit1(_ b: Bool) -> (Int) -> Int {
	return generic_get_func(27, b)
}


@inline(never)
func generic2<T>(_ t1: T, t2: T, b: Bool) -> T {
	return b ? t1 : t2
}

// No reabstraction thunk is needed because the returned closure expects direct
// args/result anyway.

// CHECK-LABEL: sil hidden [noinline] @_TF4test17concrete_get_funcFT_FTSiSiSb_Si : $@convention(thin) () -> @owned @callee_owned (Int, Int, Bool) -> Int {
// CHECK: [[F:%[0-9]+]] = function_ref @_TTSg5Si___TF4test8generic2urFTx2t2x1bSb_x : $@convention(thin) (Int, Int, Bool) -> Int
// CHECK: [[RET:%[0-9]+]] = thin_to_thick_function [[F]] : $@convention(thin) (Int, Int, Bool) -> Int to $@callee_owned (Int, Int, Bool) -> Int
// CHECK: return [[RET]] : $@callee_owned (Int, Int, Bool) -> Int
@inline(never)
func concrete_get_func() -> (Int, Int, Bool) -> Int {
	return generic2
}

@inline(never)
func testit2() -> (Int, Int, Bool) -> Int {
	return concrete_get_func()
}


@inline(never)
func generic3<T>(_ t1: T, _ t2: T, _ b: Bool) -> T {
	return b ? t1 : t2
}

// No reabstraction thunk is needed because we directly apply the returned closure.

// CHECK-LABEL: sil hidden [noinline] @_TF4test7testit3FSbSi : $@convention(thin) (Bool) -> Int {
// CHECK: [[F:%[0-9]+]] = function_ref @_TTSg5Si___TF4test8generic3urFTxxSb_x : $@convention(thin) (Int, Int, Bool) -> Int
// CHECK: [[RET:%[0-9]+]] = apply [[F]]({{.*}}) : $@convention(thin) (Int, Int, Bool) -> Int
// CHECK: return [[RET]] : $Int
@inline(never)
func testit3(_ b: Bool) -> Int {
	return generic3(270, 28, b)
}

// The same three test cases again, but with throwing functions.

// We need a reabstraction thunk to convert from direct args/result to indirect
// args/result, which is expected in the returned closure.

// CHECK-LABEL: sil shared [noinline] @_TTSg5Si___TF4test25generic_get_func_throwingurFSbFzxx : $@convention(thin) (Bool) -> @owned @callee_owned (@in Int) -> (@out Int, @error Error) {
// CHECK: [[F:%[0-9]+]] = function_ref @_TTSr5Si___TFF4test25generic_get_func_throwingurFSbFzxxL_7genericurfzxx : $@convention(thin) (@in Int, Bool) -> (@out Int, @error Error)
// CHECK: [[PA:%[0-9]+]] = partial_apply [[F]](%0) : $@convention(thin) (@in Int, Bool) -> (@out Int, @error Error)
// CHECK: return [[PA]] : $@callee_owned (@in Int) -> (@out Int, @error Error)
@inline(never)
func generic_get_func_throwing<T>(_ b: Bool) -> (T) throws -> T {

	@inline(never)
	func generic(_ t2: T) throws -> T {
		if b {
			throw MyError(123)
		}
		return t2
	}

	return generic
}

// CHECK-LABEL: sil hidden [noinline] @_TF4test16testit1_throwingFSbFzSiSi : $@convention(thin) (Bool) -> @owned @callee_owned (Int) -> (Int, @error Error) {
// CHECK: [[F:%[0-9]+]] = function_ref @_TTSg5Si___TF4test25generic_get_func_throwingurFSbFzxx : $@convention(thin) (Bool) -> @owned @callee_owned (@in Int) -> (@out Int, @error Error)
// CHECK: [[CL:%[0-9]+]] = apply [[F]](%0) : $@convention(thin) (Bool) -> @owned @callee_owned (@in Int) -> (@out Int, @error Error)
// CHECK: [[TH:%[0-9]+]] = function_ref @_TTRXFo_iSi_iSizoPs5Error__XFo_dSi_dSizoPS___ : $@convention(thin) (Int, @owned @callee_owned (@in Int) -> (@out Int, @error Error)) -> (Int, @error Error)
// CHECK: [[RET:%[0-9]+]] = partial_apply [[TH]]([[CL]]) : $@convention(thin) (Int, @owned @callee_owned (@in Int) -> (@out Int, @error Error)) -> (Int, @error Error)
// CHECK: return [[RET]] : $@callee_owned (Int) -> (Int, @error Error)
@inline(never)
func testit1_throwing(_ b: Bool) -> (Int) throws -> Int {
	return generic_get_func_throwing(b)
}


@inline(never)
func generic2_throwing<T>(_ t1: T, b: Bool) throws -> T {
	if b {
		throw MyError(124)
	}
	return t1
}

// No reabstraction thunk is needed because the returned closure expects direct
// args/result anyway.

// CHECK-LABEL: sil hidden [noinline] @_TF4test26concrete_get_func_throwingFT_FzTSiSb_Si : $@convention(thin) () -> @owned @callee_owned (Int, Bool) -> (Int, @error Error) {
// CHECK: [[F:%[0-9]+]] = function_ref @_TTSg5Si___TF4test17generic2_throwingurFzTx1bSb_x : $@convention(thin) (Int, Bool) -> (Int, @error Error)
// CHECK: [[RET:%[0-9]+]] = thin_to_thick_function [[F]] : $@convention(thin) (Int, Bool) -> (Int, @error Error) to $@callee_owned (Int, Bool) -> (Int, @error Error)
// CHECK: return [[RET]] : $@callee_owned (Int, Bool) -> (Int, @error Error)
@inline(never)
func concrete_get_func_throwing() -> (Int, Bool) throws -> Int {
	return generic2_throwing
}

@inline(never)
func testit2_throwing() -> (Int, Bool) throws -> Int {
	return concrete_get_func_throwing()
}



@inline(never)
func generic3_throwing<T>(_ t1: T, _ b: Bool) throws -> T {
	if b {
		throw MyError(125)
	}
	return t1
}

// No reabstraction thunk is needed because we directly apply the returned closure.

// CHECK-LABEL: sil hidden [noinline] @_TF4test16testit3_throwingFSbSi : $@convention(thin) (Bool) -> Int {
// CHECK: [[F:%[0-9]+]] = function_ref @_TTSg5Si___TF4test17generic3_throwingurFzTxSb_x : $@convention(thin) (Int, Bool) -> (Int, @error Error)
// CHECK: try_apply [[F]](%{{[0-9]+}}, %0) : $@convention(thin) (Int, Bool) -> (Int, @error Error), normal bb{{[0-9]+}}, error bb{{[0-9]+}}
// CHECK: }
@inline(never)
func testit3_throwing(_ b: Bool) -> Int {
	do {
		return try generic3_throwing(271, b)
	} catch {
		return error._code
	}
}

// CHECK-LABEL: sil shared [transparent] [thunk] @_TTSr5Si___TFF4test16generic_get_funcurFTxSb_FxxL_7genericurfxx : $@convention(thin) (@in Int, Bool, @owned @box Int) -> @out Int {
// CHECK: bb0(%0 : $*Int, %1 : $*Int, %2 : $Bool, %3 : $@box Int):
// CHECK: [[LD:%[0-9]+]] = load %1 : $*Int
// CHECK: [[F:%[0-9]+]] = function_ref @_TTSg5Si___TFF4test16generic_get_funcurFTxSb_FxxL_7genericurfxx : $@convention(thin) (Int, Bool, @owned @box Int) -> Int
// CHECK: [[RET:%[0-9]+]] = apply [[F]]([[LD]], %2, %3) : $@convention(thin) (Int, Bool, @owned @box Int) -> Int
// CHECK: store [[RET]] to %0 : $*Int
// CHECK: return %{{[0-9]*}} : $()

// CHECK-LABEL: sil shared [transparent] [thunk] @_TTSr5Si___TFF4test25generic_get_func_throwingurFSbFzxxL_7genericurfzxx : $@convention(thin) (@in Int, Bool) -> (@out Int, @error Error) {
// CHECK: bb0(%0 : $*Int, %1 : $*Int, %2 : $Bool):
// CHECK: [[LD:%[0-9]+]] = load %1 : $*Int
// CHECK: [[F:%[0-9]+]] = function_ref @_TTSg5Si___TFF4test25generic_get_func_throwingurFSbFzxxL_7genericurfzxx : $@convention(thin) (Int, Bool) -> (Int, @error Error)
// CHECK: try_apply [[F]]([[LD]], %2) : $@convention(thin) (Int, Bool) -> (Int, @error Error), normal bb1, error bb2
// CHECK: bb1([[NORMAL:%[0-9]+]] : $Int):
// CHECK: store [[NORMAL]] to %0 : $*Int
// CHECK: return %{{[0-9]*}} : $()
// CHECK: bb2([[ERROR:%[0-9]+]] : $Error):
// CHECK: throw [[ERROR]] : $Error


// The main program.
// Check if the generated executable produces the correct output.

// CHECK-OUTPUT: 18
print(testit1(false)(18))
// CHECK-OUTPUT: 27
print(testit1(true)(18))

// CHECK-OUTPUT: 4
print(testit2()(3, 4, false))
// CHECK-OUTPUT: 3
print(testit2()(3, 4, true))

// CHECK-OUTPUT: 28
print(testit3(false))
// CHECK-OUTPUT: 270
print(testit3(true))

var x: Int
do {
	x = try testit1_throwing(false)(19)
} catch {
	x = error._code
}
// CHECK-OUTPUT: 19
print(x)
do {
	x = try testit1_throwing(true)(19)
} catch {
	x = error._code
}
// CHECK-OUTPUT: 123
print(x)

do {
	x = try testit2_throwing()(20, false)
} catch {
	x = error._code
}
// CHECK-OUTPUT: 20
print(x)
do {
	x = try testit2_throwing()(20, true)
} catch {
	x = error._code
}
// CHECK-OUTPUT: 124
print(x)

// CHECK-OUTPUT: 271
print(testit3_throwing(false))
// CHECK-OUTPUT: 125
print(testit3_throwing(true))

