// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=EXPR_POSTFIX_BEGIN_1 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=EXPR_POSTFIX_BEGIN_2 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=EXPR_POSTFIX_BEGIN_3 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=EXPR_POSTFIX_BEGIN_4 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=EXPR_POSTFIX_BEGIN_5 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=EXPR_POSTFIX_BEGIN_6 | %FileCheck %s -check-prefix=COMMON

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=EXPR_POSTFIX_BEGIN_IGNORED_1 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=EXPR_POSTFIX_BEGIN_IGNORED_2 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=EXPR_POSTFIX_BEGIN_IGNORED_3 | %FileCheck %s -check-prefix=COMMON

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_FUNC_PARAM_1 > %t.param.txt
// RUN: %FileCheck %s -check-prefix=COMMON < %t.param.txt
// RUN: %FileCheck %s -check-prefix=FIND_FUNC_PARAM_1 < %t.param.txt
// RUN: %FileCheck %s -check-prefix=NO_SELF < %t.param.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_FUNC_PARAM_2 > %t.param.txt
// RUN: %FileCheck %s -check-prefix=COMMON < %t.param.txt
// RUN: %FileCheck %s -check-prefix=FIND_FUNC_PARAM_2 < %t.param.txt
// RUN: %FileCheck %s -check-prefix=NO_SELF < %t.param.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_FUNC_PARAM_3 | %FileCheck %s -check-prefix=FIND_FUNC_PARAM_3
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_FUNC_PARAM_4 | %FileCheck %s -check-prefix=FIND_FUNC_PARAM_4
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_FUNC_PARAM_5 | %FileCheck %s -check-prefix=FIND_FUNC_PARAM_5
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_FUNC_PARAM_6 | %FileCheck %s -check-prefix=FIND_FUNC_PARAM_6
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_FUNC_PARAM_7 | %FileCheck %s -check-prefix=FIND_FUNC_PARAM_7

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_FUNC_PARAM_SELECTOR_1 > %t.param.txt
// RUN: %FileCheck %s -check-prefix=COMMON < %t.param.txt
// RUN: %FileCheck %s -check-prefix=FIND_FUNC_PARAM_SELECTOR_1 < %t.param.txt
// RUN: %FileCheck %s -check-prefix=NO_SELF < %t.param.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_CONSTRUCTOR_PARAM_1 | %FileCheck %s -check-prefix=FIND_CONSTRUCTOR_PARAM_1
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_CONSTRUCTOR_PARAM_2 | %FileCheck %s -check-prefix=FIND_CONSTRUCTOR_PARAM_2
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_CONSTRUCTOR_PARAM_3 | %FileCheck %s -check-prefix=FIND_CONSTRUCTOR_PARAM_3
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_CONSTRUCTOR_PARAM_4 | %FileCheck %s -check-prefix=FIND_CONSTRUCTOR_PARAM_4
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_CONSTRUCTOR_PARAM_5 | %FileCheck %s -check-prefix=FIND_CONSTRUCTOR_PARAM_5

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_CONSTRUCTOR_PARAM_SELECTOR_1 | %FileCheck %s -check-prefix=FIND_CONSTRUCTOR_PARAM_SELECTOR_1

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_DESTRUCTOR_PARAM_1 > %t.param.txt
// RUN: %FileCheck %s -check-prefix=FIND_DESTRUCTOR_PARAM_1 < %t.param.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FIND_DESTRUCTOR_PARAM_2 > %t.param.txt
// RUN: %FileCheck %s -check-prefix=FIND_DESTRUCTOR_PARAM_2 < %t.param.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=NO_PLACEHOLDER_NAMES_1 | %FileCheck %s -check-prefix=NO_PLACEHOLDER_NAMES_1

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=IN_INVALID_1 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=IN_INVALID_2 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=IN_INVALID_3 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=IN_INVALID_5 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=IN_INVALID_6 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=IN_INVALID_7 | %FileCheck %s -check-prefix=COMMON
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=IN_INVALID_8 | %FileCheck %s -check-prefix=COMMON

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=GENERIC_TYPEALIAS_1 | %FileCheck %s -check-prefix=MY_ALIAS
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=GENERIC_TYPEALIAS_2 | %FileCheck %s -check-prefix=MY_ALIAS

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=IN_FOR_EACH_1 | %FileCheck %s -check-prefix=IN_FOR_EACH_1
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=IN_FOR_EACH_2 | %FileCheck %s -check-prefix=IN_FOR_EACH_1
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=IN_FOR_EACH_3 | %FileCheck %s -check-prefix=IN_FOR_EACH_3
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=IN_FOR_EACH_4 | %FileCheck %s -check-prefix=IN_FOR_EACH_3

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=DEPRECATED_1 | %FileCheck %s -check-prefix=DEPRECATED_1

