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

@_exported import SceneKit // Clang module
import CoreGraphics
import simd

// MARK: Exposing SCNFloat

#if os(OSX)
public typealias SCNFloat = CGFloat
#elseif os(iOS) || os(tvOS) || os(watchOS)
public typealias SCNFloat = Float
#endif

// MARK: Working with SCNVector3

extension SCNVector3 {
  public init(_ x: Float, _ y: Float, _ z: Float) {
    self.x = SCNFloat(x)
    self.y = SCNFloat(y)
    self.z = SCNFloat(z)
  }
  public init(_ x: CGFloat, _ y: CGFloat, _ z: CGFloat) {
    self.x = SCNFloat(x)
    self.y = SCNFloat(y)
    self.z = SCNFloat(z)
  }
  public init(_ x: Double, _ y: Double, _ z: Double) {
    self.init(SCNFloat(x), SCNFloat(y), SCNFloat(z))
  }
  public init(_ x: Int, _ y: Int, _ z: Int) {
    self.init(SCNFloat(x), SCNFloat(y), SCNFloat(z))
  }
  public init(_ v: float3) {
    self.init(SCNFloat(v.x), SCNFloat(v.y), SCNFloat(v.z))
  }
  public init(_ v: double3) {
    self.init(SCNFloat(v.x), SCNFloat(v.y), SCNFloat(v.z))
  }
}

extension float3 {
  public init(_ v: SCNVector3) {
    self.init(Float(v.x), Float(v.y), Float(v.z))
  }
}

extension double3 {
  public init(_ v: SCNVector3) {
    self.init(Double(v.x), Double(v.y), Double(v.z))
  }
}

// MARK: Working with SCNVector4

extension SCNVector4 {
  public init(_ x: Float, _ y: Float, _ z: Float, _ w: Float) {
    self.x = SCNFloat(x)
    self.y = SCNFloat(y)
    self.z = SCNFloat(z)
    self.w = SCNFloat(w)
  }
  public init(_ x: CGFloat, _ y: CGFloat, _ z: CGFloat, _ w: CGFloat) {
    self.x = SCNFloat(x)
    self.y = SCNFloat(y)
    self.z = SCNFloat(z)
    self.w = SCNFloat(w)
  }
  public init(_ x: Double, _ y: Double, _ z: Double, _ w: Double) {
    self.init(SCNFloat(x), SCNFloat(y), SCNFloat(z), SCNFloat(w))
  }
  public init(_ x: Int, _ y: Int, _ z: Int, _ w: Int) {
    self.init(SCNFloat(x), SCNFloat(y), SCNFloat(z), SCNFloat(w))
  }
  public init(_ v: float4) {
    self.init(SCNFloat(v.x), SCNFloat(v.y), SCNFloat(v.z), SCNFloat(v.w))
  }
  public init(_ v: double4) {
    self.init(SCNFloat(v.x), SCNFloat(v.y), SCNFloat(v.z), SCNFloat(v.w))
  }
}

extension float4 {
  public init(_ v: SCNVector4) {
    self.init(Float(v.x), Float(v.y), Float(v.z), Float(v.w))
  }
}

extension double4 {
  public init(_ v: SCNVector4) {
    self.init(Double(v.x), Double(v.y), Double(v.z), Double(v.w))
  }
}

// MARK: Working with SCNMatrix4

extension SCNMatrix4 {
  public init(_ m: float4x4) {
    self.init(
      m11: SCNFloat(m[0,0]), m12: SCNFloat(m[0,1]), m13: SCNFloat(m[0,2]), m14: SCNFloat(m[0,3]),
      m21: SCNFloat(m[1,0]), m22: SCNFloat(m[1,1]), m23: SCNFloat(m[1,2]), m24: SCNFloat(m[1,3]),
      m31: SCNFloat(m[2,0]), m32: SCNFloat(m[2,1]), m33: SCNFloat(m[2,2]), m34: SCNFloat(m[2,3]),
      m41: SCNFloat(m[3,0]), m42: SCNFloat(m[3,1]), m43: SCNFloat(m[3,2]), m44: SCNFloat(m[3,3]))
  }
  public init(_ m: double4x4) {
    self.init(
      m11: SCNFloat(m[0,0]), m12: SCNFloat(m[0,1]), m13: SCNFloat(m[0,2]), m14: SCNFloat(m[0,3]),
      m21: SCNFloat(m[1,0]), m22: SCNFloat(m[1,1]), m23: SCNFloat(m[1,2]), m24: SCNFloat(m[1,3]),
      m31: SCNFloat(m[2,0]), m32: SCNFloat(m[2,1]), m33: SCNFloat(m[2,2]), m34: SCNFloat(m[2,3]),
      m41: SCNFloat(m[3,0]), m42: SCNFloat(m[3,1]), m43: SCNFloat(m[3,2]), m44: SCNFloat(m[3,3]))
  }
}

