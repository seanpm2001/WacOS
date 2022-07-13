// RUN: %empty-directory(%t)
// RUN: %build-irgen-test-overlays
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) %s -emit-ir | %FileCheck %s

// REQUIRES: CPU=x86_64
// REQUIRES: objc_interop

import gizmo

// CHECK: [[CLASS:%objc_class]] = type
// CHECK: [[TYPE:%swift.type]] = type
// CHECK: [[HOOZIT:%T10objc_super6HoozitC]] = type
// CHECK: [[PARTIAL_APPLY_CLASS:%T10objc_super12PartialApplyC]] = type
// CHECK: [[SUPER:%objc_super]] = type
// CHECK: [[OBJC:%objc_object]] = type
// CHECK: [[GIZMO:%TSo5GizmoC]] = type
// CHECK: [[NSRECT:%TSC6NSRectV]] = type

class Hoozit : Gizmo {
  // CHECK: define hidden swiftcc void @_T010objc_super6HoozitC4frobyyF([[HOOZIT]]* swiftself) {{.*}} {
  override func frob() {
    // CHECK: [[T0:%.*]] = call [[TYPE]]* @_T010objc_super6HoozitCMa()
    // CHECK: [[T1:%.*]] = bitcast [[TYPE]]* [[T0]] to [[CLASS]]*
    // CHECK: store [[CLASS]]* [[T1]], [[CLASS]]** {{.*}}, align 8
    // CHECK: load i8*, i8** @"\01L_selector(frob)"
    // CHECK: call void bitcast (void ()* @objc_msgSendSuper2 to void ([[SUPER]]*, i8*)*)([[SUPER]]* {{.*}}, i8* {{.*}})
    super.frob()
  }
  // CHECK: }

  // CHECK: define hidden swiftcc void @_T010objc_super6HoozitC5runceyyFZ([[TYPE]]* swiftself) {{.*}} {
  override class func runce() {
    // CHECK: store [[CLASS]]* @"OBJC_METACLASS_$__TtC10objc_super6Hoozit", [[CLASS]]** {{.*}}, align 8
    // CHECK: load i8*, i8** @"\01L_selector(runce)"
    // CHECK: call void bitcast (void ()* @objc_msgSendSuper2 to void ([[SUPER]]*, i8*)*)([[SUPER]]* {{.*}}, i8* {{.*}})
    super.runce()
  }
  // CHECK: }

  // CHECK: define hidden swiftcc { double, double, double, double } @_T010objc_super6HoozitC5frameSC6NSRectVyF(%T10objc_super6HoozitC* swiftself) {{.*}} {
  override func frame() -> NSRect {
    // CHECK: [[T0:%.*]] = call [[TYPE]]* @_T010objc_super6HoozitCMa()
    // CHECK: [[T1:%.*]] = bitcast [[TYPE]]* [[T0]] to [[CLASS]]*
    // CHECK: store [[CLASS]]* [[T1]], [[CLASS]]** {{.*}}, align 8
    // CHECK: load i8*, i8** @"\01L_selector(frame)"
    // CHECK: call void bitcast (void ()* @objc_msgSendSuper2_stret to void ([[NSRECT]]*, [[SUPER]]*, i8*)*)([[NSRECT]]* noalias nocapture sret {{.*}}, [[SUPER]]* {{.*}}, i8* {{.*}})
    return NSInsetRect(super.frame(), 2.0, 2.0)
  }
  // CHECK: }

  // CHECK: define hidden swiftcc [[HOOZIT]]* @_T010objc_super6HoozitCACSi1x_tcfc(i64, %T10objc_super6HoozitC* swiftself) {{.*}} {
  init(x:Int) {
    // CHECK: load i8*, i8** @"\01L_selector(init)"
    // CHECK: call [[OPAQUE:.*]]* bitcast (void ()* @objc_msgSendSuper2 to [[OPAQUE:.*]]* (%objc_super*, i8*)*)([[SUPER]]* {{.*}}, i8* {{.*}})
    // CHECK-NOT: @swift_dynamicCastClassUnconditional
    // CHECK: ret
    super.init()
  }
  // CHECK: }

  // CHECK: define hidden swiftcc [[HOOZIT]]* @_T010objc_super6HoozitCACSi1y_tcfc(i64, %T10objc_super6HoozitC* swiftself) {{.*}} {
  init(y:Int) {
    // CHECK: load i8*, i8** @"\01L_selector(initWithBellsOn:)"
    // CHECK: call [[OPAQUE:.*]]* bitcast (void ()* @objc_msgSendSuper2 to [[OPAQUE:.*]]* (%objc_super*, i8*, i64)*)([[SUPER]]* {{.*}}, i8* {{.*}}, i64 {{.*}})
    // CHECK-NOT: swift_dynamicCastClassUnconditional
    // CHECK: ret
    super.init(bellsOn:y)
  }
  // CHECK: }
}

func acceptFn(_ fn: () -> Void) { }

