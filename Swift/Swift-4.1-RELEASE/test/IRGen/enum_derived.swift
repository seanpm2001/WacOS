// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -assume-parsing-unqualified-ownership-sil -emit-module -module-name def_enum -o %t %S/Inputs/def_enum.swift
// RUN: %target-swift-frontend -assume-parsing-unqualified-ownership-sil -I %t -O -primary-file %s -emit-ir | %FileCheck -check-prefix=CHECK -check-prefix=CHECK-NORMAL %s
// RUN: %target-swift-frontend -assume-parsing-unqualified-ownership-sil -I %t -O -primary-file %s -enable-testing -emit-ir | %FileCheck -check-prefix=CHECK -check-prefix=CHECK-TESTABLE %s

import def_enum

// Check if the hashValue and == for an enum (without payload) are generated and
// check if that functions are compiled in an optimal way.

enum E {
  case E0
  case E1
  case E2
  case E3
}

// Check if the == comparison can be compiled to a simple icmp instruction.

// CHECK-NORMAL-LABEL:define hidden swiftcc i1 @_T012enum_derived1EO02__b1_A7_equalsSbAC_ACtFZ(i8, i8)
// CHECK-TESTABLE-LABEL:define{{( protected)?}} swiftcc i1 @_T012enum_derived1EO02__b1_A7_equalsSbAC_ACtFZ(i8, i8)
// CHECK: %2 = icmp eq i8 %0, %1
// CHECK: ret i1 %2

// Check if the hashValue getter can be compiled to a simple zext instruction.

// CHECK-NORMAL-LABEL:define hidden swiftcc i{{.*}} @_T012enum_derived1EO9hashValueSivg(i8)
// CHECK-TESTABLE-LABEL:define{{( protected)?}} swiftcc i{{.*}} @_T012enum_derived1EO9hashValueSivg(i8)
// CHECK: [[R:%.*]] = zext i8 %0 to i{{.*}}
// CHECK: ret i{{.*}} [[R]]

// Derived conformances from extensions
// The actual enums are in Inputs/def_enum.swift

extension def_enum.TrafficLight : Error {}

extension def_enum.Term : Error {}

// CHECK-NORMAL-LABEL: define hidden {{.*}}i64 @_T012enum_derived7PhantomO8rawValues5Int64Vvg(i8, %swift.type* nocapture readnone %T) local_unnamed_addr
// CHECK-TESTABLE-LABEL: define{{( protected)?}} {{.*}}i64 @_T012enum_derived7PhantomO8rawValues5Int64Vvg(i8, %swift.type* nocapture readnone %T)

enum Phantom<T> : Int64 {
  case Up
  case Down
}
