// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -parse-as-library -emit-silgen -disable-objc-attr-requires-foundation-module %s | %FileCheck %s

var zero: Int = 0

func use(_: Int) {}
func use(_: Double) {}
func getInt() -> Int { return zero }

// CHECK-LABEL: sil hidden @{{.*}}physical_tuple_lvalue
// CHECK: bb0(%0 : $Int):
func physical_tuple_lvalue(_ c: Int) {
  var x : (Int, Int)
  // CHECK: [[BOX:%[0-9]+]] = alloc_box ${ var (Int, Int) }
  // CHECK: [[MARKED_BOX:%[0-9]+]] = mark_uninitialized [var] [[BOX]]
  // CHECK: [[XADDR:%.*]] = project_box [[MARKED_BOX]]
  x.1 = c
  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown] [[XADDR]]
  // CHECK: [[X_1:%[0-9]+]] = tuple_element_addr [[WRITE]] : {{.*}}, 1
  // CHECK: assign %0 to [[X_1]]
}

func tuple_rvalue() -> (Int, Int) {}

// CHECK-LABEL: sil hidden @{{.*}}physical_tuple_rvalue
func physical_tuple_rvalue() -> Int {
  return tuple_rvalue().1
  // CHECK: [[FUNC:%[0-9]+]] = function_ref @_T010properties12tuple_rvalue{{[_0-9a-zA-Z]*}}F
  // CHECK: [[TUPLE:%[0-9]+]] = apply [[FUNC]]()
  // CHECK: [[RET:%[0-9]+]] = tuple_extract [[TUPLE]] : {{.*}}, 1
  // CHECK: return [[RET]]
}

// CHECK-LABEL: sil hidden @_T010properties16tuple_assignment{{[_0-9a-zA-Z]*}}F
func tuple_assignment(_ a: inout Int, b: inout Int) {
  // CHECK: bb0([[A_ADDR:%[0-9]+]] : $*Int, [[B_ADDR:%[0-9]+]] : $*Int):
  // CHECK: [[READ:%.*]] = begin_access [read] [unknown] [[B_ADDR]]
  // CHECK: [[B:%[0-9]+]] = load [trivial] [[READ]]
  // CHECK: [[READ:%.*]] = begin_access [read] [unknown] [[A_ADDR]]
  // CHECK: [[A:%[0-9]+]] = load [trivial] [[READ]]
  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown] [[A_ADDR]]
  // CHECK: assign [[B]] to [[WRITE]]
  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown] [[B_ADDR]]
  // CHECK: assign [[A]] to [[WRITE]]
  (a, b) = (b, a)
}

// CHECK-LABEL: sil hidden @_T010properties18tuple_assignment_2{{[_0-9a-zA-Z]*}}F
func tuple_assignment_2(_ a: inout Int, b: inout Int, xy: (Int, Int)) {
  // CHECK: bb0([[A_ADDR:%[0-9]+]] : $*Int, [[B_ADDR:%[0-9]+]] : $*Int, [[X:%[0-9]+]] : $Int, [[Y:%[0-9]+]] : $Int):
  (a, b) = xy
  // CHECK: [[XY2:%[0-9]+]] = tuple ([[X]] : $Int, [[Y]] : $Int)
  // CHECK: [[X:%[0-9]+]] = tuple_extract [[XY2]] : {{.*}}, 0
  // CHECK: [[Y:%[0-9]+]] = tuple_extract [[XY2]] : {{.*}}, 1
  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown] [[A_ADDR]]
  // CHECK: assign [[X]] to [[WRITE]]
  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown] [[B_ADDR]]
  // CHECK: assign [[Y]] to [[WRITE]]
}

class Ref {
  var x, y : Int
  var ref : Ref

  var z: Int { get {} set {} }

  var val_prop: Val { get {} set {} }

  subscript(i: Int) -> Float { get {} set {} }

  init(i: Int) {
    x = i
    y = i
    ref = self
  }
}

class RefSubclass : Ref {
  var w : Int

  override init (i: Int) {
    w = i
    super.init(i: i)
  }
}

struct Val {
  var x, y : Int
  var ref : Ref

  var z: Int { get {} set {} }

  var z_tuple: (Int, Int) { get {} set {} }

  subscript(i: Int) -> Float { get {} set {} }
}

// CHECK-LABEL: sil hidden @_T010properties22physical_struct_lvalue{{[_0-9a-zA-Z]*}}F
func physical_struct_lvalue(_ c: Int) {
  var v : Val
  // CHECK: [[VADDR:%[0-9]+]] = alloc_box ${ var Val }
  v.y = c
  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown]
  // CHECK: [[YADDR:%.*]] = struct_element_addr [[WRITE]]
  // CHECK: assign %0 to [[YADDR]]
}

// CHECK-LABEL: sil hidden @_T010properties21physical_class_lvalue{{[_0-9a-zA-Z]*}}F : $@convention(thin) (@owned Ref, Int) -> ()
// CHECK: bb0([[ARG0:%.*]] : $Ref,
 func physical_class_lvalue(_ r: Ref, a: Int) {
    r.y = a
   // CHECK: [[BORROWED_ARG0:%.*]] = begin_borrow [[ARG0]]
   // CHECK: [[FN:%[0-9]+]] = class_method [[BORROWED_ARG0]] : $Ref, #Ref.y!setter.1
   // CHECK: apply [[FN]](%1, [[BORROWED_ARG0]]) : $@convention(method) (Int, @guaranteed Ref) -> ()
   // CHECK: end_borrow [[BORROWED_ARG0]] from [[ARG0]]
   // CHECK: destroy_value [[ARG0]] : $Ref
  }


// CHECK-LABEL: sil hidden @_T010properties24physical_subclass_lvalue{{[_0-9a-zA-Z]*}}F
func physical_subclass_lvalue(_ r: RefSubclass, a: Int) {
  // CHECK: bb0([[ARG1:%.*]] : $RefSubclass, [[ARG2:%.*]] : $Int):
  r.y = a
  // CHECK: [[BORROWED_ARG1:%.*]] = begin_borrow [[ARG1]]
  // CHECK: [[ARG1_COPY:%.*]] = copy_value [[BORROWED_ARG1]] : $RefSubclass
  // CHECK: [[R_SUP:%[0-9]+]] = upcast [[ARG1_COPY]] : $RefSubclass to $Ref
  // CHECK: [[FN:%[0-9]+]] = class_method [[R_SUP]] : $Ref, #Ref.y!setter.1 : (Ref) -> (Int) -> (), $@convention(method) (Int, @guaranteed Ref) -> ()
  // CHECK: apply [[FN]]([[ARG2]], [[R_SUP]]) :
  // CHECK: destroy_value [[R_SUP]]
  // CHECK: end_borrow [[BORROWED_ARG1]] from [[ARG1]]
  r.w = a

  // CHECK: [[BORROWED_ARG1:%.*]] = begin_borrow [[ARG1]]
  // CHECK: [[FN:%[0-9]+]] = class_method [[BORROWED_ARG1]] : $RefSubclass, #RefSubclass.w!setter.1
  // CHECK: apply [[FN]](%1, [[BORROWED_ARG1]]) : $@convention(method) (Int, @guaranteed RefSubclass) -> ()
  // CHECK: end_borrow [[BORROWED_ARG1]] from [[ARG1]]
  // CHECK: destroy_value [[ARG1]]
}
  


func struct_rvalue() -> Val {}

// CHECK-LABEL: sil hidden @_T010properties22physical_struct_rvalue{{[_0-9a-zA-Z]*}}F
func physical_struct_rvalue() -> Int {
  return struct_rvalue().y
  // CHECK: [[FUNC:%[0-9]+]] = function_ref @_T010properties13struct_rvalueAA3ValVyF
  // CHECK: [[STRUCT:%[0-9]+]] = apply [[FUNC]]()
  // CHECK: [[BORROWED_STRUCT:%.*]] = begin_borrow [[STRUCT]]
  // CHECK: [[RET:%[0-9]+]] = struct_extract [[BORROWED_STRUCT]] : $Val, #Val.y
  // CHECK: end_borrow [[BORROWED_STRUCT]] from [[STRUCT]]
  // CHECK: destroy_value [[STRUCT]]
  // CHECK: return [[RET]]
}

func class_rvalue() -> Ref {}

