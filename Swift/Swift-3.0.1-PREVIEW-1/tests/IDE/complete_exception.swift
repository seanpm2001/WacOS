// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=CATCH1 | %FileCheck %s -check-prefix=CATCH1
// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=THROW1 > %t.throw1
// RUN: %FileCheck %s -check-prefix=THROW1 < %t.throw1
// RUN: %FileCheck %s -check-prefix=THROW1-LOCAL < %t.throw1
// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=CATCH2 | %FileCheck %s -check-prefix=CATCH2
// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=THROW2 | %FileCheck %s -check-prefix=THROW2
// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=CATCH3 | %FileCheck %s -check-prefix=CATCH3
// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=TOP_LEVEL_CATCH1 | %FileCheck %s -check-prefix=CATCH1
// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=TOP_LEVEL_THROW1 | %FileCheck %s -check-prefix=THROW1

// FIXME: <rdar://problem/21001526> No dot code completion results in switch case or catch stmt at top-level
// RUNdisabled: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=TOP_LEVEL_CATCH2 | %FileCheck %s -check-prefix=CATCH2
// RUNdisabled: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=TOP_LEVEL_THROW2 | %FileCheck %s -check-prefix=THROW2

// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=INSIDE_CATCH1 > %t.inside_catch1
// RUN: %FileCheck %s -check-prefix=STMT < %t.inside_catch1
// RUN: %FileCheck %s -check-prefix=IMPLICIT_ERROR < %t.inside_catch1

// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=INSIDE_CATCH2 > %t.inside_catch2
// RUN: %FileCheck %s -check-prefix=STMT < %t.inside_catch2
// RUN: %FileCheck %s -check-prefix=EXPLICIT_ERROR_E < %t.inside_catch2

// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=INSIDE_CATCH3 > %t.inside_catch3
// RUN: %FileCheck %s -check-prefix=STMT < %t.inside_catch3
// RUN: %FileCheck %s -check-prefix=EXPLICIT_NSERROR_E < %t.inside_catch3

// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=INSIDE_CATCH4 > %t.inside_catch4
// RUN: %FileCheck %s -check-prefix=STMT < %t.inside_catch4
// RUN: %FileCheck %s -check-prefix=EXPLICIT_ERROR_PAYLOAD_I < %t.inside_catch4

// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=INSIDE_CATCH5 > %t.inside_catch5
// RUN: %FileCheck %s -check-prefix=STMT < %t.inside_catch5
// RUN: %FileCheck %s -check-prefix=EXPLICIT_ERROR_E < %t.inside_catch5
// RUN: %FileCheck %s -check-prefix=NO_ERROR_AND_A < %t.inside_catch5

// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=INSIDE_CATCH6 > %t.inside_catch6
// RUN: %FileCheck %s -check-prefix=STMT < %t.inside_catch6
// RUN: %FileCheck %s -check-prefix=NO_E < %t.inside_catch6
// RUN: %FileCheck %s -check-prefix=NO_ERROR_AND_A < %t.inside_catch6

// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=INSIDE_CATCH_ERR_DOT1 | %FileCheck %s -check-prefix=ERROR_DOT
// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=INSIDE_CATCH_ERR_DOT2 | %FileCheck %s -check-prefix=ERROR_DOT
// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=INSIDE_CATCH_ERR_DOT3 | %FileCheck %s -check-prefix=NSERROR_DOT
// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=INSIDE_CATCH_ERR_DOT4 | %FileCheck %s -check-prefix=INT_DOT

// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=TOP_LEVEL_INSIDE_CATCH1 > %t.top_level_inside_catch1
// RUN: %FileCheck %s -check-prefix=STMT < %t.top_level_inside_catch1
// RUN: %FileCheck %s -check-prefix=IMPLICIT_ERROR < %t.top_level_inside_catch1

// RUN: %target-swift-ide-test(mock-sdk: %clang-importer-sdk) -code-completion -source-filename %s -code-completion-token=TOP_LEVEL_INSIDE_CATCH_ERR_DOT1 | %FileCheck %s -check-prefix=ERROR_DOT

// REQUIRES: objc_interop

import Foundation // importer SDK

