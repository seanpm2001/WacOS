// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -sdk %S/Inputs -I %S/Inputs -enable-source-import %s -emit-silgen -emit-verbose-sil | %FileCheck %s

// REQUIRES: objc_interop

import gizmo
import ansible

class Hoozit : Gizmo {
  func typical(_ x: Int, y: Gizmo) -> Gizmo { return y }
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozit7typical{{.*}} : $@convention(objc_method) (Int, Gizmo, Hoozit) -> @autoreleased Gizmo {
  // CHECK: bb0([[X:%.*]] : $Int, [[Y:%.*]] : $Gizmo, [[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[Y]]
  // CHECK-NEXT:   retain [[THIS]]
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Hoozit7typical{{.*}} : $@convention(method) (Int, @owned Gizmo, @guaranteed Hoozit) -> @owned Gizmo
  // CHECK-NEXT:   [[RES:%.*]] = apply [[NATIVE]]([[X]], [[Y]], [[THIS]]) {{.*}} line:[[@LINE-7]]:8:auto_gen
  // CHECK-NEXT:   strong_release [[THIS]] : $Hoozit
  // CHECK-NEXT:   return [[RES]] : $Gizmo{{.*}} line:[[@LINE-9]]:8:auto_gen
  // CHECK-NEXT: }

  // NS_CONSUMES_SELF by inheritance
  override func fork() { }
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozit4fork{{.*}} : $@convention(objc_method) (@owned Hoozit) -> () {
  // CHECK: bb0([[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Hoozit4fork{{.*}} : $@convention(method) (@guaranteed Hoozit) -> ()
  // CHECK-NEXT:   apply [[NATIVE]]([[THIS]])
  // CHECK-NEXT:   strong_release [[THIS]]
  // CHECK-NEXT:   return
  // CHECK-NEXT: }

  // NS_CONSUMED 'gizmo' argument by inheritance
  override class func consume(_ gizmo: Gizmo?) { }
   // CHECK-LABEL: sil hidden [thunk] @_TToZFC11objc_thunks6Hoozit7consume{{.*}} : $@convention(objc_method) (@owned Optional<Gizmo>, @objc_metatype Hoozit.Type) -> () {
  // CHECK: bb0([[GIZMO:%.*]] : $Optional<Gizmo>, [[THIS:%.*]] : $@objc_metatype Hoozit.Type):
  // CHECK-NEXT: [[THICK_THIS:%[0-9]+]] = objc_to_thick_metatype [[THIS]] : $@objc_metatype Hoozit.Type to $@thick Hoozit.Type
  // CHECK:   [[NATIVE:%.*]] = function_ref @_TZFC11objc_thunks6Hoozit7consume{{.*}} : $@convention(method) (@owned Optional<Gizmo>, @thick Hoozit.Type) -> ()
  // CHECK-NEXT:   apply [[NATIVE]]([[GIZMO]], [[THICK_THIS]])
  // CHECK-NEXT:   return
  // CHECK-NEXT: }

  // NS_RETURNS_RETAINED by family (-copy)
  func copyFoo() -> Gizmo { return self }
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozit7copyFoo{{.*}} : $@convention(objc_method) (Hoozit) -> @owned Gizmo
  // CHECK: bb0([[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[THIS]]
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Hoozit7copyFoo{{.*}} : $@convention(method) (@guaranteed Hoozit) -> @owned Gizmo
  // CHECK-NEXT:   [[RES:%.*]] = apply [[NATIVE]]([[THIS]])
  // CHECK:        release [[THIS]]
  // CHECK-NEXT:   return [[RES]]
  // CHECK-NEXT: }

  // Override the normal family conventions to make this non-consuming and
  // returning at +0.
  func initFoo() -> Gizmo { return self }
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozit7initFoo{{.*}} : $@convention(objc_method) (Hoozit) -> @autoreleased Gizmo
  // CHECK: bb0([[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[THIS]]
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Hoozit7initFoo{{.*}} : $@convention(method) (@guaranteed Hoozit) -> @owned Gizmo
  // CHECK-NEXT:   [[RES:%.*]] = apply [[NATIVE]]([[THIS]])
  // CHECK-NEXT:   release [[THIS]]
  // CHECK-NEXT:   return [[RES]]
  // CHECK-NEXT: }

  var typicalProperty: Gizmo
  // -- getter
  // CHECK-LABEL: sil hidden [transparent] [thunk] @_TToFC11objc_thunks6Hoozitg15typicalPropertyCSo5Gizmo : $@convention(objc_method) (Hoozit) -> @autoreleased Gizmo {
  // CHECK: bb0(%0 : $Hoozit):
  // CHECK-NEXT:   strong_retain %0
  // CHECK-NEXT:   // function_ref objc_thunks.Hoozit.typicalProperty.getter
  // CHECK-NEXT:   [[GETIMPL:%.*]] = function_ref @_TFC11objc_thunks6Hoozitg15typicalPropertyCSo5Gizmo
  // CHECK-NEXT:   [[RES:%.*]] = apply [[GETIMPL]](%0)
  // CHECK-NEXT:   strong_release %0
  // CHECK-NEXT:   return [[RES]] : $Gizmo
  // CHECK-NEXT: }
  
  // CHECK-LABEL: sil hidden [transparent] @_TFC11objc_thunks6Hoozitg15typicalPropertyCSo5Gizmo : $@convention(method) (@guaranteed Hoozit) -> @owned Gizmo
  // CHECK: bb0(%0 : $Hoozit):
  // CHECK-NEXT:   debug_value %0
  // CHECK-NEXT:   [[ADDR:%.*]] = ref_element_addr %0 : {{.*}}, #Hoozit.typicalProperty
  // CHECK-NEXT:   [[RES:%.*]] = load [[ADDR]] {{.*}}
  // CHECK-NEXT:   strong_retain [[RES]] : $Gizmo
  // CHECK-NEXT:   return [[RES]]

  // -- setter
  // CHECK-LABEL: sil hidden [transparent] [thunk] @_TToFC11objc_thunks6Hoozits15typicalPropertyCSo5Gizmo : $@convention(objc_method) (Gizmo, Hoozit) -> () {
  // CHECK: bb0([[VALUE:%.*]] : $Gizmo, [[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[VALUE]] : $Gizmo
  // CHECK-NEXT:   retain [[THIS]] : $Hoozit
  // CHECK-NEXT:   // function_ref objc_thunks.Hoozit.typicalProperty.setter
  // CHECK-NEXT:   [[FR:%.*]] = function_ref @_TFC11objc_thunks6Hoozits15typicalPropertyCSo5Gizmo
  // CHECK-NEXT:   [[RES:%.*]] = apply [[FR]](%0, %1)
  // CHECK_NEXT:   return [[RES]] line:[[@LINE-19]]:7:auto_gen

  // CHECK-LABEL: sil hidden [transparent] @_TFC11objc_thunks6Hoozits15typicalPropertyCSo5Gizmo
  // CHECK: bb0(%0 : $Gizmo, %1 : $Hoozit):
  // CHECK:        [[ADDR:%.*]] = ref_element_addr %1 : {{.*}}, #Hoozit.typicalProperty
  // CHECK-NEXT:   assign %0 to [[ADDR]] : $*Gizmo

  // NS_RETURNS_RETAINED getter by family (-copy)
  var copyProperty: Gizmo
  // -- getter
  // CHECK-LABEL: sil hidden [transparent] [thunk] @_TToFC11objc_thunks6Hoozitg12copyPropertyCSo5Gizmo : $@convention(objc_method) (Hoozit) -> @owned Gizmo {
  // CHECK: bb0(%0 : $Hoozit):
  // CHECK-NEXT:   strong_retain %0
  // CHECK-NEXT:   // function_ref objc_thunks.Hoozit.copyProperty.getter
  // CHECK-NEXT:   [[FR:%.*]] = function_ref @_TFC11objc_thunks6Hoozitg12copyPropertyCSo5Gizmo
  // CHECK-NEXT:   [[RES:%.*]] = apply [[FR]](%0)
  // CHECK-NEXT:   strong_release %0
  // CHECK-NEXT:   return [[RES]]
  // CHECK-NEXT: }

  // CHECK-LABEL: sil hidden [transparent] @_TFC11objc_thunks6Hoozitg12copyPropertyCSo5Gizmo
  // CHECK: bb0(%0 : $Hoozit):
  // CHECK:        [[ADDR:%.*]] = ref_element_addr %0 : {{.*}}, #Hoozit.copyProperty
  // CHECK-NEXT:   [[RES:%.*]] = load [[ADDR]]
  // CHECK-NEXT:   retain [[RES]] 
  // CHECK-NEXT:   return [[RES]]

  // -- setter is normal
  // CHECK-LABEL: sil hidden [transparent] [thunk] @_TToFC11objc_thunks6Hoozits12copyPropertyCSo5Gizmo : $@convention(objc_method) (Gizmo, Hoozit) -> () {
  // CHECK: bb0([[VALUE:%.*]] : $Gizmo, [[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[VALUE]]
  // CHECK-NEXT:   retain [[THIS]]
  // CHECK-NEXT:   // function_ref objc_thunks.Hoozit.copyProperty.setter
  // CHECK-NEXT:   [[FR:%.*]] = function_ref @_TFC11objc_thunks6Hoozits12copyPropertyCSo5Gizmo
  // CHECK-NEXT:   [[RES:%.*]] = apply [[FR]](%0, %1)
  // CHECK-NEXT:   strong_release [[THIS]]
  // CHECK-NEXT:   return [[RES]]

  // CHECK-LABEL: sil hidden [transparent] @_TFC11objc_thunks6Hoozits12copyPropertyCSo5Gizmo
  // CHECK:        bb0(%0 : $Gizmo, %1 : $Hoozit):
  // CHECK:   [[ADDR:%.*]] = ref_element_addr %1 : {{.*}}, #Hoozit.copyProperty
  // CHECK-NEXT:   assign %0 to [[ADDR]]

  var roProperty: Gizmo { return self }
  // -- getter
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozitg10roPropertyCSo5Gizmo : $@convention(objc_method) (Hoozit) -> @autoreleased Gizmo {
  // CHECK: bb0([[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[THIS]]
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Hoozitg10roPropertyCSo5Gizmo : $@convention(method) (@guaranteed Hoozit) -> @owned Gizmo
  // CHECK-NEXT:   [[RES:%.*]] = apply [[NATIVE]]([[THIS]])
  // CHECK-NEXT:   release [[THIS]] : $Hoozit
  // CHECK-NEXT:   return [[RES]] : $Gizmo
  // CHECK-NEXT: }

  // -- no setter
  // CHECK-NOT: sil hidden [thunk] @_TToFC11objc_thunks6Hoozits10roPropertyCSo5Gizmo

  var rwProperty: Gizmo {
    get {
      return self
    }
    set {}
  }
  // -- getter
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozitg10rwPropertyCSo5Gizmo : $@convention(objc_method) (Hoozit) -> @autoreleased Gizmo 

  // -- setter
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozits10rwPropertyCSo5Gizmo : $@convention(objc_method) (Gizmo, Hoozit) -> () {
  // CHECK: bb0([[VALUE:%.*]] : $Gizmo, [[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[VALUE]]
  // CHECK-NEXT:   retain [[THIS]]
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Hoozits10rwPropertyCSo5Gizmo : $@convention(method) (@owned Gizmo, @guaranteed Hoozit) -> ()
  // CHECK-NEXT:   apply [[NATIVE]]([[VALUE]], [[THIS]])
  // CHECK-NEXT:   release [[THIS]]
  // CHECK-NEXT:   return
  // CHECK-NEXT: }

  var copyRWProperty: Gizmo {
    get {
      return self
    }
    set {}
  }
  // -- getter
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozitg14copyRWPropertyCSo5Gizmo : $@convention(objc_method) (Hoozit) -> @owned Gizmo {
  // CHECK: bb0([[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[THIS]]
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Hoozitg14copyRWPropertyCSo5Gizmo : $@convention(method) (@guaranteed Hoozit) -> @owned Gizmo
  // CHECK-NEXT:   [[RES:%.*]] = apply [[NATIVE]]([[THIS]])
  // CHECK-NEXT:   release [[THIS]]
  // CHECK-NOT:    return
  // CHECK-NEXT:   return [[RES]]
  // CHECK-NEXT: }

  // -- setter is normal
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozits14copyRWPropertyCSo5Gizmo : $@convention(objc_method) (Gizmo, Hoozit) -> () {
  // CHECK: bb0([[VALUE:%.*]] : $Gizmo, [[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[VALUE]]
  // CHECK-NEXT:   retain [[THIS]]
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Hoozits14copyRWPropertyCSo5Gizmo : $@convention(method) (@owned Gizmo, @guaranteed Hoozit) -> ()
  // CHECK-NEXT:   apply [[NATIVE]]([[VALUE]], [[THIS]])
  // CHECK-NEXT:   release [[THIS]]
  // CHECK-NEXT:   return
  // CHECK-NEXT: }

  var initProperty: Gizmo
  // -- getter
  // CHECK-LABEL: sil hidden [transparent] [thunk] @_TToFC11objc_thunks6Hoozitg12initPropertyCSo5Gizmo : $@convention(objc_method) (Hoozit) -> @autoreleased Gizmo {
  // CHECK: bb0([[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[THIS]]
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Hoozitg12initPropertyCSo5Gizmo : $@convention(method) (@guaranteed Hoozit) -> @owned Gizmo
  // CHECK-NEXT:   [[RES:%.*]] = apply [[NATIVE]]([[THIS]])
  // CHECK-NEXT:   release [[THIS]]
  // CHECK-NEXT:   return [[RES]]
  // CHECK-NEXT: }

  // -- setter
  // CHECK-LABEL: sil hidden [transparent] [thunk] @_TToFC11objc_thunks6Hoozits12initPropertyCSo5Gizmo : $@convention(objc_method) (Gizmo, Hoozit) -> () {
  // CHECK: bb0([[VALUE:%.*]] : $Gizmo, [[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[VALUE]]
  // CHECK-NEXT:   retain [[THIS]]
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Hoozits12initPropertyCSo5Gizmo : $@convention(method) (@owned Gizmo, @guaranteed Hoozit) -> ()
  // CHECK-NEXT:   apply [[NATIVE]]([[VALUE]], [[THIS]])
  // CHECK-NEXT:   release [[THIS]]
  // CHECK-NEXT:   return
  // CHECK-NEXT: }

  var propComputed: Gizmo {
    @objc(initPropComputedGetter) get { return self }
    @objc(initPropComputedSetter:) set {}
  }
  // -- getter
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozitg12propComputedCSo5Gizmo : $@convention(objc_method) (Hoozit) -> @autoreleased Gizmo {
  // CHECK: bb0([[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[THIS]]
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Hoozitg12propComputedCSo5Gizmo : $@convention(method) (@guaranteed Hoozit) -> @owned Gizmo
  // CHECK-NEXT:   [[RES:%.*]] = apply [[NATIVE]]([[THIS]])
  // CHECK-NEXT:   release [[THIS]]
  // CHECK-NEXT:   return [[RES]]
  // CHECK-NEXT: }

  // -- setter
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozits12propComputedCSo5Gizmo : $@convention(objc_method) (Gizmo, Hoozit) -> () {
  // CHECK: bb0([[VALUE:%.*]] : $Gizmo, [[THIS:%.*]] : $Hoozit):
  // CHECK-NEXT:   retain [[VALUE]]
  // CHECK-NEXT:   retain [[THIS]]
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Hoozits12propComputedCSo5Gizmo : $@convention(method) (@owned Gizmo, @guaranteed Hoozit) -> ()
  // CHECK-NEXT:   apply [[NATIVE]]([[VALUE]], [[THIS]])
  // CHECK-NEXT:   release [[THIS]]
  // CHECK-NEXT:   return
  // CHECK-NEXT: }

  // Don't export generics to ObjC yet
  func generic<T>(_ x: T) {}
  // CHECK-NOT: sil hidden [thunk] @_TToFC11objc_thunks6Hoozit7generic{{.*}}

  // Constructor.
  // CHECK-LABEL: sil hidden @_TFC11objc_thunks6Hoozitc{{.*}} : $@convention(method) (Int, @owned Hoozit) -> @owned Hoozit {
  // CHECK: [[SELF_BOX:%[0-9]+]] = alloc_box $Hoozit
  // CHECK: [[PB:%.*]] = project_box [[SELF_BOX]]
  // CHECK: [[SELFMUI:%[0-9]+]] = mark_uninitialized [derivedself] [[PB]]
  // CHECK: [[GIZMO:%[0-9]+]] = upcast [[SELF:%[0-9]+]] : $Hoozit to $Gizmo
  // CHECK: [[SUPERMETHOD:%[0-9]+]] = super_method [volatile] [[SELF]] : $Hoozit, #Gizmo.init!initializer.1.foreign : (Gizmo.Type) -> (Int) -> Gizmo! , $@convention(objc_method) (Int, @owned Gizmo) -> @owned ImplicitlyUnwrappedOptional<Gizmo>
  // CHECK-NEXT: [[SELF_REPLACED:%[0-9]+]] = apply [[SUPERMETHOD]](%0, [[X:%[0-9]+]]) : $@convention(objc_method) (Int, @owned Gizmo) -> @owned ImplicitlyUnwrappedOptional<Gizmo>
  // CHECK-NOT: unconditional_checked_cast downcast [[SELF_REPLACED]] : $Gizmo to $Hoozit
  // CHECK: unchecked_ref_cast
  // CHECK: return
  override init(bellsOn x : Int) {
    super.init(bellsOn: x)
    other()
  }

  // Subscript
  subscript (i: Int) -> Hoozit {
  // Getter
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozitg9subscript{{.*}} : $@convention(objc_method) (Int, Hoozit) -> @autoreleased Hoozit
  // CHECK: bb0([[I:%[0-9]+]] : $Int, [[SELF:%[0-9]+]] : $Hoozit):
  // CHECK-NEXT:   strong_retain [[SELF]] : $Hoozit
  // CHECK: [[NATIVE:%[0-9]+]] = function_ref @_TFC11objc_thunks6Hoozitg9subscript{{.*}} : $@convention(method) (Int, @guaranteed Hoozit) -> @owned Hoozit
  // CHECK-NEXT: [[RESULT:%[0-9]+]] = apply [[NATIVE]]([[I]], [[SELF]]) : $@convention(method) (Int, @guaranteed Hoozit) -> @owned Hoozit
  // CHECK-NEXT: strong_release [[SELF]]
  // CHECK-NEXT: return [[RESULT]] : $Hoozit
  get {
    return self
  }

  // Setter
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozits9subscript{{.*}} : $@convention(objc_method) (Hoozit, Int, Hoozit) -> ()
  // CHECK: bb0([[SELF:%[0-9]+]] : $Hoozit, [[I:%[0-9]+]] : $Int, [[VALUE:%[0-9]+]] : $Hoozit):
  // CHECK-NEXT: strong_retain [[SELF]] : $Hoozit
  // CHECK_NEXT: strong_retain [[VALUE]] : $Hoozit
  // CHECK: [[NATIVE:%[0-9]+]] = function_ref @_TFC11objc_thunks6Hoozits9subscript{{.*}} : $@convention(method) (@owned Hoozit, Int, @guaranteed Hoozit) -> ()
  // CHECK-NEXT: [[RESULT:%[0-9]+]] = apply [[NATIVE]]([[SELF]], [[I]], [[VALUE]]) : $@convention(method) (@owned Hoozit, Int, @guaranteed Hoozit) -> ()
  // CHECK-NEXT: strong_release [[VALUE]]
  // CHECK-NEXT: return [[RESULT]] : $()
  set {}
  }
}

class Wotsit<T> : Gizmo {
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Wotsit5plain{{.*}} : $@convention(objc_method) <T> (Wotsit<T>) -> () {
  // CHECK: bb0([[SELF:%.*]] : $Wotsit<T>):
  // CHECK-NEXT: strong_retain [[SELF]] : $Wotsit<T>
  // CHECK-NEXT: // function_ref
  // CHECK-NEXT: [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Wotsit5plain{{.*}} : $@convention(method) <τ_0_0> (@guaranteed Wotsit<τ_0_0>) -> ()
  // CHECK-NEXT: [[RESULT:%.*]] = apply [[NATIVE]]<T>([[SELF]]) : $@convention(method) <τ_0_0> (@guaranteed Wotsit<τ_0_0>) -> ()
  // CHECK-NEXT: strong_release [[SELF]] : $Wotsit<T>
  // CHECK-NEXT: return [[RESULT]] : $()
  // CHECK-NEXT: }
  func plain() { }

  func generic<U>(_ x: U) {}

  var property : T

  init(t: T) {
    self.property = t
    super.init()
  }

  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Wotsitg11descriptionSS : $@convention(objc_method) <T> (Wotsit<T>) -> @autoreleased NSString {
  // CHECK: bb0([[SELF:%.*]] : $Wotsit<T>):
  // CHECK-NEXT:   strong_retain [[SELF]] : $Wotsit<T>
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[NATIVE:%.*]] = function_ref @_TFC11objc_thunks6Wotsitg11descriptionSS : $@convention(method) <τ_0_0> (@guaranteed Wotsit<τ_0_0>) -> @owned String
  // CHECK-NEXT:   [[RESULT:%.*]] = apply [[NATIVE:%.*]]<T>([[SELF]]) : $@convention(method) <τ_0_0> (@guaranteed Wotsit<τ_0_0>) -> @owned String
  // CHECK-NEXT:   strong_release [[SELF]] : $Wotsit<T>
  // CHECK-NEXT:   // function_ref
  // CHECK-NEXT:   [[BRIDGE:%.*]] = function_ref @_TFE10FoundationSS19_bridgeToObjectiveCfT_CSo8NSString
  // CHECK-NEXT:   [[NSRESULT:%.*]] = apply [[BRIDGE]]([[RESULT]]) : $@convention(method) (@guaranteed String) -> @owned NSString
  // CHECK-NEXT:   release_value [[RESULT]]
  // CHECK-NEXT:   return [[NSRESULT]] : $NSString
  // CHECK-NEXT: }
  override var description : String {
    return "Hello, world."
  }

  // Ivar destroyer
  // CHECK: sil hidden @_TToF{{.*}}WotsitE

  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6WotsitcfT_GSQGS0_x__ : $@convention(objc_method) <T> (@owned Wotsit<T>) -> @owned ImplicitlyUnwrappedOptional<Wotsit<T>>

  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6WotsitcfT7bellsOnSi_GSQGS0_x__ : $@convention(objc_method) <T> (Int, @owned Wotsit<T>) -> @owned ImplicitlyUnwrappedOptional<Wotsit<T>>
}

// CHECK-NOT: sil hidden [thunk] @_TToF{{.*}}Wotsit{{.*}}

// Extension initializers, properties and methods need thunks too.
extension Hoozit {
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozitc{{.*}} : $@convention(objc_method) (Int, @owned Hoozit) -> @owned Hoozit
  dynamic convenience init(int i: Int) { self.init(bellsOn: i) }

  // CHECK-LABEL: sil hidden @_TFC11objc_thunks6Hoozitc{{.*}} : $@convention(method) (Double, @owned Hoozit) -> @owned Hoozit
  convenience init(double d: Double) { 
    // CHECK: [[SELF_BOX:%[0-9]+]] = alloc_box $Hoozit
    // CHECK: [[PB:%.*]] = project_box [[SELF_BOX]]
    // CHECK: [[SELFMUI:%[0-9]+]] = mark_uninitialized [delegatingself] [[PB]]
    // CHECK: [[X_BOX:%[0-9]+]] = alloc_box $X
    var x = X()
    // CHECK: [[CTOR:%[0-9]+]] = class_method [volatile] [[SELF:%[0-9]+]] : $Hoozit, #Hoozit.init!initializer.1.foreign : (Hoozit.Type) -> (Int) -> Hoozit , $@convention(objc_method) (Int, @owned Hoozit) -> @owned Hoozit
    // CHECK: [[NEW_SELF:%[0-9]+]] = apply [[CTOR]]
    // CHECK: store [[NEW_SELF]] to [[SELFMUI]] : $*Hoozit
    // CHECK: [[NONNULL:%[0-9]+]] = is_nonnull [[NEW_SELF]] : $Hoozit
    // CHECK-NEXT: cond_br [[NONNULL]], [[NONNULL_BB:bb[0-9]+]], [[NULL_BB:bb[0-9]+]]
    // CHECK: [[NULL_BB]]:
    // CHECK-NEXT: strong_release [[X_BOX]] : $@box X
    // CHECK-NEXT: br [[EPILOG_BB:bb[0-9]+]]

    // CHECK: [[NONNULL_BB]]:
    // CHECK:   [[OTHER_REF:%[0-9]+]] = function_ref @_TF11objc_thunks5otherFT_T_ : $@convention(thin) () -> ()
    // CHECK-NEXT: apply [[OTHER_REF]]() : $@convention(thin) () -> ()
    // CHECK-NEXT: strong_release [[X_BOX]] : $@box X
    // CHECK-NEXT: br [[EPILOG_BB]]
    
    // CHECK: [[EPILOG_BB]]:
    // CHECK-NOT: super_method
    // CHECK: return
    self.init(int:Int(d))
    other()
  }

  func foof() {}
  // CHECK-LABEL: sil hidden [thunk] @_TToFC11objc_thunks6Hoozit4foof{{.*}} : $@convention(objc_method) (Hoozit) -> () {

  var extensionProperty: Int { return 0 }
  // CHECK-LABEL: sil hidden  @_TFC11objc_thunks6Hoozitg17extensionPropertySi : $@convention(method) (@guaranteed Hoozit) -> Int
}

// Calling objc methods of subclass should go through native entry points
func useHoozit(_ h: Hoozit) {
// sil @_TF11objc_thunks9useHoozitFT1hC11objc_thunks6Hoozit_T_
  // In the class decl, gets dynamically dispatched
  h.fork()
  // CHECK: class_method {{%.*}} : {{.*}}, #Hoozit.fork!1 :

  // In an extension, 'dynamic' was inferred.
  h.foof()
  // CHECK: class_method [volatile] {{%.*}} : {{.*}}, #Hoozit.foof!1.foreign
}

func useWotsit(_ w: Wotsit<String>) {
// sil @_TF11objc_thunks9useWotsitFT1wGCSo6WotsitSS__T_
  w.plain()
  // CHECK: class_method {{%.*}} : {{.*}}, #Wotsit.plain!1 :
  w.generic(2)
  // CHECK: class_method {{%.*}} : {{.*}}, #Wotsit.generic!1 :

  // Inherited methods only have objc entry points
  w.clone()
  // CHECK: class_method [volatile] {{%.*}} : {{.*}}, #Gizmo.clone!1.foreign
}

func other() { }

class X { }

// CHECK-LABEL: sil hidden @_TF11objc_thunks8property
func property(_ g: Gizmo) -> Int {
  // CHECK: class_method [volatile] %0 : $Gizmo, #Gizmo.count!getter.1.foreign
  return g.count
}

// CHECK-LABEL: sil hidden @_TF11objc_thunks13blockProperty
func blockProperty(_ g: Gizmo) {
  // CHECK: class_method [volatile] %0 : $Gizmo, #Gizmo.block!setter.1.foreign
  g.block = { }
  // CHECK: class_method [volatile] %0 : $Gizmo, #Gizmo.block!getter.1.foreign
  g.block()
}

class DesignatedStubs : Gizmo {
  var i: Int

  override init() { i = 5 }

  // CHECK-LABEL: sil hidden @_TFC11objc_thunks15DesignatedStubsc{{.*}}
  // CHECK: function_ref @_TFs25_unimplementedInitializer
  // CHECK: string_literal utf8 "objc_thunks.DesignatedStubs"
  // CHECK: string_literal utf8 "init(bellsOn:)"
  // CHECK: string_literal utf8 "{{.*}}objc_thunks.swift"
  // CHECK: return

  // CHECK-NOT: sil hidden @_TFCSo15DesignatedStubsc{{.*}}
}

class DesignatedOverrides : Gizmo {
  var i: Int = 5

  // CHECK-LABEL: sil hidden @_TFC11objc_thunks19DesignatedOverridesc{{.*}}
  // CHECK-NOT: return
  // CHECK: function_ref @_TFSiC{{.*}}
  // CHECK: super_method [volatile] [[SELF:%[0-9]+]] : $DesignatedOverrides, #Gizmo.init!initializer.1.foreign : (Gizmo.Type) -> () -> Gizmo! , $@convention(objc_method) (@owned Gizmo) -> @owned ImplicitlyUnwrappedOptional<Gizmo>
  // CHECK: return

  // CHECK-LABEL: sil hidden @_TFC11objc_thunks19DesignatedOverridesc{{.*}}
  // CHECK: function_ref @_TFSiC{{.*}}
  // CHECK: super_method [volatile] [[SELF:%[0-9]+]] : $DesignatedOverrides, #Gizmo.init!initializer.1.foreign : (Gizmo.Type) -> (Int) -> Gizmo! , $@convention(objc_method) (Int, @owned Gizmo) -> @owned ImplicitlyUnwrappedOptional<Gizmo>
  // CHECK: return
}

// Make sure we copy blocks passed in as IUOs - <rdar://problem/22471309>

func registerAnsible() {
  // CHECK: function_ref @_TFF11objc_thunks15registerAnsibleFT_T_U_FGSqFT_T__T_
  // CHECK: function_ref @_TTRXFo_oGSqFT_T____XFdCb_dGSqbT_T____
  Ansible.anseAsync({ completion in completion!() })
}

// FIXME: would be nice if we didn't need to re-abstract as much here.

// CHECK-LABEL: sil shared [transparent] [reabstraction_thunk] @_TTRXFo_oGSqFT_T____XFdCb_dGSqbT_T____ : $@convention(c) @pseudogeneric (@inout_aliasable @block_storage @callee_owned (@owned Optional<() -> ()>) -> (), Optional<@convention(block) () -> ()>) -> ()
// CHECK: [[HEAP_BLOCK_IUO:%.*]] = copy_block %1
// CHECK: select_enum [[HEAP_BLOCK_IUO]]
// CHECK: bb1:
// CHECK: [[HEAP_BLOCK:%.*]] = unchecked_enum_data [[HEAP_BLOCK_IUO]]
// CHECK: [[BLOCK_THUNK:%.*]] = function_ref @_TTRXFdCb___XFo___
// CHECK: [[BRIDGED_BLOCK:%.*]] = partial_apply [[BLOCK_THUNK]]([[HEAP_BLOCK]])
// CHECK: [[REABS_THUNK:%.*]] = function_ref @_TTRXFo___XFo_iT__iT__
// CHECK: [[REABS_BLOCK:%.*]] = partial_apply [[REABS_THUNK]]([[BRIDGED_BLOCK]])
// CHECK: [[REABS_BLOCK_IUO:%.*]] = enum $Optional<() -> ()>, {{.*}} [[REABS_BLOCK]]
