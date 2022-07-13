// RUN: %target-swift-frontend -assume-parsing-unqualified-ownership-sil -enable-large-loadable-types %s -emit-ir | %FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-%target-ptrsize
// REQUIRES: optimized_stdlib
// UNSUPPORTED: resilient_stdlib

public struct BigStruct {
  var i0 : Int32 = 0
  var i1 : Int32 = 1
  var i2 : Int32 = 2
  var i3 : Int32 = 3
  var i4 : Int32 = 4
  var i5 : Int32 = 5
  var i6 : Int32 = 6
  var i7 : Int32 = 7
  var i8 : Int32 = 8
}

func takeClosure(execute block: () -> Void) {
}

class OptionalInoutFuncType {
  private var lp :  BigStruct?
  private var _handler : ((BigStruct?, Error?) -> ())?

  func execute(_ error: Error?) {
    var p : BigStruct?
    var handler: ((BigStruct?, Error?) -> ())?
    
    takeClosure {
      p = self.lp
      handler = self._handler
      self._handler = nil
    }

    handler?(p, error)
  }
}

// CHECK-LABEL: define{{( protected)?}} i32 @main(i32, i8**)
// CHECK: call void @llvm.lifetime.start
// CHECK: call void @llvm.memcpy
// CHECK: call void @llvm.lifetime.end
// CHECK: ret i32 0
let bigStructGlobalArray : [BigStruct] = [
  BigStruct()
]

// CHECK-LABEL: define{{( protected)?}} internal swiftcc void @_T022big_types_corner_cases21OptionalInoutFuncTypeC7executeys5Error_pSgFyycfU_(%T22big_types_corner_cases9BigStructVSg* nocapture dereferenceable({{.*}}), %T22big_types_corner_cases21OptionalInoutFuncTypeC*, %T22big_types_corner_cases9BigStructVSgs5Error_pSgIegcx_Sg* nocapture dereferenceable({{.*}})
// CHECK: call void @_T0SqWy
// CHECK: call void @_T0SqWe
// CHECK: ret void

public func f1_returns_BigType(_ x: BigStruct) -> BigStruct {
  return x
}

public func f2_returns_f1() -> (_ x: BigStruct) -> BigStruct {
  return f1_returns_BigType
}

public func f3_uses_f2() {
  let x = BigStruct()
  let useOfF2 = f2_returns_f1()
  let _ = useOfF2(x)
}

// CHECK-LABEL: define{{( protected)?}} swiftcc void @_T022big_types_corner_cases10f3_uses_f2yyF()
// CHECK: call swiftcc void @_T022big_types_corner_cases9BigStructVACycfC(%T22big_types_corner_cases9BigStructV* noalias nocapture sret 
// CHECK: call swiftcc { i8*, %swift.refcounted* } @_T022big_types_corner_cases13f2_returns_f1AA9BigStructVADcyF()
// CHECK: call swiftcc void {{.*}}(%T22big_types_corner_cases9BigStructV* noalias nocapture sret {{.*}}, %T22big_types_corner_cases9BigStructV* noalias nocapture dereferenceable({{.*}}) {{.*}}, %swift.refcounted* swiftself {{.*}})
// CHECK: ret void

public func f4_tuple_use_of_f2() {
  let x = BigStruct()
  let tupleWithFunc = (f2_returns_f1(), x)
  let useOfF2 = tupleWithFunc.0
  let _ = useOfF2(x)
}

// CHECK-LABEL: define{{( protected)?}} swiftcc void @_T022big_types_corner_cases18f4_tuple_use_of_f2yyF()
// CHECK: [[TUPLE:%.*]] = call swiftcc { i8*, %swift.refcounted* } @_T022big_types_corner_cases13f2_returns_f1AA9BigStructVADcyF()
// CHECK: [[TUPLE_EXTRACT:%.*]] = extractvalue { i8*, %swift.refcounted* } [[TUPLE]], 0
// CHECK: [[CAST_EXTRACT:%.*]] = bitcast i8* [[TUPLE_EXTRACT]] to void (%T22big_types_corner_cases9BigStructV*, %T22big_types_corner_cases9BigStructV*, %swift.refcounted*)*
// CHECK:  call swiftcc void [[CAST_EXTRACT]](%T22big_types_corner_cases9BigStructV* noalias nocapture sret {{.*}}, %T22big_types_corner_cases9BigStructV* noalias nocapture dereferenceable({{.*}}) {{.*}}, %swift.refcounted* swiftself %{{.*}})
// CHECK: ret void