// CHECK-LABEL: sil hidden @_T010properties21physical_class_rvalue{{[_0-9a-zA-Z]*}}F
func physical_class_rvalue() -> Int {
  return class_rvalue().y
  // CHECK: [[FUNC:%[0-9]+]] = function_ref @_T010properties12class_rvalueAA3RefCyF
  // CHECK: [[CLASS:%[0-9]+]] = apply [[FUNC]]()
  // CHECK: [[FN:%[0-9]+]] = class_method [[CLASS]] : $Ref, #Ref.y!getter.1
  // CHECK: [[RET:%[0-9]+]] = apply [[FN]]([[CLASS]])
  // CHECK: return [[RET]]
}

// CHECK-LABEL: sil hidden @_T010properties18logical_struct_get{{[_0-9a-zA-Z]*}}F
func logical_struct_get() -> Int {
  return struct_rvalue().z
  // CHECK: [[GET_RVAL:%[0-9]+]] = function_ref @_T010properties13struct_rvalue{{[_0-9a-zA-Z]*}}F
  // CHECK: [[STRUCT:%[0-9]+]] = apply [[GET_RVAL]]()
  // CHECK: [[GET_METHOD:%[0-9]+]] = function_ref @_T010properties3ValV1z{{[_0-9a-zA-Z]*}}vg
  // CHECK: [[VALUE:%[0-9]+]] = apply [[GET_METHOD]]([[STRUCT]])
  // CHECK: return [[VALUE]]
}

// CHECK-LABEL: sil hidden @_T010properties18logical_struct_set{{[_0-9a-zA-Z]*}}F
func logical_struct_set(_ value: inout Val, z: Int) {
  // CHECK: bb0([[VAL:%[0-9]+]] : $*Val, [[Z:%[0-9]+]] : $Int):
  value.z = z
  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown] [[VAL]]
  // CHECK: [[Z_SET_METHOD:%[0-9]+]] = function_ref @_T010properties3ValV1z{{[_0-9a-zA-Z]*}}vs
  // CHECK: apply [[Z_SET_METHOD]]([[Z]], [[WRITE]])
  // CHECK: return
}

// CHECK-LABEL: sil hidden @_T010properties27logical_struct_in_tuple_set{{[_0-9a-zA-Z]*}}F
func logical_struct_in_tuple_set(_ value: inout (Int, Val), z: Int) {
  // CHECK: bb0([[VAL:%[0-9]+]] : $*(Int, Val), [[Z:%[0-9]+]] : $Int):
  value.1.z = z
  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown] [[VAL]]
  // CHECK: [[VAL_1:%[0-9]+]] = tuple_element_addr [[WRITE]] : {{.*}}, 1
  // CHECK: [[Z_SET_METHOD:%[0-9]+]] = function_ref @_T010properties3ValV1z{{[_0-9a-zA-Z]*}}vs
  // CHECK: apply [[Z_SET_METHOD]]([[Z]], [[VAL_1]])
  // CHECK: return
}

// CHECK-LABEL: sil hidden @_T010properties29logical_struct_in_reftype_set{{[_0-9a-zA-Z]*}}F
func logical_struct_in_reftype_set(_ value: inout Val, z1: Int) {
  // CHECK: bb0([[VAL:%[0-9]+]] : $*Val, [[Z1:%[0-9]+]] : $Int):
  value.ref.val_prop.z_tuple.1 = z1
  // -- val.ref
  // CHECK: [[READ:%.*]] = begin_access [read] [unknown] [[VAL]]
  // CHECK: [[VAL_REF_ADDR:%[0-9]+]] = struct_element_addr [[READ]] : $*Val, #Val.ref
  // CHECK: [[VAL_REF:%[0-9]+]] = load [copy] [[VAL_REF_ADDR]]
  // -- getters and setters
  // -- val.ref.val_prop
  // CHECK: [[STORAGE:%.*]] = alloc_stack $Builtin.UnsafeValueBuffer
  // CHECK: [[VAL_REF_VAL_PROP_TEMP:%.*]] = alloc_stack $Val
  // CHECK: [[VAL_REF_BORROWED:%.*]] = begin_borrow [[VAL_REF]]
  // CHECK: [[T0:%.*]] = address_to_pointer [[VAL_REF_VAL_PROP_TEMP]] : $*Val to $Builtin.RawPointer
  // CHECK: [[MAT_VAL_PROP_METHOD:%[0-9]+]] = class_method {{.*}} : $Ref, #Ref.val_prop!materializeForSet.1 : (Ref) -> (Builtin.RawPointer, inout Builtin.UnsafeValueBuffer) -> (Builtin.RawPointer, Builtin.RawPointer?)
  // CHECK: [[MAT_RESULT:%[0-9]+]] = apply [[MAT_VAL_PROP_METHOD]]([[T0]], [[STORAGE]], [[VAL_REF_BORROWED]])
  // CHECK: [[T0:%.*]] = tuple_extract [[MAT_RESULT]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>), 0
  // CHECK: [[OPT_CALLBACK:%.*]] = tuple_extract [[MAT_RESULT]] : $(Builtin.RawPointer, Optional<Builtin.RawPointer>), 1  
  // CHECK: [[T1:%[0-9]+]] = pointer_to_address [[T0]] : $Builtin.RawPointer to [strict] $*Val
  // CHECK: [[VAL_REF_VAL_PROP_MAT:%.*]] = mark_dependence [[T1]] : $*Val on [[VAL_REF]]
  // CHECK: end_borrow [[VAL_REF_BORROWED]] from [[VAL_REF]]
  // -- val.ref.val_prop.z_tuple
  // CHECK: [[V_R_VP_Z_TUPLE_MAT:%[0-9]+]] = alloc_stack $(Int, Int)
  // CHECK: [[LD:%[0-9]+]] = load_borrow [[VAL_REF_VAL_PROP_MAT]]
  // CHECK: [[A0:%.*]] = tuple_element_addr [[V_R_VP_Z_TUPLE_MAT]] : {{.*}}, 0
  // CHECK: [[A1:%.*]] = tuple_element_addr [[V_R_VP_Z_TUPLE_MAT]] : {{.*}}, 1
  // CHECK: [[GET_Z_TUPLE_METHOD:%[0-9]+]] = function_ref @_T010properties3ValV7z_tupleSi_Sitvg
  // CHECK: [[V_R_VP_Z_TUPLE:%[0-9]+]] = apply [[GET_Z_TUPLE_METHOD]]([[LD]])
  // CHECK: [[T0:%.*]] = tuple_extract [[V_R_VP_Z_TUPLE]] : {{.*}}, 0
  // CHECK: [[T1:%.*]] = tuple_extract [[V_R_VP_Z_TUPLE]] : {{.*}}, 1
  // CHECK: store [[T0]] to [trivial] [[A0]]
  // CHECK: store [[T1]] to [trivial] [[A1]]
  // CHECK: end_borrow [[LD]] from [[VAL_REF_VAL_PROP_MAT]]
  // -- write to val.ref.val_prop.z_tuple.1
  // CHECK: [[V_R_VP_Z_TUPLE_1:%[0-9]+]] = tuple_element_addr [[V_R_VP_Z_TUPLE_MAT]] : {{.*}}, 1
  // CHECK: assign [[Z1]] to [[V_R_VP_Z_TUPLE_1]]
  // -- writeback to val.ref.val_prop.z_tuple
  // CHECK: [[WB_V_R_VP_Z_TUPLE:%[0-9]+]] = load [trivial] [[V_R_VP_Z_TUPLE_MAT]]
  // CHECK: [[SET_Z_TUPLE_METHOD:%[0-9]+]] = function_ref @_T010properties3ValV7z_tupleSi_Sitvs
  // CHECK: apply [[SET_Z_TUPLE_METHOD]]({{%[0-9]+, %[0-9]+}}, [[VAL_REF_VAL_PROP_MAT]])
  // -- writeback to val.ref.val_prop
  // CHECK: switch_enum [[OPT_CALLBACK]] : $Optional<Builtin.RawPointer>, case #Optional.some!enumelt.1: [[WRITEBACK:bb[0-9]+]], case #Optional.none!enumelt: [[CONT:bb[0-9]+]]
  // CHECK: [[WRITEBACK]]([[CALLBACK_ADDR:%.*]] : $Builtin.RawPointer):
  // CHECK: [[CALLBACK:%.*]] = pointer_to_thin_function [[CALLBACK_ADDR]] : $Builtin.RawPointer to $@convention(method) (Builtin.RawPointer, @inout Builtin.UnsafeValueBuffer, @inout Ref, @thick Ref.Type) -> ()
  // CHECK: [[REF_MAT:%.*]] = alloc_stack $Ref
  // CHECK: store [[VAL_REF]] to [init] [[REF_MAT]]
  // CHECK: [[T0:%.*]] = metatype $@thick Ref.Type
  // CHECK: [[T1:%.*]] = address_to_pointer [[VAL_REF_VAL_PROP_MAT]]
  // CHECK: apply [[CALLBACK]]([[T1]], [[STORAGE]], [[REF_MAT]], [[T0]])
  // CHECK: br [[CONT]]
  // CHECK: [[CONT]]:
  // -- cleanup
  // CHECK: dealloc_stack [[V_R_VP_Z_TUPLE_MAT]]
  // CHECK: dealloc_stack [[VAL_REF_VAL_PROP_TEMP]]
  // -- don't need to write back to val.ref because it's a ref type
}

