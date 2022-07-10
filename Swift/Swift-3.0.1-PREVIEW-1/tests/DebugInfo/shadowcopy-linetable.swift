// RUN: %target-swift-frontend %s -emit-ir -g -o - | %FileCheck %s

func markUsed<T>(_ t: T) {}

func foo(_ x: inout Int64) {
  // Make sure the shadow copy is being made in the prologue or (at
  // line 0), but the code to load the value from the inout storage is
  // not.
  // CHECK: %[[X:.*]] = alloca %Vs5Int64*, align {{(4|8)}}
  // CHECK: store %Vs5Int64* %0, %Vs5Int64** %[[X]], align {{(4|8)}}
  // CHECK-SAME: !dbg ![[LOC0:.*]]
  // CHECK-NEXT: call void @llvm.dbg.declare
  // CHECK-NEXT: getelementptr inbounds %Vs5Int64, %Vs5Int64* %0, i32 0, i32 0,
  // CHECK-SAME: !dbg ![[LOC1:.*]]
  // CHECK: ![[LOC0]] = !DILocation(line: 0,
  // CHECK: ![[LOC1]] = !DILocation(line: [[@LINE+1]],
  x = x + 2
}

func main() {
  var x : Int64 = 1
  foo(&x)
  markUsed("break here to see \(x)")
}

main()
