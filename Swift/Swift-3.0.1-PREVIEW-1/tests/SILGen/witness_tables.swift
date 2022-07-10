// RUN: %target-swift-frontend -emit-silgen -I %S/Inputs -enable-source-import %s -disable-objc-attr-requires-foundation-module > %t.sil
// RUN: %FileCheck -check-prefix=TABLE -check-prefix=TABLE-ALL %s < %t.sil
// RUN: %FileCheck -check-prefix=SYMBOL %s < %t.sil

// RUN: %target-swift-frontend -emit-silgen -I %S/Inputs -enable-source-import %s -disable-objc-attr-requires-foundation-module -enable-testing > %t.testable.sil
// RUN: %FileCheck -check-prefix=TABLE-TESTABLE -check-prefix=TABLE-ALL %s < %t.testable.sil
// RUN: %FileCheck -check-prefix=SYMBOL-TESTABLE %s < %t.testable.sil

import witness_tables_b

struct Arg {}

@objc class ObjCClass {}

infix operator <~> {}

protocol AssocReqt {
  func requiredMethod()
}

protocol ArchetypeReqt {
  func requiredMethod()
}

protocol AnyProtocol {
  associatedtype AssocType
  associatedtype AssocWithReqt: AssocReqt

  func method(x x: Arg, y: Self)
  func generic<A: ArchetypeReqt>(x x: A, y: Self)

  func assocTypesMethod(x x: AssocType, y: AssocWithReqt)

  static func staticMethod(x x: Self)

  func <~>(x: Self, y: Self)
}

protocol ClassProtocol : class {
  associatedtype AssocType
  associatedtype AssocWithReqt: AssocReqt

  func method(x x: Arg, y: Self)
  func generic<B: ArchetypeReqt>(x x: B, y: Self)

  func assocTypesMethod(x x: AssocType, y: AssocWithReqt)

  static func staticMethod(x x: Self)

  func <~>(x: Self, y: Self)
}

@objc protocol ObjCProtocol {
  func method(x x: ObjCClass)
  static func staticMethod(y y: ObjCClass)
}

class SomeAssoc {}

struct ConformingAssoc : AssocReqt {
  func requiredMethod() {}
}
// TABLE-LABEL: sil_witness_table hidden ConformingAssoc: AssocReqt module witness_tables {
// TABLE-TESTABLE-LABEL: sil_witness_table [fragile] ConformingAssoc: AssocReqt module witness_tables {
// TABLE-ALL-NEXT:    method #AssocReqt.requiredMethod!1: @_TTWV14witness_tables15ConformingAssocS_9AssocReqtS_FS1_14requiredMethod{{.*}}
// TABLE-ALL-NEXT:  }
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables15ConformingAssocS_9AssocReqtS_FS1_14requiredMethod{{.*}} : $@convention(witness_method) (@in_guaranteed ConformingAssoc) -> ()
// SYMBOL-TESTABLE:      sil [transparent] [thunk] @_TTWV14witness_tables15ConformingAssocS_9AssocReqtS_FS1_14requiredMethod{{.*}} : $@convention(witness_method) (@in_guaranteed ConformingAssoc) -> ()

struct ConformingStruct : AnyProtocol {
  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = ConformingAssoc
  
  func method(x x: Arg, y: ConformingStruct) {}
  func generic<D: ArchetypeReqt>(x x: D, y: ConformingStruct) {}

  func assocTypesMethod(x x: SomeAssoc, y: ConformingAssoc) {}

  static func staticMethod(x x: ConformingStruct) {}
}
func <~>(x: ConformingStruct, y: ConformingStruct) {}
// TABLE-LABEL: sil_witness_table hidden ConformingStruct: AnyProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: ConformingAssoc
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): ConformingAssoc: AssocReqt module witness_tables
// TABLE-NEXT:    method #AnyProtocol.method!1: @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_FS1_6method{{.*}}
// TABLE-NEXT:    method #AnyProtocol.generic!1: @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_FS1_7generic{{.*}}
// TABLE-NEXT:    method #AnyProtocol.assocTypesMethod!1: @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol.staticMethod!1: @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_ZFS1_12staticMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol."<~>"!1: @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_ZFS1_oi3ltg{{.*}}
// TABLE-NEXT:  }
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_FS1_6method{{.*}} : $@convention(witness_method) (Arg, @in ConformingStruct, @in_guaranteed ConformingStruct) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_FS1_7generic{{.*}}: ArchetypeReqt> (@in A, @in ConformingStruct, @in_guaranteed ConformingStruct) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}} : $@convention(witness_method) (@in SomeAssoc, @in ConformingAssoc, @in_guaranteed ConformingStruct) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_ZFS1_12staticMethod{{.*}} : $@convention(witness_method) (@in ConformingStruct, @thick ConformingStruct.Type) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_ZFS1_oi3ltg{{.*}} : $@convention(witness_method) (@in ConformingStruct, @in ConformingStruct, @thick ConformingStruct.Type) -> ()
// SYMBOL-TESTABLE:      sil [transparent] [thunk] @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_FS1_6method{{.*}} : $@convention(witness_method) (Arg, @in ConformingStruct, @in_guaranteed ConformingStruct) -> ()
// SYMBOL-TESTABLE:      sil [transparent] [thunk] @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_FS1_7generic{{.*}} : $@convention(witness_method) <A where A : ArchetypeReqt> (@in A, @in ConformingStruct, @in_guaranteed ConformingStruct) -> ()
// SYMBOL-TESTABLE:      sil [transparent] [thunk] @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}} : $@convention(witness_method) (@in SomeAssoc, @in ConformingAssoc, @in_guaranteed ConformingStruct) -> ()
// SYMBOL-TESTABLE:      sil [transparent] [thunk] @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_ZFS1_12staticMethod{{.*}} : $@convention(witness_method) (@in ConformingStruct, @thick ConformingStruct.Type) -> ()
// SYMBOL-TESTABLE:      sil [transparent] [thunk] @_TTWV14witness_tables16ConformingStructS_11AnyProtocolS_ZFS1_oi3ltg{{.*}} : $@convention(witness_method) (@in ConformingStruct, @in ConformingStruct, @thick ConformingStruct.Type) -> ()

