// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=TUPLE_NO_DOT_1 > %t.tuple.txt
// RUN: %FileCheck %s -check-prefix=TUPLE_NO_DOT_1 < %t.tuple.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=TUPLE_NO_DOT_2 > %t.tuple.txt
// RUN: %FileCheck %s -check-prefix=TUPLE_NO_DOT_2 < %t.tuple.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=TUPLE_NO_DOT_3 > %t.tuple.txt
// RUN: %FileCheck %s -check-prefix=TUPLE_NO_DOT_3 < %t.tuple.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=TUPLE_DOT_1 > %t.tuple.txt
// RUN: %FileCheck %s -check-prefix=TUPLE_DOT_1 < %t.tuple.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=TUPLE_DOT_2 > %t.tuple.txt
// RUN: %FileCheck %s -check-prefix=TUPLE_DOT_2 < %t.tuple.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=TUPLE_DOT_3 > %t.tuple.txt
// RUN: %FileCheck %s -check-prefix=TUPLE_DOT_3 < %t.tuple.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=TUPLE_NESTED_1 > %t.tuple.txt
// RUN: %FileCheck %s -check-prefix=TUPLE_NESTED_1 < %t.tuple.txt

//===---
//===--- Test code completion for expressions that have tuple type.
//===---

func testTupleNoDot1() {
  var t = (1, 2.0)
  t#^TUPLE_NO_DOT_1^#
}
// TUPLE_NO_DOT_1: Begin completions, 9 items
// TUPLE_NO_DOT_1-DAG: Pattern/CurrNominal: .0[#Int#]{{; name=.+$}}
// TUPLE_NO_DOT_1-DAG: Pattern/CurrNominal: .1[#Double#]{{; name=.+$}}
// TUPLE_NO_DOT_1-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: == {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_1-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: <= {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_1-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: >= {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_1-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: < {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_1-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: != {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_1-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: > {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_1-DAG: BuiltinOperator/None:                                   = {#(Int, Double)#}[#Void#]{{; name=.+$}}
// TUPLE_NO_DOT_1-NEXT: End completions

func testTupleNoDot2() {
  var t = (foo: 1, bar: 2.0)
  t#^TUPLE_NO_DOT_2^#
}
// TUPLE_NO_DOT_2: Begin completions, 9 items
// TUPLE_NO_DOT_2-DAG: Pattern/CurrNominal: .foo[#Int#]{{; name=.+$}}
// TUPLE_NO_DOT_2-DAG: Pattern/CurrNominal: .bar[#Double#]{{; name=.+$}}
// TUPLE_NO_DOT_2-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: == {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_2-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: <= {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_2-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: >= {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_2-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: < {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_2-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: != {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_2-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: > {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_2-DAG: BuiltinOperator/None:                                   = {#(foo: Int, bar: Double)#}[#Void#]{{; name=.+$}}
// TUPLE_NO_DOT_2-NEXT: End completions

func testTupleNoDot3() {
  var t = (foo: 1, 2.0)
  t#^TUPLE_NO_DOT_3^#
}
// TUPLE_NO_DOT_3: Begin completions, 9 items
// TUPLE_NO_DOT_3-DAG: Pattern/CurrNominal: .foo[#Int#]{{; name=.+$}}
// TUPLE_NO_DOT_3-DAG: Pattern/CurrNominal: .1[#Double#]{{; name=.+$}}
// TUPLE_NO_DOT_3-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: == {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_3-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: <= {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_3-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: >= {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_3-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: < {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_3-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: != {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_3-DAG: Decl[InfixOperatorFunction]/OtherModule[Swift]: > {#(Int, Double)#}[#Bool#]{{; name=.+$}}
// TUPLE_NO_DOT_3-DAG: BuiltinOperator/None:                                   = {#(foo: Int, Double)#}[#Void#]{{; name=.+$}}
// TUPLE_NO_DOT_3-NEXT: End completions

func testTupleDot1() {
  var t = (1, 2.0)
  t.#^TUPLE_DOT_1^#
}
// TUPLE_DOT_1: Begin completions, 2 items
// TUPLE_DOT_1-NEXT: Pattern/CurrNominal: 0[#Int#]{{; name=.+$}}
// TUPLE_DOT_1-NEXT: Pattern/CurrNominal: 1[#Double#]{{; name=.+$}}
// TUPLE_DOT_1-NEXT: End completions

func testTupleDot2() {
  var t = (foo: 1, bar: 2.0)
  t.#^TUPLE_DOT_2^#
}
// TUPLE_DOT_2: Begin completions, 2 items
// TUPLE_DOT_2-NEXT: Pattern/CurrNominal: foo[#Int#]{{; name=.+$}}
// TUPLE_DOT_2-NEXT: Pattern/CurrNominal: bar[#Double#]{{; name=.+$}}
// TUPLE_DOT_2-NEXT: End completions

func testTupleDot3() {
  var t = (foo: 1, 2.0)
  t.#^TUPLE_DOT_3^#
}
// TUPLE_DOT_3: Begin completions, 2 items
// TUPLE_DOT_3-NEXT: Pattern/CurrNominal: foo[#Int#]{{; name=.+$}}
// TUPLE_DOT_3-NEXT: Pattern/CurrNominal: 1[#Double#]{{; name=.+$}}
// TUPLE_DOT_3-NEXT: End completions

struct FooStruct {
  var fooInstanceVar: Int = 0
  var barInstanceVar: Double = 0.0
}

func testTupleNested1() {
  var t = (foo: FooStruct(), i: Int)
  t.foo.#^TUPLE_NESTED_1^#
}
// TUPLE_NESTED_1: Begin completions, 2 items
// TUPLE_NESTED_1-NEXT: Decl[InstanceVar]/CurrNominal: fooInstanceVar[#Int#]{{; name=.+$}}
// TUPLE_NESTED_1-NEXT: Decl[InstanceVar]/CurrNominal: barInstanceVar[#Double#]{{; name=.+$}}
// TUPLE_NESTED_1-NEXT: End completions
