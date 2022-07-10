// RUN: %target-swift-frontend  -Xllvm -sil-disable-pass="Function Signature Optimization" -emit-sil -o - -O %s | %FileCheck %s

//////////////////
// Declarations //
//////////////////

public class C {}
public class D : C {}
public class E {}

var b : UInt8 = 0
var c = C()
var d = D()
var e = E()
var f : UInt64 = 0
var o : AnyObject = c

////////////////////////////
// Archetype To Archetype //
////////////////////////////

@inline(never)
public func ArchetypeToArchetype<T1, T2>(t t: T1, t2: T2) -> T2 {
  return t as! T2
}

ArchetypeToArchetype(t: b, t2: b)
ArchetypeToArchetype(t: c, t2: c)
ArchetypeToArchetype(t: b, t2: c)
ArchetypeToArchetype(t: c, t2: b)
ArchetypeToArchetype(t: c, t2: d)
ArchetypeToArchetype(t: d, t2: c)
ArchetypeToArchetype(t: c, t2: e)
ArchetypeToArchetype(t: b, t2: f)

// x -> x where x is not a class.
// CHECK-LABEL: sil shared [noinline] @_TTSg5Vs5UInt8_S____TF37specialize_unconditional_checked_cast20ArchetypeToArchetype{{.*}} : $@convention(thin) (UInt8, UInt8) -> UInt8 {
// CHECK-NOT: unconditional_checked_cast archetype_to_archetype

// x -> x where x is a class.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C_S0____TF37specialize_unconditional_checked_cast20ArchetypeToArchetype{{.*}} : $@convention(thin) (@owned C, @owned C) -> @owned C {
// CHECK-NOT: unconditional_checked_cast archetype_to_archetype

// x -> y where x is not a class but y is.
// CHECK-LABEL: sil shared [noinline] @_TTSg5Vs5UInt8_C37specialize_unconditional_checked_cast1C___TF37specialize_unconditional_checked_cast20ArchetypeToArchetype{{.*}} : $@convention(thin) (UInt8, @owned C) -> @owned C {
// CHECK-NOT: unconditional_checked_cast_addr
// CHECK-NOT: unconditional_checked_cast_addr
// CHECK:     builtin "int_trap"
// CHECK-NOT: unconditional_checked_cast_addr

// y -> x where x is not a class but y is.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C_Vs5UInt8___TF37specialize_unconditional_checked_cast20ArchetypeToArchetype{{.*}} : $@convention(thin) (@owned C, UInt8) -> UInt8 {
// CHECK-NOT: unconditional_checked_cast archetype_to_archetype
// CHECK: builtin "int_trap"
// CHECK-NOT: unconditional_checked_cast archetype_to_archetype

// x -> y where x is a super class of y.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C_CS_1D___TF37specialize_unconditional_checked_cast20ArchetypeToArchetype{{.*}} : $@convention(thin) (@owned C, @owned D) -> @owned D {
// CHECK: [[STACK:%[0-9]+]] = alloc_stack $C
// TODO: This should be optimized to an unconditional_checked_cast without the need of alloc_stack: rdar://problem/24775038
// CHECK: unconditional_checked_cast_addr take_always C in [[STACK]] : $*C to D in

// y -> x where x is a super class of y.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1D_CS_1C___TF37specialize_unconditional_checked_cast20ArchetypeToArchetype{{.*}} : $@convention(thin) (@owned D, @owned C) -> @owned C {
// CHECK-NOT: unconditional_checked_cast archetype_to_archetype
// CHECK: upcast {{%[0-9]+}} : $D to $C
// CHECK-NOT: unconditional_checked_cast archetype_to_archetype

// x -> y where x and y are unrelated classes.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C_CS_1E___TF37specialize_unconditional_checked_cast20ArchetypeToArchetype{{.*}} : $@convention(thin) (@owned C, @owned E) -> @owned E {
// CHECK-NOT: unconditional_checked_cast archetype_to_archetype
// CHECK: builtin "int_trap"
// CHECK-NOT: unconditional_checked_cast archetype_to_archetype

