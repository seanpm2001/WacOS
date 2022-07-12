// RUN: %target-swift-emit-silgen -emit-verbose-sil %s | %FileCheck %s

// Test that the attribute gets set on default argument generators.

// CHECK-LABEL: sil hidden [transparent] [ossa] @$s21transparent_attribute0A23FuncWithDefaultArgument1xS2i_tFfA_ : $@convention(thin) () -> Int {

// CHECK-LABEL: sil hidden [transparent] [ossa] @$s21transparent_attribute0A23FuncWithDefaultArgument1xS2i_tF : $@convention(thin) (Int) -> Int {

@_transparent func transparentFuncWithDefaultArgument (x: Int = 1) -> Int {
  return x
}
func useTransparentFuncWithDefaultArgument() -> Int {
  return transparentFuncWithDefaultArgument()
}
