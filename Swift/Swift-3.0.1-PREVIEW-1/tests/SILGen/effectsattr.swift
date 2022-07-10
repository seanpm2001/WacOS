// RUN: %target-swift-frontend -parse-stdlib -emit-silgen %s | %FileCheck %s


//CHECK: [readonly] @func1
@effects(readonly) @_silgen_name("func1") func func1() { }

//CHECK: [readnone] @func2
@effects(readnone) @_silgen_name("func2") func func2() { }

//CHECK: [readwrite] @func3
@effects(readwrite) @_silgen_name("func3") func func3() { }

