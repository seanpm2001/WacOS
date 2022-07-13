// RUN: %target-swift-frontend -emit-silgen %s | %FileCheck %s
// RUN: %target-swift-frontend -emit-silgen -enforce-exclusivity=unchecked %s | %FileCheck --check-prefix=UNCHECKED %s

class Base {
  var stored: Int = 0

// CHECK-LABEL: sil hidden [transparent] @_T017materializeForSet4BaseC6storedSivm : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @guaranteed Base) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// CHECK: bb0([[BUFFER:%.*]] : $Builtin.RawPointer, [[STORAGE:%.*]] : $*Builtin.UnsafeValueBuffer, [[SELF:%.*]] : $Base):
// CHECK:   [[T0:%.*]] = ref_element_addr [[SELF]] : $Base, #Base.stored
// CHECK:   [[T1:%.*]] = address_to_pointer [[T0]] : $*Int to $Builtin.RawPointer
// CHECK:   [[T2:%.*]] = enum $Optional<Builtin.RawPointer>, #Optional.some
// CHECK:   [[T3:%.*]] = tuple ([[T1]] : $Builtin.RawPointer, [[T2]] : $Optional<Builtin.RawPointer>)
// CHECK:   return [[T3]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>)
// CHECK: }

// UNCHECKED-LABEL: sil hidden [transparent] @_T017materializeForSet4BaseC6storedSivm : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @guaranteed Base) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// UNCHECKED: bb0([[BUFFER:%.*]] : $Builtin.RawPointer, [[STORAGE:%.*]] : $*Builtin.UnsafeValueBuffer, [[SELF:%.*]] : $Base):
// UNCHECKED:   [[T0:%.*]] = ref_element_addr [[SELF]] : $Base, #Base.stored
// UNCHECKED:   [[T1:%.*]] = address_to_pointer [[T0]] : $*Int to $Builtin.RawPointer
// UNCHECKED:   [[T2:%.*]] = enum $Optional<Builtin.RawPointer>, #Optional.none
// UNCHECKED:   [[T3:%.*]] = tuple ([[T1]] : $Builtin.RawPointer, [[T2]] : $Optional<Builtin.RawPointer>)
// UNCHECKED:   return [[T3]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>)
// UNCHECKED: }

// CHECK-LABEL: sil private [transparent] @_T017materializeForSet4BaseC8computedSivmytfU_ : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout Base, @thick Base.Type) -> () {
// CHECK: bb0([[BUFFER:%.*]] : $Builtin.RawPointer, [[STORAGE:%.*]] : $*Builtin.UnsafeValueBuffer, [[SELF:%.*]] : $*Base, [[SELFTYPE:%.*]] : $@thick Base.Type):
// CHECK:   [[T0:%.*]] = load_borrow [[SELF]]
// CHECK:   [[T1:%.*]] = pointer_to_address [[BUFFER]] : $Builtin.RawPointer to [strict] $*Int
// CHECK:   [[T2:%.*]] = load [trivial] [[T1]] : $*Int
// CHECK:   [[SETTER:%.*]] = function_ref @_T017materializeForSet4BaseC8computedSivs
// CHECK:   apply [[SETTER]]([[T2]], [[T0]])

// CHECK-LABEL: sil hidden [transparent] @_T017materializeForSet4BaseC8computedSivm : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @guaranteed Base) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// CHECK: bb0([[BUFFER:%.*]] : $Builtin.RawPointer, [[STORAGE:%.*]] : $*Builtin.UnsafeValueBuffer, [[SELF:%.*]] : $Base):
// CHECK:   [[ADDR:%.*]] = pointer_to_address [[BUFFER]] : $Builtin.RawPointer to [strict] $*Int
// CHECK:   [[T0:%.*]] = function_ref @_T017materializeForSet4BaseC8computedSivg
// CHECK:   [[T1:%.*]] = apply [[T0]]([[SELF]])
// CHECK:   store [[T1]] to [trivial] [[ADDR]] : $*Int
// CHECK:   [[BUFFER:%.*]] = address_to_pointer [[ADDR]]
// CHECK:   [[T0:%.*]] = function_ref @_T017materializeForSet4BaseC8computedSivmytfU_ : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout Base, @thick Base.Type) -> ()
// CHECK:   [[T2:%.*]] = thin_function_to_pointer [[T0]]
// CHECK:   [[T3:%.*]] = enum $Optional<Builtin.RawPointer>, #Optional.some!enumelt.1, [[T2]] : $Builtin.RawPointer
// CHECK:   [[T4:%.*]] = tuple ([[BUFFER]] : $Builtin.RawPointer, [[T3]] : $Optional<Builtin.RawPointer>)
// CHECK:   return [[T4]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>)
// CHECK: }

  var computed: Int {
    get { return 0 }
    set(value) {}
  }

  var storedFunction: () -> Int = { 0 }
  final var finalStoredFunction: () -> Int = { 0 }
  var computedFunction: () -> Int {
    get { return {0} }
    set {}
  }
  static var staticFunction: () -> Int {
    get { return {0} }
    set {}
  }
}

class Derived : Base {}

protocol Abstractable {
  associatedtype Result
  var storedFunction: () -> Result { get set }
  var finalStoredFunction: () -> Result { get set }
  var computedFunction: () -> Result { get set }
  static var staticFunction: () -> Result { get set }
}

// Validate that we thunk materializeForSet correctly when there's
// an abstraction pattern present.

extension Derived : Abstractable {}

// CHECK-LABEL: sil private [transparent] @_T017materializeForSet7DerivedCAA12AbstractableA2aDP14storedFunction6ResultQzycvmytfU_TW : $@convention(witness_method: Abstractable) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout Derived, @thick Derived.Type) -> ()
// CHECK: bb0(%0 : $Builtin.RawPointer, %1 : $*Builtin.UnsafeValueBuffer, %2 : $*Derived, %3 : $@thick Derived.Type):
// CHECK-NEXT: [[T0:%.*]] = load_borrow %2 : $*Derived
// CHECK-NEXT: [[SELF:%.*]] = upcast [[T0]] : $Derived to $Base
// CHECK-NEXT: [[RESULT_ADDR:%.*]] = pointer_to_address %0 : $Builtin.RawPointer to [strict] $*@callee_guaranteed () -> @out Int
// CHECK-NEXT: [[VALUE:%.*]] = load [take] [[RESULT_ADDR]] : $*@callee_guaranteed () -> @out Int
// CHECK-NEXT: function_ref
// CHECK-NEXT: [[REABSTRACTOR:%.*]] = function_ref @_T0SiIegr_SiIegd_TR : $@convention(thin) (@guaranteed @callee_guaranteed () -> @out Int) -> Int
// CHECK-NEXT: [[NEWVALUE:%.*]] = partial_apply [callee_guaranteed] [[REABSTRACTOR]]([[VALUE]])
// CHECK-NEXT: [[FN:%.*]] = class_method [[SELF]] : $Base, #Base.storedFunction!setter.1 : (Base) -> (@escaping () -> Int) -> ()
// CHECK-NEXT: apply [[FN]]([[NEWVALUE]], [[SELF]])
// CHECK-NEXT: end_borrow [[T0]] from %2
// CHECK-NEXT: tuple ()
// CHECK-NEXT: return

