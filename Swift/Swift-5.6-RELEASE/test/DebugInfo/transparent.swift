// RUN: %target-swift-frontend %s -O -I %t -emit-ir -g -o - | %FileCheck %s

func use<T>(_ t: T) {}

@inline(never)
public func noinline(_ x: Int64) -> Int64 { return x }

@_transparent
public func transparent(_ y: Int64) -> Int64 {
  var local = y
  return noinline(local)
}

let z = transparent(0)
use(z)

// Check that a transparent function has no debug information.
// CHECK: define {{.*}}$s11transparentAA
// CHECK-SAME: !dbg ![[SP:[0-9]+]]
// CHECK-NEXT: entry:
// CHECK-NEXT: !dbg ![[ZERO:[0-9]+]]
// CHECK-NEXT: }

// CHECK: ![[FILE:[0-9]+]] = {{.*}}"<compiler-generated>"
// CHECK: ![[SP]] = distinct !DISubprogram({{.*}}name: "transparent"
// CHECK-SAME:                             file: ![[FILE]]
// CHECK-NOT: line:
// CHECK: ![[ZERO]] = !DILocation(line: 0,
