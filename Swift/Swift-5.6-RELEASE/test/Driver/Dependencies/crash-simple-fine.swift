/// crash ==> main | crash --> other

// RUN: %empty-directory(%t)
// RUN: cp -r %S/Inputs/crash-simple-fine/* %t
// RUN: touch -t 201401240005 %t/*

// RUN: cd %t && %swiftc_driver -c -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./crash.swift ./other.swift -module-name main -j1 -v 2>&1 | %FileCheck -check-prefix=CHECK-FIRST %s

// CHECK-FIRST-NOT: warning
// CHECK-FIRST: Handled main.swift
// CHECK-FIRST: Handled crash.swift
// CHECK-FIRST: Handled other.swift

// RUN: touch -t 201401240006 %t/crash.swift
// RUN: cd %t && not %swiftc_driver -c -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies-bad.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./crash.swift  ./main.swift ./other.swift -module-name main -j1 -v 2>&1 |  %FileCheck -check-prefix=CHECK-SECOND %s

// CHECK-SECOND: Handled crash.swift
// CHECK-SECOND-NOT: Handled main.swift
// CHECK-SECOND-NOT: Handled other.swift

// RUN: %FileCheck -check-prefix=CHECK-RECORD %s < %t/main~buildrecord.swiftdeps

// CHECK-RECORD-DAG: "{{(./)?}}crash.swift": !private [
// CHECK-RECORD-DAG: "{{(./)?}}main.swift": !private [
// CHECK-RECORD-DAG: "{{(./)?}}other.swift": !private [

// RUN: cd %t &&  %swiftc_driver -c -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./crash.swift  ./main.swift ./other.swift -module-name main -j1 -v 2>&1 | %FileCheck -check-prefix=CHECK-THIRD %s

// CHECK-THIRD-DAG: Handled main.swift
// CHECK-THIRD-DAG: Handled crash.swift
// CHECK-THIRD-DAG: Handled other.swift
