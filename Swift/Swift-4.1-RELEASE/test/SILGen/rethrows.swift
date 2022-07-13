// RUN: %target-swift-frontend -enable-sil-ownership -emit-sil -verify %s | %FileCheck %s

@discardableResult
func rethrower(_ fn: () throws -> Int) rethrows -> Int {
  return try fn()
}
func thrower() throws -> Int { return 0 }
func nonthrower() -> Int { return 0 }

// CHECK-LABEL: sil hidden @_T08rethrows5test0yyKF : $@convention(thin) () -> @error Error {
// CHECK:       [[THROWER:%.*]] = function_ref @_T08rethrows7throwerSiyKF : $@convention(thin) () -> (Int, @error Error)
// CHECK:       [[T0:%.*]] = thin_to_thick_function [[THROWER]]
// CHECK:       [[CVT:%.*]] = convert_function [[T0]]
// CHECK:       [[RETHROWER:%.*]] = function_ref @_T08rethrows9rethrowerS2iyKcKF : $@convention(thin) (@owned @noescape @callee_guaranteed () -> (Int, @error Error)) -> (Int, @error Error)
// CHECK:       try_apply [[RETHROWER]]([[CVT]]) : $@convention(thin) (@owned @noescape @callee_guaranteed () -> (Int, @error Error)) -> (Int, @error Error), normal [[NORMAL:bb1]], error [[ERROR:bb2]]
// CHECK:     [[NORMAL]]([[T0:%.*]] : $Int):
// CHECK-NEXT:  [[T1:%.*]] = tuple ()
// CHECK-NEXT:  return [[T1]]
// CHECK:     [[ERROR]]([[T0:%.*]] : $Error):
// CHECK-NEXT:  throw [[T0]]
func test0() throws {
  try rethrower(thrower)
}

// CHECK-LABEL: sil hidden @_T08rethrows5test1yyKF : $@convention(thin) () -> @error Error {
// CHECK:       [[CLOSURE:%.*]] = function_ref @_T08rethrows5test1yyKFSiyKcfU_ : $@convention(thin) () -> (Int, @error Error)
// CHECK:       [[CVT:%.*]] = convert_function [[CLOSURE]]
// CHECK:       [[T0:%.*]] = thin_to_thick_function [[CVT]]
// CHECK:       [[RETHROWER:%.*]] = function_ref @_T08rethrows9rethrowerS2iyKcKF : $@convention(thin) (@owned @noescape @callee_guaranteed () -> (Int, @error Error)) -> (Int, @error Error)
// CHECK:       try_apply [[RETHROWER]]([[T0]]) : $@convention(thin) (@owned @noescape @callee_guaranteed () -> (Int, @error Error)) -> (Int, @error Error), normal [[NORMAL:bb1]], error [[ERROR:bb2]]
// CHECK:     [[NORMAL]]([[T0:%.*]] : $Int):
// CHECK-NEXT:  [[T1:%.*]] = tuple ()
// CHECK-NEXT:  return [[T1]]
// CHECK:     [[ERROR]]([[T0:%.*]] : $Error):
// CHECK-NEXT:  throw [[T0]]
//   Closure.
// CHECK-LABEL: sil private @_T08rethrows5test1yyKFSiyKcfU_ : $@convention(thin) () -> (Int, @error Error) {
// CHECK:       [[THROWER:%.*]] = function_ref @_T08rethrows7throwerSiyKF : $@convention(thin) () -> (Int, @error Error)
// CHECK:       [[T0:%.*]] = thin_to_thick_function [[THROWER]]
// CHECK:       [[CVT:%.*]] = convert_function [[T0]]
// CHECK:       [[RETHROWER:%.*]] = function_ref @_T08rethrows9rethrowerS2iyKcKF : $@convention(thin) (@owned @noescape @callee_guaranteed () -> (Int, @error Error)) -> (Int, @error Error)
// CHECK:       try_apply [[RETHROWER]]([[CVT]]) : $@convention(thin) (@owned @noescape @callee_guaranteed () -> (Int, @error Error)) -> (Int, @error Error), normal [[NORMAL:bb1]], error [[ERROR:bb2]]
// CHECK:     [[NORMAL]]([[T0:%.*]] : $Int):
// CHECK-NEXT:  return [[T0]]
// CHECK:     [[ERROR]]([[T0:%.*]] : $Error):
// CHECK-NEXT:  throw [[T0]]
func test1() throws {
  try rethrower { try rethrower(thrower) }
}

// CHECK-LABEL: sil hidden @_T08rethrows5test2yyF : $@convention(thin) () -> () {
// CHECK:       [[NONTHROWER:%.*]] = function_ref @_T08rethrows10nonthrowerSiyF : $@convention(thin) () -> Int
// CHECK:       [[T0:%.*]] = thin_to_thick_function [[NONTHROWER]]
// CHECK:       [[T1:%.*]] = convert_function [[T0]] : $@callee_guaranteed () -> Int to $@noescape @callee_guaranteed () -> (Int, @error Error)
// CHECK:       [[RETHROWER:%.*]] = function_ref @_T08rethrows9rethrowerS2iyKcKF : $@convention(thin) (@owned @noescape @callee_guaranteed () -> (Int, @error Error)) -> (Int, @error Error)
// CHECK:       try_apply [[RETHROWER]]([[T1]]) : $@convention(thin) (@owned @noescape @callee_guaranteed () -> (Int, @error Error)) -> (Int, @error Error), normal [[NORMAL:bb1]], error [[ERROR:bb2]]
// CHECK:     [[NORMAL]]([[T0:%.*]] : $Int):
// CHECK-NEXT:  [[T1:%.*]] = tuple ()
// CHECK-NEXT:  return [[T1]]
// CHECK:     [[ERROR]]([[T0:%.*]] : $Error):
// CHECK-NEXT:  unreachable
func test2() {
  rethrower(nonthrower)
}

// CHECK-LABEL: sil hidden @_T08rethrows5test3yyF : $@convention(thin) () -> () {
// CHECK:       [[CLOSURE:%.*]] = function_ref @_T08rethrows5test3yyFSiycfU_ : $@convention(thin) () -> Int
// CHECK:       [[CVT:%.*]] = convert_function [[CLOSURE]] : $@convention(thin) () -> Int to $@convention(thin) @noescape () -> Int
// CHECK:       [[T0:%.*]] = thin_to_thick_function [[CVT]]
// CHECK:       [[T1:%.*]] = convert_function [[T0]] : $@noescape @callee_guaranteed () -> Int to $@noescape @callee_guaranteed () -> (Int, @error Error)
// CHECK:       [[RETHROWER:%.*]] = function_ref @_T08rethrows9rethrowerS2iyKcKF : $@convention(thin) (@owned @noescape @callee_guaranteed () -> (Int, @error Error)) -> (Int, @error Error)
// CHECK:       try_apply [[RETHROWER]]([[T1]]) : $@convention(thin) (@owned @noescape @callee_guaranteed () -> (Int, @error Error)) -> (Int, @error Error), normal [[NORMAL:bb1]], error [[ERROR:bb2]]
// CHECK:     [[NORMAL]]([[T0:%.*]] : $Int):
// CHECK-NEXT:  [[T1:%.*]] = tuple ()
// CHECK-NEXT:  return [[T1]]
// CHECK:     [[ERROR]]([[T0:%.*]] : $Error):
// CHECK-NEXT:  unreachable
// CHECK-LABEL: // end sil function '_T08rethrows5test3yyF'
func test3() {
  rethrower { rethrower(nonthrower) }
}