// CHECK-LABEL: sil private [transparent] [thunk] @_T017materializeForSet7DerivedCAA12AbstractableA2aDP14storedFunction{{[_0-9a-zA-Z]*}}vmTW
// CHECK: bb0(%0 : $Builtin.RawPointer, %1 : $*Builtin.UnsafeValueBuffer, %2 : $*Derived):
// CHECK-NEXT: [[RESULT_ADDR:%.*]] = pointer_to_address %0 : $Builtin.RawPointer to [strict] $*@callee_guaranteed () -> @out Int
// CHECK-NEXT: [[T0:%.*]] = load_borrow %2 : $*Derived
// CHECK-NEXT: [[SELF:%.*]] = upcast [[T0]] : $Derived to $Base
// CHECK-NEXT: [[TEMP:%.*]] = alloc_stack $@callee_guaranteed () -> Int
// CHECK-NEXT: [[FN:%.*]] = class_method [[SELF]] : $Base, #Base.storedFunction!getter.1
// CHECK-NEXT: [[RESULT:%.*]] = apply [[FN]]([[SELF]])
// CHECK-NEXT: store [[RESULT]] to [init] [[TEMP]]
// CHECK-NEXT: [[RESULT:%.*]] = load [copy] [[TEMP]]
// CHECK-NEXT: function_ref
// CHECK-NEXT: [[REABSTRACTOR:%.*]] = function_ref @_T0SiIegd_SiIegr_TR : $@convention(thin) (@guaranteed @callee_guaranteed () -> Int) -> @out Int
// CHECK-NEXT: [[T1:%.*]] = partial_apply [callee_guaranteed] [[REABSTRACTOR]]([[RESULT]])
// CHECK-NEXT: destroy_addr [[TEMP]]
// CHECK-NEXT: store [[T1]] to [init] [[RESULT_ADDR]]
// CHECK-NEXT: [[RESULT_PTR:%.*]] = address_to_pointer [[RESULT_ADDR]] : $*@callee_guaranteed () -> @out Int to $Builtin.RawPointer
// CHECK-NEXT: function_ref
// CHECK-NEXT: [[T2:%.*]] = function_ref @_T017materializeForSet7DerivedCAA12AbstractableA2aDP14storedFunction6ResultQzycvmytfU_TW
// CHECK-NEXT: [[T3:%.*]] = thin_function_to_pointer [[T2]]
// CHECK-NEXT: [[CALLBACK:%.*]] = enum $Optional<Builtin.RawPointer>, #Optional.some!enumelt.1, [[T3]]
// CHECK-NEXT: [[T4:%.*]] = tuple ([[RESULT_PTR]] : $Builtin.RawPointer, [[CALLBACK]] : $Optional<Builtin.RawPointer>)
// CHECK-NEXT: dealloc_stack [[TEMP]]
// CHECK-NEXT: end_borrow [[T0]] from %2
// CHECK-NEXT: return [[T4]]

// CHECK-LABEL: sil private [transparent] @_T017materializeForSet7DerivedCAA12AbstractableA2aDP19finalStoredFunction6ResultQzycvmytfU_TW :
// CHECK: bb0(%0 : $Builtin.RawPointer, %1 : $*Builtin.UnsafeValueBuffer, %2 : $*Derived, %3 : $@thick Derived.Type):
// CHECK-NEXT: [[T0:%.*]] = load_borrow %2 : $*Derived
// CHECK-NEXT: [[SELF:%.*]] = upcast [[T0]] : $Derived to $Base
// CHECK-NEXT: [[RESULT_ADDR:%.*]] = pointer_to_address %0 : $Builtin.RawPointer to [strict] $*@callee_guaranteed () -> @out Int
// CHECK-NEXT: [[VALUE:%.*]] = load [take] [[RESULT_ADDR]] : $*@callee_guaranteed () -> @out Int
// CHECK-NEXT: // function_ref
// CHECK-NEXT: [[REABSTRACTOR:%.*]] = function_ref @_T0SiIegr_SiIegd_TR : $@convention(thin) (@guaranteed @callee_guaranteed () -> @out Int) -> Int
// CHECK-NEXT: [[NEWVALUE:%.*]] = partial_apply [callee_guaranteed] [[REABSTRACTOR]]([[VALUE]])
// CHECK-NEXT: [[ADDR:%.*]] = ref_element_addr [[SELF]] : $Base, #Base.finalStoredFunction
// CHECK-NEXT: [[WRITE:%.*]] = begin_access [modify] [dynamic] [[ADDR]] : $*@callee_guaranteed () -> Int
// CHECK-NEXT: assign [[NEWVALUE]] to [[WRITE]]
// CHECK-NEXT: end_access [[WRITE]] : $*@callee_guaranteed () -> Int
// CHECK-NEXT: end_borrow [[T0]] from %2
// CHECK-NEXT: tuple ()
// CHECK-NEXT: return

// UNCHECKED-LABEL: sil private [transparent] @_T017materializeForSet7DerivedCAA12AbstractableA2aDP19finalStoredFunction6ResultQzycvmytfU_TW :
// UNCHECKED: bb0(%0 : $Builtin.RawPointer, %1 : $*Builtin.UnsafeValueBuffer, %2 : $*Derived, %3 : $@thick Derived.Type):
// UNCHECKED-NEXT: [[T0:%.*]] = load_borrow %2 : $*Derived
// UNCHECKED-NEXT: [[SELF:%.*]] = upcast [[T0]] : $Derived to $Base
// UNCHECKED-NEXT: [[RESULT_ADDR:%.*]] = pointer_to_address %0 : $Builtin.RawPointer to [strict] $*@callee_guaranteed () -> @out Int
// UNCHECKED-NEXT: [[VALUE:%.*]] = load [take] [[RESULT_ADDR]] : $*@callee_guaranteed () -> @out Int
// UNCHECKED-NEXT: // function_ref
// UNCHECKED-NEXT: [[REABSTRACTOR:%.*]] = function_ref @_T0SiIegr_SiIegd_TR : $@convention(thin) (@guaranteed @callee_guaranteed () -> @out Int) -> Int
// UNCHECKED-NEXT: [[NEWVALUE:%.*]] = partial_apply [callee_guaranteed] [[REABSTRACTOR]]([[VALUE]])
// UNCHECKED-NEXT: [[ADDR:%.*]] = ref_element_addr [[SELF]] : $Base, #Base.finalStoredFunction
// UNCHECKED-NEXT: assign [[NEWVALUE]] to [[ADDR]]
// UNCHECKED-NEXT: end_borrow [[T0]] from %2
// UNCHECKED-NEXT: tuple ()
// UNCHECKED-NEXT: return

