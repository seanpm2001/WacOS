// RUN: %empty-directory(%t)
// RUN: %target-swift-ide-test -batch-code-completion -source-filename %s -filecheck %raw-FileCheck -completion-output-dir %t

// MARK: Single-expression closures

struct TestSingleExprClosureRet {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  func test() -> Int {
    return { () in
      return self.#^TestSingleExprClosureRet^#
    }()
  }

// TestSingleExprClosureRet: Begin completions
// TestSingleExprClosureRet-DAG: Decl[InstanceMethod]/CurrNominal:   str()[#String#];
// TestSingleExprClosureRet-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: int()[#Int#];
// TestSingleExprClosureRet-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Invalid]: void()[#Void#];
// TestSingleExprClosureRet: End completions
}

struct TestSingleExprClosureRetVoid {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  func test() {
    return { () in
      return self.#^TestSingleExprClosureRetVoid^#
    }()
  }

// TestSingleExprClosureRetVoid: Begin completions
// TestSingleExprClosureRetVoid-DAG: Decl[InstanceMethod]/CurrNominal: str()[#String#];
// TestSingleExprClosureRetVoid-DAG: Decl[InstanceMethod]/CurrNominal: int()[#Int#];
// TestSingleExprClosureRetVoid-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: void()[#Void#];
// TestSingleExprClosureRetVoid: End completions
}

struct TestSingleExprClosureRetUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  func test() -> MyEnum {
    return { () in
      return .#^TestSingleExprClosureRetUnresolved^#
    }()
  }

// TestSingleExprClosureRetUnresolved: Begin completions
// TestSingleExprClosureRetUnresolved-NOT: notMine
// TestSingleExprClosureRetUnresolved: Decl[EnumElement]/CurrNominal/Flair[ExprSpecific]/TypeRelation[Identical]: myEnum[#MyEnum#];
// TestSingleExprClosureRetUnresolved-NOT: notMine
// TestSingleExprClosureRetUnresolved: End completions
}

struct TestSingleExprClosure {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  func test() -> Int {
    return { () in
      self.#^TestSingleExprClosure^#
    }()
  }
// TestSingleExprClosure: Begin completions
// TestSingleExprClosure-DAG: Decl[InstanceMethod]/CurrNominal:   str()[#String#];
// TestSingleExprClosure-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: int()[#Int#];
// NOTE: this differs from the one using a return keyword.
// TestSingleExprClosure-DAG: Decl[InstanceMethod]/CurrNominal: void()[#Void#];
// TestSingleExprClosure: End completions
}

struct TestSingleExprClosureVoid {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  func test() {
    return { () in
      self.#^TestSingleExprClosureVoid^#
    }()
  }
// TestSingleExprClosureVoid: Begin completions
// TestSingleExprClosureVoid-DAG: Decl[InstanceMethod]/CurrNominal: str()[#String#];
// TestSingleExprClosureVoid-DAG: Decl[InstanceMethod]/CurrNominal: int()[#Int#];
// Note: this differs from explicit return by having no type context.
// TestSingleExprClosureVoid-DAG: Decl[InstanceMethod]/CurrNominal: void()[#Void#];
// TestSingleExprClosureVoid: End completions
}

struct TestSingleExprClosureUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  func test() -> MyEnum {
    return { () in
      .#^TestSingleExprClosureUnresolved^#
    }()
  }
}
// TestSingleExprClosureUnresolved: Begin completions
// TestSingleExprClosureUnresolved-NOT: notMine
// TestSingleExprClosureUnresolved: Decl[EnumElement]/CurrNominal/Flair[ExprSpecific]/TypeRelation[Identical]: myEnum[#MyEnum#];
// TestSingleExprClosureUnresolved-NOT: notMine
// TestSingleExprClosureUnresolved: End completions

struct TestSingleExprClosureCall {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  func take(_: () -> Int) {}

  func test() {
    take {
      self.#^TestSingleExprClosureCall?check=TestSingleExprClosure^#
    }
  }
}

struct TestSingleExprClosureGlobal {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  func test() -> Int {
    return { () in
      #^TestSingleExprClosureGlobal^#
    }()
  }
// TestSingleExprClosureGlobal: Begin completions
// TestSingleExprClosureGlobal-DAG: Decl[InstanceMethod]/OutNominal:   str()[#String#];
// TestSingleExprClosureGlobal-DAG: Decl[InstanceMethod]/OutNominal/TypeRelation[Identical]: int()[#Int#];
// TestSingleExprClosureGlobal-DAG: Decl[InstanceMethod]/OutNominal: void()[#Void#];
// TestSingleExprClosureGlobal: End completions
}

