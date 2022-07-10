// RUN: %target-swift-frontend -emit-silgen -emit-verbose-sil %s | %FileCheck %s

// FIXME: Not sure if this an ideal source info for the branch - 
// it points to if, not the last instruction in the block.
func ifexpr() -> Int {
  var x : Int = 0
  if true {
    x+=1
  }
  return x
  // CHECK-LABEL: sil hidden  @_TF13sil_locations6ifexprFT_Si
  // CHECK: apply {{.*}}, loc "{{.*}}":[[@LINE-5]]:6
  // CHECK: cond_br {{%.*}}, [[TRUE_BB:bb[0-9]+]], [[FALSE_BB:bb[0-9]+]], loc "{{.*}}":[[@LINE-6]]:6
  // CHECK: br [[FALSE_BB]], loc "{{.*}}":[[@LINE-5]]:3
  // CHECK: return {{.*}}, loc "{{.*}}":[[@LINE-5]]:3, {{.*}}:return
}

func ifelseexpr() -> Int {
  var x : Int = 0
  if true {
    x+=1
  } else {
    x-=1
  }
  return x
  // CHECK-LABEL: sil hidden  @_TF13sil_locations10ifelseexprFT_Si
  // CHECK: cond_br {{%.*}}, [[TRUE_BB:bb[0-9]+]], [[FALSE_BB:bb[0-9]+]], loc "{{.*}}":[[@LINE-7]]:6
  // CHECK: [[TRUE_BB]]:
  // CHECK: br bb{{[0-9]+}}, loc "{{.*}}":[[@LINE-7]]:3
  // CHECK: [[FALSE_BB]]:
  // CHECK: br bb{{[0-9]+}}, loc "{{.*}}":[[@LINE-7]]:3
  // CHECK: return {{.*}}, loc "{{.*}}":[[@LINE-7]]:3, {{.*}}:return
}

// The source locations are handled differently here - since 
// the return is unified, we keep the location of the return(not the if) 
// in the branch.
func ifexpr_return() -> Int {
  if true {
    return 5
  }
  return 6
  // CHECK-LABEL: sil hidden  @_TF13sil_locations13ifexpr_returnFT_Si
  // CHECK: apply {{.*}}, loc "{{.*}}":[[@LINE-5]]:6
  // CHECK: cond_br {{%.*}}, [[TRUE_BB:bb[0-9]+]], [[FALSE_BB:bb[0-9]+]], loc "{{.*}}":[[@LINE-6]]:6
  // CHECK: [[TRUE_BB]]:
  // CHECK: br bb{{[0-9]+}}({{%.*}}), loc "{{.*}}":[[@LINE-7]]:5, {{.*}}:return
  // CHECK: [[FALSE_BB]]:
  // CHECK: br bb{{[0-9]+}}({{%.*}}), loc "{{.*}}":[[@LINE-7]]:3, {{.*}}:return
  // CHECK: return {{.*}}, loc "{{.*}}":[[@LINE+1]]:1, {{.*}}:cleanup
}

func ifexpr_rval() -> Int {
  var x = true ? 5 : 6
  return x
  // CHECK-LABEL: sil hidden  @_TF13sil_locations11ifexpr_rvalFT_Si
  // CHECK: apply {{.*}}, loc "{{.*}}":[[@LINE-3]]:11
  // CHECK: cond_br {{%.*}}, [[TRUE_BB:bb[0-9]+]], [[FALSE_BB:bb[0-9]+]], loc "{{.*}}":[[@LINE-4]]:11
  // CHECK: [[TRUE_BB]]:
  // CHECK: br bb{{[0-9]+}}({{%.*}}), loc "{{.*}}":[[@LINE-6]]:18
  // CHECK: [[FALSE_BB]]:
  // CHECK: br bb{{[0-9]+}}({{%.*}}), loc "{{.*}}":[[@LINE-8]]:22
}











// --- Test function calls.
func simpleDirectCallTest(_ i: Int) -> Int {
  return simpleDirectCallTest(i)
  // CHECK-LABEL: sil hidden  @_TF13sil_locations20simpleDirectCallTest
  // CHECK: function_ref @_TF13sil_locations20simpleDirectCallTest{{.*}}, loc "{{.*}}":[[@LINE-2]]:10
  // CHECK: {{%.*}} apply {{%.*}} line:[[@LINE-3]]:10
}

