// RUN: %target-swift-ide-test -print-comments -source-filename %s | %FileCheck %s
// REQUIRES: no_asan

class Base {
  func noComments() {}
  // CHECK: Func/Base.noComments {{.*}} DocCommentAsXML=none

  /// Base
  func funcNoDerivedComment() {}
  // CHECK: Func/Base.funcNoDerivedComment {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>funcNoDerivedComment()</Name><USR>s:14swift_ide_test4BaseC20funcNoDerivedCommentyyF</USR><Declaration>func funcNoDerivedComment()</Declaration><CommentParts><Abstract><Para>Base</Para></Abstract></CommentParts></Function>]

  /// Base
  func funcWithDerivedComment() {}
  // CHECK: Func/Base.funcWithDerivedComment {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>funcWithDerivedComment()</Name><USR>s:14swift_ide_test4BaseC22funcWithDerivedCommentyyF</USR><Declaration>func funcWithDerivedComment()</Declaration><CommentParts><Abstract><Para>Base</Para></Abstract></CommentParts></Function>]

  /// Base
  var varNoDerivedComment: Bool {
    return false
  }
  // CHECK: Var/Base.varNoDerivedComment {{.*}} DocCommentAsXML=[<Other file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>varNoDerivedComment</Name><USR>s:14swift_ide_test4BaseC19varNoDerivedCommentSbvp</USR><Declaration>var varNoDerivedComment: Bool { get }</Declaration><CommentParts><Abstract><Para>Base</Para></Abstract></CommentParts></Other>]

  /// Base
  var varWithDerivedComment: Bool {
    return false
  }
  // CHECK: Var/Base.varWithDerivedComment {{.*}} DocCommentAsXML=[<Other file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>varWithDerivedComment</Name><USR>s:14swift_ide_test4BaseC21varWithDerivedCommentSbvp</USR><Declaration>var varWithDerivedComment: Bool { get }</Declaration><CommentParts><Abstract><Para>Base</Para></Abstract></CommentParts></Other>]
}

class Derived : Base {
  override func noComments() {}
  // CHECK: Func/Derived.noComments {{.*}} DocCommentAsXML=none

  override func funcNoDerivedComment() {}
  // CHECK: Func/Derived.funcNoDerivedComment {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>funcNoDerivedComment()</Name><USR>s:14swift_ide_test7DerivedC06funcNoD7CommentyyF</USR><Declaration>override func funcNoDerivedComment()</Declaration><CommentParts><Abstract><Para>Base</Para></Abstract><Discussion><Note><Para>This documentation comment was inherited from <codeVoice>Base</codeVoice>.</Para></Note></Discussion></CommentParts></Function>]

  /// Derived
  override func funcWithDerivedComment() {}
  // CHECK: Func/Derived.funcWithDerivedComment {{.*}} DocCommentAsXML=[<Function file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>funcWithDerivedComment()</Name><USR>s:14swift_ide_test7DerivedC08funcWithD7CommentyyF</USR><Declaration>override func funcWithDerivedComment()</Declaration><CommentParts><Abstract><Para>Derived</Para></Abstract></CommentParts></Function>]

  override var varNoDerivedComment: Bool {
    return false
  }
  // CHECK: Var/Derived.varNoDerivedComment {{.*}} DocCommentAsXML=[<Other file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>varNoDerivedComment</Name><USR>s:14swift_ide_test7DerivedC05varNoD7CommentSbvp</USR><Declaration>override var varNoDerivedComment: Bool { get }</Declaration><CommentParts><Abstract><Para>Base</Para></Abstract><Discussion><Note><Para>This documentation comment was inherited from <codeVoice>Base</codeVoice>.</Para></Note></Discussion></CommentParts></Other>]

  // Derived
  override var varWithDerivedComment : Bool {
    return true
  }
  // CHECK: Var/Derived.varWithDerivedComment {{.*}} DocCommentAsXML=[<Other file="{{.*}}" line="{{.*}}" column="{{.*}}"><Name>varWithDerivedComment</Name><USR>s:14swift_ide_test7DerivedC07varWithD7CommentSbvp</USR><Declaration>override var varWithDerivedComment: Bool { get }</Declaration><CommentParts><Abstract><Para>Base</Para></Abstract><Discussion><Note><Para>This documentation comment was inherited from <codeVoice>Base</codeVoice>.</Para></Note></Discussion></CommentParts></Other>]
}

