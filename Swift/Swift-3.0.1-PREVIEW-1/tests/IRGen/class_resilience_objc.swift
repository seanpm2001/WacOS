// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -enable-source-import -emit-ir -o - -primary-file %s | %FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-%target-ptrsize

// REQUIRES: objc_interop

// CHECK: %swift.type = type { [[INT:i32|i64]] }

import Foundation

public class FixedLayoutObjCSubclass : NSObject {
  // This field uses constant direct access because NSObject has fixed layout.
  public final var field: Int32 = 0
};

// CHECK-LABEL: define hidden void @_TF21class_resilience_objc29testConstantDirectFieldAccessFCS_23FixedLayoutObjCSubclassT_(%C21class_resilience_objc23FixedLayoutObjCSubclass*)
// CHECK:      [[FIELD_ADDR:%.*]] = getelementptr inbounds %C21class_resilience_objc23FixedLayoutObjCSubclass, %C21class_resilience_objc23FixedLayoutObjCSubclass* %0, i32 0, i32 1
// CHECK-NEXT: [[PAYLOAD_ADDR:%.*]] = getelementptr inbounds %Vs5Int32, %Vs5Int32* %1, i32 0, i32 0
// CHECK-NEXT: store i32 10, i32* [[PAYLOAD_ADDR]]

func testConstantDirectFieldAccess(_ o: FixedLayoutObjCSubclass) {
  o.field = 10
}

public class NonFixedLayoutObjCSubclass : NSCoder {
  // This field uses non-constant direct access because NSCoder has resilient
  // layout.
  public final var field: Int32 = 0
}

// CHECK-LABEL: define hidden void @_TF21class_resilience_objc32testNonConstantDirectFieldAccessFCS_26NonFixedLayoutObjCSubclassT_(%C21class_resilience_objc26NonFixedLayoutObjCSubclass*)
// CHECK:      [[OFFSET:%.*]] = load [[INT]], [[INT]]* @_TWvdvC21class_resilience_objc26NonFixedLayoutObjCSubclass5fieldVs5Int32
// CHECK-NEXT: [[OBJECT:%.*]] = bitcast %C21class_resilience_objc26NonFixedLayoutObjCSubclass* %0 to i8*
// CHECK-NEXT: [[ADDR:%.*]] = getelementptr inbounds i8, i8* [[OBJECT]], [[INT]] [[OFFSET]]
// CHECK-NEXT: [[FIELD_ADDR:%.*]] = bitcast i8* [[ADDR]] to %Vs5Int32*
// CHECK-NEXT: [[PAYLOAD_ADDR:%.*]] = getelementptr inbounds %Vs5Int32, %Vs5Int32* [[FIELD_ADDR]], i32 0, i32 0
// CHECK-NEXT: store i32 10, i32* [[PAYLOAD_ADDR]]

func testNonConstantDirectFieldAccess(_ o: NonFixedLayoutObjCSubclass) {
  o.field = 10
}

public class GenericObjCSubclass<T> : NSCoder {
  public final var content: T
  public final var field: Int32 = 0

  public init(content: T) {
    self.content = content
  }
}

// CHECK-LABEL: define hidden void @_TF21class_resilience_objc31testConstantIndirectFieldAccessurFGCS_19GenericObjCSubclassx_T_(%C21class_resilience_objc19GenericObjCSubclass*)

// FIXME: we could eliminate the unnecessary isa load by lazily emitting
// metadata sources in EmitPolymorphicParameters

// CHECK:         bitcast %C21class_resilience_objc19GenericObjCSubclass* %0

// CHECK-32:      [[ADDR:%.*]] = bitcast %C21class_resilience_objc19GenericObjCSubclass* %0 to %swift.type**
// CHECK-32-NEXT: [[ISA:%.*]] = load %swift.type*, %swift.type** [[ADDR]]

// CHECK-64:      [[ADDR:%.*]] = bitcast %C21class_resilience_objc19GenericObjCSubclass* %0 to [[INT]]*
// CHECK-64-NEXT: [[ISA:%.*]] = load [[INT]], [[INT]]* [[ADDR]]
// CHECK-64-NEXT: [[ISA_MASK:%.*]] = load [[INT]], [[INT]]* @swift_isaMask
// CHECK-64-NEXT: [[ISA_VALUE:%.*]] = and [[INT]] [[ISA]], [[ISA_MASK]]
// CHECK-64-NEXT: [[ISA:%.*]] = inttoptr [[INT]] [[ISA_VALUE]] to %swift.type*

// CHECK-NEXT:    [[ISA_ADDR:%.*]] = bitcast %swift.type* [[ISA]] to [[INT]]*

// CHECK-32-NEXT: [[FIELD_OFFSET_ADDR:%.*]] = getelementptr inbounds [[INT]], [[INT]]* [[ISA_ADDR]], [[INT]] 16

// CHECK-64-NEXT: [[FIELD_OFFSET_ADDR:%.*]] = getelementptr inbounds [[INT]], [[INT]]* [[ISA_ADDR]], [[INT]] 13

// CHECK-NEXT: [[FIELD_OFFSET:%.*]] = load [[INT]], [[INT]]* [[FIELD_OFFSET_ADDR:%.*]]
// CHECK-NEXT: [[OBJECT:%.*]] = bitcast %C21class_resilience_objc19GenericObjCSubclass* %0 to i8*
// CHECK-NEXT: [[ADDR:%.*]] = getelementptr inbounds i8, i8* [[OBJECT]], [[INT]] [[FIELD_OFFSET]]
// CHECK-NEXT: [[FIELD_ADDR:%.*]] = bitcast i8* [[ADDR]] to %Vs5Int32*
// CHECK-NEXT: [[PAYLOAD_ADDR:%.*]] = getelementptr inbounds %Vs5Int32, %Vs5Int32* [[FIELD_ADDR]], i32 0, i32 0
// CHECK-NEXT: store i32 10, i32* [[PAYLOAD_ADDR]]

func testConstantIndirectFieldAccess<T>(_ o: GenericObjCSubclass<T>) {
  // This field uses constant indirect access because NSCoder has resilient
  // layout. Non-constant indirect is never needed for Objective-C classes
  // because the field offset vector only contains Swift field offsets.
  o.field = 10
}
