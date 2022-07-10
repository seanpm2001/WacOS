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

extension Decimal {
    public typealias RoundingMode = NSDecimalNumber.RoundingMode
    public typealias CalculationError = NSDecimalNumber.CalculationError

    public static let leastFiniteMagnitude = Decimal(_exponent: 127, _length: 8, _isNegative: 1, _isCompact: 1, _reserved: 0, _mantissa: (0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff))
    public static let greatestFiniteMagnitude = Decimal(_exponent: 127, _length: 8, _isNegative: 0, _isCompact: 1, _reserved: 0, _mantissa: (0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff))
    public static let leastNormalMagnitude = Decimal(_exponent: -127, _length: 1, _isNegative: 0, _isCompact: 1, _reserved: 0, _mantissa: (0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000))
    public static let leastNonzeroMagnitude = Decimal(_exponent: -127, _length: 1, _isNegative: 0, _isCompact: 1, _reserved: 0, _mantissa: (0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000))

    public static let pi = Decimal(_exponent: -38, _length: 8, _isNegative: 0, _isCompact: 1, _reserved: 0, _mantissa: (0x6623, 0x7d57, 0x16e7, 0xad0d, 0xaf52, 0x4641, 0xdfa7, 0xec58))

    public var exponent: Int { 
        get {
            return Int(_exponent)
        }
    }

    public var significand: Decimal { 
        get {
            return Decimal(_exponent: 0, _length: _length, _isNegative: _isNegative, _isCompact: _isCompact, _reserved: 0, _mantissa: _mantissa)
        }
    }

    public init(sign: FloatingPointSign, exponent: Int, significand: Decimal) {
        self.init(_exponent: Int32(exponent) + significand._exponent, _length: significand._length, _isNegative: sign == .plus ? 0 : 1, _isCompact: significand._isCompact, _reserved: 0, _mantissa: significand._mantissa)
    }

    public init(signOf: Decimal, magnitudeOf magnitude: Decimal) {
        self.init(_exponent: magnitude._exponent, _length: magnitude._length, _isNegative: signOf._isNegative, _isCompact: magnitude._isCompact, _reserved: 0, _mantissa: magnitude._mantissa)
    }

    public var sign: FloatingPointSign { 
        return _isNegative == 0 ? FloatingPointSign.plus : FloatingPointSign.minus
    }

    public static var radix: Int { return 10 }

    public var ulp: Decimal {
        if !self.isFinite { return Decimal.nan }
        return Decimal(_exponent: _exponent, _length: 8, _isNegative: 0, _isCompact: 1, _reserved: 0, _mantissa: (0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000))
    }

    @available(*, unavailable, message: "Decimal does not yet fully adopt FloatingPoint.")
    public mutating func formTruncatingRemainder(dividingBy other: Decimal) { fatalError("Decimal does not yet fully adopt FloatingPoint") }

    public mutating func add(_ other: Decimal) {
        var rhs = other
        NSDecimalAdd(&self, &self, &rhs, .plain)
    }

    public mutating func subtract(_ other: Decimal) {
        var rhs = other
        NSDecimalSubtract(&self, &self, &rhs, .plain)
    }

    public mutating func multiply(by other: Decimal) {
        var rhs = other
        NSDecimalMultiply(&self, &self, &rhs, .plain)
    }

    public mutating func divide(by other: Decimal) {
        var rhs = other
        NSDecimalDivide(&self, &self, &rhs, .plain)
    }

    public mutating func negate() {
        _isNegative = _isNegative == 0 ? 1 : 0
    }

    public func isEqual(to other: Decimal) -> Bool {
        var lhs = self
        var rhs = other
        return NSDecimalCompare(&lhs, &rhs) == .orderedSame
    }

    public func isLess(than other: Decimal) -> Bool {
        var lhs = self
        var rhs = other
        return NSDecimalCompare(&lhs, &rhs) == .orderedAscending
    }

    public func isLessThanOrEqualTo(_ other: Decimal) -> Bool {
        var lhs = self
        var rhs = other
        let order = NSDecimalCompare(&lhs, &rhs)
        return order == .orderedAscending || order == .orderedSame
    }

    public func isTotallyOrdered(belowOrEqualTo other: Decimal) -> Bool {
        // Notes: Decimal does not have -0 or infinities to worry about
        if self.isNaN {
            return false
        } else if self < other {
            return true
        } else if other < self {
            return false
        }
        // fall through to == behavior
        return true
    }

    public var isCanonical: Bool {
        return true
    }

    public var nextUp: Decimal {
        return self + Decimal(_exponent: _exponent, _length: 1, _isNegative: 0, _isCompact: 1, _reserved: 0, _mantissa: (0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000))
    }

    public var nextDown: Decimal {
        return self - Decimal(_exponent: _exponent, _length: 1, _isNegative: 0, _isCompact: 1, _reserved: 0, _mantissa: (0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000))
    }

