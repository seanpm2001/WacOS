// RUN: %empty-directory(%t)

// REQUIRES: concurrency

func simple(_ completion: @escaping (Result<String, Error>) -> Void) { }
func simple() async throws -> String { "" }

func simpleWithArg(_ arg: Int) async throws -> String { "" }
func simpleWithArg(_ arg: Int, _ completion: @escaping (Result<String, Error>) -> Void) { }

func noError() async -> String { "" }
func noError(_ completion: @escaping (Result<String, Never>) -> Void) { }

func voidNoError() async {}
func voidNoError(completion: @escaping (Result<Void, Never>) -> Void) {}

func voidError() async throws {}
func voidError(completion: @escaping (Result<Void, Error>) -> Void) {}

func test(_ str: String) -> Bool { return false }

// RUN: %refactor-check-compiles -add-async-alternative -dump-text -source-filename %s -pos=%(line+1):1 | %FileCheck -check-prefix VOID-RESULT %s
func voidResult(completion: @escaping (Result<Void, Never>) -> Void) {}
// VOID-RESULT: func voidResult() async {}

// RUN: %refactor-check-compiles -add-async-alternative -dump-text -source-filename %s -pos=%(line+1):1 | %FileCheck -check-prefix VOID-AND-ERROR-RESULT %s
func voidAndErrorResult(completion: @escaping (Result<Void, Error>) -> Void) {}
// VOID-AND-ERROR-RESULT: func voidAndErrorResult() async throws {}

