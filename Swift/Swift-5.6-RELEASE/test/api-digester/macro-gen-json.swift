// RUN: %empty-directory(%t.mod)
// RUN: %empty-directory(%t.sdk)
// RUN: %empty-directory(%t.module-cache)
// RUN: %target-swift-frontend -disable-objc-attr-requires-foundation-module -emit-module -o %t.mod/macrogenleft.swiftmodule %S/Inputs/macro-gen-left.swift -parse-as-library
// RUN: %target-swift-frontend -disable-objc-attr-requires-foundation-module -emit-module -o %t.mod/macrogenright.swiftmodule %S/Inputs/macro-gen-right.swift -parse-as-library
// RUN: %api-digester -dump-sdk -module macrogenleft -o %t.dump1.json -module-cache-path %t.module-cache -sdk %t.sdk -I %t.mod -migrator
// RUN: %api-digester -dump-sdk -module macrogenright -o %t.dump2.json -module-cache-path %t.module-cache -sdk %t.sdk -I %t.mod -migrator
// RUN: %api-digester -generate-migration-script --input-paths %t.dump1.json -input-paths %t.dump2.json -o %t.result -json
// RUN: diff -u %S/Outputs/macro-gen.json %t.result
