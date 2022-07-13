// RUN: rm -rf %t
// RUN: mkdir -p %t

// RUN: %swift -target x86_64--windows-gnu -parse-as-library -parse-stdlib -emit-module-path %t/module.swiftmodule -module-name module -module-link-name module %s
// RUN: %swift -target x86_64--windows-gnu -parse-as-library -parse-stdlib -module-name autolink -I %t -D MAIN_MODULE -emit-ir -o - %s | %FileCheck %s -check-prefix CHECK-GNU-IR
// RUN: %swift -target x86_64--windows-gnu -parse-as-library -parse-stdlib -module-name autolink -I %t -D MAIN_MODULE -S -o - %s | %FileCheck %s -check-prefix CHECK-GNU-ASM

// REQUIRES: CODEGENERATOR=X86

#if MAIN_MODULE
import module
#endif

// CHECK-GNU-IR: @_swift1_autolink_entries = private constant [9 x i8] c"-lmodule\00", section ".swift1_autolink_entries", align 8
// CHECK-GNU-IR: @llvm.used = appending global [{{.*}} x i8*] [{{.*}}i8* getelementptr inbounds ([9 x i8], [9 x i8]* @_swift1_autolink_entries, i32 0, i32 0){{.*}}], section "llvm.metadata", align 8

// CHECK-GNU-ASM: .section	.swift1_autolink_entries{{.*}}
// CHECK-GNU-ASM: .asciz  "-lmodule"