func testResultConversion() async throws {
  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=SIMPLE %s
  simple { res in
    print("result \(res)")
  }
  // SIMPLE: let res = try await simple()
  // SIMPLE-NEXT: print("result \(<#res#>)")

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=NOERROR %s
  noError { res in
    print("result \(res)")
  }
  // NOERROR: let res = await noError()
  // NOERROR-NEXT: print("result \(<#res#>)")

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=DOBLOCK %s
  simple { res in
    print("before")
    switch res {
    case .success(let str):
      print("result \(str)")
    case .failure(let err):
      print("error \(err)")
    }
    print("after")
  }
  // DOBLOCK: do {
  // DOBLOCK-NEXT: let str = try await simple()
  // DOBLOCK-NEXT: print("before")
  // DOBLOCK-NEXT: print("result \(str)")
  // DOBLOCK-NEXT: print("after")
  // DOBLOCK-NEXT: } catch let err {
  // DOBLOCK-NEXT: print("error \(err)")
  // DOBLOCK-NEXT: }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=DOBLOCK %s
  simple { res in
    print("before")
    if case .success(let str) = res {
      print("result \(str)")
    } else if case .failure(let err) = res {
      print("error \(err)")
    }
    print("after")
  }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=DOBLOCK %s
  simple { res in
    print("before")
    switch res {
    case .success(let str):
      print("result \(str)")
      break
    case .failure(let err):
      print("error \(err)")
      break
    }
    print("after")
  }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=DOBLOCK %s
  simple { res in
    print("before")
    switch res {
    case .success(let str):
      print("result \(str)")
      return
    case .failure(let err):
      print("error \(err)")
      return
    }
    print("after")
  }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=SUCCESS %s
  simple { res in
    print("before")
    if case .success(let str) = res {
      print("result \(str)")
    }
    print("after")
  }
  // SUCCESS: convert_result.swift
  // SUCCESS-NEXT: let str = try await simple()
  // SUCCESS-NEXT: print("before")
  // SUCCESS-NEXT: print("result \(str)")
  // SUCCESS-NEXT: print("after")
  // SUCCESS-NOT: }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=SUCCESS %s
  simple { res in
    print("before")
    guard case .success(let str) = res else {
      return
    }
    print("result \(str)")
    print("after")
  }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=DOBLOCKUNBOUND %s
  simple { res in
    print("before")
    guard case .success(let str) = res else {
      print("err")
      return
    }
    print("result \(str)")
    print("after")
  }
  // DOBLOCKUNBOUND: do {
  // DOBLOCKUNBOUND-NEXT: let str = try await simple()
  // DOBLOCKUNBOUND-NEXT: print("before")
  // DOBLOCKUNBOUND-NEXT: print("result \(str)")
  // DOBLOCKUNBOUND-NEXT: print("after")
  // DOBLOCKUNBOUND-NEXT: } catch {
  // DOBLOCKUNBOUND-NEXT: print("err")
  // DOBLOCKUNBOUND-NEXT: }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=SUCCESS %s
  simple { res in
    print("before")
    if let str = try? res.get() {
      print("result \(str)")
    }
    print("after")
  }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=DOBLOCKUNBOUND %s
  simple { res in
    print("before")
    guard let str = try? res.get() else {
      print("err")
      return
    }
    print("result \(str)")
    print("after")
  }

  // Cannot use refactor-check-compiles, as cannot infer type of the 'get' member on the placeholder.
  // RUN: %refactor -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=UNKNOWN %s
  simple { res in
    print("before \(res)")
    if case .success(let str) = res {
      print("result \(str) \(try! res.get())")
    }
    print("after")
  }
  // UNKNOWN: convert_result.swift
  // UNKNOWN-NEXT: let str = try await simple()
  // UNKNOWN-NEXT: print("before \(<#str#>)")
  // UNKNOWN-NEXT: print("result \(str) \(try! <#str#>.get())")
  // UNKNOWN-NEXT: print("after")
  // UNKNOWN-NOT: }

  // Cannot use refactor-check-compiles, as cannot infer type of the 'get' member on the placeholder.
  // RUN: %refactor -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=UNKNOWNUNBOUND %s
  simple { res in
    print("before \(res)")
    if case .success = res {
      print("result \(res) \(try! res.get())")
    }
    print("after")
  }
  // UNKNOWNUNBOUND: convert_result.swift
  // UNKNOWNUNBOUND-NEXT: let res = try await simple()
  // UNKNOWNUNBOUND-NEXT: print("before \(<#res#>)")
  // UNKNOWNUNBOUND-NEXT: print("result \(<#res#>) \(try! <#res#>.get())")
  // UNKNOWNUNBOUND-NEXT: print("after")
  // UNKNOWN-NOT: }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=MULTIPLE-BINDS %s
  simple { res in
    print("before")
    if case .success(let str) = res {
      print("result \(str)")
    }
    if case .success(let str2) = res {
      print("result \(str2)")
    }
    print("after")
  }
  // MULTIPLE-BINDS: convert_result.swift
  // MULTIPLE-BINDS-NEXT: let str = try await simple()
  // MULTIPLE-BINDS-NEXT: print("before")
  // MULTIPLE-BINDS-NEXT: print("result \(str)")
  // MULTIPLE-BINDS-NEXT: print("result \(str)")
  // MULTIPLE-BINDS-NEXT: print("after")
  // MULTIPLE-BINDS-NOT: }

  // RUN: not %refactor -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3
  simple { res in
    print("before")
    switch res {
    case .success(let str):
      print("result \(str)")
    case .failure(let err):
      print("error \(err)")
    default:
      print("default")
    }
    print("after")
  }

  // RUN: not %refactor -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3
  simple { res in
    print("before")
    switch res {
    case .success(let str):
      print("result \(str)")
    default:
      print("err")
    }
    print("after")
  }

  // RUN: not %refactor -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3
  simple { res in
    print("before")
    switch res {
    case .success, .failure:
      print("either")
    }
    print("after")
  }

  // RUN: not %refactor -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3
  simple { res in
    print("before")
    switch res {
    case .success, .failure:
      print("either")
    }
    print("after")
  }

  // RUN: not %refactor -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3
  simple { res in
    print("before")
    switch res {
    case .success:
      fallthrough
    case .failure:
      print("either")
    }
    print("after")
  }

  // RUN: not %refactor -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3
  simple { res in
    print("before")
    switch res {
    case .success(let str) where str.hasPrefix("match"):
      print("pattern matched result \(str)")
    case .success(let str):
      print("result \(str)")
    case .failure(let err):
      print("error \(err)")
    }
    print("after")
  }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=NESTEDRET %s
  simple { res in
    print("before")
    switch res {
    case .success(let str):
      if test(str) {
        return
      }
      print("result \(str)")
    case .failure:
      break
    }
    print("after")
  }
  // NESTEDRET: convert_result.swift
  // NESTEDRET-NEXT: let str = try await simple()
  // NESTEDRET-NEXT: print("before")
  // NESTEDRET-NEXT: if test(str) {
  // NESTEDRET-NEXT:   <#return#>
  // NESTEDRET-NEXT: }
  // NESTEDRET-NEXT: print("result \(str)")
  // NESTEDRET-NEXT: print("after")
  // NESTEDRET-NOT: }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=NESTEDBREAK %s
  simple { res in
    print("before")
    switch res {
    case .success(let str):
      if test(str) {
        break
      }
      print("result \(str)")
    case .failure:
      break
    }
    print("after")
  }
  // NESTEDBREAK: convert_result.swift
  // NESTEDBREAK-NEXT: let str = try await simple()
  // NESTEDBREAK-NEXT: print("before")
  // NESTEDBREAK-NEXT: if test(str) {
  // NESTEDBREAK-NEXT:   <#break#>
  // NESTEDBREAK-NEXT: }
  // NESTEDBREAK-NEXT: print("result \(str)")
  // NESTEDBREAK-NEXT: print("after")
  // NESTEDBREAK-NOT: }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=NESTEDBREAK-COMMENT %s
  simple { res in // a
    // b
    print("before")
    // c
    switch res {
    // d
    case .success(let str): // e
      if test(str) {
        // f
        break
        // g
      }
      // h
      print("result \(str)")
      // i
    case .failure:
      // j
      break
      // k
    }
    // l
    print("after")
    // m
  }
  // NESTEDBREAK-COMMENT:      let str = try await simple()
  // NESTEDBREAK-COMMENT-NEXT: // a
  // NESTEDBREAK-COMMENT-NEXT: // b
  // NESTEDBREAK-COMMENT-NEXT: print("before")
  // NESTEDBREAK-COMMENT-NEXT: // c
  // NESTEDBREAK-COMMENT-NEXT: // d
  // NESTEDBREAK-COMMENT-NEXT: // e
  // NESTEDBREAK-COMMENT-NEXT: if test(str) {
  // NESTEDBREAK-COMMENT-NEXT: // f
  // NESTEDBREAK-COMMENT-NEXT:   <#break#>
  // NESTEDBREAK-COMMENT-NEXT: // g
  // NESTEDBREAK-COMMENT-NEXT: }
  // NESTEDBREAK-COMMENT-NEXT: // h
  // NESTEDBREAK-COMMENT-NEXT: print("result \(str)")
  // NESTEDBREAK-COMMENT-NEXT: // i
  // NESTEDBREAK-COMMENT-NEXT: // j
  // NESTEDBREAK-COMMENT-NEXT: // k
  // NESTEDBREAK-COMMENT-NEXT: // l
  // NESTEDBREAK-COMMENT-NEXT: print("after")
  // NESTEDBREAK-COMMENT-NEXT: // m
  // NESTEDBREAK-COMMENT-NOT: }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=ERROR-BLOCK-COMMENT %s
  simple { res in
    // a
    print("before")
    // b
    switch res {
    case .success(let str):
      // c
      print("result \(str)")
      // d
    case .failure:
      // e
      print("fail")
      // f
      return
      // g
    }
    // h
    print("after")
    // i
  }
  // ERROR-BLOCK-COMMENT:      do {
  // ERROR-BLOCK-COMMENT-NEXT:   let str = try await simple()
  // ERROR-BLOCK-COMMENT-NEXT:   // a
  // ERROR-BLOCK-COMMENT-NEXT:   print("before")
  // ERROR-BLOCK-COMMENT-NEXT:   // b
  // ERROR-BLOCK-COMMENT-NEXT:   // c
  // ERROR-BLOCK-COMMENT-NEXT:   print("result \(str)")
  // ERROR-BLOCK-COMMENT-NEXT:   // d
  // ERROR-BLOCK-COMMENT-NEXT:   // h
  // ERROR-BLOCK-COMMENT-NEXT:   print("after")
  // ERROR-BLOCK-COMMENT-NEXT:   // i
  // ERROR-BLOCK-COMMENT-NEXT: } catch {
  // ERROR-BLOCK-COMMENT-NEXT:   // e
  // ERROR-BLOCK-COMMENT-NEXT:   print("fail")
  // ERROR-BLOCK-COMMENT-NEXT:   // f
  // ERROR-BLOCK-COMMENT-NEXT:   // g
  // ERROR-BLOCK-COMMENT-NEXT: }
  // ERROR-BLOCK-COMMENT-NOT: }

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=VOID-RESULT-CALL %s
  voidNoError { res in
    print(res)
  }
  // VOID-RESULT-CALL: {{^}}await voidNoError()
  // VOID-RESULT-CALL: {{^}}print(<#res#>)

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=VOID-AND-ERROR-RESULT-CALL %s
  voidError { res in
    print(res)
  }
  // VOID-AND-ERROR-RESULT-CALL: {{^}}try await voidError()
  // VOID-AND-ERROR-RESULT-CALL: {{^}}print(<#res#>)

  // Make sure we ignore an unrelated switch.
  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=IGNORE-UNRELATED %s
  simple { res in
    print("before")
    switch Bool.random() {
    case true:
      break
    case false:
      break
    }
    print("after")
  }
  // IGNORE-UNRELATED:      let res = try await simple()
  // IGNORE-UNRELATED-NEXT: print("before")
  // IGNORE-UNRELATED-NEXT: switch Bool.random() {
  // IGNORE-UNRELATED-NEXT:  case true:
  // IGNORE-UNRELATED-NEXT:  {{^}} break{{$}}
  // IGNORE-UNRELATED-NEXT:  case false:
  // IGNORE-UNRELATED-NEXT:  {{^}} break{{$}}
  // IGNORE-UNRELATED-NEXT:  }
  // IGNORE-UNRELATED-NEXT: print("after")

  // RUN: %refactor-check-compiles -convert-call-to-async-alternative -dump-text -source-filename %s -pos=%(line+1):3 | %FileCheck -check-prefix=BREAK-RET-PLACEHOLDER %s
  simpleWithArg({ return 0 }()) { res in
    switch res {
    case .success:
      if .random() { break }
      x: if .random() { break x }
    case .failure:
      break
    }

    func foo<T>(_ x: T) {
      if .random() { return }
    }
    foo(res)

    let fn = {
      if .random() { return }
      return
    }
    fn()

    _ = { return }()

    switch Bool.random() {
    case true:
      break
    case false:
      if .random() { break }
      y: if .random() { break y }
      return
    }

    x: if .random() {
      break x
    }
    if .random() { return }
  }

  // Make sure we replace lifted break/returns with placeholders, but keep nested
  // break/returns in e.g closures or labelled control flow in place.

  // BREAK-RET-PLACEHOLDER:      let res = try await simpleWithArg({ return 0 }())
  // BREAK-RET-PLACEHOLDER-NEXT: if .random() { <#break#> }
  // BREAK-RET-PLACEHOLDER-NEXT: x: if .random() { break x }
  // BREAK-RET-PLACEHOLDER-NEXT: func foo<T>(_ x: T) {
  // BREAK-RET-PLACEHOLDER-NEXT:   if .random() { return }
  // BREAK-RET-PLACEHOLDER-NEXT: }
  // BREAK-RET-PLACEHOLDER-NEXT: foo(<#res#>)
  // BREAK-RET-PLACEHOLDER-NEXT: let fn = {
  // BREAK-RET-PLACEHOLDER-NEXT:   if .random() { return }
  // BREAK-RET-PLACEHOLDER-NEXT:   {{^}} return{{$}}
  // BREAK-RET-PLACEHOLDER-NEXT: }
  // BREAK-RET-PLACEHOLDER-NEXT: fn()
  // BREAK-RET-PLACEHOLDER-NEXT: _ = { return }()
  // BREAK-RET-PLACEHOLDER-NEXT: switch Bool.random() {
  // BREAK-RET-PLACEHOLDER-NEXT: case true:
  // BREAK-RET-PLACEHOLDER-NEXT:   {{^}} break{{$}}
  // BREAK-RET-PLACEHOLDER-NEXT: case false:
  // BREAK-RET-PLACEHOLDER-NEXT:   if .random() { break }
  // BREAK-RET-PLACEHOLDER-NEXT:   y: if .random() { break y }
  // BREAK-RET-PLACEHOLDER-NEXT:   <#return#>
  // BREAK-RET-PLACEHOLDER-NEXT: }
  // BREAK-RET-PLACEHOLDER-NEXT: x: if .random() {
  // BREAK-RET-PLACEHOLDER-NEXT:   {{^}} break x{{$}}
  // BREAK-RET-PLACEHOLDER-NEXT: }
  // BREAK-RET-PLACEHOLDER-NEXT: if .random() { <#return#> }
}