func reftype_rvalue() -> Ref {}

// CHECK-LABEL: sil hidden @_T010properties18reftype_rvalue_set{{[_0-9a-zA-Z]*}}F
func reftype_rvalue_set(_ value: Val) {
  reftype_rvalue().val_prop = value
}

// CHECK-LABEL: sil hidden @_T010properties27tuple_in_logical_struct_set{{[_0-9a-zA-Z]*}}F
func tuple_in_logical_struct_set(_ value: inout Val, z1: Int) {
  // CHECK: bb0([[VAL:%[0-9]+]] : $*Val, [[Z1:%[0-9]+]] : $Int):
  value.z_tuple.1 = z1
  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown] [[VAL]]
  // CHECK: [[Z_TUPLE_MATERIALIZED:%[0-9]+]] = alloc_stack $(Int, Int)
  // CHECK: [[VAL1:%[0-9]+]] = load_borrow [[WRITE]]
  // CHECK: [[A0:%.*]] = tuple_element_addr [[Z_TUPLE_MATERIALIZED]] : {{.*}}, 0
  // CHECK: [[A1:%.*]] = tuple_element_addr [[Z_TUPLE_MATERIALIZED]] : {{.*}}, 1
  // CHECK: [[Z_GET_METHOD:%[0-9]+]] = function_ref @_T010properties3ValV7z_tupleSi_Sitvg
  // CHECK: [[Z_TUPLE:%[0-9]+]] = apply [[Z_GET_METHOD]]([[VAL1]])
  // CHECK: [[T0:%.*]] = tuple_extract [[Z_TUPLE]] : {{.*}}, 0
  // CHECK: [[T1:%.*]] = tuple_extract [[Z_TUPLE]] : {{.*}}, 1
  // CHECK: store [[T0]] to [trivial] [[A0]]
  // CHECK: store [[T1]] to [trivial] [[A1]]
  // CHECK: end_borrow [[VAL1]] from [[WRITE]]
  // CHECK: [[Z_TUPLE_1:%[0-9]+]] = tuple_element_addr [[Z_TUPLE_MATERIALIZED]] : {{.*}}, 1
  // CHECK: assign [[Z1]] to [[Z_TUPLE_1]]
  // CHECK: [[Z_TUPLE_MODIFIED:%[0-9]+]] = load [trivial] [[Z_TUPLE_MATERIALIZED]]
  // CHECK: [[Z_SET_METHOD:%[0-9]+]] = function_ref @_T010properties3ValV7z_tupleSi_Sitvs
  // CHECK: apply [[Z_SET_METHOD]]({{%[0-9]+, %[0-9]+}}, [[WRITE]])
  // CHECK: dealloc_stack [[Z_TUPLE_MATERIALIZED]]
  // CHECK: return
}

var global_prop : Int {
  // CHECK-LABEL: sil hidden @_T010properties11global_prop{{[_0-9a-zA-Z]*}}vg
  get {
    return zero
  }
  // CHECK-LABEL: sil hidden @_T010properties11global_prop{{[_0-9a-zA-Z]*}}vs
  set {
    use(newValue)
  }
}

// CHECK-LABEL: sil hidden @_T010properties18logical_global_get{{[_0-9a-zA-Z]*}}F
func logical_global_get() -> Int {
  return global_prop
  // CHECK: [[GET:%[0-9]+]] = function_ref @_T010properties11global_prop{{[_0-9a-zA-Z]*}}vg
  // CHECK: [[VALUE:%[0-9]+]] = apply [[GET]]()
  // CHECK: return [[VALUE]]
}

// CHECK-LABEL: sil hidden @_T010properties18logical_global_set{{[_0-9a-zA-Z]*}}F
func logical_global_set(_ x: Int) {
  global_prop = x
  // CHECK: [[SET:%[0-9]+]] = function_ref @_T010properties11global_prop{{[_0-9a-zA-Z]*}}vs
  // CHECK: apply [[SET]](%0)
}

// CHECK-LABEL: sil hidden @_T010properties17logical_local_get{{[_0-9a-zA-Z]*}}F
func logical_local_get(_ x: Int) -> Int {
  var prop : Int {
    get {
      return x
    }
  }
  // CHECK: [[GET_REF:%[0-9]+]] = function_ref [[PROP_GET_CLOSURE:@_T010properties17logical_local_getS2iF4propL_Sivg]]
  // CHECK: apply [[GET_REF]](%0)
  return prop
}
// CHECK-: sil private [[PROP_GET_CLOSURE]]
// CHECK: bb0(%{{[0-9]+}} : $Int):

func logical_generic_local_get<T>(_ x: Int, _: T) {
  var prop1: Int {
    get {
      return x
    }
  }

  _ = prop1

  var prop2: Int {
    get {
      _ = T.self
      return x
    }
  }

  _ = prop2
}

// CHECK-LABEL: sil hidden @_T010properties26logical_local_captured_get{{[_0-9a-zA-Z]*}}F
func logical_local_captured_get(_ x: Int) -> Int {
  var prop : Int {
    get {
      return x
    }
  }
  func get_prop() -> Int {
    return prop
  }

  return get_prop()
  // CHECK: [[FUNC_REF:%[0-9]+]] = function_ref @_T010properties26logical_local_captured_getS2iF0E5_propL_SiyF
  // CHECK: apply [[FUNC_REF]](%0)
}
// CHECK: sil private @_T010properties26logical_local_captured_get{{.*}}vg
// CHECK: bb0(%{{[0-9]+}} : $Int):

func inout_arg(_ x: inout Int) {}

// CHECK-LABEL: sil hidden @_T010properties14physical_inout{{[_0-9a-zA-Z]*}}F
func physical_inout(_ x: Int) {
  var x = x
  // CHECK: [[XADDR:%[0-9]+]] = alloc_box ${ var Int }
  // CHECK: [[PB:%.*]] = project_box [[XADDR]]
  inout_arg(&x)
  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown] [[PB]]
  // CHECK: [[INOUT_ARG:%[0-9]+]] = function_ref @_T010properties9inout_arg{{[_0-9a-zA-Z]*}}F
  // CHECK: apply [[INOUT_ARG]]([[WRITE]])
}


/* TODO check writeback to more complex logical prop, check that writeback
 * reuses temporaries */

// CHECK-LABEL: sil hidden @_T010properties17val_subscript_get{{[_0-9a-zA-Z]*}}F : $@convention(thin) (@owned Val, Int) -> Float
// CHECK: bb0([[VVAL:%[0-9]+]] : $Val, [[I:%[0-9]+]] : $Int):
func val_subscript_get(_ v: Val, i: Int) -> Float {
  return v[i]
  // CHECK: [[BORROWED_VVAL:%.*]] = begin_borrow [[VVAL]]
  // CHECK: [[SUBSCRIPT_GET_METHOD:%[0-9]+]] = function_ref @_T010properties3ValV{{[_0-9a-zA-Z]*}}ig
  // CHECK: [[RET:%[0-9]+]] = apply [[SUBSCRIPT_GET_METHOD]]([[I]], [[BORROWED_VVAL]]) : $@convention(method) (Int, @guaranteed Val)
  // CHECK: end_borrow [[BORROWED_VVAL]] from [[VVAL]]
  // CHECK: destroy_value [[VVAL]]
  // CHECK: return [[RET]]
}

// CHECK-LABEL: sil hidden @_T010properties17val_subscript_set{{[_0-9a-zA-Z]*}}F
// CHECK: bb0(%0 : $Val, [[I:%[0-9]+]] : $Int, [[X:%[0-9]+]] : $Float):
func val_subscript_set(_ v: Val, i: Int, x: Float) {
  var v = v
  v[i] = x
  // CHECK: [[VADDR:%[0-9]+]] = alloc_box ${ var Val }
  // CHECK: [[PB:%.*]] = project_box [[VADDR]]
  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown] [[PB]]
  // CHECK: [[SUBSCRIPT_SET_METHOD:%[0-9]+]] = function_ref @_T010properties3ValV{{[_0-9a-zA-Z]*}}is
  // CHECK: apply [[SUBSCRIPT_SET_METHOD]]([[X]], [[I]], [[WRITE]])
}

