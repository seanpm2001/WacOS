// REQUIRES: objc_interop
// RUN: %empty-directory(%t.mod)
// RUN: %target-swift-frontend -emit-module -o %t.mod/Cities.swiftmodule %S/Inputs/Cities.swift -module-name Cities -parse-as-library
// RUN: %empty-directory(%t) && %target-swift-frontend -c -update-code -disable-migrator-fixits -primary-file %s  -I %t.mod -api-diff-data-file %S/Inputs/API.json -emit-migrated-file-path %t/wrap_optional.swift.result -o /dev/null
// RUN: diff -u %S/wrap_optional.swift.expected %t/wrap_optional.swift.result

import Cities

class MyCities : Cities {
  override init(x: Int) { super.init(x: x) }
  override init!(y: Int) { super.init(y: y) }
  override func mooloolaba(x: Cities, y: Cities?) {}
  override func toowoomba(x: [Cities], y: [Cities]?) {}
  override func mareeba(x: [String : Cities?], y: [String : Cities]?) {}
  override func maroochy(x: (Int)?, y: Int?) {}
}

class MySubCities : MyCities {}

class MySubSubCities : MySubCities {
  override func yandina(x: [[String : Cities]]!) {}
  override func buderim() -> Cities? { return nil }
  override func noosa() -> [[String : Cities]?] { return [] }
}

typealias IntPair = (Int, Int)

extension ExtraCities {
  func coolum(x: [String : [Int : [(((String))?)]]]) {}
  func currimundi(x: (Int, IntPair)!) {}
}

class MyExtraCities : ExtraCities {
  func blibli(x: (String?, String) -> String?) {}
  func currimundi(x: (Int, (Int, Int))!) {}
}

typealias IntAnd<T> = (Int, T)
class Outer {
  typealias Inner = (String?, String) -> String?
}

class MyExtraCitiesWithAliases : ExtraCities {
  func blibli(x: Outer.Inner) {}
  func currimundi(x: (Int, IntAnd<Int>)!) {}
}

typealias OptString = String?
typealias OptGeneric<T> = T?

class MyExtraCitiesWithMoreAliases : ExtraCities {
  func blibli(x: (OptString, String) -> String?) {}
  func currimundi(x: OptGeneric<(Int, (Int, Int))>) {}
}
