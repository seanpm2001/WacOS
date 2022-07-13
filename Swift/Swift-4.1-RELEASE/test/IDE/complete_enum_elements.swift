// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_SW_1 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=WITH_GLOBAL_RESULTS < %t.enum.txt
// RUN: %FileCheck %s -check-prefix=FOO_ENUM_TYPE_CONTEXT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_SW_2 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=WITH_GLOBAL_RESULTS < %t.enum.txt
// RUN: %FileCheck %s -check-prefix=FOO_ENUM_TYPE_CONTEXT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_SW_3 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=WITH_GLOBAL_RESULTS < %t.enum.txt
// RUN: %FileCheck %s -check-prefix=BAR_ENUM_TYPE_CONTEXT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_SW_4 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=WITH_GLOBAL_RESULTS < %t.enum.txt
// RUN: %FileCheck %s -check-prefix=BAZ_ENUM_TYPE_CONTEXT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_SW_5 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=WITH_GLOBAL_RESULTS < %t.enum.txt
// RUN: %FileCheck %s -check-prefix=QUX_ENUM_TYPE_CONTEXT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_SW_WITH_DOT_1 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=FOO_ENUM_DOT_ELEMENTS < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_SW_WITH_QUAL_1 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=FOO_ENUM_DOT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_SW_EXPR_ERROR_1 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=FOO_ENUM_DOT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_SW_IN_PATTERN_1 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=WITH_GLOBAL_RESULTS < %t.enum.txt
// RUN: %FileCheck %s -check-prefix=ENUM_SW_IN_PATTERN_1 < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_SW_IN_PATTERN_2 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=ENUM_SW_IN_PATTERN_2 < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_QUAL_NO_DOT_1 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=FOO_ENUM_NO_DOT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_QUAL_NO_DOT_2 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=BAR_ENUM_NO_DOT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_QUAL_NO_DOT_3 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=BAZ_INT_ENUM_NO_DOT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_QUAL_NO_DOT_4 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=BAZ_T_ENUM_NO_DOT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_QUAL_NO_DOT_5 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=QUX_ENUM_NO_DOT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_QUAL_DOT_1 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=FOO_ENUM_DOT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_QUAL_DOT_2 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=BAR_ENUM_DOT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_QUAL_DOT_3 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=BAZ_INT_ENUM_DOT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_QUAL_DOT_4 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=BAZ_T_ENUM_DOT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ENUM_QUAL_DOT_5 > %t.enum.txt
// RUN: %FileCheck %s -check-prefix=QUX_ENUM_DOT < %t.enum.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=WITH_INVALID_DOT_1 | %FileCheck %s -check-prefix=WITH_INVALID_DOT
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=UNRESOLVED_1 | %FileCheck %s -check-prefix=UNRESOLVED_1

//===---
//===--- Test that we can complete enum elements.
//===---

//===--- Helper types.

enum FooEnum {
  case Foo1
  case Foo2
}

// FOO_ENUM_TYPE_CONTEXT: Begin completions
// FOO_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Foo1[#FooEnum#]{{; name=.+$}}
// FOO_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Foo2[#FooEnum#]{{; name=.+$}}
// FOO_ENUM_TYPE_CONTEXT: End completions

// FOO_ENUM_NO_DOT: Begin completions
// FOO_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal: .Foo1[#FooEnum#]{{; name=.+$}}
// FOO_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal: .Foo2[#FooEnum#]{{; name=.+$}}
// FOO_ENUM_NO_DOT-NEXT: End completions

// FOO_ENUM_DOT: Begin completions
// FOO_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal: Foo1[#FooEnum#]{{; name=.+$}}
// FOO_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal: Foo2[#FooEnum#]{{; name=.+$}}
// FOO_ENUM_DOT-NEXT: End completions

// FOO_ENUM_DOT_ELEMENTS: Begin completions, 2 items
// FOO_ENUM_DOT_ELEMENTS-NEXT: Decl[EnumElement]/ExprSpecific: Foo1[#FooEnum#]{{; name=.+$}}
// FOO_ENUM_DOT_ELEMENTS-NEXT: Decl[EnumElement]/ExprSpecific: Foo2[#FooEnum#]{{; name=.+$}}
// FOO_ENUM_DOT_ELEMENTS-NEXT: End completions

