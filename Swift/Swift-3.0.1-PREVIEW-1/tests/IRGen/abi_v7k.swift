// RUN: %target-swift-frontend -emit-ir -primary-file %s -module-name test_v7k | %FileCheck %s
// RUN: %target-swift-frontend -S -primary-file %s -module-name test_v7k | %FileCheck -check-prefix=V7K %s

// REQUIRES: CPU=armv7k
// REQUIRES: OS=watchos

// CHECK-LABEL: define hidden float @_TF8test_v7k9addFloats{{.*}}(float, float)
// CHECK: fadd float %0, %1
// CHECK ret float
// V7K-LABEL: __TF8test_v7k9addFloats{{.*}}
// V7K: vadd.f32 s0, s0, s1
func addFloats(x: Float, y : Float) -> Float {
  return x+y
}

// CHECK-LABEL: define hidden double @_TF8test_v7k10addDoubles{{.*}}(double, double, double)
// CHECK: fadd double %0, %1
// CHECK: fadd double
// CHECK: ret double
// V7K-LABEL: __TF8test_v7k10addDoubles
// V7K: vadd.f64 d0, d0, d1
// V7K: vadd.f64 d0, d0, d2
func addDoubles(x: Double, y: Double, z: Double) -> Double {
  return x+y+z
}

// CHECK-LABEL: define hidden float @_TF8test_v7k6addFDF{{.*}}(float, double, float)
// CHECK: fmul float
// CHECK: ret float
// V7K-LABEL: __TF8test_v7k6addFDF
// V7K: vmul.f32 s0, s0, s1
// z is back-filled to s1
func addFDF(x: Float, y: Double, z: Float) -> Float {
  return x*z
}

// CHECK-LABEL: define hidden double @_TF8test_v7k8addStackFT{{.*}}(double, double, double, double, double, double, double, float, double)
// CHECK: fadd double
// CHECK: ret double
// V7K-LABEL: __TF8test_v7k8addStackFT
// V7K: vldr d16, [sp]
// V7K: vadd.f64 d0, d6, d16
// a is assigned to d6, c is passed via stack
func addStack(d0: Double, d1: Double, d2: Double, d3: Double, d4: Double,
              d5: Double, a: Double, b: Float, c: Double) -> Double {
  return a+c
}

// CHECK-LABEL: define hidden float @_TF8test_v7k9addStack2{{.*}}(double, double, double, double, double, double, double, float, double, float)
// CHECK: fadd float
// V7K-LABEL: __TF8test_v7k9addStack2
// V7K: vldr s0, [sp, #8]
// V7K: vadd.f32 s0, s14, s0
// a is assigned to s14, b is via stack, c is via stack since it can't be back-filled to s15
func addStack2(d0: Double, d1: Double, d2: Double, d3: Double, d4: Double, 
               d5: Double, d6: Double, a: Float, b: Double, c: Float) -> Float {
  return a+c
}

// Passing various enums:
// CHECK-LABEL: define hidden void @_TF8test_v7k9testEmpty{{.*}}()
// V7K-LABEL: __TF8test_v7k9testEmpty
enum Empty {}
func testEmpty(x: Empty) -> Empty {
  return x
}

// CHECK-LABEL: define hidden i32 @_TF8test_v7k10testSingle{{.*}}()
// CHECK: ret i32 1
// V7K-LABEL: __TF8test_v7k10testSingle
// V7K: movw    r0, #1
enum SingleCase { case X }
func testSingle(x: SingleCase) -> Int32{
  switch x {
  case SingleCase.X:
    return 1
  }
}

// CHECK-LABEL: define hidden double @_TF8test_v7k8testData{{.*}}(i32, double)
// CHECK: ret double
// V7K-LABEL: __TF8test_v7k8testData
// V7K: vstr d0
// V7K: vmov.f64 d0
enum DataCase { case Y(Int, Double) }
func testData(x: DataCase) -> Double {
  switch x {
  case let .Y(i, d):
    return d
  }
}

// CHECK-LABEL: define hidden i32 @_TF8test_v7k10testClike2{{.*}}(i1)
// CHECK: [[ID:%[0-9]+]] = phi i32 [ 2, {{.*}} ], [ 1, {{.*}} ]
// CHECK: ret i32 [[ID]]
// V7K-LABEL: __TF8test_v7k10testClike2
// V7K: tst r0, #1
// V7K: movw r0, #1
// V7K: movw r0, #2
enum CLike2 {
  case A
  case B
}
func testClike2(x: CLike2) -> Int {
  switch x {
  case CLike2.A:
    return 1
  case CLike2.B:
    return 2
  }
}

// CHECK-LABEL: define hidden i32 @_TF8test_v7k10testClike8{{.*}}(i32, i3)
// CHECK: [[ID:%[0-9]+]] = phi i32 [ -1, {{.*}} ], [ 1, {{.*}} ]
// CHECK: ret i32 [[ID]]
// V7K-LABEL: __TF8test_v7k10testClike8
// V7K: tst r1, #7
// V7K: movw r0, #1
// V7K: mvn r0, #0
enum CLike8 {
  case A
  case B
  case C
  case D
  case E
  case F
  case G
  case H
}
func testClike8(t: Int, x: CLike8) -> Int {
  switch x {
  case CLike8.A:
    return 1
  default:
    return -1
  }
}