// x -> y where x and y are unrelated non classes.
// CHECK-LABEL: sil shared [noinline] @_TTSg5Vs5UInt8_Vs6UInt64___TF37specialize_unconditional_checked_cast20ArchetypeToArchetype{{.*}} : $@convention(thin) (UInt8, UInt64) -> UInt64 {
// CHECK-NOT: unconditional_checked_cast archetype_to_archetype
// CHECK-NOT: unconditional_checked_cast archetype_to_archetype
// CHECK:      builtin "int_trap"
// CHECK-NOT: unconditional_checked_cast archetype_to_archetype


///////////////////////////
// Archetype To Concrete //
///////////////////////////

@inline(never)
public func ArchetypeToConcreteConvertUInt8<T>(t t: T) -> UInt8 {
  return t as! UInt8
}
ArchetypeToConcreteConvertUInt8(t: b)
ArchetypeToConcreteConvertUInt8(t: c)
ArchetypeToConcreteConvertUInt8(t: f)

// x -> x where x is not a class.
// CHECK-LABEL: sil shared [noinline] @_TTSg5Vs5UInt8___TF37specialize_unconditional_checked_cast31ArchetypeToConcreteConvertUInt8{{.*}} : $@convention(thin) (UInt8) -> UInt8 {
// CHECK: bb0
// CHECK-NEXT: debug_value %0
// CHECK-NEXT: return %0

// x -> y where y is a class but x is not.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C___TF37specialize_unconditional_checked_cast31ArchetypeToConcreteConvertUInt8
// CHECK: bb0
// CHECK: builtin "int_trap"
// CHECK: unreachable
// CHECK-NEXT: }

// x -> y where x,y are not classes and x is a different type from y.
// CHECK-LABEL: sil shared [noinline] @_TTSg5Vs6UInt64___TF37specialize_unconditional_checked_cast31ArchetypeToConcreteConvertUInt8
// CHECK: bb0
// CHECK: builtin "int_trap"
// CHECK: unreachable
// CHECK-NEXT: }

// x -> x where x is a class.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C___TF37specialize_unconditional_checked_cast27ArchetypeToConcreteConvertC{{.*}} : $@convention(thin) (@owned C) -> @owned C {
// CHECK: bb0
// CHECK-NEXT: debug_value %0
// CHECK: return %0

// x -> y where x is a class but y is not.
// CHECK-LABEL: sil shared [noinline] @_TTSg5Vs5UInt8___TF37specialize_unconditional_checked_cast27ArchetypeToConcreteConvertC
// CHECK: bb0
// CHECK: builtin "int_trap"
// CHECK: unreachable
// CHECK-NEXT: }

// x -> y where x,y are classes and x is a super class of y.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1D___TF37specialize_unconditional_checked_cast27ArchetypeToConcreteConvertC{{.*}} : $@convention(thin) (@owned D) -> @owned C {
// CHECK: bb0
// CHECK-NEXT: debug_value %0
// CHECK: [[UC:%[0-9]+]] = upcast %0
// CHECK-NEXT: return [[UC]]

// x -> y where x,y are classes, but x is unrelated to y.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1E___TF37specialize_unconditional_checked_cast27ArchetypeToConcreteConvertC
// CHECK: bb0
// CHECK: builtin "int_trap"
// CHECK: unreachable
// CHECK-NEXT: }

@inline(never)
public func ArchetypeToConcreteConvertC<T>(t t: T) -> C {
  return t as! C
}

ArchetypeToConcreteConvertC(t: c)
ArchetypeToConcreteConvertC(t: b)
ArchetypeToConcreteConvertC(t: d)
ArchetypeToConcreteConvertC(t: e)

@inline(never)
public func ArchetypeToConcreteConvertD<T>(t t: T) -> D {
  return t as! D
}