enum BarEnum {
  case Bar1
  case Bar2()
  case Bar3(Int)
  case Bar4(a: Int, b: Float)
  case Bar5(a: Int, (Float))
  case Bar6(a: Int, b: (Float))
  case Bar7(a: Int, (b: Float, c: Double))
  case Bar8(a: Int, b: (c: Float, d: Double))
  case Bar9(Int)
  case Bar10(Int, Float)
  case Bar11(Int, (Float))
  case Bar12(Int, (Float, Double))

  mutating
  func barInstanceFunc() {}
  static var staticVar: Int = 12
  static func barStaticFunc() {}
}

// BAR_ENUM_TYPE_CONTEXT: Begin completions
// BAR_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Bar1[#BarEnum#]{{; name=.+$}}
// BAR_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Bar2()[#() -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Bar3({#Int#})[#(Int) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Bar4({#a: Int#}, {#b: Float#})[#(Int, Float) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Bar6({#a: Int#}, {#Float#})[#(Int, (Float)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Bar6({#a: Int#}, {#Float#})[#(Int, (Float)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Bar7({#a: Int#}, ({#b: Float#}, {#c: Double#}))[#(Int, (b: Float, c: Double)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Bar8({#a: Int#}, b: ({#c: Float#}, {#d: Double#}))[#(Int, (c: Float, d: Double)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Bar9({#Int#})[#(Int) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Bar10({#Int#}, {#Float#})[#(Int, Float) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Bar11({#Int#}, {#Float#})[#(Int, (Float)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Bar12({#Int#}, ({#Float#}, {#Double#}))[#(Int, (Float, Double)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_TYPE_CONTEXT: End completions

// BAR_ENUM_NO_DOT: Begin completions
// BAR_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Bar1[#BarEnum#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Bar2()[#() -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Bar3({#Int#})[#(Int) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Bar4({#a: Int#}, {#b: Float#})[#(Int, Float) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Bar5({#a: Int#}, {#Float#})[#(Int, (Float)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Bar6({#a: Int#}, {#Float#})[#(Int, (Float)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Bar7({#a: Int#}, ({#b: Float#}, {#c: Double#}))[#(Int, (b: Float, c: Double)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Bar8({#a: Int#}, b: ({#c: Float#}, {#d: Double#}))[#(Int, (c: Float, d: Double)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Bar9({#Int#})[#(Int) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Bar10({#Int#}, {#Float#})[#(Int, Float) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Bar11({#Int#}, {#Float#})[#(Int, (Float)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Bar12({#Int#}, ({#Float#}, {#Double#}))[#(Int, (Float, Double)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[InstanceMethod]/CurrNominal: .barInstanceFunc({#self: &BarEnum#})[#() -> Void#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[StaticVar]/CurrNominal:      .staticVar[#Int#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: Decl[StaticMethod]/CurrNominal:   .barStaticFunc()[#Void#]{{; name=.+$}}
// BAR_ENUM_NO_DOT-NEXT: End completions

// BAR_ENUM_DOT: Begin completions
// BAR_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Bar1[#BarEnum#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Bar2()[#() -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Bar3({#Int#})[#(Int) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Bar4({#a: Int#}, {#b: Float#})[#(Int, Float) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Bar5({#a: Int#}, {#Float#})[#(Int, (Float)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Bar6({#a: Int#}, {#Float#})[#(Int, (Float)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Bar7({#a: Int#}, ({#b: Float#}, {#c: Double#}))[#(Int, (b: Float, c: Double)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Bar8({#a: Int#}, b: ({#c: Float#}, {#d: Double#}))[#(Int, (c: Float, d: Double)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Bar9({#Int#})[#(Int) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Bar10({#Int#}, {#Float#})[#(Int, Float) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Bar11({#Int#}, {#Float#})[#(Int, (Float)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Bar12({#Int#}, ({#Float#}, {#Double#}))[#(Int, (Float, Double)) -> BarEnum#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[InstanceMethod]/CurrNominal/NotRecommended/TypeRelation[Invalid]: barInstanceFunc({#self: &BarEnum#})[#() -> Void#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[StaticVar]/CurrNominal:      staticVar[#Int#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: Decl[StaticMethod]/CurrNominal/NotRecommended/TypeRelation[Invalid]:   barStaticFunc()[#Void#]{{; name=.+$}}
// BAR_ENUM_DOT-NEXT: End completions

