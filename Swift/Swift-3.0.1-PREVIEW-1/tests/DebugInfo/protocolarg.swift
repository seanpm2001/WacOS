// RUN: %target-swift-frontend %s -emit-ir -g -o - | %FileCheck %s

func markUsed<T>(_ t: T) {}
func use<T>(_ t: inout T) {}

public protocol IGiveOutInts {
  func callMe() -> Int64
}

// CHECK: define {{.*}}@_TF11protocolarg16printSomeNumbersFPS_12IGiveOutInts_T_
// CHECK: @llvm.dbg.declare(metadata %P11protocolarg12IGiveOutInts_* %
// CHECK-SAME:              metadata ![[VAR:.*]], metadata ![[EMPTY:.*]])
// CHECK: @llvm.dbg.declare(metadata %P11protocolarg12IGiveOutInts_** %
// CHECK-SAME:              metadata ![[ARG:.*]], metadata ![[DEREF:.*]])

// CHECK: ![[EMPTY]] = !DIExpression()

public func printSomeNumbers(_ gen: IGiveOutInts) {
  var gen = gen
  // CHECK: ![[VAR]] = !DILocalVariable(name: "gen", {{.*}} line: [[@LINE-1]]
  // FIXME: Should be DW_TAG_interface_type
  // CHECK: ![[PT:.*]] = !DICompositeType(tag: DW_TAG_structure_type, name: "IGiveOutInts"
  // CHECK: ![[ARG]] = !DILocalVariable(name: "gen", arg: 1,
  // CHECK-SAME:                        line: [[@LINE-6]], type: ![[PT]]
  // CHECK: ![[DEREF]] = !DIExpression(DW_OP_deref)
  markUsed(gen.callMe())
  use(&gen)
}

