// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership %s | %FileCheck %s

struct A {
        func g<U>(_ recur: (A, U) -> U) -> (A, U) -> U {
                return { _, x in return x }
        }
        // CHECK-LABEL: sil hidden @_T024call_chain_reabstraction1AV1f{{[_0-9a-zA-Z]*}}F
        // CHECK:         [[G:%.*]] = function_ref @_T024call_chain_reabstraction1AV1g{{[_0-9a-zA-Z]*}}F
        // CHECK:         [[G2:%.*]] = apply [[G]]<A>
        // CHECK:         [[REABSTRACT_THUNK:%.*]] = function_ref @_T024call_chain_reabstraction1AVA2CIegyir_A3CIegyyd_TR
        // CHECK:         [[REABSTRACT:%.*]] = partial_apply [callee_guaranteed] [[REABSTRACT_THUNK]]([[G2]])
        // CHECK:         [[BORROW:%.*]] = begin_borrow [[REABSTRACT]]
        // CHECK:         apply [[BORROW]]([[SELF:%.*]], [[SELF]])
        // CHECK:         destroy_value [[REABSTRACT]]
        func f() {
                let recur: (A, A) -> A = { c, x in x }
                let b = g(recur)(self, self)
        }
}