//
// Test code completion at the beginning of expr-postfix.
//

//===--- Helper types that are used in this test

struct FooStruct {
}

var fooObject : FooStruct

func fooFunc() -> FooStruct {
  return fooObject
}

enum FooEnum {
}

class FooClass {
}

protocol FooProtocol {
}

typealias FooTypealias = Int

// COMMON: Begin completions
// Function parameter
// COMMON-DAG: Decl[LocalVar]/Local: fooParam[#FooStruct#]{{; name=.+$}}
// Global completions
// COMMON-DAG: Decl[Struct]/CurrModule:     FooStruct[#FooStruct#]{{; name=.+$}}
// COMMON-DAG: Decl[Enum]/CurrModule:       FooEnum[#FooEnum#]{{; name=.+$}}
// COMMON-DAG: Decl[Class]/CurrModule:      FooClass[#FooClass#]{{; name=.+$}}
// COMMON-DAG: Decl[Protocol]/CurrModule:   FooProtocol[#FooProtocol#]{{; name=.+$}}
// COMMON-DAG: Decl[TypeAlias]/CurrModule:  FooTypealias[#Int#]{{; name=.+$}}
// COMMON-DAG: Decl[GlobalVar]/CurrModule:  fooObject[#FooStruct#]{{; name=.+$}}
// COMMON-DAG: Keyword[try]/None: try{{; name=.+$}}
// COMMON-DAG: Literal[Boolean]/None: true[#Bool#]{{; name=.+$}}
// COMMON-DAG: Literal[Boolean]/None: false[#Bool#]{{; name=.+$}}
// COMMON-DAG: Literal[Nil]/None: nil{{; name=.+$}}
// COMMON-DAG: Decl[Struct]/OtherModule[Swift]:    Int8[#Int8#]{{; name=.+$}}
// COMMON-DAG: Decl[Struct]/OtherModule[Swift]:    Int16[#Int16#]{{; name=.+$}}
// COMMON-DAG: Decl[Struct]/OtherModule[Swift]:    Int32[#Int32#]{{; name=.+$}}
// COMMON-DAG: Decl[Struct]/OtherModule[Swift]:    Int64[#Int64#]{{; name=.+$}}
// COMMON-DAG: Decl[Struct]/OtherModule[Swift]:      Bool[#Bool#]{{; name=.+$}}
// COMMON-DAG: Keyword[#function]/None: #function[#String#]{{; name=.+$}}
// COMMON-DAG: Keyword[#file]/None: #file[#String#]{{; name=.+$}}
// COMMON-DAG: Keyword[#line]/None: #line[#Int#]{{; name=.+$}}
// COMMON-DAG: Keyword[#column]/None: #column[#Int#]{{; name=.+$}}
// COMMON: End completions

// NO_SELF-NOT: Self
// NO_SELF-NOT: self

//===--- Test that we can code complete at the beginning of expr-postfix.

func testExprPostfixBegin1(fooParam: FooStruct) {
  #^EXPR_POSTFIX_BEGIN_1^#
}

func testExprPostfixBegin2(fooParam: FooStruct) {
  1 + #^EXPR_POSTFIX_BEGIN_2^#
}

func testExprPostfixBegin3(fooParam: FooStruct) {
  fooFunc()
  1 + #^EXPR_POSTFIX_BEGIN_3^#
}

func testExprPostfixBegin4(fooParam: FooStruct) {
  "\(#^EXPR_POSTFIX_BEGIN_4^#)"
}

func testExprPostfixBegin3(fooParam: FooStruct) {
  1+#^EXPR_POSTFIX_BEGIN_5^#
}
func testExprPostfixBegin3(fooParam: FooStruct) {
  for i in 1...#^EXPR_POSTFIX_BEGIN_6^#
}