struct Generic<T> {
  var mono_phys:Int
  var mono_log: Int { get {} set {} }
  var typevar_member:T

  subscript(x: Int) -> Float { get {} set {} }

  subscript(x: T) -> T { get {} set {} }

  // CHECK-LABEL: sil hidden @_T010properties7GenericV19copy_typevar_member{{[_0-9a-zA-Z]*}}F
  mutating
  func copy_typevar_member(_ x: Generic<T>) {
    typevar_member = x.typevar_member
  }
}

// CHECK-LABEL: sil hidden @_T010properties21generic_mono_phys_get{{[_0-9a-zA-Z]*}}F
func generic_mono_phys_get<T>(_ g: Generic<T>) -> Int {
  return g.mono_phys
  // CHECK: struct_element_addr %{{.*}}, #Generic.mono_phys
}

// CHECK-LABEL: sil hidden @_T010properties20generic_mono_log_get{{[_0-9a-zA-Z]*}}F
func generic_mono_log_get<T>(_ g: Generic<T>) -> Int {
  return g.mono_log
  // CHECK: [[GENERIC_GET_METHOD:%[0-9]+]] = function_ref @_T010properties7GenericV8mono_log{{[_0-9a-zA-Z]*}}vg
  // CHECK: apply [[GENERIC_GET_METHOD]]<
}

// CHECK-LABEL: sil hidden @_T010properties20generic_mono_log_set{{[_0-9a-zA-Z]*}}F
func generic_mono_log_set<T>(_ g: Generic<T>, x: Int) {
  var g = g
  g.mono_log = x
  // CHECK: [[GENERIC_SET_METHOD:%[0-9]+]] = function_ref @_T010properties7GenericV8mono_log{{[_0-9a-zA-Z]*}}vs
  // CHECK: apply [[GENERIC_SET_METHOD]]<
}

// CHECK-LABEL: sil hidden @_T010properties26generic_mono_subscript_get{{[_0-9a-zA-Z]*}}F
func generic_mono_subscript_get<T>(_ g: Generic<T>, i: Int) -> Float {
  return g[i]
  // CHECK: [[GENERIC_GET_METHOD:%[0-9]+]] = function_ref @_T010properties7GenericV{{[_0-9a-zA-Z]*}}ig
  // CHECK: apply [[GENERIC_GET_METHOD]]<
}

// CHECK-LABEL: sil hidden @{{.*}}generic_mono_subscript_set
func generic_mono_subscript_set<T>(_ g: inout Generic<T>, i: Int, x: Float) {
  g[i] = x
  // CHECK: [[GENERIC_SET_METHOD:%[0-9]+]] = function_ref @_T010properties7GenericV{{[_0-9a-zA-Z]*}}is
  // CHECK: apply [[GENERIC_SET_METHOD]]<
}

// CHECK-LABEL: sil hidden @{{.*}}bound_generic_mono_phys_get
func bound_generic_mono_phys_get(_ g: inout Generic<UnicodeScalar>, x: Int) -> Int {
  return g.mono_phys
  // CHECK: struct_element_addr %{{.*}}, #Generic.mono_phys
}

// CHECK-LABEL: sil hidden @_T010properties26bound_generic_mono_log_get{{[_0-9a-zA-Z]*}}F
func bound_generic_mono_log_get(_ g: Generic<UnicodeScalar>, x: Int) -> Int {
  return g.mono_log
// CHECK: [[GENERIC_GET_METHOD:%[0-9]+]] = function_ref @_T010properties7GenericV8mono_log{{[_0-9a-zA-Z]*}}vg
  // CHECK: apply [[GENERIC_GET_METHOD]]<
}

// CHECK-LABEL: sil hidden @_T010properties22generic_subscript_type{{[_0-9a-zA-Z]*}}F
func generic_subscript_type<T>(_ g: Generic<T>, i: T, x: T) -> T {
  var g = g
  g[i] = x
  return g[i]
}

/*TODO: archetype and existential properties and subscripts */

struct StaticProperty {
  static var foo: Int {
    get {
      return zero
    }
    set {}
  }
}

// CHECK-LABEL: sil hidden @_T010properties10static_get{{[_0-9a-zA-Z]*}}F
// CHECK:   function_ref @_T010properties14StaticPropertyV3foo{{[_0-9a-zA-Z]*}}vgZ : $@convention(method) (@thin StaticProperty.Type) -> Int
func static_get() -> Int {
  return StaticProperty.foo
}

// CHECK-LABEL: sil hidden @_T010properties10static_set{{[_0-9a-zA-Z]*}}F
// CHECK:   function_ref @_T010properties14StaticPropertyV3foo{{[_0-9a-zA-Z]*}}vsZ : $@convention(method) (Int, @thin StaticProperty.Type) -> ()
func static_set(_ x: Int) {
  StaticProperty.foo = x
}

func takeInt(_ a : Int) {}

protocol ForceAccessors {
  var a: Int { get set }
}

struct DidSetWillSetTests: ForceAccessors {
  var a: Int {
    willSet(newA) {
      // CHECK-LABEL: // {{.*}}.DidSetWillSetTests.a.willset
      // CHECK-NEXT: sil hidden @_T010properties010DidSetWillC5TestsV1a{{[_0-9a-zA-Z]*}}vw
      // CHECK: bb0(%0 : $Int, %1 : $*DidSetWillSetTests):
      // CHECK-NEXT: debug_value %0
      // CHECK-NEXT: debug_value_addr %1 : $*DidSetWillSetTests

      takeInt(a)

      // CHECK: [[READ:%.*]] = begin_access [read] [unknown] %1
      // CHECK-NEXT: [[FIELDPTR:%.*]] = struct_element_addr [[READ]] : $*DidSetWillSetTests, #DidSetWillSetTests.a
      // CHECK-NEXT: [[A:%.*]] = load [trivial] [[FIELDPTR]] : $*Int
      // CHECK-NEXT: end_access [[READ]]
      // CHECK: [[TAKEINTFN:%.*]] = function_ref @_T010properties7takeInt{{[_0-9a-zA-Z]*}}F
      // CHECK-NEXT: apply [[TAKEINTFN]]([[A]]) : $@convention(thin) (Int) -> ()

      takeInt(newA)

      // CHECK-NEXT: // function_ref properties.takeInt(Swift.Int) -> ()
      // CHECK-NEXT: [[TAKEINTFN:%.*]] = function_ref @_T010properties7takeInt{{[_0-9a-zA-Z]*}}F
      // CHECK-NEXT: apply [[TAKEINTFN]](%0) : $@convention(thin) (Int) -> ()
    }

    didSet {
      // CHECK-LABEL: // {{.*}}.DidSetWillSetTests.a.didset
      // CHECK-NEXT: sil hidden @_T010properties010DidSetWillC5TestsV1a{{[_0-9a-zA-Z]*}}vW
      // CHECK: bb0(%0 : $Int, %1 : $*DidSetWillSetTests):
      // CHECK-NEXT: debug
      // CHECK-NEXT: debug_value_addr %1 : $*DidSetWillSetTests

      takeInt(a)

      // CHECK: [[READ:%.*]] = begin_access [read] [unknown] %1
      // CHECK-NEXT: [[AADDR:%.*]] = struct_element_addr [[READ]] : $*DidSetWillSetTests, #DidSetWillSetTests.a
      // CHECK-NEXT: [[A:%.*]] = load [trivial] [[AADDR]] : $*Int
      // CHECK-NEXT: end_access [[READ]]
      // CHECK-NEXT: // function_ref properties.takeInt(Swift.Int) -> ()
      // CHECK-NEXT: [[TAKEINTFN:%.*]] = function_ref @_T010properties7takeInt{{[_0-9a-zA-Z]*}}F
      // CHECK-NEXT: apply [[TAKEINTFN]]([[A]]) : $@convention(thin) (Int) -> ()

      a = zero  // reassign, but don't infinite loop.

      // CHECK-NEXT: // function_ref properties.zero.unsafeMutableAddressor : Swift.Int
      // CHECK-NEXT: [[ZEROFN:%.*]] = function_ref @_T010properties4zero{{[_0-9a-zA-Z]*}}vau
      // CHECK-NEXT: [[ZERORAW:%.*]] = apply [[ZEROFN]]() : $@convention(thin) () -> Builtin.RawPointer
      // CHECK-NEXT: [[ZEROADDR:%.*]] = pointer_to_address [[ZERORAW]] : $Builtin.RawPointer to [strict] $*Int
      // CHECK-NEXT: [[READ:%.*]] = begin_access [read] [dynamic] [[ZEROADDR]] : $*Int
      // CHECK-NEXT: [[ZERO:%.*]] = load [trivial] [[READ]]
      // CHECK-NEXT: end_access [[READ]] : $*Int
      // CHECK-NEXT: [[WRITE:%.*]] = begin_access [modify] [unknown] %1
      // CHECK-NEXT: [[AADDR:%.*]] = struct_element_addr [[WRITE]] : $*DidSetWillSetTests, #DidSetWillSetTests.a
      // CHECK-NEXT: assign [[ZERO]] to [[AADDR]]
    }
  }