// CHECK-LABEL: sil private [transparent] [thunk] @_T017materializeForSet7DerivedCAA12AbstractableA2aDP19finalStoredFunction{{[_0-9a-zA-Z]*}}vmTW
// CHECK: bb0(%0 : $Builtin.RawPointer, %1 : $*Builtin.UnsafeValueBuffer, %2 : $*Derived):
// CHECK-NEXT: [[RESULT_ADDR:%.*]] = pointer_to_address %0 : $Builtin.RawPointer to [strict] $*@callee_guaranteed () -> @out Int
// CHECK-NEXT: [[T0:%.*]] = load_borrow %2 : $*Derived
// CHECK-NEXT: [[SELF:%.*]] = upcast [[T0]] : $Derived to $Base
// CHECK-NEXT: [[ADDR:%.*]] = ref_element_addr [[SELF]] : $Base, #Base.finalStoredFunction
// CHECK-NEXT: [[READ:%.*]] = begin_access [read] [dynamic] [[ADDR]] : $*@callee_guaranteed () -> Int
// CHECK-NEXT: [[RESULT:%.*]] = load [copy] [[READ]]
// CHECK-NEXT: function_ref
// CHECK-NEXT: [[REABSTRACTOR:%.*]] = function_ref @_T0SiIegd_SiIegr_TR : $@convention(thin) (@guaranteed @callee_guaranteed () -> Int) -> @out Int
// CHECK-NEXT: [[T1:%.*]] = partial_apply [callee_guaranteed] [[REABSTRACTOR]]([[RESULT]])
// CHECK-NEXT: end_access [[READ]] : $*@callee_guaranteed () -> Int
// CHECK-NEXT: store [[T1]] to [init] [[RESULT_ADDR]]
// CHECK-NEXT: [[RESULT_PTR:%.*]] = address_to_pointer [[RESULT_ADDR]] : $*@callee_guaranteed () -> @out Int to $Builtin.RawPointer
// CHECK-NEXT: function_ref
// CHECK-NEXT: [[T2:%.*]] = function_ref @_T017materializeForSet7DerivedCAA12AbstractableA2aDP19finalStoredFunction6ResultQzycvmytfU_TW
// CHECK-NEXT: [[T3:%.*]] = thin_function_to_pointer [[T2]]
// CHECK-NEXT: [[CALLBACK:%.*]] = enum $Optional<Builtin.RawPointer>, #Optional.some!enumelt.1, [[T3]]
// CHECK-NEXT: [[T4:%.*]] = tuple ([[RESULT_PTR]] : $Builtin.RawPointer, [[CALLBACK]] : $Optional<Builtin.RawPointer>)
// CHECK-NEXT: end_borrow [[T0]] from %2
// CHECK-NEXT: return [[T4]]

// UNCHECKED-LABEL: sil private [transparent] [thunk] @_T017materializeForSet7DerivedCAA12AbstractableA2aDP19finalStoredFunction{{[_0-9a-zA-Z]*}}vmTW
// UNCHECKED: bb0(%0 : $Builtin.RawPointer, %1 : $*Builtin.UnsafeValueBuffer, %2 : $*Derived):
// UNCHECKED-NEXT: [[RESULT_ADDR:%.*]] = pointer_to_address %0 : $Builtin.RawPointer to [strict] $*@callee_guaranteed () -> @out Int
// UNCHECKED-NEXT: [[T0:%.*]] = load_borrow %2 : $*Derived
// UNCHECKED-NEXT: [[SELF:%.*]] = upcast [[T0]] : $Derived to $Base
// UNCHECKED-NEXT: [[ADDR:%.*]] = ref_element_addr [[SELF]] : $Base, #Base.finalStoredFunction
// UNCHECKED-NEXT: [[RESULT:%.*]] = load [copy] [[ADDR]]
// UNCHECKED-NEXT: function_ref
// UNCHECKED-NEXT: [[REABSTRACTOR:%.*]] = function_ref @_T0SiIegd_SiIegr_TR : $@convention(thin) (@guaranteed @callee_guaranteed () -> Int) -> @out Int
// UNCHECKED-NEXT: [[T1:%.*]] = partial_apply [callee_guaranteed] [[REABSTRACTOR]]([[RESULT]])
// UNCHECKED-NEXT: store [[T1]] to [init] [[RESULT_ADDR]]
// UNCHECKED-NEXT: [[RESULT_PTR:%.*]] = address_to_pointer [[RESULT_ADDR]] : $*@callee_guaranteed () -> @out Int to $Builtin.RawPointer
// UNCHECKED-NEXT: function_ref
// UNCHECKED-NEXT: [[T2:%.*]] = function_ref @_T017materializeForSet7DerivedCAA12AbstractableA2aDP19finalStoredFunction6ResultQzycvmytfU_TW
// UNCHECKED-NEXT: [[T3:%.*]] = thin_function_to_pointer [[T2]]
// UNCHECKED-NEXT: [[CALLBACK:%.*]] = enum $Optional<Builtin.RawPointer>, #Optional.some!enumelt.1, [[T3]]
// UNCHECKED-NEXT: [[T4:%.*]] = tuple ([[RESULT_PTR]] : $Builtin.RawPointer, [[CALLBACK]] : $Optional<Builtin.RawPointer>)
// UNCHECKED-NEXT: end_borrow [[T0]] from %2
// UNCHECKED-NEXT: return [[T4]]

// CHECK-LABEL: sil private [transparent] @_T017materializeForSet7DerivedCAA12AbstractableA2aDP14staticFunction6ResultQzycvmZytfU_TW
// CHECK: bb0([[ARG1:%.*]] : $Builtin.RawPointer, [[ARG2:%.*]] : $*Builtin.UnsafeValueBuffer, [[ARG3:%.*]] : $*@thick Derived.Type, [[ARG4:%.*]] : $@thick Derived.Type.Type):
// CHECK-NEXT: [[SELF:%.*]] = load [trivial] [[ARG3]] : $*@thick Derived.Type
// CHECK-NEXT: [[BASE_SELF:%.*]] = upcast [[SELF]] : $@thick Derived.Type to $@thick Base.Type
// CHECK-NEXT: [[BUFFER:%.*]] = pointer_to_address [[ARG1]] : $Builtin.RawPointer to [strict] $*@callee_guaranteed () -> @out Int
// CHECK-NEXT: [[VALUE:%.*]] = load [take] [[BUFFER]] : $*@callee_guaranteed () -> @out Int
// CHECK:      [[REABSTRACTOR:%.*]] = function_ref @_T0SiIegr_SiIegd_TR : $@convention(thin) (@guaranteed @callee_guaranteed () -> @out Int) -> Int
// CHECK-NEXT: [[NEWVALUE:%.*]] = partial_apply [callee_guaranteed] [[REABSTRACTOR]]([[VALUE]]) : $@convention(thin) (@guaranteed @callee_guaranteed () -> @out Int) -> Int
// CHECK:      [[SETTER_FN:%.*]] = function_ref @_T017materializeForSet4BaseC14staticFunctionSiycvsZ : $@convention(method) (@owned @callee_guaranteed () -> Int, @thick Base.Type) -> ()
// CHECK-NEXT: apply [[SETTER_FN]]([[NEWVALUE]], [[BASE_SELF]]) : $@convention(method) (@owned @callee_guaranteed () -> Int, @thick Base.Type) -> ()
// CHECK-NEXT: [[RESULT:%.*]] = tuple ()
// CHECK-NEXT: return [[RESULT]] : $()

