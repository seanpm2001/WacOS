// RUN: %empty-directory(%t)
//
// Test will build a module TestModule that depends on OtherModule and LeafModule (built from other.swift and leaf.swift).
//
// RUN: echo 'public func LeafFunc() -> Int { return 10; }' >%t/leaf.swift
//
// RUN: echo 'import LeafModule' >%t/other.swift
// RUN: echo 'public func OtherFunc() -> Int { return LeafFunc(); }' >>%t/other.swift
//
// Phase 1: build LeafModule into a .swiftinterface file:
//
// RUN: %target-swift-frontend -I %t -emit-module-interface-path %t/LeafModule.swiftinterface -module-name LeafModule %t/leaf.swift -emit-module -o /dev/null
// RUN: test -f %t/LeafModule.swiftinterface
// RUN: %FileCheck %s -check-prefix=CHECK-LEAFINTERFACE <%t/LeafModule.swiftinterface
// CHECK-LEAFINTERFACE: LeafFunc
//
//
// Phase 2: build OtherModule into a .swiftinterface _using_ LeafModule via LeafModule.swiftinterface, creating LeafModule-*.swiftmodule along the way.
//
// RUN: %target-swift-frontend -I %t -module-cache-path %t/modulecache -emit-module-interface-path %t/OtherModule.swiftinterface -module-name OtherModule %t/other.swift -emit-module -o /dev/null
// RUN: test -f %t/OtherModule.swiftinterface
// RUN: %FileCheck %s -check-prefix=CHECK-OTHERINTERFACE <%t/OtherModule.swiftinterface
// CHECK-OTHERINTERFACE: OtherFunc
// RUN: test -f %t/modulecache/LeafModule-*.swiftmodule
// RUN: llvm-bcanalyzer -dump %t/modulecache/LeafModule-*.swiftmodule | %FileCheck %s -check-prefix=CHECK-LEAFMODULE
// CHECK-LEAFMODULE: {{MODULE_NAME.*blob data = 'LeafModule'}}
// CHECK-LEAFMODULE: {{FILE_DEPENDENCY.*LeafModule.swiftinterface'}}
// CHECK-LEAFMODULE: FUNC_DECL
// RUN: llvm-bcanalyzer -dump %t/modulecache/LeafModule-*.swiftmodule | %FileCheck %s -check-prefix=CHECK-LEAFMODULE-NEGATIVE
// CHECK-LEAFMODULE-NEGATIVE-NOT: {{FILE_DEPENDENCY.*Swift.swiftmodule([/\\].+[.]swiftmodule)?'}}
//
//
// Phase 3: build TestModule into a .swiftmodule explicitly us OtherModule via OtherModule.swiftinterface, creating OtherModule-*.swiftmodule along the way.
//
// RUN: %target-swift-frontend -I %t -module-cache-path %t/modulecache -emit-module -emit-dependencies -emit-dependencies-path %t/TestModule.d -o %t/TestModule.swiftmodule -module-name TestModule %s
// RUN: test -f %t/TestModule.swiftmodule
// RUN: test -f %t/modulecache/OtherModule-*.swiftmodule
// RUN: test -f %t/TestModule.d
// RUN: llvm-bcanalyzer -dump %t/modulecache/OtherModule-*.swiftmodule | %FileCheck %s -check-prefix=CHECK-OTHERMODULE
// CHECK-OTHERMODULE: {{MODULE_NAME.*blob data = 'OtherModule'}}
// CHECK-OTHERMODULE-NOT: {{FILE_DEPENDENCY.*Swift.swiftmodule([/\\].+[.]swiftmodule)?'}}
// CHECK-OTHERMODULE: {{FILE_DEPENDENCY.*LeafModule.swiftinterface'}}
// CHECK-OTHERMODULE: {{FILE_DEPENDENCY.*OtherModule.swiftinterface'}}
// CHECK-OTHERMODULE: FUNC_DECL
//
// Quirk: because the cached .swiftmodules have a hash name component that
// integrates target, and we sort the contents of lines in a .d file by the
// dependency's reverse-name (for reasons), the order in which the cached
// .swiftmodules are listed in the .d file will vary _by target_.
//
// So we cannot write a single set of CHECK-SAME lines here that will work
// for all targets: some will have LeafModule first, some OtherModule
// first. So instead we just use CHECK-DAG.
//
// RUN: %FileCheck %s -check-prefix=CHECK-DEPENDS <%t/TestModule.d
//
// CHECK-DEPENDS: TestModule.swiftmodule :
// CHECK-DEPENDS-DAG: LeafModule.swiftinterface
// CHECK-DEPENDS-DAG: OtherModule.swiftinterface
// CHECK-DEPENDS-DAG: Swift.swiftmodule
// CHECK-DEPENDS-DAG: SwiftOnoneSupport.swiftmodule
//
// RUN: %FileCheck %s -check-prefix=CHECK-DEPENDS-NEGATIVE <%t/TestModule.d
//
// CHECK-DEPENDS-NEGATIVE-NOT: {{LeafModule-[^ ]+.swiftmodule}}
// CHECK-DEPENDS-NEGATIVE-NOT: {{OtherModule-[^ ]+.swiftmodule}}

import OtherModule

public func TestFunc() {
    print(OtherFunc())
}
