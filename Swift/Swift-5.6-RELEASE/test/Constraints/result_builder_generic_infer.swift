// RUN: %target-swift-frontend -dump-ast %s | %FileCheck %s

protocol P {
  associatedtype A

  @Builder<A>
  var x1: [S] { get }
    
  @Builder<Self>
  var x2: [S] { get }
}

@resultBuilder
enum Builder<T> {
  static func buildBlock(_ args: S...) -> [S] { args }
}

struct S {}

// CHECK: struct_decl{{.*}}ProtocolSubstitution
struct ProtocolSubstitution: P {
  typealias A = Int

  // CHECK: var_decl{{.*}}x1
  // CHECK: Builder.buildBlock{{.*}}(substitution_map generic_signature=<T> (substitution T -> Int))
  var x1: [S] { S() }

  // CHECK: var_decl{{.*}}x2
  // CHECK: Builder.buildBlock{{.*}}(substitution_map generic_signature=<T> (substitution T -> ProtocolSubstitution))
  var x2: [S] { S() }
}

// CHECK: struct_decl{{.*}}ArchetypeSubstitution
struct ArchetypeSubstitution<A>: P {
  // CHECK: var_decl{{.*}}x1
  // CHECK: Builder.buildBlock{{.*}}(substitution_map generic_signature=<T> (substitution T -> A))
  var x1: [S] { S() }

  // CHECK: var_decl{{.*}}x2
  // CHECK: Builder.buildBlock{{.*}}(substitution_map generic_signature=<T> (substitution T -> ArchetypeSubstitution<A>))
  var x2: [S] { S() }
}

// CHECK: struct_decl{{.*}}ConcreteTypeSubstitution
struct ConcreteTypeSubstitution<Value> {}

extension ConcreteTypeSubstitution: P where Value == Int {
  typealias A = Value

  // CHECK: var_decl{{.*}}x1
  // CHECK: Builder.buildBlock{{.*}}(substitution_map generic_signature=<T> (substitution T -> Int))
  var x1: [S] { S() }

  // CHECK: var_decl{{.*}}x2
  // CHECK: Builder.buildBlock{{.*}}(substitution_map generic_signature=<T> (substitution T -> ConcreteTypeSubstitution<Int>))
  var x2: [S] { S() }
}
