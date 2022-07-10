// RUN: rm -rf %t
// RUN: mkdir %t
// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -emit-module -o %t %s -import-objc-header %S/Inputs/imported-block-typedefs.h -disable-objc-attr-requires-foundation-module
// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -parse-as-library %t/imported-block-typedefs.swiftmodule -parse -emit-objc-header-path %t/imported-block-typedefs-output.h -import-objc-header %S/../Inputs/empty.h -disable-objc-attr-requires-foundation-module
// RUN: %FileCheck %s < %t/imported-block-typedefs-output.h
// RUN: %check-in-clang %t/imported-block-typedefs-output.h -include %S/Inputs/imported-block-typedefs.h

// REQUIRES: objc_interop

import ObjectiveC

// CHECK-LABEL: @interface Typedefs
@objc class Typedefs {
  
  // FIXME: The imported typedefs should be printed directly as the param types,
  // but one level of sugar is currently lost when applying @noescape. The importer
  // also loses __attribute__((noescape)) for params of imported function types.
  // <https://bugs.swift.org/browse/SR-2520>
  // <https://bugs.swift.org/browse/SR-2529>
  
  // CHECK-NEXT: - (void)noescapeParam1:(SWIFT_NOESCAPE void (^ _Nonnull)(void))input;
  // CHECK-NEXT: - (void)noescapeParam2:(SWIFT_NOESCAPE void (^ _Nonnull)(PlainBlock _Nullable))input;
  // CHECK-NEXT: - (void)noescapeParam3:(SWIFT_NOESCAPE void (^ _Nonnull)(PlainBlock _Nullable))input;
  // CHECK-NEXT: - (void)noescapeParam4:(SWIFT_NOESCAPE BlockWithEscapingParam _Nullable (^ _Nonnull)(void))input;
  // CHECK-NEXT: - (void)noescapeParam5:(SWIFT_NOESCAPE BlockWithNoescapeParam _Nullable (^ _Nonnull)(void))input;
  // Ideally should be:
  // - (void)noescapeParam1:(SWIFT_NOESCAPE PlainBlock _Nonnull)input;
  // - (void)noescapeParam2:(SWIFT_NOESCAPE BlockWithEscapingParam _Nonnull)input;
  // - (void)noescapeParam3:(SWIFT_NOESCAPE BlockWithNoescapeParam _Nonnull)input;
  // - (void)noescapeParam4:(SWIFT_NOESCAPE BlockReturningBlockWithEscapingParam _Nonnull)input;
  // - (void)noescapeParam5:(SWIFT_NOESCAPE BlockReturningBlockWithNoescapeParam _Nonnull)input;
  func noescapeParam1(_ input: PlainBlock) {}
  func noescapeParam2(_ input: BlockWithEscapingParam) {}
  func noescapeParam3(_ input: BlockWithNoescapeParam) {}
  func noescapeParam4(_ input: BlockReturningBlockWithEscapingParam) {}
  func noescapeParam5(_ input: BlockReturningBlockWithNoescapeParam) {}
  
  // CHECK-NEXT: - (void)escapingParam1:(PlainBlock _Nonnull)input;
  // CHECK-NEXT: - (void)escapingParam2:(BlockWithEscapingParam _Nonnull)input;
  // CHECK-NEXT: - (void)escapingParam3:(BlockWithNoescapeParam _Nonnull)input;
  // CHECK-NEXT: - (void)escapingParam4:(BlockReturningBlockWithEscapingParam _Nonnull)input;
  // CHECK-NEXT: - (void)escapingParam5:(BlockReturningBlockWithNoescapeParam _Nonnull)input;
  func escapingParam1(_ input: @escaping PlainBlock) {}
  func escapingParam2(_ input: @escaping BlockWithEscapingParam) {}
  func escapingParam3(_ input: @escaping BlockWithNoescapeParam) {}
  func escapingParam4(_ input: @escaping BlockReturningBlockWithEscapingParam) {}
  func escapingParam5(_ input: @escaping BlockReturningBlockWithNoescapeParam) {}
  
