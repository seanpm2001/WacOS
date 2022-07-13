// RUN: %target-typecheck-verify-swift

// REQUIRES: objc_interop

import Dispatch
import Foundation

func test1(_ queue: dispatch_queue_t) {} // expected-error {{'dispatch_queue_t' is unavailable}}
func test2(_ queue: DispatchQueue) {
  let base: NSObjectProtocol = queue
  let _: DispatchObject = queue

  let _ = base as? DispatchQueue

  // Make sure the dispatch types are actually distinct types!
  let _ = queue as DispatchSource // expected-error {{cannot convert value of type 'DispatchQueue' to type 'DispatchSource' in coercion}}
  let _ = base as DispatchSource // expected-error {{'NSObjectProtocol' is not convertible to 'DispatchSource'; did you mean to use 'as!' to force downcast?}}
}

extension dispatch_queue_t {} // expected-error {{'dispatch_queue_t' is unavailable}}

// Make sure you can extend a dispatch type via its common name.
extension DispatchQueue {
  func myAsync(_ block: @escaping () -> Void) {
    async(execute: block)
  }
}

func test_dispatch1(_ object: dispatch_object_t) {} // expected-error {{'dispatch_object_t' is unavailable}}
func test_dispatch2(_ object: DispatchObject) {
  dispatch_retain(object) // expected-error {{'dispatch_retain' is unavailable in Swift}}
  dispatch_release(object) // expected-error {{'dispatch_release' is unavailable in Swift}}
}

