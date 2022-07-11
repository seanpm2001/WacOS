// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -emit-module -o %t/Test~partial.swiftmodule -module-name Test -primary-file %s
// RUN: %target-swift-frontend -merge-modules -emit-module -o %t/Test.swiftmodule %t/Test~partial.swiftmodule
// RUN: %target-swift-ide-test -print-module -module-to-print=Test -source-filename=x -I %t -prefer-type-repr=false -fully-qualified-types=true | %FileCheck %s

// RUN: %target-swift-frontend -typecheck -emit-module-interface-path %t.swiftinterface -enable-library-evolution %s
// RUN: %FileCheck %s < %t.swiftinterface

// CHECK: func hasClosureDefaultArgWithComplexNestedPoundIfs(_ x: () -> Swift.Void = {
// CHECK-NOT: #if NOT_PROVIDED
// CHECK-NOT: print("should not exist")
// CHECK-NOT: #elseif !NOT_PROVIDED
// CHECK: let innerClosure = {
// CHECK-NOT: #if false
// CHECK-NOT: print("should also not exist")
// CHECK-NOT: #else
// CHECK: print("should exist")
// CHECK-NOT: #endif
// CHECK: }
// CHECK-NOT: #endif
// CHECK: })
public func hasClosureDefaultArgWithComplexNestedPoundIfs(_ x: () -> Void = {
  #if NOT_PROVIDED
    print("should not exist")
  #elseif !NOT_PROVIDED
    let innerClosure = {
      #if false
        print("should also not exist")
      #else
        print("should exist")
      #endif
    }
  #endif
}) {
}

// CHECK: func hasClosureDefaultArgWithComplexPoundIf(_ x: () -> Swift.Void = {
// CHECK-NOT: #if NOT_PROVIDED
// CHECK-NOT: print("should not exist")
// CHECK-NOT: #else
// CHECK-NOT: #if NOT_PROVIDED
// CHECK-NOT: print("should also not exist")
// CHECK-NOT: #else
// CHECK: print("should exist"){{$}}
// CHECK-NOT: #if !second
// CHECK: print("should also exist"){{$}}
// CHECK-NOT: #endif
// CHECK-NEXT: })
public func hasClosureDefaultArgWithComplexPoundIf(_ x: () -> Void = {
  #if NOT_PROVIDED
    print("should not exist")
    #else
      #if NOT_PROVIDED
        print("should also not exist")
      #else
        print("should exist")
      #endif
    #endif

    #if !second
      print("should also exist")
    #endif
}) {
}

// CHECK: func hasClosureDefaultArgWithMultilinePoundIfCondition(_ x: () -> Swift.Void = {
// CHECK-NOT: #if (
// CHECK-NOT:   !false && true
// CHECK-NOT: )
// CHECK: print("should appear")
// CHECK-NOT: #endif
// CHECK-NOT: #if (
// CHECK-NOT:   !true
// CHECK-NOT: )
// CHECK-NOT: print("should not appear")
// CHECK-NOT: #else
// CHECK: print("also should appear")
// CHECK-NOT: #endif
// CHECK-NEXT: })
public func hasClosureDefaultArgWithMultilinePoundIfCondition(_ x: () -> Void = {
  #if (
    !false && true
  )
  print("should appear")
  #endif

  #if (
    !true
  )
  print("should not appear")
  #else
  print("also should appear")
  #endif
}) {
}

// CHECK: func hasClosureDefaultArgWithSinglePoundIf(_ x: () -> Swift.Void = {
// CHECK-NOT: #if true
// CHECK: print("true")
// CHECK-NOT: #else
// CHECK-NOT: print("false")
// CHECK-NOT: #endif
// CHECK-NEXT: })
public func hasClosureDefaultArgWithSinglePoundIf(_ x: () -> Void = {
  #if true
  print("true")
  #else
  print("false")
  #endif
}) {
}

// CHECK: func hasIfCompilerCheck
// CHECK:      #if compiler(>=5.3)
// CHECK-NEXT:   return true
// CHECK-NEXT: #else
// CHECK-NEXT:   return false
// CHECK-NEXT: #endif
@_alwaysEmitIntoClient
public func hasIfCompilerCheck(_ x: () -> Bool = {
#if compiler(>=5.3)
  return true
#else
  return false
#endif
  }) {
}