//===--- Test that we sometimes ignore the expr-postfix.
// In these cases, displaying '.instance*' completion results is technically
// valid, but would be extremely surprising.

func testExprPostfixBeginIgnored1(fooParam: FooStruct) {
  fooFunc()
  #^EXPR_POSTFIX_BEGIN_IGNORED_1^#
}

func testExprPostfixBeginIgnored2(fooParam: FooStruct) {
  123456789
  #^EXPR_POSTFIX_BEGIN_IGNORED_2^#
}

func testExprPostfixBeginIgnored3(fooParam: FooStruct) {
  123456789 +
      fooFunc()
  #^EXPR_POSTFIX_BEGIN_IGNORED_3^#
}

//===--- Test that we include function parameters in completion results.

func testFindFuncParam1(fooParam: FooStruct, a: Int, b: Float, c: inout Double, d: inout Double) {
  #^FIND_FUNC_PARAM_1^#
// FIND_FUNC_PARAM_1: Begin completions
// FIND_FUNC_PARAM_1-DAG: Decl[LocalVar]/Local: a[#Int#]{{; name=.+$}}
// FIND_FUNC_PARAM_1-DAG: Decl[LocalVar]/Local: b[#Float#]{{; name=.+$}}
// FIND_FUNC_PARAM_1-DAG: Decl[LocalVar]/Local: c[#inout Double#]{{; name=.+$}}
// FIND_FUNC_PARAM_1-DAG: Decl[LocalVar]/Local: d[#inout Double#]{{; name=.+$}}
// FIND_FUNC_PARAM_1: End completions
}

func testFindFuncParam2<Foo : FooProtocol>(fooParam: FooStruct, foo: Foo) {
  #^FIND_FUNC_PARAM_2^#
// FIND_FUNC_PARAM_2: Begin completions
// FIND_FUNC_PARAM_2-DAG: Decl[GenericTypeParam]/Local: Foo[#Foo#]{{; name=.+$}}
// FIND_FUNC_PARAM_2-DAG: Decl[LocalVar]/Local:         foo[#Foo#]{{; name=.+$}}
// FIND_FUNC_PARAM_2: End completions
}

struct TestFindFuncParam3_4 {
  func testFindFuncParam3(a: Int, b: Float, c: Double) {
    #^FIND_FUNC_PARAM_3^#
// FIND_FUNC_PARAM_3: Begin completions
// FIND_FUNC_PARAM_3-DAG: Decl[LocalVar]/Local: self[#TestFindFuncParam3_4#]{{; name=.+$}}
// FIND_FUNC_PARAM_3-DAG: Decl[LocalVar]/Local: a[#Int#]{{; name=.+$}}
// FIND_FUNC_PARAM_3-DAG: Decl[LocalVar]/Local: b[#Float#]{{; name=.+$}}
// FIND_FUNC_PARAM_3-DAG: Decl[LocalVar]/Local: c[#Double#]{{; name=.+$}}
// FIND_FUNC_PARAM_3: End completions
  }

  func testFindFuncParam4<U>(a: Int, b: U) {
    #^FIND_FUNC_PARAM_4^#
// FIND_FUNC_PARAM_4: Begin completions
// FIND_FUNC_PARAM_4-DAG: Decl[GenericTypeParam]/Local: U[#U#]{{; name=.+$}}
// FIND_FUNC_PARAM_4-DAG: Decl[LocalVar]/Local:         self[#TestFindFuncParam3_4#]{{; name=.+$}}
// FIND_FUNC_PARAM_4-DAG: Decl[LocalVar]/Local:         a[#Int#]{{; name=.+$}}
// FIND_FUNC_PARAM_4-DAG: Decl[LocalVar]/Local:         b[#U#]{{; name=.+$}}
// FIND_FUNC_PARAM_4: End completions
  }
}

struct TestFindFuncParam5_6<T> {
  func testFindFuncParam5(a: Int, b: T) {
    #^FIND_FUNC_PARAM_5^#
// FIND_FUNC_PARAM_5: Begin completions
// FIND_FUNC_PARAM_5-DAG: Decl[GenericTypeParam]/CurrNominal: T[#T#]{{; name=.+$}}
// FIND_FUNC_PARAM_5-DAG: Decl[LocalVar]/Local: self[#TestFindFuncParam5_6<T>#]{{; name=.+$}}
// FIND_FUNC_PARAM_5-DAG: Decl[LocalVar]/Local: a[#Int#]{{; name=.+$}}
// FIND_FUNC_PARAM_5-DAG: Decl[LocalVar]/Local: b[#T#]{{; name=.+$}}
// FIND_FUNC_PARAM_5: End completions
  }