protocol ErrorPro1 : Error {}
class Error1 : Error {}
class Error2 : Error {}
class Error3 {}
extension Error3 : Error{}
enum Error4 : Error {
  case E1
  case E2(Int32)
}
class NoneError1 {}

func getError1() -> Error1 { return Error1() }
func getNSError() -> NSError { return NSError(domain: "", code: 1, userInfo: [:]) }

func test001() {
  do {} catch #^CATCH1^#

// CATCH1:      Begin completions
// CATCH1-DAG:  Decl[Enum]/CurrModule:              Error4[#Error4#]; name=Error4{{$}}
// CATCH1-DAG:  Decl[Class]/CurrModule:             Error3[#Error3#]; name=Error3{{$}}
// CATCH1-DAG:  Decl[Class]/CurrModule:             Error2[#Error2#]; name=Error2{{$}}
// CATCH1-DAG:  Decl[Class]/CurrModule:             Error1[#Error1#]; name=Error1{{$}}
// CATCH1-DAG:  Keyword[let]/None:                  let{{; name=.+$}}
// CATCH1-DAG:  Decl[Class]/CurrModule:             NoneError1[#NoneError1#]; name=NoneError1{{$}}
// CATCH1-DAG:  Decl[Class]/OtherModule[Foundation]: NSError[#NSError#]{{; name=.+$}}
}

func test002() {
  let text = "NonError"
  let e1 = Error1()
  let e2 = Error2()
  throw #^THROW1^#

// THROW1:      Begin completions
// THROW1-DAG:  Decl[Enum]/CurrModule:              Error4[#Error4#]; name=Error4{{$}}
// THROW1-DAG:  Decl[Class]/CurrModule:             Error3[#Error3#]; name=Error3{{$}}
// THROW1-DAG:  Decl[Class]/CurrModule:             Error2[#Error2#]; name=Error2{{$}}
// THROW1-DAG:  Decl[Class]/CurrModule:             Error1[#Error1#]; name=Error1{{$}}
// THROW1-DAG:  Decl[Protocol]/CurrModule:          ErrorPro1[#ErrorPro1#]; name=ErrorPro1{{$}}
// THROW1-DAG:  Decl[FreeFunction]/CurrModule:      getError1()[#Error1#]{{; name=.+$}}
// THROW1-DAG:  Decl[FreeFunction]/CurrModule:      getNSError()[#NSError#]{{; name=.+$}}

// If we could prove that there is no way to get to an Error value by
// starting from these, we could remove them.  But that may be infeasible in
// the presence of overloaded operators.
// THROW1-DAG: Decl[Class]/CurrModule:             NoneError1[#NoneError1#]; name=NoneError1{{$}}
// THROW1-LOCAL: Decl[LocalVar]/Local:               text[#String#]; name=text{{$}}
// THROW1-LOCAL: Decl[LocalVar]/Local:               e1[#Error1#]; name=e1{{$}}
// THROW1-LOCAL: Decl[LocalVar]/Local:               e2[#Error2#]; name=e2{{$}}
}

func test003() {
  do {} catch Error4.#^CATCH2^#
// CATCH2: Begin completions
// CATCH2: Decl[EnumElement]/CurrNominal: E1[#Error4#]{{; name=.+$}}
// CATCH2: Decl[EnumElement]/CurrNominal: E2({#Int32#})[#(Int32) -> Error4#]{{; name=.+$}}
// CATCH2: End completions
}

func test004() {
  throw Error4.#^THROW2^#
// THROW2: Begin completions
// THROW2: Decl[EnumElement]/CurrNominal: E1[#Error4#]{{; name=.+$}}
// THROW2: Decl[EnumElement]/CurrNominal: E2({#Int32#})[#(Int32) -> Error4#]{{; name=.+$}}
// THROW2: End completions
}

func test005() {
  do {} catch Error4.E2#^CATCH3^#
// CATCH3: Begin completions
// CATCH3: Pattern/ExprSpecific:               ({#Int32#})[#Error4#]{{; name=.+$}}
// CATCH3: End completions
}

//===--- Top-level throw/catch
do {} catch #^TOP_LEVEL_CATCH1^# {}
throw #^TOP_LEVEL_THROW1^#
do {} catch Error4.#^TOP_LEVEL_CATCH2^# {}
throw Error4.#^TOP_LEVEL_THROW2^#

//===--- Inside catch body

