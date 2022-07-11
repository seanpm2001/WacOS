import _Differentiation

protocol Protocol1: Differentiable {
  // expected-note @+2 {{protocol requires function 'internalMethod1' with type '(Float) -> Float'}}
  @differentiable(reverse, wrt: (self, x))
  func internalMethod1(_ x: Float) -> Float

  // expected-note @+3 {{protocol requires function 'internalMethod2' with type '(Float) -> Float'}}
  @differentiable(reverse, wrt: x)
  @differentiable(reverse, wrt: (self, x))
  func internalMethod2(_ x: Float) -> Float

  // expected-note @+3 {{protocol requires function 'internalMethod3' with type '(Float) -> Float'}}
  @differentiable(reverse, wrt: x)
  @differentiable(reverse, wrt: (self, x))
  func internalMethod3(_ x: Float) -> Float
}

public protocol Protocol2: Differentiable {
  @differentiable(reverse, wrt: self)
  @differentiable(reverse, wrt: (self, x))
  func internalMethod4(_ x: Float) -> Float
}

// Note:
// - No `ConformingStruct: Protocol1` conformance exists in this file, so this
//   file should compile just file.
// - A `ConformingStruct: Protocol1` conformance in a different file should be
//   diagnosed to prevent linker errors. Without a diagnostic, compilation of
//   the other file creates external references to symbols for implicit
//   `@differentiable` attributes, even though no such symbols exist.
//   Context: https://github.com/apple/swift/pull/29771#issuecomment-585059721

struct ConformingStruct: Differentiable {
  // Error for missing `@differentiable` attribute.
  // expected-note @+1 {{candidate is missing explicit '@differentiable(reverse)' attribute to satisfy requirement 'internalMethod1' (in protocol 'Protocol1'); explicit attribute is necessary because candidate is declared in a different type context or file than the conformance of 'ConformingStruct' to 'Protocol1'}} {{3-3=@differentiable(reverse) }}
  func internalMethod1(_ x: Float) -> Float {
    x
  }

  // Error for missing `@differentiable` superset attribute.
  // expected-note @+2 {{candidate is missing explicit '@differentiable(reverse)' attribute to satisfy requirement 'internalMethod2' (in protocol 'Protocol1'); explicit attribute is necessary because candidate is declared in a different type context or file than the conformance of 'ConformingStruct' to 'Protocol1'}} {{3-3=@differentiable(reverse) }}
  @differentiable(reverse, wrt: x)
  func internalMethod2(_ x: Float) -> Float {
    x
  }

  // Error for missing `@differentiable` subset attribute.
  // expected-note @+2 {{candidate is missing explicit '@differentiable(reverse, wrt: x)' attribute to satisfy requirement 'internalMethod3' (in protocol 'Protocol1'); explicit attribute is necessary because candidate is declared in a different type context or file than the conformance of 'ConformingStruct' to 'Protocol1'}} {{3-3=@differentiable(reverse, wrt: x) }}
  @differentiable(reverse, wrt: (self, x))
  func internalMethod3(_ x: Float) -> Float {
    x
  }
}
