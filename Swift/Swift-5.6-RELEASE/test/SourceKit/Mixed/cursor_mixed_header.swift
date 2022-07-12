
func test(_ b : BaseInHead) {
  b.doIt(0);
}

// REQUIRES: objc_interop
// RUN: %swift -typecheck %s -module-name Mixed -import-objc-header %S/Inputs/header.h 2> %t.diags
// RUN: %FileCheck -input-file %t.diags %s -check-prefix=DIAG
// DIAG: warning: using the result of an assignment

// RUN: %sourcekitd-test -req=cursor -pos=3:7 %s -- %s -module-name Mixed -import-objc-header %S/Inputs/header.h | %FileCheck %s

// RUN: %empty-directory(%t)
// RUN: %sourcekitd-test -req=cursor -pos=3:7 %s -- %s -module-name Mixed -pch-output-dir %t -import-objc-header %S/Inputs/header.h | %FileCheck %s
// RUN: stat %t/*.pch

// CHECK: source.lang.swift.ref.function.method.instance ({{.*}}Inputs/header.h:4:9-4:23)
// CHECK: doIt(_:)
// CHECK: c:objc(cs)BaseInHead(im)doIt:
// CHECK: (BaseInHead) -> (Int32) -> ()
// CHECK: <Declaration>func doIt(_ arg: <Type usr="s:s5Int32V">Int32</Type>)</Declaration>