protocol AddressOnly {}

struct ConformingAddressOnlyStruct : AnyProtocol {
  var p: AddressOnly // force address-only layout with a protocol-type field

  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = ConformingAssoc
  
  func method(x x: Arg, y: ConformingAddressOnlyStruct) {}
  func generic<E: ArchetypeReqt>(x x: E, y: ConformingAddressOnlyStruct) {}

  func assocTypesMethod(x x: SomeAssoc, y: ConformingAssoc) {}

  static func staticMethod(x x: ConformingAddressOnlyStruct) {}
}
func <~>(x: ConformingAddressOnlyStruct, y: ConformingAddressOnlyStruct) {}
// TABLE-LABEL: sil_witness_table hidden ConformingAddressOnlyStruct: AnyProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: ConformingAssoc
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): ConformingAssoc: AssocReqt module witness_tables
// TABLE-NEXT:    method #AnyProtocol.method!1: @_TTWV14witness_tables27ConformingAddressOnlyStructS_11AnyProtocolS_FS1_6method{{.*}}
// TABLE-NEXT:    method #AnyProtocol.generic!1: @_TTWV14witness_tables27ConformingAddressOnlyStructS_11AnyProtocolS_FS1_7generic{{.*}}
// TABLE-NEXT:    method #AnyProtocol.assocTypesMethod!1: @_TTWV14witness_tables27ConformingAddressOnlyStructS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol.staticMethod!1: @_TTWV14witness_tables27ConformingAddressOnlyStructS_11AnyProtocolS_ZFS1_12staticMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol."<~>"!1: @_TTWV14witness_tables27ConformingAddressOnlyStructS_11AnyProtocolS_ZFS1_oi3ltg{{.*}}
// TABLE-NEXT:  }
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables27ConformingAddressOnlyStructS_11AnyProtocolS_FS1_6method{{.*}} : $@convention(witness_method) (Arg, @in ConformingAddressOnlyStruct, @in_guaranteed ConformingAddressOnlyStruct) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables27ConformingAddressOnlyStructS_11AnyProtocolS_FS1_7generic{{.*}} : $@convention(witness_method) <A where A : ArchetypeReqt> (@in A, @in ConformingAddressOnlyStruct, @in_guaranteed ConformingAddressOnlyStruct) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables27ConformingAddressOnlyStructS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}} : $@convention(witness_method) (@in SomeAssoc, @in ConformingAssoc, @in_guaranteed ConformingAddressOnlyStruct) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables27ConformingAddressOnlyStructS_11AnyProtocolS_ZFS1_12staticMethod{{.*}} : $@convention(witness_method) (@in ConformingAddressOnlyStruct, @thick ConformingAddressOnlyStruct.Type) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables27ConformingAddressOnlyStructS_11AnyProtocolS_ZFS1_oi3ltg{{.*}} : $@convention(witness_method) (@in ConformingAddressOnlyStruct, @in ConformingAddressOnlyStruct, @thick ConformingAddressOnlyStruct.Type) -> ()

class ConformingClass : AnyProtocol {
  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = ConformingAssoc
  
  func method(x x: Arg, y: ConformingClass) {}
  func generic<F: ArchetypeReqt>(x x: F, y: ConformingClass) {}

  func assocTypesMethod(x x: SomeAssoc, y: ConformingAssoc) {}

  class func staticMethod(x x: ConformingClass) {}
}
func <~>(x: ConformingClass, y: ConformingClass) {}
// TABLE-LABEL: sil_witness_table hidden ConformingClass: AnyProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: ConformingAssoc
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): ConformingAssoc: AssocReqt module witness_tables
// TABLE-NEXT:    method #AnyProtocol.method!1: @_TTWC14witness_tables15ConformingClassS_11AnyProtocolS_FS1_6method{{.*}}
// TABLE-NEXT:    method #AnyProtocol.generic!1: @_TTWC14witness_tables15ConformingClassS_11AnyProtocolS_FS1_7generic{{.*}}
// TABLE-NEXT:    method #AnyProtocol.assocTypesMethod!1: @_TTWC14witness_tables15ConformingClassS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol.staticMethod!1: @_TTWC14witness_tables15ConformingClassS_11AnyProtocolS_ZFS1_12staticMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol."<~>"!1: @_TTWC14witness_tables15ConformingClassS_11AnyProtocolS_ZFS1_oi3ltg{{.*}}
// TABLE-NEXT:  }
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWC14witness_tables15ConformingClassS_11AnyProtocolS_FS1_6method{{.*}} : $@convention(witness_method) (Arg, @in ConformingClass, @in_guaranteed ConformingClass) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWC14witness_tables15ConformingClassS_11AnyProtocolS_FS1_7generic{{.*}} : $@convention(witness_method) <A where A : ArchetypeReqt> (@in A, @in ConformingClass, @in_guaranteed ConformingClass) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWC14witness_tables15ConformingClassS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}} : $@convention(witness_method) (@in SomeAssoc, @in ConformingAssoc, @in_guaranteed ConformingClass) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWC14witness_tables15ConformingClassS_11AnyProtocolS_ZFS1_12staticMethod{{.*}} : $@convention(witness_method) (@in ConformingClass, @thick ConformingClass.Type) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWC14witness_tables15ConformingClassS_11AnyProtocolS_ZFS1_oi3ltg{{.*}} : $@convention(witness_method) (@in ConformingClass, @in ConformingClass, @thick ConformingClass.Type) -> ()

