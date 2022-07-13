// RUN: %empty-directory(%t)
// RUN: %target-swiftc_driver -emit-module -o %t -module-name Empty -module-link-name swiftEmpty %S/../Inputs/empty.swift
// RUN: %target-swiftc_driver %s -I %t -emit-ir | %FileCheck %s

// REQUIRES: objc_interop

import Empty

// Check that libobjc is always autolinked together with libswiftCore on
// platforms that support Objective-C.

// CHECK: !llvm.linker.options =
// CHECK-DAG: !{{.*}} = !{!"-lswiftCore"}
// CHECK-DAG: !{{.*}} = !{!"-lobjc"}
