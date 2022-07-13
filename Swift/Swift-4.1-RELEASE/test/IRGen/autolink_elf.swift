// RUN: %empty-directory(%t)
// RUN: %swift -target x86_64-unknown-linux-gnu -emit-module -parse-stdlib -o %t -module-name Empty -module-link-name swiftEmpty %S/../Inputs/empty.swift
// RUN: %swift -target x86_64-unknown-linux-gnu %s -I %t -parse-stdlib -disable-objc-interop -module-name main -emit-ir -o - | %FileCheck %s

// REQUIRES: CODEGENERATOR=X86

import Empty

// Check that on ELF targets autolinking information is emitted and marked
// as used.

// CHECK-DAG: @_swift1_autolink_entries = private constant [13 x i8] c"-lswiftEmpty\00", section ".swift1_autolink_entries", align 8
// CHECK-DAG: @llvm.used = appending global [{{.*}} x i8*] [{{.*}}i8* getelementptr inbounds ([13 x i8], [13 x i8]* @_swift1_autolink_entries, i32 0, i32 0){{.*}}], section "llvm.metadata", align 8

