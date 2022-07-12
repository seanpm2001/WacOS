// RUN: %empty-directory(%t)
// RUN: %target-build-swift %S/Inputs/Missing.swift -parse-as-library -emit-module -emit-library -module-name TypeLowering -o %t/libTypesToReflect
// RUN: %target-swift-reflection-dump -binary-filename %t/libTypesToReflect -binary-filename %platform-module-dir/libswiftCore.dylib -dump-type-lowering < %s | %FileCheck %s

// Disable asan builds until we build swift-reflection-dump and the reflection library with the same compile: rdar://problem/30406870
// REQUIRES: no_asan

// REQUIRES: objc_interop
// REQUIRES: CPU=x86_64

12TypeLowering11BasicStructV
// CHECK: (struct TypeLowering.BasicStruct)
// CHECK: Invalid lowering

12TypeLowering11BasicStructVSg
// CHECK: (bound_generic_enum Swift.Optional
// CHECK:   (struct TypeLowering.BasicStruct))
// CHECK: Invalid lowering

12TypeLowering3BarVyAA11BasicStructVG
// CHECK: (bound_generic_struct TypeLowering.Bar
// CHECK:   (struct TypeLowering.BasicStruct))
// CHECK: Invalid lowering
