// RUN: %empty-directory(%t)
// RUN: %build-irgen-test-overlays
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t -I %S/../IDE/Inputs/custom-modules) %s -emit-ir | %FileCheck %s
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t -I %S/../IDE/Inputs/custom-modules) %s -emit-ir -O | %FileCheck %s -check-prefix=OPT
import CoreFoundation
import Foundation
import Newtype

// REQUIRES: objc_interop

// Witness table for synthesized ClosedEnums : _ObjectiveCBridgeable.
// CHECK: @_T0SC10ClosedEnumVs21_ObjectiveCBridgeable7NewtypeWP = linkonce_odr

// CHECK-LABEL: define swiftcc %TSo8NSStringC* @_T07newtype14getErrorDomainSC0cD0VyF()
public func getErrorDomain() -> ErrorDomain {
  // CHECK: load %TSo8NSStringC*, %TSo8NSStringC** getelementptr inbounds (%TSC11ErrorDomainV, %TSC11ErrorDomainV* {{.*}}@SNTErrOne
  return .one
}

// CHECK-LABEL: _T07newtype6getFooSo14NSNotificationC4NameVyF
public func getFoo() -> NSNotification.Name {
  return NSNotification.Name.Foo
  // CHECK: load {{.*}} @FooNotification
  // CHECK: ret
}

// CHECK-LABEL: _T07newtype21getGlobalNotificationSSSiF
public func getGlobalNotification(_ x: Int) -> String {
  switch x {
    case 1: return kNotification
    // CHECK: load {{.*}} @kNotification
    case 2: return Notification
    // CHECK: load {{.*}} @Notification
    case 3: return swiftNamedNotification
    // CHECK: load {{.*}} @kSNNotification
    default: return NSNotification.Name.bar.rawValue
    // CHECK: load {{.*}} @kBarNotification
  }
// CHECK: ret
}

// CHECK-LABEL: _T07newtype17getCFNewTypeValueSC0cD0VSb6useVar_tF
public func getCFNewTypeValue(useVar: Bool) -> CFNewType {
  if (useVar) {
    return CFNewType.MyCFNewTypeValue
    // CHECK: load {{.*}} @MyCFNewTypeValue
  } else {
    return FooAudited()
    // CHECK: call {{.*}} @FooAudited()
  }
  // CHECK: ret
}

// CHECK-LABEL: _T07newtype21getUnmanagedCFNewTypes0C0VySo8CFStringCGSb6useVar_tF
public func getUnmanagedCFNewType(useVar: Bool) -> Unmanaged<CFString> {
  if (useVar) {
    return CFNewType.MyCFNewTypeValueUnaudited
    // CHECK: load {{.*}} @MyCFNewTypeValueUnaudited
  } else {
    return FooUnaudited()
    // CHECK: call {{.*}} @FooUnaudited()
  }
  // CHECK: ret
}

public func hasArrayOfClosedEnums(closed: [ClosedEnum]) {
  // Triggers instantiation of ClosedEnum : _ObjectiveCBridgeable
  // witness table.
  print(closed[0])
}

// CHECK-LABEL: _T07newtype11compareABIsyyF
public func compareABIs() {
  let new = getMyABINewType()
  let old = getMyABIOldType()
  takeMyABINewType(new)
  takeMyABIOldType(old)

  takeMyABINewTypeNonNull(new!)
  takeMyABIOldTypeNonNull(old!)

  let newNS = getMyABINewTypeNS()
  let oldNS = getMyABIOldTypeNS()
  takeMyABINewTypeNonNullNS(newNS!)
  takeMyABIOldTypeNonNullNS(oldNS!)

  // Make sure that the calling conventions align correctly, that is we don't
  // have double-indirection or anything else like that
  // CHECK: declare %struct.__CFString* @getMyABINewType()
  // CHECK: declare %struct.__CFString* @getMyABIOldType()
  //
  // CHECK: declare void @takeMyABINewType(%struct.__CFString*)
  // CHECK: declare void @takeMyABIOldType(%struct.__CFString*)
  //
  // CHECK: declare void @takeMyABINewTypeNonNull(%struct.__CFString*)
  // CHECK: declare void @takeMyABIOldTypeNonNull(%struct.__CFString*)
  //
  // CHECK: declare %0* @getMyABINewTypeNS()
  // CHECK: declare %0* @getMyABIOldTypeNS()
  //
  // CHECK: declare void @takeMyABINewTypeNonNullNS(%0*)
  // CHECK: declare void @takeMyABIOldTypeNonNullNS(%0*)
}

// OPT-LABEL: define swiftcc i1 @_T07newtype12compareInitsSbyF
public func compareInits() -> Bool {
  let mf = MyInt(rawValue: 1)
  let mfNoLabel = MyInt(1)
  let res = mf.rawValue == MyInt.one.rawValue 
        && mfNoLabel.rawValue == MyInt.one.rawValue
  // OPT:  [[ONE:%.*]] = load i32, i32*{{.*}}@kMyIntOne{{.*}}, align 4
  // OPT-NEXT: [[COMP:%.*]] = icmp eq i32 [[ONE]], 1

  takesMyInt(mf)
  takesMyInt(mfNoLabel)
  takesMyInt(MyInt(rawValue: kRawInt))
  takesMyInt(MyInt(kRawInt))
  // OPT: tail call void @takesMyInt(i32 1)
  // OPT-NEXT: tail call void @takesMyInt(i32 1)
  // OPT-NEXT: [[RAWINT:%.*]] = load i32, i32*{{.*}} @kRawInt{{.*}}, align 4
  // OPT-NEXT: tail call void @takesMyInt(i32 [[RAWINT]])
  // OPT-NEXT: tail call void @takesMyInt(i32 [[RAWINT]])

  return res
  // OPT-NEXT: ret i1 [[COMP]]
}