// Statement-level code completions. This isn't exhaustive.
// STMT: Begin completions
// STMT-DAG: Keyword[if]/None:                       if; name=if
// STMT-DAG: Decl[Class]/CurrModule:             Error1[#Error1#]; name=Error1
// STMT-DAG: Decl[Class]/CurrModule:             Error2[#Error2#]; name=Error2
// STMT-DAG: Decl[FreeFunction]/CurrModule:      getError1()[#Error1#]; name=getError1()
// STMT-DAG: Decl[FreeFunction]/CurrModule:      getNSError()[#NSError#]; name=getNSError()
// STMT: End completions

func test006() {
  do {
  } catch {
    #^INSIDE_CATCH1^#
  }
// IMPLICIT_ERROR: Decl[LocalVar]/Local:  error[#Error#]; name=error
}
func test007() {
  do {
  } catch let e {
    #^INSIDE_CATCH2^#
  }
// EXPLICIT_ERROR_E: Decl[LocalVar]/Local: e[#Error#]; name=e
}
func test008() {
  do {
  } catch let e as NSError {
    #^INSIDE_CATCH3^#
  }
// EXPLICIT_NSERROR_E: Decl[LocalVar]/Local: e[#NSError#]; name=e
}
func test009() {
  do {
  } catch Error4.E2(let i) {
    #^INSIDE_CATCH4^#
  }

// FIXME: we're getting parentheses around the type when it's unnamed...
// EXPLICIT_ERROR_PAYLOAD_I: Decl[LocalVar]/Local: i[#(Int32)#]; name=i
}
func test010() {
  do {
  } catch let awesomeError {
  } catch let e {
    #^INSIDE_CATCH5^#
  } catch {}
// NO_ERROR_AND_A-NOT: awesomeError
// NO_ERROR_AND_A-NOT: Decl[LocalVar]/Local: error
}
func test011() {
  do {
  } catch let awesomeError {
  } catch let excellentError {
  } catch {}
  #^INSIDE_CATCH6^#
// NO_E-NOT: excellentError
}
func test012() {
  do {
  } catch {
    error.#^INSIDE_CATCH_ERR_DOT1^#
  }
// ERROR_DOT-NOT: Begin completions
}
func test013() {
  do {
  } catch let e {
    e.#^INSIDE_CATCH_ERR_DOT2^#
  }
}
func test014() {
  do {
  } catch let e as NSError {
    e.#^INSIDE_CATCH_ERR_DOT3^#
  }
// NSERROR_DOT: Begin completions
// NSERROR_DOT-DAG: Decl[InstanceVar]/CurrNominal:      domain[#String#]; name=domain
// NSERROR_DOT-DAG: Decl[InstanceVar]/CurrNominal:      code[#Int#]; name=code
// NSERROR_DOT-DAG: Decl[InstanceVar]/Super:            hashValue[#Int#]; name=hashValue
// NSERROR_DOT-DAG: Decl[InstanceMethod]/Super:         myClass()[#AnyClass!#]; name=myClass()
// NSERROR_DOT-DAG: Decl[InstanceMethod]/Super:         isEqual({#(other): NSObject!#})[#Bool#]; name=isEqual(other: NSObject!)
// NSERROR_DOT-DAG: Decl[InstanceVar]/Super:            hash[#Int#]; name=hash
// NSERROR_DOT-DAG: Decl[InstanceVar]/Super:            description[#String#]; name=description
// NSERROR_DOT: End completions
}
func test015() {
  do {
  } catch Error4.E2(let i) where i == 2 {
    i.#^INSIDE_CATCH_ERR_DOT4^#
  }
}
// Check that we can complete on the bound value; Not exhaustive..
// INT_DOT: Begin completions
// INT_DOT-DAG: Decl[InstanceVar]/CurrNominal:      bigEndian[#Int32#]; name=bigEndian
// INT_DOT-DAG: Decl[InstanceVar]/CurrNominal:      littleEndian[#Int32#]; name=littleEndian
// INT_DOT: End completions

//===--- Inside catch body top-level
do {
} catch {
  #^TOP_LEVEL_INSIDE_CATCH1^#
}
do {
} catch {
  error.#^TOP_LEVEL_INSIDE_CATCH_ERR_DOT1^#
}
