// RUN: %empty-directory(%t) && %target-swift-frontend -c -update-code -primary-file %s -emit-migrated-file-path %t/prefix_typeof_expr.swift.result -emit-remap-file-path %t/prefix_typeof_expr.swift.remap -o /dev/null
// RUN: diff -u %S/prefix_typeof_expr.swift.expected %t/prefix_typeof_expr.swift.result

class HasTypeVar {
  var type: Int {
    precondition(true, "\(type(of: self)) should never be asked for its type")
    _ = Swift.type(of: 1) // Don't add another prefix to this one
    return 1
  }
}

class HasTypeMethod {
  func type<T>(of: T) -> Int {
    _ = type(of: 1)
    _ = Swift.type(of: 1) // Don't add another prefix to this one
    return 1
  }
}

func type<T>(of: T) -> Int {
  return 1
}

_ = type(of: 1)
_ = Swift.type(of: 1) // Don't add another prefix to this one
