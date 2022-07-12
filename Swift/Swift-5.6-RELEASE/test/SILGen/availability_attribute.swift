// RUN: %target-swift-emit-silgen %s | %FileCheck %s
// RUN: %target-swift-emit-silgen %s -target %target-cpu-apple-macosx10.50 | %FileCheck %s
// RUN: %target-swift-emit-silgen %s -target %target-cpu-apple-macosx10.60 | %FileCheck %s
// REQUIRES: OS=macosx

// CHECK-LABEL: sil [available 10.50] [ossa] @$s22availability_attribute17availableFunctionyyF : $@convention(thin) () -> ()
@available(macOS 10.50, *) public func availableFunction() {}

public struct Struct {
    // CHECK-LABEL: sil [available 10.50] [ossa] @$s22availability_attribute6StructV12availableVarSivg : $@convention(method) (Struct) -> Int
    @available(macOS 10.50, *)
    public var availableVar: Int { return 0 }

    // CHECK-LABEL: sil [available 10.50] [ossa] @$s22availability_attribute6StructV24varWithAvailableAccessorSivg : $@convention(method) (Struct) -> Int
    public var varWithAvailableAccessor: Int {
        @available(macOS 10.50, *)
        get {
            return 0
        }
    }
}

@available(macOS 10.50, *)
extension Struct {
    // CHECK-LABEL: sil [available 10.50] [ossa] @$s22availability_attribute6StructV24availableExtensionMethodyyF : $@convention(method) (Struct) -> ()
    public func availableExtensionMethod() {}
}

@available(macOS 10.50, *)
public struct AvailableStruct {
    public func availableMethod() {}

    public struct Nested {
        public func availableNestedMethod() {}
    }
}

@_specialize(exported: true, availability: macOS 10.50, iOS 13, *; where T == Int)
// CHECK-LABEL: sil [_specialize exported: true, kind: full, available: 10.50, where T == Int] [ossa] @$s22availability_attribute16testAvailabilityyyxlF
public func testAvailability<T>(_ t: T) {}
