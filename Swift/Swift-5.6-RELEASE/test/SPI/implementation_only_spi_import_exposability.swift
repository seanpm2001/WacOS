/// @_implementationOnly imported decls (SPI or not) should not be exposed in SPI.

// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -emit-module -DLIB %s -module-name Lib -emit-module-path %t/Lib.swiftmodule
// RUN: %target-typecheck-verify-swift -DCLIENT -I %t

#if LIB

@_spi(A) public func spiFunc() {}

@_spi(A) public struct SPIStruct {
  public init() {}
}

@_spi(A) public protocol SPIProtocol {}

public func ioiFunc() {}

public struct IOIStruct {
  public init() {}
}

public protocol IOIProtocol {}

#elseif CLIENT

@_spi(A) @_implementationOnly import Lib

@_spi(B) public func leakSPIStruct(_ a: SPIStruct) -> SPIStruct { fatalError() } // expected-warning 2 {{cannot use struct 'SPIStruct' in SPI; 'Lib' has been imported as implementation-only}}
@_spi(B) public func leakIOIStruct(_ a: IOIStruct) -> IOIStruct { fatalError() } // expected-warning 2 {{cannot use struct 'IOIStruct' in SPI; 'Lib' has been imported as implementation-only}}

public struct PublicStruct : IOIProtocol, SPIProtocol { // expected-error {{cannot use protocol 'IOIProtocol' here; 'Lib' has been imported as implementation-only}}
// expected-error @-1 {{cannot use protocol 'SPIProtocol' here; 'Lib' has been imported as implementation-only}}
  public var spiStruct = SPIStruct() // expected-error {{cannot use struct 'SPIStruct' here; 'Lib' has been imported as implementation-only}}
  public var ioiStruct = IOIStruct() // expected-error {{cannot use struct 'IOIStruct' here; 'Lib' has been imported as implementation-only}}

  @inlinable
  public func publicInlinable() {
    spiFunc() // expected-error {{global function 'spiFunc()' cannot be used in an '@inlinable' function because 'Lib' was imported implementation-only}}
    ioiFunc() // expected-error {{global function 'ioiFunc()' cannot be used in an '@inlinable' function because 'Lib' was imported implementation-only}}
    let _ = SPIStruct() // expected-error {{struct 'SPIStruct' cannot be used in an '@inlinable' function because 'Lib' was imported implementation-only}}
    // expected-error@-1 {{initializer 'init()' cannot be used in an '@inlinable' function because 'Lib' was imported implementation-only}}
    let _ = IOIStruct() // expected-error {{struct 'IOIStruct' cannot be used in an '@inlinable' function because 'Lib' was imported implementation-only}}
    // expected-error@-1 {{initializer 'init()' cannot be used in an '@inlinable' function because 'Lib' was imported implementation-only}}
  }
}

@_spi(B)
public struct LocalSPIStruct : IOIProtocol, SPIProtocol { // expected-warning {{cannot use protocol 'IOIProtocol' in SPI; 'Lib' has been imported as implementation-only}}
// expected-warning @-1 {{cannot use protocol 'SPIProtocol' in SPI; 'Lib' has been imported as implementation-only}}
}

#endif