// layout of the enum: the tag bit is set for the no-data cases, which are then
// assigned values in the data area of the enum in declaration order
// CHECK-LABEL: define hidden double @_TF8test_v7k11testSingleP{{.*}}(i32, i32, i1)
// CHECK: br i1
// CHECK: switch i32 [[ID:%[0-9]+]]
// CHECK: [[FIRST:%[0-9]+]] = zext i32 %0 to i64
// CHECK: [[SECOND:%[0-9]+]] = zext i32 %1 to i64
// CHECK: [[TEMP:%[0-9]+]] = shl i64 [[SECOND]], 32
// CHECK: [[RESULT:%[0-9]+]] = or i64 [[FIRST]], [[TEMP]]
// CHECK: bitcast i64 [[RESULT]] to double
// CHECK: phi double [ 0.000000e+00, {{.*}} ]
// V7K-LABEL: __TF8test_v7k11testSingleP
// V7K: tst     r2, #1
// V7K: vmov.f64 d0
enum SinglePayload {
  case Paragraph
  case Char(Double)
  case Chapter
}
func testSingleP(x: SinglePayload) -> Double {
  switch x {
  case let .Char(d):
    return d
  default:
    return 0.0
  }
}

// CHECK-LABEL: define hidden double @_TF8test_v7k10testMultiP{{.*}}(i32, i32, i2)
// CHECK: [[FIRST:%[0-9]+]] = zext i32 %0 to i64
// CHECK: [[SECOND:%[0-9]+]] = zext i32 %1 to i64
// CHECK: [[TEMP:%[0-9]+]] = shl i64 [[SECOND]], 32
// CHECK: [[RESULT:%[0-9]+]] = or i64 [[FIRST]], [[TEMP]]
// CHECK: bitcast i64 [[RESULT]] to double
// CHECK: sitofp i32 {{.*}} to double 
// CHECK: phi double [ 0.000000e+00, {{.*}} ]
// CHECK: ret double
// V7K-LABEL: __TF8test_v7k10testMultiP
// V7K: vldr d0
// Backend will assign r0, r1 and r2 for input parameters and d0 for return values.
class Bignum {}
enum MultiPayload {
  case X(Int)
  case Y(Double)
  case Z(Bignum)
}
func testMultiP(x: MultiPayload) -> Double {
  switch x {
  case let .X(i):
    return Double(i)
  case let .Y(d):
    return d
  default:
    return 0.0
  }
}

// CHECK-LABEL: define hidden float @_TF8test_v7k7testOpt{{.*}}(i32, i1)
// CHECK: entry:
// CHECK: br i1 %1, {{.*}}, label %[[PAYLOADLABEL:.*]]
// CHECK: <label>:[[PAYLOADLABEL]]
// CHECK: [[ID:%[0-9]+]] = bitcast i32 %0 to float
// CHECK: ret float [[ID]]
// V7K-LABEL: __TF8test_v7k7testOpt
// V7K:         tst     r1, #1
// V7K:         str     r0, [r7, #-4]
// V7K:         beq     [[RET:LBB.*]]
// V7K: [[RET]]:
// V7K:         ldr     r0, [r7, #-4]
// V7K:         vmov    s0, r0
// V7K:         mov     sp, r7
// V7K:         pop     {r7, pc}
func testOpt(x: Float?) -> Float {
  return x!
}

// Returning tuple: (Int, Int)
// CHECK-LABEL: define hidden { i32, i32 } @_TF8test_v7k6minMaxF{{.*}}(i32, i32)
// V7K-LABEL: __TF8test_v7k6minMaxF
// V7K: ldr r0
// V7K: ldr r1
func minMax(x : Int, y : Int) -> (min: Int, max: Int) {
    var currentMin = x
    var currentMax = y
    if y < x {
      currentMin = y
      currentMax = x
    }
    return (currentMin, currentMax)
}

// Returning struct: Double x 4; Int8, Double, Double;
struct MyRect {
  var x : Double
  var y : Double
  var w : Double
  var h : Double
}
struct MyPoint {
var x: Double
var y: Double
}

