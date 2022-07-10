// RUN: %target-swift-frontend %s -emit-ir -g -o - | %FileCheck %s

protocol PointUtils {
  func distanceFromOrigin() -> Float
}
// CHECK: define {{.*}}float @_TTWC8protocol{{.*}} !dbg !{{[0-9]+}}

class Point : PointUtils {
    var x : Float
    var y : Float
    init (_x : Float, _y : Float) {
        x = _x
        y = _y
    }

    func distanceFromOrigin() -> Float {
        return 1.0
    }

}

// CHECK: define hidden i64 @_TF8protocol4mainFT_Vs5Int64() {{.*}} {
func main() -> Int64 {
    var pt = Point(_x: 2.5, _y: 4.25)
// CHECK: [[LOC2D:%[a-zA-Z0-9]+]] = alloca %P8protocol10PointUtils_, align {{(4|8)}}
// CHECK: call void @llvm.dbg.declare(metadata {{.*}} [[LOC2D]], metadata ![[LOC:.*]], metadata !{{[0-9]+}})
    var loc2d : PointUtils = pt
    var distance = loc2d.distanceFromOrigin()

    return 0
}

// Self should be artificial.
// CHECK: !DILocalVariable(name: "self", arg: 1{{.*}} line: 16
// CHECK-SAME:             DIFlagArtificial

// CHECK: ![[LOC]] = !DILocalVariable(name: "loc2d",{{.*}} line: [[@LINE-10]]

main()

