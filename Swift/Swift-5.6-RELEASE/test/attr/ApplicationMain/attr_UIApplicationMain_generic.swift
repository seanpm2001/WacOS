// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -typecheck -parse-as-library -verify %s

// REQUIRES: objc_interop

import UIKit

@UIApplicationMain // expected-error{{generic 'UIApplicationMain' classes are not supported}}
class MyDelegate<T>: NSObject, UIApplicationDelegate {
}
