// RUN: %target-build-swift -typecheck %s -Xfrontend -verify
// RUN: %target-build-swift -typecheck -parse-as-library %s -Xfrontend -verify
// REQUIRES: objc_interop

// There was a bug where @objc without Foundation was only diagnosed if it
// appeared before any top-level expressions. The _1 and _2 variants of
// this test cover both cases.

@objc class Foo {} // expected-error {{@objc attribute used without importing module 'Foundation'}} expected-error {{only classes that inherit from NSObject can be declared @objc}} {{1-7=}}

#if false
#endif