enum BazEnum<T> {
  case Baz1
  case Baz2(T)

  mutating
  func bazInstanceFunc() {}
  static var staticVar: Int = 12
  static var staticVarT: T = 17
  static func bazStaticFunc() {}
}

// BAZ_ENUM_TYPE_CONTEXT: Begin completions
// BAZ_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Baz1[#BazEnum<T>#]{{; name=.+$}}
// BAZ_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Baz2({#T#})[#(T) -> BazEnum<T>#]{{; name=.+$}}
// BAZ_ENUM_TYPE_CONTEXT: End completions

// BAZ_INT_ENUM_NO_DOT: Begin completions, 6 items
// BAZ_INT_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Baz1[#BazEnum<T>#]{{; name=.+$}}
// BAZ_INT_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Baz2({#T#})[#(T) -> BazEnum<T>#]{{; name=.+$}}
// BAZ_INT_ENUM_NO_DOT-NEXT: Decl[InstanceMethod]/CurrNominal: .bazInstanceFunc({#self: &BazEnum<Int>#})[#() -> Void#]{{; name=.+$}}
// BAZ_INT_ENUM_NO_DOT-NEXT: Decl[StaticVar]/CurrNominal:      .staticVar[#Int#]{{; name=.+$}}
// BAZ_INT_ENUM_NO_DOT-NEXT: Decl[StaticVar]/CurrNominal:      .staticVarT[#Int#]{{; name=.+$}}
// BAZ_INT_ENUM_NO_DOT-NEXT: Decl[StaticMethod]/CurrNominal:   .bazStaticFunc()[#Void#]{{; name=.+$}}
// BAZ_INT_ENUM_NO_DOT-NEXT: End completions

// BAZ_T_ENUM_NO_DOT: Begin completions
// BAZ_T_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Baz1[#BazEnum<T>#]{{; name=.+$}}
// BAZ_T_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Baz2({#T#})[#(T) -> BazEnum<T>#]{{; name=.+$}}
// BAZ_T_ENUM_NO_DOT-NEXT: Decl[InstanceMethod]/CurrNominal: .bazInstanceFunc({#self: &BazEnum<_>#})[#() -> Void#]{{; name=.+$}}
// BAZ_T_ENUM_NO_DOT-NEXT: Decl[StaticVar]/CurrNominal:      .staticVar[#Int#]{{; name=.+$}}
// BAZ_T_ENUM_NO_DOT-NEXT: Decl[StaticVar]/CurrNominal:      .staticVarT[#_#]{{; name=.+$}}
// BAZ_T_ENUM_NO_DOT-NEXT: Decl[StaticMethod]/CurrNominal:   .bazStaticFunc()[#Void#]{{; name=.+$}}
// BAZ_T_ENUM_NO_DOT-NEXT: Decl[InfixOperatorFunction]/OtherModule[Swift]: == {#Any.Type?#}[#Bool#]; name=== Any.Type?
// BAZ_T_ENUM_NO_DOT-NEXT: Decl[InfixOperatorFunction]/OtherModule[Swift]: != {#Any.Type?#}[#Bool#]; name=!= Any.Type?
// BAZ_T_ENUM_NO_DOT-NEXT: End completions

// BAZ_INT_ENUM_DOT: Begin completions, 6 items
// BAZ_INT_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Baz1[#BazEnum<T>#]{{; name=.+$}}
// BAZ_INT_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Baz2({#T#})[#(T) -> BazEnum<T>#]{{; name=.+$}}
// BAZ_INT_ENUM_DOT-NEXT: Decl[InstanceMethod]/CurrNominal/NotRecommended/TypeRelation[Invalid]: bazInstanceFunc({#self: &BazEnum<Int>#})[#() -> Void#]{{; name=.+$}}
// BAZ_INT_ENUM_DOT-NEXT: Decl[StaticVar]/CurrNominal:      staticVar[#Int#]{{; name=.+$}}
// BAZ_INT_ENUM_DOT-NEXT: Decl[StaticVar]/CurrNominal:      staticVarT[#Int#]{{; name=.+$}}
// BAZ_INT_ENUM_DOT-NEXT: Decl[StaticMethod]/CurrNominal/NotRecommended/TypeRelation[Invalid]:   bazStaticFunc()[#Void#]{{; name=.+$}}
// BAZ_INT_ENUM_DOT-NEXT: End completions

