// Passing demangled name

// RUN: %target-swift-frontend %s -g -module-name basic -emit-sib -o - | %target-sil-func-extractor -module-name basic -func="basic.foo" | %FileCheck %s -check-prefix=EXTRACT-FOO
// RUN: %target-swift-frontend %s -g -module-name basic -emit-sib -o - | %target-sil-func-extractor -module-name basic -func="basic.X.test" | %FileCheck %s -check-prefix=EXTRACT-TEST
// RUN: %target-swift-frontend %s -g -module-name basic -emit-sib -o - | %target-sil-func-extractor -module-name basic -func="basic.Vehicle.init" | %FileCheck %s -check-prefix=EXTRACT-INIT
// RUN: %target-swift-frontend %s -g -module-name basic -emit-sib -o - | %target-sil-func-extractor -module-name basic -func="basic.Vehicle.now" | %FileCheck %s -check-prefix=EXTRACT-NOW

// Passing mangled name

// RUN: %target-swift-frontend %s -g -module-name basic -emit-sib -o - | %target-sil-func-extractor -module-name basic -func="_T05basic3fooSiyF" | %FileCheck %s -check-prefix=EXTRACT-FOO
// RUN: %target-swift-frontend %s -g -module-name basic -emit-sib -o - | %target-sil-func-extractor -module-name basic -func="_T05basic1XV4testyyF" | %FileCheck %s -check-prefix=EXTRACT-TEST
// RUN: %target-swift-frontend %s -g -module-name basic -emit-sib -o - | %target-sil-func-extractor -module-name basic -func="_T05basic7VehicleCACSi1n_tcfc" | %FileCheck %s -check-prefix=EXTRACT-INIT
// RUN: %target-swift-frontend %s -g -module-name basic -emit-sib -o - | %target-sil-func-extractor -module-name basic -func="_T05basic7VehicleC3nowSiyF" | %FileCheck %s -check-prefix=EXTRACT-NOW


// EXTRACT-FOO-NOT: sil hidden @_T05basic1XV4testyyF : $@convention(method) (X) -> () {
// EXTRACT-FOO-NOT: sil hidden @_T05basic7VehicleCACSi1n_tcfc : $@convention(method) (Int, @guaranteed Vehicle) -> @owned Vehicle {
// EXTRACT-FOO-NOT: sil hidden @_T05basic7VehicleC3nowSiyF : $@convention(method) (@guaranteed Vehicle) -> Int {

// EXTRACT-FOO-LABEL: sil [serialized] @_T05basic3fooSiyF : $@convention(thin) () -> Int {
// EXTRACT-FOO:       bb0:
// EXTRACT-FOO-NEXT:    %0 = integer_literal
// EXTRACT-FOO:         %[[POS:.*]] = struct $Int
// EXTRACT-FOO-NEXT:    return %[[POS]] : $Int


// EXTRACT-TEST-NOT: sil hidden @_T05basic3fooSiyF : $@convention(thin) () -> Int {
// EXTRACT-TEST-NOT: sil hidden @_T05basic7VehicleCACSi1n_tcfc : $@convention(method) (Int, @guaranteed Vehicle) -> @owned Vehicle {
// EXTRACT-TEST-NOT: sil hidden @_T05basic7VehicleC3nowSiyF : $@convention(method) (@guaranteed Vehicle) -> Int {

// EXTRACT-TEST-LABEL:  sil [serialized] @_T05basic1XV4testyyF : $@convention(method) (X) -> () {
// EXTRACT-TEST:        bb0(%0 : $X):
// EXTRACT-TEST-NEXT:     function_ref
// EXTRACT-TEST-NEXT:     function_ref @_T05basic3fooSiyF : $@convention(thin) () -> Int
// EXTRACT-TEST-NEXT:     apply
// EXTRACT-TEST-NEXT:     tuple
// EXTRACT-TEST-NEXT:     return


// EXTRACT-INIT-NOT: sil [serialized] @_T05basic3fooSiyF : $@convention(thin) () -> Int {
// EXTRACT-INIT-NOT: sil [serialized] @_T05basic1XV4testyyF : $@convention(method) (X) -> () {
// EXTRACT-INIT-NOT: sil [serialized] @_T05basic7VehicleC3nowSiyF : $@convention(method) (@owned Vehicle) -> Int {

// EXTRACT-INIT-LABEL:   sil [serialized] @_T05basic7VehicleCACSi1n_tcfc : $@convention(method) (Int, @owned Vehicle) -> @owned Vehicle {
// EXTRACT-INIT:         bb0
// EXTRACT-INIT-NEXT:      ref_element_addr
// EXTRACT-INIT-NEXT:      begin_access [modify] [dynamic]
// EXTRACT-INIT-NEXT:      store
// EXTRACT-INIT-NEXT:      end_access
// EXTRACT-INIT-NEXT:      return


// EXTRACT-NOW-NOT: sil [serialized] @_T05basic3fooSiyF : $@convention(thin) () -> Int {
// EXTRACT-NOW-NOT: sil [serialized] @_T05basic1XV4testyyF : $@convention(method) (X) -> () {
// EXTRACT-NOW-NOT: sil [serialized] @_T05basic7VehicleCACSi1n_tcfc : $@convention(method) (Int, @guaranteed Vehicle) -> @owned Vehicle {

// EXTRACT-NOW-LABEL:   sil [serialized] @_T05basic7VehicleC3nowSiyF : $@convention(method) (@guaranteed Vehicle) -> Int {
// EXTRACT-NOW:         bb0
// EXTRACT-NOW:           ref_element_addr
// EXTRACT-NOW-NEXT:      begin_access [read] [dynamic]
// EXTRACT-NOW-NEXT:      load
// EXTRACT-NOW-NEXT:      end_access
// EXTRACT-NOW-NEXT:      return

public struct X {
  @_versioned
  @_inlineable
  func test() {
    foo()
  }
}

public class Vehicle {
    @_versioned
    var numOfWheels: Int

    @_versioned
    @_inlineable
    init(n: Int) {
      numOfWheels = n
    }

    @_versioned
    @_inlineable
    func now() -> Int {
        return numOfWheels
    }
}

@discardableResult
@_versioned
@_inlineable
func foo() -> Int {
  return 7
}