  init(x : Int) {
    // Accesses to didset/willset variables are direct in init methods and dtors.
    a = x
    a = x
  }

  // These are the synthesized getter and setter for the willset/didset variable.

  // CHECK-LABEL: // {{.*}}.DidSetWillSetTests.a.getter
  // CHECK-NEXT: sil hidden [transparent] @_T010properties010DidSetWillC5TestsV1aSivg
  // CHECK: bb0(%0 : $DidSetWillSetTests):
  // CHECK-NEXT:   debug_value %0
  // CHECK-NEXT:   %2 = struct_extract %0 : $DidSetWillSetTests, #DidSetWillSetTests.a
  // CHECK-NEXT:   return %2 : $Int{{.*}}                      // id: %3

  // CHECK-LABEL: // {{.*}}.DidSetWillSetTests.a.setter
  // CHECK-NEXT: sil hidden @_T010properties010DidSetWillC5TestsV1aSivs
  // CHECK: bb0(%0 : $Int, %1 : $*DidSetWillSetTests):
  // CHECK-NEXT: debug_value %0
  // CHECK-NEXT: debug_value_addr %1

  // CHECK-NEXT: [[READ:%.*]] = begin_access [read] [unknown] %1
  // CHECK-NEXT: [[AADDR:%.*]] = struct_element_addr [[READ]] : $*DidSetWillSetTests, #DidSetWillSetTests.a
  // CHECK-NEXT: [[OLDVAL:%.*]] = load [trivial] [[AADDR]] : $*Int
  // CHECK-NEXT: end_access [[READ]]
  // CHECK-NEXT: debug_value [[OLDVAL]] : $Int, let, name "tmp"

  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown] %1
  // CHECK-NEXT: // function_ref {{.*}}.DidSetWillSetTests.a.willset : Swift.Int
  // CHECK-NEXT: [[WILLSETFN:%.*]] = function_ref @_T010properties010DidSetWillC5TestsV1a{{[_0-9a-zA-Z]*}}vw
  // CHECK-NEXT:  apply [[WILLSETFN]](%0, [[WRITE]]) : $@convention(method) (Int, @inout DidSetWillSetTests) -> ()
  // CHECK-NEXT: end_access [[WRITE]]
  // CHECK-NEXT: [[WRITE:%.*]] = begin_access [modify] [unknown] %1
  // CHECK-NEXT: [[AADDR:%.*]] = struct_element_addr [[WRITE]] : $*DidSetWillSetTests, #DidSetWillSetTests.a
  // CHECK-NEXT: assign %0 to [[AADDR]] : $*Int
  // CHECK-NEXT: end_access [[WRITE]]
  // CHECK-NEXT: [[WRITE:%.*]] = begin_access [modify] [unknown] %1
  // CHECK-NEXT: // function_ref {{.*}}.DidSetWillSetTests.a.didset : Swift.Int
  // CHECK-NEXT: [[DIDSETFN:%.*]] = function_ref @_T010properties010DidSetWillC5TestsV1a{{[_0-9a-zA-Z]*}}vW : $@convention(method) (Int, @inout DidSetWillSetTests) -> ()
  // CHECK-NEXT: apply [[DIDSETFN]]([[OLDVAL]], [[WRITE]]) : $@convention(method) (Int, @inout DidSetWillSetTests) -> ()

  // CHECK-LABEL: sil hidden @_T010properties010DidSetWillC5TestsV{{[_0-9a-zA-Z]*}}fC
  // CHECK: bb0(%0 : $Int, %1 : $@thin DidSetWillSetTests.Type):
  // CHECK:        [[SELF:%.*]] = mark_uninitialized [rootself]
  // CHECK:        [[PB_SELF:%.*]] = project_box [[SELF]]
  // CHECK:        [[WRITE:%.*]] = begin_access [modify] [unknown] [[PB_SELF]]
  // CHECK:        [[P1:%.*]] = struct_element_addr [[WRITE]] : $*DidSetWillSetTests, #DidSetWillSetTests.a
  // CHECK-NEXT:   assign %0 to [[P1]]
  // CHECK:        [[WRITE:%.*]] = begin_access [modify] [unknown] [[PB_SELF]]
  // CHECK:        [[P2:%.*]] = struct_element_addr [[WRITE]] : $*DidSetWillSetTests, #DidSetWillSetTests.a
  // CHECK-NEXT:   assign %0 to [[P2]]
}


// Test global observing properties.

var global_observing_property : Int = zero {
  didSet {
    takeInt(global_observing_property)
  }
}

func force_global_observing_property_setter() {
  let x = global_observing_property
  global_observing_property = x
}

// The property is initialized with "zero".
// CHECK-LABEL: sil private @globalinit_{{.*}}_func1 : $@convention(c) () -> () {
// CHECK: bb0:
// CHECK-NEXT: alloc_global @_T010properties25global_observing_propertySiv
// CHECK-NEXT: %1 = global_addr @_T010properties25global_observing_propertySivp : $*Int
// CHECK: properties.zero.unsafeMutableAddressor
// CHECK: return

// The didSet implementation needs to call takeInt.

// CHECK-LABEL: sil hidden @_T010properties25global_observing_property{{[_0-9a-zA-Z]*}}vW
// CHECK: function_ref properties.takeInt
// CHECK-NEXT: function_ref @_T010properties7takeInt{{[_0-9a-zA-Z]*}}F

// The setter needs to call didSet implementation.

// CHECK-LABEL: sil hidden @_T010properties25global_observing_property{{[_0-9a-zA-Z]*}}vs
// CHECK: function_ref properties.global_observing_property.unsafeMutableAddressor
// CHECK-NEXT:  function_ref @_T010properties25global_observing_property{{[_0-9a-zA-Z]*}}vau
// CHECK: function_ref properties.global_observing_property.didset
// CHECK-NEXT: function_ref @_T010properties25global_observing_property{{[_0-9a-zA-Z]*}}vW


// Test local observing properties.

func local_observing_property(_ arg: Int) {
  var localproperty: Int = arg {
    didSet {
      takeInt(localproperty)
    }
  }
  
  takeInt(localproperty)
  localproperty = arg
}

// This is the local_observing_property function itself.  First alloc and 
// initialize the property to the argument value.

// CHECK-LABEL: sil hidden @{{.*}}local_observing_property
// CHECK: bb0([[ARG:%[0-9]+]] : $Int)
// CHECK: [[BOX:%[0-9]+]] = alloc_box ${ var Int }
// CHECK: [[PB:%.*]] = project_box [[BOX]]
// CHECK: store [[ARG]] to [trivial] [[PB]]

func local_generic_observing_property<T>(_ arg: Int, _: T) {
  var localproperty1: Int = arg {
    didSet {
      takeInt(localproperty1)
    }
  }
  
  takeInt(localproperty1)
  localproperty1 = arg

  var localproperty2: Int = arg {
    didSet {
      _ = T.self
      takeInt(localproperty2)
    }
  }
  
  takeInt(localproperty2)
  localproperty2 = arg
}


// <rdar://problem/16006333> observing properties don't work in @objc classes
@objc
class ObservingPropertyInObjCClass {
  var bounds: Int {
    willSet {}
    didSet {}
  }

  init(b: Int) { bounds = b }
}



// Superclass init methods should not get direct access to be class properties.
// rdar://16151899

class rdar16151899Base {
  var x: Int = zero {
  willSet {
    use(x)
  }
  }
}

