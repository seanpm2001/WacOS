// RUN: %target-swift-frontend -import-cf-types -sdk %S/Inputs %s -emit-silgen -o - | %FileCheck %s

// REQUIRES: objc_interop

import CoreCooling

// CHECK: sil hidden @_TF2cf8useEmAllFCSo16CCMagnetismModelT_ :
func useEmAll(_ model: CCMagnetismModel) {
// CHECK: function_ref @CCPowerSupplyGetDefault : $@convention(c) () -> @autoreleased ImplicitlyUnwrappedOptional<CCPowerSupply>
  let power = CCPowerSupplyGetDefault()

// CHECK: function_ref @CCRefrigeratorCreate : $@convention(c) (ImplicitlyUnwrappedOptional<CCPowerSupply>) -> ImplicitlyUnwrappedOptional<Unmanaged<CCRefrigerator>>
  let unmanagedFridge = CCRefrigeratorCreate(power)

// CHECK: function_ref @CCRefrigeratorSpawn : $@convention(c) (ImplicitlyUnwrappedOptional<CCPowerSupply>) -> @owned ImplicitlyUnwrappedOptional<CCRefrigerator>
  let managedFridge = CCRefrigeratorSpawn(power)

// CHECK: function_ref @CCRefrigeratorOpen : $@convention(c) (ImplicitlyUnwrappedOptional<CCRefrigerator>) -> ()
  CCRefrigeratorOpen(managedFridge)

// CHECK: function_ref @CCRefrigeratorCopy : $@convention(c) (ImplicitlyUnwrappedOptional<CCRefrigerator>) -> @owned ImplicitlyUnwrappedOptional<CCRefrigerator>
  let copy = CCRefrigeratorCopy(managedFridge)

// CHECK: function_ref @CCRefrigeratorClone : $@convention(c) (ImplicitlyUnwrappedOptional<CCRefrigerator>) -> @autoreleased ImplicitlyUnwrappedOptional<CCRefrigerator>
  let clone = CCRefrigeratorClone(managedFridge)

// CHECK: function_ref @CCRefrigeratorDestroy : $@convention(c) (@owned ImplicitlyUnwrappedOptional<CCRefrigerator>) -> ()
  CCRefrigeratorDestroy(clone)

// CHECK: class_method [volatile] %0 : $CCMagnetismModel, #CCMagnetismModel.refrigerator!1.foreign : (CCMagnetismModel) -> () -> Unmanaged<CCRefrigerator>! , $@convention(objc_method) (CCMagnetismModel) -> ImplicitlyUnwrappedOptional<Unmanaged<CCRefrigerator>>
  let f0 = model.refrigerator()

// CHECK: class_method [volatile] %0 : $CCMagnetismModel, #CCMagnetismModel.getRefrigerator!1.foreign : (CCMagnetismModel) -> () -> CCRefrigerator! , $@convention(objc_method) (CCMagnetismModel) -> @autoreleased ImplicitlyUnwrappedOptional<CCRefrigerator>
  let f1 = model.getRefrigerator()

// CHECK: class_method [volatile] %0 : $CCMagnetismModel, #CCMagnetismModel.takeRefrigerator!1.foreign : (CCMagnetismModel) -> () -> CCRefrigerator! , $@convention(objc_method) (CCMagnetismModel) -> @owned ImplicitlyUnwrappedOptional<CCRefrigerator>
  let f2 = model.takeRefrigerator()

// CHECK: class_method [volatile] %0 : $CCMagnetismModel, #CCMagnetismModel.borrowRefrigerator!1.foreign : (CCMagnetismModel) -> () -> CCRefrigerator! , $@convention(objc_method) (CCMagnetismModel) -> @autoreleased ImplicitlyUnwrappedOptional<CCRefrigerator>
  let f3 = model.borrowRefrigerator()

// CHECK: class_method [volatile] %0 : $CCMagnetismModel, #CCMagnetismModel.setRefrigerator!1.foreign : (CCMagnetismModel) -> (CCRefrigerator!) -> () , $@convention(objc_method) (ImplicitlyUnwrappedOptional<CCRefrigerator>, CCMagnetismModel) -> ()
  model.setRefrigerator(copy)

// CHECK: class_method [volatile] %0 : $CCMagnetismModel, #CCMagnetismModel.giveRefrigerator!1.foreign : (CCMagnetismModel) -> (CCRefrigerator!) -> () , $@convention(objc_method) (@owned ImplicitlyUnwrappedOptional<CCRefrigerator>, CCMagnetismModel) -> ()
  model.giveRefrigerator(copy)

  // rdar://16846555
  let prop: CCRefrigerator = model.fridgeProp
}

// Ensure that accessors are emitted for fields used as protocol witnesses.
protocol Impedance {
  associatedtype Component
  var real: Component { get }
  var imag: Component { get }
}

extension CCImpedance: Impedance {}

// CHECK-LABEL: sil hidden [transparent] [fragile] [thunk] @_TTWVSC11CCImpedance2cf9ImpedanceS0_FS1_g4realwx9Component
// CHECK-LABEL: sil shared [transparent] [fragile] @_TFVSC11CCImpedanceg4realSd
// CHECK-LABEL: sil hidden [transparent] [fragile] [thunk] @_TTWVSC11CCImpedance2cf9ImpedanceS0_FS1_g4imagwx9Component
// CHECK-LABEL: sil shared [transparent] [fragile] @_TFVSC11CCImpedanceg4imagSd

class MyMagnetism : CCMagnetismModel {
  // CHECK-LABEL: sil hidden [thunk] @_TToFC2cf11MyMagnetism15getRefrigerator{{.*}} : $@convention(objc_method) (MyMagnetism) -> @autoreleased CCRefrigerator
  override func getRefrigerator() -> CCRefrigerator {
    return super.getRefrigerator()
  }

  // CHECK-LABEL: sil hidden [thunk] @_TToFC2cf11MyMagnetism16takeRefrigerator{{.*}} : $@convention(objc_method) (MyMagnetism) -> @owned CCRefrigerator
  override func takeRefrigerator() -> CCRefrigerator {
    return super.takeRefrigerator()
  }

  // CHECK-LABEL: sil hidden [thunk] @_TToFC2cf11MyMagnetism18borrowRefrigerator{{.*}} : $@convention(objc_method) (MyMagnetism) -> @autoreleased CCRefrigerator
  override func borrowRefrigerator() -> CCRefrigerator {
    return super.borrowRefrigerator()
  }
}