struct TestNonSingleExprClosureGlobal {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  func test() -> Int {
    return { () in
      #^TestNonSingleExprClosureGlobal^#
      return 42
    }()
  }
// TestNonSingleExprClosureGlobal: Begin completions
// TestNonSingleExprClosureGlobal-DAG: Decl[InstanceMethod]/OutNominal: str()[#String#];
// TestNonSingleExprClosureGlobal-DAG: Decl[InstanceMethod]/OutNominal: int()[#Int#];
// TestNonSingleExprClosureGlobal-DAG: Decl[InstanceMethod]/OutNominal: void()[#Void#];
// TestNonSingleExprClosureGlobal: End completions
}

struct TestNonSingleExprClosureUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  func test() -> Int {
    return { () in
      .#^TestNonSingleExprClosureUnresolved^#
      return 42
    }()
  }
// TestNonSingleExprClosureUnresolved-NOT: myEnum
// TestNonSingleExprClosureUnresolved-NOT: notMine
}

// MARK: Single-expression functions

struct TestSingleExprFuncRet {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  func test() -> Int {
    return self.#^TestSingleExprFuncRet^#
  }

// TestSingleExprFuncRet: Begin completions
// TestSingleExprFuncRet-DAG: Decl[InstanceMethod]/CurrNominal:   str()[#String#];
// TestSingleExprFuncRet-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: int()[#Int#];
// TestSingleExprFuncRet-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Invalid]: void()[#Void#];
// TestSingleExprFuncRet: End completions
}

struct TestSingleExprFunc {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  func test() -> Int {
    self.#^TestSingleExprFunc^#
  }

// TestSingleExprFunc: Begin completions
// TestSingleExprFunc-DAG: Decl[InstanceMethod]/CurrNominal:   str()[#String#];
// TestSingleExprFunc-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: int()[#Int#];
// TestSingleExprFunc-DAG: Decl[InstanceMethod]/CurrNominal: void()[#Void#];
// TestSingleExprFunc: End completions
}

struct TestSingleExprFuncUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  func test() -> MyEnum {
    .#^TestSingleExprFuncUnresolved^#
  }

// TestSingleExprFuncUnresolved: Begin completions
// TestSingleExprFuncUnresolved-NOT: notMine
// TestSingleExprFuncUnresolved: Decl[EnumElement]/CurrNominal/Flair[ExprSpecific]/TypeRelation[Identical]: myEnum[#MyEnum#];
// TestSingleExprFuncUnresolved-NOT: notMine
// TestSingleExprFuncUnresolved: End completions
}

struct TestNonSingleExprFuncUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  func test() -> MyEnum {
    .#^TestNonSingleExprFuncUnresolved^#
    return .myEnum
  }

// TestNonSingleExprFuncUnresolved-NOT: myEnum
// TestNonSingleExprFuncUnresolved-NOT: notMine
}

struct TestSingleExprLocalFuncUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  func test() {
    func local() -> MyEnum {
      .#^TestSingleExprLocalFuncUnresolved?check=TestSingleExprFuncUnresolved^#
    }
  }
}

struct TestSingleExprFuncGlobal {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  func test() -> Int {
    #^TestSingleExprFuncGlobal^#
  }
// TestSingleExprFuncGlobal: Begin completions
// TestSingleExprFuncGlobal-DAG: Decl[InstanceMethod]/CurrNominal:   str()[#String#];
// TestSingleExprFuncGlobal-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: int()[#Int#];
// TestSingleExprFuncGlobal-DAG: Decl[InstanceMethod]/CurrNominal: void()[#Void#];
// TestSingleExprFuncGlobal: End completions
}

struct TestNonSingleExprFuncGlobal {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  func test() -> Int {
    #^TestNonSingleExprFuncGlobal^#
    return 42
  }
// TestNonSingleExprFuncGlobal: Begin completions
// TestNonSingleExprFuncGlobal-DAG: Decl[InstanceMethod]/CurrNominal: str()[#String#];
// TestNonSingleExprFuncGlobal-DAG: Decl[InstanceMethod]/CurrNominal: int()[#Int#];
// TestNonSingleExprFuncGlobal-DAG: Decl[InstanceMethod]/CurrNominal: void()[#Void#];
// TestNonSingleExprFuncGlobal: End completions
}

// MARK: Single-expression accessors

struct TestSingleExprAccessorRet {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  var test: Int {
    return self.#^TestSingleExprAccessorRet^#
  }

// TestSingleExprAccessorRet: Begin completions
// TestSingleExprAccessorRet-DAG: Decl[InstanceMethod]/CurrNominal:   str()[#String#];
// TestSingleExprAccessorRet-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: int()[#Int#];
// TestSingleExprAccessorRet-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Invalid]: void()[#Void#];
// TestSingleExprAccessorRet: End completions
}

