// RUN: %target-swift-emit-silgen -Xllvm -sil-full-demangle -Xllvm -sil-print-debuginfo -emit-verbose-sil %s | %FileCheck %s

// Test top-level/module locations.
class TopLevelObjectTy {
  init() { }

  deinit {
  }
}
var topLevelObject:TopLevelObjectTy

class TopLevelObjectTyWithoutDestructor {
  init() {
  }
}
var topLevelObject2:TopLevelObjectTyWithoutDestructor

// CHECK-LABEL: sil [ossa] @main
// CHECK: integer_literal ${{.*}}, 0, {{.*}} auto_gen
// CHECK: return    {{.*}} auto_gen

// Check allocating initializer
// CHECK-LABEL: sil_locations_top_level.TopLevelObjectTy.__allocating_init
// CHECK: sil hidden [exact_self_class] [ossa] @$s23sil_locations_top_level16TopLevelObjectTyC{{[_0-9a-zA-Z]*}}fC
// CHECK: alloc_ref {{.*}}line:5:3:auto_gen
// CHECK: function_ref

// Check default constructor
// CHECK-LABEL: sil_locations_top_level.TopLevelObjectTy.init
// CHECK: bb
// CHECK: return {{.*}}// {{.*}} line:5:12

// Check explicit destructor
// CHECK_LABEL: sil hidden [ossa] @$s23sil_locations_top_level16TopLevelObjectTyCfd
// CHECK:   return {{.*}}// {{.*}} line:8:3

// Check allocating constructor
// CHECK-LABEL: sil_locations_top_level.TopLevelObjectTyWithoutDestructor.__allocating_init
// CHECK: return {{.*}}// {{.*}} line:14:3:imp_return:auto_gen

// Check explicit constructor
// FIXME: The ConstructorDecl location is wrong here (looks like it's wrong in the AST).
// CHECK-LABEL: sil hidden [ossa] @$s23sil_locations_top_level33TopLevelObjectTyWithoutDestructorC{{[_0-9a-zA-Z]*}}fc
// CHECK: return {{.*}}// {{.*}} line:14:3:imp_return

// Check implicit destructor
// CHECK_LABEL: sil hidden [ossa] @$s23sil_locations_top_level33TopLevelObjectTyWithoutDestructorCfd
// CHECK:   return {{.*}}// {{.*}} line:12:7:imp_return:auto_gen
