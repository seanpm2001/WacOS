// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -I %S/../Inputs/custom-modules -import-objc-header %S/Inputs/mixed-target/header.h -typecheck -primary-file %S/mixed-target-using-header.swift %S/Inputs/mixed-target/other-file.swift -disable-objc-attr-requires-foundation-module -verify -swift-version 4
// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) -I %S/../Inputs/custom-modules -import-objc-header %S/Inputs/mixed-target/header.h -emit-sil -primary-file %S/mixed-target-using-header.swift %S/Inputs/mixed-target/other-file.swift -disable-objc-attr-requires-foundation-module -o /dev/null -D SILGEN -swift-version 4

// REQUIRES: objc_interop
