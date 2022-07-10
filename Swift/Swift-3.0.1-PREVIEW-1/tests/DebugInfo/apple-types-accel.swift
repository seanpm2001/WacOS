// RUN: %target-swift-frontend %s -emit-ir -g -o - | %FileCheck %s
// RUN: %target-swift-frontend %s -c -g -o %t.o
// RUN: dwarfdump --verify --apple-types %t.o | %FileCheck --check-prefix=CHECK-ACCEL %s
// RUN: dwarfdump --debug-info %t.o | %FileCheck --check-prefix=CHECK-DWARF %s

// REQUIRES: OS=macosx

// Verify that the unmangles basenames end up in the accelerator table.
// CHECK-ACCEL-DAG:	 str[0]{{.*}}"Int64"
// CHECK-ACCEL-DAG:	 str[0]{{.*}}"foo"

// Verify that the mangled names end up in the debug info.
// CHECK-DWARF: TAG_module
// CHECK-DWARF: AT_name( "main" )
// CHECK-DWARF: TAG_structure_type
// CHECK-DWARF-NEXT: AT_name( "foo" )
// CHECK-DWARF-NEXT: AT_linkage_name( "_TtC4main3foo" )

// Verify the IR interface:
// CHECK: !DICompositeType(tag: DW_TAG_structure_type, name: "foo"
// CHECK-SAME:             line: [[@LINE+2]]
// CHECK-SAME:             identifier: "_TtC4main3foo"
class foo {
	var x : Int64 = 1
}

func main() -> Int64 {
	var thefoo = foo();
	return thefoo.x
}

main()