class rdar16151899Derived : rdar16151899Base {
    // CHECK-LABEL: sil hidden @_T010properties19rdar16151899DerivedC{{[_0-9a-zA-Z]*}}fc
    override init() {
        super.init()
        // CHECK: upcast {{.*}} : $rdar16151899Derived to $rdar16151899Base
        // CHECK: function_ref @_T010properties16rdar16151899BaseCACycfc : $@convention(method) (@owned rdar16151899Base) -> @owned rdar16151899Base

        // This should not be a direct access, it should call the setter in the
        // base.
        x = zero
        
        // CHECK:  [[BASEPTR:%[0-9]+]] = upcast {{.*}} : $rdar16151899Derived to $rdar16151899Base
        // CHECK: load{{.*}}Int
        // CHECK-NEXT: end_access {{.*}} : $*Int
        // CHECK-NEXT: [[SETTER:%[0-9]+]] = class_method {{.*}} : $rdar16151899Base, #rdar16151899Base.x!setter.1 : (rdar16151899Base)
        // CHECK-NEXT: apply [[SETTER]]({{.*}}, [[BASEPTR]]) 
    }
}


func propertyWithDidSetTakingOldValue() {
  var p : Int = zero {
    didSet(oldValue) {
      // access to oldValue
      use(oldValue)
      // and newValue.
      use(p)
    }
  }

  p = zero
}

// CHECK: // setter of p #1 : Swift.Int in properties.propertyWithDidSetTakingOldValue()
// CHECK-NEXT: sil {{.*}} @_T010properties32propertyWithDidSetTakingOldValueyyF1pL_Sivs
// CHECK: bb0([[ARG1:%.*]] : $Int, [[ARG2:%.*]] : ${ var Int }):
// CHECK-NEXT:  debug_value [[ARG1]] : $Int, let, name "newValue", argno 1
// CHECK-NEXT:  [[ARG2_PB:%.*]] = project_box [[ARG2]]
// CHECK-NEXT:  debug_value_addr [[ARG2_PB]] : $*Int, var, name "p", argno 2
// CHECK-NEXT:  [[READ:%.*]] = begin_access [read] [unknown] [[ARG2_PB]]
// CHECK-NEXT:  [[ARG2_PB_VAL:%.*]] = load [trivial] [[READ]] : $*Int
// CHECK-NEXT:  end_access [[READ]]
// CHECK-NEXT:  debug_value [[ARG2_PB_VAL]] : $Int
// CHECK-NEXT:  [[WRITE:%.*]] = begin_access [modify] [unknown] [[ARG2_PB]]
// CHECK-NEXT:  assign [[ARG1]] to [[WRITE]] : $*Int
// CHECK-NEXT:  end_access [[WRITE]]
// SEMANTIC ARC TODO: Another case where we need to put the mark_function_escape on a new projection after a copy.
// CHECK-NEXT:  mark_function_escape [[ARG2_PB]]
// CHECK-NEXT:  // function_ref
// CHECK-NEXT:  [[FUNC:%.*]] = function_ref @_T010properties32propertyWithDidSetTakingOldValueyyF1pL_SivW : $@convention(thin) (Int, @guaranteed { var Int }) -> ()
// CHECK-NEXT:  %{{.*}} = apply [[FUNC]]([[ARG2_PB_VAL]], [[ARG2]]) : $@convention(thin) (Int, @guaranteed { var Int }) -> ()
// CHECK-NEXT:  %{{.*}} = tuple ()
// CHECK-NEXT:  return %{{.*}} : $()
// CHECK-NEXT:} // end sil function '_T010properties32propertyWithDidSetTakingOldValue{{[_0-9a-zA-Z]*}}'


class BaseProperty {
  var x : Int { get {} set {} }
}

class DerivedProperty : BaseProperty {
  override var x : Int { get {} set {} }

  func super_property_reference() -> Int {
    return super.x
  }
}

// rdar://16381392 - Super property references in non-objc classes should be direct.

// CHECK-LABEL: sil hidden @_T010properties15DerivedPropertyC24super_property_referenceSiyF : $@convention(method) (@guaranteed DerivedProperty) -> Int {
// CHECK: bb0([[SELF:%.*]] : $DerivedProperty):
// CHECK:   [[SELF_COPY:%[0-9]+]] = copy_value [[SELF]]
// CHECK:   [[BASEPTR:%[0-9]+]] = upcast [[SELF_COPY]] : $DerivedProperty to $BaseProperty
// CHECK:   [[FN:%[0-9]+]] = function_ref @_T010properties12BasePropertyC1xSivg : $@convention(method) (@guaranteed BaseProperty) -> Int 
// CHECK:   [[RESULT:%.*]] = apply [[FN]]([[BASEPTR]]) : $@convention(method) (@guaranteed BaseProperty) -> Int
// CHECK:   destroy_value [[BASEPTR]]
// CHECK:   return [[RESULT]] : $Int
// CHECK: } // end sil function '_T010properties15DerivedPropertyC24super_property_referenceSiyF'


// <rdar://problem/16411449> ownership qualifiers don't work with non-mutating struct property
struct ReferenceStorageTypeRValues {
  unowned var p1 : Ref

  func testRValueUnowned() -> Ref {
    return p1
  }
// CHECK: sil hidden @{{.*}}testRValueUnowned{{.*}} : $@convention(method) (@guaranteed ReferenceStorageTypeRValues) -> @owned Ref {
// CHECK: bb0([[ARG:%.*]] : $ReferenceStorageTypeRValues):
// CHECK-NEXT:   debug_value [[ARG]] : $ReferenceStorageTypeRValues
// CHECK-NEXT:   [[UNOWNED_ARG_FIELD:%.*]] = struct_extract [[ARG]] : $ReferenceStorageTypeRValues, #ReferenceStorageTypeRValues.p1
// CHECK-NEXT:   [[COPIED_VALUE:%.*]] = copy_unowned_value [[UNOWNED_ARG_FIELD]]
// CHECK-NEXT:   return [[COPIED_VALUE]] : $Ref

  init() {
  }
}


// <rdar://problem/16406886> Observing properties don't work with ownership types
struct ObservingPropertiesWithOwnershipTypes {
  unowned var alwaysPresent : Ref {
    didSet {
    }
  }

  init(res: Ref) {
    alwaysPresent = res
  }
}

struct ObservingPropertiesWithOwnershipTypesInferred {
  unowned var alwaysPresent = Ref(i: 0) {
    didSet {
    }
  }

  weak var maybePresent = nil as Ref? {
    willSet {
    }
  }
}

// <rdar://problem/16554876> property accessor synthesization of weak variables doesn't work
protocol WeakPropertyProtocol {
 var maybePresent : Ref? { get set }
}

struct WeakPropertyStruct : WeakPropertyProtocol {
 weak var maybePresent : Ref?

  init() {
    maybePresent = nil
  }
}

// <rdar://problem/16629598> direct property accesses to generic struct
// properties were being mischecked as computed property accesses.

struct SomeGenericStruct<T> {
  var x: Int
}

// CHECK-LABEL: sil hidden @_T010properties4getX{{[_0-9a-zA-Z]*}}F
// CHECK:         struct_extract {{%.*}} : $SomeGenericStruct<T>, #SomeGenericStruct.x
func getX<T>(_ g: SomeGenericStruct<T>) -> Int {
  return g.x
}


// <rdar://problem/16189360> [DF] Assert on subscript with variadic parameter
struct VariadicSubscript {
  subscript(subs: Int...) -> Int {
    get {
      return 42
    }
  }

  func test() {
    var s = VariadicSubscript()
    var x = s[0, 1, 2]
  }
}


//<rdar://problem/16620121> Initializing constructor tries to initialize computed property overridden with willSet/didSet
class ObservedBase {
     var printInfo: Ref!
}
class ObservedDerived : ObservedBase {
  override init() {}
  override var printInfo: Ref! {
    didSet { }
  }
}



/// <rdar://problem/16953517> Class properties should be allowed in protocols, even without stored class properties
protocol ProtoWithClassProp {
  static var x: Int { get }
}

class ClassWithClassProp : ProtoWithClassProp {
  class var x: Int {
  return 42
  }
}

struct StructWithClassProp : ProtoWithClassProp {
  static var x: Int {
  return 19
  }
}


func getX<T : ProtoWithClassProp>(_ a : T) -> Int {
  return T.x
}

func testClassPropertiesInProtocol() -> Int {
  return getX(ClassWithClassProp())+getX(StructWithClassProp())
}

class GenericClass<T> {
  var x: T
  var y: Int
  final let z: T

  init() { fatalError("scaffold") }
}

