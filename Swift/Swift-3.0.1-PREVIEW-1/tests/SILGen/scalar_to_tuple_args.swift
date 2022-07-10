// RUN: %target-swift-frontend -emit-silgen %s | %FileCheck %s

func inoutWithDefaults(_ x: inout Int, y: Int = 0, z: Int = 0) {}
func inoutWithCallerSideDefaults(_ x: inout Int, y: Int = #line) {}

func scalarWithDefaults(_ x: Int, y: Int = 0, z: Int = 0) {}
func scalarWithCallerSideDefaults(_ x: Int, y: Int = #line) {}

func tupleWithDefaults(x x: (Int, Int), y: Int = 0, z: Int = 0) {}

func variadicFirst(_ x: Int...) {}
func variadicSecond(_ x: Int, _ y: Int...) {}

var x = 0
// CHECK: [[X_ADDR:%.*]] = global_addr @_Tv20scalar_to_tuple_args1xSi : $*Int
// CHECK: [[INOUT_WITH_DEFAULTS:%.*]] = function_ref @_TF20scalar_to_tuple_args17inoutWithDefaultsFTRSi1ySi1zSi_T_
// CHECK: [[DEFAULT_Y:%.*]] = apply {{.*}} : $@convention(thin) () -> Int
// CHECK: [[DEFAULT_Z:%.*]] = apply {{.*}} : $@convention(thin) () -> Int
// CHECK: apply [[INOUT_WITH_DEFAULTS]]([[X_ADDR]], [[DEFAULT_Y]], [[DEFAULT_Z]])
inoutWithDefaults(&x)

// CHECK: [[INOUT_WITH_CALLER_DEFAULTS:%.*]] = function_ref @_TF20scalar_to_tuple_args27inoutWithCallerSideDefaultsFTRSi1ySi_T_
// CHECK: [[LINE_VAL:%.*]] = integer_literal
// CHECK: [[LINE:%.*]] = apply {{.*}}([[LINE_VAL]]
// CHECK: apply [[INOUT_WITH_CALLER_DEFAULTS]]([[X_ADDR]], [[LINE]])
inoutWithCallerSideDefaults(&x)

// CHECK: [[SCALAR_WITH_DEFAULTS:%.*]] = function_ref @_TF20scalar_to_tuple_args18scalarWithDefaultsFTSi1ySi1zSi_T_
// CHECK: [[X:%.*]] = load [[X_ADDR]]
// CHECK: [[DEFAULT_Y:%.*]] = apply {{.*}} : $@convention(thin) () -> Int
// CHECK: [[DEFAULT_Z:%.*]] = apply {{.*}} : $@convention(thin) () -> Int
// CHECK: apply [[SCALAR_WITH_DEFAULTS]]([[X]], [[DEFAULT_Y]], [[DEFAULT_Z]])
scalarWithDefaults(x)

// CHECK: [[SCALAR_WITH_CALLER_DEFAULTS:%.*]] = function_ref @_TF20scalar_to_tuple_args28scalarWithCallerSideDefaultsFTSi1ySi_T_
// CHECK: [[X:%.*]] = load [[X_ADDR]]
// CHECK: [[LINE_VAL:%.*]] = integer_literal
// CHECK: [[LINE:%.*]] = apply {{.*}}([[LINE_VAL]]
// CHECK: apply [[SCALAR_WITH_CALLER_DEFAULTS]]([[X]], [[LINE]])
scalarWithCallerSideDefaults(x)

// CHECK: [[TUPLE_WITH_DEFAULTS:%.*]] = function_ref @_TF20scalar_to_tuple_args17tupleWithDefaultsFT1xTSiSi_1ySi1zSi_T_
// CHECK: [[X1:%.*]] = load [[X_ADDR]]
// CHECK: [[X2:%.*]] = load [[X_ADDR]]
// CHECK: [[DEFAULT_Y:%.*]] = apply {{.*}} : $@convention(thin) () -> Int
// CHECK: [[DEFAULT_Z:%.*]] = apply {{.*}} : $@convention(thin) () -> Int
// CHECK: apply [[TUPLE_WITH_DEFAULTS]]([[X1]], [[X2]], [[DEFAULT_Y]], [[DEFAULT_Z]])
tupleWithDefaults(x: (x,x))

// CHECK: [[VARIADIC_FIRST:%.*]] = function_ref @_TF20scalar_to_tuple_args13variadicFirstFtGSaSi__T_
// CHECK: [[ALLOC_ARRAY:%.*]] = apply {{.*}} -> (@owned Array<τ_0_0>, Builtin.RawPointer)
// CHECK: [[ARRAY:%.*]] = tuple_extract [[ALLOC_ARRAY]] {{.*}}, 0
// CHECK: [[MEMORY:%.*]] = tuple_extract [[ALLOC_ARRAY]] {{.*}}, 1
// CHECK: [[ADDR:%.*]] = pointer_to_address [[MEMORY]]
// CHECK: [[X:%.*]] = load [[X_ADDR]]
// CHECK: store [[X]] to [[ADDR]]
// CHECK: apply [[VARIADIC_FIRST]]([[ARRAY]])
variadicFirst(x)

// CHECK: [[VARIADIC_SECOND:%.*]] = function_ref @_TF20scalar_to_tuple_args14variadicSecondFtSiGSaSi__T_
// CHECK: [[ALLOC_ARRAY:%.*]] = apply {{.*}} -> (@owned Array<τ_0_0>, Builtin.RawPointer)
// CHECK: [[ARRAY:%.*]] = tuple_extract [[ALLOC_ARRAY]] {{.*}}, 0
// CHECK: [[X:%.*]] = load [[X_ADDR]]
// CHECK: apply [[VARIADIC_SECOND]]([[X]], [[ARRAY]])
variadicSecond(x)