func templateTest<T>(_ value: T) -> T {
  return value
}
func useTemplateTest() -> Int {
  return templateTest(5);
  // CHECK-LABEL: sil hidden  @_TF13sil_locations15useTemplateTestFT_Si

  // CHECK: function_ref @_TFSiC{{.*}}, loc "{{.*}}":87
}

func foo(_ x: Int) -> Int {
  func bar(_ y: Int) -> Int {
    return x + y
  }
  return bar(1)
  // CHECK-LABEL: sil hidden  @_TF13sil_locations3foo
  // CHECK: [[CLOSURE:%[0-9]+]] = function_ref {{.*}}, loc "{{.*}}":[[@LINE-2]]:10
  // CHECK: apply [[CLOSURE:%[0-9]+]]
}

class LocationClass {
  func mem() {}
}
func testMethodCall() {
  var l: LocationClass
  l.mem();
  // CHECK-LABEL: sil hidden  @_TF13sil_locations14testMethodCallFT_T_
  
  // CHECK: class_method {{.[0-9]+}} : $LocationClass, #LocationClass.mem!1 {{.*}}, loc "{{.*}}":[[@LINE-3]]:5
}

func multipleReturnsImplicitAndExplicit() {
  var x = 5+3
  if x > 10 {
    return
  }
  x += 1
  // CHECK-LABEL: sil hidden  @_TF13sil_locations34multipleReturnsImplicitAndExplicitFT_T_
  // CHECK: cond_br
  // CHECK: br bb{{[0-9]+}}, loc "{{.*}}":[[@LINE-5]]:5, {{.*}}:return
  // CHECK: br bb{{[0-9]+}}, loc "{{.*}}":[[@LINE+2]]:1, {{.*}}:imp_return
  // CHECK: return {{.*}}, loc "{{.*}}":[[@LINE+1]]:1, {{.*}}:cleanup
}

func simplifiedImplicitReturn() -> () {
  var y = 0 
  // CHECK-LABEL: sil hidden  @_TF13sil_locations24simplifiedImplicitReturnFT_T_
  // CHECK: return {{.*}}, loc "{{.*}}":[[@LINE+1]]:1, {{.*}}:imp_return
}

func switchfoo() -> Int { return 0 }
func switchbar() -> Int { return 0 }

// CHECK-LABEL: sil hidden @_TF13sil_locations10testSwitchFT_T_
func testSwitch() {
  var x:Int
  x = 0
  switch (switchfoo(), switchbar()) {
  // CHECK: store {{.*}}, loc "{{.*}}":[[@LINE+1]]
  case (1,2):
  // CHECK: integer_literal $Builtin.Int2048, 2, loc "{{.*}}":[[@LINE-1]]:11
  // FIXME: Location info is missing.
  // CHECK: cond_br
  //
    var z: Int = 200
  // CHECK: [[VAR_Z:%[0-9]+]] = alloc_box $Int, var, name "z"{{.*}}line:[[@LINE-1]]:9
  // CHECK: integer_literal $Builtin.Int2048, 200, loc "{{.*}}":[[@LINE-2]]:18
    x = z
  // CHECK:  strong_release [[VAR_Z]]{{.*}}, loc "{{.*}}":[[@LINE-1]]:9, {{.*}}:cleanup
  case (3, let y):
    x += 1
  }
}

func testIf() {
  if true {
    var y:Int
  } else {
    var x:Int
  }
  // CHECK-LABEL: sil hidden @_TF13sil_locations6testIfFT_T_
  //
  // FIXME: Missing location info here.
  // CHECK: function_ref
  // CHECK: apply
  // 
  //
  //
  // CHECK: br {{.*}}, loc "{{.*}}":[[@LINE-13]]:6



}