// CHECK-LABEL: sil private [transparent] [thunk] @_T017materializeForSet7DerivedCAA12AbstractableA2aDP14staticFunction6ResultQzycvmZTW
// CHECK: bb0(%0 : $Builtin.RawPointer, %1 : $*Builtin.UnsafeValueBuffer, %2 : $@thick Derived.Type):
// CHECK-NEXT: [[RESULT_ADDR:%.*]] = pointer_to_address %0 : $Builtin.RawPointer to [strict] $*@callee_guaranteed () -> @out Int
// CHECK-NEXT: [[SELF:%.*]] = upcast %2 : $@thick Derived.Type to $@thick Base.Type
// CHECK-NEXT: [[OUT:%.*]] = alloc_stack $@callee_guaranteed () -> Int
// CHECK:      [[GETTER:%.*]] = function_ref @_T017materializeForSet4BaseC14staticFunctionSiycvgZ : $@convention(method) (@thick Base.Type) -> @owned @callee_guaranteed () -> Int
// CHECK-NEXT: [[VALUE:%.*]] = apply [[GETTER]]([[SELF]]) : $@convention(method) (@thick Base.Type) -> @owned @callee_guaranteed () -> Int
// CHECK-NEXT: store [[VALUE]] to [init] [[OUT]] : $*@callee_guaranteed () -> Int
// CHECK-NEXT: [[VALUE:%.*]] = load [copy] [[OUT]] : $*@callee_guaranteed () -> Int
// CHECK:      [[REABSTRACTOR:%.*]] = function_ref @_T0SiIegd_SiIegr_TR : $@convention(thin) (@guaranteed @callee_guaranteed () -> Int) -> @out Int
// CHECK-NEXT: [[NEWVALUE:%.*]] = partial_apply [callee_guaranteed] [[REABSTRACTOR]]([[VALUE]])
// CHECK-NEXT: destroy_addr [[OUT]] : $*@callee_guaranteed () -> Int
// CHECK-NEXT: store [[NEWVALUE]] to [init] [[RESULT_ADDR]] : $*@callee_guaranteed () -> @out Int
// CHECK-NEXT: [[ADDR:%.*]] = address_to_pointer [[RESULT_ADDR]] : $*@callee_guaranteed () -> @out Int to $Builtin.RawPointer
// CHECK:      [[CALLBACK_FN:%.*]] = function_ref @_T017materializeForSet7DerivedCAA12AbstractableA2aDP14staticFunction6ResultQzycvmZytfU_TW : $@convention(witness_method: Abstractable) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout @thick Derived.Type, @thick Derived.Type.Type) -> ()
// CHECK-NEXT: [[CALLBACK_ADDR:%.*]] = thin_function_to_pointer [[CALLBACK_FN]] : $@convention(witness_method: Abstractable) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout @thick Derived.Type, @thick Derived.Type.Type) -> () to $Builtin.RawPointer
// CHECK-NEXT: [[CALLBACK:%.*]] = enum $Optional<Builtin.RawPointer>, #Optional.some!enumelt.1, [[CALLBACK_ADDR]] : $Builtin.RawPointer
// CHECK-NEXT: [[RESULT:%.*]] = tuple ([[ADDR]] : $Builtin.RawPointer, [[CALLBACK]] : $Optional<Builtin.RawPointer>)
// CHECK-NEXT: dealloc_stack [[OUT]] : $*@callee_guaranteed () -> Int
// CHECK-NEXT: return [[RESULT]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>)

protocol ClassAbstractable : class {
  associatedtype Result
  var storedFunction: () -> Result { get set }
  var finalStoredFunction: () -> Result { get set }
  var computedFunction: () -> Result { get set }
  static var staticFunction: () -> Result { get set }
}

extension Derived : ClassAbstractable {}

protocol Signatures {
  associatedtype Result
  var computedFunction: () -> Result { get set }
}
protocol Implementations {}
extension Implementations {
  var computedFunction: () -> Int {
    get { return {0} }
    set {}
  }
}

class ImplementingClass : Implementations, Signatures {}
struct ImplementingStruct : Implementations, Signatures {
  var ref: ImplementingClass?
}

class HasDidSet : Base {
  override var stored: Int {
    didSet {}
  }

// CHECK-LABEL: sil hidden [transparent] @_T017materializeForSet06HasDidC0C6storedSivm : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @guaranteed HasDidSet) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// CHECK: bb0([[BUFFER:%.*]] : $Builtin.RawPointer, [[STORAGE:%.*]] : $*Builtin.UnsafeValueBuffer, [[SELF:%.*]] : $HasDidSet):
// CHECK:   [[T2:%.*]] = pointer_to_address [[BUFFER]] : $Builtin.RawPointer to [strict] $*Int
// CHECK:   [[T0:%.*]] = function_ref @_T017materializeForSet06HasDidC0C6storedSivg
// CHECK:   [[T1:%.*]] = apply [[T0]]([[SELF]])
// CHECK:   store [[T1]] to [trivial] [[T2]] : $*Int
// CHECK:   [[BUFFER:%.*]] = address_to_pointer [[T2]]
// CHECK:   [[CALLBACK_FN:%.*]] = function_ref @_T017materializeForSet06HasDidC0C6storedSivmytfU_ : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout HasDidSet, @thick HasDidSet.Type) -> ()
// CHECK:   [[CALLBACK_ADDR:%.*]] = thin_function_to_pointer [[CALLBACK_FN]]
// CHECK:   [[CALLBACK:%.*]] = enum $Optional<Builtin.RawPointer>, #Optional.some!enumelt.1, [[CALLBACK_ADDR]]
// CHECK:   [[T4:%.*]] = tuple ([[BUFFER]] : $Builtin.RawPointer, [[CALLBACK]] : $Optional<Builtin.RawPointer>)
// CHECK:   return [[T4]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>)
// CHECK: }