class PartialApply : Gizmo {
  // CHECK: define hidden swiftcc void @_T010objc_super12PartialApplyC4frobyyF([[PARTIAL_APPLY_CLASS]]* swiftself) {{.*}} {
  override func frob() {
    // CHECK: call swiftcc void @_T010objc_super8acceptFnyyycF(i8* bitcast (void (%swift.refcounted*)* [[PARTIAL_FORWARDING_THUNK:@[A-Za-z0-9_]+]] to i8*), %swift.refcounted* %4)
    acceptFn(super.frob)
  }
  // CHECK: }
}

class GenericRuncer<T> : Gizmo {
  var x: Gizmo? = nil
  var y: T?

// Use a constant indirect field access instead of a non-constant direct
// access because the layout dependents on the alignment of y.

// CHECK: define hidden swiftcc i64 @_T010objc_super13GenericRuncerC1xSo5GizmoCSgvg(%T10objc_super13GenericRuncerC* swiftself)
// CHECK:    inttoptr
// CHECK:   [[CAST:%.*]] = bitcast %T10objc_super13GenericRuncerC* %0 to i64*
// CHECK:   [[ISA:%.*]] = load i64, i64* [[CAST]]
// CHECK:   [[ISAMASK:%.*]] = load i64, i64* @swift_isaMask
// CHECK:   [[CLASS:%.*]] = and i64 [[ISA]], [[ISAMASK]]
// CHECK:   [[TY:%.*]] = inttoptr i64 [[CLASS]] to %swift.type*
// CHECK:   [[CAST:%.*]] = bitcast %swift.type* [[TY]] to i64*
// CHECK:   [[OFFSETADDR:%.*]] = getelementptr inbounds i64, i64* [[CAST]], i64 19
// CHECK:   [[FIELDOFFSET:%.*]] = load i64, i64* [[OFFSETADDR]]
// CHECK:   [[BYTEADDR:%.*]] = bitcast %T10objc_super13GenericRuncerC* %0 to i8*
// CHECK:   [[FIELDADDR:%.*]] = getelementptr inbounds i8, i8* [[BYTEADDR]], i64 [[FIELDOFFSET]]
// CHECK:   [[XADDR:%.*]] = bitcast i8* [[FIELDADDR]] to %TSo5GizmoCSg*
// CHECK:   [[OPTIONALADDR:%.*]] = bitcast %TSo5GizmoCSg* [[XADDR]] to i64*
// CHECK:   [[OPTIONAL:%.*]] = load i64, i64* [[OPTIONALADDR]]
// CHECK:   [[OBJ:%.*]] = inttoptr i64 [[OPTIONAL]] to %objc_object*
// CHECK:    call %objc_object* @objc_retain(%objc_object* [[OBJ]]
// CHECK:   ret i64 [[OPTIONAL]]

  // CHECK: define hidden swiftcc void @_T010objc_super13GenericRuncerC5runceyyFZ(%swift.type* swiftself) {{.*}} {
  override class func runce() {
    // CHECK:      [[CLASS:%.*]] = call %swift.type* @_T010objc_super13GenericRuncerCMa(%swift.type* %T)
    // CHECK-NEXT: [[CLASS1:%.*]] = bitcast %swift.type* [[CLASS]] to %objc_class*
    // CHECK-NEXT: [[CLASS2:%.*]] = bitcast %objc_class* [[CLASS1]] to i64*
    // CHECK-NEXT: [[ISA:%.*]] = load i64, i64* [[CLASS2]], align 8
    // CHECK-NEXT: [[ISA_MASK:%.*]] = load i64, i64* @swift_isaMask, align 8
    // CHECK-NEXT: [[ISA_MASKED:%.*]] = and i64 [[ISA]], [[ISA_MASK]]
    // CHECK-NEXT: [[ISA_PTR:%.*]] = inttoptr i64 [[ISA_MASKED]] to %swift.type*
    // CHECK-NEXT: [[METACLASS:%.*]] = bitcast %swift.type* [[ISA_PTR]] to %objc_class*
    // CHECK:      [[METACLASS_ADDR:%.*]] = getelementptr inbounds %objc_super, %objc_super* %objc_super, i32 0, i32 1
    // CHECK-NEXT: store %objc_class* [[METACLASS]], %objc_class** [[METACLASS_ADDR]], align 8
    // CHECK-NEXT: [[SELECTOR:%.*]] = load i8*, i8** @"\01L_selector(runce)", align 8
    // CHECK-NEXT: call void bitcast (void ()* @objc_msgSendSuper2 to void (%objc_super*, i8*)*)(%objc_super* %objc_super, i8* [[SELECTOR]])
    // CHECK-NEXT: ret void
    super.runce()
  }
}

// CHECK: define internal swiftcc void [[PARTIAL_FORWARDING_THUNK]](%swift.refcounted* swiftself) #0 {
// CHECK: call %swift.type* @_T010objc_super12PartialApplyCMa()
// CHECK: @"\01L_selector(frob)"
// CHECK: call void bitcast (void ()* @objc_msgSendSuper2
// CHECK: }
