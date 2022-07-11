// RUN: %empty-directory(%t)
// RUN: %target-build-swift %s -module-name Names -emit-module -emit-module-path %t/
// RUN: %target-swift-symbolgraph-extract -module-name Names -I %t -pretty-print -output-dir %t
// RUN: %FileCheck %s --input-file %t/Names.symbols.json
// RUN: %FileCheck %s --input-file %t/Names.symbols.json --check-prefix=FUNC
// RUN: %FileCheck %s --input-file %t/Names.symbols.json --check-prefix=INNERTYPE
// RUN: %FileCheck %s --input-file %t/Names.symbols.json --check-prefix=INNERTYPEALIAS
// RUN: %FileCheck %s --input-file %t/Names.symbols.json --check-prefix=INNERENUM
// RUN: %FileCheck %s --input-file %t/Names.symbols.json --check-prefix=INNERCASE

public struct MyStruct {
  public struct InnerStruct {}

  public typealias InnerTypeAlias = InnerStruct

  public func foo() {}

  public enum InnerEnum {
      case InnerCase
  }
}

// CHECK-LABEL: "precise": "s:5Names8MyStructV"
// CHECK: names
// CHECK-NEXT: "title": "MyStruct"

// FUNC-LABEL: "precise": "s:5Names8MyStructV3fooyyF",
// FUNC: names
// FUNC-NEXT: "title": "foo()"

// INNERTYPE-LABEL: "precise": "s:5Names8MyStructV05InnerC0V"
// INNERTYPE: names
// INNERTYPE-NEXT: "title": "MyStruct.InnerStruct"

// INNERTYPEALIAS-LABEL: "precise": "s:5Names8MyStructV14InnerTypeAliasa"
// INNERTYPEALIAS: names
// INNERTYPEALIAS-NEXT: "title": "MyStruct.InnerTypeAlias"

// INNERENUM-LABEL: "precise": "s:5Names8MyStructV9InnerEnumO",
// INNERENUM: names
// INNERENUM-NEXT: "title": "MyStruct.InnerEnum"

// INNERCASE-LABEL: "precise": "s:5Names8MyStructV9InnerEnumO0D4CaseyA2EmF",
// INNERCASE: names
// INNERCASE-NEXT: "title": "MyStruct.InnerEnum.InnerCase",
