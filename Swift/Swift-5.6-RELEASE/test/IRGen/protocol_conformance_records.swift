// RUN: %target-swift-frontend -primary-file %s -emit-ir -enable-library-evolution -enable-source-import -I %S/../Inputs | %FileCheck %s
// RUN: %target-swift-frontend %s -emit-ir -num-threads 8 -enable-library-evolution -enable-source-import -I %S/../Inputs | %FileCheck %s

import resilient_struct
import resilient_protocol

public protocol Associate {
  associatedtype X
}

public struct Dependent<T> {}

public protocol Runcible {
  func runce()
}

// CHECK-LABEL: @"$s28protocol_conformance_records15NativeValueTypeVAA8RuncibleAAMc" ={{ dllexport | protected | }}constant %swift.protocol_conformance_descriptor {
// -- protocol descriptor
// CHECK-SAME:           [[RUNCIBLE:@"\$s28protocol_conformance_records8RuncibleMp"]]
// -- type metadata
// CHECK-SAME:           @"$s28protocol_conformance_records15NativeValueTypeVMn"
// -- witness table
// CHECK-SAME:           @"$s28protocol_conformance_records15NativeValueTypeVAA8RuncibleAAWP"
// -- flags
// CHECK-SAME:           i32 0 },
public struct NativeValueType: Runcible {
  public func runce() {}
}

// CHECK-LABEL:         @"$s28protocol_conformance_records15NativeClassTypeCAA8RuncibleAAMc" ={{ dllexport | protected | }}constant %swift.protocol_conformance_descriptor {
// -- protocol descriptor
// CHECK-SAME:           [[RUNCIBLE]]
// -- class metadata
// CHECK-SAME:           @"$s28protocol_conformance_records15NativeClassTypeCMn"
// -- witness table
// CHECK-SAME:           @"$s28protocol_conformance_records15NativeClassTypeCAA8RuncibleAAWP"
// -- flags
// CHECK-SAME:           i32 0 },
public class NativeClassType: Runcible {
  public func runce() {}
}

// CHECK-LABEL:         @"$s28protocol_conformance_records17NativeGenericTypeVyxGAA8RuncibleAAMc" ={{ dllexport | protected | }}constant %swift.protocol_conformance_descriptor {
// -- protocol descriptor
// CHECK-SAME:           [[RUNCIBLE]]
// -- nominal type descriptor
// CHECK-SAME:           @"$s28protocol_conformance_records17NativeGenericTypeVMn"
// -- witness table
// CHECK-SAME:           @"$s28protocol_conformance_records17NativeGenericTypeVyxGAA8RuncibleAAWP"
// -- flags
// CHECK-SAME:           i32 0 },
public struct NativeGenericType<T>: Runcible {
  public func runce() {}
}

// CHECK-LABEL:         @"$sSi28protocol_conformance_records8RuncibleAAMc" ={{ dllexport | protected | }}constant %swift.protocol_conformance_descriptor {
// -- protocol descriptor
// CHECK-SAME:           [[RUNCIBLE]]
// -- type metadata
// CHECK-SAME:           @"{{got.|\\01__imp__?}}$sSiMn"
// -- witness table
// CHECK-SAME:           @"$sSi28protocol_conformance_records8RuncibleAAWP"
// -- reserved
// CHECK-SAME:           i32 8
// CHECK-SAME:         }
extension Int: Runcible {
  public func runce() {}
}

// For a resilient struct, reference the NominalTypeDescriptor

// CHECK-LABEL:         @"$s16resilient_struct4SizeV28protocol_conformance_records8RuncibleADMc" ={{ dllexport | protected | }}constant %swift.protocol_conformance_descriptor {
// -- protocol descriptor
// CHECK-SAME:           [[RUNCIBLE]]
// -- nominal type descriptor
// CHECK-SAME:           @"{{got.|\\01__imp__?}}$s16resilient_struct4SizeVMn"
// -- witness table
// CHECK-SAME:           @"$s16resilient_struct4SizeV28protocol_conformance_records8RuncibleADWP"
// -- reserved
// CHECK-SAME:           i32 8
// CHECK-SAME:         }

extension Size: Runcible {
  public func runce() {}
}

// A non-dependent type conforming to a protocol with associated conformances
// does not require a generic witness table.
public protocol Simple {}

public protocol AssociateConformance {
  associatedtype X : Simple
}

public struct Other : Simple {}

public struct Concrete : AssociateConformance {
  public typealias X = Other
}