  override var computed: Int {
    get { return 0 }
    set(value) {}
  }

// CHECK-LABEL: sil hidden [transparent] @_T017materializeForSet06HasDidC0C8computedSivm : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @guaranteed HasDidSet) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// CHECK: bb0([[BUFFER:%.*]] : $Builtin.RawPointer, [[STORAGE:%.*]] : $*Builtin.UnsafeValueBuffer, [[SELF:%.*]] : $HasDidSet):
// CHECK:   [[T2:%.*]] = pointer_to_address [[BUFFER]] : $Builtin.RawPointer to [strict] $*Int
// CHECK:   [[T0:%.*]] = function_ref @_T017materializeForSet06HasDidC0C8computedSivg
// CHECK:   [[T1:%.*]] = apply [[T0]]([[SELF]])
// CHECK:   store [[T1]] to [trivial] [[T2]] : $*Int
// CHECK:   [[BUFFER:%.*]] = address_to_pointer [[T2]]
// CHECK:   [[CALLBACK_FN:%.*]] = function_ref @_T017materializeForSet06HasDidC0C8computedSivmytfU_ : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout HasDidSet, @thick HasDidSet.Type) -> ()
// CHECK:   [[CALLBACK_ADDR:%.*]] = thin_function_to_pointer [[CALLBACK_FN]]
// CHECK:   [[CALLBACK:%.*]] = enum $Optional<Builtin.RawPointer>, #Optional.some!enumelt.1, [[CALLBACK_ADDR]]
// CHECK:   [[T4:%.*]] = tuple ([[BUFFER]] : $Builtin.RawPointer, [[CALLBACK]] : $Optional<Builtin.RawPointer>)
// CHECK:   return [[T4]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>)
// CHECK: }
}

class HasStoredDidSet {
  var stored: Int = 0 {
    didSet {}
  }

// CHECK-LABEL: sil private [transparent] @_T017materializeForSet012HasStoredDidC0C6storedSivmytfU_ : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout HasStoredDidSet, @thick HasStoredDidSet.Type) -> () {
// CHECK: bb0([[BUFFER:%.*]] : $Builtin.RawPointer, [[STORAGE:%.*]] : $*Builtin.UnsafeValueBuffer, [[SELF:%.*]] : $*HasStoredDidSet, [[METATYPE:%.*]] : $@thick HasStoredDidSet.Type):
// CHECK:   [[SELF_VALUE:%.*]] = load_borrow [[SELF]] : $*HasStoredDidSet
// CHECK:   [[BUFFER_ADDR:%.*]] = pointer_to_address [[BUFFER]] : $Builtin.RawPointer to [strict] $*Int
// CHECK:   [[VALUE:%.*]] = load [trivial] [[BUFFER_ADDR]] : $*Int
// CHECK:   [[SETTER_FN:%.*]] = function_ref @_T017materializeForSet012HasStoredDidC0C6storedSivs : $@convention(method) (Int, @guaranteed HasStoredDidSet) -> ()
// CHECK:   apply [[SETTER_FN]]([[VALUE]], [[SELF_VALUE]]) : $@convention(method) (Int, @guaranteed HasStoredDidSet) -> ()
// CHECK:   return
// CHECK: }

// CHECK-LABEL: sil hidden [transparent] @_T017materializeForSet012HasStoredDidC0C6storedSivm : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @guaranteed HasStoredDidSet) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// CHECK: bb0([[BUFFER:%.*]] : $Builtin.RawPointer, [[STORAGE:%.*]] : $*Builtin.UnsafeValueBuffer, [[SELF:%.*]] : $HasStoredDidSet):
// CHECK:   [[T2:%.*]] = pointer_to_address [[BUFFER]] : $Builtin.RawPointer to [strict] $*Int
// CHECK:   [[T0:%.*]] = function_ref @_T017materializeForSet012HasStoredDidC0C6storedSivg
// CHECK:   [[T1:%.*]] = apply [[T0]]([[SELF]])
// CHECK:   store [[T1]] to [trivial] [[T2]] : $*Int
// CHECK:   [[BUFFER:%.*]] = address_to_pointer [[T2]]
// CHECK:   [[CALLBACK_FN:%.*]] = function_ref @_T017materializeForSet012HasStoredDidC0C6storedSivmytfU_ : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout HasStoredDidSet, @thick HasStoredDidSet.Type) -> ()
// CHECK:   [[CALLBACK_ADDR:%.*]] = thin_function_to_pointer [[CALLBACK_FN]]
// CHECK:   [[CALLBACK:%.*]] = enum $Optional<Builtin.RawPointer>, #Optional.some!enumelt.1, [[CALLBACK_ADDR]]
// CHECK:   [[T4:%.*]] = tuple ([[BUFFER]] : $Builtin.RawPointer, [[CALLBACK]] : $Optional<Builtin.RawPointer>)
// CHECK:   return [[T4]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>)
// CHECK: }
}

class HasWeak {
  weak var weakvar: HasWeak?
}
// CHECK-LABEL: sil hidden [transparent] @_T017materializeForSet7HasWeakC7weakvarACSgXwvm : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @guaranteed HasWeak) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// CHECK: bb0([[BUFFER:%.*]] : $Builtin.RawPointer, [[STORAGE:%.*]] : $*Builtin.UnsafeValueBuffer, [[SELF:%.*]] : $HasWeak):
// CHECK:   [[T2:%.*]] = pointer_to_address [[BUFFER]] : $Builtin.RawPointer to [strict] $*Optional<HasWeak>
// CHECK:   [[T0:%.*]] = ref_element_addr [[SELF]] : $HasWeak, #HasWeak.weakvar
// CHECK:   [[READ:%.*]] = begin_access [read] [dynamic] [[T0]] : $*@sil_weak Optional<HasWeak>
// CHECK:   [[T1:%.*]] = load_weak [[READ]] : $*@sil_weak Optional<HasWeak>
// CHECK:   end_access [[READ]] : $*@sil_weak Optional<HasWeak>
// CHECK:   store [[T1]] to [init] [[T2]] : $*Optional<HasWeak>
// CHECK:   [[BUFFER:%.*]] = address_to_pointer [[T2]]
// CHECK:   [[T0:%.*]] = function_ref @_T017materializeForSet7HasWeakC7weakvarACSgXwvmytfU_ : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout HasWeak, @thick HasWeak.Type) -> () 
// CHECK:   [[T4:%.*]] = tuple ([[BUFFER]] : $Builtin.RawPointer, {{.*}} : $Optional<Builtin.RawPointer>)
// CHECK:   return [[T4]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>)
// CHECK: }

