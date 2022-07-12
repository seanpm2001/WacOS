// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=AFTER_PAREN | %FileCheck %s -check-prefix=AFTER_PAREN
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ARG_MyEnum_NODOT | %FileCheck %s -check-prefix=ARG_MyEnum_NODOT
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ARG_MyEnum_DOT | %FileCheck %s -check-prefix=ARG_MyEnum_DOT
// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=ARG_MyEnum_NOBINDING | %FileCheck %s -check-prefix=ARG_MyEnum_NOBINDING

enum MyEnum {
  case east, west
}

@propertyWrapper
struct MyStruct {
  var wrappedValue: MyEnum
  init(wrappedValue: MyEnum) {}
  init(arg1: MyEnum, arg2: Int) {}
}

var globalInt: Int = 1
var globalMyEnum: MyEnum = .east

struct TestStruct {
  @MyStruct(#^AFTER_PAREN^#
  var test1
// AFTER_PAREN: Begin completions, 2 items
// AFTER_PAREN-DAG: Decl[Constructor]/CurrNominal/Flair[ArgLabels]:      ['(']{#wrappedValue: MyEnum#}[')'][#MyStruct#]; name=wrappedValue:
// AFTER_PAREN-DAG: Decl[Constructor]/CurrNominal/Flair[ArgLabels]:      ['(']{#arg1: MyEnum#}, {#arg2: Int#}[')'][#MyStruct#]; name=arg1:arg2:
// AFTER_PAREN: End completions

  @MyStruct(arg1: #^ARG_MyEnum_NODOT^#
  var test2
// ARG_MyEnum_NODOT: Begin completions
// ARG_MyEnum_NODOT-DAG: Decl[Struct]/CurrModule:            TestStruct[#TestStruct#]; name=TestStruct
// ARG_MyEnum_NODOT-DAG: Decl[GlobalVar]/CurrModule/TypeRelation[Identical]: globalMyEnum[#MyEnum#]; name=globalMyEnum
// ARG_MyEnum_NODOT: End completions

  @MyStruct(arg1: .#^ARG_MyEnum_DOT^#
  var test3
// ARG_MyEnum_DOT: Begin completions, 3 items
// ARG_MyEnum_DOT-DAG: Decl[EnumElement]/CurrNominal/Flair[ExprSpecific]/TypeRelation[Identical]:     east[#MyEnum#]; name=east
// ARG_MyEnum_DOT-DAG: Decl[EnumElement]/CurrNominal/Flair[ExprSpecific]/TypeRelation[Identical]:     west[#MyEnum#]; name=west
// ARG_MyEnum_DOT-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Invalid]: hash({#(self): MyEnum#})[#(into: inout Hasher) -> Void#];
// ARG_MyEnum_DOT: End completions

  @MyStruct(arg1: MyEnum.#^ARG_MyEnum_NOBINDING^#)
// ARG_MyEnum_NOBINDING: Begin completions
// ARG_MyEnum_NOBINDING-DAG: Decl[EnumElement]/CurrNominal: east[#MyEnum#];
// ARG_MyEnum_NOBINDING-DAG: Decl[EnumElement]/CurrNominal: west[#MyEnum#];
// ARG_MyEnum_NOBINDING: End completions
}
