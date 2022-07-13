// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=CONSTRUCTOR_SELF_NO_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=CONSTRUCTOR_SELF_NO_DOT_1 < %t.self.txt
// RUN: %FileCheck %s -check-prefix=COMMON_SELF_NO_DOT_1 < %t.self.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=CONSTRUCTOR_NONSELF_NO_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=COMMON_SELF_NO_DOT_1 < %t.self.txt
// RUN: %FileCheck %s -check-prefix=NO_INIT < %t.self.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=CONSTRUCTOR_SELF_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=CONSTRUCTOR_SELF_DOT_1 < %t.self.txt
// RUN: %FileCheck %s -check-prefix=COMMON_SELF_DOT_1 < %t.self.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=CONSTRUCTOR_NONSELF_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=COMMON_SELF_DOT_1 < %t.self.txt
// RUN: %FileCheck %s -check-prefix=NO_INIT < %t.self.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=DESTRUCTOR_SELF_NO_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=DESTRUCTOR_SELF_NO_DOT_1 < %t.self.txt
// RUN: %FileCheck %s -check-prefix=COMMON_SELF_NO_DOT_1 < %t.self.txt
// RUN: %FileCheck %s -check-prefix=NO_INIT < %t.self.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=DESTRUCTOR_SELF_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=DESTRUCTOR_SELF_DOT_1 < %t.self.txt
// RUN: %FileCheck %s -check-prefix=COMMON_SELF_DOT_1 < %t.self.txt
// RUN: %FileCheck %s -check-prefix=NO_INIT < %t.self.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FUNC_SELF_NO_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=FUNC_SELF_NO_DOT_1 < %t.self.txt
// RUN: %FileCheck %s -check-prefix=COMMON_SELF_NO_DOT_1 < %t.self.txt
// RUN: %FileCheck %s -check-prefix=NO_INIT < %t.self.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FUNC_SELF_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=FUNC_SELF_DOT_1 < %t.self.txt
// RUN: %FileCheck %s -check-prefix=COMMON_SELF_DOT_1 < %t.self.txt
// RUN: %FileCheck %s -check-prefix=NO_INIT < %t.self.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FUNC_STATIC_SELF_NO_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=FUNC_STATIC_SELF_NO_DOT_1 < %t.self.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=FUNC_STATIC_SELF_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=FUNC_STATIC_SELF_DOT_1 < %t.self.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=STRUCT_CONSTRUCTOR_SELF_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=STRUCT_CONSTRUCTOR_SELF_DOT_1 < %t.self.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=STRUCT_CONSTRUCTOR_NONSELF_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=NO_INIT < %t.self.txt

// RUN: %target-swift-ide-test -code-completion -source-filename %s -code-completion-token=STRUCT_FUNC_SELF_DOT_1 > %t.self.txt
// RUN: %FileCheck %s -check-prefix=NO_INIT < %t.self.txt

//===---
//===--- Tests for code completion after 'self'.
//===---

class ThisBase1 {
  var baseInstanceVar: Int

  func baseFunc0() {}
  func baseFunc1(_ a: Int) {}

  subscript(i: Int) -> Double {
    get {
      return Double(i)
    }
    set(v) {
      baseInstanceVar = i
    }
  }

  class var baseStaticVar: Int = 12

  class func baseStaticFunc0() {}
}

extension ThisBase1 {
  var baseExtProp : Int {
    get {
      return 42
    }
    set(v) {}
  }

  func baseExtInstanceFunc0() {}

  var baseExtStaticVar: Int

  var baseExtStaticProp: Int {
    get {
      return 42
    }
    sel(v) {}
  }

  class func baseExtStaticFunc0() {}

  struct BaseExtNestedStruct {}
  class BaseExtNestedClass {}
  enum BaseExtNestedEnum {
    case BaseExtEnumX(Int)
  }

  typealias BaseExtNestedTypealias = Int
}

class ThisDerived1 : ThisBase1 {
  var derivedInstanceVar: Int

  func derivedFunc0() {}

  subscript(i: Double) -> Int {
    get {
      return Int(i)
    }
    set(v) {
      baseInstanceVar = Int(i)
    }
  }

  subscript(s s: String) -> Int {
    get {
      return 0
    }
    set(v) {
    }
  }

  class var derivedStaticVar: Int = 42

