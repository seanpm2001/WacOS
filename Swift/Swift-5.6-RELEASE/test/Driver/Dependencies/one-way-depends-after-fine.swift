/// other | main
/// other ==>+ main

// RUN: %empty-directory(%t)
// RUN: cp -r %S/Inputs/one-way-depends-after-fine/* %t
// RUN: touch -t 201401240005 %t/*.swift

// Generate the build record...
// RUN: cd %t && %swiftc_driver -c -disable-incremental-imports -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./other.swift -module-name main -j1 -v

// ...then reset the .swiftdeps files.
// RUN: cp -r %S/Inputs/one-way-depends-after-fine/*.swiftdeps %t

// RUN: cd %t && %swiftc_driver -c -disable-incremental-imports -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./other.swift -module-name main -j1 -v 2>&1 | %FileCheck -check-prefix=CHECK-FIRST %s

// CHECK-FIRST-NOT: warning
// CHECK-FIRST-NOT: Handled

// RUN: touch -t 201401240006 %t/other.swift
// RUN: cd %t && %swiftc_driver -c -disable-incremental-imports -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./other.swift -module-name main -j1 -v 2>&1 | %FileCheck -check-prefix=CHECK-SECOND %s

// CHECK-SECOND-NOT: Handled main.swift
// CHECK-SECOND: Handled other.swift
// CHECK-SECOND-NOT: Handled main.swift

// RUN: touch -t 201401240007 %t/other.swift
// RUN: cd %t && %swiftc_driver -c -disable-incremental-imports -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./other.swift -module-name main -j1 -v 2>&1 | %FileCheck -check-prefix=CHECK-SECOND %s


// RUN: %empty-directory(%t)
// RUN: cp -r %S/Inputs/one-way-depends-after-fine/* %t
// RUN: touch -t 201401240005 %t/*.swift

// Generate the build record...
// RUN: cd %t && %swiftc_driver -c -disable-incremental-imports -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./other.swift -module-name main -j1 -v

// ...then reset the .swiftdeps files.
// RUN: cp -r %S/Inputs/one-way-depends-after-fine/*.swiftdeps %t

// RUN: cd %t && %swiftc_driver -c -disable-incremental-imports -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./other.swift -module-name main -j1 -v 2>&1 | %FileCheck -check-prefix=CHECK-FIRST %s

// RUN: touch -t 201401240006 %t/main.swift
// RUN: cd %t && %swiftc_driver -c -disable-incremental-imports -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./other.swift -module-name main -j1 -v 2>&1 | %FileCheck -check-prefix=CHECK-THIRD %s

// CHECK-THIRD-NOT: Handled other.swift
// CHECK-THIRD: Handled main.swift
// CHECK-THIRD-NOT: Handled other.swift

// RUN: touch -t 201401240007 %t/main.swift
// RUN: cd %t && %swiftc_driver -c -disable-incremental-imports -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./other.swift -module-name main -j1 -v 2>&1 | %FileCheck -check-prefix=CHECK-THIRD %s

// RUN: touch -t 201401240008 %t/other.swift
// RUN: cd %t && %swiftc_driver -c -disable-incremental-imports -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -output-file-map %t/output.json -incremental -driver-always-rebuild-dependents ./main.swift ./other.swift -module-name main -j1 -v 2>&1 | %FileCheck -check-prefix=CHECK-FOURTH %s

// CHECK-FOURTH-DAG: Handled other.swift
// CHECK-FOURTH-DAG: Handled main.swift
