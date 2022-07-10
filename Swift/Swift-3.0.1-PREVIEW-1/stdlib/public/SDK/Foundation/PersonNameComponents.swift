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

@available(OSX 10.11, iOS 9.0, *)
public struct PersonNameComponents : ReferenceConvertible, Hashable, Equatable, _MutableBoxing {
    public typealias ReferenceType = NSPersonNameComponents
    internal var _handle: _MutableHandle<NSPersonNameComponents>
    
    public init() {
        _handle = _MutableHandle(adoptingReference: NSPersonNameComponents())
    }
    
    fileprivate init(reference: NSPersonNameComponents) {
        _handle = _MutableHandle(reference: reference)
    }

    /* The below examples all assume the full name Dr. Johnathan Maple Appleseed Esq., nickname "Johnny" */
    
    /* Pre-nominal letters denoting title, salutation, or honorific, e.g. Dr., Mr. */
    public var namePrefix: String? {
        get { return _handle.map { $0.namePrefix } }
        set { _applyMutation { $0.namePrefix = newValue } }
    }
    
    /* Name bestowed upon an individual by one's parents, e.g. Johnathan */
    public var givenName: String? {
        get { return _handle.map { $0.givenName } }
        set { _applyMutation { $0.givenName = newValue } }
    }
    
    /* Secondary given name chosen to differentiate those with the same first name, e.g. Maple  */
    public var middleName: String? {
        get { return _handle.map { $0.middleName } }
        set { _applyMutation { $0.middleName = newValue } }
    }
    
    /* Name passed from one generation to another to indicate lineage, e.g. Appleseed  */
    public var familyName: String? {
        get { return _handle.map { $0.familyName } }
        set { _applyMutation { $0.familyName = newValue } }
    }
    
    /* Post-nominal letters denoting degree, accreditation, or other honor, e.g. Esq., Jr., Ph.D. */
    public var nameSuffix: String? {
        get { return _handle.map { $0.nameSuffix } }
        set { _applyMutation { $0.nameSuffix = newValue } }
    }
    
    /* Name substituted for the purposes of familiarity, e.g. "Johnny"*/
    public var nickname: String? {
        get { return _handle.map { $0.nickname } }
        set { _applyMutation { $0.nickname = newValue } }
    }
    
    /* Each element of the phoneticRepresentation should correspond to an element of the original PersonNameComponents instance.
     The phoneticRepresentation of the phoneticRepresentation object itself will be ignored. nil by default, must be instantiated.
     */
    public var phoneticRepresentation: PersonNameComponents? {
        get { return _handle.map { $0.phoneticRepresentation } }
        set { _applyMutation { $0.phoneticRepresentation = newValue } }
    }
    
    public var hashValue : Int {
        return _handle.map { $0.hash }
    }
    
    @available(OSX 10.11, iOS 9.0, *)
    public static func ==(lhs : PersonNameComponents, rhs: PersonNameComponents) -> Bool {
        // Don't copy references here; no one should be storing anything
        return lhs._handle._uncopiedReference().isEqual(rhs._handle._uncopiedReference())
    }
}

@available(OSX 10.11, iOS 9.0, *)
extension PersonNameComponents : CustomStringConvertible, CustomDebugStringConvertible, CustomReflectable {
    public var description: String {
        return self.customMirror.children.reduce("") {
            $0.appending("\($1.label ?? ""): \($1.value) ")
        }
    }
    
    public var debugDescription: String {
        return self.description
    }

    public var customMirror: Mirror {
        var c: [(label: String?, value: Any)] = []
        if let r = namePrefix { c.append((label: "namePrefix", value: r)) }
        if let r = givenName { c.append((label: "givenName", value: r)) }
        if let r = middleName { c.append((label: "middleName", value: r)) }
        if let r = familyName { c.append((label: "familyName", value: r)) }
        if let r = nameSuffix { c.append((label: "nameSuffix", value: r)) }
        if let r = nickname { c.append((label: "nickname", value: r)) }
        if let r = phoneticRepresentation { c.append((label: "phoneticRepresentation", value: r)) }
        return Mirror(self, children: c, displayStyle: Mirror.DisplayStyle.struct)
    }
}

@available(OSX 10.11, iOS 9.0, *)
extension PersonNameComponents : _ObjectiveCBridgeable {
    public static func _getObjectiveCType() -> Any.Type {
        return NSPersonNameComponents.self
    }
    
    @_semantics("convertToObjectiveC")
    public func _bridgeToObjectiveC() -> NSPersonNameComponents {
        return _handle._copiedReference()
    }

    public static func _forceBridgeFromObjectiveC(_ personNameComponents: NSPersonNameComponents, result: inout PersonNameComponents?) {
        if !_conditionallyBridgeFromObjectiveC(personNameComponents, result: &result) {
            fatalError("Unable to bridge \(_ObjectiveCType.self) to \(self)")
        }
    }
    
    public static func _conditionallyBridgeFromObjectiveC(_ personNameComponents: NSPersonNameComponents, result: inout PersonNameComponents?) -> Bool {
        result = PersonNameComponents(reference: personNameComponents)
        return true
    }

    public static func _unconditionallyBridgeFromObjectiveC(_ source: NSPersonNameComponents?) -> PersonNameComponents {
        var result: PersonNameComponents? = nil
        _forceBridgeFromObjectiveC(source!, result: &result)
        return result!
    }
}

@available(OSX 10.11, iOS 9.0, *)
extension NSPersonNameComponents : _HasCustomAnyHashableRepresentation {
    // Must be @nonobjc to avoid infinite recursion during bridging.
    @nonobjc
    public func _toCustomAnyHashable() -> AnyHashable? {
        return AnyHashable(self as PersonNameComponents)
    }
}