struct ConformsByExtension {}
extension ConformsByExtension : AnyProtocol {
  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = ConformingAssoc
  
  func method(x x: Arg, y: ConformsByExtension) {}
  func generic<G: ArchetypeReqt>(x x: G, y: ConformsByExtension) {}

  func assocTypesMethod(x x: SomeAssoc, y: ConformingAssoc) {}

  static func staticMethod(x x: ConformsByExtension) {}
}
func <~>(x: ConformsByExtension, y: ConformsByExtension) {}
// TABLE-LABEL: sil_witness_table hidden ConformsByExtension: AnyProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: ConformingAssoc
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): ConformingAssoc: AssocReqt module witness_tables
// TABLE-NEXT:    method #AnyProtocol.method!1: @_TTWV14witness_tables19ConformsByExtensionS_11AnyProtocolS_FS1_6method{{.*}}
// TABLE-NEXT:    method #AnyProtocol.generic!1: @_TTWV14witness_tables19ConformsByExtensionS_11AnyProtocolS_FS1_7generic{{.*}}
// TABLE-NEXT:    method #AnyProtocol.assocTypesMethod!1: @_TTWV14witness_tables19ConformsByExtensionS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol.staticMethod!1: @_TTWV14witness_tables19ConformsByExtensionS_11AnyProtocolS_ZFS1_12staticMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol."<~>"!1: @_TTWV14witness_tables19ConformsByExtensionS_11AnyProtocolS_ZFS1_oi3ltg{{.*}}
// TABLE-NEXT:  }
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables19ConformsByExtensionS_11AnyProtocolS_FS1_6method{{.*}} : $@convention(witness_method) (Arg, @in ConformsByExtension, @in_guaranteed ConformsByExtension) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables19ConformsByExtensionS_11AnyProtocolS_FS1_7generic{{.*}} : $@convention(witness_method) <A where A : ArchetypeReqt> (@in A, @in ConformsByExtension, @in_guaranteed ConformsByExtension) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables19ConformsByExtensionS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}} : $@convention(witness_method) (@in SomeAssoc, @in ConformingAssoc, @in_guaranteed ConformsByExtension) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables19ConformsByExtensionS_11AnyProtocolS_ZFS1_12staticMethod{{.*}} : $@convention(witness_method) (@in ConformsByExtension, @thick ConformsByExtension.Type) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables19ConformsByExtensionS_11AnyProtocolS_ZFS1_oi3ltg{{.*}} : $@convention(witness_method) (@in ConformsByExtension, @in ConformsByExtension, @thick ConformsByExtension.Type) -> ()

extension OtherModuleStruct : AnyProtocol {
  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = ConformingAssoc
  
  func method(x x: Arg, y: OtherModuleStruct) {}
  func generic<H: ArchetypeReqt>(x x: H, y: OtherModuleStruct) {}

  func assocTypesMethod(x x: SomeAssoc, y: ConformingAssoc) {}

  static func staticMethod(x x: OtherModuleStruct) {}
}
func <~>(x: OtherModuleStruct, y: OtherModuleStruct) {}
// TABLE-LABEL: sil_witness_table hidden OtherModuleStruct: AnyProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: ConformingAssoc
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): ConformingAssoc: AssocReqt module witness_tables
// TABLE-NEXT:    method #AnyProtocol.method!1: @_TTWV16witness_tables_b17OtherModuleStruct14witness_tables11AnyProtocolS1_FS2_6method{{.*}}
// TABLE-NEXT:    method #AnyProtocol.generic!1: @_TTWV16witness_tables_b17OtherModuleStruct14witness_tables11AnyProtocolS1_FS2_7generic{{.*}}
// TABLE-NEXT:    method #AnyProtocol.assocTypesMethod!1: @_TTWV16witness_tables_b17OtherModuleStruct14witness_tables11AnyProtocolS1_FS2_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol.staticMethod!1: @_TTWV16witness_tables_b17OtherModuleStruct14witness_tables11AnyProtocolS1_ZFS2_12staticMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol."<~>"!1: @_TTWV16witness_tables_b17OtherModuleStruct14witness_tables11AnyProtocolS1_ZFS2_oi3ltg{{.*}}
// TABLE-NEXT:  }
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV16witness_tables_b17OtherModuleStruct14witness_tables11AnyProtocolS1_FS2_6method{{.*}} : $@convention(witness_method) (Arg, @in OtherModuleStruct, @in_guaranteed OtherModuleStruct) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV16witness_tables_b17OtherModuleStruct14witness_tables11AnyProtocolS1_FS2_7generic{{.*}} : $@convention(witness_method) <A where A : ArchetypeReqt> (@in A, @in OtherModuleStruct, @in_guaranteed OtherModuleStruct) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV16witness_tables_b17OtherModuleStruct14witness_tables11AnyProtocolS1_FS2_16assocTypesMethod{{.*}} : $@convention(witness_method) (@in SomeAssoc, @in ConformingAssoc, @in_guaranteed OtherModuleStruct) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV16witness_tables_b17OtherModuleStruct14witness_tables11AnyProtocolS1_ZFS2_12staticMethod{{.*}} : $@convention(witness_method) (@in OtherModuleStruct, @thick OtherModuleStruct.Type) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV16witness_tables_b17OtherModuleStruct14witness_tables11AnyProtocolS1_ZFS2_oi3ltg{{.*}} : $@convention(witness_method) (@in OtherModuleStruct, @in OtherModuleStruct, @thick OtherModuleStruct.Type) -> ()

