// RUN: %target-swift-ide-test -print-comments -source-filename %s | %FileCheck %s

protocol ParentProtocol1 {
  /// ParentProtocol1.onlyParent1()
  func onlyParent1()
  // CHECK: Func/ParentProtocol1.onlyParent1 {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>onlyParent1()</Name><USR>s:FP14swift_ide_test15ParentProtocol111onlyParent1FT_T_</USR><Declaration>func onlyParent1()</Declaration><Abstract><Para>ParentProtocol1.onlyParent1()</Para></Abstract></Function>]

  /// ParentProtocol1.commonParentRequirement()
  func commonParentRequirement()
  // CHECKL: Func/ParentProtocol1.commonParentRequirement {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>commonParentRequirement()</Name><USR>s:FP14swift_ide_test15ParentProtocol123commonParentRequirementFT_T_</USR><Declaration>func commonParentRequirement()</Declaration><Abstract><Para>ParentProtocol1.commonParentRequirement()</Para></Abstract></Function>]

  /// ParentProtocol1.commonRequirementWithDocComment()
  func commonRequirementWithDocComment()
  // CHECK: Func/ParentProtocol1.commonRequirementWithDocComment {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>commonRequirementWithDocComment()</Name><USR>s:FP14swift_ide_test15ParentProtocol131commonRequirementWithDocCommentFT_T_</USR><Declaration>func commonRequirementWithDocComment()</Declaration><Abstract><Para>ParentProtocol1.commonRequirementWithDocComment()</Para></Abstract></Function>]

  /// ParentProtocol1.commonRequirementWithoutDocComment()
  func commonRequirementWithoutDocComment()
  // CHECK: Func/ParentProtocol1.commonRequirementWithoutDocComment {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>commonRequirementWithoutDocComment()</Name><USR>s:FP14swift_ide_test15ParentProtocol134commonRequirementWithoutDocCommentFT_T_</USR><Declaration>func commonRequirementWithoutDocComment()</Declaration><Abstract><Para>ParentProtocol1.commonRequirementWithoutDocComment()</Para></Abstract></Function>]
}

protocol ParentProtocol2 {
  /// ParentProtocol2.onlyParent2()
  func onlyParent2()
  // CHECK: Func/ParentProtocol2.onlyParent2 {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>onlyParent2()</Name><USR>s:FP14swift_ide_test15ParentProtocol211onlyParent2FT_T_</USR><Declaration>func onlyParent2()</Declaration><Abstract><Para>ParentProtocol2.onlyParent2()</Para></Abstract></Function>]

  /// ParentProtocol2.commonParentRequirement()
  func commonParentRequirement()
  // CHECK: Func/ParentProtocol2.commonParentRequirement {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>commonParentRequirement()</Name><USR>s:FP14swift_ide_test15ParentProtocol223commonParentRequirementFT_T_</USR><Declaration>func commonParentRequirement()</Declaration><Abstract><Para>ParentProtocol2.commonParentRequirement()</Para></Abstract></Function>]

  /// ParentProtocol2.commonRequirementWithDocComment()
  func commonRequirementWithDocComment()
  // CHECK: Func/ParentProtocol2.commonRequirementWithDocComment {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>commonRequirementWithDocComment()</Name><USR>s:FP14swift_ide_test15ParentProtocol231commonRequirementWithDocCommentFT_T_</USR><Declaration>func commonRequirementWithDocComment()</Declaration><Abstract><Para>ParentProtocol2.commonRequirementWithDocComment()</Para></Abstract></Function>]

  /// ParentProtocol2.commonRequirementWithoutDocComment()
  func commonRequirementWithoutDocComment()
  // CHECK: Func/ParentProtocol2.commonRequirementWithoutDocComment {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>commonRequirementWithoutDocComment()</Name><USR>s:FP14swift_ide_test15ParentProtocol234commonRequirementWithoutDocCommentFT_T_</USR><Declaration>func commonRequirementWithoutDocComment()</Declaration><Abstract><Para>ParentProtocol2.commonRequirementWithoutDocComment()</Para></Abstract></Function>]
}

protocol ChildProtocol : ParentProtocol1, ParentProtocol2 {
  /// ChildProtocol.commonRequirementWithDocComment()
  func commonRequirementWithDocComment()
  // CHECK: Func/ChildProtocol.commonRequirementWithDocComment {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>commonRequirementWithDocComment()</Name><USR>s:FP14swift_ide_test13ChildProtocol31commonRequirementWithDocCommentFT_T_</USR><Declaration>func commonRequirementWithDocComment()</Declaration><Abstract><Para>ChildProtocol.commonRequirementWithDocComment()</Para></Abstract></Function>]

  // This should show nothing because there are two inherited requirements.
  func commonRequirementWithoutDocComment()
  // CHECK: Func/ChildProtocol.commonRequirementWithoutDocComment {{.*}} DocCommentAsXML=none
}

// Test that ChildProtocol's default implementation for requirements
// come from the right place.
extension ChildProtocol {
  // Should come from ParentProtocol1.
  func onlyParent1() {}
  // CHECK: Func/onlyParent1 {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>onlyParent1()</Name><USR>s:FP14swift_ide_test15ParentProtocol111onlyParent1FT_T_</USR><Declaration>func onlyParent1()</Declaration><Abstract><Para>ParentProtocol1.onlyParent1()</Para></Abstract></Function>]

  // Should come from ParentProtocol2.
  func onlyParent2() {}
  // CHECK: Func/onlyParent2 {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>onlyParent2()</Name><USR>s:FP14swift_ide_test15ParentProtocol211onlyParent2FT_T_</USR><Declaration>func onlyParent2()</Declaration><Abstract><Para>ParentProtocol2.onlyParent2()</Para></Abstract></Function>]

  // Should show nothing because the requirement is in both parents.
  func commonParentRequirement() {}
  // CHECK: Func/commonParentRequirement {{.*}} DocCommentAsXML=none

  // Should show nothing because the requirement is in both parents.
  func commonRequirementWithDocComment() {}
  // CHECK: Func/commonRequirementWithDocComment {{.*}} DocCommentAsXML=none

  // Should show nothing because there are multiple requirements.
  func commonRequirementWithoutDocComment() {}
  // CHECK: Func/commonRequirementWithoutDocComment {{.*}} DocCommentAsXML=none
}