  func testFindFuncParam6<U>(a: Int, b: T, c: U) {
    #^FIND_FUNC_PARAM_6^#
// FIND_FUNC_PARAM_6: Begin completions
// FIND_FUNC_PARAM_6-DAG: Decl[GenericTypeParam]/CurrNominal: T[#T#]{{; name=.+$}}
// FIND_FUNC_PARAM_6-DAG: Decl[GenericTypeParam]/Local:       U[#U#]{{; name=.+$}}
// FIND_FUNC_PARAM_6-DAG: Decl[LocalVar]/Local:               self[#TestFindFuncParam5_6<T>#]{{; name=.+$}}
// FIND_FUNC_PARAM_6-DAG: Decl[LocalVar]/Local:               a[#Int#]{{; name=.+$}}
// FIND_FUNC_PARAM_6-DAG: Decl[LocalVar]/Local:               b[#T#]{{; name=.+$}}
// FIND_FUNC_PARAM_6-DAG: Decl[LocalVar]/Local:               c[#U#]{{; name=.+$}}
// FIND_FUNC_PARAM_6: End completions
  }
}

class TestFindFuncParam7 {
  func testFindFuncParam7(a: Int, b: Float, c: Double) {
    #^FIND_FUNC_PARAM_7^#
// FIND_FUNC_PARAM_7: Begin completions
// FIND_FUNC_PARAM_7-DAG: Decl[LocalVar]/Local: self[#TestFindFuncParam7#]{{; name=.+$}}
// FIND_FUNC_PARAM_7-DAG: Decl[LocalVar]/Local: a[#Int#]{{; name=.+$}}
// FIND_FUNC_PARAM_7-DAG: Decl[LocalVar]/Local: b[#Float#]{{; name=.+$}}
// FIND_FUNC_PARAM_7-DAG: Decl[LocalVar]/Local: c[#Double#]{{; name=.+$}}
// FIND_FUNC_PARAM_7: End completions
  }
}

func testFindFuncParamSelector1(a: Int, b x: Float, foo fooParam: FooStruct, bar barParam: inout FooStruct) {
  #^FIND_FUNC_PARAM_SELECTOR_1^#
// FIND_FUNC_PARAM_SELECTOR_1: Begin completions
// FIND_FUNC_PARAM_SELECTOR_1-DAG: Decl[LocalVar]/Local: a[#Int#]{{; name=.+$}}
// FIND_FUNC_PARAM_SELECTOR_1-DAG: Decl[LocalVar]/Local: x[#Float#]{{; name=.+$}}
// FIND_FUNC_PARAM_SELECTOR_1-DAG: Decl[LocalVar]/Local: barParam[#inout FooStruct#]{{; name=.+$}}
// FIND_FUNC_PARAM_SELECTOR_1: End completions
}

//===--- Test that we include constructor parameters in completion results.

class TestFindConstructorParam1 {
  init(a: Int, b: Float) {
    #^FIND_CONSTRUCTOR_PARAM_1^#
// FIND_CONSTRUCTOR_PARAM_1: Begin completions
// FIND_CONSTRUCTOR_PARAM_1-DAG: Decl[LocalVar]/Local: self[#TestFindConstructorParam1#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_1-DAG: Decl[LocalVar]/Local: a[#Int#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_1-DAG: Decl[LocalVar]/Local: b[#Float#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_1: End completions
  }
}

struct TestFindConstructorParam2 {
  init(a: Int, b: Float) {
    #^FIND_CONSTRUCTOR_PARAM_2^#
// FIND_CONSTRUCTOR_PARAM_2: Begin completions
// FIND_CONSTRUCTOR_PARAM_2-DAG: Decl[LocalVar]/Local: self[#TestFindConstructorParam2#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_2-DAG: Decl[LocalVar]/Local: a[#Int#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_2-DAG: Decl[LocalVar]/Local: b[#Float#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_2: End completions
  }
}

