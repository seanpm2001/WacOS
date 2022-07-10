// RUN: %target-swift-frontend -primary-file %s %S/Inputs/lazy_multi_file_helper.swift -emit-ir | %FileCheck %s

// REQUIRES: CPU=i386_or_x86_64

// CHECK: %C15lazy_multi_file8Subclass = type <{ %swift.refcounted, %[[OPTIONAL_INT_TY:GSqSi_]], [{{[0-9]+}} x i8], %SS }>
// CHECK: %[[OPTIONAL_INT_TY]] = type <{ [{{[0-9]+}} x i8], [1 x i8] }>
// CHECK: %V15lazy_multi_file13LazyContainer = type <{ %[[OPTIONAL_INT_TY]] }>

class Subclass : LazyContainerClass {
  final var str = "abc"

  // CHECK-LABEL: @_TFC15lazy_multi_file8Subclass6getStrfT_SS(%C15lazy_multi_file8Subclass*) {{.*}} {
  func getStr() -> String {
    // CHECK: = getelementptr inbounds %C15lazy_multi_file8Subclass, %C15lazy_multi_file8Subclass* %0, i32 0, i32 3
    return str
  }
}

// CHECK-LABEL: @_TF15lazy_multi_file4testFT_Si() {{.*}} {
func test() -> Int {
  var container = LazyContainer()
  useLazyContainer(container)
  return container.lazyVar
}