extension float4x4 {
  public init(_ m: SCNMatrix4) {
    self.init([
      float4(Float(m.m11), Float(m.m12), Float(m.m13), Float(m.m14)),
      float4(Float(m.m21), Float(m.m22), Float(m.m23), Float(m.m24)),
      float4(Float(m.m31), Float(m.m32), Float(m.m33), Float(m.m34)),
      float4(Float(m.m41), Float(m.m42), Float(m.m43), Float(m.m44))
      ])
  }
}

extension double4x4 {
  public init(_ m: SCNMatrix4) {
    self.init([
      double4(Double(m.m11), Double(m.m12), Double(m.m13), Double(m.m14)),
      double4(Double(m.m21), Double(m.m22), Double(m.m23), Double(m.m24)),
      double4(Double(m.m31), Double(m.m32), Double(m.m33), Double(m.m34)),
      double4(Double(m.m41), Double(m.m42), Double(m.m43), Double(m.m44))
      ])
  }
}

// MARK: Swift Extensions

@available(iOS, introduced: 8.0)
@available(OSX, introduced: 10.8)
extension SCNGeometryElement {
  /// Creates an instance from `indices` for a `primitiveType`
  /// that has a constant number of indices per primitive
  /// - Precondition: the `primitiveType` must be `.triangles`, `.triangleStrip`, `.line` or `.point`
  public convenience init<IndexType : Integer>(
    indices: [IndexType], primitiveType: SCNGeometryPrimitiveType
  ) {
    precondition(primitiveType == .triangles || primitiveType == .triangleStrip || primitiveType == .line || primitiveType == .point, "Expected constant number of indices per primitive")
    let indexCount = indices.count
    let primitiveCount: Int
    switch primitiveType {
    case .triangles:
      primitiveCount = indexCount / 3
    case .triangleStrip:
      primitiveCount = indexCount - 2
    case .line:
      primitiveCount = indexCount / 2
    case .point:
      primitiveCount = indexCount
    case .polygon:
      fatalError("Expected constant number of indices per primitive")
    }
    self.init(
      data: Data(bytes: indices, count: indexCount * MemoryLayout<IndexType>.stride),
      primitiveType: primitiveType,
      primitiveCount: primitiveCount,
      bytesPerIndex: MemoryLayout<IndexType>.stride)
    _fixLifetime(indices)
  }
}

@available(iOS, introduced: 8.0)
@available(OSX, introduced: 10.8)
extension SCNGeometrySource {
  public convenience init(vertices: [SCNVector3]) {
    self.init(vertices: UnsafePointer(vertices), count: vertices.count)
  }
  public convenience init(normals: [SCNVector3]) {
    self.init(normals: UnsafePointer(normals), count: normals.count)
  }
  public convenience init(textureCoordinates: [CGPoint]) {
    self.init(textureCoordinates: UnsafePointer(textureCoordinates), count: textureCoordinates.count)
  }
}

@available(iOS, introduced: 8.0)
@available(OSX, introduced: 10.10)
extension SCNBoundingVolume {
  public var boundingBox: (min: SCNVector3, max: SCNVector3) {
    get {
      var min = SCNVector3Zero
      var max = SCNVector3Zero
      __getBoundingBoxMin(&min, max: &max)
      return (min: min, max: max)
    }
    set {
      var min = newValue.min
      var max = newValue.max
      __setBoundingBoxMin(&min, max: &max)
    }
  }
  public var boundingSphere: (center: SCNVector3, radius: Float) {
    var center = SCNVector3Zero
    var radius = CGFloat(0.0)
    __getBoundingSphereCenter(&center, radius: &radius)
    return (center: center, radius: Float(radius))
  }
}

// MARK: APIs refined for Swift

@_silgen_name("SCN_Swift_SCNSceneSource_entryWithIdentifier")
internal func SCN_Swift_SCNSceneSource_entryWithIdentifier(
  _ self_: AnyObject,
  _ uid: NSString,
  _ entryClass: AnyObject) -> AnyObject?

@available(iOS, introduced: 8.0)
@available(OSX, introduced: 10.8)
extension SCNSceneSource {
  public func entryWithIdentifier<T>(_ uid: String, withClass entryClass: T.Type) -> T? {
    return SCN_Swift_SCNSceneSource_entryWithIdentifier(
      self, uid as NSString, entryClass as! AnyClass) as! T?
  }
}