  class func derivedStaticFunc0() {}

// COMMON_SELF_NO_DOT_1: Begin completions
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceVar]/CurrNominal:    .derivedInstanceVar[#Int#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceMethod]/CurrNominal: .derivedFunc0()[#Void#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[Subscript]/CurrNominal:       [{#Double#}][#Int#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[Subscript]/CurrNominal:       [{#s: String#}][#Int#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceMethod]/CurrNominal: .test1()[#Void#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceMethod]/CurrNominal: .test2()[#Void#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceVar]/CurrNominal:    .derivedExtProp[#Int#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceMethod]/CurrNominal: .derivedExtInstanceFunc0()[#Void#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceVar]/CurrNominal:    .derivedExtStaticProp[#Int#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceVar]/Super:          .baseInstanceVar[#Int#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceMethod]/Super:       .baseFunc0()[#Void#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceMethod]/Super:       .baseFunc1({#(a): Int#})[#Void#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[Subscript]/Super:            [{#Int#}][#Double#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceVar]/Super:          .baseExtProp[#Int#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceMethod]/Super:       .baseExtInstanceFunc0()[#Void#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1-DAG: Decl[InstanceVar]/Super:          .baseExtStaticProp[#Int#]{{; name=.+$}}
// COMMON_SELF_NO_DOT_1: End completions

// COMMON_SELF_DOT_1: Begin completions
// COMMON_SELF_DOT_1-DAG: Decl[InstanceVar]/CurrNominal:    derivedInstanceVar[#Int#]{{; name=.+$}}
// COMMON_SELF_DOT_1-DAG: Decl[InstanceMethod]/CurrNominal: derivedFunc0()[#Void#]{{; name=.+$}}
// COMMON_SELF_DOT_1-DAG: Decl[InstanceMethod]/CurrNominal: test1()[#Void#]{{; name=.+$}}
// COMMON_SELF_DOT_1-DAG: Decl[InstanceMethod]/CurrNominal: test2()[#Void#]{{; name=.+$}}
// COMMON_SELF_DOT_1-DAG: Decl[InstanceVar]/CurrNominal:    derivedExtProp[#Int#]{{; name=.+$}}
// COMMON_SELF_DOT_1-DAG: Decl[InstanceMethod]/CurrNominal: derivedExtInstanceFunc0()[#Void#]{{; name=.+$}}
// COMMON_SELF_DOT_1-DAG: Decl[InstanceVar]/CurrNominal:    derivedExtStaticProp[#Int#]{{; name=.+$}}
// COMMON_SELF_DOT_1-DAG: Decl[InstanceVar]/Super:          baseInstanceVar[#Int#]{{; name=.+$}}
// COMMON_SELF_DOT_1-DAG: Decl[InstanceMethod]/Super:       baseFunc0()[#Void#]{{; name=.+$}}
// COMMON_SELF_DOT_1-DAG: Decl[InstanceMethod]/Super:       baseFunc1({#(a): Int#})[#Void#]{{; name=.+$}}
// COMMON_SELF_DOT_1-DAG: Decl[InstanceVar]/Super:          baseExtProp[#Int#]{{; name=.+$}}
// COMMON_SELF_DOT_1-DAG: Decl[InstanceMethod]/Super:       baseExtInstanceFunc0()[#Void#]{{; name=.+$}}
// COMMON_SELF_DOT_1-DAG: Decl[InstanceVar]/Super:          baseExtStaticProp[#Int#]{{; name=.+$}}
// COMMON_SELF_DOT_1: End completions

  init() {
    self#^CONSTRUCTOR_SELF_NO_DOT_1^#
// CONSTRUCTOR_SELF_NO_DOT_1: Begin completions, 23 items
// CONSTRUCTOR_SELF_NO_DOT_1-DAG: Decl[Constructor]/CurrNominal: .init()[#ThisDerived1#];
// CONSTRUCTOR_SELF_NO_DOT_1-DAG: Decl[Constructor]/CurrNominal: .init({#a: Int#})[#ThisDerived1#];
// CONSTRUCTOR_SELF_NO_DOT_1: End completions
    let d: ThisDerived1
    d#^CONSTRUCTOR_NONSELF_NO_DOT_1^#
// NO_INIT-NOT: init()
  }

  init(a : Int) {
    self.#^CONSTRUCTOR_SELF_DOT_1^#
// CONSTRUCTOR_SELF_DOT_1: Begin completions, 18 items
// CONSTRUCTOR_SELF_DOT_1-DAG: Decl[Constructor]/CurrNominal: init()[#ThisDerived1#];
// CONSTRUCTOR_SELF_DOT_1-DAG: Decl[Constructor]/CurrNominal: init({#a: Int#})[#ThisDerived1#];
// CONSTRUCTOR_SELF_DOT_1: End completions
    let d: ThisDerived1
    d.#^CONSTRUCTOR_NONSELF_DOT_1^#
  }

  deinit {
    self#^DESTRUCTOR_SELF_NO_DOT_1^#
// DESTRUCTOR_SELF_NO_DOT_1: Begin completions, 20 items
// DESTRUCTOR_SELF_NO_DOT_1: End completions

    self.#^DESTRUCTOR_SELF_DOT_1^#
// DESTRUCTOR_SELF_DOT_1: Begin completions, 15 items
// DESTRUCTOR_SELF_DOT_1: End completions
  }

  func test1() {
    self#^FUNC_SELF_NO_DOT_1^#
// FUNC_SELF_NO_DOT_1: Begin completions, 20 items
// FUNC_SELF_NO_DOT_1: End completions
  }

  func test2() {
    self.#^FUNC_SELF_DOT_1^#
// FUNC_SELF_DOT_1: Begin completions, 15 items
// FUNC_SELF_DOT_1: End completions
  }

  class func staticTest1() {
    self#^FUNC_STATIC_SELF_NO_DOT_1^#
// FUNC_STATIC_SELF_NO_DOT_1: Begin completions
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[InstanceMethod]/CurrNominal:   .derivedFunc0({#self: ThisDerived1#})[#() -> Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[StaticVar]/CurrNominal:        .derivedStaticVar[#Int#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[StaticMethod]/CurrNominal:     .derivedStaticFunc0()[#Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[Constructor]/CurrNominal:      ()[#ThisDerived1#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[Constructor]/CurrNominal:      ({#a: Int#})[#ThisDerived1#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[InstanceMethod]/CurrNominal:   .test1({#self: ThisDerived1#})[#() -> Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[InstanceMethod]/CurrNominal:   .test2({#self: ThisDerived1#})[#() -> Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[StaticMethod]/CurrNominal:     .staticTest1()[#Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[StaticMethod]/CurrNominal:     .staticTest2()[#Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[InstanceMethod]/CurrNominal:   .derivedExtInstanceFunc0({#self: ThisDerived1#})[#() -> Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[StaticMethod]/CurrNominal:     .derivedExtStaticFunc0()[#Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[Struct]/CurrNominal:           .DerivedExtNestedStruct[#ThisDerived1.DerivedExtNestedStruct#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[Class]/CurrNominal:            .DerivedExtNestedClass[#ThisDerived1.DerivedExtNestedClass#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[Enum]/CurrNominal:             .DerivedExtNestedEnum[#ThisDerived1.DerivedExtNestedEnum#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[TypeAlias]/CurrNominal:        .DerivedExtNestedTypealias[#Int#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[Constructor]/CurrNominal:      ({#someExtensionArg: Int#})[#ThisDerived1#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[InstanceMethod]/Super:         .baseFunc0({#self: ThisBase1#})[#() -> Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[InstanceMethod]/Super:         .baseFunc1({#self: ThisBase1#})[#(Int) -> Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[StaticVar]/Super:              .baseStaticVar[#Int#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[StaticMethod]/Super:           .baseStaticFunc0()[#Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[InstanceMethod]/Super:         .baseExtInstanceFunc0({#self: ThisBase1#})[#() -> Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[StaticMethod]/Super:           .baseExtStaticFunc0()[#Void#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[Struct]/Super:                 .BaseExtNestedStruct[#ThisBase1.BaseExtNestedStruct#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[Class]/Super:                  .BaseExtNestedClass[#ThisBase1.BaseExtNestedClass#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[Enum]/Super:                   .BaseExtNestedEnum[#ThisBase1.BaseExtNestedEnum#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: Decl[TypeAlias]/Super:              .BaseExtNestedTypealias[#Int#]
// FUNC_STATIC_SELF_NO_DOT_1-NEXT: End completions
  }

  class func staticTest2() {
    self.#^FUNC_STATIC_SELF_DOT_1^#
// FUNC_STATIC_SELF_DOT_1: Begin completions
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[InstanceMethod]/CurrNominal:   derivedFunc0({#self: ThisDerived1#})[#() -> Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[StaticVar]/CurrNominal:        derivedStaticVar[#Int#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[StaticMethod]/CurrNominal:     derivedStaticFunc0()[#Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[InstanceMethod]/CurrNominal:   test1({#self: ThisDerived1#})[#() -> Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[InstanceMethod]/CurrNominal:   test2({#self: ThisDerived1#})[#() -> Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[StaticMethod]/CurrNominal:     staticTest1()[#Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[StaticMethod]/CurrNominal:     staticTest2()[#Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[InstanceMethod]/CurrNominal:   derivedExtInstanceFunc0({#self: ThisDerived1#})[#() -> Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[StaticMethod]/CurrNominal:     derivedExtStaticFunc0()[#Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[Struct]/CurrNominal:           DerivedExtNestedStruct[#ThisDerived1.DerivedExtNestedStruct#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[Class]/CurrNominal:            DerivedExtNestedClass[#ThisDerived1.DerivedExtNestedClass#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[Enum]/CurrNominal:             DerivedExtNestedEnum[#ThisDerived1.DerivedExtNestedEnum#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[TypeAlias]/CurrNominal:        DerivedExtNestedTypealias[#Int#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[InstanceMethod]/Super:         baseFunc0({#self: ThisBase1#})[#() -> Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[InstanceMethod]/Super:         baseFunc1({#self: ThisBase1#})[#(Int) -> Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[StaticVar]/Super:              baseStaticVar[#Int#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[StaticMethod]/Super:           baseStaticFunc0()[#Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[InstanceMethod]/Super:         baseExtInstanceFunc0({#self: ThisBase1#})[#() -> Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[StaticMethod]/Super:           baseExtStaticFunc0()[#Void#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[Struct]/Super:                 BaseExtNestedStruct[#ThisBase1.BaseExtNestedStruct#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[Class]/Super:                  BaseExtNestedClass[#ThisBase1.BaseExtNestedClass#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[Enum]/Super:                   BaseExtNestedEnum[#ThisBase1.BaseExtNestedEnum#]
// FUNC_STATIC_SELF_DOT_1-NEXT: Decl[TypeAlias]/Super:              BaseExtNestedTypealias[#Int#]
// FUNC_STATIC_SELF_DOT_1-NEXT: End completions
  }
}

extension ThisDerived1 {
  var derivedExtProp : Int {
    get {
      return 42
    }
    set(v) {}
  }

  func derivedExtInstanceFunc0() {}

  var derivedExtStaticVar: Int

  var derivedExtStaticProp: Int {
    get {
      return 42
    }
    set(v) {}
  }

  class func derivedExtStaticFunc0() {}

  struct DerivedExtNestedStruct {}
  class DerivedExtNestedClass {}
  enum DerivedExtNestedEnum {
    case DerivedExtEnumX(Int)
  }

  typealias DerivedExtNestedTypealias = Int

  convenience init(someExtensionArg: Int) {
    self.#^EXTENSION_CONSTRUCTOR_SELF_DOT_1^#
  }
}

struct S1 {
  init() {}
  init(x: Int) {
    self.#^STRUCT_CONSTRUCTOR_SELF_DOT_1^#
// STRUCT_CONSTRUCTOR_SELF_DOT_1: Begin completions, 3 items
// STRUCT_CONSTRUCTOR_SELF_DOT_1-DAG: Decl[Constructor]/CurrNominal:      init()[#S1#];
// STRUCT_CONSTRUCTOR_SELF_DOT_1-DAG: Decl[Constructor]/CurrNominal:      init({#x: Int#})[#S1#];
// STRUCT_CONSTRUCTOR_SELF_DOT_1-DAG: Decl[InstanceMethod]/CurrNominal:   f()[#Void#];
// STRUCT_CONSTRUCTOR_SELF_DOT_1: End completions
    let s: S1
    s.#^STRUCT_CONSTRUCTOR_NONSELF_DOT_1^#
  }
  func f() {
    self.#^STRUCT_FUNC_SELF_DOT_1^#
  }
}