class TestFindConstructorParam3 {
  init<U>(a: Int, b: U) {
    #^FIND_CONSTRUCTOR_PARAM_3^#
// FIND_CONSTRUCTOR_PARAM_3: Begin completions
// FIND_CONSTRUCTOR_PARAM_3-DAG: Decl[GenericTypeParam]/Local: U[#U#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_3-DAG: Decl[LocalVar]/Local:         self[#TestFindConstructorParam3#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_3-DAG: Decl[LocalVar]/Local:         a[#Int#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_3-DAG: Decl[LocalVar]/Local:         b[#U#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_3: End completions
  }
}

class TestFindConstructorParam4<T> {
  init(a: Int, b: T) {
    #^FIND_CONSTRUCTOR_PARAM_4^#
// FIND_CONSTRUCTOR_PARAM_4: Begin completions
// FIND_CONSTRUCTOR_PARAM_4-DAG: Decl[GenericTypeParam]/CurrNominal: T[#T#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_4-DAG: Decl[LocalVar]/Local:               self[#TestFindConstructorParam4<T>#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_4-DAG: Decl[LocalVar]/Local:               a[#Int#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_4-DAG: Decl[LocalVar]/Local:               b[#T#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_4: End completions
  }
}

class TestFindConstructorParam5<T> {
  init<U>(a: Int, b: T, c: U) {
    #^FIND_CONSTRUCTOR_PARAM_5^#
// FIND_CONSTRUCTOR_PARAM_5: Begin completions
// FIND_CONSTRUCTOR_PARAM_5-DAG: Decl[GenericTypeParam]/CurrNominal: T[#T#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_5-DAG: Decl[GenericTypeParam]/Local:       U[#U#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_5-DAG: Decl[LocalVar]/Local:               self[#TestFindConstructorParam5<T>#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_5-DAG: Decl[LocalVar]/Local:               a[#Int#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_5-DAG: Decl[LocalVar]/Local:               b[#T#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_5-DAG: Decl[LocalVar]/Local:               c[#U#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_5: End completions
  }
}

class TestFindConstructorParamSelector1 {
  init(a x: Int, b y: Float) {
    #^FIND_CONSTRUCTOR_PARAM_SELECTOR_1^#
// FIND_CONSTRUCTOR_PARAM_SELECTOR_1: Begin completions
// FIND_CONSTRUCTOR_PARAM_SELECTOR_1-DAG: Decl[LocalVar]/Local: self[#TestFindConstructorParamSelector1#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_SELECTOR_1-DAG: Decl[LocalVar]/Local: x[#Int#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_SELECTOR_1-DAG: Decl[LocalVar]/Local: y[#Float#]{{; name=.+$}}
// FIND_CONSTRUCTOR_PARAM_SELECTOR_1: End completions
  }
}

//===--- Test that we include destructor's 'self' in completion results.

class TestFindDestructorParam1 {
  deinit {
    #^FIND_DESTRUCTOR_PARAM_1^#
// FIND_DESTRUCTOR_PARAM_1: Begin completions
// FIND_DESTRUCTOR_PARAM_1-DAG: Decl[LocalVar]/Local: self[#TestFindDestructorParam1#]{{; name=.+$}}
// FIND_DESTRUCTOR_PARAM_1: End completions
  }
}

class TestFindDestructorParam2<T> {
  deinit {
    #^FIND_DESTRUCTOR_PARAM_2^#
// FIND_DESTRUCTOR_PARAM_2: Begin completions
// FIND_DESTRUCTOR_PARAM_2-DAG: Decl[GenericTypeParam]/CurrNominal: T[#T#]{{; name=.+$}}
// FIND_DESTRUCTOR_PARAM_2-DAG: Decl[LocalVar]/Local: self[#TestFindDestructorParam2<T>#]{{; name=.+$}}
// FIND_DESTRUCTOR_PARAM_2: End completions
  }
}

struct TestPlaceholdersInNames {
  var <#placeholder_in_name1#>: FooStruct
  func test() {
    var <#placeholder_in_name2#>: FooStruct
    #^NO_PLACEHOLDER_NAMES_1^#
  }
// NO_PLACEHOLDER_NAMES_1-NOT: placeholder_in_name
}

//===--- Test that we don't crash in constructors and destructors in contexts
//===--- where they are not allowed.