// CHECK-LABEL: sil hidden @_T010properties12genericPropsyAA12GenericClassCySSGF : $@convention(thin) (@owned GenericClass<String>) -> () {
func genericProps(_ x: GenericClass<String>) {
  // CHECK: bb0([[ARG:%.*]] : $GenericClass<String>):
  // CHECK:   [[BORROWED_ARG:%.*]] = begin_borrow [[ARG]]
  // CHECK:   class_method [[BORROWED_ARG]] : $GenericClass<String>, #GenericClass.x!getter.1
  // CHECK:   apply {{.*}}<String>({{.*}}, [[BORROWED_ARG]]) : $@convention(method) <τ_0_0> (@guaranteed GenericClass<τ_0_0>) -> @out τ_0_0
  // CHECK:   end_borrow [[BORROWED_ARG]] from [[ARG]]
  let _ = x.x
  // CHECK:   [[BORROWED_ARG:%.*]] = begin_borrow [[ARG]]
  // CHECK:   class_method [[BORROWED_ARG]] : $GenericClass<String>, #GenericClass.y!getter.1
  // CHECK:   apply {{.*}}<String>([[BORROWED_ARG]]) : $@convention(method) <τ_0_0> (@guaranteed GenericClass<τ_0_0>) -> Int
  // CHECK: end_borrow [[BORROWED_ARG]] from [[ARG]]
  let _ = x.y
  // CHECK:   [[BORROWED_ARG:%.*]] = begin_borrow [[ARG]]
  // CHECK:   [[Z:%.*]] = ref_element_addr [[BORROWED_ARG]] : $GenericClass<String>, #GenericClass.z
  // CHECK:   [[READ:%.*]] = begin_access [read] [dynamic] [[Z]] : $*String
  // CHECK:   [[LOADED_Z:%.*]] = load [copy] [[READ]] : $*String
  // CHECK:   destroy_value [[LOADED_Z]]
  // CHECK:   end_borrow [[BORROWED_ARG]] from [[ARG]]
  // CHECK:   destroy_value [[ARG]]
  let _ = x.z
}

// CHECK-LABEL: sil hidden @_T010properties28genericPropsInGenericContext{{[_0-9a-zA-Z]*}}F
func genericPropsInGenericContext<U>(_ x: GenericClass<U>) {
  // CHECK: bb0([[ARG:%.*]] : $GenericClass<U>):
  // CHECK:   [[BORROWED_ARG:%.*]] = begin_borrow [[ARG]]
  // CHECK:   [[Z:%.*]] = ref_element_addr [[BORROWED_ARG]] : $GenericClass<U>, #GenericClass.z
  // CHECK:   [[READ:%.*]] = begin_access [read] [dynamic] [[Z]] : $*U
  // CHECK:   copy_addr [[READ]] {{.*}} : $*U
  // CHECK:   end_borrow [[BORROWED_ARG]] from [[ARG]]
  let _ = x.z
}


// <rdar://problem/18275556> 'let' properties in a class should be implicitly final
class ClassWithLetProperty {
  let p = 42
  dynamic let q = 97

  // We shouldn't have any dynamic dispatch within this method, just load p.
  func ReturnConstant() -> Int { return p }
// CHECK-LABEL: sil hidden @_T010properties20ClassWithLetPropertyC14ReturnConstant{{[_0-9a-zA-Z]*}}F
// CHECK:       bb0([[ARG:%.*]] : $ClassWithLetProperty):
// CHECK-NEXT:    debug_value
// CHECK-NEXT:    [[PTR:%[0-9]+]] = ref_element_addr [[ARG]] : $ClassWithLetProperty, #ClassWithLetProperty.p
// CHECK-NEXT:    [[READ:%.*]] = begin_access [read] [dynamic] [[PTR]] : $*Int
// CHECK-NEXT:    [[VAL:%[0-9]+]] = load [trivial] [[READ]] : $*Int
// CHECK-NEXT:    end_access [[READ]] : $*Int
// CHECK-NEXT:   return [[VAL]] : $Int


  // This property is marked dynamic, so go through the getter, always.
  func ReturnDynamicConstant() -> Int { return q }
// CHECK-LABEL: sil hidden @_T010properties20ClassWithLetPropertyC21ReturnDynamicConstant{{[_0-9a-zA-Z]*}}F
// CHECK: objc_method %0 : $ClassWithLetProperty, #ClassWithLetProperty.q!getter.1.foreign
}


// <rdar://problem/19254812> DI bug when referencing let member of a class
class r19254812Base {}
class r19254812Derived: r19254812Base{
  let pi = 3.14159265359
  
  init(x : ()) {
    use(pi)
  }
  
// Accessing the "pi" property should not copy_value/release self.
// CHECK-LABEL: sil hidden @_T010properties16r19254812DerivedC{{[_0-9a-zA-Z]*}}fc
// CHECK: [[MARKED_SELF_BOX:%.*]] = mark_uninitialized [derivedself]
// CHECK: [[PB_BOX:%.*]] = project_box [[MARKED_SELF_BOX]]

// Initialization of the pi field: no copy_values/releases.
// CHECK:  [[SELF:%[0-9]+]] = load_borrow [[PB_BOX]] : $*r19254812Derived
// CHECK-NEXT:  [[PIPTR:%[0-9]+]] = ref_element_addr [[SELF]] : $r19254812Derived, #r19254812Derived.pi
// CHECK-NEXT:  [[WRITE:%.*]] = begin_access [modify] [dynamic] [[PIPTR]] : $*Double
// CHECK-NEXT:  assign {{.*}} to [[WRITE]] : $*Double

// CHECK-NOT: destroy_value
// CHECK-NOT: copy_value

// Load of the pi field: no copy_values/releases.
// CHECK:  [[SELF:%[0-9]+]] = load_borrow [[PB_BOX]] : $*r19254812Derived
// CHECK-NEXT:  [[PIPTR:%[0-9]+]] = ref_element_addr [[SELF]] : $r19254812Derived, #r19254812Derived.pi
// CHECK-NEXT:  [[READ:%.*]] = begin_access [read] [dynamic] [[PIPTR]] : $*Double
// CHECK-NEXT:  {{.*}} = load [trivial] [[READ]] : $*Double
// CHECK-NEXT:  end_access [[READ]] : $*Double
// CHECK: return
}


class RedundantSelfRetains {
  final var f : RedundantSelfRetains
  
  init() {
    f = RedundantSelfRetains()
  }
  
  // <rdar://problem/19275047> Extraneous copy_values/releases of self are bad
  func testMethod1() {
    f = RedundantSelfRetains()
  }
  // CHECK-LABEL: sil hidden @_T010properties20RedundantSelfRetainsC11testMethod1{{[_0-9a-zA-Z]*}}F
  // CHECK: bb0(%0 : $RedundantSelfRetains):

  // CHECK-NOT: copy_value
  
  // CHECK: [[FPTR:%[0-9]+]] = ref_element_addr %0 : $RedundantSelfRetains, #RedundantSelfRetains.f
  // CHECK-NEXT: [[WRITE:%.*]] = begin_access [modify] [dynamic] [[FPTR]] : $*RedundantSelfRetains
  // CHECK-NEXT: assign {{.*}} to [[WRITE]] : $*RedundantSelfRetains

  // CHECK: return
}

class RedundantRetains {
  final var field = 0
}

func testRedundantRetains() {
  let a = RedundantRetains()
  a.field = 4  // no copy_value/release of a necessary here.
}

// CHECK-LABEL: sil hidden @_T010properties20testRedundantRetainsyyF : $@convention(thin) () -> () {
// CHECK: [[A:%[0-9]+]] = apply
// CHECK-NOT: copy_value
// CHECK: destroy_value [[A]] : $RedundantRetains
// CHECK-NOT: copy_value
// CHECK-NOT: destroy_value
// CHECK: return

struct AddressOnlyNonmutatingSet<T> {
  var x: T
  init(x: T) { self.x = x }
  var prop: Int {
    get { return 0 }
    nonmutating set { }
  }
}

func addressOnlyNonmutatingProperty<T>(_ x: AddressOnlyNonmutatingSet<T>)
-> Int {
  x.prop = 0
  return x.prop
}
// CHECK-LABEL: sil hidden @_T010properties30addressOnlyNonmutatingProperty{{[_0-9a-zA-Z]*}}F
// CHECK:         [[SET:%.*]] = function_ref @_T010properties25AddressOnlyNonmutatingSetV4propSivs
// CHECK:         apply [[SET]]<T>({{%.*}}, [[TMP:%[0-9]*]])
// CHECK:         destroy_addr [[TMP]]
// CHECK:         dealloc_stack [[TMP]]
// CHECK:         [[GET:%.*]] = function_ref @_T010properties25AddressOnlyNonmutatingSetV4propSivg
// CHECK:         apply [[GET]]<T>([[TMP:%[0-9]*]])
// CHECK:         destroy_addr [[TMP]]
// CHECK:         dealloc_stack [[TMP]]

