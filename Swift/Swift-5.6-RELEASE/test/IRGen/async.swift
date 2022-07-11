// RUN: %target-swift-frontend -primary-file %s -emit-ir  -disable-availability-checking | %FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-%target-ptrsize

// REQUIRES: concurrency

// CHECK: "$s5async1fyyYaF"
public func f() async { }

// CHECK: "$s5async1gyyYaKF"
public func g() async throws { }

// CHECK: "$s5async1hyyS2iYbXEF"
public func h(_: @Sendable (Int) -> Int) { }

public class SomeClass {}

//@_silgen_name("swift_task_future_wait")
//public func task_future_wait(_ task: __owned SomeClass) async throws -> Int

@_silgen_name("swift_task_future_wait_throwing")
public func _taskFutureGetThrowing<T>(_ task: SomeClass) async throws -> T

// CHECK: define{{.*}} swift{{(tail)?}}cc void @"$s5async8testThisyyAA9SomeClassCnYaF"(%swift.context* swiftasync %0{{.*}}
// CHECK-NOT: @swift_task_alloc
// CHECK: {{(must)?}}tail call swift{{(tail)?}}cc void @swift_task_future_wait_throwing(%swift.opaque* {{.*}}, %swift.context* {{.*}}, %T5async9SomeClassC* {{.*}}, i8* {{.*}}, %swift.context* {{.*}})
public func testThis(_ task: __owned SomeClass) async {
  do {
    let _ : Int = try await _taskFutureGetThrowing(task)
  } catch _ {
    print("error")
  }
}
