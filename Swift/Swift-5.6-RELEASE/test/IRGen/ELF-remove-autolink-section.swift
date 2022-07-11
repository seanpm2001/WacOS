// RUN: %swiftc_driver -emit-ir %s -o - -Xfrontend -disable-implicit-concurrency-module-import | %FileCheck %s -check-prefix ELF

// Check that the swift auto link section is available in the object file.
// RUN: %swiftc_driver -c %s -o %t -Xfrontend -disable-implicit-concurrency-module-import
// RUN: llvm-readelf %t -S | %FileCheck %s -check-prefix SECTION

// Checks that the swift auto link section is removed from the final binary.
// RUN: %swiftc_driver  -emit-executable %s -o %t -Xfrontend -disable-implicit-concurrency-module-import
// RUN: llvm-readelf %t -S | %FileCheck %s -check-prefix NOSECTION

// REQUIRES: OS=linux-gnu

print("Hi from Swift!")

// Find the metadata entry for the denylisting of the metadata symbol
// Ensure that it is in the ASAN metadata

// ELF: !llvm.asan.globals = !{
// ELF-SAME: [[MD:![0-9]+]]
// ELF-SAME: }

// ELF-DAG: [[MD]] = !{[37 x i8]* @_swift1_autolink_entries, null, null, i1 false, i1 true}

// SECTION: .swift1_autolink_entries
// NOSECTION-NOT: .swift1_autolink_entries
