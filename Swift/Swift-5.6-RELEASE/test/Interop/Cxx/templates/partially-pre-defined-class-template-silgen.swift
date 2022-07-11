// RUN: %target-swift-emit-sil %s -I %S/Inputs -enable-cxx-interop | %FileCheck %s

import PartiallyPreDefinedClassTemplate

public func getWrappedMagicInt() -> CInt {
  let myInt = IntWrapper(value: 21)
  let magicInt = PartiallyPreDefinedMagicallyWrappedInt(t: myInt)
  return magicInt.getValuePlusArg(32)
}

// CHECK: // getWrappedMagicInt()
// CHECK: sil @$s4main18getWrappedMagicInts5Int32VyF : $@convention(thin) () -> Int32 {
// CHECK: [[INT_WRAPPER:%.*]] = struct $IntWrapper ([[_:%.*]] : $Int32)
// CHECK: [[_:%.*]] = struct $__CxxTemplateInst12MagicWrapperI10IntWrapperE ([[INT_WRAPPER]] : $IntWrapper)
// CHECK: // function_ref {{_ZNK12MagicWrapperI10IntWrapperE15getValuePlusArgEi|\?getValuePlusArg@\?\$MagicWrapper@UIntWrapper@@@@QEBAHH@Z}}
// CHECK: [[_:%.*]] = function_ref @{{_ZNK12MagicWrapperI10IntWrapperE15getValuePlusArgEi|\?getValuePlusArg@\?\$MagicWrapper@UIntWrapper@@@@QEBAHH@Z}} : $@convention(c) (@in __CxxTemplateInst12MagicWrapperI10IntWrapperE, Int32) -> Int32

// CHECK: // {{_ZNK12MagicWrapperI10IntWrapperE15getValuePlusArgEi|\?getValuePlusArg@\?\$MagicWrapper@UIntWrapper@@@@QEBAHH@Z}}
// CHECK: MagicWrapper<IntWrapper>::getValuePlusArg

// CHECK: sil [clang __CxxTemplateInst12MagicWrapperI10IntWrapperE.getValuePlusArg] @{{_ZNK12MagicWrapperI10IntWrapperE15getValuePlusArgEi|\?getValuePlusArg@\?\$MagicWrapper@UIntWrapper@@@@QEBAHH@Z}} : $@convention(c) (@in __CxxTemplateInst12MagicWrapperI10IntWrapperE, Int32) -> Int32