ArchetypeToConcreteConvertD(t: c)

// x -> y where x,y are classes and x is a sub class of y.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C___TF37specialize_unconditional_checked_cast27ArchetypeToConcreteConvertD{{.*}} : $@convention(thin) (@owned C) -> @owned D {
// CHECK: bb0(%0 : $C):
// CHECK-DAG: [[STACK_C:%[0-9]+]] = alloc_stack $C
// CHECK-DAG: store %0 to [[STACK_C]]
// CHECK-DAG: [[STACK_D:%[0-9]+]] = alloc_stack $D
// TODO: This should be optimized to an unconditional_checked_cast without the need of alloc_stack: rdar://problem/24775038
// CHECK-DAG: unconditional_checked_cast_addr take_always C in [[STACK_C]] : $*C to D in [[STACK_D]] : $*D
// CHECK-DAG: [[LOAD:%[0-9]+]] = load [[STACK_D]]
// CHECK: return [[LOAD]]

@inline(never)
public func ArchetypeToConcreteConvertE<T>(t t: T) -> E {
  return t as! E
}

ArchetypeToConcreteConvertE(t: c)

// x -> y where x,y are classes, but y is unrelated to x. The idea is
// to make sure that the fact that y is concrete does not affect the
// result.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C___TF37specialize_unconditional_checked_cast27ArchetypeToConcreteConvertE
// CHECK: bb0
// CHECK: builtin "int_trap"
// CHECK: unreachable
// CHECK-NEXT: }


///////////////////////////
// Concrete to Archetype //
///////////////////////////

@inline(never)
public func ConcreteToArchetypeConvertUInt8<T>(t t: UInt8, t2: T) -> T {
  return t as! T
}

ConcreteToArchetypeConvertUInt8(t: b, t2: b)
ConcreteToArchetypeConvertUInt8(t: b, t2: c)
ConcreteToArchetypeConvertUInt8(t: b, t2: f)

// x -> x where x is not a class.
// CHECK-LABEL: sil shared [noinline] @_TTSg5Vs5UInt8___TF37specialize_unconditional_checked_cast31ConcreteToArchetypeConvertUInt8{{.*}} : $@convention(thin) (UInt8, UInt8) -> UInt8 {
// CHECK: bb0(%0 : $UInt8, %1 : $UInt8):
// CHECK-NEXT: debug_value %0
// CHECK-NEXT: return %0

// x -> y where x is not a class but y is a class.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C___TF37specialize_unconditional_checked_cast31ConcreteToArchetypeConvertUInt8{{.*}} : $@convention(thin) (UInt8, @owned C) -> @owned C {
// CHECK: bb0
// CHECK: builtin "int_trap"
// CHECK: unreachable
// CHECK-NEXT: }

// x -> y where x,y are different non class types.
// CHECK-LABEL: sil shared [noinline] @_TTSg5Vs6UInt64___TF37specialize_unconditional_checked_cast31ConcreteToArchetypeConvertUInt8{{.*}} : $@convention(thin) (UInt8, UInt64) -> UInt64 {
// CHECK: bb0
// CHECK: builtin "int_trap"
// CHECK: unreachable
// CHECK-NEXT: }

@inline(never)
public func ConcreteToArchetypeConvertC<T>(t t: C, t2: T) -> T {
  return t as! T
}

ConcreteToArchetypeConvertC(t: c, t2: c)
ConcreteToArchetypeConvertC(t: c, t2: b)
ConcreteToArchetypeConvertC(t: c, t2: d)
ConcreteToArchetypeConvertC(t: c, t2: e)


// x -> x where x is a class.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C___TF37specialize_unconditional_checked_cast27ConcreteToArchetypeConvertC{{.*}} : $@convention(thin) (@owned C, @owned C) -> @owned C {
// CHECK: bb0(%0 : $C, %1 : $C):
// CHECK: strong_release %1
// CHECK: return %0

