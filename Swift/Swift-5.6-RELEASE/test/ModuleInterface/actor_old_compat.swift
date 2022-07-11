// RUN: %empty-directory(%t)
// RUN: mkdir -p %t/OldActor.framework/Modules/OldActor.swiftmodule
// RUN: %target-swift-frontend -emit-module -module-name OldActor %S/Inputs/OldActor.swiftinterface -o %t/OldActor.framework/Modules/OldActor.swiftmodule/%module-target-triple.swiftmodule
// RUN: %target-swift-frontend -F %t  -disable-availability-checking -typecheck -verify %s

// RUNX: cp -r %S/Inputs/OldActor.framework %t/
// RUNX: %{python} %S/../CrossImport/Inputs/rewrite-module-triples.py %t %module-target-triple

// REQUIRES: concurrency

import OldActor

@available(SwiftStdlib 5.1, *)
extension Monk {
  public func test() async {
    method()
  }
}