// UNCHECKED-LABEL: sil hidden [transparent] @_T017materializeForSet7HasWeakC7weakvarACSgXwvm : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @guaranteed HasWeak) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// UNCHECKED: bb0([[BUFFER:%.*]] : $Builtin.RawPointer, [[STORAGE:%.*]] : $*Builtin.UnsafeValueBuffer, [[SELF:%.*]] : $HasWeak):
// UNCHECKED:   [[T2:%.*]] = pointer_to_address [[BUFFER]] : $Builtin.RawPointer to [strict] $*Optional<HasWeak>
// UNCHECKED:   [[T0:%.*]] = ref_element_addr [[SELF]] : $HasWeak, #HasWeak.weakvar
// UNCHECKED:   [[T1:%.*]] = load_weak [[T0]] : $*@sil_weak Optional<HasWeak>
// UNCHECKED:   store [[T1]] to [init] [[T2]] : $*Optional<HasWeak>
// UNCHECKED:   [[BUFFER:%.*]] = address_to_pointer [[T2]]
// UNCHECKED:   [[T0:%.*]] = function_ref @_T017materializeForSet7HasWeakC7weakvarACSgXwvmytfU_ : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout HasWeak, @thick HasWeak.Type) -> () 
// UNCHECKED:   [[T4:%.*]] = tuple ([[BUFFER]] : $Builtin.RawPointer, {{.*}} : $Optional<Builtin.RawPointer>)
// UNCHECKED:   return [[T4]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>)
// UNCHECKED: }

// rdar://22109071
// Test that we don't use materializeForSet from a protocol extension.
protocol Magic {}
extension Magic {
  var hocus: Int {
    get { return 0 }
    set {}
  }
}
struct Wizard : Magic {}
func improve(_ x: inout Int) {}
func improveWizard(_ wizard: inout Wizard) {
  improve(&wizard.hocus)
}
// CHECK-LABEL: sil hidden @_T017materializeForSet13improveWizardyAA0E0VzF
// CHECK:       [[WRITE:%.*]] = begin_access [modify] [unknown] %0 : $*Wizard
// CHECK-NEXT:  [[TEMP:%.*]] = alloc_stack $Int
//   Call the getter and materialize the result in the temporary.
// CHECK-NEXT:  [[T0:%.*]] = load [trivial] [[WRITE:.*]] : $*Wizard
// CHECK:       [[WTEMP:%.*]] = alloc_stack $Wizard
// CHECK-NEXT:  store [[T0]] to [trivial] [[WTEMP]]
// CHECK:       [[GETTER:%.*]] = function_ref @_T017materializeForSet5MagicPAAE5hocusSivg
// CHECK-NEXT:  [[T0:%.*]] = apply [[GETTER]]<Wizard>([[WTEMP]])
// CHECK-NEXT:  dealloc_stack [[WTEMP]]
// CHECK-NEXT:  store [[T0]] to [trivial] [[TEMP]]
//   Call improve.
// CHECK:       [[IMPROVE:%.*]] = function_ref @_T017materializeForSet7improveySizF :
// CHECK-NEXT:  apply [[IMPROVE]]([[TEMP]])
// CHECK-NEXT:  [[T0:%.*]] = load [trivial] [[TEMP]]
// CHECK:       [[SETTER:%.*]] = function_ref @_T017materializeForSet5MagicPAAE5hocusSivs
// CHECK-NEXT:  apply [[SETTER]]<Wizard>([[T0]], [[WRITE]])
// CHECK-NEXT:  end_access [[WRITE]] : $*Wizard
// CHECK-NEXT:  dealloc_stack [[TEMP]]

protocol Totalled {
  var total: Int { get set }
}

struct Bill : Totalled {
  var total: Int
}

// CHECK-LABEL: sil hidden [transparent] @_T017materializeForSet4BillV5totalSivm : $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout Bill) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// CHECK: bb0([[BUFFER:%.*]] : $Builtin.RawPointer, [[STORAGE:%.*]] : $*Builtin.UnsafeValueBuffer, [[SELF:%.*]] : $*Bill):
// CHECK:   [[T0:%.*]] = struct_element_addr [[SELF]] : $*Bill, #Bill.total
// CHECK:   [[T1:%.*]] = address_to_pointer [[T0]] : $*Int to $Builtin.RawPointer
// CHECK:   [[T3:%.*]] = enum $Optional<Builtin.RawPointer>, #Optional.none!enumelt
// CHECK:   [[T4:%.*]] = tuple ([[T1]] : $Builtin.RawPointer, [[T3]] : $Optional<Builtin.RawPointer>)
// CHECK:   return [[T4]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>)
// CHECK: }

// CHECK-LABEL:  sil private [transparent] [thunk] @_T017materializeForSet4BillVAA8TotalledA2aDP5totalSivmTW : $@convention(witness_method: Totalled) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout Bill) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// CHECK:        bb0([[BUFFER:%.*]] : $Builtin.RawPointer, [[STORAGE:%.*]] : $*Builtin.UnsafeValueBuffer, [[SELF:%.*]] : $*Bill):
// CHECK:          [[T0:%.*]] = function_ref @_T017materializeForSet4BillV5totalSivm
// CHECK-NEXT:     [[T1:%.*]] = apply [[T0]]([[BUFFER]], [[STORAGE]], [[SELF]])
// CHECK-NEXT:     [[LEFT:%.*]] = tuple_extract [[T1]]
// CHECK-NEXT:     [[RIGHT:%.*]] = tuple_extract [[T1]]
// CHECK-NEXT:     [[T1:%.*]] = tuple ([[LEFT]] : $Builtin.RawPointer, [[RIGHT]] : $Optional<Builtin.RawPointer>)
// CHECK-NEXT:     return [[T1]] :

protocol AddressOnlySubscript {
  associatedtype Index
  subscript(i: Index) -> Index { get set }
}

struct Foo<T>: AddressOnlySubscript {
  subscript(i: T) -> T {
    get { return i }
    set { print("\(i) = \(newValue)") }
  }
}

func increment(_ x: inout Int) { x += 1 }

// Generic subscripts.

protocol GenericSubscriptProtocol {
  subscript<T>(_: T) -> T { get set }
}

struct GenericSubscriptWitness : GenericSubscriptProtocol {
  subscript<T>(_: T) -> T { get { } set { } }
}

// CHECK-LABEL: sil private [transparent] @_T017materializeForSet23GenericSubscriptWitnessVxxcluimytfU_ : $@convention(method) <T> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout GenericSubscriptWitness, @thick GenericSubscriptWitness.Type) -> () {
// CHECK:       bb0(%0 : $Builtin.RawPointer, %1 : $*Builtin.UnsafeValueBuffer, %2 : $*GenericSubscriptWitness, %3 : $@thick GenericSubscriptWitness.Type):
// CHECK:         [[BUFFER:%.*]] = project_value_buffer $T in %1 : $*Builtin.UnsafeValueBuffer
// CHECK-NEXT:    [[INDICES:%.*]] = pointer_to_address %0 : $Builtin.RawPointer to [strict] $*T
// CHECK:         [[SETTER:%.*]] = function_ref @_T017materializeForSet23GenericSubscriptWitnessVxxcluis : $@convention(method) <τ_0_0> (@in τ_0_0, @in τ_0_0, @inout GenericSubscriptWitness) -> ()
// CHECK-NEXT:    apply [[SETTER]]<T>([[INDICES]], [[BUFFER]], %2) : $@convention(method) <τ_0_0> (@in τ_0_0, @in τ_0_0, @inout GenericSubscriptWitness) -> ()
// CHECK-NEXT:    dealloc_value_buffer $*T in %1 : $*Builtin.UnsafeValueBuffer
// CHECK-NEXT:    [[RESULT:%.*]] = tuple ()
// CHECK-NEXT:    return [[RESULT]] : $()

