// RUN: %target-swift-frontend -primary-file %s -O -emit-sil | grep -v 'builtin "onFastPath"' | %FileCheck %s

// REQUIRES: swift_stdlib_no_asserts
// XFAIL: resilient_stdlib

//CHECK-LABEL: @_T017unused_containers16empty_array_testyyF
//CHECK: bb0:
//CHECK-NEXT: tuple
//CHECK-NEXT: return
func empty_array_test() {
  let unused : [Int] = []
}

//CHECK-LABEL: @_T017unused_containers14empty_dic_testyyF
//CHECK: bb0:
//CHECK-NEXT: tuple
//CHECK-NEXT: return
func empty_dic_test() {
  let unused : [Int: Int] = [:]
}

//CHECK-LABEL: sil hidden @_T017unused_containers0A12_string_testyyF
//CHECK-NEXT: bb0:
//CHECK-NEXT: tuple
//CHECK-NEXT: return
func unused_string_test() {
  let unused : String = ""
}

//CHECK-LABEL: array_of_strings_test
//CHECK: bb0:
//CHECK-NEXT: tuple
//CHECK-NEXT: return
func array_of_strings_test() {
  let x = [""]
}

//CHECK-LABEL: string_interpolation
//CHECK: bb0:
//CHECK-NEXT: tuple
//CHECK-NEXT: return
func string_interpolation() {
  // Int
  let x : Int = 2
  "\(x)"

  // String
  let y : String = "hi"
  "\(y)"

  // Float
  let f : Float = 2.0
  "\(f)"

  // Bool
  "\(true)"

  //UInt8
  "\(UInt8(2))"

  //UInt32
  "\(UInt32(4))"
}

//CHECK-LABEL: string_interpolation2
//CHECK: bb0:
//CHECK-NEXT: tuple
//CHECK-NEXT: return
func string_interpolation2() {
  "\(false) \(true)"
}

//CHECK-LABEL: string_plus
//CHECK: bb0:
//CHECK-NEXT: tuple
//CHECK-NEXT: return
func string_plus() {
  "a" + "b"
}
