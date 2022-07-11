// REQUIRES: shell
// Test that when:
//
// 1. Using -incremental -v -driver-show-incremental, and...
// 2. ...the build record file does not contain valid JSON...
//
// ...then the driver prints a message indicating that incremental compilation
// is enabled.


// RUN: %empty-directory(%t)
// RUN: cp -r %S/Inputs/one-way-with-swiftdeps-fine/* %t
// RUN: %{python} %S/Inputs/touch.py 443865900 %t/*

// RUN: echo '{version: "'$(%swiftc_driver_plain -version | head -n1)'", inputs: {"./main.swift": [443865900, 0], "./other.swift": [443865900, 0]}}' > %t/main~buildrecord.swiftdeps
// RUN: cd %t && %swiftc_driver -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -c ./main.swift ./other.swift -module-name main -incremental -v -driver-show-incremental -output-file-map %t/output.json 2>&1 | %FileCheck --check-prefix CHECK-INCREMENTAL %s
// CHECK-INCREMENTAL-NOT: Incremental compilation has been enabled
// CHECK-INCREMENTAL: Queuing (initial): {compile: main.o <= main.swift}

// RUN: rm %t/main~buildrecord.swiftdeps && touch %t/main~buildrecord.swiftdeps
// RUN: cd %t && %swiftc_driver -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -g -c ./main.swift ./other.swift -module-name main -incremental -v -driver-show-incremental -output-file-map %t/output.json 2>&1 | %FileCheck --check-prefix CHECK-MALFORMED %s

// RUN: echo 'foo' > %t/main~buildrecord.swiftdeps
// RUN: cd %t && %swiftc_driver -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -g -c ./main.swift ./other.swift -module-name main -incremental -v -driver-show-incremental -output-file-map %t/output.json 2>&1 | %FileCheck --check-prefix CHECK-MALFORMED %s

// CHECK-MALFORMED: Incremental compilation has been disabled{{.*}}malformed build record file
// CHECK-MALFORMED-NOT: Queuing (initial): {compile: main.o <= main.swift}

// RUN: echo '{version, inputs: {"./main.swift": [443865900, 0], "./other.swift": [443865900, 0]}}' > %t/main~buildrecord.swiftdeps
// RUN: cd %t && %swiftc_driver -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -g -c ./main.swift ./other.swift -module-name main -incremental -v -driver-show-incremental -output-file-map %t/output.json 2>&1 | %FileCheck --check-prefix CHECK-MISSING-KEY %s

// RUN: echo '{version: "'$(%swiftc_driver_plain -version | head -n1)'", inputs}' > %t/main~buildrecord.swiftdeps
// RUN: cd %t && %swiftc_driver -driver-use-frontend-path "%{python.unquoted};%S/Inputs/update-dependencies.py;%swift-dependency-tool" -g -c ./main.swift ./other.swift -module-name main -incremental -v -driver-show-incremental -output-file-map %t/output.json 2>&1 | %FileCheck --check-prefix CHECK-MISSING-KEY %s

// CHECK-MISSING-KEY: Incremental compilation has been disabled{{.*}}malformed build record file{{.*}}Malformed value for key
// CHECK-MISSING-KEY-NOT: Queuing (initial): {compile: main.o <= main.swift}
