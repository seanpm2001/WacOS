// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -I %t -emit-module -emit-module-path=%t/resilient_struct.swiftmodule -module-name resilient_struct %S/../Inputs/resilient_struct.swift
// RUN: %target-swift-frontend -I %t -emit-module -emit-module-path=%t/resilient_class.swiftmodule -module-name resilient_class %S/../Inputs/resilient_class.swift
// RUN: %target-swift-frontend -emit-silgen -parse-as-library -I %t %s | %FileCheck %s

import resilient_class

public class Parent {
  public final var finalProperty: String {
    return "Parent.finalProperty"
  }

  public var property: String {
    return "Parent.property"
  }

  public final class var finalClassProperty: String {
    return "Parent.finalProperty"
  }

  public class var classProperty: String {
    return "Parent.property"
  }

  public func methodOnlyInParent() {}
  public final func finalMethodOnlyInParent() {}
  public func method() {}

  public final class func finalClassMethodOnlyInParent() {}
  public class func classMethod() {}
}

public class Child : Parent {
  // CHECK-LABEL: sil @_T05super5ChildC8propertySSvg : $@convention(method) (@guaranteed Child) -> @owned String {
  // CHECK:       bb0([[SELF:%.*]] : $Child):
  // CHECK:         [[SELF_COPY:%.*]] = copy_value [[SELF]]
  // CHECK:         [[CASTED_SELF_COPY:%[0-9]+]] = upcast [[SELF_COPY]] : $Child to $Parent
  // CHECK:         [[SUPER_METHOD:%[0-9]+]] = function_ref @_T05super6ParentC8propertySSvg : $@convention(method) (@guaranteed Parent) -> @owned String
  // CHECK:         [[RESULT:%.*]] = apply [[SUPER_METHOD]]([[CASTED_SELF_COPY]])
  // CHECK:         destroy_value [[CASTED_SELF_COPY]]
  // CHECK:         return [[RESULT]]
  public override var property: String {
    return super.property
  }

  // CHECK-LABEL: sil @_T05super5ChildC13otherPropertySSvg : $@convention(method) (@guaranteed Child) -> @owned String {
  // CHECK:       bb0([[SELF:%.*]] : $Child):
  // CHECK:         [[COPIED_SELF:%.*]] = copy_value [[SELF]]
  // CHECK:         [[CASTED_SELF_COPY:%[0-9]+]] = upcast [[COPIED_SELF]] : $Child to $Parent
  // CHECK:         [[SUPER_METHOD:%[0-9]+]] = function_ref @_T05super6ParentC13finalPropertySSvg
  // CHECK:         [[RESULT:%.*]] = apply [[SUPER_METHOD]]([[CASTED_SELF_COPY]])
  // CHECK:         destroy_value [[CASTED_SELF_COPY]]
  // CHECK:         return [[RESULT]]
  public var otherProperty: String {
    return super.finalProperty
  }
}

public class Grandchild : Child {
  // CHECK-LABEL: sil @_T05super10GrandchildC06onlyInB0yyF
  public func onlyInGrandchild() {
    // CHECK: function_ref @_T05super6ParentC012methodOnlyInB0yyF : $@convention(method) (@guaranteed Parent) -> ()
    super.methodOnlyInParent()
    // CHECK: function_ref @_T05super6ParentC017finalMethodOnlyInB0yyF
    super.finalMethodOnlyInParent()
  }

  // CHECK-LABEL: sil @_T05super10GrandchildC6methodyyF
  public override func method() {
    // CHECK: function_ref @_T05super6ParentC6methodyyF : $@convention(method) (@guaranteed Parent) -> ()
    super.method()
  }
}

public class GreatGrandchild : Grandchild {
  // CHECK-LABEL: sil @_T05super15GreatGrandchildC6methodyyF
  public override func method() {
    // CHECK: function_ref @_T05super10GrandchildC6methodyyF : $@convention(method) (@guaranteed Grandchild) -> ()
    super.method()
  }
}

