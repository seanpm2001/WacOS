// RUN: rm -rf %t
// RUN: mkdir -p %t
// RUN: %target-swift-frontend -emit-module -disable-objc-attr-requires-foundation-module -o %t %S/Inputs/AnyObject/foo_swift_module.swift
// RUN: %target-swift-frontend -emit-module -disable-objc-attr-requires-foundation-module -o %t %S/Inputs/AnyObject/bar_swift_module.swift
// RUN: cp %S/Inputs/AnyObject/baz_clang_module.h %t
// RUN: cp %S/Inputs/AnyObject/module.map %t

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_FUNC_PARAM_NO_DOT_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=DL_INSTANCE_NO_DOT < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_FUNC_PARAM_DOT_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=DL_INSTANCE_DOT < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_VAR_NO_DOT_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=DL_INSTANCE_NO_DOT < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_VAR_DOT_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=DL_INSTANCE_DOT < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_RETURN_VAL_NO_DOT_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=DL_INSTANCE_NO_DOT < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_RETURN_VAL_DOT_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=DL_INSTANCE_DOT < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_CALL_RETURN_VAL_NO_DOT_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=TLOC_MEMBERS_NO_DOT < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_CALL_RETURN_VAL_DOT_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=TLOC_MEMBERS_DOT < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_FUNC_NAME_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=DL_FUNC_NAME_1 < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_FUNC_NAME_PAREN_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=DL_FUNC_NAME_PAREN_1 < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_FUNC_NAME_DOT_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=DL_FUNC_NAME_DOT_1 < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_FUNC_NAME_BANG_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=DL_FUNC_NAME_BANG_1 < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_CLASS_NO_DOT_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=DL_CLASS_NO_DOT < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -I %t -disable-objc-attr-requires-foundation-module -code-completion-token=DL_CLASS_DOT_1 > %t.dl.txt
// RUN: %FileCheck %s -check-prefix=DL_CLASS_DOT < %t.dl.txt
// RUN: %FileCheck %s -check-prefix=GLOBAL_NEGATIVE < %t.dl.txt

// REQUIRES: objc_interop

import foo_swift_module
import class bar_swift_module.Bar_ImportedObjcClass
import baz_clang_module

//===---
//===--- Helper types that are used in this test.
//===---

@objc class Base {}
@objc class Derived : Base {}

protocol Foo { func foo() }
protocol Bar { func bar() }

//===---
//===--- Types that contain members accessible by dynamic lookup.
//===---

// GLOBAL_NEGATIVE-NOT: ERROR