  // CHECK-NEXT: - (void (^ _Nonnull)(SWIFT_NOESCAPE void (^ _Nonnull)(void)))resultHasNoescapeParam1;
  // CHECK-NEXT: - (void (^ _Nonnull)(SWIFT_NOESCAPE void (^ _Nonnull)(PlainBlock _Nullable)))resultHasNoescapeParam2;
  // CHECK-NEXT: - (void (^ _Nonnull)(SWIFT_NOESCAPE void (^ _Nonnull)(PlainBlock _Nullable)))resultHasNoescapeParam3;
  // CHECK-NEXT: - (void (^ _Nonnull)(SWIFT_NOESCAPE BlockWithEscapingParam _Nullable (^ _Nonnull)(void)))resultHasNoescapeParam4;
  // CHECK-NEXT: - (void (^ _Nonnull)(SWIFT_NOESCAPE BlockWithNoescapeParam _Nullable (^ _Nonnull)(void)))resultHasNoescapeParam5;
  // Ideally should be:
  //  - (void (^ _Nonnull)(SWIFT_NOESCAPE PlainBlock _Nonnull))resultHasNoescapeParam1;
  //  - (void (^ _Nonnull)(SWIFT_NOESCAPE BlockWithEscapingParam _Nonnull))resultHasNoescapeParam2;
  //  - (void (^ _Nonnull)(SWIFT_NOESCAPE BlockWithNoescapeParam _Nonnull))resultHasNoescapeParam3;
  //  - (void (^ _Nonnull)(SWIFT_NOESCAPE BlockReturningBlockWithEscapingParam _Nonnull))resultHasNoescapeParam4;
  //  - (void (^ _Nonnull)(SWIFT_NOESCAPE BlockReturningBlockWithNoescapeParam _Nonnull))resultHasNoescapeParam5;
  func resultHasNoescapeParam1() -> (PlainBlock) -> () { fatalError() }
  func resultHasNoescapeParam2() -> (BlockWithEscapingParam) -> () { fatalError() }
  func resultHasNoescapeParam3() -> (BlockWithNoescapeParam) -> () { fatalError() }
  func resultHasNoescapeParam4() -> (BlockReturningBlockWithEscapingParam) -> () { fatalError() }
  func resultHasNoescapeParam5() -> (BlockReturningBlockWithNoescapeParam) -> () { fatalError() }
  
  // CHECK-NEXT: - (void (^ _Nonnull)(PlainBlock _Nonnull))resultHasEscapingParam1;
  // CHECK-NEXT: - (void (^ _Nonnull)(BlockWithEscapingParam _Nonnull))resultHasEscapingParam2;
  // CHECK-NEXT: - (void (^ _Nonnull)(BlockWithNoescapeParam _Nonnull))resultHasEscapingParam3;
  // CHECK-NEXT: - (void (^ _Nonnull)(BlockReturningBlockWithEscapingParam _Nonnull))resultHasEscapingParam4;
  // CHECK-NEXT: - (void (^ _Nonnull)(BlockReturningBlockWithNoescapeParam _Nonnull))resultHasEscapingParam5;
  func resultHasEscapingParam1() -> (@escaping PlainBlock) -> () { fatalError() }
  func resultHasEscapingParam2() -> (@escaping BlockWithEscapingParam) -> () { fatalError() }
  func resultHasEscapingParam3() -> (@escaping BlockWithNoescapeParam) -> () { fatalError() }
  func resultHasEscapingParam4() -> (@escaping BlockReturningBlockWithEscapingParam) -> () { fatalError() }
  func resultHasEscapingParam5() -> (@escaping BlockReturningBlockWithNoescapeParam) -> () { fatalError() }

}
// CHECK-NEXT: init
// CHECK-NEXT: @end
