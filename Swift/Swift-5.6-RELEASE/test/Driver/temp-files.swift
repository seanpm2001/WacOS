// REQUIRES: objc_interop
// FIXME: actually, this test requires Mach-O.

// RUN: %empty-directory(%t)
// RUN: %empty-directory(%t/tmp)
// RUN: touch %t/tmp/dummy
// RUN: env TMPDIR=%t/tmp/ %target-swiftc_driver -emit-executable %s -o %t/main
// RUN: ls %t/main
// RUN: ls %t/tmp | %FileCheck -check-prefix=EMPTY %s

// EMPTY-NOT: .{{(o|swiftmodule|swiftdoc)}}

// RUN: %empty-directory(%t) && %empty-directory(%t/tmp) && touch %t/tmp/dummy
// RUN: env TMPDIR=%t/tmp/ %target-swiftc_driver -target %target-triple -sdk %sdk -module-cache-path %t -emit-executable %s -o %t/main2 -emit-module-path %t/main2.swiftmodule
// RUN: ls %t/main2
// RUN: ls %t/main2.swiftmodule
// RUN: ls %t/tmp | %FileCheck -check-prefix=EMPTY %s

// RUN: %empty-directory(%t) && %empty-directory(%t/tmp) && touch %t/tmp/dummy
// RUN: env TMPDIR=%t/tmp/ %target-swiftc_driver -target %target-triple -sdk %sdk -module-cache-path %t -emit-executable %s -o %t/main3 -g
// RUN: ls %t/main3
// RUN: ls %t/tmp | %FileCheck -check-prefix=EMPTY %s

// RUN: %empty-directory(%t) && %empty-directory(%t/tmp) && touch %t/tmp/dummy
// RUN: env TMPDIR=%t/tmp/ %target-swiftc_driver -target %target-triple -sdk %sdk -module-cache-path %t -emit-executable %s -o %t/main4 -emit-module-path %t/main4.swiftmodule -g
// RUN: ls %t/main4
// RUN: ls %t/main4.swiftmodule
// RUN: ls %t | %FileCheck -check-prefix=MAIN4-%target-object-format %s
// RUN: ls %t/tmp | %FileCheck -check-prefix=EMPTY %s

// MAIN4-macho: main4.dSYM
// MAIN4-elf-NOT: .dSYM

// RUN: %empty-directory(%t) && %empty-directory(%t/tmp) && touch %t/tmp/dummy
// RUN: echo "{\"%s\": {\"object\": \"%t/main5.o\"}}" > %t.json
// RUN: env TMPDIR=%t/tmp/ %target-swiftc_driver -target %target-triple -sdk %sdk -module-cache-path %t -emit-executable %s -o %t/main5 -output-file-map %t.json -g
// RUN: ls %t/main5
// RUN: ls %t/main5.o
// RUN: ls %t | %FileCheck -check-prefix=MAIN5-%target-object-format %s
// RUN: ls %t/tmp | %FileCheck -check-prefix=EMPTY %s

// MAIN5-macho: main5.dSYM
// MAIN5-elf-NOT: .dSYM

// RUN: %empty-directory(%t) && %empty-directory(%t/tmp) && touch %t/tmp/dummy
// RUN: env TMPDIR=%t/tmp/ %target-swiftc_driver -target %target-triple -sdk %sdk -module-cache-path %t -emit-executable %s -o %t/main6 -g -save-temps
// RUN: ls %t/main6
// RUN: ls %t | %FileCheck -check-prefix=MAIN6-%target-object-format %s
// RUN: ls %t/tmp | %FileCheck -check-prefix=SAVE-TEMPS %s
// RUN: ls %t/tmp | %FileCheck -check-prefix=SAVE-TEMPS-%target-object-format %s

// MAIN6-macho: main6.dSYM
// MAIN6-elf-NOT: .dSYM

// SAVE-TEMPS-DAG: temp-files-{{.+}}.o
// SAVE-TEMPS-DAG: temp-files-{{.+}}.swiftmodule
// SAVE-TEMPS-DAG: main6-{{.+}}.swiftmodule
// SAVE-TEMPS-NOT: .{{(o|swiftmodule|swiftdoc)}}

// SAVE-TEMPS-macho-NOT: .autolink
// SAVE-TEMPS-elf: temp-files-{{.+}}.autolink