// DL_INSTANCE_NO_DOT: Begin completions
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[bar_swift_module]: .bar_ImportedObjcClass_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[bar_swift_module]:    .bar_ImportedObjcClass_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .base1_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .base1_InstanceFunc2!({#(a): Derived#})[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .base1_InstanceFunc3!({#(a): Derived#})[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .base1_InstanceFunc4!()[#Base#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      .base1_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      .base1_Property2[#Base?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[baz_clang_module]: .baz_Class_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[baz_clang_module]: .baz_Protocol_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: .foo_Nested1_ObjcInstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[foo_swift_module]:    .foo_Nested1_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: .foo_Nested2_ObjcInstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[foo_swift_module]:    .foo_Nested2_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: .foo_TopLevelClass_ObjcInstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[foo_swift_module]:    .foo_TopLevelClass_ObjcProperty1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: .foo_TopLevelObjcClass_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[foo_swift_module]:    .foo_TopLevelObjcClass_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: .foo_TopLevelObjcProtocol_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[foo_swift_module]:    .foo_TopLevelObjcProtocol_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .nested1_ObjcInstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      .nested1_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .nested2_ObjcInstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      .nested2_Property[#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .returnsObjcClass!({#(i): Int#})[#TopLevelObjcClass#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .topLevelClass_ObjcInstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      .topLevelClass_ObjcProperty1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .topLevelObjcClass_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      .topLevelObjcClass_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .topLevelObjcProtocol_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      .topLevelObjcProtocol_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[Subscript]/OtherModule[bar_swift_module]:      [{#Bar_ImportedObjcClass#}][#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[Subscript]/OtherModule[foo_swift_module]:      [{#Foo_TopLevelObjcProtocol#}][#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[Subscript]/OtherModule[swift_ide_test]:        [{#Int16#}][#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[Subscript]/OtherModule[foo_swift_module]:      [{#Int32#}][#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[Subscript]/OtherModule[foo_swift_module]:      [{#Int64#}][#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[Subscript]/OtherModule[swift_ide_test]:        [{#Int8#}][#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[Subscript]/OtherModule[swift_ide_test]:        [{#TopLevelObjcClass#}][#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[Subscript]/OtherModule[swift_ide_test]:        [{#TopLevelObjcProtocol#}][#Int?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[Subscript]/OtherModule[baz_clang_module]:      [{#Int32#}][#Any!?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT-DAG: Decl[Subscript]/OtherModule[baz_clang_module]:      [{#Any!#}][#Any!?#]{{; name=.+$}}
// DL_INSTANCE_NO_DOT: End completions
// GLOBAL_NEGATIVE-NOT:.objectAtIndexedSubscript

// DL_INSTANCE_DOT: Begin completions
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[bar_swift_module]: bar_ImportedObjcClass_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[bar_swift_module]:    bar_ImportedObjcClass_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   base1_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   base1_InstanceFunc2!({#(a): Derived#})[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   base1_InstanceFunc3!({#(a): Derived#})[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   base1_InstanceFunc4!()[#Base#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      base1_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      base1_Property2[#Base?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[baz_clang_module]: baz_Class_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[baz_clang_module]:    baz_Class_Property1[#Baz_Class!?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[baz_clang_module]:    baz_Class_Property2[#Baz_Class!?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[baz_clang_module]: baz_Protocol_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: foo_Nested1_ObjcInstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[foo_swift_module]:    foo_Nested1_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: foo_Nested2_ObjcInstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[foo_swift_module]:    foo_Nested2_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: foo_TopLevelClass_ObjcInstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[foo_swift_module]:    foo_TopLevelClass_ObjcProperty1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: foo_TopLevelObjcClass_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[foo_swift_module]:    foo_TopLevelObjcClass_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: foo_TopLevelObjcProtocol_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[foo_swift_module]:    foo_TopLevelObjcProtocol_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   nested1_ObjcInstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      nested1_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   nested2_ObjcInstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      nested2_Property[#Int?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   returnsObjcClass!({#(i): Int#})[#TopLevelObjcClass#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   topLevelClass_ObjcInstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      topLevelClass_ObjcProperty1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   topLevelObjcClass_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      topLevelObjcClass_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   topLevelObjcProtocol_InstanceFunc1!()[#Void#]{{; name=.+$}}
// DL_INSTANCE_DOT-DAG: Decl[InstanceVar]/OtherModule[swift_ide_test]:      topLevelObjcProtocol_Property1[#Int?#]{{; name=.+$}}
// DL_INSTANCE_DOT: End completions

// DL_CLASS_NO_DOT: Begin completions
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[bar_swift_module]:   .bar_ImportedObjcClass_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[bar_swift_module]: .bar_ImportedObjcClass_InstanceFunc1({#self: Bar_ImportedObjcClass#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .base1_InstanceFunc1({#self: Base1#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .base1_InstanceFunc2({#self: Base1#})[#(Derived) -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .base1_InstanceFunc3({#self: Base1#})[#(Derived) -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .base1_InstanceFunc4({#self: Base1#})[#() -> Base#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[baz_clang_module]:   .baz_Class_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[baz_clang_module]: .baz_Class_InstanceFunc1({#self: Baz_Class#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[baz_clang_module]:   .baz_Protocol_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[baz_clang_module]: .baz_Protocol_InstanceFunc1({#self: Self#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[foo_swift_module]:   .foo_Nested1_ObjcClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: .foo_Nested1_ObjcInstanceFunc1({#self: Foo_ContainerForNestedClass1.Foo_Nested1#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[foo_swift_module]:   .foo_Nested2_ObjcClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: .foo_Nested2_ObjcInstanceFunc1({#self: Foo_ContainerForNestedClass2.Foo_Nested2#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[foo_swift_module]:   .foo_TopLevelClass_ObjcClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: .foo_TopLevelClass_ObjcInstanceFunc1({#self: Foo_TopLevelClass#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[foo_swift_module]:   .foo_TopLevelObjcClass_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: .foo_TopLevelObjcClass_InstanceFunc1({#self: Foo_TopLevelObjcClass#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[foo_swift_module]:   .foo_TopLevelObjcProtocol_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: .foo_TopLevelObjcProtocol_InstanceFunc1({#self: Self#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[swift_ide_test]:     .nested1_ObjcClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .nested1_ObjcInstanceFunc1({#self: ContainerForNestedClass1.Nested1#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[swift_ide_test]:     .nested2_ObjcClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .nested2_ObjcInstanceFunc1({#self: ContainerForNestedClass2.Nested2#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .returnsObjcClass({#self: TopLevelObjcClass#})[#(Int) -> TopLevelObjcClass#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[swift_ide_test]:     .topLevelClass_ObjcClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .topLevelClass_ObjcInstanceFunc1({#self: TopLevelClass#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[swift_ide_test]:     .topLevelObjcClass_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .topLevelObjcClass_InstanceFunc1({#self: TopLevelObjcClass#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[StaticMethod]/OtherModule[swift_ide_test]:     .topLevelObjcProtocol_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   .topLevelObjcProtocol_InstanceFunc1({#self: Self#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_NO_DOT: End completions

// DL_CLASS_DOT: Begin completions
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[bar_swift_module]:   bar_ImportedObjcClass_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[bar_swift_module]: bar_ImportedObjcClass_InstanceFunc1({#self: Bar_ImportedObjcClass#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   base1_InstanceFunc1({#self: Base1#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   base1_InstanceFunc2({#self: Base1#})[#(Derived) -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   base1_InstanceFunc3({#self: Base1#})[#(Derived) -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   base1_InstanceFunc4({#self: Base1#})[#() -> Base#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[baz_clang_module]:   baz_Class_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[baz_clang_module]: baz_Class_InstanceFunc1({#self: Baz_Class#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[baz_clang_module]:   baz_Protocol_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[baz_clang_module]: baz_Protocol_InstanceFunc1({#self: AnyObject.Type#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[foo_swift_module]:   foo_Nested1_ObjcClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: foo_Nested1_ObjcInstanceFunc1({#self: Foo_ContainerForNestedClass1.Foo_Nested1#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[foo_swift_module]:   foo_Nested2_ObjcClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: foo_Nested2_ObjcInstanceFunc1({#self: Foo_ContainerForNestedClass2.Foo_Nested2#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[foo_swift_module]:   foo_TopLevelClass_ObjcClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: foo_TopLevelClass_ObjcInstanceFunc1({#self: Foo_TopLevelClass#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[foo_swift_module]:   foo_TopLevelObjcClass_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: foo_TopLevelObjcClass_InstanceFunc1({#self: Foo_TopLevelObjcClass#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[foo_swift_module]:   foo_TopLevelObjcProtocol_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[foo_swift_module]: foo_TopLevelObjcProtocol_InstanceFunc1({#self: AnyObject.Type#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[swift_ide_test]:     nested1_ObjcClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   nested1_ObjcInstanceFunc1({#self: ContainerForNestedClass1.Nested1#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[swift_ide_test]:     nested2_ObjcClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   nested2_ObjcInstanceFunc1({#self: ContainerForNestedClass2.Nested2#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   returnsObjcClass({#self: TopLevelObjcClass#})[#(Int) -> TopLevelObjcClass#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[swift_ide_test]:     topLevelClass_ObjcClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   topLevelClass_ObjcInstanceFunc1({#self: TopLevelClass#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[swift_ide_test]:     topLevelObjcClass_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   topLevelObjcClass_InstanceFunc1({#self: TopLevelObjcClass#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[StaticMethod]/OtherModule[swift_ide_test]:     topLevelObjcProtocol_ClassFunc1()[#Void#]{{; name=.+$}}
// DL_CLASS_DOT-DAG: Decl[InstanceMethod]/OtherModule[swift_ide_test]:   topLevelObjcProtocol_InstanceFunc1({#self: AnyObject.Type#})[#() -> Void#]{{; name=.+$}}
// DL_CLASS_DOT: End completions

// TLOC_MEMBERS_NO_DOT: Begin completions
// TLOC_MEMBERS_NO_DOT-NEXT: Decl[InstanceMethod]/CurrNominal: .returnsObjcClass({#(i): Int#})[#TopLevelObjcClass#]{{; name=.+$}}
// TLOC_MEMBERS_NO_DOT-NEXT: Decl[InstanceMethod]/CurrNominal: .topLevelObjcClass_InstanceFunc1()[#Void#]{{; name=.+$}}
// TLOC_MEMBERS_NO_DOT-NEXT: Decl[Subscript]/CurrNominal:      [{#Int8#}][#Int#]{{; name=.+$}}
// TLOC_MEMBERS_NO_DOT-NEXT: Decl[InstanceVar]/CurrNominal:    .topLevelObjcClass_Property1[#Int#]{{; name=.+$}}
// TLOC_MEMBERS_NO_DOT-NEXT: Decl[InfixOperatorFunction]/OtherModule[Swift]: === {#AnyObject?#}[#Bool#];
// TLOC_MEMBERS_NO_DOT-NEXT: Decl[InfixOperatorFunction]/OtherModule[Swift]: !== {#AnyObject?#}[#Bool#];
// TLOC_MEMBERS_NO_DOT-NEXT: End completions

// TLOC_MEMBERS_DOT: Begin completions
// TLOC_MEMBERS_DOT-NEXT: Decl[InstanceMethod]/CurrNominal: returnsObjcClass({#(i): Int#})[#TopLevelObjcClass#]{{; name=.+$}}
// TLOC_MEMBERS_DOT-NEXT: Decl[InstanceMethod]/CurrNominal: topLevelObjcClass_InstanceFunc1()[#Void#]{{; name=.+$}}
// TLOC_MEMBERS_DOT-NEXT: Decl[InstanceVar]/CurrNominal:    topLevelObjcClass_Property1[#Int#]{{; name=.+$}}
// TLOC_MEMBERS_DOT-NEXT: End completions

// FIXME: Properties in Clang modules.
// There's a test already: baz_Protocol_Property1.
// Blocked by: rdar://15136550 Properties in protocols not implemented

@objc class TopLevelObjcClass {
  func returnsObjcClass(_ i: Int) -> TopLevelObjcClass {}

  func topLevelObjcClass_InstanceFunc1() {}
  class func topLevelObjcClass_ClassFunc1() {}
  subscript(i: Int8) -> Int {
    get {
      return 0
    }
  }
  var topLevelObjcClass_Property1: Int
}

@objc class TopLevelObjcClass_DuplicateMembers {
  func topLevelObjcClass_InstanceFunc1() {}
  class func topLevelObjcClass_ClassFunc1() {}
  subscript(i: Int8) -> Int {
    get {
      return 0
    }
  }
  var topLevelObjcClass_Property1: Int
}

class TopLevelClass {
  @objc func topLevelClass_ObjcInstanceFunc1() {}
  @objc class func topLevelClass_ObjcClassFunc1() {}
  @objc subscript (i: Int16) -> Int {
    get {
      return 0
    }
  }
  @objc var topLevelClass_ObjcProperty1: Int

  func ERROR() {}
  typealias ERROR = Int
  subscript (i: ERROR) -> Int {
    get {
      return 0
    }
  }
  var ERROR_Property: Int
}

@objc protocol TopLevelObjcProtocol {
  func topLevelObjcProtocol_InstanceFunc1()
  class func topLevelObjcProtocol_ClassFunc1()
  subscript (i: TopLevelObjcClass) -> Int { get set }
  var topLevelObjcProtocol_Property1: Int { get set }
}

class ContainerForNestedClass1 {
  class Nested1 {
    @objc func nested1_ObjcInstanceFunc1() {}
    @objc class func nested1_ObjcClassFunc1() {}
    @objc var nested1_Property1: Int

    func ERROR() {}
    typealias ERROR = Int
    subscript (i: ERROR) -> Int {
      get {
        return 0
      }
    }
    var ERROR_Property: Int
  }
  func ERROR() {}
}

struct ContainerForNestedClass2 {
  class Nested2 {
    @objc func nested2_ObjcInstanceFunc1() {}
    @objc class func nested2_ObjcClassFunc1() {}
    @objc subscript (i: TopLevelObjcProtocol) -> Int {
      get {
        return 0
      }
    }
    @objc var nested2_Property: Int

    func ERROR() {}
    var ERROR_Property: Int
  }
  func ERROR() {}
}

class GenericContainerForNestedClass1<T> {
  class Nested3 {
    @objc func ERROR1() {}
    func ERROR2() {}
    class func ERROR3() {}
    typealias ERROR = Int
    subscript (i: ERROR) -> Int {
      get {
        return 0
      }
    }
    var ERROR_Property: Int
  }
  func ERROR() {}
}

struct GenericContainerForNestedClass2<T> {
  class Nested3 {
    @objc func ERROR1() {}
    func ERROR2() {}
    class func ERROR3() {}
    typealias ERROR = Int
    subscript (i: ERROR) -> Int {
      get {
        return 0
      }
    }
    var ERROR_Property: Int
  }
  func ERROR() {}
}

@objc class Base1 {
  func base1_InstanceFunc1() {}

  func base1_InstanceFunc2(_ a: Derived) {}

  func base1_InstanceFunc3(_ a: Derived) {}

  func base1_InstanceFunc4() -> Base {}

  var base1_Property1: Int

  var base1_Property2: Base
}

@objc class Derived1 : Base1 {
  func base1_InstanceFunc1() {}

  func base1_InstanceFunc2(_ a: Derived) {}

  func base1_InstanceFunc3(_ a: Base) {}

  func base1_InstanceFunc4() -> Derived {}

  var base1_Property1: Int {
    get {
      return 0
    }
    set {}
  }

  var base1_Property2: Derived {
    get {
      return Derived()
    }
    set {}
  }
}

func returnsAnyObject() -> AnyObject {
  return TopLevelClass()
}

func testAnyObject1(_ dl: AnyObject) {
  dl#^DL_FUNC_PARAM_NO_DOT_1^#
}

func testAnyObject2(_ dl: AnyObject) {
  dl.#^DL_FUNC_PARAM_DOT_1^#
}

func testAnyObject3() {
  var dl: AnyObject = TopLevelClass()
  dl#^DL_VAR_NO_DOT_1^#
}

func testAnyObject4() {
  var dl: AnyObject = TopLevelClass()
  dl.#^DL_VAR_DOT_1^#
}

func testAnyObject5() {
  returnsAnyObject()#^DL_RETURN_VAL_NO_DOT_1^#
}

func testAnyObject6() {
  returnsAnyObject().#^DL_RETURN_VAL_DOT_1^#
}

func testAnyObject7(_ dl: AnyObject) {
  dl.returnsObjcClass!(42)#^DL_CALL_RETURN_VAL_NO_DOT_1^#
}

func testAnyObject8(_ dl: AnyObject) {
  dl.returnsObjcClass!(42).#^DL_CALL_RETURN_VAL_DOT_1^#
}

func testAnyObject9() {
  // FIXME: this syntax is not implemented yet.
  // dl.returnsObjcClass?(42)#^DL_CALL_RETURN_OPTIONAL_NO_DOT_1^#
}

func testAnyObject10() {
  // FIXME: this syntax is not implemented yet.
  // dl.returnsObjcClass?(42).#^DL_CALL_RETURN_OPTIONAL_DOT_1^#
}

func testAnyObject11(_ dl: AnyObject) {
  dl.returnsObjcClass#^DL_FUNC_NAME_1^#
}
// FIXME: it would be nice if we produced a call pattern here.
// DL_FUNC_NAME_1:     Begin completions
// DL_FUNC_NAME_1-DAG: Decl[InstanceVar]/CurrNominal:      .description[#String#]{{; name=.+$}}
// DL_FUNC_NAME_1:     End completions

func testAnyObject11_(_ dl: AnyObject) {
  dl.returnsObjcClass!(#^DL_FUNC_NAME_PAREN_1^#
}
// DL_FUNC_NAME_PAREN_1: Begin completions
// DL_FUNC_NAME_PAREN_1-DAG: Pattern/ExprSpecific: ['(']{#Int#})[#TopLevelObjcClass#]{{; name=.+$}}
// DL_FUNC_NAME_PAREN_1: End completions

func testAnyObject12(_ dl: AnyObject) {
  dl.returnsObjcClass.#^DL_FUNC_NAME_DOT_1^#
}
// FIXME: it would be nice if we produced a call pattern here.
// DL_FUNC_NAME_DOT_1:     Begin completions
// DL_FUNC_NAME_DOT_1-DAG: Decl[InstanceVar]/CurrNominal:      description[#String#]{{; name=.+$}}
// DL_FUNC_NAME_DOT_1:     End completions

func testAnyObject13(_ dl: AnyObject) {
  dl.returnsObjcClass!#^DL_FUNC_NAME_BANG_1^#
}
// DL_FUNC_NAME_BANG_1: Begin completions
// DL_FUNC_NAME_BANG_1-NEXT: Pattern/ExprSpecific: ({#Int#})[#TopLevelObjcClass#]
// DL_FUNC_NAME_BANG_1-NEXT: End completions

func testAnyObject14() {
  // FIXME: this syntax is not implemented yet.
  // dl.returnsObjcClass?#^DL_FUNC_QUESTION_1^#
}

func testAnyObjectClassMethods1(_ dl: AnyObject) {
  type(of: dl)#^DL_CLASS_NO_DOT_1^#
}

func testAnyObjectClassMethods2(_ dl: AnyObject) {
  type(of: dl).#^DL_CLASS_DOT_1^#
}