// CHECK-LABEL: @"$s28protocol_conformance_records8ConcreteVAA20AssociateConformanceAAMc" ={{ dllexport | protected | }}constant
// -- protocol descriptor
// CHECK-SAME:           @"$s28protocol_conformance_records20AssociateConformanceMp"
// -- nominal type descriptor
// CHECK-SAME:           @"$s28protocol_conformance_records8ConcreteVMn"
// -- witness table
// CHECK-SAME:           @"$s28protocol_conformance_records8ConcreteVAA20AssociateConformanceAAWP"
// -- no flags are set, and no generic witness table follows
// CHECK-SAME:           i32 0 }

public protocol Spoon { }

// Conditional conformances
// CHECK-LABEL: {{^}}@"$s28protocol_conformance_records17NativeGenericTypeVyxGAA5SpoonA2aERzlMc" ={{ dllexport | protected | }}constant
// -- protocol descriptor
// CHECK-SAME:           @"$s28protocol_conformance_records5SpoonMp"
// -- nominal type descriptor
// CHECK-SAME:           @"$s28protocol_conformance_records17NativeGenericTypeVMn"
// -- witness table accessor
// CHECK-SAME:           @"$s28protocol_conformance_records17NativeGenericTypeVyxGAA5Spoon
// -- flags
// CHECK-SAME:           i32 131328
// -- conditional requirement #1
// CHECK-SAME:           i32 128,
// CHECK-SAME:           i32 0,
// CHECK-SAME:           @"$s28protocol_conformance_records5SpoonMp"
// CHECK-SAME:         }
extension NativeGenericType : Spoon where T: Spoon {
  public func runce() {}
}

// Retroactive conformance
// CHECK-LABEL: @"$sSi18resilient_protocol22OtherResilientProtocol0B20_conformance_recordsMc" ={{ dllexport | protected | }}constant
// -- protocol descriptor
// CHECK-SAME:           @"{{got.|\\01__imp__?}}$s18resilient_protocol22OtherResilientProtocolMp"
// -- nominal type descriptor
// CHECK-SAME:           @"{{got.|\\01__imp__?}}$sSiMn"
// -- witness table pattern
// CHECK-SAME:           i32 0,
// -- flags
// CHECK-SAME:           i32 131144,
// -- module context for retroactive conformance
// CHECK-SAME:           @"$s28protocol_conformance_recordsMXM"
// CHECK-SAME:         }
extension Int : OtherResilientProtocol { }

// Dependent conformance
// CHECK-LABEL: @"$s28protocol_conformance_records9DependentVyxGAA9AssociateAAMc" ={{ dllexport | protected | }}constant
// -- protocol descriptor
// CHECK-SAME:           @"$s28protocol_conformance_records9AssociateMp"
// -- nominal type descriptor
// CHECK-SAME:           @"$s28protocol_conformance_records9DependentVMn"
// -- witness table pattern
// CHECK-SAME:           @"$s28protocol_conformance_records9DependentVyxGAA9AssociateAAWp"
// -- flags
// CHECK-SAME:           i32 131072,
// -- number of words in witness table
// CHECK-SAME:           i16 2,
// -- number of private words in witness table + bit for "needs instantiation"
// CHECK-SAME:           i16 1
// CHECK-SAME:         }
extension Dependent : Associate {
  public typealias X = (T, T)
}

// CHECK-LABEL: @"$s28protocol_conformance_records9AssociateHr" = private constant
// CHECK-LABEL: @"$s28protocol_conformance_records8RuncibleHr" = private constant
// CHECK-LABEL: @"$s28protocol_conformance_records5SpoonHr" = private constant

// CHECK-LABEL: @"$s28protocol_conformance_records15NativeValueTypeVAA8RuncibleAAHc" = private constant
// CHECK-LABEL: @"$s28protocol_conformance_records15NativeClassTypeCAA8RuncibleAAHc" = private constant
// CHECK-LABEL: @"$s28protocol_conformance_records17NativeGenericTypeVyxGAA8RuncibleAAHc" = private constant
// CHECK-LABEL: @"$s16resilient_struct4SizeV28protocol_conformance_records8RuncibleADHc" = private constant
// CHECK-LABEL: @"$s28protocol_conformance_records8ConcreteVAA20AssociateConformanceAAHc" = private constant
// CHECK-LABEL: @"$s28protocol_conformance_records17NativeGenericTypeVyxGAA5SpoonA2aERzlHc" = private constant
// CHECK-LABEL: @"$sSi18resilient_protocol22OtherResilientProtocol0B20_conformance_recordsHc" = private constant
