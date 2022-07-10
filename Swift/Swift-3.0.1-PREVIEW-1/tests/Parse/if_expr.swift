// RUN: %target-swift-frontend -dump-ast %s 2>&1 | %FileCheck %s

// CHECK: (func_decl "r13756261(_:_:)"
func r13756261(_ x: Bool, _ y: Int) -> Int {
  // CHECK: (if_expr
  // CHECK:   (call_expr
  // CHECK:   (declref_expr
  // CHECK:   (if_expr
  // CHECK:     (call_expr
  // CHECK:     (declref_expr
  // CHECK:     (if_expr
  // CHECK:       (call_expr
  // CHECK:       (declref_expr
  // CHECK:       (declref_expr
  return (x) ? y : (x) ? y : (x) ? y : y
}

// CHECK: (func_decl "r13756221(_:_:)"
func r13756221(_ x: Bool, _ y: Int) -> Int {
  // CHECK: (if_expr
  // CHECK:   (call_expr
  // CHECK:   (declref_expr
  // CHECK:   (if_expr
  // CHECK:     (call_expr
  // CHECK:     (declref_expr
  // CHECK:     (if_expr
  // CHECK:       (call_expr
  // CHECK:       (declref_expr
  // CHECK:       (declref_expr
  return (x) ? y
       : (x) ? y
       : (x) ? y
       : y
}

// CHECK: (func_decl "telescoping_if(_:_:)"
func telescoping_if(_ x: Bool, _ y: Int) -> Int {
  // CHECK: (if_expr
  // CHECK:   (call_expr
  // CHECK:   (if_expr
  // CHECK:     (call_expr
  // CHECK:     (if_expr
  // CHECK:       (call_expr
  // CHECK:       (declref_expr
  // CHECK:       (declref_expr
  // CHECK:     (declref_expr
  // CHECK:   (declref_expr
  return (x) ? (x) ? (x) ? y : y : y : y
}

// Operator with precedence above ? :
infix operator +>> : Rightwards
precedencegroup Rightwards {
  associativity: left
  higherThan: TernaryPrecedence
}

// Operator with precedence below ? :
infix operator +<< : Leftwards
precedencegroup Leftwards {
  associativity: left
  lowerThan: TernaryPrecedence
}

// Operator with precedence equal to ? :
infix operator +== : TernaryPrecedence

func +>> (x: Bool, y: Bool) -> Bool {}
func +<< (x: Bool, y: Bool) -> Bool {}
func +== (x: Bool, y: Bool) -> Bool {}

// CHECK: (func_decl "prec_above(_:_:_:)"
func prec_above(_ x: Bool, _ y: Bool, _ z: Bool) -> Bool {
  // (x +>> y) ? (y +>> z) : ((x +>> y) ? (y +>> z) : (x +>> y))
  // CHECK: (if_expr
  // CHECK:   (binary_expr
  // CHECK:   (binary_expr
  // CHECK:   (if_expr
  // CHECK:     (binary_expr
  // CHECK:     (binary_expr
  // CHECK:     (binary_expr
  return x +>> y ? y +>> z : x +>> y ? y +>> z : x +>> y
}

// CHECK: (func_decl "prec_below(_:_:_:)"
func prec_below(_ x: Bool, _ y: Bool, _ z: Bool) -> Bool {
  // The middle arm of the ternary is max-munched, so this is:
  // ((x +<< (y ? (y +<< z) : x)) +<< (y ? (y +<< z) : x)) +<< y
  // CHECK: (binary_expr
  // CHECK:   (binary_expr
  // CHECK:     (binary_expr
  // CHECK:       (declref_expr
  // CHECK:       (if_expr
  // CHECK:         (call_expr
  // CHECK:         (binary_expr
  // CHECK:         (declref_expr
  // CHECK:     (if_expr
  // CHECK:       (call_expr
  // CHECK:       (binary_expr
  // CHECK:       (declref_expr
  // CHECK:   (declref_expr
  return x +<< y ? y +<< z : x +<< y ? y +<< z : x +<< y
}

// CHECK: (func_decl "prec_equal(_:_:_:)"
func prec_equal(_ x: Bool, _ y: Bool, _ z: Bool) -> Bool {
  // The middle arm of the ternary is max-munched, so this is:
  // x +== (y ? (y +== z) : (x +== (y ? (y +== z) : (x +== y))))
  // CHECK: (binary_expr
  // CHECK:   (declref_expr
  // CHECK:   (if_expr
  // CHECK:     (call_expr
  // CHECK:     (binary_expr
  // CHECK:       (declref_expr
  // CHECK:       (declref_expr
  // CHECK:     (binary_expr
  // CHECK:       (declref_expr
  // CHECK:       (if_expr
  // CHECK:         (call_expr
  // CHECK:         (binary_expr
  // CHECK:           (declref_expr
  // CHECK:           (declref_expr
  // CHECK:         (binary_expr
  // CHECK:           (declref_expr
  // CHECK:           (declref_expr
  return x +== y ? y +== z : x +== y ? y +== z : x +== y
}

