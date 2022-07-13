// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -assume-parsing-unqualified-ownership-sil -import-objc-header %S/Inputs/c_functions.h -primary-file %s -emit-ir | %FileCheck %s
// RUN: %target-swift-frontend -assume-parsing-unqualified-ownership-sil -import-objc-header %S/Inputs/c_functions.h -primary-file %s -emit-ir |  %FileCheck %s --check-prefix=%target-cpu

// This is deliberately not a SIL test so that we can test SILGen too.

// CHECK-LABEL: define hidden swiftcc void @_T011c_functions14testOverloadedyyF
func testOverloaded() {
  // CHECK: call void @_Z10overloadedv()
  overloaded()
  // CHECK: call void @_Z10overloadedi(i32{{( signext)?}} 42)
  overloaded(42)
  // CHECK: call void @{{.*}}test_my_log
  test_my_log()
} // CHECK: {{^}$}}

func test_indirect_by_val_alignment() {
  let x = a_thing()
  log_a_thing(x)
}

// x86_64-LABEL: define hidden swiftcc void  @_T011c_functions30test_indirect_by_val_alignmentyyF()
// x86_64: %indirect-temporary = alloca %TSC7a_thingV, align [[ALIGN:[0-9]+]]
// x86_64: [[CAST:%.*]] = bitcast %TSC7a_thingV* %indirect-temporary to %struct.a_thing*
// x86_64: call void @log_a_thing(%struct.a_thing* byval align [[ALIGN]] [[CAST]])
// x86_64: define internal void @log_a_thing(%struct.a_thing* byval align [[ALIGN]]


// We only want to test x86_64.
// aarch64: define hidden swiftcc void  @_T011c_functions30test_indirect_by_val_alignmentyyF()
// arm64: define hidden swiftcc void  @_T011c_functions30test_indirect_by_val_alignmentyyF()
// armv7k: define hidden swiftcc void  @_T011c_functions30test_indirect_by_val_alignmentyyF()
// armv7s: define hidden swiftcc void  @_T011c_functions30test_indirect_by_val_alignmentyyF()
// armv7: define hidden swiftcc void  @_T011c_functions30test_indirect_by_val_alignmentyyF()
// i386: define hidden swiftcc void  @_T011c_functions30test_indirect_by_val_alignmentyyF()
// s390x: define hidden swiftcc void  @_T011c_functions30test_indirect_by_val_alignmentyyF()