struct TestSingleExprAccessor {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  var test: Int {
    self.#^TestSingleExprAccessor^#
  }

// TestSingleExprAccessor: Begin completions
// TestSingleExprAccessor-DAG: Decl[InstanceMethod]/CurrNominal:   str()[#String#];
// TestSingleExprAccessor-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: int()[#Int#];
// TestSingleExprAccessor-DAG: Decl[InstanceMethod]/CurrNominal: void()[#Void#];
// TestSingleExprAccessor: End completions
}

struct TestSingleExprAccessorUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  var test: MyEnum {
    .#^TestSingleExprAccessorUnresolved^#
  }

// TestSingleExprAccessorUnresolved: Begin completions
// TestSingleExprAccessorUnresolved-NOT: notMine
// TestSingleExprAccessorUnresolved: Decl[EnumElement]/CurrNominal/Flair[ExprSpecific]/TypeRelation[Identical]: myEnum[#MyEnum#];
// TestSingleExprAccessorUnresolved-NOT: notMine
// TestSingleExprAccessorUnresolved: End completions
}

struct TestNonSingleExprAccessorUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  var test: MyEnum {
    .#^TestNonSingleExprAccessorUnresolved^#
    return .myEnum
  }

// TestNonSingleExprAccessorUnresolved-NOT: myEnum
// TestNonSingleExprAccessorUnresolved-NOT: notMine
}

struct TestSingleExprLocalAccessorUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  func test() {
    var test: MyEnum {
      .#^TestSingleExprLocalAccessorUnresolved?check=TestSingleExprAccessorUnresolved^#
    }
  }
}

struct TestSingleExprAccessorGlobal {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  var test: Int {
    #^TestSingleExprAccessorGlobal^#
  }
// TestSingleExprAccessorGlobal: Begin completions
// TestSingleExprAccessorGlobal-DAG: Decl[InstanceMethod]/CurrNominal:   str()[#String#];
// TestSingleExprAccessorGlobal-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: int()[#Int#];
// TestSingleExprAccessorGlobal-DAG: Decl[InstanceMethod]/CurrNominal: void()[#Void#];
// TestSingleExprAccessorGlobal: End completions
}

struct TestNonSingleExprAccessorGlobal {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  var test: Int {
    #^TestNonSingleExprAccessorGlobal^#
    return 42
  }

// TestNonSingleExprAccessorGlobal: Begin completions
// TestNonSingleExprAccessorGlobal-DAG: Decl[InstanceMethod]/CurrNominal: str()[#String#];
// FIXME: should should not have type context.
// TestNonSingleExprAccessorGlobal-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: int()[#Int#];
// TestNonSingleExprAccessorGlobal-DAG: Decl[InstanceMethod]/CurrNominal: void()[#Void#];
// TestNonSingleExprAccessorGlobal: End completions
}

struct TestSingleExprAccessorGetUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  var test: MyEnum {
    get {
      .#^TestSingleExprAccessorGetUnresolved?check=TestSingleExprAccessorUnresolved^#
    }
  }
}

struct TestSingleExprAccessorGetGlobal {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  var test: Int {
    get {
      #^TestSingleExprAccessorGetGlobal?check=TestSingleExprAccessorGlobal^#
    }
  }
}

struct TestSingleExprAccessorSetUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  var test: MyEnum {
    set {
      .#^TestSingleExprAccessorSetUnresolved^#
    }
  }
// TestSingleExprAccessorSetUnresolved-NOT: myEnum
// TestSingleExprAccessorSetUnresolved-NOT: notMine
}

// MARK: Single-expression subscripts

struct TestSingleExprSubscriptRet {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  subscript(_: Int) -> Int {
    return self.#^TestSingleExprSubscriptRet^#
  }

// TestSingleExprSubscriptRet: Begin completions
// TestSingleExprSubscriptRet-DAG: Decl[InstanceMethod]/CurrNominal:   str()[#String#];
// TestSingleExprSubscriptRet-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: int()[#Int#];
// TestSingleExprSubscriptRet-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Invalid]: void()[#Void#];
// TestSingleExprSubscriptRet: End completions
}

struct TestSingleExprSubscript {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  subscript(_: Int) -> Int {
    self.#^TestSingleExprSubscript^#
  }

// TestSingleExprSubscript: Begin completions
// TestSingleExprSubscript-DAG: Decl[InstanceMethod]/CurrNominal:   str()[#String#];
// TestSingleExprSubscript-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: int()[#Int#];
// TestSingleExprSubscript-DAG: Decl[InstanceMethod]/CurrNominal: void()[#Void#];
// TestSingleExprSubscript: End completions
}

struct TestSingleExprSubscriptUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  subscript(_: Int) -> MyEnum {
    .#^TestSingleExprSubscriptUnresolved^#
  }

// TestSingleExprSubscriptUnresolved: Begin completions
// TestSingleExprSubscriptUnresolved-NOT: notMine
// TestSingleExprSubscriptUnresolved: Decl[EnumElement]/CurrNominal/Flair[ExprSpecific]/TypeRelation[Identical]: myEnum[#MyEnum#];
// TestSingleExprSubscriptUnresolved-NOT: notMine
// TestSingleExprSubscriptUnresolved: End completions
}

struct TestNonSingleExprSubscriptUnresolved {
  enum MyEnum { case myEnum }
  enum NotMine { case notMine }
  func mine() -> MyEnum { return .myEnum }
  func notMine() -> NotMine { return .notMine }

  subscript(_: Int) -> MyEnum {
    .#^TestNonSingleExprSubscriptUnresolved^#
    return .myEnum
  }

// TestNonSingleExprSubscriptUnresolved-NOT: myEnum
// TestNonSingleExprSubscriptUnresolved-NOT: notMine
}

struct TestSingleExprSubscriptGlobal {
  func void() -> Void {}
  func str() -> String { return "" }
  func int() -> Int { return 0 }

  subscript(input: Int) -> Int {
    #^TestSingleExprSubscriptGlobal^#
  }

// TestSingleExprSubscriptGlobal: Begin completions
// TestSingleExprSubscriptGlobal-DAG: Decl[InstanceMethod]/CurrNominal: str()[#String#];
// TestSingleExprSubscriptGlobal-DAG: Decl[InstanceMethod]/CurrNominal/TypeRelation[Identical]: int()[#Int#];
// TestSingleExprSubscriptGlobal-DAG: Decl[InstanceMethod]/CurrNominal: void()[#Void#];
// TestSingleExprSubscriptGlobal: End completions
}

// MARK: Single-expression initializers

enum TestSingeExprInitInvalid {
  case foo
  init() {
    .#^TestSingeExprInitInvalid^#
  }
// TestSingeExprInitInvalid-NOT: foo
}

enum TestSingeExprInitNone {
  case foo
  init?() {
    .#^TestSingeExprInitNone^#
  }
// TestSingeExprInitNone-NOT: foo
// Note: only `nil` is allowed here, not `.none`.
// TestSingeExprInitNone-NOT: none
}

enum TestSingeExprInitNilRet {
  case foo
  init?() {
    return #^TestSingeExprInitNilRet^#
  }
// TestSingeExprInitNilRet: Literal[Nil]/None/TypeRelation[Identical]: nil[#TestSingeExprInitNil{{(Ret)?}}?#];
}

enum TestSingeExprInitNil {
  case foo
  init?() {
    #^TestSingeExprInitNil^#
  }
// FIXME: For consistency, this should be same as TestSingeExprInitNilRet.
// TestSingeExprInitNil: Literal[Nil]/None: nil;
}

enum TestNonSingeExprInitNil1 {
  case foo
  init?() {
    #^TestNonSingeExprInitNil1?check=TestNonSingeExprInitNil^#
    return nil
  }
// No type relation.
// TestNonSingeExprInitNil: Literal[Nil]/None: nil;
}

enum TestNonSingeExprInitNil2 {
  case foo
  init?() {
    #^TestNonSingeExprInitNil2?check=TestNonSingeExprInitNil^#
    self = .foo
  }
}

enum TestSingeExprDeinitInvalid {
  case foo
  deinit {
    .#^TestSingeExprDeinitInvalid^#
  }
// TestSingeExprDeinitInvalid-NOT: foo
}

// MARK: Top-level code

enum TopLevelEnum {
  case foo
}

// TopLevelEnum: Decl[EnumElement]/CurrNominal/Flair[ExprSpecific]/TypeRelation[Identical]: foo[#TopLevelEnum#];

var testAccessorUnresolvedTopLevel: TopLevelEnum {
  .#^testAccessorUnresolvedTopLevel?check=TopLevelEnum^#
}

var testAccessorUnresolvedTopLevelGet: TopLevelEnum {
  get {
    .#^testAccessorUnresolvedTopLevelGet?check=TopLevelEnum^#
  }
}

var testClosureUnresolvedTopLevelInit: TopLevelEnum = {
  .#^testClosureUnresolvedTopLevelInit?check=TopLevelEnum^#
}()