struct MySize {
var w: Double
var h: Double
}
struct MyRect2 {
  var t: Int8
  var p : MyPoint
  init() {
    t = 1
    p = MyPoint(x : 0.0, y: 0.0)
  }
}
struct MyRect4 {
  var t: Int8
  var p : MyPoint
  var s: MySize
  init() {
   t = 1
   p = MyPoint(x : 0.0, y: 0.0)
   s = MySize(w: 1.0, h: 2.0)
  }
}
// CHECK-LABEL: define hidden void @_TF8test_v7k8testRet2{{.*}}(%V8test_v7k6MyRect* noalias nocapture sret, double, i32)
// V7K-LABEL: __TF8test_v7k8testRet2
// sret in r0, double in d0, i32 in r1
// V7K: vmov [[ID:s[0-9]+]], r1
// V7K: vcvt.f64.s32 [[ID2:d[0-9]+]], [[ID]]
// V7K: vmov.f64 d0, [[ID2]]
// V7K: bl
// V7K: vstr d{{.*}}, [r0]
// V7K: vstr d{{.*}}, [r0, #8]
// V7K: vstr d{{.*}}, [r0, #16]
// V7K: vstr d{{.*}}, [r0, #24]
func testRet2(w : Double, i : Int) -> MyRect {
  var r = MyRect(x : Double(i), y : 2.0, w : 3.0, h : 4.0)
  r.w = w
  return r
}
// CHECK-LABEL: define hidden { i8, double, double } @_TF8test_v7k8testRet3{{.*}}()
// V7K-LABEL: __TF8test_v7k8testRet3
func testRet3() -> MyRect2 {
  var r = MyRect2()
  return r
}

// Returning tuple?: (Int x 6)?
// CHECK-LABEL: define hidden void @_TF8test_v7k7minMax2{{.*}}({{%GSq.*}} noalias nocapture sret, i32, i32)
// V7K-LABEL: __TF8test_v7k7minMax2
// We will indirectly return an optional with the address in r0, input parameters will be in r1 and r2
// V7K: cmp r1, r2
// V7K: str r0, [sp, [[IDX:#[0-9]+]]]
// V7K: ldr [[R0_RELOAD:r[0-9]+]], [sp, [[IDX]]]
// V7K: str {{.*}}, [{{.*}}[[R0_RELOAD]]]
// V7K: str {{.*}}, [{{.*}}[[R0_RELOAD]], #4]
// V7K: str {{.*}}, [{{.*}}[[R0_RELOAD]], #8]
// V7K: str {{.*}}, [{{.*}}[[R0_RELOAD]], #12]
// V7K: str {{.*}}, [{{.*}}[[R0_RELOAD]], #16]
// V7K: str {{.*}}, [{{.*}}[[R0_RELOAD]], #20]
// V7K: and {{.*}}, {{.*}}, #1
// V7K: strb {{.*}}, [{{.*}}[[R0_RELOAD]], #24]
func minMax2(x : Int, y : Int) -> (min: Int, max: Int, min2: Int, max2: Int, min3: Int, max3: Int)? {
    if x == y {
      return nil
    }
    var currentMin = x
    var currentMax = y
    if y < x {
      currentMin = y
      currentMax = x
    }
    return (currentMin, currentMax, currentMin, currentMax, currentMin, currentMax)
}

// Returning struct?: {Int x 6}?
// CHECK-LABEL: define hidden void @_TF8test_v7k7minMax3{{.*}}({{%GSq.*}} noalias nocapture sret, i32, i32)
// V7K-LABEL: __TF8test_v7k7minMax3
struct Ret {
  var min:Int
  var max:Int
  var min2, max2 : Int
  var min3, max3 : Int
}
func minMax3(x : Int, y : Int) -> Ret? {
    if x == y {
      return nil
    }
    var currentMin = x
    var currentMax = y
    if y < x {
      currentMin = y
      currentMax = x
    }
    var r = Ret(min:currentMin, max:currentMax, min2:currentMin, max2:currentMax, min3:currentMin, max3:currentMax)
    return r
}

// Passing struct: Int8, MyPoint x 10, MySize * 10
// CHECK-LABEL: define hidden double @_TF8test_v7k8testRet5{{.*}}(%V8test_v7k7MyRect3* noalias nocapture dereferenceable(328))
// V7K-LABEL: __TF8test_v7k8testRet5
// V7K: ldrb [[TMP1:r[0-9]+]], [r0]
// V7K: vldr [[REG1:d[0-9]+]], [r0, #8]
// V7K: vldr [[REG2:d[0-9]+]], [r0]
// V7K: sxtb r0, [[TMP1]]
// V7K: vmov [[TMP2:s[0-9]+]], r0
// V7K: vcvt.f64.s32 [[INTPART:d[0-9]+]], [[TMP2]]
// V7K: vadd.f64 [[TMP3:d[0-9]+]], [[INTPART]], [[REG1]]
// V7K: vadd.f64 d0, [[TMP3]], [[REG2]]
struct MyRect3 {
  var t: Int8
  var p: MyPoint
  var p2: MyPoint
  var s: MySize
  var s2: MySize
  var p3: MyPoint
  var p4: MyPoint
  var s3: MySize
  var s4: MySize
  var p5: MyPoint
  var p6: MyPoint
  var s5: MySize
  var s6: MySize
  var p7: MyPoint
  var p8: MyPoint
  var s7: MySize
  var s8: MySize
  var p9: MyPoint
  var p10: MyPoint
  var s9: MySize
  var s10: MySize
}
func testRet5(r: MyRect3) -> Double {
  return Double(r.t) + r.p.x + r.s9.w
}