public class BigClass {
  public init() {
  }

  public var optVar: ((BigStruct)-> Void)? = nil
    
  func useBigStruct(bigStruct: BigStruct) {
    optVar!(bigStruct)
  }
}

// CHECK-LABEL: define{{( protected)?}} hidden swiftcc void @_T022big_types_corner_cases8BigClassC03useE6StructyAA0eH0V0aH0_tF(%T22big_types_corner_cases9BigStructV* noalias nocapture dereferenceable({{.*}}), %T22big_types_corner_cases8BigClassC* swiftself)
// CHECK: getelementptr inbounds %T22big_types_corner_cases8BigClassC, %T22big_types_corner_cases8BigClassC*
// CHECK: call void @_T0SqWy
// CHECK: [[BITCAST:%.*]] = bitcast i8* {{.*}} to void (%T22big_types_corner_cases9BigStructV*, %swift.refcounted*)*
// CHECK: call swiftcc void [[BITCAST]](%T22big_types_corner_cases9BigStructV* noalias nocapture dereferenceable({{.*}}) %0, %swift.refcounted* swiftself 
// CHECK: ret void

public struct MyStruct {
  public let a: Int
  public let b: String?
 }

typealias UploadFunction = ((MyStruct, Int?) -> Void) -> Void
func takesUploader(_ u: UploadFunction) { }

class Foo {
  func blam() {
    takesUploader(self.myMethod) // crash compiling this
  }

  func myMethod(_ callback: (MyStruct, Int) -> Void) -> Void { }
}

// CHECK-LABEL: define{{( protected)?}} linkonce_odr hidden swiftcc { i8*, %swift.refcounted* } @_T022big_types_corner_cases3FooC8myMethodyyAA8MyStructV_SitcFTc(%T22big_types_corner_cases3FooC*)
// CHECK: getelementptr inbounds %T22big_types_corner_cases3FooC, %T22big_types_corner_cases3FooC*
// CHECK: getelementptr inbounds void (i8*, %swift.refcounted*, %T22big_types_corner_cases3FooC*)*, void (i8*, %swift.refcounted*, %T22big_types_corner_cases3FooC*)**
// CHECK: call noalias %swift.refcounted* @swift_rt_swift_allocObject(%swift.type* getelementptr inbounds (%swift.full_boxmetadata, %swift.full_boxmetadata*
// CHECK: ret { i8*, %swift.refcounted* }

public enum LargeEnum {
  public enum InnerEnum {
    case simple(Int64)
    case hard(Int64, String?)
  }
  case Empty1
  case Empty2
  case Full(InnerEnum)
}

public func enumCallee(_ x: LargeEnum) {
  switch x {
    case .Full(let inner): print(inner)
    case .Empty1: break
    case .Empty2: break
  }
}
// CHECK-LABEL-64: define{{( protected)?}} swiftcc void @_T022big_types_corner_cases10enumCalleeyAA9LargeEnumOF(%T22big_types_corner_cases9LargeEnumO* noalias nocapture dereferenceable({{.*}})) #0 {
// CHECK-64: alloca %T22big_types_corner_cases9LargeEnumO05InnerF0O
// CHECK-64: alloca %T22big_types_corner_cases9LargeEnumO
// CHECK-64: call void @llvm.memcpy.p0i8.p0i8.i64
// CHECK-64: call void @llvm.memcpy.p0i8.p0i8.i64
// CHECK-64: _T0s5printyypd_SS9separatorSS10terminatortF
// CHECK-64: ret void

