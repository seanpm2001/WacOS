// REQUIRES: objc_interop
// RUN: %empty-directory(%t.mod)
// RUN: %target-swift-frontend -emit-module -o %t.mod/Cities.swiftmodule %S/Inputs/Cities.swift -module-name Cities -parse-as-library
// RUN: %empty-directory(%t) && %target-swift-frontend -c -update-code -primary-file %s  -I %t.mod -api-diff-data-file %S/Inputs/string-representable.json -emit-migrated-file-path %t/avoid_insert_existing_functions.swift.result -disable-migrator-fixits -o /dev/null
// RUN: diff -u %S/avoid_insert_existing_functions.swift.expected %t/avoid_insert_existing_functions.swift.result

import Cities

typealias NewAttribute = String

func foo(_ c: Container) -> String {
  c.Value = ""
  return c.Value
}

fileprivate func convertToNewAttribute(_ input: String) -> NewAttribute {
 return ""
}

fileprivate func convertFromNewAttribute(_ input: NewAttribute) -> String {
 return ""
}
