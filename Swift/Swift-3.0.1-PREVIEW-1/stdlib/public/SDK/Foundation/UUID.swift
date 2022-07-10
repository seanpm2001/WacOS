//===----------------------------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

@_exported import Foundation // Clang module
import Darwin.uuid

/// Represents UUID strings, which can be used to uniquely identify types, interfaces, and other items.
@available(OSX 10.8, iOS 6.0, *)
public struct UUID : ReferenceConvertible, Hashable, Equatable, CustomStringConvertible {
    public typealias ReferenceType = NSUUID

    public private(set) var uuid: uuid_t = (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    
    /* Create a new UUID with RFC 4122 version 4 random bytes */
    public init() {
        withUnsafeMutablePointer(to: &uuid) {
            uuid_generate_random(unsafeBitCast($0, to: UnsafeMutablePointer<UInt8>.self))
        }
    }
    
    fileprivate init(reference: NSUUID) {
        var bytes: uuid_t = (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        withUnsafeMutablePointer(to: &bytes) {
            reference.getBytes(unsafeBitCast($0, to: UnsafeMutablePointer<UInt8>.self))
        }
        uuid = bytes
    }

    /// Create a UUID from a string such as "E621E1F8-C36C-495A-93FC-0C247A3E6E5F".
    /// 
    /// Returns nil for invalid strings.
    public init?(uuidString string: String) {
        let res = withUnsafeMutablePointer(to: &uuid) {
            return uuid_parse(string, unsafeBitCast($0, to: UnsafeMutablePointer<UInt8>.self))
        }
        if res != 0 {
            return nil
        }
    }
    
    /// Create a UUID from a `uuid_t`.
    public init(uuid: uuid_t) {
        self.uuid = uuid
    }
    
    /// Returns a string created from the UUID, such as "E621E1F8-C36C-495A-93FC-0C247A3E6E5F"
    public var uuidString: String {
        var bytes: uuid_string_t = (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        var localValue = uuid
        return withUnsafeMutablePointer(to: &localValue) { val in
            withUnsafeMutablePointer(to: &bytes) { str in
                uuid_unparse(unsafeBitCast(val, to: UnsafePointer<UInt8>.self), unsafeBitCast(str, to: UnsafeMutablePointer<Int8>.self))
                return String(cString: unsafeBitCast(str, to: UnsafePointer<CChar>.self), encoding: .utf8)!
            }
        }
    }
    
    public var hashValue: Int {
        var localValue = uuid
        return withUnsafeMutablePointer(to: &localValue) {
            return Int(bitPattern: CFHashBytes(unsafeBitCast($0, to: UnsafeMutablePointer<UInt8>.self), CFIndex(MemoryLayout<uuid_t>.size)))
        }
    }
    
    public var description: String {
        return uuidString
    }

    public var debugDescription: String {
        return description
    }
    
    // MARK: - Bridging Support
    
    fileprivate var reference: NSUUID {
        var bytes = uuid
        return withUnsafePointer(to: &bytes) {
            return NSUUID(uuidBytes: unsafeBitCast($0, to: UnsafePointer<UInt8>.self))
        }
    }

    public static func ==(lhs: UUID, rhs: UUID) -> Bool {
        return lhs.uuid.0 == rhs.uuid.0 &&
            lhs.uuid.1 == rhs.uuid.1 &&
            lhs.uuid.2 == rhs.uuid.2 &&
            lhs.uuid.3 == rhs.uuid.3 &&
            lhs.uuid.4 == rhs.uuid.4 &&
            lhs.uuid.5 == rhs.uuid.5 &&
            lhs.uuid.6 == rhs.uuid.6 &&
            lhs.uuid.7 == rhs.uuid.7 &&
            lhs.uuid.8 == rhs.uuid.8 &&
            lhs.uuid.9 == rhs.uuid.9 &&
            lhs.uuid.10 == rhs.uuid.10 &&
            lhs.uuid.11 == rhs.uuid.11 &&
            lhs.uuid.12 == rhs.uuid.12 &&
            lhs.uuid.13 == rhs.uuid.13 &&
            lhs.uuid.14 == rhs.uuid.14 &&
            lhs.uuid.15 == rhs.uuid.15
    }
}

extension UUID : CustomReflectable {
    public var customMirror: Mirror {
        let c : [(label: String?, value: Any)] = []
        let m = Mirror(self, children:c, displayStyle: Mirror.DisplayStyle.struct)
        return m
    }
}

extension UUID : _ObjectiveCBridgeable {
    @_semantics("convertToObjectiveC")
    public func _bridgeToObjectiveC() -> NSUUID {
        return reference
    }
    
    public static func _forceBridgeFromObjectiveC(_ x: NSUUID, result: inout UUID?) {
        if !_conditionallyBridgeFromObjectiveC(x, result: &result) {
            fatalError("Unable to bridge \(_ObjectiveCType.self) to \(self)")
        }
    }
    
    public static func _conditionallyBridgeFromObjectiveC(_ input: NSUUID, result: inout UUID?) -> Bool {
        result = UUID(reference: input)
        return true
    }

    public static func _unconditionallyBridgeFromObjectiveC(_ source: NSUUID?) -> UUID {
        var result: UUID? = nil
        _forceBridgeFromObjectiveC(source!, result: &result)
        return result!
    }
}

extension NSUUID : _HasCustomAnyHashableRepresentation {
    // Must be @nonobjc to avoid infinite recursion during bridging.
    @nonobjc
    public func _toCustomAnyHashable() -> AnyHashable? {
        return AnyHashable(self as UUID)
    }
}

