/// Test the generated private textual module interfaces and that the public
/// one doesn't leak SPI decls and info.

// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -emit-module %S/Inputs/spi_helper.swift -module-name SPIHelper -emit-module-path %t/SPIHelper.swiftmodule -swift-version 5 -enable-library-evolution -emit-module-interface-path %t/SPIHelper.swiftinterface -emit-private-module-interface-path %t/SPIHelper.private.swiftinterface
// RUN: %target-swift-frontend -emit-module %S/Inputs/ioi_helper.swift -module-name IOIHelper -emit-module-path %t/IOIHelper.swiftmodule -swift-version 5 -enable-library-evolution -emit-module-interface-path %t/IOIHelper.swiftinterface -emit-private-module-interface-path %t/IOIHelper.private.swiftinterface

/// Make sure that the public swiftinterface of spi_helper doesn't leak SPI.
// RUN: %FileCheck -check-prefix=CHECK-HELPER %s < %t/SPIHelper.swiftinterface
// CHECK-HELPER-NOT: HelperSPI
// CHECK-HELPER-NOT: @_spi
// CHECK-HELPER-NOT: @_specialize

// Test the spi parameter of the _specialize attribute in the private interface.
// RUN: %FileCheck -check-prefix=CHECK-HELPER-PRIVATE %s < %t/SPIHelper.private.swiftinterface
// CHECK-HELPER-PRIVATE: @_specialize(exported: true, spi: HelperSPI, kind: full, where T == Swift.Int)
// CHECK-HELPER-PRIVATE-NEXT: public func genericFunc<T>(_ t: T)
// CHECK-HELPER-PRIVATE:  @_specialize(exported: true, spi: HelperSPI, kind: full, where T == Swift.Int)
// CHECK-HELPER-PRIVATE-NEXT:  public func genericFunc2<T>(_ t: T)
// CHECK-HELPER-PRIVATE:  @_specialize(exported: true, spi: HelperSPI, kind: full, where T == Swift.Int)
// CHECK-HELPER-PRIVATE-NEXT:  public func genericFunc3<T>(_ t: T)
// CHECK-HELPER-PRIVATE:  @_specialize(exported: true, spi: HelperSPI, kind: full, where T == Swift.Int)
// CHECK-HELPER-PRIVATE-NEXT:  public func genericFunc4<T>(_ t: T)
// CHECK-HELPER-PRIVATE:   @_specialize(exported: true, spi: HelperSPI, kind: full, where T == Swift.Int)
// CHECK-HELPER-PRIVATE-NEXT:  public func prespecializedMethod<T>(_ t: T)


// RUN: %target-swift-frontend -emit-module %t/SPIHelper.swiftinterface -emit-module-path %t/SPIHelper-from-public-swiftinterface.swiftmodule -swift-version 5 -module-name SPIHelper -enable-library-evolution

/// Test the textual interfaces generated from this test.
// RUN: %target-swift-frontend -typecheck %s -emit-module-interface-path %t/Main.swiftinterface -emit-private-module-interface-path %t/Main.private.swiftinterface -enable-library-evolution -swift-version 5 -I %t -module-name Main
// RUN: %FileCheck -check-prefix=CHECK-PUBLIC %s < %t/Main.swiftinterface
// RUN: %FileCheck -check-prefix=CHECK-PRIVATE %s < %t/Main.private.swiftinterface
// RUN: %target-swift-frontend -typecheck-module-from-interface -I %t %t/Main.swiftinterface
// RUN: %target-swift-frontend -typecheck-module-from-interface -I %t %t/Main.private.swiftinterface -module-name Main

/// Serialize and deserialize this module, then print.
// RUN: %target-swift-frontend -emit-module %s -emit-module-path %t/Merged-partial.swiftmodule -swift-version 5 -I %t -module-name Merged -enable-library-evolution
// RUN: %target-swift-frontend -merge-modules %t/Merged-partial.swiftmodule -module-name Merged -emit-module -emit-module-path %t/Merged.swiftmodule -I %t -emit-module-interface-path %t/Merged.swiftinterface -emit-private-module-interface-path %t/Merged.private.swiftinterface -enable-library-evolution -swift-version 5 -I %t
// RUN: %FileCheck -check-prefix=CHECK-PUBLIC %s < %t/Merged.swiftinterface
// RUN: %FileCheck -check-prefix=CHECK-PRIVATE %s < %t/Merged.private.swiftinterface
// RUN: %target-swift-frontend -typecheck-module-from-interface -I %t %t/Merged.swiftinterface
// RUN: %target-swift-frontend -typecheck-module-from-interface -I %t %t/Merged.private.swiftinterface -module-name Merged

