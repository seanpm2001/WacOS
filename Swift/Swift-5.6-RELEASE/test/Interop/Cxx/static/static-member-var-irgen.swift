// RUN: %target-swift-emit-ir -I %S/Inputs -enable-cxx-interop %s | %FileCheck %s

// CHECK: @{{_ZN16WithStaticMember12staticMemberE|"\?staticMember@WithStaticMember@@2HA"}} = external {{(dso_local )?}}global i32, align 4
// CHECK: @{{_ZN26WithIncompleteStaticMember10selfMemberE|"\?selfMember@WithIncompleteStaticMember@@2V1@A"}} = {{external|linkonce_odr}} {{(dso_local )?}}global %class.WithIncompleteStaticMember, align 4

//TODO: This test uses only values of static const members, so it does not need
//to depend on external definitions. However, our code generation pattern loads
//the value dynamically. Instead, we should inline known constants. That would
//allow Swift code to even read the value of WithIncompleteStaticMember::notDefined.
// CHECK: @{{_ZN21WithConstStaticMember7definedE|"\?defined@WithConstStaticMember@@2HB"}} = {{available_externally|linkonce_odr}} {{(dso_local )?}}constant i32 48, {{(comdat, )?}}align 4
// CHECK: @{{_ZN21WithConstStaticMember16definedOutOfLineE|"\?definedOutOfLine@WithConstStaticMember@@2HB"}} = external {{(dso_local )?}}constant i32, align 4

// Make sure we remove constexpr globals after all uses have been inlined.
// CHECK-NOT: _ZN25WithConstexprStaticMember13definedInlineE
// CHECK-NOT: ?definedInline@WithConstexprStaticMember@@2HB
// CHECK-NOT: @_ZN25WithConstexprStaticMember20definedInlineWithArgE
// CHECK-NOT: @_ZN25WithConstexprStaticMember18definedInlineFloatE
// CHECK-NOT: @_ZN25WithConstexprStaticMember23definedInlineFromMethodE

import StaticMemberVar

public func readStaticMember() -> CInt {
  return WithStaticMember.staticMember
}

// CHECK: define {{(protected |dllexport )?}}swiftcc i32 @"$s4main16readStaticMembers5Int32VyF"()
// CHECK: [[VALUE:%.*]] = load i32, i32* getelementptr inbounds (%Ts5Int32V, %Ts5Int32V* bitcast (i32* @{{_ZN16WithStaticMember12staticMemberE|"\?staticMember@WithStaticMember@@2HA"}} to %Ts5Int32V*), i32 0, i32 0), align 4
// CHECK: ret i32 [[VALUE]]

public func writeStaticMember() {
  WithStaticMember.staticMember = -1
}

// CHECK: define {{(protected |dllexport )?}}swiftcc void @"$s4main17writeStaticMemberyyF"() #0
// CHECK: store i32 -1, i32* getelementptr inbounds (%Ts5Int32V, %Ts5Int32V* bitcast (i32* @{{_ZN16WithStaticMember12staticMemberE|"\?staticMember@WithStaticMember@@2HA"}} to %Ts5Int32V*), i32 0, i32 0), align 4

public func readSelfMember() -> WithIncompleteStaticMember {
  return WithIncompleteStaticMember.selfMember
}

// CHECK: define {{(protected |dllexport )?}}swiftcc i32 @"$s4main14readSelfMemberSo020WithIncompleteStaticD0VyF"() #0
// CHECK: [[VALUE:%.*]] = load i32, i32* getelementptr inbounds (%TSo26WithIncompleteStaticMemberV, %TSo26WithIncompleteStaticMemberV* bitcast (%class.WithIncompleteStaticMember* @{{_ZN26WithIncompleteStaticMember1|"\?selfMember@WithIncompleteStaticMember@@2V1@A"}}
// CHECK: ret i32 [[VALUE]]

public func writeSelfMember(_ m: WithIncompleteStaticMember) {
  WithIncompleteStaticMember.selfMember = m
}

