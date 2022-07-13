// RUN: %target-swift-frontend  -parse-as-library -emit-silgen -enable-sil-ownership %s | %FileCheck %s

// Check that we don't crash if a local function references another local
// function without captures.

// CHECK-LABEL: sil hidden @_T014local_captures10globalfuncyycyF : $@convention(thin) () -> @owned @callee_guaranteed () -> ()
func globalfunc() -> () -> () {

	// CHECK-LABEL: sil private @_T014local_captures10globalfuncyycyF0A4FuncL_yyF : $@convention(thin) () -> ()
	func localFunc() {
	}

	// CHECK-LABEL: sil private @_T014local_captures10globalfuncyycyF6callitL_yyF : $@convention(thin) () -> ()
	func callit() {
		localFunc()
	}

	// CHECK-LABEL: sil private @_T014local_captures10globalfuncyycyF5getitL_yycyF : $@convention(thin) () -> @owned @callee_guaranteed () -> ()
	func getit() -> () -> () {
		return localFunc
	}

	callit()
	return getit()
}