/// Both the public and private textual interfaces should have
/// SPI information with `-library-level spi`.
// RUN: %target-swift-frontend -typecheck %s -emit-module-interface-path %t/SPIModule.swiftinterface -emit-private-module-interface-path %t/SPIModule.private.swiftinterface -enable-library-evolution -swift-version 5 -I %t -module-name SPIModule -library-level spi
// RUN: %FileCheck -check-prefix=CHECK-PRIVATE %s < %t/SPIModule.swiftinterface
// RUN: %FileCheck -check-prefix=CHECK-PRIVATE %s < %t/SPIModule.private.swiftinterface
// RUN: %target-swift-frontend -typecheck-module-from-interface -I %t %t/SPIModule.swiftinterface
// RUN: %target-swift-frontend -typecheck-module-from-interface -I %t %t/SPIModule.private.swiftinterface -module-name SPIModule

@_spi(HelperSPI) @_spi(OtherSPI) @_spi(OtherSPI) import SPIHelper
// CHECK-PUBLIC: import SPIHelper
// CHECK-PRIVATE: @_spi(OtherSPI) @_spi(HelperSPI) import SPIHelper

@_implementationOnly import IOIHelper
public func foo() {}
// CHECK-PUBLIC: foo()
// CHECK-PRIVATE: foo()

@_spi(MySPI) @_spi(MyOtherSPI) public func localSPIFunc() {}
// CHECK-PRIVATE: @_spi(MySPI)
// CHECK-PRIVATE: localSPIFunc()
// CHECK-PUBLIC-NOT: localSPIFunc()

// SPI declarations
@_spi(MySPI) public class SPIClassLocal {
// CHECK-PRIVATE: @_spi(MySPI) public class SPIClassLocal
// CHECK-PUBLIC-NOT: class SPIClassLocal

  public init() {}
}

@_spi(MySPI) public extension SPIClassLocal {
// CHECK-PRIVATE: @_spi(MySPI) extension {{.+}}.SPIClassLocal
// CHECK-PUBLIC-NOT: extension {{.+}}.SPIClassLocal

  @_spi(MySPI) func extensionMethod() {}
  // CHECK-PRIVATE: @_spi(MySPI) public func extensionMethod
  // CHECK-PUBLIC-NOT: func extensionMethod

  internal func internalExtensionMethod() {}
  // CHECK-PRIVATE-NOT: internalExtensionMethod
  // CHECK-PUBLIC-NOT: internalExtensionMethod

  func inheritedSPIExtensionMethod() {}
  // CHECK-PRIVATE: inheritedSPIExtensionMethod
  // CHECK-PUBLIC-NOT: inheritedSPIExtensionMethod
}

public extension SPIClassLocal {
  internal func internalExtensionMethode1() {}
  // CHECK-PRIVATE-NOT: internalExtensionMethod1
  // CHECK-PUBLIC-NOT: internalExtensionMethod1
}

class InternalClassLocal {}
// CHECK-PRIVATE-NOT: InternalClassLocal
// CHECK-PUBLIC-NOT: InternalClassLocal

private class PrivateClassLocal {}
// CHECK-PRIVATE-NOT: PrivateClassLocal
// CHECK-PUBLIC-NOT: PrivateClassLocal

@_spi(LocalSPI) public func useOfSPITypeOk(_ p: SPIClassLocal) -> SPIClassLocal {
  fatalError()
}
// CHECK-PRIVATE: @_spi(LocalSPI) public func useOfSPITypeOk
// CHECK-PUBLIC-NOT: useOfSPITypeOk

@_spi(LocalSPI) extension SPIClass {
  // CHECK-PRIVATE: @_spi(LocalSPI) extension SPIHelper.SPIClass
  // CHECK-PUBLIC-NOT: SPIHelper.SPIClass

  @_spi(LocalSPI) public func extensionSPIMethod() {}
  // CHECK-PRIVATE: @_spi(LocalSPI) public func extensionSPIMethod()
  // CHECK-PUBLIC-NOT: extensionSPIMethod
}

