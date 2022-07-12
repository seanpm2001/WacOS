// RUN: %target-swift-remoteast-test-with-sdk %s | %FileCheck %s

// REQUIRES: swift-remoteast-test
// REQUIRES: objc_interop

import Foundation

@_silgen_name("printMetadataType")
func printType(_: Any.Type)

@_silgen_name("printHeapMetadataType")
func printDynamicType(_: AnyObject)

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