// BAZ_T_ENUM_DOT: Begin completions, 6 items
// BAZ_T_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Baz1[#BazEnum<T>#]{{; name=.+$}}
// BAZ_T_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Baz2({#T#})[#(T) -> BazEnum<T>#]{{; name=.+$}}
// BAZ_T_ENUM_DOT-NEXT: Decl[InstanceMethod]/CurrNominal: bazInstanceFunc({#self: &BazEnum<_>#})[#() -> Void#]{{; name=.+$}}
// BAZ_T_ENUM_DOT-NEXT: Decl[StaticVar]/CurrNominal:      staticVar[#Int#]{{; name=.+$}}
// BAZ_T_ENUM_DOT-NEXT: Decl[StaticVar]/CurrNominal:      staticVarT[#_#]{{; name=.+$}}
// BAZ_T_ENUM_DOT-NEXT: Decl[StaticMethod]/CurrNominal:   bazStaticFunc()[#Void#]{{; name=.+$}}
// BAZ_T_ENUM_DOT-NEXT: End completions

enum QuxEnum : Int {
  case Qux1 = 10
  case Qux2 = 20
}

// QUX_ENUM_TYPE_CONTEXT: Begin completions
// QUX_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Qux1[#QuxEnum#]{{; name=.+$}}
// QUX_ENUM_TYPE_CONTEXT-DAG: Decl[EnumElement]/ExprSpecific: .Qux2[#QuxEnum#]{{; name=.+$}}
// QUX_ENUM_TYPE_CONTEXT: End completions

// QUX_ENUM_NO_DOT: Begin completions, 4 items
// QUX_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Qux1[#QuxEnum#]{{; name=.+$}}
// QUX_ENUM_NO_DOT-NEXT: Decl[EnumElement]/CurrNominal:    .Qux2[#QuxEnum#]{{; name=.+$}}
// QUX_ENUM_NO_DOT-NEXT: Decl[TypeAlias]/CurrNominal:      .RawValue[#Int#]{{; name=.+$}}
// QUX_ENUM_NO_DOT-NEXT: Decl[Constructor]/CurrNominal:    ({#rawValue: Int#})[#QuxEnum?#]{{; name=.+$}}
// QUX_ENUM_NO_DOT-NEXT: End completions

// QUX_ENUM_DOT: Begin completions, 4 items
// QUX_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Qux1[#QuxEnum#]{{; name=.+$}}
// QUX_ENUM_DOT-NEXT: Decl[EnumElement]/CurrNominal:    Qux2[#QuxEnum#]{{; name=.+$}}
// QUX_ENUM_DOT-NEXT: Decl[TypeAlias]/CurrNominal:      RawValue[#Int#]{{; name=.+$}}
// QUX_ENUM_DOT-NEXT: Decl[Constructor]/CurrNominal:    init({#rawValue: Int#})[#QuxEnum?#]{{; name=.+$}}
// QUX_ENUM_DOT-NEXT: End completions

func freeFunc() {}

// WITH_GLOBAL_RESULTS: Begin completions
// WITH_GLOBAL_RESULTS: Decl[FreeFunction]/CurrModule: freeFunc()[#Void#]{{; name=.+$}}
// WITH_GLOBAL_RESULTS: End completions

//===--- Complete enum elements in 'switch'.

func testSwitch1(e: FooEnum) {
  switch e {
  case #^ENUM_SW_1^#
  }
}
func testSwitch2(e: FooEnum) {
  switch e {
  case .Foo1:
  case #^ENUM_SW_2^#
  }
}
func testSwitch3(e: BarEnum) {
  switch e {
  case #^ENUM_SW_3^#
  }
}
func testSwitch4(e: BazEnum<Int>) {
  switch e {
  case #^ENUM_SW_4^#
  }
}
func testSwitch5(e: QuxEnum) {
  switch e {
  case #^ENUM_SW_5^#
  }
}

