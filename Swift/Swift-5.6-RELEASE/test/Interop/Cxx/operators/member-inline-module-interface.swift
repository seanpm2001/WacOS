// RUN: %target-swift-ide-test -print-module -module-to-print=MemberInline -I %S/Inputs -source-filename=x -enable-cxx-interop | %FileCheck %s

// CHECK: struct LoadableIntWrapper {
// CHECK:   static func - (lhs: inout LoadableIntWrapper, rhs: LoadableIntWrapper) -> LoadableIntWrapper
// CHECK:   mutating func callAsFunction() -> Int32
// CHECK:   mutating func callAsFunction(_ x: Int32) -> Int32
// CHECK:   mutating func callAsFunction(_ x: Int32, _ y: Int32) -> Int32
// CHECK: }

// CHECK: struct AddressOnlyIntWrapper {
// CHECK:   mutating func callAsFunction() -> Int32
// CHECK:   mutating func callAsFunction(_ x: Int32) -> Int32
// CHECK:   mutating func callAsFunction(_ x: Int32, _ y: Int32) -> Int32
// CHECK: }

// CHECK: struct HasDeletedOperator {
// CHECK: }


// CHECK: struct ReadWriteIntArray {
// CHECK:   subscript(x: Int32) -> Int32

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   func __operatorSubscriptConst(_ x: Int32) -> UnsafePointer<Int32>

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   mutating func __operatorSubscript(_ x: Int32) -> UnsafeMutablePointer<Int32>

// CHECK:   struct NestedIntArray {
// CHECK:     subscript(x: Int32) -> Int32 { get }

// CHECK:     @available(*, unavailable, message: "use subscript")
// CHECK:     func __operatorSubscriptConst(_ x: Int32) -> UnsafePointer<Int32>
// CHECK:   }
// CHECK: }


// CHECK: struct ReadOnlyIntArray {
// CHECK:   subscript(x: Int32) -> Int32 { get }

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   func __operatorSubscriptConst(_ x: Int32) -> UnsafePointer<Int32>
// CHECK: }


// CHECK: struct WriteOnlyIntArray {
// CHECK:   subscript(x: Int32) -> Int32 { mutating get set }

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   mutating func __operatorSubscript(_ x: Int32) -> UnsafeMutablePointer<Int32>
// CHECK: }


// CHECK: struct DifferentTypesArray {
// CHECK:   subscript(x: Int32) -> Int32
// CHECK:   subscript(x: Bool) -> Bool
// CHECK:   subscript(x: Double) -> Double { get }

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   func __operatorSubscriptConst(_ x: Int32) -> UnsafePointer<Int32>

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   mutating func __operatorSubscript(_ x: Int32) -> UnsafeMutablePointer<Int32>

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   mutating func __operatorSubscript(_ x: Bool) -> UnsafeMutablePointer<Bool>

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   func __operatorSubscriptConst(_ x: Bool) -> UnsafePointer<Bool>

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   func __operatorSubscriptConst(_ x: Double) -> UnsafePointer<Double>

// CHECK: }


// CHECK: struct TemplatedArray<T> {
// CHECK: }
// CHECK: struct __CxxTemplateInst14TemplatedArrayIdE {
// CHECK:   subscript(i: Int32) -> Double

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   mutating func __operatorSubscript(_ i: Int32) -> UnsafeMutablePointer<Double>

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   func __operatorSubscriptConst(_ i: Int32) -> UnsafePointer<Double>
// CHECK: }
// CHECK: typealias TemplatedDoubleArray = __CxxTemplateInst14TemplatedArrayIdE


// CHECK: struct TemplatedSubscriptArray {
// CHECK: }


// CHECK: struct IntArrayByVal {
// CHECK:   subscript(x: Int32) -> Int32 { get }
// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   func __operatorSubscriptConst(_ x: Int32) -> Int32
// CHECK: }

// CHECK: struct NonTrivialIntArrayByVal {
// CHECK:   subscript(x: Int32) -> Int32 { get }
// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   func __operatorSubscriptConst(_ x: Int32) -> Int32
// CHECK: }

// CHECK: struct DifferentTypesArrayByVal {
// CHECK:   subscript(x: Int32) -> Int32 { mutating get }
// CHECK:   subscript(x: Bool) -> Bool { mutating get }
// CHECK:   subscript(x: Double) -> Double { get }

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   func __operatorSubscriptConst(_ x: Int32) -> Int32

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   func __operatorSubscriptConst(_ x: Bool) -> Bool

// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   func __operatorSubscriptConst(_ x: Double) -> Double
// CHECK: }


// CHECK: struct TemplatedArrayByVal<T> {
// CHECK: }
// CHECK: struct __CxxTemplateInst19TemplatedArrayByValIdE {
// CHECK:   subscript(i: Int32) -> Double { mutating get }
// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   mutating func __operatorSubscriptConst(_ i: Int32) -> Double
// CHECK: }
// CHECK: typealias TemplatedDoubleArrayByVal = __CxxTemplateInst19TemplatedArrayByValIdE


// CHECK: struct TemplatedSubscriptArrayByVal {
// CHECK:   subscript(i: T) -> T { mutating get }
// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   mutating func __operatorSubscriptConst<T>(_ i: T) -> T
// CHECK: }

// CHECK: struct NonTrivialArrayByVal {
// CHECK:   subscript(x: Int32) -> NonTrivial { mutating get }
// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   mutating func __operatorSubscriptConst(_ x: Int32) -> NonTrivial
// CHECK: }
// CHECK: struct PtrByVal {
// CHECK:   subscript(x: Int32) -> UnsafeMutablePointer<Int32>! { mutating get }
// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   mutating func __operatorSubscript(_ x: Int32) -> UnsafeMutablePointer<Int32>!
// CHECK: }
// CHECK: struct RefToPtr {
// CHECK:   subscript(x: Int32) -> UnsafeMutablePointer<Int32>? { mutating get set }
// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   mutating func __operatorSubscript(_ x: Int32) -> UnsafeMutablePointer<UnsafeMutablePointer<Int32>?>
// CHECK: }
// CHECK: struct PtrToPtr {
// CHECK:   subscript(x: Int32) -> UnsafeMutablePointer<UnsafeMutablePointer<Int32>?>! { mutating get }
// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   mutating func __operatorSubscript(_ x: Int32) -> UnsafeMutablePointer<UnsafeMutablePointer<Int32>?>!
// CHECK: }
// CHECK: struct ConstOpPtrByVal {
// CHECK:   subscript(x: Int32) -> UnsafePointer<Int32>! { get }
// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   func __operatorSubscriptConst(_ x: Int32) -> UnsafePointer<Int32>!
// CHECK: }
// CHECK: struct ConstPtrByVal {
// CHECK:   subscript(x: Int32) -> UnsafePointer<Int32>! { mutating get }
// CHECK:   @available(*, unavailable, message: "use subscript")
// CHECK:   mutating func __operatorSubscriptConst(_ x: Int32) -> UnsafePointer<Int32>!
// CHECK: }
