
// RUN: %target-swift-frontend %s -parse-as-library -enable-spec-devirt -O -emit-sil | %FileCheck %s
// RUN: %target-swift-frontend %s -parse-as-library -Osize -emit-sil
//
// Test speculative devirtualization.

public class Cat {
  var cats: Int

  required init(cats: Int) {
    self.cats = cats
  }
}

public class BigCat : Cat {
  required init(cats: Int) {
    super.init(cats: cats)
  }
}

public func make(type: Cat.Type, cats: Int) {
  type.init(cats: cats)
}

// CHECK-LABEL: sil @$s23devirt_speculative_init4make4type4catsyAA3CatCm_SitF : $@convention(thin) (@thick Cat.Type, Int) -> ()
// CHECK:   checked_cast_br [exact] %0 : $@thick Cat.Type to @thick Cat.Type, bb2, bb3
// CHECK: bb1:
// CHECK:   return
// CHECK: bb2({{%.*}} : $@thick Cat.Type):
// CHECK:   alloc_ref [stack] $Cat
// CHECK:   br bb1
// CHECK: bb3:
// CHECK:   alloc_ref [stack] $BigCat
// CHECK:   br bb1
