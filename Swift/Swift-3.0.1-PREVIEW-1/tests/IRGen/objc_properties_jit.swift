// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) %s -emit-ir -disable-objc-attr-requires-foundation-module -use-jit | %FileCheck %s

// REQUIRES: objc_interop

import Foundation

extension NSString {
  class var classProp: Int {
    get { fatalError() }
    set { fatalError() }
  }
  var instanceProp: Int {
    get { fatalError() }
    set { fatalError() }
  }
}

// CHECK-LABEL: define{{( protected)?}} private void @runtime_registration
// CHECK:         [[GET_CLASS_PROP:%.*]] = call i8* @sel_registerName({{.*}}(classProp)
// CHECK:         call i8* @class_replaceMethod(%objc_class* @"OBJC_METACLASS_$_NSString", i8* [[GET_CLASS_PROP]]
// CHECK:         [[SET_CLASS_PROP:%.*]] = call i8* @sel_registerName({{.*}}(setClassProp:)
// CHECK:         call i8* @class_replaceMethod(%objc_class* @"OBJC_METACLASS_$_NSString", i8* [[SET_CLASS_PROP]]
// CHECK:         [[GET_INSTANCE_PROP:%.*]] = call i8* @sel_registerName({{.*}}(instanceProp)
// CHECK:         call i8* @class_replaceMethod(%objc_class* @"OBJC_CLASS_$_NSString", i8* [[GET_INSTANCE_PROP]]
// CHECK:         [[SET_INSTANCE_PROP:%.*]] = call i8* @sel_registerName({{.*}}(setInstanceProp:)
// CHECK:         call i8* @class_replaceMethod(%objc_class* @"OBJC_CLASS_$_NSString", i8* [[SET_INSTANCE_PROP]]
