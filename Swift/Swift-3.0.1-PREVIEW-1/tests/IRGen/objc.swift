// RUN: rm -rf %t && mkdir %t
// RUN: %build-irgen-test-overlays
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) -primary-file %s -emit-ir -disable-objc-attr-requires-foundation-module | %FileCheck %s

// REQUIRES: CPU=x86_64
// REQUIRES: objc_interop

import Foundation
import gizmo

// CHECK: [[TYPE:%swift.type]] = type
// CHECK: [[BLAMMO:%C4objc6Blammo]] = type
// CHECK: [[MYBLAMMO:%C4objc8MyBlammo]] = type
// CHECK: [[TEST2:%C4objc5Test2]] = type
// CHECK: [[OBJC:%objc_object]] = type
// CHECK: [[ID:%V4objc2id]] = type <{ %Ps9AnyObject_ }>
// CHECK: [[GIZMO:%CSo5Gizmo]] = type
// CHECK: [[RECT:%VSC4Rect]] = type
// CHECK: [[FLOAT:%Sf]] = type

// CHECK: @"\01L_selector_data(bar)" = private global [4 x i8] c"bar\00", section "__TEXT,__objc_methname,cstring_literals", align 1
// CHECK: @"\01L_selector(bar)" = private externally_initialized global i8* getelementptr inbounds ([4 x i8], [4 x i8]* @"\01L_selector_data(bar)", i64 0, i64 0), section "__DATA,__objc_selrefs,literal_pointers,no_dead_strip", align 8

// CHECK: @_TMnVSC4Rect = linkonce_odr hidden constant
// CHECK: @_TMVSC4Rect = linkonce_odr hidden global

// CHECK: @"\01L_selector_data(acquiesce)"
// CHECK-NOT: @"\01L_selector_data(disharmonize)"
// CHECK: @"\01L_selector_data(eviscerate)"

struct id {
  var data : AnyObject
}

// Exporting something as [objc] doesn't make it an ObjC class.
@objc class Blammo {
}
// Class and methods are [objc] by inheritance.
class MyBlammo : Blammo {
  func foo() {}
// CHECK:  define hidden void @_TFC4objc8MyBlammo3foofT_T_([[MYBLAMMO]]*) {{.*}} {
// CHECK:    call {{.*}} @rt_swift_release
// CHECK:    ret void
}

// Class and methods are [objc] by inheritance.
class Test2 : Gizmo {
  func foo() {}
// CHECK:  define hidden void @_TFC4objc5Test23foofT_T_([[TEST2]]*) {{.*}} {
// CHECK:    call {{.*}} @objc_release
// CHECK:    ret void

  dynamic func bar() {}
}

// Test @nonobjc.
class Contrarian : Blammo {
  func acquiesce() {}
  @nonobjc func disharmonize() {}
  @nonobjc func eviscerate() {}
}

class Octogenarian : Contrarian {
  // Override of @nonobjc is @objc again unless made @nonobjc.
  @nonobjc override func disharmonize() {}

  // Override of @nonobjc can be @objc.
  @objc override func eviscerate() {}
}

// CHECK:    define hidden %objc_object* @_TF4objc5test0{{.*}}(%objc_object*)
// CHECK-NOT:  call {{.*}} @swift_unknownRetain
// CHECK:      call {{.*}} @swift_unknownRetain
// CHECK-NOT:  call {{.*}} @swift_unknownRelease
// CHECK:      call {{.*}} @swift_unknownRelease
// CHECK:      ret %objc_object*
func test0(_ arg: id) -> id {
  var x : id
  x = arg
  var y = x
  return y
}

func test1(_ cell: Blammo) {}
// CHECK:  define hidden void @_TF4objc5test1{{.*}}([[BLAMMO]]*) {{.*}} {
// CHECK:    call {{.*}} @rt_swift_release
// CHECK:    ret void


// FIXME: These ownership convention tests should become SILGen tests.
func test2(_ v: Test2) { v.bar() }
func test3() -> NSObject {
  return Gizmo()
}
// Normal message send with argument, no transfers.
func test5(_ g: Gizmo) {
  Gizmo.inspect(g)
}
// The argument to consume: is __attribute__((ns_consumed)).
func test6(_ g: Gizmo) {
  Gizmo.consume(g)
}
// fork is __attribute__((ns_consumes_self)).
func test7(_ g: Gizmo) {
  g.fork()
}
// clone is __attribute__((ns_returns_retained)).
func test8(_ g: Gizmo) {
  g.clone()
}
// duplicate has an object returned at +0.
func test9(_ g: Gizmo) {
  g.duplicate()
}

func test10(_ g: Gizmo, r: Rect) {
  Gizmo.run(with: r, andGizmo:g);
}

// Force the emission of the Rect metadata.
func test11_helper<T>(_ t: T) {}
// NSRect's metadata needs to be uniqued at runtime using getForeignTypeMetadata.
// CHECK-LABEL: define hidden void @_TF4objc6test11FVSC4RectT_
// CHECK:         call %swift.type* @swift_getForeignTypeMetadata({{.*}} @_TMVSC4Rect
func test11(_ r: Rect) { test11_helper(r) }

class WeakObjC {
  weak var obj: NSObject?
  weak var id: AnyObject?

  init() {
    var foo = obj
    var bar: AnyObject? = id
  }
}

// rdar://17528908
// CHECK:  i32 1, !"Objective-C Version", i32 2}
// CHECK:  i32 1, !"Objective-C Image Info Version", i32 0}
// CHECK:  i32 1, !"Objective-C Image Info Section", !"__DATA, __objc_imageinfo, regular, no_dead_strip"}
//   512 == (2 << 8).  2 is the Swift ABI version.
// CHECK:  i32 4, !"Objective-C Garbage Collection", i32 1024}
// CHECK:  i32 1, !"Swift Version", i32 4}