protocol MakeAddressOnly {}
struct AddressOnlyReadOnlySubscript {
  var x: MakeAddressOnly?

  subscript(z: Int) -> Int { return z }
}

// CHECK-LABEL: sil hidden @_T010properties015addressOnlyReadC24SubscriptFromMutableBase
// CHECK:         [[BASE:%.*]] = alloc_box ${ var AddressOnlyReadOnlySubscript }
// CHECK:         copy_addr [[BASE:%.*]] to [initialization] [[COPY:%.*]] :
// CHECK:         [[GETTER:%.*]] = function_ref @_T010properties015AddressOnlyReadC9SubscriptV{{[_0-9a-zA-Z]*}}ig
// CHECK:         apply [[GETTER]]({{%.*}}, [[COPY]])
func addressOnlyReadOnlySubscriptFromMutableBase(_ x: Int) {
  var base = AddressOnlyReadOnlySubscript()
  _ = base[x]
}



/// <rdar://problem/20912019> passing unmaterialized r-value as inout argument
struct MutatingGetterStruct {
  var write: Int {
    mutating get {  }
  }

  // CHECK-LABEL: sil hidden @_T010properties20MutatingGetterStructV4test
  // CHECK: [[X:%.*]] = alloc_box ${ var MutatingGetterStruct }, var, name "x"
  // CHECK-NEXT: [[PB:%.*]] = project_box [[X]]
  // CHECK: store {{.*}} to [trivial] [[PB]] : $*MutatingGetterStruct
  // CHECK: [[WRITE:%.*]] = begin_access [modify] [unknown] [[PB]]
  // CHECK: apply {{%.*}}([[WRITE]]) : $@convention(method) (@inout MutatingGetterStruct) -> Int
  static func test() {
    var x = MutatingGetterStruct()
    _ = x.write
  }
}


protocol ProtocolWithReadWriteSubscript {
  subscript(i: Int) -> Int { get set }
}

struct CrashWithUnnamedSubscript : ProtocolWithReadWriteSubscript {
  subscript(_: Int) -> Int { get { } set { } }
}


/// <rdar://problem/26408353> crash when overriding internal property with
/// public property

public class BaseClassWithInternalProperty {
  var x: () = ()
}

public class DerivedClassWithPublicProperty : BaseClassWithInternalProperty {
  public override var x: () {
    didSet {}
  }
}

// CHECK-LABEL: sil hidden [transparent] @_T010properties29BaseClassWithInternalPropertyC1xytvg

// CHECK-LABEL: sil [transparent] [serialized] @_T010properties30DerivedClassWithPublicPropertyC1xytvg
// CHECK:       bb0([[SELF:%.*]] : $DerivedClassWithPublicProperty):
// CHECK:         [[SELF_COPY:%.*]] = copy_value [[SELF]] : $DerivedClassWithPublicProperty
// CHECK-NEXT:    [[SUPER:%.*]] = upcast [[SELF_COPY]] : $DerivedClassWithPublicProperty to $BaseClassWithInternalProperty
// CHECK-NEXT:    [[BORROWED_SUPER:%.*]] = begin_borrow [[SUPER]]
// CHECK-NEXT:    [[DOWNCAST_BORROWED_SUPER:%.*]] = unchecked_ref_cast [[BORROWED_SUPER]] : $BaseClassWithInternalProperty to $DerivedClassWithPublicProperty
// CHECK-NEXT:    [[METHOD:%.*]] = super_method [[DOWNCAST_BORROWED_SUPER]] : $DerivedClassWithPublicProperty, #BaseClassWithInternalProperty.x!getter.1 : (BaseClassWithInternalProperty) -> () -> (), $@convention(method) (@guaranteed BaseClassWithInternalProperty) -> ()
// CHECK-NEXT:    end_borrow [[BORROWED_SUPER]] from [[SUPER]]
// CHECK-NEXT:    [[RESULT:%.*]] = apply [[METHOD]]([[SUPER]]) : $@convention(method) (@guaranteed BaseClassWithInternalProperty) -> ()
// CHECK-NEXT:    destroy_value [[SUPER]] : $BaseClassWithInternalProperty
// CHECK: } // end sil function '_T010properties30DerivedClassWithPublicPropertyC1xytvg'

// Make sure that we can handle this AST:
// (load_expr
//   (open_existential_expr
//     (opaque_expr A)
//     ...
//     (load_expr
//        (opaque_expr  ))))

class ReferenceType {
  var p: NonmutatingProtocol
  init(p: NonmutatingProtocol) { self.p = p }
}

protocol NonmutatingProtocol {
  var x: Int { get nonmutating set }
}

// sil hidden @_T010properties19overlappingLoadExpryAA13ReferenceTypeCz1c_tF : $@convention(thin) (@inout ReferenceType) -> () {
// CHECK:        [[RESULT:%.*]] = alloc_stack $Int
// CHECK-NEXT:   [[UNINIT:%.*]] = mark_uninitialized [var] [[RESULT]] : $*Int
// CHECK-NEXT:   [[C_INOUT:%.*]] = begin_access [read] [unknown] %0 : $*ReferenceType
// CHECK-NEXT:   [[C:%.*]] = load [copy] [[C_INOUT:%.*]] : $*ReferenceType
// CHECK-NEXT:   end_access [[C_INOUT]] : $*ReferenceType
// CHECK-NEXT:   [[C_FIELD_BOX:%.*]] = alloc_stack $NonmutatingProtocol
// CHECK-NEXT:   [[GETTER:%.*]] = class_method [[C]] : $ReferenceType, #ReferenceType.p!getter.1 : (ReferenceType) -> () -> NonmutatingProtocol, $@convention(method) (@guaranteed ReferenceType) -> @out NonmutatingProtocol
// CHECK-NEXT:   apply [[GETTER]]([[C_FIELD_BOX]], [[C]]) : $@convention(method) (@guaranteed ReferenceType) -> @out NonmutatingProtocol
// CHECK-NEXT:   destroy_value [[C]] : $ReferenceType
// CHECK-NEXT:   [[C_FIELD_PAYLOAD:%.*]] = open_existential_addr immutable_access [[C_FIELD_BOX]] : $*NonmutatingProtocol to $*@opened("{{.*}}") NonmutatingProtocol
// CHECK-NEXT:   [[C_FIELD_COPY:%.*]] = alloc_stack $@opened("{{.*}}") NonmutatingProtocol
// CHECK-NEXT:   copy_addr [[C_FIELD_PAYLOAD]] to [initialization] [[C_FIELD_COPY]] : $*@opened("{{.*}}") NonmutatingProtocol
// CHECK-NEXT:   [[GETTER:%.*]] = witness_method $@opened("{{.*}}") NonmutatingProtocol, #NonmutatingProtocol.x!getter.1 : <Self where Self : NonmutatingProtocol> (Self) -> () -> Int, [[C_FIELD_PAYLOAD]] : $*@opened("{{.*}}") NonmutatingProtocol : $@convention(witness_method: NonmutatingProtocol) <τ_0_0 where τ_0_0 : NonmutatingProtocol> (@in_guaranteed τ_0_0) -> Int
// CHECK-NEXT:   [[RESULT_VALUE:%.*]] = apply [[GETTER]]<@opened("{{.*}}") NonmutatingProtocol>([[C_FIELD_COPY]]) : $@convention(witness_method: NonmutatingProtocol) <τ_0_0 where τ_0_0 : NonmutatingProtocol> (@in_guaranteed τ_0_0) -> Int
// CHECK-NEXT:   destroy_addr [[C_FIELD_COPY]] : $*@opened("{{.*}}") NonmutatingProtocol
// CHECK-NEXT:   assign [[RESULT_VALUE]] to [[UNINIT]]
// CHECK-NEXT:   destroy_addr [[C_FIELD_BOX]]
// CHECK-NEXT:   dealloc_stack [[C_FIELD_COPY]] : $*@opened("{{.*}}") NonmutatingProtocol
// CHECK-NEXT:   dealloc_stack [[C_FIELD_BOX]] : $*NonmutatingProtocol
// CHECK-NEXT:   dealloc_stack [[RESULT]] : $*Int
// CHECK-NEXT:   tuple ()
// CHECK-NEXT:   return

func overlappingLoadExpr(c: inout ReferenceType) {
  _ = c.p.x
}
