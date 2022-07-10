// RUN: %target-parse-verify-swift

#if swift(>=1.0)
  let w = 1
#else
  // This shouldn't emit any diagnostics.
  asdf asdf asdf asdf
#endif

#if swift(>=1.2)

#if os(iOS)
  let z = 1
#else
  let z = 1
#endif

#else
  // This shouldn't emit any diagnostics.
  asdf asdf asdf asdf
#if os(iOS)
  // This shouldn't emit any diagnostics.
  asdf asdf asdf asdf
#else
  // This shouldn't emit any diagnostics.
  asdf asdf asdf asdf
#endif
  // This shouldn't emit any diagnostics.
  asdf asdf asdf asdf
#endif

#if !swift(>=1.0)
  // This shouldn't emit any diagnostics.
  $#%^*&
#endif

#if swift(">=7.1") // expected-error {{unexpected platform condition argument: expected a unary comparison, such as '>=2.2'}}
#endif

#if swift(">=2n.2") // expected-error {{unexpected platform condition argument: expected a unary comparison, such as '>=2.2'}}
#endif

#if swift("") // expected-error {{unexpected platform condition argument: expected a unary comparison, such as '>=2.2'}}
#endif

// We won't expect three version components to work for now.
#if swift(>=2.2.1) // expected-error {{expected named member of numeric literal}}
#endif

#if swift(>=2.0, *) // expected-error {{expected only one argument to platform condition}}
#endif

#if swift(>=, 2.0) // expected-error {{expected only one argument to platform condition}}
#endif

protocol P {
#if swift(>=2.2)
  associatedtype Index
#else
  // There should be no warning here.
  typealias Index

  // There should be no error here.
  adsf asdf asdf
  $#%^*&
  func foo(sdfsdfdsf adsf adsf asdf adsf adsf)
#endif
}

#if swift(>=2.2)
  func foo() {}
#else
  // There should be no error here.
  func foo(sdfsdfdsf adsf adsf asdf adsf adsf)
#endif

struct S {
#if swift(>=2.2)
  let x: Int
#else
  // There should be no error here.
  let x: @#$()%&*)@#$(%&*
#endif
}

#if swift(>=2.2)
var zzz = "zzz"
#else
// There should be no error here.
var zzz = zzz
#endif
