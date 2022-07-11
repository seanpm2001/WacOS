/// other ==> main

// RUN: %empty-directory(%t)
// RUN: cp -r %S/Inputs/one-way-fine/* %t
// RUN: touch -t 201401240005 %t/*

// RUN: cd %t && not %swiftc_driver -c -driver-use-frontend-path "%{python.unquoted};%S/Inputs/modify-non-primary-files.py" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./other.swift -module-name main -j1 -v 2>&1 | %FileCheck %s

// CHECK: Handled main.swift
// CHECK: Handled other.swift
// CHECK: error: input file 'other.swift' was modified during the build

// RUN: cd %t && %swiftc_driver -c -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./other.swift -module-name main -j1 -v 2>&1 | %FileCheck -check-prefix=CHECK-RECOVER %s

// CHECK-RECOVER: Handled main.swift
// CHECK-RECOVER: Handled other.swift


// RUN: touch -t 201401240005 %t/*
// RUN: cd %t && not %swiftc_driver -c -driver-use-frontend-path "%{python.unquoted};%S/Inputs/modify-non-primary-files.py" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./other.swift ./main.swift -module-name main -j1 -v 2>&1 | %FileCheck -check-prefix=CHECK-REVERSED %s

// CHECK-REVERSED: Handled other.swift
// CHECK-REVERSED: Handled main.swift
// CHECK-REVERSED: error: input file 'main.swift' was modified during the build

// RUN: cd %t && %swiftc_driver -c -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./other.swift -module-name main -j1 -v 2>&1 | %FileCheck -check-prefix=CHECK-REVERSED-RECOVER %s

// CHECK-REVERSED-RECOVER: Handled main.swift