init() {
  var fooParam = FooStruct()
  #^IN_INVALID_1^#
}

init { // Missing parameters
  var fooParam = FooStruct()
  #^IN_INVALID_2^#
}

deinit {
  var fooParam = FooStruct()
  #^IN_INVALID_3^#
}

func testInInvalid5() {
  var fooParam = FooStruct()
  init() {
    #^IN_INVALID_5^#
  }
}

func testInInvalid6() {
  deinit {
    var fooParam = FooStruct()
    #^IN_INVALID_6^#
  }
}

struct TestInInvalid7 {
  deinit {
    var fooParam = FooStruct()
    #^IN_INVALID_7^#
  }
}

func foo() -> Undeclared {
  var fooParam = FooStruct()
  #^IN_INVALID_8^#
}

func testGenericTypealias1() {
  typealias MyAlias<T> = (T, T)
  let x: MyAlias<Int> = (1, 2)
  var y: (Int, Int)
  y = #^GENERIC_TYPEALIAS_1^#
}
// FIXME: should we use the alias name in the annotation?
// MY_ALIAS: Decl[TypeAlias]/Local:                        MyAlias[#(T, T)#];
// MY_ALIAS: Decl[LocalVar]/Local/TypeRelation[Identical]: x[#(Int, Int)#];
// MY_ALIAS: Decl[LocalVar]/Local/TypeRelation[Identical]: y[#(Int, Int)#];
func testGenericTypealias2() {
  typealias MyAlias<T> = (T, T)
  let x: (Int, Int) = (1, 2)
  var y: MyAlias<Int>
  y = #^GENERIC_TYPEALIAS_2^#
}

func testInForEach1(arg: Int) {
  let local = 2
  for index in #^IN_FOR_EACH_1^# {
    let inBody = 3
  }
  let after = 4
// IN_FOR_EACH_1-NOT: Decl[LocalVar]
// IN_FOR_EACH_1: Decl[LocalVar]/Local:               local[#Int#];
// FIXME: shouldn't show 'after' here.
// IN_FOR_EACH_1: Decl[LocalVar]/Local:               after[#Int#];
// IN_FOR_EACH_1: Decl[LocalVar]/Local:               arg[#Int#];
// IN_FOR_EACH_1-NOT: Decl[LocalVar]
}
func testInForEach2(arg: Int) {
  let local = 2
  for index in 1 ... #^IN_FOR_EACH_2^# {
    let inBody = 3
  }
  let after = 4
}
func testInForEach3(arg: Int) {
  let local = 2
  for index: Int in 1 ... 2 where #^IN_FOR_EACH_3^# {
    let inBody = 3
  }
  let after = 4
// IN_FOR_EACH_3-NOT: Decl[LocalVar]
// IN_FOR_EACH_3: Decl[LocalVar]/Local:               index[#Int#];
// IN_FOR_EACH_3-NOT: Decl[LocalVar]
// IN_FOR_EACH_3: Decl[LocalVar]/Local:               local[#Int#];
// FIXME: shouldn't show 'after' here.
// IN_FOR_EACH_3: Decl[LocalVar]/Local:               after[#Int#];
// IN_FOR_EACH_3: Decl[LocalVar]/Local:               arg[#Int#];
// IN_FOR_EACH_3-NOT: Decl[LocalVar]
}
func testInForEach4(arg: Int) {
  let local = 2
  for index in 1 ... 2 {
    #^IN_FOR_EACH_4^#
  }
  let after = 4
}

@available(*, deprecated)
struct Deprecated {
  @available(*, deprecated)
  func testDeprecated() {
    @available(*, deprecated) let local = 1
    @available(*, deprecated) func f() {}

    #^DEPRECATED_1^#
  }
}
// DEPRECATED_1: Begin completions
// DEPRECATED_1-DAG: Decl[LocalVar]/Local/NotRecommended: local[#Int#];
// DEPRECATED_1-DAG: Decl[FreeFunction]/Local/NotRecommended: f()[#Void#];
// DEPRECATED_1-DAG: Decl[InstanceMethod]/CurrNominal/NotRecommended: testDeprecated()[#Void#];
// DEPRECATED_1-DAG: Decl[Struct]/CurrModule/NotRecommended: Deprecated[#Deprecated#];
// DEPRECATED_1: End completions
