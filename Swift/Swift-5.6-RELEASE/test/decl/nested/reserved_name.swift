// RUN: %target-typecheck-verify-swift

struct S1 {
  // expected-error @+2 {{type member must not be named 'Type', since it would conflict with the 'foo.Type' expression}}
  // expected-note @+1 {{if this name is unavoidable, use backticks to escape it}} {{8-12=`Type`}}
  enum Type {
    case A
  }
}

struct S2 {
  enum `Type` {
    case A
  }
}

let s1: S1.Type = .A // expected-error {{type 'S1.Type' has no member 'A'}}
let s2: S2.`Type` = .A // no-error
