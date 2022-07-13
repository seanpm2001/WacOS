// RUN: %target-swift-frontend -emit-ir %s | %FileCheck %s

// REQUIRES: objc_interop

// Check that type metadata defined inside extensions of files imported from
// other modules is emitted with the right linkage.
//
// In particular, it should be possible to define types inside extensions of
// types imported from Foundation (rdar://problem/27245620).

import Foundation

// CHECK-LABEL: @_T0So8NSNumberC31extension_type_metadata_linkingE4BaseCMm = global
// CHECK-LABEL: @_T0So8NSNumberC31extension_type_metadata_linkingE4BaseCMn = constant
// CHECK-LABEL: @_T0So8NSNumberC31extension_type_metadata_linkingE4BaseCMf = internal global

// CHECK-LABEL: @_T0So8NSNumberC31extension_type_metadata_linkingE7DerivedCMm = global
// CHECK-LABEL: @_T0So8NSNumberC31extension_type_metadata_linkingE7DerivedCMn = constant
// CHECK-LABEL: @_T0So8NSNumberC31extension_type_metadata_linkingE7DerivedCMf = internal global

// CHECK-LABEL: @_T0So8NSNumberC31extension_type_metadata_linkingE6StructVMn = constant
// CHECK-LABEL: @_T0So8NSNumberC31extension_type_metadata_linkingE6StructVMf = internal constant

// CHECK-LABEL: @_T0So8NSNumberC31extension_type_metadata_linkingE4BaseCN = alias
// CHECK-LABEL: @_T0So8NSNumberC31extension_type_metadata_linkingE7DerivedCN = alias
// CHECK-LABEL: @_T0So8NSNumberC31extension_type_metadata_linkingE6StructVN = alias

// CHECK-LABEL: define %swift.type* @_T0So8NSNumberC31extension_type_metadata_linkingE4BaseCMa()
// CHECK-LABEL: define %swift.type* @_T0So8NSNumberC31extension_type_metadata_linkingE7DerivedCMa

// FIXME: Not needed
// CHECK-LABEL: define %swift.type* @_T0So8NSNumberC31extension_type_metadata_linkingE6StructVMa

extension NSNumber {
  public class Base : CustomStringConvertible {
    public var description: String {
      return "Base"
    }
  }

  public class Derived : Base {
    override public var description: String {
      return "Derived"
    }
  }

  public struct Struct {}
}