public class ChildToResilientParent : ResilientOutsideParent {
  // CHECK-LABEL: sil @_T05super22ChildToResilientParentC6methodyyF : $@convention(method) (@guaranteed ChildToResilientParent) -> ()
  public override func method() {
    // CHECK: bb0([[SELF:%.*]] : $ChildToResilientParent):
    // CHECK:   [[COPY_SELF:%.*]] = copy_value [[SELF]]
    // CHECK:   [[UPCAST_SELF:%.*]] = upcast [[COPY_SELF]]
    // CHECK:   [[BORROW_UPCAST_SELF:%.*]] = begin_borrow [[UPCAST_SELF]]
    // CHECK:   [[CAST_BORROW_BACK_TO_BASE:%.*]] = unchecked_ref_cast [[BORROW_UPCAST_SELF]]
    // CHECK:   [[FUNC:%.*]] = super_method [[CAST_BORROW_BACK_TO_BASE]] : $ChildToResilientParent, #ResilientOutsideParent.method!1 : (ResilientOutsideParent) -> () -> (), $@convention(method) (@guaranteed ResilientOutsideParent) -> ()
    // CHECK:   end_borrow [[BORROW_UPCAST_SELF]] from [[UPCAST_SELF]]
    // CHECK:   apply [[FUNC]]([[UPCAST_SELF]])
    super.method()
  }
  // CHECK: } // end sil function '_T05super22ChildToResilientParentC6methodyyF'

  // CHECK-LABEL: sil @_T05super22ChildToResilientParentC11classMethodyyFZ : $@convention(method) (@thick ChildToResilientParent.Type) -> ()
  public override class func classMethod() {
    // CHECK: bb0([[METASELF:%.*]] : $@thick ChildToResilientParent.Type):
    // CHECK:   [[UPCAST_METASELF:%.*]] = upcast [[METASELF]]
    // CHECK:   [[FUNC:%.*]] = super_method [[SELF]] : $@thick ChildToResilientParent.Type, #ResilientOutsideParent.classMethod!1 : (ResilientOutsideParent.Type) -> () -> (), $@convention(method) (@thick ResilientOutsideParent.Type) -> ()
    // CHECK:   apply [[FUNC]]([[UPCAST_METASELF]])
    super.classMethod()
  }
  // CHECK: } // end sil function '_T05super22ChildToResilientParentC11classMethodyyFZ'

  // CHECK-LABEL: sil @_T05super22ChildToResilientParentC11returnsSelfACXDyFZ : $@convention(method) (@thick ChildToResilientParent.Type) -> @owned ChildToResilientParent
  public class func returnsSelf() -> Self {
    // CHECK: bb0([[METASELF:%.*]] : $@thick ChildToResilientParent.Type):
    // CHECK:   [[CAST_METASELF:%.*]] = unchecked_trivial_bit_cast [[METASELF]] : $@thick ChildToResilientParent.Type to $@thick @dynamic_self ChildToResilientParent.Type
    // CHECK:   [[UPCAST_CAST_METASELF:%.*]] = upcast [[CAST_METASELF]] : $@thick @dynamic_self ChildToResilientParent.Type to $@thick ResilientOutsideParent.Type
    // CHECK:   [[FUNC:%.*]] = super_method [[METASELF]] : $@thick ChildToResilientParent.Type, #ResilientOutsideParent.classMethod!1 : (ResilientOutsideParent.Type) -> () -> ()
    // CHECK:   apply [[FUNC]]([[UPCAST_CAST_METASELF]])
    // CHECK: unreachable
    super.classMethod()
  }
  // CHECK: } // end sil function '_T05super22ChildToResilientParentC11returnsSelfACXDyFZ'
}

public class ChildToFixedParent : OutsideParent {
  // CHECK-LABEL: sil @_T05super18ChildToFixedParentC6methodyyF : $@convention(method) (@guaranteed ChildToFixedParent) -> ()
  public override func method() {
    // CHECK: bb0([[SELF:%.*]] : $ChildToFixedParent):
    // CHECK:   [[COPY_SELF:%.*]] = copy_value [[SELF]]
    // CHECK:   [[UPCAST_COPY_SELF:%.*]] = upcast [[COPY_SELF]]
    // CHECK:   [[BORROWED_UPCAST_COPY_SELF:%.*]] = begin_borrow [[UPCAST_COPY_SELF]]
    // CHECK:   [[DOWNCAST_BORROWED_UPCAST_COPY_SELF:%.*]] = unchecked_ref_cast [[BORROWED_UPCAST_COPY_SELF]] : $OutsideParent to $ChildToFixedParent
    // CHECK:   [[FUNC:%.*]] = super_method [[DOWNCAST_BORROWED_UPCAST_COPY_SELF]] : $ChildToFixedParent, #OutsideParent.method!1 : (OutsideParent) -> () -> (), $@convention(method) (@guaranteed OutsideParent) -> ()
    // CHECK:   end_borrow [[BORROWED_UPCAST_COPY_SELF]] from [[UPCAST_COPY_SELF]]
    // CHECK:   apply [[FUNC]]([[UPCAST_COPY_SELF]])
    super.method()
  }
  // CHECK: } // end sil function '_T05super18ChildToFixedParentC6methodyyF'