    public static func +(lhs: Decimal, rhs: Decimal) -> Decimal {
        var res = Decimal()
        var leftOp = lhs
        var rightOp = rhs
        NSDecimalAdd(&res, &leftOp, &rightOp, .plain)
        return res
    }

    public static func -(lhs: Decimal, rhs: Decimal) -> Decimal {
        var res = Decimal()
        var leftOp = lhs
        var rightOp = rhs
        NSDecimalSubtract(&res, &leftOp, &rightOp, .plain)
        return res
    }

    public static func /(lhs: Decimal, rhs: Decimal) -> Decimal {
        var res = Decimal()
        var leftOp = lhs
        var rightOp = rhs
        NSDecimalDivide(&res, &leftOp, &rightOp, .plain)
        return res
    }

    public static func *(lhs: Decimal, rhs: Decimal) -> Decimal {
        var res = Decimal()
        var leftOp = lhs
        var rightOp = rhs
        NSDecimalMultiply(&res, &leftOp, &rightOp, .plain)
        return res
    }

}

public func pow(_ x: Decimal, _ y: Int) -> Decimal {
    var res = Decimal()
    var num = x
    NSDecimalPower(&res, &num, y, .plain)
    return res
}

extension Decimal : Hashable, Comparable {
    internal var doubleValue : Double {
        var d = 0.0
        if _length == 0 && _isNegative == 0 {
            return Double.nan
        }
        
        for i in 0..<8 {
            let index = 8 - i - 1
            switch index {
            case 0:
                d = d * 65536 + Double(_mantissa.0)
                break
            case 1:
                d = d * 65536 + Double(_mantissa.1)
                break
            case 2:
                d = d * 65536 + Double(_mantissa.2)
                break
            case 3:
                d = d * 65536 + Double(_mantissa.3)
                break
            case 4:
                d = d * 65536 + Double(_mantissa.4)
                break
            case 5:
                d = d * 65536 + Double(_mantissa.5)
                break
            case 6:
                d = d * 65536 + Double(_mantissa.6)
                break
            case 7:
                d = d * 65536 + Double(_mantissa.7)
                break
            default:
                fatalError("conversion overflow")
            }
        }

        if _exponent < 0 {
            for _ in _exponent..<0 {
                d /= 10.0
            }
        } else {
            for _ in 0..<_exponent {
                d *= 10.0
            }
        }
        return _isNegative != 0 ? -d : d
    }

    public var hashValue: Int {
        return Int(bitPattern: __CFHashDouble(doubleValue))
    }

    public static func ==(lhs: Decimal, rhs: Decimal) -> Bool {
        var lhsVal = lhs
        var rhsVal = rhs
        return NSDecimalCompare(&lhsVal, &rhsVal) == .orderedSame
    }

    public static func <(lhs: Decimal, rhs: Decimal) -> Bool {
        var lhsVal = lhs
        var rhsVal = rhs
        return NSDecimalCompare(&lhsVal, &rhsVal) == .orderedAscending
    }
}

extension Decimal : ExpressibleByFloatLiteral {
    public init(floatLiteral value: Double) {
        self.init(value)
    }
}

extension Decimal : ExpressibleByIntegerLiteral {
    public init(integerLiteral value: Int) {
        self.init(value)
    }
}

extension Decimal : SignedNumber { }

extension Decimal : Strideable {
    public func distance(to other: Decimal) -> Decimal {
        return self - other
    }

    public func advanced(by n: Decimal) -> Decimal {
        return self + n
    }
}

extension Decimal : AbsoluteValuable {
    public static func abs(_ x: Decimal) -> Decimal {
        return Decimal(_exponent: x._exponent, _length: x._length, _isNegative: 0, _isCompact: x._isCompact, _reserved: 0, _mantissa: x._mantissa)
    }
}

extension Decimal {
    public init(_ value: UInt8) {
        self.init(UInt64(value))
    }
    
    public init(_ value: Int8) {
        self.init(Int64(value))
    }
    
    public init(_ value: UInt16) {
        self.init(UInt64(value))
    }
    
    public init(_ value: Int16) {
        self.init(Int64(value))
    }
    
    public init(_ value: UInt32) {
        self.init(UInt64(value))
    }
    
    public init(_ value: Int32) {
        self.init(Int64(value))
    }
    
