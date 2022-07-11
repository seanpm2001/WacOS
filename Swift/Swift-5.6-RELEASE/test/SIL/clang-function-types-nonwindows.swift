// RUN: %target-swift-frontend(mock-sdk: %clang-importer-sdk) %s -emit-sil -swift-version 5 -use-clang-function-types -experimental-print-full-convention -o - | %FileCheck %s

// UNSUPPORTED: OS=windows-msvc
// UNSUPPORTED: OS=linux-android, OS=linux-androideabi

import ctypes

public func f(g: @convention(c, cType: "void (*)(size_t)") (Int) -> ()) { g(0) }

// CHECK: sil @$s4main1f1gyySiXzC9_ZTSPFvmE_tF : $@convention(thin) (@convention(c, cType: "void (*)(unsigned long)") @noescape (Int) -> ()) -> () {
// CHECK: bb0(%0 : $@convention(c, cType: "void (*)(unsigned long)") @noescape (Int) -> ()):
// CHECK:   debug_value %0 : $@convention(c, cType: "void (*)(unsigned long)") @noescape (Int) -> (), let, name "g", argno 1 // id: %1