@propertyWrapper
public struct Wrapper<T> {
  public var value: T

  public var wrappedValue: T {
    get { value }
    set { value = newValue }
  }
}

@propertyWrapper
public struct WrapperWithInitialValue<T> {
  private var value: T

  public var wrappedValue: T {
    get { value }
    set { value = newValue }
  }

  public var projectedValue: Wrapper<T> {
    get { Wrapper(value: value) }
    set { value = newValue.value }
  }
}

public class SomeClass {
}

public struct PublicStruct {
  @_spi(S) @Wrapper public var spiWrappedSimple: SomeClass
  // CHECK-PRIVATE: @_spi(S) @{{.*}}.Wrapper public var spiWrappedSimple: {{.*}}.SomeClass
  // CHECK-PUBLIC-NOT: spiWrappedSimple

  @_spi(S) @WrapperWithInitialValue public var spiWrappedDefault: SomeClass
  // CHECK-PRIVATE: @_spi(S) @{{.*}}.WrapperWithInitialValue @_projectedValueProperty($spiWrappedDefault) public var spiWrappedDefault: {{.*}}.SomeClass
  // CHECK-PRIVATE: @_spi(S) public var $spiWrappedDefault: {{.*}}.Wrapper<{{.*}}.SomeClass>
  // CHECK-PUBLIC-NOT: spiWrappedDefault
}

@_spi(LocalSPI) public protocol SPIProto3 {
// CHECK-PRIVATE: @_spi(LocalSPI) public protocol SPIProto3
// CHECK-PUBLIC-NOT: SPIProto3

  associatedtype AssociatedType
  // CHECK-PRIVATE: {{^}}  associatedtype AssociatedType
  // CHECK-PUBLIC-NOT: AssociatedType

  func implicitSPIMethod()
  // CHECK-PRIVATE: @_spi(LocalSPI) func implicitSPIMethod()
  // CHECK-PUBLIC-NOT: implicitSPIMethod
}

// Test the dummy conformance printed to replace private types used in
// conditional conformances. rdar://problem/63352700

// Conditional conformances using SPI types should appear in full in the
// private swiftinterface.
public struct PublicType<T> {}
@_spi(LocalSPI) public protocol SPIProto {}
private protocol PrivateConstraint {}
@_spi(LocalSPI) public protocol SPIProto2 {}

@_spi(LocalSPI)
extension PublicType: SPIProto2 where T: SPIProto2 {}
// CHECK-PRIVATE: extension {{.*}}.PublicType : {{.*}}.SPIProto2 where T : {{.*}}.SPIProto2
// CHECK-PUBLIC-NOT: _ConstraintThatIsNotPartOfTheAPIOfThisLibrary

public protocol LocalPublicProto {}
extension IOIPublicStruct : LocalPublicProto {}
// CHECK-PRIVATE-NOT: IOIPublicStruct
// CHECK-PUBLIC-NOT: IOIPublicStruct

@_spi(S)
@frozen public struct SPIFrozenStruct {
// CHECK-PRIVATE: struct SPIFrozenStruct
// CHECK-PUBLIC-NOT: SPIFrozenStruct

  var spiTypeInFrozen = SPIStruct()
  // CHECK-PRIVATE: @_spi(S) internal var spiTypeInFrozen

  private var spiTypeInFrozen1: SPIClass
  // CHECK-PRIVATE: @_spi(S) private var spiTypeInFrozen1
}

// The dummy conformance should be only in the private swiftinterface for
// SPI extensions.
@_spi(LocalSPI)
extension PublicType: SPIProto where T: PrivateConstraint {}
// CHECK-PRIVATE: extension {{.*}}.PublicType : {{.*}}.SPIProto where T : _ConstraintThatIsNotPartOfTheAPIOfThisLibrary
// CHECK-PUBLIC-NOT: _ConstraintThatIsNotPartOfTheAPIOfThisLibrary

// Preserve SPI information when printing indirect conformances via
// an internal protocol. rdar://73082943
@_spi(S) public protocol SPIProtocol {}
internal protocol InternalProtocol: SPIProtocol {}
public struct PublicStruct2: InternalProtocol {}
// CHECK-PRIVATE: @_spi(S) extension {{.*}}PublicStruct2 : {{.*}}.SPIProtocol
// CHECK-PUBLIC-NOT: SPIProtocol