protocol OtherProtocol {}

struct ConformsWithMoreGenericWitnesses : AnyProtocol, OtherProtocol {
  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = ConformingAssoc
  
  func method<I, J>(x x: I, y: J) {}
  func generic<K, L>(x x: K, y: L) {}

  func assocTypesMethod<M, N>(x x: M, y: N) {}

  static func staticMethod<O>(x x: O) {}
}
func <~> <P: OtherProtocol>(x: P, y: P) {}
// TABLE-LABEL: sil_witness_table hidden ConformsWithMoreGenericWitnesses: AnyProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: ConformingAssoc
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): ConformingAssoc: AssocReqt module witness_tables
// TABLE-NEXT:    method #AnyProtocol.method!1: @_TTWV14witness_tables32ConformsWithMoreGenericWitnessesS_11AnyProtocolS_FS1_6method{{.*}}
// TABLE-NEXT:    method #AnyProtocol.generic!1: @_TTWV14witness_tables32ConformsWithMoreGenericWitnessesS_11AnyProtocolS_FS1_7generic{{.*}}
// TABLE-NEXT:    method #AnyProtocol.assocTypesMethod!1: @_TTWV14witness_tables32ConformsWithMoreGenericWitnessesS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol.staticMethod!1: @_TTWV14witness_tables32ConformsWithMoreGenericWitnessesS_11AnyProtocolS_ZFS1_12staticMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol."<~>"!1: @_TTWV14witness_tables32ConformsWithMoreGenericWitnessesS_11AnyProtocolS_ZFS1_oi3ltg{{.*}}
// TABLE-NEXT:  }
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables32ConformsWithMoreGenericWitnessesS_11AnyProtocolS_FS1_6method{{.*}} : $@convention(witness_method) (Arg, @in ConformsWithMoreGenericWitnesses, @in_guaranteed ConformsWithMoreGenericWitnesses) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables32ConformsWithMoreGenericWitnessesS_11AnyProtocolS_FS1_7generic{{.*}} : $@convention(witness_method) <A where A : ArchetypeReqt> (@in A, @in ConformsWithMoreGenericWitnesses, @in_guaranteed ConformsWithMoreGenericWitnesses) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables32ConformsWithMoreGenericWitnessesS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}} : $@convention(witness_method) (@in SomeAssoc, @in ConformingAssoc, @in_guaranteed ConformsWithMoreGenericWitnesses) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables32ConformsWithMoreGenericWitnessesS_11AnyProtocolS_ZFS1_12staticMethod{{.*}} : $@convention(witness_method) (@in ConformsWithMoreGenericWitnesses, @thick ConformsWithMoreGenericWitnesses.Type) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWV14witness_tables32ConformsWithMoreGenericWitnessesS_11AnyProtocolS_ZFS1_oi3ltg{{.*}} : $@convention(witness_method) (@in ConformsWithMoreGenericWitnesses, @in ConformsWithMoreGenericWitnesses, @thick ConformsWithMoreGenericWitnesses.Type) -> ()

class ConformingClassToClassProtocol : ClassProtocol {
  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = ConformingAssoc
  
  func method(x x: Arg, y: ConformingClassToClassProtocol) {}
  func generic<Q: ArchetypeReqt>(x x: Q, y: ConformingClassToClassProtocol) {}

  func assocTypesMethod(x x: SomeAssoc, y: ConformingAssoc) {}

  class func staticMethod(x x: ConformingClassToClassProtocol) {}
}
func <~>(x: ConformingClassToClassProtocol,
         y: ConformingClassToClassProtocol) {}
