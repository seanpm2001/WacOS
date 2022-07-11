// RUN: %empty-directory(%t)
// RUN: mkdir -p %t/clang-module-cache
// RUN: mkdir -p %t/inputs
// RUN: mkdir -p %t/barinputs
// RUN: echo "public func foo() {}" >> %t/foo.swift
// RUN: %target-swift-frontend -emit-module -emit-module-path %t/inputs/Foo.swiftmodule -emit-module-doc-path %t/inputs/Foo.swiftdoc -emit-module-source-info -emit-module-source-info-path %t/inputs/Foo.swiftsourceinfo -module-cache-path %t.module-cache %t/foo.swift -module-name Foo
// RUN: echo "public func bar() {}" >> %t/bar.swift
// RUN: %target-swift-frontend -emit-module -emit-module-path %t/barinputs/Bar.swiftmodule -emit-module-doc-path %t/barinputs/Bar.swiftdoc -emit-module-source-info -emit-module-source-info-path %t/barinputs/Bar.swiftsourceinfo -module-cache-path %t.module-cache %t/bar.swift -module-name Bar

// RUN: echo "[{" > %/t/inputs/map.json
// RUN: echo "\"moduleName\": \"Foo\"," >> %/t/inputs/map.json
// RUN: echo "\"modulePath\": \"%/t/inputs/Foo.swiftmodule\"," >> %/t/inputs/map.json
// RUN: echo "\"docPath\": \"%/t/inputs/Foo.swiftdoc\"," >> %/t/inputs/map.json
// RUN: echo "\"sourceInfoPath\": \"%/t/inputs/Foo.swiftsourceinfo\"," >> %/t/inputs/map.json
// RUN: echo "\"isFramework\": false" >> %/t/inputs/map.json
// RUN: echo "}]" >> %/t/inputs/map.json

// RUN: not %target-swift-frontend -typecheck %s -explicit-swift-module-map-file %t/inputs/map.json -disable-implicit-swift-modules
// RUN: %target-swift-frontend -typecheck %s -explicit-swift-module-map-file %t/inputs/map.json -I %t/barinputs
import Foo
import Bar
