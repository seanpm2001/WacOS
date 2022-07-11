// RUN: %target-typecheck-verify-swift

func test1() {
  var a : Int {
#if arch(x86_64)
    return 0
#else
    return 1
#endif
  }
}

// Would trigger assertion when AST verifier checks source ranges ("child source range not contained within its parent")
func test2() { // expected-note {{match}}
  var a : Int { // expected-note {{match}} expected-note {{'a' declared here}}
    switch i { // expected-error {{cannot find 'i' in scope; did you mean 'a'}} expected-error{{'switch' statement body must have at least one 'case'}}
}
// expected-error@+1 2 {{expected '}'}}
