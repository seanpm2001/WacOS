// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -parse %s -verify

// REQUIRES: objc_interop

import Foundation

class X : NSObject {
  func foo() -> X { return self }
}

@NSManaged struct SomeStruct {} // expected-error {{@NSManaged cannot be applied to this declaration}}

@NSManaged var global: Int // expected-error {{@NSManaged only allowed on an instance property or method}}

@NSManaged     // expected-error {{@NSManaged only allowed on an instance property or method}}
func managedFunction() {}

protocol SwiftProto { }

class SwiftGizmo : A {
  @NSManaged var a: X
  @NSManaged var b: Int
  @NSManaged let c: Int  // expected-error {{@NSManaged not allowed on a 'let' property}}
  
  // expected-error@+1{{@NSManaged property cannot have an initial value}}
  @NSManaged var gizmo: SwiftGizmo = SwiftGizmo()
  
  // expected-error@+1{{@NSManaged not allowed on computed properties}}
  @NSManaged var computed_var: Int {
    return 5
  }

  // expected-error@+1{{@NSManaged not allowed on observing properties}}
  @NSManaged var observing_var: Int {
    willSet { }
  }

  // expected-error@+1{{property cannot be marked @NSManaged because its type cannot be represented in Objective-C}}
  @NSManaged var nonobjc_var: SwiftProto?

  @NSManaged class var d: Int = 4  // expected-error {{@NSManaged only allowed on an instance property or method}}

  @NSManaged var e: Int { return 4 } // expected-error {{@NSManaged not allowed on computed properties}}

  @NSCopying @NSManaged var optionalProperty : NSString?  // expected-error {{@NSManaged property cannot also be marked @NSCopying}}

  @NSManaged func mutableArrayValueForA() // no-warning
  @NSManaged func mutableArrayValueForB() {} // expected-error {{NSManaged method cannot have a body; it must be provided at runtime}}
  @NSManaged class func mutableArrayValueForA() {} // expected-error {{@NSManaged only allowed on an instance property or method}}

  // SR-1050: don't assert
  @NSManaged var multiA, multiB, multiC : NSNumber?

  override init() {}
}