// CHECK-LABEL: anchor
// OPT-LABEL: anchor
public func anchor() -> Bool {
  return false
}

class ObjCTest {
  // CHECK-LABEL: define hidden %0* @_T07newtype8ObjCTestC19optionalPassThroughSC11ErrorDomainVSgAGFTo
  // CHECK: [[CASTED:%.+]] = ptrtoint %0* %2 to i{{32|64}}
  // CHECK: [[RESULT:%.+]] = call swiftcc i{{32|64}} @_T07newtype8ObjCTestC19optionalPassThroughSC11ErrorDomainVSgAGF(i{{32|64}} [[CASTED]], %T7newtype8ObjCTestC* swiftself {{%.+}})
  // CHECK: [[OPAQUE_RESULT:%.+]] = inttoptr i{{32|64}} [[RESULT]] to %0*
  // CHECK: ret %0* [[OPAQUE_RESULT]]
  // CHECK: {{^}$}}

  // OPT-LABEL: define hidden %0* @_T07newtype8ObjCTestC19optionalPassThroughSC11ErrorDomainVSgAGFTo
  // OPT: ret %0* %2
  // OPT: {{^}$}}
  @objc func optionalPassThrough(_ ed: ErrorDomain?) -> ErrorDomain? {
    return ed
  }

  // CHECK-LABEL: define hidden i32 @_T07newtype8ObjCTestC18integerPassThroughSC5MyIntVAFFTo
  // CHECK: [[RESULT:%.+]] = call swiftcc i32 @_T07newtype8ObjCTestC18integerPassThroughSC5MyIntVAFF(i32 %2, %T7newtype8ObjCTestC* swiftself {{%.+}})
  // CHECK: ret i32 [[RESULT]]
  // CHECK: {{^}$}}

  // OPT-LABEL: define hidden i32 @_T07newtype8ObjCTestC18integerPassThroughSC5MyIntVAFFTo
  // OPT: ret i32 %2
  // OPT: {{^}$}}
  @objc func integerPassThrough(_ num: MyInt) -> MyInt {
    return num
  }
}

// OPT-LABEL: _T07newtype6mutateyyF
public func mutate() {
  // Check for a mismatch in indirectness of the swift_newtype and the Clang
  // type. These pointers should be passed directly for non-mutating functions,
  // rather than passing a pointer indirectly. I.e. only 1 overall level of
  // indirection for non-mutating, 2 for mutating.
  //
  // OPT: [[TRefAlloca:%.+]] = alloca %struct.T*,
  // OPT: [[TRef:%.+]] = tail call %struct.T* @create_T()
  // OPT: store %struct.T* [[TRef]], %struct.T** [[TRefAlloca]],
  var myT = create_T()

  // OPT: [[TRefConst:%.+]] = tail call %struct.T* @create_ConstT()
  let myConstT = create_ConstT()

  // OPT: tail call void @mutate_TRef_Pointee(%struct.T* [[TRef]])
  myT.mutatePointee()

  // OPT: call void @mutate_TRef(%struct.T** nonnull [[TRefAlloca]])
  myT.mutate()

  // Since myT itself got mutated, now we have to reload from the alloca
  //
  // OPT: [[TRefReloaded:%.+]] = load %struct.T*, %struct.T** [[TRefAlloca]],
  // OPT: call void @mutate_TRef_Pointee(%struct.T* [[TRefReloaded]])
  myT.mutatePointee()

  // OPT: call void @use_ConstT(%struct.T* [[TRefConst]])
  myConstT.use()

  // OPT: ret void
}

// OPT-LABEL: _T07newtype9mutateRefyyF
public func mutateRef() {
  // Check for a mismatch in indirectness of the swift_newtype and the Clang
  // type. These pointer pointers should be passed directly, rather than passing
  // a pointer pointer indirectly. I.e. only 2 overall levels of indirection for
  // non-mutating, 3 for mutating.
  //
  // OPT: [[TRefRefAlloca:%.+]] = alloca %struct.T**,
  // OPT: [[TRefRef:%.+]] = tail call %struct.T** @create_TRef()
  // OPT: store %struct.T** [[TRefRef]], %struct.T*** [[TRefRefAlloca]]
  var myTRef = create_TRef()

  // OPT: [[ConstTRefRef:%.+]] = tail call %struct.T** @create_ConstTRef()
  let myConstTRef = create_ConstTRef()

  // OPT: tail call void @mutate_TRefRef_Pointee(%struct.T** [[TRefRef]])
  myTRef.mutatePointee()

  // OPT: call void @mutate_TRefRef(%struct.T*** nonnull [[TRefRefAlloca]])
  myTRef.mutate()

  // Since myTRef itself got mutated, now we have to reload from the alloca
  //
  // OPT: [[TRefReloaded:%.+]] = load %struct.T**, %struct.T*** [[TRefRefAlloca]]
  // OPT: call void @mutate_TRefRef_Pointee(%struct.T** [[TRefReloaded]])
  myTRef.mutatePointee()

  // OPT: call void @use_ConstTRef(%struct.T** [[ConstTRefRef]])
  myConstTRef.use()

  // OPT: ret void
}


