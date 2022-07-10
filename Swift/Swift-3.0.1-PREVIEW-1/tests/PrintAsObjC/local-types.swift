// Please keep this file in alphabetical order!

// RUN: rm -rf %t
// RUN: mkdir %t
// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -emit-module -o %t %s -module-name local -disable-objc-attr-requires-foundation-module
// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -parse-as-library %t/local.swiftmodule -parse -emit-objc-header-path %t/local.h -import-objc-header %S/../Inputs/empty.h -disable-objc-attr-requires-foundation-module
// RUN: %FileCheck %s < %t/local.h
// RUN: %check-in-clang %t/local.h

// REQUIRES: objc_interop

import ObjectiveC

// CHECK-LABEL: @interface AFullyDefinedClass
// CHECK-NEXT: init
// CHECK-NEXT: @end
@objc class AFullyDefinedClass {}

class ANonObjCClass {}

// CHECK-NOT: @class AFullyDefinedClass
// CHECK: @class ZForwardClass1;
// CHECK-NEXT: @class ZForwardClass2;
// CHECK-NEXT: @class ZForwardAliasClass;
// CHECK-NEXT: @protocol ZForwardProtocol1;
// CHECK-NEXT: @protocol ZForwardProtocol2;
// CHECK-NEXT: @protocol ZForwardProtocol3;
// CHECK-NEXT: @protocol ZForwardProtocol4;
// CHECK-NEXT: @protocol ZForwardProtocol5;
// CHECK-NEXT: @protocol ZForwardProtocol6;
// CHECK-NEXT: @protocol ZForwardProtocol7;
// CHECK-NEXT: @protocol ZForwardProtocol8;
// CHECK-NEXT: @class ZForwardClass3;

// CHECK-LABEL: @interface UseForward
// CHECK-NEXT: - (void)definedAlready:(AFullyDefinedClass * _Nonnull)a;
// CHECK-NEXT: - (void)a:(ZForwardClass1 * _Nonnull)a;
// CHECK-NEXT: - (ZForwardClass2 * _Nonnull)b;
// CHECK-NEXT: - (void)c:(ZForwardAliasClass * _Nonnull)c;
// CHECK-NEXT: - (void)d:(id <ZForwardProtocol1> _Nonnull)d;
// CHECK-NEXT: - (void)e:(Class <ZForwardProtocol2> _Nonnull)e;
// CHECK-NEXT: - (void)e2:(id <ZForwardProtocol2> _Nonnull)e;
// CHECK-NEXT: - (void)f:(SWIFT_NOESCAPE id <ZForwardProtocol5> _Nonnull (^ _Nonnull)(id <ZForwardProtocol3> _Nonnull, id <ZForwardProtocol4> _Nonnull))f;
// CHECK-NEXT: - (void)g:(id <ZForwardProtocol6, ZForwardProtocol7> _Nonnull)g;
// CHECK-NEXT: - (void)i:(id <ZForwardProtocol8> _Nonnull)_;
// CHECK-NEXT: @property (nonatomic, readonly, strong) ZForwardClass3 * _Nonnull j;
// CHECK-NEXT: @property (nonatomic, readonly) SWIFT_METATYPE(ZForwardClass4) _Nonnull k;
// CHECK-NEXT: init
// CHECK-NEXT: @end

@objc class UseForward {
  func definedAlready(_ a: AFullyDefinedClass) {}

  func a(_ a: ZForwardClass1) {}
  func b() -> ZForwardClass2 { return ZForwardClass2() }
  func c(_ c: ZForwardAlias) {}

  func d(_ d: (ZForwardProtocol1)) {}
  func e(_ e: ZForwardProtocol2.Type) {}
  func e2(_ e: ZForwardProtocol2) {}
  func f(_ f: (ZForwardProtocol3, ZForwardProtocol4) -> ZForwardProtocol5) {}
  func g(_ g: ZForwardProtocol6 & ZForwardProtocol7) {}

  func h(_ h: ANonObjCClass) -> ANonObjCClass.Type { return type(of: h) }
  func i(_: ZForwardProtocol8) {}

  var j: ZForwardClass3 { return ZForwardClass3() }
  var k: ZForwardClass4.Type { return ZForwardClass4.self }
}

// CHECK-NOT: @class ZForwardClass1;
// CHECK-NOT: @protocol ZForwardProtocol1;

// CHECK-LABEL: @interface UseForwardAgain
// CHECK-NEXT: - (void)a:(ZForwardClass1 * _Nonnull)a;
// CHECK-NEXT: - (void)b:(id <ZForwardProtocol1> _Nonnull)b;
// CHECK-NEXT: init
// CHECK-NEXT: @end
@objc class UseForwardAgain {
  func a(_ a: ZForwardClass1) {}
  func b(_ b: ZForwardProtocol1) {}
}

typealias ZForwardAlias = ZForwardAliasClass
@objc class ZForwardAliasClass {}

// CHECK-NOT: @class UseForward;

// CHECK-LABEL: @interface ZForwardClass1
// CHECK-NEXT: - (void)circular:(UseForward * _Nonnull)a;
// CHECK-NEXT: init
// CHECK-NEXT: @end
@objc class ZForwardClass1 {
  func circular(_ a: UseForward) {}
}
@objc class ZForwardClass2 {}
@objc class ZForwardClass3 {}
@objc class ZForwardClass4 {}

@objc protocol ZForwardProtocol1 {}
@objc protocol ZForwardProtocol2 {}
@objc protocol ZForwardProtocol3 {}
@objc protocol ZForwardProtocol4 {}
@objc protocol ZForwardProtocol5 {}
@objc protocol ZForwardProtocol6 {}
@objc protocol ZForwardProtocol7 {}
@objc protocol ZForwardProtocol8 {}
