// Playgrounds
// RUN: %target-parse-verify-swift -playground

// Immediate mode
// RUN: %target-parse-verify-swift -interpret

// REQUIRES: OS=macosx
// REQUIRES: swift_interpreter

@available(OSX, introduced: 10.7, deprecated: 10.8)
func deprecatedOn10_8() { }

func someFunction() {
  // We would normally emit a warning indicating this check is useless (because
  // the minimum deployment target is 10.9) -- but do not when compiling for
  // playgrounds because the developer cannot set the minimum deployment target
  // herself. We also suppress this warning when compiling in immediate mode.
  if #available(OSX 10.8, *) {
  } else {
    // This branch is dead with our minimum deployment target, so don't emit
    // deprecation and unavailability diagnostics in it.
    deprecatedOn10_8() // no-warning
    availableOn10_50() // no-warning
  }

  if #available(OSX 10.50, *) { // expected-note {{enclosing scope here}}
    // Still warn if the check is useless because an enclosing #available rules
    // it out.
    if #available(OSX 10.50, *) { // expected-warning {{unnecessary check for 'OSX'; enclosing scope ensures guard will always be true}}
    }
  }
}

@available(OSX 10.50, *)
func availableOn10_50() { // expected-note {{enclosing scope here}}
  // Still warn if the check is useless because an enclosing @available rules
  // it out.
  if #available(OSX 10.50, *) { // expected-warning {{unnecessary check for 'OSX'; enclosing scope ensures guard will always be true}}
  }
}

// Make sure we don't warn at the top level
if #available(OSX 10.8, *) {
}
