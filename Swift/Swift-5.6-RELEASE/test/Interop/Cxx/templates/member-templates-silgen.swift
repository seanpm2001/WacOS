// RUN: %target-swift-emit-sil %s -I %S/Inputs -enable-cxx-interop | %FileCheck %s

// We can't yet call member functions correctly on Windows (SR-13129).
// XFAIL: OS=windows-msvc

import MemberTemplates

// CHECK-LABEL: sil hidden @$s4main9basicTestyyF : $@convention(thin) () -> ()

// CHECK: [[ADD:%.*]] = function_ref @_ZN18HasMemberTemplates17addSameTypeParamsIiEET_S1_S1_ : $@convention(c) (Int32, Int32, @inout HasMemberTemplates) -> Int32
// CHECK: apply [[ADD]]({{.*}}) : $@convention(c) (Int32, Int32, @inout HasMemberTemplates) -> Int32

// CHECK: [[ADD_TWO_TEMPLATES:%.*]] = function_ref @_ZN18HasMemberTemplates18addMixedTypeParamsIiiEET_S1_T0_ : $@convention(c) (Int32, Int32, @inout HasMemberTemplates) -> Int32 // user: %26
// CHECK: apply [[ADD_TWO_TEMPLATES]]({{.*}}) : $@convention(c) (Int32, Int32, @inout HasMemberTemplates) -> Int32

// CHECK: [[ADD_ALL:%.*]] = function_ref @_ZN18HasMemberTemplates6addAllIiiEEiiT_T0_ : $@convention(c) (Int32, Int32, Int32, @inout HasMemberTemplates) -> Int32 // user: %39
// CHECK: apply [[ADD_ALL]]({{.*}}) : $@convention(c) (Int32, Int32, Int32, @inout HasMemberTemplates) -> Int32

// CHECK: [[DO_NOTHING:%.*]] = function_ref @_ZN18HasMemberTemplates17doNothingConstRefIiEEvRKT_ : $@convention(c) (UnsafePointer<Int32>, @inout HasMemberTemplates) -> () // user: %48
// CHECK: apply [[DO_NOTHING]]({{.*}}) : $@convention(c) (UnsafePointer<Int32>, @inout HasMemberTemplates) -> ()

// CHECK-LABEL: end sil function '$s4main9basicTestyyF'
func basicTest() {
  var i: Int32 = 0
  var obj = HasMemberTemplates()
  obj.addSameTypeParams(i, i)
  obj.addMixedTypeParams(i, i)
  obj.addAll(i, i, i)
  obj.doNothingConstRef(&i)
}

// CHECK-LABEL: sil hidden_external [clang HasMemberTemplates._ZN18HasMemberTemplates17addSameTypeParamsIiEET_S1_S1_] @_ZN18HasMemberTemplates17addSameTypeParamsIiEET_S1_S1_ : $@convention(c) (Int32, Int32, @inout HasMemberTemplates) -> Int32

// CHECK-LABEL: sil hidden_external [clang HasMemberTemplates._ZN18HasMemberTemplates18addMixedTypeParamsIiiEET_S1_T0_] @_ZN18HasMemberTemplates18addMixedTypeParamsIiiEET_S1_T0_ : $@convention(c) (Int32, Int32, @inout HasMemberTemplates) -> Int32

// CHECK-LABEL: sil hidden_external [clang HasMemberTemplates._ZN18HasMemberTemplates6addAllIiiEEiiT_T0_] @_ZN18HasMemberTemplates6addAllIiiEEiiT_T0_ : $@convention(c) (Int32, Int32, Int32, @inout HasMemberTemplates) -> Int32

// CHECK-LABEL: sil hidden_external [clang HasMemberTemplates._ZN18HasMemberTemplates17doNothingConstRefIiEEvRKT_] @_ZN18HasMemberTemplates17doNothingConstRefIiEEvRKT_ : $@convention(c) (UnsafePointer<Int32>, @inout HasMemberTemplates) -> ()

// CHECK-LABEL: sil hidden @$s4main12testSetValueyyF : $@convention(thin) () -> ()

// CHECK: [[SET_VALUE:%.*]] = function_ref @_ZN32TemplateClassWithMemberTemplatesIiE8setValueIlEEvT_ : $@convention(c) (Int, @inout __CxxTemplateInst32TemplateClassWithMemberTemplatesIiE) -> ()
// CHECK: apply [[SET_VALUE]]({{.*}}) : $@convention(c) (Int, @inout __CxxTemplateInst32TemplateClassWithMemberTemplatesIiE) -> ()

// CHECK-LABEL: end sil function '$s4main12testSetValueyyF'
func testSetValue() {
  var w = IntWrapper(11)
  w.setValue(42)
}

// CHECK-LABEL: sil hidden @$s4main17testStaticMembersyyF : $@convention(thin) () -> ()

// CHECK: [[ADD_FN:%.*]] = function_ref @_ZN24HasStaticMemberTemplates3addIlEET_S1_S1_ : $@convention(c) (Int, Int) -> Int
// CHECK: apply [[ADD_FN]]({{.*}}) : $@convention(c) (Int, Int) -> Int

// CHECK: [[ADD_TWO_TEMPLATES_FN:%.*]] = function_ref @_ZN24HasStaticMemberTemplates15addTwoTemplatesIlcEET_S1_T0_ : $@convention(c) (Int, Int8) -> Int
// CHECK: apply [[ADD_TWO_TEMPLATES_FN]]({{.*}}) : $@convention(c) (Int, Int8) -> Int

// CHECK: [[REMOVE_REFERENCE_FN:%.*]] = function_ref @_ZN24HasStaticMemberTemplates15removeReferenceIlEET_RS1_ : $@convention(c) (UnsafeMutablePointer<Int>) -> Int
// CHECK: apply [[REMOVE_REFERENCE_FN]]({{.*}}) : $@convention(c) (UnsafeMutablePointer<Int>) -> Int

// CHECK-LABEL: end sil function '$s4main17testStaticMembersyyF'
func testStaticMembers() {
  var x: Int = 0
  let y: CChar = 0
  HasStaticMemberTemplates.add(x, x)
  HasStaticMemberTemplates.addTwoTemplates(x, y)
  HasStaticMemberTemplates.removeReference(&x)
}

// CHECK: sil hidden_external [clang HasStaticMemberTemplates._ZN24HasStaticMemberTemplates3addIlEET_S1_S1_] @_ZN24HasStaticMemberTemplates3addIlEET_S1_S1_ : $@convention(c) (Int, Int) -> Int

// CHECK: sil hidden_external [clang HasStaticMemberTemplates._ZN24HasStaticMemberTemplates15addTwoTemplatesIlcEET_S1_T0_] @_ZN24HasStaticMemberTemplates15addTwoTemplatesIlcEET_S1_T0_ : $@convention(c) (Int, Int8) -> Int

// CHECK: sil hidden_external [clang HasStaticMemberTemplates._ZN24HasStaticMemberTemplates15removeReferenceIlEET_RS1_] @_ZN24HasStaticMemberTemplates15removeReferenceIlEET_RS1_ : $@convention(c) (UnsafeMutablePointer<Int>) -> Int