// TABLE-LABEL: sil_witness_table hidden ConformingClassToClassProtocol: ClassProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: ConformingAssoc
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): ConformingAssoc: AssocReqt module witness_tables
// TABLE-NEXT:    method #ClassProtocol.method!1: @_TTWC14witness_tables30ConformingClassToClassProtocolS_13ClassProtocolS_FS1_6method{{.*}}
// TABLE-NEXT:    method #ClassProtocol.generic!1: @_TTWC14witness_tables30ConformingClassToClassProtocolS_13ClassProtocolS_FS1_7generic{{.*}}
// TABLE-NEXT:    method #ClassProtocol.assocTypesMethod!1: @_TTWC14witness_tables30ConformingClassToClassProtocolS_13ClassProtocolS_FS1_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #ClassProtocol.staticMethod!1: @_TTWC14witness_tables30ConformingClassToClassProtocolS_13ClassProtocolS_ZFS1_12staticMethod{{.*}}
// TABLE-NEXT:    method #ClassProtocol."<~>"!1: @_TTWC14witness_tables30ConformingClassToClassProtocolS_13ClassProtocolS_ZFS1_oi3ltg{{.*}}
// TABLE-NEXT:  }
// SYMBOL:  sil hidden [transparent] [thunk] @_TTWC14witness_tables30ConformingClassToClassProtocolS_13ClassProtocolS_FS1_6method{{.*}} : $@convention(witness_method) (Arg, @owned ConformingClassToClassProtocol, @guaranteed ConformingClassToClassProtocol) -> ()
// SYMBOL:  sil hidden [transparent] [thunk] @_TTWC14witness_tables30ConformingClassToClassProtocolS_13ClassProtocolS_FS1_7generic{{.*}} : $@convention(witness_method) <B where B : ArchetypeReqt> (@in B, @owned ConformingClassToClassProtocol, @guaranteed ConformingClassToClassProtocol) -> ()
// SYMBOL:  sil hidden [transparent] [thunk] @_TTWC14witness_tables30ConformingClassToClassProtocolS_13ClassProtocolS_FS1_16assocTypesMethod{{.*}} : $@convention(witness_method) (@in SomeAssoc, @in ConformingAssoc, @guaranteed ConformingClassToClassProtocol) -> ()
// SYMBOL:  sil hidden [transparent] [thunk] @_TTWC14witness_tables30ConformingClassToClassProtocolS_13ClassProtocolS_ZFS1_12staticMethod{{.*}} : $@convention(witness_method) (@owned ConformingClassToClassProtocol, @thick ConformingClassToClassProtocol.Type) -> ()
// SYMBOL:  sil hidden [transparent] [thunk] @_TTWC14witness_tables30ConformingClassToClassProtocolS_13ClassProtocolS_ZFS1_oi3ltg{{.*}} : $@convention(witness_method) (@owned ConformingClassToClassProtocol, @owned ConformingClassToClassProtocol, @thick ConformingClassToClassProtocol.Type) -> ()

class ConformingClassToObjCProtocol : ObjCProtocol {
  @objc func method(x x: ObjCClass) {}
  @objc class func staticMethod(y y: ObjCClass) {}
}
// TABLE-NOT:  sil_witness_table hidden ConformingClassToObjCProtocol

struct ConformingGeneric<R: AssocReqt> : AnyProtocol {
  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = R

  func method(x x: Arg, y: ConformingGeneric) {}
  func generic<Q: ArchetypeReqt>(x x: Q, y: ConformingGeneric) {}

  func assocTypesMethod(x x: SomeAssoc, y: R) {}

  static func staticMethod(x x: ConformingGeneric) {}
}
func <~> <R: AssocReqt>(x: ConformingGeneric<R>, y: ConformingGeneric<R>) {}
// TABLE-LABEL: sil_witness_table hidden <R where R : AssocReqt> ConformingGeneric<R>: AnyProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: R
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): dependent
// TABLE-NEXT:    method #AnyProtocol.method!1: @_TTWuRx14witness_tables9AssocReqtrGVS_17ConformingGenericx_S_11AnyProtocolS_FS2_6method{{.*}}
// TABLE-NEXT:    method #AnyProtocol.generic!1: @_TTWuRx14witness_tables9AssocReqtrGVS_17ConformingGenericx_S_11AnyProtocolS_FS2_7generic{{.*}}
// TABLE-NEXT:    method #AnyProtocol.assocTypesMethod!1: @_TTWuRx14witness_tables9AssocReqtrGVS_17ConformingGenericx_S_11AnyProtocolS_FS2_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol.staticMethod!1: @_TTWuRx14witness_tables9AssocReqtrGVS_17ConformingGenericx_S_11AnyProtocolS_ZFS2_12staticMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol."<~>"!1: @_TTWuRx14witness_tables9AssocReqtrGVS_17ConformingGenericx_S_11AnyProtocolS_ZFS2_oi3ltg{{.*}}
// TABLE-NEXT:  }

protocol AnotherProtocol {}