func testFor() {
  for i in 0..<10 {
    var y: Int = 300
    y+=1
    if true {
      break
    }
    y-=1
    continue
  }

  // CHECK-LABEL: sil hidden @_TF13sil_locations7testForFT_T_
  // CHECK: [[VAR_Y_IN_FOR:%[0-9]+]]  = alloc_box $Int, var, name "y", loc "{{.*}}":[[@LINE-10]]:9
  // CHECK: integer_literal $Builtin.Int2048, 300, loc "{{.*}}":[[@LINE-11]]:18
  // CHECK: strong_release [[VAR_Y_IN_FOR]] : $@box Int
  // CHECK: br bb{{.*}}, loc "{{.*}}":[[@LINE-10]]:7
  // CHECK: strong_release [[VAR_Y_IN_FOR]] : $@box Int
  // CHECK: br bb{{.*}}, loc "{{.*}}":[[@LINE-9]]:5
  
  
}

func testTuples() {
  var t = (2,3)
  var tt = (2, (4,5))
  var d = "foo"
  // CHECK-LABEL: sil hidden @_TF13sil_locations10testTuplesFT_T_


  // CHECK: tuple_element_addr {{.*}}, loc "{{.*}}":[[@LINE-6]]:11
  // CHECK: integer_literal $Builtin.Int2048, 2, loc "{{.*}}":[[@LINE-7]]:12
  // CHECK: integer_literal $Builtin.Int2048, 3, loc "{{.*}}":[[@LINE-8]]:14
  // CHECK: tuple_element_addr {{.*}}, loc "{{.*}}":[[@LINE-8]]:12
  // CHECK: tuple_element_addr {{.*}}, loc "{{.*}}":[[@LINE-9]]:16  
}

// Test tuple imploding/exploding.
protocol Ordinable {
  func ord() -> Int
}

func b<T : Ordinable>(_ seq: T) -> (Int) -> Int {
  return {i in i + seq.ord() }
}

func captures_tuple<T, U>(x: (T, U)) -> () -> (T, U) {
  return {x}
  // CHECK-LABEL: sil hidden @_TF13sil_locations14captures_tuple


  // CHECK: tuple_element_addr {{.*}}, loc "{{.*}}":[[@LINE-5]]:27
  // CHECK: copy_addr [take] {{.*}}, loc "{{.*}}":[[@LINE-6]]:27
  // CHECK: function_ref {{.*}}, loc "{{.*}}":[[@LINE-6]]:10

  
  // CHECK-LABEL: sil shared @_TFF13sil_locations14captures_tuple
  // CHECK: copy_addr {{.*}}, loc "{{.*}}":[[@LINE-10]]:11
}

func interpolated_string(_ x: Int, y: String) -> String {
  return "The \(x) Million Dollar \(y)"
  // CHECK-LABEL: sil hidden @_TF13sil_locations19interpolated_string



  // CHECK: retain_value{{.*}}, loc "{{.*}}":[[@LINE-5]]:37


}


func int(_ x: Int) {}
func tuple() -> (Int, Float) { return (1, 1.0) }  
func tuple_element(_ x: (Int, Float)) {
  int(tuple().0)
  // CHECK-LABEL: sil hidden @_TF13sil_locations13tuple_element

  // CHECK: apply {{.*}} line:[[@LINE-3]]:7
  // CHECK: tuple_extract{{.*}}, 0, {{.*}}line:[[@LINE-4]]:7
  // CHECK: tuple_extract{{.*}}, 1, {{.*}}line:[[@LINE-5]]:7
  // CHECK: apply {{.*}} line:[[@LINE-6]]:3
     
}

func containers() -> ([Int], Dictionary<String, Int>) {
  return ([1, 2, 3], ["Ankeny": 1, "Burnside": 2, "Couch": 3])
  // CHECK-LABEL: sil hidden @_TF13sil_locations10containers
  // CHECK: apply {{%.*}}<(String, Int)>({{%.*}}), loc "{{.*}}":[[@LINE-2]]:22
  
  // CHECK: string_literal utf8 "Ankeny", loc "{{.*}}":[[@LINE-4]]:23

  // CHECK: integer_literal $Builtin.Int2048, 1, loc "{{.*}}":[[@LINE-6]]:33
  // CHECK: integer_literal $Builtin.Int2048, 2, loc "{{.*}}":[[@LINE-7]]:48

  
  
}


