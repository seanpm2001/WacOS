// RUN: %target-run-simple-swift | %FileCheck %s
// REQUIRES: executable_test

// REQUIRES: objc_interop

import Foundation

protocol Hashish {
  var hash: Int { get }
}

extension NSObject: Hashish {}

func getHash<T: Hashish>(_ x: T) -> Int { return x.hash }

let u = NSURL(string: "http://www.example.com")!

// CHECK: true
print(u.hash == getHash(u))