// x -> y where x is a class but y is not.
// CHECK-LABEL: sil shared [noinline] @_TTSg5Vs5UInt8___TF37specialize_unconditional_checked_cast27ConcreteToArchetypeConvertC{{.*}} : $@convention(thin) (@owned C, UInt8) -> UInt8 {
// CHECK: bb0
// CHECK: builtin "int_trap"
// CHECK: unreachable
// CHECK-NEXT: }

// x -> y where x is a super class of y.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1D___TF37specialize_unconditional_checked_cast27ConcreteToArchetypeConvertC{{.*}} : $@convention(thin) (@owned C, @owned D) -> @owned D {
// CHECK: bb0(%0 : $C, %1 : $D):
// CHECK-DAG: [[STACK_C:%[0-9]+]] = alloc_stack $C
// CHECK-DAG: store %0 to [[STACK_C]]
// CHECK-DAG: [[STACK_D:%[0-9]+]] = alloc_stack $D
// TODO: This should be optimized to an unconditional_checked_cast without the need of alloc_stack: rdar://problem/24775038
// CHECK-DAG: unconditional_checked_cast_addr take_always C in [[STACK_C]] : $*C to D in [[STACK_D]] : $*D
// CHECK-DAG: strong_release %1
// CHECK-DAG: strong_release %0
// CHECK-DAG: [[LOAD:%[0-9]+]] = load [[STACK_D]]
// CHECK: return [[LOAD]]

// x -> y where x and y are unrelated classes.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1E___TF37specialize_unconditional_checked_cast27ConcreteToArchetypeConvertC{{.*}} : $@convention(thin) (@owned C, @owned E) -> @owned E {
// CHECK: bb0(%0 : $C, %1 : $E):
// CHECK-NEXT: builtin "int_trap"
// CHECK-NEXT: unreachable
// CHECK-NEXT: }

@inline(never)
public func ConcreteToArchetypeConvertD<T>(t t: D, t2: T) -> T {
  return t as! T
}

ConcreteToArchetypeConvertD(t: d, t2: c)

// x -> y where x is a subclass of y.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C___TF37specialize_unconditional_checked_cast27ConcreteToArchetypeConvertD{{.*}} : $@convention(thin) (@owned D, @owned C) -> @owned C {
// CHECK: bb0(%0 : $D, %1 : $C):
// CHECK-DAG: [[UC:%[0-9]+]] = upcast %0
// CHECK-DAG: strong_release %1
// CHECK: return [[UC]]


////////////////////////
// Super To Archetype //
////////////////////////

@inline(never)
public func SuperToArchetypeC<T>(c c : C, t : T) -> T {
  return c as! T
}

SuperToArchetypeC(c: c, t: c)
SuperToArchetypeC(c: c, t: d)
SuperToArchetypeC(c: c, t: b)


// x -> x where x is a class.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C___TF37specialize_unconditional_checked_cast17SuperToArchetypeC{{.*}} : $@convention(thin) (@owned C, @owned C) -> @owned C {
// CHECK: bb0(%0 : $C, %1 : $C):
// CHECK: strong_release %1
// CHECK: return %0

// x -> y where x is a super class of y.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1D___TF37specialize_unconditional_checked_cast17SuperToArchetypeC{{.*}} : $@convention(thin) (@owned C, @owned D) -> @owned D {
// CHECK: bb0
// CHECK: unconditional_checked_cast_addr take_always C in

// x -> y where x is a class and y is not.
// CHECK-LABEL: sil shared [noinline] @_TTSg5Vs5UInt8___TF37specialize_unconditional_checked_cast17SuperToArchetypeC{{.*}} : $@convention(thin) (@owned C, UInt8) -> UInt8 {
// CHECK: bb0
// CHECK: builtin "int_trap"
// CHECK: unreachable
// CHECK-NEXT: }

@inline(never)
public func SuperToArchetypeD<T>(d d : D, t : T) -> T {
  return d as! T
}

SuperToArchetypeD(d: d, t: c)
SuperToArchetypeD(d: d, t: d)

