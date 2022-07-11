// RUN: %target-swift-frontend -emit-ir %s -enable-objc-interop -import-objc-header %S/Inputs/enum-anon.h | %FileCheck -check-prefix=CHECK -check-prefix=CHECK-%target-runtime %s

func verifyIsInt(_: inout Int) { }
func verifyIsInt32(_: inout Int32) { }
func verifyIsUInt32(_: inout UInt32) { }

var a = Constant2
var b = VarConstant2
var c = SR2511().y

verifyIsInt(&a)
#if !os(Windows)
verifyIsUInt32(&b)
verifyIsUInt32(&c)
#else
verifyIsInt32(&b)
verifyIsInt32(&c)
#endif

// CHECK: %TSo6SR2511V = type <{ %Ts5Int32V, [[ENUM_TYPE:%Ts5Int32V|%Ts6UInt32V]], %Ts5Int32V }>
// CHECK-LABEL: define{{.*}} i32 @"$s4main6testIR1xs5Int32VSPySo6SR2511VG_tF"(
public func testIR(x: UnsafePointer<SR2511>) -> CInt {
  // CHECK: store i32 1, i32* getelementptr inbounds ([[ENUM_TYPE]], [[ENUM_TYPE]]* bitcast (i32* @global to [[ENUM_TYPE]]*), i32 0, i32 0), align 4
  global = VarConstant2

#if _runtime(_ObjC)
  // CHECK-objc: store i16 1, i16* getelementptr inbounds (%Ts6UInt16V, %Ts6UInt16V* bitcast (i16* @usGlobal to %Ts6UInt16V*), i32 0, i32 0), align 2
  usGlobal = USVarConstant2
#endif

  // Force the definition of the type above.
  // CHECK: ret
  return x.pointee.z
} // CHECK-NEXT: {{^}$}}

