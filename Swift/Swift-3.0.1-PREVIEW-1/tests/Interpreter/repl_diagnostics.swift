// RUN: %target-repl-run-simple-swift | %FileCheck %s

// REQUIRES: swift_repl

var tooLarge = 11111111111111111111111111111
// CHECK: error: integer literal '11111111111111111111111111111' overflows when stored into 'Int'