// *NOTE* The frontend is smart enough to turn this into an upcast. When this
// test is converted to SIL, this should be fixed appropriately.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C___TF37specialize_unconditional_checked_cast17SuperToArchetypeD{{.*}} : $@convention(thin) (@owned D, @owned C) -> @owned C {
// CHECK-NOT: unconditional_checked_cast super_to_archetype
// CHECK: upcast
// CHECK-NOT: unconditional_checked_cast super_to_archetype

// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1D___TF37specialize_unconditional_checked_cast17SuperToArchetypeD{{.*}} : $@convention(thin) (@owned D, @owned D) -> @owned D {
// CHECK: bb0(%0 : $D, %1 : $D):
// CHECK: strong_release %1
// CHECK: return %0

//////////////////////////////
// Existential To Archetype //
//////////////////////////////

@inline(never)
public func ExistentialToArchetype<T>(o o : AnyObject, t : T) -> T {
  return o as! T
}

// AnyObject -> Class.
// CHECK-LABEL: sil shared [noinline] @_TTSg5C37specialize_unconditional_checked_cast1C___TF37specialize_unconditional_checked_cast22ExistentialToArchetype{{.*}} : $@convention(thin) (@owned AnyObject, @owned C) -> @owned C {
// CHECK: unconditional_checked_cast_addr take_always AnyObject in {{%.*}} : $*AnyObject to C

// AnyObject -> Non Class (should always fail)
// CHECK-LABEL: sil shared [noinline] @_TTSg5Vs5UInt8___TF37specialize_unconditional_checked_cast22ExistentialToArchetype{{.*}} : $@convention(thin) (@owned AnyObject, UInt8) -> UInt8 {
// CHECK-NOT: builtin "int_trap"()
// CHECK-NOT: unreachable
// CHECK: return

// AnyObject -> AnyObject
// CHECK-LABEL: sil shared [noinline] @_TTSg5Ps9AnyObject____TF37specialize_unconditional_checked_cast22ExistentialToArchetype{{.*}} : $@convention(thin) (@owned AnyObject, @owned AnyObject) -> @owned AnyObject {
// CHECK: bb0(%0 : $AnyObject, %1 : $AnyObject):
// CHECK: strong_release %1
// CHECK: return %0

ExistentialToArchetype(o: o, t: c)
ExistentialToArchetype(o: o, t: b)
ExistentialToArchetype(o: o, t: o)

// Ensure that a downcast from an Optional source is not promoted to a
// value cast. We could do the promotion, but the optimizer would need
// to insert the Optional unwrapping logic before the cast.
//
// CHECK-LABEL: sil shared [noinline] @_TTSg5GSqC37specialize_unconditional_checked_cast1C__CS_1D___TF37specialize_unconditional_checked_cast15genericDownCastu0_rFTxMq__q_ : $@convention(thin) (@owned Optional<C>, @thick D.Type) -> @owned D {
// CHECK: bb0(%0 : $Optional<C>, %1 : $@thick D.Type):
// CHECK-DAG: [[STACK_D:%[0-9]+]] = alloc_stack $D
// CHECK-DAG: [[STACK_C:%[0-9]+]] = alloc_stack $Optional<C>
// CHECK-DAG: store %0 to [[STACK_C]]
// TODO: This should be optimized to an unconditional_checked_cast without the need of alloc_stack: rdar://problem/24775038
// CHECK-DAG: unconditional_checked_cast_addr take_always Optional<C> in [[STACK_C]] : $*Optional<C> to D in [[STACK_D]] : $*D
// CHECK-DAG: [[LOAD:%[0-9]+]] = load [[STACK_D]]
// CHECK: return [[LOAD]]
@inline(never)
public func genericDownCast<T, U>(_ a: T, _ : U.Type) -> U {
  return a as! U
}

public func callGenericDownCast(_ c: C?) -> D {
  return genericDownCast(c, D.self)
}

