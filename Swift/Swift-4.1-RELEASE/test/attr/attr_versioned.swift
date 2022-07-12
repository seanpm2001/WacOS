// RUN: %target-typecheck-verify-swift
// RUN: %target-typecheck-verify-swift -enable-testing

@_versioned private func privateVersioned() {}
// expected-error@-1 {{'@_versioned' attribute can only be applied to internal declarations, but 'privateVersioned()' is private}}

@_versioned fileprivate func fileprivateVersioned() {}
// expected-error@-1 {{'@_versioned' attribute can only be applied to internal declarations, but 'fileprivateVersioned()' is fileprivate}}

@_versioned internal func internalVersioned() {}
// OK

@_versioned func implicitInternalVersioned() {}
// OK

@_versioned public func publicVersioned() {}
// expected-error@-1 {{'@_versioned' attribute can only be applied to internal declarations, but 'publicVersioned()' is public}}

internal class internalClass {
  @_versioned public func publicVersioned() {}
  // expected-error@-1 {{'@_versioned' attribute can only be applied to internal declarations, but 'publicVersioned()' is public}}
}

fileprivate class filePrivateClass {
  @_versioned internal func internalVersioned() {}
}

@_versioned struct S {
  var x: Int
  @_versioned var y: Int
}

@_versioned extension S {}
// expected-error@-1 {{'@_versioned' attribute cannot be applied to this declaration}}

@_versioned
protocol VersionedProtocol {
  associatedtype T

  func requirement() -> T

  public func publicRequirement() -> T
  // expected-error@-1 {{'public' modifier cannot be used in protocols}}

  @_versioned func versionedRequirement() -> T
  // expected-error@-1 {{'@_versioned' attribute cannot be used in protocols}}
}

// Derived conformances had issues with @_versioned - rdar://problem/34342955
@_versioned
internal enum EqEnum {
  case foo
}

@_versioned
internal enum RawEnum : Int {
  case foo = 0
}

@_inlineable
public func usesEqEnum() -> Bool {
  _ = (EqEnum.foo == .foo)
  _ = EqEnum.foo.hashValue

  _ = RawEnum.foo.rawValue
  _ = RawEnum(rawValue: 0)
}
