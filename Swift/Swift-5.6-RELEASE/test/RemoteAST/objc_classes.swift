// RUN: %target-swift-remoteast-test-with-sdk %s | %FileCheck %s

// REQUIRES: swift-remoteast-test
// REQUIRES: objc_interop

// This is an interpreter test that cannot use swift-darwin-postprocess.py to
// work around the DYLD_LIBRARY_LOAD bug in recent dylds. We need to find an
// alternative workaround for it, such as bumping this test's deployment target.
// REQUIRES: rdar78933143

import Foundation

@_silgen_name("printMetadataType")
func printType(_: Any.Type)

@_silgen_name("printHeapMetadataType")
func printDynamicType(_: AnyObject)

@_silgen_name("stopRemoteAST")
func stopRemoteAST()

printType(NSString.self)
// CHECK: NSString

class A<T> : NSObject {
  @objc var property: Int
  override init() { property = 0 }
}
let a = A<Int>()
printDynamicType(a)
// CHECK: A<Int>

let observer = NSObject()
a.addObserver(observer, forKeyPath: "property", options: [], context: nil)
printDynamicType(a)
// CHECK: A<Int>

// FIXME: The ... & AnyObject is redundant here:

printType(NSFastEnumeration.self)
// CHECK: NSFastEnumeration & AnyObject

printType(Optional<NSFastEnumeration>.self)
// CHECK: Optional<NSFastEnumeration & AnyObject>

@objc protocol OurObjCProtocol {}

printType(OurObjCProtocol.self)
// CHECK: OurObjCProtocol & AnyObject

printType(Optional<OurObjCProtocol>.self)
// CHECK: Optional<OurObjCProtocol & AnyObject>

stopRemoteAST()
