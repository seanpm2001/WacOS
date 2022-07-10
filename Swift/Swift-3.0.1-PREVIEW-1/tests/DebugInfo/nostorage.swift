// RUN: %target-swift-frontend %s -emit-ir -g -o %t
// RUN: cat %t | %FileCheck %s --check-prefix=CHECK1
// RUN: cat %t | %FileCheck %s --check-prefix=CHECK2
// RUN: cat %t | %FileCheck %s --check-prefix=CHECK3

func used<T>(_ t: T) {}

public class Foo {
    func foo() {
      { [weak self] in
      // CHECK1: call void @llvm.dbg.value(metadata i{{.*}} 0,
      // CHECK1-SAME:                      metadata ![[TYPE:.*]], metadata
      // CHECK1: ![[TYPE]] = !DILocalVariable(name: "type",
      // CHECK1-SAME:                         line: [[@LINE+4]],
      // CHECK1-SAME:                         type: ![[METAFOO:[0-9]+]]
      // CHECK1: ![[METAFOO]] = !DICompositeType(tag: DW_TAG_structure_type,
      // CHECK1-SAME:                            align: 8, flags:
            let type = type(of: self)
            used(type)
        }()
    }
}

struct AStruct {}

// CHECK2: define{{.*}}app
public func app() {
  // No members? No storage! Emitted as a constant 0, because.
  // CHECK2: call void @llvm.dbg.value(metadata i{{.*}} 0,
  // CHECK2-SAME:                      metadata ![[AT:.*]], metadata
  // CHECK2: ![[AT]] = !DILocalVariable(name: "at",{{.*}}line: [[@LINE+1]]
  var at = AStruct()
  
  used(at)
}

public enum empty { case exists }
public let globalvar = empty.exists
// CHECK3: !DIGlobalVariable(name: "globalvar", {{.*}}line: [[@LINE-1]],
// CHECK3-SAME:          isLocal: false, isDefinition: true, variable: i64 0)
