// RUN: %empty-directory(%t)

// RUN: %swift -disable-legacy-type-info -target x86_64-apple-macosx10.9 -parse-stdlib -autolink-force-load -module-name TEST -module-link-name TEST -emit-ir %s %S/../Inputs/empty.swift | %FileCheck -check-prefix=CHECK-WMO %s

// CHECK-WMO: source_filename = "<swift-imported-modules>"
// CHECK-WMO: @"_swift_FORCE_LOAD_$_TEST"
// CHECK-WMO-NOT: source_filename


// RUN: %swift -disable-legacy-type-info -target x86_64-apple-macosx10.9 -parse-stdlib -autolink-force-load -module-name TEST -module-link-name TEST -emit-ir -num-threads 1 %s %S/../Inputs/empty.swift | %FileCheck -check-prefix=CHECK-WMO-THREADED %s

// CHECK-WMO-THREADED: source_filename = "<swift-imported-modules>"
// CHECK-WMO-THREADED: @"_swift_FORCE_LOAD_$_TEST"
// CHECK-WMO-THREADED: source_filename = "<swift-imported-modules>"
// CHECK-WMO-THREADED: @"_swift_FORCE_LOAD_$_TEST"

// RUN: %swift -disable-legacy-type-info -target x86_64-apple-macosx10.9 -parse-stdlib -autolink-force-load -module-name TEST -module-link-name TEST -emit-ir -primary-file %s %S/../Inputs/empty.swift | %FileCheck -check-prefix=CHECK-SINGLE-FILE-FIRST %s
// RUN: %swift -disable-legacy-type-info -target x86_64-apple-macosx10.9 -parse-stdlib -autolink-force-load -module-name TEST -module-link-name TEST -emit-ir %S/../Inputs/empty.swift -primary-file %s | %FileCheck -check-prefix=CHECK-SINGLE-FILE-SECOND %s

// CHECK-SINGLE-FILE-FIRST: source_filename = "<swift-imported-modules>"
// CHECK-SINGLE-FILE-FIRST: @"_swift_FORCE_LOAD_$_TEST"
// CHECK-SINGLE-FILE-FIRST-NOT: source_filename

// CHECK-SINGLE-FILE-SECOND: source_filename = "<swift-imported-modules>"
// CHECK-SINGLE-FILE-SECOND: @"_swift_FORCE_LOAD_$_TEST"
// CHECK-SINGLE-FILE-SECOND-NOT: source_filename

