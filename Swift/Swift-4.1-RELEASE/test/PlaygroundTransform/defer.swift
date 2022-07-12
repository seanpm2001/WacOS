// RUN: %empty-directory(%t)
// RUN: cp %s %t/main.swift
// RUN: %target-build-swift -Xfrontend -playground -Xfrontend -debugger-support -o %t/main %S/Inputs/PlaygroundsRuntime.swift %t/main.swift
// RUN: %target-run %t/main | %FileCheck %s
// RUN: %target-build-swift -Xfrontend -pc-macro -Xfrontend -playground -Xfrontend -debugger-support -o %t/main %S/Inputs/PlaygroundsRuntime.swift %S/Inputs/SilentPCMacroRuntime.swift %t/main.swift
// RUN: %target-run %t/main | %FileCheck %s
// REQUIRES: executable_test
func foo() {
	defer {
		2
	}
	1
}
foo()
// CHECK: {{.*}} $builtin_log_scope_entry
// CHECK-NEXT: {{.*}} $builtin_log[='1']
// CHECK-NEXT: {{.*}} $builtin_log_scope_exit
// CHECK-NEXT: {{.*}} $builtin_log_scope_entry
// CHECK-NEXT: {{.*}} $builtin_log[='2']
// CHECK-NEXT: {{.*}} $builtin_log_scope_exit