// CHECK: define {{(protected |dllexport )?}}swiftcc void @"$s4main15writeSelfMemberyySo020WithIncompleteStaticD0VF"(i32 %0) #0
// CHECK: store i32 %0, i32* getelementptr inbounds (%TSo26WithIncompleteStaticMemberV, %TSo26WithIncompleteStaticMemberV* bitcast (%class.WithIncompleteStaticMember* @{{_ZN26WithIncompleteStaticMember10selfMemberE|"\?selfMember@WithIncompleteStaticMember@@2V1@A"}} to %TSo26WithIncompleteStaticMemberV*), i32 0, i32 0, i32 0), align 4

// TODO: Currently, the generated code would try to load the value from
// a symbol that is not defined. We should inline the value instead.
// public func readNotDefinedConstMember() -> CInt {
//   return WithConstStaticMember.notDefined
// }

public func readDefinedConstMember() -> CInt {
  return WithConstStaticMember.defined
}

// CHECK: define {{(protected |dllexport )?}}swiftcc i32 @"$s4main22readDefinedConstMembers5Int32VyF"() #0
// CHECK: [[VALUE:%.*]] = load i32, i32* getelementptr inbounds (%Ts5Int32V, %Ts5Int32V* bitcast (i32* @{{_ZN21WithConstStaticMember7definedE|"\?defined@WithConstStaticMember@@2HB"}} to %Ts5Int32V*), i32 0, i32 0), align 4
// CHECK: ret i32 [[VALUE]]

public func readDefinedOutOfLineConstMember() -> CInt {
  return WithConstStaticMember.definedOutOfLine
}

// CHECK: define {{(protected |dllexport )?}}swiftcc i32 @"$s4main31readDefinedOutOfLineConstMembers5Int32VyF"() #0
// CHECK: [[VALUE:%.*]] = load i32, i32* getelementptr inbounds (%Ts5Int32V, %Ts5Int32V* bitcast (i32* @{{_ZN21WithConstStaticMember16definedOutOfLineE|"\?definedOutOfLine@WithConstStaticMember@@2HB"}} to %Ts5Int32V*), i32 0, i32 0), align 4
// CHECK: ret i32 [[VALUE]]

public func readConstexprStaticIntMembers() {
  let x = WithConstexprStaticMember.definedInline
  let y = WithConstexprStaticMember.definedInlineWithArg
}

// CHECK-LABEL: define {{(protected |dllexport )?}}swiftcc void @"$s4main29readConstexprStaticIntMembersyyF"()
// CHECK: call swiftcc i32 @"$sSo25WithConstexprStaticMemberV13definedInlines5Int32VvgZ"()
// CHECK: call swiftcc i32 @"$sSo25WithConstexprStaticMemberV013definedInlineA3Args5Int32VvgZ"()
// CHECK: ret void

// CHECK-LABEL: define linkonce_odr {{.*}}swiftcc i32 @"$sSo25WithConstexprStaticMemberV13definedInlines5Int32VvgZ"()
// CHECK-NEXT: entry
// CHECK-NEXT: ret i32 139

// CHECK-LABEL: define linkonce_odr {{.*}}swiftcc i32 @"$sSo25WithConstexprStaticMemberV013definedInlineA3Args5Int32VvgZ"()
// CHECK-NEXT: entry
// CHECK-NEXT: ret i32 42

public func readConstexprStaticFloatMembers() {
  let x = WithConstexprStaticMember.definedInlineFloat
  let y = WithConstexprStaticMember.definedInlineFromMethod
}

// CHECK-LABEL: define {{(protected |dllexport )?}}swiftcc void @"$s4main31readConstexprStaticFloatMembersyyF"()
// CHECK: call swiftcc float @"$sSo25WithConstexprStaticMemberV18definedInlineFloatSfvgZ"()
// CHECK: call swiftcc float @"$sSo25WithConstexprStaticMemberV23definedInlineFromMethodSfvgZ"()
// CHECK: ret void

// CHECK-LABEL: define linkonce_odr {{.*}}swiftcc float @"$sSo25WithConstexprStaticMemberV18definedInlineFloatSfvgZ"()
// CHECK-NEXT: entry
// CHECK-NEXT: ret float 1.390000e+02

// CHECK-LABEL: define linkonce_odr {{.*}}swiftcc float @"$sSo25WithConstexprStaticMemberV23definedInlineFromMethodSfvgZ"()
// CHECK-NEXT: entry
// CHECK-NEXT: ret float 4.200000e+01