struct ConformingGenericWithMoreGenericWitnesses<S: AssocReqt>
  : AnyProtocol, AnotherProtocol
{
  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = S

  func method<T, U>(x x: T, y: U) {}
  func generic<V, W>(x x: V, y: W) {}

  func assocTypesMethod<X, Y>(x x: X, y: Y) {}

  static func staticMethod<Z>(x x: Z) {}
}
func <~> <AA: AnotherProtocol, BB: AnotherProtocol>(x: AA, y: BB) {}
// TABLE-LABEL: sil_witness_table hidden <S where S : AssocReqt> ConformingGenericWithMoreGenericWitnesses<S>: AnyProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: S
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): dependent
// TABLE-NEXT:    method #AnyProtocol.method!1: @_TTWuRx14witness_tables9AssocReqtrGVS_41ConformingGenericWithMoreGenericWitnessesx_S_11AnyProtocolS_FS2_6method{{.*}}
// TABLE-NEXT:    method #AnyProtocol.generic!1: @_TTWuRx14witness_tables9AssocReqtrGVS_41ConformingGenericWithMoreGenericWitnessesx_S_11AnyProtocolS_FS2_7generic{{.*}}
// TABLE-NEXT:    method #AnyProtocol.assocTypesMethod!1: @_TTWuRx14witness_tables9AssocReqtrGVS_41ConformingGenericWithMoreGenericWitnessesx_S_11AnyProtocolS_FS2_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol.staticMethod!1: @_TTWuRx14witness_tables9AssocReqtrGVS_41ConformingGenericWithMoreGenericWitnessesx_S_11AnyProtocolS_ZFS2_12staticMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol."<~>"!1: @_TTWuRx14witness_tables9AssocReqtrGVS_41ConformingGenericWithMoreGenericWitnessesx_S_11AnyProtocolS_ZFS2_oi3ltg{{.*}}
// TABLE-NEXT:  }

protocol InheritedProtocol1 : AnyProtocol {
  func inheritedMethod()
}

protocol InheritedProtocol2 : AnyProtocol {
  func inheritedMethod()
}

protocol InheritedClassProtocol : class, AnyProtocol {
  func inheritedMethod()
}

struct InheritedConformance : InheritedProtocol1 {
  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = ConformingAssoc

  func method(x x: Arg, y: InheritedConformance) {}
  func generic<H: ArchetypeReqt>(x x: H, y: InheritedConformance) {}

  func assocTypesMethod(x x: SomeAssoc, y: ConformingAssoc) {}

  static func staticMethod(x x: InheritedConformance) {}

  func inheritedMethod() {}
}
func <~>(x: InheritedConformance, y: InheritedConformance) {}
// TABLE-LABEL: sil_witness_table hidden InheritedConformance: InheritedProtocol1 module witness_tables {
// TABLE-NEXT:    base_protocol AnyProtocol: InheritedConformance: AnyProtocol module witness_tables
// TABLE-NEXT:    method #InheritedProtocol1.inheritedMethod!1: @_TTWV14witness_tables20InheritedConformanceS_18InheritedProtocol1S_FS1_15inheritedMethod{{.*}}
// TABLE-NEXT:  }
// TABLE-LABEL: sil_witness_table hidden InheritedConformance: AnyProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: ConformingAssoc
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): ConformingAssoc: AssocReqt module witness_tables
// TABLE-NEXT:    method #AnyProtocol.method!1: @_TTWV14witness_tables20InheritedConformanceS_11AnyProtocolS_FS1_6method{{.*}}
// TABLE-NEXT:    method #AnyProtocol.generic!1: @_TTWV14witness_tables20InheritedConformanceS_11AnyProtocolS_FS1_7generic{{.*}}
// TABLE-NEXT:    method #AnyProtocol.assocTypesMethod!1: @_TTWV14witness_tables20InheritedConformanceS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol.staticMethod!1: @_TTWV14witness_tables20InheritedConformanceS_11AnyProtocolS_ZFS1_12staticMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol."<~>"!1: @_TTWV14witness_tables20InheritedConformanceS_11AnyProtocolS_ZFS1_oi3ltg{{.*}}
// TABLE-NEXT:  }

struct RedundantInheritedConformance : InheritedProtocol1, AnyProtocol {
  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = ConformingAssoc

  func method(x x: Arg, y: RedundantInheritedConformance) {}
  func generic<H: ArchetypeReqt>(x x: H, y: RedundantInheritedConformance) {}

  func assocTypesMethod(x x: SomeAssoc, y: ConformingAssoc) {}

  static func staticMethod(x x: RedundantInheritedConformance) {}

  func inheritedMethod() {}
}
func <~>(x: RedundantInheritedConformance, y: RedundantInheritedConformance) {}
// TABLE-LABEL: sil_witness_table hidden RedundantInheritedConformance: InheritedProtocol1 module witness_tables {
// TABLE-NEXT:    base_protocol AnyProtocol: RedundantInheritedConformance: AnyProtocol module witness_tables
// TABLE-NEXT:    method #InheritedProtocol1.inheritedMethod!1: @_TTWV14witness_tables29RedundantInheritedConformanceS_18InheritedProtocol1S_FS1_15inheritedMethod{{.*}}
// TABLE-NEXT:  }
// TABLE-LABEL: sil_witness_table hidden RedundantInheritedConformance: AnyProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: ConformingAssoc
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): ConformingAssoc: AssocReqt module witness_tables
// TABLE-NEXT:    method #AnyProtocol.method!1: @_TTWV14witness_tables29RedundantInheritedConformanceS_11AnyProtocolS_FS1_6method{{.*}}
// TABLE-NEXT:    method #AnyProtocol.generic!1: @_TTWV14witness_tables29RedundantInheritedConformanceS_11AnyProtocolS_FS1_7generic{{.*}}
// TABLE-NEXT:    method #AnyProtocol.assocTypesMethod!1: @_TTWV14witness_tables29RedundantInheritedConformanceS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol.staticMethod!1: @_TTWV14witness_tables29RedundantInheritedConformanceS_11AnyProtocolS_ZFS1_12staticMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol."<~>"!1: @_TTWV14witness_tables29RedundantInheritedConformanceS_11AnyProtocolS_ZFS1_oi3ltg{{.*}}
// TABLE-NEXT:  }