func testSwitchWithDot1(e: FooEnum) {
  switch e {
  case .#^ENUM_SW_WITH_DOT_1^#
  }
}

func testSwitchWithQualification1(e: FooEnum) {
  switch e {
  case FooEnum.#^ENUM_SW_WITH_QUAL_1^#
  }
}

func testSwitchExprError1() {
  switch unknown_var {
  case FooEnum.#^ENUM_SW_EXPR_ERROR_1^#
  }
}

// FIXME
func testSwitchInPattern1(e: BazEnum<Int>) {
  switch e {
  case .Baz2(#^ENUM_SW_IN_PATTERN_1^#
  }
}
// ENUM_SW_IN_PATTERN_1-NOT: .Baz1

func testSwitchInPattern2(e: BazEnum<Int>) {
  switch e {
  case .Baz2(.#^ENUM_SW_IN_PATTERN_2^#
  }
}
// ENUM_SW_IN_PATTERN_2-NOT: .Baz1

//===--- Complete qualified references to enum elements.

func testQualifiedNoDot1() {
  var e = FooEnum#^ENUM_QUAL_NO_DOT_1^#
}
func testQualifiedNoDot2() {
  var e = BarEnum#^ENUM_QUAL_NO_DOT_2^#
}
func testQualifiedNoDot3() {
  var e = BazEnum<Int>#^ENUM_QUAL_NO_DOT_3^#
}
func testQualifiedNoDot4() {
  var e = BazEnum#^ENUM_QUAL_NO_DOT_4^#
}
func testQualifiedNoDot5() {
  var e = QuxEnum#^ENUM_QUAL_NO_DOT_5^#
}

func testQualifiedDot1() {
  var e = FooEnum.#^ENUM_QUAL_DOT_1^#
}
func testQualifiedDot2() {
  var e = BarEnum.#^ENUM_QUAL_DOT_2^#
}
func testQualifiedDot3() {
  var e = BazEnum<Int>.#^ENUM_QUAL_DOT_3^#
}
func testQualifiedDot4() {
  var e = BazEnum.#^ENUM_QUAL_DOT_4^#
}
func testQualifiedDot5() {
  var e = QuxEnum.#^ENUM_QUAL_DOT_5^#
}

// ===--- Complete in the presence of invalid enum elements.

enum WithInvalid {
  case Okay
  case NotOkay.
  case .AlsoNotOkay
  case
  case JustFine
}

func testWithInvalid1() {
  let x = WithInvalid.#^WITH_INVALID_DOT_1^#

// WITH_INVALID_DOT: Begin completions
// WITH_INVALID_DOT-DAG: Decl[EnumElement]/CurrNominal:      Okay[#WithInvalid#]; name=Okay
// WITH_INVALID_DOT-DAG: Decl[EnumElement]/CurrNominal:      NotOkay[#WithInvalid#]; name=NotOkay
// WITH_INVALID_DOT-DAG: Decl[EnumElement]/CurrNominal:      AlsoNotOkay[#WithInvalid#]; name=AlsoNotOkay
// WITH_INVALID_DOT-DAG: Decl[EnumElement]/CurrNominal:      JustFine[#WithInvalid#]; name=JustFine
// WITH_INVALID_DOT: End completions

  var y : QuxEnum
  y = .#^UNRESOLVED_1^#
// FIXME: Only contains resolvable ones.
// UNRESOLVED_1:  Begin completions
// UNRESOLVED_1-NOT:  Baz
// UNRESOLVED_1-NOT:  Bar
// UNRESOLVED_1-DAG:  Decl[EnumElement]/ExprSpecific:     Qux1[#QuxEnum#]; name=Qux1
// UNRESOLVED_1-DAG:  Decl[EnumElement]/ExprSpecific:     Qux2[#QuxEnum#]; name=Qux2
// UNRESOLVED_1-NOT:  Okay
}
