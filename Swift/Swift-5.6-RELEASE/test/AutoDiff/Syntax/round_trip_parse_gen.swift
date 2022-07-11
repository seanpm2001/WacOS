// RUN: rm -rf %t
// RUN: %swift-syntax-test -input-source-filename %s -parse-gen > %t
// RUN: diff -u %s %t
// RUN: %swift-syntax-test -input-source-filename %s -parse-gen -print-node-kind > %t.withkinds
// RUN: diff -u %S/Outputs/round_trip_parse_gen.swift.withkinds %t.withkinds
// RUN: %swift-syntax-test -input-source-filename %s -eof > %t
// RUN: diff -u %s %t
// RUN: %swift-syntax-test -serialize-raw-tree -input-source-filename %s > %t.dump
// RUN: %swift-syntax-test -deserialize-raw-tree -input-source-filename %t.dump -output-filename %t
// RUN: diff -u %s %t

// Note: RUN lines copied from test/Syntax/round_trip_parse_gen.swift.

@differentiable(reverse)
func bar(_ x: Float, _: Float) -> Float { return 1 }

@differentiable(reverse where T : FloatingPoint)
func bar<T : Numeric>(_ x: T, _: T) -> T { return 1 }

@differentiable(reverse, wrt: x)
func bar(_ x: Float, _: Float) -> Float { return 1 }

@differentiable(reverse, wrt: (self, x, y))
func bar(_ x: Float, y: Float) -> Float { return 1 }

@differentiable(reverse, wrt: (self, x, y) where T : FloatingPoint)
func bar<T : Numeric>(_ x: T, y: T) -> T { return 1 }

@derivative(of: -)
func negateDerivative(_ x: Float)
    -> (value: Float, pullback: (Float) -> Float) {
  return (-x, { v in -v })
}

@derivative(of: baz(label:_:), wrt: (x))
func bazDerivative(_ x: Float, y: Float)
    -> (value: Float, pullback: (Float) -> Float) {
  return (x, { v in v })
}

@derivative(of: baz(label:_:).set, wrt: (x))
func bazSetDerivative(_ x: Float, y: Float)
    -> (value: Float, pullback: (Float) -> Float) {
  return (x, { v in v })
}

@derivative(of: baz(label:_:).get, wrt: (x))
func bazGetDerivative(_ x: Float, y: Float)
    -> (value: Float, pullback: (Float) -> Float) {
  return (x, { v in v })
}

@transpose(of: -)
func negateDerivative(_ x: Float)
    -> (value: Float, pullback: (Float) -> Float) {
  return (-x, { v in -v })
}

@derivative(of: baz(label:_:), wrt: (x))
func bazDerivative(_ x: Float, y: Float)
    -> (value: Float, pullback: (Float) -> Float) {
  return (x, { v in v })
}

@derivative(of: A<T>.B<U, V>.C.foo(label:_:), wrt: x)
func qualifiedDerivative(_ x: Float, y: Float)
    -> (value: Float, pullback: (Float) -> Float) {
  return (x, { v in v })
}

@derivative(of: A<T>.B<U, V>.C.foo(label:_:).get, wrt: x)
func qualifiedGetDerivative(_ x: Float, y: Float)
    -> (value: Float, pullback: (Float) -> Float) {
  return (x, { v in v })
}

@derivative(of: A<T>.B<U, V>.C.foo(label:_:).set, wrt: x)
func qualifiedSetDerivative(_ x: Float, y: Float)
    -> (value: Float, pullback: (Float) -> Float) {
  return (x, { v in v })
}

@transpose(of: +)
func addTranspose(_ v: Float) -> (Float, Float) {
  return (v, v)
}

@transpose(of: -, wrt: (0, 1))
func subtractTranspose(_ v: Float) -> (Float, Float) {
  return (v, -v)
}

@transpose(of: Float.-, wrt: (0, 1))
func subtractTranspose(_ v: Float) -> (Float, Float) {
  return (v, -v)
}

@transpose(of: Float.-.get, wrt: (0, 1))
func subtractGetTranspose(_ v: Float) -> (Float, Float) {
  return (v, -v)
}

@transpose(of: Float.-.set, wrt: (0, 1))
func subtractSetTranspose(_ v: Float) -> (Float, Float) {
  return (v, -v)
}

@derivative(of: A<T>.B<U, V>.C.foo(label:_:), wrt: 0)
func qualifiedTranspose(_ v: Float) -> (Float, Float) {
  return (v, -v)
}

@transpose(of: A<T>.B<U, V>.C.foo(label:_:).get, wrt: 0)
func qualifiedGetTranspose(_ v: Float) -> (Float, Float) {
  return (v, -v)
}

@derivative(of: subscript(_:).set, wrt: (self, newValue))
func subscriptSetterDerivative() {}

@transpose(of: subscript(_:).set, wrt: (self, 0)
func subscriptSetterTranspose() {}
