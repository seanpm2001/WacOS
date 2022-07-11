// RUN: %swift -typecheck %s -verify -target armv7-unknown-linux-androideabi -disable-objc-interop -parse-stdlib
// RUN: %swift-ide-test -test-input-complete -source-filename=%s -target armv7-unknown-linux-androideabi

#if os(Linux)
// This block should not parse.
// os(Android) does not imply os(Linux).
let i: Int = "Hello"
#endif

#if arch(arm) && os(Android) && _runtime(_Native) && _endian(little)
class C {}
var x = C()
#endif
var y = x
