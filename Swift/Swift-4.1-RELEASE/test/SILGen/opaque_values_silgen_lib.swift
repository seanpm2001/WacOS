// RUN: %target-swift-frontend -enable-sil-ownership -enable-sil-opaque-values -emit-sorted-sil -Xllvm -sil-full-demangle -parse-stdlib -parse-as-library -emit-silgen -module-name Swift %s | %FileCheck %s
// UNSUPPORTED: resilient_stdlib

precedencegroup AssignmentPrecedence { assignment: true }

enum Optional<Wrapped> {
  case none
  case some(Wrapped)
}

protocol EmptyP {}

struct String { var ptr: Builtin.NativeObject }

// Tests Empty protocol + Builtin.NativeObject enum (including opaque tuples as a return value)
// ---
// CHECK-LABEL: sil hidden @_T0s21s010______PAndS_casesyyF : $@convention(thin) () -> () {
// CHECK: bb0:
// CHECK:   [[MTYPE:%.*]] = metatype $@thin PAndSEnum.Type
// CHECK:   [[EAPPLY:%.*]] = apply {{.*}}([[MTYPE]]) : $@convention(thin) (@thin PAndSEnum.Type) -> @owned @callee_guaranteed (@in EmptyP, @owned String) -> @out PAndSEnum
// CHECK:   destroy_value [[EAPPLY]]
// CHECK:   return %{{.*}} : $()
// CHECK-LABEL: } // end sil function '_T0s21s010______PAndS_casesyyF'
func s010______PAndS_cases() {
  _ = PAndSEnum.A
}

// Test emitBuiltinReinterpretCast.
// ---
// CHECK-LABEL: sil hidden @_T0s21s020__________bitCastq_x_q_m2totr0_lF : $@convention(thin) <T, U> (@in T, @thick U.Type) -> @out U {
// CHECK: [[BORROW:%.*]] = begin_borrow %0 : $T
// CHECK: [[COPY:%.*]] = copy_value [[BORROW]] : $T
// CHECK: [[CAST:%.*]] = unchecked_bitwise_cast [[COPY]] : $T to $U
// CHECK: [[RET:%.*]] = copy_value [[CAST]] : $U
// CHECK: destroy_value [[COPY]] : $T
// CHECK: return [[RET]] : $U
// CHECK-LABEL: } // end sil function '_T0s21s020__________bitCastq_x_q_m2totr0_lF'
func s020__________bitCast<T, U>(_ x: T, to type: U.Type) -> U {
  return Builtin.reinterpretCast(x)
}

// Test emitBuiltinCastReference
// ---
// CHECK-LABEL: sil hidden @_T0s21s030__________refCastq_x_q_m2totr0_lF : $@convention(thin) <T, U> (@in T, @thick U.Type) -> @out U {
// CHECK: bb0(%0 : @owned $T, %1 : @trivial $@thick U.Type):
// CHECK: [[BORROW:%.*]] = begin_borrow %0 : $T
// CHECK: [[COPY:%.*]] = copy_value [[BORROW]] : $T
// CHECK: [[SRC:%.*]] = alloc_stack $T
// CHECK: store [[COPY]] to [init] [[SRC]] : $*T
// CHECK: [[DEST:%.*]] = alloc_stack $U
// CHECK: unchecked_ref_cast_addr  T in [[SRC]] : $*T to U in [[DEST]] : $*U
// CHECK: [[LOAD:%.*]] = load [take] [[DEST]] : $*U
// CHECK: dealloc_stack [[DEST]] : $*U
// CHECK: dealloc_stack [[SRC]] : $*T
// CHECK: destroy_value %0 : $T
// CHECK: return [[LOAD]] : $U
// CHECK-LABEL: } // end sil function '_T0s21s030__________refCastq_x_q_m2totr0_lF'
func s030__________refCast<T, U>(_ x: T, to: U.Type) -> U {
  return Builtin.castReference(x)
}

// Init of Empty protocol + Builtin.NativeObject enum (including opaque tuples as a return value)
// ---
// CHECK-LABEL: sil shared [transparent] @_T0s9PAndSEnumO1AABs6EmptyP_p_SStcABmF : $@convention(method) (@in EmptyP, @owned String, @thin PAndSEnum.Type) -> @out PAndSEnum {
// CHECK: bb0([[ARG0:%.*]] : @owned $EmptyP, [[ARG1:%.*]]  : @owned $String, [[ARG2:%.*]] : @trivial $@thin PAndSEnum.Type):
// CHECK:   [[RTUPLE:%.*]] = tuple ([[ARG0]] : $EmptyP, [[ARG1]] : $String)
// CHECK:   [[RETVAL:%.*]] = enum $PAndSEnum, #PAndSEnum.A!enumelt.1, [[RTUPLE]] : $(EmptyP, String)
// CHECK:   return [[RETVAL]] : $PAndSEnum
// CHECK-LABEL: } // end sil function '_T0s9PAndSEnumO1AABs6EmptyP_p_SStcABmF'
// CHECK-LABEL: sil shared [transparent] [thunk] @_T0s9PAndSEnumO1AABs6EmptyP_p_SStcABmFTc : $@convention(thin) (@thin PAndSEnum.Type) -> @owned @callee_guaranteed (@in EmptyP, @owned String) -> @out PAndSEnum {
// CHECK: bb0([[ARG:%.*]] : @trivial $@thin PAndSEnum.Type):
// CHECK:   [[RETVAL:%.*]] = partial_apply [callee_guaranteed] {{.*}}([[ARG]]) : $@convention(method) (@in EmptyP, @owned String, @thin PAndSEnum.Type) -> @out PAndSEnum
// CHECK:   return [[RETVAL]] : $@callee_guaranteed (@in EmptyP, @owned String) -> @out PAndSEnum
// CHECK-LABEL: } // end sil function '_T0s9PAndSEnumO1AABs6EmptyP_p_SStcABmFTc'
enum PAndSEnum { case A(EmptyP, String) }

