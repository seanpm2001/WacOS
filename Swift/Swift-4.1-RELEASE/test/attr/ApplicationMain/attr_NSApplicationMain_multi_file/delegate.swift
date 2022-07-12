// This file is a part of the multi-file test driven by 'another_delegate.swift'.

// NB: No "-verify"--this file should parse successfully on its own.
// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -typecheck -parse-as-library %s

// REQUIRES: objc_interop

import AppKit

@NSApplicationMain // expected-error{{'NSApplicationMain' attribute can only apply to one class in a module}}
class MyDelegate: NSObject, NSApplicationDelegate {
}

func hi() {}