// CHECK-LABEL: define{{( protected)?}} internal swiftcc void @_T022big_types_corner_cases8SuperSubC1fyyFAA9BigStructVycfU_(%T22big_types_corner_cases9BigStructV* noalias nocapture sret, %T22big_types_corner_cases8SuperSubC*)
// CHECK-64: [[ALLOC1:%.*]] = alloca %T22big_types_corner_cases9BigStructV
// CHECK-64: [[ALLOC2:%.*]] = alloca %T22big_types_corner_cases9BigStructV
// CHECK-64: [[ALLOC3:%.*]] = alloca %T22big_types_corner_cases9BigStructVSg
// CHECK-64: [[ALLOC4:%.*]] = alloca %T22big_types_corner_cases9BigStructVSg
// CHECK-64: call swiftcc void @_T022big_types_corner_cases9SuperBaseC4boomAA9BigStructVyF(%T22big_types_corner_cases9BigStructV* noalias nocapture sret [[ALLOC1]], %T22big_types_corner_cases9SuperBaseC* swiftself {{.*}})
// CHECK: ret void
class SuperBase {
  func boom() -> BigStruct {
    return BigStruct()
  }
}

class SuperSub : SuperBase {
  override func boom() -> BigStruct {
    return BigStruct()
  }
  func f() {
    let _ = {
      nil ?? super.boom()
    }
  }
}

// CHECK-LABEL: define{{( protected)?}} swiftcc void @_T022big_types_corner_cases10MUseStructV16superclassMirrorAA03BigF0VSgvg(%T22big_types_corner_cases9BigStructVSg* noalias nocapture sret, %T22big_types_corner_cases10MUseStructV* noalias nocapture swiftself dereferenceable({{.*}}))
// CHECK: [[ALLOC:%.*]] = alloca %T22big_types_corner_cases9BigStructVSg
// CHECK: [[LOAD:%.*]] = load %swift.refcounted*, %swift.refcounted** %.callInternalLet.data
// CHECK: call swiftcc void %7(%T22big_types_corner_cases9BigStructVSg* noalias nocapture sret [[ALLOC]], %swift.refcounted* swiftself [[LOAD]])
// CHECK: ret void
public struct MUseStruct {
  var x = BigStruct()
  public var superclassMirror: BigStruct? {
    return callInternalLet()
  }

  internal let callInternalLet: () -> BigStruct?
}

// CHECK-LABEL-64: define{{( protected)?}} swiftcc void @_T022big_types_corner_cases18stringAndSubstringSS_s0G0VtyF(<{ %TSS, %Ts9SubstringV }>* noalias nocapture sret) #0 {
// CHECK-LABEL-32: define{{( protected)?}} swiftcc void @_T022big_types_corner_cases18stringAndSubstringSS_s0G0VtyF(<{ %TSS, [4 x i8], %Ts9SubstringV }>* noalias nocapture sret) #0 {
// CHECK: alloca %Ts9SubstringV
// CHECK: alloca %Ts9SubstringV
// CHECK: ret void
public func stringAndSubstring() -> (String, Substring) {
  let s = "Hello, World"
  let a = Substring(s).dropFirst()
  return (s, a)
}

func bigStructGet() -> BigStruct {
  return BigStruct()
}

// CHECK-LABEL: define{{( protected)?}} swiftcc void @_T022big_types_corner_cases11testGetFuncyyF()
// CHECK: ret void
public func testGetFunc() {
  let testGetPtr: @convention(thin) () -> BigStruct = bigStructGet
}

// CHECK-LABEL: define{{( protected)?}} hidden swiftcc void @_T022big_types_corner_cases7TestBigC4testyyF(%T22big_types_corner_cases7TestBigC* swiftself)
// CHECK: [[CALL1:%.*]] = call %swift.type* @_T0Sayy22big_types_corner_cases9BigStructVcSgGMa
// CHECK: [[CALL2:%.*]] = call i8** @_T0Sayy22big_types_corner_cases9BigStructVcSgGSayxGs10CollectionsWl
// CHECK:  call swiftcc void @_T0s10CollectionPsE5index5IndexQzSgSb7ElementQzKc5where_tKF(%TSq.0* noalias nocapture sret %10, i8* bitcast (i1 (%T22big_types_corner_cases9BigStructVytIegir_Sg*, %swift.refcounted*, %swift.error**)* @_T022big_types_corner_cases9BigStructVIegy_SgSbs5Error_pIgxdzo_ACytIegir_SgSbsAE_pIgidzo_TRTA to i8*), %swift.refcounted* %7, %swift.type* %11, i8** [[CALL2]]
// CHECK: ret void
class TestBig {
    typealias Handler = (BigStruct) -> Void

    func test() {
        let arr = [Handler?]()
        let d = arr.index(where: { _ in true })
    }
}
