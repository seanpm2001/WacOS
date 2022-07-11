// RUN: %swift -disable-legacy-type-info -target thumbv7--windows-itanium -parse-stdlib -parse-as-library -module-name Swift -O -emit-ir %s -o - | %FileCheck %s

// REQUIRES: CODEGENERATOR=ARM

precedencegroup AssignmentPrecedence {
  assignment: true
}

public enum Optional<Wrapped> {
  case none
  case some(Wrapped)
}

public protocol P {
  associatedtype T
}

enum E {
  case a
  case b
}

public struct S : P {
  public typealias T = Optional<S>
  var e = E.a
}

var gg = S()

public func f(s : S) -> (() -> ()) {
  return { gg = s }
}

// CHECK-DAG: @"\01l__swift5_reflection_descriptor" = private constant {{.*}}, section ".sw5cptr$B"
// CHECK-DAG: @"{{.*}}" = {{.*}} c"Sq", {{.*}} section ".sw5tyrf$B"
// CHECK-DAG: @{{[0-9]+}} = {{.*}} c"none\00", section ".sw5rfst$B"
// CHECK-DAG: @{{[0-9]+}} = {{.*}} c"some\00", section ".sw5rfst$B"
// CHECK-DAG: @"$sSqMF" = internal constant {{.*}}, section ".sw5flmd$B"
// CHECK-DAG: @"$ss1SVs1PsMA" = internal constant {{.*}}, section ".sw5asty$B"
// CHECK-DAG: @"$sBoMB" = internal constant {{.*}}, section ".sw5bltn$B"