  // CHECK-LABEL: sil @_T05super18ChildToFixedParentC11classMethodyyFZ : $@convention(method) (@thick ChildToFixedParent.Type) -> ()
  public override class func classMethod() {
    // CHECK: bb0([[SELF:%.*]] : $@thick ChildToFixedParent.Type):
    // CHECK:   [[UPCAST_SELF:%.*]] = upcast [[SELF]]
    // CHECK:   [[FUNC:%.*]] = super_method [[SELF]] : $@thick ChildToFixedParent.Type, #OutsideParent.classMethod!1 : (OutsideParent.Type) -> () -> (), $@convention(method) (@thick OutsideParent.Type) -> ()
    // CHECK:   apply [[FUNC]]([[UPCAST_SELF]])
    super.classMethod()
  }
  // CHECK: } // end sil function '_T05super18ChildToFixedParentC11classMethodyyFZ'

  // CHECK-LABEL: sil @_T05super18ChildToFixedParentC11returnsSelfACXDyFZ : $@convention(method) (@thick ChildToFixedParent.Type) -> @owned ChildToFixedParent
  public class func returnsSelf() -> Self {
    // CHECK: bb0([[SELF:%.*]] : $@thick ChildToFixedParent.Type):
    // CHECK:   [[FIRST_CAST:%.*]] = unchecked_trivial_bit_cast [[SELF]]
    // CHECK:   [[SECOND_CAST:%.*]] = upcast [[FIRST_CAST]]
    // CHECK:   [[FUNC:%.*]] = super_method [[SELF]] : $@thick ChildToFixedParent.Type, #OutsideParent.classMethod!1 : (OutsideParent.Type) -> () -> ()
    // CHECK:   apply [[FUNC]]([[SECOND_CAST]])
    // CHECK:   unreachable
    super.classMethod()
  }
  // CHECK: } // end sil function '_T05super18ChildToFixedParentC11returnsSelfACXDyFZ'
}

public extension ResilientOutsideChild {
  public func callSuperMethod() {
    super.method()
  }

  public class func callSuperClassMethod() {
    super.classMethod()
  }
}

public class GenericBase<T> {
  public func method() {}
}

public class GenericDerived<T> : GenericBase<T> {
  public override func method() {
    // CHECK-LABEL: sil private @_T05super14GenericDerivedC6methodyyFyycfU_ : $@convention(thin) <T> (@guaranteed GenericDerived<T>) -> ()
    // CHECK: upcast {{.*}} : $GenericDerived<T> to $GenericBase<T>
    // CHECK: return
    {
      super.method()
    }()
    // CHECK: } // end sil function '_T05super14GenericDerivedC6methodyyFyycfU_'

    // CHECK-LABEL: sil private @_T05super14GenericDerivedC6methodyyF13localFunctionL_yylF : $@convention(thin) <T> (@guaranteed GenericDerived<T>) -> ()
    // CHECK: upcast {{.*}} : $GenericDerived<T> to $GenericBase<T>
    // CHECK: return
    // CHECK: } // end sil function '_T05super14GenericDerivedC6methodyyF13localFunctionL_yylF'
    func localFunction() {
      super.method()
    }
    localFunction()

    // CHECK-LABEL: sil private @_T05super14GenericDerivedC6methodyyF15genericFunctionL_yqd__r__lF : $@convention(thin) <T><U> (@in U, @guaranteed GenericDerived<T>) -> ()
    // CHECK: upcast {{.*}} : $GenericDerived<T> to $GenericBase<T>
    // CHECK: return
    func genericFunction<U>(_: U) {
      super.method()
    }
    // CHECK: } // end sil function '_T05super14GenericDerivedC6methodyyF15genericFunctionL_yqd__r__lF'
    genericFunction(0)
  }
}