struct DiamondInheritedConformance : InheritedProtocol1, InheritedProtocol2 {
  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = ConformingAssoc

  func method(x x: Arg, y: DiamondInheritedConformance) {}
  func generic<H: ArchetypeReqt>(x x: H, y: DiamondInheritedConformance) {}

  func assocTypesMethod(x x: SomeAssoc, y: ConformingAssoc) {}

  static func staticMethod(x x: DiamondInheritedConformance) {}

  func inheritedMethod() {}
}
func <~>(x: DiamondInheritedConformance, y: DiamondInheritedConformance) {}
// TABLE-LABEL: sil_witness_table hidden DiamondInheritedConformance: InheritedProtocol1 module witness_tables {
// TABLE-NEXT:    base_protocol AnyProtocol: DiamondInheritedConformance: AnyProtocol module witness_tables
// TABLE-NEXT:    method #InheritedProtocol1.inheritedMethod!1: @_TTWV14witness_tables27DiamondInheritedConformanceS_18InheritedProtocol1S_FS1_15inheritedMethod{{.*}}
// TABLE-NEXT:  }
// TABLE-LABEL: sil_witness_table hidden DiamondInheritedConformance: InheritedProtocol2 module witness_tables {
// TABLE-NEXT:    base_protocol AnyProtocol: DiamondInheritedConformance: AnyProtocol module witness_tables
// TABLE-NEXT:    method #InheritedProtocol2.inheritedMethod!1: @_TTWV14witness_tables27DiamondInheritedConformanceS_18InheritedProtocol2S_FS1_15inheritedMethod{{.*}}
// TABLE-NEXT:  }
// TABLE-LABEL: sil_witness_table hidden DiamondInheritedConformance: AnyProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: ConformingAssoc
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): ConformingAssoc: AssocReqt module witness_tables
// TABLE-NEXT:    method #AnyProtocol.method!1: @_TTWV14witness_tables27DiamondInheritedConformanceS_11AnyProtocolS_FS1_6method{{.*}}
// TABLE-NEXT:    method #AnyProtocol.generic!1: @_TTWV14witness_tables27DiamondInheritedConformanceS_11AnyProtocolS_FS1_7generic{{.*}}
// TABLE-NEXT:    method #AnyProtocol.assocTypesMethod!1: @_TTWV14witness_tables27DiamondInheritedConformanceS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol.staticMethod!1: @_TTWV14witness_tables27DiamondInheritedConformanceS_11AnyProtocolS_ZFS1_12staticMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol."<~>"!1: @_TTWV14witness_tables27DiamondInheritedConformanceS_11AnyProtocolS_ZFS1_oi3ltg{{.*}}
// TABLE-NEXT:  }

class ClassInheritedConformance : InheritedClassProtocol {
  typealias AssocType = SomeAssoc
  typealias AssocWithReqt = ConformingAssoc

  func method(x x: Arg, y: ClassInheritedConformance) {}
  func generic<H: ArchetypeReqt>(x x: H, y: ClassInheritedConformance) {}

  func assocTypesMethod(x x: SomeAssoc, y: ConformingAssoc) {}

  class func staticMethod(x x: ClassInheritedConformance) {}

  func inheritedMethod() {}
}
func <~>(x: ClassInheritedConformance, y: ClassInheritedConformance) {}
// TABLE-LABEL: sil_witness_table hidden ClassInheritedConformance: InheritedClassProtocol module witness_tables {
// TABLE-NEXT:    base_protocol AnyProtocol: ClassInheritedConformance: AnyProtocol module witness_tables
// TABLE-NEXT:    method #InheritedClassProtocol.inheritedMethod!1: @_TTWC14witness_tables25ClassInheritedConformanceS_22InheritedClassProtocolS_FS1_15inheritedMethod{{.*}}
// TABLE-NEXT:  }
// TABLE-LABEL: sil_witness_table hidden ClassInheritedConformance: AnyProtocol module witness_tables {
// TABLE-NEXT:    associated_type AssocType: SomeAssoc
// TABLE-NEXT:    associated_type AssocWithReqt: ConformingAssoc
// TABLE-NEXT:    associated_type_protocol (AssocWithReqt: AssocReqt): ConformingAssoc: AssocReqt module witness_tables
// TABLE-NEXT:    method #AnyProtocol.method!1: @_TTWC14witness_tables25ClassInheritedConformanceS_11AnyProtocolS_FS1_6method{{.*}}
// TABLE-NEXT:    method #AnyProtocol.generic!1: @_TTWC14witness_tables25ClassInheritedConformanceS_11AnyProtocolS_FS1_7generic{{.*}}
// TABLE-NEXT:    method #AnyProtocol.assocTypesMethod!1: @_TTWC14witness_tables25ClassInheritedConformanceS_11AnyProtocolS_FS1_16assocTypesMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol.staticMethod!1: @_TTWC14witness_tables25ClassInheritedConformanceS_11AnyProtocolS_ZFS1_12staticMethod{{.*}}
// TABLE-NEXT:    method #AnyProtocol."<~>"!1: @_TTWC14witness_tables25ClassInheritedConformanceS_11AnyProtocolS_ZFS1_oi3ltg{{.*}}
// TABLE-NEXT:  }
// -- Witnesses have the 'self' abstraction level of their protocol.
//    AnyProtocol has no class bound, so its witnesses treat Self as opaque.
//    InheritedClassProtocol has a class bound, so its witnesses treat Self as
//    a reference value.
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWC14witness_tables25ClassInheritedConformanceS_22InheritedClassProtocolS_FS1_15inheritedMethod{{.*}} : $@convention(witness_method) (@guaranteed ClassInheritedConformance) -> ()
// SYMBOL:      sil hidden [transparent] [thunk] @_TTWC14witness_tables25ClassInheritedConformanceS_11AnyProtocolS_FS1_6method{{.*}} : $@convention(witness_method) (Arg, @in ClassInheritedConformance, @in_guaranteed ClassInheritedConformance) -> ()

