// RUN: %target-swift-frontend -emit-silgen -enable-sil-ownership %s | %FileCheck %s

// CHECK-LABEL: sil hidden [thunk] @pear : $@convention(c)
// CHECK:         function_ref @_T05cdecl5apple{{[_0-9a-zA-Z]*}}F
// CHECK-LABEL: sil hidden @_T05cdecl5apple{{[_0-9a-zA-Z]*}}F
@_cdecl("pear")
func apple(_ f: @convention(c) (Int) -> Int) {
}

// CHECK-LABEL: sil hidden @_T05cdecl16forceCEntryPoint{{[_0-9a-zA-Z]*}}F
// CHECK:         function_ref @grapefruit
func forceCEntryPoint() {
  apple(orange)
}

// CHECK-LABEL: sil hidden [thunk] @grapefruit : $@convention(c)
// CHECK:         function_ref @_T05cdecl6orange{{[_0-9a-zA-Z]*}}F
// CHECK-LABEL: sil hidden @_T05cdecl6orange{{[_0-9a-zA-Z]*}}F
@_cdecl("grapefruit")
func orange(_ x: Int) -> Int {
  return x
}

// CHECK-LABEL: sil [thunk] @cauliflower : $@convention(c)
// CHECK:         function_ref @_T05cdecl8broccoli{{[_0-9a-zA-Z]*}}F
// CHECK-LABEL: sil @_T05cdecl8broccoli{{[_0-9a-zA-Z]*}}F
@_cdecl("cauliflower")
public func broccoli(_ x: Int) -> Int {
  return x
}

// CHECK-LABEL: sil private [thunk] @collard_greens : $@convention(c)
// CHECK:         function_ref @_T05cdecl4kale[[PRIVATE:.*]]
// CHECK:       sil private @_T05cdecl4kale[[PRIVATE:.*]]
@_cdecl("collard_greens")
private func kale(_ x: Int) -> Int {
  return x
}

/* TODO: Handle error conventions
@_cdecl("vomits")
func barfs() throws {}
 */
