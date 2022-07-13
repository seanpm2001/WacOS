// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -import-objc-header %S/Inputs/objc_dynamic_init.h -emit-silgen -enable-sil-ownership %s | %FileCheck %s
// REQUIRES: objc_interop

import Foundation

protocol Hoozit {
    init()
}

protocol Wotsit {
    init()
}

class Gadget: NSObject, Hoozit {
    required override init() {
        super.init()
    }
}

class Gizmo: Gadget, Wotsit {
    required init() {
        super.init()
    }
}

class Thingamabob: ObjCBaseWithInitProto {
    required init(proto: Int) {
        super.init(proto: proto)
    }
}

final class Bobamathing: Thingamabob {
    required init(proto: Int) {
        super.init(proto: proto)
    }
}

// CHECK-LABEL: sil private [transparent] [thunk] @_T{{.*}}GadgetC{{.*}}CTW
// CHECK:         class_method {{%.*}} : $@thick Gadget.Type, #Gadget.init!allocator.1 :

// CHECK-LABEL: sil_vtable Gadget {
// CHECK:         #Gadget.init!allocator.1: (Gadget.Type) -> () -> Gadget : _T{{.*}}GadgetC{{.*}}C //

// CHECK-LABEL: sil_vtable Gizmo {
// CHECK:         #Gadget.init!allocator.1: (Gadget.Type) -> () -> Gadget : _T{{.*}}GizmoC{{.*}}C [override] //