struct GenericAssocType<T> : AssocReqt {
  func requiredMethod() {}
}

protocol AssocTypeWithReqt {
  associatedtype AssocType : AssocReqt
}

struct ConformsWithDependentAssocType1<CC: AssocReqt> : AssocTypeWithReqt {
  typealias AssocType = CC
}
// TABLE-LABEL: sil_witness_table hidden <CC where CC : AssocReqt> ConformsWithDependentAssocType1<CC>: AssocTypeWithReqt module witness_tables {
// TABLE-NEXT:    associated_type AssocType: CC
// TABLE-NEXT:    associated_type_protocol (AssocType: AssocReqt): dependent
// TABLE-NEXT:  }

struct ConformsWithDependentAssocType2<DD> : AssocTypeWithReqt {
  typealias AssocType = GenericAssocType<DD>
}
// TABLE-LABEL: sil_witness_table hidden <DD> ConformsWithDependentAssocType2<DD>: AssocTypeWithReqt module witness_tables {
// TABLE-NEXT:    associated_type AssocType: GenericAssocType<DD>
// TABLE-NEXT:    associated_type_protocol (AssocType: AssocReqt): GenericAssocType<DD>: specialize <DD> (<T> GenericAssocType<T>: AssocReqt module witness_tables)
// TABLE-NEXT:  }

protocol InheritedFromObjC : ObjCProtocol {
  func inheritedMethod()
}

class ConformsInheritedFromObjC : InheritedFromObjC {
  @objc func method(x x: ObjCClass) {}
  @objc class func staticMethod(y y: ObjCClass) {}
  func inheritedMethod() {}
}
// TABLE-LABEL: sil_witness_table hidden ConformsInheritedFromObjC: InheritedFromObjC module witness_tables {
// TABLE-NEXT:    method #InheritedFromObjC.inheritedMethod!1: @_TTWC14witness_tables25ConformsInheritedFromObjCS_17InheritedFromObjCS_FS1_15inheritedMethod{{.*}}
// TABLE-NEXT:  }

protocol ObjCAssoc {
  associatedtype AssocType : ObjCProtocol
}

struct HasObjCAssoc : ObjCAssoc {
  typealias AssocType = ConformsInheritedFromObjC
}
// TABLE-LABEL: sil_witness_table hidden HasObjCAssoc: ObjCAssoc module witness_tables {
// TABLE-NEXT:    associated_type AssocType: ConformsInheritedFromObjC
// TABLE-NEXT:  }

protocol Initializer {
  init(arg: Arg)
}

// TABLE-LABEL: sil_witness_table hidden HasInitializerStruct: Initializer module witness_tables {
// TABLE-NEXT:  method #Initializer.init!allocator.1: @_TTWV14witness_tables20HasInitializerStructS_11InitializerS_FS1_C{{.*}}
// TABLE-NEXT: }
// SYMBOL: sil hidden [transparent] [thunk] @_TTWV14witness_tables20HasInitializerStructS_11InitializerS_FS1_C{{.*}} : $@convention(witness_method) (Arg, @thick HasInitializerStruct.Type) -> @out HasInitializerStruct
struct HasInitializerStruct : Initializer { 
  init(arg: Arg) { }
}

// TABLE-LABEL: sil_witness_table hidden HasInitializerClass: Initializer module witness_tables {
// TABLE-NEXT:  method #Initializer.init!allocator.1: @_TTWC14witness_tables19HasInitializerClassS_11InitializerS_FS1_C{{.*}}
// TABLE-NEXT: }
// SYMBOL: sil hidden [transparent] [thunk] @_TTWC14witness_tables19HasInitializerClassS_11InitializerS_FS1_C{{.*}} : $@convention(witness_method) (Arg, @thick HasInitializerClass.Type) -> @out HasInitializerClass
class HasInitializerClass : Initializer {
  required init(arg: Arg) { }
}

// TABLE-LABEL: sil_witness_table hidden HasInitializerEnum: Initializer module witness_tables {
// TABLE-NEXT:  method #Initializer.init!allocator.1: @_TTWO14witness_tables18HasInitializerEnumS_11InitializerS_FS1_C{{.*}}
// TABLE-NEXT: }
// SYMBOL: sil hidden [transparent] [thunk] @_TTWO14witness_tables18HasInitializerEnumS_11InitializerS_FS1_C{{.*}} : $@convention(witness_method) (Arg, @thick HasInitializerEnum.Type) -> @out HasInitializerEnum
enum HasInitializerEnum : Initializer {
  case A

  init(arg: Arg) { self = .A }
}
  
