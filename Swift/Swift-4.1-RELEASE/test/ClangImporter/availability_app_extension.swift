// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -typecheck -verify -application-extension %s

// REQUIRES: objc_interop

import Foundation

func test_unavailable_app_extension() {
  _ = SomeCrazyAppExtensionForbiddenAPI() // expected-error {{'SomeCrazyAppExtensionForbiddenAPI()' is unavailable: Not available in App Extensions}}
}
