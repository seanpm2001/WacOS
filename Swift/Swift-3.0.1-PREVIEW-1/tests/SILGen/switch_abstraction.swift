// RUN: %target-swift-frontend -emit-silgen -parse-stdlib %s | %FileCheck %s

struct A {}

enum Optionable<T> {
  case Summn(T)
  case Nuttn
}

// CHECK-LABEL: sil hidden @_TF18switch_abstraction18enum_reabstractionFT1xGOS_10OptionableFVS_1AS1__1aS1__T_ : $@convention(thin) (@owned Optionable<(A) -> A>, A) -> ()
// CHECK: switch_enum {{%.*}} : $Optionable<(A) -> A>, case #Optionable.Summn!enumelt.1: [[DEST:bb[0-9]+]]
// CHECK: [[DEST]]([[ORIG:%.*]] : $@callee_owned (@in A) -> @out A):
// CHECK:   [[REABSTRACT:%.*]] = function_ref @_TTR
// CHECK:   [[SUBST:%.*]] = partial_apply [[REABSTRACT]]([[ORIG]])
func enum_reabstraction(x x: Optionable<(A) -> A>, a: A) {
  switch x {
  case .Summn(var f):
    f(a)
  case .Nuttn:
    ()
  }
}

enum Wacky<A, B> {
  case Foo(A)
  case Bar((B) -> A)
}

// CHECK-LABEL: sil hidden @_TF18switch_abstraction45enum_addr_only_to_loadable_with_reabstraction{{.*}} : $@convention(thin) <T> (@in Wacky<T, A>, A) -> @out T {
// CHECK: switch_enum_addr [[ENUM:%.*]] : $*Wacky<T, A>, {{.*}} case #Wacky.Bar!enumelt.1: [[DEST:bb[0-9]+]]
// CHECK: [[DEST]]:
// CHECK:   [[ORIG_ADDR:%.*]] = unchecked_take_enum_data_addr [[ENUM]] : $*Wacky<T, A>, #Wacky.Bar
// CHECK:   [[ORIG:%.*]] = load [[ORIG_ADDR]]
// CHECK:   [[REABSTRACT:%.*]] = function_ref @_TTR
// CHECK:   [[SUBST:%.*]] = partial_apply [[REABSTRACT]]<T>([[ORIG]])
func enum_addr_only_to_loadable_with_reabstraction<T>(x x: Wacky<T, A>, a: A)
  -> T
{
  switch x {
  case .Foo(var b):
    return b
  case .Bar(var f):
    return f(a)
  }
}