func a() {}
func b() -> Int { return 0 }
protocol P { func p() }
struct X : P { func p() {} }
func test_isa_2(_ p: P) {
  switch (p, b()) {
  case (is X, b()):
    a()
  case _:
    a()
  }
  


  // CHECK-LABEL: sil hidden @_TF13sil_locations10test_isa_2
  // CHECK: alloc_stack $(P, Int), loc "{{.*}}":[[@LINE-10]]:10
  // CHECK: tuple_element_addr{{.*}} $*(P, Int), 0, loc "{{.*}}":[[@LINE-11]]:10
  // CHECK: tuple_element_addr{{.*}} $*(P, Int), 1, loc "{{.*}}":[[@LINE-12]]:10
  // CHECK: load {{.*}}, loc "{{.*}}":[[@LINE-12]]:8
  //
  // CHECK: checked_cast_addr_br {{.*}}, loc "{{.*}}":[[@LINE-14]]:9
  // CHECK: load {{.*}}, loc "{{.*}}":[[@LINE-15]]:9
    
}

func runcibleWhy() {}
protocol Runcible {
  func runce()
}
enum SinglePayloadAddressOnly {
  case x(Runcible)
  case y
}
func printSinglePayloadAddressOnly(_ v:SinglePayloadAddressOnly) {
  switch v {
  case .x(let runcible):
    runcible.runce()
  case .y:
    runcibleWhy()
  }
  
  
  // CHECK_LABEL: sil hidden @_TF13sil_locations29printSinglePayloadAddressOnly
  // CHECK: bb0
  // CHECK: switch_enum_addr {{.*}} [[FALSE_BB:bb[0-9]+]], {{.*}}line:[[@LINE-10]]:3
  // CHECK: [[FALSE_BB]]:

}


func testStringForEachStmt() {
  var i = 0
  for index in 1..<20 {
    i += 1
    if i == 15 {
      break
    }
  }
  
  // CHECK-LABEL: sil hidden @_TF13sil_locations21testStringForEachStmtFT_T_
  // CHECK: br {{.*}} line:[[@LINE-8]]:3
  // CHECK: cond_br {{.*}} line:[[@LINE-9]]:3
  // CHECK: cond_br {{.*}} line:[[@LINE-8]]:8
  // Break branch:
  // CHECK: br {{.*}} line:[[@LINE-9]]:7
  // Looping back branch:
  // CHECK: br {{.*}} line:[[@LINE-9]]:3
  // Condition is false branch:
  // CHECK: br {{.*}} line:[[@LINE-16]]:3
  
  
  
  
}


func testForStmt() {
  
  var m = 0
  for i in 0..<10 {
    m += 1
    if m == 15 {
      break
    } else {
      continue
    }

  }



  

  
  
  
  
  
  
  

  
  
}


func testRepeatWhile() {
  var m = 0
  repeat {
    m += 1
  } while (m < 200)
  
  
  // CHECK-LABEL: sil hidden @_TF13sil_locations15testRepeatWhileFT_T_
  // CHECK: br {{.*}} line:[[@LINE-6]]:3
  // CHECK: cond_br {{.*}} line:[[@LINE-5]]:11
  // Loop back branch:
  // CHECK: br {{.*}} line:[[@LINE-7]]:11  
}



func testWhile() {
  var m = 0
  while m < 100 {
    m += 1
    if m > 5 {
      break
    }
    m += 1
  }
  
  // CHECK-LABEL: sil hidden @_TF13sil_locations9testWhileFT_T_
  // CHECK: br {{.*}} line:[[@LINE-9]]:3
  // While loop conditional branch:
  // CHECK: cond_br {{.*}} line:[[@LINE-11]]:9
  // If stmt condition branch:
  // CHECK: cond_br {{.*}} line:[[@LINE-11]]:8
  // Break branch:
  // CHECK: br {{.*}} line:[[@LINE-12]]:7
  // Looping back branch:
  // CHECK: br {{.*}} line:[[@LINE-11]]:3


  
}
