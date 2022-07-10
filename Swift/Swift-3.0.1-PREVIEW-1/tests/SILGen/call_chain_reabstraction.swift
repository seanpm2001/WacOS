// RUN: %target-swift-frontend -emit-silgen %s | %FileCheck %s

struct A {
        func g<U>(_ recur: (A, U) -> U) -> (A, U) -> U {
                return { _, x in return x }
        }
        // CHECK-LABEL: sil hidden @_TFV24call_chain_reabstraction1A1f
        // CHECK:         [[G:%.*]] = function_ref @_TFV24call_chain_reabstraction1A1g
        // CHECK:         [[G2:%.*]] = apply [[G]]<A>
        // CHECK:         [[REABSTRACT_THUNK:%.*]] = function_ref @_TTRXFo_dV24call_chain_reabstraction1AiS0__iS0__XFo_dS0_dS0__dS0__
        // CHECK:         [[REABSTRACT:%.*]] = partial_apply [[REABSTRACT_THUNK]]([[G2]])
        // CHECK:         apply [[REABSTRACT]]([[SELF:%.*]], [[SELF]])
        func f() {
                let recur: (A, A) -> A = { c, x in x }
                let b = g(recur)(self, self)
        }
}