// CHECK-LABEL: sil hidden [transparent] @_T017materializeForSet23GenericSubscriptWitnessVxxcluim : $@convention(method) <T> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @in T, @inout GenericSubscriptWitness) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>) {
// CHECK:      bb0(%0 : $Builtin.RawPointer, %1 : $*Builtin.UnsafeValueBuffer, %2 : $*T, %3 : $*GenericSubscriptWitness):
// CHECK-NEXT:   [[BUFFER:%.*]] = alloc_value_buffer $T in %1 : $*Builtin.UnsafeValueBuffer
// CHECK-NEXT:   copy_addr %2 to [initialization] [[BUFFER]] : $*T
// CHECK-NEXT:   [[VALUE:%.*]] = pointer_to_address %0 : $Builtin.RawPointer to [strict] $*T
// CHECK-NEXT:   [[SELF:%.*]] = load [trivial] %3 : $*GenericSubscriptWitness
// CHECK:        [[GETTER:%.*]] = function_ref @_T017materializeForSet23GenericSubscriptWitnessVxxcluig : $@convention(method) <τ_0_0> (@in τ_0_0, GenericSubscriptWitness) -> @out τ_0_0
// CHECK-NEXT:   apply [[GETTER]]<T>([[VALUE]], %2, [[SELF]]) : $@convention(method) <τ_0_0> (@in τ_0_0, GenericSubscriptWitness) -> @out τ_0_0
// CHECK-NEXT:   [[VALUE_PTR:%.*]] = address_to_pointer [[VALUE]] : $*T to $Builtin.RawPointer
// CHECK:        [[CALLBACK:%.*]] = function_ref @_T017materializeForSet23GenericSubscriptWitnessVxxcluimytfU_
// CHECK-NEXT:   [[CALLBACK_PTR:%.*]] = thin_function_to_pointer [[CALLBACK]] : $@convention(method) <τ_0_0> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout GenericSubscriptWitness, @thick GenericSubscriptWitness.Type) -> () to $Builtin.RawPointer
// CHECK-NEXT:   [[CALLBACK_OPTIONAL:%.*]] = enum $Optional<Builtin.RawPointer>, #Optional.some!enumelt.1, [[CALLBACK_PTR]] : $Builtin.RawPointer
// CHECK-NEXT:   [[RESULT:%.*]] = tuple ([[VALUE_PTR]] : $Builtin.RawPointer, [[CALLBACK_OPTIONAL]] : $Optional<Builtin.RawPointer>)
// CHECK-NEXT:   return [[RESULT]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>)

extension GenericSubscriptProtocol {
  subscript<T>(t: T) -> T { get { } set { } }
}

struct GenericSubscriptDefaultWitness : GenericSubscriptProtocol { }

// Make sure we correctly infer the 'T : Magic' requirement on all the accessors
// of the subscript.

struct GenericTypeWithRequirement<T : Magic> {}

protocol InferredRequirementOnSubscriptProtocol {
  subscript<T>(i: Int) -> GenericTypeWithRequirement<T> { get set }
}

struct InferredRequirementOnSubscript : InferredRequirementOnSubscriptProtocol {
  subscript<T>(i: Int) -> GenericTypeWithRequirement<T> {
    get { }
    set { }
  }
}

// CHECK-LABEL: sil hidden @_T017materializeForSet30InferredRequirementOnSubscriptVAA015GenericTypeWithE0VyxGSicAA5MagicRzluig : $@convention(method) <T where T : Magic> (Int, InferredRequirementOnSubscript) -> GenericTypeWithRequirement<T>

// CHECK-LABEL: sil hidden @_T017materializeForSet30InferredRequirementOnSubscriptVAA015GenericTypeWithE0VyxGSicAA5MagicRzluis : $@convention(method) <T where T : Magic> (GenericTypeWithRequirement<T>, Int, @inout InferredRequirementOnSubscript) -> ()

// CHECK-LABEL: sil hidden [transparent] @_T017materializeForSet30InferredRequirementOnSubscriptVAA015GenericTypeWithE0VyxGSicAA5MagicRzluim : $@convention(method) <T where T : Magic> (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, Int, @inout InferredRequirementOnSubscript) -> (Builtin.RawPointer, Optional<Builtin.RawPointer>)

// Test for materializeForSet vs static properties of structs.

protocol Beverage {
  static var abv: Int { get set }
}

struct Beer : Beverage {
  static var abv: Int {
    get {
      return 7
    }
    set { }
  }
}

struct Wine<Color> : Beverage {
  static var abv: Int {
    get {
      return 14
    }
    set { }
  }
}

// Make sure we can perform an inout access of such a property too.

func inoutAccessOfStaticProperty<T : Beverage>(_ t: T.Type) {
  increment(&t.abv)
}

// Test for materializeForSet vs overridden computed property of classes.
class BaseForOverride {
  var valueStored: Int
  var valueComputed: Int { get { } set { } }

  init(valueStored: Int) {
    self.valueStored = valueStored
  }
}

class DerivedForOverride : BaseForOverride {
  override var valueStored: Int { get { } set { } }
  override var valueComputed: Int { get { } set { } }
}

// Test for materializeForSet vs static properties of classes.

class ReferenceBeer {
  class var abv: Int {
    get {
      return 7
    }
    set { }
  }
}

func inoutAccessOfClassProperty() {
  increment(&ReferenceBeer.abv)
}

// Test for materializeForSet when Self is re-abstracted.
//
// We have to open-code the materializeForSelf witness, and not screw up
// the re-abstraction.

protocol Panda {
  var x: (Self) -> Self { get set }
}

func id<T>(_ t: T) -> T { return t }

extension Panda {
  var x: (Self) -> Self {
    get { return id }
    set { }
  }
}

struct TuxedoPanda : Panda { }

