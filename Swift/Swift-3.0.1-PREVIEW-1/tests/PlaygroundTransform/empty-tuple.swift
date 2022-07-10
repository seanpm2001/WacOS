// RUN: rm -rf %t && mkdir %t
// RUN: cp %s %t/main.swift
// RUN: %target-build-swift -Xfrontend -playground -Xfrontend -debugger-support -o %t/main %S/Inputs/PlaygroundsRuntime.swift %t/main.swift
// RUN: %target-run %t/main | %FileCheck %s
// REQUIRES: executable_test
func foo() { }
foo()
1+2
// CHECK: {{.*}} $builtin_log[='3']
