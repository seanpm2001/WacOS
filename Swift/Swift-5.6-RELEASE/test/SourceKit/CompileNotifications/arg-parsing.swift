// RUN: not %sourcekitd-test -req=track-compiles == -req=complete %s -offset=0 -- %s -no-such-arg 2>&1 | %FileCheck %s -check-prefix=ARG_PARSE_0
// ARG_PARSE_0: (Request Failed): error: unknown argument: '-no-such-arg'
// ARG_PARSE_0: {
// ARG_PARSE_0:  key.notification: source.notification.compile-will-start
// ARG_PARSE_0:  key.compileid: [[CID1:".*"]]
// ARG_PARSE_0:  key.compilerargs-string: "{{.*}}.swift -no-such-arg"
// ARG_PARSE_0:  key.compile_operation: source.compile.operation.code-completion
// ARG_PARSE_0: }
// ARG_PARSE_0: {
// ARG_PARSE_0:   key.notification: source.notification.compile-did-finish
// ARG_PARSE_0:   key.diagnostics: [
// ARG_PARSE_0:     {
// ARG_PARSE_0:       key.filepath: "<unknown>",
// ARG_PARSE_0:       key.severity: source.diagnostic.severity.error,
// ARG_PARSE_0:       key.id: "error_unknown_arg",
// ARG_PARSE_0:       key.description: "unknown argument: '-no-such-arg'"
// ARG_PARSE_0:     }
// ARG_PARSE_0:   ]
// ARG_PARSE_0:   key.compileid: [[CID1]]
// ARG_PARSE_0:   key.compile_operation: source.compile.operation.code-completion
// ARG_PARSE_0: }
// ARG_PARSE_0-NOT: compile-will-start
// ARG_PARSE_0-NOT: compile-did-finish

// RUN: %sourcekitd-test -req=track-compiles == -req=sema %s -print-raw-response -- %s -no-such-arg | %FileCheck %s -check-prefix=ARG_PARSE_1
// RUN: not %sourcekitd-test -req=track-compiles == -req=cursor -offset=0 %s -- %s -no-such-arg | %FileCheck %s -check-prefix=ARG_PARSE_1
// ARG_PARSE_1: {
// ARG_PARSE_1:  key.notification: source.notification.compile-will-start
// ARG_PARSE_1:  key.compileid: [[CID1:".*"]]
// ARG_PARSE_1:  key.compilerargs-string: "{{.*}}.swift -no-such-arg"
// ARG_PARSE_1: }
// ARG_PARSE_1: {
// ARG_PARSE_1:   key.notification: source.notification.compile-did-finish
// ARG_PARSE_1:   key.diagnostics: [
// ARG_PARSE_1:     {
// ARG_PARSE_1:       key.filepath: "<unknown>",
// ARG_PARSE_1:       key.severity: source.diagnostic.severity.error,
// ARG_PARSE_1:       key.id: "error_unknown_arg",
// ARG_PARSE_1:       key.description: "unknown argument: '-no-such-arg'"
// ARG_PARSE_1:     }
// ARG_PARSE_1:   ]
// ARG_PARSE_1:   key.compileid: [[CID1]]
// ARG_PARSE_1: }
// ARG_PARSE_1-NOT: compile-will-start
// ARG_PARSE_1-NOT: compile-did-finish

//  %sourcekitd-test -req=track-compiles == -req=complete %s -offset=0 | %FileCheck %s -check-prefix=ARG_PARSE_2
//  %sourcekitd-test -req=track-compiles == -req=sema %s -print-raw-response | %FileCheck %s -check-prefix=ARG_PARSE_2
// ARG_PARSE_2: {
// ARG_PARSE_2:  key.notification: source.notification.compile-will-start
// ARG_PARSE_2:  key.compileid: [[CID1:".*"]]
// ARG_PARSE_2: }
// ARG_PARSE_2: {
// ARG_PARSE_2:   key.notification: source.notification.compile-did-finish
// ARG_PARSE_2:   key.diagnostics: [
// ARG_PARSE_2:     {
// ARG_PARSE_2:       key.filepath: "<unknown>",
// ARG_PARSE_2:       key.severity: source.diagnostic.severity.error,
// ARG_PARSE_2:       key.id: "error_no_input_files",
// ARG_PARSE_2:       key.description: "no input files"
// ARG_PARSE_2:     }
// ARG_PARSE_2:   ]
// ARG_PARSE_2:   key.compileid: [[CID1]]
// ARG_PARSE_2: }
// ARG_PARSE_2-NOT: compile-will-start
// ARG_PARSE_2-NOT: compile-did-finish
