// RUN: %target-swift-frontend -emit-ir -g %s -o %t.ll
// RUN: %FileCheck %s --check-prefix IMPORT-CHECK < %t.ll
// RUN: %FileCheck %s --check-prefix LOC-CHECK < %t.ll
// RUN: llc %t.ll -filetype=obj -o %t.o
// RUN: %llvm-dwarfdump %t.o | %FileCheck %s --check-prefix DWARF-CHECK
// DISABLED <rdar://problem/28232630>: %llvm-dwarfdump --verify %t.o

// REQUIRES: OS=macosx

import ObjectiveC
import Foundation

class MyObject : NSObject {
  // Ensure we don't emit linetable entries for ObjC thunks.
  // LOC-CHECK: define {{.*}} @_T04main8MyObjectC0B3ArrSo7NSArrayCvgTo
  // LOC-CHECK: ret {{.*}}, !dbg ![[DBG:.*]]
  // LOC-CHECK: ret
  var MyArr = NSArray()
// IMPORT-CHECK: filename: "test-foundation.swift"
// IMPORT-CHECK-DAG: [[FOUNDATION:[0-9]+]] = !DIModule({{.*}} name: "Foundation",{{.*}} includePath:
  // IMPORT-CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "NSArray", scope: ![[NSARRAY:[0-9]+]]
  //  IMPORT-CHECK-DAG: ![[NSARRAY]] = !DIModule(scope: ![[FOUNDATION:[0-9]+]], name: "NSArray"
  // IMPORT-CHECK-DAG: !DIImportedEntity(tag: DW_TAG_imported_module, {{.*}}entity: ![[FOUNDATION]]

  func foo(_ obj: MyObject) {
    return obj.foo(obj)
  }
}

// SANITY-DAG: !DISubprogram(name: "blah",{{.*}} line: [[@LINE+2]],{{.*}} isDefinition: true
extension MyObject {
  func blah() {
    var _ = MyObject()
  }
}

// SANITY-DAG: ![[NSOBJECT:.*]] = !DICompositeType(tag: DW_TAG_structure_type, name: "NSObject",{{.*}} identifier: "_T0So8NSObjectC"
// SANITY-DAG: !DIGlobalVariable(name: "NsObj",{{.*}} line: [[@LINE+1]],{{.*}} type: ![[NSOBJECT]],{{.*}} isDefinition: true
var NsObj: NSObject
NsObj = MyObject()
var MyObj: MyObject
MyObj = NsObj as! MyObject
MyObj.blah()

public func err() {
  // DWARF-CHECK: DW_AT_name ("NSError")
  // DWARF-CHECK: DW_AT_linkage_name{{.*}}_T0So7NSErrorC
  let _ = NSError(domain: "myDomain", code: 4, 
                  userInfo: [AnyHashable("a"):1,
                             AnyHashable("b"):2,
                             AnyHashable("c"):3])
}

// LOC-CHECK: define {{.*}}4date
public func date() {
  // LOC-CHECK: call {{.*}} @_T0S2SBp21_builtinStringLiteral_Bw17utf8CodeUnitCountBi1_7isASCIItcfC{{.*}}, !dbg ![[L1:.*]]
  let d1 = DateFormatter()
  d1.dateFormat = "dd. mm. yyyy" // LOC-CHECK: call{{.*}}objc_msgSend{{.*}}, !dbg ![[L2:.*]]
  // LOC-CHECK: call {{.*}} @_T0S2SBp21_builtinStringLiteral_Bw17utf8CodeUnitCountBi1_7isASCIItcfC{{.*}}, !dbg ![[L3:.*]]
  let d2 = DateFormatter()
  d2.dateFormat = "mm dd yyyy" // LOC-CHECK: call{{.*}}objc_msgSend{{.*}}, !dbg ![[L4:.*]]
}

// Make sure we build some witness tables for enums.
func useOptions(_ opt: URL.BookmarkCreationOptions)
       -> URL.BookmarkCreationOptions {
  return [opt, opt]
}

// LOC-CHECK: ![[THUNK:.*]] = distinct !DISubprogram({{.*}}linkageName: "_T04main8MyObjectC0B3ArrSo7NSArrayCvgTo"
// LOC-CHECK-NOT:                           line:
// LOC-CHECK-SAME:                          isDefinition: true
// LOC-CHECK: ![[DBG]] = !DILocation(line: 0, scope: ![[THUNK]])

// These debug locations should all be in ordered by increasing line number.
// LOC-CHECK: ![[L1]] =
// LOC-CHECK: ![[L2]] =
// LOC-CHECK: ![[L3]] =
// LOC-CHECK: ![[L4]] =