// CHECK-LABEL: sil private [transparent] @_T017materializeForSet11TuxedoPandaVAA0E0A2aDP1xxxcvmytfU_TW : $@convention(witness_method: Panda) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout TuxedoPanda, @thick TuxedoPanda.Type) -> ()

  // FIXME: Useless re-abstractions

  // CHECK: function_ref @_T017materializeForSet11TuxedoPandaVACIegir_A2CIegyd_TR : $@convention(thin) (TuxedoPanda, @guaranteed @callee_guaranteed (@in TuxedoPanda) -> @out TuxedoPanda) -> TuxedoPanda

  // CHECK: function_ref @_T017materializeForSet11TuxedoPandaVACIegyd_A2CIegir_TR : $@convention(thin) (@in TuxedoPanda, @guaranteed @callee_guaranteed (TuxedoPanda) -> TuxedoPanda) -> @out TuxedoPanda

  // CHECK: function_ref @_T017materializeForSet5PandaPAAE1xxxcvs : $@convention(method) <τ_0_0 where τ_0_0 : Panda> (@owned @callee_guaranteed (@in τ_0_0) -> @out τ_0_0, @inout τ_0_0) -> ()

// CHECK: }

// CHECK-LABEL: sil private [transparent] [thunk] @_T017materializeForSet11TuxedoPandaVAA0E0A2aDP1xxxcvmTW

// Call the getter:

  // CHECK: function_ref @_T017materializeForSet5PandaPAAE1xxxcvg : $@convention(method) <τ_0_0 where τ_0_0 : Panda> (@in_guaranteed τ_0_0) -> @owned @callee_guaranteed (@in τ_0_0) -> @out τ_0_0

// Result of calling the getter is re-abstracted to the maximally substituted type
// by SILGenFunction::emitApply():

  // CHECK: function_ref @_T017materializeForSet11TuxedoPandaVACIegir_A2CIegyd_TR : $@convention(thin) (TuxedoPanda, @guaranteed @callee_guaranteed (@in TuxedoPanda) -> @out TuxedoPanda) -> TuxedoPanda

// ... then we re-abstract to the requirement signature:
// FIXME: Peephole this away with the previous one since there's actually no
// abstraction change in this case.

  // CHECK: function_ref @_T017materializeForSet11TuxedoPandaVACIegyd_A2CIegir_TR : $@convention(thin) (@in TuxedoPanda, @guaranteed @callee_guaranteed (TuxedoPanda) -> TuxedoPanda) -> @out TuxedoPanda

// The callback:

  // CHECK: function_ref @_T017materializeForSet11TuxedoPandaVAA0E0A2aDP1xxxcvmytfU_TW : $@convention(witness_method: Panda) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout TuxedoPanda, @thick TuxedoPanda.Type) -> ()

// CHECK: }


// Test for materializeForSet vs lazy properties of structs.

struct LazyStructProperty {
  lazy var cat: Int = 5
}

// CHECK-LABEL: sil hidden @_T017materializeForSet31inoutAccessOfLazyStructPropertyyAA0ghI0Vz1l_tF
// CHECK:   function_ref @_T017materializeForSet18LazyStructPropertyV3catSivg
// CHECK:   function_ref @_T017materializeForSet18LazyStructPropertyV3catSivs
func inoutAccessOfLazyStructProperty(l: inout LazyStructProperty) {
  increment(&l.cat)
}

// Test for materializeForSet vs lazy properties of classes.

// CHECK-LABEL: sil hidden [transparent] @_T017materializeForSet17LazyClassPropertyC3catSivm

class LazyClassProperty {
  lazy var cat: Int = 5
}

// CHECK-LABEL: sil hidden @_T017materializeForSet30inoutAccessOfLazyClassPropertyyAA0ghI0Cz1l_tF
// CHECK:    class_method {{.*}} : $LazyClassProperty, #LazyClassProperty.cat!materializeForSet.1
func inoutAccessOfLazyClassProperty(l: inout LazyClassProperty) {
  increment(&l.cat)
}

// Test for materializeForSet vs lazy properties of final classes.

final class LazyFinalClassProperty {
  lazy var cat: Int = 5
}

// CHECK-LABEL: sil hidden @_T017materializeForSet35inoutAccessOfLazyFinalClassPropertyyAA0ghiJ0Cz1l_tF
// CHECK:    function_ref @_T017materializeForSet22LazyFinalClassPropertyC3catSivg
// CHECK:    function_ref @_T017materializeForSet22LazyFinalClassPropertyC3catSivs
func inoutAccessOfLazyFinalClassProperty(l: inout LazyFinalClassProperty) {
  increment(&l.cat)
}

// Make sure the below doesn't crash SILGen
struct FooClosure {
 var computed: (((Int) -> Int) -> Int)? {
   get { return stored }
   set {}
 }
 var stored: (((Int) -> Int) -> Int)? = nil
}

// CHECK-LABEL: _T017materializeForSet22testMaterializedSetteryyF
func testMaterializedSetter() {
  // CHECK: function_ref @_T017materializeForSet10FooClosureVACycfC
  var f = FooClosure()
  // CHECK: function_ref @_T017materializeForSet10FooClosureV8computedS3iccSgvg
  // CHECK: function_ref @_T017materializeForSet10FooClosureV8computedS3iccSgvs
  f.computed = f.computed
}

// CHECK-LABEL: sil_vtable DerivedForOverride {
// CHECK:   #BaseForOverride.valueStored!getter.1: (BaseForOverride) -> () -> Int : _T017materializeForSet07DerivedB8OverrideC11valueStoredSivg
// CHECK:   #BaseForOverride.valueStored!setter.1: (BaseForOverride) -> (Int) -> () : _T017materializeForSet07DerivedB8OverrideC11valueStoredSivs
// CHECK:   #BaseForOverride.valueStored!materializeForSet.1: (BaseForOverride) -> (Builtin.RawPointer, inout Builtin.UnsafeValueBuffer) -> (Builtin.RawPointer, Builtin.RawPointer?) : _T017materializeForSet07DerivedB8OverrideC11valueStoredSivm
// CHECK:   #BaseForOverride.valueComputed!getter.1: (BaseForOverride) -> () -> Int : _T017materializeForSet07DerivedB8OverrideC13valueComputedSivg
// CHECK:   #BaseForOverride.valueComputed!setter.1: (BaseForOverride) -> (Int) -> () : _T017materializeForSet07DerivedB8OverrideC13valueComputedSivs
// CHECK:   #BaseForOverride.valueComputed!materializeForSet.1: (BaseForOverride) -> (Builtin.RawPointer, inout Builtin.UnsafeValueBuffer) -> (Builtin.RawPointer, Builtin.RawPointer?) : _T017materializeForSet07DerivedB8OverrideC13valueComputedSivm
// CHECK: }

// CHECK-LABEL: sil_witness_table hidden Bill: Totalled module materializeForSet {
// CHECK:   method #Totalled.total!getter.1: {{.*}} : @_T017materializeForSet4BillVAA8TotalledA2aDP5totalSivgTW
// CHECK:   method #Totalled.total!setter.1: {{.*}} : @_T017materializeForSet4BillVAA8TotalledA2aDP5totalSivsTW
// CHECK:   method #Totalled.total!materializeForSet.1: {{.*}} : @_T017materializeForSet4BillVAA8TotalledA2aDP5totalSivmTW
// CHECK: }
