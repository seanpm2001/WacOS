// RUN: rm -rf %t && mkdir %t
// RUN: %target-swift-frontend -emit-module -o %t %s -module-name Import
// RUN: %target-swift-frontend -parse -I %t %s -module-name main -DMAIN -verify

// Note: This file is compiled both as a library and as a client of that library. The -verify checks only apply to the client.

import Import

public func test() {
  Import.test()
  Import.hidden() // expected-error {{module 'Import' has no member named 'hidden'}}
}

internal func hidden() {}