    public init(_ value: Double) {
        if value.isNaN {
            self = Decimal.nan
        } else if value == 0.0 {
            self = Decimal(_exponent: 0, _length: 0, _isNegative: 0, _isCompact: 0, _reserved: 0, _mantissa: (0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000))
        } else {
            let negative = value < 0
            var val = negative ? -1 * value : value
            var exponent = 0
            while val < Double(UInt64.max - 1) {
                val *= 10.0
                exponent -= 1
            }
            while Double(UInt64.max - 1) < val {
                val /= 10.0
                exponent += 1
            }
            var mantissa = UInt64(val)
            
            var i = UInt32(0)
            // this is a bit ugly but it is the closest approximation of the C initializer that can be expressed here.
            _mantissa = (0, 0, 0, 0, 0, 0, 0, 0)
            while mantissa != 0 && i < 8 /* NSDecimalMaxSize */ {
                switch i {
                    case 0:
                        _mantissa.0 = UInt16(mantissa & 0xffff)
                        break
                    case 1:
                        _mantissa.1 = UInt16(mantissa & 0xffff)
                        break
                    case 2:
                        _mantissa.2 = UInt16(mantissa & 0xffff)
                        break
                    case 3:
                        _mantissa.3 = UInt16(mantissa & 0xffff)
                        break
                    case 4:
                        _mantissa.4 = UInt16(mantissa & 0xffff)
                        break
                    case 5:
                        _mantissa.5 = UInt16(mantissa & 0xffff)
                        break
                    case 6:
                        _mantissa.6 = UInt16(mantissa & 0xffff)
                        break
                    case 7:
                        _mantissa.7 = UInt16(mantissa & 0xffff)
                        break
                    default:
                        fatalError("initialization overflow")
                }
                mantissa = mantissa >> 16
                i += 1
            }
            _length = i
            _isNegative = negative ? 1 : 0
            _isCompact = 0
            _exponent = Int32(exponent)
            NSDecimalCompact(&self)
        }
    }
    
    public init(_ value: UInt64) {
        self.init(Double(value))
    }
    
    public init(_ value: Int64) {
        self.init(Double(value))
    }
    
    public init(_ value: UInt) {
        self.init(UInt64(value))
    }
    
    public init(_ value: Int) {
        self.init(Int64(value))
    }
    
    @available(*, unavailable, message: "Decimal does not yet fully adopt FloatingPoint.")
    public static var infinity: Decimal { fatalError("Decimal does not yet fully adopt FloatingPoint") }
    
    @available(*, unavailable, message: "Decimal does not yet fully adopt FloatingPoint.")
    public static var signalingNaN: Decimal { fatalError("Decimal does not yet fully adopt FloatingPoint") }

    public var isSignalingNaN: Bool { 
        return false
    } 

    public static var nan: Decimal {
        return quietNaN
    }
    
    public static var quietNaN: Decimal {
        return Decimal(_exponent: 0, _length: 0, _isNegative: 1, _isCompact: 0, _reserved: 0, _mantissa: (0, 0, 0, 0, 0, 0, 0, 0))
    }

    /// The IEEE 754 "class" of this type.
    public var floatingPointClass: FloatingPointClassification {
        if _length == 0 && _isNegative == 1 {
            return .quietNaN
        } else if _length == 0 {
            return .positiveZero
        }
        // NSDecimal does not really represent normal and subnormal in the same manner as the IEEE standard, for now we can probably claim normal for any nonzero, nonnan values
        if _isNegative == 1 {
            return .negativeNormal
        } else {
            return .positiveNormal
        }
    }
    /// `true` iff `self` is negative.
    public var isSignMinus: Bool { return _isNegative != 0 }
    /// `true` iff `self` is normal (not zero, subnormal, infinity, or
    /// NaN).
    public var isNormal: Bool { return !isZero && !isInfinite && !isNaN }
    /// `true` iff `self` is zero, subnormal, or normal (not infinity
    /// or NaN).
    public var isFinite: Bool { return !isNaN }
    /// `true` iff `self` is +0.0 or -0.0.
    public var isZero: Bool { return _length == 0 && _isNegative == 0 }
    /// `true` iff `self` is subnormal.
    public var isSubnormal: Bool { return false }
    /// `true` iff `self` is infinity.
    public var isInfinite: Bool { return false }
    /// `true` iff `self` is NaN.
    public var isNaN: Bool { return _length == 0 && _isNegative == 1 }
    /// `true` iff `self` is a signaling NaN.
    public var isSignaling: Bool { return false }
}

extension Decimal : CustomStringConvertible {
    public init?(string: String, locale: Locale? = nil) {
        let scan = Scanner(string: string)
        var theDecimal = Decimal()
        scan.locale = locale
        if !scan.scanDecimal(&theDecimal) {
            return nil
        }
        self = theDecimal
    }
    
    public var description: String {
        var val = self
        return NSDecimalString(&val, nil)
    }
}

extension Decimal : _ObjectiveCBridgeable {
    @_semantics("convertToObjectiveC")
    public func _bridgeToObjectiveC() -> NSDecimalNumber {
        return NSDecimalNumber(decimal: self)
    }
    
    public static func _forceBridgeFromObjectiveC(_ x: NSDecimalNumber, result: inout Decimal?) {
        if !_conditionallyBridgeFromObjectiveC(x, result: &result) {
            fatalError("Unable to bridge \(_ObjectiveCType.self) to \(self)")
        }
    }
    
    public static func _conditionallyBridgeFromObjectiveC(_ input: NSDecimalNumber, result: inout Decimal?) -> Bool {
        result = input.decimalValue
        return true
    }

    public static func _unconditionallyBridgeFromObjectiveC(_ source: NSDecimalNumber?) -> Decimal {
        var result: Decimal? = nil
        _forceBridgeFromObjectiveC(source!, result: &result)
        return result!
    }
}
