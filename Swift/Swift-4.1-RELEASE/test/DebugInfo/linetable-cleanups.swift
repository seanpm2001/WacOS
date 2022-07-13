// RUN: %target-swift-frontend %s -emit-ir -g -o - | %FileCheck %s

// TODO: check why this is failing on linux
// REQUIRES: OS=macosx

func markUsed<T>(_ t: T) {}

class Person {
    var name = "No Name"
    var age = 0
}

func main() {
    var person = Person()
    var b = [0,1,13]
    for element in b {
        markUsed("element = \(element)")
    }
    markUsed("Done with the for loop")
// CHECK: call {{.*}}void @_T04main8markUsedyxlF
// CHECK: br label
// CHECK: <label>:
// CHECK: call %Ts16IndexingIteratorVySaySiGG* @_T0s16IndexingIteratorVySaySiGGWh0_(%Ts16IndexingIteratorVySaySiGG* %{{.*}}), !dbg ![[LOOPHEADER_LOC:.*]]
// CHECK: call {{.*}}void @_T04main8markUsedyxlF
// The cleanups should share the line number with the ret stmt.
// CHECK:  call %TSa* @_T0SaySiGWh0_(%TSa* %{{.*}}), !dbg ![[CLEANUPS:.*]]
// CHECK-NEXT:  !dbg ![[CLEANUPS]]
// CHECK-NEXT:  llvm.lifetime.end
// CHECK-NEXT:  load
// CHECK-NEXT:  swift_rt_swift_release
// CHECK-NEXT:  bitcast
// CHECK-NEXT:  llvm.lifetime.end
// CHECK-NEXT:  ret void, !dbg ![[CLEANUPS]]
// CHECK: ![[CLEANUPS]] = !DILocation(line: [[@LINE+1]], column: 1,
}
main()
